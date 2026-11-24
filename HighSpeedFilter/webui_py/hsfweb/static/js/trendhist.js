/* -*- coding: utf-8 -*-
 * 全局趋势历史（后台采集）：**不在趋势页时也继续收点**。
 *
 * [用户口径 2026-11-24「这些折线图在后台也要实时获取数据」「不在这个界面
 *  的时候不要暂停数据采集」]
 *
 * [为什么不能放页面里] 网页切页 = 页面 unmount：页内定时器清掉、TrendChart
 * 销毁；采集逻辑挂在页面上，离开那一刻就断，回来曲线从空白重来。所以：
 *   ① 三个趋势主题（变频器/浊度/流量计）用 `store.addExtraTopic` **常驻
 *      订阅**（不随切页失效；断线重连由 store 重发主题意图）；
 *   ② 本模块在加载时注册 store 监听，把每次遥测按页采样进环形缓冲
 *      （与 TrendChart.WINDOW_POINTS 同为 18000 点）；
 *   ③ 页面 mount 时 `HSF.trendHistory.seed(chart, group)` 把历史灌回曲线。
 *
 * [采样口径] 只在数据新鲜时记录（与页面一致：过期数据不伪造"还在报数"）；
 * 同一 `updatedMs` 只记一次（快照 + 推送可能重复同一帧）。
 * 点的时间戳用 `Date.now()`（墙上钟，与页面 append 同口径）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* 与 TrendChart.WINDOW_POINTS 对齐（1h @ 5Hz ≈ 18000）。 */
  var CAP = 18000;

  /* 三个趋势页的主题（0x00010001 VFD / 0x00010009 浊度 / 0x00010004 流量计）
   * —— 常驻订阅；页面自己的 topics() 仍照旧（不影响页面数据路径）。 */
  var EXTRA_TOPICS = [0x00010001, 0x00010009, 0x00010004];

  /* group = 页面；key = 趋势序列 key（与各页 TREND_SERIES 一致）。 */
  var groups = {
    vfd: { out: [], set: [], cur: [] },
    water: { turb1: [], turb2: [], temp: [] },
    flow: { flow: [], speed: [], fwd: [], rev: [] },
  };
  /* 每组上次记录的源帧时刻（updatedMs），防同一帧重复入缓冲。 */
  var lastMs = { vfd: 0, water: 0, flow: 0 };

  function append(group, key, t, v) {
    var g = groups[group];
    if (!g) return;
    if (!g[key]) g[key] = [];
    var a = g[key];
    a.push([t, v]);
    if (a.length > CAP) a.splice(0, a.length - CAP);   /* 环形窗 */
  }

  function record(group, sourceMs, values) {
    if (!sourceMs || sourceMs === lastMs[group]) return;
    lastMs[group] = sourceMs;
    var t = Date.now();                 /* 与页面 append 的墙上钟一致 */
    Object.keys(values).forEach(function (key) {
      var v = values[key];
      if (typeof v === "number" && isFinite(v)) append(group, key, t, v);
    });
  }

  /* ---- 遥测 → 历史缓冲（字段口径照各页 trendFeed） ---- */
  HSF.store.on("vfd", function (d) {
    if (!d || !d.fresh) return;
    record("vfd", d.updatedMs, {
      out: d.outputFreqHz, set: d.setFreqHz, cur: d.outputCurrentA,
    });
  });

  HSF.store.on("turbidity", function (d) {
    if (!d || !d.fresh) return;
    var vals = {};
    (d.items || []).forEach(function (it, idx) {
      if (idx < 2 && it && it.online && typeof it.ntu === "number") {
        vals["turb" + (idx + 1)] = it.ntu;
      }
      /* 水温：取第一台带温度的（现场通常只有一台有机型）。 */
      if (!("temp" in vals) && it && it.hasTemp &&
          typeof it.temp === "number") {
        vals.temp = it.temp;
      }
    });
    record("water", d.updatedMs, vals);
  });

  HSF.store.on("flowmeter", function (d) {
    if (!d || !d.fresh || typeof d.flow !== "number") return;
    record("flow", d.updatedMs, {
      flow: d.flow, speed: d.speed,
      fwd: d.forwardTotal, rev: d.reverseTotal,
    });
  });

  /* 主题常驻：加载即登记（连接尚未建立也没关系，SSE open 时重发意图）。 */
  EXTRA_TOPICS.forEach(function (t) { HSF.store.addExtraTopic(t); });

  HSF.trendHistory = {
    groups: groups,

    /** 某序列的历史点（测试/诊断用）。 */
    points: function (group, key) {
      var g = groups[group];
      return (g && g[key]) ? g[key] : [];
    },

    /** 把整组历史灌回图表（页面 mount 时调；无历史 = 空操作）。 */
    seed: function (chart, group) {
      var g = groups[group];
      if (!g || !chart || typeof chart.appendPoints !== "function") return 0;
      var n = 0;
      Object.keys(g).forEach(function (key) {
        n += chart.appendPoints(key, g[key]);
      });
      return n;
    },

    /** 清空某组（测试用；产品不调）。 */
    clear: function (group) {
      if (!groups[group]) return;
      Object.keys(groups[group]).forEach(function (k) {
        groups[group][k] = [];
      });
      lastMs[group] = 0;
    },
  };
})();
