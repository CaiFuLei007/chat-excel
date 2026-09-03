#include <memory>
#include <string>
#include <vector>
#include <gflags/gflags.h>
#include <spdlog/common.h>
#include <cpp-toolkit/logger.h>
#include "excel_parse_server.h"

// gflags 参数配置文件路径(包含 gflags 参数配置), 相对路径相对于工作目录
DEFINE_string(conf, "chat_data.conf", "gflags 参数配置文件路径, 相对路径相对于工作目录");

// 日志输出文件路径, "stdout" 表示输出到标准输出, 其他值为日志文件路径
DEFINE_string(logger_file, "stdout", "日志输出文件路径, stdout 表示输出到标准输出");

// 日志输出级别, 可选值 : trace / debug / info / warn / error / critical
DEFINE_string(log_level, "info", "日志输出级别: trace/debug/info/warn/error/critical");

// Excel 解析子服务监听端口
DEFINE_int32(listen_port, 8083, "Excel 解析子服务监听端口");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// Excel 解析子服务名称
DEFINE_string(server_name, "ExcelParseService", "Excel 解析子服务名称");

// Excel 解析子服务注册地址(须与 host:listen_port 一致, 客户端可访问)
DEFINE_string(server_addr, "127.0.0.1:8083", "Excel 解析子服务注册地址");

// 服务注册 TTL(秒)
DEFINE_int32(registry_ttl, 10, "服务注册 TTL(秒)");

// FastDFS tracker 服务器地址列表(逗号分隔)
DEFINE_string(fdfs_tracker_servers, "127.0.0.1:22122",
              "FastDFS tracker 服务器地址列表, 逗号分隔");

namespace
{

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

/**
 * @brief 将逗号分隔的 tracker 服务器地址字符串拆分为地址列表
 * @param tracker_servers 逗号分隔的 tracker 服务器地址字符串
 * @return tracker 服务器地址列表
 */
std::vector<std::string> SplitTrackerServers(const std::string& tracker_servers)
{
    std::vector<std::string> server_list;
    std::string::size_type start_pos = 0;
    while (start_pos <= tracker_servers.size())
    {
        const std::string::size_type comma_pos = tracker_servers.find(',', start_pos);
        if (comma_pos == std::string::npos)
        {
            const std::string server = tracker_servers.substr(start_pos);
            if (!server.empty())
            {
                server_list.push_back(server);
            }
            break;
        }
        const std::string server = tracker_servers.substr(start_pos, comma_pos - start_pos);
        if (!server.empty())
        {
            server_list.push_back(server);
        }
        start_pos = comma_pos + 1;
    }
    return server_list;
}

} // namespace

int main(int argc, char* argv[])
{
    // 1. 解析 gflags 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 2. 从配置文件读取 gflags 参数(文件中已定义参数的配置覆盖默认值, 未定义的参数自动忽略)
    gflags::ReadFromFlagsFile(FLAGS_conf, argv[0], false);

    // 3. 初始化日志记录(异步日志, 不阻塞业务线程)
    //    loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings logger_settings;
    logger_settings.async = true;
    logger_settings.loggerName = "excel_parse_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("Excel 解析子服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}",
         FLAGS_logger_file, FLAGS_log_level);

    // 3. 组装 brpc 子服务服务器配置信息(监听端口/子服务名称/子服务注册地址)
    chat_excel::excel_parse_service::BrpcSettings brpc_settings;
    brpc_settings.listen_port = FLAGS_listen_port;
    brpc_settings.service_name = FLAGS_server_name;
    brpc_settings.service_addr = FLAGS_server_addr;
    INFO("brpc 服务器配置组装完成, 监听端口: {} , 服务名称: {} , 服务地址: {}",
         FLAGS_listen_port, FLAGS_server_name, FLAGS_server_addr);

    // 4. 组装 FastDFS 客户端配置信息(tracker 服务器地址列表),
    //    供服务器构建器在内部完成 FdfsClient 初始化
    chat_excel::excel_parse_service::FdfsClientSettings fdfs_settings;
    fdfs_settings.tracker_servers = SplitTrackerServers(FLAGS_fdfs_tracker_servers);
    INFO("FastDFS 客户端配置组装完成, tracker 服务器个数: {}",
         fdfs_settings.tracker_servers.size());

    // 5. 链式构建 Excel 解析子服务服务器
    std::shared_ptr<chat_excel::excel_parse_service::ExcelParseServer> excel_parse_server =
        chat_excel::excel_parse_service::ExcelParseServerBuilder()
            .SetEtcdAddress(FLAGS_etcd_address)
            .SetBrpcSettings(brpc_settings)
            .SetFdfsSettings(fdfs_settings)
            .SetRegistryTtl(FLAGS_registry_ttl)
            .Build();
    if (excel_parse_server == nullptr)
    {
        ERR("Excel 解析子服务服务器构建失败, 程序退出");
        return 1;
    }

    // 6. 启动服务器(阻塞), brpc 的 RunUntilAskedToQuit 内部处理 SIGINT/SIGTERM 信号,
    //    收到信号后 RunUntilAskedToQuit 返回, 服务器自动 Stop+Join
    excel_parse_server->Start();

    INFO("Excel 解析子服务退出");
    return 0;
}
