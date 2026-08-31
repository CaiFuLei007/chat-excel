#include "svc_file_service/file_service_impl.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/redis.h>
#include <cpp-toolkit/rpc.h>
#include <database_service.pb.h>
#include <excel_parse_service.pb.h>
#include <gtest/gtest.h>
#include <odb/database.hxx>
#include <odb/transaction.hxx>
#include <sw/redis++/redis.h>
#include "common/exception.h"
#include "data/file_data.h"
#include "data/worksheet_data.h"
// FastDFS 客户端头文件必须最后导入 : 其依赖的 fastcommon 头文件会向全局作用域
// 定义 byte 等宏, 先行导入会破坏 fmt/boost 等后续头文件的解析
#include <cpp-toolkit/fdfs.h>

using chat_excel::ErrorCode;
using chat_excel::file_service::FileBusiness;
using chat_excel::file_service::FileData;
using chat_excel::file_service::FileServiceImpl;
using chat_excel::file_service::WorkSheetData;

namespace
{

// proto 生成代码命名空间别名
namespace proto = ::chat_excel_proto::file_service;
namespace db_proto = ::chat_excel_proto::database_service;

// mock Excel 解析子服务监听端口(避开 file_business_test 使用的 28992 端口)
constexpr int kExcelParseMockServerPort = 28993;

// Excel 解析子服务名称(与 FileBusiness 实现中的常量保持一致)
constexpr const char* kExcelParseServiceName = "ExcelParseService";

// mock 数据库子服务监听端口(避开 file_business_test 与 mock Excel 解析子服务端口)
constexpr int kDatabaseMockServerPort = 28994;

// 数据库子服务名称(与 FileBusiness 实现中的常量保持一致)
constexpr const char* kDatabaseServiceName = "DataBaseService";

// Excel 数据库全局连接 ID(与数据库子服务连接管理器中的全局连接 ID 保持一致)
constexpr const char* kExcelDbConnectionId = "excel_connection";

// FastDFS tracker 服务器地址(本地 docker 容器)
constexpr const char* kFdfsTrackerAddr = "127.0.0.1:22122";

// 测试用户 ID 与会话 ID(长度受表结构 VARCHAR(32) 限制)
constexpr const char* kTestUserId = "uid_for_fs_test";
constexpr const char* kOtherUserId = "uid_other_fs_test";
constexpr const char* kTestSessionId = "sid_for_fs_test";

// 测试请求 ID
constexpr const char* kRequestId = "rid_for_fs_test";

// 文件缓存与 WorkSheet 缓存 hash 类型的 key(与数据层实现保持一致)
constexpr const char* kFileCacheKey = "file_data";
constexpr const char* kWorkSheetCacheKey = "worksheet_data";

// 上传接口测试使用的文件内容
constexpr const char* kTestFileContent = "mock excel binary content for rpc test";

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

/**
 * @brief 初始化 FastDFS 客户端(进程内只执行一次), 连接本地 docker 容器中的 tracker
 */
void InitFdfsClient()
{
    static const bool inited = [] {
        cpp_toolkit::FdfsSettings settings;
        settings.tracker_servers_.emplace_back(kFdfsTrackerAddr);
        if (!cpp_toolkit::FdfsClient::Init(settings))
        {
            GTEST_LOG_(FATAL) << "FastDFS 客户端初始化失败, tracker: " << kFdfsTrackerAddr;
        }
        return true;
    }();
    (void)inited;
}

/**
 * @brief mock Excel 解析子服务 : 校验请求的 FastDFS 文件 ID 对应的文件真实存在
 *        (可从 FastDFS 下载), 返回固定的工作表列表与解析结果,
 *        用于验证 UploadFile 接口触发的 Excel 解析流程
 */
class MockExcelParserServiceImpl : public ::chat_excel_proto::excel_parse_service::ExcelParserService
{
public:
    void GetWorksheets(google::protobuf::RpcController* /*controller*/,
                       const ::chat_excel_proto::excel_parse_service::GetWorksheetsRequest* request,
                       ::chat_excel_proto::excel_parse_service::GetWorksheetsResponse* response,
                       google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());

        // 校验业务层传递的 FastDFS 文件 ID 非空且文件真实存在(下载失败视为文件不存在)
        std::string downloaded_content;
        if (request->fastdfs_file_id().empty() ||
            !cpp_toolkit::FdfsClient::DownloadToBuffer(request->fastdfs_file_id(), downloaded_content))
        {
            response->set_error_code(static_cast<int>(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED));
            response->set_error_msg("mock: FastDFS 文件不存在");
            return;
        }
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
        response->add_worksheets("Sheet1");
        response->add_worksheets("Sheet2");
    }

