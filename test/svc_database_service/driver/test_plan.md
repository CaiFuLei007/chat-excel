# 数据库驱动层单元测试计划 (test_plan)

## 1. 概述

本文档为 `svc_database_service/driver` 驱动层的单元测试计划, 覆盖 4 个测试目标, 共 120 个测试用例, 全部通过。

| 编号前缀 | 测试文件 | 测试目标 | 用例数 |
|---------|---------|---------|-------|
| SV | sql_validator_test.cc | SQLValidator SQL 校验器 | 30 |
| SC | database_schema_test.cc | MySQLConfig / SQLiteConfig 配置校验 | 9 |
| SQ | sqlite_database_driver_test.cc | SQLiteDatabaseDriver SQLite 驱动 | 40 |
| MY | mysql_database_driver_test.cc | MySQLDatabaseDriver MySQL 驱动 | 41 |

## 2. 测试环境与运行方式

### 2.1 SQLite 测试

- 无外部依赖, 使用 `:memory:` 内存数据库, 用例之间互不影响
- 文件数据库用例使用 `/tmp/chat_excel_sqlite_driver_test.db`, 运行前后自动清理

### 2.2 MySQL 测试

连接真实 MySQL 数据库, 配置从以下 6 个环境变量获取, 任一缺失时相关用例自动跳过 (`GTEST_SKIP`):

| 环境变量 | 含义 |
|---------|------|
| MYSQL_CHAT_EXCEL_TEST_HOST | 主机地址 |
| MYSQL_CHAT_EXCEL_TEST_PORT | 端口 |
| MYSQL_CHAT_EXCEL_TEST_USER | 用户名 |
| MYSQL_CHAT_EXCEL_TEST_PASSWORD | 密码 |
| MYSQL_CHAT_EXCEL_TEST_DATABASE | 数据库名 |
| MYSQL_CHAT_EXCEL_TEST_CHARSET | 字符集 (写入 other_config["charset"]) |

- 测试表 `chat_excel_driver_test_table`: `id INT PRIMARY KEY, name VARCHAR(128), score DOUBLE, is_active TINYINT, remark TEXT`
- 数据准备使用 `CREATE TABLE IF NOT EXISTS` + `REPLACE INTO` 固定主键, 保证幂等可重复运行
- 危险关键字全面禁止, 测试数据不做 DROP/DELETE 清理, 依赖固定主键覆盖

### 2.3 编译与运行

```bash
cmake --build build -j$(nproc)
./build/test/svc_database_service/driver/sql_validator_test
./build/test/svc_database_service/driver/database_schema_test
./build/test/svc_database_service/driver/sqlite_database_driver_test
./build/test/svc_database_service/driver/mysql_database_driver_test
```

驱动实现内部大量记录日志, `sqlite_database_driver_test` 与 `mysql_database_driver_test` 在自定义 `main()` 中初始化日志系统后运行 (未初始化日志会因全局 logger 为空而崩溃)。

## 3. sql_validator_test.cc (SV-001 ~ SV-030)

测试对象: `SQLValidator` 全部公共接口, 纯字符串处理, 无数据库依赖。

### 3.1 GetSqlType (语句类型识别)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-001 | GetSqlTypeTest.ReturnTypeForEachSupportedKeyword | 正常 | SELECT/SHOW/DESC/DESCRIBE/INSERT/UPDATE/DELETE/REPLACE/TRUNCATE/CREATE/DROP/ALTER 各一条 | 每个关键字返回对应 SqlType |
| SV-002 | GetSqlTypeTest.CaseInsensitiveAndIgnoreLeadingWhitespace | 正常 | `select * from users`、`  \t\n Select 1`、`Show Tables`、`insert into users (name) values ('tom')` | 大小写不敏感, 忽略前后空白 |
| SV-003 | GetSqlTypeTest.ReturnUnknownForUnsupportedStatement | 异常 | `GRANT ALL ON *.* TO 'guest'`、`BEGIN`、`COMMIT`、空串、全空白串 | 返回 SqlType::UNKNOWN |

### 3.2 IsReadOnlySql (只读判定)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-004 | IsReadOnlySqlTest.ReturnTrueForReadOnlyStatement | 正常 | `SELECT * FROM users`、`show tables`、`DESC users`、`DESCRIBE users` | 判定为只读 |
| SV-005 | IsReadOnlySqlTest.ReturnFalseForNonReadOnlyStatement | 异常 | `INSERT INTO users (name) VALUES ('tom')`、`UPDATE users SET name = 'tom'`、`CREATE TABLE logs (id INT)`、`BEGIN` | 判定非只读 |

