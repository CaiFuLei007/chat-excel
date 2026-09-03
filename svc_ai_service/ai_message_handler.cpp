#include "svc_ai_service/ai_message_handler.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <brpc/controller.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include <ai_service.pb.h>
#include <database_service.pb.h>
#include <file_service.pb.h>
#include <notify_service.pb.h>
#include <user_service.pb.h>
#include "common/exception.h"
#include "svc_ai_service/prompt_template.h"

namespace chat_excel
{
namespace ai_service
{

// proto 生成代码命名空间别名, 简化 RPC 调用代码
namespace proto = ::chat_excel_proto::ai_service;
namespace file_proto = ::chat_excel_proto::file_service;
namespace db_proto = ::chat_excel_proto::database_service;
namespace user_proto = ::chat_excel_proto::user_service;
namespace notify_proto = ::chat_excel_proto::notify_service;

namespace
{

// 子服务名称, 用于从信道管理对象获取通信信道
constexpr const char* kFileServiceName = "FileService";
constexpr const char* kDatabaseServiceName = "DataBaseService";
constexpr const char* kUserServiceName = "UserService";
constexpr const char* kNotifyServiceName = "NotifyService";

// 轻量 RPC 调用超时时间(毫秒) : 元数据收集/SQL 执行/邮箱查询均为轻量操作, 设置 3 秒
constexpr int kLightRpcTimeoutMs = 3 * 1000;

// 邮件发送 RPC 调用超时时间(毫秒) : 邮件发送涉及 SMTP 交互, 耗时较长, 设置 10 秒
constexpr int kEmailRpcTimeoutMs = 10 * 1000;

// Excel 数据库全局连接 ID, 与数据库子服务中登记的全局连接一致
constexpr const char* kExcelConnectionId = "excel_connection";

// 模型回复中的 SQL 标签
constexpr const char* kSqlStartTag = "<SQL_START>";
constexpr const char* kSqlEndTag = "<SQL_END>";

// 模型回复中的邮件发送工具调用标签
constexpr const char* kEmailStartTag = "<EMAIL_START>";
constexpr const char* kEmailEndTag = "<EMAIL_END>";
constexpr const char* kEmailToolCommand = "sendEmail";

// 模型回复中的标题, 任务列表与分析内容标签, 邮件参数构建时使用
constexpr const char* kTitleStartTag = "<TITLE_START>";
constexpr const char* kTitleEndTag = "<TITLE_END>";
constexpr const char* kTasksStartTag = "<TASKS_START>";
constexpr const char* kTasksEndTag = "<TASKS_END>";
constexpr const char* kAnalysisStartTag = "<ANALYSIS_START>";
constexpr const char* kAnalysisEndTag = "<ANALYSIS_END>";

// 邮件发送成功通知消息
constexpr const char* kEmailSuccessMessage = "邮箱发送成功 , 请注意查收";

// 提示词模板文件名
constexpr const char* kAnalyzePromptFileName = "analyze_prompt.md";
constexpr const char* kSummaryPromptFileName = "summary_prompt.md";
constexpr const char* kEmailPromptFileName = "email_content_prompt.md";

// 各提示词模板的占位符列表(占位符名称不包含花括号), 与模板文件中的占位符一一对应,
// 由外部传入 PromptTemplate, PromptTemplate 不再从模板内容中解析占位符
const std::vector<std::string> kAnalyzePromptPlaceholders = {
    "DataBase", "table_schema", "table_name", "data_example", "user_input"};
const std::vector<std::string> kSummaryPromptPlaceholders = {"user_input", "result_json"};
const std::vector<std::string> kEmailPromptPlaceholders = {"email_param", "user_input"};

// 数据表采样数据条数
constexpr size_t kSampleDataLimit = 5;

// 会话标题最大字符数
constexpr size_t kSessionTitleMaxChars = 20;

// 需要过滤后才能透传给前端的开标签与闭标签(SQL 标签与邮件指令标签, 区间内容一并过滤)
const std::vector<std::string> kFilterOpenTags = {kSqlStartTag, kEmailStartTag};
const std::vector<std::string> kFilterCloseTags = {kSqlEndTag, kEmailEndTag};

// 仅剔除标签本身但保留区间内容的标签(标题, 任务列表与分析标签, 区间内容正常透传给前端)
const std::vector<std::string> kMarkerTags = {kTitleStartTag, kTitleEndTag,
                                              kTasksStartTag, kTasksEndTag,
                                              kAnalysisStartTag, kAnalysisEndTag};

/**
 * @brief 标签流式过滤器, 基于跨数据块状态机过滤模型响应中的标签内容,
 *        SQL 标签与邮件指令标签的区间内容整体过滤, 标题/任务列表/分析标签仅剔除标签本身;
 *        标签可能被模型响应的分块边界切断, 暂存可能是标签前缀的尾部内容直至可以判定
 */
class TagStreamFilter
{
public:
    /**
     * @brief 输入一个模型响应数据块, 返回过滤后可以透传的内容
     * @param chunk 模型响应数据块
     * @return 过滤后可以透传的内容
     */
    std::string Feed(const std::string& chunk)
    {
        pending_.append(chunk);
        std::string output;
        while (true)
        {
            if (in_tag_)
            {
                const size_t pos = FindEarliestTag(pending_, kFilterCloseTags);
                if (pos == std::string::npos)
                {
                    // 标签区间内容不透传, 仅暂存可能是闭标签前缀的尾部内容
                    const size_t keep = LongestTagPrefixSuffix(pending_, kFilterCloseTags);
                    pending_.erase(0, pending_.size() - keep);
                    break;
                }
                pending_.erase(0, pos + MatchedTagSize(pending_, pos, kFilterCloseTags));
                in_tag_ = false;
                continue;
            }

            // 未处于过滤区间时, 同时查找需整体过滤的开标签与仅剔除标签本身的标签
            const size_t filter_pos = FindEarliestTag(pending_, kFilterOpenTags);
            const size_t marker_pos = FindEarliestTag(pending_, kMarkerTags);
            if (filter_pos == std::string::npos && marker_pos == std::string::npos)
            {
                // 未找到任何标签, 透传除标签前缀外的内容, 标签前缀暂存等待后续数据块
                const size_t filter_keep = LongestTagPrefixSuffix(pending_, kFilterOpenTags);
                const size_t marker_keep = LongestTagPrefixSuffix(pending_, kMarkerTags);
                const size_t keep = std::max(filter_keep, marker_keep);
                output.append(pending_, 0, pending_.size() - keep);
                pending_.erase(0, pending_.size() - keep);
                break;
            }
            if (filter_pos <= marker_pos)
            {
                // 进入过滤区间, 区间内容(含其间的标记标签)一并丢弃
                output.append(pending_, 0, filter_pos);
                pending_.erase(0, filter_pos + MatchedTagSize(pending_, filter_pos, kFilterOpenTags));
                in_tag_ = true;
            }
            else
            {
                // 标题/分析标签仅剔除标签本身, 区间内容照常透传
                output.append(pending_, 0, marker_pos);
                pending_.erase(0, marker_pos + MatchedTagSize(pending_, marker_pos, kMarkerTags));
            }
        }
        return output;
    }

