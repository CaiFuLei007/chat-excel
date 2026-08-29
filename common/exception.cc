#include "exception.h"

#include <string>
#include <unordered_map>

namespace chat_excel
{

std::string ErrorMessage(ErrorCode error_code)
{
    // 错误码与错误信息的映射表, 后续新增错误码时在此处补充对应的中文描述
    static const std::unordered_map<int, std::string> kErrorMessageMap = {
        {static_cast<int>(ErrorCode::SUCCESS), "成功"},
        {static_cast<int>(ErrorCode::USER_DATA_MYSQL_ERROR), "用户数据 MySQL 操作失败"},
        {static_cast<int>(ErrorCode::USER_DATA_REDIS_ERROR), "用户数据 Redis 操作失败"},
        {static_cast<int>(ErrorCode::USER_DATA_SERIALIZE_ERROR), "用户数据 JSON 序列化或反序列化失败"},
        {static_cast<int>(ErrorCode::SESSION_DATA_MYSQL_ERROR), "会话数据 MySQL 操作失败"},
        {static_cast<int>(ErrorCode::SESSION_DATA_REDIS_ERROR), "会话数据 Redis 操作失败"},
        {static_cast<int>(ErrorCode::SESSION_DATA_SERIALIZE_ERROR), "会话数据 JSON 序列化或反序列化失败"},
        {static_cast<int>(ErrorCode::VERIFYCODE_DATA_REDIS_ERROR), "验证码数据 Redis 操作失败"},
        {static_cast<int>(ErrorCode::VERIFYCODE_DATA_SERIALIZE_ERROR), "验证码数据 JSON 序列化或反序列化失败"},
        {static_cast<int>(ErrorCode::USER_DATA_NOT_FOUND), "用户数据不存在"},
        {static_cast<int>(ErrorCode::USER_PASSWORD_ERROR), "用户密码错误"},
        {static_cast<int>(ErrorCode::VERIFYCODE_ERROR), "验证码无效或已过期"},
        {static_cast<int>(ErrorCode::SESSION_NOT_FOUND), "会话不存在或已失效"},
        {static_cast<int>(ErrorCode::USER_PASSWORD_ENCRYPT_ERROR), "用户密码加密失败"},
        {static_cast<int>(ErrorCode::USER_NICKNAME_EXISTS), "用户昵称已存在"},
        {static_cast<int>(ErrorCode::USER_EMAIL_EXISTS), "用户邮箱已存在"},
        {static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR), "用户子服务请求参数错误"},
        {static_cast<int>(ErrorCode::USER_SERVICE_INTERNAL_ERROR), "用户子服务内部错误"},
        {static_cast<int>(ErrorCode::USER_NOTIFY_RPC_ERROR), "通知子服务 RPC 调用失败"},
        {static_cast<int>(ErrorCode::FILE_SAVE_TO_MYSQL_ERROR), "保存文件信息到数据库失败"},
        {static_cast<int>(ErrorCode::FILE_UPDATE_IN_MYSQL_ERROR), "更新数据库中的文件信息失败"},
        {static_cast<int>(ErrorCode::FILE_GET_BY_FILE_ID_ERROR), "通过文件 ID 获取文件信息失败"},
        {static_cast<int>(ErrorCode::FILE_DELETE_BY_FILE_ID_ERROR), "通过文件 ID 删除文件信息失败"},
        {static_cast<int>(ErrorCode::FILE_GET_LIST_BY_USER_ID_ERROR), "通过用户 ID 获取文件列表失败"},
        {static_cast<int>(ErrorCode::FILE_DATA_SERIALIZE_ERROR), "文件数据 JSON 序列化或反序列化失败"},
        {static_cast<int>(ErrorCode::FILE_DATA_NOT_FOUND), "文件数据不存在"},
        {static_cast<int>(ErrorCode::FILE_SAVE_TO_CACHE_ERROR), "保存文件数据到缓存失败"},
        {static_cast<int>(ErrorCode::FILE_GET_FROM_CACHE_BY_FILE_ID_ERROR), "通过文件 ID 从缓存获取文件信息失败"},
        {static_cast<int>(ErrorCode::FILE_DELETE_FROM_CACHE_BY_FILE_ID_ERROR), "通过文件 ID 删除缓存文件信息失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_SAVE_TO_MYSQL_ERROR), "保存 WorkSheet 信息到数据库失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_GET_BY_FILE_ID_ERROR), "通过文件 ID 获取 WorkSheet 信息失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_DELETE_BY_FILE_ID_ERROR), "通过文件 ID 删除 WorkSheet 信息失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_DATA_SERIALIZE_ERROR), "WorkSheet 数据 JSON 序列化或反序列化失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_SAVE_TO_CACHE_ERROR), "保存 WorkSheet 数据到缓存失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_GET_FROM_CACHE_BY_FILE_ID_ERROR), "通过文件 ID 从缓存获取 WorkSheet 信息失败"},
        {static_cast<int>(ErrorCode::WORKSHEET_DELETE_FROM_CACHE_BY_FILE_ID_ERROR), "通过文件 ID 删除缓存 WorkSheet 信息失败"},
        {static_cast<int>(ErrorCode::FILE_USER_MISMATCH), "当前用户与文件属主不一致"},
        {static_cast<int>(ErrorCode::FILE_FDFS_UPLOAD_ERROR), "上传文件数据到 FastDFS 失败"},
        {static_cast<int>(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR), "从 FastDFS 下载文件数据失败"},
        {static_cast<int>(ErrorCode::FILE_FDFS_DELETE_ERROR), "从 FastDFS 删除文件数据失败"},
        {static_cast<int>(ErrorCode::FILE_EXCEL_PARSE_RPC_ERROR), "Excel 解析子服务 RPC 调用失败"},
        {static_cast<int>(ErrorCode::FILE_LOCAL_FILE_ERROR), "本地文件读写操作失败"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_SESSION_ID_EMPTY), "文件子服务请求参数错误 : 会话 ID 为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_USER_ID_EMPTY), "文件子服务请求参数错误 : 用户 ID 为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_FILE_ID_EMPTY), "文件子服务请求参数错误 : 文件 ID 为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_FILE_INFO_EMPTY), "文件子服务请求参数错误 : 文件信息缺失"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_FILE_NAME_EMPTY), "文件子服务请求参数错误 : 文件名为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_FILE_EXT_EMPTY), "文件子服务请求参数错误 : 文件扩展名为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_FILE_CONTENT_EMPTY), "文件子服务请求参数错误 : 上传的文件数据为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_CHAT_SESSION_ID_EMPTY), "文件子服务请求参数错误 : 聊天会话 ID 为空"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_PAGE_NUMBER_ERROR), "文件子服务请求参数错误 : 预览页码无效(页码从 1 开始)"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_PAGE_SIZE_ERROR), "文件子服务请求参数错误 : 预览每页行数无效(每页行数从 1 开始)"},
        {static_cast<int>(ErrorCode::FILE_SERVICE_INTERNAL_ERROR), "文件子服务内部错误"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED), "Excel 文件打开失败"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_WORKSHEET_NOT_FOUND), "worksheet 不存在"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_FAILED), "Excel 解析失败"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_PARAMS_ERROR), "Excel 解析子服务请求参数错误"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_INTERNAL_ERROR), "Excel 解析子服务内部错误"},
        {static_cast<int>(ErrorCode::EXCEL_PARSE_FDFS_DOWNLOAD_ERROR), "从 FastDFS 下载 Excel 文件失败"},
        {static_cast<int>(ErrorCode::NOTIFY_SEND_FAILED), "邮件发送失败"},
        {static_cast<int>(ErrorCode::NOTIFY_VERIFYCODE_EMAIL_EMPTY), "验证码邮件接收方邮箱为空"},
        {static_cast<int>(ErrorCode::NOTIFY_VERIFYCODE_CODE_EMPTY), "验证码为空"},
        {static_cast<int>(ErrorCode::NOTIFY_EMAIL_TO_EMPTY), "普通邮件接收方邮箱为空"},
        {static_cast<int>(ErrorCode::NOTIFY_EMAIL_SUBJECT_EMPTY), "普通邮件主题为空"},
        {static_cast<int>(ErrorCode::NOTIFY_EMAIL_CONTENT_EMPTY), "普通邮件内容为空"},
        {static_cast<int>(ErrorCode::NOTIFY_SERVICE_INTERNAL_ERROR), "通知子服务内部错误"},
    };

    auto iter = kErrorMessageMap.find(static_cast<int>(error_code));
    if (iter == kErrorMessageMap.end())
    {
        return "未知错误";
    }
    return iter->second;
}