### 3.3 IsModifySql (修改判定)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-006 | IsModifySqlTest.ReturnTrueForModifyStatement | 正常 | INSERT/UPDATE/DELETE/REPLACE/TRUNCATE/CREATE/DROP/ALTER 各一条 | 判定为修改语句 |
| SV-007 | IsModifySqlTest.ReturnFalseForNonModifyStatement | 异常 | `SELECT * FROM users`、`SHOW TABLES`、`DESC users`、`BEGIN`、`COMMIT` | 判定非修改语句 |

### 3.4 IsTransactionStatement (事务语句判定)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-008 | IsTransactionStatementTest.ReturnTrueForTransactionStatement | 正常 | `BEGIN`、`begin work`、`START TRANSACTION`、`COMMIT`、`commit`、`ROLLBACK`、`SAVEPOINT sp1`、`RELEASE SAVEPOINT sp1` | 判定为事务语句, 大小写不敏感 |
| SV-009 | IsTransactionStatementTest.ReturnFalseForNonTransactionStatement | 异常 | `COMMITTED`、`SELECT 1`、`INSERT INTO users (name) VALUES ('BEGIN')`、空串 | 前缀单词与普通语句不误判 |

### 3.5 IsValidTableName (表名校验)

| 编号 | 测试用例 | 场景 | 输入 | 验证点 |
|------|---------|------|------|--------|
| SV-010 | IsValidTableNameTest.AcceptValidTableName | 正常 | `users`、`_tmp_data`、`t1`、`学生表`、`my-table`、`db.orders` | 均为有效表名 |
| SV-011 | IsValidTableNameTest.RejectInvalidTableName | 异常 | 空串、`1table`、`a..b`、`a--b`、`my table`、`tab@le` | 均为无效表名 |
| SV-012 | IsValidTableNameTest.CheckMaxLengthBoundary | 边界 | 长度恰为 kMaxIdentifierLength / kMaxIdentifierLength + 1 的表名 | 边界内有效, 超界无效 |

### 3.6 IsValidColumnName (列名校验)

| 编号 | 测试用例 | 场景 | 输入 | 验证点 |
|------|---------|------|------|--------|
| SV-013 | IsValidColumnNameTest.AcceptValidColumnName | 正常 | `name`、`_count`、`a`、`` `col-name` ``、`'col.name'`、`"123col"`、`` `成绩` `` | 普通与引号包裹列名均有效 |
| SV-014 | IsValidColumnNameTest.RejectInvalidColumnName | 异常 | `col-name`、`col.name`、空串、`1col`、`-` | 未引号包裹的特殊字符列名等无效 |

### 3.7 ContainsDangerousOperation (危险操作检测)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-015 | ContainsDangerousOperationTest.ReturnTrueForDangerousKeyword | 正常 | `DROP DATABASE shop`、`drop table users`、`TRUNCATE TABLE logs`、`DELETE FROM logs`、`EXEC sp_help`、`UNION ALL SELECT ...`、`OR 1=1`、`SLEEP(10)`、`BENCHMARK(...)`、`LOAD_FILE(...)`、`INTO OUTFILE` | 常见注入特征全部命中, 大小写不敏感 |
| SV-016 | ContainsDangerousOperationTest.MatchKeywordAfterWhitespaceCollapse | 正常 | `SELECT * FROM users WHERE id = 1 OR  '1'='1'` (多空白) | 空白合并后仍命中 |
| SV-017 | ContainsDangerousOperationTest.IgnoreKeywordInsideComment | 正常 | `SELECT 1 // DROP TABLE users`、`SELECT /* DELETE FROM logs */ 1` | 注释内关键字不参与匹配 |
| SV-018 | ContainsDangerousOperationTest.ReturnFalseForSafeStatement | 正常 | `SELECT name, age FROM users WHERE id = 42`、`INSERT INTO students (name) VALUES ('小明')`、`SHOW TABLES`、空串 | 安全语句不误报 |

