
# 用户子服务 HTTP 接口实现

**系统身份** : 你是一名资深的 C++ 开发工程师 , 擅长网关服务的实现

**任务** : 完成网关子服务中涉及到的用户子服务 9 个 HTTP 接口的实现

## 1. 相关文档

1. 网关子服务 API 接口定义文档 :  chat-excel/prompt/gateway_service/api.md
2. 用户子服务 RPC 接口定义文档 : chat-excel/proto/user_service.proto 
3. 所有的用户子服务 RPC 接口声明和实现 : chat-excel/svc_gateway_service/gateway_impl_service.h , chat-excel/svc_gateway_service/gateway_impl_service.cc

## 2. 用户子服务涉及到的 9 个 HTTP 接口

1. 检验用户邮箱是否唯一 : HandleUserNicknameValid
2. 检验用户昵称是否唯一 : HandleUserEmailValid
3. 用户注册 : HandleUserRegister
4. 获取验证码 : HandleUserCode
5. 用户密码登录 : HandleUserPasswdLogin
6. 验证码登录 : HandleUserVcodeLogin
7. 会话登录 : HandleUserSessionLogin
8. 获取个人信息 : HandleUserInfo
9. 退出登录 : HandleUserLogout

每个接口的实现流程如下 : 
1. 解析 HTTP 请求
2. 构建并发送 RPC 请求
	1. 获取用户子服务信道
	2. 创建用户子服务 RPC 客户端
	3. 构建 RPC 请求
	4. 调用用户子服务的 RPC 接口
	5. 解析 RPC 响应是否成功
3. 解析 RPC 响应
4. 构建 HTTP 响应

## 3. 补充

1. 在实现 获取用户信息和退出登录的时候 , 要先进行鉴权 , 判断用户的会话是否有效
2. 使用 <cpp-toolkit/util.h> 中封装的 JsonUtil 来进行序列化和反序列化
3. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印
4. 对于逻辑相同 , 重复性的代码逻辑进行封装, 提升代码的可复用性

请严格按照上述要求, 帮我完成网关中涉及的用户子服务的9个HTTP接口的实现. 实现完成之后, 同步修改CMakeLists.txt文件 ,确保网关子服务代码能编译成功 