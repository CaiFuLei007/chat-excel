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

    // 通知子服务 RPC 调用失败(获取信道失败或调用超时/失败)
    USER_NOTIFY_RPC_ERROR = 117,

    // ==================== 文件子服务错误码范围 200 - 299 ====================
    // 保存文件信息到数据库失败
    FILE_SAVE_TO_MYSQL_ERROR = 200,

    // 更新数据库中的文件信息失败
    FILE_UPDATE_IN_MYSQL_ERROR = 201,

    // 通过文件 ID 获取文件信息失败
    FILE_GET_BY_FILE_ID_ERROR = 202,

    // 通过文件 ID 删除文件信息失败
    FILE_DELETE_BY_FILE_ID_ERROR = 203,

    // 通过用户 ID 获取文件列表失败
    FILE_GET_LIST_BY_USER_ID_ERROR = 204,

    // 文件数据 JSON 序列化或反序列化失败
    FILE_DATA_SERIALIZE_ERROR = 205,

    // 文件数据不存在
    FILE_DATA_NOT_FOUND = 206,

    // 保存文件数据到缓存失败
    FILE_SAVE_TO_CACHE_ERROR = 207,

    // 通过文件 ID 从缓存获取文件信息失败
    FILE_GET_FROM_CACHE_BY_FILE_ID_ERROR = 208,

    // 通过文件 ID 删除缓存文件信息失败
    FILE_DELETE_FROM_CACHE_BY_FILE_ID_ERROR = 209,

    // 保存 WorkSheet 信息到数据库失败
    WORKSHEET_SAVE_TO_MYSQL_ERROR = 210,

    // 通过文件 ID 获取 WorkSheet 信息失败
    WORKSHEET_GET_BY_FILE_ID_ERROR = 211,

    // 通过文件 ID 删除 WorkSheet 信息失败
    WORKSHEET_DELETE_BY_FILE_ID_ERROR = 212,

    // WorkSheet 数据 JSON 序列化或反序列化失败
    WORKSHEET_DATA_SERIALIZE_ERROR = 213,

    // 保存 WorkSheet 数据到缓存失败
    WORKSHEET_SAVE_TO_CACHE_ERROR = 214,

    // 通过文件 ID 从缓存获取 WorkSheet 信息失败
    WORKSHEET_GET_FROM_CACHE_BY_FILE_ID_ERROR = 215,

    // 通过文件 ID 删除缓存 WorkSheet 信息失败
    WORKSHEET_DELETE_FROM_CACHE_BY_FILE_ID_ERROR = 216,

    // 当前用户与文件属主不一致
    FILE_USER_MISMATCH = 217,

    // 上传文件数据到 FastDFS 失败
    FILE_FDFS_UPLOAD_ERROR = 218,

    // 从 FastDFS 下载文件数据失败
    FILE_FDFS_DOWNLOAD_ERROR = 219,

    // 从 FastDFS 删除文件数据失败
    FILE_FDFS_DELETE_ERROR = 220,

    // Excel 解析子服务 RPC 调用失败(获取信道失败或调用超时/失败)
    FILE_EXCEL_PARSE_RPC_ERROR = 221,

    // 本地文件读写操作失败
    FILE_LOCAL_FILE_ERROR = 222,

    // 文件子服务请求参数错误 : 会话 ID 为空
    FILE_SERVICE_SESSION_ID_EMPTY = 223,

    // 文件子服务请求参数错误 : 用户 ID 为空
    FILE_SERVICE_USER_ID_EMPTY = 224,

    // 文件子服务请求参数错误 : 文件 ID 为空
    FILE_SERVICE_FILE_ID_EMPTY = 225,

    // 文件子服务请求参数错误 : 文件信息缺失
    FILE_SERVICE_FILE_INFO_EMPTY = 226,

    // 文件子服务请求参数错误 : 文件名为空
    FILE_SERVICE_FILE_NAME_EMPTY = 227,

    // 文件子服务请求参数错误 : 文件扩展名为空
    FILE_SERVICE_FILE_EXT_EMPTY = 228,

    // 文件子服务请求参数错误 : 上传的文件数据为空
    FILE_SERVICE_FILE_CONTENT_EMPTY = 229,

    // 文件子服务请求参数错误 : 聊天会话 ID 为空
    FILE_SERVICE_CHAT_SESSION_ID_EMPTY = 230,

    // 文件子服务请求参数错误 : 预览页码无效(页码从 1 开始)
    FILE_SERVICE_PAGE_NUMBER_ERROR = 231,

    // 文件子服务请求参数错误 : 预览每页行数无效(每页行数从 1 开始)
    FILE_SERVICE_PAGE_SIZE_ERROR = 232,

    // 文件子服务内部错误
    FILE_SERVICE_INTERNAL_ERROR = 233,

    // ==================== 数据库子服务错误码范围 300 - 399 ====================
    // 数据库连接失败
    DB_CONNECTION_FAILED = 300,

    // 数据库断开连接失败
    DB_DISCONNECT_FAILED = 301,

    // 数据库尚未建立连接
    DB_NOT_CONNECTED = 302,

    // 数据库配置无效
    DB_CONFIG_INVALID = 303,

    // 不支持的数据库类型
    DB_UNSUPPORTED_DATABASE_TYPE = 304,

    // SQL 执行失败
    DB_EXECUTE_FAILED = 305,

    // 预编译语句构建失败
    DB_PREPARE_FAILED = 306,

    // 预编译语句参数绑定失败
    DB_PARAM_BIND_FAILED = 307,

    // 查询结果获取失败
    DB_RESULT_FETCH_FAILED = 308,

    // 事务操作失败
    DB_TRANSACTION_FAILED = 309,

    // SQL 语句为空
    DB_SQL_EMPTY = 310,

    // SQL 语句无效或类型不受支持
    DB_SQL_INVALID = 311,

    // SQL 语句包含危险操作
    DB_SQL_DANGEROUS = 312,

    // SQL 语句包含多条语句
    DB_SQL_MULTIPLE_STATEMENTS = 313,

    // 数据库标识符(表名, 列名)无效
    DB_IDENTIFIER_INVALID = 314,

    // 数据库连接不存在
    DB_CONNECTION_NOT_FOUND = 315,

    // 数据库连接受保护, 禁止删除
    DB_CONNECTION_PROTECTED = 316,

    // 文件子服务 RPC 调用失败(获取信道失败或调用超时/失败)
    DB_FILE_RPC_ERROR = 317,

    // 数据库子服务本地文件操作失败
    DB_LOCAL_FILE_ERROR = 318,

    // 从 FastDFS 下载数据库文件失败
    DB_FDFS_DOWNLOAD_ERROR = 319,

    // 数据 JSON 序列化或反序列化失败
    DB_SERIALIZE_ERROR = 320,

    // 数据库子服务请求参数错误 : 会话 ID 为空
    DB_SERVICE_SESSION_ID_EMPTY = 321,

    // 数据库子服务请求参数错误 : 用户 ID 为空
    DB_SERVICE_USER_ID_EMPTY = 322,

    // 数据库子服务请求参数错误 : 数据库连接 ID 为空
    DB_SERVICE_CONNECTION_ID_EMPTY = 323,

    // 数据库子服务请求参数错误 : 表名为空
    DB_SERVICE_TABLE_NAME_EMPTY = 324,

    // 数据库子服务请求参数错误 : SQL 语句为空
    DB_SERVICE_SQL_EMPTY = 325,

    // 数据库子服务内部错误
    DB_SERVICE_INTERNAL_ERROR = 326,

    // 数据库子服务请求参数错误 : 要删除的表名列表为空
    DB_SERVICE_TABLE_NAMES_EMPTY = 327,

    // 数据库子服务请求参数错误 : WorkSheet 数据缺失
    DB_SERVICE_WORKSHEET_DATA_EMPTY = 328,

    // ==================== Excel 解析子服务错误码范围 400 - 499 ====================
    // Excel 文件打开失败(文件不存在或格式非法)
    EXCEL_PARSE_FILE_OPEN_FAILED = 400,

    // worksheet 不存在
    EXCEL_PARSE_WORKSHEET_NOT_FOUND = 401,

    // Excel 解析过程失败
    EXCEL_PARSE_FAILED = 402,

    // Excel 解析子服务请求参数错误
    EXCEL_PARSE_PARAMS_ERROR = 403,

    // Excel 解析子服务内部错误
    EXCEL_PARSE_INTERNAL_ERROR = 404,

    // 从 FastDFS 下载 Excel 文件失败
    EXCEL_PARSE_FDFS_DOWNLOAD_ERROR = 405,

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
