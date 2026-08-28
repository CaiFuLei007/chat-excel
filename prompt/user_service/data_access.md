
# odb 代码封装 + redis 操作封装

**系统身份** : 你是一个资深的 C++ 后端开发工程师 , 熟悉通过 odb 代码操作 MySQL 数据库 , 以及 redis++ 操作 Redis

任务 : 完成用户子服务操作数据的封装 , 包括两个部分 : 对 odb 生成的代码进行封装 , 对 redis 操作进行封装

## 1. 用户信息表

1. odb 对 UserEntity 类编译生成的代码在 /chat-excel/data/odb 目录下 
2. 定义 UserData 类 , 包含在 chat_excel::user_service 命名空间中
3. 头文件 user_data.h , 源文件 user_data.cpp , 放在 /chat-excel/data 路径下
4. 类中包含两个句柄 , 分别是 MySQL 操作句柄 和 Redis 操作句柄
5. Mysql 句柄使用 <cpp-toolkit/odb.h> 中封装的代码实现 , redis 句柄使用 <cpp-toolkit/redis.h> 中封装的代码实现
6. UserData 类涉及到的 MySQL 操作如下 : 
	1. 保存用户信息到数据库
	2. 通过用户 ID 获取用户信息
	3. 通过用户 email 获取用户信息
	4. 通过用户 昵称 获取用户信息
	5. 检查用户昵称是否存在
	6. 检查用户 Email 是否存在
    7. 更新用户信息
    8. 删除用户信息
    9. 删除用户缓存
7. UserData 类涉及到的 Redis 操作如下 : 
	1. 保存用户数据到缓存
	2. 通过用户 昵称 获取用户信息
	3. 通过用户 email 获取用户信息
	4. 通过用户 ID 获取用户信息
	5. 检查用户昵称是否存在
	6. 检查用户 Email 是否存在
8. UserData 缓存策略 : Cache-Aside 旁路策略 ; 过期时间为 1小时
	1. 写策略 : 删除缓存中的数据 , 修改 MySQL 数据库 , 删除缓存中的数据
	2. 读策略 : 先从缓存中进行读取 , 如果没有再到 MySQL 中进行读取 , 再将数据添加到缓存中
	3. 删除或注销 : 依赖 TTL 自然过期 
	4. 缓存类型 : hash 类型 , key 是 user_data , 
		1. 存储的 field 有三个 : user:{user_id} , user:{user_name} , user:{user_email} 
		2. 三个 field 对应相同的 value : 一个 JSON 字符串 包含 : 用户 ID , 昵称 , 邮箱 , 密码 , 状态
9. UserData 只实现对数据库和 Redis 的 增删查改操作 , 业务逻辑上层实现 , 不考虑什么时候加入删除缓存 , 只实现对应的接口

## 2. 会话表

1. odb 对 SessionEntity 类编译生成的代码在 /chat-excel/data/odb 目录下 
2. 定义 SessionData 类 , 包含在 chat_excel::user_service 命名空间中
3. 头文件 session_data.h , 源文件 session_data.cpp , 放在 /chat-excel/data 路径下
4. 类中包含两个句柄 , 分别是 MySQL 操作句柄 和 Redis 操作句柄
5. Mysql 句柄使用 <cpp-toolkit/odb.h> 中封装的代码实现 , redis 句柄使用 <cpp-toolkit/redis.h> 中封装的代码实现
6. SessionData 类涉及到的 MySQL 操作如下 : 
	1. 保存会话信息到数据库
	2. 通过 会话 ID 获取会话信息
	3. 通过 会话 ID 删除会话信息
7. SessionData 类涉及到的 Redis 操作如下 : 
	1. 保存会话数据到缓存
	2. 通过 会话 ID 获取会话信息
	3. 通过 会话 ID 删除会话信息
