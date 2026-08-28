#include "worksheet_data.h"

#include <chrono>
#include <odb/exceptions.hxx>
#include <odb/result.hxx>
#include <odb/transaction.hxx>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"
#include "odb/work_sheet_entity-odb.hxx"

namespace chat_excel
{
namespace file_service
{

namespace
{

// WorkSheet 缓存 hash 类型的 key
constexpr const char* kWorkSheetCacheKey = "worksheet_data";

// WorkSheet 缓存 field 前缀, 拼接文件 ID 构成完整 field
constexpr const char* kWorkSheetFieldPrefix = "worksheet:";

// WorkSheet 缓存过期时间(秒), 3 天
constexpr int kWorkSheetCacheExpireTime = 3 * 24 * 60 * 60;

// JSON 字段名 : WorkSheet 所属文件 ID(整体只保存一份)
constexpr const char* kJsonFileId = "file_id";

// JSON 字段名 : WorkSheet 信息数组
constexpr const char* kJsonWorksheets = "worksheets";

// JSON 字段名 : WorkSheet 名称
constexpr const char* kJsonWorksheetName = "worksheet_name";

// JSON 字段名 : WorkSheet 真实数据存储在的数据库表名
constexpr const char* kJsonTableName = "table_name";

/**
 * @brief 将一个文件对应的全部 WorkSheet 信息序列化为 JSON 字符串
 *        JSON 结构为对象 : 文件 ID 只保存一份, worksheets 数组中每个元素
 *        为一对 <worksheet 名称, 对应数据库表名>
 * @param file_id WorkSheet 所属文件 ID
 * @param worksheet_list WorkSheet 信息列表
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeWorkSheetList(const std::string& file_id, const std::vector<WorkSheetInfo>& worksheet_list,
                            std::string& json_str)
{
    Json::Value json(Json::objectValue);
    json[kJsonFileId] = file_id;

    // JSON 数组, 每个元素为一对 <worksheet 名称, 对应数据库表名>
    Json::Value worksheet_array(Json::arrayValue);
    for (const WorkSheetInfo& worksheet_info : worksheet_list)
    {
        Json::Value worksheet_json;
        worksheet_json[kJsonWorksheetName] = worksheet_info.worksheet_name;
        worksheet_json[kJsonTableName] = worksheet_info.table_name;
        worksheet_array.append(worksheet_json);
    }
    json[kJsonWorksheets] = worksheet_array;
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为 WorkSheet 信息列表
 * @param json_str JSON 字符串
 * @return 反序列化成功返回 WorkSheet 信息列表, JSON 格式错误时返回空列表
 */
std::vector<WorkSheetInfo> DeserializeWorkSheetList(const std::string& json_str)
{
    std::vector<WorkSheetInfo> worksheet_list;
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str) || !json.isObject())
    {
        return worksheet_list;
    }

    // 文件 ID 从 JSON 中读取, 逐个填充到每个 WorkSheet 信息中
    const std::string worksheet_file_id = json[kJsonFileId].asString();
    for (const Json::Value& worksheet_json : json[kJsonWorksheets])
    {
        WorkSheetInfo worksheet_info;
        worksheet_info.file_id = worksheet_file_id;
        worksheet_info.worksheet_name = worksheet_json[kJsonWorksheetName].asString();
        worksheet_info.table_name = worksheet_json[kJsonTableName].asString();
        worksheet_list.push_back(worksheet_info);
    }
    return worksheet_list;
}

} // namespace

WorkSheetData::WorkSheetData(std::shared_ptr<odb::database> mysql_handle,
                             std::shared_ptr<sw::redis::Redis> redis_handle)
    : mysql_handle_(std::move(mysql_handle)),
      redis_handle_(std::move(redis_handle))
{
}

