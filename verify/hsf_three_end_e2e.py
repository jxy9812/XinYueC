#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HSF 三端联调端到端审核（Python 服务端 + Python 桌面客户端协议面 +
网页端 API），无人值守、退出码表成败。

[起因] 2026-11-19 用户要求对 HSF 做一轮「三端联调 E2E 审核」，覆盖：
手动/自动控制全操作、数据筛选、导出、图表生成、三端一致性，并把过程中
发现的产品 bug 修掉。后追加两条口径：
  · **内存泄漏探测进 E2E**——手动/自动控制、筛选、导出、图表连续循环
    N 轮，按轮读服务端/网页进程私有内存（Windows=PSAPI PrivateUsage，
    Linux=/proc VmData），「稳态轮每轮增量」超容差即红（判据口径对齐
    client_py/tests/test_leak_gate.py 的「最后一轮稳态增量」）；
  · **整包连跑 2~3 遍全绿才算收敛**（shake out 时序 flakiness）。

[三端怎么起]
  · 服务端：自起隔离实例 ``python -m hsfsrv --sim --port <随机>
    --config <临时目录>/hsf_server_py.json``——临时目录即「程序根」，
    配置/账户/SQLite 库全落在里面（参照 server_py/tests 的隔离做法），
    **绝不碰常驻的 6200/6276/8091**；
  · 桌面协议面：进程内 QApplication(offscreen) + client_py 的
    ``LinkBridge``（真登录/真 bson/真网络线程，参照
    client_py/tests/test_hsf_regression.py 远程设置段 B 的真链路做法）；
    导出与图表两段更进一步用**真的 HistoryPage**（控件 + 桥 +
    QFileDialog 打桩）跑产品代码路径；
  · 网页端：自起 ``python -m hsfweb --server 127.0.0.1 --port <sim端口>
    --web-port <随机>``，纯标准库 http.client 打 REST/SSE/导出下载。

[覆盖面（每段若断言，见段内 check 调用）]
  A 手动控制全操作：变频器启动/在线调频/调转速/换向/停机斜坡运行位=False/
    自由停车/点动（运行中拒、停机可点可释放）/停机拒给定/故障复位；
    电动球阀开关与到位反馈；进水泵 DO6；按钮灯 DO7~9 的「手动=设备在动」
    跟随；自动模式下手动控制被拒（fail-closed 文案）；切模式全停。
  B 自动控制全操作：四模式（产水/反洗/顺洗/放水）op=5 选模式、op=1 启动
    （阀相→泵相→运行）、op=2 停止全停、op=3 急停（全停+锁存，复位后才能
    再启动）、op=4 复位；autoRunMs 累计/停止冻结/重启恢复；组合循环
    「自动」（压差触发切相 + 阶段记忆 + 复位从头）；运行保护——压差过大
    （阈值注入：把上限压到现值之下）+ 进水泵空管超时（sim 空管复现），
    各自「报警 + 停机 + 锁存 + 复位解锁」全链。
  C 数据筛选：0x0118 历史分页（时间窗/关键字+列多选/锚点翻页不重不漏）、
    0x0119 日志（类别/关键字/时间窗）、0x011A 报警（关键字）——行数与
    内容都和**直读 SQLite 的独立连接**对账。
  D 导出：桌面真 HistoryPage 的 CSV/XLSX 流式全量导出（13000+ 行不限量）、
    列序、关键字筛选口径；网页端 /api/history/export CSV/XLSX；两端同筛选
    行数一致；**导出期间自动控制往返不被阻塞**（只观测+功能阈值，不写
    严格时延断言——机器上可能有别的负载）。
  E 图表：桌面 HistoryPage 图表视图（全量取数 + 等距抽样 ≤2000 点、首尾
    必留）；网页端趋势图数据接口（SSE vfd 事件 + /api/state 快照）。
  F 三端一致性：同一操作（切模式/选自动模式/改配置）在桌面协议面与网页
    API 的回显键面与数值一致；变频器/阀快照键集两端一致。
  G 泄漏探测循环（20 轮混合负载，见模块头注）。
  H 服务端重启持久化：autoRunMs/autoMode 跨重启恢复。
  I 卡阀实例：阀超时告警 kind=5 + 报警锁存 + 复位解锁（20s 产品缺省口径）。

[跑法]
    python verify/hsf_three_end_e2e.py
