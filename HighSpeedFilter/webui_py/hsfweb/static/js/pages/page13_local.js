/* -*- coding: utf-8 -*-
 * 页 13 本地设置（2026-11-13 三页合并）：界面设置 / 悬浮窗 / 窗口与屏幕。
 *
 * [合并背景] 用户定案「本地设置页 + 悬浮窗设置页 + 窗口设置页 三页合并
 * 成一个界面，三页内容全部搬进合并页，一个功能项不许丢」。合并后页数
 * 18 → 16（页号 0..15），原 16「悬浮窗设置」/ 17「窗口设置」两页号从
 * 注册表与导航树删除，**其余页号一律不变**（跨端页号是操作手册契约）。
 * 本文件即合并后的页 13；旧 page16_overlay.js / page17_window.js 已删。
 *
 * [三分区 = 原三页，一项不落]
 *   ① 界面设置   = 原页 13「本地设置」：显示卡（紧凑模式/全屏显示/界面缩放）
 *                   + 数据刷新卡（界面刷新周期 + 趋势刷新间隔）
 *                   + 每页条数卡（历史数据 / 运行日志）。
 *   ② 悬浮窗     = 原页 16「悬浮窗设置」：显示元素卡（显示顶部标题栏/显示
 *                   底部状态栏/显示左侧导航栏）+ 标题栏内容卡（显示时钟/
 *                   显示温湿度）——**已兑现的开关原样保留并继续兑现**。
 *   ③ 窗口与屏幕 = 原页 17「窗口设置」：尺寸卡（窗口大小 + 应用）+ 屏幕
 *                   休眠卡（状态行/自动熄屏/系统挂起/唤醒来源三勾/四个按钮）
 *                   + 信息卡。
 *
 * [趋势刷新间隔 / 历史·日志每页条数也收进本页] 这三项本机偏好**早已落盘**、
 * 但控件散在别处（趋势间隔在页 5/6 趋势卡的「刷新」下拉；每页条数在页 9/10
 * 工具条的「每页 N 条」下拉）⇒ 与参考客户端合并页同口径（pages_other.py
 * 合并后的 LocalSettingsPage._build_interface_section），本页是它们的
 * **总入口**，页内下拉是就地微调：**同一份偏好键**
 * （trendSampleMs / histPageSize / logPageSize），不新建第二份存储。
 *
 * [对齐参考 LocalSettingsPage]（client_py/hsfsrv_client/pages_other.py:4775
 * 合并后的形态）全部旋钮**改完立即生效 + 写本机偏好**（prefs.js 单一事实源，
 * localStorage；对齐 2026-11-10「可以持久化的配置都持久化」口径）。
 *
 * [与参考的三处 web 差异，都是真兑现不是缩水]
 *   · 界面缩放：参考是「下次启动生效」（Qt QSS 建窗时算死）；web 走根
 *     字号，**立即生效**（任务口径「缩放改根字号立即生效」）——提示语
 *     相应改写，不照抄「下次启动生效」骗人；
 *   · 全屏显示：浏览器 Fullscreen API 需要页面内手势授权，重启后无法
 *     自动进全屏——勾选框反映**实际**全屏态（ESC 退出会同步回来），
 *     限制说明走 tooltip（参考显示卡无说明行，2026-11-13 对齐口径）；
 *   · 紧凑模式：prefs.density() = 强制开 或 窗宽 < 600px 自动紧凑
 *     （theme.COMPACT_MAX_W 同值），与参考「不勾=按窗宽自动判」同语义。
 *
 * [屏幕休眠卡：控件集与参考同形，能力位按浏览器实际能力处置]
 *   参考端该卡走本机能力库 hsb_bridge（框架 XPlatformPower）：状态行
 *   「当前: 正常 | 熄屏支持 | 系统挂起不支持」+ 自动熄屏下拉 + 系统挂起
 *   下拉 + 唤醒来源三勾 + 四按钮，并**按能力位逐项置灰**（参考
 *   pages_other.py:6551-6563：无 CAP_SYSTEM_SUSPEND ⇒ 系统挂起置灰；无
 *   CAP_SCREEN_TIMEOUT ⇒ 请求授权置灰；无 CAP_SCREEN_CONTROL ⇒ 立即熄屏/
 *   唤醒置灰）。
 *   浏览器**没有**主动关屏 / 系统挂起 / 平台授权原语，唯一能兑现的是
 *   **Screen Wake Lock API**（navigator.wakeLock.request('screen') = 保持
 *   屏幕常亮；release() = 交还系统按自身策略熄屏）⇒ 照参考的同一套处置，
 *   控件一个不少、语义逐条落到浏览器真能做的事上：
 *     · 状态行同一形状三段（当前 / 熄屏 / 挂起），中段按参考自己的措辞写
 *       「熄屏由系统委托」（参考 pages_other.py:6636 同一能力档位的原话）
 *       —— 浏览器不能主动关屏，**不谎称「熄屏支持」**；
 *     · 「自动熄屏」下拉**真兑现**：档位语义映射成常亮锁策略——「从不」=
 *       页面可见期间始终保持常亮（屏幕不熄）；「N 分钟」= 无操作满 N 分钟
 *       后**释放常亮锁**，允许系统按自身策略熄屏（下次唤醒来源的操作自动
 *       恢复常亮）；
 *     · 「系统挂起」下拉**置灰**（浏览器无挂起原语，同参考缺能力时的处置）；
 *     · 「唤醒来源」三勾**真兑现**：勾选的输入（鼠标/键盘/触摸）算作「有
 *       活动」——重置自动熄屏计时、并可恢复常亮锁；全不勾按参考口径回落
 *       为「全部」（参考 pages_other.py:6694-6700）；
 *     · 「立即熄屏」= 立即释放常亮锁（浏览器不能主动关屏，释放后由系统按
 *       自身策略熄灭）；「唤醒屏幕」= 重新取得常亮锁（屏幕保持常亮）；
 *       「保存策略」= 落盘 + 立即生效（**回读回显**，不拿自己刚写的值当回显）；
 *       「请求授权」**置灰**（浏览器无需平台授权，同参考缺能力时的处置）；
 *     · 浏览器**完全不支持** Screen Wake Lock 时，四个按钮按参考规则整块
 *       置灰并在状态行说明原因——不留「选了不兑现」的死控件。
 *
 * [偏好键] 走 prefs.js 同一份 localStorage（键名与桌面 settings 的
 * K_POWER_SCREEN_OFF_MS / K_POWER_SUSPEND_MS / K_POWER_WAKE_INPUTS 同名），
 * 不新建第二份存储。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* 界面缩放档（参考 LocalSettingsPage.SCALE_CHOICES）。 */
  var SCALE_CHOICES = [100, 110, 125, 150];
  /* 紧凑断点（参考 theme.COMPACT_MAX_W：信息卡文案与 tooltip 用）。 */
  var COMPACT_MAX_W = 600;
  /* 尺寸档（参考 WindowPage.SIZE_CHOICES）。 */
  var SIZE_CHOICES = [[1280, 720], [1024, 600], [800, 480]];
  /* 熄屏/挂起策略档（参考 WindowPage.POWER_TIMEOUTS，pages_other.py:6397）。 */
  var POWER_TIMEOUTS = [
    [0, "从不"], [60000, "1 分钟"], [300000, "5 分钟"],
    [600000, "10 分钟"], [1800000, "30 分钟"],
  ];
  /* 唤醒来源位（参考 hsf_native.INPUT_MOUSE / INPUT_KEYBOARD / INPUT_TOUCH）。 */
  var INPUT_MOUSE = 1, INPUT_KEYBOARD = 2, INPUT_TOUCH = 4, INPUT_ALL = 7;

  /* 本机偏好键（与桌面 settings 的 K_POWER_* 同名键）。 */
  var K_BLANK_MS = "powerScreenOffMs";
  var K_SUSPEND_MS = "powerSuspendMs";
  var K_WAKE_INPUTS = "powerWakeInputs";

  var activeUnmount = null;

  HSF.router.register(13, {
    title: "本地设置",
    topics: [],
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  /* 分区容器（标题 + 两列卡容器）。
   *
   * [为什么分区里还要两列] PC 横屏 ≥1100px 时单列卡会被拉到整屏宽（1600
   * 视口下 ≈1388px），表单右侧大片空白；两列按**实测卡高**贪心平衡
   * （HSF.balanceColumns）⇒ 矮列不会提前结束（2026-11-22 实拍：固定网格
   * 让「数据刷新」下面空出一整块）。
   * 窄屏（<1100px）两列容器走 display:contents 且卡片全部回到左列 ⇒
   * 仍是原来的单列 DOM 顺序，移动端布局零变化。 */
  function section(titleText, cards) {
    var left = HSF.el("div.settings-col");
    var right = HSF.el("div.settings-col");
    var sec = HSF.el("div.setting-section", null, [
      HSF.el("div.setting-section-title", { text: titleText }),
      HSF.el("div.settings-cards", null, [left, right]),
    ]);
    sec._cards = cards;
    sec._cols = [left, right];
    return sec;
  }

  function mountPage(host, ctx) {
    var prefs = HSF.prefs;
    var timers = [];
    var unsubs = [];

    /* ==================================================================
     *                    分区一：界面设置（原页 13）
     * ================================================================== */
    /* ---- 显示卡 ---- */
    var compactCheck = HSF.el("input.check", { type: "checkbox" });
    compactCheck.checked = prefs.forceCompact();
    var compactRow = HSF.el("label.check-row", null, [
      compactCheck,
      HSF.el("span", { text: "紧凑模式（竖屏/小屏）" }),
    ]);
    compactRow.title = "勾选=强制紧凑；不勾=按窗宽自动判（宽 < " +
      COMPACT_MAX_W + "px 自动紧凑）";

    var fsCheck = HSF.el("input.check", { type: "checkbox" });
    fsCheck.checked = !!document.fullscreenElement;   /* 反映实际态，不编造 */
    var fsRow = HSF.el("label.check-row", null, [
      fsCheck,
      HSF.el("span", { text: "全屏显示" }),
    ]);
    /* 说明进 tooltip（参考显示卡只有三行，无说明行——pages_other.py:4791-4821）。 */
    fsRow.title = "浏览器全屏需在页面内点击授权；重启后勾选框如实反映当前状态";

    var scaleSel = HSF.el("select.input");
    scaleSel.setAttribute("aria-label", "界面缩放");
    SCALE_CHOICES.forEach(function (pct) {
      var opt = HSF.el("option", { text: pct + "%" });
      opt.value = String(pct);
      scaleSel.appendChild(opt);
    });
    /* 当前档：存过选存档；没存过选最近的档（参考 findData 同款）。
     * [缺省取 prefs.uiScalePct()] 它已含「没存 ⇒ 缺省 150」（参考端
     * theme.ui_scale()*100 = 150，pages_other.py:4810-4812）。 */
    var savedPct = prefs.uiScalePct();
    var best = SCALE_CHOICES[0];
    SCALE_CHOICES.forEach(function (pct) {
      if (Math.abs(pct - savedPct) < Math.abs(best - savedPct)) best = pct;
    });
    scaleSel.value = String(best);
    scaleSel.title = "改完立即生效（web 版根字号实时缩放）";

    var displayCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "显示" }),
      compactRow,
      fsRow,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "界面缩放" }),
        scaleSel,
      ]),
    ]);

    /* ---- 数据刷新卡 ---- */
    var refreshInput = HSF.el("input.input.num", {
      type: "number", min: "100", max: "2000", step: "100",
      value: String(prefs.refreshMs()),
    });
    refreshInput.title = "主界面的数据重绘节拍（改完立即生效）";
    /* 单位后缀在框内（参考 refresh_spin.setSuffix(" ms")），不放框外小字。 */
    var refreshSuffix = HSF.el("span.spin-suffix", { text: " ms" });
    var refreshWrap = HSF.el("span.spin-unit", null, [refreshInput, refreshSuffix]);

    /* 趋势刷新间隔（参考 pages_other.py 合并页的「趋势刷新间隔」行）：与页 5/6
     * 趋势卡的「刷新」下拉**同一个偏好键**（trendSampleMs）⇒ 本页是总入口，
     * 页内下拉是就地微调。档位取 trendchart.js 的 SAMPLE_CHOICES（单一事实源，
     * trendchart.js 导出 HSF.trendSampleChoices）。 */
    var TREND_CHOICES = HSF.trendSampleChoices || [200, 500, 1000, 2000];
    var trendSel = HSF.el("select.input");
    trendSel.setAttribute("aria-label", "趋势刷新间隔");
    TREND_CHOICES.forEach(function (ms) {
      var opt = HSF.el("option", { text: ms + " ms" });
      opt.value = String(ms);
      trendSel.appendChild(opt);
    });
    var savedTrend = parseInt(prefs.get("trendSampleMs", ""), 10);
    if (!(savedTrend > 0)) savedTrend = TREND_CHOICES[0];
    var tIdx = 0, tBest = Infinity;
    TREND_CHOICES.forEach(function (ms, i) {
      var d = Math.abs(ms - savedTrend);
      if (d < tBest) { tBest = d; tIdx = i; }
    });
    trendSel.selectedIndex = tIdx;
    trendSel.title = "实时趋势图的记录密度（改完立即落盘；下次进变频器/水质页按此节拍）";

    /* 数据刷新卡：界面刷新周期 + 趋势刷新间隔（参考 pages_other.py:4856-4883
     * 同款）。 */
    var refreshCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "数据刷新" }),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "界面刷新周期" }),
        refreshWrap,
      ]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "趋势刷新间隔" }),
        trendSel,
      ]),
    ]);

    /* ---- 每页条数卡（参考 pages_other.py 合并页的「每页条数」卡）----
     * 历史数据 / 运行日志 两行；与页 9/10 工具条的「每页 N 条」下拉**同一份
     * 偏好**（histPageSize / logPageSize）⇒ 本页是总入口，页内下拉是就地微调。
     * 档位取 data_toolbar.js 的 HSF.pageSizeChoices（单一事实源）。 */
    var PAGE_SIZE_CHOICES = HSF.pageSizeChoices || [10, 20, 50];
    function pageSizeSelect(labelText, key, defVal) {
      var sel = HSF.el("select.input");
      sel.setAttribute("aria-label", labelText + "每页条数");
      PAGE_SIZE_CHOICES.forEach(function (n) {
        var opt = HSF.el("option", { text: n + " 条" });
        opt.value = String(n);
        sel.appendChild(opt);
      });
      var v = parseInt(prefs.get(key, ""), 10);
      if (PAGE_SIZE_CHOICES.indexOf(v) < 0) v = defVal;
      sel.value = String(v);
      sel.title = labelText + "页每页行数（下次进该页生效；该页工具条的" +
        "「每页 N 条」下拉共用同一份偏好）";
      sel.addEventListener("change", function () {
        var n = parseInt(sel.value, 10) || defVal;
        prefs.set(key, String(n));
        HSF.statusMessage(labelText + "每页 " + n + " 条（已保存，下次进该页生效）");
      });
      return sel;
    }
    var histSizeSel = pageSizeSelect("历史数据", "histPageSize", 20);
    var logSizeSel = pageSizeSelect("运行日志", "logPageSize", 20);
    var pageSizeCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "每页条数" }),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "历史数据" }), histSizeSel]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "运行日志" }), logSizeSel]),
    ]);

    /* ==================================================================
     *                    分区二：悬浮窗（原页 16）
     * ================================================================== */
    function overlayRow(text, key, tip) {
      var cb = HSF.el("input.check", { type: "checkbox" });
      cb.checked = prefs.flag(key, true);
      var row = HSF.el("label.check-row", null, [cb, HSF.el("span", { text: text })]);
      if (tip) row.title = tip;
      cb.addEventListener("change", function () {
        prefs.setOverlayFlag(key, cb.checked);
        HSF.statusMessage("界面元素已更新（已保存）");
      });
      return row;
    }

    function contentRow(text, key, tip) {
      var cb = HSF.el("input.check", { type: "checkbox" });
      cb.checked = prefs.flag(key, true);
      var row = HSF.el("label.check-row", null, [cb, HSF.el("span", { text: text })]);
      if (tip) row.title = tip;
      cb.addEventListener("change", function () {
        prefs.setFlag(key, cb.checked);
        /* 标题栏可能未装配（理论不会：本页在壳内）；有则即时生效。 */
        if (HSF.titlebar && HSF.titlebar.applyContentPrefs) {
          HSF.titlebar.applyContentPrefs();
        }
        HSF.statusMessage("界面元素已更新（已保存）");
      });
      return row;
    }

    var navOverlayRow = overlayRow("显示左侧导航栏", "showNav");
    var overlayCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "显示元素" }),
      overlayRow("显示顶部标题栏", "showTitleBar"),
      overlayRow("显示底部状态栏", "showStatusBar"),
      navOverlayRow,
    ]);
    var navCheck = navOverlayRow.querySelector("input.check");
    /* 菜单页模式下导航树强制隐藏 ⇒ 本开关置灰（值保留，切回导航树档恢复）。 */
    function syncNavAvailability() {
      var pageMode = prefs.menuButtonStyle() === "page";
      navCheck.disabled = pageMode;
      navOverlayRow.title = pageMode
        ? "菜单页模式下导航树自动隐藏；切回「导航树」后按本开关恢复"
        : "";
    }
    syncNavAvailability();

    var titleContentCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "标题栏内容" }),
      /* [2026-11-22 标题栏新元素同步] 顺序与客户端一致：模式/健康在前
       * （后加进顶栏的元素），时钟/温湿度在后。web 标题栏无二维码徽章
       * （客户端专属）⇒ 不摆二维码开关。 */
      contentRow("显示相机", "showCamera", "标题栏相机徽章（点开悬浮画面卡；再点关闭）"),
      contentRow("显示运行模式", "showTitleMode", "标题栏手动/自动模式钮与自动运行徽标（同一条信息链，一起显隐）"),
      contentRow("显示设备健康", "showTitleHealth", "标题栏「故障N / 离线N」指示（全部正常时本来就不占位）"),
      contentRow("显示时钟", "showTitleClock", "标题栏时钟（C 端同款；web 可隐藏并记忆）"),
      contentRow("显示温湿度", "showTitleTemp", "标题栏电箱温湿度读数（web 可隐藏并记忆）"),
    ]);

    /* ---- 导航方式卡（2026-11-22 用户定案）----
     * ☰ 两档：导航树（既有折叠/抽屉）或菜单页（当前分类入口页 + 返回上级）。
     * 与客户端 LocalSettingsPage「导航方式」同键（settings.K_MENU_BUTTON_STYLE
     * = menuButtonStyle），落同一份本机偏好。 */
    var menuStyleSel = HSF.el("select.input");
    menuStyleSel.setAttribute("aria-label", "左上角菜单按钮行为");
    [["tree", "导航树"],
     ["page", "菜单页"]].forEach(function (pair) {
      var opt = HSF.el("option", { text: pair[1] });
      opt.value = pair[0];
      menuStyleSel.appendChild(opt);
    });
    menuStyleSel.value = prefs.menuButtonStyle();
    menuStyleSel.title = "点左上角 ☰ 的行为：\n" +
      "导航树 = 宽屏收起/展开左侧导航（窄屏开/关抽屉）；\n" +
      "菜单页 = 打开当前分类的入口页（如设备 → 选择子项），" +
      "子菜单页可「← 返回主菜单」。";
    menuStyleSel.addEventListener("change", function () {
      prefs.setMenuButtonStyle(menuStyleSel.value);
      prefs.applyOverlay();              /* 菜单页档 ⇒ 立即隐藏导航树 */
      syncNavAvailability();
      HSF.statusMessage(menuStyleSel.value === "page"
        ? "导航方式已更新（已保存）；导航树已隐藏"
        : "导航方式已更新（已保存）");
    });
    var navStyleCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "导航方式" }),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "左上角 ☰ 按钮" }), menuStyleSel]),
      HSF.el("div.caption", {
        text: "「菜单页」按当前页自动选分类（设备/数据/设置）；" +
              "在子菜单页再点 ☰ 回主菜单。",
      }),
    ]);

    /* ==================================================================
     *                 分区三：窗口与屏幕（原页 17）
     * ================================================================== */
    /* ---- 尺寸卡 ---- */
    var sizeSel = HSF.el("select.input");
    sizeSel.setAttribute("aria-label", "窗口大小");
    SIZE_CHOICES.forEach(function (wh) {
      var opt = HSF.el("option", { text: wh[0] + " × " + wh[1] });
      opt.value = wh[0] + "x" + wh[1];
      sizeSel.appendChild(opt);
    });
    var optFs = HSF.el("option", { text: "全屏" });
    optFs.value = "fullscreen";
    sizeSel.appendChild(optFs);
    /* 上次保存的尺寸对应哪一档（对不上留在第 0 档，参考同款）。 */
    var saved = prefs.winSize();
    var matched = false;
    Array.prototype.forEach.call(sizeSel.options, function (opt, i) {
      if (opt.value === saved) { sizeSel.selectedIndex = i; matched = true; }
    });
    if (!matched && prefs.fullscreen()) sizeSel.value = "fullscreen";

    var btnApply = HSF.el("button.btn", { type: "button", text: "应用" });
    /* [web 限制口径进 tooltip] 浏览器不允许页面自行改变窗口大小——所选尺寸
     * 作为「期望视口」约束应用显示区并居中，本机保存、重新打开沿用。 */
    var applyBaseTitle = "浏览器不允许页面自行改变窗口大小：所选尺寸按「期望视口」" +
      "约束应用显示区（居中显示）并保存在本机，重新打开页面时沿用";
    btnApply.title = applyBaseTitle;
    var sizeBaseTitle = "「全屏」档立即真全屏（浏览器要求页面内点击授权）";
    sizeSel.title = sizeBaseTitle;

    var sizeCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "尺寸" }),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "窗口大小" }), sizeSel]),
      HSF.el("div.form-row", null, [btnApply]),
    ]);

    /* ---- 屏幕休眠卡 ---- */
    /* 浏览器唯一可兑现的屏幕电源能力：Screen Wake Lock（保持常亮）。 */
    var wakeLockSupported = !!(navigator.wakeLock && navigator.wakeLock.request);
    var wakeLock = null;         /* 持有时的 sentinel */
    var manualRelease = false;   /* 「立即熄屏」后：常亮锁保持关闭，直到「唤醒屏幕」 */
    var lastActivity = Date.now();
    /* 生效策略的本地缓存（鼠标移动是热路径 ⇒ 不在事件里读 localStorage）。 */
    var blankMsCache = 0;        /* 当前生效的「自动熄屏」时长（0 = 从不） */
    var wakeInputsCache = INPUT_ALL;   /* 当前生效的唤醒来源位 */
    var lastAcquireFailAt = 0;   /* 上次取常亮锁失败的时刻（失败退避 10s） */

    var powerState = HSF.el("div.caption", { text: "" });

    /* 状态行：与参考同形状三段（当前 / 熄屏 / 挂起）。 */
    function powerStateText() {
      var current = "当前: 正常";       /* 网页版从不主动关屏 ⇒ 屏幕状态恒「正常」 */
      var blank = wakeLockSupported ? "熄屏由系统委托" : "熄屏不支持";
      var suspend = "系统挂起不支持";   /* 浏览器无系统挂起原语 */
      return current + "  |  " + blank + "  |  " + suspend;
    }

    function refreshPowerState() {
      var t = powerStateText();
      if (powerState.textContent !== t) powerState.textContent = t;
      var tip =
        "网页版的真实屏幕电源能力只有 **Screen Wake Lock（保持屏幕常亮）**：\n" +
        "· 当前：网页版从不主动关屏，屏幕状态恒为「正常」；\n" +
        "· 熄屏由系统委托：浏览器不能主动关屏，只能在需要时保持常亮" +
        "（持有常亮锁时屏幕不熄；释放后按系统自身策略熄灭）；\n" +
        "· 系统挂起不支持：浏览器没有系统挂起原语；\n" +
        "· 常亮锁当前：" + (wakeLock ? "已持有（屏幕保持常亮）" : "未持有");
      if (powerState.getAttribute("title") !== tip) {
        powerState.setAttribute("title", tip);
      }
    }

    /* --- 自动熄屏下拉（真兑现：常亮锁策略） --- */
    function powerCombo(ariaLabel, tip) {
      var sel = HSF.el("select.input");
      sel.setAttribute("aria-label", ariaLabel);
      POWER_TIMEOUTS.forEach(function (pair) {
        var opt = HSF.el("option", { text: pair[1] });
        opt.value = String(pair[0]);
        sel.appendChild(opt);
      });
      sel.title = tip;
      return sel;
    }

    var blankSel = powerCombo("自动熄屏",
      "网页版没有「主动关屏」原语，本项的真实语义是**常亮锁策略**：\n" +
      "· 从不 = 页面可见期间始终保持常亮（屏幕不熄）；\n" +
      "· N 分钟 = 无操作满 N 分钟后释放常亮锁，允许系统按自身策略熄屏" +
      "（下次有唤醒来源的操作时自动恢复常亮）。\n改完点「保存策略」生效。");

    var suspendSel = powerCombo("系统挂起",
      "浏览器没有系统挂起原语 ⇒ 整项置灰（参考同款：无系统挂起能力时该项" +
      "置灰，参考 pages_other.py:6557-6559）；值恒为「从不」。");
    suspendSel.disabled = true;

    /* --- 唤醒来源三勾（真兑现：哪些输入算「有活动」） --- */
    function wakeCheck(text, bit) {
      var cb = HSF.el("input.check", { type: "checkbox" });
      var row = HSF.el("label.check-row", null,
                       [cb, HSF.el("span", { text: text })]);
      /* .check-row 默认 10px 下边距（整行排布用），同行排布时归零以对齐标签。 */
      row.style.marginBottom = "0";
      row.title = "勾选 = 该输入算作「有活动」：重置自动熄屏计时，" +
                   "并可把已释放的常亮锁重新取回；全不勾按参考口径回落为「全部」。";
      return { cb: cb, bit: bit, row: row };
    }
    var wakeMouse = wakeCheck("鼠标", INPUT_MOUSE);
    var wakeKey = wakeCheck("键盘", INPUT_KEYBOARD);
    var wakeTouch = wakeCheck("触摸", INPUT_TOUCH);

    /* --- 四个动作按钮 --- */
    var btnOff = HSF.el("button.btn", { type: "button", text: "立即熄屏" });
    var btnOn = HSF.el("button.btn", { type: "button", text: "唤醒屏幕" });
    var btnSave = HSF.el("button.btn", { type: "button", text: "保存策略" });
    var btnPerm = HSF.el("button.btn", { type: "button", text: "请求授权" });

    btnOff.title = "浏览器不能主动关屏 ⇒ 本按钮的真实动作是**立即释放常亮锁**，" +
                   "屏幕随后按系统自身策略熄灭。";
    btnOn.title = "浏览器不能主动点亮已熄的屏幕 ⇒ 本按钮的真实动作是" +
                  "**重新取得常亮锁**（屏幕保持常亮）。";
    btnSave.title = "把当前的熄屏时长 / 唤醒来源落盘并立即生效（回读回显）。";
    btnPerm.title = "浏览器无需平台授权（Screen Wake Lock 由页面自行申请，" +
                    "失败会在状态行说明原因）⇒ 整项置灰（参考同款：无委托熄屏" +
                    "授权能力时该项置灰，参考 pages_other.py:6562-6564）。";
    btnPerm.disabled = true;

    if (!wakeLockSupported) {
      /* 浏览器完全没有 Screen Wake Lock ⇒ 四个按钮按参考规则整块置灰，
         不留「点了没反应」的死控件。 */
      btnOff.disabled = true;
      btnOn.disabled = true;
      btnSave.disabled = true;
      btnOff.title = "本浏览器不支持 Screen Wake Lock API（Edge/Chrome 支持）" +
                     "⇒ 无屏幕电源能力可操作，整块置灰（同参考缺能力时的处置）。";
      btnOn.title = btnOff.title;
      btnSave.title = btnOff.title;
    }

    var powerCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "屏幕休眠" }),
      powerState,
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "自动熄屏" }), blankSel]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "系统挂起" }), suspendSel]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "唤醒来源" }),
        wakeMouse.row, wakeKey.row, wakeTouch.row]),
      HSF.el("div.form-row", null, [btnOff, btnOn, btnSave, btnPerm]),
    ]);

    /* ---- 信息卡 ---- */
    var infoCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "信息" }),
      HSF.el("div.caption", {
        text: "窄窗口（宽 < " + COMPACT_MAX_W + "px）时左侧导航会自动收起，" +
              "由「主菜单」页承载导航。",
      }),
    ]);

    /* ==================================================================
     *                          组装 + 接线
     * ================================================================== */
    var page = HSF.el("div.settings-page", null, [
      HSF.el("div.settings-head", null, [
        HSF.el("span.page-head-title", { text: "本地设置" }),
        HSF.el("span.page-head-sub.caption",
               { text: "仅本机生效（本机保存，重启沿用）" }),
      ]),
      section("界面设置", [displayCard, refreshCard, pageSizeCard]),
      section("悬浮窗", [overlayCard, titleContentCard, navStyleCard]),
      section("窗口与屏幕", [sizeCard, powerCard, infoCard]),
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);

    /* ---- 宽屏分栏（≥1100px 才动；窄屏回到原单列顺序） ----
     * [重排时机] 首次挂载后量一次（getBoundingClientRect 会强制同步布局）；
     * 窗口拖宽/跨断点 200ms 防抖后按新列宽重量（列宽变化会让文本换行，
     * 卡高随之变）。卡片内容是静态的（不随异步数据长高），故不需要
     * ResizeObserver。 */
    var sections = Array.prototype.slice.call(
      page.querySelectorAll(".setting-section"));
    var wideMq = window.matchMedia("(min-width: 1100px)");
    var lastColW = 0;
    var colResizeTimer = 0;

    function layoutSections(force) {
      var wide = wideMq.matches;
      var colW = 0;
      if (wide) {
        var first = sections[0] && sections[0]._cols[0];
        colW = first ? first.getBoundingClientRect().width : 0;
        /* 宽度没明显变（<60px）且不是强制 ⇒ 分栏结果不会变，省一次重排。 */
        if (!force && Math.abs(colW - lastColW) < 60) return;
        lastColW = colW;
      } else {
        lastColW = 0;
      }
      sections.forEach(function (sec) {
        var cards = sec._cards || [];
        var cols = sec._cols;
        if (!wide) {
          /* 窄屏：卡片按原顺序全回左列（两列都是 display:contents，
           * 右列留空）⇒ DOM/视觉顺序与改造前一致。 */
          cards.forEach(function (c) { cols[0].appendChild(c); });
          return;
        }
        HSF.balanceColumns(cards, cols[0], cols[1]);
      });
    }

    function onColResize() {
      clearTimeout(colResizeTimer);
      colResizeTimer = setTimeout(function () { layoutSections(false); }, 200);
    }

    window.addEventListener("resize", onColResize);
    layoutSections(true);

    /* ---- 旋钮接线（初值就位之后，参考「先接线会被当成用户改动」教训） ---- */
    compactCheck.addEventListener("change", function () {
      prefs.setForceCompact(compactCheck.checked);
      HSF.statusMessage("紧凑模式：" +
        (compactCheck.checked ? "强制开" : "按窗宽自动") + "（已保存）");
    });

    fsCheck.addEventListener("change", function () {
      var want = fsCheck.checked;
      if (want) {
        document.documentElement.requestFullscreen().then(function () {
          prefs.setFullscreen(true);
          HSF.statusMessage("全屏：开（已保存）");
        }).catch(function (err) {
          fsCheck.checked = false;          /* 浏览器拒了：如实回弹 */
          HSF.statusMessage("全屏：浏览器拒绝了全屏请求（" +
            (err && err.message ? err.message : err) + "）");
        });
      } else {
        prefs.setFullscreen(false);
        if (document.fullscreenElement) {
          document.exitFullscreen().catch(function () { /* 已退出即可 */ });
        }
        HSF.statusMessage("全屏：关（已保存）");
      }
    });

    /* 用户按 ESC 退出全屏 → 勾选框跟实际态走（不留在假勾上）。 */
    function onFsChange() {
      var on = !!document.fullscreenElement;
      if (fsCheck.checked !== on) fsCheck.checked = on;
      prefs.setFullscreen(on);
    }
    document.addEventListener("fullscreenchange", onFsChange);

    scaleSel.addEventListener("change", function () {
      var pct = parseInt(scaleSel.value, 10) || SCALE_CHOICES[0];
      prefs.setScale(pct);
      HSF.statusMessage("界面缩放 " + pct + "%（已保存，立即生效）");
    });

    trendSel.addEventListener("change", function () {
      var ms = parseInt(trendSel.value, 10) || TREND_CHOICES[0];
      prefs.set("trendSampleMs", String(ms));
      HSF.statusMessage("趋势刷新间隔 " + ms + " ms（已保存）");
    });

    refreshInput.addEventListener("change", function () {
      var ms = parseInt(refreshInput.value, 10);
      if (!(ms >= 100 && ms <= 2000)) {
        ms = prefs.refreshMs();           /* 越界回生效值（不静默改语义） */
        refreshInput.value = String(ms);
      }
      prefs.setRefreshMs(ms);
      HSF.statusMessage("界面刷新周期 " + ms + " ms（已保存）");
    });

    /* ================= 屏幕休眠：策略读回 / 落盘 ================= */
    function savedBlankMs() {
      var v = parseInt(prefs.get(K_BLANK_MS, ""), 10);
      return (v >= 0) ? v : 0;
    }
    function savedWakeInputs() {
      var v = parseInt(prefs.get(K_WAKE_INPUTS, ""), 10);
      /* 参考同款：全关 = 谁也唤不醒 ⇒ 回落「全部」。 */
      return v > 0 ? v : INPUT_ALL;
    }
    function setComboMs(sel, ms) {
      var found = false;
      Array.prototype.forEach.call(sel.options, function (opt) {
        if (parseInt(opt.value, 10) === ms) found = true;
      });
      /* 对不上档位就落「从不」，不静默改语义（参考 _power_set_combo 同款）。 */
      sel.value = String(POWER_TIMEOUTS[0][0]);
      if (found) sel.value = String(ms);
    }
    function comboText(sel) {
      var opt = sel.options[sel.selectedIndex];
      return opt ? opt.text : "";
    }
    function loadPowerPolicy() {
      setComboMs(blankSel, savedBlankMs());
      setComboMs(suspendSel, 0);
      var wake = savedWakeInputs();
      wakeMouse.cb.checked = !!(wake & INPUT_MOUSE);
      wakeKey.cb.checked = !!(wake & INPUT_KEYBOARD);
      wakeTouch.cb.checked = !!(wake & INPUT_TOUCH);
      blankMsCache = savedBlankMs();
      wakeInputsCache = wake;
    }

    function savePowerPolicy() {
      var blank = parseInt(blankSel.value, 10) || 0;
      var wake = 0;
      if (wakeMouse.cb.checked) wake |= INPUT_MOUSE;
      if (wakeKey.cb.checked) wake |= INPUT_KEYBOARD;
      if (wakeTouch.cb.checked) wake |= INPUT_TOUCH;
      if (wake === 0) {
        /* 参考同款：全关 = 熄屏后谁也唤不醒 ⇒ 回落「全部」并回显。 */
        wake = INPUT_ALL;
        wakeMouse.cb.checked = true;
        wakeKey.cb.checked = true;
        wakeTouch.cb.checked = true;
      }
      prefs.set(K_BLANK_MS, String(blank));
      prefs.set(K_SUSPEND_MS, "0");     /* 浏览器无系统挂起 ⇒ 恒 0 */
      prefs.set(K_WAKE_INPUTS, String(wake));
      /* 回读回显：以读回值为准（不拿自己刚传的值当回显）。 */
      loadPowerPolicy();
      /* 改了策略即视为解除「立即熄屏」的锁定（参考：策略一变，平台侧按新
         策略接管）。 */
      manualRelease = false;
      applyPowerPolicy();
      HSF.statusMessage("屏幕休眠策略已保存（熄屏 " + comboText(blankSel) +
        " / 挂起 " + comboText(suspendSel) + "）；" + powerHoldText());
    }

    /* ================= 屏幕休眠：常亮锁（Screen Wake Lock） ================= */
    /* 取常亮锁。返回 {ok, err}：失败时把浏览器的真实原因带出去（状态行
       如实报，不编造成功）。 */
    function acquireWakeLock() {
      if (!wakeLockSupported) {
        return Promise.resolve({ ok: false, err: "本浏览器不支持 Screen Wake Lock API" });
      }
      if (wakeLock) return Promise.resolve({ ok: true, err: "" });
      var p;
      try {
        p = navigator.wakeLock.request("screen");
      } catch (err) {
        return Promise.resolve({ ok: false, err: errText(err) });
      }
      return p.then(function (sentinel) {
        wakeLock = sentinel;
        lastAcquireFailAt = 0;
        sentinel.addEventListener("release", function () {
          wakeLock = null;
          refreshPowerState();
        });
        refreshPowerState();
        return { ok: true, err: "" };
      }).catch(function (err) {
        wakeLock = null;
        refreshPowerState();
        return { ok: false, err: errText(err) };
      });
    }
    function errText(err) {
      if (!err) return "未知原因";
      var name = err.name ? err.name : "";
      var msg = err.message ? err.message : String(err);
      return name ? (name + ": " + msg) : msg;
    }
    function releaseWakeLock() {
      if (!wakeLock) return false;
      var sentinel = wakeLock;
      wakeLock = null;
      try { sentinel.release(); } catch (err) { /* 已释放即可 */ }
      refreshPowerState();
      return true;
    }
    /* 此刻该不该持有常亮锁：「从不」= 始终持有；「N 分钟」= 无操作未满
       N 分钟时持有；「立即熄屏」后由「唤醒屏幕」/「保存策略」解除。 */
    function shouldHold() {
      if (!wakeLockSupported) return false;
      if (manualRelease) return false;
      if (blankMsCache <= 0) return true;
      return (Date.now() - lastActivity) < blankMsCache;
    }
    function applyPowerPolicy() {
      var want = shouldHold();
      if (want && !wakeLock && document.visibilityState === "visible") {
        /* 失败后 10s 内不重试（页面不可见/未获焦时每秒重试只会刷失败）。 */
        if (Date.now() - lastAcquireFailAt < 10000) return;
        acquireWakeLock().then(function (r) {
          if (!r.ok) lastAcquireFailAt = Date.now();
        });
      } else if (!want && wakeLock) {
        releaseWakeLock();
      }
    }
    function powerHoldText() {
      if (!wakeLockSupported) {
        return "本浏览器不支持 Screen Wake Lock API，无法保持常亮";
      }
      return wakeLock ? "常亮锁已持有（屏幕保持常亮）" : "常亮锁未持有";
    }

    /* 唤醒来源：勾选的输入算「有活动」——重置自动熄屏计时，并可把因超时
       释放的常亮锁重新取回（「立即熄屏」是明确动作，只由「唤醒屏幕」/
       「保存策略」解除，否则鼠标一动就又亮了）。 */
    function markActivity(bit) {
      if (!(wakeInputsCache & bit)) return;   /* 未勾的来源不算活动 */
      lastActivity = Date.now();
      if (!manualRelease) applyPowerPolicy();
    }
    function onMouseMove() { markActivity(INPUT_MOUSE); }
    function onKeyDown() { markActivity(INPUT_KEYBOARD); }
    function onTouchStart() { markActivity(INPUT_TOUCH); }
    function onVisibility() {
      if (document.visibilityState === "visible") applyPowerPolicy();
    }

    /* --- 按钮接线 --- */
    btnOff.addEventListener("click", function () {
      if (!wakeLockSupported) {
        HSF.statusMessage("本浏览器不支持 Screen Wake Lock API，无法操作屏幕电源");
        return;
      }
      manualRelease = true;
      var had = releaseWakeLock();
      HSF.statusMessage(had
        ? "已释放常亮锁（浏览器不能主动关屏：屏幕随后按系统自身策略熄灭）"
        : "当前没有持有常亮锁（屏幕按系统自身策略显示）");
    });

    btnOn.addEventListener("click", function () {
      if (!wakeLockSupported) {
        HSF.statusMessage("本浏览器不支持 Screen Wake Lock API（Edge/Chrome 支持），无法保持常亮");
        return;
      }
      manualRelease = false;
      lastActivity = Date.now();
      acquireWakeLock().then(function (r) {
        HSF.statusMessage(r.ok
          ? "常亮已开启（屏幕保持常亮；「自动熄屏」超时后自动释放）"
          : "常亮请求被浏览器拒绝（" + r.err +
            "）——页面需可见且已获得焦点，请切到该窗口再点一次");
        refreshPowerState();
      });
    });

    btnSave.addEventListener("click", function () {
      if (!wakeLockSupported) {
        HSF.statusMessage("本浏览器不支持 Screen Wake Lock API，策略无平台侧可下发");
        return;
      }
      savePowerPolicy();
    });

    /* ================= 视口提示（挂 tooltip，不占卡片正文） ================= */
    function viewportSentence() {
      var vw = window.innerWidth, vh = window.innerHeight;
      var applied = prefs.winSize();
      /* 视口尺寸未就绪（嵌入/隐藏时 innerWidth 可能为 0）⇒ 显 "--"，
         不把「0 × 0」当成真视口报出去。 */
      var txt = "当前浏览器视口：" +
        (vw > 0 && vh > 0 ? (vw + " × " + vh) : "--");
      if (applied) {
        txt += "；应用显示区已按期望视口 " + applied.replace("x", " × ") +
               " 约束并居中显示";
      } else {
        txt += "；应用显示区铺满视口";
      }
      return txt;
    }

    function setTitle(node, text) {
      if (node.getAttribute("title") !== text) node.setAttribute("title", text);
    }

    function refreshViewportText() {
      var s = viewportSentence();
      setTitle(sizeSel, sizeBaseTitle + "\n" + s);
      setTitle(btnApply, applyBaseTitle + "\n" + s);
    }
    refreshViewportText();
    timers.push(setInterval(refreshViewportText, 1000));

    /* ================= 尺寸「应用」 ================= */
    btnApply.addEventListener("click", function () {
      var v = sizeSel.value;
      if (v === "fullscreen") {
        document.documentElement.requestFullscreen().then(function () {
          prefs.setFullscreen(true);
          prefs.setWinSize(0, 0);
          prefs.applyViewport();          /* 全屏时解除尺寸约束 */
          refreshViewportText();
          HSF.statusMessage("窗口尺寸已应用（全屏，已保存）；" + viewportSentence());
        }).catch(function (err) {
          HSF.statusMessage("全屏请求被浏览器拒绝（需页面内点击授权）：" +
            (err && err.message ? err.message : err));
        });
        return;
      }
      var m = /^(\d+)x(\d+)$/.exec(v) || [null, "1280", "720"];
      var w = parseInt(m[1], 10), h = parseInt(m[2], 10);
      prefs.setFullscreen(false);
      prefs.setWinSize(w, h);
      prefs.applyViewport();              /* 立即生效（下次打开沿用） */
      if (document.fullscreenElement) {
        document.exitFullscreen().catch(function () { /* 已退出即可 */ });
      }
      refreshViewportText();
      HSF.statusMessage("窗口尺寸已应用（" + w + "×" + h + "，已保存）；" +
        viewportSentence());
    });

    /* ================= 初始化 ================= */
    loadPowerPolicy();
    refreshPowerState();
    if (wakeLockSupported) {
      /* 进页即按已存策略取常亮锁（失败只在状态行说明，不静默）。 */
      applyPowerPolicy();
    } else {
      HSF.statusMessage("屏幕休眠：本浏览器不支持 Screen Wake Lock API，" +
        "仅显示策略（不能保持常亮）");
    }
    timers.push(setInterval(applyPowerPolicy, 1000));
    document.addEventListener("mousemove", onMouseMove);
    document.addEventListener("keydown", onKeyDown);
    document.addEventListener("touchstart", onTouchStart);
    document.addEventListener("visibilitychange", onVisibility);

    void ctx;

    activeUnmount = function () {
      window.removeEventListener("resize", onColResize);
      clearTimeout(colResizeTimer);
      document.removeEventListener("fullscreenchange", onFsChange);
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
      document.removeEventListener("mousemove", onMouseMove);
      document.removeEventListener("keydown", onKeyDown);
      document.removeEventListener("touchstart", onTouchStart);
      document.removeEventListener("visibilitychange", onVisibility);
      releaseWakeLock();      /* 离页交还常亮锁（页面不该在离页后还压着屏幕） */
    };
  }
})();
