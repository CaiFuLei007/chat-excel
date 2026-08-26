# =================================================================
#                         查找依赖
# =================================================================

# cpp_toolkit 工具库(NotifyBusiness 依赖 logger 封装, ServerFactory 依赖 rpc.h 封装,
# 已声明 brpc/protobuf/spdlog 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# gflags 命令行参数解析库(main.cc 直接使用 gflags 符号, 显式声明不依赖传递导入)
find_package(gflags REQUIRED)

# libcurl 邮件发送库
find_package(CURL REQUIRED)

# =================================================================
#                         生成静态库(业务逻辑层)
# =================================================================

# 通知子服务业务逻辑层静态库 : 邮箱发送器(基类/验证码/普通邮件) + 工作线程池 + 通知业务逻辑
add_library(chat_excel_notify_business STATIC
    email_sender.cc
    verify_code_email_sender.cc
    normal_email_sender.cc
    email_workers.cc
    notify_business.cc
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "common/exception.h" 方式导入
#   子服务目录 : 支持以 #include "email_sender.h" 方式直接导入本子服务头文件
target_include_directories(chat_excel_notify_business PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_common : 异常类与错误码定义
#   CURL::libcurl     : SMTP 邮件发送
#   cpp_toolkit       : logger 日志封装
#   pthread           : 工作线程池 std::thread 依赖
target_link_libraries(chat_excel_notify_business PUBLIC
    chat_excel_common
    CURL::libcurl
    cpp_toolkit::cpp_toolkit
    pthread
)

# =================================================================
#                         生成静态库(RPC 接口实现层)
# =================================================================

# 通知子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_notify_service STATIC
    notify_service_impl.cc
)

# 头文件搜索路径 :
#   子服务目录 : 支持以 #include "notify_service_impl.h" 方式直接导入本子服务头文件
#   项目根目录 : 支持以 #include "common/exception.h" 方式导入
target_include_directories(chat_excel_notify_service PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_notify_business : 通知业务逻辑层
#   chat_excel_proto          : protoc 生成的 RPC 接口代码(notify_service.pb.h/cc),
#                               传递链接 protobuf 运行时库与头文件搜索路径
#   cpp_toolkit               : brpc::ClosureGuard 与日志封装
target_link_libraries(chat_excel_notify_service PUBLIC
    chat_excel_notify_business
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)

# =================================================================
#                         生成可执行文件(服务层)
# =================================================================

# 通知子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(notify_service
    notify_server.cc
    main.cc
)

# 头文件搜索路径 : 子服务目录(支持 #include "notify_server.h" 等本子服务头文件直接导入)
target_include_directories(notify_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_notify_service : RPC 接口实现层(传递链接 业务逻辑层/proto/cpp_toolkit/libcurl)
#   cpp_toolkit              : notify_server.cc/main.cc 直接使用 ServerFactory/logger,
#                              依"不依赖间接导入"规则显式声明
#   gflags                   : main.cc 直接使用 gflags 符号, 显式声明
target_link_libraries(notify_service PRIVATE
    chat_excel_notify_service
    cpp_toolkit::cpp_toolkit
    gflags
)
