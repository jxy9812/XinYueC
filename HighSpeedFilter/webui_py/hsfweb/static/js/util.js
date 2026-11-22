/* -*- coding: utf-8 -*-
 * DOM/格式化小工具（全前端共用；页面工匠也走这里，不各自造轮子）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /**
   * 建元素。tag 字符串或 "div.cls#id" 简写；attrs 常用：
   * text / html / title / value / placeholder / type / dataset。
   * children 数组（元素、字符串、null 均可）。
   */
  function el(tag, attrs, children) {
    var name = tag.split("#")[0].split(".")[0];
    var node = document.createElement(name || "div");
    var rest = tag.slice(name.length);
    if (rest.charAt(0) === ".") {
      rest.substring(1).split(".").forEach(function (cls) {
        if (cls) node.classList.add(cls);
      });
    }
    attrs = attrs || {};
    Object.keys(attrs).forEach(function (k) {
      var v = attrs[k];
      if (v === null || v === undefined) return;
      if (k === "text") node.textContent = String(v);
      else if (k === "html") node.innerHTML = v;      /* 仅限本仓库静态文案 */
      else if (k === "class") node.className = v;
      else if (k === "dataset") Object.keys(v).forEach(function (d) {
        node.dataset[d] = v[d];
      });
      else if (k === "style" && typeof v === "object") Object.assign(node.style, v);
      else if (k === "on" && typeof v === "object") Object.keys(v).forEach(function (ev) {
        node.addEventListener(ev, v[ev]);
      });
      else if (k.slice(0, 2) === "on" && typeof v === "function") {
        node.addEventListener(k.slice(2).toLowerCase(), v);
      }
      else node.setAttribute(k, String(v));
    });
    (children || []).forEach(function (c) {
      if (c === null || c === undefined) return;
      node.appendChild(typeof c === "string" ? document.createTextNode(c) : c);
    });
    return node;
  }

  /** 清空并替换子节点（页面 mount 的标准入口姿势）。 */
  function replaceChildren(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
    return node;
  }

  /** 数值 → 定字串（无效值 → "--"，不编造）。 */
  function fmt(value, digits) {
    if (value === null || value === undefined || value === "" ||
        typeof value !== "number" || !isFinite(value)) return "--";
    if (digits === undefined || digits === null) return String(value);
    return value.toFixed(digits);
  }

  /** 毫秒 → "N天 HH时 MM分 SS秒"（自动运行时长；参考客户端 ⑨ 同款）。 */
  function fmtDuration(ms) {
    if (typeof ms !== "number" || !isFinite(ms) || ms < 0) return "--";
    var s = Math.floor(ms / 1000);
    var days = Math.floor(s / 86400); s %= 86400;
    var h = Math.floor(s / 3600); s %= 3600;
    var m = Math.floor(s / 60); s %= 60;
    var dd = days > 999 ? "999+" : String(days);
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return dd + "天 " + pad(h) + "时 " + pad(m) + "分 " + pad(s) + "秒";
  }

  /* 事件总线：on(kind, fn) / emit(kind, data) / off(kind, fn)。
   *
   * [只读计数钩子 · 2026-11-20] 网页端此前**没有任何**「bus 上还挂着几个
   * 回调」的可观测点 ⇒ 泄漏门（verify/webui/webui_leak_gate.py）只能退化成
   * 数 window/document 上的原生监听器 ⇒ 页面 unmount **漏调 off()** 这类
   * 订阅泄漏**测不出来**（门自己的「诚实标注」里写着这条缺口）。
   * ⇒ ① 每个 bus 实例登记到 HSF._busInstances（只读登记表）；
   *   ② bus.count(kind) 返回该 kind 的回调数（不传 kind = 总数）；
   *   ③ bus.stats() 返回 {kind: 回调数} 快照（拷贝，改它不影响 bus）；
   *   ④ HSF.busStats() 汇总「每个 bus 的 kind→回调数」。
   * [只读纪律] **不提供** offAll / clear / reset 之类会改变现有行为的接口；
   *   on / emit / off 的行为与原来逐字不变（本钩子只是「多看一眼」）。 */
  var _busInstances = [];         /* HSF._busInstances：bus 实例只读登记表 */

  function makeBus() {
    var map = {};
    var bus = {
      on: function (kind, fn) {
        (map[kind] = map[kind] || []).push(fn);
        return function off() {            /* 返回退订函数 */
          var list = map[kind] || [];
          var i = list.indexOf(fn);
          if (i >= 0) list.splice(i, 1);
        };
      },
      emit: function (kind, data) {
        (map[kind] || []).slice().forEach(function (fn) {
          try { fn(data); } catch (err) { console.error("[bus]", kind, err); }
        });
      },
      /* ---- 只读观测（不改任何现有行为）---- */
      /** 某类事件的回调数；不传 kind = 全部回调总数。 */
      count: function (kind) {
        if (kind === undefined || kind === null) {
          var total = 0;
          for (var k in map) {
            if (Object.prototype.hasOwnProperty.call(map, k) && map[k]) {
              total += map[k].length;
            }
          }
          return total;
        }
        var list = map[kind];
        return list ? list.length : 0;
      },
      /** 回调数快照 {kind: n}（只读拷贝；改它不会影响 bus）。 */
      stats: function () {
        var out = {};
        for (var k2 in map) {
          if (!Object.prototype.hasOwnProperty.call(map, k2)) continue;
          if (!map[k2] || !map[k2].length) continue;
          out[k2] = map[k2].length;
        }
        return out;
      },
    };
    _busInstances.push(bus);
    return bus;
  }

  /** 全局汇总（只读）：{buses:[{index,total,kinds}], total}。
   *  形如 {buses:[{index:0,total:3,kinds:{telemetry:2,alarm:1}}], total:3}。
   *  [为什么给两份] 「按 bus 看」定位是哪条总线漏订阅；「total」做门禁判据。 */
  function busStats() {
    var buses = [];
    var total = 0;
    for (var i = 0; i < _busInstances.length; i++) {
      var kinds = _busInstances[i].stats();
      var t = 0;
      for (var k in kinds) {
        if (Object.prototype.hasOwnProperty.call(kinds, k)) t += kinds[k];
      }
      buses.push({ index: i, total: t, kinds: kinds });
      total += t;
    }
    return { buses: buses, total: total };
  }

  /** 防抖（输入类控件联动用）。 */
  function debounce(fn, waitMs) {
    var timer = null;
    return function () {
      var args = arguments, self = this;
      if (timer) clearTimeout(timer);
      timer = setTimeout(function () { timer = null; fn.apply(self, args); }, waitMs);
    };
  }

  /**
   * 弹出菜单（参考 QMenu 语义：点别处/再点一次收起）。
   * anchorEl：定位锚；items：[{text, onPick}]；dir："down"|"up"（状态栏贴
   * 窗底必须 "up"，往下弹会被裁——参考 widgets.py:637 同款）。
   */
  function popupMenu(anchorEl, items, dir) {
    var layer = document.getElementById("popup-layer");
    if (!layer) return;
    replaceChildren(layer);
    layer.classList.remove("hidden");

    var menu = el("div.popup-menu");
    items.forEach(function (it) {
      menu.appendChild(el("button.menu-item", {
        type: "button", text: it.text,
        onclick: function () { close(); if (it.onPick) it.onPick(); },
      }));
    });
    layer.appendChild(menu);

    function close() {
      layer.classList.add("hidden");
      replaceChildren(layer);
      document.removeEventListener("mousedown", onDoc, true);
      window.removeEventListener("resize", close);
    }
    function onDoc(ev) {
      if (!menu.contains(ev.target)) close();
    }

    /* 定位：先显示再量尺寸，按 dir 贴锚点。 */
    var r = anchorEl.getBoundingClientRect();
    var mw = menu.offsetWidth, mh = menu.offsetHeight;
    var left, top;
    if (isNarrow()) {
      /* [窄屏全宽化] 弹层贴屏两边、宽度=视口-16px ⇒ 绝不溢出屏幕；
       * 纵向仍按 dir 贴锚点（状态栏的账户菜单要往上弹）。 */
      left = 8;
      menu.style.width = (window.innerWidth - 16) + "px";
      mw = menu.offsetWidth;
    } else {
      left = Math.max(4, Math.min(r.left, window.innerWidth - mw - 4));
    }
    if (dir === "up") {
      top = Math.max(4, r.top - mh - 4);
    } else {
      top = Math.min(window.innerHeight - mh - 4, r.bottom + 4);
    }
    menu.style.left = left + "px";
    menu.style.top = top + "px";

    /* 下一个宏任务再挂监听，避免当前 mousedown 冒泡立即关掉。 */
    setTimeout(function () {
      document.addEventListener("mousedown", onDoc, true);
      window.addEventListener("resize", close);
    }, 0);
  }

  /**
   * 控制闸（与参考 page_base.control_blocked_reason 同口径的**前端**判定，
   * 用于控件置灰；后端 auth.check_control_gate 仍终审）。
   *
   * :param canControl: 是否持有 PERM_DEVICE_CONTROL（HSF.app.canPerm）。
   * :returns: 空串 = 允许；否则是给用户看的原因（文案逐字对齐参考客户端）。
   */
  function controlBlockedReason(canControl) {
    var link = HSF.store ? (HSF.store.get("link") || {}) : {};
    if (!link.ready) return "未连接服务端";
    var m = HSF.store ? (HSF.store.get("mode") || {}) : {};
    if (m.known === false) return "运行模式未知（未连上服务端）";
    if (m.auto) {
      /* 措辞分两种（2026-11-12）：自动**模式**已切 ≠ 自动**运行**中；
       * 顺带点明「输出点由自动逻辑驱动」，免得以为 DO 不动是坏了。 */
      return m.autoRunning
        ? "自动运行中：远程控制已被禁用（输出点由自动逻辑驱动）"
        : "自动模式：远程控制已被禁用（输出点由自动逻辑驱动）";
    }
    if (!canControl) return "权限不足：控制需要操作者或管理员账户";
    return "";
  }

  var _measureCtx = null;
  /** 按指定 CSS font 实测文本宽（列宽/定宽一律实测，不写死像素）。 */
  function measureText(font, text) {
    if (!text) return 0;
    try {
      if (_measureCtx === null) {
        _measureCtx = document.createElement("canvas").getContext("2d");
      }
      _measureCtx.font = font;
      return Math.ceil(_measureCtx.measureText(String(text)).width);
    } catch (err) {
      return 0;
    }
  }

  /** %g 风格数值文本（去尾零；参考客户端 "%.3f    %g" 的 %g 段同款）。 */
  function fmtG(value) {
    if (typeof value !== "number" || !isFinite(value)) return "--";
    return String(parseFloat(value.toPrecision(6)));
  }

  /**
   * 自适应列数（参考 page_base.adaptive_columns 同款）：
   * 控件当前宽度 ÷ 每格最小宽（实测），夹在 [1, maxColumns]。
   * 宁可多几行也不裁字。
   */
  function adaptiveColumns(widthPx, maxColumns, cellMinW) {
    var w = Number(widthPx) || 0;
    if (w <= 0) return maxColumns;
    return Math.max(1, Math.min(maxColumns, Math.floor(w / Math.max(1, cellMinW))));
  }

  /**
   * 双列平衡分配：按**实测卡高**贪心，每张放进当前累计高较矮的一列。
   *
   * [为什么按高度不按奇偶/前缀切] 设置页卡片高度差很大（「自动模式配置」
   * 是「连接信息」的十几倍）：
   *   · 奇偶交替 ⇒ 两列高度互有胜负，末尾差一张卡；
   *   · 前缀/后缀切一刀 ⇒ 总量虽接近，但矮列会提前结束，用户滚到下半屏
   *     时半屏空白（2026-11-22 远设页实拍）。
   * 贪心分配把差值压在「最高一张卡」以内，两列基本同时结束。
   *
   * [为什么先全放左列再量] 列宽必须已经是最终宽（两列各一半）。若在单列
   * 宽度下量，换到半宽后文本换行变多、所有卡都会变高 ⇒ 分配失真。
   *
   * @param cards 原顺序卡片数组（函数会移动 DOM 节点）
   * @param left  左列容器
   * @param right 右列容器（与左列等宽）
   * @return {left,right} 两列累计高度（px；量不到为 0）
   */
  function balanceColumns(cards, left, right) {
    var list = cards || [];
    var i;
    if (!left || !right || !list.length) return { left: 0, right: 0 };
    for (i = 0; i < list.length; i++) left.appendChild(list[i]);
    var hs = list.map(function (c) {
      return c.getBoundingClientRect().height;
    });
    var lh = 0;
    var rh = 0;
    for (i = 0; i < list.length; i++) {
      if (lh <= rh) {
        left.appendChild(list[i]);
        lh += hs[i];
      } else {
        right.appendChild(list[i]);
        rh += hs[i];
      }
    }
    return { left: lh, right: rh };
  }

  /**
   * fetch + 超时闸（浏览器 fetch 没有内置超时）。
   *
   * [为什么要有] 网页服务器半开/假死时，await 会永远挂着 ⇒ 页面停在
   * 「正在导出…」违反「成功/失败/超时三选一终态」铁律。共享 api.js 的
   * request() 已给自己的 JSON 请求加了闸（api.js REQUEST_TIMEOUT_MS），
   * 这里补的是**不经 api.js 的原生 fetch**（导出走 blob 流、不能走 JSON
   * 封装）：超时报一个带终态文案的错，由调用方的 catch 落状态行。
   *
   * @param url     目标 URL
   * @param opts    fetch 的第二参（headers/cache/…）
   * @param ms      超时毫秒（导出可能跑很久 ⇒ 调用方给足）
   * @param what    超时文案里的动作名（"导出"）
   */
  function fetchWithTimeout(url, opts, ms, what) {
    var ctrl = null;
    var timer = null;
    if (typeof AbortController !== "undefined") {
      try { ctrl = new AbortController(); } catch (err) { ctrl = null; }
    }
    var o = opts || {};
    if (ctrl) o.signal = ctrl.signal;
    /* 与 fetch **赛跑**：到点先给终态（abort 只是顺手清理）——只靠 abort
     * 不够，fetch 未必理会 signal，那时 await 会永远挂着。 */
    var timeoutPromise = new Promise(function (_resolve, reject) {
      timer = setTimeout(function () {
        if (ctrl) { try { ctrl.abort(); } catch (err) { /* 忽略 */ } }
        reject(new Error((what || "请求") + "超时（" + Math.round(ms / 1000) +
                         " 秒无应答，请检查连接后重试）"));
      }, ms);
    });
    return Promise.race([fetch(url, o), timeoutPromise]).then(function (resp) {
      if (timer) clearTimeout(timer);
      return resp;
    }, function (err) {
      if (timer) clearTimeout(timer);
      throw err;
    });
  }

  /**
   * 数值块（参考 widgets.StatBlock：上灰标题 + 下大字读数 + 单位基线对齐）。
   * valuePx：读数字号（**字面 px**——参考端 value_label_qss 同口径，
   * 不随界面缩放变化）。set(text, color, unit)。
   */
  function statBlock(caption, unit, valuePx) {
    var cap = el("div.stat-caption", { text: caption });
    var value = el("div.stat-value", { text: "--" });
    value.style.fontSize = (valuePx || 22) + "px";
    var unitEl = el("span.stat-unit", { text: unit || "" });
    /* [单位同字号 2026-11-14] 参考端 StatBlock 把「数值 + 单位」合成一段
     * 富文本 ⇒ **同一行、同一基线、同一字号**；网页原先把单位写死 22px
     * ⇒ 比 16px 的数值还大 ⇒ 数值显得更小 ⇒ 单位跟数值走。 */
    unitEl.style.fontSize = (valuePx || 22) + "px";
    var root = el("div.stat", null, [cap,
      el("div.stat-row", null, [value, unitEl])]);
    return {
      root: root,
      set: function (text, color, newUnit) {
        text = String(text == null ? "" : text);
        if (value.textContent !== text) value.textContent = text;
        if (color) {
          if (value.dataset.c !== color) {
            value.dataset.c = color;
            value.style.color = color;
          }
        }
        if (newUnit !== undefined) {
          if (unitEl.textContent !== newUnit) unitEl.textContent = newUnit;
        }
      },
      setCaption: function (text) {
        text = String(text == null ? "" : text);
        if (cap.textContent !== text) cap.textContent = text;
      },
    };
  }

  /**
   * 定点格式化，**round-half-even**（与 Python 的 "%.Nf" 同口径）。
   * [为什么不用 toFixed] ECMAScript 的 toFixed 在「正好一半」时取**较大**
   * 那个（31.25 → "31.3"），而 Python 的 %格式化按 round-half-even 取偶数位
   * （31.25 → "31.2"）⇒ 同一个转速两端显示不同串（2026-11-13 实测：参考端
   * 「折算 ≈ 31.2 Hz」、网页端「31.3 Hz」）。
   * [只在正好一半时改判] 非中点的值 toFixed 与 Python 一致（两边都是
   * 「就近舍入」），所以只在中点改判，不影响其它数值。
   */
  function fmtFixedHalfEven(value, digits) {
    if (typeof value !== "number" || !isFinite(value)) return "";
    var neg = value < 0;
    var v = Math.abs(value);
    var f = Math.pow(10, digits);
    var scaled = v * f;
    var nearest = Math.round(scaled);
    var out;
    if (Math.abs(scaled - nearest) === 0.5) {
      var base = Math.floor(scaled);              /* 正好一半：取整数部分 */
      out = (base % 2 !== 0) ? base + 1 : base;    /* half-even：偶数位不加 */
    } else {
      out = nearest;
    }
    var s = (out / f).toFixed(digits);
    return (neg && parseFloat(s) !== 0) ? "-" + s : s;
  }

  /* ================================================================
   * 移动端断点（单一事实源；theme.css 的媒体查询同值，改一处两边一起改）
   *   DRAWER_MAX_W=860：导航树变抽屉 + 标题栏两行 + 状态栏瘦身
   *   NARROW_MAX_W=700：卡片网格单列 + 数据页横滚 + 触达目标 ≥44px
   * [为什么放 util] prefs.js 是「用户偏好」，断点是「视口事实」，两者分开；
   * util.js 是全前端共用的小工具层，装配顺序在 prefs 之后、页面之前。
   * ================================================================ */
  var DRAWER_MAX_W = 860;
  var NARROW_MAX_W = 700;

  /** 当前是否抽屉档（≤860px：导航树走抽屉，标题栏两行）。 */
  function isDrawer() {
    return window.innerWidth <= DRAWER_MAX_W;
  }

  /** 当前是否窄屏档（≤700px：卡片单列、表格横滚、触达目标放大）。 */
  function isNarrow() {
    return window.innerWidth <= NARROW_MAX_W;
  }

  HSF.el = el;
  HSF.replaceChildren = replaceChildren;
  HSF.fmt = fmt;
  HSF.fmtDuration = fmtDuration;
  HSF.makeBus = makeBus;
  HSF.busStats = busStats;
  /* [只读登记表] bus 实例清单（统计用；**不要**往里 push/改它）。 */
  HSF._busInstances = _busInstances;
  HSF.debounce = debounce;
  HSF.popupMenu = popupMenu;
  HSF.controlBlockedReason = controlBlockedReason;
  HSF.measureText = measureText;
  HSF.fmtG = fmtG;
  HSF.adaptiveColumns = adaptiveColumns;
  HSF.balanceColumns = balanceColumns;
  HSF.statBlock = statBlock;
  HSF.fmtFixedHalfEven = fmtFixedHalfEven;
  HSF.fetchWithTimeout = fetchWithTimeout;
  HSF.isDrawer = isDrawer;
  HSF.isNarrow = isNarrow;
  HSF.DRAWER_MAX_W = DRAWER_MAX_W;
  HSF.NARROW_MAX_W = NARROW_MAX_W;
})();
