# -*- coding: utf-8 -*-
"""历史页图表交互门禁（真浏览器 + CDP）：平移 / 缩放 / 双击复位 / 读数气泡。

    python verify/webui/webui_hist_chart_check.py

[为什么要有这一段] 2026-11-22 前历史页图表是**纯静态 canvas**（零事件）：
手机上拖动会被页面滚动抢走、缩放/悬停读数都没有。本脚本把「补齐的四类
交互」变成机械门禁：

  ① 初始：data-view=0（全程）、data-read=none、复位按钮 disabled
  ② 滚轮缩放 ⇒ data-view=1、复位按钮可用
  ③ 拖动平移 ⇒ 仍 data-view=1（视口接管，不被自动刷新重置）
  ④ 双击 ⇒ 回全程（data-view=0、按钮 disabled）
  ⑤ 悬停吸附 ⇒ data-read=hover（十字线/气泡画出）
  ⑥ 点按 ⇒ data-read=picked；再点同处 ⇒ none
  ⑦ 双指捏合 ⇒ data-view=1；点「复位视图」⇒ 0
  ⑧ 全程无 JS 异常 / console error

自管一套（py sim + hsfweb + Edge headless），跑完全收尾；端口自动挑空闲。
"""
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import cdp_client  # noqa: E402
import webui_check as W  # noqa: E402

PASS = []
FAIL = []


def check(name, cond, detail=""):
    print(("  PASS  " if cond else "  FAIL  ") + name
          + (("  " + detail) if detail else ""))
    (PASS if cond else FAIL).append(name)


class Ctx(object):
    def __init__(self, web, token):
        self.web = web
        self.token = token


def _get(ctx, path, timeout=20.0):
    return W.http_json(ctx.web, "GET", path, token=ctx.token, timeout=timeout)


def _post(ctx, path, body, timeout=20.0):
    return W.http_json(ctx.web, "POST", path, body, token=ctx.token,
                       timeout=timeout)


def _eval(cdp, expr):
    try:
        return cdp.evaluate(expr)
    except Exception:                                # noqa: BLE001
        return None


def _poll(cdp, expr, want, timeout=20.0):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = _eval(cdp, expr)
        if last == want:
            return last
        time.sleep(0.2)
    return last


def _mouse(cdp, typ, x, y, buttons=0, click_count=0):
    cdp.call("Input.dispatchMouseEvent",
             {"type": typ, "x": x, "y": y, "button": "left",
              "buttons": buttons, "clickCount": click_count,
              "pointerType": "mouse"})


def _touch(cdp, typ, pts):
    cdp.call("Input.dispatchTouchEvent",
             {"type": typ,
              "touchPoints": [{"x": p[0], "y": p[1], "id": i}
                              for i, p in enumerate(pts)]})


