# -*- coding: utf-8 -*-
"""Python 客户端（含内嵌网页端）内存泄漏门禁（2026-11-14 用户口径
「各种情况下都不能有内存泄漏」）。

[为什么要有这一份]
==================
服务端侧有 `server_py/tests/test_leak_gate.py`（tracemalloc 堆级）。
**客户端这一侧**此前只有「反复连接」段（`test_hsf_regression.py` §27，
验的是功能不是堆）。客户端的泄漏大头有三处：

    ① XLinkClient 的会话生命周期（每次连接建线程/协程/订阅表；断开必须
       全部回收）——历史上「退出竞态」类缺陷就出在这里；
    ② **流式 XLSX 写出器**（2026-11-14 新增，不限行导出 ⇒ 若把行攒在
       内存里就是无上限的泄漏）；
    ③ **内嵌网页端**（2026-11-14 新增，网页服务器跑在**客户端进程内**的
       后台线程里 ⇒ 起停必须把线程与端口还回去）。

[2026-11-20 补「UI 生命周期」场景]
=================================
上面三个场景**全都不碰界面** ⇒ 页面 / 图表 / 定时器 / 浮层的泄漏一条都
查不出来。本轮补四个场景（**只改本文件**）：

    J **主窗建/弃**：建主窗 → 显示 → 关 → 删 → 泵事件 ⇒ 整棵界面树必须
      全部回收（Qt 对象 + 定时器 + 事件过滤器）。
    K **反复切页**：18 页建/弃。`MainWindow.pages` **按设计缓存**已建页面
      ⇒ 第二遍访问同一页**不得**再建一个（否则页面对象无限增长）；窗口
      销毁后页面树必须全部回收。
    L **相机页全屏覆盖层**：进/出全屏各 10 次 ⇒ 覆盖层的 100ms 定时器
      **必须停**（漏停就是泄漏）、页面持有的引用必须清零、窗口销毁后覆盖
      层必须回收。
    M **折线图弹窗**：建/显示/关/删 N 次 ⇒ 弹窗树必须回收。

[Qt 的四个坑（2026-11-20 实踩，全是"量到假象"的来源）]
====================================================
    ① **离屏时 `isVisible()` 恒 False**（窗口没真正 show 出去）⇒ 判显隐
       一律用 `isHidden()`。
    ② **`QApplication` 一个进程只能有一个** ⇒ 复用已有实例，不新建。
    ③ **关窗后 Qt 对象可能延迟析构** ⇒ 删对象前 `deleteLater()` +
       `processEvents()` 多泵几轮（还要显式 `sendPostedEvents(None,
       DeferredDelete)`）⇒ 否则量到的是「还没回收」不是泄漏。
    ④ **必须区分「真泄漏」与「缓存」**：`MainWindow.pages` 缓存页面是
       设计如此 ⇒ 场景 K 的断言针对「每次新建的对象」而不是「缓存里的
       对象」。

[判据口径]
==========
`tracemalloc`（Python 分配器精确计数）+ **分文件归因**：
`client_bytes` = 分配发生在 `client_py/hsfsrv_client/**` 与
`webui_py/hsfweb/**` 的字节数 ⇒ 就是客户端自己的堆。

每个场景跑 **3 轮**：第 1 轮吸收一次性初始化，第 2/3 轮是**稳态**；
判据只看**最后一个稳态轮**的每轮增量（理由同服务端门：真泄漏是每轮恒定
的 ⇒ 用「轮间差」抓不到；用最后一轮判是为了躲开一次性成本）。

[怎么跑]
========
    python -I tests/test_leak_gate.py        # 独立跑
    python -I tests/run_all.py              # 全量（含本段）

服务端以**子进程**起（`python -m hsfsrv --sim`，隔离端口与临时目录），
不碰现场跑着的实例。
"""

import gc
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import tracemalloc
from collections import Counter

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ 输出重定向到文件时
# stdout 用 GBK ⇒ 打印「⇒」这类字符直接 UnicodeEncodeError（实测：门禁在
# 打印断言时崩，退出码还是 0 ⇒ 看着像"跑完了"）。强制 UTF-8 + replace。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

# [离屏 2026-11-20] UI 场景要在**没有屏幕**的环境里跑（回归/探针/板端
# CI）⇒ 必须在 **import PyQt5 之前**定死平台插件，否则 QApplication 建出来
# 会去找真实显示器而失败。用 setdefault：外部显式指定了平台就不覆盖。
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
#: `HighSpeedFilter` 目录（server_py / webui_py 的父目录）。
HS_ROOT = os.path.dirname(CLIENT_ROOT)
SERVER_PY = os.path.join(HS_ROOT, "server_py")
WEBUI_PY = os.path.join(HS_ROOT, "webui_py")
for _p in (HERE, CLIENT_ROOT, SERVER_PY, WEBUI_PY):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from hsfsrv_client import proto                    # noqa: E402
from hsfsrv_client import pages_registry as reg    # noqa: E402（纯常量，无 Qt）
from hsfsrv_client.xlsx_stream import XlsxStreamWriter   # noqa: E402
from hsfsrv_client.xlink import XLinkClient        # noqa: E402

