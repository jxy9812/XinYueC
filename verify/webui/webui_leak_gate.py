#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""网页端（webui_py）内存泄漏门 + 浏览器控制台零异常门禁。

用法
====
    python verify/webui/webui_leak_gate.py
    python verify/webui/webui_leak_gate.py --only s1,s3 --rounds 20
    python verify/webui/webui_leak_gate.py --web-port 8099 --server-port 6100

退出码
======
    0  全绿
    1  有失败（断言不成立）
    2  环境起不来（上游 / 网页端 / 浏览器 起不来）

[为什么要这个门]
================
    ① 网页端此前**没有任何泄漏门**：唯一沾边的是
       `verify/hsf_three_end_e2e.py` 的 `section_leak_loop`，但它只用纯
       HTTP 打 API —— 不碰 SSE 长连接、不碰反复订阅、不碰反复导航、
       更不看浏览器 JS 堆 ⇒ `hub.register_subscriber` / `_evict_subscribers`
       / `_reconcile`（退订路径）**零覆盖**。
    ② 网页端的 JS 报错此前**全靠人工看**：`verify/webui/webui_check.py`
       虽然用 CDP 开了浏览器，但从不收 `Runtime.exceptionThrown` /
       `Log.entryAdded` / `console.error` ⇒ 页面里的异常在门禁里是隐形的。

[覆盖场景]
==========
    S1  SSE 连接生命周期
        · N 条 SSE 连接，每条连接内反复 `POST /api/topics` 切主题
          （默认 5 连接 × 20 轮）。
        · 断连后 `/api/status` 的 `subscriberCount` **回落**到基线
          （断开的连接不能留在 `hub.subscribers` 里）。
        · 退订后**上游实际订阅表**（`/api/status` → `upstream.subscriptions`）
          也同步去掉该主题（`hub._reconcile` 的 drop 路径）。
    S2  订阅者上限淘汰
        · 并发开 `MAX_SUBSCRIBERS + N` 条 SSE ⇒ 最老的被淘汰、
          `subscriberCount` 封顶在 `MAX_SUBSCRIBERS`
          （上限值**从产品代码里读**，不写死）。
    S3  反复导航（浏览器侧）
        · 同一文档里把 hash 在 17 个实装页（跳过保留槽位 16）之间轮换
          30 轮，每轮采样：
            - `document.querySelectorAll('*').length`（DOM 节点数）
            - `Performance.getMetrics` 的 `JSHeapUsedSize`（JS 堆）
            - `DOMDebugger.getEventListeners` 的 window/document 监听器数
          判据：DOM 节点数不单调增长；JS 堆三点（第 10/20/30 轮）斜率
          **不随轮数线性增长**。
    S4  进程内存趋势
        · 采样 webui 进程私有内存（Windows PSAPI `PrivateUsage` /
          Linux `/proc/<pid>/status` 的 `VmData` —— 与
          `verify/hsf_three_end_e2e.py` 的 `proc_mem_bytes` **同一实现**）。
        · 判据：每轮增量恒定（第 N 轮增量 ≤ 第 1 轮增量 × 阈值 + 余量）
          + 总增量不持续爬升。
    C   浏览器控制台零异常
        · `Runtime.enable` + `Log.enable`，收集
          `Runtime.exceptionThrown` / `Log.entryAdded(level=error)` /
          `Runtime.consoleAPICalled(type=error)`。
        · 逐页打开 17 个实装页，异常条数必须为 0；有就打印
          **页号 + 异常文本 + 栈首行**。

[诚实标注 —— 这个门能回答什么、不能回答什么]
==============================================
    · **S4 是趋势门，不是堆级泄漏结论**：私有内存是**工作集口径**
      （含分配器 arenas、线程栈、mmap 的 SQLite/WAL 缓冲、连接缓冲…），
      一次 GC 或一次页缓存就能让曲线上下跳几百 KB ⇒ 只能说
      「有没有肉眼可见的持续爬升」，**不能**据此断言「有/没有堆泄漏」。
      堆级口径在浏览器侧（S3 的 `JSHeapUsedSize`）。
    · **S3 的「监听器计数」是弱代理**：`util.js` 的 `makeBus()` **没有**
      暴露只读计数钩子 ⇒ 拿不到「bus 上的订阅者个数」。现退化为
      `DOMDebugger.getEventListeners`（window/document 上的原生监听器），
      它**看不到** bus 内部的事件回调 ⇒ 若产品侧的 bus 订阅泄漏，
      本门**测不出来**。建议在 `util.js` 的 `makeBus()` 里加一个只读
      计数钩子（见交付报告的「建议」段）。
    · **S1/S2 只覆盖数量与集合**，不覆盖「单个订阅者内部推送队列是否涨」；
      后者看 `/api/status` 里每个订阅者的 `queued` 字段（本门在 S1 收尾时
      打印出来做参考；队列本身在产品侧是**有界**的（maxsize=256）⇒ 不会
      无界增长，但「积压」是另一类问题（消费不过来），不在本门职责内）。

[端口纪律]
==========
    · 网页端口**不写死**：环境变量 `HSF_WEB_PORT` → 在跑的 hsfweb →
      候选端口探活 `/api/ping`（认 `server=hsf-webui`）→ 都不满足就
      **自起**一个 `python -m hsfweb --server <上游> --web-port <空闲端口>`。
      本机 8080 常被别人的常驻程序占着 ⇒ 写死端口迟早打错服务。
    · 上游**优先复用常驻 6100**；但要**身份**是 C 版 sim
      （`bin/HsfServer.exe --sim`）—— 6100 上跑着 Python 版 hsfsrv 时
      **不抢**，改在空闲端口自起一个 C sim，且**复制到临时程序根**
      （exe + cfg 复制到临时目录 ⇒ 不碰仓库里的共享配置与共享 SQLite
      库文件；C 端对库文件是独占锁，撞上就「打开库失败 ⇒ 落盘关闭」）。
    · **收尾只杀自己起的进程**（复用的常驻服务原样保留）。

[依赖]
========
    纯标准库 + 同目录的 `cdp_client.py`（极简 CDP 客户端，本轮给它补了
    「事件收集」能力：`Cdp.events` / `drain_events()`，向后兼容）。
