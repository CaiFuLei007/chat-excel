#pragma once

#include <exception>
#include <string>

namespace chat_excel
{

// 错误码枚举类, 错误码为整形, 按子服务划分错误码范围
enum class ErrorCode : int
{
    // 成功
    SUCCESS = 0,

    // ==================== 用户子服务错误码范围 100 - 199 ====================
    // 用户数据 MySQL 操作失败
    USER_DATA_MYSQL_ERROR = 100,

    // 用户数据 Redis 操作失败
    USER_DATA_REDIS_ERROR = 101,

    // 用户数据 JSON 序列化或反序列化失败
    USER_DATA_SERIALIZE_ERROR = 102,

    // 会话数据 MySQL 操作失败
    SESSION_DATA_MYSQL_ERROR = 103,

    // 会话数据 Redis 操作失败
    SESSION_DATA_REDIS_ERROR = 104,

    // 会话数据 JSON 序列化或反序列化失败
    SESSION_DATA_SERIALIZE_ERROR = 105,

    // 验证码数据 Redis 操作失败
    VERIFYCODE_DATA_REDIS_ERROR = 106,

    // 验证码数据 JSON 序列化或反序列化失败
    VERIFYCODE_DATA_SERIALIZE_ERROR = 107,

    // 用户数据不存在
    USER_DATA_NOT_FOUND = 108,

    // 用户密码错误
    USER_PASSWORD_ERROR = 109,

    // 验证码无效或已过期
    VERIFYCODE_ERROR = 110,

    // 会话不存在或已失效
    SESSION_NOT_FOUND = 111,

    // 用户密码加密失败
    USER_PASSWORD_ENCRYPT_ERROR = 112,

    // 用户昵称已存在
    USER_NICKNAME_EXISTS = 113,

    // 用户邮箱已存在
    USER_EMAIL_EXISTS = 114,

    // 用户子服务请求参数错误
    USER_SERVICE_PARAMS_ERROR = 115,

    // 用户子服务内部错误
    USER_SERVICE_INTERNAL_ERROR = 116,

    // ==================== 通知子服务错误码范围 500 - 599 ====================
    // 邮件发送失败
    NOTIFY_SEND_FAILED = 500,

    // 验证码邮件接收方邮箱为空
    NOTIFY_VERIFYCODE_EMAIL_EMPTY = 501,

    // 验证码为空
    NOTIFY_VERIFYCODE_CODE_EMPTY = 502,

    // 普通邮件接收方邮箱为空
    NOTIFY_EMAIL_TO_EMPTY = 503,

    // 普通邮件主题为空
    NOTIFY_EMAIL_SUBJECT_EMPTY = 504,

    // 普通邮件内容为空
    NOTIFY_EMAIL_CONTENT_EMPTY = 505,

    // 通知子服务内部错误
    NOTIFY_SERVICE_INTERNAL_ERROR = 506,

    // ==================== 子服务错误码范围预留 ====================
    // 文件子服务错误码范围 200 - 299, eg: FILE_NOT_FOUND 文件不存在
    // 数据库子服务错误码范围 300 - 399, eg: DB_CONNECTION_FAILED 数据库连接失败
    // Excel 解析子服务错误码范围 400 - 499, eg: EXCEL_PARSE_FAILED Excel 解析失败
    // 通知子服务错误码范围 500 - 599, eg: NOTIFY_SEND_FAILED 通知发送失败
    // AI 子服务错误码范围 600 - 699, eg: AI_MODEL_NOT_FOUND AI 模型不存在
    // 网关子服务错误码范围 700 - 799, eg: GATEWAY_CONNECTION_FAILED 网关连接失败
    // =============================================================
};

/**
 * @brief 将错误码转换为对应的错误信息(中文描述)
 * @param error_code 错误码
 * @return 错误码对应的中文描述, 错误码不存在时返回 "未知错误"
 */
std::string ErrorMessage(ErrorCode error_code);

/**
 * @brief 获取错误码所属的子服务名称
 * @param error_code 错误码
 * @return 错误码所属的子服务名称, 错误码不存在时返回 "未知错误"
 */
std::string GetServiceName(ErrorCode error_code);

/**
 * @brief 项目自定义异常类, 继承自 std::exception, 用于处理项目中出现的异常以及错误情况
 */
class ChatExcelException : public std::exception
{
public:
    explicit ChatExcelException(ErrorCode error_code);

    ~ChatExcelException() override = default;

    /**
     * @brief 获取异常携带的错误码
     * @return 异常对应的错误码
     */
    ErrorCode error_code() const noexcept;

    /**
     * @brief 获取错误码信息, 格式为 "服务名称 : 错误码描述"
     * @return "服务名称 : 错误码描述" 格式的错误码信息字符串
     */
    virtual const char* what() const noexcept override;

private:
    // 错误码
    ErrorCode error_code_;

    // 错误码信息(格式: 服务名称 : 错误码描述)
    std::string error_message_;
};

} // namespace chat_excel
