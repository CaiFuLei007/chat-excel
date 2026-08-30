
**系统身份** : 你是一位资深的 C++ 工程师 , 擅长使用 MySQL C API 操作 MySQL 数据库 , 擅长使用 SQLite C API 操作 SQLite 数据库.

**任务** : 实现数据库驱动层 , 负责数据库连接, 断开 , 以及执行查询修改类 SQL 语句 ,事务等操作 , 支持 MySQL 和 SQLite 数据库.

## 1. 数据库驱动层介绍

数据库驱动层为数据库子服务提供所有数据库操作支持 , 目前支持 MySQL 和 SQLite 数据库. 包含一下模块 : 
1. SQL 校验器 : 负责规范化 SQL 语句 , 验证 SQL 语句的合法性 , 防止 SQL 注入 , 检测 SQL 类型(只读或修改).
2. 基础结构定义 : 提供基础的数据结构定义 , 包含数据库配置 , 列信息 , 表结构 , 参数绑定 , 查询结构集处理等.
3. 数据库驱动接口 : 采用策略模型 , 定义数据库驱动接口 , 包含连接数据库 , 断开数据库 , 执行 SQL 语句等
4. 数据库驱动实现 : 实现具体的数据库驱动 , 支持 MySQL 和 SQLite 数据库.
5. 数据库驱动工厂 : 使用工厂模型创建数据库驱动实例

## 2. SQL 校验器

chat-excel 项目中 , SQL 语句一般来源于 AI 子服务 , 因此数据库子服务在进行 SQL 操作之前 , 需要校验 SQL 语句语法的合法性 , 安全性(防止 SQL 注入) , 确保 SQL 语句安全后才能执行.
SQL 校验器需要提供的操作 : 
1. 获取 SQL 语句类型 
    1. 分为两种类型 : 只读类型 , 修改类型.
2. 是否属于只读类 SQL 语句
3. 是否属于修改类 SQL 语句
4. 是否是有效的表名
    1. 不为空
    2. 只能包含 : 数字(不能开头) , 字母 , 下划线 , 汉字 , 连接符(-) , 点(.)(不能多个连接符 , 点连续使用)
    3. 不包含单双引号 , 空白字符 , * , / , \ 等特殊字符
    4. 不能超过数据库最大长度限制
5. 是否为有效列名
    1. 不为空
    2. 只能包含 : 数字(不能开头) , 字母 , 下划线 , 汉字 , 连接符(-) , 点(.)(不能多个连接符 , 点连续使用)
    3. 特殊字符需要使用引号包裹
    4. 不能超过数据库最大长度限制
6. 是否包含危险操作
    1. 根据危险字段列表确定是否属于危险操作
7. 是否包含多条 SQL 语句
    1. 通过 ; 个数来确定是否包含多条 SQL 语句
    2. 考虑 ; 是否在单引号和双引号 , 即字符串中
8. 从 SQL 语句中获取要操作的 表名称 
    1. 要考虑各种 CURD 的场景
9. 删除 SQL 语句前后空白
10. 移除 SQL 语句中的注释 
    1. 分为两种注释 : 单行注释 // , 多行注释 /* */
    2. 判断注释内容的时候要考虑, 单引号和双引号的嵌套情况 , 即单引号中包含 // 或 /* 的情况
        1. 比如 'INSERT INTO stu(name, age) VALUES ('张//三', 12);'
        2. 比如 'INSERT INTO stu(name, age) VALUES ('张//三', 12);'
    3. 在确定 注释内容的时候 , 还需要考虑是否属于转义字符 , 即 // 或 /* 前面有 \ 的情况
11. 规范化 SQL 语句
    1. 移除 SQL 语句中的注释
    2. 删除 SQL 语句前后空白
12. 是否属于有效的 SQL 语句
    1. 为支持的 SQL 类型
    2. 不能包含危险操作
    3. 不能包含多条 SQL 语句

支持的 SQL 枚举类型 :
1. SELECT：查询语句, 用于从数据库中检索数据
2. SHOW：显示数据库, 表、索引等元数据
3. DESC：显示表结构 ,索引等元数据
4. INSERT：插入语句 , 用于将数据插入到数据库中
5. UPDATE：更新语句 , 用于更新数据库中数据
6. DELETE：删除语句 , 用于从数据库中删除数据
7. REPLACE：替换语句 , 用于替换数据库中数据
8. TRUNCATE：截空语句 , 用于截空数据库表
9. CREATE：创建语句 , 用于创建数据库、表、索引等
10. DROP：删除语句 , 用于删除数据库、表、索引等
11. ALTER：修改语句 , 用于修改数据库、表、索引等
12. UNKNOWN：未知语句 , 用于处理不支持的SQL语句

