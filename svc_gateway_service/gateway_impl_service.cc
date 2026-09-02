#include "gateway_impl_service.h"
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <brpc/controller.h>
#include <httplib.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include <cpp-toolkit/util.h>
#include <jsoncpp/json/json.h>
#include "common/exception.h"
#include "ai_service.pb.h"
#include "database_service.pb.h"
#include "file_service.pb.h"
#include "user_service.pb.h"

namespace chat_excel
{

// proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace proto = ::chat_excel_proto::user_service;

// 文件子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace file_proto = ::chat_excel_proto::file_service;

// 数据库子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace db_proto = ::chat_excel_proto::database_service;

// AI 子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace ai_proto = ::chat_excel_proto::ai_service;

namespace
{

// 用户子服务名称(与用户子服务注册到 ETCD 注册中心的服务名保持一致)
constexpr char kUserServiceName[] = "UserService";

// 文件子服务名称(与文件子服务注册到 ETCD 注册中心的服务名保持一致)
constexpr char kFileServiceName[] = "FileService";

// 数据库子服务名称(与数据库子服务注册到 ETCD 注册中心的服务名保持一致)
constexpr char kDatabaseServiceName[] = "DataBaseService";

// AI 子服务名称(与 AI 子服务注册到 ETCD 注册中心的服务名保持一致)
constexpr char kAiServiceName[] = "AIService";

// 用户子服务 RPC 调用超时时间(毫秒)
constexpr int kRpcTimeoutMilliseconds = 3000;

// 文件子服务 RPC 调用超时时间(毫秒), 文件上传/下载涉及大二进制数据传输, 超时时间较长
constexpr int kFileRpcTimeoutMilliseconds = 30 * 1000;

// AI 子服务 RPC 调用超时时间(毫秒), 模型列表/会话管理均为轻量操作
constexpr int kAiRpcTimeoutMilliseconds = 3000;

// [A06] AI 子服务发送消息 HTTP 接口路径, brpc 以"服务全名/方法名"匹配 HTTP 请求
constexpr char kAiSendMessageHttpPath[] = "/chat_excel_proto.ai_service.AIService/SendMessage";

// [A06] AI 子服务 HTTP 客户端读超时时间(秒), 流式聊天响应持续时间可能较长
constexpr int kAiHttpReadTimeoutSeconds = 300;

// [A06] AI 子服务 HTTP 客户端写超时时间(秒)
constexpr int kAiHttpWriteTimeoutSeconds = 300;

// [A06] AI 子服务 HTTP 客户端连接超时时间(秒)
constexpr int kAiHttpConnectTimeoutSeconds = 60;

// SSE 流式响应结束标记
constexpr char kSseDoneFrame[] = "data: [DONE]\n\n";

// HTTP 成功状态码
constexpr int kHttpStatusOk = 200;

// HTTP 服务器内部错误状态码
constexpr int kHttpStatusInternalError = 500;

// 网关错误码 : 请求参数错误
constexpr int kGatewayErrorCodeParams = 400;

// 网关错误码 : 后端子服务不可用
constexpr int kGatewayErrorCodeUnavailable = 503;

// [F06] 预览 Excel 文件接口的默认页码(分页参数缺省时使用)
constexpr int kDefaultPreviewPageNumber = 1;

// [F06] 预览 Excel 文件接口的默认每页行数(分页参数缺省时使用)
constexpr int kDefaultPreviewPageSize = 50;

/**
 * @brief 解析 HTTP 请求体 JSON, 提取请求 ID
 * @param request HTTP 请求对象
 * @param request_json 输出参数, 解析后的请求体 JSON 对象
 * @param request_id 输出参数, 请求体中的请求 ID
 * @return true 解析成功且请求 ID 非空, false 解析失败或请求 ID 为空
 */
bool ParseJsonBody(const httplib::Request& request, Json::Value& request_json, std::string& request_id)
{
    // 反序列化请求体 JSON
    if (!cpp_toolkit::JsonUtil::UnSerialize(request_json, request.body))
    {
        ERR("请求体 JSON 反序列化失败, 请求路径: {}", request.path);
        return false;
    }

    // 提取请求 ID, 所有请求必填
    request_id = request_json["requestId"].asString();
    if (request_id.empty())
    {
        ERR("请求体缺少 requestId 字段或字段为空, 请求路径: {}", request.path);
        return false;
    }
    return true;
}

/**
 * @brief 解析 HTTP 请求 query 参数中的通用字段(请求 ID 与会话 ID), 用于参数在 query 中的文件接口
 * @param request HTTP 请求对象
 * @param request_id 输出参数, query 中的请求 ID
 * @param session_id 输出参数, query 中的会话 ID
 * @return true 两个参数均非空, false 存在缺失或空值
 */
bool ParseQueryBaseParams(const httplib::Request& request, std::string& request_id, std::string& session_id)
{
    request_id = request.get_param_value("requestId");
    session_id = request.get_param_value("sessionId");
    if (request_id.empty() || session_id.empty())
    {
        ERR("请求 query 参数错误, requestId 或 sessionId 为空, 请求路径: {}", request.path);
        return false;
    }
    return true;
}

/**
 * @brief 构建通用响应信封并发送 HTTP 响应, HTTP 状态码统一为 200, 处理结果通过 errorCode 表达
 * @param response HTTP 响应对象
 * @param request_id 请求 ID, 回显到响应中用于链路追踪
 * @param error_code 错误码, 0 表示成功, 网关错误码(400/503)或透传的后端业务错误码
 * @param error_msg 错误信息, 成功时为空字符串
 * @param result 返回数据 JSON 对象, 无返回数据的接口不传该参数(响应中不携带 result 字段)
 */
void SendEnvelopeResponse(httplib::Response& response, const std::string& request_id,
                          int error_code, const std::string& error_msg, const Json::Value& result = Json::Value())
{
    // 构建通用响应信封
    Json::Value response_json;
    response_json["requestId"] = request_id;
    response_json["errorCode"] = error_code;
    response_json["errorMsg"] = error_msg;
    if (!result.isNull())
    {
        response_json["result"] = result;
    }

    // 使用 JsonUtil 序列化为紧凑格式 JSON 字符串
    std::string response_body;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(response_json, response_body))
    {
        ERR("HTTP 响应序列化失败, requestId: {}, errorCode: {}", request_id, error_code);
        response.status = kHttpStatusInternalError;
        return;
    }

    // HTTP 状态码统一为 200, 业务处理结果通过 errorCode 表达
    response.status = kHttpStatusOk;
    response.set_content(response_body, "application/json");
}

/**
 * @brief 向 SSE 数据接收器发送一条 SSE 数据帧, 帧体为通用响应信封格式(不含 requestId),
 *        序列化时自动完成消息片段内容的 JSON 转义
 * @param data_sink SSE 数据接收器
 * @param content 消息片段内容
 * @param done 是否结束
 * @param error_code 错误码, 0 表示成功
 * @param error_msg 错误信息, 成功时为空字符串
 */
void SendSseFrame(httplib::DataSink& data_sink, const std::string& content,
                  bool done, int error_code, const std::string& error_msg)
{
    // 构建帧体 JSON
    Json::Value frame_json;
    frame_json["content"] = content;
    frame_json["done"] = done;
    frame_json["errorCode"] = error_code;
    frame_json["errorMsg"] = error_msg;
    std::string frame_body;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(frame_json, frame_body))
    {
        ERR("SSE 数据帧序列化失败, errorCode: {}", error_code);
        return;
    }

    // SSE 帧格式 : data: {帧体}\n\n
    const std::string frame = "data: " + frame_body + "\n\n";
    data_sink.write(frame.data(), frame.size());
}

/**
 * @brief 从 brpc 信道对象中解析子服务地址(host:port), 用于构建 HTTP 客户端
 * @param channel brpc 信道对象
 * @return 地址字符串, 解析失败返回空字符串
 */
std::string ParseChannelAddress(const cpp_toolkit::ChannelPtr& channel)
{
    // 信道描述格式为 "Channel[host:port]", 提取方括号内的地址
    brpc::DescribeOptions describe_options;
    std::ostringstream describe_stream;
    channel->Describe(describe_stream, describe_options);
    const std::string description = describe_stream.str();

    const size_t begin_pos = description.find('[');
    const size_t end_pos = description.find(']');
    if (begin_pos == std::string::npos || end_pos == std::string::npos || end_pos <= begin_pos + 1)
    {
        ERR("解析子服务信道地址失败, 信道描述: {}", description);
        return "";
    }
    return description.substr(begin_pos + 1, end_pos - begin_pos - 1);
}

