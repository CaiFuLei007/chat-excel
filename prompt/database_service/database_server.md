
# RpcServer 服务器搭建

**系统身份** : 你是一位资深的 C++ 后端开发工程师 , 熟悉使用 Rpc 框架实现 Rpc 服务

**任务** : 完成数据库服务所有 Rpc 服务器的搭建 , 服务构建器的实现 , 以及程序主流程实现 , 并完成项目编译的 CMakeList.txt 文件

## 1. DatabaseServer 服务器

1. 定义 DatabaseServer 类 ,负责搭建 Rpc 服务器
2. 头文件 database_server.h , 源文件 database_server.cpp , 放在 chat-excel/svc_database_service 路径下
3. DatabaseServer 负责实现 Rpc 服务器 并提供启动接口 , 关于数据库服务的业务逻辑已经在 DatabaseServiceImpl 中进行实现了
4. 成员如下 : 
	1. DatabaseServiceImpl 智能指针 , 调用业务逻辑接口
	2. <cpp-toolkit/etcd.h> 中的 SvcWatcher 智能指针 ,  用于服务监控
	3. <cpp-toolkit/etcd.h> 中的 SvcProvider 智能指针 ,  用于服务注册
	4. brpc::Server (使用 <cpp-toolkit/rpc.h> 中的 ServerFactory 构建)
5. 接口 : 
	1. start 启动

## 2. DatabaseServerBuilder 构建器

1. 定义 DatabaseServerBuilder 类 ,负责构建 DatabaseServer 对象
2. 声明和定义放置在 database_server.h , 源文件 database_server.cpp 中
3. 成员如下 : 
    1. MySQL 的配置信息智能指针管理 (<cpp-toolkit/odb.h> 中的 MySQLSettings 对象)
	2. 监听的端口号
	3. 注册中心配置信息 (etcd 地址 , 服务名称 , 服务地址)
	4. DatabaseServer 指针指针 , 用于返回
	5. 要监听的服务名称 (std::vector<std::string>)
	6. 服务通道管理智能指针 (<cpp-toolkit/odb.h> 中的 ChannelManager)
4. 接口 : 
	1. 设置各个成员
	2. build 构建 , 构建流程如下 :  查考 chat-excel/svc_gateway_service/gateway_server.cc 的实现
		1. 创建channel的管理器
		2. 创建业务逻辑层 databaseBusiness
		3. 创建 RPC 接口定义实例 DatabaseServiceImpl
		4. 创建 RPC 服务器
		5. 创建服务发现实例 SvcWatcher (包含 服务上线 和 服务下线的回调)
		6. 开启服务发现
		7. 服务注册
		8. 创建 DatabaseServer 实例


## 3. main 程序主流程

1. 完成gflags参数解析、rpc服务器搭建、启动rpc服务器等，源文件为main.cc，放置在 chat-excel/svc_database_service 目录下
2. 完成对数据库子服务的编译，生成CMakeLists.txt文件，放置在 chat-excel/svc_database_service 目录下

请严格按照上述要求，帮我完成rpc服务器搭建以及构建器实现，并生成main.cc完成服务器构建及启动，生成CMakeLists.txt文件，完成数据库子服务的编译