"""

import argparse
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
WEBUI_DIR = os.path.join(REPO, "HighSpeedFilter", "webui_py")
#: 诊断用：可用环境变量指向**别处**的 webui 包目录（例如带插桩的副本），
#: 默认仍用仓库里的那份（产品代码一个字都不动）。
WEBUI_DIR = os.environ.get("HSF_WEBUI_DIR") or WEBUI_DIR
SERVER_PY_DIR = os.path.join(REPO, "HighSpeedFilter", "server_py")
BIN_DIR = os.path.join(REPO, "bin")
SIM_EXE = os.path.join(BIN_DIR, "HsfServer.exe")
VERIFY_DIR = os.path.join(REPO, "verify")

sys.path.insert(0, HERE)
import cdp_client                                          # noqa: E402

HOST = "127.0.0.1"

#: 上游候选端口（题目口径：优先复用常驻 6100 的 C 端 sim）。
UPSTREAM_PREFERRED = 6100

#: 网页端口候选（**不写死一个数**）：环境变量 → 常驻 hsfweb → 这些候选。
WEB_PORT_CANDIDATES = (8080, 8085, 8086, 8087, 8088, 8089, 8090)

#: Edge 候选位置（同 web_sweep_edge.py 的口径）。
EDGE_CANDIDATES = (
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    r"C:/Program Files/Google/Chrome/Application/chrome.exe",
)

# ---- 主题号（与 webui proto.py 同值） ----
T_VFD = 0x00010001
T_AI = 0x00010002
T_TEMPHUM = 0x00010003
T_FLOW = 0x00010004
T_HEALTH = 0x00010005
T_DIGITAL = 0x00010006
T_VALVE = 0x00010007
T_TURB = 0x00010009
T_PUMP = 0x0001000A
T_AUTO_ALARM = 0x0001000B
T_CAMERA = 0x0001000C

#: S1 用来切主题的**非全局常驻**主题（T_VFD 不在 GLOBAL_TOPICS 里 ⇒
#: 退订后能从上游实际订阅表里干净地看出来）。
S1_TOPIC_SETS = (
    [T_VFD],
    [T_AI, T_TEMPHUM],
    [T_FLOW],
    [],
    [T_VFD, T_HEALTH, T_DIGITAL],
)

#: S4 判据阈值（与 `verify/hsf_three_end_e2e.py` 的 LEAK_TOL_BYTES 同口径：
#: 稳态轮每轮私有内存增量上限 3MB；总增量上限 40MB）。
LEAK_TOL_BYTES = 3.0 * 1024 * 1024
LEAK_TOTAL_CAP_BYTES = 40 * 1024 * 1024

#: S3 判据：JS 堆三点斜率的「不线性增长」容忍倍数与绝对余量。
HEAP_SLOPE_FACTOR = 2.0
HEAP_SLOPE_SLACK = 2 * 1024 * 1024
#: S3 判据：DOM 节点数相对基线的允许涨幅（相对值 + 绝对值）。
DOM_GROWTH_RATIO = 1.35
DOM_GROWTH_SLACK = 120

_passed = 0
_failed = []
_section = ["?"]


# ============================================================================
#                          断言输出框架
# ============================================================================

def check(name, cond, detail=""):
    """一条硬断言（失败带段名与详情）。"""
    global _passed
    label = "[%s] %s" % (_section[0], name)
    if cond:
        _passed += 1
        print("  PASS  %s" % name)
    else:
        _failed.append((label, detail))
        print("  FAIL  %s  %s" % (name, detail))
    return cond


def step(msg):
    print("    · %s" % msg)


class Section(object):
    """段落上下文（失败定位用）。"""

    def __init__(self, title):
        self.title = title

    def __enter__(self):
        _section[0] = self.title
        print("\n=== %s ===" % self.title)
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc is not None:
            check("段落异常退出", False,
                  "%s: %s" % (type(exc).__name__, exc))
            return True                      # 已记为失败，不再外抛
        return False


# ============================================================================
#                          HTTP / 端口小工具
# ============================================================================

def http_req(port, method, path, body=None, token=None, timeout=15.0):
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
        return resp.status, dict(resp.getheaders()), resp.read()
    finally:
        conn.close()


def http_json(port, method, path, body=None, token=None, timeout=15.0):
    status, headers, data = http_req(port, method, path, body, token, timeout)
    try:
        doc = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        doc = None
    return status, headers, doc


def tcp_open(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def port_busy(port):
    """端口上有服务在听（用**连接**判定，不用 bind）。

    [为什么不用 bind] Windows 上 127.0.0.1:8080 与 0.0.0.0:8080 可以共存
    （后绑定者截走该地址的流量）⇒ bind 成功不代表端口空闲。
    """
    return tcp_open(HOST, port, timeout=0.5)


def free_port(start):
    port = int(start)
    while port < int(start) + 320:
        if not port_busy(port):
            return port
        port += 1
    raise RuntimeError("从 %s 起找不到空闲端口" % start)


def wait_tcp(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if tcp_open(HOST, port, timeout=0.8):
            return True
        time.sleep(0.5)
    return False


def poll_until(fn, timeout, interval=0.5, label=""):
    """轮询直到 fn() 返回真值；返回最后一个真值（或 None）。"""
    deadline = time.time() + timeout
    result = None
    while time.time() < deadline:
        result = fn()
        if result:
            return result
        time.sleep(interval)
    if label:
        step("轮询超时：%s（%.0fs）" % (label, timeout))
    return result


# ============================================================================
#                          进程自管（只清理自己起的）
# ============================================================================

def _run_text(argv, timeout=8.0):
    try:
        proc = subprocess.run(argv, capture_output=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return ""
    return (proc.stdout or b"").decode("utf-8", "replace")


def _spawn(label, argv, cwd):
    """起子进程（stdout/stderr 落**具名**临时文件，失败时能取日志取证）。"""
    out_fd, out_path = tempfile.mkstemp(prefix="webui_leak_%s_out_" % label)
    err_fd, err_path = tempfile.mkstemp(prefix="webui_leak_%s_err_" % label)
    os.close(out_fd)
    os.close(err_fd)
    out = open(out_path, "wb")
    err = open(err_path, "wb")
    proc = subprocess.Popen(argv, cwd=cwd, stdout=out, stderr=err)
    out.close()
    err.close()
    step("起 %s：pid=%d %s（cwd=%s）" % (label, proc.pid,
                                         " ".join(argv[:4]), cwd))
    return proc, out_path, err_path


def _dump_tail(path, limit=30):
    """读日志文件尾部（诊断用）。"""
    if not path or not os.path.isfile(path):
        return "(无日志文件：%s)" % path
    try:
        with open(path, "rb") as fh:
            data = fh.read()[-8000:]
        lines = data.decode("utf-8", "replace").splitlines()
        return "\n".join(lines[-limit:])
    except Exception as exc:                            # noqa: BLE001
        return "(读输出失败：%s)" % exc


def _kill_tree(proc):
    """杀进程**树**（Edge 会派生 gpu/renderer 子进程 ⇒ 只杀主进程会留一堆）。

    [为什么要按树杀] 2026-11-18 实锅：只 terminate 主进程 ⇒ 子进程活下来
    ⇒ 继续占着 CDP 端口与 profile 目录（一轮跑完残留 489 个进程、
    29GB profile ⇒ 系统盘满 + 下一轮「找不到空闲端口」）。
    """
    if proc is None:
        return
    if os.name == "nt":
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True, timeout=30)
            return
        except Exception:                                # noqa: BLE001
            pass
    try:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
    except Exception:                                    # noqa: BLE001
        try:
            proc.kill()
        except Exception:                                # noqa: BLE001
            pass


def _rm_profile(path):
    """删临时浏览器 profile（Windows 上文件锁要过一会儿才释放 ⇒ 重试）。"""
    if not path:
        return
    for _ in range(4):
        if not os.path.isdir(path):
            return
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.isdir(path):
            return
        time.sleep(1.0)


def stop_proc(proc, label, outs):
    """杀自己起的进程并确认真消失。"""
    if proc is None:
        return
    _kill_tree(proc)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        step("(WARN) %s 未如期退出 pid=%s" % (label, proc.pid))


class Owned(object):
    """自己起的进程集合（收尾统一杀）+ 收尾要删的临时目录/日志文件。"""

    def __init__(self):
        self.items = []                  # (label, proc, [日志路径…])
        self.dir_cleanups = []          # 收尾要删的临时目录

    def add(self, label, proc, handles):
        self.items.append((label, proc, handles))

    def log_tail(self, contains="webui", limit=40):
        """取自己起的某个进程的服务端日志尾部（失败取证用）。"""
        out = []
        for label, _proc, paths in self.items:
            if contains and contains not in label:
                continue
            for path in paths:
                tail = _dump_tail(path, limit)
                if tail:
                    out.append("--- %s 日志尾部 ---\n%s" % (label, tail))
        return "\n".join(out)

    def cleanup(self):
        for label, proc, handles in reversed(self.items):
            print("[清理] 停自己起的 %s（pid=%s）" % (label, proc.pid))
            stop_proc(proc, label, handles)
            # 日志临时文件用完即删（失败时正文已在 run() 里打印过尾部）。
            for path in handles or ():
                try:
                    os.remove(path)
                except OSError:
                    pass
        self.items = []
        for path in self.dir_cleanups:
            print("[清理] 删临时目录 %s" % path)
            shutil.rmtree(path, ignore_errors=True)
        self.dir_cleanups = []


# ============================================================================
#                     进程 / 端口身份判定（谁在听、是不是 C 端）
# ============================================================================

def listener_pid(port):
    """谁在监听这个 TCP 端口（None = 查不到 / 没人在听）。"""
    if os.name == "nt":
        out = _run_text(["netstat", "-ano", "-p", "TCP"])
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP" and parts[3] == "LISTENING":
                if parts[1].rsplit(":", 1)[-1] == str(port):
                    try:
                        return int(parts[4])
                    except ValueError:
                        return None
        return None
    out = _run_text(["ss", "-ltnp"])
    for line in out.splitlines():
        if (":%d " % port) in line or line.rstrip().endswith(":%d" % port):
            for token in line.split():
                if ":" in token and token.split(":")[-1].isdigit():
                    return int(token.split(":")[-1])
    return None


def proc_cmdline(pid):
    """进程命令行（查不到返回空串）。"""
    if os.name == "nt":
        out = _run_text(["wmic", "process", "where",
                         "ProcessId=%d" % int(pid), "get", "CommandLine",
                         "/format:list"])
        for line in out.splitlines():
            if line.lower().startswith("commandline="):
                return line.split("=", 1)[1].strip()
        out = _run_text(["powershell", "-NoProfile", "-Command",
                         "(Get-CimInstance Win32_Process -Filter "
                         "'ProcessId=%d').CommandLine" % int(pid)])
        return out.strip().splitlines()[0] if out.strip() else ""
    try:
        with open("/proc/%d/cmdline" % int(pid), "rb") as fh:
            return fh.read().decode("utf-8", "replace").replace("\x00", " ")
    except OSError:
        return ""


def upstream_is_c_sim(port):
    """监听该端口的是不是 C 版 sim（bin/HsfServer.exe）。

    :returns: (是否 C sim, 说明文字)
    """
    pid = listener_pid(port)
    if pid is None:
        return False, "端口 %d 没人在听" % port
    cmd = proc_cmdline(pid)
    if not cmd:
        return False, "端口 %d 的进程 pid=%d 命令行查不到" % (port, pid)
    low = cmd.lower()
    if "hsfserver.exe" in low:
        return True, "pid=%d %s" % (pid, cmd[:110])
    if "hsfsrv" in low:
        return False, ("pid=%d 是 **Python 版** hsfsrv（不是 C 端）：%s"
                       % (pid, cmd[:110]))
    return False, "pid=%d 认不出实现：%s" % (pid, cmd[:110])


def hsfweb_ping(port, timeout=2.5):
    """这个端口上是不是 hsfweb（探 /api/ping 的 server 标记）。"""
    try:
        status, _h, body = http_req(port, "GET", "/api/ping", timeout=timeout)
    except (OSError, http.client.HTTPException):
        return False
    if status != 200:
        return False
    try:
        doc = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return False
    return bool(doc.get("ok")) and doc.get("server") == "hsf-webui"


def wait_hsfweb(port, timeout=30.0):
    """等这个端口上的**是 hsfweb**（认 server 标记，不只看 HTTP 200）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if hsfweb_ping(port):
            return True
        time.sleep(0.4)
    return False


