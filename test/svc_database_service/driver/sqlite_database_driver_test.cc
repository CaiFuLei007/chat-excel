#include "svc_database_service/driver/sqlite_database_driver.h"

#include <cstdint>
#include <cstdio>
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
using chat_excel::ErrorCode;
using chat_excel::MySQLConfig;
using chat_excel::ParameterWrapper;
using chat_excel::QueryResult;
using chat_excel::SQLiteConfig;
using chat_excel::SslConfig;
using chat_excel::SQLiteDatabaseDriver;

namespace
{

// SQLite 测试表名
const std::string kTestTableName = "driver_test_table";

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
 * @brief 构建测试表的建表语句
 * @return 建表 SQL 语句
 */
std::string BuildCreateTableSql()
{
    return "CREATE TABLE IF NOT EXISTS " + kTestTableName +
           " (id INTEGER PRIMARY KEY, name TEXT, score REAL, is_active INTEGER)";
}

} // namespace

// 异常情况: 配置对象为空时构造驱动抛出 DB_CONFIG_INVALID
TEST(SQLiteDatabaseDriverConstructorTest, RejectNullConfig)
{
    std::shared_ptr<DatabaseConfig> empty_config;
    ExpectExceptionWithCode([&empty_config]()
                            { SQLiteDatabaseDriver driver(empty_config); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 异常情况: 配置类型不是 SQLite 时构造驱动抛出 DB_CONFIG_INVALID
TEST(SQLiteDatabaseDriverConstructorTest, RejectMismatchedConfigType)
{
    auto config = std::make_shared<MySQLConfig>("127.0.0.1", 3306, "root", "password",
                                                "chat_excel_test", false, SslConfig(),
                                                std::unordered_map<std::string, std::string>());
    ExpectExceptionWithCode([&config]()
                            { SQLiteDatabaseDriver driver(config); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 测试夹具 : 每个用例使用独立的内存数据库, 用例之间互不影响
class SQLiteDatabaseDriverTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto config = std::make_shared<SQLiteConfig>(":memory:", std::unordered_map<std::string, std::string>());
        driver_ = std::make_unique<SQLiteDatabaseDriver>(config);
        driver_->Connect();
    }

    void TearDown() override
    {
        if (driver_ != nullptr)
        {
            driver_->Disconnect();
        }
    }

    // 驱动对象
    std::unique_ptr<SQLiteDatabaseDriver> driver_;
};

// 正常情况: 连接内存数据库后心跳检测返回 true
TEST_F(SQLiteDatabaseDriverTest, TestConnectionSucceedAfterConnect)
{
    EXPECT_TRUE(driver_->TestConnection());
}

// 正常情况: 连接文件数据库, 心跳检测通过, 断开后自动清理测试文件
TEST(SQLiteDatabaseDriverFileTest, ConnectFileDatabase)
{
    // 测试前清理残留文件, 保证每次运行从全新数据库开始
    const std::string file_path = "/tmp/chat_excel_sqlite_driver_test.db";
    std::remove(file_path.c_str());

    auto config = std::make_shared<SQLiteConfig>(file_path, std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    driver.Connect();
    EXPECT_TRUE(driver.TestConnection());
    driver.Disconnect();
    std::remove(file_path.c_str());
}

// 异常情况: 配置无效(文件路径为空)时连接抛出 DB_CONFIG_INVALID
TEST(SQLiteDatabaseDriverFileTest, ConnectRejectInvalidConfig)
{
    auto config = std::make_shared<SQLiteConfig>("", std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.Connect(); },
                            ErrorCode::DB_CONFIG_INVALID);
}

// 异常情况: 数据库文件路径无法打开(目录不存在)时连接抛出 DB_CONNECTION_FAILED
TEST(SQLiteDatabaseDriverFileTest, ConnectRejectUnavailableFilePath)
{
    auto config = std::make_shared<SQLiteConfig>("/nonexistent_dir_xyz/chat_excel_test.db",
                                                 std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.Connect(); },
                            ErrorCode::DB_CONNECTION_FAILED);
}

// 正常情况: PRAGMA 配置(foreign_keys=ON)应用后外键约束生效, 插入孤儿数据失败
TEST(SQLiteDatabaseDriverPragmaTest, ApplyPragmaConfiguration)
{
    std::unordered_map<std::string, std::string> other_config;
    other_config["foreign_keys"] = "ON";
    auto config = std::make_shared<SQLiteConfig>(":memory:", other_config);
    SQLiteDatabaseDriver driver(config);
    driver.Connect();
    ASSERT_TRUE(driver.ExecuteUpdate("CREATE TABLE pragma_parent (id INTEGER PRIMARY KEY)").IsSuccess());
    ASSERT_TRUE(driver.ExecuteUpdate("CREATE TABLE pragma_child "
                                     "(parent_id INTEGER, "
                                     "FOREIGN KEY(parent_id) REFERENCES pragma_parent(id))")
                    .IsSuccess());
    // 外键约束开启后, 插入不存在父记录的子数据失败
    QueryResult result = driver.ExecuteUpdate("INSERT INTO pragma_child (parent_id) VALUES (999)");
    EXPECT_FALSE(result.IsSuccess());
}

// 正常情况: 未配置 PRAGMA 时 SQLite 默认关闭外键约束, 同样的孤儿数据插入成功
TEST(SQLiteDatabaseDriverPragmaTest, SkipPragmaWhenNotConfigured)
{
    auto config = std::make_shared<SQLiteConfig>(":memory:", std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    driver.Connect();
    ASSERT_TRUE(driver.ExecuteUpdate("CREATE TABLE pragma_parent (id INTEGER PRIMARY KEY)").IsSuccess());
    ASSERT_TRUE(driver.ExecuteUpdate("CREATE TABLE pragma_child "
                                     "(parent_id INTEGER, "
                                     "FOREIGN KEY(parent_id) REFERENCES pragma_parent(id))")
                    .IsSuccess());
    QueryResult result = driver.ExecuteUpdate("INSERT INTO pragma_child (parent_id) VALUES (999)");
    EXPECT_TRUE(result.IsSuccess());
}

// 正常情况: 无效的 PRAGMA 配置仅记录告警, 不影响连接建立
TEST(SQLiteDatabaseDriverPragmaTest, TolerateInvalidPragma)
{
    std::unordered_map<std::string, std::string> other_config;
    other_config["invalid_pragma_key"] = "invalid_pragma_value";
    auto config = std::make_shared<SQLiteConfig>(":memory:", other_config);
    SQLiteDatabaseDriver driver(config);
    driver.Connect();
    EXPECT_TRUE(driver.TestConnection());
}

// 正常情况: 断开连接后心跳检测返回 false
TEST_F(SQLiteDatabaseDriverTest, TestConnectionFailAfterDisconnect)
{
    driver_->Disconnect();
    EXPECT_FALSE(driver_->TestConnection());
}

// 正常情况: 未连接时调用断开连接为空操作, 不抛出异常
TEST(SQLiteDatabaseDriverDisconnectTest, DisconnectWithoutConnectIsNoOp)
{
    auto config = std::make_shared<SQLiteConfig>(":memory:", std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    driver.Disconnect();
}

// 正常情况: 标识符使用双引号包裹, 内部双引号双写转义
TEST_F(SQLiteDatabaseDriverTest, QuoteAndEscapeIdentifier)
{
    EXPECT_EQ(driver_->QuoteIdentifier("users"), "\"users\"");
    EXPECT_EQ(driver_->QuoteIdentifier("us\"ers"), "\"us\"\"ers\"");
    EXPECT_EQ(driver_->QuoteIdentifier(""), "\"\"");
}

// 正常情况: 执行查询语句返回列信息与行数据, 数据以字符串形式呈现
TEST_F(SQLiteDatabaseDriverTest, QueryRowsAsString)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)")
                    .IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT id, name, score, is_active FROM " + kTestTableName);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetColumnCount(), 4);
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetColumnNames()[0], "id");
    EXPECT_EQ(result.GetColumnNames()[1], "name");
    EXPECT_EQ(result.GetRow(0)[0], "1");
    EXPECT_EQ(result.GetRow(0)[1], "张三");
    EXPECT_EQ(result.GetRow(0)[2], "95.5");
    EXPECT_EQ(result.GetRow(0)[3], "1");
}

// 正常情况: 查询空表返回 0 行
TEST_F(SQLiteDatabaseDriverTest, QueryEmptyTableReturnNoRows)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    QueryResult result = driver_->ExecuteQuery("SELECT id, name FROM " + kTestTableName);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 0);
}

