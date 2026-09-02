
#  发现消息处理

**系统身份** : 你是一个资深的 C++ 开发工程师 , 熟悉后端开发中业务逻辑的实现

**任务** : 完成 AI 子服务中给 AI 发送消息的模块

阅读 chat-excel/prompt/ai_service/send_process.md 文件理解 AI 子服务中给 AI 发送消息的流程.
下面是对各个流程的实现方案介绍:

## 1. 收集数据库元信息

1. 对于 Excel 场景 : 通过文件子服务 , 根据文件 ID 获取 Excel 文件所有 WorkSheet 对应的数据库表名称列表
2. 对于数据库场景 : 从 RPC 请求参数中获取实际要操作的数据库表名列表

## 2. 提示词的构建

总共存在三种提示词模板 , 具体内容在 chat-excel/svc_ai_service/prompt_template 目录下
1. 分析提示词
2. 总结提示词
3. 发送邮件提示词

提示词模板类已经实现完成 , 在 chat-excel/svc_ai_service/prompt_template.h 中定义

## 3. 普通消息发送

如果请求参数中的 chat_type 是 "plain" , 说明是普通消息场景.
1. 直接将用户的消息通过 AIChatSDK 发送给 AI.

AIMessageHandler 类提供普通消息发送的接口 , 用于将用户的消息通过 AIChatSDK 发送给 AI.

## 4. 发送邮箱

在分析阶段的回复中 , 如果模型认为用户是想要发送邮件 , 会回复 "<EMAIL_START>sendEmail<EMAIL_END>" 内容以调用邮箱发送工具函数. 因此需要提供 AIMessageHandler 需要提供两种方法:
1. 是否包含发送邮件指令 : 检查模型回复是否包含 "<EMAIL_START>sendEmail<EMAIL_END>" 内容
2. 发送邮箱 , 具体流程如下 : 
    1. 通过用户子服务, 获取用户邮箱信息
    2. 通过 AIChatSDK 获取最近发送的两条消息(AI 返回的分析消息和总结消息)
    3. 从分析消息中提取标题和分析内容 , 从总结消息中提取总结内容 ,构建邮箱参数 JSON 对象 , 填充发送邮件提示词
    4. 将提示词发送给 AI  , 让模型生成邮件内容 , 解析 JSON 对象中的邮箱主题和邮箱内容字段
    5. 通过邮箱子服务 , 发送邮箱
    6. 给前端发送消息 , "邮箱发送成功 , 请注意查收"

## 5. 发送消息 RPC 接口

AIMessageHandler 提供发送消息的接口 , 流程如下 : 
1. 根据 chat_type 判断聊天场景
    1. "plain" : 直接发送普通消息并返回即可
    2. "excel" : 获取 Excel 对于的数据库表名称
    3. "database" : 从 RPC 请求参数中获取实际要操作的数据库表名列表
2. 收件分析提示词
3. 给模型发送消息 , 分析用户问题 , 生成 SQL 语句
4. 检查模型回复 , 是否为发送邮件工具调用 , 如果是直接调用发送邮箱方法 , 并给前端返回模型消息"邮箱发送成功 , 请注意查收" , 后续流程结束 , 不执行 SQL 语句
5. 从模型回复中提取 SQL 语句(包含在 "<SQL_START>SQL_END>" 标签中). 其余标签内容后端不关心 , 直接返回给前端 
6. 发送给数据库子服务 , 执行 SQL 语句 , 注意执行 SQL 语句的时候 , 智能 Excel 场景下使用统一的连接 ID "excel_connection" 数据库连接 , 数据库场景请求参数中携带专门的数据库连接.
7. 构建总结提示词
8. 给模型发送消息 , 生成总结内容
10. 构建并发送最终响应给前端 , 根据 SQL 执行结果 , 填充 JSON 结构返回给前端:
```json
{
    "summary": "总结阶段生成的总结内容",
    "displayType": "总结节点生成的可视化显示类型",
    "data": {                               // data为sql执行结果
        "columns": ["列1", "列2"],          // 第一张表的列
        "column_types": ["string", "string"], // 第一张表的列类型
        "rows": [["值1", "值2"]],           // 第一张表的行
        "tables": [                            // 所有表数据（完整保留）
            {"columns": [...], "rows": [...]},
            {"columns": [...], "rows": [...]},
            {"columns": [...], "rows": [...]}
        ]
    }
}
```


