/* -*- coding: utf-8 -*-
 * 页 6 水质监测：电磁流量计 + 两台浊度仪 + 进水泵（一条工艺链一页看完）。
 *
 * [对齐参考 pages_monitor.WaterPage 逐项移植]：
 *   · 流量计卡「电磁流量计 · LD 系列」：瞬时流量/瞬时流速/正向累积/
 *     反向累积（单位随服务端单位码）+ 右下报警文案（中文：
 *     空管→上限→下限→系统，后端已拼好 alarmText；正常=绿、报警=红）；
 *   · 浊度监测卡：两台「浊度仪N」（名字取服务端下发），数值两位小数，
 *     ≥20 NTU 标红（仅显示口径，非报警判据）；带温度机型同行显示
 *     「水温 N.NN ℃」（缺键=无温度机型 ⇒ 不显示，不是 0℃）；
 *   · 进水泵卡：启动/停止（POST /api/pump/control，应答整帧回显落地）；
 *     状态文案 运行（输出已开）/停止（输出已关）/未就绪；**必须注明
 *     「仅有输出、无运行反馈」**；已在该状态的那一侧按钮置灰；
 *   · 实时趋势：瞬时流量 + 浊度1 + 浊度2 三序列；流量计与浊度**双流
 *     独立时钟**节流（两路设备独立推送，共用一个节拍会互相压掉）。
 *
 * [按侧择优] 后端 model 已按「优先 B 侧 + 兜底 A 侧」择优导出——前端
 * 只读择优结果（flowmeter.fresh=false ⇒ "未收到流量计数据…"）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var TURBIDITY_COUNT = 2;
  var TURBIDITY_WARN_NTU = 20.0;

  var C_TEXT = "var(--text)";
  var C_SUB = "var(--text-secondary)";
  var C_OK = "var(--ok)";
  var C_FAULT = "var(--fault)";
  var C_OFFLINE = "var(--offline)";
  var C_STOP = "var(--text-secondary)";   /* 停止态（参考 #37474F 灰蓝档） */

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  var activeUnmount = null;

  HSF.router.register(6, {
    title: "水质监测",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["6"]) || [];
      return t.length ? t : [0x00010004, 0x00010009, 0x0001000A];
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

    /* ---- 流量计卡 ---- */
    /* [字号 2026-11-14] 逐值照参考端 FlowMeter 页（pages_monitor.py:2018-
     * 2023）：主读数 28 / 流速 24 / 正向 24 / 反向 22。 */
    var flowBlocks = {
      flow: HSF.statBlock("瞬时流量", "m³/h", 28),
      speed: HSF.statBlock("瞬时流速", "m/s", 24),
      fwd: HSF.statBlock("正向累积", "m³", 24),
      rev: HSF.statBlock("反向累积", "m³", 22),
    };
    var flowAlarm = HSF.el("div.flow-alarm", { text: "--" });
    var flowRow = HSF.el("div.water-flow-grid", null, [
      flowBlocks.flow.root, flowBlocks.speed.root,
      flowBlocks.fwd.root, flowBlocks.rev.root, flowAlarm]);
    var flowNote = HSF.el("div.card-note.hidden");
    var flowCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "电磁流量计 · LD 系列" }),
      flowRow, flowNote]);

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
      { text: "与电磁流量计共用一路 485（站号 1 / 站号 3）；" +
              "超过 " + TURBIDITY_WARN_NTU.toFixed(0) + " NTU 标红。" });
    var turbCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "浊度监测" }),
      turbGrid, turbNote, turbHint]);

    /* ---- 进水泵卡 ---- */
    var pumpTitle = HSF.el("div.card-title", { text: "进水泵" });
    var btnOn = HSF.el("button.btn", {
      type: "button", text: "启动",
      onclick: function () { requestPump(true); },
    });
    var btnOff = HSF.el("button.btn", {
      type: "button", text: "停止",
      onclick: function () { requestPump(false); },
    });
    var pumpBtnRow = HSF.el("div.pump-btns", null, [btnOn, btnOff]);
    var pumpState = HSF.el("div.pump-state", { text: "--" });
    var pumpNote = HSF.el("div.caption",
      { text: "现场仅有接触器输出、无运行反馈点：此处显示的是" +
              "输出命令状态，不代表泵确实在转。" });
    var pumpCard = HSF.el("div.card", null, [
      pumpTitle, pumpBtnRow, pumpState, pumpNote]);

    /* ---- 趋势卡（流量/浊度两流独立时钟，节流偏好共用一份） ---- */
    var trend = HSF.trendCard([
      { key: "flow", name: "瞬时流量" },
      { key: "turb1", name: "浊度1 NTU" },
      { key: "turb2", name: "浊度2 NTU" },
    ], "trendSampleMs");
    var trendHint = trend.hint;

    var bottom = HSF.el("div.water-bottom", null, [turbCard, pumpCard]);
    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.water-page", null, [
      HSF.el("div.water-head", null, [
        HSF.el("span.page-head-title", { text: "水质监测" }),
        HSF.el("span.page-head-sub.caption",
               { text: "电磁流量计 · 浊度仪 ×2 · 进水泵" }),
      ]),
      flowCard, bottom, trend.card,
    ]));

    function connected() {
      return !!((store.get("link") || {}).ready);
    }
    function blockedReason() {
      return HSF.controlBlockedReason(
        ctx.canPerm(HSF.app.PERM.DEVICE_CONTROL));
    }

    /* ---- 进水泵控制（逻辑全在服务端；应答整帧回显落地） ---- */
    var pumpBusy = false;
    function requestPump(wantOn) {
      var reason = blockedReason();
      if (reason) {
        HSF.statusMessage("禁止操作：" + reason);
        return;
      }
      if (pumpBusy) return;
      pumpBusy = true;
      HSF.statusMessage("进水泵 → 正在" + (wantOn ? "启动" : "停止") + "…");
      HSF.api.pumpControl({ index: 0, on: !!wantOn }).then(function (doc) {
        pumpBusy = false;
        if (doc && doc.ok) {
          HSF.statusMessage("进水泵：" + (wantOn ? "已启动" : "已停止"));
        } else {
          HSF.statusMessage("进水泵操作失败：" +
            ((doc && doc.err) || "未知原因"));
        }
        renderAll();
      });
    }

    /* ==================================================================
     *                          渲染
     * ================================================================== */

    var lastFlowMs = 0;
    var lastTurbMs = 0;

    function renderFlow() {
      var st = store.get("flowmeter") || {};
      var units = { flow: "m³/h", speed: "m/s", fwd: "m³", rev: "m³" };
      if (!st.fresh) {
        Object.keys(flowBlocks).forEach(function (k) {
          flowBlocks[k].set("--", C_TEXT, units[k]);
        });
        setText(flowAlarm, "--");
        flowAlarm.style.color = C_SUB;
        setNote(flowNote, "未收到流量计数据（服务端未装配 / 从站失联）");
        return;
      }
      setNote(flowNote, "");
      flowBlocks.flow.set(typeof st.flow === "number"
        ? st.flow.toFixed(2) : "--", C_TEXT, st.flowUnitText || "?");
      flowBlocks.speed.set(typeof st.speed === "number"
        ? st.speed.toFixed(3) : "--", C_TEXT, "m/s");
      flowBlocks.fwd.set(typeof st.forwardTotal === "number"
        ? st.forwardTotal.toFixed(2) : "--", C_TEXT, st.totalUnitText || "?");
      flowBlocks.rev.set(typeof st.reverseTotal === "number"
        ? st.reverseTotal.toFixed(2) : "--", C_TEXT, st.totalUnitText || "?");
      var ok = (st.alarms || 0) === 0;
      setText(flowAlarm, ok ? "正常" : ("报警: " + (st.alarmText || "--")));
      flowAlarm.style.color = ok ? C_OK : C_FAULT;
    }

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

    function renderPump() {
      var st = store.get("pump") || {};
      var reason = blockedReason();
      var items = st.items || [];
      var item = items[0];
      if (item && item.name) setText(pumpTitle, item.name);
      var has = !!(st.fresh && st.count > 0);
      if (!has) {
        setText(pumpState, "--");
        pumpState.style.color = C_SUB;
        btnOn.disabled = true;
        btnOff.disabled = true;
        return;
      }
      var on = !!item.on;
      var ready = !!item.ready;
      var text, color;
      if (!ready) {
        text = "未就绪"; color = C_OFFLINE;
      } else if (on) {
        text = "运行（输出已开）"; color = C_OK;
      } else {
        text = "停止（输出已关）"; color = C_STOP;
      }
      setText(pumpState, text);
      pumpState.style.color = color;

      var can = ready && !reason && !pumpBusy;
      /* 已是该状态的那一侧置灰（服务端也幂等，但界面不给可点的假象）。 */
      btnOn.disabled = !can || on;
      btnOff.disabled = !can || !on;
    }

    function renderAll() {
      renderFlow();
      renderTurbidity();
      renderPump();
      trendFeed();
      setText(trendHint, trend.chart.isFollowing()
        ? "" : "视口已接管：追点继续、窗口暂停右移");
    }

    /** 流量/浊度两路独立推送，各自判重 + 各自节流时钟（"flow"/"turb"）。 */
    function trendFeed() {
      var fm = store.get("flowmeter") || {};
      if (fm.fresh && typeof fm.flow === "number" && fm.updatedMs &&
          fm.updatedMs !== lastFlowMs) {
        var fmMs = Date.now();          /* 数据点用墙上钟（见 trendchart 头注） */
        if (trend.chart.acceptSample(fmMs, "flow")) {
          lastFlowMs = fm.updatedMs;
          trend.chart.append("flow", fmMs, fm.flow);
        }
      }
      var tb = store.get("turbidity") || {};
      if (tb.fresh && tb.updatedMs && tb.updatedMs !== lastTurbMs) {
        var turbMs = Date.now();
        if (trend.chart.acceptSample(turbMs, "turb")) {
          var got = false;
          (tb.items || []).forEach(function (item, idx) {
            if (idx < 2 && typeof item.ntu === "number") {
              trend.chart.append("turb" + (idx + 1), turbMs, item.ntu);
              got = true;
            }
          });
          if (got) lastTurbMs = tb.updatedMs;
        }
      }
    }

    ["flowmeter", "turbidity", "pump"].forEach(function (kind) {
      unsubs.push(store.on(kind, renderAll));
    });
    unsubs.push(store.on("link", renderAll));
    unsubs.push(store.on("mode", renderAll));
    unsubs.push((function () {
      var f = function () { renderAll(); };
      window.addEventListener("resize", f);
      return function () { window.removeEventListener("resize", f); };
    })());

    /* 入页拉一次快照（不依赖周期推送的首帧时机）。 */
    if (connected()) {
      HSF.api.flowmeterSnapshot().then(function () { renderAll(); });
      HSF.api.turbiditySnapshot().then(function () { renderAll(); });
      HSF.api.pumpSnapshot().then(function () { renderAll(); });
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
