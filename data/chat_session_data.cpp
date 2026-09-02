#include "chat_session_data.h"

#include <odb/exceptions.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"
#include "odb/chat_session_entity-odb.hxx"

namespace chat_excel
{
namespace ai_service
{

namespace
{

// 聊天会话缓存 hash 类型的 key
constexpr const char* kChatSessionCacheKey = "chat_session_data";

// 聊天会话缓存 field 前缀, 拼接会话 ID 构成完整 field
constexpr const char* kChatSessionFieldPrefix = "chat_session:";

// 聊天会话缓存过期时间(秒), 3 天
constexpr int kChatSessionCacheExpireTime = 3 * 24 * 60 * 60;

// JSON 字段名 : 聊天会话 ID
constexpr const char* kJsonChatSessionId = "chat_session_id";

// JSON 字段名 : 会话所属用户 ID
constexpr const char* kJsonUserId = "user_id";

// JSON 字段名 : 会话标题
constexpr const char* kJsonTitle = "title";

// JSON 字段名 : 会话创建时间
constexpr const char* kJsonCreateTime = "create_time";

// JSON 字段名 : 会话最近一次消息时间
constexpr const char* kJsonUpdateTime = "update_time";

// JSON 字段名 : 会话总消息数
constexpr const char* kJsonTotalMessageCount = "total_message_count";

// JSON 字段名 : 会话使用的模型名称
constexpr const char* kJsonModelName = "model_name";

// JSON 字段名 : 会话关联的文件 ID
constexpr const char* kJsonFileId = "file_id";

// JSON 字段名 : 会话类型
constexpr const char* kJsonType = "type";

// JSON 字段名 : 数据库连接信息
constexpr const char* kJsonConnectionInfo = "connection_info";

/**
 * @brief 将聊天会话信息序列化为 JSON 字符串
 * @param chat_session_info 聊天会话信息
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeChatSessionInfo(const ChatSessionInfo& chat_session_info, std::string& json_str)
{
    Json::Value json;
    json[kJsonChatSessionId] = chat_session_info.chat_session_id;
    json[kJsonUserId] = chat_session_info.user_id;
    json[kJsonTitle] = chat_session_info.title;
    json[kJsonCreateTime] = Json::Value(static_cast<Json::UInt64>(chat_session_info.create_time));
    json[kJsonUpdateTime] = Json::Value(static_cast<Json::UInt64>(chat_session_info.update_time));
    json[kJsonTotalMessageCount] = Json::Value(static_cast<Json::UInt64>(chat_session_info.total_message_count));
    json[kJsonModelName] = chat_session_info.model_name;
    json[kJsonFileId] = chat_session_info.file_id;
    json[kJsonType] = chat_session_info.type;
    json[kJsonConnectionInfo] = chat_session_info.connection_info;
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为聊天会话信息
 * @param json_str JSON 字符串
 * @return 反序列化成功返回聊天会话信息, JSON 格式错误时返回 std::nullopt
 */
std::optional<ChatSessionInfo> DeserializeChatSessionInfo(const std::string& json_str)
{
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str))
    {
        return std::nullopt;
    }

    ChatSessionInfo chat_session_info;
    chat_session_info.chat_session_id = json[kJsonChatSessionId].asString();
    chat_session_info.user_id = json[kJsonUserId].asString();
    chat_session_info.title = json[kJsonTitle].asString();
    chat_session_info.create_time = json[kJsonCreateTime].asUInt64();
    chat_session_info.update_time = json[kJsonUpdateTime].asUInt64();
    chat_session_info.total_message_count = json[kJsonTotalMessageCount].asUInt64();
    chat_session_info.model_name = json[kJsonModelName].asString();
    chat_session_info.file_id = json[kJsonFileId].asString();
    chat_session_info.type = json[kJsonType].asString();
    chat_session_info.connection_info = json[kJsonConnectionInfo].asString();
    return chat_session_info;
}

/**
 * @brief 将数据库实体对象转换为聊天会话信息结构体
 * @param entity 数据库实体对象
 * @return 聊天会话信息
 */
ChatSessionInfo EntityToInfo(const ChatSessionEntity& entity)
{
    ChatSessionInfo chat_session_info;
    chat_session_info.chat_session_id = entity.ChatSessionId();
    chat_session_info.user_id = entity.UserId();
    // title, file_id, connection_info 在数据库中可为空, 读取时 NULL 转换为空串
    chat_session_info.title = entity.Title() ? *entity.Title() : "";
    chat_session_info.create_time = entity.CreateTime();
    chat_session_info.update_time = entity.UpdateTime();
    chat_session_info.total_message_count = entity.TotalMessageCount();
    chat_session_info.model_name = entity.ModelName();
    chat_session_info.file_id = entity.FileId() ? *entity.FileId() : "";
    chat_session_info.type = entity.Type();
    chat_session_info.connection_info = entity.ConnectionInfo() ? *entity.ConnectionInfo() : "";
    return chat_session_info;
}

/**
 * @brief 将可空字符串字段由空串约定转换为 NULL 语义
 * @param value 字符串值, 空串语义为 NULL
 * @return 空串转换为 NULL, 其他转换为非空值
 */
odb::nullable<std::string> ToNullableString(const std::string& value)
{
    if (value.empty())
    {
        return odb::nullable<std::string>();
    }
    return odb::nullable<std::string>(value);
}

} // namespace

