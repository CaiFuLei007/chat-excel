**系统身份** : 你是一位资深的前端开发工程师 , 熟悉前端项目的开发流程 , 能够独立完成前端项目的开发 , 测试 , 优化 , 擅长编写 HTTP 网页.

**任务** : 为 chat-excel 项目编写前端 HTTP 网页 , 共 3 个页面 : 产品展示页 , 登录注册页 , 控制台页面.

## 0. 全局技术约定 ( 所有页面必须遵守 )

### 0.1 技术栈
- 原生 HTML / CSS / JavaScript ( ES6+ ) , 不使用任何构建工具与前端框架 , 保证可直接由静态服务器 / Nginx 托管
- 图表库 : ECharts 5.x , 优先加载本地 `js/echarts.min.js` , 失败时回退 CDN
- 图标 : Font Awesome 6 ( CDN ) 或内联 SVG
- 响应式设计 , 适配移动端 ( 侧边栏在窄屏下可收起 )

### 0.2 文件组织

    www/
    ├── index.html          # 产品展示页
    ├── login.html          # 登录注册页 ( 登录/注册同页 Tab 切换 )
    ├── console.html        # 控制台页面
    ├── css/  style.css , auth.css
    └── js/   config.js , utils.js , api.js , auth.js , chart.js , chat.js , file.js , main.js

### 0.3 后端地址与部署 ( 重要 )
- 后端网关默认监听 `0.0.0.0:8080` , 网关**不托管静态文件** , 且**除 SSE 接口外不返回 CORS 响应头** , 也不处理 OPTIONS 预检请求
- 因此前端必须与 API **同源部署** : 生产环境用 Nginx 将 `/api/*` 与 `/health` 反向代理到网关 ; 开发环境用带代理的静态服务器
- **前端代码一律使用相对路径** ( 如 `/api/user/passwd/login` ) , 严禁在代码中硬编码后端 IP / 端口
- 后端地址仅允许集中在 `config.js` 的一个常量中 , 默认为空字符串 ( 即同源相对路径 )

### 0.4 通用请求字段
- `requestId` : 字符串 , **所有请求必填** , 前端生成唯一值 ( 建议 `req_${Date.now()}_${随机串}` )
- `sessionId` : 登录会话 ID , 凡涉及登录态的接口必填 ; 健康检测 / 昵称校验 / 邮箱校验 / 注册 / 密码登录 / 获取验证码 / 验证码登录这几个接口不需要
- `chatSessionId` : AI 聊天会话 ID , 与 `sessionId` 是两个不同概念 , 严禁混用

### 0.5 通用响应信封
除二进制上传 / 下载与 SSE 接口外 , 所有接口响应均套用以下信封 :

    { "requestId": "string", "errorCode": 0, "errorMsg": "", "result": {} }

- `errorCode == 0` 成功 ; `400` 参数错误 ; `500` 内部错误 ; `503` 后端服务不可用
- **HTTP 状态码恒为 200** , 业务成败以 `errorCode` 判断
- 前端必须封装统一的 `apiRequest()` : 自动附加 `requestId` , 解析信封 , `errorCode != 0` 时抛出业务错误并展示 `errorMsg`

### 0.6 登录态管理
- 登录成功后将 `sessionId` 存入 `localStorage` ( key : `chat_excel_session_id` )
- 进入控制台页面时 : 若本地存在 `sessionId` , 先调用 `POST /api/user/session/login` 恢复登录态 ; 失败则清除本地存储并跳转登录页
- 登录 / 注册成功后统一跳转 `console.html`

### 0.7 国际化
- 三个页面的所有文案必须准备中英双语 , 默认中文 , 语言选择存入 `localStorage` ( key : `chat_excel_lang` ) , 提供切换入口

### 0.8 全局设计体系 ( 三个页面统一 , 参考 uupm.cc/demo/ai-chatbot-platform )
- **主色** : 靛蓝 `#6366f1` ; **强调色** : 绿色 `#10b981` ( 用于成功状态 / 对勾 )
- **背景** : 分区背景在白色 `#ffffff` 与浅灰 `#f5f5f5` 之间交替 ; 控制台页面主体为浅灰底 + 白色卡片
- **文字** : 主文字 `#111827` , 次要文字 `#6b7280` , 边框 `#e5e7eb`
- **字体** : 标题使用展示型无衬线字体 ( Space Grotesk 或同类 , 中文回退系统黑体 ) , 正文使用 DM Sans 或同类 ; h1 约 48px/700 , h2 约 36px/700
- **渐变文字** : 标题中的强调词使用 `linear-gradient(135deg, #6366f1, #818cf8)` + `background-clip: text`
- **按钮** : 主按钮靛蓝实心 + 白字 + 圆角 12px ; 次按钮白底/透明 + 边框 `#e5e7eb` + 深色文字
- **卡片** : 白底 + 圆角 16px + 1px 边框 `#e5e7eb` + 无阴影或极轻阴影 , hover 时 0.2s 过渡 ( 轻微上浮或边框变色 )
- **图标** : 线性图标 , 置于主色 10% 透明度的圆角底色块中
- **深色元素** : 页脚与代码窗口使用深色底 ( 页脚 `#111827` , 代码窗口 `#1a1a2e` ) , 与浅色主体形成对比