def web_login(port, user, password, attempts=5):
    """登录；瞬时拒绝按 8 秒间隔重试（DenyNew 回收窗口）。

    :returns: (status, headers, doc)
    """
    last = (0, {}, None)
    for i in range(attempts):
        status, headers, doc = http_json(port, "POST", "/api/login",
                                         {"user": user, "password": password},
                                         timeout=20.0)
        if status == 200 and isinstance(doc, dict) and doc.get("ok"):
            return status, headers, doc
        last = (status, headers, doc)
        err = str((doc or {}).get("err", "")) if isinstance(doc, dict) else ""
        low = err.lower()
        permanent = ("bad password" in low or "口令" in err or "密码" in err
                     or "unknown user" in low or "locked" in low)
        transient = (not permanent) and (
            "authentication rejected" in low or "已在线" in err
            or "并发" in err or "上限" in err or "超时" in err
            or "连不上" in err or "无应答" in err)
        if transient and i < attempts - 1:
            step("登录被瞬时拒（%s），8s 后重试（%d/%d）"
                 % (err[:60], i + 1, attempts))
            time.sleep(8.0)
            continue
        break
    return last


# ============================================================================
#                     进程私有内存（与 hsf_three_end_e2e 同一实现）
# ============================================================================

def _proc_mem_windows(pid):
    import ctypes
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    k32 = ctypes.windll.kernel32

    class PMC_EX(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong),
                    ("PageFaultCount", ctypes.c_ulong),
                    ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t),
                    ("PrivateUsage", ctypes.c_size_t)]

    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
    if not h:
        return None
    try:
        pmc = PMC_EX()
        pmc.cb = ctypes.sizeof(PMC_EX)
        if not ctypes.windll.psapi.GetProcessMemoryInfo(
                h, ctypes.byref(pmc), pmc.cb):
            return None
        return int(pmc.PrivateUsage)
    finally:
        k32.CloseHandle(h)


def _proc_mem_linux(pid):
    try:
        with open("/proc/%d/status" % pid, "r", encoding="utf-8",
                  errors="replace") as fh:
            for line in fh:
                if line.startswith("VmData:"):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass
    return None


def proc_mem_bytes(pid):
    """进程私有内存字节数：Windows=PSAPI PrivateUsage（≈VmData 口径），
    Linux=/proc/<pid>/status 的 VmData；读不到返回 None。

    [为什么不是自己另写一套] `verify/hsf_three_end_e2e.py` 已有同一口径
    的实现（那里叫 proc_mem_bytes）⇒ 本门**优先直接复用它**（同源 ⇒
    两边的曲线可横向比）；导入失败（例如那台机器没有 PyQt5）时退回下面
    这份**逐字同源**的副本，保证纯标准库可跑。
    """
    try:
        if VERIFY_DIR not in sys.path:
            sys.path.insert(0, VERIFY_DIR)
        from hsf_three_end_e2e import proc_mem_bytes as _upstream
        return _upstream(pid)
    except Exception:                                    # noqa: BLE001
        if os.name == "nt":
            return _proc_mem_windows(pid)
        return _proc_mem_linux(pid)


# ============================================================================
#                        产品常量（从代码里读，不写死）
# ============================================================================

def read_max_subscribers():
    """从产品代码里读 SSE 订阅者上限（hub.WebHub.MAX_SUBSCRIBERS）。

    [为什么不写死 32] 上限是产品口径，改了产品而门禁还按老数字判 ⇒
    门禁要么假红要么假绿 ⇒ **必须跟着代码走**。
    """
    if WEBUI_DIR not in sys.path:
        sys.path.insert(0, WEBUI_DIR)
    try:
        from hsfweb.hub import WebHub
        return int(WebHub.MAX_SUBSCRIBERS), "hsfweb.hub.WebHub.MAX_SUBSCRIBERS"
    except Exception as exc:                             # noqa: BLE001
        # 退路：直接从源码里正则读（不 import ⇒ 无副作用）。
        path = os.path.join(WEBUI_DIR, "hsfweb", "hub.py")
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                if line.strip().startswith("MAX_SUBSCRIBERS"):
                    num = line.split("=", 1)[1].split("#")[0].strip()
                    return int(num), "hub.py 源码正则（import 失败：%s）" % exc
    raise RuntimeError("读不到 MAX_SUBSCRIBERS")


def read_global_topics():
    """全局常驻主题（hub.GLOBAL_TOPICS）——退订断言要用它做对照。"""
    if WEBUI_DIR not in sys.path:
        sys.path.insert(0, WEBUI_DIR)
    from hsfweb.hub import GLOBAL_TOPICS
    return [int(t) for t in GLOBAL_TOPICS]


# ============================================================================
#                       SSE 客户端（可控、可读、可关）
# ============================================================================

