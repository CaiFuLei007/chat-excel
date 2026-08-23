
- 你是一个资深的 C++ 后端开发工程师 , 对微服务架构有深入了解 , 擅长网关设计和开发

任务 : 搭建本项目的网关服务 , 实现 HTTP 服务器 , HTTP 接口定义(业务功能展示不实现)以及路由绑定 , 并完成 main 函数编写

## 1. 任务拆分

按照一下步骤进行实现 : 
1. 先数据 chat-excel/prompt/gateway_service/api.md 中定义的 HTTP 接口
2. 使用 cpp-httplib 库搭建 HTTP 服务器 , 网关地址和端口可以通过 gflags 参数配置 , gflags 参数配置在 main 中定义
3. 实现 HTTP 接口定义 , 参考 api.md 文档 , 仅实现健康检测接口 , 其他接口只定义不进行实现 , 将 HTTP 接口实现和服务器搭建隔离开 ,在不同类中实现
4. 实现网关服务器构建器类(Builder) , 完成服务器(IP 和 Port端口)以及服务发现组件的构建
5. 实现 main 方法,  完成参数解析 , 日志初始化以及 HTTP 服务器的启动
6. 编写 CMakeLists.txt 文件 , 构建网关代码

## 2. 网关服务器实现要求

1. 仅搭建 HTTP 服务器，不负责业务逻辑，业务逻辑在其他类中实现
2. 使用 cpp-httplib 库搭建，网关地址和端口通过 gflags 参数配置
3. 服务器类名称 GatewayServer，包含在 chat_excel 命名空间中
4. 提供的方法 :  
    1. 提供 start 方法：服务器启动时需检测服务器是否已运行，如果运行则返回，未运行再启动 , 服务器在新线程中启动，防止阻塞主线程
    2. 提供 stop 方法：服务器停止时需检测服务器是否在运行，如果未运行则返回，运行中则停止。stop 方法在服务器对象销毁时自动调用
    3. 各个方法在实现时，必须要添加日志记录，记录程序的执行流程、状态以及异常情况
    4. 在构造方法中完成服务器初始化
5. 头文件名称 gateway_server.h，源文件名称 gateway_server.cc , 放在 chat-excel/svc_gateway_service 目录下


## 3. HTTP 接口实现要求

1. HTTP接口定义和HTTP服务器搭建分离开，在不同类中实现
2. 请严格遵循 chat-excel/prompt/gateway_service/api.md 文件接口定义，完成30个HTTP接口的定义和路由绑定
3. 仅健康检测接口需要实现，其余接口只定义不实现
4. 向外提供路由绑定接口，能让网关服务器在启动前完成路由绑定
5. 需要包含服务发现实例，能让后续各个接口实现时，获取到 RPC 服务器 Channel
6. 类名称 GatewayServiceImpl，包含在 chat_excel 命名空间中
7. 头文件名称 gateway_impl_service.h，源文件名称 gateway_impl_service.cc , 放在 chat-excel/svc_gateway_service 目录下
8. HTTP接口定义和路由绑定，可参考 chat-excel/prompt/gateway_service/api.md 文件 注意务必确保HTTP接口定义符合 api.md 文件接口定义

## 4. 网关服务器构建器类(Builder)实现要求
1. 职责完成网关服务器构建，完成服务器发现组件构建
2. 构建器流程如下：
    1. 构建服务信道管理对象，并设置需要监控的服务
    2. 构造服务发现(监控)对象，定义回调函数，处理服务上线、下线时，服务节点的添加和移除，并打印日志记录
    3. 在独立的线程中启动服务监控
    4. 构建HTTP接口定义对象
    5. 构建 cpp-httplib 的HTTP服务器对象，并设置读写超时时间为5分钟
    6. 绑定路由
    7. 构建网关服务器对象
    8. 启动服务器
    9. 返回网关服务器对象
10. 构建器类名称 GatewayServerBuilder，和网关服务器放在一个文件中实现


## 5. main 方法实现要求
1. main方法的实现流程： 
    1. 解析gflags参数 
    2. 初始化日志记录 
    3. 注册信号处理函数，处理SIGINT和SIGTERM信号 
    4. 配置服务发现，监控用户子服务、文件子服务、数据库子服务、AI子服务的上线、下线事件 
    5. 构建网关服务器对象 
    6. 启动服务器 
    7. 循环检测程序退出信号，服务器停止时退出循环，结束主程序
1. 定义gflags参数，包括:
    1. logger 日志配置
    2. 网关服务器端口 
    3. ETCD地址(eg : http://dev-etcd:2379)
    4. 服务名称：用户子服务-UserSerivce、文件子服务-FileService、数据库子服务-DataBaseService、AI子服务-AIService
2. 定义 SIGINT 和 SIGTERM 信号处理函数，用于处理程序退出信号，程序退出时如果服务器在运行则停止服务器，并打印日志记录
3. CMakeLists.txt实现要求 编写CMakeLists.txt文件，完成网关代码的构建 : 添加可执行文件 - 添加链接库

## 6. 其他要求
1. 日志采用 <cpp-toolkits/logger.h> 头文件封装的 spdlog
2. 服务发现实现使用 <cpp-toolkits/etcd.h> <cpp-toolkits/rpc.h> 头文件封装的 etcd 库和 rpc 库

## 7. 输出要求
1. 所有文件创建在 chat-excel/svc_gateway_service 目录下 , 包含:
    1. gateway_server.h
    2. gateway_server.cc
    3. gateway_impl_service.h
    4. gateway_impl_service.cc
    5. main.cc
    6. CMakeLists.txt
2. 所有代码必须符合项目规则，代码生成完成之后，必须检查是否符合项目规则文档中要求，务必确保符合项目规则约定
3. 无需生成测试代码


请你先严格按照上述要求，理解网关子服务的实现需求，然后给我复述下你的实现思路。在我确保你和我理解一致后，我再告诉你逐步实现。最后，在具体实现代码前，必须先详细阅读下项目规则约定，确保生成的代码符合项目规则。