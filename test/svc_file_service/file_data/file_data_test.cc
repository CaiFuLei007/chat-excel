#include "data/file_data.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <gtest/gtest.h>
#include "common/exception.h"

using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;
using chat_excel::file_service::FileData;
using chat_excel::file_service::FileInfo;

namespace
{

// 文件缓存 hash 类型的 key(与 FileData 实现保持一致)
constexpr const char* kFileCacheKey = "file_data";

// 文件缓存过期时间(秒), 3 天
constexpr int kFileCacheExpireTime = 3 * 24 * 60 * 60;

/**
 * @brief 获取必填环境变量的值
 * @param name 环境变量名
 * @return 环境变量的值
 */
std::string GetRequiredEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        GTEST_LOG_(FATAL) << "环境变量 " << name << " 未设置";
    }
    return value;
}

/**
 * @brief 获取 MySQL 操作句柄(进程内单例), 配置从环境变量读取
 * @return MySQL 操作句柄
 */
std::shared_ptr<odb::database>& GetMysqlHandle()
{
    static std::shared_ptr<odb::database> handle = [] {
        cpp_toolkit::MySQLSettings settings;
        settings.database = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_DATABASE");
        settings.user = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PASSWORD");
        settings.host = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoul(GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PORT"));
        settings.charset = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_CHARSET");
        return cpp_toolkit::ODBFactory::Create(settings);
    }();
    return handle;
}

/**
 * @brief 获取 Redis 操作句柄(进程内单例), 配置从环境变量读取
 * @return Redis 操作句柄
 */
std::shared_ptr<sw::redis::Redis>& GetRedisHandle()
{
    static std::shared_ptr<sw::redis::Redis> handle = [] {
        cpp_toolkit::RedisSettings settings;
        settings.host = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PORT"));
        settings.user = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PASSWORD");
        settings.db = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_INDEX"));
        return cpp_toolkit::RedisFactory::Create(settings);
    }();
    return handle;
}

/**
 * @brief 校验实际查询到的文件信息与期望的文件信息一致
 * @param actual 实际查询到的文件信息
 * @param expected 期望的文件信息
 */
void ExpectFileInfoEqual(const FileInfo& actual, const FileInfo& expected)
{
    EXPECT_EQ(actual.file_id, expected.file_id);
    EXPECT_EQ(actual.file_name, expected.file_name);
    EXPECT_EQ(actual.file_extension, expected.file_extension);
    EXPECT_EQ(actual.file_size, expected.file_size);
    EXPECT_EQ(actual.file_upload_time, expected.file_upload_time);
    EXPECT_EQ(actual.fastdfs_file_id, expected.fastdfs_file_id);
    EXPECT_EQ(actual.user_id, expected.user_id);
    EXPECT_EQ(actual.session_id, expected.session_id);
}

} // namespace

// 文件数据访问类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class FileDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_file_info");
        transaction.commit();
        GetRedisHandle()->del(kFileCacheKey);

        file_data_ = std::make_unique<FileData>(GetMysqlHandle(), GetRedisHandle());

        // 构造测试文件信息, 字符串字段长度受表结构 VARCHAR 限制
        test_file_.file_id = "fid_for_test";
        test_file_.file_name = "test_excel.xlsx";
        test_file_.file_extension = "xlsx";
        test_file_.file_size = 10 * 1024;
        test_file_.file_upload_time = 1725000000;
        test_file_.fastdfs_file_id = "group1/M00/00/00/test_file.xlsx";
        test_file_.user_id = "uid_for_test";
        test_file_.session_id = "sid_for_test";
    }

    std::unique_ptr<FileData> file_data_;
    FileInfo test_file_;
};

// ==================== MySQL 操作测试 ====================

// 正常情况: 保存文件后通过文件 ID 查询, 各字段与保存的数据一致
TEST_F(FileDataTest, SaveFileThenGetByFileId)
{
    file_data_->SaveFile(test_file_);

    std::optional<FileInfo> file_info = file_data_->GetFileByFileId(test_file_.file_id);
    ASSERT_TRUE(file_info.has_value());
    ExpectFileInfoEqual(*file_info, test_file_);
}

// 异常情况: 查询不存在的文件 ID 返回 std::nullopt
TEST_F(FileDataTest, GetFileByFileIdReturnNulloptWhenNotExists)
{
    std::optional<FileInfo> file_info = file_data_->GetFileByFileId("not_exists_file_id");
    EXPECT_FALSE(file_info.has_value());
}

// 异常情况: 保存重复唯一键的文件抛出 ChatExcelException
TEST_F(FileDataTest, SaveDuplicateFileThrowChatExcelException)
{
    file_data_->SaveFile(test_file_);
    EXPECT_THROW(file_data_->SaveFile(test_file_), ChatExcelException);
}