class SseClient(object):
    """一条 SSE 连接：记 connId、后台读流、知道服务端什么时候收口。

    [为什么要自己写而不是复用 webui_check.SseCollector] 那个收集器是
    「长跑验收」取向（断了自动重连）；本门要的是**能明确关掉并观察
    服务端何时收口**（S1 的回落断言、S2 的淘汰断言）⇒ 重连语义会毁掉
    断言 ⇒ 这里另写一个「不重连」的最小实现。
    """

    def __init__(self, port, token, name="sse"):
        self.port = port
        self.token = token
        self.name = name
        self.conn_id = None
        self.events = 0
        self.closed_by_peer = False
        self.error = ""
        self.conn = None
        self.resp = None
        self._lock = threading.Lock()
        self._thread = None

    def open(self, timeout=20.0):
        try:
            conn = http.client.HTTPConnection(HOST, self.port, timeout=timeout)
            conn.request("GET", "/api/stream", headers={
                "Authorization": "Bearer %s" % self.token,
                "Accept": "text/event-stream"})
            resp = conn.getresponse()
        except (OSError, http.client.HTTPException) as exc:
            self.error = repr(exc)
            return False
        self.conn = conn
        self.resp = resp
        self.conn_id = resp.getheader("X-Hsf-Conn-Id")
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return bool(self.conn_id)

    def _run(self):
        try:
            while True:
                line = self.resp.readline()
                if not line:
                    self.closed_by_peer = True
                    break
                text = line.decode("utf-8", "replace").strip()
                if text.startswith("data: "):
                    with self._lock:
                        self.events += 1
        except Exception as exc:                            # noqa: BLE001
            self.error = "%s: %s" % (type(exc).__name__, exc)
            self.closed_by_peer = True
        finally:
            self.closed_by_peer = True

    def close(self):
        try:
            if self.conn is not None:
                self.conn.close()
        except OSError:
            pass


# ============================================================================
#                             环境解析
# ============================================================================

def start_c_sim(port, owned):
    """自起一个 C 端 sim：exe + 配置**复制到临时程序根**再跑。

    [为什么要复制程序根] C 服务端的配置/账户库/历史库都落在 <程序根>/cfg/
    ⇒ 直接在 bin/ 起会与常驻 C sim 抢同一个 SQLite 文件（C 端对库文件是
    **独占**锁 ⇒ 撞上就「打开库失败 ⇒ 落盘关闭」）；复制到临时目录 ⇒
    不碰共享配置与共享数据文件，收尾直接删目录。
    """
    tmp = tempfile.mkdtemp(prefix="webui_leak_csim_%d_" % port)
    os.makedirs(os.path.join(tmp, "cfg"), exist_ok=True)
    shutil.copy2(SIM_EXE, os.path.join(tmp, "HsfServer.exe"))
    src_ini = os.path.join(BIN_DIR, "cfg", "hsf_server.ini")
    dst_ini = os.path.join(tmp, "cfg", "hsf_server.ini")
    text = ""
    if os.path.isfile(src_ini):
        with open(src_ini, "r", encoding="utf-8") as fh:
            text = fh.read()
    db_path = os.path.join(tmp, "hsf_data.db").replace("\\", "\\\\")
    out = []
    for line in text.splitlines():
        if line.startswith("path="):
            line = "path=" + db_path
        out.append(line)
    if not any(l.startswith("[storage]") for l in out):
        out += ["[storage]", "enabled=1", "sample_ms=1000", "path=" + db_path]
    with open(dst_ini, "w", encoding="utf-8", newline="") as fh:
        fh.write("\r\n".join(out) + "\r\n")
    for name in ("hsf_users.json", "hsf_login_policy.json"):
        src = os.path.join(BIN_DIR, "cfg", name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(tmp, "cfg", name))
    proc, out_h, err_h = _spawn("c-sim(%d)" % port,
                                 [os.path.join(tmp, "HsfServer.exe"),
                                  str(port), "--sim"], tmp)
    owned.add("c-sim(%d)" % port, proc, [out_h, err_h])
    owned.dir_cleanups.append(tmp)
    ok = wait_tcp(port, 40.0)
    if not ok:
        step("C sim 起不来：%s" % _dump_tail(err_h))
    return ok


def resolve_upstream(args, owned):
    """决定本门用的上游端口。

    6100 优先复用，但**要身份**是 C 版 sim；不是就**不抢**，在空闲端口
    自起一个 C sim（临时程序根，隔离共享库；自己起的 ⇒ 收尾停掉）。
    """
    want = int(args.server_port)
    is_c, why = upstream_is_c_sim(want)
    if is_c:
        step("复用常驻上游 %d（C sim：%s）" % (want, why))
        return want, "C 端：复用常驻 %d（C sim）" % want
    port = want if not port_busy(want) else free_port(want + 1)
    step("上游 %d 上不是 C sim（%s）→ 不抢，另在 %d 自起 C sim（临时程序根）"
         % (want, why.split("：")[0], port))
    if not start_c_sim(port, owned):
        return 0, "自起 C sim 失败"
    return port, "C 端：%d 不是 C sim → 自起 %d（临时程序根）" % (want, port)


def resolve_webui(upstream_port, preferred, owned):
    """网页服务器：环境变量 → 常驻 hsfweb → 候选端口 → 自起。

    :returns: (web_port, pid, 说明) —— pid=None 表示复用别人的进程
              （S4 的内存采样此时拿不到 ⇒ 只能跳过 S4 的进程口径）。
    """
    # ① 环境变量
    env_port = os.environ.get("HSF_WEB_PORT")
    if env_port:
        try:
            env_port = int(env_port)
        except ValueError:
            env_port = 0
    if env_port and hsfweb_ping(env_port):
        step("用环境变量 HSF_WEB_PORT=%d（是 hsfweb）" % env_port)
        return env_port, listener_pid(env_port), "复用环境变量端口 %d" % env_port
    # ② 显式 --web-port
    if preferred and hsfweb_ping(preferred):
        step("复用 %d 上的 hsfweb" % preferred)
        return preferred, listener_pid(preferred), "复用 %d" % preferred
    # ③ 候选端口探活
    for cand in WEB_PORT_CANDIDATES:
        if hsfweb_ping(cand):
            step("复用候选端口上的 hsfweb：%d" % cand)
            return cand, listener_pid(cand), "复用候选端口 %d" % cand
    # ④ 自起（**显式 --web-port**：缺省 8080 被占时 hsfweb 会改听随机端口，
    #    脚本自己都找不到它）
    port = preferred or WEB_PORT_CANDIDATES[1]
    if port_busy(port):
        port = free_port(port + 1)
    argv = [sys.executable, "-m", "hsfweb", "--server", HOST,
            "--port", str(upstream_port), "--bind", HOST,
            "--web-port", str(port)]
    proc, out_h, err_h = _spawn("webui(%d→%d)" % (port, upstream_port),
                                argv, WEBUI_DIR)
    owned.add("webui(%d)" % port, proc, [out_h, err_h])
    if not wait_hsfweb(port, 30.0):
        step("webui 起不来：%s" % _dump_tail(err_h))
        return 0, 0, "自起 webui 失败"
    return port, proc.pid, "自起 webui %d（上游 %d）" % (port, upstream_port)


# ============================================================================
#                               场景 S1
# ============================================================================

