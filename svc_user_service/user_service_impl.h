#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "user_service.pb.h"
#include "svc_user_service/user_business.h"

namespace chat_excel
{
namespace user_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::user_service;

/**
 * @brief 用户子服务 RPC 接口实现类, 继承 protoc 生成的 UserService 服务基类,
 *        负责解析与校验 RPC 请求参数, 调用用户业务逻辑层完成业务处理,
 *        并将业务处理结果(错误码与错误信息)填充到 RPC 响应中;
 *        业务处理过程中抛出的异常统一按照业务处理失败的逻辑进行处理
 */
class UserServiceImpl : public proto::UserService
{
public:
    /**
     * @brief 构造函数, 注入用户业务逻辑对象
     * @param user_business 用户业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit UserServiceImpl(std::shared_ptr<UserBusiness> user_business);

    ~UserServiceImpl() override = default;

    /**
     * @brief 检测昵称是否唯一
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户昵称
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ValidNickname(google::protobuf::RpcController* controller,
                               const proto::ValidNicknameRequest* request,
                               proto::ValidNicknameResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 检测邮箱是否唯一
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户邮箱
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ValidEmail(google::protobuf::RpcController* controller,
                            const proto::ValidEmailRequest* request,
                            proto::ValidEmailResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 用户注册
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户昵称、密码与邮箱
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void UserRegister(google::protobuf::RpcController* controller,
                              const proto::UserRegisterRequest* request,
                              proto::UserRegisterResponse* response,
                              google::protobuf::Closure* done) override;

    /**
     * @brief 会话登录, 通过已有会话恢复登录态
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带会话 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void SessionLogin(google::protobuf::RpcController* controller,
                              const proto::SessionLoginRequest* request,
                              proto::SessionLoginResponse* response,
                              google::protobuf::Closure* done) override;

    /**
     * @brief 密码登录, 用户名可以是用户昵称或用户邮箱
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户名与密码
     * @param response RPC 响应, 携带错误码、错误信息与登录结果(会话 ID)
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void PasswdLogin(google::protobuf::RpcController* controller,
                             const proto::PasswdLoginRequest* request,
                             proto::PasswdLoginResponse* response,
                             google::protobuf::Closure* done) override;

    /**
     * @brief 获取验证码, 生成验证码并存储到缓存中
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户邮箱
     * @param response RPC 响应, 携带错误码、错误信息与获取验证码结果(验证码 ID)
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetCode(google::protobuf::RpcController* controller,
                         const proto::GetCodeRequest* request,
                         proto::GetCodeResponse* response,
                         google::protobuf::Closure* done) override;

    /**
     * @brief 邮箱验证码登录
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户邮箱、验证码与验证码 ID
     * @param response RPC 响应, 携带错误码、错误信息与登录结果(会话 ID)
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void VcodeLogin(google::protobuf::RpcController* controller,
                            const proto::VcodeLoginRequest* request,
                            proto::VcodeLoginResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 退出登录, 删除会话并将用户状态设置为下线
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带会话 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void Logout(google::protobuf::RpcController* controller,
                        const proto::LogoutRequest* request,
                        proto::LogoutResponse* response,
                        google::protobuf::Closure* done) override;

    /**
     * @brief 获取用户信息, 通过会话 ID 获取对应用户的信息
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带会话 ID
     * @param response RPC 响应, 携带错误码、错误信息与用户信息结果
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetUserInfo(google::protobuf::RpcController* controller,
                             const proto::GetUserInfoRequest* request,
                             proto::GetUserInfoResponse* response,
                             google::protobuf::Closure* done) override;

private:
    // 用户业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<UserBusiness> user_business_;
};

} // namespace user_service
} // namespace chat_excel