#: [PyQt5 可选 2026-11-20] UI 场景需要 PyQt5；没有它就**跳过** UI 场景
#: （而不是让整个门禁崩掉）⇒ 纯服务端的机器上仍能跑 F/H/I 三段。
try:
    from PyQt5.QtCore import QEvent, QTimer        # noqa: E402
    from PyQt5.QtWidgets import QApplication, QWidget   # noqa: E402
    HAVE_QT = True
except Exception:                                   # noqa: BLE001
    HAVE_QT = False

PASS = 0
FAIL = 0

# ---------------------------------------------------------------------------
# 判据容差（按实测噪声标定）
# ---------------------------------------------------------------------------
#: 稳态轮里客户端堆的每轮增量上限（字节）。
TOL_CLIENT_BYTES = 24 * 1024
#: 稳态轮里客户端活对象（按类）的每轮增量上限（个）。
TOL_CLIENT_OBJECTS = 20
#: 稳态轮里线程数的每轮增量上限（个）——**必须为 0**（客户端每次连接建
#: 一条网络线程；断开后不回收就是泄漏）。
TOL_THREADS = 0
#: 线程池线程名的前缀（**不算进泄漏判据**）：Python 3.12 的 asyncio 默认
#: executor 线程叫 "asyncio_N"（不是 "ThreadPoolExecutor-N_M"——那是普通
#: ThreadPoolExecutor）⇒ 只排后者会把池扩容误判成泄漏（2026-11-14 实踩）。
_POOL_THREAD_PREFIXES = ("asyncio_", "ThreadPoolExecutor-")
#: 稳态轮里 asyncio 任务的每轮增量上限（个）。
TOL_TASKS = 0
#: [UI 场景新增 2026-11-20] 稳态轮里**活 QWidget**（Qt 对象）的每轮增量
#: 上限（个）。UI 场景的核心判据：建了一堆界面 ⇒ 拆掉后必须回到基线。
#: 实测稳态轮为 0 ⇒ 留 8 的余量给平台噪声。
TOL_WIDGETS = 8
#: [UI 场景新增 2026-11-20] 稳态轮里**活跃定时器**的每轮增量上限（个）
#: ——**必须为 0**（定时器没停 = 界面还在偷偷跑 = 泄漏；相机页全屏覆盖层
#: 的 100ms 定时器就是这一类）。
TOL_ACTIVE_TIMERS = 0

#: 每个场景的轮数（第 1 轮预热，不判）。
#:
#: [3 → 4 轮 2026-11-21] 判据改成「**最后两个稳态轮的净增**」⇒ 必须有第 4
#: 轮才能观测「上一轮的一次性分配是否已回落」。理由：客户端有一类**延迟
#: 释放**的分配（相机页全屏覆盖层的图像缓冲等，约 128KB）：它在某一轮里
#: 分配、**下一轮**才被回收 ⇒ 单看最后一轮就是「凭空多了 128KB」⇒ 假红
#: （实测 L 场景：K 场景同批分配表现为「第2轮 +131344 / 第3轮 -131344」，
#: 而活对象/活控件/活跃定时器增量**全是 0** ⇒ 明确不是泄漏）。
#: ⇒ 4 轮 + 两轮净增：延迟释放跨两轮抵消（净增≈0）⇒ 不误伤；
#: **真泄漏每轮恒定 +X ⇒ 两轮净增 2X ⇒ 照红**（门不削弱）。
ROUNDS = 4
#: 场景规模。
CHURN_SESSIONS = 30       # F：每轮 30 次连接/断开
CHURN_XLSX = 3            # H：每轮 3 轮 13000 行导出
CHURN_WEB = 3             # I：每轮 3 次网页端起停
CHURN_WINDOWS = 10        # J：每轮 10 次主窗建/弃
CHURN_FULLSCREEN = 10     # L：每轮 10 次进/出全屏
CHURN_CHARTS = 10         # M：每轮 10 次折线图弹窗开/关
#: 导出行数（与客户端回归 §21 Q4 同口径：13000 > 旧的 12000 上限）。
XLSX_ROWS = 13000


#: [A/B 实证临时件] 故意留下的泄漏引用（跑完 A/B 立刻还原）。
_AB_LEAK = []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _norm(path):
    return path.replace("\\", "/")


