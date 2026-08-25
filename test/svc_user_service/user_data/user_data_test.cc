#include "data/user_data.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <gtest/gtest.h>
#include "common/exception.h"

using chat_excel::ChatExcelException;
using chat_excel::UserStatus;
using chat_excel::user_service::UserData;
using chat_excel::user_service::UserInfo;

namespace
{

// 用户缓存 hash 类型的 key(与 UserData 实现保持一致)
constexpr const char* kUserCacheKey = "user_data";

// 用户缓存过期时间(秒), 1 小时
constexpr int kUserCacheExpireTime = 60 * 60;

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
 * @brief 获取 MySQL 操作句柄(进程内单例), 配置从环境变量读取
 * @return MySQL 操作句柄
 */
std::shared_ptr<odb::database>& GetMysqlHandle()
{
    static std::shared_ptr<odb::database> handle = [] {
        cpp_toolkit::mysql_settings settings;
        settings.database = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_DATABASE");
        settings.user = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PASSWORD");
        settings.host = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoul(GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PORT"));
        settings.charset = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_CHARSET");
        return cpp_toolkit::ODBFactory::Create(settings);
    }();
    return handle;
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
 * @brief 校验实际查询到的用户信息与期望的用户信息一致
 * @param actual 实际查询到的用户信息
 * @param expected 期望的用户信息
 */
void ExpectUserInfoEqual(const UserInfo& actual, const UserInfo& expected)
{
    EXPECT_EQ(actual.user_id, expected.user_id);
    EXPECT_EQ(actual.nickname, expected.nickname);
    EXPECT_EQ(actual.email, expected.email);
    EXPECT_EQ(actual.password, expected.password);
    EXPECT_EQ(actual.status, expected.status);
}

} // namespace

// 用户数据访问类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据
class UserDataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_user");
        transaction.commit();
        GetRedisHandle()->del(kUserCacheKey);

        user_data_ = std::make_unique<UserData>(GetMysqlHandle(), GetRedisHandle());

        // 构造测试用户信息, 字段长度受表结构 VARCHAR(32) 限制
        test_user_.user_id = "uid_for_test";
        test_user_.nickname = "nick_for_test";
        test_user_.email = "test@qq.com";
        test_user_.password = "pwd_for_test";
        test_user_.status = UserStatus::NOT_LOGGED_IN;
    }

    std::unique_ptr<UserData> user_data_;
    UserInfo test_user_;
};

// ==================== MySQL 操作测试 ====================

// 正常情况: 保存用户后通过用户 ID 查询, 各字段与保存的数据一致
TEST_F(UserDataTest, SaveUserThenGetByUserId)
{
    user_data_->SaveUser(test_user_);

    std::optional<UserInfo> user_info = user_data_->GetUserByUserId(test_user_.user_id);
    ASSERT_TRUE(user_info.has_value());
    ExpectUserInfoEqual(*user_info, test_user_);
}

// 正常情况: 保存用户后通过邮箱查询, 各字段与保存的数据一致
TEST_F(UserDataTest, SaveUserThenGetByEmail)
{
    user_data_->SaveUser(test_user_);

    std::optional<UserInfo> user_info = user_data_->GetUserByEmail(test_user_.email);
    ASSERT_TRUE(user_info.has_value());
    ExpectUserInfoEqual(*user_info, test_user_);
}

// 正常情况: 保存用户后通过昵称查询, 各字段与保存的数据一致
TEST_F(UserDataTest, SaveUserThenGetByNickname)
{
    user_data_->SaveUser(test_user_);

    std::optional<UserInfo> user_info = user_data_->GetUserByNickname(test_user_.nickname);
    ASSERT_TRUE(user_info.has_value());
    ExpectUserInfoEqual(*user_info, test_user_);
}

// 异常情况: 查询不存在的用户 ID 返回 std::nullopt
TEST_F(UserDataTest, GetUserByUserIdReturnNulloptWhenNotExists)
{
    std::optional<UserInfo> user_info = user_data_->GetUserByUserId("not_exists_user_id");
    EXPECT_FALSE(user_info.has_value());
}

