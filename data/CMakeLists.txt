# =================================================================
#                         查找依赖
# =================================================================

# odb 编译器(ORM), 用于编译数据库映射类, 生成 MySQL 数据库支持代码与建表 SQL
find_program(ODB_COMPILER odb)
if(NOT ODB_COMPILER)
    message(FATAL_ERROR "未找到 odb 编译器, 请先安装 odb")
endif()

# cpp-toolkit 工具库(数据访问类使用其中的 JsonUtil 序列化, Redis/日志封装,
# 已声明 redis++/jsoncpp/spdlog 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# =================================================================
#                         odb 映射类编译
# =================================================================

# 数据库映射类头文件
set(ENTITY_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/user_entity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/session_entity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/file_entity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/work_sheet_entity.h
    ${CMAKE_CURRENT_SOURCE_DIR}/chat_session_entity.h
)

# odb 生成代码输出目录(数据库支持代码)与建表 SQL 输出目录
set(ODB_CODE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/odb)
set(ODB_SQL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sql)
file(MAKE_DIRECTORY ${ODB_CODE_DIR} ${ODB_SQL_DIR})

# odb 编译后生成的数据库支持代码文件
set(ODB_CODE_FILES
    ${ODB_CODE_DIR}/user_entity-odb.hxx
    ${ODB_CODE_DIR}/user_entity-odb.ixx
    ${ODB_CODE_DIR}/user_entity-odb.cxx
    ${ODB_CODE_DIR}/session_entity-odb.hxx
    ${ODB_CODE_DIR}/session_entity-odb.ixx
    ${ODB_CODE_DIR}/session_entity-odb.cxx
    ${ODB_CODE_DIR}/file_entity-odb.hxx
    ${ODB_CODE_DIR}/file_entity-odb.ixx
    ${ODB_CODE_DIR}/file_entity-odb.cxx
    ${ODB_CODE_DIR}/work_sheet_entity-odb.hxx
    ${ODB_CODE_DIR}/work_sheet_entity-odb.ixx
    ${ODB_CODE_DIR}/work_sheet_entity-odb.cxx
    ${ODB_CODE_DIR}/chat_session_entity-odb.hxx
    ${ODB_CODE_DIR}/chat_session_entity-odb.ixx
    ${ODB_CODE_DIR}/chat_session_entity-odb.cxx
)

# odb 编译后生成的建表 SQL 文件
set(ODB_SQL_FILES
    ${ODB_SQL_DIR}/user_entity.sql
    ${ODB_SQL_DIR}/session_entity.sql
    ${ODB_SQL_DIR}/file_entity.sql
    ${ODB_SQL_DIR}/work_sheet_entity.sql
    ${ODB_SQL_DIR}/chat_session_entity.sql
)

# 运行 odb 编译器编译映射类, 选项说明 :
#   --database mysql   生成 MySQL 数据库支持代码
#   --std c++17        按 C++17 标准解析映射类
#   --generate-query   生成查询支持代码(odb::query 条件查询)
#   --generate-schema  生成建表 DDL, 默认输出 .sql 文件
# 代码文件与 SQL 文件先生成到 odb 目录, 随后将 SQL 文件移动到 sql 目录
add_custom_command(
    OUTPUT ${ODB_CODE_FILES} ${ODB_SQL_FILES}
    COMMAND ${ODB_COMPILER}
        --database mysql
        --std c++17
        --generate-query
        --generate-schema
        --output-dir ${ODB_CODE_DIR}
        ${ENTITY_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E rename
        ${ODB_CODE_DIR}/user_entity.sql ${ODB_SQL_DIR}/user_entity.sql
    COMMAND ${CMAKE_COMMAND} -E rename
        ${ODB_CODE_DIR}/session_entity.sql ${ODB_SQL_DIR}/session_entity.sql
    COMMAND ${CMAKE_COMMAND} -E rename
        ${ODB_CODE_DIR}/file_entity.sql ${ODB_SQL_DIR}/file_entity.sql
    COMMAND ${CMAKE_COMMAND} -E rename
        ${ODB_CODE_DIR}/work_sheet_entity.sql ${ODB_SQL_DIR}/work_sheet_entity.sql
    COMMAND ${CMAKE_COMMAND} -E rename
        ${ODB_CODE_DIR}/chat_session_entity.sql ${ODB_SQL_DIR}/chat_session_entity.sql
    DEPENDS ${ENTITY_HEADERS}
    COMMENT "正在使用 odb 编译数据库映射类(user_entity.h / session_entity.h / file_entity.h / work_sheet_entity.h / chat_session_entity.h)"
    VERBATIM
)

# =================================================================
#                         生成静态库
# =================================================================

# 数据实体静态库 : 映射类实现代码 + 数据访问类实现代码 + odb 生成的数据库支持代码
add_library(chat_excel_data STATIC
    user_entity.cc
    session_entity.cc
    file_entity.cc
    work_sheet_entity.cc
    chat_session_entity.cc
    user_data.cpp
    session_data.cpp
    verifycode_data.cpp
    file_data.cpp
    worksheet_data.cpp
    chat_session_data.cpp
    ${ODB_CODE_DIR}/user_entity-odb.cxx
    ${ODB_CODE_DIR}/session_entity-odb.cxx
    ${ODB_CODE_DIR}/file_entity-odb.cxx
    ${ODB_CODE_DIR}/work_sheet_entity-odb.cxx
    ${ODB_CODE_DIR}/chat_session_entity-odb.cxx
)

# 头文件搜索路径 :
#   项目根目录 : 支持下游以 #include "data/user_entity.h" 与
#                #include "svc_user_service/common.h" 方式导入
#   data 目录  : odb 生成代码以 #include "user_entity.h" 方式引用映射类头文件
target_include_directories(chat_excel_data PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# odb 运行库(静态库构建期不解析符号, 由下游可执行程序链接时使用) :
#   odb              : odb 核心运行库
#   odb-mysql        : odb MySQL 数据库运行库
#   chat_excel_common : 异常类 ChatExcelException(数据访问层抛出异常, 传递链接至下游)
#   cpp_toolkit      : JsonUtil 序列化与 Redis/日志封装(传递链接 redis++/jsoncpp/spdlog)
target_link_libraries(chat_excel_data PUBLIC
    odb
    odb-mysql
    chat_excel_common
    cpp_toolkit::cpp_toolkit
)