---

## 1. 项目介绍 ( 用于撰写产品展示页文案 )

chat-excel 是一款基于 C++17 实现的微服务项目 , 将 LLM 与数据分析结合 , 让用户通过自然语言操作 Excel 文件与数据库 , 让没有技术背景的人也能高效处理复杂数据 . 采用微服务架构 , 共 7 个子服务 :

| 子服务 | 职责 |
|--------|------|
| 网关子服务 | 前端唯一入口 , 接收浏览器请求 , 鉴权 , 协议转换 , 负载均衡 , 分发给其他子服务 |
| 用户子服务 | 注册 , 登录 , 验证码 , 用户信息 |
| 文件子服务 | 文件上传 / 下载 / 删除 / 列表 / 预览 , 关联文件与会话 , 上传 SQLite |
| Excel 解析子服务 | 解析 Excel 的 worksheet 表结构与数据 ( 内部服务 , 不直接对外 ) |
| 存储子服务 | 数据库连接管理 , 表结构 / 表数据获取 , 执行 SQL |
| AI 子服务 | 与 LLM 交互 , 聊天会话 / 消息管理 , 构建提示词 , 生成并执行 SQL , 总结与可视化 |
| 通知子服务 | 发送邮箱验证码 , 发送 AI 分析结果到邮箱 |

### 1.1 智能 Excel 助手数据流
用户上传 Excel → 文件子服务调用 Excel 解析子服务解析出所有 worksheet → 存入数据库 → 文件存入 FastDFS → 用户选择模型创建会话并提问 → AI 子服务获取表结构与采样数据 → 构建提示词发给 LLM → LLM 生成 SQL → 存储子服务执行 SQL → LLM 基于结果生成总结 + 可视化 → 流式返回浏览器

### 1.2 数据库助手数据流
用户上传 SQLite 或填写 MySQL 连接信息 → 存储子服务建立连接并返回表列表 → 浏览器渲染表 → 用户提问 → AI 子服务获取表结构与采样数据 → 构建提示词发给 LLM → 生成 SQL → 执行 → 总结 + 可视化 → 流式返回浏览器

---

## 2. 页面一 : 产品展示页 ( index.html )

整体布局参考 uupm.cc/demo/ai-chatbot-platform : 固定导航栏 + 多个全宽分区 ( 白底与浅灰底交替 ) + 深色页脚 , 每个分区为"居中标题 + 副标题 + 内容网格"的结构 .

### 2.1 导航栏 ( fixed 顶部 )
- 半透明白底 + 背景模糊 , 左侧项目 Logo ( 靛蓝圆角图标 ) + 名称 "ChatExcel"
- 中部锚点链接 : 功能特性 / 效果演示 / 架构 / 常见问题 ( 点击平滑滚动到对应分区 )
- 右侧 : 语言切换 ( 中文 ⇄ English ) + "登录"文字链 + 靛蓝实心"免费使用"按钮 ( 均跳转 `login.html` )

### 2.2 Hero 区域 ( 浅灰底 #f5f5f5 )
1. **药丸徽章** : 主色 10% 透明底 + 主色文字 , 全圆角 , 内容如"LLM × 数据分析"
2. **超大标题** : 一句话核心价值主张 ( 如"用自然语言驾驭 Excel 与数据库" ) , 其中关键词使用靛蓝渐变文字
3. **副标题** : 1-2 句说明 ( 让没有技术背景的人也能高效处理复杂数据 )
4. **双按钮** : 主按钮"立即体验" ( 靛蓝实心 , 跳转登录页 ) + 次按钮"查看文档/架构" ( 白底描边 , 锚点滚动到架构分区 )
5. **产品演示卡** : 标题下方居中放置一个白色大圆角卡片 , 模拟控制台聊天界面 :
   - 卡片头部 : 助手头像 ( 靛蓝圆标 ) + 名称 + 绿色"在线"状态点
   - 卡片内容 : 用户气泡 ( 靛蓝底白字 , 如"统计各部门的平均薪资" ) + AI 气泡 ( 浅灰底 , 展示分析回复 )
   - 可加入打字机动画 / 流式文字效果增强真实感