// 异常情况: 查询不存在的用户邮箱返回 std::nullopt
TEST_F(UserDataTest, GetUserByEmailReturnNulloptWhenNotExists)
{
    std::optional<UserInfo> user_info = user_data_->GetUserByEmail("not_exists@qq.com");
    EXPECT_FALSE(user_info.has_value());
}

// 异常情况: 查询不存在的用户昵称返回 std::nullopt
TEST_F(UserDataTest, GetUserByNicknameReturnNulloptWhenNotExists)
{
    std::optional<UserInfo> user_info = user_data_->GetUserByNickname("not_exists_nickname");
    EXPECT_FALSE(user_info.has_value());
}

// 正常情况: 保存用户后昵称存在检查返回 true
TEST_F(UserDataTest, CheckNicknameExistsReturnTrueAfterSave)
{
    user_data_->SaveUser(test_user_);
    EXPECT_TRUE(user_data_->CheckNicknameExists(test_user_.nickname));
}

// 边界情况: 昵称不存在时存在检查返回 false
TEST_F(UserDataTest, CheckNicknameExistsReturnFalseWhenNotExists)
{
    EXPECT_FALSE(user_data_->CheckNicknameExists("not_exists_nickname"));
}

// 正常情况: 保存用户后邮箱存在检查返回 true
TEST_F(UserDataTest, CheckEmailExistsReturnTrueAfterSave)
{
    user_data_->SaveUser(test_user_);
    EXPECT_TRUE(user_data_->CheckEmailExists(test_user_.email));
}

// 边界情况: 邮箱不存在时存在检查返回 false
TEST_F(UserDataTest, CheckEmailExistsReturnFalseWhenNotExists)
{
    EXPECT_FALSE(user_data_->CheckEmailExists("not_exists@qq.com"));
}

// 异常情况: 保存重复唯一键的用户抛出 ChatExcelException
TEST_F(UserDataTest, SaveDuplicateUserThrowChatExcelException)
{
    user_data_->SaveUser(test_user_);
    EXPECT_THROW(user_data_->SaveUser(test_user_), ChatExcelException);
}

// 正常情况: 保存用户后更新用户信息, 再通过用户 ID 查询验证各字段均已更新
TEST_F(UserDataTest, UpdateUserThenGetByUserId)
{
    user_data_->SaveUser(test_user_);

    UserInfo update_info;
    update_info.user_id = test_user_.user_id;
    update_info.nickname = "nick_updated";
    update_info.email = "updated@qq.com";
    update_info.password = "pwd_updated";
    update_info.status = UserStatus::LOGGED_IN;
    user_data_->UpdateUser(update_info);

    std::optional<UserInfo> user_info = user_data_->GetUserByUserId(test_user_.user_id);
    ASSERT_TRUE(user_info.has_value());
    ExpectUserInfoEqual(*user_info, update_info);
}

// 异常情况: 更新不存在的用户抛出 ChatExcelException
TEST_F(UserDataTest, UpdateUserThrowWhenNotExists)
{
    EXPECT_THROW(user_data_->UpdateUser(test_user_), ChatExcelException);
}

// 异常情况: 更新昵称为其他用户已占用的昵称, 唯一键冲突抛出 ChatExcelException
TEST_F(UserDataTest, UpdateUserThrowWhenNicknameConflicts)
{
    user_data_->SaveUser(test_user_);

    UserInfo other_user;
    other_user.user_id = "uid_other_for_test";
    other_user.nickname = "nick_other_for_test";
    other_user.email = "other_test@qq.com";
    other_user.password = "pwd_other_for_test";
    other_user.status = UserStatus::NOT_LOGGED_IN;
    user_data_->SaveUser(other_user);

    // 将 other 用户的昵称更新为 test 用户已占用的昵称, 触发唯一键冲突
    other_user.nickname = test_user_.nickname;
    EXPECT_THROW(user_data_->UpdateUser(other_user), ChatExcelException);
}

