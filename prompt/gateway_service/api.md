# ChatExcel 网关子服务 API 
## 全局约定

- 内容类型：`application/json`（二进制上传/下载接口除外）
- 时间戳：Unix 秒级整数

### 通用响应信封（除特殊说明外，所有接口的响应都套用此结构）

```json
{
  "requestId": "string（回显请求ID）",
  "errorCode": 0,
  "errorMsg": "",
  "result": "object 或不出现（仅当接口有返回数据时存在）"
}
```

- `errorCode == 0` 表示成功，此时 `errorMsg` 为空字符串
- 错误码含义：`0` 成功 / `400` 请求参数错误 / `500` 服务内部错误 / `503` 后端服务不可用

### 通用请求字段

- `requestId`：字符串，所有请求必填
- `sessionId`：字符串，凡涉及登录态的接口必填；健康检测、昵称/邮箱校验、注册、密码登录、获取验证码、验证码登录这几个接口不需要
- `chatSessionId`：字符串，标识与 AI 模型的聊天会话（注意与 `sessionId` 区分，后者标识登录用户）

### ID 语义对照表

| ID 字段                      | 含义                                         |
| -------------------------- | ------------------------------------------   |
| sessionId                  | 登录会话，区分当前登录用户                       |
| chatSessionId              | 聊天会话，区分与模型的不同对话                    |
| codeId                     | 验证码ID，与 verifyCode 配对用于验证码登录或注册  |
| fileId                     | 上传文件ID                                     |
| connectionId / dbConnectId | 数据库连接ID（默认连接 `excel_default` 专用于智能Excel场景） |

---

## 接口列表（共 30 个）

每个接口的格式：编号 | 名称 | 方法 路径 | 请求 | 响应（只写 `result` 内部字段，通用信封省略）。  
类型标注：`str` 字符串、`int` 整数、`bool` 布尔。

### 分组 0：健康检测（1 个）

```
[H01] 健康检测
GET /health
鉴权：无需登录
响应（完整结构，不套用通用信封）：
{ "status": "healthy", "service": "GatewayService", "timestamp": 1706428800 }
```

### 分组 1：用户子服务（9 个）

```
[U01] 检测用户昵称是否唯一
POST /api/user/valid/nickname
请求体：{ "requestId": str, "nickname": str }
返回结果：无

[U02] 检测邮箱是否唯一
POST /api/user/valid/email
请求体：{ "requestId": str, "email": str }
返回结果：无

[U03] 用户注册
POST /api/user/register
请求体：{ "requestId": str, "nickname": str, "password": str, "email": str, "verifyCode": str, "codeId": str }
返回结果：无
注意：verifyCode 与 codeId 通过 [U05] 获取验证码接口获得，验证码校验通过才能注册成功

[U04] 密码登录
POST /api/user/passwd/login
请求体：{ "requestId": str, "username": str /* 昵称或邮箱 */, "password": str }
返回结果：{ "sessionId": str }

[U05] 获取验证码（发送到用户邮箱）
POST /api/user/code
请求体：{ "requestId": str, "email": str }
返回结果：{ "codeId": str }   // codeId + verifyCode 配对用于 [U06]

[U06] 验证码登录
POST /api/user/vcode/login
请求体：{ "requestId": str, "email": str, "verifyCode": str, "codeId": str }
返回结果：{ "sessionId": str }

[U07] 会话登录（用已有 sessionId 恢复登录态）
POST /api/user/session/login
请求体：{ "requestId": str, "sessionId": str }
返回结果：无

[U08] 退出登录
POST /api/user/logout
请求体：{ "requestId": str, "sessionId": str }
返回结果：无

[U09] 获取用户信息
POST /api/user/info?requestId={requestId}&sessionId={sessionId}
请求体：无（参数全部在 query 中）
返回结果：{ "userInfo": { "userId": str, "nickname": str, "email": str } }
注意：用户 ID 不再由请求传入，网关通过会话鉴权获取会话所属用户 ID
```

### 分组 2：文件子服务（9 个）

