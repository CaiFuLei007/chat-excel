#include "svc_excel_parse_service/excel_parse_business.h"

#include <cstdio>
#include <string>
#include <vector>
#include <OpenXLSX.hpp>
#include <cpp-toolkit/logger.h>
#include <gtest/gtest.h>
#include "common/exception.h"
#include "excel_parse_service.pb.h"

// 业务层类型
using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;
using chat_excel::excel_parse_service::ExcelParseBusiness;

namespace
{

// 测试用 Excel 文件路径(测试临时文件)
constexpr const char* kTestExcelPath = "/tmp/excel_parse_business_test.xlsx";

// 业务层 : GetWorksheetNames 委托解析器返回名称列表
TEST(ExcelParseBusinessTest, GetWorksheetNamesReturnsList)
{
    OpenXLSX::XLDocument document;
    document.create(kTestExcelPath);
    document.workbook().addWorksheet("SecondSheet");
    document.save();

    ExcelParseBusiness business;
    std::vector<std::string> worksheet_names = business.GetWorksheetNames(kTestExcelPath);
    ASSERT_EQ(worksheet_names.size(), 2U);
    EXPECT_EQ(worksheet_names[0], "Sheet1");
    EXPECT_EQ(worksheet_names[1], "SecondSheet");
    std::remove(kTestExcelPath);
}

// 业务层 : ParseWorksheets 组织 proto 结构化数据(列信息/行数据/总行数总列数)
TEST(ExcelParseBusinessTest, ParseWorksheetsBuildsProtoData)
{
    OpenXLSX::XLDocument document;
    document.create(kTestExcelPath);
    OpenXLSX::XLWorksheet worksheet = document.workbook().worksheet("Sheet1");
    worksheet.cell(1, 1).value() = "id";
    worksheet.cell(1, 2).value() = "name";
    worksheet.cell(2, 1).value() = 42;
    worksheet.cell(2, 2).value() = "zhangsan";
    document.save();

    ExcelParseBusiness business;
    std::vector<chat_excel_proto::excel_parse_service::WorksheetData> proto_worksheets =
        business.ParseWorksheets(kTestExcelPath, {"Sheet1"});
    ASSERT_EQ(proto_worksheets.size(), 1U);

    const auto& proto_worksheet = proto_worksheets[0];
    EXPECT_EQ(proto_worksheet.name(), "Sheet1");
    EXPECT_EQ(proto_worksheet.total_rows(), 2);
    EXPECT_EQ(proto_worksheet.total_cols(), 2);
    ASSERT_EQ(proto_worksheet.columns_size(), 2);
    EXPECT_EQ(proto_worksheet.columns(0).name(), "id");
    EXPECT_EQ(proto_worksheet.columns(0).type(), "BIGINT");
    EXPECT_EQ(proto_worksheet.columns(1).name(), "name");
    EXPECT_EQ(proto_worksheet.columns(1).type(), "TEXT");
    ASSERT_EQ(proto_worksheet.rows_size(), 1);
    ASSERT_EQ(proto_worksheet.rows(0).cells_size(), 2);
    EXPECT_EQ(proto_worksheet.rows(0).cells(0).value(), "42");
    EXPECT_EQ(proto_worksheet.rows(0).cells(0).type(), "Integer");
    EXPECT_EQ(proto_worksheet.rows(0).cells(1).value(), "zhangsan");
    EXPECT_EQ(proto_worksheet.rows(0).cells(1).type(), "String");
    std::remove(kTestExcelPath);
}

// 业务层 : 指定的 worksheet 不存在时抛出异常
TEST(ExcelParseBusinessTest, ParseWorksheetsThrowsWhenWorksheetNotFound)
{
    OpenXLSX::XLDocument document;
    document.create(kTestExcelPath);
    document.save();

    ExcelParseBusiness business;
    try
    {
        business.ParseWorksheets(kTestExcelPath, {"NotExistsSheet"});
        FAIL() << "应当抛出 ChatExcelException 异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::EXCEL_PARSE_WORKSHEET_NOT_FOUND);
    }
    std::remove(kTestExcelPath);
}

} // namespace

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "excel_parse_business_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