/**
 * @brief 用户子服务 RPC 同步调用通用流程封装, 内部完成超时设置、接口调用与结果检查
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param user_service_stub 用户子服务 RPC 客户端存根
 * @param rpc_method RPC 客户端成员函数指针, 指向调用的 RPC 接口
 * @param rpc_request RPC 请求对象
 * @param rpc_response 输出参数, RPC 响应对象
 * @param error_code 输出参数, 调用失败时的错误码(RPC 调用失败为网关错误码 503, 业务失败为透传的后端业务错误码)
 * @param error_msg 输出参数, 调用失败时的错误信息
 * @return true RPC 调用成功且业务处理成功, false RPC 调用失败或业务处理失败(错误信息通过输出参数返回)
 */
template <typename RequestType, typename ResponseType>
bool CallUserRpc(const std::string& request_id,
                 proto::UserService_Stub* user_service_stub,
                 void (proto::UserService_Stub::*rpc_method)(google::protobuf::RpcController*, const RequestType*, ResponseType*, google::protobuf::Closure*),
                 const RequestType& rpc_request,
                 ResponseType& rpc_response,
                 int& error_code,
                 std::string& error_msg)
{
    // 同步调用用户子服务的 RPC 接口, 设置调用超时时间
    brpc::Controller controller;
    controller.set_timeout_ms(kRpcTimeoutMilliseconds);
    (user_service_stub->*rpc_method)(&controller, &rpc_request, &rpc_response, nullptr);

    // RPC 调用失败(网络超时、服务不可达等), 返回后端服务不可用错误码
    if (controller.Failed())
    {
        ERR("用户子服务 RPC 调用失败, requestId: {}, 错误信息: {}", request_id, controller.ErrorText());
        error_code = kGatewayErrorCodeUnavailable;
        error_msg = "用户子服务 RPC 调用失败 : " + controller.ErrorText();
        return false;
    }

    // RPC 调用成功但业务处理失败, 透传后端业务错误码与错误信息
    error_code = rpc_response.error_code();
    error_msg = rpc_response.error_msg();
    if (error_code != static_cast<int>(ErrorCode::SUCCESS))
    {
        return false;
    }
    return true;
}

/**
 * @brief 文件子服务 RPC 同步调用通用流程封装, 内部完成超时设置、附件传输、接口调用与结果检查
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param file_service_stub 文件子服务 RPC 客户端存根
 * @param rpc_method RPC 客户端成员函数指针, 指向调用的 RPC 接口
 * @param rpc_request RPC 请求对象
 * @param rpc_response 输出参数, RPC 响应对象
 * @param error_code 输出参数, 调用失败时的错误码(RPC 调用失败为网关错误码 503, 业务失败为透传的后端业务错误码)
 * @param error_msg 输出参数, 调用失败时的错误信息
 * @param request_attachment 请求附件指针, 文件上传场景指向文件二进制数据, 无附件时传 nullptr(brpc attachment 无需序列化, 零拷贝传输)
 * @param response_attachment 输出参数指针, 文件下载场景指向接收文件二进制数据的字符串, 无附件时传 nullptr
 * @return true RPC 调用成功且业务处理成功, false RPC 调用失败或业务处理失败(错误信息通过输出参数返回)
 */
template <typename RequestType, typename ResponseType>
bool CallFileRpc(const std::string& request_id,
                 file_proto::FileService_Stub* file_service_stub,
                 void (file_proto::FileService_Stub::*rpc_method)(google::protobuf::RpcController*, const RequestType*, ResponseType*, google::protobuf::Closure*),
                 const RequestType& rpc_request,
                 ResponseType& rpc_response,
                 int& error_code,
                 std::string& error_msg,
                 const std::string* request_attachment = nullptr,
                 std::string* response_attachment = nullptr)
{
    // 同步调用文件子服务的 RPC 接口, 设置调用超时时间
    brpc::Controller controller;
    controller.set_timeout_ms(kFileRpcTimeoutMilliseconds);

    // 文件上传场景, 文件二进制数据写入请求 attachment 传输
    if (request_attachment != nullptr)
    {
        controller.request_attachment().append(*request_attachment);
    }

    (file_service_stub->*rpc_method)(&controller, &rpc_request, &rpc_response, nullptr);

    // RPC 调用失败(网络超时、服务不可达等), 返回后端服务不可用错误码
    if (controller.Failed())
    {
        ERR("文件子服务 RPC 调用失败, requestId: {}, 错误信息: {}", request_id, controller.ErrorText());
        error_code = kGatewayErrorCodeUnavailable;
        error_msg = "文件子服务 RPC 调用失败 : " + controller.ErrorText();
        return false;
    }

    // 文件下载场景, 从响应 attachment 中提取文件二进制数据
    if (response_attachment != nullptr)
    {
        *response_attachment = controller.response_attachment().to_string();
    }

    // RPC 调用成功但业务处理失败, 透传后端业务错误码与错误信息
    error_code = rpc_response.error_code();
    error_msg = rpc_response.error_msg();
    if (error_code != static_cast<int>(ErrorCode::SUCCESS))
    {
        return false;
    }
    return true;
}

/**
 * @brief 数据库子服务 RPC 同步调用通用流程封装, 内部完成超时设置、接口调用与结果检查
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param database_service_stub 数据库子服务 RPC 客户端存根
 * @param rpc_method RPC 客户端成员函数指针, 指向调用的 RPC 接口
 * @param rpc_request RPC 请求对象
 * @param rpc_response 输出参数, RPC 响应对象
 * @param error_code 输出参数, 调用失败时的错误码(RPC 调用失败为网关错误码 503, 业务失败为透传的后端业务错误码)
 * @param error_msg 输出参数, 调用失败时的错误信息
 * @return true RPC 调用成功且业务处理成功, false RPC 调用失败或业务处理失败(错误信息通过输出参数返回)
 */
template <typename RequestType, typename ResponseType>
bool CallDatabaseRpc(const std::string& request_id,
                     db_proto::DatabaseService_Stub* database_service_stub,
                     void (db_proto::DatabaseService_Stub::*rpc_method)(google::protobuf::RpcController*, const RequestType*, ResponseType*, google::protobuf::Closure*),
                     const RequestType& rpc_request,
                     ResponseType& rpc_response,
                     int& error_code,
                     std::string& error_msg)
{
    // 同步调用数据库子服务的 RPC 接口, 设置调用超时时间
    brpc::Controller controller;
    controller.set_timeout_ms(kRpcTimeoutMilliseconds);
    (database_service_stub->*rpc_method)(&controller, &rpc_request, &rpc_response, nullptr);

    // RPC 调用失败(网络超时、服务不可达等), 返回后端服务不可用错误码
    if (controller.Failed())
    {
        ERR("数据库子服务 RPC 调用失败, requestId: {}, 错误信息: {}", request_id, controller.ErrorText());
        error_code = kGatewayErrorCodeUnavailable;
        error_msg = "数据库子服务 RPC 调用失败 : " + controller.ErrorText();
        return false;
    }

    // RPC 调用成功但业务处理失败, 透传后端业务错误码与错误信息
    error_code = rpc_response.error_code();
    error_msg = rpc_response.error_msg();
    if (error_code != static_cast<int>(ErrorCode::SUCCESS))
    {
        return false;
    }
    return true;
}

/**
 * @brief AI 子服务 RPC 同步调用通用流程封装, 内部完成超时设置、接口调用与结果检查
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param ai_service_stub AI 子服务 RPC 客户端存根
 * @param rpc_method RPC 客户端成员函数指针, 指向调用的 RPC 接口
 * @param rpc_request RPC 请求对象
 * @param rpc_response 输出参数, RPC 响应对象
 * @param error_code 输出参数, 调用失败时的错误码(RPC 调用失败为网关错误码 503, 业务失败为透传的后端业务错误码)
 * @param error_msg 输出参数, 调用失败时的错误信息
 * @return true RPC 调用成功且业务处理成功, false RPC 调用失败或业务处理失败(错误信息通过输出参数返回)
 */
template <typename RequestType, typename ResponseType>
bool CallAiRpc(const std::string& request_id,
               ai_proto::AIService_Stub* ai_service_stub,
               void (ai_proto::AIService_Stub::*rpc_method)(google::protobuf::RpcController*, const RequestType*, ResponseType*, google::protobuf::Closure*),
               const RequestType& rpc_request,
               ResponseType& rpc_response,
               int& error_code,
               std::string& error_msg)
{
    // 同步调用 AI 子服务的 RPC 接口, 设置调用超时时间
    brpc::Controller controller;
    controller.set_timeout_ms(kAiRpcTimeoutMilliseconds);
    (ai_service_stub->*rpc_method)(&controller, &rpc_request, &rpc_response, nullptr);

    // RPC 调用失败(网络超时、服务不可达等), 返回后端服务不可用错误码
    if (controller.Failed())
    {
        ERR("AI 子服务 RPC 调用失败, requestId: {}, 错误信息: {}", request_id, controller.ErrorText());
        error_code = kGatewayErrorCodeUnavailable;
        error_msg = "AI 子服务 RPC 调用失败 : " + controller.ErrorText();
        return false;
    }

    // RPC 调用成功但业务处理失败, 透传后端业务错误码与错误信息
    error_code = rpc_response.error_code();
    error_msg = rpc_response.error_msg();
    if (error_code != static_cast<int>(ErrorCode::SUCCESS))
    {
        return false;
    }
    return true;
}

} // namespace

