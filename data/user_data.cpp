#include "user_data.h"

#include <chrono>
#include <odb/exceptions.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"
#include "odb/user_entity-odb.hxx"

namespace chat_excel
{
namespace user_service
{

namespace
{

// 用户缓存 hash 类型的 key
constexpr const char* kUserCacheKey = "user_data";

// 用户缓存 field 前缀, 分别拼接用户 ID / 昵称 / 邮箱构成完整 field
constexpr const char* kUserFieldPrefix = "user:";

// 用户缓存过期时间(秒), 1 小时; Cache-Aside 策略下删除或注销依赖 TTL 自然过期
constexpr int kUserCacheExpireTime = 60 * 60;

// JSON 字段名 : 用户 ID
constexpr const char* kJsonUserId = "user_id";

// JSON 字段名 : 用户昵称
constexpr const char* kJsonNickname = "nickname";

// JSON 字段名 : 用户邮箱
constexpr const char* kJsonEmail = "email";

// JSON 字段名 : 用户密码(已加密)
constexpr const char* kJsonPassword = "password";

// JSON 字段名 : 用户登录状态
constexpr const char* kJsonStatus = "status";

/**
 * @brief 将用户信息序列化为 JSON 字符串
 * @param user_info 用户信息
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeUserInfo(const UserInfo& user_info, std::string& json_str)
{
    Json::Value json;
    json[kJsonUserId] = user_info.user_id;
    json[kJsonNickname] = user_info.nickname;
    json[kJsonEmail] = user_info.email;
    json[kJsonPassword] = user_info.password;
    json[kJsonStatus] = static_cast<Json::UInt>(user_info.status);
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为用户信息
 * @param json_str JSON 字符串
 * @return 反序列化成功返回用户信息, JSON 格式错误时返回 std::nullopt
 */
std::optional<UserInfo> DeserializeUserInfo(const std::string& json_str)
{
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str))
    {
        return std::nullopt;
    }

    UserInfo user_info;
    user_info.user_id = json[kJsonUserId].asString();
    user_info.nickname = json[kJsonNickname].asString();
    user_info.email = json[kJsonEmail].asString();
    user_info.password = json[kJsonPassword].asString();
    user_info.status = static_cast<UserStatus>(json[kJsonStatus].asUInt());
    return user_info;
}

/**
 * @brief 将用户表映射对象转换为用户信息结构体
 * @param entity 用户表映射对象
 * @return 用户信息结构体
 */
UserInfo EntityToInfo(const UserEntity& entity)
{
    UserInfo user_info;
    user_info.user_id = entity.UserId();
    user_info.nickname = entity.Nickname();
    user_info.email = entity.Email();
    user_info.password = entity.Password();
    user_info.status = entity.Status();
    return user_info;
}

} // namespace

UserData::UserData(std::shared_ptr<odb::database> mysql_handle,
                   std::shared_ptr<sw::redis::Redis> redis_handle)
    : mysql_handle_(std::move(mysql_handle)),
      redis_handle_(std::move(redis_handle))
{
}

