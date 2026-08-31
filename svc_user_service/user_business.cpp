#include "user_business.h"

#include <crypt.h>
#include <cerrno>
#include <ctime>
#include <random>
#include <brpc/controller.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include <database_service.pb.h>
#include <notify_service.pb.h>
#include "common/exception.h"

namespace chat_excel
{
namespace user_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 类型引用
namespace proto = ::chat_excel_proto::notify_service;

// 数据库子服务 proto 生成代码所在命名空间的别名, 简化 RPC 类型引用
namespace database_proto = ::chat_excel_proto::database_service;

namespace
{

// bcrypt 哈希算法设置前缀 : 2b 算法版本 + 成本因子 12, 拼接盐值构成完整设置串
constexpr const char* kBcryptSettingPrefix = "$2b$12$";

// bcrypt 盐值长度, bcrypt base64 编码后的 128 位盐值对应 22 字符
constexpr size_t kBcryptSaltLength = 22;

// bcrypt base64 字母表, 盐值字符的取值范围
constexpr const char kBcryptBase64Alphabet[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// bcrypt base64 字母表字符数量, 字母数组大小减去结尾符
constexpr size_t kBcryptBase64AlphabetSize = sizeof(kBcryptBase64Alphabet) - 1;

// 验证码最小值, 6 位纯数字的最小值
constexpr int kVerifyCodeMin = 100000;

// 验证码最大值, 6 位纯数字的最大值
constexpr int kVerifyCodeMax = 999999;

// 时间字符串缓冲区大小, yyyy-MM-dd HH:mm:ss 格式共 19 字符, 预留结尾符空间
constexpr size_t kTimeBufferSize = 32;

// 通知子服务名称, 用于从信道管理对象获取通知子服务通信信道
constexpr const char* kNotifyServiceName = "NotifyService";

// 通知子服务 RPC 调用超时时间(毫秒)
constexpr int kNotifyRpcTimeoutMs = 3000;

// 数据库子服务名称, 用于从信道管理对象获取数据库子服务通信信道
constexpr const char* kDatabaseServiceName = "DataBaseService";

// 数据库子服务 RPC 调用超时时间(毫秒)
constexpr int kDatabaseRpcTimeoutMs = 3000;

/**
 * @brief 获取线程本地的梅森旋转数生成器, thread_local 实例保证多线程并发调用时
 *        各线程持有独立的生成器, 无需加锁保证线程安全;
 *        每个线程首次使用时通过随机设备播种一次
 * @return 线程本地的梅森旋转数生成器引用
 */
std::mt19937& GetThreadLocalGenerator()
{
    thread_local std::mt19937 generator(std::random_device{}());
    return generator;
}

/**
 * @brief 获取线程本地的 crypt_r 结果缓冲区, thread_local 实例保证多线程并发调用时
 *        各线程使用独立的缓冲区, 无需加锁保证线程安全;
 *        同时避免较大的结构体占用线程栈空间
 * @return 线程本地的 crypt_r 结果缓冲区引用
 */
struct crypt_data& GetThreadLocalCryptData()
{
    thread_local struct crypt_data crypt_data;
    return crypt_data;
}

/**
 * @brief 生成 bcrypt 随机盐值, 从 bcrypt base64 字母表中随机取 22 个字符拼接而成,
 *         随机字符由梅森旋转数生成器生成
 * @return 22 字符的 bcrypt 盐值字符串
 */
std::string GenerateBcryptSalt()
{
    // 盐值字符在 bcrypt base64 字母表中的均匀分布
    std::uniform_int_distribution<size_t> distribution(0, kBcryptBase64AlphabetSize - 1);

    std::string salt;
    salt.reserve(kBcryptSaltLength);
    for (size_t i = 0; i < kBcryptSaltLength; ++i)
    {
        salt.push_back(kBcryptBase64Alphabet[distribution(GetThreadLocalGenerator())]);
    }
    return salt;
}

/**
 * @brief 使用 bcrypt 单向哈希算法加密密码, 盐值由梅森旋转数生成器随机生成,
 *        加密结果形如 $2b$12${22 位盐值}{31 位哈希}, 定长 60 字符
 * @param password 明文密码
 * @return 加密后的密码哈希字符串
 */
std::string EncryptPassword(const std::string& password)
{
    // bcrypt 设置串 : 算法版本 + 成本因子 + 随机盐值
    const std::string setting = std::string(kBcryptSettingPrefix) + GenerateBcryptSalt();

    // crypt_r 为可重入版本, 多线程并发调用时各自使用独立的线程本地缓冲区
    errno = 0;
    char* result = crypt_r(password.c_str(), setting.c_str(), &GetThreadLocalCryptData());
    if (result == nullptr || result[0] == '*' || result[0] == '\0')
    {
        ERR("bcrypt 加密密码失败");
        throw ChatExcelException(ErrorCode::USER_PASSWORD_ENCRYPT_ERROR);
    }
    return std::string(result);
}

/**
 * @brief 校验明文密码与 bcrypt 加密密码是否匹配,
 *        以存储的完整密码哈希作为设置串重新加密明文密码, 比对两次哈希结果
 * @param password 明文密码
 * @param encrypted_password 数据库中存储的 bcrypt 密码哈希
 * @return 匹配返回 true, 不匹配或加密失败返回 false
 */
bool VerifyPassword(const std::string& password, const std::string& encrypted_password)
{
    errno = 0;
    char* result = crypt_r(password.c_str(), encrypted_password.c_str(), &GetThreadLocalCryptData());
    if (result == nullptr || result[0] == '*' || result[0] == '\0')
    {
        ERR("bcrypt 校验密码失败");
        return false;
    }
    return std::string(result) == encrypted_password;
}

/**
 * @brief 生成随机的 6 位纯数字验证码, 随机数由梅森旋转数生成器生成
 * @return 6 位纯数字验证码字符串
 */
std::string GenerateSixDigitCode()
{
    // 验证码在 6 位纯数字范围内的均匀分布
    std::uniform_int_distribution<int> distribution(kVerifyCodeMin, kVerifyCodeMax);
    return std::to_string(distribution(GetThreadLocalGenerator()));
}

/**
 * @brief 获取当前时间字符串, 使用 localtime_r 可重入接口保证线程安全
 * @return 当前时间字符串, 格式 yyyy-MM-dd HH:mm:ss
 */
std::string GetCurrentTime()
{
    // 获取当前系统时间
    const std::time_t current_time = std::time(nullptr);

    std::tm time_struct;
    localtime_r(&current_time, &time_struct);

    // 格式化为 yyyy-MM-dd HH:mm:ss
    char buffer[kTimeBufferSize];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_struct);
    return buffer;
}

} // namespace

UserBusiness::UserBusiness(std::shared_ptr<SessionManager> session_manager,
                           std::shared_ptr<VerifyCodeData> verifycode_data,
                           std::shared_ptr<UserData> user_data,
                           cpp_toolkit::ChannelManager::Ptr channel_manager)
    : session_manager_(std::move(session_manager)),
      verifycode_data_(std::move(verifycode_data)),
      user_data_(std::move(user_data)),
      channel_manager_(std::move(channel_manager))
{
}

bool UserBusiness::CheckNicknameUnique(const std::string& nickname)
{
    // 缓存命中说明昵称已存在, 直接返回不唯一; 缓存只能证明昵称存在, 不能证明昵称不存在
    if (user_data_->CheckNicknameExistsInCache(nickname))
    {
        return false;
    }

    // 缓存未命中, 回源数据库检查昵称是否存在
    return !user_data_->CheckNicknameExists(nickname);
}

bool UserBusiness::CheckEmailUnique(const std::string& email)
{
    // 缓存命中说明邮箱已存在, 直接返回不唯一; 缓存只能证明邮箱存在, 不能证明邮箱不存在
    if (user_data_->CheckEmailExistsInCache(email))
    {
        return false;
    }

    // 缓存未命中, 回源数据库检查邮箱是否存在
    return !user_data_->CheckEmailExists(email);
}

void UserBusiness::UserRegister(const std::string& nickname, const std::string& password,
                                const std::string& email)
{
    // 构建用户信息, 用户 ID 使用 uuid 生成器生成, 密码使用 bcrypt 单向哈希算法加密
    UserInfo user_info;
    user_info.user_id = cpp_toolkit::UuidUtil::GenerateUuidV4();
    user_info.nickname = nickname;
    user_info.email = email;
    user_info.password = EncryptPassword(password);
    user_info.status = UserStatus::NOT_LOGGED_IN;

    // 将用户信息保存到数据库中, 密码为 bcrypt 加密后的哈希
    user_data_->SaveUser(user_info);
    INFO("用户注册成功, user_id: {}, nickname: {}, email: {}", user_info.user_id, nickname, email);
}

std::string UserBusiness::PasswdLogin(const std::string& username, const std::string& password)
{
    // 用户名可能是用户昵称或用户邮箱, 优先通过昵称获取用户信息
    std::optional<UserInfo> user_info = GetUserByNicknameWithCache(username);
    if (!user_info)
    {
        // 昵称获取失败, 再通过邮箱获取用户信息
        user_info = GetUserByEmailWithCache(username);
    }

    if (!user_info)
    {
        ERR("密码登录失败, 用户不存在, username: {}", username);
        throw ChatExcelException(ErrorCode::USER_DATA_NOT_FOUND);
    }

    // 检测用户密码是否正确
    if (!VerifyPassword(password, user_info->password))
    {
        ERR("密码登录失败, 用户密码错误, username: {}", username);
        throw ChatExcelException(ErrorCode::USER_PASSWORD_ERROR);
    }

    return CompleteLogin(*user_info);
}

std::string UserBusiness::VcodeLogin(const std::string& verifycode_id, const std::string& verify_code,
                                     const std::string& email)
{
    // 通过验证码 ID 从缓存中获取用户的验证码信息, 验证码 ID 即查找键, 查找命中即 ID 匹配
    std::optional<VerifyCodeInfo> verifycode_info =
        verifycode_data_->GetVerifyCodeByVerifyCodeId(verifycode_id);
    if (!verifycode_info)
    {
        ERR("验证码登录失败, 验证码不存在或已过期, verifycode_id: {}", verifycode_id);
        throw ChatExcelException(ErrorCode::VERIFYCODE_ERROR);
    }

    // 检查验证码与用户邮箱是否都匹配
    if (verifycode_info->verify_code != verify_code || verifycode_info->email != email)
    {
        ERR("验证码登录失败, 验证码或用户邮箱不匹配, verifycode_id: {}", verifycode_id);
        throw ChatExcelException(ErrorCode::VERIFYCODE_ERROR);
    }

    // 通过用户邮箱获取用户信息
    std::optional<UserInfo> user_info = GetUserByEmailWithCache(email);
    if (!user_info)
    {
        ERR("验证码登录失败, 用户不存在, email: {}", email);
        throw ChatExcelException(ErrorCode::USER_DATA_NOT_FOUND);
    }

    return CompleteLogin(*user_info);
}

void UserBusiness::SessionLogin(const std::string& session_id)
{
    // 通过会话 ID 获取用户 ID, 再通过用户 ID 获取用户信息
    const std::string user_id = session_manager_->GetUserIdBySessionId(session_id);
    const UserInfo user_info = session_manager_->GetUserInfoByUserId(user_id);

    // 将用户状态设置为上线, 更新 MySQL 并删除 Redis 用户缓存
    UpdateUserStatus(user_info, UserStatus::LOGGED_IN);
    INFO("会话登录成功, session_id: {}, user_id: {}", session_id, user_id);
}

std::string UserBusiness::GetVerifyCode(const std::string& email)
{
    // 构建验证码信息, 验证码 ID 使用 uuid 生成器生成, 验证码为随机 6 位纯数字
    VerifyCodeInfo verifycode_info;
    verifycode_info.verifycode_id = cpp_toolkit::UuidUtil::GenerateUuidV4();
    verifycode_info.verify_code = GenerateSixDigitCode();
    verifycode_info.email = email;
    verifycode_info.create_time = GetCurrentTime();

    // 通过信道管理对象获取通知子服务通信信道
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kNotifyServiceName);
    if (channel == nullptr)
    {
        ERR("获取通知子服务信道失败, 服务名称: {}, email: {}", kNotifyServiceName, email);
        throw ChatExcelException(ErrorCode::USER_NOTIFY_RPC_ERROR);
    }

