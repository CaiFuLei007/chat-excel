#pragma once

#include <memory>
#include <string>
#include <httplib.h>
#include <cpp-toolkit/rpc.h>
#include "ai_service.pb.h"
#include "database_service.pb.h"
#include "file_service.pb.h"
#include "user_service.pb.h"

namespace chat_excel
{

// proto 生成代码所在命名空间的别名, 简化 RPC 客户端类型签名
namespace proto = ::chat_excel_proto::user_service;

// 文件子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端类型签名
namespace file_proto = ::chat_excel_proto::file_service;

// 数据库子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端类型签名
namespace db_proto = ::chat_excel_proto::database_service;

// AI 子服务 proto 生成代码所在命名空间的别名, 简化 RPC 客户端类型签名
namespace ai_proto = ::chat_excel_proto::ai_service;

/**
 * @brief 网关 HTTP 接口定义类, 负责全部 30 个 HTTP 接口的定义与路由绑定
 *        已实现健康检测接口、用户子服务 9 个接口、文件子服务 9 个接口与数据库子服务 5 个接口, 其余接口只定义不实现(空处理函数), 后续版本补充业务逻辑
 *        内部持有服务信道管理对象, 供各接口实现时获取 RPC 服务器 Channel
 *        注意 : 本类对象必须使用 std::shared_ptr 管理(路由回调依赖 shared_from_this 保活)
 */
class GatewayServiceImpl : public std::enable_shared_from_this<GatewayServiceImpl>
{
public:
    /**
     * @brief 构造函数, 完成服务信道管理对象的初始化
     * @param channel_manager 服务信道管理对象, 用于获取各子服务的 RPC 信道
     */
    explicit GatewayServiceImpl(const cpp_toolkit::ChannelManager::Ptr& channel_manager);

    /**
     * @brief 默认析构函数
     */
    ~GatewayServiceImpl() = default;

    // 接口对象通过路由回调共享持有, 禁止拷贝与赋值
    GatewayServiceImpl(const GatewayServiceImpl&) = delete;
    GatewayServiceImpl& operator=(const GatewayServiceImpl&) = delete;

    /**
     * @brief 将全部 30 个 HTTP 接口绑定到 HTTP 服务器上, 需在服务器启动前调用
     * @param server cpp-httplib HTTP 服务器对象
     */
    void BindRoutes(httplib::Server& server);

    /**
     * @brief 获取指定子服务的 RPC 信道, 供后续各接口实现时调用后端子服务
     * @param service_name 子服务名称
     * @return RPC 信道对象, 服务不存在或暂无可用节点时返回 nullptr
     */
    cpp_toolkit::ChannelPtr GetServiceChannel(const std::string& service_name);

    // ==================== 分组 0 : 健康检测接口 ====================

    /**
     * @brief [H01] 健康检测接口, 返回网关服务健康状态(响应不套用通用信封)
     * @param request HTTP 请求对象
     * @param response HTTP 响应对象
     */
    void HandleHealthCheck(const httplib::Request& request, httplib::Response& response);

    // ==================== 分组 1 : 用户子服务接口 ====================

    /** @brief [U01] 检测用户昵称是否唯一 */
    void HandleUserNicknameValid(const httplib::Request& request, httplib::Response& response);

    /** @brief [U02] 检测邮箱是否唯一 */
    void HandleUserEmailValid(const httplib::Request& request, httplib::Response& response);

    /** @brief [U03] 用户注册 */
    void HandleUserRegister(const httplib::Request& request, httplib::Response& response);

    /** @brief [U04] 密码登录 */
    void HandleUserPasswdLogin(const httplib::Request& request, httplib::Response& response);

    /** @brief [U05] 获取验证码(发送到用户邮箱) */
    void HandleUserCode(const httplib::Request& request, httplib::Response& response);

    /** @brief [U06] 验证码登录 */
    void HandleUserVcodeLogin(const httplib::Request& request, httplib::Response& response);

    /** @brief [U07] 会话登录(用已有 sessionId 恢复登录态) */
    void HandleUserSessionLogin(const httplib::Request& request, httplib::Response& response);

    /** @brief [U08] 退出登录 */
    void HandleUserLogout(const httplib::Request& request, httplib::Response& response);

    /** @brief [U09] 获取用户信息(参数全部在 query 中) */
    void HandleUserInfo(const httplib::Request& request, httplib::Response& response);

    // ==================== 分组 2 : 文件子服务接口 ====================

    /** @brief [F01] 上传文件信息(第一步 : 登记元数据, 返回 fileId) */
    void HandleFileUploadInfo(const httplib::Request& request, httplib::Response& response);

    /** @brief [F02] 获取文件信息 */
    void HandleFileInfo(const httplib::Request& request, httplib::Response& response);

    /** @brief [F03] 上传文件数据(第二步 : 二进制上传, 配合 [F01] 返回的 fileId) */
    void HandleFileUpload(const httplib::Request& request, httplib::Response& response);

    /** @brief [F04] 下载文件(响应为文件二进制流) */
    void HandleFileDownload(const httplib::Request& request, httplib::Response& response);

    /** @brief [F05] 删除文件(fileId 为路径参数) */
    void HandleFileDelete(const httplib::Request& request, httplib::Response& response);

