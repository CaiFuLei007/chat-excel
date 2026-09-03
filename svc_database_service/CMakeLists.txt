# =================================================================
#                          数据库子服务
# =================================================================

# 本子服务特有依赖 : SQLite 与 MySQL 数据库客户端库
find_package(SQLite3 REQUIRED)
find_path(MYSQL_CLIENT_INCLUDE_DIR mysql/mysql.h REQUIRED)
find_library(MYSQL_CLIENT_LIBRARY mysqlclient REQUIRED)

# 数据库子服务驱动层静态库 : 公共结构定义 + SQL 校验器 +
# 驱动抽象接口与工厂 + MySQL 驱动 + SQLite 驱动
add_library(chat_excel_database_driver STATIC
    driver/database_schema.cpp
    driver/sql_validator.cpp
    driver/database_driver.cpp
    driver/mysql_database_driver.cpp
    driver/sqlite_database_driver.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "common/exception.h" 方式导入
#   子服务目录 : 支持以 #include "svc_database_service/driver/xxx.h" 方式导入
#   MySQL 目录 : 支持以 #include <mysql/mysql.h> 方式导入
target_include_directories(chat_excel_database_driver PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MYSQL_CLIENT_INCLUDE_DIR}
)

# 链接依赖 :
#   chat_excel_deps      : 公共依赖集合(公共模块/cpp_toolkit/proto 等)
#   SQLite::SQLite3      : SQLite 数据库客户端库(本子服务特有依赖)
#   mysqlclient          : MySQL 数据库客户端库(本子服务特有依赖)
target_link_libraries(chat_excel_database_driver PUBLIC
    chat_excel_deps
    SQLite::SQLite3
    ${MYSQL_CLIENT_LIBRARY}
)

# 数据库连接管理器静态库 : 连接信息类 + 连接管理器单例
add_library(chat_excel_database_connection STATIC
    connection_manager.cpp
)

target_link_libraries(chat_excel_database_connection PUBLIC
    chat_excel_database_driver
)

# 数据库子服务业务逻辑层静态库 : 数据库业务逻辑类
add_library(chat_excel_database_business STATIC
    database_business.cpp
)

target_include_directories(chat_excel_database_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(chat_excel_database_business PUBLIC
    chat_excel_database_connection
    chat_excel_deps
)

# 数据库子服务 RPC 接口实现层静态库 : DatabaseServiceImpl
add_library(chat_excel_database_service_impl STATIC
    database_service_impl.cpp
)

target_include_directories(chat_excel_database_service_impl PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(chat_excel_database_service_impl PUBLIC
    chat_excel_database_business
)

# 数据库子服务可执行文件 : 服务器构建器 + 程序入口
# 驱动实现源文件一并编译进可执行文件 : MySQL/SQLite 驱动通过文件级静态变量
# 自注册到驱动工厂, 静态库链接时未引用的目标文件会被丢弃导致自注册不执行,
# 直接编译进可执行文件保证驱动注册初始化必然运行
add_executable(database_service
    database_server.cpp
    main.cc
    driver/mysql_database_driver.cpp
    driver/sqlite_database_driver.cpp
)

target_include_directories(database_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 : RPC 接口实现层 + 公共依赖集合
target_link_libraries(database_service PRIVATE
    chat_excel_database_service_impl
    chat_excel_deps
)