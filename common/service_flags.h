#pragma once
#include <gflags/gflags.h>

// 各子服务名称 gflags 参数声明(定义在 common/service_flags.cc),
// 各子服务 RPC 调用与监控列表等需要使用其他子服务名称的场景统一通过
// FLAGS_xxx_service 获取, 禁止硬编码服务名称, 参数值可在 chat_data.conf 中配置

// 用户子服务名称
DECLARE_string(user_service);

// 文件子服务名称
DECLARE_string(file_service);

// 网关服务名称
DECLARE_string(gateway_service);

// AI 子服务名称
DECLARE_string(ai_service);

// Excel 解析子服务名称
DECLARE_string(excel_parse_service);

// 通知子服务名称
DECLARE_string(notify_service);

// 数据库子服务名称
DECLARE_string(database_service);
