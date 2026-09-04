#pragma once

#include <string>
#include <odb/core.hxx>

namespace chat_excel
{

/**
 * @brief WorkSheet 表(tbl_worksheet_info)的 odb 数据库映射类, 完成 WorkSheet 表字段与 C++ 类成员的映射
 *        一个 Excel 文件可以包含多个 WorkSheet, 每个 WorkSheet 解析后的真实数据存储在独立的数据库表中,
 *        本表仅存储每个 WorkSheet 的元信息(名称与真实数据表名)
 *        建表 SQL 语句以及 CURD 支持代码由 odb 编译器生成
 *        WorkSheet 元信息在文件解析后不会发生变更, 不提供修改接口
 */
// odb pragma 指令仅 odb 编译器识别, 使用宏守卫防止普通 C++ 编译器产生未知 pragma 警告
#ifdef ODB_COMPILER
#pragma db object table("tbl_worksheet_info")
#endif
class WorkSheetEntity
{
public:
    /**
     * @brief 构造函数, 创建 WorkSheet 表映射对象, 用于新 WorkSheet 元信息的插入
     * @param file_id WorkSheet 所属文件 ID
     * @param worksheet_name WorkSheet 名称
     * @param table_name 该 WorkSheet 真实数据存储在的数据库表名
     */
    WorkSheetEntity(const std::string& file_id, const std::string& worksheet_name,
                    const std::string& table_name);

    /**
     * @brief 获取自增主键 ID
     * @return 自增主键 ID
     */
    unsigned long long Id() const;

    /**
     * @brief 获取 WorkSheet 所属文件 ID
     * @return WorkSheet 所属文件 ID
     */
    const std::string& FileId() const;

    /**
     * @brief 获取 WorkSheet 名称
     * @return WorkSheet 名称
     */
    const std::string& WorksheetName() const;

    /**
     * @brief 获取该 WorkSheet 真实数据存储在的数据库表名
     * @return 真实数据存储在的数据库表名
     */
    const std::string& TableName() const;

private:
    // odb 框架通过友元访问私有默认构造函数与私有数据成员
    friend class odb::access;

    // 默认构造函数, 仅供 odb 框架从数据库加载数据时使用
    WorkSheetEntity() = default;

    // 自增主键 ID
#ifdef ODB_COMPILER
#pragma db id auto column("id")
#endif
    unsigned long long id_;

    // WorkSheet 所属文件 ID, 一个文件可对应多个 WorkSheet, 建普通索引加速按文件查询
    // 文件 ID 为 36 字符的 UUIDv4(带连字符), 列宽取 64, 与 tbl_file_info.file_id 保持一致
#ifdef ODB_COMPILER
#pragma db column("file_id") type("VARCHAR(64)") not_null
#pragma db index
#endif
    std::string file_id_;

    // WorkSheet 名称
#ifdef ODB_COMPILER
#pragma db column("worksheet_name") type("VARCHAR(64)") not_null
#endif
    std::string worksheet_name_;

    // 该 WorkSheet 真实数据存储在的数据库表名
#ifdef ODB_COMPILER
#pragma db column("table_name") type("VARCHAR(64)") not_null
#endif
    std::string table_name_;
};

} // namespace chat_excel
