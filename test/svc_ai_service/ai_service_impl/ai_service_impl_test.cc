#include "svc_ai_service/ai_service_impl.h"

#include <cstdio>
#include <cstdlib>
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

// proto 生成代码中的消息类型
using chat_excel_proto::ai_service::CreateChatSessionRequest;
using chat_excel_proto::ai_service::CreateChatSessionResponse;
using chat_excel_proto::ai_service::DeleteSessionRequest;
using chat_excel_proto::ai_service::DeleteSessionResponse;
using chat_excel_proto::ai_service::GetModelsRequest;
using chat_excel_proto::ai_service::GetModelsResponse;
using chat_excel_proto::ai_service::GetSessionHistoryRequest;
using chat_excel_proto::ai_service::GetSessionHistoryResponse;
using chat_excel_proto::ai_service::GetSessionsRequest;
using chat_excel_proto::ai_service::GetSessionsResponse;
using chat_excel_proto::ai_service::UpdateSessionFileRequest;
using chat_excel_proto::ai_service::UpdateSessionFileResponse;
// 业务层类型
using chat_excel::ErrorCode;
using chat_excel::ai_service::AIMessageHandler;
using chat_excel::ai_service::AiBusiness;
using chat_excel::ai_service::AiServiceImpl;
using chat_excel::ai_service::ChatSessionData;
using chat_excel::ai_service::ChatSessionManager;

namespace
{

// 聊天会话缓存 hash 类型的 key(与 ChatSessionData 实现保持一致)
constexpr const char* kChatSessionCacheKey = "chat_session_data";

// ChatSDK 本地数据库文件路径, 每个用例执行前删除保证环境干净
constexpr const char* kChatSdkDbPath = "/tmp/ai_service_impl_test_chat_sdk.db";

// 会话类型常量
constexpr const char* kSessionTypeExcel = "excel";
constexpr const char* kSessionTypeDatabase = "database";

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

// AI 子服务 RPC 接口实现层测试夹具, 每个用例执行前清理数据库, 缓存与 ChatSDK 本地数据
class AiServiceImplTest : public ::testing::Test
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

        // 创建聊天会话管理对象, 消息处理对象, 业务层对象与 RPC 接口实现层对象,
        // 消息处理对象使用空信道管理对象(测试用例不触发跨子服务 RPC 调用),
        // 提示词模板目录由测试构建配置拷贝到二进制目录, 按默认工作目录加载
        const auto chat_session_data =
            std::make_shared<ChatSessionData>(GetMysqlHandle(), GetRedisHandle());
        chat_session_manager_ = std::make_shared<ChatSessionManager>(chat_session_data);
        const auto channel_manager = std::make_shared<cpp_toolkit::ChannelManager>();
        ai_message_handler_ = std::make_shared<AIMessageHandler>(
            chat_session_manager_, channel_manager, ai_chat_sdk_);
        ai_business_ = std::make_shared<AiBusiness>(chat_session_manager_, ai_chat_sdk_);
        ai_service_impl_ = std::make_unique<AiServiceImpl>(ai_business_, ai_message_handler_);
    }

    void TearDown() override
    {
        // 释放各层对象后删除 ChatSDK 本地数据库文件
        ai_service_impl_.reset();
        ai_business_.reset();
        ai_message_handler_.reset();
        chat_session_manager_.reset();
        ai_chat_sdk_.reset();
        std::remove(kChatSdkDbPath);
    }

    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk_;

    std::shared_ptr<ChatSessionManager> chat_session_manager_;

    std::shared_ptr<AIMessageHandler> ai_message_handler_;

    std::shared_ptr<AiBusiness> ai_business_;

    std::unique_ptr<AiServiceImpl> ai_service_impl_;
};

// 获取可用的模型列表 : 返回 ChatSDK 中注册的模型
TEST_F(AiServiceImplTest, GetModels)
{
    GetModelsRequest request;
    request.set_request_id("rid_get_models");
    GetModelsResponse response;

    ai_service_impl_->GetModels(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_models");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.result().models_size(), 1);
    EXPECT_EQ(response.result().models(0).name(), "deepseek-chat");
    EXPECT_EQ(response.result().models(0).desc(), "测试模型");
}

// 新建聊天会话 : excel 类型, 返回会话 ID 与模型名称
TEST_F(AiServiceImplTest, CreateSession)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session");
    request.set_user_id("uid_impl_create");
    request.set_model("deepseek-chat");
    request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_create_session");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_FALSE(response.result().session().chat_session_id().empty());
    EXPECT_EQ(response.result().session().model(), "deepseek-chat");
}

