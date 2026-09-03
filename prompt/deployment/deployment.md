
## 一键编译

你是一位资深的 C++ 后端开发工程师 , 擅长编写 CMakeLists.txt 文件 , 请帮我完成 chat-excel 的一键编译. 目前根目录下已经存在了 CMakeLists.txt 文件 , 各个子服务中也包含独立编译的 CMakeLists.txt 文件.现在我希望对根目录的 CMakeLists.txt 文件进行修改 , 要求如下:
1. 项目根目录下的 CMakeLists.txt 主要负责 : 
    1. 将 chat-excel/common 目录下的代码编译为静态库 , 其他子服务需要使用时直接链接即可.
    2. 将 chat-excel/proto 中的 proto 文件编译为 C++ 代码 , 将生成之后的 C++ 代码编译为静态库 , 其他子服务需要使用时直接链接即可.
    3. odb 映射类让各个子服务独立编译 , odb 文件只是各个子服务自己进行使用的
    4. 将所有子服务的共同需要链接的库移动到总的 CMakeLists.txt 中 , 自己单独链接的库在自己的 CMakeLists.txt 中.
    5. 修改各个子服务的 CMakeLists.txt 文件 , 完成各个子服务的编译.
2. 简化各个子服务的 CMakeLists.txt 文件 .
3. 编译过程中生成的临时文件放在 build 目录下 , 编译生成的可执行程序放在 bin 目录下 , 静态库放在 bin/lib 目录下.

请先仔细阅读当前项目的项目结构以及现在的编译方案 , 熟悉上述需求之后 , 请先罗列出详细的实现规划 , 我看完之后确保和我理解的一致 , 我再告诉你进行实现.


## gflags 参数处理

你是一位资深的后端开发工程师 , 擅长编写 C++ 代码 , 请帮我完成 chat-excel 的 gflags 参数解析问题和服务名称硬编码问题.

1. gflags 参数解析 : 现在各个子服务的 gflags 参数都是在 main.cc 中定义的, 我需要将每个子服务中的 gflags 参数在配置文件 chat_data.conf 中配置 , 将来各个子服务启动时从配置文件中读取 gflags 参数.
2. 服务名称硬编码 : 各个子服务在进行 RPC 调用时 , 需要获取通信信道 , 目前代码中是通过服务名称硬编码的方式获取的 , 你需要帮我修改为通过 gflags 参数获取 , 如果其他子服务中需要使用服务名称 , 使用 gflags 参数.

注意:
1. 相同的配置参数只配置一次 , 比如 : MySQL , Redis , 日志 , Etcd 等
2. 各个自服务名称统一如下 : 
    1. --user_servicc=UserService
    2. --file_service=FileService
    3. --gateway_service=GatewayService
    4. --ai_service=AIService
    5. --excel_parse_service=ExcelParseService
    6. --notifiy_service=NotifyService
3. 服务都包含了端口号 , 服务名称 , 服务器地址等参数 , 不需要考虑冲突问题 ,将来每个自服务都是在独立的容器中运行的 , 端口号 , 服务名称 , 服务器地址等参数都是挡路配置的. 名称统一如下 : 
    1. --listen_port={LISTEN_PORT}
    2. --server_name={SERVER_NAME}
    3. --server_addr={SERVER_ADDR}

严格按照我上述的要求 ,完成 gflags 参数处理