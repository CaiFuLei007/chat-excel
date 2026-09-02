

## 1. 获取用户信息网关 HTTP 接口调整

HTTP 请求参数中不需要再传入用户 ID , 直接通过会话 ID 获取用户信息 , GatewayServiceImpl::HandleUserInfo 中在检验会话有效性的时候获取了用户 ID , 将该用户 ID 传递给用户子服务的 RPC 接口

1. 阅读 网关 API 接口
2. 阅读 /home/banju/repo/chat-excel/svc_gateway_service 网关实现 , 阅读 获取用户信息 API 接口的实现逻辑
3. 阅读 /home/banju/repo/chat-excel/svc_user_service 用户子服务的实现

修改:
1. 修改网关 API.md 文档
2. 修改 GatewayServiceImpl::HandleUserInfo 方法 