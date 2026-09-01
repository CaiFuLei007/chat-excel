# =================================================================
#                         AI 子服务聊天会话数据访问类单元测试
# =================================================================

# 聊天会话数据访问类单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(chat_session_data_test
    chat_session_data_test.cc
)

# 链接库 :
#   chat_excel_data   : 数据实体库(传递链接 odb/redis++/cpp_toolkit 与头文件搜索路径)
#   chat_excel_common : 异常类
#   GTest::gtest      : gtest 测试框架
target_link_libraries(chat_session_data_test PRIVATE
    chat_excel_data
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME ChatSessionDataTest COMMAND chat_session_data_test)
