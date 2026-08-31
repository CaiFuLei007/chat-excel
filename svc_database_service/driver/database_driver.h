#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{

/**
 * @brief 数据库驱动抽象接口类, 定义连接管理与 SQL 执行的统一接口,
 *        由 MySQL 与 SQLite 驱动分别实现, 对上层业务隐藏底层数据库差异;
 *        SQL 校验在基类统一完成 : 校验失败抛出 ChatExcelException,
 *        执行失败填充 QueryResult(success = false)返回
 */
class DatabaseDriver
{
public:
    /**
     * @brief 构造函数, 保存数据库配置供连接时使用
     * @param config 数据库配置对象(不可为空)
     */
    explicit DatabaseDriver(std::shared_ptr<DatabaseConfig> config);

    virtual ~DatabaseDriver() = default;

    DatabaseDriver(const DatabaseDriver&) = delete;

    DatabaseDriver& operator=(const DatabaseDriver&) = delete;

    /**
     * @brief 建立数据库连接, 内部会先校验配置合法性
     * @throws ChatExcelException 配置无效或连接失败时抛出(DB_CONFIG_INVALID/DB_CONNECTION_FAILED)
     */
    virtual void Connect() = 0;

    /**
     * @brief 断开数据库连接并释放连接资源, 未连接时调用为空操作
     * @throws ChatExcelException 断开连接失败时抛出(DB_DISCONNECT_FAILED)
     */
    virtual void Disconnect() = 0;

    /**
     * @brief 检测数据库连接是否可用
     * @return 连接可用返回 true, 连接不可用返回 false
     */
    virtual bool TestConnection() = 0;

    /**
     * @brief 执行查询类 SQL 语句(仅支持 SELECT/SHOW/DESC),
     *        执行前进行 SQL 合法性校验
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException SQL 校验失败或未建立连接时抛出
     */
    QueryResult ExecuteQuery(const std::string& sql);

    /**
     * @brief 执行修改类 SQL 语句(INSERT/UPDATE/DELETE/REPLACE/TRUNCATE/
     *        CREATE/DROP/ALTER)与事务控制语句, 执行前进行 SQL 合法性校验
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException SQL 校验失败或未建立连接时抛出
     */
    QueryResult ExecuteUpdate(const std::string& sql);

    /**
     * @brief 使用预编译语句执行查询类 SQL 语句(仅支持 SELECT/SHOW/DESC),
     *        执行前进行 SQL 合法性校验, 参数通过绑定方式传入防止 SQL 注入
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException SQL 校验失败或未建立连接时抛出
     */
    QueryResult ExecutePreparedQuery(const std::string& sql, const std::vector<ParameterWrapper>& parameters);

    /**
     * @brief 使用预编译语句执行修改类 SQL 语句与事务控制语句,
     *        执行前进行 SQL 合法性校验, 参数通过绑定方式传入防止 SQL 注入
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException SQL 校验失败或未建立连接时抛出
     */
    QueryResult ExecutePreparedUpdate(const std::string& sql, const std::vector<ParameterWrapper>& parameters);

    /**
     * @brief 开启事务, 内部执行 BEGIN 语句(走修改类 SQL 校验通路)
     * @return 事务开启结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 未建立连接时抛出
     */
    QueryResult BeginTransaction();

    /**
     * @brief 提交事务, 内部执行 COMMIT 语句(走修改类 SQL 校验通路)
     * @return 事务提交结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 未建立连接时抛出
     */
    QueryResult CommitTransaction();

    /**
     * @brief 回滚事务, 内部执行 ROLLBACK 语句(走修改类 SQL 校验通路)
     * @return 事务回滚结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 未建立连接时抛出
     */
    QueryResult RollbackTransaction();

    /**
     * @brief 获取数据库类型
     * @return 数据库类型枚举值
     */
    DatabaseType GetDatabaseType() const;

    /**
     * @brief 获取数据库配置对象
     * @return 数据库配置对象
     */
    const std::shared_ptr<DatabaseConfig>& GetConfig() const;

    /**
     * @brief 删除数据表, 供业务层执行受控的删表操作(临时表清理, 文件表删除),
     *        仅校验表名合法性, 不进行危险关键字校验(用户 SQL 仍会被危险关键字拦截)
     * @param table_name 表名
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     * @throws ChatExcelException 表名非法或未建立连接时抛出
     */
    QueryResult DropTable(const std::string& table_name);

    /**
     * @brief 为标识符(表名, 列名等)添加数据库方言的引号包裹,
     *        MySQL 使用反引号, SQLite 使用双引号, 内部已有引号时双写转义
     * @param identifier 原始标识符
     * @return 引号包裹后的标识符
     */
    virtual std::string QuoteIdentifier(const std::string& identifier) const = 0;

    /**
     * @brief 获取数据库中所有表名(包含临时表, 不过滤)
     * @return 表名列表
     * @throws ChatExcelException 未建立连接或查询失败时抛出(DB_NOT_CONNECTED/DB_EXECUTE_FAILED)
     */
    virtual std::vector<std::string> GetAllTablesName() = 0;

