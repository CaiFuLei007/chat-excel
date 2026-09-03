# =================================================================
#                          文件子服务
# =================================================================

# 文件子服务业务逻辑层静态库 : 文件业务逻辑类实现代码
add_library(chat_excel_file_business STATIC
    file_business.cpp
)

# 头文件搜索路径 : 项目根目录(支持 #include "svc_file_service/file_business.h" 等)
target_include_directories(chat_excel_file_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : 公共依赖集合(数据访问层/proto/cpp_toolkit 等), 文件子服务无特有第三方库
target_link_libraries(chat_excel_file_business PUBLIC
    chat_excel_deps
)

# 文件子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_file_service STATIC
    file_service_impl.cpp
)

# 头文件搜索路径 : 项目根目录
target_include_directories(chat_excel_file_service PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : 业务逻辑层 + 公共依赖集合
target_link_libraries(chat_excel_file_service PUBLIC
    chat_excel_file_business
    chat_excel_deps
)

# 文件子服务可执行文件 : RPC 服务器/服务器构建器 + 程序入口
add_executable(file_service
    file_server.cc
    main.cc
)

target_include_directories(file_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : RPC 接口实现层 + 公共依赖集合
target_link_libraries(file_service PRIVATE
    chat_excel_file_service
    chat_excel_deps
)