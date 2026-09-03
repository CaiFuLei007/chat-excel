#include "common/service_flags.h"

#include <gtest/gtest.h>

/**
 * @brief 测试各子服务名称 gflags 参数的默认值,
 *        默认值须与各子服务注册到 ETCD 注册中心的服务名称保持一致
 */
TEST(ServiceFlagsTest, DefaultValue)
{
    EXPECT_EQ(FLAGS_user_service, "UserService");
    EXPECT_EQ(FLAGS_file_service, "FileService");
    EXPECT_EQ(FLAGS_gateway_service, "GatewayService");
    EXPECT_EQ(FLAGS_ai_service, "AIService");
    EXPECT_EQ(FLAGS_excel_parse_service, "ExcelParseService");
    EXPECT_EQ(FLAGS_notify_service, "NotifyService");
    EXPECT_EQ(FLAGS_database_service, "DataBaseService");
}
