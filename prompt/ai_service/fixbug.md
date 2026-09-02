
## 1. 补充 AI 子服务关心的子服务

1. 文件子服务可能会向 AI 子服务发送 UpdateSessionFile RPC 方法, 来用于更新会话文件映射表
2. 因此 AI 子服务需要关心文件子服务的上线和下线事件

## 2. AIMessageHandler 发送消息 bug 处理

1. 在获取 JSON 中字段数据的时候 , 要先判断字段是否存在
2. ai_chat_sdk_->SendMessageStream 方法中的回调函数类型是std::function<void(const std::string& content, bool done)> , 第二个参数表示是否完成消息发送 , 阅读每个 SendMessageStream 调用的位置 , 接收到最后一个消息后 , done 是否设置为了 true


## 3. PromptTemplate 解析占位符 bug 处理

1. 对 PromptTemplate 的进行修改 , 内部不再解析占位符了 , 占位符由外部进行传递 , 通过一个 vector<string> 来传递提示词中有哪些占位符 , string 占位符不包含{}

## 4. AiServiceImpl::SendMessage 发送消息逻辑纠正

1. AiServiceImpl::SendMessage 在获取到用户发送的聊天数据之后 , 先向用户发送一个请求头 , 不断开连接 , 再通过 attachment 返回聊天内容 , 在返回数据块时不需要组织 SSE 格式 , 由网关来进行组织

## 5. 数据接口统一定义

1. 阅读 chat-excel/svc_ai_service AI 子服务所有实现 , 将数据接口的定义统一放置在 chat-excel/svc_ai_service/common.h 中