void WorkSheetData::SaveWorkSheets(const std::string& file_id,
                                   const std::vector<WorkSheetInfo>& worksheet_list)
{
    try
    {
        // SQL : INSERT INTO tbl_worksheet_info (file_id, worksheet_name, table_name)
        //       VALUES ('{file_id}', '{worksheet_name}', '{table_name}'),
        //              ('{file_id}', '{worksheet_name}', '{table_name}'), ...
        // 一个文件的全部 WorkSheet 在单个事务内批量插入, 保证整体成功或整体失败
        odb::transaction transaction(mysql_handle_->begin());
        for (const WorkSheetInfo& worksheet_info : worksheet_list)
        {
            WorkSheetEntity entity(file_id, worksheet_info.worksheet_name, worksheet_info.table_name);
            mysql_handle_->persist(entity);
        }
        transaction.commit();
        INFO("保存 WorkSheet 信息到数据库成功, file_id: {}, WorkSheet 数量: {}",
             file_id, worksheet_list.size());
    }
    catch (const odb::exception& e)
    {
        ERR("保存 WorkSheet 信息到数据库失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_SAVE_TO_MYSQL_ERROR);
    }
}

std::vector<WorkSheetInfo> WorkSheetData::GetWorkSheetListByFileId(const std::string& file_id)
{
    try
    {
        // SQL : SELECT id, file_id, worksheet_name, table_name FROM tbl_worksheet_info
        //       WHERE file_id = '{file_id}'
        odb::transaction transaction(mysql_handle_->begin());
        odb::result<WorkSheetEntity> result(mysql_handle_->query<WorkSheetEntity>(
            odb::query<WorkSheetEntity>::file_id == file_id));

        std::vector<WorkSheetInfo> worksheet_list;
        for (odb::result<WorkSheetEntity>::iterator iter = result.begin(); iter != result.end(); ++iter)
        {
            WorkSheetInfo worksheet_info;
            worksheet_info.file_id = iter->FileId();
            worksheet_info.worksheet_name = iter->WorksheetName();
            worksheet_info.table_name = iter->TableName();
            worksheet_list.push_back(worksheet_info);
        }
        transaction.commit();
        INFO("通过文件 ID 获取 WorkSheet 列表成功, file_id: {}, WorkSheet 数量: {}",
             file_id, worksheet_list.size());
        return worksheet_list;
    }
    catch (const odb::exception& e)
    {
        ERR("通过文件 ID 获取 WorkSheet 列表失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_GET_BY_FILE_ID_ERROR);
    }
}

void WorkSheetData::DeleteWorkSheetByFileId(const std::string& file_id)
{
    try
    {
        // SQL : DELETE FROM tbl_worksheet_info WHERE file_id = '{file_id}'
        odb::transaction transaction(mysql_handle_->begin());
        mysql_handle_->erase_query<WorkSheetEntity>(
            odb::query<WorkSheetEntity>::file_id == file_id);
        transaction.commit();
        INFO("通过文件 ID 删除数据库 WorkSheet 信息成功, file_id: {}", file_id);
    }
    catch (const odb::exception& e)
    {
        ERR("通过文件 ID 删除数据库 WorkSheet 信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_DELETE_BY_FILE_ID_ERROR);
    }
}

void WorkSheetData::SaveWorkSheetToCache(const std::string& file_id,
                                         const std::vector<WorkSheetInfo>& worksheet_list)
{
    // 序列化一个文件对应的全部 WorkSheet 信息为 JSON 字符串
    std::string json_str;
    if (!SerializeWorkSheetList(file_id, worksheet_list, json_str))
    {
        ERR("保存 WorkSheet 数据到缓存时序列化失败, file_id: {}", file_id);
        throw ChatExcelException(ErrorCode::WORKSHEET_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 写入 field 与设置过期时间是批量操作, 使用事务(MULTI/EXEC)
        // 保证 field 的写入与过期时间的设置整体原子生效
        const std::string worksheet_field = std::string(kWorkSheetFieldPrefix) + file_id;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kWorkSheetCacheKey, worksheet_field, json_str)
            .expire(kWorkSheetCacheKey, std::chrono::seconds(kWorkSheetCacheExpireTime));
        transaction.exec();
        INFO("保存 WorkSheet 数据到缓存成功, file_id: {}, WorkSheet 数量: {}",
             file_id, worksheet_list.size());
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存 WorkSheet 数据到缓存失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_SAVE_TO_CACHE_ERROR);
    }
}

std::vector<WorkSheetInfo> WorkSheetData::GetWorkSheetListByFileIdFromCache(const std::string& file_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string worksheet_field = std::string(kWorkSheetFieldPrefix) + file_id;
        sw::redis::OptionalString value = redis_handle_->hget(kWorkSheetCacheKey, worksheet_field);
        if (!value)
        {
            return {};
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层回源数据库
        return DeserializeWorkSheetList(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过文件 ID 从缓存获取 WorkSheet 列表失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_GET_FROM_CACHE_BY_FILE_ID_ERROR);
    }
}

void WorkSheetData::DeleteWorkSheetFromCache(const std::string& file_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string worksheet_field = std::string(kWorkSheetFieldPrefix) + file_id;
        redis_handle_->hdel(kWorkSheetCacheKey, worksheet_field);
        INFO("通过文件 ID 删除缓存 WorkSheet 信息成功, file_id: {}", file_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过文件 ID 删除缓存 WorkSheet 信息失败, file_id: {}, 错误: {}", file_id, e.what());
        throw ChatExcelException(ErrorCode::WORKSHEET_DELETE_FROM_CACHE_BY_FILE_ID_ERROR);
    }
}

} // namespace file_service
} // namespace chat_excel