// 正常情况: 更新文件后查询, 各字段为更新后的新值
TEST_F(FileDataTest, UpdateFileThenGetByFileId)
{
    file_data_->SaveFile(test_file_);

    FileInfo updated_file = test_file_;
    updated_file.file_name = "renamed_excel.xlsx";
    updated_file.file_size = 20 * 1024;
    updated_file.file_upload_time = 1725009999;
    file_data_->UpdateFile(updated_file);

    std::optional<FileInfo> file_info = file_data_->GetFileByFileId(test_file_.file_id);
    ASSERT_TRUE(file_info.has_value());
    ExpectFileInfoEqual(*file_info, updated_file);
}

// 异常情况: 更新不存在的文件抛出 FILE_DATA_NOT_FOUND
TEST_F(FileDataTest, UpdateFileWhenNotExistsThrowChatExcelException)
{
    try
    {
        file_data_->UpdateFile(test_file_);
        FAIL() << "更新不存在的文件应抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::FILE_DATA_NOT_FOUND);
    }
}

// 正常情况: 删除文件后通过文件 ID 查询返回 std::nullopt
TEST_F(FileDataTest, DeleteFileThenGetReturnNullopt)
{
    file_data_->SaveFile(test_file_);
    file_data_->DeleteFileByFileId(test_file_.file_id);

    std::optional<FileInfo> file_info = file_data_->GetFileByFileId(test_file_.file_id);
    EXPECT_FALSE(file_info.has_value());
}

// 边界情况: 删除不存在的文件不抛出异常
TEST_F(FileDataTest, DeleteFileWhenNotExists)
{
    EXPECT_NO_THROW(file_data_->DeleteFileByFileId("not_exists_file_id"));
}

// 正常情况: 通过用户 ID 获取该用户上传的全部文件列表
TEST_F(FileDataTest, GetFileListByUserId)
{
    file_data_->SaveFile(test_file_);

    FileInfo another_file = test_file_;
    another_file.file_id = "fid_for_test_2";
    another_file.file_name = "test_excel_2.xlsx";
    file_data_->SaveFile(another_file);

    // 其他用户的文件不应出现在列表中
    FileInfo other_user_file = test_file_;
    other_user_file.file_id = "fid_other_user";
    other_user_file.user_id = "uid_other_user";
    file_data_->SaveFile(other_user_file);

    std::vector<FileInfo> file_list = file_data_->GetFileListByUserId(test_file_.user_id);
    ASSERT_EQ(file_list.size(), 2);
    ExpectFileInfoEqual(file_list[0], test_file_);
    ExpectFileInfoEqual(file_list[1], another_file);
}

// 边界情况: 用户没有上传过文件时返回空列表
TEST_F(FileDataTest, GetFileListByUserIdReturnEmptyWhenNoFile)
{
    std::vector<FileInfo> file_list = file_data_->GetFileListByUserId("uid_no_file");
    EXPECT_TRUE(file_list.empty());
}

// ==================== Redis 操作测试 ====================

// 正常情况: 保存缓存后通过文件 ID 查询, 各字段与保存的数据一致
TEST_F(FileDataTest, SaveFileToCacheThenGetByFileId)
{
    file_data_->SaveFileToCache(test_file_);

    std::optional<FileInfo> file_info = file_data_->GetFileByFileIdFromCache(test_file_.file_id);
    ASSERT_TRUE(file_info.has_value());
    ExpectFileInfoEqual(*file_info, test_file_);
}

// 异常情况: 缓存未命中时通过文件 ID 查询返回 std::nullopt
TEST_F(FileDataTest, GetFileFromCacheReturnNulloptWhenNotExists)
{
    std::optional<FileInfo> file_info = file_data_->GetFileByFileIdFromCache("not_exists_file_id");
    EXPECT_FALSE(file_info.has_value());
}

// 正常情况: 删除缓存文件后通过文件 ID 查询返回 std::nullopt
TEST_F(FileDataTest, DeleteFileFromCacheThenGetReturnNullopt)
{
    file_data_->SaveFileToCache(test_file_);
    file_data_->DeleteFileByFileIdFromCache(test_file_.file_id);

    std::optional<FileInfo> file_info = file_data_->GetFileByFileIdFromCache(test_file_.file_id);
    EXPECT_FALSE(file_info.has_value());
}

// 边界情况: 保存缓存后缓存 key 的过期时间被设置为 3 天以内
TEST_F(FileDataTest, SaveFileToCacheSetExpireTime)
{
    file_data_->SaveFileToCache(test_file_);

    long long expire_time = GetRedisHandle()->ttl(kFileCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kFileCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "file_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