### 2.3 功能特性分区 ( 白底 )
- 分区标题 + 副标题 ( 标题中的强调词用渐变文字 )
- **2 列或 3 列卡片网格 , 共 6 张卡片** , 每张 = 主色底色块线性图标 + 小标题 + 一句描述 :
  1. 自然语言分析 : 用一句话提问 , 自动完成 Excel 数据统计
  2. 数据库对话 : 连接 MySQL / SQLite , 像聊天一样查询数据库
  3. 自动生成 SQL : LLM 根据表结构生成并执行 SQL , 无需手写
  4. 可视化图表 : 分析结果自动生成柱状图 / 折线图 / 饼图等 9 种图表
  5. 邮件分享 : 一句话将分析结果发送到邮箱
  6. 多模型支持 : 支持多种大模型自由切换

### 2.4 效果演示分区 ( 浅灰底 )
- 分区标题"看看实际效果" + 副标题
- **Tab 切换** ( 选中态靛蓝实心白字 , 未选白底灰字 ) , 至少两个 Tab :
  1. **Excel 分析** : 白色大卡片展示一段模拟对话 ( 用户提问 → 分析标题/任务列表/总结 → 内嵌一个示意柱状图 )
  2. **数据库查询** : 展示数据库场景的模拟对话 ( 用户提问 → 生成查询 → 结果表格 )
- 对话内容可静态写死 , 用于展示产品能力 , 无需真实调用接口

### 2.5 架构说明分区 ( 白底 )
- 分区标题"微服务架构" + 副标题
- **子服务网格** : 7 张卡片 ( 3-4 列 ) , 每张 = 图标 + 子服务名称 + 一句职责说明 ( 内容见第 1 节的子服务表 )
- **数据流转示意图** : 依据第 1 节的两条数据流 , 使用**纯 HTML/CSS/SVG** 绘制 ( 不引入 Mermaid 运行时 ) , 节点标注服务名称 , 箭头标明数据流向 , 至少包含"智能 Excel 助手"完整链路 ; 示意图可放在深色底卡片中突出显示

### 2.6 使用流程分区 ( 浅灰底 , 可选 )
- 3-4 个步骤横向排列 : 上传文件/连接数据库 → 自然语言提问 → 自动分析生成图表 → 邮件分享结果
- 每个步骤 = 序号圆标 + 标题 + 简述 , 步骤间用箭头或连接线串联

### 2.7 CTA 分区 ( 白底中的渐变大卡 )
- 大圆角 ( 16px ) 渐变卡片 `linear-gradient(135deg, #6366f1, #818cf8)`
- 白色大标题 ( 如"准备好开始了吗?" ) + 白色说明文字
- 白底靛蓝字主按钮"免费注册" ( 跳转登录页 ) + 白描边次按钮

### 2.8 页脚 ( 深色底 #111827 )
- 左侧 : Logo + 项目一句话简介
- 右侧 : 2-4 列链接 ( 产品 / 资源 / 关于 ) , 链接为灰色文字 , hover 变白
- 底部 : 分隔线 + 版权信息 + 绿色"服务运行正常"状态点 ( 可调用 `GET /health` 检测 , 失败时显示灰色"服务离线" )

### 2.9 技术要求
- 纯静态页面 , 响应式 ( 移动端卡片网格降为单列 , 导航收起为汉堡菜单 )
- 语言切换记住选择 ; 锚点平滑滚动 ; 分区进入视口时可加轻量淡入动画

---

## 3. 页面二 : 登录注册页 ( login.html )

### 3.1 整体说明
- 登录 / 注册同页 , 通过 Tab 切换 , 默认展示登录
- 登录成功后统一跳转 `console.html`
- 页面风格与产品展示页保持一致 ( 同一设计体系 )
- 记住上次使用的登录方式 Tab ( localStorage )

### 3.2 登录模块

#### 3.2.1 账号密码登录 ( 默认 Tab )
- 接口 : `POST /api/user/passwd/login`
- 请求体 : `{ requestId, username /* 昵称或邮箱 */, password }`
- 响应 `result` : `{ sessionId }` → 存入 localStorage 后跳转

| 字段 | 校验规则 |
|------|---------|
| 账号 | 必填 , 支持昵称或邮箱 |
| 密码 | 必填 , 非空校验 |

#### 3.2.2 邮箱验证码登录 ( Tab 切换 )
- 第一步获取验证码 : `POST /api/user/code` , 请求体 `{ requestId, email }` , 响应 `result` : `{ codeId }`
  - **前端必须保存返回的 `codeId`** , 登录时与验证码配对提交
  - 点击"获取验证码"后按钮进入 60s 倒计时禁用状态
- 第二步登录 : `POST /api/user/vcode/login` , 请求体 `{ requestId, email, verifyCode, codeId }` , 响应 `result` : `{ sessionId }`

