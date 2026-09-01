#include "svc_ai_service/ai_message_handler.h"

#include <cstdio>
#include <memory>
#include <string>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <gtest/gtest.h>
#include "common/exception.h"

// 业务层类型
using chat_excel::ErrorCode;
using chat_excel::ai_service::AIMessageHandler;
using chat_excel::ai_service::SendMessageContext;
using chat_excel::ai_service::ChatSessionData;
using chat_excel::ai_service::ChatSessionInfo;
using chat_excel::ai_service::ChatSessionManager;

namespace
{

// 聊天会话缓存 hash 类型的 key(与 ChatSessionData 实现保持一致)
constexpr const char* kChatSessionCacheKey = "chat_session_data";

// ChatSDK 本地数据库文件路径, 每个用例执行前删除保证环境干净
constexpr const char* kChatSdkDbPath = "/tmp/ai_message_handler_test_chat_sdk.db";

// 测试注册的模型名称
constexpr const char* kTestModelName = "deepseek-chat";

// 模型回复中的标签常量
constexpr const char* kSqlStartTag = "<SQL_START>";
constexpr const char* kSqlEndTag = "<SQL_END>";
constexpr const char* kEmailStartTag = "<EMAIL_START>";
constexpr const char* kEmailEndTag = "<EMAIL_END>";

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

// AI 消息处理类测试夹具, 每个用例执行前清理数据库, 缓存与 ChatSDK 本地数据
class AIMessageHandlerTest : public ::testing::Test
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
        config.model_info.model_name = kTestModelName;
        config.model_info.model_decs = "测试模型";
        config.apikey = "test_apikey";
        ASSERT_TRUE(ai_chat_sdk_->RegisterModel(config));

        // 创建聊天会话管理对象与消息处理对象,
        // 消息处理对象使用空信道管理对象(测试用例不触发跨子服务 RPC 调用),
        // 提示词模板目录由测试构建配置拷贝到二进制目录, 按默认工作目录加载
        const auto chat_session_data =
            std::make_shared<ChatSessionData>(GetMysqlHandle(), GetRedisHandle());
        chat_session_manager_ = std::make_shared<ChatSessionManager>(chat_session_data);
        const auto channel_manager = std::make_shared<cpp_toolkit::ChannelManager>();
        message_handler_ = std::make_shared<AIMessageHandler>(
            chat_session_manager_, channel_manager, ai_chat_sdk_);
    }

    void TearDown() override
    {
        // 释放各层对象后删除 ChatSDK 本地数据库文件
        message_handler_.reset();
        chat_session_manager_.reset();
        ai_chat_sdk_.reset();
        std::remove(kChatSdkDbPath);
    }

    /**
     * @brief 创建归属指定用户的聊天会话元数据(ChatSDK 会话 + MySQL 元数据)
     * @param user_id 会话归属用户 ID
     * @return 新建的聊天会话 ID
     */
    std::string CreateChatSessionForUser(const std::string& user_id)
    {
        const std::string chat_session_id =
            ai_chat_sdk_->CreateSession(user_id, kTestModelName);

        ChatSessionInfo session_info;
        session_info.chat_session_id = chat_session_id;
        session_info.user_id = user_id;
        session_info.model_name = kTestModelName;
        session_info.type = "plain";
        session_info.create_time = 1;
        session_info.update_time = 1;
        session_info.total_message_count = 0;
        chat_session_manager_->SaveOrUpdateChatSession(session_info);
        return chat_session_id;
    }

    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk_;

    std::shared_ptr<ChatSessionManager> chat_session_manager_;

    std::shared_ptr<AIMessageHandler> message_handler_;
};

// 提取 SQL : 标签区间内容被完整提取并去除首尾空白
TEST_F(AIMessageHandlerTest, ExtractSql)
{
    const std::string response =
        std::string("分析内容\n") + kSqlStartTag + "\n  SELECT * FROM tbl_user  \n" + kSqlEndTag;

    EXPECT_EQ(message_handler_->ExtractSql(response), "SELECT * FROM tbl_user");
}

