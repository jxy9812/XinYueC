# -*- coding: utf-8 -*-
"""网页版「本地设置 + 悬浮窗设置 + 窗口设置」三页合并的功能闸门。

    python verify/webui/settings_merge_check.py            # 自动解析网页端口
    python verify/webui/settings_merge_check.py --web-port 8085

[为什么要有这个脚本] 用户定案「三页合并后要测试功能是否正常」——**不许只
看控件在不在，每个项都要验「点了真生效」**。本脚本逐项断言合并页的每个
功能项「存在 + 生效」，并对邻页（远端设置/网络设置）做「合并不许误伤」的
回归断言。退出码 0 = 全绿，1 = 有断言失败。

[覆盖的断言（每条一个独立 check）]
    A 合并页结构：页头「本地设置」+ 三个分区（界面设置/悬浮窗/窗口与屏幕）
      + 6 张卡（显示/数据刷新/显示元素/标题栏内容/尺寸/屏幕休眠/信息）。
    B 页表与导航：/api/pages 页数 16、页名 16 项、导航 15 项、无 16/17 号、
      设置菜单只有 3 个入口；DOM 导航树无「悬浮窗设置/窗口设置」。
    C 界面缩放：改下拉 → 根 computed 字号变、正文字号变、偏好落盘。
    D 强制紧凑：勾选 → body[data-density=compact]；取消 → 回落 normal。
    E 界面刷新周期：改数值 → 落盘 + prefs.refreshMs() 读到新值（首页重绘
      节拍读的就是它，page00_home.js:1139）。
    F 标题栏/状态栏/导航显隐：取消勾选 → 对应 DOM 真不可见（display:none
      + 高度 0）+ body.hide-* 类生效；再勾回 → 恢复。
    G 标题栏内容（时钟/温湿度）：取消勾选 → 标题栏内该块真隐藏。
    H 窗口尺寸记忆：选 1024×600 → 点「应用」→ body.viewport-fixed +
      --app-w=1024px + 落盘；**刷新页面后仍在**（prefs.applyAll 沿用）。
    I 屏幕休眠区：状态行/自动熄屏下拉/系统挂起下拉(置灰)/唤醒来源三勾/四个
      按钮齐全；能力不支持时整块置灰且文案可解释；能力支持时「保存策略」
      落盘 + 回读回显，四个按钮各有终态消息。
    J 邻页未受影响：页 14 远端设置 / 页 15 网络设置仍能挂载、页 12 设置菜单
      仍是 3 个入口。
    K 每页条数：页 9 工具条「每页 N 条」下拉改档 → 控件文案变 + 落盘 +
      分页标签与新档位自洽。
    L 旧页号 16/17 深链 → 如实提示「已并入本地设置页」，不装死页。

[依赖] 纯标准库 + 同目录 cdp_client.py（CDP 客户端，标准库实现）；
Edge headless（--remote-debugging-port）。
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cdp_client  # noqa: E402  （同目录的 CDP 客户端）

HOST = "127.0.0.1"
DEFAULT_WEB_PORTS = (8085, 8090, 8080)

EDGE_CANDIDATES = [
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
]

SHOT_DIR = os.path.join(HERE, "shots", "merge")

# ---------------------------------------------------------------- JS 助手 --
JS_HELP = r"""
function q(sel){ return document.querySelector(sel); }
function qa(sel){ return Array.prototype.slice.call(document.querySelectorAll(sel)); }
function cardByTitle(t){
  var cs = qa('#page-host .card');
  for (var i=0;i<cs.length;i++){
    var tt = cs[i].querySelector('.card-title');
    if (tt && tt.textContent.trim() === t) return cs[i];
  }
  return null;
}
function rowByLabel(t){
  var rs = qa('#page-host .form-row');
  for (var i=0;i<rs.length;i++){
    var lb = rs[i].querySelector('.lbl');
    if (lb && lb.textContent.trim() === t) return rs[i];
  }
  return null;
}
function checkByText(t){
  var rs = qa('#page-host label.check-row');
  for (var i=0;i<rs.length;i++){
    var sp = rs[i].querySelector('span');
    if (sp && sp.textContent.indexOf(t) >= 0) return rs[i].querySelector('input.check');
  }
  return null;
}
function btnByText(t){
  var bs = qa('#page-host button');
  for (var i=0;i<bs.length;i++){
    if (bs[i].textContent.trim() === t) return bs[i];
  }
  return null;
}
function fire(el, type){ el.dispatchEvent(new Event(type, {bubbles:true})); }
function ls(k){ return localStorage.getItem('hsfweb.' + k); }
function hidden(el){
  if (!el) return true;
  var st = getComputedStyle(el);
  return st.display === 'none' || st.visibility === 'hidden' ||
         el.getBoundingClientRect().height <= 0.5;
}
"""


def js(body):
    """把一段函数体包成可求值的 IIFE 表达式。"""
    return JS_HELP + "\n(function(){\n" + body + "\n})()"


# ------------------------------------------------------------------ 工具 --
def http_json(port, path, token="", method="GET", payload=None, timeout=10.0):
    url = "http://%s:%d%s" % (HOST, port, path)
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def probe_web(port, timeout=2.0):
    try:
        with urllib.request.urlopen("http://%s:%d/api/ping" % (HOST, port),
                                    timeout=timeout) as resp:
            doc = json.loads(resp.read().decode("utf-8"))
        return doc.get("server") == "hsf-webui"
    except Exception:                                    # noqa: BLE001
        return False


def resolve_web_port(explicit=None):
    """解析网页端口：显式 > 环境变量 > 候选端口探活（/api/ping 自报身份）。"""
    if explicit:
        return int(explicit)
    env = os.environ.get("HSF_WEB_PORT")
    if env and probe_web(int(env)):
        return int(env)
    for p in DEFAULT_WEB_PORTS:
        if probe_web(p):
            return p
    return 0


def find_edge():
    for cand in EDGE_CANDIDATES:
        if os.path.isfile(cand):
            return cand
    return None


def free_port():
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def _edge_pids_for_profile(profile):
    """正在占用这个临时 profile 的 Edge 进程 PID（headless 是分离子进程，
    启动器退出后它还在跑 ⇒ 直接 rmtree 会因文件被占用而失败）。"""
    script = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.Name -eq 'msedge.exe' -and $_.CommandLine -and "
        "$_.CommandLine.Contains('%s') } | "
        "ForEach-Object { $_.ProcessId.ToString() }"
    ) % profile
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                         capture_output=True).stdout or b""
    pids = []
    for line in (out or b"").decode("utf-8", "replace").splitlines():
        line = line.strip()
        if line.isdigit():
            pids.append(int(line))
    return pids


def cleanup_profile(profile):
    """删掉临时 Edge profile：先杀占用它的 Edge 进程，再 rmtree 重试。"""
    for pid in _edge_pids_for_profile(profile):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                       capture_output=True,
                       env=dict(os.environ, MSYS_NO_PATHCONV="1"))
    for _ in range(6):
        shutil.rmtree(profile, ignore_errors=True)
        if not os.path.isdir(profile):
            return True
        time.sleep(0.5)
    return not os.path.isdir(profile)


def login(port, user="root", password="root", tries=5):
    last = ""
    for i in range(tries):
        try:
            doc = http_json(port, "/api/login", method="POST",
                            payload={"user": user, "password": password})
            tok = doc.get("token", "")
            if tok:
                return tok
            last = "应答无 token：%r" % (doc.get("error"),)
        except Exception as exc:                         # noqa: BLE001
            last = str(exc)
        time.sleep(2)
    print("[merge] 登录失败：%s" % last, file=sys.stderr)
    return ""


# --------------------------------------------------------------- 检查器 --
class Checker(object):
    def __init__(self, cdp):
        self.cdp = cdp
        self.results = []

    def run(self, name, expression, extra_ok=None, extra_detail=""):
        try:
            doc = self.cdp.evaluate(expression)
        except Exception as exc:                         # noqa: BLE001
            doc = {"ok": False, "detail": "求值异常：%s" % exc}
        if not isinstance(doc, dict):
            doc = {"ok": False, "detail": "求值返回非对象：%r" % (doc,)}
        ok = bool(doc.get("ok"))
        detail = str(doc.get("detail", ""))
        if ok and extra_ok is not None and not extra_ok:
            ok = False
            detail = (detail + " | " + extra_detail).strip(" |")
        self.results.append((name, ok, detail))
        print("[%s] %s%s" % ("PASS" if ok else "FAIL", name,
                             ("  —— " + detail) if detail else ""))
        return ok

    def run_py(self, name, ok, detail):
        """纯 Python 侧断言（HTTP 拿到的数据不必绕浏览器）。"""
        ok = bool(ok)
        self.results.append((name, ok, str(detail)))
        print("[%s] %s%s" % ("PASS" if ok else "FAIL", name,
                             ("  —— " + str(detail)) if detail else ""))
        return ok

    @property
    def failed(self):
        return [r for r in self.results if not r[1]]


def wait_page_ready(cdp, timeout_s=20.0):
    """等页面内容稳定（页容器非空 + 连续两拍不变）。"""
    deadline = time.time() + timeout_s
    last = -1
    stable = 0
    while time.time() < deadline:
        n = cdp.evaluate(
            "(document.getElementById('page-host')||{}).childElementCount")
        n = int(n) if isinstance(n, (int, float)) else -1
        if n > 0 and n == last:
            stable += 1
            if stable >= 2:
                return True
        else:
            stable = 0
        last = n
        time.sleep(0.3)
    return False


def goto_page(cdp, page, timeout_s=20.0):
    cdp.evaluate("location.hash = '#/page/%d'" % page)
    time.sleep(0.4)
    return wait_page_ready(cdp, timeout_s)


def shot(cdp, path, beyond=False):
    params = {"format": "png"}
    if beyond:
        params["captureBeyondViewport"] = True
    res = cdp.call("Page.captureScreenshot", params, timeout=60.0)
    data = res.get("data")
    if not data:
        raise RuntimeError("截图没给数据：%s" % path)
    import base64
    with open(path, "wb") as fh:
        fh.write(base64.b64decode(data))
    return os.path.getsize(path)


# ----------------------------------------------------------------- 主流程 --
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--web-port", default="")
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--shot-dir", default=SHOT_DIR)
    ap.add_argument("--no-shots", action="store_true")
    args = ap.parse_args()

    port = resolve_web_port(args.web_port)
    if not port:
        print("[merge] 找不到在跑的 hsfweb（试过 %s）" % (DEFAULT_WEB_PORTS,),
              file=sys.stderr)
        return 2
    print("[merge] 网页端口 = %d" % port)

    edge = find_edge()
    if edge is None:
        print("[merge] 找不到 msedge.exe", file=sys.stderr)
        return 2

    token = login(port, args.user, args.password)
    if not token:
        return 2

    # ---- 备份本机偏好（测完原样恢复，不给后续阶段留副作用） ----
    snap = http_json(port, "/api/pages")
    print("[merge] /api/pages count=%s names=%d" %
          (snap.get("count"), len(snap.get("names") or [])))

    profile = tempfile.mkdtemp(prefix="edge_merge_")
    dbg_port = free_port()
    url0 = "http://%s:%d/?authtoken=%s#/page/13" % (HOST, port, token)
    cmd = [edge, "--headless=new", "--disable-gpu", "--no-first-run",
           "--user-data-dir=" + profile,
           "--remote-debugging-port=%d" % dbg_port,
           "--window-size=1280,800", url0]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    cdp = None
    checker = Checker(None)
    try:
        ws_url = cdp_client.wait_for_target(dbg_port, timeout_s=45.0)
        cdp = cdp_client.Cdp(ws_url, timeout=60.0)
        checker.cdp = cdp
        cdp.call("Page.enable")
        cdp.call("Runtime.enable")
        cdp.call("Emulation.setDeviceMetricsOverride",
                 {"width": 1280, "height": 800, "deviceScaleFactor": 1,
                  "mobile": False})
        wait_page_ready(cdp, 25.0)

        # ============ A 合并页结构 ============
        checker.run(
            "A1 合并页页头=本地设置 + 三分区齐全",
            js("""