def sec_s1(ctx, args):
    """S1：SSE 连接生命周期（切主题 → 断连 → 计数回落 + 上游退订）。"""
    with Section("S1 SSE 连接生命周期（切主题 / 断连回落 / 上游退订）"):
        base = ctx.subscriber_count()
        check("S1 前基线 subscriberCount（应为 0）", base == 0,
              "subscriberCount=%d" % base)

        conns = []
        for i in range(args.sse_conns):
            c = SseClient(ctx.web, ctx.token, "s1-%d" % i)
            if not c.open():
                check("SSE 连接 #%d 建立" % i, False, c.error or "无 connId")
                break
            conns.append(c)
        check("开了 %d 条 SSE 连接（都拿到 connId）" % args.sse_conns,
              len(conns) == args.sse_conns,
              "connIds=%s" % [c.conn_id for c in conns])

        count = ctx.subscriber_count()
        check("subscriberCount 随连接数上涨（= %d）" % (base + len(conns)),
              count == base + len(conns),
              "subscriberCount=%d" % count)

        # ---- 每条连接内反复切主题（并发） ----
        errors = []

        def _switch_loop(client):
            for r in range(args.sse_rounds):
                topics = S1_TOPIC_SETS[r % len(S1_TOPIC_SETS)]
                try:
                    status, _h, doc = http_json(
                        ctx.web, "POST", "/api/topics",
                        {"connId": int(client.conn_id), "topics": topics},
                        token=ctx.token, timeout=15.0)
                    if status != 200 or (doc or {}).get("applied") != 1:
                        errors.append("conn=%s 轮=%d status=%s doc=%s"
                                      % (client.conn_id, r, status,
                                         str(doc)[:120]))
                except Exception as exc:                    # noqa: BLE001
                    errors.append("conn=%s 轮=%d %s: %s"
                                  % (client.conn_id, r, type(exc).__name__, exc))

        threads = [threading.Thread(target=_switch_loop, args=(c,))
                   for c in conns]
        t0 = time.time()
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        dur = time.time() - t0
        check("每条连接切主题 %d 轮全部成功（%d 连接并发）"
              % (args.sse_rounds, len(conns)), not errors,
              "耗时 %.1fs；首个错误：%s" % (dur, errors[0] if errors else ""))

        # ---- 收尾：把所有连接统一订成**一个非全局常驻主题**（T_VFD）----
        # [为什么要有这一步] 退订断言需要一个「一定会被退掉、且不在全局
        # 常驻集合里」的主题做探针：全局常驻主题（温湿度/健康/自动告警）
        # **永远**在上游订阅表里 ⇒ 拿它当探针 ⇒ 断言永远不可能成立
        # （本门第一版就踩了这个：拿 HEALTH 当探针 ⇒ 假红）。
        for c in conns:
            http_json(ctx.web, "POST", "/api/topics",
                      {"connId": int(c.conn_id), "topics": [T_VFD]},
                      token=ctx.token, timeout=15.0)
        probe = T_VFD
        desired = ctx.status().get("desiredTopics") or []
        check("切主题后 desiredTopics 含探针主题 T_VFD",
              probe in set(desired),
              "desired=%s（探针=0x%X）" % (sorted(desired), probe))
        up = poll_until(lambda: (ctx.upstream_subs()
                                 if probe in (ctx.upstream_subs() or []) else None),
                        15, interval=1.0, label="上游实际订阅表含 T_VFD")
        check("上游**实际**订阅表含探针主题 T_VFD",
              up is not None and probe in (up or []),
              "upstream=%s（探针=0x%X）" % (sorted(up or []), probe))

        # ---- 断连 ⇒ subscriberCount 回落 ----
        # [顺带采样] 每个订阅者的推送积压（queued）——产品侧队列有界
        # （maxsize=256）⇒ 这里只做参考打印（积压大 = 消费跟不上）。
        queued = [(s.get("connId"), s.get("queued"), s.get("dropped"))
                  for s in ctx.subscriber_detail()]
        step("订阅者积压（connId, queued, dropped）：%s" % queued)
        for c in conns:
            c.close()
        t0 = time.time()
        got = poll_until(lambda: ctx.subscriber_count() == base, 25,
                         interval=0.5, label="subscriberCount 回落")
        check("断连后 subscriberCount 回落到基线 %d" % base,
              got is not None, "当前=%d（耗时 %.1fs）｜%s"
              % (ctx.subscriber_count(), time.time() - t0, ctx.status_note()))

        # ---- 上游实际订阅表同步退订（只剩全局常驻主题） ----
        globals_ = ctx.global_topics

        def _up_clean():
            subs = set(ctx.upstream_subs() or [])
            return sorted(subs) if probe not in subs else None

        up_after = poll_until(_up_clean, 25, interval=1.0,
                              label="上游退订收敛")
        st = ctx.status()
        up_full = st.get("upstream") or {}
        check("退订后上游**实际**订阅表去掉探针主题（只剩全局常驻）",
              up_after is not None and set(up_after) <= set(globals_),
              "upstream=%s（全局常驻=%s）｜链接状态=%s ready=%s "
              "reconnect=%s error=%s desired=%s｜状态读取=%s"
              % (sorted(up_after or []), sorted(globals_),
                 up_full.get("state"), up_full.get("ready"),
                 up_full.get("reconnectCount"), up_full.get("error"),
                 sorted(st.get("desiredTopics") or []),
                 ctx.status_note()))


# ============================================================================
#                               场景 S2
# ============================================================================

def sec_s2(ctx, args):
    """S2：订阅者上限淘汰（LRU）。"""
    with Section("S2 订阅者上限淘汰（MAX_SUBSCRIBERS 封顶 + 最老被淘汰）"):
        limit, src = read_max_subscribers()
        step("订阅者上限 MAX_SUBSCRIBERS=%d（来源：%s）" % (limit, src))
        base = ctx.subscriber_count()
        extra = args.sse_extra
        total = limit + extra
        conns = []
        for i in range(total):
            c = SseClient(ctx.web, ctx.token, "s2-%d" % i)
            if not c.open():
                step("SSE 连接 #%d 建立失败：%s" % (i, c.error))
                break
            conns.append(c)
        count = ctx.subscriber_count()
        check("开了 %d 条（= 上限 %d + %d）后 subscriberCount 封顶在上限"
              % (total, limit, extra),
              count <= limit, "subscriberCount=%d（上限 %d，连上 %d 条）"
              % (count, limit, len(conns)))
        check("subscriberCount 正好等于上限（没多也没少）",
              count == limit, "subscriberCount=%d" % count)

        # 被淘汰的那些连接，服务端会主动收口 ⇒ 客户端读到 EOF。
        # [为什么要用共享 deadline] 逐条 wait_closed(20) 会变成
        # 38×20s 的串行等待 ⇒ 必须给所有连接一个**共同的**截止时间。
        deadline = time.time() + 25
        evicted = []
        while time.time() < deadline:
            evicted = [c for c in conns if c.closed_by_peer]
            if len(evicted) >= extra:
                break
            time.sleep(0.3)
        check("超上限的 %d 条连接被服务端收口（LRU 淘汰生效）" % extra,
              len(evicted) >= extra,
              "被收口 %d 条（期望 ≥%d）；仍在线 %d 条"
              % (len(evicted), extra, len(conns) - len(evicted)))
        alive = [c for c in conns if not c.closed_by_peer]
        step("仍在线 %d 条（上限 %d）" % (len(alive), limit))

        for c in conns:
            c.close()
        got = poll_until(lambda: ctx.subscriber_count() == base, 25,
                         interval=0.5, label="subscriberCount 回落")
        check("收尾后 subscriberCount 回落到基线 %d" % base,
              got is not None, "当前=%d" % ctx.subscriber_count())


# ============================================================================
#                        场景 S3 + 控制台门（浏览器侧）
# ============================================================================

