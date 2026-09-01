#include "svc_ai_service/ai_business.h"

#include <cstdio>
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
using chat_excel::ai_service::AiBusiness;
using chat_excel::ai_service::ChatSessionData;
using chat_excel::ai_service::ChatSessionHistory;
using chat_excel::ai_service::ChatSessionInfo;
using chat_excel::ai_service::ChatSessionManager;

namespace
{

// 聊天会话缓存 hash 类型的 key(与 ChatSessionData 实现保持一致)
constexpr const char* kChatSessionCacheKey = "chat_session_data";

// ChatSDK 本地数据库文件路径, 每个用例执行前删除保证环境干净
constexpr const char* kChatSdkDbPath = "/tmp/ai_business_test_chat_sdk.db";

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

} // namespace

// AI 业务逻辑类测试夹具, 每个用例执行前清理数据库, 缓存与 ChatSDK 本地数据
class AiBusinessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_chat_session");
        transaction.commit();
        GetRedisHandle()->del(kChatSessionCacheKey);

        // 删除 ChatSDK 本地数据库文件, 保证测试环境干净
        std::remove(kChatSdkDbPath);

        // 创建 ChatSDK 实例并注册测试模型
        ai_chat_sdk_ = std::make_shared<aichat_sdk::AIChatSdk>(kChatSdkDbPath);
        aichat_sdk::Config config;
        config.model_type = aichat_sdk::ModelType::DEEPSEEK;
        config.model_info.model_name = "deepseek-chat";
        config.model_info.model_decs = "测试模型";
        config.apikey = "test_apikey";
        ASSERT_TRUE(ai_chat_sdk_->RegisterModel(config));

        // 创建聊天会话管理对象与业务层对象
        const auto chat_session_data =
            std::make_shared<ChatSessionData>(GetMysqlHandle(), GetRedisHandle());
        chat_session_manager_ = std::make_shared<ChatSessionManager>(chat_session_data);
        ai_business_ = std::make_unique<AiBusiness>(chat_session_manager_, ai_chat_sdk_);
    }

    void TearDown() override
    {
        // 释放业务层与 ChatSDK 对象后删除 ChatSDK 本地数据库文件
        ai_business_.reset();
        chat_session_manager_.reset();
        ai_chat_sdk_.reset();
        std::remove(kChatSdkDbPath);
    }

    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk_;

    std::shared_ptr<ChatSessionManager> chat_session_manager_;

    std::unique_ptr<AiBusiness> ai_business_;
};

// 获取可用的模型列表
TEST_F(AiBusinessTest, GetModels)
{
    const std::vector<aichat_sdk::ModelInfo> models = ai_business_->GetModels();
    ASSERT_EQ(models.size(), 1);
    EXPECT_EQ(models[0].model_name, "deepseek-chat");
    EXPECT_EQ(models[0].model_decs, "测试模型");
}

// 新建聊天会话
TEST_F(AiBusinessTest, CreateChatSession)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_create", "uid_create", "deepseek-chat", "excel", "");
    EXPECT_FALSE(chat_session_id.empty());

    // 查询会话元数据, 校验新建会话的初始字段
    const ChatSessionInfo info =
        ai_business_->GetChatSession("rid_create", "uid_create", chat_session_id);
    EXPECT_EQ(info.chat_session_id, chat_session_id);
    EXPECT_EQ(info.user_id, "uid_create");
    EXPECT_EQ(info.title, "");
    EXPECT_EQ(info.total_message_count, 0);
    EXPECT_EQ(info.model_name, "deepseek-chat");
    EXPECT_EQ(info.file_id, "");
    EXPECT_EQ(info.type, "excel");
    EXPECT_EQ(info.connection_info, "");
    EXPECT_GT(info.create_time, 0);
    EXPECT_EQ(info.create_time, info.update_time);

    // ChatSDK 中会话存在
    EXPECT_NE(ai_chat_sdk_->GetSession(chat_session_id), nullptr);
}

// 新建 database 类型聊天会话
TEST_F(AiBusinessTest, CreateDatabaseChatSession)
{
    const std::string chat_session_id = ai_business_->CreateChatSession(
        "rid_create_db", "uid_create", "deepseek-chat", "database", "{\"host\":\"127.0.0.1\"}");
    const ChatSessionInfo info =
        ai_business_->GetChatSession("rid_create_db", "uid_create", chat_session_id);
    EXPECT_EQ(info.type, "database");
    EXPECT_EQ(info.connection_info, "{\"host\":\"127.0.0.1\"}");
}

// 获取指定用户的聊天会话列表
TEST_F(AiBusinessTest, GetChatSessionList)
{
    ai_business_->CreateChatSession("rid_list_1", "uid_list", "deepseek-chat", "excel", "");
    ai_business_->CreateChatSession("rid_list_2", "uid_list", "deepseek-chat", "database", "");
    ai_business_->CreateChatSession("rid_list_3", "uid_other", "deepseek-chat", "excel", "");

    EXPECT_EQ(ai_business_->GetChatSessionList("rid_list", "uid_list").size(), 2);
    EXPECT_EQ(ai_business_->GetChatSessionList("rid_list", "uid_other").size(), 1);
    EXPECT_TRUE(ai_business_->GetChatSessionList("rid_list", "uid_none").empty());
}

