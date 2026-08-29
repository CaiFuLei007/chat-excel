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
#include "svc_file_service/file_service_impl.h"

namespace chat_excel
{
namespace file_service
{

// FastDFS 客户端配置信息 : tracker 服务器地址列表
// 注 : 不直接使用 cpp_toolkit::FdfsSettings, 因其头文件依赖的 fastcommon
// 会向全局作用域定义 byte 等宏, 在头文件中导入会破坏其他头文件的解析
struct FdfsClientSettings
{
    // FastDFS tracker 服务器地址列表, eg : 127.0.0.1:22122
    std::vector<std::string> tracker_servers;
};

// ETCD 注册中心配置信息 : 注册中心地址/注册服务名称/注册服务地址
struct EtcdSettings
{
    // ETCD 注册中心地址, eg : http://127.0.0.1:2379
    std::string etcd_center_addr;

    // 文件子服务注册名称, eg : FileService
    std::string service_name;

    // 文件子服务注册地址(客户端可访问的 host:port), eg : 127.0.0.1:8084
    std::string service_addr;
};

/**
 * @brief 文件子服务 RPC 服务器类, 负责 brpc 服务器的启动与停止管理
 *        服务器对象由 FileServerBuilder 通过 ServerFactory 创建并启动(Start 已调用),
 *        本类持有 FileServiceImpl/SvcWatcher/SvcProvider/brpc::Server 四个核心对象,
 *        通过 FileServiceImpl 间接保活整个业务对象图(FileBusiness/数据访问层/ChannelManager);
 *        Start() 调用 RunUntilAskedToQuit 阻塞当前线程, 由 Stop() 调用 AskToQuit 解除阻塞
 */
class FileServer
{
public:
    /**
     * @brief 构造函数, 注入四个核心对象, 由 FileServerBuilder 构建完成后调用
     * @param file_service_impl RPC 接口实现对象, 须比 server 活得久(SERVER_DOESNT_OWN_SERVICE)
     * @param svc_watcher 服务发现监控对象, 监控其他子服务上线/下线
     * @param server brpc 服务器对象(ServerFactory 创建时已启动)
     * @param svc_provider 服务注册对象, KeepAlive 自动续期, 须与服务器同生命周期
     */
    FileServer(std::shared_ptr<FileServiceImpl> file_service_impl,
               cpp_toolkit::SvcWatcher::Ptr svc_watcher,
               std::shared_ptr<brpc::Server> server,
               cpp_toolkit::SvcProvider::Ptr svc_provider);

    /**
     * @brief 析构函数, 自动调用 Stop 停止服务器并回收资源
     */
    ~FileServer();

    // 服务器持有线程资源与核心对象, 禁止拷贝与赋值
    FileServer(const FileServer&) = delete;
    FileServer& operator=(const FileServer&) = delete;

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
    std::shared_ptr<FileServiceImpl> file_service_impl_;

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
 * @brief 文件子服务构建器类, 负责分步配置并构建 FileServer 对象
 *        各 Set 方法返回构建器自身引用, 支持链式调用, 最后调用 Build 获取 FileServer 对象;
 *        Build 构建流程 : FastDFS 客户端初始化 -> 信道管理 -> MySQL/Redis 句柄 ->
 *        数据访问层 -> 业务逻辑 -> RPC 接口实现 -> brpc 服务器 ->
 *        服务发现 -> 服务监控 -> 服务注册 -> FileServer
 *        使用方式 : FileServerBuilder()
 *                       .SetMysqlSettings(...)
 *                       .SetRedisSettings(...)
 *                       ...
 *                       .Build()
 */
class FileServerBuilder
{
public:
    // 简单构造函数
    FileServerBuilder() = default;

    // 简单析构函数
    ~FileServerBuilder() = default;

    // 构建器持有各组件资源, 禁止拷贝与赋值
    FileServerBuilder(const FileServerBuilder&) = delete;
    FileServerBuilder& operator=(const FileServerBuilder&) = delete;

    /**
     * @brief 设置 MySQL 数据库配置
     * @param mysql_settings MySQL 配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetMysqlSettings(std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings);

    /**
     * @brief 设置 Redis 缓存配置
     * @param redis_settings Redis 配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetRedisSettings(std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings);

    /**
     * @brief 设置 FastDFS 客户端配置信息(tracker 服务器地址列表),
     *        Build 时在内部完成 FdfsClient 的初始化
     * @param fdfs_settings FastDFS 客户端配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetFdfsSettings(const FdfsClientSettings& fdfs_settings);

    /**
     * @brief 设置 brpc 服务器监听端口
     * @param listen_port 监听端口号
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetListenPort(int listen_port);

    /**
     * @brief 设置 ETCD 注册中心配置信息(注册中心地址/服务名称/服务地址)
     * @param etcd_settings ETCD 注册中心配置信息
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetEtcdSettings(const EtcdSettings& etcd_settings);

    /**
     * @brief 设置需要监控(服务发现)的子服务名称列表
     * @param care_service_names 子服务名称列表
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetCareServiceNames(const std::vector<std::string>& care_service_names);

    /**
     * @brief 设置服务注册 TTL(存活时间), KeepAlive 自动续期
     * @param registry_ttl TTL 秒数
     * @return 构建器自身引用, 支持链式调用
     */
    FileServerBuilder& SetRegistryTtl(int registry_ttl);

    /**
     * @brief 构建文件子服务服务器, 执行完整构建流程并返回 FileServer 对象;
     *        构建失败(FastDFS 客户端初始化失败/服务器创建失败或服务注册失败)时返回 nullptr
     * @return 构建完成的 FileServer 对象, 失败时返回 nullptr
     */
    std::shared_ptr<FileServer> Build();

private:
    // MySQL 数据库配置
    std::shared_ptr<cpp_toolkit::MySQLSettings> mysql_settings_;

    // Redis 缓存配置
    std::shared_ptr<cpp_toolkit::RedisSettings> redis_settings_;

    // FastDFS 客户端配置信息(tracker 服务器地址列表)
    FdfsClientSettings fdfs_settings_;

    // brpc 服务器监听端口
    int listen_port_ = 0;

    // ETCD 注册中心配置信息(注册中心地址/服务名称/服务地址)
    EtcdSettings etcd_settings_;

    // 需要监控的子服务名称列表
    std::vector<std::string> care_service_names_;

    // 服务注册 TTL(秒)
    int registry_ttl_ = 10;

    // 服务信道管理对象(构建中间产物)
    cpp_toolkit::ChannelManager::Ptr channel_manager_;

    // RPC 接口实现对象(构建中间产物)
    std::shared_ptr<FileServiceImpl> file_service_impl_;

    // 服务发现监控对象(构建中间产物)
    cpp_toolkit::SvcWatcher::Ptr svc_watcher_;

    // brpc 服务器对象(构建中间产物)
    std::shared_ptr<brpc::Server> server_;

    // 服务注册对象(构建中间产物)
    cpp_toolkit::SvcProvider::Ptr svc_provider_;
};

} // namespace file_service
} // namespace chat_excel