    // 构建发送验证码 RPC 请求, 请求 ID 使用 uuid 生成器生成用于链路追踪
    proto::SendVerifyCodeRequest rpc_request;
    rpc_request.set_request_id(verifycode_info.verifycode_id);
    rpc_request.set_email(email);
    rpc_request.set_code(verifycode_info.verify_code);

    // 创建通知子服务 RPC 客户端存根
    proto::NotifyService_Stub notify_service_stub(channel.get());

    // 设置 RPC 调用超时时间后同步发送 RPC 请求
    brpc::Controller controller;
    controller.set_timeout_ms(kNotifyRpcTimeoutMs);
    proto::SendVerifyCodeResponse rpc_response;
    notify_service_stub.SendVerifyCode(&controller, &rpc_request, &rpc_response, nullptr);

    // 检测 RPC 调用是否成功(网络/超时/信道层面的失败)
    if (controller.Failed())
    {
        ERR("通知子服务 RPC 调用失败, email: {}, 错误信息: {}", email, controller.ErrorText());
        throw ChatExcelException(ErrorCode::USER_NOTIFY_RPC_ERROR);
    }

    // 检测通知子服务业务处理结果
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("通知子服务发送验证码失败, email: {}, 错误码: {}, 错误信息: {}",
            email, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(ErrorCode::USER_NOTIFY_RPC_ERROR);
    }
    INFO("通知子服务发送验证码成功, email: {}", email);

