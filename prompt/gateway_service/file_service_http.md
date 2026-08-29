
# 用户子服务 HTTP 接口实现

**系统身份** : 你是一名资深的 C++ 开发工程师 , 擅长网关服务的实现

**任务** : 完成网关子服务中涉及到的文件子服务 9 个 HTTP 接口的实现

## 1. 相关文档

1. 网关子服务 API 接口定义文档 :  chat-excel/prompt/gateway_service/api.md
2. 文件子服务 RPC 接口定义文档 : chat-excel/proto/file_service.proto 
3. 所有的文件子服务 RPC 接口声明和实现 : chat-excel/svc_gateway_service/gateway_impl_service.h , chat-excel/svc_gateway_service/gateway_impl_service.cc

## 2. 文件子服务涉及到的 9 个 HTTP 接口

1. 上传文件信息 : HandleFileUploadInfo
2. 获取文件信息 : HandleFileInfo
3. 上传文件数据 : HandleFileUpload
4. 下载文件 : HandleFileDownload
5. 删除文件 : HandleFileDelete
6. 预览 Excel 文件 : HandleFilePreview
7. 获取用户文件列表 : HandleFileList
8. 关联文件和聊天会话映射 : HandleFileChatMap
9. 上传 SQLite 文件 : HandleFileSqliteUpload

每个接口的实现流程如下 : 
1. 解析 HTTP 请求
2. 构建并发送 RPC 请求
	1. 获取文件子服务信道
	2. 创建文件子服务 RPC 客户端
	3. 构建 RPC 请求
	4. 调用文件子服务的 RPC 接口
	5. 解析 RPC 响应是否成功
3. 解析 RPC 响应数据
4. 构建 HTTP 响应

## 3. 补充

1. 在调用接口之前, 要先进行鉴权 , 判断用户的会话调用用户子服务的 RPC ValidSession , 并通过 RPC 响应获取用户 ID , 作为后续调用文件子服务的 RPC 请求的参数(如果需要使用的话)
2. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
3. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
4. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性

请严格按照上述要求, 帮我完成网关中涉及的文件子服务的9个HTTP接口的实现. 实现完成之后, 同步修改CMakeLists.txt文件 ,确保网关子服务代码能编译成功 
