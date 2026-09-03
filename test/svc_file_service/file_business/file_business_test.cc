#include "svc_file_service/file_business.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <brpc/server.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/odb.h>
#include <cpp-toolkit/rpc.h>
#include <cpp-toolkit/redis.h>
#include <ai_service.pb.h>
#include <database_service.pb.h>
#include <excel_parse_service.pb.h>
#include <file_service.pb.h>
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

using chat_excel::ChatExcelException;
using chat_excel::ErrorCode;
using chat_excel::file_service::FileBusiness;
using chat_excel::file_service::FileData;
using chat_excel::file_service::FileInfo;
using chat_excel::file_service::WorkSheetData;
using chat_excel::file_service::WorkSheetInfo;

namespace
{

// proto 生成类型命名空间别名
namespace proto = ::chat_excel_proto::excel_parse_service;
namespace db_proto = ::chat_excel_proto::database_service;
namespace ai_proto = ::chat_excel_proto::ai_service;

// mock Excel 解析子服务监听端口
constexpr int kExcelParseMockServerPort = 28992;

// Excel 解析子服务名称(与 FileBusiness 实现中的常量保持一致)
constexpr const char* kExcelParseServiceName = "ExcelParseService";

// mock 数据库子服务监听端口
constexpr int kDatabaseMockServerPort = 28993;

// 数据库子服务名称(与 FileBusiness 实现中的常量保持一致)
constexpr const char* kDatabaseServiceName = "DataBaseService";

// mock AI 子服务监听端口
constexpr int kAiServiceMockServerPort = 28994;

// AI 子服务名称(与 FileBusiness 实现中的常量保持一致)
constexpr const char* kAiServiceName = "AIService";

// Excel 数据库全局连接 ID(与数据库子服务连接管理器中的全局连接 ID 保持一致)
constexpr const char* kExcelDbConnectionId = "excel_connection";

// FastDFS tracker 服务器地址(本地 docker 容器)
constexpr const char* kFdfsTrackerAddr = "127.0.0.1:22122";

// 测试用户 ID 与会话 ID(长度受表结构 VARCHAR(32) 限制)
constexpr const char* kTestUserId = "uid_for_fb_test";
constexpr const char* kOtherUserId = "uid_other_fb_test";
constexpr const char* kTestSessionId = "sid_for_fb_test";

// 测试请求 ID
constexpr const char* kRequestId = "rid_for_fb_test";

// 文件缓存与 WorkSheet 缓存 hash 类型的 key(与数据层实现保持一致)
constexpr const char* kFileCacheKey = "file_data";
constexpr const char* kWorkSheetCacheKey = "worksheet_data";

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
 *        用于验证 FileBusiness 上传文件数据的完整流程
 */
class MockExcelParserServiceImpl : public proto::ExcelParserService
{
public:
    void GetWorksheets(google::protobuf::RpcController* /*controller*/,
                       const proto::GetWorksheetsRequest* request,
                       proto::GetWorksheetsResponse* response,
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
                    const proto::ParseExcelRequest* request,
                    proto::ParseExcelResponse* response,
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
            proto::WorksheetData* worksheet = response->add_worksheets();
            worksheet->set_name(worksheet_name);
            worksheet->set_total_rows(1);
            worksheet->set_total_cols(2);

            proto::ProtoColumnInfo* name_column = worksheet->add_columns();
            name_column->set_name("名称");
            name_column->set_type("String");
            proto::ProtoColumnInfo* count_column = worksheet->add_columns();
            count_column->set_name("数量");
            count_column->set_type("Integer");

            proto::RowData* row = worksheet->add_rows();
            proto::ProtoCellData* name_cell = row->add_cells();
            name_cell->set_value("apple");
            name_cell->set_type("String");
            proto::ProtoCellData* count_cell = row->add_cells();
            count_cell->set_value("10");
            count_cell->set_type("Integer");
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
        db_proto::ColumnInfo* name_column = table_schema->add_column_info();
        name_column->set_name("名称");
        name_column->set_type("String");
        db_proto::ColumnInfo* count_column = table_schema->add_column_info();
        count_column->set_name("数量");
        count_column->set_type("Integer");

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

/**
 * @brief mock AI 子服务 : 校验更新会话文件关联请求参数非空,
 *        UpdateSessionFile 返回成功, 其余接口使用基类默认实现
 */
class MockAIServiceImpl : public ai_proto::AIService
{
public:
    void UpdateSessionFile(google::protobuf::RpcController* /*controller*/,
                           const ai_proto::UpdateSessionFileRequest* request,
                           ai_proto::UpdateSessionFileResponse* response,
                           google::protobuf::Closure* done) override
    {
        brpc::ClosureGuard closure_guard(done);
        response->set_request_id(request->request_id());

        // 校验业务层传递的用户 ID/会话 ID/文件 ID 非空
        if (request->user_id().empty() || request->chat_session_id().empty() ||
            request->file_id().empty())
        {
            response->set_error_code(static_cast<int>(ErrorCode::AI_SERVICE_INTERNAL_ERROR));
            response->set_error_msg("mock: 更新会话文件关联请求参数为空");
            return;
        }

        response->set_error_code(static_cast<int>(ErrorCode::SUCCESS));
    }
};

/**
 * @brief 获取 mock AI 子服务监听地址, 服务器进程内只启动一次
 * @return "ip:port" 格式的监听地址字符串
 */
const std::string& GetAiMockServerAddr()
{
    static const std::string server_addr = [] {
        static MockAIServiceImpl ai_service_impl;
        static brpc::Server server;
        brpc::ServerOptions server_options;
        server.AddService(&ai_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE);
        if (server.Start(kAiServiceMockServerPort, &server_options) != 0)
        {
            GTEST_LOG_(FATAL) << "mock AI 子服务启动失败, 端口: " << kAiServiceMockServerPort;
        }
        return "127.0.0.1:" + std::to_string(kAiServiceMockServerPort);
    }();
    return server_addr;
}

/**
 * @brief 断言业务调用抛出指定错误码的异常
 * @param call 业务调用 lambda
 * @param expected_code 期望的错误码
 */
template <typename Call>
void ExpectBusinessError(Call call, ErrorCode expected_code)
{
    try
    {
        call();
        FAIL() << "预期抛出 ChatExcelException 但未抛出";
    }
    catch (const ChatExcelException& e)
    {
        EXPECT_EQ(e.error_code(), expected_code);
    }
}

} // namespace

// 文件业务逻辑类测试夹具, 每个用例执行前清理数据库/缓存中的测试数据
class FileBusinessTest : public ::testing::Test
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

