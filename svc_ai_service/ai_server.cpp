#include "svc_ai_service/ai_server.h"

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <brpc/server.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include "data/chat_session_data.h"
#include "svc_ai_service/ai_business.h"
#include "svc_ai_service/chat_session_manager.h"

namespace chat_excel
{
namespace ai_service
{

namespace
{

// 服务监控键前缀, "/" 表示监控全部子服务的上线/下线事件
constexpr const char* kWatchKeyPrefix = "/";

// brpc 服务器停止等待时间(毫秒, 已弃用但需传参)
constexpr int kServerStopCloseWaitMs = 0;

} // namespace

AiServer::AiServer(std::shared_ptr<AiServiceImpl> ai_service_impl,
                   cpp_toolkit::SvcWatcher::Ptr svc_watcher,
                   std::shared_ptr<brpc::Server> server,
                   cpp_toolkit::SvcProvider::Ptr svc_provider)
    : ai_service_impl_(std::move(ai_service_impl)),
      svc_watcher_(std::move(svc_watcher)),
      server_(std::move(server)),
      svc_provider_(std::move(svc_provider)),
      is_running_(false)
{
    INFO("AI 子服务服务器对象初始化完成");
}

AiServer::~AiServer()
{
    // 对象销毁时自动停止服务器, 回收资源
    Stop();
}

void AiServer::Start()
{
    // 检测服务器是否已运行, 已运行则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("AI 子服务服务器已在运行中, 忽略重复启动请求");
        return;
    }

    INFO("AI 子服务服务器启动中");

    // 先标记运行状态, 防止重复启动
    is_running_.store(true);

    // RunUntilAskedToQuit 阻塞当前线程, 直到 AskToQuit 设置退出标志后返回
    server_->RunUntilAskedToQuit();

