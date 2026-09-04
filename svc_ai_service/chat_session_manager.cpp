#include "svc_ai_service/chat_session_manager.h"

#include <optional>
#include <utility>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace ai_service
{

ChatSessionManager::ChatSessionManager(std::shared_ptr<ChatSessionData> chat_session_data)
    : chat_session_data_(std::move(chat_session_data))
{
}

bool ChatSessionManager::CheckChatSessionOwner(const std::string& user_id,
                                               const std::string& chat_session_id)
{
    // 获取聊天会话元数据(不存在时抛出异常), 比较会话属主与当前用户
    const ChatSessionInfo chat_session_info = GetChatSessionBySessionId(chat_session_id);
    return chat_session_info.user_id == user_id;
}

void ChatSessionManager::SaveOrUpdateChatSession(const ChatSessionInfo& chat_session_info)
{
    // 合并已有会话的文件关联与连接信息:
    // 消息轮次处理时, 会话元数据在发送开始即被读取并缓存在内存中; 若处理期间文件/数据库
    // 连接映射已建立(例如前端先触发 fileChatMap 再发消息的竞态), 携带的 file_id /
    // connection_info 可能仍为空, 直接保存会把刚落库的映射值覆盖为空串(NULL),
    // 导致历史会话接口(A04)拿不到 fileId、数据库会话拿不到连接信息。
    // 因此保存前对这两个字段执行"传入为空则保留数据库现值"的合并, 防止映射被空值清掉。
    ChatSessionInfo merged_info = chat_session_info;
    try
    {
        std::optional<ChatSessionInfo> existing_info =
            chat_session_data_->GetChatSessionBySessionId(chat_session_info.chat_session_id);
        if (existing_info)
        {
            if (merged_info.file_id.empty() && !existing_info->file_id.empty())
            {
                merged_info.file_id = existing_info->file_id;
            }
            if (merged_info.connection_info.empty() && !existing_info->connection_info.empty())
            {
                merged_info.connection_info = existing_info->connection_info;
            }
        }
    }
    catch (const ChatExcelException& e)
    {
        // 会话记录不存在时按传入信息原样保存(会话创建场景), 不阻塞保存流程
        ERR("保存会话元数据前合并文件/连接信息失败, chat_session_id: {}, 错误: {}",
            chat_session_info.chat_session_id, e.what());
    }

    // 保存或更新 MySQL 中的聊天会话元数据
    chat_session_data_->SaveOrUpdateChatSession(merged_info);

    // 写策略(Cache-Aside): 修改 MySQL 后删除 Redis 缓存, 等待后续重新缓存
    chat_session_data_->DeleteChatSessionBySessionIdFromCache(merged_info.chat_session_id);
    INFO("保存或更新聊天会话元数据成功, chat_session_id: {}", merged_info.chat_session_id);
}

std::vector<ChatSessionInfo> ChatSessionManager::GetChatSessionListByUserId(const std::string& user_id)
{
    return chat_session_data_->GetChatSessionListByUserId(user_id);
}

std::vector<ChatSessionInfo> ChatSessionManager::GetChatSessionListByFileId(const std::string& file_id)
{
    return chat_session_data_->GetChatSessionListByFileId(file_id);
}

ChatSessionInfo ChatSessionManager::GetChatSessionBySessionId(const std::string& chat_session_id)
{
    // 读策略(Cache-Aside): 先从缓存中读取聊天会话元数据
    std::optional<ChatSessionInfo> chat_session_info =
        chat_session_data_->GetChatSessionBySessionIdFromCache(chat_session_id);
    if (chat_session_info)
    {
        return *chat_session_info;
    }

    // 缓存未命中, 到 MySQL 中读取聊天会话元数据
    chat_session_info = chat_session_data_->GetChatSessionBySessionId(chat_session_id);
    if (!chat_session_info)
    {
        ERR("聊天会话元数据不存在, chat_session_id: {}", chat_session_id);
        throw ChatExcelException(ErrorCode::CHAT_SESSION_DATA_NOT_FOUND);
    }

    // 将数据库读取到的聊天会话元数据回填到缓存中
    chat_session_data_->SaveChatSessionToCache(*chat_session_info);
    return *chat_session_info;
}

void ChatSessionManager::DeleteChatSessionBySessionId(const std::string& chat_session_id)
{
    // 先删除 MySQL 中的聊天会话元数据, 再删除 Redis 缓存中的聊天会话元数据
    chat_session_data_->DeleteChatSessionBySessionId(chat_session_id);
    chat_session_data_->DeleteChatSessionBySessionIdFromCache(chat_session_id);
    INFO("删除聊天会话元数据成功, chat_session_id: {}", chat_session_id);
}

void ChatSessionManager::UpdateChatSessionFileId(const std::string& user_id,
                                                 const std::string& chat_session_id,
                                                 const std::string& file_id)
{
    // 获取聊天会话元数据(不存在时抛出异常)并校验会话归属
    ChatSessionInfo chat_session_info = GetChatSessionBySessionId(chat_session_id);
    if (chat_session_info.user_id != user_id)
    {
        ERR("聊天会话不属于当前用户, chat_session_id: {}, 当前用户: {}, 会话属主: {}",
            chat_session_id, user_id, chat_session_info.user_id);
        throw ChatExcelException(ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }

    // 修改聊天会话元数据的 file_id 字段并保存更新(内部会删除缓存)
    chat_session_info.file_id = file_id;
    SaveOrUpdateChatSession(chat_session_info);
    INFO("更新聊天会话关联的文件 ID 成功, chat_session_id: {}, file_id: {}", chat_session_id, file_id);
}

} // namespace ai_service
} // namespace chat_excel