| 字段 | 校验规则 |
|------|---------|
| 邮箱 | 必填 , 前端正则校验格式 , 错误提示"请输入正确的邮箱格式" |
| 验证码 | 必填 , 6 位数字 , 错误提示"验证码为6位数字" |

### 3.3 注册模块
- 接口 : `POST /api/user/register`
- 请求体 : `{ requestId, nickname, password, email, verifyCode, codeId }`
- **`verifyCode` 与 `codeId` 必须先调用 `POST /api/user/code` 获取** , 验证码校验通过才能注册成功 ; 验证码有效期 1 分钟

| 字段 | 校验规则 | 交互细节 |
|------|---------|---------|
| 用户名 | 必填 , 非空 | 失焦时调用 `POST /api/user/valid/nickname` ( 请求体 `{ requestId, nickname }` ) 校验唯一性 , 已存在提示"用户名已存在" |
| 邮箱 | 必填 , 格式校验 | 失焦时调用 `POST /api/user/valid/email` ( 请求体 `{ requestId, email }` ) 校验唯一性 , 已存在提示"邮箱已存在" |
| 密码 | 至少 6 位 | 错误提示"密码至少6位以上" |
| 确认密码 | 与密码一致 | 不一致提示"两次输入的密码不一致" |
| 邮箱验证码 | 6 位数字 | 点击"发送验证码"调用 `/api/user/code` 获取 , 保存 `codeId` , 有效期 1 分钟 , 按钮 60s 倒计时 |

#### 3.3.1 注册交互流程
1. 填写邮箱 → 点击"发送验证码" → 后端发送邮件 → 提示"验证码已发送至邮箱 , 1分钟内有效" , 按钮倒计时 60s 禁用
2. 填写完整信息 → 点击"注册" → 前端校验通过后提交
3. 注册成功 → 自动切换到登录 Tab ( 默认账号密码 ) , 提示"注册成功 , 请登录"

### 3.4 表单交互细节
- 所有校验在输入框失焦 ( onBlur ) 时触发 , 错误信息显示在输入框下方
- 提交时全量校验 , 有错误则聚焦到第一个错误字段
- 输入框错误状态 : 红色边框 + 红色提示文字
- 登录 / 注册切换时清空所有表单数据和错误提示
- 密码输入框带"显示/隐藏"切换按钮
- 响应式 , 移动端表单宽度自适应

---

## 4. 页面三 : 控制台页面 ( console.html )

### 4.0 页面初始化流程
1. 读取 localStorage 中的 `sessionId` , 调用 `POST /api/user/session/login` ( 请求体 `{ requestId, sessionId }` ) 恢复登录态 ; 失败跳转登录页
2. 调用 `POST /api/user/info?requestId={requestId}&sessionId={sessionId}` 获取用户信息
   - 请求体为空 , 参数全部在 query 中 ; **不需要也不允许传 userId** ( 后端通过会话鉴权获取 )
   - 响应 `result` : `{ userInfo: { userId, nickname, email } }`
3. 调用 `POST /api/ai/models` ( 请求体 `{ requestId, sessionId }` ) 获取模型列表 , 响应 `result` : `{ modelList: [ { modelName, modelDesc } ] }` , 填充各模块的模型下拉框

### 4.1 整体布局
- 顶部栏 : 项目 Logo + 名称 , 右侧展示用户昵称
- 可折叠侧边栏 , 包含 6 个模块 :
  1. AI 聊天
  2. Excel 智能助手
  3. 数据库智能助手
  4. 我的文件
  5. 历史会话
  6. 个人中心

### 4.2 AI 聊天模块 ( 纯聊天 , 无文件/数据库预览区 )
- **右上角** : "新建会话"按钮 , 调用 `POST /api/ai/session/create` , 请求体 `{ requestId, sessionId, modelName, sessionType: "plain" }` , 响应 `result` : `{ chatSessionId, modelName }` ( 后端已支持 `plain` 类型 ) ; 成功后清空聊天区
- **输入框上方** : 可展开的模型列表选择器 ( 数据来源 `/api/ai/models` ) , 展开后选择本次对话使用的模型
- 发送消息 : `POST /api/ai/sendStreamMessage` , `chatType` 传 `"plain"` , 仅需 `message`
- 模型回复为纯文本流式输出 , 按流式文本渲染 ( 可做简单 Markdown 渲染 ) , 无标签结构

### 4.3 Excel 智能助手模块 ( 左右分栏 , 参考 chat2Data 的 chat-page 布局 )

