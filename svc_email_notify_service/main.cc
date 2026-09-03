#include <memory>
#include <string>
#include <gflags/gflags.h>
#include <spdlog/common.h>
#include <cpp-toolkit/logger.h>
#include "notify_server.h"

// gflags 参数配置文件路径(包含 gflags 参数配置), 相对路径相对于工作目录
DEFINE_string(conf, "chat_data.conf", "gflags 参数配置文件路径, 相对路径相对于工作目录");

// 日志输出文件路径, "stdout" 表示输出到标准输出, 其他值为日志文件路径
DEFINE_string(logger_file, "stdout", "日志输出文件路径, stdout 表示输出到标准输出");

// 日志输出级别, 可选值 : trace / debug / info / warn / error / critical
DEFINE_string(log_level, "info", "日志输出级别: trace/debug/info/warn/error/critical");

// 邮箱用户名(SMTP 登录用户名)
DEFINE_string(smtp_username, "", "邮箱用户名(SMTP 登录用户名)");

// 邮箱授权码(SMTP 登录授权码, 非邮箱登录密码)
DEFINE_string(smtp_password, "", "邮箱授权码(SMTP 登录授权码)");

// 发送者邮件号(邮件 From 头中展示的发件人地址)
DEFINE_string(from_email, "", "发送者邮件号");

// 邮箱服务器地址(host:port 形式)
DEFINE_string(smtp_server, "smtp.qq.com:465", "邮箱服务器地址(host:port 形式)");

// 邮箱发送工作线程个数
DEFINE_int32(worker_thread_count, 3, "邮箱发送工作线程个数");

// 通知子服务监听端口
DEFINE_int32(listen_port, 8082, "通知子服务监听端口");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// 通知子服务名称
DEFINE_string(server_name, "NotifyService", "通知子服务名称");

// 通知子服务注册地址(须与 host:listen_port 一致, 客户端可访问)
DEFINE_string(server_addr, "127.0.0.1:8082", "通知子服务注册地址");

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

} // namespace

int main(int argc, char* argv[])
{
    // 1. 解析 gflags 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 2. 初始化日志记录(异步日志, 不阻塞业务线程)
    //    loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings logger_settings;
    logger_settings.async = true;
    logger_settings.loggerName = "notify_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("通知子服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}", FLAGS_logger_file, FLAGS_log_level);

    // 3. 组装邮箱客户端配置
    auto mail_settings = std::make_shared<chat_excel::notify_service::MailSettings>();
    mail_settings->username = FLAGS_smtp_username;
    mail_settings->password = FLAGS_smtp_password;
    mail_settings->from_email = FLAGS_from_email;
    mail_settings->smtp_server = FLAGS_smtp_server;
    INFO("邮箱客户端配置组装完成, SMTP 服务器: {} , 发送者邮件号: {}",
         FLAGS_smtp_server, FLAGS_from_email);

    // 4. 链式构建通知子服务服务器
    std::shared_ptr<chat_excel::notify_service::NotifyServer> notify_server =
        chat_excel::notify_service::NotifyServerBuilder()
            .SetMailSettings(mail_settings)
            .SetWorkerThreadCount(FLAGS_worker_thread_count)
            .SetListenPort(FLAGS_listen_port)
            .SetEtcdAddress(FLAGS_etcd_address)
            .SetServiceName(FLAGS_server_name)
            .SetServiceAddr(FLAGS_server_addr)
            .SetRegistryTtl(FLAGS_registry_ttl)
            .Build();
    if (notify_server == nullptr)
    {
        ERR("通知子服务服务器构建失败, 程序退出");
        return 1;
    }

    // 5. 启动服务器(阻塞), brpc 的 RunUntilAskedToQuit 内部处理 SIGINT/SIGTERM 信号,
    //    收到信号后 RunUntilAskedToQuit 返回, 服务器自动 Stop+Join
    notify_server->Start();

    INFO("通知子服务退出");
    return 0;
}