（需要 PyQt5：本机系统 python 3.12 自带；也可 HSF_E2E_PYTHON 指定解释器
由外部拉起。退出码 0=全绿。脚本自起自停全部进程，临时目录用完即删。）
"""
import http.client
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HS_ROOT = os.path.join(REPO_ROOT, "HighSpeedFilter")
CLIENT_ROOT = os.path.join(HS_ROOT, "client_py")
SERVER_ROOT = os.path.join(HS_ROOT, "server_py")
WEBUI_ROOT = os.path.join(HS_ROOT, "webui_py")
for _p in (CLIENT_ROOT,):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from hsfsrv_client import bson, proto            # noqa: E402
from hsfsrv_client.xlink import XLinkClient      # noqa: E402

TOPIC_AUTO_ALARM = proto.TOPIC_AUTO_ALARM

# ---- 时序预算（秒）----
VFD_STOP_BUDGET = 14.0        # 25Hz ÷ 5Hz/s ≈ 5s，含毛刺余量
PHASE_BUDGET = 15.0           # 阀相(0.6s 行程)→泵相→运行相
VALVE_TIMEOUT_BUDGET = 40.0   # 卡阀：阀层 20s 产品缺省 + 余量
EXPORT_BUDGET = 300.0         # 13000 行桌面导出（分批 40 行）
CHART_BUDGET = 300.0          # 图表全量取数（13000/50 = 260 批）
LEAK_ROUNDS = 20              # 泄漏探测轮数
LEAK_TOL_BYTES = 3.0 * 1024 * 1024      # 稳态轮每轮私有内存增量上限
SEED_ROWS = 13000             # > 旧的 12000 上限（不限量导出的判据）
SEED_STEP_MS = 20 * 1000      # 种子行间隔（铺 ~3 天）

_G_FAILS = []
_G_CHECKS = [0]


def check(name, ok, detail=""):
    _G_CHECKS[0] += 1
    tag = "[PASS]" if ok else "[FAIL]"
    line = "%s %s" % (tag, name)
    if detail:
        line += "  (%s)" % detail
    print(line)
    if not ok:
        _G_FAILS.append(name)


def step(msg):
    print("    · %s" % msg)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_tcp(port, timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            sock = socket.create_connection(("127.0.0.1", port), 0.3)
            sock.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def wait_until(fn, timeout_s, step_s=0.25):
    """轮询直到 fn() 真值；超时返回最后一次 fn() 的值（多半是假值）。"""
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        last = fn()
        if last:
            return last
        time.sleep(step_s)
    return fn()


# ============================================================================
#                       进程管理 + 内存采样
# ============================================================================

def proc_mem_bytes(pid):
    """进程私有内存字节数：Windows=PSAPI PrivateUsage（≈VmData 口径），
    Linux=/proc/<pid>/status 的 VmData；读不到返回 None。"""
    if os.name == "nt":
        return _proc_mem_windows(pid)
    return _proc_mem_linux(pid)


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


class Proc(object):
    """受管子进程：日志落文件（管道会写满卡死）、用完即杀。"""

    def __init__(self, label, argv, cwd):
        self.label = label
        self.argv = argv
        self.cwd = cwd
        self.proc = None
        self.out_path = None

    def start(self, wait_port=None, timeout=40.0):
        self.out_path = os.path.join(tempfile.gettempdir(),
                                     "hsf_e2e_%s_%d.out"
                                     % (self.label, int(time.time())))
        out = open(self.out_path, "wb")
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        self.proc = subprocess.Popen(
            self.argv, cwd=self.cwd, stdout=out, stderr=subprocess.STDOUT,
            env=env)
        if wait_port is not None and not wait_tcp(wait_port, timeout):
            self.stop()
            raise RuntimeError("%s %ds 未监听端口 %d；日志尾：%s"
                               % (self.label, timeout, wait_port,
                                  self.tail()))
        return True

    def tail(self, n=800):
        try:
            with open(self.out_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except Exception:                       # noqa: BLE001
                self.proc.kill()
        self.proc = None


# ============================================================================
#                    纯协议客户端（告警搭子 / 备用面）
# ============================================================================

class TapClient(object):
    """raw XLinkClient：订阅 TOPIC_AUTO_ALARM 收帧 + 可发请求。

    [为什么不用桥收告警] 桥把 publish 收进 model（页面消费），原始告警帧
    的 kind/text 要按帧断言——直接用 client 的 on_publish 回调。
    """

    def __init__(self, port, user="root", password="root"):
        self.port = port
        self.user = user
        self.password = password
        self.frames = []
        self.client = None
        self.subscribe_ok = False
        #: 断链/状态迁移留痕（诊断「帧没到」是订阅丢还是链路丢）。
        self.states = []
        self.client_log = []

    def connect(self):
        c = XLinkClient("127.0.0.1", self.port, self.user, self.password,
                        log=self.client_log.append)

        def on_publish(topic, body, dtype):
            if topic == TOPIC_AUTO_ALARM and dtype == proto.DATATYPE_BSON:
                try:
                    self.frames.append(bson.decode(body))
                except ValueError:
                    pass

        def on_state(state):
            self.states.append("%s %s" % (time.strftime("%H:%M:%S"), state))
            # [重连补订] 服务端订阅登记在每条会话上 ⇒ 会话重建后旧订阅
            # 已不存在，必须重发（桥的 _subscribe_persistent 同口径）。
            if state == "ready":
                try:
                    self.resubscribe()
                except Exception:                   # noqa: BLE001
                    pass

        c.on_publish = on_publish
        c.on_state_changed = on_state
        ok, err = c.start(timeout=15.0)
        if not ok:
            raise RuntimeError("tap 登录失败（%s@%d）：%s"
                               % (self.user, self.port, err))
        self.client = c
        self.resubscribe()
        return self

    def resubscribe(self):
        """（重）订阅告警主题；返回订阅登记是否在场。

        [为什么可重入] 会话被服务端回收/断线重连后旧订阅已不在（订阅登记
        在每条会话上）⇒ 断言前先核对、缺了就补订，别把「订阅没登记」误判
        成「推送链路坏了」。"""
        if self.client is None:
            return False
        fut = self.client.call_soon(self.client.subscribe(TOPIC_AUTO_ALARM))
        ok = False
        if fut is not None:
            try:
                ok = bool(fut.result(timeout=5.0))
            except Exception:                       # noqa: BLE001
                ok = False
        self.subscribe_ok = ok
        return ok

    def subscribed(self):
        if self.client is None:
            return False
        try:
            return TOPIC_AUTO_ALARM in self.client.subscriptions
        except AttributeError:
            return self.subscribe_ok

    def request(self, cmd, doc=None, timeout=8.0):
        payload = bson.encode(doc) if doc is not None else b""

        def _once():
            fut = self.client.call_soon(self.client.request(
                cmd, payload, proto.DATATYPE_BSON, timeout))
            if fut is None:
                return None
            try:
                _code, dtype, data = fut.result(timeout=timeout + 2.0)
            except Exception:                       # noqa: BLE001
                return None
            try:
                return (bson.decode(data)
                        if dtype == proto.DATATYPE_BSON else {})
            except ValueError:
                return None

        r = _once()
        if r is None:
            # [自愈重试] 会话自愈重连窗口内的请求会短暂失败 ⇒ 等 1s 再试
            # 一次，别把「重连中」当成「服务端拒绝了」。
            time.sleep(1.0)
            r = _once()
        return r

    def alarm_frames(self, kind=None):
        if kind is None:
            return list(self.frames)
        return [f for f in self.frames if f.get("kind") == kind]

    def close(self):
        if self.client is not None:
            try:
                self.client.close()
            except Exception:                       # noqa: BLE001
                pass
            self.client = None


# ============================================================================
#                          网页端 API 客户端
# ============================================================================

class WebApi(object):
    """纯标准库 http.client 的网页端面（登录/SSE/导出下载）。"""

    def __init__(self, port):
        self.port = port
        self.token = ""

    def req(self, method, path, body=None, token=None, timeout=20.0,
            raw=False):
        conn = http.client.HTTPConnection("127.0.0.1", self.port,
                                          timeout=timeout)
        headers = {"Content-Type": "application/json",
                   "Accept": "*/*"}
        tok = token if token is not None else self.token
        if tok:
            headers["Authorization"] = "Bearer %s" % tok
        payload = None
        if body is not None:
            payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
        try:
            conn.request(method, path, body=payload, headers=headers)
            resp = conn.getresponse()
            data = resp.read()
            if raw:
                return resp.status, dict(resp.getheaders()), data
            try:
                doc = json.loads(data.decode("utf-8"))
            except (ValueError, UnicodeDecodeError):
                doc = None
            return resp.status, dict(resp.getheaders()), doc
        finally:
            conn.close()

    def login(self, user="root", password="root", attempts=5):
        """登录；瞬时拒绝（同账并发 DenyNew 等）短间隔重试。
        [为什么不是 8s] 本脚本自起隔离实例且 allow_concurrent=true，
        瞬时拒只来自「惰性连接尚未就绪」的探测竞态，短重试即可。"""
        last = (0, {}, None)
        for i in range(attempts):
            st, _h, doc = self.req("POST", "/api/login",
                                   {"user": user, "password": password},
                                   token="", timeout=25.0)
            if st == 200 and isinstance(doc, dict) and doc.get("ok"):
                self.token = str(doc.get("token") or "")
                return True, ""
            last = (st, _h, doc)
            err = str((doc or {}).get("err", "")) if isinstance(doc, dict) \
                else "http %d" % st
            if i < attempts - 1:
                time.sleep(2.0)
        return False, "%s %s" % (last[0], (last[2] or {}).get("err",
                                                              last[2]))

    def get(self, path, timeout=20.0):
        return self.req("GET", path, timeout=timeout)

    def post(self, path, body=None, timeout=20.0):
        return self.req("POST", path, body=body, timeout=timeout)

    def download(self, path, timeout=120.0):
        """导出下载（chunked 全量读回）。returns (status, bytes)。"""
        st, _h, data = self.req("GET", path, timeout=timeout, raw=True)
        return st, data

    def sse_events(self, want_seconds=8.0, topics=(proto.TOPIC_VFD_STATUS,),
                   out=None):
        """连 /api/stream，回传 (connId, events[(name, dict)])。

        [为什么先设 topics] hub.GLOBAL_TOPICS 只含温湿度/健康/自动告警；
        vfd 事件要本连接显式订阅（/api/topics 按 connId）。
        """
        conn = http.client.HTTPConnection("127.0.0.1", self.port,
                                          timeout=want_seconds + 15.0)
        headers = {"Authorization": "Bearer %s" % self.token,
                   "Accept": "text/event-stream"}
        try:
            conn.request("GET", "/api/stream", headers=headers)
            resp = conn.getresponse()
            conn_id = resp.getheader("X-Hsf-Conn-Id")
            if str(resp.status) != "200":
                return None, []
            if topics:
                self.post("/api/topics", {"connId": int(conn_id or -1),
                                          "topics": list(topics)})
            deadline = time.time() + want_seconds
            name = ""
            data_lines = []
            events = []
            resp.fp.readline()                   # 首行（空行/注释）
            while time.time() < deadline:
                line = resp.fp.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").rstrip("\r\n")
                if text == "":
                    if name:
                        try:
                            doc = json.loads("".join(data_lines))
                        except ValueError:
                            doc = {"raw": "".join(data_lines)}
                        events.append((name, doc))
                        if out is not None:
                            out.append((name, doc))
                    name, data_lines = "", []
                    continue
                if text.startswith("event:"):
                    name = text.split(":", 1)[1].strip()
                elif text.startswith("data:"):
                    data_lines.append(text.split(":", 1)[1].strip())
            return conn_id, events
        finally:
            conn.close()


# ============================================================================
#                        Qt / 桌面协议面（进程内）
# ============================================================================

APP = None


def ensure_qapp():
    global APP
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    if APP is None:
        from PyQt5.QtWidgets import QApplication
        APP = QApplication.instance() or QApplication(sys.argv[:1])
    return APP


def pump(seconds):
    """泵 Qt 事件循环（信号跨线程投递必须 processEvents 才送达）。"""
    t0 = time.time()
    while time.time() - t0 < seconds:
        APP.processEvents()
        time.sleep(0.02)


class Desktop(object):
    """桌面协议面：真 LinkBridge + request_sync 快捷封装。

    [断线自愈 + 留痕] 长跑里桌面会话可能被各种原因收掉（这正是 E2E 要
    观测的）——req 遇 None 先记一次「断链」，自动重连一次再试；断链原因
    进 drop_reasons 供报告引用。重连次数不设红：它本身是**观测点**，
    但 req 的最终失败仍是断言失败。
    """

    def __init__(self, port):
        ensure_qapp()
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        self.port = port
        self.user = "root"
        self.password = "root"
        self.drop_reasons = []
        self.heals = 0
        self.bridge.disconnected.connect(self._on_drop)

    def _on_drop(self, reason):
        try:
            self.drop_reasons.append(str(reason))
        except Exception:                           # noqa: BLE001
            pass

    def connect(self, user="root", password="root"):
        self.user = user
        self.password = password
        ok, err = self.bridge.connect_to("127.0.0.1", self.port, user,
                                         password, timeout=15.0)
        if not ok:
            raise RuntimeError("桌面桥登录失败：%s" % err)
        pump(0.5)
        return self

    def alive(self):
        c = self.bridge.client
        if c is None:
            return False
        try:
            return bool(c._alive())
        except AttributeError:
            return self.bridge.is_connected()

    def req(self, cmd, doc=None, timeout=6.0):
        r = self.bridge.request_sync(cmd, doc, timeout=timeout)
        if r is None and not self.alive():
            self.heals += 1
            try:
                self.connect(self.user, self.password)
            except Exception:                       # noqa: BLE001
                return None
            pump(0.3)
            r = self.bridge.request_sync(cmd, doc, timeout=timeout)
        return r

    def close(self):
        b = self.bridge
        try:
            if b.client is not None:
                b.client.close()
        except Exception:                           # noqa: BLE001
            pass


# ============================================================================
#                      服务端装配（隔离临时程序根）
# ============================================================================

def write_sim_config(path, flowmeter_empty=True):
    """隔离实例的种子配置：并发开（三端同时在线）、落盘 1s/行、
    压差计标定（sim 默认 2985mV → 11.94mA ≈ 0.496MPa）、sim 空管。"""
    doc = {
        "login": {"allow_concurrent": True, "max_sessions": 16},
        "storage": {"enabled": True, "sample_ms": 1000},
        "analog": {"scales": [{
            "index": 0, "name": "压差计", "unit": "MPa",
            "fullScale": 1.0, "lowMa": 4.0, "highMa": 20.0}]},
        "sim": {"flowmeter_empty": bool(flowmeter_empty)},
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=1)


def start_server(label, tmp, valve_move_s="0.6", flowmeter_empty=True):
    cfg_path = os.path.join(tmp, "cfg", "hsf_server_py.json")
    write_sim_config(cfg_path, flowmeter_empty=flowmeter_empty)
    port = free_port()
    argv = [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
            "--config", cfg_path, "--sim-valve-move", valve_move_s]
    proc = Proc(label, argv, SERVER_ROOT)
    proc.start(wait_port=port)
    return proc, port, cfg_path, tmp


def start_web(port_upstream, tmp):
    wport = free_port()
    argv = [sys.executable, "-m", "hsfweb", "--server", "127.0.0.1",
            "--port", str(port_upstream), "--web-port", str(wport),
            "--bind", "127.0.0.1", "--data-dir", tmp]
    proc = Proc("web-" + os.path.basename(tmp), argv, WEBUI_ROOT)
    proc.start(wait_port=None)
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            st, _h, doc = WebApi(wport).req("GET", "/api/ping", token="",
                                            timeout=3.0)
            if st == 200 and isinstance(doc, dict) and doc.get("ok"):
                return proc, wport
        except Exception:                           # noqa: BLE001
            pass
        time.sleep(0.3)
    proc.stop()
    raise RuntimeError("网页端 30s 未就绪；日志尾：%s" % proc.tail())


# ============================================================================
#                         历史库种子（直读直写对账）
# ============================================================================

def find_db_file(tmp):
    best = None
    for root, _dirs, files in os.walk(tmp):
        for name in files:
            if name.endswith(".db") and not name.endswith(("-wal", "-shm")):
                cand = os.path.join(root, name)
                size = os.path.getsize(cand)
                if best is None or size > best[1]:
                    best = (cand, size)
    return best[0] if best else None


def seed_history(db_path, rows=SEED_ROWS, step_ms=SEED_STEP_MS):
    """直连 SQLite 灌种子行（与服务端进程经 WAL 共存）。

    [为什么要灌行] 落盘缺省 1 行/分钟，要攒 13000 行得等 9 天——E2E 必须
    直写库。WAL 模式多进程安全；行时间铺在过去 3 天，绝不与「现在」重叠。
    返回 (种子行数, 手动行数, 自动行数, 第 1000/1199 行的时间窗)。
    """
    now = int(time.time() * 1000)
    base = now - 180 * 1000 - rows * step_ms      # 与现况行留 3 分钟间隔
    items = []
    n_manual = n_auto = 0
    for i in range(rows):
        ts = base + i * step_ms
        manual = (i % 10) < 7
        mode = "手动" if manual else "自动·产水"
        n_manual += 1 if manual else 0
        n_auto += 0 if manual else 1
        items.append((
            ts, mode,
            "正转运行" if i % 3 else "停机",
            25.0 + (i % 40) / 10.0, 12.34, 720.0,
            2.29, 0.045,
            3.6, "m³/h", round(i / 1000.0, 3), "m³",
            0.4 + (i % 25) / 100.0, "",
            26.5, 55.0, 0, 0,
            19.3 if (i % 5) == 0 else None,
        ))
    conn = sqlite3.connect(db_path, timeout=30.0, isolation_level=None)
    try:
        conn.execute("BEGIN")
        conn.executemany(
            "INSERT OR REPLACE INTO history (ts, mode, vfd_state, freq_hz,"
            " current_a, rpm, turb1, turb2, flow, flow_unit, flow_total,"
            " total_unit, press_mpa, press_note, temp, hum, di, do,"
            " water_temp) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            items)
        conn.execute("COMMIT")
        total = conn.execute("SELECT COUNT(*) FROM history").fetchone()[0]
        win_lo = items[1000][0]
        win_hi = items[1199][0]
        return total, n_manual, n_auto, (win_lo, win_hi)
    finally:
        conn.close()


def db_count(db_path, where="", params=()):
    conn = sqlite3.connect(db_path, timeout=30.0)
    try:
        cur = conn.execute("SELECT COUNT(*) FROM history" +
                           ((" WHERE " + where) if where else ""), params)
        return int(cur.fetchone()[0])
    finally:
        conn.close()


# ============================================================================
#                          共用的小工具
# ============================================================================

def envelope_ok(doc):
    """0x0105/0x0115 应答的全量键面（拒绝也回全量键——两端口径）。"""
    keys = ("mode", "auto", proto.KEY_AUTO_RUNNING, proto.KEY_AUTO_MODE,
            proto.KEY_AUTO_PHASE, proto.KEY_AUTO_FAULT)
    return isinstance(doc, dict) and all(k in doc for k in keys)


def mode_state(d):
    return d.req(proto.CMD_RUN_MODE) or {}


def ensure_manual(d, name=""):
    """把模式拉回手动（矩阵/泄漏段操作手控设备的前置）。"""
    st = mode_state(d)
    if st.get("mode") != 0:
        d.req(proto.CMD_RUN_MODE, {"mode": 0})
        wait_until(lambda: mode_state(d).get("mode") == 0, 6.0)
        if name:
            check("%s 前置：模式已回手动" % name,
                  mode_state(d).get("mode") == 0,
                  "mode=%s" % mode_state(d).get("mode"))
    return mode_state(d).get("mode") == 0


def do_mask(d):
    doc = d.req(proto.CMD_DIGITAL_SNAPSHOT, {"dev": 0}) or {}
    if not doc.get(proto.KEY_VALID):
        return None
    return int(doc.get(proto.KEY_DO) or 0)


def vfd_snap(d):
    return d.req(proto.CMD_VFD_SNAPSHOT) or {}


def valve_snap(d):
    return d.req(proto.CMD_VALVE_SNAPSHOT) or {}


def auto_cfg_set(d, modes=None, cycle=None):
    doc = {"section": proto.CFG_SECTION_AUTO}
    if modes:
        doc["modes"] = modes
    if cycle:
        doc["cycle"] = cycle
    return d.req(proto.CMD_CONFIG_SET, doc, timeout=8.0) or {}


def protect_set(d, values):
    values = dict(values)
    values["section"] = proto.CFG_SECTION_PROTECT
    return d.req(proto.CMD_CONFIG_SET, values, timeout=8.0) or {}


# ============================================================================
#                     A. 手动控制全操作（桌面协议面）
# ============================================================================

def section_manual(d, tap):
    print("== A. 手动控制全操作 ==")
    st = mode_state(d)
    check("A1 缺省手动模式 + 包络键全量",
          st.get("mode") == 0 and envelope_ok(st), "%s" % {k: st.get(k) for k in ("mode", "auto")})
    v = vfd_snap(d)
    need = ("vm", "runState", "running", "setFreqHz", "outputFreqHz",
            "ratedRpm", "ratedHz")
    check("A2 变频器快照键齐全", all(k in v for k in need),
          "缺=%s" % [k for k in need if k not in v])

    # 启动 → 运行位
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("A3 启动受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    running = wait_until(lambda: bool(vfd_snap(d).get("running")), 15.0)
    check("A4 运行位=True（爬坡后）", bool(running),
          "running=%s" % vfd_snap(d).get("running"))

    # 运行中在线调速（频率）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_SET_FREQUENCY,
                                      "hz": 30.0})
    check("A5 运行中给定 30Hz 受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    got = wait_until(lambda: abs(float(vfd_snap(d).get("setFreqHz") or 0)
                                 - 30.0) < 0.6, 8.0)
    check("A6 setFreqHz=30", bool(got),
          "setFreqHz=%s" % vfd_snap(d).get("setFreqHz"))
    near = wait_until(lambda: abs(float(vfd_snap(d).get("outputFreqHz") or 0)
                                  - 30.0) < 2.0, 14.0)
    check("A7 输出频率爬到 30Hz 附近", bool(near),
          "out=%s" % vfd_snap(d).get("outputFreqHz"))

    # 转速模式给定（720rpm ≈ 25Hz）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_SET_SPEED, "rpm": 720.0})
    check("A8 转速模式给定 720rpm 受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    got = wait_until(lambda: abs(float(vfd_snap(d).get("setFreqHz") or 0)
                                 - 25.0) < 0.8, 8.0)
    check("A9 720rpm 折算 setFreqHz≈25Hz", bool(got),
          "setFreqHz=%s" % vfd_snap(d).get("setFreqHz"))

    # 运行中点动被拒（先于停机段：点动拒文案只在运行态可验）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_JOG})
    check("A10 运行中点动被拒（文案含「点动」）",
          not (r or {}).get(proto.KEY_OK, True)
          and "点动" in str((r or {}).get("err", "")),
          "err=%s" % (r or {}).get("err"))

    # 停止：停机斜坡期运行位=False（「正在停机」口径）
    # [快照有一拍缓存延迟] 停止命令刚落地时第一帧快照可能还是命令前的
    # 旧缓存（read_fresh 100ms 内复用）⇒ 用等待式断言抓「斜坡期」特征。
    # [为什么在换向之前] 换向的过零窗口里输出本来就近 0，停机斜坡特征
    # （running=False 且 out 在半坡）抓不到 ⇒ 斜坡段必须在**稳态运行**时做。
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_STOP})
    check("A11 停止受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))

    def decel_phase():
        v = vfd_snap(d)
        return (v.get("running") is False
                and 0.5 < float(v.get("outputFreqHz") or 0) < 24.0)
    check("A12 停机斜坡期运行位=False 且输出频率还在（正在停机口径）",
          bool(wait_until(decel_phase, 4.0)),
          "running=%s out=%s" % (vfd_snap(d).get("running"),
                                 vfd_snap(d).get("outputFreqHz")))
    stopped = wait_until(
        lambda: float(vfd_snap(d).get("outputFreqHz") or 0) < 0.5,
        VFD_STOP_BUDGET)
    check("A13 减速停到位", bool(stopped),
          "out=%s" % vfd_snap(d).get("outputFreqHz"))

    # 换向（先正转跑起来 → 运行中给反转命令：减速过零再反向加速）
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("A14 再启动（正转）到位", bool(wait_until(
        lambda: float(vfd_snap(d).get("outputFreqHz") or 0) > 5.0, 15.0)),
        "out=%s" % vfd_snap(d).get("outputFreqHz"))
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START, "rev": True})
    check("A15 运行中换向受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))

    def reversed_running():
        v = vfd_snap(d)
        return (bool(v.get("reverse")) and bool(v.get("running"))
                and float(v.get("outputFreqHz") or 0) > 5.0)
    check("A16 反转运行（减速过零后反向爬到给定）",
          bool(wait_until(reversed_running, VFD_STOP_BUDGET + 14.0)),
          "reverse=%s running=%s out=%s"
          % (vfd_snap(d).get("reverse"), vfd_snap(d).get("running"),
             vfd_snap(d).get("outputFreqHz")))

    # 自由停车（瞬断口径：输出立刻归零）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_FREE_STOP})
    coast = wait_until(
        lambda: (not bool(vfd_snap(d).get("running")))
        and float(vfd_snap(d).get("outputFreqHz") or 0) < 0.5, 4.0)
    check("A17 自由停车受理且瞬断（运行位回 False + 输出归零）",
          bool(r and r.get(proto.KEY_OK)) and bool(coast),
          "ok=%s running=%s out=%s" % ((r or {}).get(proto.KEY_OK),
                                       vfd_snap(d).get("running"),
                                       vfd_snap(d).get("outputFreqHz")))

    # 故障复位（无故障 = 空操作受理）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_RESET_FAULT})
    check("A18 故障复位受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))

    # 停机下发给定被拒（先启动再调速口径）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_SET_FREQUENCY,
                                      "hz": 20.0})
    check("A19 停机状态拒给定（文案提示先启动）",
          not (r or {}).get(proto.KEY_OK, True),
          "err=%s" % (r or {}).get("err"))

    # 点动（停机可点；点动保持中靠手持心跳，这里打完立即释放。
    # [快照不含点动位] M512 口径 jogActive 恒 False——点动的可观测面是
    # 「运行位/输出频率」起与落，不是 jogActive 键。）
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_JOG})
    check("A20 停机点动受理", bool(r and r.get(proto.KEY_OK)),
          "ok=%s err=%s" % ((r or {}).get(proto.KEY_OK),
                            (r or {}).get("err")))
    jog_up = wait_until(
        lambda: bool(vfd_snap(d).get("running"))
        or float(vfd_snap(d).get("outputFreqHz") or 0) > 0.5, 4.0)
    check("A20b 点动后设备在动（运行位/输出>0）", bool(jog_up),
          "running=%s out=%s" % (vfd_snap(d).get("running"),
                                 vfd_snap(d).get("outputFreqHz")))
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_JOG_RELEASE})
    check("A21 点动释放受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    wait_until(lambda: not bool(vfd_snap(d).get("running"))
               and float(vfd_snap(d).get("outputFreqHz") or 0) < 0.5, 6.0)

    # 电动球阀：开 → 到位 → 关 → 到位
    r = d.req(proto.CMD_VALVE_CONTROL, {"index": 0, "on": True})
    check("A21 开阀受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))

    def valve_open():
        doc = valve_snap(d)
        return int(doc.get("v0_state", -1)) == int(
            proto.HsfValveState.Open)
    check("A22 阀1 开到位（反馈 openFb）", bool(wait_until(valve_open, 6.0)),
          "state=%s openFb=%s" % (valve_snap(d).get("v0_state"),
                                  valve_snap(d).get("v0_openFb")))
    vs = valve_snap(d)
    check("A23 阀快照键面（count/v0_name/反馈）",
          int(vs.get("count", 0)) >= 6 and bool(vs.get("v0_name"))
          and "v0_closeFb" in vs and "v0_openFb" in vs,
          "count=%s name=%s" % (vs.get("count"), vs.get("v0_name")))
    r = d.req(proto.CMD_VALVE_CONTROL, {"index": 0, "on": False})
    check("A24 关阀受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    check("A25 阀1 关到位", bool(wait_until(
        lambda: int(valve_snap(d).get("v0_state", -1))
        == int(proto.HsfValveState.Closed), 6.0)),
        "state=%s" % valve_snap(d).get("v0_state"))

    # 进水泵 DO6
    r = d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": True})
    check("A26 启泵受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    pumpdoc = r or {}
    check("A27 泵应答回显 v0_on=True", pumpdoc.get("v0_on") is True,
          "v0_on=%s" % pumpdoc.get("v0_on"))
    check("A28 DO6 输出=1", bool(wait_until(
        lambda: (do_mask(d) or 0) & (1 << 6), 5.0)),
        "do=0x%X" % (do_mask(d) or 0))
    d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": False})
    check("A29 停泵后 DO6=0", bool(wait_until(
        lambda: not ((do_mask(d) or 0) & (1 << 6)), 5.0)),
        "do=0x%X" % (do_mask(d) or 0))

    # 按钮灯 DO7~9：手动模式「设备在动」判定
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("A30 手动在动（搅拌泵运行）→ 启动灯 DO7=1 / 停止灯 DO8=0",
          bool(wait_until(lambda: ((do_mask(d) or 0) & (1 << 7))
                          and not ((do_mask(d) or 0) & (1 << 8)), 8.0)),
          "do=0x%X" % (do_mask(d) or 0))
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_STOP})
    check("A31 全停 → DO7=0 / DO8=1", bool(wait_until(
        lambda: (not ((do_mask(d) or 0) & (1 << 7)))
        and ((do_mask(d) or 0) & (1 << 8)), VFD_STOP_BUDGET)),
        "do=0x%X" % (do_mask(d) or 0))
    d.req(proto.CMD_VALVE_CONTROL, {"index": 1, "on": True})
    check("A32 只有阀开也亮启动灯（在动=泵∨阀）", bool(wait_until(
        lambda: (do_mask(d) or 0) & (1 << 7), 8.0)),
        "do=0x%X" % (do_mask(d) or 0))
    d.req(proto.CMD_VALVE_CONTROL, {"index": 1, "on": False})
    wait_until(lambda: int(valve_snap(d).get("v1_state", -1))
               == int(proto.HsfValveState.Closed), 6.0)

    # 自动模式下的手动控制闸（fail-closed）
    r = d.req(proto.CMD_RUN_MODE, {"mode": 1})
    check("A33 切自动受理", bool(r and r.get(proto.KEY_OK))
          and envelope_ok(r), "err=%s" % (r or {}).get("err"))
    for name, cmd, doc in (
            ("A34 自动拒变频器控制", proto.CMD_VFD_CONTROL,
             {"op": proto.OP_START}),
            ("A35 自动拒 DO 写", proto.CMD_DIGITAL_CONTROL,
             {"dev": 0, "do": 0, "on": True}),
            ("A36 自动拒阀控制", proto.CMD_VALVE_CONTROL,
             {"index": 0, "on": True}),
            ("A37 自动拒泵控制", proto.CMD_PUMP_CONTROL,
             {"index": 0, "on": True})):
        r = d.req(cmd, doc)
        check("%s（文案含「自动」）" % name,
              not (r or {}).get(proto.KEY_OK, True)
              and "自动" in str((r or {}).get("err", "")),
              "err=%s" % (r or {}).get("err"))

    # 切回手动 = 全停（既有序列安全契约）
    r = d.req(proto.CMD_RUN_MODE, {"mode": 0})
    check("A38 切回手动受理", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    all_off = wait_until(lambda: ((do_mask(d) or 0) & 0x7F) == 0
                         and not vfd_snap(d).get("running"), 10.0)
    check("A39 切模式后 DO0~6 清零、变频器停", bool(all_off),
          "do=0x%X running=%s" % (do_mask(d) or 0,
                                  vfd_snap(d).get("running")))


# ============================================================================
#                     B. 自动控制全操作（桌面协议面）
# ============================================================================

MODES = {
    "produce": {"devices": ["valve1", "inletPump", "stirPump"],
                "stirRpm": 720, "stirReverse": False},
    "backwash": {"devices": ["valve2", "stirPump"],
                 "stirRpm": 720, "stirReverse": True},
    "rinse": {"devices": ["valve4", "inletPump"],
              "stirRpm": 360, "stirReverse": False},
    "drain": {"devices": ["valve6"], "stirRpm": 0, "stirReverse": False},
}


def _wait_phase(d, phase, budget=PHASE_BUDGET):
    return wait_until(lambda: mode_state(d).get(proto.KEY_AUTO_PHASE)
                      == phase, budget)


def _wait_running_phase(d):
    return _wait_phase(d, "running")


def auto_stop_and_wait_idle(d):
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP}, timeout=10.0)
    wait_until(lambda: ((do_mask(d) or 0) & 0x7F) == 0
               and not bool(vfd_snap(d).get("running")), VFD_STOP_BUDGET)


def section_auto(d, tap):
    print("== B. 自动控制全操作 ==")
    # 切自动（B 段全程在自动模式下跑；A 段收尾是手动）
    r = d.req(proto.CMD_RUN_MODE, {"mode": 1})
    check("B0 切自动受理", bool(r and r.get(proto.KEY_OK))
          and envelope_ok(r), "err=%s" % (r or {}).get("err"))
    # 配置四模式 + 回显
    r = auto_cfg_set(d, modes=MODES)
    check("B1 四模式配置 SET 受理", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    got = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
           .get(proto.CFG_SECTION_AUTO, {}).get("modes", {}))
    ok_cfg = all(sorted((got.get(mid) or {}).get("devices") or [])
                 == sorted(cfg["devices"])
                 and (got.get(mid) or {}).get("stirRpm") == cfg["stirRpm"]
                 for mid, cfg in MODES.items())
    check("B2 GET 回显四模式 devices/rpm 一致", ok_cfg,
          "produce=%s drain=%s" % (got.get("produce"), got.get("drain")))

    # op=5 只选模式：记粘性、不启动
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_SELECT, proto.KEY_AUTO_MODE: "produce"},
              timeout=8.0)
    st = mode_state(d)
    check("B3 op=5 选模式受理：autoMode=produce 且不启动",
          bool(r.get(proto.KEY_OK)) and st.get(proto.KEY_AUTO_MODE)
          == "produce" and st.get(proto.KEY_AUTO_RUNNING) is False,
          "autoMode=%s running=%s" % (st.get(proto.KEY_AUTO_MODE),
                                      st.get(proto.KEY_AUTO_RUNNING)))

    # op=1 启动产水：阀相 → 泵相 → 运行
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "produce"},
              timeout=10.0)
    check("B4 启动受理进阀相", bool(r.get(proto.KEY_OK))
          and r.get(proto.KEY_AUTO_PHASE) == "valves", "应答=%s" % r)
    check("B5 到运行相", bool(_wait_running_phase(d)),
          "phase=%s" % mode_state(d).get(proto.KEY_AUTO_PHASE))
    m = do_mask(d)
    v = vfd_snap(d)
    check("B6 阀1 DO0=1、进水泵 DO6=1",
          m is not None and (m & 0x01) and (m & (1 << 6)),
          "do=0x%X" % (m or 0))
    check("B7 搅拌泵按配置运行（正转 720rpm≈25Hz）",
          bool(v.get("running")) and not bool(v.get("reverse"))
          and abs(float(v.get("setFreqHz") or 0) - 25.0) < 0.8,
          "running=%s reverse=%s set=%s" % (v.get("running"),
                                            v.get("reverse"),
                                            v.get("setFreqHz")))

    # autoRunMs 累计
    def run_ms():
        v = mode_state(d).get(proto.KEY_AUTO_RUN_MS)
        return int(v) if v is not None else -1
    t1 = run_ms()
    time.sleep(2.2)
    t2 = run_ms()
    check("B8 autoRunMs 运行中递增", t1 >= 0 and t2 > t1,
          "t1=%s t2=%s" % (t1, t2))

    # op=2 停止全停 + 粘性 + 冻结
    r = d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP},
              timeout=10.0)
    check("B9 停止受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
    check("B10 停止后全停（DO0~6 清零 + 变频器停）", bool(wait_until(
        lambda: ((do_mask(d) or 0) & 0x7F) == 0
        and not bool(vfd_snap(d).get("running")), VFD_STOP_BUDGET)),
        "do=0x%X" % (do_mask(d) or 0))
    st = mode_state(d)
    check("B11 模式粘性保留 produce、autoRunning=false",
          st.get(proto.KEY_AUTO_MODE) == "produce"
          and st.get(proto.KEY_AUTO_RUNNING) is False,
          "autoMode=%s running=%s" % (st.get(proto.KEY_AUTO_MODE),
                                      st.get(proto.KEY_AUTO_RUNNING)))
    t3 = run_ms()
    time.sleep(1.2)
    t4 = run_ms()
    check("B12 autoRunMs 停止后冻结", t3 == t4 and t3 > 0,
          "t3=%s t4=%s" % (t3, t4))

    # 再启动 = 累计（多次运行叠加）
    d.req(proto.CMD_AUTO_CONTROL,
          {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "produce"},
          timeout=10.0)
    _wait_running_phase(d)
    t5 = run_ms()
    check("B13 再次运行 autoRunMs 在冻结值之上叠加", t5 > t4,
          "t4=%s t5=%s" % (t4, t5))
    auto_stop_and_wait_idle(d)

    # 反洗 / 顺洗 / 放水 逐模式跑一遍
    for mid, dob, name in (("backwash", 1 << 1, "阀2"),
                           ("rinse", 1 << 3, "阀4"),
                           ("drain", 1 << 5, "阀6")):
        r = d.req(proto.CMD_AUTO_CONTROL,
                  {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: mid},
                  timeout=10.0)
        ok_start = bool(r.get(proto.KEY_OK))
        check("B14 %s 启动受理" % name, ok_start, "err=%s" % r.get("err"))
        check("B15 %s 到运行相" % name, bool(_wait_running_phase(d)),
              "phase=%s" % mode_state(d).get(proto.KEY_AUTO_PHASE))
        mm = do_mask(d)
        check("B16 %s 输出（DO 位）=1" % name,
              mm is not None and (mm & dob), "do=0x%X" % (mm or 0))
        auto_stop_and_wait_idle(d)

    # 急停：全停 + 锁存 + 复位后才能再启动
    d.req(proto.CMD_AUTO_CONTROL,
          {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "produce"},
          timeout=10.0)
    _wait_running_phase(d)
    r = d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_ESTOP},
              timeout=10.0)
    st = mode_state(d)
    check("B17 急停受理：phase=estop、autoRunning=false",
          bool(r.get(proto.KEY_OK))
          and st.get(proto.KEY_AUTO_PHASE) == "estop"
          and st.get(proto.KEY_AUTO_RUNNING) is False,
          "phase=%s running=%s" % (st.get(proto.KEY_AUTO_PHASE),
                                   st.get(proto.KEY_AUTO_RUNNING)))
    check("B18 急停后泵停（DO6=0）", bool(wait_until(
        lambda: not ((do_mask(d) or 0) & (1 << 6)), 6.0)),
        "do=0x%X" % (do_mask(d) or 0))
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "produce"},
              timeout=10.0)
    check("B19 急停锁存拒启动（文案含「复位」）",
          not r.get(proto.KEY_OK) and "复位" in str(r.get("err", "")),
          "err=%s" % r.get("err"))
    r = d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_RESET},
              timeout=10.0)
    st = mode_state(d)
    check("B20 复位受理且退出急停态",
          bool(r.get(proto.KEY_OK))
          and st.get(proto.KEY_AUTO_PHASE) != "estop"
          and st.get(proto.KEY_AUTO_FAULT) is False, "应答=%s" % r)
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "produce"},
              timeout=10.0)
    check("B21 复位后可再启动", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    auto_stop_and_wait_idle(d)

    # 组合循环「自动」：压差触发 + 阶段记忆 + 复位从头
    cyc = {"enabled": True, "pressThreshold": 0.05,
           proto.CFG_AUTO_CYCLE_PRESS_COMPARE: "gt",
           "timerFallback": False, "timerMinutes": 30,
           "backwashMinutes": 30, "rinseMinutes": 30,
           proto.CFG_AUTO_CYCLE_MODES: ["produce", "backwash", "rinse"]}
    r = auto_cfg_set(d, cycle=cyc)
    check("B22 循环配置 SET 受理", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    gotc = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
            .get(proto.CFG_SECTION_AUTO, {}).get("cycle", {}))
    check("B23 循环配置 GET 回显（阈值/方向/参与列表）",
          gotc.get("pressThreshold") == 0.05
          and gotc.get(proto.CFG_AUTO_CYCLE_PRESS_COMPARE) == "gt"
          and gotc.get(proto.CFG_AUTO_CYCLE_MODES)
          == ["produce", "backwash", "rinse"],
          "cycle=%s" % {k: gotc.get(k) for k in
                        ("pressThreshold", "pressCompare", "cycleModes")})

    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "auto"},
              timeout=10.0)
    check("B24 组合模式从参与列表第一个（produce）开始",
          bool(r.get(proto.KEY_OK))
          and r.get(proto.KEY_AUTO_MODE) == "produce", "应答=%s" % r)

    def stage_is(mid):
        stq = mode_state(d)
        return (stq.get(proto.KEY_AUTO_MODE) == mid
                and stq.get(proto.KEY_AUTO_RUNNING) is True)
    check("B25 压差达标切到反洗（运行中切相）",
          bool(wait_until(lambda: stage_is("backwash"), 15.0)),
          "autoMode=%s" % mode_state(d).get(proto.KEY_AUTO_MODE))
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP}, timeout=10.0)
    wait_until(lambda: mode_state(d).get(proto.KEY_AUTO_RUNNING) is False,
               8.0)
    st = mode_state(d)
    check("B26 停止后记住阶段（autoMode=backwash）",
          st.get(proto.KEY_AUTO_MODE) == "backwash",
          "autoMode=%s" % st.get(proto.KEY_AUTO_MODE))
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "auto"},
              timeout=10.0)
    check("B27 再启动从记住的阶段继续（backwash）",
          bool(r.get(proto.KEY_OK))
          and r.get(proto.KEY_AUTO_MODE) == "backwash", "应答=%s" % r)
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP}, timeout=10.0)
    wait_until(lambda: mode_state(d).get(proto.KEY_AUTO_RUNNING) is False,
               8.0)
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_RESET}, timeout=10.0)
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "auto"},
              timeout=10.0)
    check("B28 复位后从头（produce）",
          bool(r.get(proto.KEY_OK))
          and r.get(proto.KEY_AUTO_MODE) == "produce", "应答=%s" % r)
    auto_stop_and_wait_idle(d)
    auto_cfg_set(d, cycle={"enabled": False})

    # ---- 运行保护①：压差过大 ----
    # [为什么切手动] 保护闸的「锁存拒启动」要在**手动模式**验（自动模式
    # 的远程控制本来就全被闸，文案混在一起分不清是哪道闸拦的）。
    ensure_manual(d, "B-保护①")
    r = protect_set(d, {"pressHighEnabled": True,
                        proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA: 0.001,
                        "pressHighStop": True})
    check("B29 压差保护配置受理", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    gotp = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}).get(
        proto.CFG_SECTION_PROTECT, {})
    check("B30 压差保护 GET 回显",
          gotp.get("pressHighEnabled") is True
          and float(gotp.get(proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA) or 0)
          == 0.001 and gotp.get("pressHighStop") is True, "protect=%s" % gotp)
    del tap.frames[:]
    if not tap.subscribed():
        tap.resubscribe()
    check("B30b 告警主题订阅在场（tap 会话健康）", tap.subscribed(),
          "订阅=%s 断链原因=%s" % (tap.subscribed(), d.drop_reasons[-2:]))
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("B31 运行中压差超限 → kind=8 告警帧（2s 去抖）", bool(wait_until(
        lambda: bool(tap.alarm_frames(proto.AUTO_ALARM_PRESS_HIGH)), 12.0)),
        "帧=%s 订阅=%s alive=%s 状态=%s 日志=%s"
        % (tap.alarm_frames()[:2], tap.subscribed(),
           (tap.client._alive() if tap.client else False),
           tap.states[-4:], tap.client_log[-6:]))
    check("B32 保护动作停设备（变频器已停）", bool(wait_until(
        lambda: not bool(vfd_snap(d).get("running")), 10.0)),
        "running=%s" % vfd_snap(d).get("running"))
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("B33 锁存拒启动（文案含「复位」）",
          not (r or {}).get(proto.KEY_OK, True)
          and "复位" in str((r or {}).get("err", "")),
          "err=%s" % (r or {}).get("err"))
    st, _h, doc = None, None, None
    r = d.req(proto.CMD_ALARM_PAGE, {"offset": 0, "limit": 20,
                                     "kw": "压差"}, timeout=8.0) or {}
    rows = r.get("rows") or []
    check("B34 报警历史表含压差过高记录（kind/source 对）",
          r.get(proto.KEY_OK) and len(rows) >= 1
          and "压差过高" in str(rows[0].get("text", ""))
          and rows[0].get("kind") in ("analog", "auto"),
          "rows=%s" % rows[:1])
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_RESET}, timeout=10.0)
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
    check("B35 复位后可再启动", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_STOP})
    protect_set(d, {"pressHighEnabled": False})
    wait_until(lambda: float(vfd_snap(d).get("outputFreqHz") or 0) < 0.5,
               VFD_STOP_BUDGET)

    # ---- 运行保护②：进水泵空管超时 ----
    ensure_manual(d, "B-保护②")
    r = protect_set(d, {"inletNoFlowTimeoutMs": 1500,
                        "inletNoFlowStop": True})
    check("B36 空管保护配置受理", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    del tap.frames[:]
    if not tap.subscribed():
        tap.resubscribe()
    d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": True})
    check("B37 泵开 + 空管持续 → kind=6 告警帧", bool(wait_until(
        lambda: bool(tap.alarm_frames(proto.AUTO_ALARM_FLOWMETER)), 12.0)),
        "帧=%s 订阅=%s" % (tap.alarm_frames()[:2], tap.subscribed()))
    check("B38 保护停泵（DO6=0）", bool(wait_until(
        lambda: not ((do_mask(d) or 0) & (1 << 6)), 8.0)),
        "do=0x%X" % (do_mask(d) or 0))
    r = d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": True})
    check("B39 锁存拒启泵（文案含「复位」）",
          not (r or {}).get(proto.KEY_OK, True)
          and "复位" in str((r or {}).get("err", "")),
          "err=%s" % (r or {}).get("err"))
    r = d.req(proto.CMD_ALARM_PAGE, {"offset": 0, "limit": 20,
                                     "kw": "空管"}, timeout=8.0) or {}
    check("B40 报警历史表含空管记录（source=流量计）",
          r.get(proto.KEY_OK) and len(r.get("rows") or []) >= 1
          and str((r.get("rows") or [{}])[0].get("source", ""))
          .find("流量") >= 0, "rows=%s" % (r.get("rows") or [])[:1])
    d.req(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_RESET}, timeout=10.0)
    r = d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": True})
    check("B41 复位后可再启泵", bool(r and r.get(proto.KEY_OK)),
          "err=%s" % (r or {}).get("err"))
    d.req(proto.CMD_PUMP_CONTROL, {"index": 0, "on": False})
    protect_set(d, {"inletNoFlowTimeoutMs": 0})


# ============================================================================
#                     C. 数据筛选（0x0118/0x0119/0x011A）
# ============================================================================

def section_data(d, db_path, win, n_auto, db_total):
    print("== C. 数据筛选 ==")
    # [手动行数现算] 现况行（sim 落盘）模式也是「手动」，种子数要加上
    # 它们才与服务端筛选总数同口径（采样已被 Z5 停住，现算即稳定值）。
    n_manual = db_count(db_path, "mode = ?", ("手动",))
    lo, hi = win
    base = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 1},
                 timeout=10.0) or {}
    total_all = int(base.get("total") or 0)
    check("C1 历史总数 = 库总数（协议与库对账）", total_all == db_total,
          "total=%s db=%d" % (total_all, db_total))
    check("C2 页行数 = limit（1 行页）", len(base.get("rows") or []) == 1,
          "rows=%d" % len(base.get("rows") or []))

    # 时间窗
    r = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 50,
                                       "loTs": lo, "hiTs": hi},
              timeout=10.0) or {}
    rows = r.get("rows") or []
    ts_in = all(lo <= int(row[0]) <= hi for row in rows if row)
    check("C3 时间窗命中 200 行（total 与页内容同源）",
          int(r.get("total") or 0) == 200 and len(rows) == 50 and ts_in,
          "total=%s rows=%d" % (r.get("total"), len(rows)))
    stamps = [int(row[0]) for row in rows]
    check("C4 页内时间正序", stamps == sorted(stamps), "")

    # 锚点翻页不重不漏
    anchor = int(time.time() * 1000)
    p1 = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 50,
                                        "beforeTs": anchor},
               timeout=10.0) or {}
    p2 = d.req(proto.CMD_HISTORY_PAGE, {"offset": 50, "limit": 50,
                                        "beforeTs": anchor},
               timeout=10.0) or {}
    s1 = [int(row[0]) for row in (p1.get("rows") or [])]
    s2 = [int(row[0]) for row in (p2.get("rows") or [])]
    check("C5 锚点翻页：50+50 行、无重复",
          len(s1) == 50 and len(s2) == 50 and not (set(s1) & set(s2)), "")
    check("C6 锚点翻页顺序：页1 全新于页2", min(s1) > max(s2), "")

    # 关键字 + 列多选
    r = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 50,
                                       "kw": "手动", "kwCols": [1]},
              timeout=15.0) or {}
    rows = r.get("rows") or []
    col_ok = all(row[1] == "手动" for row in rows)
    check("C7 kw=手动 且限定模式列：total=种子手动行数",
          int(r.get("total") or 0) == n_manual and col_ok,
          "total=%s want=%s" % (r.get("total"), n_manual))
    r = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 50,
                                       "kw": "自动·产水"},
              timeout=15.0) or {}
    check("C8 kw=自动·产水（全列）命中自动行数",
          int(r.get("total") or 0) == n_auto,
          "total=%s want=%s" % (r.get("total"), n_auto))
    r = d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 20,
                                       "kw": "不存在关键xyz"},
              timeout=15.0) or {}
    check("C9 无命中关键字 total=0 且行空",
          int(r.get("total") or 0) == 0 and not (r.get("rows") or []), "")

    # 列名表（线上 19 列）
    cols = base.get("cols") or []
    # [列序跟随 2026-11-24] 历史线上列 19→22（water_temp/motor_temp/
    # press1_mpa/press2_mpa 追加在末尾）；直接与客户端 proto 的线上列逐值
    # 比，避免下次加列又写死个数。
    check("C10 cols = 服务端线上列序（与客户端 proto 一致）",
          cols == list(proto.HISTORY_COLUMNS) and len(cols) == 22,
          "cols=%s len=%d" % (cols[:3], len(cols)))

    # 0x0119 日志分页
    r = d.req(proto.CMD_LOG_PAGE, {"offset": 0, "limit": 50,
                                   "category": "ctrl"}, timeout=10.0) or {}
    lrows = r.get("rows") or []
    check("C11 日志按类别 ctrl：行数>0 且类别全 ctrl",
          r.get(proto.KEY_OK) and len(lrows) > 0
          and all(row.get("category") == "ctrl" for row in lrows),
          "total=%s" % r.get("total"))
    r = d.req(proto.CMD_LOG_PAGE, {"offset": 0, "limit": 50,
                                   "kw": "变频器"}, timeout=10.0) or {}
    check("C12 日志关键字命中（变频器控制）",
          int(r.get("total") or 0) >= 1, "total=%s" % r.get("total"))
    r = d.req(proto.CMD_LOG_PAGE, {"offset": 0, "limit": 50,
                                   "loTs": 1, "hiTs": 2},
              timeout=10.0) or {}
    check("C13 日志时间窗外 total=0",
          int(r.get("total") or 0) == 0, "total=%s" % r.get("total"))

    # 0x011A 报警分页
    r = d.req(proto.CMD_ALARM_PAGE, {"offset": 0, "limit": 50},
              timeout=10.0) or {}
    arows = r.get("rows") or []
    keys_ok = all(("ts" in row and "kind" in row and "text" in row)
                  for row in arows)
    check("C14 报警历史行键面（ts/kind/text）且行数>0",
          r.get(proto.KEY_OK) and len(arows) > 0 and keys_ok,
          "total=%s" % r.get("total"))
    r = d.req(proto.CMD_ALARM_PAGE, {"offset": 0, "limit": 50,
                                     "kw": "空管"}, timeout=10.0) or {}
    check("C15 报警关键字筛选（空管）命中且内容含空管",
          int(r.get("total") or 0) >= 1
          and "空管" in str((r.get("rows") or [{}])[0].get("text", "")),
          "total=%s" % r.get("total"))


# ============================================================================
#              D. 导出（桌面真 HistoryPage + 网页端 API）
# ============================================================================

def _read_csv_rows(path):
    import csv as _csv
    with open(path, "r", encoding="utf-8-sig", newline="") as fh:
        rows = list(_csv.reader(fh))
    return rows[0], rows[1:]


def _xlsx_data_rows(path):
    import re
    import zipfile
    with zipfile.ZipFile(path) as zf:
        bad = zf.testzip()
        if bad is not None:
            raise RuntimeError("zip 损坏：%s" % bad)
        xml = zf.read("xl/worksheets/sheet1.xml").decode("utf-8",
                                                         "replace")
    return len(re.findall(r"<row[ >]", xml)) - 1     # 减表头行


def _wait_export_done(page, status_sink, timeout=EXPORT_BUDGET):
    """等导出状态机跑完（runner.running 回落）；最近状态文本进 status_sink。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        pump(0.15)
        if status_sink:
            status_sink[:] = status_sink[-1:]
        runner = getattr(page, "_export_runner", None)
        if runner is not None and not getattr(runner, "running", False):
            pump(0.3)
            return True
    return False


