#include "file_data.h"

#include <chrono>
#include <odb/exceptions.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"
#include "odb/file_entity-odb.hxx"

namespace chat_excel
{
namespace file_service
{

namespace
{

// 文件缓存 hash 类型的 key
constexpr const char* kFileCacheKey = "file_data";

// 文件缓存 field 前缀, 拼接文件 ID 构成完整 field
constexpr const char* kFileFieldPrefix = "file:";

// 文件缓存过期时间(秒), 3 天
constexpr int kFileCacheExpireTime = 3 * 24 * 60 * 60;

// JSON 字段名 : 文件 ID
constexpr const char* kJsonFileId = "file_id";

// JSON 字段名 : 文件名
constexpr const char* kJsonFileName = "file_name";

// JSON 字段名 : 文件大小
constexpr const char* kJsonFileSize = "file_size";

// JSON 字段名 : 文件上传时间
constexpr const char* kJsonFileUploadTime = "file_upload_time";

// JSON 字段名 : 文件扩展名
constexpr const char* kJsonFileExtension = "file_extension";

// JSON 字段名 : FastDFS 中的文件 ID
constexpr const char* kJsonFastdfsFileId = "fastdfs_file_id";

// JSON 字段名 : 文件所属用户 ID
constexpr const char* kJsonUserId = "user_id";

// JSON 字段名 : 文件所属会话 ID
constexpr const char* kJsonSessionId = "session_id";

/**
 * @brief 将文件信息序列化为 JSON 字符串
 * @param file_info 文件信息
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeFileInfo(const FileInfo& file_info, std::string& json_str)
{
    Json::Value json;
    json[kJsonFileId] = file_info.file_id;
    json[kJsonFileName] = file_info.file_name;
    json[kJsonFileSize] = Json::Value(static_cast<Json::UInt64>(file_info.file_size));
    json[kJsonFileUploadTime] = Json::Value(static_cast<Json::UInt64>(file_info.file_upload_time));
    json[kJsonFileExtension] = file_info.file_extension;
    json[kJsonFastdfsFileId] = file_info.fastdfs_file_id;
    json[kJsonUserId] = file_info.user_id;
    json[kJsonSessionId] = file_info.session_id;
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为文件信息
 * @param json_str JSON 字符串
 * @return 反序列化成功返回文件信息, JSON 格式错误时返回 std::nullopt
 */
std::optional<FileInfo> DeserializeFileInfo(const std::string& json_str)
{
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str))
    {
        return std::nullopt;
    }

    FileInfo file_info;
    file_info.file_id = json[kJsonFileId].asString();
    file_info.file_name = json[kJsonFileName].asString();
    file_info.file_size = json[kJsonFileSize].asUInt64();
    file_info.file_upload_time = json[kJsonFileUploadTime].asUInt64();
    file_info.file_extension = json[kJsonFileExtension].asString();
    file_info.fastdfs_file_id = json[kJsonFastdfsFileId].asString();
    file_info.user_id = json[kJsonUserId].asString();
    file_info.session_id = json[kJsonSessionId].asString();
    return file_info;
}

/**
 * @brief 将数据库实体对象转换为文件信息结构体
 * @param entity 数据库实体对象
 * @return 文件信息
 */
FileInfo EntityToInfo(const FileEntity& entity)
{
    FileInfo file_info;
    file_info.file_id = entity.FileId();
    file_info.file_name = entity.FileName();
    file_info.file_extension = entity.FileExtension();
    file_info.file_size = entity.FileSize();
    file_info.file_upload_time = entity.FileUploadTime();
    // fastdfs_file_id, session_id 在数据库中可为空, 读取时 NULL 转换为空串
    file_info.fastdfs_file_id = entity.FastdfsFileId() ? *entity.FastdfsFileId() : "";
    file_info.user_id = entity.UserId();
    file_info.session_id = entity.SessionId() ? *entity.SessionId() : "";
    return file_info;
}

/**
 * @brief 将 FastDFS 文件 ID 转换为可空字符串
 * @param fastdfs_file_id FastDFS 文件 ID, 空串语义为文件尚未上传到 FastDFS
 * @return 空串转换为 NULL, 其他转换为非空值
 */
odb::nullable<std::string> ToNullableFastdfsFileId(const std::string& fastdfs_file_id)
{
    if (fastdfs_file_id.empty())
    {
        return odb::nullable<std::string>();
    }
    return odb::nullable<std::string>(fastdfs_file_id);
}

} // namespace

