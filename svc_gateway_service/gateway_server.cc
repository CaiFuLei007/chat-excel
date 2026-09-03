#include "gateway_server.h"
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <httplib.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
#include "gateway_impl_service.h"

namespace chat_excel
{

// HTTP 服务器读写超时时间(秒), 5 分钟
static constexpr int kHttpReadWriteTimeoutSeconds = 300;

GatewayServer::GatewayServer(std::shared_ptr<httplib::Server> http_server, cpp_toolkit::SvcWatcher::Ptr svc_watcher, const std::string& host, int port)
    : http_server_(http_server),
      port_(port),
      is_running_(false),
      svc_watcher_(svc_watcher)
{
    // host 参数可能为 host:port 形式(统一 server_addr 命名), 监听时仅需主机部分, 端口单独由 port_ 提供
    std::size_t colon_position = host.rfind(':');
    host_ = (colon_position == std::string::npos) ? host : host.substr(0, colon_position);

    INFO("网关服务器对象初始化完成, 监听地址: {} , 端口: {}", host_, port_);
}

GatewayServer::~GatewayServer()
{
    // 对象销毁时自动停止服务器, 回收线程资源
    Stop();
}

bool GatewayServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("网关服务器已在运行中, 忽略重复启动请求");
        return true;
    }

    INFO("网关服务器启动中, 监听地址: {} , 端口: {}", host_, port_);

    // 先标记运行状态, 避免监听失败时状态标志被误置回运行中
    is_running_.store(true);

    // 服务器在新线程中启动, listen 阻塞监听, 防止阻塞主线程
    server_thread_ = std::thread([this]()
    {
        if (!http_server_->listen(host_.c_str(), port_))
        {
            // 监听失败(端口被占用等), 更新运行状态并记录错误日志
            is_running_.store(false);
            ERR("网关服务器监听失败, 监听地址: {} , 端口: {}", host_, port_);
            return;
        }
    });

    INFO("网关服务器启动成功");
    return true;
}

void GatewayServer::Stop()
{
    // 检测服务器是否在运行, 未运行则直接返回(仍需回收可能残留的监听线程资源)
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        // 服务器可能启动失败后线程尚未回收, 确保线程资源被回收
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
        WARN("网关服务器未在运行, 忽略停止请求");
        return;
    }

    INFO("网关服务器停止中, 监听地址: {} , 端口: {}", host_, port_);

    // 停止 HTTP 服务器, 使监听线程中的 listen 调用返回
    http_server_->stop();

    // 等待监听线程退出, 回收线程资源
    if (server_thread_.joinable())
    {
        server_thread_.join();
    }

    INFO("网关服务器已停止");
}

bool GatewayServer::IsRunning() const
{
    return is_running_.load();
}

GatewayServerBuilder& GatewayServerBuilder::BuildChannelManager(const std::vector<std::string>& care_service_names)
{
    // 构建服务信道管理对象, 并设置需要监控的服务
    channel_manager_ = std::make_shared<cpp_toolkit::ChannelManager>();
    channel_manager_->SetCareService(care_service_names);
    INFO("服务信道管理对象构建完成, 监控服务数量: {}", care_service_names.size());
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BuildServiceDiscovery(const std::string& etcd_center_addr)
{
    // 服务发现回调以 shared_ptr 值捕获信道管理对象, 保证监控线程生命周期内信道管理对象有效
    // 信道管理对象内部会自动忽略非关心服务, 回调中无需单独判断
    cpp_toolkit::ChannelManager::Ptr channel_manager = channel_manager_;
    auto online_callback = [channel_manager](const std::string& service_name, const std::string& service_addr)
    {
        // 服务上线, 添加服务节点信道
        channel_manager->AddService(service_name, service_addr);
        INFO("服务上线: {} -> {}", service_name, service_addr);
    };
    auto offline_callback = [channel_manager](const std::string& service_name, const std::string& service_addr)
    {
        // 服务下线, 移除服务节点信道
        channel_manager->DelService(service_name, service_addr);
        INFO("服务下线: {} -> {}", service_name, service_addr);
    };

    // 构造服务发现(监控)对象
    svc_watcher_ = std::make_shared<cpp_toolkit::SvcWatcher>(
        etcd_center_addr, std::move(online_callback), std::move(offline_callback));
    INFO("服务发现对象构建完成, ETCD 地址: {}", etcd_center_addr);
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::StartServiceWatch()
{
    // Watch 调用阻塞直至服务停止, 必须在独立线程中运行; 监控线程以 shared_ptr 值捕获监控对象保活
    cpp_toolkit::SvcWatcher::Ptr svc_watcher = svc_watcher_;
    std::thread watch_thread([svc_watcher]()
    {
        // 以 "/" 为前缀监控全部子服务的上线、下线事件
        if (!svc_watcher->Watch("/"))
        {
            ERR("服务监控启动失败");
            return;
        }
        INFO("服务监控启动成功, 开始监控子服务上线、下线事件");
    });
    watch_thread.detach();
    INFO("服务监控线程已启动");
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BuildHttpServiceImpl()
{
    // 构建 HTTP 接口定义对象, 传入信道管理对象供后续接口实现时获取 RPC 信道
    gateway_service_impl_ = std::make_shared<GatewayServiceImpl>(channel_manager_);
    INFO("HTTP 接口定义对象构建完成");
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BuildHttpServer()
{
    // 构建 cpp-httplib 的 HTTP 服务器对象, 并设置读写超时时间为 5 分钟
    http_server_ = std::make_shared<httplib::Server>();
    http_server_->set_read_timeout(kHttpReadWriteTimeoutSeconds, 0);
    http_server_->set_write_timeout(kHttpReadWriteTimeoutSeconds, 0);
    INFO("HTTP 服务器对象构建完成, 读写超时时间: {} 秒", kHttpReadWriteTimeoutSeconds);
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BindRoutes()
{
    // 绑定路由, 将全部 30 个 HTTP 接口绑定到 HTTP 服务器对象上
    gateway_service_impl_->BindRoutes(*http_server_);
    INFO("HTTP 路由绑定完成, 共绑定 30 个接口");
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BuildStaticFiles(const std::string& www_root)
{
    // 空字符串表示不托管静态文件, 保持纯 API 网关模式
    if (www_root.empty())
    {
        INFO("未配置前端静态文件目录, 网关以纯 API 模式运行");
        return *this;
    }

    // 挂载前端静态文件目录到站点根路径, 目录不存在时挂载失败仅记录警告, 不阻断启动
    if (!http_server_->set_mount_point("/", www_root))
    {
        WARN("前端静态文件目录挂载失败, 目录: {} , 请检查路径是否存在", www_root);
        return *this;
    }
    INFO("前端静态文件目录挂载完成, 目录: {}", www_root);
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::BuildGatewayServer(const std::string& host, int port)
{
    // 构建网关服务器对象
    gateway_server_ = std::make_shared<GatewayServer>(http_server_, svc_watcher_, host, port);
    return *this;
}

std::shared_ptr<GatewayServer> GatewayServerBuilder::StartServer()
{
    // 启动服务器
    if (!gateway_server_->Start())
    {
        ERR("网关服务器启动失败, 构建器构建失败");
        return nullptr;
    }

    // 返回网关服务器对象
    INFO("网关服务器构建器构建完成");
    return gateway_server_;
}

} // namespace chat_excel
