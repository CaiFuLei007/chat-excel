#include "svc_ai_service/chat_session_manager.h"

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
using chat_excel::ErrorCode;
using chat_excel::ai_service::ChatSessionData;
using chat_excel::ai_service::ChatSessionInfo;
using chat_excel::ai_service::ChatSessionManager;

namespace
{

// 聊天会话缓存 hash 类型的 key(与 ChatSessionData 实现保持一致)
constexpr const char* kChatSessionCacheKey = "chat_session_data";

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
 * @brief 构造测试聊天会话元数据
 * @param chat_session_id 聊天会话 ID
 * @param user_id 用户 ID
 * @return 聊天会话元数据
 */
ChatSessionInfo MakeChatSessionInfo(const std::string& chat_session_id, const std::string& user_id)
{
    ChatSessionInfo info;
    info.chat_session_id = chat_session_id;
    info.user_id = user_id;
    info.create_time = 1756000000;
    info.update_time = 1756000000;
    info.total_message_count = 0;
    info.model_name = "deepseek-chat";
    info.type = "excel";
    return info;
}

} // namespace

// 聊天会话管理类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class ChatSessionManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_chat_session");
        transaction.commit();
        GetRedisHandle()->del(kChatSessionCacheKey);

        chat_session_data_ = std::make_shared<ChatSessionData>(GetMysqlHandle(), GetRedisHandle());
        chat_session_manager_ = std::make_shared<ChatSessionManager>(chat_session_data_);
    }

    std::shared_ptr<ChatSessionData> chat_session_data_;

    std::shared_ptr<ChatSessionManager> chat_session_manager_;
};

// 保存或更新聊天会话元数据: 新增场景
TEST_F(ChatSessionManagerTest, SaveOrUpdateChatSessionInsert)
{
    ChatSessionInfo info = MakeChatSessionInfo("csid_insert", "uid_1");
    info.file_id = "fid_1";
    chat_session_manager_->SaveOrUpdateChatSession(info);

    // 缓存已删除, 查询走数据库, 校验保存的字段
    const ChatSessionInfo query_info = chat_session_manager_->GetChatSessionBySessionId("csid_insert");
    EXPECT_EQ(query_info.user_id, "uid_1");
    EXPECT_EQ(query_info.model_name, "deepseek-chat");
    EXPECT_EQ(query_info.type, "excel");
    EXPECT_EQ(query_info.file_id, "fid_1");
}

// 保存或更新聊天会话元数据: 更新场景(可变字段更新, 固有属性保持不变)
TEST_F(ChatSessionManagerTest, SaveOrUpdateChatSessionUpdate)
{
    ChatSessionInfo info = MakeChatSessionInfo("csid_update", "uid_1");
    chat_session_manager_->SaveOrUpdateChatSession(info);

    // 修改可变字段后再次保存, 固有属性 user_id/model_name/create_time 保持不变
    info.title = "新会话";
    info.update_time = 1756000100;
    info.total_message_count = 2;
    info.file_id = "fid_2";
    chat_session_manager_->SaveOrUpdateChatSession(info);

    const ChatSessionInfo query_info = chat_session_manager_->GetChatSessionBySessionId("csid_update");
    EXPECT_EQ(query_info.title, "新会话");
    EXPECT_EQ(query_info.update_time, 1756000100);
    EXPECT_EQ(query_info.total_message_count, 2);
    EXPECT_EQ(query_info.file_id, "fid_2");
    EXPECT_EQ(query_info.user_id, "uid_1");
    EXPECT_EQ(query_info.model_name, "deepseek-chat");
    EXPECT_EQ(query_info.create_time, 1756000000);
}

// 保存或更新聊天会话元数据后删除缓存(写策略 Cache-Aside)
TEST_F(ChatSessionManagerTest, SaveOrUpdateChatSessionDeleteCache)
{
    ChatSessionInfo info = MakeChatSessionInfo("csid_cache", "uid_1");
    chat_session_manager_->SaveOrUpdateChatSession(info);

    // 查询会话将元数据回填到缓存中
    chat_session_manager_->GetChatSessionBySessionId("csid_cache");
    ASSERT_TRUE(GetRedisHandle()->hexists(kChatSessionCacheKey, "chat_session:csid_cache"));

    // 再次保存或更新, 缓存中的会话元数据被删除
    info.total_message_count = 1;
    chat_session_manager_->SaveOrUpdateChatSession(info);
    EXPECT_FALSE(GetRedisHandle()->hexists(kChatSessionCacheKey, "chat_session:csid_cache"));
}

