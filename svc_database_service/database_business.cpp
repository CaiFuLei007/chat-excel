#include "database_business.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <utility>

#include <brpc/controller.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include <jsoncpp/json/json.h>

#include "common/exception.h"
#include "common/service_flags.h"
#include "file_service.pb.h"
#include "svc_database_service/driver/sql_validator.h"

// fdfs.h 依赖的 fastcommon/common_define.h 定义了 byte 宏(signed char),
// 会污染标准库与第三方库头文件, 因此必须放在所有头文件之后导入,
// 导入后立即取消定义, 避免污染本文件后续代码
#include <cpp-toolkit/fdfs.h>
#undef byte

namespace chat_excel
{

// 文件子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端调用
namespace file_proto = ::chat_excel_proto::file_service;

namespace database_service
{

namespace
{

// 文件子服务 RPC 调用超时时间(毫秒), 涉及文件下载等大数据量传输, 超时时间较长
constexpr int kFileRpcTimeoutMilliseconds = 30 * 1000;

// SQLite 数据库文件本地保存目录名(位于可执行程序当前目录下)
constexpr char kSQLiteFileDirName[] = "sqlitefile";

// SQLite 数据库本地文件扩展名
constexpr char kSQLiteFileExtension[] = ".db";

// 临时表名称分隔符, 临时表名格式 : 原始表名_temp_毫秒时间戳
constexpr char kTempTableSeparator[] = "_temp_";

// Excel 数据批量导入的单批行数
constexpr int32_t kImportBatchSize = 100;

// 采样数据默认条数
constexpr int32_t kDefaultSampleLimit = 5;

// 表数据默认每页行数
constexpr int32_t kDefaultPageSize = 50;

// Excel 数据表自增主键列名
constexpr char kImportPrimaryKeyColumn[] = "id";

// Excel 解析列类型 : 布尔类型
constexpr const char* kExcelBooleanColumnType = "BOOLEAN";

// 布尔真值的全部文本表示形式(忽略大小写匹配)
const std::vector<std::string> kBooleanTrueTexts = {"true", "1", "t", "yes", "y"};

// 布尔假值的全部文本表示形式(忽略大小写匹配)
const std::vector<std::string> kBooleanFalseTexts = {"false", "0", "f", "no", "n"};

// 布尔真值转换后的数据库存储值
constexpr const char* kBooleanTrueValue = "1";

// 布尔假值转换后的数据库存储值
constexpr const char* kBooleanFalseValue = "0";

/**
 * @brief 获取当前系统时间的毫秒时间戳
 * @return 当前毫秒时间戳
 */
int64_t NowMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/**
 * @brief 将 ASCII 字母统一转为小写, 用于布尔等文本的忽略大小写比较
 * @param text 输入字符串
 * @return 转换后的全小写字符串
 */
std::string ToLowerAscii(const std::string& text)
{
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char character)
                   {
                       return static_cast<char>(std::tolower(character));
                   });
    return lower_text;
}

/**
 * @brief 将 BOOLEAN 列的单元格文本转换为数据库存储值 : 真值文本(忽略大小写)
 *        转换为 "1", 假值文本转换为 "0", 无法识别的文本原样返回
 * @param cell_value BOOLEAN 列的单元格文本
 * @return 转换后的数据库存储值
 */
std::string ConvertBooleanCellValue(const std::string& cell_value)
{
    // 统一转小写后与真值/假值文本集合比较
    const std::string lower_value = ToLowerAscii(cell_value);
    if (std::find(kBooleanTrueTexts.begin(), kBooleanTrueTexts.end(), lower_value) !=
        kBooleanTrueTexts.end())
    {
        return kBooleanTrueValue;
    }
    if (std::find(kBooleanFalseTexts.begin(), kBooleanFalseTexts.end(), lower_value) !=
        kBooleanFalseTexts.end())
    {
        return kBooleanFalseValue;
    }
    return cell_value;
}

} // namespace

DatabaseBusiness::DatabaseBusiness(std::shared_ptr<DataBaseConnectionManager> connection_manager,
                                   cpp_toolkit::ChannelManager::Ptr channel_manager)
    : connection_manager_(std::move(connection_manager)),
      channel_manager_(std::move(channel_manager))
{
    INFO("数据库子服务业务逻辑对象构建完成");
}

std::string DatabaseBusiness::ConnectDatabase(const std::string& request_id,
                                              const std::string& session_id,
                                              const std::string& user_id,
                                              const database_proto::DatabaseConfig& database_config)
{
    // 校验数据库类型有效性
    if (database_config.type() == database_proto::DATABASE_TYPE_UNKNOWN)
    {
        ERR("连接数据库失败, 数据库类型无效, requestId: {}", request_id);
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }

    // 将 proto 配置转换为驱动层配置对象(SQLite 配置内部会先下载文件到本地)
    std::shared_ptr<DatabaseConfig> driver_config =
        ConvertDatabaseConfig(request_id, session_id, user_id, database_config);

    // 通过驱动工厂创建驱动实例并建立连接
    std::shared_ptr<DatabaseDriver> driver = DatabaseDriverFactory::CreateDriver(driver_config);
    driver->Connect();

    // 登记连接并返回连接 ID
    std::string connection_id = connection_manager_->CreateConnection(driver, user_id);
    INFO("连接数据库成功, requestId: {}, userId: {}, connectionId: {}", request_id, user_id,
         connection_id);
    return connection_id;
}