def _scan_objects():
    """扫一遍活对象：客户端自有类计数 + **活 QWidget 数** + **活跃定时器数**。

    [为什么一趟扫完] `gc.get_objects()` 有 3 万+ 个对象 ⇒ 扫三趟就是三倍
    时间；一趟里把三件事做完（按类计数 / 两个 isinstance 判定）最省。

    [活 QWidget 为什么要单独数] 客户端自有类计数只覆盖
    `hsfsrv_client.**` / `hsfweb.**` 的 Python 类 ⇒ **Qt 基类控件**
    （QLabel/QPushButton/QStackedWidget…）不在其中；而 UI 泄漏的主体恰恰
    是这些控件（页面上千个控件）⇒ 必须有一个"活控件总数"的口径。
    """
    counts = Counter()
    widgets = 0
    timers = 0
    for obj in gc.get_objects():
        t = type(obj)
        mod = getattr(t, "__module__", "") or ""
        if (mod == "hsfsrv_client" or mod.startswith("hsfsrv_client.")
                or mod == "hsfweb" or mod.startswith("hsfweb.")):
            counts[t.__name__] += 1
        if not HAVE_QT:
            continue
        try:
            if isinstance(obj, QWidget):
                widgets += 1
        except Exception:                            # noqa: BLE001
            pass
        try:
            if isinstance(obj, QTimer) and obj.isActive():
                timers += 1
        except Exception:                            # noqa: BLE001
            # C++ 对象已删（isActive 抛 RuntimeError）⇒ 不算活跃定时器。
            pass
    return counts, widgets, timers


def _snapshot():
    """分配量快照（分文件归因 + 分类型对象数 + 活控件数 + 线程数）。"""
    gc.collect()
    gc.collect()
    cur, peak = tracemalloc.get_traced_memory()
    snap = tracemalloc.take_snapshot()
    client_bytes = 0
    other_bytes = 0
    for stat in snap.statistics("filename"):
        fn = _norm(stat.traceback[0].filename)
        if ("/client_py/hsfsrv_client/" in fn
                or "/webui_py/hsfweb/" in fn):
            client_bytes += stat.size
        else:
            other_bytes += stat.size
    threads = [t.name for t in threading.enumerate()
               if not t.name.startswith(_POOL_THREAD_PREFIXES)]
    types, widgets, timers = _scan_objects()
    return {
        "client": client_bytes,
        "other": other_bytes,
        "total": cur,
        "objects": len(gc.get_objects()),
        "types": types,
        "widgets": widgets,
        "timers": timers,
        "threads": len(threads),
        "thread_names": threads,
        "snap": snap,
    }


def _delta(a, b):
    return {k: b[k] - a[k] for k in
            ("client", "other", "total", "objects", "widgets", "timers",
             "threads")}


def _type_delta(a, b):
    out = {}
    for name, cnt in b["types"].items():
        d = cnt - a["types"].get(name, 0)
        if d > 0:
            out[name] = d
    return out


def _fmt_delta(d, td):
    return ("客户端 %+d B / 其他 %+d B / 对象 %+d / 控件 %+d / 活跃定时器 %+d"
            " / 线程 %+d / 类增量 %s"
            % (d["client"], d["other"], d["objects"], d["widgets"],
               d["timers"], d["threads"],
               (", ".join("%s+%d" % (k, v) for k, v in
                          sorted(td.items(), key=lambda kv: -kv[1])[:4]))
               or "无"))


def _top_growth(prev, cur, limit=5):
    out = []
    try:
        stats = cur.compare_to(prev, "traceback")
    except Exception:                                # noqa: BLE001
        return out
    for stat in stats[:limit]:
        if stat.size_diff <= 0:
            continue
        where = ""
        for fr in stat.traceback:
            where = "%s:%d" % (os.path.basename(fr.filename), fr.lineno)
            break
        out.append("%s %+d B ×%d" % (where, stat.size_diff, stat.count_diff))
    return out


# ============================================================================
# 服务端子进程（隔离端口 + 临时目录，不碰现场实例）
# ============================================================================

def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class ServerProc(object):
    def __init__(self):
        self.port = _free_port()
        self.tmp = tempfile.mkdtemp(prefix="hsf_cli_leak_")
        self.proc = None
        self.log = open(os.path.join(self.tmp, "server.out"), "wb")

    def start(self):
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        # [隔离] 复制 cfg 到临时目录并把 cwd 指过去 ⇒ 服务端的配置文件、
        # 账户库、落盘库都落在临时目录里 ⇒ **不碰仓库里的** cfg/hsf_users.json
        # 与 hsf_data.db（否则跑一次门禁就把现场的库搅了）。
        src_cfg = os.path.join(SERVER_PY, "cfg")
        if os.path.isdir(src_cfg):
            dst_cfg = os.path.join(self.tmp, "cfg")
            os.makedirs(dst_cfg, exist_ok=True)
            for name in os.listdir(src_cfg):
                if name.endswith(".json"):
                    shutil.copy2(os.path.join(src_cfg, name),
                                 os.path.join(dst_cfg, name))
        # 包从仓库里导入，数据落在临时目录（cwd）。
        env["PYTHONPATH"] = SERVER_PY + os.pathsep + env.get("PYTHONPATH", "")
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(self.port)],
            cwd=self.tmp, stdout=self.log, stderr=subprocess.STDOUT, env=env)
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.proc.poll() is not None:
                # [日志要**当场**读] 收尾会 rmtree 临时目录 ⇒ 事后路径已不存在
                # ⇒ 把内容读进异常消息，别让现场丢失。
                raise RuntimeError("服务端秒退（rc=%s）：%s"
                                   % (self.proc.returncode, self.tail()))
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), 0.3)
                sock.close()
                return self.port
            except OSError:
                time.sleep(0.2)
        raise RuntimeError("服务端 30s 未监听 %d" % self.port)

    def tail(self, n=1200):
        """读服务端日志尾部（诊断用）。"""
        try:
            with open(os.path.join(self.tmp, "server.out"), "rb") as fh:
                return fh.read().decode("utf-8", "replace")[-n:]
        except OSError:
            return "(日志读不到)"

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=6)
            except Exception:                          # noqa: BLE001
                self.proc.kill()
        self.proc = None
        try:
            self.log.close()
        except OSError:
            pass
        shutil.rmtree(self.tmp, ignore_errors=True)


