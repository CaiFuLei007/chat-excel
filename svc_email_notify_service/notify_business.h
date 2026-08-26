#pragma once

#include <memory>
#include <string>
#include "email_sender.h"
#include "email_workers.h"
#include "normal_email_sender.h"
#include "verify_code_email_sender.h"

namespace chat_excel
{
namespace notify_service
{

/**
 * @brief 通知子服务业务逻辑类, 负责组织邮件异步发送业务 :
 *        构造时初始化 curl 全局资源并构建验证码/普通邮件两个发送器,
 *        发送接口将邮件任务封装后投递到工作线程池的任务队列中异步执行,
 *        避免邮件发送耗时阻塞 RPC 主线程
 */
class NotifyBusiness
{
public:
    /**
     * @brief 构造函数, 初始化 curl 全局资源, 构建验证码与普通邮件两个邮箱发送器
     * @param mail_settings 邮箱配置信息, 包含用户名、授权码、发送者邮件号与服务器地址
     */
    explicit NotifyBusiness(const MailSettings& mail_settings);

    /**
     * @brief 析构函数, 停止邮箱发送工作线程并释放 curl 全局资源
     */
    ~NotifyBusiness();

    // 业务对象持有发送器与线程池资源, 禁止拷贝与赋值
    NotifyBusiness(const NotifyBusiness&) = delete;
    NotifyBusiness& operator=(const NotifyBusiness&) = delete;

    /**
     * @brief 设置邮箱发送工作线程个数, 须在 StartEmailWorkers 之前调用
     * @param thread_count 工作线程个数
     */
    void SetWorkerThreadCount(int thread_count);

    /**
     * @brief 启动邮箱发送工作线程
     */
    void StartEmailWorkers();

    /**
     * @brief 停止邮箱发送工作线程
     */
    void StopEmailWorkers();

    /**
     * @brief 发送验证码邮件, 将验证码任务放入工作线程的任务队列中异步发送
     * @param email 接收方邮箱地址
     * @param code 验证码
     */
    void SendVerifyCode(const std::string& email, const std::string& code);

    /**
     * @brief 发送普通邮件, 将邮件任务放入工作线程的任务队列中异步发送
     * @param to_email 接收方邮箱地址
     * @param subject 邮件主题
     * @param content 邮件内容(HTML 格式)
     */
    void SendEmail(const std::string& to_email, const std::string& subject,
                   const std::string& content);

private:
    // 验证码邮件发送器
    std::shared_ptr<VerifyCodeEmailSender> verify_code_email_sender_;

    // 普通邮件发送器
    std::shared_ptr<NormalEmailSender> normal_email_sender_;

    // 邮箱发送工作线程池
    EmailWorkers email_workers_;
};

} // namespace notify_service
} // namespace chat_excel
