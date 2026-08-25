#include "data/session_data.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <gtest/gtest.h>
#include "common/exception.h"

using chat_excel::ChatExcelException;
using chat_excel::user_service::SessionData;
using chat_excel::user_service::SessionInfo;

namespace
{

// 会话缓存 hash 类型的 key(与 SessionData 实现保持一致)
constexpr const char* kSessionCacheKey = "session_data";

// 会话缓存过期时间(秒), 3 天
constexpr int kSessionCacheExpireTime = 3 * 24 * 60 * 60;

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
 * @brief 校验实际查询到的会话信息与期望的会话信息一致
 * @param actual 实际查询到的会话信息
 * @param expected 期望的会话信息
 */
void ExpectSessionInfoEqual(const SessionInfo& actual, const SessionInfo& expected)
{
    EXPECT_EQ(actual.session_id, expected.session_id);
    EXPECT_EQ(actual.user_id, expected.user_id);
}

} // namespace

// 会话数据访问类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class SessionDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_session");
        transaction.commit();
        GetRedisHandle()->del(kSessionCacheKey);

        session_data_ = std::make_unique<SessionData>(GetMysqlHandle(), GetRedisHandle());

        // 构造测试会话信息, 字段长度受表结构 VARCHAR(32) 限制
        test_session_.session_id = "sid_for_test";
        test_session_.user_id = "uid_for_test";
    }

    std::unique_ptr<SessionData> session_data_;
    SessionInfo test_session_;
};

// ==================== MySQL 操作测试 ====================

// 正常情况: 保存会话后通过会话 ID 查询, 各字段与保存的数据一致
TEST_F(SessionDataTest, SaveSessionThenGetBySessionId)
{
    session_data_->SaveSession(test_session_);

    std::optional<SessionInfo> session_info = session_data_->GetSessionBySessionId(test_session_.session_id);
    ASSERT_TRUE(session_info.has_value());
    ExpectSessionInfoEqual(*session_info, test_session_);
}

// 异常情况: 查询不存在的会话 ID 返回 std::nullopt
TEST_F(SessionDataTest, GetSessionBySessionIdReturnNulloptWhenNotExists)
{
    std::optional<SessionInfo> session_info = session_data_->GetSessionBySessionId("not_exists_session_id");
    EXPECT_FALSE(session_info.has_value());
}

// 正常情况: 删除会话后通过会话 ID 查询返回 std::nullopt
TEST_F(SessionDataTest, DeleteSessionThenGetReturnNullopt)
{
    session_data_->SaveSession(test_session_);
    session_data_->DeleteSessionBySessionId(test_session_.session_id);

    std::optional<SessionInfo> session_info = session_data_->GetSessionBySessionId(test_session_.session_id);
    EXPECT_FALSE(session_info.has_value());
}

// 边界情况: 删除不存在的会话不抛出异常
TEST_F(SessionDataTest, DeleteSessionWhenNotExists)
{
    EXPECT_NO_THROW(session_data_->DeleteSessionBySessionId("not_exists_session_id"));
}

// 异常情况: 保存重复唯一键的会话抛出 ChatExcelException
TEST_F(SessionDataTest, SaveDuplicateSessionThrowChatExcelException)
{
    session_data_->SaveSession(test_session_);
    EXPECT_THROW(session_data_->SaveSession(test_session_), ChatExcelException);
}

// ==================== Redis 操作测试 ====================

// 正常情况: 保存缓存后通过会话 ID 查询, 各字段与保存的数据一致
TEST_F(SessionDataTest, SaveSessionToCacheThenGetBySessionId)
{
    session_data_->SaveSessionToCache(test_session_);

    std::optional<SessionInfo> session_info =
        session_data_->GetSessionBySessionIdFromCache(test_session_.session_id);
    ASSERT_TRUE(session_info.has_value());
    ExpectSessionInfoEqual(*session_info, test_session_);
}

// 异常情况: 缓存未命中时通过会话 ID 查询返回 std::nullopt
TEST_F(SessionDataTest, GetSessionFromCacheReturnNulloptWhenNotExists)
{
    std::optional<SessionInfo> session_info =
        session_data_->GetSessionBySessionIdFromCache("not_exists_session_id");
    EXPECT_FALSE(session_info.has_value());
}

// 正常情况: 删除缓存会话后通过会话 ID 查询返回 std::nullopt
TEST_F(SessionDataTest, DeleteSessionFromCacheThenGetReturnNullopt)
{
    session_data_->SaveSessionToCache(test_session_);
    session_data_->DeleteSessionBySessionIdFromCache(test_session_.session_id);

    std::optional<SessionInfo> session_info =
        session_data_->GetSessionBySessionIdFromCache(test_session_.session_id);
    EXPECT_FALSE(session_info.has_value());
}

// 边界情况: 保存缓存后缓存 key 的过期时间被设置为 3 天以内
TEST_F(SessionDataTest, SaveSessionToCacheSetExpireTime)
{
    session_data_->SaveSessionToCache(test_session_);

    long long expire_time = GetRedisHandle()->ttl(kSessionCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kSessionCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "session_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
