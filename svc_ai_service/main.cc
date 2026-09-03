#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <gflags/gflags.h>
#include <spdlog/common.h>
// brpc/server.h 须在 cpp-toolkit/redis.h 之前包含 : sw/redis++ 引入的 hiredis/read.h
// 会以宏定义 REDIS_REPLY_STRING 等, 与 brpc/redis_reply.h 的同名枚举值冲突
#include <brpc/server.h>
// aichat_sdk/base/util/mylog.h 定义的 TRACE/DBG/INFO/WARN/ERR/CRIT 宏与
// cpp-toolkit/logger.h 的同名宏冲突, 且其日志器需显式初始化才能使用,
// 包含后立即取消定义这些宏, 统一以 cpp-toolkit 的日志宏为准
#include <aichat_sdk/base/util/mylog.h>
#undef TRACE
#undef DBG
#undef INFO
#undef WARN
#undef ERR
#undef CRIT
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/util.h>
#include "svc_ai_service/ai_server.h"

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

// ChatSDK 本地 SQLite 数据库文件路径
DEFINE_string(chat_sdk_db_path, "aichat_sdk.db", "ChatSDK 本地 SQLite 数据库文件路径");

// ChatSDK 模型配置列表(JSON 数组), 数组元素字段 :
//   model_type(DEEPSEEK/GEMINI/CHATGPT), model_name, model_desc,
//   end_point, apikey, path, streampath, model, temperature, max_token, top_p,
//   reasoning_effort(deepseek), proxy{set_proxy, proxy_ip, proxy_port}
// 非必填字段缺省时使用 aichat_sdk::Config 的默认值
DEFINE_string(chat_sdk_models,
              R"([{"model_type" : "DEEPSEEK" , "model_name" : "deepseek-chat" , "model_desc" : "DeepSeek 对话模型" , "end_point" : "https://api.deepseek.com" , "apikey" : "your_apikey" , "path" : "/chat/completions" , "streampath" : "/chat/completions" , "model" : "deepseek-chat"}])",
              "ChatSDK 模型配置列表(JSON 数组)");

// AI 子服务监听端口
DEFINE_int32(listen_port, 8085, "AI 子服务监听端口");

// ETCD 注册中心地址
DEFINE_string(etcd_address, "http://127.0.0.1:2379", "ETCD 注册中心地址");

// AI 子服务名称
DEFINE_string(server_name, "AIService", "AI 子服务名称");

// AI 子服务注册地址(须与 host:listen_port 一致, 客户端可访问)
DEFINE_string(server_addr, "127.0.0.1:8085", "AI 子服务注册地址");

