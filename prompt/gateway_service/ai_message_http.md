
# 网关 HandleAiSendStreamMessage 接口实现

**系统身份** : 你是一名资深的 C++ 开发工程师 , 擅长网关服务的实现

**任务** : 完成 HandleAiSendStreamMessage 接口的实现([A06] 发送消息 流式, SSE)

## 1. 相关文档

1. 网关子服务 API 接口定义文档 :  chat-excel/prompt/gateway_service/api.md
2. AI 子服务 RPC 接口定义文档 : chat-excel/proto/ai_service.proto 
3. 所有的 AI 子服务 RPC 接口声明和实现 : chat-excel/svc_ai_service/ai_impl_service.h , chat-excel/svc_ai_service/ai_impl_service.cc
4. 网关所有 HTTP 接口定义和声明 : chat-excel/svc_gateway_service/gateway_impl_service.h , chat-excel/svc_gateway_service/gateway_impl_service.cc

## 2. HandleAiSendStreamMessage 接口

因为发送消息接口内部需要搭建 HTTP 客户端 , 向 AI 子服务发送消息请求 ,然后通过 HTTP 协议分块传输 , 将 AI 子服务返回的流失消息 , 主动推送给前端. 虽然 AI 子服务是 RPC 服务器 ,但是 Brpc 兼容 HTTP 协议 , 并且 AI 子服务发送流失消息响应就是以 HTTP 协议实现的 , 因此网关以 HTTP 方式将发送消息请求路由给 AI 子服务 , AI 子服务是可以处理的.

每个接口的实现流程如下 : 
1. 解析 HTTP 请求
2. 鉴权
3. 设置 SSE 响应头 , 响应头开启流式传输 , 并设置 SSE 数据块处理回调
4. 在回调函数中 , 构建 HTTP 客户端 , 向 AI 子服务发送消息请求 , 具体如下 : 
    1. 构建请求参数
    2. 通过 ChannelManager 获取通信信道
    3. 创建 HTTP 客户端
    4. 设置 HTTP 请求头参数 : 读超时时间300s , 写超时时间30s , 连接超时时间10s , 设置 Content-Type-Type 为 application/json , 禁止压缩传输 , 启动 TCP_NODELAY
    5. 配置 HTTP 请求参数 : 设置请求方法为 POST 
    6. 设置响应头处理器回调 , 检测是否成功建立连接
    7. 设置内容接收器回调 : 定义回调函数 , 将 AI 子服务返回的 SSE 数据块 , 主动推送给前端
    8. 发送 HTTP 请求 

## 3. 补充

1. 在调用接口之前, 要先进行鉴权 , 判断用户的会话调用用户子服务的 RPC ValidSession , 并通过 RPC 响应获取用户 ID , 作为后续调用 AI 子服务的 RPC 请求的参数(如果需要使用的话)
2. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
3. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
4. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性
5. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
6. 实现时参考网关其余 HTTP 接口的实现

请严格按照上述要求, 帮我完成 HandleAiSendStreamMessage 接口的实现. 实现完成之后, 同步修改CMakeLists.txt文件 ,确保网关服务代码能编译成功 
