/* -*- coding: utf-8 -*-
 * 页 10 日志：服务端运行日志分页查询（0x0119 转发 /api/log/page）。
 *
 * [对齐参考 pages_other.LogPage 逐项移植]
 *   · 3 列表：时间/类别/事件（时间类别居中、事件左对齐）；
 *   · 类别下拉**立即生效**：全部/conn 连接/ctrl 控制/alarm 告警/auto 自动/
 *     auth 鉴权/sys 系统（sys=Python 服务端专有档；C 端选中恒空但不报错）；
 *   · 请求 {offset,limit,beforeTs,loTs,hiTs,category,kw}；锚点 <=0 时
 *     发包前定格本地「现在」；翻页沿用同一锚点 ⇒ 整段分页钉在同一批数据上；
 *   · **应答归属判定**：offset/limit 必须回显同值才铺表（防串页——翻页
 *     连点/筛选抢先把迟到旧应答丢弃）；
 *   · 老服务端「未知命令」否定应答 ⇒ 提示「服务端不支持运行日志查询
 *     （0x0119）」+ 空表不崩；此后入页不再自动发（显式操作仍照发）；
 *   · 时间窗 DESC 取页、页内正序应答原样铺；页大小 10/20/50；
 *   · 「客户端运行日志」勾选框：展开客户端本地日志折叠副视图（网页状态
 *     消息收流，环形 500 行 + 自动滚动 + 清空，参考 LogPage._client_card）；
 *   · 导出（权限闸在 web——0x0119 无 export 键形态）：按筛选全量，走后端
 *     /api/log/export 批取流（批 40、整轮钉同一锚点）；导出按钮弹菜单
 *     选 **CSV / Excel(.xlsx)** 两种形态（文案与历史页一致）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var COLUMNS = ["时间", "类别", "事件"];
  /* 类别下拉项：(入库字面, 显示名)。下标 0 = 全部（字面空串）。
   * 前五档与 C 端 kLogCategoryKeys/kLogCategoryNames 同序同字面；
   * sys 为 Python 服务端专有档（C 端不产生 sys 行 ⇒ 选中恒空，不报错）。 */
  var CATEGORY_ITEMS = [
    { key: "", name: "全部类别" },
    { key: "conn", name: "连接" },
    { key: "ctrl", name: "控制" },
    { key: "alarm", name: "告警" },
    { key: "auto", name: "自动" },
    { key: "auth", name: "鉴权" },
    { key: "sys", name: "系统" },
  ];
  var CATEGORY_TEXT = {};
  CATEGORY_ITEMS.slice(1).forEach(function (it) { CATEGORY_TEXT[it.key] = it.name; });

  var PAGE_SIZES = [10, 20, 50];
  var PAGE_DEFAULT = 20;
  var UNKNOWN_CMD_ERR = "未知命令";
  var CLIENT_LOG_MAX_LINES = 500;  /* 客户端运行日志环形缓冲（参考同值 500） */
  /* ---- 列宽（rem@14 基准；参考端同宽视口逐像素实测原图：时间≈294px /
   *   类别≈236px，事件列吃剩余宽（≈516px）、超宽「…」截断——同 DataTable
   *   min_widths 思路。上一轮 215/200 是裁剪图目测值，本轮按原图订正）。
   *
   * [2026-11-20 窄屏塌列根修 · 与页面 9 同款「列宽下限」思路]
   * 原实现只给「时间/类别」两列显式宽，「事件」列靠 table-layout:fixed
   * 吃剩余 ⇒ **容器窄于前两列之和时剩余为 0**，「事件」列被压成 0px
   * （整列不可见，连表头「事件」都只剩一个字；实测 390px 视口下
   *  th clientWidth=0 / scrollWidth=48）。
   * ⇒ ① 三列**都**给显式宽（fixed 布局按首行定列宽，没给宽的列会被挤成 0）；
   *   ② 每列有**内容下限**（窄屏退化用）：时间要装下 "yyyy-MM-dd HH:MM:SS"
   *      （实测 197px@20px 字 + 内边距 ⇒ 220px）、类别装下两字、事件给一段
   *      可读正文（正文长度不定 ⇒ 固定下限，同参考端 min_widths 的 260）；
   *   ③ 表格总宽 = max(容器可用宽, 各列下限之和) ⇒ 窄屏时**表格宽于容器**，
   *      由 .dt-table-wrap 横向滚动（窄屏表格的既定形态，CSS ≤700px 已开
   *      overflow:auto）⇒ **任何一列的宽度都不会是 0**。
   * 宽屏仍是原图口径（时间 294 / 类别 236 / 事件吃剩余宽）。 */
  var COL_W_REM = { "时间": 294 / 14, "类别": 236 / 14 };
  /* 内容下限（rem@14；窄屏退化用） */
  var COL_MIN_REM = { "时间": 220 / 14, "类别": 72 / 14, "事件": 260 / 14 };

  /* ================================================================== */
  var activeUnmount = null;

  HSF.router.register(10, {
    title: "日志",
    topics: [],                      /* 查询型页面（0x0119），不订遥测主题 */
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function mountPage(host, ctx) {
    var store = HSF.store;

    /* ---- 状态（与参考同名字段对应）---- */
    var pageSize = PAGE_DEFAULT;
    var savedSize = parseInt(HSF.prefs.get("logPageSize", ""), 10);
    if (PAGE_SIZES.indexOf(savedSize) >= 0) pageSize = savedSize;
    var offset = 0;                  /* 已铺表的页起始偏移 */
    var totalRows = 0;
    var anchorTs = 0;                /* 分页锚点（<=0 = 发包前取"现在"） */
    var loMs = 0, hiMs = 0;          /* 时间窗（筛选按钮才更新） */
    var category = "";               /* 当前生效类别字面（空=全部） */
    var kw = "";                     /* 当前生效关键字（空=不筛） */
    var inflight = false;
    var pendingOffset = 0, pendingLimit = 0;
    var serverSupported = true;
    var exporting = false;
    var rows = [];                   /* 当前页（[时间,类别,事件] 文本，应答序） */

    /* ---- 工具条（无折线列/双视图/滚动锁定——没有图表也没有自动刷新；
     * 有「客户端运行日志」开关：客户端本地日志折叠副视图，参考同款）----
     * [折叠把手挂标题行右侧 2026-11-19] 与历史页同款（把手不再独占一行）。 */
    var foldHost = HSF.el("span.dt-fold-host");
    var head = HSF.el("div.data-page-head", null, [
      HSF.el("span.page-head-title", { text: "日志" }),
      HSF.el("span.page-head-sub.caption",
             { text: "服务端运行日志（连接/控制/告警/自动/鉴权）" }),
      foldHost,
    ]);
    var tb = HSF.dataToolbar({
      pageKey: "log",
      foldHost: foldHost,
      columns: COLUMNS,
      extraCombo: {
        label: "类别",
        items: CATEGORY_ITEMS.map(function (it) { return it.name; }),
        values: CATEGORY_ITEMS.map(function (it) { return it.key; }),
        tip: "按服务端入库类别过滤（conn/ctrl/alarm/auto/auth/sys）",
      },
      pageSizes: PAGE_SIZES,
      paging: true, refresh: true, chartView: false, freeze: false,
      clientLog: true,
      exportBtn: true,
      handlers: {
        filter: applyFilter,
        clear: clearFilter,
        pageSize: onPageSize,
        prev: function () { prevPage(); },
        next: function () { nextPage(); },
        refresh: reloadCurrent,
        export: onExport,
        extraCombo: function (_index, value) { onCategoryChanged(value); },
        columns: onColumnsChanged,
        clientLog: toggleClientLog,
      },
    });
    tb.setPageSize(pageSize);

    /* ---- 服务端日志表卡片 ---- */
    var thead = HSF.el("thead");
    var tbody = HSF.el("tbody");
    /* dt-fixed = table-layout:fixed（列宽由表头行决定，事件列吃剩余宽）
     * ——事件列的超宽文本才能以「…」截断（参考端形态）。 */
    var table = HSF.el("table.table.dt-table.dt-fixed", null, [thead, tbody]);
    var tableWrap = HSF.el("div.dt-table-wrap", null, [table]);
    var card = HSF.el("div.card.dt-fill", null, [tableWrap]);

    /* ---- 客户端运行日志（折叠副视图；参考 LogPage._client_card 同款）----
     * 网页侧"客户端自己发生了什么"（状态消息收流），与服务端日志是两个
     * 数据源；勾选展开/收起，内容不清零。 */
    var clientLines = [];
    var autoScrollCheck = HSF.el("input.check", { type: "checkbox" });
    autoScrollCheck.checked = true;
    var clientView = HSF.el("div.clientlog-view");
    var clientClearBtn = HSF.el("button.btn", {
      type: "button", text: "清空",
      onclick: function () {
        clientLines = [];
        clientView.textContent = "";
      },
    });
    var clientCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "客户端运行日志" }),
      HSF.el("div.dt-row", null, [
        HSF.el("label.dt-freeze", null, [
          autoScrollCheck,
          HSF.el("span", { text: "自动滚动" }),
        ]),
        HSF.el("span.top-stretch"),
        clientClearBtn,
      ]),
      clientView,
    ]);
    clientCard.classList.add("hidden");       /* 缺省收起（勾选展开） */

    function appendClientLine(text) {
      var d = new Date();
      function p(n) { return (n < 10 ? "0" : "") + n; }
      var stamp = p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
      clientLines.push("[" + stamp + "] " + String(text == null ? "" : text));
      if (clientLines.length > CLIENT_LOG_MAX_LINES) {
        clientLines = clientLines.slice(-CLIENT_LOG_MAX_LINES);
      }
      clientView.textContent = clientLines.join("\n");
      if (autoScrollCheck.checked) clientView.scrollTop = clientView.scrollHeight;
    }

    /** 状态消息旁听（statusbar 写入点旁路；不改全局写入点）。 */
    var unsubClientLog = (HSF.onStatusMessage || function () { return function () {}; })
      (appendClientLine);

    function toggleClientLog(on) {
      clientCard.classList.toggle("hidden", !on);
    }

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.data-page", null, [
      head,
      tb.root,
      card,
      clientCard,
    ]));

    function connected() { return !!((store.get("link") || {}).ready); }

    /** 服务端一行对象 {ts,category,text} → [时间, 类别, 事件] 文本。 */
    function rowTexts(row) {
      var ts = parseInt(row && row.ts, 10) || 0;
      var stamp = ts > 0 ? HSF.fmtStamp(ts) : "--";
      var cat = String((row && row.category) || "");
      return [stamp, CATEGORY_TEXT[cat] || cat || "--",
              String((row && row.text) || "")];
    }

    function visibleColumns() {
      var cols = tb.colCombo.checkedIndexes().slice().sort(function (a, b) { return a - b; });
      if (!cols.length) return [0, 1, 2];
      return cols;
    }

    /* ==================================================================
     *                          列宽（fixed 布局）
     * ==================================================================
     * [为什么必须自己算] table-layout:fixed 下，列宽**只**由首行单元格决定；
     * 没给宽度的列会吃掉「剩余宽」——剩余宽为 0 时该列就是 **0px**（内容
     * 整列不可见）。2026-11-20 实测：390px 视口下「事件」列 th clientWidth=0。
     * ⇒ 每列都给显式宽；下限按内容给；总宽 = max(容器宽, 下限之和)。
     */

    /** rem → px（根字号随 uiScalePct 变 ⇒ 换算一次，别把 px 写死）。 */
    function remPx(rem) {
      var root = parseFloat(
        getComputedStyle(document.documentElement).fontSize) || 14;
      return rem * root;
    }

    /**
     * 算并落到表头（fixed 布局按首行定列宽；表头行是首行）。
     * @param cols  可见列号
     * @param ths   表头单元格（与 cols 同序）
     */
    function applyColWidths(cols, ths) {
      if (!ths.length) return;
      /* 容器可用宽（去掉 .dt-table-wrap 的 1px 边框） */
      var avail = Math.max(0, (tableWrap.clientWidth || 0) - 2);
      var widths = cols.map(function (c) {
        return remPx(COL_MIN_REM[COLUMNS[c]] || 6);
      });
      var sum = widths.reduce(function (a, b) { return a + b; }, 0);
      var spare = Math.max(0, avail - sum);
      /* 余量先把「时间/类别」补到原图定宽，剩下全给最后一列（事件吃剩余）。 */
      for (var i = 0; i < cols.length - 1; i++) {
        var pref = COL_W_REM[COLUMNS[cols[i]]];
        if (!pref) continue;
        var add = Math.min(remPx(pref) - widths[i], spare);
        if (add > 0) { widths[i] += add; spare -= add; }
      }
      if (spare > 0) widths[widths.length - 1] += spare;
      var used = 0;
      for (var j = 0; j < ths.length; j++) {
        var px = Math.round(widths[j]);
        ths[j].style.width = px + "px";
        used += px;
      }
      /* 表格总宽：窄屏时 used > avail ⇒ 容器内横向滚动（列宽不为 0）。 */
      table.style.width = used + "px";
    }

    /* ==================================================================
     *                    服务端查询：入页 / 请求 / 应答
     * ================================================================== */

    function buildQs(extra) {
      var parts = [];
      Object.keys(extra).forEach(function (k) {
        var v = extra[k];
        if (v === null || v === undefined || v === "") return;
        parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
      });
      return "?" + parts.join("&");
    }

    /** 向服务端要一页（offset 为页起点；窗口/类别/关键字随包下发）。 */
    function request(offsetArg) {
      if (inflight) return;
      if (!connected()) { showFailed("服务端未连接"); return; }
      var off = Math.max(0, offsetArg | 0);
      if (pageSize <= 0) pageSize = PAGE_DEFAULT;
      /* [锚点] <=0（新查询）先定格为本地"现在"再发包；翻页沿用。 */
      if (anchorTs <= 0) anchorTs = Date.now();
      inflight = true;
      pendingOffset = off;
      pendingLimit = pageSize;
      var q = {
        offset: off,
        limit: pageSize,
        beforeTs: anchorTs,
      };
      if (loMs > 0) q.loTs = loMs;
      if (hiMs > 0) q.hiTs = hiMs;
      if (category) q.category = category;
      if (kw) q.kw = kw;
      HSF.api.logPage(buildQs(q)).then(function (doc) { onLogReady(doc); });
    }

    function onLogReady(doc) {
      if (!doc || typeof doc !== "object") { inflight = false; return; }
      if (!doc.ok) {
        inflight = false;
        var err = String(doc.err || "服务端拒绝");
        if (err.indexOf(UNKNOWN_CMD_ERR) >= 0) {
          /* 老服务端不认 0x0119：提示 + 空表，不崩。 */
          serverSupported = false;
          showUnsupported();
        } else {
          /* 服务端明确拒绝（如"落盘未启用：服务端没有日志可查"）：显示原因。 */
          rows = [];
          totalRows = 0;
          fillTable();
          tb.setTotalLabel("查询失败：" + err);
          HSF.statusMessage("运行日志查询失败：" + err);
        }
        return;
      }
      /* [应答归属] offset/limit 必须回显发出的值，否则是迟到旧应答——丢弃。
       * 在途闸也不动（本包应答还没回来，别提前放行下一个请求）。 */
      var rOffset = parseInt(doc.offset, 10);
      var rLimit = parseInt(doc.limit, 10);
      if (rOffset !== pendingOffset || rLimit !== pendingLimit) return;
      inflight = false;
      serverSupported = true;
      offset = pendingOffset;
      totalRows = parseInt(doc.total, 10) || 0;
      rows = (doc.rows || []).filter(function (r) { return r && typeof r === "object"; })
        .map(rowTexts);
      fillTable();
    }

    function onLogFailed(msg) {
      inflight = false;
      showFailed(String(msg));
    }

    function showFailed(msg) {
      tb.setTotalLabel("查询失败：" + msg);
      if (tb.btnPrev) tb.btnPrev.disabled = true;
      if (tb.btnNext) tb.btnNext.disabled = true;
      HSF.statusMessage("运行日志：" + msg);
    }

    function showUnsupported() {
      rows = [];
      totalRows = 0;
      fillTable();
      tb.setTotalLabel("服务端不支持运行日志查询（0x0119），请升级服务端");
      HSF.statusMessage("运行日志：服务端不支持该命令（未知命令），请升级服务端");
    }

    /* ---- 铺表 + 分页 UI ---- */

    function fillTable() {
      var cols = visibleColumns();
      HSF.replaceChildren(thead);
      var hr = HSF.el("tr");
      var ths = [];
      cols.forEach(function (c) {
        var th = HSF.el("th", { text: COLUMNS[c] });
        ths.push(th);
        hr.appendChild(th);
      });
      thead.appendChild(hr);
      /* [列宽] 三列都给显式宽（fixed 布局：没给宽的列会被压成 0px）。 */
      applyColWidths(cols, ths);
      /* [列宽·补一拍] 首次铺表时容器可能还没排版完（clientWidth=0）⇒
       * 下一帧布局稳定后按真实宽度重算一次（否则表格停在最窄的一档）。 */
      if (typeof requestAnimationFrame === "function") {
        requestAnimationFrame(function () {
          if (ths.length && ths[0].isConnected) applyColWidths(cols, ths);
        });
      }
      HSF.replaceChildren(tbody);
      var frag = document.createDocumentFragment();
      rows.forEach(function (row) {
        var tr = HSF.el("tr");
        cols.forEach(function (c) {
          var td = HSF.el("td", { text: c < row.length ? row[c] : "" });
          /* 时间/类别居中，事件左对齐（长文本从左读）。 */
          if (c === 2) td.classList.add("ta-left");
          tr.appendChild(td);
        });
        frag.appendChild(tr);
      });
      tbody.appendChild(frag);
      tableWrap.scrollTop = 0;
      updatePagerUI();
    }

    function pageCount() {
      if (pageSize <= 0) return 0;
      return Math.max(1, Math.ceil(totalRows / pageSize));
    }

    function updatePagerUI() {
      var pages = pageCount();
      var cur = pageSize > 0 ? (Math.floor(offset / pageSize) + 1) : 1;
      tb.setPageLabel("第 " + cur + "/" + Math.max(1, pages) + " 页");
      var shown = rows.length;
      tb.setTotalLabel("共 " + totalRows + " 条（本页 " + shown + "）" +
                       (shown ? "" : "（无数据）"));
      tb.setCountLabel("本页显示 " + shown + " 行");
      if (tb.btnPrev) tb.btnPrev.disabled = offset <= 0;
      if (tb.btnNext) tb.btnNext.disabled = offset + shown >= totalRows;
    }

    function prevPage() {
      if (inflight || pageSize <= 0 || offset <= 0) return;
      request(offset - pageSize);       /* 翻页沿用当前锚点 */
    }

    function nextPage() {
      if (inflight || pageSize <= 0 || !rows.length) return;
      if (offset + rows.length >= totalRows) return;
      request(offset + pageSize);
    }

    function onPageSize(size) {
      /* 改每页条数：回第 1 页 + 锚点归零（行号口径变了）。 */
      if (!(size > 0) || size === pageSize) return;
      pageSize = size;
      HSF.prefs.set("logPageSize", size);      /* 本机记住（日志页） */
      anchorTs = 0;
      request(0);
    }

    function reloadCurrent() {
      /* 手动刷新：锚点归零（重新加载想看的是"此刻"），offset 不变。 */
      anchorTs = 0;
      request(offset);
    }

    /* ---- 筛选（类别立即生效；时间窗/关键字按「筛选」）---- */

    function onCategoryChanged(value) {
      /* 换类别 = 新查询（锚点归零、回第 1 页）；时间窗/关键字保持。
       * [签名对齐] 工具条回调给 (selectedIndex, value)，适配层只传 value
       * 过来——曾把形参写成 (_index, value) ⇒ value 恒 undefined ⇒
       * 类别筛永远落回空串（2026-11-13 实锅：选「控制」后表还是全部）。 */
      category = String(value || "");
      anchorTs = 0;
      request(0);
    }

    function timeWindowMs() {
      var lo = (tb.startEdit.value || "").trim()
        ? HSF.normStamp(tb.startEdit.value, false) : null;
      var hi = (tb.endEdit.value || "").trim()
        ? HSF.normStamp(tb.endEdit.value, true) : null;
      if (lo === null && hi === null) return [0, 0];
      return [HSF.stampToMs(lo) || 0, HSF.stampToMs(hi) || 0];
    }

    function applyFilter() {
      var rawStart = (tb.startEdit.value || "").trim();
      var rawEnd = (tb.endEdit.value || "").trim();
      if (rawStart && HSF.normStamp(rawStart, false) === null) {
        HSF.statusMessage("起始时间格式不对（HH:MM / MM-dd HH:MM / yyyy-MM-dd HH:MM）");
        return;
      }
      if (rawEnd && HSF.normStamp(rawEnd, true) === null) {
        HSF.statusMessage("结束时间格式不对（HH:MM / MM-dd HH:MM / yyyy-MM-dd HH:MM）");
        return;
      }
      var win = timeWindowMs();
      if (win[0] > 0 && win[1] > 0 && win[0] > win[1]) {
        HSF.statusMessage("起始时间晚于结束时间，未筛选");
        return;
      }
      loMs = win[0];
      hiMs = win[1];
      kw = (tb.keywordEdit.value || "").trim();
      anchorTs = 0;                 /* 新查询：重取"现在"作锚 */
      request(0);
    }

    function clearFilter() {
      /* 清时间窗 + 关键字，回第 1 页；**类别保持**（是"看哪一类"的视图设置）。 */
      loMs = 0;
      hiMs = 0;
      kw = "";
      tb.startEdit.value = "";
      tb.endEdit.value = "";
      tb.keywordEdit.value = "";
      anchorTs = 0;
      request(0);
    }

    /* ---- 导出（CSV / Excel；PERM_DOWNLOAD 闸；按筛选全量）----
     *
     * [两种形态同一套参数] 后端 /api/log/export 的 format=csv|xlsx；
     * 其余参数（类别/关键字/时间窗/锚点）与 /api/log/page 一致 ⇒ 导出
     * 内容 = 界面筛选命中的**全量**（不是当前页那几十行；与桌面客户端
     * LogPage._export_data 同口径）。
     * [权限闸在 web] 0x0119 没有 export 键可验 ⇒ 下载权限只能由网页
     * 这道闸把守（普通用户及以上可导出，更低角色 403）。
     * [重复点击闸] exporting 为真时再点只提示，不并发两个导出。
     */

    function onExport() {
      if (!ctx.canPerm(HSF.app.PERM.DOWNLOAD)) {
        HSF.statusMessage("权限不足：下载数据需要普通用户及以上账户");
        return;
      }
      HSF.popupMenu(tb.btnExport, [
        { text: "导出 CSV（全部命中行）", onPick: function () { doExport("csv"); } },
        { text: "导出 Excel 工作簿（.xlsx，全部命中行）",
          onPick: function () { doExport("xlsx"); } },
      ]);
    }

    function doExport(fmt) {
      if (exporting) { HSF.statusMessage("正在导出…（请等当前导出结束）"); return; }
      if (!connected()) { HSF.statusMessage("导出失败：未连接服务端"); return; }
      exporting = true;
      /* [筛选条件定格] 类别/关键字/时间窗/锚点在发起那一刻冻结。 */
      var q = {
        format: fmt,
        beforeTs: anchorTs || Date.now(),
      };
      if (loMs > 0) q.loTs = loMs;
      if (hiMs > 0) q.hiTs = hiMs;
      if (category) q.category = category;
      if (kw) q.kw = kw;
      HSF.statusMessage("正在导出…（按筛选全量逐批取数）");
      var token = HSF.api.getToken();
      /* [导出超时闸] 同页 9：导出可能跑很久 ⇒ 超时给足 120s；但必须有闸
       * （网页服务器半开 ⇒ fetch 永远挂着 ⇒ 状态行停在「正在导出…」违反
       * 三态终态）。超时由 HSF.fetchWithTimeout 抛 ⇒ 落下面的 catch。 */
      HSF.fetchWithTimeout("/api/log/export" + buildQs(q), {
        headers: token ? { "Authorization": "Bearer " + token } : {},
        cache: "no-store",
      }, 120000, "导出").then(function (resp) {
        if (!resp.ok) {
          return resp.json().catch(function () { return null; }).then(function (doc) {
            throw new Error((doc && doc.err) || ("HTTP " + resp.status));
          });
        }
        var cd = resp.headers.get("Content-Disposition") || "";
        var m = /filename="([^"]+)"/.exec(cd);
        var filename = m ? m[1] : ("hsf_log." + fmt);
        return resp.blob().then(function (blob) { return { blob: blob, filename: filename }; });
      }).then(function (got) {
        exporting = false;
        /* [XLSX 空文件 = 失败] XLSX 的取数与写盘都在第一个字节发出之前
         * 完成（后端先写临时文件、再分块回读）⇒ 中途失败时一个字节都没
         * 发出来，浏览器拿到 0 字节文件。**不能**报"导出完成"（那是骗
         * 操作员：文件是空的/坏的）。 */
        if (fmt === "xlsx" && (!got.blob || got.blob.size === 0)) {
          HSF.statusMessage("导出失败：未取到数据（链路中断或上游拒绝），请重试");
          return;
        }
        var url = URL.createObjectURL(got.blob);
        var a = HSF.el("a", { href: url, download: got.filename });
        document.body.appendChild(a);
        a.click();
        a.parentNode.removeChild(a);
        setTimeout(function () { URL.revokeObjectURL(url); }, 3000);
        if (fmt === "csv" && got.blob.type.indexOf("csv") >= 0) {
          got.blob.text().then(function (text) {
            var lines = text.split(/\r\n/).filter(function (l) { return l.length; });
            var n = Math.max(0, lines.length - 1);
            var aborted = text.indexOf("#EXPORT_ABORTED") >= 0;
            HSF.statusMessage("已导出 " + n + " 条 → " + got.filename +
              (aborted ? "（导出中止：取数中途链路断开，文件尾部有说明行）" : ""));
          });
        } else {
          HSF.statusMessage("导出完成 → " + got.filename);
        }
      }).catch(function (err) {
        exporting = false;
        HSF.statusMessage("导出失败：" + (err && err.message ? err.message : err));
      });
    }

    /* ==================================================================
     *                          生命周期
     * ================================================================== */

    function onColumnsChanged() {
      var cols = visibleColumns();
      fillTable();
      HSF.statusMessage("显示 " + cols.length + "/" + COLUMNS.length + " 列：" +
                        cols.map(function (c) { return COLUMNS[c]; }).join("、"));
    }

    /* 入页：拉第 1 页服务端运行日志（重数据入页才取）。 */
    if (!serverSupported) {
      showUnsupported();
    } else {
      anchorTs = 0;                  /* 新查询：重取"现在"作锚 */
      request(0);
    }

    /* [窗宽变化 → 重算列宽] 列宽按视口宽分配（页面 9 同款口径：
     * 参考端在 resizeEvent 里重算，page_base.py:395）。
     * 不重算的话：窗口拉窄后表格仍按旧宽排 ⇒ 「事件」列又被压成 0px
     * （2026-11-20 塌列缺陷的另一半触发路径）。 */
    var onResize = function () {
      var cols = visibleColumns();
      var ths = thead.querySelectorAll("th");
      if (ths.length) applyColWidths(cols, Array.prototype.slice.call(ths));
    };
    window.addEventListener("resize", onResize);

    activeUnmount = function () {
      window.removeEventListener("resize", onResize);
      if (unsubClientLog) unsubClientLog();
      tb.destroy();
    };
    void ctx;
  }
})();
