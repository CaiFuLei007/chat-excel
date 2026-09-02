#pragma once

#include <memory>
#include <string>
#include <vector>
#include "data/chat_session_data.h"
#include "svc_ai_service/common.h"

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief 聊天会话管理类, 对聊天会话数据访问类 ChatSessionData 进行封装,
 *        实现聊天会话元数据的持久化存储, Redis 缓存读写以及数据库操作的编排
 *        读策略(Cache-Aside): 先从缓存中读取, 未命中时读取 MySQL 并回填缓存;
 *        写策略(Cache-Aside): 先修改 MySQL, 再删除 Redis 缓存等待后续重新缓存
 */
class ChatSessionManager
{
public:
    /**
     * @brief 构造函数, 注入聊天会话数据访问对象, 所有成员变量在构造函数中完成初始化
     * @param chat_session_data 聊天会话数据访问对象, 由上层创建并统一管理
     */
    explicit ChatSessionManager(std::shared_ptr<ChatSessionData> chat_session_data);

    /**
     * @brief 检测指定聊天会话是否属于指定用户, 会话不存在时抛出异常
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @return 会话属于该用户返回 true, 不属于返回 false
     */
    bool CheckChatSessionOwner(const std::string& user_id, const std::string& chat_session_id);

    /**
     * @brief 保存或更新聊天会话元数据(写策略 Cache-Aside: 先修改 MySQL, 再删除 Redis 缓存)
     * @param chat_session_info 聊天会话元数据
     */
    void SaveOrUpdateChatSession(const ChatSessionInfo& chat_session_info);

    /**
     * @brief 通过用户 ID 获取该用户的所有聊天会话元数据, 直接从 MySQL 中读取
     * @param user_id 用户 ID
     * @return 聊天会话元数据列表, 用户没有聊天会话时返回空列表
     */
    std::vector<ChatSessionInfo> GetChatSessionListByUserId(const std::string& user_id);

    /**
     * @brief 通过文件 ID 获取关联该文件的所有聊天会话元数据, 直接从 MySQL 中读取
     * @param file_id 文件 ID
     * @return 聊天会话元数据列表, 没有会话关联该文件时返回空列表
     */
    std::vector<ChatSessionInfo> GetChatSessionListByFileId(const std::string& file_id);

    /**
     * @brief 通过会话 ID 获取指定聊天会话元数据(读策略 Cache-Aside: 先查缓存,
     *        未命中时查 MySQL 并回填缓存), 会话不存在时抛出异常
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话元数据
     */
    ChatSessionInfo GetChatSessionBySessionId(const std::string& chat_session_id);

    /**
     * @brief 通过会话 ID 删除指定聊天会话元数据(先删除 MySQL, 再删除 Redis 缓存)
     * @param chat_session_id 聊天会话 ID
     */
    void DeleteChatSessionBySessionId(const std::string& chat_session_id);

    /**
     * @brief 将文件 ID 与聊天会话关联起来, 修改聊天会话元数据的 file_id 字段
     *        并删除缓存(写策略 Cache-Aside), 会话不存在或不属于指定用户时抛出异常
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @param file_id 文件 ID
     */
    void UpdateChatSessionFileId(const std::string& user_id, const std::string& chat_session_id,
                                 const std::string& file_id);

private:
    // 聊天会话数据访问对象
    std::shared_ptr<ChatSessionData> chat_session_data_;
};

} // namespace ai_service
} // namespace chat_excel
