#include "svc_ai_service/ai_business.h"

#include <ctime>
#include <utility>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace ai_service
{

AiBusiness::AiBusiness(std::shared_ptr<ChatSessionManager> chat_session_manager,
                       std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk)
    : chat_session_manager_(std::move(chat_session_manager)),
      ai_chat_sdk_(std::move(ai_chat_sdk))
{
}

std::vector<aichat_sdk::ModelInfo> AiBusiness::GetModels()
{
    return ai_chat_sdk_->GetAllModels();
}

std::string AiBusiness::CreateChatSession(const std::string& request_id, const std::string& user_id,
                                          const std::string& model_name, const std::string& session_type,
                                          const std::string& db_connection_info)
{
    // 调用 ChatSDK 创建聊天会话, ChatSDK 不管理用户信息, 直接使用系统 user_id 作为会话归属用户
    const std::string chat_session_id = ai_chat_sdk_->CreateSession(user_id, model_name);
    if (chat_session_id.empty())
    {
        ERR("ChatSDK 创建聊天会话失败, request_id: {}, user_id: {}, model_name: {}",
            request_id, user_id, model_name);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_CREATE_SESSION_ERROR);
    }

    // 构建聊天会话元数据, 标题默认为第一条消息(发送消息时更新), 新建时为空, 总消息数为 0
    ChatSessionInfo chat_session_info;
    chat_session_info.chat_session_id = chat_session_id;
    chat_session_info.user_id = user_id;
    chat_session_info.create_time = static_cast<unsigned long long>(std::time(nullptr));
    chat_session_info.update_time = chat_session_info.create_time;
    chat_session_info.model_name = model_name;
    chat_session_info.type = session_type;
    chat_session_info.connection_info = db_connection_info;

    // 保存聊天会话元数据(写策略 Cache-Aside: 修改 MySQL 后删除缓存)
    chat_session_manager_->SaveOrUpdateChatSession(chat_session_info);
    INFO("新建聊天会话成功, request_id: {}, chat_session_id: {}, user_id: {}, model_name: {}",
         request_id, chat_session_id, user_id, model_name);
    return chat_session_id;
}

std::vector<ChatSessionInfo> AiBusiness::GetChatSessionList(const std::string& request_id,
                                                            const std::string& user_id)
{
    const std::vector<ChatSessionInfo> chat_session_list =
        chat_session_manager_->GetChatSessionListByUserId(user_id);
    INFO("获取用户聊天会话列表成功, request_id: {}, user_id: {}, 会话数量: {}",
         request_id, user_id, chat_session_list.size());
    return chat_session_list;
}

ChatSessionInfo AiBusiness::GetChatSession(const std::string& request_id, const std::string& user_id,
                                           const std::string& chat_session_id)
{
    return GetChatSessionWithOwnerCheck(request_id, user_id, chat_session_id);
}

ChatSessionHistory AiBusiness::GetSessionHistory(const std::string& request_id,
                                                 const std::string& user_id,
                                                 const std::string& chat_session_id)
{
    // 获取聊天会话元数据并校验会话归属
    ChatSessionHistory chat_session_history;
    chat_session_history.chat_session_info =
        GetChatSessionWithOwnerCheck(request_id, user_id, chat_session_id);

    // 从 ChatSDK 中获取会话信息, 历史消息存储在 ChatSDK 中
    const std::shared_ptr<aichat_sdk::Session> session = ai_chat_sdk_->GetSession(chat_session_id);
    if (session == nullptr)
    {
        ERR("ChatSDK 中聊天会话不存在, request_id: {}, chat_session_id: {}",
            request_id, chat_session_id);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_SESSION_NOT_FOUND);
    }
    chat_session_history.messages = session->messages;
    INFO("获取聊天会话历史消息成功, request_id: {}, chat_session_id: {}, 消息数量: {}",
         request_id, chat_session_id, chat_session_history.messages.size());
    return chat_session_history;
}

void AiBusiness::DeleteChatSession(const std::string& request_id, const std::string& user_id,
                                   const std::string& chat_session_id)
{
    // 获取聊天会话元数据并校验会话归属
    GetChatSessionWithOwnerCheck(request_id, user_id, chat_session_id);

    // 先删除 MySQL 中的会话元数据与 Redis 缓存, 再删除 ChatSDK 中的会话与消息,
    // 元数据删除后即使 ChatSDK 删除失败也可以通过重试完成清理
    chat_session_manager_->DeleteChatSessionBySessionId(chat_session_id);
    if (!ai_chat_sdk_->RemoveSession(chat_session_id))
    {
        ERR("ChatSDK 删除聊天会话失败, request_id: {}, chat_session_id: {}",
            request_id, chat_session_id);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_REMOVE_SESSION_ERROR);
    }
    INFO("删除聊天会话成功, request_id: {}, chat_session_id: {}, user_id: {}",
         request_id, chat_session_id, user_id);
}

void AiBusiness::UpdateChatSessionFileId(const std::string& request_id, const std::string& user_id,
                                         const std::string& chat_session_id, const std::string& file_id)
{
    // 修改聊天会话元数据的 file_id 字段并保存更新(内部会校验会话归属并删除缓存)
    chat_session_manager_->UpdateChatSessionFileId(user_id, chat_session_id, file_id);
    INFO("更新聊天会话关联的文件 ID 成功, request_id: {}, chat_session_id: {}, file_id: {}",
         request_id, chat_session_id, file_id);
}

ChatSessionInfo AiBusiness::GetChatSessionWithOwnerCheck(const std::string& request_id,
                                                         const std::string& user_id,
                                                         const std::string& chat_session_id)
{
    // 读策略(Cache-Aside)获取聊天会话元数据, 不存在时抛出异常
    const ChatSessionInfo chat_session_info =
        chat_session_manager_->GetChatSessionBySessionId(chat_session_id);

    // 检查当前 user_id 与会话元数据中的 user_id 是否一致
    if (chat_session_info.user_id != user_id)
    {
        ERR("聊天会话不属于当前用户, request_id: {}, chat_session_id: {}, 当前用户: {}, 会话属主: {}",
            request_id, chat_session_id, user_id, chat_session_info.user_id);
        throw ChatExcelException(ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }
    return chat_session_info;
}

} // namespace ai_service
} // namespace chat_excel
