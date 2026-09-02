#include <memory>
#include <string>
#include <vector>
#include <gflags/gflags.h>
#include <spdlog/common.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include "svc_database_service/database_server.h"

// 日志输出文件路径, "stdout" 表示输出到标准输出, 其他值为日志文件路径
DEFINE_string(logger_file, "stdout", "日志输出文件路径, stdout 表示输出到标准输出");

// 日志输出级别, 可选值 : trace / debug / info / warn / error / critical
DEFINE_string(log_level, "info", "日志输出级别: trace/debug/info/warn/error/critical");

// MySQL 数据库地址(excel_connection 全局连接使用)
DEFINE_string(mysql_host, "127.0.0.1", "MySQL 数据库地址");

// MySQL 数据库端口
DEFINE_int32(mysql_port, 3306, "MySQL 数据库端口");

// MySQL 数据库用户名
DEFINE_string(mysql_user, "root", "MySQL 数据库用户名");

// MySQL 数据库密码
DEFINE_string(mysql_password, "", "MySQL 数据库密码");

// MySQL 数据库名
DEFINE_string(mysql_database, "chat_excel", "MySQL 数据库名");

// 数据库子服务监听端口
DEFINE_int32(listen_port, 8084, "数据库子服务监听端口");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// 数据库子服务名称
DEFINE_string(service_name, "DataBaseService", "数据库子服务名称");

// 数据库子服务注册地址(须与 host:listen_port 一致, 客户端可访问)
DEFINE_string(service_addr, "127.0.0.1:8084", "数据库子服务注册地址");

// 需要监控的子服务名称列表(逗号分隔)
DEFINE_string(care_service_names, "FileService", "需要监控的子服务名称列表(逗号分隔)");

// FastDFS Tracker 服务器地址列表(逗号分隔), SQLite 文件下载依赖
DEFINE_string(fdfs_tracker_servers, "127.0.0.1:22122", "FastDFS Tracker 服务器地址列表(逗号分隔)");

// 服务注册 TTL(秒)
DEFINE_int32(registry_ttl, 10, "服务注册 TTL(秒)");

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
 * @brief 按逗号分隔字符串, 跳过空 token
 * @param str 待分隔的字符串
 * @return 分隔后的字符串列表
 */
std::vector<std::string> SplitCommaSeparated(const std::string& str)
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = str.find(',');
    while (end != std::string::npos)
    {
        if (end > start)
        {
            result.push_back(str.substr(start, end - start));
        }
        start = end + 1;
        end = str.find(',', start);
    }
    if (start < str.size())
    {
        result.push_back(str.substr(start));
    }
    return result;
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
    logger_settings.loggerName = "database_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("数据库子服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}",
         FLAGS_logger_file, FLAGS_log_level);

    // 3. 组装 brpc 服务器配置信息(监听端口)
    chat_excel::database_service::RpcSettings rpc_settings;
    rpc_settings.listen_port = FLAGS_listen_port;
    INFO("brpc 服务器配置组装完成, 监听端口: {}", FLAGS_listen_port);

    // 4. 组装 ETCD 注册中心配置信息(注册中心地址/服务名称/服务地址/注册 TTL)
    chat_excel::database_service::EtcdSettings etcd_settings;
    etcd_settings.etcd_center_addr = FLAGS_etcd_address;
    etcd_settings.service_name = FLAGS_service_name;
    etcd_settings.service_addr = FLAGS_service_addr;
    etcd_settings.registry_ttl = FLAGS_registry_ttl;
    INFO("ETCD 注册中心配置组装完成, 服务名称: {} , 服务地址: {} , TTL: {}s",
         FLAGS_service_name, FLAGS_service_addr, FLAGS_registry_ttl);

    // 5. 组装 excel_connection 全局连接使用的 MySQL 配置
    auto mysql_settings = std::make_shared<cpp_toolkit::MySQLSettings>();
    mysql_settings->host = FLAGS_mysql_host;
    mysql_settings->port = FLAGS_mysql_port;
    mysql_settings->user = FLAGS_mysql_user;
    mysql_settings->password = FLAGS_mysql_password;
    mysql_settings->database = FLAGS_mysql_database;
    INFO("MySQL 配置组装完成, 地址: {} , 端口: {} , 数据库: {}",
         FLAGS_mysql_host, FLAGS_mysql_port, FLAGS_mysql_database);

    // 6. 解析需要监控的子服务名称列表(数据库子服务依赖文件子服务下载 SQLite 文件)
    std::vector<std::string> care_service_names = SplitCommaSeparated(FLAGS_care_service_names);
    INFO("服务发现配置完成, 监控服务数量: {}", care_service_names.size());

    // 7. 组装 FastDFS 客户端配置(SQLite 连接业务层下载文件依赖)
    chat_excel::database_service::FdfsSettings fdfs_settings;
    fdfs_settings.tracker_servers = SplitCommaSeparated(FLAGS_fdfs_tracker_servers);
    INFO("FastDFS 配置组装完成, Tracker 服务器数量: {}", fdfs_settings.tracker_servers.size());

    // 8. 链式构建数据库子服务服务器
    std::shared_ptr<chat_excel::database_service::DatabaseServer> database_server =
        chat_excel::database_service::DatabaseServerBuilder()
            .SetRpcSettings(rpc_settings)
            .SetEtcdSettings(etcd_settings)
            .SetMysqlSettings(mysql_settings)
            .SetFdfsSettings(fdfs_settings)
            .SetCareServiceNames(care_service_names)
            .Build();
    if (database_server == nullptr)
    {
        ERR("数据库子服务服务器构建失败, 程序退出");
        return 1;
    }

    // 8. 启动服务器(阻塞), brpc 的 RunUntilAskedToQuit 内部处理 SIGINT/SIGTERM 信号,
    //    收到信号后 RunUntilAskedToQuit 返回, 服务器自动 Stop+Join
    database_server->Start();

    INFO("数据库子服务退出");
    return 0;
}
