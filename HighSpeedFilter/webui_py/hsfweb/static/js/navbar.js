/* -*- coding: utf-8 -*-
 * 左侧导航树：/api/pages 的 nav（pages_registry.NAV_TREE 同源）。
 *
 * [对齐参考 NavPanel]（widgets.py:658-）两级树：顶层条目（childOf=-1）+
 * 缩进子条目，**纯缩进平面列表**——参考端没有折叠箭头（分组条目与普通
 * 条目同款式、恒展开），当前页也无可见高亮（按钮 never setCheckable，
 * :checked 样式从不显示；实测 ref 各页导航背景均一）。
 * [2026-11-13 对齐参考撤增强] 原「分组可折叠 ▾/▸」是 web 自增强，按
 * 「普通用户一眼分不出两端」判据删除；.current 类保留作逻辑定位，无样式。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  function mount(root, navEntries) {
    HSF.replaceChildren(root);
    root.appendChild(HSF.el("div.nav-title", { text: "导航" }));

    /* 跳页并顺手收起抽屉（窄屏导航浮在内容上，选完就该让位）。 */
    function goPage(page) {
      HSF.router.go(page);
      if (HSF.isDrawer() && HSF.app) HSF.app.closeDrawer();
    }

    var childButtons = {};          /* 页号 → <button>（逻辑定位用） */

    var groups = [];                /* 顶层项 + 各自的子项 */
    var current = null;
    for (var i = 0; i < navEntries.length; i++) {
      var e = navEntries[i];
      if (e.childOf < 0) {
        current = { entry: e, children: [] };
        groups.push(current);
      } else if (current) {
        current.children.push(e);
      }
    }

    groups.forEach(function (g) {
      /* 顶层组条目（设备/数据/设置）：可点（跳分类页），无箭头无折叠。 */
      var headBtn = HSF.el("button.nav-item", {
        type: "button", text: g.entry.caption,
        onclick: function () { goPage(g.entry.page); },
      });

      var childBox = HSF.el("div.nav-children");
      g.children.forEach(function (c) {
        var btn = HSF.el("button.nav-item.child", {
          type: "button", text: c.caption,
          onclick: function () { goPage(c.page); },
        });
        childBox.appendChild(btn);
        childButtons[c.page] = btn;
      });

      var group = HSF.el("div.nav-group", null, [headBtn, childBox]);
      root.appendChild(group);
      childButtons[g.entry.page] = headBtn;
    });

    /* 当前页类挂载（供样式/脚本定位；theme.css 已无 .current 可见样式，
     * 与参考端「无当前页高亮」一致）。 */
    function applyCurrent(page) {
      Object.keys(childButtons).forEach(function (k) {
        childButtons[k].classList.toggle("current", Number(k) === Number(page));
      });
    }
    applyCurrent(HSF.router.current());

    return {
      applyCurrent: applyCurrent,
      setCollapsed: function (collapsed) {
        root.classList.toggle("collapsed", !!collapsed);
      },
    };
  }

  HSF.navbar = { mount: mount };
})();
