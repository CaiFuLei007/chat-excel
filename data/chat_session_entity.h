#pragma once

#include <string>
#include <odb/core.hxx>
#include <odb/nullable.hxx>

namespace chat_excel
{

/**
 * @brief 聊天会话表(tbl_chat_session)的 odb 数据库映射类, 完成聊天会话表字段与 C++ 类成员的映射
 *        一个用户可以拥有多个聊天会话, 每个会话绑定一个模型, Excel 类型的会话可以关联一个文件
 *        type 字段区分会话类型(excel / database), 前端根据会话类型跳转到对应的聊天页面
 *        建表 SQL 语句以及 CURD 支持代码由 odb 编译器生成
 *        chat_session_id 为唯一键, 会话标题 / 最后更新时间 / 总消息数等字段随聊天过程更新, 提供修改接口
 */
// odb pragma 指令仅 odb 编译器识别, 使用宏守卫防止普通 C++ 编译器产生未知 pragma 警告
#ifdef ODB_COMPILER
#pragma db object table("tbl_chat_session")
#endif
class ChatSessionEntity
{
public:
    /**
     * @brief 构造函数, 创建聊天会话表映射对象, 用于新聊天会话数据的插入
     * @param chat_session_id 聊天会话 ID
     * @param user_id 会话所属用户 ID
     * @param title 会话标题, 默认为第一条消息, 最长 20 个字符, 可为空(会话创建后随第一条消息更新)
     * @param create_time 会话创建时间戳
     * @param update_time 会话最后更新时间戳
     * @param total_message_count 会话总消息数
     * @param model_name 会话使用的模型名称
     * @param file_id 会话关联的文件 ID, Excel 类型会话关联上传的 Excel 文件, 可为空(尚未关联文件)
     * @param type 会话类型(excel / database)
     * @param connection_info 数据库连接信息, 仅 database 类型会话使用, Excel 类型会话为空
     */
    ChatSessionEntity(const std::string& chat_session_id, const std::string& user_id,
                      const odb::nullable<std::string>& title, unsigned long long create_time,
                      unsigned long long update_time, unsigned long long total_message_count,
                      const std::string& model_name, const odb::nullable<std::string>& file_id,
                      const std::string& type, const odb::nullable<std::string>& connection_info);

    /**
     * @brief 获取自增主键 ID
     * @return 自增主键 ID
     */
    unsigned long long Id() const;

    /**
     * @brief 获取聊天会话 ID
     * @return 聊天会话 ID
     */
    const std::string& ChatSessionId() const;

    /**
     * @brief 获取会话所属用户 ID
     * @return 会话所属用户 ID
     */
    const std::string& UserId() const;

    /**
     * @brief 获取会话标题
     * @return 会话标题, 可为空(会话创建后随第一条消息更新)
     */
    const odb::nullable<std::string>& Title() const;

    /**
     * @brief 获取会话创建时间戳
     * @return 会话创建时间戳
     */
    unsigned long long CreateTime() const;

    /**
     * @brief 获取会话最后更新时间戳
     * @return 会话最后更新时间戳
     */
    unsigned long long UpdateTime() const;

    /**
     * @brief 获取会话总消息数
     * @return 会话总消息数
     */
    unsigned long long TotalMessageCount() const;

    /**
     * @brief 获取会话使用的模型名称
     * @return 会话使用的模型名称
     */
    const std::string& ModelName() const;

    /**
     * @brief 获取会话关联的文件 ID
     * @return 会话关联的文件 ID, 可为空(尚未关联文件)
     */
    const odb::nullable<std::string>& FileId() const;

    /**
     * @brief 获取会话类型
     * @return 会话类型(excel / database)
     */
    const std::string& Type() const;

    /**
     * @brief 获取数据库连接信息
     * @return 数据库连接信息, 可为空(仅 database 类型会话使用)
     */
    const odb::nullable<std::string>& ConnectionInfo() const;

