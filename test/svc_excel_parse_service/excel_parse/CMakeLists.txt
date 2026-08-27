# =================================================================
#                         Excel 解析子服务单元测试
# =================================================================

# Excel 解析器单元测试可执行文件
add_executable(excel_parse_test
    excel_parse_test.cc
)

# 链接库 :
#   chat_excel_excel_parse_business : 业务逻辑层(传递链接 Excel 解析器/OpenXLSX/proto 生成代码)
#   chat_excel_common               : 异常类与错误码
#   GTest::gtest                    : gtest 测试框架
target_link_libraries(excel_parse_test PRIVATE
    chat_excel_excel_parse_business
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME ExcelParseTest COMMAND excel_parse_test)

# Excel 解析业务层单元测试可执行文件
add_executable(excel_parse_business_test
    excel_parse_business_test.cc
)

# 链接库 :
#   chat_excel_excel_parse_business : 业务逻辑层(传递链接 Excel 解析器/OpenXLSX/proto 生成代码)
#   chat_excel_common               : 异常类与错误码
#   GTest::gtest                    : gtest 测试框架
target_link_libraries(excel_parse_business_test PRIVATE
    chat_excel_excel_parse_business
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME ExcelParseBusinessTest COMMAND excel_parse_business_test)

# Excel 解析 RPC 接口实现层单元测试可执行文件
add_executable(excel_parse_service_impl_test
    excel_parse_service_impl_test.cc
)

# 链接库 :
#   chat_excel_excel_parse_service : RPC 接口实现层(传递链接业务逻辑层/proto 生成代码)
#   chat_excel_common             : 异常类与错误码
#   GTest::gtest                  : gtest 测试框架
target_link_libraries(excel_parse_service_impl_test PRIVATE
    chat_excel_excel_parse_service
    chat_excel_common
    GTest::gtest
    pthread
)

# 注册单元测试
add_test(NAME ExcelParseServiceImplTest COMMAND excel_parse_service_impl_test)
