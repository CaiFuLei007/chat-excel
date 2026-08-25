# =================================================================
#                         查找依赖
# =================================================================

# protobuf 库, 用于调用 protoc 编译 proto 文件以及链接生成代码
find_package(Protobuf REQUIRED)

# =================================================================
#                         生成 proto 代码
# =================================================================

# proto 生成代码输出目录 : chat-excel/proto/proto_code,
# protoc 不会自动创建输出目录, 需要提前创建
set(PROTO_CODE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto_code)
file(MAKE_DIRECTORY ${PROTO_CODE_DIR})

# 生成 user_service.pb.h 与 user_service.pb.cc,
# protobuf_generate_cpp 不支持指定输出目录, 使用通用的 protobuf_generate
protobuf_generate(OUT_VAR PROTO_GENERATED_FILES
    LANGUAGE cpp
    PROTOC_OUT_DIR ${PROTO_CODE_DIR}
    PROTOS user_service.proto
)

# =================================================================
#                         生成静态库
# =================================================================

# proto 生成代码静态库, 供各子服务 RPC 接口实现层链接使用
add_library(chat_excel_proto STATIC ${PROTO_GENERATED_FILES})

# 头文件搜索路径 :
#   proto_code 目录 : 支持下游以 #include "user_service.pb.h" 方式导入
target_include_directories(chat_excel_proto PUBLIC
    ${PROTO_CODE_DIR}
)

# 链接依赖 :
#   protobuf::libprotobuf : proto 生成代码依赖的 protobuf 运行时库
target_link_libraries(chat_excel_proto PUBLIC
    protobuf::libprotobuf
)
