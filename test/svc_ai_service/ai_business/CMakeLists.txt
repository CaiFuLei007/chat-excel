# =================================================================
#                     AI 子服务业务逻辑类单元测试
# =================================================================

# AI 业务逻辑类单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(ai_business_test
    ai_business_test.cc
)

# 链接库 :
#   chat_excel_ai_business : 业务逻辑库(传递链接 chat_excel_data/aichat_sdk 与头文件搜索路径)
#   chat_excel_common      : 异常类
#   GTest::gtest           : gtest 测试框架
target_link_libraries(ai_business_test PRIVATE
    chat_excel_ai_business
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME AiBusinessTest COMMAND ai_business_test)
