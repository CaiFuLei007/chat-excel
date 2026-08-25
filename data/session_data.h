#pragma once

#include <memory>
#include <optional>
#include <string>
#include <odb/database.hxx>
#include <sw/redis++/redis.h>
#include "svc_user_service/common.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 会话数据访问类, 封装会话表(tbl_session)的 MySQL 操作与会话数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 缓存读写时机等业务逻辑由上层实现;
 *        缓存结构为 hash 类型, key 为 session_data, field 为 session:{session_id},
 *        value 为包含会话 ID 与用户 ID 的 JSON 字符串, 过期时间 3 天
 */
class SessionData
{
public:
    /**
     * @brief 构造函数, 注入 MySQL 操作句柄与 Redis 操作句柄
     * @param mysql_handle MySQL 操作句柄, 由上层创建并统一管理
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    SessionData(std::shared_ptr<odb::database> mysql_handle, std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存会话信息到数据库
     * @param session_info 会话信息
     */
    void SaveSession(const SessionInfo& session_info);

    /**
     * @brief 通过会话 ID 获取会话信息
     * @param session_id 会话 ID
     * @return 会话信息, 会话不存在时返回 std::nullopt
     */
    std::optional<SessionInfo> GetSessionBySessionId(const std::string& session_id);

    /**
     * @brief 通过会话 ID 删除数据库中的会话信息
     * @param session_id 会话 ID
     */
    void DeleteSessionBySessionId(const std::string& session_id);

    /**
     * @brief 保存会话数据到缓存, 通过事务批量执行写入与过期时间设置
     * @param session_info 会话信息
     */
    void SaveSessionToCache(const SessionInfo& session_info);

    /**
     * @brief 通过会话 ID 从缓存获取会话信息
     * @param session_id 会话 ID
     * @return 会话信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<SessionInfo> GetSessionBySessionIdFromCache(const std::string& session_id);

    /**
     * @brief 通过会话 ID 删除缓存中的会话信息
     * @param session_id 会话 ID
     */
    void DeleteSessionBySessionIdFromCache(const std::string& session_id);

private:
    // MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle_;

    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace user_service
} // namespace chat_excel
