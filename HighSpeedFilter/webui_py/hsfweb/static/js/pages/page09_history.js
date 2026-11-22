/* -*- coding: utf-8 -*-
 * 页 9 历史数据：服务端历史库（0x0118）分页浏览 + 页内表格/图表双视图。
 *
 * [对齐参考 pages_other.HistoryPage 逐项移植（web 数据源同一服务端库）]
 *   · 三行工具条（共用 data_toolbar）：筛选行（起止时间/列多选/关键字/
 *     筛选/清除筛选）+ 分页行（每页 10/20/50 + 上下页 + 统计 + 刷新）+
 *     视图行（折线列多选 + 表格/图表双视图 + 滚动锁定 + 导出数据）；
 *   · **显示列 16**（水温在第 8 位）；服务端发**原值数组 + cols 名**，
 *     格式化在客户端（%.2f / %.0f / 0x%08X / NULL→"--"，参考
 *     _server_row_texts 同口径）；异常行淡红底（故障/浊度≥20/压差说明）；
 *   · **所有筛选条件下推服务端同一条 WHERE**（loTs/hiTs/kw/kwCols）——
 *     本地筛=「关键字筛选无效」；
 *   · **分页窗口锚点**：首屏取一次、翻页原样回传；第 1 页每 3s 带新锚点
 *     整页重取（追最新），第 N 页只探总数不动内容；在途单飞 + 页码夹取；
 *   · 起始框缺省回填 earliestTs（只填一次、用户碰过不覆盖；坏数据闸
 *     VALID_TS_MIN）；双击表头排序（数值列按数值、非数值排后）；
 *   · **图表视图**：页内二选一不弹窗；按筛选条件分批累加**全量**
 *     （批 50、上限 12000 行）；每线等距抽样 ≤2000 点且首尾必留；
 *     筛选签名（时间/关键字/列，不含锚点）防重复上百批重取；
 *   · **导出 = 筛选命中全量**：后端 /api/history/export 批取流（CSV 与
 *     XLSX 都**不限行数**、都按 chunked 流式下发），web 冻结筛选后触发
 *     下载。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var COLUMNS = ["时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
                 "搅拌转速(rpm)", "浊度1(NTU)", "浊度2(NTU)", "水温(℃)",
                 "瞬时流量", "正累计", "压差(MPa)",
                 "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)"];
  /* 折线列 = 全部数值列（时间/模式/搅拌状态是文本，画不了折线）。 */
  var CHART_COL_IDX = [];
  for (var ci = 3; ci < COLUMNS.length; ci++) CHART_COL_IDX.push(ci);

  var PAGE_SIZES = [10, 20, 50];
  var PAGE_DEFAULT = 20;
  var VALID_TS_MIN = 1577836800000;          /* 2020-01-01，坏数据闸 */
  var LIVE_MS = 3000;                        /* 第 1 页追新 / 探总数周期 */
  var CHART_MAX_ROWS = 12000;
  var CHART_CHUNK = 50;                      /* 协议上限（HISTORY_PAGE_MAX） */
  var CHART_MAX_POINTS = 2000;
  var TURBIDITY_WARN_NTU = 20.0;
  /* ---- 列宽：按**当前字体实测**表头文字算下限 + 按权重分剩余宽 ----
   * [为什么不再写死一张 px 表] 写死的像素只在量它的那个字号下成立：
   * 参考端早先写死 165/78/96…（按 10pt 量），客户端实际 15pt ⇒ 12 列表头
   * 被截成「出频率(H」「度1(NTU」（2026-11-10 实锅）。参考端后来改成
   * `set_column_weights(weights, auto_min_texts=…)`（pages_other.py:1641）
   * ——每次重算都按表头字体现量，web 侧同口径（HSF.measureText）。
   * 权重逐值照参考端 (3,2,2,2,2,2,2,2,3,2,2,2,2,3,3)（16 列时末列补 3，
   * 与参考端「权重不足按末位补齐」同款）。 */
  var COL_WEIGHTS = [3, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 3, 3, 3];
  /* 表头右侧要留排序指示器 + 左右单元格内边距（参考 page_base.py:381）。 */
  var COL_PAD = 30;
  /* 内容格的内边距余量：`.table td` 左右 8px + 边框余量（内容**没有**
   * 排序指示器，不能跟表头同用 COL_PAD，否则每列白多 12px）。 */
  var COL_CELL_PAD = 18;
  /* 全局最小档（参考 header.setMinimumSectionSize(72)）。 */
  var COL_MIN_W_DEF = 72;
  /* 时间列按**内容**量（表头只有「时间」两个字，实际要装
   * 'yyyy-MM-dd HH:MM:SS'）——参考 auto_min_texts={0: …} 同款。
   * [DI/DO 也要按内容量 2026-11-19 用户实报「两列数据叠在一起」]
   * 数据是 `0x%08X`（10 字符），比表头 `DI(32位)`（6 字符）**长** ⇒
   * 只按表头量宽，fixed 布局分到的列宽装不下 nowrap 内容 ⇒ 溢出画到
   * 隔壁列 ⇒ 两列数据叠在一起。
   * [模式列同款 2026-11-20 用户实报「web端列数据重叠」] 表头「模式」2 字、
   * 内容「自动·运行中」6 字 ⇒ 又糊在「搅拌状态」上。
   * ⇒ **静态表只是"当前页看不到的长值"的稳定基线**（模式列尤其：运行
   * 子阶段一变，内容长度就变，只靠现量会让列宽每 3s 抖一次）；真正兜住
   * 这一类缺陷的是下面 applyColWidths 里**逐页量实际单元格文字**（参考端
   * page_base._measure_content_texts 同口径）。新增列先想清楚「数据会不会
   * 比表头长」，会就同时补基线。 */
  var COL_MIN_TEXTS = { 0: "2026-11-10 00:00:00",
                        1: "自动·运行中",
                        9: "3.66 m³/h", 10: "2.30 m³",
                        14: "0x00005AAA", 15: "0x00000100" };

  /* 文本宽度测量带缓存（每页 ≤50 行 × 16 列，每 3s 全量重算；
   * canvas measureText 本身便宜，但同文本反复量没必要）。 */
  var _textWCache = {};
  var _textWN = 0;
  var TEXT_W_CACHE_MAX = 4096;
  function textWidth(font, text) {
    var key = font + "\u0000" + text;
    var w = _textWCache[key];
    if (w === undefined) {
      if (_textWN > TEXT_W_CACHE_MAX) { _textWCache = {}; _textWN = 0; }
      w = HSF.measureText(font, text);
      _textWCache[key] = w;
      _textWN++;
    }
    return w;
  }

  /** 各可见列的**最长单元格文本**（{列号: 文本}）。
   *  [为什么必须量内容] 列宽下限只按表头算 ⇒ 表头短、内容长的列拿不到
   *  装得下 nowrap 内容的宽度 ⇒ 文本溢出画到隔壁列（视觉=两列数据重叠）。
   *  记**文本**不记像素：像素只在量它的那个字号下成立（全局缩放/换主题
   *  后要按新字体重算），参考端 page_base.py:472 同款。
   *  "--"/空串是占位不是内容，跳过（不把空值当宽度依据）。 */
  function measureContentTexts(cols, list, font) {
    var out = {};
    var bestW = {};
    for (var i = 0; i < list.length; i++) {
      var texts = list[i].texts || [];
      for (var j = 0; j < cols.length; j++) {
        var c = cols[j];
        var text = c < texts.length ? String(texts[c] == null ? "" : texts[c]) : "";
        if (!text || text === "--") continue;
        var w = textWidth(font, text);
        if (!bestW.hasOwnProperty(c) || w > bestW[c]) {
          bestW[c] = w;
          out[c] = text;
        }
      }
    }
    return out;
  }

  /* 参考客户端 pages_other.LineChartDialog.SERIES_COLORS（画布序列色，
   * 参考实现的名义常量；网格/轴字走主题变量）。 */
  var SERIES_COLORS = ["#2E7CE8", "#E8730C", "#2E9E4F", "#B33DC6",
                       "#C62E2E", "#7A6F00", "#0F9BA8", "#8B5A2B"];

  function cssVar(name) {
    var v = getComputedStyle(document.documentElement).getPropertyValue(name);
    return (v || "").trim() || "#888888";
  }

  function rootScale() {
    var fs = parseFloat(getComputedStyle(document.documentElement).fontSize);
    return (fs || 14) / 14;
  }

  /* ------------------------------------------------------------------
   *      服务端一行（按 cols 顺序的值数组）→ {texts, abnormal, tsMs}
   *      （参考 HistoryPage._server_row_texts 逐条同口径）
   * ------------------------------------------------------------------ */
  function rowOf(raw, cols) {
    var rec = {};
    cols.forEach(function (c, i) { rec[c] = raw[i]; });

    function num(key, digits) {
      var v = rec[key];
      if (v === null || v === undefined || isNaN(Number(v))) return "--";
      return Number(v).toFixed(digits);
    }
    var ts = rec.ts;
    var stamp = (typeof ts === "number" && ts > 0) ? HSF.fmtStamp(ts) : "--";
    var flow = rec.flow, flowUnit = rec.flow_unit || "";
    var total = rec.flow_total, totalUnit = rec.total_unit || "";
    var pressNote = rec.press_note || "";
    var vfdState = rec.vfd_state || "--";
    var texts = [
      stamp,
      rec.mode || "--",
      vfdState,
      num("freq_hz", 2),
      num("current_a", 2),
      num("rpm", 0),
      num("turb1", 2), num("turb2", 2),
      /* 水温：显示第 8 位（线上列序在末尾，按列名取值位置无关）；
       * 无温度机型入库 NULL ⇒ "--"（不是 0℃）。 */
      num("water_temp", 1),
      (flow === null || flow === undefined) ? "--"
        : Number(flow).toFixed(2) + " " + flowUnit,
      (total === null || total === undefined) ? "--"
        : Number(total).toFixed(2) + " " + totalUnit,
      /* [压差列显示**数值** 2026-11-19 用户口径「历史数据里应该记录具体
         的负压值用负数，而不是说是个负压」] 原来 note 优先 ⇒ 负压行显示
         「低于 4.0mA（负压）」、看不到 -0.008 ⇒ 现在数值优先；「当时越限」
         的信号仍在 press_note 上（下面的异常行判定照旧用它）⇒ 不丢信号。 */
      num("press_mpa", 3),
      num("temp", 1), num("hum", 1),
      (rec.di === null || rec.di === undefined) ? "--"
        : "0x" + ((Number(rec.di) & 0xFFFFFFFF) >>> 0).toString(16).toUpperCase().padStart(8, "0"),
      (rec.do === null || rec.do === undefined) ? "--"
        : "0x" + ((Number(rec.do) & 0xFFFFFFFF) >>> 0).toString(16).toUpperCase().padStart(8, "0"),
    ];
    var abnormal = (vfdState.indexOf("故障") >= 0) || !!pressNote ||
      (rec.turb1 !== null && rec.turb1 !== undefined &&
       Number(rec.turb1) >= TURBIDITY_WARN_NTU) ||
      (rec.turb2 !== null && rec.turb2 !== undefined &&
       Number(rec.turb2) >= TURBIDITY_WARN_NTU);
    return { texts: texts, abnormal: abnormal, tsMs: ts > 0 ? ts : null };
  }

  /** 表格文本 → 数值（排序/画图共用）；"--"/纯文字 → null；0x 按十六进制。
   *  整串锚定：数字后只允许空白+单位后缀（时间串不当数值）。 */
  function cellFloat(text) {
    var t = String(text == null ? "" : text).trim();
    if (!t || t === "--") return null;
    if (t.slice(0, 2).toLowerCase() === "0x") {
      var h = parseInt(t.slice(2), 16);
      return isNaN(h) ? null : h;
    }
    var m = /^[-+]?\d*\.?\d+(\s+.*)?$/.exec(t);
    if (!m) return null;
    var v = parseFloat(m[0].split(/\s+/)[0]);
    return isNaN(v) ? null : v;
  }

  /** 等距抽样：n 点压到 ≤ limit 个，且**首尾必留**（参考 _sample_points）。 */
  function samplePoints(points, limit) {
    var n = points.length;
    if (limit <= 1 || n <= limit) return points;
    var step = (n - 1) / (limit - 1);
    var out = [];
    for (var i = 0; i < limit; i++) out.push(points[Math.round(i * step)]);
    out[out.length - 1] = points[n - 1];
    return out;
  }

  /* ================================================================== */
  var activeUnmount = null;

  HSF.router.register(9, {
    title: "历史数据",
    topics: [],                      /* 查询型页面，在线时不订遥测主题 */
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    /* ---- 状态（与参考同名字段一一对应）---- */
    var dataMode = "local";          /* "server" | "local" */
    var pageIndex = 0;
    var pageSize = PAGE_DEFAULT;
    var savedSize = parseInt(HSF.prefs.get("histPageSize", ""), 10);
    if (PAGE_SIZES.indexOf(savedSize) >= 0) pageSize = savedSize;
    var total = 0, dbRows = 0, latestTs = 0;
    var windowEnd = null;            /* 分页窗口锚点（epoch ms） */
    var inflight = false;
    var pendingKind = "page";
    var pendingIndex = 0;
    var rows = [];                   /* [{texts, abnormal, tsMs}]（当前页，旧→新） */
    var sortCol = null, sortDesc = true;
    var viewMode = 0;
    var defaultStartDone = false;

    var chartRows = [];              /* 全量筛选结果累加缓冲 */
    var chartFetchOffset = 0;
    var chartFetching = false;
    var chartAnchor = null;          /* 图表取数的冻结锚点 */
    var chartSig = null;

    /* ---- 工具条 ---- */
    /* [折叠把手挂标题行右侧 2026-11-19] 先建标题行（含右侧把手宿主），
     * dataToolbar 拿到宿主后不建独立折叠行。 */
    var foldHost = HSF.el("span.dt-fold-host");
    var head = HSF.el("div.data-page-head", null, [
      HSF.el("span.page-head-title", { text: "历史数据" }),
      HSF.el("span.page-head-sub.caption", { text: "服务端历史库，分页浏览" }),
      foldHost,
    ]);
    var tb = HSF.dataToolbar({
      pageKey: "history",
      foldHost: foldHost,
      columns: COLUMNS,
      chartColumns: CHART_COL_IDX.map(function (c) { return COLUMNS[c]; }),
      pageSizes: PAGE_SIZES,
      paging: true, refresh: true, chartView: true, freeze: true,
      exportBtn: true,
      handlers: {
        filter: applyFilter,
        clear: clearFilter,
        pageSize: onPageSize,
        prev: function () { prevPage(); },
        next: function () { nextPage(); },
        refresh: reloadCurrent,
        export: onExport,
        view: function (mode) { setView(mode); },
        columns: onColumnsChanged,
        chartColumns: onChartColumnsChanged,
        freeze: function (frozen) { if (!frozen) rebuild(); },
      },
    });
    tb.setPageSize(pageSize);
    tb.chartColCombo.setCheckedIndexes([0]);   /* 默认勾第一个折线列 */
    tb.startEdit.onUserEdit = function () { defaultStartDone = true; };
    tb.endEdit.onUserEdit = function () { defaultStartDone = true; };

    /* ---- 表格卡片 + 图表卡片（页内双视图，二选一）---- */
    var thead = HSF.el("thead");
    var tbody = HSF.el("tbody");
    var table = HSF.el("table.table.dt-table", null, [thead, tbody]);
    /* [固定布局] 参考端历史表是 Fixed 模式按像素分列（page_base.py:373），
     * web 侧同口径：列宽由 applyColWidths 算好后写死，浏览器不再自作主张。 */
    table.style.tableLayout = "fixed";
    var tableWrap = HSF.el("div.dt-table-wrap", null, [table]);
    var tableCard = HSF.el("div.card", null, [tableWrap]);

    var chartStatus = HSF.el("div.caption.hist-chart-status", { text: "" });
    var chartCanvas = HSF.el("canvas.hist-canvas");
    var chartHolder = HSF.el("div.hist-chart-holder", null, [chartCanvas]);
    /* [2026-11-22 视口接管] 缩放/平移后给一个可点的回程（触屏没有双击
     * 习惯，光靠 dblclick 不够）——与桌面端「双击复位」同口径。 */
    var chartResetBtn = HSF.el("button.btn.hist-chart-reset", {
      type: "button", text: "复位视图",
      title: "回到全程时间范围（也可双击图表）",
    });
    chartResetBtn.disabled = true;
    var chartBar = HSF.el("div.hist-chart-bar", null,
                          [chartStatus, chartResetBtn]);
    var chartPage = HSF.el("div.hist-chart-page", null, [chartBar, chartHolder]);
    var tablePage = HSF.el("div.hist-table-page", null, [tableCard]);

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.data-page", null, [
      head,
      tb.root,
      tablePage,
      chartPage,
    ]));
    chartPage.classList.add("hidden");

    /* ==================================================================
     *                       取数（入页 / 翻页 / 探活）
     * ================================================================== */

    function connected() { return !!((store.get("link") || {}).ready); }

    function enterServerMode() {
      if (!connected()) { fallBackLocal("未连接服务端"); return; }
      dataMode = "server";
      pageIndex = 0;
      windowEnd = Date.now();
      requestPage(0);
      startLive();
    }

    function fallBackLocal(reason) {
      dataMode = "local";
      stopLive();
      tb.setTotalLabel("服务端不可用（" + reason + "）");
      tb.setPageLabel("");
      if (tb.btnPrev) tb.btnPrev.disabled = true;
      if (tb.btnNext) tb.btnNext.disabled = true;
    }

    var liveTimer = null;
    function startLive() {
      if (liveTimer === null) liveTimer = setInterval(liveTick, LIVE_MS);
    }
    function stopLive() {
      if (liveTimer !== null) { clearInterval(liveTimer); liveTimer = null; }
    }

    function liveTick() {
      if (dataMode !== "server" || inflight) return;
      if (pageIndex === 0) {
        windowEnd = Date.now();      /* 换新锚点 = 追最新 */
        requestPage(0);
      } else {
        requestPage(pageIndex, true);   /* 只探总数，不动页内容 */
      }
    }

    function serverTimeRange() {
      var lo = HSF.normStamp(tb.startEdit.value, false);
      var hi = HSF.normStamp(tb.endEdit.value, true);
      if (lo === null && hi === null) return [null, null];
      return [HSF.stampToMs(lo), HSF.stampToMs(hi)];
    }

    function visibleColumns() {
      var cols = tb.colCombo.checkedIndexes().slice().sort(function (a, b) { return a - b; });
      return cols.length ? cols : range(COLUMNS.length);
    }

    function range(n) {
      var out = [];
      for (var i = 0; i < n; i++) out.push(i);
      return out;
    }

    function buildQs(base) {
      var parts = [];
      Object.keys(base).forEach(function (k) {
        var v = base[k];
        if (v === null || v === undefined || v === "") return;
        parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
      });
      return "?" + parts.join("&");
    }

    function filterQs(offset, limit, anchor) {
      var tr = serverTimeRange();
      var q = {
        offset: offset,
        limit: limit,
        beforeTs: anchor || 0,
        kw: (tb.keywordEdit.value || "").trim(),
        kwCols: visibleColumns().join(","),
      };
      if (tr[0]) q.loTs = tr[0];
      if (tr[1]) q.hiTs = tr[1];
      return buildQs(q);
    }

    /** 向服务端要第 index 页（probeOnly=只探总数）。 */
    function requestPage(index, probeOnly) {
      if (inflight) return;
      if (!connected()) { fallBackLocal("未连接服务端"); return; }
      inflight = true;
      pendingKind = probeOnly ? "probe" : "page";
      pendingIndex = index;
      windowEnd = windowEnd || Date.now();
      HSF.api.historyPage(filterQs(index * pageSize, pageSize, windowEnd))
        .then(function (doc) { onPageReady(doc); });
    }

    function onPageFailed(msg) {
      inflight = false;
      if (dataMode !== "server") return;
      if (pendingKind === "chart") {
        chartFetching = false;
        setChartStatus("图表取数失败：" + msg);
        HSF.statusMessage("图表取数失败：" + msg);
        return;
      }
      tb.setTotalLabel("读取失败：" + msg);
      HSF.statusMessage("历史数据读取失败：" + msg);
    }

    function onPageReady(doc) {
      /* [图表全量取数走自己的累加器] 早于表格逻辑返回。 */
      if (pendingKind === "chart") {
        if (dataMode !== "server") { inflight = false; return; }
        chartAccumulate(doc);
        return;
      }
      inflight = false;
      if (dataMode !== "server") return;
      if (!doc || !doc.ok) { onPageFailed((doc && doc.err) || "服务端拒绝"); return; }

      var cols = (doc.cols && doc.cols.length) ? doc.cols.slice()
        : ["ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
           "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
           "press_mpa", "press_note", "temp", "hum", "di", "do", "water_temp"];
      total = parseInt(doc.total, 10) || 0;
      dbRows = parseInt(doc.dbRows, 10) || 0;
      var latest = parseInt(doc.latestTs, 10) || 0;
      var earliest = parseInt(doc.earliestTs, 10) || 0;
      var changed = latest !== latestTs;
      latestTs = latest;
      applyDefaultStart(earliest);

      if (pendingKind === "page") {
        pageIndex = pendingIndex;
        var raws = doc.rows || [];
        rows = raws.map(function (r) { return rowOf(r, cols); });
        rebuild();
      }

      /* 页码夹取：库被裁剪（ring 策略）后总页数可能变少。 */
      var pages = pageCount();
      if (pages && pageIndex >= pages) {
        pageIndex = pages - 1;
        if (pendingKind === "probe") { requestPage(pageIndex); return; }
      }
      updatePagerUI();
      if (viewMode === 1 && filterSig() !== chartSig) {
        /* [只在筛选真变时重取] 锚点漂移不算筛选变化（参考同款签名闸）。 */
        chartRefresh();
      }
      void changed;
    }

    function applyDefaultStart(earliestTs) {
      if (defaultStartDone) return;
      if (!earliestTs || earliestTs < VALID_TS_MIN) return;
      if ((tb.startEdit.value || "").trim() || tb.startEdit.wasTouched()) return;
      defaultStartDone = true;
      tb.startEdit.value = HSF.fmtStamp(earliestTs);
      HSF.statusMessage("已载入服务端历史：最早一条 " + tb.startEdit.value +
                        "（可改起始时间筛选）");
    }

    /* ---- 分页 ---- */
    function pageCount() {
      if (pageSize <= 0) return 0;
      return Math.max(1, Math.ceil(total / pageSize));
    }

    function updatePagerUI() {
      var pages = pageCount();
      tb.setPageLabel("第 " + (pageIndex + 1) + "/" + Math.max(1, pages) + " 页");
      tb.setTotalLabel("共 " + total + " 条（本页 " + rows.length + "）　库内共 " +
                       dbRows + " 条");
      if (tb.btnPrev) tb.btnPrev.disabled = pageIndex <= 0;
      if (tb.btnNext) tb.btnNext.disabled = pageIndex + 1 >= pages;
    }

    function prevPage() {
      if (dataMode !== "server" || pageIndex <= 0) return;
      requestPage(pageIndex - 1);
    }

    function nextPage() {
      if (dataMode !== "server") return;
      if (pageIndex + 1 >= pageCount()) return;
      requestPage(pageIndex + 1);
    }

    function onPageSize(size) {
      if (!(size > 0) || size === pageSize) return;
      pageSize = size;
      HSF.prefs.set("histPageSize", size);       /* 本机记住（历史页） */
      if (dataMode === "server") {
        pageIndex = 0;
        requestPage(0);
      } else {
        rebuild();
      }
    }

    function reloadCurrent() {
      if (dataMode !== "server") { enterServerMode(); return; }
      requestPage(pageIndex);        /* 续用原窗口锚点（行内容不乱跳） */
    }

    /* ---- 列显示 / 排序 / 筛选 ---- */
    function onColumnsChanged() {
      var cols = visibleColumns();
      rebuild();
      if (dataMode === "server" && (tb.keywordEdit.value || "").trim()) {
        /* [关键字激活时列也是筛选条件] kwCols 进同一条 WHERE ⇒ 重取第 0 页。 */
        pageIndex = 0;
        windowEnd = Date.now();
        requestPage(0);
      }
      var headers = cols.map(function (c) { return COLUMNS[c]; });
      HSF.statusMessage("显示 " + cols.length + "/" + COLUMNS.length + " 列：" +
        (headers.length <= 4 ? headers.join("、") : headers.slice(0, 4).join("、") + "…"));
    }

    function visibleRows() {
      /* 服务端模式：筛选已在服务端 WHERE 里做过，直接返回当前页（排序生效）。 */
      var list = rows.slice();
      if (sortCol === null) return list.reverse();   /* 默认最新在上 */
      var col = sortCol, desc = sortDesc;
      list.sort(function (a, b) {
        var ka = sortKey(a.texts[col]), kb = sortKey(b.texts[col]);
        var c;
        if (ka[0] !== kb[0]) c = ka[0] - kb[0];
        else if (ka[0] === 0) c = ka[1] - kb[1];
        else c = ka[2] < kb[2] ? -1 : (ka[2] > kb[2] ? 1 : 0);
        return desc ? -c : c;
      });
      return list;
    }

    function sortKey(text) {
      var num = cellFloat(text);
      /* 数值列按数值排；非数值（"--"、文字）排在数值后面，组内按文本序。 */
      return num !== null ? [0, num, ""] : [1, 0, String(text)];
    }

    /**
     * 列宽分配（对齐参考 DataTable._apply_column_widths，
     * page_base.py:295-400）：
     *   ① 各列下限 = max(全局最小档 72, 当前字体实测的表头文字宽 + 30,
     *      **当前页该列最长单元格文字宽 + 18**)；
     *      时间列按内容串「2026-11-10 00:00:00」量（表头只有两个字）。
     *   ② 剩余宽（视口 - 下限总和）按权重分给各列；最后一列吃余数。
     *   ③ 下限总和超视口 ⇒ 宁出横向滚动条，也不把表头压成半个字 /
     *      让内容溢出叠到隔壁列。
     * 表格用 table-layout:fixed（参考端也是 Fixed 模式按像素分列）。
     */
    function applyColWidths(cols, ths, list) {
      var avail = (tableWrap.clientWidth || 0) - 2;
      if (avail <= 0 || !ths.length) return;
      var cs = window.getComputedStyle(ths[0]);
      var font = (cs.fontStyle || "") + " " + (cs.fontWeight || "") + " " +
        (cs.fontSize || "14px") + " " + (cs.fontFamily || "sans-serif");
      /* [内容下限] fixed 布局 + nowrap ⇒ 宽度不够就是溢出叠列，不会自动
       * 撑开 ⇒ 每列都要装得下**当前页最长的那条内容**（见函数头注）。 */
      var content = measureContentTexts(cols, list || [], font);
      var floors = [];
      var sum = 0;
      var totalW = 0;
      for (var i = 0; i < cols.length; i++) {
        var c = cols[i];
        var txt = COL_MIN_TEXTS.hasOwnProperty(c)
          ? COL_MIN_TEXTS[c] : COLUMNS[c];
        var need = textWidth(font, txt) + COL_PAD;
        if (content.hasOwnProperty(c)) {
          need = Math.max(need, textWidth(font, content[c]) + COL_CELL_PAD);
        }
        var fl = Math.max(COL_MIN_W_DEF, need);
        floors.push(fl);
        sum += fl;
        totalW += COL_WEIGHTS[c] || 1;
      }
      var spare = Math.max(0, avail - sum);
      var used = 0;
      for (var j = 0; j < ths.length - 1; j++) {
        var w = COL_WEIGHTS[cols[j]] || 1;
        var px = floors[j] + Math.floor(spare * w / totalW);
        ths[j].style.width = px + "px";
        used += px;
      }
      var lastPx = Math.max(floors[floors.length - 1], avail - used);
      ths[ths.length - 1].style.width = lastPx + "px";
      used += lastPx;
      table.style.width = used + "px";
    }

    function rebuild() {
      var list = visibleRows();
      var cols = visibleColumns();
      /* 表头（可见列；排序列带 ↓/↑ 指示，双击换序）。 */
      HSF.replaceChildren(thead);
      var hr = HSF.el("tr");
      var ths = [];
      cols.forEach(function (c) {
        var label = COLUMNS[c] +
          (sortCol === c ? (sortDesc ? " ↓" : " ↑") : "");
        var th = HSF.el("th", { text: label, title: "双击排序" });
        th.addEventListener("dblclick", function () { headerDblClicked(c); });
        ths.push(th);
        hr.appendChild(th);
      });
      thead.appendChild(hr);
      applyColWidths(cols, ths, list);
      HSF.replaceChildren(tbody);
      var frag = document.createDocumentFragment();
      list.forEach(function (row) {
        var tr = HSF.el("tr");
        if (row.abnormal) tr.classList.add("abnormal");
        cols.forEach(function (c) {
          var td = HSF.el("td", { text: c < row.texts.length ? row.texts[c] : "" });
          tr.appendChild(td);
        });
        frag.appendChild(tr);
      });
      tbody.appendChild(frag);
      if (!tb.frozen()) tableWrap.scrollTop = 0;
      if (dataMode === "server") {
        tb.setCountLabel("本页显示 " + list.length + " 行");
      } else {
        tb.setCountLabel(list.length + "/" + rows.length + " 条");
      }
    }

    function headerDblClicked(col) {
      /* 新列→降序；同列降→升；同列升→取消排序。 */
      if (sortCol === col) {
        if (sortDesc) sortDesc = false;
        else sortCol = null;
      } else {
        sortCol = col;
        sortDesc = true;
      }
      rebuild();
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
      if (dataMode === "server") {
        /* [所有筛选条件都下推服务端] 时间/关键字/列一起进同一条 WHERE。 */
        pageIndex = 0;
        windowEnd = Date.now();
        requestPage(0);
      }
    }

    function clearFilter() {
      tb.startEdit.value = "";
      tb.endEdit.value = "";
      tb.keywordEdit.value = "";
      /* [清筛选后不自动回填默认值] 保持清空，直到下次入页。 */
      defaultStartDone = true;
      if (dataMode === "server") {
        pageIndex = 0;
        requestPage(0);
      }
    }

    /* ==================================================================
     *          图表视图（页内二选一；全量取数 + 等距抽样）
     * ================================================================== */

    function setChartStatus(text) {
      text = String(text == null ? "" : text);
      if (chartStatus.textContent !== text) chartStatus.textContent = text;
    }

    function setView(mode) {
      mode = mode === 1 ? 1 : 0;
      viewMode = mode;
      tablePage.classList.toggle("hidden", mode === 1);
      chartPage.classList.toggle("hidden", mode !== 1);
      tb.setViewMode(mode);
      if (mode === 1) chartRefresh();
    }

    function onChartColumnsChanged() {
      /* [勾选列变了只重画不重取] 全量数据已在 chartRows，换列只是换画法。 */
      if (viewMode === 1 && chartRows.length) {
        /* [复审] 旧读数时刻可能只属于被取消的列 ⇒ 清掉，免得气泡写
         * 「该时刻附近无数据点」。 */
        chartPickedT = null;
        chartHoverT = null;
        chartBuild(chartRows.slice(), total);
      }
    }

    function filterSig() {
      /* 签名（时间窗/关键字/列）。**不含窗口锚点**——它会随刷新重锚定，
       * 算进签名会每拍都变 ⇒ 每 3 秒重跑上百批全量取数（参考实测踩到）。 */
      var tr = serverTimeRange();
      return JSON.stringify([tr[0], tr[1],
        (tb.keywordEdit.value || "").trim() || null, visibleColumns()]);
    }

    function chartRefresh() {
      if (dataMode !== "server") {
        setChartStatus("服务端不可用（未连接服务端），无历史图表");
        return;
      }
      if (chartFetching) return;   /* 取数中：别用半截数据重画 */
      var sig = filterSig();
      if (chartRows.length && sig === chartSig) {
        chartBuild(chartRows.slice(), total);   /* 筛选没变：原地重画 */
        return;
      }
      chartSig = sig;
      /* [新筛选=新数据集 ⇒ 视口归零] 沿用旧视口会指向一段不存在的区间
       * （用户看不出是空窗口还是没数据）。列勾选变化不重置（同一批数据）。 */
      chartView = null;
      chartPickedT = null;
      chartHoverT = null;
      chartResetBtn.disabled = true;
      /* [复审] 全量分块取数期间不要继续显示旧的缩放图（按钮已灰但画面
       * 还是老视口，用户会以为图表卡住）⇒ 先清画布与可测属性。 */
      chartSeries = null;
      try {
        var g0 = chartCanvas.getContext("2d");
        g0.clearRect(0, 0, chartCanvas.width, chartCanvas.height);
      } catch (e) { /* 画布未就绪：忽略 */ }
      chartCanvas.setAttribute("data-view", "0");
      chartCanvas.setAttribute("data-read", "none");
      /* [冻结锚点] 取数开始那一刻定下，中途刷新重锚不影响。 */
      chartAnchor = windowEnd;
      chartRows = [];
      chartFetchOffset = 0;
      chartFetching = true;
      setChartStatus("正在加载图表数据…");
      chartRequestChunk();
    }

    function chartRequestChunk() {
      inflight = true;
      pendingKind = "chart";
      var tr = serverTimeRange();
      var q = {
        offset: chartFetchOffset,
        limit: CHART_CHUNK,
        beforeTs: chartAnchor || 0,
        kw: (tb.keywordEdit.value || "").trim(),
        kwCols: visibleColumns().join(","),
      };
      if (tr[0]) q.loTs = tr[0];
      if (tr[1]) q.hiTs = tr[1];
      HSF.api.historyPage(buildQs(q)).then(function (doc) { onPageReady(doc); });
    }

    function chartAccumulate(doc) {
      inflight = false;
      if (!chartFetching) return;              /* 用户已切走/筛选已变 */
      if (!doc || !doc.ok) {
        chartFetching = false;
        setChartStatus("图表取数失败：" + ((doc && doc.err) || "服务端拒绝"));
        return;
      }
      var cols = (doc.cols && doc.cols.length) ? doc.cols.slice()
        : ["ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
           "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
           "press_mpa", "press_note", "temp", "hum", "di", "do", "water_temp"];
      var raws = doc.rows || [];
      raws.forEach(function (r) { chartRows.push(rowOf(r, cols)); });
      total = parseInt(doc.total, 10) || total;
      var got = raws.length;
      chartFetchOffset += got;
      /* [收尾判据] 短批 = 没有更多行了；或触到上限（同 C 端）。 */
      if (got < CHART_CHUNK || chartRows.length >= CHART_MAX_ROWS) {
        chartFetching = false;
        chartBuild(chartRows.slice(), total);
        return;
      }
      setChartStatus("正在加载图表数据… 已取 " + chartRows.length + "/" + total + " 行");
      chartRequestChunk();
    }

    function chartBuild(list, totalRows) {
      var sel = tb.chartColCombo.checkedIndexes();
      var cols = sel.map(function (i) { return CHART_COL_IDX[i]; })
        .filter(function (c) { return c < COLUMNS.length; });
      if (!cols.length) {
        setChartStatus("请先在「折线列」里勾选至少一列");
        HSF.statusMessage("请先在「折线列」里勾选至少一列");
        return;
      }
      /* 显式按时间正序：X 轴必须旧→新，不把正确性押在批次顺序上。 */
      var ordered = list.slice().sort(function (a, b) {
        return (a.tsMs || 0) - (b.tsMs || 0);
      });
      var series = [];
      var rawCounts = [];
      cols.forEach(function (col) {
        var points = [];
        ordered.forEach(function (row) {
          var v = col < row.texts.length ? cellFloat(row.texts[col]) : null;
          if (v === null || row.tsMs === null) return;
          points.push({ t: row.tsMs, v: v });
        });
        rawCounts.push(points.length);
        if (points.length >= 2) {
          series.push({
            name: COLUMNS[col],
            points: samplePoints(points, CHART_MAX_POINTS),
          });
        }
      });
      if (!series.length) {
        setChartStatus("可画的数值点不足 2 个（换列或放宽筛选）");
        HSF.statusMessage("可画的数值点不足 2 个（换列或放宽筛选）");
        return;
      }
      drawHistChart(series);
      var peak = rawCounts.length ? Math.max.apply(null, rawCounts) : 0;
      setChartStatus("筛选共 " + (totalRows || list.length) + " 行 · 图表取回 " +
                     list.length + " 行 · 每线最多 " + CHART_MAX_POINTS + " 点（" +
                     (peak > CHART_MAX_POINTS ? "已等距抽样" : "未抽样") + "）");
    }

    /* ---- 折线绘制（Canvas；各序列各自归一化，不做双 Y 轴）----
     * [2026-11-22 补交互：对齐桌面端历史图] 桌面端有「单指拖动/左键拖动
     * 平移、双指捏合/滚轮缩放、双击复位、点按读数气泡」；网页历史图此前
     * 是一张**纯静态图**（canvas 上零事件）⇒ 手机上连拖动都会被页面滚动
     * 抢走。这里补齐同一套：
     *   · 视口 chartView={t0,t1}（null=全程跟随；自动刷新不重置视口）
     *   · 拖动平移 / 捏合与滚轮缩放（只作用时间轴；Y 轴随可见段自适应）
     *   · 双击或「复位视图」按钮回全程
     *   · 悬停/点按吸附最近时刻 ⇒ 十字线 + 气泡，显示**各序列原始值**
     *     （各线独立归一化，不读原值必被误读）
     */
    var chartSeries = null;      /* 最近一次构建的序列（重绘用，不重取数） */
    var chartView = null;        /* 用户接管的视口 {t0,t1}；null=全程 */
    var chartPickedT = null;     /* 点按固定的读数时刻 */
    var chartHoverT = null;      /* 悬停读数时刻（鼠标） */
    var chartLastDraw = null;    /* 最近绘制的几何（命中测试用） */
    var chartRaf = 0;
    var chartPointers = {};      /* 触摸指针表（双指捏合） */
    var chartPinchPrev = 0;
    var chartDrag = null;        /* {x, t0, t1} 拖动原点 */
    var chartDragged = false;

    function trimNum(v) {
      return String(parseFloat(Number(v).toPrecision(4)));
    }

    function pad2n(n) { return (n < 10 ? "0" : "") + n; }

    /** X 轴刻度：≤60s 视口带 0.1s（否则 5 档刻度同秒）；跨天带日期。 */
    function chartTickText(ms, t0, t1) {
      var d = new Date(ms);
      var t = pad2n(d.getHours()) + ":" + pad2n(d.getMinutes()) + ":"
              + pad2n(d.getSeconds());
      if ((t1 - t0) <= 60000) {
        t += "." + Math.floor(d.getMilliseconds() / 100);
      }
      if (new Date(t0).toDateString() !== new Date(t1).toDateString()) {
        t = (d.getMonth() + 1) + "-" + pad2n(d.getDate()) + " " + t;
      }
      return t;
    }

    function chartDataSpan() {
      var t0 = Infinity, t1 = -Infinity;
      (chartSeries || []).forEach(function (s) {
        s.points.forEach(function (p) {
          if (p.t < t0) t0 = p.t;
          if (p.t > t1) t1 = p.t;
        });
      });
      if (!(t1 > t0)) { t1 = t0 + 1000; }
      return { t0: t0, t1: t1 };
    }

    /** 最小可缩视口：2s / 全程 1/500 / 2 个采样点距，三者取大。

     * [为什么要"2 个点距"] 抽样后点距可达数秒，固定 2s 下限会缩到一个
     * 采样点都没有的窗口（只剩跨窗直线，"看细节"变成看插值）。
     */
    function chartMinSpan() {
      var full = chartDataSpan();
      var fullSpan = Math.max(1000, full.t1 - full.t0);
      var minSpan = Math.max(2000, fullSpan / 500);
      var pts = (chartSeries && chartSeries.length) ? chartSeries[0].points : [];
      if (pts.length >= 2) {
        var gaps = [];
        for (var i = 1; i < pts.length && i < 50; i++) {
          gaps.push(pts[i].t - pts[i - 1].t);
        }
        gaps.sort(function (a, b) { return a - b; });
        var med = gaps[Math.floor(gaps.length / 2)] || 0;
        minSpan = Math.max(minSpan, med * 2);
      }
      return Math.min(minSpan, fullSpan);   /* 数据本身更短 ⇒ 不可缩 */
    }

    /** 当前视口（按数据范围夹取；夹到"接近全程"就视同全程）。 */
    function chartViewSpan() {
      var full = chartDataSpan();
      if (!chartView) return full;
      var t0 = Math.max(full.t0, chartView.t0);
      var t1 = Math.min(full.t1, chartView.t1);
      var span = t1 - t0;
      /* [复审 2026-11-22] 数据范围变化（筛选/列勾选/裁剪）后旧视口可能
       * 已接近或窄于下限 ⇒ 统一回落到全程，避免「看的是全程却标着已缩
       * 放、复位按钮亮着」。 */
      if (span >= (full.t1 - full.t0) - 1 || span < chartMinSpan() * 0.999) {
        return full;
      }
      return { t0: t0, t1: t1 };
    }

    function chartSetView(t0, t1) {
      if (!chartSeries || !chartSeries.length) return;
      var full = chartDataSpan();
      var fullSpan = Math.max(1000, full.t1 - full.t0);
      var minSpan = chartMinSpan();
      var span = Math.max(minSpan, Math.min(fullSpan, t1 - t0));
      var lo = Math.max(full.t0, Math.min(full.t1 - span, t0));
      /* [复审对齐桌面端 zoom_at/pan_pixels] 视口一变，旧的固定读数就
       * 与当前画面无关了 ⇒ 清掉（否则气泡停在视口外/错位）。 */
      chartPickedT = null;
      chartHoverT = null;
      if (span >= fullSpan - 1) {
        chartView = null;             /* 回到全程 = 恢复跟随最新 */
      } else {
        chartView = { t0: lo, t1: lo + span };
      }
      chartResetBtn.disabled = !chartView;
      scheduleHistRedraw();
    }

    function chartResetView() {
      chartView = null;
      chartPickedT = null;
      chartHoverT = null;
      chartResetBtn.disabled = true;
      scheduleHistRedraw();
    }

    function scheduleHistRedraw() {
      if (chartRaf) return;
      chartRaf = requestAnimationFrame(function () {
        chartRaf = 0;
        if (!chartCanvas.isConnected) return;    /* 已离页 */
        if (chartSeries && chartSeries.length) paintHistChart();
      });
    }

    function drawHistChart(series) {
      chartSeries = series;
      paintHistChart();
    }

    /** 吸附：像素 (x,y) ⇒ 最近点的时刻（无点/超出容差/不在绘图区返回 null）。 */
    function chartNearestT(xPix, yPix) {
      var d = chartLastDraw;
      if (!d || !chartSeries) return null;
      /* [复审] 只看 x 会让标题/坐标轴上的点按也弹气泡 ⇒ 先限定绘图区 */
      if (xPix < d.mL || xPix > d.mL + d.pw) return null;
      if (yPix !== undefined && yPix !== null
          && (yPix < d.mT || yPix > d.mT + d.ph)) {
        return null;
      }
      var best = null;
      chartSeries.forEach(function (s) {
        s.points.forEach(function (p) {
          if (p.t < d.t0 || p.t > d.t1) return;
          var px = d.mL + d.pw * (p.t - d.t0) / (d.t1 - d.t0);
          var dx = Math.abs(px - xPix);
          if (!best || dx < best.dx) best = { dx: dx, t: p.t };
        });
      });
      if (!best || best.dx > 28 * d.k) return null;
      return best.t;
    }

    /** 某时刻各序列的值（容差内找不到该时刻的点就跳过）。 */
    function chartValuesAt(t) {
      var d = chartLastDraw;
      var out = [];
      if (!d || !chartSeries) return out;
      /* [复审 2026-11-22] 容差按像素换算（≈4px 的时间量）：原按跨度 1%
       * 算，大跨度下可达数分钟，会把很远的列值列进"该时刻"。 */
      var tol = Math.max(1, (d.t1 - d.t0) / Math.max(1, d.pw) * 4);
      chartSeries.forEach(function (s, si) {
        var hit = null, best = Infinity;
        s.points.forEach(function (p) {
          var dt = Math.abs(p.t - t);
          if (dt < best) { best = dt; hit = p.v; }
        });
        if (hit !== null && best <= tol) {
          out.push({ si: si, name: s.name, v: hit,
                     color: SERIES_COLORS[si % SERIES_COLORS.length] });
        }
      });
      return out;
    }

    function paintHistChart() {
      var holder = chartHolder;
      var w = holder.clientWidth || 600;
      var hgt = holder.clientHeight || 420;
      var dpr = window.devicePixelRatio || 1;
      chartCanvas.width = Math.max(1, Math.round(w * dpr));
      chartCanvas.height = Math.max(1, Math.round(hgt * dpr));
      chartCanvas.style.width = w + "px";
      chartCanvas.style.height = hgt + "px";
      var g = chartCanvas.getContext("2d");
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, hgt);

      var k = rootScale();
      var mL = 72 * k, mT = 66 * k, mR = 26 * k, mB = 46 * k;
      var pw = Math.max(10, w - mL - mR);
      var ph = Math.max(10, hgt - mT - mB);
      var grid = cssVar("--chart-grid");
      var axisC = cssVar("--chart-axis");
      var textColor = cssVar("--text");
      var series = chartSeries || [];
      var view = chartViewSpan();
      var t0 = view.t0, t1 = view.t1;

      /* 可见点：保留视口外**各一个邻居**，边缘线段才会连着画到框外。 */
      function visiblePoints(s) {
        var pts = s.points, out = [];
        for (var i = 0; i < pts.length; i++) {
          var p = pts[i];
          if (p.t < t0) { out = [p]; continue; }
          if (p.t > t1) { out.push(p); break; }
          out.push(p);
        }
        return out;
      }

      g.font = (12 * k) + "px sans-serif";
      g.textBaseline = "middle";

      /* 网格 + X 轴本地时钟 HH:MM:SS 5 档。 */
      g.strokeStyle = grid;
      g.fillStyle = axisC;
      g.lineWidth = 1;
      for (var i = 0; i <= 4; i++) {
        var fx = mL + (pw * i) / 4;
        g.beginPath();
        g.moveTo(fx, mT);
        g.lineTo(fx, mT + ph);
        g.stroke();
        var tv = t0 + ((t1 - t0) * i) / 4;
        var label = chartTickText(tv, t0, t1);
        g.textAlign = i === 0 ? "left" : (i === 4 ? "right" : "center");
        g.fillText(label, fx, mT + ph + 14 * k);
      }
      /* Y 轴按**首序列可见段**量程 5 档（参考口径：多序列各自归一化，
       * 图例看量程；缩放后纵轴随可见段自适应——这正是看细节要的）。 */
      var s0pts = series.length ? visiblePoints(series[0]) : [];
      /* [复审 2026-11-22] 量程严格按**视口内**点算：视口外邻居只用于
       * 线段连到框外，不能让它的极值主导纵轴（点稀时尤其明显）。 */
      var s0in = s0pts.filter(function (p) { return p.t >= t0 && p.t <= t1; });
      if (s0in.length) s0pts = s0in;
      var ymin = Infinity, ymax = -Infinity;
      s0pts.forEach(function (p) {
        if (p.v < ymin) ymin = p.v;
        if (p.v > ymax) ymax = p.v;
      });
      if (!(ymax > ymin)) { ymin -= 0.5; ymax += 0.5; }
      g.textAlign = "right";
      for (var j = 0; j <= 4; j++) {
        var fy = mT + (ph * (4 - j)) / 4;
        g.beginPath();
        g.moveTo(mL, fy);
        g.lineTo(mL + pw, fy);
        g.stroke();
        var yv = ymin + ((ymax - ymin) * j) / 4;
        g.fillText(trimNum(yv), mL - 6 * k, fy);
      }

      /* 序列线：各自按自己 min/max（可见段）归一化到绘图区。 */
      var seriesDraw = [];
      g.save();
      g.beginPath();
      g.rect(mL, mT, pw, ph);
      g.clip();
      series.forEach(function (s, si) {
        var pts = visiblePoints(s);
        var lo = Infinity, hi = -Infinity, nIn = 0;
        pts.forEach(function (p) {
          if (p.t < t0 || p.t > t1) return;   /* 量程只看视口内 */
          nIn++;
          if (p.v < lo) lo = p.v;
          if (p.v > hi) hi = p.v;
        });
        if (!nIn) {                            /* 视口内无点：退回邻居量程 */
          pts.forEach(function (p) {
            if (p.v < lo) lo = p.v;
            if (p.v > hi) hi = p.v;
          });
        }
        if (hi === lo) { lo -= 0.5; hi += 0.5; }
        seriesDraw.push({ s: s, si: si, lo: lo, hi: hi });
        g.strokeStyle = SERIES_COLORS[si % SERIES_COLORS.length];
        g.lineWidth = Math.max(1, 1.6 * k);
        g.beginPath();
        pts.forEach(function (p, pi) {
          var x = mL + pw * (p.t - t0) / (t1 - t0);
          var y = mT + ph * (1 - (p.v - lo) / (hi - lo));
          if (pi === 0) g.moveTo(x, y);
          else g.lineTo(x, y);
        });
        g.stroke();
      });
      g.restore();

      /* 标题 + 图例（名称 + 可见段 min~max）。 */
      g.fillStyle = textColor;
      g.textAlign = "left";
      g.font = "bold " + (13 * k) + "px sans-serif";
      g.fillText("历史数据折线（各序列独立归一化）", mL, mT - 44 * k);
      g.font = (12 * k) + "px sans-serif";
      var lx = mL;
      var ly = mT - 22 * k;
      seriesDraw.forEach(function (sd) {
        var text = sd.s.name + " [" + trimNum(sd.lo) + " ~ " +
                   trimNum(sd.hi) + "]";
        g.strokeStyle = SERIES_COLORS[sd.si % SERIES_COLORS.length];
        g.lineWidth = Math.max(2, 3 * k);
        g.beginPath();
        g.moveTo(lx, ly);
        g.lineTo(lx + 16 * k, ly);
        g.stroke();
        g.fillStyle = textColor;
        g.fillText(text, lx + 22 * k, ly);
        lx += 30 * k + g.measureText(text).width + 14 * k;
        if (lx > mL + pw - 120 * k) { lx = mL; ly += 18 * k; }
      });

      /* 视口接管提示（右上角）——按**实际**跨度判，不只看 chartView 标志 */
      var fullNow = chartDataSpan();
      var isZoomed = (t1 - t0) < (fullNow.t1 - fullNow.t0) - 1;
      chartResetBtn.disabled = !isZoomed;
      if (isZoomed) {
        g.fillStyle = axisC;
        g.textAlign = "right";
        g.font = (11 * k) + "px sans-serif";
        g.fillText("已缩放 · 双击或「复位视图」回全程",
                   mL + pw, mT + ph - 8 * k);
      }

      /* ---- 读数层：十字线 + 各序列圆点 + 气泡（原始值）---- */
      var readT = chartPickedT !== null ? chartPickedT : chartHoverT;
      /* 读数时刻若已不在视口内 ⇒ 不画、属性也标 none（否则"属性说
       * picked、画面什么都没有"）。 */
      if (readT !== null && (readT < t0 || readT > t1)) readT = null;
      if (readT !== null) {
        var cx = mL + pw * (readT - t0) / (t1 - t0);
        var vals = chartValuesAt(readT);
        g.save();
        g.strokeStyle = axisC;
        if (g.setLineDash) g.setLineDash([3 * k, 3 * k]);
        g.beginPath();
        g.moveTo(cx, mT);
        g.lineTo(cx, mT + ph);
        g.stroke();
        if (g.setLineDash) g.setLineDash([]);
        vals.forEach(function (v) {
          var sd = seriesDraw[v.si];        /* 用下标，不靠名字反查 */
          if (!sd) return;
          var y = mT + ph * (1 - (v.v - sd.lo) / (sd.hi - sd.lo));
          g.fillStyle = v.color;
          g.beginPath();
          g.arc(cx, y, Math.max(2, 3 * k), 0, Math.PI * 2);
          g.fill();
        });
        /* 气泡：完整时刻（跨天/跨年也没有歧义）+ 每序列原值 */
        var lines = [{ text: (HSF.fmtStamp ? HSF.fmtStamp(readT)
                                          : chartTickText(readT, t0, t1)),
                       head: true }];
        vals.forEach(function (v) {
          lines.push({ text: v.name + "：" + trimNum(v.v), color: v.color });
        });
        if (!vals.length) lines.push({ text: "该时刻附近无数据点" });
        g.font = (12 * k) + "px sans-serif";
        var bw = 0;
        lines.forEach(function (ln) {
          var tw = (ln.head ? 12 * k : 12 * k) + g.measureText(ln.text).width;
          if (tw > bw) bw = tw;
        });
        bw += 26 * k;
        var bh = (lines.length * 18 + 10) * k;
        var bx = cx + 12 * k;
        if (bx + bw > mL + pw - 4 * k) bx = cx - 12 * k - bw;
        bx = Math.max(mL + 4 * k, Math.min(mL + pw - bw - 4 * k, bx));
        var by = mT + 8 * k;
        g.fillStyle = cssVar("--panel-bg");
        g.strokeStyle = cssVar("--border-sub");
        g.lineWidth = 1;
        g.fillRect(bx, by, bw, bh);
        g.strokeRect(bx + 0.5, by + 0.5, bw - 1, bh - 1);
        var ty = by + 9 * k;
        lines.forEach(function (ln, li) {
          if (li > 0) ty += 18 * k;
          if (ln.color) {
            g.fillStyle = ln.color;
            g.fillRect(bx + 8 * k, ty + 4 * k, 8 * k, 2.5 * k);
          }
          g.fillStyle = textColor;
          g.textAlign = "left";
          g.fillText(ln.text, bx + 20 * k, ty + 6 * k);
        });
        g.restore();
      }

      chartLastDraw = { mL: mL, mT: mT, pw: pw, ph: ph, t0: t0, t1: t1,
                        k: k, w: w, h: hgt };
      /* [可测性] 交互状态落到 DOM 属性上，门禁/探针可按属性断言，不必
       * 反推像素（产品逻辑不读它们）。readT 已按视口过滤 ⇒ 属性与画面
       * 一致。 */
      chartCanvas.setAttribute("data-view", isZoomed ? "1" : "0");
      chartCanvas.setAttribute("data-read",
        readT !== null
          ? (chartPickedT !== null ? "picked" : "hover")
          : "none");
    }

    /* ---- 交互事件（触摸 + 鼠标）----
     * canvas 已由 CSS 给 touch-action:none ⇒ 单指拖动不会滚页面；
     * 双指捏合/滚轮只改时间轴视口；点按（位移 ≤4px）固定读数气泡。 */
    function chartLocalPos(ev) {
      var r = chartCanvas.getBoundingClientRect();
      return { x: ev.clientX - r.left, y: ev.clientY - r.top };
    }
    function chartPinchDist() {
      var ids = Object.keys(chartPointers);
      if (ids.length < 2) return 0;
      var a = chartPointers[ids[0]], b = chartPointers[ids[1]];
      var dx = a.x - b.x, dy = a.y - b.y;
      return Math.sqrt(dx * dx + dy * dy) || 1;
    }
    function chartPinchCenterX() {
      var ids = Object.keys(chartPointers);
      if (ids.length < 2) return 0;
      return (chartPointers[ids[0]].x + chartPointers[ids[1]].x) / 2;
    }
    function chartZoomAt(frac, factor) {
      var v = chartViewSpan();
      var span = v.t1 - v.t0;
      var anchor = v.t0 + span * frac;
      chartSetView(anchor - span * factor * frac,
                   anchor + span * factor * (1 - frac));
    }

    chartCanvas.addEventListener("pointerdown", function (ev) {
      if (ev.button !== 0) return;
      var p = chartLocalPos(ev);
      chartPointers[ev.pointerId] = p;
      chartCanvas.setPointerCapture && chartCanvas.setPointerCapture(ev.pointerId);
      chartDragged = false;
      if (Object.keys(chartPointers).length >= 2) {
        chartPinchPrev = chartPinchDist();
        chartDrag = null;
        ev.preventDefault();
        return;
      }
      var v = chartViewSpan();
      chartDrag = { x: p.x, t0: v.t0, t1: v.t1 };
      ev.preventDefault();
    });
    chartCanvas.addEventListener("pointermove", function (ev) {
      var p = chartLocalPos(ev);
      if (chartPointers[ev.pointerId]) chartPointers[ev.pointerId] = p;
      if (Object.keys(chartPointers).length >= 2) {
        var d = chartPinchDist();
        if (chartPinchPrev > 0 && d > 0) {
          var dd = chartLastDraw;
          var frac = dd
            ? Math.min(1, Math.max(0, (chartPinchCenterX() - dd.mL) /
                                 Math.max(1, dd.pw)))
            : 0.5;
          chartZoomAt(frac, chartPinchPrev / d);   /* 张开⇒放大 */
          chartPinchPrev = d;
        }
        return;
      }
      if (chartDrag) {
        var dx = p.x - chartDrag.x;
        /* [阈值对齐桌面端 TAP_SLOP=6px] 超过 6px 才算拖动（避免手抖把
         * 点按判成拖动、拖完又弹气泡）。 */
        if (Math.abs(dx) > 6) chartDragged = true;
        if (chartDragged) {
          var dd2 = chartLastDraw;
          var pw2 = dd2 ? dd2.pw : 600;
          var span2 = chartDrag.t1 - chartDrag.t0;
          var dt = -dx / Math.max(1, pw2) * span2;
          chartSetView(chartDrag.t0 + dt, chartDrag.t1 + dt);
        }
        return;
      }
      var t = chartNearestT(p.x, p.y);
      if (t !== chartHoverT) { chartHoverT = t; scheduleHistRedraw(); }
    });
    function chartEndPointer(ev, cancelled) {
      if (ev && ev.pointerId !== undefined) delete chartPointers[ev.pointerId];
      if (Object.keys(chartPointers).length >= 2) chartPinchPrev = chartPinchDist();
      else chartPinchPrev = 0;
      if (chartDrag && !cancelled && !chartDragged && ev) {
        var p = chartLocalPos(ev);
        var t = chartNearestT(p.x, p.y);
        chartPickedT = (t !== null && t === chartPickedT) ? null : t;
        scheduleHistRedraw();
      }
      chartDrag = null;
      chartDragged = false;
    }
    chartCanvas.addEventListener("pointerup", function (ev) {
      chartEndPointer(ev, false);
    });
    chartCanvas.addEventListener("pointercancel", function (ev) {
      chartEndPointer(ev, true);
    });
    chartCanvas.addEventListener("pointerleave", function (ev) {
      if (ev && ev.pointerId !== undefined && chartPointers[ev.pointerId]) {
        delete chartPointers[ev.pointerId];
        /* [复审] 剩余仍 ≥2 指时按现存指对重算（别一律清零 ⇒ 捏合停摆） */
        chartPinchPrev = (Object.keys(chartPointers).length >= 2)
          ? chartPinchDist() : 0;
        chartDrag = null;
        return;
      }
      if (chartHoverT !== null) { chartHoverT = null; scheduleHistRedraw(); }
    });
    chartCanvas.addEventListener("dblclick", function () { chartResetView(); });
    chartCanvas.addEventListener("wheel", function (ev) {
      var dd = chartLastDraw;
      if (!dd) return;
      var p = chartLocalPos(ev);
      var frac = Math.min(1, Math.max(0, (p.x - dd.mL) / Math.max(1, dd.pw)));
      var steps = -ev.deltaY / 120.0;
      if (Math.abs(steps) < 0.01) return;
      chartZoomAt(frac, Math.pow(0.8, steps));
      ev.preventDefault();
    }, { passive: false });
    chartResetBtn.addEventListener("click", function () { chartResetView(); });

    /* ==================================================================
     *          导出（按筛选全量；后端批取流，浏览器下载）
     * ================================================================== */

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

    var exporting = false;
    function doExport(fmt) {
      if (exporting) { HSF.statusMessage("正在导出…（请等当前导出结束）"); return; }
      if (!connected()) { HSF.statusMessage("导出失败：未连接服务端"); return; }
      exporting = true;
      /* [筛选条件定格] 导出发起那一刻的筛选/列/锚点全部冻结。 */
      var tr = serverTimeRange();
      var q = {
        format: fmt,
        beforeTs: windowEnd || Date.now(),
        kw: (tb.keywordEdit.value || "").trim(),
        kwCols: visibleColumns().join(","),
      };
      if (tr[0]) q.loTs = tr[0];
      if (tr[1]) q.hiTs = tr[1];
      HSF.statusMessage("正在导出…（按筛选全量逐批取数）");
      var token = HSF.api.getToken();
      /* [导出超时闸] 导出可能跑很久（分批取全量）⇒ 超时给足 120s；但
       * 必须有闸：网页服务器半开时 fetch 永远挂着 ⇒ 状态行停在「正在导出…」
       * 违反三态终态（超时终态由 HSF.fetchWithTimeout 抛 ⇒ 落到下面的
       * catch ⇒ 「导出失败：导出超时…」）。 */
      HSF.fetchWithTimeout("/api/history/export" + buildQs(q), {
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
        var filename = m ? m[1] : ("hsf_history." + fmt);
        return resp.blob().then(function (blob) { return { blob: blob, filename: filename }; });
      }).then(function (got) {
        exporting = false;
        /* [XLSX 空文件 = 失败] XLSX 的取数与写盘都在第一个字节发出之前
         * 完成（后端先写临时文件、再分块回读）⇒ 中途失败时一个字节都
         * 没发出来，浏览器拿到 0 字节文件。**不能**报"导出完成"（那是
         * 骗操作员：文件是空的/坏的）。 */
        if (fmt === "xlsx" && (!got.blob || got.blob.size === 0)) {
          HSF.statusMessage("导出失败：未取到数据（链路中断或上游拒绝），" +
                            "请重试");
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

    unsubs.push(store.on("link", function () {
      if (connected() && dataMode === "local") enterServerMode();
      else if (!connected() && dataMode === "server") fallBackLocal("未连接服务端");
    }));

    /* [2026-11-22 窄屏收窄] 16 列每列下限 72px ⇒ 手机上要横滚很远才能看全
     * （用户口径：移动端要能用）。窄屏（≤700）默认只勾 5 个关键列：
     * 时间/模式/搅拌状态/输出频率/浊度1 ⇒ 列宽下限合计约 700px（时间列
     * 要放完整时刻 ≈215px），375 宽下**仍需在表格容器内横滚约 1~2 屏**，
     * 但已从 16 列的 ~1.6k px 砍到不足一半；用户仍可在工具条的「列」里
     * 勾回任意列。放在首次取数之前 ⇒ 第一屏就是收窄后的列。
     * [注] setCheckedIndexes 会经 onChanged 触发一次**空表** rebuild +
     * 「显示 5/16 列」状态行（此时还没取数，代价可忽略）。 */
    if (HSF.isNarrow && HSF.isNarrow()) {
      tb.colCombo.setCheckedIndexes([0, 1, 2, 3, 6]);
    }

    /* 入页：立刻拉第一页服务端历史（用户提的原始需求）。 */
    enterServerMode();
    /* [当前视图要有按下态] 缺省就是表格视图，但不显式调一次的话
     * 「表格视图」按钮停在普通态，看不出当前在哪个视图。 */
    setView(0);

    /* [窗宽变化 → 重算列宽] 列宽按视口宽分配（参考端在 resizeEvent 里
     * 重算，page_base.py:395）；web 侧同理，窗宽变了要重新分配。 */
    var onResize = function () {
      rebuild();
      /* [图表也跟着重排] 原来的 resize 只重算表格列宽；图表视图下
       * canvas 尺寸不变会留白/糊掉（视口几何变了但没重画）。 */
      if (viewMode === 1 && chartSeries) {
        chartLastDraw = null;
        scheduleHistRedraw();
      }
    };
    window.addEventListener("resize", onResize);

    activeUnmount = function () {
      window.removeEventListener("resize", onResize);
      stopLive();
      timers.forEach(clearInterval);
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      tb.destroy();
    };
    void ctx;
  }
})();
