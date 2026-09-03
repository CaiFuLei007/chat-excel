# =================================================================
#                          网关子服务
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

# 链接依赖 :
#   chat_excel_deps : 公共依赖集合(proto 生成的服务桩代码/公共模块/cpp_toolkit/gflags/pthread)
#                    网关子服务无特有第三方库, 仅链接公共依赖集合即可
target_link_libraries(gateway_service PRIVATE
    chat_excel_deps
)