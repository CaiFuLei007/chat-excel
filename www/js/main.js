/**
 * main.js — 展示页（index.html）与控制台（console.html）页面逻辑
 * 依赖：config.js / utils.js / api.js / chart.js / chat.js / file.js（控制台）
 */

/* ============================================================
   文案注册（展示页 + 控制台）
   ============================================================ */
I18N.register(
  {
    /* ---- 展示页 ---- */
    'nav.features': '功能特性', 'nav.demo': '效果演示', 'nav.arch': '架构', 'nav.faq': '常见问题',
    'nav.login': '登录', 'nav.free': '免费使用',
    'hero.pill': 'LLM × 数据分析',
    'hero.title': '用自然语言驾驭 <span class="grad-text">Excel 与数据库</span>',
    'hero.sub': 'ChatExcel 将大语言模型与数据分析结合，让没有技术背景的人也能高效处理复杂数据。',
    'hero.cta1': '立即体验', 'hero.cta2': '查看架构',
    'hero.chatName': 'ChatExcel 助手', 'hero.online': '在线',
    'hero.excelSummary': '已分析「员工表」共 286 条记录：技术部平均薪资最高（¥21,500），市场部次之（¥16,800）。已生成柱状图。',
    'hero.dbSummary': '查询完成：上季度销售额最高的三款产品为 iPhone 15、MateBook X 与 AirPods Pro，明细如下。',

    'feat.title': '让数据分析像 <span class="grad-text">聊天一样简单</span>',
    'feat.sub': '六大核心能力，覆盖从提问到分享的完整链路',
    'demo.title': '看看实际效果', 'demo.sub': '真实场景下的对话与分析结果',
    'demo.tabExcel': 'Excel 分析', 'demo.tabDb': '数据库查询',
    'demo.excelQ': '统计各部门的平均薪资，并生成图表',
    'demo.excelTitle': '各部门平均薪资分析',
    'demo.excelTask1': '读取「员工表」的部门与薪资列',
    'demo.excelTask2': '按部门分组计算平均薪资',
    'demo.excelTask3': '生成柱状图可视化',
    'demo.dbQ': '查询上个季度销售额最高的三款产品',
    'demo.dbTitle': '已生成并执行查询',
    'demo.dbDesc': '自动分析 orders 与 products 表结构，生成 SQL 并执行：',
    'demo.dbCol1': '产品名称', 'demo.dbCol2': '销量', 'demo.dbCol3': '销售额',
    'arch.title': '微服务架构', 'arch.sub': 'C++17 实现的 7 个子服务，职责清晰、独立部署',
    'arch.flowTitle': '智能 Excel 助手 · 数据流转',
    'steps.title': '四步开始分析', 'steps.sub': '从上传到分享，全程自然语言驱动',
    'faq.title': '常见问题', 'faq.sub': '关于 ChatExcel 你可能想知道的',
    'cta.title': '准备好开始了吗？', 'cta.sub': '免费注册，上传你的第一份 Excel，体验自然语言数据分析。',
    'cta.btn1': '免费注册', 'cta.btn2': '了解架构',
    'footer.desc': '基于 C++17 微服务架构的智能数据对话平台，支持自然语言操作 Excel / MySQL / SQLite。',
    'footer.product': '产品', 'footer.resources': '资源', 'footer.about': '关于', 'footer.guide': '使用指南',
    'footer.top': '返回顶部', 'footer.copyright': '© 2026 ChatExcel · Powered by C++ Microservices',
    'footer.checking': '检测服务状态…', 'footer.online': '服务运行正常', 'footer.offline': '服务离线',
    'flow.n1': '用户上传', 'flow.n1s': 'Excel 文件', 'flow.n2': '文件子服务', 'flow.n2s': '接收与分发',
    'flow.n3': 'Excel 解析', 'flow.n3s': '解析 worksheet', 'flow.n4': '存储入库', 'flow.n4s': '数据库 + FastDFS',
    'flow.ask': '用户选择模型创建会话并提问',
    'flow.n5': 'AI 子服务', 'flow.n5s': '构建提示词', 'flow.n6': 'LLM', 'flow.n6s': '生成 SQL',
    'flow.n7': '存储子服务', 'flow.n7s': '执行 SQL', 'flow.n8': 'AI 子服务', 'flow.n8s': '总结 + 可视化',
    'flow.n9': '流式返回', 'flow.n9s': '浏览器渲染',
    'flow.note1': '数据库助手链路类似：', 'flow.note2': '上传 SQLite / 连接 MySQL → 提问 → 生成执行 SQL → 流式返回',

    /* ---- 控制台 ---- */
    'console.modChat': 'AI 聊天', 'console.modExcel': 'Excel 智能助手', 'console.modDb': '数据库智能助手',
    'console.modFiles': '我的文件', 'console.modHistory': '历史会话', 'console.modProfile': '个人中心',
    'console.newSession': '新建会话', 'console.refresh': '刷新', 'console.inputPh': '输入你的问题，Enter 发送，Shift+Enter 换行',
    'console.filePreview': '文件预览', 'console.resultAnalysis': '结果分析', 'console.tablePreview': '表预览',
    'console.uploadTitle': '点击或拖拽上传 Excel 文件', 'console.uploadDesc': '仅支持 .xlsx 格式',
    'console.worksheet': 'WorkSheet', 'console.table': '数据表',
    'console.model': '模型', 'console.selectModel': '选择模型',
    'console.noSession': '点击右上角「新建会话」开始对话',
    'console.thinking': '正在分析…', 'console.stop': '停止生成',
    'console.needFile': '请先上传 Excel 文件', 'console.needSession': '请先新建会话',
    'console.needTable': '请选择数据表',
    'console.resultEmpty': '分析完成后，结果将展示在这里',
    'console.tableEmpty': '选择左侧数据表查看结构与数据',
    'console.emailHint': '可以说「把结果发送到我邮箱」来邮件分享分析结果',
    'console.uploadOk': '文件上传成功',
    'console.sessionCreated': '会话已创建',
    'console.fileDeleted': '文件已删除', 'console.sessionDeleted': '会话已删除',
    'console.confirmDeleteFile': '确定删除该文件？删除后不可恢复。',
    'console.confirmDeleteSession': '确定删除该会话？删除后不可恢复。',
    'console.logoutOk': '已退出登录',
    'console.tempTableTip': '检测到修改操作：已在新表（{tables}）上执行，原表未受影响',
    'console.restoreConn': '正在恢复数据库连接…',
    'console.connOk': '连接成功',
    'console.groupExcel': 'Excel 文件', 'console.groupSqlite': 'SQLite 数据库',
    'console.filesEmptyTitle': '还没有文件', 'console.filesEmptyDesc': '前往 Excel 智能助手上传你的第一份文件',
    'console.goUpload': '去上传',
    'console.historyEmptyTitle': '暂无历史会话', 'console.historyEmptyDesc': '开始一次对话后，会话将出现在这里',
    'console.msgCount': '{n} 条消息',
    'console.typePlain': '聊天', 'console.typeExcel': 'Excel', 'console.typeDb': '数据库',
    'console.logout': '退出登录',
    'console.confirmLogout': '确定退出登录吗？',

    /* ---- 数据库 ---- */
    'db.connList': '已连接的数据库', 'db.newConn': '建立新连接', 'db.connEmpty': '暂无已建立的连接',
    'db.host': '主机地址', 'db.hostPh': '如 127.0.0.1', 'db.port': '端口', 'db.dbName': '数据库名',
    'db.dbNamePh': '要连接的数据库', 'db.username': '用户名', 'db.usernamePh': '数据库用户名',
    'db.password': '密码', 'db.passwordPh': '数据库密码', 'db.charset': '字符集', 'db.connect': '建立连接',
    'db.errHost': '请输入主机地址', 'db.errPort': '端口范围 1-65535', 'db.errName': '请输入数据库名',
    'db.errUser': '请输入用户名',
    'db.sqliteUploadTitle': '上传 SQLite 文件', 'db.sqliteUploadDesc': '支持 .db / .sqlite / .sqlite3',
    'db.sqliteConnectTitle': '选择已上传的 SQLite 文件建立连接',
    'db.sqliteEmpty': '暂无已上传的 SQLite 文件，请先上传',
    'db.backManage': '返回连接管理',
    'db.disconnectOk': '已断开连接',
    'db.confirmDisconnect': '确定断开该连接？',
    'db.connected': '已连接',

    /* ---- 个人中心 ---- */
    'profile.nickname': '昵称', 'profile.email': '邮箱'
  },
  {
    /* ---- Landing ---- */
    'nav.features': 'Features', 'nav.demo': 'Demo', 'nav.arch': 'Architecture', 'nav.faq': 'FAQ',
    'nav.login': 'Sign In', 'nav.free': 'Get Started',
    'hero.pill': 'LLM × Data Analytics',
    'hero.title': 'Master <span class="grad-text">Excel & Databases</span> with Natural Language',
    'hero.sub': 'ChatExcel combines LLMs with data analytics, empowering non-technical users to process complex data effortlessly.',
    'hero.cta1': 'Try It Now', 'hero.cta2': 'View Architecture',
    'hero.chatName': 'ChatExcel Assistant', 'hero.online': 'Online',
    'hero.excelSummary': 'Analyzed 286 records in "Employees": Engineering has the highest average salary (¥21,500), followed by Marketing (¥16,800). A bar chart has been generated.',
    'hero.dbSummary': 'Query complete: the top 3 products by revenue last quarter are iPhone 15, MateBook X and AirPods Pro. Details below.',

    'feat.title': 'Data analysis as easy as <span class="grad-text">chatting</span>',
    'feat.sub': 'Six core capabilities covering the full journey from question to sharing',
    'demo.title': 'See it in action', 'demo.sub': 'Real conversations and analysis results',
    'demo.tabExcel': 'Excel Analysis', 'demo.tabDb': 'Database Query',
    'demo.excelQ': 'Calculate the average salary by department and generate a chart',
    'demo.excelTitle': 'Average Salary by Department',
    'demo.excelTask1': 'Read department and salary columns from "Employees"',
    'demo.excelTask2': 'Group by department and compute averages',
    'demo.excelTask3': 'Generate bar chart visualization',
    'demo.dbQ': 'Find the top 3 products by sales last quarter',
    'demo.dbTitle': 'Query generated and executed',
    'demo.dbDesc': 'Automatically analyzed orders & products schemas, generated and executed SQL:',
    'demo.dbCol1': 'Product', 'demo.dbCol2': 'Units', 'demo.dbCol3': 'Revenue',
    'arch.title': 'Microservice Architecture', 'arch.sub': 'Seven C++17 services with clear responsibilities, independently deployable',
    'arch.flowTitle': 'Smart Excel Assistant · Data Flow',
    'steps.title': 'Start analyzing in 4 steps', 'steps.sub': 'From upload to sharing, fully driven by natural language',
    'faq.title': 'FAQ', 'faq.sub': 'What you may want to know about ChatExcel',
    'cta.title': 'Ready to get started?', 'cta.sub': 'Sign up free, upload your first Excel, and experience natural-language data analysis.',
    'cta.btn1': 'Sign Up Free', 'cta.btn2': 'Learn Architecture',
    'footer.desc': 'An intelligent data conversation platform built on C++17 microservices — operate Excel / MySQL / SQLite with natural language.',
    'footer.product': 'Product', 'footer.resources': 'Resources', 'footer.about': 'About', 'footer.guide': 'User Guide',
    'footer.top': 'Back to top', 'footer.copyright': '© 2026 ChatExcel · Powered by C++ Microservices',
    'footer.checking': 'Checking service…', 'footer.online': 'Service is healthy', 'footer.offline': 'Service offline',
    'flow.n1': 'Upload', 'flow.n1s': 'Excel file', 'flow.n2': 'File Service', 'flow.n2s': 'Receive & route',
    'flow.n3': 'Excel Parser', 'flow.n3s': 'Parse worksheets', 'flow.n4': 'Storage', 'flow.n4s': 'DB + FastDFS',
    'flow.ask': 'User picks a model, creates a session and asks',
    'flow.n5': 'AI Service', 'flow.n5s': 'Build prompt', 'flow.n6': 'LLM', 'flow.n6s': 'Generate SQL',
    'flow.n7': 'Storage Service', 'flow.n7s': 'Execute SQL', 'flow.n8': 'AI Service', 'flow.n8s': 'Summary + charts',
    'flow.n9': 'Stream back', 'flow.n9s': 'Browser rendering',
    'flow.note1': 'Database assistant is similar:', 'flow.note2': 'Upload SQLite / connect MySQL → ask → generate & run SQL → stream back',

    /* ---- Console ---- */
    'console.modChat': 'AI Chat', 'console.modExcel': 'Excel Assistant', 'console.modDb': 'Database Assistant',
    'console.modFiles': 'My Files', 'console.modHistory': 'History', 'console.modProfile': 'Profile',
    'console.newSession': 'New Session', 'console.refresh': 'Refresh', 'console.inputPh': 'Type your question. Enter to send, Shift+Enter for new line',
    'console.filePreview': 'File Preview', 'console.resultAnalysis': 'Result Analysis', 'console.tablePreview': 'Table Preview',
    'console.uploadTitle': 'Click or drag to upload an Excel file', 'console.uploadDesc': '.xlsx only',
    'console.worksheet': 'WorkSheet', 'console.table': 'Table',
    'console.model': 'Model', 'console.selectModel': 'Select model',
    'console.noSession': 'Click "New Session" in the top right to start',
    'console.thinking': 'Analyzing…', 'console.stop': 'Stop',
    'console.needFile': 'Please upload an Excel file first', 'console.needSession': 'Please create a session first',
    'console.needTable': 'Please select a table',
    'console.resultEmpty': 'Results will appear here after analysis',
    'console.tableEmpty': 'Select a table on the left to view schema and data',
    'console.emailHint': 'Say "email me the results" to share analysis results by email',
    'console.uploadOk': 'File uploaded',
    'console.sessionCreated': 'Session created',
    'console.fileDeleted': 'File deleted', 'console.sessionDeleted': 'Session deleted',
    'console.confirmDeleteFile': 'Delete this file? This cannot be undone.',
    'console.confirmDeleteSession': 'Delete this session? This cannot be undone.',
    'console.logoutOk': 'Signed out',
    'console.tempTableTip': 'Modifications detected: executed on new tables ({tables}); original tables untouched',
    'console.restoreConn': 'Restoring database connection…',
    'console.connOk': 'Connected',
    'console.groupExcel': 'Excel Files', 'console.groupSqlite': 'SQLite Databases',
    'console.filesEmptyTitle': 'No files yet', 'console.filesEmptyDesc': 'Go to Excel Assistant to upload your first file',
    'console.goUpload': 'Upload',
    'console.historyEmptyTitle': 'No history yet', 'console.historyEmptyDesc': 'Sessions will appear here after your first chat',
    'console.msgCount': '{n} messages',
    'console.typePlain': 'Chat', 'console.typeExcel': 'Excel', 'console.typeDb': 'Database',
    'console.logout': 'Sign Out',
    'console.confirmLogout': 'Sign out?',

    /* ---- DB ---- */
    'db.connList': 'Connected Databases', 'db.newConn': 'New Connection', 'db.connEmpty': 'No connections yet',
    'db.host': 'Host', 'db.hostPh': 'e.g. 127.0.0.1', 'db.port': 'Port', 'db.dbName': 'Database',
    'db.dbNamePh': 'Database name', 'db.username': 'Username', 'db.usernamePh': 'DB username',
    'db.password': 'Password', 'db.passwordPh': 'DB password', 'db.charset': 'Charset', 'db.connect': 'Connect',
    'db.errHost': 'Host is required', 'db.errPort': 'Port must be 1-65535', 'db.errName': 'Database name is required',
    'db.errUser': 'Username is required',
    'db.sqliteUploadTitle': 'Upload SQLite file', 'db.sqliteUploadDesc': '.db / .sqlite / .sqlite3 supported',
    'db.sqliteConnectTitle': 'Select an uploaded SQLite file to connect',
    'db.sqliteEmpty': 'No uploaded SQLite files yet — upload one first',
    'db.backManage': 'Back to Connections',
    'db.disconnectOk': 'Disconnected',
    'db.confirmDisconnect': 'Disconnect this connection?',
    'db.connected': 'Connected',

    /* ---- Profile ---- */
    'profile.nickname': 'Nickname', 'profile.email': 'Email'
  }
);

