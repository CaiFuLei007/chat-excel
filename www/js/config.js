/**
 * ChatExcel 全局配置
 *
 * 部署约定（重要）：
 * - 网关默认托管前端静态文件（--www_root 指向 www/ 目录，空字符串关闭托管），
 *   前端与 API 同源访问（http://网关地址:8080/），无需额外代理。
 * - 生产环境也可由 Nginx 托管静态文件，并将 /api/* 与 /health 反向代理到网关（默认 0.0.0.0:8080）。
 * - API_BASE 仅允许在此处集中配置；默认空字符串表示同源相对路径，
 *   严禁在其他文件中硬编码后端 IP / 端口。
 */
const CONFIG = {
  /** 后端 API 基础地址：空字符串 = 同源相对路径（推荐） */
  API_BASE: '',

  /** localStorage 键名约定 */
  KEYS: {
    SESSION_ID: 'chat_excel_session_id',
    LANG: 'chat_excel_lang',
    LOGIN_TAB: 'chat_excel_login_tab',
    MYSQL_CONNS: 'chat_excel_mysql_conns',
    MODEL: 'chat_excel_model'
  },

  /** 文件类型后缀 */
  EXCEL_EXTS: ['.xlsx'],
  SQLITE_EXTS: ['.db', '.sqlite', '.sqlite3'],

  /** 验证码倒计时秒数 */
  CODE_COUNTDOWN: 60,

  /** 分页默认大小 */
  PAGE_SIZE: 50
};
