#include "svc_database_service/database_service_impl.h"

#include <string>
#include <utility>
#include <vector>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>

#include "common/exception.h"
#include "svc_database_service/driver/sql_validator.h"

namespace chat_excel
{
namespace database_service
{

namespace
{

/**
 * @brief 将错误码与错误码描述填充到 RPC 响应中
 * @param response RPC 响应对象
 * @param error_code 错误码
 * @param detail 错误详情, 非空时追加到错误码描述之后, 供上层定位具体错误原因
 */
template <typename ResponseType>
void SetErrorResponse(ResponseType* response, ErrorCode error_code, const std::string& detail = "")
{
    response->set_error_code(static_cast<int>(error_code));
    if (detail.empty())
    {
        response->set_error_msg(ErrorMessage(error_code));
    }
    else
    {
        response->set_error_msg(ErrorMessage(error_code) + " : " + detail);
    }
}

} // namespace

DatabaseServiceImpl::DatabaseServiceImpl(std::shared_ptr<DatabaseBusiness> database_business)
    : database_business_(std::move(database_business))
{
    INFO("数据库子服务 RPC 接口实现对象构建完成");
}

void DatabaseServiceImpl::ConnectDatabase(google::protobuf::RpcController* /*controller*/,
                                           const proto::ConnectDatabaseRequest* request,
                                           proto::ConnectDatabaseResponse* response,
                                           google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 会话 ID、用户 ID、数据库配置均不能为空
        if (request->session_id().empty())
        {
            ERR("ConnectDatabase 接口请求参数错误, session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_SESSION_ID_EMPTY);
            return;
        }
        else if (request->user_id().empty())
        {
            ERR("ConnectDatabase 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (!request->has_database() ||
                 request->database().type() == proto::DATABASE_TYPE_UNKNOWN)
        {
            ERR("ConnectDatabase 接口请求参数错误, 数据库配置缺失或类型无效, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_CONFIG_INVALID);
            return;
        }

        // 调用业务逻辑层连接数据库, 失败时业务逻辑层抛出异常
        const std::string connection_id = database_business_->ConnectDatabase(
            request->request_id(), request->session_id(), request->user_id(),
            request->database());

        // 填充连接数据库结果, 成功无需添加成功描述信息
        response->mutable_result()->set_connection_id(connection_id);
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ConnectDatabase 接口业务处理异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ConnectDatabase 接口非预期异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::DisconnectDatabase(google::protobuf::RpcController* /*controller*/,
                                            const proto::DisconnectDatabaseRequest* request,
                                            proto::DisconnectDatabaseResponse* response,
                                            google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 不能为空
        if (request->connection_id().empty())
        {
            ERR("DisconnectDatabase 接口请求参数错误, connection_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层断开数据库连接, 失败时业务逻辑层抛出异常
        database_business_->DisconnectDatabase(request->request_id(), request->connection_id());
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DisconnectDatabase 接口业务处理异常, request_id: {}, connection_id: {}, 错误信息: {}",
            request->request_id(), request->connection_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DisconnectDatabase 接口非预期异常, request_id: {}, connection_id: {}, 错误信息: {}",
            request->request_id(), request->connection_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::ListTables(google::protobuf::RpcController* /*controller*/,
                                     const proto::ListTablesRequest* request,
                                     proto::ListTablesResponse* response,
                                     google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 不能为空
        if (request->db_connect_id().empty())
        {
            ERR("ListTables 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取数据库表列表, 失败时业务逻辑层抛出异常
        const std::vector<std::string> tables =
            database_business_->ListTables(request->request_id(), request->db_connect_id());

        // 填充表名列表, 成功无需添加成功描述信息
        proto::ListTablesResult* result = response->mutable_result();
        for (const std::string& table_name : tables)
        {
            result->add_tables(table_name);
        }
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ListTables 接口业务处理异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ListTables 接口非预期异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::GetTableData(google::protobuf::RpcController* /*controller*/,
                                       const proto::GetTableDataRequest* request,
                                       proto::GetTableDataResponse* response,
                                       google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 与表名均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("GetTableData 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->table_name().empty())
        {
            ERR("GetTableData 接口请求参数错误, table_name 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_TABLE_NAME_EMPTY);
            return;
        }

        // 调用业务逻辑层获取表数据, 失败时业务逻辑层抛出异常
        const proto::TableSchemaInfo table_schema =
            database_business_->GetTableData(request->request_id(), request->db_connect_id(),
                                             request->table_name(), request->force_original(),
                                             request->page_number(), request->page_size());

        // 填充表结构信息与分页数据, 成功无需添加成功描述信息
        *response->mutable_result()->mutable_table_schema() = table_schema;
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetTableData 接口业务处理异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetTableData 接口非预期异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::ExecuteSQL(google::protobuf::RpcController* /*controller*/,
                                     const proto::ExecuteSQLRequest* request,
                                     proto::ExecuteSQLResponse* response,
                                     google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 与 SQL 语句均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("ExecuteSQL 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->sql().empty())
        {
            ERR("ExecuteSQL 接口请求参数错误, sql 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_SQL_EMPTY);
            return;
        }

        // 调用业务逻辑层执行 SQL 语句, 失败时业务逻辑层抛出异常
        const QueryResult execute_result =
            database_business_->ExecuteSql(request->request_id(), request->db_connect_id(),
                                            request->sql());

        // SQL 执行失败, 携带执行错误信息返回
        if (!execute_result.IsSuccess())
        {
            ERR("ExecuteSQL 接口 SQL 执行失败, request_id: {}, sql: {}, 错误信息: {}",
                request->request_id(), request->sql(), execute_result.GetErrorMessage());
            SetErrorResponse(response, ErrorCode::DB_EXECUTE_FAILED,
                             execute_result.GetErrorMessage());
            return;
        }

        // 按语句类型填充执行结果 : 查询类填充列名/列类型/行数据, 修改类填充影响行数
        const bool is_query = SQLValidator::IsReadOnlySql(request->sql());
        response->set_is_query(is_query);
        if (is_query)
        {
            for (const std::string& column_name : execute_result.GetColumnNames())
            {
                response->add_columns(column_name);
            }
            for (const std::string& column_type : execute_result.GetColumnTypes())
            {
                response->add_column_types(column_type);
            }
            for (size_t row_index = 0; row_index < execute_result.GetRowCount(); ++row_index)
            {
                proto::Row* row = response->add_rows();
                for (const std::string& cell : execute_result.GetRow(row_index))
                {
                    row->add_cells(cell);
                }
            }
        }
        else
        {
            response->set_affected_rows(static_cast<int32_t>(execute_result.GetAffectedRows()));
        }

        // SQL 执行成功, 成功无需添加成功描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ExecuteSQL 接口业务处理异常, request_id: {}, sql: {}, 错误信息: {}",
            request->request_id(), request->sql(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ExecuteSQL 接口非预期异常, request_id: {}, sql: {}, 错误信息: {}",
            request->request_id(), request->sql(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::GetConnTempTables(google::protobuf::RpcController* /*controller*/,
                                            const proto::GetConnTempTablesRequest* request,
                                            proto::GetConnTempTablesResponse* response,
                                            google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 不能为空
        if (request->db_connect_id().empty())
        {
            ERR("GetConnTempTables 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取连接下的临时表信息, 失败时业务逻辑层抛出异常
        const std::vector<DatabaseTempTableInfo> temp_tables =
            database_business_->GetConnTempTables(request->request_id(),
                                                   request->db_connect_id());

        // 填充临时表名列表与是否存在临时表, 成功无需添加成功描述信息
        for (const DatabaseTempTableInfo& temp_table_info : temp_tables)
        {
            response->add_temp_tables(temp_table_info.temp_table_name);
        }
        response->set_has_temp_tables(!temp_tables.empty());
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetConnTempTables 接口业务处理异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetConnTempTables 接口非预期异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::GetTableStruct(google::protobuf::RpcController* /*controller*/,
                                         const proto::GetTableStructRequest* request,
                                         proto::GetTableStructResponse* response,
                                         google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 与表名均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("GetTableStruct 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->table_name().empty())
        {
            ERR("GetTableStruct 接口请求参数错误, table_name 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_TABLE_NAME_EMPTY);
            return;
        }

        // 调用业务逻辑层获取表结构, 失败时业务逻辑层抛出异常
        const std::string table_struct =
            database_business_->GetTableStructure(request->request_id(),
                                                  request->db_connect_id(), request->table_name());

        // 填充表结构描述, 成功无需添加成功描述信息
        response->set_table_struct(table_struct);
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetTableStruct 接口业务处理异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetTableStruct 接口非预期异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::GetSampleData(google::protobuf::RpcController* /*controller*/,
                                         const proto::GetSampleDataRequest* request,
                                         proto::GetSampleDataResponse* response,
                                         google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 与表名均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("GetSampleData 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->table_name().empty())
        {
            ERR("GetSampleData 接口请求参数错误, table_name 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_TABLE_NAME_EMPTY);
            return;
        }

        // 调用业务逻辑层获取采样数据, 失败时业务逻辑层抛出异常
        const std::string sample_data =
            database_business_->GetSampleData(request->request_id(), request->db_connect_id(),
                                              request->table_name(), request->limit());

        // 填充采样数据, 成功无需添加成功描述信息
        response->set_sample_data(sample_data);
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetSampleData 接口业务处理异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetSampleData 接口非预期异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::ImportExcelData(google::protobuf::RpcController* /*controller*/,
                                          const proto::ImportExcelDataRequest* request,
                                          proto::ImportExcelDataResponse* response,
                                          google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID、表名与 WorkSheet 数据均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("ImportExcelData 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->table_name().empty())
        {
            ERR("ImportExcelData 接口请求参数错误, table_name 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_TABLE_NAME_EMPTY);
            return;
        }
        else if (!request->has_worksheet_data())
        {
            ERR("ImportExcelData 接口请求参数错误, worksheet_data 缺失, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_WORKSHEET_DATA_EMPTY);
            return;
        }

        // 调用业务逻辑层导入 Excel 数据, 失败时业务逻辑层抛出异常;
        // 某批导入失败时业务逻辑层返回已成功导入的行数
        const int32_t imported_rows =
            database_business_->ImportExcelData(request->request_id(),
                                                request->db_connect_id(), request->table_name(),
                                                request->worksheet_data());

        // 填充导入结果(部分失败时仍视为业务处理成功, 通过导入行数体现), 成功无需添加成功描述信息
        proto::ImportExcelDataResult* result = response->mutable_result();
        result->set_table_name(request->table_name());
        result->set_imported_rows(imported_rows);
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("ImportExcelData 接口业务处理异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("ImportExcelData 接口非预期异常, request_id: {}, table_name: {}, 错误信息: {}",
            request->request_id(), request->table_name(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::DropTableExcel(google::protobuf::RpcController* /*controller*/,
                                         const proto::DropTableExcelRequest* request,
                                         proto::DropTableExcelResponse* response,
                                         google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 数据库连接 ID 与表名列表均不能为空
        if (request->db_connect_id().empty())
        {
            ERR("DropTableExcel 接口请求参数错误, db_connect_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY);
            return;
        }
        else if (request->table_names().empty())
        {
            ERR("DropTableExcel 接口请求参数错误, table_names 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_TABLE_NAMES_EMPTY);
            return;
        }

        // 调用业务逻辑层删除 Excel 文件表, 失败时业务逻辑层抛出异常;
        // 部分表删除失败时业务逻辑层返回成功与失败的表名明细
        const DropTableResult drop_result =
            database_business_->DropTableExcel(request->request_id(),
                                               request->db_connect_id(),
                                               {request->table_names().begin(),
                                                request->table_names().end()});

        // 填充删除结果(部分失败时仍视为业务处理成功, 通过失败表名列表体现), 成功无需添加成功描述信息
        proto::DropTableExcelResult* result = response->mutable_result();
        result->set_dropped_count(drop_result.dropped_count);
        for (const std::string& dropped_table : drop_result.dropped_tables)
        {
            result->add_dropped_tables(dropped_table);
        }
        for (const std::string& failed_table : drop_result.failed_tables)
        {
            result->add_failed_tables(failed_table);
        }
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DropTableExcel 接口业务处理异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DropTableExcel 接口非预期异常, request_id: {}, db_connect_id: {}, 错误信息: {}",
            request->request_id(), request->db_connect_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

void DatabaseServiceImpl::DeleteUserAllConn(google::protobuf::RpcController* /*controller*/,
                                            const proto::DeleteUserAllConnRequest* request,
                                            proto::DeleteUserAllConnResponse* response,
                                            google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 不能为空
        if (request->user_id().empty())
        {
            ERR("DeleteUserAllConn 接口请求参数错误, user_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::DB_SERVICE_USER_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层删除用户的所有数据库连接, 失败时业务逻辑层抛出异常
        database_business_->DeleteUserAllConn(request->request_id(), request->user_id());
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DeleteUserAllConn 接口业务处理异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DeleteUserAllConn 接口非预期异常, request_id: {}, user_id: {}, 错误信息: {}",
            request->request_id(), request->user_id(), e.what());
        SetErrorResponse(response, ErrorCode::DB_SERVICE_INTERNAL_ERROR, e.what());
    }
}

} // namespace database_service
} // namespace chat_excel
