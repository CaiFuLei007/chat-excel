
#  文件业务逻辑实现

**系统身份** : 你是一个资深的 C++ 开发工程师 , 熟悉后端开发中业务逻辑的实现

**任务** : 完成 AI 子服务的业务逻辑的实现

## 1. 聊天会话管理

聊天会话管理是对ChatSessionData的进一步封装 , 实现聊天会话的持久化存储 , 缓存以及数据库操作编排. 聊天会话元数据信息如下 : 

| 字段 | 类型 | 约束 | 空值 | 说明 | 
| --- | --- | --- | --- | --- | --- |
| id | BIGINT UNSIGNED | PRIMARY KEY AUTOINCREMENT | NOT NULL | 自增主键 |
| chat_session_id | VARCHAR(32) | UNIQUE | NOT NULL | 会话 ID |
| user_id | VARCHAR(32) | | NOT NULL | 用户 ID |
| title | TEXT | NOT NULL | 会话标题 , 默认为第一条消息，最长20个字符 |
| create_time | BIGINT |  | NOT NULL | 创建时间戳 |
| update_time | BIGINT |  | NOT NULL | 最后更新时间戳 |
| total_message_count | BIGINT |  | NOT NULL | 会话总消息数 |
| model_name | VARCHAR(30) | | NOT NULL | 模型名称 |
| file_id | VARCHAR(32) | | NOT NULL | 文件 ID |
| type | VARCHAR(20) | | NOT NULL | 会话类型(excel / database) |
| connection_info | TEXT | | NULL | 连接信息 |

聊天消息原信息如下 : 不放入缓存
| 列名	| 类型	| 约束	| 说明 |
| --- | --- | --- | --- |
| ID | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键 |
| MID | CHAR(36) | NOT NULL UNIQUE | 消息 ID |  
| SSID | CHAR(36) | NOT NULL | 所属会话 ID , 与 ChatSessionData中的 chat_session_id 字段对应 |
| ROLE | VARCHAR(30) | NOT NULL | 角色 ("user"/"assistant") |
| CONTENT | TEXT | NOT NULL | 消息内容 |
| CREATE_TIME | INT | NOT NULL | 创建时间戳 |

## 2. 聊天会话管理类 CharSessionManager

1. 成员 : 
    1. ChatSessionData 实例指针
2. 接口 : 
    1. 检测指定聊天会话是否属于指定用户
    2. 保存或更新聊天会话元数据(更新后 , 删除缓存)
    3. 通过用户 ID 获取所有聊天会话元数据(先从缓存中获取 , 再从数据库中获取)
    4. 通过聊天会话 ID 获取指定聊天会话元数据(先从缓存中获取 , 再从数据库中获取)
    5. 通过会话 ID 删除指定聊天会话元数据(先删除数据库 , 再删除Redis缓存)
    6. 将文件 ID 与聊天会话关联起来 ,修改聊天会话元数据的 file_id 字段

## 3. 业务层 AiBusiness 类

1. 定义 AiBusiness 类 , 包含在 chat_excel::ai_service 命名空间中,负责管理 AI 业务逻辑
2. 头文件 ai_business.h , 源文件 ai_business.cpp , 放在 chat-excel/svc_ai_service 路径下
3. 成员如下 :
	1. CharSessionManager 智能指针 , 用于操作管理聊天会话
	2. <aichat_sdk/aichat_sdk.h> 中的 AIChatSdk 智能指针 , 和ChatSDK交互的实例 , 用于新建聊天会话 , 获取指定聊天会话等操作
    3. <cpp-toolkit/rpc.h> 中的 ChannelManager 智能指针, 用于操作RPC通道
	4. 所有成员变量的初始化必须在 AiBusiness 构造函数中进行,但具体实例其他位置传入
4. 接口
    1. 获取可用的模型列表
    2. 新建聊天会话
    3. 获取指定用户的聊天会话列表
    4. 通过会话 ID 获取指定聊天会话元数据
    5. 通过用户 ID 获取所有聊天会话元数据
    6. 获取指定会话 ID 的历史消息
    7. 删除指定用户的指定聊天会话元数据
    8. 更新聊天会话关联的文件 ID 字段

## 3. 注意事项

1. 除了新建会话以外 , 所有操作都需要检查会话是否属于当前用户
2. MySQL 中文件信息更新之后要将 Redis 原来存储的文件信息删除 , 等待后续重新缓存
3. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
4. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
5. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
6. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性
7. 业务层主要负责调用 ChatSDK 和数据层方法 , 处理业务逻辑 , 不负责具体的RPC接口实现
8. aichat_sdk 已经被安装到了 /usr/local/include/aichat_sdk/aichat_sdk.h 下 , 直接包含即可使用

请求仔细阅读业务逻辑层的要求,然后帮我复述一遍,并给出你的实现详细思路,等我确认你和我理解一致之后,我再逐步告诉你实现实现步骤