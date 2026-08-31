#include "svc_database_service/connection_manager.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>

#include "common/exception.h"

namespace chat_excel
{

namespace
{

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

} // namespace

ConnectionInfo::ConnectionInfo(std::shared_ptr<DatabaseDriver> driver, std::string user_id)
    : ConnectionInfo(cpp_toolkit::UuidUtil::GenerateUuidV4(), std::move(driver), std::move(user_id))
{
}

ConnectionInfo::ConnectionInfo(std::string connection_id, std::shared_ptr<DatabaseDriver> driver,
                               std::string user_id)
    : connection_id_(std::move(connection_id)),
      driver_(std::move(driver)),
      user_id_(std::move(user_id)),
      create_time_(NowMilliseconds()),
      last_active_time_(create_time_)
{
}

Json::Value ConnectionInfo::ToJson() const
{
    Json::Value json;
    json["connection_id"] = connection_id_;
    json["user_id"] = user_id_;
    json["create_time"] = static_cast<Json::Int64>(create_time_);
    json["last_active_time"] = static_cast<Json::Int64>(last_active_time_);
    return json;
}

const std::string& ConnectionInfo::GetConnectionId() const
{
    return connection_id_;
}

const std::shared_ptr<DatabaseDriver>& ConnectionInfo::GetDriver() const
{
    return driver_;
}

const std::string& ConnectionInfo::GetUserId() const
{
    return user_id_;
}

int64_t ConnectionInfo::GetCreateTime() const
{
    return create_time_;
}

int64_t ConnectionInfo::GetLastActiveTime() const
{
    return last_active_time_;
}

void ConnectionInfo::UpdateActiveTime()
{
    last_active_time_ = NowMilliseconds();
}

DataBaseConnectionManager& DataBaseConnectionManager::GetInstance(std::shared_ptr<MySQLConfig> config)
{
    // C++11 起静态局部变量的初始化线程安全, 保证单例只构造一次
    static DataBaseConnectionManager instance(std::move(config));
    return instance;
}

DataBaseConnectionManager::DataBaseConnectionManager(std::shared_ptr<MySQLConfig> config)
{
    InitExcelConnection(std::move(config));

    // 启动时间轮并注册周期清理任务
    timer_wheel_.Start();
    ScheduleCleanTask();
    INFO("数据库连接管理器初始化完成");
}

void DataBaseConnectionManager::InitExcelConnection(std::shared_ptr<MySQLConfig> config)
{
    if (config == nullptr || !config->CheckConfig())
    {
        ERR("excel_connection 全局连接配置无效");
        throw ChatExcelException(ErrorCode::DB_CONFIG_INVALID);
    }

    // 通过驱动工厂创建 MySQL 驱动并建立连接, 失败时异常向上传播
    std::shared_ptr<DatabaseDriver> driver = DatabaseDriverFactory::CreateDriver(config);
    driver->Connect();

    // 以固定连接 ID 登记全局连接, 用户 ID 为空表示不归属任何用户
    auto connection_info = std::make_shared<ConnectionInfo>(kExcelConnectionId, driver, "");
    connections_.emplace(kExcelConnectionId, connection_info);
    INFO("excel_connection 全局连接创建成功, 数据库: {}", config->database_name);
}

std::string DataBaseConnectionManager::CreateConnection(std::shared_ptr<DatabaseDriver> driver,
                                                        std::string user_id)
{
    auto connection_info = std::make_shared<ConnectionInfo>(driver, std::move(user_id));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.emplace(connection_info->GetConnectionId(), connection_info);
        user_connections_[connection_info->GetUserId()].push_back(connection_info->GetConnectionId());
    }
    INFO("数据库连接创建成功, 连接 ID: {} , 用户 ID: {}", connection_info->GetConnectionId(),
         connection_info->GetUserId());
    return connection_info->GetConnectionId();
}

std::shared_ptr<ConnectionInfo> DataBaseConnectionManager::GetConnection(const std::string& connection_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = connections_.find(connection_id);
    if (iter == connections_.end())
    {
        WARN("获取数据库连接失败, 连接不存在, 连接 ID: {}", connection_id);
        throw ChatExcelException(ErrorCode::DB_CONNECTION_NOT_FOUND);
    }

    // 每次成功获取连接视为一次活跃, 刷新最近活跃时间
    iter->second->UpdateActiveTime();
    DBG("获取数据库连接成功, 连接 ID: {}", connection_id);
    return iter->second;
}