    /**
     * @brief 模型响应结束时调用, 输出暂存区中剩余的可透传内容
     * @return 暂存区中剩余的可透传内容
     */
    std::string Finish()
    {
        // 流结束时未闭合的标签区间内容全部丢弃
        std::string output;
        if (!in_tag_)
        {
            output = std::move(pending_);
        }
        pending_.clear();
        in_tag_ = false;
        return output;
    }

private:
    /**
     * @brief 查找多个标签在文本中最早出现的位置
     * @param text 待查找文本
     * @param tags 标签列表
     * @return 最早出现的位置, 均未出现时返回 std::string::npos
     */
    static size_t FindEarliestTag(const std::string& text, const std::vector<std::string>& tags)
    {
        size_t earliest = std::string::npos;
        for (const std::string& tag : tags)
        {
            const size_t pos = text.find(tag);
            if (pos < earliest)
            {
                earliest = pos;
            }
        }
        return earliest;
    }

    /**
     * @brief 获取文本指定位置匹配的标签长度
     * @param text 待匹配文本
     * @param pos 匹配位置
     * @param tags 标签列表
     * @return 匹配的标签长度, 无匹配时返回 0
     */
    static size_t MatchedTagSize(const std::string& text, size_t pos, const std::vector<std::string>& tags)
    {
        for (const std::string& tag : tags)
        {
            if (text.compare(pos, tag.size(), tag) == 0)
            {
                return tag.size();
            }
        }
        return 0;
    }

    /**
     * @brief 计算文本尾部与任一标签前缀匹配的最长长度, 该部分内容可能是被切断的标签,
     *        需要暂存等待后续数据块判定
     * @param text 待检查文本
     * @param tags 标签列表
     * @return 尾部与标签前缀匹配的最长长度, 无匹配时返回 0
     */
    static size_t LongestTagPrefixSuffix(const std::string& text, const std::vector<std::string>& tags)
    {
        size_t max_tag_size = 0;
        for (const std::string& tag : tags)
        {
            max_tag_size = std::max(max_tag_size, tag.size());
        }
        const size_t max_len = std::min(max_tag_size, text.size());
        for (size_t len = max_len; len >= 1; --len)
        {
            const std::string suffix = text.substr(text.size() - len);
            for (const std::string& tag : tags)
            {
                if (tag.size() >= len && tag.compare(0, len, suffix) == 0)
                {
                    return len;
                }
            }
        }
        return 0;
    }

    // 暂存区, 保存可能是标签前缀的尾部内容或标签区间内的内容
    std::string pending_;

    // 是否正处于标签区间内部
    bool in_tag_ = false;
};

/**
 * @brief 去除字符串首尾的空白字符
 * @param text 原始字符串
 * @return 去除首尾空白后的字符串
 */
std::string Trim(const std::string& text)
{
    constexpr const char* kWhitespace = " \t\n\r";
    const size_t begin = text.find_first_not_of(kWhitespace);
    if (begin == std::string::npos)
    {
        return "";
    }
    const size_t end = text.find_last_not_of(kWhitespace);
    return text.substr(begin, end - begin + 1);
}

/**
 * @brief 提取标签区间内容, 即开始标签与结束标签之间的文本(去除首尾空白)
 * @param text 原始文本
 * @param begin_tag 开始标签
 * @param end_tag 结束标签
 * @return 标签区间内容, 标签不存在时返回空字符串
 */
std::string ExtractTaggedContent(const std::string& text, const std::string& begin_tag,
                                 const std::string& end_tag)
{
    size_t begin = text.find(begin_tag);
    if (begin == std::string::npos)
    {
        return "";
    }
    begin += begin_tag.size();
    const size_t end = text.find(end_tag, begin);
    if (end == std::string::npos)
    {
        return "";
    }
    return Trim(text.substr(begin, end - begin));
}

/**
 * @brief 解析模型回复中的 JSON 内容, 提取首个 { 到最后一个 } 之间的文本进行解析,
 *        兼容模型在 JSON 前后输出的多余文本, 解析失败时抛出指定错误码的异常
 * @param model_response 模型回复内容
 * @param error_code 解析失败时抛出的错误码
 * @return 解析后的 JSON 对象
 */
Json::Value ParseModelJson(const std::string& model_response, ErrorCode error_code)
{
    const size_t begin = model_response.find('{');
    const size_t end = model_response.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end <= begin)
    {
        ERR("模型回复中不包含合法的 JSON 内容, 内容片段: {}", model_response.substr(0, 200));
        throw ChatExcelException(error_code);
    }
    const std::string_view json_text(model_response.data() + begin, end - begin + 1);
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_text) || !json.isObject())
    {
        ERR("模型回复 JSON 解析失败, 内容片段: {}", model_response.substr(0, 200));
        throw ChatExcelException(error_code);
    }
    return json;
}

/**
 * @brief 获取数据库类型的展示名称, 用于填充分析提示词中的 {DataBase} 占位符
 * @param db_type 数据库类型, 取值与 proto DataType 一致: EXCEL = 0, MYSQL = 1, SQLITE = 2
 * @return 数据库类型展示名称
 */
