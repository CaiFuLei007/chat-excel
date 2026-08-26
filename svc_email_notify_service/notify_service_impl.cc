#include "notify_service_impl.h"

#include <string>
#include <utility>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace notify_service
{

namespace
{

/**
 * @brief 将错误码与错误码描述填充到 RPC 响应中
 * @param response RPC 响应对象
 * @param error_code 错误码
 */
template <typename ResponseType>
void SetErrorResponse(ResponseType* response, ErrorCode error_code)
{
    response->set_error_code(static_cast<int>(error_code));
    response->set_error_msg(ErrorMessage(error_code));
}

} // namespace

NotifyServiceImpl::NotifyServiceImpl(std::shared_ptr<NotifyBusiness> notify_business)
    : notify_business_(std::move(notify_business))
{
}

void NotifyServiceImpl::SendVerifyCode(google::protobuf::RpcController* /*controller*/,
                                      const proto::SendVerifyCodeRequest* request,
                                      proto::SendVerifyCodeResponse* response,
                                      google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 分别定位具体为空的参数并记录日志
        if (request->email().empty())
        {
            ERR("SendVerifyCode 接口请求参数错误, email 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::NOTIFY_VERIFYCODE_EMAIL_EMPTY);
            return;
        }
        else if (request->code().empty())
        {
            ERR("SendVerifyCode 接口请求参数错误, code 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::NOTIFY_VERIFYCODE_CODE_EMPTY);
            return;
        }
        // 调用业务逻辑层投递验证码邮件任务(异步发送, 入队成功即成功)
        notify_business_->SendVerifyCode(request->email(), request->code());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("SendVerifyCode 接口业务处理异常, email: {} , request_id: {} , 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("SendVerifyCode 接口非预期异常, email: {} , request_id: {} , 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::NOTIFY_SERVICE_INTERNAL_ERROR);
    }
}

void NotifyServiceImpl::SendEmail(google::protobuf::RpcController* /*controller*/,
                                  const proto::SendEmailRequest* request,
                                  proto::SendEmailResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 分别定位具体为空的参数并记录日志
        if (request->to_email().empty())
        {
            ERR("SendEmail 接口请求参数错误, to_email 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::NOTIFY_EMAIL_TO_EMPTY);
            return;
        }
        else if (request->subject().empty())
        {
            ERR("SendEmail 接口请求参数错误, subject 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::NOTIFY_EMAIL_SUBJECT_EMPTY);
            return;
        }
        else if (request->content().empty())
        {
            ERR("SendEmail 接口请求参数错误, content 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::NOTIFY_EMAIL_CONTENT_EMPTY);
            return;
        }

        // 调用业务逻辑层投递普通邮件任务(异步发送, 入队成功即成功)
        notify_business_->SendEmail(request->to_email(), request->subject(), request->content());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("SendEmail 接口业务处理异常, to_email: {} , request_id: {} , 错误信息: {}",
            request->to_email(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("SendEmail 接口非预期异常, to_email: {} , request_id: {} , 错误信息: {}",
            request->to_email(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::NOTIFY_SERVICE_INTERNAL_ERROR);
    }
}

} // namespace notify_service
} // namespace chat_excel
