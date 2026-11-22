/* -*- coding: utf-8 -*-
 * 页 8 数据（数据子菜单）：三张入口卡片。
 *
 * [单一事实源] 条目 = /api/pages 的 subMenus["8"]
 * （后端 pages_registry.SUBMENU_ENTRIES：历史数据9 / 日志10 / 报警11）。
 * [对齐参考 SubMenuPage] 页内标题「数据」+ 说明「选择子项」，3 列网格。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.router.register(8, {
    title: "数据",
    topics: [],
    mount: function (host, ctx) {
      var raw = (HSF.registry.subMenus && HSF.registry.subMenus["8"]) || [];
      var mod = HSF.buildMenuPage({
        title: "数据", subtitle: "选择子项",
        entries: raw.map(function (e) { return { name: e.name, page: e.page }; }),
        main: false,
      });
      mod.mount(host, ctx);
    },
  });
})();
