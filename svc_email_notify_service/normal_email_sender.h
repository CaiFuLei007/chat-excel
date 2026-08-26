#pragma once

#include <string>
#include "email_sender.h"

namespace chat_excel
{
namespace notify_service
{

/**
 * @brief 普通邮件发送类, 继承 BaseEmailSender,
 *        负责实现普通邮件的 HTML 正文构建(主题标题 + 内容区域),
 *        用于 AI 汇总信息等 HTML 格式内容的通知邮件
 */
class NormalEmailSender : public BaseEmailSender
{
public:
    /**
     * @brief 构造函数, 注入邮箱配置信息
     * @param mail_settings 邮箱配置信息, 包含用户名、授权码、发送者邮件号与服务器地址
     */
    explicit NormalEmailSender(const MailSettings& mail_settings);

    /**
     * @brief 构建普通邮件的完整邮件内容 : 内部构建 HTML 格式正文
     *        (主题标题 + 内容区域, content 为 HTML 格式内容直接嵌入)后组装为 MIME 报文
     *        (报文头 + base64 编码正文), 返回的内容由 SendEmail 直接作为发送数据上传
     * @param to_email 接收方邮箱地址
     * @param subject 邮件主题
     * @param content 邮件内容(HTML 格式)
     * @return 完整的 MIME 邮件报文字符串
     */
    std::string BuildEmailBody(const std::string& to_email, const std::string& subject,
                               const std::string& content) override;
};

} // namespace notify_service
} // namespace chat_excel
