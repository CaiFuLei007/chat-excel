#include <memory>
#include <string>
#include <vector>
#include <gflags/gflags.h>
#include <spdlog/common.h>
// brpc/server.h 须在 cpp-toolkit/redis.h 之前包含 : sw/redis++ 引入的 hiredis/read.h
// 会以宏定义 REDIS_REPLY_STRING 等, 与 brpc/redis_reply.h 的同名枚举值冲突
#include <brpc/server.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include "svc_file_service/file_server.h"

// 日志输出文件路径, "stdout" 表示输出到标准输出, 其他值为日志文件路径
DEFINE_string(logger_file, "stdout", "日志输出文件路径, stdout 表示输出到标准输出");

// 日志输出级别, 可选值 : trace / debug / info / warn / error / critical
DEFINE_string(log_level, "info", "日志输出级别: trace/debug/info/warn/error/critical");

// MySQL 数据库地址
DEFINE_string(mysql_host, "127.0.0.1", "MySQL 数据库地址");

// MySQL 数据库端口
DEFINE_int32(mysql_port, 3306, "MySQL 数据库端口");

// MySQL 数据库用户名
DEFINE_string(mysql_user, "root", "MySQL 数据库用户名");

// MySQL 数据库密码
DEFINE_string(mysql_password, "", "MySQL 数据库密码");

// MySQL 数据库名
DEFINE_string(mysql_database, "chat_excel", "MySQL 数据库名");

// MySQL 数据库字符集
DEFINE_string(mysql_charset, "utf8", "MySQL 数据库字符集");

// MySQL 连接池大小
DEFINE_int32(mysql_connection_pool_size, 3, "MySQL 连接池大小");

// Redis 地址
DEFINE_string(redis_host, "127.0.0.1", "Redis 地址");

// Redis 端口
DEFINE_int32(redis_port, 6379, "Redis 端口");

// Redis 用户名
DEFINE_string(redis_user, "default", "Redis 用户名");

// Redis 密码
DEFINE_string(redis_password, "", "Redis 密码");

// Redis 数据库索引
DEFINE_int32(redis_db, 0, "Redis 数据库索引");

// Redis 连接池大小
DEFINE_int32(redis_pool_connections_size, 3, "Redis 连接池大小");

// FastDFS tracker 服务器地址列表(逗号分隔)
DEFINE_string(fdfs_tracker_servers, "127.0.0.1:22122",
              "FastDFS tracker 服务器地址列表, 逗号分隔");

// 文件子服务监听端口
DEFINE_int32(listen_port, 8084, "文件子服务监听端口");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// 文件子服务名称
DEFINE_string(service_name, "FileService", "文件子服务名称");

// 文件子服务注册地址(须与 host:listen_port 一致, 客户端可访问)
DEFINE_string(service_addr, "127.0.0.1:8084", "文件子服务注册地址");

// 需要监控的子服务名称列表(逗号分隔), Excel 解析子服务负责 Excel 文件解析,
// 数据库子服务负责 Excel 数据的保存与预览查询,
// AI 子服务提供会话文件映射表更新接口(文件上传/删除时调用)
DEFINE_string(care_service_names, "ExcelParseService,DataBaseService,AIService",
              "需要监控的子服务名称列表(逗号分隔)");

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
 * @brief 按逗号分隔字符串, 跳过空 token, 用于解析服务名称列表/tracker 地址列表等
 * @param str 逗号分隔的字符串
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
    logger_settings.loggerName = "file_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("文件子服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}",
         FLAGS_logger_file, FLAGS_log_level);

    // 3. 组装 MySQL 配置
    auto mysql_settings = std::make_shared<cpp_toolkit::MySQLSettings>();
    mysql_settings->host = FLAGS_mysql_host;
    mysql_settings->port = FLAGS_mysql_port;
    mysql_settings->user = FLAGS_mysql_user;
    mysql_settings->password = FLAGS_mysql_password;
    mysql_settings->database = FLAGS_mysql_database;
    mysql_settings->charset = FLAGS_mysql_charset;
    mysql_settings->connection_pool_size = FLAGS_mysql_connection_pool_size;
    INFO("MySQL 配置组装完成, 地址: {} , 端口: {} , 数据库: {}",
         FLAGS_mysql_host, FLAGS_mysql_port, FLAGS_mysql_database);

    // 4. 组装 Redis 配置
    auto redis_settings = std::make_shared<cpp_toolkit::RedisSettings>();
    redis_settings->host = FLAGS_redis_host;
    redis_settings->port = FLAGS_redis_port;
    redis_settings->user = FLAGS_redis_user;
    redis_settings->password = FLAGS_redis_password;
    redis_settings->db = FLAGS_redis_db;
    redis_settings->pool_connections_size = FLAGS_redis_pool_connections_size;
    INFO("Redis 配置组装完成, 地址: {} , 端口: {}", FLAGS_redis_host, FLAGS_redis_port);

    // 5. 组装 FastDFS 客户端配置信息(tracker 服务器地址列表),
    //    供服务器构建器在内部完成 FdfsClient 初始化
    chat_excel::file_service::FdfsClientSettings fdfs_settings;
    fdfs_settings.tracker_servers = SplitCommaSeparated(FLAGS_fdfs_tracker_servers);
    INFO("FastDFS 客户端配置组装完成, tracker 服务器个数: {}",
         fdfs_settings.tracker_servers.size());

    // 6. 组装 ETCD 注册中心配置信息(注册中心地址/服务名称/服务地址)
    chat_excel::file_service::EtcdSettings etcd_settings;
    etcd_settings.etcd_center_addr = FLAGS_etcd_address;
    etcd_settings.service_name = FLAGS_service_name;
    etcd_settings.service_addr = FLAGS_service_addr;
    INFO("ETCD 注册中心配置组装完成, 地址: {} , 服务: {} -> {}",
         FLAGS_etcd_address, FLAGS_service_name, FLAGS_service_addr);

    // 7. 解析需要监控的子服务名称列表
    std::vector<std::string> care_service_names = SplitCommaSeparated(FLAGS_care_service_names);
    INFO("服务发现配置完成, 监控服务数量: {}", care_service_names.size());

    // 8. 链式构建文件子服务服务器
    std::shared_ptr<chat_excel::file_service::FileServer> file_server =
        chat_excel::file_service::FileServerBuilder()
            .SetMysqlSettings(mysql_settings)
            .SetRedisSettings(redis_settings)
            .SetFdfsSettings(fdfs_settings)
            .SetEtcdSettings(etcd_settings)
            .SetListenPort(FLAGS_listen_port)
            .SetCareServiceNames(care_service_names)
            .SetRegistryTtl(FLAGS_registry_ttl)
            .Build();
    if (file_server == nullptr)
    {
        ERR("文件子服务服务器构建失败, 程序退出");
        return 1;
    }

    // 9. 启动服务器(阻塞), brpc 的 RunUntilAskedToQuit 内部处理 SIGINT/SIGTERM 信号,
    //    收到信号后 RunUntilAskedToQuit 返回, 服务器自动 Stop+Join
    file_server->Start();

    INFO("文件子服务退出");
    return 0;
}
