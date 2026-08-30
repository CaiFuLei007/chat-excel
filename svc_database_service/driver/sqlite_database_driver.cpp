#include "svc_database_service/driver/sqlite_database_driver.h"

#include <memory>
#include <string>
#include <vector>

#include <cpp-toolkit/logger.h>

#include "common/exception.h"

namespace chat_excel
{

namespace
{

/**
 * @brief sqlite3_stmt 语句守护类, 作用域结束时自动释放语句对象
 */
class StatementGuard
{
public:
    explicit StatementGuard(sqlite3_stmt* statement) : statement_(statement)
    {
    }

    ~StatementGuard()
    {
        if (statement_ != nullptr)
        {
            sqlite3_finalize(statement_);
        }
    }

    StatementGuard(const StatementGuard&) = delete;

    StatementGuard& operator=(const StatementGuard&) = delete;

private:
    // 待释放的语句对象
    sqlite3_stmt* statement_;
};

/**
 * @brief 为预编译语句绑定参数, 按参数类型调用对应的绑定接口
 * @param statement 已预编译的语句对象
 * @param parameters 绑定参数集合
 * @return 参数列表为空返回 true(无需绑定), 绑定失败返回 false
 * @throws ChatExcelException 参数个数与占位符个数不匹配时抛出(DB_PARAM_BIND_FAILED)
 */
bool BindParameters(sqlite3_stmt* statement, const std::vector<ParameterWrapper>& parameters)
{
    // 校验参数个数与占位符个数一致(SQLite 参数下标从 1 开始)
    const int placeholder_count = sqlite3_bind_parameter_count(statement);
    if (placeholder_count != static_cast<int>(parameters.size()))
    {
        ERR("SQLite 参数个数与占位符个数不匹配, 占位符个数: {}, 参数个数: {}",
            placeholder_count,
            parameters.size());
        throw ChatExcelException(ErrorCode::DB_PARAM_BIND_FAILED);
    }
    for (size_t index = 0; index < parameters.size(); ++index)
    {
        const int parameter_index = static_cast<int>(index) + 1;
        const ParameterWrapper& parameter = parameters[index];
        int bind_status = SQLITE_OK;
        switch (parameter.GetParameterType())
        {
        case ParameterType::NULL_TYPE:
            bind_status = sqlite3_bind_null(statement, parameter_index);
            break;
        case ParameterType::INT:
            bind_status = sqlite3_bind_int64(statement, parameter_index, std::get<int64_t>(parameter.GetValue()));
            break;
        case ParameterType::DOUBLE:
            bind_status = sqlite3_bind_double(statement, parameter_index, std::get<double>(parameter.GetValue()));
            break;
        case ParameterType::STRING:
        {
            const std::string& value = std::get<std::string>(parameter.GetValue());
            // SQLITE_TRANSIENT 让 SQLite 拷贝字符串内容, 避免悬空指针
            bind_status = sqlite3_bind_text(statement,
                                            parameter_index,
                                            value.c_str(),
                                            static_cast<int>(value.size()),
                                            SQLITE_TRANSIENT);
            break;
        }
        case ParameterType::BOOL:
            bind_status = sqlite3_bind_int(statement, parameter_index, std::get<bool>(parameter.GetValue()) ? 1 : 0);
            break;
        default:
            ERR("SQLite 参数绑定失败 : 未知的参数类型, index: {}", index);
            return false;
        }
        if (bind_status != SQLITE_OK)
        {
            ERR("SQLite 参数绑定失败, index: {}, error: {}", index, sqlite3_errmsg(sqlite3_db_handle(statement)));
            return false;
        }
    }
    return true;
}

// SQLite 驱动自注册器, 动态库或可执行文件加载时向工厂注册驱动创建器
const bool kSQLiteDriverRegistered = []()
{
    DatabaseDriverFactory::RegisterDriver(DatabaseType::SQLITE,
                                          [](std::shared_ptr<DatabaseConfig> config)
                                          {
                                              return std::static_pointer_cast<DatabaseDriver>(
                                                  std::make_shared<SQLiteDatabaseDriver>(std::move(config)));
                                          });
    return true;
}();

} // namespace

SQLiteDatabaseDriver::SQLiteDatabaseDriver(std::shared_ptr<DatabaseConfig> config) : DatabaseDriver(std::move(config))
{
    if (config_ == nullptr || config_->GetDatabaseType() != DatabaseType::SQLITE)
    {
        ERR("创建 SQLite 驱动失败 : 数据库配置为空或配置类型不是 SQLite");
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }
}

SQLiteDatabaseDriver::~SQLiteDatabaseDriver()
{
    Disconnect();
}

void SQLiteDatabaseDriver::Connect()
{
    if (is_connected_)
    {
        WARN("SQLite 已处于连接状态, 忽略本次连接请求");
        return;
    }
    auto sqlite_config = std::static_pointer_cast<SQLiteConfig>(config_);
    if (!sqlite_config->CheckConfig())
    {
        ERR("SQLite 打开数据库失败 : 数据库配置非法, database_file_path: {}", sqlite_config->database_file_path);
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }

    // 释放可能残留的旧连接句柄
    if (connection_ != nullptr)
    {
        sqlite3_close_v2(connection_);
        connection_ = nullptr;
    }

    // 打开数据库连接, 文件不存在时自动创建
    const int open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(sqlite_config->database_file_path.c_str(), &connection_, open_flags, nullptr) != SQLITE_OK)
    {
        ERR("SQLite 打开数据库失败, database_file_path: {}, error: {}",
            sqlite_config->database_file_path,
            connection_ != nullptr ? sqlite3_errmsg(connection_) : "连接句柄分配失败");
        if (connection_ != nullptr)
        {
            sqlite3_close_v2(connection_);
            connection_ = nullptr;
        }
        throw ChatExcelException(ErrorCode::DB_CONNECTION_FAILED);
    }

