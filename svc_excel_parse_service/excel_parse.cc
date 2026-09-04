#include "excel_parse.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include <OpenXLSX.hpp>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace excel_parse_service
{

namespace
{

// N/A 单元格文本标识(统一转为小写后比较, 不参与列类型统计)
constexpr const char* kNaCellText = "n/a";

// 空列名的默认前缀(column_列号)
constexpr const char* kEmptyColumnPrefix = "column_";

// 数字开头列名的前缀(col_列名)
constexpr const char* kDigitStartPrefix = "col_";

// 布尔真值的全部文本表示形式
const std::vector<std::string> kBooleanTrueTexts = {"true", "1", "t", "yes", "y"};

// 布尔假值的全部文本表示形式
const std::vector<std::string> kBooleanFalseTexts = {"false", "0", "f", "no", "n"};

/**
 * @brief 将 ASCII 字母统一转为小写, 用于布尔等文本的忽略大小写比较
 * @param text 输入字符串
 * @return 转换后的全小写字符串
 */
std::string ToLowerAscii(const std::string& text)
{
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return lower_text;
}

/**
 * @brief 去除字符串前后空白字符(空格/制表符/回车/换行)
 * @param text 输入字符串
 * @return 去除前后空白后的字符串
 */
std::string TrimWhitespace(const std::string& text)
{
    size_t start_pos = 0;
    while (start_pos < text.size() && std::isspace(static_cast<unsigned char>(text[start_pos])))
    {
        ++start_pos;
    }
    size_t end_pos = text.size();
    while (end_pos > start_pos && std::isspace(static_cast<unsigned char>(text[end_pos - 1])))
    {
        --end_pos;
    }
    return text.substr(start_pos, end_pos - start_pos);
}

/**
 * @brief 判断文本是否为布尔值(真假所有表示形式, 忽略大小写)
 * @param lower_text 全小写文本
 * @return 是布尔值表示返回 true
 */
bool IsBooleanText(const std::string& lower_text)
{
    if (std::find(kBooleanTrueTexts.begin(), kBooleanTrueTexts.end(), lower_text) != kBooleanTrueTexts.end())
    {
        return true;
    }
    return std::find(kBooleanFalseTexts.begin(), kBooleanFalseTexts.end(), lower_text) != kBooleanFalseTexts.end();
}

/**
 * @brief 判断文本是否为整型 : 仅存在数值, 以及开头的 +/- 号
 * @param text 已去除前后空白的文本
 * @return 是整型表示返回 true
 */
bool IsIntegerText(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    size_t pos = 0;
    // 开头允许一个 +/- 号
    if (text[pos] == '+' || text[pos] == '-')
    {
        ++pos;
    }
    // 剩余字符必须全部为数字且至少存在一位数字
    bool has_digit = false;
    while (pos < text.size())
    {
        if (!std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            return false;
        }
        has_digit = true;
        ++pos;
    }
    return has_digit;
}

/**
 * @brief 判断文本是否为浮点型 : 存在小数点, 除小数点外仅包含数字与开头的 +/- 号
 * @param text 已去除前后空白的文本
 * @return 是浮点型表示返回 true
 */
bool IsFloatText(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    size_t pos = 0;
    // 开头允许一个 +/- 号
    if (text[pos] == '+' || text[pos] == '-')
    {
        ++pos;
    }
    bool has_dot = false;
    bool has_digit = false;
    while (pos < text.size())
    {
        if (std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            has_digit = true;
        }
        else if (text[pos] == '.')
        {
            // 不允许多个小数点
            if (has_dot)
            {
                return false;
            }
            has_dot = true;
        }
        else
        {
            return false;
        }
        ++pos;
    }
    return has_dot && has_digit;
}

/**
 * @brief 校验年月日数值范围(月份 1 - 12 , 天数 1 - 31)
 * @param year 年份数值
 * @param month 月份数值
 * @param day 天数数值
 * @return 合法返回 true
 */
bool IsValidDateParts(int year, int month, int day)
{
    return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

// 日期匹配正则 : 覆盖 6 种格式
//   1. yyyy-mm-dd / yyyy/mm/dd / yyyy.mm.dd (sep1 分隔年月日)
//   2. mm-dd-yyyy / mm/dd/yyyy     (sep2 分隔月日年)
//   3. yyyy-mm-dd-HH:MM:SS         (日期 + 时间)
// 数字部分用捕获组取出, 配合数值范围校验
const std::regex kDatePattern(
    // 大端日期 : 4 位年 sep1 2 位月 sep1 2 位日, sep1 取 - / . 三种
    R"(^(\d{4})([-/.])(\d{2})\2(\d{2})$)"
    // 交替分支 : 上述结构后接 -HH:MM:SS 的时间形式
    R"(|^(\d{4})-(\d{2})-(\d{2})-(\d{2}):(\d{2}):(\d{2})$)"
    // 美式日期 : 2 位月 sep2 2 位日 sep2 4 位年, sep2 取 - / 两种
    R"(|^(\d{2})([-/])(\d{2})\9(\d{4})$)");

/**
 * @brief 匹配全部 6 种支持的日期格式 :
 *        yyyy-mm-dd / yyyy/mm/dd / yyyy.mm.dd / mm-dd-yyyy / mm/dd/yyyy / yyyy-mm-dd-HH:MM:SS ,
 *        正则完成格式匹配后进一步校验年月日/时分秒的数值范围
 * @param text 已去除前后空白的文本
 * @return 属于任一日期格式返回 true
 */
bool IsDateText(const std::string& text)
{
    std::smatch match;
    if (!std::regex_match(text, match, kDatePattern))
    {
        return false;
    }

    // 大端日期分支 : 年(1) 月(3) 日(4)
    if (match[1].matched)
    {
        return IsValidDateParts(std::stoi(match[1].str()), std::stoi(match[3].str()),
                                std::stoi(match[4].str()));
    }
    // 日期时间分支 : 年(5) 月(6) 日(7) 时(8) 分(9) 秒(10) , 注意捕获组编号与美式分支共用后需重新核对
    if (match[5].matched)
    {
        int hour = std::stoi(match[8].str());
        int minute = std::stoi(match[9].str());
        int second = std::stoi(match[10].str());
        return IsValidDateParts(std::stoi(match[5].str()), std::stoi(match[6].str()),
                                std::stoi(match[7].str())) &&
               hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
    }
    // 美式日期分支 : 月(11) 日(13) 年(14)
    return IsValidDateParts(std::stoi(match[14].str()), std::stoi(match[11].str()),
                            std::stoi(match[13].str()));
}

/**
 * @brief 对文本单元格进行类型检测, 检测顺序 : 布尔 -> 整型 -> 浮点 -> 日期 -> 字符串
 * @param text 已去除前后空白的文本
 * @return 检测出的单元格类型
 */
CellType DetectTextCellType(const std::string& text)
{
    std::string lower_text = ToLowerAscii(text);
    if (IsBooleanText(lower_text))
    {
        return CellType::BOOLEAN;
    }
    else if (IsIntegerText(text))
    {
        return CellType::INTEGER;
    }
    else if (IsFloatText(text))
    {
        return CellType::FLOAT;
    }
    else if (IsDateText(text))
    {
        return CellType::DATE;
    }
    else
    {
        return CellType::STRING;
    }
}

/**
 * @brief 将 double 转换为字符串, 采用能保证二进制精度往返一致的最短表示,
 *        避免 to_string 固定 6 位小数的精度损失
 * @param value 浮点值
 * @return 浮点值对应的字符串
 */
std::string DoubleToString(double value)
{
    // %.17g 可保证 double 完整往返, 从 15 位有效数字开始寻找最短表示
    constexpr size_t kBufferMaxChars = 48;
    char buffer[kBufferMaxChars];
    for (int precision = 15; precision <= 17; ++precision)
    {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        // 重新解析回 double, 与原值相等说明该精度足以还原原值
        if (std::strtod(buffer, nullptr) == value)
        {
            break;
        }
    }
    return buffer;
}

/**
 * @brief 安全读取 String/Error 类型单元格的原始文本, 读出失败时兜底占位
 * @param proxy 单元格值代理对象
 * @return 单元格原始文本
 */
std::string SafeGetCellText(const OpenXLSX::XLCellValueProxy& proxy)
{
    try
    {
        return proxy.get<std::string>();
    }
    catch (const std::exception&)
    {
        return "#ERROR";
    }
}

/**
 * @brief 将单元格值统一转换为字符串形式 :
 *        整形转 int64 后转字符串, 浮点转 double 后转字符串,
 *        布尔转 "0"/"1" , 其余(String/Error)取原始文本
 * @param proxy 单元格值代理对象
 * @return 单元格值对应的字符串
 */
std::string CellValueToString(const OpenXLSX::XLCellValueProxy& proxy)
{
    switch (proxy.type())
    {
    case OpenXLSX::XLValueType::Empty:
        return "";
    case OpenXLSX::XLValueType::Boolean:
        return proxy.get<bool>() ? "1" : "0";
    case OpenXLSX::XLValueType::Integer:
        return std::to_string(proxy.get<int64_t>());
    case OpenXLSX::XLValueType::Float:
        return DoubleToString(proxy.get<double>());
    default:
        return SafeGetCellText(proxy);
    }
}

/**
 * @brief 清洗表头列名 :
 *        非法字符(非大小写字母/数字/汉字/下划线)替换为下划线,
 *        清洗后为空使用 column_列号 , 开头是数字添加前缀 col_
 * @param raw_name 表头单元格原始文本
 * @param column_number 列号(从 1 开始)
 * @return 清洗后的列名
 */
std::string SanitizeColumnName(const std::string& raw_name, uint16_t column_number)
{
    std::string trimmed_name = TrimWhitespace(raw_name);
    if (trimmed_name.empty())
    {
        return kEmptyColumnPrefix + std::to_string(column_number);
    }

    // 合法字符 : 大小写字母/数字/下划线/汉字(汉字为 UTF-8 多字节序列, 高位字节统一视为合法)
    std::string sanitized_name;
    sanitized_name.reserve(trimmed_name.size());
    for (char c : trimmed_name)
    {
        unsigned char byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) || c == '_' || byte >= 0x80)
        {
            sanitized_name.push_back(c);
        }
        else
        {
            sanitized_name.push_back('_');
        }
    }

    // 开头是数字则添加 col_ 前缀
    if (std::isdigit(static_cast<unsigned char>(sanitized_name.front())))
    {
        sanitized_name.insert(0, kDigitStartPrefix);
    }
    return sanitized_name;
}

/**
 * @brief 判断数字文本(可带 +/- 号)的符号与纯数字部分
 * @param text 已去除前后空白的文本
 * @param negative 输出是否为负数
 * @param digits 输出的纯数字部分(保留前导零)
 * @return 文本为合法数字串返回 true, 否则返回 false
 */
bool SplitSignedNumberText(const std::string& text, bool& negative, std::string& digits)
{
    negative = false;
    digits.clear();
    size_t pos = 0;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
    {
        negative = text[pos] == '-';
        ++pos;
    }
    bool has_digit = false;
    for (; pos < text.size(); ++pos)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            return false;
        }
        digits.push_back(text[pos]);
        has_digit = true;
    }
    return has_digit;
}