def section_export_desktop(page, db_path, status_sink):
    print("== D1. 桌面导出（真 HistoryPage 全量流式）==")
    from PyQt5.QtWidgets import QFileDialog
    out_dir = tempfile.mkdtemp(prefix="hsf_e2e_export_")
    n_manual = db_count(db_path, "mode = ?", ("手动",))
    try:
        # CSV：kw=手动
        page._f_text.setText("手动")
        csv_path = os.path.join(out_dir, "h.csv")
        orig = QFileDialog.getSaveFileName
        QFileDialog.getSaveFileName = staticmethod(
            lambda *a, **k: (csv_path, "CSV"))
        try:
            page._export_data()
        finally:
            QFileDialog.getSaveFileName = orig
        done = _wait_export_done(page, status_sink, EXPORT_BUDGET)
        check("D1a CSV 导出跑完（状态行终态）", done,
              "status=%s" % (status_sink[-1] if status_sink else ""))
        if os.path.isfile(csv_path):
            headers, rows = _read_csv_rows(csv_path)
            want_headers = [page.COLUMNS[c] for c in page.visible_columns()]
            check("D1b CSV 表头 = 可见列序（%d 列）" % len(page.COLUMNS),
                  headers == want_headers
                  and len(headers) == len(page.COLUMNS),
                  "got=%s" % headers[:4])
            check("D1c CSV 行数 = 筛选命中（kw=手动，与库对账）",
                  len(rows) == n_manual,
                  "rows=%d want=%d" % (len(rows), n_manual))
            check("D1d CSV 筛选内容一致（模式列全=手动）",
                  all(row[1] == "手动" for row in rows), "")
        else:
            check("D1b CSV 产物生成", False, csv_path)

        # XLSX：无筛选全量（>12000 行，不限量口径）
        page._f_text.setText("")
        xlsx_path = os.path.join(out_dir, "h.xlsx")
        orig = QFileDialog.getSaveFileName
        QFileDialog.getSaveFileName = staticmethod(
            lambda *a, **k: (xlsx_path, "Excel"))
        try:
            page._export_data()
        finally:
            QFileDialog.getSaveFileName = orig
        done = _wait_export_done(page, status_sink, EXPORT_BUDGET)
        check("D1e XLSX 导出跑完", done,
              "status=%s" % (status_sink[-1] if status_sink else ""))
        if os.path.isfile(xlsx_path):
            try:
                nrows = _xlsx_data_rows(xlsx_path)
                db_all = db_count(db_path)
                check("D1f XLSX 不限量：数据行数 = 全库行数（>12000）",
                      nrows == db_all and nrows > 12000,
                      "xlsx=%d db=%d" % (nrows, db_all))
            except Exception as exc:                # noqa: BLE001
                check("D1f XLSX 产物合法 zip", False, "%s: %s"
                      % (type(exc).__name__, exc))
        else:
            check("D1f XLSX 产物生成", False, xlsx_path)
    finally:
        shutil.rmtree(out_dir, ignore_errors=True)