std::string GetServiceName(ErrorCode error_code)
{
    // 取出整形错误码, 用于按范围判断所属子服务
    int code = static_cast<int>(error_code);

    if (error_code == ErrorCode::SUCCESS)
    {
        return "服务成功 , 无错误";
    }
    // 用户子服务错误码范围 100 - 199
    else if (code >= 100 && code <= 199)
    {
        return "UserService";
    }
    // 文件子服务错误码范围 200 - 299
    else if (code >= 200 && code <= 299)
    {
        return "FileService";
    }
    // 数据库子服务错误码范围 300 - 399
    else if (code >= 300 && code <= 399)
    {
        return "DatabaseService";
    }
    // Excel 解析子服务错误码范围 400 - 499
    else if (code >= 400 && code <= 499)
    {
        return "ExcelService";
    }
    // 通知子服务错误码范围 500 - 599
    else if (code >= 500 && code <= 599)
    {
        return "NotifyService";
    }
    // AI 子服务错误码范围 600 - 699
    else if (code >= 600 && code <= 699)
    {
        return "AIService";
    }
    // 网关子服务错误码范围 700 - 799
    else if (code >= 700 && code <= 799)
    {
        return "GatewayService";
    }
    // 错误码不在任何子服务范围内
    else
    {
        return "未知错误";
    }
}

ChatExcelException::ChatExcelException(ErrorCode error_code)
    : error_code_(error_code),
      error_message_(GetServiceName(error_code) + " : " + ErrorMessage(error_code))
{
}

ErrorCode ChatExcelException::error_code() const noexcept
{
    return error_code_;
}

const char* ChatExcelException::what() const noexcept
{
    return error_message_.c_str();
}

} // namespace chat_excel
