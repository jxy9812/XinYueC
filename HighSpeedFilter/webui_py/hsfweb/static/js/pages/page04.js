/* -*- coding: utf-8 -*-
 * 页 4 模拟量：8 路 4~20mA 输入只读表。
 *
 * [对齐参考 pages_monitor.AnalogPage 逐项移植]：
 *   · 四列表（通道/名称/数值/单位），通道号与名字**分成两列**（通道号是
 *     接线/排查口径，名字是工艺口径）；没有名字的通道留空；
 *   · 服务端配了工程量的通道（现场通道0 接压差计）同行显示**原始 mA** 与
 *     换算后的物理量：`11.988    0.499`，单位列 `mA / MPa`；越限说明
 *     （ew{i}）拼在数值后面；
 *   · 无效通道数值 "--"（单位列仍 mA）；表底一行原始寄存器串
 *     「原始寄存器：CH0=...mV CH1=...mV …」；
 *   · 无数据（未订阅或设备离线）：整表 "--" + 说明行。
 *
 * [只读页] 无控制动作；数据面 = store 的 analog 快照/SSE（订阅
 * TOPIC_AI_STATUS）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var HEADERS = ["通道", "名称", "数值", "单位"];

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  /** 单元格文本写入（同值早退）。 */
  function cellText(td, text) {
    text = String(text == null ? "" : text);
    if (td.textContent !== text) td.textContent = text;
  }

  var activeUnmount = null;

  HSF.router.register(4, {
    title: "模拟量",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["4"]) || [];
      return t.length ? t : [0x00010002];
    },
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];
    var tbody = HSF.el("tbody");
    var table = HSF.el("table.table.analog-table", null, [
      (function () {
        var thead = HSF.el("thead");
        var tr = HSF.el("tr");
        HEADERS.forEach(function (h) {
          tr.appendChild(HSF.el("th", { text: h }));
        });
        thead.appendChild(tr);
        return thead;
      })(),
      tbody,
    ]);
    var rawCaption = HSF.el("div.caption.analog-raw", { text: "" });
    /* [窄屏横滚容器] 表宽按内容算（数值列两段数字 + 单位列），窄屏放不下
     * 时让容器内横滚，而不是把整页撑破（桌面端 overflow 可见 ⇒ 无变化）。 */
    var tableWrap = HSF.el("div.analog-table-wrap", null, [table]);

    var page = HSF.el("div.analog-page", null, [
      HSF.el("div.analog-head", null, [
        HSF.el("span.page-head-title", { text: "模拟量" }),
        HSF.el("span.page-head-sub.caption", { text: "8 路 4~20 mA 输入" }),
      ]),
      HSF.el("div.card", null, [
        HSF.el("div.card-title", { text: "通道读数" }), tableWrap]),
      rawCaption,
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);

    /** 占位/无数据行。 */
    function emptyRows(count) {
      var rows = [];
      for (var i = 0; i < count; i++) {
        rows.push(["通道 " + i, "", "--", "mA"]);
      }
      return rows;
    }

    function renderRows(rows) {
      /* 行数不变就只改单元格文本（每拍刷新零重建）。 */
      if (tbody.children.length !== rows.length) {
        HSF.replaceChildren(tbody);
        rows.forEach(function () {
          var tr = HSF.el("tr");
          for (var c = 0; c < HEADERS.length; c++) {
            tr.appendChild(HSF.el("td"));
          }
          tbody.appendChild(tr);
        });
      }
      for (var r = 0; r < rows.length; r++) {
        var tds = tbody.children[r].children;
        for (var c = 0; c < HEADERS.length; c++) {
          cellText(tds[c], rows[r][c]);
        }
      }
    }

    function renderAll() {
      var st = store.get("analog") || {};
      var channels = st.channels || [];
      var count = st.count || channels.length || 8;
      if (!st.fresh) {
        renderRows(emptyRows(count));
        setText(rawCaption, "无数据（未订阅或设备离线）");
        return;
      }
      var rows = [];
      for (var i = 0; i < count; i++) {
        var ch = channels[i] || {};
        var label = "通道 " + i;
        var name = ch.name || "";
        if (!ch.valid) {
          rows.push([label, name, "--", "mA"]);
          continue;
        }
        if (ch.eng !== null && ch.eng !== undefined) {
          /* 换算行：数值列两段纯数字（原始 mA + 工程量），单位列 mA / 单位。
           * 两段数字与两个单位一一对应（不把单位写进数值文字）。 */
          var value = (typeof ch.ma === "number" ? ch.ma.toFixed(3) : "--") +
            "    " + HSF.fmtG(ch.eng);
          var note = ch.note || "";
          if (note) value += "  " + note;
          rows.push([label, name, value,
                     ch.unit ? ("mA / " + ch.unit) : "mA"]);
        } else {
          rows.push([label, name,
                     typeof ch.ma === "number" ? ch.ma.toFixed(3) : "--",
                     "mA"]);
        }
      }
      renderRows(rows);
      var raws = [];
      for (var k = 0; k < count; k++) {
        raws.push("CH" + k + "=" + ((channels[k] || {}).rawMv || 0) + "mV");
      }
      setText(rawCaption, "原始寄存器：" + raws.join(" "));
    }

    unsubs.push(store.on("analog", renderAll));
    timers.push(setInterval(renderAll, 500));   /* 兜底重绘（同值早退） */

    renderAll();
    void ctx;

    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
    };
  }
})();
