#include "data/worksheet_data.h"

#include <cstdlib>
#include <memory>
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
using chat_excel::file_service::WorkSheetData;
using chat_excel::file_service::WorkSheetInfo;

namespace
{

// WorkSheet 缓存 hash 类型的 key(与 WorkSheetData 实现保持一致)
constexpr const char* kWorkSheetCacheKey = "worksheet_data";

// WorkSheet 缓存过期时间(秒), 3 天
constexpr int kWorkSheetCacheExpireTime = 3 * 24 * 60 * 60;

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
 * @brief 校验实际查询到的 WorkSheet 信息与期望的 WorkSheet 信息一致
 * @param actual 实际查询到的 WorkSheet 信息
 * @param expected 期望的 WorkSheet 信息
 */
void ExpectWorkSheetInfoEqual(const WorkSheetInfo& actual, const WorkSheetInfo& expected)
{
    EXPECT_EQ(actual.file_id, expected.file_id);
    EXPECT_EQ(actual.worksheet_name, expected.worksheet_name);
    EXPECT_EQ(actual.table_name, expected.table_name);
}

} // namespace

// WorkSheet 数据访问类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class WorkSheetDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_worksheet_info");
        transaction.commit();
        GetRedisHandle()->del(kWorkSheetCacheKey);

        worksheet_data_ = std::make_unique<WorkSheetData>(GetMysqlHandle(), GetRedisHandle());

        // 构造测试 WorkSheet 信息列表, 一个文件对应两个 WorkSheet, 字符串字段长度受表结构 VARCHAR 限制
        test_file_id_ = "fid_for_test";
        test_worksheet_list_ = {
            {"fid_for_test", "Sheet1", "tbl_excel_fid_for_test_sheet1"},
            {"fid_for_test", "Sheet2", "tbl_excel_fid_for_test_sheet2"},
        };
    }

    std::unique_ptr<WorkSheetData> worksheet_data_;
    std::string test_file_id_;
    std::vector<WorkSheetInfo> test_worksheet_list_;
};

// ==================== MySQL 操作测试 ====================

// 正常情况: 批量保存后通过文件 ID 查询, 各字段与保存的数据一致
TEST_F(WorkSheetDataTest, SaveWorkSheetsThenGetByFileId)
{
    worksheet_data_->SaveWorkSheets(test_file_id_, test_worksheet_list_);

    std::vector<WorkSheetInfo> worksheet_list = worksheet_data_->GetWorkSheetListByFileId(test_file_id_);
    ASSERT_EQ(worksheet_list.size(), test_worksheet_list_.size());
    for (size_t i = 0; i < worksheet_list.size(); ++i)
    {
        ExpectWorkSheetInfoEqual(worksheet_list[i], test_worksheet_list_[i]);
    }
}

// 正常情况: 通过文件 ID 查询只返回该文件对应的 WorkSheet, 不包含其他文件的数据
TEST_F(WorkSheetDataTest, GetWorkSheetListByFileIdOnlyReturnOwnFile)
{
    worksheet_data_->SaveWorkSheets(test_file_id_, test_worksheet_list_);

    std::vector<WorkSheetInfo> other_file_list = {{"fid_other_file", "Sheet1", "tbl_excel_other_sheet1"}};
    worksheet_data_->SaveWorkSheets("fid_other_file", other_file_list);

    std::vector<WorkSheetInfo> worksheet_list = worksheet_data_->GetWorkSheetListByFileId(test_file_id_);
    ASSERT_EQ(worksheet_list.size(), test_worksheet_list_.size());
    for (size_t i = 0; i < worksheet_list.size(); ++i)
    {
        ExpectWorkSheetInfoEqual(worksheet_list[i], test_worksheet_list_[i]);
    }
}

// 边界情况: 查询没有 WorkSheet 信息的文件返回空列表
TEST_F(WorkSheetDataTest, GetWorkSheetListByFileIdReturnEmptyWhenNotExists)
{
    std::vector<WorkSheetInfo> worksheet_list = worksheet_data_->GetWorkSheetListByFileId("not_exists_file_id");
    EXPECT_TRUE(worksheet_list.empty());
}

