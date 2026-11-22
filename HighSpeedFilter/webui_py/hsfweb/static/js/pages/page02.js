/* -*- coding: utf-8 -*-
 * 页 2 设备（子菜单）：入口卡片（条目由后端下发，2026-11-22 起 7 项）。
 *
 * [单一事实源] 条目 = /api/pages 的 subMenus["2"]
 * （后端 api.py 的 SUBMENU_ENTRIES：数字量3 / 模拟量4 / 搅拌泵5 /
 * 水质6 / 流量18 / 电动球阀7 / 相机17——2026-11-10 用户定案口径：
 * 阀=电动球阀、该路设备叫搅拌泵；2026-11-22 页 6 拆出流量页 18）。
 * [对齐参考 SubMenuPage] 页内标题「设备」+ 说明「选择子项」，3 列网格。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.router.register(2, {
    title: "设备",
    topics: [],
    mount: function (host, ctx) {
      var raw = (HSF.registry.subMenus && HSF.registry.subMenus["2"]) || [];
      var mod = HSF.buildMenuPage({
        title: "设备", subtitle: "选择子项",
        entries: raw.map(function (e) { return { name: e.name, page: e.page }; }),
        main: false,
      });
      mod.mount(host, ctx);
    },
  });
})();