    is_running_.store(false);
    INFO("AI 子服务服务器已停止");
}

void AiServer::Stop()
{
    // 幂等保护, 未运行则直接返回
    bool was_running = is_running_.exchange(false);
    if (!was_running)
    {
        WARN("AI 子服务服务器未在运行, 忽略停止请求");
        return;
    }

    INFO("AI 子服务服务器停止中");

    // AskToQuit 设置 brpc 内部退出标志, 解除 RunUntilAskedToQuit 阻塞
    brpc::AskToQuit();

    // Stop 停止接受新连接, Join 等待在处理请求完成
    server_->Stop(kServerStopCloseWaitMs);
    server_->Join();

    INFO("AI 子服务服务器停止完成");
}

AiServerBuilder& AiServerBuilder::SetMysqlSettings(
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings)
{
    mysql_settings_ = std::move(mysql_settings);
    return *this;
}

AiServerBuilder& AiServerBuilder::SetRedisSettings(
    std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings)
{
    redis_settings_ = std::move(redis_settings);
    return *this;
}

AiServerBuilder& AiServerBuilder::SetChatSdkSettings(
    std::shared_ptr<ChatSdkSettings> chat_sdk_settings)
{
    chat_sdk_settings_ = std::move(chat_sdk_settings);
    return *this;
}

AiServerBuilder& AiServerBuilder::SetListenPort(int listen_port)
{
    listen_port_ = listen_port;
    return *this;
}

AiServerBuilder& AiServerBuilder::SetEtcdSettings(const EtcdSettings& etcd_settings)
{
    etcd_settings_ = etcd_settings;
    return *this;
}

AiServerBuilder& AiServerBuilder::SetCareServiceNames(
    const std::vector<std::string>& care_service_names)
{
    care_service_names_ = care_service_names;
    return *this;
}

std::shared_ptr<AiServer> AiServerBuilder::Build()
{
    // 1. 创建服务信道管理对象, 设置需要监控的子服务
    channel_manager_ = std::make_shared<cpp_toolkit::ChannelManager>();
    channel_manager_->SetCareService(care_service_names_);
    INFO("服务信道管理对象构建完成, 监控服务数量: {}", care_service_names_.size());

    // 2. 创建 MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle = cpp_toolkit::ODBFactory::Create(*mysql_settings_);
    INFO("MySQL 操作句柄创建完成");

    // 3. 创建 Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle =
        cpp_toolkit::RedisFactory::Create(*redis_settings_);
    INFO("Redis 操作句柄创建完成");

    // 4. 创建 ChatSDK 实例并注册模型配置列表,
    //    任一模型注册失败则视为构建失败, 返回 nullptr
    std::shared_ptr<aichat_sdk::AIChatSdk> ai_chat_sdk =
        std::make_shared<aichat_sdk::AIChatSdk>(chat_sdk_settings_->db_path);
    for (const aichat_sdk::Config& model_config : chat_sdk_settings_->models)
    {
        if (!ai_chat_sdk->RegisterModel(model_config))
        {
            ERR("ChatSDK 模型注册失败, 模型名称: {}", model_config.model_info.model_name);
            return nullptr;
        }
        INFO("ChatSDK 模型注册完成, 模型名称: {}", model_config.model_info.model_name);
    }
    INFO("ChatSDK 初始化完成, 本地数据库路径: {} , 注册模型数量: {}",
         chat_sdk_settings_->db_path, chat_sdk_settings_->models.size());

    // 5. 创建数据访问层对象(聊天会话数据)
    std::shared_ptr<ChatSessionData> chat_session_data =
        std::make_shared<ChatSessionData>(mysql_handle, redis_handle);
    INFO("数据访问层对象构建完成");

    // 6. 创建聊天会话管理对象
    std::shared_ptr<ChatSessionManager> chat_session_manager =
        std::make_shared<ChatSessionManager>(chat_session_data);
    INFO("聊天会话管理对象构建完成");

    // 7. 创建 AI 业务逻辑对象(注入会话管理/ChatSDK)
    std::shared_ptr<AiBusiness> ai_business =
        std::make_shared<AiBusiness>(chat_session_manager, ai_chat_sdk);
    INFO("AI 业务逻辑对象构建完成");

    // 8. 创建 RPC 接口实现对象
    //    ServerFactory 使用 SERVER_DOESNT_OWN_SERVICE, 业务对象必须比 server 活得久
    ai_service_impl_ = std::make_shared<AiServiceImpl>(ai_business);
    INFO("RPC 接口实现对象构建完成");

    // 9. 创建并启动 brpc 服务器(ServerFactory 内部 AddService + Start, 立即启动)
    server_ = cpp_toolkit::ServerFactory::CreateServer(listen_port_, ai_service_impl_.get());
    if (server_ == nullptr)
    {
        ERR("brpc 服务器创建失败, 监听端口: {}", listen_port_);
        return nullptr;
    }
    INFO("brpc 服务器已启动, 监听端口: {}", listen_port_);

    // 10. 创建服务发现(监控)对象, 定义上线/下线回调
    //     回调以 shared_ptr 值捕获信道管理对象, 保证监控线程生命周期内信道管理对象有效
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

    // 11. 独立线程启动服务监控(Watch 阻塞直至服务停止, detach; 值捕获 svc_watcher 保活)
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

    // 12. 服务注册(SvcProvider + Registry, KeepAlive 自动续期; 须在 server 启动后注册)
    svc_provider_ = std::make_shared<cpp_toolkit::SvcProvider>(
        etcd_settings_.etcd_center_addr, etcd_settings_.service_name, etcd_settings_.service_addr);
    if (!svc_provider_->Registry(etcd_settings_.registry_ttl))
    {
        ERR("服务注册失败: {} -> {}", etcd_settings_.service_name, etcd_settings_.service_addr);
        return nullptr;
    }
    INFO("服务注册完成: {} -> {}, TTL: {}s",
         etcd_settings_.service_name, etcd_settings_.service_addr, etcd_settings_.registry_ttl);

    // 13. 创建 AiServer 实例(持有四个核心对象, 保证 keepalive/watcher/impl 生命周期)
    std::shared_ptr<AiServer> ai_server = std::make_shared<AiServer>(
        ai_service_impl_, svc_watcher_, server_, svc_provider_);
    INFO("AI 子服务服务器构建完成");
    return ai_server;
}

} // namespace ai_service
} // namespace chat_excel