        // 构建信道管理对象, 注册关心服务后添加 mock Excel 解析子服务、
        // mock 数据库子服务与 mock AI 子服务地址
        // (AddService 只对 SetCareService 预注册的关心服务生效)
        channel_manager_ = std::make_shared<cpp_toolkit::ChannelManager>();
        channel_manager_->SetCareService(kExcelParseServiceName);
        channel_manager_->AddService(kExcelParseServiceName, GetExcelParseMockServerAddr());
        channel_manager_->SetCareService(kDatabaseServiceName);
        channel_manager_->AddService(kDatabaseServiceName, GetDatabaseMockServerAddr());
        channel_manager_->SetCareService(kAiServiceName);
        channel_manager_->AddService(kAiServiceName, GetAiMockServerAddr());

        // 构建文件业务逻辑对象, 依赖对象全部由外部注入
        const std::shared_ptr<FileData> file_data =
            std::make_shared<FileData>(GetMysqlHandle(), GetRedisHandle());
        const std::shared_ptr<WorkSheetData> worksheet_data =
            std::make_shared<WorkSheetData>(GetMysqlHandle(), GetRedisHandle());
        file_business_ = std::make_unique<FileBusiness>(file_data, worksheet_data, channel_manager_);
    }

    /**
     * @brief 上传测试文件信息(测试用户上传 xlsx 文件), 返回生成的文件 ID
     */
    std::string UploadTestFileInfo()
    {
        return file_business_->UploadFileInfo(kRequestId, kTestUserId, kTestSessionId,
                                              "test_excel.xlsx", "xlsx", 10 * 1024);
    }

