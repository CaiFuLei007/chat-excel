#include "session_manager.h"

#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"

namespace chat_excel
{
namespace user_service
{

SessionManager::SessionManager(std::shared_ptr<SessionData> session_data,
                               std::shared_ptr<UserData> user_data)
    : session_data_(std::move(session_data)),
      user_data_(std::move(user_data))
{
}

std::string SessionManager::CreateSession(const std::string& user_id)
{
    // 构建会话信息, 会话 ID 使用 uuid 生成器生成
    SessionInfo session_info;
    session_info.session_id = cpp_toolkit::UuidUtil::GenerateUuidV4();
    session_info.user_id = user_id;

    // 将会话信息存储到数据库和缓存中
    session_data_->SaveSession(session_info);
    session_data_->SaveSessionToCache(session_info);
    INFO("创建会话成功, session_id: {}, user_id: {}", session_info.session_id, user_id);
    return session_info.session_id;
}

bool SessionManager::CheckSessionValid(const std::string& session_id, std::string& user_id)
{
    // 会话无效时用户 ID 输出为空
    user_id.clear();

    // 先检查缓存中的会话, 缓存未命中再检查数据库中的会话, 会话不存在时会话无效
    std::optional<SessionInfo> session_info = GetSessionBySessionIdWithCache(session_id);
    if (!session_info)
    {
        WARN("检查会话有效性失败, 会话不存在, session_id: {}", session_id);
        return false;
    }

    // 通过会话中的用户 ID 获取用户信息, 用户不存在时会话无效
    std::optional<UserInfo> user_info = GetUserByUserIdWithCache(session_info->user_id);
    if (!user_info)
    {
        WARN("检查会话有效性失败, 会话所属用户不存在, session_id: {}, user_id: {}",
             session_id, session_info->user_id);
        return false;
    }

    // 检查用户状态是否上线
    if (user_info->status != UserStatus::LOGGED_IN)
    {
        WARN("检查会话有效性失败, 用户未上线, session_id: {}, user_id: {}",
             session_id, session_info->user_id);
        return false;
    }

    // 会话有效, 输出会话所属的用户 ID
    user_id = session_info->user_id;
    return true;
}

void SessionManager::DeleteSession(const std::string& session_id)
{
    // 先删除 MySQL 中的会话, 再删除 Redis 缓存中的会话
    session_data_->DeleteSessionBySessionId(session_id);
    session_data_->DeleteSessionBySessionIdFromCache(session_id);
    INFO("删除会话成功, session_id: {}", session_id);
}

UserInfo SessionManager::GetUserInfoByUserId(const std::string& user_id)
{
    // 先读取缓存中的用户信息, 缓存未命中再读取数据库并回写缓存
    std::optional<UserInfo> user_info = GetUserByUserIdWithCache(user_id);
    if (!user_info)
    {
        ERR("通过用户 ID 获取用户信息失败, 用户不存在, user_id: {}", user_id);
        throw ChatExcelException(ErrorCode::USER_DATA_NOT_FOUND);
    }
    return *user_info;
}

std::string SessionManager::GetUserIdBySessionId(const std::string& session_id)
{
    // 先读取缓存中的会话信息, 缓存未命中再读取数据库并回写缓存
    std::optional<SessionInfo> session_info = GetSessionBySessionIdWithCache(session_id);
    if (!session_info)
    {
        ERR("通过会话 ID 获取用户 ID 失败, 会话不存在, session_id: {}", session_id);
        throw ChatExcelException(ErrorCode::SESSION_NOT_FOUND);
    }
    return session_info->user_id;
}

std::optional<SessionInfo> SessionManager::GetSessionBySessionIdWithCache(const std::string& session_id)
{
    // 先读取缓存中的会话信息
    std::optional<SessionInfo> session_info = session_data_->GetSessionBySessionIdFromCache(session_id);
    if (session_info)
    {
        return session_info;
    }

    // 缓存未命中, 读取数据库中的会话信息并回写缓存
    session_info = session_data_->GetSessionBySessionId(session_id);
    if (session_info)
    {
        session_data_->SaveSessionToCache(*session_info);
    }
    return session_info;
}

std::optional<UserInfo> SessionManager::GetUserByUserIdWithCache(const std::string& user_id)
{
    // 先读取缓存中的用户信息
    std::optional<UserInfo> user_info = user_data_->GetUserByUserIdFromCache(user_id);
    if (user_info)
    {
        return user_info;
    }

    // 缓存未命中, 读取数据库中的用户信息并回写缓存
    user_info = user_data_->GetUserByUserId(user_id);
    if (user_info)
    {
        user_data_->SaveUserToCache(*user_info);
    }
    return user_info;
}

} // namespace user_service
} // namespace chat_excel