    /**
     * @brief 获取指定表的结构信息, 按各数据库方言查询并解析表结构
     * @param table_name 表名
     * @return 表信息(表名与列信息集合)
     * @throws ChatExcelException 表名非法, 未建立连接或查询失败时抛出
     */
    virtual TableInfo GetTableStructure(const std::string& table_name) = 0;

    /**
     * @brief 将 Excel 解析列类型转化为当前数据库的列类型,
     *        BOOLEAN 转化为整型类型, DATE 转化为文本类型, 其余类型原样返回
     * @param excel_type Excel 解析列类型(TEXT/BIGINT/DOUBLE/BOOLEAN/DATE)
     * @return 数据库列类型
     */
    virtual std::string ConvertExcelColumnType(const std::string& excel_type) const = 0;

protected:
    /**
     * @brief 执行查询类 SQL 语句的内部实现, 由子类完成具体数据库操作,
     *        调用方保证连接已建立且 SQL 已通过校验
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    virtual QueryResult ExecuteQueryInternal(const std::string& sql) = 0;

    /**
     * @brief 执行修改类 SQL 语句的内部实现, 由子类完成具体数据库操作,
     *        调用方保证连接已建立且 SQL 已通过校验
     * @param sql 原始 SQL 语句
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    virtual QueryResult ExecuteUpdateInternal(const std::string& sql) = 0;

    /**
     * @brief 使用预编译语句执行查询类 SQL 语句的内部实现, 由子类完成具体数据库操作,
     *        调用方保证连接已建立且 SQL 已通过校验
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    virtual QueryResult ExecutePreparedQueryInternal(const std::string& sql,
                                                     const std::vector<ParameterWrapper>& parameters) = 0;

    /**
     * @brief 使用预编译语句执行修改类 SQL 语句的内部实现, 由子类完成具体数据库操作,
     *        调用方保证连接已建立且 SQL 已通过校验
     * @param sql 含占位符的原始 SQL 语句
     * @param parameters 绑定参数集合, 顺序与占位符一一对应
     * @return SQL 执行结果(执行失败时 success 为 false 并携带错误信息)
     */
    virtual QueryResult ExecutePreparedUpdateInternal(const std::string& sql,
                                                      const std::vector<ParameterWrapper>& parameters) = 0;

    /**
     * @brief 检测连接状态, 未连接时记录日志并抛出异常
     * @param connected 当前连接状态
     * @throws ChatExcelException 未连接时抛出(DB_NOT_CONNECTED)
     */
    void CheckConnected(bool connected) const;

    /**
     * @brief 校验查询类 SQL 语句 : 基础合法性校验 + 只读类型校验
     * @param sql 原始 SQL 语句
     * @throws ChatExcelException 校验失败时抛出对应错误码
     */
    void ValidateQuerySql(const std::string& sql) const;

    /**
     * @brief 校验修改类 SQL 语句 : 基础合法性校验 + 修改类型或事务语句校验
     * @param sql 原始 SQL 语句
     * @throws ChatExcelException 校验失败时抛出对应错误码
     */
    void ValidateUpdateSql(const std::string& sql) const;

    /**
     * @brief 获取行数据中指定下标的列值, 供各驱动解析表结构等结果集时使用,
     *        下标越界时返回空字符串
     * @param row 行数据集合
     * @param column_index 列下标
     * @return 指定下标的列值
     */
    static std::string GetRowColumnValue(const std::vector<std::string>& row, size_t column_index);

    // 数据库配置对象
    std::shared_ptr<DatabaseConfig> config_;
};

/**
 * @brief 数据库驱动工厂, 基于注册表机制根据数据库配置动态创建对应的驱动实例,
 *        各驱动实现在加载时通过命名空间作用域静态变量完成自注册
 */
class DatabaseDriverFactory
{
public:
    // 驱动创建器类型, 接收数据库配置并返回驱动实例
    using DriverCreator = std::function<std::shared_ptr<DatabaseDriver>(std::shared_ptr<DatabaseConfig>)>;

    // 纯静态工具类, 禁止实例化
    DatabaseDriverFactory() = delete;

    /**
     * @brief 注册数据库类型的驱动创建器, 重复注册会覆盖已有创建器
     * @param database_type 数据库类型
     * @param creator 驱动创建器
     */
    static void RegisterDriver(DatabaseType database_type, DriverCreator creator);

    /**
     * @brief 根据数据库配置创建对应的驱动实例
     * @param config 数据库配置对象, 其数据库类型决定创建的驱动类型
     * @return 数据库驱动实例
     * @throws ChatExcelException 配置为空, 配置非法或类型未注册时抛出
     */
    static std::shared_ptr<DatabaseDriver> CreateDriver(std::shared_ptr<DatabaseConfig> config);

private:
    /**
     * @brief 获取驱动创建器注册表(函数内静态变量, 保证初始化顺序安全)
     * @return 驱动创建器注册表(数据库类型 -> 创建器)
     */
    static std::unordered_map<DatabaseType, DriverCreator>& GetRegistry();
};

} // namespace chat_excel
