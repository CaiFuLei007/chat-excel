#include "svc_database_service/driver/mysql_database_driver.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <cpp-toolkit/logger.h>

#include "common/exception.h"
#include "svc_database_service/driver/sql_validator.h"

namespace chat_excel
{

namespace
{

// 结果集列缓冲区的初始字节长度, 发生截断时按实际长度重新获取
constexpr unsigned long kInitialColumnBufferLength = 256;

// MySQL 连接使用的字符集
constexpr const char* kMySQLCharset = "utf8mb4";

// MySQL 查询所有表名语句
constexpr const char* kMySQLListTablesSql = "SHOW TABLES";

// MySQL DESC 结果 Extra 列中标识自增列的关键字
constexpr const char* kMySQLAutoIncrementKeyword = "auto_increment";

// MySQL 布尔列类型(Excel BOOLEAN 类型转化后的类型)
constexpr const char* kMySQLBooleanColumnType = "BOOLEAN";

// MySQL DESC 语句对 BOOLEAN 列返回的实际类型文本
constexpr const char* kMySQLBooleanDescType = "tinyint(1)";

// MySQL 文本列类型(Excel DATE 类型转化后的类型)
constexpr const char* kMySQLDateColumnType = "TEXT";

// Excel 解析列类型 : 布尔类型
constexpr const char* kExcelBooleanColumnType = "BOOLEAN";

// Excel 解析列类型 : 日期类型
constexpr const char* kExcelDateColumnType = "DATE";

/**
 * @brief MYSQL_RES 结果集守护类, 作用域结束时自动释放结果集
 */
class ResultSetGuard
{
public:
    explicit ResultSetGuard(MYSQL_RES* result_set) : result_set_(result_set)
    {
    }

    ~ResultSetGuard()
    {
        if (result_set_ != nullptr)
        {
            mysql_free_result(result_set_);
        }
    }

    ResultSetGuard(const ResultSetGuard&) = delete;

    ResultSetGuard& operator=(const ResultSetGuard&) = delete;

private:
    // 待释放的结果集
    MYSQL_RES* result_set_;
};

/**
 * @brief MYSQL_STMT 语句守护类, 作用域结束时自动关闭语句句柄
 */
class StatementGuard
{
public:
    explicit StatementGuard(MYSQL_STMT* statement) : statement_(statement)
    {
    }

    ~StatementGuard()
    {
        if (statement_ != nullptr)
        {
            mysql_stmt_close(statement_);
        }
    }

    StatementGuard(const StatementGuard&) = delete;

    StatementGuard& operator=(const StatementGuard&) = delete;

private:
    // 待关闭的语句句柄
    MYSQL_STMT* statement_;
};

/**
 * @brief 预编译语句参数绑定上下文, 保存参数值内存,
 *        保证 mysql_stmt_bind_param 到 mysql_stmt_execute 期间指针有效
 */
struct ParameterBindContext
{
    // 参数绑定描述数组, 与参数顺序一一对应
    std::vector<MYSQL_BIND> binds;

    // 整数参数值存储
    std::vector<int64_t> int_values;

    // 浮点数参数值存储
    std::vector<double> double_values;

    // 字符串参数值存储
    std::vector<std::string> string_values;

    // 布尔参数值存储(单字节缓冲)
    std::vector<char> bool_values;

    // 字符串参数实际长度存储
    std::vector<unsigned long> string_lengths;

