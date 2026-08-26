#!/bin/bash
# ============================================================================
# 网关用户子服务 9 个 HTTP 接口 curl 集成测试脚本
#
# 测试前置条件 :
#   1. docker 容器已启动 : mysql(3306) / redis(6379) / etcd(2379), 密码均为 123456
#   2. 用户子服务已启动 :
#      ./build/svc_user_service/user_service --mysql_password=123456 --redis_password=123456
#   3. 网关子服务已启动 :
#      ./build/svc_gateway_service/gateway_service
#
# 响应格式约定 :
#   HTTP 状态码统一 200, 处理结果通过响应体 errorCode 表达 :
#     0    : 成功
#     400  : 网关层错误(请求体 JSON 解析失败 / 必填参数缺失)
#     503  : 网关层错误(用户子服务不可用 / RPC 调用失败)
#     1xx  : 后端业务错误码透传(如 108 用户不存在, 109 密码错误, 110 验证码错误,
#            111 会话不存在, 113 昵称已存在, 114 邮箱已存在)
#
# 数据状态检查辅助命令(redis 中 key 为 hash 类型) :
#   会话缓存   : docker exec redis redis-cli -a 123456 HKEYS session_data
#   验证码缓存 : docker exec redis redis-cli -a 123456 HGETALL verifycode_data
#   用户缓存   : docker exec redis redis-cli -a 123456 HKEYS user_data
#   用户表     : docker exec mysql mysql -uroot -p123456 chat_excel \
#                  -e "SELECT user_id, nickname, email, status FROM tbl_user;"
#   会话表     : docker exec mysql mysql -uroot -p123456 chat_excel \
#                  -e "SELECT session_id, user_id FROM tbl_session;"
# ============================================================================

# 网关服务地址
GATEWAY_URL="http://127.0.0.1:8080"

# 测试用户信息(执行前确保数据库中不存在该用户, 或先手动清理)
TEST_NICKNAME="nick_curl_test"
TEST_EMAIL="curl_test@qq.com"
TEST_PASSWORD="pass123456"

# 运行过程中提取的动态值
SESSION_ID=""
CODE_ID=""
VERIFY_CODE=""

# ----------------------------------------------------------------------------
# 辅助函数 : 从 JSON 响应中提取指定字段的值
# 参数 : $1 为 JSON 响应字符串, $2 为字段路径(如 result.sessionId)
# ----------------------------------------------------------------------------
ExtractJsonField()
{
    echo "$1" | python3 -c "
import sys, json
data = json.load(sys.stdin)
value = data
for key in '$2'.split('.'):
    value = value.get(key, '') if isinstance(value, dict) else ''
print(value)
"
}

# ==================== U01 检测用户昵称是否唯一 ====================

echo "======== U01 检测用户昵称是否唯一 ========"
# 预期 : errorCode=0, 昵称未被占用(测试前置 : 数据库中无该昵称)
curl -s -X POST ${GATEWAY_URL}/api/user/valid/nickname \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u01-1\",\"nickname\":\"${TEST_NICKNAME}\"}"
echo ""

# 预期 : errorCode=113, 昵称已存在
# 注意 : 本脚本 U03 注册发生在 U01 之后, 此用例需在 U03 注册成功后执行
#        (或修改昵称为数据库中已存在的用户昵称, 如下方的 nick_rpc_test)
# curl -s -X POST ${GATEWAY_URL}/api/user/valid/nickname \
#     -H "Content-Type: application/json" \
#     -d '{"requestId":"req-u01-2","nickname":"nick_rpc_test"}'
# 数据状态 : 唯一性检查只读缓存不写数据, MySQL/Redis 均无变化

# 预期 : errorCode=400, 缺少必填参数 nickname
curl -s -X POST ${GATEWAY_URL}/api/user/valid/nickname \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u01-3"}'
echo ""

# ==================== U02 检测邮箱是否唯一 ====================

echo "======== U02 检测邮箱是否唯一 ========"
# 预期 : errorCode=0, 邮箱未被占用
curl -s -X POST ${GATEWAY_URL}/api/user/valid/email \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u02-1\",\"email\":\"${TEST_EMAIL}\"}"
echo ""

# 预期 : errorCode=114, 邮箱已存在
# 注意 : 同 U01, 需在 U03 注册成功后执行
# curl -s -X POST ${GATEWAY_URL}/api/user/valid/email \
#     -H "Content-Type: application/json" \
#     -d "{\"requestId\":\"req-u02-2\",\"email\":\"${TEST_EMAIL}\"}"
# 数据状态 : 唯一检查不产生数据变更

