# =================================================================
#                          邮箱通知子服务
# =================================================================

# libcurl 邮件发送库(本子服务特有依赖)
find_package(CURL REQUIRED)

# 邮箱通知子服务业务逻辑层静态库 : 邮箱发送器(基类/验证码/普通邮件) + 工作线程池 + 通知业务逻辑
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
#   chat_excel_deps : 公共依赖集合(公共模块/cpp_toolkit/pthread 等)
#   CURL::libcurl   : SMTP 邮件发送(本子服务特有依赖)
target_link_libraries(chat_excel_notify_business PUBLIC
    chat_excel_deps
    CURL::libcurl
)

# 邮箱通知子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_notify_service STATIC
    notify_service_impl.cc
)

# 头文件搜索路径 :
#   项目根目录与子服务目录
target_include_directories(chat_excel_notify_service PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 : 业务逻辑层 + 公共依赖集合
target_link_libraries(chat_excel_notify_service PUBLIC
    chat_excel_notify_business
    chat_excel_deps
)

# 邮箱通知子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(notify_service
    notify_server.cc
    main.cc
)

target_include_directories(notify_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 : RPC 接口实现层 + 公共依赖集合
target_link_libraries(notify_service PRIVATE
    chat_excel_notify_service
    chat_excel_deps
)