## 6. 总结 AIMessageHandler 类

1. 成员 : 
    1. ChatSessionData 智能指针 , 用于对用户会话数据进行管理
    2. <cpp-toolkit/rpc.h> 中的 ChannelManager 智能指针, 用于操作RPC通道
    3. AIChatSDK 智能指针 , 用于调用 AI 模型
2. 接口 : 
    1. 发送消息给 AI 模型 , 并返回模型回复 , 具体流程如下 : 
        1. 根据 chat_type 判断聊天场景
            1. "plain" : 直接发送普通消息并返回即可
            2. "excel" : 获取 Excel 对于的数据库表名称
            3. "database" : 从 RPC 请求参数中获取实际要操作的数据库表名列表
        2. 收件分析提示词
        3. 给模型发送消息 , 分析用户问题 , 生成 SQL 语句
        4. 检查模型回复 , 是否为发送邮件工具调用 , 如果是直接调用发送邮箱方法 , 并给前端返回模型消息"邮箱发送成功 , 请注意查收" , 后续流程结束 , 不执行 SQL 语句
        5. 从模型回复中提取 SQL 语句(包含在 "<SQL_START>SQL_END>" 标签中). 其余标签内容后端不关心 , 直接返回给前端 
        6. 发送给数据库子服务 , 执行 SQL 语句 , 注意执行 SQL 语句的时候 , 智能 Excel 场景下使用统一的连接 ID "excel_connection" 数据库连接 , 数据库场景请求参数中携带专门的数据库连接.
        7. 构建总结提示词
        8. 给模型发送消息 , 生成总结内容
        9. 构建并发送最终响应给前端 , 根据 SQL 执行结果 , 填充 JSON 结构返回给前端
        10. 通过 ChatSessionData 更新用户会话数据 , 聊天总数 , 如果是第一条消息更新 title 字段 , 更新最近一次消息时间
    2. 将上述涉及到的方法封装为函数


## 7. 补充说明
1. 定义 AIMessageHandler  类 , 包含在 chat_excel::ai_service 命名空间中,负责管理提示词模板
2. 头文件 ai_message_handler.h , 源文件 ai_message_handler.cpp , 放在 chat-excel/svc_ai_service 路径下
3. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
4. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
5. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
6. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性
7. 单元测试 : 对于 AIChatSDK 使用下面配置信息进行初始化 , 从环境变量中获取 : 
    1. DEEPSEEK_API_URL : 模型 API 地址
    2. DEEPSEEK_API_KEY : 模型 API 密钥
    3. DEEPSEEK_API_MODEL : 模型名称

## 8. 接口实现细节

1. 邮件参数中 analysis 和 summary 字段来源: 邮箱中总结的内容是用户上一次发送的消息 , AI 上层的分析和总结内容是模型 , 并不是本次发送的消息 , 本次发送的消息是用户说明要将刚刚的分析和总结内容发送到邮箱中


请求仔细阅读业务逻辑层的要求,然后帮我复述一遍,并给出你的实现详细思路,等我确认你和我理解一致之后,我再逐步告诉你实现实现步骤


## 9. 修改会话 Title 字段的生成逻辑

1. 当前 title 字段的生成逻辑 : UpdateSessionMetadata(context.request_id, session_info, context.message); 直接将用户发送的消息作为会话标题
2. 不使用用户发送的消息作为 title 字段 , 使用模型在对于问题的chat-excel/svc_ai_service/prompt_template/analyze_prompt.md 分析提示词中返回的 <TITLE_START> ... <TITLE_END> 中的内容作为会话标题
3. 但是对于 plain 场景 , 不存在分析提示词的构建 , 因此如果 AI 的消息中不包含 <TITLE_START> ... <TITLE_END> 标签 , 则直接将用户发送的消息(截取前 20 字)作为会话标题