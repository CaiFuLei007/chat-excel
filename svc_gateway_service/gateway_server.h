#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <httplib.h>
#include <cpp-toolkit/etcd.h>
#include <cpp-toolkit/rpc.h>
#include "gateway_impl_service.h"

namespace chat_excel
{

/**
 * @brief 网关服务器类, 负责 HTTP 服务器的启动与停止管理, 不包含业务逻辑
 *        服务器在新线程中启动, 防止阻塞主线程; 对象销毁时自动调用 Stop 方法停止服务器
 */
class GatewayServer
{
public:
    /**
     * @brief 构造函数, 完成 HTTP 服务器对象与监听地址的初始化
     * @param http_server cpp-httplib HTTP 服务器对象(已完成路由绑定与超时配置)
     * @param host 网关服务器监听地址
     * @param port 网关服务器监听端口
     */
    GatewayServer(std::shared_ptr<httplib::Server> http_server, const std::string& host, int port);

    /**
     * @brief 析构函数, 自动调用 Stop 方法停止服务器并回收线程资源
     */
    ~GatewayServer();

    // 服务器对象内部持有线程资源, 禁止拷贝与赋值
    GatewayServer(const GatewayServer&) = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;

    /**
     * @brief 启动网关服务器, 服务器在新线程中启动, 防止阻塞主线程
     *        若服务器已在运行则直接返回, 不重复启动
     * @return true 启动成功或服务器已在运行, false 启动失败
     */
    bool Start();

    /**
     * @brief 停止网关服务器, 若服务器未在运行则直接返回
     *        停止时等待服务器监听线程退出并回收线程资源
     */
    void Stop();

    /**
     * @brief 获取服务器运行状态
     * @return true 服务器运行中, false 服务器未运行
     */
    bool IsRunning() const;

private:
    // cpp-httplib HTTP 服务器对象
    std::shared_ptr<httplib::Server> http_server_;

    // 网关服务器监听地址
    std::string host_;

    // 网关服务器监听端口
    int port_;

    // 服务器运行状态标志
    std::atomic<bool> is_running_;

    // 服务器监听线程
    std::thread server_thread_;
};

/**
 * @brief 网关服务器构建器类, 负责网关服务器与服务发现组件的分步构建
 *        构建流程 : 服务信道管理对象 -> 服务发现(监控)对象 -> 服务监控线程
 *        -> HTTP 接口定义对象 -> HTTP 服务器对象 -> 路由绑定 -> 网关服务器对象 -> 启动服务器
 *        各构建方法返回构建器自身引用, 支持链式调用, 最后调用 StartServer 获取网关服务器对象
 *        使用方式 : GatewayServerBuilder()
 *                     .BuildChannelManager(care_service_names)
 *                     .BuildServiceDiscovery(etcd_center_addr)
 *                     ...
 *                     .StartServer()
 */
class GatewayServerBuilder
{
public:
    // 简单构造函数
    GatewayServerBuilder() = default;

    // 简单析构函数
    ~GatewayServerBuilder() = default;

    // 构建器持有各组件资源, 禁止拷贝与赋值
    GatewayServerBuilder(const GatewayServerBuilder&) = delete;
    GatewayServerBuilder& operator=(const GatewayServerBuilder&) = delete;

    /**
     * @brief 构建服务信道管理对象, 并设置需要监控的服务
     * @param care_service_names 需要监控的子服务名称列表(用户/文件/数据库/AI 子服务)
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BuildChannelManager(const std::vector<std::string>& care_service_names);

    /**
     * @brief 构造服务发现(监控)对象, 定义回调函数, 处理服务上线、下线时服务节点的添加和移除
     * @param etcd_center_addr ETCD 注册中心地址, eg : http://dev-etcd:2379
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BuildServiceDiscovery(const std::string& etcd_center_addr);

    /**
     * @brief 在独立的线程中启动服务监控
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& StartServiceWatch();

    /**
     * @brief 构建 HTTP 接口定义对象
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BuildHttpServiceImpl();

    /**
     * @brief 构建 cpp-httplib 的 HTTP 服务器对象, 并设置读写超时时间为 5 分钟
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BuildHttpServer();

    /**
     * @brief 绑定路由, 将全部 30 个 HTTP 接口绑定到 HTTP 服务器对象上
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BindRoutes();

    /**
     * @brief 构建网关服务器对象
     * @param host 网关服务器监听地址
     * @param port 网关服务器监听端口
     * @return 构建器自身引用, 支持链式调用
     */
    GatewayServerBuilder& BuildGatewayServer(const std::string& host, int port);

    /**
     * @brief 启动服务器并返回网关服务器对象, 作为链式调用的终结方法
     * @return 构建并启动完成的网关服务器对象, 启动失败时返回 nullptr
     */
    std::shared_ptr<GatewayServer> StartServer();

private:
    // 服务信道管理对象, 用于管理各子服务的 RPC 信道
    cpp_toolkit::ChannelManager::Ptr channel_manager_;

    // 服务发现(监控)对象, 监控子服务的上线、下线事件
    cpp_toolkit::SvcWatcher::Ptr svc_watcher_;

    // HTTP 接口定义对象, 负责 30 个 HTTP 接口的定义与路由绑定
    std::shared_ptr<GatewayServiceImpl> gateway_service_impl_;

    // cpp-httplib HTTP 服务器对象
    std::shared_ptr<httplib::Server> http_server_;

    // 网关服务器对象
    std::shared_ptr<GatewayServer> gateway_server_;
};

} // namespace chat_excel
