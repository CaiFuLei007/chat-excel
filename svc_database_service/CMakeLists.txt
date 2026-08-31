# =================================================================
#                         查找依赖
# =================================================================

# cpp_toolkit 工具库(QueryResult JSON 序列化依赖 JsonUtil, 关键流程日志依赖 logger)
find_package(cpp_toolkit REQUIRED)

# SQLite3 数据库客户端库(SQLite 驱动依赖)
find_package(SQLite3 REQUIRED)

# MySQL 客户端开发组件(驱动层头文件 mysql/mysql.h 与链接库 libmysqlclient)
find_path(MYSQL_CLIENT_INCLUDE_DIR mysql/mysql.h REQUIRED)
find_library(MYSQL_CLIENT_LIBRARY mysqlclient REQUIRED)

# =================================================================
#                         生成静态库(驱动层)
# =================================================================

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
#   chat_excel_common : 异常类与错误码定义
#   cpp_toolkit       : logger 日志封装与 JsonUtil JSON 序列化封装
#   SQLite::SQLite3   : SQLite 数据库客户端库
#   mysqlclient       : MySQL 数据库客户端库
target_link_libraries(chat_excel_database_driver PUBLIC
    chat_excel_common
    cpp_toolkit::cpp_toolkit
    SQLite::SQLite3
    ${MYSQL_CLIENT_LIBRARY}
)

# =================================================================
#                         生成静态库(连接管理器)
# =================================================================

# 数据库连接管理器静态库 : 连接信息类 + 连接管理器单例
add_library(chat_excel_database_connection STATIC
    connection_manager.cpp
)

# 链接依赖 :
#   chat_excel_database_driver : 驱动抽象接口与工厂, 连接管理器管理的连接实例
target_link_libraries(chat_excel_database_connection PUBLIC
    chat_excel_database_driver
)
