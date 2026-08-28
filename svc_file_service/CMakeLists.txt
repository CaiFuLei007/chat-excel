# =================================================================
#                         查找依赖
# =================================================================

# cpp_toolkit 工具库(FileBusiness 成员 ChannelManager 依赖 rpc.h 封装,
# 已声明 brpc/protobuf/spdlog 等底层传递依赖)
find_package(cpp_toolkit REQUIRED)

# =================================================================
#                         生成静态库(业务逻辑层)
# =================================================================

# 文件子服务业务逻辑层静态库 : 文件业务逻辑类实现代码
add_library(chat_excel_file_business STATIC
    file_business.cpp
)

# 头文件搜索路径 : 项目根目录(支持 #include "svc_file_service/file_business.h" 与
# #include "data/file_data.h" 方式导入)
target_include_directories(chat_excel_file_business PUBLIC
    ${PROJECT_SOURCE_DIR}
)

# 链接依赖 :
#   chat_excel_data : 数据访问层(FileData / WorkSheetData), 传递链接 odb / odb-mysql / cpp_toolkit
#   chat_excel_proto : protoc 生成的 RPC 接口代码(UploadFileData 调用
#                      excel_parse_service.pb.h 中 ExcelParserService_Stub 解析 Excel)
#   cpp_toolkit     : ChannelManager RPC 信道管理 / FdfsClient 文件存储 / UuidUtil 封装
target_link_libraries(chat_excel_file_business PUBLIC
    chat_excel_data
    chat_excel_proto
    cpp_toolkit::cpp_toolkit
)
