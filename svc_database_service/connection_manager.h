#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpp-toolkit/timerwheel.h>
#include <jsoncpp/json/json.h>

#include "svc_database_service/driver/database_driver.h"
#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{

// Excel 数据库全局连接 ID
constexpr char kExcelConnectionId[] = "excel_connection";

// 连接过期时间 : 空闲 1 小时未使用自动断开(毫秒)
constexpr int64_t kConnectionIdleTimeout = 3600 * 1000;

// 过期连接清理周期(秒)
constexpr int kCleanIntervalSeconds = 60;

// 时间轮任务 ID 无效值
constexpr int kInvalidTimerTaskId = -1;

/**
 * @brief 数据库连接信息类, 封装一条数据库连接的全部状态 :
 *        连接 ID, 数据库连接实例, 归属用户以及创建/活跃时间,
 *        由 DataBaseConnectionManager 统一登记与维护
 */
class ConnectionInfo
{
public:
    /**
     * @brief 构造连接信息, 使用 uuid 生成器自动生成连接 ID,
     *        并记录创建时间与最近活跃时间
     * @param driver 已建立连接的数据库驱动实例
     * @param user_id 连接归属的用户 ID
     */
    ConnectionInfo(std::shared_ptr<DatabaseDriver> driver, std::string user_id);

    /**
     * @brief 构造连接信息, 使用指定连接 ID(excel_connection 全局连接使用),
     *        并记录创建时间与最近活跃时间
     * @param connection_id 指定的连接 ID
     * @param driver 已建立连接的数据库驱动实例
     * @param user_id 连接归属的用户 ID
     */
    ConnectionInfo(std::string connection_id, std::shared_ptr<DatabaseDriver> driver, std::string user_id);

    /**
     * @brief 将连接信息序列化为 JSON 对象,
     *        包含 connection_id/user_id/create_time/last_active_time 四个字段
     * @return 表示连接信息的 Json::Value 对象
     */
    Json::Value ToJson() const;

    /**
     * @brief 获取连接 ID
     * @return 连接 ID
     */
    const std::string& GetConnectionId() const;

    /**
     * @brief 获取数据库连接实例
     * @return 数据库驱动实例的 const 引用
     */
    const std::shared_ptr<DatabaseDriver>& GetDriver() const;

    /**
     * @brief 获取连接归属的用户 ID
     * @return 用户 ID
     */
    const std::string& GetUserId() const;

    /**
     * @brief 获取连接创建时间
     * @return 创建时间戳(毫秒)
     */
    int64_t GetCreateTime() const;

    /**
     * @brief 获取最近一次活跃时间
     * @return 最近活跃时间戳(毫秒)
     */
    int64_t GetLastActiveTime() const;

    /**
     * @brief 刷新最近活跃时间为当前时间
     */
    void UpdateActiveTime();

private:
    // 连接 ID(uuid 生成)
    std::string connection_id_;

    // 数据库连接实例
    std::shared_ptr<DatabaseDriver> driver_;

    // 连接归属的用户 ID
    std::string user_id_;

    // 创建时间戳(毫秒)
    int64_t create_time_ = 0;

    // 最近一次活跃时间戳(毫秒)
    int64_t last_active_time_ = 0;
};

/**
 * @brief 数据库连接管理器单例类, 负责登记, 查询与清理所有的数据库连接 :
 *        维护连接 ID 到连接信息, 用户 ID 到连接 ID 列表两张映射表,
 *        内部自动创建 excel_connection 全局连接(不参与过期清理),
 *        并通过时间轮定时器周期清理空闲超过 1 小时的过期连接
 */
class DataBaseConnectionManager
{
public:
    ~DataBaseConnectionManager() = default;

    DataBaseConnectionManager(const DataBaseConnectionManager&) = delete;

    DataBaseConnectionManager& operator=(const DataBaseConnectionManager&) = delete;

