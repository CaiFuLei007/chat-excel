/**
 * api.js — 统一请求封装
 * - apiRequest(): 自动附加 requestId、解析通用响应信封、errorCode != 0 抛业务错误
 * - 二进制上传 / 下载与 SSE 接口不走信封，单独封装
 * 依赖：config.js / utils.js
 */

/** 业务错误：携带后端 errorCode 与 errorMsg */
class ApiError extends Error {
  constructor(errorCode, errorMsg, requestId) {
    super(errorMsg || `errorCode=${errorCode}`);
    this.name = 'ApiError';
    this.errorCode = errorCode;
    this.errorMsg = errorMsg;
    this.requestId = requestId;
  }
}

/** 会话失效统一处理 : 清登录态并回登录页(111 = SESSION_NOT_FOUND, 会话不存在或已过期) */
function handleAuthFailure(errorCode) {
  if (errorCode === 111) {
    clearSessionId();
    window.location.href = 'login.html';
    return true;
  }
  return false;
}

/**
 * 通用 JSON 请求（套用响应信封）
 * @param {string} path   相对路径，如 '/api/user/passwd/login'
 * @param {object} body   请求体对象（无需包含 requestId，自动附加）
 * @param {object} opts   { sessionId?: string|false, query?: object, method?: string, silent?: bool }
 *   sessionId 传字符串则覆盖默认；传 false 表示该接口不需要登录态
 * @returns {Promise<object>} 响应中的 result（可能为 undefined）
 */
async function apiRequest(path, body, opts) {
  const o = opts || {};
  const requestId = genRequestId();
  const payload = Object.assign({}, body || {}, { requestId });

  // 登录态接口默认附加 sessionId（调用方可显式传 false 关闭）
  if (o.sessionId !== false) {
    const sid = typeof o.sessionId === 'string' ? o.sessionId : getSessionId();
    if (sid) payload.sessionId = sid;
  }

  let url = CONFIG.API_BASE + path;
  if (o.query) {
    const qs = Object.entries(o.query)
      .filter(([, v]) => v !== undefined && v !== null)
      .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
      .join('&');
    if (qs) url += (url.includes('?') ? '&' : '?') + qs;
  }

  // 调试日志: 打印前端发送给后端的完整请求(method + URL + 请求体)
  console.log(`[api→] ${o.method || 'POST'} ${url}`, payload);

  let resp;
  try {
    const method = o.method || 'POST';
    resp = await fetch(url, {
      method,
      headers: { 'Content-Type': 'application/json' },
      // GET/HEAD 请求不允许携带 body, 参数已放入 query
      body: method === 'GET' || method === 'HEAD' ? undefined : JSON.stringify(payload)
    });
  } catch (e) {
    console.error('[api!] 请求发送失败(网络错误)', path, e);
    throw new ApiError(-1, I18N.t('common.networkError'), requestId);
  }

  let data;
  try {
    data = await resp.json();
  } catch (e) {
    console.error('[api!] 响应解析失败, http状态:', resp.status, path, e);
    throw new ApiError(-1, I18N.t('common.networkError'), requestId);
  }
  // 调试日志: 打印后端返回的完整响应信封(http 状态 + 响应体)
  console.log(`[api←] ${path} http=${resp.status}`, data);
  if (data && data.result !== undefined) {
    console.log('[api←result]', path, data.result);
  }

  if (data.errorCode !== 0) {
    if (!handleAuthFailure(data.errorCode) && !o.silent) {
      toast(data.errorMsg || I18N.t('common.serviceUnavailable'), 'error');
    }
    throw new ApiError(data.errorCode, data.errorMsg, data.requestId || requestId);
  }
  return data.result;
}

/** GET 请求（信封接口，参数全部在 query） */
async function apiGet(path, query, opts) {
  return apiRequest(path, {}, Object.assign({}, opts, { method: 'GET', query, sessionId: false }));
}

/**
 * 二进制上传（octet-stream，响应仍套用信封）
 * @param {string} path   含 query 的完整相对路径
 * @param {Blob|ArrayBuffer} blob
 */
async function apiUploadBinary(path, blob) {
  let resp;
  try {
    resp = await fetch(CONFIG.API_BASE + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: blob
    });
  } catch (e) {
    throw new ApiError(-1, I18N.t('common.networkError'));
  }
  let data;
  try {
    data = await resp.json();
  } catch (e) {
    throw new ApiError(-1, I18N.t('common.networkError'));
  }
  if (data.errorCode !== 0) {
    toast(data.errorMsg || I18N.t('common.serviceUnavailable'), 'error');
    throw new ApiError(data.errorCode, data.errorMsg);
  }
  return data.result;
}

