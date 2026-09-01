#pragma once

#include <functional>
#include <string>
#include <vector>
#include <aichat_sdk/base/common.h>

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief 流式输出回调类型, 消息处理过程中通过该回调将模型响应与最终结果实时发送给前端,
 *        done 为 true 表示本次发送消息流程结束, 回调在 RPC 接口层实现具体的流式写出逻辑
 * @param content 本次输出的消息片段内容
 * @param done 本次发送消息流程是否结束
 */
using StreamCallback = std::function<void(const std::string& content, bool done)>;

/**
 * @brief 发送消息上下文结构体, 承载一次发送消息请求的全部参数,
 *        由 RPC 接口层从 RPC 请求中解析组装
 */
struct SendMessageContext
{
    // 请求 ID, 用于日志链路追踪
    std::string request_id;

    // 网关会话 ID, 调用其他子服务 RPC 时原样透传
    std::string session_id;

    // 用户 ID
    std::string user_id;

    // AI 聊天会话 ID, 即 ChatSDK 中的会话 ID
    std::string chat_session_id;

    // 聊天类型: plain / excel / database
    std::string chat_type;

    // 用户消息内容
    std::string message;

    // 文件 ID, 仅 excel 场景使用, 为空时使用会话元数据中关联的文件 ID
    std::string file_id;

    // 数据库类型(数据库场景), 取值与 proto DataType 一致: EXCEL = 0, MYSQL = 1, SQLITE = 2
    int db_type = 0;

    // 数据库连接 ID, 仅 database 场景使用
    std::string db_connect_id;

    // 数据库表名列表, 仅 database 场景使用, 由 RPC 请求中的表名字段按逗号拆分
    std::vector<std::string> table_names;
};

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
