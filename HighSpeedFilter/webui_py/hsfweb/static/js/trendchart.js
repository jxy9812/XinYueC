/* -*- coding: utf-8 -*-
 * 实时趋势图（变频器页 / 水质页 / 流量页共用）：滚动窗 + 视口归属仲裁。
 *
 * [对齐参考 pages_monitor.TrendChart 逐项移植]（唯一功能基准，QPainter →
 * Canvas 2D 同构）：
 *   · 每序列一个滚动窗（2026-11-24：60 点 → 18000 点，配合「范围」最长
 *     1 小时；新点进、最旧出）；
 *   · [2026-11-25 用户定案] Y 轴口径按页面数据特性选（构造第 2 参 axisMode）：
 *     shared（缺省，VFD/水质页）= 全部可见序列真实值共用 0~全局最大轴，
 *     Y 刻度是真值、各线高低可比；normalized（流量页）= 公共 0~1 相对轴，
 *     各线按自身视口 max 满幅（四条线 115 m³ vs 0.00 差 4 个量级，真轴会
 *     把三条压在底边）。图例逐条标真实 min~max；
 *   · [图例多选 2026-11-22] 卡片标题行的「图例」下拉（复用
 *     HSF.multiCheckCombo，与表格「列」多选同款）控制**显示哪些曲线**：
 *     隐藏的曲线不画线、不进图例、不参与悬停；Y 轴取**第一条可见且有
 *     视口内数据**的序列量程；隐藏序列的 pts **继续累积**（重新勾选后
 *     历史点仍在）。setVisibleKeys 供 trendCard 的 onChange 调用。
 *   · [服务端设备名跟随 2026-11-24 用户「水质界面的折线图图例名字没跟着
 *     改」] setSeriesName(key, name) 改序列显示名并同步图例下拉文本（与
 *     桌面端同款）；水质页首帧拿到「进水浊度仪/出水浊度仪」后调用。
 *   · X 轴 6 格、标签 = 本地时钟 HH:MM:SS（跨度 ≥10 分钟切 HH:MM，避免
 *     窄窗标签互相压住）；Y 轴按首条可见序列量程 5 档刻度；
 *   · 「范围」下拉 1/5/10/30/60 分钟（2026-11-24）+「刷新」下拉
 *     200/500/1000/2000ms 纯客户端节流（按数据流独立时钟，流量计与浊度仪
 *     独立推送，共用一个节拍会互相压掉）；
 *   · 拖动/滚轮缩放 = 视口接管（追点继续、窗口暂停右移）；右缘推回实时点
 *     （CATCHUP_MS 容差）自动交还；「跟随」按钮/双击恢复（跟随**不再**
 *     把窗宽打回默认——范围由下拉/缩放决定）；
 *   · 悬停气泡显示**该时刻全部可见序列真值**（时刻 + 每序列一行，同桌面
 *     端 hover_values 与历史折线图 tooltip_lines；2026-11-25 用户口径）；
 *   · 窗宽不随刷新率变（切档瞬间老点全挤左边像图坏了——既有实锅口径）。
 *
 * [配色纪律] 一律取 theme.css 的 chart 令牌（--chart-grid/--chart-axis/
 * --chart-takeover/--chart-s1..s8），canvas 里用 getComputedStyle 解析，
 * 不散写十六进制字面量。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var WINDOW_POINTS = 18000;      /* 滚动窗保留点数（1h @200ms，见 RANGE_CHOICES） */
  var WIN_DEF_MS = 60000;         /* 缺省视口窗宽（1 分钟） */
  var WIN_MIN_MS = 1000;
  var WIN_MAX_MS = 3600000;
  var RANGE_CHOICES = [
    [60000, "1 分钟"], [300000, "5 分钟"], [600000, "10 分钟"],
    [1800000, "30 分钟"], [3600000, "1 小时"],
  ];
  var CATCHUP_MS = 50;            /* 右缘容差（≈1px，恢复跟随判据） */
  var SAMPLE_CHOICES = [200, 500, 1000, 2000];
  var MARGIN_L = 62, MARGIN_T = 34, MARGIN_R = 12, MARGIN_B = 30;
  var PICK_TOLERANCE = 24;

  function colorVar(name, fallback) {
    var v = getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim();
    return v || (fallback || "#98A2AC");
  }

  /** Y 轴刻度文本：按量级选小数位（1440→"1440"、48.62→"48.6"）。 */
  function trendNum(value) {
    var v = parseFloat(value), a = Math.abs(v);
    if (a >= 100) return v.toFixed(0);
    if (a >= 10) return v.toFixed(1);
    return v.toFixed(2);
  }

  /** %.4g 同款（图例 min~max）。 */
  function g4(value) {
    var s = parseFloat(value).toPrecision(4);
    return String(parseFloat(s));
  }

  function hhmmss(tMs) {
    var d = new Date(tMs);
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" +
      pad(d.getSeconds());
  }

  /** HH:MM（长跨度刻度用；秒位恒 00 时省宽度，见 draw 的 xfmt）。 */
  function hhmm(tMs) {
    var d = new Date(tMs);
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return pad(d.getHours()) + ":" + pad(d.getMinutes());
  }

  /** 抽稀：把可见点抽到约 maxPoints 个（保留每桶首/末/最小/最大）。
   *  [对齐 _trend_thin] 最长 1h 窗 18000 点，只接不抽会让每帧画 1.8 万段
   *  折线；等距隔点又会丢尖峰（现场最关心的东西）⇒ 按桶留极值包络。 */
  function thinPoints(points, maxPoints) {
    var n = points.length;
    if (maxPoints < 4 || n <= maxPoints) return points;
    var bucket = Math.max(1, Math.floor(n / Math.max(1, Math.floor(maxPoints / 4))));
    var out = [];
    for (var start = 0; start < n; start += bucket) {
      var chunk = points.slice(start, start + bucket);
      if (!chunk.length) continue;
      var lo = chunk[0], hi = chunk[0];
      for (var i = 1; i < chunk.length; i++) {
        if (chunk[i][1] < lo[1]) lo = chunk[i];
        if (chunk[i][1] > hi[1]) hi = chunk[i];
      }
      var cand = [chunk[0], lo, hi, chunk[chunk.length - 1]];
      for (var j = 0; j < cand.length; j++) {
        var p = cand[j];
        if (out.length) {
          var last = out[out.length - 1];
          if (p[0] < last[0]) continue;              /* 保持时间升序 */
          if (p[0] === last[0] && p[1] === last[1]) continue;
        }
        out.push(p);
      }
    }
    return out;
  }

  /**
   * 建趋势图。series = [{key, name}]。
   * 返回 {el, acceptSample, append, setSampleMs, sampleMs, follow,
   *       isFollowing, destroy}。
   */
  function TrendChart(series, axisMode) {
    var wrap = document.createElement("div");
    wrap.className = "trend-chart";
    var canvas = document.createElement("canvas");
    wrap.appendChild(canvas);
    var ctx = canvas.getContext("2d");

    var colors = {
      grid: colorVar("--chart-grid", "#39414C"),
      axis: colorVar("--chart-axis", "#A9B2BC"),
      takeover: colorVar("--chart-takeover", "#F9AB00"),
      tipBg: colorVar("--panel-bg", "#23282F"),
      tipLine: colorVar("--border-sub", "#6A7585"),
      tipText: colorVar("--text", "#E8EAED"),
      plotBg: colorVar("--panel-bg", "#23282F"),
    };
    var seriesColors = [
      colorVar("--chart-s1", "#2E7CE8"), colorVar("--chart-s2", "#E8730C"),
      colorVar("--chart-s3", "#2E9E4F"), colorVar("--chart-s4", "#B33DC6"),
    ];
    var defs = (series || []).map(function (s, i) {
      return {
        key: s.key, name: s.name,
        color: seriesColors[i % seriesColors.length],
        visible: true,                            /* 图例勾选态（缺省全显示） */
        pts: [],                                  /* [(tMs, v)] 滚动窗 */
      };
    });
    var byKey = {};
    defs.forEach(function (d) { byKey[d.key] = d; });
    /* [2026-11-25 Y 轴口径] "shared"=全部可见序列真实值共用 0~全局最大轴
     * （VFD/水质页）；"normalized"=公共 0~1 相对轴、各线按自身视口 max
     * 满幅（流量页 115 m³ vs 0.00 差 4 个量级，真轴会把三条压在底边）。
     * 缺省 shared。 */
    var axisMode = axisMode === "normalized" ? "normalized" : "shared";

    /* ---- 图例可见集合 ----
     * [隐藏≠停止收点] 隐藏只影响**绘制**；append 照旧往 pts 里进点 ⇒
     * 重新勾上时历史窗口完整（用户口径）。
     * 空集合合法（chart 层不假设至少一条；约束在 UI 的 minChecked=1）。 */
    function visibleDefs() {
      return defs.filter(function (d) { return d.visible; });
    }

    function setVisibleKeys(keys) {
      var want = {};
      (keys || []).forEach(function (k) { want[String(k)] = true; });
      defs.forEach(function (d) { d.visible = !!want[d.key]; });
      requestDraw();
    }

    function visibleKeys() {
      return visibleDefs().map(function (d) { return d.key; });
    }

    var originMs = null;                          /* 时间轴 0 点（首点） */
    var winW = WIN_DEF_MS;
    var winLo = null, winHi = null;               /* 当前视口；null=无数据 */
    var userView = false;                         /* true=用户接管（停写） */
    var sampleMs = SAMPLE_CHOICES[0];
    var lastFedMs = {};                           /* stream → 上次收点时刻 */
    var hoverPos = null;                          /* {x, y} 或 null */
    var dragOrigin = null;                        /* {x, lo, hi} 或 null */
    var rafPending = false;

    function requestDraw() {
      if (rafPending) return;
      rafPending = true;
      (window.requestAnimationFrame || function (fn) { setTimeout(fn, 16); })(
        function () { rafPending = false; draw(); });
    }

    /* ---- 尺寸（DPR 适配；容器宽由布局给，高 CSS 固定） ---- */
    function resize() {
      var dpr = window.devicePixelRatio || 1;
      var w = wrap.clientWidth || 320;
      var h = wrap.clientHeight || 200;
      var bw = Math.round(w * dpr), bh = Math.round(h * dpr);
      if (canvas.width !== bw || canvas.height !== bh) {
        canvas.width = bw;
        canvas.height = bh;
        canvas.style.width = w + "px";
        canvas.style.height = h + "px";
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      return { w: w, h: h };
    }

    /* ---- 数据 ---- */
    function newestMs() {
      var newest = null;
      defs.forEach(function (d) {
        if (d.pts.length) {
          var t = d.pts[d.pts.length - 1][0];
          newest = (newest === null) ? t : Math.max(newest, t);
        }
      });
      return newest;
    }

    function followWindow(xNow) {
      if (xNow === null) return;
      var lo = xNow - winW;
      if (originMs !== null && lo < originMs) lo = originMs;
      winLo = lo;
      winHi = Math.max(lo + winW, xNow);
    }

    function maybeResumeFollow(newest) {
      if (userView && newest !== null && winHi !== null &&
          winHi >= newest - CATCHUP_MS) {
        userView = false;
      }
    }

    function append(key, tMs, value) {
      var d = byKey[key];
      if (!d || tMs === null || tMs === undefined ||
          value === null || value === undefined) return false;
      tMs = parseInt(tMs, 10);
      value = parseFloat(value);
      if (!isFinite(tMs) || !isFinite(value)) return false;
      d.pts.push([tMs, value]);
      if (d.pts.length > WINDOW_POINTS) d.pts.shift();
      if (originMs === null) originMs = tMs;
      var newest = newestMs();
      maybeResumeFollow(newest);
      if (!userView) followWindow(newest);
      requestDraw();
      return true;
    }

    /** 批量追点（历史回填用）：一次校验/裁剪/定窗，只触发一次重绘。
     *  [后台趋势 2026-11-24] 页面重新进入时把全局历史灌回曲线；逐点
     *  append 会触发 1.8 万次 requestDraw（虽被 rAF 合并，校验/定窗仍
     *  逐点跑），批量接口只做一遍。 */
    function appendPoints(key, points) {
      var d = byKey[key];
      if (!d || !points || !points.length) return 0;
      var added = 0;
      for (var i = 0; i < points.length; i++) {
        var t = parseInt(points[i][0], 10);
        var v = parseFloat(points[i][1]);
        if (!isFinite(t) || !isFinite(v)) continue;
        d.pts.push([t, v]);
        added++;
      }
      if (!added) return 0;
      if (d.pts.length > WINDOW_POINTS) {
        d.pts.splice(0, d.pts.length - WINDOW_POINTS);
      }
      if (originMs === null && d.pts.length) originMs = d.pts[0][0];
      var newest = newestMs();
      maybeResumeFollow(newest);
      if (!userView) followWindow(newest);
      requestDraw();
      return added;
    }

    /** 节拍闸：距上次收点不足 sample_ms 丢弃（stream 各自独立时钟）。 */
    function acceptSample(tMs, stream) {      if (sampleMs <= 0) return true;
      var t = parseInt(tMs, 10);
      if (!isFinite(t)) return false;
      var last = lastFedMs[stream || ""] || 0;
      if (last && t - last < sampleMs) return false;
      lastFedMs[stream || ""] = t;
      return true;
    }

    function setSampleMs(ms) {
      ms = parseInt(ms, 10);
      if (!isFinite(ms) || ms <= 0) ms = SAMPLE_CHOICES[0];
      if (ms !== sampleMs) {
        sampleMs = ms;
        requestDraw();
      }
    }

    function follow() {
      userView = false;
      /* [2026-11-24] 不再把窗宽打回 WIN_DEF_MS：窗宽由「范围」下拉/缩放
       * 决定，「跟随」只管右缘跟不跟（桌面端同口径）。 */
      followWindow(newestMs());
      requestDraw();
    }

    function isFollowing() { return !userView; }

    /** 设时间范围（毫秒）：窗宽生效 + 立即回到跟随（同桌面 set_range_ms）。 */
    function setRangeMs(ms) {
      ms = parseInt(ms, 10);
      if (!isFinite(ms)) return;
      winW = Math.max(WIN_MIN_MS, Math.min(WIN_MAX_MS, ms));
      userView = false;
      followWindow(newestMs());
      requestDraw();
    }

    function rangeMs() { return winW; }

    /** 改一条序列的显示名（图例/悬停用）；key 不存在或空名返回 false。
     *  通过 onNamesChanged 钩子同步 trendCard 的图例下拉（见该函数）。 */
    function setSeriesName(key, name) {
      name = String(name || "");
      if (!name) return false;
      var d = byKey[key];
      if (!d) return false;
      if (d.name === name) return true;
      d.name = name;
      if (api && typeof api.onNamesChanged === "function") {
        api.onNamesChanged(defs.map(function (x) { return x.name; }));
      }
      requestDraw();
      return true;
    }

    /* ---- 几何 ---- */
    function plotRect(w, h) {
      return { l: MARGIN_L, t: MARGIN_T, r: w - MARGIN_R, b: h - MARGIN_B };
    }

    function visibleScale(d) {
      if (winLo === null || winHi === null) return null;
      var vals = [];
      for (var i = 0; i < d.pts.length; i++) {
        var t = d.pts[i][0];
        if (t >= winLo && t <= winHi) vals.push(d.pts[i][1]);
      }
      if (!vals.length) return null;
      /* [2026-11-24] Y 轴从 0 到最大值等分（同桌面端 _visible_scale）：
       * 慢变量的 min~max+8% 刻度几乎不可分辨；负值兜底不切掉。 */
      var vmin = Math.min.apply(null, vals);
      var vmax = Math.max.apply(null, vals);
      var base = Math.min(0, vmin);
      if (vmax <= base) vmax = base + 1.0;
      return [base, vmax];
    }

    /** 整图 Y 轴量程（2026-11-25 用户定案，同桌面端 axis_scale）：
     *  shared = 全部可见序列真实值合并 → 0~全局最大（刻度是真值）；
     *  normalized = 公共 0~1 相对轴（各线按自身视口 max 满幅）。
     *  隐藏的序列不占轴；全部无点返回 null。 */
    function axisScale() {
      var scales = [];
      visibleDefs().forEach(function (d) {
        var sc = visibleScale(d);
        if (sc) scales.push(sc);
      });
      if (!scales.length) return null;
      if (axisMode === "normalized") return [0, 1];
      var base = Infinity, vmax = -Infinity;
      scales.forEach(function (s) {
        base = Math.min(base, s[0]);
        vmax = Math.max(vmax, s[1]);
      });
      return [base, vmax];
    }

    /** 某序列的值 → 绘图区 y（shared 用全局轴，normalized 用自身量程）。 */
    function seriesY(d, v, rect) {
      var scale = (axisMode === "shared") ? axisScale() : visibleScale(d);
      if (!scale) return null;
      return rect.b - (rect.b - rect.t) * (v - scale[0]) /
        ((scale[1] - scale[0]) || 1.0);
    }

    /** 该时刻**全部可见序列**的 [{name, value}]（无数据不出现，同桌面端
     *  hover_values / 历史折线图 tooltip_lines）。 */
    function hoverValues(tMs) {
      var out = [];
      visibleDefs().forEach(function (d) {
        for (var i = 0; i < d.pts.length; i++) {
          if (d.pts[i][0] === tMs) {
            out.push({ name: d.name, value: d.pts[i][1] });
            break;
          }
        }
      });
      return out;
    }

    /** 图例条目（**只含可见序列**）：{key, name, label, color}。
     *  label 带视口内 min~max；隐藏序列不在这里出现（图例与曲线同源）。 */
    function legendEntries() {
      return visibleDefs().map(function (d) {
        var label = d.name;
        if (winLo !== null && winHi !== null) {
          var vals = [];
          for (var i = 0; i < d.pts.length; i++) {
            if (d.pts[i][0] >= winLo && d.pts[i][0] <= winHi) {
              vals.push(d.pts[i][1]);
            }
          }
          if (vals.length) {
            label = d.name + " " +
              g4(Math.min.apply(null, vals)) + "~" +
              g4(Math.max.apply(null, vals));
          }
        }
        return { key: d.key, name: d.name, label: label, color: d.color };
      });
    }

    function xAt(tMs, rect) {
      if (winLo === null || winHi === null || winHi <= winLo) {
        return (rect.l + rect.r) / 2;
      }
      return rect.l + (rect.r - rect.l) * (tMs - winLo) / (winHi - winLo);
    }

    /* ---- 视口操作 ---- */
    function zoomAt(fraction, factor) {
      if (winLo === null || winHi === null) return false;
      if (!(fraction >= 0 && fraction <= 1) || factor <= 0) return false;
      var span = winHi - winLo;
      var newSpan = Math.min(WIN_MAX_MS, Math.max(WIN_MIN_MS, span * factor));
      if (Math.abs(newSpan - span) < 1e-6) return false;
      var anchor = winLo + span * fraction;
      var lo = anchor - newSpan * fraction;
      var hi = lo + newSpan;
      if (originMs !== null && lo < originMs) {
        lo = originMs;
        hi = originMs + newSpan;
      }
      winLo = lo; winHi = hi; winW = newSpan;
      userView = true;
      requestDraw();
      return true;
    }

    function panFrom(originLo, originHi, dxPixels, plotW) {
      var span = originHi - originLo;
      var delta = dxPixels / Math.max(1, plotW) * span;
      var lo = originLo - delta;
      var hi = lo + span;
      if (originMs !== null && lo < originMs) {
        lo = originMs;
        hi = originMs + span;
      }
      var newest = newestMs();
      if (newest !== null && hi > newest) {
        hi = newest;
        lo = newest - span;
        if (originMs !== null && lo < originMs) lo = originMs;
      }
      var changed = (lo !== winLo || hi !== winHi);
      winLo = lo; winHi = hi;
      if (changed) {
        userView = true;
        maybeResumeFollow(newest);
        requestDraw();
      }
      return changed;
    }

    /* ---- 悬停 ---- */
    function hoverAt(px, py, rect) {
      if (winLo === null || winHi === null) return null;
      if (px < rect.l - PICK_TOLERANCE || px > rect.r + PICK_TOLERANCE ||
          py < rect.t - 16 || py > rect.b + 16) return null;
      var best = null;
      defs.forEach(function (d) {
        if (!d.visible) return;                   /* 隐藏序列不参与悬停 */
        if (!visibleScale(d)) return;
        for (var i = 0; i < d.pts.length; i++) {
          var t = d.pts[i][0], v = d.pts[i][1];
          if (t < winLo || t > winHi) continue;
          var sx = xAt(t, rect);
          var dist = Math.abs(sx - px);
          if (dist > PICK_TOLERANCE) continue;
          var sy = seriesY(d, v, rect);
          if (sy === null) sy = rect.t;
          var score = [dist, Math.abs(sy - py)];
          if (best === null || score[0] < best.score[0] ||
              (score[0] === best.score[0] && score[1] < best.score[1])) {
            best = { score: score, def: d, t: t, v: v };
          }
        }
      });
      return best;
    }

    /* ---- 绘制 ---- */
    function draw() {
      var size = resize();
      var w = size.w, h = size.h;
      var rect = plotRect(w, h);
      var plotW = rect.r - rect.l, plotH = rect.b - rect.t;
      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = colors.plotBg;
      ctx.fillRect(rect.l, rect.t, plotW, plotH);
      if (plotW <= 40 || plotH <= 40) return;

      /* 横向网格（4 分） */
      ctx.strokeStyle = colors.grid;
      ctx.lineWidth = 1;
      for (var gi = 1; gi < 4; gi++) {
        var gy = Math.round(rect.t + plotH * gi / 4.0) + 0.5;
        ctx.beginPath();
        ctx.moveTo(rect.l, gy);
        ctx.lineTo(rect.r, gy);
        ctx.stroke();
      }

      if (winLo === null || winHi === null) {
        ctx.fillStyle = colors.axis;
        ctx.font = "12px " + canvasFont();
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText("等待数据…", rect.l + plotW / 2, rect.t + plotH / 2);
        return;
      }
      var span = (winHi - winLo) || 1.0;

      /* X 刻度：6 格，本地时钟 HH:MM:SS（≥10 分钟跨度切 HH:MM 省宽度） */
      ctx.font = "11px " + canvasFont();
      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      for (var xi = 0; xi < 6; xi++) {
        var frac = xi / 5.0;
        var x = Math.round(rect.l + plotW * frac) + 0.5;
        ctx.strokeStyle = colors.grid;
        ctx.beginPath();
        ctx.moveTo(x, rect.t);
        ctx.lineTo(x, rect.b);
        ctx.stroke();
        ctx.fillStyle = colors.axis;
        var tickMs = winLo + span * frac;
        ctx.fillText(span >= 600000 ? hhmm(tickMs) : hhmmss(tickMs),
                     x, rect.b + 6);
      }

      /* Y 刻度：按首条**可见**序列视口量程 5 档 */
      var yScale = axisScale();
      if (yScale) {
        ctx.textAlign = "right";
        ctx.textBaseline = "middle";
        for (var yi = 0; yi < 5; yi++) {
          var fy = yi / 4.0;
          var y = rect.b - plotH * fy;
          ctx.fillStyle = colors.axis;
          ctx.fillText(trendNum(yScale[0] + (yScale[1] - yScale[0]) * fy),
                       rect.l - 6, y);
        }
      }

      /* 各序列折线（只画可见序列；视口裁剪两侧各多取一点防断线）。
       * [2026-11-25] shared 按全局真值轴画，normalized 按自身量程满幅。 */
      visibleDefs().forEach(function (d) {
        var scale2 = (axisMode === "shared") ? axisScale() : visibleScale(d);
        if (!scale2) return;
        var vmin = scale2[0], vmax = scale2[1];
        var spanV = (vmax - vmin) || 1.0;
        var visible = [];
        for (var i = 0; i < d.pts.length; i++) {
          var t = d.pts[i][0], v = d.pts[i][1];
          if (t >= winLo && t <= winHi) {
            visible.push([t, v]);
          } else if (t < winLo) {
            if (i + 1 < d.pts.length && d.pts[i + 1][0] >= winLo) {
              visible.push([t, v]);
            }
          } else {
            if (visible.length) visible.push([t, v]);
            break;
          }
        }
        if (!visible.length) return;
        /* [抽稀 2026-11-24] 最长 1h 窗 = 18000 点；按画布宽度抽到约 2×宽，
         * 保留每桶首/末/极值（尖峰不丢），绘制量不再随窗长膨胀。 */
        visible = thinPoints(visible, Math.max(64, Math.round(plotW * 2)));
        ctx.strokeStyle = d.color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (var j = 0; j < visible.length; j++) {
          var px = xAt(visible[j][0], rect);
          var py = rect.b - plotH * (visible[j][1] - vmin) / spanV;
          if (j === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
        }
        ctx.stroke();
        /* [不画数据点圆点 2026-11-24 用户「点不要显示不好看」] 原来点少时
         * 每点画 3px 圆点，实际曲线看着像一串珠子；只留折线（悬停时的单点
         * 标记仍保留，见下方 hover）。 */
      });

      /* 图例（色块 + 名 + 视口内 min~max；只列可见序列，放不下截断不换行） */
      ctx.font = "11px " + canvasFont();
      ctx.textBaseline = "middle";
      var cursor = rect.l;
      legendEntries().forEach(function (e) {
        var width = ctx.measureText(e.label).width + 26;
        if (cursor + width > rect.r) return;      /* 放不下截断 */
        ctx.fillStyle = e.color;
        ctx.fillRect(cursor, rect.t - 18 + 5, 12, 4);
        ctx.fillStyle = colors.axis;
        ctx.textAlign = "left";
        ctx.fillText(e.label, cursor + 16, rect.t - 18 + 7);
        cursor += width;
      });

      /* 接管角标（琥珀；视口在用户手里时提示） */
      if (!isFollowing()) {
        ctx.fillStyle = colors.takeover;
        ctx.textAlign = "right";
        ctx.fillText("已暂停跟随（双击或点「跟随」恢复）",
                     rect.r, rect.t - 16 + 7);
      }

      /* 悬停气泡：时刻 + 该时刻全部可见序列真值（2026-11-25 用户口径） */
      if (hoverPos && !dragOrigin) {
        var hit = hoverAt(hoverPos.x, hoverPos.y, rect);
        if (hit) {
          var hx = xAt(hit.t, rect);
          var hy = seriesY(hit.def, hit.v, rect);
          if (hy === null) hy = rect.t;
          ctx.strokeStyle = hit.def.color;
          ctx.setLineDash([4, 3]);
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(hx, rect.t);
          ctx.lineTo(hx, rect.b);
          ctx.stroke();
          ctx.setLineDash([]);
          ctx.fillStyle = hit.def.color;
          ctx.beginPath();
          ctx.arc(hx, hy, 4, 0, Math.PI * 2);
          ctx.fill();

          var lines = [hhmmss(hit.t)];
          hoverValues(hit.t).forEach(function (e) {
            lines.push(e.name + " " + e.value.toFixed(2));
          });
          ctx.font = "12px " + canvasFont();
          var tw = 0;
          lines.forEach(function (s) {
            tw = Math.max(tw, ctx.measureText(s).width);
          });
          tw += 16;
          var th = lines.length * 14 + 10;
          var bx = hx + 12;
          if (bx + tw > rect.r) bx = hx - 12 - tw;
          var by = hy - th - 10;
          if (by < rect.t) by = hy + 12;
          bx = Math.max(rect.l, Math.min(bx, rect.r - tw));
          by = Math.max(rect.t, Math.min(by, rect.b - th));
          ctx.fillStyle = colors.tipBg;
          ctx.strokeStyle = colors.tipLine;
          ctx.beginPath();
          ctx.rect(bx, by, tw, th);
          ctx.fill();
          ctx.stroke();
          ctx.fillStyle = colors.tipText;
          ctx.textAlign = "left";
          ctx.textBaseline = "middle";
          for (var li = 0; li < lines.length; li++) {
            ctx.fillText(lines[li], bx + 8, by + 9 + li * 14);
          }
        }
      }
    }

    function canvasFont() {
      return '"Microsoft YaHei","Noto Sans CJK SC",sans-serif';
    }

    /* ---- 指针/滚轮交互 ---- */
    /* [2026-11-22 双指捏合] 桌面已有拖拽平移 + 滚轮缩放；触摸设备没有
     * wheel ⇒ 手机上只能平移不能缩放。这里维护活跃指针表：双指距离变化
     * ⇒ zoomAt（张开=放大，与滚轮「上滚=放大」同语义）。canvas 已有
     * touch-action:none ⇒ 手势不会被浏览器抢走。 */
    var pointers = {};
    var pinchPrevDist = 0;
    function twoPointerDist() {
      var ids = Object.keys(pointers);
      if (ids.length < 2) return 0;
      var a = pointers[ids[0]], b = pointers[ids[1]];
      var dx = a.x - b.x, dy = a.y - b.y;
      return Math.sqrt(dx * dx + dy * dy) || 1;
    }
    function twoPointerCenterX() {
      var ids = Object.keys(pointers);
      if (ids.length < 2) return 0;
      return (pointers[ids[0]].x + pointers[ids[1]].x) / 2;
    }
    function localPos(ev) {
      var r = canvas.getBoundingClientRect();
      return { x: ev.clientX - r.left, y: ev.clientY - r.top };
    }

    canvas.addEventListener("pointerdown", function (ev) {
      if (ev.button !== 0) return;
      var p = localPos(ev);
      pointers[ev.pointerId] = p;
      canvas.setPointerCapture && canvas.setPointerCapture(ev.pointerId);
      if (Object.keys(pointers).length >= 2) {
        pinchPrevDist = twoPointerDist();
        dragOrigin = null;           /* 双指期间不做平移 */
        ev.preventDefault();
        return;
      }
      dragOrigin = { x: p.x, lo: winLo, hi: winHi };
      ev.preventDefault();
    });
    canvas.addEventListener("pointermove", function (ev) {
      var p = localPos(ev);
      if (pointers[ev.pointerId]) pointers[ev.pointerId] = p;
      if (Object.keys(pointers).length >= 2) {
        var d = twoPointerDist();
        if (pinchPrevDist > 0) {
          var rect2 = plotRect(canvas.clientWidth || 320,
                               canvas.clientHeight || 200);
          var cx = twoPointerCenterX();
          var frac = Math.min(1, Math.max(0, (cx - rect2.l) /
                                       Math.max(1, rect2.r - rect2.l)));
          /* 张开(d 变大) ⇒ prev/d < 1 ⇒ span 缩小 ⇒ 放大 */
          zoomAt(frac, pinchPrevDist / d);
          pinchPrevDist = d;
        }
        return;
      }
      var rect = plotRect(canvas.clientWidth || 320,
                          canvas.clientHeight || 200);
      if (dragOrigin) {
        if (dragOrigin.lo !== null && dragOrigin.hi !== null) {
          panFrom(dragOrigin.lo, dragOrigin.hi, p.x - dragOrigin.x,
                  Math.max(1, rect.r - rect.l));
        }
        return;
      }
      hoverPos = p;
      requestDraw();
    });
    function endDrag(ev) {
      if (ev && ev.pointerId !== undefined) delete pointers[ev.pointerId];
      /* [2026-11-22 复审] 指针集合变了就无条件重算基准距离：3 指→2 指时
       * 若抬掉的恰是原来参与计算的两指之一，沿用旧距离会让下一帧跳变。 */
      if (Object.keys(pointers).length >= 2) pinchPrevDist = twoPointerDist();
      else pinchPrevDist = 0;
      dragOrigin = null;
      requestDraw();
    }
    canvas.addEventListener("pointerup", endDrag);
    canvas.addEventListener("pointercancel", endDrag);
    canvas.addEventListener("pointerleave", function (ev) {
      if (ev && ev.pointerId !== undefined && pointers[ev.pointerId]) {
        delete pointers[ev.pointerId];
        if (Object.keys(pointers).length < 2) pinchPrevDist = 0;
        /* [2026-11-22 复审] 早退也要清拖拽/tooltip：capture 失效时
         * pointerleave 可能在按下期间到达 ⇒ 不清会留下"已抬手仍在平移"。 */
        dragOrigin = null;
        hoverPos = null;
        requestDraw();
        return;
      }
      hoverPos = null;
      requestDraw();
    });
    canvas.addEventListener("dblclick", function () { follow(); });
    canvas.addEventListener("wheel", function (ev) {
      var rect = plotRect(canvas.clientWidth || 320,
                          canvas.clientHeight || 200);
      var p = localPos(ev);
      var frac = Math.min(1, Math.max(0, (p.x - rect.l) /
                                     Math.max(1, rect.r - rect.l)));
      var steps = -ev.deltaY / 120.0;
      if (Math.abs(steps) < 0.01) return;
      zoomAt(frac, Math.pow(0.8, steps));   /* 1 格 ≈ 1.25 倍 */
      ev.preventDefault();
    }, { passive: false });

    if (window.ResizeObserver) {
      var ro = new ResizeObserver(function () { requestDraw(); });
      ro.observe(wrap);
    }
    requestDraw();

    var api = {
      el: wrap,
      append: append,
      appendPoints: appendPoints,
      acceptSample: acceptSample,
      setSampleMs: setSampleMs,
      sampleMs: function () { return sampleMs; },
      SAMPLE_CHOICES: SAMPLE_CHOICES,
      setRangeMs: setRangeMs,
      rangeMs: rangeMs,
      RANGE_CHOICES: RANGE_CHOICES,
      setSeriesName: setSeriesName,
      /* trendCard 用它把改名同步回图例下拉（页面也可不接）。 */
      onNamesChanged: null,
      setVisibleKeys: setVisibleKeys,
      visibleKeys: visibleKeys,
      legendEntries: legendEntries,
      pointCount: function (key) {
        var d = byKey[key];
        return d ? d.pts.length : 0;
      },
      axisRange: axisScale,                       /* Y 轴量程（只读查询） */
      hoverValues: hoverValues,                   /* 该时刻全部可见序列（测试/断言） */
      axisMode: function () { return axisMode; },
      follow: follow,
      isFollowing: isFollowing,
      destroy: function () { /* 事件挂在 canvas 自身，随 DOM 移除一并回收 */ },
    };
    return api;
  }

  /**
   * 建「实时趋势」卡（参考 _trend_card 同版式）：提示 + 图例 + 范围 + 刷新 + 跟随。
   *
   * @param {Array}  series    [{key, name}] 序列定义
   * @param {string} prefsKey  本机刷新间隔存档键（各页共用一份偏好）
   * @param {number} defaultMs 本机**无存档**时的缺省刷新间隔（毫秒）；
   *                           缺省 = SAMPLE_CHOICES[0]（200ms）。水质/流量
   *                           两页传 2000（服务端遥测本就慢节拍，2s 足够）；
   *                           **已有存档优先于它**（用户手动选过就沿用）。
   * @param {string} axisMode   Y 轴口径 "shared"（缺省，真值共轴）或
   *                           "normalized"（流量页四条量程差 4 个量级）。
   */
  function trendCard(series, prefsKey, defaultMs, axisMode) {
    series = series || [];
    var hint = HSF.el("div.trend-hint.caption", { text: "" });
    var chart = TrendChart(series, axisMode);

    var fallbackMs = parseInt(defaultMs, 10);
    if (!(fallbackMs > 0)) fallbackMs = SAMPLE_CHOICES[0];

    /* ---- 时间范围（2026-11-24 用户「图表时间范围太少了添加下拉框」）----
     * 五档 1/5/10/30/60 分钟，本机存档三页共用一份（"我要看最近多久"是
     * 同一个使用偏好）。选档即回跟随（贴着最新数据看最近 N 分钟）。 */
    var rangeSel = HSF.el("select.input.trend-range");
    RANGE_CHOICES.forEach(function (pair) {
      var opt = HSF.el("option", { text: pair[1] });
      opt.value = String(pair[0]);
      rangeSel.appendChild(opt);
    });
    var rawRange = HSF.prefs.get("trendRangeMs", "");
    var savedRange = parseInt(rawRange, 10);
    if (!(savedRange > 0)) savedRange = WIN_DEF_MS;
    var rIdx = 0, rBest = Infinity;
    RANGE_CHOICES.forEach(function (pair, i) {
      var diff = Math.abs(pair[0] - savedRange);
      if (diff < rBest) { rBest = diff; rIdx = i; }
    });
    rangeSel.value = String(RANGE_CHOICES[rIdx][0]);
    chart.setRangeMs(RANGE_CHOICES[rIdx][0]);
    rangeSel.title = "X 轴显示的时间范围（选完自动回到跟随最新数据）\n" +
      "设置会保存到本机，重启后沿用";
    rangeSel.addEventListener("change", function () {
      var ms = parseInt(rangeSel.value, 10) || WIN_DEF_MS;
      chart.setRangeMs(ms);
      HSF.prefs.set("trendRangeMs", String(ms));
    });

    var sample = HSF.el("select.input.trend-sample");
    SAMPLE_CHOICES.forEach(function (ms) {
      var opt = HSF.el("option", { text: (ms / 1000.0).toFixed(1) + " s" });
      opt.value = String(ms);
      sample.appendChild(opt);
    });    /* 本地存档（两页共用一份偏好；不在档位里落最近档）。
     * [无存档用 defaultMs] 不能拿 SAMPLE_CHOICES[0] 当 prefs.get 的缺省值
     * ——那会让 defaultMs 永远失效（本机从没存过也读到 "200"）。 */
    var raw = HSF.prefs.get(prefsKey || "trendSampleMs", "");
    var saved = parseInt(raw, 10);
    if (!(saved > 0)) saved = fallbackMs;
    var idx = 0, bestDiff = Infinity;
    SAMPLE_CHOICES.forEach(function (ms, i) {
      var diff = Math.abs(ms - saved);
      if (diff < bestDiff) { bestDiff = diff; idx = i; }
    });
    sample.selectedIndex = idx;
    chart.setSampleMs(SAMPLE_CHOICES[idx]);
    sample.title = "曲线记录间隔（只影响本图密度；服务端采集/推送不变）\n" +
      "设置会保存到本机，重启后沿用";
    sample.addEventListener("change", function () {
      var ms = parseInt(sample.value, 10) || SAMPLE_CHOICES[0];
      chart.setSampleMs(ms);
      HSF.prefs.set(prefsKey || "trendSampleMs", String(ms));
    });

    var btn = HSF.el("button.btn", {
      type: "button", text: "跟随",
      title: "拖动/滚轮缩放会接管视口；点此恢复自动跟随最新数据",
      onclick: function () { chart.follow(); },
    });

    /* ---- 图例多选（与表格「列」多选同一个 HSF.multiCheckCombo） ----
     * [加载顺序] trendchart.js 先于 pages/data_toolbar.js 加载，但这里只
     * 在**页面 mount 时**执行（那时 data_toolbar 早已执行完）⇒ 直接调用。
     * [单序列不渲染] 只有一条曲线时下拉是死控件（勾掉就空图），不建。
     * [至少留一条] minChecked:1 ⇒ 取消最后一条被拒、勾选框回弹。 */
    var legend = null;
    if (series.length > 1) {
      legend = HSF.multiCheckCombo(series.map(function (s) { return s.name; }), {
        allText: "全部图例",
        initial: "all",
        minChecked: 1,
        tip: "勾选要显示的曲线（可多选；至少保留一条；点外面收起）",
        onChange: function () {
          var keys = [];
          legend.checkedIndexes().forEach(function (i) {
            if (series[i]) keys.push(series[i].key);
          });
          chart.setVisibleKeys(keys);
        },
      });
      legend.root.classList.add("trend-legend");
      /* 设备名改名（水质浊度仪）同步到图例下拉文本；勾选态/index 不变。 */
      chart.onNamesChanged = function (names) { legend.setLabels(names); };
    }

    var headKids = [
      HSF.el("span", { text: "实时趋势" }),
      hint,
      HSF.el("span.trend-spacer"),
    ];
    if (legend) {
      /* 包一层 group：窄屏媒体查询整组落到第二行（见 theme.css 的
       * .trend-legend-group），宽屏与「刷新」控件同排。 */
      headKids.push(HSF.el("span.trend-legend-group", null, [
        HSF.el("span.trend-refresh-label", { text: "图例" }),
        legend.root,
      ]));
    }
    headKids.push(HSF.el("span.trend-refresh-label", { text: "范围" }),
                  rangeSel,
                  HSF.el("span.trend-refresh-label", { text: "刷新" }),
                  sample, btn);
    var head = HSF.el("div.card-title.trend-head", null, headKids);
    var card = HSF.el("div.card.trend-card", null, [head, chart.el]);
    /* [调试/验收钩子] 卡片节点上挂图实例：无头浏览器验收要读图内状态
     * （可见集合/图例条目/Y 轴量程），不必给 window 加全局变量；节点随
     * 页面卸载一并回收。 */
    card.hsfTrendChart = chart;
    if (legend) {
      /* 页面卸载只调 trend.chart.destroy()（三页既有口径）⇒ 顺带收起图例
       * 弹层：它是挂在 document.body 上的，不随卡片 DOM 一起移除。 */
      var chartDestroy = chart.destroy;
      chart.destroy = function () {
        legend.destroy();
        chartDestroy();
      };
    }
    return { card: card, chart: chart, hint: hint, legend: legend,
             range: rangeSel };
  }

  HSF.TrendChart = TrendChart;
  HSF.trendCard = trendCard;
  /* [单一事实源] 趋势刷新间隔的档位：页 13「本地设置·数据刷新」卡的总入口下拉
   * 与页 5/6/18 趋势卡的「刷新」下拉共用这一份（页 13 只读，不另立一份）。 */
  HSF.trendSampleChoices = SAMPLE_CHOICES;
})();
