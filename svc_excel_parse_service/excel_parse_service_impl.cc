#include "excel_parse_service_impl.h"

#include <string>
#include <utility>
#include <vector>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"
#include "excel_parse_business.h"

namespace chat_excel
{
namespace excel_parse_service
{

namespace
{

/**
 * @brief 将错误码与错误码描述填充到 RPC 响应中
 * @param response RPC 响应对象
 * @param error_code 错误码
 */
template <typename ResponseType>
void SetErrorResponse(ResponseType* response, ErrorCode error_code)
{
    response->set_error_code(static_cast<int>(error_code));
    response->set_error_msg(ErrorMessage(error_code));
}

} // namespace

ExcelParseServiceImpl::ExcelParseServiceImpl(
    std::shared_ptr<ExcelParseBusiness> excel_parse_business)
    : excel_parse_business_(std::move(excel_parse_business))
{
}

void ExcelParseServiceImpl::GetWorksheets(google::protobuf::RpcController* /*controller*/,
                                          const proto::GetWorksheetsRequest* request,
                                          proto::GetWorksheetsResponse* response,
                                          google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, fastdfs_file_id 为空属于请求参数错误
        if (request->fastdfs_file_id().empty())
        {
            ERR("GetWorksheets 接口请求参数错误, fastdfs_file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::EXCEL_PARSE_PARAMS_ERROR);
            return;
        }

        // 调用业务逻辑层获取所有 worksheet 名称列表
        // (业务层负责从 FastDFS 下载文件到本地临时目录并解析, 解析完成后清理临时目录)
        std::vector<std::string> worksheet_names =
            excel_parse_business_->GetWorksheetNames(request->request_id(),
                                                     request->fastdfs_file_id());
        for (const std::string& worksheet_name : worksheet_names)
        {
            response->add_worksheets(worksheet_name);
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetWorksheets 接口业务处理异常, fastdfs_file_id: {} , request_id: {} , 错误信息: {}",
            request->fastdfs_file_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetWorksheets 接口非预期异常, fastdfs_file_id: {} , request_id: {} , 错误信息: {}",
            request->fastdfs_file_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::EXCEL_PARSE_INTERNAL_ERROR);
    }
}

void ExcelParseServiceImpl::ParseExcel(google::protobuf::RpcController* /*controller*/,
                                       const proto::ParseExcelRequest* request,
                                       proto::ParseExcelResponse* response,
                                       google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, fastdfs_file_id 为空属于请求参数错误
        if (request->fastdfs_file_id().empty())
        {
            ERR("ParseExcel 接口请求参数错误, fastdfs_file_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::EXCEL_PARSE_PARAMS_ERROR);
            return;
        }

        // 请求中 worksheet 列表为空时, 视为解析文件中的全部 worksheet
        std::vector<std::string> worksheets(request->worksheets().begin(),
                                            request->worksheets().end());
        if (worksheets.empty())
        {
            worksheets = excel_parse_business_->GetWorksheetNames(request->request_id(),
                                                                  request->fastdfs_file_id());
            INFO("ParseExcel 接口未指定 worksheet, 解析全部 worksheet, 个数: {} , request_id: {}",
                 worksheets.size(), request->request_id());
        }

        // 调用业务逻辑层解析各 worksheet 的表结构(列信息)与表数据
        // (业务层负责从 FastDFS 下载文件到本地临时目录并解析, 解析完成后清理临时目录)
        std::vector<proto::WorksheetData> proto_worksheets =
            excel_parse_business_->ParseWorksheets(request->request_id(),
                                                   request->fastdfs_file_id(), worksheets);
        for (proto::WorksheetData& proto_worksheet : proto_worksheets)
        {
            *response->add_worksheets() = std::move(proto_worksheet);
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ParseExcel 接口业务处理异常, fastdfs_file_id: {} , request_id: {} , 错误信息: {}",
            request->fastdfs_file_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ParseExcel 接口非预期异常, fastdfs_file_id: {} , request_id: {} , 错误信息: {}",
            request->fastdfs_file_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::EXCEL_PARSE_INTERNAL_ERROR);
    }
}

} // namespace excel_parse_service
} // namespace chat_excel
