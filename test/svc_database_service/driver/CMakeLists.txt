# =================================================================
#                         SQL 校验器单元测试
# =================================================================

# SQL 校验器单元测试可执行文件
add_executable(sql_validator_test
    sql_validator_test.cc
)

# 链接库 :
#   chat_excel_database_driver : 数据库驱动层(SQL 校验器与基础结构)
#   GTest::gtest_main          : gtest 测试框架(含 main 函数)
target_link_libraries(sql_validator_test PRIVATE
    chat_excel_database_driver
    GTest::gtest
    GTest::gtest_main
    pthread
)

# 注册单元测试
add_test(NAME SqlValidatorTest COMMAND sql_validator_test)

# =================================================================
#                     数据库基础结构单元测试
# =================================================================

# 数据库基础结构单元测试可执行文件(配置类校验与结果集结构)
add_executable(database_schema_test
    database_schema_test.cc
)

target_link_libraries(database_schema_test PRIVATE
    chat_excel_database_driver
    GTest::gtest
    GTest::gtest_main
    pthread
)

# 注册单元测试
add_test(NAME DatabaseSchemaTest COMMAND database_schema_test)

# =================================================================
#                     MySQL 驱动单元测试
# =================================================================

# MySQL 驱动单元测试可执行文件, 运行前需配置 MYSQL_CHAT_EXCEL_TEST_* 环境变量
add_executable(mysql_database_driver_test
    mysql_database_driver_test.cc
)

# 链接库 :
#   chat_excel_database_driver : 数据库驱动层(含 MySQL 驱动实现)
target_link_libraries(mysql_database_driver_test PRIVATE
    chat_excel_database_driver
    GTest::gtest
    GTest::gtest_main
    pthread
)

# 注册单元测试
add_test(NAME MySQLDatabaseDriverTest COMMAND mysql_database_driver_test)

# =================================================================
#                     SQLite 驱动单元测试
# =================================================================

# SQLite 驱动单元测试可执行文件
add_executable(sqlite_database_driver_test
    sqlite_database_driver_test.cc
)

# 链接库 :
#   chat_excel_database_driver : 数据库驱动层(含 SQLite 驱动实现)
#   GTest::gtest               : gtest 测试框架(测试文件内自定义 main 并初始化日志系统)
target_link_libraries(sqlite_database_driver_test PRIVATE
    chat_excel_database_driver
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME SQLiteDatabaseDriverTest COMMAND sqlite_database_driver_test)
