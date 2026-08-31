#include "svc_database_service/driver/database_driver.h"

#include <cpp-toolkit/logger.h>

#include "common/exception.h"
#include "svc_database_service/driver/sql_validator.h"

namespace chat_excel
{

namespace
{

// 开启事务的基础语句
constexpr const char* kBeginTransactionSql = "BEGIN";

// 提交事务的基础语句
constexpr const char* kCommitTransactionSql = "COMMIT";

// 回滚事务的基础语句
constexpr const char* kRollbackTransactionSql = "ROLLBACK";

} // namespace

DatabaseDriver::DatabaseDriver(std::shared_ptr<DatabaseConfig> config) : config_(std::move(config))
{
}

QueryResult DatabaseDriver::ExecuteQuery(const std::string& sql)
{
    ValidateQuerySql(sql);
    return ExecuteQueryInternal(sql);
}

QueryResult DatabaseDriver::ExecuteUpdate(const std::string& sql)
{
    ValidateUpdateSql(sql);
    return ExecuteUpdateInternal(sql);
}

QueryResult DatabaseDriver::ExecutePreparedQuery(const std::string& sql,
                                                 const std::vector<ParameterWrapper>& parameters)
{
    ValidateQuerySql(sql);
    return ExecutePreparedQueryInternal(sql, parameters);
}

QueryResult DatabaseDriver::ExecutePreparedUpdate(const std::string& sql,
                                                  const std::vector<ParameterWrapper>& parameters)
{
    ValidateUpdateSql(sql);
    return ExecutePreparedUpdateInternal(sql, parameters);
}

QueryResult DatabaseDriver::BeginTransaction()
{
    return ExecuteUpdate(kBeginTransactionSql);
}

QueryResult DatabaseDriver::CommitTransaction()
{
    return ExecuteUpdate(kCommitTransactionSql);
}

QueryResult DatabaseDriver::RollbackTransaction()
{
    return ExecuteUpdate(kRollbackTransactionSql);
}

DatabaseType DatabaseDriver::GetDatabaseType() const
{
    return config_->GetDatabaseType();
}

const std::shared_ptr<DatabaseConfig>& DatabaseDriver::GetConfig() const
{
    return config_;
}

QueryResult DatabaseDriver::DropTable(const std::string& table_name)
{
    // 删表属于受控管理操作, 仅校验表名合法性, 不进行危险关键字校验
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("删除数据表失败, 表名非法, table_name: {}", table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }
    return ExecuteUpdateInternal("DROP TABLE " + QuoteIdentifier(table_name));
}

void DatabaseDriver::CheckConnected(bool connected) const
{
    if (!connected)
    {
        ERR("数据库尚未建立连接, 请先调用 Connect");
        throw ChatExcelException(ErrorCode::DB_NOT_CONNECTED);
    }
}

void DatabaseDriver::ValidateQuerySql(const std::string& sql) const
{
    // 基础合法性校验(空语句, 危险操作, 多条语句, 支持的类型)
    if (SQLValidator::TrimSql(sql).empty())
    {
        ERR("SQL 语句为空");
        throw ChatExcelException(ErrorCode::DB_SQL_EMPTY);
    }
    if (SQLValidator::ContainsDangerousOperation(sql))
    {
        ERR("SQL 语句包含危险操作, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_DANGEROUS);
    }
    if (SQLValidator::ContainsMultipleStatements(sql))
    {
        ERR("SQL 语句包含多条语句, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_MULTIPLE_STATEMENTS);
    }
    if (!SQLValidator::IsValidSql(sql))
    {
        ERR("SQL 语句类型不受支持, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_INVALID);
    }
    // 查询通路仅允许只读语句
    if (!SQLValidator::IsReadOnlySql(sql))
    {
        ERR("查询通路仅支持只读 SQL 语句, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_INVALID);
    }
}

void DatabaseDriver::ValidateUpdateSql(const std::string& sql) const
{
    // 基础合法性校验(空语句, 危险操作, 多条语句, 支持的类型)
    if (SQLValidator::TrimSql(sql).empty())
    {
        ERR("SQL 语句为空");
        throw ChatExcelException(ErrorCode::DB_SQL_EMPTY);
    }
    if (SQLValidator::ContainsDangerousOperation(sql))
    {
        ERR("SQL 语句包含危险操作, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_DANGEROUS);
    }
    if (SQLValidator::ContainsMultipleStatements(sql))
    {
        ERR("SQL 语句包含多条语句, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_MULTIPLE_STATEMENTS);
    }
    if (!SQLValidator::IsValidSql(sql))
    {
        ERR("SQL 语句类型不受支持, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_INVALID);
    }
    // 修改通路允许修改类语句与事务控制语句
    if (!SQLValidator::IsModifySql(sql) && !SQLValidator::IsTransactionStatement(sql))
    {
        ERR("修改通路仅支持修改类 SQL 语句或事务控制语句, sql: {}", sql);
        throw ChatExcelException(ErrorCode::DB_SQL_INVALID);
    }
}

void DatabaseDriverFactory::RegisterDriver(DatabaseType database_type, DriverCreator creator)
{
    auto& registry = GetRegistry();
    registry[database_type] = std::move(creator);
}

std::shared_ptr<DatabaseDriver> DatabaseDriverFactory::CreateDriver(std::shared_ptr<DatabaseConfig> config)
{
    if (config == nullptr)
    {
        ERR("创建数据库驱动失败 : 数据库配置为空");
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }
    if (!config->CheckConfig())
    {
        ERR("创建数据库驱动失败 : 数据库配置非法");
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }
    auto& registry = GetRegistry();
    auto iterator = registry.find(config->GetDatabaseType());
    if (iterator == registry.end())
    {
        ERR("创建数据库驱动失败 : 数据库类型未注册, database_type: {}", static_cast<int>(config->GetDatabaseType()));
        throw ChatExcelException(ErrorCode::DB_UNSUPPORTED_DATABASE_TYPE);
    }
    return iterator->second(std::move(config));
}

std::unordered_map<DatabaseType, DatabaseDriverFactory::DriverCreator>& DatabaseDriverFactory::GetRegistry()
{
    // 函数内静态变量, 保证注册表在任何驱动自注册之前完成初始化
    static std::unordered_map<DatabaseType, DriverCreator> registry;
    return registry;
}

} // namespace chat_excel
