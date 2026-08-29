#include "excel_parse_business.h"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"
#include "excel_parse.h"
// FastDFS 客户端头文件必须最后导入 : 其依赖的 fastcommon 头文件会向全局作用域
// 定义 byte 等宏, 先行导入会破坏 fmt/boost 等后续头文件的解析
#include <cpp-toolkit/fdfs.h>

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

// 本地暂存 Excel 文件的根目录, 每个请求的文件保存在 {request_id} 子目录下,
// 防止不同请求之间的文件冲突
constexpr const char* kTempExcelFilesDir = "/tmp/excel_files";

// 下载到本地后的 Excel 文件名(每个请求独立目录, 固定文件名即可)
constexpr const char* kLocalExcelFileName = "excel.xlsx";

/**
 * @brief 构建请求级临时目录路径, 格式为 /tmp/excel_files/{request_id}
 * @param request_id 请求 ID
 * @return 请求级临时目录路径
 */
std::string BuildTempDirPath(const std::string& request_id)
{
    return std::string(kTempExcelFilesDir) + "/" + request_id;
}

/**
 * @brief 请求级临时目录 RAII 守护类, 构造时记录目录路径,
 *        析构时递归删除目录及其中所有文件; 无论解析成功还是中途抛出异常,
 *        都保证 /tmp/excel_files/{request_id}/ 被清理
 */
class TempDirGuard
{
public:
    /**
     * @brief 构造函数, 记录请求 ID 与临时目录路径
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param dir_path 临时目录路径
     */
    TempDirGuard(std::string request_id, std::string dir_path)
        : request_id_(std::move(request_id)), dir_path_(std::move(dir_path))
    {
    }

    // 守护类持有目录路径资源, 禁止拷贝与赋值
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;

    // 简单析构函数
    ~TempDirGuard()
    {
        std::error_code error_code;
        const std::uintmax_t removed_count = std::filesystem::remove_all(dir_path_, error_code);
        if (error_code)
        {
            ERR("删除请求级临时目录失败, request_id: {}, 路径: {}, 错误: {}",
                request_id_, dir_path_, error_code.message());
            return;
        }
        INFO("删除请求级临时目录成功, request_id: {}, 路径: {}, 删除文件个数: {}",
             request_id_, dir_path_, removed_count);
    }

private:
    // 请求 ID, 仅用于日志链路追踪
    std::string request_id_;

    // 临时目录路径
    std::string dir_path_;
};

/**
 * @brief 从 FastDFS 下载 Excel 文件到请求级临时目录,
 *        本地保存路径为 /tmp/excel_files/{request_id}/excel.xlsx
 * @param request_id 请求 ID, 用于日志链路追踪
 * @param fastdfs_file_id FastDFS 文件 ID
 * @return 下载后的本地 Excel 文件路径
 * @throws ChatExcelException 临时目录创建失败抛 EXCEL_PARSE_INTERNAL_ERROR,
 *                            文件下载失败抛 EXCEL_PARSE_FDFS_DOWNLOAD_ERROR
 */
std::string DownloadExcelFile(const std::string& request_id, const std::string& fastdfs_file_id)
{
    // 逐级创建请求级临时目录, 目录已存在时创建成功
    const std::string temp_dir_path = BuildTempDirPath(request_id);
    std::error_code error_code;
    std::filesystem::create_directories(temp_dir_path, error_code);
    if (error_code)
    {
        ERR("创建请求级临时目录失败, request_id: {}, 路径: {}, 错误: {}",
            request_id, temp_dir_path, error_code.message());
        throw ChatExcelException(ErrorCode::EXCEL_PARSE_INTERNAL_ERROR);
    }

    // 通过 FastDFS 文件 ID 将文件下载到本地临时目录
    const std::string local_file_path = temp_dir_path + "/" + kLocalExcelFileName;
    if (!cpp_toolkit::FdfsClient::DownloadToFile(fastdfs_file_id, local_file_path))
    {
        ERR("从 FastDFS 下载 Excel 文件失败, request_id: {}, fastdfs_file_id: {}",
            request_id, fastdfs_file_id);
        throw ChatExcelException(ErrorCode::EXCEL_PARSE_FDFS_DOWNLOAD_ERROR);
    }
    INFO("从 FastDFS 下载 Excel 文件成功, request_id: {}, fastdfs_file_id: {}, 本地路径: {}",
         request_id, fastdfs_file_id, local_file_path);
    return local_file_path;
}

} // namespace

ExcelParseBusiness::ExcelParseBusiness()
    : excel_parse_(std::make_shared<ExcelParse>())
{
    INFO("Excel 解析业务逻辑对象构建完成");
}

std::vector<std::string> ExcelParseBusiness::GetWorksheetNames(
    const std::string& request_id, const std::string& fastdfs_file_id)
{
    // RAII 守护 : 无论解析成功还是中途抛出异常, 函数结束时都清理请求级临时目录
    TempDirGuard temp_dir_guard(request_id, BuildTempDirPath(request_id));

    // 从 FastDFS 下载 Excel 文件到本地临时目录
    const std::string local_file_path = DownloadExcelFile(request_id, fastdfs_file_id);

    // 委托给 Excel 解析器完成, 解析失败时异常向上抛出由 RPC 层捕获
    return excel_parse_->GetWorksheetNames(local_file_path);
}

std::vector<proto::WorksheetData> ExcelParseBusiness::ParseWorksheets(
    const std::string& request_id, const std::string& fastdfs_file_id,
    const std::vector<std::string>& worksheets)
{
    // RAII 守护 : 无论解析成功还是中途抛出异常, 函数结束时都清理请求级临时目录
    TempDirGuard temp_dir_guard(request_id, BuildTempDirPath(request_id));

    // 从 FastDFS 下载 Excel 文件到本地临时目录
    const std::string local_file_path = DownloadExcelFile(request_id, fastdfs_file_id);

    std::vector<proto::WorksheetData> proto_worksheets;
    proto_worksheets.reserve(worksheets.size());

    // 逐个解析 worksheet 并转换为 proto 结构化数据, 任一表解析失败整体抛出异常
    for (const std::string& worksheet_name : worksheets)
    {
        WorksheetData worksheet_data = excel_parse_->ParseWorksheet(local_file_path, worksheet_name);
        proto::WorksheetData proto_worksheet;
        BuildWorksheetProto(worksheet_data, &proto_worksheet);
        proto_worksheets.push_back(std::move(proto_worksheet));
    }

    INFO("Excel 解析业务完成, fastdfs_file_id: {} , 解析 worksheet 个数: {}",
         fastdfs_file_id, proto_worksheets.size());
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
