#include "svc_ai_service/prompt_template.h"

#include <cctype>
#include <utility>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace ai_service
{

namespace
{

/**
 * @brief 判断字符串是否为合法标识符(仅包含字母, 数字, 下划线, 且以字母或下划线开头)
 * @param str 待判断的字符串
 * @return 合法标识符返回 true, 否则返回 false
 */
bool IsIdentifier(const std::string& str)
{
    if (str.empty())
    {
        return false;
    }
    // 首字符必须是字母或下划线, 用于排除模板中 JSON 示例等非占位符内容
    char first = str.front();
    if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_'))
    {
        return false;
    }
    for (char ch : str)
    {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
        {
            return false;
        }
    }
    return true;
}

} // namespace

PromptTemplate::PromptTemplate(std::string template_content)
    : template_content_(std::move(template_content))
{
    if (template_content_.empty())
    {
        ERR("提示词模板内容为空, 构建提示词模板对象失败");
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    ExtractPlaceholders();
    INFO("提示词模板对象构建完成, 提取到 {} 个占位符", placeholder_set_.size());
}

void PromptTemplate::SetPlaceholder(const std::string& key, const std::string& value)
{
    if (key.empty())
    {
        ERR("设置占位符失败, 占位符名称为空");
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    // 模板中不存在的占位符直接忽略, 避免无效映射参与替换
    if (placeholder_set_.find(key) == placeholder_set_.end())
    {
        WARN("设置占位符失败, 模板中不存在占位符: {}", key);
        return;
    }
    placeholder_value_map_[key] = value;
}

std::string PromptTemplate::Generate() const
{
    std::string result = template_content_;
    // 遍历映射关系, 将模板中所有已设置值的占位符替换为实际值, 未设置的占位符保留原样
    for (const auto& [key, value] : placeholder_value_map_)
    {
        // 构建完整的占位符字符串, 例如 {user_input}
        std::string pattern = "{" + key + "}";
        size_t pos = result.find(pattern);
        while (pos != std::string::npos)
        {
            result.replace(pos, pattern.size(), value);
            // 跳过已替换的部分, 避免实际值中包含相同占位符导致重复替换
            pos = result.find(pattern, pos + value.size());
        }
    }
    return result;
}

void PromptTemplate::ExtractPlaceholders()
{
    size_t pos = template_content_.find('{');
    while (pos != std::string::npos)
    {
        size_t end = template_content_.find('}', pos + 1);
        // 找不到闭合花括号, 剩余内容不可能再出现合法占位符
        if (end == std::string::npos)
        {
            break;
        }
        std::string placeholder = template_content_.substr(pos + 1, end - pos - 1);
        // 仅提取合法标识符形式的占位符, JSON 花括号等内容不视为占位符
        if (IsIdentifier(placeholder))
        {
            placeholder_set_.insert(placeholder);
        }
        pos = template_content_.find('{', end + 1);
    }
}

} // namespace ai_service
} // namespace chat_excel
