#include "common/exception.h"

#include <exception>
#include <string>

#include <gtest/gtest.h>

using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;

// 正常情况: 已定义的错误码返回对应的中文描述
TEST(ErrorMessageTest, ReturnDescriptionForDefinedCode)
{
    EXPECT_EQ(chat_excel::ErrorMessage(ErrorCode::SUCCESS), "成功");
}

// 异常情况: 未定义的错误码返回 "未知错误"
TEST(ErrorMessageTest, ReturnUnknownForUndefinedCode)
{
    EXPECT_EQ(chat_excel::ErrorMessage(static_cast<ErrorCode>(999)), "未知错误");
    EXPECT_EQ(chat_excel::ErrorMessage(static_cast<ErrorCode>(-1)), "未知错误");
}

// 正常情况: 各子服务范围内的错误码返回对应的子服务名称
TEST(GetServiceNameTest, ReturnNameForEachServiceRange)
{
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(150)), "UserService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(250)), "FileService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(350)), "DatabaseService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(450)), "ExcelService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(550)), "NotifiyService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(650)), "AIService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(750)), "GatewayService");
}

// 边界情况: 各子服务错误码范围的上下边界值
TEST(GetServiceNameTest, ReturnNameAtRangeBoundary)
{
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(99)), "未知错误");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(100)), "UserService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(199)), "UserService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(200)), "FileService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(299)), "FileService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(300)), "DatabaseService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(399)), "DatabaseService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(400)), "ExcelService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(499)), "ExcelService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(500)), "NotifiyService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(599)), "NotifiyService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(600)), "AIService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(699)), "AIService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(700)), "GatewayService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(799)), "GatewayService");
    EXPECT_EQ(chat_excel::GetServiceName(static_cast<ErrorCode>(800)), "未知错误");
    EXPECT_EQ(chat_excel::GetServiceName(ErrorCode::SUCCESS), "服务成功 , 无错误");
}

// 正常情况: what 方法返回 "服务名称 : 错误码描述" 格式的错误码信息
TEST(ChatExcelExceptionTest, WhatReturnFormattedMessage)
{
    ChatExcelException exception(ErrorCode::SUCCESS);

    EXPECT_STREQ(exception.what(), "服务成功 , 无错误 : 成功");
}

// 正常情况: 异常对象可被 std::exception 基类捕获并获取错误码信息
TEST(ChatExcelExceptionTest, CatchByStdExceptionBase)
{
    try
    {
        throw ChatExcelException(static_cast<ErrorCode>(250));
        FAIL() << "应当抛出 ChatExcelException 异常";
    }
    catch (const std::exception& e)
    {
        EXPECT_STREQ(e.what(), "FileService : 未知错误");
    }
}