// 新建聊天会话 : database 类型且携带数据库连接信息
TEST_F(AiServiceImplTest, CreateSessionDatabaseWithConnectionInfo)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_db");
    request.set_user_id("uid_impl_create");
    request.set_model("deepseek-chat");
    request.set_session_type(kSessionTypeDatabase);
    request.set_db_connection_info("{\"host\":\"127.0.0.1\"}");
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_FALSE(response.result().session().chat_session_id().empty());
}

// 新建聊天会话 : 用户 ID 为空时返回用户 ID 为空错误
TEST_F(AiServiceImplTest, CreateSessionUserIdEmpty)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_empty_uid");
    request.set_model("deepseek-chat");
    request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_USER_ID_EMPTY));
}

// 新建聊天会话 : 模型名称为空时返回模型名称为空错误
TEST_F(AiServiceImplTest, CreateSessionModelEmpty)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_empty_model");
    request.set_user_id("uid_impl_create");
    request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_MODEL_NAME_EMPTY));
}

// 新建聊天会话 : 会话类型为空时返回会话类型为空错误
TEST_F(AiServiceImplTest, CreateSessionTypeEmpty)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_empty_type");
    request.set_user_id("uid_impl_create");
    request.set_model("deepseek-chat");
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_SESSION_TYPE_EMPTY));
}

// 新建聊天会话 : 会话类型无效时返回会话类型无效错误
TEST_F(AiServiceImplTest, CreateSessionTypeInvalid)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_invalid_type");
    request.set_user_id("uid_impl_create");
    request.set_model("deepseek-chat");
    request.set_session_type("video");
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_SESSION_TYPE_INVALID));
}

// 新建聊天会话 : database 类型缺少数据库连接信息时返回数据库连接信息为空错误
TEST_F(AiServiceImplTest, CreateSessionDatabaseWithoutConnectionInfo)
{
    CreateChatSessionRequest request;
    request.set_request_id("rid_create_session_db_no_info");
    request.set_user_id("uid_impl_create");
    request.set_model("deepseek-chat");
    request.set_session_type(kSessionTypeDatabase);
    CreateChatSessionResponse response;

    ai_service_impl_->CreateSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(),
              static_cast<int>(ErrorCode::AI_SERVICE_DB_CONNECTION_INFO_EMPTY));
}

// 获取指定用户的聊天会话列表 : 返回该用户的所有会话
TEST_F(AiServiceImplTest, GetSessions)
{
    // 通过接口新建两个属于 uid_impl_list 的会话与一个属于其他用户的会话
    CreateChatSessionRequest create_request;
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse create_response;
    create_request.set_request_id("rid_get_sessions_1");
    create_request.set_user_id("uid_impl_list");
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    create_request.set_request_id("rid_get_sessions_2");
    create_request.set_session_type(kSessionTypeDatabase);
    create_request.set_db_connection_info("{\"host\":\"127.0.0.1\"}");
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    create_request.set_request_id("rid_get_sessions_3");
    create_request.set_user_id("uid_impl_other");
    create_request.set_session_type(kSessionTypeExcel);
    create_request.set_db_connection_info("");
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);

    GetSessionsRequest request;
    request.set_request_id("rid_get_sessions");
    request.set_user_id("uid_impl_list");
    GetSessionsResponse response;

    ai_service_impl_->GetSessions(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_sessions");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.result().sessioninfo_size(), 2);
    // 会话列表字段填充正确, 首条消息内容暂为"新会话"
    for (const chat_excel_proto::ai_service::SessionInfo& session_info : response.result().sessioninfo())
    {
        EXPECT_EQ(session_info.model(), "deepseek-chat");
        EXPECT_EQ(session_info.message_count(), 0);
        EXPECT_EQ(session_info.first_user_message_content(), "新会话");
        EXPECT_GT(session_info.created_at(), 0);
        EXPECT_EQ(session_info.created_at(), session_info.updated_at());
    }
}

// 获取指定用户的聊天会话列表 : 用户 ID 为空时返回用户 ID 为空错误
TEST_F(AiServiceImplTest, GetSessionsUserIdEmpty)
{
    GetSessionsRequest request;
    request.set_request_id("rid_get_sessions_empty_uid");
    GetSessionsResponse response;

    ai_service_impl_->GetSessions(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_USER_ID_EMPTY));
}

