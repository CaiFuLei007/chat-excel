#include "verifycode_data.h"

#include <chrono>
#include <jsoncpp/json/json.h>
#include <sw/redis++/redis++.h>
#include <cpp-toolkit/logger.h>
#include <cpp-toolkit/util.h>
#include "common/exception.h"

namespace chat_excel
{
namespace user_service
{

namespace
{

// 验证码缓存 hash 类型的 key
constexpr const char* kVerifyCodeCacheKey = "verifycode_data";

// 验证码缓存 field 前缀, 拼接验证码 ID 构成完整 field
constexpr const char* kVerifyCodeFieldPrefix = "verifycode:";

// 验证码缓存过期时间(秒), 1 分钟
constexpr int kVerifyCodeCacheExpireTime = 60;

// JSON 字段名 : 验证码 ID
constexpr const char* kJsonVerifyCodeId = "verifycode_id";

// JSON 字段名 : 验证码
constexpr const char* kJsonVerifyCode = "verify_code";

// JSON 字段名 : 用户邮箱
constexpr const char* kJsonEmail = "email";

// JSON 字段名 : 创建时间
constexpr const char* kJsonCreateTime = "create_time";

/**
 * @brief 将验证码信息序列化为 JSON 字符串
 * @param verifycode_info 验证码信息
 * @param json_str 输出的 JSON 字符串
 * @return 序列化成功返回 true, 失败返回 false
 */
bool SerializeVerifyCodeInfo(const VerifyCodeInfo& verifycode_info, std::string& json_str)
{
    Json::Value json;
    json[kJsonVerifyCodeId] = verifycode_info.verifycode_id;
    json[kJsonVerifyCode] = verifycode_info.verify_code;
    json[kJsonEmail] = verifycode_info.email;
    json[kJsonCreateTime] = verifycode_info.create_time;
    return cpp_toolkit::JsonUtil::SerializeCompact(json, json_str);
}

/**
 * @brief 将 JSON 字符串反序列化为验证码信息
 * @param json_str JSON 字符串
 * @return 反序列化成功返回验证码信息, JSON 格式错误时返回 std::nullopt
 */
std::optional<VerifyCodeInfo> DeserializeVerifyCodeInfo(const std::string& json_str)
{
    Json::Value json;
    if (!cpp_toolkit::JsonUtil::UnSerialize(json, json_str))
    {
        return std::nullopt;
    }

    VerifyCodeInfo verifycode_info;
    verifycode_info.verifycode_id = json[kJsonVerifyCodeId].asString();
    verifycode_info.verify_code = json[kJsonVerifyCode].asString();
    verifycode_info.email = json[kJsonEmail].asString();
    verifycode_info.create_time = json[kJsonCreateTime].asString();
    return verifycode_info;
}

} // namespace

VerifyCodeData::VerifyCodeData(std::shared_ptr<sw::redis::Redis> redis_handle)
    : redis_handle_(std::move(redis_handle))
{
}

void VerifyCodeData::SaveVerifyCode(const VerifyCodeInfo& verifycode_info)
{
    // 序列化验证码信息为 JSON 字符串
    std::string json_str;
    if (!SerializeVerifyCodeInfo(verifycode_info, json_str))
    {
        ERR("保存验证码信息到缓存时序列化失败, verifycode_id: {}", verifycode_info.verifycode_id);
        throw ChatExcelException(ErrorCode::VERIFYCODE_DATA_SERIALIZE_ERROR);
    }

    try
    {
        // 写入 field 与设置过期时间是批量操作, 使用事务(MULTI/EXEC)
        // 保证 field 的写入与过期时间的设置整体原子生效
        const std::string verifycode_field =
            std::string(kVerifyCodeFieldPrefix) + verifycode_info.verifycode_id;

        auto transaction = redis_handle_->transaction();
        transaction.hset(kVerifyCodeCacheKey, verifycode_field, json_str)
            .expire(kVerifyCodeCacheKey, std::chrono::seconds(kVerifyCodeCacheExpireTime));
        transaction.exec();
        INFO("保存验证码信息到缓存成功, verifycode_id: {}", verifycode_info.verifycode_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("保存验证码信息到缓存失败, verifycode_id: {}, 错误: {}",
            verifycode_info.verifycode_id, e.what());
        throw ChatExcelException(ErrorCode::VERIFYCODE_DATA_REDIS_ERROR);
    }
}

std::optional<VerifyCodeInfo> VerifyCodeData::GetVerifyCodeByVerifyCodeId(const std::string& verifycode_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string verifycode_field = std::string(kVerifyCodeFieldPrefix) + verifycode_id;
        sw::redis::OptionalString value = redis_handle_->hget(kVerifyCodeCacheKey, verifycode_field);
        if (!value)
        {
            return std::nullopt;
        }

        // 反序列化失败(缓存数据损坏)视为缓存未命中, 由上层重新生成验证码
        return DeserializeVerifyCodeInfo(*value);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过验证码 ID 获取验证码信息失败, verifycode_id: {}, 错误: {}", verifycode_id, e.what());
        throw ChatExcelException(ErrorCode::VERIFYCODE_DATA_REDIS_ERROR);
    }
}

void VerifyCodeData::DeleteVerifyCodeByVerifyCodeId(const std::string& verifycode_id)
{
    try
    {
        // 单命令操作直接执行, 无需 pipeline
        const std::string verifycode_field = std::string(kVerifyCodeFieldPrefix) + verifycode_id;
        redis_handle_->hdel(kVerifyCodeCacheKey, verifycode_field);
        INFO("通过验证码 ID 删除缓存验证码信息成功, verifycode_id: {}", verifycode_id);
    }
    catch (const sw::redis::Error& e)
    {
        ERR("通过验证码 ID 删除缓存验证码信息失败, verifycode_id: {}, 错误: {}", verifycode_id, e.what());
        throw ChatExcelException(ErrorCode::VERIFYCODE_DATA_REDIS_ERROR);
    }
}

} // namespace user_service
} // namespace chat_excel
