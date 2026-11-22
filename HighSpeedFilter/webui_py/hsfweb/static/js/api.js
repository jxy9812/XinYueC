/* -*- coding: utf-8 -*-
 * 后端 API 封装：统一 Bearer 鉴权 / JSON 解析 / 401 处理。
 *
 * [对齐后端] 端点清单见 hsfweb/api.py Router._api（/api/login、/api/state、
 * /api/stream、/api/topics、/api/mode、/api/config、/api/history/* …）。
 * 鉴权：Authorization: Bearer <token>（后端 Request.bearer_token 也接受
 * hsf_token Cookie 兜底，见 httpd.py:105-110——刷新页面不掉线靠它）。
 *
 * [401 语义] 后端「未登录或会话已过期」回 401（api.py:355）——前端统一
 * 转成 unauthorized 事件，由 app 切回登录视图。503 = 后端活着但上游
 * XLink 未连（api.py:360「未连接服务端」），调用方按 err 文案显示。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  var token = "";
  var bus = HSF.makeBus();

  /*: 请求超时（毫秒）。
   *
   *: [为什么共享层就要有] 浏览器 fetch **没有内置超时**：网页服务器进程
   *: 假死或 TCP 半开时，await 永远挂着 ⇒ 页面的「正在启动…/正在开阀…/
   *: 正在写 DO…/正在导出…」停在「正在…」，违反「动作反馈必须成功/失败/
   *: 超时三选一终态」铁律（本项目纪律 §3 明写 fetch 封装要有超时终态）。
   *: 之前这个闸只做在远端设置页一处（page14_remote.js 的 withTimeout），
   *: 页 0/3/5/6/7/9/10 的动作全裸 await ⇒ 同样会永久挂住。
   *:
   *: [为什么 15s 而不是页 14 的 12s] 页 14 那条已验证的文案要原样保留：
   *: 它自己的 12s 本地闸先触发 ⇒ 本层的 15s 只是更外层的兜底，
   *: 不会抢在页 14 前面改写文案（口径不变）。
   *:
   *: [谁来覆盖] 长耗时操作（导出走的是页面里的原生 fetch，不经本封装）
   *: 不受影响；确需更长的调用可传 opts.timeoutMs。 */
  var REQUEST_TIMEOUT_MS = 15000;

  function setToken(t) {
    token = String(t || "");
  }

  function getToken() {
    return token;
  }

  /**
   * 统一请求。返回解析后的 JSON dict（非 2xx 也返回：
   * {ok:false, err, status}；网络异常返回 {ok:false, err:"网络错误…", status:0}）。
   * 401 额外发 unauthorized 事件。
   *
   * [超时终态] 超过 REQUEST_TIMEOUT_MS（15s）没应答 ⇒ 主动中止并回
   * {ok:false, err:"请求超时（15 秒无应答…）"}——**绝不让调用方永远挂着**
   * （「正在开阀…」停在「正在…」违反三态终态铁律）。
   */
  async function request(method, path, body, opts) {
    opts = opts || {};
    var headers = { "Accept": "application/json" };
    if (body !== undefined && body !== null) {
      headers["Content-Type"] = "application/json";
    }
    if (token && !opts.noAuth) {
      headers["Authorization"] = "Bearer " + token;
    }
    var timeoutMs = (opts.timeoutMs > 0) ? opts.timeoutMs : REQUEST_TIMEOUT_MS;
    /* AbortController：到点主动中止 fetch（省掉那条悬挂连接）。
     * [为什么要 Promise.race] 只靠 abort 是不够的——fetch 未必理会
     * signal（代理/补丁/某些 polyfill），那样 await 仍会永远挂着 ⇒
     * 页面停在「正在…」。所以**超时信封与 fetch 赛跑**：到点先给终态，
     * abort 只是顺手清理。 */
    var ctrl = null;
    var timer = null;
    if (typeof AbortController !== "undefined") {
      try { ctrl = new AbortController(); } catch (err) { ctrl = null; }
    }
    var timeoutPromise = new Promise(function (resolve) {
      timer = setTimeout(function () {
        if (ctrl) { try { ctrl.abort(); } catch (err) { /* 忽略 */ } }
        resolve({ __hsfTimeout: true });
      }, timeoutMs);
    });
    var resp;
    try {
      resp = await Promise.race([
        fetch(path, {
          method: method,
          headers: headers,
          body: body === undefined || body === null ? undefined : JSON.stringify(body),
          cache: "no-store",
          signal: ctrl ? ctrl.signal : undefined,
        }),
        timeoutPromise,
      ]);
    } catch (err) {
      return { ok: false, status: 0, err: "网络错误：" + (err && err.message ? err.message : err) };
    } finally {
      if (timer) clearTimeout(timer);
    }
    if (resp && resp.__hsfTimeout) {
      return { ok: false, status: 0,
               err: "请求超时（" + Math.round(timeoutMs / 1000) +
                    " 秒无应答，请检查连接后重试）" };
    }
    var doc = null;
    try {
      var text = await resp.text();
      if (text) doc = JSON.parse(text);
    } catch (err) {
      doc = null;
    }
    if (!resp.ok) {
      if (resp.status === 401) bus.emit("unauthorized", doc);
      return {
        ok: false,
        status: resp.status,
        err: (doc && doc.err) ? doc.err : ("HTTP " + resp.status),
        doc: doc,
      };
    }
    if (doc === null) return { ok: true, status: resp.status };
    doc.status = resp.status;
    return doc;
  }

  var api = {
    setToken: setToken,
    getToken: getToken,
    onUnauthorized: function (fn) { return bus.on("unauthorized", fn); },
    request: request,

    /* ---- 具名端点（页面工匠优先用这些，不散拼路径） ---- */
    ping: function () { return request("GET", "/api/ping", undefined, { noAuth: true }); },
    version: function () { return request("GET", "/api/version"); },
    pages: function () { return request("GET", "/api/pages"); },
    login: function (user, password) {
      return request("POST", "/api/login", { user: user, password: password }, { noAuth: true });
    },
    logout: function () { return request("POST", "/api/logout", {}); },
    me: function () { return request("GET", "/api/me"); },
    state: function () { return request("GET", "/api/state"); },
    status: function () { return request("GET", "/api/status"); },
    setTopics: function (connId, topics) {
      return request("POST", "/api/topics", { connId: connId, topics: topics });
    },
    getMode: function () { return request("GET", "/api/mode"); },
    setMode: function (mode) { return request("POST", "/api/mode", { mode: mode }); },
    autoControl: function (payload) { return request("POST", "/api/auto/control", payload); },
    configGet: function () { return request("GET", "/api/config"); },
    configSet: function (payload) { return request("POST", "/api/config", payload); },
    /* 变频器参数读/写（0x011B 转发；远端设置页「变频器参数」区块）。
     * GET = 读真实值（服务端现读驱动器，应答 params[].value 是回读工程值）；
     * POST {op:1, values:{寄存器地址:原始码}} = 写参数（管理员闸在后端，
     * RunLocked 由服务端终审拒绝、err 原样透传）。 */
    vfdParamsGet: function () { return request("GET", "/api/vfd/params"); },
    vfdParamsWrite: function (payload) {
      return request("POST", "/api/vfd/params", payload);
    },
    historyPage: function (qs) { return request("GET", "/api/history/page" + (qs || "")); },
    logPage: function (qs) { return request("GET", "/api/log/page" + (qs || "")); },
    serverLog: function (qs) { return request("GET", "/api/serverlog" + (qs || "")); },
    alarms: function (qs) { return request("GET", "/api/alarms" + (qs || "")); },
    /* 服务端报警历史分页（0x011A 转发；报警页主数据源，2026-11-17 对齐
     * 桌面客户端）。/api/alarms（本地边沿存档）保留作老服务端兜底。 */
    alarmsPage: function (qs) { return request("GET", "/api/alarms/page" + (qs || "")); },
    /* 本机无线**只读**快照（适配器 + 当前连接；网页服务器代查 hsb_bridge） */
    wifi: function () { return request("GET", "/api/wifi"); },

    /* ---- 设备监视四页（页 3~7）用：控制/快照（后端 api.py 同名端点） ---- */
    vfdControl: function (payload) { return request("POST", "/api/vfd/control", payload); },
    vfdSnapshot: function () { return request("POST", "/api/vfd/snapshot", {}); },
    jogStart: function (payload) { return request("POST", "/api/jog/start", payload); },
    jogStop: function () { return request("POST", "/api/jog/stop", {}); },
    digitalSnapshot: function (dev) {
      return request("POST", "/api/digital/snapshot", { dev: dev });
    },
    digitalControl: function (payload) { return request("POST", "/api/digital/control", payload); },
    valveSnapshot: function () { return request("POST", "/api/valve/snapshot", {}); },
    valveControl: function (payload) { return request("POST", "/api/valve/control", payload); },
    pumpSnapshot: function () { return request("POST", "/api/pump/snapshot", {}); },
    pumpControl: function (payload) { return request("POST", "/api/pump/control", payload); },
    turbiditySnapshot: function () { return request("POST", "/api/turbidity/snapshot", {}); },
    flowmeterSnapshot: function () { return request("POST", "/api/flowmeter/snapshot", {}); },
  };

  HSF.api = api;
})();
