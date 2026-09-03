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
   *   onPlain(text)             标签外普通文本（增量）
   */
  constructor(render) {
    this.render = render;
    this.buffer = '';
    this.state = 'scan'; // scan | title | tasks | analysis
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

  _drain() {
    for (;;) {
      if (this.state === 'scan') {
        // 寻找下一个开始标签；保留可能是标签前缀的尾部
        const idx = this._findTagStart();
        if (idx === -1) {
          const safe = this._safePlainLength();
          if (safe > 0) {
            this._emitPlain(this.buffer.slice(0, safe));
            this.buffer = this.buffer.slice(safe);
          }
          return; // 等待更多数据
        }
        if (idx > 0) {
          this._emitPlain(this.buffer.slice(0, idx));
          this.buffer = this.buffer.slice(idx);
        }
        // 判定是哪个标签
        if (this._tryConsume('<TITLE_START>', 'title')) continue;
        if (this._tryConsume('<TASKS_START>', 'tasks')) continue;
        if (this._tryConsume('<ANALYSIS_START>', 'analysis')) continue;
        // 不完整的标签前缀，等待更多数据
        return;
      }

      // 处于某个标签段内：寻找对应结束标签
      const endTag =
        this.state === 'title' ? '<TITLE_END>' : this.state === 'tasks' ? '<TASKS_END>' : '<ANALYSIS_END>';
      const endIdx = this.buffer.indexOf(endTag);
      if (endIdx === -1) {
        // 结束标签可能被切断：保留尾部可能是结束标签前缀的部分
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
    }
  }

  _findTagStart() {
    const tags = ['<TITLE_START>', '<TASKS_START>', '<ANALYSIS_START>'];
    let best = -1;
    tags.forEach((t) => {
      const i = this.buffer.indexOf(t);
      if (i !== -1 && (best === -1 || i < best)) best = i;
    });
    return best;
  }

  /** 普通文本安全输出长度：尾部若以 '<' 开头的潜在标签前缀则保留 */
  _safePlainLength() {
    const lt = this.buffer.lastIndexOf('<');
    if (lt === -1) return this.buffer.length;
    const tail = this.buffer.slice(lt);
    const candidates = ['<TITLE_START>', '<TASKS_START>', '<ANALYSIS_START>', '<TITLE_END>', '<TASKS_END>', '<ANALYSIS_END>'];
    for (const c of candidates) {
      if (c.startsWith(tail)) return lt; // 尾部是某标签前缀，保留
    }
    return this.buffer.length;
  }

  /** 段内安全输出长度：尾部若是结束标签前缀则保留 */
  _safeEndLength(endTag) {
    for (let k = 1; k < endTag.length; k++) {
      if (this.buffer.endsWith(endTag.slice(0, k))) return this.buffer.length - k;
    }
    return this.buffer.length;
  }

  _tryConsume(openTag, nextState) {
    if (this.buffer.startsWith(openTag)) {
      this.buffer = this.buffer.slice(openTag.length);
      this.state = nextState;
      if (nextState === 'tasks') this.taskCount = 0;
      return true;
    }
    return false;
  }

  _emit(text) {
    if (!text) return;
    if (this.state === 'title') {
      this.render.onTitle && this.render.onTitle(text);
    } else if (this.state === 'analysis') {
      this.render.onAnalysis && this.render.onAnalysis(text);
    } else if (this.state === 'tasks') {
      this._emitTasks(text);
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
    // 已完整出现下一条分隔符的任务才算完成；最后一段留在缓冲
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

/** 创建一条消息骨架，返回 { root, bodyEl } */
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
  const getPlainEl = () => {
    if (!plainEl) {
      plainEl = document.createElement('div');
      plainEl.className = 'ai-stream-text';
      bubble.appendChild(plainEl);
      parts.push(plainEl);
    }
    return plainEl;
  };

  let titleEl = null;
  let tasksEl = null;
  let analysisEl = null;
  let titleText = '';
  let analysisText = '';

  const parser = structured
    ? new StreamTagParser({
        onTitle(t) {
          titleEl = ensureContainer('ai-title');
          titleText += t;
          titleEl.textContent = titleText;
        },
        onTask(text) {
          tasksEl = ensureContainer('ai-tasks');
          const item = document.createElement('div');
          item.className = 'ai-task';
          item.innerHTML = `${icon('check')}<span></span>`;
          item.querySelector('span').textContent = text;
          tasksEl.appendChild(item);
        },
        onAnalysis(t) {
          analysisEl = ensureContainer('ai-analysis');
          analysisText += t;
          analysisEl.textContent = analysisText;
        },
        onPlain(t) {
          const el = getPlainEl();
          el.textContent += t;
        }
      })
    : null;

  return {
    /** 追加流式片段 */
    append(chunk) {
      if (structured) parser.feed(chunk);
      else getPlainEl().textContent += chunk;
    },
    /** 流结束 */
    finish() {
      if (structured) parser.flush();
      // plain 模式结束后做轻量 Markdown 渲染
      if (!structured && plainEl) {
        const raw = plainEl.textContent;
        plainEl.innerHTML = mdRender(raw);
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