/* ============================================================
   页面判定
   ============================================================ */
const PAGE = document.getElementById('site-nav') ? 'home' : document.body.classList.contains('auth-page') ? 'auth' : 'console';

document.addEventListener('DOMContentLoaded', () => {
  if (PAGE === 'home') initHomePage();
  else if (PAGE === 'console') initConsolePage();
});

/* ============================================================
   展示页逻辑（index.html）
   ============================================================ */
function initHomePage() {
  // Logo
  document.getElementById('nav-logo').innerHTML = icon('logo');
  document.getElementById('footer-logo').innerHTML = icon('logo');
  document.getElementById('demo-avatar').innerHTML = icon('sparkle');
  document.getElementById('nav-burger').innerHTML = icon('menu');
  document.querySelectorAll('[data-icon]').forEach((el) => {
    el.innerHTML = icon(el.dataset.icon);
  });

  // Hero 药丸
  document.getElementById('hero-pill').textContent = I18N.t('hero.pill');

  I18N.apply();
  I18N.bindSwitch(() => {
    document.getElementById('hero-pill').textContent = I18N.t('hero.pill');
    renderFeatures();
    renderArch();
    renderSteps();
    renderFaq();
    runDemoScene(document.querySelector('.demo-tab.active').dataset.demo);
    checkHealth();
  });

  // 导航滚动状态
  const nav = document.getElementById('site-nav');
  window.addEventListener('scroll', () => {
    nav.classList.toggle('scrolled', window.scrollY > 8);
  }, { passive: true });

  // 移动端菜单
  document.getElementById('nav-burger').addEventListener('click', () => {
    document.getElementById('nav-links').classList.toggle('open');
  });
  document.querySelectorAll('#nav-links a').forEach((a) => {
    a.addEventListener('click', () => document.getElementById('nav-links').classList.remove('open'));
  });

  // 淡入动画
  const io = new IntersectionObserver(
    (entries) => entries.forEach((e) => e.isIntersecting && e.target.classList.add('visible')),
    { threshold: 0.12 }
  );
  document.querySelectorAll('.fade-in').forEach((el) => io.observe(el));

  // 效果演示 Tab（切换即重播流式对话）
  document.querySelectorAll('.demo-tab').forEach((btn) => {
    btn.addEventListener('click', () => {
      if (btn.classList.contains('active')) return;
      document.querySelectorAll('.demo-tab').forEach((b) => b.classList.remove('active'));
      btn.classList.add('active');
      runDemoScene(btn.dataset.demo);
    });
  });

  renderFeatures();
  renderArch();
  renderSteps();
  renderFaq();
  runDemoScene('excel');
  checkHealth();
}

