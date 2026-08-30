#include "svc_database_service/driver/mysql_database_driver.h"

#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <cpp-toolkit/logger.h>

#include "common/exception.h"

using chat_excel::ChatExcelException;
using chat_excel::DatabaseConfig;
using chat_excel::DatabaseDriver;
using chat_excel::ErrorCode;
using chat_excel::MySQLConfig;
using chat_excel::ParameterWrapper;
using chat_excel::QueryResult;
using chat_excel::SQLiteConfig;
using chat_excel::SslConfig;
using chat_excel::MySQLDatabaseDriver;

namespace
{

// MySQL 测试环境变量名称
constexpr const char* kEnvHost = "MYSQL_CHAT_EXCEL_TEST_HOST";
constexpr const char* kEnvPort = "MYSQL_CHAT_EXCEL_TEST_PORT";
constexpr const char* kEnvUser = "MYSQL_CHAT_EXCEL_TEST_USER";
constexpr const char* kEnvPassword = "MYSQL_CHAT_EXCEL_TEST_PASSWORD";
constexpr const char* kEnvDatabase = "MYSQL_CHAT_EXCEL_TEST_DATABASE";
constexpr const char* kEnvCharset = "MYSQL_CHAT_EXCEL_TEST_CHARSET";

// MySQL 测试表名
const std::string kTestTableName = "chat_excel_driver_test_table";

/**
 * @brief 断言指定操作抛出携带预期错误码的 ChatExcelException
 * @param action 待执行操作
 * @param expected_error_code 预期错误码
 */
void ExpectExceptionWithCode(const std::function<void()>& action, ErrorCode expected_error_code)
{
    try
    {
        action();
        FAIL() << "操作未抛出预期异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), expected_error_code);
    }
}

/**
 * @brief 从环境变量加载 MySQL 测试配置
 * @return 配置对象, 必需环境变量缺失时返回空指针
 */
std::shared_ptr<MySQLConfig> LoadMySQLTestConfig()
{
    const char* host = std::getenv(kEnvHost);
    const char* port = std::getenv(kEnvPort);
    const char* user_name = std::getenv(kEnvUser);
    const char* password = std::getenv(kEnvPassword);
    const char* database_name = std::getenv(kEnvDatabase);
    const char* charset = std::getenv(kEnvCharset);
    if (host == nullptr || port == nullptr || user_name == nullptr ||
        password == nullptr || database_name == nullptr || charset == nullptr)
    {
        return nullptr;
    }
    std::unordered_map<std::string, std::string> other_config;
    other_config["charset"] = charset;
    return std::make_shared<MySQLConfig>(host, std::stoi(port), user_name, password,
                                         database_name, false, SslConfig(), other_config);
}

/**
 * @brief 构建测试表的建表语句
 * @return 建表 SQL 语句
 */
std::string BuildCreateTableSql()
{
    return "CREATE TABLE IF NOT EXISTS " + kTestTableName +
           " (id INT PRIMARY KEY, name VARCHAR(128), score DOUBLE, is_active TINYINT, remark TEXT)";
}

/**
 * @brief 准备幂等测试数据 : 固定主键 1/2, 使用 REPLACE 保证重复运行时数据一致
 *        注意 : REPLACE 影响行数不稳定(首次插入为 1, 覆盖更新为 2), 调用方不要断言精确值
 * @param driver 已连接的数据库驱动
 */
void EnsureTestTableAndRows(DatabaseDriver& driver)
{
    EXPECT_TRUE(driver.ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    EXPECT_TRUE(driver.ExecuteUpdate("REPLACE INTO " + kTestTableName +
                                     " (id, name, score, is_active, remark) VALUES (1, 'name-1', 90.5, 1, 'remark-1')")
                    .IsSuccess());
    EXPECT_TRUE(driver.ExecuteUpdate("REPLACE INTO " + kTestTableName +
                                     " (id, name, score, is_active, remark) VALUES (2, 'name-2', 80.25, 0, NULL)")
                    .IsSuccess());
}

} // namespace

// 异常情况: 配置对象为空时构造驱动抛出 DB_CONFIG_INVALID
TEST(MySQLDatabaseDriverConstructorTest, RejectNullConfig)
{
    std::shared_ptr<DatabaseConfig> empty_config;
    ExpectExceptionWithCode([&empty_config]()
                            { MySQLDatabaseDriver driver(empty_config); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 异常情况: 配置类型不是 MySQL 时构造驱动抛出 DB_CONFIG_INVALID
TEST(MySQLDatabaseDriverConstructorTest, RejectMismatchedConfigType)
{
    auto config = std::make_shared<SQLiteConfig>("/tmp/chat_excel_mysql_type_test.db",
                                                 std::unordered_map<std::string, std::string>());
    ExpectExceptionWithCode([&config]()
                            { MySQLDatabaseDriver driver(config); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 正常情况: 标识符使用反引号包裹
TEST(MySQLDatabaseDriverQuoteIdentifierTest, WrapWithBackticks)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    EXPECT_EQ(driver.QuoteIdentifier("users"), "`users`");
    EXPECT_EQ(driver.QuoteIdentifier(""), "``");
}

// 正常情况: 标识符内部的反引号双写转义
TEST(MySQLDatabaseDriverQuoteIdentifierTest, EscapeInnerBackticks)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    EXPECT_EQ(driver.QuoteIdentifier("us`ers"), "`us``ers`");
}

// 正常情况: 使用环境变量配置连接 MySQL, 心跳检测通过, 断开后心跳检测失败
TEST(MySQLDatabaseDriverConnectTest, ConnectWithEnvironmentConfig)
{
    auto config = LoadMySQLTestConfig();
    if (config == nullptr)
    {
        GTEST_SKIP() << "未配置 MySQL 测试环境变量, 跳过该用例";
    }
    MySQLDatabaseDriver driver(config);
    driver.Connect();
    EXPECT_TRUE(driver.TestConnection());
    driver.Disconnect();
    EXPECT_FALSE(driver.TestConnection());
}

// 异常情况: 配置无效(主机名为空)时连接抛出 DB_CONFIG_INVALID
TEST(MySQLDatabaseDriverConnectTest, RejectInvalidConfig)
{
    auto config = std::make_shared<MySQLConfig>("", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.Connect(); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 异常情况: 目标主机端口不可达时连接抛出 DB_CONNECTION_FAILED
TEST(MySQLDatabaseDriverConnectTest, RejectUnreachableHost)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 1, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.Connect(); },
                            ErrorCode::DB_CONNECTION_FAILED);
}

// 正常情况: 未连接时调用断开连接为空操作, 不抛出异常
TEST(MySQLDatabaseDriverDisconnectTest, DisconnectWithoutConnectIsNoOp)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    driver.Disconnect();
}

// 异常情况: 未建立连接时开启事务抛出 DB_NOT_CONNECTED
TEST(MySQLDatabaseDriverDisconnectTest, BeginTransactionWithoutConnectThrow)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    MySQLDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.BeginTransaction(); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 测试夹具 : 使用环境变量配置连接真实 MySQL, 并准备幂等测试数据
class MySQLDatabaseDriverTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto config = LoadMySQLTestConfig();
        if (config == nullptr)
        {
            GTEST_SKIP() << "未配置 MySQL 测试环境变量, 跳过 MySQL 驱动测试";
        }
        driver_ = std::make_unique<MySQLDatabaseDriver>(config);
        driver_->Connect();
        EnsureTestTableAndRows(*driver_);
    }

    void TearDown() override
    {
        if (driver_ != nullptr)
        {
            driver_->Disconnect();
        }
    }

    // 驱动对象
    std::unique_ptr<MySQLDatabaseDriver> driver_;
};

// 正常情况: 连接建立后心跳检测返回 true
TEST_F(MySQLDatabaseDriverTest, TestConnectionSucceedAfterConnect)
{
    EXPECT_TRUE(driver_->TestConnection());
}

// 正常情况: 断开连接后心跳检测返回 false
TEST_F(MySQLDatabaseDriverTest, TestConnectionFailAfterDisconnect)
{
    driver_->Disconnect();
    EXPECT_FALSE(driver_->TestConnection());
}

// 正常情况: 执行查询语句返回列信息与行数据, 整数/浮点/布尔/空值统一以字符串形式呈现
TEST_F(MySQLDatabaseDriverTest, QueryRowsAsString)
{
    QueryResult result = driver_->ExecuteQuery("SELECT id, name, score, is_active, remark FROM " +
                                               kTestTableName + " WHERE id = 1");
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetColumnCount(), 5);
    EXPECT_EQ(result.GetColumnNames()[0], "id");
    EXPECT_EQ(result.GetColumnNames()[1], "name");
    EXPECT_EQ(result.GetColumnNames()[2], "score");
    EXPECT_EQ(result.GetColumnNames()[3], "is_active");
    EXPECT_EQ(result.GetColumnNames()[4], "remark");
    EXPECT_EQ(result.GetRow(0)[0], "1");
    EXPECT_EQ(result.GetRow(0)[1], "name-1");
    EXPECT_EQ(result.GetRow(0)[2], "90.5");
    EXPECT_EQ(result.GetRow(0)[3], "1");
    EXPECT_EQ(result.GetRow(0)[4], "remark-1");
}

