#include "svc_file_service/file_business.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <brpc/controller.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include <excel_parse_service.pb.h>
#include "common/exception.h"
// FastDFS 客户端头文件必须最后导入 : 其依赖的 fastcommon 头文件会向全局作用域
// 定义 byte 等宏, 先行导入会破坏 fmt/boost 等后续头文件的解析
#include <cpp-toolkit/fdfs.h>

namespace chat_excel
{
namespace file_service
{

namespace proto = ::chat_excel_proto::excel_parse_service;

namespace
{

// Excel 解析子服务名称, 用于从信道管理对象获取通信信道
constexpr const char* kExcelParseServiceName = "ExcelParseService";

// Excel 解析子服务 RPC 调用超时时间(毫秒), Excel 解析耗时较长, 设置 30 秒
constexpr int kExcelParseRpcTimeoutMs = 30 * 1000;

/**
 * @brief 清洗表名中的非法字符, 字母/数字/汉字/下划线原样保留, 其余字符替换为下划线
 * @param name 待清洗的名称片段
 * @return 清洗后的名称片段
 */
std::string SanitizeTableNamePart(const std::string& name)
{
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char ch : name)
    {
        const unsigned char current_byte = static_cast<unsigned char>(ch);
        // 汉字为 UTF-8 多字节编码, 每个字节的最高位为 1, 原样保留
        if (ch == '_' || current_byte >= 0x80 || std::isalnum(current_byte) != 0)
        {
            sanitized.push_back(ch);
        }
        else
        {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

/**
 * @brief 基于解析结果生成数据库表名称, 格式为 {file_id}_{worksheet_name},
 *        名称中的字母/数字/汉字/下划线保留, 其余字符(含 uuid 中的连字符)替换为下划线
 * @param file_id 文件 ID
 * @param worksheet_name 工作表名称
 * @return 生成的数据库表名称
 */
std::string GenerateTableName(const std::string& file_id, const std::string& worksheet_name)
{
    return SanitizeTableNamePart(file_id) + "_" + SanitizeTableNamePart(worksheet_name);
}

/**
 * @brief 上传文件二进制内容到 FastDFS
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param file_id 文件 ID
 * @param file_content 文件二进制内容
 * @return FastDFS 中的文件 ID
 */
std::string UploadBufferToFdfs(const std::string& request_id, const std::string& file_id,
                               const std::string& file_content)
{
    const std::optional<std::string> fastdfs_file_id =
        cpp_toolkit::FdfsClient::UploadFromBuffer(file_content);
    if (!fastdfs_file_id)
    {
        ERR("上传文件数据到 FastDFS 失败, request_id: {}, file_id: {}, 文件大小: {}",
            request_id, file_id, file_content.size());
        throw ChatExcelException(ErrorCode::FILE_FDFS_UPLOAD_ERROR);
    }
    INFO("上传文件数据到 FastDFS 成功, request_id: {}, file_id: {}, fastdfs_file_id: {}",
         request_id, file_id, *fastdfs_file_id);
    return *fastdfs_file_id;
}

/**
 * @brief 调用 Excel 解析子服务 RPC 接口获取 Excel 文件的所有工作表名称,
 *        传递 FastDFS 文件 ID, 由 Excel 解析子服务自行下载文件(支持分布式部署)
 * @param channel_manager RPC 信道管理对象
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param fastdfs_file_id FastDFS 中的文件 ID
 * @return 工作表名称列表
 */
std::vector<std::string> GetWorksheetNamesFromRpc(const cpp_toolkit::ChannelManager::Ptr& channel_manager,
                                                  const std::string& request_id,
                                                  const std::string& fastdfs_file_id)
{
    // 通过信道管理对象获取 Excel 解析子服务通信信道
    cpp_toolkit::ChannelPtr channel = channel_manager->GetChannel(kExcelParseServiceName);
    if (channel == nullptr)
    {
        ERR("获取 Excel 解析子服务信道失败, request_id: {}, 服务名称: {}",
            request_id, kExcelParseServiceName);
        throw ChatExcelException(ErrorCode::FILE_EXCEL_PARSE_RPC_ERROR);
    }

    // 构建 RPC 请求并同步调用获取工作表列表接口
    proto::GetWorksheetsRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_fastdfs_file_id(fastdfs_file_id);

    proto::ExcelParserService_Stub excel_parse_service_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kExcelParseRpcTimeoutMs);
    proto::GetWorksheetsResponse rpc_response;
    excel_parse_service_stub.GetWorksheets(&controller, &rpc_request, &rpc_response, nullptr);

    // 检测 RPC 调用是否成功(网络/超时/信道层面的失败)
    if (controller.Failed())
    {
        ERR("Excel 解析子服务 RPC 调用失败, request_id: {}, fastdfs_file_id: {}, 错误信息: {}",
            request_id, fastdfs_file_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::FILE_EXCEL_PARSE_RPC_ERROR);
    }

    // 检测 Excel 解析子服务业务处理结果, 业务错误码透传给上层调用方
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("获取工作表名称失败, request_id: {}, fastdfs_file_id: {}, 错误码: {}, 错误信息: {}",
            request_id, fastdfs_file_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }

    return {rpc_response.worksheets().begin(), rpc_response.worksheets().end()};
}

/**
 * @brief 调用 Excel 解析子服务 RPC 接口解析 Excel 文件的所有工作表数据
 *        (包括表头, 列信息, worksheet 数据), 传递 FastDFS 文件 ID,
 *        由 Excel 解析子服务自行下载文件(支持分布式部署)
 * @param channel_manager RPC 信道管理对象
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param fastdfs_file_id FastDFS 中的文件 ID
 * @param worksheet_names 待解析的工作表名称列表
 * @return 解析后的工作表数据列表
 */
std::vector<proto::WorksheetData> ParseWorksheetsFromRpc(
    const cpp_toolkit::ChannelManager::Ptr& channel_manager, const std::string& request_id,
    const std::string& fastdfs_file_id, const std::vector<std::string>& worksheet_names)
{
    // 通过信道管理对象获取 Excel 解析子服务通信信道
    cpp_toolkit::ChannelPtr channel = channel_manager->GetChannel(kExcelParseServiceName);
    if (channel == nullptr)
    {
        ERR("获取 Excel 解析子服务信道失败, request_id: {}, 服务名称: {}",
            request_id, kExcelParseServiceName);
        throw ChatExcelException(ErrorCode::FILE_EXCEL_PARSE_RPC_ERROR);
    }

    // 构建 RPC 请求并同步调用解析 Excel 文件接口
    proto::ParseExcelRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_fastdfs_file_id(fastdfs_file_id);
    for (const std::string& worksheet_name : worksheet_names)
    {
        rpc_request.add_worksheets(worksheet_name);
    }

    proto::ExcelParserService_Stub excel_parse_service_stub(channel.get());
    brpc::Controller controller;
    controller.set_timeout_ms(kExcelParseRpcTimeoutMs);
    proto::ParseExcelResponse rpc_response;
    excel_parse_service_stub.ParseExcel(&controller, &rpc_request, &rpc_response, nullptr);

    // 检测 RPC 调用是否成功(网络/超时/信道层面的失败)
    if (controller.Failed())
    {
        ERR("Excel 解析子服务 RPC 调用失败, request_id: {}, fastdfs_file_id: {}, 错误信息: {}",
            request_id, fastdfs_file_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::FILE_EXCEL_PARSE_RPC_ERROR);
    }

    // 检测 Excel 解析子服务业务处理结果, 业务错误码透传给上层调用方
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("解析 Excel 文件失败, request_id: {}, fastdfs_file_id: {}, 错误码: {}, 错误信息: {}",
            request_id, fastdfs_file_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(static_cast<ErrorCode>(rpc_response.error_code()));
    }

    // 拷贝解析结果, 后续用于生成数据库表名称与保存 WorkSheet 信息
    std::vector<proto::WorksheetData> worksheet_datas(rpc_response.worksheets().begin(),
                                                      rpc_response.worksheets().end());
    INFO("解析 Excel 文件成功, request_id: {}, fastdfs_file_id: {}, 解析 worksheet 个数: {}",
         request_id, fastdfs_file_id, worksheet_datas.size());
    return worksheet_datas;
}

} // namespace

FileBusiness::FileBusiness(std::shared_ptr<FileData> file_data,
                           std::shared_ptr<WorkSheetData> worksheet_data,
                           cpp_toolkit::ChannelManager::Ptr channel_manager)
    : file_data_(std::move(file_data)),
      worksheet_data_(std::move(worksheet_data)),
      channel_manager_(std::move(channel_manager))
{
    INFO("文件业务逻辑对象构建完成");
}

std::string FileBusiness::UploadFileInfo(const std::string& request_id, const std::string& user_id,
                                         const std::string& session_id, const std::string& file_name,
                                         const std::string& file_extension, unsigned long long file_size)
{
    // 构建文件信息, 文件 ID 使用 uuid 生成器生成(去掉连字符, 36 字符 -> 32 字符,
    // 与文件信息表 file_id 字段 VARCHAR(32) 的长度限制匹配), 上传时间为当前时间戳,
    // fastdfs_file_id 为空字符串(文件数据尚未上传)
    FileInfo file_info;
    file_info.file_id = cpp_toolkit::UuidUtil::GenerateUuidV4();
    file_info.file_id.erase(std::remove(file_info.file_id.begin(), file_info.file_id.end(), '-'),
                            file_info.file_id.end());
    file_info.file_name = file_name;
    file_info.file_extension = file_extension;
    file_info.file_size = file_size;
    file_info.file_upload_time = static_cast<unsigned long long>(std::time(nullptr));
    file_info.fastdfs_file_id = "";
    file_info.user_id = user_id;
    file_info.session_id = session_id;

    // 保存文件信息到数据库
    file_data_->SaveFile(file_info);
    INFO("上传文件信息成功, request_id: {}, file_id: {}, 文件名: {}, user_id: {}",
         request_id, file_info.file_id, file_info.file_name, user_id);
    return file_info.file_id;
}

FileInfo FileBusiness::GetFileInfo(const std::string& request_id, const std::string& user_id,
                                   const std::string& file_id)
{
    return GetFileInfoWithOwnerCheck(request_id, user_id, file_id);
}

void FileBusiness::DeleteFileInfo(const std::string& request_id, const std::string& user_id,
                                  const std::string& file_id)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 删除 Excel 对应的 WorkSheet 数据(MySQL 与缓存)
    worksheet_data_->DeleteWorkSheetByFileId(file_info.file_id);
    worksheet_data_->DeleteWorkSheetFromCache(file_info.file_id);