**左半部分 : 预览与结果分析区**
- 顶部导航栏 Tab 切换 "文件预览" / "结果分析" ( 选中态靛蓝高亮 )
- **文件预览 Tab** :
  - 上传入口 , 上传分两步 :
    1. 登记元数据 : `POST /api/file/upload/info` , 请求体 `{ requestId, sessionId, fileInfo: { filename, fileSize, fileExt } }` ( 仅支持 `.xlsx` ) , 响应 `result` : `{ fileId }`
    2. 上传二进制 : `POST /api/file/upload?requestId={requestId}&sessionId={sessionId}&fileId={fileId}` , 请求头 `Content-Type: application/octet-stream` , 请求体为文件二进制
  - 上传成功后预览 : `POST /api/file/preview` , 请求体 `{ requestId, sessionId, fileId, pageNumber?, pageSize? }` , 按返回的 `excelData.sheets` 渲染 Excel 风格表格 ( sheet 标签页 , 列头 , 行号 , 分页 )
  - 从"我的文件"页点击 Excel 文件跳转进入时 , 直接加载该文件的预览
- **结果分析 Tab** : 展示模型返回的最终分析结果 ( 总结文本 + ECharts 图表 , 如饼状图 / 柱状图 / 折线图 , 按 `displayType` 渲染 , 解析规则见第 5 节 )

**右半部分 : 聊天区**
- **右上角** : "新建会话"按钮 , 调用 `POST /api/ai/session/create` , `sessionType` 传 `"excel"` ; 新建会话后调用 `POST /api/file/chat/map` ( 请求体 `{ requestId, sessionId, fileId, chatSessionId }` ) 关联文件与会话
- **聊天框顶部** : WorkSheet 选择器 ( 下拉框 ) , 列出当前 Excel 文件的所有 worksheet , 用户可切换查看目标 worksheet , 选中项与左侧预览区联动
- **输入框上方** : 可展开的模型列表选择器 ( 数据来源 `/api/ai/models` ) , 展开后选择本次对话使用的模型
- 发送消息 : `POST /api/ai/sendStreamMessage` , `chatType` 传 `"excel"` , 携带 `fileId` ; 模型响应按第 5 节流式渲染

### 4.4 数据库智能助手模块 ( 连接管理页 + 聊天界面两个状态 , 参考 chat2Data 的 database-page 与 db-chat-page )

#### 4.4.1 连接管理页 ( 进入模块的默认视图 )
顶部导航栏 Tab 切换 "MySQL" / "SQLite" ( 选中态靛蓝高亮 ) :

**MySQL Tab ( 上下两部分 )** :
- **上半部分"已连接的数据库"** : 已建立连接的 MySQL 数据库卡片列表
  - 后端**没有**"连接列表"接口 , 由前端自行维护 ( 连接成功后记录连接信息 + `connectionId` , 可存入 localStorage 持久化 )
  - 每张卡片展示连接摘要 ( 如 `主机:端口/数据库名` ) + 两个操作按钮 :
    - "进入" : 选用该连接作为当前连接 , 进入下方聊天界面
    - "断开连接" : 调用 `POST /api/db/disconnect` ( 请求体 `{ requestId, sessionId, connectionId }` ) , 成功后从列表移除
  - 列表为空时展示空状态提示
- **下半部分"建立新连接"** : MySQL 连接表单 ( 主机地址 , 端口默认 3306 , 数据库名 , 用户名 , 密码 , 字符集下拉 utf8mb4/utf8/latin1 ) , 提交调用 `POST /api/db/connect` , 请求体 :

      { "requestId": "", "sessionId": "", "database": { "type": "MySQL",
        "MySQL": { "host": "", "port": 3306, "name": "", "username": "", "password": "", "charset": "utf8mb4" } } }

  - 连接成功响应 `result` : `{ connectionId }` , 将其加入上半部分已连接列表并自动进入聊天界面

**SQLite Tab ( 两个功能 )** :
- **上传 SQLite 文件** : `POST /api/file/sqlite/upload?requestId={requestId}&sessionId={sessionId}&filename={filename}` ( 请求头 `Content-Type: application/octet-stream` , 请求体为文件二进制 , 仅接受 `.db` / `.sqlite` / `.sqlite3` ) , 响应 `result` : `{ fileId }`
- **建立连接** : 列出已上传的 SQLite 文件 ( 从 `/api/file/list` 按后缀筛选 ) , 选择某个文件后调用 `POST /api/db/connect` :

      { "requestId": "", "sessionId": "", "database": { "type": "SQLite",
        "SQLite": { "fileId": "", "readonly": false } } }

  - 连接成功同样获取 `connectionId` 并进入聊天界面

#### 4.4.2 聊天界面 ( 连接成功后左右分栏 , 参考 chat2Data 的 db-chat-page )

**左半部分 : 表预览与结果分析区**
- 顶部导航栏 Tab 切换 "表预览" / "结果分析"
- **表预览 Tab** :
  - 获取表列表 : `GET /api/db/tables?requestId={requestId}&sessionId={sessionId}&dbConnectId={connectionId}` , 响应 `result` : `{ tables: [str] }`
  - 选择表后获取表数据 : `POST /api/db/table/data` , 请求体 `{ requestId, sessionId, dbConnectId, tableName, forceOriginal? }` , 按返回的 `tableSchema` 渲染表结构与行数据
  - 表选择为**单选** , 连接成功后默认选中第一张表
