#include "svc_database_service/database_server.h"

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/rpc.h>
// fdfs.h 依赖的 fastcommon/common_define.h 定义了 byte 宏(signed char),
// 会污染标准库与第三方库头文件, 因此必须放在所有头文件之后导入,
// 导入后立即取消定义, 避免污染本文件后续代码
#include <cpp-toolkit/fdfs.h>
#undef byte
#include "svc_database_service/connection_manager.h"
#include "svc_database_service/database_business.h"
#include "svc_database_service/database_service_impl.h"
#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{
namespace database_service
{

namespace
{

// 服务监控键前缀, "/" 表示监控全部子服务的上线/下线事件
constexpr char kWatchKeyPrefix[] = "/";

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
constexpr int kServerStopCloseWaitMs = 0;

} // namespace

DatabaseServer::DatabaseServer(std::shared_ptr<DatabaseServiceImpl> database_service_impl,
                               cpp_toolkit::SvcWatcher::Ptr svc_watcher,
                               std::shared_ptr<brpc::Server> server,
                               cpp_toolkit::SvcProvider::Ptr svc_provider)
    : database_service_impl_(std::move(database_service_impl)),
      svc_watcher_(std::move(svc_watcher)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("数据库子服务服务器对象初始化完成");
}

DatabaseServer::~DatabaseServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void DatabaseServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("数据库子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("数据库子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("数据库子服务服务器已停止");
}

void DatabaseServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        WARN("数据库子服务服务器未在运行, 忽略停止请求");
        return;
    }

    INFO("数据库子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("数据库子服务服务器停止完成");
}

DatabaseServerBuilder& DatabaseServerBuilder::SetRpcSettings(const RpcSettings& rpc_settings)
{
    rpc_settings_ = rpc_settings;
    return *this;
}

DatabaseServerBuilder& DatabaseServerBuilder::SetEtcdSettings(const EtcdSettings& etcd_settings)
{
    etcd_settings_ = etcd_settings;
    return *this;
}

DatabaseServerBuilder& DatabaseServerBuilder::SetMysqlSettings(
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings)
{
    mysql_settings_ = std::move(mysql_settings);
    return *this;
}

DatabaseServerBuilder& DatabaseServerBuilder::SetFdfsSettings(
    const FdfsSettings& fdfs_settings)
{
    fdfs_settings_ = fdfs_settings;
    return *this;
}

DatabaseServerBuilder& DatabaseServerBuilder::SetCareServiceNames(
    const std::vector<std::string>& care_service_names)
{
    care_service_names_ = care_service_names;
    return *this;
}