# 预期 : errorCode=400, 请求体非法 JSON(无法反序列化)
curl -s -X POST ${GATEWAY_URL}/api/user/valid/email \
    -H "Content-Type: application/json" \
    -d 'invalid-json'
echo ""

# ==================== U03 用户注册 ====================

echo "======== U03 用户注册 ========"
# 预期 : errorCode=0, 注册成功
# 数据状态 : MySQL tbl_user 新增记录(status=0 未登录, 密码为 bcrypt 哈希 $2b$12$ 前缀),
#           Redis 无缓存(注册只写库)
curl -s -X POST ${GATEWAY_URL}/api/user/register \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u03-1\",\"nickname\":\"${TEST_NICKNAME}\",\"password\":\"${TEST_PASSWORD}\",\"email\":\"${TEST_EMAIL}\"}"
echo ""

# 预期 : errorCode=100, 重复注册相同昵称
# 说明 : 后端注册逻辑无唯一性预检查, 依赖 MySQL 唯一索引冲突抛异常返回 100;
#        实际调用方应先调 U01/U02 确认昵称邮箱唯一后再注册
curl -s -X POST ${GATEWAY_URL}/api/user/register \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u03-2\",\"nickname\":\"${TEST_NICKNAME}\",\"password\":\"${TEST_PASSWORD}\",\"email\":\"other@qq.com\"}"
echo ""

# 预期 : errorCode=100, 重复注册相同邮箱(原因同上)
curl -s -X POST ${GATEWAY_URL}/api/user/register \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u03-3\",\"nickname\":\"other_nick\",\"password\":\"${TEST_PASSWORD}\",\"email\":\"${TEST_EMAIL}\"}"
echo ""

# 预期 : errorCode=400, 缺少必填参数 password
curl -s -X POST ${GATEWAY_URL}/api/user/register \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u03-4\",\"nickname\":\"nick_x\",\"email\":\"x@qq.com\"}"
echo ""

# ==================== U04 密码登录 ====================

echo "======== U04 密码登录 ========"
# 预期 : errorCode=0, 返回 result.sessionId(用户名可以是昵称)
# 数据状态 : MySQL tbl_user.status 置为 1(已登录), tbl_session 新增会话记录,
#           Redis session_data 新增 field, user_data 中该用户旧缓存被删除(登录后失效)
RESP=$(curl -s -X POST ${GATEWAY_URL}/api/user/passwd/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u04-1\",\"username\":\"${TEST_NICKNAME}\",\"password\":\"${TEST_PASSWORD}\"}")
echo "${RESP}"
SESSION_ID=$(ExtractJsonField "${RESP}" "result.sessionId")
echo "提取 sessionId: ${SESSION_ID}"

# 预期 : errorCode=0, 返回新的 sessionId(用户名可以是邮箱, 同一用户可有多个会话)
curl -s -X POST ${GATEWAY_URL}/api/user/passwd/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u04-2\",\"username\":\"${TEST_EMAIL}\",\"password\":\"${TEST_PASSWORD}\"}"
echo ""

# 预期 : errorCode=109, 密码错误
curl -s -X POST ${GATEWAY_URL}/api/user/passwd/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u04-3\",\"username\":\"${TEST_NICKNAME}\",\"password\":\"wrong_password\"}"
echo ""

# 预期 : errorCode=108, 用户不存在
# 数据状态 : 失败的登录不产生会话, 不改变用户状态
curl -s -X POST ${GATEWAY_URL}/api/user/passwd/login \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u04-4","username":"no_such_user","password":"pass123456"}'
echo ""

# 预期 : errorCode=400, 缺少必填参数 password
curl -s -X POST ${GATEWAY_URL}/api/user/passwd/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u04-5\",\"username\":\"${TEST_NICKNAME}\"}"
echo ""

# ==================== U05 获取验证码 ====================

echo "======== U05 获取验证码 ========"
# 预期 : errorCode=0, 返回 result.codeId
# 数据状态 : Redis verifycode_data 新增 field(verifycode:{codeId}), 验证码为 6 位数字, TTL 60 秒
RESP=$(curl -s -X POST ${GATEWAY_URL}/api/user/code \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u05-1\",\"email\":\"${TEST_EMAIL}\"}")
echo "${RESP}"
CODE_ID=$(ExtractJsonField "${RESP}" "result.codeId")
echo "提取 codeId: ${CODE_ID}"

