#pragma once

#include <memory>
#include <optional>
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
 * @brief 文件数据访问类, 封装文件信息表(tbl_file_info)的 MySQL 操作与文件数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 缓存读写时机等业务逻辑由上层实现(Cache-Aside 旁路策略);
 *        缓存结构为 hash 类型, key 为 file_data, field 为 file:{file_id},
 *        value 为包含文件 ID, 文件名, 文件大小, 上传时间, 扩展名, fastdfs 文件 ID,
 *        用户 ID, 会话 ID 的 JSON 字符串, 过期时间 3 天
 */
class FileData
{
public:
    /**
     * @brief 构造函数, 注入 MySQL 操作句柄与 Redis 操作句柄
     * @param mysql_handle MySQL 操作句柄, 由上层创建并统一管理
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    FileData(std::shared_ptr<odb::database> mysql_handle, std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存文件信息到数据库, 文件不存在时插入新记录
     * @param file_info 文件信息
     */
    void SaveFile(const FileInfo& file_info);

    /**
     * @brief 更新数据库中的文件信息, 通过文件 ID 定位文件, 更新其余字段
     * @param file_info 文件信息, 其中文件 ID 用于定位文件, 其余字段为更新后的新值
     */
    void UpdateFile(const FileInfo& file_info);

    /**
     * @brief 通过文件 ID 获取文件信息
     * @param file_id 文件 ID
     * @return 文件信息, 文件不存在时返回 std::nullopt
     */
    std::optional<FileInfo> GetFileByFileId(const std::string& file_id);

    /**
     * @brief 通过文件 ID 删除数据库中的文件信息, 文件不存在时不抛出异常
     * @param file_id 文件 ID
     */
    void DeleteFileByFileId(const std::string& file_id);

    /**
     * @brief 通过用户 ID 获取用户上传的所有文件列表
     * @param user_id 用户 ID
     * @return 文件信息列表, 用户没有上传过文件时返回空列表
     */
    std::vector<FileInfo> GetFileListByUserId(const std::string& user_id);

    /**
     * @brief 保存文件数据到缓存, 通过事务批量执行写入与过期时间设置
     * @param file_info 文件信息
     */
    void SaveFileToCache(const FileInfo& file_info);

    /**
     * @brief 通过文件 ID 从缓存获取文件信息
     * @param file_id 文件 ID
     * @return 文件信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<FileInfo> GetFileByFileIdFromCache(const std::string& file_id);

    /**
     * @brief 通过文件 ID 删除缓存中的文件数据
     * @param file_id 文件 ID
     */
    void DeleteFileByFileIdFromCache(const std::string& file_id);

private:
    // MySQL 操作句柄
    std::shared_ptr<odb::database> mysql_handle_;

    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace file_service
} // namespace chat_excel
