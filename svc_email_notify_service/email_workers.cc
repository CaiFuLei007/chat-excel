#include "email_workers.h"

#include <queue>
#include <string>
#include <utility>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace notify_service
{

EmailWorkers::~EmailWorkers()
{
    // 对象销毁时自动停止工作线程, 回收线程资源
    Stop();
}

void EmailWorkers::SetWorkerThreadCount(int thread_count)
{
    // 启动后线程个数不可调整, 忽略设置请求
    if (is_running_.load())
    {
        WARN("邮件工作线程已启动, 忽略线程个数设置请求, 期望个数: {}", thread_count);
        return;
    }

    thread_count_ = thread_count;
    INFO("邮件工作线程个数设置完成, 个数: {}", thread_count_);
}

void EmailWorkers::Start()
{
    // 幂等保护, 已启动则直接返回, 不重复启动
    if (is_running_.load())
    {
        WARN("邮件工作线程已在运行中, 忽略重复启动请求");
        return;
    }

    // 重置停止标志, 允许 Stop 之后重新启动
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_stopped_ = false;
    }

    is_running_.store(true);

    // 创建指定个数的工作线程, 进入任务消费循环
    for (int i = 0; i < thread_count_; ++i)
    {
        worker_threads_.emplace_back(&EmailWorkers::WorkerLoop, this);
    }
    INFO("邮件工作线程启动完成, 线程个数: {}", thread_count_);
}

void EmailWorkers::Stop()
{
    // 幂等保护, 未运行则直接返回
    if (!is_running_.load())
    {
        return;
    }

    // 置位停止标志并唤醒全部工作线程, 工作线程退出消费循环
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_stopped_ = true;
    }
    condition_variable_.notify_all();

    // join 等待全部工作线程退出
    for (std::thread& worker_thread : worker_threads_)
    {
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }
    }
    worker_threads_.clear();
    is_running_.store(false);

    // 丢弃队列中未处理的任务并记录日志, 由调用方通过重试机制补救
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!task_queue_.empty())
        {
            WARN("邮件工作线程停止, 丢弃未发送的邮件任务个数: {}", task_queue_.size());
            std::queue<EmailTask> empty_queue;
            task_queue_.swap(empty_queue);
        }
    }
    INFO("邮件工作线程已全部停止");
}

void EmailWorkers::AddTask(const EmailTask& task)
{
    // 任务入队并唤醒一个等待中的工作线程
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task_queue_.push(task);
    }
    condition_variable_.notify_one();
}

void EmailWorkers::WorkerLoop()
{
    while (true)
    {
        EmailTask task;
        {
            // 队列为空时阻塞等待, 收到新任务或停止通知时被唤醒
            std::unique_lock<std::mutex> lock(mutex_);
            condition_variable_.wait(lock, [this]()
                                     { return is_stopped_ || !task_queue_.empty(); });

            // 收到停止通知, 退出消费循环
            if (is_stopped_)
            {
                return;
            }

            // 取出队首任务
            task = task_queue_.front();
            task_queue_.pop();
        }

        // 锁外执行邮件发送, 避免发送耗时阻塞其他任务的入队与取出;
        // 工作线程内不能让异常外泄, 发送失败仅记录日志
        try
        {
            task.sender->SendEmail(task.to_email, task.subject, task.content);
        }
        catch (const ChatExcelException& e)
        {
            ERR("邮件任务发送异常, 收件人: {} , 主题: {} , 错误信息: {}",
                task.to_email, task.subject, e.what());
        }
        catch (const std::exception& e)
        {
            ERR("邮件任务非预期异常, 收件人: {} , 主题: {} , 错误信息: {}",
                task.to_email, task.subject, e.what());
        }
    }
}

} // namespace notify_service
} // namespace chat_excel
