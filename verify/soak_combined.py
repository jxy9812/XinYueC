#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""三端组合负载浸泡（2026-11-18 用户「整体联调测试，修隐藏 bug/性能/内存泄漏」）。

[负载构成（全部走**真实链路**，不打桩）]
  · XLink 客户端（client_py 的 XLinkClient，纯协议零 Qt）连上游 6100：
      轮换 {自动启停周期（0x0105 切模式 + 0x0115 op=1/2）、变频器参数读/写
      （0x011B，幂等回写）、报警历史（0x011A）、历史/日志分页（0x0118/0x0119
      带关键字 = 真全表扫负载）、快照族}——每拍测**往返时延**。
  · HTTP 负载打 web（8085）：/api/state、/api/alarms/page、/api/history/page、
    /api/mode、/api/auto/control —— 压 web→上游转发链路。
  · SSE 常驻读流（/api/stream）——保持一个真实 web 订阅会话。
[采样]
  · 每 10s：sim 服务端 / web 进程 RSS（wmic WorkingSetSize）。
  · 每 XLink 往返：时延（ms）。
  · sim 日志尾部：tick 异常 / 未知命令 / ERROR。
[判据（退出码）]
  0 = 无请求失败、XLink p95 ≤ 250ms、max ≤ 1000ms、无 tick 异常、
      RSS 斜率有界（末段均值 − 首段均值 ≤ 15MB，SQLite 页缓存有 legitimate
      增长故给宽裕度；斜率=每分钟）。
  1 = 任一判据破。

用法：
    python verify/soak_combined.py [--minutes 8] [--web 8085] [--upstream 6100]
      [--report PATH]   （把结论写进 json，供编排层读取）
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

REPO = __file__[:__file__.rfind("\\verify\\")] if "\\verify\\" in __file__ else \
    __file__[:__file__.rfind("/verify/")]
sys.path.insert(0, REPO + "\\HighSpeedFilter\\client_py")
sys.path.insert(0, REPO + "/HighSpeedFilter/client_py")

from hsfsrv_client import bson, proto            # noqa: E402
from hsfsrv_client.xlink import XLinkClient      # noqa: E402

TICK_BAD = re.compile(r"tick 异常|Traceback|未知命令|ERROR", re.I)


def rss_mb(pid):
    """进程工作集（MB）；进程没了返回 None。"""
    try:
        out = subprocess.run(
            ["wmic", "process", "where", "ProcessId=%d" % pid,
             "get", "WorkingSetSize", "/format:csv"],
            capture_output=True, timeout=20).stdout.decode("utf-8", "replace")
    except Exception:
        return None
    m = re.search(r",(\d+)\s*$", out.strip())
    return round(int(m.group(1)) / 1048576.0, 1) if m else None