/**
 * @brief 去掉纯数字串的前导零(全零时保留一个 0)
 * @param digits 纯数字串
 * @return 去除前导零后的数字串
 */
std::string StripLeadingZeros(const std::string& digits)
{
    size_t start = 0;
    while (start + 1 < digits.size() && digits[start] == '0')
    {
        ++start;
    }
    return digits.substr(start);
}

/**
 * @brief 判断整型数字文本是否带前导零(如 "0012", 学号/序号类编码)。
 *        带前导零的数字写入数值列会丢失前导零, 此类列应整列按文本存储
 * @param text 已去除前后空白的整型数字文本
 * @return 带前导零返回 true, 否则返回 false
 */
bool HasLeadingZeroDigits(const std::string& text)
{
    bool negative = false;
    std::string digits;
    if (!SplitSignedNumberText(text, negative, digits))
    {
        return false;
    }
    return StripLeadingZeros(digits).size() < digits.size();
}

/**
 * @brief 判断整型数字文本是否在 int64 范围内(可安全存入 BIGINT)
 * @param text 已去除前后空白的整型数字文本
 * @return 在范围内返回 true, 否则返回 false
 */
bool IsInt64SafeText(const std::string& text)
{
    bool negative = false;
    std::string digits;
    if (!SplitSignedNumberText(text, negative, digits))
    {
        return false;
    }
    digits = StripLeadingZeros(digits);
    // int64 范围: [-9223372036854775808, 9223372036854775807], 均为 19 位数字
    const std::string bound = negative ? "9223372036854775808" : "9223372036854775807";
    if (digits.size() < 19)
    {
        return true;
    }
    if (digits.size() > 19)
    {
        return false;
    }
    // 同位数纯数字串可直接按字典序比较
    return digits <= bound;
}

