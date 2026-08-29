
# 文件子服务所有 RPC 接口实现

**系统身份** : 你是一个资深的 C++ 开发工程师 , 熟悉后端的 RPC 接口实现

**任务** : 完成文件子服务的所有 RPC 接口实现

## 1. RPC 接口

1. RPC 接口的定义在 chat-excel/proto/file_service.proto 
2. 在 chat-excel/proto/ 中添加 CMakeLists.txt 文件用于编译 proto 文件, 将生成的头文件和源文件存放到 chat-excel/proto/proto_code 中


## 2. FileServiceImpl

1. 定义 FileServiceImpl 类 , 包含在 chat_excel::file_service 命名空间中 , 负责实现文件相关的 RPC 接口
2. 头文件 file_service_impl.h , 源文件 file_service_impl.cpp , 放在 chat-excel/svc_file_service 路径下
3. 成员 : 包含一个 FileBussiness 智能指针 , 用于调用业务逻辑处理接口 , FileBusiness 示例在外部构建 , 构造函数存入智能指针
4. 定义所有 RPC 接口 , 步骤如下
	1. 定义 brpc::Closureguard 对象 , 用于管理 rpc 响应的内存生命周期
	2. 参数解析 : 根据接口定义 , 解析 RPC 请求中的参数 , 并进行参数校验
	3. 调用业务逻辑层的接口 , 完成业务逻辑的实现
	4. 检查接口调用的返回值 , 如果失败 设置对应的错误码和错误码描述 , 如果成功设置 ErrorCode::SUCCESS , 无需添加成功的描述信息
	5. 在业务处理过程中 , 一旦抛出异常 , 统一按照业务处理失败的逻辑进行处理
5. 所有的rpc接口实现函数,必须按照以上步骤实现

请仔细阅读业务逻辑层实现、rpc接口定义的要求，严格按照上述要求，帮我完成所有的rpc接口实现。