#include "svc_database_service/driver/sql_validator.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using chat_excel::kMaxIdentifierLength;
using chat_excel::SqlType;
using chat_excel::SQLValidator;

// 正常情况: 各支持的 SQL 类型返回对应的语句类型
TEST(GetSqlTypeTest, ReturnTypeForEachSupportedKeyword)
{
    EXPECT_EQ(SQLValidator::GetSqlType("SELECT 1"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("SHOW DATABASES"), SqlType::SHOW);
    EXPECT_EQ(SQLValidator::GetSqlType("DESC users"), SqlType::DESC);
    EXPECT_EQ(SQLValidator::GetSqlType("DESCRIBE users"), SqlType::DESC);
    EXPECT_EQ(SQLValidator::GetSqlType("INSERT INTO users (name) VALUES ('tom')"), SqlType::INSERT);
    EXPECT_EQ(SQLValidator::GetSqlType("UPDATE users SET name = 'tom'"), SqlType::UPDATE);
    EXPECT_EQ(SQLValidator::GetSqlType("DELETE FROM logs"), SqlType::DELETE);
    EXPECT_EQ(SQLValidator::GetSqlType("REPLACE INTO dict VALUES (1)"), SqlType::REPLACE);
    EXPECT_EQ(SQLValidator::GetSqlType("TRUNCATE TABLE logs"), SqlType::TRUNCATE);
    EXPECT_EQ(SQLValidator::GetSqlType("CREATE TABLE logs (id INT)"), SqlType::CREATE);
    EXPECT_EQ(SQLValidator::GetSqlType("DROP TABLE logs"), SqlType::DROP);
    EXPECT_EQ(SQLValidator::GetSqlType("ALTER TABLE logs ADD COLUMN remark VARCHAR(64)"), SqlType::ALTER);
}

// 正常情况: 关键字大小写不敏感, 允许语句前后存在空白字符
TEST(GetSqlTypeTest, CaseInsensitiveAndIgnoreLeadingWhitespace)
{
    EXPECT_EQ(SQLValidator::GetSqlType("select * from users"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("  \t\n Select 1"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("Show Tables"), SqlType::SHOW);
    EXPECT_EQ(SQLValidator::GetSqlType("insert into users (name) values ('tom')"), SqlType::INSERT);
}

// 异常情况: 不支持的语句类型与空语句返回 UNKNOWN
TEST(GetSqlTypeTest, ReturnUnknownForUnsupportedStatement)
{
    EXPECT_EQ(SQLValidator::GetSqlType("GRANT ALL ON *.* TO 'guest'"), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::GetSqlType("BEGIN"), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::GetSqlType("COMMIT"), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::GetSqlType(""), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::GetSqlType("   \t\n  "), SqlType::UNKNOWN);
}

// 正常情况: 语句开头的 -- 单行注释被移除, 不影响语句类型识别(模型生成的 SQL 常带注释)
TEST(GetSqlTypeTest, IgnoreLeadingDashDashComment)
{
    EXPECT_EQ(SQLValidator::GetSqlType("-- 统计用户总数\nSELECT COUNT(*) FROM users"),
              SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("-- 注释一\n-- 注释二\nSELECT 1"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("# MySQL 井号注释\nSELECT 1"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::GetSqlType("-- 注释\nUPDATE users SET name = 'tom'"),
              SqlType::UPDATE);
}

// 边界情况: 双负号后无空白时是双负号运算而非注释, 不能误判
TEST(GetSqlTypeTest, DoubleMinusWithoutWhitespaceIsNotComment)
{
    EXPECT_EQ(SQLValidator::GetSqlType("SELECT 1--2"), SqlType::SELECT);
}

// 正常情况: SELECT/SHOW/DESC 判定为只读语句
TEST(IsReadOnlySqlTest, ReturnTrueForReadOnlyStatement)
{
    EXPECT_TRUE(SQLValidator::IsReadOnlySql("SELECT * FROM users"));
    EXPECT_TRUE(SQLValidator::IsReadOnlySql("show tables"));
    EXPECT_TRUE(SQLValidator::IsReadOnlySql("DESC users"));
    EXPECT_TRUE(SQLValidator::IsReadOnlySql("DESCRIBE users"));
}

// 异常情况: 修改类语句与事务语句不属于只读语句
TEST(IsReadOnlySqlTest, ReturnFalseForNonReadOnlyStatement)
{
    EXPECT_FALSE(SQLValidator::IsReadOnlySql("INSERT INTO users (name) VALUES ('tom')"));
    EXPECT_FALSE(SQLValidator::IsReadOnlySql("UPDATE users SET name = 'tom'"));
    EXPECT_FALSE(SQLValidator::IsReadOnlySql("CREATE TABLE logs (id INT)"));
    EXPECT_FALSE(SQLValidator::IsReadOnlySql("BEGIN"));
}

// 正常情况: 各修改类语句判定为修改语句
TEST(IsModifySqlTest, ReturnTrueForModifyStatement)
{
    EXPECT_TRUE(SQLValidator::IsModifySql("INSERT INTO users (name) VALUES ('tom')"));
    EXPECT_TRUE(SQLValidator::IsModifySql("UPDATE users SET name = 'tom'"));
    EXPECT_TRUE(SQLValidator::IsModifySql("DELETE FROM logs"));
    EXPECT_TRUE(SQLValidator::IsModifySql("REPLACE INTO dict VALUES (1)"));
    EXPECT_TRUE(SQLValidator::IsModifySql("TRUNCATE TABLE logs"));
    EXPECT_TRUE(SQLValidator::IsModifySql("CREATE TABLE logs (id INT)"));
    EXPECT_TRUE(SQLValidator::IsModifySql("DROP TABLE logs"));
    EXPECT_TRUE(SQLValidator::IsModifySql("ALTER TABLE logs ADD COLUMN remark VARCHAR(64)"));
}

// 异常情况: 只读语句与事务语句不属于修改语句
TEST(IsModifySqlTest, ReturnFalseForNonModifyStatement)
{
    EXPECT_FALSE(SQLValidator::IsModifySql("SELECT * FROM users"));
    EXPECT_FALSE(SQLValidator::IsModifySql("SHOW TABLES"));
    EXPECT_FALSE(SQLValidator::IsModifySql("DESC users"));
    EXPECT_FALSE(SQLValidator::IsModifySql("BEGIN"));
    EXPECT_FALSE(SQLValidator::IsModifySql("COMMIT"));
}

// 正常情况: 各事务控制语句判定为事务语句, 大小写不敏感
TEST(IsTransactionStatementTest, ReturnTrueForTransactionStatement)
{
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("BEGIN"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("begin work"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("START TRANSACTION"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("start transaction"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("COMMIT"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("commit"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("ROLLBACK"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("SAVEPOINT sp1"));
    EXPECT_TRUE(SQLValidator::IsTransactionStatement("RELEASE SAVEPOINT sp1"));
}

// 异常情况: 以事务关键字为前缀的其他单词与非事务语句不判定为事务语句
TEST(IsTransactionStatementTest, ReturnFalseForNonTransactionStatement)
{
    EXPECT_FALSE(SQLValidator::IsTransactionStatement("COMMITTED"));
    EXPECT_FALSE(SQLValidator::IsTransactionStatement("SELECT 1"));
    EXPECT_FALSE(SQLValidator::IsTransactionStatement("INSERT INTO users (name) VALUES ('BEGIN')"));
    EXPECT_FALSE(SQLValidator::IsTransactionStatement(""));
}

// 正常情况: 字母, 数字, 下划线, 汉字, 连接符, 点, 库名.表名 形式均为有效表名
TEST(IsValidTableNameTest, AcceptValidTableName)
{
    EXPECT_TRUE(SQLValidator::IsValidTableName("users"));
    EXPECT_TRUE(SQLValidator::IsValidTableName("_tmp_data"));
    EXPECT_TRUE(SQLValidator::IsValidTableName("t1"));
    EXPECT_TRUE(SQLValidator::IsValidTableName("学生表"));
    EXPECT_TRUE(SQLValidator::IsValidTableName("my-table"));
    EXPECT_TRUE(SQLValidator::IsValidTableName("db.orders"));
}

// 异常情况: 空表名, 数字开头, 连续连接符/点, 空格与特殊字符均为无效表名
TEST(IsValidTableNameTest, RejectInvalidTableName)
{
    EXPECT_FALSE(SQLValidator::IsValidTableName(""));
    EXPECT_FALSE(SQLValidator::IsValidTableName("1table"));
    EXPECT_FALSE(SQLValidator::IsValidTableName("a..b"));
    EXPECT_FALSE(SQLValidator::IsValidTableName("a--b"));
    EXPECT_FALSE(SQLValidator::IsValidTableName("my table"));
    EXPECT_FALSE(SQLValidator::IsValidTableName("tab@le"));
}

// 边界情况: 恰好达到最大长度限制的表名有效, 超过最大长度限制的表名无效
TEST(IsValidTableNameTest, CheckMaxLengthBoundary)
{
    EXPECT_TRUE(SQLValidator::IsValidTableName(std::string(kMaxIdentifierLength, 'a')));
    EXPECT_FALSE(SQLValidator::IsValidTableName(std::string(kMaxIdentifierLength + 1, 'a')));
}

// 边界情况: 表名长度按 UTF-8 字符统计(汉字等多字节字符计 1 个字符), 而非按字节数
TEST(IsValidTableNameTest, CountLengthByUtf8Characters)
{
    // 66 字节 / 48 字符的表名(汉字 + 全角括号), 在 MySQL 标识符 64 字符上限内, 应为有效表名
    EXPECT_TRUE(SQLValidator::IsValidTableName(
        "excel_25879187e1714aafa85f35eece556fb4_考场明细（学生用）"));
    // 64 个汉字(192 字节)恰好达到上限有效, 65 个汉字超过上限无效
    // 注意: 不能使用 '汉' 多字符字面量填充 std::string, 需按 UTF-8 字符串重复拼接
    std::string chinese_64;
    for (size_t i = 0; i < kMaxIdentifierLength; ++i)
    {
        chinese_64 += "汉";
    }
    EXPECT_TRUE(SQLValidator::IsValidTableName(chinese_64));
    EXPECT_FALSE(SQLValidator::IsValidTableName(chinese_64 + "汉"));
}

// 正常情况: 普通列名与引号包裹的含特殊字符列名均为有效列名
TEST(IsValidColumnNameTest, AcceptValidColumnName)
{
    EXPECT_TRUE(SQLValidator::IsValidColumnName("name"));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("_count"));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("a"));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("`col-name`"));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("'col.name'"));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("\"123col\""));
    EXPECT_TRUE(SQLValidator::IsValidColumnName("`成绩`"));
}

// 异常情况: 未用引号包裹的含特殊字符列名, 空列名与数字开头列名无效
TEST(IsValidColumnNameTest, RejectInvalidColumnName)
{
    EXPECT_FALSE(SQLValidator::IsValidColumnName("col-name"));
    EXPECT_FALSE(SQLValidator::IsValidColumnName("col.name"));
    EXPECT_FALSE(SQLValidator::IsValidColumnName(""));
    EXPECT_FALSE(SQLValidator::IsValidColumnName("1col"));
    EXPECT_FALSE(SQLValidator::IsValidColumnName("-"));
}

// 正常情况: 标识符未超长时原样返回, 空字符串保持原样
TEST(TruncateIdentifierTest, ReturnOriginalWhenWithinLimit)
{
    EXPECT_EQ(SQLValidator::TruncateIdentifier("excel_table", 64), "excel_table");
    EXPECT_EQ(SQLValidator::TruncateIdentifier("", 10), "");
    EXPECT_EQ(SQLValidator::TruncateIdentifier("汉字表", 3), "汉字表");
}

// 正常情况: 标识符超长时按字符数截断, 且不会切断 UTF-8 多字节字符
TEST(TruncateIdentifierTest, TruncateByCharactersNotBytes)
{
    // 7 个 ASCII 字符 + 6 个汉字共 13 个字符(25 字节), 截断到 8 个字符
    const std::string source = "abcdefg汉字汉字汉字";
    EXPECT_EQ(SQLValidator::TruncateIdentifier(source, 8), "abcdefg汉");
    EXPECT_EQ(SQLValidator::TruncateIdentifier(source, 0), "");
    EXPECT_EQ(SQLValidator::TruncateIdentifier(std::string(20, 'a'), 10), std::string(10, 'a'));
}

// 正常情况: 各常见注入攻击特征命中危险操作判定, 大小写不敏感
TEST(ContainsDangerousOperationTest, ReturnTrueForDangerousKeyword)
{
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("DROP DATABASE shop"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("drop table users"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("TRUNCATE TABLE logs"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("EXEC sp_help"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT * FROM users WHERE id = 1 OR 1=1"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT SLEEP(10)"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT BENCHMARK(1000000, MD5('x'))"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT LOAD_FILE('/etc/passwd')"));
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT * FROM users INTO OUTFILE '/tmp/data'"));
}

// 正常情况: 连续空白合并后仍可命中带空格的危险关键字
TEST(ContainsDangerousOperationTest, MatchKeywordAfterWhitespaceCollapse)
{
    EXPECT_TRUE(SQLValidator::ContainsDangerousOperation("SELECT * FROM users WHERE id = 1 OR  '1'='1'"));
}

// 正常情况: 注释中的危险关键字不参与匹配
TEST(ContainsDangerousOperationTest, IgnoreKeywordInsideComment)
{
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SELECT 1 // DROP TABLE users"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SELECT /* DELETE FROM logs */ 1"));
}

// 正常情况: 普通查询与写入语句不命中危险操作判定
TEST(ContainsDangerousOperationTest, ReturnFalseForSafeStatement)
{
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SELECT name, age FROM users WHERE id = 42"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("INSERT INTO students (name) VALUES ('小明')"));
    // DELETE 是修改类语句, 由业务层在临时表上执行保护原表, 不判定为危险操作
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("DELETE FROM logs WHERE id = 1"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SHOW TABLES"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(""));
}

// 正常情况: UNION ALL 是合法的只读合并查询语法, 不判定为危险操作
// (回归用例: 模型生成的多表合并统计 SQL 曾因 "UNION ALL SELECT" 关键字被误判)
TEST(ContainsDangerousOperationTest, ReturnFalseForUnionAllQuery)
{
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT `专业`, COUNT(*) FROM t1 UNION ALL SELECT `专业`, COUNT(*) FROM t2 GROUP BY `专业`"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "-- 合并两表统计人数\nSELECT name FROM students_a UNION ALL SELECT name FROM students_b"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT id FROM users UNION ALL SELECT id FROM orders LIMIT 100;"));
}

// 正常情况: 表名/字段名中包含危险关键字字样不判定为危险操作, 完整词匹配排除误判
// (回归用例: SELECT description FROM products 曾因 description 包含 script 字样被误判)
TEST(ContainsDangerousOperationTest, IgnoreKeywordInsideIdentifier)
{
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SELECT description FROM products"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation("SELECT `script` FROM products"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT * FROM task WHERE execute_time > '2024-01-01'"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT evaluate_score, eval_remark FROM student"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT * FROM transcript WHERE student_id = 1"));
}

// 正常情况: 字符串数据中包含危险关键字不判定为危险操作, 引号内容匹配前被遮蔽
TEST(ContainsDangerousOperationTest, IgnoreKeywordInsideStringLiteral)
{
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT * FROM logs WHERE content = 'DROP TABLE users'"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "INSERT INTO articles (title) VALUES ('如何用 JavaScript 编写脚本')"));
    EXPECT_FALSE(SQLValidator::ContainsDangerousOperation(
        "SELECT * FROM users WHERE name = 'exec' AND age > 18"));
}

// 正常情况: 末尾单个分号为语句结束符, 不判定为多条语句
TEST(ContainsMultipleStatementsTest, AllowTrailingSemicolon)
{
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT 1;"));
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("select * from users;"));
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT 1"));
}

// 异常情况: 语句中间出现分号判定为多条语句, 连续多个分号同样命中
TEST(ContainsMultipleStatementsTest, ReturnTrueForMultipleStatements)
{
    EXPECT_TRUE(SQLValidator::ContainsMultipleStatements("SELECT 1; SELECT 2"));
    EXPECT_TRUE(SQLValidator::ContainsMultipleStatements("SELECT 1; SELECT 2;"));
    EXPECT_TRUE(SQLValidator::ContainsMultipleStatements("SELECT 1; ;"));
}

// 正常情况: 引号字符串与注释内部的分号不参与多条语句判定
TEST(ContainsMultipleStatementsTest, IgnoreSemicolonInsideStringAndComment)
{
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT * FROM users WHERE name = 'a;b'"));
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT * FROM `order;detail`"));
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT 1 /* ; */"));
    EXPECT_FALSE(SQLValidator::ContainsMultipleStatements("SELECT 1 // 注释; 不会结束语句"));
}

// 正常情况: 单表查询, 多表连接, 写入, 更新, 删除, 建表等场景均可提取表名
TEST(ExtractTableNamesTest, ExtractTableNamesForCommonStatement)
{
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT * FROM users"), std::vector<std::string>({"users"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT * FROM orders o JOIN users u ON o.id = u.user_id"),
              std::vector<std::string>({"orders", "users"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("INSERT INTO students (name) VALUES ('tom')"),
              std::vector<std::string>({"students"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("UPDATE users SET name = 'tom' WHERE id = 1"),
              std::vector<std::string>({"users"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("DELETE FROM logs WHERE id < 10"),
              std::vector<std::string>({"logs"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("CREATE TABLE IF NOT EXISTS student_score (id INT)"),
              std::vector<std::string>({"student_score"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("DROP TABLE IF EXISTS temp_data"),
              std::vector<std::string>({"temp_data"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("REPLACE INTO dict VALUES (1)"),
              std::vector<std::string>({"dict"}));
}

// 正常情况: 支持逗号分隔多表与 AS 别名形式, 库名.表名 形式整体提取
TEST(ExtractTableNamesTest, ExtractMultipleTablesWithCommaAndAlias)
{
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT * FROM t1 AS a, t2"),
              std::vector<std::string>({"t1", "t2"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT name FROM db.orders"),
              std::vector<std::string>({"db.orders"}));
}

// 正常情况: 重复出现的表名按出现顺序去重, 字符串字面量不参与提取
TEST(ExtractTableNamesTest, DeduplicateAndIgnoreStringLiteral)
{
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT * FROM users JOIN users ON users.id = 1"),
              std::vector<std::string>({"users"}));
    EXPECT_EQ(SQLValidator::ExtractTableNames("SELECT * FROM t WHERE name = 'users'"),
              std::vector<std::string>({"t"}));
}

// 异常情况: 未携带表名的语句返回空列表
TEST(ExtractTableNamesTest, ReturnEmptyListWhenNoTableName)
{
    EXPECT_TRUE(SQLValidator::ExtractTableNames("SELECT 1").empty());
    EXPECT_TRUE(SQLValidator::ExtractTableNames("SHOW DATABASES").empty());
    EXPECT_TRUE(SQLValidator::ExtractTableNames("").empty());
}

// 正常情况: 去除语句前后各类空白字符, 全空白语句返回空字符串
TEST(TrimSqlTest, TrimWhitespaceOnBothSides)
{
    EXPECT_EQ(SQLValidator::TrimSql("  SELECT 1  "), "SELECT 1");
    EXPECT_EQ(SQLValidator::TrimSql("\t\n select \r\n"), "select");
    EXPECT_EQ(SQLValidator::TrimSql("   "), "");
    EXPECT_EQ(SQLValidator::TrimSql(""), "");
}

// 正常情况: 单行注释与多行注释被移除, 引号内的注释符不视为注释
TEST(RemoveCommentsTest, RemoveLineAndBlockComment)
{
    EXPECT_EQ(SQLValidator::RemoveComments("SELECT 1 // comment"), "SELECT 1  ");
    EXPECT_EQ(SQLValidator::RemoveComments("SELECT /* hint */ 1"), "SELECT   1");
    EXPECT_EQ(SQLValidator::RemoveComments("SELECT '// not comment' FROM t"), "SELECT '// not comment' FROM t");
    EXPECT_EQ(SQLValidator::RemoveComments(""), "");
}

// 正常情况: 规范化为移除注释并去除前后空白后的语句
TEST(NormalizeSqlTest, CombineRemoveCommentAndTrim)
{
    EXPECT_EQ(SQLValidator::NormalizeSql("  SELECT 1  "), "SELECT 1");
    EXPECT_EQ(SQLValidator::NormalizeSql("// 说明\nSELECT 1"), "SELECT 1");
    EXPECT_EQ(SQLValidator::NormalizeSql("SELECT /* c */ 1"), "SELECT   1");
}

// 正常情况: 合法的只读与写入语句判定有效, 事务控制语句判定有效
TEST(IsValidSqlTest, ReturnTrueForValidStatement)
{
    EXPECT_TRUE(SQLValidator::IsValidSql("SELECT * FROM users WHERE id = 1"));
    EXPECT_TRUE(SQLValidator::IsValidSql("select 1"));
    EXPECT_TRUE(SQLValidator::IsValidSql("INSERT INTO users (name) VALUES ('tom')"));
    EXPECT_TRUE(SQLValidator::IsValidSql("DELETE FROM logs WHERE id = 1"));
    EXPECT_TRUE(SQLValidator::IsValidSql(
        "SELECT name FROM students_a UNION ALL SELECT name FROM students_b"));
    EXPECT_TRUE(SQLValidator::IsValidSql("SHOW TABLES"));
    EXPECT_TRUE(SQLValidator::IsValidSql("BEGIN"));
    EXPECT_TRUE(SQLValidator::IsValidSql("commit"));
}

// 异常情况: 危险操作, 多条语句, 不支持的语句类型均判定无效
TEST(IsValidSqlTest, ReturnFalseForInvalidStatement)
{
    EXPECT_FALSE(SQLValidator::IsValidSql("DROP TABLE users"));
    EXPECT_FALSE(SQLValidator::IsValidSql("TRUNCATE TABLE logs"));
    EXPECT_FALSE(SQLValidator::IsValidSql("UPDATE users SET name = 'a' WHERE 1=1"));
    EXPECT_FALSE(SQLValidator::IsValidSql("SELECT 1; SELECT 2"));
    EXPECT_FALSE(SQLValidator::IsValidSql("GRANT ALL ON *.* TO 'guest'"));
    EXPECT_FALSE(SQLValidator::IsValidSql(""));
}
