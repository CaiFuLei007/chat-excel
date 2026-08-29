#include "svc_file_service/file_service_impl.h"

#include <string>
#include <utility>
#include <vector>
#include <brpc/closure_guard.h>
#include <brpc/controller.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace file_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::file_service;

namespace
{

// SQLite 文件固定扩展名
constexpr const char* kSqliteFileExtension = ".db";

/**
 * @brief 将错误码与错误码描述填充到 RPC 响应中
 * @param response RPC 响应对象
 * @param error_code 错误码
 * @param detail 错误详情, 非空时追加到错误码描述之后, 供上层定位具体错误原因
 */
template <typename ResponseType>
void SetErrorResponse(ResponseType* response, ErrorCode error_code, const std::string& detail = "")
{
    response->set_error_code(static_cast<int>(error_code));
    if (detail.empty())
    {
        response->set_error_msg(ErrorMessage(error_code));
    }
    else
    {
        response->set_error_msg(ErrorMessage(error_code) + " : " + detail);
    }
}

/**
 * @brief 从 RPC 控制器中读取请求 attachment 携带的文件二进制内容
 * @param controller RPC 控制器, brpc 中 attachment 无需序列化且零拷贝传输
 * @return attachment 中的文件二进制内容, attachment 为空时返回空字符串
 */
std::string GetRequestAttachment(google::protobuf::RpcController* controller)
{
    // brpc 服务端传入的控制器实际类型为 brpc::Controller, 安全向下转换
    brpc::Controller* brpc_controller = static_cast<brpc::Controller*>(controller);
    return brpc_controller->request_attachment().to_string();
}

/**
 * @brief 将文件二进制内容写入 RPC 控制器的响应 attachment 中
 * @param controller RPC 控制器, brpc 中 attachment 无需序列化且零拷贝传输
 * @param file_content 待写入的文件二进制内容
 */
void SetResponseAttachment(google::protobuf::RpcController* controller, const std::string& file_content)
{
    // brpc 服务端传入的控制器实际类型为 brpc::Controller, 安全向下转换
    brpc::Controller* brpc_controller = static_cast<brpc::Controller*>(controller);
    brpc_controller->response_attachment().append(file_content);
}

} // namespace

FileServiceImpl::FileServiceImpl(std::shared_ptr<FileBusiness> file_business)
    : file_business_(std::move(file_business))
{
    INFO("文件子服务 RPC 接口实现对象构建完成");
}

