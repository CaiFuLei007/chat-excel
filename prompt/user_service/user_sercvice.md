
# 用户子服务介绍

项目整体框架, 功能参考 : chat-excel/prompt/chat_excel.md

## 1. 用户子服务介绍
用户子服务负责处理用户相关的业务逻辑，包括用户注册、登录、获取个人信息等 , 以 RPC 方式给网关提供服务. 除此之外 , 还需要服务注册和服务发现功能, 确保服务之间的通信正常.

## 2. 用户子服务接口
用户子服务总共涉及到 9个 RPC 接口 , 分别如下:
1. 检测用户昵称是否唯一
2. 检测邮箱是否唯一
3. 用户注册
4. 昵称密码登录
5. 邮箱登录
6. 会话登录
7. 获取验证码
8. 退出登录
9. 获取个人信息

具体的 RPC 接口定义在 chat-excel/proto/user_service.proto 中


## 3. 数据库设计
用户子服务中产生的数据将来存储在 MySQL 中, 涉及到用户表和会话表

### 3.1 用户表
用户表的字段定义如下:
| 字段 | 类型 | 约束 | 空值 | 备注 | 
| --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | PRI | NOT NULL | 主键 ID |
| user_id | VARCHAR(32) | UNIQUE | NOT NULL | 用户 ID |
| nickname | VARCHAR(32) | UNIQUE | NOT NULL | 用户昵称 |
| email | VARCHAR(32) | UNIQUE | NOT NULL | 用户邮箱 |
| password | VARCHAR(32) | | NOT NULL | 用户密码 |
| status | TINYINT UNSIGNED | | NOT NULL | 0 表示未登录 , 1 表示已登录 |

注意 : 
1. 密码必须存储加密后结果 , 不能存储明文密码
2. 系统支持用户昵称登录 , 因此用户昵称必须唯一
3. 系统支持用户邮箱登录 , 因此用户邮箱必须唯一
4. 创建用户表的 SQL 语句以及 CURD 操作将来通过 odb 完成

### 3.2 会话表
会话表的字段定义如下:
| 字段 | 类型 | 约束 | 空值 | 备注 | 
| --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | PRI | NOT NULL | 主键 ID |
| session_id | VARCHAR(32) | UNIQUE | NOT NULL | 会话 ID |
| user_id | VARCHAR(32) | | NOT NULL | 用户 ID |

注意 :
1. 会话表的会话 ID 必须唯一 , 不能重复
2. 会话表的用户 ID 可以重复 , 支持多设备登录同一账号 , 不同设备登录后对应不同会话
3. 创建会话表的 SQL 语句以及 CURD 操作将来通过 odb 完成

## 4. 缓存设计

缓存使用 redis , 使用 旁路缓存策略 :
1. 写 : 写数据库 , 删除缓存 , 数据库是唯一 "真数据源"
2. 读 : 从缓存读取 , 未命中缓存 , 则从数据库读取 , 并将结果缓存到 redis 中

### 4.1 个人信息缓存

过期时间 : 1小时
缓存策略 :
1. 写策略 : 写数据库成功 , 删除 Redis
2. 读策略 : 从缓存读取 , 未命中缓存 , 则从数据库读取 , 并将结果缓存到 redis 中
3. 删除/失效策略 : 依赖 TTL 自然过期
4. 缓存类型 : hash  
    1. key : user:user_id
    2. key : user:nickname
    3. key : user:email
    4. value : 三个 key 对应的 value 相同都是 Json 字符串
        1. 用户 ID
        2. 用户昵称
        3. 用户邮箱
        4. 用户状态
        5. 用户密码
    
### 4.2 会话缓存

过期时间 : 3天
缓存策略 :
1. 写策略 : 写数据库成功 , 删除 Redis
2. 读策略 : 从缓存读取 , 未命中缓存 , 则从数据库读取 , 并将结果缓存到 redis 中
3. 删除/失效策略 : 依赖 TTL 自然过期
4. 缓存类型 : hash  
    1. key : session:session_id
    3. value : Json 字符串
        1. 会话 ID
        2. 用户 ID

### 4.3 验证码缓存
过期时间 : 5min
缓存策略 :
1. 写策略 : 写数据库成功 , 删除 Redis
2. 读策略 : 从缓存读取 , 未命中缓存 , 则从数据库读取 , 并将结果缓存到 redis 中
3. 缓存类型 : hash  
    1. key : verify_code:code_id
    2. value : Json 字符串
        1. 验证码 ID
        2. 验证码
        3. 用户邮箱
        4. 验证码创建时间
        5. 验证码是否被使用

## 5. 实现流程
用户子服务实现流程如下 : 
1. 编写 odb 数据库表映射类 , 完成用户表和会话表的 C++ 类映射 , 以及映射类的编译 , 设计 UserEntity 类和 SessionEntity 类
2. 封装 odb 生成操作 mysql 的 C++ 代码 , 完成用户表和会话表的 CURD 操作 , 以及缓存读写操作的封装 , 设计 UserData 和 SessionData 类
3. 对 redis 验证码操作进行封装, 存储验证码 , 查找验证码, 删除验证码 , 设计 VerifyCodeCache 类
3. 业务实现 : 
    1. 封装会话管理类 : 创建会话 , 删除会话 , 检查会话有效性等 , 设计 SessionManager 类
    2. 用户业务逻辑类 : 实现 9 个 RPC 接口 , 分别对应用户子服务的 9 个接口 , 设计 UserService 类
4. RPC 接口实现 : 完成用户子服务的 RPC 接口定义和实现 , 继承 proto 生成的基类, 实现 9 个 RPC 接口
5. RPC 服务端搭建 : 完成 RPC 服务器搭建 , 以及服务发现 , 服务注册 , Redis , Rtcd 等初始化
6. 程序 main 主流程实现 : 完成 gflags 参数解析 , 日志初始化, 注册中心初始化, Redis 初始化 , 以及 RPC 服务器启动
7. 编写完整的 CMakeLists.txt 文件 , 完成项目构建
8. 在网关实现用户子服务 HTTP 接口 , 使用 curl 测试接口

注意 :
1. 完成 UserData 和 SessionData 类 , VerifyCodeCache 类后, 就进行 gtest 单元测试 , 将测试代码放在 chat-excel/test/svc_user_service/ 下
2. 后续继续业务实现 , 完成 9 个 RPC 接口的实现 , 等所有接口和类都实现之后使用 main 主程序进行联调 , 不需要再进行 gtest 单元测试

## 6. 依赖库
1. C++ 工具库 , 头文件在 <cpp_toolkit/>中, 内容包含: 
    1. etcd.h 的封装 ,服务注册和发现
    2. logger.h 日志封装
    3. redis.h 缓存封装
    4. rpc.h 对 rpc 的封装 
    5. utils.h 工具函数封装 , 包含 json 序列化和反序列化


请你先严格按照上述要求，理解用户子服务的实现需求，然后给我复述下你的实现思路。在我确保你和我理解一致后，我再告诉你逐步实现。最后，在具体实现代码前，必须先详细阅读下项目规则约定，确保生成的代码符合项目规则。