// 通过会话 ID 获取会话元数据: 缓存未命中时查数据库并回填缓存
TEST_F(ChatSessionManagerTest, GetChatSessionBySessionIdFillCache)
{
    // 通过数据层直接插入, 不经过缓存删除逻辑
    chat_session_data_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_get", "uid_1"));

    // 首次查询: 缓存未命中, 从数据库读取并回填缓存
    const ChatSessionInfo query_info = chat_session_manager_->GetChatSessionBySessionId("csid_get");
    EXPECT_EQ(query_info.chat_session_id, "csid_get");
    EXPECT_TRUE(GetRedisHandle()->hexists(kChatSessionCacheKey, "chat_session:csid_get"));

    // 第二次查询: 缓存命中
    const ChatSessionInfo cache_info = chat_session_manager_->GetChatSessionBySessionId("csid_get");
    EXPECT_EQ(cache_info.user_id, "uid_1");
}

// 通过会话 ID 获取会话元数据: 会话不存在时抛出异常
TEST_F(ChatSessionManagerTest, GetChatSessionBySessionIdNotFound)
{
    try
    {
        chat_session_manager_->GetChatSessionBySessionId("csid_not_exist");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

// 通过用户 ID 获取会话列表
TEST_F(ChatSessionManagerTest, GetChatSessionListByUserId)
{
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_list_1", "uid_1"));
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_list_2", "uid_1"));
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_list_3", "uid_2"));

    EXPECT_EQ(chat_session_manager_->GetChatSessionListByUserId("uid_1").size(), 2);
    EXPECT_EQ(chat_session_manager_->GetChatSessionListByUserId("uid_2").size(), 1);
    EXPECT_TRUE(chat_session_manager_->GetChatSessionListByUserId("uid_3").empty());
}

// 删除会话元数据: MySQL 与缓存同时删除
TEST_F(ChatSessionManagerTest, DeleteChatSessionBySessionId)
{
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_del", "uid_1"));
    chat_session_manager_->GetChatSessionBySessionId("csid_del");

    chat_session_manager_->DeleteChatSessionBySessionId("csid_del");

    EXPECT_FALSE(GetRedisHandle()->hexists(kChatSessionCacheKey, "chat_session:csid_del"));
    try
    {
        chat_session_manager_->GetChatSessionBySessionId("csid_del");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

// 删除不存在的会话元数据不抛出异常
TEST_F(ChatSessionManagerTest, DeleteChatSessionBySessionIdNotExist)
{
    chat_session_manager_->DeleteChatSessionBySessionId("csid_not_exist");
}

// 检测指定聊天会话是否属于指定用户
TEST_F(ChatSessionManagerTest, CheckChatSessionOwner)
{
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_owner", "uid_1"));
    EXPECT_TRUE(chat_session_manager_->CheckChatSessionOwner("uid_1", "csid_owner"));
    EXPECT_FALSE(chat_session_manager_->CheckChatSessionOwner("uid_2", "csid_owner"));

    // 会话不存在时抛出异常
    try
    {
        chat_session_manager_->CheckChatSessionOwner("uid_1", "csid_not_exist");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

// 更新会话关联的文件 ID
TEST_F(ChatSessionManagerTest, UpdateChatSessionFileId)
{
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_file", "uid_1"));

    // 更新文件 ID 后查询验证(内部会删除缓存, 查询走数据库)
    chat_session_manager_->UpdateChatSessionFileId("uid_1", "csid_file", "fid_new");
    EXPECT_EQ(chat_session_manager_->GetChatSessionBySessionId("csid_file").file_id, "fid_new");
}

// 更新会话关联的文件 ID: 会话不属于当前用户时抛出异常
TEST_F(ChatSessionManagerTest, UpdateChatSessionFileIdUserMismatch)
{
    chat_session_manager_->SaveOrUpdateChatSession(MakeChatSessionInfo("csid_file_2", "uid_1"));
    try
    {
        chat_session_manager_->UpdateChatSessionFileId("uid_2", "csid_file_2", "fid_new");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
}

// 更新会话关联的文件 ID: 会话不存在时抛出异常
TEST_F(ChatSessionManagerTest, UpdateChatSessionFileIdNotFound)
{
    try
    {
        chat_session_manager_->UpdateChatSessionFileId("uid_1", "csid_not_exist", "fid_new");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "chat_session_manager_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
