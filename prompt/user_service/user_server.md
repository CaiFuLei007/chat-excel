
# RpcServer 服务器搭建

**系统身份** : 你是一位资深的 C++ 后端开发工程师 , 熟悉使用 Rpc 框架实现 Rpc 服务

**任务** : 完成用户子服务所有 Rpc 服务器的搭建 , 服务构建器的实现 , 以及程序主流程实现 , 并完成项目编译的 CMakeList.txt 文件

## 1. UserServer 服务器

1. 定义 UserServer 类 ,负责搭建 Rpc 服务器
2. 头文件 user_server.h , 源文件 user_server.cpp , 放在 chat-excel/svc_user_service 路径下
3. UserServer 负责实现 Rpc 服务器 并提供启动接口 , 关于用户子服务的业务逻辑已经在 UserServiceImpl 中进行实现了
4. 成员如下 : 
	1. UserServiceImpl 智能指针 , 调用业务逻辑接口
	2. <cpp-toolkit/etcd.h> 中的 SvcWatcher 智能指针 ,  用于服务监控
	3. <cpp-toolkit/etcd.h> 中的 SvcProvider 智能指针 ,  用于服务注册
	4. brpc::Server (使用 <cpp-toolkit/rpc.h> 中的 ServerFactory 构建)
5. 接口 : 
	1. start 启动

## 2. UserServerBuilder 构建器

1. 定义 UserServerBuilder 类 ,负责构建 UserServer 对象
2. 头文件 user_server_builder.h , 源文件 user_server_builder.cpp , 放在 chat-excel/svc_user_service 路径下
3. 成员如下 : 
	1. MySQL 的配置信息智能指针管理 (<cpp-toolkit/odb.h> 中的 MySQLSettings 对象)
	2. Redis 的配置信息智能指针管理 (<cpp-toolkit/redis.h> 中的 RedisSettings 对象)
	3. 监听的端口号
	4. 注册中心配置信息 (etcd 地址 , 服务名称 , 服务地址)
	5. UserServer 指针指针 , 用于返回
	6. 要监听的服务名称 (std::vector<std::string>)
	7. 服务通道管理智能指针 (<cpp-toolkit/odb.h> 中的 ChannelManager)
4. 接口 : 
	1. 设置各个成员
	2. build 构建 , 构建流程如下 :  查考 chat-excel/svc_gateway_service/gateway_server.cc 的实现
		1. 创建channel的管理器
		2. 创建业务逻辑层 userBusiness
		3. 创建 RPC 接口定义实例 UserServiceImpl
		4. 创建 RPC 服务器
		5. 创建服务发现实例 SvcWatcher (包含 服务上线 和 服务下线的回调)
		6. 开启服务发现
		7. 服务注册
		8. 创建 UserServer 实例


## 3. main 程序主流程

1. 完成gflags参数解析、rpc服务器搭建、启动rpc服务器等，源文件为main.cc，放置在 chat-excel/svc_user_service 目录下
2. 完成对用户子服务的编译，生成CMakeLists.txt文件，放置在 chat-excel/svc_user_service 目录下

请严格按照上述要求，帮我完成rpc服务器搭建以及构建器实现，并生成main.cc完成服务器构建及启动，生成CMakeLists.txt文件，完成用户子服务的编译