GatewayServiceImpl::GatewayServiceImpl(const cpp_toolkit::ChannelManager::Ptr& channel_manager)
    : channel_manager_(channel_manager)
{
    INFO("网关 HTTP 接口定义对象初始化完成");
}

void GatewayServiceImpl::BindRoutes(httplib::Server& server)
{
    // 捕获 shared_from_this, 保证接口对象生命周期覆盖服务器运行期(路由回调持有对象引用)
    std::shared_ptr<GatewayServiceImpl> self = shared_from_this();

    // ==================== 分组 0 : 健康检测接口 ====================

    // [H01] 健康检测
    server.Get("/health", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleHealthCheck(request, response);
    });

    // ==================== 分组 1 : 用户子服务接口 ====================

    // [U01] 检测用户昵称是否唯一
    server.Post("/api/user/valid/nickname", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserNicknameValid(request, response);
    });

    // [U02] 检测邮箱是否唯一
    server.Post("/api/user/valid/email", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserEmailValid(request, response);
    });

    // [U03] 用户注册
    server.Post("/api/user/register", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserRegister(request, response);
    });

    // [U04] 密码登录
    server.Post("/api/user/passwd/login", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserPasswdLogin(request, response);
    });

    // [U05] 获取验证码(发送到用户邮箱)
    server.Post("/api/user/code", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserCode(request, response);
    });

    // [U06] 验证码登录
    server.Post("/api/user/vcode/login", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserVcodeLogin(request, response);
    });

    // [U07] 会话登录(用已有 sessionId 恢复登录态)
    server.Post("/api/user/session/login", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserSessionLogin(request, response);
    });

    // [U08] 退出登录
    server.Post("/api/user/logout", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserLogout(request, response);
    });

    // [U09] 获取用户信息(参数全部在 query 中)
    server.Post("/api/user/info", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleUserInfo(request, response);
    });

    // ==================== 分组 2 : 文件子服务接口 ====================

    // [F01] 上传文件信息(第一步 : 登记元数据, 返回 fileId)
    server.Post("/api/file/upload/info", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileUploadInfo(request, response);
    });

    // [F02] 获取文件信息
    server.Get("/api/file/info", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileInfo(request, response);
    });

    // [F03] 上传文件数据(第二步 : 二进制上传, 配合 [F01] 返回的 fileId)
    server.Post("/api/file/upload", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileUpload(request, response);
    });

    // [F04] 下载文件
    server.Get("/api/file/download", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileDownload(request, response);
    });

    // [F05] 删除文件(fileId 为路径参数, 通过正则匹配获取)
    server.Delete(R"(/api/file/([^/]+))", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileDelete(request, response);
    });

    // [F06] 预览 Excel 文件(分页)
    server.Post("/api/file/preview", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFilePreview(request, response);
    });

    // [F07] 获取用户文件列表
    server.Post("/api/file/list", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileList(request, response);
    });

    // [F08] 关联文件和聊天会话映射
    server.Post("/api/file/chat/map", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileChatMap(request, response);
    });

    // [F09] 上传 SQLite 文件(二进制)
    server.Post("/api/file/sqlite/upload", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleFileSqliteUpload(request, response);
    });

    // ==================== 分组 3 : 存储子服务接口 ====================

    // [D01] 新建数据库连接
    server.Post("/api/db/connect", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleDbConnect(request, response);
    });

    // [D02] 断开数据库连接
    server.Post("/api/db/disconnect", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleDbDisconnect(request, response);
    });

    // [D03] 获取数据库表列表
    server.Get("/api/db/tables", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleDbTables(request, response);
    });

    // [D04] 获取表数据
    server.Post("/api/db/table/data", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleDbTableData(request, response);
    });

    // [D05] 获取连接状态
    server.Post("/api/db/connection/status", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleDbConnectionStatus(request, response);
    });

    // ==================== 分组 4 : AI 子服务接口 ====================

    // [A01] 获取支持模型列表
    server.Post("/api/ai/models", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiModels(request, response);
    });

    // [A02] 新建聊天会话
    server.Post("/api/ai/session/create", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiSessionCreate(request, response);
    });

    // [A03] 获取聊天会话列表
    server.Post("/api/ai/chatSessionLists", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiChatSessionLists(request, response);
    });

    // [A04] 获取指定聊天会话历史消息
    server.Post("/api/ai/history", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiHistory(request, response);
    });

    // [A05] 删除指定聊天会话
    server.Post("/api/ai/delete", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiDelete(request, response);
    });

    // [A06] 发送消息(流式, SSE)
    server.Post("/api/ai/sendStreamMessage", [self](const httplib::Request& request, httplib::Response& response)
    {
        self->HandleAiSendStreamMessage(request, response);
    });
}

cpp_toolkit::ChannelPtr GatewayServiceImpl::GetServiceChannel(const std::string& service_name)
{
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(service_name);
    if (channel == nullptr)
    {
        ERR("获取服务信道失败, 服务名称: {}", service_name);
        return nullptr;
    }
    return channel;
}

std::unique_ptr<proto::UserService_Stub> GatewayServiceImpl::CreateUserRpcStub(cpp_toolkit::ChannelPtr& channel)
{
    // 获取用户子服务信道
    channel = GetServiceChannel(kUserServiceName);
    if (channel == nullptr)
    {
        ERR("获取用户子服务信道失败, 服务名称: {}", kUserServiceName);
        return nullptr;
    }

    // 创建用户子服务 RPC 客户端存根, 信道对象通过输出参数交由调用方持有(存根依赖信道对象存活)
    return std::make_unique<proto::UserService_Stub>(channel.get());
}

std::unique_ptr<file_proto::FileService_Stub> GatewayServiceImpl::CreateFileRpcStub(cpp_toolkit::ChannelPtr& channel)
{
    // 获取文件子服务信道
    channel = GetServiceChannel(kFileServiceName);
    if (channel == nullptr)
    {
        ERR("获取文件子服务信道失败, 服务名称: {}", kFileServiceName);
        return nullptr;
    }

    // 创建文件子服务 RPC 客户端存根, 信道对象通过输出参数交由调用方持有(存根依赖信道对象存活)
    return std::make_unique<file_proto::FileService_Stub>(channel.get());
}

std::unique_ptr<db_proto::DatabaseService_Stub> GatewayServiceImpl::CreateDatabaseRpcStub(cpp_toolkit::ChannelPtr& channel)
{
    // 获取数据库子服务信道
    channel = GetServiceChannel(kDatabaseServiceName);
    if (channel == nullptr)
    {
        ERR("获取数据库子服务信道失败, 服务名称: {}", kDatabaseServiceName);
        return nullptr;
    }

    // 创建数据库子服务 RPC 客户端存根, 信道对象通过输出参数交由调用方持有(存根依赖信道对象存活)
    return std::make_unique<db_proto::DatabaseService_Stub>(channel.get());
}

std::unique_ptr<ai_proto::AIService_Stub> GatewayServiceImpl::CreateAiRpcStub(cpp_toolkit::ChannelPtr& channel)
{
    // 获取 AI 子服务信道
    channel = GetServiceChannel(kAiServiceName);
    if (channel == nullptr)
    {
        ERR("获取 AI 子服务信道失败, 服务名称: {}", kAiServiceName);
        return nullptr;
    }

    // 创建 AI 子服务 RPC 客户端存根, 信道对象通过输出参数交由调用方持有(存根依赖信道对象存活)
    return std::make_unique<ai_proto::AIService_Stub>(channel.get());
}

bool GatewayServiceImpl::CheckSessionValid(const std::string& request_id, const std::string& session_id,
                                           std::string& user_id, int& error_code, std::string& error_msg)
{
    // 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        error_code = kGatewayErrorCodeUnavailable;
        error_msg = "用户子服务不可用";
        return false;
    }

    // 构建会话有效性检查 RPC 请求
    proto::ValidSessionRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);

    // 调用 ValidSession RPC 接口进行鉴权, 失败时透传错误码与错误信息
    proto::ValidSessionResponse rpc_response;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::ValidSession,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        ERR("会话鉴权失败, requestId: {}, sessionId: {}, errorCode: {}, 错误信息: {}",
            request_id, session_id, error_code, error_msg);
        return false;
    }

    // 会话有效, 提取会话所属用户 ID, 作为后续文件等子服务 RPC 请求的参数
    user_id = rpc_response.user_id();
    return true;
}