# 预期 : errorCode=0, 再次获取返回新的 codeId(旧验证码仍有效直至 TTL 过期)
curl -s -X POST ${GATEWAY_URL}/api/user/code \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u05-2\",\"email\":\"${TEST_EMAIL}\"}"
echo ""

# 预期 : errorCode=400, 邮箱为空
curl -s -X POST ${GATEWAY_URL}/api/user/code \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u05-3"}'
echo ""

# 测试替身 : 从 Redis 读取验证码(模拟用户从邮箱获取验证码)
# 说明 : 生产环境验证码通过邮件发送给用户, 测试环境直接从缓存读取
VERIFY_CODE=$(docker exec redis redis-cli -a 123456 \
    HGET verifycode_data "verifycode:${CODE_ID}" 2>/dev/null | \
    python3 -c "import sys, json; print(json.load(sys.stdin)['verify_code'])" 2>/dev/null)
echo "从 Redis 提取验证码: ${VERIFY_CODE} (codeId: ${CODE_ID})"

# ==================== U06 验证码登录 ====================

echo "======== U06 验证码登录 ========"
# 预期 : errorCode=110, 验证码错误
curl -s -X POST ${GATEWAY_URL}/api/user/vcode/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u06-1\",\"email\":\"${TEST_EMAIL}\",\"verifyCode\":\"000000\",\"codeId\":\"${CODE_ID}\"}"
echo ""

# 预期 : errorCode=0, 返回 result.sessionId
# 数据状态 : MySQL tbl_user.status 置为 1, tbl_session 新增会话, Redis session_data 新增 field;
#           登录成功后网关会调用 DeleteVerifyCode RPC 删除验证码防止重复使用
RESP=$(curl -s -X POST ${GATEWAY_URL}/api/user/vcode/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u06-2\",\"email\":\"${TEST_EMAIL}\",\"verifyCode\":\"${VERIFY_CODE}\",\"codeId\":\"${CODE_ID}\"}")
echo "${RESP}"
SESSION_ID=$(ExtractJsonField "${RESP}" "result.sessionId")
echo "提取 sessionId: ${SESSION_ID}"

# 验证登录后验证码已被删除(预期输出 0 表示 field 不存在, 验证码失效)
echo -n "登录后 Redis 验证码存在性(预期 0 已删除) : "
docker exec redis redis-cli -a 123456 \
    HEXISTS verifycode_data "verifycode:${CODE_ID}" 2>/dev/null
echo ""

# 预期 : errorCode=110, 重复使用已删除的验证码登录失败(防重放)
curl -s -X POST ${GATEWAY_URL}/api/user/vcode/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u06-3\",\"email\":\"${TEST_EMAIL}\",\"verifyCode\":\"${VERIFY_CODE}\",\"codeId\":\"${CODE_ID}\"}"
echo ""

# 预期 : errorCode=400, 缺少必填参数 codeId
curl -s -X POST ${GATEWAY_URL}/api/user/vcode/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u06-4\",\"email\":\"${TEST_EMAIL}\",\"verifyCode\":\"123456\"}"
echo ""

# ==================== U07 会话登录 ====================

echo "======== U07 会话登录 ========"
# 预期 : errorCode=0, 会话有效
# 数据状态 : 会话登录不新建会话; 若用户已下线(status=0)则会话登录会恢复登录态(status=1)
curl -s -X POST ${GATEWAY_URL}/api/user/session/login \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u07-1\",\"sessionId\":\"${SESSION_ID}\"}"
echo ""

# 预期 : errorCode=111, 会话不存在或已失效
curl -s -X POST ${GATEWAY_URL}/api/user/session/login \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u07-2","sessionId":"invalid-session-id-000"}'
echo ""

# 预期 : errorCode=400, sessionId 为空
curl -s -X POST ${GATEWAY_URL}/api/user/session/login \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u07-3","sessionId":""}'
echo ""

# ==================== U09 获取用户信息 ====================

echo "======== U09 获取用户信息 ========"
# 注意 : 本接口参数全部在 URL query 中, 不在请求体中
# 预期 : errorCode=0, 返回 result.userInfo(userId/nickname/email)
# 数据状态 : Redis user_data 回写用户缓存(3 个 field, userId/nickname/email 三索引)
curl -s -X POST \
    "${GATEWAY_URL}/api/user/info?requestId=req-u09-1&sessionId=${SESSION_ID}"