    // 删除数据库中的文件信息与缓存中的文件信息
    file_data_->DeleteFileByFileId(file_info.file_id);
    file_data_->DeleteFileByFileIdFromCache(file_info.file_id);
    INFO("删除文件信息成功, request_id: {}, file_id: {}", request_id, file_info.file_id);
}

void FileBusiness::UploadFileData(const std::string& request_id, const std::string& user_id,
                                  const std::string& file_id, const std::string& file_content)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 上传文件数据到 FastDFS
    const std::string fastdfs_file_id = UploadBufferToFdfs(request_id, file_info.file_id, file_content);

    // 更新 MySQL 中的 fastdfs_file_id 字段(写策略 Cache-Aside: 先改数据库再删缓存)
    FileInfo updated_file_info = file_info;
    updated_file_info.fastdfs_file_id = fastdfs_file_id;
    file_data_->UpdateFile(updated_file_info);
    file_data_->DeleteFileByFileIdFromCache(file_info.file_id);

    // 调用 Excel 解析子服务 RPC 接口, 传递 FastDFS 文件 ID(由解析子服务自行下载文件),
    // 获取 Excel 所有工作表名称
    const std::vector<std::string> worksheet_names =
        GetWorksheetNamesFromRpc(channel_manager_, request_id, fastdfs_file_id);