    // 文件业务逻辑对象
    std::unique_ptr<FileBusiness> file_business_;

    // RPC 信道管理对象
    cpp_toolkit::ChannelManager::Ptr channel_manager_;
};

// 上传文件信息 : 生成 32 字符文件 ID 并成功保存文件元信息
TEST_F(FileBusinessTest, UploadFileInfoGeneratesFileId)
{
    const std::string file_id = UploadTestFileInfo();
    // uuid 去掉连字符后为 32 字符
    EXPECT_EQ(file_id.size(), 32u);

    const FileInfo file_info = file_business_->GetFileInfo(kRequestId, kTestUserId, file_id);
    EXPECT_EQ(file_info.file_id, file_id);
    EXPECT_EQ(file_info.file_name, "test_excel.xlsx");
    EXPECT_EQ(file_info.file_extension, "xlsx");
    EXPECT_EQ(file_info.file_size, 10 * 1024u);
    EXPECT_GT(file_info.file_upload_time, 0u);
    EXPECT_TRUE(file_info.fastdfs_file_id.empty());
    EXPECT_EQ(file_info.user_id, kTestUserId);
    EXPECT_EQ(file_info.session_id, kTestSessionId);
}

// 获取文件信息 : 缓存命中时数据库记录被删除仍能读取(Cache-Aside 读策略)
TEST_F(FileBusinessTest, GetFileInfoCacheAsideHit)
{
    const std::string file_id = UploadTestFileInfo();

    // 第一次读取会回填缓存
    const FileInfo file_info = file_business_->GetFileInfo(kRequestId, kTestUserId, file_id);
    EXPECT_EQ(file_info.file_id, file_id);

    // 删除 MySQL 中的记录后再读取, 缓存命中仍能获取文件信息
    odb::transaction transaction(GetMysqlHandle()->begin());
    GetMysqlHandle()->execute("DELETE FROM tbl_file_info");
    transaction.commit();
    const FileInfo cached_file_info = file_business_->GetFileInfo(kRequestId, kTestUserId, file_id);
    EXPECT_EQ(cached_file_info.file_id, file_id);
}

// 获取文件信息 : 文件不存在时抛出异常
TEST_F(FileBusinessTest, GetFileInfoNotFound)
{
    ExpectBusinessError(
        [this] { file_business_->GetFileInfo(kRequestId, kTestUserId, "fid_not_exist"); },
        ErrorCode::FILE_DATA_NOT_FOUND);
}

// 获取文件信息 : 当前用户与文件属主不一致时抛出异常
TEST_F(FileBusinessTest, GetFileInfoOwnerMismatch)
{
    const std::string file_id = UploadTestFileInfo();
    ExpectBusinessError(
        [this, &file_id] { file_business_->GetFileInfo(kRequestId, kOtherUserId, file_id); },
        ErrorCode::FILE_USER_MISMATCH);
}

// 上传文件数据 : 完整流程(FastDFS 上传 + 元信息更新 + WorkSheet 保存, 传递 FastDFS 文件 ID 解析)
TEST_F(FileBusinessTest, UploadFileDataFullFlow)
{
    const std::string file_id = UploadTestFileInfo();
    const std::string file_content = "mock excel binary content for upload test";

    file_business_->UploadFileData(kRequestId, kTestUserId, file_id, file_content);

    // 校验 MySQL 中 fastdfs_file_id 已更新
    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    const std::optional<FileInfo> stored_file_info = file_data.GetFileByFileId(file_id);
    ASSERT_TRUE(stored_file_info.has_value());
    EXPECT_FALSE(stored_file_info->fastdfs_file_id.empty());

    // 校验 WorkSheet 元信息已保存, 表名称格式为 {file_id}_{worksheet_name}
    WorkSheetData worksheet_data(GetMysqlHandle(), GetRedisHandle());
    const std::vector<WorkSheetInfo> worksheet_list =
        worksheet_data.GetWorkSheetListByFileId(file_id);
    ASSERT_EQ(worksheet_list.size(), 2u);
    EXPECT_EQ(worksheet_list[0].file_id, file_id);
    EXPECT_EQ(worksheet_list[0].worksheet_name, "Sheet1");
    EXPECT_EQ(worksheet_list[0].table_name, file_id + "_Sheet1");
    EXPECT_EQ(worksheet_list[1].file_id, file_id);
    EXPECT_EQ(worksheet_list[1].worksheet_name, "Sheet2");
    EXPECT_EQ(worksheet_list[1].table_name, file_id + "_Sheet2");

    // 校验 FastDFS 中文件数据可下载且内容一致
    const std::string downloaded_content =
        file_business_->DownloadFileData(kRequestId, kTestUserId, file_id);
    EXPECT_EQ(downloaded_content, file_content);
}