危险关键字列表:
1. "DROP DATABASE"：防止删除整个数据库
2. "DROP TABLE"：防止删除整个表
3. "TRUNCATE"：清空表数据
4. "DELETE FROM"：删除表数据
5. "EXEC"：执行系统命令
6. "EXECUTE"：执行系统命令
7. "SCRIPT"：执行脚本
8. "JAVASCRIPT"：执行JavaScript代码
9. "EXECUTE"：执行系统命令
10. "EVAL"：执行表达式
11. "UNION ALL SELECT"：常见SQL注入模式
12. "1=1"：常见SQL注入模式，比如攻击者输入：
13. "OR 1=1"：SELECT * FROM users WHERE username = '' OR '1'='1'
14. "OR '1'='1'"：SELECT * FROM users WHERE username = '' OR '1'='1'，会返回所有用户
15. "SLEEP("：导致数据库响应延迟
16. "BENCHMARK("：MySQL的性能测试函数，消耗CPU资源
17. "LOAD_FILE("：读取文件内容
18. "INTO OUTFILE"：将数据写入文件
19. "INTO DUMPFILE"：将数据写入文件

## 3. 基础结构定义

### 3.1 数据库类型枚举

定义支持的数据库类型枚举:
目前支持 MySQL 和 SQLite 数据库.

### 3.2 数据库配置

1. 定义基类 : DatabaseConfig
    1. 获取数据库类型 , 纯虚函数
    2. 检查数据库配置是否合法 , 纯虚函数
2. 继承基类 , 实现 MySQL 和 SQLite 数据库的配置类.
3. MySQLConfig :
    1. 成员 : 主机名 , 端口号 , 数据库用户名 , 数据库密码 , 数据库名称 , 是否使用 SSL 连接 , SSL 配置(证书路径 , 密钥路径 , CA 证书路径) , 其他配置(unordered_map 进行组织)
4. SQLiteConfig :
    1. 成员 : 数据库文件路径 , 其他配置(unordered_map 进行组织)

### 3.3 列信息结构

1. 成员 : 
    1. 类名 , 列类型
    2. 是否为空 , 是否为主键 , 是否自增
    3. 默认值 , 最大长度(字符串类型使用)

### 3.4 表信息结构

1. 成员 : 
    1. 表名 , 列信息集合 , 主键集合 , 索引集合 , 表注释

### 3.5 SQL 操作结果结构

QueryResult类 : 用于存储查询类 SQL 语句的结果
1. 成员 : 
    1. 列名集合 , 列类型结合 , 行数据结合, 影响行数 , SQL 操作是否成功 , 错误信息
2. 方法 : 
    1. 获取总列数
    2. 获取总行数
    3. 获取查询结果中指定行数据
    4. 获取操作是否成功
    5. 获取错误信息
    6. 将查询结果转换为 JSON 格式 , 格式如下 :

```json
{
  "success": true,
  "error": "",
  "affected_rows": 10,
  "columns": [
    {"name":"id","type":"int"},
    {"name":"username","type":"varchar"}
  ],
  "rows": [
    [1,"admin"],
    [2,"user"]
  ]
}
```

### 3.6 参数包装器

如果使用预编译语句执行 SQL 语句 , 在 SQL 语句预编译之后 , 需要将参数绑定到 SQL 语句中 , 比如 : 
```sql
insert into student (name, age, score) values (?, ?, ?)
```
在执行 SQL 语句之前 , 需要将姓名 , 年龄 , 成绩绑定到 SQL 语句中.为了一次性提供多个参数 , 需要将不同类型参数包装起来 , 使用统一类型提供. 这就是参数包装器.

参数类型统一转化为以下 5 种类型 ,使用 enum 枚举类型表示 : 
1. Null
2. Int
3. Double
4. String
5. Bool

 
ParameterWrapper 类 : 
1. 成员 : 
    1. 参数类型 : 根据用户实际绑定的值 , 自动判断参数类型
    2. 参数值 Variant : 存储用户绑定的值