// 异常情况: 未建立连接时执行查询抛出 DB_NOT_CONNECTED
TEST_F(SQLiteDatabaseDriverTest, QueryWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("SELECT 1"); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 异常情况: 执行语法错误的查询语句返回失败结果并携带错误信息
TEST_F(SQLiteDatabaseDriverTest, QueryInvalidSqlReturnFailedResult)
{
    QueryResult result = driver_->ExecuteQuery("SELECT * FROM table_not_exists");
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_FALSE(result.GetErrorMessage().empty());
}

// 异常情况: 查询通路拒绝修改类语句, 抛出 DB_SQL_INVALID
TEST_F(SQLiteDatabaseDriverTest, QueryPathRejectModifySql)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("INSERT INTO " + kTestTableName + " VALUES (1, 'a', 1.0, 1)"); },
                            ErrorCode::DB_SQL_INVALID);
}

// 异常情况: 查询语句为空抛出 DB_SQL_EMPTY
TEST_F(SQLiteDatabaseDriverTest, QueryEmptySqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("   "); },
                            ErrorCode::DB_SQL_EMPTY);
}

// 异常情况: 查询语句包含危险操作抛出 DB_SQL_DANGEROUS
TEST_F(SQLiteDatabaseDriverTest, QueryDangerousSqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("DROP TABLE " + kTestTableName); },
                            ErrorCode::DB_SQL_DANGEROUS);
}

