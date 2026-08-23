#include "gateway_impl_service.h"
#include <ctime>
#include <memory>
#include <string>
#include <httplib.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include <cpp-toolkit/util.h>
#include <jsoncpp/json/json.h>

namespace chat_excel
{

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

void GatewayServiceImpl::HandleUserNicknameValid(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U01] 检测用户昵称是否唯一接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserEmailValid(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U02] 检测邮箱是否唯一接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserRegister(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U03] 用户注册接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserPasswdLogin(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U04] 密码登录接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserCode(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U05] 获取验证码接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserVcodeLogin(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U06] 验证码登录接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserSessionLogin(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U07] 会话登录接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserLogout(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U08] 退出登录接口暂未实现, 请求路径: {}", request.path);
}

void GatewayServiceImpl::HandleUserInfo(const httplib::Request& request, httplib::Response&)
{
    // 接口暂未实现, 仅记录调用日志, 后续版本补充业务逻辑
    INFO("[U09] 获取用户信息接口暂未实现, 请求路径: {}", request.path);
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
