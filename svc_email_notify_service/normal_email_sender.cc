#include "normal_email_sender.h"

#include <string>

namespace chat_excel
{
namespace notify_service
{

NormalEmailSender::NormalEmailSender(const MailSettings& mail_settings)
    : BaseEmailSender(mail_settings)
{
}

std::string NormalEmailSender::BuildEmailBody(const std::string& to_email,
                                              const std::string& subject,
                                              const std::string& content)
{
    // 1. 构建普通邮件的 HTML 正文 : 主题标题 + 内容区域, content 为 HTML 格式内容直接嵌入
    std::string email_body;
    email_body.reserve(subject.size() + content.size() + 512);
    email_body += "<!DOCTYPE html>";
    email_body += "<html><body style=\"margin:0;padding:24px;background-color:#f4f6f8;"
            "font-family:'Helvetica Neue',Arial,sans-serif;\">";
    email_body += "<div style=\"max-width:560px;margin:0 auto;background-color:#ffffff;"
            "border-radius:8px;padding:32px;\">";
    email_body += "<h2 style=\"margin:0 0 24px 0;color:#333333;font-size:20px;"
            "border-bottom:1px solid #eeeeee;padding-bottom:16px;\">" + subject + "</h2>";
    email_body += "<div style=\"color:#444444;font-size:14px;line-height:1.6;\">" + content + "</div>";
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
