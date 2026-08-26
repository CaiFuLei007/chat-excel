#include "email_sender.h"

#include <algorithm>
#include <cstdint>
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

// SMTPS 协议 URL 前缀(隐式 TLS, 端口一般为 465)
constexpr const char* kSmtpsUrlPrefix = "smtps://";

// base64 编码字符表
constexpr const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// MIME 报文中上传数据的读取进度载体, 供读回调按偏移分块供给报文
struct PayloadSource
{
    // 待上传的完整 MIME 报文
    const std::string* data;

    // 当前已供给的字节偏移
    size_t offset;
};

/**
 * @brief libcurl 读回调, 从 PayloadSource 中按偏移分块拷贝报文数据到上传缓冲区
 * @param buffer libcurl 提供的上传缓冲区
 * @param size 单个元素字节数
 * @param nmemb 元素个数, 实际可写容量为 size * nmemb 字节
 * @param userdata 读回调用户数据, 指向 PayloadSource 对象
 * @return 本次实际拷贝的字节数, 数据耗尽时返回 0 表示上传结束
 */
size_t PayloadSourceReadCallback(char* buffer, size_t size, size_t nmemb, void* userdata)
{
    PayloadSource* payload = static_cast<PayloadSource*>(userdata);

    // 本次可供给的最大容量
    size_t max_bytes = size * nmemb;

    // 剩余待供给的字节数
    size_t remain_bytes = payload->data->size() - payload->offset;
    if (remain_bytes == 0)
    {
        return 0;
    }

    // 实际供给字节数取剩余量与容量的较小值, 并推进读取偏移
    size_t supply_bytes = (remain_bytes < max_bytes) ? remain_bytes : max_bytes;
    std::copy(payload->data->begin() + payload->offset,
              payload->data->begin() + payload->offset + supply_bytes, buffer);
    payload->offset += supply_bytes;
    return supply_bytes;
}

/**
 * @brief 判断字符串是否为纯 ASCII 字符串
 * @param text 待判断的字符串
 * @return 全部为 ASCII 字符返回 true, 包含非 ASCII 字符返回 false
 */
bool IsAllAscii(const std::string& text)
{
    for (char ch : text)
    {
        if (static_cast<unsigned char>(ch) > 127)
        {
            return false;
        }
    }
    return true;
}

} // namespace

BaseEmailSender::BaseEmailSender(const MailSettings& mail_settings)
    : mail_settings_(mail_settings)
{
}

std::string BaseEmailSender::Base64Encode(const std::string& data)
{
    std::string encoded;
    encoded.reserve((data.size() + 2) / 3 * 4);

    // 3 字节一组转换为 4 个 base64 字符, 不足 3 字节按填充规则补 '='
    size_t i = 0;
    while (i + 3 <= data.size())
    {
        uint32_t chunk = (static_cast<unsigned char>(data[i]) << 16)
                       | (static_cast<unsigned char>(data[i + 1]) << 8)
                       | static_cast<unsigned char>(data[i + 2]);
        encoded += kBase64Chars[(chunk >> 18) & 0x3F];
        encoded += kBase64Chars[(chunk >> 12) & 0x3F];
        encoded += kBase64Chars[(chunk >> 6) & 0x3F];
        encoded += kBase64Chars[chunk & 0x3F];
        i += 3;
    }

    // 处理剩余的 1 或 2 个字节, 不足部分以 '=' 填充
    size_t remain = data.size() - i;
    if (remain == 1)
    {
        uint32_t chunk = static_cast<unsigned char>(data[i]) << 16;
        encoded += kBase64Chars[(chunk >> 18) & 0x3F];
        encoded += kBase64Chars[(chunk >> 12) & 0x3F];
        encoded += "==";
    }
    else if (remain == 2)
    {
        uint32_t chunk = (static_cast<unsigned char>(data[i]) << 16)
                       | (static_cast<unsigned char>(data[i + 1]) << 8);
        encoded += kBase64Chars[(chunk >> 18) & 0x3F];
        encoded += kBase64Chars[(chunk >> 12) & 0x3F];
        encoded += kBase64Chars[(chunk >> 6) & 0x3F];
        encoded += "=";
    }
    return encoded;
}

std::string BaseEmailSender::EncodeSubjectRfc2047(const std::string& subject)
{
    if (IsAllAscii(subject))
    {
        return subject;
    }
    return "=?UTF-8?B?" + Base64Encode(subject) + "?=";
}

bool BaseEmailSender::SendEmail(const std::string& to_email, const std::string& subject,
                                const std::string& content)
{
    // 由子类构建完整的邮件内容(报文头 + base64 正文), 返回内容直接作为发送数据上传
    std::string mime_message = BuildEmailBody(to_email, subject, content);

    // 创建 easy handle, 每次发送独立创建, 保证多工作线程并发调用安全
    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        ERR("邮件发送失败, curl easy handle 创建失败, 收件人: {}", to_email);
        throw ChatExcelException(ErrorCode::NOTIFY_SEND_FAILED);
    }

    // 设置 SMTPS 服务器地址(隐式 TLS)与登录凭证(用户名/授权码)
    std::string smtp_url = std::string(kSmtpsUrlPrefix) + mail_settings_.smtp_server;
    curl_easy_setopt(curl, CURLOPT_URL, smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, mail_settings_.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, mail_settings_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_settings_.from_email.c_str());

    // 设置收件人列表(SMTP 信封收件人, 决定邮件的实际投递)
    struct curl_slist* recipients = curl_slist_append(nullptr, to_email.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    // 设置上传模式与读回调, 按偏移分块供给邮件内容
    PayloadSource payload = {&mime_message, 0};
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, PayloadSourceReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(mime_message.size()));

    // 执行发送
    CURLcode result = curl_easy_perform(curl);

    // 释放收件人列表与 easy handle
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK)
    {
        ERR("邮件发送失败, curl 执行失败, 错误码: {} , 错误描述: {} , 收件人: {} , 主题: {}",
            static_cast<int>(result), curl_easy_strerror(result), to_email, subject);
        throw ChatExcelException(ErrorCode::NOTIFY_SEND_FAILED);
    }

    INFO("邮件发送成功, 收件人: {} , 主题: {}", to_email, subject);
    return true;
}

} // namespace notify_service
} // namespace chat_excel
