#pragma once

#include <string>

namespace chat_excel
{
namespace notify_service
{

/**
 * @brief 邮箱配置结构体, 封装 SMTP 邮箱服务器连接所需的全部配置信息,
 *        由服务器构建器组装并注入邮箱发送器
 */
struct MailSettings
{
    // 邮箱用户名(SMTP 登录用户名, eg : xxx@qq.com)
    std::string username;

    // 邮箱授权码(SMTP 登录授权码, 非邮箱登录密码)
    std::string password;

    // 发送者邮件号(邮件 From 头中展示的发件人地址)
    std::string from_email;

    // 邮箱服务器地址(host:port 形式, eg : smtp.qq.com:465)
    std::string smtp_server;
};

/**
 * @brief 邮箱发送抽象基类, 基于 libcurl 封装 SMTP 邮件发送的通用主流程与回调函数
 *        (easy handle 创建 / SMTP 连接与上传发送), 邮件内容的构建通过纯虚函数
 *        BuildEmailBody 交由子类实现, SendEmail 将 BuildEmailBody 返回的内容
 *        直接作为发送数据上传; SendEmail 线程安全(每次调用创建独立的 easy handle, 配置只读)
 */
class BaseEmailSender
{
public:
    /**
     * @brief 构造函数, 注入邮箱配置信息
     * @param mail_settings 邮箱配置信息, 包含用户名、授权码、发送者邮件号与服务器地址
     */
    explicit BaseEmailSender(const MailSettings& mail_settings);

    // 虚析构函数, 保证通过基类指针析构子类对象时行为正确
    virtual ~BaseEmailSender() = default;

    /**
     * @brief 构建邮件内容, 由子类实现 : 内部构建 HTML 格式正文(包含邮件主题与邮件内容),
     *        再组装为完整的 MIME 邮件报文(报文头 + base64 编码正文),
     *        返回的内容由 SendEmail 直接作为发送数据上传
     * @param to_email 接收方邮箱地址
     * @param subject 邮件主题
     * @param content 邮件内容(验证码或 HTML 格式内容)
     * @return 完整的 MIME 邮件报文字符串
     */
    virtual std::string BuildEmailBody(const std::string& to_email, const std::string& subject,
                                       const std::string& content) = 0;

    /**
     * @brief 发送邮件 : 调用 BuildEmailBody 组装邮件内容, 将其返回的内容直接
     *        通过 libcurl 以 smtps:// 隐式 TLS 方式上传发送
     * @param to_email 接收方邮箱地址
     * @param subject 邮件标题
     * @param content 邮件正文内容(HTML 格式, 交由子类构建完整邮件内容)
     * @return 发送流程执行成功返回 true
     * @throws ChatExcelException 邮件发送失败(easy handle 创建失败或 curl 执行失败)时抛出
     */
    bool SendEmail(const std::string& to_email, const std::string& subject,
                   const std::string& content);

protected:
    /**
     * @brief 对数据进行 base64 编码, 供子类构建 MIME 邮件报文时编码正文使用
     * @param data 待编码的原始数据
     * @return base64 编码后的字符串
     */
    static std::string Base64Encode(const std::string& data);

    /**
     * @brief 对邮件主题进行 RFC 2047 编码, 保证中文主题在邮件头中不乱码;
     *        纯 ASCII 主题原样返回, 非 ASCII 主题编码为 =?UTF-8?B?<base64>?= 形式
     * @param subject 邮件主题
     * @return 编码后的邮件主题字符串
     */
    static std::string EncodeSubjectRfc2047(const std::string& subject);

    // base64 编码每行最大字符数(MIME 规范建议 76 字符换行)
    static constexpr size_t kBase64LineLength = 76;

    // 邮箱配置信息(用户名/授权码/发送者邮件号/服务器地址)
    MailSettings mail_settings_;
};

} // namespace notify_service
} // namespace chat_excel
