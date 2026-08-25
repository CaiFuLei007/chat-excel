#include "data/verifycode_data.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/redis.h>
#include <gtest/gtest.h>

using chat_excel::user_service::VerifyCodeData;
using chat_excel::user_service::VerifyCodeInfo;

namespace
{

// 验证码缓存 hash 类型的 key(与 VerifyCodeData 实现保持一致)
constexpr const char* kVerifyCodeCacheKey = "verifycode_data";

// 验证码缓存过期时间(秒), 1 分钟
constexpr int kVerifyCodeCacheExpireTime = 60;

/**
 * @brief 获取必填环境变量的值
 * @param name 环境变量名
 * @return 环境变量的值
 */
std::string GetRequiredEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        GTEST_LOG_(FATAL) << "环境变量 " << name << " 未设置";
    }
    return value;
}

/**
 * @brief 获取 Redis 操作句柄(进程内单例), 配置从环境变量读取
 * @return Redis 操作句柄
 */
std::shared_ptr<sw::redis::Redis>& GetRedisHandle()
{
    static std::shared_ptr<sw::redis::Redis> handle = [] {
        cpp_toolkit::RedisSettings settings;
        settings.host = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PORT"));
        settings.user = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PASSWORD");
        settings.db = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_INDEX"));
        return cpp_toolkit::RedisFactory::Create(settings);
    }();
    return handle;
}

/**
 * @brief 校验实际查询到的验证码信息与期望的验证码信息一致
 * @param actual 实际查询到的验证码信息
 * @param expected 期望的验证码信息
 */
void ExpectVerifyCodeInfoEqual(const VerifyCodeInfo& actual, const VerifyCodeInfo& expected)
{
    EXPECT_EQ(actual.verifycode_id, expected.verifycode_id);
    EXPECT_EQ(actual.verify_code, expected.verify_code);
    EXPECT_EQ(actual.email, expected.email);
    EXPECT_EQ(actual.create_time, expected.create_time);
}

} // namespace

// 验证码数据访问类测试夹具, 每个用例执行前清理缓存中的测试数据
class VerifyCodeDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理缓存中的测试数据, 避免脏数据
        GetRedisHandle()->del(kVerifyCodeCacheKey);

        verifycode_data_ = std::make_unique<VerifyCodeData>(GetRedisHandle());

        // 构造测试验证码信息
        test_verifycode_.verifycode_id = "vcid_for_test";
        test_verifycode_.verify_code = "123456";
        test_verifycode_.email = "test@qq.com";
        test_verifycode_.create_time = "2026-08-25 10:30:00";
    }

    std::unique_ptr<VerifyCodeData> verifycode_data_;
    VerifyCodeInfo test_verifycode_;
};

// 正常情况: 保存验证码后通过验证码 ID 查询, 各字段与保存的数据一致
TEST_F(VerifyCodeDataTest, SaveVerifyCodeThenGetByVerifyCodeId)
{
    verifycode_data_->SaveVerifyCode(test_verifycode_);

    std::optional<VerifyCodeInfo> verifycode_info =
        verifycode_data_->GetVerifyCodeByVerifyCodeId(test_verifycode_.verifycode_id);
    ASSERT_TRUE(verifycode_info.has_value());
    ExpectVerifyCodeInfoEqual(*verifycode_info, test_verifycode_);
}

// 异常情况: 查询不存在的验证码 ID 返回 std::nullopt
TEST_F(VerifyCodeDataTest, GetVerifyCodeReturnNulloptWhenNotExists)
{
    std::optional<VerifyCodeInfo> verifycode_info =
        verifycode_data_->GetVerifyCodeByVerifyCodeId("not_exists_verifycode_id");
    EXPECT_FALSE(verifycode_info.has_value());
}

// 正常情况: 删除验证码后通过验证码 ID 查询返回 std::nullopt
TEST_F(VerifyCodeDataTest, DeleteVerifyCodeThenGetReturnNullopt)
{
    verifycode_data_->SaveVerifyCode(test_verifycode_);
    verifycode_data_->DeleteVerifyCodeByVerifyCodeId(test_verifycode_.verifycode_id);

    std::optional<VerifyCodeInfo> verifycode_info =
        verifycode_data_->GetVerifyCodeByVerifyCodeId(test_verifycode_.verifycode_id);
    EXPECT_FALSE(verifycode_info.has_value());
}

// 边界情况: 保存验证码后缓存 key 的过期时间被设置为 1 分钟以内
TEST_F(VerifyCodeDataTest, SaveVerifyCodeSetExpireTime)
{
    verifycode_data_->SaveVerifyCode(test_verifycode_);

    long long expire_time = GetRedisHandle()->ttl(kVerifyCodeCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kVerifyCodeCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "verifycode_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
