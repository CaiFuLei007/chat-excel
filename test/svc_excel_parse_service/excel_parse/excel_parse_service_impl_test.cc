#include "svc_excel_parse_service/excel_parse_service_impl.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <OpenXLSX.hpp>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>
#include <gtest/gtest.h>
#include "common/exception.h"
#include "svc_excel_parse_service/excel_parse_business.h"

// RPC 层类型
using chat_excel::ErrorCode;
using chat_excel::excel_parse_service::ExcelParseBusiness;
using chat_excel::excel_parse_service::ExcelParseServiceImpl;

// proto 生成代码中的消息类型
using chat_excel_proto::excel_parse_service::GetWorksheetsRequest;
using chat_excel_proto::excel_parse_service::GetWorksheetsResponse;
using chat_excel_proto::excel_parse_service::ParseExcelRequest;
using chat_excel_proto::excel_parse_service::ParseExcelResponse;

namespace
{

// 测试用 Excel 文件路径(测试临时文件)
constexpr const char* kTestExcelPath = "/tmp/excel_parse_service_impl_test.xlsx";

// 测试使用的 RPC 接口实现对象(注入真实业务对象)
std::shared_ptr<ExcelParseServiceImpl> CreateServiceImpl()
{
    return std::make_shared<ExcelParseServiceImpl>(std::make_shared<ExcelParseBusiness>());
}

/**
 * @brief 创建测试用 Excel 文件并写入表头与数据行
 * @param headers 表头单元格文本列表
 * @param rows 数据行(每行为单元格文本列表)
 */
void CreateTestExcel(const std::vector<std::string>& headers,
                     const std::vector<std::vector<std::string>>& rows)
{
    OpenXLSX::XLDocument document;
    document.create(kTestExcelPath);
    OpenXLSX::XLWorksheet worksheet = document.workbook().worksheet("Sheet1");
    for (size_t column_index = 0; column_index < headers.size(); ++column_index)
    {
        worksheet.cell(1, static_cast<uint16_t>(column_index + 1)).value() = headers[column_index];
    }
    for (size_t row_index = 0; row_index < rows.size(); ++row_index)
    {
        for (size_t column_index = 0; column_index < rows[row_index].size(); ++column_index)
        {
            worksheet.cell(static_cast<uint32_t>(row_index + 2),
                           static_cast<uint16_t>(column_index + 1)).value() = rows[row_index][column_index];
        }
    }
    document.save();
}

// 正常情况 : GetWorksheets 返回所有 worksheet 名称
TEST(ExcelParseServiceImplTest, GetWorksheetsReturnsWorksheetList)
{
    CreateTestExcel({"id"}, {{"1"}});
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    GetWorksheetsRequest request;
    request.set_request_id("req-001");
    request.set_file_path(kTestExcelPath);
    GetWorksheetsResponse response;

    service_impl->GetWorksheets(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.request_id(), "req-001");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.worksheets_size(), 1);
    EXPECT_EQ(response.worksheets(0), "Sheet1");
    std::remove(kTestExcelPath);
}

// 异常情况 : file_path 为空返回参数错误
TEST(ExcelParseServiceImplTest, GetWorksheetsReturnsParamsErrorWhenFilePathEmpty)
{
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    GetWorksheetsRequest request;
    request.set_request_id("req-002");
    GetWorksheetsResponse response;

    service_impl->GetWorksheets(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.request_id(), "req-002");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::EXCEL_PARSE_PARAMS_ERROR));
}

// 异常情况 : 文件不存在返回文件打开失败错误码
TEST(ExcelParseServiceImplTest, GetWorksheetsReturnsErrorWhenFileNotExists)
{
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    GetWorksheetsRequest request;
    request.set_request_id("req-003");
    request.set_file_path("/tmp/not_exist_excel_file.xlsx");
    GetWorksheetsResponse response;

    service_impl->GetWorksheets(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED));
}

// 正常情况 : ParseExcel 返回指定 worksheet 的结构化数据
TEST(ExcelParseServiceImplTest, ParseExcelReturnsWorksheetData)
{
    CreateTestExcel({"id", "name"}, {{"1", "zhangsan"}});
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    ParseExcelRequest request;
    request.set_request_id("req-004");
    request.set_file_path(kTestExcelPath);
    request.add_worksheets("Sheet1");
    ParseExcelResponse response;

    service_impl->ParseExcel(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.request_id(), "req-004");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.worksheets_size(), 1);
    EXPECT_EQ(response.worksheets(0).name(), "Sheet1");
    ASSERT_EQ(response.worksheets(0).columns_size(), 2);
    EXPECT_EQ(response.worksheets(0).columns(0).name(), "id");
    ASSERT_EQ(response.worksheets(0).rows_size(), 1);
    ASSERT_EQ(response.worksheets(0).rows(0).cells_size(), 2);
    EXPECT_EQ(response.worksheets(0).rows(0).cells(0).value(), "1");
    std::remove(kTestExcelPath);
}

// 正常情况 : worksheets 列表为空时解析全部 worksheet
TEST(ExcelParseServiceImplTest, ParseExcelParsesAllWorksheetsWhenListEmpty)
{
    CreateTestExcel({"id"}, {{"1"}});
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    ParseExcelRequest request;
    request.set_request_id("req-005");
    request.set_file_path(kTestExcelPath);
    ParseExcelResponse response;

    service_impl->ParseExcel(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.worksheets_size(), 1);
    EXPECT_EQ(response.worksheets(0).name(), "Sheet1");
    std::remove(kTestExcelPath);
}

// 异常情况 : file_path 为空返回参数错误
TEST(ExcelParseServiceImplTest, ParseExcelReturnsParamsErrorWhenFilePathEmpty)
{
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    ParseExcelRequest request;
    request.set_request_id("req-006");
    ParseExcelResponse response;

    service_impl->ParseExcel(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.request_id(), "req-006");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::EXCEL_PARSE_PARAMS_ERROR));
}

// 异常情况 : 指定的 worksheet 不存在返回对应错误码
TEST(ExcelParseServiceImplTest, ParseExcelReturnsErrorWhenWorksheetNotFound)
{
    CreateTestExcel({"id"}, {{"1"}});
    std::shared_ptr<ExcelParseServiceImpl> service_impl = CreateServiceImpl();

    ParseExcelRequest request;
    request.set_request_id("req-007");
    request.set_file_path(kTestExcelPath);
    request.add_worksheets("NotExistsSheet");
    ParseExcelResponse response;

    service_impl->ParseExcel(nullptr, &request, &response, nullptr);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::EXCEL_PARSE_WORKSHEET_NOT_FOUND));
    std::remove(kTestExcelPath);
}

} // namespace

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "excel_parse_service_impl_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