void DatabaseBusiness::DisconnectDatabase(const std::string& request_id,
                                          const std::string& connection_id)
{
    // excel_connection 全局连接受保护, 禁止断开
    if (connection_id == kExcelConnectionId)
    {
        ERR("禁止断开 excel_connection 全局连接, requestId: {}", request_id);
        throw ChatExcelException(ErrorCode::DB_CONNECTION_PROTECTED);
    }

    // 获取连接信息(连接不存在时抛出异常)
    std::shared_ptr<ConnectionInfo> connection_info = connection_manager_->GetConnection(connection_id);

    // 删除该连接下的所有临时表
    DropAllTempTables(connection_info->GetDriver(), request_id, connection_id);

    // 移除连接信息并断开底层连接
    connection_manager_->RemoveConnection(connection_id);
    INFO("断开数据库连接成功, requestId: {}, connectionId: {}", request_id, connection_id);
}

std::vector<std::string> DatabaseBusiness::ListTables(const std::string& request_id,
                                                      const std::string& connection_id)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 查询所有表名并过滤临时表
    std::vector<std::string> all_tables = ListAllTables(driver);
    std::vector<std::string> tables;
    tables.reserve(all_tables.size());
    for (const std::string& table_name : all_tables)
    {
        // 隐藏临时表, 临时表通过 GetConnTempTables 接口单独查看
        if (table_name.find(kTempTableSeparator) == std::string::npos)
        {
            tables.push_back(table_name);
        }
    }
    INFO("获取数据库表列表成功, requestId: {}, connectionId: {}, 表数量: {}", request_id,
         connection_id, tables.size());
    return tables;
}