// 正常情况: SHOW 语句返回匹配的表信息
TEST_F(MySQLDatabaseDriverTest, QueryShowTables)
{
    QueryResult result = driver_->ExecuteQuery("SHOW TABLES LIKE '" + kTestTableName + "'");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
}

// 正常情况: DESC 语句返回表结构信息, 每列对应一行
TEST_F(MySQLDatabaseDriverTest, QueryDescribeTable)
{
    QueryResult result = driver_->ExecuteQuery("DESC " + kTestTableName);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 5);
    EXPECT_EQ(result.GetColumnNames()[0], "Field");
}

// 正常情况: 查询无匹配数据时返回 0 行
TEST_F(MySQLDatabaseDriverTest, QueryEmptyResult)
{
    QueryResult result = driver_->ExecuteQuery("SELECT id, name FROM " + kTestTableName + " WHERE id = 999999");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 0);
}

// 异常情况: 未建立连接时执行查询抛出 DB_NOT_CONNECTED
TEST_F(MySQLDatabaseDriverTest, QueryWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("SELECT 1"); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 异常情况: 执行访问不存在表的查询语句返回失败结果并携带错误信息
TEST_F(MySQLDatabaseDriverTest, QueryInvalidSqlReturnFailedResult)
{
    QueryResult result = driver_->ExecuteQuery("SELECT * FROM table_not_exists");
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_FALSE(result.GetErrorMessage().empty());
}

// 异常情况: 查询通路拒绝修改类语句, 抛出 DB_SQL_INVALID
TEST_F(MySQLDatabaseDriverTest, QueryPathRejectModifySql)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("INSERT INTO " + kTestTableName + " (id) VALUES (1)"); },
                            ErrorCode::DB_SQL_INVALID);
}

