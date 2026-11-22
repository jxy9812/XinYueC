/* -*- coding: utf-8 -*-
 * 应用装配（最后加载）：壳装配 / 登录态机 / ?authtoken 直登 / 权限位。
 *
 * 启动序列：
 *   1. prefs.applyAll（缩放/紧凑档，先于任何布局测量）；
 *   2. ?authtoken=<tok> → 直接当会话用（无头截图/自动化通路），
 *      失效则回登录页并说明原因；无参数 → sessionStorage → Cookie 兜底
 *      （后端 bearer_token 也认 hsf_token Cookie，httpd.py:105-110，
 *      刷新不掉线）→ 都没有再试 /api/me，成功就复用；
 *   3. 建壳（titlebar/nav/status）→ 拉 /api/pages（导航与菜单条目）；
 *   4. SSE 连接 + 首帧 /api/state 灌 store → 路由 start。
 * 未登录：壳隐藏，只显示登录视图；登录成功重跑 3~4。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var TOKEN_KEY = "hsfweb.token";           /* sessionStorage（标签页内有效） */
  /* 标题栏恒产品名（theme.APP_TITLE 同源；client_py main_window.py:188
   * set_title(theme.APP_TITLE)——页名只进 document.title 不进标题栏）。
   * 2026-11-19 用户改名「互嵌深层高速过滤机」（前身 PMMA有机玻璃高速过滤器）。 */
  var APP_TITLE = "互嵌深层高速过滤机";
  var PERM = {
    VIEW: 1 << 0,
    DEVICE_CONTROL: 1 << 1,
    MODE_SWITCH: 1 << 2,
    AUTO_CONTROL: 1 << 3,
    ACCOUNT_ADMIN: 1 << 4,
    DOWNLOAD: 1 << 5,
  };

  var app = {
    PERM: PERM,
    registry: {},                            /* /api/pages 文档（含 nav/names…） */
    session: null,                           /* /api/me 的 session 节 */
    _titlebar: null,
    _navbar: null,
    _statusbar: null,
    _loginView: null,
  };
  HSF.app = app;

  /** 页面挂载上下文（router.resolve 传给 mount；只读视图，勿缓存跨页）。 */
  HSF.pageContext = function () {
    return {
      session: app.session,
      canPerm: app.canPerm,
      registry: HSF.registry,
      route: HSF.router.current(),
    };
  };

  /* ---- 权限位（session.perms 与位表判；角色中文名直接用后端 roleText） ---- */
  app.canPerm = function (perm) {
    var perms = (app.session && app.session.perms) | 0;
    return ((perms & perm) === perm);
  };

  app.setSession = function (session) {
    app.session = session || null;
    if (app._statusbar) {
      app._statusbar.setAccount(
        session ? session.user : "",
        session ? session.roleText : "");
    }
  };

  /* ---- 导航显隐（☰） ----
   * [两档语义] 宽屏（>860px）：☰ 折叠/展开左侧导航栏（既有行为，逐像素不变）。
   * 窄屏（≤860px）：☰ 开/关**抽屉**（导航树浮在内容之上，遮罩点一下关闭）。
   * 断点值见 util.js 的 HSF.DRAWER_MAX_W（与 theme.css 的媒体查询同值）。 */
  app.toggleNav = function () {
    if (HSF.isDrawer()) {
      app.setDrawer(!document.body.classList.contains("nav-open"));
      return;
    }
    if (app._navbar) {
      app._navbar.setCollapsed(
        document.getElementById("nav-panel").classList.contains("collapsed")
        === false);
    }
  };

  /** 抽屉开/关（窄屏导航）。 */
  app.setDrawer = function (on) {
    document.body.classList.toggle("nav-open", !!on);
    if (on) app.syncDrawerTop();
  };

  /** 关闭抽屉（选中页面 / 点遮罩 / Esc 都走这里）。 */
  app.closeDrawer = function () {
    if (document.body.classList.contains("nav-open")) {
      document.body.classList.remove("nav-open");
    }
  };

  /** 抽屉顶边 = 标题栏实测下缘（标题栏窄屏是两行，固定值会错位）。 */
  app.syncDrawerTop = function () {
    var tb = document.getElementById("title-bar");
    if (!tb) return;
    var h = tb.getBoundingClientRect().height;
    document.documentElement.style.setProperty(
      "--drawer-top", Math.round(h) + "px");
  };

  /* ---- 模式切换（标题栏钮；PERM_MODE_SWITCH；结果落状态行终态） ---- */
  app.toggleRunMode = async function () {
    if (!app.session || !app.canPerm(PERM.MODE_SWITCH)) return;
    var cur = HSF.store.get("mode") || {};
    var target = cur.auto ? 0 : 1;
    var doc = await HSF.api.setMode(target);
    if (doc && doc.ok) {
      HSF.statusMessage("已切换到" + (target ? "自动" : "手动") + "模式");
    } else {
      HSF.statusMessage("切换失败：" + ((doc && doc.err) || "未知错误"));
    }
  };

  /* ---- 账户动作（状态栏菜单；对齐参考 switchUser/logout 两动作） ---- */
  app.switchAccount = function () {
    app._teardownShellData();
    app._showLogin(null);
  };
  app.logout = async function () {
    await HSF.api.logout();                   /* 后端吊销会话+清 Cookie */
    try { sessionStorage.removeItem(TOKEN_KEY); } catch (e) { /* 忽略 */ }
    app.setSession(null);
    app._teardownShellData();
    HSF.statusMessage("已退出登录");
    app._showLogin("已退出登录，请重新登录");
  };
  app._teardownShellData = function () {
    /* [先卸载当前页] 页面模块的 unmount 会停掉本页的定时器（1s 的
     * /api/mode 轮询、200ms 重绘）并退订 store 事件。不卸载 ⇒ 登出/
     * 会话过期后这些定时器继续跑，而 token 已被清空 ⇒ 每个请求都吃
     * 401 ⇒ **401 轮询风暴**（页面还在、界面全空）。 */
    if (HSF.router && HSF.router.unmountCurrent) {
      HSF.router.unmountCurrent();
    }
    HSF.store.disconnect();
    HSF.store.data = {};
    HSF.api.setToken("");
    document.getElementById("app-shell").classList.add("hidden");
    document.getElementById("login-view").classList.remove("hidden");
  };

  /* ---- 登录成功文案（登录表单/authtoken 直登两路同串；xlink.py:576）---- */
  function loginMessage(session) {
    return "登录成功（" + ((session && session.user) || "") +
           "，角色 " + ((session && session.role) | 0) + "）";
  }
  HSF.loginMessage = loginMessage;

  /* ---- 登录视图 ---- */
  app._showLogin = function (presetError) {
    if (app._loginView) app._loginView.destroy();
    document.getElementById("app-shell").classList.add("hidden");
    var root = document.getElementById("login-view");
    app._loginView = HSF.login.mount(root, function (token, session) {
      HSF.api.setToken(token);
      try { sessionStorage.setItem(TOKEN_KEY, token); } catch (e) { /* 忽略 */ }
      app.setSession(session);
      if (app._loginView) { app._loginView.destroy(); app._loginView = null; }
      document.getElementById("app-shell").classList.remove("hidden");
      HSF.statusMessage(loginMessage(session));
      app._enterShell();
    });
    if (presetError && app._loginView) {
      app._loginView.showError(presetError);
    }
  };

  /* ---- 会话建立后的壳装配（可重入：切换账户→登录→再进） ---- */
  app._enterShell = async function () {
    document.getElementById("app-shell").classList.remove("hidden");

    /* 壳组件只装一次；再入时刷新账户显示。 */
    if (!app._statusbar) {
      app._statusbar = HSF.statusbar.mount(document.getElementById("status-bar"));
      app._titlebar = HSF.titlebar.mount(document.getElementById("title-bar"));
      app.routerUnsub = HSF.router.onPage(function (ev) {
        if (app._navbar) app._navbar.applyCurrent(ev.mounted ? ev.page : -1);
        /* 换页即收起抽屉（窄屏）：选中项后导航不该还盖在内容上。 */
        app.closeDrawer();
        /* [标题栏恒产品名] 参考端页名不进标题栏（main_window.py:186-188
         * 显式重设 theme.APP_TITLE）；页名只进浏览器标签 document.title。 */
        app._titlebar.setTitle(APP_TITLE);
        document.title = APP_TITLE + " - " +
          ((app.registry.names || [])[ev.page] || ev.title || "");
      });
    }
    app.setSession(app.session);

    /* 注册表（页名/导航/菜单条目/每页主题）——失败也给占位，不让壳白屏。 */
    var pages = await HSF.api.pages();
    if (pages && pages.ok) {
      app.registry = pages;
      HSF.registry = pages;
    } else {
      HSF.registry = app.registry;
      HSF.statusMessage("页表拉取失败：" + ((pages && pages.err) || "未知错误"));
    }
    app._navbar = HSF.navbar.mount(
      document.getElementById("nav-panel"),
      (HSF.registry.nav || []));

    /* chrome 显隐还原（页 13·悬浮窗分区的本机旗标；与设置页同一份判定）。 */
    HSF.prefs.applyOverlay();

    /* 首帧快照 + SSE（快照先灌，SSE 到来前界面也有值）。 */
    var st = await HSF.api.state();
    if (st && st.ok) {
      ["vfd", "temphum", "analog", "flowmeter", "turbidity", "pump",
       "digital", "valve", "health", "mode"].forEach(function (k) {
        if (st[k] !== undefined) HSF.store.applyEvent(k, st[k]);
      });
    }
    if (st && st.link) HSF.store.applyEvent("link", st.link);
    HSF.store.connect();

    HSF.router.start();
    /* [重入壳] 登出时当前页已被卸载（router.unmountCurrent），而
     * router.start() 只在首次生效 ⇒ 显式再 resolve 一次，把当前页
     * 重新挂上（否则重新登录后内容区是空的）。 */
    HSF.router.resolve();
  };

  /* ==================================================================
   *                            启动
   * ================================================================== */
  async function boot() {
    HSF.prefs.applyAll();

    /* ---- 抽屉导航的外围接线（窄屏）----
     * 遮罩点一下关闭、Esc 关闭、窗口变宽时自动收起抽屉（回桌面档）。 */
    var scrim = document.getElementById("nav-scrim");
    if (scrim) {
      scrim.addEventListener("click", function () { app.closeDrawer(); });
    }
    document.addEventListener("keydown", function (ev) {
      if (ev.key === "Escape") app.closeDrawer();
    });
    window.addEventListener("resize", function () {
      if (!HSF.isDrawer()) app.closeDrawer();
      app.syncDrawerTop();
    });
    app.syncDrawerTop();

    /* ?authtoken=<tok> 直登（无头截图/自动化）：验活后即进入，
     * 随后把参数从地址栏清掉（token 留 sessionStorage，深链 hash 保留）。 */
    var url = new URL(window.location.href);
    var autoToken = url.searchParams.get("authtoken");
    if (autoToken) {
      url.searchParams.delete("authtoken");
      window.history.replaceState(null, "", url.pathname + url.search + url.hash);
    }

    var token = autoToken || "";
    if (!token) {
      try { token = sessionStorage.getItem(TOKEN_KEY) || ""; } catch (e) { /* 忽略 */ }
    }
    if (token) {
      HSF.api.setToken(token);
      var me = await HSF.api.me();
      if (me && me.ok && me.session) {
        try { sessionStorage.setItem(TOKEN_KEY, token); } catch (e) { /* 忽略 */ }
        app.setSession(me.session);
        if (me.runMode) HSF.store.applyEvent("mode", me.runMode);
        /* [直登也写登录终态] 与登录表单同串（参考端两条路径都写
         * 「登录成功（root，角色 2）」，pages_other/xlink.py:576 同款）。
         * [写入时机] 写在**壳装配之前**：此时状态栏还没挂，消息进
         * statusbar.js 的待投队列（_pending），壳装配时补投 ⇒ 页面随后
         * 写的业务消息（如历史页「已载入服务端历史：…」）仍会**覆盖**它，
         * 与参考端「登录文案 → 业务文案」的先后顺序一致（2026-11-13
         * 对照：写终态若发生在页面挂载之后，会把业务消息顶掉）。 */
        HSF.statusMessage(loginMessage(me.session));
        await app._enterShell();
        return;
      }
      HSF.api.setToken("");
      try { sessionStorage.removeItem(TOKEN_KEY); } catch (e) { /* 忽略 */ }
      app._showLogin(autoToken
        ? "登录会话无效或已过期，请重新登录"
        : "上次会话已过期，请重新登录");
      return;
    }

    /* 完全无凭据：仍问一次 /api/me——后端 Cookie 兜底（刷新不掉线）。 */
    var me2 = await HSF.api.me();
    if (me2 && me2.ok && me2.session) {
      app.setSession(me2.session);
      if (me2.runMode) HSF.store.applyEvent("mode", me2.runMode);
      await app._enterShell();
      return;
    }
    app._showLogin(null);
  }

  /* 401 统一收口：任何请求被判未登录 → 拆数据面回登录页。 */
  HSF.api.onUnauthorized(function () {
    if (app._loginView) return;               /* 已在登录页：不重复 */
    app.setSession(null);
    app._teardownShellData();
    app._showLogin("登录会话已过期，请重新登录");
  });

  document.addEventListener("DOMContentLoaded", boot);
})();
