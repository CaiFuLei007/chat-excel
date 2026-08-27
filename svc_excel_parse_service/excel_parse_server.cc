#include "excel_parse_server.h"

#include <memory>
#include <string>
#include <utility>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include "excel_parse_business.h"
#include "excel_parse_service_impl.h"

namespace chat_excel
{
namespace excel_parse_service
{

namespace
{

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
constexpr int kServerStopCloseWaitMs = 0;

} // namespace

ExcelParseServer::ExcelParseServer(
    std::shared_ptr<ExcelParseServiceImpl> excel_parse_service_impl,
    std::shared_ptr<brpc::Server> server,
    cpp_toolkit::SvcProvider::Ptr svc_provider)
    : excel_parse_service_impl_(std::move(excel_parse_service_impl)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("Excel 解析子服务服务器对象初始化完成");
}

ExcelParseServer::~ExcelParseServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void ExcelParseServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("Excel 解析子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("Excel 解析子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("Excel 解析子服务服务器已停止");
}

void ExcelParseServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        return;
    }

    INFO("Excel 解析子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("Excel 解析子服务服务器停止完成");
}

ExcelParseServerBuilder& ExcelParseServerBuilder::SetEtcdAddress(
    const std::string& etcd_center_addr)
{
    etcd_center_addr_ = etcd_center_addr;
    return *this;
}

ExcelParseServerBuilder& ExcelParseServerBuilder::SetBrpcSettings(
    const BrpcSettings& brpc_settings)
{
    brpc_settings_ = brpc_settings;
    return *this;
}

ExcelParseServerBuilder& ExcelParseServerBuilder::SetRegistryTtl(int registry_ttl)
{
    registry_ttl_ = registry_ttl;
    return *this;
}

std::shared_ptr<ExcelParseServer> ExcelParseServerBuilder::Build()
{
    // 1. 创建 Excel 解析业务逻辑对象(内部构建 Excel 解析器实例),
    //    业务对象由 RPC 接口实现对象间接持有保活
    std::shared_ptr<ExcelParseBusiness> excel_parse_business =
        std::make_shared<ExcelParseBusiness>();
    INFO("Excel 解析业务逻辑对象构建完成");

    // 2. 创建 RPC 接口实现对象(注入业务逻辑对象, 间接保活整个业务对象图)
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    excel_parse_service_impl_ = std::make_shared<ExcelParseServiceImpl>(excel_parse_business);
    INFO("RPC 接口实现对象构建完成");

    // 3. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(brpc_settings_.listen_port,
                                                       excel_parse_service_impl_.get());
    if (server_ == nullptr)
    {
        ERR("brpc 服务器创建失败, 监听端口: {}", brpc_settings_.listen_port);
        return nullptr;
    }
    INFO("brpc 服务器已启动, 监听端口: {}", brpc_settings_.listen_port);

    // 4. 服务注册(SvcProvider + Registry, KeepAlive 自动续期; 须在 server 启动后注册)
    svc_provider_ = std::make_shared<cpp_toolkit::SvcProvider>(
        etcd_center_addr_, brpc_settings_.service_name, brpc_settings_.service_addr);
    if (!svc_provider_->Registry(registry_ttl_))
    {
        ERR("服务注册失败: {} -> {}", brpc_settings_.service_name, brpc_settings_.service_addr);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s",
         brpc_settings_.service_name, brpc_settings_.service_addr, registry_ttl_);

    // 5. 创建 ExcelParseServer 实例(持有三个核心对象, 保证 keepalive/impl 生命周期)
    std::shared_ptr<ExcelParseServer> excel_parse_server = std::make_shared<ExcelParseServer>(
        excel_parse_service_impl_, server_, svc_provider_);
    INFO("Excel 解析子服务服务器构建完成");
    return excel_parse_server;
}

} // namespace excel_parse_service
} // namespace chat_excel
