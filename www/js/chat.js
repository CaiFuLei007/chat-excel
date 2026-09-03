/**
 * chat.js — SSE 流式消息的增量标签解析与分阶段渲染
 *
 * 后端透传三个标签（其余已被后端过滤）：
 *   <TITLE_START> 分析标题 <TITLE_END>
 *   <TASKS_START> 1. 任务1  2. 任务2 ... <TASKS_END>
 *   <ANALYSIS_START> 分析思路 <ANALYSIS_END>
 * 标签可能被 SSE 分块切断，解析器基于缓冲增量扫描，边接收边渲染。
 * 依赖：utils.js
 */

class StreamTagParser {
  /**
   * @param {object} render 渲染回调：
   *   onTitle(text)             标题（增量）
   *   onTask(taskText, index)   新任务条目
   *   onAnalysis(text)          分析思路（增量）
   *   onPlain(text)             标签外 / 未知标签内 普通文本（增量）
   *
   * 标签统一为 <XXX_START> ... <XXX_END>（XXX 为大写下划线命名）。
   * TITLE/TASKS/ANALYSIS 进入各自区块；其余标签只剥除标记、内容按普通文本展示。
   */
  constructor(render) {
    this.render = render;
    this.buffer = '';
    this.state = 'scan'; // scan | title | tasks | analysis | other
    this.otherTag = '';  // state=other 时记录的标签名
    this.taskCount = 0;
    this.lastTaskRaw = '';
    this._taskDone = [];
  }

  /** 送入一段流式片段 */
  feed(chunk) {
    this.buffer += chunk;
    this._drain();
  }

  /** 流结束时冲刷剩余缓冲 */
  flush() {
    if (this.state !== 'scan' && this.buffer) {
      this._emit(this.buffer);
      this.buffer = '';
    }
    this.buffer = '';
  }

  /** 当前状态对应的结束标签（如 <TITLE_END>） */
  _endTagOf() {
    if (this.state === 'title') return '<TITLE_END>';
    if (this.state === 'tasks') return '<TASKS_END>';
    if (this.state === 'analysis') return '<ANALYSIS_END>';
    if (this.state === 'other') return '<' + this.otherTag + '_END>';
    return '';
  }

  _drain() {
    for (;;) {
      if (this.state === 'scan') {
        // 寻找下一个 <XXX_START>；保留可能是标签前缀的尾部
        const idx = this._findTagStart();
        if (idx === -1) {
          const safe = this._safePlainLength();
          if (safe > 0) {
            this._emitPlain(this.buffer.slice(0, safe));
            this.buffer = this.buffer.slice(safe);
          }
          return;
        }
        if (idx > 0) {
          this._emitPlain(this.buffer.slice(0, idx));
          this.buffer = this.buffer.slice(idx);
        }
        const m = /^<([A-Z][A-Z0-9_]*)_START>/.exec(this.buffer);
        if (m) {
          const name = m[1].toLowerCase();
          this.buffer = this.buffer.slice(m[0].length);
          if (name === 'title') this.state = 'title';
          else if (name === 'tasks') { this.state = 'tasks'; this.taskCount = 0; }
          else if (name === 'analysis') this.state = 'analysis';
          else { this.state = 'other'; this.otherTag = m[1].toUpperCase(); }
          continue;
        }
        // 不完整的标签前缀，等待更多数据
        return;
      }

      // 处于某个标签段内：寻找对应结束标签
      const endTag = this._endTagOf();
      const endIdx = this.buffer.indexOf(endTag);
      if (endIdx === -1) {
        const safe = this._safeEndLength(endTag);
        if (safe > 0) {
          this._emit(this.buffer.slice(0, safe));
          this.buffer = this.buffer.slice(safe);
        }
        return;
      }
      this._emit(this.buffer.slice(0, endIdx));
      this.buffer = this.buffer.slice(endIdx + endTag.length);
      this.state = 'scan';
      this.otherTag = '';
    }
  }

  /** 找到下一个 <XXX_START> 位置（任意大写标签） */
  _findTagStart() {
    const m = /<[A-Z][A-Z0-9_]*_START>/.exec(this.buffer);
    return m ? m.index : -1;
  }

  /** 普通文本安全输出长度：尾部若是未闭合的潜在标签前缀则保留 */
  _safePlainLength() {
    const lt = this.buffer.lastIndexOf('<');
    if (lt === -1) return this.buffer.length;
    const tail = this.buffer.slice(lt);
    // 形如 <TITLE_... 尚未收到 '>'，可能是标签前缀（含普通文本中出现的小于号则无碍地等到后续判断）
    if (/^<[A-Z][A-Z0-9_]*/.test(tail) && tail.indexOf('>') === -1) return lt;
    return this.buffer.length;
  }

  /** 段内安全输出长度：尾部若是结束标签前缀则保留 */
  _safeEndLength(endTag) {
    for (let k = 1; k < endTag.length; k++) {
      if (this.buffer.endsWith(endTag.slice(0, k))) return this.buffer.length - k;
    }
    return this.buffer.length;
  }

