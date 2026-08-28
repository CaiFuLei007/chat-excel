#include "work_sheet_entity.h"

namespace chat_excel
{

WorkSheetEntity::WorkSheetEntity(const std::string& file_id, const std::string& worksheet_name,
                                 const std::string& table_name)
    : file_id_(file_id),
      worksheet_name_(worksheet_name),
      table_name_(table_name)
{
}

unsigned long long WorkSheetEntity::Id() const
{
    return id_;
}

const std::string& WorkSheetEntity::FileId() const
{
    return file_id_;
}

const std::string& WorkSheetEntity::WorksheetName() const
{
    return worksheet_name_;
}

const std::string& WorkSheetEntity::TableName() const
{
    return table_name_;
}

} // namespace chat_excel
