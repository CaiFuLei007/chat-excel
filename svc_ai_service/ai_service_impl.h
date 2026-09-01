#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "ai_service.pb.h"
#include "svc_ai_service/ai_business.h"

namespace chat_excel
{
namespace ai_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::ai_service;

/**
 * @brief AI 子服务 RPC 接口实现类, 继承 protoc 生成的 AIService 服务基类,
 *        负责解析与校验 RPC 请求参数, 调用 AI 业务逻辑层完成业务处理,
 *        并将业务处理结果(错误码与错误信息)填充到 RPC 响应中;
 *        业务处理过程中抛出的异常统一按照业务处理失败的逻辑进行处理
 */
class AiServiceImpl : public proto::AIService
{
public:
    /**
     * @brief 构造函数, 注入 AI 业务逻辑对象
     * @param ai_business AI 业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit AiServiceImpl(std::shared_ptr<AiBusiness> ai_business);

    ~AiServiceImpl() override = default;

    /**
     * @brief 获取可用的模型列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID
     * @param response RPC 响应, 携带错误码、错误信息与模型列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetModels(google::protobuf::RpcController* controller,
                           const proto::GetModelsRequest* request,
                           proto::GetModelsResponse* response,
                           google::protobuf::Closure* done) override;

    /**
     * @brief 新建聊天会话
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户 ID、模型名称、会话类型与数据库连接信息
     * @param response RPC 响应, 携带错误码、错误信息与新建会话数据
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void CreateSession(google::protobuf::RpcController* controller,
                               const proto::CreateChatSessionRequest* request,
                               proto::CreateChatSessionResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 获取指定用户的聊天会话列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户 ID
     * @param response RPC 响应, 携带错误码、错误信息与会话列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetSessions(google::protobuf::RpcController* controller,
                             const proto::GetSessionsRequest* request,
                             proto::GetSessionsResponse* response,
                             google::protobuf::Closure* done) override;

    /**
     * @brief 获取指定会话的历史消息
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户 ID 与聊天会话 ID
     * @param response RPC 响应, 携带错误码、错误信息与会话元数据及历史消息列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetSessionHistory(google::protobuf::RpcController* controller,
                                   const proto::GetSessionHistoryRequest* request,
                                   proto::GetSessionHistoryResponse* response,
                                   google::protobuf::Closure* done) override;

    /**
     * @brief 删除指定会话
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户 ID 与聊天会话 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DeleteSession(google::protobuf::RpcController* controller,
                               const proto::DeleteSessionRequest* request,
                               proto::DeleteSessionResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 更新会话文件关联
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带用户 ID、聊天会话 ID 与文件 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void UpdateSessionFile(google::protobuf::RpcController* controller,
                                   const proto::UpdateSessionFileRequest* request,
                                   proto::UpdateSessionFileResponse* response,
                                   google::protobuf::Closure* done) override;

private:
    // AI 业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<AiBusiness> ai_business_;
};

} // namespace ai_service
} // namespace chat_excel