    // 将验证码存储到 Redis 缓存中
    verifycode_data_->SaveVerifyCode(verifycode_info);
    INFO("生成验证码成功, verifycode_id: {}, email: {}", verifycode_info.verifycode_id, email);
    return verifycode_info.verifycode_id;
}

void UserBusiness::DeleteVerifyCode(const std::string& verifycode_id)
{
    // 通过验证码 ID 删除缓存中的验证码信息, 使验证码失效
    verifycode_data_->DeleteVerifyCodeByVerifyCodeId(verifycode_id);
    INFO("删除验证码成功, verifycode_id: {}", verifycode_id);
}

void UserBusiness::Logout(const std::string& session_id)
{
    // 通过会话 ID 获取用户 ID, 再通过用户 ID 获取用户信息
    const std::string user_id = session_manager_->GetUserIdBySessionId(session_id);
    const UserInfo user_info = session_manager_->GetUserInfoByUserId(user_id);

    // 调用数据库子服务删除该用户名下的所有数据库连接
    DeleteUserAllDatabaseConn(user_id);

    // 将用户状态设置为下线, 更新 MySQL 并删除 Redis 用户缓存
    UpdateUserStatus(user_info, UserStatus::NOT_LOGGED_IN);

    // 删除当前会话
    session_manager_->DeleteSession(session_id);
    INFO("退出登录成功, session_id: {}, user_id: {}", session_id, user_id);
}

