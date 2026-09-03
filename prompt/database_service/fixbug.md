

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
2. 在对数据库进行备份的时候 , 只对当前将要修改的表进行备份即可 , 不需要对其他表进行备份 , 检查代码逻辑 , 将原来对所有表的备份逻辑 , 改为只对当前将要修改的表进行备份 , 对不需要的接口或函数进行删除
2. 在 DatabaseDriver 驱动层中添加接口 : MySQLDatabaseDriver 和 SQLiteDatabaseDriver 中分别进行实现 , 根据不同数据库类型 , 进行具体实现
	1. 获取表结构 GetTableStructure , DatabaseBusiness::GetTableStructure 直接调用驱动层的接口即可
	2. 获取所有表名 GetAllTablesName , DatabaseBusiness::ListAllTables 直接调用驱动层的接口即可
	3. Excel 解析类型转化为数据库列类型 , DatabaseBusiness::CreateImportTable 在创建新的表结构的时候 , 需要根据 Excel 中的列类型 , 转化为对应的数据库列类型 . 具体转化 :  将 BOOLEAN 类型转化为 INT 类型 , 数据行中对应的 BOOLEAN 类型字段需要进行转换 , 转化为 0 和 1 , DATE 类型转化为 TEXT 类型
3. DatabaseBusiness::CreateImportTable , 插入行数据的时候 , 如果 Excel 中的列类型是 BOOLEAN 类型 , 则需要将数据行中对应的 BOOLEAN 类型字段进行转换 , 转化为 0 和 1 , DATE 类型转化为 TEXT 类型
4. 检查 SQLite 数据库在执行修改类 SQL 语句的时候 , 是否进行了备份 , 如果没有 , 则需要添加备份逻辑


## 3. 完善对 SQLValidator::ContainsDangerousOperation 的判断
1. 在进行判断的时候 , 存在误判的情况, 如果输入时 SELECT description FROM products 就会命中 SCRIPT .
2. 因此修改判断逻辑 , 在确保命中的是关键字,而不是表名称或字段名.