// 异常情况: 查询语句为空抛出 DB_SQL_EMPTY
TEST_F(MySQLDatabaseDriverTest, QueryEmptySqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("   "); },
                            ErrorCode::DB_SQL_EMPTY);
}

// 异常情况: 查询语句包含危险操作抛出 DB_SQL_DANGEROUS
TEST_F(MySQLDatabaseDriverTest, QueryDangerousSqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("DROP TABLE " + kTestTableName); },
                            ErrorCode::DB_SQL_DANGEROUS);
}

// 异常情况: 查询语句包含多条语句抛出 DB_SQL_MULTIPLE_STATEMENTS
TEST_F(MySQLDatabaseDriverTest, QueryMultipleStatementsThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("SELECT 1; SELECT 2"); },
                            ErrorCode::DB_SQL_MULTIPLE_STATEMENTS);
}

// 正常情况: 执行建表语句成功, 表已存在时影响行数为 0
TEST_F(MySQLDatabaseDriverTest, UpdateCreateTable)
{
    QueryResult result = driver_->ExecuteUpdate(BuildCreateTableSql());
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetAffectedRows(), 0);
}

// 正常情况: REPLACE 语句执行成功, 因主键覆盖次数不同影响行数不定, 仅断言不小于 1
TEST_F(MySQLDatabaseDriverTest, UpdateReplaceRowReturnAffectedRows)
{
    QueryResult result = driver_->ExecuteUpdate("REPLACE INTO " + kTestTableName +
                                                " (id, name, score, is_active, remark) VALUES (1, 'name-1', 90.5, 1, 'remark-1')");
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_GE(result.GetAffectedRows(), 1);
}

