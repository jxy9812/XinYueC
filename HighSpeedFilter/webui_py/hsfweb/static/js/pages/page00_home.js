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
 *   · 第二排：压差/流量计/电动球阀六路；第三排：浊度仪/按钮组系统状态；
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
    var cycleWasRunning = false;
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
    var vfdFreq = statBlock("输出频率", "Hz", 40);
    /* [第三项=转速 2026-11-17 用户实报] 原「设定频率」⇒ 主页一览卡显示
     * **实际转速**（现场铭牌 50Hz≈1440rpm ⇒ 28.8 rpm/Hz，按输出频率换算；
     * 变频器无转速寄存器，属换算值）。「设定频率」仍在泵详情悬浮卡里。
     * [与桌面客户端/C 端同口径] C 端 HsfHomePage.c 的 HOME_TXT_VFD_SET
     * 已是「转速」（HOME_RPM_PER_HZ=1440/50）；桌面客户端 2026-11-18 补齐
     * ⇒ 三端主页一致。 */
    var vfdRpm = statBlock("转速", "rpm", 26);
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
             [vfdState.root, vfdFreq.root, vfdRpm.root]),
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
      HSF.el("div.card-title", { text: "设备健康" }),
      HSF.el("div.stat-grid.cols-3", null,
             [hTotal.root, hFault.root, hOffline.root]),
    ]);

    /* ---- 第二排：压差 / 流量计 / 电动球阀 ---- */
    var dpValue = statBlock("通道 0", "", 40);
    var dpNote = HSF.el("div.card-note", { text: "--" });
    var dpCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "压差" }),
      dpValue.root, dpNote,
    ]);

    var fmFlow = statBlock("瞬时流量", "", 40);
    var fmSub = HSF.el("div.card-note", { text: "无数据" });
    var fmCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "流量计" }),
      fmFlow.root, fmSub,
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
      var block = statBlock("浊度仪" + (ti + 1), "NTU", 32);
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
      HSF.el("div.home-cards.cols-3", null, [turbCard, btnCard, dpCard]),
      HSF.el("div.home-cards.cols-3.row2", null,
             [vfdCard, fmCard, valveCard]),
      HSF.el("div.home-cards.cols-3.row3", null,
             [thCard, healthCard, alarmCard]),
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);
    host.appendChild(shade);
    host.appendChild(floatCard);

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
      resetBtn.disabled = busy === "reset" || !canCtrl ||
        !(fault || estopLatched);
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
      thTemp.set(doc.tempText || "--", C_TEXT);
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
      if (forDetail) {
        pdState.set(stateText, color);
        pdFreq.set(freq);
        pdSetf.set(setf);
        pdCur.set((typeof doc.outputCurrentA === "number")
          ? doc.outputCurrentA.toFixed(2) : "--");
        pdRpm.set((typeof doc.speedRpm === "number")
          ? String(Math.round(doc.speedRpm)) : "--");
        pdPower.set(power);
        pdTorque.set(torque);
      } else {
        vfdState.set(stateText, color);
        vfdFreq.set(freq);
        /* [主页第三项=转速] 优先服务端下发的 speedRpm（设备层按铭牌额定点
         * 算的换算值）；没有该字段就按同一系数本地换算（25Hz ⇒ 720rpm）
         * ——与桌面客户端的 _vfd_rpm_text 同款同值（C 端 1440/50）。
         * [0 是**真值**不是"没数据" 2026-11-19 用户实报「转速怎么显示 --
         *  不是 0」] 原来两个分支都带 `> 0` ⇒ 变频器停机（频率 0 ⇒ 转速 0）
         * 时掉进 "--" ⇒ 现在：字段**存在**就用它（0 也显示 0），字段缺失
         * 才显示 "--"。 */
        var rpm = (typeof doc.speedRpm === "number")
          ? String(Math.round(doc.speedRpm))
          : ((typeof doc.outputFreqHz === "number")
             ? String(Math.round(doc.outputFreqHz * (1440.0 / 50.0)))
             : "--");
        vfdRpm.set(rpm);
        vfdPower.set(power);
        vfdTorque.set(torque);
        vfdMtemp.set(mtemp);
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
          var tr = HSF.el("tr", null, tds);
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

    function refreshDp(doc) {
      var ch = (doc.channels || [])[0];
      if (!doc.fresh || !ch) {
        dpValue.set("--", C_TEXT, "");
        setNote(dpNote, "无数据", C_OFFLINE);
        return;
      }
      /* [note 是说明，不是「值无效」2026-11-19 用户实报] 服务端对越限/负压
       * 给**线性值 + note**（如 -0.008 MPa +「低于 4.0mA（负压）」）⇒ 值
       * **照显示**，note 只改说明行的颜色。旧写法「note 非空 ⇒ 值显示 --」
       * 把负压读数整个吃掉了（负压是真实读数，现场要靠它判断）。
       * [与桌面客户端同口径] client_py/hsfsrv_client/pages_other.py
       * ::_refresh_dp（同日修复）。网页端模拟量页 page04.js 本来就是对的。 */
      dpValue.set(fmt3(ch.eng), C_TEXT, ch.unit || "");
      if (ch.note) {
        setNote(dpNote, ch.note, C_WARN);
      } else {
        setNote(dpNote, ch.name || "通道 0", C_SUB);
      }
    }

    function refreshFm(doc) {
      if (!doc.fresh) {
        fmFlow.set("--", C_TEXT, "");
        setNote(fmSub, "无数据", C_OFFLINE);
        return;
      }
      fmFlow.set(fmt2(doc.flow), C_TEXT, doc.flowUnitText || "");
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
          name = item.name || ("电动球阀" + (i + 1));
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
          color = ntu >= TURBIDITY_WARN_NTU ? C_FAULT : C_TEXT;
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
     *  [有界/无界判据] 服务端阶段计时 autoStageElapsedMs>=0 = 组合循环
     *  在跑（单模式服务端恒报 -1）；该相位 total>0 = 有换相定时（循环的
     *  反洗/顺洗、兜底开启的产水）⇒ 有界倒计时；total=0 = 无定时（单模式
     *  三模式、循环产水兜底关）⇒ 无界。兜底有无以服务端阶段 total 为准
     *  （服务端执行的就是远端设置卡下发的那份 cycle 配置；拿不到 ⇒
     *  total=0 ⇒ 按无界显示运行时间，绝不显示假倒计时）。 */
    function runSegmentKey(m) {
      if (m.autoRunning !== true) return "";
      var stageOn = !!m.autoStageKnown && (m.autoStageElapsedMs >= 0);
      if (stageOn && m.autoStageTotalMs > 0) return "";
      return (m.autoMode || "?") + "|" + (stageOn ? "cycle" : "single");
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
       * 与桌面端 pages_other._refresh_auto_card 同口径。 */
      if (!running && canAutoCtrl() && !modeUserPicked) {
        var srvMode = m.autoMode || "";
        if (srvMode) {
          for (var k = 0; k < modeSelect.options.length; k++) {
            if (modeSelect.options[k].value === srvMode) {
              if (modeSelect.selectedIndex !== k) modeSelect.selectedIndex = k;
              break;
            }
          }
        }
      }

      /* 循环运行中 ⇒ 下拉回显「自动（流量循环）」（上升沿一次，不每拍抢）。 */
      var cycleStage = !!running && !!m.autoStageKnown &&
        (m.autoStageElapsedMs >= 0);
      if (cycleStage && !cycleWasRunning) {
        for (var i = 0; i < modeSelect.options.length; i++) {
          if (modeSelect.options[i].value === "auto") {
            if (modeSelect.selectedIndex !== i) modeSelect.selectedIndex = i;
            break;
          }
        }
      }
      cycleWasRunning = cycleStage;

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
        var head = cycleStage ? ("自动循环（" + modeName + "）") : modeName;
        text = "运行中 · " + head + (phaseText ? (" · " + phaseText) : "");
        color = C_WARN;
        bold = true;
      } else if (m.autoPhase === "paused") {
        /* [paused 2026-11-19] 停止＝暂停 ⇒ 按启动从暂停处续跑，复位才
         * 清零（与桌面客户端同口径）。 */
        var pm = modeNameOf(m.autoMode) || m.autoMode || "";
        text = "已暂停" + (pm ? ("（" + pm + "）") : "")
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

      /* 运行表盘（2026-11-13 定案）：运行中（autoRunning=true）四个模式都
       * 显示；停止/急停态隐藏（急停后控制器 running=false，同闸收口）。
       * · 有界相位（autoStageElapsedMs>=0 且 total>0：循环的反洗/顺洗、
       *   兜底开启的产水）⇒ 维持现状的相位倒计时口径；
       * · 无界状态（单模式产水/反洗/顺洗、循环产水且兜底关）⇒ 中央显示
       *   「本次运行」时长：本地从进入该状态的状态沿起算（trackRunSegment
       *   在每帧先记账）；进页/断线重连已在跑、本地起点未知 ⇒ 回退显示
       *   服务端累计 autoRunMs（与状态行同源，不加「本次运行」标签），
       *   拿到下一个状态沿后恢复本次计时。秒级取整 ⇒ 文本每秒才变一次。 */
      trackRunSegment(m);
      var dialShow = m.autoRunning === true;
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