// 获取指定会话的历史消息 : 新建会话没有历史消息, 返回会话元数据
TEST_F(AiServiceImplTest, GetSessionHistory)
{
    // 新建 database 类型会话并关联文件, 校验元数据回填
    CreateChatSessionRequest create_request;
    create_request.set_request_id("rid_get_history_create");
    create_request.set_user_id("uid_impl_history");
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeDatabase);
    create_request.set_db_connection_info("{\"host\":\"127.0.0.1\"}");
    CreateChatSessionResponse create_response;
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    const std::string chat_session_id = create_response.result().session().chat_session_id();

    UpdateSessionFileRequest update_request;
    update_request.set_request_id("rid_get_history_update");
    update_request.set_user_id("uid_impl_history");
    update_request.set_chat_session_id(chat_session_id);
    update_request.set_file_id("fid_impl_history");
    UpdateSessionFileResponse update_response;
    ai_service_impl_->UpdateSessionFile(nullptr, &update_request, &update_response, nullptr);

    GetSessionHistoryRequest request;
    request.set_request_id("rid_get_history");
    request.set_user_id("uid_impl_history");
    request.set_chat_session_id(chat_session_id);
    GetSessionHistoryResponse response;

    ai_service_impl_->GetSessionHistory(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_history");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().file_id(), "fid_impl_history");
    EXPECT_EQ(response.result().session_type(), kSessionTypeDatabase);
    EXPECT_EQ(response.result().db_connection_info(), "{\"host\":\"127.0.0.1\"}");
    EXPECT_EQ(response.result().messages_size(), 0);
}

// 获取指定会话的历史消息 : 聊天会话 ID 为空时返回聊天会话 ID 为空错误
TEST_F(AiServiceImplTest, GetSessionHistoryChatSessionIdEmpty)
{
    GetSessionHistoryRequest request;
    request.set_request_id("rid_get_history_empty_ssid");
    request.set_user_id("uid_impl_history");
    GetSessionHistoryResponse response;

    ai_service_impl_->GetSessionHistory(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_CHAT_SESSION_ID_EMPTY));
}

// 获取指定会话的历史消息 : 会话不存在时返回会话数据不存在错误
TEST_F(AiServiceImplTest, GetSessionHistoryNotFound)
{
    GetSessionHistoryRequest request;
    request.set_request_id("rid_get_history_not_found");
    request.set_user_id("uid_impl_history");
    request.set_chat_session_id("csid_not_exist");
    GetSessionHistoryResponse response;

    ai_service_impl_->GetSessionHistory(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_DATA_NOT_FOUND));
}

// 获取指定会话的历史消息 : 会话不属于当前用户时返回归属校验错误
TEST_F(AiServiceImplTest, GetSessionHistoryUserMismatch)
{
    CreateChatSessionRequest create_request;
    create_request.set_request_id("rid_get_history_mismatch_create");
    create_request.set_user_id("uid_impl_1");
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse create_response;
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    const std::string chat_session_id = create_response.result().session().chat_session_id();

    GetSessionHistoryRequest request;
    request.set_request_id("rid_get_history_mismatch");
    request.set_user_id("uid_impl_2");
    request.set_chat_session_id(chat_session_id);
    GetSessionHistoryResponse response;

    ai_service_impl_->GetSessionHistory(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_USER_MISMATCH));
}

// 删除指定会话 : 删除后再次获取历史消息时返回会话数据不存在
TEST_F(AiServiceImplTest, DeleteSession)
{
    CreateChatSessionRequest create_request;
    create_request.set_request_id("rid_delete_create");
    create_request.set_user_id("uid_impl_delete");
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse create_response;
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    const std::string chat_session_id = create_response.result().session().chat_session_id();

    DeleteSessionRequest request;
    request.set_request_id("rid_delete");
    request.set_user_id("uid_impl_delete");
    request.set_chat_session_id(chat_session_id);
    DeleteSessionResponse response;

    ai_service_impl_->DeleteSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_delete");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 会话已删除, 再次获取历史消息时返回会话数据不存在
    GetSessionHistoryRequest history_request;
    history_request.set_request_id("rid_delete_check");
    history_request.set_user_id("uid_impl_delete");
    history_request.set_chat_session_id(chat_session_id);
    GetSessionHistoryResponse history_response;
    ai_service_impl_->GetSessionHistory(nullptr, &history_request, &history_response, nullptr);
    EXPECT_EQ(history_response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_DATA_NOT_FOUND));
}