/**
 * @brief 判断整型数字文本的绝对值是否在 double 可精确表示范围内(±2^53),
 *        超出该范围的整数与浮点混排时不能无损存入 DOUBLE
 * @param text 已去除前后空白的整型数字文本
 * @return 可精确表示返回 true, 否则返回 false
 */
bool IsDoubleSafeIntText(const std::string& text)
{
    bool negative = false;
    std::string digits;
    if (!SplitSignedNumberText(text, negative, digits))
    {
        return false;
    }
    digits = StripLeadingZeros(digits);
    // 2^53 = 9007199254740992(16 位数字)
    const std::string bound = "9007199254740992";
    if (digits.size() < 16)
    {
        return true;
    }
    if (digits.size() > 16)
    {
        return false;
    }
    return digits <= bound;
}

// ISO 规范日期(可含时间部分) : yyyy-mm-dd 或 yyyy-mm-dd-HH:MM:SS
const std::regex kIsoDatePattern(
    R"(^(\d{4})-(\d{2})-(\d{2})(-(\d{2}):(\d{2}):(\d{2}))?$)");

/**
 * @brief 判断是否为 MySQL 可直接解析的 ISO 规范日期(yyyy-mm-dd, 可选 -HH:MM:SS)。
 *        IsDateText 能识别的其余格式(斜杠/点分隔/美式日期等)不能保证 MySQL 正确解析,
 *        含此类日期的列统一按文本存储
 * @param text 已去除前后空白的日期文本
 * @return 是 ISO 规范日期返回 true
 */
