#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/rpc.h>
#include "email_sender.h"
#include "notify_service_impl.h"

namespace chat_excel
{
namespace notify_service
{

/**
 * @brief 通知子服务 RPC 服务器类, 负责 brpc 服务器的启动与停止管理
 *        服务器对象由 NotifyServerBuilder 通过 ServerFactory 创建并启动(Start 已调用),
 *        本类持有 NotifyServiceImpl/SvcProvider/brpc::Server 三个核心对象,
 *        通过 NotifyServiceImpl 间接保活整个业务对象图
 *        (NotifyBusiness/邮箱发送器/工作线程池);
 *        Start() 调用 RunUntilAskedToQuit 阻塞当前线程, 由 Stop() 调用 AskToQuit 解除阻塞
 */
class NotifyServer
{
public:
    /**
     * @brief 构造函数, 注入三个核心对象, 由 NotifyServerBuilder 构建完成后调用
     * @param notify_service_impl RPC 接口实现对象, 须比 server 活得久(SERVER_DOESNT_OWN_SERVICE),
     *                            并间接保活业务对象图(NotifyBusiness/邮箱发送器/工作线程池)
     * @param server brpc 服务器对象(ServerFactory 创建时已启动)
     * @param svc_provider 服务注册对象, KeepAlive 自动续期, 须与服务器同生命周期
     */
    NotifyServer(std::shared_ptr<NotifyServiceImpl> notify_service_impl,
                 std::shared_ptr<brpc::Server> server,
                 cpp_toolkit::SvcProvider::Ptr svc_provider);

    /**
     * @brief 析构函数, 自动调用 Stop 停止服务器并回收资源
     */
    ~NotifyServer();

    // 服务器持有线程资源与核心对象, 禁止拷贝与赋值
    NotifyServer(const NotifyServer&) = delete;
    NotifyServer& operator=(const NotifyServer&) = delete;

    /**
     * @brief 启动服务器, 调用 RunUntilAskedToQuit 阻塞当前线程,
     *        直到 Stop() 调用 AskToQuit 设置 brpc 内部退出标志后返回;
     *        若服务器已在运行则直接返回, 不重复启动
     */
    void Start();

    /**
     * @brief 停止服务器, 调用 AskToQuit 解除 Start 阻塞,
     *        再调用 server Stop 停止接受新连接, Join 等待在处理请求完成;
     *        幂等接口, 未运行时直接返回
     */
    void Stop();

private:
    // RPC 接口实现对象, 间接保活整个业务对象图(NotifyBusiness/邮箱发送器/工作线程池)
    std::shared_ptr<NotifyServiceImpl> notify_service_impl_;

    // brpc 服务器对象(ServerFactory 创建时已启动)
    std::shared_ptr<brpc::Server> server_;

    // 服务注册对象, KeepAlive 自动续期
    cpp_toolkit::SvcProvider::Ptr svc_provider_;

    // 服务器运行状态标志, Start/Stop 幂等保护
    std::atomic<bool> is_running_;
};

/**
 * @brief 通知子服务构建器类, 负责分步配置并构建 NotifyServer 对象
 *        各 Set 方法返回构建器自身引用, 支持链式调用, 最后调用 Build 获取 NotifyServer 对象;
 *        Build 构建流程 : 邮箱客户端结构初始化(业务逻辑) -> 邮件工作线程启动 ->
 *        RPC 接口实现 -> brpc 服务器 -> 服务注册 -> NotifyServer
 *        使用方式 : NotifyServerBuilder()
 *                     .SetMailSettings(...)
 *                     .SetListenPort(...)
 *                     ...
 *                     .Build()
 */
class NotifyServerBuilder
{
public:
    // 简单构造函数
    NotifyServerBuilder() = default;

    // 简单析构函数
    ~NotifyServerBuilder() = default;

    // 构建器持有各组件资源, 禁止拷贝与赋值
    NotifyServerBuilder(const NotifyServerBuilder&) = delete;
    NotifyServerBuilder& operator=(const NotifyServerBuilder&) = delete;

    /**
     * @brief 设置邮箱配置信息
     * @param mail_settings 邮箱配置信息, 包含用户名、授权码、发送者邮件号与服务器地址
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetMailSettings(std::shared_ptr<MailSettings> mail_settings);

    /**
     * @brief 设置邮箱发送工作线程个数
     * @param worker_thread_count 工作线程个数
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetWorkerThreadCount(int worker_thread_count);

    /**
     * @brief 设置 brpc 服务器监听端口
     * @param listen_port 监听端口号
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetListenPort(int listen_port);

    /**
     * @brief 设置 ETCD 注册中心地址
     * @param etcd_center_addr ETCD 注册中心地址, eg : http://127.0.0.1:2379
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetEtcdAddress(const std::string& etcd_center_addr);

    /**
     * @brief 设置通知子服务注册名称
     * @param service_name 服务名称, eg : NotifyService
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetServiceName(const std::string& service_name);

    /**
     * @brief 设置通知子服务注册地址(客户端可访问的 host:port)
     * @param service_addr 服务地址, eg : 127.0.0.1:8082
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetServiceAddr(const std::string& service_addr);

    /**
     * @brief 设置服务注册 TTL(存活时间), KeepAlive 自动续期
     * @param registry_ttl TTL 秒数
     * @return 构建器自身引用, 支持链式调用
     */
    NotifyServerBuilder& SetRegistryTtl(int registry_ttl);

    /**
     * @brief 构建通知子服务服务器, 执行完整构建流程并返回 NotifyServer 对象;
     *        构建失败(服务器创建失败或服务注册失败)时返回 nullptr
     * @return 构建完成的 NotifyServer 对象, 失败时返回 nullptr
     */
    std::shared_ptr<NotifyServer> Build();

private:
    // 邮箱配置信息
    std::shared_ptr<MailSettings> mail_settings_;

    // 邮箱发送工作线程个数
    int worker_thread_count_ = 0;

    // brpc 服务器监听端口
    int listen_port_ = 0;

    // ETCD 注册中心地址
    std::string etcd_center_addr_;

    // 通知子服务注册名称
    std::string service_name_;

    // 通知子服务注册地址
    std::string service_addr_;

    // 服务注册 TTL(秒)
    int registry_ttl_ = 10;

    // RPC 接口实现对象(构建中间产物)
    std::shared_ptr<NotifyServiceImpl> notify_service_impl_;

    // brpc 服务器对象(构建中间产物)
    std::shared_ptr<brpc::Server> server_;

    // 服务注册对象(构建中间产物)
    cpp_toolkit::SvcProvider::Ptr svc_provider_;
};

} // namespace notify_service
} // namespace chat_excel
