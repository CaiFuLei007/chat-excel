#include "data/chat_session_data.h"

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
using chat_excel::ai_service::ChatSessionData;
using chat_excel::ai_service::ChatSessionInfo;

namespace
{

// 聊天会话缓存 hash 类型的 key(与 ChatSessionData 实现保持一致)
constexpr const char* kChatSessionCacheKey = "chat_session_data";

// 聊天会话缓存过期时间(秒), 3 天
constexpr int kChatSessionCacheExpireTime = 3 * 24 * 60 * 60;

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
 * @brief 校验实际查询到的聊天会话信息与期望的聊天会话信息一致
 * @param actual 实际查询到的聊天会话信息
 * @param expected 期望的聊天会话信息
 */
void ExpectChatSessionInfoEqual(const ChatSessionInfo& actual, const ChatSessionInfo& expected)
{
    EXPECT_EQ(actual.chat_session_id, expected.chat_session_id);
    EXPECT_EQ(actual.user_id, expected.user_id);
    EXPECT_EQ(actual.title, expected.title);
    EXPECT_EQ(actual.create_time, expected.create_time);
    EXPECT_EQ(actual.update_time, expected.update_time);
    EXPECT_EQ(actual.total_message_count, expected.total_message_count);
    EXPECT_EQ(actual.model_name, expected.model_name);
    EXPECT_EQ(actual.file_id, expected.file_id);
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.connection_info, expected.connection_info);
}

} // namespace

// 聊天会话数据访问类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class ChatSessionDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_chat_session");
        transaction.commit();
        GetRedisHandle()->del(kChatSessionCacheKey);

        chat_session_data_ = std::make_unique<ChatSessionData>(GetMysqlHandle(), GetRedisHandle());

        // 构造测试聊天会话信息(excel 类型, 关联文件), 字符串字段长度受表结构 VARCHAR 限制
        test_session_.chat_session_id = "csid_for_test";
        test_session_.user_id = "uid_for_test";
        test_session_.title = "测试会话";
        test_session_.create_time = 1725000000;
        test_session_.update_time = 1725000000;
        test_session_.total_message_count = 5;
        test_session_.model_name = "deepseek-chat";
        test_session_.file_id = "fid_for_test";
        test_session_.type = "excel";
        test_session_.connection_info = "";
    }

    std::unique_ptr<ChatSessionData> chat_session_data_;
    ChatSessionInfo test_session_;
};

// ==================== MySQL 操作测试 ====================

