/**
 * utils.js — 通用工具：i18n 引擎 / 图标库 / 请求辅助 / UI 反馈组件
 * 依赖：config.js
 */

/* ============================ i18n 引擎 ============================ */

const I18N = {
  dict: { zh: {}, en: {} },

  /** 各页面脚本调用以注册本页面文案 */
  register(zh, en) {
    Object.assign(this.dict.zh, zh);
    Object.assign(this.dict.en, en);
  },

  lang() {
    return localStorage.getItem(CONFIG.KEYS.LANG) === 'en' ? 'en' : 'zh';
  },

  setLang(lang) {
    localStorage.setItem(CONFIG.KEYS.LANG, lang === 'en' ? 'en' : 'zh');
  },

  /** 取文案；找不到时回退中文，再回退 key 本身 */
  t(key) {
    const d = this.dict[this.lang()];
    if (d && d[key] !== undefined) return d[key];
    if (this.dict.zh[key] !== undefined) return this.dict.zh[key];
    return key;
  },

  /** 填充 root 内所有 [data-i18n] / [data-i18n-ph] / [data-i18n-title] 节点 */
  apply(root) {
    const scope = root || document;
    scope.querySelectorAll('[data-i18n]').forEach((el) => {
      el.textContent = this.t(el.getAttribute('data-i18n'));
    });
    // data-i18n-html：文案本身含 HTML（如渐变强调词），文案由本前端控制，可安全注入
    scope.querySelectorAll('[data-i18n-html]').forEach((el) => {
      el.innerHTML = this.t(el.getAttribute('data-i18n-html'));
    });
    scope.querySelectorAll('[data-i18n-ph]').forEach((el) => {
      el.setAttribute('placeholder', this.t(el.getAttribute('data-i18n-ph')));
    });
    scope.querySelectorAll('[data-i18n-title]').forEach((el) => {
      el.setAttribute('title', this.t(el.getAttribute('data-i18n-title')));
    });
  },

  /** 绑定语言切换按钮（.lang-switch），并在切换后回调 onPageLangChange */
  bindSwitch(onChange) {
    const render = () => {
      document.querySelectorAll('.lang-switch').forEach((btn) => {
        btn.textContent = this.lang() === 'zh' ? 'EN' : '中文';
      });
    };
    document.querySelectorAll('.lang-switch').forEach((btn) => {
      btn.addEventListener('click', () => {
        this.setLang(this.lang() === 'zh' ? 'en' : 'zh');
        this.apply();
        render();
        if (typeof onChange === 'function') onChange();
      });
    });
    render();
  }
};

/* ============================ 图标库（内联 SVG，线性风格） ============================ */

const ICON_PATHS = {
  logo: '<path d="M4 4h16v12H4z"/><path d="M4 9h16M9 4v12"/><path d="m13 13 2 2 3-3"/>',
  chat: '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>',
  table: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
  database: '<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/>',
  folder: '<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>',
  history: '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>',
  user: '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
  upload: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>',
  download: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>',
  trash: '<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
  eye: '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>',
  eyeOff: '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>',
  send: '<line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/>',
  plus: '<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>',
  close: '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
  check: '<polyline points="20 6 9 17 4 12"/>',
  chevronDown: '<polyline points="6 9 12 15 18 9"/>',
  chevronLeft: '<polyline points="15 18 9 12 15 6"/>',
  chevronRight: '<polyline points="9 18 15 12 9 6"/>',
  arrowRight: '<line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/>',
  menu: '<line x1="3" y1="6" x2="21" y2="6"/><line x1="3" y1="12" x2="21" y2="12"/><line x1="3" y1="18" x2="21" y2="18"/>',
  globe: '<circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>',
  mail: '<path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/><polyline points="22,6 12,13 2,6"/>',
  zap: '<polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/>',
  cpu: '<rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3"/>',
  chartBar: '<line x1="12" y1="20" x2="12" y2="10"/><line x1="18" y1="20" x2="18" y2="4"/><line x1="6" y1="20" x2="6" y2="16"/>',
  chartPie: '<path d="M21.21 15.89A10 10 0 1 1 8 2.83"/><path d="M22 12A10 10 0 0 0 12 2v10z"/>',
  fileText: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/>',
  link: '<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>',
  unlink: '<path d="m18.84 12.25 1.72-1.71a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="m5.17 11.75-1.72 1.71a5 5 0 0 0 7.07 7.07l1.71-1.71"/><line x1="8" y1="2" x2="8" y2="5"/><line x1="2" y1="8" x2="5" y2="8"/><line x1="16" y1="19" x2="16" y2="22"/><line x1="19" y1="16" x2="22" y2="16"/>',
  alert: '<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/>',
  info: '<circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>',
  lock: '<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
  logout: '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/>',
  refresh: '<polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>',
  sparkle: '<path d="M12 3l1.9 5.7L19.6 10l-5.7 1.9L12 17.6l-1.9-5.7L4.4 10l5.7-1.9z"/><path d="M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"/>',
  layers: '<polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/>',
  server: '<rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/><line x1="6" y1="6" x2="6.01" y2="6"/><line x1="6" y1="18" x2="6.01" y2="18"/>',
  shield: '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>',
  help: '<circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>'
};

