#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include "email_sender.h"

namespace chat_excel
{
namespace notify_service
{

/**
 * @brief 邮件任务结构体, 封装一次异步邮件发送所需的全部数据,
 *        由业务层构造并投递到任务队列, 由工作线程取出执行发送
 */
struct EmailTask
{
    // 接收方邮箱地址
    std::string to_email;

    // 邮件标题
    std::string subject;

    // 邮件正文内容(HTML 格式)
    std::string content;

    // 邮件发送器智能指针, 指向具体的邮箱发送类(验证码/普通邮件发送器)
    std::shared_ptr<BaseEmailSender> sender;
};

/**
 * @brief 邮件发送工作线程池类, 生产者-消费者模式异步发送邮件 :
 *        业务线程调用 AddTask 将邮件任务入队(生产者),
 *        工作线程循环从任务队列中取出任务并调用发送器执行发送(消费者),
 *        避免邮件发送耗时阻塞 RPC 主线程; 内部通过互斥锁与条件变量保证线程安全
 */
class EmailWorkers
{
public:
    // 简单构造函数, 线程个数默认值由 SetWorkerThreadCount 设置
    EmailWorkers() = default;

    // 析构函数, 若工作线程仍在运行则先停止并回收线程资源
    ~EmailWorkers();

    // 线程池持有线程资源, 禁止拷贝与赋值
    EmailWorkers(const EmailWorkers&) = delete;
    EmailWorkers& operator=(const EmailWorkers&) = delete;

    /**
     * @brief 设置工作线程个数, 须在 Start 之前调用, 启动后设置无效
     * @param thread_count 工作线程个数
     */
    void SetWorkerThreadCount(int thread_count);

    /**
     * @brief 启动工作线程, 创建指定个数的工作线程并进入任务消费循环;
     *        已启动时重复调用无效
     */
    void Start();

    /**
     * @brief 停止工作线程, 通知全部工作线程退出并 join 等待线程结束,
     *        队列中未处理的任务会被丢弃并记录日志; 幂等接口, 未启动时直接返回
     */
    void Stop();

    /**
     * @brief 添加邮件任务到任务队列, 并唤醒一个等待中的工作线程
     * @param task 邮件任务对象
     */
    void AddTask(const EmailTask& task);

private:
    /**
     * @brief 工作线程主函数, 循环从任务队列中取出任务并执行邮件发送,
     *        队列为空时阻塞等待, 收到停止通知且队列清空后退出循环
     */
    void WorkerLoop();

    // 互斥锁, 保护任务队列与停止标志的并发访问
    std::mutex mutex_;

    // 条件变量, 队列非空或收到停止通知时唤醒工作线程
    std::condition_variable condition_variable_;

    // 任务队列, 存放待发送的邮件任务
    std::queue<EmailTask> task_queue_;

    // 工作线程列表
    std::vector<std::thread> worker_threads_;

    // 工作线程个数
    int thread_count_ = 0;

    // 工作线程运行状态标志
    std::atomic<bool> is_running_{false};

    // 停止标志, Stop 时置位并通知全部工作线程退出
    bool is_stopped_ = false;
};

} // namespace notify_service
} // namespace chat_excel
