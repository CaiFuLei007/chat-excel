#pragma once

#include <string>
#include <odb/core.hxx>

namespace chat_excel
{

/**
 * @brief 会话表(tbl_session)的 odb 数据库映射类, 完成会话表字段与 C++ 类成员的映射
 *        建表 SQL 语句以及 CURD 支持代码由 odb 编译器生成
 *        会话 ID 必须唯一; 用户 ID 可以重复, 支持多设备登录同一账号,
 *        不同设备登录后对应不同会话
 */
// odb pragma 指令仅 odb 编译器识别, 使用宏守卫防止普通 C++ 编译器产生未知 pragma 警告
#ifdef ODB_COMPILER
#pragma db object table("tbl_session")
#endif
class SessionEntity
{
public:
    /**
     * @brief 构造函数, 创建会话表映射对象, 用于登录成功后新会话数据的插入
     * @param session_id 会话 ID
     * @param user_id 用户 ID
     */
    SessionEntity(const std::string& session_id, const std::string& user_id);

    /**
     * @brief 获取自增主键 ID
     * @return 自增主键 ID
     */
    unsigned long long Id() const;

    /**
     * @brief 获取会话 ID
     * @return 会话 ID
     */
    const std::string& SessionId() const;

    /**
     * @brief 获取会话所属的用户 ID
     * @return 会话所属的用户 ID
     */
    const std::string& UserId() const;

private:
    // odb 框架通过友元访问私有默认构造函数与私有数据成员
    friend class odb::access;

    // 默认构造函数, 仅供 odb 框架从数据库加载数据时使用
    SessionEntity() = default;

    // 自增主键 ID
#ifdef ODB_COMPILER
#pragma db id auto column("id")
#endif
    unsigned long long id_;

    // 会话 ID, 全系统唯一
#ifdef ODB_COMPILER
#pragma db column("session_id") type("VARCHAR(64)") unique
#endif
    std::string session_id_;

    // 会话所属的用户 ID, 可以重复(多设备登录同一账号)
#ifdef ODB_COMPILER
#pragma db column("user_id") type("VARCHAR(64)")
#endif
    std::string user_id_;
};

} // namespace chat_excel
