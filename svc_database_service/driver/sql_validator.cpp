#include "svc_database_service/driver/sql_validator.h"

#include <cctype>
#include <unordered_set>

namespace chat_excel
{

namespace
{

// SQL 扫描状态机的状态
enum class SqlScanState
{
    NORMAL,            // 普通状态
    IN_SINGLE_QUOTE,   // 单引号字符串内部
    IN_DOUBLE_QUOTE,   // 双引号字符串内部
    IN_BACKTICK,       // 反引号标识符内部
    IN_LINE_COMMENT,   // 单行注释内部
    IN_BLOCK_COMMENT,  // 多行注释内部
};

// 危险关键字列表(统一为大写), 命中任意一个即判定包含危险操作
const std::vector<std::string> kDangerousKeywords = {
    "DROP DATABASE", "DROP TABLE", "TRUNCATE", "DELETE FROM",
    "EXEC", "EXECUTE", "SCRIPT", "JAVASCRIPT", "EVAL",
    "UNION ALL SELECT", "1=1", "OR 1=1", "OR '1'='1'",
    "SLEEP(", "BENCHMARK(", "LOAD_FILE(", "INTO OUTFILE", "INTO DUMPFILE",
};

// 事务控制语句前缀(统一为大写), 用于识别事务语句
const std::vector<std::string> kTransactionPrefixes = {
    "BEGIN", "START TRANSACTION", "COMMIT", "ROLLBACK", "SAVEPOINT", "RELEASE",
};

// SQL 关键字集合, 用于表名提取时排除关键字
const std::unordered_set<std::string> kSqlKeywords = {
    "SELECT", "FROM", "WHERE", "GROUP", "BY", "HAVING", "ORDER", "LIMIT", "OFFSET",
    "UNION", "ALL", "DISTINCT", "AS", "ON", "INNER", "LEFT", "RIGHT", "FULL",
    "OUTER", "CROSS", "JOIN", "USING", "AND", "OR", "NOT", "IN", "EXISTS",
    "BETWEEN", "LIKE", "IS", "NULL", "INTO", "VALUES", "VALUE", "SET",
    "INSERT", "UPDATE", "DELETE", "CREATE", "DROP", "ALTER", "TABLE",
    "IF", "TRUNCATE", "REPLACE", "DESC", "ASC", "SHOW", "DATABASE", "SCHEMA",
    "INDEX", "KEY", "PRIMARY", "FOREIGN", "CONSTRAINT", "DEFAULT", "FOR",
};

// SQL 词法单元结构
struct SqlToken
{
    // 词法单元内容(引号包裹的内容已去除引号)
    std::string text;

