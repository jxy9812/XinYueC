/* -*- coding: utf-8 -*-
 * 偏好落盘（localStorage）+ 全局缩放/紧凑档。
 *
 * [对齐参考客户端] 参考客户端把本机设置统一走一个模块
 * （client_py/hsfsrv_client/settings.py，INI 存程序目录），界面缩放
 * uiScalePct / 紧凑 forceCompact 等都从它读。web 版同一口径：
 *   · 所有偏好集中在本模块（key 前缀 `hsfweb.`），页面不许各写一份；
 *   · uiScalePct：根字号 = 14px × pct/150（**全局生效**，theme.css 的正文
 *     族全用 rem），缺省 150（参考端 ui_scale 缺省 1.5），夹在 100~200；
 *     固定 px 的标题类（卡标题/页头标题/数值读数）不随缩放变化——与参考端
 *     「固定 px 标题 + 缩放正文」两套因子同口径；
 *   · forceCompact：强制紧凑档；不勾 = 按窗宽自动判（宽 <
 *     COMPACT_MAX_W(600px) 自动紧凑，settings.py 键名同源）——生效档
 *     每次读时推导，窗口 resize 时由模块级监听重刷 body[data-density]；
 *   · fullscreen / refreshMs / winSize / showTitleBar / showStatusBar /
 *     showNav / showTitleClock / showTitleTemp：**页 13「本地设置」**（2026-11-13
 *     三页合并：原 16 悬浮窗设置 + 17 窗口设置已并入本页三个分区）的本机
 *     旋钮（settings.py 同名键），由该页负责接线；
 *   · theme："light"|"dark"（可选；缺省/无键=亮色，参考客户端缺省口径，
 *     applyAll 写 body[data-theme] 供 theme.css 备选档生效）；
 *   · login：{user, remember}——登录视图的「记住服务器地址与用户名」。
 *     （navFold 已随导航折叠增强一起撤除，见 navbar.js 头注释。）
 *
 * [为什么不是注册表/cookie] 与参考客户端「存程序目录、不用注册表」
 * 同哲学：localStorage 随浏览器配置走、可被站点清理，不产生系统级残留。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var PREFIX = "hsfweb.";
  /* 界面缩放档（参考 LocalSettingsPage.SCALE_CHOICES = 100/110/125/150；
   * 参考端缺省 = theme.ui_scale()*100 = 150 ⇒ web 缺省同口径 150）。 */
  var SCALE_MIN = 100, SCALE_MAX = 200, SCALE_DEF = 150;
  /* 根字号基准（theme.css :root font-size 同值）：14px 根 = 参考端
   * **1.5 档实寸**（app 字体 15pt=20px、caption 18px 都按这档标定）。 */
  var ROOT_FONT_PX = 14;
  /* 缩放百分比基准：参考端 ui_scale 缺省 1.5 ⇒ 百分比 150 时根字号 = 14px。
   * [为什么不是 ×pct/100] 参考端有两套字号因子：**固定 px 的标题类**
   * （卡标题 19px / 页头标题 26px / 数值读数 40px…）与**随缩放走的正文族**
   * （app 字体、caption_label_qss=font_px(12)、DO 按钮=fz12）。web 侧
   * 固定 px 那族写在各自规则里（不随根字号变），正文族走 rem ⇒ 根字号
   * 必须按 pct/150 换算，才能让「界面缩放 150%」= 参考端 1.5 档。 */
  var SCALE_BASE_PCT = 150;
  /* 紧凑断点（client theme.COMPACT_MAX_W 同值，theme.py:136）。 */
  var COMPACT_MAX_W = 600;
  /* 界面刷新周期范围（参考 LocalSettingsPage spin range 100..2000）。 */
  var REFRESH_MIN = 100, REFRESH_MAX = 2000, REFRESH_DEF = 200;

  function readRaw(key) {
    try {
      return window.localStorage.getItem(PREFIX + key);
    } catch (err) {
      return null;                /* 隐私模式禁 localStorage：退化为不记忆 */
    }
  }

  function writeRaw(key, value) {
    try {
      window.localStorage.setItem(PREFIX + key, value);
      return true;
    } catch (err) {
      return false;
    }
  }

  var prefs = {
    /** 读字符串偏好（无值返回 def）。 */
    get: function (key, def) {
      var v = readRaw(key);
      return v === null ? def : v;
    },

    /** 读 JSON 偏好（损坏按无值处理，不抛错）。 */
    getJson: function (key, def) {
      var v = readRaw(key);
      if (v === null) return def;
      try {
        return JSON.parse(v);
      } catch (err) {
        return def;
      }
    },

    /** 写偏好（JSON 序列化对象/数组）。 */
    set: function (key, value) {
      if (value === undefined || value === null) {
        try { window.localStorage.removeItem(PREFIX + key); } catch (e) { /* 忽略 */ }
        return;
      }
      var text = (typeof value === "string") ? value : JSON.stringify(value);
      writeRaw(key, text);
    },

    /** 界面缩放（百分比整数，100..200；非法回缺省 150）。 */
    uiScalePct: function () {
      var v = parseInt(this.get("uiScalePct", ""), 10);
      if (!(v >= SCALE_MIN && v <= SCALE_MAX)) v = SCALE_DEF;
      return v;
    },

    /**
     * 旧档迁移：老版本 web 的「界面缩放」缺省是 100，且根字号语义是
     * 「14px × pct/100」（100 ⇒ 14px）。新语义是「14px × pct/150」
     * （150 ⇒ 14px）——老用户存下的 100 在新语义下会变成 9.3px 的迷你字。
     * 这里把「恰好是 100 的旧值」一次性改写成 150（老缺省值 ⇒ 新缺省值，
     * 观感不变），并打标记只迁一次。
     */
    _migrateScale: function () {
      if (this.get("uiScalePct.v2", "") === "1") return;
      var raw = this.get("uiScalePct", "");
      if (raw === "100") this.set("uiScalePct", String(SCALE_DEF));
      this.set("uiScalePct.v2", "1");
    },

    /** 应用缩放：根字号 = 14px × pct/150（见 SCALE_BASE_PCT 的口径说明）。 */
    applyScale: function () {
      var pct = this.uiScalePct();
      var px = Math.round(ROOT_FONT_PX * pct / SCALE_BASE_PCT * 10) / 10;
      document.documentElement.style.fontSize = px + "px";
    },

    setScale: function (pct) {
      pct = Math.max(SCALE_MIN, Math.min(SCALE_MAX, Math.round(pct)));
      this.set("uiScalePct", String(pct));
      this.applyScale();
    },

    /* ---- 旗标类偏好（"1"/"0" 存取；缺省 def） ---- */
    flag: function (key, def) {
      var v = readRaw(key);
      return v === null ? !!def : v === "1";
    },

    setFlag: function (key, on) {
      writeRaw(key, on ? "1" : "0");
    },

    /** 强制紧凑档（本地设置「紧凑模式」勾选；settings.K_FORCE_COMPACT）。 */
    forceCompact: function () {
      return this.flag("forceCompact", false);
    },

    setForceCompact: function (on) {
      this.setFlag("forceCompact", !!on);
      this.applyDensity();
    },

    /**
     * 生效密度档：'normal' | 'compact'。强制开**或**窗宽 < 600px 都算
     * 紧凑（参考 MainWindow.set_compact_override 的「强制/自动」两态）。
     */
    density: function () {
      var compact = this.forceCompact() ||
        window.innerWidth < COMPACT_MAX_W;
      return compact ? "compact" : "normal";
    },

    applyDensity: function () {
      document.body.setAttribute("data-density", this.density());
    },

    /** 兼容旧调用（外壳阶段签名）：本页语义下 compact==强制紧凑。 */
    setDensity: function (mode) {
      this.setForceCompact(mode === "compact");
    },

    /**
     * 左上角 ☰ 的导航方式（本地设置「导航方式」卡；参考端
     * settings.K_MENU_BUTTON_STYLE 同名键）：
     *   · 'tree'（缺省）= 宽屏收起/展开左侧导航树；
     *   · 'page' = 打开当前页所属分类的菜单页（如设备 → 选择子项）；
     *     菜单页再点 ☰ 回主菜单；子菜单页有「← 返回主菜单」。
     * 非法值回 'tree'（fail-safe，不静默变成第三态）。
     */
    menuButtonStyle: function () {
      var v = this.get("menuButtonStyle", "tree");
      return v === "page" ? "page" : "tree";
    },

    setMenuButtonStyle: function (style) {
      this.set("menuButtonStyle", style === "page" ? "page" : "tree");
    },

    /** 全屏偏好（Fullscreen API 的记忆；浏览器重启后需页面内再点一次）。 */
    fullscreen: function () {
      return this.flag("fullscreen", false);
    },

    setFullscreen: function (on) {
      this.setFlag("fullscreen", !!on);
    },

    /** 界面刷新周期 ms（100..2000，越界回缺省 200；settings.refresh_ms）。 */
    refreshMs: function () {
      var v = parseInt(this.get("refreshMs", ""), 10);
      if (!(v >= REFRESH_MIN && v <= REFRESH_MAX)) v = REFRESH_DEF;
      return v;
    },

    setRefreshMs: function (ms) {
      ms = Math.max(REFRESH_MIN, Math.min(REFRESH_MAX, Math.round(Number(ms) || 0)));
      this.set("refreshMs", String(ms));
    },

    /** 期望窗口尺寸 "宽x高"（页 13·窗口与屏幕分区的尺寸记忆；全屏时清空 = ""）。 */
    winSize: function () {
      return this.get("winSize", "");
    },

    setWinSize: function (w, h) {
      this.set("winSize", w > 0 ? (w + "x" + h) : "");
    },

    /**
     * 期望视口（页 13·窗口与屏幕分区的 web 等效，**真兑现**）。
     *
     * [为什么不能直接改窗口大小] 浏览器**不允许页面自行改变窗口尺寸**
     * （window.resizeTo 对非脚本开的窗口一律被拒）。桌面客户端那边
     * 「写本机设置 → 下次启动按它开窗」是原生能力；网页这边没有等价
     * 原语 ⇒ 落成**应用显示区尺寸**：按所选尺寸约束外壳并居中显示
     * （theme.css 的 body.viewport-fixed），本机保存、重新打开沿用
     * （本函数由 applyAll 在启动时调一次）。
     * 空值 = 不约束（缺省：应用铺满浏览器视口）。
     */
    applyViewport: function () {
      var b = document.body;
      var m = /^(\d+)x(\d+)$/.exec(this.winSize());
      if (!m) {
        b.classList.remove("viewport-fixed");
        b.style.removeProperty("--app-w");
        b.style.removeProperty("--app-h");
        return;
      }
      b.style.setProperty("--app-w", m[1] + "px");
      b.style.setProperty("--app-h", m[2] + "px");
      b.classList.add("viewport-fixed");
    },

    /* ---- chrome 显隐（页 13·悬浮窗分区；settings.K_SHOW_* 同名键） ---- */
    showTitleBar: function () { return this.flag("showTitleBar", true); },
    showStatusBar: function () { return this.flag("showStatusBar", true); },
    showNav: function () { return this.flag("showNav", true); },

    /** 写一个显隐旗标并立即作用到壳（与启动还原同一份 applyOverlay）。 */
    setOverlayFlag: function (key, on) {
      this.setFlag(key, !!on);
      this.applyOverlay();
    },

    /**
     * 按旗标显隐标题栏/状态栏/导航（对齐参考 pages_other.apply_overlay：
     * 启动路径与本页勾选回调**同一套**判定）。实现走 body.hide-* 类 +
     * CSS 变量归零（theme.css「chrome 显隐」段），网格随之收行/收列。
     */
    applyOverlay: function () {
      var b = document.body;
      var pageMode = this.menuButtonStyle() === "page";
      b.classList.toggle("hide-title", !this.showTitleBar());
      b.classList.toggle("hide-status", !this.showStatusBar());
      /* [2026-11-22 用户定案「切到菜单页时隐藏导航树」] 菜单页模式只收
       * 左侧导航树，**保留 ☰**（它是入口）；不用 hide-nav —— 那条在
       * ≤520px 会把 ☰ 一起藏掉（用户实报「导航图标没了」）。「显示左侧
       * 导航栏」开关值原样保留，切回导航树档即按它恢复。 */
      b.classList.toggle("nav-page-mode", pageMode);
      b.classList.toggle("hide-nav", !pageMode && !this.showNav());
    },

    /* ---- 标题栏内容显隐（页 13·悬浮窗分区·标题栏内容；web 可隐藏并持久化） ---- */
    showTitleClock: function () { return this.flag("showTitleClock", true); },
    showTitleTemp: function () { return this.flag("showTitleTemp", true); },
    /* [2026-11-22 标题栏新元素同步] 运行模式钮（含自动徽标）/设备健康。
     * web 标题栏没有二维码徽章（客户端专属）⇒ 无 showTitleQr。 */
    showTitleMode: function () { return this.flag("showTitleMode", true); },
    showTitleHealth: function () { return this.flag("showTitleHealth", true); },
    /** [2026-11-22] 标题栏相机徽章显隐（settings.K_SHOW_CAMERA 同名键）。 */
    showCamera: function () { return this.flag("showCamera", true); },

    /** 登录记忆：{user, remember}（口令绝不落盘——与参考客户端同口径）。 */
    loginMemo: function () {
      var memo = this.getJson("login", {}) || {};
      return {
        user: typeof memo.user === "string" ? memo.user : "",
        remember: !!memo.remember,
      };
    },

    saveLoginMemo: function (user, remember) {
      if (!remember) {
        this.set("login", null);          /* 不记 = 清掉旧记忆 */
        return;
      }
      this.set("login", { user: String(user || ""), remember: true });
    },

    /* ---- 主题备选档（无 UI 旋钮：参考客户端没有皮肤开关，缺省亮色；
     * 深色档经 localStorage 键 "theme"="dark" 手动开启，供调机/演示用） ---- */
    applyTheme: function () {
      var t = (this.get("theme", "light") || "light").toLowerCase();
      if (t === "dark") {
        document.body.setAttribute("data-theme", "dark");
      } else {
        document.body.removeAttribute("data-theme");
      }
    },

    /** 启动时应用一次全局偏好（app.js 装配前调）。 */
    applyAll: function () {
      this._migrateScale();
      this.applyScale();
      this.applyDensity();
      this.applyTheme();
      this.applyViewport();      /* 期望视口（页 13·窗口与屏幕分区的尺寸记忆）沿用 */
    },
  };

  /* 窗宽变化 → 自动紧凑档重判（「不勾=按窗宽自动判」的那一半）。 */
  window.addEventListener("resize", function () {
    prefs.applyDensity();
  });

  HSF.prefs = prefs;
})();