```
[F01] 上传文件信息（第一步：登记元数据，返回 fileId）
POST /api/file/upload/info
请求体：{
  "requestId": str, "sessionId": str,
  "fileInfo": { "filename": str, "fileSize": int, "fileExt": str },  // 仅支持 .xlsx
  "chatSessionId": str  // 可选；上传时可不填，上传成功后用 [F08] 建立映射
}
返回结果：{ "fileId": str }

[F02] 获取文件信息
GET /api/file/info?requestId={requestId}&sessionId={sessionId}&fileId={fileId}
返回结果：{ "fileId": str, "fileName": str, "fileSize": int, "uploadTime": int, "fileExt": str }

[F03] 上传文件数据（第二步：二进制上传，配合 [F01] 返回的 fileId）
POST /api/file/upload?requestId={requestId}&sessionId={sessionId}&fileId={fileId}
请求头：Content-Type: application/octet-stream
请求体：文件二进制数据
返回结果：{ "fileId": str }

[F04] 下载文件
GET /api/file/download?requestId={requestId}&sessionId={sessionId}&fileId={fileId}
响应：文件二进制流
响应头：Content-Type: application/octet-stream
       Content-Disposition: attachment; filename="文件名"

[F05] 删除文件
DELETE /api/file/{fileId}?requestId={requestId}&sessionId={sessionId}
（fileId 为路径参数）
返回结果：无  // 成功时 errorMsg = "删除成功"

[F06] 预览 Excel 文件（分页）
POST /api/file/preview
请求体：{ "requestId": str, "sessionId": str, "fileId": str, "pageNumber": int /* 可选，默认1 */, "pageSize": int /* 可选，默认50 */ }
返回结果：{
  "fileId": str, "fileName": str, "fileSize": int, "fileExt": str,
  "excelData": { "sheets": [ Sheet ] }
}
Sheet 结构 = {
  "name": str,               // worksheet 名称
  "totalRows": int, "colCount": int,       // 总行数、总列数
  "currentPage": int, "totalPages": int, "pageSize": int,  // 分页信息
  "columns": [str, ...],     // 列名列表
  "data": [[str, ...], ...]  // 行数据，二维数组
}

[F07] 获取用户文件列表
POST /api/file/list
请求体：{ "requestId": str, "sessionId": str }
返回结果：{ "fileList": [ { "fileId": str, "fileName": str, "fileSize": int, "uploadTime": int } ] }

[F08] 关联文件和聊天会话映射
POST /api/file/chat/map
请求体：{ "requestId": str, "sessionId": str, "fileId": str, "chatSessionId": str }
返回结果：无

[F09] 上传 SQLite 文件（二进制）
POST /api/file/sqlite/upload?requestId={requestId}&sessionId={sessionId}&filename={filename}
请求头：Content-Type: application/octet-stream
请求体：SQLite 文件二进制数据
返回结果：{ "fileId": str }
```

### 分组 3：存储子服务（5 个）

```
[D01] 新建数据库连接
POST /api/db/connect
请求体：{
  "requestId": str, "sessionId": str,
  "database": {
    "type": "MySQL" 或 "SQLite",
    "MySQL":  { "host": str, "port": int, "name": str, "username": str, "password": str, "charset": str },  // type=MySQL 时填写
    "SQLite": { "fileId": str, "readonly": bool /* 默认 false */ }                                        // type=SQLite 时填写
  }
}
返回结果：{ "connectionId": str }
注意：
- 程序启动时会建立默认 MySQL 连接，connectionId = "excel_default"，专用于智能Excel场景
- 智能DB场景需用户自行提交数据库信息；目前仅支持 MySQL 和 SQLite

[D02] 断开数据库连接
POST /api/db/disconnect
请求体：{ "requestId": str, "sessionId": str, "connectionId": str }
返回结果：无

[D03] 获取数据库表列表
GET /api/db/tables?requestId={requestId}&sessionId={sessionId}&dbConnectId={dbConnectId}
返回结果：{ "tables": [str, ...] }

[D04] 获取表数据
POST /api/db/table/data
请求体：{ "requestId": str, "sessionId": str, "dbConnectId": str, "tableName": str, "forceOriginal": bool /* 默认false，true=强制获取原始表数据 */ }
返回结果：{
  "tableSchema": {
    "columnInfo": [ { "name": str, "type": str } ],   // 列信息
    "tableData":  { "rows": [ { "cells": [str, ...] } ] }  // 行数据
  }
}

[D05] 获取连接状态
POST /api/db/connection/status
请求体：{ "requestId": str, "sessionId": str, "dbConnectId": str }
返回结果：{ "tempTables": [str, ...], "hasModifications": bool }
注意：修改类 SQL 不直接操作原表，而是复制一份新表（如 users → users_temp）在新表上修改；tempTables 即新表名列表。
```

### 分组 4：AI 子服务（6 个）

