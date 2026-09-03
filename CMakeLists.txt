# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(chat_excel CXX)

# C++ 标准版本, 项目基于 C++20 实现, cpp-toolkit 的 timerwheel.h(jthread) 要求 C++20 及以上
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译警告选项
add_compile_options(-Wall -Wextra)

# 单元测试开关 : 默认关闭(不参与默认构建), 通过 -DCHAT_EXCEL_ENABLE_TESTING=ON 显式开启
option(chat_excel_ENABLE_TESTING "构建并注册单元测试, 测试产物输出到 bin/test/ 目录" OFF)

# 全局输出目录 :
#   编译产生的临时文件保留在 build 目录(build 目录作为 cmake 构建目录),
#   可执行文件统一输出到 build/bin/ 下,
#   静态库统一输出到 build/bin/lib/ 下,
#   运行时依赖的动态库(.so)也收集到 build/bin/lib/ 下,
#   测试产物输出到 build/bin/test/ 下,
#   便于 Docker 镜像构建时从 build 目录整体取用(可执行文件 + prompt_template + 动态库聚合)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =================================================================
#                       公共模块静态库
# =================================================================

# chat-excel/common 目录编译为静态库, 供各子服务与测试复用
add_library(chat_excel_common STATIC
    common/exception.cc
    common/service_flags.cc
)

