/* -*- coding: utf-8 -*-
 * 页 11 报警：设备/阀/自动运行的异常记录。
 *
 * [数据源 2026-11-17 对齐桌面客户端「服务端单一数据源」]
 *   报警历史一律向**服务端**要：XLink 0x011A 报警历史分页（web 后端
 *   /api/alarms/page 转发，与桌面客户端 pages_other.AlarmPage 同一份
 *   服务端 alarms 表——健康边沿 + 自动告警 + 阀告警 + 保护动作都由服务端
 *   记档）。此前网页端靠 hub 进程内存记边沿（AlarmLog），与桌面客户端
 *   各算各的 ⇒ 条数天然对不上（用户实报「web 端报警有数据，客户端报警
 *   视图没数据」）。
 *   [实时行 vs 服务端历史的合并策略：**不合并**] 服务端是唯一事实源：
 *   SSE 告警事件（alarm/health/valve）只当作「有新行」的信号，到达后
 *   触发一次**整表重取**（2s 节流 + 250ms 合并抖动），本地不做增量拼行。
 *   理由：① 增量拼行要按「设备+类型+状态」去重、还要处理「服务端已把
 *   旧行裁剪（ring/rotate）」的口径差，两套事实源并存必然漂移（桌面端
 *   同款教训）；② 报警行是边沿触发的低频数据，整表重取（≤4 批 × 50 行）
 *   开销可忽略。
 *   [老服务端兜底 fail-open] 上游没有 0x011A（旧 C 服务端）、链路未就绪
 *   （503）或超时 ⇒ /api/alarms/page 回 ok:false，本页回落 GET /api/alarms
 *   （hub 本地边沿存档）——保留旧行为，别让报警页变空白；后续每次刷新
 *   仍先试服务端，上游恢复/换新服务端后自动切回。
 *   · 4 列表：时间/设备/类型/状态（时间列 HH:MM:SS，与参考同口径）；
 *   · 工具条：类型下拉（**与参考同一套 10 档**：全部类型 + 8 类设备型别
 *     + 自动运行；参考 pages_other.py:4391 KIND_ITEMS）——类型列取设备
 *     型别（服务端行按 dev 键还原、本地行按 deviceKind）+ 起止时间窗
 *     + 列多选 + 关键字（设备/类型/状态文本包含）+ 筛选/清除筛选；
 *   · **客户端内部分页** 10/20/50：表格只铺当前页，筛选命中**全集**
 *     （_visible_records 同款）⇒ 统计/导出都按全集，不受当前页影响；
 *   · **滚动锁定**：锁定期新报警只入库不刷表，取消勾选补刷一次；
 *   · **无刷新按钮**（推送数据没有"重查"动作）、**无图表**（用户定案：
 *     日志/报警都不要图表）；
 *   · 导出（CSV / Excel）：PERM_DOWNLOAD 客户端闸；**主路走后端
 *     /api/alarm/export 批取流**（批 40、整轮钉同一锚点、按筛选全量，
 *     与日志页同款）；老服务端没有 0x011A ⇒ 后端回显式失败包络 ⇒ 回落
 *     本机边沿存档的 CSV 导出（utf-8 BOM + CRLF + 最小引号转义，与 C 端
 *     HsfExport 字节口径一致）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var COLUMNS = ["时间", "设备", "类型", "状态"];
  /* 类型下拉：**与参考端同一套 10 档**（全部类型 + 8 类设备型别 +
   * 自动运行）——参考 pages_other.py:4391 KIND_ITEMS（= model.KIND_TEXT
   * 的 8 类 + 「自动运行」）。此前 web 只有 4 档（按**记录来源**分：
   * 健康/电动球阀/自动运行），与参考端的**设备型别**口径不是一套 ⇒ 一旦
   * 真有报警，两端对不上。 */
  var KIND_ITEMS = [
    { key: "", name: "全部类型" },
    { key: "变频器", name: "变频器" },
    { key: "温湿度", name: "温湿度" },
    { key: "流量计", name: "流量计" },
    { key: "模拟量", name: "模拟量" },
    { key: "485总线", name: "485总线" },
    { key: "数字量", name: "数字量" },
    { key: "电动球阀组", name: "电动球阀组" },
    { key: "水质", name: "水质" },
    { key: "自动运行", name: "自动运行" },
  ];
  var PAGE_SIZES = [10, 20, 50];
  var ALARM_FETCH_LIMIT = 500;     /* 本地兜底：与后端 AlarmLog.MAX_ROWS 同量级 */
  /* ---- 服务端取数（0x011A，对齐桌面客户端 AlarmPage 同名口径）---- */
  var SERVER_PAGE_MAX = 50;        /* 0x011A 单批上限（服务端夹取，16KB 载荷反推） */
  var SERVER_MAX_ROWS = 200;       /* 取数上限（桌面 AlarmPage.MAX_ROWS 同值） */
  var SERVER_FETCH_INTERVAL_MS = 2000;  /* 服务端取数节拍（桌面 FETCH_INTERVAL_MS 同值） */
  /* 服务端行的 dev（设备类别键）→ 中文类别名（与 web 后端 model.KIND_TEXT /
   * client model.KIND_TEXT 逐项一致；缺 dev 或不认识 → 空串，调用方兜底）。 */
  var DEV_KIND_TEXT = {
    0: "变频器", 1: "温湿度", 2: "流量计", 3: "模拟量",
    4: "485总线", 5: "数字量", 6: "电动球阀组", 7: "水质",
    8: "相机",   /* [2026-11-27 工作流] 与三处 Python KIND_TEXT 对齐 */
  };
  /* 服务端报警行 kind → (类型列文案, 状态列文案)——与桌面客户端
   * pages_other.AlarmPage.SERVER_KIND_ROWS 逐项一致。 */
  var SERVER_KIND_ROWS = {
    auto: ["自动运行", "自动运行告警"],
    valve: ["电动球阀组", "阀告警"],
    flowmeter: ["流量计", "流量计告警"],
    device: ["设备故障", "设备故障"],
        analog: ["模拟量", "越限保护"],
        digital: ["数字量", "数字量告警"]
      };
  /* 列宽（rem@14 基准，参考端同宽视口逐像素实测：时间≈247 / 设备≈305 /
   * 类型≈247 / 状态≈252——四列均衡，参考 set_column_weights((3,4,3,3))）。 */
  var COL_W_REM = {
    "时间": 247 / 14, "设备": 305 / 14, "类型": 247 / 14, "状态": 252 / 14,
  };

  /* ================================================================== */
  var activeUnmount = null;

  HSF.router.register(11, {
    title: "报警",
    /* 阀告警走阀主题合帧（0x00010007）⇒ 本页补订它，alarm 事件才推得到。
     * 自动告警主题（0x0001000B）是**全局常驻**（hub.GLOBAL_TOPICS 无条件
     * 订阅 ⇒ 用户不在本页时告警照样落档），故本页不再列它——单一事实源。 */
    topics: [0x00010007],
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    /* ---- 数据与筛选状态 ---- */
    var records = [];                /* 当前数据源的原始行（最新在前）：
                                      * 服务端行 {ts,kind,source,text,dev} 或
                                      * 本地 AlarmLog 行（见 rowOf 的形状分派） */
    var kindKey = "";                /* ""=全部类型 */
    var loMs = 0, hiMs = 0;          /* 时间窗（<=0 不限） */
    var kw = "";                     /* 客户端关键字（空=不筛） */
    var pageSize = 20;               /* 缺省中间档 */
    var pageIndex = 0;
    var exporting = false;           /* 导出互斥（重复点击闸；同日志页） */
    var refetchTimer = null;
    /* ---- 数据源状态（服务端 0x011A 优先，失败回落本地边沿）---- */
    var usingLocal = false;          /* true=已回落本机边沿存档（本次提示已给） */
    var lastFetchMs = 0;             /* 上次取数时刻（服务端 2s 节流窗） */
    var trailingTimer = null;        /* 节流窗内的尾随补取（合并连发事件） */

    /* ---- 工具条（无折线列/双视图/刷新；有内部分页 + 滚动锁定）----
     * [折叠把手挂标题行右侧 2026-11-19] 与历史页同款（把手不再独占一行）。
     * 副标题 = 当前数据源指示（回落本地时改文案，见 fallBackToLocal）。 */
    var SOURCE_NOTE_SERVER = "报警历史（服务端 alarms 表 · 0x011A 转发）";
    var SOURCE_NOTE_LOCAL = "报警历史（本机边沿存档：服务端历史不可用）";
    var subEl = HSF.el("span.page-head-sub.caption",
                       { text: SOURCE_NOTE_SERVER });
    var foldHost = HSF.el("span.dt-fold-host");
    var head = HSF.el("div.data-page-head", null, [
      HSF.el("span.page-head-title", { text: "报警" }),
      subEl,
      foldHost,
    ]);
    var tb = HSF.dataToolbar({
      pageKey: "alarm",
      foldHost: foldHost,
      columns: COLUMNS,
      extraCombo: {
        label: "类型",
        items: KIND_ITEMS.map(function (it) { return it.name; }),
        values: KIND_ITEMS.map(function (it) { return it.key; }),
        tip: "只显示选中设备型别的报警（全部类型 = 不限）",
      },
      pageSizes: PAGE_SIZES,
      paging: true, refresh: false, chartView: false, freeze: true,
      exportBtn: true,
      handlers: {
        filter: applyFilter,
        clear: clearFilter,
        pageSize: onPageSize,
        prev: function () { prevPage(); },
        next: function () { nextPage(); },
        export: onExport,
        columns: onColumnsChanged,
        freeze: function (frozen) { if (!frozen) rebuild(); },
      },
    });
    /* [显示态=逻辑态] 缺省页大小是 20（上方 pageSize），不回填下拉的话
     * select 停在首个选项「10 条」——逻辑与显示自相矛盾（历史/日志页都有
     * 这步，唯独本页漏了）。 */
    tb.setPageSize(pageSize);

    /* ---- 数据表卡片（直接进页面布局，没有双视图栈）----
     * dt-fixed = table-layout:fixed + 每列显式宽（时间≈247/设备≈305/类型≈247/
     * 状态≈252，参考端同宽视口逐像素实测）——auto 布局下全局「首列大
     * min-width」会把余空按比例灌给首列，时间列被撑到 ≈465px 独大。 */
    var thead = HSF.el("thead");
    var tbody = HSF.el("tbody");
    var table = HSF.el("table.table.dt-table.dt-fixed", null, [thead, tbody]);
    var tableWrap = HSF.el("div.dt-table-wrap", null, [table]);
    var card = HSF.el("div.card.dt-fill", null, [tableWrap]);

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.data-page", null, [
      head,
      tb.root,
      card,
    ]));

    /* ==================================================================
     *   取数（服务端 0x011A 分批 + SSE 触发重取 + 老服务端本地兜底）
     * ================================================================== */

    function fetchAlarms() {
      /* [服务端取数节流，对齐桌面 AlarmPage.FETCH_INTERVAL_MS=2000]
       * health 事件按帧广播（不止边沿）⇒ 不节流会变成每秒一批请求；
       * 节流窗内的后续触发合并成一次尾随补取（「太密会把服务端打爆，
       * 太疏则新报警要等」——桌面同款注释）。 */
      var now = Date.now();
      var wait = lastFetchMs + SERVER_FETCH_INTERVAL_MS - now;
      if (wait > 0) {
        if (trailingTimer === null) {
          trailingTimer = setTimeout(function () {
            trailingTimer = null;
            fetchAlarms();
          }, wait);
        }
        return;
      }
      lastFetchMs = now;
      fetchServerRows(0, []);
    }

    /** 分批取服务端报警历史（单批 ≤50 行，累计 ≤SERVER_MAX_ROWS；
     *  参考 AlarmPage._fetch_server_rows 同款递归）。 */
    function fetchServerRows(offset, acc) {
      var remain = SERVER_MAX_ROWS - acc.length;
      var limit = Math.min(SERVER_PAGE_MAX, remain);
      if (limit <= 0) { settleServerRows(acc); return; }
      HSF.api.alarmsPage("?offset=" + offset + "&limit=" + limit)
        .then(function (doc) {
          if (!doc || !doc.ok) { fallBackToLocal(doc); return; }
          var rows = doc.rows || [];
          /* 服务端 ts DESC 取页、页内正序 ⇒ 每批反转后按批序拼接 =
           * 全局最新在前（桌面「每批反转」同款；同毫秒行后入库的排前）。
           * [实时行不并入] SSE 事件只触发重取（见头注），这里永远以
           * 服务端应答整表替换，不做增量拼行。 */
          for (var i = rows.length - 1; i >= 0; i--) {
            if (rows[i] && typeof rows[i] === "object") acc.push(rows[i]);
          }
          if (rows.length >= limit && acc.length < SERVER_MAX_ROWS) {
            fetchServerRows(offset + rows.length, acc);
          } else {
            settleServerRows(acc);
          }
        });
    }

    function settleServerRows(rows) {
      usingLocal = false;
      subEl.textContent = SOURCE_NOTE_SERVER;
      records = rows;
      if (!tb.frozen()) rebuild();   /* 锁定期新报警只入库不刷表 */
    }

    /** 老服务端兜底（fail-open）：回落本地 AlarmLog，保留旧行为、
     *  别让报警页变空白。源切换只提示一次；下次刷新仍先试服务端。 */
    function fallBackToLocal(doc) {
      if (!usingLocal) {
        usingLocal = true;
        subEl.textContent = SOURCE_NOTE_LOCAL;
        HSF.statusMessage("服务端报警历史不可用（" +
          ((doc && doc.err) || "无应答") + "），已改用本机边沿存档");
      }
      HSF.api.alarms("?limit=" + ALARM_FETCH_LIMIT).then(function (doc2) {
        if (!doc2 || !doc2.ok) return;
        records = (doc2.alarms || []).filter(function (r) {
          return r && typeof r === "object";
        });
        if (!tb.frozen()) rebuild();
      });
    }

    function refetchSoon() {
      if (refetchTimer !== null) return;     /* 合并一拍内的连发事件 */
      refetchTimer = setTimeout(function () {
        refetchTimer = null;
        fetchAlarms();
      }, 250);
    }

    /** 记录 → [时间, 设备, 类型, 状态] 文本（时间列 HH:MM:SS 同参考口径）。
     *  [两个数据源、两种行形状，按形状分派] 服务端行 {ts,kind,source,text,
     *  dev} 没有 name 键；本地 AlarmLog 行恒带 name（状态列文案，后端
     *  store.AlarmLog._append 保证）⇒ 以「有没有 name」分派映射。筛选/
     *  分页/导出只认映射后的展示行，不关心来源。 */
    function rowOf(rec) {
      if (rec && rec.name === undefined) return serverRowOf(rec);
      return localRowOf(rec);
    }

    /** 服务端一行（0x011A）→ 展示行（桌面 AlarmPage._server_row_to_record
     *  逐条同口径）：健康边沿行设备列=设备名、类型列=设备类别（dev 键
     *  还原）、状态列=健康档文案；自动/阀/流量计/设备故障行按
     *  SERVER_KIND_ROWS 还原，带 dev 时类型列显示**设备类别**（缺 dev 的
     *  老服务端保持类别兜底文案——设备类别不能撒谎）。 */
    function serverRowOf(row) {
      var ts = parseInt(row.ts, 10) || 0;
      var kind = String(row.kind || "");
      var source = String(row.source || "");
      var text = String(row.text || "");
      var devText = devKindText(row.dev);
      var name, kindText, state;
      if (kind === "health" || (!kind && source)) {
        kindText = devText || "设备异常";
        name = source;
        state = text;
      } else {
        var base = SERVER_KIND_ROWS[kind] || ["自动运行", "自动运行告警"];
        kindText = devText || base[0];
        state = base[1];
        /* [2026-11-25 设备故障状态列显示具体故障] device 行 state 原来写死
         * "设备故障" ⇒ 现场只看得到四个字，看不出是电机过载还是欠压（text
         * 里其实有"变频器故障：电机过载（已停止设备，需复位）"）。设备列
         * 已有 source、类型列已有 dev 类别，状态列显示 text 原文才能回答
         * "到底是什么故障"；与桌面 pages_other._server_row_to_record 同款。 */
        if (kind === "device" && text) state = text;
        /* 自动告警的「设备」列放告警原文（"现场启动按钮被拒——…"），
         * 阀/其余类别放来源名；与桌面客户端同款。 */
        name = (kind === "auto" || kind === "analog" || kind === "digital") ? text : (source || text);
      }
      return {
        tsMs: ts,
        kind: kindText,
        /* ts 缺失/为 0 显示 "--"（桌面 `if ts else ""` 同款，不显示 1970）。 */
        texts: [ts ? HSF.fmtHMS(ts) : "--", name || "--", kindText || "--",
                state || "--"],
      };
    }

    /** dev（设备类别键）→ 中文类别名；null/缺/不认识 → 空串。
     *  [注意顺序] 必须先排除 null 再转数字：Number(null)===0 会把
     *  「无类别」错映射成「变频器」。 */
    function devKindText(dev) {
      if (dev === null || dev === undefined) return "";
      var n = Number(dev);
      if (isNaN(n)) return "";
      return DEV_KIND_TEXT[n] || "";
    }

    /** 本地兜底行（AlarmLog）→ 展示行。
     *  [两列口径同参考端] 参考记的是 [时间, 设备名, 设备型别, 健康文案]
     *  （pages_other.py:4520 _record_alarm(dev.name, dev.kind_text(),
     *  dev.health_text())）⇒ 「类型」列 = 设备型别（8 类 + 自动运行），
     *  「状态」列 = 健康文案（正常/告警/数据过期/离线/故障）。web 后端把
     *  型别放在 deviceKind、状态文案放在 name。
     *  [kind 必须随行携带] 筛选在映射后的行上做——原始记录的 kind 丢了
     *  类型下拉就永远筛出 0 行（2026-11-13 实锅）。 */
    function localRowOf(rec) {
      var ts = parseInt(rec.ts, 10) || 0;
      var kindText = String(rec.deviceKind || "");
      if (!kindText) {
        /* 旧记录（没有 deviceKind 字段）按来源兜底，别显示空白。 */
        kindText = rec.kind === "auto" ? "自动运行"
          : (rec.kind === "valve" ? "电动球阀组" : "");
      }
      return {
        tsMs: ts,
        kind: kindText,
        texts: [HSF.fmtHMS(ts), String(rec.source || "--"),
                kindText || "--", String(rec.name || "--")],
      };
    }

    /* ==================================================================
     *                筛选（类型 + 时间窗 + 关键字）
     * ================================================================== */

    function rowVisible(rec) {
      if (kindKey && rec.kind !== kindKey) return false;
      if (loMs > 0 && rec.tsMs > 0 && rec.tsMs < loMs) return false;
      if (hiMs > 0 && rec.tsMs > hiMs) return false;
      if (kw) {
        var hit = false;
        for (var c = 1; c <= 3; c++) {
          if (c < rec.texts.length && rec.texts[c].indexOf(kw) >= 0) { hit = true; break; }
        }
        if (!hit) return false;
      }
      return true;
    }

    /** 当前筛选命中的**全集**记录（最新在前；铺表/导出/统计共用）。 */
    function visibleRecords() {
      return records.map(rowOf).filter(rowVisible);
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
      var sel = tb.extraSel.selectedIndex;
      kindKey = sel >= 0 ? String(tb.extraSel.options[sel].value) : "";
      loMs = win[0];
      hiMs = win[1];
      kw = (tb.keywordEdit.value || "").trim();
      pageIndex = 0;                /* 新筛选：回第 1 页 */
      rebuild();
    }

    function clearFilter() {
      /* 类型回全部、时间窗回数据全区间（回填实际区间——「看得见的就是
       * 全部」）、关键字清空、列回全勾。 */
      tb.extraSel.selectedIndex = 0;
      kindKey = "";
      kw = "";
      tb.keywordEdit.value = "";
      tb.colCombo.checkAll();       /* 触发 columnsChanged → 立即重铺 */
      loMs = 0;
      hiMs = 0;
      if (records.length) {
        var first = records[records.length - 1];
        var last = records[0];
        tb.startEdit.value = HSF.fmtStamp(parseInt(first.ts, 10) || 0);
        tb.endEdit.value = HSF.fmtStamp(parseInt(last.ts, 10) || 0);
      } else {
        tb.startEdit.value = "";
        tb.endEdit.value = "";
      }
      pageIndex = 0;
      rebuild();
    }

    /* ==================================================================
     *              客户端内部分页 + 铺表 / 行数统计
     * ================================================================== */

    function visibleColumns() {
      var cols = tb.colCombo.checkedIndexes().slice().sort(function (a, b) { return a - b; });
      if (!cols.length) return [0, 1, 2, 3];
      return cols;
    }

    function pageCount() {
      var total = visibleRecords().length;
      if (pageSize <= 0) return 1;
      return Math.max(1, Math.ceil(total / pageSize));
    }

    function prevPage() {
      if (pageIndex > 0) { pageIndex -= 1; rebuild(); }
    }

    function nextPage() {
      if (pageIndex + 1 < pageCount()) { pageIndex += 1; rebuild(); }
    }

    function onPageSize(size) {
      /* 改每页条数：回第 1 页（行号口径变了，停在原页号没意义）。 */
      if (size > 0) pageSize = size;
      pageIndex = 0;
      rebuild();
    }

    function onColumnsChanged() {
      var cols = visibleColumns();
      rebuild();
      HSF.statusMessage("显示 " + cols.length + "/" + COLUMNS.length + " 列：" +
                        cols.map(function (c) { return COLUMNS[c]; }).join("、"));
    }

    /** 把**当前页**那一段铺进表格（最新在上）并刷新统计/翻页钮。 */
    function rebuild() {
      var visible = visibleRecords();
      var total = visible.length;
      var pages = pageCount();
      if (pageIndex >= pages) pageIndex = pages - 1;
      var start = pageIndex * pageSize;
      var pageRows = visible.slice(start, start + pageSize);
      var cols = visibleColumns();
      fillTable(cols, pageRows);
      /* [统计口径对齐历史页] 总数按**筛选命中全集**，不受当前页影响。 */
      tb.setTotalLabel("共 " + total + " 条（本页 " + pageRows.length + "）");
      tb.setCountLabel("本页显示 " + pageRows.length + " 行");
      tb.setPageLabel("第 " + (pageIndex + 1) + "/" + pages + " 页");
      if (tb.btnPrev) tb.btnPrev.disabled = pageIndex <= 0;
      if (tb.btnNext) tb.btnNext.disabled = pageIndex + 1 >= pages;
    }

    function fillTable(cols, pageRows) {
      HSF.replaceChildren(thead);
      var hr = HSF.el("tr");
      cols.forEach(function (c) {
        var th = HSF.el("th", { text: COLUMNS[c] });
        if (COL_W_REM[COLUMNS[c]]) th.style.width = COL_W_REM[COLUMNS[c]] + "rem";
        hr.appendChild(th);
      });
      thead.appendChild(hr);
      HSF.replaceChildren(tbody);
      var frag = document.createDocumentFragment();
      (pageRows || []).forEach(function (rec) {
        var tr = HSF.el("tr");
        cols.forEach(function (c) {
          tr.appendChild(HSF.el("td", {
            text: c < rec.texts.length ? rec.texts[c] : "",
          }));
        });
        frag.appendChild(tr);
      });
      tbody.appendChild(frag);
      tableWrap.scrollTop = 0;
    }

    /* ==================================================================
     *        导出（CSV / Excel；需 Download 权限；按筛选全量）
     *
     * [主路 = 后端 /api/alarm/export] 服务端 alarms 表（0x011A，与本页
     * 同一份事实源）按筛选**全量**批取；两种形态 CSV / XLSX（XLSX 由
     * 后端流式写出，行数不限、事件循环不卡）。文案与历史页一致。
     * [兜底 = 本机边沿存档] 老服务端（C 端）没有 0x011A ⇒ 后端回**显式
     * 失败包络**（文案带「0x011A」）⇒ 此时回落本地 CSV 导出（保住老
     * 服务端下"还能导出"的能力；Excel 此时无源可导，如实报错）。
     * [筛选冻结] 类型/时间窗/关键字/列集合在发起那一刻定格（取数期间
     * 改筛选或翻页不影响这一轮导出）。
     * ================================================================== */

    function csvField(v) {
      var s = String(v == null ? "" : v);
      /* 最小引号转义（与 C 端 HsfExport 字节级兼容口径）：仅当含
       * " , CR LF 之一才加引号，内部 " 翻倍。 */
      if (/["\,\r\n]/.test(s)) s = '"' + s.replace(/"/g, '""') + '"';
      return s;
    }

    /** 拼查询串（跳过空值）。 */
    function buildQs(extra) {
      var parts = [];
      Object.keys(extra).forEach(function (k) {
        var v = extra[k];
        if (v === null || v === undefined || v === "") return;
        parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
      });
      return "?" + parts.join("&");
    }

    /** 触发浏览器下载（Blob → 临时 <a> →  revoke）。 */
    function saveBlob(blob, filename) {
      var url = URL.createObjectURL(blob);
      var a = HSF.el("a", { href: url, download: filename });
      document.body.appendChild(a);
      a.click();
      a.parentNode.removeChild(a);
      setTimeout(function () { URL.revokeObjectURL(url); }, 3000);
    }

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
      var rowsAll = visibleRecords();
      if (usingLocal && !rowsAll.length) {
        HSF.statusMessage("无可导出的数据");
        return;
      }
      exporting = true;
      /* [筛选条件定格] 类型/时间窗/关键字/可见列在发起那一刻冻结。 */
      var cols = visibleColumns();
      var q = {
        format: fmt,
        beforeTs: Date.now(),
        cols: cols.join(","),
      };
      if (loMs > 0) q.loTs = loMs;
      if (hiMs > 0) q.hiTs = hiMs;
      if (kindKey) q.kindText = kindKey;
      if (kw) q.kw = kw;
      HSF.statusMessage("正在导出…（按筛选全量逐批取数）");
      var token = HSF.api.getToken();
      /* [导出超时闸] 导出可能跑很久 ⇒ 超时给足 120s；但必须有闸（网页
       * 服务器半开 ⇒ fetch 永远挂着 ⇒ 状态行停在「正在导出…」违反三态
       * 终态）。超时由 HSF.fetchWithTimeout 抛 ⇒ 落下面的 catch。 */
      HSF.fetchWithTimeout("/api/alarm/export" + buildQs(q), {
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
        var filename = m ? m[1] : ("hsf_alarm." + fmt);
        return resp.blob().then(function (blob) { return { blob: blob, filename: filename }; });
      }).then(function (got) {
        exporting = false;
        /* [XLSX 空文件 = 失败] XLSX 的取数与写盘都在第一个字节发出之前
         * 完成（后端先写临时文件、再分块回读）⇒ 中途失败时一个字节都没
         * 发出来，浏览器拿到 0 字节文件。**不能**报"导出完成"。 */
        if (fmt === "xlsx" && (!got.blob || got.blob.size === 0)) {
          HSF.statusMessage("导出失败：未取到数据（链路中断或上游拒绝），请重试");
          return;
        }
        saveBlob(got.blob, got.filename);
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
        var msg = (err && err.message) ? err.message : String(err);
        /* [老服务端兜底] 后端明确说"没有 0x011A" ⇒ 回落本机边沿存档的
         * CSV 导出（Excel 此时无源可导，如实报错，不给空文件）。 */
        if (fmt === "csv" && msg.indexOf("0x011A") >= 0) {
          exportLocalCsv(rowsAll, cols);
          return;
        }
        HSF.statusMessage("导出失败：" + msg);
      });
    }

    /** 本机边沿存档的 CSV 导出（老服务端没有 0x011A 时的兜底）。 */
    function exportLocalCsv(rowsAll, cols) {
      if (!rowsAll.length) {
        HSF.statusMessage("无可导出的数据");
        return;
      }
      /* [全集 + 可见列] 表头与行内容用同一份列集合（界面看到哪些列就导哪些列）。 */
      var lines = [cols.map(function (c) { return csvField(COLUMNS[c]); }).join(",")];
      rowsAll.forEach(function (rec) {
        lines.push(cols.map(function (c) {
          return csvField(c < rec.texts.length ? rec.texts[c] : "");
        }).join(","));
      });
      var blob = new Blob(["\uFEFF" + lines.join("\r\n") + "\r\n"],
                          { type: "text/csv;charset=utf-8" });
      var d = new Date();
      function p2(n) { return (n < 10 ? "0" : "") + n; }
      var filename = "hsf_alarm_" + d.getFullYear() + p2(d.getMonth() + 1) +
        p2(d.getDate()) + "-" + p2(d.getHours()) + p2(d.getMinutes()) +
        p2(d.getSeconds()) + ".csv";
      saveBlob(blob, filename);
      HSF.statusMessage("已导出 " + rowsAll.length + " 条 → " + filename +
        "（本机边沿存档：服务端未提供报警历史查询 0x011A）");
    }

    /* ==================================================================
     *                          生命周期
     * ================================================================== */

    /* 新行信号：alarm（自动告警）+ health（健康边沿入库）+ valve（合帧告警）。
     * [只当触发器，不当数据] 三类事件都只触发一次重取（节流 + 合并抖动），
     * 重取永远以服务端应答整表替换——服务端是唯一事实源（见头注）。 */
    ["alarm", "health", "valve"].forEach(function (kind) {
      unsubs.push(store.on(kind, refetchSoon));
    });

    fetchAlarms();
    /* 表格只在「新数据入库 / 筛选 / 翻页 / 解锁」时重铺（参考同款：
     * 推送来的数据没有"重查"，也没有定时刷表——锁滚动时不打扰阅读）。 */

    activeUnmount = function () {
      timers.forEach(clearInterval);
      timers = [];
      if (refetchTimer !== null) { clearTimeout(refetchTimer); refetchTimer = null; }
      if (trailingTimer !== null) { clearTimeout(trailingTimer); trailingTimer = null; }
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      tb.destroy();
    };
    void ctx;
  }
})();
