#include "svc_ai_service/prompt_template.h"

#include <utility>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace ai_service
{

PromptTemplate::PromptTemplate(std::string template_content, std::vector<std::string> placeholders)
    : template_content_(std::move(template_content)), placeholders_(std::move(placeholders))
{
    if (template_content_.empty())
    {
        ERR("提示词模板内容为空, 构建提示词模板对象失败");
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    INFO("提示词模板对象构建完成, 外部传入占位符数量: {}", placeholders_.size());
}

void PromptTemplate::SetPlaceholder(const std::string& key, const std::string& value)
{
    if (key.empty())
    {
        ERR("设置占位符失败, 占位符名称为空");
        throw ChatExcelException(ErrorCode::AI_PROMPT_TEMPLATE_ERROR);
    }
    // 占位符列表中不存在的占位符直接忽略, 避免无效映射参与替换
    for (const std::string& placeholder : placeholders_)
    {
        if (placeholder == key)
        {
            placeholder_value_map_[key] = value;
            return;
        }
    }
    WARN("设置占位符失败, 占位符列表中不存在占位符: {}", key);
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

} // namespace ai_service
} // namespace chat_excel