/** 返回完整 <svg> 字符串 */
function icon(name, cls) {
  const body = ICON_PATHS[name] || ICON_PATHS.info;
  return `<svg class="icon ${cls || ''}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
}

/* ============================ 会话与请求辅助 ============================ */

function genRequestId() {
  return `req_${Date.now()}_${Math.random().toString(36).slice(2, 10)}`;
}

function getSessionId() {
  return localStorage.getItem(CONFIG.KEYS.SESSION_ID) || '';
}

function setSessionId(id) {
  localStorage.setItem(CONFIG.KEYS.SESSION_ID, id);
}

function clearSessionId() {
  localStorage.removeItem(CONFIG.KEYS.SESSION_ID);
}

/* ============================ 格式化 ============================ */

function escapeHtml(str) {
  return String(str ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return '-';
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB'];
  let v = bytes / 1024;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(1)} ${units[i]}`;
}

/** Unix 秒 → 本地时间字符串 */
function formatTime(ts) {
  if (!ts) return '-';
  const d = new Date(ts * 1000);
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function fileExt(name) {
  const i = String(name || '').lastIndexOf('.');
  return i >= 0 ? name.slice(i).toLowerCase() : '';
}

function isExcelFile(name) {
  return CONFIG.EXCEL_EXTS.includes(fileExt(name));
}

function isSqliteFile(name) {
  return CONFIG.SQLITE_EXTS.includes(fileExt(name));
}

/** 判定列类型是否为布尔类型(兼容 BOOLEAN / tinyint(1)) */
function isBoolColumnType(type) {
  const t = String(type || '').toLowerCase();
  return t === 'boolean' || t === 'bool' || t === 'tinyint(1)';
}

/**
 * 按数据类型格式化单元格展示值
 * - NULL 值不展示(返回空字符串)
 * - 布尔列展示为 true / false
 * @param {string} cell 单元格原始值
 * @param {string} type 列类型
 * @returns {string} 展示值
 */
function formatCellByType(cell, type) {
  // NULL 值(后端统一以字符串 "NULL" 透传)不展示
  if (cell === null || cell === undefined || cell === 'NULL') return '';
  if (isBoolColumnType(type)) {
    const v = String(cell).trim();
    if (v === '1') return 'true';
    if (v === '0') return 'false';
  }
  return String(cell);
}

function debounce(fn, wait) {
  let timer = null;
  return function (...args) {
    clearTimeout(timer);
    timer = setTimeout(() => fn.apply(this, args), wait);
  };
}

/* ============================ 轻量 Markdown 渲染（plain 聊天） ============================ */

function mdRender(text) {
  let s = escapeHtml(text);
  // 代码块
  s = s.replace(/```([\s\S]*?)```/g, (_, code) => `<pre class="md-pre"><code>${code.replace(/^\n|\n$/g, '')}</code></pre>`);
  // 行内代码
  s = s.replace(/`([^`\n]+)`/g, '<code class="md-code">$1</code>');
  // 加粗 / 斜体
  s = s.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  s = s.replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>');
  // 标题（仅行首）
  s = s.replace(/^### (.+)$/gm, '<div class="md-h">$1</div>');
  s = s.replace(/^## (.+)$/gm, '<div class="md-h">$1</div>');
  s = s.replace(/^# (.+)$/gm, '<div class="md-h">$1</div>');
  // 无序列表
  s = s.replace(/^[-*] (.+)$/gm, '<div class="md-li">• $1</div>');
  // 有序列表
  s = s.replace(/^\d+\. (.+)$/gm, (m, p1) => `<div class="md-li">${m.slice(0, m.indexOf('.'))}. ${p1}</div>`);
  // 段落换行
  s = s.replace(/\n/g, '<br>');
  return s;
}

/* ============================ Toast ============================ */

function ensureToastRoot() {
  let root = document.getElementById('toast-root');
  if (!root) {
    root = document.createElement('div');
    root.id = 'toast-root';
    document.body.appendChild(root);
  }
  return root;
}

/** toast(message, 'success' | 'error' | 'info') */
function toast(message, type) {
  const root = ensureToastRoot();
  const el = document.createElement('div');
  el.className = `toast toast-${type || 'info'}`;
  const iconMap = { success: 'check', error: 'alert', info: 'info' };
  el.innerHTML = `${icon(iconMap[type] || 'info')}<span>${escapeHtml(message)}</span>`;
  root.appendChild(el);
  requestAnimationFrame(() => el.classList.add('show'));
  setTimeout(() => {
    el.classList.remove('show');
    setTimeout(() => el.remove(), 300);
  }, 3200);
}

/* ============================ 加载遮罩 ============================ */

function showLoading(text) {
  let overlay = document.getElementById('loading-overlay');
  if (!overlay) {
    overlay = document.createElement('div');
    overlay.id = 'loading-overlay';
    overlay.innerHTML = '<div class="loading-box"><div class="spinner"></div><div class="loading-text"></div></div>';
    document.body.appendChild(overlay);
  }
  overlay.querySelector('.loading-text').textContent = text || '';
  overlay.classList.add('show');
}

function hideLoading() {
  const overlay = document.getElementById('loading-overlay');
  if (overlay) overlay.classList.remove('show');
}

/* ============================ 确认对话框 ============================ */

/** confirmDialog(message) → Promise<boolean> */
function confirmDialog(message, title) {
  return new Promise((resolve) => {
    const mask = document.createElement('div');
    mask.className = 'modal-mask show';
    mask.innerHTML = `
      <div class="modal-card" role="dialog" aria-modal="true">
        <div class="modal-title">${escapeHtml(title || I18N.t('common.confirm'))}</div>
        <div class="modal-body">${escapeHtml(message)}</div>
        <div class="modal-actions">
          <button class="btn btn-secondary" data-act="cancel">${escapeHtml(I18N.t('common.cancel'))}</button>
          <button class="btn btn-danger" data-act="ok">${escapeHtml(I18N.t('common.ok'))}</button>
        </div>
      </div>`;
    document.body.appendChild(mask);
    mask.addEventListener('click', (e) => {
      const act = e.target && e.target.dataset ? e.target.dataset.act : null;
      if (act === 'ok') { mask.remove(); resolve(true); }
      else if (act === 'cancel' || e.target === mask) { mask.remove(); resolve(false); }
    });
  });
}

/* ============================ 通用文案（所有页面共享） ============================ */

I18N.register(
  {
    'common.confirm': '提示',
    'common.cancel': '取消',
    'common.ok': '确定',
    'common.loading': '加载中…',
    'common.networkError': '网络错误，请稍后重试',
    'common.serviceUnavailable': '后端服务不可用，请稍后重试',
    'common.paramError': '请求参数错误',
    'common.delete': '删除',
    'common.download': '下载',
    'common.preview': '预览',
    'common.enter': '进入',
    'common.disconnect': '断开连接',
    'common.empty': '暂无数据'
  },
  {
    'common.confirm': 'Notice',
    'common.cancel': 'Cancel',
    'common.ok': 'OK',
    'common.loading': 'Loading…',
    'common.networkError': 'Network error, please try again later',
    'common.serviceUnavailable': 'Backend service unavailable, please try again later',
    'common.paramError': 'Invalid request parameters',
    'common.delete': 'Delete',
    'common.download': 'Download',
    'common.preview': 'Preview',
    'common.enter': 'Enter',
    'common.disconnect': 'Disconnect',
    'common.empty': 'No data'
  }
);