### 3.8 ContainsMultipleStatements (多条语句检测)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-019 | ContainsMultipleStatementsTest.AllowTrailingSemicolon | 边界 | `SELECT 1;`、`select * from users;`、`SELECT 1` | 末尾分号不判定为多条语句 |
| SV-020 | ContainsMultipleStatementsTest.ReturnTrueForMultipleStatements | 异常 | `SELECT 1; SELECT 2`、`SELECT 1; SELECT 2;`、`SELECT 1; ;` | 语句中间分号与连续分号均命中 |
| SV-021 | ContainsMultipleStatementsTest.IgnoreSemicolonInsideStringAndComment | 正常 | `SELECT * FROM users WHERE name = 'a;b'`、``SELECT * FROM `order;detail` ``、`SELECT 1 /* ; */`、`SELECT 1 // 注释; 不会结束语句` | 字符串与注释内的分号不参与判定 |

### 3.9 ExtractTableNames (表名提取)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-022 | ExtractTableNamesTest.ExtractTableNamesForCommonStatement | 正常 | `SELECT * FROM users`、`SELECT * FROM orders o JOIN users u ON o.id = u.user_id`、`INSERT INTO students (name) VALUES ('tom')`、`UPDATE users SET ...`、`DELETE FROM logs WHERE id < 10`、`CREATE TABLE IF NOT EXISTS student_score (id INT)`、`DROP TABLE IF EXISTS temp_data`、`REPLACE INTO dict VALUES (1)` | 各类语句正确提取表名 |
| SV-023 | ExtractTableNamesTest.ExtractMultipleTablesWithCommaAndAlias | 正常 | `SELECT * FROM t1 AS a, t2`、`SELECT name FROM db.orders` | 逗号多表/AS 别名/库名.表名均支持 |
| SV-024 | ExtractTableNamesTest.DeduplicateAndIgnoreStringLiteral | 正常 | `SELECT * FROM users JOIN users ON users.id = 1`、`SELECT * FROM t WHERE name = 'users'` | 重复表名去重, 字符串字面量不参与 |
| SV-025 | ExtractTableNamesTest.ReturnEmptyListWhenNoTableName | 异常 | `SELECT 1`、`SHOW DATABASES`、空串 | 返回空列表 |

### 3.10 TrimSql / RemoveComments / NormalizeSql (语句规范化)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-026 | TrimSqlTest.TrimWhitespaceOnBothSides | 正常/边界 | `  SELECT 1  `、`\t\n select \r\n`、全空白、空串 | 去除前后空白, 全空白返回空串 |
| SV-027 | RemoveCommentsTest.RemoveLineAndBlockComment | 正常 | `SELECT 1 // comment`、`SELECT /* hint */ 1`、`SELECT '// not comment' FROM t`、空串 | 单行/多行注释移除, 字符串内注释符保留 |
| SV-028 | NormalizeSqlTest.CombineRemoveCommentAndTrim | 正常 | `  SELECT 1  `、`// 说明\nSELECT 1`、`SELECT /* c */ 1` | 组合移除注释并去除空白 |

### 3.11 IsValidSql (综合校验)

| 编号 | 测试用例 | 场景 | 输入 SQL | 验证点 |
|------|---------|------|---------|--------|
| SV-029 | IsValidSqlTest.ReturnTrueForValidStatement | 正常 | `SELECT * FROM users WHERE id = 1`、`select 1`、`INSERT INTO users (name) VALUES ('tom')`、`SHOW TABLES`、`BEGIN`、`commit` | 合法只读/写入/事务语句均有效 |
| SV-030 | IsValidSqlTest.ReturnFalseForInvalidStatement | 异常 | `DROP TABLE users`、`TRUNCATE TABLE logs`、`DELETE FROM logs WHERE id = 1`、`UPDATE users SET name = 'a' WHERE 1=1`、`SELECT 1; SELECT 2`、`GRANT ALL ON *.* TO 'guest'`、空串 | 危险/多条/不支持类型/空语句均无效 |

## 4. database_schema_test.cc (SC-001 ~ SC-009)

测试对象: `MySQLConfig::CheckConfig`、`SQLiteConfig::CheckConfig`、`GetDatabaseType`。

### 4.1 MySQLConfig

| 编号 | 测试用例 | 场景 | 输入 | 验证点 |
|------|---------|------|------|--------|
| SC-001 | MySQLConfigTest.AcceptValidConfig | 正常 | host=127.0.0.1, port=3306, user=root, password=password, database=chat_excel_test | 校验通过 |
| SC-002 | MySQLConfigTest.AcceptEmptyPassword | 边界 | password 为空 | 密码非必填, 校验通过 |
| SC-003 | MySQLConfigTest.RejectEmptyRequiredField | 异常 | 分别置空 host / user_name / database_name | 三种缺失均校验失败 |
| SC-004 | MySQLConfigTest.AcceptPortBoundaryValue | 边界 | port=1 / port=65535 | 边界值校验通过 |
| SC-005 | MySQLConfigTest.RejectPortOutOfBoundary | 边界 | port=0 / port=65536 / port=-1 | 超界与负值校验失败 |
| SC-006 | MySQLConfigTest.ReturnMySQLDatabaseType | 正常 | 合法配置 | GetDatabaseType 返回 MYSQL |