void GatewayServiceImpl::HandleHealthCheck(const httplib::Request& request, httplib::Response& response)
{
    // 构建健康检测响应(完整结构, 不套用通用响应信封)
    Json::Value response_json;
    response_json["status"] = "healthy";
    response_json["service"] = "GatewayService";
    response_json["timestamp"] = static_cast<Json::Int64>(std::time(nullptr));

    // 使用 JsonUtil 序列化为紧凑格式 JSON 字符串
    std::string response_body;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(response_json, response_body))
    {
        ERR("健康检测响应序列化失败, 请求路径: {}", request.path);
        response.status = 500;
        return;
    }

    response.status = 200;
    response.set_content(response_body, "application/json");
    INFO("健康检测接口处理成功, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserNicknameValid(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与昵称
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string nickname = request_json["nickname"].asString();
    if (nickname.empty())
    {
        ERR("[U01] 请求参数错误, nickname 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : nickname 不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::ValidNicknameRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_nickname(nickname);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::ValidNicknameResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::ValidNickname,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[U01] 检测用户昵称是否唯一接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserEmailValid(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与邮箱
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string email = request_json["email"].asString();
    if (email.empty())
    {
        ERR("[U02] 请求参数错误, email 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : email 不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::ValidEmailRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_email(email);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::ValidEmailResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::ValidEmail,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[U02] 检测邮箱是否唯一接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserRegister(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、昵称、密码、邮箱与验证码信息
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string nickname = request_json["nickname"].asString();
    std::string password = request_json["password"].asString();
    std::string email = request_json["email"].asString();
    std::string verify_code = request_json["verifyCode"].asString();
    std::string code_id = request_json["codeId"].asString();
    if (nickname.empty() || password.empty() || email.empty() || verify_code.empty() || code_id.empty())
    {
        ERR("[U03] 请求参数错误, nickname/password/email/verifyCode/codeId 存在空值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : nickname、password、email、verifyCode 与 codeId 均不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::UserRegisterRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_nickname(nickname);
    rpc_request.set_password(password);
    rpc_request.set_email(email);
    rpc_request.set_verify_code(verify_code);
    rpc_request.set_code_id(code_id);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::UserRegisterResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::UserRegister,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[U03] 用户注册接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserPasswdLogin(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、用户名与密码
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string username = request_json["username"].asString();
    std::string password = request_json["password"].asString();
    if (username.empty() || password.empty())
    {
        ERR("[U04] 请求参数错误, username 或 password 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : username 与 password 不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求(用户名可以是昵称或邮箱)
    proto::PasswdLoginRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_username(username);
    rpc_request.set_password(password);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::PasswdLoginResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::PasswdLogin,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建登录结果(会话 ID)并发送 HTTP 响应
    Json::Value result;
    result["sessionId"] = rpc_response.result().session_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[U04] 密码登录接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserCode(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与邮箱
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string email = request_json["email"].asString();
    if (email.empty())
    {
        ERR("[U05] 请求参数错误, email 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : email 不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::GetCodeRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_email(email);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::GetCodeResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::GetCode,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建验证码结果(验证码 ID)并发送 HTTP 响应
    Json::Value result;
    result["codeId"] = rpc_response.result().code_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[U05] 获取验证码接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserVcodeLogin(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、邮箱、验证码与验证码 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string email = request_json["email"].asString();
    std::string verify_code = request_json["verifyCode"].asString();
    std::string code_id = request_json["codeId"].asString();
    if (email.empty() || verify_code.empty() || code_id.empty())
    {
        ERR("[U06] 请求参数错误, email/verifyCode/codeId 存在空值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : email、verifyCode 与 codeId 均不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::VcodeLoginRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_email(email);
    rpc_request.set_verify_code(verify_code);
    rpc_request.set_code_id(code_id);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::VcodeLoginResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::VcodeLogin,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 登录成功后发送删除验证码的 RPC 请求, 使验证码失效防止重复使用;
    //    删除失败不影响登录结果(验证码仍有 TTL 过期兜底), 仅记录警告日志
    proto::DeleteVerifyCodeRequest delete_request;
    delete_request.set_request_id(request_id);
    delete_request.set_code_id(code_id);
    proto::DeleteVerifyCodeResponse delete_response;
    int delete_error_code = 0;
    std::string delete_error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::DeleteVerifyCode,
                     delete_request, delete_response, delete_error_code, delete_error_msg))
    {
        WARN("删除验证码失败, requestId: {}, codeId: {}, errorCode: {}, 错误信息: {}",
             request_id, code_id, delete_error_code, delete_error_msg);
    }

    // 6. 构建登录结果(会话 ID)并发送 HTTP 响应
    Json::Value result;
    result["sessionId"] = rpc_response.result().session_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[U06] 验证码登录接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserSessionLogin(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    if (session_id.empty())
    {
        ERR("[U07] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }

    // 2. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 3. 构建 RPC 请求
    proto::SessionLoginRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);

    // 4. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::SessionLoginResponse rpc_response;
    int error_code = 0;
    std::string error_msg;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::SessionLogin,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[U07] 会话登录接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserLogout(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    if (session_id.empty())
    {
        ERR("[U08] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    proto::LogoutRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);

    // 5. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::LogoutResponse rpc_response;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::Logout,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[U08] 退出登录接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleUserInfo(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数(本接口参数全部在 query 中, 请求体为空, 用户 ID 由会话鉴权获取)
    std::string request_id = request.get_param_value("requestId");
    std::string session_id = request.get_param_value("sessionId");
    if (request_id.empty() || session_id.empty())
    {
        ERR("[U09] 请求参数错误, requestId 或 sessionId 为空, 请求路径: {}", request.path);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取会话所属用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建用户子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<proto::UserService_Stub> user_service_stub = CreateUserRpcStub(channel);
    if (user_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "用户子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求(用户 ID 来自会话鉴权结果)
    proto::GetUserInfoRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用用户子服务的 RPC 接口, 失败时透传错误码与错误信息
    proto::GetUserInfoResponse rpc_response;
    if (!CallUserRpc(request_id, user_service_stub.get(), &proto::UserService_Stub::GetUserInfo,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建用户信息结果并发送 HTTP 响应
    Json::Value user_info;
    user_info["userId"] = rpc_response.result().user_info().user_id();
    user_info["nickname"] = rpc_response.result().user_info().nickname();
    user_info["email"] = rpc_response.result().user_info().email();
    Json::Value result;
    result["userInfo"] = user_info;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[U09] 获取用户信息接口处理完成, requestId: {}, errorCode: {}", request_id, error_code);
}

void GatewayServiceImpl::HandleFileUploadInfo(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID 与文件信息
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    const Json::Value& file_info_json = request_json["fileInfo"];
    std::string filename = file_info_json["filename"].asString();
    int64_t file_size = file_info_json["fileSize"].asInt64();
    std::string file_ext = file_info_json["fileExt"].asString();
    if (session_id.empty() || filename.empty() || file_ext.empty() || file_size <= 0)
    {
        ERR("[F01] 请求参数错误, sessionId 或 fileInfo 存在缺失或非法值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId 与 fileInfo(filename/fileSize/fileExt) 不能为空, fileSize 必须为正整数");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求(请求体中的 chatSessionId 为可选字段, 文件与聊天会话的映射统一由 [F08] 接口建立)
    file_proto::UploadFileInfoRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.mutable_file_info()->set_filename(filename);
    rpc_request.mutable_file_info()->set_file_size(file_size);
    rpc_request.mutable_file_info()->set_file_ext(file_ext);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::UploadFileInfoResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::UploadFileInfo,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建文件信息登记结果(文件 ID)并发送 HTTP 响应
    Json::Value result;
    result["fileId"] = rpc_response.result().file_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F01] 上传文件信息接口处理完成, requestId: {}, fileId: {}, errorCode: {}",
         request_id, rpc_response.result().file_id(), error_code);
}

void GatewayServiceImpl::HandleFileInfo(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数(本接口参数全部在 query 中, 请求体为空)
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string file_id = request.get_param_value("fileId");
    if (file_id.empty())
    {
        ERR("[F02] 请求参数错误, fileId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : fileId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::GetFileInfoRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::GetFileInfoResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::GetFileInfo,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建文件信息结果并发送 HTTP 响应
    const file_proto::FileDetail& file_detail = rpc_response.result();
    Json::Value result;
    result["fileId"] = file_detail.file_id();
    result["fileName"] = file_detail.file_name();
    result["fileSize"] = static_cast<Json::Int64>(file_detail.file_size());
    result["uploadTime"] = static_cast<Json::Int64>(file_detail.upload_time());
    result["fileExt"] = file_detail.file_ext();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F02] 获取文件信息接口处理完成, requestId: {}, fileId: {}, errorCode: {}",
         request_id, file_id, error_code);
}

void GatewayServiceImpl::HandleFileUpload(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数, 请求体为文件二进制数据
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string file_id = request.get_param_value("fileId");
    if (file_id.empty())
    {
        ERR("[F03] 请求参数错误, fileId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : fileId 不能为空");
        return;
    }
    if (request.body.empty())
    {
        ERR("[F03] 请求参数错误, 文件二进制数据为空, requestId: {}, fileId: {}", request_id, file_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : 文件二进制数据不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::UploadFileRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 文件二进制数据通过请求 attachment 传输, 失败时透传错误码与错误信息
    file_proto::UploadFileResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::UploadFile,
                     rpc_request, rpc_response, error_code, error_msg, &request.body))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建上传结果(回显文件 ID, RPC 响应中不携带文件 ID)并发送 HTTP 响应
    Json::Value result;
    result["fileId"] = file_id;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F03] 上传文件数据接口处理完成, requestId: {}, fileId: {}, 文件大小: {}, errorCode: {}",
         request_id, file_id, request.body.size(), error_code);
}

void GatewayServiceImpl::HandleFileDownload(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数(本接口参数全部在 query 中, 请求体为空)
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string file_id = request.get_param_value("fileId");
    if (file_id.empty())
    {
        ERR("[F04] 请求参数错误, fileId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : fileId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 先调用 GetFileInfo RPC 接口获取文件名, 用于设置下载响应头中的文件名
    file_proto::GetFileInfoRequest info_request;
    info_request.set_request_id(request_id);
    info_request.set_session_id(session_id);
    info_request.set_file_id(file_id);
    info_request.set_user_id(user_id);
    file_proto::GetFileInfoResponse info_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::GetFileInfo,
                     info_request, info_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }
    std::string filename = info_response.result().file_name();

    // 5. 调用 DownloadFile RPC 接口, 文件二进制数据通过响应 attachment 传输, 失败时透传错误码与错误信息
    file_proto::DownloadFileRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(user_id);
    file_proto::DownloadFileResponse rpc_response;
    std::string file_content;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::DownloadFile,
                     rpc_request, rpc_response, error_code, error_msg, nullptr, &file_content))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建文件下载响应(二进制流, 不套用通用信封), 文件名通过 Content-Disposition 响应头返回
    response.status = kHttpStatusOk;
    response.set_content(file_content, "application/octet-stream");
    response.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    INFO("[F04] 下载文件接口处理完成, requestId: {}, fileId: {}, 文件名: {}, 文件大小: {}",
         request_id, file_id, filename, file_content.size());
}

void GatewayServiceImpl::HandleFileDelete(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求参数, fileId 为路径参数, requestId 与 sessionId 在 query 中
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string file_id = request.matches[1].str();
    if (file_id.empty())
    {
        ERR("[F05] 请求参数错误, 路径中的 fileId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : fileId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::DeleteFileRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::DeleteFileResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::DeleteFile,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建 HTTP 响应(接口无返回数据, 成功时按 API 约定返回"删除成功"提示)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, "删除成功");
    INFO("[F05] 删除文件接口处理完成, requestId: {}, fileId: {}, errorCode: {}", request_id, file_id, error_code);
}

void GatewayServiceImpl::HandleFilePreview(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID、文件 ID 与分页参数
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string file_id = request_json["fileId"].asString();
    if (session_id.empty() || file_id.empty())
    {
        ERR("[F06] 请求参数错误, sessionId 或 fileId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId 与 fileId 不能为空");
        return;
    }
    // 分页参数为可选字段, 缺省时使用默认页码与每页行数
    int page_number = request_json["pageNumber"].asInt();
    int page_size = request_json["pageSize"].asInt();
    if (page_number <= 0)
    {
        page_number = kDefaultPreviewPageNumber;
    }
    if (page_size <= 0)
    {
        page_size = kDefaultPreviewPageSize;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::PreviewExcelRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_page_number(page_number);
    rpc_request.set_page_size(page_size);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::PreviewExcelResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::PreviewExcel,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建预览结果并发送 HTTP 响应(行数据构建为二维数组)
    const file_proto::PreviewExcelResult& preview_result = rpc_response.result();
    Json::Value result;
    result["fileId"] = preview_result.file_id();
    result["fileName"] = preview_result.file_name();
    result["fileSize"] = static_cast<Json::Int64>(preview_result.file_size());
    result["fileExt"] = preview_result.file_ext();

    Json::Value sheets(Json::arrayValue);
    for (const file_proto::Sheet& sheet : preview_result.excel_data().sheets())
    {
        Json::Value sheet_json;
        sheet_json["name"] = sheet.name();
        sheet_json["totalRows"] = sheet.total_rows();
        sheet_json["colCount"] = sheet.col_count();
        sheet_json["currentPage"] = sheet.current_page();
        sheet_json["totalPages"] = sheet.total_pages();
        sheet_json["pageSize"] = sheet.page_size();

        Json::Value columns(Json::arrayValue);
        for (const std::string& column : sheet.columns())
        {
            columns.append(column);
        }
        sheet_json["columns"] = columns;

        Json::Value rows(Json::arrayValue);
        for (const file_proto::Row& row : sheet.data())
        {
            Json::Value row_json(Json::arrayValue);
            for (const std::string& cell : row.cells())
            {
                row_json.append(cell);
            }
            rows.append(row_json);
        }
        sheet_json["data"] = rows;
        sheets.append(sheet_json);
    }
    Json::Value excel_data_json;
    excel_data_json["sheets"] = sheets;
    result["excelData"] = excel_data_json;

    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F06] 预览 Excel 文件接口处理完成, requestId: {}, fileId: {}, errorCode: {}",
         request_id, file_id, error_code);
}

void GatewayServiceImpl::HandleFileList(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    if (session_id.empty())
    {
        ERR("[F07] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::GetFileListRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::GetFileListResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::GetFileList,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建文件列表结果并发送 HTTP 响应
    Json::Value file_list(Json::arrayValue);
    for (const file_proto::FileListItem& item : rpc_response.result().file_list())
    {
        Json::Value item_json;
        item_json["fileId"] = item.file_id();
        item_json["fileName"] = item.file_name();
        item_json["fileSize"] = static_cast<Json::Int64>(item.file_size());
        item_json["uploadTime"] = static_cast<Json::Int64>(item.upload_time());
        file_list.append(item_json);
    }
    Json::Value result;
    result["fileList"] = file_list;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F07] 获取用户文件列表接口处理完成, requestId: {}, 文件数量: {}, errorCode: {}",
         request_id, rpc_response.result().file_list_size(), error_code);
}

void GatewayServiceImpl::HandleFileChatMap(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID、文件 ID 与聊天会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string file_id = request_json["fileId"].asString();
    std::string chat_session_id = request_json["chatSessionId"].asString();
    if (session_id.empty() || file_id.empty() || chat_session_id.empty())
    {
        ERR("[F08] 请求参数错误, sessionId/fileId/chatSessionId 存在空值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId、fileId 与 chatSessionId 均不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::HandleFileChatSessionMapRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_chat_session_id(chat_session_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, 失败时透传错误码与错误信息
    file_proto::HandleFileChatSessionMapResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::HandleFileChatSessionMap,
                     rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[F08] 关联文件和聊天会话映射接口处理完成, requestId: {}, fileId: {}, chatSessionId: {}, errorCode: {}",
         request_id, file_id, chat_session_id, error_code);
}

void GatewayServiceImpl::HandleFileSqliteUpload(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数, 请求体为 SQLite 文件二进制数据
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string filename = request.get_param_value("filename");
    if (filename.empty())
    {
        ERR("[F09] 请求参数错误, filename 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : filename 不能为空");
        return;
    }
    if (request.body.empty())
    {
        ERR("[F09] 请求参数错误, SQLite 文件二进制数据为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : SQLite 文件二进制数据不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建文件子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<file_proto::FileService_Stub> file_service_stub = CreateFileRpcStub(channel);
    if (file_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "文件子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    file_proto::UploadSQLiteFileRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_filename(filename);
    rpc_request.set_user_id(user_id);

    // 5. 调用文件子服务的 RPC 接口, SQLite 文件二进制数据通过请求 attachment 传输, 失败时透传错误码与错误信息
    file_proto::UploadSQLiteFileResponse rpc_response;
    if (!CallFileRpc(request_id, file_service_stub.get(), &file_proto::FileService_Stub::UploadSQLiteFile,
                     rpc_request, rpc_response, error_code, error_msg, &request.body))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建上传结果(文件 ID)并发送 HTTP 响应
    Json::Value result;
    result["fileId"] = rpc_response.result().file_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[F09] 上传 SQLite 文件接口处理完成, requestId: {}, 文件名: {}, 文件大小: {}, fileId: {}, errorCode: {}",
         request_id, filename, request.body.size(), rpc_response.result().file_id(), error_code);
}

void GatewayServiceImpl::HandleDbConnect(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID 与数据库配置
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    const Json::Value& database_json = request_json["database"];
    std::string database_type = database_json["type"].asString();
    if (session_id.empty() || database_json.isNull() || database_type.empty())
    {
        ERR("[D01] 请求参数错误, sessionId 或 database 缺失或为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId 与 database(type) 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效并获取用户 ID, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建数据库子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<db_proto::DatabaseService_Stub> database_service_stub = CreateDatabaseRpcStub(channel);
    if (database_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "数据库子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求, 将 HTTP 请求中的数据库配置映射为 proto 数据库配置
    db_proto::ConnectDatabaseRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_user_id(user_id);
    db_proto::DatabaseConfig* database_config = rpc_request.mutable_database();
    if (database_type == "MySQL")
    {
        // 校验 MySQL 数据库配置, 主机/库名/用户名不能为空
        const Json::Value& mysql_json = database_json["MySQL"];
        if (mysql_json.isNull() || mysql_json["host"].asString().empty() ||
            mysql_json["name"].asString().empty() || mysql_json["username"].asString().empty())
        {
            ERR("[D01] 请求参数错误, MySQL 数据库配置缺失或 host/name/username 存在空值, requestId: {}", request_id);
            SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                                 "请求参数错误 : MySQL 数据库配置的 host、name 与 username 不能为空");
            return;
        }

        // 填充 MySQL 数据库配置(端口与字符集为空时使用数据库子服务默认值)
        database_config->set_type(db_proto::DATABASE_TYPE_MYSQL);
        db_proto::MySQLDatabaseConfig* mysql_config = database_config->mutable_mysql_config();
        mysql_config->set_host(mysql_json["host"].asString());
        mysql_config->set_port(mysql_json["port"].asInt());
        mysql_config->set_name(mysql_json["name"].asString());
        mysql_config->set_username(mysql_json["username"].asString());
        mysql_config->set_password(mysql_json["password"].asString());
        mysql_config->set_charset(mysql_json["charset"].asString());
    }
    else if (database_type == "SQLite")
    {
        // 校验 SQLite 数据库配置, SQLite 文件 ID 不能为空
        const Json::Value& sqlite_json = database_json["SQLite"];
        if (sqlite_json.isNull() || sqlite_json["fileId"].asString().empty())
        {
            ERR("[D01] 请求参数错误, SQLite 数据库配置缺失或 fileId 为空, requestId: {}", request_id);
            SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                                 "请求参数错误 : SQLite 数据库配置的 fileId 不能为空");
            return;
        }

        // 填充 SQLite 数据库配置(只读标志缺省时默认为 false)
        database_config->set_type(db_proto::DATABASE_TYPE_SQLITE);
        db_proto::SQLiteDatabaseConfig* sqlite_config = database_config->mutable_sqlite_config();
        sqlite_config->set_file_id(sqlite_json["fileId"].asString());
        sqlite_config->set_readonly(sqlite_json["readonly"].asBool());
    }
    else
    {
        ERR("[D01] 请求参数错误, 不支持的数据库类型: {}, requestId: {}", database_type, request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : 数据库类型仅支持 MySQL 与 SQLite");
        return;
    }

    // 5. 调用数据库子服务的 RPC 接口, 失败时透传错误码与错误信息
    db_proto::ConnectDatabaseResponse rpc_response;
    if (!CallDatabaseRpc(request_id, database_service_stub.get(), &db_proto::DatabaseService_Stub::ConnectDatabase,
                         rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建连接结果(数据库连接 ID)并发送 HTTP 响应
    Json::Value result;
    result["connectionId"] = rpc_response.result().connection_id();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[D01] 新建数据库连接接口处理完成, requestId: {}, connectionId: {}, errorCode: {}",
         request_id, rpc_response.result().connection_id(), error_code);
}

void GatewayServiceImpl::HandleDbDisconnect(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID 与数据库连接 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string connection_id = request_json["connectionId"].asString();
    if (session_id.empty() || connection_id.empty())
    {
        ERR("[D02] 请求参数错误, sessionId 或 connectionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId 与 connectionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建数据库子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<db_proto::DatabaseService_Stub> database_service_stub = CreateDatabaseRpcStub(channel);
    if (database_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "数据库子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    db_proto::DisconnectDatabaseRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_connection_id(connection_id);

    // 5. 调用数据库子服务的 RPC 接口, 失败时透传错误码与错误信息
    db_proto::DisconnectDatabaseResponse rpc_response;
    if (!CallDatabaseRpc(request_id, database_service_stub.get(), &db_proto::DatabaseService_Stub::DisconnectDatabase,
                         rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[D02] 断开数据库连接接口处理完成, requestId: {}, connectionId: {}, errorCode: {}",
         request_id, connection_id, error_code);
}

void GatewayServiceImpl::HandleDbTables(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求 query 参数(本接口参数全部在 query 中, 请求体为空)
    std::string request_id;
    std::string session_id;
    if (!ParseQueryBaseParams(request, request_id, session_id))
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }
    std::string db_connect_id = request.get_param_value("dbConnectId");
    if (db_connect_id.empty())
    {
        ERR("[D03] 请求参数错误, dbConnectId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : dbConnectId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建数据库子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<db_proto::DatabaseService_Stub> database_service_stub = CreateDatabaseRpcStub(channel);
    if (database_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "数据库子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    db_proto::ListTablesRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_db_connect_id(db_connect_id);

    // 5. 调用数据库子服务的 RPC 接口, 失败时透传错误码与错误信息
    db_proto::ListTablesResponse rpc_response;
    if (!CallDatabaseRpc(request_id, database_service_stub.get(), &db_proto::DatabaseService_Stub::ListTables,
                         rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建表名列表结果并发送 HTTP 响应
    Json::Value result;
    Json::Value tables(Json::arrayValue);
    for (const std::string& table_name : rpc_response.result().tables())
    {
        tables.append(table_name);
    }
    result["tables"] = tables;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[D03] 获取数据库表列表接口处理完成, requestId: {}, dbConnectId: {}, errorCode: {}",
         request_id, db_connect_id, error_code);
}

void GatewayServiceImpl::HandleDbTableData(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID、数据库连接 ID、表名与强制原始数据标志
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string db_connect_id = request_json["dbConnectId"].asString();
    std::string table_name = request_json["tableName"].asString();
    if (session_id.empty() || db_connect_id.empty() || table_name.empty())
    {
        ERR("[D04] 请求参数错误, sessionId/dbConnectId/tableName 存在空值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId、dbConnectId 与 tableName 不能为空");
        return;
    }
    bool force_original = request_json["forceOriginal"].asBool();

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建数据库子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<db_proto::DatabaseService_Stub> database_service_stub = CreateDatabaseRpcStub(channel);
    if (database_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "数据库子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求(HTTP 接口未携带页码与每页行数, 由数据库子服务按默认值处理)
    db_proto::GetTableDataRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_db_connect_id(db_connect_id);
    rpc_request.set_table_name(table_name);
    rpc_request.set_force_original(force_original);

    // 5. 调用数据库子服务的 RPC 接口, 失败时透传错误码与错误信息
    db_proto::GetTableDataResponse rpc_response;
    if (!CallDatabaseRpc(request_id, database_service_stub.get(), &db_proto::DatabaseService_Stub::GetTableData,
                         rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建表结构信息结果(列信息 + 行数据)并发送 HTTP 响应
    const db_proto::TableSchemaInfo& table_schema = rpc_response.result().table_schema();
    Json::Value column_info(Json::arrayValue);
    for (const db_proto::ColumnInfo& column : table_schema.column_info())
    {
        Json::Value column_json;
        column_json["name"] = column.name();
        column_json["type"] = column.type();
        column_info.append(column_json);
    }
    Json::Value rows(Json::arrayValue);
    for (const db_proto::Row& row : table_schema.table_data().rows())
    {
        Json::Value row_json;
        Json::Value cells(Json::arrayValue);
        for (const std::string& cell : row.cells())
        {
            cells.append(cell);
        }
        row_json["cells"] = cells;
        rows.append(row_json);
    }
    Json::Value table_data;
    table_data["rows"] = rows;
    Json::Value table_schema_json;
    table_schema_json["columnInfo"] = column_info;
    table_schema_json["tableData"] = table_data;
    Json::Value result;
    result["tableSchema"] = table_schema_json;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[D04] 获取表数据接口处理完成, requestId: {}, dbConnectId: {}, tableName: {}, errorCode: {}",
         request_id, db_connect_id, table_name, error_code);
}

void GatewayServiceImpl::HandleDbConnectionStatus(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID、会话 ID 与数据库连接 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string db_connect_id = request_json["dbConnectId"].asString();
    if (session_id.empty() || db_connect_id.empty())
    {
        ERR("[D05] 请求参数错误, sessionId 或 dbConnectId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId 与 dbConnectId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建数据库子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<db_proto::DatabaseService_Stub> database_service_stub = CreateDatabaseRpcStub(channel);
    if (database_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "数据库子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求(获取连接临时表接口不依赖会话 ID 与用户 ID)
    db_proto::GetConnTempTablesRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_db_connect_id(db_connect_id);

    // 5. 调用数据库子服务的 RPC 接口, 失败时透传错误码与错误信息
    db_proto::GetConnTempTablesResponse rpc_response;
    if (!CallDatabaseRpc(request_id, database_service_stub.get(), &db_proto::DatabaseService_Stub::GetConnTempTables,
                         rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建连接状态结果(临时表名列表 + 是否有修改)并发送 HTTP 响应
    Json::Value result;
    Json::Value temp_tables(Json::arrayValue);
    for (const std::string& temp_table : rpc_response.temp_tables())
    {
        temp_tables.append(temp_table);
    }
    result["tempTables"] = temp_tables;
    result["hasModifications"] = rpc_response.has_temp_tables();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[D05] 获取连接状态接口处理完成, requestId: {}, dbConnectId: {}, errorCode: {}",
         request_id, db_connect_id, error_code);
}

void GatewayServiceImpl::HandleAiModels(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    if (session_id.empty())
    {
        ERR("[A01] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息(模型列表接口不使用用户 ID)
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建 AI 子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<ai_proto::AIService_Stub> ai_service_stub = CreateAiRpcStub(channel);
    if (ai_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    ai_proto::GetModelsRequest rpc_request;
    rpc_request.set_request_id(request_id);

    // 5. 调用 AI 子服务的 RPC 接口, 失败时透传错误码与错误信息
    ai_proto::GetModelsResponse rpc_response;
    if (!CallAiRpc(request_id, ai_service_stub.get(), &ai_proto::AIService_Stub::GetModels,
                   rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建模型列表结果并发送 HTTP 响应
    Json::Value result;
    Json::Value model_list(Json::arrayValue);
    for (const ai_proto::ModelInfo& model_info : rpc_response.result().models())
    {
        Json::Value model_json;
        model_json["modelName"] = model_info.name();
        model_json["modelDesc"] = model_info.desc();
        model_list.append(model_json);
    }
    result["modelList"] = model_list;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[A01] 获取支持模型列表接口处理完成, requestId: {}, 模型数量: {}", request_id, model_list.size());
}

void GatewayServiceImpl::HandleAiSessionCreate(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID(title 字段不透传, 会话标题由首条消息发送后自动更新)
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string model_name = request_json["modelName"].asString();
    std::string session_type = request_json["sessionType"].asString();
    if (session_id.empty() || model_name.empty() || session_type.empty())
    {
        ERR("[A02] 请求参数错误, sessionId/modelName/sessionType 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId/modelName/sessionType 不能为空");
        return;
    }

    // 2. 校验会话类型有效性, database 类型会话必须携带数据库连接信息
    std::string db_connection_info;
    if (session_type == "database")
    {
        db_connection_info = request_json["dbConnectionInfo"].asString();
        if (db_connection_info.empty())
        {
            ERR("[A02] 请求参数错误, database 类型会话缺少 dbConnectionInfo, requestId: {}", request_id);
            SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                                 "请求参数错误 : database 类型会话的 dbConnectionInfo 不能为空");
            return;
        }
    }
    else if (session_type != "excel")
    {
        ERR("[A02] 请求参数错误, sessionType 无效: {}, requestId: {}", session_type, request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionType 仅支持 excel/database");
        return;
    }

    // 3. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息(获取用户 ID 作为会话归属用户)
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 4. 创建 AI 子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<ai_proto::AIService_Stub> ai_service_stub = CreateAiRpcStub(channel);
    if (ai_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }

    // 5. 构建 RPC 请求
    ai_proto::CreateChatSessionRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_user_id(user_id);
    rpc_request.set_model(model_name);
    rpc_request.set_session_type(session_type);
    rpc_request.set_db_connection_info(db_connection_info);

    // 6. 调用 AI 子服务的 RPC 接口, 失败时透传错误码与错误信息
    ai_proto::CreateChatSessionResponse rpc_response;
    if (!CallAiRpc(request_id, ai_service_stub.get(), &ai_proto::AIService_Stub::CreateSession,
                   rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 7. 构建聊天会话数据结果并发送 HTTP 响应
    Json::Value result;
    result["chatSessionId"] = rpc_response.result().session().chat_session_id();
    result["modelName"] = rpc_response.result().session().model();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[A02] 新建聊天会话接口处理完成, requestId: {}, userId: {}, chatSessionId: {}, errorCode: {}",
         request_id, user_id, result["chatSessionId"].asString(), error_code);
}

void GatewayServiceImpl::HandleAiChatSessionLists(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    if (session_id.empty())
    {
        ERR("[A03] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息(获取用户 ID 用于查询其会话列表)
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建 AI 子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<ai_proto::AIService_Stub> ai_service_stub = CreateAiRpcStub(channel);
    if (ai_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    ai_proto::GetSessionsRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_user_id(user_id);

    // 5. 调用 AI 子服务的 RPC 接口, 失败时透传错误码与错误信息
    ai_proto::GetSessionsResponse rpc_response;
    if (!CallAiRpc(request_id, ai_service_stub.get(), &ai_proto::AIService_Stub::GetSessions,
                   rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建会话列表结果并发送 HTTP 响应
    Json::Value result;
    Json::Value session_list(Json::arrayValue);
    for (const ai_proto::SessionInfo& session_info : rpc_response.result().sessioninfo())
    {
        Json::Value session_json;
        session_json["chatSessionId"] = session_info.id();
        session_json["modelName"] = session_info.model();
        session_json["title"] = session_info.title();
        session_json["createdAt"] = static_cast<Json::Int64>(session_info.created_at());
        session_json["updatedAt"] = static_cast<Json::Int64>(session_info.updated_at());
        session_json["messageCount"] = session_info.message_count();
        session_json["firstUserMessageContent"] = session_info.first_user_message_content();
        session_json["sessionType"] = session_info.session_type();
        session_json["dbConnectionInfo"] = session_info.db_connection_info();
        session_list.append(session_json);
    }
    result["chatSessionLists"] = session_list;
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[A03] 获取聊天会话列表接口处理完成, requestId: {}, userId: {}, 会话数量: {}",
         request_id, user_id, session_list.size());
}

void GatewayServiceImpl::HandleAiHistory(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string chat_session_id = request_json["chatSessionId"].asString();
    if (session_id.empty() || chat_session_id.empty())
    {
        ERR("[A04] 请求参数错误, sessionId/chatSessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId/chatSessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息(获取用户 ID 用于校验会话归属)
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建 AI 子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<ai_proto::AIService_Stub> ai_service_stub = CreateAiRpcStub(channel);
    if (ai_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    ai_proto::GetSessionHistoryRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_user_id(user_id);
    rpc_request.set_chat_session_id(chat_session_id);

    // 5. 调用 AI 子服务的 RPC 接口, 失败时透传错误码与错误信息
    ai_proto::GetSessionHistoryResponse rpc_response;
    if (!CallAiRpc(request_id, ai_service_stub.get(), &ai_proto::AIService_Stub::GetSessionHistory,
                   rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建历史消息列表结果并发送 HTTP 响应(会话未关联文件时 fileId/dbConnectionInfo 为空字符串)
    Json::Value result;
    Json::Value message_list(Json::arrayValue);
    for (const ai_proto::HistoryMessage& history_message : rpc_response.result().messages())
    {
        Json::Value message_json;
        message_json["id"] = history_message.id();
        message_json["role"] = history_message.role();
        message_json["content"] = history_message.content();
        message_json["timestamp"] = static_cast<Json::Int64>(history_message.timestamp());
        message_list.append(message_json);
    }
    result["messageList"] = message_list;
    result["fileId"] = rpc_response.result().file_id();
    result["sessionType"] = rpc_response.result().session_type();
    result["dbConnectionInfo"] = rpc_response.result().db_connection_info();
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg, result);
    INFO("[A04] 获取指定聊天会话历史消息接口处理完成, requestId: {}, userId: {}, chatSessionId: {}, 消息数量: {}",
         request_id, user_id, chat_session_id, message_list.size());
}

void GatewayServiceImpl::HandleAiDelete(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID 与会话 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }
    std::string session_id = request_json["sessionId"].asString();
    std::string chat_session_id = request_json["chatSessionId"].asString();
    if (session_id.empty() || chat_session_id.empty())
    {
        ERR("[A05] 请求参数错误, sessionId/chatSessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : sessionId/chatSessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息(获取用户 ID 用于校验会话归属)
    int error_code = 0;
    std::string error_msg;
    std::string user_id;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 3. 创建 AI 子服务 RPC 客户端
    cpp_toolkit::ChannelPtr channel;
    std::unique_ptr<ai_proto::AIService_Stub> ai_service_stub = CreateAiRpcStub(channel);
    if (ai_service_stub == nullptr)
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }

    // 4. 构建 RPC 请求
    ai_proto::DeleteSessionRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_user_id(user_id);
    rpc_request.set_chat_session_id(chat_session_id);

    // 5. 调用 AI 子服务的 RPC 接口, 失败时透传错误码与错误信息
    ai_proto::DeleteSessionResponse rpc_response;
    if (!CallAiRpc(request_id, ai_service_stub.get(), &ai_proto::AIService_Stub::DeleteSession,
                   rpc_request, rpc_response, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 6. 构建 HTTP 响应(接口无返回数据, 不携带 result 字段)
    SendEnvelopeResponse(response, rpc_response.request_id(), error_code, error_msg);
    INFO("[A05] 删除指定聊天会话接口处理完成, requestId: {}, userId: {}, chatSessionId: {}, errorCode: {}",
         request_id, user_id, chat_session_id, error_code);
}

void GatewayServiceImpl::HandleAiSendStreamMessage(const httplib::Request& request, httplib::Response& response)
{
    // 1. 解析 HTTP 请求体, 提取请求 ID
    Json::Value request_json;
    std::string request_id;
    if (!ParseJsonBody(request, request_json, request_id))
    {
        SendEnvelopeResponse(response, "", kGatewayErrorCodeParams, "请求体 JSON 解析失败或缺少 requestId 字段");
        return;
    }

    // 2. 提取请求参数
    std::string session_id = request_json["sessionId"].asString();
    std::string chat_session_id = request_json["chatSessionId"].asString();
    std::string chat_type = request_json["chatType"].asString();
    std::string message = request_json["message"].asString();
    std::string file_id = request_json["fileId"].asString();
    std::string db_type = request_json["dbType"].asString();
    std::string db_connect_id = request_json["dbConnectId"].asString();
    std::string table_name = request_json["tableName"].asString();

    // 3. 参数校验, 逐项校验参数并返回对应的错误描述
    if (session_id.empty())
    {
        ERR("[A06] 请求参数错误, sessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : sessionId 不能为空");
        return;
    }
    else if (chat_session_id.empty())
    {
        ERR("[A06] 请求参数错误, chatSessionId 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : chatSessionId 不能为空");
        return;
    }
    else if (chat_type != "plain" && chat_type != "excel" && chat_type != "database")
    {
        ERR("[A06] 请求参数错误, chatType 无效: {}, requestId: {}", chat_type, request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : chatType 必须为 plain/excel/database");
        return;
    }
    else if (message.empty())
    {
        ERR("[A06] 请求参数错误, message 为空, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : message 不能为空");
        return;
    }
    else if (chat_type == "database" && db_connect_id.empty())
    {
        ERR("[A06] 请求参数错误, database 场景缺少 dbConnectId, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : database 场景 dbConnectId 不能为空");
        return;
    }
    else if (chat_type == "database" && table_name.empty())
    {
        ERR("[A06] 请求参数错误, database 场景缺少 tableName, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams, "请求参数错误 : database 场景 tableName 不能为空");
        return;
    }

    // 4. 会话鉴权, 鉴权失败时透传错误码与错误信息
    std::string user_id;
    int error_code = 0;
    std::string error_msg;
    if (!CheckSessionValid(request_id, session_id, user_id, error_code, error_msg))
    {
        SendEnvelopeResponse(response, request_id, error_code, error_msg);
        return;
    }

    // 5. 获取 AI 子服务信道并解析服务地址(host:port), 用于构建 HTTP 客户端
    cpp_toolkit::ChannelPtr ai_channel = GetServiceChannel(kAiServiceName);
    std::string ai_service_addr = (ai_channel == nullptr) ? "" : ParseChannelAddress(ai_channel);
    if (ai_service_addr.empty())
    {
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
        return;
    }
    INFO("[A06] 获取 AI 子服务地址成功, requestId: {}, aiServiceAddr: {}", request_id, ai_service_addr);

    // 6. dbType 区分大小写映射为 AI 子服务的数据库类型枚举名, 非法或缺省按 Excel 场景处理
    std::string db_type_name;
    if (db_type == "MYSQL")
    {
        db_type_name = "MYSQL";
    }
    else if (db_type == "SQLITE")
    {
        db_type_name = "SQLITE";
    }
    else
    {
        db_type_name = "EXCEL";
    }

    // 7. 构建转发给 AI 子服务的请求体, 字段名与 AI 子服务 proto 定义保持一致
    Json::Value ai_request_json;
    ai_request_json["request_id"] = request_id;
    ai_request_json["session_id"] = session_id;
    ai_request_json["user_id"] = user_id;
    ai_request_json["chat_session_id"] = chat_session_id;
    ai_request_json["chat_type"] = chat_type;
    ai_request_json["message"] = message;
    ai_request_json["file_id"] = file_id;
    ai_request_json["db_type"] = db_type_name;
    ai_request_json["db_connect_id"] = db_connect_id;
    ai_request_json["table_name"] = table_name;
    std::string request_body;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(ai_request_json, request_body))
    {
        ERR("[A06] 转发请求体序列化失败, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kHttpStatusInternalError, "服务器内部错误");
        return;
    }

    // 8. 设置 SSE 响应头, 响应头开启流式传输
    response.status = kHttpStatusOk;
    response.set_header("Cache-Control", "no-cache");
    response.set_header("Connection", "keep-alive");
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "*");

    // 9. 设置流式传输回调, 回调中构建 HTTP 客户端向 AI 子服务发起发送消息请求,
    //    AI 子服务推送纯文本块, 网关负责包装为 SSE 数据帧推送给前端
    response.set_chunked_content_provider("text/event-stream",
        [ai_service_addr, request_id, request_body](size_t /*offset*/, httplib::DataSink& data_sink) -> bool
        {
            // 9.1 解析 AI 子服务地址 "host:port"
            const size_t colon_pos = ai_service_addr.find(':');
            if (colon_pos == std::string::npos)
            {
                ERR("[A06] AI 子服务地址格式非法, aiServiceAddr: {}", ai_service_addr);
                SendSseFrame(data_sink, "", true, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
                data_sink.write(kSseDoneFrame, sizeof(kSseDoneFrame) - 1);
                data_sink.done();
                return false;
            }
            const std::string host = ai_service_addr.substr(0, colon_pos);
            const int port = std::stoi(ai_service_addr.substr(colon_pos + 1));

            // 9.2 创建 HTTP 客户端并设置超时时间, 禁用压缩传输并开启 TCP_NODELAY 降低传输延迟
            httplib::Client ai_client(host, port);
            ai_client.set_read_timeout(kAiHttpReadTimeoutSeconds, 0);
            ai_client.set_write_timeout(kAiHttpWriteTimeoutSeconds, 0);
            ai_client.set_connection_timeout(kAiHttpConnectTimeoutSeconds, 0);
            ai_client.set_tcp_nodelay(true);
            ai_client.set_decompress(false);

            // 9.3 构建 HTTP 请求, brpc 以"服务全名/方法名"匹配 HTTP 请求路径
            httplib::Request ai_request;
            ai_request.method = "POST";
            ai_request.path = kAiSendMessageHttpPath;
            ai_request.set_header("Content-Type", "application/json");
            ai_request.set_header("Accept", "text/event-stream");
            ai_request.body = request_body;

            // 9.4 设置响应头处理器回调 : 检测是否成功建立流式连接,
            //     AI 子服务流式响应的 Content-Type 为 text/event-stream,
            //     非 200 状态码或非流式响应(如业务错误 JSON 响应)均按连接失败处理
            bool connection_failed = false;
            ai_request.response_handler = [&connection_failed](const httplib::Response& ai_response) -> bool
            {
                if (ai_response.status != kHttpStatusOk ||
                    ai_response.get_header_value("Content-Type").find("text/event-stream") == std::string::npos)
                {
                    connection_failed = true;
                    return false;
                }
                return true;
            };

            // 9.5 设置内容接收器回调 : 将 AI 子服务返回的纯文本块包装为 SSE 数据帧,
            //     主动推送给前端
            ai_request.content_receiver = [&connection_failed, &data_sink](const char* data,
                                                                           size_t data_length,
                                                                           size_t /*offset*/,
                                                                           size_t /*total_length*/) -> bool
            {
                if (connection_failed)
                {
                    return false;
                }
                SendSseFrame(data_sink, std::string(data, data_length), false,
                             static_cast<int>(ErrorCode::SUCCESS), "");
                return true;
            };

            // 9.6 发送 HTTP 请求
            httplib::Result ai_result = ai_client.send(ai_request);
            if (!ai_result)
            {
                ERR("[A06] AI 子服务 HTTP 请求失败, requestId: {}, 错误信息: {}",
                    request_id, httplib::to_string(ai_result.error()));
                connection_failed = true;
            }

            // 9.7 发送结束帧 : 连接失败时发送错误帧, 成功时发送完成帧(错误码为 0);
            //     AI 子服务业务处理异常以文本块形式透传, 最终结束帧统一为成功状态
            if (connection_failed)
            {
                SendSseFrame(data_sink, "", true, kGatewayErrorCodeUnavailable, "AI 子服务不可用");
            }
            else
            {
                SendSseFrame(data_sink, "", true, static_cast<int>(ErrorCode::SUCCESS), "");
            }

            // 9.8 发送 SSE 流结束标记
            data_sink.write(kSseDoneFrame, sizeof(kSseDoneFrame) - 1);
            data_sink.done();
            return false;
        });

    INFO("[A06] 发送消息接口处理完成, requestId: {}, userId: {}", request_id, user_id);
}

} // namespace chat_excel