// 异常情况: 查询语句包含多条语句抛出 DB_SQL_MULTIPLE_STATEMENTS
TEST_F(SQLiteDatabaseDriverTest, QueryMultipleStatementsThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteQuery("SELECT 1; SELECT 2"); },
                            ErrorCode::DB_SQL_MULTIPLE_STATEMENTS);
}

// 正常情况: 执行建表语句成功, 影响行数为 0
TEST_F(SQLiteDatabaseDriverTest, CreateTable)
{
    QueryResult result = driver_->ExecuteUpdate(BuildCreateTableSql());
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetAffectedRows(), 0);
}

// 正常情况: 执行插入语句成功并返回正确的影响行数
TEST_F(SQLiteDatabaseDriverTest, InsertRowsReturnAffectedRows)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    QueryResult first_result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                      " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)");
    EXPECT_TRUE(first_result.IsSuccess());
    EXPECT_EQ(first_result.GetAffectedRows(), 1);

    QueryResult second_result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                       " (id, name, score, is_active) VALUES (2, '李四', 80.0, 0)");
    EXPECT_EQ(second_result.GetAffectedRows(), 1);
}

// 正常情况: 执行更新语句返回受影响行数, 无匹配行时影响行数为 0
TEST_F(SQLiteDatabaseDriverTest, UpdateRowsReturnAffectedRows)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)")
                    .IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (2, '李四', 80.0, 1)")
                    .IsSuccess());

    QueryResult result = driver_->ExecuteUpdate("UPDATE " + kTestTableName + " SET score = 60.0 WHERE is_active = 1");
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetAffectedRows(), 2);

    QueryResult no_match_result = driver_->ExecuteUpdate("UPDATE " + kTestTableName +
                                                         " SET score = 60.0 WHERE id = 999");
    EXPECT_TRUE(no_match_result.IsSuccess());
    EXPECT_EQ(no_match_result.GetAffectedRows(), 0);
}

// 异常情况: 未建立连接时执行修改语句抛出 DB_NOT_CONNECTED
TEST_F(SQLiteDatabaseDriverTest, UpdateWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("CREATE TABLE t (id INTEGER)"); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 异常情况: 修改通路拒绝只读语句, 抛出 DB_SQL_INVALID
TEST_F(SQLiteDatabaseDriverTest, UpdatePathRejectReadOnlySql)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("SELECT 1"); },
                            ErrorCode::DB_SQL_INVALID);
}