    /** @brief [F06] 预览 Excel 文件(分页) */
    void HandleFilePreview(const httplib::Request& request, httplib::Response& response);

    /** @brief [F07] 获取用户文件列表 */
    void HandleFileList(const httplib::Request& request, httplib::Response& response);

    /** @brief [F08] 关联文件和聊天会话映射 */
    void HandleFileChatMap(const httplib::Request& request, httplib::Response& response);

    /** @brief [F09] 上传 SQLite 文件(二进制) */
    void HandleFileSqliteUpload(const httplib::Request& request, httplib::Response& response);

    // ==================== 分组 3 : 存储子服务接口 ====================

    /** @brief [D01] 新建数据库连接 */
    void HandleDbConnect(const httplib::Request& request, httplib::Response& response);

    /** @brief [D02] 断开数据库连接 */
    void HandleDbDisconnect(const httplib::Request& request, httplib::Response& response);

    /** @brief [D03] 获取数据库表列表 */
    void HandleDbTables(const httplib::Request& request, httplib::Response& response);

    /** @brief [D04] 获取表数据 */
    void HandleDbTableData(const httplib::Request& request, httplib::Response& response);

    /** @brief [D05] 获取连接状态 */
    void HandleDbConnectionStatus(const httplib::Request& request, httplib::Response& response);

    // ==================== 分组 4 : AI 子服务接口 ====================

    /** @brief [A01] 获取支持模型列表 */
    void HandleAiModels(const httplib::Request& request, httplib::Response& response);

    /** @brief [A02] 新建聊天会话 */
    void HandleAiSessionCreate(const httplib::Request& request, httplib::Response& response);

    /** @brief [A03] 获取聊天会话列表 */
    void HandleAiChatSessionLists(const httplib::Request& request, httplib::Response& response);

    /** @brief [A04] 获取指定聊天会话历史消息 */
    void HandleAiHistory(const httplib::Request& request, httplib::Response& response);

    /** @brief [A05] 删除指定聊天会话 */
    void HandleAiDelete(const httplib::Request& request, httplib::Response& response);

    /** @brief [A06] 发送消息(流式, SSE) */
    void HandleAiSendStreamMessage(const httplib::Request& request, httplib::Response& response);

private:
    /**
     * @brief 创建用户子服务 RPC 客户端存根, 内部获取用户子服务信道
     * @param channel 输出参数, 获取到的 RPC 信道对象(客户端存根依赖信道对象存活, RPC 调用期间须由调用方保持其生命周期)
     * @return 用户子服务 RPC 客户端存根, 获取信道失败时返回 nullptr
     */
    std::unique_ptr<proto::UserService_Stub> CreateUserRpcStub(cpp_toolkit::ChannelPtr& channel);

    /**
     * @brief 创建文件子服务 RPC 客户端存根, 内部获取文件子服务信道
     * @param channel 输出参数, 获取到的 RPC 信道对象(客户端存根依赖信道对象存活, RPC 调用期间须由调用方保持其生命周期)
     * @return 文件子服务 RPC 客户端存根, 获取信道失败时返回 nullptr
     */
    std::unique_ptr<file_proto::FileService_Stub> CreateFileRpcStub(cpp_toolkit::ChannelPtr& channel);

    /**
     * @brief 创建数据库子服务 RPC 客户端存根, 内部获取数据库子服务信道
     * @param channel 输出参数, 获取到的 RPC 信道对象(客户端存根依赖信道对象存活, RPC 调用期间须由调用方保持其生命周期)
     * @return 数据库子服务 RPC 客户端存根, 获取信道失败时返回 nullptr
     */
    std::unique_ptr<db_proto::DatabaseService_Stub> CreateDatabaseRpcStub(cpp_toolkit::ChannelPtr& channel);

    /**
     * @brief 创建 AI 子服务 RPC 客户端存根, 内部获取 AI 子服务信道
     * @param channel 输出参数, 获取到的 RPC 信道对象(客户端存根依赖信道对象存活, RPC 调用期间须由调用方保持其生命周期)
     * @return AI 子服务 RPC 客户端存根, 获取信道失败时返回 nullptr
     */
    std::unique_ptr<ai_proto::AIService_Stub> CreateAiRpcStub(cpp_toolkit::ChannelPtr& channel);

    /**
     * @brief 鉴权辅助函数, 调用用户子服务 ValidSession RPC 接口检查会话是否有效, 并返回会话所属用户 ID
     * @param request_id 请求 ID, 用于日志链路追踪
     * @param session_id 会话 ID
     * @param user_id 输出参数, 会话有效时返回会话所属用户 ID, 作为文件等子服务 RPC 请求的参数
     * @param error_code 输出参数, 鉴权失败时的错误码(网关错误码或透传的后端业务错误码)
     * @param error_msg 输出参数, 鉴权失败时的错误信息
     * @return true 会话有效, false 会话无效或 RPC 调用失败(错误信息通过输出参数返回)
     */
    bool CheckSessionValid(const std::string& request_id, const std::string& session_id,
                           std::string& user_id, int& error_code, std::string& error_msg);

    // 服务信道管理对象, 用于获取各子服务的 RPC 信道
    cpp_toolkit::ChannelManager::Ptr channel_manager_;
};

} // namespace chat_excel