/* ---- 展示页：功能特性 ---- */
function renderFeatures() {
  const feats = [
    { icon: 'chat', zh: ['自然语言分析', '用一句话提问，自动完成 Excel 数据统计'], en: ['Natural Language Analysis', 'Ask in one sentence; Excel statistics done automatically'] },
    { icon: 'database', zh: ['数据库对话', '连接 MySQL / SQLite，像聊天一样查询数据库'], en: ['Database Chat', 'Connect MySQL / SQLite and query like chatting'] },
    { icon: 'zap', zh: ['自动生成 SQL', 'LLM 根据表结构生成并执行 SQL，无需手写'], en: ['Auto SQL', 'LLM generates and executes SQL from table schemas'] },
    { icon: 'chartBar', zh: ['可视化图表', '分析结果自动生成柱状图 / 折线图 / 饼图等 9 种图表'], en: ['Visualizations', 'Auto-generated bar / line / pie and 9 chart types'] },
    { icon: 'mail', zh: ['邮件分享', '一句话将分析结果发送到邮箱'], en: ['Email Sharing', 'Send analysis results to your inbox with one sentence'] },
    { icon: 'cpu', zh: ['多模型支持', '支持多种大模型自由切换'], en: ['Multi-Model', 'Switch freely between multiple LLMs'] }
  ];
  const zh = I18N.lang() === 'zh';
  document.getElementById('feature-grid').innerHTML = feats
    .map(
      (f) => `<div class="card card-hover feature-card fade-in visible">
        <div class="icon-box">${icon(f.icon)}</div>
        <h3>${escapeHtml(zh ? f.zh[0] : f.en[0])}</h3>
        <p>${escapeHtml(zh ? f.zh[1] : f.en[1])}</p>
      </div>`
    )
    .join('');
}

/* ---- 展示页：架构卡片 ---- */
function renderArch() {
  const svcs = [
    { icon: 'shield', zh: ['网关子服务', '前端唯一入口：接收请求、鉴权、协议转换、负载均衡'], en: ['Gateway', 'Single entry: requests, auth, protocol conversion, load balancing'] },
    { icon: 'user', zh: ['用户子服务', '注册、登录、验证码、用户信息管理'], en: ['User Service', 'Registration, login, verification codes, user info'] },
    { icon: 'folder', zh: ['文件子服务', '文件上传 / 下载 / 删除 / 预览，关联文件与会话'], en: ['File Service', 'Upload / download / delete / preview, file-session mapping'] },
    { icon: 'table', zh: ['Excel 解析子服务', '解析 Excel 的 worksheet 表结构与数据'], en: ['Excel Parser', 'Parse worksheet schemas and data'] },
    { icon: 'database', zh: ['存储子服务', '数据库连接管理、表结构获取、执行 SQL'], en: ['Storage Service', 'Connection management, schema access, SQL execution'] },
    { icon: 'sparkle', zh: ['AI 子服务', '与 LLM 交互，生成并执行 SQL，总结与可视化'], en: ['AI Service', 'LLM interaction, SQL generation, summary & visualization'] },
    { icon: 'mail', zh: ['通知子服务', '发送邮箱验证码，发送 AI 分析结果到邮箱'], en: ['Notification', 'Email codes and AI analysis results delivery'] }
  ];
  const zh = I18N.lang() === 'zh';
  document.getElementById('arch-grid').innerHTML = svcs
    .map(
      (s) => `<div class="card card-hover arch-card fade-in visible">
        <div class="icon-box">${icon(s.icon)}</div>
        <h3>${escapeHtml(zh ? s.zh[0] : s.en[0])}</h3>
        <p>${escapeHtml(zh ? s.zh[1] : s.en[1])}</p>
      </div>`
    )
    .join('');
}

/* ---- 展示页：使用流程 ---- */
function renderSteps() {
  const steps = [
    { zh: ['上传文件 / 连接数据库', '支持 .xlsx、SQLite 文件与 MySQL 连接'], en: ['Upload / Connect', 'Supports .xlsx, SQLite files and MySQL'] },
    { zh: ['自然语言提问', '像聊天一样描述你的分析需求'], en: ['Ask in Natural Language', 'Describe your analysis like chatting'] },
    { zh: ['自动分析生成图表', 'LLM 生成 SQL 并执行，输出总结与可视化'], en: ['Auto Analysis & Charts', 'LLM generates SQL, returns summary & charts'] },
    { zh: ['邮件分享结果', '一句话把分析结果发送到邮箱'], en: ['Share by Email', 'Send results to your inbox in one sentence'] }
  ];
  const zh = I18N.lang() === 'zh';
  document.getElementById('steps-grid').innerHTML = steps
    .map(
      (s, i) => `<div class="card step fade-in visible">
        <div class="step-num">${i + 1}</div>
        <h3>${escapeHtml(zh ? s.zh[0] : s.en[0])}</h3>
        <p>${escapeHtml(zh ? s.zh[1] : s.en[1])}</p>
      </div>`
    )
    .join('');
}

/* ---- 展示页：FAQ ---- */
function renderFaq() {
  const faqs = [
    { zh: ['ChatExcel 支持哪些文件格式？', '目前支持 .xlsx 格式的 Excel 文件；数据库方面支持上传 SQLite 文件（.db / .sqlite / .sqlite3）或直接连接 MySQL。'], en: ['Which file formats are supported?', 'Excel files in .xlsx; for databases, upload SQLite files (.db / .sqlite / .sqlite3) or connect to MySQL directly.'] },
    { zh: ['分析结果可以导出或分享吗？', '可以。在对话中说「把结果发送到我邮箱」，系统会自动将分析结果发送到你的邮箱。'], en: ['Can I export or share results?', 'Yes. Say "email me the results" in the chat and the analysis will be sent to your inbox.'] },
    { zh: ['对数据库的修改会影响原表吗？', '不会。修改类 SQL 会复制一份新表（如 users → users_temp）在新表上执行，原表数据保持不变。'], en: ['Do modifications affect original tables?', 'No. Write SQL runs on a copied table (e.g. users → users_temp); original tables stay untouched.'] },
    { zh: ['支持哪些大模型？', '模型列表由后端配置，控制台内可随时切换当前可用的模型。'], en: ['Which LLMs are supported?', 'The model list is configured on the backend; you can switch between available models anytime in the console.'] }
  ];
  const zh = I18N.lang() === 'zh';
  const list = document.getElementById('faq-list');
  list.innerHTML = faqs
    .map(
      (f, i) => `<div class="card faq-item" data-faq="${i}">
        <button class="faq-q" type="button">${escapeHtml(zh ? f.zh[0] : f.en[0])}${icon('chevronDown')}</button>
        <div class="faq-a"><div class="faq-a-inner">${escapeHtml(zh ? f.zh[1] : f.en[1])}</div></div>
      </div>`
    )
    .join('');
  list.querySelectorAll('.faq-item').forEach((item) => {
    const q = item.querySelector('.faq-q');
    const a = item.querySelector('.faq-a');
    q.addEventListener('click', () => {
      const open = item.classList.toggle('open');
      a.style.maxHeight = open ? a.scrollHeight + 'px' : '0';
    });
  });
}

/* ---- 展示页：流式对话演示 ---- */
let demoToken = 0;

function _demoSchedule(token, fn, delay) {
  setTimeout(() => { if (token === demoToken) fn(); }, delay);
}

function _demoTypeText(token, el, text, speed, done) {
  let i = 0;
  const caret = '<span class="typing-caret"></span>';
  const step = () => {
    if (token !== demoToken) return;
    if (i <= text.length) {
      el.innerHTML = escapeHtml(text.slice(0, i)) + caret;
      i++;
      _demoSchedule(token, step, speed);
    } else {
      el.textContent = text;
      if (done) done();
    }
  };
  step();
}

function runDemoScene(scene) {
  demoToken++;
  const token = demoToken;
  const body = document.getElementById('demo-chat-body');
  body.innerHTML = '';

  const zh = I18N.lang() === 'zh';
  const q = I18N.t(scene === 'excel' ? 'demo.excelQ' : 'demo.dbQ');

  // 用户提问行（右侧）
  const userRow = document.createElement('div');
  userRow.className = 'conv-row user-row';
  const userAvatar = document.createElement('span');
  userAvatar.className = 'conv-avatar user';
  userAvatar.innerHTML = icon('user');
  const userBubble = document.createElement('div');
  userBubble.className = 'conv-bubble user-bubble';
  userRow.appendChild(userAvatar);
  userRow.appendChild(userBubble);
  body.appendChild(userRow);

  // AI 回复行（左侧）
  const aiRow = document.createElement('div');
  aiRow.className = 'conv-row';
  const aiAvatar = document.createElement('span');
  aiAvatar.className = 'conv-avatar ai';
  aiAvatar.innerHTML = icon('sparkle');
  const aiBubble = document.createElement('div');
  aiBubble.className = 'conv-bubble';
  aiRow.appendChild(aiAvatar);
  aiRow.appendChild(aiBubble);

  const finishExcel = () => {
    const title = document.createElement('div');
    title.className = 'conv-title';
    title.textContent = I18N.t('demo.excelTitle');
    aiBubble.appendChild(title);
    const tasks = ['demo.excelTask1', 'demo.excelTask2', 'demo.excelTask3'];
    tasks.forEach((key, idx) => {
      _demoSchedule(token, () => {
        const item = document.createElement('div');
        item.className = 'task-item';
        item.innerHTML = `${icon('check')}<span>${escapeHtml(I18N.t(key))}</span>`;
        aiBubble.appendChild(item);
        if (idx === tasks.length - 1) {
          _demoSchedule(token, () => {
            const chartEl = document.createElement('div');
            chartEl.className = 'demo-chart';
            aiBubble.appendChild(chartEl);
            ChartKit.renderDemoBar(chartEl,
              zh ? ['技术部', '市场部', '财务部', '人事部'] : ['Engineering', 'Marketing', 'Finance', 'HR'],
              [21500, 16800, 15200, 14300], '');
          }, 300);
        }
      }, 350 * (idx + 1));
    });
  };

  const finishDb = () => {
    const title = document.createElement('div');
    title.className = 'conv-title';
    title.textContent = I18N.t('demo.dbTitle');
    const desc = document.createElement('p');
    desc.style.cssText = 'color:var(--text-sub);font-size:13.5px;margin-bottom:4px;';
    desc.textContent = I18N.t('demo.dbDesc');
    const table = document.createElement('table');
    table.className = 'mini-table';
    table.innerHTML = `<thead><tr><th>${escapeHtml(I18N.t('demo.dbCol1'))}</th><th>${escapeHtml(I18N.t('demo.dbCol2'))}</th><th>${escapeHtml(I18N.t('demo.dbCol3'))}</th></tr></thead>
      <tbody><tr><td>iPhone 15</td><td>1,286</td><td>¥8,962,000</td></tr><tr><td>MateBook X</td><td>742</td><td>¥6,318,000</td></tr><tr><td>AirPods Pro</td><td>2,915</td><td>¥5,243,000</td></tr></tbody>`;
    aiBubble.appendChild(title);
    aiBubble.appendChild(desc);
    _demoSchedule(token, () => aiBubble.appendChild(table), 350);
  };

  // 流式播放：先打字用户提问，再追加 AI 回复并打字，最后渐入结果
  _demoTypeText(token, userBubble, q, 55, () => {
    _demoSchedule(token, () => {
      body.appendChild(aiRow);
      const summary = scene === 'excel'
        ? I18N.t('hero.excelSummary')
        : I18N.t('hero.dbSummary');
      _demoTypeText(token, aiBubble, summary, 22, () => {
        _demoSchedule(token, scene === 'excel' ? finishExcel : finishDb, 300);
      });
    }, 450);
  });
}

