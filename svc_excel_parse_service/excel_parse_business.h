#pragma once

#include <memory>
#include <string>
#include <vector>
#include "excel_parse.h"
#include "excel_parse_service.pb.h"

namespace chat_excel
{
namespace excel_parse_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 数据结构引用
namespace proto = ::chat_excel_proto::excel_parse_service;

/**
 * @brief Excel 解析子服务业务逻辑类, 是对 Excel 解析器的封装,
 *        负责组织解析流程并构建指定 worksheet 的表头信息/列类型信息/表数据,
 *        将内部解析结果转换为 RPC 接口需要的 proto 结构化数据
 */
class ExcelParseBusiness
{
public:
    /**
     * @brief 构造函数, 构建 Excel 解析器实例(智能指针持有)
     */
    ExcelParseBusiness();

    // 业务对象持有解析器实例, 禁止拷贝与赋值
    ExcelParseBusiness(const ExcelParseBusiness&) = delete;
    ExcelParseBusiness& operator=(const ExcelParseBusiness&) = delete;

    // 简单析构函数
    ~ExcelParseBusiness() = default;

    /**
     * @brief 解析 Excel 文件所有 worksheet 的名称
     * @param file_path Excel 文件路径
     * @return 所有 worksheet 名称列表
     * @throws ChatExcelException 文件打开失败时抛出对应异常
     */
    std::vector<std::string> GetWorksheetNames(const std::string& file_path);

    /**
     * @brief 解析指定的 worksheet 集合, 每张表返回表头信息/列类型信息/表数据,
     *        结果为 proto 结构化数据, 可直接填充到 ParseExcelResponse 中
     * @param file_path Excel 文件路径
     * @param worksheets 待解析的 worksheet 名称列表
     * @return 各 worksheet 的结构化解析结果(与请求中名称顺序一致)
     * @throws ChatExcelException 文件打开失败/worksheet 不存在/解析异常时抛出对应异常
     */
    std::vector<proto::WorksheetData> ParseWorksheets(const std::string& file_path,
                                                      const std::vector<std::string>& worksheets);

private:
    /**
     * @brief 将内部解析结果转换为 proto 结构化数据 :
     *        填充表名/列信息(列名与数据库列类型)/行数据(单元格值与类型)/总行数总列数
     * @param worksheet_data 内部 worksheet 解析结果
     * @param proto_worksheet 输出的 proto 结构化数据
     */
    void BuildWorksheetProto(const WorksheetData& worksheet_data,
                             proto::WorksheetData* proto_worksheet) const;

    // Excel 解析器实例(智能指针持有)
    std::shared_ptr<ExcelParse> excel_parse_;
};

} // namespace excel_parse_service
} // namespace chat_excel
