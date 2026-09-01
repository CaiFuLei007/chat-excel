#pragma once

#include <string>
#include <vector>
#include <aichat_sdk/base/common.h>

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief 聊天会话信息结构体, 用于数据层与业务层之间传递聊天会话元信息
 *        title / file_id / connection_info 在数据库中可为空, 采用空字符串约定 :
 *        空串语义为数据库中的 NULL, 转换逻辑在数据层完成
 */
struct ChatSessionInfo
{
    // 聊天会话 ID, 全系统唯一
    std::string chat_session_id;

    // 会话所属用户 ID
    std::string user_id;

    // 会话标题, 默认为第一条消息, 最长 20 个字符, 空串语义为尚未设置标题
    std::string title;

    // 会话创建时间戳
    unsigned long long create_time = 0;

    // 会话最近一次消息时间戳, 随消息收发更新
    unsigned long long update_time = 0;

    // 会话总消息数
    unsigned long long total_message_count = 0;

    // 会话使用的模型名称
    std::string model_name;

    // 会话关联的文件 ID, 空串语义为尚未关联文件
    std::string file_id;

    // 会话类型(excel / database), 前端根据会话类型跳转到对应的聊天页面
    std::string type;

    // 数据库连接信息, 仅 database 类型会话使用, 空串语义为无数据库连接信息
    std::string connection_info;
};

/**
 * @brief 聊天会话历史消息结构体, 用于业务层返回指定聊天会话的元数据与历史消息,
 *        元数据来自 AI 子服务的 MySQL 数据库, 历史消息来自 ChatSDK 的本地存储;
 *        该结构体依赖 aichat_sdk 的类型, 定义在 aichat_sdk.h 包含之后
 */
struct ChatSessionHistory
{
    // 聊天会话元数据
    ChatSessionInfo chat_session_info;

    // 会话的历史消息列表
    std::vector<aichat_sdk::Message> messages;
};


} // namespace ai_service
} // namespace chat_excel