var secs = qa('#page-host .setting-section-title')
            .map(function(e){ return e.textContent.trim(); });
var head = q('#page-host .page-head-title');
var cards = qa('#page-host .card .card-title')
            .map(function(e){ return e.textContent.trim(); });
var want = ['显示','数据刷新','每页条数','显示元素','标题栏内容','尺寸',
            '屏幕休眠','信息'];
var missing = [];
want.forEach(function(w){ if (cards.indexOf(w) < 0) missing.push(w); });
return {ok: !!head && head.textContent.trim() === '本地设置'
            && secs.length === 3 && secs[0] === '界面设置'
            && secs[1] === '悬浮窗' && secs[2] === '窗口与屏幕'
            && missing.length === 0,
        detail: JSON.stringify({head: head && head.textContent.trim(),
            secs: secs, cards: cards, missing: missing})};
"""))

        # ============ B 页表与导航 ============
        names = snap.get("names") or []
        nav = snap.get("nav") or []
        sub12 = (snap.get("subMenus") or {}).get("12") or []
        checker.run_py(
            "B1 后端页表=19 页 / 页名 19 项 / 导航 17 项 / 无 16-17 旧残骸",
            snap.get("count") == 19 and len(names) == 19 and len(nav) == 17
            and names[13] == "本地设置"
            and names[16] == "Web 设置" and names[17] == "相机"
            and names[18] == "流量"
            and "悬浮窗设置" not in names and "窗口设置" not in names,
            "count=%s names=%d nav=%d names[13]=%r names[16:19]=%r" %
            (snap.get("count"), len(names), len(nav),
             names[13] if len(names) > 13 else None, names[16:19]))
        checker.run_py(
            "B2 设置菜单（页 12）只剩 3 个入口",
            len(sub12) == 3
            and [e["name"] for e in sub12] == ["本地设置", "远端设置", "网络设置"],
            "subMenus[12]=%r" % (sub12,))
        checker.run(
            "B3 DOM 导航树无「悬浮窗设置/窗口设置」条目",
            js("""