/* ---- 展示页：健康检测 ---- */
async function checkHealth() {
  const dot = document.getElementById('health-dot');
  const text = document.getElementById('health-text');
  const result = await API.health();
  if (result && result.status === 'healthy') {
    dot.classList.remove('offline');
    text.textContent = I18N.t('footer.online');
  } else {
    dot.classList.add('offline');
    text.textContent = I18N.t('footer.offline');
  }
}

/* ============================================================
   控制台逻辑（console.html）
   ============================================================ */

/** 控制台全局状态 */
const ConsoleState = {
  models: [], // [{ modelName, modelDesc }]
  currentModel: '',
  userInfo: null,
  activeModule: 'chat'
};

/* ---------------- 模型选择器组件 ---------------- */
function createModelPicker(container) {
  container.innerHTML = `
    <button class="model-picker-toggle" type="button">
      ${icon('cpu')}<span>${escapeHtml(I18N.t('console.model'))}:</span>
      <span class="model-name">-</span>${icon('chevronDown')}
    </button>
    <div class="model-picker-list"></div>`;
  const toggle = container.querySelector('.model-picker-toggle');
  const list = container.querySelector('.model-picker-list');
  const nameEl = container.querySelector('.model-name');

  const render = () => {
    nameEl.textContent = ConsoleState.currentModel || '-';
    list.innerHTML = ConsoleState.models
      .map(
        (m) => `<button class="model-option ${m.modelName === ConsoleState.currentModel ? 'selected' : ''}" type="button" data-model="${escapeHtml(m.modelName)}">
          <span class="mo-name">${escapeHtml(m.modelName)}</span>
          <span class="mo-desc">${escapeHtml(m.modelDesc || '')}</span>
        </button>`
      )
      .join('');
  };

  toggle.addEventListener('click', () => list.classList.toggle('open'));
  list.addEventListener('click', (e) => {
    const btn = e.target.closest('.model-option');
    if (!btn) return;
    ConsoleState.currentModel = btn.dataset.model;
    localStorage.setItem(CONFIG.KEYS.MODEL, ConsoleState.currentModel);
    list.classList.remove('open');
    render();
  });
  document.addEventListener('click', (e) => {
    if (!container.contains(e.target)) list.classList.remove('open');
  });

  return { render };
}

/* ---------------- 聊天引擎（三个模块共用） ---------------- */
/**
 * @param {object} opts {
 *   messagesEl, inputEl, sendBtn, hintEl,
 *   structured: bool,              // excel/database 启用标签解析
 *   buildPayload: (message) => body // 构造 sendStreamMessage 请求体，返回 null 表示拦截
 * }
 */
function createChatEngine(opts) {
  const engine = {
    chatSessionId: '',
    abortController: null,
    busy: false,

    setSession(id) {
      this.chatSessionId = id || '';
    },

    clearMessages() {
      opts.messagesEl.innerHTML = '';
      this._appendEmptyHint();
    },

    _appendEmptyHint() {
      if (opts.messagesEl.children.length === 0) {
        const hint = document.createElement('div');
        hint.className = 'empty-state';
        hint.dataset.chatHint = '1';
        hint.innerHTML = `${icon('chat')}<div class="empty-desc">${escapeHtml(I18N.t('console.noSession'))}</div>`;
        opts.messagesEl.appendChild(hint);
      }
    },

    _removeEmptyHint() {
      opts.messagesEl.querySelectorAll('[data-chat-hint]').forEach((el) => el.remove());
    },

    appendUserMessage(text) {
      this._removeEmptyHint();
      const { root, bodyEl } = buildMessageEl('user');
      bodyEl.textContent = text;
      opts.messagesEl.appendChild(root);
      this._scrollBottom();
    },

    /** 渲染历史消息列表 */
    renderHistory(messageList, finalResults) {
      opts.messagesEl.innerHTML = '';
      (messageList || []).forEach((m) => {
        const { root, bodyEl } = buildMessageEl(m.role);
        if (m.role === 'user') {
          bodyEl.textContent = m.content;
        } else {
          // assistant：先尝试判定最终结果帧
          let parsed = null;
          const trimmed = (m.content || '').trim();
          if (trimmed.startsWith('{')) {
            try {
              const obj = JSON.parse(trimmed);
              if (obj && 'summary' in obj && 'displayType' in obj) parsed = obj;
            } catch (e) { /* 非结果帧 */ }
          }
          if (parsed) {
            bodyEl.innerHTML = `<div class="ai-analysis"></div>`;
            bodyEl.querySelector('.ai-analysis').textContent = parsed.summary || '';
          } else {
            renderAssistantHistory(bodyEl, m.content || '', opts.structured);
          }
        }
        opts.messagesEl.appendChild(root);
      });
      this._scrollBottom();
    },

    /** 发送消息（流式） */
    async send() {
      if (this.busy) {
        // 二次点击 = 停止生成
        this.abortController && this.abortController.abort();
        return;
      }
      const message = opts.inputEl.value.trim();
      if (!message) return;
      const body = opts.buildPayload(message);
      if (!body) return; // 被拦截（如未建会话）

      opts.inputEl.value = '';
      this._autoGrow(opts.inputEl);
      this.appendUserMessage(message);

      // AI 消息骨架
      this._removeEmptyHint();
      const { root, bodyEl } = buildMessageEl('assistant');
      opts.messagesEl.appendChild(root);
      const renderer = createAiStreamRenderer(bodyEl, opts.structured);
      this._scrollBottom();

      this.busy = true;
      this._setSendState(true);
      this.abortController = new AbortController();
      let gotContent = false;

      await apiSendStreamMessage(
        Object.assign({ message }, body),
        {
          signal: this.abortController.signal,
          onText: (content) => {
            gotContent = true;
            renderer.append(content);
            this._scrollBottom();
          },
          onFinalResult: (obj) => {
            gotContent = true;
            opts.onFinalResult && opts.onFinalResult(obj);
            // 气泡内给出简短指示，引导用户查看结果分析区
            const note = document.createElement('div');
            note.className = 'ai-analysis';
            note.style.color = 'var(--accent)';
            note.textContent = I18N.t('console.resultAnalysis') + ' ✓';
            bodyEl.appendChild(note);
          },
          onError: (msg) => {
            toast(msg, 'error');
          },
          onDone: () => {
            renderer.finish();
            if (!gotContent && !renderer.hasContent()) {
              bodyEl.innerHTML = `<div class="ai-stream-text" style="color:var(--text-sub)">…</div>`;
            }
            this.busy = false;
            this.abortController = null;
            this._setSendState(false);
            this._scrollBottom();
          }
        }
      );
    },

    _setSendState(streaming) {
      if (streaming) {
        opts.sendBtn.classList.add('stop-mode');
        opts.sendBtn.innerHTML = icon('stop');
        opts.sendBtn.title = I18N.t('console.stop');
      } else {
        opts.sendBtn.classList.remove('stop-mode');
        opts.sendBtn.innerHTML = icon('send');
        opts.sendBtn.title = '';
      }
    },

    _scrollBottom() {
      opts.messagesEl.scrollTop = opts.messagesEl.scrollHeight;
    },

    _autoGrow(ta) {
      ta.style.height = 'auto';
      ta.style.height = Math.min(ta.scrollHeight, 130) + 'px';
    },

    bindInput() {
      opts.sendBtn.innerHTML = icon('send');
      opts.sendBtn.addEventListener('click', () => this.send());
      opts.inputEl.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
          e.preventDefault();
          this.send();
        }
      });
      opts.inputEl.addEventListener('input', () => this._autoGrow(opts.inputEl));
      if (opts.hintEl) {
        opts.hintEl.innerHTML = `${icon('mail')}<span>${escapeHtml(I18N.t('console.emailHint'))}</span>`;
      }
      this.clearMessages();
    }
  };
  return engine;
}

