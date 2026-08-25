# =================================================================
#                         查找依赖
# =================================================================

# cpp_toolkit 工具库(UserBusiness 成员 ChannelManager 依赖 rpc.h 封装,
# 已声明 brpc/protobuf/spdlog 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# gflags 命令行参数解析库(main.cpp 直接使用 gflags 符号, 显式声明不依赖传递导入)
find_package(gflags REQUIRED)

# =================================================================
#                         生成静态库(业务逻辑层)
# =================================================================

# 用户子服务业务逻辑层静态库 : 会话管理类 + 用户业务逻辑类实现代码
add_library(chat_excel_user_business STATIC
    session_manager.cpp
    user_business.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持下游以 #include "svc_user_service/common.h" 与
#                #include "data/user_data.h" 方式导入
target_include_directories(chat_excel_user_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_data : 数据访问层(UserData / SessionData / VerifyCodeData),
#                    传递链接 odb / odb-mysql / cpp_toolkit
#   cpp_toolkit     : ChannelManager RPC 信道管理封装
#   crypt           : bcrypt 单向哈希加密(libxcrypt 的 crypt_r 可重入接口)
target_link_libraries(chat_excel_user_business PUBLIC
    chat_excel_data
    cpp_toolkit::cpp_toolkit
    crypt
)

# =================================================================
#                         生成静态库(RPC 接口实现层)
# =================================================================

# 用户子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_user_service STATIC
    user_service_impl.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持下游以 #include "svc_user_service/user_service_impl.h" 方式导入
target_include_directories(chat_excel_user_service PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_user_business : 用户业务逻辑层
#   chat_excel_proto         : protoc 生成的 RPC 接口代码(user_service.pb.h/cc),
#                              传递链接 protobuf 运行时库与头文件搜索路径
#   cpp_toolkit              : brpc::ClosureGuard 与日志封装
target_link_libraries(chat_excel_user_service PUBLIC
    chat_excel_user_business
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)

# =================================================================
#                         生成可执行文件(服务层)
# =================================================================

# 用户子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(user_service
    user_server.cpp
    main.cpp
)

# 头文件搜索路径 : 项目根目录(支持 #include "svc_user_service/user_server.h" 等)
target_include_directories(user_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_user_service : RPC 接口实现层(传递链接 业务逻辑层/数据访问层/proto/cpp_toolkit/crypt)
#   cpp_toolkit            : user_server.cpp/main.cpp 直接使用 ServerFactory/ODBFactory/RedisFactory/logger,
#                            依"不依赖间接导入"规则显式声明
#   gflags                 : main.cpp 直接使用 gflags 符号, 显式声明
target_link_libraries(user_service PRIVATE
    chat_excel_user_service
    cpp_toolkit::cpp_toolkit
    gflags
    pthread
)
