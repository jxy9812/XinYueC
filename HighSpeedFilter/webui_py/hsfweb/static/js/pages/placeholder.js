/* -*- coding: utf-8 -*-
 * 未接入页的统一占位模块（router.resolve 兜底调用 HSF.pagesPlaceholder）。
 *
 * [诚实占位] 只声明「该页模块尚未接入」，不铺任何假数据、不放
 * 不能用的控件（项目纪律：不留占位控件与"选了不兑现"的选项）。
 * [2026-11-22 现状] 全部**可达页**都已接入（0~15 + 17）；本模块只兜底
 * 两种深链：①保留槽位（页 16「Web 设置」仅桌面客户端提供）②未知页号。
 * 导航树里没有的页不会被点到。
 *
 * [2026-11-13 三页合并] 当时页 16/17（悬浮窗/窗口设置）并入页 13。
 * [2026-11-22] 页号已重排（16=「Web 设置」保留槽位、17=相机）⇒ 旧的
 * 「pageIndex ≥ count ⇒ 已并入」启发式被删除：它对任意未知深链都在说假话。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.pagesPlaceholder = function (pageIndex) {
    var registry = HSF.registry || {};
    var count = registry.count || 0;
    var names = registry.names || [];
    /* [保留槽位] 「Web 设置」只在 Python 桌面客户端实现（网页端是它自己）。
     * 深链 #/page/16 落到这里时要说明"桌面端独有"，不是"以后接入"。 */
    var webSetIdx = names.indexOf("Web 设置");
    if (webSetIdx >= 0 && pageIndex === webSetIdx) {
      return {
        title: "Web 设置",
        topics: [],
        mount: function (host) {
          HSF.replaceChildren(host).appendChild(
            HSF.el("div.todo-page", null, [
              HSF.el("div.todo-badge",
                     { text: "「Web 设置」仅在桌面客户端提供" }),
              HSF.el("div.caption", {
                text: "网页端本身就是 Web 界面，不需要（也无法）再配置" +
                      "「网页服务/域名/二维码」；这些设置请用桌面客户端的" +
                      "「Web 设置」页（页 16）。",
              }),
            ]));
        },
      };
    }
    /* [2026-11-22 删掉「已并入」启发式] 旧逻辑把「pageIndex ≥ count」当成
     * 被合并的旧页号，一律回「已并入本地设置」——但页号早就重排过（旧
     * 16/17 现在是「Web 设置」/「相机」），任意未知深链（如 999）都会被
     * 谎称"已并入"。现在只保留上面的 Web 设置特例，其余按未知页号如实说。 */
    var name = (registry.names && registry.names[pageIndex]) ||
               ("页 " + pageIndex);
    return {
      title: name,
      topics: [],
      mount: function (host) {
        HSF.replaceChildren(host).appendChild(
          HSF.el("div.todo-page", null, [
            /* [2026-11-22] 全部可达页都已接入 ⇒ 走到这里的只剩"导航里没有
             * 的页号"（手工深链/旧书签）。说"以后接入"是假的，直说未知。 */
            HSF.el("div.todo-badge",
                   { text: "未知页号 #/page/" + pageIndex }),
            HSF.el("div.caption", {
              text: "导航树里没有这个页号（当前共 " + (count || "?") +
                    " 页）；请从左侧导航进入。",
            }),
          ]));
      },
    };
  };
})();
