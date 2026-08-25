#include "svc_user_service/user_server.h"
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include "data/session_data.h"
#include "data/user_data.h"
#include "data/verifycode_data.h"
#include "svc_user_service/session_manager.h"
#include "svc_user_service/user_business.h"
#include "svc_user_service/user_service_impl.h"

namespace chat_excel
{
namespace user_service
{

// 服务监控键前缀, "/" 表示监控全部子服务的上线/下线事件
static constexpr const char* kWatchKeyPrefix = "/";

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
static constexpr int kServerStopCloseWaitMs = 0;

UserServer::UserServer(std::shared_ptr<UserServiceImpl> user_service_impl,
                       cpp_toolkit::SvcWatcher::Ptr svc_watcher,
                       std::shared_ptr<brpc::Server> server,
                       cpp_toolkit::SvcProvider::Ptr svc_provider)
    : user_service_impl_(std::move(user_service_impl)),
      svc_watcher_(std::move(svc_watcher)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("用户子服务服务器对象初始化完成");
}

UserServer::~UserServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void UserServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("用户子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("用户子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("用户子服务服务器已停止");
}

void UserServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        WARN("用户子服务服务器未在运行, 忽略停止请求");
        return;
    }

    INFO("用户子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("用户子服务服务器停止完成");
}

UserServerBuilder& UserServerBuilder::SetMysqlSettings(
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings)
{
    mysql_settings_ = std::move(mysql_settings);
    return *this;
}

UserServerBuilder& UserServerBuilder::SetRedisSettings(
    std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings)
{
    redis_settings_ = std::move(redis_settings);
    return *this;
}

UserServerBuilder& UserServerBuilder::SetListenPort(int listen_port)
{
    listen_port_ = listen_port;
    return *this;
}

UserServerBuilder& UserServerBuilder::SetEtcdAddress(const std::string& etcd_center_addr)
{
    etcd_center_addr_ = etcd_center_addr;
    return *this;
}

UserServerBuilder& UserServerBuilder::SetServiceName(const std::string& service_name)
{
    service_name_ = service_name;
    return *this;
}

UserServerBuilder& UserServerBuilder::SetServiceAddr(const std::string& service_addr)
{
    service_addr_ = service_addr;
    return *this;
}

UserServerBuilder& UserServerBuilder::SetCareServiceNames(
    const std::vector<std::string>& care_service_names)
{
    care_service_names_ = care_service_names;
    return *this;
}

UserServerBuilder& UserServerBuilder::SetRegistryTtl(int registry_ttl)
{
    registry_ttl_ = registry_ttl;
    return *this;
}

std::shared_ptr<UserServer> UserServerBuilder::Build()
{
    // 1. 创建服务信道管理对象, 设置需要监控的子服务
    channel_manager_ = std::make_shared<cpp_toolkit::ChannelManager>();
    channel_manager_->SetCareService(care_service_names_);
    INFO("服务信道管理对象构建完成, 监控服务数量: {}", care_service_names_.size());

    // 2. 创建 MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle = cpp_toolkit::ODBFactory::Create(*mysql_settings_);
    INFO("MySQL 操作句柄创建完成");

    // 3. 创建 Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle = cpp_toolkit::RedisFactory::Create(*redis_settings_);
    INFO("Redis 操作句柄创建完成");

    // 4. 创建数据访问层对象(用户/会话/验证码)
    std::shared_ptr<UserData> user_data = std::make_shared<UserData>(mysql_handle, redis_handle);
    std::shared_ptr<SessionData> session_data = std::make_shared<SessionData>(mysql_handle, redis_handle);
    std::shared_ptr<VerifyCodeData> verifycode_data = std::make_shared<VerifyCodeData>(redis_handle);
    INFO("数据访问层对象构建完成");

    // 5. 创建会话管理对象
    std::shared_ptr<SessionManager> session_manager =
        std::make_shared<SessionManager>(session_data, user_data);
    INFO("会话管理对象构建完成");

    // 6. 创建用户业务逻辑对象(注入会话管理/验证码数据/用户数据/信道管理)
    std::shared_ptr<UserBusiness> user_business =
        std::make_shared<UserBusiness>(session_manager, verifycode_data, user_data, channel_manager_);
    INFO("用户业务逻辑对象构建完成");

    // 7. 创建 RPC 接口实现对象
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    user_service_impl_ = std::make_shared<UserServiceImpl>(user_business);
    INFO("RPC 接口实现对象构建完成");

    // 8. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(listen_port_, user_service_impl_.get());
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
        etcd_center_addr_, std::move(online_callback), std::move(offline_callback));
    INFO("服务发现对象构建完成, ETCD 地址: {}", etcd_center_addr_);

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
        etcd_center_addr_, service_name_, service_addr_);
    if (!svc_provider_->Registry(registry_ttl_))
    {
        ERR("服务注册失败: {} -> {}", service_name_, service_addr_);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s", service_name_, service_addr_, registry_ttl_);

    // 12. 创建 UserServer 实例(持有四个核心对象, 保证 keepalive/watcher/impl 生命周期)
    std::shared_ptr<UserServer> user_server = std::make_shared<UserServer>(
        user_service_impl_, svc_watcher_, server_, svc_provider_);
    INFO("用户子服务服务器构建完成");
    return user_server;
}

} // namespace user_service
} // namespace chat_excel