class Browser(object):
    """极简浏览器驱动：一个 Edge 实例、一个文档、hash 翻页 + 采样。"""

    def __init__(self, web_port, token):
        self.web_port = web_port
        self.token = token
        self.proc = None
        self.profile = None
        self.cdp = None
        self.current_page = -1
        self.console = []            # 收集到的异常/错误条目

    # ---- 生命周期 ----
    def start(self):
        edge = None
        for path in EDGE_CANDIDATES:
            if os.path.isfile(path):
                edge = path
                break
        if edge is None:
            return False, "找不到浏览器（%s）" % (EDGE_CANDIDATES,)
        self.profile = tempfile.mkdtemp(prefix="webui_leak_edge_")
        port = _free_debug_port()
        url = ("http://%s:%d/?authtoken=%s#/page/0"
               % (HOST, self.web_port, self.token))
        cmd = [edge, "--headless=new", "--disable-gpu", "--no-first-run",
               "--user-data-dir=" + self.profile,
               "--remote-debugging-port=%d" % port,
               "--window-size=1280,800", url]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        try:
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            self.cdp = cdp_client.Cdp(ws_url, timeout=60.0)
            self.cdp.enable_console()          # Page + Runtime + Log
            self.cdp.call("Performance.enable")
            self.cdp.call("Emulation.setDeviceMetricsOverride",
                          {"width": 1280, "height": 800,
                           "deviceScaleFactor": 1, "mobile": False})
        except Exception as exc:                            # noqa: BLE001
            return False, "CDP 连接失败：%s: %s" % (type(exc).__name__, exc)
        return True, "ok"

    def stop(self):
        try:
            if self.cdp is not None:
                self.cdp.close()
        except Exception:                                   # noqa: BLE001
            pass
        _kill_tree(self.proc)
        try:
            if self.proc is not None:
                self.proc.wait(timeout=10)
        except Exception:                                   # noqa: BLE001
            pass
        _rm_profile(self.profile)

    # ---- 页面操作 ----
    def goto(self, page):
        """切到某页（hash 路由 ⇒ 同一文档内翻页）。"""
        self.current_page = int(page)
        self.cdp.evaluate("window.HSF && HSF.router.go(%d)" % int(page))

    def current_hash(self):
        val = self.cdp.evaluate("location.hash")
        return val if isinstance(val, str) else ""

    def implemented_pages(self, candidates):
        """候选页号里**前端真注册了模块**的那些（= 实装页）。

        [为什么不用后端 nav 树筛] nav 树里没有「主菜单(1)」——它是主页
        菜单卡片进的页（router.js 注释：「主菜单(1) 仍可从导航/主界面
        菜单卡片进入」）⇒ 按 nav 筛会漏掉第 1 页 ⇒ 门禁少测一页。
        ⇒ 判据改成前端自己的注册表 `HSF.router.has(n)`（保留槽位 16
        没有模块 ⇒ 自动被排除）。
        """
        expr = ("[" + ",".join(str(c) for c in candidates) + "].filter("
                "function(n){return !!(window.HSF && HSF.router && "
                "HSF.router.has(n));})")
        out = self.cdp.evaluate(expr)
        if isinstance(out, list) and out:
            return [int(x) for x in out]
        return list(candidates)

    def wait_ready(self, timeout=25.0, settle=0.45, expect_page=None):
        """等页面挂载完成（hash 到位 + 页容器非空 + 静默一小段）。

        [为什么要核对 hash] `HSF.router.go` 失败时（比如 JS 还没装配完）
        页面根本不会切 ⇒ 只看「容器非空」会把**上一页**当成新页 ⇒
        采样点错位、泄漏判据失真 ⇒ 必须核对 location.hash。
        """
        deadline = time.time() + timeout
        last = -1
        stable = 0
        while time.time() < deadline:
            if expect_page is not None:
                want = "#/page/%d" % int(expect_page)
                if self.current_hash() != want:
                    time.sleep(0.2)
                    continue
            n = self.cdp.evaluate(
                "(document.getElementById('page-host')||{}).childElementCount")
            n = int(n) if isinstance(n, (int, float)) else -1
            if n > 0 and n == last:
                stable += 1
                if stable >= 2:
                    time.sleep(settle)
                    return True
            else:
                stable = 0
            last = n
            time.sleep(0.35)
        return False

    def sample(self):
        """采一份页面侧数据：DOM 节点数 / JS 堆 / 监听器数。"""
        nodes = self.cdp.evaluate("document.querySelectorAll('*').length")
        try:
            metrics = self.cdp.performance_metrics()
        except Exception:                                   # noqa: BLE001
            metrics = {}
        listeners = self._listener_counts()
        return {
            "page": self.current_page,
            "nodes": int(nodes) if isinstance(nodes, (int, float)) else -1,
            "heap": int(metrics.get("JSHeapUsedSize") or 0),
            "listeners": listeners,
        }

    def _listener_counts(self):
        """window / document 上的原生监听器个数（DOMDebugger 口径）。

        [为什么是弱代理] bus（util.js 的 makeBus）内部的回调**不在**这里
        ⇒ 产品若在 bus 上漏退订，本计数看不出来（见文件头「诚实标注」）。
        """
        out = {}
        for name, expr in (("window", "window"), ("document", "document")):
            try:
                res = self.cdp.call("Runtime.evaluate",
                                    {"expression": expr, "returnByValue": False})
                oid = (res.get("result") or {}).get("objectId")
                if not oid:
                    continue
                lst = self.cdp.call("DOMDebugger.getEventListeners",
                                    {"objectId": oid})
                out[name] = len(lst.get("listeners") or [])
                self.cdp.call("Runtime.releaseObject", {"objectId": oid})
            except Exception:                               # noqa: BLE001
                out[name] = -1
        return out

    # ---- 控制台 / 异常收集 ----
    def collect(self, page):
        """把缓冲区里的异常/错误取出来，归到当前页。

        [为什么要先 evaluate 一次] CDP 的事件是异步推来的，只有在
        下一次调用等待应答时才会被读出 ⇒ 先做一次廉价 evaluate 把
        socket 缓冲冲出来，再取事件。
        """
        self.cdp.evaluate("1")                       # 冲事件
        events = self.cdp.drain_events()
        out = []
        for ev in events:
            method = ev.get("method")
            params = ev.get("params") or {}
            if method == "Runtime.exceptionThrown":
                det = params.get("exceptionDetails") or {}
                text = det.get("text") or ""
                exc = det.get("exception") or {}
                desc = exc.get("description") or exc.get("value") or text
                stack = ((det.get("stackTrace") or {}).get("callFrames")
                         or [])
                first = ""
                if stack:
                    cf = stack[0]
                    first = "%s:%s:%s" % (cf.get("url") or "?",
                                          cf.get("lineNumber"),
                                          cf.get("columnNumber"))
                out.append({"kind": "exceptionThrown", "page": page,
                            "text": str(desc)[:400], "stack": first})
            elif method == "Runtime.consoleAPICalled":
                if params.get("type") != "error":
                    continue
                args = params.get("args") or []
                texts = []
                for a in args:
                    if isinstance(a, dict):
                        texts.append(str(a.get("value") if "value" in a
                                         else a.get("description") or ""))
                    else:
                        texts.append(str(a))
                out.append({"kind": "console.error", "page": page,
                            "text": " ".join(texts)[:400],
                            "stack": _console_stack(params)})
            elif method == "Log.entryAdded":
                entry = params.get("entry") or {}
                if entry.get("level") != "error":
                    continue
                out.append({"kind": "log.error", "page": page,
                            "text": str(entry.get("text") or "")[:400],
                            "stack": str(entry.get("url") or "")})
        self.console.extend(out)
        return out


def _console_stack(params):
    stack = params.get("stackTrace") or {}
    frames = stack.get("callFrames") or []
    if not frames:
        return ""
    cf = frames[0]
    return "%s:%s:%s" % (cf.get("url") or "?", cf.get("lineNumber"),
                         cf.get("columnNumber"))


def _free_debug_port():
    sock = socket.socket()
    try:
        sock.bind((HOST, 0))
        return sock.getsockname()[1]
    finally:
        sock.close()