    // 调用 Excel 解析子服务 RPC 接口解析所有 WorkSheet 工作表信息
    // (包括表头, 列信息, worksheet 数据)
    const std::vector<proto::WorksheetData> worksheet_datas =
        ParseWorksheetsFromRpc(channel_manager_, request_id, fastdfs_file_id, worksheet_names);

    // TODO: 通过数据库子服务将解析的 Excel 数据保存到数据库中,
    //       表名称使用 {file_id}_{worksheet_name}

    // 基于解析结果生成数据库表名称, 构建 WorkSheet 信息保存到 WorkSheet 表
    std::vector<WorkSheetInfo> worksheet_list;
    worksheet_list.reserve(worksheet_datas.size());
    for (const proto::WorksheetData& worksheet_data : worksheet_datas)
    {
        WorkSheetInfo worksheet_info;
        worksheet_info.file_id = file_info.file_id;
        worksheet_info.worksheet_name = worksheet_data.name();
        worksheet_info.table_name = GenerateTableName(file_info.file_id, worksheet_data.name());
        worksheet_list.push_back(std::move(worksheet_info));
    }
    worksheet_data_->SaveWorkSheets(file_info.file_id, worksheet_list);

    INFO("上传文件数据成功, request_id: {}, file_id: {}, worksheet 个数: {}",
         request_id, file_info.file_id, worksheet_list.size());
}

std::string FileBusiness::DownloadFileData(const std::string& request_id, const std::string& user_id,
                                           const std::string& file_id)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 文件数据尚未上传到 FastDFS 时无法下载
    if (file_info.fastdfs_file_id.empty())
    {
        ERR("文件数据尚未上传到 FastDFS, request_id: {}, file_id: {}", request_id, file_id);
        throw ChatExcelException(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR);
    }