// 需要监控的子服务名称列表(逗号分隔), 用户子服务提供用户邮箱信息,
// 数据库子服务提供 Excel 对应的数据库表列表, 通知子服务负责邮件发送,
// 文件子服务会调用 AI 子服务的 UpdateSessionFile 接口更新会话文件映射表
DEFINE_string(care_service_names, "UserService,DataBaseService,NotifyService,FileService",
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
 * @brief 按逗号分隔字符串, 跳过空 token, 用于解析服务名称列表
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

/**
 * @brief 将模型类型字符串解析为 aichat_sdk 模型类型枚举值
 * @param model_type 模型类型字符串
 * @return 对应的模型类型枚举值, 无法识别时抛出异常
 */
aichat_sdk::ModelType ParseModelType(const std::string& model_type)
{
    if (model_type == "DEEPSEEK")
    {
        return aichat_sdk::ModelType::DEEPSEEK;
    }
    else if (model_type == "GEMINI")
    {
        return aichat_sdk::ModelType::GEMINI;
    }
    else if (model_type == "CHATGPT")
    {
        return aichat_sdk::ModelType::CHATGPT;
    }
    else
    {
        throw std::invalid_argument("无法识别的模型类型: " + model_type);
    }
}

/**
 * @brief 解析单个模型的 JSON 配置为 aichat_sdk 模型配置对象,
 *        model_type/model_name 为必填字段, 其余字段缺省时使用 Config 默认值
 * @param model_json 单个模型的 JSON 配置
 * @return 模型配置对象
 */
aichat_sdk::Config ParseModelConfig(const Json::Value& model_json)
{
    // 必填字段校验 : 模型类型与模型名称
    if (!model_json.isMember("model_type") || !model_json.isMember("model_name"))
    {
        throw std::invalid_argument("模型配置缺少必填字段 model_type/model_name");
    }

    aichat_sdk::Config config;
    config.model_type = ParseModelType(model_json["model_type"].asString());
    config.model_info.model_name = model_json["model_name"].asString();
    config.model_info.model_decs = model_json.get("model_desc", "").asString();
    config.end_point = model_json.get("end_point", "").asString();
    config.apikey = model_json.get("apikey", "").asString();
    config.path = model_json.get("path", "").asString();
    config.streampath = model_json.get("streampath", "").asString();
    config.model = model_json.get("model", "").asString();
    config.temperature = model_json.get("temperature", config.temperature).asDouble();
    config.max_token = model_json.get("max_token", config.max_token).asInt();
    config.top_p = model_json.get("top_p", config.top_p).asDouble();
    config.reasoning_effort = model_json.get("reasoning_effort", config.reasoning_effort).asString();

    // 代理配置为可选嵌套对象, 缺省时不启用代理
    if (model_json.isMember("proxy"))
    {
        const Json::Value& proxy_json = model_json["proxy"];
        config.proxy.set_proxy = proxy_json.get("set_proxy", false).asBool();
        config.proxy.proxy_ip = proxy_json.get("proxy_ip", "").asString();
        config.proxy.proxy_port = static_cast<uint16_t>(proxy_json.get("proxy_port", 0).asUInt());
    }
    return config;
}

/**
 * @brief 解析 ChatSDK 模型配置列表(JSON 数组字符串)为模型配置列表
 * @param models_json_str 模型配置列表 JSON 字符串
 * @return 模型配置列表
 */
std::vector<aichat_sdk::Config> ParseChatSdkModels(const std::string& models_json_str)
{
    Json::Value models_json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(models_json, models_json_str))
    {
        throw std::invalid_argument("ChatSDK 模型配置列表 JSON 解析失败");
    }
    if (!models_json.isArray() || models_json.empty())
    {
        throw std::invalid_argument("ChatSDK 模型配置列表必须为非空 JSON 数组");
    }

    std::vector<aichat_sdk::Config> models;
    models.reserve(models_json.size());
    for (const Json::Value& model_json : models_json)
    {
        models.push_back(ParseModelConfig(model_json));
    }
    return models;
}

} // namespace

