#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace chat_excel
{
namespace ai_service
{

/**
 * @brief 提示词模板类, 负责管理提示词模板与占位符的映射关系,
 *        占位符列表由外部构造时传入(占位符名称不包含花括号),
 *        通过替换模板中的占位符生成最终发送给模型的提示词
 */
class PromptTemplate
{
public:
    /**
     * @brief 构造函数, 使用提示词与外部传入的占位符列表构建提示词模板对象
     * @param template_content 提示词模板内容, 内部包含 {} 包裹的占位符
     * @param placeholders 模板中的占位符列表, 占位符名称不包含花括号, 例如 user_input
     */
    PromptTemplate(std::string template_content, std::vector<std::string> placeholders);

    ~PromptTemplate() = default;

    PromptTemplate(const PromptTemplate&) = default;

    PromptTemplate& operator=(const PromptTemplate&) = default;

    /**
     * @brief 设置占位符与实际值的映射关系, 占位符列表中不存在的占位符将被忽略并记录告警日志
     * @param key 占位符名称(不包含花括号, 例如 user_input)
     * @param value 占位符对应的实际值
     */
    void SetPlaceholder(const std::string& key, const std::string& value);

    /**
     * @brief 生成最终的提示词, 将模板中已设置值的占位符替换为实际值,
     *        未设置值的占位符保留原样
     * @return 替换占位符后的最终提示词
     */
    std::string Generate() const;

private:
    // 提示词模板内容
    std::string template_content_;

    // 外部传入的占位符列表
    std::vector<std::string> placeholders_;

    // 占位符与实际值的映射关系
    std::unordered_map<std::string, std::string> placeholder_value_map_;
};

} // namespace ai_service
} // namespace chat_excel