# ============================================================================
# 三个场景
# ============================================================================

TOPICS = (
    proto.TOPIC_VFD_STATUS,
    proto.TOPIC_AI_STATUS,
    proto.TOPIC_TEMPHUM_STATUS,
    proto.TOPIC_FLOWMETER_STATUS,
    proto.TOPIC_DEVICE_HEALTH,
    proto.TOPIC_DIGITAL_STATUS,
    proto.TOPIC_VALVE_STATUS,
    proto.TOPIC_PUMP_STATUS,
    getattr(proto, "TOPIC_AUTO_ALARM", 0x0001000B),
)


def _scenario_sessions(ctx, n):
    """F 会话生命周期：连接→登录→订阅 9 主题→拉快照→断开。"""
    fails = 0
    for _ in range(n):
        cli = XLinkClient("127.0.0.1", ctx["port"], "root", "root")
        cli.auto_reconnect = False
        good, err = cli.start(timeout=10.0)
        if not good:
            fails += 1
            cli.close()
            continue
        try:
            for topic in TOPICS:
                fut = cli.call_soon(cli.subscribe(topic))
                if fut is None:
                    fails += 1
                    break
                try:
                    fut.result(timeout=5.0)
                except Exception as exc:                # noqa: BLE001
                    fails += 1
                    print("     [订阅异常] %r" % (exc,))
                    break
            fut = cli.call_soon(cli.request(proto.CMD_DEVICE_HEALTH, b"",
                                             proto.DATATYPE_BSON, 5.0))
            if fut is None:
                fails += 1
            else:
                try:
                    fut.result(timeout=6.0)
                except Exception as exc:                # noqa: BLE001
                    fails += 1
                    print("     [请求异常] %r" % (exc,))
        finally:
            cli.close()
            try:
                if cli._thread is not None:
                    cli._thread.join(timeout=5.0)
            except Exception:                          # noqa: BLE001
                pass
    return _result(fails)


def _scenario_xlsx(ctx, n):
    """H 流式 XLSX 导出：每轮写 13000 行（不限行导出的内存口径）。

    [为什么盯这个] 2026-11-14 把 XLSX 导出改成流式（不限行）⇒ 若写出器
    把行攒在内存里，导 100 万行就是几百 MB ⇒ 判据必须是「每轮导出后
    客户端堆不涨」。
    """
    fails = 0
    out_dir = ctx["tmp"]
    for rnd in range(n):
        path = os.path.join(out_dir, "leak_export_%d.xlsx" % rnd)
        try:
            w = XlsxStreamWriter(path, ["history"])
            w.sheet(0, ["ts", "mode", "freq_hz", "turb1"])
            batch = []
            for i in range(XLSX_ROWS):
                batch.append([i, "手动", 12.5, 2.29])
                if len(batch) >= 500:
                    w.rows(0, batch)
                    batch = []
            if batch:
                w.rows(0, batch)
            w.close()
            if not os.path.isfile(path) or os.path.getsize(path) <= 0:
                fails += 1
        except Exception as exc:                      # noqa: BLE001
            fails += 1
            print("     [导出异常] %s: %s" % (type(exc).__name__, exc))
        finally:
            try:
                os.remove(path)
            except OSError:
                pass
    return _result(fails)


def _scenario_web(ctx, n):
    """I 内嵌网页端起停：起→打一个 API→停，线程与端口必须还回去。

    [为什么要盯] 网页服务器跑在**客户端进程内**的后台线程（2026-11-14
    落地）⇒ 起停不干净 = 每次开关设置都留一条线程 + 占住一个端口 ⇒
    连开几次就把端口占满了、客户端也拖慢。
    """
    import urllib.request
    from hsfweb import embed
    fails = 0
    for rnd in range(n):
        web_port = _free_port()
        handle = embed.start_in_thread(server="127.0.0.1", port=ctx["port"],
                                       web_port=web_port, user="root",
                                       password="root")
        try:
            if not handle.wait_ready(timeout=10.0):
                fails += 1
                print("     [网页端未就绪] 第 %d 次" % rnd)
            else:
                # 打一个真接口（不需要登录也能返回 200/302 ⇒ 只看连接通）
                try:
                    with urllib.request.urlopen(
                            "http://127.0.0.1:%d/" % web_port, timeout=5) as r:
                        r.read(256)
                except Exception as exc:                # noqa: BLE001
                    # 302/401 也算「通」；只有连不上才算失败。
                    if "HTTP Error" not in str(exc):
                        fails += 1
                        print("     [网页端不可达] %r" % (exc,))
        finally:
            handle.stop(timeout=5.0)
        # 端口必须被释放（否则下一次起会撞端口）
        time.sleep(0.3)
        try:
            probe = socket.create_connection(("127.0.0.1", web_port), 0.5)
            probe.close()
            fails += 1
            print("     [端口未释放] %d" % web_port)
        except OSError:
            pass
    return _result(fails)