### 4.2 SQLiteConfig

| 编号 | 测试用例 | 场景 | 输入 | 验证点 |
|------|---------|------|------|--------|
| SC-007 | SQLiteConfigTest.AcceptValidConfig | 正常 | database_file_path=/tmp/chat_excel_test.db | 校验通过 |
| SC-008 | SQLiteConfigTest.RejectEmptyDatabaseFilePath | 异常 | database_file_path 为空 | 校验失败 |
| SC-009 | SQLiteConfigTest.ReturnSQLiteDatabaseType | 正常 | 合法配置 | GetDatabaseType 返回 SQLITE |

## 5. sqlite_database_driver_test.cc (SQ-001 ~ SQ-040)

测试对象: `SQLiteDatabaseDriver` 全部公共接口。使用内存数据库 `:memory:` (夹具) 或临时文件数据库。

测试表结构: `CREATE TABLE IF NOT EXISTS driver_test_table (id INTEGER PRIMARY KEY, name TEXT, score REAL, is_active INTEGER)`

### 5.1 构造 (SQLiteDatabaseDriver)

| 编号 | 测试用例 | 场景 | 验证点 |
|------|---------|------|--------|
| SQ-001 | ConstructorTest.RejectNullConfig | 异常 | 空配置构造抛 DB_CONFIG_INVALID |
| SQ-002 | ConstructorTest.RejectMismatchedConfigType | 异常 | 传入 MySQLConfig 构造抛 DB_CONFIG_INVALID |

### 5.2 Connect / TestConnection / Disconnect (连接管理)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| SQ-003 | TestConnectionSucceedAfterConnect | 正常 | 无 | 连接内存库后 TestConnection 为 true |
| SQ-004 | FileTest.ConnectFileDatabase | 正常 | 无 | 连接 /tmp 文件库成功, 断开后清理文件 |
| SQ-005 | FileTest.ConnectRejectInvalidConfig | 异常 | 无 | 文件路径为空时 Connect 抛 DB_CONFIG_INVALID |
| SQ-006 | FileTest.ConnectRejectUnavailableFilePath | 异常 | 无 | 目录不存在 (/nonexistent_dir_xyz/) 时 Connect 抛 DB_CONNECTION_FAILED |
| SQ-007 | PragmaTest.ApplyPragmaConfiguration | 正常 | `CREATE TABLE pragma_parent (id INTEGER PRIMARY KEY)`、`CREATE TABLE pragma_child (parent_id INTEGER, FOREIGN KEY(parent_id) REFERENCES pragma_parent(id))`、`INSERT INTO pragma_child (parent_id) VALUES (999)` | foreign_keys=ON 生效, 孤儿数据插入失败 |
| SQ-008 | PragmaTest.SkipPragmaWhenNotConfigured | 正常 | 同 SQ-007 | 未配置 PRAGMA 时同样插入成功 (默认关闭外键) |
| SQ-009 | PragmaTest.TolerateInvalidPragma | 异常 | 无 (配置 invalid_pragma_key) | 无效 PRAGMA 仅告警, 连接仍建立 |
| SQ-010 | TestConnectionFailAfterDisconnect | 正常 | 无 | Disconnect 后 TestConnection 为 false |
| SQ-011 | DisconnectTest.DisconnectWithoutConnectIsNoOp | 边界 | 无 | 未连接直接 Disconnect 为空操作 |
| SQ-012 | DisconnectTest.BeginTransactionWithoutConnectThrow | 异常 | 无 | 未连接 BeginTransaction 抛 DB_NOT_CONNECTED |

### 5.3 QuoteIdentifier (标识符转义)

| 编号 | 测试用例 | 场景 | 输入 → 输出 | 验证点 |
|------|---------|------|------------|--------|
| SQ-013 | QuoteAndEscapeIdentifier | 正常 | `users` → `"users"`、`us"ers` → `"us""ers"`、空串 → `""` | 双引号包裹, 内部双引号双写转义 |

