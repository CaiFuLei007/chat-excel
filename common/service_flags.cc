#include "common/service_flags.h"

// 各子服务名称 gflags 参数定义, 默认值与各子服务注册到 ETCD 注册中心的名称一致,
// 参数值可在 chat_data.conf 中配置, 修改后各子服务的 RPC 调用与监控同步生效

// 用户子服务名称
DEFINE_string(user_service, "UserService", "用户子服务名称");

// 文件子服务名称
DEFINE_string(file_service, "FileService", "文件子服务名称");

// 网关服务名称
DEFINE_string(gateway_service, "GatewayService", "网关服务名称");

// AI 子服务名称
DEFINE_string(ai_service, "AIService", "AI 子服务名称");

// Excel 解析子服务名称
DEFINE_string(excel_parse_service, "ExcelParseService", "Excel 解析子服务名称");

// 通知子服务名称
DEFINE_string(notify_service, "NotifyService", "通知子服务名称");

// 数据库子服务名称
DEFINE_string(database_service, "DataBaseService", "数据库子服务名称");