  _emit(text) {
    if (!text) return;
    if (this.state === 'title') {
      this.render.onTitle && this.render.onTitle(text);
    } else if (this.state === 'analysis') {
      this.render.onAnalysis && this.render.onAnalysis(text);
    } else if (this.state === 'tasks') {
      this._emitTasks(text);
    } else if (this.state === 'other') {
      // 未知标签：剥除标记，内容按普通文本展示
      this.render.onPlain && this.render.onPlain(text);
    }
  }

  _emitPlain(text) {
    if (text) this.render.onPlain && this.render.onPlain(text);
  }

  /** 任务列表增量解析：按 "数字." 切分，逐条触发 */
  _emitTasks(text) {
    this.lastTaskRaw += text;
    // 以 "数字." 作为任务分隔（兼容全角点）
    const re = /(?:^|\s)(\d+)[.、．]\s*/g;
    let m;
    let lastIdx = -1;
    let lastNum = null;
    const segments = [];
    while ((m = re.exec(this.lastTaskRaw)) !== null) {
      if (lastIdx !== -1) {
        segments.push({ num: lastNum, text: this.lastTaskRaw.slice(lastIdx, m.index) });
      }
      lastIdx = re.lastIndex;
      lastNum = m[1];
    }
    segments.forEach((seg) => {
      const key = `${seg.num}:${seg.text.trim()}`;
      if (!this._taskDone.includes(key)) {
        this._taskDone.push(key);
        this.taskCount += 1;
        this.render.onTask && this.render.onTask(seg.text.trim(), this.taskCount);
      }
    });
  }
}

/* ============================ 聊天消息 DOM 构建 ============================ */

/** 单行整理：把任意连续空白/换行折叠为单个空格并去除首尾空白 */
function tidySingle(s) {
  return (s || '').replace(/\s+/g, ' ').trim();
}

/** 多行正文整理：去除空行，去掉每行首尾空白，保留单个换行分段 */
function tidyBody(s) {
  return (s || '')
    .replace(/\r\n?/g, '\n')
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l !== '')
    .join('\n');
}

/** 增量片段整理：压缩片段内的连续换行，便于流式期间避免出现空行闪烁 */
function tidyChunk(s) {
  return (s || '')
    .replace(/\r\n?/g, '\n')
    .replace(/\n{2,}/g, '\n')
    .split('\n')
    .map((l) => l.trimRight())
    .join('\n');
}

/**
 * 创建一条消息骨架，返回 { root, bodyEl }
 */
function buildMessageEl(role) {
  const root = document.createElement('div');
  root.className = `msg ${role === 'user' ? 'user-msg' : 'ai-msg'}`;
  const avatar = document.createElement('div');
  avatar.className = `msg-avatar ${role === 'user' ? 'user' : 'ai'}`;
  avatar.innerHTML = icon(role === 'user' ? 'user' : 'sparkle');
  const body = document.createElement('div');
  body.className = 'msg-body';
  const bubble = document.createElement('div');
  bubble.className = 'msg-bubble';
  body.appendChild(bubble);
  root.appendChild(avatar);
  root.appendChild(body);
  return { root, bodyEl: bubble };
}

/**
 * 创建一个 AI 流式消息渲染器（支持标签分阶段 + 纯文本两种模式）
 * @param {HTMLElement} bubble  消息气泡元素
 * @param {boolean} structured  是否启用标签解析（excel/database 为 true，plain 为 false）
 */
