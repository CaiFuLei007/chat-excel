

## 1. 测试数据库驱动层代码

1. 测试 SQLValidator 所有接口 , 能够正确验证 SQL 语句是否符合语法规范
2. 测试 MySQLConfig 和 SQLiteConfig 能够正确校验参数
3. 测试 MySQLDatabaseDriver 所有接口
4. 测试 SQLiteDatabaseDriver 所有接口

要求 : 
1. 每个公共函数都需要进行独立的测试
2. 每个行数都要包含正常测试和异常测试的场景
3. 边界测试 : 包含边界值测试
4. 测试文件存放在 test/svc_database_service/driver 目录下 , 文件名为 类名_test.cc
5. 生成 test_plan.md 文件 ,存放在 test/svc_database_service/driver 目录下 , 包含所有接口对应的测试用例, 以及测试用例中涉及到的 SQL 语句, 对测试用例进行编号

MySQL 配置 : 配置信息从环境变量中获取
	1. 数据库 : MYSQL_CHAT_EXCEL_TEST_DATABASE
	2. 用户名 : MYSQL_CHAT_EXCEL_TEST_USER
	3. 密码 : MYSQL_CHAT_EXCEL_TEST_PASSWORD
	4. 主机地址 : MYSQL_CHAT_EXCEL_TEST_HOST
	5. 端口 : MYSQL_CHAT_EXCEL_TEST_PORT
	6. 字符集 : MYSQL_CHAT_EXCEL_TEST_CHARSET


## 2. DataBusiness 业务层代码纠正

1. DatabaseBusiness::ExecuteModifySqlWithTempTable 在执行 修改类 SQL 语句的时候 , 并不需要对所有相关表都进行备份, 只需要对当前将要修改的表进行备份即可 , DatabaseBusiness::ReplaceTableNames 也需要同步进行修改
