#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <jsoncpp/json/json.h>

namespace chat_excel
{

// 数据库类型枚举
enum class DatabaseType
{
    MYSQL,   // MySQL 数据库
    SQLITE,  // SQLite 数据库
};

// SQL 语句类型枚举
enum class SqlType
{
    SELECT,    // 查询语句, 用于从数据库中检索数据
    SHOW,      // 显示数据库, 表、索引等元数据
    DESC,      // 显示表结构, 索引等元数据
    PRAGMA,    // SQLite 元数据查询语句, 用于查询表结构等信息
    INSERT,    // 插入语句, 用于将数据插入到数据库中
    UPDATE,    // 更新语句, 用于更新数据库中数据
    DELETE,    // 删除语句, 用于从数据库中删除数据
    REPLACE,   // 替换语句, 用于替换数据库中数据
    TRUNCATE,  // 截断语句, 用于截断数据库表
    CREATE,    // 创建语句, 用于创建数据库、表、索引等
    DROP,      // 删除语句, 用于删除数据库、表、索引等
    ALTER,     // 修改语句, 用于修改数据库、表、索引等
    UNKNOWN,   // 未知语句, 用于处理不支持的 SQL 语句
};

// 数据库标识符(表名、列名)最大长度限制, 统一按 MySQL 的 64 字符限制
constexpr size_t kMaxIdentifierLength = 64;

// SSL 连接配置结构, 仅存储数据, 数据之间无关联
struct SslConfig
{
    std::string cert_path;     // 客户端证书路径
    std::string key_path;      // 客户端私钥路径
    std::string ca_cert_path;  // CA 证书路径
};

/**
 * @brief 数据库配置抽象基类, 由各数据库类型的配置类继承实现
 */
class DatabaseConfig
{
public:
    DatabaseConfig() = default;

    virtual ~DatabaseConfig() = default;

    /**
     * @brief 获取数据库类型
     * @return 数据库类型枚举值
     */
    virtual DatabaseType GetDatabaseType() const = 0;

    /**
     * @brief 检查数据库配置是否合法
     * @return 配置合法返回 true, 否则返回 false
     */
    virtual bool CheckConfig() const = 0;
};

/**
 * @brief MySQL 数据库配置类
 */
class MySQLConfig : public DatabaseConfig
{
public:
    MySQLConfig() = default;

    /**
     * @brief 构造函数, 构建 MySQL 数据库连接配置
     * @param host 主机名
     * @param port 端口号
     * @param user_name 数据库用户名
     * @param password 数据库密码
     * @param database_name 数据库名称
     * @param use_ssl 是否使用 SSL 连接
     * @param ssl_config SSL 配置(证书路径, 密钥路径, CA 证书路径)
     * @param other_config 其他连接配置(键值对形式)
     */
    MySQLConfig(std::string host, int port, std::string user_name, std::string password,
                std::string database_name, bool use_ssl, SslConfig ssl_config,
                std::unordered_map<std::string, std::string> other_config);

    ~MySQLConfig() override = default;

    /**
     * @brief 获取数据库类型
     * @return 固定返回 DatabaseType::MYSQL
     */
    DatabaseType GetDatabaseType() const override;

    /**
     * @brief 检查数据库配置是否合法 : 主机名/用户名/数据库名称不为空, 端口号在有效范围内
     * @return 配置合法返回 true, 否则返回 false
     */
    bool CheckConfig() const override;

    // 主机名
    std::string host;

    // 端口号
    int port = 0;

    // 数据库用户名
    std::string user_name;

    // 数据库密码
    std::string password;

    // 数据库名称
    std::string database_name;

    // 是否使用 SSL 连接
    bool use_ssl = false;

    // SSL 配置(证书路径, 密钥路径, CA 证书路径)
    SslConfig ssl_config;

    // 其他连接配置(键值对形式)
    std::unordered_map<std::string, std::string> other_config;
};

/**
 * @brief SQLite 数据库配置类
 */
class SQLiteConfig : public DatabaseConfig
{
public:
    SQLiteConfig() = default;