// 通过会话 ID 获取会话元数据: 会话不属于当前用户时抛出异常
TEST_F(AiBusinessTest, GetChatSessionUserMismatch)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_mismatch", "uid_1", "deepseek-chat", "excel", "");
    try
    {
        ai_business_->GetChatSession("rid_mismatch", "uid_2", chat_session_id);
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
}

// 通过会话 ID 获取会话元数据: 会话不存在时抛出异常
TEST_F(AiBusinessTest, GetChatSessionNotFound)
{
    try
    {
        ai_business_->GetChatSession("rid_not_found", "uid_1", "csid_not_exist");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

// 获取指定会话的历史消息: 新建会话没有历史消息
TEST_F(AiBusinessTest, GetSessionHistoryEmpty)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_history", "uid_history", "deepseek-chat", "excel", "");

    const ChatSessionHistory history =
        ai_business_->GetSessionHistory("rid_history", "uid_history", chat_session_id);
    EXPECT_EQ(history.chat_session_info.chat_session_id, chat_session_id);
    EXPECT_EQ(history.chat_session_info.user_id, "uid_history");
    EXPECT_TRUE(history.messages.empty());
}

// 获取指定会话的历史消息: ChatSDK 中会话不存在时抛出异常
TEST_F(AiBusinessTest, GetSessionHistorySdkSessionNotFound)
{
    // 通过会话管理对象直接构造元数据, 不经过 ChatSDK 创建会话,
    // 模拟 MySQL 元数据存在但 ChatSDK 中会话丢失的场景
    ChatSessionInfo info;
    info.chat_session_id = "csid_sdk_missing";
    info.user_id = "uid_history";
    info.create_time = 1756000000;
    info.update_time = 1756000000;
    info.model_name = "deepseek-chat";
    info.type = "excel";
    chat_session_manager_->SaveOrUpdateChatSession(info);

    try
    {
        ai_business_->GetSessionHistory("rid_history_missing", "uid_history", "csid_sdk_missing");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::AI_CHAT_SDK_SESSION_NOT_FOUND);
    }
}

// 删除指定用户的指定聊天会话
TEST_F(AiBusinessTest, DeleteChatSession)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_delete", "uid_delete", "deepseek-chat", "excel", "");

    ai_business_->DeleteChatSession("rid_delete", "uid_delete", chat_session_id);

    // 会话元数据已删除
    try
    {
        ai_business_->GetChatSession("rid_delete", "uid_delete", chat_session_id);
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
    // ChatSDK 中的会话与消息已删除
    EXPECT_EQ(ai_chat_sdk_->GetSession(chat_session_id), nullptr);
}

// 删除聊天会话: 会话不属于当前用户时抛出异常
TEST_F(AiBusinessTest, DeleteChatSessionUserMismatch)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_delete_2", "uid_1", "deepseek-chat", "excel", "");
    try
    {
        ai_business_->DeleteChatSession("rid_delete_2", "uid_2", chat_session_id);
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
}

// 删除聊天会话: 会话不存在时抛出异常
TEST_F(AiBusinessTest, DeleteChatSessionNotFound)
{
    try
    {
        ai_business_->DeleteChatSession("rid_delete_3", "uid_1", "csid_not_exist");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }
}

// 更新聊天会话关联的文件 ID
TEST_F(AiBusinessTest, UpdateChatSessionFileId)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_update_file", "uid_file", "deepseek-chat", "excel", "");

    ai_business_->UpdateChatSessionFileId("rid_update_file", "uid_file", chat_session_id, "fid_excel");

    const ChatSessionInfo info =
        ai_business_->GetChatSession("rid_update_file", "uid_file", chat_session_id);
    EXPECT_EQ(info.file_id, "fid_excel");
}

// 更新聊天会话关联的文件 ID: 会话不属于当前用户时抛出异常
TEST_F(AiBusinessTest, UpdateChatSessionFileIdUserMismatch)
{
    const std::string chat_session_id =
        ai_business_->CreateChatSession("rid_update_file_2", "uid_1", "deepseek-chat", "excel", "");
    try
    {
        ai_business_->UpdateChatSessionFileId("rid_update_file_2", "uid_2", chat_session_id, "fid_excel");
        FAIL() << "期望抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
}

// 更新聊天会话关联的文件 ID: 会话不存在时抛出异常
TEST_F(AiBusinessTest, UpdateChatSessionFileIdNotFound)
{
    try
    {
        ai_business_->UpdateChatSessionFileId("rid_update_file_3", "uid_1", "csid_not_exist", "fid_excel");
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
    settings.loggerName = "ai_business_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