// 上传文件数据 : 当前用户与文件属主不一致时抛出异常
TEST_F(FileBusinessTest, UploadFileDataOwnerMismatch)
{
    const std::string file_id = UploadTestFileInfo();
    ExpectBusinessError(
        [this, &file_id] { file_business_->UploadFileData(kRequestId, kOtherUserId, file_id, "content"); },
        ErrorCode::FILE_USER_MISMATCH);
}

// 上传文件数据 : 文件不存在时抛出异常
TEST_F(FileBusinessTest, UploadFileDataFileNotFound)
{
    ExpectBusinessError(
        [this] { file_business_->UploadFileData(kRequestId, kTestUserId, "fid_not_exist", "content"); },
        ErrorCode::FILE_DATA_NOT_FOUND);
}

// 下载文件数据 : 文件数据尚未上传到 FastDFS 时抛出异常
TEST_F(FileBusinessTest, DownloadFileDataNotUploaded)
{
    const std::string file_id = UploadTestFileInfo();
    ExpectBusinessError(
        [this, &file_id] { file_business_->DownloadFileData(kRequestId, kTestUserId, file_id); },
        ErrorCode::FILE_FDFS_DOWNLOAD_ERROR);
}

// 下载文件数据 : 当前用户与文件属主不一致时抛出异常
TEST_F(FileBusinessTest, DownloadFileDataOwnerMismatch)
{
    const std::string file_id = UploadTestFileInfo();
    file_business_->UploadFileData(kRequestId, kTestUserId, file_id, "content");
    ExpectBusinessError(
        [this, &file_id] { file_business_->DownloadFileData(kRequestId, kOtherUserId, file_id); },
        ErrorCode::FILE_USER_MISMATCH);
}

// 删除文件 : 完整流程(FastDFS + WorkSheet 元信息 + MySQL + 缓存全部删除)
TEST_F(FileBusinessTest, DeleteFileFullFlow)
{
    const std::string file_id = UploadTestFileInfo();
    file_business_->UploadFileData(kRequestId, kTestUserId, file_id, "content for delete test");

    // 记录 FastDFS 文件 ID, 用于删除后校验 FastDFS 文件已被删除
    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    const std::optional<FileInfo> stored_file_info = file_data.GetFileByFileId(file_id);
    ASSERT_TRUE(stored_file_info.has_value());
    const std::string fastdfs_file_id = stored_file_info->fastdfs_file_id;

    file_business_->DeleteFile(kRequestId, kTestUserId, file_id);

    // 校验 MySQL 与缓存中的文件信息已删除
    EXPECT_FALSE(file_data.GetFileByFileId(file_id).has_value());
    EXPECT_FALSE(file_data.GetFileByFileIdFromCache(file_id).has_value());

    // 校验 WorkSheet 元信息已删除
    WorkSheetData worksheet_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_TRUE(worksheet_data.GetWorkSheetListByFileId(file_id).empty());

    // 校验 FastDFS 中的文件数据已删除(下载失败)
    std::string downloaded_content;
    EXPECT_FALSE(cpp_toolkit::FdfsClient::DownloadToBuffer(fastdfs_file_id, downloaded_content));
}