// 提取 SQL : 缺少开始标签或结束标签时返回空字符串
TEST_F(AIMessageHandlerTest, ExtractSqlMissingTag)
{
    EXPECT_EQ(message_handler_->ExtractSql(std::string("没有开始标签 ") + kSqlEndTag), "");
    EXPECT_EQ(message_handler_->ExtractSql(std::string(kSqlStartTag) + " SELECT 1 "), "");
    EXPECT_EQ(message_handler_->ExtractSql("普通回复内容"), "");
}

// 提取 SQL : 标签区间内容为空白时返回空字符串
TEST_F(AIMessageHandlerTest, ExtractSqlEmptyContent)
{
    const std::string response = std::string(kSqlStartTag) + "   \n  " + kSqlEndTag;

    EXPECT_EQ(message_handler_->ExtractSql(response), "");
}

// 检测邮件工具调用 : 包含完整邮件指令标记时返回 true
TEST_F(AIMessageHandlerTest, IsEmailToolCall)
{
    const std::string response =
        std::string("好的 , 已为您发送") + kEmailStartTag + "sendEmail" + kEmailEndTag;

    EXPECT_TRUE(message_handler_->IsEmailToolCall(response));
}

// 检测邮件工具调用 : 标记不完整或不含指令时返回 false
TEST_F(AIMessageHandlerTest, IsEmailToolCallInvalid)
{
    // 缺少结束标签
    EXPECT_FALSE(message_handler_->IsEmailToolCall(
        std::string(kEmailStartTag) + "sendEmail"));
    // 缺少 sendEmail 指令
    EXPECT_FALSE(message_handler_->IsEmailToolCall(
        std::string(kEmailStartTag) + kEmailEndTag));
    // 普通回复
    EXPECT_FALSE(message_handler_->IsEmailToolCall("邮箱发送成功"));
}

// 发送消息 : 会话不存在时抛出会话不存在错误
TEST_F(AIMessageHandlerTest, SendMessageSessionNotFound)
{
    SendMessageContext context;
    context.request_id = "rid_handler_not_found";
    context.session_id = "gateway_session";
    context.user_id = "uid_handler";
    context.chat_session_id = "not_exist_session_id";
    context.chat_type = "plain";
    context.message = "你好";

    bool callback_called = false;
    const auto stream_callback = [&callback_called](const std::string&, bool)
    {
        callback_called = true;
    };

    try
    {
        message_handler_->SendMessage(context, stream_callback);
        FAIL() << "期望抛出异常";
    }
    catch (const chat_excel::ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
        EXPECT_FALSE(callback_called);
    }
}

// 发送消息 : 会话不属于当前用户时抛出归属校验失败错误
TEST_F(AIMessageHandlerTest, SendMessageUserMismatch)
{
    const std::string chat_session_id = CreateChatSessionForUser("uid_handler_owner");

    SendMessageContext context;
    context.request_id = "rid_handler_mismatch";
    context.session_id = "gateway_session";
    context.user_id = "uid_handler_other";
    context.chat_session_id = chat_session_id;
    context.chat_type = "plain";
    context.message = "你好";

    try
    {
        message_handler_->SendMessage(
            context, [](const std::string&, bool) {});
        FAIL() << "期望抛出异常";
    }
    catch (const chat_excel::ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
}

// 发送消息 : 聊天类型无效时抛出参数错误
TEST_F(AIMessageHandlerTest, SendMessageChatTypeInvalid)
{
    const std::string chat_session_id = CreateChatSessionForUser("uid_handler_type");

    SendMessageContext context;
    context.request_id = "rid_handler_type";
    context.session_id = "gateway_session";
    context.user_id = "uid_handler_type";
    context.chat_session_id = chat_session_id;
    context.chat_type = "voice";
    context.message = "你好";

    try
    {
        message_handler_->SendMessage(
            context, [](const std::string&, bool) {});
        FAIL() << "期望抛出异常";
    }
    catch (const chat_excel::ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::AI_SERVICE_CHAT_TYPE_INVALID);
    }
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察消息处理流程日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "ai_message_handler_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
