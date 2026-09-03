# =================================================================
#                         Excel 解析子服务
# =================================================================

# OpenXLSX Excel 读写库(本子服务特有依赖)
find_package(OpenXLSX REQUIRED)

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
#   chat_excel_deps     : 公共依赖集合(公共模块/proto/cpp_toolkit 等)
#   OpenXLSX::OpenXLSX  : Excel 文件解析(本子服务特有依赖)
target_link_libraries(chat_excel_excel_parse_business PUBLIC
    chat_excel_deps
    OpenXLSX::OpenXLSX
)

# Excel 解析子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码
add_library(chat_excel_excel_parse_service STATIC
    excel_parse_service_impl.cc
)

# 头文件搜索路径 :
#   项目根目录与子服务目录
target_include_directories(chat_excel_excel_parse_service PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 : 业务逻辑层 + 公共依赖集合
target_link_libraries(chat_excel_excel_parse_service PUBLIC
    chat_excel_excel_parse_business
    chat_excel_deps
)

# Excel 解析子服务可执行文件 : 服务器构建器 + 程序入口
add_executable(excel_parse_service
    excel_parse_server.cc
    main.cc
)

target_include_directories(excel_parse_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接依赖 : RPC 接口实现层 + 公共依赖集合
target_link_libraries(excel_parse_service PRIVATE
    chat_excel_excel_parse_service
    chat_excel_deps
)