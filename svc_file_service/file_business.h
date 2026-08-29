#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cpp-toolkit/rpc.h>
#include "data/file_data.h"
#include "data/worksheet_data.h"
#include "svc_file_service/common.h"

namespace chat_excel
{
namespace file_service
{

/**
 * @brief 文件业务逻辑类, 负责文件上传/下载/删除/查询等业务逻辑的组织与实现,
 *        组织文件信息缓存与 WorkSheet 缓存的读写时机(Cache-Aside 旁路缓存策略),
 *        文件二进制数据通过 FastDFS 客户端存储, Excel 解析通过 RPC 调用
 *        Excel 解析子服务完成, 数据库表名称由本层基于解析结果生成,
 *        格式为 {file_id}_{worksheet_name}
 */
class FileBusiness
{
public:
    /**
     * @brief 构造函数, 注入业务依赖对象, 所有成员变量在构造函数中完成初始化
     * @param file_data 文件数据访问对象, 由上层创建并统一管理
     * @param worksheet_data WorkSheet 数据访问对象, 由上层创建并统一管理
     * @param channel_manager RPC 信道管理对象, 由上层创建并统一管理
     */
    FileBusiness(std::shared_ptr<FileData> file_data,
                 std::shared_ptr<WorkSheetData> worksheet_data,
                 cpp_toolkit::ChannelManager::Ptr channel_manager);

    /**
     * @brief 上传文件(Excel/SQLite)信息, 构建 FileInfo 结构体并保存到数据库;
     *        文件 ID 使用 uuid 生成器生成, 上传时间为当前时间戳,
     *        fastdfs_file_id 为空字符串(文件数据尚未上传)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param session_id 会话 ID
     * @param file_name 文件名
     * @param file_extension 文件扩展名
     * @param file_size 文件大小, 单位字节
     * @return 生成的文件 ID
     */
    std::string UploadFileInfo(const std::string& request_id, const std::string& user_id,
                               const std::string& session_id, const std::string& file_name,
                               const std::string& file_extension, unsigned long long file_size);

    /**
     * @brief 获取文件信息, 读策略(Cache-Aside): 先查缓存, 未命中时查数据库并回填缓存;
     *        并校验当前用户与文件属主是否一致
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @return 文件信息
     */
    FileInfo GetFileInfo(const std::string& request_id, const std::string& user_id,
                         const std::string& file_id);

    /**
     * @brief 删除文件信息, 校验文件属主后删除数据库与缓存中的文件信息,
     *        以及 Excel 对应的 WorkSheet 数据(数据库与缓存)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     */
    void DeleteFileInfo(const std::string& request_id, const std::string& user_id,
                        const std::string& file_id);

    /**
     * @brief 上传 Excel 文件数据, 完整流程 : 上传文件到 FastDFS 并更新数据库 ->
     *        RPC 调用 Excel 解析子服务(传递 FastDFS 文件 ID, 由解析子服务自行下载文件)
     *        获取工作表名称并解析 -> 生成数据库表名称保存 WorkSheet 信息
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @param file_content 文件二进制内容
     */
    void UploadFileData(const std::string& request_id, const std::string& user_id,
                        const std::string& file_id, const std::string& file_content);

    /**
     * @brief 下载 Excel 文件数据, 校验文件属主后从 FastDFS 获取文件内容
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @return 文件二进制内容
     */
    std::string DownloadFileData(const std::string& request_id, const std::string& user_id,
                                 const std::string& file_id);

    /**
     * @brief 删除文件, 完整流程 : 校验文件属主 -> 删除 FastDFS 中的文件数据 ->
     *        删除 WorkSheet 元信息(数据库与缓存) -> 删除数据库文件信息 -> 删除缓存文件信息
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     */
    void DeleteFile(const std::string& request_id, const std::string& user_id,
                    const std::string& file_id);

    /**
     * @brief 获取用户上传的所有文件信息列表
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @return 文件信息列表, 用户没有上传过文件时返回空列表
     */
    std::vector<FileInfo> GetFileList(const std::string& request_id, const std::string& user_id);

    /**
     * @brief 预览 Excel 文件, 校验文件属主后返回文件信息, 解析结果从数据库子服务获取(暂未实现)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @param page_number 预览数据页号(预留, 供数据库子服务分页查询使用)
     * @param page_size 预览数据每页条数(预留, 供数据库子服务分页查询使用)
     * @return 文件信息
     */
    FileInfo PreviewExcel(const std::string& request_id, const std::string& user_id,
                          const std::string& file_id, int page_number, int page_size);

    /**
     * @brief 上传 SQLite 文件数据, 实现逻辑与上传 Excel 文件相同(上传文件到
     *        FastDFS 并更新数据库), 但无需 Excel 解析流程
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @param file_content 文件二进制内容
     */
    void UploadSQLiteFileData(const std::string& request_id, const std::string& user_id,
                              const std::string& file_id, const std::string& file_content);

    /**
     * @brief 获取 SQLite 文件, 校验文件属主后返回文件对应的 FastDFS 文件 ID,
     *        sqlite 文件由数据库子服务负责下载保存到其本地目录并进行解析(暂未实现)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @return FastDFS 中的文件 ID
     */
    std::string GetSQLiteFile(const std::string& request_id, const std::string& user_id,
                              const std::string& file_id);

    /**
     * @brief 关联文件和聊天会话, 校验文件属主后将聊天会话 ID 设置到
     *        文件信息中(写策略 Cache-Aside: 先改数据库再删缓存)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @param chat_session_id 聊天会话 ID
     */
    void HandleFileChatSessionMap(const std::string& request_id, const std::string& user_id,
                                  const std::string& file_id, const std::string& chat_session_id);

    /**
     * @brief 获取文件对应的所有 WorkSheet 数据库表名列表, 读策略(Cache-Aside):
     *        先查 WorkSheet 缓存, 未命中时查数据库并回填缓存;
     *        文件存在但没有 WorkSheet 信息时(如 SQLite 文件)返回空列表
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @return WorkSheet 对应的数据库表名列表
     */
    std::vector<std::string> GetWorksheetDBTables(const std::string& request_id,
                                                  const std::string& user_id,
                                                  const std::string& file_id);

private:
    /**
     * @brief 获取文件信息并校验文件属主, 读策略(Cache-Aside): 先查缓存,
     *        未命中时查数据库并回填缓存; 文件不存在或属主不一致时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     * @param file_id 文件 ID
     * @return 文件信息
     */
    FileInfo GetFileInfoWithOwnerCheck(const std::string& request_id, const std::string& user_id,
                                       const std::string& file_id);

    // 文件数据访问对象, 操作管理文件元信息
    std::shared_ptr<FileData> file_data_;

    // WorkSheet 数据访问对象, 操作管理 WorkSheet 工作表信息
    std::shared_ptr<WorkSheetData> worksheet_data_;

    // RPC 信道管理对象, 用于获取 Excel 解析子服务通信信道
    cpp_toolkit::ChannelManager::Ptr channel_manager_;
};

} // namespace file_service
} // namespace chat_excel
