#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "excel_parse_business.h"
#include "excel_parse_service.pb.h"

namespace chat_excel
{
namespace excel_parse_service
{

/**
 * @brief Excel 解析子服务 RPC 接口实现类, 继承 protoc 生成的 ExcelParserService 服务基类,
 *        负责解析与校验 RPC 请求参数, 调用 Excel 解析业务逻辑层完成工作表列表获取与
 *        指定工作表的表结构/表数据解析, 并将处理结果(错误码与错误信息)填充到 RPC 响应中;
 *        业务处理过程中抛出的异常统一按照业务处理失败的逻辑进行处理
 */
class ExcelParseServiceImpl : public proto::ExcelParserService
{
public:
    /**
     * @brief 构造函数, 注入 Excel 解析业务逻辑对象
     * @param excel_parse_business Excel 解析业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit ExcelParseServiceImpl(std::shared_ptr<ExcelParseBusiness> excel_parse_business);

    ~ExcelParseServiceImpl() override = default;

    /**
     * @brief 获取 Excel 文件包含的所有 worksheet 工作表名称列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID 与 Excel 文件路径
     * @param response RPC 响应, 携带错误码、错误信息与工作表名称列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetWorksheets(google::protobuf::RpcController* controller,
                               const proto::GetWorksheetsRequest* request,
                               proto::GetWorksheetsResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 解析 Excel 文件中指定的 worksheet 集合, 返回各表的表结构(列信息)与表数据;
     *        请求中 worksheets 列表为空时视为解析文件中的全部 worksheet
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、Excel 文件路径与待解析的工作表名称列表
     * @param response RPC 响应, 携带错误码、错误信息与各工作表的结构化数据
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ParseExcel(google::protobuf::RpcController* controller,
                           const proto::ParseExcelRequest* request,
                           proto::ParseExcelResponse* response,
                           google::protobuf::Closure* done) override;

private:
    // Excel 解析业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<ExcelParseBusiness> excel_parse_business_;
};

} // namespace excel_parse_service
} // namespace chat_excel
