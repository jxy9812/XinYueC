/* -*- coding: utf-8 -*-
 * 数据三页（9 历史数据 / 10 日志 / 11 报警）**共用的工具条**与通用控件。
 *
 * [对齐参考 data_toolbar.DataPageToolbar 逐项移植（三页一份、按能力裁剪）]
 *   三行结构照参考逐字同款：
 *     ① 筛选行：[页面特有下拉] 起始 结束 列(多选) 关键字 筛选 清除筛选
 *     ② 分页行：每页 上一页 第N/M页 下一页 总数统计 计数 刷新
 *     ③ 视图行：折线列(多选) 表格视图 图表视图 滚动锁定 导出数据
 *   没有的能力**就不长控件**（项目铁律：不许留占位/死控件）：
 *     · 历史页 = 全套；
 *     · 日志页 = 无折线列/双视图/滚动锁定（无图表、也无自动刷新）；
 *     · 报警页 = 无折线列/双视图/刷新（推送来的数据没有"重查"动作）。
 *
 * [筛选行拆两条独立布局] 筛选项共 7 组，窄窗下全塞一行会把「列」「关键字」
 * 挤成省略号（参考 data_toolbar.py:471-506 同款口径）——时间窗一行、
 * 列+关键字一行。
 *
 * [控件只长控件+发回调，不持业务状态] 分页/筛选/数据仍在页面里，
 * 本模块只把 DOM 句柄与回调交出来（对齐参考「属性别名」的职责边界）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* ================================================================
   *           时间归一化（参考 HistoryPage._norm_stamp 同口径）
   * ================================================================ */

  /**
   * 筛选时间归一化；无法解析返回 null。
   * :returns: null 或 {mode:"F"|"T", key}。mode="F" → key 为完整
   * "yyyy-MM-dd HH:MM:SS"；mode="T" → key 为 "HH:MM:SS"（只填时刻
   * 按"当天"语义）。接受（起点缺省补 00:00:00、终点补 23:59:59）：
   *   HH:MM[:SS] / MM-dd [HH:MM[:SS]] / yyyy-MM-dd [HH:MM[:SS]]
   */
  function normStamp(text, isEnd) {
    text = String(text == null ? "" : text).trim();
    if (!text) return null;
    var tokens = text.split(/\s+/);
    if (tokens.length > 2) return null;
    var datePart = null, timePart = null;
    tokens.forEach(function (token) {
      if (token.indexOf(":") >= 0) {
        if (timePart !== null) { timePart = "?"; return; }
        timePart = token;
      } else {
        if (datePart !== null) { datePart = "?"; return; }
        datePart = token;
      }
    });
    if (datePart === "?" || timePart === "?") return null;
    if (datePart === null && timePart === null) return null;

    /* 时间段：缺省补端点 */
    var hh = isEnd ? 23 : 0, mm = isEnd ? 59 : 0, ss = isEnd ? 59 : 0;
    if (timePart !== null) {
      var parts = timePart.split(":");
      if (parts.length < 2 || parts.length > 3) return null;
      var nums = [];
      for (var i = 0; i < parts.length; i++) {
        if (!/^\d+$/.test(parts[i])) return null;
        nums.push(parseInt(parts[i], 10));
      }
      if (nums.length === 2) nums.push(isEnd ? 59 : 0);
      hh = nums[0]; mm = nums[1]; ss = nums[2];
      if (!(hh >= 0 && hh < 24 && mm >= 0 && mm < 60 && ss >= 0 && ss < 60)) {
        return null;
      }
    }

    /* 日期段：缺省 = 只比当天时刻（mode "T"） */
    if (datePart === null) {
      return { mode: "T", key: pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss) };
    }
    var dp = datePart.split("-");
    var y, mo, d;
    if (dp.length === 2) {                       /* MM-dd → 当年 */
      var today = new Date();
      y = today.getFullYear();
      mo = parseInt(dp[0], 10); d = parseInt(dp[1], 10);
    } else if (dp.length === 3) {                /* yyyy-MM-dd */
      y = parseInt(dp[0], 10); mo = parseInt(dp[1], 10); d = parseInt(dp[2], 10);
    } else {
      return null;
    }
    var probe = new Date(y, mo - 1, d);          /* 合法性校验（含闰年） */
    if (isNaN(probe.getTime()) || probe.getMonth() !== mo - 1 ||
        probe.getDate() !== d) {
      return null;
    }
    return { mode: "F", key: y + "-" + pad2(mo) + "-" + pad2(d) +
             " " + pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss) };
  }

  /** normStamp 的 {mode,key} → epoch 毫秒；null 原样返回（参考 _stamp_to_ms）。
   *  [月份 0 基] JS Date 构造的月份要 -1——2026-11-06 传成 11 月就是 12 月，
   *  时间窗整体后移一个月 ⇒ WHERE 永远 0 行（实锅：历史页第 3 秒探活后
   *  「共 0 条」，就是它）。 */
  function stampToMs(stamp) {
    if (stamp === null || stamp === undefined) return null;
    var key = stamp.key;
    var y, mo, d, hh, mm, ss;
    if (key.length === 19) {
      y = parseInt(key.slice(0, 4), 10);
      mo = parseInt(key.slice(5, 7), 10) - 1;
      d = parseInt(key.slice(8, 10), 10);
      hh = parseInt(key.slice(11, 13), 10);
      mm = parseInt(key.slice(14, 16), 10);
      ss = parseInt(key.slice(17, 19), 10);
    } else {
      var today = new Date();
      y = today.getFullYear(); mo = today.getMonth(); d = today.getDate();
      hh = parseInt(key.slice(0, 2), 10);
      mm = parseInt(key.slice(3, 5), 10);
      ss = parseInt(key.slice(6, 8), 10);
    }
    return new Date(y, mo, d, hh, mm, ss).getTime();
  }

  function pad2(n) { return (n < 10 ? "0" : "") + n; }

  /** 毫秒 → "yyyy-MM-dd HH:MM:SS" 本地时间（无效 → "--"）。 */
  function fmtStamp(ms) {
    if (typeof ms !== "number" || !isFinite(ms) || ms <= 0) return "--";
    var d = new Date(ms);
    if (isNaN(d.getTime())) return "--";
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate()) +
      " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
  }

  /** 毫秒 → "HH:MM:SS"（报警页时间列，参考只显示时刻口径）。 */
  function fmtHMS(ms) {
    if (typeof ms !== "number" || !isFinite(ms) || ms <= 0) return "--";
    var d = new Date(ms);
    if (isNaN(d.getTime())) return "--";
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
  }

  /* ================================================================
   *     锚定弹层（时间选择 / 多选列表共用；点外面才收，点里面不收）
   * ================================================================ */

  var openPopupState = null;      /* {panel, onDoc, onResize} */

  function closeAnchoredPopup() {
    if (!openPopupState) return;
    var st = openPopupState;
    openPopupState = null;
    if (st.panel.parentNode) st.panel.parentNode.removeChild(st.panel);
    document.removeEventListener("mousedown", st.onDoc, true);
    window.removeEventListener("resize", st.onResize);
    if (st.onClose) st.onClose();
  }

  /** 在 anchorEl 下方开一个弹层；点层外自动收（对齐 Qt.Popup 关闭语义）。 */
  function anchoredPopup(anchorEl, build) {
    closeAnchoredPopup();
    var panel = HSF.el("div.mcc-pop");
    build(panel);
    document.body.appendChild(panel);

    var r = anchorEl.getBoundingClientRect();
    var pw = panel.offsetWidth, ph = panel.offsetHeight;
    var left;
    if (HSF.isNarrow()) {
      /* [窄屏全宽化] 列多选/时间选择浮层贴屏两边铺满（不溢出视口）；
       * 纵向仍贴锚点：下方放不下就往上翻。 */
      left = 8;
      panel.style.width = (window.innerWidth - 16) + "px";
      pw = panel.offsetWidth;
    } else {
      left = Math.max(4, Math.min(r.left, window.innerWidth - pw - 4));
    }
    var top = r.bottom + 2;
    if (top + ph > window.innerHeight - 4) {
      top = Math.max(4, r.top - ph - 2);       /* 下方放不下：往上翻 */
    }
    panel.style.left = left + "px";
    panel.style.top = top + "px";

    var st = openPopupState = { panel: panel, onClose: null };
    st.onDoc = function (ev) {
      if (!panel.contains(ev.target) && ev.target !== anchorEl &&
          !anchorEl.contains(ev.target)) {
        closeAnchoredPopup();
      }
    };
    st.onResize = function () { closeAnchoredPopup(); };
    /* 下一个宏任务再挂监听：开弹层的那次 mousedown 不许立刻关掉它。 */
    setTimeout(function () {
      if (openPopupState !== st) return;
      document.addEventListener("mousedown", st.onDoc, true);
      window.addEventListener("resize", st.onResize);
    }, 0);
    return st;
  }

  /* ================================================================
   *   MultiCheckCombo（按钮 + 弹出勾选列表；点项不收，点外面才收）
   * ================================================================ */

  /**
   * :param items: 选项名数组；:param o:
   *   allText（缺省"全部列"）/ initial:"all"|"none" / minChecked（至少保留
   *   几个勾选，0=允许全不勾）/ emptyText（空选独立文案，None 沿用 allText）/
   *   onChange()（勾选变化回调，等价 selectionChanged）。
   * 汇总口径照参考 _summary_text：全勾 → allText（minChecked>0 时后缀
   * "（全部）"）；部分勾 → "、".join(names)；空选 → emptyText || allText。
   */
  function multiCheckCombo(items, o) {
    o = o || {};
    var allText = o.allText || "全部列";
    var minChecked = Math.max(0, o.minChecked | 0);
    var labels = (items || []).map(String);
    var checked = labels.map(function () { return o.initial === "all"; });
    var checks = [];

    var btn = HSF.el("button.btn.mcc-btn", { type: "button", title: o.tip || "" });
    var syncing = false;

    function summaryText() {
      var names = [];
      checked.forEach(function (on, i) { if (on) names.push(labels[i]); });
      if (names.length && names.length === labels.length) {
        return minChecked > 0 ? allText + "（全部）" : allText;
      }
      if (names.length) return names.join("、");
      return o.emptyText || allText;
    }

    function syncText(fire) {
      var text = summaryText() + "  ▾";
      if (btn.textContent !== text) btn.textContent = text;
      if (fire && o.onChange && !syncing) o.onChange();
    }

    /** 改单项勾选（受 min_checked 约束）。返回是否真的改了。 */
    function setChecked(index, on) {
      if (index < 0 || index >= checked.length) return false;
      if (!on && minChecked > 0 && sum() <= minChecked) return false;
      if (checked[index] === on) return false;
      checked[index] = on;
      syncing = true;
      try {
        if (checks[index]) checks[index].checked = on;
      } finally { syncing = false; }
      syncText(true);
      return true;
    }

    function sum() {
      var n = 0;
      checked.forEach(function (on) { if (on) n++; });
      return n;
    }

    btn.addEventListener("click", function () {
      if (openPopupState && openPopupState.owner === api) {
        closeAnchoredPopup();
        return;
      }
      anchoredPopup(btn, function (panel) {
        checks = [];
        labels.forEach(function (label, i) {
          var cb = HSF.el("input.check", { type: "checkbox" });
          cb.checked = checked[i];
          var row = HSF.el("label.mcc-item", null, [cb, HSF.el("span", { text: label })]);
          cb.addEventListener("change", function () {
            /* 至少留一列的约束在 setChecked 里；被拒时把勾选框回弹。 */
            if (!setChecked(i, cb.checked)) cb.checked = checked[i];
          });
          checks.push(cb);
          panel.appendChild(row);
        });
      }).owner = api;
    });

    var api = {
      root: btn,
      /** 当前勾中的选项下标列表（升序）。 */
      checkedIndexes: function () {
        var out = [];
        checked.forEach(function (on, i) { if (on) out.push(i); });
        return out;
      },
      /** 整体重设勾选（给定下标集合，其余全清；"整体回显"语义）。 */
      setCheckedIndexes: function (indexes) {
        var want = {};
        (indexes || []).forEach(function (i) { want[i | 0] = true; });
        checked = labels.map(function (_l, i) { return !!want[i]; });
        syncing = true;
        try {
          checks.forEach(function (cb, i) { if (cb) cb.checked = checked[i]; });
        } finally { syncing = false; }
        syncText(true);
      },
      checkAll: function () {
        checked = labels.map(function () { return true; });
        syncing = true;
        try {
          checks.forEach(function (cb, i) { if (cb) cb.checked = true; });
        } finally { syncing = false; }
        syncText(true);
      },
      destroy: function () {
        if (openPopupState && openPopupState.owner === api) closeAnchoredPopup();
      },
    };
    syncText(false);
    return api;
  }

  /* ================================================================
   *        TimePickerEdit（文本框 + 单击弹日期时间选择浮层）
   * ================================================================ */

  /** 宽度按最宽候选文本实测（参考 _fit_time_edit_width 同口径，不写死像素）。
   *  加数 = 输入框自身的左右 padding + 边框（从计算样式量，缩放后仍成立），
   *  只按文本量会把末位数字裁掉（2026-11-13 实锅：起始框 "…02:34:1"）。
   *  [必须挂到 DOM 后再量一次] 刚 createElement 还没进文档时 class 样式
   *  不生效（padding/字号都是缺省值），量出来的宽度偏窄——挂载后的下一帧
   *  复量一次（Qt「QSS 要 polish 后字体才定」的 web 版同款坑）。 */
  function fitTimeEditWidth(edit) {
    var cs = window.getComputedStyle(edit);
    var font = (cs.fontStyle || "") + " " + (cs.fontWeight || "") + " " +
      (cs.fontSize || "14px") + " " + (cs.fontFamily || "sans-serif");
    function px(v) { return parseFloat(v) || 0; }
    var chrome = px(cs.paddingLeft) + px(cs.paddingRight) +
      px(cs.borderLeftWidth) + px(cs.borderRightWidth);
    /* 下限 200px（app 字体 20px 下「2026-11-11 15:04:48」实测 ≈190px，
     * 旧下限 140px 是 14px 字号时代的值，缩到那儿会裁末位秒）。 */
    var w = Math.max(200, HSF.measureText(font, "2026-11-11 15:04:48") +
                     Math.ceil(chrome) + 2);
    edit.style.width = w + "px";
    edit.style.minWidth = w + "px";
    edit.style.maxWidth = w + "px";
  }

  function timeEditFitOnAttach(edit) {
    fitTimeEditWidth(edit);
    /* [用 setTimeout 不用 rAF] 内嵌/无头浏览器可能不渲染帧 ⇒ rAF 回调
     * 永不执行（实测：IAB 里 rAF 500ms 不触发）——宏任务与帧无关，
     * 挂载后必跑。两次兜底：0ms（同步挂载完成后）+ 250ms（字体/样式
     * 最终确定后，对齐 Qt「QSS polish 后复量」的口径）。 */
    setTimeout(function () { fitTimeEditWidth(edit); }, 0);
    setTimeout(function () { fitTimeEditWidth(edit); }, 250);
  }

  function timeEdit(placeholder) {
    var edit = HSF.el("input.input.dt-time", { type: "text", placeholder: placeholder || "",
                                               title: "支持 HH:MM / MM-dd HH:MM / yyyy-MM-dd HH:MM" });
    var touched = false;
    edit.addEventListener("input", function () { touched = true; });
    timeEditFitOnAttach(edit);

    edit.addEventListener("click", function () {
      anchoredPopup(edit, function (panel) {
        panel.classList.add("dt-picker");
        panel.appendChild(HSF.el("div.caption", { text: "日期时间" }));
        var dt = HSF.el("input.input", { type: "datetime-local", step: 1 });
        /* 现有文本回填（local ISO 形态；解析不了就留空=当前时刻）。 */
        var cur = normStamp(edit.value, false);
        if (cur && cur.mode === "F") {
          dt.value = cur.key.replace(" ", "T");
        } else {
          var now = new Date();
          dt.value = now.getFullYear() + "-" + pad2(now.getMonth() + 1) + "-" +
            pad2(now.getDate()) + "T" + pad2(now.getHours()) + ":" +
            pad2(now.getMinutes()) + ":" + pad2(now.getSeconds());
        }
        panel.appendChild(dt);
        var row = HSF.el("div.dt-picker-btns");
        var ok = HSF.el("button.btn", { type: "button", text: "确定" });
        var clearBtn = HSF.el("button.btn", { type: "button", text: "清空" });
        row.appendChild(ok); row.appendChild(clearBtn);
        panel.appendChild(row);
        ok.addEventListener("click", function () {
          var v = String(dt.value || "");          /* yyyy-MM-ddTHH:MM[:SS] */
          if (/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2})?$/.test(v)) {
            edit.value = v.replace("T", " ");
            touched = true;
            if (edit.onUserEdit) edit.onUserEdit();
          }
          closeAnchoredPopup();
        });
        clearBtn.addEventListener("click", function () {
          edit.value = "";
          touched = true;
          if (edit.onUserEdit) edit.onUserEdit();
          closeAnchoredPopup();
        });
      });
    });

    edit.wasTouched = function () { return touched; };
    edit.markTouched = function () { touched = true; };
    return edit;
  }

  /* ================================================================
   *                      DataPageToolbar 主体
   * ================================================================ */

  /**
   * :param o: columns / chartColumns（名数组或 null）/ extraCombo:{label,items}
   *   （value 数组可选 extraValues）/ pageSizes / paging / refresh /
   *   chartView / freeze / clientLog / exportBtn / exportText /
   *   keywordPlaceholder /
   *   handlers:{ filter, clear, pageSize(size), prev, next, refresh,
   *     exportBtn:…, view(mode), columns, chartColumns, freeze(frozen),
   *     clientLog(on), extraCombo(index) }
   */
  function dataToolbar(o) {
    o = o || {};
    var h = o.handlers || {};
    var root = HSF.el("div.dt");
    /* ---- 折叠把手（顶行）----
     * [2026-11-19 用户口径「表格的上面与这些下面中间增加一个分割条，
     *  可折叠功能区，浏览数据更方便」⇒ 与桌面客户端数据三页同款] 把手
     * 放工具条顶部，折叠 = 收起筛选/分页/视图全部行 ⇒ 表格大一屏。
     * 状态按页落 localStorage（操作员长期偏好，跨刷新保留）。
     * [触摸] 把手整行可点、高 32px；折叠态文案「▸ 展开筛选」。 */
    var pageKey = o.pageKey || "";
    var _rows = [];
    var _collapsed = false;
    try {
      if (window.HSF && HSF.prefs)
        _collapsed = HSF.prefs.get(
            "dataToolbarCollapsed." + pageKey, "") === "1";
    } catch (err) { _collapsed = false; }
    var foldBtn = HSF.el("button.btn.dt-fold", {
      type: "button",
      text: _collapsed ? "▸ 展开筛选" : "▾ 收起筛选"
    });
    /* [2026-11-19 用户口径「放第一行标题栏右边」] 折叠把手不再独占一行
     * （白白占一行高）⇒ 页面把标题行右侧的宿主元素传进来（o.foldHost），
     * 把手挂到那里；未传（老页面）保持原独立一行。折叠逻辑不变。 */
    if (o.foldHost) {
      foldBtn.classList.add("dt-fold-inline");
      o.foldHost.appendChild(foldBtn);
    } else {
      var foldRow = HSF.el("div.dt-row.dt-fold-row", null,
        [foldBtn, HSF.el("span.top-stretch")]);
      root.appendChild(foldRow);
    }

    /* ---- ① 筛选行（两条独立布局）---- */
    var fbar1 = HSF.el("div.dt-row");
    var extraSel = null;
    if (o.extraCombo) {
      fbar1.appendChild(HSF.el("span.dt-lbl", { text: o.extraCombo.label }));
      extraSel = HSF.el("select.input.dt-select");
      var vals = o.extraCombo.values ||
        o.extraCombo.items.map(function (_t, i) { return i; });
      o.extraCombo.items.forEach(function (item, i) {
        var opt = HSF.el("option", { text: item });
        opt.value = String(vals[i]);
        extraSel.appendChild(opt);
      });
      extraSel.addEventListener("change", function () {
        if (h.extraCombo) h.extraCombo(extraSel.selectedIndex, extraSel.value);
      });
      if (o.extraCombo.tip) extraSel.title = o.extraCombo.tip;
      fbar1.appendChild(extraSel);
    }
    /* [标签与输入框成对] 「起始 + 框」「结束 + 框」各包一个 .dt-field：
     * 窄屏换行时标签不会跟输入框分家（原来「结束」标签会孤零零留在上一行
     * 末尾，看着像「起始」的第二个框）。宽屏 .dt-field 是 inline-flex 且
     * 内部 gap 与行 gap 同值 ⇒ 与改前逐像素一致。 */
    var startEdit = timeEdit("yyyy-MM-dd HH:MM 或 HH:MM");
    fbar1.appendChild(HSF.el("span.dt-field", null, [
      HSF.el("span.dt-lbl", { text: "起始" }), startEdit]));
    var endEdit = timeEdit("yyyy-MM-dd HH:MM 或 HH:MM");
    fbar1.appendChild(HSF.el("span.dt-field", null, [
      HSF.el("span.dt-lbl", { text: "结束" }), endEdit]));
    fbar1.appendChild(HSF.el("span.top-stretch"));
    var btnFilter = HSF.el("button.btn", { type: "button", text: "筛选",
                                           onclick: function () { if (h.filter) h.filter(); } });
    fbar1.appendChild(btnFilter);
    var btnClear = HSF.el("button.btn", { type: "button", text: "清除筛选",
                                          onclick: function () { if (h.clear) h.clear(); } });
    fbar1.appendChild(btnClear);
    _rows.push(fbar1);
    root.appendChild(fbar1);

    var fbar2 = HSF.el("div.dt-row");
    /* [「列」= 显示哪些列] 默认全部勾选、至少留一列（参考 MultiCheckCombo）。 */
    var colCombo = null;
    if (o.columns) {
      fbar2.appendChild(HSF.el("span.dt-lbl", { text: "列" }));
      colCombo = multiCheckCombo(o.columns, {
        allText: "全部列", initial: "all", minChecked: 1,
        tip: "勾选的列才显示（默认全部勾选，至少保留一列）",
        onChange: function () { if (h.columns) h.columns(); },
      });
      fbar2.appendChild(colCombo.root);
    }
    fbar2.appendChild(HSF.el("span.dt-lbl", { text: "关键字" }));
    var keywordEdit = HSF.el("input.input.dt-kw", {
      type: "text", placeholder: o.keywordPlaceholder || "包含…（空=不限）",
    });
    keywordEdit.addEventListener("keydown", function (ev) {
      if (ev.key === "Enter" && h.filter) h.filter();   /* 回车=应用筛选 */
    });
    fbar2.appendChild(keywordEdit);
    _rows.push(fbar2);
    root.appendChild(fbar2);

    /* ---- ② 分页行 ---- */
    var pageSizeSel = null, btnPrev = null, btnNext = null, btnReload = null;
    var pageLabel = null, totalLabel = null, countLabel = null;
    if (o.paging !== false) {
      var pbar = HSF.el("div.dt-row");
      pbar.appendChild(HSF.el("span.dt-lbl", { text: "每页" }));
      pageSizeSel = HSF.el("select.input.dt-select");
      (o.pageSizes || HSF.pageSizeChoices || [10, 20, 50]).forEach(function (n) {
        var opt = HSF.el("option", { text: n + " 条" });
        opt.value = String(n);
        pageSizeSel.appendChild(opt);
      });
      pageSizeSel.addEventListener("change", function () {
        if (h.pageSize) h.pageSize(parseInt(pageSizeSel.value, 10));
      });
      pbar.appendChild(pageSizeSel);
      btnPrev = HSF.el("button.btn", { type: "button", text: "上一页",
                                       onclick: function () { if (h.prev) h.prev(); } });
      pbar.appendChild(btnPrev);
      pageLabel = HSF.el("span.caption.dt-stat");
      pbar.appendChild(pageLabel);
      btnNext = HSF.el("button.btn", { type: "button", text: "下一页",
                                       onclick: function () { if (h.next) h.next(); } });
      pbar.appendChild(btnNext);
      totalLabel = HSF.el("span.caption.dt-stat");
      pbar.appendChild(totalLabel);
      countLabel = HSF.el("span.caption.dt-stat");
      pbar.appendChild(countLabel);
      pbar.appendChild(HSF.el("span.top-stretch"));
      if (o.refresh) {
        btnReload = HSF.el("button.btn", { type: "button", text: "刷新",
                                           onclick: function () { if (h.refresh) h.refresh(); } });
        pbar.appendChild(btnReload);
      }
      _rows.push(pbar);
      root.appendChild(pbar);
    }

    /* ---- ③ 视图行 ---- */
    var vbar = HSF.el("div.dt-row");
    var chartColCombo = null;
    if (o.chartColumns) {
      vbar.appendChild(HSF.el("span.dt-lbl", { text: "折线列" }));
      chartColCombo = multiCheckCombo(o.chartColumns, {
        allText: "选择列…", initial: "none", minChecked: 0,
        tip: "可多选：勾中的列各画一条线（至少选一列）",
        onChange: function () { if (h.chartColumns) h.chartColumns(); },
      });
      vbar.appendChild(chartColCombo.root);
    }
    var btnTableView = null, btnChartView = null;
    if (o.chartView) {
      btnTableView = HSF.el("button.btn", { type: "button", text: "表格视图" });
      btnTableView.addEventListener("click", function () { if (h.view) h.view(0); });
      vbar.appendChild(btnTableView);
      btnChartView = HSF.el("button.btn", { type: "button", text: "图表视图" });
      btnChartView.addEventListener("click", function () { if (h.view) h.view(1); });
      vbar.appendChild(btnChartView);
    }
    /* [客户端运行日志开关] 日志页专用能力位（参考 data_toolbar.extra_slot：
     * 页面把自己的「客户端运行日志」QCheckBox 塞进视图行**左端**、stretch
     * 之前；勾选 = 页面展开客户端本地日志副视图）。没有该能力的页面不长
     * 控件。 */
    var clientLogCheck = null;
    if (o.clientLog) {
      clientLogCheck = HSF.el("label.dt-freeze", null, [
        HSF.el("input.check", { type: "checkbox" }),
        HSF.el("span", { text: "客户端运行日志" })]);
      clientLogCheck.firstChild.addEventListener("change", function () {
        if (h.clientLog) h.clientLog(clientLogCheck.firstChild.checked);
      });
      clientLogCheck.title = "勾选展开客户端本地运行日志（网页侧状态消息收流）";
      vbar.appendChild(clientLogCheck);
    }
    vbar.appendChild(HSF.el("span.top-stretch"));
    var freezeCheck = null;
    if (o.freeze) {
      freezeCheck = HSF.el("label.dt-freeze", null, [
        HSF.el("input.check", { type: "checkbox" }),
        HSF.el("span", { text: "滚动锁定（停止刷表，后台照常采样）" })]);
      freezeCheck.firstChild.addEventListener("change", function () {
        if (h.freeze) h.freeze(freezeCheck.firstChild.checked);
      });
      vbar.appendChild(freezeCheck);
    }
    var btnExport = null;
    if (o.exportBtn !== false) {
      btnExport = HSF.el("button.btn", {
        type: "button", text: o.exportText || "导出数据",
        onclick: function () { if (h.export) h.export(); },
      });
      vbar.appendChild(btnExport);
    }
    _rows.push(vbar);
    root.appendChild(vbar);

    /* ---- 折叠（内部闭包：状态与行句柄都在本函数作用域）---- */
    function setCollapsed(c) {
      _collapsed = !!c;
      _rows.forEach(function (r) {
        r.style.display = _collapsed ? "none" : "";
      });
      foldBtn.textContent = _collapsed ? "▸ 展开筛选" : "▾ 收起筛选";
    }
    function saveCollapsed(c) {
      try {
        if (window.HSF && HSF.prefs)
          HSF.prefs.set("dataToolbarCollapsed." + pageKey,
                        c ? "1" : "");
      } catch (err) { /* 隐私模式等：不记忆也不报错 */ }
    }
    foldBtn.addEventListener("click", function () {
      setCollapsed(!_collapsed);
      saveCollapsed(_collapsed);
    });
    /* 构造即应用（刷新后恢复上次的折叠状态）。 */
    setCollapsed(_collapsed);

    function setLabels(node, text) {
      text = String(text == null ? "" : text);
      if (node.textContent !== text) node.textContent = text;
    }

    return {
      root: root,
      startEdit: startEdit,
      endEdit: endEdit,
      keywordEdit: keywordEdit,
      colCombo: colCombo,
      chartColCombo: chartColCombo,
      extraSel: extraSel,
      pageSizeSel: pageSizeSel,
      btnPrev: btnPrev,
      btnNext: btnNext,
      btnReload: btnReload,
      pageLabel: pageLabel,
      totalLabel: totalLabel,
      countLabel: countLabel,
      btnTableView: btnTableView,
      btnChartView: btnChartView,
      clientLogCheck: clientLogCheck,
      freezeCheck: freezeCheck,
      btnExport: btnExport,
      setPageLabel: function (t) { if (pageLabel) setLabels(pageLabel, t); },
      setTotalLabel: function (t) { if (totalLabel) setLabels(totalLabel, t); },
      setCountLabel: function (t) { if (countLabel) setLabels(countLabel, t); },
      /** 按值选中「每页」档（找不到就不动；对齐 set_page_size）。 */
      setPageSize: function (size) {
        if (!pageSizeSel) return;
        var kids = pageSizeSel.options;
        for (var i = 0; i < kids.length; i++) {
          if (parseInt(kids[i].value, 10) === size) {
            if (pageSizeSel.selectedIndex !== i) pageSizeSel.selectedIndex = i;
            return;
          }
        }
      },
      pageSize: function () {
        return pageSizeSel ? parseInt(pageSizeSel.value, 10) : 0;
      },
      /** 同步「表格视图/图表视图」按钮按下态（对齐 set_view_mode）。 */
      setViewMode: function (mode) {
        if (btnTableView) btnTableView.classList.toggle("primary", mode === 0);
        if (btnChartView) btnChartView.classList.toggle("primary", mode === 1);
      },
      frozen: function () { return !!(freezeCheck && freezeCheck.firstChild.checked); },
      destroy: function () {
        if (colCombo) colCombo.destroy();
        if (chartColCombo) chartColCombo.destroy();
        closeAnchoredPopup();
      },
    };
  }

  /** 共享卸载：页面 unmount 时收弹层（多选/时间选择都可能开着）。 */
  HSF.closeDataPopups = closeAnchoredPopup;

  HSF.normStamp = normStamp;
  HSF.stampToMs = stampToMs;
  HSF.fmtStamp = fmtStamp;
  HSF.fmtHMS = fmtHMS;
  HSF.multiCheckCombo = multiCheckCombo;
  HSF.dataToolbar = dataToolbar;
  /* [单一事实源] 「每页条数」档位（10/20/50）：数据三页工具条的「每页 N 条」
   * 下拉与页 13「本地设置·每页条数」卡共用这一份（页 13 只读，不另立一份）。 */
  HSF.pageSizeChoices = [10, 20, 50];
})();