# ---------------------------------------------------------------------------
def section_export_web(web, db_path, n_manual, d_bridge):
    print("== D2. 网页端导出 + 导出期间服务端不阻塞 ==")
    kw_q = urllib.parse.quote("手动")
    st, data = web.download(
        "/api/history/export?format=csv&kw=%s&kwCols=1" % kw_q)
    check("D2a 网页 CSV 导出 200", st == 200 and bool(data),
          "status=%s size=%d" % (st, len(data or b"")))
    text = (data or b"").decode("utf-8-sig", "replace")
    lines = [ln for ln in text.splitlines() if ln]
    body = [ln for ln in lines[1:] if not ln.startswith("#")]
    check("D2b 网页 CSV 行数 = 同筛选命中（与库对账）",
          len(body) == n_manual,
          "rows=%d want=%d" % (len(body), n_manual))

    db_all = db_count(db_path)
    lat = []
    box = {}

    def _download():
        try:
            box["st"], box["data"] = web.download(
                "/api/history/export?format=xlsx", timeout=180.0)
        except Exception as exc:                    # noqa: BLE001
            box["err"] = "%s: %s" % (type(exc).__name__, exc)

    th = threading.Thread(target=_download, name="e2e-web-export")
    th.start()
    while th.is_alive():
        t0 = time.time()
        d_bridge.req(proto.CMD_VFD_SNAPSHOT, timeout=4.0)
        lat.append((time.time() - t0) * 1000.0)
        time.sleep(0.05)
    th.join()
    check("D2c 网页 XLSX 导出 200", box.get("st") == 200,
          "st=%s err=%s" % (box.get("st"), box.get("err")))
    if box.get("data"):
        tmpf = os.path.join(tempfile.gettempdir(), "hsf_e2e_web_export.xlsx")
        with open(tmpf, "wb") as fh:
            fh.write(box["data"])
        try:
            nrows = _xlsx_data_rows(tmpf)
            check("D2d 网页 XLSX 行数 = 全库行数（>12000）",
                  nrows == db_all, "xlsx=%d db=%d" % (nrows, db_all))
        except Exception as exc:                    # noqa: BLE001
            check("D2d 网页 XLSX 合法", False, str(exc))
    if lat:
        lat.sort()
        p95 = lat[min(len(lat) - 1, int(len(lat) * 0.95))]
        check("D2e 导出期间自动面往返不阻塞（max<5s 功能阈值）",
              max(lat) < 5000.0,
              "n=%d 中位=%.1fms p95=%.1fms max=%.1fms（只观测不设严格门）"
              % (len(lat), lat[len(lat) // 2], p95, max(lat)))
        check("D2f 导出期间有采样点（观测有效性）", len(lat) >= 5,
              "n=%d" % len(lat))

    # 两端同筛选一致性（历史分页 total 同口径）
    st, _h, doc = web.get("/api/history/page?limit=1&kw=%s&kwCols=1"
                          % kw_q)
    wtotal = int(((doc or {}).get("total")) or -1)
    dtotal = int((d_bridge.req(proto.CMD_HISTORY_PAGE,
                               {"offset": 0, "limit": 1, "kw": "手动",
                                "kwCols": [1]}, timeout=10.0) or {})
                 .get("total") or -1)
    check("D2g 同筛选 total 三端一致（网页=桌面=库）",
          wtotal == dtotal == n_manual,
          "web=%s desktop=%s db=%d" % (wtotal, dtotal, n_manual))


# ============================================================================
#                     E. 图表（桌面全量+抽样；网页趋势接口）
# ============================================================================

def section_chart_desktop(page):
    print("== E1. 桌面图表（全量取数 + 等距抽样）==")
    page._f_text.setText("")
    page.set_view(1)
    t0 = time.time()
    while time.time() - t0 < CHART_BUDGET:
        pump(0.2)
        if not page._chart_fetching:
            break
    check("E1a 图表全量取数收尾", not page._chart_fetching,
          "status=%s" % page._chart_status.text())
    # [全量取数是产品口径 2026-11-24] 图表取数不再有 12000 截断：命中多少
    # 扫回多少（_chart_scanned=全部扫描行；_chart_rows 是抽样后的绘图行）。
    check("E1b 图表全量取数（命中>12000 ⇒ 全部扫回，不再 12000 截断）",
          page._total > 12000 and page._chart_scanned == page._total,
          "scanned=%s total=%s" % (page._chart_scanned, page._total))
    dlg = page._chart_dialog
    series = list(getattr(dlg, "_series", []) if dlg else [])
    check("E1c 折线序列已建（≥1 条）", bool(series),
          "n=%d" % len(series))
    if series:
        name, points, _color = series[0]
        check("E1d 每线 ≤2000 点（等距抽样）", len(points) <= 2000,
              "%s: %d 点" % (name, len(points)))
        col = page.COLUMNS.index(name) if name in page.COLUMNS else 0
        stamps = []
        for row, _abn in page._chart_rows:
            if page._cell_float(row[col]) is None:
                continue
            ms = page._stamp_text_to_ms(row[0])
            if ms is not None:
                stamps.append(ms)
        if stamps and points:
            check("E1e 首尾必留（抽样端点=数据两端）",
                  points[0][0] == min(stamps) and points[-1][0] == max(stamps),
                  "head=%s want=%s tail=%s want=%s"
                  % (points[0][0], min(stamps), points[-1][0], max(stamps)))
    # [server 模式状态行] 「筛选共 N 行 · 已取 N 行 · 抽样 M 行绘图」：
    # 注明全量取数与抽样口径（"已等距抽样" 字样只在离线分支的文本里）。
    check("E1f 状态行注明全量取数+抽样口径",
          "已取" in page._chart_status.text()
          and "抽样" in page._chart_status.text()
          and str(page._total) in page._chart_status.text(),
          "text=%s" % page._chart_status.text())
    page.set_view(0)


# ---------------------------------------------------------------------------
def section_web_trend(web):
    print("== E2. 网页端趋势数据接口 ==")
    st, _h, doc = web.get("/api/state")
    snap_vfd = (doc or {}).get("vfd") or {}
    check("E2a /api/state 快照含 vfd 数据（趋势初值源）",
          st == 200 and "setFreqHz" in snap_vfd
          and "outputFreqHz" in snap_vfd,
          "keys=%s" % sorted(snap_vfd.keys())[:8])
    events = []

    def _collect():
        try:
            web.sse_events(want_seconds=8.0, out=events)
        except Exception:                           # noqa: BLE001
            pass

    th = threading.Thread(target=_collect, name="e2e-sse")
    th.start()
    th.join(timeout=20.0)
    names = [n for n, _d in events]
    check("E2b SSE 收到事件流（含首帧 snapshot）",
          bool(events) and "snapshot" in names,
          "事件=%s" % names[:6])
    vfd_ev = [d for n, d in events if n == "vfd"]
    check("E2c SSE vfd 事件（趋势样本：输出/给定频率）",
          bool(vfd_ev) and "outputFreqHz" in vfd_ev[0]
          and "setFreqHz" in vfd_ev[0],
          "n=%d sample=%s" % (len(vfd_ev), sorted(vfd_ev[0].keys())[:6])
          if vfd_ev else "n=0")


# ============================================================================
#                     F. 三端一致性
# ============================================================================

def section_consistency(d, web):
    print("== F. 三端一致性 ==")
    # 模式键面/数值一致
    # [键名差异是传输面口径] 桌面 0x0105 应答的键是 mode；网页 JSON 是
    # runMode（web 前端 page00 即按 runMode 消费）——语义同一数值，比对
    # 时做映射，不要求字面相同。
    st, _h, wmode = web.get("/api/mode")
    dmode = mode_state(d)
    shared = ("auto", proto.KEY_AUTO_RUNNING, proto.KEY_AUTO_MODE,
              proto.KEY_AUTO_PHASE, proto.KEY_AUTO_FAULT,
              proto.KEY_AUTO_RUN_MS)
    _wm = (wmode or {}).get("runMode")
    _dm = dmode.get("mode")
    mode_ok = (st == 200 and _wm is not None and _dm is not None
               and int(_wm) == int(_dm))
    vals_ok = all((wmode or {}).get(k) == dmode.get(k)
                  for k in shared if (wmode or {}).get(k) is not None)
    check("F1 模式状态两端同值（runMode↔mode + auto 族）",
          mode_ok and vals_ok,
          "web=%s desktop=%s" % ({k: (wmode or {}).get(k) for k in shared},
                                 {k: dmode.get(k) for k in shared}))

    # 网页切模式 → 桌面回显
    st, _h, doc = web.post("/api/mode", {"mode": 1})
    check("F2 网页切自动受理且回全量状态键",
          st == 200 and (doc or {}).get("ok") is True
          and all(k in (doc or {}) for k in ("mode", "auto",
                                             proto.KEY_AUTO_RUNNING)),
          "应答=%s" % (doc or {}).get("err"))
    check("F3 桌面查询看到 auto=1", mode_state(d).get("auto") == 1
          or mode_state(d).get("mode") == 1,
          "auto=%s" % mode_state(d).get("auto"))

    # 网页 op=5 选模式 → 桌面粘性一致
    st, _h, doc = web.post("/api/auto/control",
                           {"op": proto.AUTO_OP_SELECT,
                            proto.KEY_AUTO_MODE: "rinse"})
    check("F4 网页 op=5 选 rinse 受理",
          st == 200 and (doc or {}).get("ok") is True,
          "应答=%s" % (doc or {}).get("err"))
    check("F5 桌面粘性=rinse",
          mode_state(d).get(proto.KEY_AUTO_MODE) == "rinse",
          "autoMode=%s" % mode_state(d).get(proto.KEY_AUTO_MODE))
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_SELECT, proto.KEY_AUTO_MODE: "backwash"},
              timeout=8.0) or {}
    # [web 模型 1s 轮询] 桌面侧的动作要等 hub 的 MODE_POLL 环追上才在
    # /api/mode 可见（组合循环相位也是这条环驱动的）⇒ 等待式断言。
    def web_sees_backwash():
        _st, _h2, wdoc = web.get("/api/mode")
        return ((wdoc or {}).get(proto.KEY_AUTO_MODE) == "backwash"
                and wdoc) or None
    check("F6 桌面 op=5 选 backwash → 网页同值（≤3s 轮询环）",
          bool(wait_until(web_sees_backwash, 3.0, step_s=0.4)),
          "ok=%s" % r.get(proto.KEY_OK))
    web.post("/api/mode", {"mode": 0})
    wait_until(lambda: mode_state(d).get("mode") == 0, 5.0)

    # 快照键集一致
    dv = d.req(proto.CMD_VFD_SNAPSHOT) or {}
    st, _h, wv = web.post("/api/vfd/snapshot")
    wvk = set((wv or {}).keys()) - {"ok"}
    check("F7 变频器快照键集两端一致",
          st == 200 and wvk == set(dv.keys()),
          "web缺=%s 桌面缺=%s" % (sorted(set(dv) - wvk)[:6],
                                  sorted(wvk - set(dv))[:6]))
    dal = valve_snap(d)
    st, _h, wal = web.post("/api/valve/snapshot")
    wak = set((wal or {}).keys()) - {"ok"}
    check("F8 阀快照键集两端一致",
          st == 200 and wak == set(dal.keys()),
          "diff=%s" % sorted(wak ^ set(dal))[:6])

    # 配置：网页写 → 桌面读同值；桌面写 → 网页读同值
    # [值域内取值] sampleMs 服务端夹取 [1000,3600000]（文档口径），用
    # 3000000（域内、与现值不同）验「写→另一端读到同值」。
    st, _h, doc = web.post("/api/config", {"section": "storage",
                                           "sampleMs": 3000000})
    check("F9 网页配置写受理（需管理员）",
          st == 200 and (doc or {}).get("ok") is True,
          "应答=%s" % (doc or {}).get("err"))
    dcfg = d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}
    check("F10 桌面读到网页写的 sampleMs=3000000",
          int((dcfg.get("storage") or {}).get("sampleMs") or 0) == 3000000,
          "storage=%s" % (dcfg.get("storage") or {}).get("sampleMs"))
    r = d.req(proto.CMD_CONFIG_SET, {"section": "storage",
                                     "sampleMs": 3600000},
              timeout=8.0)
    st, _h, wcfg = web.get("/api/config")
    check("F11 桌面配置写 → 网页读到同值（3600000）",
          bool(r.get(proto.KEY_OK))
          and int(((wcfg or {}).get("storage") or {}).get("sampleMs") or 0)
          == 3600000,
          "web storage=%s" % ((wcfg or {}).get("storage") or {})
          .get("sampleMs"))