FileData::FileData(std::shared_ptr<odb::database> mysql_handle,
                   std::shared_ptr<sw::redis::Redis> redis_handle)
    : mysql_handle_(std::move(mysql_handle)),
      redis_handle_(std::move(redis_handle))
{
}

void FileData::SaveFile(const FileInfo& file_info)
{
    try
    {
        // SQL : INSERT INTO tbl_file_info (file_id, file_name, file_extension, file_size,
        //       file_upload_time, fastdfs_file_id, user_id, session_id)
        //       VALUES ('{file_id}', '{file_name}', '{file_extension}', {file_size},
        //       {file_upload_time}, {fastdfs_file_id}(可为 NULL), '{user_id}', {session_id}(可为 NULL))
        odb::transaction transaction(mysql_handle_->begin());
        FileEntity entity(file_info.file_id, file_info.file_name, file_info.file_extension,
                          file_info.file_size, file_info.file_upload_time,
                          ToNullableFastdfsFileId(file_info.fastdfs_file_id), file_info.user_id,
                          file_info.session_id);
        mysql_handle_->persist(entity);
        transaction.commit();
        INFO("保存文件信息到数据库成功, file_id: {}", file_info.file_id);
    }
    catch (const odb::exception& e)
    {
        ERR("保存文件信息到数据库失败, file_id: {}, 错误: {}", file_info.file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_SAVE_TO_MYSQL_ERROR);
    }
}

void FileData::UpdateFile(const FileInfo& file_info)
{
    try
    {
        odb::transaction transaction(mysql_handle_->begin());

        // SQL : SELECT id, file_id, file_name, file_extension, file_size, file_upload_time,
        //       fastdfs_file_id, user_id, session_id FROM tbl_file_info WHERE file_id = '{file_id}'
        odb::result<FileEntity> result(mysql_handle_->query<FileEntity>(
            odb::query<FileEntity>::file_id == file_info.file_id));
        odb::result<FileEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            ERR("更新文件信息失败, 文件不存在, file_id: {}", file_info.file_id);
            throw ChatExcelException(ErrorCode::FILE_DATA_NOT_FOUND);
        }

        // SQL : UPDATE tbl_file_info SET file_name = '{file_name}', file_extension = '{file_extension}',
        //       file_size = {file_size}, file_upload_time = {file_upload_time},
        //       fastdfs_file_id = '{fastdfs_file_id}', user_id = '{user_id}',
        //       session_id = '{session_id}' WHERE file_id = '{file_id}'
        FileEntity entity = *iter;
        entity.SetFileName(file_info.file_name);
        entity.SetFileExtension(file_info.file_extension);
        entity.SetFileSize(file_info.file_size);
        entity.SetFileUploadTime(file_info.file_upload_time);
        entity.SetFastdfsFileId(ToNullableFastdfsFileId(file_info.fastdfs_file_id));
        entity.SetUserId(file_info.user_id);
        entity.SetSessionId(file_info.session_id);
        mysql_handle_->update(entity);
        transaction.commit();
        INFO("更新文件信息成功, file_id: {}", file_info.file_id);
    }
    catch (const odb::exception& e)
    {
        ERR("更新文件信息失败, file_id: {}, 错误: {}", file_info.file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_UPDATE_IN_MYSQL_ERROR);
    }
}

std::optional<FileInfo> FileData::GetFileByFileId(const std::string& file_id)
{
    try
    {
        // SQL : SELECT id, file_id, file_name, file_extension, file_size, file_upload_time,
        //       fastdfs_file_id, user_id, session_id FROM tbl_file_info WHERE file_id = '{file_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<FileEntity> result(mysql_handle_->query<FileEntity>(
            odb::query<FileEntity>::file_id == file_id));
        odb::result<FileEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        FileInfo file_info = EntityToInfo(*iter);
        transaction.commit();
        return file_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过文件 ID 获取文件信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_GET_BY_FILE_ID_ERROR);
    }
}

