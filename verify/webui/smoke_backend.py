# -*- coding: utf-8 -*-
"""webui 后端冒烟（纯标准库，自管进程）：

    python verify/webui/smoke_backend.py            # 全流程
    python verify/webui/smoke_backend.py --web-port 8090
    python verify/webui/smoke_backend.py --no-reconnect-test

流程：探活/自起 C sim(6100) 与 webui(8080) → 登录 root（真挑战应答）→
/api/state → SSE 首帧（连接期内 POST /api/topics 并收九主题事件）→
控制命令包络（阀开/关各回终态）→ 无权限 403（admin）→ 未登录 401 →
历史/日志分页 + 三种导出 + 配置读写 + 点动起停 →（自起 sim 时）断链
重连后订阅重发复检 → 清理收尾。

退出码 0=全过，非 0=有失败。断言不许绕过：sim 落盘关闭时历史类只断言
「端点返回良构包络」，缺数据会显式报告而不是判绿。

[进程纪律] 只清理**自己起的**进程：复用的常驻服务（已在跑的 6100/8080）
原样保留。web 端口被旧代码实例占着（/api/ping 不通）时不杀它，改起在
后备端口（8090 起）。
"""

import argparse
import http.client
import json
import os
import socket
import subprocess
import sys
import time

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", ".."))
WEBUI_DIR = os.path.join(REPO, "HighSpeedFilter", "webui_py")
SIM_EXE = os.path.join(REPO, "bin", "HsfServer.exe")

HOST = "127.0.0.1"
UP_PORT = 6100

#: 九个遥测主题（订阅必须一个不漏）。
NINE_TOPICS = [0x00010001, 0x00010002, 0x00010003, 0x00010004,
               0x00010009, 0x0001000A, 0x00010006, 0x00010007,
               0x00010005]
#: SSE 周期内应见到的主题事件。health 是**事件驱动**主题（设备状态翻转才
#: 推；sim 稳态无翻转就没有帧）——它的模型路径已由 /api/state 的
#: 「健康条目 ≥10 + addr 键」断言覆盖（web 在上游就绪时用 0x0107 拉）。
SSE_EVENTS = {"vfd", "analog", "temphum", "flowmeter", "turbidity",
              "pump", "digital", "valve"}

_passed = 0
_failed = []


def check(name, cond, detail=""):
    global _passed
    if cond:
        _passed += 1
        print("  PASS  %s" % name)
    else:
        _failed.append(name)
        print("  FAIL  %s  %s" % (name, detail))
    return cond


# ============================================================================
#                          HTTP 小工具
# ============================================================================

def http_req(port, method, path, body=None, token=None, timeout=10.0):
    """一次 HTTP 请求 → (status, headers, body_bytes)。"""
    conn = http.client.HTTPConnection(HOST, port, timeout=timeout)
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    payload = None
    if body is not None:
        payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    try:
        conn.request(method, path, body=payload, headers=headers)
        resp = conn.getresponse()
        data = resp.read()
        return resp.status, dict(resp.getheaders()), data
    finally:
        conn.close()


def http_json(port, method, path, body=None, token=None, timeout=10.0):
    status, headers, data = http_req(port, method, path, body, token, timeout)
    try:
        doc = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        doc = None
    return status, headers, doc


def wait_http(port, path, want_status=200, timeout=20.0):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            status, _h, _b = http_req(port, "GET", path, timeout=2.0)
            last = status
            if status == want_status:
                return True
        except (OSError, http.client.HTTPException):
            pass
        time.sleep(0.5)
    print("  (探活超时 port=%d path=%s last=%s)" % (port, path, last))
    return False