- **结果分析 Tab** : 展示模型返回的最终分析结果 ( 总结文本 + ECharts 图表 , 如饼状图 / 柱状图 , 按 `displayType` 渲染 , 见第 5 节 )

**右半部分 : 聊天区**
- **右上角** : "新建会话"按钮 , `POST /api/ai/session/create` , `sessionType` 传 `"database"` , **必须携带 `dbConnectionInfo`** ( JSON 字符串 , 结构见 4.4.3 )
- **聊天框顶部** : 表选择器 ( 下拉框 ) , 列出当前数据库的所有表 , 用户可切换查看数据库中的哪一张表 , 选中项与左侧"表预览"区联动 ; 发送消息时 `tableName` 传当前选中的表名
- **输入框上方** : 可展开的模型列表选择器 ( 数据来源 `/api/ai/models` )
- 发送消息 : `POST /api/ai/sendStreamMessage` , `chatType` 传 `"database"` , 必须携带 `dbConnectId` , `tableName` ( 当前选中的表名 ) , `dbType` ( 大写 `"MYSQL"` 或 `"SQLITE"` , 区分大小写 )
- 连接状态 : `POST /api/db/connection/status` , 请求体 `{ requestId, sessionId, dbConnectId }` , 响应 `result` : `{ tempTables: [str], hasModifications: bool }`
  - 说明 : 修改类 SQL 不直接操作原表 , 而是复制新表 ( 如 `users` → `users_temp` ) 在新表上修改 ; `tempTables` 即新表名列表 , 有修改时前端应给出提示
- 提供"返回连接管理页"入口 ; 连接在模块切换时保持 , 仅当用户在连接管理页点击"断开连接"时才调用 `/api/db/disconnect`

#### 4.4.3 `dbConnectionInfo` 结构 ( 新建 database 会话时 `JSON.stringify` 后传入 )
- **后端不解析该字段内容** , 仅作为字符串原样存储到会话元数据 , 并在会话列表 / 历史消息接口中原样返回 ; 结构完全由前端定义 , 用途是历史会话恢复时重新建立数据库连接
- 约定结构如下 ( 与参考前端保持一致 ) :

      MySQL : { "type": "MySQL", "host": "", "port": 3306, "name": "", "username": "", "password": "", "charset": "utf8mb4" }
      SQLite : { "type": "SQLite", "fileId": "", "fileName": "" }

- **恢复历史会话时** : `JSON.parse` 该字段 → 按 `type` 字段 ( 或字段特征 ) 识别数据库类型 → 重新组装 `/api/db/connect` 请求体 ( MySQL 填 `database.MySQL` , SQLite 填 `database.SQLite.fileId` ) → 调用连接接口获取**新的 `connectionId`** ( 连接是临时的 , 不要存储旧的 connectionId ) → 再获取表列表并默认选中第一张表

### 4.5 我的文件模块 ( 文件管理页 )
- 获取文件列表 : `POST /api/file/list` , 请求体 `{ requestId, sessionId }` , 响应 `result` : `{ fileList: [ { fileId, fileName, fileSize, uploadTime } ] }`
- 页面完整展示用户上传的**所有**文件 , 按类型分组或加类型标识 :
  - Excel 文件 ( `.xlsx` 后缀 )
  - SQLite 数据库文件 ( `.db` / `.sqlite` / `.sqlite3` 后缀 )
  - 注意 : 列表接口**不返回 `fileExt` 字段** , 前端通过 `fileName` 后缀区分文件类型 , 并为不同类型展示不同图标
- 每个文件项提供三个操作 :
  1. **下载** : `GET /api/file/download?requestId={requestId}&sessionId={sessionId}&fileId={fileId}` ( 二进制流 , 前端以 Blob 触发下载 )
  2. **预览** ( 跳转 ) :
     - Excel 文件 → 跳转 **Excel 智能助手**模块 , 用 `fileId` 调用 `/api/file/preview` 加载预览并进入对话界面
     - SQLite 文件 → 跳转 **数据库智能助手**模块 , 用该 `fileId` 调用 `/api/db/connect` 建立连接后进入对话界面
  3. **删除** : `DELETE /api/file/{fileId}?requestId={requestId}&sessionId={sessionId}` ( fileId 为路径参数 ) , 删除前弹出二次确认 , 成功后刷新列表
- 文件列表为空时展示空状态引导 ( 提示前往 Excel 智能助手上传 )

