# =================================================================
#                   AI 子服务提示词模板类单元测试
# =================================================================

# 提示词模板类单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(prompt_template_test
    prompt_template_test.cc
)

# 链接库 :
#   chat_excel_ai_business : 业务逻辑库(提供提示词模板类实现)
#   chat_excel_common      : 异常类
#   GTest::gtest           : gtest 测试框架
target_link_libraries(prompt_template_test PRIVATE
    chat_excel_ai_business
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME PromptTemplateTest COMMAND prompt_template_test)
