#include "notify_business.h"

#include <memory>
#include <string>
#include <curl/curl.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace notify_service
{

namespace
{

// 验证码邮件主题
constexpr const char* kVerifyCodeEmailSubject = "ChatExcel 邮箱验证码";

} // namespace

NotifyBusiness::NotifyBusiness(const MailSettings& mail_settings)
{
    // 初始化 curl 全局资源, 须在任何线程使用 libcurl 之前调用一次(非线程安全, 主流程构造期调用)
    CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK)
    {
        ERR("curl 全局资源初始化失败, 错误码: {} , 错误描述: {}",
            static_cast<int>(result), curl_easy_strerror(result));
        throw ChatExcelException(ErrorCode::NOTIFY_SERVICE_INTERNAL_ERROR);
    }
    INFO("curl 全局资源初始化完成");

    // 构建验证码与普通邮件两个邮箱发送器
    verify_code_email_sender_ = std::make_shared<VerifyCodeEmailSender>(mail_settings);
    normal_email_sender_ = std::make_shared<NormalEmailSender>(mail_settings);
    INFO("邮箱发送器构建完成, 验证码发送器与普通邮件发送器就绪");
}

NotifyBusiness::~NotifyBusiness()
{
    // 先停止工作线程再释放 curl 全局资源, 保证工作线程不访问已释放的全局资源
    StopEmailWorkers();
    curl_global_cleanup();
    INFO("curl 全局资源已释放");
}

void NotifyBusiness::SetWorkerThreadCount(int thread_count)
{
    email_workers_.SetWorkerThreadCount(thread_count);
}

void NotifyBusiness::StartEmailWorkers()
{
    email_workers_.Start();
}

void NotifyBusiness::StopEmailWorkers()
{
    email_workers_.Stop();
}

void NotifyBusiness::SendVerifyCode(const std::string& email, const std::string& code)
{
    // 参数校验, 分别定位具体为空的参数并记录日志
    if (email.empty())
    {
        ERR("发送验证码参数错误, email 为空");
        throw ChatExcelException(ErrorCode::NOTIFY_VERIFYCODE_EMAIL_EMPTY);
    }
    else if (code.empty())
    {
        ERR("发送验证码参数错误, code 为空");
        throw ChatExcelException(ErrorCode::NOTIFY_VERIFYCODE_CODE_EMPTY);
    }

    // 封装验证码邮件任务, 交给验证码发送器在工作线程中构建正文并发送
    EmailTask task;
    task.to_email = email;
    task.subject = kVerifyCodeEmailSubject;
    task.content = code;
    task.sender = verify_code_email_sender_;
    email_workers_.AddTask(task);
    INFO("验证码邮件任务已入队, 收件人: {}", email);
}

void NotifyBusiness::SendEmail(const std::string& to_email, const std::string& subject,
                               const std::string& content)
{
    // 参数校验, 分别定位具体为空的参数并记录日志
    if (to_email.empty())
    {
        ERR("发送邮件参数错误, to_email 为空");
        throw ChatExcelException(ErrorCode::NOTIFY_EMAIL_TO_EMPTY);
    }
    else if (subject.empty())
    {
        ERR("发送邮件参数错误, subject 为空");
        throw ChatExcelException(ErrorCode::NOTIFY_EMAIL_SUBJECT_EMPTY);
    }
    else if (content.empty())
    {
        ERR("发送邮件参数错误, content 为空");
        throw ChatExcelException(ErrorCode::NOTIFY_EMAIL_CONTENT_EMPTY);
    }

    // 封装普通邮件任务, 交给普通邮件发送器在工作线程中构建正文并发送
    EmailTask task;
    task.to_email = to_email;
    task.subject = subject;
    task.content = content;
    task.sender = normal_email_sender_;
    email_workers_.AddTask(task);
    INFO("普通邮件任务已入队, 收件人: {} , 主题: {}", to_email, subject);
}

} // namespace notify_service
} // namespace chat_excel