// 删除文件 : 当前用户与文件属主不一致时抛出异常且不删除任何数据
TEST_F(FileBusinessTest, DeleteFileOwnerMismatch)
{
    const std::string file_id = UploadTestFileInfo();
    ExpectBusinessError(
        [this, &file_id] { file_business_->DeleteFile(kRequestId, kOtherUserId, file_id); },
        ErrorCode::FILE_USER_MISMATCH);

    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_TRUE(file_data.GetFileByFileId(file_id).has_value());
}

// 删除文件信息 : 校验属主后删除数据库与缓存中的文件信息以及 WorkSheet 数据
TEST_F(FileBusinessTest, DeleteFileInfoSuccess)
{
    const std::string file_id = UploadTestFileInfo();
    // 上传文件数据以产生 WorkSheet 元信息
    file_business_->UploadFileData(kRequestId, kTestUserId, file_id, "content for delete info test");

    file_business_->DeleteFileInfo(kRequestId, kTestUserId, file_id);

    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_FALSE(file_data.GetFileByFileId(file_id).has_value());
    EXPECT_FALSE(file_data.GetFileByFileIdFromCache(file_id).has_value());

    // 校验 WorkSheet 元信息已删除
    WorkSheetData worksheet_data(GetMysqlHandle(), GetRedisHandle());
    EXPECT_TRUE(worksheet_data.GetWorkSheetListByFileId(file_id).empty());
}

// 获取文件列表 : 只返回当前用户上传的文件
TEST_F(FileBusinessTest, GetFileListReturnsOnlyUserFiles)
{
    UploadTestFileInfo();
    UploadTestFileInfo();
    file_business_->UploadFileInfo(kRequestId, kOtherUserId, kTestSessionId,
                                   "other.xlsx", "xlsx", 10 * 1024);

    const std::vector<FileInfo> file_list = file_business_->GetFileList(kRequestId, kTestUserId);
    ASSERT_EQ(file_list.size(), 2u);
    for (const FileInfo& file_info : file_list)
    {
        EXPECT_EQ(file_info.user_id, kTestUserId);
    }

    const std::vector<FileInfo> other_file_list =
        file_business_->GetFileList(kRequestId, kOtherUserId);
    EXPECT_EQ(other_file_list.size(), 1u);
}

// 预览 Excel 文件 : 通过数据库子服务返回文件信息与各工作表的表结构及分页表数据
TEST_F(FileBusinessTest, PreviewExcelReturnsFileInfo)
{
    const std::string file_id = UploadTestFileInfo();
    // 上传文件数据以产生 WorkSheet 元信息(mock 解析出 Sheet1 与 Sheet2 两个工作表)
    file_business_->UploadFileData(kRequestId, kTestUserId, file_id, "content for preview test");

    chat_excel_proto::file_service::ExcelData excel_data;
    const FileInfo file_info =
        file_business_->PreviewExcel(kRequestId, kTestUserId, file_id, 1, 10, false, &excel_data);
    EXPECT_EQ(file_info.file_id, file_id);
    EXPECT_EQ(file_info.file_name, "test_excel.xlsx");

    // 校验预览数据 : 每个工作表对应一个 Sheet, 表头与数据来自数据库子服务
    ASSERT_EQ(excel_data.sheets_size(), 2);
    EXPECT_EQ(excel_data.sheets(0).name(), "Sheet1");
    EXPECT_EQ(excel_data.sheets(0).columns_size(), 2);
    EXPECT_EQ(excel_data.sheets(0).columns(0), "名称");
    EXPECT_EQ(excel_data.sheets(0).columns(1), "数量");
    EXPECT_EQ(excel_data.sheets(0).col_count(), 2);
    ASSERT_EQ(excel_data.sheets(0).data_size(), 1);
    EXPECT_EQ(excel_data.sheets(0).data(0).cells(0), "apple");
    EXPECT_EQ(excel_data.sheets(0).data(0).cells(1), "10");
    EXPECT_EQ(excel_data.sheets(0).total_rows(), 1);
    EXPECT_EQ(excel_data.sheets(0).current_page(), 1);
    EXPECT_EQ(excel_data.sheets(0).total_pages(), 1);
    EXPECT_EQ(excel_data.sheets(0).page_size(), 10);
    EXPECT_EQ(excel_data.sheets(1).name(), "Sheet2");
}