var txt = q('#nav-panel') ? q('#nav-panel').textContent : '';
return {ok: txt.indexOf('悬浮窗设置') < 0 && txt.indexOf('窗口设置') < 0,
        detail: 'nav-panel 文本=' + JSON.stringify(txt)};
"""))

        # ============ C 界面缩放 ============
        checker.run(
            "C1 界面缩放：改下拉 → 根/正文字号变 + 落盘",
            js("""
var sel = q('#page-host select[aria-label="界面缩放"]');
if (!sel) return {ok:false, detail:'找不到界面缩放下拉'};
var rootBefore = getComputedStyle(document.documentElement).fontSize;
var lbl = q('#page-host .form-row .lbl');
var lblBefore = lbl ? getComputedStyle(lbl).fontSize : '';
var cur = parseInt(sel.value, 10);
var target = (cur === 100) ? 150 : 100;
sel.value = String(target); fire(sel, 'change');
var rootAfter = getComputedStyle(document.documentElement).fontSize;
var lblAfter = lbl ? getComputedStyle(lbl).fontSize : '';
var stored = ls('uiScalePct');
var msg = q('#status-bar') ? q('#status-bar').textContent : '';
return {ok: rootAfter !== rootBefore && lblAfter !== lblBefore
            && stored === String(target) && msg.indexOf('界面缩放') >= 0,
        detail: JSON.stringify({rootBefore: rootBefore, rootAfter: rootAfter,
            lblBefore: lblBefore, lblAfter: lblAfter,
            stored: stored, target: target, status: msg.slice(-80)})};
"""))

        # ============ D 强制紧凑 ============
        checker.run(
            "D1 强制紧凑：勾选 → data-density=compact，取消 → 回落",
            js("""
var cb = checkByText('紧凑模式');
if (!cb) return {ok:false, detail:'找不到紧凑模式勾选框'};
cb.checked = true; fire(cb, 'change');
var d1 = document.body.getAttribute('data-density');
var stored1 = ls('forceCompact');
cb.checked = false; fire(cb, 'change');
var d2 = document.body.getAttribute('data-density');
return {ok: d1 === 'compact' && d2 === 'normal' && stored1 === '1',
        detail: JSON.stringify({on: d1, off: d2, stored: stored1})};
"""))

        # ============ E 界面刷新周期 / 趋势刷新间隔 ============
        checker.run(
            "E1 界面刷新周期：改数值 → 落盘 + prefs 读到新值",
            js("""
var row = rowByLabel('界面刷新周期');
if (!row) return {ok:false, detail:'找不到「界面刷新周期」行'};
var inp = row.querySelector('input');
inp.value = '800'; fire(inp, 'change');
var stored = ls('refreshMs');
var read = HSF.prefs.refreshMs();
return {ok: stored === '800' && read === 800,
        detail: JSON.stringify({stored: stored, read: read})};
"""))
        checker.run(
            "E2 趋势刷新间隔：合并页改档 → 落盘（与页 5/6 趋势卡同一份偏好键）",
            js("""
var sel = q('#page-host select[aria-label="趋势刷新间隔"]');
if (!sel) return {ok:false, detail:'找不到「趋势刷新间隔」下拉'};
var opts = Array.prototype.slice.call(sel.options)
    .map(function(o){ return o.value; });
var target = null;
Array.prototype.forEach.call(sel.options, function(o){
  if (o.value === '1000') target = o.value;
});
if (!target) return {ok:false, detail:'档位里没有 1000ms：' + JSON.stringify(opts)};
sel.value = target; fire(sel, 'change');
var stored = ls('trendSampleMs');
var msg = q('#status-bar') ? q('#status-bar').textContent : '';
return {ok: stored === '1000' && msg.indexOf('趋势刷新间隔') >= 0,
        detail: JSON.stringify({opts: opts, stored: stored,
            status: msg.slice(-80)})};
"""))

        # ---- 趋势间隔真兑现：页 5 趋势卡的「刷新」下拉读到同一份偏好 ----
        goto_page(cdp, 5, timeout_s=30.0)
        checker.run(
            "E3 趋势刷新间隔真兑现：页 5 趋势卡「刷新」下拉 = 合并页设的值",
            js("""
var sel = q('#page-host select.trend-sample');
if (!sel) return {ok:false, detail:'页 5 没找到趋势卡的「刷新」下拉'};
return {ok: sel.value === '1000' && ls('trendSampleMs') === '1000',
        detail: JSON.stringify({value: sel.value,
            text: sel.options[sel.selectedIndex].textContent.trim(),
            stored: ls('trendSampleMs')})};
"""))

        # ---- 每页条数卡（合并页总入口）落盘 ----
        goto_page(cdp, 13)
        checker.run(
            "M1 每页条数卡：历史数据/运行日志 两行改档 → 落盘（与页 9/10 同一份偏好）",
            js("""
var h = q('#page-host select[aria-label="历史数据每页条数"]');
var l = q('#page-host select[aria-label="运行日志每页条数"]');
if (!h || !l) return {ok:false, detail:'找不到「每页条数」卡的两个下拉'};
h.value = '10'; fire(h, 'change');
l.value = '50'; fire(l, 'change');
var sh = ls('histPageSize'), sl = ls('logPageSize');
var msg = q('#status-bar') ? q('#status-bar').textContent : '';
return {ok: sh === '10' && sl === '50'
            && msg.indexOf('每页') >= 0,
        detail: JSON.stringify({hist: sh, log: sl,
            histText: h.options[h.selectedIndex].textContent.trim(),
            logText: l.options[l.selectedIndex].textContent.trim(),
            status: msg.slice(-80)})};
"""))
        goto_page(cdp, 9, timeout_s=30.0)
        checker.run(
            "M2 每页条数真兑现：页 9 工具条「每页」下拉 = 合并页设的值",
            js("""
