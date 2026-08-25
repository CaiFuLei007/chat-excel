#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <brpc/server.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include "svc_user_service/user_service_impl.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 用户子服务 RPC 服务器类, 负责 brpc 服务器的启动与停止管理
 *        服务器对象由 UserServerBuilder 通过 ServerFactory 创建并启动(Start 已调用),
 *        本类持有 UserServiceImpl/SvcWatcher/SvcProvider/brpc::Server 四个核心对象,
 *        通过 UserServiceImpl 间接保活整个业务对象图(UserBusiness/数据访问层/ChannelManager);
 *        Start() 调用 RunUntilAskedToQuit 阻塞当前线程, 由 Stop() 调用 AskToQuit 解除阻塞
 */
class UserServer
{
public:
    /**
     * @brief 构造函数, 注入四个核心对象, 由 UserServerBuilder 构建完成后调用
     * @param user_service_impl RPC 接口实现对象, 须比 server 活得久(SERVER_DOESNT_OWN_SERVICE)
     * @param svc_watcher 服务发现监控对象, 监控其他子服务上线/下线
     * @param server brpc 服务器对象(ServerFactory 创建时已启动)
     * @param svc_provider 服务注册对象, KeepAlive 自动续期, 须与服务器同生命周期
     */
    UserServer(std::shared_ptr<UserServiceImpl> user_service_impl,
               cpp_toolkit::SvcWatcher::Ptr svc_watcher,
               std::shared_ptr<brpc::Server> server,
               cpp_toolkit::SvcProvider::Ptr svc_provider);

    /**
     * @brief 析构函数, 自动调用 Stop 停止服务器并回收资源
     */
    ~UserServer();

    // 服务器持有线程资源与核心对象, 禁止拷贝与赋值
    UserServer(const UserServer&) = delete;
    UserServer& operator=(const UserServer&) = delete;

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
    // RPC 接口实现对象, 间接保活整个业务对象图
    std::shared_ptr<UserServiceImpl> user_service_impl_;

    // 服务发现监控对象
    cpp_toolkit::SvcWatcher::Ptr svc_watcher_;

    // brpc 服务器对象(ServerFactory 创建时已启动)
    std::shared_ptr<brpc::Server> server_;

    // 服务注册对象, KeepAlive 自动续期
    cpp_toolkit::SvcProvider::Ptr svc_provider_;

    // 服务器运行状态标志, Start/Stop 幂等保护
    std::atomic<bool> is_running_;
};

/**
 * @brief 用户子服务构建器类, 负责分步配置并构建 UserServer 对象
 *        各 Set 方法返回构建器自身引用, 支持链式调用, 最后调用 Build 获取 UserServer 对象;
 *        Build 构建流程 : 信道管理 -> MySQL/Redis 句柄 -> 数据访问层 -> 会话管理 ->
 *        业务逻辑 -> RPC 接口实现 -> brpc 服务器 -> 服务发现 -> 服务监控 -> 服务注册 -> UserServer
 *        使用方式 : UserServerBuilder()
 *                     .SetMysqlSettings(...)
 *                     .SetRedisSettings(...)
 *                     ...
 *                     .Build()
 */
class UserServerBuilder
{
public:
    // 简单构造函数
    UserServerBuilder() = default;

    // 简单析构函数
    ~UserServerBuilder() = default;

    // 构建器持有各组件资源, 禁止拷贝与赋值
    UserServerBuilder(const UserServerBuilder&) = delete;
    UserServerBuilder& operator=(const UserServerBuilder&) = delete;

    /**
     * @brief 设置 MySQL 数据库配置
     * @param mysql_settings MySQL 配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetMysqlSettings(std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings);

    /**
     * @brief 设置 Redis 缓存配置
     * @param redis_settings Redis 配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetRedisSettings(std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings);

    /**
     * @brief 设置 brpc 服务器监听端口
     * @param listen_port 监听端口号
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetListenPort(int listen_port);

    /**
     * @brief 设置 ETCD 注册中心地址
     * @param etcd_center_addr ETCD 注册中心地址, eg : http://127.0.0.1:2379
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetEtcdAddress(const std::string& etcd_center_addr);

    /**
     * @brief 设置用户子服务注册名称
     * @param service_name 服务名称, eg : UserService
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetServiceName(const std::string& service_name);

    /**
     * @brief 设置用户子服务注册地址(客户端可访问的 host:port)
     * @param service_addr 服务地址, eg : 127.0.0.1:8081
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetServiceAddr(const std::string& service_addr);

    /**
     * @brief 设置需要监控(服务发现)的子服务名称列表
     * @param care_service_names 子服务名称列表
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetCareServiceNames(const std::vector<std::string>& care_service_names);

    /**
     * @brief 设置服务注册 TTL(存活时间), KeepAlive 自动续期
     * @param registry_ttl TTL 秒数
     * @return 构建器自身引用, 支持链式调用
     */
    UserServerBuilder& SetRegistryTtl(int registry_ttl);

    /**
     * @brief 构建用户子服务服务器, 执行完整构建流程并返回 UserServer 对象;
     *        构建失败(服务器创建失败或服务注册失败)时返回 nullptr
     * @return 构建完成的 UserServer 对象, 失败时返回 nullptr
     */
    std::shared_ptr<UserServer> Build();

private:
    // MySQL 数据库配置
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings_;

    // Redis 缓存配置
    std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings_;

    // brpc 服务器监听端口
    int listen_port_ = 0;

    // ETCD 注册中心地址
    std::string etcd_center_addr_;

    // 用户子服务注册名称
    std::string service_name_;

    // 用户子服务注册地址
    std::string service_addr_;

    // 需要监控的子服务名称列表
    std::vector<std::string> care_service_names_;

    // 服务注册 TTL(秒)
    int registry_ttl_ = 10;

    // 服务信道管理对象(构建中间产物)
    cpp_toolkit::ChannelManager::Ptr channel_manager_;

    // RPC 接口实现对象(构建中间产物)
    std::shared_ptr<UserServiceImpl> user_service_impl_;

    // 服务发现监控对象(构建中间产物)
    cpp_toolkit::SvcWatcher::Ptr svc_watcher_;

    // brpc 服务器对象(构建中间产物)
    std::shared_ptr<brpc::Server> server_;

    // 服务注册对象(构建中间产物)
    cpp_toolkit::SvcProvider::Ptr svc_provider_;
};

} // namespace user_service
} // namespace chat_excel
