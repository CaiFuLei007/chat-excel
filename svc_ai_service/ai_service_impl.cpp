#include "svc_ai_service/ai_service_impl.h"

#include <string>
#include <utility>
#include <vector>
#include <brpc/closure_guard.h>
#include <cpp-toolkit/logger.h>
#include "common/exception.h"

namespace chat_excel
{
namespace ai_service
{

// proto 生成代码所在命名空间的别名, 简化 RPC 接口签名
namespace proto = ::chat_excel_proto::ai_service;

namespace
{

/**
 * @brief 将错误码与错误码描述填充到 RPC 响应中
 * @param response RPC 响应对象
 * @param error_code 错误码
 */
template <typename ResponseType>
void SetErrorResponse(ResponseType* response, ErrorCode error_code)
{
    response->set_error_code(static_cast<int>(error_code));
    response->set_error_msg(ErrorMessage(error_code));
}

} // namespace

AiServiceImpl::AiServiceImpl(std::shared_ptr<AiBusiness> ai_business)
    : ai_business_(std::move(ai_business))
{
}

void AiServiceImpl::GetModels(google::protobuf::RpcController* /*controller*/,
                              const proto::GetModelsRequest* request,
                              proto::GetModelsResponse* response,
                              google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 调用业务逻辑层获取可用的模型列表
        const std::vector<aichat_sdk::ModelInfo> models = ai_business_->GetModels();

        // 将模型列表填充到响应结果中
        for (const aichat_sdk::ModelInfo& model : models)
        {
            proto::ModelInfo* model_info = response->mutable_result()->add_models();
            model_info->set_name(model.model_name);
            model_info->set_desc(model.model_decs);
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetModels 接口业务处理异常, request_id: {}, 错误信息: {}",
            request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetModels 接口非预期异常, request_id: {}, 错误信息: {}",
            request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

void AiServiceImpl::CreateSession(google::protobuf::RpcController* /*controller*/,
                                  const proto::CreateChatSessionRequest* request,
                                  proto::CreateChatSessionResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 逐项校验参数并返回对应的错误码, 便于上层直接定位具体错误
        if (request->user_id().empty())
        {
            ERR("CreateSession 接口请求参数错误, user_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->model().empty())
        {
            ERR("CreateSession 接口请求参数错误, model 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_MODEL_NAME_EMPTY);
            return;
        }
        else if (request->session_type().empty())
        {
            ERR("CreateSession 接口请求参数错误, session_type 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_SESSION_TYPE_EMPTY);
            return;
        }
        else if (request->session_type() != "excel" && request->session_type() != "database")
        {
            ERR("CreateSession 接口请求参数错误, session_type 无效: {}, request_id: {}",
                request->session_type(), request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_SESSION_TYPE_INVALID);
            return;
        }
        else if (request->session_type() == "database" && request->db_connection_info().empty())
        {
            ERR("CreateSession 接口请求参数错误, database 类型会话缺少数据库连接信息, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_DB_CONNECTION_INFO_EMPTY);
            return;
        }

        // 调用业务逻辑层新建聊天会话, 创建失败时业务逻辑层抛出异常
        const std::string chat_session_id = ai_business_->CreateChatSession(
            request->request_id(), request->user_id(), request->model(),
            request->session_type(), request->db_connection_info());

        // 将新建会话数据填充到响应结果中
        proto::CreateChatSessionData* session = response->mutable_result()->mutable_session();
        session->set_chat_session_id(chat_session_id);
        session->set_model(request->model());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("CreateSession 接口业务处理异常, user_id: {}, request_id: {}, 错误信息: {}",
            request->user_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("CreateSession 接口非预期异常, user_id: {}, request_id: {}, 错误信息: {}",
            request->user_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

void AiServiceImpl::GetSessions(google::protobuf::RpcController* /*controller*/,
                                const proto::GetSessionsRequest* request,
                                proto::GetSessionsResponse* response,
                                google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 用户 ID 不能为空
        if (request->user_id().empty())
        {
            ERR("GetSessions 接口请求参数错误, user_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_USER_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取指定用户的聊天会话列表
        const std::vector<ChatSessionInfo> session_list =
            ai_business_->GetChatSessionList(request->request_id(), request->user_id());

        // 将会话列表填充到响应结果中, 首条消息内容暂填充为"新会话"
        for (const ChatSessionInfo& session_info : session_list)
        {
            proto::SessionInfo* result_session_info = response->mutable_result()->add_sessioninfo();
            result_session_info->set_id(session_info.chat_session_id);
            result_session_info->set_model(session_info.model_name);
            result_session_info->set_title(session_info.title);
            result_session_info->set_created_at(static_cast<int64_t>(session_info.create_time));
            result_session_info->set_updated_at(static_cast<int64_t>(session_info.update_time));
            result_session_info->set_message_count(static_cast<int32_t>(session_info.total_message_count));
            result_session_info->set_first_user_message_content("新会话");
            result_session_info->set_session_type(session_info.type);
            result_session_info->set_db_connection_info(session_info.connection_info);
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetSessions 接口业务处理异常, user_id: {}, request_id: {}, 错误信息: {}",
            request->user_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetSessions 接口非预期异常, user_id: {}, request_id: {}, 错误信息: {}",
            request->user_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

void AiServiceImpl::GetSessionHistory(google::protobuf::RpcController* /*controller*/,
                                      const proto::GetSessionHistoryRequest* request,
                                      proto::GetSessionHistoryResponse* response,
                                      google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 逐项校验参数并返回对应的错误码, 便于上层直接定位具体错误
        if (request->user_id().empty())
        {
            ERR("GetSessionHistory 接口请求参数错误, user_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->chat_session_id().empty())
        {
            ERR("GetSessionHistory 接口请求参数错误, chat_session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_CHAT_SESSION_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层获取指定会话的历史消息, 会话不存在或不属于当前用户时抛出异常
        const ChatSessionHistory history =
            ai_business_->GetSessionHistory(request->request_id(), request->user_id(),
                                            request->chat_session_id());

        // 将会话元数据填充到响应结果中
        proto::GetSessionHistoryResult* result = response->mutable_result();
        result->set_file_id(history.chat_session_info.file_id);
        result->set_session_type(history.chat_session_info.type);
        result->set_db_connection_info(history.chat_session_info.connection_info);

        // 将历史消息列表填充到响应结果中
        for (const aichat_sdk::Message& message : history.messages)
        {
            proto::HistoryMessage* history_message = result->add_messages();
            history_message->set_id(message.mid);
            history_message->set_role(message.role);
            history_message->set_content(message.content);
            history_message->set_timestamp(static_cast<int64_t>(message.create_time));
        }

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("GetSessionHistory 接口业务处理异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("GetSessionHistory 接口非预期异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

void AiServiceImpl::DeleteSession(google::protobuf::RpcController* /*controller*/,
                                  const proto::DeleteSessionRequest* request,
                                  proto::DeleteSessionResponse* response,
                                  google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 逐项校验参数并返回对应的错误码, 便于上层直接定位具体错误
        if (request->user_id().empty())
        {
            ERR("DeleteSession 接口请求参数错误, user_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->chat_session_id().empty())
        {
            ERR("DeleteSession 接口请求参数错误, chat_session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_CHAT_SESSION_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层删除指定会话, 会话不存在或不属于当前用户时抛出异常
        ai_business_->DeleteChatSession(request->request_id(), request->user_id(),
                                        request->chat_session_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("DeleteSession 接口业务处理异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("DeleteSession 接口非预期异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

void AiServiceImpl::UpdateSessionFile(google::protobuf::RpcController* /*controller*/,
                                      const proto::UpdateSessionFileRequest* request,
                                      proto::UpdateSessionFileResponse* response,
                                      google::protobuf::Closure* done)
{
    // 管理 RPC 响应的内存生命周期, 函数结束析构时自动调用 done->Run() 返回响应
    brpc::ClosureGuard closure_guard(done);

    // 响应中回填请求 ID, 用于请求与响应的链路追踪
    response->set_request_id(request->request_id());

    try
    {
        // 参数解析与校验, 逐项校验参数并返回对应的错误码, 便于上层直接定位具体错误
        if (request->user_id().empty())
        {
            ERR("UpdateSessionFile 接口请求参数错误, user_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_USER_ID_EMPTY);
            return;
        }
        else if (request->chat_session_id().empty())
        {
            ERR("UpdateSessionFile 接口请求参数错误, chat_session_id 为空, request_id: {}",
                request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_CHAT_SESSION_ID_EMPTY);
            return;
        }
        else if (request->file_id().empty())
        {
            ERR("UpdateSessionFile 接口请求参数错误, file_id 为空, request_id: {}", request->request_id());
            SetErrorResponse(response, ErrorCode::AI_SERVICE_FILE_ID_EMPTY);
            return;
        }

        // 调用业务逻辑层更新会话关联的文件 ID, 会话不存在或不属于当前用户时抛出异常
        ai_business_->UpdateChatSessionFileId(request->request_id(), request->user_id(),
                                              request->chat_session_id(), request->file_id());

        // 成功仅设置成功错误码, 不添加成功的描述信息
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
    catch (const ChatExcelException& e)
    {
        // 业务处理异常, 按照业务处理失败的逻辑进行处理
        ERR("UpdateSessionFile 接口业务处理异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, e.error_code());
    }
    catch (const std::exception& e)
    {
        // 非预期异常, 统一按照业务处理失败的逻辑进行处理
        ERR("UpdateSessionFile 接口非预期异常, chat_session_id: {}, request_id: {}, 错误信息: {}",
            request->chat_session_id(), request->request_id(), e.what());
        SetErrorResponse(response, ErrorCode::AI_SERVICE_INTERNAL_ERROR);
    }
}

} // namespace ai_service
} // namespace chat_excel
