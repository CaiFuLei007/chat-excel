#pragma once

#include <string>
#include <odb/core.hxx>

namespace chat_excel
{

/**
 * @brief 用户登录状态枚举, 对应用户表 status 字段
 */
enum class UserStatus : unsigned char
{
    // 未登录
    NOT_LOGGED_IN = 0,

    // 已登录
    LOGGED_IN = 1
};

/**
 * @brief 用户表(tbl_user)的 odb 数据库映射类, 完成用户表字段与 C++ 类成员的映射
 *        建表 SQL 语句以及 CURD 支持代码由 odb 编译器生成
 *        密码字段存储加密后的结果, 不存储明文密码
 */
// odb pragma 指令仅 odb 编译器识别, 使用宏守卫防止普通 C++ 编译器产生未知 pragma 警告
#ifdef ODB_COMPILER
#pragma db object table("tbl_user")
#endif
class UserEntity
{
public:
    /**
     * @brief 构造函数, 创建用户表映射对象, 用于新用户数据的插入
     * @param user_id 用户 ID
     * @param nickname 用户昵称
     * @param email 用户邮箱
     * @param password 加密后的用户密码
     * @param status 用户登录状态
     */
    UserEntity(const std::string& user_id, const std::string& nickname, const std::string& email,
               const std::string& password, UserStatus status);

    /**
     * @brief 获取自增主键 ID
     * @return 自增主键 ID
     */
    unsigned long long Id() const;

    /**
     * @brief 获取用户 ID
     * @return 用户 ID
     */
    const std::string& UserId() const;

    /**
     * @brief 获取用户昵称
     * @return 用户昵称
     */
    const std::string& Nickname() const;

    /**
     * @brief 获取用户邮箱
     * @return 用户邮箱
     */
    const std::string& Email() const;

    /**
     * @brief 获取加密后的用户密码
     * @return 加密后的用户密码
     */
    const std::string& Password() const;

    /**
     * @brief 获取用户登录状态
     * @return 用户登录状态
     */
    UserStatus Status() const;

    /**
     * @brief 设置用户登录状态
     * @param status 用户登录状态
     */
    void SetStatus(UserStatus status);

private:
    // odb 框架通过友元访问私有默认构造函数与私有数据成员
    friend class odb::access;

    // 默认构造函数, 仅供 odb 框架从数据库加载数据时使用
    UserEntity() = default;

    // 自增主键 ID
#ifdef ODB_COMPILER
#pragma db id auto column("id")
#endif
    unsigned long long id_;

    // 用户 ID, 全系统唯一
#ifdef ODB_COMPILER
#pragma db column("user_id") type("VARCHAR(32)") unique
#endif
    std::string user_id_;

    // 用户昵称, 支持昵称登录, 必须唯一
#ifdef ODB_COMPILER
#pragma db column("nickname") type("VARCHAR(32)") unique
#endif
    std::string nickname_;

    // 用户邮箱, 支持邮箱登录, 必须唯一
#ifdef ODB_COMPILER
#pragma db column("email") type("VARCHAR(32)") unique
#endif
    std::string email_;

    // 加密后的用户密码
#ifdef ODB_COMPILER
#pragma db column("password") type("VARCHAR(32)")
#endif
    std::string password_;

    // 用户登录状态
#ifdef ODB_COMPILER
#pragma db column("status") type("TINYINT UNSIGNED")
#endif
    UserStatus status_;
};

} // namespace chat_excel