ChatSessionData::ChatSessionData(std::shared_ptr<odb::database> mysql_handle,
                                 std::shared_ptr<sw::redis::Redis> redis_handle)
    : mysql_handle_(std::move(mysql_handle)),
      redis_handle_(std::move(redis_handle))
{
}

void ChatSessionData::SaveOrUpdateChatSession(const ChatSessionInfo& chat_session_info)
{
    try
    {
        odb::transaction transaction(mysql_handle_->begin());

        // SQL : SELECT id, chat_session_id, user_id, title, create_time, update_time,
        //       total_message_count, model_name, file_id, type, connection_info
        //       FROM tbl_chat_session WHERE chat_session_id = '{chat_session_id}'
        odb::result<ChatSessionEntity> result(mysql_handle_->query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chat_session_id == chat_session_info.chat_session_id));
        odb::result<ChatSessionEntity>::iterator iter = result.begin();

        if (iter == result.end())
        {
            // SQL : INSERT INTO tbl_chat_session (chat_session_id, user_id, title, create_time,
            //       update_time, total_message_count, model_name, file_id, type, connection_info)
            //       VALUES ('{chat_session_id}', '{user_id}', '{title}'(可为 NULL), {create_time},
            //       {update_time}, {total_message_count}, '{model_name}', '{file_id}'(可为 NULL),
            //       '{type}', '{connection_info}'(可为 NULL))
            ChatSessionEntity entity(chat_session_info.chat_session_id, chat_session_info.user_id,
                                     ToNullableString(chat_session_info.title),
                                     chat_session_info.create_time, chat_session_info.update_time,
                                     chat_session_info.total_message_count, chat_session_info.model_name,
                                     ToNullableString(chat_session_info.file_id), chat_session_info.type,
                                     ToNullableString(chat_session_info.connection_info));
            mysql_handle_->persist(entity);
            transaction.commit();
            INFO("保存聊天会话信息到数据库成功, chat_session_id: {}", chat_session_info.chat_session_id);
            return;
        }

        // SQL : UPDATE tbl_chat_session SET title = '{title}', update_time = {update_time},
        //       total_message_count = {total_message_count}, file_id = '{file_id}'(可为 NULL),
        //       connection_info = '{connection_info}'(可为 NULL) WHERE chat_session_id = '{chat_session_id}'
        //       用户 ID, 创建时间, 模型名称, 会话类型为会话固有属性, 保持数据库中的值不变
        ChatSessionEntity entity = *iter;
        entity.SetTitle(ToNullableString(chat_session_info.title));
        entity.SetUpdateTime(chat_session_info.update_time);
        entity.SetTotalMessageCount(chat_session_info.total_message_count);
        entity.SetFileId(ToNullableString(chat_session_info.file_id));
        entity.SetConnectionInfo(ToNullableString(chat_session_info.connection_info));
        mysql_handle_->update(entity);
        transaction.commit();
        INFO("更新数据库中的聊天会话信息成功, chat_session_id: {}", chat_session_info.chat_session_id);
    }
    catch (const odb::exception& e)
    {
        ERR("保存或更新聊天会话信息失败, chat_session_id: {}, 错误: {}",
            chat_session_info.chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_SAVE_TO_MYSQL_ERROR);
    }
}

std::optional<ChatSessionInfo> ChatSessionData::GetChatSessionBySessionId(const std::string& chat_session_id)
{
    try
    {
        // SQL : SELECT id, chat_session_id, user_id, title, create_time, update_time,
        //       total_message_count, model_name, file_id, type, connection_info
        //       FROM tbl_chat_session WHERE chat_session_id = '{chat_session_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<ChatSessionEntity> result(mysql_handle_->query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chat_session_id == chat_session_id));
        odb::result<ChatSessionEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        ChatSessionInfo chat_session_info = EntityToInfo(*iter);
        transaction.commit();
        return chat_session_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过会话 ID 获取聊天会话信息失败, chat_session_id: {}, 错误: {}", chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_GET_BY_SESSION_ID_ERROR);
    }
}

void ChatSessionData::DeleteChatSessionBySessionId(const std::string& chat_session_id)
{
    try
    {
        // SQL : DELETE FROM tbl_chat_session WHERE chat_session_id = '{chat_session_id}'
        odb::transaction transaction(mysql_handle_->begin());
        mysql_handle_->erase_query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chat_session_id == chat_session_id);
        transaction.commit();
        INFO("通过会话 ID 删除数据库聊天会话信息成功, chat_session_id: {}", chat_session_id);
    }
    catch (const odb::exception& e)
    {
        ERR("通过会话 ID 删除数据库聊天会话信息失败, chat_session_id: {}, 错误: {}", chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_DELETE_BY_SESSION_ID_ERROR);
    }
}

