
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
