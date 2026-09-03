# =================================================================
#                          用户子服务
# =================================================================

# 用户子服务业务逻辑层静态库 : 会话管理类 + 用户业务逻辑类实现代码
add_library(chat_excel_user_business STATIC
    session_manager.cpp
    user_business.cpp
)

# 头文件搜索路径 : 项目根目录(支持 #include "svc_user_service/common.h" 等)
target_include_directories(chat_excel_user_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_deps : 公共依赖集合(数据访问层/公共模块/proto/cpp_toolkit 等)
#   crypt           : 用户子服务特有依赖, bcrypt 单向哈希加密(libxcrypt 的 crypt_r 可重入接口)
target_link_libraries(chat_excel_user_business PUBLIC
    chat_excel_deps
    crypt
)

# 用户子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_user_service STATIC
    user_service_impl.cpp
)

# 头文件搜索路径 : 项目根目录
target_include_directories(chat_excel_user_service PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : 业务逻辑层 + 公共依赖集合
target_link_libraries(chat_excel_user_service PUBLIC
    chat_excel_user_business
    chat_excel_deps
)

# 用户子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(user_service
    user_server.cpp
    main.cpp
)

target_include_directories(user_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : RPC 接口实现层 + 公共依赖集合
target_link_libraries(user_service PRIVATE
    chat_excel_user_service
    chat_excel_deps
)