#include "svc_excel_parse_service/excel_parse.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <OpenXLSX.hpp>
#include <cpp-toolkit/logger.h>
#include <gtest/gtest.h>
#include "common/exception.h"

// 业务层类型
using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;
using chat_excel::excel_parse_service::CellType;
using chat_excel::excel_parse_service::ExcelParse;
using chat_excel::excel_parse_service::WorksheetData;

namespace
{

// 测试用 Excel 文件路径(测试临时文件)
constexpr const char* kTestExcelPath = "/tmp/excel_parse_test.xlsx";

/**
 * @brief 测试夹具, 负责测试用 Excel 文件的创建与清理 :
 *        每个测试用例使用独立的临时文件, 避免用例间相互干扰
 */
class ExcelParseTest : public ::testing::Test
{
protected:
    // 测试使用的解析器实例
    ExcelParse excel_parse_;

    void SetUp() override
    {
        // 清理上一个用例可能遗留的临时文件
        std::remove(kTestExcelPath);
    }

    void TearDown() override
    {
        // 用例结束后清理临时文件
        std::remove(kTestExcelPath);
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

        // 第一行写表头
        for (size_t column_index = 0; column_index < headers.size(); ++column_index)
        {
            worksheet.cell(1, static_cast<uint16_t>(column_index + 1)).value() = headers[column_index];
        }

        // 第二行起写数据
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
};

// 正常情况 : 获取 worksheet 名称列表
TEST_F(ExcelParseTest, GetWorksheetNamesReturnsAllWorksheets)
{
    CreateTestExcel({"id", "name"}, {{"1", "zhangsan"}});

    OpenXLSX::XLDocument document;
    document.open(kTestExcelPath);
    document.workbook().addWorksheet("ExtraSheet");
    document.save();

    std::vector<std::string> worksheet_names = excel_parse_.GetWorksheetNames(kTestExcelPath);
    ASSERT_EQ(worksheet_names.size(), 2U);
    EXPECT_EQ(worksheet_names[0], "Sheet1");
    EXPECT_EQ(worksheet_names[1], "ExtraSheet");
}

// 异常情况 : 文件不存在时抛出文件打开失败异常
TEST_F(ExcelParseTest, GetWorksheetNamesThrowsWhenFileNotExists)
{
    try
    {
        excel_parse_.GetWorksheetNames("/tmp/not_exist_excel_file.xlsx");
        FAIL() << "应当抛出 ChatExcelException 异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED);
    }
}

// 正常情况 : 表头清洗, 非法字符替换为下划线
TEST_F(ExcelParseTest, SanitizeHeaderReplacesIllegalCharacters)
{
    CreateTestExcel({"user id", "na#me!"}, {{"1", "a"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    ASSERT_EQ(worksheet_data.columns.size(), 2U);
    EXPECT_EQ(worksheet_data.columns[0].name, "user_id");
    EXPECT_EQ(worksheet_data.columns[1].name, "na_me_");
}

// 边界情况 : 空列名使用 column_列号, 数字开头列名添加 col_ 前缀
TEST_F(ExcelParseTest, SanitizeHeaderHandlesEmptyAndDigitStart)
{
    CreateTestExcel({"", "123abc"}, {{"1", "2"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    ASSERT_EQ(worksheet_data.columns.size(), 2U);
    EXPECT_EQ(worksheet_data.columns[0].name, "column_1");
    EXPECT_EQ(worksheet_data.columns[1].name, "col_123abc");
}

// 正常情况 : 纯整型列(排除布尔 1)判定为 BIGINT
TEST_F(ExcelParseTest, IntegerColumnDetectedAsBigint)
{
    CreateTestExcel({"id", "name"}, {{"2", "a"}, {"3", "b"}, {"4", "c"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "BIGINT");
    EXPECT_EQ(worksheet_data.columns[1].type, "TEXT");
}

// 正常情况 : 纯 1/0 列判定为 BOOLEAN
TEST_F(ExcelParseTest, PureOneZeroColumnDetectedAsBoolean)
{
    CreateTestExcel({"flag"}, {{"1"}, {"0"}, {"1"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "BOOLEAN");
}

// 正常情况 : 布尔文本列(true/false/yes/no 忽略大小写)判定为 BOOLEAN
TEST_F(ExcelParseTest, BooleanTextColumnDetectedAsBoolean)
{
    CreateTestExcel({"flag"}, {{"true"}, {"FALSE"}, {"yes"}, {"No"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "BOOLEAN");
}

// 正常情况 : 浮点列判定为 DOUBLE
TEST_F(ExcelParseTest, FloatColumnDetectedAsDouble)
{
    CreateTestExcel({"price"}, {{"1.5"}, {"2.75"}, {"-3.14"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "DOUBLE");
}

// 正常情况 : 纯 ISO 规范日期列判定为 DATE
TEST_F(ExcelParseTest, DateColumnDetectedAsDate)
{
    CreateTestExcel({"created"}, {{"2024-01-15"}, {"2024-02-16"}, {"2024-03-17"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "DATE");
}

// 边界情况 : 列中存在非 ISO 规范日期(斜杠/点分隔/美式日期)时整列判定为 TEXT,
//            避免 MySQL 无法统一解析的日期格式导致插入失败
TEST_F(ExcelParseTest, NonIsoDateFormatColumnFallsBackToText)
{
    CreateTestExcel({"created"}, {{"2024-01-15"}, {"2024/02/16"}, {"02-17-2024"}, {"2024.02.18"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 正常情况 : 日期时间格式 yyyy-mm-dd-HH:MM:SS 判定为 DATE
TEST_F(ExcelParseTest, DateTimeColumnDetectedAsDate)
{
    CreateTestExcel({"created"}, {{"2024-01-15-10:30:00"}, {"2024-02-16-23:59:59"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "DATE");
}

// 正常情况 : 普通文本列判定为 TEXT
TEST_F(ExcelParseTest, TextColumnDetectedAsText)
{
    CreateTestExcel({"remark"}, {{"hello"}, {"world"}, {"ok"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 全空列(不统计任何单元格)默认 TEXT
TEST_F(ExcelParseTest, AllEmptyColumnDefaultsToText)
{
    CreateTestExcel({"empty_col", "id"}, {{"", "1"}, {"", "2"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : N/A 单元格不参与列类型统计
TEST_F(ExcelParseTest, NaCellSkippedInColumnDetection)
{
    CreateTestExcel({"flag"}, {{"N/A"}, {"true"}, {"false"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "BOOLEAN");
}

// 正常情况 : 混合列取众数类型
TEST_F(ExcelParseTest, MixedColumnResolvedByMajority)
{
    CreateTestExcel({"mixed"}, {{"true"}, {"false"}, {"hello"}, {"text2"}, {"more"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 数值占多数但存在文本单元格的列(如考试地点混有文字)整列判定为 TEXT,
//            避免文本行插入数值列时 MySQL 类型转换失败导致整批导入回滚
TEST_F(ExcelParseTest, ColumnWithAnyTextFallsBackToText)
{
    CreateTestExcel({"place"}, {{"101"}, {"102"}, {"103"}, {"104"}, {"105"},
                     {"东区教学楼"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 列类型判定遍历全部数据行, 前 100 行之后的文本行不会被漏检
TEST_F(ExcelParseTest, TypeDetectionCoversAllRows)
{
    std::vector<std::vector<std::string>> rows;
    rows.reserve(152);
    for (int i = 1; i <= 150; ++i)
    {
        rows.push_back({std::to_string(i)});
    }
    rows.push_back({"补考地点: 6号教学楼101"});
    CreateTestExcel({"place"}, rows);

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 前导零数字列(学号/序号类编码)按文本存储, 避免入库丢失前导零
TEST_F(ExcelParseTest, LeadingZeroIntegerColumnFallsBackToText)
{
    CreateTestExcel({"student_no"}, {{"0012"}, {"0023"}, {"0045"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
    // 文本列原样保留前导零
    EXPECT_EQ(worksheet_data.rows[0][0].value, "0012");
}

// 边界情况 : 超出 int64 范围的大整数按文本存储, 避免 BIGINT 溢出
TEST_F(ExcelParseTest, OversizedIntegerColumnFallsBackToText)
{
    CreateTestExcel({"big"}, {{"9223372036854775807"}, {"99999999999999999999"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 布尔与整数混排的列按文本存储(无法用单一布尔/数值列无损容纳)
TEST_F(ExcelParseTest, BoolAndIntegerMixedColumnFallsBackToText)
{
    CreateTestExcel({"flag"}, {{"true"}, {"42"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 边界情况 : 千分位/全角数字等带格式数字文本按文本存储(需数值语义时由入库侧规范化)
TEST_F(ExcelParseTest, FormattedNumberTextKeptAsText)
{
    CreateTestExcel({"amount"}, {{"1,234"}, {"\uFF15\uFF16\uFF17\uFF18"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 正常情况 : 单元格前后空白不参与类型判定
TEST_F(ExcelParseTest, WhitespaceTrimmedBeforeDetection)
{
    CreateTestExcel({"price"}, {{" 1.5 "}, {"2.75"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.columns[0].type, "DOUBLE");
}

// 正常情况 : 表数据解析, 总行数包含表头行
TEST_F(ExcelParseTest, ParseWorksheetReturnsRowsAndTotals)
{
    CreateTestExcel({"id", "name"}, {{"1", "zhangsan"}, {"2", "lisi"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.total_rows, 3);
    EXPECT_EQ(worksheet_data.total_cols, 2);
    ASSERT_EQ(worksheet_data.rows.size(), 2U);
    EXPECT_EQ(worksheet_data.rows[0][0].value, "1");
    EXPECT_EQ(worksheet_data.rows[0][1].value, "zhangsan");
    EXPECT_EQ(worksheet_data.rows[1][0].value, "2");
    EXPECT_EQ(worksheet_data.rows[1][1].value, "lisi");
}

// 正常情况 : 单元格类型与转换(数值单元格存为 int64, 文本单元格为字符串)
TEST_F(ExcelParseTest, CellValueAndTypeConversion)
{
    CreateTestExcel({"id", "name"}, {{"1", "zhangsan"}});

    // 将第一列数据行覆写为原生整型值, OpenXLSX 以数值类型存储
    {
        OpenXLSX::XLDocument document;
        document.open(kTestExcelPath);
        OpenXLSX::XLWorksheet worksheet = document.workbook().worksheet("Sheet1");
        worksheet.cell(2, 1).value() = 42;
        document.save();
    }

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    ASSERT_EQ(worksheet_data.rows.size(), 1U);
    EXPECT_EQ(worksheet_data.rows[0][0].value, "42");
    EXPECT_EQ(worksheet_data.rows[0][0].type, CellType::INTEGER);
    EXPECT_EQ(worksheet_data.rows[0][1].value, "zhangsan");
    EXPECT_EQ(worksheet_data.rows[0][1].type, CellType::STRING);
}

// 正常情况 : N/A 单元格输出空值
TEST_F(ExcelParseTest, NaCellOutputsEmptyValue)
{
    CreateTestExcel({"id"}, {{"N/A"}, {"1"}});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    ASSERT_EQ(worksheet_data.rows.size(), 2U);
    EXPECT_EQ(worksheet_data.rows[0][0].value, "");
    EXPECT_EQ(worksheet_data.rows[0][0].type, CellType::EMPTY);
}

// 边界情况 : 空 worksheet(仅表头无数据行)返回空结果不抛异常
TEST_F(ExcelParseTest, EmptyWorksheetReturnsEmptyResult)
{
    CreateTestExcel({"id", "name"}, {});

    WorksheetData worksheet_data = excel_parse_.ParseWorksheet(kTestExcelPath, "Sheet1");
    EXPECT_EQ(worksheet_data.total_rows, 1);
    EXPECT_EQ(worksheet_data.total_cols, 2);
    EXPECT_TRUE(worksheet_data.rows.empty());
    ASSERT_EQ(worksheet_data.columns.size(), 2U);
    // 仅表头无数据行, 列无采样数据默认 TEXT
    EXPECT_EQ(worksheet_data.columns[0].type, "TEXT");
}

// 异常情况 : worksheet 不存在抛出对应异常
TEST_F(ExcelParseTest, ParseWorksheetThrowsWhenWorksheetNotFound)
{
    CreateTestExcel({"id"}, {{"1"}});

    try
    {
        excel_parse_.ParseWorksheet(kTestExcelPath, "NotExistsSheet");
        FAIL() << "应当抛出 ChatExcelException 异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::EXCEL_PARSE_WORKSHEET_NOT_FOUND);
    }
}

// 异常情况 : 文件不存在时解析抛出文件打开失败异常
TEST_F(ExcelParseTest, ParseWorksheetThrowsWhenFileNotExists)
{
    try
    {
        excel_parse_.ParseWorksheet("/tmp/not_exist_excel_file.xlsx", "Sheet1");
        FAIL() << "应当抛出 ChatExcelException 异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED);
    }
}

} // namespace

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "excel_parse_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
