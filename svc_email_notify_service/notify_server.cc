#include "notify_server.h"

#include <memory>
#include <string>
#include <utility>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include "notify_business.h"
#include "notify_service_impl.h"

namespace chat_excel
{
namespace notify_service
{

namespace
{

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
constexpr int kServerStopCloseWaitMs = 0;

} // namespace

NotifyServer::NotifyServer(std::shared_ptr<NotifyServiceImpl> notify_service_impl,
                           std::shared_ptr<brpc::Server> server,
                           cpp_toolkit::SvcProvider::Ptr svc_provider)
    : notify_service_impl_(std::move(notify_service_impl)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("通知子服务服务器对象初始化完成");
}

NotifyServer::~NotifyServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void NotifyServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("通知子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("通知子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("通知子服务服务器已停止");
}

void NotifyServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        return;
    }

    INFO("通知子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("通知子服务服务器停止完成");
}

NotifyServerBuilder& NotifyServerBuilder::SetMailSettings(
    std::shared_ptr<MailSettings> mail_settings)
{
    mail_settings_ = std::move(mail_settings);
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetWorkerThreadCount(int worker_thread_count)
{
    worker_thread_count_ = worker_thread_count;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetListenPort(int listen_port)
{
    listen_port_ = listen_port;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetEtcdAddress(const std::string& etcd_center_addr)
{
    etcd_center_addr_ = etcd_center_addr;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetServiceName(const std::string& service_name)
{
    service_name_ = service_name;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetServiceAddr(const std::string& service_addr)
{
    service_addr_ = service_addr;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::SetRegistryTtl(int registry_ttl)
{
    registry_ttl_ = registry_ttl;
    return *this;
}

std::shared_ptr<NotifyServer> NotifyServerBuilder::Build()
{
    // 1. 创建通知业务逻辑对象(内部完成 curl 全局资源初始化与邮箱发送器构建),
    //    业务对象由 RPC 接口实现对象间接持有保活
    std::shared_ptr<NotifyBusiness> notify_business =
        std::make_shared<NotifyBusiness>(*mail_settings_);
    INFO("通知业务逻辑对象构建完成");

    // 2. 设置并启动邮箱发送工作线程
    notify_business->SetWorkerThreadCount(worker_thread_count_);
    notify_business->StartEmailWorkers();
    INFO("邮箱发送工作线程已启动, 线程个数: {}", worker_thread_count_);

    // 3. 创建 RPC 接口实现对象(注入业务逻辑对象, 间接保活整个业务对象图)
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    notify_service_impl_ = std::make_shared<NotifyServiceImpl>(notify_business);
    INFO("RPC 接口实现对象构建完成");

    // 4. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(listen_port_, notify_service_impl_.get());
    if (server_ == nullptr)
    {
        ERR("brpc 服务器创建失败, 监听端口: {}", listen_port_);
        return nullptr;
    }
    INFO("brpc 服务器已启动, 监听端口: {}", listen_port_);

    // 5. 服务注册(SvcProvider + Registry, KeepAlive 自动续期; 须在 server 启动后注册)
    svc_provider_ = std::make_shared<cpp_toolkit::SvcProvider>(
        etcd_center_addr_, service_name_, service_addr_);
    if (!svc_provider_->Registry(registry_ttl_))
    {
        ERR("服务注册失败: {} -> {}", service_name_, service_addr_);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s", service_name_, service_addr_, registry_ttl_);

    // 6. 创建 NotifyServer 实例(持有三个核心对象, 保证 keepalive/impl 生命周期)
    std::shared_ptr<NotifyServer> notify_server = std::make_shared<NotifyServer>(
        notify_service_impl_, server_, svc_provider_);
    INFO("通知子服务服务器构建完成");
    return notify_server;
}

} // namespace notify_service
} // namespace chat_excel
