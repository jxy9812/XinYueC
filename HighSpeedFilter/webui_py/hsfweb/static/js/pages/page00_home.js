/* -*- coding: utf-8 -*-
 * 页 0 主界面：自动模式操作卡 + 关键指标总览（首行=自动模式）。
 *
 * [对齐参考 pages_other.HomePage 逐项移植]（唯一视觉与功能基准）：
 *   · 页头已删（2026-11-12 定案）⇒ 首行直接是自动模式卡；
 *   · 自动模式卡：模式选择（产水/反洗/顺洗/自动（流量循环））+ 启动/停止/
 *     复位三按钮 + 状态行短语 + 「累计」运行时长（定宽防跳动，天数封顶
 *     999+天，缺数据显「--」）+ 运行表盘（2026-11-13 定案：运行中四个模式
 *     都显示——有界相位维持倒计时现状；无界状态（单模式三模式、循环产水
 *     且兜底关）显示「本次运行」时长）；
 *   · 总览卡：电箱温度/搅拌泵(可点弹详情)/设备健康(可点弹设备表)；
 *   · 第二排：压差（三栏：压差 / 压力1 / 压力2）/流量计/电动球阀六路；
 *     第三排：浊度仪/按钮组系统状态；
 *   · 悬浮详情卡：设备状态表（5 列）与搅拌泵详情（5 块）二选一居中弹出，
 *     ×/遮罩收起；
 *   · 快捷入口行**不做**（参考 2026-11-12 已删：与导航树完全重复）。
 *
 * [数据面] store 各主题快照 + SSE 增量；自动状态族 1s 轮询 /api/mode
 * （参考 bridge.refresh_run_mode 的 1s 节流同口径），应答写回 store
 * （标题栏自动徽标同源刷新）。控制动作 POST /api/auto/control，
 * 反馈落状态栏终态（成功/失败，fetch 异常已由 api.js 归一为失败）。
 *
 * [权限置灰] 启动/停止/复位按 PERM_AUTO_CONTROL + 链路 + 自动模式 +
 * 运行位/锁存位判缺省态；后端仍终审。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* ---- 常量（与参考 HomePage 类常量逐项一致） ---- */
  var AUTO_PHASE_TEXT = { valves: "阀动作中…", pumps: "启动泵…", running: "" };
  var AUTO_RUN_MAX_TEXT = "999+天 23时 59分 59秒";
  var VALVE_COUNT = 6;
  /* [改名 2026-11-23 用户口径] 电动球阀1~6 ⇒ 按工艺位号命名（服务端缺省
   * 与两端客户端逐字一致；这里是「快照没带 name」时的兜底，不编编号）。 */
  var VALVE_FALLBACK_NAMES = ["产水阀(1)", "反洗进水阀(2)", "进水阀(3)",
                              "排空阀(4)", "反洗出水阀(5)", "顺洗出水阀(6)"];
  var TURBIDITY_COUNT = 2;
  var TURBIDITY_WARN_NTU = 20.0;
  var BTN_ESTOP_NAME = "急停";
  var BTN_START_NAME = "启动按钮";
  var BTN_STOP_NAME = "停止按钮";
  var BTN_RESET_NAME = "复位按钮";
  var BTN_LAMP_NAMES = ["启动按钮灯", "停止按钮灯", "复位按钮灯"];
  /* 触点极性：急停/停止 = 常闭（1=未按/回路通）、启动/复位 = 常开。 */
  var BTN_NORMALLY_CLOSED = [true, false, true, false];
  var BTN_SLOT_COUNT = 7;
  var BTN_SLOT_LABELS = ["急停", "启动", "停止", "复位",
                         "启动灯", "停止灯", "复位灯"];
  var DETAIL_TABLE_W = 960;
  var DETAIL_PUMP_W = 640;

  /* 语义 tone → 颜色令牌（不许散写十六进制字面量；深套取 theme.css 变量）。 */
  var C_OK = "var(--ok)";
  var C_WARN = "var(--warn)";
  var C_FAULT = "var(--fault)";
  var C_OFFLINE = "var(--offline)";
  var C_TEXT = "var(--text)";
  var C_SUB = "var(--text-secondary)";
  var C_NA = "var(--offline)";
  var C_INFO = "var(--accent-line)";
  /* 阀状态 tone → 色（色义对齐 pages_valve：已开绿/已关灰蓝/动作中橙/故障红）。 */
  var TONE_COLOR = {
    unknown: C_NA,
    closed: "var(--valve-closed)",   /* 已关灰蓝 #37474F（pages_valve.py:65，
                                        参考首页阀卡同用这张 VALVE_STATE_TEXT 表） */
    open: C_OK,
    moving: C_WARN,
    fault: C_FAULT,
    unconfigured: C_NA,
  };
  /* 设备健康 → 色（参考 _refresh_device_table 的 color 表）。 */
  var HEALTH_COLOR = { 0: C_OK, 1: C_WARN, 2: C_WARN, 3: C_OFFLINE, 4: C_FAULT };

  function colorVar(name) {
    /* 取 CSS 变量的实际值（canvas 画盘用；DOM style 可直接用 var()）。 */
    return getComputedStyle(document.documentElement)
      .getPropertyValue(name).trim() || "#98A2AC";
  }

  /* 数值字号 → CSS。
   * [为什么写 px 不写 rem] 参考端 StatBlock 的读数字号走
   * theme.value_label_qss(color, size) —— **字面 px**（widgets.py:790），
   * 不随界面缩放变化 ⇒ web 侧同样用字面 px（rem 会跟着根字号缩放）。 */
  function px2rem(px) { return px + "px"; }

  function fmt2(v) {
    return (typeof v === "number" && isFinite(v)) ? v.toFixed(2) : "--";
  }

  /* [显示渐变 2026-11-24] 公共色带在 util.js（HSF.gradientColor，多个页面
   * 共用）；本地别名便于阅读。 */
  var gradientColor = HSF.gradientColor;

  function fmt3(v) {
    return (typeof v === "number" && isFinite(v)) ? v.toFixed(3) : "--";
  }

  /** 同值早退写文本（稳态零重排）。 */
  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  /** 同值早退上色。 */
  function setColor(node, color) {
    if (node.dataset.c === color) return;
    node.dataset.c = color;
    node.style.color = color;
  }

  /* ======================================================================
   *                          小构件
   * ====================================================================== */

  /** 数值块：上方灰色标题 + 下方大字读数 + 单位（参考 widgets.StatBlock）。 */
  function statBlock(caption, unit, valuePx) {
    var cap = HSF.el("div.stat-caption", { text: caption });
    var value = HSF.el("div.stat-value", { text: "--" });
    value.style.fontSize = px2rem(valuePx);
    var unitEl = HSF.el("span.stat-unit", { text: unit });
    /* [单位同字号 2026-11-14] 与参考端一致：单位跟数值同大。 */
    unitEl.style.fontSize = px2rem(valuePx);
    var root = HSF.el("div.stat", null, [cap,
      HSF.el("div.stat-row", null, [value, unitEl])]);
    return {
      root: root,
      set: function (text, color, newUnit) {
        setText(value, text);
        setColor(value, color || C_TEXT);
        if (newUnit !== undefined) setText(unitEl, newUnit);
      },
      setCaption: function (text) { setText(cap, text); },
    };
  }

  /**
   * 压差卡的一栏（2026-11-23 用户口径「新增两台压力传感器并入压差卡做
   * 三栏」）：小标签 + 数值 + 单位 MPa + 恒占位的说明行。
   *
   * [说明行恒占位] 有 ew 说明就上警示色，没有就清空文本——**绝不切
   * display/加 .hidden**：三栏卡每拍都在刷，显隐会改卡片高度 ⇒ 整排
   * 卡片跳动（项目铁律：内容可变、几何不可变）。
   */
  /** 一行式压力读数：标签 + 读数 + 单位**同一行**（2026-11-23 用户口径
   * 「压差 数值 单位写在一行，另外的两个压力一样的布局分别两行，总共
   * 三行」）。三行放进 .press-rows（CSS grid：标签列对齐、读数占余宽）。 */
  function pressureRow(label) {
    var value = HSF.el("div.press-value", { text: "--" });
    var unit = HSF.el("span.press-unit", { text: "MPa" });
    var lastNote = "";
    var root = HSF.el("div.press-row", null, [
      HSF.el("div.press-label", { text: label }),
      HSF.el("div.press-reading", null, [value, unit]),
    ]);
    return {
      root: root,
      note: function () { return lastNote; },
      set: function (text, color, newUnit, noteText) {
        setText(value, text);
        setColor(value, color || C_TEXT);
        setText(unit, newUnit || "MPa");
        lastNote = noteText || "";
      },
    };
  }

  /** 「● 状态」色点格：状态行 + 名字小字（参考电磁阀卡/按钮组卡格子）。 */
  function dotCell() {
    var state = HSF.el("div.dot-state", { text: "● --" });
    state.style.color = C_NA;
    var name = HSF.el("div.dot-name", { text: "-" });
    var root = HSF.el("div.dot-cell", null, [state, name]);
    return {
      root: root,
      set: function (text, color, newName) {
        setText(state, "● " + text);
        setColor(state, color);
        setText(name, newName);
      },
    };
  }

  /* ======================================================================
   *                  剩余时间表盘（参考 widgets.RemainDial）
   * ====================================================================== */

  var DIAL_ARC_START_DEG = 225;   /* Qt 角制；缺口朝下 */
  var DIAL_ARC_SPAN_DEG = 270;

  function remainDial(sideRem) {
    var canvas = HSF.el("canvas.auto-dial");
    canvas.style.width = sideRem;
    canvas.style.height = sideRem;
    var ctx = canvas.getContext("2d");
    var elapsed = -1, total = 0, capText = "";

    function fmtMs(ms) {
      var secs = Math.max(0, Math.floor((ms || 0) / 1000));
      var h = Math.floor(secs / 3600), m, s;
      secs %= 3600; m = Math.floor(secs / 60); s = secs % 60;
      function pad(n) { return (n < 10 ? "0" : "") + n; }
      return h ? (h + ":" + pad(m) + ":" + pad(s)) : (pad(m) + ":" + pad(s));
    }

    function draw() {
      var side = canvas.clientWidth || 126;
      var dpr = window.devicePixelRatio || 1;
      if (canvas.width !== Math.round(side * dpr)) {
        canvas.width = Math.round(side * dpr);
        canvas.height = Math.round(side * dpr);
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, side, side);
      var pad = Math.max(3, side * 0.09);
      var ringW = Math.max(3, side * 0.09);
      var cx = side / 2, cy = side / 2;
      var r = (side - 2 * pad) / 2;
      var a0 = (DIAL_ARC_START_DEG - 90) * Math.PI / 180;  /* Qt→canvas 角制 */
      var span = DIAL_ARC_SPAN_DEG * Math.PI / 180;

      /* 底盘整圆（对齐 C 端：缺口与环内透盘面色）。 */
      ctx.beginPath();
      ctx.arc(cx, cy, r + ringW / 2, 0, Math.PI * 2);
      ctx.fillStyle = colorVar("--panel-bg");
      ctx.fill();

      /* 轨道 270°（从起点顺时针，Qt 负 span = canvas 正方向）。 */
      ctx.beginPath();
      ctx.arc(cx, cy, r, a0, a0 + span);
      ctx.strokeStyle = colorVar("--border-soft");
      ctx.lineWidth = ringW;
      ctx.lineCap = "round";
      ctx.stroke();

      var bigText = "--";
      var caption = "";
      var ringColor = colorVar("--offline");
      var textColor = colorVar("--offline");
      if (elapsed >= 0 && total > 0) {
        var remain = Math.max(0, total - elapsed);
        var frac = Math.min(1, Math.max(0, remain / total));
        ringColor = remain > 60000 ? colorVar("--accent-line")
          : (remain > 0 ? colorVar("--warn") : colorVar("--fault"));
        ctx.beginPath();
        ctx.arc(cx, cy, r, a0, a0 + span * frac);
        ctx.strokeStyle = ringColor;
        ctx.stroke();
        bigText = fmtMs(remain);
        caption = "剩余";
        textColor = colorVar("--text");
      } else if (elapsed >= 0) {
        /* 无界（2026-11-13 定案）：单模式运行/循环产水无兜底 ⇒ 灰环 +
         * 「本次运行」时长，格式与状态行 fmtDuration 同款（N天 HH时 MM分
         * SS秒）；caption 为空 = 起点未知回退显示服务端累计（不加标签）。 */
        bigText = HSF.fmtDuration(elapsed);
        caption = capText;
        ringColor = colorVar("--accent-line");
        textColor = colorVar("--text");
      }
      if (caption) {
        ctx.fillStyle = colorVar("--text-secondary");
        ctx.font = Math.max(9, Math.round(side * 0.125)) +
          'px "Microsoft YaHei", sans-serif';
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(caption, cx, side * 0.335);
      }
      /* 中央大字：无界分支是 fmtDuration 全格式（最长 999+天 23时 59分
       * 59秒），比盘宽长 ⇒ 按盘宽实测缩字号防裁字（能放得下的文本不受
       * 影响，倒计时分支字号不变）。 */
      var bigSize = Math.max(10, Math.round(side * 0.24));
      if (elapsed >= 0 && total <= 0) {
        var maxW = side - 2 * pad;
        while (bigSize > 9) {
          ctx.font = "bold " + bigSize + 'px "Microsoft YaHei", sans-serif';
          if (ctx.measureText(bigText).width <= maxW) break;
          bigSize -= 1;
        }
      }
      ctx.fillStyle = textColor;
      ctx.font = "bold " + bigSize + 'px "Microsoft YaHei", sans-serif';
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(bigText, cx, side * 0.57);
    }

    function setState(elapsedMs, totalMs, caption) {
      elapsedMs = (elapsedMs === null || elapsedMs === undefined)
        ? -1 : elapsedMs;
      totalMs = Math.max(0, totalMs || 0);
      caption = String(caption || "");
      if (elapsedMs === elapsed && totalMs === total &&
          caption === capText) return;
      elapsed = elapsedMs;
      total = totalMs;
      capText = caption;
      draw();
    }

    return { el: canvas, setState: setState, draw: draw };
  }

  /* ======================================================================
   *                            页面模块
   * ====================================================================== */

  var activeUnmount = null;         /* 当前挂载实例的清理函数 */

  HSF.router.register(0, {
    title: "主界面",
    topics: function () {
      /* 主题单一事实源在后端（/api/pages 的 pageTopics）；拉不到时用
       * 协议常量兜底（VFD/TEMPHUM/AI/FLOWMETER/TURBIDITY/PUMP/DIGITAL/VALVE，
       * 健康=全局常驻后端无条件叠加）。 */
      var t = (HSF.registry && HSF.registry.pageTopics
               && HSF.registry.pageTopics["0"]) || [];
      return t.length ? t : [0x00010001, 0x00010002, 0x00010003,
                             0x00010004, 0x00010009, 0x0001000A,
                             0x00010006, 0x00010007];
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
    var lastModeDoc = store.get("mode") || {};
    var detailMode = "none";          /* "none" | "table" | "pump" */
    var busy = null;                  /* 在途动作："start"|"stop"|"reset" */
    /* [2026-11-27] 下拉回显改为每拍同步（syncModeSelect）⇒ 原上升沿
     * 状态位 cycleWasRunning 已删除。 */
    /* [用户是否动过模式下拉 2026-11-18] 动过之后，服务端粘性对齐就不再
     * 覆盖操作员的选择（一次性对齐，见 refreshAutoCard）。 */
    var modeUserPicked = false;
    /* 无界「本次运行」分段计时：lastSegKey=null=尚未观测/断线作废（此时
     * 在跑只能回退服务端累计，起点未知不编造）；""=停止/有界相位；其余=
     * "<autoMode>|<single|cycle>"。segStartMs=0 ⇒ 没有有效的本地起点。 */
    var lastSegKey = null;
    var segStartMs = 0;

    /* ---- 自动模式卡（首行） ---- */
    var autoTitle = HSF.el("div.card-title.auto-title", { text: "自动模式" });
    var modeSelect = HSF.el("select.input.auto-select");
    var autoModes = (HSF.registry && HSF.registry.autoModes) ||
      [{ id: "produce", name: "产水模式" },
       { id: "backwash", name: "反洗模式" },
       { id: "rinse", name: "顺洗模式" },
       { id: "auto", name: "自动（流量循环）" }];
    autoModes.forEach(function (m) {
      var opt = HSF.el("option", { text: m.name });
      opt.value = m.id;
      modeSelect.appendChild(opt);
    });
    modeSelect.title = "选择本次启动要运行的自动模式";
    /* ---- 模式选择同步（0x0115 op=5；参考客户端同口径）----
     *
     * [为什么要发] 现场物理启动按钮用「服务端上次选中的」（记粘性 + 落盘）。
     * 网页端原先下拉纯本地 ⇒ 刷新即丢、服务端粘性与界面不同步 ⇒ 物理启动
     * 键可能按旧模式启动（参考端 bridge.send_auto_select 就是为这个）。
     *
     * [文案照参考端] 成功「已选模式：X（现场启动按钮将按此模式启动）」、
     * 失败「模式选择失败：<err>」、未连接「未连接服务端：模式选择未同步…」
     * （client_py/hsfsrv_client/pages_other.py:750-766）。
     *
     * [C 端不支持 op=5] C sim 的 0x0115 只认 1..4 ⇒ 回「非法 op 操作字」；
     * 参考客户端对 C sim **同样**失败 ⇒ 两端口径一致，如实报出即可。 */
    modeSelect.addEventListener("change", function () {
      if (!connected()) {
        HSF.statusMessage("未连接服务端：模式选择未同步（物理启动按钮将用" +
                          "服务端上次记住的模式）");
        return;
      }
      /* [用户已选 2026-11-18] 记下：一次性对齐不再覆盖操作员的选择
       * （与桌面端 pages_other._auto_mode_selected 同口径）。 */
      modeUserPicked = true;
      var mode = modeSelect.value;
      var name = modeNameOf(mode);
      HSF.statusMessage("已选模式：" + name +
                        "（现场启动按钮将按此模式启动）");
      HSF.api.autoControl({ op: 5, autoMode: mode }).then(function (doc) {
        if (doc && doc.ok === false) {
          HSF.statusMessage("模式选择失败：" +
                            ((doc && doc.err) || "未知"));
        }
      });
    });
    var autoHint = HSF.el("div.auto-hint", { text: "--" });
    autoHint.style.color = C_SUB;

    var startBtn = HSF.el("button.btn", {
      type: "button", text: "启动",
      title: "按所选模式启动：开选中阀、关未选阀，等全部到位后按配置启动" +
             "搅拌泵/进水泵（需自动模式 + 管理员）",
      onclick: function () { autoAction("start"); },
    });
    var stopBtn = HSF.el("button.btn", {
      type: "button", text: "停止",
      title: "停止自动运行并停全部设备",
      onclick: function () { autoAction("stop"); },
    });
    var resetBtn = HSF.el("button.btn", {
      type: "button", text: "复位",
      title: "清除报警锁存与急停锁存（复位后才能再按启动；不会自动启动）",
      onclick: function () { autoAction("reset"); },
    });
    var btnRow = HSF.el("div.auto-btns", null,
                        [startBtn, stopBtn, resetBtn]);

    var runTitle = HSF.el("span.auto-run-title", { text: "累计" });
    var runLabel = HSF.el("span.auto-run-label", { text: "--" });
    runTitle.style.color = C_SUB;
    runLabel.style.color = C_SUB;
    var runRow = HSF.el("div.auto-runrow", null, [runTitle, runLabel]);

    var dial = remainDial("9rem");
    dial.el.classList.add("hidden");

    /* ---- 阶段信息行（2026-11-19 用户定案「主页补一行阶段信息」）----
     * [不是新卡] 首页已有「自动模式」卡（模式/按钮/表盘/累计）⇒ 再来一张
     * 「自动运行」卡是**重复表达** ⇒ 阶段信息作为既有卡里的一行补上。
     * [与表盘不重复] 表盘是**倒计时**（剩余）；本行是**本阶段已用/总**。
     * [手动模式隐藏] 手动模式"第几阶段"无意义 ⇒ 整行隐藏（与参考端同款）。
     * [口径逐字对齐参考端 pages_other.HomePage._refresh_phase_row] */
    /* [进度条已删 2026-11-19 用户口径「那个进度条删掉」] 阶段行 =
     * 阶段名 + 已用时长文字（与桌面客户端同款，两端逐字对齐）。 */
    var phaseLabel = HSF.el("span.auto-phase-label", { text: "--" });
    var phaseText = HSF.el("span.auto-phase-text", { text: "--" });
    var phaseRow = HSF.el("div.auto-phase-row", null,
                          [phaseLabel, phaseText]);

    var autoCard = HSF.el("div.card.auto-card", null, [
      HSF.el("div.auto-grid", null, [
        autoTitle,
        HSF.el("div.auto-modeline", null, [
          HSF.el("span.auto-modelabel", { text: "模式" }), modeSelect]),
        autoHint,
        dial.el,
        btnRow,
        runRow,
        phaseRow,
      ]),
    ]);

    /* ---- 电箱温度卡（2026-11-19 重排：挪到第三排，字号降一档）---- */
    /* [读数字号] 逐值照参考端 HomePage 的 StatBlock value_size
     * （pages_other.py:317-350）：电箱温湿度 **32**（2026-11-19 从 40 降
     * 一档）/ 运行状态 30 / 输出频率 40 / 转速 26 / 功率·转矩 26 /
     * 设备总数·故障·离线 34。
     * [为什么降档] 用户「电箱温湿度不需要这么大的卡片」⇒ 两个 40px 的数
     * 把卡撑成 122px（占着两行高的大卡位）⇒ 换到第三排的矮卡位。 */
    var thTemp = statBlock("电箱温度", "℃", 32);
    var thHum = statBlock("电箱湿度", "%RH", 32);
    var thCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "电箱温度" }),
      HSF.el("div.stat-grid.cols-2", null, [thTemp.root, thHum.root]),
    ]);

    var vfdState = statBlock("运行状态", "", 30);
    /* [第三项=转速 2026-11-17 用户实报] 原「设定频率」⇒ 主页一览卡显示
     * **实际转速**（现场铭牌 50Hz≈1440rpm ⇒ 28.8 rpm/Hz，按输出频率换算；
     * 变频器无转速寄存器，属换算值）。「设定频率」仍在泵详情悬浮卡里。
     * [与桌面客户端/C 端同口径] C 端 HsfHomePage.c 的 HOME_TXT_VFD_SET
     * 已是「转速」（HOME_RPM_PER_HZ=1440/50）；桌面客户端 2026-11-18 补齐
     * ⇒ 三端主页一致。 */
    /* [转速升为主数值 + 与频率互调 2026-11-23 用户口径「搅拌泵的输出频率和
     * 转速位置互调下，现在频率字大，但一般关心的是转速」] ⇒ 转速 40px 占
     * 主位（第二格，原频率的位置），输出频率 26px 退居第三格。与桌面客户端
     * 同批对调（三端文字口径仍一致）。 */
    var vfdRpm = statBlock("转速", "rpm", 40);
    var vfdFreq = statBlock("输出频率", "Hz", 26);
    /* [第四/五项=功率/转矩 2026-11-19 用户实报] 主页搅拌泵卡补
     * **输出功率(kW)** 与 **转矩(%)** —— 现场看搅拌泵最常问的三个数就是
     * "转没转 / 多快 / 出多大力"，功率与转矩正好补上后两个。
     * [数据已有] 服务端 VFD 快照带 outputPowerKw / torquePercent
     * ⇒ **不新增协议**，只是没显示出来（与参考端逐字同款）。 */
    var vfdPower = statBlock("输出功率", "kW", 26);
    var vfdTorque = statBlock("转矩", "%", 26);
    /* [第六项=电机温度 2026-11-20 用户口径「搅拌泵卡片可以增加温度显示
     * 了，就是那个模拟量的温度计」] 服务端随 VFD 遥测下发 motorTempC
     * （模拟量温度传感器，通道远端可配）。缺键 ⇒ "--"（不编 0℃）。 */
    var vfdMtemp = statBlock("电机温度", "℃", 26);
    var vfdCard = HSF.el("button.card.card-click", {
      type: "button", title: "点击查看搅拌泵详情",
      onclick: function () { toggleDetail("pump"); },
    }, [
      HSF.el("div.card-title", { text: "搅拌泵" }),
      HSF.el("div.stat-grid.cols-3", null,
             [vfdState.root, vfdRpm.root, vfdFreq.root]),
      HSF.el("div.stat-grid.cols-3", null,
             [vfdPower.root, vfdTorque.root, vfdMtemp.root]),
    ]);

    var hTotal = statBlock("设备总数", "台", 34);
    var hFault = statBlock("故障", "台", 34);
    var hOffline = statBlock("离线", "台", 34);
    var healthCard = HSF.el("button.card.card-click", {
      type: "button", title: "点击查看设备状态表",
      onclick: function () { toggleDetail("table"); },
    }, [
      /* [改名 2026-11-23 用户口径「改名设备及仪表」] 与桌面客户端同口径。 */
      HSF.el("div.card-title", { text: "设备及仪表" }),
      HSF.el("div.stat-grid.cols-3", null,
             [hTotal.root, hFault.root, hOffline.root]),
    ]);

    /* ---- 第二排：压差（压差 / 压力1 / 压力2 三栏） / 流量计 / 电动球阀 ----
     * [三栏 2026-11-23 用户口径] 新增两台压力传感器（名字「压力1」「压力2」，
     * MPa）并入现有「压差」卡：卡标题保持「压差」，内容等分三栏。三栏的
     * 数值都来自模拟量快照的**同一批通道**（服务端按 `en{i}` 给名字，
     * 通道号远端可配 ⇒ 客户端**按名字找**，不写死下标）。 */
    /* [布局第四版 2026-11-23 用户口径「压差 数值 单位写在一行，另外的
     * 两个压力一样的布局分别两行，总共三行」] 三行同构：每行 = 标签 +
     * 读数 + 单位（.press-row）；.press-rows 用 grid ⇒ 三个标签成列对齐，
     * 读数占余宽。 */
    var dpCol = pressureRow("压差");
    var p1Col = pressureRow("进水压力");
    var p2Col = pressureRow("出水压力");
    /* [共享说明行] 三路 ew 说明合并成卡底一行（无说明时收起不占行，
     * 有说明上警示色；超长省略、title 看全）。 */
    var dpNote = HSF.el("div.press-note.press-note-shared", { text: "" });
    var dpCard = HSF.el("div.card.press-card", null, [
      /* [改名 2026-11-23 用户口径「卡片名字修改为压力及压差」] 卡里既有
       * 压差又有压力1/压力2 ⇒ 标题如实叫「压力及压差」。 */
      HSF.el("div.card-title", { text: "压力及压差" }),
      HSF.el("div.press-rows", null,
             [dpCol.root, p1Col.root, p2Col.root]),
      dpNote,
    ]);

    var fmFlow = statBlock("瞬时流量", "", 40);
    /* [滤速 2026-11-25 用户「增加滤速显示，单位 m/h，公式瞬时流量/面积」]
     * 面积在远端设置配；服务端随流量计帧下发 filterVelocityMh。 */
    var fmVel = statBlock("罐体滤速", "m/h", 28);
    fmVel.root.title = "罐体滤速 = 瞬时流量 ÷ 过滤面积（面积在远端设置 → 采集配置里配）";
    var fmRow = HSF.el("div.fm-row", null, [fmFlow.root, fmVel.root]);
    var fmSub = HSF.el("div.card-note", { text: "无数据" });
    var fmCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "流量计" }),
      fmRow, fmSub,
    ]);

    var valveCells = [];
    var valveGrid = HSF.el("div.dot-grid.cols-3");
    for (var vi = 0; vi < VALVE_COUNT; vi++) {
      var cell = dotCell();
      valveCells.push(cell);
      valveGrid.appendChild(cell.root);
    }
    var valveCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "电动球阀" }), valveGrid]);

    /* ---- 第三排：浊度仪 / 按钮组系统状态 ---- */
    var turbBlocks = [];
    var turbTemps = [];
    var turbGrid = HSF.el("div.stat-grid.cols-2");
    for (var ti = 0; ti < TURBIDITY_COUNT; ti++) {
      /* [数字放大 2026-11-23 用户「浊度仪卡片的数字可以大点」] 32 → 36；
       * [900 档回落 32 2026-11-24 多尺寸门禁实测] 900 宽三列卡内容宽
       * 311.5px，36px 两列需求 335.3px ⇒ 卡片横向溢出（347>336）。窗口
       * 窄于 1200 时回落 32px（该档基线不溢出），宽屏仍 36px。 */
      var turbPx = (window.innerWidth < 1200) ? 32 : 36;
      var block = statBlock("浊度仪" + (ti + 1), "NTU", turbPx);
      var temp = HSF.el("div.card-note.hidden");
      var wrap = HSF.el("div", null, [block.root, temp]);
      turbBlocks.push(block);
      turbTemps.push(temp);
      turbGrid.appendChild(wrap);
    }
    var turbNote = HSF.el("div.card-note.hidden");
    var turbCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "浊度仪" }),
      turbGrid, turbNote]);

    var btnCells = [];
    var btnGrid = HSF.el("div.dot-grid.cols-4");
    for (var bi = 0; bi < BTN_SLOT_COUNT; bi++) {
      var bcell = dotCell();
      bcell.set("--", C_NA, BTN_SLOT_LABELS[bi]);
      btnCells.push(bcell);
      btnGrid.appendChild(bcell.root);
    }
    var btnNote = HSF.el("div.card-note.hidden");
    var btnCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "按钮组系统状态" }),
      btnGrid, btnNote]);

    /* ---- 最近报警卡（2026-11-19 用户定案「主页加一张最近报警卡」）----
     * [主页确实缺这块] 现场最常问的两件事之一就是"刚才发生了什么" ⇒
     * 主页给一行摘要（时间 + 类型 + 原文）。
     * [数据源] 服务端状态快照的 lastAlarm（web 后端 model.last_alarm）。
     * [文案逐字对齐参考端 pages_other.HomePage._refresh_alarm_card] */
    var alarmMeta = HSF.el("div.alarm-meta", { text: "--" });
    var alarmText = HSF.el("div.alarm-text", { text: "无报警" });
    /* [点卡=展开；展开后给一条「查看全部报警」去报警页] 网页端没有
     * 桌面端那种"跳页"的天然手势（网页上点卡更自然是展开内容）⇒ 点卡
     * 展开原文（默认 3 行截断 → 展开后全显），再点收起；展开后额外给
     * 一条链接直达报警页（全量历史在那边）。
     * [为什么不用 button 套 link] HTML 不允许 button 里嵌 button/a ⇒
     * 卡做成 div + role=button（可聚焦、可回车展开）。 */
    var alarmMore = HSF.el("a.alarm-more", {
      text: "查看全部报警 »", href: "#",
      onclick: function (ev) {
        ev.stopPropagation();
        HSF.router.go(11);
      },
    });
    alarmMore.style.display = "none";
    var alarmCard = HSF.el("div.card.alarm-card", {
      role: "button", tabindex: "0",
      title: "点击展开报警原文",
      onclick: function () { toggleAlarmExpand(); },
      onkeydown: function (ev) {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          toggleAlarmExpand();
        }
      },
    }, [
      HSF.el("div.card-title", { text: "最近报警" }),
      alarmMeta, alarmText, alarmMore,
    ]);
    var alarmExpanded = false;

    function toggleAlarmExpand() {
      alarmExpanded = !alarmExpanded;
      alarmCard.classList.toggle("expanded", alarmExpanded);
      alarmMore.style.display = alarmExpanded ? "inline-block" : "none";
      alarmCard.title = alarmExpanded ? "点击收起" : "点击展开报警原文";
    }

    /* ---- 悬浮详情卡（设备状态表 / 搅拌泵详情二选一） ---- */
    var detailTable = HSF.el("table.table.detail-table");
    var thead = HSF.el("thead");
    var headRow = HSF.el("tr");
    ["设备", "类型", "状态", "地址", "错误信息"].forEach(function (h) {
      headRow.appendChild(HSF.el("th", { text: h }));
    });
    thead.appendChild(headRow);
    var tbody = HSF.el("tbody");
    detailTable.appendChild(thead);
    detailTable.appendChild(tbody);

    var pdState = statBlock("运行状态", "", 24);
    var pdFreq = statBlock("输出频率", "Hz", 24);
    var pdSetf = statBlock("设定频率", "Hz", 24);
    var pdCur = statBlock("输出电流", "A", 24);
    var pdRpm = statBlock("搅拌转速", "rpm", 24);
    /* [功率/转矩 2026-11-19] 详情浮层补上 ⇒ 与 C 端详情卡字段齐平
     * （与参考端 pages_other.HomePage 的 pd_power / pd_torque 同款）。 */
    var pdPower = statBlock("输出功率", "kW", 24);
    var pdTorque = statBlock("转矩", "%", 24);
    var pumpDetail = HSF.el("div.pump-detail", null, [
      HSF.el("div.stat-grid.cols-3", null,
             [pdState.root, pdFreq.root, pdSetf.root]),
      HSF.el("div.stat-grid.cols-3.left", null,
             [pdCur.root, pdRpm.root, pdPower.root]),
      HSF.el("div.stat-grid.cols-2.left", null, [pdTorque.root]),
    ]);

    var detailTitle = HSF.el("span.float-title", { text: "设备状态" });
    var detailClose = HSF.el("button.float-close", {
      type: "button", text: "×", title: "收起",
      onclick: function () { setDetail("none"); },
    });
    var floatCard = HSF.el("div.home-float.hidden", null, [
      HSF.el("div.float-head", null, [detailTitle, detailClose]),
      detailTable, pumpDetail,
    ]);
    var shade = HSF.el("div.home-shade.hidden", {
      onclick: function () { setDetail("none"); },
    });

    /* [设备单行详情 2026-11-22 用户口径「显示不全，单击一行弹一张卡片」]
     * 设备状态表只有 5 列且浮层宽度有限：地址（完整 ip:port/…）与错误信息
     * （多路故障长串）必然省略号。点行用原生 <dialog> 弹一张详情卡，字段
     * 原文换行显示（可选中复制）；Esc/关闭按钮收起。 */
    var ddTitle = HSF.el("div.device-dd-title", { text: "--" });
    var ddGrid = HSF.el("div.device-dd-grid");
    var ddClose = HSF.el("button.btn", {
      type: "button", text: "关闭",
      onclick: function () { deviceDialog.close(); },
    });
    var deviceDialog = HSF.el("dialog.device-detail-dialog", null, [
      ddTitle, ddGrid,
      HSF.el("div.device-dd-foot", null, [ddClose]),
    ]);

    function showDeviceDetail(d) {
      setText(ddTitle, d.name || "--");
      HSF.replaceChildren(ddGrid);
      var fields = [
        ["类型", d.kindText || "--"],
        ["状态", d.healthText || "--"],
        ["地址", d.addrFull || d.addr || "-"],
        ["错误信息", d.text || "-"],
      ];
      fields.forEach(function (kv) {
        ddGrid.appendChild(HSF.el("div.device-dd-key", { text: kv[0] }));
        ddGrid.appendChild(HSF.el("div.device-dd-value",
                                  { text: String(kv[1]) }));
      });
      if (typeof deviceDialog.showModal === "function") {
        if (!deviceDialog.open) deviceDialog.showModal();
      } else {
        deviceDialog.setAttribute("open", "open");   /* 老浏览器兜底 */
      }
    }

    var page = HSF.el("div.home-page", null, [
      autoCard,
      /* [三排按实测卡高重排 2026-11-19] 板端 1280x800@150% 实测各卡自然高：
       *   出水浊度仪06 / 按钮组202 / 搅拌泵194 / 压差153 / 流量计153 /
       *   电动球阀147 / 设备健康113 / 电箱温度111 / 最近报警97。
       *   ⇒ 每排三张、**按高度从高到低**，同排卡高差最小：
       *     第一排 浊度仪 ｜ 按钮组 ｜ 压差        （排内差 53）
       *     第二排 搅拌泵 ｜ 流量计 ｜ 电动球阀    （排内差 47）
       *     第三排 设备健康 ｜ 电箱温度 ｜ 最近报警（排内差 16）
       *   ⇒ 三排合计 513 + 自动模式卡 ⇒ 内容自然高 665（待机）/697（自动）
       *     vs 视口 720 ⇒ **不出滚动条**，末排不留大空。
       *   [与桌面客户端同排布] client_py pages_other.HomePage 同款
       *   （「电箱温度」挪到矮卡位、「浊度仪」挪到高卡位）。 */
      /* [2026-11-23 用户口径「按钮与电动球阀的卡片交换位置」] 默认排布
       * 第一排 = 浊度仪/电动球阀/压力及压差，第二排 = 搅拌泵/流量计/
       * 按钮组系统状态；用户还可长按拖拽自定义（顺序存 localStorage，
       * 见下方 applyCardOrder / 长按拖拽段）。 */
      HSF.el("div.home-cards.cols-3", null, [turbCard, valveCard, dpCard]),
      HSF.el("div.home-cards.cols-3.row2", null,
             [vfdCard, fmCard, btnCard]),
      HSF.el("div.home-cards.cols-3.row3", null,
             [thCard, healthCard, alarmCard]),
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);
    host.appendChild(shade);
    host.appendChild(floatCard);
    host.appendChild(deviceDialog);

    /* ======================================================================
     *        看板卡片数值自动缩放（2026-11-23 用户「文字自动缩放下，
     *        减少空白」）
     * ======================================================================
     * [口径] 同一排卡片等高拉伸后，短卡底部会留白 ⇒ 按**剩余高度**把数值
     * 字号放大约 1.3× 以内填满。放大后若内容溢出卡片（scrollHeight >
     * clientHeight）就整卡回退基准 —— 保证不把这一排撑高（避免布局循环）。
     * 只在本页显示 / 窗口尺寸变化 / 卡片重排后跑一次（rAF 合并）。 */
    var fitBoardPending = false;

    function valueElsOf(card) {
      return Array.prototype.slice.call(
        card.querySelectorAll(".stat-value, .press-value"));
    }

    /** 卡内"已用高度"（最后一个**可见**子控件的下沿，相对卡片内容区）。
     *  [为什么不用 scrollHeight] 卡片是 flex 列容器：内容比卡矮时
     *  scrollHeight 仍等于 clientHeight（永远量不到空白）⇒ 必须按子控件
     *  的实际下沿量。 */
    function contentBottom(card) {
      var cs = window.getComputedStyle(card);
      var top = card.getBoundingClientRect().top
        + (parseFloat(cs.paddingTop) || 0);
      var bottom = top;
      for (var i = 0; i < card.children.length; i++) {
        var child = card.children[i];
        var r = child.getBoundingClientRect();
        if (r.width <= 0 && r.height <= 0) continue;   /* display:none */
        bottom = Math.max(bottom, r.bottom);
      }
      return bottom - top;
    }

    function fitBoardText() {
      var cards = page.querySelectorAll(".home-board-card");
      for (var ci = 0; ci < cards.length; ci++) {
        var card = cards[ci];
        var els = valueElsOf(card);
        if (!els.length) continue;
        /* 取基准字号（首次记为当前计算值，用 px 落到 dataset）。 */
        for (var ei = 0; ei < els.length; ei++) {
          var el = els[ei];
          if (!el.dataset.basePx) {
            el.dataset.basePx = String(parseFloat(
              window.getComputedStyle(el).fontSize) || 0);
          }
          el.style.fontSize = el.dataset.basePx + "px";
        }
        var cs = window.getComputedStyle(card);
        var padB = parseFloat(cs.paddingBottom) || 0;
        var avail = card.clientHeight - padB - (parseFloat(cs.paddingTop) || 0);
        var used = contentBottom(card);
        var spare = avail - used;
        if (spare < 12) continue;
        if (card.scrollWidth > card.clientWidth + 1) continue;  /* 已贴满宽 */
        var content = Math.max(1, used);
        var factor = Math.min(1.30, 1 + (spare / content) * 0.9);
        if (factor <= 1.03) continue;
        /* 压力卡的三行标签跟数值**同比例**放大（用户「压差/压力1/压力2
         * 这几个字也能同步大点」）：标签基准取自身计算字号（CSS 已把它
         * 提到 captions×1.35），放大时乘同一个 factor。 */
        var labels = Array.prototype.slice.call(
          card.querySelectorAll(".press-label, .press-unit"));
        for (var lj = 0; lj < labels.length; lj++) {
          if (!labels[lj].dataset.basePx) {
            labels[lj].dataset.basePx = String(parseFloat(
              window.getComputedStyle(labels[lj]).fontSize) || 0);
          }
          labels[lj].style.fontSize = labels[lj].dataset.basePx + "px";
        }
        for (var ej = 0; ej < els.length; ej++) {
          var base = parseFloat(els[ej].dataset.basePx) || 0;
          if (base > 0) {
            els[ej].style.fontSize = (base * factor) + "px";
          }
        }
        for (var lk = 0; lk < labels.length; lk++) {
          var lbase = parseFloat(labels[lk].dataset.basePx) || 0;
          if (lbase > 0) {
            labels[lk].style.fontSize = (lbase * factor) + "px";
          }
        }
        if (contentBottom(card) > avail + 1
            || card.scrollWidth > card.clientWidth + 1) {
          /* 放大撑破卡片 ⇒ 整卡回退（宁可留白，不裁字/不顶大行高）。 */
          for (var ek = 0; ek < els.length; ek++) {
            els[ek].style.fontSize = els[ek].dataset.basePx + "px";
          }
          for (var lr = 0; lr < labels.length; lr++) {
            labels[lr].style.fontSize = labels[lr].dataset.basePx + "px";
          }
        }
      }
    }

    function scheduleFitBoardText() {
      if (fitBoardPending) return;
      fitBoardPending = true;
      window.requestAnimationFrame(function () {
        fitBoardPending = false;
        fitBoardText();
      });
    }
    window.addEventListener("resize", scheduleFitBoardText);
    window.setTimeout(scheduleFitBoardText, 0);

    /* ======================================================================
     *        看板卡片顺序：默认换位 + 长按拖拽自助排序（2026-11-23）
     * ======================================================================
     * [用户口径] 「按钮与电动球阀的卡片交换位置，支持下主界面长按可以拖拽
     * 卡片进行自己排序，并本地持久化记录位置」。
     * [本地持久化] 网页端 = localStorage（每台设备/浏览器各存一份，与桌面
     * 客户端的 INI 同口径：只影响这一台机器）。键存 9 个稳定 id，显示名换
     * 了也不影响；值不是 9 个 id 的合法排列 ⇒ 回默认（fail-closed，避免
     * "少一张卡"这种静默损坏）。
     * [长按判定] 550ms 内位移 ≤10px 才算长按；先动就先算滚动（不抢页面
     * 上下滑）。拖拽期间 touchmove 一律 preventDefault ⇒ 页面不跟着滚。
     * [松手] 落在另一张卡上 = 两张卡**交换位置**并立即落盘；落空白 = 取消。
     * 拖拽后 350ms 内吞掉 click（卡片本身可能绑定点击行为）。 */
    var BOARD_ORDER_KEY = "hsf.home.cardOrder";
    var BOARD_CARD_IDS = ["turb", "valve", "dp", "vfd", "fm", "btn",
                          "th", "health", "alarm"];
    var CARD_BY_ID = {
      turb: turbCard, valve: valveCard, dp: dpCard,
      vfd: vfdCard, fm: fmCard, btn: btnCard,
      th: thCard, health: healthCard, alarm: alarmCard,
    };
    BOARD_CARD_IDS.forEach(function (id) {
      CARD_BY_ID[id].dataset.cardId = id;
      CARD_BY_ID[id].classList.add("home-board-card");
    });
    var boardRows = Array.prototype.slice.call(
      page.querySelectorAll(".home-cards"));

    function loadCardOrder() {
      try {
        var raw = window.localStorage.getItem(BOARD_ORDER_KEY) || "";
        var arr = raw.split(",").filter(function (x) { return !!x; });
        if (arr.length === BOARD_CARD_IDS.length
            && BOARD_CARD_IDS.every(function (id) {
                 return arr.indexOf(id) >= 0;
               })) {
          return arr;
        }
      } catch (err) { /* localStorage 被禁用（隐私模式）⇒ 用默认 */ }
      return BOARD_CARD_IDS.slice();
    }

    function applyCardOrder(order) {
      boardRows.forEach(function (row) {
        while (row.firstChild) row.removeChild(row.firstChild);
      });
      order.forEach(function (id, i) {
        var card = CARD_BY_ID[id];
        if (card) boardRows[Math.floor(i / 3)].appendChild(card);
      });
      boardOrder = order.slice();
      if (typeof scheduleFitBoardText === "function") {
        scheduleFitBoardText();
      }
    }

    function saveCardOrder(order) {
      try {
        window.localStorage.setItem(BOARD_ORDER_KEY, order.join(","));
      } catch (err) { /* 存不了也不影响本次会话 */ }
    }

    var boardOrder = loadCardOrder();
    applyCardOrder(boardOrder);

    /** 拖拽状态机（长按 → 跟手 → 松手交换）。 */
    var dragState = {
      card: null, id: null, pointerId: null,
      startX: 0, startY: 0, timer: null,
      ghost: null, target: null, suppressUntil: 0,
    };

    function cardOfNode(node) {
      while (node && node !== page) {
        if (node.classList && node.classList.contains("home-board-card")
            && node.dataset.cardId) {
          return node;
        }
        node = node.parentNode;
      }
      return null;
    }

    function isDragging() { return !!dragState.card && !!dragState.ghost; }

    function clearDragVisuals() {
      if (dragState.ghost && dragState.ghost.parentNode) {
        dragState.ghost.parentNode.removeChild(dragState.ghost);
      }
      dragState.ghost = null;
      if (dragState.target) {
        dragState.target.classList.remove("card-drop-target");
      }
      dragState.target = null;
    }

    function resetPress() {
      if (dragState.timer) {
        window.clearTimeout(dragState.timer);
        dragState.timer = null;
      }
      dragState.card = null;
      dragState.id = null;
      dragState.pointerId = null;
    }

    function beginLongPress(card, ev) {
      dragState.card = card;
      dragState.id = card.dataset.cardId;
      dragState.pointerId = ev.pointerId;
      dragState.startX = ev.clientX;
      dragState.startY = ev.clientY;
      dragState.timer = window.setTimeout(function () {
        dragState.timer = null;
        if (!dragState.card) return;
        var rect = dragState.card.getBoundingClientRect();
        var ghost = dragState.card.cloneNode(true);
        ghost.classList.add("card-drag-ghost");
        ghost.style.width = rect.width + "px";
        ghost.style.height = rect.height + "px";
        ghost.style.left = rect.left + "px";
        ghost.style.top = rect.top + "px";
        document.body.appendChild(ghost);
        dragState.ghost = ghost;
        dragState.card.classList.add("card-dragging");
      }, 550);
    }

    function updateGhost(ev) {
      if (!isDragging()) return;
      var g = dragState.ghost;
      var w = g.offsetWidth, h = g.offsetHeight;
      g.style.left = (ev.clientX - w / 2) + "px";
      g.style.top = (ev.clientY - h / 2) + "px";
      /* 目标卡：当前指针下、不是自己 ⇒ 高亮（虚线框）。 */
      var el = document.elementFromPoint(ev.clientX, ev.clientY);
      var card = cardOfNode(el);
      if (card === dragState.card) card = null;
      if (dragState.target && dragState.target !== card) {
        dragState.target.classList.remove("card-drop-target");
      }
      dragState.target = card;
      if (card) card.classList.add("card-drop-target");
    }

    function finishDrag(ev) {
      var dragged = dragState.id;
      var target = null;
      if (ev) {
        var el = document.elementFromPoint(ev.clientX, ev.clientY);
        var card = cardOfNode(el);
        if (card && card !== dragState.card) {
          target = card.dataset.cardId;
        }
      }
      clearDragVisuals();
      if (dragState.card) dragState.card.classList.remove("card-dragging");
      resetPress();
      if (dragged && target && dragged !== target) {
        var order = boardOrder.slice();
        var ia = order.indexOf(dragged), ib = order.indexOf(target);
        if (ia >= 0 && ib >= 0) {
          order[ia] = target;
          order[ib] = dragged;
          applyCardOrder(order);
          saveCardOrder(order);
        }
        dragState.suppressUntil = Date.now() + 350;
      }
    }

    page.addEventListener("pointerdown", function (ev) {
      if (ev.pointerType === "mouse" && ev.button !== 0) return;
      if (isDragging()) return;
      var card = cardOfNode(ev.target);
      if (!card) return;
      beginLongPress(card, ev);
    });
    document.addEventListener("pointermove", function (ev) {
      if (!dragState.card || ev.pointerId !== dragState.pointerId) return;
      if (!isDragging()) {
        var dx = ev.clientX - dragState.startX;
        var dy = ev.clientY - dragState.startY;
        if (Math.sqrt(dx * dx + dy * dy) > 10) resetPress();   /* 先动=滚动 */
        return;
      }
      ev.preventDefault();
      updateGhost(ev);
    }, { passive: false });
    document.addEventListener("pointerup", function (ev) {
      if (!dragState.card || ev.pointerId !== dragState.pointerId) return;
      if (isDragging()) finishDrag(ev);
      else resetPress();
    });
    document.addEventListener("pointercancel", function () {
      if (isDragging()) finishDrag(null);
      else resetPress();
    });
    /* 拖拽期间拦住触摸滚动与移动端长按菜单。 */
    page.addEventListener("touchmove", function (ev) {
      if (isDragging()) ev.preventDefault();
    }, { passive: false });
    page.addEventListener("contextmenu", function (ev) {
      if (dragState.card) ev.preventDefault();
    });
    /* 拖拽结束后吞掉紧随其后的 click（卡片=可点开详情/跳页）。 */
    page.addEventListener("click", function (ev) {
      if (Date.now() < dragState.suppressUntil) {
        ev.preventDefault();
        ev.stopPropagation();
      }
    }, true);

    /* 「累计」定宽：按标签实际字体实测最长串（不写死像素，缩放后仍成立）。 */
    (function fitRunWidth() {
      var need = 0;
      try {
        var c = document.createElement("canvas").getContext("2d");
        c.font = getComputedStyle(runLabel).font;
        need = Math.ceil(c.measureText(AUTO_RUN_MAX_TEXT).width) + 8;
      } catch (err) { /* 量不了就不定宽（退化为自然宽度） */ }
      if (need > 0) runLabel.style.width = need + "px";
    })();
    runLabel.style.display = "inline-block";
    runLabel.style.textAlign = "right";

    /* ==================================================================
     *                       动作（三态终态反馈）
     * ================================================================== */
    function modeNameOf(id) {
      for (var i = 0; i < autoModes.length; i++) {
        if (autoModes[i].id === id) return autoModes[i].name;
      }
      return id || "";
    }

    /** 是否「组合循环（自动/流量循环）」上下文（运行中或暂停记忆）。
     *
     * [判据 2026-11-27 根修] 服务端运行组合循环时 `autoMode` 报的是**子
     * 阶段**（produce/backwash/rinse，server_py app._auto_state_extra 与
     * webui_check 的「组合模式运行中 autoMode == produce」断言同口径）；
     * "auto" 只在停止/复位后才回到 autoMode ⇒ 旧判据
     * `m.autoMode === "auto"` 运行中**恒假** ⇒ 下拉永远停在缺省「产水
     * 模式」（用户实报：桌面端循环已启动，网页端主界面下拉仍是产水）。
     *
     * ⇒ 以服务端新键 `autoStageTitle` 判循环：循环里非空（「产水模式2」；
     * 暂停记忆时也是该实例），单模式/老服务端为空串或缺键；`autoMode
     * === "auto"` 留作停止态/老服务端兜底。缺键 ⇒ 不编造序号。
     */
    function isCycleDoc(m) {
      m = m || {};
      if (typeof m.autoStageTitle === "string" && m.autoStageTitle) {
        return true;
      }
      return m.autoMode === "auto";
    }

    /** 循环实例标题（单模式/老服务端缺键 ⇒ ""，调用方退回模式名）。 */
    function stageTitleOf(m) {
      m = m || {};
      return (typeof m.autoStageTitle === "string") ? m.autoStageTitle : "";
    }

    function connected() {
      var link = store.get("link") || {};
      return !!link.ready;
    }

    /** 自动控制权限 + 链路就绪（与参考端 can_ctrl 同口径）。 */
    function canAutoCtrl() {
      return connected() && ctx.canPerm(HSF.app.PERM.AUTO_CONTROL);
    }

    function autoAction(kind) {
      if (!ctx.canPerm(HSF.app.PERM.AUTO_CONTROL)) {
        HSF.statusMessage("权限不足：自动运行控制需要管理员账户");
        return;
      }
      if (!connected()) {
        HSF.statusMessage("未连接服务端");
        return;
      }
      var payload;
      var busyText;
      if (kind === "start") {
        var mode = modeSelect.value;
        payload = { on: true, autoMode: mode };
        busyText = "正在启动自动运行（" + modeNameOf(mode) + "）…";
      } else if (kind === "stop") {
        payload = { on: false };
        busyText = "正在停止自动运行…";
      } else {
        payload = { op: 4 };
        busyText = "正在复位（清除报警/急停锁存）…";
      }
      HSF.statusMessage(busyText);
      busy = kind;
      applyEnabled();
      HSF.api.autoControl(payload).then(function (doc) {
        busy = null;
        var ok = !!(doc && doc.ok);
        if (kind === "start") {
          HSF.statusMessage(ok
            ? "自动运行已受理（" + modeNameOf(modeSelect.value) +
              "），设备按序列动作中"
            : "启动失败：" + ((doc && doc.err) || "未知"));
        } else if (kind === "stop") {
          HSF.statusMessage(ok ? "自动运行已停止（设备已全停）"
                               : "停止失败：" + ((doc && doc.err) || "未知"));
        } else {
          HSF.statusMessage(ok ? "已复位：报警与急停锁存已解除，可按启动"
                               : "复位失败：" + ((doc && doc.err) || "未知"));
        }
        /* 应答透传了全量状态键：写回 store 让状态行/按钮立即落到终态。 */
        if (doc) store.applyEvent("mode", doc);
        applyEnabled();
      });
    }

    function applyEnabled() {
      var m = lastModeDoc;
      var canCtrl = canAutoCtrl();
      var running = m.autoRunning;           /* null = 未知 */
      var fault = !!m.autoFault;
      startBtn.disabled = busy === "start" || !canCtrl ||
        !m.auto || running === true || fault;
      stopBtn.disabled = busy === "stop" || !canCtrl || running !== true;
      var estopLatched = (m.autoPhase === "estop");
      /* [暂停态 2026-11-27 用户「停止暂停时复位没法按，无法复位状态」]
       * 停止＝暂停 ⇒ 只有复位能清暂停记忆从头（口径与桌面端一致）；
       * 原判据只认报警/急停锁存 ⇒ 暂停时按钮灰着、复位无从触发。 */
      var paused = m.autoPhase === "paused" || m.autoPaused === true;
      resetBtn.disabled = busy === "reset" || !canCtrl ||
        !(fault || estopLatched || paused);
      /* [运行中禁改模式 2026-11-18 用户口径]「自动运行的时候模式切换
       * 下拉禁止操作」——理由不只是防误触：运行中改下拉会让**屏幕上的
       * 启动**（按下拉）与**物理启动按钮**（按服务端粘性）按不同模式跑
       * （板端实锅：下拉已切、物理按钮仍按旧模式启动 ⇒ 现场以为按钮坏
       * 了）。运行中下拉置灰 ⇒ 两路必然同源。
       * [权限同款] 下拉改选要发 op=5（服务端按自动控制权限受理）⇒ 无
       * 权限也一并置灰（否则点了必吃一条失败）。 */
      modeSelect.disabled = !canCtrl || running === true;
    }

    /* ==================================================================
     *                     悬浮详情卡开合
     * ================================================================== */
    function toggleDetail(mode) {
      setDetail(detailMode === mode ? "none" : mode);
    }

    function setDetail(mode) {
      detailMode = mode;
      if (mode === "none") {
        shade.classList.add("hidden");
        floatCard.classList.add("hidden");
        return;
      }
      var isTable = (mode === "table");
      setText(detailTitle, isTable ? "设备状态" : "搅拌泵详情");
      detailTable.classList.toggle("hidden", !isTable);
      pumpDetail.classList.toggle("hidden", isTable);
      if (isTable) {
        /* 进入表格态时强制重铺一次（行数没变也铺，保证内容是最新的）。 */
        delete tbody.dataset.rows;
        refreshHealth(store.get("health") || {});
      }
      var w = Math.min(isTable ? DETAIL_TABLE_W : DETAIL_PUMP_W,
                       Math.max(320, window.innerWidth - 24));
      floatCard.style.width = w + "px";
      floatCard.classList.remove("hidden");
      shade.classList.remove("hidden");
      centerFloat();
    }

    function centerFloat() {
      if (detailMode === "none") return;
      var r = floatCard.getBoundingClientRect();
      floatCard.style.left =
        Math.max(0, Math.floor((window.innerWidth - r.width) / 2)) + "px";
      floatCard.style.top =
        Math.max(0, Math.floor((window.innerHeight - r.height) / 2)) + "px";
    }

    function onResize() { centerFloat(); }
    window.addEventListener("resize", onResize);

    /* ==================================================================
     *                          刷新（各卡）
     * ================================================================== */

    function refreshTemphum(doc) {
      /* [显示渐变 2026-11-24 用户「温度也支持下」] 电箱温度按服务端下发的
       * 显示范围上色；湿度不渐变。缺范围键（老服务端）保持单色。 */
      var tColor = (doc.tempGradient !== false)
        ? (gradientColor(doc.tempC, doc.tempMin, doc.tempMax) || C_TEXT)
        : C_TEXT;
      thTemp.set(doc.tempText || "--", tColor);
      thHum.set(doc.humText || "--", C_TEXT);
    }

    function refreshVfd(doc, forDetail) {
      if (!doc.fresh) {
        if (forDetail) {
          pdState.set("无数据", C_OFFLINE);
          pdFreq.set("--"); pdSetf.set("--"); pdCur.set("--"); pdRpm.set("--");
          pdPower.set("--"); pdTorque.set("--");
        } else {
          vfdState.set("无数据", C_OFFLINE);
          vfdFreq.set("--"); vfdRpm.set("--");
          vfdPower.set("--"); vfdTorque.set("--");
        }
        return;
      }
      var color = doc.faulted ? C_FAULT : (doc.running ? C_OK : C_TEXT);
      var stateText = doc.runStateText || "--";
      var freq = (typeof doc.outputFreqHz === "number")
        ? doc.outputFreqHz.toFixed(2) : "--";
      var setf = (typeof doc.setFreqHz === "number")
        ? doc.setFreqHz.toFixed(2) : "--";
      /* [功率/转矩 2026-11-19] 缺键 ⇒ "--"（不编造 0）；有键 ⇒ 按服务端
       * 下发的原值显示（功率两位小数、转矩一位小数）。 */
      var power = (typeof doc.outputPowerKw === "number")
        ? doc.outputPowerKw.toFixed(2) : "--";
      var torque = (typeof doc.torquePercent === "number")
        ? doc.torquePercent.toFixed(1) : "--";
      /* [电机温度 2026-11-20] 缺键（未配/读不到）⇒ "--"；有键 ⇒ 一位
       * 小数。 */
      var mtemp = (typeof doc.motorTempC === "number")
        ? doc.motorTempC.toFixed(1) : "--";
      /* [显示渐变 2026-11-24 用户「电机温度也支持下 / 电机温度呢」]
       * 优先用服务端下发的「电机温度」显示范围；缺键回落模拟量
       * 「温度传感器」通道的工程量程。 */
      var mtempColor = C_TEXT;
      if (typeof doc.motorTempC === "number" && doc.motorTempGradient !== false) {
        mtempColor = gradientColor(doc.motorTempC,
                                   doc.motorTempRangeMin,
                                   doc.motorTempRangeMax) || C_TEXT;
        if (mtempColor === C_TEXT) {
          var _mtCh = findAnalogByName(
            analogChannelTable(store.get("analog") || {}), "温度传感器");
          if (_mtCh) {
            mtempColor = gradientColor(doc.motorTempC,
                                       _mtCh.rangeMin, _mtCh.rangeMax)
              || C_TEXT;
          }
        }
      }
      /* [转速统一 2026-11-27 用户实报「主界面已经 600、搅拌泵详情还是 599」]
       * 稳态对齐给定（|实测-给定换算| <= max(2rpm, 1%) ⇒ 取给定换算值，
       * 治 0.1Hz 级量化）原先只加在**主页分支** ⇒ 详情弹窗走原始
       * speedRpm 四舍五入 ⇒ 两处不一致。现在主页/详情共用这一份取值。 */
      var rpmNum = (typeof doc.speedRpm === "number") ? doc.speedRpm
        : ((typeof doc.outputFreqHz === "number")
           ? doc.outputFreqHz * (1440.0 / 50.0) : null);
      if (rpmNum != null && doc.running === true
          && typeof doc.setFreqHz === "number") {
        var rpmTarget = doc.setFreqHz * (1440.0 / 50.0);
        if (rpmTarget > 0
            && Math.abs(rpmNum - rpmTarget)
               <= Math.max(2, Math.abs(rpmTarget) * 0.01)) {
          rpmNum = rpmTarget;
        }
      }
      var rpmText = (rpmNum == null) ? "--" : String(Math.round(rpmNum));
      if (forDetail) {
        pdState.set(stateText, color);
        pdFreq.set(freq);
        pdSetf.set(setf);
        pdCur.set((typeof doc.outputCurrentA === "number")
          ? doc.outputCurrentA.toFixed(2) : "--");
        pdRpm.set(rpmText);
        pdPower.set(power);
        pdTorque.set(torque);
      } else {
        vfdState.set(stateText, color);
        vfdFreq.set(freq);
        /* [转速取值] 见上方刷新入口的 rpmNum/rpmText（主页与详情共用，
         * 2026-11-27 统一）；这里只负责上色与显示。 */
        /* [电机转速渐变 2026-11-24 用户追加] 按服务端下发的转速范围上色；
         * 缺范围键（老服务端）保持单色。 */
        var rpmColor = (doc.speedGradient !== false)
          ? (gradientColor(rpmNum, doc.speedRangeMin,
                           doc.speedRangeMax) || C_TEXT)
          : C_TEXT;
        vfdRpm.set(rpmText, rpmColor);
        vfdPower.set(power);
        vfdTorque.set(torque);
        vfdMtemp.set(mtemp, mtempColor);
      }
    }

    function refreshHealth(doc) {
      var devices = doc.devices || [];
      hTotal.set(String(doc.count != null ? doc.count : devices.length),
                 C_TEXT);
      var fault = doc.faultCount || 0;
      var offline = doc.offlineCount || 0;
      hFault.set(String(fault), fault ? C_FAULT : C_OK);
      hOffline.set(String(offline), offline ? C_WARN : C_OK);
      /* 设备状态表（悬浮卡 table 态内容；行数变化才重建）。 */
      if (tbody.dataset.rows !== String(devices.length)) {
        tbody.dataset.rows = String(devices.length);
        HSF.replaceChildren(tbody);
        devices.forEach(function (d) {
          /* [地址格 2026-11-19] 显示**瘦身串**（"COM17/115200/#2"，后端
           * slim_addr 算好 ⇒ 与桌面客户端同口径）；完整串进 title（悬停）
           * + **点击复制**（触摸板没有 hover ⇒ 复制才是兜底）。 */
          var full = d.addrFull || d.addr || "";
          var addrCell = HSF.el("td.addr-cell", {
            text: d.addr || "-",
            title: full,
            onclick: function (ev) {
              ev.stopPropagation();
              copyAddr(full);
              showDeviceDetail(d);       /* 地址格也弹详情（完整串在卡里） */
            },
          });
          var tds = [
            HSF.el("td", { text: d.name || "--" }),
            HSF.el("td", { text: d.kindText || "--" }),
            HSF.el("td", { text: d.healthText || "--" }),
            addrCell,
            HSF.el("td", { text: d.text || "-" }),
          ];
          setColor(tds[2], HEALTH_COLOR[d.health] || C_TEXT);
          /* [点行弹详情 2026-11-22] 表格列宽有限 ⇒ 地址/错误信息省略号；
           * 点任意格弹 `deviceDialog` 看原文（地址格额外复制地址）。 */
          var tr = HSF.el("tr", {
            title: "点击查看完整设备信息",
            onclick: function () { showDeviceDetail(d); },
          }, tds);
          tbody.appendChild(tr);
        });
      }
    }

    /* [复制地址 2026-11-19] 触摸板没有 hover ⇒ "完整地址"只能靠复制取。
     * [失败不崩] 剪贴板在非安全上下文（http 页面）里可能不可用 ⇒
     * 捕获异常并在状态栏照实报，不让整页刷新被打断。 */
    function copyAddr(text) {
      if (!text) return;
      var done = function (ok) {
        if (HSF.statusMessage) {
          HSF.statusMessage(ok ? ("已复制地址：" + text)
                                : "复制地址失败（浏览器不允许）");
        }
      };
      try {
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(text).then(function () { done(true); },
                                                function () { done(false); });
          return;
        }
      } catch (e) { /* 落到下面的兜底 */ }
      try {
        var ta = document.createElement("textarea");
        ta.value = text;
        ta.style.position = "fixed";
        ta.style.opacity = "0";
        document.body.appendChild(ta);
        ta.select();
        var ok = document.execCommand("copy");
        document.body.removeChild(ta);
        done(!!ok);
      } catch (e) {
        done(false);
      }
    }

    /**
     * 模拟量快照 → 通道表（两种形状都认）。
     *
     * [为什么两种] 网页 store 的 analog 文档是 model.py 归一化过的
     * （channels[i].name/eng/unit/note，源头仍是服务端的 `en{i}`/`e{i}`/
     * `eu{i}`/`ew{i}`）；但页面在极早期/直连帧场景可能拿到原始哈希 ⇒
     * 这里对 `en{i}` 等原始键做兜底，取数逻辑只有这一处。
     */
    function analogChannelTable(doc) {
      var out = [];
      var chs = doc.channels || [];
      var count = doc.count || doc.n || chs.length || 0;
      for (var i = 0; i < count; i++) {
        var ch = chs[i];
        var name = ch ? (ch.name || "") : "";
        if (!name && doc["en" + i] !== undefined) {
          name = String(doc["en" + i] || "");
        }
        var eng = null;
        if (ch && ch.eng !== undefined && ch.eng !== null) {
          eng = ch.eng;
        } else if (typeof doc["e" + i] === "number") {
          eng = doc["e" + i] / 1000.0;      /* e{i} 是 ×1000 定点 */
        }
        var unit = (ch && ch.unit) ? ch.unit : String(doc["eu" + i] || "");
        var note = (ch && ch.note) ? ch.note : String(doc["ew" + i] || "");
        /* [显示渐变 2026-11-24] 量程：JSON 里是 rangeMin/Max（已 ÷1000），
         * 兼容原始 lo{i}/hi{i} 快照键（×1000 定点）。 */
        var rmin = (ch && ch.rangeMin !== undefined && ch.rangeMin !== null)
          ? ch.rangeMin
          : (typeof doc["lo" + i] === "number" ? doc["lo" + i] / 1000.0 : null);
        var rmax = (ch && ch.rangeMax !== undefined && ch.rangeMax !== null)
          ? ch.rangeMax
          : (typeof doc["hi" + i] === "number" ? doc["hi" + i] / 1000.0 : null);
        out.push({ name: name, eng: eng, unit: unit, note: note,
                   rangeMin: rmin, rangeMax: rmax });
      }
      return out;
    }

    /** 按通道名找（服务端把「压差计/压力1/压力2/温度传感器」写进 en{i}）；
     *  名字对不上就返回 null（该栏显示 "--"，不拿别的通道顶替）。 */
    function findAnalogByName(list, name) {
      for (var i = 0; i < list.length; i++) {
        /* trim：现场配置的通道名可能带首尾空格（`en{i}` 原样下发）。 */
        if (String(list[i].name || "").trim() === name) return list[i];
      }
      return null;
    }

    var DP_LABELS = ["压差", "进水压力", "出水压力"];

    /** 欠量程/负压类量程说明（主页压差卡不显示这类提示）。
     *  [2026-11-27 用户口径「这个负压提示不需要」] 只过滤「低于 x mA」
     *  这一类（负压线性延续 / 欠量程钳下限）；断线与超量程仍要提示。 */
    function isLowRangeNote(note) {
      var s = String(note || "");
      return s.indexOf("低于") === 0 || s.indexOf("负压") >= 0
        || s.indexOf("欠量程") >= 0;
    }

    function refreshDp(doc) {
      var cols = [dpCol, p1Col, p2Col];
      var fresh = !!doc.fresh;
      var list = fresh ? analogChannelTable(doc) : [];
      /* 压差栏以名字「压差计」为准；老服务端没给名字时回落通道 0
       * （既有行为，现场压差计一直接 CH0 缺省）。 */
      var dpCh = findAnalogByName(list, "压差计") || (list[0] || null);
      var chans = [dpCh,
                   findAnalogByName(list, "进水压力"),
                   findAnalogByName(list, "出水压力")];
      var notes = [];
      for (var i = 0; i < cols.length; i++) {
        var ch = chans[i];
        if (!fresh || !ch) {
          cols[i].set("--", C_TEXT, "MPa", "");
          continue;
        }
        /* [note 是说明，不是「值无效」] 越限/负压时服务端给线性值 + ew
         * 说明 ⇒ 数值照显示、说明上警示色（与桌面客户端同口径）。
         * eng 为 null（通道无效/未配换算）时 fmt3 给 "--"，不编造 0。
         * [显示渐变 2026-11-24] 三栏各按自己的量程上色。 */
        var chColor = (ch.gradient !== false)
          ? (gradientColor(ch.eng, ch.rangeMin, ch.rangeMax) || C_TEXT)
          : C_TEXT;
        cols[i].set(fmt3(ch.eng), chColor, ch.unit || "MPa", ch.note || "");
        /* [负压提示不上主页 2026-11-27 用户「这个负压提示不需要」] 数值
         * 照常显示（负数是真值，4mA 以下线性延续）；「低于 x mA（负压/
         * 线性延续/欠量程）」这类量程说明不再占卡底一行。断线/超量程
         * 是真故障信号 ⇒ 照旧提示。 */
        if (ch.note && !isLowRangeNote(ch.note)) {
          notes.push(DP_LABELS[i] + "：" + ch.note);
        }
      }
      if (!fresh) {
        setText(dpNote, "无数据");
        dpNote.style.color = C_OFFLINE;
        dpNote.title = "无数据";
      } else {
        var joined = notes.join("；");
        setText(dpNote, joined);
        dpNote.style.color = notes.length ? C_WARN : C_SUB;
        dpNote.title = joined;
      }
    }

    function refreshFm(doc) {
      if (!doc.fresh) {
        fmFlow.set("--", C_TEXT, "");
        fmVel.set("--", C_TEXT, "m/h");
        setNote(fmSub, "无数据", C_OFFLINE);
        return;
      }
      /* [负零 2026-11-25] 零点 -0.0/极小负值 ⇒ 显示 0.00，不出现 "-0.00"。 */
      var flowShow = (typeof doc.flow === "number"
        && Math.abs(doc.flow) < 0.005) ? 0 : doc.flow;
      fmFlow.set(fmt2(flowShow), C_TEXT, doc.flowUnitText || "");
      /* [滤速 2026-11-25] 服务端算好；未配面积 ⇒ null ⇒ "--"（不是 0）。
       * [负号 2026-11-25 用户实报「-0.00」] 负向流量不算过滤方向滤速 ⇒
       * 夹到 0，界面不出现负号。 */
      fmVel.set(doc.filterVelocityMh == null
        ? "--" : fmt2(Math.max(0, doc.filterVelocityMh)), C_TEXT, "m/h");
      var totals = "正累计 " + fmt2(doc.forwardTotal) + " " +
        (doc.totalUnitText || "?") + " / 反累计 " + fmt2(doc.reverseTotal) +
        " " + (doc.totalUnitText || "?");
      var key;
      if (doc.alarms) {
        /* 报警码直接说中文（空管→上限→下限→系统），红色 span 醒目。 */
        key = "alarm|" + totals + "|" + (doc.alarmText || "--");
        if (fmSub.dataset.noteKey !== key) {
          fmSub.dataset.noteKey = key;
          fmSub.textContent = "";
          fmSub.appendChild(document.createTextNode(totals + "　·　"));
          var span = HSF.el("span",
                            { text: "报警: " + (doc.alarmText || "--") });
          span.style.color = C_FAULT;
          fmSub.appendChild(span);
          fmSub.style.color = C_SUB;
          fmSub.classList.remove("hidden");
        }
      } else {
        setNote(fmSub, totals, C_SUB);
      }
    }

    function refreshValves(doc) {
      var fresh = !!doc.fresh;
      var items = doc.valves || [];
      for (var i = 0; i < VALVE_COUNT; i++) {
        var item = fresh ? items[i] : null;
        var name, text, tone;
        if (item) {
          name = item.name || (VALVE_FALLBACK_NAMES[i] || ("电动球阀" + (i + 1)));
          if (item.ready) {
            text = item.stateText || "未知";
            tone = item.tone || "unknown";
          } else {
            text = "未配置";
            tone = "unconfigured";
          }
        } else {
          name = "-";
          text = "--";
          tone = "unconfigured";
        }
        valveCells[i].set(text, TONE_COLOR[tone] || C_NA, name);
      }
    }

    function refreshTurb(doc) {
      var st = doc;                       /* 后端已按侧择优 */
      var items = st.items || [];
      var noteText = "";
      if (!st.fresh) {
        noteText = "未收到浊度仪数据（服务端未装配 / 从站失联）";
      } else if ((st.count || 0) === 0) {
        noteText = "服务端未装配浊度仪";
      }
      for (var i = 0; i < TURBIDITY_COUNT; i++) {
        var item = items[i];
        var name = item ? (item.name || ("浊度仪" + (i + 1)))
                        : ("浊度仪" + (i + 1));
        var ntu = item ? item.ntu : null;
        var hasTemp = !!(item && item.hasTemp);
        var tempVal = item ? item.temp : null;
        var text, color;
        if (ntu === null || ntu === undefined) {
          text = "--";
          color = C_NA;
        } else {
          text = ntu.toFixed(2);
          /* [显示渐变 2026-11-24 用户「两个浊度仪范围不一样，远端可配；蓝→
           * 绿→红」] 逐台 rangeMin/rangeMax（后端已 ÷100）；缺键退回旧的
           * 「到 20 NTU 标红」口径。 */
          color = ((item.gradient !== false)
            ? gradientColor(ntu, item.rangeMin, item.rangeMax) : null) ||
            (ntu >= TURBIDITY_WARN_NTU ? C_FAULT : C_TEXT);
        }
        turbBlocks[i].setCaption(name);
        turbBlocks[i].set(text, color);
        /* 水温：缺键 = 该机型无温度 ⇒ 不显示（不是 0℃）。 */
        var tempText = hasTemp && tempVal !== null
          ? ("水温 " + tempVal.toFixed(2) + " ℃") : "";
        setText(turbTemps[i], tempText);
        turbTemps[i].classList.toggle("hidden", !tempText);
      }
      setNote(turbNote, noteText, C_OFFLINE);
    }

    /** 数字量里的进水泵 DO 回显 → (通/断|null, 该帧 updatedMs)。 */
    function pumpDoState(digitalDocs) {
      var bestOn = null, bestMs = 0;
      (digitalDocs || []).forEach(function (dev) {
        if (!dev || !dev.fresh) return;
        (dev.do || []).forEach(function (ch) {
          if (ch.name && ch.name.indexOf("进水泵") >= 0) {
            if ((dev.updatedMs || 0) >= bestMs) {
              bestOn = !!ch.on;
              bestMs = dev.updatedMs || 0;
            }
          }
        });
      });
      return { on: bestOn, ms: bestMs };
    }

    function refreshTurbPump(m) {
      var pump = m.pump || {};
      var fb = pumpDoState(m.digitalDocs);
      var useFb = fb.on !== null &&
        (!pump.fresh || fb.ms > (pump.updatedMs || 0));
      var mark, color, note;
      if (useFb) {
        mark = fb.on ? "通" : "断";
        color = fb.on ? C_OK : C_OFFLINE;
        note = "输出点（无反馈）";
      } else if (!pump.fresh) {
        mark = "--"; color = C_NA; note = "未收到泵状态";
      } else if (!(pump.count > 0)) {
        mark = "--"; color = C_NA; note = "服务端未装配进水泵";
      } else {
        var item = (pump.items || [])[0] || {};
        if (!item.ready) {
          mark = "--"; color = C_OFFLINE; note = "未配置点位";
        } else if (item.on) {
          mark = "通"; color = C_OK; note = "输出点（无反馈）";
        } else {
          mark = "断"; color = C_OFFLINE; note = "输出点（无反馈）";
        }
      }
      var key = mark + "|" + color + "|" + note + "|" + (turbNoteText || "");
      if (key === lastPumpKey) return;
      lastPumpKey = key;
      turbPumpEl.textContent = "";
      turbPumpEl.appendChild(document.createTextNode("进水泵 "));
      var dot = HSF.el("span", { text: "● " + mark });
      dot.style.color = color;
      turbPumpEl.appendChild(dot);
      turbPumpEl.appendChild(document.createTextNode(" " + note));
    }

    var turbNoteText = "";
    var lastPumpKey = null;
    var turbPumpEl = HSF.el("span");
    (function placePumpLine() {
      /* 进水泵状态并进浊度仪卡说明行（2026-11-12 参考口径）：与浊度说明
       * 共用一行（turbNote 的兄弟），有数据时这行显示泵状态。 */
      turbNote.parentNode.insertBefore(turbPumpEl, turbNote);
      turbPumpEl.className = "card-note";
      turbPumpEl.style.color = C_OFFLINE;
    })();

    /** 在数字量模块里按服务端下发通道名找按钮组通道（参考
     *  _find_button_channels：按名字匹配，不写死通道号）。 */
    function findButtonChannels(digitalDocs) {
      var fresh = (digitalDocs || []).filter(function (d) {
        return d && d.fresh;
      });
      if (!fresh.length) return null;
      var dev = null;
      for (var i = 0; i < fresh.length; i++) {
        var names = [];
        (fresh[i].di || []).forEach(function (c) { names.push(c.name || ""); });
        (fresh[i].do || []).forEach(function (c) { names.push(c.name || ""); });
        var hit = names.some(function (n) {
          return n.indexOf(BTN_ESTOP_NAME) >= 0;
        }) || names.some(function (n) {
          return BTN_LAMP_NAMES.some(function (k) {
            return n.indexOf(k) >= 0;
          });
        });
        if (hit) { dev = fresh[i]; break; }
      }
      if (dev === null) dev = fresh[0];
      var chMap = {};
      var diKeys = [BTN_ESTOP_NAME, BTN_START_NAME, BTN_STOP_NAME,
                    BTN_RESET_NAME];
      diKeys.forEach(function (key, slot) {
        (dev.di || []).some(function (c) {
          if ((c.name || "").indexOf(key) === 0) {
            chMap[slot] = { ch: c.index, isDo: false };
            return true;
          }
          return false;
        });
      });
      BTN_LAMP_NAMES.forEach(function (key, idx) {
        var slot = idx + 4;
        (dev.do || []).some(function (c) {
          if ((c.name || "").indexOf(key) === 0) {
            chMap[slot] = { ch: c.index, isDo: true };
            return true;
          }
          return false;
        });
      });
      return { dev: dev, chMap: chMap };
    }

    function refreshButtons(digitalDocs) {
      var found = findButtonChannels(digitalDocs);
      if (found === null) {
        for (var i = 0; i < BTN_SLOT_COUNT; i++) {
          btnCells[i].set("--", C_NA, BTN_SLOT_LABELS[i]);
        }
        setNote(btnNote, "未收到按钮组数据（数字量模块离线）", C_OFFLINE);
        return;
      }
      var dev = found.dev, chMap = found.chMap;
      for (var slot = 0; slot < BTN_SLOT_COUNT; slot++) {
        var text = "--", color = C_NA;
        var hit = chMap[slot];
        if (hit) {
          if (hit.isDo) {
            /* 灯：亮=绿，灭=灰。 */
            text = dev.do && dev.do[hit.ch] && dev.do[hit.ch].on
              ? "亮" : "灭";
            color = (text === "亮") ? C_OK : C_NA;
          } else {
            var level = !!(dev.di && dev.di[hit.ch] && dev.di[hit.ch].on);
            var pressed = BTN_NORMALLY_CLOSED[slot] ? !level : level;
            if (slot === 0) {
              /* 急停：常闭，1=回路正常；按下或断线 = 急停生效（红）。 */
              text = pressed ? "急停" : "正常";
              color = pressed ? C_FAULT : C_OK;
            } else {
              text = pressed ? "按下" : "未按";
              color = pressed ? C_OK : C_NA;
            }
          }
        }
        btnCells[slot].set(text, color, BTN_SLOT_LABELS[slot]);
      }
      var missing = [];
      for (var s2 = 0; s2 < BTN_SLOT_COUNT; s2++) {
        if (!chMap[s2]) missing.push(BTN_SLOT_LABELS[s2]);
      }
      setNote(btnNote, missing.length ? ("未配置：" + missing.join("、"))
                                      : "", C_WARN);
    }

    function setNote(node, text, color) {
      var key = text + "|" + color;
      if (node.dataset.noteKey === key) return;
      node.dataset.noteKey = key;
      node.textContent = text;
      node.style.color = color;
      /* 说明行是事件驱动文案：空时收起（参考 _set_note 的空时隐藏口径，
       * 不违反恒占位——该行从无到有才变一次，不是逐拍翻转）。 */
      node.classList.toggle("hidden", !text);
    }

      /* ---- 自动模式卡回显（参考 _refresh_auto_card） ---- */

    /** 无界段键：运行中且当前状态**无界** ⇒ "<autoMode>|<single|cycle>"；
     *  停止/急停/有界相位 ⇒ ""（本次段结束，段起点一并作废）。模式取
     *  **服务端下发的 autoMode**（权威），不用下拉框选中值（那只是下次
     *  启动意向）。
     *  [有界/无界判据] 该相位 total>0 = 有换相定时（循环的反洗/顺洗、兜底
     *  开启的产水）⇒ 有界倒计时；total=0 = 无定时（单模式三模式、循环产水
     *  兜底关）⇒ 无界。兜底有无以服务端阶段 total 为准（服务端执行的就是
     *  远端设置卡下发的那份 cycle 配置；拿不到 ⇒ total=0 ⇒ 按无界显示运行
     *  时间，绝不显示假倒计时）。
     *  [single/cycle 判据 2026-11-23 订正] 不能再用 elapsed>=0 推断"组合
     *  循环在跑"：单模式现在也报运行已用时长（stage_timing 新口径）⇒
     *  一律按根模式 autoMode 区分。 */
    function runSegmentKey(m) {
      if (m.autoRunning !== true) return "";
      var stageOn = !!m.autoStageKnown && (m.autoStageElapsedMs >= 0);
      if (stageOn && m.autoStageTotalMs > 0) return "";
      return (m.autoMode || "?") + "|" +
        (isCycleDoc(m) ? "cycle" : "single");
    }

    /** 状态沿记账（每次 mode 文档到达都要经过，别漏沿）：autoRunning 非
     *  bool = 未知（进页首帧 / 断线 reset_live / 老服务端）⇒ 观测作废，
     *  恢复后在跑只能回退累计；键变化且**前一次观测真实存在**才起本地
     *  计时（真实启动/换相沿），首次观测不编造起点。相位 valves→pumps→
     *  running 不进键 ⇒ 同一次运行跨相位不重置「本次」。 */
    function trackRunSegment(m) {
      if (m.autoRunning !== true && m.autoRunning !== false) {
        lastSegKey = null;
        segStartMs = 0;
        return;
      }
      var key = runSegmentKey(m);
      if (key !== lastSegKey) {
        segStartMs = (lastSegKey === null) ? 0 : Date.now();
        lastSegKey = key;
      }
    }

    /** 组合循环进度后缀（2026-11-27 用户「组合有多次循环要在主界面提示
     *  总共几次、当前第几次」）。
     *
     * [服务端键] autoRepCurrent/autoRepTotal = 当前阶段所属最近一层组的
     * 第几遍/总遍数（1-based；0=不可用；total==1 不必显示）；
     * autoRound/autoRoundTotal = 顶层第几轮/总轮数（total=0 ⇒ 一直循环）。
     * [缺键不编造] 老服务端缺键 ⇒ model 给 0 ⇒ 这里整段不出现；只有确实
     * 收到 1-based 的值才拼文案（不把「没数据」说成「第 0 遍」）。 */
    function cycleProgressSuffix(m) {
      var out = "";
      var cur = Number(m.autoRepCurrent || 0);
      var total = Number(m.autoRepTotal || 0);
      if (total >= 2 && cur >= 1) {
        out += " · 第 " + cur + "/" + total + " 遍";
      }
      var round = Number(m.autoRound || 0);
      var roundTotal = Number(m.autoRoundTotal || 0);
      if (roundTotal > 0 && round >= 1) {
        out += " · 第 " + round + "/" + roundTotal + " 轮";
      } else if (round >= 1) {
        /* 一直循环（total=0）：只报当前第几轮，不编造总数。 */
        out += " · 第 " + round + " 轮";
      }
      return out;
    }

    /** 模式下拉与服务端当前模式对齐（2026-11-27 根修）。
     *
     *  [运行中] 每拍跟随实际模式：循环 ⇒ auto，单模式 ⇒ autoMode；
     *  下拉在运行中已禁用，对齐不会覆盖用户操作。
     *  [未运行/暂停] 仅在用户没动过下拉时按服务端粘性对齐一次
     *  （防止刷新页面后「屏幕启动」与「物理启动按钮」跑不同模式）。 */
    function syncModeSelect(m, running) {
      if (!canAutoCtrl()) return;
      var srvMode = "";
      if (running) {
        srvMode = isCycleDoc(m) ? "auto" : (m.autoMode || "");
      } else if (!modeUserPicked) {
        srvMode = isCycleDoc(m) ? "auto" : (m.autoMode || "");
      }
      if (!srvMode) return;
      for (var k = 0; k < modeSelect.options.length; k++) {
        if (modeSelect.options[k].value === srvMode) {
          if (modeSelect.selectedIndex !== k) modeSelect.selectedIndex = k;
          break;
        }
      }
    }

    function refreshAutoCard() {
      var m = lastModeDoc;
      var running = m.autoRunning;
      var fault = !!m.autoFault;
      applyEnabled();

      /* [下拉与服务端粘性对齐 2026-11-18] 网页端原先下拉只发 op=5、
       * 从不与服务端粘性对齐 ⇒ 刷新页面后下拉回到缺省（产水），而服务端
       * 的粘性（**现场物理启动按钮按它跑**）可能是别的模式 ⇒ 两个按钮
       * 按不同模式跑（"屏幕上的启动"与"物理启动"分裂）。
       * ⇒ **未运行且用户没动过下拉时**，用服务端 autoMode 对齐一次。
       * 与桌面端 pages_other._refresh_auto_card 同口径。
       * [循环上下文 2026-11-27] 循环暂停（记忆未清）时服务端报的 autoMode
       * 是**记忆子阶段**（如 produce）⇒ 直接按它对齐会把下拉写成「产水
       * 模式」，而再按启动实际从循环继续 ⇒ 循环上下文一律对齐到「自动
       * （流量循环）」（判据 isCycleDoc：autoStageTitle 非空）。 */
      /* [下拉回显根修 2026-11-27 用户实报「自动运行中，下拉还是产水
       * 模式、状态行写反洗模式」] 运行中一律回显**服务端实际在跑的
       * 模式**：循环里（isCycleDoc：autoStageTitle 非空）⇒「自动（流量
       * 循环）」（运行中 autoMode 报的是子阶段，旧判据 autoMode === "auto"
       * 恒假 ⇒ 下拉停在缺省，用户实报）；单模式 ⇒ autoMode 本身。
       * 运行中下拉本就禁用（运行中禁改模式）⇒ 每拍对齐不会抢用户；
       * 停止/暂停态保持原口径（未运行且用户没动过下拉才对齐一次，循环
       * 上下文同样对齐到 auto）。老服务端没有 autoStageTitle 时循环运行中
       * 退化为显示当前子阶段名——与状态行一致，不撒谎。 */
      syncModeSelect(m, running);
      /* 状态行判据（循环上下文）：与下拉回显同源 isCycleDoc。 */
      var cycleStage = !!running && isCycleDoc(m);

      var text, color, bold = false;
      if (!m.known) {
        text = "连接后显示服务端状态";
        color = C_SUB;
      } else if (!m.auto) {
        text = "手动模式（切到自动后可按启动）";
        color = C_SUB;
      } else if (fault) {
        text = "报警锁存：按「复位」清除后再启动";
        color = C_FAULT;
      } else if (running) {
        var phaseText = AUTO_PHASE_TEXT[m.autoPhase] || "";
        var modeName = modeNameOf(m.autoMode) || m.autoMode || "未选择";
        /* [序号后缀 2026-11-27 用户口径「自动循环模式时各模式提示带上
         * 序号后缀；单模式不带」] 循环里优先用服务端 autoStageTitle
         * （「产水模式2」= 同模式第 2 个实例）；缺键/空串（老服务端、单
         * 模式）⇒ 退回「自动循环（模式名）」/ 纯模式名，**不编造序号**。 */
        var stageTitle = stageTitleOf(m);
        /* [循环判据] 与 syncModeSelect / 表盘同源（isCycleDoc）：运行组合
         * 循环时 autoMode 报的是子阶段，"auto" 只在停止后才回到 autoMode
         * ⇒ 不能用 autoMode === "auto"。本分支只在 running 为真时进入。 */
        var head = isCycleDoc(m)
          ? ("自动循环（" + (stageTitle || modeName) + "）")
          : modeName;
        text = "运行中 · " + head + (phaseText ? (" · " + phaseText) : "")
             + cycleProgressSuffix(m);
        color = C_WARN;
        bold = true;
      } else if (m.autoPhase === "paused") {
        /* [paused 2026-11-19] 停止＝暂停 ⇒ 按启动从暂停处续跑，复位才
         * 清零（与桌面客户端同口径）。 */
        var pm = modeNameOf(m.autoMode) || m.autoMode || "";
        text = "已暂停" + (pm ? ("（" + pm + "）") : "")
             + cycleProgressSuffix(m)
             + " · 按启动继续（按复位则从头）";
        color = C_WARN;
      } else if (m.autoPhase === "estop") {
        text = "急停锁存：按「复位」后再启动";
        color = C_FAULT;
      } else {
        var selName = modeNameOf(modeSelect.value);
        if (modeSelect.value === "auto" && m.autoMode) {
          text = "已停止 · 待启动（将从" + (modeNameOf(m.autoMode) ||
            m.autoMode) + "继续；按复位则从头）";
        } else {
          text = selName ? ("已停止 · 待启动（选中：" + selName + "）")
                         : "已停止";
        }
        color = C_SUB;
      }
      setText(autoHint, text);
      if (autoHint.dataset.c !== color || autoHint.dataset.b !== String(bold)) {
        autoHint.dataset.c = color;
        autoHint.dataset.b = String(bold);
        autoHint.style.color = color;
        autoHint.style.fontWeight = bold ? "bold" : "normal";
      }

      /* 累计运行时长：缺键（老服务端/未连接）显「--」，不编造 0。 */
      setText(runLabel, m.autoRunMsKnown
        ? HSF.fmtDuration(m.autoRunMs) : "--");

      /* 运行表盘（2026-11-23 改口径，用户定案「除了自动循环模式，其他几个
       * 模式去掉这个表盘，不好看」）：**只有组合循环（自动/流量循环）
       * 在运行中才显示**；单模式（产水/反洗/顺洗/放水）运行中不再显示
       * ——单模式没有阶段总时长，表盘里只能显示「本次运行」累计，与下方
       * 状态行/累计行重复且不好看。autoMode 为空（老服务端/未连接）⇒ 不
       * 显示，不编造模式。停止/急停态照旧隐藏（急停后控制器
       * running=false，同闸收口）。
       * [判据 2026-11-27 根修] 与下拉/状态行同源走 isCycleDoc：运行组合
       * 循环时服务端 autoMode 是子阶段（produce/…），旧判据
       * `m.autoMode === "auto"` 恒假 ⇒ 表盘自 2026-11-23 起从未显示过。
       * · 有界相位（autoStageElapsedMs>=0 且 total>0：循环的反洗/顺洗、
       *   兜底开启的产水）⇒ 维持现状的相位倒计时口径；
       * · 无界状态（循环产水且兜底关）⇒ 中央显示「本次运行」时长：本地
       *   从进入该状态的状态沿起算（trackRunSegment 在每帧先记账）；进页/
       *   断线重连已在跑、本地起点未知 ⇒ 回退显示服务端累计 autoRunMs
       *   （与状态行同源，不加「本次运行」标签），拿到下一个状态沿后恢复
       *   本次计时。秒级取整 ⇒ 文本每秒才变一次。 */
      trackRunSegment(m);
      var dialShow = m.autoRunning === true && isCycleDoc(m);
      dial.el.classList.toggle("hidden", !dialShow);
      if (dialShow && !!m.autoStageKnown && (m.autoStageElapsedMs >= 0) &&
          m.autoStageTotalMs > 0) {
        dial.setState(m.autoStageElapsedMs, m.autoStageTotalMs);
      } else if (dialShow) {
        var segMs = (segStartMs > 0)
          ? Math.floor((Date.now() - segStartMs) / 1000) * 1000
          : (m.autoRunMsKnown ? m.autoRunMs : -1);
        dial.setState(segMs, 0, segStartMs > 0 ? "本次运行" : "");
      } else {
        dial.setState(-1, 0);
      }
      refreshPhaseRow(m);
      refreshAlarmCard(m);
    }

    /* ---- 阶段信息行（2026-11-19；主页「自动模式」卡里的一行）----
     *
     * [显示条件] 自动模式 ⇒ 显示；手动模式 ⇒ 隐藏整行（手动模式"第几
     * 阶段"无意义）。
     * [进度条条件] total > 0（本阶段有定时）⇒ 显示进度条与"已用/共"；
     * 否则（单模式 / 老服务端 / 不在运行）⇒ 只显示阶段名，**不编造**
     * 倒计时。
     * [与表盘不重复] 表盘是倒计时（剩余）；本行是本阶段已用/总。
     * [口径逐字对齐参考端 pages_other.HomePage._refresh_phase_row] */
    var PHASE_NAME_TEXT = {
      idle: "待命", valves: "阀相", pumps: "泵相",
      running: "运行中", estop: "急停",
      /* [paused 2026-11-19] 停止＝暂停 ⇒ 阶段行照常显示冻结的已用
       * 时长（服务端 stage_timing 暂停分支）。 */
      paused: "暂停",
    };

    function fmtPhaseMs(ms) {
      /* 毫秒 → 「M分SS秒」（阶段进度用；小时级不现实——阶段最长几十分钟）。 */
      var secs = Math.max(0, Math.floor((ms || 0) / 1000));
      var minutes = Math.floor(secs / 60);
      var seconds = secs % 60;
      var h = Math.floor(minutes / 60);
      minutes = minutes % 60;
      function pad(n) { return (n < 10 ? "0" : "") + n; }
      if (h) return h + "时" + pad(minutes) + "分" + pad(seconds) + "秒";
      return minutes + "分" + pad(seconds) + "秒";
    }

    function refreshPhaseRow(m) {
      var inAuto = !!m.known && !!m.auto;
      phaseRow.classList.toggle("hidden", !inAuto);
      if (!inAuto) return;
      var phase = m.autoPhase || "";
      var name = PHASE_NAME_TEXT[phase] || "--";
      if (!phase) name = "--";
      setText(phaseLabel, name);
      var elapsed = m.autoStageElapsedMs || 0;
      var total = m.autoStageTotalMs || 0;
      if (m.autoStageKnown && total > 0 && elapsed >= 0) {
        setText(phaseText, "已用 " + fmtPhaseMs(elapsed) + " / 共 " +
                            fmtPhaseMs(total));
      } else if (m.autoStageKnown && elapsed >= 0) {
        /* [所有模式都显示运行时长 2026-11-23 用户口径「原先所有模式都有运行
         * 时长显示」] 单模式（产水/反洗/顺洗/放水）与组合循环的无界阶段没有
         * "共 N"，服务端仍下发「本次运行已用」（autoStageTotalMs=0）⇒ 这里
         * 显示"已用 X"。只有服务端真的没给数据（autoStageKnown=false）才 "--"，
         * 不把"没数据"说成"没在跑"。与桌面客户端逐字同款。 */
        setText(phaseText, "已用 " + fmtPhaseMs(elapsed));
      } else if (phase === "paused") {
        /* 暂停且无定时数据 ⇒ 文案提示可续跑（状态行已有「已暂停 ·
         * 按启动继续」，不重复表达）。 */
        setText(phaseText, "按启动继续");
      } else {
        setText(phaseText, "--");
      }
    }

    /* ---- 最近报警卡（2026-11-19 主页新增）----
     *
     * [数据来源] 服务端状态快照里的 lastAlarm（web 后端 model.last_alarm
     * ⇒ store 的 "mode" 文档里带 lastAlarm）。
     * [老服务端兼容] 没有该键 ⇒ 显示「无报警」（**不报错、不崩**）。
     * [类型文案后端算好] 「类型」由后端 alarm_type_text 算好放在
     * typeText 里 ⇒ 前端直接显示（不在 JS 里再抄一份映射表 ⇒ 不会与
     * 桌面端/报警页漂移）。 */
    function refreshAlarmCard(m) {
      var info = m.lastAlarm;
      if (!info) {
        setText(alarmMeta, "--");
        setText(alarmText, "无报警");
        return;
      }
      var atMs = info.atMs || 0;
      var stamp = "--";
      if (atMs) {
        var d = new Date(atMs);
        function pad(n) { return (n < 10 ? "0" : "") + n; }
        stamp = pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" +
                pad(d.getSeconds());
      }
      var kindText = info.typeText || "自动运行";
      setText(alarmMeta, stamp + " · " + kindText);
      setText(alarmText, info.text || "");
    }

    /* ==================================================================
     *                          装配与节拍
     * ================================================================== */

    /** 数字量统一成数组：/api/state 快照里是数组，SSE 单模块事件里是
     *  单个对象（hub 逐模块 broadcast）——两种形状都要吃得下。 */
    function digitalDocs() {
      var d = store.get("digital");
      if (Array.isArray(d)) return d;
      return d ? [d] : [];
    }

    function renderAll() {
      refreshTemphum(store.get("temphum") || {});
      refreshVfd(store.get("vfd") || {}, false);
      refreshVfd(store.get("vfd") || {}, true);
      refreshHealth(store.get("health") || {});
      refreshDp(store.get("analog") || {});
      refreshFm(store.get("flowmeter") || {});
      refreshValves(store.get("valve") || {});
      var docs = digitalDocs();
      refreshTurb(store.get("turbidity") || {});
      refreshButtons(docs);
      refreshTurbPump({ pump: store.get("pump") || {}, digitalDocs: docs });
      refreshAutoCard();
    }

    ["vfd", "temphum", "analog", "flowmeter", "turbidity", "pump",
     "digital", "valve", "health"].forEach(function (kind) {
      unsubs.push(store.on(kind, renderAll));
    });
    unsubs.push(store.on("mode", function (doc) {
      /* /api/mode 轮询与控制应答都会写回 store；这里只记最新文档。 */
      lastModeDoc = doc || lastModeDoc;
      refreshAutoCard();
    }));
    unsubs.push(store.on("link", function () { refreshAutoCard(); }));

    /* 自动状态族 1s 轮询（参考 HomePage._auto_poll_at 节流口径）。 */
    var pollTimer = setInterval(function () {
      if (!connected()) return;
      HSF.api.getMode().then(function (doc) {
        if (doc && doc.ok) store.applyEvent("mode", doc);
      });
    }, 1000);
    timers.push(pollTimer);

    /* 兜底重绘：dial/累计时长等不依赖推送的节拍。节拍 = 本地设置·
     * 界面刷新周期（参考 MainWindow._refresh_timer 100~2000ms；200ms 主闸
     * 每拍重读偏好，改完立即生效；同值早退，稳态零重排）。 */
    var lastRenderMs = 0;
    timers.push(setInterval(function () {
      var now = Date.now();
      if (now - lastRenderMs >= HSF.prefs.refreshMs()) {
        lastRenderMs = now;
        renderAll();
      }
    }, 200));

    renderAll();
    void ctx;

    /* ---- unmount（router 调）：定时器/订阅/窗口监听全清 ---- */
    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      window.removeEventListener("resize", onResize);
    };
  }
})();
