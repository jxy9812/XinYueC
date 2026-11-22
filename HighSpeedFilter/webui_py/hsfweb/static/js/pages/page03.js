/* -*- coding: utf-8 -*-
 * 页 3 数字量：DI 只读灯阵 + DO 可点按钮阵（点数由服务端下发）。
 *
 * [对齐参考 pages_monitor.DigitalPage 逐项移植]：
 *   · 顶部模块下拉（台数=服务端推送的 dev 数）+「请求快照」按钮 +
 *     状态行「DI 0x%0*X · DO 0x%0*X」（>16 点用 8 位十六进制，后端已算好）；
 *   · DI 区「输入 DI（只读）」灯阵：圆点（通=绿/断=红）+ CH 号 + 语义名；
 *   · DO 区「输出 DO（点击切换）」按钮阵：「DO<n> 通/断」+ 语义名两行；
 *   · 点数按快照 count 重建（16/32；首帧未到保持 16 占位格，不清成 0）；
 *   · 名字三态保留：帧里**有** dinN/donN 键才整表替换，没有沿用缓存
 *     （model.py 同口径；hub 落模后前端只见合并结果，天然不抖）；
 *   · 名字随下发自适应列数：每格最小宽按**当前字体实测**最长语义名，
 *     窄屏自动折列，宁可多几行也不裁字（page_base.adaptive_columns 同款）。
 *
 * [动作三态] DO 写 POST /api/digital/control：被闸拒绝时回滚按钮显示态
 * （不允许的状态不能留在界面上骗人）+「禁止操作：原因」；成功/失败/网络
 * 异常都给终态文案（api.js 已把 fetch 异常归一为失败）。
 *
 * [权限置灰] 控制闸（未连接/自动模式两态/无设备控制位）→ DO 阵整组置灰；
 * 后端仍终审。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var PLACEHOLDER_COUNT = 16;      /* 首帧到达前的占位格数（真实点数见 count） */
  var MAX_COLUMNS = 8;             /* 宽屏 8 列（与 C 端一致），窄屏自适应折列 */
  /* 每格最小宽下限（px；语义名实测后可更大）。参考端同宽窗口每行 6 格
   * （CH0-5/CH6-11/CH12-15、DO0-5/DO6-11/DO12-15）——下限**照参考端的
   * CELL_MIN_W**（灯阵 108 / DO 阵 132，page_base.py:591/753），列数由
   * 「实测最长语义名 + 内边距 + 间距」决定（1280 窗/176 导航 ⇒ 阵宽
   * ≈1042，实测语义名 18px ⇒ 每格 ≈152 ⇒ 6 列）。 */
  var LAMP_CELL_MIN_W = 108;       /* 灯格最小宽下限（参考 page_base.LampGrid.CELL_MIN_W） */
  var DO_CELL_MIN_W = 132;         /* DO 格最小宽下限（参考 page_base.ToggleGrid.CELL_MIN_W） */
  var GRID_GAP = 8;                /* 列间距：算每格宽度时让出来，否则末格被挤 */

  /* 语义色（theme.css 令牌；灯点色义对齐 LampDot 纯色口径：
   * 通=#00A000/断=#D00000，走 --lamp-on/--lamp-off 随主题翻）。 */
  var C_LAMP_ON = "var(--lamp-on)";
  var C_LAMP_OFF = "var(--lamp-off)";
  var C_LAMP_NA = "var(--text-disabled)";

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  /* ======================================================================
   *                       灯阵 / 按钮阵（自适应列数）
   * ====================================================================== */

  /** DI 灯阵：每格 = 圆点 + CH 号 + 语义名（无名字隐藏该行）。
   *  cleanups：unmount 时统一摘除 window 监听（防跨页泄漏）。 */
  function makeLampGrid(cleanups) {
    var grid = HSF.el("div.lamp-grid");
    var cells = [];                /* [{root, dot, ch, alias}] */
    var names = {};                /* index → 语义名（列宽实测用） */
    var states = {};               /* index → bool */
    var cols = MAX_COLUMNS;

    function cell(i) {
      var dot = HSF.el("span.lamp-dot");
      var ch = HSF.el("div.lamp-ch", { text: "CH" + i });
      var alias = HSF.el("div.lamp-alias");
      alias.style.display = "none";
      var root = HSF.el("div.lamp-cell", null, [dot, ch, alias]);
      cells.push({ root: root, dot: dot, ch: ch, alias: alias });
      grid.appendChild(root);
    }

    function fontOf() {
      /* [量**语义名自己的**字体] 语义名走 caption 字号（18px），而 grid
       * 继承的是 app 字体（20px）——拿 grid 的字测量会把每格算宽 11%，
       * 列数从 6 掉到 5（2026-11-13 对照实拍：参考端 6 列/行）。 */
      for (var i = 0; i < cells.length; i++) {
        if (cells[i].alias.textContent) {
          return getComputedStyle(cells[i].alias).font;
        }
      }
      return getComputedStyle(grid).font;
    }

    function minCellW() {
      var need = 0;
      Object.keys(names).forEach(function (k) {
        var t = names[k];
        if (t) need = Math.max(need, HSF.measureText(fontOf(), t));
      });
      if (need <= 0) return LAMP_CELL_MIN_W;
      return Math.max(LAMP_CELL_MIN_W, need + 12 + GRID_GAP);
    }

    function reflow() {
      grid.style.gridTemplateColumns =
        "repeat(" + cols + ", minmax(0, 1fr))";
    }

    function adapt() {
      var c = HSF.adaptiveColumns(grid.clientWidth, MAX_COLUMNS, minCellW());
      if (c !== cols) { cols = c; reflow(); }
    }

    function setCount(count) {
      count = Math.max(0, count | 0);
      if (count === cells.length) return;
      HSF.replaceChildren(grid);
      cells = [];
      names = {};
      states = {};
      for (var i = 0; i < count; i++) cell(i);
      reflow();
      adapt();
    }

    function setState(index, on) {
      var c = cells[index];
      if (!c) return;
      on = !!on;
      if (states[index] === on && c.dot.dataset.on === String(on)) return;
      states[index] = on;
      c.dot.dataset.on = String(on);
      c.dot.style.background = on ? C_LAMP_ON : C_LAMP_OFF;
    }

    function setName(index, text) {
      var c = cells[index];
      if (!c) return;
      text = (text || "").trim();
      if (names[index] === text) return;      /* 名字没变：不动（每拍都来） */
      names[index] = text;
      setText(c.alias, text);
      c.alias.style.display = text ? "" : "none";
      adapt();                                /* 名字到达 ⇒ 必要时少排一列 */
    }

    if (cleanups) {
      cleanups.push(function () { window.removeEventListener("resize", adapt); });
    }
    window.addEventListener("resize", adapt);
    return {
      el: grid,
      setCount: setCount,
      setState: setState,
      setName: setName,
      adapt: adapt,
    };
  }

  /** DO 按钮阵：每格 = 「DO<n> 通/断」+ 语义名 两行按钮。 */
  function makeDoGrid(onToggle, cleanups) {
    var grid = HSF.el("div.do-grid");
    var buttons = [];              /* [{btn, alias}] */
    var aliases = {};
    var states = {};
    var cols = MAX_COLUMNS;

    function fontOf() {
      return buttons.length ? getComputedStyle(buttons[0].btn).font
                            : getComputedStyle(grid).font;
    }

    function minCellW() {
      var need = Math.max(HSF.measureText(fontOf(), "电动球阀0 控制"),
                          HSF.measureText(fontOf(), "启动按钮灯"));
      Object.keys(aliases).forEach(function (k) {
        if (aliases[k]) {
          need = Math.max(need, HSF.measureText(fontOf(), aliases[k]));
        }
      });
      return Math.max(DO_CELL_MIN_W, need + 26 + 6);
    }

    function reflow() {
      grid.style.gridTemplateColumns =
        "repeat(" + cols + ", minmax(0, 1fr))";
    }

    function adapt() {
      var c = HSF.adaptiveColumns(grid.clientWidth, MAX_COLUMNS, minCellW());
      if (c !== cols) { cols = c; reflow(); }
    }

    function render(i) {
      var b = buttons[i];
      if (!b) return;
      var on = !!states[i];
      var text = "DO" + i + (on ? " 通" : " 断");
      var alias = aliases[i] || "";
      var want = alias ? (text + "\n" + alias) : text;
      if (b.btn.textContent !== want) b.btn.textContent = want;
      /* 通态挂类 ⇒ CSS 着色（含禁用下仍可辨，对齐桌面端 disabled:checked）。 */
      b.btn.classList.toggle("on", on);
    }

    function setCount(count) {
      count = Math.max(0, count | 0);
      if (count === buttons.length) return;
      HSF.replaceChildren(grid);
      buttons = [];
      aliases = {};
      states = {};
      for (var i = 0; i < count; i++) {
        (function (idx) {
          var btn = HSF.el("button.do-toggle", {
            type: "button",
            onclick: function () {
              /* 点击即回调；显示态由数据面（应答/推送）统一回写。 */
              onToggle(idx, !states[idx]);
            },
          });
          buttons.push({ btn: btn });
          grid.appendChild(btn);
          render(idx);
        })(i);
      }
      reflow();
      adapt();
    }

    function setState(index, on) {
      if (!buttons[index]) return;
      on = !!on;
      if (states[index] !== on) {
        states[index] = on;
        render(index);
      }
    }

    function setName(index, text) {
      if (!buttons[index]) return;
      text = (text || "").trim();
      if (aliases[index] === text) return;
      aliases[index] = text;
      render(index);
      adapt();
    }

    function setEnabledAll(enabled) {
      buttons.forEach(function (b) { b.btn.disabled = !enabled; });
    }

    if (cleanups) {
      cleanups.push(function () { window.removeEventListener("resize", adapt); });
    }
    window.addEventListener("resize", adapt);
    return {
      el: grid,
      setCount: setCount,
      setState: setState,
      setName: setName,
      setEnabledAll: setEnabledAll,
      adapt: adapt,
    };
  }

  /* ======================================================================
   *                            页面模块
   * ====================================================================== */

  var activeUnmount = null;

  HSF.router.register(3, {
    title: "数字量",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["3"]) || [];
      return t.length ? t : [0x00010006];
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
    var currentDev = 0;
    var docs = {};                 /* devIndex → 该模块最新 JSON（单一事实源=store） */

    function digitalDocs() {
      var d = store.get("digital");
      return Array.isArray(d) ? d : (d ? [d] : []);
    }
    function collectDocs() {
      docs = {};
      digitalDocs().forEach(function (d) {
        if (d && typeof d.index === "number") docs[d.index] = d;
      });
    }

    /* ---- 顶部：模块选择 + 状态行 + 请求快照 ---- */
    var devSelect = HSF.el("select.input.dev-select");
    var stateLabel = HSF.el("span.caption.dev-state", { text: "--" });
    var snapBtn = HSF.el("button.btn", {
      type: "button", text: "请求快照",
      title: "现读一次数字量模块（同时刷新语义名与位图）",
      onclick: function () {
        if (!connected()) { HSF.statusMessage("未连接服务端"); return; }
        snapBtn.disabled = true;
        HSF.statusMessage("正在请求快照（模块 " + (currentDev + 1) + "）…");
        HSF.api.digitalSnapshot(currentDev).then(function (doc) {
          snapBtn.disabled = false;
          if (doc && doc.ok !== false) {
            HSF.statusMessage("快照已更新（模块 " + (currentDev + 1) + "）");
          } else {
            HSF.statusMessage("快照失败：" +
              ((doc && doc.err) || "服务端无应答"));
          }
          renderAll();            /* 应答已由 hub 落模并广播 digital 事件 */
        });
      },
    });
    /* [两行结构] 第一行=页头（标题+副题），第二行=模块行（下拉+状态+快照
     * 钮在右端）——对齐参考 DigitalPage 的 add_header 行 + 控制行两段式。 */
    var headRow = HSF.el("div.digital-top", null, [
      HSF.el("span.page-head-title", { text: "数字量" }),
      HSF.el("span.page-head-sub.caption", { text: "点数由服务端下发" }),
    ]);
    var topRow = HSF.el("div.digital-top2", null, [
      HSF.el("span", { text: "模块" }),
      devSelect,
      stateLabel,
      HSF.el("span.top-stretch"),
      snapBtn,
    ]);

    /* ---- DI / DO 两卡 ---- */
    var diLamp = makeLampGrid(unsubs);
    var diCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "输入 DI（只读）" }), diLamp.el]);

    var doGrid = makeDoGrid(onDoToggle, unsubs);
    var doHint = HSF.el("div.card-note", { text: "" });
    var doCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "输出 DO（点击切换）" }),
      doGrid.el, doHint]);

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.digital-page", null, [headRow, topRow, diCard, doCard]));

    function connected() {
      var link = store.get("link") || {};
      return !!link.ready;
    }

    /* ---- 模块下拉（服务端推几台就几台；名字后到只改文本不重挂） ---- */
    function syncDeviceList() {
      var count = 0;
      Object.keys(docs).forEach(function (k) {
        count = Math.max(count, Number(k) + 1);
      });
      var want = Math.max(count, 1);
      if (devSelect.options.length !== want) {
        var keep = currentDev;
        HSF.replaceChildren(devSelect);
        for (var i = 0; i < want; i++) {
          var opt = HSF.el("option", { text: "" });
          opt.value = String(i);
          devSelect.appendChild(opt);
        }
        devSelect.value = String(Math.min(keep, want - 1));
        currentDev = Math.min(keep, want - 1);
      }
      for (var j = 0; j < want; j++) {
        var st = docs[j];
        var name = (st && st.name) ? st.name : ("数字量" + (j + 1));
        var text = (j + 1) + " · " + name;
        var opt2 = devSelect.options[j];
        if (opt2.textContent !== text) opt2.textContent = text;
      }
    }

    devSelect.addEventListener("change", function () {
      currentDev = Math.max(0, parseInt(devSelect.value, 10) || 0);
      renderAll();
    });

    /* ---- DO 写（三态终态；被闸拒绝时回滚显示态） ---- */
    function onDoToggle(index, wantOn) {
      var reason = HSF.controlBlockedReason(
        ctx.canPerm(HSF.app.PERM.DEVICE_CONTROL));
      if (reason) {
        HSF.statusMessage("禁止操作：" + reason);
        /* 回滚按钮显示态（不允许的状态不能留在界面上骗人）。 */
        var st = docs[currentDev];
        var ch = (st && st.do || [])[index];
        doGrid.setState(index, !!(ch && ch.on));
        return;
      }
      HSF.statusMessage("正在写 DO" + index + " = " +
                        (wantOn ? "通" : "断") + "…");
      HSF.api.digitalControl(
        { dev: currentDev, do: index, on: !!wantOn }).then(function (doc) {
        if (doc && doc.ok) {
          HSF.statusMessage("已写 DO" + index + " = " +
                            (wantOn ? "通" : "断"));
        } else {
          HSF.statusMessage("写 DO" + index + " 失败：" +
            ((doc && doc.err) || "未知原因"));
        }
        renderAll();            /* 应答/广播已落 store，状态自动回写 */
      });
    }

    /* ---- 刷新（与参考 DigitalPage.refresh 同构） ---- */
    var laidOut = -1;
    function renderAll() {
      collectDocs();
      syncDeviceList();
      var st = docs[currentDev] || null;
      var total = (st && st.count) ? st.count : PLACEHOLDER_COUNT;
      if (total !== laidOut) {
        diLamp.setCount(total);
        doGrid.setCount(total);
        laidOut = total;
      }

      if (!st || !st.fresh) {
        for (var i = 0; i < total; i++) {
          diLamp.setState(i, false);
          diLamp.setName(i, "");
          doGrid.setState(i, false);
          doGrid.setName(i, "");
        }
        var why = (!st || !st.valid) ? "离线" : "数据过期";
        setText(stateLabel, "无数据（" + why + "）");
        doGrid.setEnabledAll(false);
        setHint("设备数据不可用，DO 操作已禁用");
        return;
      }
      for (var k = 0; k < total; k++) {
        var diCh = (st.di || [])[k] || {};
        var doCh = (st.do || [])[k] || {};
        diLamp.setState(k, !!diCh.on);
        diLamp.setName(k, diCh.name || "");
        doGrid.setState(k, !!doCh.on);
        doGrid.setName(k, doCh.name || "");
      }
      setText(stateLabel, "DI " + (st.diText || "--") + " · DO " +
              (st.doText || "--"));
      var reason = HSF.controlBlockedReason(
        ctx.canPerm(HSF.app.PERM.DEVICE_CONTROL));
      doGrid.setEnabledAll(!reason);
      setHint(reason ? ("操作已禁用：" + reason) : "");
    }

    function setHint(text) {
      text = String(text || "");
      if (doHint.textContent !== text) doHint.textContent = text;
    }

    unsubs.push(store.on("digital", renderAll));
    unsubs.push(store.on("link", renderAll));
    unsubs.push(store.on("mode", renderAll));
    /* 入页先拉一次快照：不依赖周期推送的首帧时机（on_enter 同口径）。 */
    if (connected()) {
      HSF.api.digitalSnapshot(currentDev).then(function () { renderAll(); });
    }
    timers.push(setInterval(renderAll, 500));   /* 兜底重绘（同值早退） */

    renderAll();
    void ctx;

    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
    };
  }
})();
