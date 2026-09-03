/**
 * chart.js — ECharts 加载（本地优先 + CDN 回退）与结果图表渲染
 * 支持 displayType：Table / BarChart / ColumnChart / LineChart / AreaChart /
 *                   PieChart / DonutChart / ScatterChart / NumberDisplay
 * 依赖：config.js / utils.js
 */

const ChartKit = {
  _loading: null,
  _instances: [],

  /** 确保 echarts 可用：本地 js/echarts.min.js 优先，失败回退 CDN */
  ensure() {
    if (window.echarts) return Promise.resolve(window.echarts);
    if (this._loading) return this._loading;

    const tryLoad = (src) =>
      new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = src;
        s.onload = () => (window.echarts ? resolve(window.echarts) : reject(new Error('echarts load failed')));
        s.onerror = () => reject(new Error('echarts load failed'));
        document.head.appendChild(s);
      });

    this._loading = tryLoad('js/echarts.min.js')
      .catch(() => tryLoad('https://cdn.jsdelivr.net/npm/echarts@5.5.1/dist/echarts.min.js'))
      .catch(() => tryLoad('https://unpkg.com/echarts@5.5.1/dist/echarts.min.js'))
      .catch(() => {
        this._loading = null;
        throw new Error('ECharts unavailable');
      });
    return this._loading;
  },

  /** 窗口尺寸变化时重排所有实例 */
  _bindResize() {
    if (this._resized) return;
    this._resized = true;
    window.addEventListener(
      'resize',
      debounce(() => this._instances.forEach((c) => c && !c.isDisposed() && c.resize()), 150)
    );
  },

  _palette() {
    return ['#6366f1', '#10b981', '#f59e0b', '#818cf8', '#ef4444', '#06b6d4', '#ec4899', '#84cc16', '#a855f7'];
  },

  _baseOption() {
    return {
      color: this._palette(),
      textStyle: { fontFamily: "'DM Sans','PingFang SC','Microsoft YaHei',sans-serif" },
      grid: { left: 12, right: 20, top: 36, bottom: 12, containLabel: true },
      tooltip: { trigger: 'item', confine: true },
      animationDuration: 500
    };
  },

  /**
   * 在 container 中渲染最终结果的图表部分
   * @param {HTMLElement} container
   * @param {object} result  最终结果帧 { summary, displayType, data }
   */
  async renderResult(container, result) {
    container.innerHTML = '';
    const type = result.displayType;
    const data = result.data || {};

    /* ---- 非 ECharts 类型 ---- */
    if (type === 'NumberDisplay') {
      const value = this._firstCell(data);
      const el = document.createElement('div');
      el.className = 'result-number';
      el.textContent = value ?? '-';
      container.appendChild(el);
      return;
    }

    if (type === 'Table') {
      container.appendChild(this._renderTables(data));
      return;
    }

    /* ---- ECharts 类型 ---- */
    const div = document.createElement('div');
    div.className = 'result-chart';
    container.appendChild(div);

    let echarts;
    try {
      echarts = await this.ensure();
    } catch (e) {
      // ECharts 不可用时降级为表格
      container.innerHTML = '';
      container.appendChild(this._renderTables(data));
      return;
    }

    const chart = echarts.init(div);
    this._instances.push(chart);
    this._bindResize();
    chart.setOption(this._buildOption(type, data));
  },

  /** 取第一个单元格值（NumberDisplay 用） */
  _firstCell(data) {
    if (data.rows && data.rows.length && data.rows[0].length) return data.rows[0][0];
    if (data.tables && data.tables.length) {
      const t = data.tables[0];
      if (t.rows && t.rows.length && t.rows[0].length) return t.rows[0][0];
    }
    return null;
  },

  /** 主数据：优先 columns+rows，其次第一张 tables */
  _primary(data) {
    if (data.columns && data.rows) return { columns: data.columns, rows: data.rows };
    if (data.tables && data.tables.length) {
      const t = data.tables[0];
      return { columns: t.columns || [], rows: t.rows || [] };
    }
    return { columns: [], rows: [] };
  },

  _buildOption(type, data) {
    const { columns, rows } = this._primary(data);
    const opt = this._baseOption();
    const cats = rows.map((r) => String(r[0] ?? ''));

    const numericCols = columns.map((_, ci) => {
      if (ci === 0) return false;
      return rows.some((r) => r[ci] !== '' && !isNaN(Number(r[ci])));
    });

    const seriesFromCols = () =>
      columns
        .map((name, ci) => ({ name, ci }))
        .filter((x) => numericCols[x.ci])
        .map((x) => ({
          name: x.name,
          type: 'bar',
          data: rows.map((r) => Number(r[x.ci] ?? 0))
        }));

    switch (type) {
      case 'BarChart': {
        // 横向条形
        opt.tooltip = { trigger: 'axis', axisPointer: { type: 'shadow' }, confine: true };
        opt.xAxis = { type: 'value' };
        opt.yAxis = { type: 'category', data: cats, inverse: true };
        opt.series = columns
          .map((name, ci) => ({ name, ci }))
          .filter((x) => numericCols[x.ci])
          .map((x) => ({
            name: x.name,
            type: 'bar',
            barMaxWidth: 22,
            itemStyle: { borderRadius: [0, 6, 6, 0] },
            data: rows.map((r) => Number(r[x.ci] ?? 0))
          }));
        opt.legend = opt.series.length > 1 ? { top: 0 } : undefined;
        break;
      }
      case 'ColumnChart': {
        opt.tooltip = { trigger: 'axis', axisPointer: { type: 'shadow' }, confine: true };
        opt.xAxis = { type: 'category', data: cats };
        opt.yAxis = { type: 'value' };
        opt.series = seriesFromCols().map((s) => Object.assign(s, { barMaxWidth: 34, itemStyle: { borderRadius: [6, 6, 0, 0] } }));
        opt.legend = opt.series.length > 1 ? { top: 0 } : undefined;
        break;
      }
      case 'LineChart':
      case 'AreaChart': {
        opt.tooltip = { trigger: 'axis', confine: true };
        opt.xAxis = { type: 'category', data: cats, boundaryGap: false };
        opt.yAxis = { type: 'value' };
        opt.series = columns
          .map((name, ci) => ({ name, ci }))
          .filter((x) => numericCols[x.ci])
          .map((x) => ({
            name: x.name,
            type: 'line',
            smooth: true,
            symbolSize: 6,
            areaStyle: type === 'AreaChart' ? { opacity: 0.16 } : undefined,
            data: rows.map((r) => Number(r[x.ci] ?? 0))
          }));
        opt.legend = opt.series.length > 1 ? { top: 0 } : undefined;
        break;
      }
      case 'PieChart':
      case 'DonutChart': {
        const valueCi = numericCols.findIndex(Boolean);
        opt.tooltip = { trigger: 'item', formatter: '{b}: {c} ({d}%)', confine: true };
        opt.series = [
          {
            type: 'pie',
            radius: type === 'DonutChart' ? ['46%', '70%'] : '68%',
            center: ['50%', '54%'],
            itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
            label: { formatter: '{b}: {d}%' },
            data: rows.map((r) => ({ name: String(r[0] ?? ''), value: Number(r[valueCi > 0 ? valueCi : 1] ?? 0) }))
          }
        ];
        break;
      }
      case 'ScatterChart': {
        opt.tooltip = { trigger: 'item', confine: true };
        opt.xAxis = { type: 'value', name: columns[1] || '' };
        opt.yAxis = { type: 'value', name: columns[2] || '' };
        opt.series = [
          {
            type: 'scatter',
            symbolSize: 12,
            itemStyle: { opacity: 0.75 },
            data: rows.map((r) => [Number(r[1] ?? 0), Number(r[2] ?? r[1] ?? 0)])
          }
        ];
        break;
      }
      default: {
        // 未知类型回退为竖向柱状图
        opt.xAxis = { type: 'category', data: cats };
        opt.yAxis = { type: 'value' };
        opt.series = seriesFromCols();
      }
    }
    return opt;
  },

  /** 渲染 data.tables（或 columns/rows）为 HTML 表格 */
  _renderTables(data) {
    const wrap = document.createElement('div');
    wrap.className = 'result-table-wrap';
    const tables =
      data.tables && data.tables.length
        ? data.tables
        : [{ columns: data.columns || [], rows: data.rows || [] }];

    tables.forEach((t) => {
      if (!t.columns.length && !t.rows.length) return;
      const table = document.createElement('table');
      table.className = 'excel-table';
      const thead = document.createElement('thead');
      const trh = document.createElement('tr');
      (t.columns || []).forEach((c) => {
        const th = document.createElement('th');
        th.textContent = c;
        trh.appendChild(th);
      });
      thead.appendChild(trh);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      (t.rows || []).forEach((row) => {
        const tr = document.createElement('tr');
        (row || []).forEach((cell) => {
          const td = document.createElement('td');
          td.textContent = cell ?? '';
          tr.appendChild(td);
        });
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      wrap.appendChild(table);
    });

    if (!wrap.children.length) {
      wrap.innerHTML = `<div class="empty-state"><div class="empty-title">${escapeHtml(I18N.t('common.empty'))}</div></div>`;
    }
    return wrap;
  },

  /** 展示页演示用小型柱状图 */
  async renderDemoBar(container, categories, values, seriesName) {
    let echarts;
    try {
      echarts = await this.ensure();
    } catch (e) {
      return;
    }
    const chart = echarts.init(container);
    this._instances.push(chart);
    this._bindResize();
    chart.setOption({
      color: this._palette(),
      grid: { left: 8, right: 8, top: 24, bottom: 4, containLabel: true },
      tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' }, confine: true },
      xAxis: { type: 'category', data: categories, axisLabel: { fontSize: 11 } },
      yAxis: { type: 'value', axisLabel: { fontSize: 11 } },
      series: [
        {
          name: seriesName || '',
          type: 'bar',
          barMaxWidth: 30,
          itemStyle: { borderRadius: [6, 6, 0, 0] },
          data: values
        }
      ]
    });
  }
};
