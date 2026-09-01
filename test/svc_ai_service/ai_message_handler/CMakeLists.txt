# =================================================================
#                   AI 子服务消息处理类单元测试
# =================================================================

# AI 消息处理类单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(ai_message_handler_test
    ai_message_handler_test.cc
)

# 链接库 :
#   chat_excel_ai_service : RPC 接口实现层(提供消息处理类实现, 传递链接业务逻辑层/proto 生成代码)
#   chat_excel_common     : 异常类与错误码
#   GTest::gtest          : gtest 测试框架
target_link_libraries(ai_message_handler_test PRIVATE
    chat_excel_ai_service
    chat_excel_common
    GTest::gtest
    pthread
)

# 将提示词模板目录拷贝到测试二进制目录,
# AIMessageHandler 构造时按可执行文件工作目录加载模板文件
add_custom_command(TARGET ai_message_handler_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/svc_ai_service/prompt_template
            $<TARGET_FILE_DIR:ai_message_handler_test>/prompt_template
)

# 注册单元测试
add_test(NAME AIMessageHandlerTest COMMAND ai_message_handler_test)
