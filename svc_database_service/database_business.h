#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpp-toolkit/rpc.h>

#include "database_service.pb.h"
#include "excel_parse_service.pb.h"
#include "svc_database_service/connection_manager.h"

namespace chat_excel
{

// 数据库子服务 proto 生成代码所在命名空间的别名, 简化业务层参数定义
namespace database_proto = ::chat_excel_proto::database_service;

// Excel 解析子服务 proto 生成代码所在命名空间的别名, 简化业务层参数定义
namespace excel_parse_proto = ::chat_excel_proto::excel_parse_service;

namespace database_service
{

// 临时表信息结构, 仅存储数据, 数据之间无关联
struct DatabaseTempTableInfo
{
    // 原始表名称
    std::string original_table_name;

    // 临时表名称
    std::string temp_table_name;

    // 临时表创建时间戳(毫秒)
    int64_t create_time = 0;
};

// 删除 Excel 文件表结果结构, 仅存储数据, 数据之间无关联
struct DropTableResult
{
    // 成功删除的表数量
    int32_t dropped_count = 0;

    // 成功删除的表名列表
    std::vector<std::string> dropped_tables;

    // 删除失败的表名列表
    std::vector<std::string> failed_tables;
};

/**
 * @brief 数据库子服务业务逻辑类, 负责数据库连接管理, 表数据查询, SQL 执行,
 *        Excel 数据导入等业务逻辑的组织与实现;
 *        修改类 SQL 通过临时表机制保护原始数据 : 逐表创建临时表并备份原表数据,
 *        替换 SQL 中的原表名后在临时表上执行, 原始表数据始终保持不变;
 *        SQLite 数据库文件通过 RPC 调用文件子服务获取 FastDFS 文件 ID 后下载到本地
 */
class DatabaseBusiness
{
public:
    /**
     * @brief 构造函数, 注入业务依赖对象, 所有成员变量在构造函数中完成初始化
     * @param connection_manager 数据库连接管理器, 由上层创建并统一管理
     * @param channel_manager RPC 信道管理对象, 由上层创建并统一管理
     */
    DatabaseBusiness(std::shared_ptr<DataBaseConnectionManager> connection_manager,
                     cpp_toolkit::ChannelManager::Ptr channel_manager);

    /**
     * @brief 连接数据库, 根据数据库配置创建驱动实例并建立连接, 返回连接 ID;
     *        SQLite 配置内部会先通过文件子服务下载文件到本地再建立连接
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param session_id 会话 ID
     * @param user_id 用户 ID
     * @param database_config proto 数据库配置(MySQL 配置或 SQLite 配置)
     * @return 数据库连接 ID
     */
    std::string ConnectDatabase(const std::string& request_id, const std::string& session_id,
                                const std::string& user_id,
                                const database_proto::DatabaseConfig& database_config);

    /**
     * @brief 断开数据库连接, 删除该连接下的所有临时表以及连接信息, 最后断开底层连接
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     */
    void DisconnectDatabase(const std::string& request_id, const std::string& connection_id);

    /**
     * @brief 获取数据库表列表, 过滤临时表(临时表通过 GetConnTempTables 接口单独查看)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @return 表名列表
     */
    std::vector<std::string> ListTables(const std::string& request_id,
                                        const std::string& connection_id);

    /**
     * @brief 获取数据库表数据, 返回表结构信息与分页数据;
     *        默认查询临时表数据(展示修改类 SQL 的执行效果), force_original 为 true 时强制查原表
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param table_name 表名
     * @param force_original 是否强制查询原表
     * @param page_number 页码(从 1 开始, 缺省为 1)
     * @param page_size 每页行数(缺省为 50)
     * @return 表结构信息与分页数据
     */
    database_proto::TableSchemaInfo GetTableData(const std::string& request_id,
                                                 const std::string& connection_id,
                                                 const std::string& table_name, bool force_original,
                                                 int32_t page_number, int32_t page_size);

    /**
     * @brief 执行 SQL 语句, 查询类 SQL 直接在原表上执行;
     *        修改类 SQL 在临时表上执行, 保护原始表数据(危险关键字语句被驱动层校验拦截)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param sql SQL 语句
     * @return SQL 执行结果(查询结果或影响行数)
     */
    QueryResult ExecuteSql(const std::string& request_id, const std::string& connection_id,
                           const std::string& sql);

    /**
     * @brief 获取表结构, 返回 JSON 格式的表结构描述(表名与列信息集合),
     *        直接调用驱动层的 GetTableStructure 接口
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param table_name 表名
     * @return JSON 格式的表结构描述字符串
     */
    std::string GetTableStructure(const std::string& request_id, const std::string& connection_id,
                                  const std::string& table_name);