function createAiStreamRenderer(bubble, structured) {
  const parts = []; // 按出现顺序保存渲染节点

  const ensureContainer = (cls) => {
    let el = bubble.querySelector(`.${cls}`);
    if (!el) {
      el = document.createElement('div');
      el.className = cls;
      bubble.appendChild(el);
      parts.push(el);
    }
    return el;
  };

  // 纯文本容器（标签外文本 / plain 模式共用）
  let plainEl = null;
  // plain 模式累计的原始 Markdown（含换行与标记）。
  // 渲染一律基于本变量：若从已渲染 HTML 的 textContent 回读，
  // 换行(<br>/块级标签)与 ** 等标记都会丢失，导致格式被逐步破坏。
  let plainBuf = '';
  const getPlainEl = () => {
    if (!plainEl) {
      plainEl = document.createElement('div');
      plainEl.className = 'ai-stream-text';
      bubble.appendChild(plainEl);
      parts.push(plainEl);
    }
    return plainEl;
  };

  // ---- 结构化区块（TITLE / TASKS / ANALYSIS）----
  let titleCard = null;
  let titleBody = null;
  let titleBuf = '';
  let tasksCard = null;
  let tasksBody = null;
  let analysisCard = null;
  let analysisBody = null;

  // 标题：渐变卡（一次创建结构，增量更新文字）
  const ensureTitle = () => {
    if (!titleCard) {
      titleCard = ensureContainer('ai-title-card');
      titleCard.innerHTML = '<span class="ai-title-ic">📊</span><span class="ai-title-body"></span>';
      titleBody = titleCard.querySelector('.ai-title-body');
    }
    return titleBody;
  };
  // 任务：浅底卡 + 状态方框列表
  const ensureTasks = () => {
    if (!tasksCard) {
      tasksCard = ensureContainer('ai-tasks-card');
      tasksCard.innerHTML = '<div class="ai-card-label ai-label-tasks"></div><div class="ai-task-list"></div>';
      const label = tasksCard.querySelector('.ai-label-tasks');
      label.textContent = I18N.t('ai.tasks');
      tasksBody = tasksCard.querySelector('.ai-task-list');
    }
    return tasksBody;
  };
  // 分析：浅底卡
  const ensureAnalysis = () => {
    if (!analysisCard) {
      analysisCard = ensureContainer('ai-analysis-card');
      analysisCard.innerHTML = '<div class="ai-card-label ai-label-analysis"></div><div class="ai-analysis-body"></div>';
      const label = analysisCard.querySelector('.ai-label-analysis');
      label.textContent = I18N.t('ai.analysis');
      analysisBody = analysisCard.querySelector('.ai-analysis-body');
    }
    return analysisBody;
  };

  /** 递归清掉残留在气泡中的 <XXX_START>/<XXX_END> 标记文本（防御性兜底） */
  const stripResidualTags = (node) => {
    const walker = document.createTreeWalker(node, NodeFilter.SHOW_TEXT);
    const textNodes = [];
    while (walker.nextNode()) textNodes.push(walker.currentNode);
    textNodes.forEach((tn) => {
      if (/<[A-Z][A-Z0-9_]*_(START|END)>/.test(tn.nodeValue)) {
        tn.nodeValue = tn.nodeValue.replace(/<[A-Z][A-Z0-9_]*_(START|END)>/g, '');
      }
    });
  };

  const parser = structured
    ? new StreamTagParser({
        onTitle(t) {
          const body = ensureTitle();
          titleBuf += t;
          body.textContent = titleBuf;
        },
        onTask(text) {
          const list = ensureTasks();
          const item = document.createElement('div');
          item.className = 'ai-task';
          item.innerHTML = '<span class="ai-task-box"></span><span class="ai-task-text"></span>';
          item.querySelector('.ai-task-text').textContent = tidySingle(text);
          list.appendChild(item);
        },
        onAnalysis(t) {
          const body = ensureAnalysis();
          let c = tidyChunk(t);
          const cur = body.textContent;
          if (cur === '') {
            // 首个片段：剥离前导空白，避免标题下方流式期间出现空行
            c = c.replace(/^\s+/, '');
          } else if (cur.endsWith('\n')) {
            c = c.replace(/^\n+/, '');
          }
          body.textContent = cur + c;
        },
        onPlain(t) {
          const el = getPlainEl();
          let c = tidyChunk(t);
          const cur = el.textContent;
          if (cur === '') {
            // 首个片段：剥离前导空白
            c = c.replace(/^\s+/, '');
          } else if (cur.endsWith('\n')) {
            c = c.replace(/^\n+/, '');
          }
          el.textContent = cur + c;
        }
      })
    : null;

  return {
    /** 追加流式片段 */
    append(chunk) {
      if (structured) {
        parser.feed(chunk);
      } else {
        // plain 模式：累计原始 Markdown 后整体渲染，
        // 不从 DOM 回读，避免换行与 markdown 标记在增量渲染中丢失
        plainBuf += chunk;
        getPlainEl().innerHTML = mdRender(plainBuf);
      }
    },
    /** 流结束 */
    finish() {
      if (structured) {
        parser.flush();
        // 本回合回复完成：所有任务标记为已完成（方框变 √）
        if (tasksCard) tasksCard.classList.add('done');
        // 最终整理：去除空行 / 多余换行 / 行首尾空白，标签残渣清理
        if (titleBody) titleBody.textContent = tidySingle(titleBody.textContent);
        if (analysisBody) analysisBody.textContent = tidyBody(analysisBody.textContent);
        if (plainEl) plainEl.textContent = tidyBody(plainEl.textContent);
        stripResidualTags(bubble);
      }
      // plain 模式：基于累计的原始 Markdown 做最终渲染（与流式中一致，保留换行与格式）
      if (!structured && plainEl) {
        plainEl.innerHTML = mdRender(plainBuf);
      }
    },
    /** 是否渲染过任何内容 */
    hasContent() {
      return parts.length > 0 && bubble.textContent.trim() !== '';
    }
  };
}

/**
 * 渲染历史消息中的 assistant 内容（非流式，一次性解析）
 * 复用流式渲染器：一次性 feed 全部内容后 finish
 */
function renderAssistantHistory(bubble, content, structured) {
  const renderer = createAiStreamRenderer(bubble, structured);
  renderer.append(content);
  renderer.finish();
  return renderer;
}