    void ParseExcel(google::protobuf::RpcController* /*controller*/,
                    const ::chat_excel_proto::excel_parse_service::ParseExcelRequest* request,
                    ::chat_excel_proto::excel_parse_service::ParseExcelResponse* response,
                    google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());

        // 校验业务层传递的 FastDFS 文件 ID 非空且文件真实存在(下载失败视为文件不存在)
        std::string downloaded_content;
        if (request->fastdfs_file_id().empty() ||
            !cpp_toolkit::FdfsClient::DownloadToBuffer(request->fastdfs_file_id(), downloaded_content))
        {
            response->set_error_code(static_cast<int>(ErrorCode::EXCEL_PARSE_FILE_OPEN_FAILED));
            response->set_error_msg("mock: FastDFS 文件不存在");
            return;
        }
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));

        // 逐个构建 mock 工作表解析结果 : 名称 + 2 列信息 + 1 行数据
        for (const std::string& worksheet_name : request->worksheets())
        {
            ::chat_excel_proto::excel_parse_service::WorksheetData* worksheet = response->add_worksheets();
            worksheet->set_name(worksheet_name);
            worksheet->set_total_rows(1);
            worksheet->set_total_cols(2);
            worksheet->add_columns()->set_name("名称");
            worksheet->mutable_columns(0)->set_type("String");
            worksheet->add_columns()->set_name("数量");
            worksheet->mutable_columns(1)->set_type("Integer");
            worksheet->add_rows()->add_cells()->set_value("apple");
            worksheet->mutable_rows(0)->mutable_cells(0)->set_type("String");
            worksheet->mutable_rows(0)->add_cells()->set_value("10");
            worksheet->mutable_rows(0)->mutable_cells(1)->set_type("Integer");
        }
    }
};

/**
 * @brief 获取 mock Excel 解析子服务监听地址, 服务器进程内只启动一次
 * @return "ip:port" 格式的监听地址字符串
 */
const std::string& GetExcelParseMockServerAddr()
{
    static const std::string server_addr = [] {
        static MockExcelParserServiceImpl excel_parse_service_impl;
        static brpc::Server server;
        brpc::ServerOptions server_options;
        server.AddService(&excel_parse_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);
        if (server.Start(kExcelParseMockServerPort, &server_options) != 0)
        {
            GTEST_LOG_(FATAL) << "mock Excel 解析子服务启动失败, 端口: " << kExcelParseMockServerPort;
        }
        return "127.0.0.1:" + std::to_string(kExcelParseMockServerPort);
    }();
    return server_addr;
}

/**
 * @brief mock 数据库子服务 : 校验业务层使用 Excel 数据库全局连接,
 *        ImportExcelData 返回导入行数, GetTableData 返回与 mock Excel
 *        解析结果一致的表结构与分页表数据, 其余接口使用基类默认实现
 */
class MockDatabaseServiceImpl : public db_proto::DatabaseService
{
public:
    void ImportExcelData(google::protobuf::RpcController* /*controller*/,
                         const db_proto::ImportExcelDataRequest* request,
                         db_proto::ImportExcelDataResponse* response,
                         google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());

        // 校验业务层使用 Excel 数据库全局连接, 且表名称包含 {file_id}_{worksheet_name} 的分隔下划线
        if (request->db_connect_id() != kExcelDbConnectionId)
        {
            response->set_error_code(static_cast<int>(ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY));
            response->set_error_msg("mock: 数据库连接 ID 非 Excel 全局连接");
            return;
        }
        if (request->table_name().empty() || request->table_name().find('_') == std::string::npos)
        {
            response->set_error_code(static_cast<int>(ErrorCode::DB_IDENTIFIER_INVALID));
            response->set_error_msg("mock: 表名非法");
            return;
        }

        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
        response->mutable_result()->set_table_name(request->table_name());
        response->mutable_result()->set_imported_rows(
            static_cast<int32_t>(request->worksheet_data().rows_size()));
    }

    void GetTableData(google::protobuf::RpcController* /*controller*/,
                      const db_proto::GetTableDataRequest* request,
                      db_proto::GetTableDataResponse* response,
                      google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());

        // 校验业务层使用 Excel 数据库全局连接
        if (request->db_connect_id() != kExcelDbConnectionId)
        {
            response->set_error_code(static_cast<int>(ErrorCode::DB_SERVICE_CONNECTION_ID_EMPTY));
            response->set_error_msg("mock: 数据库连接 ID 非 Excel 全局连接");
            return;
        }

        // 返回与 mock Excel 解析结果一致的表结构(2 列)与分页表数据(1 行)
        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
        db_proto::TableSchemaInfo* table_schema = response->mutable_result()->mutable_table_schema();
        table_schema->add_column_info()->set_name("名称");
        table_schema->mutable_column_info(0)->set_type("String");
        table_schema->add_column_info()->set_name("数量");
        table_schema->mutable_column_info(1)->set_type("Integer");

        db_proto::TableData* table_data = table_schema->mutable_table_data();
        db_proto::Row* row = table_data->add_rows();
        row->add_cells("apple");
        row->add_cells("10");
        table_data->set_total_rows(1);
        table_data->set_current_page(request->page_number());
        table_data->set_total_pages(1);
        table_data->set_page_size(request->page_size());
    }
};