std::shared_ptr<DatabaseServer> DatabaseServerBuilder::Build()
{
    // 1. 初始化 FastDFS 客户端(SQLite 连接业务层下载文件依赖),
    //    未初始化时 tracker 连接获取会因空指针导致进程崩溃
    cpp_toolkit::FdfsSettings fdfs_settings;
    fdfs_settings.tracker_servers_ = fdfs_settings_.tracker_servers;
    if (!cpp_toolkit::FdfsClient::Init(fdfs_settings))
    {
        ERR("FastDFS 客户端初始化失败, tracker 服务器个数: {}",
            fdfs_settings_.tracker_servers.size());
        return nullptr;
    }
    INFO("FastDFS 客户端初始化完成, tracker 服务器个数: {}",
         fdfs_settings_.tracker_servers.size());

    // 2. 创建服务信道管理对象, 设置需要监控的子服务
    channel_manager_ = std::make_shared<cpp_toolkit::ChannelManager>();
    channel_manager_->SetCareService(care_service_names_);
    INFO("服务信道管理对象构建完成, 监控服务数量: {}", care_service_names_.size());

    // 3. 创建数据库连接管理器单例(内部创建 excel_connection 全局连接并启动过期连接清理),
    //    单例生命周期由静态局部变量管理, 使用空删除器包装为共享指针供业务逻辑层持有;
    //    全局连接创建失败(配置无效或 MySQL 不可达)时构建失败;
    //    cpp_toolkit::MySQLSettings 仅含连接参数, 此处转换为驱动层 MySQLConfig(不使用 SSL)
    std::shared_ptr<DataBaseConnectionManager> connection_manager;
    try
    {
        auto mysql_config = std::make_shared<MySQLConfig>(
            mysql_settings_->host, mysql_settings_->port, mysql_settings_->user,
            mysql_settings_->password, mysql_settings_->database, false, SslConfig(),
            std::unordered_map<std::string, std::string>{});
        connection_manager = std::shared_ptr<DataBaseConnectionManager>(
            &DataBaseConnectionManager::GetInstance(mysql_config), [](DataBaseConnectionManager*) {});
    }
    catch (const std::exception& e)
    {
        ERR("数据库连接管理器创建失败, 错误: {}", e.what());
        return nullptr;
    }
    INFO("数据库连接管理器构建完成");

    // 4. 创建数据库业务逻辑对象(注入连接管理器与信道管理, 内部通过信道管理调用文件子服务下载 SQLite 文件)
    std::shared_ptr<DatabaseBusiness> database_business =
        std::make_shared<DatabaseBusiness>(connection_manager, channel_manager_);
    INFO("数据库业务逻辑对象构建完成");

    // 5. 创建 RPC 接口实现对象
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    database_service_impl_ = std::make_shared<DatabaseServiceImpl>(database_business);
    INFO("RPC 接口实现对象构建完成");

    // 6. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(rpc_settings_.listen_port,
                                                       database_service_impl_.get());
    if (server_ == nullptr)
    {
        ERR("brpc 服务器创建失败, 监听端口: {}", rpc_settings_.listen_port);
        return nullptr;
    }
    INFO("brpc 服务器已启动, 监听端口: {}", rpc_settings_.listen_port);

    // 7. 创建服务发现(监控)对象, 定义上线/下线回调
    //    回调以 shared_ptr 值捕获信道管理对象, 保证监控线程生命周期内信道管理对象有效
    cpp_toolkit::ChannelManager::Ptr channel_manager = channel_manager_;
    auto online_callback = [channel_manager](const std::string& service_name,
                                             const std::string& service_addr)
    {
        // 服务上线, 添加服务节点信道
        channel_manager->AddService(service_name, service_addr);
        INFO("服务上线: {} -> {}", service_name, service_addr);
    };
    auto offline_callback = [channel_manager](const std::string& service_name,
                                              const std::string& service_addr)
    {
        // 服务下线, 移除服务节点信道
        channel_manager->DelService(service_name, service_addr);
        INFO("服务下线: {} -> {}", service_name, service_addr);
    };
    svc_watcher_ = std::make_shared<cpp_toolkit::SvcWatcher>(
        etcd_settings_.etcd_center_addr, std::move(online_callback), std::move(offline_callback));
    INFO("服务发现对象构建完成, ETCD 地址: {}", etcd_settings_.etcd_center_addr);

    // 8. 独立线程启动服务监控(Watch 阻塞直至服务停止, detach; 值捕获 svc_watcher 保活)
    cpp_toolkit::SvcWatcher::Ptr svc_watcher = svc_watcher_;
    std::thread watch_thread([svc_watcher]()
    {
        // 以 "/" 为前缀监控全部子服务的上线、下线事件
        if (!svc_watcher->Watch(kWatchKeyPrefix))
        {
            ERR("服务监控启动失败");
            return;
        }
        INFO("服务监控启动成功, 开始监控子服务上线、下线事件");
    });
    watch_thread.detach();
    INFO("服务监控线程已启动");

    // 9. 服务注册(SvcProvider + Registry, KeepAlive 自动续期; 须在 server 启动后注册)
    svc_provider_ = std::make_shared<cpp_toolkit::SvcProvider>(
        etcd_settings_.etcd_center_addr, etcd_settings_.service_name, etcd_settings_.service_addr);
    if (!svc_provider_->Registry(etcd_settings_.registry_ttl))
    {
        ERR("服务注册失败: {} -> {}", etcd_settings_.service_name, etcd_settings_.service_addr);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s",
         etcd_settings_.service_name, etcd_settings_.service_addr, etcd_settings_.registry_ttl);

    // 9. 创建 DatabaseServer 实例(持有四个核心对象, 保证 keepalive/watcher/impl 生命周期)
    database_server_ = std::make_shared<DatabaseServer>(
        database_service_impl_, svc_watcher_, server_, svc_provider_);
    INFO("数据库子服务服务器构建完成");
    return database_server_;
}

} // namespace database_service
} // namespace chat_excel