    /**
     * @brief 构造函数, 构建 SQLite 数据库连接配置
     * @param database_file_path 数据库文件路径
     * @param other_config 其他连接配置(键值对形式)
     */
    SQLiteConfig(std::string database_file_path,
                 std::unordered_map<std::string, std::string> other_config);

    ~SQLiteConfig() override = default;

    /**
     * @brief 获取数据库类型
     * @return 固定返回 DatabaseType::SQLITE
     */
    DatabaseType GetDatabaseType() const override;

    /**
     * @brief 检查数据库配置是否合法 : 数据库文件路径不为空
     * @return 配置合法返回 true, 否则返回 false
     */
    bool CheckConfig() const override;

    // 数据库文件路径
    std::string database_file_path;

    // 其他连接配置(键值对形式)
    std::unordered_map<std::string, std::string> other_config;
};

// 列信息结构, 仅存储数据, 数据之间无关联
struct ColumnInfo
{
    std::string name;            // 列名
    std::string type;            // 列类型
    bool nullable = false;       // 是否为空
    bool is_primary_key = false; // 是否为主键
    bool auto_increment = false; // 是否自增
    std::string default_value;   // 默认值
    size_t max_length = 0;       // 最大长度(字符串类型使用)
};

// 索引信息结构, 仅存储数据, 数据之间无关联
struct IndexInfo
{
    std::string name;                   // 索引名称
    std::vector<std::string> columns;   // 索引包含的列名集合
    bool is_unique = false;             // 是否为唯一索引
};

// 表信息结构, 仅存储数据, 数据之间无关联
struct TableInfo
{
    std::string name;                       // 表名
    std::vector<ColumnInfo> columns;        // 列信息集合
    std::vector<std::string> primary_keys;  // 主键集合
    std::vector<IndexInfo> indexes;         // 索引集合
    std::string comment;                    // 表注释
};

/**
 * @brief SQL 操作结果类, 用于存储查询类/修改类 SQL 语句的执行结果,
 *        执行失败时通过 success 与 error_message 携带错误信息
 */
class QueryResult
{
public:
    QueryResult() = default;

    /**
     * @brief 构造函数
     * @param success SQL 操作是否成功
     */
    explicit QueryResult(bool success);

    /**
     * @brief 构造函数, 通常用于构建失败结果
     * @param success SQL 操作是否成功
     * @param error_message 错误信息
     */
    QueryResult(bool success, std::string error_message);

    /**
     * @brief 设置 SQL 操作是否成功
     * @param success SQL 操作是否成功
     */
    void SetSuccess(bool success);

    /**
     * @brief 设置错误信息
     * @param error_message 错误信息
     */
    void SetErrorMessage(std::string error_message);

    /**
     * @brief 设置影响行数
     * @param affected_rows 影响行数
     */
    void SetAffectedRows(uint64_t affected_rows);

    /**
     * @brief 设置列名集合与列类型集合
     * @param column_names 列名集合
     * @param column_types 列类型集合(与列名集合一一对应)
     */
    void SetColumns(std::vector<std::string> column_names, std::vector<std::string> column_types);

    /**
     * @brief 追加一行数据
     * @param row 行数据(与列名集合顺序一一对应)
     */
    void AddRow(std::vector<std::string> row);

    /**
     * @brief 获取总列数
     * @return 查询结果的列数
     */
    size_t GetColumnCount() const;

    /**
     * @brief 获取总行数
     * @return 查询结果的行数
     */
    size_t GetRowCount() const;

    /**
     * @brief 获取查询结果中指定行数据
     * @param row_index 行下标, 从 0 开始
     * @return 指定行的数据集合(与列名集合顺序一一对应)
     * @throws std::out_of_range 行下标越界时抛出
     */
    const std::vector<std::string>& GetRow(size_t row_index) const;

    /**
     * @brief 获取 SQL 操作是否成功
     * @return 操作成功返回 true, 否则返回 false
     */
    bool IsSuccess() const;

