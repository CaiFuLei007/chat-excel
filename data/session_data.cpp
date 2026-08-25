#include "session_data.h"

#include <chrono>
#include <odb/exceptions.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"
#include "odb/session_entity-odb.hxx"

namespace chat_excel
{
namespace user_service
{

namespace
{

// 会话缓存 hash 类型的 key
constexpr const char* kSessionCacheKey = "session_data";

// 会话缓存 field 前缀, 拼接会话 ID 构成完整 field
constexpr const char* kSessionFieldPrefix = "session:";

// 会话缓存过期时间(秒), 3 天
constexpr int kSessionCacheExpireTime = 3 * 24 * 60 * 60;

// JSON 字段名 : 会话 ID
constexpr const char* kJsonSessionId = "session_id";

// JSON 字段名 : 会话所属的用户 ID
constexpr const char* kJsonUserId = "user_id";

/**
 * @brief 将会话信息序列化为 JSON 字符串
 * @param session_info 会话信息
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeSessionInfo(const SessionInfo& session_info, std::string& json_str)
{
    Json::Value json;
    json[kJsonSessionId] = session_info.session_id;
    json[kJsonUserId] = session_info.user_id;
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为会话信息
 * @param json_str JSON 字符串
 * @return 反序列化成功返回会话信息, JSON 格式错误时返回 std::nullopt
 */
std::optional<SessionInfo> DeserializeSessionInfo(const std::string& json_str)
{
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str))
    {
        return std::nullopt;
    }

    SessionInfo session_info;
    session_info.session_id = json[kJsonSessionId].asString();
    session_info.user_id = json[kJsonUserId].asString();
    return session_info;
}

} // namespace

SessionData::SessionData(std::shared_ptr<odb::database> mysql_handle,
                         std::shared_ptr<sw::redis::Redis> redis_handle)
    : mysql_handle_(std::move(mysql_handle)),
      redis_handle_(std::move(redis_handle))
{
}

void SessionData::SaveSession(const SessionInfo& session_info)
{
    try
    {
        // SQL : INSERT INTO tbl_session (session_id, user_id)
        //       VALUES ('{session_id}', '{user_id}')
        odb::transaction transaction(mysql_handle_->begin());
        SessionEntity entity(session_info.session_id, session_info.user_id);
        mysql_handle_->persist(entity);
        transaction.commit();
        INFO("保存会话信息到数据库成功, session_id: {}", session_info.session_id);
    }
    catch (const odb::exception& e)
    {
        ERR("保存会话信息到数据库失败, session_id: {}, 错误: {}", session_info.session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_MYSQL_ERROR);
    }
}

std::optional<SessionInfo> SessionData::GetSessionBySessionId(const std::string& session_id)
{
    try
    {
        // SQL : SELECT id, session_id, user_id FROM tbl_session WHERE session_id = '{session_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<SessionEntity> result(mysql_handle_->query<SessionEntity>(
            odb::query<SessionEntity>::session_id == session_id));
        odb::result<SessionEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        SessionInfo session_info;
        session_info.session_id = iter->SessionId();
        session_info.user_id = iter->UserId();
        transaction.commit();
        return session_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过会话 ID 获取会话信息失败, session_id: {}, 错误: {}", session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_MYSQL_ERROR);
    }
}

void SessionData::DeleteSessionBySessionId(const std::string& session_id)
{
    try
    {
        // SQL : DELETE FROM tbl_session WHERE session_id = '{session_id}'
        odb::transaction transaction(mysql_handle_->begin());
        mysql_handle_->erase_query<SessionEntity>(
            odb::query<SessionEntity>::session_id == session_id);
        transaction.commit();
        INFO("通过会话 ID 删除数据库会话信息成功, session_id: {}", session_id);
    }
    catch (const odb::exception& e)
    {
        ERR("通过会话 ID 删除数据库会话信息失败, session_id: {}, 错误: {}", session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_MYSQL_ERROR);
    }
}

void SessionData::SaveSessionToCache(const SessionInfo& session_info)
{
    // 序列化会话信息为 JSON 字符串
    std::string json_str;
    if (!SerializeSessionInfo(session_info, json_str))
    {
        ERR("保存会话数据到缓存时序列化失败, session_id: {}", session_info.session_id);
        throw ChatExcelException(ErrorCode::SESSION_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 写入 field 与设置过期时间是批量操作, 使用事务(MULTI/EXEC)
        // 保证 field 的写入与过期时间的设置整体原子生效
        const std::string session_field = std::string(kSessionFieldPrefix) + session_info.session_id;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kSessionCacheKey, session_field, json_str)
            .expire(kSessionCacheKey, std::chrono::seconds(kSessionCacheExpireTime));
        transaction.exec();
        INFO("保存会话数据到缓存成功, session_id: {}", session_info.session_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存会话数据到缓存失败, session_id: {}, 错误: {}", session_info.session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_REDIS_ERROR);
    }
}

std::optional<SessionInfo> SessionData::GetSessionBySessionIdFromCache(const std::string& session_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string session_field = std::string(kSessionFieldPrefix) + session_id;
        sw::redis::OptionalString value = redis_handle_->hget(kSessionCacheKey, session_field);
        if (!value)
        {
            return std::nullopt;
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层回源数据库
        return DeserializeSessionInfo(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过会话 ID 从缓存获取会话信息失败, session_id: {}, 错误: {}", session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_REDIS_ERROR);
    }
}

void SessionData::DeleteSessionBySessionIdFromCache(const std::string& session_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string session_field = std::string(kSessionFieldPrefix) + session_id;
        redis_handle_->hdel(kSessionCacheKey, session_field);
        INFO("通过会话 ID 删除缓存会话信息成功, session_id: {}", session_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过会话 ID 删除缓存会话信息失败, session_id: {}, 错误: {}", session_id, e.what());
        throw ChatExcelException(ErrorCode::SESSION_DATA_REDIS_ERROR);
    }
}

} // namespace user_service
} // namespace chat_excel