// 异常情况: 修改语句包含危险操作抛出 DB_SQL_DANGEROUS
TEST_F(SQLiteDatabaseDriverTest, UpdateDangerousSqlThrow)
{
    ExpectExceptionWithCode([&]()
                            { driver_->ExecuteUpdate("TRUNCATE TABLE " + kTestTableName); },
                            ErrorCode::DB_SQL_DANGEROUS);
}

// 异常情况: 违反表约束(主键冲突)的修改语句返回失败结果
TEST_F(SQLiteDatabaseDriverTest, UpdateConstraintViolationReturnFailedResult)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)")
                    .IsSuccess());
    QueryResult result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                " (id, name, score, is_active) VALUES (1, '王五', 70.0, 1)");
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_FALSE(result.GetErrorMessage().empty());
}

// 正常情况: 预编译查询绑定整数与布尔参数, 返回匹配行
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithIntAndBoolParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)")
                    .IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (2, '李四', 80.0, 1)")
                    .IsSuccess());

    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT name FROM " + kTestTableName + " WHERE id = ? AND is_active = ?",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(true)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "张三");
}

// 正常情况: 预编译查询绑定浮点数参数
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithDoubleParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                       " (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)")
                    .IsSuccess());

    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT id FROM " + kTestTableName + " WHERE score = ?",
        {ParameterWrapper(95.5)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
    EXPECT_EQ(result.GetRow(0)[0], "1");
}

// 正常情况: 预编译语句绑定 NULL 参数后, 查询结果中的空值以 NULL 字符串呈现
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithNullParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    // NULL 参数绑定 : name 列写入空值
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name, score, is_active) VALUES (?, ?, ?, ?)",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(), ParameterWrapper(88.5), ParameterWrapper(true)});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name, score FROM " + kTestTableName + " WHERE id = 1");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0], "NULL");
    EXPECT_EQ(result.GetRow(0)[1], "88.5");
}

// 正常情况: 预编译查询绑定字符串参数, 中文与特殊字符均可正确匹配
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithSpecialCharacterParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    std::string special_name = "张三's \"note\";--注释";
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name) VALUES (?, ?)",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(special_name)});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecutePreparedQuery(
        "SELECT id FROM " + kTestTableName + " WHERE name = ?",
        {ParameterWrapper(special_name)});
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
}

// 边界情况: 预编译语句绑定超长字符串参数, 数据完整写入与读出
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithLongStringParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    // 超过 SQLite 默认 1KB 页大小, 验证溢出页存储路径
    std::string long_name(10000, 'a');
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name) VALUES (?, ?)",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(long_name)});
    ASSERT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name FROM " + kTestTableName + " WHERE id = 1");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0].size(), long_name.size());
}

// 异常情况: 预编译查询参数个数与占位符不匹配抛出 DB_PARAM_BIND_FAILED
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryParameterCountMismatchThrow)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedQuery("SELECT name FROM " + kTestTableName + " WHERE id = ?",
                                                            {ParameterWrapper(int64_t(1)),
                                                             ParameterWrapper("额外参数")}); },
                            ErrorCode::DB_PARAM_BIND_FAILED);
}

// 异常情况: 未建立连接时预编译查询抛出 DB_NOT_CONNECTED
TEST_F(SQLiteDatabaseDriverTest, PreparedQueryWithoutConnectThrow)
{
    driver_->Disconnect();
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedQuery("SELECT name FROM t WHERE id = ?",
                                                            {ParameterWrapper(int64_t(1))}); },
                            ErrorCode::DB_NOT_CONNECTED);
}

