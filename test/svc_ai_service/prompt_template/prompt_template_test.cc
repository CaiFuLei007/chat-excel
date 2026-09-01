#include "svc_ai_service/prompt_template.h"

#include <string>
#include <vector>
#include <cpp-toolkit/logger.h>
#include <gtest/gtest.h>
#include "common/exception.h"

using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;
using chat_excel::ai_service::PromptTemplate;

namespace
{

// 测试模板的占位符列表(占位符名称不包含花括号), 由外部传入
const std::vector<std::string> kTestPlaceholders = {"DataBase", "table_name", "user_input"};

/**
 * @brief 构建包含多种占位符与 JSON 花括号的测试模板
 * @return 测试模板内容
 */
std::string BuildTestTemplate()
{
    return "数据库类型: {DataBase}, 表名: {table_name}, 用户问题: {user_input}\n"
           "JSON 示例: { \"taskStatus\": [ {\"taskId\": 1} ] }\n"
           "未闭合: {not_closed";
}

/**
 * @brief 将占位符名称转换为完整的占位符字符串
 * @param key 占位符名称
 * @return {} 包裹的占位符字符串
 */
std::string WrapPlaceholder(const std::string& key)
{
    return "{" + key + "}";
}

} // namespace

// 构造函数测试 : 正常模板与空模板
TEST(PromptTemplateTest, Constructor)
{
    // 正常模板与占位符列表构建成功
    PromptTemplate normal_template(BuildTestTemplate(), kTestPlaceholders);
    (void)normal_template;

    // 空模板构建失败, 抛出提示词模板错误码异常
    try
    {
        PromptTemplate empty_template("", kTestPlaceholders);
        FAIL() << "空模板构建应该抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
}

// 占位符过滤测试 : 占位符列表内的占位符可以设置, 列表外的占位符设置时被忽略
TEST(PromptTemplateTest, SetPlaceholderFilter)
{
    PromptTemplate prompt_template(BuildTestTemplate(), kTestPlaceholders);

    // 占位符列表内的占位符可以设置成功
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("DataBase", "MySQL"));
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("table_name", "tbl_employee"));
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("user_input", "统计各部门薪资"));

    // 占位符列表外的占位符设置时被忽略(包括模板中 JSON 示例内部的标识符)
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("taskStatus", "xxx"));
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("taskId", "xxx"));

    // 占位符列表外且模板中不存在的占位符设置时被忽略
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("not_exist", "xxx"));

    // 列表外的占位符设置不生效, JSON 示例内容在生成结果中保留原样
    EXPECT_NE(prompt_template.Generate().find("{ \"taskStatus\": [ {\"taskId\": 1} ] }"),
              std::string::npos);
}

// 占位符替换测试 : 验证生成的最终提示词内容正确
TEST(PromptTemplateTest, Generate)
{
    std::string template_content = BuildTestTemplate();
    PromptTemplate prompt_template(template_content, kTestPlaceholders);
    prompt_template.SetPlaceholder("DataBase", "MySQL");
    prompt_template.SetPlaceholder("table_name", "tbl_employee");
    prompt_template.SetPlaceholder("user_input", "统计各部门薪资");

    std::string result = prompt_template.Generate();

    // 已设置值的占位符被替换为实际值
    EXPECT_EQ(result.find(WrapPlaceholder("DataBase")), std::string::npos);
    EXPECT_EQ(result.find(WrapPlaceholder("table_name")), std::string::npos);
    EXPECT_EQ(result.find(WrapPlaceholder("user_input")), std::string::npos);
    EXPECT_NE(result.find("数据库类型: MySQL"), std::string::npos);
    EXPECT_NE(result.find("表名: tbl_employee"), std::string::npos);
    EXPECT_NE(result.find("用户问题: 统计各部门薪资"), std::string::npos);

    // JSON 花括号与未闭合花括号保留原样
    EXPECT_NE(result.find("{ \"taskStatus\": [ {\"taskId\": 1} ] }"), std::string::npos);
    EXPECT_NE(result.find("{not_closed"), std::string::npos);
}

// 重复占位符替换测试 : 同一占位符在模板中出现多次时全部被替换
TEST(PromptTemplateTest, GenerateWithRepeatedPlaceholder)
{
    // 分析提示词中 {DataBase} 出现多次
    std::string template_content =
        "数据库类型: {DataBase}, SQL 必须符合 {DataBase} 语法, 使用 {DataBase} 函数";
    PromptTemplate prompt_template(template_content, {"DataBase"});
    prompt_template.SetPlaceholder("DataBase", "SQLite");

    std::string result = prompt_template.Generate();

    EXPECT_EQ(result, "数据库类型: SQLite, SQL 必须符合 SQLite 语法, 使用 SQLite 函数");
}

