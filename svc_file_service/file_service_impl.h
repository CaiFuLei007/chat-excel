#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "file_service.pb.h"
#include "svc_file_service/file_business.h"

namespace chat_excel
{
namespace file_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::file_service;

/**
 * @brief 文件子服务 RPC 接口实现类, 继承 protoc 生成的 FileService 服务基类,
 *        负责解析与校验 RPC 请求参数(文件数据通过 brpc attachment 传输),
 *        调用文件业务逻辑层完成业务处理, 并将业务处理结果(错误码与错误信息)
 *        填充到 RPC 响应中; 业务处理过程中抛出的异常统一按照业务处理失败的
 *        逻辑进行处理
 */
class FileServiceImpl : public proto::FileService
{
public:
    /**
     * @brief 构造函数, 注入文件业务逻辑对象
     * @param file_business 文件业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit FileServiceImpl(std::shared_ptr<FileBusiness> file_business);

    ~FileServiceImpl() override = default;

    /**
     * @brief 上传文件(Excel/SQLite)信息, 生成文件 ID 并保存文件元信息
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件信息
     * @param response RPC 响应, 携带错误码、错误信息与生成的文件 ID
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void UploadFileInfo(google::protobuf::RpcController* controller,
                                const proto::UploadFileInfoRequest* request,
                                proto::UploadFileInfoResponse* response,
                                google::protobuf::Closure* done) override;

    /**
     * @brief 获取文件信息, 返回文件详情(文件名、大小、上传时间、扩展名)
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码、错误信息与文件详情
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetFileInfo(google::protobuf::RpcController* controller,
                             const proto::GetFileInfoRequest* request,
                             proto::GetFileInfoResponse* response,
                             google::protobuf::Closure* done) override;

    /**
     * @brief 上传 Excel 文件数据, 文件数据从 RPC attachment 中读取,
     *        上传到 FastDFS 后触发 Excel 解析流程
     * @param controller RPC 控制器, 文件数据通过其 request_attachment 传输
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void UploadFile(google::protobuf::RpcController* controller,
                            const proto::UploadFileRequest* request,
                            proto::UploadFileResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 下载文件数据, 文件数据通过 RPC attachment 返回(零拷贝传输)
     * @param controller RPC 控制器, 文件数据通过其 response_attachment 返回
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DownloadFile(google::protobuf::RpcController* controller,
                              const proto::DownloadFileRequest* request,
                              proto::DownloadFileResponse* response,
                              google::protobuf::Closure* done) override;

    /**
     * @brief 删除文件, 删除 FastDFS 文件数据、WorkSheet 信息与文件元信息
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DeleteFile(google::protobuf::RpcController* controller,
                            const proto::DeleteFileRequest* request,
                            proto::DeleteFileResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 预览 Excel 文件, 返回文件信息与解析结果(分页)
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID、文件 ID 与分页参数
     * @param response RPC 响应, 携带错误码、错误信息与预览结果
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void PreviewExcel(google::protobuf::RpcController* controller,
                              const proto::PreviewExcelRequest* request,
                              proto::PreviewExcelResponse* response,
                              google::protobuf::Closure* done) override;

    /**
     * @brief 获取用户上传的所有文件列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID 与用户 ID
     * @param response RPC 响应, 携带错误码、错误信息与文件列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetFileList(google::protobuf::RpcController* controller,
                             const proto::GetFileListRequest* request,
                             proto::GetFileListResponse* response,
                             google::protobuf::Closure* done) override;

    /**
     * @brief 关联文件和聊天会话
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID、文件 ID 与聊天会话 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void HandleFileChatSessionMap(google::protobuf::RpcController* controller,
                                          const proto::HandleFileChatSessionMapRequest* request,
                                          proto::HandleFileChatSessionMapResponse* response,
                                          google::protobuf::Closure* done) override;

    /**
     * @brief 上传 SQLite 文件, 先生成文件元信息(扩展名固定 .db,
     *        文件大小取 attachment 大小), 再将文件数据从 RPC attachment
     *        中读取并上传到 FastDFS
     * @param controller RPC 控制器, 文件数据通过其 request_attachment 传输
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件名
     * @param response RPC 响应, 携带错误码、错误信息与生成的文件 ID
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void UploadSQLiteFile(google::protobuf::RpcController* controller,
                                  const proto::UploadSQLiteFileRequest* request,
                                  proto::UploadSQLiteFileResponse* response,
                                  google::protobuf::Closure* done) override;

    /**
     * @brief 获取 SQLite 文件信息, 返回文件对应的 FastDFS 文件 ID
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码、错误信息与 FastDFS 文件 ID
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetSQLiteFile(google::protobuf::RpcController* controller,
                               const proto::GetSQLiteFileRequest* request,
                               proto::GetSQLiteFileResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 获取文件对应的所有 WorkSheet 数据库表名列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与文件 ID
     * @param response RPC 响应, 携带错误码、错误信息与数据库表名列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetWorksheetDBTables(google::protobuf::RpcController* controller,
                                      const proto::GetWorksheetDBTablesRequest* request,
                                      proto::GetWorksheetDBTablesResponse* response,
                                      google::protobuf::Closure* done) override;

private:
    // 文件业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<FileBusiness> file_business_;
};

} // namespace file_service
} // namespace chat_excel