// 正常情况: 保存新会话后通过会话 ID 查询, 各字段与保存的数据一致
TEST_F(ChatSessionDataTest, SaveNewSessionThenGetBySessionId)
{
    chat_session_data_->SaveOrUpdateChatSession(test_session_);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId(test_session_.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    ExpectChatSessionInfoEqual(*chat_session_info, test_session_);
}

// 正常情况: 可空字段为空串的会话保存后查询, 空串语义保持不变(数据库 NULL 转换为空串)
TEST_F(ChatSessionDataTest, SaveSessionWithEmptyNullableFields)
{
    ChatSessionInfo session = test_session_;
    session.title = "";
    session.file_id = "";
    session.type = "database";
    session.connection_info = "host=127.0.0.1;port=3306";
    chat_session_data_->SaveOrUpdateChatSession(session);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId(session.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    ExpectChatSessionInfoEqual(*chat_session_info, session);
}

// 正常情况: 更新已有会话后查询, 可变字段为更新后的新值
TEST_F(ChatSessionDataTest, UpdateExistingSessionThenGetBySessionId)
{
    chat_session_data_->SaveOrUpdateChatSession(test_session_);

    ChatSessionInfo updated_session = test_session_;
    updated_session.title = "更新后的标题";
    updated_session.update_time = 1725009999;
    updated_session.total_message_count = 10;
    updated_session.file_id = "fid_for_test_2";
    chat_session_data_->SaveOrUpdateChatSession(updated_session);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId(test_session_.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    ExpectChatSessionInfoEqual(*chat_session_info, updated_session);
}

// 正常情况: 更新会话时, 用户 ID, 创建时间, 模型名称, 会话类型等固有属性保持数据库中的值不变
TEST_F(ChatSessionDataTest, UpdateSessionKeepImmutableFields)
{
    chat_session_data_->SaveOrUpdateChatSession(test_session_);

    // 固有属性传入不同的新值, 更新后应保持数据库中的原值
    ChatSessionInfo updated_session = test_session_;
    updated_session.user_id = "uid_other_user";
    updated_session.create_time = 1700000000;
    updated_session.model_name = "gpt-4o-mini";
    updated_session.type = "database";
    updated_session.update_time = 1725009999;
    chat_session_data_->SaveOrUpdateChatSession(updated_session);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId(test_session_.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    EXPECT_EQ(chat_session_info->user_id, test_session_.user_id);
    EXPECT_EQ(chat_session_info->create_time, test_session_.create_time);
    EXPECT_EQ(chat_session_info->model_name, test_session_.model_name);
    EXPECT_EQ(chat_session_info->type, test_session_.type);
    EXPECT_EQ(chat_session_info->update_time, updated_session.update_time);
}

// 异常情况: 查询不存在的会话 ID 返回 std::nullopt
TEST_F(ChatSessionDataTest, GetChatSessionBySessionIdReturnNulloptWhenNotExists)
{
    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId("not_exists_session_id");
    EXPECT_FALSE(chat_session_info.has_value());
}

// 正常情况: 删除会话后通过会话 ID 查询返回 std::nullopt
TEST_F(ChatSessionDataTest, DeleteChatSessionThenGetReturnNullopt)
{
    chat_session_data_->SaveOrUpdateChatSession(test_session_);
    chat_session_data_->DeleteChatSessionBySessionId(test_session_.chat_session_id);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionId(test_session_.chat_session_id);
    EXPECT_FALSE(chat_session_info.has_value());
}

// 边界情况: 删除不存在的会话不抛出异常
TEST_F(ChatSessionDataTest, DeleteChatSessionWhenNotExists)
{
    EXPECT_NO_THROW(chat_session_data_->DeleteChatSessionBySessionId("not_exists_session_id"));
}

// 正常情况: 通过用户 ID 获取该用户的全部聊天会话列表
TEST_F(ChatSessionDataTest, GetChatSessionListByUserId)
{
    chat_session_data_->SaveOrUpdateChatSession(test_session_);

    ChatSessionInfo another_session = test_session_;
    another_session.chat_session_id = "csid_for_test_2";
    another_session.title = "测试会话 2";
    chat_session_data_->SaveOrUpdateChatSession(another_session);

    // 其他用户的会话不应出现在列表中
    ChatSessionInfo other_user_session = test_session_;
    other_user_session.chat_session_id = "csid_other_user";
    other_user_session.user_id = "uid_other_user";
    chat_session_data_->SaveOrUpdateChatSession(other_user_session);

    std::vector<ChatSessionInfo> chat_session_list =
        chat_session_data_->GetChatSessionListByUserId(test_session_.user_id);
    ASSERT_EQ(chat_session_list.size(), 2);
    ExpectChatSessionInfoEqual(chat_session_list[0], test_session_);
    ExpectChatSessionInfoEqual(chat_session_list[1], another_session);
}

// 边界情况: 用户没有聊天会话时返回空列表
TEST_F(ChatSessionDataTest, GetChatSessionListByUserIdReturnEmptyWhenNoSession)
{
    std::vector<ChatSessionInfo> chat_session_list =
        chat_session_data_->GetChatSessionListByUserId("uid_no_session");
    EXPECT_TRUE(chat_session_list.empty());
}

// ==================== Redis 操作测试 ====================

// 正常情况: 保存缓存后通过会话 ID 查询, 各字段与保存的数据一致
TEST_F(ChatSessionDataTest, SaveChatSessionToCacheThenGetBySessionId)
{
    chat_session_data_->SaveChatSessionToCache(test_session_);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionIdFromCache(test_session_.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    ExpectChatSessionInfoEqual(*chat_session_info, test_session_);
}

// 异常情况: 缓存未命中时通过会话 ID 查询返回 std::nullopt
TEST_F(ChatSessionDataTest, GetChatSessionFromCacheReturnNulloptWhenNotExists)
{
    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionIdFromCache("not_exists_session_id");
    EXPECT_FALSE(chat_session_info.has_value());
}

// 正常情况: 重复保存缓存时后写入的数据覆盖先写入的数据
TEST_F(ChatSessionDataTest, SaveChatSessionToCacheTwiceOverwriteOldValue)
{
    chat_session_data_->SaveChatSessionToCache(test_session_);

    ChatSessionInfo updated_session = test_session_;
    updated_session.title = "覆盖后的标题";
    updated_session.total_message_count = 20;
    chat_session_data_->SaveChatSessionToCache(updated_session);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionIdFromCache(test_session_.chat_session_id);
    ASSERT_TRUE(chat_session_info.has_value());
    ExpectChatSessionInfoEqual(*chat_session_info, updated_session);
}

// 正常情况: 删除缓存会话后通过会话 ID 查询返回 std::nullopt
TEST_F(ChatSessionDataTest, DeleteChatSessionFromCacheThenGetReturnNullopt)
{
    chat_session_data_->SaveChatSessionToCache(test_session_);
    chat_session_data_->DeleteChatSessionBySessionIdFromCache(test_session_.chat_session_id);

    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionIdFromCache(test_session_.chat_session_id);
    EXPECT_FALSE(chat_session_info.has_value());
}

// 边界情况: 删除缓存中不存在的会话不抛出异常(hdel 本身幂等)
TEST_F(ChatSessionDataTest, DeleteChatSessionFromCacheWhenNotExists)
{
    EXPECT_NO_THROW(chat_session_data_->DeleteChatSessionBySessionIdFromCache("not_exists_session_id"));
}

// 边界情况: 保存缓存后缓存 key 的过期时间被设置为 3 天以内
TEST_F(ChatSessionDataTest, SaveChatSessionToCacheSetExpireTime)
{
    chat_session_data_->SaveChatSessionToCache(test_session_);

    long long expire_time = GetRedisHandle()->ttl(kChatSessionCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kChatSessionCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "chat_session_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
