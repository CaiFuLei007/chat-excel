# =================================================================
#                         AI 子服务业务逻辑层
# =================================================================

# aichat_sdk ChatSDK 库(通过安装的包配置导入, 自动携带头文件搜索路径
# 与 SQLite3 传递依赖, 公共头文件 data_manager.h 包含 <sqlite3.h>)
find_package(aichat_sdk REQUIRED)

# AI 子服务业务逻辑静态库(服务层 RPC 接口实现后续添加)
add_library(chat_excel_ai_business STATIC
    chat_session_manager.cpp
    ai_business.cpp
)

# 头文件搜索路径 :
#   项目根目录 : 支持以 #include "svc_ai_service/xxx.h" 方式导入
target_include_directories(chat_excel_ai_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接库 :
#   chat_excel_data       : 数据实体库(传递链接 odb/redis++/cpp_toolkit 与头文件搜索路径)
#   aichat_sdk::aichat_sdk: ChatSDK 库, 用于会话与消息管理
target_link_libraries(chat_excel_ai_business PUBLIC
    chat_excel_data
    aichat_sdk::aichat_sdk
)
