#pragma once

#include <string>
#include <vector>

#include "svc_database_service/driver/database_schema.h"

namespace chat_excel
{

/**
 * @brief SQL 校验器, 负责规范化 SQL 语句, 验证 SQL 语句的合法性,
 *        防止 SQL 注入, 检测 SQL 类型(只读或修改), 全部接口为静态方法
 */
class SQLValidator
{
public:
    // 纯静态工具类, 禁止实例化
    SQLValidator() = delete;

    /**
     * @brief 获取 SQL 语句类型, 内部先规范化语句再按首个关键字判断
     * @param sql 原始 SQL 语句
     * @return SQL 语句类型, 不支持的语句返回 SqlType::UNKNOWN
     */
    static SqlType GetSqlType(const std::string& sql);

    /**
     * @brief 判断是否属于只读类 SQL 语句(SELECT/SHOW/DESC)
     * @param sql 原始 SQL 语句
     * @return 属于只读类语句返回 true, 否则返回 false
     */
    static bool IsReadOnlySql(const std::string& sql);

    /**
     * @brief 判断是否属于修改类 SQL 语句(INSERT/UPDATE/DELETE/REPLACE/TRUNCATE/CREATE/DROP/ALTER)
     * @param sql 原始 SQL 语句
     * @return 属于修改类语句返回 true, 否则返回 false
     */
    static bool IsModifySql(const std::string& sql);

    /**
     * @brief 判断是否为有效的表名 : 不为空; 只能包含数字(不能开头), 字母, 下划线,
     *        汉字, 连接符(-), 点(.); 连接符与点不能连续使用; 不超过最大长度限制
     * @param table_name 表名
     * @return 表名有效返回 true, 否则返回 false
     */
    static bool IsValidTableName(const std::string& table_name);

    /**
     * @brief 判断是否为有效的列名 : 校验规则同表名, 但包含特殊字符(连接符, 点)时
     *        必须使用引号(单引号/双引号/反引号)包裹
     * @param column_name 列名
     * @return 列名有效返回 true, 否则返回 false
     */
    static bool IsValidColumnName(const std::string& column_name);

    /**
     * @brief 将标识符按字符数截断到最大长度限制, 超长时截断; 长度按 UTF-8
     *        字符统计(汉字等多字节字符按 1 个字符计), 截断位置保证不会切断多字节字符
     * @param identifier 原始标识符(表名/列名等)
     * @param max_char_count 截断后允许的最大字符数
     * @return 未超长返回原字符串, 超长返回按字符截断后的字符串
     */
    static std::string TruncateIdentifier(const std::string& identifier,
                                           size_t max_char_count);

    /**
     * @brief 判断是否包含危险操作, 以完整词形式匹配危险关键字列表(大小写不敏感,
     *        匹配前会移除注释, 遮蔽引号内容并合并连续空白); 词边界校验确保命中的
     *        是关键字本身, 引号内的表名/字段名/字符串数据不会误判
     * @param sql 原始 SQL 语句
     * @return 包含危险操作返回 true, 否则返回 false
     */
    static bool ContainsDangerousOperation(const std::string& sql);

    /**
     * @brief 判断是否包含多条 SQL 语句, 通过移除末尾分号后统计字符串与注释之外
     *        的分号个数来确定
     * @param sql 原始 SQL 语句
     * @return 包含多条语句返回 true, 否则返回 false
     */
    static bool ContainsMultipleStatements(const std::string& sql);

    /**
     * @brief 从 SQL 语句中获取要操作的表名称, 覆盖 SELECT/INSERT/UPDATE/DELETE/
     *        CREATE/DROP/ALTER/TRUNCATE/JOIN 等常见 CRUD 场景
     * @param sql 原始 SQL 语句
     * @return 表名列表(已去除引号包裹, 按出现顺序去重), 未识别到表名时返回空列表
     */
    static std::vector<std::string> ExtractTableNames(const std::string& sql);

    /**
     * @brief 从修改类 SQL 语句中提取将要修改的目标表名, 只读引用的数据源表
     *        (SELECT/JOIN 子句中的表)不会被提取; 覆盖 INSERT/REPLACE INTO,
     *        UPDATE, DELETE FROM, CREATE/ALTER/TRUNCATE TABLE 等场景
     * @param sql 原始 SQL 语句
     * @return 修改目标表名列表(已去除引号包裹, 按出现顺序去重), 未识别到目标表时返回空列表
     */
    static std::vector<std::string> ExtractModifyTargetTableNames(const std::string& sql);

    /**
     * @brief 删除 SQL 语句前后的空白字符(空格/制表符/换行符等)
     * @param sql 原始 SQL 语句
     * @return 去除前后空白后的 SQL 语句
     */
    static std::string TrimSql(const std::string& sql);

    /**
     * @brief 移除 SQL 语句中的注释, 支持单行注释(//)与多行注释(星号斜杠),
     *        引号内的注释符与被转义字符前的注释符不视为注释
     * @param sql 原始 SQL 语句
     * @return 移除注释后的 SQL 语句
     */
    static std::string RemoveComments(const std::string& sql);

    /**
     * @brief 规范化 SQL 语句 : 移除注释 + 删除前后空白
     * @param sql 原始 SQL 语句
     * @return 规范化后的 SQL 语句
     */
    static std::string NormalizeSql(const std::string& sql);

    /**
     * @brief 判断是否属于事务控制语句(BEGIN/START TRANSACTION/COMMIT/ROLLBACK/
     *        SAVEPOINT/RELEASE), 事务语句走修改类 SQL 通路执行
     * @param sql 原始 SQL 语句
     * @return 属于事务控制语句返回 true, 否则返回 false
     */
    static bool IsTransactionStatement(const std::string& sql);

    /**
     * @brief 判断是否属于有效的 SQL 语句 : 为支持的 SQL 类型(或事务控制语句),
     *        不包含危险操作, 不包含多条 SQL 语句
     * @param sql 原始 SQL 语句
     * @return SQL 语句有效返回 true, 否则返回 false
     */
    static bool IsValidSql(const std::string& sql);
};

} // namespace chat_excel