void UserData::SaveUser(const UserInfo& user_info)
{
    try
    {
        // SQL : INSERT INTO tbl_user (user_id, nickname, email, password, status)
        //       VALUES ('{user_id}', '{nickname}', '{email}', '{password}', {status})
        odb::transaction transaction(mysql_handle_->begin());
        UserEntity entity(user_info.user_id, user_info.nickname, user_info.email,
                          user_info.password, user_info.status);
        mysql_handle_->persist(entity);
        transaction.commit();
        INFO("保存用户信息到数据库成功, user_id: {}", user_info.user_id);
    }
    catch (const odb::exception& e)
    {
        ERR("保存用户信息到数据库失败, user_id: {}, 错误: {}", user_info.user_id, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

std::optional<UserInfo> UserData::GetUserByUserId(const std::string& user_id)
{
    try
    {
        // SQL : SELECT id, user_id, nickname, email, password, status
        //       FROM tbl_user WHERE user_id = '{user_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<UserEntity> result(mysql_handle_->query<UserEntity>(
            odb::query<UserEntity>::user_id == user_id));
        odb::result<UserEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        UserInfo user_info = EntityToInfo(*iter);
        transaction.commit();
        return user_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过用户 ID 获取用户信息失败, user_id: {}, 错误: {}", user_id, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

std::optional<UserInfo> UserData::GetUserByEmail(const std::string& email)
{
    try
    {
        // SQL : SELECT id, user_id, nickname, email, password, status
        //       FROM tbl_user WHERE email = '{email}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<UserEntity> result(mysql_handle_->query<UserEntity>(
            odb::query<UserEntity>::email == email));
        odb::result<UserEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        UserInfo user_info = EntityToInfo(*iter);
        transaction.commit();
        return user_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过用户邮箱获取用户信息失败, email: {}, 错误: {}", email, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

std::optional<UserInfo> UserData::GetUserByNickname(const std::string& nickname)
{
    try
    {
        // SQL : SELECT id, user_id, nickname, email, password, status
        //       FROM tbl_user WHERE nickname = '{nickname}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<UserEntity> result(mysql_handle_->query<UserEntity>(
            odb::query<UserEntity>::nickname == nickname));
        odb::result<UserEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        UserInfo user_info = EntityToInfo(*iter);
        transaction.commit();
        return user_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过用户昵称获取用户信息失败, nickname: {}, 错误: {}", nickname, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

bool UserData::CheckNicknameExists(const std::string& nickname)
{
    try
    {
        // SQL : SELECT id FROM tbl_user WHERE nickname = '{nickname}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<UserEntity> result(mysql_handle_->query<UserEntity>(
            odb::query<UserEntity>::nickname == nickname));
        bool exists = result.begin() != result.end();
        transaction.commit();
        return exists;
    }
    catch (const odb::exception& e)
    {
        ERR("检查用户昵称是否存在失败, nickname: {}, 错误: {}", nickname, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

bool UserData::CheckEmailExists(const std::string& email)
{
    try
    {
        // SQL : SELECT id FROM tbl_user WHERE email = '{email}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<UserEntity> result(mysql_handle_->query<UserEntity>(
            odb::query<UserEntity>::email == email));
        bool exists = result.begin() != result.end();
        transaction.commit();
        return exists;
    }
    catch (const odb::exception& e)
    {
        ERR("检查用户邮箱是否存在失败, email: {}, 错误: {}", email, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_MYSQL_ERROR);
    }
}

void UserData::SaveUserToCache(const UserInfo& user_info)
{
    // 序列化用户信息为 JSON 字符串, 三个 field 对应相同的 value
    std::string json_str;
    if (!SerializeUserInfo(user_info, json_str))
    {
        ERR("保存用户数据到缓存时序列化失败, user_id: {}", user_info.user_id);
        throw ChatExcelException(ErrorCode::USER_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 一个用户对应三个 field, 使用事务(MULTI/EXEC)批量执行,
        // 保证三个 field 的写入与过期时间的设置整体原子生效
        const std::string user_id_field = std::string(kUserFieldPrefix) + user_info.user_id;
        const std::string nickname_field = std::string(kUserFieldPrefix) + user_info.nickname;
        const std::string email_field = std::string(kUserFieldPrefix) + user_info.email;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kUserCacheKey, user_id_field, json_str)
            .hset(kUserCacheKey, nickname_field, json_str)
            .hset(kUserCacheKey, email_field, json_str)
            .expire(kUserCacheKey, std::chrono::seconds(kUserCacheExpireTime));
        transaction.exec();
        INFO("保存用户数据到缓存成功, user_id: {}", user_info.user_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存用户数据到缓存失败, user_id: {}, 错误: {}", user_info.user_id, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_REDIS_ERROR);
    }
}

std::optional<UserInfo> UserData::GetUserFromCacheByField(const std::string& field)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        sw::redis::OptionalString value = redis_handle_->hget(kUserCacheKey, field);
        if (!value)
        {
            return std::nullopt;
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层回源数据库
        return DeserializeUserInfo(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过缓存 field 获取用户信息失败, field: {}, 错误: {}", field, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_REDIS_ERROR);
    }
}

std::optional<UserInfo> UserData::GetUserByUserIdFromCache(const std::string& user_id)
{
    return GetUserFromCacheByField(std::string(kUserFieldPrefix) + user_id);
}

std::optional<UserInfo> UserData::GetUserByEmailFromCache(const std::string& email)
{
    return GetUserFromCacheByField(std::string(kUserFieldPrefix) + email);
}

std::optional<UserInfo> UserData::GetUserByNicknameFromCache(const std::string& nickname)
{
    return GetUserFromCacheByField(std::string(kUserFieldPrefix) + nickname);
}

bool UserData::CheckNicknameExistsInCache(const std::string& nickname)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        return redis_handle_->hexists(kUserCacheKey, std::string(kUserFieldPrefix) + nickname);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("检查用户昵称在缓存中是否存在失败, nickname: {}, 错误: {}", nickname, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_REDIS_ERROR);
    }
}

bool UserData::CheckEmailExistsInCache(const std::string& email)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        return redis_handle_->hexists(kUserCacheKey, std::string(kUserFieldPrefix) + email);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("检查用户邮箱在缓存中是否存在失败, email: {}, 错误: {}", email, e.what());
        throw ChatExcelException(ErrorCode::USER_DATA_REDIS_ERROR);
    }
}

} // namespace user_service
} // namespace chat_excel
