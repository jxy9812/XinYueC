/* -*- coding: utf-8 -*-
 * 顶部标题栏：☰ + 时钟(1Hz) + 居中页名 + 运行模式钮 + 自动状态 + 健康 + 温湿度。
 *
 * [对齐参考客户端 TitleBar]（client_py/hsfsrv_client/widgets.py:315-）：
 * 布局从左到右 ☰ / 时钟盒（日期+时间，窄栏隐藏日期）/ 居中标题 /
 * 模式钮 / 自动运行徽标 / 健康 / 温湿度两行。文案与配色同源：
 *   · 手动=绿 #2E7D32、自动=橙 #ED6C02、未知=灰 #5A6572；
 *   · 自动状态三态：自动·运行中（橙）/ 自动·已停止（灰=正常待命）/
 *     自动·未知（灰），手动模式整块隐藏；
 *   · 健康「故障N / 离线N」全正常时**不显示**（不占视觉重量）；
 *   · 温湿度带单位占位「--℃ / --%RH」（裸 -- 像断裂横线），
 *     tooltip 写明是电箱内读数。
 * [账户钮不在这里] 参考客户端把账户入口放**状态栏右下角**
 * （widgets.py:563-609，标题栏已 7 个元素两次"抢地方被裁"），web 同口径。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var NARROW_DATE_W = 700;        /* 窄栏阈值（参考 TitleBar.NARROW_W） */

  function mount(root) {
    HSF.replaceChildren(root);

    var menuBtn = HSF.el("button.tb-menu-btn", {
      type: "button", title: "显示/隐藏导航", text: "\u2630",
      onclick: function () { HSF.app.toggleNav(); },
    });

    var dateLabel = HSF.el("div.tb-date", { text: "" });
    var timeLabel = HSF.el("div.tb-time", { text: "" });
    var clockBox = HSF.el("div.tb-clock", null, [dateLabel, timeLabel]);

    var title = HSF.el("div.tb-title", { text: "" });

    /* ---- 相机徽章（模式钮左侧；2026-11-22 用户「顶部状态栏加相机位图」）
     * 只画位图，不开流：点击弹/收悬浮相机卡，卡片存在期间才订相机主题、
     * 关闭即退订（免得后台一直传大流量）。图标自绘 SVG（不引资源）。 ---- */
    var cameraBtn = HSF.el("button.tb-camera-btn", {
      type: "button",
      title: "相机画面（点开/再点关闭）",
      "aria-label": "相机画面",
      onclick: function () { HSF.app.toggleCameraPreview(); },
    });
    cameraBtn.innerHTML =
      '<svg viewBox="0 0 36 36" xmlns="http://www.w3.org/2000/svg" ' +
      'aria-hidden="true" focusable="false">' +
      '<g fill="none" stroke="currentColor" stroke-width="2" ' +
      'stroke-linejoin="round">' +
      '<rect x="5.5" y="11" width="25" height="17" rx="3"/>' +
      '<path d="M12.5 11 L15 6.8 H21 L23.5 11"/>' +
      '<circle cx="18" cy="19.2" r="5.2"/>' +
      '<circle cx="26" cy="14.8" r="1" fill="currentColor"/>' +
      '</g></svg>';

    /* ---- 模式钮（点击切换，需 PERM_MODE_SWITCH；未知/无权限禁用） ---- */
    var modeBtn = HSF.el("button.tb-mode-btn", {
      type: "button", title: "点击切换运行模式（需管理员）",
      text: "未知", onclick: function () { HSF.app.toggleRunMode(); },
    });

    var autoState = HSF.el("span.tb-auto-state.hidden", { text: "" });

    var health = HSF.el("span.tb-health.hidden", { text: "" });

    var tempLabel = HSF.el("div.tb-temp", { text: "--\u2103" });
    var humLabel = HSF.el("div.tb-hum", { text: "--%RH" });
    tempLabel.title = "电箱温度";
    humLabel.title = "电箱湿度";
    var thBox = HSF.el("div.tb-th", null, [tempLabel, humLabel]);

    root.appendChild(menuBtn);
    root.appendChild(clockBox);
    root.appendChild(title);
    root.appendChild(cameraBtn);
    root.appendChild(modeBtn);
    root.appendChild(autoState);
    root.appendChild(health);
    root.appendChild(thBox);
    /* [窄屏换行分隔符] 桌面端 display:none（零影响）；窄屏（≤860px）时
     * 以「flex-basis:100% + 高度 0」占一行，强制标题栏在 ☰/标题/模式 与
     * 时钟/自动态/健康/温湿度 之间断行（theme.css 的 .tb-break 规则）。 */
    root.appendChild(HSF.el("span.tb-break", { "aria-hidden": "true" }));

    /* ---- 时钟 1Hz（参考同款 QTimer 1000ms） ---- */
    function refreshClock() {
      var now = new Date();
      function pad(n) { return (n < 10 ? "0" : "") + n; }
      timeLabel.textContent = pad(now.getHours()) + ":" +
        pad(now.getMinutes()) + ":" + pad(now.getSeconds());
      dateLabel.textContent = now.getFullYear() + "-" +
        pad(now.getMonth() + 1) + "-" + pad(now.getDate());
      dateLabel.style.display = (window.innerWidth >= NARROW_DATE_W) ? "" : "none";
    }
    var timer = setInterval(refreshClock, 1000);
    refreshClock();

    /* ---- 标题栏内容显隐（页 13·悬浮窗分区·标题栏内容；prefs 单一事实源） ----
     * 参考客户端这两项是常显（QToolTip 写明）；web 版做成可隐藏并
     * 持久化（PARITY_PLAN 页 16 口径「兑现才摆」）——勾选即时生效。 */
    function applyContentPrefs() {
      var p = HSF.prefs;
      clockBox.style.display = p.showTitleClock() ? "" : "none";
      thBox.style.display = p.showTitleTemp() ? "" : "none";
      /* [2026-11-22 标题栏新元素同步] 模式钮 + 自动徽标同属一条信息链 ⇒
       * 一起显隐；健康指示只控制"允不允许显示"，全部正常时仍由
       * applyHealth 的 .hidden 类负责不占位。 */
      var modeOn = p.showTitleMode();
      modeBtn.style.display = modeOn ? "" : "none";
      autoState.style.display = modeOn ? "" : "none";
      health.style.display = p.showTitleHealth() ? "" : "none";
      cameraBtn.style.display = p.showCamera() ? "" : "none";
    }
    HSF.titlebar.applyContentPrefs = applyContentPrefs;
    applyContentPrefs();

    /* ---- 数据联动 ---- */
    var unsubs = [
      HSF.store.on("mode", applyMode),
      HSF.store.on("health", applyHealth),
      HSF.store.on("temphum", applyTempHum),
    ];
    applyMode(HSF.store.get("mode"));
    applyHealth(HSF.store.get("health"));
    applyTempHum(HSF.store.get("temphum"));

    function applyMode(doc) {
      doc = doc || {};
      var known = !!doc.known;
      var auto = !!doc.auto;
      modeBtn.classList.toggle("auto", known && auto);
      modeBtn.classList.toggle("unknown", !known);
      modeBtn.textContent = !known ? "未知" : (auto ? "自动" : "手动");
      var canSwitch = HSF.app.canPerm(HSF.app.PERM.MODE_SWITCH);
      modeBtn.disabled = !known || !canSwitch;
      modeBtn.title = !canSwitch
        ? "切换运行模式需要管理员账户"
        : "点击切换运行模式（需管理员）";
      /* 自动运行徽标：手动/未知隐藏（参考 _restyle_auto_state 口径） */
      if (!known || !auto) {
        autoState.classList.add("hidden");
        return;
      }
      autoState.classList.remove("hidden");
      if (doc.autoRunning === null || doc.autoRunning === undefined) {
        autoState.textContent = "自动 · 未知";
        autoState.classList.remove("running");
      } else if (doc.autoRunning) {
        autoState.textContent = "自动 · 运行中";
        autoState.classList.add("running");
      } else {
        autoState.textContent = "自动 · 已停止";
        autoState.classList.remove("running");
      }
      autoState.title = "自动控制器的实际运行状态（不是模式开关的位置）。\n" +
        "「自动 · 已停止」= 已授权自动但还没按启动（正常待命）。";
    }

    function applyHealth(doc) {
      doc = doc || {};
      var fault = doc.faultCount || 0;
      var offline = doc.offlineCount || 0;
      if (!fault && !offline) {
        health.classList.add("hidden");       /* 全正常不占位 */
        return;
      }
      var parts = [];
      if (fault) parts.push("故障" + fault);
      if (offline) parts.push("离线" + offline);
      health.textContent = parts.join(" / ");
      health.classList.add("alarm");
      health.classList.remove("hidden");
    }

    function applyTempHum(doc) {
      doc = doc || {};
      /* 对齐参考 widgets.py:474-477：有效值 "%.1f℃"/"%.1f%%"（湿度不带
       * RH）；无效 = 裸 "--"（不带单位）。初始占位 "--℃ / --%RH" 是
       * widgets.py:436-437 的原样口径，保留。 */
      tempLabel.textContent = typeof doc.tempC === "number"
        ? doc.tempC.toFixed(1) + "\u2103" : "--";
      humLabel.textContent = typeof doc.humPct === "number"
        ? doc.humPct.toFixed(1) + "%" : "--";
    }

    /* 窄栏日期显隐随窗口宽度 */
    function onResize() {
      dateLabel.style.display = (window.innerWidth >= NARROW_DATE_W) ? "" : "none";
    }
    window.addEventListener("resize", onResize);

    return {
      setTitle: function (t) { title.textContent = t || ""; },
      destroy: function () {
        clearInterval(timer);
        window.removeEventListener("resize", onResize);
        unsubs.forEach(function (off) { off(); });
      },
    };
  }

  HSF.titlebar = { mount: mount };
})();
