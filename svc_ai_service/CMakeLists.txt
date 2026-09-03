# =================================================================
#                           AI 子服务
# =================================================================

# aichat_sdk ChatSDK 库(本子服务特有依赖, 通过安装的包配置导入,
# 自动携带头文件搜索路径与 SQLite3 传递依赖, 公共头文件 data_manager.h 包含 <sqlite3.h>)
find_package(aichat_sdk REQUIRED)

# AI 子服务业务逻辑静态库 : 聊天会话管理类 + AI 业务逻辑类 + 提示词模板类实现代码
add_library(chat_excel_ai_business STATIC
    chat_session_manager.cpp
    ai_business.cpp
    prompt_template.cpp
)

# 头文件搜索路径 : 项目根目录(支持 #include "svc_ai_service/xxx.h" 方式导入)
target_include_directories(chat_excel_ai_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_deps    : 公共依赖集合(数据访问层/cpp_toolkit/proto 等)
#   aichat_sdk::aichat_sdk : ChatSDK 库, 用于会话与消息管理(本子服务特有依赖)
target_link_libraries(chat_excel_ai_business PUBLIC
    chat_excel_deps
    aichat_sdk::aichat_sdk
)

# AI 子服务 RPC 接口实现层静态库 : protoc 生成服务类的 RPC 接口实现代码,
# 包含 AI 消息处理类
add_library(chat_excel_ai_service STATIC
    ai_service_impl.cpp
    ai_message_handler.cpp
)

target_include_directories(chat_excel_ai_service PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(chat_excel_ai_service PUBLIC
    chat_excel_ai_business
    chat_excel_deps
)

# AI 子服务可执行文件 : RPC 服务器/服务器构建器 + 程序入口
add_executable(ai_service
    ai_server.cpp
    main.cc
)

target_include_directories(ai_service PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_ai_service : RPC 接口实现层
#   aichat_sdk::aichat_sdk : ai_server.cpp 直接使用 AIChatSdk 注册模型(本子服务特有依赖)
#   chat_excel_deps       : 公共依赖集合
target_link_libraries(ai_service PRIVATE
    chat_excel_ai_service
    aichat_sdk::aichat_sdk
    chat_excel_deps
)

# 将提示词模板目录拷贝到可执行文件所在目录(bin/),
# AIMessageHandler 构造时按可执行文件工作目录加载模板文件
add_custom_command(TARGET ai_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/prompt_template
            $<TARGET_FILE_DIR:ai_service>/prompt_template
)