    /**
     * @brief 设置会话标题
     * @param title 会话标题
     */
    void SetTitle(const odb::nullable<std::string>& title);

    /**
     * @brief 设置会话最后更新时间戳
     * @param update_time 会话最后更新时间戳
     */
    void SetUpdateTime(unsigned long long update_time);

    /**
     * @brief 设置会话总消息数
     * @param total_message_count 会话总消息数
     */
    void SetTotalMessageCount(unsigned long long total_message_count);

    /**
     * @brief 设置会话关联的文件 ID
     * @param file_id 会话关联的文件 ID, 空值表示会话尚未关联文件
     */
    void SetFileId(const odb::nullable<std::string>& file_id);

    /**
     * @brief 设置数据库连接信息
     * @param connection_info 数据库连接信息, 空值表示会话不使用数据库连接信息
     */
    void SetConnectionInfo(const odb::nullable<std::string>& connection_info);

private:
    // odb 框架通过友元访问私有默认构造函数与私有数据成员
    friend class odb::access;

    // 默认构造函数, 仅供 odb 框架从数据库加载数据时使用
    ChatSessionEntity() = default;

    // 自增主键 ID
#ifdef ODB_COMPILER
#pragma db id auto column("id")
#endif
    unsigned long long id_;

    // 聊天会话 ID, 全系统唯一
    // 会话 ID 由 ChatSDK 生成, 格式为 {user_id}_{微秒时间戳}, 长度约 53 字符, 列宽取 64
    // unique 约束自动创建唯一索引, 保证字段唯一性的同时支持快速查找会话信息
#ifdef ODB_COMPILER
#pragma db column("chat_session_id") type("VARCHAR(64)") unique
#endif
    std::string chat_session_id_;

    // 会话所属用户 ID, 普通索引用于快速查找用户的聊天会话列表
    // 用户 ID 为 36 字符的 UUIDv4(带连字符), 列宽取 64
#ifdef ODB_COMPILER
#pragma db column("user_id") type("VARCHAR(64)") not_null
#pragma db index
#endif
    std::string user_id_;

    // 会话标题, 默认为第一条消息, 最长 20 个字符, 可为空(会话创建后随第一条消息更新)
#ifdef ODB_COMPILER
#pragma db column("title") type("TEXT")
#endif
    odb::nullable<std::string> title_;

    // 会话创建时间戳
#ifdef ODB_COMPILER
#pragma db column("create_time") type("BIGINT") not_null
#endif
    unsigned long long create_time_;

    // 会话最后更新时间戳, 随消息收发更新
#ifdef ODB_COMPILER
#pragma db column("update_time") type("BIGINT") not_null
#endif
    unsigned long long update_time_;

    // 会话总消息数, 随消息收发更新
#ifdef ODB_COMPILER
#pragma db column("total_message_count") type("BIGINT") not_null
#endif
    unsigned long long total_message_count_;

    // 会话使用的模型名称
#ifdef ODB_COMPILER
#pragma db column("model_name") type("VARCHAR(30)") not_null
#endif
    std::string model_name_;

    // 会话关联的文件 ID, Excel 类型会话关联上传的 Excel 文件, 可为空(尚未关联文件)
    // 文件 ID 为 36 字符的 UUIDv4(带连字符), 列宽取 64, 与 tbl_file_info.file_id 保持一致
#ifdef ODB_COMPILER
#pragma db column("file_id") type("VARCHAR(64)")
#endif
    odb::nullable<std::string> file_id_;

    // 会话类型(excel / database), 前端根据会话类型跳转到对应的聊天页面
#ifdef ODB_COMPILER
#pragma db column("type") type("VARCHAR(20)") not_null
#endif
    std::string type_;

    // 数据库连接信息, 仅 database 类型会话使用, Excel 类型会话为空
#ifdef ODB_COMPILER
#pragma db column("connection_info") type("TEXT")
#endif
    odb::nullable<std::string> connection_info_;
};

} // namespace chat_excel