std::string GetDatabaseTypeName(int db_type)
{
    if (db_type == static_cast<int>(proto::DataType::MYSQL))
    {
        return "MySQL";
    }
    else if (db_type == static_cast<int>(proto::DataType::SQLITE))
    {
        return "SQLite";
    }
    // EXCEL 类型连接的 excel_connection 实际为 MySQL 数据库
    return "MySQL";
}

/**
 * @brief 获取执行 SQL 使用的数据库连接 ID, Excel 场景使用统一的 excel_connection 连接,
 *        database 场景使用请求中携带的连接 ID
 * @param context 发送消息上下文
 * @return 数据库连接 ID
 */
std::string ResolveConnectionId(const SendMessageContext& context)
{
    if (context.chat_type == "excel")
    {
        return kExcelConnectionId;
    }
    return context.db_connect_id;
}

} // namespace

AIMessageHandler::AIMessageHandler(std::shared_ptr<ChatSessionManager> chat_session_manager,
                                   cpp_toolkit::ChannelManager::Ptr channel_manager,
                                   std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk,
                                   std::string prompt_template_dir)
    : chat_session_manager_(std::move(chat_session_manager)),
      channel_manager_(std::move(channel_manager)),
      ai_chat_sdk_(std::move(ai_chat_sdk)),
      prompt_template_dir_(std::move(prompt_template_dir))
{
    // 构造时加载提示词模板文件, 模板缺失属于启动期致命错误, 直接抛出异常
    analyze_prompt_template_ = ReadPromptTemplateFile(kAnalyzePromptFileName);
    summary_prompt_template_ = ReadPromptTemplateFile(kSummaryPromptFileName);
    email_prompt_template_ = ReadPromptTemplateFile(kEmailPromptFileName);
    INFO("提示词模板加载完成, 分析模板大小: {}, 总结模板大小: {}, 邮件模板大小: {}",
         analyze_prompt_template_.size(), summary_prompt_template_.size(), email_prompt_template_.size());
}

void AIMessageHandler::SendMessage(const SendMessageContext& context, const StreamCallback& stream_callback)
{
    INFO("开始处理发送消息请求, request_id: {}, chat_session_id: {}, chat_type: {}",
         context.request_id, context.chat_session_id, context.chat_type);

    // 获取会话元数据并校验会话归属, 会话不存在或不属于当前用户时抛出异常
    ChatSessionInfo session_info = chat_session_manager_->GetChatSessionBySessionId(context.chat_session_id);
    if (session_info.user_id != context.user_id)
    {
        ERR("聊天会话不属于当前用户, request_id: {}, user_id: {}, chat_session_id: {}",
            context.request_id, context.user_id, context.chat_session_id);
        throw ChatExcelException(ErrorCode::CHAT_SESSION_USER_MISMATCH);
    }

    // 根据聊天类型分发到对应场景的处理流程
    if (context.chat_type == "plain")
    {
        SendPlainMessage(context, stream_callback);
    }
    else if (context.chat_type == "excel" || context.chat_type == "database")
    {
        HandleAnalysisChat(context, stream_callback, session_info);
    }
    else
    {
        ERR("聊天类型无效: {}, request_id: {}", context.chat_type, context.request_id);
        throw ChatExcelException(ErrorCode::AI_SERVICE_CHAT_TYPE_INVALID);
    }
}