    /**
     * @brief 获取指定表的采样数据, 返回 JSON 格式的采样数据(列名集合与行数据集合);
     *        存在临时表时采样临时表数据, 与前端预览逻辑保持一致
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param table_name 表名
     * @param limit 采样条数(小于等于 0 时默认采样 5 条)
     * @return JSON 格式的采样数据字符串
     */
    std::string GetSampleData(const std::string& request_id, const std::string& connection_id,
                              const std::string& table_name, int32_t limit);

    /**
     * @brief 导入 Excel 的 WorkSheet 数据到数据库 : 表已存在时先删除旧表再重建
     *        (重新导入 = 覆盖旧数据), 自动增加一列自增 id 作为主键建立索引,
     *        然后批量导入数据, 每批 100 条一个事务, 某批失败时返回已成功导入的行数
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID(通常是 excel_connection 全局连接)
     * @param table_name 数据库表名
     * @param worksheet_data Excel 解析出的 WorkSheet 数据
     * @return 成功导入的行数
     */
    int32_t ImportExcelData(const std::string& request_id, const std::string& connection_id,
                            const std::string& table_name,
                            const excel_parse_proto::WorksheetData& worksheet_data);

    /**
     * @brief 删除 Excel 文件表 : MySQL 数据库删除指定表及其关联临时表;
     *        SQLite 数据库删除本地数据库文件; 最后移除连接信息
     *        (excel_connection 全局连接受保护, 跳过移除)
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param table_names 要删除的表名列表
     * @return 删除结果(成功与失败的表名列表)
     */
    DropTableResult DropTableExcel(const std::string& request_id, const std::string& connection_id,
                                   const std::vector<std::string>& table_names);

    /**
     * @brief 获取 SQLite 数据库文件 : 通过文件子服务的 RPC 接口获取指定文件
     *        在 FastDFS 中的文件 ID, 并将该文件下载到本地 sqlitefile/{用户 ID}/ 目录
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param session_id 会话 ID
     * @param user_id 用户 ID
     * @param file_id 文件子服务中的文件 ID
     * @return 下载后的本地文件路径
     */
    std::string GetSQLiteFileId(const std::string& request_id, const std::string& session_id,
                                const std::string& user_id, const std::string& file_id) const;

    /**
     * @brief 获取连接下的临时表信息列表
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @return 临时表信息列表, 连接下没有临时表时返回空列表
     */
    std::vector<DatabaseTempTableInfo> GetConnTempTables(const std::string& request_id,
                                                         const std::string& connection_id);

    /**
     * @brief 删除用户对应的所有连接 : 逐连接删除临时表以及连接信息,
     *        单个连接删除失败不影响其余连接的删除
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param user_id 用户 ID
     */
    void DeleteUserAllConn(const std::string& request_id, const std::string& user_id);

private:
    /**
     * @brief 将 proto 数据库配置转换为驱动层配置对象,
     *        SQLite 配置内部会先通过文件子服务下载文件到本地
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param session_id 会话 ID
     * @param user_id 用户 ID
     * @param database_config proto 数据库配置
     * @return 驱动层数据库配置对象
     */
    std::shared_ptr<DatabaseConfig> ConvertDatabaseConfig(const std::string& request_id,
                                                          const std::string& session_id,
                                                          const std::string& user_id,
                                                          const database_proto::DatabaseConfig& database_config) const;

    /**
     * @brief 通过连接 ID 获取数据库驱动实例, 连接不存在时抛出异常
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @return 数据库驱动实例
     */
    std::shared_ptr<DatabaseDriver> GetDriverByConnectionId(const std::string& request_id,
                                                            const std::string& connection_id) const;

    /**
     * @brief 查询数据库中所有表名(包含临时表, 不过滤),
     *        直接调用驱动层的 GetAllTablesName 接口
     * @param driver 数据库驱动实例
     * @return 表名列表
     */
    std::vector<std::string> ListAllTables(const std::shared_ptr<DatabaseDriver>& driver) const;

    /**
     * @brief 判断数据库中是否存在指定表
     * @param driver 数据库驱动实例
     * @param table_name 表名
     * @return 表存在返回 true, 否则返回 false
     */
    bool IsTableExists(const std::shared_ptr<DatabaseDriver>& driver,
                       const std::string& table_name) const;

    /**
     * @brief 确定实际查询的表名 : 存在临时表且未强制查原表时返回临时表名
     * @param connection_id 数据库连接 ID
     * @param table_name 原表名
     * @param force_original 是否强制查询原表
     * @return 实际查询的表名
     */
    std::string ResolveQueryTableName(const std::string& connection_id,
                                      const std::string& table_name, bool force_original) const;

    /**
     * @brief 查询指定原表当前登记的临时表名, 每个原表对应一个临时表,
     *        取最新登记的一条, 不存在时返回空字符串
     * @param connection_id 数据库连接 ID
     * @param original_table_name 原表名
     * @return 临时表名, 不存在时返回空字符串
     */
    std::string GetTempTableName(const std::string& connection_id,
                                 const std::string& original_table_name) const;

