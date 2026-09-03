# =================================================================
#                     AI 子服务 RPC 接口实现层单元测试
# =================================================================

# AI 子服务 RPC 接口实现层单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(ai_service_impl_test
    ai_service_impl_test.cc
)

# 链接库 :
#   chat_excel_ai_service : RPC 接口实现层(传递链接业务逻辑层/proto 生成代码/数据访问层)
#   chat_excel_common     : 异常类与错误码
#   GTest::gtest          : gtest 测试框架
target_link_libraries(ai_service_impl_test PRIVATE
    chat_excel_ai_service
    chat_excel_common
    GTest::gtest
    pthread
)

# 将提示词模板目录拷贝到测试二进制目录,
# AIMessageHandler 构造时按可执行文件工作目录加载模板文件
add_custom_command(TARGET ai_service_impl_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/svc_ai_service/prompt_template
            $<TARGET_FILE_DIR:ai_service_impl_test>/prompt_template
)

# 注册单元测试(可执行文件输出到 bin/test/, 需在该目录下运行以加载提示词模板)
add_test(NAME AiServiceImplTest COMMAND ai_service_impl_test)
set_tests_properties(AiServiceImplTest PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin/test
)