### 4.6 历史会话模块
- 获取会话列表 : `POST /api/ai/chatSessionLists` , 请求体 `{ requestId, sessionId }` , 响应 `result` : `{ chatSessionLists: [ { chatSessionId, modelName, title, createdAt, updatedAt, messageCount, firstUserMessageContent, sessionType, dbConnectionInfo } ] }`
- 列表展示所有历史会话 , 每项显示标题 ( 或首条用户消息摘要 ) , 会话类型标识 ( plain / excel / database ) , 模型名 , 消息数 , 更新时间
- **点击某个会话** → 查看完整历史场景 : 调用 `POST /api/ai/history` , 请求体 `{ requestId, sessionId, chatSessionId }` , 响应 `result` : `{ messageList: [ { id, role, content, timestamp } ], fileId, sessionType, dbConnectionInfo }`
  - 根据返回的 `sessionType` 跳转到对应模块并**完整恢复当时的聊天场景** :
    - `excel` : 跳转 Excel 智能助手 , 用 `fileId` 调用 `/api/file/preview` 恢复文件预览 ; 左侧"结果分析"恢复之前的分析结果 ; 右侧聊天区按 `messageList` 渲染之前的聊天内容
    - `database` : 跳转数据库智能助手 , 解析 `dbConnectionInfo` 重新调用 `/api/db/connect` 恢复连接并加载表预览 ; 同样恢复结果分析与聊天内容
    - `plain` : 跳转 AI 聊天模块 , 仅恢复聊天内容
  - 历史消息中的最终结果帧 ( 含 `summary` / `displayType` 的 JSON ) 也需按第 5 节规则解析渲染为总结 + 图表
- **删除会话** : 每个会话项提供删除入口 , 调用 `POST /api/ai/delete` ( 请求体 `{ requestId, sessionId, chatSessionId }` ) , 删除前二次确认 , 成功后从列表移除

### 4.7 个人中心模块
- 展示用户基本信息 ( 昵称 , 邮箱 , 来自 `/api/user/info` )
- 退出登录 : `POST /api/user/logout` , 请求体 `{ requestId, sessionId }` ; 成功后清除 localStorage 并跳转登录页

---

## 5. SSE 流式消息协议与解析 ( 核心 , 必须正确实现 )

### 5.1 请求
`POST /api/ai/sendStreamMessage` , 请求体 :

    { "requestId": "", "sessionId": "", "chatSessionId": "", "chatType": "plain|excel|database",
      "message": "", "fileId": "", "dbConnectId": "", "dbType": "", "tableName": "" }

- `chatType=excel` : 携带 `fileId`
- `chatType=database` : 必须携带 `dbConnectId` , `tableName` , `dbType`
- 使用 `fetch` + `ReadableStream` 读取 , 请求头 `Accept: text/event-stream`

### 5.2 响应帧格式
响应为 SSE 流 ( `Content-Type: text/event-stream` ) , 每帧格式 :

    data: {"content": "消息片段", "done": false, "errorCode": 0, "errorMsg": ""}

- **每帧 `data:` 后是一个 JSON 对象 , 前端必须 `JSON.parse` 后取 `content` 字段** , 不能把整行 JSON 当作消息内容
- 流结束前最后一帧 `done` 为 `true` , 随后是结束标记行 `data: [DONE]`
- 若某帧 `errorCode != 0` , 取 `errorMsg` 作为错误提示

### 5.3 消息内容结构 ( excel / database 场景 )
后端对模型回复做了标签过滤 : **`<SQL_START>...<SQL_END>` 与 `<EMAIL_START>...<EMAIL_END>` 区间内容已被后端过滤 , 不会下发** ; 但以下三个标签会原样透传给前端 , 需要前端解析并分阶段渲染 :

    <TITLE_START> 分析标题 <TITLE_END>
    <TASKS_START> 1. 任务1  2. 任务2 ... <TASKS_END>
    <ANALYSIS_START> 分析思路 <ANALYSIS_END>

- 前端应支持**流式增量解析** ( 标签可能被分块切断 ) , 边接收边渲染 : 标题 → 任务列表 ( 逐条动画 ) → 分析过程
- 流式文本之后 , 后端会**单独下发一帧最终结果 JSON** , 结构为 :

      { "summary": "总结文本", "displayType": "BarChart",
        "data": { "columns": ["列名"], "rows": [["值"]], "tables": [ { "columns": [], "rows": [] } ] } }

- **图表类型字段名是 `displayType` ( 不是 `chartType` )** , 可选值 : `Table` , `BarChart` , `ColumnChart` , `LineChart` , `AreaChart` , `PieChart` , `DonutChart` , `ScatterChart` , `NumberDisplay`
- 前端区分最终结果帧的方法 : 对每帧 `content` 尝试 `JSON.parse` , 成功且包含 `summary` 与 `displayType` 字段即判定为最终结果帧 , 交由"结果分析"区渲染总结 + ECharts 图表 , 不作为普通流式文本显示