bool UserBusiness::CheckSessionValid(const std::string& session_id, std::string& user_id)
{
    // 会话有效性检查由会话管理对象完成, 先检查缓存再检查数据库,
    // 会话有效时输出会话所属的用户 ID, 会话无效时输出为空
    return session_manager_->CheckSessionValid(session_id, user_id);
}

UserInfo UserBusiness::GetUserInfo(const std::string& session_id)
{
    // 先通过会话 ID 获取用户 ID, 再通过用户 ID 获取用户信息
    const std::string user_id = session_manager_->GetUserIdBySessionId(session_id);
    return session_manager_->GetUserInfoByUserId(user_id);
}

std::string UserBusiness::CompleteLogin(const UserInfo& user_info)
{
    // 为该用户新建会话
    const std::string session_id = session_manager_->CreateSession(user_info.user_id);

    // 将用户状态设置为上线, 更新 MySQL 并删除 Redis 用户缓存
    UpdateUserStatus(user_info, UserStatus::LOGGED_IN);
    INFO("用户登录成功, user_id: {}, session_id: {}", user_info.user_id, session_id);
    return session_id;
}

void UserBusiness::UpdateUserStatus(const UserInfo& user_info, UserStatus status)
{
    // 修改用户信息中的登录状态
    UserInfo new_user_info = user_info;
    new_user_info.status = status;

    // 更新 MySQL 中的用户信息, 删除 Redis 中的用户缓存, 数据库是唯一真数据源
    user_data_->UpdateUser(new_user_info);
    user_data_->DeleteUserFromCache(new_user_info);
}