# ============================================================================
#          M. 远端设置配置矩阵（合法组合 × 错误值 fuzz × 拒后不坏）
# ============================================================================

CFG_SECTIONS = ("collect", "storage", "login", proto.CFG_SECTION_AUTO,
                proto.CFG_SECTION_PROTECT)


def cfg_sections_of(d):
    """GET 全量，只取 5 个配置节（剔除会自然漂移的 storageDbPath/Stats）。"""
    doc = d.req(proto.CMD_CONFIG_GET, None, timeout=10.0) or {}
    return {k: doc.get(k) for k in CFG_SECTIONS}


def snaps_equal(a, b):
    return json.dumps(a, sort_keys=True, ensure_ascii=False,
                      default=str) == json.dumps(b, sort_keys=True,
                                                 ensure_ascii=False,
                                                 default=str)


def section_config_matrix(d, web, server_proc, srv_port):
    print("== M. 配置矩阵（合法组合 + 错误值 fuzz + 拒后状态不坏）==")
    perm_port = [srv_port]

    def set_ok(doc, name, timeout=8.0):
        r = d.req(proto.CMD_CONFIG_SET, doc, timeout=timeout) or {}
        check(name, r.get(proto.KEY_OK) is True,
              "err=%s" % r.get("err"))
        return r

    def expect_reject(doc, name, want_kw, snap_before):
        """SET 必须被拒（含关键词）；拒后全量回读逐键一致（整包回滚）。"""
        r = d.req(proto.CMD_CONFIG_SET, doc, timeout=8.0) or {}
        ok = (r.get(proto.KEY_OK) is not True
              and (not want_kw or want_kw in str(r.get("err", ""))))
        check(name, ok,
              "ok=%s err=%s" % (r.get(proto.KEY_OK), r.get("err")))
        after = cfg_sections_of(d)
        check("%s ⇢ 拒后配置回读一致" % name,
              snaps_equal(snap_before, after), "")

    def snap():
        return cfg_sections_of(d)

    # ---- M1 collect：逐台开关 + 周期档位 ----
    s0 = snap()
    set_ok({"section": "collect", proto.CFGK_VFD_ENABLED: False,
            proto.CFGK_TURBIDITY1_ENABLED: False,
            proto.CFGK_TURBIDITY2_ENABLED: True},
           "M1a collect 逐台开关（停 VFD/浊1、留浊2）")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("collect", {})
    check("M1b GET 回显逐台开关",
          got.get(proto.CFGK_VFD_ENABLED) is False
          and got.get(proto.CFGK_TURBIDITY1_ENABLED) is False
          and got.get(proto.CFGK_TURBIDITY2_ENABLED) is True,
          "collect=%s" % {k: got.get(k) for k in
                          (proto.CFGK_VFD_ENABLED,
                           proto.CFGK_TURBIDITY1_ENABLED,
                           proto.CFGK_TURBIDITY2_ENABLED)})
    set_ok({"section": "collect", proto.CFGK_VFD_POLL_MS: 700,
            proto.CFGK_TURBIDITY1_POLL_MS: 700}, "M1c 周期逐台写 700ms")
    set_ok({"section": "collect", proto.CFGK_VFD_POLL_MS: 49},
           "M1d 周期下界 49 → 夹取 50")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("collect", {})
    check("M1e GET = 夹取值 50",
          int(got.get(proto.CFGK_VFD_POLL_MS) or 0) == proto.CFG_POLL_MS_MIN,
          "vfdPollMs=%s" % got.get(proto.CFGK_VFD_POLL_MS))
    set_ok({"section": "collect", proto.CFGK_VFD_POLL_MS: 60001},
           "M1f 周期上界 60001 → 夹取 60000")
    set_ok({"section": "collect", proto.CFGK_VFD_ENABLED: True,
            proto.CFGK_TURBIDITY1_ENABLED: True,
            proto.CFGK_VFD_POLL_MS: 500,
            proto.CFGK_TURBIDITY1_POLL_MS: 500}, "M1g 恢复 collect")

    # ---- M2 storage：三档 fullPolicy / 触发两档 / 边界夹取 ----
    for pol in ("ring", "stop", "rotate"):
        set_ok({"section": "storage", proto.CFGK_STORAGE_FULL_POLICY: pol},
               "M2a fullPolicy=%s" % pol)
    for trig in ("auto", "always"):
        set_ok({"section": "storage",
                proto.CFGK_STORAGE_HISTORY_TRIGGER: trig},
               "M2b historyTrigger=%s" % trig)
    set_ok({"section": "storage", proto.CFGK_STORAGE_MAX_MB: 0},
           "M2c maxMb=0（显式「不限」仍合法）")
    set_ok({"section": "storage", proto.CFGK_STORAGE_SAMPLE_MS: 999},
           "M2d sampleMs 999 → 夹取 1000")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("storage", {})
    check("M2e GET sampleMs=1000（下界夹取）",
          int(got.get(proto.CFGK_STORAGE_SAMPLE_MS) or 0) == 1000,
          "sampleMs=%s" % got.get(proto.CFGK_STORAGE_SAMPLE_MS))
    set_ok({"section": "storage", proto.CFGK_STORAGE_PATH: "矩阵库.db"},
           "M2f path 写文件名")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("storage", {})
    check("M2g path 回显一致",
          got.get(proto.CFGK_STORAGE_PATH) == "矩阵库.db",
          "path=%s" % got.get(proto.CFGK_STORAGE_PATH))
    set_ok({"section": "storage", proto.CFGK_STORAGE_PATH: "",
            proto.CFGK_STORAGE_SAMPLE_MS: 3600000,
            proto.CFGK_STORAGE_MAX_MB: 50,
            proto.CFGK_STORAGE_FULL_POLICY: "ring"}, "M2h 恢复 storage")

    # ---- M3 login：匿名/并发/上限边界 ----
    set_ok({"section": "login", proto.CFGK_LOGIN_ALLOW_ANON: True},
           "M3a 匿名只读开")
    set_ok({"section": "login", proto.CFGK_LOGIN_ALLOW_ANON: False,
            proto.CFGK_LOGIN_ALLOW_CONCUR: True,
            proto.CFGK_LOGIN_MAX_SESSIONS: 0}, "M3b 匿名关 + 上限 0（不限）")
    set_ok({"section": "login", proto.CFGK_LOGIN_MAX_SESSIONS: -5},
           "M3c 上限 -5 → 夹取 0")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("login", {})
    _ms = got.get(proto.CFGK_LOGIN_MAX_SESSIONS)
    check("M3d GET maxSessions=0",
          int(_ms) == 0 if _ms is not None else False,
          "login=%s" % got)
    set_ok({"section": "login", proto.CFGK_LOGIN_MAX_SESSIONS: 999},
           "M3e 上限 999 → 夹取 256")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("login", {})
    check("M3f GET maxSessions=256", int(got.get(
        proto.CFGK_LOGIN_MAX_SESSIONS) or -1) == 256, "")
    set_ok({"section": "login", proto.CFGK_LOGIN_MAX_SESSIONS: 16,
            proto.CFGK_LOGIN_ALLOW_CONCUR: True}, "M3g 恢复 login")

    # ---- M4 auto.modes：空选/单选/全选 + rpm 边界（换相策略 9 组合已迁
    #      至 M5f/M5g 的 cycle.instances，见下方）----
    all_devs = ["valve1", "valve2", "valve3", "valve4", "valve5", "valve6",
                "stirPump", "inletPump"]
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
        "devices": []}}}, "M4a produce 空选（=清空）")
    got = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
           .get(proto.CFG_SECTION_AUTO, {}).get("modes", {})
           .get("produce", {}))
    check("M4b GET devices=空", got.get("devices") == [], "")
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
        "devices": ["valve1"]}}}, "M4c 单选 valve1")
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
        "devices": all_devs}}}, "M4d 全选 8 设备")
    got = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
           .get(proto.CFG_SECTION_AUTO, {}).get("modes", {})
           .get("produce", {}))
    check("M4e GET devices=全选（集合一致）",
          sorted(got.get("devices") or []) == sorted(all_devs), "")
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
        "stirRpm": 0}}}, "M4f rpm 下界 0")
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
        "stirRpm": 1500}}}, "M4g rpm 1500 → 夹取 1440")
    got = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
           .get(proto.CFG_SECTION_AUTO, {}).get("modes", {})
           .get("produce", {}))
    check("M4h GET rpm=1440", int(got.get("stirRpm") or 0) == 1440,
          "rpm=%s" % got.get("stirRpm"))
    # [换相策略 2026-11-26 实例化迁移（原 M4i/M4j）] 策略从逐模式
    # （auto.modes.*）搬进逐实例（cycle.instances[i].stirSwitchPolicy /
    # inletSwitchPolicy）：服务端对 modes 里的旧策略键是「忽略不报错、
    # 不回显」⇒ 9 组合的 SET/GET 往返改到 instances 上做（见下方 M5f/M5g，
    # 与 webui_check 的 sec_switch_policy 同款改法）——放在 M5 的旧键用例
    # 之后，因为 SET instances 会让配置进入「显式 instances」态（旧键降级
    # 为派生视图），不能再走 M5 的 cycleModes 旧键路径。
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {
        "produce": {"devices": ["valve1", "xyz_unknown"],
                    proto.AUTO_MODE_RESET_VALVES_ON_STOP: False,
                    proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: True}},
    }, "M4k 混入未知设备 id（可识别项保留）+ 双复位开关")
    got = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
           .get(proto.CFG_SECTION_AUTO, {}).get("modes", {})
           .get("produce", {}))
    check("M4l 未知 id 丢弃、复位开关回显",
          got.get("devices") == ["valve1"]
          and got.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP) is False
          and got.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP) is True,
          "produce=%s" % got)

    # ---- M5 auto.cycle：cycleModes 组合 + stageCfg 全键 ----
    for modes_list in (["produce", "backwash"],
                       ["produce", "backwash", "rinse"],
                       ["drain", "rinse", "backwash", "produce"]):
        set_ok({"section": proto.CFG_SECTION_AUTO, "cycle": {
            "enabled": True,
            proto.CFG_AUTO_CYCLE_MODES: modes_list}},
            "M5a cycleModes=%s" % "→".join(modes_list))
        gotc = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
                .get(proto.CFG_SECTION_AUTO, {}).get("cycle", {}))
        check("M5b GET cycleModes 回显有序一致",
              gotc.get(proto.CFG_AUTO_CYCLE_MODES) == modes_list,
              "got=%s" % gotc.get(proto.CFG_AUTO_CYCLE_MODES))
    stage = {m: {proto.CFG_AUTO_STAGE_PRESS_ENABLED: True,
                 proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.3 + i * 0.1,
                 proto.CFG_AUTO_STAGE_PRESS_COMPARE: "gt",
                 proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                 proto.CFG_AUTO_STAGE_TIMER_MINUTES: 5 + i}
             for i, m in enumerate(("produce", "backwash", "rinse"))}
    set_ok({"section": proto.CFG_SECTION_AUTO, "cycle": {
        "enabled": True,
        proto.CFG_AUTO_CYCLE_MODES: ["produce", "backwash", "rinse"],
        proto.CFG_AUTO_CYCLE_STAGE_CFG: stage}}, "M5c stageCfg 全键写入")
    gotc = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
            .get(proto.CFG_SECTION_AUTO, {}).get("cycle", {}))
    scfg = gotc.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}
    check("M5d stageCfg 回显逐阶段一致",
          all(abs(float((scfg.get(m) or {}).get(
              proto.CFG_AUTO_STAGE_PRESS_THRESHOLD) or 0)
              - (0.3 + i * 0.1)) < 1e-6
              and int((scfg.get(m) or {}).get(
                  proto.CFG_AUTO_STAGE_TIMER_MINUTES) or 0) == 5 + i
              for i, m in enumerate(("produce", "backwash", "rinse"))),
          "stageCfg=%s" % scfg)
    set_ok({"section": proto.CFG_SECTION_AUTO, "cycle": {"enabled": False}},
           "M5e 恢复 cycle 关闭")

    # ---- M5f/M5g 换相策略 9 组合（原 M4i/M4j；2026-11-26 实例化迁移）----
    # 策略逐实例可配 ⇒ SET/GET 往返改在 cycle.instances 上做（服务端对
    # modes 旧策略键忽略不回显）。SET instances 成功后配置进入「显式
    # instances」态（无取消入口）⇒ 收尾恢复成两实例语义：
    #   · produce＝设备齐全 + 双触发（不拦闸），承载本轮策略组合；
    #   · rinse＝定时触发在（SET 合法——devices 非空是**启动闸**要求，
    #     SET 不拒空）但设备留空 ⇒ M6b 的「缺设备启动拒」由实例闸复现
    #     （文案「需要参与循环的实例都配设备，缺：顺洗模式#1」）。
    # M6 之后无组合循环消费方（M13 只跑变频器 + op=5、H 只查粘性），
    # sim 用完即弃 ⇒ 残留无害。
    _pol_base = [
        {"mode": "produce",
         "devices": list(MODES["produce"]["devices"]),
         "pressEnabled": True, "pressThreshold": 0.3, "pressCompare": "gt",
         "timerEnabled": True, "timerMinutes": 30},
        {"mode": "rinse", "devices": [],
         "pressEnabled": False, "pressThreshold": 0.5, "pressCompare": "gt",
         "timerEnabled": True, "timerMinutes": 10},
    ]
    for st_pol in proto.AUTO_SWITCH_POLICY_IDS:
        for in_pol in proto.AUTO_SWITCH_POLICY_IDS:
            _inst0 = dict(_pol_base[0])
            _inst0[proto.AUTO_MODE_STIR_SWITCH] = st_pol
            _inst0[proto.AUTO_MODE_INLET_SWITCH] = in_pol
            set_ok({"section": proto.CFG_SECTION_AUTO, "cycle": {
                "enabled": True,
                proto.CFG_AUTO_CYCLE_INSTANCES:
                    [_inst0, dict(_pol_base[1])]}},
                "M5f 换相策略 %s×%s（instances）" % (st_pol, in_pol))
    gota = ((d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {})
            .get(proto.CFG_SECTION_AUTO, {}))
    gotc = gota.get("cycle", {})
    goti = gotc.get(proto.CFG_AUTO_CYCLE_INSTANCES) or []
    check("M5g 策略回显 = 最后一组（instances[0] 逐实例生效）",
          bool(goti)
          and goti[0].get(proto.AUTO_MODE_STIR_SWITCH) == in_pol
          and goti[0].get(proto.AUTO_MODE_INLET_SWITCH) == in_pol
          and goti[0].get("mode") == "produce"
          and (goti[1] if len(goti) > 1 else {}).get(
              proto.AUTO_MODE_STIR_SWITCH) == "stopRestart",
          "instances=%s" % str(goti)[:240])
    check("M5h modes.produce 不再回显旧策略键（忽略 ≠ 改值）",
          proto.AUTO_MODE_STIR_SWITCH
          not in (gota.get("modes", {}).get("produce") or {})
          and proto.AUTO_MODE_INLET_SWITCH
          not in (gota.get("modes", {}).get("produce") or {}),
          "produce=%s" % str(gota.get("modes", {}).get("produce"))[:160])

    # ---- M6 组合循环启动闸：循环开 + 子模式缺设备 ⇒ 启动拒 ----
    # [启动闸在自动模式后面] 模式闸先于配置校验 ⇒ 验配置闸必须先切自动。
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": {"rinse": {
        "devices": []}}, "cycle": {"enabled": True}},
        "M6a rinse 清设备 + 循环开")
    d.req(proto.CMD_RUN_MODE, {"mode": 1})
    wait_until(lambda: mode_state(d).get("mode") == 1, 6.0)
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "auto"},
              timeout=10.0) or {}
    check("M6b 组合模式启动被拒（参与循环子模式配置校验）",
          not r.get(proto.KEY_OK)
          and ("参与循环" in str(r.get("err", ""))
               or "配设备" in str(r.get("err", ""))
               or "子模式" in str(r.get("err", ""))
               or "未配置" in str(r.get("err", ""))),
          "err=%s" % r.get("err"))
    d.req(proto.CMD_RUN_MODE, {"mode": 0})
    wait_until(lambda: mode_state(d).get("mode") == 0, 6.0)
    set_ok({"section": proto.CFG_SECTION_AUTO, "modes": MODES,
            "cycle": {"enabled": False}}, "M6c 恢复四模式 + 循环关")

    # ---- M7 protect：时限边界 + 双保护开关全档 ----
    set_ok({"section": proto.CFG_SECTION_PROTECT,
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: -100},
           "M7a 空管时限 -100 → 夹取 0（关闭）")
    got = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get(proto.CFG_SECTION_PROTECT, {})
    _to = got.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS)
    check("M7b GET 时限=0", int(_to) == 0 if _to is not None else False,
          "protect=%s" % got)
    for stop in (True, False):
        set_ok({"section": proto.CFG_SECTION_PROTECT,
                proto.CFGK_PROTECT_INLET_NOFLOW_STOP: stop,
                proto.CFGK_PROTECT_PRESS_HIGH_STOP: stop},
               "M7c 停机开关=%s" % stop)
    set_ok({"section": proto.CFG_SECTION_PROTECT,
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: 0,
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP: True,
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED: False,
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA: 0.8,
            proto.CFGK_PROTECT_PRESS_HIGH_STOP: True}, "M7d 恢复 protect")

    # ---- M8 错误值 fuzz（fail-closed 拒 + 拒后整包回读一致）----
    print("   · M8 错误值 fuzz（每条拒后回读比对）")
    s_fuzz = snap()
    fuzz_cases = [
        ("M8a collect 布尔发字符串",
         {"section": "collect", proto.CFGK_VFD_ENABLED: "true"}, "bool"),
        ("M8b collect 周期发字符串",
         {"section": "collect", proto.CFGK_VFD_POLL_MS: "700"}, "毫秒"),
        ("M8c collect 周期发布尔",
         {"section": "collect", proto.CFGK_VFD_POLL_MS: True}, "毫秒"),
        ("M8d collect 未知键",
         {"section": "collect", "noSuchKey": 1}, "未知"),
        ("M8e storage enabled 发数字",
         {"section": "storage", proto.CFGK_STORAGE_ENABLED: 1}, "bool"),
        ("M8f storage sampleMs 发字符串",
         {"section": "storage", proto.CFGK_STORAGE_SAMPLE_MS: "5"}, "毫秒"),
        ("M8g storage fullPolicy 野值",
         {"section": "storage", proto.CFGK_STORAGE_FULL_POLICY: "ring2"},
         "fullPolicy"),
        ("M8h storage historyTrigger 野值",
         {"section": "storage", proto.CFGK_STORAGE_HISTORY_TRIGGER:
             "sometimes"}, "historyTrigger"),
        ("M8i storage path 发数字",
         {"section": "storage", proto.CFGK_STORAGE_PATH: 123}, "字符串"),
        ("M8j storage keepFiles 发字符串",
         {"section": "storage", proto.CFGK_STORAGE_KEEP_FILES: "x"},
         "keepFiles"),
        ("M8k login maxSessions 发布尔",
         {"section": "login", proto.CFGK_LOGIN_MAX_SESSIONS: True}, "int"),
        ("M8l login 并发发字符串",
         {"section": "login", proto.CFGK_LOGIN_ALLOW_CONCUR: "yes"},
         "bool"),
        ("M8m auto 未知模式 id",
         {"section": proto.CFG_SECTION_AUTO,
          "modes": {"nope": {"devices": ["valve1"]}}}, "未知自动模式"),
        ("M8n auto stirRpm 发字符串",
         {"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
             "stirRpm": "abc"}}}, "整数"),
        ("M8o auto stirReverse 发字符串",
         {"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
             "stirReverse": "true"}}}, "stirReverse"),
        # [0/1 是合法口径] strict_bool 只拒字符串、放行 bool/0/1（文档
        # 口径「有些客户端发整数开关」）⇒ 数字 1 不进 fuzz 拒绝集。
        ("M8p auto devices 发字符串",
         {"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
             "devices": "valve1"}}}, "数组"),
        ("M8q auto devices 全未知",
         {"section": proto.CFG_SECTION_AUTO, "modes": {"produce": {
             "devices": ["xyz1", "xyz2"]}}}, "可识别"),
        # [2026-11-26 实例化迁移] M8r 原打 modes 旧策略键（服务端忽略不拒）
        # ⇒ 改打实例载荷；M8t~M8zb 原打 cycleModes/stageCfg 旧键（M5f 之后
        # 配置已「显式 instances」，旧键走写穿路径：cycleModes 忽略、
        # stageCfg 只做逐字段校验，不再有「重复/缺参与阶段」这类列表级
        # 校验）⇒ 全部改到 instances 的同语义 fail-closed 上（want 关键词
        # 与 app.py _cfg_set_cycle_instances/_cycle_instance_entry_check
        # 的实读文案对齐）。
        ("M8r auto 换相策略野值（instances）",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {"instances": [
             {"mode": "produce", "devices": ["valve1"],
              "pressEnabled": True,
              proto.AUTO_MODE_STIR_SWITCH: "teleport"},
             {"mode": "backwash", "timerEnabled": True}]}}, "只能是"),
        ("M8s cycle enabled 发字符串",
         {"section": proto.CFG_SECTION_AUTO,
          "cycle": {"enabled": "yes"}}, "enabled"),
        ("M8t instances 单实例",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: [
                 {"mode": "produce", "devices": ["valve1"],
                  "timerEnabled": True}]}}, "2 个"),
        ("M8u instances 空",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: []}}, "不能为空"),
        ("M8v instances 元素非对象",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: [1, 2]}}, "对象"),
        ("M8w instances 未知模式 id",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: [
                 {"mode": "produce", "devices": ["valve1"],
                  "timerEnabled": True},
                 {"mode": "nope", "devices": ["valve1"],
                  "timerEnabled": True}]}}, "mode"),
        ("M8x instances 发字符串",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: "produce"}}, "数组"),
        ("M8y stageCfg 发数组",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_MODES: ["produce", "backwash"],
             proto.CFG_AUTO_CYCLE_STAGE_CFG: []}}, "对象"),
        ("M8z instances 超 32 个",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: [
                 {"mode": "produce", "devices": ["valve1"],
                  "timerEnabled": True} for _i in range(33)]}}, "32 个"),
        ("M8za instances 四触发全关",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_INSTANCES: [
                 {"mode": "produce", "devices": ["valve1"],
                  "pressEnabled": True, "timerEnabled": True},
                 {"mode": "produce", "devices": ["valve1"],
                  "pressEnabled": False, "press1Enabled": False,
                  "press2Enabled": False, "timerEnabled": False}]}},
         "切换条件"),
        ("M8zb cycle 压差方向野值",
         {"section": proto.CFG_SECTION_AUTO, "cycle": {
             proto.CFG_AUTO_CYCLE_PRESS_COMPARE: "around"}}, "pressCompare"),
        ("M8zc protect 时限发字符串",
         {"section": proto.CFG_SECTION_PROTECT,
          proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: "x"}, "整数"),
        ("M8zd protect 停机发字符串",
         {"section": proto.CFG_SECTION_PROTECT,
          proto.CFGK_PROTECT_INLET_NOFLOW_STOP: "no"}, "布尔"),
        ("M8ze protect 上限发字符串",
         {"section": proto.CFG_SECTION_PROTECT,
          proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA: "abc"}, "数字"),
        ("M8zf 未知配置节",
         {"section": "noSuchSection", "x": 1}, ""),
    ]
    for name, doc, kw in fuzz_cases:
        expect_reject(doc, name, kw, s_fuzz)

    # ---- M9 怪值（合法但极端）：UTF-8 注入 / 超长串（走网页 JSON 面）----
    # [恢复走 web 面] M 段桌面桥可能已自愈重连，但 path 的恢复不依赖桌面：
    # 全程用 web 面 SET/GET + 桌面 GET 交叉验证。
    st, _h, doc = web.post("/api/config",
                           {"section": "storage",
                            proto.CFGK_STORAGE_PATH: "泄漏\线路「回车」✓"})
    check("M9a UTF-8 特殊字符 path 可存（web 面）",
          st == 200 and (doc or {}).get("ok") is True,
          "err=%s" % (doc or {}).get("err"))
    st, _h, wsto = web.get("/api/config")
    got = ((wsto or {}).get("storage") or {})
    check("M9b UTF-8 path web GET 回显逐字一致",
          got.get(proto.CFGK_STORAGE_PATH) == "泄漏\线路「回车」✓",
          "path=%r" % got.get(proto.CFGK_STORAGE_PATH))
    dgot = (d.req(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}) \
        .get("storage", {})
    check("M9b2 UTF-8 path 桌面 GET 同值（两端一致）",
          dgot.get(proto.CFGK_STORAGE_PATH) == "泄漏\线路「回车」✓",
          "path=%r" % dgot.get(proto.CFGK_STORAGE_PATH))
    big = "B" * (70 * 1024)
    st, _h, doc = web.post("/api/config", {"section": "storage",
                                           proto.CFGK_STORAGE_PATH: big})
    # [判据 = 不打挂 + 状态可判定] web 面没有 16KB BSON 闸，但转发经
    # XLink 的上游帧有大小上限：要么受理（回显一致）要么显式失败
    # （4xx/5xx，实测上游会话被超大帧掐断 ⇒ hub 自愈重连）——两种都算
    # fail-closed；唯独「挂着不动/进程死/回不来」不行。
    check("M9c 70KB path：受理或显式拒绝（不挂不死）",
          st in (200, 400, 413, 502, 504), "st=%s" % st)
    # [上游会话恢复] 超大帧可能掐断 hub 的上游会话 ⇒ 先等 web 面回活，
    # 这本身就是「服务没被打死」的断言。
    web_back = wait_until(lambda: web.get("/api/mode")[0] == 200, 25.0,
                          step_s=0.5)
    check("M9c2 web 上游链路自愈回活", bool(web_back), "")
    st2, _h2, wsto2 = web.get("/api/config")
    applied = (((wsto2 or {}).get("storage") or {})
               .get(proto.CFGK_STORAGE_PATH) == big)
    check("M9d 70KB path 状态可判定（受理=回显 / 拒绝=保持）",
          (st == 200 and applied)
          or (st != 200 and not applied),
          "st=%s applied=%s" % (st, applied))

    def _restore_path():
        _st, _h3, _doc = web.post("/api/config",
                                  {"section": "storage",
                                   proto.CFGK_STORAGE_PATH: ""})
        return (_st == 200 and (_doc or {}).get("ok") is True) or None
    check("M9e 恢复 path=空（web 面；等自愈后受理）",
          bool(wait_until(_restore_path, 15.0)), "")
    st, _h, wsto3 = web.get("/api/config")
    check("M9e2 恢复后回显为空",
          ((wsto3 or {}).get("storage") or {})
          .get(proto.CFGK_STORAGE_PATH) == "", "")

    # ---- M10 权限错：非管理员 SET 拒 / 只读 GET 可 ----
    # [为什么单独开 oper] 缺省建户三档（admin 只读 / oper 操作者 / root），
    # 配置 SET 需 PERM_ACCOUNT_ADMIN——操作者与只读都该被拒，只读 GET 放行。
    oper = None
    try:
        oper = TapClient(perm_port[0], user="oper", password="oper").connect()
    except Exception as exc:                        # noqa: BLE001
        check("M10a 非管理员客户端可用", False, "%s: %s"
              % (type(exc).__name__, exc))
    if oper is not None:
        r = oper.request(proto.CMD_CONFIG_SET,
                         {"section": "storage",
                          proto.CFGK_STORAGE_SAMPLE_MS: 1000},
                         timeout=8.0) or {}
        check("M10a 操作者 SET 被拒（文案含「管理员」）",
              not r.get(proto.KEY_OK)
              and "管理员" in str(r.get("err", "")),
              "err=%s" % r.get("err"))
        r = oper.request(proto.CMD_CONFIG_GET, None, timeout=8.0) or {}
        check("M10b 只读 GET 允许（设置页回显不涉密）",
              r.get(proto.KEY_OK) is True
              and isinstance(r.get("storage"), dict), "")
        r = oper.request(proto.CMD_VFD_SNAPSHOT, timeout=6.0) or {}
        check("M10c 被拒后继续读快照正常", "vm" in r, "")
        oper.close()

    # ---- M11 LinkBridge 收错误包络不崩（抽 5 种坏值走桌面桥）----
    s_m11 = snap()
    bridge_cases = [
        {"section": "collect", proto.CFGK_VFD_ENABLED: "true"},
        {"section": "storage", proto.CFGK_STORAGE_FULL_POLICY: 7},
        {"section": proto.CFG_SECTION_AUTO, "modes": {"nope": {}}},
        # [2026-11-26 实例化迁移] 原 cycleModes 单元素（显式 instances 后
        # cycleModes 是写穿路径上的「忽略」键 ⇒ 不再拒）⇒ 改打 instances
        # 发字符串（仍是「SET 必拒」的坏值包络）。
        {"section": proto.CFG_SECTION_AUTO, "cycle": {
            proto.CFG_AUTO_CYCLE_INSTANCES: "produce"}},
        {"section": proto.CFG_SECTION_PROTECT,
         proto.CFGK_PROTECT_INLET_NOFLOW_STOP: "no"},
    ]
    alive = True
    for i, doc in enumerate(bridge_cases):
        r = d.req(proto.CMD_CONFIG_SET, doc, timeout=8.0)
        if not isinstance(r, dict) or r.get(proto.KEY_OK) is not False:
            alive = False
            check("M11%s 桥收错误包络" % ("abcdefg"[i]), False,
                  "应答=%s" % (r,))
    check("M11 桥连续收 5 种坏值错误包络后仍可用", alive, "")
    check("M12 桥拒后配置回读一致", snaps_equal(s_m11, snap()), "")

    # ---- M13 收尾大断言：拒了 30+ 种坏值后正常跑一轮 ----
    ensure_manual(d, "M13")
    r = d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START}) or {}
    check("M13a fuzz 后变频器启动受理", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get("err"))
    ok_run = bool(wait_until(lambda: vfd_snap(d).get("running"), 15.0))
    d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_STOP})
    check("M13b fuzz 后正常启停（服务端没被坏值打挂）", ok_run,
          "running=%s" % vfd_snap(d).get("running"))
    r = d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_SELECT, proto.KEY_AUTO_MODE: "produce"},
              timeout=8.0) or {}
    check("M13c fuzz 后自动面受理（op=5 选模式）",
          bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
    st, _h, wdoc = web.get("/api/mode")
    check("M13d fuzz 后网页端正常响应", st == 200, "st=%s" % st)
    check("M13e 服务端进程活着", server_proc.proc.poll() is None, "")


# ============================================================================
#                     G. 泄漏探测循环
# ============================================================================

def section_leak_loop(d, web, server_proc, web_proc):
    print("== G. 泄漏探测（%d 轮混合负载）==" % LEAK_ROUNDS)
    ensure_manual(d, "G")
    sv_mem = []
    web_mem = []
    anchor = int(time.time() * 1000)
    kw_path = ("/api/history/export?format=csv&kw=%s&kwCols=1"
               "&beforeTs=%d" % (urllib.parse.quote("手动"), anchor))
    for i in range(LEAK_ROUNDS):
        # —— 桌面协议面混合操作 ——
        d.req(proto.CMD_VFD_SNAPSHOT)
        d.req(proto.CMD_AUTO_CONTROL,
              {"op": proto.AUTO_OP_SELECT, proto.KEY_AUTO_MODE: "produce"},
              timeout=8.0)
        d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_START})
        time.sleep(0.3)
        d.req(proto.CMD_VFD_CONTROL, {"op": proto.OP_STOP})
        d.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 10},
              timeout=10.0)
        d.req(proto.CMD_LOG_PAGE, {"offset": 0, "limit": 10,
                                   "category": "ctrl"}, timeout=10.0)
        d.req(proto.CMD_ALARM_PAGE, {"offset": 0, "limit": 10},
              timeout=10.0)
        d.req(proto.CMD_VALVE_CONTROL, {"index": 0, "on": True})
        d.req(proto.CMD_VALVE_CONTROL, {"index": 0, "on": False})
        # —— 网页端混合操作 ——
        web.get("/api/mode")
        web.post("/api/vfd/snapshot")
        web.get("/api/history/page?limit=5")
        if i % 4 == 0:
            web.download(kw_path)
        pump(0.1)
        time.sleep(0.7)
        sv_mem.append(proc_mem_bytes(server_proc.proc.pid))
        web_mem.append(proc_mem_bytes(web_proc.proc.pid))
        alive = (server_proc.proc.poll() is None
                 and web_proc.proc.poll() is None)
        if not alive:
            check("G%d 进程存活" % i, False, "轮 %d 进程退出" % i)
            break

    def _trend(mem, label):
        got = [m for m in mem if m is not None]
        if len(got) < LEAK_ROUNDS - 1:
            check("G-%s 内存采样可用" % label, False, "样本=%d" % len(got))
            return
        incs = [got[i] - got[i - 1] for i in range(3, len(got))]
        steady = incs[-5:] if len(incs) >= 5 else incs
        mean_steady = sum(steady) / float(len(steady))
        growth = got[-1] - got[3]
        check("G-%s 稳态轮每轮私有内存增量 ≤ %dKB" % (label, LEAK_TOL_BYTES // 1024),
              mean_steady <= LEAK_TOL_BYTES,
              "稳态均值=%+.0fKB/轮 总增=%+.0fMB 序列=%s"
              % (mean_steady / 1024.0, growth / 1048576.0,
                 ["%+.0f" % (d / 1024.0) for d in incs]))
        check("G-%s 无持续爬升（总增 ≤ 40MB）" % label,
              growth <= 40 * 1024 * 1024,
              "总增=%+.1fMB" % (growth / 1048576.0))

    _trend(sv_mem, "服务端")
    _trend(web_mem, "网页端")


# ============================================================================
#                     H. 服务端重启持久化
# ============================================================================

def section_restart_persist(server_proc, port, cfg_path, tmp, d):
    print("== H. 重启持久化 ==")
    before = mode_state(d)
    check("H1 重启前 autoRunMs>0 且有粘性模式",
          int(before.get(proto.KEY_AUTO_RUN_MS) or 0) > 0
          and bool(before.get(proto.KEY_AUTO_MODE)),
          "runMs=%s mode=%s" % (before.get(proto.KEY_AUTO_RUN_MS),
                                before.get(proto.KEY_AUTO_MODE)))
    sticky = before.get(proto.KEY_AUTO_MODE)
    d.close()
    server_proc.stop()
    time.sleep(1.0)
    argv = [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
            "--config", cfg_path, "--sim-valve-move", "0.6"]
    server_proc.argv = argv
    server_proc.start(wait_port=port, timeout=40.0)
    d2 = Desktop(port).connect()
    st = mode_state(d2)
    check("H2 重启后 autoRunMs 恢复（不归零）",
          int(st.get(proto.KEY_AUTO_RUN_MS) or 0)
          >= int(before.get(proto.KEY_AUTO_RUN_MS) or 0) - 1000,
          "before=%s after=%s" % (before.get(proto.KEY_AUTO_RUN_MS),
                                  st.get(proto.KEY_AUTO_RUN_MS)))
    check("H3 重启后粘性模式恢复（%s）" % sticky,
          st.get(proto.KEY_AUTO_MODE) == sticky,
          "autoMode=%s" % st.get(proto.KEY_AUTO_MODE))
    r = d2.req(proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 1},
               timeout=10.0) or {}
    check("H4 重启后历史库仍在（种子行未丢）",
          int(r.get("total") or 0) >= SEED_ROWS,
          "total=%s" % r.get("total"))
    return d2