/**
 * @brief 获取 mock 数据库子服务监听地址, 服务器进程内只启动一次
 * @return "ip:port" 格式的监听地址字符串
 */
const std::string& GetDatabaseMockServerAddr()
{
    static const std::string server_addr = [] {
        static MockDatabaseServiceImpl database_service_impl;
        static brpc::Server server;
        brpc::ServerOptions server_options;
        server.AddService(&database_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);
        if (server.Start(kDatabaseMockServerPort, &server_options) != 0)
        {
            GTEST_LOG_(FATAL) << "mock 数据库子服务启动失败, 端口: " << kDatabaseMockServerPort;
        }
        return "127.0.0.1:" + std::to_string(kDatabaseMockServerPort);
    }();
    return server_addr;
}

} // namespace

// 文件子服务 RPC 接口实现类测试夹具, 每个用例执行前清理数据库与缓存中的测试数据,
// 并构建完整业务栈(数据访问层 -> 业务逻辑层 -> RPC 接口实现层)
class FileServiceImplTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 清理数据库与缓存中的测试数据, 避免唯一键冲突与脏数据
        odb::transaction transaction(GetMysqlHandle()->begin());
        GetMysqlHandle()->execute("DELETE FROM tbl_file_info");
        GetMysqlHandle()->execute("DELETE FROM tbl_worksheet_info");
        transaction.commit();
        GetRedisHandle()->del(kFileCacheKey);
        GetRedisHandle()->del(kWorkSheetCacheKey);

        // 初始化 FastDFS 客户端, 连接本地 docker 容器
        InitFdfsClient();

        // 构建信道管理对象, 注册关心服务后添加 mock Excel 解析子服务与
        // mock 数据库子服务地址(AddService 只对 SetCareService 预注册的关心服务生效)
        auto channel_manager = std::make_shared<cpp_toolkit::ChannelManager>();
        channel_manager->SetCareService(kExcelParseServiceName);
        channel_manager->AddService(kExcelParseServiceName, GetExcelParseMockServerAddr());
        channel_manager->SetCareService(kDatabaseServiceName);
        channel_manager->AddService(kDatabaseServiceName, GetDatabaseMockServerAddr());

        // 构建业务栈各层对象
        auto file_data = std::make_shared<FileData>(GetMysqlHandle(), GetRedisHandle());
        auto worksheet_data = std::make_shared<WorkSheetData>(GetMysqlHandle(), GetRedisHandle());
        auto file_business = std::make_shared<FileBusiness>(file_data, worksheet_data, channel_manager);
        file_service_impl_ = std::make_unique<FileServiceImpl>(file_business);
    }

    /**
     * @brief 通过 RPC 接口上传测试 Excel 文件信息, 返回生成的文件 ID
     */
    std::string CallUploadFileInfo(const std::string& file_name, const std::string& file_ext)
    {
        proto::UploadFileInfoRequest request;
        request.set_request_id(kRequestId);
        request.set_session_id(kTestSessionId);
        request.set_user_id(kTestUserId);
        proto::FileInfo* file_info = request.mutable_file_info();
        file_info->set_filename(file_name);
        file_info->set_file_size(10 * 1024);
        file_info->set_file_ext(file_ext);

        proto::UploadFileInfoResponse response;
        file_service_impl_->UploadFileInfo(nullptr, &request, &response, nullptr);
        EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
        return response.result().file_id();
    }

    /**
     * @brief 通过 RPC 接口上传文件数据(attachment 方式), 返回响应错误码
     */
    int CallUploadFile(const std::string& user_id, const std::string& file_id,
                       const std::string& file_content)
    {
        proto::UploadFileRequest request;
        request.set_request_id(kRequestId);
        request.set_session_id(kTestSessionId);
        request.set_user_id(user_id);
        request.set_file_id(file_id);

        // 文件数据通过 brpc 控制器的请求 attachment 传输
        brpc::Controller controller;
        controller.request_attachment().append(file_content);

        proto::UploadFileResponse response;
        file_service_impl_->UploadFile(&controller, &request, &response, nullptr);
        return response.error_code();
    }

    // RPC 接口实现对象
    std::unique_ptr<FileServiceImpl> file_service_impl_;
};