2. 接口 :
    1. 支持各种参数类型的构造
    2. 获取实际参数类型
    3. 检测是否为 NULL 值
    4. 获取参数值

## 4. 数据库驱动接口

定义数据库操作抽象接口 :
1. 连接数据库
2. 断开数据库
3. 测试连接(心跳检测)
4. 执行查询类 SQL 语句
5. 执行修改类 SQL 语句 
6. 执行预编译查询类 SQL 语句
7. 执行预编译修改类 SQL 语句 
8. 开始事务
9. 提交事务
10. 回滚事务
11. 转移字段名 , 数据库表名和字段名需要使用引号包裹起来 , 根据数据库类型进行处理. 例如 , MySQL 数据库需要使用反引号包裹起来 , SQLite 数据库需要使用双引号包裹起来.

## 5. 数据库驱动实现

### 5. 1 MySQL 数据库驱动实现

1. 成员 : 
    1. MySQL 配置信息
    2. 数据库连接句柄
    3. 是否已连接
2. 内部类 :
    1. ParamBind : 用于预编译 SQL 语句的参数绑定
        1. 成员 : vector<MYSQL_BIND> , 缓冲区 : 整形缓冲区 , 字符串缓冲区 , 浮点数缓冲区 , 布尔值缓冲区 , NULL 缓冲区 , 以上参数的大小与预编译 SQL 语句中参数的数量相同 , bool 缓冲区不能使用 vector 存储 , bool 类型的缓冲区使用指针存储.
        2. 根据参数在SQL语句中的索引 , 将 ParameterWrapper 对象值绑定对应对应索引的数据缓冲区 , 并将该索引数据缓冲区绑定到 MYSQL_BIND 数组对应索引的结构中
        3. 提供获取 MYSQL_BIND 结果的方法
        4. 析构释放缓冲区
    2. ResultBind : 用于查询类 SQL 语句的结果绑定
        1. 成员 : MYSQL_RES , MYSQL_STMT , MYSQL_BIND
        2. 通过 MYSQL_RES 获取查询结果 , 初始化 MYSQL_BIND 数组 , 并分配各个字段的缓冲区 , 建立 MYSQL_BIND 数组与各个元素和字段缓冲区的地址绑定 , 所以值统一以字符串的方式绑定
        3. 将 MYSQL_BIND 数组绑定到 MYSQL_STMT 中
        4. 根据MYSQL_RES句柄 , 逐列获取结果集中的数据, 最终保存在 QueryResult 中返回
        5. 析构释放缓冲区

### 5. 2 SQLite 数据库驱动实现

1. 成员 : 
    1. SQLite 配置信息
    2. SQLite 连接句柄
    3. 是否已连接
2. 内部类 :
    1. ParamBind : 用于预编译 SQL 语句的参数绑定
        1. 根据 ParameterWrapper 中参数类型 , 将数值绑定到 sqlite3_stmt 中
    2. ResultBind : 用于查询类 SQL 语句的结果绑定
        1. 获取结果集中的数据, 最终保存在 QueryResult 中返回


## 6. 数据库驱动工厂

使用工厂模式 , 创建不同的数据库驱动实例 , 支持动态创建新的数据库驱动.


## 要求

1. 在进行 SQL 操作之前 , 必须验证 SQL 语句的有效性
2. SQL 校验器 在 SQLValidator 类中进行实现,  头文件 sql_validator.h , 实现文件 sql_validator.cpp , 存放在 chat-excel/svc_database_service/driver 中
3. 基础结构定义头文件为 database_schema.h , 实现文件 database_schema.cpp , 存放在 chat-excel/svc_database_service/driver 中
4. 接口的抽象类在 database_driver.h 中 , 实现文件 database_driver.cpp , 存放在 chat-excel/svc_database_service/driver 中
5. MySQL 数据库驱动实现类在 mysql_database_driver.h 中 , 实现文件 mysql_database_driver.cpp , 存放在 chat-excel/svc_database_service/driver 中
6. SQLite 数据库驱动实现类在 sqlite_database_driver.h 中 , 实现文件 sqlite_database_driver.cpp , 存放在 chat-excel/svc_database_service/driver 中
7. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
8. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
9. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印


请求仔细阅读数据库驱动层的要求,然后帮我复述一遍,并给出你的实现详细思路,等我确认你和我理解一致之后,我再逐步告诉你实现实现步骤

