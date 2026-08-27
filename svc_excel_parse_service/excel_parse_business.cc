#include "excel_parse_business.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"
#include "excel_parse.h"

namespace chat_excel
{
namespace excel_parse_service
{

namespace
{

/**
 * @brief 将内部单元格类型枚举转换为 proto 协议的类型字符串 :
 *        EMPTY -> "Empty" , INTEGER -> "Integer" , FLOAT -> "Float" ,
 *        BOOLEAN -> "Boolean" , 其余(DATE/STRING) -> "String"
 * @param cell_type 内部单元格类型
 * @return proto 协议的单元格类型字符串
 */
const char* ConvertCellTypeString(CellType cell_type)
{
    switch (cell_type)
    {
    case CellType::EMPTY:
        return "Empty";
    case CellType::INTEGER:
        return "Integer";
    case CellType::FLOAT:
        return "Float";
    case CellType::BOOLEAN:
        return "Boolean";
    default:
        return "String";
    }
}

} // namespace

ExcelParseBusiness::ExcelParseBusiness()
    : excel_parse_(std::make_shared<ExcelParse>())
{
    INFO("Excel 解析业务逻辑对象构建完成");
}

std::vector<std::string> ExcelParseBusiness::GetWorksheetNames(const std::string& file_path)
{
    // 直接委托给 Excel 解析器完成, 解析失败时异常向上抛出由 RPC 层捕获
    return excel_parse_->GetWorksheetNames(file_path);
}

std::vector<proto::WorksheetData> ExcelParseBusiness::ParseWorksheets(
    const std::string& file_path, const std::vector<std::string>& worksheets)
{
    std::vector<proto::WorksheetData> proto_worksheets;
    proto_worksheets.reserve(worksheets.size());

    // 逐个解析 worksheet 并转换为 proto 结构化数据, 任一表解析失败整体抛出异常
    for (const std::string& worksheet_name : worksheets)
    {
        WorksheetData worksheet_data = excel_parse_->ParseWorksheet(file_path, worksheet_name);
        proto::WorksheetData proto_worksheet;
        BuildWorksheetProto(worksheet_data, &proto_worksheet);
        proto_worksheets.push_back(std::move(proto_worksheet));
    }

    INFO("Excel 解析业务完成, file_path: {} , 解析 worksheet 个数: {}",
         file_path, proto_worksheets.size());
    return proto_worksheets;
}

void ExcelParseBusiness::BuildWorksheetProto(const WorksheetData& worksheet_data,
                                             proto::WorksheetData* proto_worksheet) const
{
    proto_worksheet->set_name(worksheet_data.name);
    proto_worksheet->set_total_rows(worksheet_data.total_rows);
    proto_worksheet->set_total_cols(worksheet_data.total_cols);

    // 填充列信息 : 列名 + 数据库列类型
    for (const ColumnInfo& column : worksheet_data.columns)
    {
        proto::ProtoColumnInfo* proto_column = proto_worksheet->add_columns();
        proto_column->set_name(column.name);
        proto_column->set_type(column.type);
    }

    // 填充行数据 : 每个单元格的值 + 类型字符串
    for (const std::vector<CellData>& row : worksheet_data.rows)
    {
        proto::RowData* proto_row = proto_worksheet->add_rows();
        for (const CellData& cell : row)
        {
            proto::ProtoCellData* proto_cell = proto_row->add_cells();
            proto_cell->set_value(cell.value);
            proto_cell->set_type(ConvertCellTypeString(cell.type));
        }
    }
}

} // namespace excel_parse_service
} // namespace chat_excel
