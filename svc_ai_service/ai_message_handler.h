#pragma once

#include <memory>
#include <string>
#include <vector>
#include <aichat_sdk/aichat_sdk.h>
#include <cpp-toolkit/rpc.h>
#include "svc_ai_service/chat_session_manager.h"
#include "svc_ai_service/common.h"

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief AI 消息处理类, 负责 AI 子服务中给模型发送消息的完整业务流程,
 *        支持普通聊天(plain), Excel 聊天(excel)与数据库聊天(database)三种场景;
 *        Excel/数据库场景通过跨子服务 RPC 收集数据库元数据, 构建分析/总结/邮件提示词,
 *        与模型进行两阶段交互(分析生成 SQL, 总结生成可视化数据), 并将模型响应实时流式返回;
 *        类内不保存请求级状态, 支持多请求并发调用
 */
class AIMessageHandler
{
public:
    /**
     * @brief 构造函数, 注入业务依赖对象并加载提示词模板文件,
     *        模板文件缺失或读取失败时抛出异常
     * @param chat_session_manager 聊天会话管理对象, 用于会话归属校验与会话元数据更新
     * @param channel_manager RPC 信道管理对象, 用于调用文件/数据库/用户/通知子服务
     * @param ai_chat_sdk ChatSDK 实例指针, 用于与模型交互及获取历史消息
     * @param prompt_template_dir 提示词模板文件目录, 相对于可执行文件工作目录
     */
    AIMessageHandler(std::shared_ptr<ChatSessionManager> chat_session_manager,
                     cpp_toolkit::ChannelManager::Ptr channel_manager,
                     std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk,
                     std::string prompt_template_dir = "prompt_template");

    ~AIMessageHandler() = default;

    /**
     * @brief 发送消息给模型的总入口, 根据聊天类型分发到对应场景的处理流程,
     *        处理过程中的模型响应与最终结果通过流式回调实时发送给前端
     * @param context 发送消息上下文
     * @param stream_callback 流式输出回调
     */
    void SendMessage(const SendMessageContext& context, const StreamCallback& stream_callback);

    /**
     * @brief 发送普通消息, 将用户消息通过 ChatSDK 流式发送给模型,
     *        模型响应通过流式回调实时返回, 无提示词构建与 SQL 执行流程
     * @param context 发送消息上下文
     * @param stream_callback 流式输出回调
     */
    void SendPlainMessage(const SendMessageContext& context, const StreamCallback& stream_callback);

    /**
     * @brief 检测模型回复是否包含发送邮件指令, 即是否包含
     *        "<EMAIL_START>sendEmail<EMAIL_END>" 邮件发送工具调用标记
     * @param model_response 模型回复内容
     * @return 包含发送邮件指令返回 true, 否则返回 false
     */
    bool IsEmailToolCall(const std::string& model_response);

    /**
     * @brief 发送邮箱, 从会话历史消息中提取上一轮的分析与总结内容构建邮件参数,
     *        通过用户子服务获取用户邮箱, 由模型生成邮件主题与正文后,
     *        通过通知子服务发送邮件, 成功后通过流式回调通知前端
     * @param context 发送消息上下文
     * @param stream_callback 流式输出回调
     */
    void SendEmail(const SendMessageContext& context, const StreamCallback& stream_callback);

    /**
     * @brief 从模型回复中提取 SQL 语句, SQL 语句包含在
     *        "<SQL_START>" 与 "<SQL_END>" 标签之间, 去除首尾空白后返回
     * @param model_response 模型回复内容
     * @return 提取出的 SQL 语句, 标签不存在或内容为空时返回空字符串
     */
    std::string ExtractSql(const std::string& model_response);

private:
    /**
     * @brief 处理 Excel 与数据库聊天场景的公共流程: 收集数据库元数据, 构建分析提示词,
     *        与模型进行两阶段交互并执行 SQL, 组织可视化 JSON 结果返回前端
     * @param context 发送消息上下文
     * @param stream_callback 流式输出回调
     * @param session_info 聊天会话元数据, 会话归属已校验
     */
    void HandleAnalysisChat(const SendMessageContext& context, const StreamCallback& stream_callback,
                            const ChatSessionInfo& session_info);

    /**
     * @brief 获取待分析的数据库表名列表, excel 场景通过文件子服务按文件 ID 获取,
     *        database 场景直接使用上下文中解析好的表名列表
     * @param context 发送消息上下文
     * @param session_info 聊天会话元数据
     * @return 数据库表名列表, 获取失败时抛出异常
     */
    std::vector<std::string> GetTableNames(const SendMessageContext& context,
                                           const ChatSessionInfo& session_info);

    /**
     * @brief 收集数据库表元数据, 逐表调用数据库子服务获取表结构与采样数据,
     *        并按分析提示词要求的格式组装表结构信息, 表名列表与采样数据文本
     * @param context 发送消息上下文
     * @param table_names 数据库表名列表
     * @param table_schema 组装后的表结构信息文本
     * @param table_name_text 组装后的表名列表文本
     * @param data_example 组装后的采样数据文本
     */
    void CollectTableMetadata(const SendMessageContext& context,
                              const std::vector<std::string>& table_names,
                              std::string& table_schema, std::string& table_name_text,
                              std::string& data_example);

    /**
     * @brief 构建分析提示词, 使用分析提示词模板填充数据库类型, 表结构, 表名,
     *        采样数据与用户问题占位符, 生成最终发送给模型的分析提示词
     * @param context 发送消息上下文
     * @param table_schema 表结构信息文本
     * @param table_name_text 表名列表文本
     * @param data_example 采样数据文本
     * @return 填充完成的分析提示词
     */
    std::string BuildAnalyzePrompt(const SendMessageContext& context, const std::string& table_schema,
                                   const std::string& table_name_text,
                                   const std::string& data_example);