// 正常情况: UPDATE 语句返回受影响行数, 无匹配行时影响行数为 0, 更新后可查询到新值
TEST_F(MySQLDatabaseDriverTest, UpdateRowsReturnAffectedRows)
{
    QueryResult result = driver_->ExecuteUpdate("UPDATE " + kTestTableName +
                                                " SET name = 'name-1-updated', score = 91.5 WHERE id = 1");
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetAffectedRows(), 1);

    QueryResult verify_result = driver_->ExecuteQuery("SELECT name, score FROM " + kTestTableName + " WHERE id = 1");
    ASSERT_TRUE(verify_result.IsSuccess());
    EXPECT_EQ(verify_result.GetRow(0)[0], "name-1-updated");
    EXPECT_EQ(verify_result.GetRow(0)[1], "91.5");

    QueryResult no_match_result = driver_->ExecuteUpdate("UPDATE " + kTestTableName +
                                                         " SET name = 'no-match' WHERE id = 999999");
    EXPECT_TRUE(no_match_result.IsSuccess());
    EXPECT_EQ(no_match_result.GetAffectedRows(), 0);
}

// 异常情况: 未建立连接时执行修改语句抛出 DB_NOT_CONNECTED
TEST_F(MySQLDatabaseDriverTest, UpdateWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("CREATE TABLE some_table (id INT)"); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 异常情况: 修改通路拒绝只读语句, 抛出 DB_SQL_INVALID
TEST_F(MySQLDatabaseDriverTest, UpdatePathRejectReadOnlySql)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("SELECT 1"); },
                            ErrorCode::DB_SQL_INVALID);
}

// 异常情况: 修改语句包含危险操作抛出 DB_SQL_DANGEROUS
TEST_F(MySQLDatabaseDriverTest, UpdateDangerousSqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("TRUNCATE TABLE " + kTestTableName); },
                            ErrorCode::DB_SQL_DANGEROUS);
}

// 异常情况: 违反表约束(主键冲突)的修改语句返回失败结果
TEST_F(MySQLDatabaseDriverTest, UpdateConstraintViolationReturnFailedResult)
{
    QueryResult result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                " (id, name, score, is_active) VALUES (1, 'duplicate', 70.0, 1)");
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_FALSE(result.GetErrorMessage().empty());
}

// 正常情况: 预编译查询绑定整数与布尔参数, 返回匹配行
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithIntAndBoolParameter)
{
    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT name FROM " + kTestTableName + " WHERE id = ? AND is_active = ?",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(true)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "name-1");
}

// 正常情况: 预编译查询绑定浮点数参数, 精确匹配
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithDoubleParameter)
{
    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT name FROM " + kTestTableName + " WHERE id = ? AND score = ?",
        {ParameterWrapper(int64_t(2)), ParameterWrapper(80.25)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "name-2");
}

// 正常情况: 预编译查询绑定字符串参数
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithStringParameter)
{
    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT id, score FROM " + kTestTableName + " WHERE name = ?",
        {ParameterWrapper("name-2")});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "2");
    EXPECT_EQ(result.GetRow(0)[1], "80.25");
}

// 正常情况: 预编译语句绑定 NULL 参数后, 查询结果中的空值以 NULL 字符串呈现
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithNullParameter)
{
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name, score, is_active, remark) VALUES (?, ?, ?, ?, ?)",
        {ParameterWrapper(int64_t(3)), ParameterWrapper(), ParameterWrapper(88.5),
         ParameterWrapper(true), ParameterWrapper()});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name, score, remark FROM " + kTestTableName + " WHERE id = 3");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0], "NULL");
    EXPECT_EQ(result.GetRow(0)[1], "88.5");
    EXPECT_EQ(result.GetRow(0)[2], "NULL");
}

// 正常情况: 预编译查询绑定含中文与特殊字符的字符串参数, 写入与匹配均正确
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithSpecialCharacterParameter)
{
    std::string special_name = "张三's \"note\";--注释";
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name) VALUES (?, ?)",
        {ParameterWrapper(int64_t(4)), ParameterWrapper(special_name)});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT id FROM " + kTestTableName + " WHERE name = ?",
        {ParameterWrapper(special_name)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "4");
}

// 边界情况: 预编译语句绑定超长字符串参数写入 TEXT 列, 数据完整读出(验证截断重取路径)
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithLongStringParameter)
{
    std::string long_remark(10000, 'a');
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, remark) VALUES (?, ?)",
        {ParameterWrapper(int64_t(5)), ParameterWrapper(long_remark)});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT remark FROM " + kTestTableName + " WHERE id = 5");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0].size(), long_remark.size());
}