var sel = null;
qa('#page-host select').forEach(function(s){
  var t = s.options.length ? s.options[0].textContent.trim() : '';
  if (/条$/.test(t)) sel = s;
});
if (!sel) return {ok:false, detail:'页 9 没找到「每页 N 条」下拉'};
var txt = sel.options[sel.selectedIndex].textContent.trim();
return {ok: sel.value === '10' && txt === '10 条',
        detail: JSON.stringify({value: sel.value, text: txt,
            stored: ls('histPageSize')})};
"""))
        goto_page(cdp, 10, timeout_s=30.0)
        checker.run(
            "M3 每页条数真兑现：页 10 工具条「每页」下拉 = 合并页设的值",
            js("""
var sel = null;
qa('#page-host select').forEach(function(s){
  var t = s.options.length ? s.options[0].textContent.trim() : '';
  if (/条$/.test(t)) sel = s;
});
if (!sel) return {ok:false, detail:'页 10 没找到「每页 N 条」下拉'};
var txt = sel.options[sel.selectedIndex].textContent.trim();
return {ok: sel.value === '50' && txt === '50 条',
        detail: JSON.stringify({value: sel.value, text: txt,
            stored: ls('logPageSize')})};
"""))

        # ============ F/G chrome 显隐 ============
        goto_page(cdp, 13)     # 回到合并页（上面几段翻过页）
        for label, elem_id, pref_key, body_cls, tag in (
                ("显示顶部标题栏", "title-bar", "showTitleBar", "hide-title", "F1"),
                ("显示底部状态栏", "status-bar", "showStatusBar", "hide-status", "F2"),
                ("显示左侧导航栏", "nav-panel", "showNav", "hide-nav", "F3")):
            checker.run(
                "%s %s：取消勾选 → DOM 真不可见；勾回 → 恢复" % (tag, label),
                js("""
var cb = checkByText('%s');
if (!cb) return {ok:false, detail:'找不到勾选框 %s'};
var el = document.getElementById('%s');
var before = getComputedStyle(el).display;
cb.checked = false; fire(cb, 'change');
var after = getComputedStyle(el).display;
var isHidden = hidden(el);
var cls = document.body.classList.contains('%s');
var stored = ls('%s');
cb.checked = true; fire(cb, 'change');
var restored = !hidden(el);
return {ok: before !== 'none' && after === 'none' && isHidden && cls
            && stored === '0' && restored,
        detail: JSON.stringify({before: before, after: after,
            rectH: el.getBoundingClientRect().height,
            bodyClass: cls, stored: stored, restored: restored})};
""" % (label, label, elem_id, body_cls, pref_key)))

        for label, sel_name, tag in (("显示时钟", ".tb-clock", "G1"),
                                     ("显示温湿度", ".tb-th", "G2")):
            checker.run(
                "%s 标题栏内容 %s：取消勾选 → 该块真隐藏" % (tag, label),
                js("""
var cb = checkByText('%s');
if (!cb) return {ok:false, detail:'找不到勾选框 %s'};
var el = q('#title-bar %s');
if (!el) return {ok:false, detail:'标题栏里没有 %s'};
var before = getComputedStyle(el).display;
cb.checked = false; fire(cb, 'change');
var isHidden = hidden(el);
cb.checked = true; fire(cb, 'change');
var restored = !hidden(el);
return {ok: before !== 'none' && isHidden && restored,
        detail: JSON.stringify({before: before, hidden: isHidden,
            restored: restored})};
""" % (label, label, sel_name, sel_name)))

        # [2026-11-22 标题栏新元素同步] 模式钮（含自动徽标）/健康指示也进了
        # 「标题栏内容」卡。模式钮做真实显隐断言；健康正常时本来不占位，
        # 只锁"勾选框在 + 开关落盘"。
        checker.run(
            "G3 标题栏内容 显示运行模式：取消勾选 → 模式钮真隐藏",
            js("""
var cb = checkByText('显示运行模式');
if (!cb) return {ok:false, detail:'找不到勾选框 显示运行模式'};
var el = q('#title-bar .tb-mode-btn');
if (!el) return {ok:false, detail:'标题栏里没有 .tb-mode-btn'};
var before = getComputedStyle(el).display;
cb.checked = false; fire(cb, 'change');
var isHidden = hidden(el);
cb.checked = true; fire(cb, 'change');
var restored = !hidden(el);
return {ok: before !== 'none' && isHidden && restored,
        detail: JSON.stringify({before: before, hidden: isHidden,
            restored: restored})};
"""))
        checker.run(
            "G4 标题栏内容 显示设备健康：勾选框在位且开关落盘",
            js("""
var cb = checkByText('显示设备健康');
if (!cb) return {ok:false, detail:'找不到勾选框 显示设备健康'};
cb.checked = false; fire(cb, 'change');
var off = ls('showTitleHealth');
cb.checked = true; fire(cb, 'change');
var on = ls('showTitleHealth');
return {ok: off === '0' && on === '1',
        detail: JSON.stringify({off: off, on: on})};
"""))
        # [2026-11-22 用户定案「顶部状态栏加相机位图 + 本地设置同步」]
        checker.run(
            "G4b 标题栏内容 显示相机：取消勾选 → 相机徽章真隐藏 + 落盘",
            js("""
var cb = checkByText('显示相机');
if (!cb) return {ok:false, detail:'找不到勾选框 显示相机'};
var el = q('#title-bar .tb-camera-btn');
if (!el) return {ok:false, detail:'标题栏里没有 .tb-camera-btn'};
var before = getComputedStyle(el).display;
cb.checked = false; fire(cb, 'change');
var isHidden = hidden(el);
cb.checked = true; fire(cb, 'change');
var restored = !hidden(el);
return {ok: before !== 'none' && isHidden && restored
            && ls('showCamera') === '1',
        detail: JSON.stringify({before: before, hidden: isHidden,
            restored: restored, stored: ls('showCamera')})};
"""))
        # 相机徽章：点开悬浮卡（相机主题临时加入）→ 再点关闭（移除主题、
        # 销毁卡）→ 再点重新创建。相机未启用时卡内是「等待画面」占位。
        checker.run(
            "G4c 相机徽章：点开悬浮卡 + 临时订相机流（extraTopics）",
            js("""
var btn = q('#title-bar .tb-camera-btn');
if (!btn) return {ok:false, detail:'没有相机徽章'};
btn.click();
var card = q('.camera-float');
var view = q('.camera-float .camera-compact');
var extra = (HSF.store._extraTopics || []).slice();
return {ok: !!card && !!view && extra.indexOf(0x0001000C) >= 0,
        detail: JSON.stringify({card: !!card, view: !!view, extra: extra,
            topics: HSF.store._topics})};
"""))
        checker.run(
            "G4d 相机悬浮卡：按住拖动会移动",
            js("""