void FileServiceImpl::UploadFileInfo(google::protobuf::RpcController* /*controller*/,
                                     const proto::UploadFileInfoRequest* request,
                                     proto::UploadFileInfoResponse* response,
                                     google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID、用户 ID、文件信息、文件名与文件扩展名均不能为空
        if (request->session_id().empty())
        {
            ERR("UploadFileInfo 接口请求参数错误, session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_SESSION_ID_EMPTY);
            return;
        }
        else if (request->user_id().empty())
        {
            ERR("UploadFileInfo 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (!request->has_file_info())
        {
            ERR("UploadFileInfo 接口请求参数错误, file_info 文件信息缺失, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_INFO_EMPTY);
            return;
        }
        else if (request->file_info().filename().empty())
        {
            ERR("UploadFileInfo 接口请求参数错误, filename 为空, request_id: {}, user_id: {}",
                request->request_id(), request->user_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_NAME_EMPTY);
            return;
        }
        else if (request->file_info().file_ext().empty())
        {
            ERR("UploadFileInfo 接口请求参数错误, file_ext 为空, request_id: {}, 文件名: {}",
                request->request_id(), request->file_info().filename());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_EXT_EMPTY);
            return;
        }

        // 调用业务逻辑层上传文件信息, 失败时业务逻辑层抛出异常
        const std::string file_id = file_business_->UploadFileInfo(
            request->request_id(), request->user_id(), request->session_id(),
            request->file_info().filename(), request->file_info().file_ext(),
            static_cast<unsigned long long>(request->file_info().file_size()));

        // 将上传结果填充到响应中
        response->mutable_result()->set_file_id(file_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("UploadFileInfo 接口业务处理异常, request_id: {}, 文件名: {}, 错误信息: {}",
            request->request_id(), request->file_info().filename(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("UploadFileInfo 接口非预期异常, request_id: {}, 文件名: {}, 错误信息: {}",
            request->request_id(), request->file_info().filename(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::GetFileInfo(google::protobuf::RpcController* /*controller*/,
                                  const proto::GetFileInfoRequest* request,
                                  proto::GetFileInfoResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("GetFileInfo 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("GetFileInfo 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取文件信息, 文件不存在时业务逻辑层抛出异常
        const FileInfo file_info = file_business_->GetFileInfo(request->request_id(),
                                                               request->user_id(), request->file_id());

        // 将文件详情填充到响应结果中, proto 的 FileDetail 消息使用命名空间别名限定
        proto::FileDetail* result_file_detail = response->mutable_result();
        result_file_detail->set_file_id(file_info.file_id);
        result_file_detail->set_file_name(file_info.file_name);
        result_file_detail->set_file_size(static_cast<int64_t>(file_info.file_size));
        result_file_detail->set_upload_time(static_cast<int64_t>(file_info.file_upload_time));
        result_file_detail->set_file_ext(file_info.file_extension);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetFileInfo 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetFileInfo 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::UploadFile(google::protobuf::RpcController* controller,
                                 const proto::UploadFileRequest* request,
                                 proto::UploadFileResponse* response,
                                 google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("UploadFile 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("UploadFile 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 从请求 attachment 中读取文件二进制内容, 文件数据为空视为参数错误
        const std::string file_content = GetRequestAttachment(controller);
        if (file_content.empty())
        {
            ERR("UploadFile 接口请求参数错误, attachment 文件数据为空, request_id: {}, file_id: {}",
                request->request_id(), request->file_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_CONTENT_EMPTY);
            return;
        }

        // 调用业务逻辑层上传文件数据, 失败时业务逻辑层抛出异常
        file_business_->UploadFileData(request->request_id(), request->user_id(),
                                        request->file_id(), file_content);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("UploadFile 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("UploadFile 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::DownloadFile(google::protobuf::RpcController* controller,
                                   const proto::DownloadFileRequest* request,
                                   proto::DownloadFileResponse* response,
                                   google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("DownloadFile 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("DownloadFile 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层下载文件数据, 文件不存在时业务逻辑层抛出异常
        const std::string file_content = file_business_->DownloadFileData(request->request_id(),
                                                                          request->user_id(),
                                                                          request->file_id());

        // 将文件数据写入响应 attachment 中, 零拷贝传输给调用方
        SetResponseAttachment(controller, file_content);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DownloadFile 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DownloadFile 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::DeleteFile(google::protobuf::RpcController* /*controller*/,
                                 const proto::DeleteFileRequest* request,
                                 proto::DeleteFileResponse* response,
                                 google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("DeleteFile 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("DeleteFile 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层删除文件, 文件不存在时业务逻辑层抛出异常
        file_business_->DeleteFile(request->request_id(), request->user_id(), request->file_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DeleteFile 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DeleteFile 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::PreviewExcel(google::protobuf::RpcController* /*controller*/,
                                   const proto::PreviewExcelRequest* request,
                                   proto::PreviewExcelResponse* response,
                                   google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空, 分页参数 page_number 与
        // page_size 均从 1 开始, 0 视为参数错误
        if (request->user_id().empty())
        {
            ERR("PreviewExcel 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("PreviewExcel 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }
        else if (request->page_number() < 1)
        {
            ERR("PreviewExcel 接口请求参数错误, page_number 无效, request_id: {}, file_id: {}, page_number: {}",
                request->request_id(), request->file_id(), request->page_number());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_PAGE_NUMBER_ERROR);
            return;
        }
        else if (request->page_size() < 1)
        {
            ERR("PreviewExcel 接口请求参数错误, page_size 无效, request_id: {}, file_id: {}, page_size: {}",
                request->request_id(), request->file_id(), request->page_size());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_PAGE_SIZE_ERROR);
            return;
        }

        // 调用业务逻辑层预览 Excel 文件, 文件不存在时业务逻辑层抛出异常
        const FileInfo file_info = file_business_->PreviewExcel(request->request_id(),
                                                                request->user_id(), request->file_id(),
                                                                request->page_number(),
                                                                request->page_size());

        // 将文件信息填充到响应结果中
        proto::PreviewExcelResult* result = response->mutable_result();
        result->set_file_id(file_info.file_id);
        result->set_file_name(file_info.file_name);
        result->set_file_size(static_cast<int64_t>(file_info.file_size));
        result->set_file_ext(file_info.file_extension);
        // TODO: 通过数据库子服务获取 Excel 文件的解析结果并填充 excel_data
        //       (page_number 与 page_size 用于解析结果分页查询)

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("PreviewExcel 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("PreviewExcel 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::GetFileList(google::protobuf::RpcController* /*controller*/,
                                  const proto::GetFileListRequest* request,
                                  proto::GetFileListResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 不能为空
        if (request->user_id().empty())
        {
            ERR("GetFileList 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取用户上传的所有文件列表
        const std::vector<FileInfo> file_list = file_business_->GetFileList(request->request_id(),
                                                                            request->user_id());

        // 将文件列表填充到响应结果中
        proto::GetFileListResult* result = response->mutable_result();
        for (const FileInfo& file_info : file_list)
        {
            proto::FileListItem* file_list_item = result->add_file_list();
            file_list_item->set_file_id(file_info.file_id);
            file_list_item->set_file_name(file_info.file_name);
            file_list_item->set_file_size(static_cast<int64_t>(file_info.file_size));
            file_list_item->set_upload_time(static_cast<int64_t>(file_info.file_upload_time));
            // TODO: 文件与聊天会话的关联暂未实现, chat_session_id 填充空字符串
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetFileList 接口业务处理异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetFileList 接口非预期异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::HandleFileChatSessionMap(google::protobuf::RpcController* /*controller*/,
                                               const proto::HandleFileChatSessionMapRequest* request,
                                               proto::HandleFileChatSessionMapResponse* response,
                                               google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID、文件 ID 与聊天会话 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("HandleFileChatSessionMap 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("HandleFileChatSessionMap 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }
        else if (request->chat_session_id().empty())
        {
            ERR("HandleFileChatSessionMap 接口请求参数错误, chat_session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_CHAT_SESSION_ID_EMPTY);
            return;
        }

        // TODO: 调用业务逻辑层关联文件和聊天会话, 当前业务逻辑层为空实现

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("HandleFileChatSessionMap 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("HandleFileChatSessionMap 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::UploadSQLiteFile(google::protobuf::RpcController* controller,
                                       const proto::UploadSQLiteFileRequest* request,
                                       proto::UploadSQLiteFileResponse* response,
                                       google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID、用户 ID 与文件名均不能为空
        if (request->session_id().empty())
        {
            ERR("UploadSQLiteFile 接口请求参数错误, session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_SESSION_ID_EMPTY);
            return;
        }
        else if (request->user_id().empty())
        {
            ERR("UploadSQLiteFile 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->filename().empty())
        {
            ERR("UploadSQLiteFile 接口请求参数错误, filename 为空, request_id: {}, user_id: {}",
                request->request_id(), request->user_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_NAME_EMPTY);
            return;
        }

        // 从请求 attachment 中读取文件二进制内容, 文件数据为空视为参数错误
        const std::string file_content = GetRequestAttachment(controller);
        if (file_content.empty())
        {
            ERR("UploadSQLiteFile 接口请求参数错误, attachment 文件数据为空, request_id: {}, 文件名: {}",
                request->request_id(), request->filename());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_CONTENT_EMPTY);
            return;
        }

        // 调用业务逻辑层上传文件信息生成文件 ID, 扩展名固定为 .db,
        // 文件大小取 attachment 中文件数据的大小
        const std::string file_id = file_business_->UploadFileInfo(
            request->request_id(), request->user_id(), request->session_id(),
            request->filename(), kSqliteFileExtension, file_content.size());

        // 调用业务逻辑层上传 SQLite 文件数据到 FastDFS, 失败时业务逻辑层抛出异常
        file_business_->UploadSQLiteFileData(request->request_id(), request->user_id(),
                                             file_id, file_content);

        // 将上传结果填充到响应中
        response->mutable_result()->set_file_id(file_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("UploadSQLiteFile 接口业务处理异常, request_id: {}, 文件名: {}, 错误信息: {}",
            request->request_id(), request->filename(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("UploadSQLiteFile 接口非预期异常, request_id: {}, 文件名: {}, 错误信息: {}",
            request->request_id(), request->filename(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::GetSQLiteFile(google::protobuf::RpcController* /*controller*/,
                                    const proto::GetSQLiteFileRequest* request,
                                    proto::GetSQLiteFileResponse* response,
                                    google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("GetSQLiteFile 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("GetSQLiteFile 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取 SQLite 文件对应的 FastDFS 文件 ID,
        // 文件不存在时业务逻辑层抛出异常
        const std::string fastdfs_file_id = file_business_->GetSQLiteFile(request->request_id(),
                                                                          request->user_id(),
                                                                          request->file_id());

        // 将获取结果填充到响应中
        response->mutable_result()->set_fdfs_file_id(fastdfs_file_id);

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetSQLiteFile 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetSQLiteFile 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void FileServiceImpl::GetWorksheetDBTables(google::protobuf::RpcController* /*controller*/,
                                          const proto::GetWorksheetDBTablesRequest* request,
                                          proto::GetWorksheetDBTablesResponse* response,
                                          google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 与文件 ID 均不能为空
        if (request->user_id().empty())
        {
            ERR("GetWorksheetDBTables 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("GetWorksheetDBTables 接口请求参数错误, file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::FILE_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取 WorkSheet 数据库表名列表, 文件不存在时业务逻辑层抛出异常
        const std::vector<std::string> table_names =
            file_business_->GetWorksheetDBTables(request->request_id(), request->user_id(),
                                                 request->file_id());

        // 将表名列表填充到响应结果中
        proto::GetWorksheetDBTablesResult* result = response->mutable_result();
        for (const std::string& table_name : table_names)
        {
            result->add_worksheet_db_tables(table_name);
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetWorksheetDBTables 接口业务处理异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetWorksheetDBTables 接口非预期异常, request_id: {}, file_id: {}, 错误信息: {}",
            request->request_id(), request->file_id(), e.what());
        SetErrorResponse(response, ErrorCode::FILE_SERVICE_INTERNAL_ERROR, e.what());
    }
}

} // namespace file_service
} // namespace chat_excel
