# =================================================================
#                         查找依赖
# =================================================================

# cpp_toolkit 工具库(ExcelParseBusiness 依赖 logger 封装, ServerFactory 依赖 rpc.h 封装,
# 已声明 brpc/protobuf/spdlog 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# gflags 命令行参数解析库(main.cc 直接使用 gflags 符号, 显式声明不依赖传递导入)
find_package(gflags REQUIRED)

# OpenXLSX Excel 读写库(Excel 解析器底层依赖)
find_package(OpenXLSX REQUIRED)

# =================================================================
#                         生成静态库(业务逻辑层)
# =================================================================

# Excel 解析子服务业务逻辑层静态库 : Excel 解析器(OpenXLSX 封装) + Excel 解析业务逻辑
add_library(chat_excel_excel_parse_business STATIC
    excel_parse.cc
    excel_parse_business.cc
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "common/exception.h" 方式导入
#   子服务目录 : 支持以 #include "excel_parse.h" 方式直接导入本子服务头文件
target_include_directories(chat_excel_excel_parse_business PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_common : 异常类与错误码定义
#   OpenXLSX::OpenXLSX : Excel 文件解析
#   chat_excel_proto  : protoc 生成的数据结构(excel_parse_service.pb.h), 业务层组织 RPC 结构化数据
#   cpp_toolkit        : logger 日志封装
target_link_libraries(chat_excel_excel_parse_business PUBLIC
    chat_excel_common
    OpenXLSX::OpenXLSX
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)

# =================================================================
#                         生成静态库(RPC 接口实现层)
# =================================================================

# Excel 解析子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_excel_parse_service STATIC
    excel_parse_service_impl.cc
)

# 头文件搜索路径 :
#   子服务目录 : 支持以 #include "excel_parse_service_impl.h" 方式直接导入本子服务头文件
#   项目根目录 : 支持以 #include "common/exception.h" 方式导入
target_include_directories(chat_excel_excel_parse_service PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_excel_parse_business : Excel 解析业务逻辑层
#   chat_excel_proto               : protoc 生成的 RPC 接口代码(excel_parse_service.pb.h/cc),
#                                    传递链接 protobuf 运行时库与头文件搜索路径
#   cpp_toolkit                    : brpc::ClosureGuard 与日志封装
target_link_libraries(chat_excel_excel_parse_service PUBLIC
    chat_excel_excel_parse_business
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)

# =================================================================
#                         生成可执行文件(服务层)
# =================================================================

# Excel 解析子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(excel_parse_service
    excel_parse_server.cc
    main.cc
)

# 头文件搜索路径 : 子服务目录(支持 #include "excel_parse_server.h" 等本子服务头文件直接导入)
target_include_directories(excel_parse_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_excel_parse_service : RPC 接口实现层(传递链接 业务逻辑层/proto/cpp_toolkit/OpenXLSX)
#   cpp_toolkit                   : excel_parse_server.cc/main.cc 直接使用 ServerFactory/logger,
#                                   依"不依赖间接导入"规则显式声明
#   gflags                        : main.cc 直接使用 gflags 符号, 显式声明
target_link_libraries(excel_parse_service PRIVATE
    chat_excel_excel_parse_service
    cpp_toolkit::cpp_toolkit
    gflags
)