std::optional<FileInfo> FileData::GetFileByChatSession(const std::string& user_id,
                                                       const std::string& chat_session_id)
{
    try
    {
        // SQL : SELECT ... FROM tbl_file_info WHERE user_id = '{user_id}' AND session_id = '{chat_session_id}'
        // 文件表中 session_id 列保存关联的聊天会话 ID(fileChatMap 写入), 取第一条即可
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<FileEntity> result(mysql_handle_->query<FileEntity>(
            odb::query<FileEntity>::user_id == user_id
            && odb::query<FileEntity>::session_id == chat_session_id));
        odb::result<FileEntity>::iterator iter = result.begin();
        if (iter == result.end())
        {
            transaction.commit();
            return std::nullopt;
        }

        FileInfo file_info = EntityToInfo(*iter);
        transaction.commit();
        return file_info;
    }
    catch (const odb::exception& e)
    {
        ERR("通过聊天会话反查文件信息失败, user_id: {}, chat_session_id: {}, 错误: {}",
            user_id, chat_session_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_GET_BY_FILE_ID_ERROR);
    }
}

void FileData::DeleteFileByFileId(const std::string& file_id)
{
    try
    {
        // SQL : DELETE FROM tbl_file_info WHERE file_id = '{file_id}'
        odb::transaction transaction(mysql_handle_->begin());
        mysql_handle_->erase_query<FileEntity>(
            odb::query<FileEntity>::file_id == file_id);
        transaction.commit();
        INFO("通过文件 ID 删除数据库文件信息成功, file_id: {}", file_id);
    }
    catch (const odb::exception& e)
    {
        ERR("通过文件 ID 删除数据库文件信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_DELETE_BY_FILE_ID_ERROR);
    }
}

std::vector<FileInfo> FileData::GetFileListByUserId(const std::string& user_id)
{
    try
    {
        // SQL : SELECT id, file_id, file_name, file_extension, file_size, file_upload_time,
        //       fastdfs_file_id, user_id, session_id FROM tbl_file_info WHERE user_id = '{user_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<FileEntity> result(mysql_handle_->query<FileEntity>(
            odb::query<FileEntity>::user_id == user_id));

        std::vector<FileInfo> file_list;
        for (odb::result<FileEntity>::iterator iter = result.begin(); iter != result.end(); ++iter)
        {
            file_list.push_back(EntityToInfo(*iter));
        }
        transaction.commit();
        INFO("通过用户 ID 获取文件列表成功, user_id: {}, 文件数量: {}", user_id, file_list.size());
        return file_list;
    }
    catch (const odb::exception& e)
    {
        ERR("通过用户 ID 获取文件列表失败, user_id: {}, 错误: {}", user_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_GET_LIST_BY_USER_ID_ERROR);
    }
}

void FileData::SaveFileToCache(const FileInfo& file_info)
{
    // 序列化文件信息为 JSON 字符串
    std::string json_str;
    if (!SerializeFileInfo(file_info, json_str))
    {
        ERR("保存文件数据到缓存时序列化失败, file_id: {}", file_info.file_id);
        throw ChatExcelException(ErrorCode::FILE_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 写入 field 与设置过期时间是批量操作, 使用事务(MULTI/EXEC)
        // 保证 field 的写入与过期时间的设置整体原子生效
        const std::string file_field = std::string(kFileFieldPrefix) + file_info.file_id;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kFileCacheKey, file_field, json_str)
            .expire(kFileCacheKey, std::chrono::seconds(kFileCacheExpireTime));
        transaction.exec();
        INFO("保存文件数据到缓存成功, file_id: {}", file_info.file_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存文件数据到缓存失败, file_id: {}, 错误: {}", file_info.file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_SAVE_TO_CACHE_ERROR);
    }
}

std::optional<FileInfo> FileData::GetFileByFileIdFromCache(const std::string& file_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string file_field = std::string(kFileFieldPrefix) + file_id;
        sw::redis::OptionalString value = redis_handle_->hget(kFileCacheKey, file_field);
        if (!value)
        {
            return std::nullopt;
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层回源数据库
        return DeserializeFileInfo(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过文件 ID 从缓存获取文件信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_GET_FROM_CACHE_BY_FILE_ID_ERROR);
    }
}

void FileData::DeleteFileByFileIdFromCache(const std::string& file_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string file_field = std::string(kFileFieldPrefix) + file_id;
        redis_handle_->hdel(kFileCacheKey, file_field);
        INFO("通过文件 ID 删除缓存文件信息成功, file_id: {}", file_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过文件 ID 删除缓存文件信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::FILE_DELETE_FROM_CACHE_BY_FILE_ID_ERROR);
    }
}

} // namespace file_service
} // namespace chat_excel