    // NULL 指示符存储
    std::unique_ptr<bool[]> null_flags;
};

/**
 * @brief 将 MySQL 字段类型转换为可读的类型名称字符串
 * @param field_type MySQL 字段类型枚举
 * @return 类型名称字符串, 未知类型返回 "UNKNOWN"
 */
std::string ConvertFieldTypeToString(enum enum_field_types field_type)
{
    switch (field_type)
    {
    case MYSQL_TYPE_TINY:
        return "TINYINT";
    case MYSQL_TYPE_SHORT:
        return "SMALLINT";
    case MYSQL_TYPE_LONG:
        return "INT";
    case MYSQL_TYPE_LONGLONG:
        return "BIGINT";
    case MYSQL_TYPE_FLOAT:
        return "FLOAT";
    case MYSQL_TYPE_DOUBLE:
        return "DOUBLE";
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
        return "DECIMAL";
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
        return "VARCHAR";
    case MYSQL_TYPE_STRING:
        return "CHAR";
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
        return "TEXT";
    case MYSQL_TYPE_DATE:
        return "DATE";
    case MYSQL_TYPE_DATETIME:
        return "DATETIME";
    case MYSQL_TYPE_TIMESTAMP:
        return "TIMESTAMP";
    case MYSQL_TYPE_TIME:
        return "TIME";
    case MYSQL_TYPE_YEAR:
        return "YEAR";
    case MYSQL_TYPE_BIT:
        return "BIT";
    case MYSQL_TYPE_JSON:
        return "JSON";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief 构建预编译语句的参数绑定描述, 将包装参数统一填充到绑定上下文中
 * @param parameters 绑定参数集合
 * @param context 参数绑定上下文(输出)
 * @return 参数列表为空返回 true(无需绑定), 构建失败返回 false
 */
bool BuildParameterBinds(const std::vector<ParameterWrapper>& parameters, ParameterBindContext& context)
{
    if (parameters.empty())
    {
        return true;
    }
    const size_t parameter_count = parameters.size();
    context.binds.resize(parameter_count);
    context.int_values.resize(parameter_count);
    context.double_values.resize(parameter_count);
    context.string_values.resize(parameter_count);
    context.bool_values.resize(parameter_count);
    context.string_lengths.assign(parameter_count, 0);
    context.null_flags.reset(new bool[parameter_count]());

    for (size_t index = 0; index < parameter_count; ++index)
    {
        const ParameterWrapper& parameter = parameters[index];
        MYSQL_BIND& bind = context.binds[index];
        std::memset(&bind, 0, sizeof(bind));
        switch (parameter.GetParameterType())
        {
        case ParameterType::NULL_TYPE:
            context.null_flags[index] = true;
            bind.buffer_type = MYSQL_TYPE_NULL;
            bind.is_null = &context.null_flags[index];
            break;
        case ParameterType::INT:
            context.int_values[index] = std::get<int64_t>(parameter.GetValue());
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
            bind.buffer = &context.int_values[index];
            bind.is_unsigned = false;
            break;
        case ParameterType::DOUBLE:
            context.double_values[index] = std::get<double>(parameter.GetValue());
            bind.buffer_type = MYSQL_TYPE_DOUBLE;
            bind.buffer = &context.double_values[index];
            break;
        case ParameterType::STRING:
            context.string_values[index] = std::get<std::string>(parameter.GetValue());
            context.string_lengths[index] = context.string_values[index].size();
            bind.buffer_type = MYSQL_TYPE_STRING;
            bind.buffer = context.string_values[index].data();
            bind.buffer_length = context.string_values[index].size();
            bind.length = &context.string_lengths[index];
            break;
        case ParameterType::BOOL:
            context.bool_values[index] = std::get<bool>(parameter.GetValue()) ? 1 : 0;
            bind.buffer_type = MYSQL_TYPE_TINY;
            bind.buffer = &context.bool_values[index];
            bind.is_unsigned = false;
            break;
        default:
            ERR("构建 MySQL 参数绑定失败 : 未知的参数类型, index: {}", index);
            return false;
        }
    }
    return true;
}

// MySQL 驱动自注册器, 动态库或可执行文件加载时向工厂注册驱动创建器
const bool kMySQLDriverRegistered = []()
{
    DatabaseDriverFactory::RegisterDriver(DatabaseType::MYSQL,
                                          [](std::shared_ptr<DatabaseConfig> config)
                                          {
                                              return std::static_pointer_cast<DatabaseDriver>(
                                                  std::make_shared<MySQLDatabaseDriver>(std::move(config)));
                                          });
    return true;
}();

} // namespace

MySQLDatabaseDriver::MySQLDatabaseDriver(std::shared_ptr<DatabaseConfig> config) : DatabaseDriver(std::move(config))
{
    if (config_ == nullptr || config_->GetDatabaseType() != DatabaseType::MYSQL)
    {
        ERR("创建 MySQL 驱动失败 : 数据库配置为空或配置类型不是 MySQL");
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }
}

MySQLDatabaseDriver::~MySQLDatabaseDriver()
{
    Disconnect();
}

void MySQLDatabaseDriver::Connect()
{
    if (is_connected_)
    {
        WARN("MySQL 已处于连接状态, 忽略本次连接请求");
        return;
    }
    auto mysql_config = std::static_pointer_cast<MySQLConfig>(config_);
    if (!mysql_config->CheckConfig())
    {
        ERR("MySQL 建立连接失败 : 数据库配置非法, host: {}, port: {}", mysql_config->host, mysql_config->port);
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }

    // 释放可能残留的旧连接句柄
    if (connection_ != nullptr)
    {
        mysql_close(connection_);
        connection_ = nullptr;
    }

    connection_ = mysql_init(nullptr);
    if (connection_ == nullptr)
    {
        ERR("MySQL 初始化连接句柄失败");
        throw ChatExcelException(ErrorCode::DB_CONNECTION_FAILED);
    }

    // 设置字符集与自动重连
    if (mysql_options(connection_, MYSQL_SET_CHARSET_NAME, kMySQLCharset) != 0)
    {
        ERR("MySQL 设置字符集失败, charset: {}, error: {}", kMySQLCharset, GetErrorMessage());
        mysql_close(connection_);
        connection_ = nullptr;
        throw ChatExcelException(ErrorCode::DB_CONNECTION_FAILED);
    }
    bool reconnect = true;
    if (mysql_options(connection_, MYSQL_OPT_RECONNECT, &reconnect) != 0)
    {
        ERR("MySQL 设置自动重连失败, error: {}", GetErrorMessage());
        mysql_close(connection_);
        connection_ = nullptr;
        throw ChatExcelException(ErrorCode::DB_CONNECTION_FAILED);
    }

    // 按需启用 SSL 连接
    if (mysql_config->use_ssl)
    {
        mysql_ssl_set(connection_,
                      mysql_config->ssl_config.key_path.c_str(),
                      mysql_config->ssl_config.cert_path.c_str(),
                      mysql_config->ssl_config.ca_cert_path.c_str(),
                      nullptr,
                      nullptr);
        INFO("MySQL 已启用 SSL 连接配置, cert_path: {}, key_path: {}, ca_cert_path: {}",
             mysql_config->ssl_config.cert_path,
             mysql_config->ssl_config.key_path,
             mysql_config->ssl_config.ca_cert_path);
    }

    // 记录其他连接配置, 当前版本暂不逐项应用
    for (const auto& [key, value] : mysql_config->other_config)
    {
        INFO("MySQL 其他连接配置项, key: {}, value: {}", key, value);
    }

    if (mysql_real_connect(connection_,
                           mysql_config->host.c_str(),
                           mysql_config->user_name.c_str(),
                           mysql_config->password.c_str(),
                           mysql_config->database_name.c_str(),
                           mysql_config->port,
                           nullptr,
                           0) == nullptr)
    {
        ERR("MySQL 建立连接失败, host: {}, port: {}, user_name: {}, error: {}",
              mysql_config->host,
              mysql_config->port,
              mysql_config->user_name,
              GetErrorMessage());
        mysql_close(connection_);
        connection_ = nullptr;
        throw ChatExcelException(ErrorCode::DB_CONNECTION_FAILED);
    }
    is_connected_ = true;
    INFO("MySQL 建立连接成功, host: {}, port: {}, database_name: {}",
         mysql_config->host,
         mysql_config->port,
         mysql_config->database_name);
}

void MySQLDatabaseDriver::Disconnect()
{
    if (connection_ != nullptr)
    {
        mysql_close(connection_);
        connection_ = nullptr;
        INFO("MySQL 连接已断开并释放连接资源");
    }
    is_connected_ = false;
}

bool MySQLDatabaseDriver::TestConnection()
{
    if (!is_connected_ || connection_ == nullptr)
    {
        WARN("MySQL 连接检测失败 : 尚未建立连接");
        return false;
    }
    // 心跳检测, 期间断开的连接会被自动重连
    if (mysql_ping(connection_) != 0)
    {
        ERR("MySQL 连接检测失败, error: {}", GetErrorMessage());
        return false;
    }
    return true;
}

std::string MySQLDatabaseDriver::QuoteIdentifier(const std::string& identifier) const
{
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('`');
    for (const char character : identifier)
    {
        // 标识符内部的反引号双写转义
        if (character == '`')
        {
            quoted.push_back('`');
        }
        quoted.push_back(character);
    }
    quoted.push_back('`');
    return quoted;
}

std::vector<std::string> MySQLDatabaseDriver::GetAllTablesName()
{
    CheckConnected(is_connected_);
    QueryResult result = ExecuteQuery(kMySQLListTablesSql);
    if (!result.IsSuccess())
    {
        ERR("MySQL 查询所有表名失败, 错误: {}", result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }

    // SHOW TABLES 结果集中表名位于第一列
    std::vector<std::string> table_names;
    table_names.reserve(result.GetRowCount());
    for (size_t row_index = 0; row_index < result.GetRowCount(); ++row_index)
    {
        table_names.push_back(result.GetRow(row_index)[0]);
    }
    INFO("MySQL 查询所有表名成功, 表数量: {}", table_names.size());
    return table_names;
}

TableInfo MySQLDatabaseDriver::GetTableStructure(const std::string& table_name)
{
    CheckConnected(is_connected_);

    // 校验表名合法性, 非法表名拼入查询语句会带来 SQL 注入风险
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("MySQL 获取表结构失败, 表名非法, table_name: {}", table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }

    // DESC 结果列 : Field, Type, Null, Key, Default, Extra
    QueryResult result = ExecuteQuery("DESC " + QuoteIdentifier(table_name));
    if (!result.IsSuccess())
    {
        ERR("MySQL 查询表结构失败, table_name: {}, 错误: {}", table_name, result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }

    TableInfo table_info;
    table_info.name = table_name;
    for (size_t row_index = 0; row_index < result.GetRowCount(); ++row_index)
    {
        const std::vector<std::string>& row = result.GetRow(row_index);
        ColumnInfo column_info;
        column_info.name = GetRowColumnValue(row, 0);                      // Field
        // BOOLEAN 列在 MySQL 中实际为 TINYINT(1), DESC 返回 tinyint(1),
        // 映射回 BOOLEAN 供上层按布尔类型处理(前端展示 true/false)
        std::string column_type = GetRowColumnValue(row, 1);               // Type
        if (column_type == kMySQLBooleanDescType)
        {
            column_type = kExcelBooleanColumnType;
        }
        column_info.type = column_type;
        column_info.nullable = GetRowColumnValue(row, 2) == "YES";         // Null
        column_info.is_primary_key = GetRowColumnValue(row, 3) == "PRI";   // Key
        column_info.default_value = GetRowColumnValue(row, 4);             // Default
        // Extra 中包含 auto_increment 表示自增列
        column_info.auto_increment =
            GetRowColumnValue(row, 5).find(kMySQLAutoIncrementKeyword) != std::string::npos;
        table_info.columns.push_back(std::move(column_info));
    }
    INFO("MySQL 查询表结构成功, table_name: {}, 列数: {}", table_name, table_info.columns.size());
    return table_info;
}

std::string MySQLDatabaseDriver::ConvertExcelColumnType(const std::string& excel_type) const
{
    // BOOLEAN 类型保留为 BOOLEAN(MySQL 中等价于 TINYINT(1)), 便于前端按布尔类型展示;
    // DATE 类型转化为 TEXT 类型, 其余类型原样返回
    if (excel_type == kExcelBooleanColumnType)
    {
        return kMySQLBooleanColumnType;
    }
    if (excel_type == kExcelDateColumnType)
    {
        return kMySQLDateColumnType;
    }
    return excel_type;
}

QueryResult MySQLDatabaseDriver::ExecuteQueryInternal(const std::string& sql)
{
    CheckConnected(is_connected_);
    if (mysql_query(connection_, sql.c_str()) != 0)
    {
        ERR("MySQL 执行查询语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    MYSQL_RES* result_set = mysql_store_result(connection_);
    if (result_set == nullptr)
    {
        // 只读语句必然返回结果集, 结果集为空说明获取失败
        if (mysql_field_count(connection_) > 0)
        {
            ERR("MySQL 获取查询结果集失败, sql: {}, error: {}", sql, GetErrorMessage());
            return QueryResult(false, GetErrorMessage());
        }
        return QueryResult(true);
    }
    ResultSetGuard result_set_guard(result_set);

    const unsigned int column_count = mysql_num_fields(result_set);
    MYSQL_FIELD* fields = mysql_fetch_fields(result_set);
    std::vector<std::string> column_names;
    std::vector<std::string> column_types;
    column_names.reserve(column_count);
    column_types.reserve(column_count);
    for (unsigned int index = 0; index < column_count; ++index)
    {
        column_names.emplace_back(fields[index].name != nullptr ? fields[index].name : "");
        column_types.push_back(ConvertFieldTypeToString(fields[index].type));
    }

    QueryResult result;
    result.SetColumns(std::move(column_names), std::move(column_types));
    while (MYSQL_ROW row_data = mysql_fetch_row(result_set))
    {
        const unsigned long* row_lengths = mysql_fetch_lengths(result_set);
        std::vector<std::string> row;
        row.reserve(column_count);
        for (unsigned int index = 0; index < column_count; ++index)
        {
            // NULL 值统一存储为字符串 "NULL"
            if (row_data[index] == nullptr)
            {
                row.emplace_back("NULL");
            }
            else
            {
                row.emplace_back(row_data[index], row_lengths[index]);
            }
        }
        result.AddRow(std::move(row));
    }
    result.SetAffectedRows(mysql_affected_rows(connection_));
    result.SetSuccess(true);
    INFO("MySQL 执行查询语句成功, sql: {}, 行数: {}", sql, result.GetRowCount());
    return result;
}

QueryResult MySQLDatabaseDriver::ExecuteUpdateInternal(const std::string& sql)
{
    CheckConnected(is_connected_);
    if (mysql_query(connection_, sql.c_str()) != 0)
    {
        ERR("MySQL 执行修改语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    QueryResult result;
    result.SetAffectedRows(mysql_affected_rows(connection_));
    result.SetSuccess(true);
    INFO("MySQL 执行修改语句成功, sql: {}, 影响行数: {}", sql, result.GetAffectedRows());
    return result;
}

QueryResult MySQLDatabaseDriver::ExecutePreparedQueryInternal(const std::string& sql,
                                                              const std::vector<ParameterWrapper>& parameters)
{
    return ExecutePrepared(sql, parameters, true);
}

QueryResult MySQLDatabaseDriver::ExecutePreparedUpdateInternal(const std::string& sql,
                                                               const std::vector<ParameterWrapper>& parameters)
{
    return ExecutePrepared(sql, parameters, false);
}

std::string MySQLDatabaseDriver::GetErrorMessage() const
{
    if (connection_ == nullptr)
    {
        return "MySQL 连接句柄未初始化";
    }
    const char* message = mysql_error(connection_);
    return message != nullptr ? message : "";
}

QueryResult MySQLDatabaseDriver::ExecutePrepared(const std::string& sql,
                                                 const std::vector<ParameterWrapper>& parameters,
                                                 bool need_result_set)
{
    CheckConnected(is_connected_);
    MYSQL_STMT* statement = mysql_stmt_init(connection_);
    if (statement == nullptr)
    {
        ERR("MySQL 初始化预编译语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    StatementGuard statement_guard(statement);

    if (mysql_stmt_prepare(statement, sql.c_str(), sql.size()) != 0)
    {
        ERR("MySQL 预编译语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }

    // 校验参数个数与占位符个数一致
    const unsigned long placeholder_count = mysql_stmt_param_count(statement);
    if (placeholder_count != parameters.size())
    {
        ERR("MySQL 参数个数与占位符个数不匹配, sql: {}, 占位符个数: {}, 参数个数: {}",
              sql,
              placeholder_count,
              parameters.size());
        throw ChatExcelException(ErrorCode::DB_PARAM_BIND_FAILED);
    }

    // 绑定参数并执行
    ParameterBindContext bind_context;
    if (!BuildParameterBinds(parameters, bind_context))
    {
        return QueryResult(false, "MySQL 参数绑定失败 : 未知的参数类型");
    }
    if (!parameters.empty() && mysql_stmt_bind_param(statement, bind_context.binds.data()) != 0)
    {
        ERR("MySQL 绑定预编译语句参数失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    if (mysql_stmt_execute(statement) != 0)
    {
        ERR("MySQL 执行预编译语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }

    if (need_result_set)
    {
        return FetchPreparedResult(statement);
    }
    QueryResult result;
    result.SetAffectedRows(mysql_stmt_affected_rows(statement));
    result.SetSuccess(true);
    INFO("MySQL 执行预编译修改语句成功, sql: {}, 影响行数: {}", sql, result.GetAffectedRows());
    return result;
}

QueryResult MySQLDatabaseDriver::FetchPreparedResult(MYSQL_STMT* statement)
{
    MYSQL_RES* metadata = mysql_stmt_result_metadata(statement);
    if (metadata == nullptr)
    {
        ERR("MySQL 获取预编译语句结果集元数据失败, error: {}", GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    ResultSetGuard metadata_guard(metadata);

    const unsigned int column_count = mysql_num_fields(metadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    std::vector<std::string> column_names;
    std::vector<std::string> column_types;
    column_names.reserve(column_count);
    column_types.reserve(column_count);
    for (unsigned int index = 0; index < column_count; ++index)
    {
        column_names.emplace_back(fields[index].name != nullptr ? fields[index].name : "");
        column_types.push_back(ConvertFieldTypeToString(fields[index].type));
    }
    QueryResult result;
    result.SetColumns(std::move(column_names), std::move(column_types));
    if (mysql_stmt_field_count(statement) == 0)
    {
        // 语句没有返回结果集, 直接返回空的成功结果
        result.SetSuccess(true);
        return result;
    }

    // 每列分配初始缓冲区, 统一按字符串类型绑定
    std::vector<std::vector<char>> column_buffers(column_count,
                                                  std::vector<char>(kInitialColumnBufferLength));
    std::vector<unsigned long> column_lengths(column_count, 0);
    std::unique_ptr<bool[]> null_flags(new bool[column_count]());
    std::vector<MYSQL_BIND> result_binds(column_count);
    for (unsigned int index = 0; index < column_count; ++index)
    {
        MYSQL_BIND& bind = result_binds[index];
        std::memset(&bind, 0, sizeof(bind));
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = column_buffers[index].data();
        bind.buffer_length = column_buffers[index].size();
        bind.length = &column_lengths[index];
        bind.is_null = &null_flags[index];
    }
    if (mysql_stmt_bind_result(statement, result_binds.data()) != 0)
    {
        ERR("MySQL 绑定预编译语句结果集失败, error: {}", GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    if (mysql_stmt_store_result(statement) != 0)
    {
        ERR("MySQL 缓存预编译语句结果集失败, error: {}", GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }

    // 逐行获取结果, 截断的列按实际长度重新获取
    while (true)
    {
        const int fetch_status = mysql_stmt_fetch(statement);
        if (fetch_status == MYSQL_NO_DATA)
        {
            break;
        }
        std::vector<std::string> row;
        row.reserve(column_count);
        for (unsigned int index = 0; index < column_count; ++index)
        {
            if (null_flags[index])
            {
                // NULL 值统一存储为字符串 "NULL"
                row.emplace_back("NULL");
                continue;
            }
            if (fetch_status == MYSQL_DATA_TRUNCATED && column_lengths[index] >= column_buffers[index].size())
            {
                // 该列发生截断, 按实际长度扩容缓冲区后重新获取该列数据
                column_buffers[index].resize(column_lengths[index] + 1);
                result_binds[index].buffer = column_buffers[index].data();
                result_binds[index].buffer_length = column_buffers[index].size();
                if (mysql_stmt_fetch_column(statement, &result_binds[index], index, 0) != 0)
                {
                    ERR("MySQL 重新获取截断列数据失败, 列下标: {}, error: {}", index, GetErrorMessage());
                    return QueryResult(false, GetErrorMessage());
                }
            }
            row.emplace_back(column_buffers[index].data(), column_lengths[index]);
        }
        result.AddRow(std::move(row));
    }
    result.SetAffectedRows(mysql_stmt_affected_rows(statement));
    result.SetSuccess(true);
    INFO("MySQL 执行预编译查询语句成功, 行数: {}", result.GetRowCount());
    return result;
}

} // namespace chat_excel