// 正常情况: 预编译修改语句成功执行并返回影响行数
TEST_F(SQLiteDatabaseDriverTest, PreparedInsertAndUpdateWithParameters)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());

    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name, score, is_active) VALUES (?, ?, ?, ?)",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(std::string("张三")),
         ParameterWrapper(95.5), ParameterWrapper(true)});
    EXPECT_TRUE(insert_result.IsSuccess());
    EXPECT_EQ(insert_result.GetAffectedRows(), 1);

    QueryResult update_result = driver_->ExecutePreparedUpdate(
        "UPDATE " + kTestTableName + " SET score = ? WHERE id = ?",
        {ParameterWrapper(60.0), ParameterWrapper(int64_t(1))});
    EXPECT_TRUE(update_result.IsSuccess());
    EXPECT_EQ(update_result.GetAffectedRows(), 1);
}

// 正常情况: 预编译语句绑定 NULL 参数(const char* 形式)成功写入空值
TEST_F(SQLiteDatabaseDriverTest, PreparedInsertWithNullPointerParameter)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    QueryResult insert_result = driver_->ExecutePreparedUpdate(
        "INSERT INTO " + kTestTableName + " (id, name) VALUES (?, ?)",
        {ParameterWrapper(int64_t(1)), ParameterWrapper(static_cast<const char*>(nullptr))});
    EXPECT_TRUE(insert_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT name FROM " + kTestTableName + " WHERE id = 1");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRow(0)[0], "NULL");
}

// 异常情况: 预编译修改语句参数个数与占位符不匹配抛出 DB_PARAM_BIND_FAILED
TEST_F(SQLiteDatabaseDriverTest, PreparedUpdateParameterCountMismatchThrow)
{
    // 先建表保证预编译成功, 参数个数校验发生在预编译之后的绑定阶段
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ExpectExceptionWithCode([&]()
                            { driver_->ExecutePreparedUpdate("INSERT INTO " + kTestTableName + " (id) VALUES (?)",
                                                             {}); },
                            ErrorCode::DB_PARAM_BIND_FAILED);
}

// 正常情况: 开启事务并提交后数据变更生效
TEST_F(SQLiteDatabaseDriverTest, CommitTransaction)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());

    QueryResult begin_result = driver_->BeginTransaction();
    ASSERT_TRUE(begin_result.IsSuccess());
    QueryResult insert_result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                       " (id, name) VALUES (1, '张三')");
    ASSERT_TRUE(insert_result.IsSuccess());
    QueryResult commit_result = driver_->CommitTransaction();
    ASSERT_TRUE(commit_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT id, name FROM " + kTestTableName);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
}

// 正常情况: 事务回滚后未提交的数据变更被撤销
TEST_F(SQLiteDatabaseDriverTest, RollbackTransaction)
{
    ASSERT_TRUE(driver_->ExecuteUpdate(BuildCreateTableSql()).IsSuccess());
    ASSERT_TRUE(driver_->ExecuteUpdate("INSERT INTO " + kTestTableName + " (id, name) VALUES (1, '张三')").IsSuccess());

    QueryResult begin_result = driver_->BeginTransaction();
    ASSERT_TRUE(begin_result.IsSuccess());
    QueryResult insert_result = driver_->ExecuteUpdate("INSERT INTO " + kTestTableName +
                                                       " (id, name) VALUES (2, '李四')");
    ASSERT_TRUE(insert_result.IsSuccess());
    QueryResult rollback_result = driver_->RollbackTransaction();
    ASSERT_TRUE(rollback_result.IsSuccess());

    QueryResult result = driver_->ExecuteQuery("SELECT id, name FROM " + kTestTableName);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetRowCount(), 1);
}

// 异常情况: 未建立连接时开启事务抛出 DB_NOT_CONNECTED
TEST(SQLiteDatabaseDriverDisconnectTest, BeginTransactionWithoutConnectThrow)
{
    auto config = std::make_shared<SQLiteConfig>(":memory:", std::unordered_map<std::string, std::string>());
    SQLiteDatabaseDriver driver(config);
    ExpectExceptionWithCode([&driver]()
                            { driver.BeginTransaction(); },
                            ErrorCode::DB_NOT_CONNECTED);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台 : 驱动实现内部记录大量日志, 未初始化日志系统会因全局 logger 为空而崩溃
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "sqlite_database_driver_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