// ==================== 上传文件信息测试 ====================

// 正常情况: 上传文件信息成功, 返回 32 字符文件 ID
TEST_F(FileServiceImplTest, UploadFileInfoSuccess)
{
    proto::UploadFileInfoRequest request;
    request.set_request_id("rid_upload_info");
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    proto::FileInfo* file_info = request.mutable_file_info();
    file_info->set_filename("test_excel.xlsx");
    file_info->set_file_size(10 * 1024);
    file_info->set_file_ext("xlsx");

    proto::UploadFileInfoResponse response;
    file_service_impl_->UploadFileInfo(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), "rid_upload_info");
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().file_id().size(), 32u);
}

// 异常情况: 文件信息缺失或字段为空返回参数错误
TEST_F(FileServiceImplTest, UploadFileInfoWithEmptyParamsReturnParamsError)
{
    // 文件信息缺失
    proto::UploadFileInfoRequest no_file_info_request;
    no_file_info_request.set_request_id("rid_upload_info_1");
    no_file_info_request.set_session_id(kTestSessionId);
    no_file_info_request.set_user_id(kTestUserId);
    proto::UploadFileInfoResponse no_file_info_response;
    file_service_impl_->UploadFileInfo(nullptr, &no_file_info_request, &no_file_info_response, nullptr);
    EXPECT_EQ(no_file_info_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_FILE_INFO_EMPTY));

    // 文件名为空
    proto::UploadFileInfoRequest empty_name_request;
    empty_name_request.set_request_id("rid_upload_info_2");
    empty_name_request.set_session_id(kTestSessionId);
    empty_name_request.set_user_id(kTestUserId);
    proto::FileInfo* empty_name_info = empty_name_request.mutable_file_info();
    empty_name_info->set_filename("");
    empty_name_info->set_file_ext("xlsx");
    proto::UploadFileInfoResponse empty_name_response;
    file_service_impl_->UploadFileInfo(nullptr, &empty_name_request, &empty_name_response, nullptr);
    EXPECT_EQ(empty_name_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_FILE_NAME_EMPTY));

    // 用户 ID 为空
    proto::UploadFileInfoRequest empty_user_request;
    empty_user_request.set_request_id("rid_upload_info_3");
    empty_user_request.set_session_id(kTestSessionId);
    empty_user_request.set_user_id("");
    proto::FileInfo* empty_user_info = empty_user_request.mutable_file_info();
    empty_user_info->set_filename("test.xlsx");
    empty_user_info->set_file_ext("xlsx");
    proto::UploadFileInfoResponse empty_user_response;
    file_service_impl_->UploadFileInfo(nullptr, &empty_user_request, &empty_user_response, nullptr);
    EXPECT_EQ(empty_user_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_USER_ID_EMPTY));
    EXPECT_FALSE(empty_user_response.error_msg().empty());
}

// ==================== 获取文件信息测试 ====================

// 正常情况: 获取文件信息成功, 返回文件详情各字段
TEST_F(FileServiceImplTest, GetFileInfoSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    proto::GetFileInfoRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    proto::GetFileInfoResponse response;
    file_service_impl_->GetFileInfo(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().file_id(), file_id);
    EXPECT_EQ(response.result().file_name(), "test_excel.xlsx");
    EXPECT_EQ(response.result().file_size(), 10 * 1024);
    EXPECT_GT(response.result().upload_time(), 0);
    EXPECT_EQ(response.result().file_ext(), "xlsx");
}

