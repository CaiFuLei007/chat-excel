
# 邮箱通知子服务的实现

## 1. 通知子服务介绍

1. 通知子服务属于内部子服务 , 不与网关进行交互 , 主要负责发送验证码和普通邮件. 
2. 情况一 : 用户在前端界面点击获取验证码之后 , 网关路由到用户子服务中 , 用户子服务创建验证码 , 并将验证码通过 RPC 请求交给通知子服务 , 通知子服务将验证码发送到用户邮箱
3. 情况二 : 用户前端点击将 AI 汇总信息发送到邮箱, 网关将请求路由到 AI 子服务 , AI 子服务身份发送信息 , 交给邮箱子服务进行发送

## 2. 接口定义

通知子服务包含两个 RPC 接口 , 分别是 : 
1. 发送验证码
2. 发送普通邮件

具体的接口定义在 : chat-excel/proto/notofy_service.proto

## 3. 封装邮箱发送客户端

1. 邮箱配置结构体 MailSettings 类 : 包含邮箱用户名 , 授权码 , 发送者邮件号 , 邮箱服务器地址
2. 邮箱发送客户端是否 libcurl 进行实现 
3. 需要封装三个类 : 
	1. BaseEmailSender : 抽象基类 , 使用 curl 封装发送邮箱的主题流程和回调函数 . 邮箱正文构造通过纯虚函数由子类来进行实现. 邮箱正文构建包含邮箱主题 , 邮箱内容 (HTML格式)
		1. 传入 MailSettings 进行构造
		2. BuildEmailBody 构建邮箱正文 , 纯虚函数
		3. SendEmail 进行发送 , 传入三个参数 : 接收人 , 邮件标题 , 邮件正文
	2. VerifyCodeEmailSender : 继承基类 , 负责发送验证码 , 实现验证码邮箱正文的构建
		1. 实现 BuildEmailBody
	3. NormalEmailSender : 继承基类 , 负责发送普通邮件 , 实现普通邮件正文的构建
		1. 实现 BuildEmailBody


## 4. 异步支持

由于邮件发送耗时 , 因此邮件的发送采用异步发送的模式 . 使用生产消费者模式 , 创建一系列的工作线程用于专门发送邮件 , 每收到一个邮件发送的请求 , 都转化为一个邮件任务 EmailTask 对象 , 将该任务放到任务队列中 , 由工作线程取出执行发送逻辑, 避免主线程卡顿.

1. EmailWorkers 类 : 
	1. 设置工作线程个数
	2. 启动工作线程
	3. 停止工作线程
	4. 添加任务
2. 由工作线程循环从队列中获取任务 , 并执行 .
3. 实现时需要考虑线程同步互斥
4. EmailTask 类 :
	1. 包含三个成员 : 接收方邮箱 , 邮件标题 , 邮件正文 , 邮件发送器智能指针(调用具体的邮箱发送类)


## 5. 邮箱发送业务逻辑实现

发送业务逻辑主要由 NotifyBusiness 类进行实现 

1. 成员 : 
	1. 两个邮箱发送器
2. 接口 : 
	1. 构造函数 : 初始化 curl 全局资源 , 构建 两个邮箱发送器
	2. 析构函数 : 释放 curl 全局资源
	3. 设置邮箱发送工作线程线程个数
	4. 启动邮箱发送工作线程
	5. 停止邮箱发送工作线程
	6. 发送验证码 : 将任务放入到工作线程的任务队列中
	7. 发送普通邮件

## 6. RPC 接口实现

1. 通过子服务的 RPC 接口实现 NotifyServiceImpl. RPC 接口由 proto 工具生成 , NotifyServiceImpl 继承 proto 生成的 NotifyService 类 
2. 通过 NotifyBusiness 实现邮件发送
3. 实现流程可参考用户子服务的 RPC 接口定义和实现 : chat-excel/svc_user_service/user_service_impl.h , chat-excel/svc_user_service/user_service_impl.cpp
4. 该类只实现发送邮件的RPC接口 , 不负责RPC服务器搭建

## 7. RPC 服务器搭建

NotifyServer 类搭建 RPC 服务器 , NotifyServerBuilder 构建 NotifyServer .

1. NotifyServer 
	1. 成员 : 
		1. NotifyServiceImpl  , 调用业务逻辑接口
		2.  <cpp-toolkit/etcd.h> 中的 SvcProvider 智能指针 ,  用于服务注册
		3. brpc::Server (使用 <cpp-toolkit/rpc.h> 中的 ServerFactory 构建) , 启动 RPC 服务器