// 异常情况: 预编译查询参数个数与占位符不匹配抛出 DB_PARAM_BIND_FAILED
TEST_F(MySQLDatabaseDriverTest, PreparedQueryParameterCountMismatchThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedQuery("SELECT name FROM " + kTestTableName + " WHERE id = ?",
                                                            {ParameterWrapper(int64_t(1)),
                                                             ParameterWrapper("额外参数")}); },
                            ErrorCode::DB_PARAM_BIND_FAILED);
}

// 异常情况: 未建立连接时预编译查询抛出 DB_NOT_CONNECTED
TEST_F(MySQLDatabaseDriverTest, PreparedQueryWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedQuery("SELECT name FROM some_table WHERE id = ?",
                                                            {ParameterWrapper(int64_t(1))}); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 正常情况: 预编译修改语句成功执行并返回影响行数, 更新后可查询到新值
TEST_F(MySQLDatabaseDriverTest, PreparedInsertAndUpdateWithParameters)
{
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "REPLACE INTO " + kTestTableName + " (id, name, score, is_active, remark) VALUES (?, ?, ?, ?, ?)",
        {ParameterWrapper(int64_t(6)), ParameterWrapper(std::string("name-6")),
         ParameterWrapper(70.5), ParameterWrapper(true), ParameterWrapper()});
    EXPECT_TRUE(insert_result.IsSuccess());

    QueryResult update_result = driver_->ExecutePreparedUpdate(
        "UPDATE " + kTestTableName + " SET score = ? WHERE id = ?",
        {ParameterWrapper(71.5), ParameterWrapper(int64_t(6))});
    EXPECT_TRUE(update_result.IsSuccess());
    EXPECT_EQ(update_result.GetAffectedRows(), 1);

    QueryResult verify_result = driver_->ExecuteQuery("SELECT name, score FROM " + kTestTableName + " WHERE id = 6");
    ASSERT_TRUE(verify_result.IsSuccess());
    EXPECT_EQ(verify_result.GetRow(0)[0], "name-6");
    EXPECT_EQ(verify_result.GetRow(0)[1], "71.5");
}

// 正常情况: 预编译语句绑定 NULL 参数(const char* 形式)成功写入空值
TEST_F(MySQLDatabaseDriverTest, PreparedInsertWithNullPointerParameter)
{
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name) VALUES (?, ?)",
        {ParameterWrapper(int64_t(7)), ParameterWrapper(static_cast<const char*>(nullptr))});
    EXPECT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name FROM " + kTestTableName + " WHERE id = 7");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0], "NULL");
}

// 异常情况: 预编译修改语句参数个数与占位符不匹配抛出 DB_PARAM_BIND_FAILED
TEST_F(MySQLDatabaseDriverTest, PreparedUpdateParameterCountMismatchThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedUpdate("INSERT INTO " + kTestTableName + " (id) VALUES (?)",
                                                             {}); },
                            ErrorCode::DB_PARAM_BIND_FAILED);
}

// 正常情况: 开启事务并提交后数据变更生效
TEST_F(MySQLDatabaseDriverTest, CommitTransaction)
{
    QueryResult begin_result = driver_->BeginTransaction();
    ASSERT_TRUE(begin_result.IsSuccess());
    QueryResult insert_result = driver_->ExecuteUpdate("REPLACE INTO " + kTestTableName +
                                                       " (id, name, score, is_active, remark) VALUES (8, 'name-8', 60.5, 1, NULL)");
    ASSERT_TRUE(insert_result.IsSuccess());
    QueryResult commit_result = driver_->CommitTransaction();
    ASSERT_TRUE(commit_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name FROM " + kTestTableName + " WHERE id = 8");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "name-8");
}

// 正常情况: 事务回滚后未提交的数据变更被撤销
TEST_F(MySQLDatabaseDriverTest, RollbackTransaction)
{
    QueryResult begin_result = driver_->BeginTransaction();
    ASSERT_TRUE(begin_result.IsSuccess());
    QueryResult insert_result = driver_->ExecuteUpdate("REPLACE INTO " + kTestTableName +
                                                       " (id, name, score, is_active, remark) VALUES (9, 'name-9', 50.5, 1, NULL)");
    ASSERT_TRUE(insert_result.IsSuccess());
    QueryResult rollback_result = driver_->RollbackTransaction();
    ASSERT_TRUE(rollback_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT id FROM " + kTestTableName + " WHERE id = 9");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 0);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台 : 驱动实现内部记录大量日志, 未初始化日志系统会因全局 logger 为空而崩溃
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "mysql_database_driver_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