// 异常情况: 文件不存在与参数为空
TEST_F(FileServiceImplTest, GetFileInfoWithError)
{
    // 文件不存在
    proto::GetFileInfoRequest not_found_request;
    not_found_request.set_request_id(kRequestId);
    not_found_request.set_session_id(kTestSessionId);
    not_found_request.set_user_id(kTestUserId);
    not_found_request.set_file_id("fid_not_exist");
    proto::GetFileInfoResponse not_found_response;
    file_service_impl_->GetFileInfo(nullptr, &not_found_request, &not_found_response, nullptr);
    EXPECT_EQ(not_found_response.error_code(), static_cast<int>(ErrorCode::FILE_DATA_NOT_FOUND));

    // 用户 ID 为空
    proto::GetFileInfoRequest empty_user_request;
    empty_user_request.set_request_id(kRequestId);
    empty_user_request.set_user_id("");
    empty_user_request.set_file_id("fid_any");
    proto::GetFileInfoResponse empty_user_response;
    file_service_impl_->GetFileInfo(nullptr, &empty_user_request, &empty_user_response, nullptr);
    EXPECT_EQ(empty_user_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_USER_ID_EMPTY));
}

// ==================== 上传文件数据测试 ====================

// 正常情况: 通过 attachment 上传文件数据成功, 完整触发 Excel 解析流程
TEST_F(FileServiceImplTest, UploadFileSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    const int error_code = CallUploadFile(kTestUserId, file_id, kTestFileContent);
    EXPECT_EQ(error_code, static_cast<int>(ErrorCode::SUCCESS));

    // 校验 MySQL 中 fastdfs_file_id 已更新
    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    const std::optional<chat_excel::file_service::FileInfo> stored_file_info =
        file_data.GetFileByFileId(file_id);
    ASSERT_TRUE(stored_file_info.has_value());
    EXPECT_FALSE(stored_file_info->fastdfs_file_id.empty());
}

// 异常情况: attachment 文件数据为空 / 文件不存在 / 属主不一致
TEST_F(FileServiceImplTest, UploadFileWithError)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    // attachment 文件数据为空
    EXPECT_EQ(CallUploadFile(kTestUserId, file_id, ""),
              static_cast<int>(ErrorCode::FILE_SERVICE_FILE_CONTENT_EMPTY));

    // 文件不存在
    EXPECT_EQ(CallUploadFile(kTestUserId, "fid_not_exist", kTestFileContent),
              static_cast<int>(ErrorCode::FILE_DATA_NOT_FOUND));

    // 当前用户与文件属主不一致
    EXPECT_EQ(CallUploadFile(kOtherUserId, file_id, kTestFileContent),
              static_cast<int>(ErrorCode::FILE_USER_MISMATCH));
}

// ==================== 下载文件数据测试 ====================

// 正常情况: 下载文件数据成功, 响应 attachment 中文件内容与上传内容一致
TEST_F(FileServiceImplTest, DownloadFileSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");
    ASSERT_EQ(CallUploadFile(kTestUserId, file_id, kTestFileContent),
              static_cast<int>(ErrorCode::SUCCESS));

    proto::DownloadFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    // 文件数据通过 brpc 控制器的响应 attachment 返回
    brpc::Controller controller;
    proto::DownloadFileResponse response;
    file_service_impl_->DownloadFile(&controller, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(controller.response_attachment().to_string(), kTestFileContent);
}

// 异常情况: 文件数据尚未上传到 FastDFS 与文件不存在
TEST_F(FileServiceImplTest, DownloadFileWithError)
{
    // 文件不存在
    proto::DownloadFileRequest not_found_request;
    not_found_request.set_request_id(kRequestId);
    not_found_request.set_user_id(kTestUserId);
    not_found_request.set_file_id("fid_not_exist");
    brpc::Controller not_found_controller;
    proto::DownloadFileResponse not_found_response;
    file_service_impl_->DownloadFile(&not_found_controller, &not_found_request, &not_found_response, nullptr);
    EXPECT_EQ(not_found_response.error_code(), static_cast<int>(ErrorCode::FILE_DATA_NOT_FOUND));

    // 文件数据尚未上传到 FastDFS
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");
    proto::DownloadFileRequest not_uploaded_request;
    not_uploaded_request.set_request_id(kRequestId);
    not_uploaded_request.set_user_id(kTestUserId);
    not_uploaded_request.set_file_id(file_id);
    brpc::Controller not_uploaded_controller;
    proto::DownloadFileResponse not_uploaded_response;
    file_service_impl_->DownloadFile(&not_uploaded_controller, &not_uploaded_request,
                                      &not_uploaded_response, nullptr);
    EXPECT_EQ(not_uploaded_response.error_code(), static_cast<int>(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR));
}

// ==================== 删除文件测试 ====================

// 正常情况: 删除文件成功, 删除后文件信息不存在
TEST_F(FileServiceImplTest, DeleteFileSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");
    ASSERT_EQ(CallUploadFile(kTestUserId, file_id, kTestFileContent),
              static_cast<int>(ErrorCode::SUCCESS));

    proto::DeleteFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    proto::DeleteFileResponse response;
    file_service_impl_->DeleteFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));

    // 校验删除后文件信息不存在
    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_FALSE(file_data.GetFileByFileId(file_id).has_value());
}

