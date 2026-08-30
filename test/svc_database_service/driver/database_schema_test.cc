#include "svc_database_service/driver/database_schema.h"

#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

using chat_excel::DatabaseType;
using chat_excel::MySQLConfig;
using chat_excel::SQLiteConfig;
using chat_excel::SslConfig;

namespace
{

/**
 * @brief 构建一份合法的 MySQL 配置, 供各测试用例在其基础上修改字段构造异常场景
 * @return 合法的 MySQL 配置对象
 */
MySQLConfig BuildValidMySQLConfig()
{
    return MySQLConfig("127.0.0.1",
                       3306,
                       "root",
                       "password",
                       "chat_excel_test",
                       false,
                       SslConfig(),
                       std::unordered_map<std::string, std::string>());
}

/**
 * @brief 构建一份合法的 SQLite 配置, 供各测试用例在其基础上修改字段构造异常场景
 * @return 合法的 SQLite 配置对象
 */
SQLiteConfig BuildValidSQLiteConfig()
{
    return SQLiteConfig("/tmp/chat_excel_test.db", std::unordered_map<std::string, std::string>());
}

} // namespace

// 正常情况: 合法配置(主机名, 端口, 用户名, 数据库名称齐全)校验通过
TEST(MySQLConfigTest, AcceptValidConfig)
{
    EXPECT_TRUE(BuildValidMySQLConfig().CheckConfig());
}

// 正常情况: 密码为空的配置同样校验通过(密码不属于必填项)
TEST(MySQLConfigTest, AcceptEmptyPassword)
{
    MySQLConfig config = BuildValidMySQLConfig();
    config.password = "";
    EXPECT_TRUE(config.CheckConfig());
}

// 异常情况: 主机名, 用户名, 数据库名称为空时校验失败
TEST(MySQLConfigTest, RejectEmptyRequiredField)
{
    MySQLConfig empty_host = BuildValidMySQLConfig();
    empty_host.host = "";
    EXPECT_FALSE(empty_host.CheckConfig());

    MySQLConfig empty_user_name = BuildValidMySQLConfig();
    empty_user_name.user_name = "";
    EXPECT_FALSE(empty_user_name.CheckConfig());

    MySQLConfig empty_database_name = BuildValidMySQLConfig();
    empty_database_name.database_name = "";
    EXPECT_FALSE(empty_database_name.CheckConfig());
}

// 边界情况: 端口号取有效范围边界值 1 与 65535 时校验通过
TEST(MySQLConfigTest, AcceptPortBoundaryValue)
{
    MySQLConfig min_port_config = BuildValidMySQLConfig();
    min_port_config.port = 1;
    EXPECT_TRUE(min_port_config.CheckConfig());

    MySQLConfig max_port_config = BuildValidMySQLConfig();
    max_port_config.port = 65535;
    EXPECT_TRUE(max_port_config.CheckConfig());
}

// 边界情况: 端口号超出有效范围边界值(0 与 65536)时校验失败
TEST(MySQLConfigTest, RejectPortOutOfBoundary)
{
    MySQLConfig below_min_config = BuildValidMySQLConfig();
    below_min_config.port = 0;
    EXPECT_FALSE(below_min_config.CheckConfig());

    MySQLConfig above_max_config = BuildValidMySQLConfig();
    above_max_config.port = 65536;
    EXPECT_FALSE(above_max_config.CheckConfig());

    MySQLConfig negative_port_config = BuildValidMySQLConfig();
    negative_port_config.port = -1;
    EXPECT_FALSE(negative_port_config.CheckConfig());
}

// 正常情况: MySQL 配置的数据库类型固定返回 MYSQL
TEST(MySQLConfigTest, ReturnMySQLDatabaseType)
{
    EXPECT_EQ(BuildValidMySQLConfig().GetDatabaseType(), DatabaseType::MYSQL);
}

// 正常情况: 合法配置(数据库文件路径非空)校验通过
TEST(SQLiteConfigTest, AcceptValidConfig)
{
    EXPECT_TRUE(BuildValidSQLiteConfig().CheckConfig());
}

// 异常情况: 数据库文件路径为空时校验失败
TEST(SQLiteConfigTest, RejectEmptyDatabaseFilePath)
{
    SQLiteConfig config = BuildValidSQLiteConfig();
    config.database_file_path = "";
    EXPECT_FALSE(config.CheckConfig());
}

// 正常情况: SQLite 配置的数据库类型固定返回 SQLITE
TEST(SQLiteConfigTest, ReturnSQLiteDatabaseType)
{
    EXPECT_EQ(BuildValidSQLiteConfig().GetDatabaseType(), DatabaseType::SQLITE);
}
