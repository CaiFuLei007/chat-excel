#pragma once

#include <string>
#include "data/user_entity.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 用户信息结构体, 用于数据层与业务层之间传递用户数据
 *        password 字段存储加密后的密码, 不存储明文密码
 */
struct UserInfo
{
    // 用户 ID, 全系统唯一
    std::string user_id;

    // 用户昵称, 全系统唯一
    std::string nickname;

    // 用户邮箱, 全系统唯一
    std::string email;

    // 加密后的用户密码
    std::string password;

    // 用户登录状态
    UserStatus status = UserStatus::NOT_LOGGED_IN;
};

/**
 * @brief 会话信息结构体, 用于数据层与业务层之间传递会话数据
 */
struct SessionInfo
{
    // 会话 ID, 全系统唯一
    std::string session_id;

    // 会话所属的用户 ID, 可以重复(多设备登录同一账号)
    std::string user_id;
};

/**
 * @brief 验证码信息结构体, 用于数据层与业务层之间传递验证码数据
 */
struct VerifyCodeInfo
{
    // 验证码 ID, 全系统唯一
    std::string verifycode_id;

    // 验证码
    std::string verify_code;

    // 用户邮箱
    std::string email;

    // 创建时间, 格式 yyyy-MM-dd HH:mm:ss
    std::string create_time;
};

} // namespace user_service
} // namespace chat_excel