    /**
     * @brief 在临时表上执行修改类 SQL : 仅对 SQL 中将要修改的目标表创建临时表
     *        并备份原表数据(只读引用的数据源表不备份, 已存在临时表时先删除旧临时表),
     *        替换 SQL 中的目标表名后在临时表上执行;
     *        执行失败时立即删除本次创建的所有临时表回滚
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param sql 原始修改类 SQL 语句
     * @return SQL 执行结果
     */
    QueryResult ExecuteModifySqlWithTempTable(const std::shared_ptr<DatabaseDriver>& driver,
                                              const std::string& request_id,
                                              const std::string& connection_id,
                                              const std::string& sql);

    /**
     * @brief 创建临时表并备份原表数据, 临时表名格式 : 原始表名_temp_毫秒时间戳;
     *        MySQL 使用 CREATE TABLE LIKE + INSERT SELECT, SQLite 使用 CREATE TABLE AS SELECT
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param original_table_name 原表名
     * @return 临时表名
     */
    std::string CreateTempTableAndBackup(const std::shared_ptr<DatabaseDriver>& driver,
                                         const std::string& request_id,
                                         const std::string& original_table_name) const;

    /**
     * @brief 生成临时表名 : 原始表名_temp_毫秒时间戳,
     *        原表名部分超长时截断, 保证临时表名不超过数据库标识符最大长度限制
     * @param original_table_name 原表名
     * @return 临时表名
     */
    static std::string GenerateTempTableName(const std::string& original_table_name);

    /**
     * @brief 将 SQL 中出现的修改目标表名整词替换为对应的临时表名,
     *        避免表名前缀相同的其他表被误替换(如 users 与 users_backup)
     * @param sql 原始 SQL 语句
     * @param table_name_map 修改目标表名到临时表名的替换映射
     * @return 替换后的 SQL 语句
     */
    static std::string ReplaceTableNames(const std::string& sql,
                                         const std::unordered_map<std::string, std::string>& table_name_map);

    /**
     * @brief 删除单张临时表并从管理集合移除对应记录, 删除失败仅记录日志
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param temp_table_info 临时表信息
     */
    void DropSingleTempTable(const std::shared_ptr<DatabaseDriver>& driver,
                             const std::string& request_id, const std::string& connection_id,
                             const DatabaseTempTableInfo& temp_table_info);

    /**
     * @brief 删除指定连接下与指定原表关联的所有临时表并清理管理集合记录
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     * @param original_table_name 原表名
     */
    void DropRelatedTempTables(const std::shared_ptr<DatabaseDriver>& driver,
                               const std::string& request_id, const std::string& connection_id,
                               const std::string& original_table_name);

    /**
     * @brief 删除指定连接下的所有临时表并清空管理集合记录
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param connection_id 数据库连接 ID
     */
    void DropAllTempTables(const std::shared_ptr<DatabaseDriver>& driver,
                           const std::string& request_id, const std::string& connection_id);

    /**
     * @brief 从管理集合移除指定临时表的登记记录
     * @param connection_id 数据库连接 ID
     * @param temp_table_name 临时表名
     */
    void RemoveTempTableRecord(const std::string& connection_id, const std::string& temp_table_name);

    /**
     * @brief 创建 Excel 数据表 : 自增 id 主键列 + 数据列,
     *        MySQL 表引擎为 InnoDB, 字符集为 utf8mb4, 排序规则为 utf8mb4_unicode_ci
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param table_name 数据库表名
     * @param worksheet_data Excel 解析出的 WorkSheet 数据(提供列名与列类型)
     */
    void CreateImportTable(const std::shared_ptr<DatabaseDriver>& driver, const std::string& request_id,
                           const std::string& table_name,
                           const excel_parse_proto::WorksheetData& worksheet_data) const;

    /**
     * @brief 批量导入 WorkSheet 行数据, 使用预编译语句逐行插入,
     *        每批 100 条一个事务, 某批失败时回滚当前批并返回已成功导入的行数
     * @param driver 数据库驱动实例
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param table_name 数据库表名
     * @param worksheet_data Excel 解析出的 WorkSheet 数据
     * @return 成功导入的行数
     */
    int32_t ImportWorksheetRows(const std::shared_ptr<DatabaseDriver>& driver,
                                const std::string& request_id, const std::string& table_name,
                                const excel_parse_proto::WorksheetData& worksheet_data) const;

    // 数据库连接管理器, 负责连接的登记与维护
    std::shared_ptr<DataBaseConnectionManager> connection_manager_;

    // RPC 信道管理对象, 用于调用文件子服务接口
    cpp_toolkit::ChannelManager::Ptr channel_manager_;

    // 保护临时表集合映射表
    mutable std::mutex mutex_;

    // 临时表集合 : 连接 ID -> 临时表信息列表
    std::unordered_map<std::string, std::vector<DatabaseTempTableInfo>> temp_tables_;
};

} // namespace database_service
} // namespace chat_excel
