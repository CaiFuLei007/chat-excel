#include "gateway_impl_service.h"
#include <ctime>
#include <memory>
#include <string>
#include <brpc/controller.h>
#include <httplib.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include <cpp-toolkit/util.h>
#include <jsoncpp/json/json.h>
#include "common/exception.h"
#include "user_service.pb.h"

namespace chat_excel
{

// proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace proto = ::chat_excel_proto::user_service;

namespace
{

// 用户子服务名称(与用户子服务注册到 ETCD 注册中心的服务名保持一致)
constexpr char kUserServiceName[] = "UserService";

// 用户子服务 RPC 调用超时时间(毫秒)
constexpr int kRpcTimeoutMilliseconds = 3000;

// HTTP 成功状态码
constexpr int kHttpStatusOk = 200;

// HTTP 服务器内部错误状态码
constexpr int kHttpStatusInternalError = 500;

// 网关错误码 : 请求参数错误
constexpr int kGatewayErrorCodeParams = 400;

// 网关错误码 : 后端服务不可用
constexpr int kGatewayErrorCodeUnavailable = 503;

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

bool GatewayServiceImpl::CheckSessionValid(const std::string& request_id, const std::string& session_id,
                                           int& error_code, std::string& error_msg)
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
    // 1. 解析 HTTP 请求体, 提取请求 ID、昵称、密码与邮箱
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
    if (nickname.empty() || password.empty() || email.empty())
    {
        ERR("[U03] 请求参数错误, nickname/password/email 存在空值, requestId: {}", request_id);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : nickname、password 与 email 均不能为空");
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
    if (!CheckSessionValid(request_id, session_id, error_code, error_msg))
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
    // 1. 解析 HTTP 请求 query 参数(本接口参数全部在 query 中, 请求体为空)
    std::string request_id = request.get_param_value("requestId");
    std::string session_id = request.get_param_value("sessionId");
    std::string user_id = request.get_param_value("userId");
    if (request_id.empty() || session_id.empty())
    {
        ERR("[U09] 请求参数错误, requestId 或 sessionId 为空, 请求路径: {}", request.path);
        SendEnvelopeResponse(response, request_id, kGatewayErrorCodeParams,
                             "请求参数错误 : requestId 与 sessionId 不能为空");
        return;
    }

    // 2. 鉴权, 检查会话是否有效, 失败时透传错误码与错误信息
    int error_code = 0;
    std::string error_msg;
    if (!CheckSessionValid(request_id, session_id, error_code, error_msg))
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

    // 4. 构建 RPC 请求(user_id 透传给后端, 由后端决定是否使用)
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

void GatewayServiceImpl::HandleFileUploadInfo(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F01] 上传文件信息接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileInfo(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F02] 获取文件信息接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileUpload(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F03] 上传文件数据接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileDownload(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F04] 下载文件接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileDelete(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F05] 删除文件接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFilePreview(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F06] 预览 Excel 文件接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileList(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F07] 获取用户文件列表接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileChatMap(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F08] 关联文件和聊天会话映射接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleFileSqliteUpload(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[F09] 上传 SQLite 文件接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleDbConnect(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[D01] 新建数据库连接接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleDbDisconnect(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[D02] 断开数据库连接接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleDbTables(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[D03] 获取数据库表列表接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleDbTableData(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[D04] 获取表数据接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleDbConnectionStatus(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[D05] 获取连接状态接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiModels(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A01] 获取支持模型列表接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiSessionCreate(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A02] 新建聊天会话接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiChatSessionLists(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A03] 获取聊天会话列表接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiHistory(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A04] 获取指定聊天会话历史消息接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiDelete(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A05] 删除指定聊天会话接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleAiSendStreamMessage(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[A06] 发送消息(流式, SSE)接口暂未实现, 请求路径: {}", request.path);
}

} // namespace chat_excel
