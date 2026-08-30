#include "database_schema.h"

#include <cpp-toolkit/util.h>

namespace chat_excel
{

MySQLConfig::MySQLConfig(std::string host, int port, std::string user_name, std::string password,
                         std::string database_name, bool use_ssl, SslConfig ssl_config,
                         std::unordered_map<std::string, std::string> other_config)
    : host(std::move(host)),
      port(port),
      user_name(std::move(user_name)),
      password(std::move(password)),
      database_name(std::move(database_name)),
      use_ssl(use_ssl),
      ssl_config(std::move(ssl_config)),
      other_config(std::move(other_config))
{
}

DatabaseType MySQLConfig::GetDatabaseType() const
{
    return DatabaseType::MYSQL;
}

bool MySQLConfig::CheckConfig() const
{
    // 端口号有效范围 1 - 65535
    constexpr int kMinPort = 1;
    constexpr int kMaxPort = 65535;

    return !host.empty() && port >= kMinPort && port <= kMaxPort &&
           !user_name.empty() && !database_name.empty();
}

SQLiteConfig::SQLiteConfig(std::string database_file_path,
                           std::unordered_map<std::string, std::string> other_config)
    : database_file_path(std::move(database_file_path)),
      other_config(std::move(other_config))
{
}

DatabaseType SQLiteConfig::GetDatabaseType() const
{
    return DatabaseType::SQLITE;
}

bool SQLiteConfig::CheckConfig() const
{
    return !database_file_path.empty();
}

QueryResult::QueryResult(bool success)
    : success_(success)
{
}

QueryResult::QueryResult(bool success, std::string error_message)
    : success_(success),
      error_message_(std::move(error_message))
{
}

void QueryResult::SetSuccess(bool success)
{
    success_ = success;
}

void QueryResult::SetErrorMessage(std::string error_message)
{
    error_message_ = std::move(error_message);
}

void QueryResult::SetAffectedRows(uint64_t affected_rows)
{
    affected_rows_ = affected_rows;
}

void QueryResult::SetColumns(std::vector<std::string> column_names, std::vector<std::string> column_types)
{
    column_names_ = std::move(column_names);
    column_types_ = std::move(column_types);
}

void QueryResult::AddRow(std::vector<std::string> row)
{
    rows_.push_back(std::move(row));
}

size_t QueryResult::GetColumnCount() const
{
    return column_names_.size();
}

size_t QueryResult::GetRowCount() const
{
    return rows_.size();
}

const std::vector<std::string>& QueryResult::GetRow(size_t row_index) const
{
    // 行下标越界时由 vector::at 抛出 std::out_of_range 异常
    return rows_.at(row_index);
}

bool QueryResult::IsSuccess() const
{
    return success_;
}

const std::string& QueryResult::GetErrorMessage() const
{
    return error_message_;
}

uint64_t QueryResult::GetAffectedRows() const
{
    return affected_rows_;
}

const std::vector<std::string>& QueryResult::GetColumnNames() const
{
    return column_names_;
}

const std::vector<std::string>& QueryResult::GetColumnTypes() const
{
    return column_types_;
}

const std::vector<std::vector<std::string>>& QueryResult::GetRows() const
{
    return rows_;
}

Json::Value QueryResult::ToJson() const
{
    Json::Value json;
    json["success"] = success_;
    json["error"] = error_message_;
    json["affected_rows"] = static_cast<Json::Int64>(affected_rows_);

    // 列信息数组 : 每个元素为 <列名, 列类型> 键值对
    Json::Value columns(Json::arrayValue);
    for (size_t i = 0; i < column_names_.size(); ++i)
    {
        Json::Value column;
        column["name"] = column_names_[i];
        column["type"] = i < column_types_.size() ? column_types_[i] : "";
        columns.append(column);
    }
    json["columns"] = columns;

    // 行数据数组 : 每行为列数据组成的数组
    Json::Value rows(Json::arrayValue);
    for (const auto& row : rows_)
    {
        Json::Value json_row(Json::arrayValue);
        for (const auto& cell : row)
        {
            json_row.append(cell);
        }
        rows.append(json_row);
    }
    json["rows"] = rows;

    return json;
}

std::string QueryResult::ToJsonString() const
{
    std::string json_string;
    // 序列化失败时返回空字符串
    if (!cpp_toolkit::JsonUtil::SerializeCompact(ToJson(), json_string))
    {
        return "";
    }
    return json_string;
}

ParameterWrapper::ParameterWrapper()
    : parameter_type_(ParameterType::NULL_TYPE),
      value_(std::monostate())
{
}

ParameterWrapper::ParameterWrapper(int value)
    : parameter_type_(ParameterType::INT),
      value_(static_cast<int64_t>(value))
{
}

ParameterWrapper::ParameterWrapper(int64_t value)
    : parameter_type_(ParameterType::INT),
      value_(value)
{
}

ParameterWrapper::ParameterWrapper(double value)
    : parameter_type_(ParameterType::DOUBLE),
      value_(value)
{
}

ParameterWrapper::ParameterWrapper(const char* value)
    : parameter_type_(value == nullptr ? ParameterType::NULL_TYPE : ParameterType::STRING),
      value_(value == nullptr ? ParameterValue(std::monostate()) : ParameterValue(std::string(value)))
{
}

ParameterWrapper::ParameterWrapper(std::string value)
    : parameter_type_(ParameterType::STRING),
      value_(std::move(value))
{
}

ParameterWrapper::ParameterWrapper(bool value)
    : parameter_type_(ParameterType::BOOL),
      value_(value)
{
}

ParameterType ParameterWrapper::GetParameterType() const
{
    return parameter_type_;
}

bool ParameterWrapper::IsNull() const
{
    return parameter_type_ == ParameterType::NULL_TYPE;
}

const ParameterValue& ParameterWrapper::GetValue() const
{
    return value_;
}

} // namespace chat_excel
