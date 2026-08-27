#include "svc_user_service/user_service_impl.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <brpc/closure_guard.h>
#include <brpc/server.h>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include <gtest/gtest.h>
#include <notify_service.pb.h>
#include "common/exception.h"
#include "data/session_data.h"
#include "data/user_data.h"
#include "data/verifycode_data.h"
#include "svc_user_service/session_manager.h"
#include "user_service.pb.h"

// proto 生成代码中的消息类型
using chat_excel_proto::user_service::DeleteVerifyCodeRequest;
using chat_excel_proto::user_service::DeleteVerifyCodeResponse;
using chat_excel_proto::user_service::GetCodeRequest;
using chat_excel_proto::user_service::GetCodeResponse;
using chat_excel_proto::user_service::GetUserInfoRequest;
using chat_excel_proto::user_service::GetUserInfoResponse;
using chat_excel_proto::user_service::LogoutRequest;
using chat_excel_proto::user_service::LogoutResponse;
using chat_excel_proto::user_service::PasswdLoginRequest;
using chat_excel_proto::user_service::PasswdLoginResponse;
using chat_excel_proto::user_service::SessionLoginRequest;
using chat_excel_proto::user_service::SessionLoginResponse;
using chat_excel_proto::user_service::UserRegisterRequest;
using chat_excel_proto::user_service::UserRegisterResponse;
using chat_excel_proto::user_service::ValidEmailRequest;
using chat_excel_proto::user_service::ValidEmailResponse;
using chat_excel_proto::user_service::ValidNicknameRequest;
using chat_excel_proto::user_service::ValidNicknameResponse;
using chat_excel_proto::user_service::ValidSessionRequest;
using chat_excel_proto::user_service::ValidSessionResponse;
using chat_excel_proto::user_service::VcodeLoginRequest;
using chat_excel_proto::user_service::VcodeLoginResponse;
// 业务层类型
using chat_excel::ErrorCode;
using chat_excel::user_service::SessionData;
using chat_excel::user_service::SessionManager;
using chat_excel::user_service::UserData;
using chat_excel::user_service::UserBusiness;
using chat_excel::user_service::UserServiceImpl;
using chat_excel::user_service::VerifyCodeData;

namespace
{

// 用户缓存 hash 类型的 key(与 UserData 实现保持一致)
constexpr const char* kUserCacheKey = "user_data";

// 会话缓存 hash 类型的 key(与 SessionData 实现保持一致)
constexpr const char* kSessionCacheKey = "session_data";

// 验证码缓存 hash 类型的 key(与 VerifyCodeData 实现保持一致)
constexpr const char* kVerifyCodeCacheKey = "verifycode_data";

// mock 通知子服务名称(与 UserBusiness 实现保持一致)
constexpr const char* kNotifyServiceName = "NotifyService";

// mock 通知子服务监听端口(避开常用端口, 降低端口冲突概率)
constexpr int kNotifyMockServerPort = 28991;

/**
 * @brief 通知子服务 mock 实现, 对所有 RPC 请求均返回成功,
 *        用于替代真实通知子服务支撑 GetCode 接口的测试
 */
class MockNotifyServiceImpl : public chat_excel_proto::notify_service::NotifyService
{
public:
    void SendVerifyCode(google::protobuf::RpcController* /*controller*/,
                        const chat_excel_proto::notify_service::SendVerifyCodeRequest* request,
                        chat_excel_proto::notify_service::SendVerifyCodeResponse* response,
                        google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }

    void SendEmail(google::protobuf::RpcController* /*controller*/,
                   const chat_excel_proto::notify_service::SendEmailRequest* request,
                   chat_excel_proto::notify_service::SendEmailResponse* response,
                   google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
};

/**
 * @brief 获取 mock 通知子服务监听地址, 服务器进程内只启动一次
 * @return "ip:port" 格式的监听地址字符串
 */
const std::string& GetNotifyMockServerAddr()
{
    static const std::string server_addr = [] {
        static MockNotifyServiceImpl notify_service_impl;
        static brpc::Server server;
        brpc::ServerOptions server_options;
        server.AddService(&notify_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);
        if (server.Start(kNotifyMockServerPort, &server_options) != 0)
        {
            GTEST_LOG_(FATAL) << "mock 通知子服务启动失败, 端口: " << kNotifyMockServerPort;
        }
        return "127.0.0.1:" + std::to_string(kNotifyMockServerPort);
    }();
    return server_addr;
}

/**
 * @brief 获取必填环境变量的值
 * @param name 环境变量名
 * @return 环境变量的值
 */
std::string GetRequiredEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        GTEST_LOG_(FATAL) << "环境变量 " << name << " 未设置";
    }
    return value;
}

/**
 * @brief 获取 MySQL 操作句柄(进程内单例), 配置从环境变量读取
 * @return MySQL 操作句柄
 */
std::shared_ptr<odb::database>& GetMysqlHandle()
{
    static std::shared_ptr<odb::database> handle = [] {
        cpp_toolkit::MySQLSettings settings;
        settings.database = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_DATABASE");
        settings.user = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PASSWORD");
        settings.host = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoul(GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_PORT"));
        settings.charset = GetRequiredEnv("MYSQL_CHAT_EXCEL_TEST_CHARSET");
        return cpp_toolkit::ODBFactory::Create(settings);
    }();
    return handle;
}

/**
 * @brief 获取 Redis 操作句柄(进程内单例), 配置从环境变量读取
 * @return Redis 操作句柄
 */
std::shared_ptr<sw::redis::Redis>& GetRedisHandle()
{
    static std::shared_ptr<sw::redis::Redis> handle = [] {
        cpp_toolkit::RedisSettings settings;
        settings.host = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_HOST");
        settings.port = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PORT"));
        settings.user = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_USER");
        settings.password = GetRequiredEnv("Redis_CHAT_EXCEL_TEST_PASSWORD");
        settings.db = std::stoi(GetRequiredEnv("Redis_CHAT_EXCEL_TEST_INDEX"));
        return cpp_toolkit::RedisFactory::Create(settings);
    }();
    return handle;
}

} // namespace

// 用户子服务 RPC 接口实现类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据,
// 并构建完整业务栈(数据访问层 -> 会话管理/业务逻辑层 -> RPC 接口实现层)
class UserServiceImplTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_user");
        GetMysqlHandle()->execute("DELETE FROM tbl_session");
        transaction.commit();
        GetRedisHandle()->del(kUserCacheKey);
        GetRedisHandle()->del(kSessionCacheKey);
        GetRedisHandle()->del(kVerifyCodeCacheKey);

        // 构建业务栈各层对象
        auto user_data = std::make_shared<UserData>(GetMysqlHandle(), GetRedisHandle());
        auto session_data = std::make_shared<SessionData>(GetMysqlHandle(), GetRedisHandle());
        auto verifycode_data = std::make_shared<VerifyCodeData>(GetRedisHandle());
        auto session_manager = std::make_shared<SessionManager>(session_data, user_data);
        auto channel_manager = std::make_shared<cpp_toolkit::ChannelManager>();
        // 声明关心通知子服务并注册 mock 信道, 支撑 GetCode 接口调用通知子服务发送验证码
        channel_manager->SetCareService(kNotifyServiceName);
        channel_manager->AddService(kNotifyServiceName, GetNotifyMockServerAddr());
        auto user_business = std::make_shared<UserBusiness>(session_manager, verifycode_data, user_data,
                                                            channel_manager);
        user_service_impl_ = std::make_unique<UserServiceImpl>(user_business);

        // 测试用户信息, 字段长度受表结构 VARCHAR(32) 限制
        test_nickname_ = "nick_rpc_test";
        test_email_ = "rpc_test@qq.com";
        test_password_ = "pwd_rpc_test";
    }

    // RPC 接口实现对象
    std::unique_ptr<UserServiceImpl> user_service_impl_;

    // 测试用户信息
    std::string test_nickname_;

    // 测试用户邮箱
    std::string test_email_;

    // 测试用户密码
    std::string test_password_;
};

// ==================== 参数校验测试(参数为空返回参数错误) ====================

