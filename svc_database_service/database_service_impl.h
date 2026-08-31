#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "database_service.pb.h"
#include "svc_database_service/database_business.h"

namespace chat_excel
{
namespace database_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::database_service;

/**
 * @brief 数据库子服务 RPC 接口实现类, 继承 protoc 生成的 DatabaseService 服务基类,
 *        负责解析与校验 RPC 请求参数, 调用数据库业务逻辑层完成业务处理,
 *        并将业务处理结果(错误码与错误信息)填充到 RPC 响应中;
 *        业务处理过程中抛出的异常统一按照业务处理失败的逻辑进行处理
 */
class DatabaseServiceImpl : public proto::DatabaseService
{
public:
    /**
     * @brief 构造函数, 注入数据库业务逻辑对象
     * @param database_business 数据库业务逻辑对象, 由外部构建并管理生命周期
     */
    explicit DatabaseServiceImpl(std::shared_ptr<DatabaseBusiness> database_business);

    ~DatabaseServiceImpl() override = default;

    /**
     * @brief 连接数据库, 根据数据库配置建立连接并返回连接 ID
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、用户 ID 与数据库配置
     * @param response RPC 响应, 携带错误码、错误信息与数据库连接 ID
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ConnectDatabase(google::protobuf::RpcController* controller,
                                 const proto::ConnectDatabaseRequest* request,
                                 proto::ConnectDatabaseResponse* response,
                                 google::protobuf::Closure* done) override;

    /**
     * @brief 断开数据库连接, 删除该连接下的所有临时表并断开底层连接
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID 与数据库连接 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DisconnectDatabase(google::protobuf::RpcController* controller,
                                    const proto::DisconnectDatabaseRequest* request,
                                    proto::DisconnectDatabaseResponse* response,
                                    google::protobuf::Closure* done) override;

    /**
     * @brief 获取数据库表列表, 返回该连接下所有表名(不含临时表)
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID 与数据库连接 ID
     * @param response RPC 响应, 携带错误码、错误信息与表名列表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ListTables(google::protobuf::RpcController* controller,
                            const proto::ListTablesRequest* request,
                            proto::ListTablesResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 获取数据库表数据, 返回表结构信息与分页数据
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID、表名、
     *                是否强制查原表、页码与每页行数
     * @param response RPC 响应, 携带错误码、错误信息与表结构信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetTableData(google::protobuf::RpcController* controller,
                              const proto::GetTableDataRequest* request,
                              proto::GetTableDataResponse* response,
                              google::protobuf::Closure* done) override;

    /**
     * @brief 执行通用 SQL 语句, 查询类 SQL 直接执行, 修改类 SQL 在临时表上执行
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID 与 SQL 语句
     * @param response RPC 响应, 携带错误码、错误信息、查询结果或影响行数
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ExecuteSQL(google::protobuf::RpcController* controller,
                            const proto::ExecuteSQLRequest* request,
                            proto::ExecuteSQLResponse* response,
                            google::protobuf::Closure* done) override;

    /**
     * @brief 获取连接下的临时表列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID 与数据库连接 ID
     * @param response RPC 响应, 携带错误码、错误信息、临时表名列表与是否存在临时表
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetConnTempTables(google::protobuf::RpcController* controller,
                                   const proto::GetConnTempTablesRequest* request,
                                   proto::GetConnTempTablesResponse* response,
                                   google::protobuf::Closure* done) override;

    /**
     * @brief 获取表结构, 返回 JSON 格式的表结构描述
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID 与表名
     * @param response RPC 响应, 携带错误码、错误信息与表结构描述
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetTableStruct(google::protobuf::RpcController* controller,
                                const proto::GetTableStructRequest* request,
                                proto::GetTableStructResponse* response,
                                google::protobuf::Closure* done) override;

    /**
     * @brief 获取指定表的采样数据, 返回 JSON 格式的采样数据
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID、表名与采样条数
     * @param response RPC 响应, 携带错误码、错误信息与采样数据
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void GetSampleData(google::protobuf::RpcController* controller,
                               const proto::GetSampleDataRequest* request,
                               proto::GetSampleDataResponse* response,
                               google::protobuf::Closure* done) override;

    /**
     * @brief 导入 Excel 的 WorkSheet 数据到数据库中
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID、表名与 WorkSheet 数据
     * @param response RPC 响应, 携带错误码、错误信息、表名与导入的行数
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void ImportExcelData(google::protobuf::RpcController* controller,
                                 const proto::ImportExcelDataRequest* request,
                                 proto::ImportExcelDataResponse* response,
                                 google::protobuf::Closure* done) override;

    /**
     * @brief 删除 Excel 文件表, 支持批量删除并返回成功与失败的表名列表
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID、会话 ID、数据库连接 ID与要删除的表名列表
     * @param response RPC 响应, 携带错误码、错误信息与删除结果
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DropTableExcel(google::protobuf::RpcController* controller,
                                const proto::DropTableExcelRequest* request,
                                proto::DropTableExcelResponse* response,
                                google::protobuf::Closure* done) override;

    /**
     * @brief 删除用户对应的所有数据库连接
     * @param controller RPC 控制器
     * @param request RPC 请求, 携带请求 ID与用户 ID
     * @param response RPC 响应, 携带错误码与错误信息
     * @param done RPC 结束回调, 由 brpc::ClosureGuard 管理生命周期
     */
    virtual void DeleteUserAllConn(google::protobuf::RpcController* controller,
                                   const proto::DeleteUserAllConnRequest* request,
                                   proto::DeleteUserAllConnResponse* response,
                                   google::protobuf::Closure* done) override;

private:
    // 数据库业务逻辑对象, 由外部构建并管理生命周期
    std::shared_ptr<DatabaseBusiness> database_business_;
};

} // namespace database_service
} // namespace chat_excel
