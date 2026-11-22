/* -*- coding: utf-8 -*-
 * 页 14 远端设置：运行模式/自动运行/自动模式配置（含自动循环）/连接信息/
 * 采集配置/数据存储/登录策略/变频器参数。
 *
 * [对齐参考 RemoteSettingsPage]（client_py/hsfsrv_client/pages_other.py:4783）
 * 逐卡移植；数据面走 /api/mode（0x0105 轮询）与 /api/config（0x0116/0x0117）。
 *
 * [必须守住的三条参考纪律]
 *   1. **局部应答只刷新自己那一节**（参考 _populate 2026-11-10 实锅）：
 *      应答带哪节铺哪节；「应用登录策略」的应答只带 login 节 ⇒ 采集/存储/
 *      auto 卡**不得**被冲回缺省；storageDbPath/storageStats 只在应答
 *      确实带了这两个键时才刷新用量行。
 *   2. **模式切换 ≠ 启动自动运行**：两张卡两组按钮；切模式只是授权
 *      （「自动模式 · 已停止」），启动要再按一次「启动自动运行」。
 *   3. **自动模式配置 + 自动循环 = 一张卡、一个应用按钮**（2026-11-12
 *      用户定案）：同一份 auto 配置节一次下发 {modes, cycle}。
 *
 * [变频器参数 2026-11-18] 数据面走 /api/vfd/params（0x011B 转发）：显示值
 * 一律是**服务端从驱动器回读**的工程值（应答 params[].value）——绝不显示
 * 提交值；写后用应答的 params 刷新（「已写入并回读确认」才算成功）。读 =
 * 登录即可；写 = 管理员（RunLocked 由服务端终审，err 原样显示）。
 *
 * [权限] 无 PERM_ACCOUNT_ADMIN ⇒ 配置卡整体置灰（改了也应用不上）；
 * 模式/自动按钮分别按 PERM_MODE_SWITCH / PERM_AUTO_CONTROL，后端仍终审。
 *
 * [动作三态终态] 每个 POST 都过 withTimeout 闸（12s）：成功 / 失败 / 超时
 * 三选一落到状态行，不许停在「正在…」（浏览器 fetch 无内置超时，网页服务
 * 器半开时 await 会永远挂着）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* ---- 与 webui proto.py 同值的界面常量（下拉文案/缺省/范围） ---- */
  var STORAGE_POLICIES = [
    ["ring", "循环覆盖（删最旧，长期无人值守）"],
    ["stop", "到限停写（保留已有数据，只读）"],
    ["rotate", "分段滚动（写满换文件，按段归档）"],
  ];
  var STORAGE_POLICY_HINTS = {
    ring: "库满时删最旧的整段数据（三表同一时间线），磁盘占用封顶在上限。" +
          "适合长期无人值守。",
    stop: "库满后停止入库，已有数据原样保留只读。适合宁可丢新数据也" +
          "不能丢历史取证的场合。",
    rotate: "库满后改名归档成带时间戳的分段文件并另起新库，只保留最近" +
            "N 段。单段上限 = 总上限 / 段数。",
  };
  var HISTORY_TRIGGERS = [
    ["always", "持续采集入库（与自动模式无关）"],
    ["auto", "仅自动运行时入库（停机期间不记）"],
  ];
  var PRESS_COMPARE = [["gt", "大于阈值时切换"], ["lt", "小于阈值时切换"]];
  /* [换相策略 2026-11-14 用户定案] 切到本模式时，阀门切换（真机行程
     ~9s）期间该泵怎么处理——**逐泵**可配（现场常见：搅拌泵要保持运行，
     进水泵可以先停）：
       stopRestart 先停再启（保持既有逻辑，缺省）
       carry      保持上一模式状态（换相途中不动它）
       direct     直接切到当前模式状态（换相立即按本模式启停）
     与 server_py/client_py 的 proto.AUTO_SWITCH_POLICIES 逐值同步。 */
  var SWITCH_POLICIES = [
    /* [文案从简 2026-11-14] 桌面端同款：长文案会把两个下拉撑得一行放不下
       ⇒ 改成短文案，完整含义放在 title/tooltip 里（两端一致）。 */
    ["stopRestart", "先停再启"],
    ["carry", "保持上一模式"],
    ["direct", "直接切到当前模式"],
  ];
  var SWITCH_DEF = "stopRestart";
  /* [相机三键 2026-11-21 三端对齐补洞] 与 webui proto.py（与 client_py
   * proto.py 逐值同步）的 CAMERA_TRANSPORT / CAMERA_CODEC / CAMERA_HW
   * 三组常量同值。
   * 桌面端 RemoteSettingsPage 的「传输/编码/本端解码」三个下拉也用这三项；
   * 文案照桌面端逐字抄（长含义进 title，别撑破一行）。 */
  var CAM_TRANSPORTS = [
    ["stream", "画面直传（服务端不解码，20fps）"],
    ["jpeg", "JPEG 转发（旧路，排查兜底）"],
  ];
  var CAM_CODECS = [
    ["h264", "H.264（全端可解）"],
    ["h265", "H.265（带宽减半，需终端支持）"],
  ];
  var CAM_HW_DECODES = [
    ["auto", "解码自动（优先 GPU 硬解）"],
    ["on", "强制 GPU 硬解"],
    ["off", "强制软解"],
  ];
  /* [自动循环 · 新契约 2026-11-18 用户定案] 与 server_py/client_py 的 proto
     逐值同步（两端同口径，本页只是把同一份契约画出来）：
       · cycleModes = **有序数组**（顺序＝循环顺序；第一个＝**主运行阶段**；
         末位→首位＝闭环）；不在数组里的模式＝不参与循环；
       · stageCfg   = {modeId: {pressEnabled, pressThreshold, pressCompare,
         timerEnabled, timerMinutes}}——**每个参与阶段各自**一套切换条件；
         压差与定时**互不排斥**（先到先切）；两个都没开 ⇒ 服务端**明确拒**
         （fail-closed，文案原样显示）。
     [旧的扁平键不再提交] pressThreshold/pressCompare/timerFallback/
     timerMinutes/backwashMinutes/rinseMinutes 已作废——服务端会把旧键
     **折进对应阶段**（只改值）⇒ 网页端发一次旧键就会悄悄覆盖别的端配好
     的该阶段条件（跨端互踩）⇒ 本页只发 cycleModes + stageCfg。
     [兼容视图 stageMinutes] 服务端 GET 会回它（由 stageCfg 派生），本页
     **不消费**（界面按每阶段条件显示，不再有「旧版兼容」区）。 */
  var CYCLE = {
    enabledDef: true, pressDef: 0.5, pressMax: 10.0,
    compareDef: "gt",
    minMinutes: 1, maxMinutes: 1440,
  };
  /* [可参与循环的模式池] 与 server_py proto.AUTO_CYCLE_MODE_POOL 同源
     （＝AUTO_MODES 四模式：产水/反洗/顺洗/放水）——放水**可以**被勾进
     循环（缺省列表里没有它只是缺省，不是禁令）。 */
  var CYCLE_MODE_POOL = [
    ["produce", "产水模式"],
    ["backwash", "反洗模式"],
    ["rinse", "顺洗模式"],
    ["drain", "放水模式"],
  ];
  /* [缺省参与循环的模式与顺序] 产水→反洗→顺洗（与既有行为一致）。 */
  var CYCLE_MODES_DEF = ["produce", "backwash", "rinse"];
  /* [各阶段缺省切换条件] 与 client proto.AUTO_CYCLE_STAGE_CFG_DEFAULTS
     同值：产水＝**压差触发**（0.5MPa，大于才切）+ 定时关；反洗/顺洗/放水
     ＝**定时触发**（10 分钟）+ 压差关。 */
  var CYCLE_STAGE_DEF = {
    produce: { pressEnabled: true, pressThreshold: 0.5, pressCompare: "gt",
               timerEnabled: false, timerMinutes: 30 },
    backwash: { pressEnabled: false, pressThreshold: 0.5, pressCompare: "gt",
                timerEnabled: true, timerMinutes: 10 },
    rinse: { pressEnabled: false, pressThreshold: 0.5, pressCompare: "gt",
             timerEnabled: true, timerMinutes: 10 },
    drain: { pressEnabled: false, pressThreshold: 0.5, pressCompare: "gt",
             timerEnabled: true, timerMinutes: 10 },
  };
  /* [未知模式 id 的通用缺省] 与 client proto.AUTO_CYCLE_STAGE_CFG_DEF
     同值（压差关 + 定时开 10 分钟）。 */
  var CYCLE_STAGE_DEF_GEN = { pressEnabled: false, pressThreshold: 0.5,
                              pressCompare: "gt", timerEnabled: true,
                              timerMinutes: 10 };
  var RPM = { min: 0, max: 1440, def: 1440, ratedRpm: 1440.0, ratedHz: 50.0 };
  var POLL = { min: 50, max: 60000 };
  var STORAGE = { sampleDef: 60000, sampleMin: 1000, sampleMax: 3600000,
                  maxDef: 50, maxMin: 0, maxMax: 1024 * 1024, keepDef: 3 };
  /* 采集配置 7 行（参考 pages_other 2026-11-10「设备必须列全」口径）。 */
  var COLLECT_ROWS = [
    ["搅拌泵", "vfdEnabled", "vfdPollMs"],
    ["温湿度", "temphumEnabled", "temphumPollMs"],
    ["进水浊度仪", "turbidity1Enabled", "turbidity1PollMs"],
    ["出水浊度仪", "turbidity2Enabled", "turbidity2PollMs"],
    ["流量计", "flowmeterEnabled", "flowmeterPollMs"],
    ["模拟量", "analogEnabled", "analogPollMs"],
    ["数字量", "digitalEnabled", "digitalPollMs"],
  ];
  /* [变频器参数 2026-11-18] 0x3000 **原始字** → 中文（1 正转 / 2 反转 /
   * 3 停机）。[别与 XVfdRunState 混] 0x011B 应答的 runState 是驱动器
   * 0x3000 的**原始字**（与 server_py devices.ADDR_STATUS 同口径），**不是**
   * 遥测帧里的 XVfdRunState 枚举（1=停机/2=就绪/3=正转运行）——两者数值
   * 撞车但含义相反（桌面客户端 2026-11-18 踩过：运行中显示「变频器停机」、
   * 停机显示「变频器正转运行」，方向正好反了）。⇒ 本表单独命名，与桌面端
   * proto.VFD_RUN_WORD_TEXT 逐值一致。 */
  var VFD_RUN_WORD_TEXT = { 1: "正转运行", 2: "反转运行", 3: "停机" };
  /* P1 电机铭牌组（0xF100~0xF105）的工程值↔原始码换算：scale = 10^小数位
   * （原始码 = 工程值 × scale，如 P1.03 电流 2 位小数 ⇒ 原始码 123 =
   * 1.23 A）。与 webui proto.py / server_py hsfsrv proto.py 的 PARAM_DEFS
   * 逐值同步（内容出处 = docs/M512_511_512_513寄存器映射表.md §9.5 的
   * 0xF100 段；P1.04 小数位「待实机标定」，标定后三处一起改）。本表只用于
   * 「写入值」输入框的小数位与提交换算；显示值一律以**服务端回读**的
   * params[].value 为准。 */
  /* [枚举型参数的本地兜底选项] raw → 中文名。**服务端应答里带 options 时
   * 以服务端为准**（服务端是单一事实源）；本表只兜底老服务端。
   * 来源 = 说明书第 20 页「P1 组 电机参数」P1.00 电机类型选择。 */
  var VFD_PARAM_OPTIONS = {
    0xF100: [{ value: 0, label: "普通异步电机" },
             { value: 1, label: "变频异步电机" },
             { value: 2, label: "永磁同步电机" }],
  };
  var VFD_PARAM_SCALES = {
    0xF100: 1,    /* P1.00 电机类型选择（无纲量） */
    0xF101: 10,   /* P1.01 电机额定功率 kW */
    0xF102: 1,    /* P1.02 电机额定电压 V */
    0xF103: 100,  /* P1.03 电机额定电流 A */
    0xF104: 10,   /* P1.04 电机额定频率 Hz */
    0xF105: 1,    /* P1.05 电机额定转速 rpm */
  };
  /* [量程表（工程值）] 与桌面客户端 pages_other 的 PARAM_DEFS 的
   * vmin/vmax **同值**（vmin/vmax 是工程值量纲，raw 量纲 = vmin×scale）。
   * 输入框据此夹取：不给用户填「额定功率 999999 kW」这种越界值
   * （桌面端的微调框同样按 vmin/vmax 夹）。表外地址回落宽范围。 */
  var VFD_PARAM_RANGES = {
    0xF101: [1.0, 1000.0],      /* P1.01 额定功率 kW */
    0xF102: [1.0, 2000.0],      /* P1.02 额定电压 V */
    0xF103: [0.01, 655.35],     /* P1.03 额定电流 A */
    0xF104: [0.1, 5000.0],      /* P1.04 额定频率 Hz */
    0xF105: [1.0, 65535.0],     /* P1.05 额定转速 rpm */
  };

  var activeUnmount = null;

  HSF.router.register(14, {
    title: "远端设置",
    topics: [],
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  /* ==================== 小工具 ==================== */
  /** 实测一段文字的像素宽（探针 span，同字体）——不写死像素。 */
  function measureTextWidth(text, font) {
    var probe = document.createElement("span");
    probe.style.cssText = "position:absolute;left:-9999px;top:0;" +
      "white-space:nowrap;visibility:hidden;";
    if (font) probe.style.font = font;
    probe.textContent = text || "";
    document.body.appendChild(probe);
    var w = probe.getBoundingClientRect().width;
    document.body.removeChild(probe);
    return w;
  }

  /**
   * 给 <select> 设**下限宽度**：装得下最长选项（2026-11-18：中文类型名被裁）。
   *
   * [要点] ① 必须在控件**已挂进 DOM 之后**调用——游离元素的
   *   getComputedStyle 全是空串 ⇒ 量出来的文字宽偏小 ⇒ 字还是被裁
   *   （本次实踩）；② 只设 min-width、不设 width——`<select>` 的自然宽度
   *   本来就是最宽选项（浏览器契约），写死 width 反而可能与兄弟行不齐；
   *   ③ 右侧内边距已含自绘箭头的留白 ⇒ 文本宽 + 左右内边距 + 余量。
   */
  function fitSelectWidth(sel) {
    if (!sel || !sel.options || !sel.options.length) return;
    if (!sel.isConnected) return;            /* 游离元素量不准 ⇒ 不量 */
    var cs = getComputedStyle(sel);
    var font = cs.font || (cs.fontSize + " " + cs.fontFamily);
    var longest = "";
    for (var i = 0; i < sel.options.length; i++) {
      var t = String(sel.options[i].text || "");
      if (t.length > longest.length) longest = t;
    }
    var tw = measureTextWidth(longest, font);
    var pl = parseFloat(cs.paddingLeft || 0) || 0;
    var pr = parseFloat(cs.paddingRight || 0) || 0;
    var need = Math.ceil(tw + pl + pr + 6);
    var cur = parseFloat(cs.minWidth || 0) || 0;
    if (need > cur) sel.style.minWidth = need + "px";
  }

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }
  function numInput(min, max, step, value, wide) {
    return HSF.el("input.input" + (wide ? ".num-wide" : ".num"), {
      type: "number", min: String(min), max: String(max),
      step: String(step), value: String(value),
    });
  }
  /**
   * 定点小数框（参考 QDoubleSpinBox.setDecimals(n)：显示固定 n 位小数）。
   * HTML 的 number 框会把 "0.600" 规范化成 "0.6" ⇒ 用 text 框承载，
   * 失焦时按 n 位定点回写（值语义仍是 float，载荷不变）。
   */
  function fixedInput(min, max, value, digits, wide) {
    var inp = HSF.el("input.input" + (wide ? ".num-wide" : ".num"), {
      type: "text", inputmode: "decimal",
      value: Number(value).toFixed(digits),
    });
    inp.addEventListener("change", function () {
      var v = parseFloat(inp.value);
      if (isNaN(v)) v = value;
      v = Math.max(min, Math.min(max, v));
      inp.value = v.toFixed(digits);
    });
    return inp;
  }
  function clampInt(input, def, min, max) {
    var v = parseInt(input.value, 10);
    if (isNaN(v)) v = def;
    return Math.max(min, Math.min(max, v));
  }
  function clampFloat(input, def, min, max) {
    var v = parseFloat(input.value);
    if (isNaN(v)) v = def;
    return Math.max(min, Math.min(max, v));
  }
  function selectEl(pairs, tip, ariaLabel) {
    var sel = HSF.el("select.input");
    pairs.forEach(function (p) {
      var opt = HSF.el("option", { text: p[1] });
      opt.value = p[0];
      sel.appendChild(opt);
    });
    if (tip) sel.title = tip;
    /* [无障碍名] 行首标签是 span 不是 label ⇒ 显式给 aria-label，
       否则读屏/自动化只能拿到 title（提示语）当名字。 */
    if (ariaLabel) sel.setAttribute("aria-label", ariaLabel);
    return sel;
  }
  function checkRow(text, checked, tip) {
    var cb = HSF.el("input.check", { type: "checkbox" });
    cb.checked = !!checked;
    var row = HSF.el("label.check-row", null, [cb, HSF.el("span", { text: text })]);
    if (tip) row.title = tip;
    return { cb: cb, row: row };
  }
  function setEnabled(el, on) {
    if (el.disabled !== !on) el.disabled = !on;
  }
  function caption(text) { return HSF.el("div.caption", { text: text }); }

  /* ---- 动作超时闸（三态终态铁律） ----
   *
   * [为什么要自己加超时] 浏览器 fetch **没有内置超时**：网页服务器进程假死
   * 或 TCP 半开时，await 会永远挂着 ⇒ 状态行停在「正在切换运行模式…」，
   * 违反「成功/失败/超时三选一终态」。后端每个请求都有 8s 超时并回
   * 503/504（api.py 的 hub.request timeout），正常路径不会走到本闸；
   * 本闸兜的是「连应答都没有」的悬挂。
   */
  var ACTION_TIMEOUT_MS = 12000;

  /**
   * 给一个 fetch 承诺加超时闸：超时 → {ok:false, err:"…超时…"}（终态）。
   * 异常 → {ok:false, err:"…失败：…"}（终态）。原承诺迟到也不改写结果。
   */
  function withTimeout(promise, what) {
    return new Promise(function (resolve) {
      var settled = false;
      var timer = setTimeout(function () {
        if (settled) return;
        settled = true;
        resolve({ ok: false, err: what + "超时（" +
                 (ACTION_TIMEOUT_MS / 1000) + " 秒无应答，请检查连接后重试）" });
      }, ACTION_TIMEOUT_MS);
      promise.then(function (doc) {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(doc);
      }, function (err) {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve({ ok: false, err: what + "失败：" +
                 (err && err.message ? err.message : String(err)) });
      });
    });
  }

  /** 失败/超时的终态文案：超时单独成句（不混进「失败」）。 */
  function failText(prefix, err) {
    err = String(err == null ? "未知" : err);
    return /超时/.test(err) ? ("操作超时：" + err) : (prefix + err);
  }
  /**
   * 「应答带哪节铺哪节」的 JS 版判据。**不能用 truthy**：JS 里 {} 是真值，
   * 参考客户端 Python 的 `if collect:` 对空 dict 是假——照抄会退化成
   * 「应用登录策略的应答没带采集节 ⇒ 采集卡被冲回缺省」（2026-11-10
   * pages_other.py 实锅的 web 复刻，交互测试实测抓到）。必须数键。
   */
  function hasKeys(o) {
    return !!o && Object.keys(o).length > 0;
  }

  /* ---- 变频器参数小工具（0x011B） ---- */
  /** 参数换算 scale：先查 PARAM_DEFS 同步表；表外地址（服务端将来扩参数集）
   * 从应答 raw/value 反推。推不出（表外 + value=0）回 0 =「换算未知」，
   * 该项不给输入框（提交换算不了，不本地瞎猜）。 */
  function paramScale(addr, raw, value) {
    var s = VFD_PARAM_SCALES[addr];
    if (s) return s;
    var v = Number(value);
    var r = Number(raw);
    if (isFinite(v) && isFinite(r) && v > 0 && r > 0 && r % v === 0) {
      var k = Math.round(r / v);
      if (k === 1 || k === 10 || k === 100 || k === 1000) return k;
    }
    return 0;
  }
  /** scale → 小数位数（1/10/100/1000 → 0/1/2/3）。 */
  function paramDigits(scale) {
    return Math.max(0, Math.round(Math.log(scale) / Math.LN10));
  }
  /** 回读工程值 → 显示串（无效值 "--"，不编造）。 */
  function fmtParamValue(value, digits) {
    var v = Number(value);
    if (value === null || value === undefined || !isFinite(v)) return "--";
    return HSF.fmtFixedHalfEven(v, digits);
  }
  /** 0x011B 应答的 vfdFaulted/runState → 状态短语（缺键=未知，不编造）。 */
  function vfdRunStateText(doc) {
    var rs = doc ? doc.runState : undefined;
    var text;
    if (rs === undefined || rs === null || rs === "") {
      text = "状态未知";
    } else if (typeof rs === "string" && isNaN(Number(rs))) {
      text = rs;                        /* 非数字字符串原样显示 */
    } else {
      text = VFD_RUN_WORD_TEXT[Number(rs)] || ("状态码 " + rs);
    }
    if (doc && doc.vfdFaulted) text += " · 故障";
    return text;
  }

  /* ==================== 页面 ==================== */
  function mountPage(host, ctx) {
    var store = HSF.store;
    var app = HSF.app;
    var PERM = app.PERM;
    var unsubs = [];
    var timers = [];
    var modeBlocks = [];          /* 三个模式块控件 */
    var collectRows = [];         /* [keyEnabled, cb, keyPoll, spin] */
    var cfgControls = [];         /* 无管理权限整组置灰的控件 */
    var configFetched = false;
    var cycle = {};               /* 自动循环节控件（建卡前必须先有壳） */
    var vfdInputs = [];           /* 变频器参数可写项输入框（随管理权限置灰） */
    var vfdFetched = false;       /* 入页自动读取一次的闸（不随节拍轮询） */

    /* ---- 卡 1：运行模式 ---- */
    var modeLabel = HSF.el("div.mode-big", { text: "未知" });
    var btnManual = HSF.el("button.btn", { type: "button", text: "切换到手动" });
    var btnAuto = HSF.el("button.btn", { type: "button", text: "切换到自动" });
    var cardMode = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "运行模式" }),
      modeLabel,
      caption("自动模式会拒绝全部远程控制（设备由现场逻辑驱动）；" +
              "手动模式允许管理员远程操作。"),
      HSF.el("div.form-row", null, [btnManual, btnAuto]),
    ]);

    /* ---- 卡 2：自动运行（与模式切换**分开**） ---- */
    var autoLabel = HSF.el("div.auto-big.dim", { text: "--" });
    var btnAutoStart = HSF.el("button.btn", { type: "button", text: "启动自动运行" });
    var btnAutoStop = HSF.el("button.btn", { type: "button", text: "停止自动运行" });
    var cardAutoRun = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "自动运行" }),
      autoLabel,
      caption("切到自动模式只是授权；要真正开始自动运行，需按下面的" +
              "「启动自动运行」。服务端重启后控制器一律为停止状态。"),
      HSF.el("div.form-row", null, [btnAutoStart, btnAutoStop]),
    ]);

    /* ---- 卡 3：自动模式配置 + 自动循环（一次提交） ---- */
    var devicesList = (HSF.registry && HSF.registry.autoDevices) || [];
    /* [只发服务端认识的模式 2026-11-18] null = 未知（还没收到 auto 回显
     * ）⇒ 只发通用子集；已知 ⇒ 只发服务端回显里出现过的键 ⇒ C 服务端
     * （只认三个模式）不会因为多一个「放水模式」而**整包拒**。 */
    var serverKnownModes = null;
    var skippedModes = [];
    /* [2026-11-18 放水模式] 配置块用**可配置**的模式集合（autoConfigModes
       = 产水/反洗/顺洗/放水 四块）；首页下拉（page00）用 autoModes
       （四模式 + 组合「自动」）⇒ 放水模式在两边都有，只是**不进组合循环**
       （循环子模式另配，见「自动循环」段）。
       兜底仍取 autoModes（老后端没有 autoConfigModes 时不至于空卡）。 */
    var modesList = (HSF.registry && HSF.registry.autoConfigModes) ||
                    (HSF.registry && HSF.registry.autoModes) || [];
    var autoCardBody = [HSF.el("div.card-title", { text: "自动模式配置" }),
      caption("每个模式各自设置：勾选自动运行进入该模式时要启动的设备（可多选），" +
              "并设定搅拌电机的运行转速与方向。改完按卡底的「应用」一次性下发。"
              + "「放水模式」与其它三个一样可在主界面选来启动，也可以**勾进"
              + "下方的自动循环**（循环的参与模式与顺序可配）。")];
    modesList.forEach(function (m) {
      if (m.id === "auto") return;        /* 组合模式不进配置块（参考同款） */
      var blk = buildModeBlock(m.id, m.name, devicesList);
      modeBlocks.push(blk);
      autoCardBody.push(blk.root);
    });
    autoCardBody.push(buildCycleSection());
    var btnAutoApply = HSF.el("button.btn", {
      type: "button", text: "应用自动配置（需管理员）",
    });
    btnAutoApply.title = "一次性提交四个模式（产水/反洗/顺洗/放水）的设备/" +
      "转速/方向/阀复位/换相策略设置，以及自动循环的参与模式/顺序与" +
      "各阶段的切换条件（压差/定时）";
    cfgControls.push(btnAutoApply);       /* 无管理权限随整卡置灰（参考同款） */
    autoCardBody.push(HSF.el("div.form-row", null, [btnAutoApply]));
    var cardAutoCfg = HSF.el("div.card", null, autoCardBody);

    /* ---- 卡 4：连接信息 ---- */
    var connLabel = HSF.el("div", { text: "--" });
    var cardConn = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "连接信息" }), connLabel]);

    /* ---- 卡 5：采集配置 ---- */
    var grid = HSF.el("div.cfg-grid");
    grid.appendChild(HSF.el("span.cfg-head", { text: "设备" }));
    grid.appendChild(HSF.el("span.cfg-head", { text: "采集" }));
    grid.appendChild(HSF.el("span.cfg-head", { text: "周期" }));
    COLLECT_ROWS.forEach(function (def) {
      grid.appendChild(HSF.el("span", { text: def[0] }));
      var cb = HSF.el("input.check", { type: "checkbox" });
      grid.appendChild(cb);
      var spin = numInput(POLL.min, POLL.max, 50, 200, true);
      /* 单位后缀在框内（参考 spin.setSuffix(" ms")，pages_other.py:5117）。 */
      grid.appendChild(HSF.el("span.spin-unit", null, [
        spin, HSF.el("span.spin-suffix", { text: " ms" })]));
      collectRows.push([def[1], cb, def[2], spin]);
      cfgControls.push(cb, spin);
    });
    var btnCollectRefresh = HSF.el("button.btn", { type: "button", text: "刷新" });
    var btnCollectApply = HSF.el("button.btn", { type: "button", text: "应用（需管理员）" });
    cfgControls.push(btnCollectApply);
    /* ---- 模拟量通道与量程（2026-11-20·对齐桌面客户端；二改：量程=下限~上限）----
     * 模拟量模块挂两台传感器：压差计（缺省 CH0，铭牌 0~1 MPa）+ 电机温度
     * 传感器（缺省 CH1，0~100 ℃）。现场维护更换传感器（换通道接线/换量程
     * 不同的表头）不用改代码。
     * [量程=下限~上限 2026-11-20 用户口径「只要填上限和下限即可」]
     * 4mA → 下限、20mA → 上限（服务端 lowValue 承载），「温度满量程」废弃。
     * [压差计负压口径] 4mA 以下按线性继续显示负压（既有特殊处理保留）。 */
    /* [通道号用下拉 2026-11-20 用户口径「是不是用下拉框更合适」] 通道是
     * 离散选择（0..模块路数-1），数字框一格格点很难受；条目数随 GET 回的
     * analogChannels 重建（缺键回落 8 路），保留选中值。 */
    var anChannels = 8;
    function fillChannelSelect(sel, keep) {
      var cur = (keep !== undefined) ? keep : sel.value;
      while (sel.firstChild) sel.removeChild(sel.firstChild);
      for (var ch = 0; ch < anChannels; ch++) {
        var opt = HSF.el("option", { text: "CH" + ch });
        opt.value = String(ch);
        sel.appendChild(opt);
      }
      sel.value = String(cur === undefined || cur === null ? 0 : cur);
      if (sel.value === "" || sel.selectedIndex < 0) sel.value = "0";
    }
    var anPressCh = HSF.el("select.input");
    anPressCh.setAttribute("aria-label", "压差计通道");
    anPressCh.title = "压差计接在模拟量模块的第几路（4~20mA）；换接线后在这里改";
    var anPressLow = numInput(-10, 100, 0.1, 0, false);
    anPressLow.setAttribute("aria-label", "压差下限");
    anPressLow.title = "压差计量程下限（MPa）：4mA 对应的值；低于它的电流按线性继续显示为负压";
    var anPressLowWrap = HSF.el("span.spin-unit", null, [
      anPressLow, HSF.el("span.spin-suffix", { text: " MPa" })]);
    var anPressHigh = numInput(0.01, 100, 0.1, 1, false);
    anPressHigh.setAttribute("aria-label", "压差上限");
    anPressHigh.title = "压差计量程上限（MPa）：20mA 对应的值（铭牌量程，常见 1.0）";
    var anPressHighWrap = HSF.el("span.spin-unit", null, [
      anPressHigh, HSF.el("span.spin-suffix", { text: " MPa" })]);
    var anTempCh = HSF.el("select.input");
    anTempCh.setAttribute("aria-label", "温度传感器通道");
    anTempCh.title = "电机温度传感器接在第几路（缺省 CH1）；与压差计同一通道时温度判定失效";
    fillChannelSelect(anPressCh, 0);
    fillChannelSelect(anTempCh, 1);
    var anTempLow = numInput(-100, 100, 5, 0, false);
    anTempLow.setAttribute("aria-label", "温度下限");
    anTempLow.title = "温度传感器量程下限（℃）：4mA 对应的值";
    var anTempLowWrap = HSF.el("span.spin-unit", null, [
      anTempLow, HSF.el("span.spin-suffix", { text: " ℃" })]);
    var anTempHigh = numInput(-50, 500, 5, 100, false);
    anTempHigh.setAttribute("aria-label", "温度上限");
    anTempHigh.title = "温度传感器量程上限（℃）：20mA 对应的值";
    var anTempHighWrap = HSF.el("span.spin-unit", null, [
      anTempHigh, HSF.el("span.spin-suffix", { text: " ℃" })]);
    /* [4mA 以下有效 2026-11-20 用户口径「增加 0-4 有效选项」] 开=0~4 段
     * 按线性延续（压差计的负压特殊处理就是它）；关=欠量程钳到下限。
     * 压差计缺省开（4mA 以下是真负压）；温度缺省关。 */
    var anPressBelow4Row = checkRow("压差计 4mA 以下有效（负压线性延续）",
      true, "勾选：低于 4mA 的电流按线性继续显示负压（铭牌 0~1 的表可显示吸力）；不勾：按欠量程钳到量程下限");
    var anTempBelow4Row = checkRow("温度 4mA 以下有效（线性延续）",
      false, "勾选：低于 4mA 的电流按线性继续算温度（适配 0~4 段有输出的传感器）；不勾：按欠量程钳到量程下限（超温保护不会误动作）");
    var anGrid = HSF.el("div.cfg-grid");
    anGrid.appendChild(HSF.el("span", { text: "压差计通道" }));
    anGrid.appendChild(anPressCh);
    anGrid.appendChild(HSF.el("span", { text: "" }));
    anGrid.appendChild(HSF.el("span", { text: "温度通道" }));
    anGrid.appendChild(anTempCh);
    anGrid.appendChild(HSF.el("span", { text: "" }));
    cfgControls.push(anPressCh, anPressLow, anPressHigh,
                     anTempCh, anTempLow, anTempHigh,
                     anPressBelow4Row.cb, anTempBelow4Row.cb);
    var cardCollect = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "采集配置" }),
      grid,
      caption("流量计/两台浊度仪各按自己的周期采集（共用 485 但节拍互" +
              "不影响）；两台浊度仪是两台独立设备，采集开关各自独立。" +
              "电磁阀/进水泵跟随数字量的开关。改开关立即生效（被停的" +
              "设备健康转数据过期），改周期下一拍生效。"),
      HSF.el("div.card-title", { text: "模拟量通道与量程" }),
      anGrid,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "压差下限" }), anPressLowWrap,
        HSF.el("span.lbl", { text: "压差上限" }), anPressHighWrap]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "温度下限" }), anTempLowWrap,
        HSF.el("span.lbl", { text: "温度上限" }), anTempHighWrap]),
      anPressBelow4Row.row,
      anTempBelow4Row.row,
      HSF.el("div.form-row", null, [btnCollectRefresh, btnCollectApply]),
    ]);

    /* ---- 卡 6：数据存储 ---- */
    var stEnable = checkRow("启用落盘", true);
    var stPath = HSF.el("input.input", {
      type: "text", style: { flex: "1", minWidth: "220px" },
      placeholder: "空 = 服务端程序目录\\hsf_data.db；相对路径也按程序目录展开",
    });
    var stSample = numInput(STORAGE.sampleMin, STORAGE.sampleMax, 1000,
                            STORAGE.sampleDef, true);
    /* 单位后缀在框内（参考 QSpinBox.setSuffix(" ms")，pages_other.py:5161）。 */
    var stSampleWrap = HSF.el("span.spin-unit", null, [
      stSample, HSF.el("span.spin-suffix", { text: " ms" })]);
    var stTrigger = selectEl(HISTORY_TRIGGERS,
      "持续采集入库：只要落盘开着就一直按周期记历史；\n" +
      "仅自动运行时入库：只有自动模式在跑时才记（停机期间不记）",
      "入库触发");
    var stMax = numInput(STORAGE.maxMin, STORAGE.maxMax, 50, STORAGE.maxDef, true);
    stMax.title = "0 = 不限；库达到上限后按下方策略处理";
    var stMaxWrap = HSF.el("span.spin-unit", null, [
      stMax, HSF.el("span.spin-suffix", { text: " MB" })]);
    var stMaxHint = HSF.el("span.caption", { text: "" });
    var stPolicy = selectEl(STORAGE_POLICIES, null, "写满后");
    var stKeep = numInput(1, 99, 1, STORAGE.keepDef);
    stKeep.title = "仅「分段滚动」策略用：总上限会均分到各段";
    var stPolicyHint = caption("");
    var usageFill = HSF.el("div.usage-fill");
    var usageBar = HSF.el("div.usage-bar", null, [usageFill]);
    var usageLabel = HSF.el("div.usage-label", { text: "用量：--" });
    /* [.path] 绝对路径没有折行点（无空格）⇒ 窄屏会顶出卡片与视口
     * （2026-11-20 实测：390px 下 scrollWidth=855 / clientWidth=354）。
     *   ⇒ 路径标签单独挂 .path（CSS 里给 break-all）⇒ 长路径折成多行。
     *   用量标签不挂：它是「用量：12.3 MB / 50 MB」这种按空格折行的文案。 */
    var dbPathLabel = HSF.el("div.usage-label.path", { text: "当前库文件：--" });
    var btnStorageSave = HSF.el("button.btn", { type: "button", text: "保存（需管理员）" });
    cfgControls.push(stEnable.cb, stPath, stSample, stTrigger, stMax,
                     stPolicy, stKeep, btnStorageSave);
    var cardStorage = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "数据存储（SQLite：历史/日志/报警）" }),
      stEnable.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "库文件路径" }), stPath]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "入库周期" }), stSampleWrap]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "入库触发" }), stTrigger]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "容量上限" }), stMaxWrap, stMaxHint,
        HSF.el("span.lbl", { text: "写满后" }), stPolicy,
        HSF.el("span.lbl", { text: "保留分段" }), stKeep]),
      stPolicyHint,
      usageBar, usageLabel, dbPathLabel,
      HSF.el("div.form-row", null, [btnStorageSave]),
    ]);
    function syncPolicyUi() {
      var key = stPolicy.value || "ring";
      setText(stPolicyHint, STORAGE_POLICY_HINTS[key] || "");
      setEnabled(stKeep, canCfg() && key === "rotate");
    }
    function syncMaxHint() {
      var unlimited = clampInt(stMax, 0, 0, STORAGE.maxMax) === 0;
      setText(stMaxHint, unlimited ? "（0 = 不限）" : "");
    }
    stPolicy.addEventListener("change", syncPolicyUi);
    stMax.addEventListener("input", syncMaxHint);
    syncMaxHint();

    /* ---- 卡 6-2：运行保护（2026-11-15·对齐桌面客户端）----
     * 进水泵开着、流量计仍报「空管」持续到设定时长 ⇒ 按配置动作（停设备 /
     * 只报警）。时长 0 = 关闭该保护（缺省不启用：现场流量计的空管位可能
     * 长期挂着，缺省启用会"每次开泵一分钟就停线"）。 */
    var protectStop = checkRow("超时后停止设备", true,
      "勾选：超时后停变频器/进水泵并关阀；不勾：只报警、不动设备");
    var protectTimeout = numInput(0, 3600, 10, 0, false);
    protectTimeout.title = "进水泵开着且流量计报空管，持续这么久后动作；0 = 关闭该保护";
    var protectTimeoutWrap = HSF.el("span.spin-unit", null, [
      protectTimeout, HSF.el("span.spin-suffix", { text: " 秒" })]);
    /* ---- 压差过大保护（2026-11-15·对齐桌面客户端）----
     * 压差连续超上限约 2 秒 ⇒ 触发（滤芯堵塞/水路异常时泵在憋压）。
     * 上限与现场工艺强相关 ⇒ 缺省不启用，由现场自己定阈值。
     * [界面用 kPa、协议用 MPa] 800 kPa = 0.8 MPa（正常运行约 500 kPa）。 */
    var pressHighEnable = checkRow("启用压差过大保护", false,
      "不勾 = 不判压差（保护关闭）");
    var pressHighLimit = numInput(0, 10000, 10, 800, false);
    pressHighLimit.title = "压差上限（kPa）：800 kPa = 0.8 MPa";
    var pressHighStop = checkRow("触发后停止设备", true,
      "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备");
    /* ---- 电机超温保护（2026-11-20·对齐桌面客户端）----
     * 电机温度（模拟量，通道在采集配置里配）连续超阈值约 2 秒 ⇒ 触发：
     * 报警（进报警表）+ 按配置停设备；锁存 —— 温度回落后也需人工复位。
     * 缺省不启用（阈值与现场电机强相关，写死必误动作）。 */
    var motorTempEnable = checkRow("启用电机超温保护", false,
      "不勾 = 不判电机温度（保护关闭）");
    var motorTempLimit = numInput(0, 200, 5, 75, false);
    motorTempLimit.title = "电机超温阈值（℃）：温度连续超过它约 2 秒即触发";
    var motorTempLimitWrap = HSF.el("span.spin-unit", null, [
      motorTempLimit, HSF.el("span.spin-suffix", { text: " ℃" })]);
    var motorTempStop = checkRow("触发后停止设备", true,
      "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备");
    var btnProtectApply = HSF.el("button.btn", { type: "button",
      text: "应用运行保护（需管理员）" });
    cfgControls.push(protectStop.cb, protectTimeout, btnProtectApply,
                     pressHighEnable.cb, pressHighLimit, pressHighStop.cb,
                     motorTempEnable.cb, motorTempLimit, motorTempStop.cb);
    var cardProtect = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "运行保护" }),
      caption("进水泵开启后，若流量计仍报「空管」并持续到设定时长，说明水路没通" +
              "（或流量计异常）：可只报警留记录，也可直接停止设备。"),
      protectStop.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "空管判定时长" }), protectTimeoutWrap,
        HSF.el("span.caption", { text: "（0 = 关闭该保护）" })]),
      HSF.el("div.card-title", { text: "压差过大保护" }),
      caption("压差连续超过上限并持续约 2 秒即触发（滤芯堵塞/水路异常时泵会" +
              "憋压，继续跑会伤设备）。上限与现场工艺相关，请按实际情况设定；" +
              "不启用则不判。"),
      pressHighEnable.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "压差上限" }),
        HSF.el("span.spin-unit", null, [pressHighLimit,
          HSF.el("span.spin-suffix", { text: " kPa" })])]),
      pressHighStop.row,
      HSF.el("div.card-title", { text: "电机超温保护" }),
      caption("电机温度连续超过阈值并持续约 2 秒即触发：报警（记录到报警表）" +
              "并可停止设备。报警锁存 —— 温度回落后也需人工复位才能再启动。" +
              "温度传感器接在哪一路、量程多少，在「采集配置 → 模拟量通道与" +
              "量程」里配。不启用则不判。"),
      motorTempEnable.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "超温阈值" }), motorTempLimitWrap]),
      motorTempStop.row,
      HSF.el("div.form-row", null, [btnProtectApply]),
    ]);

    /* ---- 卡 6-3：相机（2026-11-19·对齐桌面客户端）----
     * 服务端自己去拉相机的 RTSP、解码、抽帧；客户端在「相机」页看画面。
     * [帧率档跟随相机自身] 实测本相机能力表的帧率只有 1/10/15/20（码值
     * 65536+fps）⇒ 下拉给这四档 + 2/5（低于相机帧率，服务端抽帧）；
     * **没有 25/30 档**（相机本身就没有）。
     * [口令不回显] 服务端只回 hasPassword、不回明文 ⇒ 留空 = 不改。 */
    var camEnabled = checkRow("启用相机", false,
      "勾选：服务端开始拉流；不勾：服务端不采集");
    /* [连接方式 + 地址框两用 2026-11-20 用户口径「mac 和 ip 合并成一个
     * 输入框，根据下拉是 ip 或 mac」] 切下拉时框内容/占位符跟着换，
     * 两边的值都留在缓存里（切来切去不丢）；协议仍是 ip/mac 两键各发各的。 */
    var camAddrIp = "";
    var camAddrMac = "";
    var camResolve = selectEl(
      [["ip", "按 IP 连接"], ["mac", "按 MAC 连接（相机动态 IP）"]],
      "按 MAC：服务端每次连接前把 MAC 解析成当前 IP（先查本机 ARP，" +
      "查不到对网段探测一轮再查）——相机换了 DHCP 地址也能自动跟上",
      "相机连接方式");
    var camAddr = HSF.el("input.input", { type: "text", value: "",
      placeholder: "相机 IP，如 192.168.1.181" });
    camAddr.setAttribute("aria-label", "相机地址（IP 或 MAC）");
    camAddr.title = "按 IP 连接时填相机 IP；按 MAC 连接时填相机 MAC" +
      "（冒号/连字符/裸 hex 都认，相机标签或路由器 DHCP 列表里能查到）";
    function syncCamAddr() {
      if (camResolve.value === "mac") {
        camAddr.placeholder = "相机 MAC，如 AA:BB:CC:DD:EE:FF";
        camAddr.value = camAddrMac;
      } else {
        camAddr.placeholder = "相机 IP，如 192.168.1.181";
        camAddr.value = camAddrIp;
      }
    }
    camResolve.addEventListener("change", function () {
      if (camResolve.value === "mac") {
        camAddrIp = String(camAddr.value || "").trim();
      } else {
        camAddrMac = String(camAddr.value || "").trim();
      }
      syncCamAddr();
    });
    var camPort = numInput(1, 65535, 1, 554, false);
    camPort.setAttribute("aria-label", "相机端口");
    var camReconnect = checkRow("断线重连", true,
      "勾选：断线后每隔「重连间隔」自动重试；不勾：断线后停在错误提示上，重新点「应用」才再试");
    var camReconnectS = numInput(1, 3600, 5, 5, true);
    camReconnectS.setAttribute("aria-label", "重连间隔");
    camReconnectS.title = "断线后每隔多少秒重试一次（1~3600 秒）";
    var camReconnectSWrap = HSF.el("span.spin-unit", null, [
      camReconnectS, HSF.el("span.spin-suffix", { text: " 秒" })]);
    var camUser = HSF.el("input.input", { type: "text", value: "admin" });
    camUser.setAttribute("aria-label", "相机账号");
    var camPass = HSF.el("input.input", { type: "password", value: "" });
    camPass.setAttribute("aria-label", "相机口令");
    camPass.placeholder = "留空 = 不改";
    var camStream = selectEl(
      [["main", "主码流（高分辨率）"], ["minor", "子码流（低分辨率，推荐）"]],
      "取哪一路码流", "相机码流");
    var camFps = selectEl(
      [["1", "1 fps"], ["2", "2 fps（抽帧）"], ["5", "5 fps（抽帧）"],
       ["10", "10 fps"], ["15", "15 fps"], ["20", "20 fps"]],
      "跟随相机自身档位（1/10/15/20）；2/5 为服务端抽帧", "相机帧率");
    var camWidth = numInput(128, 4096, 64, 704, false);
    camWidth.setAttribute("aria-label", "画面宽度");
    var camQuality = numInput(1, 100, 10, 80, false);
    camQuality.setAttribute("aria-label", "相机画质");
    /* [相机三键 2026-11-21] 传输/编码/本端解码——桌面端同款三项，
     * 缺省值照 proto CAMERA_*_DEF（stream / h264 / auto）。 */
    var camTransport = selectEl(CAM_TRANSPORTS,
      "画面直传=服务端不解码直接转发编码流（20fps，省 CPU）；" +
      "JPEG 转发=老路（排查兜底用）", "相机传输方式");
    var camCodec = selectEl(CAM_CODECS,
      "H.264 全端可解；H.265 带宽减半但需浏览器/终端支持", "相机编码");
    var camHw = selectEl(CAM_HW_DECODES,
      "本端（浏览器）解码方式；服务端直传时本项只影响画面显示端",
      "本端解码");
    var camState = HSF.el("span.caption", { text: "" });
    var btnCamApply = HSF.el("button.btn", { type: "button",
      text: "应用相机配置（需管理员）" });
    btnCamApply.addEventListener("click", applyCamera);
    cfgControls.push(camEnabled.cb, camAddr, camPort, camUser, camPass,
                     camStream, camFps, camWidth, camQuality,
                     camTransport, camCodec, camHw,
                     camResolve, camReconnect.cb, camReconnectS,
                     btnCamApply);
    var cardCamera = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "相机" }),
      caption("服务端连接网络相机并采集画面，客户端在「相机」页观看。" +
              "帧率跟随相机自身能力（1/10/15/20 fps）；画质越大越清晰、" +
              "画面越大、占用带宽越多。"),
      camEnabled.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "连接方式" }), camResolve,
        HSF.el("span.lbl", { text: "地址" }), camAddr,
        HSF.el("span.lbl", { text: "端口" }), camPort,
        /* [2026-11-22 触达] 用 label 行（带「断线重连」文字）而不是裸
         * checkbox：裸框命中区只有 22px，触摸下难点；label 整行 ≥44px。 */
        camReconnect.row, camReconnectSWrap]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "账号" }), camUser,
        HSF.el("span.lbl", { text: "口令" }), camPass]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "码流" }), camStream,
        HSF.el("span.lbl", { text: "帧率" }), camFps,
        HSF.el("span.lbl", { text: "画面宽度" }), camWidth,
        HSF.el("span.lbl", { text: "画质" }), camQuality]),
      /* [相机三键 2026-11-21] 三项单独一行——塞进上行在窄窗会被压窄裁字。 */
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "传输" }), camTransport,
        HSF.el("span.lbl", { text: "编码" }), camCodec,
        HSF.el("span.lbl", { text: "本端解码" }), camHw]),
      HSF.el("div.form-row", null, [btnCamApply, camState]),
    ]);

    /* ---- 卡 7：变频器参数（0x011B 读真实值/写/回读刷新）----
     * 显示值一律是**服务端从驱动器回读**的工程值（应答 params[].value）——
     * 绝不显示提交值；写后用应答的 params 刷新（「已写入并回读确认」才算
     * 成功）。读 = 登录即可；写 = 管理员（输入框随管理权限置灰，照本页
     * 「改了应用不上就别让人误以为生效」的口径）。 */
    var vfdGrid = HSF.el("div.cfg-grid");
    var vfdStateLabel = caption("变频器：--");
    var vfdStatus = caption("尚未读取（连上服务端后自动读取一次，" +
                            "或点「刷新（读真实值）」）");
    var btnVfdRefresh = HSF.el("button.btn",
                               { type: "button", text: "刷新（读真实值）" });
    var btnVfdApply = HSF.el("button.btn",
                             { type: "button", text: "应用到变频器（需管理员）" });
    btnVfdApply.title = "把「写入值」逐项写进变频器，并按应答的回读值刷新显示";
    cfgControls.push(btnVfdApply);
    var cardVfdParams = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "变频器参数" }),
      caption("P1 电机铭牌组（0xF100~0xF105）。当前值是服务端刚从变频器回读" +
              "的真实值（不是本地缓存的提交值）；写入后自动回读确认。变频器" +
              "运行中服务端拒绝写参数（RunLocked：请先停止再写）。"),
      vfdStateLabel,
      vfdGrid,
      vfdStatus,
      HSF.el("div.form-row", null, [btnVfdRefresh, btnVfdApply]),
    ]);

    /* ---- 卡 8：登录策略 ---- */
    var loginAnon = checkRow("允许匿名登录（只读）", false);
    var loginConcur = checkRow("允许同账并发（同账户多端在线）", false);
    var loginMax = numInput(0, 256, 1, 0, true);   /* 初值 0=不限（参考 QSpinBox min 起步） */
    loginMax.title = "0 = 不限";
    /* 单位后缀在框内（参考 QSpinBox.setSuffix(" 个")，pages_other.py:5268）。 */
    var loginMaxWrap = HSF.el("span.spin-unit", null, [
      loginMax, HSF.el("span.spin-suffix", { text: " 个" })]);
    var btnLoginSave = HSF.el("button.btn", { type: "button", text: "应用登录策略（需管理员）" });
    cfgControls.push(loginAnon.cb, loginConcur.cb, loginMax, btnLoginSave);
    var cardLogin = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "登录策略（账号）" }),
      loginAnon.row, loginConcur.row,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "会话上限" }), loginMaxWrap,
        HSF.el("span.caption", { text: "（0 = 不限）" })]),
      caption("写入立即对下一次登录生效并落盘 hsf_login_policy.json；" +
              "已在线会话不受影响（关闭同账并发不会踢人）。"),
      HSF.el("div.form-row", null, [btnLoginSave]),
    ]);

    /* ==================== 页内导航（TOC）+ 宽屏两列 ====================
     * [2026-11-22 用户口径] PC 横屏此前只有 980px 上限（1600×900 下右侧
     * 空 400+px），且 10 张顶层卡没有页内导航。这里只加**表现层**：
     *   · TOC 条目 = 10 张顶层卡（卡内子标题「模拟量通道与量程/压差过大
     *     保护/电机超温保护」不进 TOC）；
     *   · ≥1100px：左 TOC sticky + 右侧卡片两列（按实测高度切分，保住
     *     卡片顺序）；右侧不足两列宽（≈880px）时回落单列，避免表单挤爆；
     *   · <1100px：.settings-toc 隐藏、.settings-cards/.settings-col 走
     *     display:contents ⇒ 卡片仍是 .settings-page 的直接 flex 子项，
     *     原单列布局与顺序不变（两列切分始终是「前缀/后缀」且按原顺序
     *     回填 DOM ⇒ 窄屏 DOM 顺序 = 原 0..9，不靠 order 兜底）。
     * 滚动容器是 .page-host（不是 window）⇒ 全部滚动/监听都对 host 做。 */
    var tocItems = [
      { label: "运行模式", card: cardMode },
      { label: "自动运行", card: cardAutoRun },
      { label: "自动模式配置", card: cardAutoCfg },
      { label: "连接信息", card: cardConn },
      { label: "采集配置", card: cardCollect },
      { label: "数据存储", card: cardStorage },
      { label: "运行保护", card: cardProtect },
      { label: "相机", card: cardCamera },
      { label: "变频器参数", card: cardVfdParams },
      { label: "登录策略", card: cardLogin },
    ];

    var colLeft = HSF.el("div.settings-col");
    var colRight = HSF.el("div.settings-col");
    var cardsWrap = HSF.el("div.settings-cards", null, [colLeft, colRight]);
    tocItems.forEach(function (it) { colLeft.appendChild(it.card); });

    var tocRoot = HSF.el("nav.settings-toc", { "aria-label": "设置导航" }, [
      HSF.el("div.card-title.settings-toc-title", { text: "导航" }),
    ]);
    var tocButtons = [];
    tocItems.forEach(function (it, i) {
      var btn = HSF.el("button.settings-toc-btn", {
        type: "button",
        text: it.label,
        "aria-label": "跳转到" + it.label,
      });
      btn.addEventListener("click", function () { tocGo(i); });
      tocButtons.push(btn);
      tocRoot.appendChild(btn);
    });

    var page = HSF.el("div.settings-page.settings-page--toc", null, [
      HSF.el("div.settings-head", null, [
        HSF.el("span.page-head-title", { text: "远端设置" }),
        HSF.el("span.page-head-sub.caption",
               { text: "服务端运行模式与连接信息" }),
      ]),
      tocRoot, cardsWrap,
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);

    /* ==================== TOC 滚动 / 高亮 / 两列分配 ====================
     * [offset] 目标卡顶端留 10px（页头滚动出去后，卡片标题贴在 TOC 同一
     * 水平线附近；不是 0 ⇒ 不被 .page-host 的上内边距/滚动条压住标题）。
     * [高亮] 滚动事件 rAF 节流（root 是 .page-host）；取「顶端已越过视口
     * 参考线且 top 最大」的那张卡 = 最近刚开始的那张，单列时就是常见的
     * scrollspy，两列时取更靠下开始的一张；点击后先立即置高亮，并在
     * 平滑滚动窗口内抑制滚动重算（否则会一路闪过高亮）。 */
    var SCROLL_OFFSET = 10;
    var wideMq = window.matchMedia("(min-width: 1100px)");
    var TWO_COL_MIN = 880;        /* 两列各 ≈435px（卡内 ≈410px）；再窄
                                     「采集配置」的 cfg-grid（最小 ≈394px）
                                     会横向溢出 ⇒ 回落单列 */
    var lastWrapW = 0;
    var resizeTimer = 0;
    var spyRaf = 0;
    var clickGuardUntil = 0;

    function cardTopInHost(card) {
      return card.getBoundingClientRect().top - host.getBoundingClientRect().top +
             host.scrollTop;
    }

    function setTocActive(idx) {
      tocButtons.forEach(function (b, i) {
        var on = i === idx;
        b.classList.toggle("active", on);
        if (on) b.setAttribute("aria-current", "true");
        else b.removeAttribute("aria-current");
      });
    }

    function tocGo(idx) {
      var top = cardTopInHost(tocItems[idx].card) - SCROLL_OFFSET;
      if (top < 0) top = 0;
      clickGuardUntil = Date.now() + 700;
      setTocActive(idx);
      if (typeof host.scrollTo === "function") {
        host.scrollTo({ top: top, behavior: "smooth" });
      } else {
        host.scrollTop = top;
      }
    }

    function updateTocFromScroll() {
      if (!wideMq.matches) return;
      var line = host.getBoundingClientRect().top + SCROLL_OFFSET;
      var best = -1;
      var bestTop = -Infinity;
      tocItems.forEach(function (it, i) {
        var t = it.card.getBoundingClientRect().top;
        if (t <= line + 1 && t > bestTop) { bestTop = t; best = i; }
      });
      /* 滚到底时末张卡的顶端到不了参考线 ⇒ 直接高亮最后一条。
         只在真的有滚动余量时生效：内容整页放得下时不算「到底」。 */
      if (host.scrollHeight - host.clientHeight > 8 &&
          host.scrollTop + host.clientHeight >= host.scrollHeight - 4) {
        best = tocItems.length - 1;
      }
      if (best < 0) best = 0;
      setTocActive(best);
    }

    function onHostScroll() {
      if (spyRaf) return;
      spyRaf = requestAnimationFrame(function () {
        spyRaf = 0;
        if (Date.now() < clickGuardUntil) return;
        updateTocFromScroll();
      });
    }

    /* 按实测卡高**贪心**分两列：每张放进当前累计高较矮的一列
     * （HSF.balanceColumns）。比前缀/后缀切一刀更稳：
     *   · 卡片高度差极大（自动模式配置 vs 连接信息），前缀切法总量虽
     *     接近，但矮列会提前结束 ⇒ 滚到下半屏时半屏空白（2026-11-22
     *     真机实拍）；
     *   · 贪心把两列高差压在「最高一张卡」以内，两列基本同时结束。
     * [窄屏必须并回左列] 贪心会改变 DOM 顺序（display:contents 下直接
     * 反映为视觉顺序）⇒ 窄屏所有卡按原顺序回左列，移动端顺序与改造前
     * 完全一致（右列留空、display:contents 展平不影响）。 */
    var cardList = tocItems.map(function (it) { return it.card; });

    function restoreCardOrder() {
      cardList.forEach(function (c) { colLeft.appendChild(c); });
    }

    function layoutCardColumns(force) {
      if (!wideMq.matches) {
        cardsWrap.classList.remove("cols-2");
        cardsWrap.classList.add("cols-1");
        restoreCardOrder();
        lastWrapW = 0;                 /* 回窄屏后下次进来必须重新量 */
        return;
      }
      var w = cardsWrap.clientWidth;
      var two = w >= TWO_COL_MIN;
      /* 宽度变化 <80px 且列数没变 ⇒ 不必重排（拖动窗口时不抖）。 */
      if (!force && two === cardsWrap.classList.contains("cols-2") &&
          Math.abs(w - lastWrapW) < 80) return;
      lastWrapW = w;
      cardsWrap.classList.remove("cols-1");
      if (!two) {
        cardsWrap.classList.add("cols-1");
        restoreCardOrder();
        return;
      }
      cardsWrap.classList.add("cols-2");
      HSF.balanceColumns(cardList, colLeft, colRight);
    }

    /* [异步填充后必须重量 2026-11-22 实拍] 远端设置的卡是配置 GET 回来
     * 才填满的：挂载那一刻卡是空的 ⇒ 首量高度偏小、分栏切点失真；数据
     * 到达后卡长高，用户的观感就是"下半屏一边空、一边全是卡"。
     * ResizeObserver 盯每张卡的尺寸（宽不变只长高），200ms 防抖后按
     * 最终高度重分一次。移动卡本身不改卡尺寸 ⇒ 不会自激。 */
    var cardObserver = null;
    var rebalanceTimer = 0;
    if (typeof ResizeObserver === "function") {
      cardObserver = new ResizeObserver(function () {
        clearTimeout(rebalanceTimer);
        rebalanceTimer = setTimeout(function () {
          layoutCardColumns(true);
          if (Date.now() >= clickGuardUntil) updateTocFromScroll();
        }, 200);
      });
      cardList.forEach(function (c) { cardObserver.observe(c); });
    }

    function onWindowResize() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function () { layoutCardColumns(false); }, 200);
    }

    host.addEventListener("scroll", onHostScroll, { passive: true });
    window.addEventListener("resize", onWindowResize);
    layoutCardColumns(true);
    updateTocFromScroll();        /* 入页即按当前滚动位给首条高亮 */

    /* ==================== 模式块 / 循环节 ==================== */
    function buildModeBlock(modeId, modeName, devs) {
      var names = devs.map(function (d) { return d.name; });
      var devCombo = HSF.multiCheckCombo(names, {
        allText: "全部设备", emptyText: "未选择",
        initial: "none", minChecked: 0,
        tip: "勾选该模式要启动的设备（可多选；点外面收起）",
      });
      var rpm = numInput(RPM.min, RPM.max, 1, RPM.def);
      /* 单位后缀在框内（参考 rpm.setSuffix(" rpm")，pages_other.py:5287）。 */
      var rpmSuffix = HSF.el("span.spin-suffix", { text: " rpm" });
      var rpmWrap = HSF.el("span.spin-unit", null, [rpm, rpmSuffix]);
      var hzHint = HSF.el("span.caption", { text: "" });
      function syncHz() {
        var v = clampInt(rpm, RPM.def, RPM.min, RPM.max);
        /* 折算：参考端 "折算 ≈ %.1f Hz" % (v/额定rpm*额定hz)
         * （pages_other.py:5390-5392）——用 round-half-even（HSF.
         * fmtFixedHalfEven）而不是 toFixed，否则 900rpm 这类「正好一半」
         * 的档位两端会差 0.1Hz（参考 31.2 / web 31.3）。 */
        setText(hzHint, "折算 ≈ " +
          HSF.fmtFixedHalfEven(v / RPM.ratedRpm * RPM.ratedHz, 1) + " Hz");
      }
      rpm.addEventListener("input", syncHz);
      syncHz();
      var reverse = selectEl([["0", "正转"], ["1", "反转"]], null, "方向");
      var rstStop = checkRow("停止时关阀复位", true,
        "停止自动运行时：勾选=把电动球阀关回原位；不勾=阀保持现状");
      var rstEstop = checkRow("急停时关阀复位", false,
        "急停时：勾选=把电动球阀关回原位；不勾=阀保持现状（缺省，急停只停泵与变频器）");
      /* [换相策略 2026-11-14] 切到**本模式**时，阀门切换期间两个泵各自
         怎么办（逐泵可配；参考 pages_other.py 的同款两个下拉）。 */
      var stirSwitch = selectEl(SWITCH_POLICIES,
        "切到本模式时，搅拌泵在阀门切换期间怎么处理", "搅拌泵换相策略");
      var inletSwitch = selectEl(SWITCH_POLICIES,
        "切到本模式时，进水泵在阀门切换期间怎么处理", "进水泵换相策略");
      cfgControls.push(devCombo.root, rpm, reverse, rstStop.cb, rstEstop.cb,
                       stirSwitch, inletSwitch);
      var root = HSF.el("div.auto-block", null, [
        HSF.el("div.sec-title", { text: modeName }),
        HSF.el("div.form-row", null, [
          HSF.el("span.lbl", { text: "启动设备" }), devCombo.root]),
        HSF.el("div.form-row", null, [
          HSF.el("span.lbl", { text: "搅拌电机" }), rpmWrap, hzHint,
          HSF.el("span.lbl", { text: "方向" }), reverse]),
        HSF.el("div.form-row", null, [
          HSF.el("span.lbl", { text: "阀复位" }), rstStop.row, rstEstop.row]),
        HSF.el("div.form-row", null, [
          HSF.el("span.lbl", { text: "换相策略" }),
          HSF.el("span.lbl", { text: "搅拌泵" }), stirSwitch,
          HSF.el("span.lbl", { text: "进水泵" }), inletSwitch]),
      ]);
      return { id: modeId, devCombo: devCombo, rpm: rpm, reverse: reverse,
               rstStop: rstStop.cb, rstEstop: rstEstop.cb,
               stirSwitch: stirSwitch, inletSwitch: inletSwitch, root: root };
    }

    /* ==================== 自动循环（组合模式「自动」） ====================
     *
     * [新契约 2026-11-18 用户定案] 参与循环的**模式与顺序**可配，且**每个
     * 阶段各自**的切换条件（压差 / 定时）可配：
     *   · cycleModes = 有序数组（顺序＝循环顺序；第一个＝主运行阶段；
     *     末位→首位＝闭环）；
     *   · stageCfg   = {modeId: {pressEnabled, pressThreshold,
     *     pressCompare, timerEnabled, timerMinutes}}——压差与定时互不排斥、
     *     **先到先切**；两个都没开 ⇒ 服务端明确拒（fail-closed）。
     * [旧扁平键作废] 界面不再有「旧版兼容」区、载荷也不再发旧键（服务端
     * 会把旧键折进对应阶段 ⇒ 发旧键＝悄悄覆盖别的端配好的条件）。
     *
     * [与桌面端同款的结构] 一行 = 一个模式：① 参与勾选 + 序号 + 模式名 +
     * 上移/下移；② **该阶段的条件**（压差开关/阈值/方向 + 定时开关/分钟）。
     * 未参与的行**不显示条件**（换成一句提示），但**行高不变**（条件区与
     * 提示语占同一格 ⇒ 勾/不勾时下面的行不上下跳）。
     */
    function buildCycleSection() {
      cycle.enabled = checkRow("启用自动循环模式", CYCLE.enabledDef);
      window.__cycLog = window.__cycLog || [];
      window.__cycLog.push({what: "buildCycleSection"});
      cycle.rows = {};
      cycle.order = CYCLE_MODE_POOL.map(function (m) { return m[0]; });
      cycle.renderedOrder = null;
      /* 服务端回显里出现过的键（老服务端没有 ⇒ 不发，免得整包被拒）。 */
      cycle.serverKeys = {};
      cycle.modesDirty = false;
      cycle.stageDirty = false;
      cycle.updating = false;
      cycle.listBox = HSF.el("div.cycle-list");
      CYCLE_MODE_POOL.forEach(function (m) {
        var row = buildCycleRow(m[0], m[1]);
        cycle.rows[m[0]] = row;
        cycle.listBox.appendChild(row.root);
      });
      cfgControls.push(cycle.enabled.cb);
      var sec = HSF.el("div", null, [
        HSF.el("div.sec-title", { text: "自动循环（组合模式「自动」）" }),
        caption("勾选＝该模式参与循环；用「上移 / 下移」调整循环顺序。序号 1" +
                "（第一个）是主运行阶段；每个阶段可分别用压差或定时切换，两者" +
                "都开则先到先切；最后一个的下一个是第一个（闭环）。不勾的仍留" +
                "在可参与池里（不显示条件、不提交）。"),
        cycle.enabled.row,
        cycle.listBox,
      ]);
      window.__cycLog.push({what: "builtRows", rows: Object.keys(cycle.rows).length});
      cycleRenderOrder();
      return sec;
    }

    /** 建一个模式的循环行（参与勾选 + 序号 + 上下移 + 该阶段的条件）。 */
    function buildCycleRow(modeId, modeName) {
      var dflt = CYCLE_STAGE_DEF[modeId] || CYCLE_STAGE_DEF_GEN;
      /* ---- ① 头行：参与 / 序号 / 模式名 / 上移下移 ---- */
      var check = HSF.el("input.check", { type: "checkbox" });
      check.checked = CYCLE_MODES_DEF.indexOf(modeId) >= 0;
      var order = HSF.el("span.cycle-order", { text: "1" });
      var up = HSF.el("button.btn", { type: "button", text: "上移" });
      var down = HSF.el("button.btn", { type: "button", text: "下移" });
      up.title = "在参与的模式里往上挪一位（序号 1 ＝ 主运行阶段）";
      down.title = "在参与的模式里往下挪一位";
      var head = HSF.el("div.form-row", null, [
        HSF.el("label.check-row", null,
               [check, HSF.el("span", { text: "参与" })]),
        order,
        HSF.el("span.cycle-name", { text: modeName }),
        HSF.el("span.grow"),
        up, down,
      ]);
      /* ---- ② 条件行：压差（开关/阈值/方向）+ 定时（开关/分钟） ---- */
      var pressEnable = HSF.el("input.check", { type: "checkbox" });
      pressEnable.checked = !!dflt.pressEnabled;
      /* 压差阈值：定点 3 位小数（参考 QDoubleSpinBox.setDecimals(3)）——
         HTML number 框会把 "0.600" 规范化成 "0.6" ⇒ 用 text 框 + 定点
         格式化，值语义不变（载荷仍是 float）。 */
      var pressTh = fixedInput(0, CYCLE.pressMax, dflt.pressThreshold, 3, true);
      pressTh.title = "压差阈值（MPa）：压差计读数越过这个值时切换";
      var pressCmp = selectEl(PRESS_COMPARE,
        "压差大于阈值才切换，还是小于阈值才切换",
        modeName + " 压差比较方向");
      pressCmp.value = dflt.pressCompare;
      var timerEnable = HSF.el("input.check", { type: "checkbox" });
      timerEnable.checked = !!dflt.timerEnable;
      var timerMin = numInput(CYCLE.minMinutes, CYCLE.maxMinutes, 1,
                              dflt.timerMinutes);
      timerMin.title = "本阶段的运行时长（到点切下一个阶段）";
      var condInner = HSF.el("div.cycle-cond-inner", null, [
        HSF.el("label.check-row", null,
               [pressEnable, HSF.el("span", { text: "压差" })]),
        HSF.el("span.spin-unit", null,
               [pressTh, HSF.el("span.spin-suffix", { text: " MPa" })]),
        pressCmp,
        HSF.el("label.check-row", null,
               [timerEnable, HSF.el("span", { text: "定时" })]),
        HSF.el("span.spin-unit", null,
               [timerMin, HSF.el("span.spin-suffix", { text: " 分钟" })]),
      ]);
      /* [几何不变] 提示语与条件区**占同一格**（CSS grid 叠放）⇒ 参与状态
         变化时行高不变（下面的行不上下跳）。 */
      var condHint = HSF.el("div.caption.cycle-hint",
        { text: "未参与循环（不显示条件、不提交）" });
      var cond = HSF.el("div.cycle-cond", null, [condInner, condHint]);
      var root = HSF.el("div.cycle-row", null, [head, cond]);
      cfgControls.push(check, up, down, pressEnable, pressTh, pressCmp,
                       timerEnable, timerMin);
      check.addEventListener("change", function () { cycleOnToggle(modeId); });
      up.addEventListener("click", function () { cycleMove(modeId, -1); });
      down.addEventListener("click", function () { cycleMove(modeId, 1); });
      [pressEnable, pressTh, pressCmp, timerEnable, timerMin].forEach(
        function (el) {
          el.addEventListener("change", function () { cycleOnCondChanged(); });
          el.addEventListener("input", function () { cycleOnCondChanged(); });
        });
      return { root: root, check: check, order: order, up: up, down: down,
               condInner: condInner, condHint: condHint,
               pressEnable: pressEnable, pressTh: pressTh, pressCmp: pressCmp,
               timerEnable: timerEnable, timerMin: timerMin };
    }

    /** 参与循环的模式 id（按循环顺序 = 当前行序）。 */
    function cycleParticipating() {
      var out = [];
      cycle.order.forEach(function (mid) {
        var row = cycle.rows[mid];
        if (row && row.check.checked) out.push(mid);
      });
      return out;
    }

    /** 勾选/取消参与 ⇒ 重排（参与行在前，池行在后）。 */
    function cycleOnToggle(modeId) {
      if (cycle.updating) return;          /* 回显/程序化改动不算用户动作 */
      var part = cycleParticipating();
      var pool = cycle.order.filter(function (mid) {
        return part.indexOf(mid) < 0;
      });
      cycle.order = part.concat(pool);
      cycle.modesDirty = true;
      /* [参与集合变了 ⇒ 条件清单也变了] 服务端要同时拿到新阶段的切换条件
         （stageCfg）⇒ 两个键一起标 dirty（成对发）。 */
      cycle.stageDirty = true;
      cycleRenderOrder();
    }

    /** 上移/下移：在**参与组内部**调整循环顺序（delta=-1 上移）。 */
    function cycleMove(modeId, delta) {
      if (cycle.updating) return;
      var part = cycleParticipating();
      var i = part.indexOf(modeId);
      if (i < 0) return;
      var j = i + delta;
      if (j < 0 || j >= part.length) return;
      var tmp = part[i];
      part[i] = part[j];
      part[j] = tmp;
      var pool = cycle.order.filter(function (mid) {
        return part.indexOf(mid) < 0;
      });
      cycle.order = part.concat(pool);
      cycle.modesDirty = true;
      cycleRenderOrder();
    }

    /** 改了某个阶段的条件 ⇒ 标脏（提交时带上 stageCfg）。 */
    function cycleOnCondChanged() {
      if (cycle.updating) return;
      cycle.stageDirty = true;
    }

    /** 按当前顺序重排列表行 + 刷每行的可见性与按钮可用性。 */
    function cycleRenderOrder() {
      var part = cycleParticipating();
      var pool = cycle.order.filter(function (mid) {
        return part.indexOf(mid) < 0;
      });
      var order = part.concat(pool);
      var changed = !cycle.renderedOrder ||
        cycle.renderedOrder.join("|") !== order.join("|");
      if (changed) {
        /* appendChild ＝ 移动 ⇒ 直接按新顺序搬一遍即可。 */
        order.forEach(function (mid) {
          cycle.listBox.appendChild(cycle.rows[mid].root);
        });
        cycle.renderedOrder = order.slice();
      }
      cycle.order = order;
      /* [逐行显示] 序号 + 条件区可见性（未参与的行换成一句提示）。 */
      order.forEach(function (mid, idx) {
        var row = cycle.rows[mid];
        var inPart = part.indexOf(mid) >= 0;
        setText(row.order, inPart ? String(idx + 1) : "—");
        row.condInner.style.visibility = inPart ? "" : "hidden";
        row.condHint.style.display = inPart ? "none" : "";
      });
      cycleApplyGate();
    }

    /** 权限闸 + 逐行控件可用性（只读用户不能改顺序/条件）。 */
    function cycleApplyGate() {
      if (!cycle.rows || !cycle.order) return;   /* 循环区还没建（早于建卡） */
      var can = canCfg();
      var part = cycleParticipating();
      cycle.order.forEach(function (mid) {
        var row = cycle.rows[mid];
        var i = part.indexOf(mid);
        var inPart = i >= 0;
        setEnabled(row.check, can);
        setEnabled(row.up, can && inPart && i > 0);
        setEnabled(row.down, can && inPart && i < part.length - 1);
        /* [未参与的行不参与排序 ⇒ 隐藏排序按钮 2026-11-18 用户
           「没勾选的是不是不应该进排序」] 未勾选的模式在循环里**没有位置**
           ⇒ 上下移对它没有意义 ⇒ 直接隐藏（不是留两个灰按钮让人猜）。
           参与行才显示；首行的「上移」/末行的「下移」按边界禁用
           （网页端 .btn:disabled 有明确的置灰样式）。
           [用 visibility 而不是 display] display:none 会把按钮从布局里
           拿走 ⇒ 行高变化 ⇒ 勾/不勾时下面的行上下跳（与桌面端 A8f5b
           同款纪律）⇒ visibility:hidden **保留占位**、只是不画。 */
        row.up.style.visibility = inPart ? "" : "hidden";
        row.down.style.visibility = inPart ? "" : "hidden";
        setEnabled(row.pressEnable, can && inPart);
        setEnabled(row.pressTh, can && inPart && row.pressEnable.checked);
        setEnabled(row.pressCmp, can && inPart && row.pressEnable.checked);
        setEnabled(row.timerEnable, can && inPart);
        setEnabled(row.timerMin, can && inPart && row.timerEnable.checked);
      });
    }

    /** stageCfg：只含**参与**的模式 ⇒ 未参与的不提交条件。 */
    function cycleStageValues() {
      var out = {};
      cycleParticipating().forEach(function (mid) {
        var row = cycle.rows[mid];
        out[mid] = {
          pressEnabled: !!row.pressEnable.checked,
          pressThreshold: clampFloat(row.pressTh, CYCLE.pressDef, 0,
                                      CYCLE.pressMax),
          pressCompare: row.pressCmp.value || CYCLE.compareDef,
          timerEnabled: !!row.timerEnable.checked,
          timerMinutes: clampInt(row.timerMin, 10, CYCLE.minMinutes,
                                  CYCLE.maxMinutes),
        };
      });
      return out;
    }

    /** 自动循环配置（cycle 子节）的载荷。 */
    function autoCycleValues() {
      var values = { enabled: !!cycle.enabled.cb.checked };
      /* [两个键是**一对**] 服务端按「cycleModes 的每个阶段都要有 stageCfg」
         校验 ⇒ 只发其一会得到「阶段 X 缺少参与循环的阶段配置」这类半吊子
         错误 ⇒ 任一被用户动过（列表 **或** 条件）就**成对发**。
         [老服务端闸] 只在「服务端回显过该键」**或**「用户明确动过」时才
         发——旧版服务端对未知键是整包拒（不是丢弃）⇒ 无条件带上会让
         「自动模式配置」整包存不进去。新版服务端每次 GET 都回这两个键。 */
      var touched = cycle.modesDirty || cycle.stageDirty;
      if (touched || cycle.serverKeys.cycleModes) {
        values.cycleModes = cycleParticipating();
        values.stageCfg = cycleStageValues();
      }
      return values;
    }

    /** 按服务端回显的顺序设置参与状态与顺序（回显 ≠ 用户改动）。 */
    function cycleApplyServerOrder(modes) {
      var known = [];
      modes.forEach(function (mid) {
        if (cycle.rows[mid] && known.indexOf(mid) < 0) known.push(mid);
      });
      Object.keys(cycle.rows).forEach(function (mid) {
        cycle.rows[mid].check.checked = known.indexOf(mid) >= 0;
      });
      /* [必须按**服务端给的顺序**排] 按池内原序过滤会把回显顺序吃掉
         （顺序＝没配）。 */
      var pool = cycle.order.filter(function (mid) {
        return known.indexOf(mid) < 0;
      });
      cycle.order = known.concat(pool);
    }

    /** 把 auto 节的 cycle 子节铺到控件（缺键 ⇒ 保持缺省，不误改）。 */
    function populateCycle(c) {
      window.__cycLog = window.__cycLog || [];
      window.__cycLog.push({what: "populateCycle", keys: Object.keys(c || {}),
        hasStage: !!(c && c.stageCfg), rows: Object.keys(cycle.rows || {}).length,
        stageSample: c && c.stageCfg ? JSON.stringify(c.stageCfg).slice(0, 200) : null});
      if (!c || typeof c !== "object") return;
      /* [服务端认识哪些新键] 回显里有哪个键 ⇒ 提交时可以发哪个键。 */
      if ("cycleModes" in c) cycle.serverKeys.cycleModes = true;
      if ("stageCfg" in c) cycle.serverKeys.stageCfg = true;
      /* [回显一律覆盖 2026-11-18 定案] 既有口径是「服务端回显 = 事实 ⇒
         覆盖界面」（桌面客户端回归 A8e 锁着同一口径）⇒ **不要**在这里加
         「用户动过就跳过覆盖」的保护（那会与既有口径冲突，且让界面与
         服务端长期对不上）。
         [已知小缺陷] 配置 GET 比用户操作晚到时，用户刚点的勾选会被这份
         回显覆盖（真浏览器实测撞到过：勾「放水模式」后回显才到 ⇒ 勾被
         抹掉）。真要根治得引入「未应用修改」提示，而不是偷偷改口径。 */
      /* [回显不算用户改动] 铺控件期间屏蔽 dirty 标记：服务端把条件改回去
         是**服务端的事**，不是用户动过列表。 */
      var prev = cycle.updating;
      cycle.updating = true;
      try {
        cycle.enabled.cb.checked = c.enabled === undefined
          ? CYCLE.enabledDef : !!c.enabled;
        /* ---- 参与循环的模式 + 顺序 ---- */
        /* [缺键不误改] 服务端没给 cycleModes ⇒ **不动**列表。 */
        var modes = c.cycleModes;
        if (Array.isArray(modes) && modes.length) {
          cycleApplyServerOrder(modes);
        }
        /* ---- 每阶段各自的切换条件（stageCfg）---- */
        /* [缺键不误改] 服务端没给 stageCfg ⇒ **不动**各行条件。 */
        var cfg = c.stageCfg;
        if (cfg && typeof cfg === "object") {
          Object.keys(cfg).forEach(function (mid) {
            var row = cycle.rows[mid];
            if (!row) return;
            var sub = cfg[mid];
            if (!sub || typeof sub !== "object") return;
            var dflt = CYCLE_STAGE_DEF[mid] || CYCLE_STAGE_DEF_GEN;
            /* [键名拼写 2026-11-19 修] 服务端下的键是 **pressEnabled**
            * （带 d）；原来写成 `sub.pressEnable` ⇒ 恒为 undefined
            * ⇒ 三元表达式走 false 分支 ⇒ **压差开关被强制取消**
            * （用户实报「web端数据没同步、刷新也不对」：阈值/方向/分钟数
            * 是对的，只有开关错了）。 */
            row.pressEnable.checked = sub.pressEnabled === undefined
              ? !!dflt.pressEnabled : !!sub.pressEnabled;
            window.__cycLog.push({what: "setPress", mid: mid,
              want: sub.pressEnabled, got: row.pressEnable.checked,
              tag: row.pressEnable.tagName, type: row.pressEnable.type,
              same: row.pressEnable === document.querySelectorAll(
                ".cycle-row")[Object.keys(cycle.rows).indexOf(mid)].querySelectorAll("input[type=checkbox]")[1]});
            var th = parseFloat(sub.pressThreshold);
            row.pressTh.value = (isNaN(th) ? dflt.pressThreshold : th)
              .toFixed(3);
            row.pressCmp.value = (sub.pressCompare === "lt") ? "lt" : "gt";
            /* [键名拼写 2026-11-19 修] 同上：键是 **timerEnabled**。 */
            row.timerEnable.checked = sub.timerEnabled === undefined
              ? !!dflt.timerEnabled : !!sub.timerEnabled;
            var tm = parseInt(sub.timerMinutes, 10);
            row.timerMin.value = String(isNaN(tm) ? dflt.timerMinutes : tm);
          });
        }
        cycleRenderOrder();
      } finally {
        cycle.updating = prev;
      }
    }

    /* ==================== 动作 ==================== */
    function canCfg() {
      return app.canPerm(PERM.ACCOUNT_ADMIN);
    }

    async function setMode(mode) {
      if (!app.canPerm(PERM.MODE_SWITCH)) {
        HSF.statusMessage("权限不足：切换运行模式需要管理员账户");
        return;
      }
      if (!linkReady()) {
        HSF.statusMessage("未连接服务端");
        return;
      }
      HSF.statusMessage("正在切换运行模式…");
      var doc = await withTimeout(HSF.api.setMode(mode), "切换运行模式");
      if (doc && doc.ok) {
        store.applyEvent("mode", doc);
        HSF.statusMessage("运行模式已切到 " + (mode ? "自动" : "手动"));
      } else {
        HSF.statusMessage(failText("切换失败：", (doc && doc.err) || "未知"));
      }
    }

    async function setAutoRunning(wantOn) {
      if (!app.canPerm(PERM.AUTO_CONTROL)) {
        HSF.statusMessage("权限不足：自动运行控制需要管理员账户");
        return;
      }
      if (!linkReady()) {
        HSF.statusMessage("未连接服务端");
        return;
      }
      HSF.statusMessage("正在" + (wantOn ? "启动" : "停止") + "自动运行…");
      var doc = await withTimeout(HSF.api.autoControl({ on: wantOn }),
                                  wantOn ? "启动自动运行" : "停止自动运行");
      if (doc && doc.ok) {
        store.applyEvent("mode", doc);
        HSF.statusMessage("自动运行已" + (wantOn ? "启动" : "停止"));
      } else {
        HSF.statusMessage(failText("操作失败：", (doc && doc.err) || "未知"));
      }
    }

    function configGate() {
      if (!linkReady()) {
        HSF.statusMessage("未连接服务端");
        return false;
      }
      if (!canCfg()) {
        HSF.statusMessage("权限不足：修改服务端配置需要管理员账户");
        return false;
      }
      return true;
    }

    async function fetchConfig(silent) {
      if (!linkReady()) {
        if (!silent) HSF.statusMessage("未连接服务端");
        return;
      }
      configFetched = true;
      var doc = await withTimeout(HSF.api.configGet(), "读取配置");
      if (doc && doc.ok) {
        populate(doc);
        if (!silent) HSF.statusMessage("配置已刷新");
      } else if (!silent) {
        HSF.statusMessage(failText("读取配置失败：", (doc && doc.err) || "未知"));
      }
    }

    function autoConfigValues() {
      var modes = {};
      modeBlocks.forEach(function (blk) {
        modes[blk.id] = {
          devices: blk.devCombo.checkedIndexes().map(function (i) {
            return devicesList[i] ? devicesList[i].id : null;
          }).filter(function (id) { return id; }),
          stirRpm: clampInt(blk.rpm, RPM.def, RPM.min, RPM.max),
          stirReverse: blk.reverse.value === "1",
          resetValvesOnStop: blk.rstStop.checked,
          resetValvesOnEstop: blk.rstEstop.checked,
          /* [换相策略 2026-11-14] 切到本模式时两个泵在阀门切换期间的处理 */
          stirSwitchPolicy: blk.stirSwitch.value || SWITCH_DEF,
          inletSwitchPolicy: blk.inletSwitch.value || SWITCH_DEF,
        };
      });
      /* [只发服务端认识的模式 2026-11-18] 见 serverKnownModes 的说明。 */
      var allowed = (serverKnownModes === null)
        ? ["produce", "backwash", "rinse"]
        : serverKnownModes;
      skippedModes = Object.keys(modes).filter(function (id) {
        return allowed.indexOf(id) < 0;
      });
      var sendModes = {};
      Object.keys(modes).forEach(function (id) {
        if (allowed.indexOf(id) >= 0) sendModes[id] = modes[id];
      });
      return {
        modes: sendModes,
        cycle: autoCycleValues(),
      };
    }

    async function applyAuto() {
      if (!configGate()) return;
      var payload = Object.assign({ section: "auto" }, autoConfigValues());
      HSF.statusMessage("正在应用自动配置（模式 + 循环）…");
      var doc = await withTimeout(HSF.api.configSet(payload), "应用自动配置");
      if (doc && doc.ok) {
        /* [应用成功 ⇒ 清 dirty 2026-11-18] 已提交的内容 == 服务端确认的
           内容 ⇒ 之后的回显可以照铺（不再需要"用户动过就跳过覆盖"的
           保护）；不清的话回显会被当成旧状态跳过 ⇒ 界面与服务端对不上。 */
        cycle.modesDirty = false;
        cycle.stageDirty = false;
        populate(doc);
        var skipNote = "";
        if (skippedModes && skippedModes.length) {
          var names = skippedModes.map(function (id) {
            for (var i = 0; i < modesList.length; i++) {
              if (modesList[i].id === id) return modesList[i].name;
            }
            return id;
          });
          skipNote = "（服务端不支持" + names.join("、") + "，已跳过）";
        }
        HSF.statusMessage("自动配置已生效（模式 + 自动循环）" + skipNote);
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    async function applyCollect() {
      if (!configGate()) return;
      var values = {};
      collectRows.forEach(function (row) {
        values[row[0]] = row[1].checked;
        values[row[2]] = clampInt(row[3], POLL.min, POLL.min, POLL.max);
      });
      /* 逐台开关之外仍带共同键（兼容旧服务端；参考 _apply_collect 同款）。 */
      values.turbidityEnabled = !!(values.turbidity1Enabled ||
                                   values.turbidity2Enabled);
      values.turbidityPollMs = clampInt(collectRows[2][3], POLL.min,
                                        POLL.min, POLL.max);   /* = 进水浊度仪 的 */
      /* [模拟量通道与量程 2026-11-20] 同一次下发（collect 节）。
       * [下限在前] 服务端交叉校验按「另一侧当前值」判 ⇒ 先发下限再发上限。 */
      values.pressChannel = clampInt(anPressCh, 0, 0, 31);
      values.pressLowMpa = parseFloat(anPressLow.value) || 0;
      values.pressHighMpa = parseFloat(anPressHigh.value) || 1;
      values.tempChannel = clampInt(anTempCh, 0, 0, 31);
      values.tempLowC = clampInt(anTempLow, 0, -100, 100);
      values.tempHighC = clampInt(anTempHigh, 100, -50, 500);
      values.pressBelow4Valid = !!anPressBelow4Row.cb.checked;
      values.tempBelow4Valid = !!anTempBelow4Row.cb.checked;
      var payload = Object.assign({ section: "collect" }, values);
      HSF.statusMessage("正在应用采集配置…");
      var doc = await withTimeout(HSF.api.configSet(payload), "应用采集配置");
      if (doc && doc.ok) {
        populate(doc);
        HSF.statusMessage("采集配置已生效（开关即时，周期下一拍）");
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    async function applyStorage() {
      if (!configGate()) return;
      var payload = {
        section: "storage",
        enabled: stEnable.cb.checked,
        path: String(stPath.value || "").trim(),
        sampleMs: clampInt(stSample, STORAGE.sampleDef,
                           STORAGE.sampleMin, STORAGE.sampleMax),
        maxMb: clampInt(stMax, STORAGE.maxDef, STORAGE.maxMin, STORAGE.maxMax),
        fullPolicy: stPolicy.value || "ring",
        keepFiles: clampInt(stKeep, STORAGE.keepDef, 1, 99),
        historyTrigger: stTrigger.value || "always",
      };
      HSF.statusMessage("正在应用数据存储配置…");
      var doc = await withTimeout(HSF.api.configSet(payload), "应用数据存储配置");
      if (doc && doc.ok) {
        populate(doc);
        HSF.statusMessage("数据存储配置已生效（含容量上限与满额策略）");
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    async function applyProtect() {
      if (!configGate()) return;
      /* 界面用秒、协议用毫秒（与服务端/桌面客户端同口径）。 */
      var payload = {
        section: "protect",
        inletNoFlowTimeoutMs: clampInt(protectTimeout, 0, 0, 3600) * 1000,
        inletNoFlowStop: !!protectStop.cb.checked,
        /* [压差过大保护 2026-11-15] 界面 kPa、协议 MPa（÷1000）。 */
        pressHighEnabled: !!pressHighEnable.cb.checked,
        pressHighLimitMpa: clampInt(pressHighLimit, 0, 0, 10000) / 1000.0,
        pressHighStop: !!pressHighStop.cb.checked,
        /* [电机超温保护 2026-11-20] 阈值单位 ℃（与服务端/桌面客户端同）。 */
        motorOverTempEnabled: !!motorTempEnable.cb.checked,
        motorOverTempLimitC: clampInt(motorTempLimit, 75, 0, 200),
        motorOverTempStop: !!motorTempStop.cb.checked,
      };
      HSF.statusMessage("正在应用运行保护配置…");
      var doc = await withTimeout(HSF.api.configSet(payload),
                                  "应用运行保护配置");
      if (doc && doc.ok) {
        populate(doc);
        HSF.statusMessage("运行保护配置已生效");
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    async function applyCamera() {
      if (!configGate()) return;
      /* [口令留空 = 不改] 服务端不回显口令 ⇒ 空串解释为"不改"（两端同款，
         存一次不会把相机锁在门外）。 */
      var payload = {
        section: "camera",
        enabled: !!camEnabled.cb.checked,
        /* [防呆 2026-11-20 实锅] 框里内容形态与连接方式不符时用缓存值：
         * 含 '.' 的必是 IP，含 5 个 ':'/' - ' 分组的必是 MAC。这样即使
         * UI 状态错位也不会把 IP 当 MAC 发出去（服务端会拒）。 */
        ip: camResolve.value === "mac"
          ? camAddrIp : String(camAddr.value || "").trim(),
        port: clampInt(camPort, 554, 1, 65535),
        username: String(camUser.value || "").trim(),
        password: String(camPass.value || ""),
        stream: camStream.value,
        fps: clampInt(camFps, 2, 1, 30),
        width: clampInt(camWidth, 704, 128, 4096),
        quality: clampInt(camQuality, 80, 1, 100),
        /* [相机三键 2026-11-21] 传输/编码/本端解码——与桌面端同键同值。 */
        transport: camTransport.value,
        codec: camCodec.value,
        hwDecode: camHw.value,
        resolveMode: camResolve.value || "ip",
        mac: (function () {
          var txt = String(camAddr.value || "").trim();
          if (camResolve.value !== "mac") return camAddrMac;
          if (txt.indexOf(".") >= 0) return camAddrMac;   /* 框里是 IP */
          return txt;
        })(),
        reconnectEnabled: !!camReconnect.cb.checked,
        reconnectIntervalS: clampInt(camReconnectS, 5, 1, 3600),
      };
      HSF.statusMessage("正在应用相机配置…");
      var doc = await withTimeout(HSF.api.configSet(payload),
                                  "应用相机配置");
      if (doc && doc.ok) {
        populate(doc);
        HSF.statusMessage("相机配置已生效（服务端已重启采集）");
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    async function applyLogin() {
      if (!configGate()) return;
      var payload = {
        section: "login",
        loginAllowAnonymous: loginAnon.cb.checked,
        loginAllowConcurrent: loginConcur.cb.checked,
        loginMaxSessions: clampInt(loginMax, 8, 0, 256),
      };
      HSF.statusMessage("正在应用登录策略…");
      var doc = await withTimeout(HSF.api.configSet(payload), "应用登录策略");
      if (doc && doc.ok) {
        populate(doc);
        HSF.statusMessage("登录策略已生效（下次登录起）");
      } else {
        HSF.statusMessage(failText("应用失败：", (doc && doc.err) || "未知"));
      }
    }

    btnManual.addEventListener("click", function () { setMode(0); });
    btnAuto.addEventListener("click", function () { setMode(1); });
    btnAutoStart.addEventListener("click", function () { setAutoRunning(true); });
    btnAutoStop.addEventListener("click", function () { setAutoRunning(false); });
    btnAutoApply.addEventListener("click", applyAuto);
    btnCollectRefresh.addEventListener("click", function () { fetchConfig(false); });
    btnCollectApply.addEventListener("click", applyCollect);
    btnStorageSave.addEventListener("click", applyStorage);
    btnProtectApply.addEventListener("click", applyProtect);
    btnLoginSave.addEventListener("click", applyLogin);
    btnVfdRefresh.addEventListener("click", function () { fetchVfdParams(false); });
    btnVfdApply.addEventListener("click", applyVfdParams);

    /* ==================== 回显（局部应答只刷新自己那一节） ==================== */
    function populate(doc) {
      doc = doc || {};
      var collect = doc.collect;
      if (hasKeys(collect)) {
        /* 逐台键缺省回落共同键 turbidityEnabled（旧服务端兼容，参考同款）。 */
        var tAll = collect.turbidityEnabled === undefined
          ? true : !!collect.turbidityEnabled;
        collectRows.forEach(function (row) {
          var def = (row[0] === "turbidity1Enabled" ||
                     row[0] === "turbidity2Enabled") ? tAll : true;
          row[1].checked = collect[row[0]] === undefined
            ? def : !!collect[row[0]];
          var poll = parseInt(collect[row[2]], 10);
          row[3].value = String(isNaN(poll) || poll <= 0 ? 200 : poll);
        });
        /* [模拟量通道与量程 2026-11-20] 缺键（老服务端）⇒ 保持缺省。 */
        /* [通道下拉重建 2026-11-20] 条目数随 GET 的 analogChannels 刷新
         * （缺键回落 8 路），再回显选中值。 */
        if (collect.analogChannels !== undefined) {
          anChannels = Math.max(1, parseInt(collect.analogChannels, 10) || 8);
          fillChannelSelect(anPressCh,
            collect.pressChannel === undefined
              ? undefined : parseInt(collect.pressChannel, 10) || 0);
          fillChannelSelect(anTempCh,
            collect.tempChannel === undefined
              ? undefined : parseInt(collect.tempChannel, 10) || 1);
        }
        if (collect.pressChannel !== undefined)
          anPressCh.value = String(parseInt(collect.pressChannel, 10) || 0);
        if (collect.pressLowMpa !== undefined)
          anPressLow.value = String(parseFloat(collect.pressLowMpa) || 0);
        if (collect.pressHighMpa !== undefined)
          anPressHigh.value = String(parseFloat(collect.pressHighMpa) || 1);
        if (collect.tempChannel !== undefined)
          anTempCh.value = String(parseInt(collect.tempChannel, 10) || 1);
        if (collect.tempLowC !== undefined)
          anTempLow.value = String(parseFloat(collect.tempLowC) || 0);
        if (collect.tempHighC !== undefined)
          anTempHigh.value = String(parseFloat(collect.tempHighC) || 100);
        if (collect.pressBelow4Valid !== undefined)
          anPressBelow4Row.cb.checked = !!collect.pressBelow4Valid;
        if (collect.tempBelow4Valid !== undefined)
          anTempBelow4Row.cb.checked = !!collect.tempBelow4Valid;
      }
      var storage = doc.storage;
      if (hasKeys(storage)) {
        stEnable.cb.checked = storage.enabled === undefined
          ? true : !!storage.enabled;
        stPath.value = String(storage.path || "");
        var sample = parseInt(storage.sampleMs, 10);
        stSample.value = String(isNaN(sample) || sample <= 0
                                ? STORAGE.sampleDef : sample);
        var maxMb = parseInt(storage.maxMb, 10);
        stMax.value = String(isNaN(maxMb) ? 0 : maxMb);
        stPolicy.value = String(storage.fullPolicy || "ring");
        var keep = parseInt(storage.keepFiles, 10);
        stKeep.value = String(isNaN(keep) || keep <= 0 ? STORAGE.keepDef : keep);
        var trig = String(storage.historyTrigger || "always");
        stTrigger.value = (trig === "auto") ? "auto" : "always";
        syncPolicyUi();
        syncMaxHint();
      }
      /* 谁给的字段谁负责：只在应答确实带存储侧键时刷新这两行。 */
      if ("storageDbPath" in doc || "storageStats" in doc) {
        var dbPath = String(doc.storageDbPath || "");
        setText(dbPathLabel,
          "当前库文件：" + (dbPath || "未启用（落盘关闭）"));
        populateUsage(doc.storageStats || {});
      }
      var login = doc.login;
      if (hasKeys(login)) {
        loginAnon.cb.checked = !!login.loginAllowAnonymous;
        loginConcur.cb.checked = !!login.loginAllowConcurrent;
        var maxS = parseInt(login.loginMaxSessions, 10);
        loginMax.value = String(isNaN(maxS) ? 8 : maxS);
      }
      var auto = doc.auto;
      if (hasKeys(auto)) populateAuto(auto);
      /* [运行保护 2026-11-15] 只在应答确实带 protect 节时铺（局部应答不带
       * ⇒ 不得把保护卡冲回缺省，那等于悄悄改了保护动作）。 */
      if (hasKeys(doc.protect)) {
        protectTimeout.value = clampInt(doc.protect.inletNoFlowTimeoutMs,
                                        0, 0, 3600000) / 1000;
        protectStop.cb.checked = !!doc.protect.inletNoFlowStop;
        /* [压差过大保护 2026-11-15] 缺键（老服务端）⇒ 保持缺省不启用。 */
        pressHighEnable.cb.checked = !!doc.protect.pressHighEnabled;
        /* [协议是 MPa、界面是 kPa] ×1000 换算；clampInt 吃的是**输入控件**
         * （这里传的是数值）⇒ 用 parseFloat 自己夹。 */
        pressHighLimit.value = Math.round(
          Math.max(0, Math.min(10,
            parseFloat(doc.protect.pressHighLimitMpa) || 0)) * 1000);
        pressHighStop.cb.checked = !!doc.protect.pressHighStop;
        /* [电机超温保护 2026-11-20] 缺键（老服务端）⇒ 保持缺省不启用。 */
        motorTempEnable.cb.checked = !!doc.protect.motorOverTempEnabled;
        motorTempLimit.value = String(Math.round(
          Math.max(0, Math.min(200,
            parseFloat(doc.protect.motorOverTempLimitC) || 0))));
        motorTempStop.cb.checked = doc.protect.motorOverTempStop === undefined
          ? true : !!doc.protect.motorOverTempStop;
      }
      /* [camera 节 2026-11-19] 局部应答不带 camera ⇒ 不动控件（与 protect
         同一纪律：「应答里没有的字段不要当成已关闭」）。 */
      if (hasKeys(doc.camera)) {
        camEnabled.cb.checked = !!doc.camera.enabled;
        /* [顺序 2026-11-20 实锅] **必须先定连接方式再装地址框**：原来
         * syncCamAddr() 写在 resolveMode 赋值之前 ⇒ 装框时下拉还是旧值
         * （按 IP）⇒ 选「按 MAC」模式下框里仍是 IP，点应用就报
         * 「相机 MAC 格式不对：'192.168.1.181'」（板端实测）。 */
        if (doc.camera.resolveMode)
          camResolve.value = String(doc.camera.resolveMode);
        if (doc.camera.ip !== undefined)
          camAddrIp = String(doc.camera.ip || "");
        if (doc.camera.mac !== undefined)
          camAddrMac = String(doc.camera.mac || "");
        syncCamAddr();
        camPort.value = parseInt(doc.camera.port, 10) || 554;
        if (doc.camera.username) camUser.value = String(doc.camera.username);
        /* [口令不回显] 服务端只回 hasPassword ⇒ 口令框保持空（留空=不改）。 */
        if (doc.camera.stream) camStream.value = String(doc.camera.stream);
        if (doc.camera.fps) camFps.value = String(doc.camera.fps);
        camWidth.value = parseInt(doc.camera.width, 10) || 704;
        camQuality.value = parseInt(doc.camera.quality, 10) || 80;
        /* [相机三键回显 2026-11-21] 缺键（老服务端）⇒ 保持缺省
         * （stream/h264/auto，与 selectEl 首项一致），不误改已选值。 */
        if (doc.camera.transport) camTransport.value = String(doc.camera.transport);
        if (doc.camera.codec) camCodec.value = String(doc.camera.codec);
        if (doc.camera.hwDecode) camHw.value = String(doc.camera.hwDecode);
        /* [MAC 连接 + 断线重连回显] 连接方式与地址框已在上面那组做过
         * （顺序有讲究）；这里只补开关与间隔。 */
        camReconnect.cb.checked = doc.camera.reconnectEnabled === undefined
          ? true : !!doc.camera.reconnectEnabled;
        camReconnectS.value = String(parseInt(doc.camera.reconnectIntervalS,
                                              10) || 5);
        /* [在线状态回显] 让操作员一眼看出「配了但相机没出画面」。
         * [ES 模式口径 2026-11-19] stream 模式的 frames 是 **NAL 计数**，
         * 文案说「帧」就是撒谎 ⇒ 按 actualTransport 区分单位（与桌面
         * 客户端相机卡同款）。 */
        var resolvedTxt = "";
        if (camResolve.value === "mac") {
          resolvedTxt = "· MAC " + (camAddrMac || "未填")
            + " → " + (String(doc.camera.resolvedIp || "") || "未解析");
        }
        setText(camState, doc.camera.online
          ? ("相机：画面接收中（已收 " + (doc.camera.frames || 0)
             + (doc.camera.actualTransport === "stream" ? " NAL" : " 帧")
             + (doc.camera.actualCodec
                ? ("· " + String(doc.camera.actualCodec).toUpperCase()
                   .replace("H2", "H.2")) : "")
             + "）" + resolvedTxt)
          : ("相机：未出画面" +
             (doc.camera.error ? ("（" + doc.camera.error + "）") : "")
             + resolvedTxt));
      }
    }

    function populateAuto(auto) {
      var modes = auto.modes || {};
      /* [服务端认识的模式集合 2026-11-18] 回显的键 = 它认识的模式。 */
      if (modes && typeof modes === "object") {
        serverKnownModes = Object.keys(modes);
      }
      var idToIdx = {};
      devicesList.forEach(function (d, i) { idToIdx[d.id] = i; });
      modeBlocks.forEach(function (blk) {
        var m = modes[blk.id] || {};
        /* 未知设备 id 静默丢弃（服务端将来增删清单不撑爆多选）。 */
        blk.devCombo.setCheckedIndexes((m.devices || [])
          .map(function (d) { return idToIdx[d]; })
          .filter(function (i) { return i !== undefined; }));
        var rpm = parseInt(m.stirRpm, 10);
        blk.rpm.value = String(isNaN(rpm) ? RPM.def
          : Math.max(RPM.min, Math.min(RPM.max, rpm)));
        blk.reverse.value = m.stirReverse ? "1" : "0";
        blk.rstStop.checked = m.resetValvesOnStop === undefined
          ? true : !!m.resetValvesOnStop;
        blk.rstEstop.checked = m.resetValvesOnEstop === undefined
          ? false : !!m.resetValvesOnEstop;
        /* [换相策略回显 2026-11-14] 缺键（老服务端）⇒ 保持缺省不误改。 */
        blk.stirSwitch.value = m.stirSwitchPolicy || SWITCH_DEF;
        blk.inletSwitch.value = m.inletSwitchPolicy || SWITCH_DEF;
        blk.rpm.dispatchEvent(new Event("input"));
      });
      /* cycle 子节只在应答带它时铺（老服务端没有 ⇒ 保持缺省，不误改）。
         [新契约 2026-11-18] 只吃 cycleModes / stageCfg 两个新键；旧的扁平
         键不再据以铺界面（用户口径：不必兼容旧版）。 */
      var c = auto.cycle;
      if (hasKeys(c)) populateCycle(c);
    }

    function populateUsage(stats) {
      if (!stats) {
        usageFill.style.width = "0";
        usageLabel.classList.remove("paused");
        setText(usageLabel, "用量：--");
        return;
      }
      var usedMb = Number(stats.usedMb) || 0;
      var maxMb = parseInt(stats.maxMb, 10) || 0;
      var rows = parseInt(stats.rows, 10) || 0;
      var segs = parseInt(stats.segments, 10) || 0;
      var dropped = parseInt(stats.droppedRows, 10) || 0;
      var parts = ["已用 " + usedMb.toFixed(2) + " MB"];
      var pct = 0;
      if (maxMb > 0) {
        parts.push("上限 " + maxMb + " MB");
        pct = Math.max(0, Math.min(100, usedMb / maxMb * 100));
        parts.push(Math.round(pct) + "%");
      } else {
        parts.push("上限不限");
      }
      parts.push("行数 " + rows);
      var policy = String(stats.policy || "");
      STORAGE_POLICIES.forEach(function (p) {
        if (p[0] === policy) parts.push("策略 " + p[1].split("（")[0]);
      });
      if (segs) parts.push("分段 " + segs);
      if (dropped) parts.push("已循环丢弃 " + dropped + " 行");
      usageFill.style.width = (maxMb > 0 ? pct : 0) + "%";
      var text = "用量：" + parts.join(" · ");
      if (stats.paused) {
        text += "\n[已停写] " + (stats.pauseReason || "");
        usageLabel.classList.add("paused");
      } else {
        usageLabel.classList.remove("paused");
      }
      setText(usageLabel, text);
    }

    /* ==================== 变频器参数（0x011B） ==================== */
    /** 按应答重建参数表（应答带哪几条铺哪几条；value 一律取服务端回读值）。
     * 可写项（writable!==false 且换算 scale 已知）给「写入值」输入框，
     * 预填回读工程值；只读项第三列标「只读」。 */
    function populateVfdParams(params) {
      HSF.replaceChildren(vfdGrid);
      vfdInputs.length = 0;
      vfdGrid.appendChild(HSF.el("span.cfg-head", { text: "参数" }));
      vfdGrid.appendChild(HSF.el("span.cfg-head", { text: "当前值（回读）" }));
      vfdGrid.appendChild(HSF.el("span.cfg-head", { text: "写入值" }));
      (Array.isArray(params) ? params : []).forEach(function (p) {
        if (!p || typeof p !== "object") return;
        var addr = Number(p.addr);
        var scale = paramScale(addr, p.raw, p.value);
        var digits = paramDigits(scale || 1);
        var unit = String(p.unit || "");
        /* [枚举型参数 2026-11-18 用户要求] 服务端带 options ⇒ 该行用
         * **下拉框**（显示中文类型名，value = 原始码），不再填裸数字。
         * 选项**以服务端为准**（服务端是单一事实源）；本地表只兜底。 */
        var opts = (Array.isArray(p.options) && p.options.length)
          ? p.options
          : (VFD_PARAM_OPTIONS[addr] || []);
        var isEnum = opts.length > 0;
        vfdGrid.appendChild(HSF.el("span", {
          text: String(p.code || "") + " " + String(p.label || "") }));
        if (isEnum) {
          /* 回读列显示**中文名**（服务端 valueText = 按回读值取的名）；
           * 服务端没给名字 ⇒ 回落原始码（**不编**）。 */
          var rawNow = Number(p.raw);
          var txt = String(p.valueText || "");
          if (!txt && isFinite(rawNow)) {
            opts.forEach(function (o) {
              if (o && Number(o.value) === rawNow) txt = String(o.label || "");
            });
          }
          vfdGrid.appendChild(HSF.el("span", {
            text: txt || (isFinite(rawNow) ? String(rawNow) : "--") }));
        } else {
          vfdGrid.appendChild(HSF.el("span", {
            text: fmtParamValue(p.value, digits) + (unit ? " " + unit : "") }));
        }
        if (p.writable === false || !isFinite(addr) || addr < 0) {
          vfdGrid.appendChild(caption("只读"));
          return;
        }
        if (isEnum) {
          /* [不进微调框壳 2026-11-18 用户指出] 数值框那列套着 .spin-unit
           *（壳画边框 + ▲▼ 微调箭头）⇒ 下拉若也套进去就变成"下拉嵌在微调
           * 框里"（两层框 + 一对多余的上下箭头）。⇒ 枚举行直接放
           * `<select class="input">`，与页面其它下拉（如运行模式）同款；
           * 也不加 .num-wide（那个类写死 10rem ⇒ 裁字）。 */
          var sel = HSF.el("select.input", null);
          opts.forEach(function (o) {
            if (!o) return;
            var ov = Number(o.value);
            sel.appendChild(HSF.el("option", {
              value: String(ov), text: String(o.label || ov) }));
          });
          /* 选中项 = **回读值**（不是第一项）⇒ 用户看到的起点就是
           * 驱动器里的真实值。 */
          var rawSel = Number(p.raw);
          if (isFinite(rawSel)) {
            for (var oi = 0; oi < sel.options.length; oi++) {
              if (Number(sel.options[oi].value) === rawSel) {
                sel.selectedIndex = oi;
                break;
              }
            }
          }
          sel.dataset.addr = String(addr);
          sel.dataset.code = String(p.code || ("0x" + addr.toString(16)));
          sel.dataset.scale = "1";     /* 下拉的 value 就是 raw */
          sel.title = "直接选择类型（提交时按原始码下发）" +
                      (p.runLocked ? "；变频器运行中写会被拒绝" : "");
          /* 直接进格（**不进 .spin-unit 壳**）⇒ 与其它下拉同款外观。 */
          vfdGrid.appendChild(sel);
          /* [宽度按字体实测] 必须**挂进 DOM 之后**量（游离元素量不准）。 */
          fitSelectWidth(sel);
          vfdInputs.push(sel);
          return;
        }
        if (!(scale > 0)) {
          /* 表外地址且从应答推不出换算 ⇒ 不给输入框（换算不了就不瞎写）。 */
          vfdGrid.appendChild(caption("换算未知（暂不可改）"));
          return;
        }
        /* [量程按桌面端同款 vmin/vmax 夹取] 表外地址回落宽范围（不瞎限）。 */
        var range = VFD_PARAM_RANGES[addr] || [0, 4294967295];
        var inp = fixedInput(range[0], range[1], Number(p.value) || 0,
                             digits, true);
        inp.dataset.addr = String(addr);
        inp.dataset.code = String(p.code || ("0x" + addr.toString(16)));
        inp.dataset.scale = String(scale);
        inp.title = "按工程值填（量程 " + range[0] + "~" + range[1] +
                    (unit ? " " + unit : "") + "）；提交时换算成原始码（×" +
                    scale + "）" +
                    (p.runLocked ? "；变频器运行中写会被拒绝" : "");
        vfdGrid.appendChild(HSF.el("span.spin-unit", null, [inp]));
        vfdInputs.push(inp);
      });
    }

    /** 0x011B 应答落显示：设备状态行 + 参数表（回读值）。 */
    function applyVfdDoc(doc) {
      doc = doc || {};
      setText(vfdStateLabel, "变频器：" + vfdRunStateText(doc));
      populateVfdParams(doc.params);
    }

    /** 读真实值（GET /api/vfd/params）。入页自动静默读一次；手动按钮响亮读。 */
    async function fetchVfdParams(silent) {
      if (!linkReady()) {
        if (!silent) HSF.statusMessage("未连接服务端");
        return;
      }
      vfdFetched = true;
      if (!silent) HSF.statusMessage("正在读取变频器参数…");
      var doc = await withTimeout(HSF.api.vfdParamsGet(), "读取变频器参数");
      if (doc && doc.ok) {
        applyVfdDoc(doc);
        var n = Array.isArray(doc.params) ? doc.params.length : 0;
        setText(vfdStatus, "已读取 " + n + " 项（当前值 = 服务端回读）");
        if (!silent) HSF.statusMessage("变频器参数已回读");
      } else {
        /* 显式失败包络（旧 C 服务端没有 0x011B / 链路未就绪）：原因留在
         * 卡内状态行，不洗白成"已读 0 项"。 */
        var msg = failText("读取失败：", (doc && doc.err) || "未知");
        setText(vfdStatus, msg);
        if (!silent) HSF.statusMessage(msg);
      }
    }

    /** 变频器写入**成功**时的文案（确认数 + 失败清单）。
     *
     * [口径与桌面客户端逐字一致 2026-11-18] 确认数 = **总条数 −
     * applied===false 的条数**——5 项写进去 + 1 项被驱动器拒 ⇒「已写入并
     * 回读确认：5 项；失败：P1.02」。**不是**「只要有一项失败就整体归
     * 零」：那会把明明生效的 5 项说成「确认 0 项」⇒ 误导现场（现场会以为
     * 一次都没写进去）。
     * [老服务端] 应答里**没有** applied 键 ⇒ 按提交项数计（旧口径不变）。
     * [失败清单] applied===false 的项逐个列出（失败项确实没写进去）。 */
    function vfdWriteOkMessage(params, sent) {
      var list = Array.isArray(params) ? params : [];
      var failed = [];
      var hasApplied = false;
      list.forEach(function (p) {
        if (!p) return;
        if (p.applied === false) {
          failed.push(String(p.code || ("0x" + Number(p.addr).toString(16))));
        }
        if (p.applied === true || p.applied === false) hasApplied = true;
      });
      var n = hasApplied ? (list.length - failed.length)
                          : Object.keys(sent || {}).length;
      var msg = "已写入并回读确认：" + n + " 项";
      if (failed.length) msg += "；失败：" + failed.join("、");
      return msg;
    }

    /** 应用到变频器（POST {op:1, values}）：工程值 → 原始码（×scale）；
     * 成功用应答回读值刷新显示；失败 err 原样显示（RunLocked 三端同款
     * 文案）+ 逐项清单（应答标 applied=false 的项）。 */
    var vfdInflight = false;
    async function applyVfdParams() {
      if (!configGate()) return;
      /* [在途闸 2026-11-18 联调深检] 双击「应用」= 两批写并发下发，两批在
       * 总线事务级交错时各自的 applied 判定可能互相污染。⇒ 在途禁入。
       * [修 2026-11-18 联调验收实测] 闸必须**成对开合**：原实现只置位不复位
       * ⇒ 第一次「应用」成功后按钮**永久失灵**（后续点击全被 early return
       * 吞掉，状态行还停在上一次的文案）。⇒ 函数体整体进 try/finally，
       * finally 复位（JS 的 return 会先跑 finally ⇒ 所有早退路径都覆盖）。 */
      if (vfdInflight) return;
      vfdInflight = true;
      var failed = [];
      var msg = "";
      try {
      var values = {};
      var bad = [];
      vfdInputs.forEach(function (inp) {
        var addr = parseInt(inp.dataset.addr, 10);
        var scale = parseFloat(inp.dataset.scale);
        var v = parseFloat(inp.value);
        if (isNaN(addr) || !(scale > 0) || isNaN(v) || v < 0) {
          bad.push(inp.dataset.code || String(addr));
          return;
        }
        values[addr] = Math.round(v * scale);
      });
      if (bad.length) {
        /* 整包不发（fail-closed）：半个包写下去，回读会把"没改的项"也刷掉。 */
        var badMsg = "写入值无效（须为非负数）：" + bad.join("、");
        setText(vfdStatus, badMsg);
        HSF.statusMessage(badMsg);
        return;
      }
      if (!Object.keys(values).length) {
        HSF.statusMessage("没有可写的参数（服务端未回可写项）");
        return;
      }
      HSF.statusMessage("正在应用变频器参数…");
      var doc = await withTimeout(
        HSF.api.vfdParamsWrite({ op: 1, values: values }), "应用变频器参数");
      if (doc && doc.ok) {
        applyVfdDoc(doc);          /* 用应答回读值刷新显示（绝不显示提交值） */
        /* [文案口径与桌面客户端同款 2026-11-18] 确认数 = 总条数 −
         * applied===false 的条数；失败项逐个列出（deny-write 驱动器
         * FC06 ACK 但寄存器不落时服务端回 ok=true + 逐项 applied=false）。 */
        msg = vfdWriteOkMessage(doc.params, values);
        setText(vfdStatus, msg);
        HSF.statusMessage(msg);
      } else {
        failed = [];
        if (doc && Array.isArray(doc.params)) {
          doc.params.forEach(function (p) {
            if (p && p.applied === false) {
              failed.push(String(p.code ||
                                 ("0x" + Number(p.addr).toString(16))));
            }
          });
        }
        msg = failText("写入失败：", (doc && doc.err) || "未知");
        if (failed.length) msg += "（未写入：" + failed.join("、") + "）";
        setText(vfdStatus, msg);
        HSF.statusMessage(msg);
        /* 拒写应答若带了 params（当前值/RunLocked 态）⇒ 拿它刷新一次回读。 */
        if (doc && Array.isArray(doc.params) && doc.params.length) {
          applyVfdDoc(doc);
        }
      }
      } finally {
        vfdInflight = false;      /* 闸必须成对开合（否则按钮只活一次） */
      }
    }

    /* ==================== 节拍渲染（模式/自动/连接/置灰） ==================== */
    function linkReady() {
      var link = store.get("link") || {};
      return !!link.ready;
    }

    function refreshBeat() {
      var m = store.get("mode") || {};
      var known = !!m.known;
      var auto = !!m.auto;
      var running = m.autoRunning;          /* null/undefined = 未知 */
      var canMode = app.canPerm(PERM.MODE_SWITCH);

      setText(modeLabel, !known ? "未知"
        : (auto ? "自动（远程控制已禁用）" : "手动（允许远程操作）"));
      modeLabel.classList.toggle("warn", known && auto);
      modeLabel.classList.toggle("ok", known && !auto);
      setEnabled(btnManual, canMode && auto);
      setEnabled(btnAuto, canMode && !auto);

      var canCtrl = app.canPerm(PERM.AUTO_CONTROL) && auto;
      if (!known) {
        setText(autoLabel, "未知");
        autoLabel.classList.remove("warn", "dim");
      } else if (!auto) {
        setText(autoLabel, "手动模式（自动运行不可用）");
        autoLabel.classList.add("dim");
        autoLabel.classList.remove("warn");
      } else if (running === null || running === undefined) {
        setText(autoLabel, "自动模式 · 运行状态未知");
        autoLabel.classList.add("dim");
        autoLabel.classList.remove("warn");
      } else if (running) {
        setText(autoLabel, "自动模式 · 运行中");
        autoLabel.classList.add("warn");
        autoLabel.classList.remove("dim");
      } else {
        setText(autoLabel, "自动模式 · 已停止");
        autoLabel.classList.add("dim");
        autoLabel.classList.remove("warn");
      }
      setEnabled(btnAutoStart, canCtrl && running !== true);
      setEnabled(btnAutoStop, canCtrl && running === true);

      var session = app.session;
      if (linkReady()) {
        setText(connLabel, "已连接 · 用户 " + ((session && session.user) || "?") +
          "（" + ((session && session.roleText) || "?") + "）");
      } else {
        setText(connLabel, "未连接");
      }

      /* 无管理权限 ⇒ 配置卡整体只读（改了应用不上，别让人误以为生效）。 */
      var on = canCfg();
      cfgControls.forEach(function (el) { setEnabled(el, on); });
      vfdInputs.forEach(function (el) { setEnabled(el, on); });
      /* [权限闸与逐行状态共存] 循环行还有「是否参与 / 是否第一个」这类
         逐行状态 ⇒ 在整卡置灰**之后**再刷一遍（否则未参与行的上移下移
         会被整卡闸重新点亮）。 */
      cycleApplyGate();
      syncPolicyUi();

      /* 配置回显兜底：入页没连上时拉空过；连上后的第一拍补拉。
       * 变频器参数同样入页自动读一次（一次真实 485 读，不随节拍轮询——
       * 周期读会砸总线；要看新值点「刷新（读真实值）」）。 */
      if (!linkReady()) {
        configFetched = false;
        vfdFetched = false;
      } else if (!configFetched) {
        fetchConfig(true);
      } else if (!vfdFetched) {
        fetchVfdParams(true);
      }
    }

    /* 模式/链路事件即时刷 + 主节拍兜底（同值早退，文本不变零重排）。 */
    unsubs.push(store.on("mode", refreshBeat));
    unsubs.push(store.on("link", refreshBeat));
    var lastBeat = 0;
    timers.push(setInterval(function () {
      var now = Date.now();
      if (now - lastBeat >= HSF.prefs.refreshMs()) {
        lastBeat = now;
        refreshBeat();
      }
    }, 200));
    /* 自动状态族 1s 轮询（参考 HomePage 同款；模式标签随它活）。
     * [同样加超时闸] 后台轮询的承诺若悬挂（网页服务器半开）会无限堆积 ⇒
     * 这里也让超时闸兜住（超时后不再改 store）。 */
    timers.push(setInterval(function () {
      if (!linkReady()) return;
      withTimeout(HSF.api.getMode(), "查询运行模式").then(function (doc) {
        if (doc && doc.ok) store.applyEvent("mode", doc);
      });
    }, 1000));

    refreshBeat();
    void ctx;

    activeUnmount = function () {
      host.removeEventListener("scroll", onHostScroll);
      window.removeEventListener("resize", onWindowResize);
      clearTimeout(resizeTimer);
      if (cardObserver) { cardObserver.disconnect(); cardObserver = null; }
      clearTimeout(rebalanceTimer);
      if (spyRaf) { cancelAnimationFrame(spyRaf); spyRaf = 0; }
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      modeBlocks.forEach(function (blk) { blk.devCombo.destroy(); });
    };
  }
})();
