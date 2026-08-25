#pragma once

#include <memory>
#include <optional>
#include <string>
#include "data/session_data.h"
#include "data/user_data.h"
#include "svc_user_service/common.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 会话管理类, 负责会话的创建、有效性检查、删除,
 *        以及通过用户 ID 获取用户信息;
 *        组织会话缓存与用户缓存的读写时机(Cache-Aside 旁路缓存策略)
 */
class SessionManager
{
public:
    /**
     * @brief 构造函数, 注入会话数据访问对象与用户数据访问对象
     * @param session_data 会话数据访问对象, 由上层创建并统一管理
     * @param user_data 用户数据访问对象, 由上层创建并统一管理
     */
    SessionManager(std::shared_ptr<SessionData> session_data, std::shared_ptr<UserData> user_data);

    /**
     * @brief 创建会话, 会话 ID 使用 uuid 生成器生成,
     *        会话信息存储到数据库与缓存中
     * @param user_id 会话所属的用户 ID
     * @return 创建成功的会话 ID
     */
    std::string CreateSession(const std::string& user_id);

    /**
     * @brief 检查会话是否有效, 先检查缓存再检查数据库,
     *        会话存在且会话所属用户处于上线状态时会话有效
     * @param session_id 会话 ID
     * @return 会话有效返回 true, 会话不存在或用户未上线返回 false
     */
    bool CheckSessionValid(const std::string& session_id);

    /**
     * @brief 删除会话, 先删除数据库中的会话, 再删除缓存中的会话
     * @param session_id 会话 ID
     */
    void DeleteSession(const std::string& session_id);

    /**
     * @brief 通过用户 ID 获取用户信息, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param user_id 用户 ID
     * @return 用户信息
     */
    UserInfo GetUserInfoByUserId(const std::string& user_id);

    /**
     * @brief 通过会话 ID 获取用户 ID, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param session_id 会话 ID
     * @return 用户 ID
     */
    std::string GetUserIdBySessionId(const std::string& session_id);

private:
    /**
     * @brief 通过会话 ID 获取会话信息, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param session_id 会话 ID
     * @return 会话信息, 会话不存在时返回 std::nullopt
     */
    std::optional<SessionInfo> GetSessionBySessionIdWithCache(const std::string& session_id);

    /**
     * @brief 通过用户 ID 获取用户信息, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param user_id 用户 ID
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByUserIdWithCache(const std::string& user_id);

    // 会话数据访问对象
    std::shared_ptr<SessionData> session_data_;

    // 用户数据访问对象
    std::shared_ptr<UserData> user_data_;
};

} // namespace user_service
} // namespace chat_excel