2. NotifyServerBuilder 构建者模式都将 NotifyServer
	1. 成员 : 
		1. MailSettings
		2. etcd 服务器地址
		3. brpc 子服务服务器端口 , 子服务名称 , 子服务地址
	2. 主要功能 :  完成RPC服务器端口 , 注册中心结构初始化 , 邮件客户端结构初始化 , 最后通过 Build 构造好 NotifyServer 对象并返回
3. 实现流程可参考用户子服务的 RPC 服务器搭建 : chat-excel/svc_user_service/user_server.h , chat-excel/svc_user_service/user_server.cpp

## 8. 主流程

1. 完成gflags参数解析 , 日志初始化 , 注册中心初始化 ,邮件客户端初始化 以及 RPC服务器构建和启动 , 源文件为main.cc, 放置在 chat-excel/svc_user_service 目录下
2. 完成对邮箱通知子服务的编译，生成CMakeLists.txt文件，放置在 chat-excel/svc_user_service 目录下

## 9. 实现流程

通知子服务实现流程如下：
1. 封装 BaseEmailSender 类 , 使用 curl 封装发送邮件的逻辑 , 头文件为 email_sender.h , 源文件为 email_sender.cc , 包含在 notify_service 命名空间中 
2. 封装 VerifyCodeEmailSender 类 , 负责发送验证码邮件的子类 , 实现验证码邮件的正文构造 , 头文件为 verify_code_email_sender.h , verify_code_email_sender.cc , 包含在 notify_service 命名空间中
3. 封装 NormalEmailSender 类 , 负责发送普通邮件的子类 , 实现普通邮件的正文构造 , 头文件为 normal_email_sender.h , 源文件为 normal_email_sender.cc , 包含在 notify_service 命名空间中
4. 实现异步支持
	1. 封装 EmailTask 结构 , 负责封装发送邮件的任务 , 包含接收者邮箱、邮件主题、邮件正文、邮件发送器指针
	2. 封装 EmailWorker 类 , 负责异步发送邮件 , 头文件为 email_workers.h , 源文件为 email_workers.cc , 包含在 notify_service 命名空间中
5. 封装NotifyBusiness类 , 负责发送邮件的业务逻辑 , notify_business.h , 源文件为 notify_business.cc , 包含在 notify_service 命名空间中
6. 封装NotifyServiceImpl类 , 实现发送验证码以及普通邮件的RPC接口 , 头文件为 notify_service_impl.h , 源文件为 notify_service_impl.cc , 包含在 notify_service 命名空间中
7. 搭建RPC服务器 , 并完成RPC服务器构建 , 头文件为 notify_server.h , 源文件为 notify_server.cc , 包含在 notify_service 命名空间中
8. 实现主流程 , 头文件为main.cc
9. 实现CMakeLists.txt , 完成项目构建 注意：
- 所有类包含在 notify_service 命名空间中
- 代码实现必须严格遵守项目规则文档 以及 上述要求
- 所有文件放置在 chat-excel/svc_email_notify_service 目录中


## 10. 依赖库及参考代码

1. 程序中异常情况优先使用异常 , 已经对异常进行了封装在 chat-excel/common/exception.h 下 , 必须按照该文件中的说明定义和使用
2. 使用 <cpp-toolkit/logger.h> 中封装的 spdlog 接口进行日志的输出打印

## 11. 完善用户子服务获取验证码 RPC 请求

chat-excel/svc_user_service/user_business.h 中的`std::string UserBusiness::GetVerifyCode(const std::string& email)` 接口已经完成了验证码 ID , 验证码的生成但是还没有调用 邮箱通知子服务的 RPC 接口发送验证码 , 请完善该接口 , 实现发送验证码的逻辑 :
1. 通过 ChannelManager 获取通知子服务的通信信道
2. 创建发送验证码的 RPC 请求
3. 创建通知子服务的 RPC 客户端
4. 发送 RPC 请求
5. 检测 RPC 调用是否成功
6. 如果调用失败可以抛出具体异常


请仔细阅读上述需求 , 然后列出你的详细实现规划 , 具体到每个类的职责以及实现逻辑 , 等我看完确保你和我理解一致后 , 我再告诉你后续实现 