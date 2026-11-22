/* -*- coding: utf-8 -*-
 * 底部状态栏：消息（ElideRight）+ 连接状态 + 右下角账户钮（弹菜单）。
 *
 * [对齐参考 StatusBar]（widgets.py:563-651）：
 *   · 不放时钟（顶栏已有，参考 2026-11-06 用户口径）；
 *   · message flex:1 + ellipsis = 参考「stretch=1 + ElideRight」；
 *   · 连接文本绿/红两色（#A8D8A8 / #FFB4B4）；
 *   · 账户钮三态文案：未登录 ▾ / 账号·角色 ▾；
 *   · 菜单两项「切换账户 / 退出登录」，在按钮**上方**弹（状态栏贴窗底，
 *     往下弹会被裁——widgets.py:637 同款）。
 * [消息行三态铁律] 动作反馈必须落「成功/失败/超时」终态文案，
 * 不许停在「正在…」；setMessage 是唯一写入点。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  function mount(root) {
    HSF.replaceChildren(root);

    var message = HSF.el("span.sb-message", { text: "就绪" });
    var conn = HSF.el("span.sb-conn.bad", { text: "未连接" });
    var accountBtn = HSF.el("button.sb-account", {
      type: "button", text: "未登录  \u25BE",
      onclick: showAccountMenu,
    });
    root.appendChild(message);
    root.appendChild(conn);
    root.appendChild(accountBtn);

    /* ---- 连接状态：上游链路（store.link）+ SSE 通道双来源 ----
     * [conn 只说连接态] 账号·角色一律走右侧账户钮（参考端 2026-11-11
     * 「conn 双写收拢」口径；main_window.py:294-296 set_connection("已连接")）。 */
    function applyLink(doc) {
      doc = doc || {};
      if (doc.ready) {
        conn.textContent = "已连接";
        conn.className = "sb-conn ok";
      } else {
        conn.textContent = doc.detail ? ("未连接：" + doc.detail) : "未连接服务端";
        conn.className = "sb-conn bad";
      }
    }
    /* [SSE 消息不覆盖业务终态] 登录成功/页面动作/「已载入服务端历史…」都是
     * 终态文案，不能被通道态消息顶掉——connecting/open 只在当前消息是
     * 本模块写的中间态（「正在连接…」「断开…」，即断开→恢复链）时才改写；
     * closed 是用户必须知道的真实事件，始终写（参考端历史页消息跨页持续
     * 保留，pages_other.py:1936 同口径）。 */
    var SSE_TRANSIENT = ["正在连接实时通道…", "实时通道断开"];
    function isTransientMsg() {
      return SSE_TRANSIENT.some(function (s) {
        return message.textContent.indexOf(s) === 0;
      });
    }
    function onSse(ev) {
      if (ev.state === "open") {
        if (isTransientMsg()) setMessage("实时通道已建立");
      } else if (ev.state === "connecting") {
        if (isTransientMsg()) setMessage("正在连接实时通道…");
      } else if (ev.state === "closed") {
        setMessage("实时通道断开（" + (ev.detail || "自动重连中") + "）");
      }
    }
    var unsubs = [
      HSF.store.on("link", applyLink),
      HSF.store.on("sse", onSse),
    ];
    applyLink(HSF.store.get("link"));

    /** 消息写入点（ElideRight 由 CSS .sb-message 承担）。
     *  [三态铁律] 动作反馈必须给「成功/失败/超时」终态，不许停在「正在…」。 */
    var listeners = [];            /* 页面旁听（页 10 客户端运行日志收流用） */
    function setMessage(text) {
      message.textContent = String(text == null ? "" : text);
      listeners.slice().forEach(function (fn) {
        try { fn(message.textContent); } catch (err) { /* 旁听者异常不碍状态栏 */ }
      });
    }
    HSF.statusMessage = setMessage;
    /* [mount 前的消息不丢] 登录成功写在壳装配之前（app.js 登录回调先于
     * _enterShell 调 statusMessage），此前的写入落进模块级缓存，这里补投。 */
    _pending.splice(0).forEach(setMessage);
    /** 订阅状态消息流（返回退订函数）；页面用它做本地收流，不改全局写入点。 */
    HSF.onStatusMessage = function (fn) {
      listeners.push(fn);
      return function () {
        var i = listeners.indexOf(fn);
        if (i >= 0) listeners.splice(i, 1);
      };
    };

    /* ---- 账户菜单 ---- */
    function showAccountMenu(ev) {
      var session = HSF.app.session || null;
      var logged = !!(session && session.user);
      var items = [];
      if (logged) {
        items.push({
          text: "切换账户",
          onPick: function () { HSF.app.switchAccount(); },
        });
        items.push({
          text: "退出登录",
          onPick: function () { HSF.app.logout(); },
        });
      } else {
        items.push({
          text: "登录",
          onPick: function () { HSF.app.switchAccount(); },
        });
      }
      HSF.popupMenu(accountBtn, items, "up");
      if (ev) ev.stopPropagation();
    }

    return {
      setMessage: setMessage,
      setAccount: function (user, roleName) {
        var text = (user ? (roleName ? user + "·" + roleName : user)
                         : "未登录") + "  \u25BE";
        accountBtn.textContent = text;
        accountBtn.title = user ? ("已登录：" + user +
                                  (roleName ? "（" + roleName + "）" : ""))
                               : "点击登录";
      },
      destroy: function () { unsubs.forEach(function (off) { off(); }); },
    };
  }

  HSF.statusbar = { mount: mount };
  /* HSF.statusMessage 由 mount() 装配时赋成真正的写入点；mount 前（登录
   * 成功写在壳装配之前）先缓存，mount 后补投——不丢「登录成功」终态。 */
  var _pending = [];
  HSF.statusMessage = function (t) { _pending.push(String(t == null ? "" : t)); };
})();