database_proto::TableSchemaInfo DatabaseBusiness::GetTableData(const std::string& request_id,
                                                               const std::string& connection_id,
                                                               const std::string& table_name,
                                                               bool force_original,
                                                               int32_t page_number,
                                                               int32_t page_size)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 校验表名合法性
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("获取表数据失败, 表名非法, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }

    // 分页参数缺省时的默认值 : 页码从 1 开始, 每页 50 行
    int32_t current_page = page_number > 0 ? page_number : 1;
    int32_t page_rows = page_size > 0 ? page_size : kDefaultPageSize;

    // 确定实际查询的表名 : 存在临时表且未强制查原表时查询临时表, 展示修改类 SQL 的执行效果
    std::string query_table_name = ResolveQueryTableName(connection_id, table_name, force_original);

    // 查询表结构信息(列名与列类型), 直接调用驱动层的 GetTableStructure 接口
    TableInfo table_info = driver->GetTableStructure(query_table_name);

    // 查询总行数
    QueryResult count_result =
        driver->ExecuteQuery("SELECT COUNT(*) FROM " + driver->QuoteIdentifier(query_table_name));
    if (!count_result.IsSuccess() || count_result.GetRowCount() == 0)
    {
        ERR("查询表总行数失败, requestId: {}, table_name: {}, 错误: {}", request_id,
            query_table_name,
            count_result.IsSuccess() ? "查询结果为空" : count_result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }
    int32_t total_rows = std::stoi(count_result.GetRow(0)[0]);

    // 计算总页数与分页偏移量
    int32_t total_pages = (total_rows + page_rows - 1) / page_rows;
    int32_t offset = (current_page - 1) * page_rows;

    // 查询当前页数据
    QueryResult data_result =
        driver->ExecuteQuery("SELECT * FROM " + driver->QuoteIdentifier(query_table_name) + " LIMIT " +
                             std::to_string(page_rows) + " OFFSET " + std::to_string(offset));
    if (!data_result.IsSuccess())
    {
        ERR("查询表数据失败, requestId: {}, table_name: {}, 错误: {}", request_id, query_table_name,
            data_result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }

    // 填充表结构信息(列名与列类型)
    database_proto::TableSchemaInfo table_schema;
    for (const ColumnInfo& column_info : table_info.columns)
    {
        database_proto::ColumnInfo* proto_column_info = table_schema.add_column_info();
        proto_column_info->set_name(column_info.name);
        proto_column_info->set_type(column_info.type);
    }

    // 填充分页数据
    database_proto::TableData* table_data = table_schema.mutable_table_data();
    table_data->set_total_rows(total_rows);
    table_data->set_current_page(current_page);
    table_data->set_total_pages(total_pages);
    table_data->set_page_size(page_rows);
    for (size_t row_index = 0; row_index < data_result.GetRowCount(); ++row_index)
    {
        database_proto::Row* row = table_data->add_rows();
        for (const std::string& cell : data_result.GetRow(row_index))
        {
            row->add_cells(cell);
        }
    }

    INFO("获取表数据成功, requestId: {}, connectionId: {}, table_name: {}, 查询表: {}, 总行数: {}",
         request_id, connection_id, table_name, query_table_name, total_rows);
    return table_schema;
}

QueryResult DatabaseBusiness::ExecuteSql(const std::string& request_id,
                                         const std::string& connection_id, const std::string& sql)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 查询类 SQL 直接在原表上执行, 不会对数据库安全造成影响
    if (SQLValidator::IsReadOnlySql(sql))
    {
        INFO("执行查询类 SQL, requestId: {}, connectionId: {}, sql: {}", request_id, connection_id,
             sql);
        return driver->ExecuteQuery(sql);
    }

    // 修改类 SQL 在临时表上执行, 保护原始表数据
    return ExecuteModifySqlWithTempTable(driver, request_id, connection_id, sql);
}

std::string DatabaseBusiness::GetTableStructure(const std::string& request_id,
                                                const std::string& connection_id,
                                                const std::string& table_name)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 校验表名合法性
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("获取表结构失败, 表名非法, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }

    // 查询原表的表结构信息(临时表与原表结构一致), 直接调用驱动层的 GetTableStructure 接口
    TableInfo table_info = driver->GetTableStructure(table_name);

    // 组装 JSON 格式的表结构描述
    Json::Value table_struct_json;
    table_struct_json["table_name"] = table_info.name;
    Json::Value columns_json(Json::arrayValue);
    for (const ColumnInfo& column_info : table_info.columns)
    {
        Json::Value column_json;
        column_json["name"] = column_info.name;
        column_json["type"] = column_info.type;
        column_json["nullable"] = column_info.nullable;
        column_json["is_primary_key"] = column_info.is_primary_key;
        column_json["auto_increment"] = column_info.auto_increment;
        column_json["default_value"] = column_info.default_value;
        columns_json.append(column_json);
    }
    table_struct_json["columns"] = columns_json;

    // 序列化为 JSON 字符串
    std::string table_struct;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(table_struct_json, table_struct))
    {
        ERR("表结构序列化失败, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_SERIALIZE_ERROR);
    }
    INFO("获取表结构成功, requestId: {}, connectionId: {}, table_name: {}", request_id,
         connection_id, table_name);
    return table_struct;
}

std::string DatabaseBusiness::GetSampleData(const std::string& request_id,
                                            const std::string& connection_id,
                                            const std::string& table_name, int32_t limit)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 校验表名合法性
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("获取采样数据失败, 表名非法, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }

    // 采样条数缺省时默认采样 5 条
    int32_t sample_limit = limit > 0 ? limit : kDefaultSampleLimit;

    // 确定实际查询的表名 : 存在临时表时采样临时表数据, 与前端预览逻辑保持一致
    std::string query_table_name = ResolveQueryTableName(connection_id, table_name, false);

    // 查询采样数据
    QueryResult result = driver->ExecuteQuery("SELECT * FROM " +
                                              driver->QuoteIdentifier(query_table_name) + " LIMIT " +
                                              std::to_string(sample_limit));
    if (!result.IsSuccess())
    {
        ERR("查询采样数据失败, requestId: {}, table_name: {}, 错误: {}", request_id,
            query_table_name, result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }

    // 组装 JSON 格式的采样数据 : 列名集合 + 行数据集合
    Json::Value sample_data_json;
    Json::Value columns_json(Json::arrayValue);
    for (const std::string& column_name : result.GetColumnNames())
    {
        columns_json.append(column_name);
    }
    sample_data_json["columns"] = columns_json;

    Json::Value rows_json(Json::arrayValue);
    for (size_t row_index = 0; row_index < result.GetRowCount(); ++row_index)
    {
        Json::Value row_json(Json::arrayValue);
        for (const std::string& cell : result.GetRow(row_index))
        {
            row_json.append(cell);
        }
        rows_json.append(row_json);
    }
    sample_data_json["rows"] = rows_json;

    // 序列化为 JSON 字符串
    std::string sample_data;
    if (!cpp_toolkit::JsonUtil::SerializeCompact(sample_data_json, sample_data))
    {
        ERR("采样数据序列化失败, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_SERIALIZE_ERROR);
    }
    INFO("获取采样数据成功, requestId: {}, connectionId: {}, table_name: {}, 查询表: {}, 采样条数: {}",
         request_id, connection_id, table_name, query_table_name, result.GetRowCount());
    return sample_data;
}

int32_t DatabaseBusiness::ImportExcelData(const std::string& request_id,
                                          const std::string& connection_id,
                                          const std::string& table_name,
                                          const excel_parse_proto::WorksheetData& worksheet_data)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    // 校验表名与列名合法性
    if (!SQLValidator::IsValidTableName(table_name))
    {
        ERR("导入 Excel 数据失败, 表名非法, requestId: {}, table_name: {}", request_id, table_name);
        throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
    }
    for (const excel_parse_proto::ProtoColumnInfo& column_info : worksheet_data.columns())
    {
        if (!SQLValidator::IsValidColumnName(column_info.name()))
        {
            ERR("导入 Excel 数据失败, 列名非法, requestId: {}, column_name: {}", request_id,
                column_info.name());
            throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
        }
    }

    // 表已存在时先删除旧表(重新导入 = 覆盖旧数据), 同时清理与旧表关联的临时表
    if (IsTableExists(driver, table_name))
    {
        INFO("导入 Excel 数据, 表已存在, 先删除旧表, requestId: {}, table_name: {}", request_id,
             table_name);
        DropRelatedTempTables(driver, request_id, connection_id, table_name);
        QueryResult drop_result = driver->DropTable(table_name);
        if (!drop_result.IsSuccess())
        {
            ERR("删除旧表失败, requestId: {}, table_name: {}, 错误: {}", request_id, table_name,
                drop_result.GetErrorMessage());
            throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
        }
    }

    // 创建数据库表 : 自增 id 主键列 + 数据列(主键自带索引)
    CreateImportTable(driver, request_id, table_name, worksheet_data);

    // 批量导入数据, 每批 100 条一个事务, 某批失败时返回已成功导入的行数
    int32_t imported_rows = ImportWorksheetRows(driver, request_id, table_name, worksheet_data);
    INFO("导入 Excel 数据完成, requestId: {}, connectionId: {}, table_name: {}, 导入行数: {}",
         request_id, connection_id, table_name, imported_rows);
    return imported_rows;
}

DropTableResult DatabaseBusiness::DropTableExcel(const std::string& request_id,
                                                 const std::string& connection_id,
                                                 const std::vector<std::string>& table_names)
{
    std::shared_ptr<DatabaseDriver> driver = GetDriverByConnectionId(request_id, connection_id);

    DropTableResult result;
    for (const std::string& table_name : table_names)
    {
        // 校验表名合法性, 非法表名记录到失败列表
        if (!SQLValidator::IsValidTableName(table_name))
        {
            ERR("删除 Excel 文件表失败, 表名非法, requestId: {}, table_name: {}", request_id,
                table_name);
            result.failed_tables.push_back(table_name);
            continue;
        }

        try
        {
            // 删除与该表关联的所有临时表
            DropRelatedTempTables(driver, request_id, connection_id, table_name);

            // 删除原表
            QueryResult drop_result = driver->DropTable(table_name);
            if (!drop_result.IsSuccess())
            {
                ERR("删除 Excel 文件表失败, requestId: {}, table_name: {}, 错误: {}", request_id,
                    table_name, drop_result.GetErrorMessage());
                result.failed_tables.push_back(table_name);
                continue;
            }
            result.dropped_tables.push_back(table_name);
            result.dropped_count += 1;
        }
        catch (const ChatExcelException& e)
        {
            ERR("删除 Excel 文件表异常, requestId: {}, table_name: {}, 错误: {}", request_id,
                table_name, e.what());
            result.failed_tables.push_back(table_name);
        }
    }

    // SQLite 数据库 : 删除本地 SQLite 数据库文件
    if (driver->GetDatabaseType() == DatabaseType::SQLITE)
    {
        std::shared_ptr<ConnectionInfo> connection_info = connection_manager_->GetConnection(connection_id);
        std::shared_ptr<SQLiteConfig> sqlite_config =
            std::dynamic_pointer_cast<SQLiteConfig>(connection_info->GetDriver()->GetConfig());
        if (sqlite_config != nullptr && !sqlite_config->database_file_path.empty())
        {
            std::error_code file_system_error;
            std::filesystem::remove(sqlite_config->database_file_path, file_system_error);
            if (file_system_error)
            {
                ERR("删除本地 SQLite 数据库文件失败, requestId: {}, 路径: {}, 错误: {}", request_id,
                    sqlite_config->database_file_path, file_system_error.message());
            }
            else
            {
                INFO("删除本地 SQLite 数据库文件成功, requestId: {}, 路径: {}", request_id,
                     sqlite_config->database_file_path);
            }
        }
    }

    // 移除连接信息并断开底层连接(excel_connection 全局连接受保护, 跳过移除)
    if (connection_id != kExcelConnectionId)
    {
        connection_manager_->RemoveConnection(connection_id);
    }

    INFO("删除 Excel 文件表完成, requestId: {}, connectionId: {}, 成功数量: {}, 失败数量: {}",
         request_id, connection_id, result.dropped_count, result.failed_tables.size());
    return result;
}

std::string DatabaseBusiness::GetSQLiteFileId(const std::string& request_id,
                                              const std::string& session_id,
                                              const std::string& user_id,
                                              const std::string& file_id) const
{
    // 参数校验 : 用户 ID 与文件 ID 不能为空
    if (user_id.empty() || file_id.empty())
    {
        ERR("获取 SQLite 文件失败, 参数为空, requestId: {}, userId: {}, fileId: {}", request_id,
            user_id, file_id);
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }

    // 获取文件子服务信道
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(FLAGS_file_service);
    if (channel == nullptr)
    {
        ERR("获取文件子服务信道失败, requestId: {}, serviceName: {}", request_id, FLAGS_file_service);
        throw ChatExcelException(ErrorCode::DB_FILE_RPC_ERROR);
    }

    // 构建 RPC 请求, 调用文件子服务获取 SQLite 文件在 FastDFS 中的文件 ID
    file_proto::GetSQLiteFileRequest rpc_request;
    rpc_request.set_request_id(request_id);
    rpc_request.set_session_id(session_id);
    rpc_request.set_file_id(file_id);
    rpc_request.set_user_id(user_id);

    file_proto::GetSQLiteFileResponse rpc_response;
    brpc::Controller controller;
    controller.set_timeout_ms(kFileRpcTimeoutMilliseconds);
    file_proto::FileService_Stub stub(channel.get());
    stub.GetSQLiteFile(&controller, &rpc_request, &rpc_response, nullptr);

    // RPC 调用失败(网络超时, 服务不可达等)
    if (controller.Failed())
    {
        ERR("文件子服务 RPC 调用失败, requestId: {}, 错误信息: {}", request_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::DB_FILE_RPC_ERROR);
    }

    // RPC 调用成功但业务处理失败
    if (rpc_response.error_code() != static_cast<int32_t>(ErrorCode::SUCCESS))
    {
        ERR("获取 SQLite 文件信息失败, requestId: {}, errorCode: {}, 错误信息: {}", request_id,
            rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(ErrorCode::DB_FILE_RPC_ERROR);
    }

    // 创建本地保存目录 : 可执行程序当前目录/sqlitefile/{用户 ID}/
    std::filesystem::path local_dir_path = std::filesystem::current_path() / kSQLiteFileDirName / user_id;
    std::error_code file_system_error;
    std::filesystem::create_directories(local_dir_path, file_system_error);
    if (file_system_error)
    {
        ERR("创建 SQLite 文件目录失败, requestId: {}, 路径: {}, 错误: {}", request_id,
            local_dir_path.string(), file_system_error.message());
        throw ChatExcelException(ErrorCode::DB_LOCAL_FILE_ERROR);
    }

    // 通过 FastDFS 文件 ID 将文件下载到本地目录, 本地文件名使用文件 ID 命名
    std::filesystem::path local_file_path = local_dir_path / (file_id + kSQLiteFileExtension);
    if (!cpp_toolkit::FdfsClient::DownloadToFile(rpc_response.result().fdfs_file_id(),
                                                 local_file_path.string()))
    {
        ERR("从 FastDFS 下载 SQLite 文件失败, requestId: {}, fdfsFileId: {}", request_id,
            rpc_response.result().fdfs_file_id());
        throw ChatExcelException(ErrorCode::DB_FDFS_DOWNLOAD_ERROR);
    }

    INFO("SQLite 文件下载成功, requestId: {}, fileId: {}, 本地路径: {}", request_id, file_id,
         local_file_path.string());
    return local_file_path.string();
}

std::vector<DatabaseTempTableInfo> DatabaseBusiness::GetConnTempTables(
    const std::string& request_id, const std::string& connection_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = temp_tables_.find(connection_id);
    if (iter == temp_tables_.end())
    {
        INFO("连接下没有临时表, requestId: {}, connectionId: {}", request_id, connection_id);
        return {};
    }
    INFO("获取连接临时表列表成功, requestId: {}, connectionId: {}, 临时表数量: {}", request_id,
         connection_id, iter->second.size());
    return iter->second;
}

void DatabaseBusiness::DeleteUserAllConn(const std::string& request_id, const std::string& user_id)
{
    // 获取该用户名下的所有连接 ID
    std::vector<std::string> connection_ids = connection_manager_->GetUserConnectionIds(user_id);

    // 逐连接删除临时表以及连接信息, 单个连接删除失败不影响其余连接
    for (const std::string& connection_id : connection_ids)
    {
        try
        {
            std::shared_ptr<ConnectionInfo> connection_info =
                connection_manager_->GetConnection(connection_id);
            DropAllTempTables(connection_info->GetDriver(), request_id, connection_id);
            connection_manager_->RemoveConnection(connection_id);
        }
        catch (const ChatExcelException& e)
        {
            ERR("删除用户连接失败, requestId: {}, userId: {}, connectionId: {}, 错误: {}", request_id,
                user_id, connection_id, e.what());
        }
    }
    INFO("用户的所有数据库连接删除完成, requestId: {}, userId: {}, 连接数量: {}", request_id,
         user_id, connection_ids.size());
}

std::shared_ptr<DatabaseConfig> DatabaseBusiness::ConvertDatabaseConfig(
    const std::string& request_id, const std::string& session_id, const std::string& user_id,
    const database_proto::DatabaseConfig& database_config) const
{
    // MySQL 数据库配置转换
    if (database_config.type() == database_proto::DATABASE_TYPE_MYSQL)
    {
        if (!database_config.has_mysql_config())
        {
            ERR("连接数据库失败, 缺少 MySQL 配置, requestId: {}", request_id);
            throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
        }
        const database_proto::MySQLDatabaseConfig& mysql_config = database_config.mysql_config();
        auto config = std::make_shared<MySQLConfig>();
        config->host = mysql_config.host();
        config->port = mysql_config.port();
        config->user_name = mysql_config.username();
        config->password = mysql_config.password();
        config->database_name = mysql_config.name();
        // 字符集通过其他连接配置传递
        config->other_config.emplace("charset", mysql_config.charset());
        return config;
    }

    // SQLite 数据库配置转换
    if (!database_config.has_sqlite_config())
    {
        ERR("连接数据库失败, 缺少 SQLite 配置, requestId: {}", request_id);
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }
    const database_proto::SQLiteDatabaseConfig& sqlite_config = database_config.sqlite_config();

    // 通过文件子服务获取 SQLite 文件并下载到本地
    std::string local_file_path = GetSQLiteFileId(request_id, session_id, user_id, sqlite_config.file_id());

    auto config = std::make_shared<SQLiteConfig>();
    config->database_file_path = local_file_path;
    // 只读配置通过其他连接配置传递
    config->other_config.emplace("readonly", sqlite_config.readonly() ? "true" : "false");
    return config;
}

std::shared_ptr<DatabaseDriver> DatabaseBusiness::GetDriverByConnectionId(
    const std::string& request_id, const std::string& connection_id) const
{
    // 获取连接信息(连接不存在时抛出异常, 获取成功自动刷新连接的最近活跃时间)
    std::shared_ptr<ConnectionInfo> connection_info = connection_manager_->GetConnection(connection_id);
    DBG("获取数据库连接成功, requestId: {}, connectionId: {}", request_id, connection_id);
    return connection_info->GetDriver();
}

std::vector<std::string> DatabaseBusiness::ListAllTables(
    const std::shared_ptr<DatabaseDriver>& driver) const
{
    // 直接调用驱动层接口获取所有表名, 数据库方言差异由各驱动自行处理
    return driver->GetAllTablesName();
}

bool DatabaseBusiness::IsTableExists(const std::shared_ptr<DatabaseDriver>& driver,
                                     const std::string& table_name) const
{
    std::vector<std::string> all_tables = ListAllTables(driver);
    return std::find(all_tables.begin(), all_tables.end(), table_name) != all_tables.end();
}

std::string DatabaseBusiness::ResolveQueryTableName(const std::string& connection_id,
                                                    const std::string& table_name,
                                                    bool force_original) const
{
    // 强制查询原表时直接返回原表名
    if (force_original)
    {
        return table_name;
    }

    // 存在临时表时查询临时表, 展示修改类 SQL 的执行效果
    std::string temp_table_name = GetTempTableName(connection_id, table_name);
    return temp_table_name.empty() ? table_name : temp_table_name;
}

std::string DatabaseBusiness::GetTempTableName(const std::string& connection_id,
                                               const std::string& original_table_name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = temp_tables_.find(connection_id);
    if (iter == temp_tables_.end())
    {
        return {};
    }
    // 每个原表对应一个临时表, 取最新登记的一条
    for (auto temp_iter = iter->second.rbegin(); temp_iter != iter->second.rend(); ++temp_iter)
    {
        if (temp_iter->original_table_name == original_table_name)
        {
            return temp_iter->temp_table_name;
        }
    }
    return {};
}

QueryResult DatabaseBusiness::ExecuteModifySqlWithTempTable(const std::shared_ptr<DatabaseDriver>& driver,
                                                            const std::string& request_id,
                                                            const std::string& connection_id,
                                                            const std::string& sql)
{
    // 提取 SQL 中将要修改的目标表名(只读引用的数据源表不需要备份与替换,
    // 危险关键字语句已被驱动层校验拦截, 到达此处均为安全的修改类语句)
    std::vector<std::string> table_names = SQLValidator::ExtractModifyTargetTableNames(sql);
    if (table_names.empty())
    {
        ERR("执行修改类 SQL 失败, 未识别到修改的目标表名, requestId: {}, sql: {}", request_id, sql);
        throw ChatExcelException(ErrorCode::DB_SQL_INVALID);
    }

    // 校验表名合法性
    for (const std::string& table_name : table_names)
    {
        if (!SQLValidator::IsValidTableName(table_name))
        {
            ERR("执行修改类 SQL 失败, 表名非法, requestId: {}, table_name: {}", request_id,
                table_name);
            throw ChatExcelException(ErrorCode::DB_IDENTIFIER_INVALID);
        }
    }

    // 逐表创建临时表并备份数据, 记录原表名到临时表名的替换映射
    std::unordered_map<std::string, std::string> table_name_map;
    table_name_map.reserve(table_names.size());
    for (const std::string& table_name : table_names)
    {
        // 每个原表对应一个临时表, 已存在临时表时先删除旧临时表
        DropRelatedTempTables(driver, request_id, connection_id, table_name);

        // 创建临时表并备份原表数据
        std::string temp_table_name = CreateTempTableAndBackup(driver, request_id, table_name);

        // 登记到临时表管理集合
        DatabaseTempTableInfo temp_table_info;
        temp_table_info.original_table_name = table_name;
        temp_table_info.temp_table_name = temp_table_name;
        temp_table_info.create_time = NowMilliseconds();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            temp_tables_[connection_id].push_back(temp_table_info);
        }
        table_name_map.emplace(table_name, temp_table_name);
        INFO("临时表创建并备份完成, requestId: {}, 原表: {}, 临时表: {}", request_id, table_name,
             temp_table_name);
    }

    // 替换 SQL 中所有原表名为临时表名后在临时表上执行
    std::string replaced_sql = ReplaceTableNames(sql, table_name_map);
    INFO("执行修改类 SQL, requestId: {}, connectionId: {}, sql: {}, 替换后: {}", request_id,
         connection_id, sql, replaced_sql);
    QueryResult result = driver->ExecuteUpdate(replaced_sql);

    // 执行失败时立即删除本次创建的所有临时表回滚
    if (!result.IsSuccess())
    {
        ERR("修改类 SQL 执行失败, 删除本次创建的临时表回滚, requestId: {}, 错误: {}", request_id,
            result.GetErrorMessage());
        for (const auto& [original_table_name, temp_table_name] : table_name_map)
        {
            DatabaseTempTableInfo temp_table_info;
            temp_table_info.original_table_name = original_table_name;
            temp_table_info.temp_table_name = temp_table_name;
            DropSingleTempTable(driver, request_id, connection_id, temp_table_info);
        }
    }
    return result;
}

std::string DatabaseBusiness::CreateTempTableAndBackup(const std::shared_ptr<DatabaseDriver>& driver,
                                                       const std::string& request_id,
                                                       const std::string& original_table_name) const
{
    // 生成临时表名 : 原始表名_temp_毫秒时间戳
    std::string temp_table_name = GenerateTempTableName(original_table_name);
    const std::string quoted_original = driver->QuoteIdentifier(original_table_name);
    const std::string quoted_temp = driver->QuoteIdentifier(temp_table_name);
    bool is_mysql = driver->GetDatabaseType() == DatabaseType::MYSQL;

    // 创建临时表 : MySQL 复制原表表结构, SQLite 建表与备份数据一步完成
    std::string create_sql =
        is_mysql ? "CREATE TABLE " + quoted_temp + " LIKE " + quoted_original
                 : "CREATE TABLE " + quoted_temp + " AS SELECT * FROM " + quoted_original;
    QueryResult create_result = driver->ExecuteUpdate(create_sql);
    if (!create_result.IsSuccess())
    {
        ERR("创建临时表失败, requestId: {}, 原表: {}, 临时表: {}, 错误: {}", request_id,
            original_table_name, temp_table_name, create_result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }

    // MySQL 需要单独执行备份数据语句
    if (is_mysql)
    {
        std::string backup_sql = "INSERT INTO " + quoted_temp + " SELECT * FROM " + quoted_original;
        QueryResult backup_result = driver->ExecuteUpdate(backup_sql);
        if (!backup_result.IsSuccess())
        {
            ERR("备份原表数据到临时表失败, requestId: {}, 原表: {}, 临时表: {}, 错误: {}",
                request_id, original_table_name, temp_table_name, backup_result.GetErrorMessage());
            // 备份失败时删除已创建的临时表, 避免残留空临时表
            try
            {
                driver->DropTable(temp_table_name);
            }
            catch (const ChatExcelException& e)
            {
                ERR("删除残留临时表失败, requestId: {}, 临时表: {}, 错误: {}", request_id,
                    temp_table_name, e.what());
            }
            throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
        }
    }
    return temp_table_name;
}

std::string DatabaseBusiness::GenerateTempTableName(const std::string& original_table_name)
{
    // 临时表后缀 : _temp_毫秒时间戳, 时间戳保证同一原表多次创建的临时表名不重复
    const std::string temp_table_suffix = kTempTableSeparator + std::to_string(NowMilliseconds());

    // 原表名部分超长时按字符数截断, 保证临时表名不超过数据库标识符最大长度限制;
    // 原表名可包含汉字等多字节字符, 按字节截断会误超长或切断多字节字符,
    // 因此截断与长度判断均按 UTF-8 字符进行(后缀为 ASCII, 字节数等于字符数)
    const size_t max_prefix_char_count = kMaxIdentifierLength - temp_table_suffix.size();
    const std::string table_name_prefix =
        SQLValidator::TruncateIdentifier(original_table_name, max_prefix_char_count);
    return table_name_prefix + temp_table_suffix;
}

std::string DatabaseBusiness::ReplaceTableNames(
    const std::string& sql, const std::unordered_map<std::string, std::string>& table_name_map)
{
    // 判断字符是否属于标识符词法字符(字母/数字/下划线/汉字等多字节非 ASCII 字符)
    auto is_word_character = [](unsigned char ch)
    {
        return std::isalnum(ch) != 0 || ch == '_' || ch >= 0x80;
    };

    std::string result = sql;
    for (const auto& [original_table_name, temp_table_name] : table_name_map)
    {
        // 整词匹配替换, 避免表名前缀相同的其他表被误替换(如 users 与 users_backup)
        std::string replaced;
        replaced.reserve(result.size());
        size_t position = 0;
        while (position < result.size())
        {
            size_t found = result.find(original_table_name, position);
            if (found == std::string::npos)
            {
                replaced.append(result, position, std::string::npos);
                break;
            }

            // 匹配位置前后均不是标识符词法字符时才判定为整词匹配
            bool left_is_boundary =
                found == 0 || !is_word_character(static_cast<unsigned char>(result[found - 1]));
            size_t end_position = found + original_table_name.size();
            bool right_is_boundary =
                end_position >= result.size() ||
                !is_word_character(static_cast<unsigned char>(result[end_position]));
            if (left_is_boundary && right_is_boundary)
            {
                replaced.append(result, position, found - position);
                replaced.append(temp_table_name);
            }
            else
            {
                replaced.append(result, position, end_position - position);
            }
            position = end_position;
        }
        result = std::move(replaced);
    }
    return result;
}

void DatabaseBusiness::DropSingleTempTable(const std::shared_ptr<DatabaseDriver>& driver,
                                           const std::string& request_id,
                                           const std::string& connection_id,
                                           const DatabaseTempTableInfo& temp_table_info)
{
    // 从管理集合移除临时表记录
    RemoveTempTableRecord(connection_id, temp_table_info.temp_table_name);

    // 删除数据库中的临时表, 删除失败仅记录日志, 不中断后续清理
    try
    {
        QueryResult result = driver->DropTable(temp_table_info.temp_table_name);
        if (!result.IsSuccess())
        {
            ERR("删除临时表失败, requestId: {}, connectionId: {}, 临时表: {}, 错误: {}", request_id,
                connection_id, temp_table_info.temp_table_name, result.GetErrorMessage());
            return;
        }
        INFO("删除临时表成功, requestId: {}, connectionId: {}, 原表: {}, 临时表: {}", request_id,
             connection_id, temp_table_info.original_table_name, temp_table_info.temp_table_name);
    }
    catch (const ChatExcelException& e)
    {
        ERR("删除临时表异常, requestId: {}, connectionId: {}, 临时表: {}, 错误: {}", request_id,
            connection_id, temp_table_info.temp_table_name, e.what());
    }
}

void DatabaseBusiness::DropRelatedTempTables(const std::shared_ptr<DatabaseDriver>& driver,
                                             const std::string& request_id,
                                             const std::string& connection_id,
                                             const std::string& original_table_name)
{
    // 取出该原表关联的所有临时表信息
    std::vector<DatabaseTempTableInfo> related_temp_tables;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = temp_tables_.find(connection_id);
        if (iter == temp_tables_.end())
        {
            return;
        }
        for (const DatabaseTempTableInfo& temp_table_info : iter->second)
        {
            if (temp_table_info.original_table_name == original_table_name)
            {
                related_temp_tables.push_back(temp_table_info);
            }
        }
    }

    // 逐个删除临时表并移除管理记录
    for (const DatabaseTempTableInfo& temp_table_info : related_temp_tables)
    {
        DropSingleTempTable(driver, request_id, connection_id, temp_table_info);
    }
}

void DatabaseBusiness::DropAllTempTables(const std::shared_ptr<DatabaseDriver>& driver,
                                         const std::string& request_id,
                                         const std::string& connection_id)
{
    // 取出该连接下的所有临时表信息
    std::vector<DatabaseTempTableInfo> all_temp_tables;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = temp_tables_.find(connection_id);
        if (iter == temp_tables_.end())
        {
            return;
        }
        all_temp_tables = iter->second;
    }

    // 逐个删除临时表并移除管理记录
    for (const DatabaseTempTableInfo& temp_table_info : all_temp_tables)
    {
        DropSingleTempTable(driver, request_id, connection_id, temp_table_info);
    }
    INFO("连接下所有临时表删除完成, requestId: {}, connectionId: {}, 数量: {}", request_id,
         connection_id, all_temp_tables.size());
}

void DatabaseBusiness::RemoveTempTableRecord(const std::string& connection_id,
                                             const std::string& temp_table_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = temp_tables_.find(connection_id);
    if (iter == temp_tables_.end())
    {
        return;
    }
    std::vector<DatabaseTempTableInfo>& temp_table_list = iter->second;
    temp_table_list.erase(
        std::remove_if(temp_table_list.begin(), temp_table_list.end(),
                       [&temp_table_name](const DatabaseTempTableInfo& temp_table_info)
                       {
                           return temp_table_info.temp_table_name == temp_table_name;
                       }),
        temp_table_list.end());
    if (temp_table_list.empty())
    {
        temp_tables_.erase(iter);
    }
}

void DatabaseBusiness::CreateImportTable(const std::shared_ptr<DatabaseDriver>& driver,
                                         const std::string& request_id, const std::string& table_name,
                                         const excel_parse_proto::WorksheetData& worksheet_data) const
{
    bool is_mysql = driver->GetDatabaseType() == DatabaseType::MYSQL;

    // 构建建表语句 : 自增 id 主键列(主键自带索引) + 数据列
    std::string create_sql =
        "CREATE TABLE " + driver->QuoteIdentifier(table_name) + " (" +
        driver->QuoteIdentifier(kImportPrimaryKeyColumn) +
        (is_mysql ? " BIGINT PRIMARY KEY AUTO_INCREMENT" : " INTEGER PRIMARY KEY AUTOINCREMENT");
    for (const excel_parse_proto::ProtoColumnInfo& column_info : worksheet_data.columns())
    {
        // 列类型由 Excel 解析子服务给出 : TEXT/BIGINT/DOUBLE/BOOLEAN/DATE,
        // 通过驱动层接口转化为数据库列类型(BOOLEAN 转为整型, DATE 转为文本)
        create_sql += ", " + driver->QuoteIdentifier(column_info.name()) + " " +
                      driver->ConvertExcelColumnType(column_info.type());
    }
    create_sql += ")";
    if (is_mysql)
    {
        // MySQL 表引擎为 InnoDB, 字符集为 utf8mb4, 排序规则为 utf8mb4_unicode_ci
        create_sql += " ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
    }

    QueryResult result = driver->ExecuteUpdate(create_sql);
    if (!result.IsSuccess())
    {
        ERR("创建 Excel 数据表失败, requestId: {}, table_name: {}, sql: {}, 错误: {}", request_id,
            table_name, create_sql, result.GetErrorMessage());
        throw ChatExcelException(ErrorCode::DB_EXECUTE_FAILED);
    }
    INFO("创建 Excel 数据表成功, requestId: {}, table_name: {}, sql: {}", request_id, table_name,
         create_sql);
}

int32_t DatabaseBusiness::ImportWorksheetRows(const std::shared_ptr<DatabaseDriver>& driver,
                                              const std::string& request_id,
                                              const std::string& table_name,
                                              const excel_parse_proto::WorksheetData& worksheet_data) const
{
    // 构建批量插入语句 : INSERT INTO 表 (列...) VALUES (?, ?, ...), 不含自增主键 id 列
    std::string insert_sql = "INSERT INTO " + driver->QuoteIdentifier(table_name) + " (";
    std::string placeholders;
    const int column_count = worksheet_data.columns_size();
    for (int column_index = 0; column_index < column_count; ++column_index)
    {
        const std::string& column_name = worksheet_data.columns(column_index).name();
        insert_sql += (column_index > 0 ? ", " : "") + driver->QuoteIdentifier(column_name);
        placeholders += (column_index > 0 ? ", " : "") + std::string("?");
    }
    insert_sql += ") VALUES (" + placeholders + ")";

    // 标记 BOOLEAN 类型列, 行数据导入时需要将布尔文本转换为 0/1 后再绑定;
    // DATE 列的值保持文本形式, 直接绑定到 TEXT 类型列
    std::vector<bool> is_boolean_column(static_cast<size_t>(column_count), false);
    for (int column_index = 0; column_index < column_count; ++column_index)
    {
        is_boolean_column[static_cast<size_t>(column_index)] =
            worksheet_data.columns(column_index).type() == kExcelBooleanColumnType;
    }

    // 批量导入数据, 每批 kImportBatchSize 条一个事务, 某批失败时回滚当前批并返回已导入行数
    int32_t imported_rows = 0;
    const int total_rows = worksheet_data.rows_size();
    for (int row_index = 0; row_index < total_rows;)
    {
        // 开启当前批事务
        QueryResult begin_result = driver->BeginTransaction();
        if (!begin_result.IsSuccess())
        {
            ERR("开启导入事务失败, requestId: {}, table_name: {}, 错误: {}", request_id, table_name,
                begin_result.GetErrorMessage());
            return imported_rows;
        }

        // 逐行导入当前批数据
        for (int batch_index = 0; batch_index < kImportBatchSize && row_index < total_rows;
             ++batch_index, ++row_index)
        {
            const excel_parse_proto::RowData& row_data = worksheet_data.rows(row_index);

            // 收集当前行的绑定参数, 缺失或为空的单元格统一绑定为 NULL
            std::vector<ParameterWrapper> parameters;
            parameters.reserve(column_count);
            for (int column_index = 0; column_index < column_count; ++column_index)
            {
                if (column_index < row_data.cells_size() &&
                    !row_data.cells(column_index).value().empty())
                {
                    // BOOLEAN 列的文本值转换为 0/1 后再绑定
                    if (is_boolean_column[static_cast<size_t>(column_index)])
                    {
                        parameters.emplace_back(
                            ConvertBooleanCellValue(row_data.cells(column_index).value()));
                    }
                    else
                    {
                        parameters.emplace_back(row_data.cells(column_index).value());
                    }
                }
                else
                {
                    parameters.emplace_back();
                }
            }

            QueryResult insert_result = driver->ExecutePreparedUpdate(insert_sql, parameters);
            if (!insert_result.IsSuccess())
            {
                // 某批导入失败, 回滚当前批事务, 返回已成功导入的行数
                ERR("导入 Excel 行数据失败, requestId: {}, table_name: {}, 行号: {}, 错误: {}",
                    request_id, table_name, row_index, insert_result.GetErrorMessage());
                driver->RollbackTransaction();
                return imported_rows;
            }
            imported_rows += 1;
        }

        // 提交当前批事务
        QueryResult commit_result = driver->CommitTransaction();
        if (!commit_result.IsSuccess())
        {
            ERR("提交导入事务失败, requestId: {}, table_name: {}, 错误: {}", request_id, table_name,
                commit_result.GetErrorMessage());
            return imported_rows;
        }
    }
    return imported_rows;
}

} // namespace database_service
} // namespace chat_excel
