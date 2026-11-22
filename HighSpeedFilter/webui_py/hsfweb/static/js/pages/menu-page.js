/* -*- coding: utf-8 -*-
 * 菜单页构建器：入口卡片（主菜单 / 设备 / 数据 / 设置 四页共用）。
 *
 * [对齐参考 pages_other.MainMenuPage / SubMenuPage]
 *   · 页内标题 + 说明（参考 add_header：主菜单「选择功能分类」、
 *     子菜单页「选择子项」）；
 *   · 主菜单三大分类入口 = 大按钮（图标在上、名称在下，触控友好；
 *     MAIN_MENU_ENTRIES 的 ▤ ≣ ⚙）；
 *   · 子菜单页 = 3 列网格文字按钮（参考 grid 3 列）。
 * 条目一律来自 /api/pages（mainMenu / subMenus），前端不硬编码页名——
 * 后端 pages_registry 是唯一事实源。
 *
 * [图标改自绘 SVG] 三个 Unicode 码位（▤/≣/⚙）在**浏览器 fallback** 与
 * **Qt fallback** 下画出来完全不是一回事：齿轮一个实心浅灰、一个空心
 * 描边小齿；表格 17×14 vs 29×22（2026-11-13 对照实拍，一眼能看出不是
 * 同一套 UI）。⇒ 主菜单图标改为内联 SVG 自绘（线宽/齿形照参考端观感），
 * 尺寸固定 36px 方框（theme.css .menu-icon；齿轮例外，见 ICON_SVG 注释）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  /* 主菜单三枚图标（键 = 后端 mainMenu 的 icon 码位）。
     [尺寸照参考端实测] ref/p01 逐像素量得三枚图标的**墨迹**尺寸：
     ▤ 19×18 / ≣ 17 宽（1px 细笔画）/ ⚙ 40×39（6 瓣**实心**齿轮）。
     2026-11-13 第 2 轮对照实拍：网页端旧版齿轮是**空心描边**（墨迹
     36×36）、数据是 3 条**粗长**线（16×14）⇒ 与参考差一档。本轮按实测
     重画：齿轮 = 浅灰实心填充 + 深色描边 + 灰色内环 + 深色方心（墨迹
     放大到 40px）；数据 = 3 条 1.2px 细线、收短到 17 宽 10 高。
     用 currentColor 跟随按钮前景色：描边/内环/方心取纯前景色，齿轮填充
     取前景色 20% 透明（亮色主题下叠出 ≈#D1D1D1，参考端实测 #CCCCCC）。 */
  var ICON_SVG = {
    /* ▤：方框内横线（参考端 ▤ 码位的字形；墨迹 19×18，已与参考齐平） */
    "▤": '<svg viewBox="0 0 36 36" xmlns="http://www.w3.org/2000/svg" ' +
         'aria-hidden="true" focusable="false">' +
         '<g fill="none" stroke="currentColor" stroke-width="1.6">' +
         '<rect x="9.5" y="10" width="17" height="16" rx="0.5"/>' +
         '<line x1="10.5" y1="14.5" x2="25.5" y2="14.5"/>' +
         '<line x1="10.5" y1="18" x2="25.5" y2="18"/>' +
         '<line x1="10.5" y1="21.5" x2="25.5" y2="21.5"/>' +
         '</g></svg>',
    /* ≣：三条细横线（参考端字形是 1px 细笔画、宽 17，前 3 条覆盖 10 高；
     * 旧版网页端是 1.8px 粗线 × 3 条、16 宽 13.8 高 ⇒ 本轮改细并收短）。 */
    "≣": '<svg viewBox="0 0 36 36" xmlns="http://www.w3.org/2000/svg" ' +
         'aria-hidden="true" focusable="false">' +
         '<g stroke="currentColor" stroke-width="1.2" stroke-linecap="butt">' +
         '<line x1="9.5" y1="13.6" x2="26.5" y2="13.6"/>' +
         '<line x1="9.5" y1="18" x2="26.5" y2="18"/>' +
         '<line x1="9.5" y1="22.4" x2="26.5" y2="22.4"/>' +
         '</g></svg>',
    /* ⚙：6 瓣实心齿轮（浅灰填充 + 深色描边 + 灰色内环 + 深色方心）。
     * [为什么单独给 40px] 参考端 ⚙ 码位是 40×39 的实心大齿轮，比 ▤/≣
     * 大一倍多；theme.css 的 .menu-icon svg 固定 36px ⇒ 这里用内联
     * style 覆写成 40px（内联样式优先级高于样式表，不动共享 theme.css）。
     * [为什么还要 flex:0 0 auto] .menu-icon 是 36px 宽的 flex 容器，
     * SVG（带 viewBox、无固有宽）min-width:auto 解析为 0 ⇒ 默认会被
     * flex-shrink 压回 36px（实测墨迹只剩 36×36）；写 flex:0 0 auto 才
     * 真正按 40px 渲染，且因 justify-content:center 仍居中（左右各溢
     * 出 2px，容器无 overflow 裁剪）。
     * 齿形：齿顶半径 18.8、齿根 15.3（缺口在 12/6 点方向），描边 2.4。 */
    "⚙": '<svg viewBox="0 0 40 40" xmlns="http://www.w3.org/2000/svg" ' +
         'aria-hidden="true" focusable="false" ' +
         'style="width:40px;height:40px;flex:0 0 auto">' +
         '<path d="M37.78 13.88 A18.8 18.8 0 0 1 37.78 26.12 ' +
         'Q38.3 27.02 33.38 27.42 A15.3 15.3 0 0 1 32.22 29.21 ' +
         'Q35.23 32.33 34.19 32.33 A18.8 18.8 0 0 1 23.59 38.45 ' +
         'Q23.07 39.36 20.27 35.3 A15.3 15.3 0 0 1 18.14 35.19 ' +
         'Q16.93 39.36 16.41 38.45 A18.8 18.8 0 0 1 5.81 32.33 ' +
         'Q4.77 32.33 6.89 27.88 A15.3 15.3 0 0 1 5.92 25.98 ' +
         'Q1.7 27.02 2.22 26.12 A18.8 18.8 0 0 1 2.22 13.88 ' +
         'Q1.7 12.98 6.62 12.58 A15.3 15.3 0 0 1 7.78 10.79 ' +
         'Q4.77 7.67 5.81 7.67 A18.8 18.8 0 0 1 16.41 1.55 ' +
         'Q16.93 0.64 19.73 4.7 A15.3 15.3 0 0 1 21.86 4.81 ' +
         'Q23.07 0.64 23.59 1.55 A18.8 18.8 0 0 1 34.19 7.67 ' +
         'Q35.23 7.67 33.11 12.12 A15.3 15.3 0 0 1 34.08 14.02 ' +
         'Q38.3 12.98 37.78 13.88 Z" ' +
         'fill="currentColor" fill-opacity="0.2" stroke="currentColor" ' +
         'stroke-width="2.4" stroke-linejoin="round"/>' +
         '<circle cx="20" cy="20" r="9" fill="none" stroke="currentColor" ' +
         'stroke-opacity="0.4" stroke-width="2"/>' +
         '<rect x="16.4" y="16.4" width="7.2" height="7.2" rx="1.8" ' +
         'fill="currentColor"/></svg>',
  };
  HSF.menuIconSvg = function (icon) {
    return ICON_SVG[icon] || "";
  };

  /**
   * 建一个菜单页模块。
   * opts：{ title, subtitle, entries:[{name,page,icon?}], main:boolean }。
   * main=true 走主菜单大按钮版式（图标+名称竖排），否则 3 列子菜单版式。
   */
  HSF.buildMenuPage = function (opts) {
    if (typeof opts === "string") {
      /* 兼容旧签名 buildMenuPage(title, entries)。 */
      var title = opts;
      opts = { title: title, entries: arguments[1] || [], main: false };
    }
    var entries = opts.entries || [];
    return {
      title: opts.title,
      topics: [],
      mount: function (host) {
        var grid = HSF.el("div.menu-grid" + (opts.main ? ".main" : ""));
        entries.forEach(function (e) {
          /* 图标只有主菜单三大分类有（MAIN_MENU_ENTRIES 的 ▤ ≣ ⚙）；
           * 子菜单页参考实现是纯文字条目，不放同形假图标冒充语义。
           * 图标用自绘 SVG（HSF.menuIconSvg），不用 Unicode 码位——
           * 浏览器 fallback 与 Qt fallback 画出来不是一回事。 */
          var kids = [];
          if (e.icon) {
            var iconSpan = HSF.el("span.menu-icon");
            iconSpan.innerHTML = HSF.menuIconSvg(e.icon);
            kids.push(iconSpan);
          }
          kids.push(HSF.el("span.menu-name", { text: e.name }));
          grid.appendChild(HSF.el("button.menu-entry", {
            type: "button",
            title: "进入「" + e.name + "」",
            onclick: function () { HSF.router.go(e.page); },
          }, kids));
        });
        HSF.replaceChildren(host).appendChild(
          HSF.el("div.menu-page", null, [
            HSF.el("div.menu-head", null, [
              HSF.el("h2", { text: opts.title }),
              opts.subtitle
                ? HSF.el("span.menu-sub", { text: opts.subtitle })
                : null,
            ]),
            grid,
          ]));
      },
    };
  };
})();
