#include "user_service_impl.h"

#include <string>
#include <utility>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace user_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::user_service;

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

UserServiceImpl::UserServiceImpl(std::shared_ptr<UserBusiness> user_business)
    : user_business_(std::move(user_business))
{
}

void UserServiceImpl::ValidNickname(google::protobuf::RpcController* /*controller*/,
                                    const proto::ValidNicknameRequest* request,
                                    proto::ValidNicknameResponse* response,
                                    google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 昵称不能为空
        if (request->nickname().empty())
        {
            ERR("ValidNickname 接口请求参数错误, nickname 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层检测昵称是否唯一
        if (!user_business_->CheckNicknameUnique(request->nickname()))
        {
            ERR("昵称已存在, nickname: {}, request_id: {}", request->nickname(), request->request_id());
            SetErrorResponse(response, ErrorCode::USER_NICKNAME_EXISTS);
            return;
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ValidNickname 接口业务处理异常, nickname: {}, request_id: {}, 错误信息: {}",
            request->nickname(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ValidNickname 接口非预期异常, nickname: {}, request_id: {}, 错误信息: {}",
            request->nickname(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::ValidEmail(google::protobuf::RpcController* /*controller*/,
                                 const proto::ValidEmailRequest* request,
                                 proto::ValidEmailResponse* response,
                                 google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 邮箱不能为空
        if (request->email().empty())
        {
            ERR("ValidEmail 接口请求参数错误, email 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层检测邮箱是否唯一
        if (!user_business_->CheckEmailUnique(request->email()))
        {
            ERR("邮箱已存在, email: {}, request_id: {}", request->email(), request->request_id());
            SetErrorResponse(response, ErrorCode::USER_EMAIL_EXISTS);
            return;
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ValidEmail 接口业务处理异常, email: {}, request_id: {}, 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ValidEmail 接口非预期异常, email: {}, request_id: {}, 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::UserRegister(google::protobuf::RpcController* /*controller*/,
                                   const proto::UserRegisterRequest* request,
                                   proto::UserRegisterResponse* response,
                                   google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 昵称、密码、邮箱、验证码与验证码 ID 均不能为空
        if (request->nickname().empty() || request->password().empty() || request->email().empty() ||
            request->verify_code().empty() || request->code_id().empty())
        {
            ERR("UserRegister 接口请求参数错误, nickname: {}, email: {}, request_id: {}",
                request->nickname(), request->email(), request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层完成用户注册, 注册失败时业务逻辑层抛出异常
        user_business_->UserRegister(request->nickname(), request->password(), request->email(),
                                     request->code_id(), request->verify_code());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("UserRegister 接口业务处理异常, nickname: {}, email: {}, request_id: {}, 错误信息: {}",
            request->nickname(), request->email(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("UserRegister 接口非预期异常, nickname: {}, email: {}, request_id: {}, 错误信息: {}",
            request->nickname(), request->email(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::SessionLogin(google::protobuf::RpcController* /*controller*/,
                                   const proto::SessionLoginRequest* request,
                                   proto::SessionLoginResponse* response,
                                   google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID 不能为空
        if (request->session_id().empty())
        {
            ERR("SessionLogin 接口请求参数错误, session_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层完成会话登录, 会话不存在时业务逻辑层抛出异常
        user_business_->SessionLogin(request->session_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("SessionLogin 接口业务处理异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("SessionLogin 接口非预期异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::PasswdLogin(google::protobuf::RpcController* /*controller*/,
                                  const proto::PasswdLoginRequest* request,
                                  proto::PasswdLoginResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户名与密码均不能为空
        if (request->username().empty() || request->password().empty())
        {
            ERR("PasswdLogin 接口请求参数错误, username: {}, request_id: {}",
                request->username(), request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层完成密码登录, 登录失败时业务逻辑层抛出异常
        const std::string session_id = user_business_->PasswdLogin(request->username(), request->password());

        // 将登录结果填充到响应中
        response->mutable_result()->set_session_id(session_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("PasswdLogin 接口业务处理异常, username: {}, request_id: {}, 错误信息: {}",
            request->username(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("PasswdLogin 接口非预期异常, username: {}, request_id: {}, 错误信息: {}",
            request->username(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::GetCode(google::protobuf::RpcController* /*controller*/,
                              const proto::GetCodeRequest* request,
                              proto::GetCodeResponse* response,
                              google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 邮箱不能为空
        if (request->email().empty())
        {
            ERR("GetCode 接口请求参数错误, email 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层生成验证码, 生成失败时业务逻辑层抛出异常
        const std::string code_id = user_business_->GetVerifyCode(request->email());

        // 将获取验证码结果填充到响应中
        response->mutable_result()->set_code_id(code_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetCode 接口业务处理异常, email: {}, request_id: {}, 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetCode 接口非预期异常, email: {}, request_id: {}, 错误信息: {}",
            request->email(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::VcodeLogin(google::protobuf::RpcController* /*controller*/,
                                 const proto::VcodeLoginRequest* request,
                                 proto::VcodeLoginResponse* response,
                                 google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 邮箱、验证码与验证码 ID 均不能为空
        if (request->email().empty() || request->verify_code().empty() || request->code_id().empty())
        {
            ERR("VcodeLogin 接口请求参数错误, email: {}, code_id: {}, request_id: {}",
                request->email(), request->code_id(), request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层完成邮箱验证码登录, 登录失败时业务逻辑层抛出异常
        const std::string session_id =
            user_business_->VcodeLogin(request->code_id(), request->verify_code(), request->email());

        // 将登录结果填充到响应中
        response->mutable_result()->set_session_id(session_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("VcodeLogin 接口业务处理异常, email: {}, code_id: {}, request_id: {}, 错误信息: {}",
            request->email(), request->code_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("VcodeLogin 接口非预期异常, email: {}, code_id: {}, request_id: {}, 错误信息: {}",
            request->email(), request->code_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::DeleteVerifyCode(google::protobuf::RpcController* /*controller*/,
                                       const proto::DeleteVerifyCodeRequest* request,
                                       proto::DeleteVerifyCodeResponse* response,
                                       google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 验证码 ID 不能为空
        if (request->code_id().empty())
        {
            ERR("DeleteVerifyCode 接口请求参数错误, code_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层删除验证码, 删除失败时业务逻辑层抛出异常
        user_business_->DeleteVerifyCode(request->code_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DeleteVerifyCode 接口业务处理异常, code_id: {}, request_id: {}, 错误信息: {}",
            request->code_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DeleteVerifyCode 接口非预期异常, code_id: {}, request_id: {}, 错误信息: {}",
            request->code_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::Logout(google::protobuf::RpcController* /*controller*/,
                             const proto::LogoutRequest* request,
                             proto::LogoutResponse* response,
                             google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID 不能为空
        if (request->session_id().empty())
        {
            ERR("Logout 接口请求参数错误, session_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层完成退出登录, 会话不存在时业务逻辑层抛出异常
        user_business_->Logout(request->session_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("Logout 接口业务处理异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("Logout 接口非预期异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::ValidSession(google::protobuf::RpcController* /*controller*/,
                                   const proto::ValidSessionRequest* request,
                                   proto::ValidSessionResponse* response,
                                   google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID 不能为空
        if (request->session_id().empty())
        {
            ERR("ValidSession 接口请求参数错误, session_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层检查会话是否有效, 会话不存在或用户未上线时会话无效
        std::string user_id;
        if (!user_business_->CheckSessionValid(request->session_id(), user_id))
        {
            ERR("会话不存在或已失效, session_id: {}, request_id: {}", request->session_id(), request->request_id());
            SetErrorResponse(response, ErrorCode::SESSION_NOT_FOUND);
            return;
        }

        // 将会话所属的用户 ID 填充到响应中
        response->set_user_id(user_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ValidSession 接口业务处理异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ValidSession 接口非预期异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

void UserServiceImpl::GetUserInfo(google::protobuf::RpcController* /*controller*/,
                                  const proto::GetUserInfoRequest* request,
                                  proto::GetUserInfoResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID 不能为空(用户信息通过会话 ID 获取)
        if (request->session_id().empty())
        {
            ERR("GetUserInfo 接口请求参数错误, session_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::USER_SERVICE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层获取用户信息, 会话不存在时业务逻辑层抛出异常
        const UserInfo user_info = user_business_->GetUserInfo(request->session_id());

        // 将用户信息填充到响应结果中, proto 的 UserInfo 消息需要使用命名空间别名限定
        proto::UserInfo* result_user_info = response->mutable_result()->mutable_user_info();
        result_user_info->set_user_id(user_info.user_id);
        result_user_info->set_nickname(user_info.nickname);
        result_user_info->set_email(user_info.email);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetUserInfo 接口业务处理异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetUserInfo 接口非预期异常, session_id: {}, request_id: {}, 错误信息: {}",
            request->session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::USER_SERVICE_INTERNAL_ERROR);
    }
}

} // namespace user_service
} // namespace chat_excel