var card = q('.camera-float');
if (!card) return {ok:false, detail:'没有悬浮卡'};
var x0 = parseFloat(card.style.left) || 0;
var y0 = parseFloat(card.style.top) || 0;
function pe(t, x, y) {
  return new PointerEvent(t, {bubbles:true, cancelable:true, button:0,
                              buttons:1, pointerId:1, clientX:x, clientY:y});
}
card.dispatchEvent(pe('pointerdown', 300, 120));
card.dispatchEvent(pe('pointermove', 180, 220));
card.dispatchEvent(pe('pointerup', 180, 220));
var x1 = parseFloat(card.style.left) || 0;
var y1 = parseFloat(card.style.top) || 0;
return {ok: x1 !== x0 || y1 !== y0,
        detail: JSON.stringify({from: [x0, y0], to: [x1, y1]})};
"""))
        checker.run(
            "G4d2 相机悬浮卡：拖右下角会缩放尺寸",
            js("""
var card = q('.camera-float');
if (!card) return {ok:false, detail:'没有悬浮卡'};
var r = card.getBoundingClientRect();
var w0 = Math.round(r.width), h0 = Math.round(r.height);
function pe(t, x, y) {
  return new PointerEvent(t, {bubbles:true, cancelable:true, button:0,
                              buttons:1, pointerId:2, clientX:x, clientY:y});
}
var x = r.right - 2, y = r.bottom - 2;
card.dispatchEvent(pe('pointerdown', x, y));
card.dispatchEvent(pe('pointermove', x + 60, y + 40));
card.dispatchEvent(pe('pointerup', x + 60, y + 40));
var r2 = card.getBoundingClientRect();
return {ok: Math.round(r2.width) > w0 && Math.round(r2.height) > h0,
        detail: JSON.stringify({from: [w0, h0],
            to: [Math.round(r2.width), Math.round(r2.height)]})};
"""))
        checker.run(
            "G4e 再次点徽章 ⇒ 关闭并移除相机主题（后台停推）",
            js("""
var btn = q('#title-bar .tb-camera-btn');
if (!btn) return {ok:false, detail:'没有相机徽章'};
btn.click();
var extra = (HSF.store._extraTopics || []).slice();
var topics = (HSF.store._topics || []).slice();
return {ok: !q('.camera-float') && extra.indexOf(0x0001000C) < 0
            && topics.indexOf(0x0001000C) < 0,
        detail: JSON.stringify({card: !!q('.camera-float'), extra: extra,
            topicsHasCamera: topics.indexOf(0x0001000C) >= 0})};
"""))
        # 再点一次：必须重新创建出一张新卡（不复用旧实例）。
        checker.run(
            "G4f 再点徽章 ⇒ 重新创建新的悬浮卡",
            js("""
var btn = q('#title-bar .tb-camera-btn');
btn.click();
var card = q('.camera-float');
var extra = (HSF.store._extraTopics || []).slice();
return {ok: !!card && extra.indexOf(0x0001000C) >= 0,
        detail: JSON.stringify({card: !!card, extra: extra})};
"""))
        # 收尾：关掉卡，保持后续用例干净。
        cdp.evaluate("(function(){ var b=q('#title-bar .tb-camera-btn'); if(b) b.click(); })()")
        time.sleep(0.3)

        # [2026-11-22 用户定案「左上角 ☰ 用导航树还是菜单页 + 返回上一级菜单」]
        checker.run(
            "G5 导航方式卡：下拉在位且缺省「导航树」",
            js("""
var sel = q('#page-host select[aria-label="左上角菜单按钮行为"]');
var card = cardByTitle('导航方式');
return {ok: !!sel && !!card && sel.value === 'tree'
            && sel.options.length === 2,
        detail: JSON.stringify({hasSel: !!sel, hasCard: !!card,
            value: sel ? sel.value : null,
            options: sel ? sel.options.length : 0})};
"""))
        # 菜单页档：先落偏好，再进「数字量」页，点 ☰ 应打开「设备 选择子项」。
        checker.run(
            "G6 导航方式：选「菜单页」→ 偏好落盘 + 导航树隐藏 + ☰ 保留",
            js("""
var sel = q('#page-host select[aria-label="左上角菜单按钮行为"]');
if (!sel) return {ok:false, detail:'找不到导航方式下拉'};
sel.value = 'page'; fire(sel, 'change');
var nav = q('#nav-panel');
var btn = q('#title-bar .tb-menu-btn');
var cb = checkByText('显示左侧导航栏');
return {ok: ls('menuButtonStyle') === 'page'
            && document.body.classList.contains('nav-page-mode')
            && !document.body.classList.contains('hide-nav')
            && hidden(nav) && !!btn && !hidden(btn)
            && !!cb && cb.disabled,
        detail: JSON.stringify({stored: ls('menuButtonStyle'),
            pageMode: document.body.classList.contains('nav-page-mode'),
            hideNav: document.body.classList.contains('hide-nav'),
            navHidden: hidden(nav), menuBtnVisible: btn ? !hidden(btn) : null,
            switchDisabled: cb ? cb.disabled : null})};
"""))
        goto_page(cdp, 3)
        checker.run(
            "G6a ☰ 菜单页档：点 ☰ 进入当前分类入口（设备）",
            js("""
var btn = q('#title-bar .tb-menu-btn');
if (!btn) return {ok:false, detail:'标题栏没有 ☰'};
btn.click();
return {ok: location.hash === '#/page/2',
        detail: JSON.stringify({hash: location.hash})};
"""))
        time.sleep(0.6)
        wait_page_ready(cdp, 15.0)
        checker.run(
            "G6b 设备入口页：标题=设备 + 有「返回主菜单」",
            js("""
var h2 = q('#page-host .menu-page h2');
var back = q('#page-host .menu-back');
return {ok: location.hash === '#/page/2' && !!back && !!h2
            && h2.textContent.trim() === '设备'
            && back.textContent.indexOf('返回') >= 0,
        detail: JSON.stringify({hash: location.hash,
            title: h2 ? h2.textContent.trim() : null,
            back: back ? back.textContent.trim() : null})};
"""))
        # 在子菜单页再点 ☰ ⇒ 回主菜单（「上一级」链路）。
        checker.run(
            "G7 菜单页档：子菜单页再点 ☰ ⇒ 回主菜单",
            js("""
var btn = q('#title-bar .tb-menu-btn');
if (!btn) return {ok:false, detail:'标题栏没有 ☰'};
btn.click();
return {ok: location.hash === '#/page/1',
        detail: JSON.stringify({hash: location.hash})};
"""))
        time.sleep(0.6)
        wait_page_ready(cdp, 15.0)
        checker.run(
            "G7a 主菜单页不放返回（顶层没有上一级）+ 自带「主界面」入口",
            js("""
