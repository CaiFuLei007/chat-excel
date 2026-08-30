#pragma once

#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "svc_database_service/driver/database_driver.h"
#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{

/**
 * @brief SQLite 数据库驱动类, 基于 SQLite C API 实现连接管理与 SQL 执行
 */
class SQLiteDatabaseDriver : public DatabaseDriver
{
public:
    /**
     * @brief 构造函数, 校验并保存 SQLite 数据库配置
     * @param config SQLite 数据库配置对象
     * @throws ChatExcelException 配置为空或配置类型不是 SQLite 时抛出(DB_CONFIG_INVALID)
     */
    explicit SQLiteDatabaseDriver(std::shared_ptr<DatabaseConfig> config);

    /**
     * @brief 析构函数, 自动断开连接并释放连接资源
     */
    ~SQLiteDatabaseDriver() override;

    /**
     * @brief 打开数据库连接(文件不存在时自动创建), 并按配置应用 PRAGMA 参数
     * @throws ChatExcelException 配置无效或打开失败时抛出(DB_CONFIG_INVALID/DB_CONNECTION_FAILED)
     */
    void Connect() override;

    /**
     * @brief 关闭数据库连接并释放连接资源, 未连接时调用为空操作
     */
    void Disconnect() override;

    /**
     * @brief 检测数据库连接是否可用, 内部执行轻量查询语句 SELECT 1
     * @return 连接可用返回 true, 连接不可用返回 false
     */
    bool TestConnection() override;

    /**
     * @brief 为标识符添加 SQLite 方言的双引号包裹, 内部双引号双写转义
     * @param identifier 原始标识符
     * @return 双引号包裹后的标识符
     */
    std::string QuoteIdentifier(const std::string& identifier) const override;

protected:
    /**
     * @brief 执行查询类 SQL 语句并取回结果集
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    QueryResult ExecuteQueryInternal(const std::string& sql) override;

    /**
     * @brief 执行修改类 SQL 语句
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
     * @brief 获取最近一次 SQLite API 调用的错误信息
     * @return 错误信息, 连接句柄未初始化时返回固定提示
     */
    std::string GetErrorMessage() const;

    /**
     * @brief SQL 执行公共流程 : 预编译语句, 绑定参数, 逐步执行,
     *        按需取回结果集(查询语句为 true), 修改语句取回影响行数
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @param need_result_set 是否需要取回结果集(查询语句为 true)
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 参数个数与占位符不匹配时抛出(DB_PARAM_BIND_FAILED)
     */
    QueryResult ExecutePrepared(const std::string& sql, const std::vector<ParameterWrapper>& parameters,
                                bool need_result_set);

    // SQLite 数据库连接句柄
    sqlite3* connection_ = nullptr;

    // 连接状态
    bool is_connected_ = false;
};

} // namespace chat_excel
