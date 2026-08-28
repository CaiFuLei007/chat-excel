#pragma once

#include <memory>
#include <string>
#include <vector>
#include <odb/database.hxx>
#include <sw/redis++/redis.h>
#include "svc_file_service/common.h"

namespace chat_excel
{
namespace file_service
{

/**
 * @brief WorkSheet 数据访问类, 封装 WorkSheet 表(tbl_worksheet_info)的 MySQL 操作
 *        与 WorkSheet 数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 缓存读写时机等业务逻辑由上层实现(Cache-Aside 旁路策略);
 *        缓存结构为 hash 类型, key 为 worksheet_data, field 为 worksheet:{file_id},
 *        value 为 JSON 对象字符串 : 文件 ID 只保存一份, worksheets 数组中每个元素
 *        为一对 <worksheet 名称, 对应数据库表名>, 过期时间 3 天
 */
class WorkSheetData
{
public:
    /**
     * @brief 构造函数, 注入 MySQL 操作句柄与 Redis 操作句柄
     * @param mysql_handle MySQL 操作句柄, 由上层创建并统一管理
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    WorkSheetData(std::shared_ptr<odb::database> mysql_handle, std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存一个文件对应的全部 WorkSheet 信息到数据库, 在单个事务内批量插入
     * @param file_id WorkSheet 所属文件 ID
     * @param worksheet_list WorkSheet 信息列表
     */
    void SaveWorkSheets(const std::string& file_id, const std::vector<WorkSheetInfo>& worksheet_list);

    /**
     * @brief 通过文件 ID 获取该文件对应的全部 WorkSheet 信息
     * @param file_id 文件 ID
     * @return WorkSheet 信息列表, 文件没有 WorkSheet 信息时返回空列表
     */
    std::vector<WorkSheetInfo> GetWorkSheetListByFileId(const std::string& file_id);

    /**
     * @brief 通过文件 ID 删除该文件对应的全部 WorkSheet 信息, 不存在时不抛出异常
     * @param file_id 文件 ID
     */
    void DeleteWorkSheetByFileId(const std::string& file_id);

    /**
     * @brief 保存一个文件对应的全部 WorkSheet 数据到缓存, 通过事务批量执行写入与过期时间设置
     * @param file_id WorkSheet 所属文件 ID
     * @param worksheet_list WorkSheet 信息列表
     */
    void SaveWorkSheetToCache(const std::string& file_id, const std::vector<WorkSheetInfo>& worksheet_list);

    /**
     * @brief 通过文件 ID 从缓存获取该文件对应的全部 WorkSheet 信息
     * @param file_id 文件 ID
     * @return WorkSheet 信息列表, 缓存未命中或数据损坏时返回空列表
     */
    std::vector<WorkSheetInfo> GetWorkSheetListByFileIdFromCache(const std::string& file_id);

    /**
     * @brief 通过文件 ID 删除缓存中的 WorkSheet 数据
     * @param file_id 文件 ID
     */
    void DeleteWorkSheetFromCache(const std::string& file_id);

private:
    // MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle_;

    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace file_service
} // namespace chat_excel