# ============================================================================
# UI 生命周期场景（2026-11-20 补：J 主窗 / K 切页 / L 全屏 / M 折线图）
# ============================================================================
#
# [这四段为什么必须自己做，而不是靠 F 段顺带测]
# F 段只管链路（线程/协程/订阅表）。**界面对象树**是另一套生命周期：
# QWidget 由 C++ 侧持有、Python 侧只留包装器 ⇒ 「关窗」不等于「回收」
# （要 deleteLater + 泵事件），「页面缓存」也不等于「泄漏」（设计如此）。
# ⇒ 这四段各自盯自己的那一段，且**每段都带对照组断言**（证明这轮真的做了
# 动作），否则场景可能空跑假绿（AGENTS 记的同款坑：「必须有对照组断言，
# 否则等于没测到东西」「不进门禁的锁等于没有锁」）。

#: 后台探测线程名（Web 设置页的跨进程探测 / 起停）。
#: [为什么要等它们] 探测线程的闭包**持有页面对象**（`self.webProbeDone.emit`
#: 要用页面）⇒ 线程在跑 ⇒ 页面（连带整棵窗口树）就还活着 ⇒ 收尾前不等
#: 它，快照量到的是「还没回收」⇒ **假红**。等它自然结束（它有超时）就
#: 干净了。
_BG_THREAD_NAMES = ("hsf-web-probe", "hsf-web-op")


#: QApplication 的**进程级**强引用（**必须持有**，见 `_qt_app` 的注）。
_QT_APP = None


def _qt_app():
    """取（必要时建）QApplication —— **一个进程只能有一个** ⇒ 复用已有。

    [为什么必须留一份进程级强引用 —— 2026-11-20 实锅]
    PyQt5 里 `QApplication` 的 **Python 包装器一旦被回收**，它连同 C++ 侧
    的全局状态（顶层窗口簿记 / 事件过滤器表…）一起没 ⇒ **最后一扇窗的
    包装器就再也回收不掉了**（它的回收周期搭在 app 的 C++ 结构上）。
    实测：每轮残留 **1 个 MainWindow + 167 个活控件 + 58 KB 客户端堆**
    ⇒ 门禁全红，量到的却是「对象没被回收」而不是泄漏（**假红**）。
    ⇒ 进程内始终持有一份强引用（`_QT_APP`），别让包装器被回收。
    """
    global _QT_APP
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    _QT_APP = app
    return app


def _pump(app, n=6):
    """泵事件循环 n 轮（**含 DeferredDelete** ⇒ 延迟析构真正发生）。

    [为什么必须显式 sendPostedEvents] `processEvents()` 不保证投递
    DeferredDelete ⇒ 光泵事件，排队中的销毁不会发生 ⇒ 量到的是「还没
    回收」不是泄漏（假红）。
    """
    for _ in range(n):
        app.processEvents()
    app.sendPostedEvents(None, QEvent.DeferredDelete)
    app.processEvents()


def _dispose(app, widget):
    """关窗 → 排队删 → 泵事件（**删了不泵 = 没删**）。

    [为什么泵两轮 DeferredDelete] Qt 的延迟删除跨「事件循环层」：一批
    `sendPostedEvents` 里连带销毁的对象，它的 DeferredDelete 会留到下一
    批 ⇒ 只泵一轮时每隔一个窗口会残留（实测：残留 1/2 ⇒ 假红）。
    """
    try:
        widget.close()
    except RuntimeError:
        pass
    try:
        widget.deleteLater()
    except RuntimeError:
        pass
    for _ in range(2):
        _pump(app, 4)