var back = q('#page-host .menu-back');
var h2 = q('#page-host .menu-page h2');
var names = qa('#page-host .menu-entry .menu-name')
  .map(function(el){ return el.textContent.trim(); });
return {ok: location.hash === '#/page/1' && !back && !!h2
            && h2.textContent.trim() === '主菜单'
            && names.length === 4 && names.indexOf('主界面') >= 0
            && names.indexOf('设备') >= 0 && names.indexOf('数据') >= 0
            && names.indexOf('设置') >= 0,
        detail: JSON.stringify({hash: location.hash, back: !!back,
            title: h2 ? h2.textContent.trim() : null, names: names})};
"""))
        # [2026-11-22 用户实报「菜单页档主界面在菜单上打不开」] 主界面入口
        # 必须真回首页（导航树此时是隐藏的，这是唯一入口）。
        checker.run(
            "G7b 主菜单「主界面」入口 ⇒ 首页(0)",
            js("""
var entries = qa('#page-host .menu-entry');
var home = null;
entries.forEach(function(b){
  var n = b.querySelector('.menu-name');
  if (n && n.textContent.trim() === '主界面') home = b;
});
if (!home) return {ok:false, detail:'主菜单没有「主界面」入口'};
home.click();
return {ok: location.hash === '#/page/0',
        detail: JSON.stringify({hash: location.hash})};
"""))
        time.sleep(0.5)
        # 返回按钮本身：再进设备入口页，点「← 返回主菜单」⇒ #/page/1。
        goto_page(cdp, 2)
        checker.run(
            "G8 子菜单「← 返回主菜单」真回主菜单",
            js("""
var back = q('#page-host .menu-back');
if (!back) return {ok:false, detail:'子菜单页没有返回按钮'};
back.click();
return {ok: location.hash === '#/page/1',
        detail: JSON.stringify({hash: location.hash})};
"""))
        time.sleep(0.5)
        # 导航树档：恢复缺省并验证宽屏 ☰ 仍是收起/展开、不切页。
        goto_page(cdp, 13)
        checker.run(
            "G9 导航方式：切回「导航树」→ 导航恢复 + ☰ 折叠/展开且不切页",
            js("""
var sel = q('#page-host select[aria-label="左上角菜单按钮行为"]');
if (!sel) return {ok:false, detail:'找不到导航方式下拉'};
sel.value = 'tree'; fire(sel, 'change');
var nav = q('#nav-panel');
var cb = checkByText('显示左侧导航栏');
var visible = !hidden(nav);
var before = nav.classList.contains('collapsed');
var hash0 = location.hash;
q('#title-bar .tb-menu-btn').click();
var after = nav.classList.contains('collapsed');
var hash1 = location.hash;
q('#title-bar .tb-menu-btn').click();       /* 复原 */
return {ok: ls('menuButtonStyle') === 'tree' && visible
            && !!cb && !cb.disabled
            && !document.body.classList.contains('hide-nav')
            && !document.body.classList.contains('nav-page-mode')
            && before !== after && hash0 === hash1,
        detail: JSON.stringify({stored: ls('menuButtonStyle'),
            visible: visible, switchDisabled: cb ? cb.disabled : null,
            before: before, after: after, hash0: hash0, hash1: hash1})};
"""))

        # ============ H 窗口尺寸记忆 ============
        checker.run(
            "H1 窗口尺寸：选 1024×600 + 点「应用」→ viewport-fixed 生效 + 落盘",
            js("""
var sel = q('#page-host select[aria-label="窗口大小"]');
if (!sel) return {ok:false, detail:'找不到窗口大小下拉'};
var want = null;
Array.prototype.forEach.call(sel.options, function(o){
  if (o.value === '1024x600') want = o;
});
if (!want) return {ok:false, detail:'下拉里没有 1024x600 档'};
sel.value = '1024x600';
var card = cardByTitle('尺寸');
var btn = card ? Array.prototype.slice.call(card.querySelectorAll('button'))
                    .filter(function(b){ return b.textContent.trim() === '应用'; })[0]
              : null;
if (!btn) return {ok:false, detail:'尺寸卡里没有「应用」按钮'};
btn.click();
var fixed = document.body.classList.contains('viewport-fixed');
var w = getComputedStyle(document.body).getPropertyValue('--app-w').trim();
var stored = ls('winSize');
return {ok: fixed && w === '1024px' && stored === '1024x600',
        detail: JSON.stringify({fixed: fixed, appW: w, stored: stored})};
"""))

        # ---- 刷新页面：记忆必须沿用（prefs.applyAll → applyViewport） ----
        cdp.call("Page.reload", {"ignoreCache": True})
        time.sleep(1.0)
        wait_page_ready(cdp, 25.0)
        checker.run(
            "H2 窗口尺寸记忆：刷新页面后仍沿用（viewport-fixed + 下拉回显）",
            js("""
var fixed = document.body.classList.contains('viewport-fixed');
var w = getComputedStyle(document.body).getPropertyValue('--app-w').trim();
var stored = ls('winSize');
var sel = q('#page-host select[aria-label="窗口大小"]');
var selText = sel ? sel.options[sel.selectedIndex].textContent.trim() : '';
return {ok: fixed && w === '1024px' && stored === '1024x600'
            && selText.indexOf('1024') >= 0,
        detail: JSON.stringify({fixed: fixed, appW: w, stored: stored,
            selText: selText})};
"""))

        # ---- 还原尺寸记忆（免得截图被约束在 1024×600 里） ----
        cdp.evaluate("""
(function(){
  localStorage.removeItem('hsfweb.winSize');
  HSF.prefs.setFullscreen(false);
  HSF.prefs.applyViewport();
  return true;
})()
""")

        # ============ I 屏幕休眠区 ============
        checker.run(
            "I1 屏幕休眠区：控件齐全（状态行/自动熄屏/系统挂起置灰/唤醒来源三勾/四按钮）",
            js("""
var card = cardByTitle('屏幕休眠');
if (!card) return {ok:false, detail:'没有「屏幕休眠」卡'};
var state = card.querySelector('.caption');
var autoSel = card.querySelector('select[aria-label="自动熄屏"]');
var suspSel = card.querySelector('select[aria-label="系统挂起"]');
var wakeBoxes = card.querySelectorAll('input.check');
var btnTexts = Array.prototype.slice.call(card.querySelectorAll('button'))
    .map(function(b){ return b.textContent.trim(); });
var need = ['立即熄屏','唤醒屏幕','保存策略','请求授权'];
var missingBtns = need.filter(function(t){ return btnTexts.indexOf(t) < 0; });
var autoOpts = autoSel ? Array.prototype.slice.call(autoSel.options)
                          .map(function(o){ return o.textContent.trim(); }) : [];
