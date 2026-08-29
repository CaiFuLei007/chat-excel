# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(chat_excel CXX)

# C++ 标准版本, 项目基于 C++17 实现, cpp-toolkit 的 util.h(string_view) 要求 C++17 及以上
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译警告选项
add_compile_options(-Wall -Wextra)

# 公共模块编译为静态库, 供各子服务与测试复用
add_library(chat_excel_common STATIC common/exception.cc)

# 以项目根目录作为头文件搜索路径, 支持 #include "common/exception.h" 形式导入
target_include_directories(chat_excel_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 数据实体模块(odb 数据库映射类与生成的数据库支持代码)
add_subdirectory(data)

# proto 接口定义模块(protoc 生成的 RPC 接口代码)
add_subdirectory(proto)

# 用户子服务
add_subdirectory(svc_user_service)

# 网关子服务
add_subdirectory(svc_gateway_service)

# 邮箱通知子服务
add_subdirectory(svc_email_notify_service)

# Excel 解析子服务
add_subdirectory(svc_excel_parse_service)

# 文件子服务
add_subdirectory(svc_file_service)

# 查找 gtest 测试框架
find_package(GTest REQUIRED)

# 启用 ctest 测试(必须在注册测试的 add_test 与子目录添加之前调用)
enable_testing()

# 异常模块单元测试
add_executable(exception_test test/common/exception/exception_test.cc)
target_link_libraries(exception_test PRIVATE chat_excel_common GTest::gtest GTest::gtest_main)

# 注册异常模块单元测试
add_test(NAME ExceptionTest COMMAND exception_test)

# 用户子服务数据访问层单元测试
add_subdirectory(test/svc_user_service/user_data)
add_subdirectory(test/svc_user_service/session_data)
add_subdirectory(test/svc_user_service/verifycode_data)

# 文件子服务数据访问层单元测试
add_subdirectory(test/svc_file_service/file_data)
add_subdirectory(test/svc_file_service/worksheet_data)

# 文件子服务业务逻辑层单元测试
add_subdirectory(test/svc_file_service/file_business)

# 文件子服务 RPC 接口实现层单元测试
add_subdirectory(test/svc_file_service/file_service_impl)

# 用户子服务 RPC 接口实现层单元测试
add_subdirectory(test/svc_user_service/user_service_impl)

# Excel 解析子服务单元测试
add_subdirectory(test/svc_excel_parse_service/excel_parse)
