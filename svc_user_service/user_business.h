#pragma once

#include <memory>
#include <optional>
#include <string>
#include <cpp-toolkit/rpc.h>
#include "data/user_data.h"
#include "data/verifycode_data.h"
#include "svc_user_service/common.h"
#include "svc_user_service/session_manager.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 用户业务逻辑类, 负责用户注册、登录、退出等用户业务逻辑的组织与实现,
 *        组织用户缓存与验证码缓存的读写时机(Cache-Aside 旁路缓存策略)
 */
class UserBusiness
{
public:
    /**
     * @brief 构造函数, 注入业务依赖对象, 所有成员变量在构造函数中完成初始化
     * @param session_manager 会话管理对象, 由上层创建并统一管理
     * @param verifycode_data 验证码数据访问对象, 由上层创建并统一管理
     * @param user_data 用户数据访问对象, 由上层创建并统一管理
     * @param channel_manager RPC 信道管理对象, 由上层创建并统一管理
     */
    UserBusiness(std::shared_ptr<SessionManager> session_manager,
                 std::shared_ptr<VerifyCodeData> verifycode_data,
                 std::shared_ptr<UserData> user_data,
                 cpp_toolkit::ChannelManager::Ptr channel_manager);

    /**
     * @brief 检查用户昵称是否唯一, 先检查缓存, 未命中时检查数据库
     * @param nickname 用户昵称
     * @return 昵称唯一返回 true, 昵称已存在返回 false
     */
    bool CheckNicknameUnique(const std::string& nickname);

    /**
     * @brief 检查用户邮箱是否唯一, 先检查缓存, 未命中时检查数据库
     * @param email 用户邮箱
     * @return 邮箱唯一返回 true, 邮箱已存在返回 false
     */
    bool CheckEmailUnique(const std::string& email);

    /**
     * @brief 用户注册, 调用该接口之前昵称和邮箱唯一性已经检测通过;
     *        先校验验证码 ID、验证码与用户邮箱是否匹配, 校验通过才能进行注册;
     *        用户 ID 使用 uuid 生成器生成, 密码使用 bcrypt 单向哈希算法加密后保存到数据库
     * @param nickname 用户昵称
     * @param password 用户明文密码
     * @param email 用户邮箱
     * @param verifycode_id 验证码 ID
     * @param verify_code 验证码
     */
    void UserRegister(const std::string& nickname, const std::string& password, const std::string& email,
                      const std::string& verifycode_id, const std::string& verify_code);

    /**
     * @brief 密码登录, 用户名可以是用户昵称或用户邮箱, 优先通过昵称获取用户信息;
     *        登录成功后创建会话, 用户状态设置为上线, 更新 MySQL 并删除 Redis 用户缓存
     * @param username 用户昵称或用户邮箱
     * @param password 用户明文密码
     * @return 登录成功创建的会话 ID
     */
    std::string PasswdLogin(const std::string& username, const std::string& password);

    /**
     * @brief 验证码登录, 检查验证码 ID、验证码、用户邮箱是否都匹配;
     *        登录成功后创建会话, 用户状态设置为上线, 更新 MySQL 并删除 Redis 用户缓存
     * @param verifycode_id 验证码 ID
     * @param verify_code 验证码
     * @param email 用户邮箱
     * @return 登录成功创建的会话 ID
     */
    std::string VcodeLogin(const std::string& verifycode_id, const std::string& verify_code,
                           const std::string& email);

    /**
     * @brief 会话登录, 通过已有会话恢复登录态;
     *        用户状态设置为上线, 更新 MySQL 并删除 Redis 用户缓存
     * @param session_id 会话 ID
     */
    void SessionLogin(const std::string& session_id);

    /**
     * @brief 生成验证码, 随机生成 6 位纯数字验证码,
     *        验证码 ID 使用 uuid 生成器生成, 验证码存储到 Redis 缓存中
     * @param email 用户邮箱
     * @return 验证码 ID
     */
    std::string GetVerifyCode(const std::string& email);

    /**
     * @brief 删除验证码, 通过验证码 ID 删除缓存中的验证码信息,
     *        用于验证码登录成功后使验证码失效, 防止验证码被重复使用
     * @param verifycode_id 验证码 ID
     */
    void DeleteVerifyCode(const std::string& verifycode_id);

    /**
     * @brief 退出登录, 调用数据库子服务删除用户所有数据库连接,
     *        用户状态设置为下线, 更新 MySQL 并删除 Redis 用户缓存,
     *        删除当前会话
     * @param session_id 会话 ID
     */
    void Logout(const std::string& session_id);

    /**
     * @brief 检查会话是否有效, 会话存在且会话所属用户处于上线状态时会话有效,
     *        先检查缓存再检查数据库
     * @param session_id 会话 ID
     * @param user_id 输出参数, 会话有效时为会话所属的用户 ID, 会话无效时为空
     * @return 会话有效返回 true, 会话不存在或用户未上线返回 false
     */
    bool CheckSessionValid(const std::string& session_id, std::string& user_id);

    /**
     * @brief 获取用户信息, 先通过会话 ID 获取用户 ID,
     *        再通过用户 ID 获取用户信息(先读取缓存, 未命中时读取数据库并回写缓存)
     * @param session_id 会话 ID
     * @return 用户信息
     */
    UserInfo GetUserInfo(const std::string& session_id);

private:
    /**
     * @brief 用户登录公共流程, 为用户创建会话, 用户状态设置为上线,
     *        更新 MySQL 并删除 Redis 用户缓存
     * @param user_info 用户信息
     * @return 创建的会话 ID
     */
    std::string CompleteLogin(const UserInfo& user_info);

    /**
     * @brief 更新用户登录状态, 更新 MySQL 并删除 Redis 用户缓存
     * @param user_info 用户信息
     * @param status 更新后的用户登录状态
     */
    void UpdateUserStatus(const UserInfo& user_info, UserStatus status);

    /**
     * @brief 调用数据库子服务 RPC DeleteUserAllConn, 删除用户名下的所有数据库连接,
     *        退出登录时清理用户连接资源, 失败时抛出异常
     * @param user_id 用户 ID
     */
    void DeleteUserAllDatabaseConn(const std::string& user_id);

    /**
     * @brief 通过用户昵称获取用户信息, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param nickname 用户昵称
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByNicknameWithCache(const std::string& nickname);

    /**
     * @brief 通过用户邮箱获取用户信息, 先读取缓存,
     *        未命中时读取数据库并回写缓存
     * @param email 用户邮箱
     * @return 用户信息, 用户不存在时返回 std::nullopt
     */
    std::optional<UserInfo> GetUserByEmailWithCache(const std::string& email);

    // 会话管理对象
    std::shared_ptr<SessionManager> session_manager_;

    // 验证码数据访问对象
    std::shared_ptr<VerifyCodeData> verifycode_data_;

    // 用户数据访问对象
    std::shared_ptr<UserData> user_data_;

    // RPC 信道管理对象
    cpp_toolkit::ChannelManager::Ptr channel_manager_;
};

} // namespace user_service
} // namespace chat_excel