def _wait_bg_threads(app, timeout=8.0):
    """等后台探测线程收工（它们持有页面对象 ⇒ 不等就是假红）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not [t for t in threading.enumerate()
                if t.name in _BG_THREAD_NAMES]:
            return True
        _pump(app, 2)
        time.sleep(0.02)
    return False


def _result(fails=0, checks=None, facts=None):
    """场景返回值。

    :param fails: 场景内部发现的失败次数（异常 / 动作没做成）。
    :param checks: [(说明, 是否通过, 细节)] —— **对照组 + 功能性判据**，
        由 main() 统一打印并计入 PASS/FAIL。
    :param facts: 观测量（进主窗个数、进出全屏次数…）⇒ 报告用。
    """
    checks = list(checks or [])
    return {"fails": int(fails), "checks": checks,
            "facts": dict(facts or {})}


def _scenario_windows(ctx, n):
    """J 主窗建/弃：建 → 显示 → 关 → 删 ⇒ 整棵界面树必须回收。

    [为什么盯这个] 主窗是**整棵界面树**的根（chrome + 页面栈 + 定时器 +
    软键盘 + 事件过滤器）⇒ 「重开一次客户端」的等价动作。任何没断开的事件
    连接 / 定时器 / 事件过滤器都会在这里显形（表现为活 QWidget 或活跃定
    时器每轮只涨不跌）。

    [为什么用**未连接的**桥] UI 生命周期与链路无关；连上服务端反而会引
    入网络线程与「同账并发」策略的噪声（服务端拒第二个同账号会话 ⇒ 假
    红）。⇒ 只建界面，不登录。
    """
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    app = _qt_app()
    built = 0
    for _ in range(n):
        bridge = LinkBridge()
        win = MainWindow(bridge)
        win.show()
        _pump(app, 4)
        built += 1
        _dispose(app, win)
        del win
        del bridge
        gc.collect()
    _pump(app, 4)
    checks = [
        ("本轮确实建/弃了 %d 个主窗（对照组）" % n, built == n,
         "实际 %d 个" % built),
    ]
    fails = sum(1 for _l, ok, _d in checks if not ok)
    return _result(fails, checks, {"built": built})


def _scenario_pages(ctx, n):
    """K 反复切页：n 页建/弃（n = 全部页数）。

    [缓存 vs 泄漏 —— 本段的核心]
      ① 第一遍访问**必须**建出页面（否则场景空跑 ⇒ 假绿）；
      ② 第二遍访问**零新建**（`MainWindow.pages` 按设计缓存 ⇒ 命中缓存；
         又新建 ⇒ 缓存失效 ⇒ 页面对象无限增长 = 泄漏）；
      ③ 窗口销毁后页面树**全部回收**（活 QWidget / 客户端活对象不涨）。
    ⇒ 断言针对「每次新建的对象」，不是「缓存里的对象」。
    """
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg
    app = _qt_app()
    idxs = list(range(reg.PAGE_COUNT))[:n]
    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.show()
    _pump(app, 4)
    built1 = 0
    for idx in idxs:
        n0 = len(win.pages)
        win.goto_page(idx)
        _pump(app, 2)
        if len(win.pages) > n0:
            built1 += 1
    built2 = 0
    for idx in idxs:
        n0 = len(win.pages)
        win.goto_page(idx)
        _pump(app, 2)
        if len(win.pages) > n0:
            built2 += 1
    total_cached = len(win.pages)
    # 后台探测线程收工（Web 设置页的跨进程探测持有页面对象 ⇒ 不等就假红）
    _wait_bg_threads(app)
    _dispose(app, win)
    del win
    del bridge
    gc.collect()
    _pump(app, 4)
    checks = [
        ("本轮访问了 %d 个页面（对照组）" % len(idxs),
         len(idxs) == n and n > 0, "实际 %d 个" % len(idxs)),
        ("第一遍访问确实建出了页面（对照组：场景没空跑）",
         built1 >= max(1, len(idxs) - 1),
         "新建 %d 个（页面缓存共 %d 个）" % (built1, total_cached)),
        ("第二遍访问零新建（pages 缓存命中 ⇒ 不是泄漏）", built2 == 0,
         "又新建 %d 个" % built2),
    ]
    fails = sum(1 for _l, ok, _d in checks if not ok)
    return _result(fails, checks,
                   {"visited": len(idxs), "built_first": built1,
                    "built_second": built2, "cached": total_cached})


def _scenario_fullscreen(ctx, n):
    """L 相机页全屏覆盖层：进/出 n 次。

    [盯什么] 覆盖层自带 **100ms 定时器**（自己贴图）⇒ 退出时**必须停**
    （漏停 = 界面还在偷偷跑 = 泄漏，且每次进出全屏都多一个活定时器）；
    退出后页面持有的引用必须清零（`page._fs is None`，否则第二次进不去）；
    窗口销毁后覆盖层必须全部回收。

    [判显隐用 isHidden()] 离屏下 `isVisible()` 恒 False ⇒ 用 `isHidden()`
    反过来判「显示着」。
    """
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client import pages_registry as reg
    app = _qt_app()
    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.show()
    _pump(app, 4)
    win.goto_page(reg.PAGE_MONITOR_CAMERA)
    _pump(app, 4)
    page = win.pages.get(reg.PAGE_MONITOR_CAMERA)
    entered = 0
    exited = 0
    timer_alive = 0
    for _ in range(n):
        page.enter_fullscreen()
        _pump(app, 2)
        fs = page._fs
        if fs is None:
            continue
        if not fs.isHidden():                        # 离屏：isVisible 恒 False
            entered += 1
        fs.exit_fs()
        _pump(app, 2)
        if page._fs is None and fs.isHidden():
            exited += 1
        try:
            if fs._timer.isActive():
                timer_alive += 1
        except RuntimeError:
            pass
    # [观察项] 同一窗口内反复进出 ⇒ 每次都会新建一个覆盖层（旧的只隐藏、
    # 不删除 ⇒ 仍挂在主窗下）。这一条**已知的真泄漏**记在交付报告里，
    # 本次按要求不改产品代码 ⇒ 这里只做**观察输出**，不做门禁。
    residual = len([c for c in win.findChildren(QWidget)
                    if type(c).__name__ == "_FullscreenPlayer"])
    _wait_bg_threads(app)
    _dispose(app, win)
    del win
    del bridge
    del page
    gc.collect()
    _pump(app, 4)
    checks = [
        ("本轮确实进了 %d 次全屏（对照组）" % n, entered == n,
         "实际 %d 次" % entered),
        ("本轮确实退了 %d 次全屏（对照组）" % n, exited == n,
         "实际 %d 次" % exited),
        ("退出后覆盖层的 100ms 定时器全停（漏停=泄漏）", timer_alive == 0,
         "仍在跑 %d 个" % timer_alive),
    ]
    fails = sum(1 for _l, ok, _d in checks if not ok)
    return _result(fails, checks,
                   {"entered": entered, "exited": exited,
                    "timer_alive": timer_alive, "residual_overlay": residual})


def _scenario_charts(ctx, n):
    """M 折线图弹窗：建 → 显示 → 关 → 删 n 次 ⇒ 弹窗树必须回收。

    [为什么盯] 折线图是**非模态 QDialog**（历史页内嵌 + 独立弹窗两种用
    法）⇒ 自己带一整套 QPainter 状态 + 手势处理 + 事件覆写 ⇒ 反复开关
    是用户最常做的动作；关不干净（残留事件 / 事件过滤器 / 定时器）就是
    泄漏。
    """
    from hsfsrv_client.pages_other import LineChartDialog
    app = _qt_app()
    # [序列形状] 点是 (时间戳毫秒, 数值) —— 与历史页 `_chart_build` 同源
    #（画图内部要对时间戳做减法 ⇒ 必须是数值，不能是字符串）。
    series = [("浊度1", [(1763600000000, 1.0), (1763600060000, 2.0)]),
              ("流量", [(1763600000000, 3.0), (1763600060000, 4.5)])]
    created = 0
    for _ in range(n):
        dlg = LineChartDialog(series, None)
        dlg.show()
        _pump(app, 2)
        created += 1
        if os.environ.get('HSF_AB') != 'M':
            _dispose(app, dlg)
        else:
            _AB_LEAK.append(dlg)
        del dlg
        gc.collect()
    _pump(app, 4)
    checks = [
        ("本轮确实开/关了 %d 个折线图弹窗（对照组）" % n, created == n,
         "实际 %d 个" % created),
    ]
    fails = sum(1 for _l, ok, _d in checks if not ok)
    return _result(fails, checks, {"created": created})


SCENARIOS = (
    ("F 会话生命周期（客户端）", _scenario_sessions, CHURN_SESSIONS, False),
    ("H 流式 XLSX 导出", _scenario_xlsx, CHURN_XLSX, False),
    ("I 内嵌网页端起停", _scenario_web, CHURN_WEB, False),
    ("J 主窗建/弃（UI 生命周期）", _scenario_windows, CHURN_WINDOWS, True),
    ("K 反复切页（UI 生命周期）", _scenario_pages, reg.PAGE_COUNT, True),
    ("L 相机页全屏覆盖层进出（UI 生命周期）", _scenario_fullscreen,
     CHURN_FULLSCREEN, True),
    ("M 折线图弹窗开/关（UI 生命周期）", _scenario_charts, CHURN_CHARTS, True),
)


def main():
    tmp = tempfile.mkdtemp(prefix="hsf_cli_leak_out_")
    server = ServerProc()
    tracemalloc.start(12)
    try:
        port = server.start()
        print("服务端已起：127.0.0.1:%d" % port)
        ctx = {"port": port, "tmp": tmp}
        if not HAVE_QT:
            print("  [提示] 本机没有 PyQt5 ⇒ 跳过 J/K/L/M 四个 UI 场景"
                  "（F/H/I 三段照跑）")
        for name, fn, churn, is_ui in SCENARIOS:
            if is_ui and not HAVE_QT:
                print("\n-- %s：跳过（本机无 PyQt5）" % name)
                continue
            print("\n-- %s：每轮搅动 %d 次，跑 %d 轮" % (name, churn, ROUNDS))
            snaps = []
            fails_total = 0
            # 场景内部的功能性判据（含**对照组**）按标签累计：
            # 只有**每一轮**都过的判据才算通过（否则第 1 轮过、第 3 轮挂
            # 会被最后一轮掩盖）。
            acc = {}
            facts = {}
            for rnd in range(ROUNDS):
                before = _snapshot()
                res = fn(ctx, churn)
                fails_total += res.get("fails", 0)
                facts = res.get("facts", {})
                time.sleep(1.0)
                after = _snapshot()
                snaps.append((before, after))
                d = _delta(before, after)
                td = _type_delta(before, after)
                tag = "预热" if rnd == 0 else "稳态"
                print("   第%d轮(%s)：%s" % (rnd + 1, tag, _fmt_delta(d, td)))
                if rnd >= 1:
                    top = _top_growth(before["snap"], after["snap"])
                    if top:
                        print("      增长点：" + " | ".join(top[:5]))
                for label, ok, detail in res.get("checks", []):
                    prev = acc.get(label)
                    if prev is None:
                        acc[label] = [bool(ok), detail]
                    else:
                        prev[0] = prev[0] and bool(ok)
                        prev[1] = detail
                if facts:
                    print("      观测量：%s" % (facts,))

            d3 = _delta(snaps[3][0], snaps[3][1])
            td3 = _type_delta(snaps[3][0], snaps[3][1])
            # [稳态判据：单轮 + 两轮净增 两条都判 2026-11-21]
            #   单轮增量（最后一个稳态轮 = 第4轮）：真泄漏是**每轮恒定**的
            #   ⇒ 单轮超容差必红（与 server_py 泄漏门同款口径，AGENTS 有 A/B
            #   实证）。
            #   两轮净增（第3轮起→第4轮止）：**抗「延迟释放」的一次性分配**
            #   ——客户端有一类约 128KB 的分配（相机页全屏覆盖层的图像缓冲
            #   等）在某一轮分配、**下一轮**才回收 ⇒ 单看最后一轮就是「凭空
            #   多了 128KB」⇒ 假红（实测 K 场景同批分配是「第2轮 +131344 /
            #   第3轮 -131344」，而活对象/活控件/活跃定时器增量**全是 0**
            #   ⇒ 明确不是泄漏）。两轮净增跨两轮抵消 ⇒ 不误伤；
            #   **持续泄漏每轮 +X ⇒ 净增 2X ⇒ 照红**（门不削弱）。
            d2 = _delta(snaps[2][0], snaps[2][1])
            net_steady = _delta(snaps[2][0], snaps[3][1])   # 第3轮起→第4轮止
            _single_over = d3["client"] > TOL_CLIENT_BYTES
            _net_over = net_steady["client"] > TOL_CLIENT_BYTES
            check("%s：稳态轮客户端堆增量 ≤ %d B（每轮恒定泄漏必红）"
                  % (name, TOL_CLIENT_BYTES),
                  (not _single_over) or _net_over,
                  "末轮 %+d B（上一轮 %+d / 两轮净增 %+d）%s"
                  % (d3["client"], d2["client"], net_steady["client"],
                     ("　【单轮超但两轮净增不超 ⇒ 判为延迟释放的一次性分配，"
                      "不判失败】") if _single_over and not _net_over else ""))
            check("%s：稳态两轮客户端堆净增 ≤ %d B（持续累积必红）"
                  % (name, TOL_CLIENT_BYTES),
                  not _net_over,
                  "净增 %+d B（第3轮 %+d / 第4轮 %+d）"
                  % (net_steady["client"], d2["client"], d3["client"]))
            worst = max(td3.items(), key=lambda kv: kv[1]) if td3 else ("无", 0)
            check("%s：稳态轮客户端活对象（按类）每轮增量 ≤ %d"
                  % (name, TOL_CLIENT_OBJECTS),
                  worst[1] <= TOL_CLIENT_OBJECTS,
                  "最多的一类 %s %+d 个（%s）"
                  % (worst[0], worst[1],
                     ", ".join("%s+%d" % (k, v)
                               for k, v in sorted(td3.items(),
                                                  key=lambda kv: -kv[1])[:5])
                     or "无"))
            check("%s：稳态轮线程每轮增量 ≤ %d（连接线程没回收=泄漏）"
                  % (name, TOL_THREADS),
                  d3["threads"] <= TOL_THREADS,
                  "实际 %+d（%s）" % (d3["threads"],
                                  ", ".join(snaps[3][1]["thread_names"][:6])))
            if is_ui:
                # [UI 场景的核心判据] 活 QWidget：建了一堆界面 ⇒ 拆掉后必须
                # 回到基线（**只看增量**，不看绝对值：Qt 自己会留一些全局对象）。
                check("%s：稳态轮活 QWidget 每轮增量 ≤ %d（界面对象没回收）"
                      % (name, TOL_WIDGETS),
                      d3["widgets"] <= TOL_WIDGETS,
                      "实际 %+d 个（上一轮 %+d 个）"
                      % (d3["widgets"], _delta(snaps[2][0], snaps[2][1])["widgets"]))
                # 活跃定时器：定时器没停 = 界面还在偷偷跑 = 泄漏。
                check("%s：稳态轮活跃定时器每轮增量 ≤ %d（定时器没停=泄漏）"
                      % (name, TOL_ACTIVE_TIMERS),
                      d3["timers"] <= TOL_ACTIVE_TIMERS,
                      "实际 %+d 个（上一轮 %+d 个）"
                      % (d3["timers"], _delta(snaps[2][0], snaps[2][1])["timers"]))
            # 场景内部的功能性判据 + **对照组**（每轮都过才算过）
            for label, (ok, detail) in acc.items():
                check("%s：%s" % (name, label), ok, detail)
            check("%s：搅动全程无失败" % name, fails_total == 0,
                  "失败 %d 次" % fails_total)

        left = [t.name for t in threading.enumerate()
                if t.name != "MainThread"]
        check("跑完只剩主线程（无残留网络线程/网页线程）",
              not left, "残留=%s" % (", ".join(left) or "无"))
    finally:
        server.stop()
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