    // 通过 fastdfs_file_id 从 FastDFS 下载文件数据
    std::string file_content;
    if (!cpp_toolkit::FdfsClient::DownloadToBuffer(file_info.fastdfs_file_id, file_content))
    {
        ERR("从 FastDFS 下载文件数据失败, request_id: {}, file_id: {}, fastdfs_file_id: {}",
            request_id, file_id, file_info.fastdfs_file_id);
        throw ChatExcelException(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR);
    }
    INFO("下载文件数据成功, request_id: {}, file_id: {}, 文件大小: {}",
         request_id, file_id, file_content.size());
    return file_content;
}

void FileBusiness::DeleteFile(const std::string& request_id, const std::string& user_id,
                              const std::string& file_id)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 删除 FastDFS 中的文件数据, 文件数据尚未上传时跳过删除
    if (!file_info.fastdfs_file_id.empty())
    {
        if (!cpp_toolkit::FdfsClient::DeleteFile(file_info.fastdfs_file_id))
        {
            ERR("从 FastDFS 删除文件数据失败, request_id: {}, file_id: {}, fastdfs_file_id: {}",
                request_id, file_id, file_info.fastdfs_file_id);
            throw ChatExcelException(ErrorCode::FILE_FDFS_DELETE_ERROR);
        }
    }

    // TODO: 调用数据库子服务, 删除数据库中对应的 WorkSheet 表数据

    // 删除 WorkSheet 元信息(MySQL 与缓存)
    worksheet_data_->DeleteWorkSheetByFileId(file_info.file_id);
    worksheet_data_->DeleteWorkSheetFromCache(file_info.file_id);

    // 删除 MySQL 中的文件信息
    file_data_->DeleteFileByFileId(file_info.file_id);

    // 删除 Redis 缓存中的文件信息
    file_data_->DeleteFileByFileIdFromCache(file_info.file_id);
    INFO("删除文件成功, request_id: {}, file_id: {}", request_id, file_info.file_id);
}

std::vector<FileInfo> FileBusiness::GetFileList(const std::string& request_id,
                                                const std::string& user_id)
{
    // 通过用户 ID 获取用户上传的所有文件列表(所有文件元信息)
    std::vector<FileInfo> file_list = file_data_->GetFileListByUserId(user_id);
    INFO("获取用户文件列表成功, request_id: {}, user_id: {}, 文件个数: {}",
         request_id, user_id, file_list.size());
    return file_list;
}

FileInfo FileBusiness::PreviewExcel(const std::string& request_id, const std::string& user_id,
                                    const std::string& file_id, int page_number, int page_size)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // TODO: 通过数据库子服务, 从数据库中获取 Excel 文件的解析结果
    //       (page_number 与 page_size 用于解析结果分页查询)
    (void)page_number;
    (void)page_size;

    INFO("预览 Excel 文件成功(解析结果获取暂未实现), request_id: {}, file_id: {}",
         request_id, file_id);
    return file_info;
}

void FileBusiness::UploadSQLiteFileData(const std::string& request_id, const std::string& user_id,
                                        const std::string& file_id, const std::string& file_content)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 上传 sqlite 文件数据到 FastDFS(实现逻辑与上传 Excel 文件相同, 无需 Excel 解析流程)
    const std::string fastdfs_file_id = UploadBufferToFdfs(request_id, file_info.file_id, file_content);

    // 更新 MySQL 中的 fastdfs_file_id 字段(写策略 Cache-Aside: 先改数据库再删缓存)
    FileInfo updated_file_info = file_info;
    updated_file_info.fastdfs_file_id = fastdfs_file_id;
    file_data_->UpdateFile(updated_file_info);
    file_data_->DeleteFileByFileIdFromCache(file_info.file_id);
    INFO("上传 SQLite 文件数据成功, request_id: {}, file_id: {}", request_id, file_info.file_id);
}

