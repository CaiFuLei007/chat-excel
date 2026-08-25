# =================================================================
#                         用户子服务 RPC 接口实现层单元测试
# =================================================================

# 用户子服务 RPC 接口实现层单元测试可执行文件(main 函数在测试文件中实现, 用于初始化日志)
add_executable(user_service_impl_test
    user_service_impl_test.cc
)

# 链接库 :
#   chat_excel_user_service : RPC 接口实现层(传递链接业务逻辑层/proto 生成代码/数据访问层)
#   chat_excel_common       : 异常类与错误码
#   GTest::gtest            : gtest 测试框架
target_link_libraries(user_service_impl_test PRIVATE
    chat_excel_user_service
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME UserServiceImplTest COMMAND user_service_impl_test)
