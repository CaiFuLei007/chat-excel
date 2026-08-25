#pragma once

#include <memory>
#include <optional>
#include <string>
#include <odb/database.hxx>
#include <sw/redis++/redis.h>
#include "svc_user_service/common.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 用户数据访问类, 封装用户表(tbl_user)的 MySQL 操作与用户数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 缓存读写时机等业务逻辑由上层实现;
 *        缓存结构为 hash 类型, key 为 user_data, 一个用户对应三个 field
 *        (user:{user_id} / user:{user_name} / user:{user_email}), 三个 field 对应相同的
 *        value(包含用户 ID, 昵称, 邮箱, 密码, 状态的 JSON 字符串), 过期时间 1 小时
 */
class UserData
{
public:
    /**
     * @brief 构造函数, 注入 MySQL 操作句柄与 Redis 操作句柄
     * @param mysql_handle MySQL 操作句柄, 由上层创建并统一管理
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    UserData(std::shared_ptr<odb::database> mysql_handle, std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存用户信息到数据库
     * @param user_info 用户信息
     */
    void SaveUser(const UserInfo& user_info);

    /**
     * @brief 通过用户 ID 获取用户信息
     * @param user_id 用户 ID
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByUserId(const std::string& user_id);

    /**
     * @brief 通过用户邮箱获取用户信息
     * @param email 用户邮箱
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByEmail(const std::string& email);

    /**
     * @brief 通过用户昵称获取用户信息
     * @param nickname 用户昵称
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByNickname(const std::string& nickname);

    /**
     * @brief 检查用户昵称在数据库中是否存在
     * @param nickname 用户昵称
     * @return 昵称存在返回 true, 不存在返回 false
     */
    bool CheckNicknameExists(const std::string& nickname);

    /**
     * @brief 检查用户邮箱在数据库中是否存在
     * @param email 用户邮箱
     * @return 邮箱存在返回 true, 不存在返回 false
     */
    bool CheckEmailExists(const std::string& email);

    /**
     * @brief 更新数据库中的用户信息, 通过用户 ID 定位用户, 更新昵称 / 邮箱 / 密码 / 状态
     * @param user_info 用户信息, 其中用户 ID 用于定位用户, 其余字段为更新后的新值
     */
    void UpdateUser(const UserInfo& user_info);

    /**
     * @brief 通过用户 ID 删除数据库中的用户信息, 用户不存在时不抛出异常
     * @param user_id 用户 ID
     */
    void DeleteUserByUserId(const std::string& user_id);

    /**
     * @brief 保存用户数据到缓存, 通过事务一次写入三个 field 并刷新缓存过期时间
     * @param user_info 用户信息
     */
    void SaveUserToCache(const UserInfo& user_info);

    /**
     * @brief 通过用户 ID 从缓存获取用户信息
     * @param user_id 用户 ID
     * @return 用户信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByUserIdFromCache(const std::string& user_id);

    /**
     * @brief 通过用户邮箱从缓存获取用户信息
     * @param email 用户邮箱
     * @return 用户信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByEmailFromCache(const std::string& email);

    /**
     * @brief 通过用户昵称从缓存获取用户信息
     * @param nickname 用户昵称
     * @return 用户信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByNicknameFromCache(const std::string& nickname);

    /**
     * @brief 删除缓存中的用户数据, 通过事务一次删除用户对应的三个 field
     * @param user_info 用户信息
     */
    void DeleteUserFromCache(const UserInfo& user_info);

    /**
     * @brief 检查用户昵称在缓存中是否存在
     * @param nickname 用户昵称
     * @return 昵称存在返回 true, 不存在返回 false
     */
    bool CheckNicknameExistsInCache(const std::string& nickname);

    /**
     * @brief 检查用户邮箱在缓存中是否存在
     * @param email 用户邮箱
     * @return 邮箱存在返回 true, 不存在返回 false
     */
    bool CheckEmailExistsInCache(const std::string& email);

private:
    /**
     * @brief 通过缓存 field 获取用户信息
     * @param field 缓存 field 名
     * @return 用户信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserFromCacheByField(const std::string& field);

    // MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle_;

    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace user_service
} // namespace chat_excel
