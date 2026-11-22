/* -*- coding: utf-8 -*-
 * 登录视图（覆盖层）——用户名/口令/记住用户名，错误文案与参考 login.py 同款。
 *
 * [对齐参考 LoginDialog]（client_py/hsfsrv_client/login.py）：
 *   · 卡片：标题 APP_TITLE + 副题「服务端连接」+ 表单 + 记住勾选 +
 *     错误行（红字）+ 登录钮；
 *   · **缺省用户名为 admin（只读账户，最低权限）**——2026-11-16 用户定案：
 *     浏览器扫码/局域网打开网页先拿到最低权限；需要控制设备/改配置时由
 *     用户自己换成 oper/root（卡片下方提示行写明）。
 *   · 空口令补**该账户的缺省口令**（admin/oper/root 与其用户名同名），
 *     占位符随用户名变化：admin ⇒ 「口令（缺省 admin）」（login.py 同口径）；
 *   · 错误文案：「请填写用户名」（本地校验）/「登录失败：%s」（后端）；
 *     含「口令/password」的错误清空口令框并聚焦（login.py:219-221）；
 *   · 每次尝试前**必须清空**残留错误（login.py:199-205 实锅：残留 error
 *     会让"登录成功"被误判成失败——web 版同一条纪律）；
 *   · 忙态：按钮禁用 + 文案「连接中…」，动作必落终态（成功/失败）。
 * [web 差异] 服务器地址/端口=浏览器当前源（location.host），只读展示；
 * 后端上游由 webui 进程参数决定（--server/--port），浏览器改不了——
 * 不放可编辑假输入（不留"选了不兑现"的选项）。没有「离线浏览」：
 * web 所有数据端点都要会话（api.py:351 _require_session），放那个按钮
 * 就是死按钮。
 */
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  function mount(root, onSuccess) {
    HSF.replaceChildren(root);
    root.classList.remove("hidden");

    var memo = HSF.prefs.loginMemo();

    /* 服务器地址（只读展示：本页就来自这台 webui） */
    var locParts = window.location.host.split(":");
    var hostShown = locParts[0] || "127.0.0.1";
    var portShown = locParts[1] || (window.location.protocol === "https:" ? "443" : "80");

    /* [2026-11-16 用户定案「web 的默认登录账号为 admin、最低权限」]
     * 缺省预填 **admin**（只读账户）而不是 root：浏览器扫码/局域网打开网页
     * 的人先拿到最低权限，避免"默认就是管理员"的误操作面。需要控制设备
     * 或改配置时由用户自己换成 oper/root（提示行里写明）。 */
    var DEFAULT_USER = "admin";
    var userEdit = HSF.el("input.input", {
      type: "text", value: memo.user || DEFAULT_USER,
      placeholder: DEFAULT_USER, autocomplete: "username",
    });
    var passEdit = HSF.el("input.input", {
      type: "password", value: "",
      placeholder: "口令", autocomplete: "current-password",
    });
    var errorLabel = HSF.el("div.login-error", { text: "" });
    var remember = HSF.el("input.check", { type: "checkbox" });
    remember.checked = !!memo.remember;
    var loginBtn = HSF.el("button.btn.primary", {
      type: "button", text: "登录",
      style: { minWidth: "96px" },
    });

    function syncPassPlaceholder() {
      /* 占位符必须兑现：空口令补的就是该账户的缺省口令（见 doLogin 与
       * 桌面 login.py 同口径）。只对三个缺省账户承诺"缺省口令"。 */
      var u = userEdit.value.trim();
      var known = (u === "admin" || u === "oper" || u === "root");
      passEdit.placeholder = known ? ("口令（缺省 " + u + "）") : "口令";
    }
    syncPassPlaceholder();
    userEdit.addEventListener("input", syncPassPlaceholder);

    var form = HSF.el("div.login-form", null, [
      HSF.el("label", { text: "服务器地址" }),
      HSF.el("input.input", { type: "text", value: hostShown, readonly: "readonly", title: "本页所在网页服务器（浏览器当前源）" }),
      HSF.el("label", { text: "端口" }),
      HSF.el("input.input", { type: "text", value: portShown, readonly: "readonly", title: "网页服务器端口（上游由服务器进程参数决定）" }),
      HSF.el("label", { text: "用户名" }),
      userEdit,
      HSF.el("label", { text: "口令" }),
      passEdit,
      /* [2026-11-22 触达] 用 label 而不是 div：整行（含文字）都是命中区，
       * 触摸下不再只有一个 22px 小方框可点；行高由 CSS 给到 ≥44px。 */
      HSF.el("label.login-row-check", null, [
        remember,
        HSF.el("span.caption", { text: "记住服务器地址与用户名" }),
      ]),
    ]);

    var card = HSF.el("div.login-card", null, [
      /* 产品名与 app.js 的 APP_TITLE 同源（2026-11-19 改名
       * 「互嵌深层高速过滤机」）。 */
      HSF.el("div.login-title", { text: "互嵌深层高速过滤机" }),
      HSF.el("div.login-sub", { text: "服务端连接" }),
      form,
      errorLabel,
      HSF.el("div.caption.login-hint", {
        /* [拼接] JS **没有** C/Python 那种相邻字符串字面量隐式拼接——
         * 写成两行相邻字符串会整份文件 SyntaxError ⇒ HSF.login 未定义
         * ⇒ app.js 启动即抛错 ⇒ 页面全白（2026-11-17 实锅）。跨行一律 "+"。 */
        text: "缺省只读账户 admin / admin（查看与下载）；控制设备用 oper，"
              + "管理配置用 root。",
      }),
      HSF.el("div.login-btns", null, [loginBtn]),
    ]);
    root.appendChild(card);

    function setBusy(busy) {
      loginBtn.disabled = busy;
      userEdit.disabled = busy;
      passEdit.disabled = busy;
      loginBtn.textContent = busy ? "连接中…" : "登录";
    }

    /** [三态终态] 成功走 onSuccess；失败/超时都落在错误行，不悬在「连接中…」。 */
    async function doLogin() {
      var user = userEdit.value.trim();
      if (!user) {
        errorLabel.textContent = "请填写用户名";
        userEdit.focus();
        return;
      }
      /* 空口令补**该账户的缺省口令**（admin/oper/root 与其用户名同名）：
       * 占位符承诺了它（桌面 login.py 空口令补 root 的同款口径）。
       * [2026-11-16] 原来一律补 "root" ⇒ 缺省账户改成 admin 之后，空口令
       * 会拿 root 的口令去登 admin，必然报"口令错误"。 */
      var password = passEdit.value
        || ((user === "admin" || user === "oper" || user === "root")
            ? user : "root");
      errorLabel.textContent = "";               /* 残留错误必须每次清空 */
      setBusy(true);
      var doc;
      try {
        doc = await HSF.api.login(user, password);
      } finally {
        setBusy(false);
      }
      if (doc && doc.ok && doc.token) {
        HSF.prefs.saveLoginMemo(user, remember.checked);
        onSuccess(doc.token, doc.session || {}, null);
        return;
      }
      var err = (doc && doc.err) ? doc.err : "登录失败";
      errorLabel.textContent = "登录失败：" + err;
      if (/口令|password/i.test(err)) {
        passEdit.value = "";
        passEdit.focus();
      }
    }

    loginBtn.addEventListener("click", doLogin);
    [userEdit, passEdit].forEach(function (inp) {
      inp.addEventListener("keydown", function (ev) {
        if (ev.key === "Enter") doLogin();
      });
    });

    if (!userEdit.value) userEdit.focus();
    else passEdit.focus();

    return {
      /** 供 app.js 预填错误（如 authtoken 失效），并聚焦口令框。 */
      showError: function (msg) {
        errorLabel.textContent = msg || "";
      },
      destroy: function () {
        root.classList.add("hidden");
        HSF.replaceChildren(root);
      },
    };
  }

  HSF.login = { mount: mount };
})();