// 预览 Excel 文件 : 当前用户与文件属主不一致时抛出异常
TEST_F(FileBusinessTest, PreviewExcelOwnerMismatch)
{
    const std::string file_id = UploadTestFileInfo();
    chat_excel_proto::file_service::ExcelData excel_data;
    ExpectBusinessError(
        [this, &file_id, &excel_data]
        { file_business_->PreviewExcel(kRequestId, kOtherUserId, file_id, 1, 10, false, &excel_data); },
        ErrorCode::FILE_USER_MISMATCH);
}

// 上传 SQLite 文件数据与获取 SQLite 文件 : FastDFS 上传后返回文件 ID 且内容可下载一致
TEST_F(FileBusinessTest, UploadSQLiteFileAndGet)
{
    const std::string file_id = file_business_->UploadFileInfo(
        kRequestId, kTestUserId, kTestSessionId, "test.db", "db", 5 * 1024);
    const std::string file_content = "mock sqlite binary content";

    file_business_->UploadSQLiteFileData(kRequestId, kTestUserId, file_id, file_content);

    // 校验 MySQL 中 fastdfs_file_id 已更新
    FileData file_data(GetMysqlHandle(), GetRedisHandle());
    const std::optional<FileInfo> stored_file_info = file_data.GetFileByFileId(file_id);
    ASSERT_TRUE(stored_file_info.has_value());
    EXPECT_FALSE(stored_file_info->fastdfs_file_id.empty());

    // 获取 SQLite 文件对应的 FastDFS 文件 ID 并校验文件内容可下载且一致
    const std::string fdfs_file_id =
        file_business_->GetSQLiteFile(kRequestId, kTestUserId, file_id);
    EXPECT_EQ(fdfs_file_id, stored_file_info->fastdfs_file_id);
    std::string downloaded_content;
    EXPECT_TRUE(cpp_toolkit::FdfsClient::DownloadToBuffer(fdfs_file_id, downloaded_content));
    EXPECT_EQ(downloaded_content, file_content);
}

// 获取 SQLite 文件 : 文件数据尚未上传到 FastDFS 时抛出异常
TEST_F(FileBusinessTest, GetSQLiteFileNotUploaded)
{
    const std::string file_id = file_business_->UploadFileInfo(
        kRequestId, kTestUserId, kTestSessionId, "test.db", "db", 5 * 1024);
    ExpectBusinessError(
        [this, &file_id] { file_business_->GetSQLiteFile(kRequestId, kTestUserId, file_id); },
        ErrorCode::FILE_FDFS_DOWNLOAD_ERROR);
}

// 获取 SQLite 文件 : 当前用户与文件属主不一致时抛出异常
TEST_F(FileBusinessTest, GetSQLiteFileOwnerMismatch)
{
    const std::string file_id = file_business_->UploadFileInfo(
        kRequestId, kTestUserId, kTestSessionId, "test.db", "db", 5 * 1024);
    file_business_->UploadSQLiteFileData(kRequestId, kTestUserId, file_id, "content");
    ExpectBusinessError(
        [this, &file_id] { file_business_->GetSQLiteFile(kRequestId, kOtherUserId, file_id); },
        ErrorCode::FILE_USER_MISMATCH);
}

// 关联文件和聊天会话 : 暂未实现, 调用不抛出异常
TEST_F(FileBusinessTest, HandleFileChatSessionMapDoesNotThrow)
{
    const std::string file_id = UploadTestFileInfo();
    EXPECT_NO_THROW(
        file_business_->HandleFileChatSessionMap(kRequestId, kTestUserId, file_id, "chat_session_1"));
}

int main(int argc, char** argv)
{
    // 初始化日志输出到控制台, 便于观察业务层日志
    // loggerName 必须非空 : spdlog 注册中心已存在名为空字符串的默认 logger, 空名称会因重名抛出异常
    cpp_toolkit::logger_settings settings;
    settings.async = false;
    settings.loggerName = "file_business_test";
    settings.loggerFile = "stdout";
    cpp_toolkit::Logger::initLogger(settings);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
