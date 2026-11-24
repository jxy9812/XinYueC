/* -*- coding: utf-8 -*-
 * 页 5 搅拌泵（设备类别=变频器）：运行状态 10 块 + 控制 + 实时趋势。
 *
 * [对齐参考 pages_monitor.VfdPage 逐项移植]：
 *   · 运行状态卡 10 块（运行状态/输出频率/输出转速/设定频率/母线电压/
 *     输出电压/输出电流/输出功率/输出转矩/故障），过渡态配色（加速/减速=
 *     强调蓝、运行=绿、故障=红、停机=正文色），「正在停机」口径后端已推导
 *     （runStateText）；未提供的字段显 "--"；
 *   · 控制卡：方向下拉（正转/反转，运行中改选=立即换向、稳态同向跳过）+
 *     启停合一按钮（运行中=停止红、停止=启动绿）+ 自由停车（仅运行中）+
 *     故障复位 + 按住点动（运行中禁点动；POST /api/jog/start 由服务器代持
 *     200ms 重发，松手 jog/stop）；
 *   · 调速：频率(Hz 0.1 定点)/转速(rpm 0~1440)下拉切换，缺省**转速**；
 *     给定框内右对齐单位后缀（setSuffix 口径，随调速方式换 rpm/Hz）；
 *     改值即下发（滑块去抖 250ms、数字框
 *     400ms）；停机只预置（`_preset_dirty` 启动帧捎带——服务端拒绝停机
 *     独立给定）；运行中采纳服务端回报的 setFreqHz（用户没在动它时）；
 *   · 实时趋势：3 序列（输出频率/给定频率/输出电流）60 点滚动窗；
 *     「刷新」下拉 200/500/1000/2000ms 纯客户端节流；拖动/滚轮接管视口、
 *     「跟随」/双击恢复；X 轴本地时钟 HH:MM:SS、Y 轴按首序列 5 档。
 *
 * [转速是换算值] 变频器无转速寄存器，转速由输出频率按铭牌额定点换算
 * （额定点由快照应答下发，缺省 50Hz→1440rpm）。
 *
 * [X 轴时间戳] 数据点打**墙上钟**（Date.now()）：帧到达时刻即该点时刻，
 * 与 C 端 m_trendStartMs=XDateTime_currentMSecsSinceEpoch 同源（UTC 纪元
 * 毫秒 + 本地时区展开成 HH:MM:SS）。updatedMs（单调毫秒）只用于**判重**
 * ——同一拍只追一次点，不参与轴刻度。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var SPEED_MODE_HZ = 0;
  var SPEED_MODE_RPM = 1;

  /* [拖动调速条渐变色 2026-11-24 用户「拖动调速的这个拖动条也按蓝绿红
   * 改下」「整体一个颜色改变不是这种」] 整条轨道 + 手柄 + 读数用**同一个
   * 当前值色**；拖动时颜色按电机转速在「显示范围」里的位置连续变化
   * （蓝→绿→橙→红，32 档量化）。范围随 VFD 快照下发
   * （speedRangeMin/Max，缺键回落 0~铭牌额定）。原 440/1000 三档阈值已废弃。 */
  var speedRangeMin = 0;
  var speedRangeMax = 1440;
  var speedGradientEnabled = true;
  var RUN_RUNNING_FORWARD = 3;
  var RUN_RUNNING_REVERSE = 4;

  /* 语义色（theme.css 令牌）。 */
  var C_TEXT = "var(--text)";
  var C_SUB = "var(--text-secondary)";
  var C_OK = "var(--ok)";
  var C_FAULT = "var(--fault)";
  var C_TRANS = "var(--accent-line)";      /* 过渡态（加速/减速）= 强调蓝 */
  var C_OFFLINE = "var(--offline)";

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  var activeUnmount = null;

  HSF.router.register(5, {
    title: "搅拌泵",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["5"]) || [];
      return t.length ? t : [0x00010001];
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

    /* ---- 可变状态 ---- */
    var ratedRpm = 1440.0;
    var ratedHz = 50.0;
    /* [2026-11-25] 搅拌最高转速（服务端下发；100~1440，缺省不额外限制）。 */
    var stirMaxRpm = 1440.0;
    var ratedKnown = false;
    var speedMode = SPEED_MODE_RPM;       /* 缺省转速（用户 2026-11-06 口径） */
    var presetDirty = false;
    /* 已按服务端快照播种过给定量程（停机时播种一次，避免半量程缺省）。 */
    var seededFromDoc = false;
    var deviceReady = null;               /* null = 还没探测过 */
    var notReadyReason = "";
    var jogHeld = false;                  /* 按住中（期间不禁用按钮） */
    var lastFrameMs = 0;                  /* updatedMs 判重（同一拍只追一次） */
    var syncing = false;                  /* 数字框↔滑块 互踢闸 */
    /* 当前着色档位（green/blue/red）：**只在跨档时**改 class，避免重排。 */
    var bandNow = null;

    var lastDoc = store.get("vfd") || {};

    function docFresh() { return !!lastDoc.fresh; }
    function isRunning() { return !!(docFresh() && lastDoc.running); }

    /* ==================================================================
     *                            DOM
     * ================================================================== */

    function blk(key, caption, unit, size) {
      var b = HSF.statBlock(caption, unit, size);
      return { b: b, key: key };
    }
    var specs = [
      /* 字号逐值照参考端 VfdPage 的 specs（pages_monitor.py:1225-1243）：
       * [字号 2026-11-14 用户定案「这些数字有点小」] 主读数（频率/转速）
       * 22→28、次级量（设定/母线/电压/电流/功率/转矩）16→24、状态 18→22
       * ⇒ 单位与数值同字号同基线（util.js 的 statBlock 让单位跟数值走）。 */
      blk("run", "运行状态", "", 22),
      blk("out", "输出频率", "Hz", 28),
      blk("rpm", "输出转速", "rpm", 28),
      blk("set", "设定频率", "Hz", 24),
      blk("bus", "母线电压", "V", 24),
      blk("volt", "输出电压", "V", 24),
      blk("cur", "输出电流", "A", 24),
      blk("power", "输出功率", "kW", 24),
      blk("torque", "输出转矩", "%", 24),
      blk("fault", "故障", "", 20),
      /* [电机温度 2026-11-24 用户「电机温度显示到红色框的位置上」]
       * 放在「故障」右侧空位（第 4 行第 2 列），与桌面端同序。 */
      blk("motorTemp", "电机温度", "℃", 24),
    ];
    var statByKey = {};
    var statGrid = HSF.el("div.stat-grid.cols-3.vfd-stats");
    specs.forEach(function (s) {
      statByKey[s.key] = s.b;
      statGrid.appendChild(s.b.root);
    });
    var rpmHint = HSF.el("div.caption.vfd-rpm-hint",
                         { text: "输出转速为频率换算值（变频器无转速传感器）" });
    var readCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "运行状态" }), statGrid, rpmHint]);

    /* ---- 控制卡 ---- */
    var ctrlHint = HSF.el("div.caption.vfd-ctrl-hint", { text: "" });

    var dirSelect = HSF.el("select.input");
    [["正转", "0"], ["反转", "1"]].forEach(function (p) {
      var opt = HSF.el("option", { text: p[0] });
      opt.value = p[1];
      dirSelect.appendChild(opt);
    });
    dirSelect.title = "停机时改选=预选方向（随下次启动下发）；运行中改选=立即换向";
    var dirReverse = function () { return dirSelect.value === "1"; };

    var btnRun = HSF.el("button.btn.vfd-run-btn", {
      type: "button", text: "启动",
      title: "运行中=停止；停止时=按界面给定值与所选方向启动" +
             "（给定随启动帧捎带下发）",
      onclick: onRunStop,
    });
    var btnFree = HSF.el("button.btn", {
      type: "button", text: "自由停车",
      title: "自由停车（仅运行中可点）",
      onclick: function () { sendOp(6); },
    });
    var btnReset = HSF.el("button.btn", {
      type: "button", text: "故障复位",
      onclick: function () { sendOp(8); },
    });
    var btnJog = HSF.el("button.btn.vfd-jog-btn", {
      type: "button", text: "按住点动",
      title: "按住期间由网页服务器代持 200ms 重发点动帧；松手即停。" +
             "方向跟随上面的方向下拉；运行中禁点动",
    });

    var speedModeSel = HSF.el("select.input");
    [["频率调速", String(SPEED_MODE_HZ)], ["转速调速", String(SPEED_MODE_RPM)]]
      .forEach(function (p) {
        var opt = HSF.el("option", { text: p[0] });
        opt.value = p[1];
        speedModeSel.appendChild(opt);
      });
    speedModeSel.value = String(SPEED_MODE_RPM);

    /* [数字区宽] 走 .num（ch 单位，随字号缩放）——旧的 .vfd-spin 写死
     * 9rem（126px）+ 后缀 + 箭头位 ⇒ 整框 198px，比参考 QSpinBox 的
     * 131px 大一圈（2026-11-13 第 4 轮对照）。 */
    var spin = HSF.el("input.input.num.vfd-spin", {
      type: "number", title: "给定值：改值即生效" +
        "（停机时只预置，启动时按此值运行）",
    });
    /* [框内单位后缀] 参考 QSpinBox.setSuffix：单位在框内右对齐
     * （「720 rpm」，切 Hz 同样带 Hz），随调速方式联动换字。 */
    var spinSuffix = HSF.el("span.spin-suffix", { text: " rpm" });
    var spinWrap = HSF.el("span.spin-unit", null, [spin, spinSuffix]);
    var slider = HSF.el("input.vfd-slider", {
      type: "range", title: "拖动调速：手停 0.25s 后下发；松手立即下发" +
        "（停机时只预置）",
    });
    var sliderValue = HSF.el("span.vfd-slider-value", { text: "--" });

    var row1 = HSF.el("div.vfd-row", null, [
      HSF.el("span.lbl", { text: "方向" }), dirSelect,
      btnRun, btnFree, btnReset,
      HSF.el("span.lbl.jog-lbl", { text: "点动" }), btnJog,
    ]);
    var row2 = HSF.el("div.vfd-row", null, [
      HSF.el("span.lbl", { text: "调速方式" }), speedModeSel,
      HSF.el("span.lbl", { text: "给定值" }), spinWrap,
    ]);
    /* [手动加速度 2026-11-26 用户「搅拌机界面 手动也支持下加速度」]
     * rpm/s 整数，0 = 跟随变频器（特殊文案与桌面 setSpecialValueText 同口径）；
     * 旁边给满量程折算提示（额定转速 ÷ 加速度），随快照额定点联动。 */
    var accelInput = HSF.el("input.input.num.vfd-spin", {
      type: "number", min: "0", max: "1440", step: "10", value: "0",
      title: "手动加转速速率（rpm/s）：从 0 加到给定转速的时间＝转速÷该值。\n"
        + "0 = 跟随变频器当前加速时间（不写参数）。启动/点动/调速时生效。",
    });
    var accelSuffix = HSF.el("span.spin-suffix", { text: " rpm/s" });
    var accelWrap = HSF.el("span.spin-unit", null, [accelInput, accelSuffix]);
    var accelHint = HSF.el("span.caption", { text: "0 = 跟随变频器（不改参数）" });
    function syncAccelHint() {
      var v = parseFloat(accelInput.value);
      accelHint.textContent = (isFinite(v) && v > 0)
        ? "满量程 ≈ " + (ratedRpm / v).toFixed(1) + " s（额定 " + Math.round(ratedRpm) + " rpm）"
        : "0 = 跟随变频器（不改参数）";
    }
    accelInput.addEventListener("input", syncAccelHint);
    var rowAcc = HSF.el("div.vfd-row", null, [
      HSF.el("span.lbl", { text: "加速度" }), accelWrap, accelHint,
    ]);
    var row3 = HSF.el("div.vfd-row", null, [
      HSF.el("span.lbl", { text: "拖动调速" }), slider, sliderValue,
    ]);
    var ctrlCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "控制" }),
      ctrlHint, row1, row2, rowAcc, row3]);

    /* ---- 趋势卡（共享模块；节流偏好两页共用一份） ---- */
    var trend = HSF.trendCard([
      { key: "out", name: "输出频率 Hz" },
      { key: "set", name: "给定频率 Hz" },
      { key: "cur", name: "输出电流 A" },
    ], "trendSampleMs");
    /* [后台趋势 2026-11-24] 把离开本页期间继续采到的历史灌回来。 */
    HSF.trendHistory.seed(trend.chart, "vfd");
    var trendHint = trend.hint;

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.vfd-page", null, [
      HSF.el("div.vfd-head", null, [
        HSF.el("span.page-head-title", { text: "搅拌泵" }),
        HSF.el("span.page-head-sub.caption",
               { text: "M512/M513 变频器 · Modbus-RTU" }),
      ]),
      readCard, ctrlCard, trend.card,
    ]));

    /* ==================================================================
     *                       给定：量纲切换 / 滑块 / 数字框
     * ================================================================== */

    function curUnit() { return speedMode === SPEED_MODE_RPM ? "rpm" : "Hz"; }

    function spinValue() {
      var v = parseFloat(spin.value);
      return isFinite(v) ? v : 0;
    }

    /* ---- 拖动调速条渐变（2026-11-24）----
     * 轨道渐变由 CSS 固定（全量程蓝→绿→橙→红）；这里只按当前转速取
     * 手柄/读数的颜色。颜色量化到 32 个色阶再写 DOM ⇒ 拖动时不逐格重排
     * （与桌面 pages_monitor.py 的 SPEED_GRADIENT_STEPS 同口径）。 */
    function valueToRpm(value) {
      if (speedMode === SPEED_MODE_RPM) return value;
      if (!(ratedHz > 0) || !(ratedRpm > 0)) return null;
      return value / ratedHz * ratedRpm;
    }

    function syncBand(value) {
      var rpm = valueToRpm(value);
      var lo = speedRangeMin;
      var hi = speedRangeMax;
      if (!(hi > lo)) {
        lo = 0;
        hi = ratedRpm > 0 ? ratedRpm : 1440;
      }
      var color = null;
      if (speedGradientEnabled && rpm !== null && hi > lo) {
        var t = (rpm - lo) / (hi - lo);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        t = Math.round(t * 32) / 32;
        color = HSF.gradientColor(lo + t * (hi - lo), lo, hi);
      }
      if (!color) color = "var(--accent-line)";
      if (color === bandNow) return;
      bandNow = color;
      slider.style.setProperty("--vfd-current", color);
      sliderValue.style.color = color;
    }

    function givenPayload() {
      var value = spinValue();
      if (speedMode === SPEED_MODE_RPM) {
        var hz = ratedRpm > 0 ? (value / ratedRpm * ratedHz) : 0.0;
        return { rpm: value, hz: hz };
      }
      return { hz: value };
    }

    /* 停机预置的提示文案（**唯一事实源**，与桌面端 pages_monitor.py 的
     * `_preset_given_text()` 逐字同源）。
     *
     * [为什么要有这个函数] renderAll() 每次遥测到达都会**重刷**提示行 ⇒
     * 预置文案必须能从「当前给定值」随时重新生成，否则刚拖出来的预置值
     * 会被通用文案冲掉，操作员看不到自己预置了多少。
     *
     * [0 的特例] 服务端把捎带给定里的 **0 当「未指定」**（两端同口径：
     * 写频率的判断是 `if given_hz > 0`）⇒ 预置 0 之后启动，设备是**沿用
     * 变频器里已有的给定**，不是「按 0 运行」⇒ 文案不能说「启动时按此值
     * 运行」（那是假的）⇒ 0 单独一句话。 */
    function presetGivenText() {
      var value = spinValue();
      if (!(value > 0)) {
        return "已预置给定 0 " + curUnit() +
          "（0 表示不指定给定，启动时沿用变频器当前值）";
      }
      return "已预置给定 " + value + " " + curUnit() +
        "，启动时按此值运行（停机时不下发设备）";
    }

    function applyRanges(baseHz) {
      syncing = true;
      try {
        setText(spinSuffix, " " + curUnit());   /* 框内单位随调速方式联动 */
        var limitedRpm = Math.min(ratedRpm, stirMaxRpm);
        if (speedMode === SPEED_MODE_RPM) {
          var maxRpm = Math.round(limitedRpm);
          spin.min = "0";
          spin.max = String(maxRpm);
          spin.step = "1";
          spin.value = String(Math.max(0, Math.min(maxRpm,
            Math.round(baseHz / ratedHz * ratedRpm))));
          slider.min = "0";
          slider.max = String(maxRpm);
          slider.step = "1";
          slider.value = spin.value;
        } else {
          var maxHz = Math.round(
            (ratedRpm > 0 ? limitedRpm / ratedRpm * ratedHz : ratedHz) * 10
          ) / 10;
          spin.min = "0";
          spin.max = String(maxHz);
          spin.step = "0.1";               /* 0.1Hz 定点 */
          spin.value = String(Math.max(0, Math.min(maxHz,
            Math.round(baseHz * 10) / 10)));
          slider.min = "0";
          slider.max = String(maxHz);
          slider.step = "0.1";
          slider.value = spin.value;
        }
        setText(sliderValue, spinValue() + " " + curUnit());
        syncBand(spinValue());
      } finally {
        syncing = false;
      }
    }

    function onSpeedModeChanged() {
      var prevMode = speedMode;
      var newMode = parseInt(speedModeSel.value, 10);
      if (newMode === prevMode) return;
      /* 切换方式后按当前给定换算初值（无数据退中值），不产生意外跳变。 */
      var baseHz = (docFresh() && typeof lastDoc.setFreqHz === "number")
        ? lastDoc.setFreqHz : (ratedHz / 2.0);
      speedMode = newMode;
      applyRanges(baseHz);
    }
    speedModeSel.addEventListener("change", onSpeedModeChanged);

    function adoptSetFreq(hz) {
      /* 变频器回报的设定频率对到滑块/数字框（保持当前调速口径）。 */
      if (speedMode === SPEED_MODE_RPM) {
        if (ratedHz <= 0) return;
        var value = Math.round(hz / ratedHz * ratedRpm);
        value = Math.max(0, Math.min(Math.round(
          Math.min(ratedRpm, stirMaxRpm)), value));
        syncing = true;
        try {
          if (parseFloat(spin.value) !== value) spin.value = String(value);
          if (parseFloat(slider.value) !== value) slider.value = String(value);
          setText(sliderValue, value + " rpm");
          syncBand(value);
        } finally { syncing = false; }
      } else {
        var v = Math.round(hz * 10) / 10;
        v = Math.max(0, Math.min(Math.round(ratedHz * 10) / 10, v));
        syncing = true;
        try {
          if (parseFloat(spin.value) !== v) spin.value = String(v);
          if (parseFloat(slider.value) !== v) slider.value = String(v);
          setText(sliderValue, v + " Hz");
          syncBand(v);
        } finally { syncing = false; }
      }
    }

    /* 去抖下发：滑块 250ms / 数字框 400ms（拖动中不发，停手才发一条）。 */
    var sliderTimer = null;
    var spinTimer = null;
    slider.addEventListener("input", function () {
      if (syncing) return;
      var value = parseFloat(slider.value);
      if (!isFinite(value)) return;
      syncing = true;
      try {
        spin.value = String(value);
        setText(sliderValue, value + " " + curUnit());
        syncBand(value);
      } finally { syncing = false; }
      if (spinTimer) { clearTimeout(spinTimer); spinTimer = null; }
      if (sliderTimer) clearTimeout(sliderTimer);
      sliderTimer = setTimeout(function () {
        sliderTimer = null;
        onGivenSettled(true);
      }, 250);
    });
    slider.addEventListener("change", function () {
      /* 松手/单点提交：手已离开意图明确，立即**提交**（不等去抖窗口）。
       * [2026-11-18] 提交≠发帧：停机时 onGivenSettled 只预置、不发帧
       * （服务端拒绝停机独立给定）⇒ 松手不会招来「命令被拒」。 */
      if (syncing) return;
      if (sliderTimer) { clearTimeout(sliderTimer); sliderTimer = null; }
      if (spinTimer) { clearTimeout(spinTimer); spinTimer = null; }
      onGivenSettled(false);
    });
    spin.addEventListener("input", function () {
      if (syncing) return;
      var value = parseFloat(spin.value);
      if (!isFinite(value)) return;
      syncing = true;
      try {
        if (parseFloat(slider.value) !== value) slider.value = String(value);
        setText(sliderValue, value + " " + curUnit());
        syncBand(value);
      } finally { syncing = false; }
      if (sliderTimer) { clearTimeout(sliderTimer); sliderTimer = null; }
      if (spinTimer) clearTimeout(spinTimer);
      spinTimer = setTimeout(function () {
        spinTimer = null;
        onGivenSettled(true);
      }, 400);
    });

    function onGivenSettled(debounced) {
      if (syncing) return;
      var value = spinValue();
      if (!isRunning()) {
        /* 停机时只预置，不发帧（服务端拒绝停机独立给定：给定是**运行参数**）。
         *
         * [2026-11-18 与桌面端对齐] 停机时的三条交互路径（拖动去抖 / 松手
         * 提交 / 数字框去抖）**一律**走这里 ⇒ 停机永远不发帧；提示行改成
         * 「已预置给定 …，启动时按此值运行（停机时不下发设备）」——把
         * **预期行为**说清楚，不让操作员以为被拒。
         * 预置值等启动时由启动帧捎带下去（onRunStop → givenPayload）。 */
        presetDirty = true;
        var msg = presetGivenText();
        setText(ctrlHint, msg);
        HSF.statusMessage(msg);
        return;
      }
      presetDirty = false;
      if (speedMode === SPEED_MODE_RPM) {
        var hz = ratedRpm > 0 ? (value / ratedRpm * ratedHz) : 0.0;
        sendOp(3, { rpm: value, hz: hz }, true);
      } else {
        sendOp(2, { hz: value }, true);
      }
      void debounced;
    }

    /* ==================================================================
     *                          控制动作
     * ================================================================== */

    function blockedReason() {
      var reason = HSF.controlBlockedReason(
        ctx.canPerm(HSF.app.PERM.DEVICE_CONTROL));
      if (!reason && deviceReady === false) {
        reason = notReadyReason || "设备未就绪";
      }
      return reason;
    }

    /* [手动加速度 2026-11-27 工作流实证] 运动类命令统一捎带 accelRpmS。
     * 原实现只在 sendOp 里注入，而「启动/运行中换向/点动」三条路径不走
     * sendOp ⇒ 网页端这三类动作的手动加速度被丢弃（桌面端四条 op 统一挂）。
     * 0/非法 = 不发键（跟随变频器，不碰参数）。 */
    function withAccel(doc, op) {
      if (op === 4 || op === 9 || op === 2 || op === 3) {
        var accel = parseFloat(accelInput.value);
        if (isFinite(accel) && accel > 0) doc.accelRpmS = accel;
      }
      return doc;
    }

    function sendOp(op, extra, silent) {
      var reason = blockedReason();
      if (reason) {
        HSF.statusMessage("禁止操作：" + reason);
        return;
      }
      var doc = { op: op };
      if (extra) Object.keys(extra).forEach(function (k) {
        doc[k] = extra[k];
      });
      /* [手动加速度 2026-11-26] 运动类命令捎带 accelRpmS（rpm/s，>0）:
       * 服务端先写满量程加速时间（额定转速 ÷ 加速度）再发命令字，
       * 与自动模式同一条数学；0 = 不发键（跟随变频器，不碰参数）。 */
      withAccel(doc, op);   /* START/JOG/设频/设速 */
      if (!silent) HSF.statusMessage("下发命令 op=" + op + "…");
      HSF.api.vfdControl(doc).then(function (resp) {
        if (resp && resp.ok) {
          if (!silent) HSF.statusMessage("命令已受理");
        } else {
          HSF.statusMessage("命令被拒：" + ((resp && resp.err) || "未知原因"));
        }
        renderAll();
      });
    }

    function onRunStop() {
      if (isRunning()) {
        sendOp(5);                     /* OP_STOP */
        return;
      }
      var reason = blockedReason();
      if (reason) {
        HSF.statusMessage("禁止操作：" + reason);
        return;
      }
      /* 给定必须捎带在启动帧里（服务端拒绝停机独立给定）。预置值在此刻兑现。 */
      presetDirty = false;
      var payload = givenPayload();
      payload.rev = dirReverse();
      withAccel(payload, 4);            /* 启动也捎带加速度 */
      HSF.statusMessage("正在启动（" + (dirReverse() ? "反转" : "正转") +
                        "，给定 " + spinValue() + " " + curUnit() + "）…");
      HSF.api.vfdControl(
        Object.assign({ op: 4 }, payload)).then(function (resp) {
        if (resp && resp.ok) {
          HSF.statusMessage("命令已受理（启动中，设备按加减速时间爬坡）");
        } else {
          HSF.statusMessage("启动被拒：" + ((resp && resp.err) || "未知原因"));
        }
        renderAll();
      });
    }

    function onDirChanged() {
      if (!isRunning()) return;        /* 停机＝仅预选 */
      var st = lastDoc;
      var reverse = dirReverse();
      /* 稳态同向才跳过；换向途中翻回原方向也必须下发（服务端按"同向命令"
       * 取消待决换向、拉回原方向——对齐 C 客户端 vfdDirChangeSlot）。 */
      var steadySame = (reverse === !!st.reverse) &&
        (st.runState === RUN_RUNNING_FORWARD ||
         st.runState === RUN_RUNNING_REVERSE);
      if (steadySame) return;
      presetDirty = false;
      var payload = givenPayload();
      payload.rev = reverse;
      withAccel(payload, 4);            /* 换向=一次启动命令，同样捎带 */
      HSF.statusMessage("正在换向（" + (reverse ? "反转" : "正转") +
                        "：先减速过零再反向加速）…");
      HSF.api.vfdControl(
        Object.assign({ op: 4 }, payload)).then(function (resp) {
        if (resp && resp.ok) {
          HSF.statusMessage("换向命令已受理");
        } else {
          HSF.statusMessage("换向被拒：" + ((resp && resp.err) || "未知原因"));
        }
        renderAll();
      });
    }
    dirSelect.addEventListener("change", onDirChanged);

    /* ---- 点动：按住语义（服务器代持 200ms 重发；松手 jog/stop） ---- */
    function jogStart() {
      if (jogHeld) return;
      var reason = blockedReason();
      if (reason) {
        HSF.statusMessage("禁止点动：" + reason);
        return;
      }
      if (isRunning()) {
        HSF.statusMessage("运行中禁点动（先停止再点动）");
        return;
      }
      jogHeld = true;
      var payload = givenPayload();
      payload.rev = dirReverse();
      payload.connId = store.connId ? Number(store.connId) : null;
      withAccel(payload, 9);            /* 点动同样捎带加速度 */
      HSF.statusMessage("点动中（" + (payload.rev ? "反转" : "正转") +
                        "，给定 " + spinValue() + " " + curUnit() +
                        "）——松手即停");
      HSF.api.jogStart(payload).then(function (resp) {
        if (!(resp && resp.ok)) {
          jogHeld = false;
          HSF.statusMessage("点动失败：" + ((resp && resp.err) || "未知原因"));
          renderAll();
        }
      });
    }
    function jogStop() {
      if (!jogHeld) return;
      jogHeld = false;
      HSF.api.jogStop().then(function (resp) {
        HSF.statusMessage(resp && resp.ok
          ? "点动释放"
          : "点动释放失败：" + ((resp && resp.err) || "未知原因"));
        renderAll();
      });
    }
    btnJog.addEventListener("pointerdown", function (ev) {
      if (ev.button !== 0) return;
      ev.preventDefault();
      jogStart();
    });
    ["pointerup", "pointerleave", "pointercancel"].forEach(function (name) {
      btnJog.addEventListener(name, jogStop);
    });
    /* 页面切走/窗口失焦也要松手即停（安全动作，不等看门狗）。 */
    unsubs.push(store.on("jog", function (doc) {
      /* 服务器代持状态广播（页面断开自动释放/超时强停都走这里）。 */
      if (doc && doc.active === false && jogHeld) jogHeld = false;
      renderAll();
    }));

    /* ==================================================================
     *                          刷新
     * ================================================================== */

    function showStat(key, text, color) {
      statByKey[key].set(text, color === undefined ? C_TEXT : color);
    }

    function renderAll() {
      var doc = lastDoc = store.get("vfd") || lastDoc || {};
      var fresh = !!doc.fresh;
      var reason = blockedReason();

      /* 额定点（快照应答随帧下发）。 */
      if (typeof doc.ratedRpm === "number" && doc.ratedRpm > 0) {
        ratedRpm = doc.ratedRpm;
        ratedKnown = !!doc.ratedKnown;
      }
      if (typeof doc.ratedHz === "number" && doc.ratedHz > 0) {
        ratedHz = doc.ratedHz;
        ratedKnown = !!doc.ratedKnown;
      }
      /* [2026-11-25] 搅拌最高转速：服务端随快照/遥测下发；变了才重设量程。 */
      if (typeof doc.stirMaxRpm === "number" && doc.stirMaxRpm > 0
          && doc.stirMaxRpm !== stirMaxRpm) {
        stirMaxRpm = doc.stirMaxRpm;
        applyRanges(typeof doc.setFreqHz === "number"
          ? doc.setFreqHz : (ratedHz / 2.0));
      }
      /* [拖动条渐变 2026-11-24] 电机转速显示范围随 VFD 快照下发。 */
      if (typeof doc.speedRangeMin === "number") {
        speedRangeMin = doc.speedRangeMin;
      }
      if (typeof doc.speedRangeMax === "number") {
        speedRangeMax = doc.speedRangeMax;
      }
      if (typeof doc.speedGradient === "boolean") {
        speedGradientEnabled = doc.speedGradient;
      }
      /* 范围可能刚变化 ⇒ 用当前滑块值重算手柄/读数颜色。 */
      syncBand(parseFloat(slider.value) || 0);
      var _lim = Math.min(ratedRpm, stirMaxRpm);
      setText(rpmHint, ratedKnown
        ? ("输出转速为频率换算值（额定点 " + ratedRpm.toFixed(0) + " rpm @ " +
           ratedHz.toFixed(0) + " Hz" +
           (_lim < ratedRpm ? ("，最高限速 " + _lim.toFixed(0) + " rpm") : "") +
           "）")
        : "输出转速为频率换算值（变频器无转速传感器）");

      if (!fresh) {
        Object.keys(statByKey).forEach(function (k) { showStat(k, "--"); });
        showStat("run", "无数据", C_OFFLINE);
      } else {
        var transition = (doc.runState === 7 || doc.runState === 8);
        var color = doc.faulted ? C_FAULT
          : (transition ? C_TRANS : (doc.running ? C_OK : C_TEXT));
        showStat("run", doc.runStateText || "--", color);
        showStat("out", typeof doc.outputFreqHz === "number"
          ? doc.outputFreqHz.toFixed(2) : "--");
        var rpmText = "--";
        var rpmNum = null;
        if (typeof doc.speedRpm === "number") {
          rpmText = doc.speedRpm.toFixed(0);
          rpmNum = doc.speedRpm;
        } else if (typeof doc.outputFreqHz === "number" && ratedHz > 0) {
          rpmNum = doc.outputFreqHz / ratedHz * ratedRpm;
          rpmText = rpmNum.toFixed(0);
        }
        /* [显示渐变 2026-11-24 用户「电机转速也支持下，其他界面也支持下」]
         * 按服务端下发的转速范围上色；缺范围键保持单色。 */
        showStat("rpm", rpmText,
                 HSF.gradientColor(rpmNum, doc.speedRangeMin,
                                   doc.speedRangeMax) || undefined);
        showStat("set", typeof doc.setFreqHz === "number"
          ? doc.setFreqHz.toFixed(2) : "--");
        showStat("bus", typeof doc.busVoltageV === "number"
          ? doc.busVoltageV.toFixed(1) : "--");
        showStat("volt", typeof doc.outputVoltageV === "number"
          ? doc.outputVoltageV.toFixed(0) : "--");
        showStat("cur", typeof doc.outputCurrentA === "number"
          ? doc.outputCurrentA.toFixed(2) : "--");
        showStat("power", typeof doc.outputPowerKw === "number"
          ? doc.outputPowerKw.toFixed(2) : "--");
        showStat("torque", typeof doc.torquePercent === "number"
          ? doc.torquePercent.toFixed(1) : "--");
        showStat("fault", doc.faultText || "--",
                 doc.faulted ? C_FAULT : C_OK);
        /* [电机温度 2026-11-24] 缺键=未配传感器 ⇒ "--"；有值按服务端下发的
         * 电机温度显示范围上色（缺范围/关闭渐变=正文色）。 */
        var mt = doc.motorTempC;
        var mtColor = C_TEXT;
        if (typeof mt === "number" && doc.motorTempGradient !== false) {
          mtColor = HSF.gradientColor(mt, doc.motorTempRangeMin,
                                      doc.motorTempRangeMax) || C_TEXT;
        }
        showStat("motorTemp", typeof mt === "number" ? mt.toFixed(1) : "--",
                 mtColor);
      }

      /* ---- 趋势追点（同一拍只追一次；数据点用墙上钟） ---- */
      var tMs = doc.updatedMs || 0;
      if (fresh && tMs && tMs !== lastFrameMs &&
          trend.chart.acceptSample(Date.now())) {
        lastFrameMs = tMs;
        var now = Date.now();
        if (typeof doc.outputFreqHz === "number") {
          trend.chart.append("out", now, doc.outputFreqHz);
        }
        if (typeof doc.setFreqHz === "number") {
          trend.chart.append("set", now, doc.setFreqHz);
        }
        if (typeof doc.outputCurrentA === "number") {
          trend.chart.append("cur", now, doc.outputCurrentA);
        }
      }
      setText(trendHint, trend.chart.isFollowing()
        ? "" : "视口已接管：追点继续、窗口暂停右移");

      /* ---- 启停按钮（运行中=停止红 / 停止=启动绿） ---- */
      var running = isRunning();
      var wantText = running ? "停止" : "启动";
      if (btnRun.textContent !== wantText) btnRun.textContent = wantText;
      btnRun.classList.toggle("is-stop", running);

      /* ---- 控件使能（点动按住中不更新，防浏览器清按下态掐断点动） ---- */
      var canCtrl = !reason;
      [btnRun, btnFree, btnReset].forEach(function (b) {
        if (b.disabled !== !canCtrl) b.disabled = !canCtrl;
      });
      btnFree.disabled = !canCtrl || !running;   /* 自由停车仅运行中可点 */
      if (!jogHeld) {
        btnJog.disabled = !canCtrl || running;   /* 运行中禁点动 */
      }
      slider.disabled = !canCtrl;
      spin.disabled = !canCtrl;
      dirSelect.disabled = !canCtrl;
      /* 调速方式下拉只切本地 Hz/rpm 单位（不发命令），但给定框在闸下已
       * 禁 ⇒ 单位切换无意义；与桌面端同批补齐（2026-11-19 三端同款缺口）。 */
      speedModeSel.disabled = !canCtrl;

      /* ---- 滑块/数字框跟随变频器设定值（用户没在动它时才采纳） ----
       * [停机播种一次] 快照是**异步**到的：入页时 store 里可能还没有 VFD
       * 数据，initRanges() 只能按「半量程」播种（720 rpm＝25Hz）⇒ 与同
       * 卡「设定频率 10.00 Hz（＝288 rpm）」自相矛盾（2026-11-13 对照实
       * 拍）。参考端在**构造时**就用当前 set_freq_hz 播种给定量程
       * （pages_monitor.py:1412-1432 _on_speed_mode），web 侧对应口径
       * 就是：快照到达后（停机时）按 setFreqHz 播种一次。
       * [运行中实时跟随] 运行中照参考端 _adopt_set_freq 实时对冲
       * （pages_monitor.py:1918-1921）。
       * 用户刚拖过的给定由 presetDirty 挡着（停机预置时置位），不会
       * 被回灌覆盖。 */
      if (fresh && typeof doc.setFreqHz === "number" &&
          !sliderTimer && !spinTimer && !presetDirty) {
        if (running) {
          adoptSetFreq(doc.setFreqHz);
        } else if (!seededFromDoc) {
          seededFromDoc = true;
          applyRanges(doc.setFreqHz);
        }
      }

      /* ---- 控制提示行（恒占位） ---- */
      var hint;
      if (reason) {
        hint = "控制已禁用：" + reason;
      } else if (presetDirty && !running) {
        /* [2026-11-18 与桌面端对齐] 预置文案走 presetGivenText()（唯一事实
         * 源）⇒ 提示行不会被 renderAll 的重刷冲掉。 */
        hint = presetGivenText();
      } else if (!running) {
        hint = "已停机：拖动/改值只预置给定，启动时按此值运行（在线调速需先启动）";
      } else {
        hint = "";
      }
      setText(ctrlHint, hint);
    }

    unsubs.push(store.on("vfd", renderAll));
    unsubs.push(store.on("link", renderAll));
    unsubs.push(store.on("mode", renderAll));
    unsubs.push((function () {
      var f = function () { renderAll(); };
      window.addEventListener("resize", f);
      return function () { window.removeEventListener("resize", f); };
    })());

    /* ---- 入页拉快照：顺便探测「设备是否装配」+ 取额定点 ---- */
    if ((store.get("link") || {}).ready) {
      HSF.api.vfdSnapshot().then(function (doc) {
        var okFlag = !!(doc && doc.ok);
        if (!okFlag) {
          deviceReady = false;
          notReadyReason = (doc && doc.err) || "设备未就绪";
        } else {
          deviceReady = true;
          notReadyReason = "";
        }
        renderAll();
      });
    }

    /* 入页若已有数据，按当前给定初始化输入框量程。 */
    (function initRanges() {
      var baseHz = (docFresh() && typeof lastDoc.setFreqHz === "number")
        ? lastDoc.setFreqHz : (ratedHz / 2.0);
      applyRanges(baseHz);
    })();

    renderAll();
    void ctx;

    activeUnmount = function () {
      /* 切页松手即停（安全动作；服务器侧 SSE 断开也会自动释放）。 */
      if (jogHeld) { jogHeld = false; HSF.api.jogStop(); }
      if (sliderTimer) clearTimeout(sliderTimer);
      if (spinTimer) clearTimeout(spinTimer);
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      trend.chart.destroy();
    };
  }
})();