```
[A01] 获取支持模型列表
POST /api/ai/models
请求体：{ "requestId": str, "sessionId": str }
返回结果：{ "modelList": [ { "modelName": str, "modelDesc": str } ] }

[A02] 新建聊天会话
POST /api/ai/session/create
请求体：{ "requestId": str, "sessionId": str, "modelName": str, "sessionType": str, "dbConnectionInfo": str }
sessionType：会话类型，必填，仅支持 excel/database/plain
dbConnectionInfo：数据库连接信息JSON，database 类型会话必填，excel 类型不填
返回结果：{ "chatSessionId": str, "modelName": str }
注意：会话标题不透传，首条聊天消息后会自动更新为会话标题

[A03] 获取聊天会话列表
POST /api/ai/chatSessionLists
请求体：{ "requestId": str, "sessionId": str }
返回结果：{ "chatSessionLists": [ {
  "chatSessionId": str, "modelName": str, "title": str,
  "createdAt": int, "updatedAt": int, "messageCount": int,
  "firstUserMessageContent": str, "sessionType": str, "dbConnectionInfo": str
} ] }

[A04] 获取指定聊天会话历史消息
POST /api/ai/history
请求体：{ "requestId": str, "sessionId": str, "chatSessionId": str }
返回结果：{
  "messageList": [ { "id": str, "role": "user"|"assistant", "content": str, "timestamp": int } ],
  "fileId": str,                              // 关联文件ID
  "sessionType": "excel"|"database",          // 会话类型
  "dbConnectionInfo": str /* JSON字符串 */    // 数据库场景下的连接信息
}

[A05] 删除指定聊天会话
POST /api/ai/delete
请求体：{ "requestId": str, "sessionId": str, "chatSessionId": str }
返回结果：无

[A06] 发送消息（流式，SSE）
POST /api/ai/sendStreamMessage
请求体：{
  "requestId": str, "sessionId": str, "chatSessionId": str,
  "chatType": "plain"|"excel"|"database",   // 会话类型
  "message": str,                            // 用户消息内容
  "fileId": str,          // 可选，Excel场景
  "dbConnectId": str,     // 可选，数据库场景
  "dbType": str,          // 可选，数据库场景，确定操作哪个数据库
  "tableName": str        // 可选，数据库场景，多表名用逗号分隔
}
响应：SSE 流式响应（不套用通用信封）：
  data: {"content": "消息片段", "done": false, "errorCode": 0, "errorMsg": ""}
  data: {"content": "更多消息", "done": false, "errorCode": 0, "errorMsg": ""}
  ...
  data: [DONE]
字段说明：content=消息片段(str)，done=是否结束(bool)，errorCode=0表示成功，errorMsg=错误信息
```

---

## 快速索引表（方法 + 路径 + 用途）

| 编号  | 方法     | 路径                        | 用途              |
| --- | ------ | ------------------------- | --------------- |
| H01 | GET    | /health                   | 健康检测            |
| U01 | POST   | /api/user/valid/nickname  | 昵称唯一性校验         |
| U02 | POST   | /api/user/valid/email     | 邮箱唯一性校验         |
| U03 | POST   | /api/user/register        | 用户注册            |
| U04 | POST   | /api/user/passwd/login    | 密码登录            |
| U05 | POST   | /api/user/code            | 获取邮箱验证码         |
| U06 | POST   | /api/user/vcode/login     | 验证码登录           |
| U07 | POST   | /api/user/session/login   | 会话登录            |
| U08 | POST   | /api/user/logout          | 退出登录            |
| U09 | POST   | /api/user/info            | 获取用户信息（query参数） |
| F01 | POST   | /api/file/upload/info     | 登记文件元数据         |
| F02 | GET    | /api/file/info            | 获取文件信息          |
| F03 | POST   | /api/file/upload          | 上传文件二进制         |
| F04 | GET    | /api/file/download        | 下载文件            |
| F05 | DELETE | /api/file/{fileId}        | 删除文件            |
| F06 | POST   | /api/file/preview         | 分页预览Excel       |
| F07 | POST   | /api/file/list            | 用户文件列表          |
| F08 | POST   | /api/file/chat/map        | 文件↔聊天会话映射       |
| F09 | POST   | /api/file/sqlite/upload   | 上传SQLite文件      |
| D01 | POST   | /api/db/connect           | 新建数据库连接         |
| D02 | POST   | /api/db/disconnect        | 断开数据库连接         |
| D03 | GET    | /api/db/tables            | 数据库表列表          |
| D04 | POST   | /api/db/table/data        | 获取表数据           |
| D05 | POST   | /api/db/connection/status | 获取连接状态          |
| A01 | POST   | /api/ai/models            | 模型列表            |
| A02 | POST   | /api/ai/session/create    | 新建聊天会话          |
| A03 | POST   | /api/ai/chatSessionLists  | 聊天会话列表          |
| A04 | POST   | /api/ai/history           | 历史消息            |
| A05 | POST   | /api/ai/delete            | 删除聊天会话          |
| A06 | POST   | /api/ai/sendStreamMessage | 流式发送消息（SSE）     |

## 典型调用链（接口依赖关系参考）

1. **注册登录流程**：U01/U02 唯一性校验 → U05 获取验证码 → U03 注册（携带 codeId + verifyCode）→ U04 密码登录（或 U06 验证码登录）→ 获得 sessionId
2. **Excel 分析流程**：F01 登记元数据 → F03 上传二进制 → F08 绑定 chatSessionId → A02 新建聊天会话 → A06 流式对话（chatType=excel，携带 fileId）→ A04 查历史消息
3. **数据库分析流程**：F09 上传 SQLite 文件 或 D01 连接 MySQL → D03/D04 浏览表结构和数据 → A06 流式对话（chatType=database，携带 dbConnectId）→ D05 查看修改状态