    // 是否为字符串字面量(单引号包裹)
    bool is_string_literal = false;
};

/**
 * @brief 判断字符是否属于标识符字符 : 字母, 数字, 下划线, 连接符, 点, 汉字等多字节字符
 * @param ch 待判断的字符
 * @return 属于标识符字符返回 true, 否则返回 false
 */
bool IsIdentifierChar(unsigned char ch)
{
    return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.' || ch >= 0x80;
}

/**
 * @brief 字符串大写转换(仅转换 ASCII 字母, 汉字等多字节字符保持不变)
 * @param text 原始字符串
 * @return 大写转换后的字符串
 */
std::string ToUpperAscii(std::string text)
{
    for (auto& ch : text)
    {
        if (ch >= 'a' && ch <= 'z')
        {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return text;
}

/**
 * @brief 规范化空白 : 将连续的空白字符合并为单个空格,
 *        避免危险关键字匹配时因空白差异(如 "OR    1=1")而漏检
 * @param text 原始字符串
 * @return 合并连续空白后的字符串
 */
std::string CollapseWhitespace(std::string text)
{
    std::string result;
    result.reserve(text.size());

    // 上一个字符是否为空白字符
    bool previous_is_space = false;
    for (char ch : text)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f')
        {
            if (!previous_is_space)
            {
                result += ' ';
            }
            previous_is_space = true;
        }
        else
        {
            result += ch;
            previous_is_space = false;
        }
    }
    return result;
}

/**
 * @brief 校验标识符主体的有效性 : 不为空, 不超过最大长度限制; 只能包含数字, 字母,
 *        下划线, 汉字等多字节字符, 以及可选的连接符(-)与点(.);
 *        连接符与点不能连续使用; 数字是否允许开头由参数控制
 * @param name 标识符名称
 * @param allow_special_char 是否允许包含特殊字符(连接符与点)
 * @param allow_digit_first 是否允许数字开头(引号包裹的标识符允许数字开头)
 * @return 标识符有效返回 true, 否则返回 false
 */
bool CheckIdentifierBody(const std::string& name, bool allow_special_char, bool allow_digit_first)
{
    if (name.empty() || name.size() > kMaxIdentifierLength)
    {
        return false;
    }

    // 上一个字符是否为连接符或点, 用于判断连接符/点连续使用的情况
    bool previous_is_special = false;
    for (size_t i = 0; i < name.size(); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(name[i]);
        if (std::isdigit(ch) != 0)
        {
            // 数字不能作为标识符开头
            if (i == 0 && !allow_digit_first)
            {
                return false;
            }
            previous_is_special = false;
        }
        else if (std::isalpha(ch) != 0 || ch == '_')
        {
            previous_is_special = false;
        }
        else if (ch == '-' || ch == '.')
        {
            // 未使用引号包裹时不允许包含特殊字符
            if (!allow_special_char)
            {
                return false;
            }
            // 连接符与点不能连续使用
            if (previous_is_special)
            {
                return false;
            }
            previous_is_special = true;
        }
        else if (ch >= 0x80)
        {
            // UTF-8 多字节字符(汉字等)
            previous_is_special = false;
        }
        else
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 获取规范化语句的首个关键字(取语句开头连续的 ASCII 字母并大写化)
 * @param normalized_sql 规范化后的 SQL 语句
 * @return 首个关键字, 不存在时返回空字符串
 */
std::string GetFirstKeyword(const std::string& normalized_sql)
{
    std::string keyword;
    for (char ch : normalized_sql)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
        {
            keyword += ch;
        }
        else
        {
            break;
        }
    }
    return ToUpperAscii(keyword);
}

/**
 * @brief 将 SQL 语句切分为词法单元 : 引号包裹的内容(去除引号)作为一个单元,
 *        标识符/关键字/数字作为一个单元, 其余符号作为单字符单元
 * @param sql 规范化后的 SQL 语句
 * @return 词法单元列表
 */
std::vector<SqlToken> TokenizeSql(const std::string& sql)
{
    std::vector<SqlToken> tokens;
    tokens.reserve(sql.size() / 4 + 1);

    size_t i = 0;
    while (i < sql.size())
    {
        char ch = sql[i];
        // 跳过空白字符
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f')
        {
            ++i;
            continue;
        }
        // 引号包裹的内容作为一个词法单元, 内容去除引号
        if (ch == '\'' || ch == '"' || ch == '`')
        {
            char quote = ch;
            ++i;
            std::string content;
            while (i < sql.size())
            {
                // 反斜杠转义(反引号标识符中不存在反斜杠转义)
                if (sql[i] == '\\' && quote != '`' && i + 1 < sql.size())
                {
                    content += sql[i + 1];
                    i += 2;
                    continue;
                }
                if (sql[i] == quote)
                {
                    // 双写引号为内容内部的转义引号, 继续收集内容
                    if (i + 1 < sql.size() && sql[i + 1] == quote)
                    {
                        content += quote;
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                content += sql[i];
                ++i;
            }
            tokens.push_back(SqlToken{content, quote == '\''});
        }
        else if (IsIdentifierChar(static_cast<unsigned char>(ch)))
        {
            // 标识符/关键字/数字作为一个词法单元(连接符与点并入, 支持 库名.表名 形式)
            std::string word;
            while (i < sql.size() && IsIdentifierChar(static_cast<unsigned char>(sql[i])))
            {
                word += sql[i];
                ++i;
            }
            tokens.push_back(SqlToken{word, false});
        }
        else
        {
            // 其余符号(括号, 逗号, 分号, 运算符等)作为单字符词法单元
            tokens.push_back(SqlToken{std::string(1, ch), false});
            ++i;
        }
    }
    return tokens;
}

/**
 * @brief 判断词法单元是否可以作为表名 : 非字符串字面量, 非关键字,
 *        且以标识符字符开头(排除括号, 逗号, 运算符等符号)
 * @param token 词法单元
 * @return 可以作为表名返回 true, 否则返回 false
 */
bool IsTableNameToken(const SqlToken& token)
{
    if (token.is_string_literal || token.text.empty())
    {
        return false;
    }
    // 关键字不能作为表名
    if (kSqlKeywords.find(ToUpperAscii(token.text)) != kSqlKeywords.end())
    {
        return false;
    }
    unsigned char first = static_cast<unsigned char>(token.text[0]);
    return std::isalpha(first) != 0 || std::isdigit(first) != 0 ||
           first == '_' || first >= 0x80;
}

/**
 * @brief 向表名列表中追加表名, 已存在的表名不重复追加(按出现顺序去重)
 * @param table_names 表名列表
 * @param table_name 待追加的表名
 */
void AddTableName(std::vector<std::string>& table_names, std::string table_name)
{
    for (const auto& existing : table_names)
    {
        if (existing == table_name)
        {
            return;
        }
    }
    table_names.push_back(std::move(table_name));
}

} // namespace

SqlType SQLValidator::GetSqlType(const std::string& sql)
{
    std::string first_keyword = GetFirstKeyword(NormalizeSql(sql));
    if (first_keyword == "SELECT")
    {
        return SqlType::SELECT;
    }
    else if (first_keyword == "SHOW")
    {
        return SqlType::SHOW;
    }
    else if (first_keyword == "DESC" || first_keyword == "DESCRIBE")
    {
        return SqlType::DESC;
    }
    else if (first_keyword == "PRAGMA")
    {
        return SqlType::PRAGMA;
    }
    else if (first_keyword == "INSERT")
    {
        return SqlType::INSERT;
    }
    else if (first_keyword == "UPDATE")
    {
        return SqlType::UPDATE;
    }
    else if (first_keyword == "DELETE")
    {
        return SqlType::DELETE;
    }
    else if (first_keyword == "REPLACE")
    {
        return SqlType::REPLACE;
    }
    else if (first_keyword == "TRUNCATE")
    {
        return SqlType::TRUNCATE;
    }
    else if (first_keyword == "CREATE")
    {
        return SqlType::CREATE;
    }
    else if (first_keyword == "DROP")
    {
        return SqlType::DROP;
    }
    else if (first_keyword == "ALTER")
    {
        return SqlType::ALTER;
    }
    return SqlType::UNKNOWN;
}

bool SQLValidator::IsReadOnlySql(const std::string& sql)
{
    switch (GetSqlType(sql))
    {
    case SqlType::SELECT:
    case SqlType::SHOW:
    case SqlType::DESC:
    case SqlType::PRAGMA:
        return true;
    default:
        return false;
    }
}

bool SQLValidator::IsModifySql(const std::string& sql)
{
    switch (GetSqlType(sql))
    {
    case SqlType::INSERT:
    case SqlType::UPDATE:
    case SqlType::DELETE:
    case SqlType::REPLACE:
    case SqlType::TRUNCATE:
    case SqlType::CREATE:
    case SqlType::DROP:
    case SqlType::ALTER:
        return true;
    default:
        return false;
    }
}

bool SQLValidator::IsValidTableName(const std::string& table_name)
{
    // 表名允许包含连接符与点(支持 库名.表名 形式), 但数字不能开头
    return CheckIdentifierBody(table_name, true, false);
}

bool SQLValidator::IsValidColumnName(const std::string& column_name)
{
    // 单字符不可能是引号包裹形式, 直接按未包裹校验
    if (column_name.size() < 2)
    {
        return CheckIdentifierBody(column_name, false, false);
    }

    char first = column_name.front();
    char last = column_name.back();
    // 引号包裹形式 : 去除引号后校验内部内容, 内部允许包含特殊字符且允许数字开头
    if ((first == '\'' || first == '"' || first == '`') && first == last)
    {
        return CheckIdentifierBody(column_name.substr(1, column_name.size() - 2), true, true);
    }
    // 未使用引号包裹 : 不允许包含特殊字符(连接符与点)
    return CheckIdentifierBody(column_name, false, false);
}

bool SQLValidator::ContainsDangerousOperation(const std::string& sql)
{
    // 规范化 + 大写化 + 合并连续空白后进行危险关键字匹配
    std::string text = CollapseWhitespace(ToUpperAscii(NormalizeSql(sql)));
    for (const auto& keyword : kDangerousKeywords)
    {
        if (text.find(keyword) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool SQLValidator::ContainsMultipleStatements(const std::string& sql)
{
    std::string normalized = NormalizeSql(sql);
    // 移除末尾单个分号(末尾分号为语句结束符, 不参与多语句判定)
    if (!normalized.empty() && normalized.back() == ';')
    {
        normalized.pop_back();
    }

    SqlScanState state = SqlScanState::NORMAL;
    for (size_t i = 0; i < normalized.size(); ++i)
    {
        char current = normalized[i];
        switch (state)
        {
        case SqlScanState::NORMAL:
            if (current == '\'')
            {
                state = SqlScanState::IN_SINGLE_QUOTE;
            }
            else if (current == '"')
            {
                state = SqlScanState::IN_DOUBLE_QUOTE;
            }
            else if (current == '`')
            {
                state = SqlScanState::IN_BACKTICK;
            }
            else if (current == ';')
            {
                // 引号外出现分号即包含多条 SQL 语句
                return true;
            }
            break;
        case SqlScanState::IN_SINGLE_QUOTE:
            // 反斜杠转义 : 跳过被转义的字符
            if (current == '\\' && i + 1 < normalized.size())
            {
                ++i;
            }
            else if (current == '\'')
            {
                // 双写引号为字符串内部的转义引号
                if (i + 1 < normalized.size() && normalized[i + 1] == '\'')
                {
                    ++i;
                }
                else
                {
                    state = SqlScanState::NORMAL;
                }
            }
            break;
        case SqlScanState::IN_DOUBLE_QUOTE:
            if (current == '\\' && i + 1 < normalized.size())
            {
                ++i;
            }
            else if (current == '"')
            {
                if (i + 1 < normalized.size() && normalized[i + 1] == '"')
                {
                    ++i;
                }
                else
                {
                    state = SqlScanState::NORMAL;
                }
            }
            break;
        case SqlScanState::IN_BACKTICK:
            if (current == '`')
            {
                if (i + 1 < normalized.size() && normalized[i + 1] == '`')
                {
                    ++i;
                }
                else
                {
                    state = SqlScanState::NORMAL;
                }
            }
            break;
        default:
            break;
        }
    }
    return false;
}

std::vector<std::string> SQLValidator::ExtractTableNames(const std::string& sql)
{
    std::vector<SqlToken> tokens = TokenizeSql(NormalizeSql(sql));
    std::vector<std::string> table_names;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        // 字符串字面量不参与表名提取
        if (tokens[i].is_string_literal)
        {
            continue;
        }

        std::string upper_token = ToUpperAscii(tokens[i].text);
        // 仅处理携带表名的关键字
        if (upper_token != "FROM" && upper_token != "INTO" && upper_token != "UPDATE" &&
            upper_token != "JOIN" && upper_token != "TABLE")
        {
            continue;
        }

        size_t cursor = i + 1;
        // TABLE 关键字后跳过 IF [NOT] EXISTS(如 DROP TABLE IF EXISTS)
        if (upper_token == "TABLE")
        {
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "IF")
            {
                ++cursor;
            }
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "NOT")
            {
                ++cursor;
            }
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "EXISTS")
            {
                ++cursor;
            }
        }

        if (upper_token == "FROM")
        {
            // FROM 子句 : 支持逗号分隔的多表查询与表别名
            while (cursor < tokens.size() && IsTableNameToken(tokens[cursor]))
            {
                AddTableName(table_names, tokens[cursor].text);
                ++cursor;
                // 跳过可选的 AS 关键字(表别名前缀)
                if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "AS")
                {
                    ++cursor;
                }
                // 逗号分隔的多表查询, 继续收集下一张表
                if (cursor < tokens.size() && tokens[cursor].text == ",")
                {
                    ++cursor;
                    continue;
                }
                // 此处的标识符为无 AS 前缀的表别名, 跳过后结束收集
                if (cursor < tokens.size() && IsTableNameToken(tokens[cursor]))
                {
                    ++cursor;
                    // 别名之后仍可能存在逗号分隔的下一张表(如 FROM t1 alias, t2)
                    if (cursor < tokens.size() && tokens[cursor].text == ",")
                    {
                        ++cursor;
                        continue;
                    }
                }
                break;
            }
        }
        else if (upper_token == "TABLE")
        {
            // TABLE 关键字 : 支持逗号分隔的多表(CREATE/DROP/ALTER/TRUNCATE TABLE t1, t2)
            while (cursor < tokens.size() && IsTableNameToken(tokens[cursor]))
            {
                AddTableName(table_names, tokens[cursor].text);
                ++cursor;
                if (cursor < tokens.size() && tokens[cursor].text == ",")
                {
                    ++cursor;
                    continue;
                }
                break;
            }
        }
        else
        {
            // INTO/UPDATE/JOIN 关键字 : 仅携带单个表名
            if (cursor < tokens.size() && IsTableNameToken(tokens[cursor]))
            {
                AddTableName(table_names, tokens[cursor].text);
            }
        }
    }
    return table_names;
}

std::vector<std::string> SQLValidator::ExtractModifyTargetTableNames(const std::string& sql)
{
    std::string normalized = NormalizeSql(sql);
    // DELETE 语句中 FROM 后的表为修改目标表, 其余语句中 FROM 后的表为只读数据源表
    const bool delete_from_is_target = GetFirstKeyword(normalized) == "DELETE";

    std::vector<SqlToken> tokens = TokenizeSql(normalized);
    std::vector<std::string> table_names;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        // 字符串字面量不参与表名提取
        if (tokens[i].is_string_literal)
        {
            continue;
        }

        std::string upper_token = ToUpperAscii(tokens[i].text);
        // 仅处理携带修改目标表名的关键字 : INSERT/REPLACE INTO, UPDATE,
        // DELETE FROM, CREATE/ALTER/TRUNCATE/DROP TABLE
        const bool is_target_keyword = upper_token == "INTO" || upper_token == "UPDATE" ||
                                       upper_token == "TABLE" ||
                                       (upper_token == "FROM" && delete_from_is_target);
        if (!is_target_keyword)
        {
            continue;
        }

        size_t cursor = i + 1;
        // TABLE 关键字后跳过 IF [NOT] EXISTS(如 DROP TABLE IF EXISTS)
        if (upper_token == "TABLE")
        {
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "IF")
            {
                ++cursor;
            }
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "NOT")
            {
                ++cursor;
            }
            if (cursor < tokens.size() && ToUpperAscii(tokens[cursor].text) == "EXISTS")
            {
                ++cursor;
            }
        }

