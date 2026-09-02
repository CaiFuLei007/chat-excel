
**身份** : 你是一位资深的 C++ 开发者 , 擅长对 RPC 接口进行修改 和 实现.

**任务** : 按照下面的要求对代码进行修改

## 1. 对 UserService::ValidSession RPC 接口进行调整

该 RPC 接口用于检查会话是否有效 , 校验完毕之后通过该接口不仅仅返回会话是否有效 , 还需要返回用户 ID

1. 对 chat-excel/proto/user_service.proto 中的 ValidSessionResponse 结构体进行调整 , 添加 user_id 字段
2. 修改 UserBusiness::CheckSessionValid 方法 , 增加一个输出型参数 user_id , 用于获取会话所属的用户 ID
3. 修改 UserServiceServiceImpl::ValidSession RPC 方法 , 对于 RPC 响应加上 user_id 字段

## 2. 对 用户注册 RPC 接口进行调整

用户注册的时候给用户邮箱发送验证码 , 如果验证码正确才能进行注册

1. 阅读 网关 API 接口
2. 阅读 /home/banju/repo/chat-excel/svc_gateway_service 网关实现 , 阅读 用户注册 API 接口的实现逻辑
3. 阅读 /home/banju/repo/chat-excel/svc_user_service 用户子服务的实现

修改:
1. 修改网关 API.md 文档 , 用户注册时添加验证码参数字段
2. 修改 GatewayServiceImpl::HandleUserRegister 方法 , 提取出验证码参数
3. 修改 chat-excel/proto/user_service.proto 用户子服务用户注册的 RPC 接口 , 添加验证码字段
4. 修改 UserServiceImpl::UserRegister 添加对验证码字段解析
5. 修改 UserBusiness::UserRegister 方法 , 校验验证码是否正确 , 正确才能进行注册