### 5.4 邮件分享功能
- 用户在对话中表达"将结果发送到我邮箱 / 发邮件给我"等意图时 , 后端会自动发送邮件 , 并下发一条文本消息"邮箱发送成功 , 请注意查收"
- 前端正常按流式文本展示该消息即可 , 可在输入区提供"发送结果到邮箱"的引导提示

### 5.5 plain 场景
- `chatType=plain` 时模型回复为纯文本流 , 无标签结构 , 直接流式渲染

---

## 6. 设计参考 ( 取其精华 , 去其糟粕 )

### 6.1 产品展示页布局参考 : uupm.cc/demo/ai-chatbot-platform
1. 整体结构 : 固定半透明导航栏 + 白/浅灰交替的全宽分区 + 深色页脚 , 每分区"居中标题 + 副标题 + 内容网格"
2. Hero : 药丸徽章 + 渐变强调词大标题 + 双按钮 + 居中的聊天演示卡 ( 含打字机/流式动效 )
3. 功能特性 : 6 卡片网格 , 图标置于主色 10% 透明底色块中
4. 效果演示 : Tab 切换展示不同场景的模拟对话
5. 集成/架构 : 网格卡片 + 深色代码窗口风格的对比元素
6. CTA : 渐变大卡 + 反色按钮
7. 配色 : 靛蓝 `#6366f1` 主色 , 绿色 `#10b981` 强调 , 白 `#ffffff` / 浅灰 `#f5f5f5` 交替背景 , 深色 `#111827` 页脚
8. 注意 : 该参考站含"定价"板块 , 本项目为开源/内部项目 , **不需要定价板块** , 用"使用流程"或"常见问题"替代

### 6.2 控制台交互参考 
1. 控制台左右分栏布局 : 左侧"文件预览 / 结果分析" Tab 区 + 右侧聊天区
2. Excel 风格表格预览 : sheet 标签页 , 列头 , 行号 , 分页
3. 流式消息分阶段渲染 : 标题 → 任务列表逐条动画 → 分析过程
4. ECharts 可视化 , 支持多种图表类型 , 本地 + CDN 双加载策略
5. 数据库连接 MySQL / SQLite 双 Tab 表单
6. 加载遮罩 ( loading overlay ) 与 toast 消息提示
7. 密码显示/隐藏切换 , 验证码 60s 倒计时

## 7. 接口速查表

| 编号 | 方法 | 路径 | 用途 |
|------|------|------|------|
| H01 | GET | /health | 健康检测 |
| U01 | POST | /api/user/valid/nickname | 昵称唯一性校验 |
| U02 | POST | /api/user/valid/email | 邮箱唯一性校验 |
| U03 | POST | /api/user/register | 用户注册 ( 需 codeId + verifyCode ) |
| U04 | POST | /api/user/passwd/login | 密码登录 |
| U05 | POST | /api/user/code | 获取邮箱验证码 ( 返回 codeId ) |
| U06 | POST | /api/user/vcode/login | 验证码登录 ( 需 codeId ) |
| U07 | POST | /api/user/session/login | 会话登录 ( 恢复登录态 ) |
| U08 | POST | /api/user/logout | 退出登录 |
| U09 | POST | /api/user/info | 获取用户信息 ( query 参数 , 不传 userId ) |
| F01 | POST | /api/file/upload/info | 登记文件元数据 ( 返回 fileId ) |
| F02 | GET | /api/file/info | 获取文件信息 |
| F03 | POST | /api/file/upload | 上传文件二进制 |
| F04 | GET | /api/file/download | 下载文件 |
| F05 | DELETE | /api/file/{fileId} | 删除文件 |
| F06 | POST | /api/file/preview | 分页预览 Excel |
| F07 | POST | /api/file/list | 用户文件列表 |
| F08 | POST | /api/file/chat/map | 文件↔聊天会话映射 |
| F09 | POST | /api/file/sqlite/upload | 上传 SQLite 文件 |
| D01 | POST | /api/db/connect | 新建数据库连接 ( 返回 connectionId ) |
| D02 | POST | /api/db/disconnect | 断开数据库连接 |
| D03 | GET | /api/db/tables | 数据库表列表 |
| D04 | POST | /api/db/table/data | 获取表数据 |
| D05 | POST | /api/db/connection/status | 获取连接状态 |
| A01 | POST | /api/ai/models | 模型列表 |
| A02 | POST | /api/ai/session/create | 新建聊天会话 |
| A03 | POST | /api/ai/chatSessionLists | 聊天会话列表 |
| A04 | POST | /api/ai/history | 历史消息 |
| A05 | POST | /api/ai/delete | 删除聊天会话 |
| A06 | POST | /api/ai/sendStreamMessage | 流式发送消息 ( SSE ) |