        // INTO/UPDATE/FROM 关键字仅携带单个目标表名,
        // TABLE 关键字支持逗号分隔的多表(CREATE/ALTER/TRUNCATE TABLE t1, t2)
        while (cursor < tokens.size() && IsTableNameToken(tokens[cursor]))
        {
            AddTableName(table_names, tokens[cursor].text);
            ++cursor;
            if (upper_token == "TABLE" && cursor < tokens.size() && tokens[cursor].text == ",")
            {
                ++cursor;
                continue;
            }
            break;
        }
    }
    return table_names;
}

std::string SQLValidator::TrimSql(const std::string& sql)
{
    constexpr const char* kWhitespaceCharacters = " \t\n\r\v\f";
    size_t begin = sql.find_first_not_of(kWhitespaceCharacters);
    if (begin == std::string::npos)
    {
        return "";
    }
    size_t end = sql.find_last_not_of(kWhitespaceCharacters);
    return sql.substr(begin, end - begin + 1);
}

std::string SQLValidator::RemoveComments(const std::string& sql)
{
    std::string result;
    result.reserve(sql.size());

    SqlScanState state = SqlScanState::NORMAL;
    for (size_t i = 0; i < sql.size(); ++i)
    {
        char current = sql[i];
        char next = i + 1 < sql.size() ? sql[i + 1] : '\0';

        switch (state)
        {
        case SqlScanState::NORMAL:
            if (current == '\'')
            {
                state = SqlScanState::IN_SINGLE_QUOTE;
                result += current;
            }
            else if (current == '"')
            {
                state = SqlScanState::IN_DOUBLE_QUOTE;
                result += current;
            }
            else if (current == '`')
            {
                state = SqlScanState::IN_BACKTICK;
                result += current;
            }
            else if (current == '/' && next == '/' && (i == 0 || sql[i - 1] != '\\'))
            {
                // 单行注释开始, 注释符与注释内容均不保留
                state = SqlScanState::IN_LINE_COMMENT;
                ++i;
                // 注释移除后补一个空格, 避免前后词法单元粘连
                result += ' ';
            }
            else if (current == '/' && next == '*' && (i == 0 || sql[i - 1] != '\\'))
            {
                // 多行注释开始, 注释符与注释内容均不保留
                state = SqlScanState::IN_BLOCK_COMMENT;
                ++i;
                result += ' ';
            }
            else
            {
                result += current;
            }
            break;
        case SqlScanState::IN_SINGLE_QUOTE:
        case SqlScanState::IN_DOUBLE_QUOTE:
            result += current;
            if (current == '\\' && i + 1 < sql.size())
            {
                // 反斜杠转义 : 转义字符与被转义字符均保留
                result += next;
                ++i;
            }
            else if (current == '\'' || current == '"')
            {
                if (next == current)
                {
                    // 双写引号为字符串内部的转义引号, 仍是字符串内容
                    result += next;
                    ++i;
                }
                else
                {
                    state = SqlScanState::NORMAL;
                }
            }
            break;
        case SqlScanState::IN_BACKTICK:
            result += current;
            if (current == '`')
            {
                if (next == '`')
                {
                    // 双写反引号为标识符内部的转义反引号
                    result += next;
                    ++i;
                }
                else
                {
                    state = SqlScanState::NORMAL;
                }
            }
            break;
        case SqlScanState::IN_LINE_COMMENT:
            // 单行注释内容跳过, 保留换行符维持语句结构
            if (current == '\n')
            {
                result += current;
                state = SqlScanState::NORMAL;
            }
            break;
        case SqlScanState::IN_BLOCK_COMMENT:
            // 多行注释内容跳过, 遇到注释结束符退出注释状态
            if (current == '*' && next == '/')
            {
                state = SqlScanState::NORMAL;
                ++i;
            }
            break;
        }
    }
    return result;
}

std::string SQLValidator::NormalizeSql(const std::string& sql)
{
    return TrimSql(RemoveComments(sql));
}

bool SQLValidator::IsTransactionStatement(const std::string& sql)
{
    std::string normalized = CollapseWhitespace(ToUpperAscii(NormalizeSql(sql)));
    for (const auto& prefix : kTransactionPrefixes)
    {
        // 完整单词匹配, 避免误匹配以此为前缀的其他单词(如 COMMITTED)
        if (normalized.size() >= prefix.size() &&
            normalized.compare(0, prefix.size(), prefix) == 0 &&
            (normalized.size() == prefix.size() || normalized[prefix.size()] == ' '))
        {
            return true;
        }
    }
    return false;
}

bool SQLValidator::IsValidSql(const std::string& sql)
{
    // 有效的 SQL 语句 : 为支持的 SQL 类型或事务控制语句 + 不包含危险操作 + 不包含多条语句
    return (GetSqlType(sql) != SqlType::UNKNOWN || IsTransactionStatement(sql)) &&
           !ContainsDangerousOperation(sql) &&
           !ContainsMultipleStatements(sql);
}

} // namespace chat_excel
