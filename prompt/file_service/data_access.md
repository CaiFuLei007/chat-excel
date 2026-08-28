
# odb 代码封装 + redis 操作封装

**系统身份** : 你是一个资深的 C++ 后端开发工程师 , 熟悉通过 odb 代码操作 MySQL 数据库 , 以及 redis++ 操作 Redis

任务 : 完成文件子服务操作数据的封装 , 包括两个部分 : 对 odb 生成的代码进行封装 , 对 redis 操作进行封装

## 1. 文件信息表

1. odb 对 FileEntity 类编译生成的代码在 /chat-excel/data/odb 目录下 
2. 定义 FileData 类 , 包含在 chat_excel::file_service 命名空间中
3. 头文件 file_data.h , 源文件 file_data.cpp , 放在 /chat-excel/data 路径下
4. 类中包含两个句柄 , 分别是 MySQL 操作句柄 和 Redis 操作句柄
5. Mysql 句柄使用 <cpp-toolkit/odb.h> 中封装的代码实现 , redis 句柄使用 <cpp-toolkit/redis.h> 中封装的代码实现
6. FileData 类涉及到的 MySQL 操作如下 : 
	1. 保存或更新文件信息到数据库
	2. 通过文件 ID 获取文件信息
	3. 通过文件 ID 删除文件信息
    4. 通过 user_id 获取用户上传的所有文件列表 (所有文件元信息)
7. FileData 类涉及到的 Redis 操作如下 : 
	1. 保存文件数据到缓存
    2. 通过文件 ID 获取文件信息
    3. 通过文件 ID 删除文件信息
8. FileData 缓存策略 : Cache-Aside 旁路策略 ; 过期时间为 3天
	1. 写策略 : 删除缓存中的数据 , 修改 MySQL 数据库 , 删除缓存中的数据
	2. 读策略 : 先从缓存中进行读取 , 如果没有再到 MySQL 中进行读取 , 再将数据添加到缓存中
	3. 删除或注销 : 依赖 TTL 自然过期 
	4. 缓存类型 : hash 类型 , key 是 file_data , 
        1. field : file:{file_id}
		2. value : 一个 JSON 字符串 包含 : 文件信息 : 文件 ID , 文件名 , 文件大小 , 上传时间 , 上传用户 ID , 扩展名 , fastdfs 文件id , user_id , session_id
	9. FileData 只实现对数据库和 Redis 的 增删查改操作 , 业务逻辑上层实现 , 不考虑什么时候加入删除缓存 , 只实现对应的接口

## 2. WorkSheet 表

1. odb 对 WorkSheetEntity 类编译生成的代码在 /chat-excel/data/odb 目录下 
2. 定义 WorkSheetData 类 , 包含在 chat_excel::file_service 命名空间中
3. 头文件 worksheet_data.h , 源文件 worksheet_data.cpp , 放在 /chat-excel/data 路径下
4. 类中包含两个句柄 , 分别是 MySQL 操作句柄 和 Redis 操作句柄
5. Mysql 句柄使用 <cpp-toolkit/odb.h> 中封装的代码实现 , redis 句柄使用 <cpp-toolkit/redis.h> 中封装的代码实现
6. WorkSheetData 类涉及到的 MySQL 操作如下 : 
	1. 保存 WorkSheet 信息到数据库
	2. 通过 文件 ID 获取工作表对应的所有 WorkSheet 信息
	3. 通过 文件 ID 删除工作表对应的所有 WorkSheet 信息
7. WorkSheetData 类涉及到的 Redis 操作如下 : 
	1. 保存工作表数据到缓存
	2. 通过 文件 ID 获取工作表对应的所有 WorkSheet 信息
	3. 通过 文件 ID 删除工作表对应的所有 WorkSheet 信息
8. WorkSheetData 缓存策略 : Cache-Aside 旁路策略 ; 过期时间为 3天
	1. 写策略 : 删除缓存中的数据 , 修改 MySQL 数据库 , 删除缓存中的数据
	2. 读策略 : 先从缓存中进行读取 , 如果没有再到 MySQL 中进行读取 , 再将数据添加到缓存中
	3. 删除或注销 : 先删除 MySQL 再删除 Redis
	4. 缓存类型 : hash 类型 , key 是 worksheet_data , 
		1.  field : worksheet:{file_id}  , value 是 JSON字符串 存储 工作表对应的所有 WorkSheet 信息 : 文件 ID(只保存一份) , < worksheet 名称 , worksheet 对应的数据库表名称 >一对一
9. WorkSheetData 只实现对数据库和 Redis 的 增删查改操作 , 业务逻辑上层实现 , 不考虑什么时候加入删除缓存 , 只实现对应的接口

## 4. 注意事项

1. 如果需要定义通用结构体 , 将结构体放在 chat-excel/svc_file_service 下的 common.h 中
2. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
3. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
4. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
5. 使用 gtest 测试框架对上面的两个类进行测试
	1. 测试 FileData 类的所有接口，包括 MySQL 操作和 Redis 操作。测试代码放在: chat-excel/test/svc_file_service/file_data 目录下
	2. 测试 WorkSheetData 类的所有接口，包括 MySQL 操作和 Redis 操作。测试代码放在: chat-excel/test/svc_file_service/worksheet_data 目录下
6. 为所有测试代码编写对应的 CMakeLists.txt 进行单元测试
7. MySQL 配置 : 配置信息从环境变量中获取
	1. 数据库 : MYSQL_CHAT_EXCEL_TEST_DATABASE
	2. 用户名 : MYSQL_CHAT_EXCEL_TEST_USER
	3. 密码 : MYSQL_CHAT_EXCEL_TEST_PASSWORD
	4. 主机地址 : MYSQL_CHAT_EXCEL_TEST_HOST
	5. 端口 : MYSQL_CHAT_EXCEL_TEST_PORT
	6. 字符集 : MYSQL_CHAT_EXCEL_TEST_CHARSET
8. Redis 配置 :  配置信息从环境变量中获取
	1. 数据库索引 :Redis_CHAT_EXCEL_TEST_INDEX
	2. 用户名 : Redis_CHAT_EXCEL_TEST_USER
	3. 密码 : Redis_CHAT_EXCEL_TEST_PASSWORD
	4. 主机 IP : Redis_CHAT_EXCEL_TEST_HOST
	5. 端口 : Redis_CHAT_EXCEL_TEST_PORT
9. 注释说明 : 在每一次对 MySQL 进行 CURD 的位置, 使用注释写出具体的 SQL 语句
10. Redis 操作的时候为了提高效率 , 推荐使用 pipeline 和 transaction 来进行批量操作

请求仔细阅读数据操作封装的要求，然后帮我复述一遍，并给出你的实现详细思路，等我确认你和我理解一致之后，我再逐步告诉你实现实现步骤。