    // 应用其他连接配置(PRAGMA 参数), 仅记录失败项不中断连接
    for (const auto& [key, value] : sqlite_config->other_config)
    {
        const std::string pragma_sql = "PRAGMA " + key + " = " + value + ";";
        if (sqlite3_exec(connection_, pragma_sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            WARN("SQLite 应用连接配置失败, key: {}, value: {}, error: {}", key, value, GetErrorMessage());
        }
    }
    is_connected_ = true;
    INFO("SQLite 打开数据库成功, database_file_path: {}", sqlite_config->database_file_path);
}

void SQLiteDatabaseDriver::Disconnect()
{
    if (connection_ != nullptr)
    {
        sqlite3_close_v2(connection_);
        connection_ = nullptr;
        INFO("SQLite 连接已关闭并释放连接资源");
    }
    is_connected_ = false;
}

bool SQLiteDatabaseDriver::TestConnection()
{
    if (!is_connected_ || connection_ == nullptr)
    {
        WARN("SQLite 连接检测失败 : 尚未建立连接");
        return false;
    }
    // 执行轻量查询语句检测连接可用性
    char* error_message = nullptr;
    const int exec_status = sqlite3_exec(connection_, "SELECT 1;", nullptr, nullptr, &error_message);
    if (error_message != nullptr)
    {
        ERR("SQLite 连接检测失败, error: {}", error_message);
        sqlite3_free(error_message);
    }
    return exec_status == SQLITE_OK;
}

std::string SQLiteDatabaseDriver::QuoteIdentifier(const std::string& identifier) const
{
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('"');
    for (const char character : identifier)
    {
        // 标识符内部的双引号双写转义
        if (character == '"')
        {
            quoted.push_back('"');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

QueryResult SQLiteDatabaseDriver::ExecuteQueryInternal(const std::string& sql)
{
    return ExecutePrepared(sql, {}, true);
}

QueryResult SQLiteDatabaseDriver::ExecuteUpdateInternal(const std::string& sql)
{
    return ExecutePrepared(sql, {}, false);
}

QueryResult SQLiteDatabaseDriver::ExecutePreparedQueryInternal(const std::string& sql,
                                                               const std::vector<ParameterWrapper>& parameters)
{
    return ExecutePrepared(sql, parameters, true);
}

QueryResult SQLiteDatabaseDriver::ExecutePreparedUpdateInternal(const std::string& sql,
                                                                const std::vector<ParameterWrapper>& parameters)
{
    return ExecutePrepared(sql, parameters, false);
}

std::string SQLiteDatabaseDriver::GetErrorMessage() const
{
    if (connection_ == nullptr)
    {
        return "SQLite 连接句柄未初始化";
    }
    const char* message = sqlite3_errmsg(connection_);
    return message != nullptr ? message : "";
}

QueryResult SQLiteDatabaseDriver::ExecutePrepared(const std::string& sql,
                                                  const std::vector<ParameterWrapper>& parameters,
                                                  bool need_result_set)
{
    CheckConnected(is_connected_);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_,
                           sql.c_str(),
                           static_cast<int>(sql.size()),
                           &statement,
                           nullptr) != SQLITE_OK)
    {
        ERR("SQLite 预编译语句失败, sql: {}, error: {}", sql, GetErrorMessage());
        return QueryResult(false, GetErrorMessage());
    }
    StatementGuard statement_guard(statement);

    // 绑定参数(参数个数为空时跳过绑定)
    if (!BindParameters(statement, parameters))
    {
        return QueryResult(false, GetErrorMessage());
    }

    QueryResult result;
    const int column_count = sqlite3_column_count(statement);
    if (need_result_set && column_count > 0)
    {
        // 填充列名与列类型(表达式列没有声明类型, 统一标记为 UNKNOWN)
        std::vector<std::string> column_names;
        std::vector<std::string> column_types;
        column_names.reserve(static_cast<size_t>(column_count));
        column_types.reserve(static_cast<size_t>(column_count));
        for (int index = 0; index < column_count; ++index)
        {
            const char* column_name = sqlite3_column_name(statement, index);
            const char* decltype_name = sqlite3_column_decltype(statement, index);
            column_names.emplace_back(column_name != nullptr ? column_name : "");
            column_types.emplace_back(decltype_name != nullptr ? decltype_name : "UNKNOWN");
        }
        result.SetColumns(std::move(column_names), std::move(column_types));

        // 逐步取回全部行数据
        while (true)
        {
            const int step_status = sqlite3_step(statement);
            if (step_status == SQLITE_ROW)
            {
                std::vector<std::string> row;
                row.reserve(static_cast<size_t>(column_count));
                for (int index = 0; index < column_count; ++index)
                {
                    if (sqlite3_column_type(statement, index) == SQLITE_NULL)
                    {
                        // NULL 值统一存储为字符串 "NULL"
                        row.emplace_back("NULL");
                        continue;
                    }
                    const unsigned char* value = sqlite3_column_text(statement, index);
                    const int value_length = sqlite3_column_bytes(statement, index);
                    row.emplace_back(value != nullptr ? reinterpret_cast<const char*>(value) : "",
                                     static_cast<size_t>(value_length));
                }
                result.AddRow(std::move(row));
                continue;
            }
            if (step_status == SQLITE_DONE)
            {
                break;
            }
            ERR("SQLite 执行查询语句失败, sql: {}, error: {}", sql, GetErrorMessage());
            return QueryResult(false, GetErrorMessage());
        }
    }
    else
    {
        // 修改语句逐步执行至完成
        while (true)
        {
            const int step_status = sqlite3_step(statement);
            if (step_status == SQLITE_ROW)
            {
                continue;
            }
            if (step_status == SQLITE_DONE)
            {
                break;
            }
            ERR("SQLite 执行修改语句失败, sql: {}, error: {}", sql, GetErrorMessage());
            return QueryResult(false, GetErrorMessage());
        }
    }
    result.SetAffectedRows(static_cast<uint64_t>(sqlite3_changes(connection_)));
    result.SetSuccess(true);
    INFO("SQLite 执行语句成功, sql: {}, 影响行数: {}", sql, result.GetAffectedRows());
    return result;
}

} // namespace chat_excel