### 5.4 ExecuteQuery (查询通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| SQ-014 | QueryRowsAsString | 正常 | `INSERT INTO driver_test_table (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)`、`SELECT id, name, score, is_active FROM driver_test_table` | 列数/列名/行数正确, 各类型以字符串呈现 (`"1"`/`"张三"`/`"95.5"`/`"1"`) |
| SQ-015 | QueryEmptyTableReturnNoRows | 边界 | `SELECT id, name FROM driver_test_table` (空表) | 返回 0 行 |
| SQ-016 | QueryWithoutConnectThrow | 异常 | `SELECT 1` (断开后) | 抛 DB_NOT_CONNECTED |
| SQ-017 | QueryInvalidSqlReturnFailedResult | 异常 | `SELECT * FROM table_not_exists` | 返回失败结果且错误信息非空 |
| SQ-018 | QueryPathRejectModifySql | 异常 | `INSERT INTO driver_test_table VALUES (1, 'a', 1.0, 1)` | 查询通路拒绝修改语句, 抛 DB_SQL_INVALID |
| SQ-019 | QueryEmptySqlThrow | 异常 | `"   "` | 抛 DB_SQL_EMPTY |
| SQ-020 | QueryDangerousSqlThrow | 异常 | `DROP TABLE driver_test_table` | 抛 DB_SQL_DANGEROUS |
| SQ-021 | QueryMultipleStatementsThrow | 异常 | `SELECT 1; SELECT 2` | 抛 DB_SQL_MULTIPLE_STATEMENTS |

### 5.5 ExecuteUpdate (修改通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| SQ-022 | CreateTable | 正常 | `CREATE TABLE IF NOT EXISTS driver_test_table (...)` | 成功且影响行数为 0 |
| SQ-023 | InsertRowsReturnAffectedRows | 正常 | `INSERT INTO driver_test_table (id, name, score, is_active) VALUES (1, '张三', 95.5, 1)`、`VALUES (2, '李四', 80.0, 0)` | 两次影响行数均为 1 |
| SQ-024 | UpdateRowsReturnAffectedRows | 正常 | `UPDATE driver_test_table SET score = 60.0 WHERE is_active = 1`、`UPDATE driver_test_table SET score = 60.0 WHERE id = 999` | 匹配 2 行影响行数为 2, 无匹配影响行数为 0 |
| SQ-025 | UpdateWithoutConnectThrow | 异常 | `CREATE TABLE t (id INTEGER)` (断开后) | 抛 DB_NOT_CONNECTED |
| SQ-026 | UpdatePathRejectReadOnlySql | 异常 | `SELECT 1` | 修改通路拒绝只读语句, 抛 DB_SQL_INVALID |
| SQ-027 | UpdateDangerousSqlThrow | 异常 | `TRUNCATE TABLE driver_test_table` | 抛 DB_SQL_DANGEROUS |
| SQ-028 | UpdateConstraintViolationReturnFailedResult | 异常 | `INSERT INTO driver_test_table (id, ...) VALUES (1, '张三', ...)` 后重复插入 id=1 | 主键冲突返回失败结果, 错误信息非空 |