/* ---------------- 控制台初始化 ---------------- */
async function initConsolePage() {
  // 图标
  document.getElementById('console-logo').innerHTML = icon('logo');
  document.getElementById('user-avatar').innerHTML = icon('user');
  document.getElementById('topbar-burger').innerHTML = icon('menu');
  document.getElementById('excel-upload-icon').innerHTML = icon('upload');
  document.getElementById('sqlite-upload-icon').innerHTML = icon('upload');
  document.getElementById('profile-avatar').innerHTML = icon('user');

  // 侧边栏文案与图标
  const sideIcons = { chat: 'chat', excel: 'table', database: 'database', files: 'folder', history: 'history', profile: 'user' };
  const sideKeys = { chat: 'console.modChat', excel: 'console.modExcel', database: 'console.modDb', files: 'console.modFiles', history: 'console.modHistory', profile: 'console.modProfile' };
  document.querySelectorAll('.sidebar-item').forEach((btn) => {
    const m = btn.dataset.module;
    btn.innerHTML = `${icon(sideIcons[m])}<span data-i18n="${sideKeys[m]}"></span>`;
  });

  // 按钮文案
  setBtn('chat-new-session', 'plus', 'console.newSession');
  setBtn('excel-new-session', 'plus', 'console.newSession');
  setBtn('db-new-session', 'plus', 'console.newSession');
  setBtn('db-back-manage', 'chevronLeft', 'db.backManage');
  setBtn('files-refresh', 'refresh', 'console.refresh');
  setBtn('history-refresh', 'refresh', 'console.refresh');
  setBtn('profile-logout', 'logout', 'console.logout');
  document.getElementById('excel-chat-title').innerHTML = `${icon('sparkle')}<span>ChatExcel</span>`;
  document.getElementById('db-chat-title').innerHTML = `${icon('sparkle')}<span>ChatExcel</span>`;

  I18N.apply();
  I18N.bindSwitch(() => {
    renderDbConnList();
    FilesModule.render();
    HistoryModule.render();
  });

  /* ---- 1. 恢复登录态 ---- */
  const sid = getSessionId();
  if (!sid) {
    window.location.href = 'login.html';
    return;
  }
  showLoading(I18N.t('common.loading'));
  try {
    await API.sessionLogin(sid);
  } catch (e) {
    clearSessionId();
    window.location.href = 'login.html';
    return;
  }

  /* ---- 2. 用户信息 + 3. 模型列表（并行） ---- */
  try {
    const [userResult, modelResult] = await Promise.all([API.userInfo(), API.aiModels()]);
    ConsoleState.userInfo = (userResult && userResult.userInfo) || null;
    ConsoleState.models = (modelResult && modelResult.modelList) || [];
    const saved = localStorage.getItem(CONFIG.KEYS.MODEL);
    ConsoleState.currentModel =
      (ConsoleState.models.find((m) => m.modelName === saved) || ConsoleState.models[0] || {}).modelName || '';
  } catch (e) {
    hideLoading();
    toast(I18N.t('common.serviceUnavailable'), 'error');
    return;
  }
  hideLoading();

  const nickname = ConsoleState.userInfo ? ConsoleState.userInfo.nickname : '';
  document.getElementById('user-nickname').textContent = nickname || '-';
  document.getElementById('profile-name').textContent = nickname || '-';
  document.getElementById('profile-nickname').textContent = nickname || '-';
  document.getElementById('profile-email').textContent = (ConsoleState.userInfo && ConsoleState.userInfo.email) || '-';

  // 模型选择器
  const pickers = [
    createModelPicker(document.getElementById('chat-model-picker')),
    createModelPicker(document.getElementById('excel-model-picker')),
    createModelPicker(document.getElementById('db-model-picker'))
  ];
  pickers.forEach((p) => p.render());

  // 侧边栏切换
  document.querySelectorAll('.sidebar-item').forEach((btn) => {
    btn.addEventListener('click', () => switchModule(btn.dataset.module));
  });
  // 移动端侧边栏
  const sidebar = document.getElementById('console-sidebar');
  const mask = document.getElementById('sidebar-mask');
  document.getElementById('topbar-burger').addEventListener('click', () => {
    sidebar.classList.toggle('open');
    mask.classList.toggle('show', sidebar.classList.contains('open'));
  });
  mask.addEventListener('click', () => {
    sidebar.classList.remove('open');
    mask.classList.remove('show');
  });

  ChatModule.init();
  ExcelModule.init();
  DatabaseModule.init();
  FilesModule.init();
  HistoryModule.init();
  ProfileModule.init();
}

function setBtn(id, iconName, i18nKey) {
  const btn = document.getElementById(id);
  if (btn) btn.innerHTML = `${icon(iconName)}<span>${escapeHtml(I18N.t(i18nKey))}</span>`;
}

function switchModule(name) {
  ConsoleState.activeModule = name;
  document.querySelectorAll('.sidebar-item').forEach((b) => {
    b.classList.toggle('active', b.dataset.module === name);
  });
  document.querySelectorAll('.module-panel').forEach((p) => {
    p.classList.toggle('active', p.id === `module-${name}`);
  });
  // 移动端收起侧边栏
  document.getElementById('console-sidebar').classList.remove('open');
  document.getElementById('sidebar-mask').classList.remove('show');

  if (name === 'files') FilesModule.load();
  if (name === 'history') HistoryModule.load();
}

/* ================= 模块 1：AI 聊天（plain） ================= */
const ChatModule = {
  engine: null,

  init() {
    this.engine = createChatEngine({
      messagesEl: document.getElementById('chat-messages'),
      inputEl: document.getElementById('chat-input'),
      sendBtn: document.getElementById('chat-send'),
      hintEl: null,
      structured: false,
      buildPayload: (message) => {
        if (!this.engine.chatSessionId) {
          toast(I18N.t('console.needSession'), 'error');
          return null;
        }
        return { chatSessionId: this.engine.chatSessionId, chatType: 'plain' };
      }
    });
    this.engine.bindInput();

    document.getElementById('chat-new-session').addEventListener('click', () => this.newSession());
  },

  async newSession() {
    if (!ConsoleState.currentModel) {
      toast(I18N.t('common.serviceUnavailable'), 'error');
      return;
    }
    try {
      const result = await API.aiSessionCreate({
        modelName: ConsoleState.currentModel,
        sessionType: 'plain'
      });
      this.engine.setSession(result.chatSessionId);
      this.engine.clearMessages();
      toast(I18N.t('console.sessionCreated'), 'success');
    } catch (e) { /* 已提示 */ }
  },

  /** 历史恢复入口 */
  restore(chatSessionId, messageList) {
    this.engine.setSession(chatSessionId);
    this.engine.renderHistory(messageList);
    switchModule('chat');
  }
};

/* ================= 模块 2：Excel 智能助手 ================= */
const ExcelModule = {
  engine: null,
  preview: null,
  fileId: '',

  init() {
    const previewArea = document.getElementById('excel-preview-area');
    this.preview = new ExcelPreview(previewArea, {
      onSheetsLoaded: (names) => this._fillSheetSelect(names)
    });

    // 内层 Tab
    document.querySelectorAll('#excel-tabs .inner-tab').forEach((btn) => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('#excel-tabs .inner-tab').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        document.querySelectorAll('#module-excel .split-left .pane').forEach((p) => {
          const active = p.dataset.pane === btn.dataset.pane;
          p.classList.toggle('active', active);
          p.style.display = active ? 'flex' : 'none';
        });
      });
    });

    // 上传区
    const zone = document.getElementById('excel-upload-zone');
    const fileInput = document.getElementById('excel-file-input');
    zone.addEventListener('click', () => fileInput.click());
    zone.addEventListener('dragover', (e) => { e.preventDefault(); zone.classList.add('dragover'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('dragover'));
    zone.addEventListener('drop', (e) => {
      e.preventDefault();
      zone.classList.remove('dragover');
      if (e.dataTransfer.files.length) this.upload(e.dataTransfer.files[0]);
    });
    fileInput.addEventListener('change', () => {
      if (fileInput.files.length) this.upload(fileInput.files[0]);
      fileInput.value = '';
    });

    // WorkSheet 选择器联动
    document.getElementById('excel-sheet-select').addEventListener('change', (e) => {
      this.preview.switchSheet(e.target.value);
    });

    // 聊天引擎
    this.engine = createChatEngine({
      messagesEl: document.getElementById('excel-messages'),
      inputEl: document.getElementById('excel-input'),
      sendBtn: document.getElementById('excel-send'),
      hintEl: document.getElementById('excel-hint'),
      structured: true,
      buildPayload: (message) => {
        if (!this.fileId) { toast(I18N.t('console.needFile'), 'error'); return null; }
        if (!this.engine.chatSessionId) { toast(I18N.t('console.needSession'), 'error'); return null; }
        return { chatSessionId: this.engine.chatSessionId, chatType: 'excel', fileId: this.fileId };
      },
      onFinalResult: (obj) => this.showResult(obj)
    });
    this.engine.bindInput();

    document.getElementById('excel-new-session').addEventListener('click', () => this.newSession());
    this._setResultEmpty();
  },

  _fillSheetSelect(names) {
    const sel = document.getElementById('excel-sheet-select');
    sel.innerHTML = names.map((n) => `<option value="${escapeHtml(n)}">${escapeHtml(n)}</option>`).join('');
  },

  async upload(file) {
    showLoading(I18N.t('common.loading'));
    try {
      const fileId = await uploadExcelFile(file);
      this.fileId = fileId;
      await this.preview.load(fileId);
      document.getElementById('excel-upload-zone').style.display = 'none';
      toast(I18N.t('console.uploadOk'), 'success');
      // 若已有会话，关联文件与会话
      if (this.engine.chatSessionId) {
        API.fileChatMap(fileId, this.engine.chatSessionId).catch(() => {});
      }
    } catch (e) { /* 已提示 */ }
    hideLoading();
  },

  /** 外部（我的文件）携 fileId 进入 */
  async openFile(fileId) {
    switchModule('excel');
    showLoading(I18N.t('common.loading'));
    try {
      this.fileId = fileId;
      await this.preview.load(fileId);
      document.getElementById('excel-upload-zone').style.display = 'none';
    } catch (e) { /* 已提示 */ }
    hideLoading();
  },

  async newSession() {
    if (!ConsoleState.currentModel) { toast(I18N.t('common.serviceUnavailable'), 'error'); return; }
    if (!this.fileId) { toast(I18N.t('console.needFile'), 'error'); return; }
    try {
      const result = await API.aiSessionCreate({
        modelName: ConsoleState.currentModel,
        sessionType: 'excel'
      });
      this.engine.setSession(result.chatSessionId);
      this.engine.clearMessages();
      await API.fileChatMap(this.fileId, result.chatSessionId);
      toast(I18N.t('console.sessionCreated'), 'success');
    } catch (e) { /* 已提示 */ }
  },

  showResult(obj) {
    document.getElementById('excel-result-empty').style.display = 'none';
    const content = document.getElementById('excel-result-content');
    content.style.display = '';
    document.getElementById('excel-result-summary').textContent = obj.summary || '';
    ChartKit.renderResult(document.getElementById('excel-result-chart'), obj);
    // 自动切到结果分析 Tab
    const tab = document.querySelector('#excel-tabs .inner-tab[data-pane="excel-result"]');
    tab && tab.click();
  },

  _setResultEmpty() {
    document.getElementById('excel-result-empty').innerHTML = `${icon('chartBar')}<div class="empty-desc">${escapeHtml(I18N.t('console.resultEmpty'))}</div>`;
  },

  /** 历史恢复入口 */
  async restore(chatSessionId, fileId, messageList, finalResult) {
    switchModule('excel');
    this.engine.setSession(chatSessionId);
    this.engine.renderHistory(messageList);
    if (finalResult) this.showResult(finalResult);
    if (fileId) {
      try {
        this.fileId = fileId;
        await this.preview.load(fileId);
        document.getElementById('excel-upload-zone').style.display = 'none';
      } catch (e) { /* 已提示 */ }
    }
  }
};

