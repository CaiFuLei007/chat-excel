#pragma once

#include <string>

namespace chat_excel
{
namespace file_service
{

/**
 * @brief 文件信息结构体, 用于数据层与业务层之间传递文件元信息
 *        文件的二进制内容存储在 FastDFS, 此处仅传递元信息
 */
struct FileInfo
{
    // 文件 ID, 全系统唯一
    std::string file_id;

    // 文件名
    std::string file_name;

    // 文件扩展名
    std::string file_extension;

    // 文件大小, 单位字节
    unsigned long long file_size = 0;

    // 文件上传时间, 由上层业务层传递
    unsigned long long file_upload_time = 0;

    // 文件在 FastDFS 中的文件 ID, 用于定位二进制文件内容
    std::string fastdfs_file_id;

    // 文件所属用户 ID
    std::string user_id;

    // 文件所属会话 ID
    std::string session_id;
};

/**
 * @brief WorkSheet 信息结构体, 用于数据层与业务层之间传递工作表元信息
 */
struct WorkSheetInfo
{
    // WorkSheet 所属文件 ID
    std::string file_id;

    // WorkSheet 名称
    std::string worksheet_name;

    // 该 WorkSheet 真实数据存储在的数据库表名
    std::string table_name;
};

} // namespace file_service
} // namespace chat_excel