// 正常情况: 保存用户后删除用户, 再通过用户 ID 查询返回 std::nullopt
TEST_F(UserDataTest, DeleteUserThenGetReturnNullopt)
{
    user_data_->SaveUser(test_user_);
    user_data_->DeleteUserByUserId(test_user_.user_id);

    std::optional<UserInfo> user_info = user_data_->GetUserByUserId(test_user_.user_id);
    EXPECT_FALSE(user_info.has_value());
}

// 边界情况: 删除不存在的用户不抛出异常
TEST_F(UserDataTest, DeleteUserWhenNotExists)
{
    EXPECT_NO_THROW(user_data_->DeleteUserByUserId("not_exists_user_id"));
}

// ==================== Redis 操作测试 ====================

// 正常情况: 保存缓存后通过用户 ID / 邮箱 / 昵称均可查询到相同用户信息
TEST_F(UserDataTest, SaveUserToCacheThenGetByAllFields)
{
    user_data_->SaveUserToCache(test_user_);

    std::optional<UserInfo> user_info_by_id = user_data_->GetUserByUserIdFromCache(test_user_.user_id);
    ASSERT_TRUE(user_info_by_id.has_value());
    ExpectUserInfoEqual(*user_info_by_id, test_user_);

    std::optional<UserInfo> user_info_by_email = user_data_->GetUserByEmailFromCache(test_user_.email);
    ASSERT_TRUE(user_info_by_email.has_value());
    ExpectUserInfoEqual(*user_info_by_email, test_user_);

    std::optional<UserInfo> user_info_by_nickname = user_data_->GetUserByNicknameFromCache(test_user_.nickname);
    ASSERT_TRUE(user_info_by_nickname.has_value());
    ExpectUserInfoEqual(*user_info_by_nickname, test_user_);
}

// 异常情况: 缓存未命中时三种查询方式均返回 std::nullopt
TEST_F(UserDataTest, GetUserFromCacheReturnNulloptWhenNotExists)
{
    EXPECT_FALSE(user_data_->GetUserByUserIdFromCache("not_exists_user_id").has_value());
    EXPECT_FALSE(user_data_->GetUserByEmailFromCache("not_exists@qq.com").has_value());
    EXPECT_FALSE(user_data_->GetUserByNicknameFromCache("not_exists_nickname").has_value());
}

// 正常情况: 保存缓存后昵称存在检查返回 true
TEST_F(UserDataTest, CheckNicknameExistsInCacheReturnTrueAfterSave)
{
    user_data_->SaveUserToCache(test_user_);
    EXPECT_TRUE(user_data_->CheckNicknameExistsInCache(test_user_.nickname));
}

// 边界情况: 缓存未命中时昵称存在检查返回 false
TEST_F(UserDataTest, CheckNicknameExistsInCacheReturnFalseWhenNotExists)
{
    EXPECT_FALSE(user_data_->CheckNicknameExistsInCache("not_exists_nickname"));
}

// 正常情况: 保存缓存后邮箱存在检查返回 true
TEST_F(UserDataTest, CheckEmailExistsInCacheReturnTrueAfterSave)
{
    user_data_->SaveUserToCache(test_user_);
    EXPECT_TRUE(user_data_->CheckEmailExistsInCache(test_user_.email));
}

// 边界情况: 缓存未命中时邮箱存在检查返回 false
TEST_F(UserDataTest, CheckEmailExistsInCacheReturnFalseWhenNotExists)
{
    EXPECT_FALSE(user_data_->CheckEmailExistsInCache("not_exists@qq.com"));
}

// 边界情况: 保存缓存后缓存 key 的过期时间被设置为 1 小时以内
TEST_F(UserDataTest, SaveUserToCacheSetExpireTime)
{
    user_data_->SaveUserToCache(test_user_);

    long long expire_time = GetRedisHandle()->ttl(kUserCacheKey);
    EXPECT_GT(expire_time, 0);
    EXPECT_LE(expire_time, kUserCacheExpireTime);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察数据层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "user_data_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