    /**
     * @brief 获取错误信息
     * @return 错误信息, 操作成功时为空字符串
     */
    const std::string& GetErrorMessage() const;

    /**
     * @brief 获取影响行数
     * @return SQL 操作影响的数据行数
     */
    uint64_t GetAffectedRows() const;

    /**
     * @brief 获取列名集合
     * @return 列名集合
     */
    const std::vector<std::string>& GetColumnNames() const;

    /**
     * @brief 获取列类型集合
     * @return 列类型集合(与列名集合一一对应)
     */
    const std::vector<std::string>& GetColumnTypes() const;

    /**
     * @brief 获取全部行数据集合
     * @return 行数据集合
     */
    const std::vector<std::vector<std::string>>& GetRows() const;

    /**
     * @brief 将查询结果转换为 JSON 对象,
     *        包含 success/error/affected_rows/columns/rows 五个字段
     * @return 表示查询结果的 Json::Value 对象
     */
    Json::Value ToJson() const;

    /**
     * @brief 将查询结果转换为 JSON 格式字符串(紧凑格式), 内部使用 JsonUtil 完成序列化
     * @return 查询结果的 JSON 字符串, 序列化失败时返回空字符串
     */
    std::string ToJsonString() const;

private:
    // 列名集合
    std::vector<std::string> column_names_;

    // 列类型集合(与列名集合一一对应)
    std::vector<std::string> column_types_;

    // 行数据集合
    std::vector<std::vector<std::string>> rows_;

    // 影响行数
    uint64_t affected_rows_ = 0;

    // SQL 操作是否成功
    bool success_ = false;

    // 错误信息
    std::string error_message_;
};

// 参数类型枚举, 所有绑定的参数统一转化为以下 5 种类型
enum class ParameterType
{
    NULL_TYPE,  // 空值
    INT,        // 整数
    DOUBLE,     // 浮点数
    STRING,     // 字符串
    BOOL,       // 布尔值
};

// 参数值 Variant 类型, 存储 ParameterWrapper 包装的值
using ParameterValue = std::variant<std::monostate, int64_t, double, std::string, bool>;

/**
 * @brief 参数包装器类, 将不同类型的参数包装为统一类型,
 *        用于预编译 SQL 语句执行时的参数绑定
 */
class ParameterWrapper
{
public:
    /**
     * @brief 默认构造函数, 构造 NULL 类型的参数
     */
    ParameterWrapper();

    /**
     * @brief 构造整数类型参数
     * @param value 整数值
     */
    explicit ParameterWrapper(int value);

    /**
     * @brief 构造 64 位整数类型参数
     * @param value 64 位整数值
     */
    explicit ParameterWrapper(int64_t value);

    /**
     * @brief 构造浮点数类型参数
     * @param value 浮点数值
     */
    explicit ParameterWrapper(double value);

    /**
     * @brief 构造字符串类型参数(C 风格字符串)
     * @param value 字符串值, 允许为 nullptr(等价于 NULL 参数)
     */
    explicit ParameterWrapper(const char* value);

    /**
     * @brief 构造字符串类型参数
     * @param value 字符串值
     */
    explicit ParameterWrapper(std::string value);

    /**
     * @brief 构造布尔类型参数
     * @param value 布尔值
     */
    explicit ParameterWrapper(bool value);

    /**
     * @brief 获取实际参数类型
     * @return 参数类型枚举值
     */
    ParameterType GetParameterType() const;

    /**
     * @brief 检测是否为 NULL 值
     * @return 参数为 NULL 时返回 true, 否则返回 false
     */
    bool IsNull() const;

    /**
     * @brief 获取参数值 Variant
     * @return 参数值的 const 引用, 由调用方根据参数类型取出具体值
     */
    const ParameterValue& GetValue() const;

private:
    // 参数类型, 根据用户实际绑定的值自动判断
    ParameterType parameter_type_ = ParameterType::NULL_TYPE;

    // 参数值, 存储用户绑定的值
    ParameterValue value_;
};

} // namespace chat_excel