/* ================= 模块 3：数据库智能助手 ================= */
const DatabaseModule = {
  engine: null,
  /** 当前连接 */
  conn: null, // { connectionId, type: 'MySQL'|'SQLite', ...info }
  tables: [],
  currentTable: '',
  sqliteFiles: [],
  selectedSqliteFileId: '',

  init() {
    // MySQL / SQLite Tab
    document.querySelectorAll('#db-type-tabs .inner-tab').forEach((btn) => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('#db-type-tabs .inner-tab').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById('db-pane-mysql').style.display = btn.dataset.dbtab === 'mysql' ? '' : 'none';
        document.getElementById('db-pane-sqlite').style.display = btn.dataset.dbtab === 'sqlite' ? '' : 'none';
        if (btn.dataset.dbtab === 'sqlite') this.loadSqliteFiles();
      });
    });

    document.getElementById('db-conn-title').innerHTML = `${icon('link')}<span data-i18n="db.connList"></span>`;
    document.getElementById('db-newconn-title').innerHTML = `${icon('plus')}<span data-i18n="db.newConn"></span>`;
    document.getElementById('sqlite-upload-title').innerHTML = `${icon('upload')}<span data-i18n="db.sqliteUploadTitle"></span>`;
    document.getElementById('sqlite-connect-title').innerHTML = `${icon('link')}<span data-i18n="db.sqliteConnectTitle"></span>`;

    // MySQL 表单
    document.getElementById('db-mysql-form').addEventListener('submit', (e) => {
      e.preventDefault();
      this.connectMySQL(e.target);
    });

    // SQLite 上传
    const zone = document.getElementById('sqlite-upload-zone');
    const input = document.getElementById('sqlite-file-input');
    zone.addEventListener('click', () => input.click());
    input.addEventListener('change', async () => {
      if (!input.files.length) return;
      showLoading(I18N.t('common.loading'));
      try {
        await uploadSqliteFile(input.files[0]);
        toast(I18N.t('console.uploadOk'), 'success');
        await this.loadSqliteFiles();
      } catch (e) { /* 已提示 */ }
      hideLoading();
      input.value = '';
    });
    zone.addEventListener('dragover', (e) => { e.preventDefault(); zone.classList.add('dragover'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('dragover'));
    zone.addEventListener('drop', async (e) => {
      e.preventDefault();
      zone.classList.remove('dragover');
      if (!e.dataTransfer.files.length) return;
      showLoading(I18N.t('common.loading'));
      try {
        await uploadSqliteFile(e.dataTransfer.files[0]);
        toast(I18N.t('console.uploadOk'), 'success');
        await this.loadSqliteFiles();
      } catch (err) { /* 已提示 */ }
      hideLoading();
    });

    document.getElementById('sqlite-connect-btn').addEventListener('click', () => this.connectSqlite());

    // 表预览 / 结果分析 Tab
    document.querySelectorAll('#db-inner-tabs .inner-tab').forEach((btn) => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('#db-inner-tabs .inner-tab').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        document.querySelectorAll('#db-chat-view .split-left .pane').forEach((p) => {
          const active = p.dataset.pane === btn.dataset.pane;
          p.classList.toggle('active', active);
          p.style.display = active ? (p.dataset.pane === 'db-table' ? 'flex' : 'block') : 'none';
        });
      });
    });

    // 表选择器联动
    document.getElementById('db-table-select').addEventListener('change', (e) => {
      this.currentTable = e.target.value;
      this.loadTableData(this.currentTable);
    });

    document.getElementById('db-back-manage').addEventListener('click', () => this.showManageView());

    // 聊天引擎
    this.engine = createChatEngine({
      messagesEl: document.getElementById('db-messages'),
      inputEl: document.getElementById('db-input'),
      sendBtn: document.getElementById('db-send'),
      hintEl: document.getElementById('db-hint'),
      structured: true,
      buildPayload: (message) => {
        if (!this.conn) { toast(I18N.t('console.needSession'), 'error'); return null; }
        if (!this.engine.chatSessionId) { toast(I18N.t('console.needSession'), 'error'); return null; }
        if (!this.currentTable) { toast(I18N.t('console.needTable'), 'error'); return null; }
        return {
          chatSessionId: this.engine.chatSessionId,
          chatType: 'database',
          dbConnectId: this.conn.connectionId,
          dbType: this.conn.type === 'MySQL' ? 'MYSQL' : 'SQLITE',
          tableName: this.currentTable
        };
      },
      onFinalResult: (obj) => this.showResult(obj)
    });
    this.engine.bindInput();

    document.getElementById('db-new-session').addEventListener('click', () => this.newSession());

    this.renderDbConnListInit();
    document.getElementById('db-result-empty').innerHTML = `${icon('chartBar')}<div class="empty-desc">${escapeHtml(I18N.t('console.resultEmpty'))}</div>`;
    document.getElementById('db-table-empty').innerHTML = `${icon('table')}<div class="empty-desc">${escapeHtml(I18N.t('console.tableEmpty'))}</div>`;
  },

  /* ---- MySQL 连接列表（前端维护，localStorage 持久化） ---- */
  _loadConns() {
    try {
      return JSON.parse(localStorage.getItem(CONFIG.KEYS.MYSQL_CONNS) || '[]');
    } catch (e) {
      return [];
    }
  },
  _saveConns(conns) {
    localStorage.setItem(CONFIG.KEYS.MYSQL_CONNS, JSON.stringify(conns));
  },

  renderDbConnListInit() {
    renderDbConnList();
  },

  async connectMySQL(form) {
    const host = form.host.value.trim();
    const port = parseInt(form.port.value, 10);
    const name = form.name.value.trim();
    const username = form.username.value.trim();
    const password = form.password.value;
    const charset = form.charset.value;

    let ok = true;
    const setErr = (field, msg) => {
      const g = form.querySelector(`.form-group[data-field="${field}"]`);
      if (!g) return;
      g.classList.toggle('error', !!msg);
      const input = g.querySelector('.input');
      input && input.classList.toggle('has-error', !!msg);
      const err = g.querySelector('.error-msg');
      if (err) err.textContent = msg || '';
    };
    setErr('host', host ? '' : I18N.t('db.errHost')); if (!host) ok = false;
    setErr('port', port >= 1 && port <= 65535 ? '' : I18N.t('db.errPort')); if (!(port >= 1 && port <= 65535)) ok = false;
    setErr('name', name ? '' : I18N.t('db.errName')); if (!name) ok = false;
    setErr('username', username ? '' : I18N.t('db.errUser')); if (!username) ok = false;
    if (!ok) return;

    const mysqlInfo = { host, port, name, username, password, charset };
    showLoading(I18N.t('common.loading'));
    try {
      const result = await API.dbConnect({ type: 'MySQL', MySQL: mysqlInfo });
      const conns = this._loadConns();
      conns.push(Object.assign({ connectionId: result.connectionId }, mysqlInfo));
      this._saveConns(conns);
      renderDbConnList();
      toast(I18N.t('console.connOk'), 'success');
      await this.enterConnection({ connectionId: result.connectionId, type: 'MySQL', info: mysqlInfo });
    } catch (e) { /* 已提示 */ }
    hideLoading();
  },

  async loadSqliteFiles() {
    try {
      const result = await API.fileList();
      this.sqliteFiles = (result.fileList || []).filter((f) => isSqliteFile(f.fileName));
      const list = document.getElementById('sqlite-file-list');
      if (!this.sqliteFiles.length) {
        list.innerHTML = `<div class="empty-state" style="padding:20px"><div class="empty-desc">${escapeHtml(I18N.t('db.sqliteEmpty'))}</div></div>`;
        document.getElementById('sqlite-connect-btn').disabled = true;
        return;
      }
      list.innerHTML = this.sqliteFiles
        .map(
          (f) => `<div class="sqlite-file-item ${f.fileId === this.selectedSqliteFileId ? 'selected' : ''}" data-fileid="${escapeHtml(f.fileId)}">
            ${icon('database')}<span>${escapeHtml(f.fileName)}</span>
          </div>`
        )
        .join('');
      list.querySelectorAll('.sqlite-file-item').forEach((item) => {
        item.addEventListener('click', () => {
          this.selectedSqliteFileId = item.dataset.fileid;
          list.querySelectorAll('.sqlite-file-item').forEach((i) => i.classList.toggle('selected', i === item));
          document.getElementById('sqlite-connect-btn').disabled = false;
        });
      });
    } catch (e) { /* 已提示 */ }
  },

  async connectSqlite() {
    if (!this.selectedSqliteFileId) return;
    const file = this.sqliteFiles.find((f) => f.fileId === this.selectedSqliteFileId);
    showLoading(I18N.t('common.loading'));
    try {
      const result = await API.dbConnect({ type: 'SQLite', SQLite: { fileId: this.selectedSqliteFileId, readonly: false } });
      toast(I18N.t('console.connOk'), 'success');
      await this.enterConnection({
        connectionId: result.connectionId,
        type: 'SQLite',
        info: { fileId: this.selectedSqliteFileId, fileName: file ? file.fileName : '' }
      });
    } catch (e) { /* 已提示 */ }
    hideLoading();
  },

  /** 进入聊天界面：加载表列表 */
  async enterConnection(conn) {
    this.conn = conn;
    document.getElementById('db-manage-view').style.display = 'none';
    document.getElementById('db-chat-view').style.display = '';
    document.getElementById('db-back-manage').style.display = '';
    document.getElementById('db-new-session').style.display = '';

    try {
      // 调试日志: 进入连接时打印连接信息与将要请求的表列表接口参数
      console.log('[db] enterConnection 连接信息:', conn);
      const result = await API.dbTables(conn.connectionId);
      this.tables = result.tables || [];
      // 调试日志: 表列表接口返回结果
      console.log('[db] dbTables 返回, 表数量:', this.tables.length, result);
      const sel = document.getElementById('db-table-select');
      sel.innerHTML = this.tables.map((t) => `<option value="${escapeHtml(t)}">${escapeHtml(t)}</option>`).join('');
      this.currentTable = this.tables[0] || '';
      // 调试日志: 默认选中表
      console.log('[db] 默认选中表:', this.currentTable);
      if (this.currentTable) await this.loadTableData(this.currentTable);
      this.refreshConnStatus();
    } catch (e) {
      // 调试日志: 表列表加载失败时打印完整错误
      console.error('[db!] enterConnection 加载表列表失败:', e);
    }
  },

  async loadTableData(tableName) {
    const area = document.getElementById('db-table-area');
    // 调试日志: 加载表数据前打印目标表名
    console.log('[db] loadTableData 请求表数据, tableName:', tableName);
    try {
      const result = await API.dbTableData(this.conn.connectionId, tableName);
      // 调试日志: 表数据接口原始返回
      console.log('[db] dbTableData 返回:', result);
      const schema = result.tableSchema || {};
      const cols = (schema.columnInfo || []).map((c) => c);
      const rows = ((schema.tableData && schema.tableData.rows) || []).map((r) => r.cells || []);
      // 调试日志: 解析后的列数与行数, 便于判断是返回空还是结构不匹配
      console.log('[db] 解析结果 列数:', cols.length, '行数:', rows.length, 'cols:', cols.map((c) => `${c.name}:${c.type}`).join(', '));

      area.innerHTML = '';
      const table = document.createElement('table');
      table.className = 'excel-table';
      const thead = document.createElement('thead');
      const trh = document.createElement('tr');
      const corner = document.createElement('th');
      corner.className = 'row-num';
      corner.textContent = '#';
      trh.appendChild(corner);
      cols.forEach((c) => {
        const th = document.createElement('th');
        th.innerHTML = `${escapeHtml(c.name)}<span class="col-type">${escapeHtml(c.type || '')}</span>`;
        trh.appendChild(th);
      });
      thead.appendChild(trh);
      table.appendChild(thead);
      const tbody = document.createElement('tbody');
      rows.forEach((row, ri) => {
        const tr = document.createElement('tr');
        const num = document.createElement('td');
        num.className = 'row-num';
        num.textContent = ri + 1;
        tr.appendChild(num);
        row.forEach((cell) => {
          const td = document.createElement('td');
          td.textContent = cell ?? '';
          tr.appendChild(td);
        });
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      area.appendChild(table);
    } catch (e) {
      // 调试日志: 表数据加载失败时打印完整错误
      console.error('[db!] loadTableData 加载表数据失败:', tableName, e);
    }
  },

  async refreshConnStatus() {
    if (!this.conn) return;
    try {
      const result = await API.dbConnectionStatus(this.conn.connectionId);
      const bar = document.getElementById('db-status-bar');
      if (result && result.hasModifications && result.tempTables && result.tempTables.length) {
        bar.style.display = '';
        bar.innerHTML = `${icon('info')}<span>${escapeHtml(I18N.t('console.tempTableTip').replace('{tables}', result.tempTables.join(', ')))}</span>`;
      } else {
        bar.style.display = 'none';
      }
    } catch (e) { /* 忽略 */ }
  },

  showManageView() {
    document.getElementById('db-chat-view').style.display = 'none';
    document.getElementById('db-manage-view').style.display = '';
    document.getElementById('db-back-manage').style.display = 'none';
    document.getElementById('db-new-session').style.display = 'none';
    renderDbConnList();
  },

  async newSession() {
    if (!this.conn) { toast(I18N.t('console.needSession'), 'error'); return; }
    if (!ConsoleState.currentModel) { toast(I18N.t('common.serviceUnavailable'), 'error'); return; }
    // dbConnectionInfo：前端定义结构，用于历史会话恢复
    const info =
      this.conn.type === 'MySQL'
        ? { type: 'MySQL', host: this.conn.info.host, port: this.conn.info.port, name: this.conn.info.name, username: this.conn.info.username, password: this.conn.info.password, charset: this.conn.info.charset }
        : { type: 'SQLite', fileId: this.conn.info.fileId, fileName: this.conn.info.fileName };
    try {
      const result = await API.aiSessionCreate({
        modelName: ConsoleState.currentModel,
        sessionType: 'database',
        dbConnectionInfo: JSON.stringify(info)
      });
      this.engine.setSession(result.chatSessionId);
      this.engine.clearMessages();
      toast(I18N.t('console.sessionCreated'), 'success');
    } catch (e) { /* 已提示 */ }
  },

  showResult(obj) {
    document.getElementById('db-result-empty').style.display = 'none';
    document.getElementById('db-result-content').style.display = '';
    document.getElementById('db-result-summary').textContent = obj.summary || '';
    ChartKit.renderResult(document.getElementById('db-result-chart'), obj);
    const tab = document.querySelector('#db-inner-tabs .inner-tab[data-pane="db-result"]');
    tab && tab.click();
    this.refreshConnStatus();
  },

  /** 历史恢复：解析 dbConnectionInfo → 重新连接 → 恢复场景 */
  async restore(chatSessionId, dbConnectionInfoStr, messageList, finalResult) {
    switchModule('database');
    let info = null;
    try {
      info = JSON.parse(dbConnectionInfoStr);
    } catch (e) {
      toast(I18N.t('common.paramError'), 'error');
      return;
    }
    showLoading(I18N.t('console.restoreConn'));
    try {
      let connResult;
      if (info.type === 'MySQL' || info.host !== undefined) {
        connResult = await API.dbConnect({
          type: 'MySQL',
          MySQL: { host: info.host, port: info.port || 3306, name: info.name, username: info.username, password: info.password, charset: info.charset || 'utf8mb4' }
        });
        this.conn = { connectionId: connResult.connectionId, type: 'MySQL', info };
      } else {
        connResult = await API.dbConnect({ type: 'SQLite', SQLite: { fileId: info.fileId, readonly: false } });
        this.conn = { connectionId: connResult.connectionId, type: 'SQLite', info };
      }
      await this.enterConnection(this.conn);
      this.engine.setSession(chatSessionId);
      this.engine.renderHistory(messageList);
      if (finalResult) this.showResult(finalResult);
    } catch (e) { /* 已提示 */ }
    hideLoading();
  }
};

/** 渲染 MySQL 已连接列表（连接管理页） */
function renderDbConnList() {
  const grid = document.getElementById('db-conn-grid');
  const empty = document.getElementById('db-conn-empty');
  if (!grid) return;
  const conns = DatabaseModule._loadConns();
  if (!conns.length) {
    grid.innerHTML = '';
    empty.style.display = '';
    empty.innerHTML = `${icon('database')}<div class="empty-desc">${escapeHtml(I18N.t('db.connEmpty'))}</div>`;
    return;
  }
  empty.style.display = 'none';
  grid.innerHTML = conns
    .map(
      (c) => `<div class="card conn-card" data-connid="${escapeHtml(c.connectionId)}">
        <div class="conn-summary">${icon('database')}<span>${escapeHtml(c.host)}:${c.port}/${escapeHtml(c.name)}</span></div>
        <div class="conn-meta">${escapeHtml(c.username)} · ${escapeHtml(c.charset || 'utf8mb4')}</div>
        <div class="conn-actions">
          <button class="btn btn-primary btn-sm" data-act="enter" type="button">${escapeHtml(I18N.t('common.enter'))}</button>
          <button class="btn btn-secondary btn-sm" data-act="disconnect" type="button">${escapeHtml(I18N.t('common.disconnect'))}</button>
        </div>
      </div>`
    )
    .join('');
  grid.querySelectorAll('.conn-card').forEach((card) => {
    const connId = card.dataset.connid;
    const conn = conns.find((c) => c.connectionId === connId);
    card.querySelector('[data-act="enter"]').addEventListener('click', () => {
      DatabaseModule.enterConnection({ connectionId: connId, type: 'MySQL', info: conn });
    });
    card.querySelector('[data-act="disconnect"]').addEventListener('click', async () => {
      const yes = await confirmDialog(I18N.t('db.confirmDisconnect'));
      if (!yes) return;
      try {
        await API.dbDisconnect(connId);
        DatabaseModule._saveConns(conns.filter((c) => c.connectionId !== connId));
        renderDbConnList();
        toast(I18N.t('db.disconnectOk'), 'success');
      } catch (e) { /* 已提示 */ }
    });
  });
}

/* ================= 模块 4：我的文件 ================= */
const FilesModule = {
  files: [],

  init() {
    document.getElementById('files-refresh').addEventListener('click', () => this.load());
  },

  async load() {
    try {
      const result = await API.fileList();
      this.files = result.fileList || [];
      this.render();
    } catch (e) { /* 已提示 */ }
  },

  render() {
    const wrap = document.getElementById('file-list-wrap');
    if (!wrap) return;
    if (!this.files.length) {
      wrap.innerHTML = `<div class="empty-state">
        <div class="icon-box">${icon('folder')}</div>
        <div class="empty-title">${escapeHtml(I18N.t('console.filesEmptyTitle'))}</div>
        <div class="empty-desc">${escapeHtml(I18N.t('console.filesEmptyDesc'))}</div>
        <button class="btn btn-primary btn-sm" id="files-go-upload" type="button">${escapeHtml(I18N.t('console.goUpload'))}</button>
      </div>`;
      wrap.querySelector('#files-go-upload').addEventListener('click', () => switchModule('excel'));
      return;
    }

    const excels = this.files.filter((f) => isExcelFile(f.fileName));
    const sqlites = this.files.filter((f) => isSqliteFile(f.fileName));
    const others = this.files.filter((f) => !isExcelFile(f.fileName) && !isSqliteFile(f.fileName));

    const group = (title, iconName, list) => {
      if (!list.length) return '';
      return `<div class="file-group-title">${icon(iconName)}<span>${escapeHtml(title)}</span></div>` +
        list
          .map(
            (f) => `<div class="card file-item" data-fileid="${escapeHtml(f.fileId)}">
              <div class="file-icon ${isExcelFile(f.fileName) ? 'excel' : 'sqlite'}">${icon(isExcelFile(f.fileName) ? 'table' : 'database')}</div>
              <div class="file-info">
                <div class="file-name">${escapeHtml(f.fileName)}</div>
                <div class="file-meta">${formatBytes(f.fileSize)} · ${formatTime(f.uploadTime)}</div>
              </div>
              <div class="file-actions">
                <button class="icon-btn" data-act="download" type="button" title="${escapeHtml(I18N.t('common.download'))}">${icon('download')}</button>
                <button class="icon-btn" data-act="preview" type="button" title="${escapeHtml(I18N.t('common.preview'))}">${icon('eye')}</button>
                <button class="icon-btn danger" data-act="delete" type="button" title="${escapeHtml(I18N.t('common.delete'))}">${icon('trash')}</button>
              </div>
            </div>`
          )
          .join('');
    };

    wrap.innerHTML =
      group(I18N.t('console.groupExcel'), 'table', excels) +
      group(I18N.t('console.groupSqlite'), 'database', sqlites) +
      group(I18N.t('common.empty'), 'folder', others);

    wrap.querySelectorAll('.file-item').forEach((item) => {
      const fileId = item.dataset.fileid;
      const file = this.files.find((f) => f.fileId === fileId);
      item.querySelector('[data-act="download"]').addEventListener('click', async () => {
        try {
          const { blob, filename } = await API.fileDownload(fileId);
          triggerBlobDownload(blob, filename || file.fileName);
        } catch (e) { /* 已提示 */ }
      });
      item.querySelector('[data-act="preview"]').addEventListener('click', async () => {
        if (isExcelFile(file.fileName)) {
          ExcelModule.openFile(fileId);
        } else if (isSqliteFile(file.fileName)) {
          // SQLite：建立连接后进入数据库助手
          switchModule('database');
          showLoading(I18N.t('common.loading'));
          try {
            const result = await API.dbConnect({ type: 'SQLite', SQLite: { fileId, readonly: false } });
            await DatabaseModule.enterConnection({
              connectionId: result.connectionId,
              type: 'SQLite',
              info: { fileId, fileName: file.fileName }
            });
          } catch (e) { /* 已提示 */ }
          hideLoading();
        }
      });
      item.querySelector('[data-act="delete"]').addEventListener('click', async () => {
        const yes = await confirmDialog(I18N.t('console.confirmDeleteFile'));
        if (!yes) return;
        try {
          await API.fileDelete(fileId);
          toast(I18N.t('console.fileDeleted'), 'success');
          await this.load();
        } catch (e) { /* 已提示 */ }
      });
    });
  }
};

/* ================= 模块 5：历史会话 ================= */
const HistoryModule = {
  sessions: [],

  init() {
    document.getElementById('history-refresh').addEventListener('click', () => this.load());
  },

  async load() {
    try {
      const result = await API.aiChatSessionLists();
      this.sessions = result.chatSessionLists || [];
      this.render();
    } catch (e) { /* 已提示 */ }
  },

  render() {
    const list = document.getElementById('session-list');
    if (!list) return;
    if (!this.sessions.length) {
      list.innerHTML = `<div class="empty-state">
        <div class="icon-box">${icon('history')}</div>
        <div class="empty-title">${escapeHtml(I18N.t('console.historyEmptyTitle'))}</div>
        <div class="empty-desc">${escapeHtml(I18N.t('console.historyEmptyDesc'))}</div>
      </div>`;
      return;
    }
    const typeIcon = { plain: 'chat', excel: 'table', database: 'database' };
    const typeKey = { plain: 'console.typePlain', excel: 'console.typeExcel', database: 'console.typeDb' };
    list.innerHTML = this.sessions
      .map(
        (s) => `<div class="card card-hover session-item" data-sessionid="${escapeHtml(s.chatSessionId)}">
          <div class="session-type-badge ${escapeHtml(s.sessionType || 'plain')}">${icon(typeIcon[s.sessionType] || 'chat')}</div>
          <div class="session-info">
            <div class="session-title">${escapeHtml(s.title || s.firstUserMessageContent || '-')}</div>
            <div class="session-meta">
              <span class="tag">${escapeHtml(I18N.t(typeKey[s.sessionType] || 'console.typePlain'))}</span>
              <span class="tag">${escapeHtml(s.modelName || '')}</span>
              <span>${escapeHtml(I18N.t('console.msgCount').replace('{n}', s.messageCount || 0))}</span>
              <span>${formatTime(s.updatedAt)}</span>
            </div>
          </div>
          <button class="icon-btn danger" data-act="delete" type="button" style="width:34px;height:34px;border-radius:9px;border:1px solid var(--border);background:#fff;color:var(--text-sub);display:flex;align-items:center;justify-content:center;">${icon('trash')}</button>
        </div>`
      )
      .join('');

    list.querySelectorAll('.session-item').forEach((item) => {
      const sessionId = item.dataset.sessionid;
      const session = this.sessions.find((s) => s.chatSessionId === sessionId);
      item.addEventListener('click', (e) => {
        if (e.target.closest('[data-act="delete"]')) return;
        this.openSession(session);
      });
      item.querySelector('[data-act="delete"]').addEventListener('click', async (e) => {
        e.stopPropagation();
        const yes = await confirmDialog(I18N.t('console.confirmDeleteSession'));
        if (!yes) return;
        try {
          await API.aiDelete(sessionId);
          toast(I18N.t('console.sessionDeleted'), 'success');
          await this.load();
        } catch (err) { /* 已提示 */ }
      });
    });
  },

  /** 打开历史会话：拉取历史消息并按类型恢复场景 */
  async openSession(session) {
    showLoading(I18N.t('common.loading'));
    try {
      const result = await API.aiHistory(session.chatSessionId);
      const messageList = result.messageList || [];
      const sessionType = result.sessionType || session.sessionType || 'plain';

      // 提取最后一条最终结果帧（assistant 且为结果 JSON）
      let finalResult = null;
      for (let i = messageList.length - 1; i >= 0; i--) {
        const m = messageList[i];
        if (m.role !== 'assistant') continue;
        const trimmed = (m.content || '').trim();
        if (trimmed.startsWith('{')) {
          try {
            const obj = JSON.parse(trimmed);
            if (obj && 'summary' in obj && 'displayType' in obj) { finalResult = obj; break; }
          } catch (e) { /* 继续找 */ }
        }
      }

      hideLoading();
      if (sessionType === 'excel') {
        await ExcelModule.restore(session.chatSessionId, result.fileId, messageList, finalResult);
      } else if (sessionType === 'database') {
        await DatabaseModule.restore(session.chatSessionId, result.dbConnectionInfo || session.dbConnectionInfo, messageList, finalResult);
      } else {
        ChatModule.restore(session.chatSessionId, messageList);
      }
    } catch (e) {
      hideLoading();
    }
  }
};

/* ================= 模块 6：个人中心 ================= */
const ProfileModule = {
  init() {
    document.getElementById('profile-logout').addEventListener('click', async () => {
      const yes = await confirmDialog(I18N.t('console.confirmLogout'));
      if (!yes) return;
      try {
        await API.logout();
      } catch (e) { /* 忽略登出错误 */ }
      clearSessionId();
      toast(I18N.t('console.logoutOk'), 'success');
      setTimeout(() => { window.location.href = 'login.html'; }, 400);
    });
  }
};
