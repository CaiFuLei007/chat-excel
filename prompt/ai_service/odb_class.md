# odb 数据库映射类实现

**身份** : 你是一个资深的 C++ 后端开发工程师 , 对微服务架构有深入了解 , 熟悉 odb 数据库映射类的编写 , 以及 odb 工具使用

任务 : 帮我给AI子服务编写 odb 数据库表映射类, 完成用户聊天会话表的 C++ 类映射 , 并完成映射类的编译


## 1. ChatSDK 第三方库介绍

ChatSDK 已经实现好了 , 已经安装到系统目录下了, 可以直接进行使用 头文件在 : /usr/local/include/aichat_sdk/aichat_sdk.h 

### 1.1 表结构

会话信息表 :
| 列名	| 类型	| 约束	| 说明 |
| ID | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键 |
| UID | CHAR(36) | NOT NULL | 所属用户 UUID |
| SSID | CHAR(36) | NOT NULL UNIQUE | 会话 ID |
| MODEL_NAME | VARCHAR(30) | NOT NULL | 模型名称 |
| CREATE_TIME | INT | NOT NULL | 创建时间戳 |
| UPDATE_TIME | INT | NOT NULL | 最后更新时间戳 |

聊天消息表:
| 列名	| 类型	| 约束	| 说明 |
| ID | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键 |
| MID | CHAR(36) | NOT NULL UNIQUE | 消息 ID |  
| SSID | CHAR(36) | NOT NULL | 所属会话 ID |
| ROLE | VARCHAR(30) | NOT NULL | 角色 ("user"/"assistant") |
| CONTENT | TEXT | NOT NULL | 消息内容 |
| CREATE_TIME | INT | NOT NULL | 创建时间戳 |

### 1.2 接口

1. 获取模型列表
2. 新建聊天会话
3. 获取聊天会话列表
4. 获取指定聊天会话的历史消息
5. 删除聊天会话
6. 给模型发送消息(模型以流式响应)

## 2. AI 子服务 用户聊天会话表设计

| 字段 | 类型 | 约束 | 空值 | 说明 | 
| --- | --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | PRIMARY KEY AUTOINCREMENT | NOT NULL | 自增主键 |
| chat_session_id | VARCHAR(32) | UNIQUE | NOT NULL | 会话 ID |
| user_id | VARCHAR(32) | INDEX | NOT NULL | 用户 ID |
| title | TEXT | NULL | 会话标题 , 默认为第一条消息，最长20个字符 |
| create_time | BIGINT |  | NOT NULL | 创建时间戳 |
| update_time | BIGINT |  | NOT NULL | 最后更新时间戳 |
| total_message_count | BIGINT |  | NOT NULL | 会话总消息数 |
| model_name | VARCHAR(30) | | NOT NULL | 模型名称 |
| file_id | VARCHAR(32) | | NULL | 文件 ID |
| type | VARCHAR(20) | | NOT NULL | 会话类型(excel / database) |
| connection_info | TEXT | | NULL | 连接信息 |

此处的 type 功能 : 
1. 前端通过会话类型确认该会话用户操作的是 Excel 文件还是数据库
2. 如果是 Excel 类型 , 用户查看该会话是就会跳转到 Excel 文件的聊天页面
3. 如果是数据库 类型 , 用户查看该会话是就会跳转到数据库的聊天页面

说明 : 
1. 数据库表明为 tbl_chat_session
2. 创建数据库表的 SQL 语句 , 通过 odb 工具生成

# 3. 要求

1. 编写 odb 数据库表映射类
    1. 编写文件信息表的 odb 映射类 ChatSessionEntity , 表名称为 tbl_chat_session , 严格按照文件信息表设计中的字段进行映射
    3. ChatSessionEntity 头文件名称 为 chat_session_entity.h 
    4. 头文件存放在 chat-excel/data 目录下
2. 编写 odb 映射类编译的 CMakeLists.txt 文件 , 完成 ChatSessionEntity 的编译 , 要求 : 
    1. CMakeLists.txt 文件存放在 chat-excel/data 目录下
    2. odb 编译后生成的代码文件放置在 chat-excel/data/odb 目录下
    3. odb 生成的 .sql 文件放置在 chat-excel/data/sql 目录下

仔细阅读会话信息表的设计 , 以及映射类的实现要求 , 完成AI聊天子服务中会话信息表的 C++ 类映射 , 并完成映射类的编译