def _seed_history(base_dir, n=6):
    """往 py sim 的临时库种 n 行（1s 间隔）——图表只看"行"，不看来源。

    [为什么种夹具而不是等采样] 服务端有「热身门」：VFD/温湿度/浊度×2/
    流量/压差/数字量**每组都见过有效值**才开始入库；本门禁只验**交互**，
    不为等一次真实采样把装置依赖拖进来（历史类断言用夹具库是本仓惯例）。
    """
    db = os.path.join(base_dir, "hsf_data.db")
    deadline = time.time() + 15
    while not os.path.isfile(db) and time.time() < deadline:
        time.sleep(0.3)
    now = int(time.time() * 1000)
    rows = []
    for i in range(n):
        ts = now - (n - i) * 1000
        rows.append((ts, "手动", 2, 30.0 + i, 12.5 + i * 0.1, 900 + i * 10,
                     2.29, 0.045, 3.6, "m3/h", 1000.0 + i, "m3",
                     0.42 + i * 0.001, None, 26.5, 55.0, 0, 0, 19.3 + i * 0.01))
    conn = sqlite3.connect(db, timeout=10)
    try:
        conn.executemany(
            "INSERT OR REPLACE INTO history (ts, mode, vfd_state, freq_hz,"
            " current_a, rpm, turb1, turb2, flow, flow_unit, flow_total,"
            " total_unit, press_mpa, press_note, temp, hum, di, do, water_temp)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
        conn.commit()
    finally:
        conn.close()
    return len(rows)


def _drain_errors(cdp):
    cdp.evaluate("1")                    # 冲事件（cdp_client 只在 call 时读帧）
    evs = cdp.drain_events(["Runtime.exceptionThrown", "Log.entryAdded",
                            "Runtime.consoleAPICalled"])
    bad = []
    for e in evs:
        p = e.get("params") or {}
        s = json.dumps(p, ensure_ascii=False).lower()
        if "favicon.ico" in s:
            continue                    # 无 favicon 是噪音，不是页面错误
        if e.get("method") == "Runtime.exceptionThrown":
            bad.append(json.dumps(e, ensure_ascii=False)[:200])
        elif e.get("method") == "Runtime.consoleAPICalled":
            if (p.get("type") or "") == "error":
                bad.append(json.dumps(e, ensure_ascii=False)[:200])
        elif e.get("method") == "Log.entryAdded":
            # [按 level 判，不用 "error" 子串] 子串会把 "no errors" 之类
            # 正常文案判红。
            if ((p.get("entry") or {}).get("level") or "") == "error":
                bad.append(json.dumps(e, ensure_ascii=False)[:200])
    return bad


def main():
    owned = W.Owned()
    exe = W.find_browser()
    if exe is None:
        print("未找到 Edge/Chrome：跳过（环境问题，不判红）")
        return 0
    cdp = None
    proc = None
    profile = None
    try:
        # [必修 2026-11-22 复审] 不能用固定 6200：本机常驻 6200 时新 sim
        # 绑定失败，而 wait_tcp 只证明"有人听" ⇒ 会把夹具种进失败实例的
        # 临时库、真实服务读不到（假红/假绿都难查）。用空闲端口 + 把端口
        # 传给 hsfweb。
        pysim = W.free_port(6230)
        W.start_py_sim(pysim, owned)
        web = W.free_port(8090)
        p, oh, eh = W._spawn(
            "hsfweb(%d)" % web,
            [sys.executable, "-m", "hsfweb", "--server", "127.0.0.1",
             "--port", str(pysim), "--bind", "127.0.0.1",
             "--web-port", str(web)], W.WEBUI_DIR)
        owned.add("hsfweb(%d)" % web, p, [oh, eh])
        if not W.wait_hsfweb(web, 30.0):
            check("hsfweb 起来", False, "30s 未就绪")
            return 1
        st, _hd, doc = W.http_json(web, "POST", "/api/login",
                                   {"user": "root", "password": "root"})
        token = (doc or {}).get("token") or ""
        check("网页登录拿到 token", bool(token), "status=%s" % st)
        if not token:
            return 1
        ctx = Ctx(web, token)

        # 种夹具行（见 _seed_history 说明），再确认分页能读到
        base_dir = owned.dir_cleanups[-1]
        seeded = _seed_history(base_dir)
        check("夹具历史行已就位", seeded >= 3, "seeded=%d" % seeded)
        deadline = time.time() + 20
        total = 0
        while time.time() < deadline:
            st, _h, d = _get(ctx, "/api/history/page?offset=0&limit=5")
            total = int((d or {}).get("total") or 0)
            if total >= 3:
                break
            time.sleep(0.5)
        check("历史分页读到夹具行（≥3）", total >= 3, "total=%d" % total)

        profile = tempfile.mkdtemp(prefix="edge_hist_")
        port = W.free_port(9540)
        proc = subprocess.Popen(
            [exe, "--headless=new", "--disable-gpu", "--no-first-run",
             "--user-data-dir=" + profile,
             "--remote-debugging-port=%d" % port,
             "--window-size=390,844", "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ws = cdp_client.wait_for_target(port, timeout_s=45.0)
        cdp = cdp_client.Cdp(ws, timeout=60.0)
        for dom in ("Page", "Runtime", "Log"):
            cdp.call(dom + ".enable")
        cdp.call("Emulation.setTouchEmulationEnabled",
                 {"enabled": True, "maxTouchPoints": 2})
        cdp.call("Emulation.setDeviceMetricsOverride",
                 {"width": 390, "height": 844, "deviceScaleFactor": 1,
                  "mobile": True})
        cdp.call("Page.navigate", {"url": "http://127.0.0.1:%d/?authtoken=%s"
                                   % (web, token)})
        # 等壳 + 进历史页 + 切图表视图
        ok = False
        for _ in range(60):
            if _eval(cdp, "!!document.getElementById('page-host')"):
                ok = True
                break
            time.sleep(0.4)
        check("页面壳已渲染", ok)
        _eval(cdp, "location.hash = '#/page/9'")
        # [等表格首屏数据落地再切图表] 首次分页请求在途时 setView(1) 会与
        # 表格请求抢 pendingKind；默认起始时间回填还会再触发一次
        # chartRefresh 复位视口 ⇒ 交互断言会在中途变红（复审指出）。
        rows_ok = False
        for _ in range(40):
            n = _eval(cdp, "document.querySelectorAll("
                           "'.dt-table tbody tr').length")
            if isinstance(n, int) and n > 0:
                rows_ok = True
                break
            time.sleep(0.3)
        check("表格首屏已取到夹具行", rows_ok, "rows=%r" % n)
        clicked = _eval(cdp,
                        "(function(){var b=Array.prototype.slice.call("
                        "document.querySelectorAll('button')).filter("
                        "function(x){return x.textContent.trim()==='图表视图';})[0];"
                        "if(!b) return false; b.click(); return true;})()")
        check("切到图表视图（点「图表视图」）", bool(clicked))
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          " ? document.querySelector('.hist-canvas')"
                          "   .getAttribute('data-view') : null", "0", 30.0)
        check("图表已绘制（data-view=0 全程）", got == "0", "data-view=%r" % got)
        check("初始读数态 none",
              _eval(cdp, "document.querySelector('.hist-canvas')"
                         ".getAttribute('data-read')") == "none")
        check("初始「复位视图」按钮 disabled",
              bool(_eval(cdp, "document.querySelector('.hist-chart-reset')"
                              ".disabled")))

        rect = _eval(cdp,
                     "(function(){var c=document.querySelector('.hist-canvas');"
                     "if(!c) return null;"
                     "try{c.scrollIntoView({block:'center'});}catch(e){}"
                     "var r=c.getBoundingClientRect();"
                     "return {x:r.left,y:r.top,w:r.width,h:r.height};})()")
        time.sleep(0.6)
        # 滚动后再取一次（scrollIntoView 之后坐标才在视口内）
        rect = _eval(cdp,
                     "(function(){var c=document.querySelector('.hist-canvas');"
                     "if(!c) return null;var r=c.getBoundingClientRect();"
                     "return {x:r.left,y:r.top,w:r.width,h:r.height,"
                     " vh:window.innerHeight};})()")
        if not rect or not rect.get("w"):
            check("图表几何可读", False, str(rect))
            return 1
        # [CDP 输入用视口坐标] rect 必须在视口内，否则点的是空白
        in_view = (0 <= rect["y"] < rect.get("vh", 844) - 20
                   and rect["y"] + rect["h"] > 20)
        check("图表已滚入视口（输入坐标有效）", in_view,
              "y=%.0f h=%.0f vh=%s" % (rect["y"], rect["h"],
                                       rect.get("vh")))
        cx = rect["x"] + rect["w"] / 2
        cy = max(rect["y"] + 40, min(rect["y"] + rect["h"] - 40,
                                     rect["y"] + rect["h"] / 2))

        # ② 滚轮缩放
        cdp.call("Input.dispatchMouseEvent",
                 {"type": "mouseWheel", "x": cx, "y": cy,
                  "deltaX": 0, "deltaY": -120, "button": "none",
                  "pointerType": "mouse"})
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-view')", "1", 5.0)
        check("滚轮缩放 ⇒ data-view=1（视口接管）", got == "1",
              "data-view=%r" % got)
        check("缩放后「复位视图」按钮可用",
              not bool(_eval(cdp, "document.querySelector('.hist-chart-reset')"
                                  ".disabled")))

        # ③ 拖动平移（仍接管）
        _mouse(cdp, "mousePressed", cx, cy, buttons=1, click_count=1)
        for step in range(1, 5):
            _mouse(cdp, "mouseMoved", cx + step * 15, cy, buttons=1)
            time.sleep(0.05)
        _mouse(cdp, "mouseReleased", cx + 60, cy, buttons=0, click_count=1)
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-view')", "1", 3.0)
        check("拖动平移后仍 data-view=1", got == "1", "data-view=%r" % got)

        # ④ 双击复位
        _mouse(cdp, "mousePressed", cx, cy, buttons=1, click_count=1)
        _mouse(cdp, "mouseReleased", cx, cy, buttons=0, click_count=1)
        _mouse(cdp, "mousePressed", cx, cy, buttons=1, click_count=2)
        _mouse(cdp, "mouseReleased", cx, cy, buttons=0, click_count=2)
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-view')", "0", 5.0)
        check("双击 ⇒ 回全程（data-view=0）", got == "0", "data-view=%r" % got)
        check("回全程后按钮 disabled",
              bool(_eval(cdp, "document.querySelector('.hist-chart-reset')"
                              ".disabled")))

        # ⑤ 悬停吸附：在绘图区扫几个位置，命中任一数据点即可
        hover_ok = False
        x0 = rect["x"] + 90
        x1 = rect["x"] + rect["w"] - 30
        x = x0
        while x < x1 and not hover_ok:
            _mouse(cdp, "mouseMoved", x, cy, buttons=0)
            time.sleep(0.12)
            if _eval(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-read')") == "hover":
                hover_ok = True
                break
            x += 12
        check("悬停吸附 ⇒ data-read=hover（十字线/气泡）", hover_ok)

        # ⑥ 点按固定气泡 → 再点同处取消
        tap_x = x if hover_ok else cx
        _touch(cdp, "touchStart", [(tap_x, cy)])
        time.sleep(0.05)
        _touch(cdp, "touchEnd", [(tap_x, cy)])
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-read')", "picked", 4.0)
        check("点按 ⇒ data-read=picked（固定气泡）", got == "picked",
              "data-read=%r" % got)
        _touch(cdp, "touchStart", [(tap_x, cy)])
        time.sleep(0.05)
        _touch(cdp, "touchEnd", [(tap_x, cy)])
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-read')", "none", 4.0)
        check("再点同处 ⇒ data-read=none（取消固定）", got == "none",
              "data-read=%r" % got)

        # ⑦ 双指捏合放大 ⇒ 接管；复位按钮回全程
        _touch(cdp, "touchStart", [(cx - 20, cy), (cx + 20, cy)])
        for step in range(1, 8):
            d = 20 + step * 8
            _touch(cdp, "touchMove", [(cx - d, cy), (cx + d, cy)])
            time.sleep(0.05)
        _touch(cdp, "touchEnd", [(cx - 76, cy), (cx + 76, cy)])
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-view')", "1", 5.0)
        check("双指捏合 ⇒ data-view=1", got == "1", "data-view=%r" % got)
        # 复位按钮回全程（先确认"确实处于接管态"，否则捏合失败也会
        # 因一直是 0 而假通过）
        pre_view = _eval(cdp, "document.querySelector('.hist-canvas')"
                              ".getAttribute('data-view')")
        pre_btn = _eval(cdp, "!document.querySelector('.hist-chart-reset')"
                             ".disabled")
        check("复位前确处于接管态（data-view=1 且按钮可用）",
              pre_view == "1" and bool(pre_btn),
              "view=%r btn=%r" % (pre_view, pre_btn))
        _eval(cdp, "document.querySelector('.hist-chart-reset').click()")
        got = _poll(cdp, "document.querySelector('.hist-canvas')"
                          ".getAttribute('data-view')", "0", 5.0)
        check("点「复位视图」⇒ 回全程（data-view=0）", got == "0",
              "data-view=%r" % got)

        bad = _drain_errors(cdp)
        check("全程无 JS 异常/console error", not bad,
              json.dumps(bad[:2], ensure_ascii=False)[:300])
    except Exception as exc:                         # noqa: BLE001
        check("门禁执行", False, "%s: %s" % (type(exc).__name__, exc))
    finally:
        if cdp is not None:
            try:
                cdp.close()
            except Exception:                        # noqa: BLE001
                pass
        if proc is not None:
            W._kill_tree(proc)
        if profile:
            W._rm_profile(profile)
        owned.cleanup()

    print("\n汇总：PASS %d / FAIL %d" % (len(PASS), len(FAIL)))
    if FAIL:
        print("失败项：")
        for n in FAIL:
            print("  - %s" % n)
    return 0 if not FAIL else 1


if __name__ == "__main__":
    sys.exit(main())
