/* -*- coding: utf-8 -*-
 * 页 15 网络设置：服务端地址/端口的只读展示 + 重连 + 本机 WiFi 区。
 *
 * [对齐参考 NetworkPage]（client_py/hsfsrv_client/pages_other.py:5895）：
 * 当前连接卡（服务器 host:port + 状态 已就绪/<链路态>）+ 重新连接按钮 +
 * 说明卡。web 版数据源 = store 的 link（webui 进程到上游 XLink 的链路态，
 * hub._link_json：state/ready/host/port）——「服务器」即上游 HSF 服务端，
 * 与参考客户端语义一致；浏览器侧的网页服务器地址见登录页只读框。
 *
 * [重连按钮] 参考行为 = 只给状态行提示（连接参数在登录窗，改地址要退出
 * 重登）；web 同款文案照抄，不造假「正在重连」。链路重连由 webui 进程
 * 自动做（xlink 指数退避），无需人为干预。
 *
 * [WiFi 区 2026-11-13 对照补齐] 参考端「WiFi 设置」卡读的是**本机**无线
 * 状态（适配器枚举 / 当前连接），走 C 动态库 hsb_bridge（框架 XNetworkWiFi，
 * 客户端侧封装见 client_py/hsfsrv_client/hsf_native.py）。
 * ⇒ 网页版由**网页服务器代查同一个动态库**（新增 GET /api/wifi，**只读**：
 * 适配器 + 当前连接），前端显示真实主机数据（适配器名 / SSID / 信号 /
 * 加密方式），不再是「--」占位。
 * [为什么操作仍置灰] 无线会话句柄在网页服务器进程里，浏览器一关就断 ⇒
 * 代持扫描/连接/断开等于让后台进程乱动本机网络；操作仍走桌面客户端
 * （按钮置灰 + 文案写明「网页版只读」）。动态库缺失时整区置灰并说明
 * 原因（与参考端「能力位不支持 ⇒ 整区置灰」同款口径）。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var activeUnmount = null;

  HSF.router.register(15, {
    title: "网络设置",
    topics: [],
    mount: function (host, ctx) { mountPage(host, ctx); },
    unmount: function () {
      if (activeUnmount) { activeUnmount(); activeUnmount = null; }
    },
  });

  function setText(node, text) {
    text = String(text == null ? "" : text);
    if (node.textContent !== text) node.textContent = text;
  }

  function mountPage(host, ctx) {
    var store = HSF.store;
    var unsubs = [];
    var timers = [];

    var hostLabel = HSF.el("div", { text: "--" });
    var stateLabel = HSF.el("div", { text: "--" });
    var btnReconnect = HSF.el("button.btn", { type: "button", text: "重新连接" });
    /* web 版说明进 tooltip（参考说明卡只有一段——pages_other.py:5823-5828）。 */
    btnReconnect.title = "「服务器」是网页服务器所连的上游 HSF 服务端（由 " +
      "webui 进程参数 --server/--port 决定）；浏览器侧的网页服务器地址在" +
      "登录页只读展示";

    /* ---- WiFi 设置卡（**只读**真实主机数据；操作仍走桌面客户端）----
     * [为什么不是「--」占位] 参考端这卡读的是本机无线状态（走 hsb_bridge，
     * pages_other.py:5899-5994）⇒ 网页端由**网页服务器**代查同一个动态库
     * （/api/wifi，只读：适配器枚举 + 当前连接），前端只读展示。
     * [为什么扫描/连接/断开仍置灰] 无线会话句柄在网页服务器进程里，浏览器
     * 一关会话就断；代持操作等于把本机网络交给一个后台进程乱动 ⇒ 只读，
     * 操作走桌面客户端（文案写明）。 */
    var adapterSel = HSF.el("select.input");
    adapterSel.setAttribute("aria-label", "无线适配器");
    adapterSel.appendChild(HSF.el("option", { text: "--" }));
    var scanBtn = HSF.el("button.btn", { type: "button", text: "扫描" });
    var netSel = HSF.el("select.input");
    netSel.setAttribute("aria-label", "无线网络");
    netSel.appendChild(HSF.el("option", { text: "--" }));
    var passEdit = HSF.el("input.input", {
      type: "password", placeholder: "加密网络必填",
    });
    var connectBtn = HSF.el("button.btn", { type: "button", text: "连接" });
    var disconnectBtn = HSF.el("button.btn", { type: "button", text: "断开" });
    var wifiInfo = HSF.el("div.caption", { text: "无线：未初始化" });
    /* 操作类控件置灰（本页只读）；读得到的下拉不禁用（真数据显示）。 */
    [scanBtn, netSel, passEdit, connectBtn, disconnectBtn]
      .forEach(function (w) { w.disabled = true; });
    scanBtn.title = "网页版只读：本机无线操作请用桌面客户端";
    connectBtn.title = "网页版只读：本机无线操作请用桌面客户端";
    disconnectBtn.title = "网页版只读：本机无线操作请用桌面客户端";
    var wifiCard = HSF.el("div.card", null, [
      HSF.el("div.card-title", { text: "WiFi 设置" }),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "适配器" }), adapterSel, scanBtn]),
      HSF.el("div.form-row", null, [
        HSF.el("span.lbl", { text: "网络" }), netSel,
        HSF.el("span.lbl", { text: "口令" }), passEdit,
        connectBtn, disconnectBtn]),
      wifiInfo,
    ]);

    var page = HSF.el("div.settings-page", null, [
      HSF.el("div.settings-head", null, [
        HSF.el("span.page-head-title", { text: "网络设置" }),
        HSF.el("span.page-head-sub.caption",
               { text: "服务端连接参数 + 本机 WiFi" }),
      ]),
      HSF.el("div.card", null, [
        HSF.el("div.card-title", { text: "当前连接" }),
        hostLabel, stateLabel,
        HSF.el("div.form-row", null, [btnReconnect]),
      ]),
      HSF.el("div.card", null, [
        HSF.el("div.card-title", { text: "说明" }),
        HSF.el("div.caption", {
          text: "连接参数在登录窗口填写。要改服务器地址，请退出后重新登录" +
                "（或用下方重新连接按钮回到登录窗）。",
        }),
      ]),
      wifiCard,
    ]);
    HSF.replaceChildren(host);
    host.appendChild(page);

    btnReconnect.addEventListener("click", function () {
      HSF.statusMessage("请退出程序后重新登录以更换服务器");
    });

    function render() {
      var link = store.get("link") || {};
      if (link.host !== undefined && link.host !== "" && link.port) {
        setText(hostLabel, "服务器：" + link.host + ":" + link.port);
      } else {
        setText(hostLabel, "服务器：--");
      }
      setText(stateLabel, "状态：" +
        (link.ready ? "已就绪" : (link.state || "未连接")));
    }

    /* ---- WiFi 只读数据（网页服务器代查 hsb_bridge） ---- */
    function fillAdapters(list) {
      var names = (list || []).map(function (a) {
        /* 参考端下拉项 = 「名 - 描述」（pages_other.py:5954 附近的口径）。 */
        return a.desc ? (a.name + " - " + a.desc) : (a.name || "");
      });
      var cur = adapterSel.value;
      HSF.replaceChildren(adapterSel);
      if (!names.length) {
        adapterSel.appendChild(HSF.el("option", { text: "--" }));
        adapterSel.disabled = true;
        return;
      }
      names.forEach(function (n) {
        adapterSel.appendChild(HSF.el("option", { text: n }));
      });
      adapterSel.disabled = false;
      if (cur && names.indexOf(cur) >= 0) adapterSel.value = cur;
    }

    function wifiStateText(doc) {
      /* 文案逐字照参考端（pages_other.py:6100-6116）。 */
      var conn = doc.connection || {};
      var state = conn.state || "idle";
      var ssid = conn.ssid;
      if (state === "connected" && ssid) {
        return "无线：已连接 " + ssid + "（信号 " + (conn.signal == null ? "--" : conn.signal) +
               "%，" + (conn.secure || "未知加密") + "）";
      }
      if (state === "connecting") return "无线：连接中…";
      if (state === "failed") return "无线：连接失败";
      return "无线：未连接";
    }

    function loadWifi() {
      HSF.api.wifi().then(function (doc) {
        if (!doc || !doc.ok) {
          setText(wifiInfo, "无线：状态查询失败（" +
                 ((doc && doc.err) || "未知原因") + "）");
          return;
        }
        if (!doc.available && !(doc.adapters && doc.adapters.length)) {
          setText(wifiInfo, "无线功能不可用：" +
                 (doc.err || "本机不支持（缺 hsb_bridge 动态库）"));
          fillAdapters([]);
          return;
        }
        fillAdapters(doc.adapters);
        setText(wifiInfo, wifiStateText(doc));
      }).catch(function (err) {
        setText(wifiInfo, "无线：状态查询失败（" +
               ((err && err.message) ? err.message : err) + "）");
      });
    }

    unsubs.push(store.on("link", render));
    timers.push(setInterval(render, 500));   /* 兜底（同值早退） */
    render();
    loadWifi();
    timers.push(setInterval(loadWifi, 4000));  /* 连接状态会变，定期刷新 */
    void ctx;

    activeUnmount = function () {
      timers.forEach(function (t) { clearInterval(t); });
      timers = [];
      unsubs.forEach(function (off) { off(); });
      unsubs = [];
    };
  }
})();