### 5.6 ExecutePreparedQuery / ExecutePreparedUpdate (预编译通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| SQ-029 | PreparedQueryWithIntAndBoolParameter | 正常 | `SELECT name FROM driver_test_table WHERE id = ? AND is_active = ?` | 整数与布尔绑定正确, 返回匹配行 "张三" |
| SQ-030 | PreparedQueryWithDoubleParameter | 正常 | `SELECT id FROM driver_test_table WHERE score = ?` (95.5) | 浮点精确匹配, 返回 "1" |
| SQ-031 | PreparedQueryWithNullParameter | 正常 | `INSERT INTO driver_test_table (id, name, score, is_active) VALUES (?, ?, ?, ?)` (name=NULL)、`SELECT name, score FROM driver_test_table WHERE id = 1` | NULL 参数绑定成功, 读出为 "NULL" 字符串, score 为 "88.5" |
| SQ-032 | PreparedQueryWithSpecialCharacterParameter | 正常 | `INSERT INTO driver_test_table (id, name) VALUES (?, ?)` (值 `张三's "note";--注释`)、`SELECT id FROM driver_test_table WHERE name = ?` | 特殊字符写入与参数化匹配均正确, 无注入 |
| SQ-033 | PreparedQueryWithLongStringParameter | 边界 | `INSERT INTO driver_test_table (id, name) VALUES (?, ?)` (10000 字符) | 超长数据完整写入读出 (溢出页存储路径) |
| SQ-034 | PreparedQueryParameterCountMismatchThrow | 异常 | `SELECT name FROM driver_test_table WHERE id = ?` 绑定 2 个参数 (先建表) | 抛 DB_PARAM_BIND_FAILED |
| SQ-035 | PreparedQueryWithoutConnectThrow | 异常 | `SELECT name FROM t WHERE id = ?` (断开后) | 抛 DB_NOT_CONNECTED |
| SQ-036 | PreparedInsertAndUpdateWithParameters | 正常 | `INSERT INTO driver_test_table (id, name, score, is_active) VALUES (?, ?, ?, ?)`、`UPDATE driver_test_table SET score = ? WHERE id = ?` | 预编译插入/更新成功, 影响行数均为 1 |
| SQ-037 | PreparedInsertWithNullPointerParameter | 正常 | `INSERT INTO driver_test_table (id, name) VALUES (?, ?)` (const char* nullptr) | 空指针绑定为 NULL, 读出 "NULL" |
| SQ-038 | PreparedUpdateParameterCountMismatchThrow | 异常 | `INSERT INTO driver_test_table (id) VALUES (?)` 绑定 0 个参数 (先建表保证预编译成功) | 抛 DB_PARAM_BIND_FAILED (校验发生在绑定阶段) |

### 5.7 事务 (BeginTransaction / CommitTransaction / RollbackTransaction)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| SQ-039 | CommitTransaction | 正常 | BEGIN → `INSERT INTO driver_test_table (id, name) VALUES (1, '张三')` → COMMIT → `SELECT id, name FROM driver_test_table` | 提交后变更生效, 查到 1 行 |
| SQ-040 | RollbackTransaction | 正常 | 预插入 id=1 → BEGIN → `INSERT INTO driver_test_table (id, name) VALUES (2, '李四')` → ROLLBACK → `SELECT id, name FROM driver_test_table` | 回滚后未提交数据被撤销, 仅剩 1 行 |

## 6. mysql_database_driver_test.cc (MY-001 ~ MY-041)

测试对象: `MySQLDatabaseDriver` 全部公共接口。夹具连接真实 MySQL (环境变量配置) 并准备幂等数据 (id=1/2)。

测试表结构: `CREATE TABLE IF NOT EXISTS chat_excel_driver_test_table (id INT PRIMARY KEY, name VARCHAR(128), score DOUBLE, is_active TINYINT, remark TEXT)`

数据准备: `REPLACE INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (1, 'name-1', 90.5, 1, 'remark-1')`、`VALUES (2, 'name-2', 80.25, 0, NULL)`

### 6.1 构造 (MySQLDatabaseDriver)

| 编号 | 测试用例 | 场景 | 验证点 |
|------|---------|------|--------|
| MY-001 | ConstructorTest.RejectNullConfig | 异常 | 空配置构造抛 DB_CONFIG_INVALID |
| MY-002 | ConstructorTest.RejectMismatchedConfigType | 异常 | 传入 SQLiteConfig 构造抛 DB_CONFIG_INVALID |

### 6.2 QuoteIdentifier (标识符转义)

| 编号 | 测试用例 | 场景 | 输入 → 输出 | 验证点 |
|------|---------|------|------------|--------|
| MY-003 | QuoteIdentifierTest.WrapWithBackticks | 正常 | `users` → `` `users` ``、空串 → ` `` ` | 反引号包裹 |
| MY-004 | QuoteIdentifierTest.EscapeInnerBackticks | 正常 | `us\`ers` → `` `us``ers` `` | 内部反引号双写转义 |

### 6.3 Connect / TestConnection / Disconnect (连接管理)

| 编号 | 测试用例 | 场景 | 验证点 |
|------|---------|------|--------|
| MY-005 | ConnectTest.ConnectWithEnvironmentConfig | 正常 | 环境变量配置连接成功, TestConnection 为 true, Disconnect 后为 false (无环境变量则跳过) |
| MY-006 | ConnectTest.RejectInvalidConfig | 异常 | 主机名为空时 Connect 抛 DB_CONFIG_INVALID |
| MY-007 | ConnectTest.RejectUnreachableHost | 异常 | 127.0.0.1:1 不可达时 Connect 抛 DB_CONNECTION_FAILED |
| MY-008 | DisconnectTest.DisconnectWithoutConnectIsNoOp | 边界 | 未连接直接 Disconnect 为空操作 |
| MY-009 | DisconnectTest.BeginTransactionWithoutConnectThrow | 异常 | 未连接 BeginTransaction 抛 DB_NOT_CONNECTED |
| MY-010 | TestConnectionSucceedAfterConnect | 正常 | 连接后心跳检测为 true |
| MY-011 | TestConnectionFailAfterDisconnect | 正常 | 断开后心跳检测为 false |

