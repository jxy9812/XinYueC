# 页面模块约定（static/js/pages/）

给四组页面工匠的接入约定。外壳（布局/路由/登录/SSE/store/主题）已就绪，
**每页一个模块文件，注册进路由表即可被导航/深链/标题栏自动接管**。

## 1. 文件与命名

- 一页一个文件：`pageNN.js`（NN=两位页号，如 `page03.js` 数字量页）。
- 文件尾注册：

```js
(function () {
  "use strict";
  var HSF = (window.HSF = window.HSF || {});

  HSF.router.register(3, {                    // 页号与 pages_registry 一致
    title: "数字量",                           // 标题栏/浏览器标签用
    topics: [0x00010006],                      // 本页需要的推送主题（0x00010000+n）
    mount: function (host, ctx) { /* 建页面 DOM，host 已清空 */ },
    unmount: function () { /* 清定时器/退订；框架层不代劳 */ },
  });
})();
```

- 别忘了把 `<script src="js/pages/pageNN.js"></script>` 加进
  `index.html`（放在 `menu-page.js` 之后、`app.js` 之前）。

## 2. 页号与命名事实源

页号/页名/导航树/每页主题**不许在页面里硬编码**：

- 页名与导航：`HSF.registry.names[page]`、`HSF.registry.nav`
  （来自 `/api/pages`，后端 `api.py` 的 `PAGE_NAMES/NAV_TREE` 单一事实源）；
- 本页需要的主题：`HSF.registry.pageTopics[String(page)]`（后端
  `PAGE_TOPICS` 已列全，页面模块的 `topics` 直接引用它）；
- 权限位：`HSF.app.PERM.*` + `HSF.app.canPerm(...)`（位值来自
  `/api/pages.perms` 同源表）。

## 3. 数据面（ctx 与 store）

`mount(host, ctx)` 的 ctx = `HSF.pageContext()`（app.js 提供）：

| 用途 | 入口 |
|---|---|
| 读快照 | `HSF.store.get("vfd"/"temphum"/"analog"/"flowmeter"/"turbidity"/"pump"/"digital"/"valve"/"health"/"mode")` |
| 订阅增量 | `var off = HSF.store.on("vfd", fn)`；`unmount` 里 `off()` |
| REST | `HSF.api.state()/historyPage(qs)/logPage(qs)/configGet()/autoControl(payload)…`（api.js 具名端点优先，不散拼路径） |
| 控制动作 | `HSF.api.setMode/autoControl/…`；返回 `{ok, err, …}` |
| 反馈文案 | `HSF.statusMessage("…")`（状态栏左侧行，ElideRight） |
| 偏好 | `HSF.prefs.get/set`（localStorage，前缀 `hsfweb.`；不要另立存储） |

**动作三态铁律**：任何控制类动作（阀/泵/模式/自动/点动）必须有
成功/失败/超时三选一终态反馈（状态行 + 页面内状态区），不许停在「正在…」。
后端包络：`ok:true` 成功；`ok:false + err` 失败（err 是给用户看的原文）；
fetch 异常已由 api.js 归一为 `{ok:false, err:"网络错误…"}`；
**超时终态在共享层**（api.js 的 `request()`：15s 赛跑 ⇒
`{ok:false, err:"请求超时（15 秒无应答…）"}`）⇒ 页内**不要**再自己写
超时闸（页 14 历史上做过一个页内局部闸，现已由共享层兜底）。

**「模式」下拉要发 op=5（只选模式）**：改下拉即发
`HSF.api.autoControl({op:5, autoMode:<id>})`——服务端据此**记粘性 + 落盘**
（现场物理启动按钮用「上次选中的」；不同步 ⇒ 服务端粘性为空 ⇒ 物理启动键
被拒）。口径照参考客户端 `bridge.send_auto_select`
（client_py/hsfsrv_client/bridge.py:560-579）。**已知差异**：C 端服务端
只认 op 1..4 ⇒ 对 C sim 下拉会回「模式选择失败：非法 op 操作字」；
参考客户端对 C sim **同样**失败 ⇒ 两端一致，如实报出即可。

**权限置灰**：控制控件按 `HSF.app.canPerm(HSF.app.PERM.DEVICE_CONTROL)`
等判缺省态；后端仍终审（web 网关预检 + 服务端闸门，见 hsfweb/auth.py）。

## 4. DOM 与样式

- 用 `HSF.el(tag, attrs, children)` 建树（util.js；支持 `"div.card#id"`
  简写、`text`、`dataset`、`on`、`style` 对象）；`mount` 入口先
  `HSF.replaceChildren(host)`（router 已清空，兜底无害）；
- 颜色/间距/字号一律取 `css/theme.css` 的 CSS 变量（`var(--panel-bg)`…），
  **不许散写十六进制字面量**；语义色深套：`--ok/--warn/--fault/--offline`；
- 现成类：`.card/.card-title/.caption/.btn/.input/.check/.table/.dot`；
- **字号分两族**（对齐参考端 theme.py 的两套因子，2026-11-13 对照后定的）：
  · **随界面缩放走的正文族**用 `rem`——走 theme.css 的三个变量
    `--font-app`（20px@14＝参考 app 字体 15pt@1.5）/ `--font-check`
    （22px@14＝工具条勾选项）/ `--font-caption`（18px@14＝
    `caption_label_qss`＝`font_px(12)`）；根字号 = 14px × pct/150，
    缺省 pct=150（参考端 `theme.ui_scale()` 缺省 1.5）⇒ 根 14px；
  · **参考端本来就是字面 px 的那族**（卡标题 19px / 页头标题 26px /
    数值读数 40px / 单位 22px / 菜单按钮 24px…）**照抄 px**，别改成 rem
    ——参考端它们不随界面缩放变化，改成 rem 会让缩放时标题跟着放大、
    与参考端对不上（这是 2026-11-13 对照实测后定的口径）；
- 全局缩放/紧凑档自动生效（prefs.uiScalePct → 根字号；body[data-density]），
  页面无须自己处理，但**不要**把布局钉死成固定像素。

## 5. 生命周期与卫生

- 定时器/事件监听/SSE 订阅在 `unmount` 里全部清掉（router 会调用）；
- 不污染全局：页面私有函数放 IIFE 内；共享能力进 util.js，不复制粘贴；
- **不留占位控件与"选了不兑现"的选项**：没接后端的下拉/按钮不要上页面；
- **不造假数据**：无数据显示 `--`（`HSF.fmt`），不编造 0；
- 临时诊断打印（console.log）验收完必须删除。

## 6. 自检（每页交付前必跑）

```bash
# 常驻服务：C sim 6100 + webui（**端口运行期解析**，本机实测 8085；
# 8080 上跑着别人的程序 read_analog_gui.py，不是 hsfweb，别打它也别杀）
curl -s http://127.0.0.1:8085/api/ping
# 后端冒烟必须仍绿：
python verify/webui/smoke_backend.py
# 无头截图自证（先 POST /api/login 拿 token）：
msedge --headless=new --disable-gpu --screenshot=out.png \
  --window-size=1280,800 --virtual-time-budget=8000 \
  "http://127.0.0.1:8085/?authtoken=<tok>#/page/<页号>"
```

页面状态（在线/离线/无数据三种）都要看得过去：断上游时数值 `--`、
控制控件按权限与「未连接」置灰，不崩不白屏。