# 以项目根目录作为头文件搜索路径, 支持 #include "common/exception.h" 形式导入
target_include_directories(chat_excel_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接依赖 :
#   gflags : service_flags.cc 定义子服务名称 gflags 参数(FlagRegisterer 注册符号)
target_link_libraries(chat_excel_common PUBLIC gflags)

# =================================================================
#                      proto / data 模块
# =================================================================

# proto 接口定义模块(protoc 生成的 RPC 接口代码与静态库 chat_excel_proto)
add_subdirectory(proto)

# 数据实体模块(odb 数据库映射类与数据访问层静态库 chat_excel_data)
add_subdirectory(data)

# =================================================================
#                    各子服务公共依赖集合
# =================================================================

# 各子服务共同需要链接的第三方库统一在根目录查找
find_package(cpp_toolkit REQUIRED)
find_package(gflags REQUIRED)

# 各子服务公共依赖集合 : 将各子服务共同需要链接的库统一收拢在根目录,
# 子服务的 CMakeLists.txt 只需链接该集合加上各自特有的库
add_library(chat_excel_deps INTERFACE)

# 链接依赖 :
#   chat_excel_common : 异常类与错误码定义
#   chat_excel_data   : odb 映射与数据访问(实体/数据访问类/odb 生成代码)
#   chat_excel_proto  : protoc 生成的 RPC 接口代码
#   cpp_toolkit       : 日志/JsonUtil/RPC 信道管理/服务发现等公共封装
#   gflags            : 命令行参数解析
#   pthread           : 多线程依赖
target_link_libraries(chat_excel_deps INTERFACE
    chat_excel_common
    chat_excel_data
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
    gflags
    pthread
)

# =================================================================
#                         各子服务
# =================================================================

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

# 数据库子服务
add_subdirectory(svc_database_service)

# AI 子服务
add_subdirectory(svc_ai_service)

# =================================================================
#                          单元测试
# =================================================================

# 单元测试默认不参与构建, 需显式指定 -DCHAT_EXCEL_ENABLE_TESTING=ON 开启
if(chat_excel_ENABLE_TESTING)

    # 测试可执行文件统一输出到 build/bin/test/ 目录(与可执行程序 build/bin/ 区分)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

    # 查找 gtest 测试框架
    find_package(GTest REQUIRED)

    # 启用 ctest 测试(必须在注册测试的 add_test 与子目录添加之前调用)
    enable_testing()

    # 异常模块单元测试
    add_executable(exception_test test/common/exception/exception_test.cc)
    target_link_libraries(exception_test PRIVATE chat_excel_common GTest::gtest GTest::gtest_main)
    add_test(NAME ExceptionTest COMMAND exception_test)

    # 子服务名称 gflags 参数单元测试
    add_executable(service_flags_test test/common/service_flags/service_flags_test.cc)
    target_link_libraries(service_flags_test PRIVATE chat_excel_common GTest::gtest GTest::gtest_main)
    add_test(NAME ServiceFlagsTest COMMAND service_flags_test)

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

    # 数据库子服务驱动层单元测试
    add_subdirectory(test/svc_database_service/driver)

    # AI 子服务数据访问层单元测试
    add_subdirectory(test/svc_ai_service/chat_session_data)

    # AI 子服务会话管理类单元测试
    add_subdirectory(test/svc_ai_service/chat_session_manager)

    # AI 子服务业务逻辑层单元测试
    add_subdirectory(test/svc_ai_service/ai_business)

    # AI 子服务 RPC 接口实现层单元测试
    add_subdirectory(test/svc_ai_service/ai_service_impl)

    # AI 子服务消息处理类单元测试
    add_subdirectory(test/svc_ai_service/ai_message_handler)

    # AI 子服务提示词模板类单元测试
    add_subdirectory(test/svc_ai_service/prompt_template)

endif()

# =================================================================
#       运行时动态库收集(供 Docker 镜像构建聚合取用)
# =================================================================

# 将所有子服务可执行文件运行期依赖的动态库(.so)拷贝到 build/bin/lib/,
# 与静态库(build/bin/lib)/可执行文件(build/bin)/提示词模板(build/bin/prompt_template)
# 在 build/bin 下聚合, Docker 镜像构建时从 build 目录整体取用, 无需额外脚本。
# 基础系统库由基础镜像(ubuntu:24.04)提供, 无需打入。

# 参与收集的可执行文件目标名(其产物位于 build/bin/, 由 ldd 探测运行时依赖)
set(CHAT_EXCEL_RUNTIME_TARGETS
    user_service notify_service gateway_service excel_parse_service
    file_service database_service ai_service
)

# 运行时动态库收集: 将收集脚本生成到 build 目录(构建产物, 不占用源码, 不新增源码脚本文件)。
# 在全部可执行文件链接完成后, 将各服务运行期依赖的 .so 拷贝到 build/bin/lib,
# 与可执行文件(build/bin)/prompt_template(build/bin/prompt_template)/静态库聚合, 供 Docker 从 build 取用。
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/collect_runtime_libs.sh CONTENT [=[
#!/usr/bin/env bash
# 收集 chat-excel 各子服务运行期依赖的动态库到 build/bin/lib
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}"
OUTPUT_DIR="${ROOT_DIR}/bin/lib"
mkdir -p "${OUTPUT_DIR}"
BINARIES=(user_service notify_service gateway_service excel_parse_service file_service database_service ai_service)
for binary in "${BINARIES[@]}"; do
    ldd "${ROOT_DIR}/bin/${binary}" 2>/dev/null || true
done | grep -oE '/[^[:space:]]+\.so[^[:space:]]*' \
     | grep -vE '^/(lib(64|/x86_64-linux-gnu)?/(libc\.|ld-linux|libstdc\+\+|libgcc_s|libm\.|libpthread|libdl\.)|linux-vdso|libcrypt\.1)' \
     | grep -vE '^/home/' \
     | sort -u \
     | while IFS= read -r dependency_path; do
         [ -f "${dependency_path}" ] || continue
         cp -aLf "${dependency_path}" "${OUTPUT_DIR}/"
     done
]=])
add_custom_target(chat_excel_collect_runtime_libs ALL
    COMMAND bash ${CMAKE_BINARY_DIR}/collect_runtime_libs.sh
    COMMENT "收集运行时动态库到 ${CMAKE_BINARY_DIR}/bin/lib"
)

# 确保收集动作在全部可执行文件链接完成之后执行
foreach(runtime_target IN LISTS CHAT_EXCEL_RUNTIME_TARGETS)
    add_dependencies(chat_excel_collect_runtime_libs ${runtime_target})
endforeach()