### 6.4 ExecuteQuery (查询通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| MY-012 | QueryRowsAsString | 正常 | `SELECT id, name, score, is_active, remark FROM chat_excel_driver_test_table WHERE id = 1` | 5 列列名/行数正确, 各类型以字符串呈现 (`"1"`/`"name-1"`/`"90.5"`/`"1"`/`"remark-1"`) |
| MY-013 | QueryShowTables | 正常 | `SHOW TABLES LIKE 'chat_excel_driver_test_table'` | 返回 1 行匹配表 |
| MY-014 | QueryDescribeTable | 正常 | `DESC chat_excel_driver_test_table` | 返回 5 行 (每列一行), 首列名为 Field |
| MY-015 | QueryEmptyResult | 边界 | `SELECT id, name FROM chat_excel_driver_test_table WHERE id = 999999` | 返回 0 行 |
| MY-016 | QueryWithoutConnectThrow | 异常 | `SELECT 1` (断开后) | 抛 DB_NOT_CONNECTED |
| MY-017 | QueryInvalidSqlReturnFailedResult | 异常 | `SELECT * FROM table_not_exists` | 返回失败结果且错误信息非空 |
| MY-018 | QueryPathRejectModifySql | 异常 | `INSERT INTO chat_excel_driver_test_table (id) VALUES (1)` | 查询通路拒绝修改语句, 抛 DB_SQL_INVALID |
| MY-019 | QueryEmptySqlThrow | 异常 | `"   "` | 抛 DB_SQL_EMPTY |
| MY-020 | QueryDangerousSqlThrow | 异常 | `DROP TABLE chat_excel_driver_test_table` | 抛 DB_SQL_DANGEROUS |
| MY-021 | QueryMultipleStatementsThrow | 异常 | `SELECT 1; SELECT 2` | 抛 DB_SQL_MULTIPLE_STATEMENTS |

### 6.5 ExecuteUpdate (修改通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| MY-022 | UpdateCreateTable | 正常 | `CREATE TABLE IF NOT EXISTS chat_excel_driver_test_table (...)` (表已存在) | 成功且影响行数为 0 |
| MY-023 | UpdateReplaceRowReturnAffectedRows | 正常 | `REPLACE INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (1, 'name-1', 90.5, 1, 'remark-1')` | 成功且影响行数 ≥ 1 (首次插入为 1, 覆盖更新为 2, 不断言精确值) |
| MY-024 | UpdateRowsReturnAffectedRows | 正常 | `UPDATE chat_excel_driver_test_table SET name = 'name-1-updated', score = 91.5 WHERE id = 1`、`SELECT name, score FROM ... WHERE id = 1`、`UPDATE ... SET name = 'no-match' WHERE id = 999999` | 匹配影响行数为 1, 更新值可查 (`"name-1-updated"`/`"91.5"`), 无匹配影响行数为 0 |
| MY-025 | UpdateWithoutConnectThrow | 异常 | `CREATE TABLE some_table (id INT)` (断开后) | 抛 DB_NOT_CONNECTED |
| MY-026 | UpdatePathRejectReadOnlySql | 异常 | `SELECT 1` | 修改通路拒绝只读语句, 抛 DB_SQL_INVALID |
| MY-027 | UpdateDangerousSqlThrow | 异常 | `TRUNCATE TABLE chat_excel_driver_test_table` | 抛 DB_SQL_DANGEROUS |
| MY-028 | UpdateConstraintViolationReturnFailedResult | 异常 | `INSERT INTO chat_excel_driver_test_table (id, name, score, is_active) VALUES (1, 'duplicate', 70.0, 1)` (主键已存在) | 主键冲突返回失败结果, 错误信息非空 |

