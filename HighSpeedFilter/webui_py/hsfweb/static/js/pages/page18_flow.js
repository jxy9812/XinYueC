/* -*- coding: utf-8 -*-
 * 页 18 流量：电磁流量计 + 进水泵控制 + 流量实时趋势。
 *
 * [2026-11-22 页 6 拆分] 原页 6「水质监测」一页看完流量计 + 浊度×2 +
 * 进水泵；按用户口径拆成：
 *   · 页 6「水质」= page06.js：浊度×2（含水温）+ 浊度趋势；
 *   · 页 18「流量」= 本文件：电磁流量计 + 进水泵 + 流量趋势。
 * 页号**追加末尾**（16 是 Web 设置保留槽位、17 是相机），绝不重排既有页。
 * **C 端未拆**（HsfPages.c 仍把三样放在一页）⇒ 跨端页数/页名差异是已知项。
 *
 * [对齐参考 pages_monitor.WaterPage 的流量计/泵段，逐项移植]：
 *   · 流量计卡「电磁流量计 · LD 系列」：瞬时流量/瞬时流速/正向累积/
 *     反向累积（单位随服务端单位码）+ 右下报警文案（中文：
 *     空管→上限→下限→系统，后端已拼好 alarmText；正常=绿、报警=红）；
 *   · 进水泵卡：启动/停止（POST /api/pump/control，应答整帧回显落地）；
 *     状态文案 运行（输出已开）/停止（输出已关）/未就绪；**必须注明
 *     「仅有输出、无运行反馈」**；已在该状态的那一侧按钮置灰；
 *   · 实时趋势：瞬时流量/瞬时流速/正向累积/反向累积**四序列**（同一拍
 *     一起喂点；「图例」多选控制显示哪些曲线，缺省全选）；刷新下拉缺省
 *     2.0s（本机有存档优先）。
 *
 * [按侧择优] 后端 model 已按「优先 B 侧 + 兜底 A 侧」择优导出——前端
 * 只读择优结果（flowmeter.fresh=false ⇒ "未收到流量计数据…"）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

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

  HSF.router.register(18, {
    title: "流量",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["18"]) || [];
      /* 缺省 = TOPIC_FLOWMETER_STATUS(0x00010004) + TOPIC_PUMP_STATUS(0x0001000A) */
      return t.length ? t : [0x00010004, 0x0001000A];
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

    /* ---- 趋势卡（流量计四序列；缺省 2s，本机有存档优先） ---- */
    var trend = HSF.trendCard([
      { key: "flow", name: "瞬时流量" },
      { key: "speed", name: "瞬时流速" },
      { key: "fwd", name: "正向累积" },
      { key: "rev", name: "反向累积" },
    ], "trendSampleMs", 2000, "normalized");
    /* [后台趋势 2026-11-24] 把离开本页期间继续采到的历史灌回来。 */
    HSF.trendHistory.seed(trend.chart, "flow");
    var trendHint = trend.hint;

    var bottom = HSF.el("div.water-bottom", null, [flowCard, pumpCard]);
    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.water-page", null, [
      HSF.el("div.water-head", null, [
        HSF.el("span.page-head-title", { text: "流量" }),
        HSF.el("span.page-head-sub.caption",
               { text: "电磁流量计 · 进水泵" }),
      ]),
      bottom, trend.card,
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

    function setNote(node, text) {
      text = String(text || "");
      if (node.textContent !== text) node.textContent = text;
      node.classList.toggle("hidden", !text);
    }

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
      renderPump();
      trendFeed();
      setText(trendHint, trend.chart.isFollowing()
        ? "" : "视口已接管：追点继续、窗口暂停右移");
    }

    /** 流量推送判重 + 节流（stream="flow"；四序列同一拍进图）。 */
    function trendFeed() {
      var fm = store.get("flowmeter") || {};
      if (fm.fresh && typeof fm.flow === "number" && fm.updatedMs &&
          fm.updatedMs !== lastFlowMs) {
        var fmMs = Date.now();          /* 数据点用墙上钟（见 trendchart 头注） */
        if (trend.chart.acceptSample(fmMs, "flow")) {
          lastFlowMs = fm.updatedMs;
          /* append 自行过滤 null/非数（缺字段的序列只是这一点没有）。 */
          trend.chart.append("flow", fmMs, fm.flow);
          trend.chart.append("speed", fmMs, fm.speed);
          trend.chart.append("fwd", fmMs, fm.forwardTotal);
          trend.chart.append("rev", fmMs, fm.reverseTotal);
        }
      }
    }

    ["flowmeter", "pump", "link", "mode"].forEach(function (kind) {
      unsubs.push(store.on(kind, renderAll));
    });
    unsubs.push((function () {
      var f = function () { renderAll(); };
      window.addEventListener("resize", f);
      return function () { window.removeEventListener("resize", f); };
    })());

    /* 入页拉一次快照（不依赖周期推送的首帧时机）。 */
    if (connected()) {
      HSF.api.flowmeterSnapshot().then(function () { renderAll(); });
      HSF.api.pumpSnapshot().then(function () { renderAll(); });
    }
    timers.push(setInterval(renderAll, 500));   /* 兜底重绘（同值早退） */

    renderAll();

    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      trend.chart.destroy();
    };
  }
})();
