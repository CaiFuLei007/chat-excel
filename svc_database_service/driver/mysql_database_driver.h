#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "svc_database_service/driver/database_driver.h"
#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{

/**
 * @brief MySQL 数据库驱动类, 基于 MySQL C API 实现连接管理与 SQL 执行,
 *        支持 utf8mb4 字符集, SSL 连接与自动重连
 */
class MySQLDatabaseDriver : public DatabaseDriver
{
public:
    /**
     * @brief 构造函数, 校验并保存 MySQL 数据库配置
     * @param config MySQL 数据库配置对象
     * @throws ChatExcelException 配置为空或配置类型不是 MySQL 时抛出(DB_CONFIG_INVALID)
     */
    explicit MySQLDatabaseDriver(std::shared_ptr<DatabaseConfig> config);

    /**
     * @brief 析构函数, 自动断开连接并释放连接资源
     */
    ~MySQLDatabaseDriver() override;

    /**
     * @brief 建立数据库连接 : 初始化连接句柄, 设置 utf8mb4 字符集与自动重连,
     *        按需启用 SSL, 最后建立连接
     * @throws ChatExcelException 配置无效或连接失败时抛出(DB_CONFIG_INVALID/DB_CONNECTION_FAILED)
     */
    void Connect() override;

    /**
     * @brief 断开数据库连接并释放连接资源, 未连接时调用为空操作
     */
    void Disconnect() override;

    /**
     * @brief 检测数据库连接是否可用, 内部通过 mysql_ping 心跳检测
     * @return 连接可用返回 true, 连接不可用返回 false
     */
    bool TestConnection() override;

    /**
     * @brief 为标识符添加 MySQL 方言的反引号包裹, 内部反引号双写转义
     * @param identifier 原始标识符
     * @return 反引号包裹后的标识符
     */
    std::string QuoteIdentifier(const std::string& identifier) const override;

    /**
     * @brief 获取 MySQL 数据库中所有表名, 内部执行 SHOW TABLES 语句
     * @return 表名列表
     * @throws ChatExcelException 未建立连接或查询失败时抛出(DB_NOT_CONNECTED/DB_EXECUTE_FAILED)
     */
    std::vector<std::string> GetAllTablesName() override;

    /**
     * @brief 获取 MySQL 指定表的结构信息, 内部执行 DESC 语句并解析
     *        Field/Type/Null/Key/Default/Extra 六列结果
     * @param table_name 表名
     * @return 表信息(表名与列信息集合)
     * @throws ChatExcelException 表名非法, 未建立连接或查询失败时抛出
     */
    TableInfo GetTableStructure(const std::string& table_name) override;

    /**
     * @brief 将 Excel 解析列类型转化为 MySQL 列类型,
     *        BOOLEAN 转化为 INT 类型, DATE 转化为 TEXT 类型, 其余类型原样返回
     * @param excel_type Excel 解析列类型(TEXT/BIGINT/DOUBLE/BOOLEAN/DATE)
     * @return MySQL 列类型
     */
    std::string ConvertExcelColumnType(const std::string& excel_type) const override;

protected:
    /**
     * @brief 使用 mysql_query 与 mysql_store_result 执行查询类 SQL 语句
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    QueryResult ExecuteQueryInternal(const std::string& sql) override;

    /**
     * @brief 使用 mysql_query 执行修改类 SQL 语句
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    QueryResult ExecuteUpdateInternal(const std::string& sql) override;

    /**
     * @brief 使用预编译语句执行查询类 SQL 语句
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 参数个数与占位符不匹配时抛出(DB_PARAM_BIND_FAILED)
     */
    QueryResult ExecutePreparedQueryInternal(const std::string& sql,
                                             const std::vector<ParameterWrapper>& parameters) override;

    /**
     * @brief 使用预编译语句执行修改类 SQL 语句
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 参数个数与占位符不匹配时抛出(DB_PARAM_BIND_FAILED)
     */
    QueryResult ExecutePreparedUpdateInternal(const std::string& sql,
                                              const std::vector<ParameterWrapper>& parameters) override;

private:
    /**
     * @brief 获取最近一次 MySQL API 调用的错误信息
     * @return 错误信息, 连接句柄未初始化时返回固定提示
     */
    std::string GetErrorMessage() const;

    /**
     * @brief 预编译语句执行公共流程 : 初始化语句句柄, 预编译, 参数绑定,
     *        执行, 最后按需取回结果集
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @param need_result_set 是否需要取回结果集(查询语句为 true)
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 参数个数与占位符不匹配时抛出(DB_PARAM_BIND_FAILED)
     */
    QueryResult ExecutePrepared(const std::string& sql, const std::vector<ParameterWrapper>& parameters,
                                bool need_result_set);

    /**
     * @brief 获取预编译语句执行后的结果集, 所有列统一按字符串绑定,
     *        缓冲区不足导致截断时按实际长度重新获取该列数据
     * @param statement 已执行成功的语句句柄
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    QueryResult FetchPreparedResult(MYSQL_STMT* statement);

    // MySQL 连接句柄
    MYSQL* connection_ = nullptr;

    // 连接状态
    bool is_connected_ = false;
};

} // namespace chat_excel