8. SessionData 缓存策略 : Cache-Aside 旁路策略 ; 过期时间为 3天
	1. 写策略 : 删除缓存中的数据 , 修改 MySQL 数据库 , 删除缓存中的数据
	2. 读策略 : 先从缓存中进行读取 , 如果没有再到 MySQL 中进行读取 , 再将数据添加到缓存中
	3. 删除或注销 : 先删除 MySQL 再删除 Redis
	4. 缓存类型 : hash 类型 , key 是 session_data , 
		1.  field : session:{session_id}  , value 是 JSON字符串 存储 会话 ID 和 用户 ID
9. SessionData 只实现对数据库和 Redis 的 增删查改操作 , 业务逻辑上层实现 , 不考虑什么时候加入删除缓存 , 只实现对应的接口

## 3. 验证码缓存

1. 定义 VerifyCodeData 类 , 包含在 chat_excel::user_service 命名空间中
2. 头文件 verifycode_data.h , 源文件 verifycode_data.cpp , 放在 /chat-excel/data 路径下
3. 类中包含 Redis 操作句柄 , 用以操作Redis中验证码信息
4. redis 句柄使用 <cpp-toolkit/redis.h> 中封装的代码实现
5. VerifyCodeData 类涉及到的 Redis 操作如下 : 
	1. 保存验证码信息
	2. 通过验证码 ID 获取验证码信息
	3. 通过验证码 ID 删除验证码信息
6. VerifyCodeData 缓存策略 : Cache-Aside 旁路策略 ; 过期时间为 1min
	1. 缓存类型 : hash 类型 , key 是 verifycode_data , 
		1.  field : verifycode:{verifycode_id}  , value 是 JSON字符串 存储 验证码 ID , 验证码 , 用户邮箱 , 创建时间


## 4. 注意事项

1. 密码会在逻辑层进行加密 , 数据层拿到的是已经完成加密的密码 , 不需要再进行加密
2. 如果需要定义通用结构体 , 将结构体放在 chat-excel/svc_user_service 下的 common.h 中
3. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
4. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
5. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
6. 使用 gtest 测试框架对上面的三个类进行测试
	1. 测试 UserData 类的所有接口，包括 MySQL 操作和 Redis 操作。测试代码放在: chat-excel/test/svc_user_service/user_data 目录下
	2. 测试 SessionData 类的所有接口，包括 MySQL 操作和 Redis 操作。测试代码放在: chat-excel/test/svc_user_service/session_data 目录下
	3. 测试 VerifyCodeData 类的所有接口， Redis 操作。测试代码放在: chat-excel/test/svc_user_service/verifycode_data 目录下
7. 为所有测试代码编写对应的 CMakeLists.txt 进行单元测试
8. MySQL 配置 : 配置信息从环境变量中获取
	1. 数据库 : MYSQL_CHAT_EXCEL_TEST_DATABASE
	2. 用户名 : MYSQL_CHAT_EXCEL_TEST_USER
	3. 密码 : MYSQL_CHAT_EXCEL_TEST_PASSWORD
	4. 主机地址 : MYSQL_CHAT_EXCEL_TEST_HOST
	5. 端口 : MYSQL_CHAT_EXCEL_TEST_PORT
	6. 字符集 : MYSQL_CHAT_EXCEL_TEST_CHARSET
9. Redis 配置 :  配置信息从环境变量中获取
	1. 数据库索引 :Redis_CHAT_EXCEL_TEST_INDEX
	2. 用户名 : Redis_CHAT_EXCEL_TEST_USER
	3. 密码 : Redis_CHAT_EXCEL_TEST_PASSWORD
	4. 主机 IP : Redis_CHAT_EXCEL_TEST_HOST
	5. 端口 : Redis_CHAT_EXCEL_TEST_PORT
10. 注释说明 : 在每一次对 MySQL 进行 CURD 的位置, 使用注释写出具体的 SQL 语句
11. Redis 操作的时候为了提高效率 , 推荐使用 pipeline 和 transaction 来进行批量操作

请求仔细阅读数据操作封装的要求，然后帮我复述一遍，并给出你的实现详细思路，等我确认你和我理解一致之后，我再逐步告诉你实现实现步骤。