#pragma once

#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis.h>
#include "svc_user_service/common.h"

namespace chat_excel
{
namespace user_service
{

/**
 * @brief 验证码数据访问类, 封装验证码数据的 Redis 缓存操作
 *        只提供数据的增删查改接口, 验证码的生成与校验等业务逻辑由上层实现;
 *        缓存结构为 hash 类型, key 为 verifycode_data, field 为 verifycode:{verifycode_id},
 *        value 为包含验证码 ID, 验证码, 用户邮箱, 创建时间的 JSON 字符串, 过期时间 1 分钟
 */
class VerifyCodeData
{
public:
    /**
     * @brief 构造函数, 注入 Redis 操作句柄
     * @param redis_handle Redis 操作句柄, 由上层创建并统一管理
     */
    explicit VerifyCodeData(std::shared_ptr<sw::redis::Redis> redis_handle);

    /**
     * @brief 保存验证码信息到缓存, 通过事务批量执行写入与过期时间设置
     * @param verifycode_info 验证码信息
     */
    void SaveVerifyCode(const VerifyCodeInfo& verifycode_info);

    /**
     * @brief 通过验证码 ID 获取验证码信息
     * @param verifycode_id 验证码 ID
     * @return 验证码信息, 缓存未命中或数据损坏时返回 std::nullopt
     */
    std::optional<VerifyCodeInfo> GetVerifyCodeByVerifyCodeId(const std::string& verifycode_id);

    /**
     * @brief 通过验证码 ID 删除缓存中的验证码信息
     * @param verifycode_id 验证码 ID
     */
    void DeleteVerifyCodeByVerifyCodeId(const std::string& verifycode_id);

private:
    // Redis 操作句柄
    std::shared_ptr<sw::redis::Redis> redis_handle_;
};

} // namespace user_service
} // namespace chat_excel