/** 二进制下载：返回 { blob, filename } */
async function apiDownload(path) {
  let resp;
  try {
    resp = await fetch(CONFIG.API_BASE + path, { method: 'GET' });
  } catch (e) {
    throw new ApiError(-1, I18N.t('common.networkError'));
  }
  if (!resp.ok) throw new ApiError(resp.status, I18N.t('common.networkError'));
  const blob = await resp.blob();
  let filename = 'download';
  const cd = resp.headers.get('Content-Disposition') || '';
  const m = cd.match(/filename\*?=(?:UTF-8'')?"?([^";]+)"?/i);
  if (m) {
    try { filename = decodeURIComponent(m[1]); } catch (e) { filename = m[1]; }
  }
  return { blob, filename };
}

/** 触发浏览器下载 */
function triggerBlobDownload(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 3000);
}

/* ============================ 接口封装（按编号） ============================ */

const API = {
  /* ---- 健康检测 ---- */
  health: () => fetch(CONFIG.API_BASE + '/health', { method: 'GET' }).then((r) => (r.ok ? r.json() : null)).catch(() => null),

  /* ---- 用户 ---- */
  validNickname: (nickname) => apiRequest('/api/user/valid/nickname', { nickname }, { sessionId: false, silent: true }),
  validEmail: (email) => apiRequest('/api/user/valid/email', { email }, { sessionId: false, silent: true }),
  register: (form) => apiRequest('/api/user/register', form, { sessionId: false }),
  passwdLogin: (username, password) => apiRequest('/api/user/passwd/login', { username, password }, { sessionId: false }),
  sendCode: (email) => apiRequest('/api/user/code', { email }, { sessionId: false }),
  vcodeLogin: (email, verifyCode, codeId) => apiRequest('/api/user/vcode/login', { email, verifyCode, codeId }, { sessionId: false }),
  sessionLogin: (sessionId) => apiRequest('/api/user/session/login', {}, { sessionId, silent: true }),
  logout: () => apiRequest('/api/user/logout', {}),
  userInfo: () => apiRequest('/api/user/info', {}, { query: { requestId: genRequestId(), sessionId: getSessionId() }, sessionId: false }),

  /* ---- 文件 ---- */
  fileUploadInfo: (fileInfo) => apiRequest('/api/file/upload/info', { fileInfo }),
  fileUploadBinary: (fileId, blob) =>
    apiUploadBinary(`/api/file/upload?requestId=${genRequestId()}&sessionId=${encodeURIComponent(getSessionId())}&fileId=${encodeURIComponent(fileId)}`, blob),
  fileDownload: (fileId) =>
    apiDownload(`/api/file/download?requestId=${genRequestId()}&sessionId=${encodeURIComponent(getSessionId())}&fileId=${encodeURIComponent(fileId)}`),
  fileDelete: (fileId) =>
    apiRequest(`/api/file/${encodeURIComponent(fileId)}`, {}, { method: 'DELETE', query: { requestId: genRequestId(), sessionId: getSessionId() }, sessionId: false }),
  filePreview: (fileId, pageNumber, pageSize, forceOriginal) => apiRequest('/api/file/preview', { fileId, pageNumber, pageSize, forceOriginal }),
  fileList: () => apiRequest('/api/file/list', {}),
  fileChatMap: (fileId, chatSessionId) => apiRequest('/api/file/chat/map', { fileId, chatSessionId }),
  sqliteUpload: (filename, blob) =>
    apiUploadBinary(`/api/file/sqlite/upload?requestId=${genRequestId()}&sessionId=${encodeURIComponent(getSessionId())}&filename=${encodeURIComponent(filename)}`, blob),

  /* ---- 数据库 ---- */
  dbConnect: (database) => apiRequest('/api/db/connect', { database }),
  dbDisconnect: (connectionId) => apiRequest('/api/db/disconnect', { connectionId }),
  dbTables: (dbConnectId) => apiGet('/api/db/tables', { requestId: genRequestId(), sessionId: getSessionId(), dbConnectId }),
  dbTableData: (dbConnectId, tableName, forceOriginal) => apiRequest('/api/db/table/data', { dbConnectId, tableName, forceOriginal }),
  dbConnectionStatus: (dbConnectId) => apiRequest('/api/db/connection/status', { dbConnectId }),

  /* ---- AI ---- */
  aiModels: () => apiRequest('/api/ai/models', {}),
  aiSessionCreate: (payload) => apiRequest('/api/ai/session/create', payload),
  aiChatSessionLists: () => apiRequest('/api/ai/chatSessionLists', {}),
  aiHistory: (chatSessionId) => apiRequest('/api/ai/history', { chatSessionId }),
  aiDelete: (chatSessionId) => apiRequest('/api/ai/delete', { chatSessionId })
};

