# =================================================================
#                         AI 子服务业务逻辑层
# =================================================================

# aichat_sdk ChatSDK 库(通过安装的包配置导入, 自动携带头文件搜索路径
# 与 SQLite3 传递依赖, 公共头文件 data_manager.h 包含 <sqlite3.h>)
find_package(aichat_sdk REQUIRED)

# cpp_toolkit 工具库(RPC 接口实现层使用 brpc::ClosureGuard 与日志封装)
find_package(cpp_toolkit REQUIRED)

# gflags 命令行参数解析库(main.cc 直接使用 gflags 符号, 显式声明不依赖传递导入)
find_package(gflags REQUIRED)

# AI 子服务业务逻辑静态库 : 聊天会话管理类 + AI 业务逻辑类实现代码
add_library(chat_excel_ai_business STATIC
    chat_session_manager.cpp
    ai_business.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "svc_ai_service/xxx.h" 方式导入
target_include_directories(chat_excel_ai_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接库 :
#   chat_excel_data       : 数据实体库(传递链接 odb/redis++/cpp_toolkit 与头文件搜索路径)
#   aichat_sdk::aichat_sdk: ChatSDK 库, 用于会话与消息管理
target_link_libraries(chat_excel_ai_business PUBLIC
    chat_excel_data
    aichat_sdk::aichat_sdk
)

# =================================================================
#                         AI 子服务 RPC 接口实现层
# =================================================================

# AI 子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_ai_service STATIC
    ai_service_impl.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "svc_ai_service/ai_service_impl.h" 方式导入
target_include_directories(chat_excel_ai_service PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_ai_business : AI 业务逻辑层
#   chat_excel_proto       : protoc 生成的 RPC 接口代码(ai_service.pb.h/cc),
#                            传递链接 protobuf 运行时库与头文件搜索路径
#   cpp_toolkit            : brpc::ClosureGuard 与日志封装
target_link_libraries(chat_excel_ai_service PUBLIC
    chat_excel_ai_business
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)

# =================================================================
#                         AI 子服务服务层(可执行文件)
# =================================================================

# AI 子服务可执行文件 : RPC 服务器/服务器构建器 + 程序入口
add_executable(ai_service
    ai_server.cpp
    main.cc
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "svc_ai_service/ai_server.h" 方式导入
target_include_directories(ai_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_ai_service : RPC 接口实现层(传递链接 业务逻辑层/数据访问层/proto/cpp_toolkit)
#   aichat_sdk::aichat_sdk: ChatSDK 库, ai_server_builder.cpp 直接使用 AIChatSdk 注册模型
#   cpp_toolkit           : ai_server.cpp/ai_server_builder.cpp/main.cc 直接使用
#                           ServerFactory/SvcWatcher/SvcProvider/logger/JsonUtil,
#                           依"不依赖间接导入"规则显式声明
#   gflags                : main.cc 直接使用 gflags 符号, 显式声明
#   pthread               : 服务监控线程使用 std::thread
target_link_libraries(ai_service PRIVATE
    chat_excel_ai_service
    aichat_sdk::aichat_sdk
    cpp_toolkit::cpp_toolkit
    gflags
    pthread
)