// 异常情况: 属主不一致时删除失败且数据保留
TEST_F(FileServiceImplTest, DeleteFileWithOwnerMismatch)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    proto::DeleteFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kOtherUserId);
    request.set_file_id(file_id);

    proto::DeleteFileResponse response;
    file_service_impl_->DeleteFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::FILE_USER_MISMATCH));

    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_TRUE(file_data.GetFileByFileId(file_id).has_value());
}

// ==================== 预览 Excel 文件测试 ====================

// 正常情况: 预览返回文件信息, excel_data 填充数据库子服务返回的工作表数据
TEST_F(FileServiceImplTest, PreviewExcelSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");
    // 上传文件数据以触发 Excel 解析与数据导入, 产生 WorkSheet 元信息
    ASSERT_EQ(CallUploadFile(kTestUserId, file_id, kTestFileContent),
              static_cast<int>(ErrorCode::SUCCESS));

    proto::PreviewExcelRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);
    request.set_page_number(1);
    request.set_page_size(50);

    proto::PreviewExcelResponse response;
    file_service_impl_->PreviewExcel(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().file_id(), file_id);
    EXPECT_EQ(response.result().file_name(), "test_excel.xlsx");
    EXPECT_EQ(response.result().file_size(), 10 * 1024);
    EXPECT_EQ(response.result().file_ext(), "xlsx");

    // 校验 excel_data : 每个工作表对应一个 Sheet, 表头与数据来自数据库子服务
    ASSERT_TRUE(response.result().has_excel_data());
    ASSERT_EQ(response.result().excel_data().sheets_size(), 2);
    const proto::Sheet& first_sheet = response.result().excel_data().sheets(0);
    EXPECT_EQ(first_sheet.name(), "Sheet1");
    EXPECT_EQ(first_sheet.columns_size(), 2);
    EXPECT_EQ(first_sheet.columns(0), "名称");
    EXPECT_EQ(first_sheet.columns(1), "数量");
    EXPECT_EQ(first_sheet.col_count(), 2);
    ASSERT_EQ(first_sheet.data_size(), 1);
    EXPECT_EQ(first_sheet.data(0).cells(0), "apple");
    EXPECT_EQ(first_sheet.data(0).cells(1), "10");
    EXPECT_EQ(first_sheet.total_rows(), 1);
    EXPECT_EQ(first_sheet.current_page(), 1);
    EXPECT_EQ(first_sheet.total_pages(), 1);
    EXPECT_EQ(first_sheet.page_size(), 50);
    EXPECT_EQ(response.result().excel_data().sheets(1).name(), "Sheet2");
}

// 异常情况: 分页参数从 1 开始, 0 视为参数错误
TEST_F(FileServiceImplTest, PreviewExcelWithInvalidPageReturnParamsError)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    // page_number 为 0
    proto::PreviewExcelRequest invalid_number_request;
    invalid_number_request.set_request_id(kRequestId);
    invalid_number_request.set_user_id(kTestUserId);
    invalid_number_request.set_file_id(file_id);
    invalid_number_request.set_page_number(0);
    invalid_number_request.set_page_size(50);
    proto::PreviewExcelResponse invalid_number_response;
    file_service_impl_->PreviewExcel(nullptr, &invalid_number_request, &invalid_number_response, nullptr);
    EXPECT_EQ(invalid_number_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_PAGE_NUMBER_ERROR));

    // page_size 为 0
    proto::PreviewExcelRequest invalid_size_request;
    invalid_size_request.set_request_id(kRequestId);
    invalid_size_request.set_user_id(kTestUserId);
    invalid_size_request.set_file_id(file_id);
    invalid_size_request.set_page_number(1);
    invalid_size_request.set_page_size(0);
    proto::PreviewExcelResponse invalid_size_response;
    file_service_impl_->PreviewExcel(nullptr, &invalid_size_request, &invalid_size_response, nullptr);
    EXPECT_EQ(invalid_size_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_PAGE_SIZE_ERROR));
}

// ==================== 获取文件列表测试 ====================

