#pragma once

#include <string>
#include <vector>

namespace chat_excel
{
namespace excel_parse_service
{

// 单元格类型枚举, 用于列类型采样统计与单元格数据解析
enum class CellType
{
    EMPTY = 0,
    INTEGER,
    FLOAT,
    BOOLEAN,
    DATE,
    STRING,
};

// 单元格数据 : 值 + 单元格类型(值为字符串形式, 空单元格 value 为空)
struct CellData
{
    std::string value;

    // 单元格类型
    CellType type = CellType::EMPTY;
};

// 列信息 : 列名 + 数据库列类型(TEXT/BIGINT/DOUBLE/BOOLEAN/DATE)
struct ColumnInfo
{
    std::string name;

    // 数据库列类型
    std::string type;
};

// worksheet 解析结果 : 表头信息 + 列类型信息 + 表数据 + 总行数总列数
struct WorksheetData
{
    std::string name;
    std::vector<ColumnInfo> columns;
    std::vector<std::vector<CellData>> rows;

    // 总行数(包含表头行)
    int total_rows = 0;

    // 总列数
    int total_cols = 0;
};

/**
 * @brief Excel 解析器类, 基于 OpenXLSX 库实现 Excel 文件解析,
 *        提供两层解析能力 :
 *        1. 解析 Excel 文件所有 worksheet 的名称
 *        2. 解析指定 worksheet 的表头信息、列类型信息与表数据,
 *           结果供文件子服务将 worksheet 数据以数据库表的形式存储到数据库中;
 *        解析失败时抛出 ChatExcelException 异常, 由上层调用者捕获处理
 */
class ExcelParse
{
public:
    // 简单构造函数
    ExcelParse() = default;

    // 简单析构函数
    ~ExcelParse() = default;

    /**
     * @brief 解析 Excel 文件所有 worksheet 的名称
     * @param file_path Excel 文件路径
     * @return 所有 worksheet 名称列表
     * @throws ChatExcelException EXCEL_PARSE_FILE_OPEN_FAILED 文件打开失败或文件格式非法
     */
    std::vector<std::string> GetWorksheetNames(const std::string& file_path);

    /**
     * @brief 解析指定 worksheet 的表头信息、列类型信息与表数据;
     *        表头为第一行(需清洗非法字符), 列类型按前 100 行数据采样众数判定,
     *        表数据为除表头行外的所有行;
     *        空 worksheet(0 行或 0 列)不视为异常, 返回空结果
     * @param file_path Excel 文件路径
     * @param worksheet_name worksheet 名称
     * @return worksheet 结构化解析结果
     * @throws ChatExcelException EXCEL_PARSE_FILE_OPEN_FAILED 文件打开失败,
     *                            EXCEL_PARSE_WORKSHEET_NOT_FOUND worksheet 不存在,
     *                            EXCEL_PARSE_FAILED 其他解析异常
     */
    WorksheetData ParseWorksheet(const std::string& file_path, const std::string& worksheet_name);
};

} // namespace excel_parse_service
} // namespace chat_excel
