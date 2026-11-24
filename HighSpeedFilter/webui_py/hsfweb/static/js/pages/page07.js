/* -*- coding: utf-8 -*-
 * 页 7 电动球阀：6 路阀卡片看板（每路 1 输出 + 2 到位反馈）。
 *
 * [对齐参考 pages_valve.ValvePage 逐项移植]：
 *   · 顶部横幅（**恒占位**，不用显隐改结构——横幅出现/消失会把整张卡片
 *     网格往下推）：控制禁用原因 / N 路点位未配置 / 最近告警「⚠ 阀名：文字」
 *     （有文案上色、无文案透明占位）；
 *   · 6 张卡片：顶部状态点 + 名称 + 状态徽标（状态未知/已关/已开/动作中/
 *     故障；未配置覆盖显示）、开到位/关到位反馈点（实心=到位）、
 *     开阀/关阀按钮（**已到位那一侧置灰**；动作中两侧都可点=反向）、
 *     提示行（服务端 text / 未配置提示 / 「点另一侧可反向」；恒占位不跳）；
 *   · 动作：POST /api/valve/control {index,on}；应答带 v{i}_cmdResult
 *     （issued/repeated/reversed/ignored → 四种状态栏文案，与同端口径）；
 *     入页拉一次快照；告警帧与状态帧同主题（hub 已拆成 alarm/valve 事件）。
 *
 * [告警只显示不判断] 超没超时（20s）、算不算故障全在服务端算；
 * 客户端只复述服务端给的文案（「阀名 告警：文字」→ 状态行 + 横幅）。
 *
 * [色板] 状态色义对齐参考（已开绿/已关灰蓝/动作中橙/故障红/未配置浅灰），
 * 色值取 theme.css 语义令牌（深色登记表），不散写十六进制字面量。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var VALVE_COUNT = 6;
  var COLUMNS = 3;
  /* [改名 2026-11-23 用户口径] 电动球阀1~6 ⇒ 工艺位号名（快照没带 name
   * 时的兜底；与 server_py / 桌面客户端 / 首页逐字一致）。 */
  var VALVE_FALLBACK_NAMES = ["产水阀(1)", "反洗进水阀(2)", "进水阀(3)",
                              "排空阀(4)", "反洗出水阀(5)", "顺洗出水阀(6)"];

  /* 状态 tone → 色（page00 TONE_COLOR 同一套令牌映射）。 */
  var TONE_COLOR = {
    unknown: "var(--offline)",
    closed: "var(--valve-closed)",   /* 已关灰蓝 #37474F（pages_valve.py:65） */
    open: "var(--ok)",
    moving: "var(--warn)",
    fault: "var(--fault-fill)",
    unconfigured: "var(--offline)",
  };

  /* 控制命令处置结果 → 状态栏文案（与服务端/client pages_valve 同口径）。 */
  var VALVE_CMD_TEXT = {
    issued: "命令已下发",
    repeated: "动作中，重复命令不重置计时",
    reversed: "已反向（重新计时）",
    ignored: "已在目标位置，无需动作",
  };

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  var activeUnmount = null;

  HSF.router.register(7, {
    title: "电动球阀",
    topics: function () {
      var t = (HSF.registry && HSF.registry.pageTopics &&
               HSF.registry.pageTopics["7"]) || [];
      return t.length ? t : [0x00010007];
    },
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  /** 单路阀卡片（参考 ValveCard 同构：徽标/反馈点/两按钮/恒占位提示行）。 */
  function makeValveCard(index, onRequest) {
    var dot = HSF.el("span.valve-dot");
    var name = HSF.el("span.valve-name", { text: VALVE_FALLBACK_NAMES[index] });
    var badge = HSF.el("span.valve-badge", { text: "未配置" });
    var top = HSF.el("div.valve-top", null, [dot, name, badge]);

    function fbRow(caption) {
      var d = HSF.el("span.fb-dot");
      var label = HSF.el("span.fb-label", { text: caption + "：--" });
      label.dataset.cap = caption;
      return { root: HSF.el("div.fb-row", null, [d, label]), dot: d,
               label: label };
    }
    var openFb = fbRow("开到位");
    var closeFb = fbRow("关到位");

    var btnOpen = HSF.el("button.btn", {
      type: "button", text: "开阀",
      onclick: function () { onRequest(index, true); },
    });
    var btnClose = HSF.el("button.btn", {
      type: "button", text: "关阀",
      onclick: function () { onRequest(index, false); },
    });
    var btns = HSF.el("div.valve-btns", null, [btnOpen, btnClose]);

    /* 提示行恒占位：行高按字体实测固定，空文案只清文字 ⇒ 按钮不跳。 */
    var hint = HSF.el("div.valve-hint", { text: "" });

    var root = HSF.el("div.card.valve-card", null,
                      [top, openFb.root, closeFb.root, btns, hint]);

    function setFb(row, on, known) {
      row.dot.classList.toggle("on", !!on);
      var cap = row.label.dataset.cap;
      setText(row.label, known ? (cap + "：" + (on ? "是" : "否"))
                               : (cap + "：--"));
    }

    return {
      root: root,
      btnOpen: btnOpen,
      btnClose: btnClose,
      nameText: function () { return name.textContent; },
      apply: function (item, blockedReason) {
        item = item || {};
        var ready = !!item.ready;
        var state = item.state | 0;
        var tone = item.tone || "unknown";

        setText(name, item.name || VALVE_FALLBACK_NAMES[index]);
        var stateText = ready ? (item.stateText || "状态未知") : "未配置";
        var color = ready ? (TONE_COLOR[tone] || TONE_COLOR.unknown)
                          : TONE_COLOR.unconfigured;
        setText(badge, stateText);
        badge.style.backgroundColor = color;
        /* 顶部状态点：开/动作中/故障=亮实心，关/未知=灰。 */
        dot.classList.toggle("lit",
          ready && (tone === "open" || tone === "moving" || tone === "fault"));

        var openKnown = ready;
        var closeKnown = ready;
        setFb(openFb, !!item.openFb, openKnown);
        setFb(closeFb, !!item.closeFb, closeKnown);

        /* 提示：优先服务端给的故障/状态说明；未配置给配置指引；
         * 动作中说清「点另一侧可反向」。 */
        var hint2 = item.text || "";
        if (!hint2 && !ready) {
          hint2 = "点位未配置（服务端 config 的 valves.items）";
        }
        if (ready && tone === "moving") {
          hint2 = (hint2 ? hint2 + " · " : "") + "点另一侧可反向";
        }
        setText(hint, hint2);

        /* 按钮可用性：已到位那一侧置灰（动作中两侧都可点=反向）。 */
        var canControl = ready && !blockedReason;
        btnOpen.disabled = !canControl || tone === "open";
        btnClose.disabled = !canControl || tone === "closed";
      },
    };
  }

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    /* ---- 顶部横幅（恒占位） ---- */
    var gate = HSF.el("div.valve-gate", { text: " " });

    /* ---- 卡片网格 ---- */
    var cards = [];
    var grid = HSF.el("div.valve-grid");
    for (var i = 0; i < VALVE_COUNT; i++) {
      var card = makeValveCard(i, requestControl);
      cards.push(card);
      grid.appendChild(card.root);
    }

    HSF.replaceChildren(host);
    host.appendChild(HSF.el("div.valve-page", null, [
      HSF.el("div.valve-head", null, [
        HSF.el("span.page-head-title", { text: "电动球阀" }),
        HSF.el("span.page-head-sub.caption",
               { text: VALVE_COUNT + " 路 · 1 路输出 + 2 路到位反馈" }),
      ]),
      gate, grid,
    ]));

    function connected() {
      return !!((store.get("link") || {}).ready);
    }
    function blockedReason() {
      return HSF.controlBlockedReason(
        ctx.canPerm(HSF.app.PERM.DEVICE_CONTROL));
    }

    /* ---- 横幅统一入口：有文案上色、无文案透明占位（布局恒定）。 ---- */
    function setGate(text) {
      text = text || "";
      gate.textContent = text || " ";
      gate.classList.toggle("alert", !!text);
    }

    /* ---- 控制（三态终态；按服务端处置结果给不同文案） ---- */
    function requestControl(index, wantOn) {
      var reason = blockedReason();
      if (reason) {
        HSF.statusMessage("禁止操作：" + reason);
        return;
      }
      var card = cards[index];
      var name = card.nameText();
      HSF.statusMessage(name + " → 正在" + (wantOn ? "开阀" : "关阀") + "…");
      HSF.api.valveControl({ index: index, on: !!wantOn })
        .then(function (doc) {
          if (doc && doc.ok) {
            var result = doc["v" + index + "_cmdResult"];
            var text = VALVE_CMD_TEXT[result] || "命令已受理";
            HSF.statusMessage(name + " " + text);
          } else {
            HSF.statusMessage(name + " 操作失败：" +
              ((doc && doc.err) || "未知原因"));
          }
          renderAll();
        });
    }

    /* ---- 告警（服务端判，客户端只复述） ---- */
    function onAlarm(doc) {
      if (!doc || doc.kind !== "valve") return;
      var name = doc.source || "阀";
      var text = doc.text || "";
      if (doc.level === "clear") {
        HSF.statusMessage(name + " 已恢复：" + text);
      } else {
        HSF.statusMessage(name + " 告警：" + text);
        /* 顶部横幅留一条最近的告警（不清除，直到下一条覆盖）。 */
        setGate("⚠ " + name + "：" + text);
      }
    }

    /* ---- 刷新（model 是推送落地处；控制应答也经 hub 落模广播） ---- */
    function renderAll() {
      var doc = store.get("valve") || {};
      var reason = blockedReason();
      var items = doc.valves || [];
      for (var i = 0; i < VALVE_COUNT; i++) {
        cards[i].apply(items[i], reason);
      }
      if (reason) {
        setGate("控制已禁用：" + reason);
      } else {
        var unconfigured = 0;
        items.forEach(function (item) {
          if (item && !item.ready) unconfigured++;
        });
        if (unconfigured) {
          setGate(unconfigured + " 路点位未配置（服务端 config 的 " +
                  "valves.items 里补 module/do/openDi/closeDi 即可启用）");
        } else {
          setGate("");
        }
      }
    }

    unsubs.push(store.on("valve", renderAll));
    unsubs.push(store.on("alarm", onAlarm));
    unsubs.push(store.on("link", renderAll));
    unsubs.push(store.on("mode", renderAll));

    /* 入页拉一次快照（不依赖周期推送的首帧时机）。 */
    if (connected()) {
      HSF.api.valveSnapshot().then(function () { renderAll(); });
    }
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
