
**系统身份** : 你是一位资深的全栈开发工程师 , 熟悉前端项目的开发流程 , 能够独立完成前端项目的开发 , 测试 , 优化 , 擅长编写 HTTP 网页. 熟悉C++ 后端开发 , 能够结合前后端代码进行修改.

**任务** : 按照一下要求对前后端代码进行修改.

## 1. 在智能 Excel 和智能 DB 页面添加一个按钮，可以查看原数据和预览修改
1. 添加一个按钮，可以查看原数据和预览修改

## 2. 前端根据数据类型进行展示

1. 如果列是 NULL , 不进行展示
2. 如果类是 bool 类型 , 展示为 true 或 false

## 3. 在用户控制台界面允许将侧边栏收起或展开
1. 前端侧边栏可以收起

## 4. 在用户控制台界面添加github图标
1. 用户控制台上方添加github图标 ,跳转到github仓库 , 参考产品介绍页中的设计

## 5. 新建会话逻辑修改

1. 前端点击新建会话后 , 要清空所有信息包含文件预览中的数据，结果分析和聊天框中的内容

## 6. 自动新建会话
1. 如果用户没有新建会话前 , 就发送了消息 , 则要自动新建会话

## 7. 我的文件中预览功能
1. 点击我的文件中的某个文件 , 要加载文件预览内容 , 不需要加载历史会话

## 8. 补充前端对标签进行解析

我对后端给前端反馈的数据进行了修改(你需要重新编译后端代码 , 重新启动 AI 子服务) , 后端在进行反馈的时候会携带标签内容 , 比如:简化 Feed 状态机： <TITLE_START>/<TITLE_END>、<TASKS_START>/<TASKS_END>、<ANALYSIS_START>/<ANALYSIS_END>这些标签也会进行返回 , 现在对前端代码进行修改 ,去掉这些标签 ,将这些内容都展示在 AI 回复中, 每个标签的展示格式可以参考 : C:\Users\LENOVO\Desktop\chat2data-tech\chat2Data\bin\www 中的展示方式.

明白我的要求后 , 向我展示你设计的 AI 回复布局 , 当我确定之后 , 你再对前端代码进行修改

## 8. 参考文件

1. chat-excel/prompt/gateway_service/api.md 中介绍了所有 API 接口文档
2. chat-excel/prompt/web/web.md 中包含了前端设计的提示词
3. chat-excel/proto 是对所有 RPC 接口的定义
4. chat-excel/svc_ai_service 是后端 AI 服务 , 包含了所有 AI 相关的逻辑
5. chat-excel/svc_file_service 是后端文件服务 , 包含了所有文件相关的逻辑
6. chat-excel/svc_gateway_service 是后端网关服务 , 包含了所有网关相关的逻辑
7. chat-excel/svc_user_service 是后端用户服务 , 包含了所有用户相关的逻辑
8. chat-excel/svc_email_service 是后端邮箱服务 , 包含了所有邮件相关的逻辑
9. chat-excel/svc_excel_parse_service 是后端 Excel 解析服务 , 包含了所有 Excel 解析相关的逻辑
10. chat-excel/www 是前端所有代码的目录 , 包含了所有前端的 HTML , CSS , JS 等文件

按照以上新增或修改的部分对前端代码进行修改 , 修改过程中如果发现后端返回值或后端逻辑存在问题, 要及时修改后端代码. 先列出你的执行计划  , 如果对修改纯在疑问先与我沟通 , 让我进行确认.