/**
 * file.js — Excel 预览渲染（sheet 标签页 / 列头 / 行号 / 分页）与文件上传
 * 依赖：config.js / utils.js / api.js
 */

/**
 * Excel 预览渲染器：挂载到一个容器，负责调用 /api/file/preview 并渲染
 */
class ExcelPreview {
  /**
   * @param {HTMLElement} container  预览区根容器
   * @param {object} hooks { onSheetsLoaded(sheetNames), onFileLoaded(fileId, fileName) }
   */
  constructor(container, hooks) {
    this.container = container;
    this.hooks = hooks || {};
    this.fileId = null;
    this.fileName = '';
    this.sheets = [];
    this.activeSheet = null;
    this.page = 1;
  }

  clear() {
    this.fileId = null;
    this.sheets = [];
    this.activeSheet = null;
    this.container.innerHTML = '';
  }

  /** 加载文件预览 */
  async load(fileId, pageNumber) {
    this.page = pageNumber || 1;
    const result = await API.filePreview(fileId, this.page, CONFIG.PAGE_SIZE);
    this.fileId = result.fileId || fileId;
    this.fileName = result.fileName || '';
    const sheets = (result.excelData && result.excelData.sheets) || [];
    const firstLoad = this.sheets.length === 0;
    this.sheets = sheets;

    // 保持当前选中 sheet；默认第一个
    const prevName = this.activeSheet;
    if (!sheets.find((s) => s.name === prevName)) {
      this.activeSheet = sheets.length ? sheets[0].name : null;
    }

    this._render();
    if (firstLoad && sheets.length) {
      this.hooks.onSheetsLoaded && this.hooks.onSheetsLoaded(sheets.map((s) => s.name));
      this.hooks.onFileLoaded && this.hooks.onFileLoaded(this.fileId, this.fileName);
    }
  }

  /** 切换 sheet（重新加载该 sheet 第 1 页） */
  async switchSheet(name) {
    if (name === this.activeSheet) return;
    this.activeSheet = name;
    this.page = 1;
    await this.load(this.fileId, 1);
  }

  async setPage(page) {
    if (page < 1) return;
    await this.load(this.fileId, page);
  }

  _activeSheetData() {
    return this.sheets.find((s) => s.name === this.activeSheet) || null;
  }

  _render() {
    const c = this.container;
    c.innerHTML = '';

    if (!this.sheets.length) {
      c.innerHTML = `<div class="empty-state">${icon('table')}<div class="empty-title">${escapeHtml(I18N.t('file.noPreview'))}</div></div>`;
      return;
    }

    // sheet 标签页
    const tabs = document.createElement('div');
    tabs.className = 'sheet-tabs';
    this.sheets.forEach((s) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `sheet-tab ${s.name === this.activeSheet ? 'active' : ''}`;
      btn.textContent = s.name;
      btn.addEventListener('click', () => this.switchSheet(s.name));
      tabs.appendChild(btn);
    });
    c.appendChild(tabs);

    const sheet = this._activeSheetData();
    if (!sheet) return;

    // 表格
    const scroll = document.createElement('div');
    scroll.className = 'table-scroll';
    const table = document.createElement('table');
    table.className = 'excel-table';

    const thead = document.createElement('thead');
    const trh = document.createElement('tr');
    const corner = document.createElement('th');
    corner.className = 'row-num';
    corner.textContent = '#';
    trh.appendChild(corner);
    (sheet.columns || []).forEach((col) => {
      const th = document.createElement('th');
      th.textContent = col;
      trh.appendChild(th);
    });
    thead.appendChild(trh);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    const baseRow = (sheet.currentPage - 1) * sheet.pageSize;
    (sheet.data || []).forEach((row, ri) => {
      const tr = document.createElement('tr');
      const num = document.createElement('td');
      num.className = 'row-num';
      num.textContent = baseRow + ri + 1;
      tr.appendChild(num);
      (row || []).forEach((cell) => {
        const td = document.createElement('td');
        td.textContent = cell ?? '';
        tr.appendChild(td);
      });
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    scroll.appendChild(table);
    c.appendChild(scroll);

    // 分页器
    const pager = document.createElement('div');
    pager.className = 'table-pager';
    const prev = document.createElement('button');
    prev.type = 'button';
    prev.innerHTML = icon('chevronLeft');
    prev.disabled = sheet.currentPage <= 1;
    prev.addEventListener('click', () => this.setPage(sheet.currentPage - 1));
    const info = document.createElement('span');
    info.textContent = I18N.t('file.pageInfo')
      .replace('{cur}', sheet.currentPage)
      .replace('{total}', sheet.totalPages)
      .replace('{rows}', sheet.totalRows);
    const next = document.createElement('button');
    next.type = 'button';
    next.innerHTML = icon('chevronRight');
    next.disabled = sheet.currentPage >= sheet.totalPages;
    next.addEventListener('click', () => this.setPage(sheet.currentPage + 1));
    pager.appendChild(prev);
    pager.appendChild(info);
    pager.appendChild(next);
    c.appendChild(pager);
  }
}

/**
 * 上传 Excel 文件（两步：登记元数据 → 二进制上传）
 * @param {File} file
 * @returns {Promise<string>} fileId
 */
async function uploadExcelFile(file) {
  if (!isExcelFile(file.name)) {
    toast(I18N.t('file.onlyXlsx'), 'error');
    throw new Error('unsupported file type');
  }
  const infoResult = await API.fileUploadInfo({
    filename: file.name,
    fileSize: file.size,
    fileExt: fileExt(file.name).replace(/^\./, '')
  });
  const fileId = infoResult.fileId;
  await API.fileUploadBinary(fileId, file);
  return fileId;
}

/**
 * 上传 SQLite 文件（单步二进制）
 * @param {File} file
 * @returns {Promise<string>} fileId
 */
async function uploadSqliteFile(file) {
  if (!isSqliteFile(file.name)) {
    toast(I18N.t('file.onlySqlite'), 'error');
    throw new Error('unsupported file type');
  }
  const result = await API.sqliteUpload(file.name, file);
  return result.fileId;
}

I18N.register(
  {
    'file.noPreview': '暂无预览数据',
    'file.pageInfo': '第 {cur} / {total} 页 · 共 {rows} 行',
    'file.onlyXlsx': '仅支持 .xlsx 格式的 Excel 文件',
    'file.onlySqlite': '仅支持 .db / .sqlite / .sqlite3 格式的 SQLite 文件'
  },
  {
    'file.noPreview': 'No preview data',
    'file.pageInfo': 'Page {cur} / {total} · {rows} rows',
    'file.onlyXlsx': 'Only .xlsx Excel files are supported',
    'file.onlySqlite': 'Only .db / .sqlite / .sqlite3 SQLite files are supported'
  }
);
