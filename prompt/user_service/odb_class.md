
# odb 数据库映射类实现

- 你是一个资深的 C++ 后端开发工程师 , 对微服务架构有深入了解 , 熟悉 odb 数据库映射类的编写 , 以及 odb 工具使用

任务 : 帮我给用户子服务编写 odb 数据库表映射类, 完成用户表和会话表的 C++ 类映射 , 并完成映射类的编译

## 1. 用户表设计
用户表的字段定义如下:
| 字段 | 类型 | 约束 | 空值 | 备注 | 
| --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | PRI | NOT NULL | 主键 ID |
| user_id | VARCHAR(32) | UNIQUE | NOT NULL | 用户 ID |
| nickname | VARCHAR(32) | UNIQUE | NOT NULL | 用户昵称 |
| email | VARCHAR(32) | UNIQUE | NOT NULL | 用户邮箱 |
| password | VARCHAR(32) | | NOT NULL | 用户密码 |
| status | TINYINT UNSIGNED | | NOT NULL | 0 表示未登录 , 1 表示已登录 |

注意事项 : 
1. 密码必须存储加密后结果 , 不能存储明文密码
2. 系统支持用户昵称登录 , 因此用户昵称必须唯一
3. 系统支持用户邮箱登录 , 因此用户邮箱必须唯一
4. 创建用户表的 SQL 语句以及 CURD 操作将来通过 odb 完成


## 2. 会话表
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

# 3. 要求

1. 编写 odb 数据库表映射类
    1. 编写用户表的 odb 映射类 UserEntity , 表名称为 tbl_user , 严格按照用户表设计中的字段进行映射
    2. 编写会话表的 odb 映射类 SessionEntity , 表名称为 tbl_session , 严格按照会话表设计中的字段进行映射
    3. UserEntity 头文件名称 为 user_entity.h , SessionEntity 头文件名称 为 session_entity.h
    4. 头文件存放在 chat-excel/data 目录下
2. 编写 dob 映射类编译的 CMakeLists.txt 文件 , 完成 UserEntity 和 SessionEntity 的编译 , 要求 : 
    1. CMakeLists.txt 文件存放在 chat-excel/data 目录下
    2. odb 编译后生成的代码文件放置在 chat-excel/data/odb 目录下
    3. odb 生成的 .sql 文件放置在 chat-excel/data/sql 目录下

仔细阅读用户表和会话表的设计 , 以及映射类的实现要求 , 完成用户子服务中用户表和会话表的 C++ 类映射 , 并完成映射类的编译