def sec_s3_and_console(ctx, args):
    """S3 反复导航 + 控制台零异常门（同一浏览器会话里一起做）。"""
    with Section("S3 反复导航（DOM/JS 堆不单调增长）+ 控制台零异常"):
        browser = Browser(ctx.web, ctx.token)
        ok, why = browser.start()
        if not check("浏览器起来（Edge headless + CDP）", ok, why):
            # 启动失败也要收尾（Edge 可能已经起来了 ⇒ 否则留一堆孤儿进程
            # 占着 CDP 端口与 profile 目录）。
            browser.stop()
            return
        try:
            # 首屏就绪
            ready = browser.wait_ready(timeout=40.0, settle=1.2)
            check("首屏挂载完成（page-host 非空）", ready, "首屏没就绪")
            cand = ctx.page_candidates()
            pages = browser.implemented_pages(cand)
            ref = ctx.ref_page()
            step("页轮换集合（前端注册表里实装的页，共 %d 页；"
                 "保留槽位 16 无模块 ⇒ 自动排除）：%s" % (len(pages), pages))
            step("采样基准页（reference）：%d —— 每轮都回到它再采样，"
                 "保证各轮堆/节点数**同页可比**" % ref)

            # ---- 逐页开一遍（控制台门）：收集异常 ----
            for page in pages:
                browser.goto(page)
                browser.wait_ready(timeout=25.0, expect_page=page)
                browser.collect(page)

            noise = _split_console_noise(browser.console)
            hard = noise["hard"]
            check("控制台零异常（Runtime.exceptionThrown / console.error / "
                  "Log.error 全为 0）", not hard,
                  _console_digest(hard))
            step("已过滤的**网络噪音** %d 条（不判失败，但列出来）：%s"
                 % (len(noise["soft"]), _console_digest(noise["soft"])))

            # ---- S3：反复导航 ----
            samples = []
            for rnd in range(1, args.rounds + 1):
                page = pages[(rnd - 1) % len(pages)]
                browser.goto(page)
                browser.wait_ready(timeout=25.0, expect_page=page)
                browser.goto(ref)
                browser.wait_ready(timeout=25.0, expect_page=ref)
                sample = browser.sample()
                sample["round"] = rnd
                sample["visited"] = page
                samples.append(sample)
                browser.collect(page)
                ctx.mem_sample(rnd)
                if rnd % 5 == 0:
                    step("第 %d 轮（访问 p%d）：nodes=%d heap=%.1fMB listeners=%s"
                         % (rnd, page, sample["nodes"],
                            sample["heap"] / 1048576.0, sample["listeners"]))

            # ---- S3 判据 1：DOM 节点数不单调增长 ----
            node_seq = [s["nodes"] for s in samples]
            rising_steps = sum(1 for i in range(1, len(node_seq))
                               if node_seq[i] > node_seq[i - 1])
            base_nodes = node_seq[0]
            last_nodes = node_seq[-1]
            check("DOM 节点数不单调增长（存在不增的轮次）",
                  rising_steps < len(node_seq) - 1,
                  "上升步 %d/%d，序列=%s" % (rising_steps, len(node_seq) - 1,
                                           node_seq))
            check("DOM 节点数末轮相对首轮涨幅受限（≤%.0f%% + %d）"
                  % (int(DOM_GROWTH_RATIO * 100), DOM_GROWTH_SLACK),
                  last_nodes <= base_nodes * DOM_GROWTH_RATIO + DOM_GROWTH_SLACK,
                  "首轮=%d 末轮=%d" % (base_nodes, last_nodes))

            # ---- S3 判据 2：JS 堆三点斜率不随轮数线性增长 ----
            marks = [s for s in samples if s["round"] in (10, 20, 30)]
            if len(marks) < 3:
                step("轮数不足 30 ⇒ JS 堆三点斜率判据跳过（可用标记 %d 个）"
                     % len(marks))
            else:
                h10, h20, h30 = [m["heap"] for m in marks]
                inc1 = h20 - h10
                inc2 = h30 - h20
                check("JS 堆增量不随轮数线性增长（20→30 增量 ≤ 10→20 增量 × "
                      "%.1f + %dKB）" % (HEAP_SLOPE_FACTOR,
                                        HEAP_SLOPE_SLACK // 1024),
                      inc2 <= inc1 * HEAP_SLOPE_FACTOR + HEAP_SLOPE_SLACK,
                      "heap10=%d heap20=%d heap30=%d inc1=%+d inc2=%+d"
                      % (h10, h20, h30, inc1, inc2))

            # ---- 监听器计数（弱代理，仅报告） ----
            lis = [s["listeners"] for s in samples if s["listeners"]]
            if lis:
                first = lis[0]
                last = lis[-1]
                step("window/document 监听器数：首=%s 末=%s（弱代理："
                     "看不到 bus 内部回调）" % (first, last))
                check("原生监听器数不单调增长（弱代理判据）",
                      last.get("window", -1) <= first.get("window", -1) + 2
                      and last.get("document", -1)
                      <= first.get("document", -1) + 2,
                      "首=%s 末=%s" % (first, last))

            ctx.s3_samples = samples
        finally:
            browser.stop()


# 控制台网络噪音白名单（**实测得出**，2026-11-20 本机实测只有一条：
# 浏览器自动请求 /favicon.ico ⇒ 404 ⇒ 浏览器级 Log.entryAdded(level=error)。
# 网页端 index.html **没有** favicon 引用 ⇒ 这是「浏览器自动行为」的既有
# 噪音，与页面逻辑无关 ⇒ 只放行这一类（按 URL 精确匹配）。
# [纪律] 只放行 **Log.entryAdded 的网络类条目**；`Runtime.exceptionThrown`
# （JS 未捕获异常）与 `console.error` **一律不豁免** ⇒ 不会「一刀切过滤
# console.error」。被放行的条目**照样打印出来**（条数 + 内容），不静默丢弃。
NOISE_PATTERNS = (
    ("favicon", "favicon.ico"),
)


def _split_console_noise(items):
    """把收集到的条目分成「硬错」与「已知网络噪音」。

    [口径] 只有 **Log.entryAdded（浏览器级日志）里的网络类条目** 才可能
    算既有噪音；`Runtime.exceptionThrown`（JS 未捕获异常）与
    `console.error` **一律算硬错**（不豁免）⇒ 不会「一刀切过滤
    console.error」。
    """
    hard = []
    soft = []
    for it in items:
        low = ("%s %s %s" % (it.get("kind"), it.get("text"),
                             it.get("stack"))).lower()
        if it.get("kind") == "log.error" and \
                any(key in low for _name, key in NOISE_PATTERNS):
            soft.append(it)
        else:
            hard.append(it)
    return {"hard": hard, "soft": soft}


def _console_digest(items, limit=6):
    if not items:
        return "（无）"
    lines = []
    for it in items[:limit]:
        lines.append("p%s %s: %s @ %s" % (it.get("page"), it.get("kind"),
                                          it.get("text"),
                                          it.get("stack") or "-"))
    if len(items) > limit:
        lines.append("…共 %d 条" % len(items))
    return " | ".join(lines)


# ============================================================================
#                               场景 S4
# ============================================================================

def sec_s4(ctx, args):
    """S4：webui 进程私有内存趋势（**趋势门，不是堆级结论**）。"""
    with Section("S4 进程内存趋势（webui 私有内存；工作集口径）"):
        if not ctx.web_pid:
            # 复用别人的 webui ⇒ 进程内存里混着别的负载 ⇒ 采样没有意义
            # ⇒ 明确跳过（**不计入通过项**，避免用假 PASS 充数）。
            step("跳过 S4：本次复用的是常驻 webui，进程不属于本门 ⇒ "
                 "内存曲线里混着别人的负载，采样无意义")
            return
        if len(ctx.mem_series) < 5:
            # S3 没跑（例如 --only s4）⇒ 自己采一轮（每次采样前打一次
            # /api/status + 切一次主题，让服务端有点活干）。
            step("S3 未跑 ⇒ S4 自己采 %d 轮" % args.rounds)
            for i in range(1, args.rounds + 1):
                http_json(ctx.web, "GET", "/api/status", token=ctx.token,
                          timeout=15.0)
                http_json(ctx.web, "POST", "/api/topics",
                          {"connId": None, "topics": []}, token=ctx.token,
                          timeout=15.0)
                time.sleep(0.2)
                ctx.mem_sample(i)
        mem = ctx.mem_series
        got = [m for m in mem if m is not None]
        if len(got) < 5:
            check("内存采样可用", False, "样本=%d" % len(got))
            return
        incs = [got[i] - got[i - 1] for i in range(1, len(got))]
        step("内存序列（MB）：%s"
             % ", ".join("%.1f" % (m / 1048576.0) for m in got))
        step("每轮增量（KB）：%s"
             % ", ".join("%+.0f" % (d / 1024.0) for d in incs))
        half = max(1, len(incs) // 2)
        first_half = incs[:half]
        steady = incs[half:]                      # 后半段 = 稳态
        mean_first = sum(first_half) / float(len(first_half))
        mean_steady = sum(steady) / float(len(steady))
        growth = got[-1] - got[0]
        check("稳态轮每轮私有内存增量均值 ≤ %dKB"
              % (LEAK_TOL_BYTES // 1024),
              mean_steady <= LEAK_TOL_BYTES,
              "稳态均值=%+.0fKB/轮 总增=%+.1fMB" % (mean_steady / 1024.0,
                                                   growth / 1048576.0))
        check("稳态轮单轮最大增量 ≤ %dKB（无单轮尖峰）"
              % (LEAK_TOL_BYTES // 1024),
              max(steady) <= LEAK_TOL_BYTES,
              "稳态最大=%+.0fKB" % (max(steady) / 1024.0))
        # [为什么用「前半段均值」而不是「第 1 轮增量」做基准]
        # 第 1 轮增量常常恰好是 0（两拍之间没变化）⇒ 拿它当基准等于
        # 基准=0 ⇒ 判据退化成绝对值 ⇒ 一次几百 KB 的正常抖动就判红
        # （本门第一版就踩了：+292KB 的正常抖动被判「增量不恒定」）。
        # ⇒ 基准取前半段**均值**（更稳）+ 256KB 余量吸收工作集抖动。
        check("每轮增量恒定（稳态均值 ≤ 前半段均值 × 2 + 256KB）",
              mean_steady <= max(mean_first, 0.0) * 2 + 256 * 1024,
              "前半段均值=%+.0fKB 稳态均值=%+.0fKB 稳态最大=%+.0fKB"
              % (mean_first / 1024.0, mean_steady / 1024.0,
                 max(steady) / 1024.0))
        check("总增量不持续爬升（≤ 40MB）",
              growth <= LEAK_TOTAL_CAP_BYTES,
              "总增=%+.1fMB" % (growth / 1048576.0))
        step("诚实标注：这是**工作集口径的趋势门**，不是堆级泄漏结论"
             "（私有内存含分配器/线程栈/SQLite 缓冲 ⇒ GC 一次就能跳几百 KB）")


# ============================================================================
#                              验收上下文
# ============================================================================

class Ctx(object):
    """一端验收的全部句柄。"""

    def __init__(self, web_port, upstream_port, token):
        self.web = web_port
        self.upstream = upstream_port
        self.token = token
        self.web_pid = 0
        self.global_topics = []
        self.mem_series = []
        self.s3_samples = []

    # ---- 状态 ----
    def status(self):
        """`/api/status` 的 JSON（失败时返回空字典，但**记下原因**）。

        [为什么要记状态码] 早期版本把「HTTP 500 / 非 JSON 应答」当成
        「空文档」⇒ subscriberCount 读成 0 ⇒ **回落断言假绿**、
        上游订阅表读成空 ⇒ 退订断言假红。⇒ 这里保留原始状态码与正文，
        断言失败时能一眼看出是「服务端报错」还是「值真的不对」。
        """
        status, _h, doc = http_json(self.web, "GET", "/api/status",
                                    token=self.token, timeout=15.0)
        self.last_status_code = status
        self.last_status_doc = doc
        return doc if isinstance(doc, dict) else {}

    def status_note(self):
        """状态读取的诊断串（失败详情里用）。"""
        return "HTTP=%s doc=%s" % (getattr(self, "last_status_code", "?"),
                                   str(getattr(self, "last_status_doc",
                                               None))[:200])

    def subscriber_count(self):
        try:
            return int(self.status().get("subscriberCount") or 0)
        except (TypeError, ValueError):
            return -1

    def upstream_subs(self):
        return ((self.status().get("upstream") or {}).get("subscriptions")
                or [])

    def subscriber_detail(self):
        return self.status().get("subscribers") or []

    # ---- 页面集合 ----
    def page_candidates(self):
        """候选页号（0..count-1，含保留槽位；实装与否由浏览器侧判定）。"""
        _s, _h, doc = http_json(self.web, "GET", "/api/pages", timeout=15.0)
        doc = doc or {}
        count = int(doc.get("count") or 0)
        return list(range(count))

    def ref_page(self):
        """采样基准页（默认 0 = 主界面）。

        [为什么要固定基准页] JS 堆/DOM 节点数**只在同一页上才可比** ⇒
        每轮导航后回到同一页再采样，否则「堆涨了」可能只是「换了个更重的
        页面」。
        """
        return 0

    # ---- 内存采样 ----
    def mem_sample(self, rnd):
        if not self.web_pid:
            return None
        val = proc_mem_bytes(self.web_pid)
        self.mem_series.append(val)
        return val


# ============================================================================
#                              主流程
# ============================================================================

def sec_env(ctx, args, owned):
    """进程与探活：复用常驻 / 自起。"""
    with Section("环境与进程（探活/自起/复用）"):
        up, note = resolve_upstream(args, owned)
        ctx.upstream = up
        check("上游 %d 可连" % up, tcp_open(HOST, up) if up else False, note)
        if not up:
            return False
        web, pid, wnote = resolve_webui(up, args.web_port, owned)
        ctx.web = web
        ctx.web_pid = pid or 0
        check("网页服务器 %d 可用（hsf-webui）" % web,
              hsfweb_ping(web), wnote)
        if not web:
            return False
        # 登录（root）：**只登一次** —— 网页端的主链路会用 root 连上游，
        # 再登一次会被上游的同账并发策略瞬时拒。
        status, _h, doc = web_login(web, "root", "root")
        sess = (doc or {}).get("session") or {}
        if not check("root 登录 200（角色=2 管理员）",
                     status == 200 and sess.get("role") == 2,
                     "status=%s err=%s" % (status, (doc or {}).get("err"))):
            return False
        ctx.token = doc.get("token", "")
        ctx.global_topics = read_global_topics()
        step("全局常驻主题（永远在上游订阅表里）：%s"
             % sorted(hex(t) for t in ctx.global_topics))
        # 等上游主链路就绪（惰性连接：首个浏览器登录后 1s 才连）
        ready = poll_until(lambda: (ctx.status().get("upstream") or {})
                            .get("ready"), 30, interval=0.5,
                            label="上游链路就绪")
        check("上游链路就绪（link.ready）", bool(ready),
              "upstream=%s" % str((ctx.status().get("upstream") or {}))[:160])
        return True


def run(args, owned):
    ctx = Ctx(0, 0, "")
    if not sec_env(ctx, args, owned):
        return False
    if "s1" in args.only:
        sec_s1(ctx, args)
    if "s2" in args.only:
        sec_s2(ctx, args)
    if "s3" in args.only or "c" in args.only:
        sec_s3_and_console(ctx, args)
    if "s4" in args.only:
        sec_s4(ctx, args)
    if _failed:
        # 失败时把网页端日志尾部打出来（订阅收敛/断链的根因都在里面）。
        tail = owned.log_tail("webui")
        if tail:
            print("\n--- 网页端日志尾部（失败取证） ---")
            print(tail)
    return True


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    parser = argparse.ArgumentParser(
        description="网页端内存泄漏门 + 浏览器控制台零异常门禁")
    parser.add_argument("--server-port", type=int, default=UPSTREAM_PREFERRED,
                        help="上游端口（默认 6100；不是 C sim 就另起一个）")
    parser.add_argument("--web-port", type=int, default=0,
                        help="网页端口覆盖（缺省：环境变量 → 常驻 → 候选 → "
                             "自起空闲端口）")
    parser.add_argument("--only", default="s1,s2,s3,c,s4",
                        help="只跑哪些段（逗号分隔：s1/s2/s3/c/s4）")
    parser.add_argument("--rounds", type=int, default=30,
                        help="S3 反复导航轮数（默认 30）")
    parser.add_argument("--sse-conns", type=int, default=5,
                        help="S1 的 SSE 连接数（默认 5）")
    parser.add_argument("--sse-rounds", type=int, default=20,
                        help="S1 每条连接切主题轮数（默认 20）")
    parser.add_argument("--sse-extra", type=int, default=6,
                        help="S2 超出上限的连接数（默认 6）")
    args = parser.parse_args()
    args.only = [x.strip().lower() for x in args.only.split(",") if x.strip()]

    owned = Owned()
    t0 = time.time()
    env_ok = False
    try:
        env_ok = run(args, owned)
    except Exception as exc:                                # noqa: BLE001
        import traceback
        check("门禁执行异常", False, "%s: %s" % (type(exc).__name__, exc))
        traceback.print_exc()
    finally:
        owned.cleanup()

    print("")
    print("=" * 68)
    print("合计（用时 %.0fs）：%d 项通过 / %d 项失败"
          % (time.time() - t0, _passed, len(_failed)))
    for label, detail in _failed:
        print("  FAIL %s\n       %s" % (label, detail))
    print("=" * 68)
    if not env_ok:
        print("环境：目标端没起来（退出码 2）")
        return 2
    return 0 if not _failed else 1


if __name__ == "__main__":
    sys.exit(main())