/**
 * SSE 流式发送消息
 * @param {object} body { chatSessionId, chatType, message, fileId?, dbConnectId?, dbType?, tableName? }
 * @param {object} handlers {
 *   onText(content),        // 普通流式文本片段
 *   onFinalResult(obj),     // 最终结果帧（含 summary + displayType）
 *   onError(errorMsg),      // 帧内业务错误
 *   onDone(),               // 流结束
 *   signal                  // AbortSignal（可选）
 * }
 */
async function apiSendStreamMessage(body, handlers) {
  const payload = Object.assign(
    { requestId: genRequestId(), sessionId: getSessionId() },
    body
  );

  let resp;
  try {
    resp = await fetch(CONFIG.API_BASE + '/api/ai/sendStreamMessage', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Accept: 'text/event-stream' },
      body: JSON.stringify(payload),
      signal: handlers.signal
    });
  } catch (e) {
    if (e.name === 'AbortError') return;
    handlers.onError(I18N.t('common.networkError'));
    handlers.onDone && handlers.onDone();
    return;
  }

  if (!resp.ok || !resp.body) {
    handlers.onError(I18N.t('common.serviceUnavailable'));
    handlers.onDone && handlers.onDone();
    return;
  }

  const reader = resp.body.getReader();
  const decoder = new TextDecoder('utf-8');
  let buffer = '';

  // 本轮流式统计与完整内容累计
  let accText = '';
  let accFrames = 0;
  let accFinal = null;

  const processLine = (line) => {
    line = line.trim();
    if (!line.startsWith('data:')) return;
    const raw = line.slice(5).trim();
    if (!raw) return;
    if (raw === '[DONE]') {
      console.log('[sse-raw]', '[DONE]'); // 流结束标记
      return;
    }
    let frame;
    try {
      frame = JSON.parse(raw);
    } catch (e) {
      console.log('[sse-raw]', raw.slice(0, 500)); // 非 JSON 内容原样输出（限长）
      return;
    }
    console.log('[sse-frame]', frame); // 网关返回的帧（含 errorCode/errorMsg/done/content 等）
    if (frame.errorCode && frame.errorCode !== 0) {
      console.log('[sse-frame] 业务错误 errorCode =', frame.errorCode, frame.errorMsg);
      handlers.onError(frame.errorMsg || I18N.t('common.serviceUnavailable'));
      return;
    }
    const content = frame.content;
    if (typeof content !== 'string' || content === '') {
      console.log('[sse-frame] content 为空 / done =', frame.done);
      if (frame.done) handlers.onDone && handlers.onDone();
      return;
    }
    console.log('[sse-content]', content.length > 600 ? content.slice(0, 600) + '…(截断)' : content);
    // 尝试判定最终结果帧：JSON 且含 summary + displayType
    const trimmed = content.trim();
    if (trimmed.startsWith('{')) {
      try {
        const obj = JSON.parse(trimmed);
        if (obj && typeof obj === 'object' && 'summary' in obj && 'displayType' in obj) {
          accFinal = obj; // 记录最终结果帧（summary/displayType/data）
          handlers.onFinalResult(obj);
          return;
        }
      } catch (e) { /* 非 JSON，按普通文本处理 */ }
    }
    accText += content; // 累计普通文本片段
    accFrames += 1;
    handlers.onText(content);
    if (frame.done) handlers.onDone && handlers.onDone();
  };

  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      // SSE 逐行处理
      let idx;
      while ((idx = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, idx);
        buffer = buffer.slice(idx + 1);
        processLine(line);
      }
    }
    if (buffer.trim()) processLine(buffer);
  } catch (e) {
    if (e.name !== 'AbortError') handlers.onError(I18N.t('common.networkError'));
  } finally {
    // 汇总本轮流式响应：帧数 / 文本总长 / 完整文本 / 最终结果帧
    console.log(`[sse-summary] 普通文本帧=${accFrames} 完整文本总长=${accText.length}${accFinal ? ` 最终结果帧=${accFinal.summary ? accFinal.summary.length : 0}` : ''}`);
    if (accText) {
      console.log('[sse-full-text] 完整响应全文：');
      console.log(accText);
    }
    if (accFinal) {
      console.log('[sse-full-final] 最终结果帧（完整 JSON）：');
      console.log(JSON.stringify(accFinal));
    }
    handlers.onDone && handlers.onDone();
  }
}
