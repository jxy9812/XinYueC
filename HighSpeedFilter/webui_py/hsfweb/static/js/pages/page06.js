/* -*- coding: utf-8 -*-
 * 页 6 水质：两台浊度仪（进水/出水，含水温）+ 浊度实时趋势。
 *
 * [2026-11-22 页 6 拆分] 原「水质监测」把电磁流量计 + 浊度×2 + 进水泵
 * 塞在一页（一条工艺链一页看完）；按用户口径拆成两页：
 *   · 页 6「水质」= 本文件：浊度×2（带温度机型同行显示水温）+ 浊度趋势；
 *   · 页 18「流量」= page18_flow.js：电磁流量计 + 进水泵 + 流量趋势。
 * 页号只追加、不重排（16 是 Web 设置保留槽位、17 是相机）。**C 端未拆**。
 *
 * [对齐参考 pages_monitor.WaterPage 的浊度段]：
 *   · 两台「浊度仪N」（名字取服务端下发），数值两位小数，≥20 NTU 标红
 *     （仅显示口径，非报警判据）；
 *   · 带温度机型同行显示「水温 N.NN ℃」（缺键=无温度机型 ⇒ 不显示，不是
 *     0℃）；
 *   · 实时趋势：浊度1 + 浊度2 + 水温（℃）三序列；水温只取**带温度机型**
 *     的那台（缺键=无温度 ⇒ 该线无点，不是 0℃）；「图例」多选控制显示
 *     哪些曲线（缺省全选）。刷新下拉缺省 2.0s（本机有存档优先）。
 *
 * [按侧择优] 后端 model 已按「优先 B 侧 + 兜底 A 侧」择优导出——前端
 * 只读择优结果（turbidity.fresh=false ⇒ "未收到浊度仪数据…"）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var TURBIDITY_COUNT = 2;
  var TURBIDITY_WARN_NTU = 20.0;

  var C_TEXT = "var(--text)";
  var C_SUB = "var(--text-secondary)";
  var C_FAULT = "var(--fault)";
  var C_OFFLINE = "var(--offline)";

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  var activeUnmount = null;

  HSF.router.register(6, {
    title: "水质",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["6"]) || [];
      /* 缺省 = TOPIC_TURBIDITY_STATUS（0x00010009） */
      return t.length ? t : [0x00010009];
    },
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    /* ---- 浊度卡（每台一行：名字 + 大字值 + NTU + 水温） ---- */
    var turbRows = [];
    var turbGrid = HSF.el("div.turb-rows");
    for (var ti = 0; ti < TURBIDITY_COUNT; ti++) {
      var name = HSF.el("span.turb-name", { text: "浊度仪" + (ti + 1) });
      var value = HSF.el("span.turb-value", { text: "--" });
      var unit = HSF.el("span.turb-unit", { text: "NTU" });
      var temp = HSF.el("span.turb-temp");
      temp.style.display = "none";
      var row = HSF.el("div.turb-row", null, [name, value, unit, temp]);
      turbRows.push({ name: name, value: value, temp: temp });
      turbGrid.appendChild(row);
    }
    var turbNote = HSF.el("div.card-note.hidden");
    var turbHint = HSF.el("div.caption",
      { text: "进/出水两台浊度仪各按自己的周期采集；超过 " +
              TURBIDITY_WARN_NTU.toFixed(0) + " NTU 标红。" });
    var turbCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "浊度监测" }),
      turbGrid, turbNote, turbHint]);

    /* ---- 趋势卡（浊度两台 + 水温；缺省 2s，本机有存档优先） ---- */
    var trend = HSF.trendCard([
      { key: "turb1", name: "浊度1 NTU" },
      { key: "turb2", name: "浊度2 NTU" },
      { key: "temp", name: "水温 ℃" },
    ], "trendSampleMs", 2000);
    var trendHint = trend.hint;

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.water-page", null, [
      HSF.el("div.water-head", null, [
        HSF.el("span.page-head-title", { text: "水质" }),
        HSF.el("span.page-head-sub.caption",
               { text: "浊度仪 ×2 · 进水/出水（含水温）" }),
      ]),
      turbCard, trend.card,
    ]));

    function connected() {
      return !!((store.get("link") || {}).ready);
    }

    /* ==================================================================
     *                          渲染
     * ================================================================== */

    var lastTurbMs = 0;

    function setNote(node, text) {
      text = String(text || "");
      if (node.textContent !== text) node.textContent = text;
      node.classList.toggle("hidden", !text);
    }

    function renderTurbidity() {
      var st = store.get("turbidity") || {};
      var items = st.items || [];
      for (var i = 0; i < TURBIDITY_COUNT; i++) {
        var row = turbRows[i];
        var item = items[i];
        if (item && item.name) setText(row.name, item.name);
        var ntu = item ? item.ntu : null;
        if (ntu === null || ntu === undefined) {
          setText(row.value, "--");
          row.value.style.color = C_OFFLINE;
          row.temp.style.display = "none";
          continue;
        }
        setText(row.value, ntu.toFixed(2));
        row.value.style.color =
          ntu >= TURBIDITY_WARN_NTU ? C_FAULT : C_TEXT;
        /* 水温：缺值 = 这台表没有温度 ⇒ 不显示（不写 0℃）。 */
        if (item.hasTemp && typeof item.temp === "number") {
          setText(row.temp, "水温 " + item.temp.toFixed(2) + " ℃");
          row.temp.style.display = "";
        } else {
          row.temp.style.display = "none";
        }
      }
      if (!st.fresh) {
        setNote(turbNote, "未收到浊度仪数据（服务端未装配 / 从站失联）");
      } else if ((st.count || 0) === 0) {
        setNote(turbNote, "服务端未装配浊度仪");
      } else {
        setNote(turbNote, "");
      }
    }

    function renderAll() {
      renderTurbidity();
      trendFeed();
      setText(trendHint, trend.chart.isFollowing()
        ? "" : "视口已接管：追点继续、窗口暂停右移");
    }

    /** 浊度推送判重 + 节流（stream="turb"；三条序列同一时刻进图）。 */
    function trendFeed() {
      var tb = store.get("turbidity") || {};
      if (tb.fresh && tb.updatedMs && tb.updatedMs !== lastTurbMs) {
        var turbMs = Date.now();          /* 数据点用墙上钟（见 trendchart 头注） */
        if (trend.chart.acceptSample(turbMs, "turb")) {
          var got = false;
          (tb.items || []).forEach(function (item, idx) {
            if (idx < TURBIDITY_COUNT && typeof item.ntu === "number") {
              trend.chart.append("turb" + (idx + 1), turbMs, item.ntu);
              got = true;
            }
          });
          /* 水温：取**第一台带温度**的表（现场通常只有一台有机型；
           * hasTemp/temp 由 model.to_json 给出，缺键=无温度机型）。
           * 只有一条水温线 ⇒ 不按台拆序列（图例固定 3 项）。 */
          var tempItem = null;
          (tb.items || []).some(function (item) {
            if (item && item.hasTemp && typeof item.temp === "number") {
              tempItem = item;
              return true;
            }
            return false;
          });
          if (tempItem) {
            trend.chart.append("temp", turbMs, tempItem.temp);
            got = true;
          }
          if (got) lastTurbMs = tb.updatedMs;
        }
      }
    }

    ["turbidity", "link"].forEach(function (kind) {
      unsubs.push(store.on(kind, renderAll));
    });
    unsubs.push((function () {
      var f = function () { renderAll(); };
      window.addEventListener("resize", f);
      return function () { window.removeEventListener("resize", f); };
    })());

    /* 入页拉一次快照（不依赖周期推送的首帧时机）。 */
    if (connected()) {
      HSF.api.turbiditySnapshot().then(function () { renderAll(); });
    }
    timers.push(setInterval(renderAll, 500));   /* 兜底重绘（同值早退） */

    renderAll();
    void ctx;

    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      trend.chart.destroy();
    };
  }
})();