// 删除指定会话 : 用户 ID 为空时返回用户 ID 为空错误
TEST_F(AiServiceImplTest, DeleteSessionUserIdEmpty)
{
    DeleteSessionRequest request;
    request.set_request_id("rid_delete_empty_uid");
    request.set_chat_session_id("csid_not_exist");
    DeleteSessionResponse response;

    ai_service_impl_->DeleteSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_USER_ID_EMPTY));
}

// 删除指定会话 : 聊天会话 ID 为空时返回聊天会话 ID 为空错误
TEST_F(AiServiceImplTest, DeleteSessionChatSessionIdEmpty)
{
    DeleteSessionRequest request;
    request.set_request_id("rid_delete_empty_ssid");
    request.set_user_id("uid_impl_delete");
    DeleteSessionResponse response;

    ai_service_impl_->DeleteSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_CHAT_SESSION_ID_EMPTY));
}

// 删除指定会话 : 会话不存在时返回会话数据不存在错误
TEST_F(AiServiceImplTest, DeleteSessionNotFound)
{
    DeleteSessionRequest request;
    request.set_request_id("rid_delete_not_found");
    request.set_user_id("uid_impl_delete");
    request.set_chat_session_id("csid_not_exist");
    DeleteSessionResponse response;

    ai_service_impl_->DeleteSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_DATA_NOT_FOUND));
}

// 更新会话文件关联 : 更新后历史消息接口返回关联的文件 ID
TEST_F(AiServiceImplTest, UpdateSessionFile)
{
    CreateChatSessionRequest create_request;
    create_request.set_request_id("rid_update_file_create");
    create_request.set_user_id("uid_impl_file");
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse create_response;
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    const std::string chat_session_id = create_response.result().session().chat_session_id();

    UpdateSessionFileRequest request;
    request.set_request_id("rid_update_file");
    request.set_user_id("uid_impl_file");
    request.set_chat_session_id(chat_session_id);
    request.set_file_id("fid_impl_file");
    UpdateSessionFileResponse response;

    ai_service_impl_->UpdateSessionFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_update_file");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 通过历史消息接口校验文件 ID 已关联
    GetSessionHistoryRequest history_request;
    history_request.set_request_id("rid_update_file_check");
    history_request.set_user_id("uid_impl_file");
    history_request.set_chat_session_id(chat_session_id);
    GetSessionHistoryResponse history_response;
    ai_service_impl_->GetSessionHistory(nullptr, &history_request, &history_response, nullptr);
    EXPECT_EQ(history_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(history_response.result().file_id(), "fid_impl_file");
}

// 更新会话文件关联 : 文件 ID 为空时返回文件 ID 为空错误
TEST_F(AiServiceImplTest, UpdateSessionFileFileIdEmpty)
{
    UpdateSessionFileRequest request;
    request.set_request_id("rid_update_file_empty_fid");
    request.set_user_id("uid_impl_file");
    request.set_chat_session_id("csid_not_exist");
    UpdateSessionFileResponse response;

    ai_service_impl_->UpdateSessionFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::AI_SERVICE_FILE_ID_EMPTY));
}

// 更新会话文件关联 : 会话不存在时返回会话数据不存在错误
TEST_F(AiServiceImplTest, UpdateSessionFileNotFound)
{
    UpdateSessionFileRequest request;
    request.set_request_id("rid_update_file_not_found");
    request.set_user_id("uid_impl_file");
    request.set_chat_session_id("csid_not_exist");
    request.set_file_id("fid_impl_file");
    UpdateSessionFileResponse response;

    ai_service_impl_->UpdateSessionFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_DATA_NOT_FOUND));
}

// 更新会话文件关联 : 会话不属于当前用户时返回归属校验错误
TEST_F(AiServiceImplTest, UpdateSessionFileUserMismatch)
{
    CreateChatSessionRequest create_request;
    create_request.set_request_id("rid_update_file_mismatch_create");
    create_request.set_user_id("uid_impl_1");
    create_request.set_model("deepseek-chat");
    create_request.set_session_type(kSessionTypeExcel);
    CreateChatSessionResponse create_response;
    ai_service_impl_->CreateSession(nullptr, &create_request, &create_response, nullptr);
    const std::string chat_session_id = create_response.result().session().chat_session_id();

    UpdateSessionFileRequest request;
    request.set_request_id("rid_update_file_mismatch");
    request.set_user_id("uid_impl_2");
    request.set_chat_session_id(chat_session_id);
    request.set_file_id("fid_impl_file");
    UpdateSessionFileResponse response;

    ai_service_impl_->UpdateSessionFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::CHAT_SESSION_USER_MISMATCH));
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察 RPC 接口实现层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "ai_service_impl_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