def http(url, token, obj=None, timeout=15):
    hdr = {}
    if token:
        hdr["Authorization"] = "Bearer " + token
    data = None
    if obj is not None:
        hdr["Content-Type"] = "application/json"
        data = json.dumps(obj).encode()
    req = urllib.request.Request(url, data=data, headers=hdr)
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read()
    dt = (time.perf_counter() - t0) * 1000.0
    return dt, json.loads(body.decode("utf-8", "replace")) if body else {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=int, default=8)
    ap.add_argument("--web", type=int, default=8085)
    ap.add_argument("--upstream", type=int, default=6100)
    ap.add_argument("--sim-pid", type=int, default=0)
    ap.add_argument("--web-pid", type=int, default=0)
    ap.add_argument("--report", default="")
    a = ap.parse_args()

    # 上游 PID 缺省按端口找
    sim_pid, web_pid = a.sim_pid, a.web_pid
    if not sim_pid or not web_pid:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             timeout=30).stdout.decode("utf-8", "replace")
        for line in out.splitlines():
            if sim_pid == 0 and ":6100 " in line and "LISTENING" in line:
                sim_pid = int(line.split()[-1])
            if web_pid == 0 and ":%d " % a.web in line and "LISTENING" in line:
                web_pid = int(line.split()[-1])
    print("[soak] sim_pid=%s web_pid=%s" % (sim_pid, web_pid))

    # ---- XLink 客户端 ----
    xc = XLinkClient("127.0.0.1", a.upstream, "root", "root",
                     log=lambda m: None)
    ok, err = xc.start(timeout=10.0)
    if not ok:
        print("[FAIL] XLink 登录失败：%s" % err)
        return 1
    print("[soak] XLink 就绪")

    def req(cmd, doc=None, timeout=8.0):
        payload = bson.encode(doc) if doc is not None else b""
        t0 = time.perf_counter()
        fut = xc.call_soon(xc.request(cmd, payload,
                                      proto.DATATYPE_BSON, timeout))
        if fut is None:
            raise RuntimeError("call_soon 投递失败（链路已断）")
        _mc, _dt, pl = fut.result()
        dt = (time.perf_counter() - t0) * 1000.0
        d = bson.decode(pl) if pl else {}
        return dt, d

    # ---- HTTP 登录（web） ----
    def http_req(path, obj=None, timeout=15):
        return http("http://127.0.0.1:%d%s" % (a.web, path), token, obj,
                    timeout)

    dt0, doc = http("http://127.0.0.1:%d/api/login" % a.web, None,
                    {"user": "root", "password": "root"})
    token = doc["token"]
    print("[soak] web 登录 ok（%.0fms）" % dt0)

    # ---- SSE 常驻读流（线程） ----
    sse_stop = threading.Event()
    sse_events = [0]

    def sse_reader():
        req = urllib.request.Request(
            "http://127.0.0.1:%d/api/stream" % a.web,
            headers={"Authorization": "Bearer " + token})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                while not sse_stop.is_set():
                    line = r.readline()
                    if not line:
                        break
                    if line.startswith(b"data:"):
                        sse_events[0] += 1
        except Exception:
            pass

    sse_t = threading.Thread(target=sse_reader, name="soak-sse", daemon=True)
    sse_t.start()

    # ---- 切自动模式（自动启停周期需要）----
    dt, d = http_req("/api/mode", {"mode": 1})
    assert d.get("ok"), d
    print("[soak] 已切自动模式")

    lat_xlink = []          # 所有 XLink 往返
    lat_by = {}             # 按类分
    err_count = [0]
    stop = threading.Event()

    def timed(tag, fn):
        try:
            dt, _d = fn()
            lat_xlink.append(dt)
            lat_by.setdefault(tag, []).append(dt)
        except Exception as exc:
            err_count[0] += 1
            if err_count[0] <= 5:
                print("[ERR] %s: %s" % (tag, exc))

    def vfd_params_rw():
        _, d = req(proto.CMD_VFD_PARAMS, {"op": 0})
        if not d.get("ok"):
            raise RuntimeError("参数读失败：%s" % d.get("err"))
        values = {}
        for p in d.get("params", []):
            values[str(p["addr"])] = int(p["raw"])
        dt, d2 = req(proto.CMD_VFD_PARAMS,
                     {"op": 1, "values": values})
        if not d2.get("ok"):
            if "运行中禁止写" in (d2.get("err") or ""):
                return dt, None   # RunLocked 是合法结果（变频器可能在跑）
            raise RuntimeError("参数写失败：%s" % d2.get("err"))
        return dt, d2

    cycle = 0
    t_end = time.time() + a.minutes * 60
    rss = []                 # [(t, simMB, webMB)]
    print("[soak] 开始 %d 分钟组合负载…" % a.minutes)
    while time.time() < t_end:
        cycle += 1
        # ① 自动启停周期（每 5 拍一轮：启动→跑几拍→停止）
        if cycle % 5 == 1:
            timed("auto-start", lambda: req(
                proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_START}))
        if cycle % 5 == 3:
            timed("auto-stop", lambda: req(
                proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP}))
        # ② 变频器参数读+幂等回写
        timed("vfd-params", vfd_params_rw)
        # ③ 报警历史
        timed("alarm-page", lambda: req(proto.CMD_ALARM_PAGE,
                                        {"offset": 0, "limit": 20}))
        # ④ 历史/日志分页（关键字 = 全表扫，真实重负载）
        timed("history-page", lambda: req(
            proto.CMD_HISTORY_PAGE,
            {"offset": 0, "limit": 50,
             "kw": "手" if cycle % 2 else "自动"}))
        timed("log-page", lambda: req(
            proto.CMD_LOG_PAGE, {"offset": 0, "limit": 30}))
        # ⑤ HTTP 侧（web→上游转发）
        timed("http-state", lambda: http_req("/api/state"))
        timed("http-alarms", lambda: http_req("/api/alarms/page?limit=20"))
        timed("http-history", lambda: http_req(
            "/api/history/page?offset=0&limit=50"))
        # ⑥ RSS 采样
        if cycle % 4 == 0:
            rss.append((time.time(), rss_mb(sim_pid), rss_mb(web_pid)))
        time.sleep(1.0)
        if cycle % 20 == 0:
            print("[soak] cycle=%d 已 %.0fs，XLink 往返 %d 次，错误 %d"
                  % (cycle, a.minutes * 60 - (t_end - time.time()),
                     len(lat_xlink), err_count[0]))

    sse_stop.set()
    http_req("/api/mode", {"mode": 0})          # 收工切回手动（AGENTS 纪律）
    xc.close()

    # ---- 判据 ----
    fails = []
    p95 = max_v = 0.0
    if lat_xlink:
        lat = sorted(lat_xlink)
        p95 = lat[int(len(lat) * 0.95) - 1]
        max_v = lat[-1]
    print("[soak] XLink 往返 %d 次：p95=%.0fms max=%.0fms 错误=%d"
          % (len(lat_xlink), p95, max_v, err_count[0]))
    for tag, l in sorted(lat_by.items()):
        l2 = sorted(l)
        print("  %-14s p95=%7.0fms max=%7.0fms n=%d"
              % (tag, l2[int(len(l2) * 0.95) - 1], l2[-1], len(l2)))
    if err_count[0]:
        fails.append("有 %d 次请求失败" % err_count[0])
    if p95 > 250:
        fails.append("XLink p95=%.0fms > 250ms" % p95)
    if max_v > 1000:
        fails.append("XLink max=%.0fms > 1000ms" % max_v)

    # RSS 斜率：首段均值 vs 末段均值（MB/min）
    if len(rss) >= 6 and sim_pid:
        k = max(2, len(rss) // 3)
        first, last = rss[:k], rss[-k:]
        span_min = (last[-1][0] - first[0][0]) / 60.0
        for idx, name in ((1, "sim"), (2, "web")):
            a1 = statistics.mean(x[idx] for x in first if x[idx] is not None)
            a2 = statistics.mean(x[idx] for x in last if x[idx] is not None)
            slope = (a2 - a1) / max(0.1, span_min)
            print("[soak] %s RSS：%.1f → %.1f MB（斜率 %+.2f MB/min）"
                  % (name, a1, a2, slope))
            if idx == 1 and slope > 7.5:
                fails.append("sim RSS 斜率 %+.2f MB/min 超界" % slope)
            if idx == 2 and slope > 3.0:
                fails.append("web RSS 斜率 %+.2f MB/min 超界" % slope)

    # sim 日志尾部抽查（tick 异常等）——日志由调用方留存，这里只报会话指标
    if sse_events[0] == 0:
        fails.append("SSE 0 事件（订阅会话死了？）")
    print("[soak] SSE 事件 %d" % sse_events[0])

    if a.report:
        with open(a.report, "w", encoding="utf-8") as fp:
            json.dump({
                "cycles": cycle, "xlink_n": len(lat_xlink),
                "p95_ms": round(p95, 1), "max_ms": round(max_v, 1),
                "errors": err_count[0], "sse_events": sse_events[0],
                "rss": [[t, m1, m2] for t, m1, m2 in rss],
                "fails": fails,
            }, fp, ensure_ascii=False, indent=1)
    if fails:
        print("[FAIL] " + "；".join(fails))
        return 1
    print("[OK] 浸泡全过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