// 正常情况: 只返回当前用户上传的文件, chat_session_id 暂未关联填充空字符串
TEST_F(FileServiceImplTest, GetFileListSuccess)
{
    CallUploadFileInfo("first.xlsx", "xlsx");
    CallUploadFileInfo("second.xlsx", "xlsx");

    proto::GetFileListRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);

    proto::GetFileListResponse response;
    file_service_impl_->GetFileList(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.result().file_list_size(), 2);
    for (const proto::FileListItem& file_list_item : response.result().file_list())
    {
        EXPECT_EQ(file_list_item.file_size(), 10 * 1024);
        EXPECT_GT(file_list_item.upload_time(), 0);
        EXPECT_TRUE(file_list_item.chat_session_id().empty());
    }
}

// 异常情况: 用户 ID 为空返回参数错误
TEST_F(FileServiceImplTest, GetFileListWithEmptyUserReturnParamsError)
{
    proto::GetFileListRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id("");

    proto::GetFileListResponse response;
    file_service_impl_->GetFileList(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_USER_ID_EMPTY));
}

// ==================== 关联文件和聊天会话测试 ====================

// 正常情况: 关联暂未实现, 调用返回成功
TEST_F(FileServiceImplTest, HandleFileChatSessionMapSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");

    proto::HandleFileChatSessionMapRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);
    request.set_chat_session_id("chat_session_for_test");

    proto::HandleFileChatSessionMapResponse response;
    file_service_impl_->HandleFileChatSessionMap(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
}

// 异常情况: 聊天会话 ID 为空返回参数错误
TEST_F(FileServiceImplTest, HandleFileChatSessionMapWithEmptyParamsReturnParamsError)
{
    proto::HandleFileChatSessionMapRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id("fid_any");
    request.set_chat_session_id("");

    proto::HandleFileChatSessionMapResponse response;
    file_service_impl_->HandleFileChatSessionMap(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_CHAT_SESSION_ID_EMPTY));
}

// ==================== 上传 SQLite 文件测试 ====================

// 正常情况: 上传 SQLite 文件成功, 扩展名固定 .db, 文件大小取 attachment 大小
TEST_F(FileServiceImplTest, UploadSQLiteFileSuccess)
{
    const std::string file_content = "mock sqlite binary content for rpc test";

    proto::UploadSQLiteFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_filename("test.db");

    // 文件数据通过 brpc 控制器的请求 attachment 传输
    brpc::Controller controller;
    controller.request_attachment().append(file_content);

    proto::UploadSQLiteFileResponse response;
    file_service_impl_->UploadSQLiteFile(&controller, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().file_id().size(), 32u);

    // 通过获取文件信息接口校验扩展名与文件大小
    proto::GetFileInfoRequest get_info_request;
    get_info_request.set_request_id(kRequestId);
    get_info_request.set_user_id(kTestUserId);
    get_info_request.set_file_id(response.result().file_id());
    proto::GetFileInfoResponse get_info_response;
    file_service_impl_->GetFileInfo(nullptr, &get_info_request, &get_info_response, nullptr);
    EXPECT_EQ(get_info_response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(get_info_response.result().file_ext(), ".db");
    EXPECT_EQ(get_info_response.result().file_size(),
              static_cast<int64_t>(file_content.size()));
}

// 异常情况: attachment 文件数据为空返回参数错误
TEST_F(FileServiceImplTest, UploadSQLiteFileWithEmptyAttachmentReturnParamsError)
{
    proto::UploadSQLiteFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_filename("test.db");

    brpc::Controller controller;
    proto::UploadSQLiteFileResponse response;
    file_service_impl_->UploadSQLiteFile(&controller, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_FILE_CONTENT_EMPTY));
}

// ==================== 获取 SQLite 文件测试 ====================