## 6. AI 子服务流失响应切换为 HTTP 格式
参考:
```cpp
void AIServiceImpl::SendMessage(google::protobuf::RpcController* controller,
                                const chat2Data::AiService::SendMessageRequest* request,
                                chat2Data::AiService::SendMessageResponse* response,
                                google::protobuf::Closure* done) {

    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

    // 1. 解析请求参数
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string sessionId = request->session_id();
    std::string chatSessionId = request->chat_session_id();
    std::string chatType = request->chat_type();
    std::string message = request->message();
    std::string fileId = request->file_id();
    auto dbType = request->db_type();
    std::string dbConnectId = request->db_connect_id();
    std::string tableName = request->table_name();

    
    INF("SendMessage called: requestId={}, userId={}, chatSessionId={}, chatType={}",
        requestId, userId, chatSessionId, chatType);

    // 2. 验证参数合法性
    if (chatSessionId.empty() || message.empty() || sessionId.empty()) {
        WRN("SendMessage params invalid: chatSessionId or message or sessionId is empty");
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg("参数不完整：chatSessionId、message和sessionId不能为空");
        done->Run();
        return;
    }
    
    // 3. 设置HTTP响应头，开启流式传输
    cntl->http_response().set_content_type("text/event-stream");
    cntl->http_response().SetHeader("Cache-Control", "no-cache");
    cntl->http_response().SetHeader("Connection", "keep-alive");
    cntl->http_response().SetHeader("Access-Control-Allow-Origin", "*");
    cntl->http_response().SetHeader("Access-Control-Allow-Headers", "*");
    response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
    response->mutable_result()->set_done(false);

    // 5. 获取ProgressiveAttachment实现流式响应
    auto progressiveAttachment = cntl->CreateProgressiveAttachment();

    done->Run();

    try{
        // 6. 构建发送消息上下文
        SendMessageContext context;
        context._requestId = requestId;
        context._sessionId = sessionId;
        context._chatSessionId = chatSessionId;
        context._userId = userId;
        context._message = message;
        context._chatType = chatType;
        context._fileId = fileId;
        context._dbType = dbType;
        context._dbConnectId = dbConnectId;
        context._tableNames = {tableName};  // 在数据库场景中，该表名需要重新解析；因为多个表名是通过逗号分隔的

        // 7. 调用AIBusiness::sendMessage，流式响应会通过progressiveAttachment逐步返回
        // 注意：这里不调用done->Run()，因为流式响应会持续进行，done会在连接关闭时自动调用
        _aiBusiness->sendMessage(context, progressiveAttachment);
    } catch (const chat2Data::Chat2DataException& e) {
        std::string errorMsg = "data: " + chat2Data::error2String(e.getErrorCode()) + "\n\n";
        progressiveAttachment->Write(errorMsg.c_str(), errorMsg.size());
        errorMsg = "data: DONE\n\n";
        progressiveAttachment->Write(errorMsg.c_str(), errorMsg.size());
    }
        
}
```
网关同步进行修改 , 按照 chat-excel/prompt/gateway_service/ai_message_http.md 中的要求修改
void GatewayServiceImpl::handleAiChat(const httplib::Request& req, httplib::Response& res) {
    INF("Handling ai chat request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }

    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string chatSessionId = requestJson.get("chatSessionId", "").asString();
    std::string message = requestJson.get("message", "").asString();
    std::string chatType = requestJson.get("chatType", "").asString();
    std::string fileId = requestJson.get("fileId", "").asString();
    int dbType = requestJson.get("dbType", 0).asInt();
    std::string dbConnectId = requestJson.get("dbConnectId", "").asString();
    std::string tableName = requestJson.get("tableName", "").asString();

    // 3. 鉴权操作
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }

    // 4. 获取AI子服务的服务器地址
    auto aiAddrOpt = _svcChannels->getNodeAddr(FLAGS_ai_service);
    if (!aiAddrOpt) {
        ERR("Failed to get AIService address");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    std::string aiAddr = aiAddrOpt.value();
    INF("AIService address: {}", aiAddr);

    // 5. 构建请求参数
    Json::Value aiRequestJson;
    aiRequestJson["request_id"] = requestId;
    aiRequestJson["session_id"] = sessionId;
    aiRequestJson["user_id"] = userId;
    aiRequestJson["chat_session_id"] = chatSessionId;
    aiRequestJson["chat_type"] = chatType;
    aiRequestJson["message"] = message;
    aiRequestJson["file_id"] = fileId;
    aiRequestJson["db_type"] = dbType;
    aiRequestJson["db_connect_id"] = dbConnectId;
    aiRequestJson["table_name"] = tableName;

    auto requestBodyOpt = biteutil::JSON::serialize(aiRequestJson);
    if (!requestBodyOpt) {
        ERR("Failed to serialize request body");
        sendErrorResponse(res, requestId, 500, "Internal server error");
        return;
    }
    std::string requestBody = requestBodyOpt.value();

    // 6. 设置SSE响应头，响应头中需开启流式传输，并设置SSE数据块处理回调
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");

    // 7. 在回调函数中，构建HTTP客户端，向AI子服务发起发送消息请求
    res.set_chunked_content_provider("text/event-stream", [this, aiAddr, requestBody](size_t offset, httplib::DataSink& dataSink) -> bool {
        // 7.1 解析AI服务地址 "host:port"
        size_t colonPos = aiAddr.find(':');  // 192.168.150.129:9006
        if (colonPos == std::string::npos) {
            ERR("Invalid AIService address format: {}", aiAddr);
            std::string errorData = "data: [ERROR]\n\n";
            dataSink.write(errorData.c_str(), errorData.size());
            dataSink.done();
            return false;
        }
        std::string host = aiAddr.substr(0, colonPos);
        int port = std::stoi(aiAddr.substr(colonPos + 1));

        // 7.2 创建HTTP客户端
        httplib::Client client(host, port);

        // 7.3 设置HTTP请求头参数
        client.set_read_timeout(300, 0);  // 读超时时间300s
        client.set_write_timeout(300, 0);  // 写超时时间300s
        client.set_connection_timeout(60, 0);  // 连接超时时间60s

        // 7.4 构建HTTP请求
        httplib::Request req;
        req.method = "POST";
        req.path = "/chat2Data.AiService.AIService/SendMessage";
        req.headers = {
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}
        };
        req.body = requestBody;

        // 7.5 设置响应头处理器回调：检测是否成功建立连接
        bool connectionFailed = false;
        req.response_handler = [&](const httplib::Response& res) {
            if (res.status != 200) {
                connectionFailed = true;
                return false;     // 终止请求
            }
            return true;         // 继续接收后续响应数据(SSE)
        };

        // 7.6 设置内容接收器回调：将AI子服务返回的SSE数据块，主动推送给前端
        req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t totalLength) {
            if (connectionFailed) {
                return false;
            }
            // 将AI子服务返回的SSE数据块直接写入响应流
            dataSink.write(data, len);
            return true;
        };

        // 7.7 发送HTTP请求
        auto httpResponse = client.send(req);

        // 7.8 检测是否成功建立连接
        if (!httpResponse) {
            ERR("HTTP request to AIService failed");
            std::string errorData = "data: [ERROR]\n\n";
            dataSink.write(errorData.c_str(), errorData.size());
            dataSink.done();
            return false;
        }

        // 7.9 发送结束标记
        std::string doneData = "data: [DONE]\n\n";
        dataSink.write(doneData.c_str(), doneData.size());
        dataSink.done();

        return false;
    });

    INF("Ai chat request handled successfully, requestId: {}", requestId);
}