std::string FileBusiness::GetSQLiteFile(const std::string& request_id, const std::string& user_id,
                                        const std::string& file_id)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 文件数据尚未上传到 FastDFS 时无法获取
    if (file_info.fastdfs_file_id.empty())
    {
        ERR("文件数据尚未上传到 FastDFS, request_id: {}, file_id: {}", request_id, file_id);
        throw ChatExcelException(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR);
    }

    // TODO: 调用数据库子服务, 传入 FastDFS 文件 ID, 由数据库子服务下载 sqlite 文件
    //       保存到其本地目录并进行解析
    INFO("获取 SQLite 文件信息成功, request_id: {}, file_id: {}, fastdfs_file_id: {}",
         request_id, file_id, file_info.fastdfs_file_id);
    return file_info.fastdfs_file_id;
}

void FileBusiness::HandleFileChatSessionMap(const std::string& request_id, const std::string& user_id,
                                            const std::string& file_id,
                                            const std::string& chat_session_id)
{
    // TODO: 实现文件和聊天会话的关联管理
    INFO("关联文件和聊天会话暂未实现, request_id: {}, user_id: {}, file_id: {}, 会话 ID: {}",
         request_id, user_id, file_id, chat_session_id);
}

std::vector<std::string> FileBusiness::GetWorksheetDBTables(const std::string& request_id,
                                                            const std::string& user_id,
                                                            const std::string& file_id)
{
    // 获取文件信息并校验文件属主
    const FileInfo file_info = GetFileInfoWithOwnerCheck(request_id, user_id, file_id);

    // 读策略(Cache-Aside): 先从缓存中读取 WorkSheet 信息
    std::vector<WorkSheetInfo> worksheet_list =
        worksheet_data_->GetWorkSheetListByFileIdFromCache(file_info.file_id);
    if (worksheet_list.empty())
    {
        // 缓存未命中, 到 MySQL 中读取 WorkSheet 信息并回填缓存
        worksheet_list = worksheet_data_->GetWorkSheetListByFileId(file_info.file_id);
        if (!worksheet_list.empty())
        {
            worksheet_data_->SaveWorkSheetToCache(file_info.file_id, worksheet_list);
        }
    }

    // 提取每个 WorkSheet 数据存储的数据库表名称
    std::vector<std::string> table_names;
    table_names.reserve(worksheet_list.size());
    for (const WorkSheetInfo& worksheet_info : worksheet_list)
    {
        table_names.push_back(worksheet_info.table_name);
    }
    INFO("获取 WorkSheet 数据库表名列表成功, request_id: {}, file_id: {}, 表名个数: {}",
         request_id, file_info.file_id, table_names.size());
    return table_names;
}

FileInfo FileBusiness::GetFileInfoWithOwnerCheck(const std::string& request_id,
                                                 const std::string& user_id,
                                                 const std::string& file_id)
{
    // 读策略(Cache-Aside): 先从缓存中读取文件信息
    std::optional<FileInfo> file_info = file_data_->GetFileByFileIdFromCache(file_id);
    if (!file_info)
    {
        // 缓存未命中, 到 MySQL 中读取文件信息
        file_info = file_data_->GetFileByFileId(file_id);
        if (!file_info)
        {
            ERR("文件信息不存在, request_id: {}, file_id: {}", request_id, file_id);
            throw ChatExcelException(ErrorCode::FILE_DATA_NOT_FOUND);
        }

        // 将数据库读取到的文件信息添加到缓存中
        file_data_->SaveFileToCache(*file_info);
    }

    // 检查当前 user_id 与文件信息中的 user_id 是否一致
    if (file_info->user_id != user_id)
    {
        ERR("当前用户与文件属主不一致, request_id: {}, file_id: {}, 当前用户: {}, 文件属主: {}",
            request_id, file_id, user_id, file_info->user_id);
        throw ChatExcelException(ErrorCode::FILE_USER_MISMATCH);
    }
    return *file_info;
}

} // namespace file_service
} // namespace chat_excel