void UserBusiness::DeleteUserAllDatabaseConn(const std::string& user_id)
{
    // 通过信道管理对象获取数据库子服务通信信道
    cpp_toolkit::ChannelPtr channel = channel_manager_->GetChannel(kDatabaseServiceName);
    if (channel == nullptr)
    {
        ERR("获取数据库子服务信道失败, 服务名称: {}, user_id: {}", kDatabaseServiceName, user_id);
        throw ChatExcelException(ErrorCode::USER_DATABASE_RPC_ERROR);
    }

    // 构建删除用户所有数据库连接 RPC 请求, 请求 ID 使用 uuid 生成器生成用于链路追踪
    database_proto::DeleteUserAllConnRequest rpc_request;
    rpc_request.set_request_id(cpp_toolkit::UuidUtil::GenerateUuidV4());
    rpc_request.set_user_id(user_id);

    // 创建数据库子服务 RPC 客户端存根
    database_proto::DatabaseService_Stub database_service_stub(channel.get());

    // 设置 RPC 调用超时时间后同步发送 RPC 请求
    brpc::Controller controller;
    controller.set_timeout_ms(kDatabaseRpcTimeoutMs);
    database_proto::DeleteUserAllConnResponse rpc_response;
    database_service_stub.DeleteUserAllConn(&controller, &rpc_request, &rpc_response, nullptr);

    // 检测 RPC 调用是否成功(网络/超时/信道层面的失败)
    if (controller.Failed())
    {
        ERR("数据库子服务 RPC 调用失败, user_id: {}, 错误信息: {}", user_id, controller.ErrorText());
        throw ChatExcelException(ErrorCode::USER_DATABASE_RPC_ERROR);
    }

    // 检测数据库子服务业务处理结果
    if (rpc_response.error_code() != static_cast<int>(ErrorCode::SUCCESS))
    {
        ERR("数据库子服务删除用户所有连接失败, user_id: {}, 错误码: {}, 错误信息: {}",
            user_id, rpc_response.error_code(), rpc_response.error_msg());
        throw ChatExcelException(ErrorCode::USER_DATABASE_RPC_ERROR);
    }
    INFO("数据库子服务删除用户所有数据库连接成功, user_id: {}", user_id);
}

std::optional<UserInfo> UserBusiness::GetUserByNicknameWithCache(const std::string& nickname)
{
    // 先读取缓存中的用户信息
    std::optional<UserInfo> user_info = user_data_->GetUserByNicknameFromCache(nickname);
    if (user_info)
    {
        return user_info;
    }

    // 缓存未命中, 读取数据库中的用户信息并回写缓存
    user_info = user_data_->GetUserByNickname(nickname);
    if (user_info)
    {
        user_data_->SaveUserToCache(*user_info);
    }
    return user_info;
}

std::optional<UserInfo> UserBusiness::GetUserByEmailWithCache(const std::string& email)
{
    // 先读取缓存中的用户信息
    std::optional<UserInfo> user_info = user_data_->GetUserByEmailFromCache(email);
    if (user_info)
    {
        return user_info;
    }

    // 缓存未命中, 读取数据库中的用户信息并回写缓存
    user_info = user_data_->GetUserByEmail(email);
    if (user_info)
    {
        user_data_->SaveUserToCache(*user_info);
    }
    return user_info;
}

} // namespace user_service
} // namespace chat_excel
