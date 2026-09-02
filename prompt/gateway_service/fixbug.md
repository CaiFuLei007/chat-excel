

## 1. 获取用户信息网关 HTTP 接口调整

HTTP 请求参数中不需要再传入用户 ID , 直接通过会话 ID 获取用户信息 , GatewayServiceImpl::HandleUserInfo 中在检验会话有效性的时候获取了用户 ID , 将该用户 ID 传递给用户子服务的 RPC 接口

1. 阅读 网关 API 接口
2. 阅读 /home/banju/repo/chat-excel/svc_gateway_service 网关实现 , 阅读 获取用户信息 API 接口的实现逻辑
3. 阅读 /home/banju/repo/chat-excel/svc_user_service 用户子服务的实现

修改:
1. 修改网关 API.md 文档
2. 修改 GatewayServiceImpl::HandleUserInfo 方法 


## 2. GatewayServiceImpl::HandleAiSessionCreate 支持创建 plain 会话

1. 校验参数类型的时候新增 plain 类型, 会话类型只能是 excel , database , plain


## 3. 对删除文件代码逻辑进行检查

1. 前端删除 Excel 文件之后 , 文件子服务中的 Excel 元信息 , fastdfs 中的 Excel 数据 , 文件子服务中 tbl_worksheets 表中所有 Excel 的 WorkSheet 元信息 , 数据库子服务中所有 WorkSheet 数据库表 , Excel 对应的关联的用户 Session 会话信息(AI 子服务中进行管理)都要进行删除
2. 前端发送删除 SQLite 文件请求之后 , fastdfs 中的 SQLite 数据 , SQLite 管理的会话信息这两部分内容都要删除
3. 前端发送删除某个指定会话后 , 只需要将 AI 子服务中的 Session 会话信息删除即可