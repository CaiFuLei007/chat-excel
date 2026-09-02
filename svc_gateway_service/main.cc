#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <gflags/gflags.h>
#include <spdlog/common.h>
#include <cpp-toolkit/logger.h>
#include "gateway_server.h"

// 日志输出文件路径, "stdout" 表示输出到标准输出, 其他值为日志文件路径
DEFINE_string(logger_file, "stdout", "日志输出文件路径, stdout 表示输出到标准输出");

// 日志输出级别, 可选值 : trace / debug / info / warn / error / critical
DEFINE_string(log_level, "info", "日志输出级别: trace/debug/info/warn/error/critical");

// 网关服务器监听地址
DEFINE_string(gateway_address, "0.0.0.0", "网关服务器监听地址");

// 网关服务器监听端口
DEFINE_int32(gateway_port, 8080, "网关服务器监听端口");

// 前端静态文件根目录, 网关将其挂载到站点根路径, 空字符串表示不托管静态文件
DEFINE_string(www_root, "www", "前端静态文件根目录(相对或绝对路径), 空字符串表示不托管静态文件");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// 用户子服务名称
DEFINE_string(user_service_name, "UserService", "用户子服务名称");

// 文件子服务名称
DEFINE_string(file_service_name, "FileService", "文件子服务名称");

// 数据库子服务名称
DEFINE_string(database_service_name, "DataBaseService", "数据库子服务名称");

// AI 子服务名称
DEFINE_string(ai_service_name, "AIService", "AI 子服务名称");

namespace
{

// 程序退出信号标志, 记录收到的信号编号, 0 表示未收到退出信号
std::atomic<int> g_exit_signal_number(0);

/**
 * @brief SIGINT 和 SIGTERM 信号处理函数
 *        信号处理函数中仅设置退出标志(保证异步信号安全), 日志记录与服务器停止由主循环完成
 * @param signal_number 收到的信号编号
 */
void HandleExitSignal(int signal_number)
{
    g_exit_signal_number.store(signal_number);
}

/**
 * @brief 将日志级别字符串解析为 spdlog 日志级别枚举值
 * @param log_level 日志级别字符串
 * @return 对应的 spdlog 日志级别, 无法识别时默认返回 info 级别
 */
spdlog::level::level_enum ParseLogLevel(const std::string& log_level)
{
    if (log_level == "trace")
    {
        return spdlog::level::trace;
    }
    else if (log_level == "debug")
    {
        return spdlog::level::debug;
    }
    else if (log_level == "info")
    {
        return spdlog::level::info;
    }
    else if (log_level == "warn")
    {
        return spdlog::level::warn;
    }
    else if (log_level == "error")
    {
        return spdlog::level::err;
    }
    else if (log_level == "critical")
    {
        return spdlog::level::critical;
    }
    else
    {
        return spdlog::level::info;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    // 1. 解析 gflags 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 2. 初始化日志记录(异步日志, 不阻塞业务线程)
    //    loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings logger_settings;
    logger_settings.async = true;
    logger_settings.loggerName = "gateway_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("网关服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}", FLAGS_logger_file, FLAGS_log_level);

    // 3. 注册信号处理函数, 处理 SIGINT 和 SIGTERM 信号
    std::signal(SIGINT, HandleExitSignal);
    std::signal(SIGTERM, HandleExitSignal);
    INFO("信号处理函数注册完成, 支持 SIGINT 和 SIGTERM 信号");

    // 4. 配置服务发现监控的子服务名称列表(用户、文件、数据库、AI 子服务)
    std::vector<std::string> care_service_names = {
        FLAGS_user_service_name,
        FLAGS_file_service_name,
        FLAGS_database_service_name,
        FLAGS_ai_service_name,
    };
    INFO("服务发现配置完成, 监控服务: {} , {} , {} , {}",
         care_service_names[0], care_service_names[1], care_service_names[2], care_service_names[3]);

    // 5/6. 使用构建器链式构建网关服务器对象并启动服务器
    //      构建流程 : 服务信道管理 -> 服务发现 -> 服务监控 -> HTTP 接口定义 -> HTTP 服务器 -> 路由绑定 -> 静态文件托管 -> 网关服务器对象 -> 启动
    std::shared_ptr<chat_excel::GatewayServer> gateway_server =
        chat_excel::GatewayServerBuilder()
            .BuildChannelManager(care_service_names)
            .BuildServiceDiscovery(FLAGS_etcd_address)
            .StartServiceWatch()
            .BuildHttpServiceImpl()
            .BuildHttpServer()
            .BindRoutes()
            .BuildStaticFiles(FLAGS_www_root)
            .BuildGatewayServer(FLAGS_gateway_address, FLAGS_gateway_port)
            .StartServer();
    if (gateway_server == nullptr)
    {
        ERR("网关服务器构建失败, 程序退出");
        return 1;
    }

    // 7. 循环检测程序退出信号, 服务器停止时退出循环, 结束主程序
    while (true)
    {
        // 收到退出信号, 停止服务器并退出循环
        int exit_signal_number = g_exit_signal_number.load();
        if (exit_signal_number != 0)
        {
            INFO("收到程序退出信号: {} , 准备停止网关服务器", exit_signal_number);
            if (gateway_server->IsRunning())
            {
                gateway_server->Stop();
            }
            break;
        }

        // 服务器已停止(监听失败等异常情况), 退出循环
        if (!gateway_server->IsRunning())
        {
            WARN("网关服务器已停止, 退出主程序");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    INFO("网关服务退出");
    return 0;
}