void DataBaseConnectionManager::RemoveConnection(const std::string& connection_id)
{
    if (connection_id == kExcelConnectionId)
    {
        WARN("禁止删除 excel_connection 全局连接");
        throw ChatExcelException(ErrorCode::DB_CONNECTION_PROTECTED);
    }

    std::shared_ptr<DatabaseDriver> driver;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = connections_.find(connection_id);
        if (iter == connections_.end())
        {
            WARN("移除数据库连接失败, 连接不存在, 连接 ID: {}", connection_id);
            throw ChatExcelException(ErrorCode::DB_CONNECTION_NOT_FOUND);
        }

        driver = iter->second->GetDriver();

        // 从用户连接映射表中移除该连接, 用户没有连接时删除映射项
        std::vector<std::string>& connection_ids = user_connections_[iter->second->GetUserId()];
        connection_ids.erase(std::remove(connection_ids.begin(), connection_ids.end(), connection_id),
                             connection_ids.end());
        if (connection_ids.empty())
        {
            user_connections_.erase(iter->second->GetUserId());
        }
        connections_.erase(iter);
    }

    // 锁外断开底层连接, 断开失败不影响连接移除
    try
    {
        driver->Disconnect();
    }
    catch (const ChatExcelException& e)
    {
        ERR("断开数据库连接失败, 连接 ID: {} , 错误: {}", connection_id, e.what());
    }
    INFO("数据库连接已移除, 连接 ID: {}", connection_id);
}

void DataBaseConnectionManager::RemoveUserConnections(const std::string& user_id)
{
    std::vector<std::shared_ptr<DatabaseDriver>> drivers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = user_connections_.find(user_id);
        if (iter == user_connections_.end())
        {
            INFO("用户没有数据库连接, 无需删除, 用户 ID: {}", user_id);
            return;
        }

        for (const std::string& connection_id : iter->second)
        {
            // excel_connection 全局连接不归属任何用户, 防御性跳过
            if (connection_id == kExcelConnectionId)
            {
                continue;
            }
            auto connection_iter = connections_.find(connection_id);
            if (connection_iter != connections_.end())
            {
                drivers.push_back(connection_iter->second->GetDriver());
                connections_.erase(connection_iter);
            }
        }
        user_connections_.erase(iter);
    }

    // 锁外逐个断开底层连接, 断开失败不影响其余连接删除
    for (const std::shared_ptr<DatabaseDriver>& driver : drivers)
    {
        try
        {
            driver->Disconnect();
        }
        catch (const ChatExcelException& e)
        {
            ERR("断开数据库连接失败, 错误: {}", e.what());
        }
    }
    INFO("用户的所有数据库连接已删除, 用户 ID: {} , 数量: {}", user_id, drivers.size());
}

void DataBaseConnectionManager::ScheduleCleanTask()
{
    std::lock_guard<std::mutex> lock(timer_mutex_);

    // 注册前移除旧任务, 兼容时间轮任务单次/周期两种语义, 保证轮内始终只有一个清理任务
    if (clean_task_id_ != kInvalidTimerTaskId)
    {
        timer_wheel_.RemoveTask(clean_task_id_);
    }
    clean_task_id_ = timer_wheel_.AddTask(
        [this]()
        {
            // 执行清理后重新注册自身, 实现周期扫描
            CleanExpiredConnections();
            ScheduleCleanTask();
        }, 0, 0, 0, kCleanIntervalSeconds);
}

void DataBaseConnectionManager::CleanExpiredConnections()
{
    std::vector<std::shared_ptr<DatabaseDriver>> expired_drivers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = NowMilliseconds();
        for (auto iter = connections_.begin(); iter != connections_.end();)
        {
            // excel_connection 全局连接不参与过期清理
            if (iter->first == kExcelConnectionId)
            {
                ++iter;
                continue;
            }

            // 连接空闲超过 1 小时未使用, 判定为过期连接
            if (now - iter->second->GetLastActiveTime() >= kConnectionIdleTimeout)
            {
                INFO("数据库连接空闲超时, 自动断开, 连接 ID: {} , 用户 ID: {}", iter->first,
                     iter->second->GetUserId());
                expired_drivers.push_back(iter->second->GetDriver());

                // 同步清理用户连接映射表
                auto user_iter = user_connections_.find(iter->second->GetUserId());
                if (user_iter != user_connections_.end())
                {
                    std::vector<std::string>& connection_ids = user_iter->second;
                    connection_ids.erase(std::remove(connection_ids.begin(), connection_ids.end(), iter->first),
                                         connection_ids.end());
                    if (connection_ids.empty())
                    {
                        user_connections_.erase(user_iter);
                    }
                }
                iter = connections_.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }

    // 锁外逐个断开过期的底层连接
    for (const std::shared_ptr<DatabaseDriver>& driver : expired_drivers)
    {
        try
        {
            driver->Disconnect();
        }
        catch (const ChatExcelException& e)
        {
            ERR("断开过期数据库连接失败, 错误: {}", e.what());
        }
    }
}

} // namespace chat_excel