TEST_F(UserServiceImplTest, ValidNicknameWithEmptyNicknameReturnParamsError)
{
    ValidNicknameRequest request;
    request.set_request_id("rid_valid_nickname_param");
    ValidNicknameResponse response;

    user_service_impl_->ValidNickname(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_valid_nickname_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, ValidEmailWithEmptyEmailReturnParamsError)
{
    ValidEmailRequest request;
    request.set_request_id("rid_valid_email_param");
    ValidEmailResponse response;

    user_service_impl_->ValidEmail(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_valid_email_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, UserRegisterWithEmptyParamsReturnParamsError)
{
    // 昵称为空
    UserRegisterRequest nickname_empty_request;
    nickname_empty_request.set_request_id("rid_register_param_1");
    nickname_empty_request.set_password(test_password_);
    nickname_empty_request.set_email(test_email_);
    UserRegisterResponse nickname_empty_response;
    user_service_impl_->UserRegister(nullptr, &nickname_empty_request, &nickname_empty_response, nullptr);
    EXPECT_EQ(nickname_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));

    // 密码为空
    UserRegisterRequest password_empty_request;
    password_empty_request.set_request_id("rid_register_param_2");
    password_empty_request.set_nickname(test_nickname_);
    password_empty_request.set_email(test_email_);
    UserRegisterResponse password_empty_response;
    user_service_impl_->UserRegister(nullptr, &password_empty_request, &password_empty_response, nullptr);
    EXPECT_EQ(password_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));

    // 邮箱为空
    UserRegisterRequest email_empty_request;
    email_empty_request.set_request_id("rid_register_param_3");
    email_empty_request.set_nickname(test_nickname_);
    email_empty_request.set_password(test_password_);
    UserRegisterResponse email_empty_response;
    user_service_impl_->UserRegister(nullptr, &email_empty_request, &email_empty_response, nullptr);
    EXPECT_EQ(email_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
}

TEST_F(UserServiceImplTest, SessionLoginWithEmptySessionIdReturnParamsError)
{
    SessionLoginRequest request;
    request.set_request_id("rid_session_login_param");
    SessionLoginResponse response;

    user_service_impl_->SessionLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_session_login_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, PasswdLoginWithEmptyParamsReturnParamsError)
{
    // 用户名为空
    PasswdLoginRequest username_empty_request;
    username_empty_request.set_request_id("rid_passwd_login_param_1");
    username_empty_request.set_password(test_password_);
    PasswdLoginResponse username_empty_response;
    user_service_impl_->PasswdLogin(nullptr, &username_empty_request, &username_empty_response, nullptr);
    EXPECT_EQ(username_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));

    // 密码为空
    PasswdLoginRequest password_empty_request;
    password_empty_request.set_request_id("rid_passwd_login_param_2");
    password_empty_request.set_username(test_nickname_);
    PasswdLoginResponse password_empty_response;
    user_service_impl_->PasswdLogin(nullptr, &password_empty_request, &password_empty_response, nullptr);
    EXPECT_EQ(password_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
}

TEST_F(UserServiceImplTest, GetCodeWithEmptyEmailReturnParamsError)
{
    GetCodeRequest request;
    request.set_request_id("rid_get_code_param");
    GetCodeResponse response;

    user_service_impl_->GetCode(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_code_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, VcodeLoginWithEmptyParamsReturnParamsError)
{
    // 邮箱为空
    VcodeLoginRequest email_empty_request;
    email_empty_request.set_request_id("rid_vcode_login_param_1");
    email_empty_request.set_verify_code("123456");
    email_empty_request.set_code_id("code_id_for_test");
    VcodeLoginResponse email_empty_response;
    user_service_impl_->VcodeLogin(nullptr, &email_empty_request, &email_empty_response, nullptr);
    EXPECT_EQ(email_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));

    // 验证码为空
    VcodeLoginRequest code_empty_request;
    code_empty_request.set_request_id("rid_vcode_login_param_2");
    code_empty_request.set_email(test_email_);
    code_empty_request.set_code_id("code_id_for_test");
    VcodeLoginResponse code_empty_response;
    user_service_impl_->VcodeLogin(nullptr, &code_empty_request, &code_empty_response, nullptr);
    EXPECT_EQ(code_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));

    // 验证码 ID 为空
    VcodeLoginRequest code_id_empty_request;
    code_id_empty_request.set_request_id("rid_vcode_login_param_3");
    code_id_empty_request.set_email(test_email_);
    code_id_empty_request.set_verify_code("123456");
    VcodeLoginResponse code_id_empty_response;
    user_service_impl_->VcodeLogin(nullptr, &code_id_empty_request, &code_id_empty_response, nullptr);
    EXPECT_EQ(code_id_empty_response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
}

TEST_F(UserServiceImplTest, LogoutWithEmptySessionIdReturnParamsError)
{
    LogoutRequest request;
    request.set_request_id("rid_logout_param");
    LogoutResponse response;

    user_service_impl_->Logout(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_logout_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, GetUserInfoWithEmptySessionIdReturnParamsError)
{
    GetUserInfoRequest request;
    request.set_request_id("rid_get_user_info_param");
    GetUserInfoResponse response;

    user_service_impl_->GetUserInfo(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_user_info_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, ValidSessionWithEmptySessionIdReturnParamsError)
{
    ValidSessionRequest request;
    request.set_request_id("rid_valid_session_param");
    ValidSessionResponse response;

    user_service_impl_->ValidSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_valid_session_param");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 检测昵称/邮箱唯一性测试 ====================

TEST_F(UserServiceImplTest, ValidNicknameUniqueThenExistsAfterRegister)
{
    // 未注册时昵称唯一
    ValidNicknameRequest unique_request;
    unique_request.set_request_id("rid_valid_nickname_1");
    unique_request.set_nickname(test_nickname_);
    ValidNicknameResponse unique_response;
    user_service_impl_->ValidNickname(nullptr, &unique_request, &unique_response, nullptr);
    EXPECT_EQ(unique_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(unique_response.error_msg().empty());

    // 注册用户
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_nickname");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    EXPECT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 注册后昵称不唯一
    ValidNicknameRequest exists_request;
    exists_request.set_request_id("rid_valid_nickname_2");
    exists_request.set_nickname(test_nickname_);
    ValidNicknameResponse exists_response;
    user_service_impl_->ValidNickname(nullptr, &exists_request, &exists_response, nullptr);
    EXPECT_EQ(exists_response.error_code(), static_cast<int>(ErrorCode::USER_NICKNAME_EXISTS));
    EXPECT_FALSE(exists_response.error_msg().empty());
}

TEST_F(UserServiceImplTest, ValidEmailUniqueThenExistsAfterRegister)
{
    // 未注册时邮箱唯一
    ValidEmailRequest unique_request;
    unique_request.set_request_id("rid_valid_email_1");
    unique_request.set_email(test_email_);
    ValidEmailResponse unique_response;
    user_service_impl_->ValidEmail(nullptr, &unique_request, &unique_response, nullptr);
    EXPECT_EQ(unique_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(unique_response.error_msg().empty());

    // 注册用户
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_email");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    EXPECT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 注册后邮箱不唯一
    ValidEmailRequest exists_request;
    exists_request.set_request_id("rid_valid_email_2");
    exists_request.set_email(test_email_);
    ValidEmailResponse exists_response;
    user_service_impl_->ValidEmail(nullptr, &exists_request, &exists_response, nullptr);
    EXPECT_EQ(exists_response.error_code(), static_cast<int>(ErrorCode::USER_EMAIL_EXISTS));
    EXPECT_FALSE(exists_response.error_msg().empty());
}

// ==================== 用户注册测试 ====================

TEST_F(UserServiceImplTest, UserRegisterSuccessWithoutErrorMsg)
{
    UserRegisterRequest request;
    request.set_request_id("rid_register_success");
    request.set_nickname(test_nickname_);
    request.set_password(test_password_);
    request.set_email(test_email_);
    UserRegisterResponse response;

    user_service_impl_->UserRegister(nullptr, &request, &response, nullptr);

    // 成功仅设置成功错误码, 不添加成功的描述信息
    EXPECT_EQ(response.request_id(), "rid_register_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, UserRegisterDuplicateNicknameReturnMysqlError)
{
    UserRegisterRequest request;
    request.set_request_id("rid_register_duplicate");
    request.set_nickname(test_nickname_);
    request.set_password(test_password_);
    request.set_email(test_email_);

    // 第一次注册成功
    UserRegisterResponse first_response;
    user_service_impl_->UserRegister(nullptr, &request, &first_response, nullptr);
    EXPECT_EQ(first_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 第二次注册昵称唯一键冲突, 业务逻辑层抛出的异常统一转换为业务处理失败
    UserRegisterResponse second_response;
    user_service_impl_->UserRegister(nullptr, &request, &second_response, nullptr);
    EXPECT_EQ(second_response.error_code(), static_cast<int>(ErrorCode::USER_DATA_MYSQL_ERROR));
    EXPECT_FALSE(second_response.error_msg().empty());
}

// ==================== 密码登录测试 ====================

TEST_F(UserServiceImplTest, PasswdLoginSuccessReturnSessionId)
{
    // 注册用户
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_passwd_login");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 使用昵称密码登录成功, 返回会话 ID
    PasswdLoginRequest request;
    request.set_request_id("rid_passwd_login_success");
    request.set_username(test_nickname_);
    request.set_password(test_password_);
    PasswdLoginResponse response;
    user_service_impl_->PasswdLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_passwd_login_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
    EXPECT_FALSE(response.result().session_id().empty());
}

TEST_F(UserServiceImplTest, PasswdLoginWithWrongPasswordReturnPasswordError)
{
    // 注册用户
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_wrong_password");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 密码错误
    PasswdLoginRequest request;
    request.set_request_id("rid_passwd_login_wrong_password");
    request.set_username(test_nickname_);
    request.set_password("wrong_password");
    PasswdLoginResponse response;
    user_service_impl_->PasswdLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_PASSWORD_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, PasswdLoginWithUnknownUserReturnUserDataNotFound)
{
    PasswdLoginRequest request;
    request.set_request_id("rid_passwd_login_unknown_user");
    request.set_username("unknown_user");
    request.set_password(test_password_);
    PasswdLoginResponse response;

    user_service_impl_->PasswdLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_DATA_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 获取验证码测试 ====================

TEST_F(UserServiceImplTest, GetCodeSuccessReturnCodeId)
{
    GetCodeRequest request;
    request.set_request_id("rid_get_code_success");
    request.set_email(test_email_);
    GetCodeResponse response;

    user_service_impl_->GetCode(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_code_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
    EXPECT_FALSE(response.result().code_id().empty());
}

// ==================== 验证码登录测试 ====================

TEST_F(UserServiceImplTest, VcodeLoginSuccessReturnSessionId)
{
    // 注册用户
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_vcode_login");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 获取验证码
    GetCodeRequest get_code_request;
    get_code_request.set_request_id("rid_get_code_for_vcode_login");
    get_code_request.set_email(test_email_);
    GetCodeResponse get_code_response;
    user_service_impl_->GetCode(nullptr, &get_code_request, &get_code_response, nullptr);
    ASSERT_EQ(get_code_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 从数据访问层读取生成的验证码(测试替身模拟用户从邮箱获取验证码)
    VerifyCodeData verifycode_data(GetRedisHandle());
    const auto verifycode_info =
        verifycode_data.GetVerifyCodeByVerifyCodeId(get_code_response.result().code_id());
    ASSERT_TRUE(verifycode_info.has_value());

    // 使用匹配的验证码登录成功, 返回会话 ID
    VcodeLoginRequest request;
    request.set_request_id("rid_vcode_login_success");
    request.set_email(test_email_);
    request.set_verify_code(verifycode_info->verify_code);
    request.set_code_id(get_code_response.result().code_id());
    VcodeLoginResponse response;
    user_service_impl_->VcodeLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_vcode_login_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
    EXPECT_FALSE(response.result().session_id().empty());
}

TEST_F(UserServiceImplTest, VcodeLoginWithWrongCodeReturnVerifycodeError)
{
    // 注册用户并获取验证码
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_wrong_code");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    GetCodeRequest get_code_request;
    get_code_request.set_request_id("rid_get_code_for_wrong_code");
    get_code_request.set_email(test_email_);
    GetCodeResponse get_code_response;
    user_service_impl_->GetCode(nullptr, &get_code_request, &get_code_response, nullptr);
    ASSERT_EQ(get_code_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 使用错误验证码登录失败
    VcodeLoginRequest request;
    request.set_request_id("rid_vcode_login_wrong_code");
    request.set_email(test_email_);
    request.set_verify_code("654321");
    request.set_code_id(get_code_response.result().code_id());
    VcodeLoginResponse response;
    user_service_impl_->VcodeLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::VERIFYCODE_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, VcodeLoginWithUnknownEmailReturnUserDataNotFound)
{
    // 未注册邮箱获取验证码(验证码生成不检查用户是否存在)
    GetCodeRequest get_code_request;
    get_code_request.set_request_id("rid_get_code_for_unknown_email");
    get_code_request.set_email("unregistered@qq.com");
    GetCodeResponse get_code_response;
    user_service_impl_->GetCode(nullptr, &get_code_request, &get_code_response, nullptr);
    ASSERT_EQ(get_code_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 从数据访问层读取生成的验证码
    VerifyCodeData verifycode_data(GetRedisHandle());
    const auto verifycode_info =
        verifycode_data.GetVerifyCodeByVerifyCodeId(get_code_response.result().code_id());
    ASSERT_TRUE(verifycode_info.has_value());

    // 验证码匹配但用户不存在
    VcodeLoginRequest request;
    request.set_request_id("rid_vcode_login_unknown_email");
    request.set_email("unregistered@qq.com");
    request.set_verify_code(verifycode_info->verify_code);
    request.set_code_id(get_code_response.result().code_id());
    VcodeLoginResponse response;
    user_service_impl_->VcodeLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_DATA_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 删除验证码测试 ====================

TEST_F(UserServiceImplTest, DeleteVerifyCodeWithEmptyCodeIdReturnParamsError)
{
    DeleteVerifyCodeRequest request;
    request.set_request_id("rid_delete_code_empty");
    request.set_code_id("");
    DeleteVerifyCodeResponse response;

    user_service_impl_->DeleteVerifyCode(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_delete_code_empty");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::USER_SERVICE_PARAMS_ERROR));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, DeleteVerifyCodeSuccessThenVcodeLoginFail)
{
    // 注册用户并获取验证码
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_delete_code");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    GetCodeRequest get_code_request;
    get_code_request.set_request_id("rid_get_code_for_delete_code");
    get_code_request.set_email(test_email_);
    GetCodeResponse get_code_response;
    user_service_impl_->GetCode(nullptr, &get_code_request, &get_code_response, nullptr);
    ASSERT_EQ(get_code_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 从数据访问层读取生成的验证码(测试替身模拟用户从邮箱获取验证码)
    VerifyCodeData verifycode_data(GetRedisHandle());
    const auto verifycode_info =
        verifycode_data.GetVerifyCodeByVerifyCodeId(get_code_response.result().code_id());
    ASSERT_TRUE(verifycode_info.has_value());

    // 删除验证码成功
    DeleteVerifyCodeRequest delete_request;
    delete_request.set_request_id("rid_delete_code_success");
    delete_request.set_code_id(get_code_response.result().code_id());
    DeleteVerifyCodeResponse delete_response;
    user_service_impl_->DeleteVerifyCode(nullptr, &delete_request, &delete_response, nullptr);

    EXPECT_EQ(delete_response.request_id(), "rid_delete_code_success");
    EXPECT_EQ(delete_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(delete_response.error_msg().empty());

    // 验证码已失效, 使用原验证码登录失败
    VcodeLoginRequest login_request;
    login_request.set_request_id("rid_vcode_login_after_delete");
    login_request.set_email(test_email_);
    login_request.set_verify_code(verifycode_info->verify_code);
    login_request.set_code_id(get_code_response.result().code_id());
    VcodeLoginResponse login_response;
    user_service_impl_->VcodeLogin(nullptr, &login_request, &login_response, nullptr);

    EXPECT_EQ(login_response.error_code(), static_cast<int>(ErrorCode::VERIFYCODE_ERROR));
    EXPECT_FALSE(login_response.error_msg().empty());
}

TEST_F(UserServiceImplTest, DeleteVerifyCodeWithUnknownCodeIdStillSuccess)
{
    // 删除不存在的验证码, hdel 幂等删除, 仍返回成功
    DeleteVerifyCodeRequest request;
    request.set_request_id("rid_delete_code_unknown");
    request.set_code_id("unknown-code-id-000");
    DeleteVerifyCodeResponse response;

    user_service_impl_->DeleteVerifyCode(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_delete_code_unknown");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
}

// ==================== 会话登录测试 ====================

TEST_F(UserServiceImplTest, SessionLoginSuccessAfterPasswdLogin)
{
    // 注册用户并密码登录获取会话
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_session_login");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    PasswdLoginRequest passwd_login_request;
    passwd_login_request.set_request_id("rid_passwd_login_for_session_login");
    passwd_login_request.set_username(test_nickname_);
    passwd_login_request.set_password(test_password_);
    PasswdLoginResponse passwd_login_response;
    user_service_impl_->PasswdLogin(nullptr, &passwd_login_request, &passwd_login_response, nullptr);
    ASSERT_EQ(passwd_login_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 使用已有会话恢复登录态成功
    SessionLoginRequest request;
    request.set_request_id("rid_session_login_success");
    request.set_session_id(passwd_login_response.result().session_id());
    SessionLoginResponse response;
    user_service_impl_->SessionLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_session_login_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, SessionLoginWithInvalidSessionReturnSessionNotFound)
{
    SessionLoginRequest request;
    request.set_request_id("rid_session_login_invalid");
    request.set_session_id("invalid_session_id");
    SessionLoginResponse response;

    user_service_impl_->SessionLogin(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 获取用户信息测试 ====================

TEST_F(UserServiceImplTest, GetUserInfoSuccessReturnUserInfo)
{
    // 注册用户并密码登录获取会话
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_get_user_info");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    PasswdLoginRequest passwd_login_request;
    passwd_login_request.set_request_id("rid_passwd_login_for_get_user_info");
    passwd_login_request.set_username(test_nickname_);
    passwd_login_request.set_password(test_password_);
    PasswdLoginResponse passwd_login_response;
    user_service_impl_->PasswdLogin(nullptr, &passwd_login_request, &passwd_login_response, nullptr);
    ASSERT_EQ(passwd_login_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 通过会话 ID 获取用户信息成功
    GetUserInfoRequest request;
    request.set_request_id("rid_get_user_info_success");
    request.set_session_id(passwd_login_response.result().session_id());
    GetUserInfoResponse response;
    user_service_impl_->GetUserInfo(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_get_user_info_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
    EXPECT_FALSE(response.result().user_info().user_id().empty());
    EXPECT_EQ(response.result().user_info().nickname(), test_nickname_);
    EXPECT_EQ(response.result().user_info().email(), test_email_);
}

TEST_F(UserServiceImplTest, GetUserInfoWithInvalidSessionReturnSessionNotFound)
{
    GetUserInfoRequest request;
    request.set_request_id("rid_get_user_info_invalid");
    request.set_session_id("invalid_session_id");
    GetUserInfoResponse response;

    user_service_impl_->GetUserInfo(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 退出登录测试 ====================

TEST_F(UserServiceImplTest, LogoutSuccessThenSessionInvalid)
{
    // 注册用户并密码登录获取会话
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_logout");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    PasswdLoginRequest passwd_login_request;
    passwd_login_request.set_request_id("rid_passwd_login_for_logout");
    passwd_login_request.set_username(test_nickname_);
    passwd_login_request.set_password(test_password_);
    PasswdLoginResponse passwd_login_response;
    user_service_impl_->PasswdLogin(nullptr, &passwd_login_request, &passwd_login_response, nullptr);
    ASSERT_EQ(passwd_login_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 退出登录成功
    LogoutRequest request;
    request.set_request_id("rid_logout_success");
    request.set_session_id(passwd_login_response.result().session_id());
    LogoutResponse response;
    user_service_impl_->Logout(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_logout_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());

    // 退出登录后原会话失效, 获取用户信息返回会话不存在
    GetUserInfoRequest get_user_info_request;
    get_user_info_request.set_request_id("rid_get_user_info_after_logout");
    get_user_info_request.set_session_id(passwd_login_response.result().session_id());
    GetUserInfoResponse get_user_info_response;
    user_service_impl_->GetUserInfo(nullptr, &get_user_info_request, &get_user_info_response, nullptr);
    EXPECT_EQ(get_user_info_response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
}

TEST_F(UserServiceImplTest, LogoutWithInvalidSessionReturnSessionNotFound)
{
    LogoutRequest request;
    request.set_request_id("rid_logout_invalid");
    request.set_session_id("invalid_session_id");
    LogoutResponse response;

    user_service_impl_->Logout(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

// ==================== 检查会话有效性测试 ====================

TEST_F(UserServiceImplTest, ValidSessionSuccessAfterPasswdLogin)
{
    // 注册用户并密码登录获取会话
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_valid_session");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    PasswdLoginRequest passwd_login_request;
    passwd_login_request.set_request_id("rid_passwd_login_for_valid_session");
    passwd_login_request.set_username(test_nickname_);
    passwd_login_request.set_password(test_password_);
    PasswdLoginResponse passwd_login_response;
    user_service_impl_->PasswdLogin(nullptr, &passwd_login_request, &passwd_login_response, nullptr);
    ASSERT_EQ(passwd_login_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 登录后检查会话有效
    ValidSessionRequest request;
    request.set_request_id("rid_valid_session_success");
    request.set_session_id(passwd_login_response.result().session_id());
    ValidSessionResponse response;
    user_service_impl_->ValidSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_valid_session_success");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_TRUE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, ValidSessionWithInvalidSessionReturnSessionNotFound)
{
    ValidSessionRequest request;
    request.set_request_id("rid_valid_session_invalid");
    request.set_session_id("invalid_session_id");
    ValidSessionResponse response;

    user_service_impl_->ValidSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

TEST_F(UserServiceImplTest, ValidSessionFailAfterLogout)
{
    // 注册用户并密码登录获取会话
    UserRegisterRequest register_request;
    register_request.set_request_id("rid_register_for_valid_session_logout");
    register_request.set_nickname(test_nickname_);
    register_request.set_password(test_password_);
    register_request.set_email(test_email_);
    UserRegisterResponse register_response;
    user_service_impl_->UserRegister(nullptr, &register_request, &register_response, nullptr);
    ASSERT_EQ(register_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    PasswdLoginRequest passwd_login_request;
    passwd_login_request.set_request_id("rid_passwd_login_for_valid_session_logout");
    passwd_login_request.set_username(test_nickname_);
    passwd_login_request.set_password(test_password_);
    PasswdLoginResponse passwd_login_response;
    user_service_impl_->PasswdLogin(nullptr, &passwd_login_request, &passwd_login_response, nullptr);
    ASSERT_EQ(passwd_login_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 退出登录后原会话失效
    LogoutRequest logout_request;
    logout_request.set_request_id("rid_logout_for_valid_session");
    logout_request.set_session_id(passwd_login_response.result().session_id());
    LogoutResponse logout_response;
    user_service_impl_->Logout(nullptr, &logout_request, &logout_response, nullptr);
    ASSERT_EQ(logout_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    ValidSessionRequest request;
    request.set_request_id("rid_valid_session_after_logout");
    request.set_session_id(passwd_login_response.result().session_id());
    ValidSessionResponse response;
    user_service_impl_->ValidSession(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SESSION_NOT_FOUND));
    EXPECT_FALSE(response.error_msg().empty());
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "user_service_impl_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