# ============================================================================
#                     I. 卡阀实例（阀超时告警链）
# ============================================================================

def section_valve_timeout(port):
    print("== I. 卡阀：阀超时告警 + 锁存 + 复位 ==")
    tmp = tempfile.mkdtemp(prefix="hsf_e2e_stuck_")
    proc, _p, _cfg, _t = start_server("stuck", tmp, valve_move_s="-1")
    tap = None
    try:
        tap = TapClient(_p).connect()
        r = tap.request(proto.CMD_CONFIG_SET, {
            "section": proto.CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve1", "inletPump"],
                                  "stirRpm": 720}}}, timeout=8.0) or {}
        check("I1 卡阀实例配置受理", bool(r.get(proto.KEY_OK)), "")
        r = tap.request(proto.CMD_RUN_MODE, {"mode": 1}) or {}
        check("I2 切自动受理", bool(r.get(proto.KEY_OK)), "")
        r = tap.request(proto.CMD_AUTO_CONTROL,
                        {"op": proto.AUTO_OP_START,
                         proto.KEY_AUTO_MODE: "produce"}, timeout=10.0) or {}
        check("I3 启动受理（进阀相）",
              bool(r.get(proto.KEY_OK))
              and r.get(proto.KEY_AUTO_PHASE) == "valves", "应答=%s" % r)
        frames = wait_until(
            # [请求兼保活] 等告警的窗口里周期性发**真网络请求**——alarm_frames
            # 只读本地缓存不算流量，裸 TapClient 无心跳，20s 等待会被服务端
            # 按 6s 静默判线断链（实测 18:17:11 disconnected），后续断言全
            # 拿不到数据。CMD_RUN_MODE 查询兼保活（test_sim_full_flow 同口径）。
            lambda: (tap.request(proto.CMD_RUN_MODE, timeout=8.0) and
                     tap.alarm_frames(proto.AUTO_ALARM_VALVE_TIMEOUT)) or None,
            VALVE_TIMEOUT_BUDGET, step_s=1.0)
        check("I4 收到 kind=5 阀超时告警帧", bool(frames),
              "帧=%s 状态=%s 日志=%s" % (tap.frames[:2], tap.states[-4:],
                                        tap.client_log[-4:]))
        if frames:
            text = str(frames[0].get("text", ""))
            check("I5 告警文字点名阀1 +「超时」",
                  "阀1" in text and "超时" in text, "text=%s" % text)
        st = tap.request(proto.CMD_RUN_MODE) or {}
        check("I6 停止自动模式 + autoFault=true",
              st.get(proto.KEY_AUTO_RUNNING) is False
              and st.get(proto.KEY_AUTO_FAULT) is True,
              "running=%s fault=%s" % (st.get(proto.KEY_AUTO_RUNNING),
                                       st.get(proto.KEY_AUTO_FAULT)))
        doc = tap.request(proto.CMD_DIGITAL_SNAPSHOT, {"dev": 0}) or {}
        m = int(doc.get(proto.KEY_DO) or 0)
        check("I7 泵绝不被启动（DO6=0）", (m & (1 << 6)) == 0,
              "do=0x%X" % m)
        r = tap.request(proto.CMD_AUTO_CONTROL,
                        {"op": proto.AUTO_OP_START,
                         proto.KEY_AUTO_MODE: "produce"}, timeout=10.0) or {}
        check("I8 未复位再启动被拒（含「复位」）",
              not r.get(proto.KEY_OK) and "复位" in str(r.get("err", "")),
              "err=%s" % r.get("err"))
        r = tap.request(proto.CMD_AUTO_CONTROL,
                        {"op": proto.AUTO_OP_RESET}, timeout=10.0) or {}
        check("I9 复位受理", bool(r.get(proto.KEY_OK)), "")
        r = tap.request(proto.CMD_AUTO_CONTROL,
                        {"op": proto.AUTO_OP_START,
                         proto.KEY_AUTO_MODE: "produce"}, timeout=10.0) or {}
        check("I10 复位后可再启动", bool(r.get(proto.KEY_OK)),
              "err=%s" % r.get("err"))
        tap.request(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_STOP},
                    timeout=10.0)
        # 手动开卡阀：受理但永不到位（动作中/超时口径，不等 20s）
        # [先复位] 停止时「关阀复位」会向卡阀发关命令 ⇒ 20s 后阀层超时
        # 再次锁存（产品行为正确）；手动命令面前先解一次锁。
        tap.request(proto.CMD_AUTO_CONTROL, {"op": proto.AUTO_OP_RESET},
                    timeout=10.0)
        wait_until(lambda: (tap.request(proto.CMD_RUN_MODE) or {})
                   .get(proto.KEY_AUTO_FAULT) is False, 6.0)
        tap.request(proto.CMD_RUN_MODE, {"mode": 0})
        wait_until(lambda: (tap.request(proto.CMD_RUN_MODE) or {})
                   .get("mode") == 0, 6.0)
        r = tap.request(proto.CMD_VALVE_CONTROL, {"index": 0, "on": True},
                        timeout=8.0) or {}
        check("I11 手动开卡阀受理（命令面 OK）",
              bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
    finally:
        if tap is not None:
            tap.close()
        proc.stop()
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                               主流程
# ============================================================================

def main():
    t_start = time.time()
    ensure_qapp()
    tmp_srv = tempfile.mkdtemp(prefix="hsf_e2e_srv_")
    tmp_web = tempfile.mkdtemp(prefix="hsf_e2e_web_")
    server = None
    webproc = None
    d = None
    d2 = None
    tap = None
    page = None
    web = None
    try:
        # ---- 装配三端 ----
        server, port, cfg_path, _ = start_server("srv", tmp_srv)
        print("== 隔离 sim 服务端已起：127.0.0.1:%d（tmp=%s）"
              % (port, tmp_srv))
        d = Desktop(port).connect()
        check("Z1 桌面桥登录（root/root）+ 角色=管理员",
              d.bridge.role == 2, "role=%s" % d.bridge.role)
        tap = TapClient(port).connect()
        webproc, wport = start_web(port, tmp_web)
        web = WebApi(wport)
        ok, err = web.login()
        check("Z2 网页端登录（root/root）", ok, err)
        ready = wait_until(lambda: (web.get("/api/mode")[0] == 200), 20.0)
        check("Z3 网页端上游链路就绪", bool(ready), "")

        # 等热身门放行、现况行入库
        live = wait_until(lambda: int((d.req(
            proto.CMD_HISTORY_PAGE, {"offset": 0, "limit": 1},
            timeout=8.0) or {}).get("dbRows") or 0) >= 3, 40.0)
        check("Z4 sim 现况历史行入库（热身门放行）", bool(live),
              "dbRows=%s" % (d.req(proto.CMD_HISTORY_PAGE,
                                   {"offset": 0, "limit": 1},
                                   timeout=8.0) or {}).get("dbRows"))

        # 停住入库节拍（计数稳定），灌种子
        r = d.req(proto.CMD_CONFIG_SET, {"section": "storage",
                                         "sampleMs": 3600000},
                  timeout=8.0)
        check("Z5 停住入库节拍（sampleMs=3600000）",
              bool(r and r.get(proto.KEY_OK)), "err=%s" % (r or {}).get("err"))
        time.sleep(2.5)
        live_total = int((d.req(proto.CMD_HISTORY_PAGE,
                                {"offset": 0, "limit": 1}, timeout=8.0) or {})
                         .get("total") or 0)
        db_path = wait_until(lambda: find_db_file(tmp_srv), 10.0)
        check("Z6 找到隔离 SQLite 库", bool(db_path), db_path or "")
        db_total, n_manual, n_auto, win = seed_history(db_path)
        check("Z7 种子 13000 行入库（库总数对账）",
              db_total == live_total + SEED_ROWS,
              "db=%d live=%d seed=%d" % (db_total, live_total, SEED_ROWS))
        total_after = int((d.req(proto.CMD_HISTORY_PAGE,
                                 {"offset": 0, "limit": 1},
                                 timeout=8.0) or {}).get("total") or 0)
        check("Z8 协议侧总数 = 库总数（跨进程 WAL 可见）",
              total_after == db_total,
              "proto=%d db=%d" % (total_after, db_total))

        # 桌面真历史页（导出/图表共用）；状态行走捕获池（观察终态文案）
        from hsfsrv_client.bridge import LinkBridge as _LB  # noqa: F401
        from hsfsrv_client.page_base import PageContext
        from hsfsrv_client.pages_other import HistoryPage
        status_sink = []
        ctx = PageContext(d.bridge, lambda i: None, status_sink.append)
        page = HistoryPage(ctx)
        page.on_enter()
        pump(1.0)
        check("Z9 桌面历史页进入服务端数据源",
              page._data_mode == "server", "mode=%s" % page._data_mode)

        # ---- 各段 ----
        sections = [
            ("A", lambda: section_manual(d, tap)),
            ("B", lambda: section_auto(d, tap)),
            ("C", lambda: section_data(d, db_path, win, n_auto,
                                       db_total)),
            ("D1", lambda: section_export_desktop(page, db_path,
                                                  status_sink)),
            ("D2", lambda: section_export_web(
                web, db_path,
                db_count(db_path, "mode = ?", ("手动",)), d)),
            ("E1", lambda: section_chart_desktop(page)),
            ("E2", lambda: section_web_trend(web)),
            ("F", lambda: section_consistency(d, web)),
            ("G", lambda: section_leak_loop(d, web, server, webproc)),
            ("M", lambda: section_config_matrix(d, web, server, port)),
        ]
        for tag, fn in sections:
            try:
                fn()
            except Exception as exc:                # noqa: BLE001
                import traceback
                check("%s 段异常" % tag, False, "%s: %s"
                      % (type(exc).__name__, exc))
                traceback.print_exc()

        try:
            d2 = section_restart_persist(server, port, cfg_path, tmp_srv, d)
        except Exception as exc:                    # noqa: BLE001
            import traceback
            check("H 段异常", False, "%s: %s" % (type(exc).__name__, exc))
            traceback.print_exc()
            try:
                d2 = Desktop(port).connect() if server.proc else None
            except Exception:                       # noqa: BLE001
                d2 = None

        try:
            section_valve_timeout(0)
        except Exception as exc:                    # noqa: BLE001
            import traceback
            check("I 段异常", False, "%s: %s" % (type(exc).__name__, exc))
            traceback.print_exc()

        # 收尾把模式拉回手动（本实例用完即弃，仅为日志整洁）
        try:
            if d2 is not None:
                d2.req(proto.CMD_RUN_MODE, {"mode": 0})
        except Exception:                           # noqa: BLE001
            pass

        cost = time.time() - t_start
        print("\n===== 三端 E2E：检查 %d 项，失败 %d 项，耗时 %.1fs ====="
              % (_G_CHECKS[0], len(_G_FAILS), cost))
        for name in _G_FAILS:
            print("   失败项：%s" % name)
        return 0 if not _G_FAILS else 1
    except Exception as exc:                        # noqa: BLE001
        import traceback
        print("[FAIL] 装配/主流程异常：%s: %s" % (type(exc).__name__, exc))
        traceback.print_exc()
        if server is not None:
            print(server.tail())
        if webproc is not None:
            print(webproc.tail())
        return 1
    finally:
        for closer in (lambda: d.close() if d else None,
                       lambda: d2.close() if d2 else None,
                       lambda: tap.close() if tap else None):
            try:
                closer()
            except Exception:                       # noqa: BLE001
                pass
        if server is not None:
            server.stop()
        if webproc is not None:
            webproc.stop()
        shutil.rmtree(tmp_srv, ignore_errors=True)
        shutil.rmtree(tmp_web, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
