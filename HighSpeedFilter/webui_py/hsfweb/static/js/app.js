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
   * [2026-11-22 用户定案「左上角图标用导航树还是第二张图那种菜单页」]
   * 本地设置「导航方式」两档：
   *   · tree（缺省）：宽屏折叠/展开左侧导航栏；窄屏开/关抽屉；
   *   · page：打开**当前页所属分类的菜单页**（如设备页 → 「设备 选择子项」）；
   *     已在分类菜单页再点 ☰ ⇒ 主菜单（1）。返回链与子菜单页的
   *     「← 返回主菜单」同一条。
   * 分类归属从 /api/pages 的 nav（pages_registry.NAV_TREE 同源）推导，
   * 前端不硬编码页号表。断点值见 util.js 的 HSF.DRAWER_MAX_W。
   */
  function menuPageFor(page) {
    var nav = (app.registry && app.registry.nav) || [];
    for (var i = 0; i < nav.length; i++) {
      if (Number(nav[i].page) !== Number(page)) continue;
      var e = nav[i];
      if (e.childOf >= 0 && nav[e.childOf]) {
        var parent = nav[e.childOf];
        if (parent.childOf < 0) return Number(parent.page);
      }
      break;
    }
    return 1;                       /* 主菜单页（PAGE_MENU） */
  }

  app.toggleNav = function () {
    if (HSF.prefs.menuButtonStyle() === "page") {
      HSF.prefs.applyOverlay();          /* 菜单页档：导航树强制隐藏 */
      HSF.router.go(menuPageFor(HSF.router.current()));
      app.closeDrawer();
      return;
    }
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

  /* ---- 悬浮相机卡（标题栏相机徽章；2026-11-22 用户定案） ----
   * · 点徽章开卡，**再点关闭**；每次打开重新创建（不复用旧实例）。
   * · 相机主题只在卡片存在期间加入 store 的 extraTopics；关闭立刻移除
   *   ⇒ 上游停止推帧，后台不传大流量（用户口径）。
   * · 按住卡片任意处拖动（pointer events）；位置夹在视口内。 */
  var cameraCard = null;
  var CAMERA_TOPIC_DEF = 0x0001000C;

  function cameraTopic() {
    var list = (HSF.registry && HSF.registry.pageTopics &&
                HSF.registry.pageTopics["17"]) || [];
    return list.length ? Number(list[0]) : CAMERA_TOPIC_DEF;
  }

  function closeCameraPreview() {
    if (!cameraCard) return;
    var card = cameraCard;
    cameraCard = null;
    try { if (card.cleanup) card.cleanup(); } catch (e) { console.error(e); }
    if (card.el && card.el.parentNode) card.el.parentNode.removeChild(card.el);
    HSF.store.removeExtraTopic(cameraTopic());
  }
  app.closeCameraPreview = closeCameraPreview;

  app.toggleCameraPreview = function () {
    if (cameraCard) { closeCameraPreview(); return; }
    var btn = document.querySelector(".tb-camera-btn");
    var card = HSF.el("div.camera-float");
    var head = HSF.el("div.camera-float-head", null, [
      HSF.el("span.camera-float-title", { text: "相机" }),
      HSF.el("span.camera-float-hint", { text: "拖动/拖边角缩放 · 再点图标关闭" }),
    ]);
    var view = HSF.el("div.camera-float-view");
    card.appendChild(head);
    card.appendChild(view);
    document.body.appendChild(card);

    /* 位置：徽章下方、右对齐，夹在视口内。 */
    var w = Math.min(460, Math.max(260, window.innerWidth - 32));
    var h = Math.min(320, Math.max(200, window.innerHeight - 120));
    card.style.width = w + "px";
    card.style.height = h + "px";
    var r = btn ? btn.getBoundingClientRect() : { right: window.innerWidth - 20, bottom: 60 };
    var left = Math.max(8, Math.min(r.right - w, window.innerWidth - w - 8));
    var top = Math.max(8, Math.min(r.bottom + 8, window.innerHeight - h - 8));
    card.style.left = Math.round(left) + "px";
    card.style.top = Math.round(top) + "px";

    /* 拖动 + 边缘缩放（pointer capture：拖出卡片也跟手）。
     * [2026-11-22 用户「同时支持拉伸缩放」] 距边缘 8px 内按住 = 缩放，
     * 其余区域 = 拖动；最小 260×200，左/上边缩放时反向补偿 left/top。 */
    var drag = null;
    var resize = null;
    var EDGE = 8;
    function hitEdge(ev) {
      var r = card.getBoundingClientRect();
      return { left: ev.clientX <= r.left + EDGE,
               right: ev.clientX >= r.right - EDGE,
               top: ev.clientY <= r.top + EDGE,
               bottom: ev.clientY >= r.bottom - EDGE };
    }
    function edgeCursor(e) {
      if ((e.left && e.top) || (e.right && e.bottom)) return "nwse-resize";
      if ((e.right && e.top) || (e.left && e.bottom)) return "nesw-resize";
      if (e.left || e.right) return "ew-resize";
      if (e.top || e.bottom) return "ns-resize";
      return "";
    }
    card.addEventListener("pointerdown", function (ev) {
      if (ev.button !== 0) return;
      var e = hitEdge(ev);
      if (e.left || e.right || e.top || e.bottom) {
        resize = { x: ev.clientX, y: ev.clientY, edge: e,
                   left: card.offsetLeft, top: card.offsetTop,
                   w: card.offsetWidth, h: card.offsetHeight };
        card.style.cursor = edgeCursor(e);
      } else {
        drag = { x: ev.clientX, y: ev.clientY,
                 left: card.offsetLeft, top: card.offsetTop };
        card.classList.add("dragging");
      }
      try { card.setPointerCapture(ev.pointerId); } catch (err) { /* ignore */ }
      ev.preventDefault();
    });
    card.addEventListener("pointermove", function (ev) {
      if (resize) {
        var dx = ev.clientX - resize.x;
        var dy = ev.clientY - resize.y;
        var nl = resize.left, nt = resize.top;
        var nw = resize.w, nh = resize.h;
        var MINW = 260, MINH = 200;
        if (resize.edge.left) {
          nw = Math.max(MINW, resize.w - dx);
          nl = resize.left + (resize.w - nw);
        } else if (resize.edge.right) {
          nw = Math.max(MINW, resize.w + dx);
        }
        if (resize.edge.top) {
          nh = Math.max(MINH, resize.h - dy);
          nt = resize.top + (resize.h - nh);
        } else if (resize.edge.bottom) {
          nh = Math.max(MINH, resize.h + dy);
        }
        card.style.width = Math.round(nw) + "px";
        card.style.height = Math.round(nh) + "px";
        card.style.left = Math.round(Math.max(8, nl)) + "px";
        card.style.top = Math.round(Math.max(8, nt)) + "px";
        return;
      }
      if (drag) {
        var dl = drag.left + (ev.clientX - drag.x);
        var dt = drag.top + (ev.clientY - drag.y);
        var maxL = window.innerWidth - card.offsetWidth - 8;
        var maxT = window.innerHeight - card.offsetHeight - 8;
        card.style.left = Math.round(Math.max(8, Math.min(dl, maxL))) + "px";
        card.style.top = Math.round(Math.max(8, Math.min(dt, maxT))) + "px";
        return;
      }
      if (ev.pointerType === "mouse") {
        card.style.cursor = edgeCursor(hitEdge(ev));
      }
    });
    function endDrag() {
      drag = null;
      resize = null;
      card.classList.remove("dragging");
      card.style.cursor = "";
    }
    card.addEventListener("pointerup", endDrag);
    card.addEventListener("pointercancel", endDrag);

    var cleanup = null;
    if (HSF.cameraViewMount) {
      cleanup = HSF.cameraViewMount(view, true);
    } else {
      view.appendChild(HSF.el("div.camera-placeholder",
                              { text: "相机组件未就绪" }));
    }
    cameraCard = { el: card, cleanup: cleanup };
    /* 卡片存在期间才订相机流；关闭 removeExtraTopic ⇒ 上游停推。 */
    HSF.store.addExtraTopic(cameraTopic());
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
    closeCameraPreview();          /* 登出/会话过期：关悬浮相机卡并退订 */
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
    /* [无会话时的 401 不进这里 2026-11-27 实锅] 登录前/authtoken 直登
     * 落地前可能有请求被判 401（模块加载期 addExtraTopic 等）⇒ 这里若
     * 直接拆链+清 token，会把 boot 即将/刚设好的 authtoken 一并清掉、
     * 把登录页翻上来（`_enterShell` 不隐藏登录页 ⇒ 整页停在登录页）。
     * 没有会话时该由 boot/登录表单自己给出原因文案，本收口只管
     * **已登录后被判过期**。 */
    if (!app.session) return;
    app.setSession(null);
    app._teardownShellData();
    app._showLogin("登录会话已过期，请重新登录");
  });

  document.addEventListener("DOMContentLoaded", boot);
})();
