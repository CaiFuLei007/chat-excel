#include "verify_code_email_sender.h"

#include <string>

namespace chat_excel
{
namespace notify_service
{

VerifyCodeEmailSender::VerifyCodeEmailSender(const MailSettings& mail_settings)
    : BaseEmailSender(mail_settings)
{
}

std::string VerifyCodeEmailSender::BuildEmailBody(const std::string& to_email,
                                                  const std::string& subject,
                                                  const std::string& content)
{
    // 1. 构建验证码邮件的 HTML 正文 : 主题标题 + 大字号验证码展示 + 有效期提示
    std::string email_body;
    email_body.reserve(1024);
    email_body += "<!DOCTYPE html>";
    email_body += "<html><body style=\"margin:0;padding:24px;background-color:#f4f6f8;"
            "font-family:'Helvetica Neue',Arial,sans-serif;\">";
    email_body += "<div style=\"max-width:480px;margin:0 auto;background-color:#ffffff;"
            "border-radius:8px;padding:32px;\">";
    email_body += "<h2 style=\"margin:0 0 24px 0;color:#333333;font-size:20px;text-align:center;\">"
            + subject + "</h2>";
    email_body += "<p style=\"margin:0 0 24px 0;color:#666666;font-size:14px;\">您好 , 您的验证码为 :</p>";
    email_body += "<div style=\"margin:0 0 24px 0;padding:16px;background-color:#eef2ff;"
            "border-radius:8px;text-align:center;\">";
    email_body += "<span style=\"font-size:32px;font-weight:bold;color:#1a56db;"
            "letter-spacing:8px;\">" + content + "</span>";
    email_body += "</div>";
    email_body += "<p style=\"margin:0;color:#999999;font-size:12px;text-align:center;\">"
            "验证码 10 分钟内有效 , 请勿泄露给他人</p>";
    email_body += "</div></body></html>";

    // 2. 组装完整的 MIME 邮件报文 : 报文头(From/To/Subject/MIME) + base64 编码正文,
    //    报文头与报文体之间以空行分隔, 每行以 CRLF 结尾(RFC 5322 要求)
    std::string encoded_subject = EncodeSubjectRfc2047(subject);
    std::string encoded_body = Base64Encode(email_body);
    std::string mime_message;
    mime_message.reserve(encoded_body.size() + encoded_body.size() / kBase64LineLength + 256);
    mime_message += "From: <" + mail_settings_.from_email + ">\r\n";
    mime_message += "To: <" + to_email + ">\r\n";
    mime_message += "Subject: " + encoded_subject + "\r\n";
    mime_message += "MIME-Version: 1.0\r\n";
    mime_message += "Content-Type: text/html; charset=UTF-8\r\n";
    mime_message += "Content-Transfer-Encoding: base64\r\n";
    mime_message += "\r\n";

    // base64 正文按 76 字符换行, 符合 MIME 传输规范
    for (size_t i = 0; i < encoded_body.size(); i += kBase64LineLength)
    {
        size_t line_length = (encoded_body.size() - i < kBase64LineLength)
                                 ? encoded_body.size() - i
                                 : kBase64LineLength;
        mime_message.append(encoded_body, i, line_length);
        mime_message += "\r\n";
    }
    return mime_message;
}

} // namespace notify_service
} // namespace chat_excel