bool IsIsoDateText(const std::string& text)
{
    return std::regex_match(text, kIsoDatePattern);
}

/**
 * @brief 列类型证据 : 记录整列逐格扫描后出现的全部内容类别。
 *        列类型决策只依赖"是否出现某类内容", 与出现次数无关(全列验证保证不丢数据)
 */
struct ColumnTypeEvidence
{
    bool has_value = false;                // 是否存在有效(非空/N/A)单元格
    bool has_bool = false;                 // 是否存在布尔内容
    bool has_int = false;                  // 是否存在可安全存入 BIGINT 的整数
    bool has_high_precision_int = false;   // 是否存在超过 double 精确范围的整数(与浮点混排时生效)
    bool has_float = false;                // 是否存在浮点内容
    bool has_iso_date = false;             // 是否存在 ISO 规范日期
    bool has_loose_date = false;           // 是否存在非 ISO 规范日期文本
    bool has_text = false;                 // 是否存在无法归类的文本或需保格式的内容
};

/**
 * @brief 将单个数据单元格计入列类型证据
 * @param evidence 列类型证据
 * @param cell 单元格数据(值与类型, 由 ParseOneCell 产出)
 */
void AccountColumnCell(ColumnTypeEvidence& evidence, const CellData& cell)
{
    if (cell.type == CellType::EMPTY)
    {
        return;
    }
    const std::string text = TrimWhitespace(cell.value);
    if (text.empty() || ToLowerAscii(text) == kNaCellText)
    {
        return;
    }
    evidence.has_value = true;

    // Excel 原生类型直接归类(原生整型/浮点/布尔值一定是规范表示)
    if (cell.type == CellType::BOOLEAN)
    {
        evidence.has_bool = true;
        return;
    }
    if (cell.type == CellType::INTEGER)
    {
        evidence.has_int = true;
        return;
    }
    if (cell.type == CellType::FLOAT)
    {
        evidence.has_float = true;
        return;
    }

    // 文本内容复用文本识别规则二次归类
    switch (DetectTextCellType(text))
    {
    case CellType::BOOLEAN:
        evidence.has_bool = true;
        break;
    case CellType::INTEGER:
        // 前导零(学号/序号等)与超 int64 范围的整数无法无损存入数值列, 整列按文本处理
        if (HasLeadingZeroDigits(text) || !IsInt64SafeText(text))
        {
            evidence.has_text = true;
        }
        else
        {
            evidence.has_int = true;
            if (!IsDoubleSafeIntText(text))
            {
                evidence.has_high_precision_int = true;
            }
        }
        break;
    case CellType::FLOAT:
        evidence.has_float = true;
        break;
    case CellType::DATE:
        if (IsIsoDateText(text))
        {
            evidence.has_iso_date = true;
        }
        else
        {
            evidence.has_loose_date = true;
        }
        break;
    default:
        // STRING : 数字+文字混合、千分位/全角数字等一律按文本保底
        evidence.has_text = true;
        break;
    }
}

