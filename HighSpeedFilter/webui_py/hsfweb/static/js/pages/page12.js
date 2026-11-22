/* -*- coding: utf-8 -*-
 * 页 12 设置（设置子菜单）：三张入口卡片（2026-11-13 三页合并后）。
 *
 * [单一事实源] 条目 = /api/pages 的 subMenus["12"]
 * （后端 pages_registry.SUBMENU_ENTRIES：本地设置13 / 远端设置14 /
 * 网络设置15；原「悬浮窗设置16」「窗口设置17」已并入页 13，条目随之删除）。
 * [对齐参考 SubMenuPage] 页内标题「设置」+ 说明「选择子项」，3 列网格。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.router.register(12, {
    title: "设置",
    topics: [],
    mount: function (host, ctx) {
      var raw = (HSF.registry.subMenus && HSF.registry.subMenus["12"]) || [];
      var mod = HSF.buildMenuPage({
        title: "设置", subtitle: "选择子项",
        entries: raw.map(function (e) { return { name: e.name, page: e.page }; }),
        main: false,
      });
      mod.mount(host, ctx);
    },
  });
})();