def tcp_open(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def port_busy(port):
    probe = socket.socket()
    try:
        probe.bind((HOST, port))
        return False
    except OSError:
        return True
    finally:
        probe.close()


def free_port(start):
    port = start
    while port < start + 50:
        if not port_busy(port):
            return port
        port += 1
    raise RuntimeError("找不到空闲端口")


# ============================================================================
#                          进程自管
# ============================================================================

def wait_tcp(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if tcp_open(HOST, port, 0.8):
            return True
        time.sleep(0.5)
    return False


def start_sim():
    print("  起 C 模拟服务端：%s --sim（%d）" % (SIM_EXE, UP_PORT))
    proc = subprocess.Popen([SIM_EXE, "--sim"], cwd=os.path.dirname(SIM_EXE),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if not wait_tcp(UP_PORT, 20.0):
        raise RuntimeError("C sim 起不来（端口 %d）" % UP_PORT)
    return proc


def stop_proc(proc, label):
    """杀自己起的进程并确认真消失（Git Bash taskkill /F 被啃成路径的教训）。"""
    if proc is None or proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       capture_output=True)
    else:
        proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print("  (WARN) %s 未如期退出 pid=%d" % (label, proc.pid))


def start_web(port, up_port):
    print("  起 webui：python -m hsfweb（%d → 上游 %d）" % (port, up_port))
    proc = subprocess.Popen(
        [sys.executable, "-m", "hsfweb", "--server", HOST,
         "--port", str(up_port), "--bind", HOST, "--web-port", str(port)],
        cwd=WEBUI_DIR, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not wait_http(port, "/api/ping", 200, 15.0):
        raise RuntimeError("webui 起不来（端口 %d）" % port)
    return proc


# ============================================================================
#                              主流程
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="webui 后端冒烟")
    parser.add_argument("--web-port", type=int, default=8080)
    parser.add_argument("--upstream-port", type=int, default=UP_PORT)
    parser.add_argument("--no-reconnect-test", action="store_true",
                        help="跳过断链重连复检（复用常驻 sim 时自动跳过）")
    args = parser.parse_args()
    web_port = args.web_port

    own_sim = None
    own_web = None
    try:
        # ---- 1. 上游 C sim ----
        print("[1] C 模拟服务端（%d）" % args.upstream_port)
        if tcp_open(HOST, args.upstream_port):
            print("  已在跑，复用（不动它）")
            args.no_reconnect_test = True
        else:
            own_sim = start_sim()
            print("  自起完成")
        check("上游 6100 可连", tcp_open(HOST, args.upstream_port))

        # ---- 2. webui ----
        print("[2] webui（%d）" % web_port)
        reused = False
        try:
            status, _h, doc = http_json(web_port, "GET", "/api/ping",
                                        timeout=3.0)
            reused = status == 200 and isinstance(doc, dict) and doc.get("ok")
        except (OSError, http.client.HTTPException):
            reused = False
        if reused:
            print("  %d 已在跑且是新版代码（/api/ping 通），复用" % web_port)
        else:
            if port_busy(web_port):
                web_port = free_port(8090)
                print("  %d 被旧实例占用（ping 不通），改用自己的 %d"
                      % (args.web_port, web_port))
            own_web = start_web(web_port, args.upstream_port)
        check("GET /api/ping", wait_http(web_port, "/api/ping", 200, 5.0))

        # ---- 3. 无鉴权面 ----
        print("[3] 无鉴权端点")
        status, _h, pages = http_json(web_port, "GET", "/api/pages")
        check("GET /api/pages 200", status == 200 and isinstance(pages, dict))
        if isinstance(pages, dict):
            names = pages.get("names") or []
            nav = pages.get("nav") or []
            check("页名新口径（设备/搅拌泵/水质监测/电动球阀）",
                  len(names) == 16 and names[2] == "设备"
                  and names[5] == "搅拌泵" and names[6] == "水质监测"
                  and names[7] == "电动球阀", str(names))
            check("导航树 15 项", len(nav) == 15, str(len(nav)))
            pt = pages.get("pageTopics") or {}
            check("pageTopics 含浊度/泵（页0/页6）",
                  0x00010009 in (pt.get("0") or [])
                  and 0x00010009 in (pt.get("6") or [])
                  and 0x0001000A in (pt.get("6") or []), str(pt))

        # ---- 4. 登录（真到上游挑战应答）----
        print("[4] 登录")
        status, _h, doc = http_json(web_port, "POST", "/api/login",
                                    {"user": "root", "password": "root"})
        if not check("POST /api/login 200", status == 200, str(doc)[:160]):
            return finish()
        token = (doc or {}).get("token", "")
        session = (doc or {}).get("session") or {}
        check("root 角色=管理员(2)", session.get("role") == 2
              and session.get("roleText") == "管理员", str(session))
        check("token 已颁", bool(token))

        # ---- 5. /api/state 全量快照（等上游就绪与首拍数据）----
        print("[5] /api/state")
        state = None
        status = 0
        deadline = time.time() + 25.0
        while time.time() < deadline:
            status, _h, state = http_json(web_port, "GET", "/api/state",
                                          token=token)
            if status == 200 and isinstance(state, dict):
                mode = state.get("mode") or {}
                health = state.get("health") or {}
                if mode.get("known") and (health.get("count") or 0) > 0:
                    break
            time.sleep(0.8)
        check("GET /api/state 200", status == 200)
        keys = ("vfd", "temphum", "analog", "flowmeter", "turbidity", "pump",
                "digital", "valve", "health", "mode", "link")
        check("state 键全（%s）" % " ".join(sorted(keys)),
              isinstance(state, dict) and all(k in state for k in keys),
              str(sorted((state or {}).keys())))
        mode = (state or {}).get("mode") or {}
        health = (state or {}).get("health") or {}
        check("运行模式已知", bool(mode.get("known")), str(mode)[:120])
        check("健康条目 ≥10（sim 十台）", (health.get("count") or 0) >= 10,
              "count=%s" % health.get("count"))
        devices = health.get("devices") or []
        hdev = devices[0] if devices else {}
        check("健康条目带 addr 键", "addr" in hdev, str(hdev)[:120])
        check("turbidity/pump 是结构化节（非 -- 字符串）",
              isinstance(state.get("turbidity"), dict)
              and isinstance(state.get("pump"), dict))

        # ---- 6/7. SSE 首帧 + 连接期内订齐九主题并收全事件 ----
        print("[6/7] SSE 首帧 + 订阅收敛")
        got_events = set()
        snap = {}
        conn_id = ""
        try:
            sse = http.client.HTTPConnection(HOST, web_port, timeout=25.0)
            sse.request("GET", "/api/stream",
                        headers={"Authorization": "Bearer %s" % token,
                                 "Accept": "text/event-stream"})
            resp = sse.getresponse()
            conn_id = resp.getheader("X-Hsf-Conn-Id") or ""
            check("SSE X-Hsf-Conn-Id", bool(conn_id), conn_id)

            deadline = time.time() + 12.0
            # 先等首帧 snapshot（register_subscriber 时必投）。
            while time.time() < deadline:
                line = resp.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").strip()
                if text == "event: snapshot":
                    data_line = resp.readline().decode("utf-8", "replace")
                    if data_line.startswith("data: "):
                        snap = json.loads(data_line[len("data: "):])
                    break
            check("SSE snapshot 首帧（含 mode/health/turbidity/pump/link）",
                  all(k in snap for k in ("vfd", "mode", "health",
                                          "turbidity", "pump", "link")),
                  str(sorted(snap.keys())))

            # 连接期内 POST /api/topics（九主题并集 + 全局常驻自动叠加）。
            status, _h, tdoc = http_json(web_port, "POST", "/api/topics",
                                         {"connId": int(conn_id),
                                          "topics": NINE_TOPICS}, token=token)
            check("POST /api/topics 200（connId 回传）",
                  status == 200 and isinstance(tdoc, dict)
                  and tdoc.get("applied") == 1, str(tdoc))
            status, _h, st = http_json(web_port, "GET", "/api/status",
                                       token=token)
            ups = (st or {}).get("upstream") or {}
            subs = ups.get("subscriptions") or []
            check("上游订阅=九主题并集+常驻（切页不互踢的收敛面）",
                  set(NINE_TOPICS) <= set(subs),
                  "subs=%s" % sorted(subs))

            # 继续读，直到主题事件全到（C sim 订阅补发 + 周期推送）。
            deadline = time.time() + 20.0
            while time.time() < deadline and len(got_events) < len(SSE_EVENTS):
                line = resp.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").strip()
                if text.startswith("event: "):
                    ev = text[len("event: "):].strip()
                    if ev in SSE_EVENTS:
                        got_events.add(ev)
            check("SSE 八主题事件全到（health 事件驱动另行断言）",
                  got_events == SSE_EVENTS,
                  "缺 %s" % sorted(SSE_EVENTS - got_events))
            sse.close()
        except (OSError, http.client.HTTPException, ValueError) as exc:
            check("SSE 流程", False, repr(exc))

        # ---- 7.5 切回手动模式（控制闸要求手动；sim 可能停在自动）----
        print("[7.5] 运行模式切手动（0x0105；控制前置）")
        status, _h, doc = http_json(web_port, "POST", "/api/mode",
                                    {"mode": 0}, token=token)
        check("POST /api/mode {mode:0}（root 有 MODE_SWITCH）",
              status == 200 and isinstance(doc, dict) and doc.get("ok") is True,
              str(doc)[:200])

        # ---- 8. 控制命令包络（阀开→关，各回终态）----
        print("[8] 控制命令包络（0x0111 阀）")
        status, _h, doc = http_json(web_port, "POST", "/api/valve/control",
                                    {"index": 0, "on": True}, token=token)
        ok_open = status == 200 and isinstance(doc, dict) \
            and doc.get("ok") is True
        check("开阀包络 ok=true（成功/失败终态，不停「正在…」）",
              ok_open, str(doc)[:200])
        if ok_open:
            check("应答带 cmdResult",
                  any(str(k).endswith("_cmdResult") for k in doc.keys()),
                  str(sorted(doc.keys())))
        time.sleep(0.5)
        status, _h, doc = http_json(web_port, "POST", "/api/valve/control",
                                    {"index": 0, "on": False}, token=token)
        check("关阀包络 ok=true",
              status == 200 and isinstance(doc, dict) and doc.get("ok") is True,
              str(doc)[:200])

        # ---- 9. 权限闸（401 / 403）----
        print("[9] 权限闸")
        status, _h, doc = http_json(web_port, "GET", "/api/state")
        check("未登录 401", status == 401, "status=%s" % status)
        status, _h, doc = http_json(web_port, "POST", "/api/login",
                                    {"user": "admin", "password": "admin"})
        admin_token = (doc or {}).get("token", "") if status == 200 else ""
        check("admin 登录（普通用户角色 1）",
              status == 200 and ((doc or {}).get("session") or {}).get("role") == 1,
              str(doc)[:160])
        status, _h, doc = http_json(web_port, "POST", "/api/vfd/control",
                                    {"op": 4}, token=admin_token)
        check("admin 控制设备 403（权限不足文案）",
              status == 403 and "权限不足" in str((doc or {}).get("err", "")),
              "status=%s doc=%s" % (status, str(doc)[:160]))
        status, _h, doc = http_json(web_port, "POST", "/api/config",
                                    {"section": "storage"}, token=admin_token)
        check("admin 写配置 403", status == 403, "status=%s" % status)
        status, _h, doc = http_json(web_port, "GET", "/api/status",
                                    token=admin_token)
        check("admin 可读状态（VIEW 放行）", status == 200)

        # ---- 10. 数据面（历史/日志分页 + 导出 + 配置 + 点动）----
        print("[10] 数据面")
        status, _h, cfg = http_json(web_port, "GET", "/api/config",
                                    token=token)
        cfg_ok = status == 200 and isinstance(cfg, dict) and cfg.get("ok")
        check("GET /api/config（collect/storage/login）",
              cfg_ok and all(k in cfg for k in ("collect", "storage", "login")),
              str(cfg)[:200])
        storage_on = bool(((cfg or {}).get("storage") or {}).get("enabled")) \
            if cfg_ok else False

        status, _h, doc = http_json(web_port, "GET",
                                    "/api/history/page?offset=0&limit=5",
                                    token=token)
        hist_ok = status == 200 and isinstance(doc, dict) and doc.get("ok")
        if not storage_on:
            # 落盘关闭是**环境**而非后端缺陷：只断言包络良构并显式报告。
            check("历史分页包络（sim 落盘关闭 ⇒ ok=false+err）",
                  status == 200 and isinstance(doc, dict)
                  and doc.get("ok") is False and doc.get("err"),
                  str(doc)[:160])
            print("  (NOTE) sim 落盘未启用：历史行数/导出内容断言按环境跳过")
        else:
            check("GET /api/history/page", hist_ok, str(doc)[:160])
            if hist_ok:
                check("历史应答键（total/rows/cols）",
                      all(k in doc for k in ("total", "rows", "cols")),
                      str(sorted(doc.keys())))

        status, _h, doc = http_json(web_port, "GET",
                                    "/api/log/page?offset=0&limit=5",
                                    token=token)
        check("GET /api/log/page（0x0119 转发）",
              status == 200 and isinstance(doc, dict)
              and (("ok" in doc) and (doc.get("ok") is True
                                      or bool(doc.get("err")))),
              str(doc)[:200])

        status, _h, blob = http_req(web_port, "GET",
                                    "/api/history/export?format=csv",
                                    token=token, timeout=30.0)
        if storage_on or status == 200:
            check("历史导出 CSV（BOM+表头）",
                  status == 200 and blob[:3] == b"\xef\xbb\xbf"
                  and "时间".encode("utf-8") in blob[:400],
                  "status=%s head=%r" % (status, blob[:60]))
        else:
            check("历史导出包络（落盘关闭 ⇒ 502+err）",
                  status == 502 and "导出失败" in str(blob, "utf-8",
                                                     "replace"),
                  "status=%s" % status)
        status, _h, blob = http_req(web_port, "GET",
                                    "/api/history/export?format=xlsx",
                                    token=token, timeout=60.0)
        if storage_on or status == 200:
            check("历史导出 XLSX（PK 头）",
                  status == 200 and blob[:2] == b"PK",
                  "status=%s head=%r" % (status, blob[:16]))
        else:
            check("历史导出 XLSX 包络（落盘关闭 ⇒ 502）",
                  status == 502, "status=%s" % status)
        status, _h, blob = http_req(web_port, "GET",
                                    "/api/log/export?format=csv",
                                    token=token, timeout=30.0)
        if storage_on or status == 200:
            check("日志导出 CSV（BOM；权限闸在 web，root 放行）",
                  status == 200 and blob[:3] == b"\xef\xbb\xbf",
                  "status=%s head=%r" % (status, blob[:40]))
        else:
            # 0x0119 与历史同库：落盘关闭时服务端回否定包络，web 502 透传。
            check("日志导出包络（落盘关闭 ⇒ 502+err）",
                  status == 502, "status=%s" % status)

        if storage_on:
            writeback = {"section": "storage",
                         "historyTrigger": ((cfg or {}).get("storage") or {})
                         .get("historyTrigger", "always")}
            status, _h, doc2 = http_json(web_port, "POST", "/api/config",
                                         writeback, token=token)
            check("POST /api/config（原值写回，需 ACCOUNT_ADMIN）",
                  status == 200 and isinstance(doc2, dict)
                  and doc2.get("ok") is True, str(doc2)[:200])

        status, _h, doc = http_json(web_port, "POST", "/api/jog/start",
                                    {"rev": False, "connId": None,
                                     "hz": 10.0}, token=token, timeout=8.0)
        check("POST /api/jog/start（web 代持 200ms 重发）",
              status == 200 and isinstance(doc, dict) and doc.get("ok") is True,
              str(doc)[:160])
        status, _h, doc = http_json(web_port, "POST", "/api/jog/stop",
                                    {}, token=token, timeout=8.0)
        check("POST /api/jog/stop（安全动作不设闸）",
              status == 200 and isinstance(doc, dict) and doc.get("ok") is True,
              str(doc)[:160])

        # ---- 11. 断链重连后订阅重发（只在自己起的 sim 上做）----
        if not args.no_reconnect_test and own_sim is not None:
            print("[11] 断链重连后订阅重发")
            stop_proc(own_sim, "C sim")
            own_sim = None
            deadline = time.time() + 20.0
            went_down = False
            st = {}
            while time.time() < deadline:
                status, _h, st = http_json(web_port, "GET", "/api/status",
                                           token=token)
                ups = (st or {}).get("upstream") or {}
                if ups.get("ready") is False:
                    went_down = True
                    break
                time.sleep(0.5)
            check("断链后 upstream.ready=false", went_down,
                  str((st or {}).get("upstream"))[:160])
            own_sim = start_sim()
            deadline = time.time() + 30.0
            resub = False
            while time.time() < deadline:
                status, _h, st = http_json(web_port, "GET", "/api/status",
                                           token=token)
                ups = (st or {}).get("upstream") or {}
                subs = ups.get("subscriptions") or []
                if ups.get("ready") and set(NINE_TOPICS) <= set(subs):
                    resub = True
                    break
                time.sleep(0.8)
            check("重连成功且订阅已重发（重连后必须重发订阅）", resub,
                  str((st or {}).get("upstream"))[:200])
        else:
            print("[11] 跳过断链重连复检（复用常驻 sim 或 --no-reconnect-test）")

        return finish()
    finally:
        if own_web is not None:
            print("[清理] 停自己起的 webui（%d）" % web_port)
            stop_proc(own_web, "webui")
        if own_sim is not None:
            print("[清理] 停自己起的 C sim")
            stop_proc(own_sim, "C sim")


def finish():
    print("")
    print("=" * 56)
    print("通过 %d 项；失败 %d 项 %s"
          % (_passed, len(_failed), _failed if _failed else ""))
    return 0 if not _failed else 1


if __name__ == "__main__":
    sys.exit(main())
