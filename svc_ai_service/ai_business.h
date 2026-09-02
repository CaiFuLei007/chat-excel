#pragma once

#include <memory>
#include <string>
#include <vector>
#include <aichat_sdk/aichat_sdk.h>
#include "svc_ai_service/chat_session_manager.h"
#include "svc_ai_service/common.h"

namespace chat_excel
{
namespace ai_service
{


/**
 * @brief AI 业务逻辑类, 负责管理 AI 业务逻辑, 组织聊天会话管理类与 ChatSDK 的调用,
 *        提供模型列表获取, 聊天会话的新建/查询/删除, 历史消息获取,
 *        以及会话与文件关联关系维护等业务逻辑
 *        ChatSDK 只负责会话与消息的管理, 聊天会话元数据的归属关系以
 *        tbl_chat_session 表为准, 会话 ID 使用 ChatSDK 生成的会话 ID
 */
class AiBusiness
{
public:
    /**
     * @brief 构造函数, 注入业务依赖对象, 所有成员变量在构造函数中完成初始化
     * @param chat_session_manager 聊天会话管理对象, 由上层创建并统一管理
     * @param ai_chat_sdk ChatSDK 实例指针, 用于新建聊天会话, 获取指定聊天会话等操作
     */
    AiBusiness(std::shared_ptr<ChatSessionManager> chat_session_manager,
               std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk);

    /**
     * @brief 获取可用的模型列表, 直接透传 ChatSDK 中已注册并初始化的模型信息
     * @return 可用的模型列表, 没有可用模型时返回空列表
     */
    std::vector<aichat_sdk::ModelInfo> GetModels();

    /**
     * @brief 新建聊天会话, 调用 ChatSDK 创建会话并将会话元数据保存到 MySQL,
     *        会话标题默认为第一条消息(发送消息时更新), 新建时为空, 总消息数为 0;
     *        ChatSDK 不管理用户信息, 创建会话时直接使用系统 user_id 作为会话归属用户
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param model_name 模型名称
     * @param session_type 会话类型(excel / database)
     * @param db_connection_info 数据库连接信息 JSON, 仅 database 类型会话使用
     * @return 新建的聊天会话 ID
     */
    std::string CreateChatSession(const std::string& request_id, const std::string& user_id,
                                  const std::string& model_name, const std::string& session_type,
                                  const std::string& db_connection_info);

    /**
     * @brief 获取指定用户的聊天会话列表, 直接从 MySQL 中读取
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @return 聊天会话元数据列表, 用户没有聊天会话时返回空列表
     */
    std::vector<ChatSessionInfo> GetChatSessionList(const std::string& request_id,
                                                    const std::string& user_id);

    /**
     * @brief 通过会话 ID 获取指定聊天会话元数据, 会话不存在或不属于当前用户时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话元数据
     */
    ChatSessionInfo GetChatSession(const std::string& request_id, const std::string& user_id,
                                   const std::string& chat_session_id);

    /**
     * @brief 获取指定会话 ID 的历史消息, 会话元数据来自 MySQL(校验会话归属),
     *        历史消息来自 ChatSDK, ChatSDK 中会话不存在时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话元数据与历史消息
     */
    ChatSessionHistory GetSessionHistory(const std::string& request_id, const std::string& user_id,
                                         const std::string& chat_session_id);

    /**
     * @brief 删除指定用户的指定聊天会话元数据, 同时删除 ChatSDK 中的会话与消息,
     *        会话不存在或不属于当前用户时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     */
    void DeleteChatSession(const std::string& request_id, const std::string& user_id,
                           const std::string& chat_session_id);

    /**
     * @brief 更新聊天会话关联的文件 ID 字段, 会话不存在或不属于当前用户时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @param file_id 文件 ID
     */
    void UpdateChatSessionFileId(const std::string& request_id, const std::string& user_id,
                                 const std::string& chat_session_id, const std::string& file_id);

    /**
     * @brief 删除指定用户关联了指定文件的所有聊天会话(元数据与 ChatSDK 中的会话与消息),
     *        文件删除时联动清理关联会话, 会话不存在时跳过不抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     */
    void DeleteChatSessionsByFileId(const std::string& request_id, const std::string& user_id,
                                    const std::string& file_id);

private:
    /**
     * @brief 获取聊天会话元数据并校验会话归属(读策略 Cache-Aside),
     *        会话不存在时抛出 CHAT_SESSION_DATA_NOT_FOUND,
     *        会话不属于当前用户时抛出 CHAT_SESSION_USER_MISMATCH
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param chat_session_id 聊天会话 ID
     * @return 聊天会话元数据
     */
    ChatSessionInfo GetChatSessionWithOwnerCheck(const std::string& request_id,
                                                 const std::string& user_id,
                                                 const std::string& chat_session_id);

    // 聊天会话管理对象, 用于操作管理聊天会话元数据
    std::shared_ptr<ChatSessionManager> chat_session_manager_;

    // ChatSDK 实例指针, 用于新建聊天会话, 获取指定聊天会话, 删除会话等操作
    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk_;
};

} // namespace ai_service
} // namespace chat_excel