return {ok: !!state && !!autoSel && !!suspSel && wakeBoxes.length === 3
            && missingBtns.length === 0 && autoOpts.length === 5
            && state.textContent.indexOf('当前: 正常') >= 0
            && state.textContent.indexOf('系统挂起不支持') >= 0,
        detail: JSON.stringify({state: state && state.textContent.trim(),
            autoOpts: autoOpts, wakeBoxes: wakeBoxes.length,
            btnTexts: btnTexts, missingBtns: missingBtns})};
"""))

        checker.run(
            "I2 屏幕休眠：能力不支持时整块置灰且文案可解释；支持时「保存策略」落盘+回显",
            js("""
var card = cardByTitle('屏幕休眠');
var autoSel = card.querySelector('select[aria-label="自动熄屏"]');
var suspSel = card.querySelector('select[aria-label="系统挂起"]');
var btnOff = btnByText('立即熄屏'), btnOn = btnByText('唤醒屏幕');
var btnSave = btnByText('保存策略'), btnPerm = btnByText('请求授权');
var supported = !!(navigator.wakeLock && navigator.wakeLock.request);
var detail = {supported: supported,
              suspDisabled: suspSel ? suspSel.disabled : null,
              suspTip: suspSel ? (suspSel.title || '') : '',
              permDisabled: btnPerm ? btnPerm.disabled : null};
if (!supported) {
  detail.offDisabled = btnOff ? btnOff.disabled : null;
  detail.saveDisabled = btnSave ? btnSave.disabled : null;
  detail.offTip = btnOff ? (btnOff.title || '') : '';
  return {ok: btnOff && btnOff.disabled && btnSave && btnSave.disabled
              && suspSel && suspSel.disabled && btnPerm && btnPerm.disabled
              && detail.offTip.indexOf('不支持') >= 0
              && detail.suspTip.indexOf('不支持') >= 0,
          detail: JSON.stringify(detail)};
}
/* 能力支持：系统挂起/请求授权仍按参考置灰（浏览器无该原语）。 */
if (!(suspSel && suspSel.disabled && btnPerm && btnPerm.disabled)) {
  return {ok:false, detail: JSON.stringify(detail)};
}
/* 「保存策略」真兑现：改档 → 点保存 → 落盘 + 回读回显。 */
autoSel.value = '300000';
btnSave.click();
var stored = ls('powerScreenOffMs');
var back = autoSel.options[autoSel.selectedIndex].textContent.trim();
var msg = q('#status-bar') ? q('#status-bar').textContent : '';
detail.stored = stored;
detail.back = back;
detail.status = msg.slice(-120);
return {ok: stored === '300000' && back === '5 分钟'
            && msg.indexOf('屏幕休眠策略已保存') >= 0,
        detail: JSON.stringify(detail)};
"""))

        checker.run(
            "I3 屏幕休眠：四个动作按钮各有终态反馈（成功/失败/超时三选一，不停在「正在…」）",
            js("""
var btnOff = btnByText('立即熄屏'), btnOn = btnByText('唤醒屏幕');
var supported = !!(navigator.wakeLock && navigator.wakeLock.request);
if (!supported) {
  var t = btnOff ? (btnOff.title || '') : '';
  return {ok: !!btnOff && btnOff.disabled && t.indexOf('不支持') >= 0,
          detail: JSON.stringify({supported: false, tip: t})};
}
btnOff.click();
var m1 = q('#status-bar') ? q('#status-bar').textContent : '';
btnOn.click();
return new Promise(function(resolve){
  setTimeout(function(){
    var m2 = q('#status-bar') ? q('#status-bar').textContent : '';
    var ok1 = m1.indexOf('常亮锁') >= 0 || m1.indexOf('屏幕电源') >= 0;
    var ok2 = m2.indexOf('常亮') >= 0;
    resolve({ok: ok1 && ok2,
             detail: JSON.stringify({off: m1.slice(-100), on: m2.slice(-100)})});
  }, 1200);
});
"""))

        # ============ J 邻页未受影响 ============
        goto_page(cdp, 14)
        checker.run(
            "J1 邻页 14 远端设置仍能挂载（合并不许误伤）",
            js("""
var head = q('#page-host .page-head-title');
return {ok: !!head && head.textContent.trim() === '远端设置',
        detail: '页头=' + (head ? head.textContent.trim() : '(无)')};
"""))
        goto_page(cdp, 15)
        checker.run(
            "J2 邻页 15 网络设置仍能挂载",
            js("""
var head = q('#page-host .page-head-title');
return {ok: !!head && head.textContent.trim() === '网络设置',
        detail: '页头=' + (head ? head.textContent.trim() : '(无)')};
"""))
        goto_page(cdp, 12)
        checker.run(
            "J3 页 12 设置菜单只列 3 个入口（本地/远端/网络）",
            js("""
var entries = qa('#page-host button.menu-entry');
var names = entries.map(function(b){
  var n = b.querySelector('.menu-name');
  return n ? n.textContent.trim() : b.textContent.trim();
});
var txt = q('#page-host') ? q('#page-host').textContent : '';
return {ok: entries.length === 3 && names.join('/') === '本地设置/远端设置/网络设置'
            && txt.indexOf('悬浮窗设置') < 0 && txt.indexOf('窗口设置') < 0,
        detail: JSON.stringify({entries: entries.length, names: names})};
"""))

        # ============ K 每页条数 ============
        goto_page(cdp, 9, timeout_s=30.0)
        checker.run(
            "K1 每页条数（页 9 工具条）：改档 → 控件文案变 + 落盘 + 分页标签自洽",
            js("""
var sel = null;
qa('#page-host select').forEach(function(s){
  var t = s.options.length ? s.options[0].textContent.trim() : '';
  if (/条$/.test(t)) sel = s;
});
if (!sel) return {ok:false, detail:'页 9 没找到「每页 N 条」下拉'};
var beforeTxt = sel.options[sel.selectedIndex].textContent.trim();
/* [改到一个与当前不同的档] 前面几段可能已把页大小设成别的值 ⇒ 固定改 50
   会撞成「没变」，断言就假红了。 */
var target = null, targetSize = 50;
Array.prototype.forEach.call(sel.options, function(o){
  if (o.textContent.trim() !== beforeTxt && o.value === '50') {
    target = o.value; targetSize = 50;
  }
});
if (!target) {
  Array.prototype.forEach.call(sel.options, function(o){
    if (o.textContent.trim() !== beforeTxt && o.value === '20') {
      target = o.value; targetSize = 20;
    }
  });
}
if (!target) return {ok:false, detail:'找不到与当前不同的档位'};
sel.value = target; fire(sel, 'change');
var afterTxt = sel.options[sel.selectedIndex].textContent.trim();
var stored = ls('histPageSize');
/* [等真生效] 改档会重取第 0 页（服务端往返）⇒ 分页标签是异步更新的，
   这里轮询到「新档位自洽」或超时（上限 8s）再判。 */