// 未设置占位符保留原样测试 : 未设置值的占位符不做任何处理
TEST(PromptTemplateTest, GenerateWithUnsetPlaceholder)
{
    PromptTemplate prompt_template(BuildTestTemplate(), kTestPlaceholders);
    prompt_template.SetPlaceholder("DataBase", "MySQL");

    std::string result = prompt_template.Generate();

    // 已设置的占位符被替换, 未设置的占位符保留原样
    EXPECT_NE(result.find("数据库类型: MySQL"), std::string::npos);
    EXPECT_NE(result.find(WrapPlaceholder("table_name")), std::string::npos);
    EXPECT_NE(result.find(WrapPlaceholder("user_input")), std::string::npos);
}

// 空占位符列表测试 : 未传入占位符列表时所有占位符设置均被忽略, 模板内容保留原样
TEST(PromptTemplateTest, EmptyPlaceholdersIgnored)
{
    std::string template_content = BuildTestTemplate();
    PromptTemplate prompt_template(template_content, {});
    prompt_template.SetPlaceholder("DataBase", "MySQL");

    // 所有设置均被忽略, 生成结果与模板内容一致
    EXPECT_EQ(prompt_template.Generate(), template_content);
}

// 覆盖设置测试 : 后设置的值覆盖先设置的值
TEST(PromptTemplateTest, OverwritePlaceholder)
{
    PromptTemplate prompt_template(BuildTestTemplate(), kTestPlaceholders);
    prompt_template.SetPlaceholder("DataBase", "MySQL");
    prompt_template.SetPlaceholder("DataBase", "PostgreSQL");

    EXPECT_NE(prompt_template.Generate().find("数据库类型: PostgreSQL"), std::string::npos);
}

// 空占位符名称测试 : 设置空占位符名称时抛出异常
TEST(PromptTemplateTest, SetEmptyKey)
{
    PromptTemplate prompt_template(BuildTestTemplate(), kTestPlaceholders);

    try
    {
        prompt_template.SetPlaceholder("", "value");
        FAIL() << "空占位符名称设置应该抛出异常";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
}

// 特殊字符值替换测试 : 占位符实际值中包含特殊字符时正常替换
TEST(PromptTemplateTest, GenerateWithSpecialCharValue)
{
    std::string template_content = "表名: {table_name}, 用户问题: {user_input}";
    PromptTemplate prompt_template(template_content, {"table_name", "user_input"});
    // 实际值中包含反引号, 中文与空格等特殊字符
    prompt_template.SetPlaceholder("table_name", "`员工信息表`");
    prompt_template.SetPlaceholder("user_input", "统计 各部门 薪资");

    EXPECT_EQ(prompt_template.Generate(), "表名: `员工信息表`, 用户问题: 统计 各部门 薪资");
}

// 下划线占位符测试 : 下划线开头的占位符名称可以正常设置和替换
TEST(PromptTemplateTest, UnderscorePlaceholder)
{
    std::string template_content = "数据: {_result_json}, 结束";
    PromptTemplate prompt_template(template_content, {"_result_json"});
    prompt_template.SetPlaceholder("_result_json", "查询结果");

    EXPECT_EQ(prompt_template.Generate(), "数据: 查询结果, 结束");
}

// 非占位符内容测试 : JSON 花括号等非占位符内容不在占位符列表中, 设置被忽略且生成时保留原样
TEST(PromptTemplateTest, InvalidPlaceholderIgnored)
{
    std::string template_content = "JSON: {\"subject\": \"主题\"}, 空: {}, 数字开头: {1abc}";
    // 占位符列表中仅包含模板中真实存在的占位符, JSON 键与数字开头内容均不属于占位符
    PromptTemplate prompt_template(template_content, {"subject"});

    // 非占位符内容设置时被忽略, JSON 内容在生成结果中保留原样
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("1abc", "xxx"));
    EXPECT_NO_THROW(prompt_template.SetPlaceholder("subject", "xxx"));
    EXPECT_EQ(prompt_template.Generate(), template_content);
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察提示词模板类日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "prompt_template_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
