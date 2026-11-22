/* -*- coding: utf-8 -*-
 * hash 路由：#/page/N 深链直达 + 页面模块注册表。
 *
 * [对齐 pages_registry] 页号/页名/导航树与参考客户端 pages_registry.py
 * 同源（后端 /api/pages 下发，前端不硬编码——见 app.js 的 registry 装配）。
 *
 * [页面模块约定] 见 static/js/pages/README.md：
 *   HSF.pages.register(<页号>, { title, topics, mount(el, ctx), unmount() })
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var registry = {};              /* 页号 → 模块 */
  var bus = HSF.makeBus();
  var current = -1;

  var router = {
    /** 缺省着陆页：**主界面(0)**——桌面/移动端统一（2026-11-17 用户定案
     *  「web 不管是不是移动端页统一默认进入主界面」）。主菜单(1) 仍可从
     *  导航/主界面菜卡片进入；深链 #/page/N 不受影响。 */
    DEFAULT_PAGE: 0,

    /** 注册页面模块（pages/pageNN.js 在加载时调）。 */
    register: function (pageIndex, module) {
      registry[Number(pageIndex)] = module || {};
    },

    has: function (pageIndex) {
      return !!registry[Number(pageIndex)];
    },

    module: function (pageIndex) {
      return registry[Number(pageIndex)] || null;
    },

    current: function () { return current; },

    /** 跳页（写 hash → 触发 hashchange → resolve）。 */
    go: function (pageIndex) {
      var target = "#/page/" + Number(pageIndex);
      if (location.hash === target) {
        this._lastHash = null;              /* 同页强制重放（点菜单回自己） */
        this.resolve();
      } else {
        location.hash = target;
      }
    },

    /** 从 location.hash 解析页号（非法回缺省）。 */
    parse: function () {
      var m = /^#\/page\/(\d+)$/.exec(location.hash || "");
      var n = m ? parseInt(m[1], 10) : NaN;
      if (!(n >= 0)) n = this.DEFAULT_PAGE;
      return n;
    },

    /**
     * 解析当前 hash 并挂载对应模块。
     * 找不到已注册模块 → 落到 placeholder（诚实提示，不装假数据）。
     */
    resolve: function () {
      var page = this.parse();
      /* [同 hash 早退] start() 里显式 resolve 一次 + hashchange 事件随后
         又触发一次——不挡会挂载两遍（页面 mount 有副作用时不可重复）。 */
      if (this._lastHash === location.hash) return;
      this._lastHash = location.hash;
      var host = document.getElementById("page-host");
      var mod = registry[page];
      if (mod && typeof mod.mount === "function") {
        current = page;
      } else {
        mod = HSF.pagesPlaceholder(page);   /* 未接入页：统一占位模块 */
        current = -1;                       /* 占位不进 current（导航不高亮） */
      }
      if (this._unmount) {
        try { this._unmount(); } catch (err) { console.error(err); }
        this._unmount = null;
      }
      HSF.replaceChildren(host);
      var ctx = HSF.pageContext();
      try {
        mod.mount(host, ctx);
      } catch (err) {
        console.error("[router] 页面挂载失败 page=" + page, err);
        HSF.replaceChildren(host).appendChild(HSF.el("div.todo-page", null, [
          HSF.el("div", { text: "页面加载出错：" + (err && err.message ? err.message : err) }),
        ]));
      }
      this._unmount = typeof mod.unmount === "function" ? mod.unmount : null;
      /* 页面主题收敛（store 会同值早退）+ 标题联动。 */
      HSF.store.updatePageTopics(typeof mod.topics === "function"
                                 ? mod.topics(ctx) : (mod.topics || []));
      bus.emit("page", { page: page, mounted: mod !== null, title: mod.title || "" });
    },

    /** 初始化：监听 hashchange + 立即解析一次（只做一次；重登录再入不重挂）。 */
    start: function () {
      if (this._started) return;
      this._started = true;
      var self = this;
      window.addEventListener("hashchange", function () { self.resolve(); });
      if (!location.hash) {
        location.hash = "#/page/" + this.DEFAULT_PAGE;   /* 触发首次 resolve */
      }
      this.resolve();
    },

    /**
     * 卸载当前页（登出 / 会话过期时调）。
     *
     * [为什么必须有] 页面模块的 unmount 负责停掉本页的定时器（1s 的
     * /api/mode 轮询、200ms 重绘）与退订 store 事件。不卸载 ⇒ 登出后
     * 这些定时器**继续跑**，而且每个请求带的 token 已被清空 ⇒ 服务端
     * 一律回 401 ⇒ **401 轮询风暴**（页面还在、界面全空）。
     *
     * [为什么要清 _lastHash] 卸载后要能**重新挂载**同一个 hash（重新登录
     * 回来时壳是空的）⇒ 把"同 hash 早退"的凭据清掉，下次 resolve() 会
     * 真的挂一遍。
     */
    unmountCurrent: function () {
      if (this._unmount) {
        try { this._unmount(); } catch (err) { console.error(err); }
        this._unmount = null;
      }
      this._lastHash = null;
      current = -1;
      var host = document.getElementById("page-host");
      if (host) HSF.replaceChildren(host);
    },

    onPage: function (fn) { return bus.on("page", fn); },
  };

  HSF.router = router;
})();
