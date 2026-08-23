# =================================================================
#                         查找依赖
# =================================================================

# cpp-toolkit 工具库(日志、服务发现、RPC 信道管理, 已声明 etcd/brpc 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# gflags 命令行参数解析库(main.cc 直接使用 gflags 符号, 显式声明不依赖传递导入)
find_package(gflags REQUIRED)

# =================================================================
#                         网关子服务
# =================================================================

# 网关服务可执行文件
add_executable(gateway_service
    gateway_server.cc
    gateway_impl_service.cc
    main.cc
)

# 头文件搜索路径 : 项目根目录(公共模块头文件)
target_include_directories(gateway_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接库 : cpp_toolkit(静态库, 传递链接 etcd-cpp-api/brpc/leveldb/fdfsclient/spdlog/jsoncpp 等底层依赖)
#          gflags 由 main.cc 直接使用, 显式声明链接不依赖传递导入
target_link_libraries(gateway_service PRIVATE
    cpp_toolkit::cpp_toolkit
    gflags
    pthread
)
