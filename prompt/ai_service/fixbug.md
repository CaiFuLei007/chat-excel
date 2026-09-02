
## 1. 补充 AI 子服务关心的子服务

1. 文件子服务可能会向 AI 子服务发送 UpdateSessionFile RPC 方法, 来用于更新会话文件映射表
2. 因此 AI 子服务需要关心文件子服务的上线和下线事件

## 2. AIMessageHandler 发送消息 bug 处理

1. 在获取 JSON 中字段数据的时候 , 要先判断字段是否存在
2. ai_chat_sdk_->SendMessageStream 方法中的回调函数类型是std::function<void(const std::string& content, bool done)> , 第二个参数表示是否完成消息发送 , 阅读每个 SendMessageStream 调用的位置 , 接收到最后一个消息后 , done 是否设置为了 true


## 3. PromptTemplate 解析占位符 bug 处理

1. 对 PromptTemplate 的进行修改 , 内部不再解析占位符了 , 占位符由外部进行传递 , 通过一个 vector<string> 来传递提示词中有哪些占位符 , string 占位符不包含{}

## 4. AiServiceImpl::SendMessage 发送消息逻辑纠正

1. AiServiceImpl::SendMessage 在获取到用户发送的聊天数据之后 , 先向用户发送一个请求头 , 不断开连接 , 再通过 attachment 返回聊天内容 , 在返回数据块时不需要组织 SSE 格式 , 由网关来进行组织

## 5. 数据接口统一定义

1. 阅读 chat-excel/svc_ai_service AI 子服务所有实现 , 将数据接口的定义统一放置在 chat-excel/svc_ai_service/common.h 中

## 6. 增加 AIMessageHandler 发送总结消息的字段

现在的字段如下 : 
```json
{
    "summary": "总结阶段生成的总结内容",
    "displayType": "总结节点生成的可视化显示类型",
    "data": {                               // data为sql执行结果
        "columns": ["列1", "列2"],          // 第一张表的列
        "rows": [["值1", "值2"]],           // 第一张表的行
        "tables": [                            // 所有表数据（完整保留）
            {"columns": [...], "rows": [...]},
            {"columns": [...], "rows": [...]},
            {"columns": [...], "rows": [...]}
        ]
    }
}
```

为了让 AI 能够更好的展示数据 ,需要额外添加一个 column_types 字段 , 来表示每列的数据类型 :
修改后字段:
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


## 7. AI 总结的结果添加到 AIChatSDK 数据库中

1. 问题描述 : 前端发起发送消息请求之后 , 后端返回的分析阶段和总结阶段的结果都可以在前端展示出来 , 但是当用户获取指定聊天会话的历史消息的时候 , 前端能展示历史消息 , 但是对于可视化图表无法正常展示.
2. 原因分析 : 在发送消息的总结阶段 , 模型会先返回一个 JSON 结构, 该 JSON 结构会被 ChatSDK 存储到其底层的 SQLite 数据库中的 Message 中 , 然后后端会在 JSON 结果中新增用于显示可视化图表的 SQL 执行结果, 并返回给前端进行可视化图表显示 , 此时可以正常显示 , 因为最后 JSON 中包含了 SQL 的执行结果.
但是通过聊天会话 ID 获取历史消息 , 在前端展示时可视化图表无法正常显示 , 原因时保存到 ChatSDK SQLite 中的 JSON 结果中没有包含 SQL 的执行结果.
3. 因此你需要帮助我修改AI 发送消息的 RPC 方法 , 在构建完最终 JSON 结构之后 , 你需要将最终 JSON 加入到 ChatSDK 中 , 相同聊天会话中 Message 表中的最后一条 assistant 消息为最终 JSON 结构.
4. 要求 : 在 ChatSDK 现有代码下进行修改 , 不可以修改 ChatSDK 中的代码