/**
 * @brief 根据整列逐格扫描得到的列类型证据确定数据库列类型(TEXT/BIGINT/DOUBLE/BOOLEAN/DATE)。
 *        核心原则 : 列类型必须能无损容纳该列全部单元格, 任一单元格放不下即整列降级为 TEXT,
 *        保证导入时不会因单元格与列类型冲突导致整批回滚(表现为"有表结构但无数据")
 * @param evidence 列类型证据
 * @return 数据库列类型字符串
 */
std::string DecideColumnType(const ColumnTypeEvidence& evidence)
{
    // 无有效单元格(全空/N/A)默认 TEXT
    if (!evidence.has_value)
    {
        return "TEXT";
    }
    // 存在任意文本/需保格式内容, 或存在非 ISO 规范日期 -> TEXT
    if (evidence.has_text || evidence.has_loose_date)
    {
        return "TEXT";
    }
    // 超 double 精确范围的大整数与浮点混排时无法无损存储 -> TEXT
    if (evidence.has_high_precision_int && evidence.has_float)
    {
        return "TEXT";
    }
    // 日期与其他类别混排 -> TEXT(语义冲突); 纯 ISO 日期 -> DATE
    if (evidence.has_iso_date)
    {
        if (evidence.has_bool || evidence.has_int || evidence.has_float)
        {
            return "TEXT";
        }
        return "DATE";
    }
    // 布尔与数值混排 -> TEXT; 纯布尔 -> BOOLEAN
    if (evidence.has_bool)
    {
        if (evidence.has_int || evidence.has_float)
        {
            return "TEXT";
        }
        return "BOOLEAN";
    }
    // 整数与浮点混排 -> DOUBLE(无损容纳全部数值)
    if (evidence.has_int && evidence.has_float)
    {
        return "DOUBLE";
    }
    if (evidence.has_int)
    {
        return "BIGINT";
    }
    if (evidence.has_float)
    {
        return "DOUBLE";
    }
    return "TEXT";
}

/**
 * @brief 解析 worksheet 第一行(表头行)生成列信息 : 列名清洗后填充,
 *        列类型暂为 TEXT, 待数据行解析完成后由 FillColumnTypes 依据整列内容最终确定
 * @param worksheet 工作表对象
 * @param column_count 总列数
 * @return 表头列信息列表
 */
std::vector<ColumnInfo> ParseColumnHeaders(const OpenXLSX::XLWorksheet& worksheet,
                                           uint16_t column_count)
{
    std::vector<ColumnInfo> columns;
    columns.reserve(column_count);
    for (uint16_t column_number = 1; column_number <= column_count; ++column_number)
    {
        ColumnInfo column_info;
        column_info.name = SanitizeColumnName(
            CellValueToString(worksheet.cell(1, column_number).value()), column_number);
        column_info.type = "TEXT";
        columns.push_back(std::move(column_info));
    }
    return columns;
}

/**
 * @brief 依据全部数据行逐格验证各列内容, 填充各列的最终数据库列类型
 * @param columns 表头列信息(由 ParseColumnHeaders 产出, 已含列名)
 * @param rows 全部数据行(由 ParseDataRows 产出)
 */
void FillColumnTypes(std::vector<ColumnInfo>& columns,
                     const std::vector<std::vector<CellData>>& rows)
{
    for (size_t column_index = 0; column_index < columns.size(); ++column_index)
    {
        ColumnTypeEvidence evidence;
        for (const std::vector<CellData>& row : rows)
        {
            if (column_index < row.size())
            {
                AccountColumnCell(evidence, row[column_index]);
            }
        }
        columns[column_index].type = DecideColumnType(evidence);
    }
}


/**
 * @brief 解析单个数据单元格 : 值与类型的转换规则见 CellData 定义,
 *        N/A 与空单元格输出空值(EMPTY)
 * @param proxy 单元格值代理对象
 * @return 单元格结构化数据
 */
CellData ParseOneCell(const OpenXLSX::XLCellValueProxy& proxy)
{
    CellData cell_data;
    switch (proxy.type())
    {
    case OpenXLSX::XLValueType::Empty:
        break;
    case OpenXLSX::XLValueType::Boolean:
        cell_data.value = proxy.get<bool>() ? "1" : "0";
        cell_data.type = CellType::BOOLEAN;
        break;
    case OpenXLSX::XLValueType::Integer:
        cell_data.value = std::to_string(proxy.get<int64_t>());
        cell_data.type = CellType::INTEGER;
        break;
    case OpenXLSX::XLValueType::Float:
        cell_data.value = DoubleToString(proxy.get<double>());
        cell_data.type = CellType::FLOAT;
        break;
    default:
        // String/Error 类型统一输出文本; N/A 输出空值
        std::string text = TrimWhitespace(SafeGetCellText(proxy));
        if (ToLowerAscii(text) == kNaCellText)
        {
            break;
        }
        cell_data.value = text;
        cell_data.type = CellType::STRING;
        break;
    }
    return cell_data;
}

