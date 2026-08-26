#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "notify_service.pb.h"
#include "notify_business.h"

namespace chat_excel
{
namespace notify_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::notify_service;

/**
 * @brief 通知子服务 RPC 接口实现类, 继承 protoc 生成的 NotifyService 服务基类,
 *        负责解析与校验 RPC 请求参数, 调用通知业务逻辑层将邮件任务异步投递,
 *        并将业务处理结果(错误码与错误信息)填充到 RPC 响应中;
 *        邮件任务入队成功即视为本次 RPC 成功(异步语义, 实际发送结果仅记录日志);
 *        业务处理过程中抛出的异常统一按照业务处理失败的逻辑进行处理
 */
class NotifyServiceImpl : public proto::NotifyService
{
public:
    /**
     * @brief 构造函数, 注入通知业务逻辑对象
     * @param notify_business 通知业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit NotifyServiceImpl(std::shared_ptr<NotifyBusiness> notify_business);

    ~NotifyServiceImpl() override = default;

    /**
     * @brief 发送验证码邮件, 将验证码邮件任务异步投递到工作线程队列
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、收件人邮箱与验证码
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void SendVerifyCode(google::protobuf::RpcController* controller,
                                const proto::SendVerifyCodeRequest* request,
                                proto::SendVerifyCodeResponse* response,
                                google::protobuf::Closure* done) override;

    /**
     * @brief 发送普通邮件, 将普通邮件任务异步投递到工作线程队列
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、收件人邮箱、邮件主题与邮件内容
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void SendEmail(google::protobuf::RpcController* controller,
                          const proto::SendEmailRequest* request,
                          proto::SendEmailResponse* response,
                          google::protobuf::Closure* done) override;

private:
    // 通知业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<NotifyBusiness> notify_business_;
};

} // namespace notify_service
} // namespace chat_excel