void AIMessageHandler::SendPlainMessage(const SendMessageContext& context,
                                        const StreamCallback& stream_callback)
{
    INFO("发送普通消息, request_id: {}, chat_session_id: {}",
         context.request_id, context.chat_session_id);

    // 流式发送用户消息, 模型响应逐块透传给前端
    std::string full_response;
    bool finished = false;
    ai_chat_sdk_->SendMessageStream(context.chat_session_id, context.message,
                                    [&](const std::string& delta, bool finish)
                                    {
                                        if (!delta.empty())
                                        {
                                            full_response.append(delta);
                                            stream_callback(delta, false);
                                        }
                                        if (finish)
                                        {
                                            finished = true;
                                        }
                                    });

    // ChatSDK 会话不存在或发送失败时不会产生结束回调
    if (!finished)
    {
        ERR("ChatSDK 发送消息失败或会话不存在, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_SESSION_NOT_FOUND);
    }

    // 发送结束标记
    stream_callback("", true);
    INFO("普通消息发送完成, request_id: {}, 响应长度: {}", context.request_id, full_response.size());

    // 更新会话元数据(消息总数, 标题, 最近一次消息时间), plain 场景无分析标题, 使用用户消息生成标题
    ChatSessionInfo session_info = chat_session_manager_->GetChatSessionBySessionId(context.chat_session_id);
    UpdateSessionMetadata(context.request_id, session_info, context.message, "");
}

bool AIMessageHandler::IsEmailToolCall(const std::string& model_response)
{
    // 邮件发送工具调用标记 : <EMAIL_START>sendEmail<EMAIL_END>
    return model_response.find(kEmailStartTag) != std::string::npos
           && model_response.find(kEmailToolCommand) != std::string::npos
           && model_response.find(kEmailEndTag) != std::string::npos;
}

void AIMessageHandler::SendEmail(const SendMessageContext& context, const StreamCallback& stream_callback)
{
    INFO("开始发送邮件流程, request_id: {}, chat_session_id: {}",
         context.request_id, context.chat_session_id);

    // 1. 通过用户子服务获取用户邮箱
    const std::string to_email = GetUserEmail(context);

    // 2. 从会话历史消息中提取邮件参数(上一轮的用户提问, 分析与总结内容)
    const std::string email_param_json =
        BuildEmailParamJson(context.request_id, context.chat_session_id);

    // 3. 构建发送邮件提示词
    const std::string email_prompt = BuildEmailPrompt(email_param_json, context.message);

    // 4. 发送给模型生成邮件内容, 解析邮件主题与正文
    const std::string model_response = ai_chat_sdk_->SendMessage(context.chat_session_id, email_prompt);
    if (model_response.empty())
    {
        ERR("模型邮件内容响应为空, request_id: {}", context.request_id);
        throw ChatExcelException(ErrorCode::AI_EMAIL_CONTENT_PARSE_ERROR);
    }
    const Json::Value email_json = ParseModelJson(model_response, ErrorCode::AI_EMAIL_CONTENT_PARSE_ERROR);
    // 获取 JSON 字段前先判断字段是否存在, 字段缺失或类型不符时按解析失败处理
    if (!email_json.isMember("subject") || !email_json.isMember("content")
        || !email_json["subject"].isString() || !email_json["content"].isString())
    {
        ERR("模型邮件内容响应缺少 subject 或 content 字段, request_id: {}", context.request_id);
        throw ChatExcelException(ErrorCode::AI_EMAIL_CONTENT_PARSE_ERROR);
    }
    const std::string subject = email_json["subject"].asString();
    const std::string content = email_json["content"].asString();
    if (subject.empty() || content.empty())
    {
        ERR("邮件主题或正文为空, request_id: {}, subject 长度: {}, content 长度: {}",
            context.request_id, subject.size(), content.size());
        throw ChatExcelException(ErrorCode::AI_EMAIL_CONTENT_PARSE_ERROR);
    }

    // 5. 通过通知子服务发送邮件
    SendEmailByNotifyService(context, to_email, subject, content);

    // 6. 通知前端邮件发送成功
    stream_callback(kEmailSuccessMessage, true);
    INFO("邮件发送流程完成, request_id: {}, 收件邮箱: {}", context.request_id, to_email);
}

std::string AIMessageHandler::ExtractSql(const std::string& model_response)
{
    return ExtractTaggedContent(model_response, kSqlStartTag, kSqlEndTag);
}

void AIMessageHandler::HandleAnalysisChat(const SendMessageContext& context,
                                          const StreamCallback& stream_callback,
                                          const ChatSessionInfo& session_info)
{
    // 1. 获取待分析的数据库表名列表
    const std::vector<std::string> table_names = GetTableNames(context, session_info);
    if (table_names.empty())
    {
        ERR("数据库表名列表为空, request_id: {}, chat_type: {}",
            context.request_id, context.chat_type);
        throw ChatExcelException(ErrorCode::AI_FILE_RPC_ERROR);
    }

    // 2. 收集数据库表元数据(表结构 + 采样数据)
    std::string table_schema;
    std::string table_name_text;
    std::string data_example;
    CollectTableMetadata(context, table_names, table_schema, table_name_text, data_example);
    INFO("数据库元数据收集完成, request_id: {}, 表数量: {}", context.request_id, table_names.size());

    // 3. 将用户原话写入会话历史并记录消息快照, ChatSDK 自动落库的只有提示词消息,
    //    用户原话需要显式插入, 对话完成后仅保留用户原话与模型最终返回
    if (!ai_chat_sdk_->CreateMessage(context.chat_session_id, "user", context.message))
    {
        ERR("用户原话写入 ChatSDK 失败, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
    }
    const std::vector<std::string> keep_message_ids =
        SnapshotMessageIds(context.request_id, context.chat_session_id);

    // 4. 构建分析提示词并流式发送给模型, 模型响应过滤标签区间内容后实时透传给前端
    const std::string analyze_prompt =
        BuildAnalyzePrompt(context, table_schema, table_name_text, data_example);
    std::string analysis_response;
    TagStreamFilter stream_filter;
    ai_chat_sdk_->SendMessageStream(context.chat_session_id, analyze_prompt,
                                    [&](const std::string& delta, bool finish)
                                    {
                                        analysis_response.append(delta);
                                        const std::string filtered = stream_filter.Feed(delta);
                                        if (!filtered.empty())
                                        {
                                            stream_callback(filtered, false);
                                        }
                                        if (finish)
                                        {
                                            const std::string tail = stream_filter.Finish();
                                            if (!tail.empty())
                                            {
                                                stream_callback(tail, false);
                                            }
                                        }
                                    });
    if (analysis_response.empty())
    {
        ERR("模型分析阶段响应为空, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
        throw ChatExcelException(ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
    INFO("分析阶段完成, request_id: {}, 响应长度: {}", context.request_id, analysis_response.size());
    // 打印 AI 分析阶段完整输出, 便于排查 SQL 提取与危险操作校验问题
    INFO("AI 分析阶段输出, request_id: {}, 响应内容: {}", context.request_id, analysis_response);

    // 提取模型分析回复中生成的会话标题, 会话首条消息时作为标题使用
    const std::string model_title = ExtractTaggedContent(analysis_response, kTitleStartTag, kTitleEndTag);

    // 5. 检测模型是否回复了发送邮件工具调用, 命中则执行发邮件流程并结束, 不执行 SQL
    if (IsEmailToolCall(analysis_response))
    {
        INFO("检测到发送邮件工具调用, request_id: {}", context.request_id);
        SendEmail(context, stream_callback);
        // 邮件轮次同样清理中间消息, 仅保留用户原话与模型的邮件内容响应
        CleanupRoundIntermediateMessages(context.request_id, context.chat_session_id,
                                         keep_message_ids);
        UpdateSessionMetadata(context.request_id, session_info, context.message, model_title);
        return;
    }

    // 6. 从模型回复中提取 SQL 语句
    const std::string sql = ExtractSql(analysis_response);
    if (sql.empty())
    {
        ERR("从模型回复中提取 SQL 失败, request_id: {}, 响应片段: {}",
            context.request_id, analysis_response.substr(0, 200));
        throw ChatExcelException(ErrorCode::AI_EXTRACT_SQL_ERROR);
    }
    INFO("SQL 提取成功, request_id: {}, SQL 长度: {} , SQL 语句: {}", context.request_id, sql.size(), sql);

    // 7. 通过数据库子服务执行 SQL 语句
    std::vector<std::string> columns;
    std::vector<std::string> column_types;
    std::vector<std::vector<std::string>> rows;
    const std::string result_json = ExecuteSql(context, sql, columns, column_types, rows);

    // 8. 构建总结提示词并发送给模型, 生成总结内容
    const std::string summary_prompt = BuildSummaryPrompt(context, result_json);
    const std::string summary_response = ai_chat_sdk_->SendMessage(context.chat_session_id, summary_prompt);
    if (summary_response.empty())
    {
        ERR("模型总结阶段响应为空, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
        throw ChatExcelException(ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
    // 打印 AI 总结阶段完整输出, 便于排查 JSON 解析问题
    INFO("AI 总结阶段输出, request_id: {}, 响应内容: {}", context.request_id, summary_response);

    // 解析总结内容 JSON, 提取总结文本与可视化图表类型, 其余字段丢弃;
    // 获取 JSON 字段前先判断字段是否存在, 字段缺失时按解析失败处理
    const Json::Value summary_json =
        ParseModelJson(summary_response, ErrorCode::AI_SUMMARY_CONTENT_PARSE_ERROR);
    if (!summary_json.isMember("summary") || !summary_json.isMember("chartType")
        || !summary_json["summary"].isString() || !summary_json["chartType"].isString())
    {
        ERR("模型总结阶段响应缺少 summary 或 chartType 字段, request_id: {}", context.request_id);
        throw ChatExcelException(ErrorCode::AI_SUMMARY_CONTENT_PARSE_ERROR);
    }
    const std::string summary = summary_json["summary"].asString();
    const std::string chart_type = summary_json["chartType"].asString();
    INFO("总结阶段完成, request_id: {}, 图表类型: {}", context.request_id, chart_type);

    // 9. 组装最终响应 JSON, 并将最终 JSON 作为一条 assistant 消息追加到 ChatSDK,
    //    保证通过聊天会话 ID 获取历史消息时前端也能正常展示可视化图表
    //    (模型总结阶段存储的 assistant 消息不含 SQL 执行结果, 无法支撑图表展示)
    const std::string final_response = BuildFinalResponseJson(summary, chart_type, columns, column_types, rows);
    if (!ai_chat_sdk_->CreateMessage(context.chat_session_id, "assistant", final_response))
    {
        ERR("最终 JSON 写入 ChatSDK 失败, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
    }

    // 10. 清理本轮对话的中间消息, 仅保留用户原话与最终 JSON
    CleanupRoundIntermediateMessages(context.request_id, context.chat_session_id, keep_message_ids);

    // 11. 将最终响应 JSON 一次性发送给前端
    stream_callback(final_response, true);

    // 12. 更新会话元数据(消息总数, 标题, 最近一次消息时间)
    UpdateSessionMetadata(context.request_id, session_info, context.message, model_title);
}

std::vector<std::string> AIMessageHandler::GetTableNames(const SendMessageContext& context,
                                                         const ChatSessionInfo& session_info)
{
    // 数据库场景直接使用上下文中解析好的表名列表
    if (context.chat_type == "database")
    {
        return context.table_names;
    }

    // Excel 场景通过文件子服务按文件 ID 获取 WorkSheet 对应的数据库表名列表,
    // 请求中未携带文件 ID 时使用会话元数据中关联的文件 ID
    std::string file_id = context.file_id;
    if (file_id.empty())
    {
        file_id = session_info.file_id;
    }
    if (file_id.empty())
    {
        ERR("Excel 场景缺少文件 ID, request_id: {}, chat_session_id: {}",
            context.request_id, context.chat_session_id);
        throw ChatExcelException(ErrorCode::AI_SERVICE_FILE_ID_EMPTY);
    }

    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kFileServiceName);
    if (channel == nullptr)
    {
        ERR("获取文件子服务信道失败, request_id: {}, 服务名称: {}",
            context.request_id, kFileServiceName);
        throw ChatExcelException(ErrorCode::AI_FILE_RPC_ERROR);
    }

    file_proto::GetWorksheetDBTablesRequest rpc_request;
    rpc_request.set_request_id(context.request_id);
    rpc_request.set_session_id(context.session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(context.user_id);

    file_proto::FileService_Stub file_service_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kLightRpcTimeoutMs);
    file_proto::GetWorksheetDBTablesResponse rpc_response;
    file_service_stub.GetWorksheetDBTables(&controller, &rpc_request, &rpc_response, nullptr);

    if (controller.Failed())
    {
        ERR("文件子服务 RPC 调用失败, request_id: {}, file_id: {}, 错误信息: {}",
            context.request_id, file_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::AI_FILE_RPC_ERROR);
    }
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("获取 WorkSheet 数据库表名列表失败, request_id: {}, 错误码: {}, 错误信息: {}",
            context.request_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }

    return {rpc_response.result().worksheet_db_tables().begin(),
            rpc_response.result().worksheet_db_tables().end()};
}

void AIMessageHandler::CollectTableMetadata(const SendMessageContext& context,
                                            const std::vector<std::string>& table_names,
                                            std::string& table_schema, std::string& table_name_text,
                                            std::string& data_example)
{
    // 逐表调用数据库子服务获取表结构与采样数据
    for (size_t i = 0; i < table_names.size(); ++i)
    {
        const std::string& table_name = table_names[i];

        cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kDatabaseServiceName);
        if (channel == nullptr)
        {
            ERR("获取数据库子服务信道失败, request_id: {}, 服务名称: {}",
                context.request_id, kDatabaseServiceName);
            throw ChatExcelException(ErrorCode::AI_DATABASE_RPC_ERROR);
        }

        // 获取表结构
        db_proto::GetTableStructRequest struct_request;
        struct_request.set_request_id(context.request_id);
        struct_request.set_session_id(context.session_id);
        struct_request.set_db_connect_id(ResolveConnectionId(context));
        struct_request.set_table_name(table_name);

        db_proto::DatabaseService_Stub database_stub(channel.get());
        brpc::Controller struct_controller;
        struct_controller.set_timeout_ms(kLightRpcTimeoutMs);
        db_proto::GetTableStructResponse struct_response;
        database_stub.GetTableStruct(&struct_controller, &struct_request, &struct_response, nullptr);

        if (struct_controller.Failed())
        {
            ERR("获取表结构 RPC 调用失败, request_id: {}, table_name: {}, 错误信息: {}",
                context.request_id, table_name, struct_controller.ErrorText());
            throw ChatExcelException(ErrorCode::AI_DATABASE_RPC_ERROR);
        }
        if (struct_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
        {
            ERR("获取表结构失败, request_id: {}, table_name: {}, 错误码: {}, 错误信息: {}",
                context.request_id, table_name, struct_response.error_code(), struct_response.error_msg());
            throw ChatExcelException(static_cast<ErrorCode>(struct_response.error_code()));
        }

        // 获取采样数据
        db_proto::GetSampleDataRequest sample_request;
        sample_request.set_request_id(context.request_id);
        sample_request.set_session_id(context.session_id);
        sample_request.set_db_connect_id(ResolveConnectionId(context));
        sample_request.set_table_name(table_name);
        sample_request.set_limit(static_cast<int32_t>(kSampleDataLimit));

        brpc::Controller sample_controller;
        sample_controller.set_timeout_ms(kLightRpcTimeoutMs);
        db_proto::GetSampleDataResponse sample_response;
        database_stub.GetSampleData(&sample_controller, &sample_request, &sample_response, nullptr);

        if (sample_controller.Failed())
        {
            ERR("获取采样数据 RPC 调用失败, request_id: {}, table_name: {}, 错误信息: {}",
                context.request_id, table_name, sample_controller.ErrorText());
            throw ChatExcelException(ErrorCode::AI_DATABASE_RPC_ERROR);
        }
        if (sample_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
        {
            ERR("获取采样数据失败, request_id: {}, table_name: {}, 错误码: {}, 错误信息: {}",
                context.request_id, table_name, sample_response.error_code(), sample_response.error_msg());
            throw ChatExcelException(static_cast<ErrorCode>(sample_response.error_code()));
        }

        // 按分析提示词要求的格式组装元数据文本
        table_schema += "表 `" + table_name + "` : " + struct_response.table_struct() + "\n";
        if (!table_name_text.empty())
        {
            table_name_text += ", ";
        }
        table_name_text += "`" + table_name + "`";
        data_example += "表 `" + table_name + "`:\n" + sample_response.sample_data() + "\n";
    }
}

std::string AIMessageHandler::BuildAnalyzePrompt(const SendMessageContext& context,
                                                 const std::string& table_schema,
                                                 const std::string& table_name_text,
                                                 const std::string& data_example)
{
    PromptTemplate prompt_template(analyze_prompt_template_, kAnalyzePromptPlaceholders);
    prompt_template.SetPlaceholder("DataBase", GetDatabaseTypeName(context.db_type));
    prompt_template.SetPlaceholder("table_schema", table_schema);
    prompt_template.SetPlaceholder("table_name", table_name_text);
    prompt_template.SetPlaceholder("data_example", data_example);
    prompt_template.SetPlaceholder("user_input", context.message);
    return prompt_template.Generate();
}

std::string AIMessageHandler::BuildSummaryPrompt(const SendMessageContext& context,
                                                 const std::string& result_json)
{
    PromptTemplate prompt_template(summary_prompt_template_, kSummaryPromptPlaceholders);
    prompt_template.SetPlaceholder("user_input", context.message);
    prompt_template.SetPlaceholder("result_json", result_json);
    return prompt_template.Generate();
}

std::string AIMessageHandler::ExecuteSql(const SendMessageContext& context, const std::string& sql,
                                         std::vector<std::string>& columns,
                                         std::vector<std::string>& column_types,
                                         std::vector<std::vector<std::string>>& rows)
{
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kDatabaseServiceName);
    if (channel == nullptr)
    {
        ERR("获取数据库子服务信道失败, request_id: {}, 服务名称: {}",
            context.request_id, kDatabaseServiceName);
        throw ChatExcelException(ErrorCode::AI_DATABASE_RPC_ERROR);
    }

    db_proto::ExecuteSQLRequest rpc_request;
    rpc_request.set_request_id(context.request_id);
    rpc_request.set_session_id(context.session_id);
    rpc_request.set_db_connect_id(ResolveConnectionId(context));
    rpc_request.set_sql(sql);

    db_proto::DatabaseService_Stub database_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kLightRpcTimeoutMs);
    db_proto::ExecuteSQLResponse rpc_response;
    database_stub.ExecuteSQL(&controller, &rpc_request, &rpc_response, nullptr);

    if (controller.Failed())
    {
        ERR("执行 SQL RPC 调用失败, request_id: {}, 错误信息: {}",
            context.request_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::AI_DATABASE_RPC_ERROR);
    }
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("SQL 执行失败, request_id: {}, 错误码: {}, 错误信息: {}",
            context.request_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }

    columns.assign(rpc_response.columns().begin(), rpc_response.columns().end());
    column_types.assign(rpc_response.column_types().begin(), rpc_response.column_types().end());
    rows.reserve(static_cast<size_t>(rpc_response.rows_size()));
    for (const db_proto::Row& row : rpc_response.rows())
    {
        rows.emplace_back(row.cells().begin(), row.cells().end());
    }

    INFO("SQL 执行完成, request_id: {}, 列数: {}, 行数: {}, 是否查询: {}",
         context.request_id, columns.size(), rows.size(), rpc_response.is_query());
    return BuildSqlResultJson(columns, rows);
}

std::string AIMessageHandler::BuildSqlResultJson(const std::vector<std::string>& columns,
                                                 const std::vector<std::vector<std::string>>& rows)
{
    Json::Value result;
    for (const std::string& column : columns)
    {
        result["columns"].append(column);
    }
    for (const std::vector<std::string>& row : rows)
    {
        Json::Value row_json;
        for (const std::string& cell : row)
        {
            row_json.append(cell);
        }
        result["rows"].append(row_json);
    }

    std::string result_json;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(result, result_json))
    {
        ERR("SQL 执行结果 JSON 序列化失败");
        throw ChatExcelException(ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
    return result_json;
}

std::string AIMessageHandler::BuildFinalResponseJson(const std::string& summary,
                                                     const std::string& chart_type,
                                                     const std::vector<std::string>& columns,
                                                     const std::vector<std::string>& column_types,
                                                     const std::vector<std::vector<std::string>>& rows)
{
    Json::Value data;
    for (const std::string& column : columns)
    {
        data["columns"].append(column);
    }
    // 列类型列表与列名列表一一对应, 供前端更好地展示数据
    for (const std::string& column_type : column_types)
    {
        data["column_types"].append(column_type);
    }
    for (const std::vector<std::string>& row : rows)
    {
        Json::Value row_json;
        for (const std::string& cell : row)
        {
            row_json.append(cell);
        }
        data["rows"].append(row_json);
    }
    // tables 数组完整保留所有表数据, 单条 SQL 执行结果只有一张表
    Json::Value table;
    table["columns"] = data["columns"];
    table["rows"] = data["rows"];
    data["tables"].append(table);

    Json::Value response;
    response["summary"] = summary;
    response["displayType"] = chart_type;
    response["data"] = data;

    std::string response_json;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(response, response_json))
    {
        ERR("最终响应 JSON 序列化失败");
        throw ChatExcelException(ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
    return response_json;
}

void AIMessageHandler::UpdateSessionMetadata(const std::string& request_id, ChatSessionInfo session_info,
                                             const std::string& user_message,
                                             const std::string& model_title)
{
    // 会话第一条消息时更新标题, 优先使用模型分析回复中提取的标题,
    // 提取不到时回退为用户消息前 20 个字符, 保证标题不为空
    if (session_info.total_message_count == 0)
    {
        const std::string& title = model_title.empty() ? user_message : model_title;
        session_info.title = TruncateUtf8(title, kSessionTitleMaxChars);
    }
    session_info.total_message_count += 1;
    session_info.update_time = static_cast<unsigned long long>(time(nullptr));

    // 写策略 Cache-Aside : 先更新 MySQL, 再删除缓存
    chat_session_manager_->SaveOrUpdateChatSession(session_info);
    INFO("会话元数据更新完成, request_id: {}, chat_session_id: {}, 消息总数: {}",
         request_id, session_info.chat_session_id, session_info.total_message_count);
}

std::vector<std::string> AIMessageHandler::SnapshotMessageIds(const std::string& request_id,
                                                              const std::string& chat_session_id)
{
    // 获取会话历史消息, ChatSDK 中会话不存在时返回空指针
    const std::shared_ptr<aichat_sdk::Session> session = ai_chat_sdk_->GetSession(chat_session_id);
    if (session == nullptr)
    {
        ERR("ChatSDK 中聊天会话不存在, request_id: {}, chat_session_id: {}",
            request_id, chat_session_id);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_SESSION_NOT_FOUND);
    }
    std::vector<std::string> message_ids;
    message_ids.reserve(session->messages.size());
    for (const aichat_sdk::Message& message : session->messages)
    {
        message_ids.push_back(message.mid);
    }
    return message_ids;
}

void AIMessageHandler::CleanupRoundIntermediateMessages(const std::string& request_id,
                                                        const std::string& chat_session_id,
                                                        const std::vector<std::string>& keep_message_ids)
{
    const std::shared_ptr<aichat_sdk::Session> session = ai_chat_sdk_->GetSession(chat_session_id);
    if (session == nullptr)
    {
        ERR("ChatSDK 中聊天会话不存在, 跳过中间消息清理, request_id: {}, chat_session_id: {}",
            request_id, chat_session_id);
        return;
    }
    const std::unordered_set<std::string> keep_message_id_set(keep_message_ids.begin(),
                                                              keep_message_ids.end());
    // 拷贝消息列表后再遍历删除, 避免 RemoveMessage 更新会话缓存导致消息列表引用失效
    const std::vector<aichat_sdk::Message> messages = session->messages;
    // 最后一条消息为本轮模型最终返回(可视化最终 JSON 或邮件内容响应), 一并保留
    size_t removed_count = 0;
    for (size_t i = 0; i + 1 < messages.size(); ++i)
    {
        const aichat_sdk::Message& message = messages[i];
        // 快照中的消息(含用户原话)保留, 其余为本轮中间消息, 逐条删除
        if (keep_message_id_set.find(message.mid) != keep_message_id_set.end())
        {
            continue;
        }
        if (!ai_chat_sdk_->RemoveMessage(message.mid))
        {
            ERR("删除本轮中间消息失败, request_id: {}, chat_session_id: {}, mid: {}",
                request_id, chat_session_id, message.mid);
        }
        else
        {
            ++removed_count;
        }
    }
    INFO("本轮中间消息清理完成, request_id: {}, chat_session_id: {}, 删除消息数: {}, 保留消息数: {}",
         request_id, chat_session_id, removed_count, messages.size() - removed_count);
}

std::string AIMessageHandler::GetUserEmail(const SendMessageContext& context)
{
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kUserServiceName);
    if (channel == nullptr)
    {
        ERR("获取用户子服务信道失败, request_id: {}, 服务名称: {}",
            context.request_id, kUserServiceName);
        throw ChatExcelException(ErrorCode::AI_USER_RPC_ERROR);
    }

    user_proto::GetUserInfoRequest rpc_request;
    rpc_request.set_request_id(context.request_id);
    rpc_request.set_session_id(context.session_id);
    rpc_request.set_user_id(context.user_id);

    user_proto::UserService_Stub user_service_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kLightRpcTimeoutMs);
    user_proto::GetUserInfoResponse rpc_response;
    user_service_stub.GetUserInfo(&controller, &rpc_request, &rpc_response, nullptr);

    if (controller.Failed())
    {
        ERR("用户子服务 RPC 调用失败, request_id: {}, 错误信息: {}",
            context.request_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::AI_USER_RPC_ERROR);
    }
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("获取用户信息失败, request_id: {}, 错误码: {}, 错误信息: {}",
            context.request_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }

    const std::string email = rpc_response.result().user_info().email();
    if (email.empty())
    {
        ERR("用户邮箱为空, request_id: {}, user_id: {}", context.request_id, context.user_id);
        throw ChatExcelException(ErrorCode::AI_USER_EMAIL_EMPTY);
    }
    return email;
}

std::string AIMessageHandler::BuildEmailParamJson(const std::string& request_id,
                                                  const std::string& chat_session_id)
{
    // 获取会话历史消息, ChatSDK 中会话不存在时返回空指针
    const std::shared_ptr<aichat_sdk::Session> session = ai_chat_sdk_->GetSession(chat_session_id);
    if (session == nullptr)
    {
        ERR("ChatSDK 中聊天会话不存在, request_id: {}, chat_session_id: {}",
            request_id, chat_session_id);
        throw ChatExcelException(ErrorCode::AI_CHAT_SDK_SESSION_NOT_FOUND);
    }
    const std::vector<aichat_sdk::Message>& messages = session->messages;

    // 从后向前查找最近一条包含 summary 与 displayType 字段的 assistant 消息,
    // 即上一轮对话的最终 JSON 消息(上一轮的中间消息在对话完成后已被清理, 不存在了)
    std::string question;
    std::string analysis_content;
    std::string summary_content;
    size_t final_json_index = messages.size();
    for (size_t i = messages.size(); i > 0; --i)
    {
        const aichat_sdk::Message& message = messages[i - 1];
        if (message.role != "assistant")
        {
            continue;
        }
        try
        {
            const Json::Value final_json =
                ParseModelJson(message.content, ErrorCode::AI_SUMMARY_CONTENT_PARSE_ERROR);
            // 获取 JSON 字段前先判断字段是否存在, 最终 JSON 消息以 summary 与 displayType 字段为标识
            if (final_json.isMember("summary") && final_json["summary"].isString()
                && final_json.isMember("displayType") && final_json["displayType"].isString())
            {
                summary_content = final_json["summary"].asString();
                final_json_index = i - 1;
                break;
            }
        }
        catch (const ChatExcelException&)
        {
            // JSON 解析失败说明该消息不是最终 JSON 消息, 继续向前扫描
        }
    }
    if (final_json_index == messages.size())
    {
        ERR("历史消息中缺少最终 JSON 消息, request_id: {}", request_id);
        throw ChatExcelException(ErrorCode::AI_EMAIL_HISTORY_ERROR);
    }

    // 上一轮用户提问为最终 JSON 消息之前最近的一条用户消息
    for (int j = static_cast<int>(final_json_index) - 1; j >= 0; --j)
    {
        if (messages[j].role == "user")
        {
            question = messages[j].content;
            break;
        }
    }
    if (question.empty())
    {
        ERR("历史消息中缺少上一轮用户提问, request_id: {}", request_id);
        throw ChatExcelException(ErrorCode::AI_EMAIL_HISTORY_ERROR);
    }

    // 分析内容为可选参数 : 中间分析消息在对话完成后已被清理, 仅未清理的历史会话中存在
    for (int j = static_cast<int>(final_json_index) - 1; j >= 0; --j)
    {
        const aichat_sdk::Message& message = messages[j];
        if (message.role == "assistant"
            && message.content.find(kAnalysisStartTag) != std::string::npos)
        {
            const std::string title = ExtractTaggedContent(message.content, kTitleStartTag, kTitleEndTag);
            const std::string analysis =
                ExtractTaggedContent(message.content, kAnalysisStartTag, kAnalysisEndTag);
            analysis_content = "标题: " + title + "\n分析内容: " + analysis;
            break;
        }
    }

    // 构建邮箱参数 JSON
    Json::Value email_param;
    email_param["question"] = question;
    email_param["analysis"] = analysis_content;
    email_param["summary"] = summary_content;
    std::string email_param_json;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(email_param, email_param_json))
    {
        ERR("邮箱参数 JSON 序列化失败, request_id: {}", request_id);
        throw ChatExcelException(ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
    INFO("邮件参数构建完成, request_id: {}, question 长度: {}, analysis 长度: {}, summary 长度: {}",
         request_id, question.size(), analysis_content.size(), summary_content.size());
    return email_param_json;
}

std::string AIMessageHandler::BuildEmailPrompt(const std::string& email_param_json,
                                               const std::string& user_input)
{
    PromptTemplate prompt_template(email_prompt_template_, kEmailPromptPlaceholders);
    prompt_template.SetPlaceholder("email_param", email_param_json);
    prompt_template.SetPlaceholder("user_input", user_input);
    return prompt_template.Generate();
}

void AIMessageHandler::SendEmailByNotifyService(const SendMessageContext& context,
                                                const std::string& to_email,
                                                const std::string& subject, const std::string& content)
{
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kNotifyServiceName);
    if (channel == nullptr)
    {
        ERR("获取通知子服务信道失败, request_id: {}, 服务名称: {}",
            context.request_id, kNotifyServiceName);
        throw ChatExcelException(ErrorCode::AI_NOTIFY_RPC_ERROR);
    }

    notify_proto::SendEmailRequest rpc_request;
    rpc_request.set_request_id(context.request_id);
    rpc_request.set_to_email(to_email);
    rpc_request.set_subject(subject);
    rpc_request.set_content(content);

    notify_proto::NotifyService_Stub notify_service_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kEmailRpcTimeoutMs);
    notify_proto::SendEmailResponse rpc_response;
    notify_service_stub.SendEmail(&controller, &rpc_request, &rpc_response, nullptr);

    if (controller.Failed())
    {
        ERR("通知子服务 RPC 调用失败, request_id: {}, to_email: {}, 错误信息: {}",
            context.request_id, to_email, controller.ErrorText());
        throw ChatExcelException(ErrorCode::AI_NOTIFY_RPC_ERROR);
    }
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("邮件发送失败, request_id: {}, to_email: {}, 错误码: {}, 错误信息: {}",
            context.request_id, to_email, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }
}

std::string AIMessageHandler::ReadPromptTemplateFile(const std::string& file_name)
{
    const std::string file_path = prompt_template_dir_ + "/" + file_name;
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        ERR("提示词模板文件打开失败, 文件路径: {}", file_path);
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    if (content.empty())
    {
        ERR("提示词模板文件内容为空, 文件路径: {}", file_path);
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    return content;
}

std::string AIMessageHandler::TruncateUtf8(const std::string& text, size_t max_chars)
{
    size_t offset = 0;
    size_t char_count = 0;
    while (offset < text.size() && char_count < max_chars)
    {
        // UTF-8 多字节序列长度由首字节高位决定, 最多 4 字节
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        size_t char_size = 1;
        if ((lead & 0xE0) == 0xC0)
        {
            char_size = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            char_size = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            char_size = 4;
        }
        if (offset + char_size > text.size())
        {
            break;
        }
        offset += char_size;
        ++char_count;
    }
    return text.substr(0, offset);
}

} // namespace ai_service
} // namespace chat_excel