echo ""

# 预期 : errorCode=111, 会话无效(鉴权失败透传后端错误码)
curl -s -X POST \
    "${GATEWAY_URL}/api/user/info?requestId=req-u09-2&sessionId=invalid-session-id-000"
echo ""

# 预期 : errorCode=400, 缺少必填参数 sessionId
curl -s -X POST "${GATEWAY_URL}/api/user/info?requestId=req-u09-3"
echo ""

# 预期 : errorCode=0, 重复获取(验证缓存命中路径)
curl -s -X POST \
    "${GATEWAY_URL}/api/user/info?requestId=req-u09-4&sessionId=${SESSION_ID}"
echo ""

# ==================== U08 退出登录 ====================

echo "======== U08 退出登录 ========"
# 预期 : errorCode=0, 退出成功
# 数据状态 : MySQL tbl_user.status 置为 0(下线), tbl_session 删除该会话记录,
#           Redis session_data 删除对应 field, user_data 删除该用户缓存
curl -s -X POST ${GATEWAY_URL}/api/user/logout \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u08-1\",\"sessionId\":\"${SESSION_ID}\"}"
echo ""

# 预期 : errorCode=111, 重复退出已删除的会话(鉴权失败)
curl -s -X POST ${GATEWAY_URL}/api/user/logout \
    -H "Content-Type: application/json" \
    -d "{\"requestId\":\"req-u08-2\",\"sessionId\":\"${SESSION_ID}\"}"
echo ""

# 预期 : errorCode=111, 无效会话退出(鉴权失败)
curl -s -X POST ${GATEWAY_URL}/api/user/logout \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u08-3","sessionId":"invalid-session-id-000"}'
echo ""

# 预期 : errorCode=400, 缺少必填参数 sessionId
curl -s -X POST ${GATEWAY_URL}/api/user/logout \
    -H "Content-Type: application/json" \
    -d '{"requestId":"req-u08-4"}'
echo ""

# ==================== 退出后连锁状态验证 ====================

echo "======== 退出后连锁状态验证 ========"
# 预期 : errorCode=111, 已退出的会话获取用户信息失败(会话已删除)
curl -s -X POST \
    "${GATEWAY_URL}/api/user/info?requestId=req-u09-5&sessionId=${SESSION_ID}"
echo ""

# 数据状态检查 : 用户已下线(预期 status=0), 会话表无该会话记录
docker exec mysql mysql -uroot -p123456 chat_excel \
    -e "SELECT nickname, status FROM tbl_user WHERE nickname='${TEST_NICKNAME}';" 2>/dev/null
docker exec redis redis-cli -a 123456 HKEYS session_data 2>/dev/null
docker exec redis redis-cli -a 123456 HKEYS user_data 2>/dev/null
echo "(以上两行分别为 Redis 会话缓存与用户缓存的 field 列表, 已退出的会话与用户缓存应不在其中)"

# ==================== 后端服务不可用容错测试 ====================

echo "======== 后端服务不可用容错测试 ========"
# 测试方法 : 停止用户子服务(kill 进程), 等待 ETCD 租约 TTL 过期(约 10 秒)后请求
# 预期 : errorCode=503, 网关返回后端服务不可用(含 brpc 错误详情)
# 说明 : 需手动停止用户子服务后执行以下命令
# curl -s -X POST ${GATEWAY_URL}/api/user/valid/nickname \
#     -H "Content-Type: application/json" \
#     -d '{"requestId":"req-503-1","nickname":"some_nick"}'

# 预期 : HTTP 200, 网关健康检测接口, 返回 healthy(不依赖任何后端子服务)
curl -s ${GATEWAY_URL}/health
echo ""

# ==================== 测试数据清理(可选) ====================

echo "======== 测试数据清理(可选, 取消注释执行) ========"
# 清理本次测试产生的用户与会话数据
# docker exec mysql mysql -uroot -p123456 chat_excel \
#     -e "DELETE FROM tbl_session WHERE user_id=(SELECT user_id FROM tbl_user WHERE nickname='${TEST_NICKNAME}'); \
#         DELETE FROM tbl_user WHERE nickname='${TEST_NICKNAME}';" 2>/dev/null
# docker exec redis redis-cli -a 123456 DEL session_data user_data verifycode_data 2>/dev/null