    /**
     * @brief 构建总结提示词, 使用总结提示词模板填充用户问题与 SQL 执行结果占位符
     * @param context 发送消息上下文
     * @param result_json SQL 执行结果 JSON 字符串
     * @return 填充完成的总结提示词
     */
    std::string BuildSummaryPrompt(const SendMessageContext& context, const std::string& result_json);

    /**
     * @brief 执行 SQL 语句, 通过数据库子服务执行模型生成的 SQL 并返回执行结果,
     *        excel 场景使用统一的 excel_connection 连接, database 场景使用请求中的连接 ID
     * @param context 发送消息上下文
     * @param sql 待执行的 SQL 语句
     * @param columns SQL 执行结果的列名列表
     * @param rows SQL 执行结果的行数据列表, 每行为单元格字符串列表
     * @return SQL 执行结果 JSON 字符串, 包含列名与行数据
     */
    std::string ExecuteSql(const SendMessageContext& context, const std::string& sql,
                           std::vector<std::string>& columns,
                           std::vector<std::vector<std::string>>& rows);

    /**
     * @brief 构建 SQL 执行结果 JSON, 根据总结提示词的要求将列名与行数据
     *        组织为 {"columns": [...], "rows": [[...], ...]} 格式的 JSON 字符串
     * @param columns 列名列表
     * @param rows 行数据列表, 每行为单元格字符串列表
     * @return SQL 执行结果 JSON 字符串
     */
    std::string BuildSqlResultJson(const std::vector<std::string>& columns,
                                   const std::vector<std::vector<std::string>>& rows);

    /**
     * @brief 构建最终响应 JSON, 将总结内容, 可视化显示类型与 SQL 执行结果组装为
     *        {"summary", "displayType", "data": {"columns", "rows", "tables"}} 结构返回前端
     * @param summary 总结阶段生成的总结内容
     * @param chart_type 总结阶段推荐的图表类型
     * @param columns SQL 执行结果的列名列表
     * @param rows SQL 执行结果的行数据列表
     * @return 最终响应 JSON 字符串
     */
    std::string BuildFinalResponseJson(const std::string& summary, const std::string& chart_type,
                                       const std::vector<std::string>& columns,
                                       const std::vector<std::vector<std::string>>& rows);

    /**
     * @brief 更新会话元数据, 消息总数加一, 会话第一条消息时以用户消息前 20 个字符
     *        更新标题, 并刷新最近一次消息时间
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param session_info 会话元数据(按值传递, 内部修改后写回数据库并删除缓存)
     * @param user_message 用户消息内容, 用于生成会话标题
     */
    void UpdateSessionMetadata(const std::string& request_id, ChatSessionInfo session_info,
                               const std::string& user_message);

    /**
     * @brief 获取指定用户邮箱, 通过用户子服务查询用户信息并返回邮箱
     * @param context 发送消息上下文
     * @return 用户邮箱
     */
    std::string GetUserEmail(const SendMessageContext& context);

    /**
     * @brief 从 ChatSDK 会话历史消息中提取上一轮对话的邮件参数, 包含上一轮用户提问,
     *        分析消息中的标题与分析内容, 总结消息中的总结内容;
     *        历史消息中缺少分析消息或总结消息时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param chat_session_id AI 聊天会话 ID
     * @return 邮箱参数 JSON 字符串, 格式为 {"question", "analysis", "summary"}
     */
    std::string BuildEmailParamJson(const std::string& request_id,
                                    const std::string& chat_session_id);

    /**
     * @brief 构建发送邮件提示词, 使用邮件提示词模板填充邮箱参数与用户消息占位符
     * @param email_param_json 邮箱参数 JSON 字符串
     * @param user_input 本次用户消息内容
     * @return 填充完成的发送邮件提示词
     */
    std::string BuildEmailPrompt(const std::string& email_param_json, const std::string& user_input);

    /**
     * @brief 发送邮件, 通过通知子服务将指定主题与内容的邮件发送到指定邮箱
     * @param context 发送消息上下文
     * @param to_email 收件人邮箱
     * @param subject 邮件主题
     * @param content 邮件正文(HTML 格式)
     */
    void SendEmailByNotifyService(const SendMessageContext& context, const std::string& to_email,
                                  const std::string& subject, const std::string& content);

    /**
     * @brief 读取提示词模板文件内容, 文件不存在或读取失败时抛出异常
     * @param file_name 提示词模板文件名(不含目录)
     * @return 提示词模板文件内容
     */
    std::string ReadPromptTemplateFile(const std::string& file_name);

    /**
     * @brief 截断 UTF-8 编码字符串到指定最大字符数, 保证不会截断多字节字符
     * @param text 原始字符串
     * @param max_chars 最大字符数
     * @return 截断后的字符串
     */
    static std::string TruncateUtf8(const std::string& text, size_t max_chars);

    // 聊天会话管理对象, 用于会话归属校验与会话元数据更新
    std::shared_ptr<ChatSessionManager> chat_session_manager_;

    // RPC 信道管理对象, 用于调用文件/数据库/用户/通知子服务
    cpp_toolkit::ChannelManager::Ptr channel_manager_;

    // ChatSDK 实例指针, 用于与模型交互及获取历史消息
    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk_;

    // 提示词模板文件目录, 相对于可执行文件工作目录
    std::string prompt_template_dir_;

    // 分析提示词模板内容
    std::string analyze_prompt_template_;

    // 总结提示词模板内容
    std::string summary_prompt_template_;

    // 发送邮件提示词模板内容
    std::string email_prompt_template_;
};

} // namespace ai_service
} // namespace chat_excel