// 正常情况: 删除后通过文件 ID 查询返回空列表
TEST_F(WorkSheetDataTest, DeleteWorkSheetThenGetReturnEmpty)
{
    worksheet_data_->SaveWorkSheets(test_file_id_, test_worksheet_list_);
    worksheet_data_->DeleteWorkSheetByFileId(test_file_id_);

    std::vector<WorkSheetInfo> worksheet_list = worksheet_data_->GetWorkSheetListByFileId(test_file_id_);
    EXPECT_TRUE(worksheet_list.empty());
}

// 正常情况: 删除指定文件的 WorkSheet 不影响其他文件的数据
TEST_F(WorkSheetDataTest, DeleteWorkSheetNotAffectOtherFile)
{
    worksheet_data_->SaveWorkSheets(test_file_id_, test_worksheet_list_);

    std::vector<WorkSheetInfo> other_file_list = {{"fid_other_file", "Sheet1", "tbl_excel_other_sheet1"}};
    worksheet_data_->SaveWorkSheets("fid_other_file", other_file_list);
    worksheet_data_->DeleteWorkSheetByFileId(test_file_id_);

    std::vector<WorkSheetInfo> worksheet_list = worksheet_data_->GetWorkSheetListByFileId("fid_other_file");
    ASSERT_EQ(worksheet_list.size(), 1);
    ExpectWorkSheetInfoEqual(worksheet_list[0], other_file_list[0]);
}

// 边界情况: 删除不存在的文件对应的 WorkSheet 不抛出异常
TEST_F(WorkSheetDataTest, DeleteWorkSheetWhenNotExists)
{
    EXPECT_NO_THROW(worksheet_data_->DeleteWorkSheetByFileId("not_exists_file_id"));
}

// 边界情况: 批量保存空列表不抛出异常
TEST_F(WorkSheetDataTest, SaveEmptyWorkSheetList)
{
    EXPECT_NO_THROW(worksheet_data_->SaveWorkSheets(test_file_id_, {}));
}

// ==================== Redis 操作测试 ====================

// 正常情况: 保存缓存后通过文件 ID 查询, 各字段与保存的数据一致
TEST_F(WorkSheetDataTest, SaveWorkSheetToCacheThenGetByFileId)
{
    worksheet_data_->SaveWorkSheetToCache(test_file_id_, test_worksheet_list_);

    std::vector<WorkSheetInfo> worksheet_list =
        worksheet_data_->GetWorkSheetListByFileIdFromCache(test_file_id_);
    ASSERT_EQ(worksheet_list.size(), test_worksheet_list_.size());
    for (size_t i = 0; i < worksheet_list.size(); ++i)
    {
        ExpectWorkSheetInfoEqual(worksheet_list[i], test_worksheet_list_[i]);
    }
}

// 异常情况: 缓存未命中时通过文件 ID 查询返回空列表
TEST_F(WorkSheetDataTest, GetWorkSheetFromCacheReturnEmptyWhenNotExists)
{
    std::vector<WorkSheetInfo> worksheet_list =
        worksheet_data_->GetWorkSheetListByFileIdFromCache("not_exists_file_id");
    EXPECT_TRUE(worksheet_list.empty());
}

// 正常情况: 删除缓存后通过文件 ID 查询返回空列表
TEST_F(WorkSheetDataTest, DeleteWorkSheetFromCacheThenGetReturnEmpty)
{
    worksheet_data_->SaveWorkSheetToCache(test_file_id_, test_worksheet_list_);
    worksheet_data_->DeleteWorkSheetFromCache(test_file_id_);

    std::vector<WorkSheetInfo> worksheet_list =
        worksheet_data_->GetWorkSheetListByFileIdFromCache(test_file_id_);
    EXPECT_TRUE(worksheet_list.empty());
}

// 边界情况: 保存缓存后缓存 key 的过期时间被设置为 3 天以内
TEST_F(WorkSheetDataTest, SaveWorkSheetToCacheSetExpireTime)
{
    worksheet_data_->SaveWorkSheetToCache(test_file_id_, test_worksheet_list_);

    long long expire_time = GetRedisHandle()->ttl(kWorkSheetCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kWorkSheetCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "worksheet_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
