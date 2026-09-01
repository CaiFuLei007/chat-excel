#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <odb/database.hxx>
#include <sw/redis++/redis.h>
#include "svc_ai_service/common.h"

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief 聊天会话数据访问类, 封装聊天会话表(tbl_chat_session)的 MySQL 操作
 *        与聊天会话数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 缓存读写时机等业务逻辑由上层实现(Cache-Aside 旁路策略);
 *        缓存结构为 hash 类型, key 为 chat_session_data, field 为 chat_session:{session_id},
 *        value 为包含会话 ID, 用户 ID, 标题, 创建时间, 最近一次消息时间, 总消息数,
 *        模型名称, 文件 ID, 会话类型, 连接信息的 JSON 字符串, 过期时间 3 天
 */
class ChatSessionData
{
public:
    /**
     * @brief 构造函数, 注入 MySQL 操作句柄与 Redis 操作句柄
     * @param mysql_handle MySQL 操作句柄, 由上层创建并统一管理
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    ChatSessionData(std::shared_ptr<odb::database> mysql_handle, std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存或更新会话信息到数据库, 会话不存在时插入新记录,
     *        已存在时更新可变字段(标题, 最近一次消息时间, 总消息数, 文件 ID, 连接信息);
     *        用户 ID, 创建时间, 模型名称, 会话类型为会话固有属性, 更新时保持数据库中的值不变
     * @param chat_session_info 聊天会话信息, 其中会话 ID 用于定位会话
     */
    void SaveOrUpdateChatSession(const ChatSessionInfo& chat_session_info);

    /**
     * @brief 通过会话 ID 获取聊天会话信息
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话信息, 会话不存在时返回 std::nullopt
     */
    std::optional<ChatSessionInfo> GetChatSessionBySessionId(const std::string& chat_session_id);

    /**
     * @brief 通过会话 ID 删除数据库中的聊天会话信息, 会话不存在时不抛出异常
     * @param chat_session_id 聊天会话 ID
     */
    void DeleteChatSessionBySessionId(const std::string& chat_session_id);

    /**
     * @brief 通过用户 ID 获取用户的所有聊天会话信息
     * @param user_id 用户 ID
     * @return 聊天会话信息列表, 用户没有聊天会话时返回空列表
     */
    std::vector<ChatSessionInfo> GetChatSessionListByUserId(const std::string& user_id);

    /**
     * @brief 保存聊天会话数据到缓存, 通过事务批量执行写入与过期时间设置
     * @param chat_session_info 聊天会话信息
     */
    void SaveChatSessionToCache(const ChatSessionInfo& chat_session_info);

    /**
     * @brief 通过会话 ID 从缓存获取聊天会话信息
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<ChatSessionInfo> GetChatSessionBySessionIdFromCache(const std::string& chat_session_id);

    /**
     * @brief 通过会话 ID 删除缓存中的聊天会话数据
     * @param chat_session_id 聊天会话 ID
     */
    void DeleteChatSessionBySessionIdFromCache(const std::string& chat_session_id);

private:
    // MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle_;

    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace ai_service
} // namespace chat_excel