std::vector<ChatSessionInfo> ChatSessionData::GetChatSessionListByUserId(const std::string& user_id)
{
    try
    {
        // SQL : SELECT id, chat_session_id, user_id, title, create_time, update_time,
        //       total_message_count, model_name, file_id, type, connection_info
        //       FROM tbl_chat_session WHERE user_id = '{user_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<ChatSessionEntity> result(mysql_handle_->query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::user_id == user_id));

        std::vector<ChatSessionInfo> chat_session_list;
        for (odb::result<ChatSessionEntity>::iterator iter = result.begin(); iter != result.end(); ++iter)
        {
            chat_session_list.push_back(EntityToInfo(*iter));
        }
        transaction.commit();
        INFO("通过用户 ID 获取聊天会话列表成功, user_id: {}, 会话数量: {}", user_id, chat_session_list.size());
        return chat_session_list;
    }
    catch (const odb::exception& e)
    {
        ERR("通过用户 ID 获取聊天会话列表失败, user_id: {}, 错误: {}", user_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_GET_LIST_BY_USER_ID_ERROR);
    }
}

std::vector<ChatSessionInfo> ChatSessionData::GetChatSessionListByFileId(const std::string& file_id)
{
    try
    {
        // SQL : SELECT id, chat_session_id, user_id, title, create_time, update_time,
        //       total_message_count, model_name, file_id, type, connection_info
        //       FROM tbl_chat_session WHERE file_id = '{file_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<ChatSessionEntity> result(mysql_handle_->query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::file_id == file_id));

        std::vector<ChatSessionInfo> chat_session_list;
        for (odb::result<ChatSessionEntity>::iterator iter = result.begin(); iter != result.end(); ++iter)
        {
            chat_session_list.push_back(EntityToInfo(*iter));
        }
        transaction.commit();
        INFO("通过文件 ID 获取聊天会话列表成功, file_id: {}, 会话数量: {}", file_id, chat_session_list.size());
        return chat_session_list;
    }
    catch (const odb::exception& e)
    {
        ERR("通过文件 ID 获取聊天会话列表失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_GET_LIST_BY_FILE_ID_ERROR);
    }
}

void ChatSessionData::SaveChatSessionToCache(const ChatSessionInfo& chat_session_info)
{
    // 序列化聊天会话信息为 JSON 字符串
    std::string json_str;
    if (!SerializeChatSessionInfo(chat_session_info, json_str))
    {
        ERR("保存聊天会话数据到缓存时序列化失败, chat_session_id: {}", chat_session_info.chat_session_id);
        throw ChatExcelException(ErrorCode::CHAT_SESSION_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 写入 field 与设置过期时间是批量操作, 使用事务(MULTI/EXEC)
        // 保证 field 的写入与过期时间的设置整体原子生效
        const std::string chat_session_field = std::string(kChatSessionFieldPrefix) + chat_session_info.chat_session_id;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kChatSessionCacheKey, chat_session_field, json_str)
            .expire(kChatSessionCacheKey, std::chrono::seconds(kChatSessionCacheExpireTime));
        transaction.exec();
        INFO("保存聊天会话数据到缓存成功, chat_session_id: {}", chat_session_info.chat_session_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存聊天会话数据到缓存失败, chat_session_id: {}, 错误: {}",
            chat_session_info.chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_SAVE_TO_CACHE_ERROR);
    }
}

std::optional<ChatSessionInfo> ChatSessionData::GetChatSessionBySessionIdFromCache(const std::string& chat_session_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string chat_session_field = std::string(kChatSessionFieldPrefix) + chat_session_id;
        sw::redis::OptionalString value = redis_handle_->hget(kChatSessionCacheKey, chat_session_field);
        if (!value)
        {
            return std::nullopt;
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层回源数据库
        return DeserializeChatSessionInfo(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过会话 ID 从缓存获取聊天会话信息失败, chat_session_id: {}, 错误: {}", chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_GET_FROM_CACHE_BY_SESSION_ID_ERROR);
    }
}

void ChatSessionData::DeleteChatSessionBySessionIdFromCache(const std::string& chat_session_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string chat_session_field = std::string(kChatSessionFieldPrefix) + chat_session_id;
        redis_handle_->hdel(kChatSessionCacheKey, chat_session_field);
        INFO("通过会话 ID 删除缓存聊天会话信息成功, chat_session_id: {}", chat_session_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过会话 ID 删除缓存聊天会话信息失败, chat_session_id: {}, 错误: {}", chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::CHAT_SESSION_DELETE_FROM_CACHE_BY_SESSION_ID_ERROR);
    }
}

} // namespace ai_service
} // namespace chat_excel
