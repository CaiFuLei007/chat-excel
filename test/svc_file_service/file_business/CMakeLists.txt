# =================================================================
#                         文件业务逻辑类单元测试
# =================================================================

# 文件业务逻辑类单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志与 FastDFS 客户端)
add_executable(file_business_test
    file_business_test.cc
)

# 链接库 :
#   chat_excel_file_business : 文件业务逻辑层(传递链接 数据访问层/proto/cpp_toolkit)
#   chat_excel_common        : 异常类
#   GTest::gtest             : gtest 测试框架
target_link_libraries(file_business_test PRIVATE
    chat_excel_file_business
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME FileBusinessTest COMMAND file_business_test)
