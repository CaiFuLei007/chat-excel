#include "svc_file_service/file_server.h"

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include "data/file_data.h"
#include "data/worksheet_data.h"
#include "svc_file_service/file_business.h"
// FastDFS 客户端头文件必须最后导入 : 其依赖的 fastcommon 头文件会向全局作用域
// 定义 byte 等宏, 先行导入会破坏 fmt/boost 等后续头文件的解析
#include <cpp-toolkit/fdfs.h>

namespace chat_excel
{
namespace file_service
{

namespace
{

// 服务监控键前缀, "/" 表示监控全部子服务的上线/下线事件
constexpr const char* kWatchKeyPrefix = "/";

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
constexpr int kServerStopCloseWaitMs = 0;

} // namespace

FileServer::FileServer(std::shared_ptr<FileServiceImpl> file_service_impl,
                       cpp_toolkit::SvcWatcher::Ptr svc_watcher,
                       std::shared_ptr<brpc::Server> server,
                       cpp_toolkit::SvcProvider::Ptr svc_provider)
    : file_service_impl_(std::move(file_service_impl)),
      svc_watcher_(std::move(svc_watcher)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("文件子服务服务器对象初始化完成");
}

FileServer::~FileServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void FileServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("文件子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("文件子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("文件子服务服务器已停止");
}

void FileServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        WARN("文件子服务服务器未在运行, 忽略停止请求");
        return;
    }

    INFO("文件子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("文件子服务服务器停止完成");
}

FileServerBuilder& FileServerBuilder::SetMysqlSettings(
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings)
{
    mysql_settings_ = std::move(mysql_settings);
    return *this;
}

FileServerBuilder& FileServerBuilder::SetRedisSettings(
    std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings)
{
    redis_settings_ = std::move(redis_settings);
    return *this;
}

FileServerBuilder& FileServerBuilder::SetFdfsSettings(const FdfsClientSettings& fdfs_settings)
{
    fdfs_settings_ = fdfs_settings;
    return *this;
}

FileServerBuilder& FileServerBuilder::SetListenPort(int listen_port)
{
    listen_port_ = listen_port;
    return *this;
}

FileServerBuilder& FileServerBuilder::SetEtcdSettings(const EtcdSettings& etcd_settings)
{
    etcd_settings_ = etcd_settings;
    return *this;
}

FileServerBuilder& FileServerBuilder::SetCareServiceNames(
    const std::vector<std::string>& care_service_names)
{
    care_service_names_ = care_service_names;
    return *this;
}

FileServerBuilder& FileServerBuilder::SetRegistryTtl(int registry_ttl)
{
    registry_ttl_ = registry_ttl;
    return *this;
}

std::shared_ptr<FileServer> FileServerBuilder::Build()
{
    // 1. 初始化 FastDFS 客户端(文件上传/下载/删除业务依赖),
    //    其余配置项(连接/网络超时等)使用 FdfsSettings 默认值
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

    // 3. 创建 MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle = cpp_toolkit::ODBFactory::Create(*mysql_settings_);
    INFO("MySQL 操作句柄创建完成");

    // 4. 创建 Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle =
        cpp_toolkit::RedisFactory::Create(*redis_settings_);
    INFO("Redis 操作句柄创建完成");

    // 5. 创建数据访问层对象(文件信息/工作表信息)
    std::shared_ptr<FileData> file_data = std::make_shared<FileData>(mysql_handle, redis_handle);
    std::shared_ptr<WorkSheetData> worksheet_data =
        std::make_shared<WorkSheetData>(mysql_handle, redis_handle);
    INFO("数据访问层对象构建完成");

    // 6. 创建文件业务逻辑对象(注入数据访问层/信道管理)
    std::shared_ptr<FileBusiness> file_business =
        std::make_shared<FileBusiness>(file_data, worksheet_data, channel_manager_);
    INFO("文件业务逻辑对象构建完成");

    // 7. 创建 RPC 接口实现对象
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    file_service_impl_ = std::make_shared<FileServiceImpl>(file_business);
    INFO("RPC 接口实现对象构建完成");

    // 8. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(listen_port_, file_service_impl_.get());
    if (server_ == nullptr)
    {
        ERR("brpc 服务器创建失败, 监听端口: {}", listen_port_);
        return nullptr;
    }
    INFO("brpc 服务器已启动, 监听端口: {}", listen_port_);

    // 9. 创建服务发现(监控)对象, 定义上线/下线回调
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

    // 10. 独立线程启动服务监控(Watch 阻塞直至服务停止, detach; 值捕获 svc_watcher 保活)
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

    // 11. 服务注册(SvcProvider + Registry, KeepAlive 自动续期; 须在 server 启动后注册)
    svc_provider_ = std::make_shared<cpp_toolkit::SvcProvider>(
        etcd_settings_.etcd_center_addr, etcd_settings_.service_name, etcd_settings_.service_addr);
    if (!svc_provider_->Registry(registry_ttl_))
    {
        ERR("服务注册失败: {} -> {}", etcd_settings_.service_name, etcd_settings_.service_addr);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s",
         etcd_settings_.service_name, etcd_settings_.service_addr, registry_ttl_);

    // 12. 创建 FileServer 实例(持有四个核心对象, 保证 keepalive/watcher/impl 生命周期)
    std::shared_ptr<FileServer> file_server = std::make_shared<FileServer>(
        file_service_impl_, svc_watcher_, server_, svc_provider_);
    INFO("文件子服务服务器构建完成");
    return file_server;
}

} // namespace file_service
} // namespace chat_excel
