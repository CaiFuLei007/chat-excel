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
        return "NotifiyService";
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

const char* ChatExcelException::what() const noexcept
{
    return error_message_.c_str();
}

} // namespace chat_excel