return new Promise(function(resolve){
  var deadline = Date.now() + 8000;
  function probe(){
    var stats = qa('#page-host .dt-stat').map(function(e){ return e.textContent.trim(); });
    var m = /共\\s*(\\d+)\\s*条/.exec(stats.join(' '));
    var total = m ? parseInt(m[1], 10) : 0;
    var pageLabel = '';
    stats.forEach(function(t){ if (t.indexOf('第') >= 0) pageLabel = t; });
    var expectPages = Math.max(1, Math.ceil(total / targetSize));
    var okLabel = pageLabel.indexOf('/' + expectPages + ' ') >= 0
                  || pageLabel.indexOf('/' + expectPages + '页') >= 0;
    if (okLabel || Date.now() > deadline) {
      resolve({ok: afterTxt !== beforeTxt && stored === String(targetSize)
                  && okLabel,
               detail: JSON.stringify({beforeTxt: beforeTxt, afterTxt: afterTxt,
                   stored: stored, total: total, pageLabel: pageLabel,
                   expectPages: expectPages, stats: stats})});
    } else {
      setTimeout(probe, 300);
    }
  }
  setTimeout(probe, 300);
});
"""))

        # ============ L 页 16/17/18 深链（2026-11-22 起都是真实页） ============
        # [历史] 16/17 曾是「悬浮窗设置/窗口设置」合并后的旧页号，深链应提示
        # 已并入本地设置；2026-11-16 起 16 = Web 设置、2026-11-19 起 17 = 相机、
        # 2026-11-22 起 18 = 流量 —— 三个深链都必须挂真实页，不再是占位。
        goto_page(cdp, 16)
        checker.run(
            "L1 页 16 深链 = Web 设置（真实页，不是旧页占位）",
            js("""
var txt = q('#page-host') ? q('#page-host').textContent : '';
return {ok: txt.indexOf('Web 设置') >= 0,
        detail: txt.slice(0, 160)};
"""))
        goto_page(cdp, 17)
        checker.run(
            "L2 页 17 深链 = 相机（真实页，不是旧页占位）",
            js("""
var txt = q('#page-host') ? q('#page-host').textContent : '';
return {ok: txt.indexOf('相机') >= 0,
        detail: txt.slice(0, 160)};
"""))

        # ============ 截图 ============
        if not args.no_shots:
            os.makedirs(args.shot_dir, exist_ok=True)
            # 截图前把被测项改过的本机偏好**恢复缺省**（截图要代表默认形态；
            # 本次跑在一次性 Edge profile 里，恢复只是让画面干净）。
            cdp.evaluate("""
(function(){
  HSF.prefs.setScale(150);
  HSF.prefs.setForceCompact(false);
  HSF.prefs.setRefreshMs(200);
  HSF.prefs.setFlag('showTitleBar', true);
  HSF.prefs.setFlag('showStatusBar', true);
  HSF.prefs.setFlag('showNav', true);
  HSF.prefs.setFlag('showTitleClock', true);
  HSF.prefs.setFlag('showTitleTemp', true);
  HSF.prefs.setFlag('showTitleMode', true);
  HSF.prefs.setFlag('showTitleHealth', true);
  HSF.prefs.applyOverlay();
  if (HSF.titlebar && HSF.titlebar.applyContentPrefs) {
    HSF.titlebar.applyContentPrefs();
  }
  /* 屏幕休眠策略回缺省（自动熄屏=从不 / 唤醒来源=全部） */
  HSF.prefs.set('powerScreenOffMs', '0');
  HSF.prefs.set('powerWakeInputs', '7');
  /* 趋势间隔 / 每页条数回缺省（200 ms / 20 条） */
  HSF.prefs.set('trendSampleMs', '200');
  HSF.prefs.set('histPageSize', '20');
  HSF.prefs.set('logPageSize', '20');
  return true;
})()
""")
            goto_page(cdp, 13)
            p1 = os.path.join(args.shot_dir, "p13_1280x800.png")
            p2 = os.path.join(args.shot_dir, "p13_1280x800_bottom.png")
            cdp.evaluate("""
(function(){
  window.scrollTo(0, 0);
  var h = document.getElementById('page-host');
  if (h) h.scrollTop = 0;
  return true;
})()
""")
            time.sleep(0.4)
            s1 = shot(cdp, p1)
            # 页容器自身滚动（.page-host overflow:auto）⇒ 滚到底再截一张，
            # 让「屏幕休眠」「信息」两卡也进证据图。
            cdp.evaluate("""
(function(){
  var h = document.getElementById('page-host');
  if (h) h.scrollTop = h.scrollHeight;
  return true;
})()
""")
            time.sleep(0.6)
            s2 = shot(cdp, p2)
            print("[merge] 截图 %s (%d bytes) / %s (%d bytes)" % (p1, s1, p2, s2))
            # 竖屏 390×844
            cdp.call("Emulation.setDeviceMetricsOverride",
                     {"width": 390, "height": 844, "deviceScaleFactor": 1,
                      "mobile": True})
            time.sleep(0.8)
            goto_page(cdp, 13)
            p3 = os.path.join(args.shot_dir, "p13_390x844.png")
            p4 = os.path.join(args.shot_dir, "p13_390x844_bottom.png")
            cdp.evaluate("""
(function(){
  window.scrollTo(0, 0);
  var h = document.getElementById('page-host');
  if (h) h.scrollTop = 0;
  return true;
})()
""")
            time.sleep(0.4)
            s3 = shot(cdp, p3)
            cdp.evaluate("""
(function(){
  var h = document.getElementById('page-host');
  if (h) h.scrollTop = h.scrollHeight;
  return true;
})()
""")
            time.sleep(0.6)
            s4 = shot(cdp, p4)
            print("[merge] 截图 %s (%d bytes) / %s (%d bytes)" % (p3, s3, p4, s4))
    finally:
        if cdp is not None:
            try:
                cdp.close()
            except Exception:                            # noqa: BLE001
                pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True,
                           env=dict(os.environ, MSYS_NO_PATHCONV="1"))
        if not cleanup_profile(profile):
            print("[merge] 警告：临时 profile 未能删除（%s）" % profile,
                  file=sys.stderr)

    total = len(checker.results)
    bad = checker.failed
    print("[merge] 断言 %d 条，失败 %d 条" % (total, len(bad)))
    for name, _ok, detail in bad:
        print("[merge]   FAIL %s —— %s" % (name, detail))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