int main(int argc, char* argv[])
{
    // 1. 解析 gflags 参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 2. 初始化日志记录(异步日志, 不阻塞业务线程)
    //    注意顺序 : aichat_sdk 与 cpp-toolkit 的 initLogger 都会调用 spdlog::init_thread_pool
    //    重置全局线程池(销毁旧线程池), 后初始化的一方会导致先创建的异步日志器失效,
    //    故 aichat_sdk 必须先初始化, cpp-toolkit 业务日志最后初始化, 保证业务日志可用;
    //    aichat_sdk 的静态 logger 不会自动初始化, 未初始化时模型注册/调用过程中的
    //    日志打印会因空指针导致段错误
    aichat_sdk::Logger::initLogger("aichat_sdk", FLAGS_logger_file, ParseLogLevel(FLAGS_log_level));

    //    loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings logger_settings;
    logger_settings.async = true;
    logger_settings.loggerName = "ai_service";
    logger_settings.loggerFile = FLAGS_logger_file;
    logger_settings.logLevel = ParseLogLevel(FLAGS_log_level);
    cpp_toolkit::Logger::initLogger(logger_settings);
    INFO("AI 子服务启动, 日志初始化完成, 日志输出位置: {} , 日志级别: {}", FLAGS_logger_file, FLAGS_log_level);

    // 3. 组装 MySQL 配置
    auto mysql_settings = std::make_shared<cpp_toolkit::MySQLSettings>();
    mysql_settings->host = FLAGS_mysql_host;
    mysql_settings->port = FLAGS_mysql_port;
    mysql_settings->user = FLAGS_mysql_user;
    mysql_settings->password = FLAGS_mysql_password;
    mysql_settings->database = FLAGS_mysql_database;
    mysql_settings->charset = FLAGS_mysql_charset;
    mysql_settings->connection_pool_size = FLAGS_mysql_connection_pool_size;
    INFO("MySQL 配置组装完成, 地址: {} , 端口: {} , 数据库: {}", FLAGS_mysql_host, FLAGS_mysql_port, FLAGS_mysql_database);

    // 4. 组装 Redis 配置
    auto redis_settings = std::make_shared<cpp_toolkit::RedisSettings>();
    redis_settings->host = FLAGS_redis_host;
    redis_settings->port = FLAGS_redis_port;
    redis_settings->user = FLAGS_redis_user;
    redis_settings->password = FLAGS_redis_password;
    redis_settings->db = FLAGS_redis_db;
    redis_settings->pool_connections_size = FLAGS_redis_pool_connections_size;
    INFO("Redis 配置组装完成, 地址: {} , 端口: {}", FLAGS_redis_host, FLAGS_redis_port);

    // 5. 组装 ChatSDK 配置信息(本地数据库路径 + 模型配置列表)
    auto chat_sdk_settings = std::make_shared<chat_excel::ai_service::ChatSdkSettings>();
    chat_sdk_settings->db_path = FLAGS_chat_sdk_db_path;
    try
    {
        chat_sdk_settings->models = ParseChatSdkModels(FLAGS_chat_sdk_models);
    }
    catch (const std::exception& exception)
    {
        ERR("ChatSDK 模型配置解析失败: {}", exception.what());
        return 1;
    }
    INFO("ChatSDK 配置组装完成, 本地数据库路径: {} , 模型数量: {}",
         chat_sdk_settings->db_path, chat_sdk_settings->models.size());

    // 6. 组装 ETCD 注册中心配置信息(注册中心地址/服务名称/服务地址/服务注册 TTL)
    chat_excel::ai_service::EtcdSettings etcd_settings;
    etcd_settings.etcd_center_addr = FLAGS_etcd_address;
    etcd_settings.service_name = FLAGS_server_name;
    etcd_settings.service_addr = FLAGS_server_addr;
    etcd_settings.registry_ttl = FLAGS_registry_ttl;
    INFO("ETCD 注册中心配置组装完成, 地址: {} , 服务: {} -> {}",
         FLAGS_etcd_address, FLAGS_server_name, FLAGS_server_addr);

    // 7. 解析需要监控的子服务名称列表
    std::vector<std::string> care_service_names = SplitCommaSeparated(FLAGS_care_service_names);
    INFO("服务发现配置完成, 监控服务数量: {}", care_service_names.size());

    // 8. 链式构建 AI 子服务服务器
    std::shared_ptr<chat_excel::ai_service::AiServer> ai_server =
        chat_excel::ai_service::AiServerBuilder()
            .SetMysqlSettings(mysql_settings)
            .SetRedisSettings(redis_settings)
            .SetChatSdkSettings(chat_sdk_settings)
            .SetListenPort(FLAGS_listen_port)
            .SetEtcdSettings(etcd_settings)
            .SetCareServiceNames(care_service_names)
            .Build();
    if (ai_server == nullptr)
    {
        ERR("AI 子服务服务器构建失败, 程序退出");
        return 1;
    }

    // 9. 启动服务器(阻塞), brpc 的 RunUntilAskedToQuit 内部处理 SIGINT/SIGTERM 信号,
    //    收到信号后 RunUntilAskedToQuit 返回, 服务器自动 Stop+Join
    ai_server->Start();

    INFO("AI 子服务退出");
    return 0;
}