// 正常情况: 获取 SQLite 文件成功, 返回非空 FastDFS 文件 ID
TEST_F(FileServiceImplTest, GetSQLiteFileSuccess)
{
    const std::string file_content = "mock sqlite binary content for rpc test";
    const std::string file_id = CallUploadFileInfo("test.db", "db");
    ASSERT_EQ(CallUploadFile(kTestUserId, file_id, file_content), static_cast<int>(ErrorCode::SUCCESS));

    proto::GetSQLiteFileRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    proto::GetSQLiteFileResponse response;
    file_service_impl_->GetSQLiteFile(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_FALSE(response.result().fdfs_file_id().empty());
}

// 异常情况: 文件数据尚未上传到 FastDFS 与属主不一致
TEST_F(FileServiceImplTest, GetSQLiteFileWithError)
{
    // 文件数据尚未上传到 FastDFS
    const std::string file_id = CallUploadFileInfo("test.db", "db");
    proto::GetSQLiteFileRequest not_uploaded_request;
    not_uploaded_request.set_request_id(kRequestId);
    not_uploaded_request.set_user_id(kTestUserId);
    not_uploaded_request.set_file_id(file_id);
    proto::GetSQLiteFileResponse not_uploaded_response;
    file_service_impl_->GetSQLiteFile(nullptr, &not_uploaded_request, &not_uploaded_response, nullptr);
    EXPECT_EQ(not_uploaded_response.error_code(), static_cast<int>(ErrorCode::FILE_FDFS_DOWNLOAD_ERROR));

    // 当前用户与文件属主不一致
    proto::GetSQLiteFileRequest mismatch_request;
    mismatch_request.set_request_id(kRequestId);
    mismatch_request.set_user_id(kOtherUserId);
    mismatch_request.set_file_id(file_id);
    proto::GetSQLiteFileResponse mismatch_response;
    file_service_impl_->GetSQLiteFile(nullptr, &mismatch_request, &mismatch_response, nullptr);
    EXPECT_EQ(mismatch_response.error_code(), static_cast<int>(ErrorCode::FILE_USER_MISMATCH));
}

// ==================== 获取 Worksheet 数据库表名列表测试 ====================

// 正常情况: 上传 Excel 文件后获取表名列表, 格式为 {file_id}_{worksheet_name}
TEST_F(FileServiceImplTest, GetWorksheetDBTablesSuccess)
{
    const std::string file_id = CallUploadFileInfo("test_excel.xlsx", "xlsx");
    ASSERT_EQ(CallUploadFile(kTestUserId, file_id, kTestFileContent),
              static_cast<int>(ErrorCode::SUCCESS));

    proto::GetWorksheetDBTablesRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    proto::GetWorksheetDBTablesResponse response;
    file_service_impl_->GetWorksheetDBTables(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.request_id(), kRequestId);
    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    ASSERT_EQ(response.result().worksheet_db_tables_size(), 2);
    EXPECT_EQ(response.result().worksheet_db_tables(0), file_id + "_Sheet1");
    EXPECT_EQ(response.result().worksheet_db_tables(1), file_id + "_Sheet2");
}

// 边界情况: SQLite 文件没有 WorkSheet 信息时返回空列表
TEST_F(FileServiceImplTest, GetWorksheetDBTablesForSqliteFileReturnEmptyList)
{
    const std::string file_id = CallUploadFileInfo("test.db", "db");

    proto::GetWorksheetDBTablesRequest request;
    request.set_request_id(kRequestId);
    request.set_session_id(kTestSessionId);
    request.set_user_id(kTestUserId);
    request.set_file_id(file_id);

    proto::GetWorksheetDBTablesResponse response;
    file_service_impl_->GetWorksheetDBTables(nullptr, &request, &response, nullptr);

    EXPECT_EQ(response.error_code(), static_cast<int>(ErrorCode::SUCCESS));
    EXPECT_EQ(response.result().worksheet_db_tables_size(), 0);
}

// 异常情况: 文件不存在与参数为空
TEST_F(FileServiceImplTest, GetWorksheetDBTablesWithError)
{
    // 文件不存在
    proto::GetWorksheetDBTablesRequest not_found_request;
    not_found_request.set_request_id(kRequestId);
    not_found_request.set_user_id(kTestUserId);
    not_found_request.set_file_id("fid_not_exist");
    proto::GetWorksheetDBTablesResponse not_found_response;
    file_service_impl_->GetWorksheetDBTables(nullptr, &not_found_request, &not_found_response, nullptr);
    EXPECT_EQ(not_found_response.error_code(), static_cast<int>(ErrorCode::FILE_DATA_NOT_FOUND));

    // 用户 ID 为空
    proto::GetWorksheetDBTablesRequest empty_user_request;
    empty_user_request.set_request_id(kRequestId);
    empty_user_request.set_user_id("");
    empty_user_request.set_file_id("fid_any");
    proto::GetWorksheetDBTablesResponse empty_user_response;
    file_service_impl_->GetWorksheetDBTables(nullptr, &empty_user_request, &empty_user_response, nullptr);
    EXPECT_EQ(empty_user_response.error_code(), static_cast<int>(ErrorCode::FILE_SERVICE_USER_ID_EMPTY));
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "file_service_impl_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
