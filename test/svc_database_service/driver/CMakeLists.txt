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