    /**
     * @brief 获取连接管理器单例, 首次调用时使用传入的 MySQL 配置自动创建
     *        excel_connection 全局连接, 并启动过期连接周期清理任务
     * @param config excel_connection 全局连接使用的 MySQL 配置(仅首次调用生效)
     * @return 连接管理器单例引用
     * @throws ChatExcelException 配置无效或 excel_connection 创建失败时抛出
     */
    static DataBaseConnectionManager& GetInstance(std::shared_ptr<MySQLConfig> config);

    /**
     * @brief 登记一条业务层已建立好的数据库连接, 自动生成连接 ID 并返回
     * @param driver 已建立连接的数据库驱动实例
     * @param user_id 连接归属的用户 ID
     * @return 生成的连接 ID
     */
    std::string CreateConnection(std::shared_ptr<DatabaseDriver> driver, std::string user_id);

    /**
     * @brief 通过连接 ID 获取连接信息, 获取成功自动刷新连接的最近活跃时间
     * @param connection_id 连接 ID
     * @return 连接信息对象
     * @throws ChatExcelException 连接不存在时抛出(DB_CONNECTION_NOT_FOUND)
     */
    std::shared_ptr<ConnectionInfo> GetConnection(const std::string& connection_id);

    /**
     * @brief 通过连接 ID 移除连接信息, 并断开对应的底层连接;
     *        excel_connection 全局连接受保护, 禁止移除
     * @param connection_id 连接 ID
     * @throws ChatExcelException 连接不存在时抛出(DB_CONNECTION_NOT_FOUND),
     *         移除 excel_connection 时抛出(DB_CONNECTION_PROTECTED)
     */
    void RemoveConnection(const std::string& connection_id);

    /**
     * @brief 通过用户 ID 删除该用户的所有连接信息, 并断开对应的底层连接
     * @param user_id 用户 ID
     */
    void RemoveUserConnections(const std::string& user_id);

private:
    /**
     * @brief 私有构造函数, 创建 excel_connection 全局连接并启动过期清理任务
     * @param config excel_connection 全局连接使用的 MySQL 配置
     * @throws ChatExcelException 配置无效或 excel_connection 创建失败时抛出
     */
    explicit DataBaseConnectionManager(std::shared_ptr<MySQLConfig> config);

    /**
     * @brief 创建 excel_connection 全局连接 : 通过驱动工厂创建 MySQL 驱动,
     *        建立连接后以固定连接 ID 登记(仅构造期调用, 无需加锁)
     * @param config MySQL 数据库配置
     * @throws ChatExcelException 配置无效或连接失败时抛出
     */
    void InitExcelConnection(std::shared_ptr<MySQLConfig> config);

    /**
     * @brief 向时间轮注册过期连接清理任务, 任务执行完毕后重新注册自身,
     *        实现 60 秒周期扫描(注册前移除旧任务, 保证轮内只有一个清理任务)
     */
    void ScheduleCleanTask();

    /**
     * @brief 清理空闲超过 1 小时的过期连接 : 断开底层连接并同步清理两张映射表,
     *        excel_connection 全局连接不参与清理
     */
    void CleanExpiredConnections();

    // 保护连接信息映射表与用户连接映射表
    std::mutex mutex_;

    // 连接信息映射表 : 连接 ID -> 连接信息
    std::unordered_map<std::string, std::shared_ptr<ConnectionInfo>> connections_;

    // 用户连接映射表 : 用户 ID -> 连接 ID 列表
    std::unordered_map<std::string, std::vector<std::string>> user_connections_;

    // 时间轮定时器, 驱动过期连接的周期清理
    cpp_toolkit::MultiLayerTimerWheel timer_wheel_;

    // 保护清理任务的注册与移除
    std::mutex timer_mutex_;

    // 当前清理任务的 ID
    int clean_task_id_ = kInvalidTimerTaskId;
};

} // namespace chat_excel