/**
 * @brief 解析 worksheet 中除表头行外的所有数据行
 * @param worksheet 工作表对象
 * @param row_count 总行数
 * @param column_count 总列数
 * @return 数据行列表(每行为按列序排列的单元格数据)
 */
std::vector<std::vector<CellData>> ParseDataRows(const OpenXLSX::XLWorksheet& worksheet,
                                                 uint32_t row_count, uint16_t column_count)
{
    std::vector<std::vector<CellData>> rows;
    rows.reserve(row_count - 1);

    for (uint32_t row_number = 2; row_number <= row_count; ++row_number)
    {
        std::vector<CellData> row_cells;
        row_cells.reserve(column_count);
        for (uint16_t column_number = 1; column_number <= column_count; ++column_number)
        {
            row_cells.push_back(ParseOneCell(worksheet.cell(row_number, column_number).value()));
        }
        rows.push_back(std::move(row_cells));
    }
    return rows;
}

} // namespace

std::vector<std::string> ExcelParse::GetWorksheetNames(const std::string& file_path)
{
    OpenXLSX::XLDocument document;
    try
    {
        document.open(file_path);
        std::vector<std::string> worksheet_names = document.workbook().worksheetNames();
        INFO("解析 Excel 工作表名称完成, file_path: {} , worksheet 个数: {}",
             file_path, worksheet_names.size());
        return worksheet_names;
    }
    catch (const std::exception& e)
    {
        ERR("解析 Excel 工作表名称失败, file_path: {} , 错误信息: {}", file_path, e.what());
        throw ChatExcelException(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED);
    }
}

WorksheetData ExcelParse::ParseWorksheet(const std::string& file_path,
                                         const std::string& worksheet_name)
{
    OpenXLSX::XLDocument document;
    try
    {
        document.open(file_path);
    }
    catch (const std::exception& e)
    {
        ERR("打开 Excel 文件失败, file_path: {} , 错误信息: {}", file_path, e.what());
        throw ChatExcelException(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED);
    }

    try
    {
        OpenXLSX::XLWorkbook workbook = document.workbook();

        // worksheet 名称不存在时报错, 由上层调用者捕获处理
        std::vector<std::string> worksheet_names = workbook.worksheetNames();
        if (std::find(worksheet_names.begin(), worksheet_names.end(), worksheet_name) ==
            worksheet_names.end())
        {
            WARN("worksheet 不存在, file_path: {} , worksheet_name: {}",
                 file_path, worksheet_name);
            throw ChatExcelException(ErrorCode::EXCEL_PARSE_WORKSHEET_NOT_FOUND);
        }

        OpenXLSX::XLWorksheet worksheet = workbook.worksheet(worksheet_name);
        uint32_t row_count = worksheet.rowCount();
        uint16_t column_count = worksheet.columnCount();

        WorksheetData result;
        result.name = worksheet_name;

        // 空 worksheet(0 行或 0 列)不视为异常, 返回空结果
        if (row_count == 0 || column_count == 0)
        {
            INFO("worksheet 为空表, file_path: {} , worksheet_name: {}", file_path, worksheet_name);
            return result;
        }

        result.total_rows = static_cast<int>(row_count);
        result.total_cols = static_cast<int>(column_count);
        result.columns = ParseColumnHeaders(worksheet, column_count);
        result.rows = ParseDataRows(worksheet, row_count, column_count);
        // 列类型须覆盖整列全部单元格(全列验证), 避免少数异常单元格导致整批导入回滚
        FillColumnTypes(result.columns, result.rows);
        INFO("解析 worksheet 完成, file_path: {} , worksheet_name: {} , 行数: {} , 列数: {}",
             file_path, worksheet_name, result.total_rows, result.total_cols);
        return result;
    }
    catch (const ChatExcelException&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        ERR("Excel 解析过程失败, file_path: {} , worksheet_name: {} , 错误信息: {}",
            file_path, worksheet_name, e.what());
        throw ChatExcelException(ErrorCode::EXCEL_PARSE_FAILED);
    }
}

} // namespace excel_parse_service
} // namespace chat_excel