### 6.6 ExecutePreparedQuery / ExecutePreparedUpdate (预编译通路)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| MY-029 | PreparedQueryWithIntAndBoolParameter | 正常 | `SELECT name FROM chat_excel_driver_test_table WHERE id = ? AND is_active = ?` (1, true) | 返回 "name-1" |
| MY-030 | PreparedQueryWithDoubleParameter | 正常 | `SELECT name FROM chat_excel_driver_test_table WHERE id = ? AND score = ?` (2, 80.25) | 浮点精确匹配, 返回 "name-2" |
| MY-031 | PreparedQueryWithStringParameter | 正常 | `SELECT id, score FROM chat_excel_driver_test_table WHERE name = ?` ('name-2') | 返回 "2" / "80.25" |
| MY-032 | PreparedQueryWithNullParameter | 正常 | `INSERT INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (?, ?, ?, ?, ?)` (id=3, name/remark=NULL)、`SELECT name, score, remark FROM ... WHERE id = 3` | NULL 绑定成功, 读出 "NULL"/"88.5"/"NULL" |
| MY-033 | PreparedQueryWithSpecialCharacterParameter | 正常 | `INSERT INTO chat_excel_driver_test_table (id, name) VALUES (?, ?)` (id=4, 值 `张三's "note";--注释`)、`SELECT id FROM ... WHERE name = ?` | 中文/引号/注释符写入与匹配正确, 返回 "4" |
| MY-034 | PreparedQueryWithLongStringParameter | 边界 | `INSERT INTO chat_excel_driver_test_table (id, remark) VALUES (?, ?)` (id=5, 10000 字符写入 TEXT 列)、`SELECT remark FROM ... WHERE id = 5` | 超长数据完整读出 (验证 mysql 截断重取路径) |
| MY-035 | PreparedQueryParameterCountMismatchThrow | 异常 | `SELECT name FROM chat_excel_driver_test_table WHERE id = ?` 绑定 2 个参数 | 抛 DB_PARAM_BIND_FAILED |
| MY-036 | PreparedQueryWithoutConnectThrow | 异常 | `SELECT name FROM some_table WHERE id = ?` (断开后) | 抛 DB_NOT_CONNECTED |
| MY-037 | PreparedInsertAndUpdateWithParameters | 正常 | `REPLACE INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (?, ?, ?, ?, ?)` (id=6)、`UPDATE chat_excel_driver_test_table SET score = ? WHERE id = ?` (71.5)、`SELECT name, score FROM ... WHERE id = 6` | 插入/更新成功, 影响行数为 1, 查得 "name-6"/"71.5" |
| MY-038 | PreparedInsertWithNullPointerParameter | 正常 | `INSERT INTO chat_excel_driver_test_table (id, name) VALUES (?, ?)` (id=7, const char* nullptr)、`SELECT name FROM ... WHERE id = 7` | 空指针绑定为 NULL, 读出 "NULL" |
| MY-039 | PreparedUpdateParameterCountMismatchThrow | 异常 | `INSERT INTO chat_excel_driver_test_table (id) VALUES (?)` 绑定 0 个参数 | 抛 DB_PARAM_BIND_FAILED |

### 6.7 事务 (BeginTransaction / CommitTransaction / RollbackTransaction)

| 编号 | 测试用例 | 场景 | 涉及 SQL | 验证点 |
|------|---------|------|---------|--------|
| MY-040 | CommitTransaction | 正常 | BEGIN → `REPLACE INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (8, 'name-8', 60.5, 1, NULL)` → COMMIT → `SELECT name FROM ... WHERE id = 8` | 提交后查得 "name-8" |
| MY-041 | RollbackTransaction | 正常 | BEGIN → `REPLACE INTO chat_excel_driver_test_table (id, name, score, is_active, remark) VALUES (9, 'name-9', 50.5, 1, NULL)` → ROLLBACK → `SELECT id FROM ... WHERE id = 9` | 回滚后查不到 id=9, 返回 0 行 |

## 7. 测试结果

| 测试目标 | 用例数 | 通过 | 失败 | 跳过 |
|---------|-------|------|------|------|
| sql_validator_test | 30 | 30 | 0 | 0 |
| database_schema_test | 9 | 9 | 0 | 0 |
| sqlite_database_driver_test | 40 | 40 | 0 | 0 |
| mysql_database_driver_test | 41 | 41 | 0 | 0 |
| **合计** | **120** | **120** | **0** | **0** |

说明: mysql_database_driver_test 在未配置 6 个 `MYSQL_CHAT_EXCEL_TEST_*` 环境变量的环境中运行时, 依赖真实连接的用例 (夹具用例与 MY-005) 将自动跳过; 其余纯配置/字符串逻辑用例始终执行。
