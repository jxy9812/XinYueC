/* -*- coding: utf-8 -*-
 * 页 1 主菜单：三大分类入口卡片（设备/数据/设置）。
 *
 * [单一事实源] 条目 = /api/pages 的 mainMenu（后端 pages_registry.
 * MAIN_MENU_ENTRIES：设备→页2 / 数据→页8 / 设置→页12，图标 ▤ ≣ ⚙）。
 * [对齐参考 MainMenuPage] 页内标题「主菜单」+ 说明「选择功能分类」，
 * 大按钮版式（图标+名称竖排）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.router.register(1, {
    title: "主菜单",
    topics: [],
    mount: function (host, ctx) {
      var entries = (HSF.registry.mainMenu || []).map(function (e) {
        return { name: e.name, page: e.page, icon: e.icon };
      });
      var mod = HSF.buildMenuPage({
        title: "主菜单", subtitle: "选择功能分类",
        entries: entries, main: true,
      });
      mod.mount(host, ctx);
    },
  });
})();
