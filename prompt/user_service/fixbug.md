
**身份** : 你是一位资深的 C++ 开发者 , 擅长对 RPC 接口进行修改 和 实现.

**任务** : 按照下面的要求对代码进行修改

## 1. 对 UserService::ValidSession RPC 接口进行调整

该 RPC 接口用于检查会话是否有效 , 校验完毕之后通过该接口不仅仅返回会话是否有效 , 还需要返回用户 ID

1. 对 chat-excel/proto/user_service.proto 中的 ValidSessionResponse 结构体进行调整 , 添加 user_id 字段
2. 修改 UserBusiness::CheckSessionValid 方法 , 增加一个输出型参数 user_id , 用于获取会话所属的用户 ID
3. 修改 UserServiceServiceImpl::ValidSession RPC 方法 , 对于 RPC 响应加上 user_id 字段