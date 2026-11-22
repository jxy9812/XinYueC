# -*- coding: utf-8 -*-
"""Python 服务端内存泄漏门禁（2026-11-14 用户口径「各种情况下都不能有
内存泄漏」）。

[为什么要有这一份]
==================
C 端服务端有 `verify/hsf_leak_gate.py`（判据＝堆计数 `XMemory_statistics_2`
的 systemBytes）。**Python 服务端这一侧一直只有 e2e 的「进程 RSS 增长有
界」**（`test_sim_e2e_auto.py` ③）——RSS 受页回收/共享页影响，**判不出
KB 级泄漏**（AGENTS 铁律：泄漏结论必须用堆计数，绝不看工作集）。⇒ Python
侧缺一把**堆级**的泄漏锁，本文件补上。

[判据口径]
==========
`tracemalloc`（Python 分配器的**精确**计数，不含 SQLite 的 C 侧页缓存
⇒ 不会把「落盘库变大」误判成泄漏）+ **分文件/分类型归因**：

    server_bytes  分配发生在 `server_py/hsfsrv/**` 的字节数 ⇒ 就是服务端
                  自己的堆（测试用的客户端在本文件里，分配归到
                  `server_py/tests/**` ⇒ 不混进来）。
    server_types  `hsfsrv.*` 里**各类活对象**的数量（按 gc 统计）⇒ 直指
                  「每会话多留一个 Session」这种真泄漏形状，且不受瞬时
                  对象噪声影响（gc.collect() 后瞬时对象已回收）。

每个场景跑 **3 轮**：第 1 轮吸收一次性初始化（模块导入、asyncio 内部
缓存、SQLite 语句缓存、**线程池预热**），第 2/3 轮是**稳态**。

判据（**只判最后一个稳态轮**＝第 3 轮）：

    · 服务端堆每轮增量 ≤ 容差（绝对值）——真泄漏是**每轮恒定**的，
      轮间差 Δ23−Δ12 = 0 ⇒ 用「轮间差」判据**抓不到** ⇒ 只能用稳态轮
      的绝对增量对小容差；用第 3 轮判是为了躲开第 1→2 轮的一次性成本
      （典型：asyncio 默认线程池懒建 8 条线程）。
    · 服务端活对象（按类型）每轮增量 ≤ 容差——「Session 多留一个」这类
      泄漏的直接指纹。
    · asyncio 任务每轮增量 = 0（多一个协程 = 没回收）。
    · 线程每轮增量 = 0（后台线程没退干净 = 泄漏源；本轮新引入的**落盘
      后台维护线程**正是这条的靶子）。

[场景矩阵]（每种都是现场会反复发生的动作）
============================================
    A 会话生命周期  连接→登录→订阅 9 主题→拉健康快照→断开 ×N
                    （历史真凶：每会话漏一个对象 ⇒ C 端真机踩过 40B/会话）
    B 自动模式生命周期 start/stop ×N，每 N/10 次一对急停/复位
                    （控制器状态机 + 阀/泵动作 + 落盘）
    C 查询与导出大流量 历史分页（带关键字 ⇒ 真扫描）/日志分页/计数 ×N
                    （BSON 编解码 + dict 构建 + 读连接 + 线程池）
    D 落盘容量维护  ring 裁剪反复触发（**后台维护线程** ⇒ 验线程/连接回收）
    E 告警事件流    阀超时 → 报警入库 + 推送（事件流最容易攒对象）

[怎么跑]
========
    python -I tests/test_leak_gate.py        # 独立跑
    python -I tests/run_all.py              # 全量（含本段）

服务端在本进程内起（假设备 + 真 XLink 协议栈 + 真 SQLite 落盘），客户端走
真 socket ⇒ 测的是**真链路**上的分配与回收。
"""

import asyncio
import concurrent.futures
import gc
import os
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
import tracemalloc
from collections import Counter

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ 输出重定向到文件时
# stdout 用 GBK ⇒ 打印「⇒」这类字符直接 UnicodeEncodeError **把门禁打断**
# （实测：退出码 0，看着像"跑完了"）。强制 UTF-8 + replace。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import FakeDigital, build_fake_app   # noqa: E402

PASS = 0
FAIL = 0

# ---------------------------------------------------------------------------
# 判据容差（**按实测噪声标定**，见文件末「容差标定记录」）
# ---------------------------------------------------------------------------
#: 稳态轮里服务端堆的**每轮**增量上限（字节）。实测各场景稳态轮的最大
#: 噪声是 +17KB（容量维护那轮的行数统计查询），留 1.4 倍余量。
TOL_SERVER_BYTES = 24 * 1024
#: 稳态轮里**任一类**服务端活对象的每轮增量上限（个）。实测噪声 ≤ 4
#: （瞬时对象在 gc.collect() 后已回收）；真泄漏（每会话多留 1 个 Session）
#: ⇒ 每轮 +40 ⇒ 判红。
TOL_SERVER_OBJECTS = 20
#: 稳态轮里 asyncio 任务的每轮增量上限（个）——**必须为 0**。
TOL_TASKS = 0
#: 稳态轮里线程数的每轮增量上限（个）——**必须为 0**。
TOL_THREADS = 0

#: 线程池线程名的前缀（**不算进泄漏判据**）：
#:   "asyncio_"           ——Python 3.12 的 asyncio 默认 executor 线程
#:   "ThreadPoolExecutor-" ——普通 ThreadPoolExecutor 的线程
#: 两者都是**池的懒建扩容**，不是泄漏（2026-11-14 实踩：只排后者时，慢
#: 查询场景每轮 +4~16 条 ⇒ 假红）。
_POOL_THREAD_PREFIXES = ("asyncio_", "ThreadPoolExecutor-")

#: 每个场景的轮数（第 1 轮预热，不判）。
ROUNDS = 3
#: 每个场景每轮的搅动规模。
CHURN_SESSIONS = 40          # A 场景：每轮 40 次完整会话
CHURN_AUTO = 30              # B 场景：每轮 30 次 start/stop
CHURN_QUERY = 30             # C 场景：每轮 30 次分页查询
CHURN_MAINT = 3              # D 场景：每轮 3 次容量维护触发
CHURN_ALARM = 6              # E 场景：每轮 6 次阀超时报警

#: 单轮订阅的主题（覆盖全部推送类别 ⇒ 检验每会话订阅账本的清理）。
TOPICS = (
    proto.TOPIC_VFD_STATUS,
    proto.TOPIC_AI_STATUS,
    proto.TOPIC_TEMPHUM_STATUS,
    proto.TOPIC_FLOWMETER_STATUS,
    proto.TOPIC_DEVICE_HEALTH,
    proto.TOPIC_DIGITAL_STATUS,
    proto.TOPIC_VALVE_STATUS,
    proto.TOPIC_PUMP_STATUS,
    proto.TOPIC_AUTO_ALARM,
)


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


# ============================================================================
# 最小协议客户端（不复用 client_py 的 XLinkClient：它的分配会混进归因；
#              这里自己写一份，分配归到本文件 ⇒ 不污染服务端口径）
# ============================================================================

class RawClient(object):
    """连接→登录→订阅→请求→断开。"""

    def __init__(self, host, port, timeout=6.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(0.3)
        self.req_id = 0
        self.pending = {}
        self.publishes = []
        self.buf = b""

    # ---- 收发 ----
    def _send(self, mclass, mcode, dtype, payload=b"", rid=0):
        head = proto.encode_header(len(payload), mclass, mcode, rid, dtype)
        self.sock.sendall(head + payload)

    def _recv_exact(self, n):
        while len(self.buf) < n:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return None
            if not chunk:
                return None
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _read_frame(self):
        head = self._recv_exact(proto.XLINK_HEADER_SIZE)
        if not head:
            return None
        hdr = proto.decode_header(head)
        if hdr is None:
            return None
        plen, mclass, mcode, rid, dtype = hdr
        payload = self._recv_exact(plen) if plen else b""
        return mclass, mcode, rid, dtype, payload

    def pump(self, seconds=0.0):
        deadline = time.time() + seconds
        while True:
            f = self._read_frame()
            if f:
                mclass, mcode, rid, dtype, payload = f
                if mclass == proto.MSGCLASS_PUBLISH and len(payload) >= 4:
                    topic = struct.unpack("<I", payload[:4])[0]
                    self.publishes.append(topic)
                elif mclass == proto.MSGCLASS_RESPONSE:
                    try:
                        body = (bson.decode(payload)
                                if dtype == proto.DATATYPE_BSON else {})
                    except ValueError:
                        body = {}
                    self.pending[rid] = (mcode, body)
            if time.time() >= deadline:
                break

    # ---- 握手与登录 ----
    def hello(self):
        self._send(proto.MSGCLASS_SESSION, proto.SESSION_HELLO,
                   proto.DATATYPE_RAW, proto.encode_hello(proto.CAP_AUTH))
        self.pump(0.3)

    def _wait_session(self, code, seconds=5.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            f = self._read_frame()
            if f:
                mclass, mcode, _rid, _dt, payload = f
                if mclass == proto.MSGCLASS_SESSION and mcode == code:
                    return payload
            else:
                time.sleep(0.01)
        return None

    def login(self, user, password):
        """登录（HMAC 挑战应答；环回偶发握手丢包 ⇒ 重试由调用方做）。"""
        import hashlib
        import hmac
        name = user.encode("utf-8")
        self._send(proto.MSGCLASS_SESSION, proto.SESSION_AUTH_REQUEST,
                   proto.DATATYPE_RAW, bytes([len(name)]) + name)
        ch = self._wait_session(proto.SESSION_AUTH_CHALLENGE)
        if not ch or len(ch) < 8:
            return False, "no challenge"
        method = ch[0]
        salt_len = ch[1]
        iterations = struct.unpack("<I", ch[4:8])[0]
        salt = ch[8:8 + salt_len]
        nonce = ch[8 + salt_len:8 + salt_len + proto.XLINK_AUTH_NONCE_BYTES]
        digest = hmac.new(password.encode("utf-8"), salt, hashlib.sha256).digest()
        for _ in range(iterations - 1):
            # [口径：key 恒为口令] 与服务端 `auth.derive_hash` 逐字节同式：
            #   H_0 = HMAC(口令, 盐)；H_{i+1} = HMAC(口令, H_i)
            # 写成 key=摘要（`hmac.new(digest, salt)`）⇒ 摘要不一致 ⇒
            # 服务端回「bad password」（2026-11-14 实踩）。
            digest = hmac.new(password.encode("utf-8"), digest,
                              hashlib.sha256).digest()
        resp = hmac.new(digest, nonce, hashlib.sha256).digest()
        self._send(proto.MSGCLASS_SESSION, proto.SESSION_AUTH_RESPONSE,
                   proto.DATATYPE_RAW,
                   bytes([method, 0]) + struct.pack("<H", len(resp)) + resp)
        res = self._wait_session(proto.SESSION_AUTH_RESULT)
        if not res or len(res) < 4:
            return False, "no auth result"
        status = res[0]
        text_len = struct.unpack("<H", res[2:4])[0]
        text = res[4:4 + text_len].decode("utf-8", "replace")
        if status != proto.AUTHSTATUS_OK:
            return False, "rejected: %s" % (text or status)
        return True, res[1]

    # ---- 业务 ----
    def subscribe(self, topic_id):
        self._send(proto.MSGCLASS_SUBSCRIBE, topic_id & 0xFFFF,
                   proto.DATATYPE_RAW, proto.pack_topic(topic_id))
        self.pump(0.02)

    def request(self, code, payload=b"", data_type=proto.DATATYPE_BSON,
                wait=5.0):
        self.req_id += 1
        rid = self.req_id
        self._send(proto.MSGCLASS_REQUEST, code, data_type, payload, rid)
        deadline = time.time() + wait
        while time.time() < deadline:
            self.pump(0.02)
            if rid in self.pending:
                return self.pending.pop(rid)
        return None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ============================================================================
# 分配量快照
# ============================================================================

def _norm(path):
    return path.replace("\\", "/")


def _server_type_counts():
    """服务端自有类的活对象数量（按类名）。"""
    counts = Counter()
    for obj in gc.get_objects():
        t = type(obj)
        mod = getattr(t, "__module__", "") or ""
        if mod == "hsfsrv" or mod.startswith("hsfsrv."):
            counts[t.__name__] += 1
    return counts


def _snapshot(loop):
    """取一份分配量快照（分文件归因 + 分类型对象数 + 任务/线程数）。"""
    gc.collect()
    gc.collect()          # 两轮：回收带环的临时对象
    cur, peak = tracemalloc.get_traced_memory()
    snap = tracemalloc.take_snapshot()
    server_bytes = 0
    test_bytes = 0
    other_bytes = 0
    for stat in snap.statistics("filename"):
        fn = _norm(stat.traceback[0].filename)
        if "/server_py/hsfsrv/" in fn:
            server_bytes += stat.size
        elif "/server_py/tests/" in fn:
            test_bytes += stat.size
        else:
            other_bytes += stat.size
    return {
        "server": server_bytes,
        "test": test_bytes,
        "other": other_bytes,
        "total": cur,
        "peak": peak,
        "objects": len(gc.get_objects()),
        "types": _server_type_counts(),
        "tasks": len(asyncio.all_tasks(loop)),
        # [线程口径：不算 asyncio 默认线程池] `asyncio.to_thread`（慢查询
        # 挪出事件循环）用的是 loop 的默认 ThreadPoolExecutor ⇒ 它会**懒建**
        # 工作线程（实测第 2 轮 +6~16 条），那是**池的正常扩容**不是泄漏
        # ⇒ 判据只算**我们自己的**线程（后台维护线程之类）。
        # [线程名的两个前缀都要排] Python 3.12 里 asyncio 自建的默认
        # executor 线程叫 **"asyncio_N"**（不是 "ThreadPoolExecutor-N_M"
        # ——那是普通 ThreadPoolExecutor 的名字）⇒ 只排后者会把池扩容
        # 误判成泄漏（2026-11-14 实踩：假红 +4~16 条）。
        "threads": sum(1 for t in threading.enumerate()
                       if not _POOL_THREAD_PREFIXES
                       or not t.name.startswith(_POOL_THREAD_PREFIXES)),
        "pool_threads": sum(1 for t in threading.enumerate()
                            if t.name.startswith(_POOL_THREAD_PREFIXES)),
        "thread_names": [t.name for t in threading.enumerate()],
        "snap": snap,
    }


def _delta(a, b):
    """b - a（逐字段；snap/types 不参与 ⇒ types 单独比）。"""
    return {k: b[k] - a[k] for k in
            ("server", "test", "other", "total", "objects", "tasks", "threads")}


def _type_delta(a, b):
    """按类的活对象增量（只列增长的）。"""
    out = {}
    for name, cnt in b["types"].items():
        d = cnt - a["types"].get(name, 0)
        if d > 0:
            out[name] = d
    return out


def _fmt_delta(d, td):
    return ("服务端 %+d B / 测试侧 %+d B / 其他 %+d B / 对象 %+d / "
            "任务 %+d / 线程 %+d / 类增量 %s"
            % (d["server"], d["test"], d["other"], d["objects"],
               d["tasks"], d["threads"],
               (", ".join("%s+%d" % (k, v) for k, v in
                          sorted(td.items(), key=lambda kv: -kv[1])[:4]))
               or "无"))


def _top_growth(prev_snap, cur_snap, limit=6):
    """两个快照之间**增长最多**的分配点（泄漏归因用）。"""
    out = []
    try:
        stats = cur_snap.compare_to(prev_snap, "traceback")
    except Exception:                                # noqa: BLE001
        return out
    for stat in stats[:limit]:
        if stat.size_diff <= 0:
            continue
        frames = stat.traceback
        where = ""
        for fr in frames:
            if "/server_py/hsfsrv/" in _norm(fr.filename):
                where = "%s:%d" % (os.path.basename(fr.filename), fr.lineno)
                break
        if not where:
            for fr in frames:
                where = "%s:%d" % (os.path.basename(fr.filename), fr.lineno)
                break
        out.append("%s %+d B ×%d" % (where, stat.size_diff, stat.count_diff))
    return out


# ============================================================================
# 服务端夹具（进程内：假设备 + 真 XLink + 真 SQLite）
# ============================================================================

class LeakDigital(FakeDigital):
    """假数字量：**模拟阀的行程**（写 DO ⇒ 到位反馈跟着变）。

    [为什么要模拟行程] 假设备的 `poll` 只刷新 `last_ok_ms`，**不改 DI**
    ⇒ 阀永远「不到位」⇒ 自动序列每次都在阀相超时报警 ⇒ 场景 B 量到的
    不是「正常自动模式」而是「一直报警」⇒ 泄漏形状被扭曲。
    这里让被驱动的阀**瞬时到位**（真机是 9s 行程），并留**第 6 台阀永
    不到位**给场景 E 制造超时报警。

    [位序] 现场订正后的口径：**开到位 = DI 2i，关到位 = DI 2i+1**
    （config.py 的 `openDi=i*2 / closeDi=i*2+1` 同款）。
    """

    #: 0 基的第 6 台阀（cfg 里的 valve6）——**永不到位**（场景 E 用）。
    STUCK_VALVE = 5

    async def write_do(self, index, on):
        ok, err = await FakeDigital.write_do(self, index, on)
        if ok and 0 <= index <= 5 and index != self.STUCK_VALVE:
            self.di[2 * index] = 1 if on else 0
            self.di[2 * index + 1] = 0 if on else 1
        return ok, err


async def _spin_server(basedir, log, valve_move_timeout_ms=400):
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    # [同账并发] 多会话搅动要同时在线 ⇒ 必须放开同账互斥。
    cfg.login_allow_concurrent = True
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)
    # ---- 补装三件真 Bank（阀/泵/按钮组），否则自动序列起不来 ----
    from hsfsrv import buttons as buttons_mod
    from hsfsrv import pumps as pumps_mod
    from hsfsrv import valves as valves_mod
    cfg.valve_move_timeout_ms = valve_move_timeout_ms
    cfg.button_debounce_ms = 1
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, lambda _m: None,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    business.pump_bank = pumps_mod.PumpBank(
        business.digital, cfg.pump_items, lambda _m: None)
    business.button_station = buttons_mod.ButtonStation(
        business.auto_ctx, cfg.button_items,
        debounce_ms=cfg.button_debounce_ms, log=lambda _m: None)
    business.button_station.on_state_change = business._on_button_state
    business.button_station.set_running(False)
    d0 = business.digital[0]
    # 阀 1~5：先放「关到位」；第 6 台（index 5）放「都不在位」⇒ 永不到位。
    for n in range(6):
        d0.di[2 * n] = 0
        d0.di[2 * n + 1] = 1
    d0.di[2 * LeakDigital.STUCK_VALVE] = 0
    d0.di[2 * LeakDigital.STUCK_VALVE + 1] = 0
    d0.di[cfg.button_items["estop_di"]] = 1
    d0.di[cfg.button_items.get("stop_di", 14)] = 1
    # 换成本文件自己的假数字量（带阀行程模拟）。
    business.digital[0] = LeakDigital(business.digital[0].name,
                                      log=log,
                                      channels=business.digital[0].channels)
    business.digital[0].di = list(d0.di)
    business.digital[0].do = list(d0.do)
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, lambda _m: None,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    business.pump_bank = pumps_mod.PumpBank(
        business.digital, cfg.pump_items, lambda _m: None)
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


async def _feed_devices(business):
    """**喂假设备**（按拍 poll）——没有它阀反馈永远「不新鲜」。

    [照抄 test_auto_modes 的 feeder] 阀反馈的新鲜度闸认 `module.last_ok_ms`
    （设备 poll 才刷新）⇒ 不喂 ⇒ 6 路阀全部「数据不新鲜」⇒ 阀相直接超时
    ⇒ 量到的形状不对。
    """
    while True:
        now = int(time.monotonic() * 1000)
        for dev in business.digital:
            try:
                await dev.poll(now)
            except Exception:                       # noqa: BLE001
                pass
        try:
            await business.vfd.poll(now)
        except Exception:                           # noqa: BLE001
            pass
        await asyncio.sleep(0.05)


def _connect(port, retries=3):
    """连一次并登录（环回偶发握手丢包 ⇒ 重试）。"""
    last = ""
    for _ in range(retries):
        c = RawClient("127.0.0.1", port)
        c.hello()
        ok, err = c.login("root", "root")
        if ok:
            return c, ""
        last = err
        c.close()
        time.sleep(0.2)
    raise RuntimeError("登录失败：%s" % last)


def _on_loop(loop, fn, *args, **kwargs):
    """在**事件循环线程**里跑 fn 并等结果。

    [为什么必须这样] sqlite3 的连接对象是**线程绑定**的（写连接默认
    check_same_thread=True）⇒ 跨线程写会抛 ProgrammingError。

    [场景设计的要害] **搅动必须在工作线程**：客户端是阻塞式 socket 读写，
    若在事件循环线程里做 ⇒ 服务端根本得不到调度（实测：登录全部超时、
    连接积压把 accept 背压顶爆 ⇒ 报「连接被拒绝」，看着像服务端崩了，
    其实是自己的测试把服务端饿死了——2026-11-14 实踩）。
    """
    fut = concurrent.futures.Future()

    def _runner():
        try:
            fut.set_result(fn(*args, **kwargs))
        except Exception as exc:                      # noqa: BLE001
            fut.set_exception(exc)

    loop.call_soon_threadsafe(_runner)
    return fut.result(timeout=60.0)


# ============================================================================
# 五个场景（每个都是「一次搅动」；返回失败次数供断言）
# ============================================================================

def _scenario_sessions(ctx, n):
    """A 会话生命周期：连接→登录→订阅 9 主题→健康快照→断开。"""
    fails = 0
    for _ in range(n):
        c = RawClient("127.0.0.1", ctx["port"])
        try:
            c.hello()
            ok, _err = c.login("root", "root")
            if not ok:
                fails += 1
                continue
            for topic in TOPICS:
                c.subscribe(topic)
            if c.request(proto.CMD_DEVICE_HEALTH, b"") is None:
                fails += 1
        finally:
            c.close()
    return fails


#: [自检开关 2026-11-14] HSF_LEAK_INJECT=1 ⇒ 故意让服务端每建一个会话就
#: 多留一个 Session 对象（**服务端侧**泄漏）。用途：**验证这道门真的能抓
#: 漏**——不会红的门等于没有门（AGENTS 纪律：A/B 实测）。
#: 实测：注入后「稳态轮服务端活对象（按类）每轮增量」判红（Session +40/轮）。
_INJECTED = []


def _install_leak_injection():
    orig_init = xlink.Session.__init__

    def _leaky_init(self, *args, **kwargs):
        orig_init(self, *args, **kwargs)
        _INJECTED.append(self)

    xlink.Session.__init__ = _leaky_init


def _scenario_auto(ctx, n):
    """B 自动模式生命周期：start/stop ×N，每 N/10 次一对急停/复位。"""
    c, _ = _connect(ctx["port"])
    fails = 0
    try:
        c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
        c.request(proto.CMD_CONFIG_SET, bson.encode({
            "section": proto.CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve1", "inletPump"],
                                  "stirRpm": 720, "stirReverse": False}}}))
        for i in range(n):
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": 1, proto.KEY_AUTO_MODE: "produce"}))
            if r is None:
                fails += 1
            if i % 10 == 0:
                c.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": 3}))
                c.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": 4}))
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": 2}))
            if r is None:
                fails += 1
    finally:
        c.close()
    return fails


def _scenario_query(ctx, n):
    """C 查询与导出大流量：历史分页（带关键字）/日志分页/计数。"""
    c, _ = _connect(ctx["port"])
    fails = 0
    try:
        for _ in range(n):
            r = c.request(proto.CMD_HISTORY_PAGE, bson.encode(
                {"offset": 0, "limit": 50, "kw": "泄漏"}))
            if r is None:
                fails += 1
            r = c.request(proto.CMD_LOG_PAGE, bson.encode(
                {"offset": 0, "limit": 50, "kw": "泄漏"}))
            if r is None:
                fails += 1
    finally:
        c.close()
    return fails


def _scenario_maintenance(ctx, n):
    """D 落盘容量维护：反复触发 ring 裁剪（后台线程动）。

    [写库必须回事件循环线程] store 的**写连接**是线程绑定的
    （sqlite3 默认 check_same_thread=True）⇒ 从工作线程直接写会抛
    "SQLite objects created in a thread can only be used in that same
    thread" ⇒ 用 `_on_loop` 把写操作投递回事件循环线程（读连接是
    check_same_thread=False，后台维护线程用的就是它）。
    """
    store = ctx["store"]
    loop = ctx["loop"]
    fails = 0
    try:
        for i in range(n):
            _on_loop(loop, store.configure, max_kb=64, full_policy="ring")
            _on_loop(loop, store.insert_log, "触发容量维护 %d" % i,
                     category="conn")
            # 给后台维护线程一点时间把裁剪做完（分块删）。
            deadline = time.time() + 5.0
            while time.time() < deadline:
                time.sleep(0.1)
                if _on_loop(loop, store.row_count, "history") == 0:
                    break
        _on_loop(loop, store.configure, max_kb=0, full_policy="ring")
    except Exception as exc:                          # noqa: BLE001
        fails += 1
        print("     [异常] 容量维护场景：%s: %s" % (type(exc).__name__, exc))
    return fails


def _scenario_alarm(ctx, n):
    """E 告警事件流：制造阀超时 → 报警入库 + 推送。

    [用第 6 台阀制造超时] LeakDigital.STUCK_VALVE（cfg 的 valve6）永不到位
    ⇒ 阀相超时 ⇒ 报警。阀 1~5 有行程模拟 ⇒ 场景 B 走的是**正常**路径。
    """
    c, _ = _connect(ctx["port"])
    fails = 0
    try:
        c.subscribe(proto.TOPIC_AUTO_ALARM)
        c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
        c.request(proto.CMD_CONFIG_SET, bson.encode({
            "section": proto.CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve6", "inletPump"],
                                  "stirRpm": 720, "stirReverse": False}}}))
        for _ in range(n):
            # [先复位] 上一轮的报警锁存会拒启动 ⇒ 不复位就量不到下一轮。
            c.request(proto.CMD_AUTO_CONTROL, bson.encode({"op": 4}))
            before = len(c.publishes)
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": 1, proto.KEY_AUTO_MODE: "produce"}))
            started = bool(r and r[1].get("ok") is True)
            if not started:
                fails += 1
                continue
            # 等报警推送（阀超时 = 400ms）+ 落盘。
            deadline = time.time() + 6.0
            got = False
            while time.time() < deadline:
                c.pump(0.05)
                if len(c.publishes) > before:
                    got = True
                    break
            if not got:
                fails += 1
            c.publishes.clear()
    finally:
        c.close()
    return fails


SCENARIOS = (
    ("A 会话生命周期", _scenario_sessions, CHURN_SESSIONS),
    ("B 自动模式生命周期", _scenario_auto, CHURN_AUTO),
    ("C 查询与导出大流量", _scenario_query, CHURN_QUERY),
    ("D 落盘容量维护", _scenario_maintenance, CHURN_MAINT),
    ("E 告警事件流", _scenario_alarm, CHURN_ALARM),
)


# ============================================================================
# 主流程
# ============================================================================

async def _run_case(basedir, log):
    if os.environ.get("HSF_LEAK_INJECT") == "1":
        _install_leak_injection()
    business, server = await _spin_server(basedir, log)
    loop = asyncio.get_running_loop()
    ctx = {"port": server.port, "business": business, "store": business.store,
           "loop": loop}

    # 喂假设备（否则阀反馈不新鲜 ⇒ 自动序列秒超时）。
    feeder = asyncio.ensure_future(_feed_devices(business))

    # 造一批历史行（让 C 场景的查询是**真扫描**，不是空转）。
    store = business.store
    if store.db_path is None:
        store.open_for("")
    store.configure(max_kb=0, full_policy="ring")
    base_ts = int(time.time() * 1000) - 20000 * 1000
    for i in range(3000):
        store.insert_history({
            "ts": base_ts + i * 1000, "mode": "手动",
            "vfd_state": "停止", "freq_hz": 12.5, "current_a": 3.2,
            "rpm": 360.0, "turb1": 2.29, "turb2": 0.045,
            "flow": 1.25, "flow_unit": "m³/h", "flow_total": 100.5,
            "total_unit": "m³", "press_mpa": 0.18, "press_note": "正常",
            "temp": 22.5, "hum": 55.0, "di": 0, "do": 0, "water_temp": 19.3,
        })

    # ---- 搅动跑**工作线程**（客户端是阻塞式 socket ⇒ 不能占着事件循环）----
    def _worker():
        for name, fn, churn in SCENARIOS:
            print("\n-- %s：每轮搅动 %d 次，跑 %d 轮"
                  % (name, churn, ROUNDS))
            snaps = []
            fails_total = 0
            for rnd in range(ROUNDS):
                before = _snapshot(loop)
                fails = fn(ctx, churn)
                fails_total += fails
                # [让服务端把断开的会话回收掉] 会话清理是异步的 ⇒ 等一拍再量。
                time.sleep(1.0)
                after = _snapshot(loop)
                snaps.append((before, after))
                d = _delta(before, after)
                td = _type_delta(before, after)
                tag = "预热" if rnd == 0 else "稳态"
                print("   第%d轮(%s)：%s" % (rnd + 1, tag, _fmt_delta(d, td)))
                if rnd >= 1:
                    top = _top_growth(before["snap"], after["snap"])
                    if top:
                        print("      增长点：" + " | ".join(top[:5]))

            # ---- 判据：**只判最后一个稳态轮**（第 3 轮）----
            # [为什么只判最后一轮] 第 1→2 轮含一次性成本（典型：asyncio
            # 默认线程池懒建 8 条线程、SQLite 语句缓存），判它必假红；
            # 真泄漏是**每轮恒定**的 ⇒ 在第 3 轮的绝对增量里照样显形。
            d3 = _delta(snaps[2][0], snaps[2][1])
            td3 = _type_delta(snaps[2][0], snaps[2][1])
            check("%s：稳态轮服务端堆每轮增量 ≤ %d B"
                  % (name, TOL_SERVER_BYTES),
                  d3["server"] <= TOL_SERVER_BYTES,
                  "实际 %+d B（第2轮 %+d B）"
                  % (d3["server"],
                     _delta(snaps[1][0], snaps[1][1])["server"]))
            worst_type = ""
            worst = 0
            for tname, tdelta in td3.items():
                if tdelta > worst:
                    worst = tdelta
                    worst_type = tname
            check("%s：稳态轮服务端活对象（按类）每轮增量 ≤ %d"
                  % (name, TOL_SERVER_OBJECTS),
                  worst <= TOL_SERVER_OBJECTS,
                  "最多的一类 %s %+d 个（各类：%s）"
                  % (worst_type or "无", worst,
                     ", ".join("%s+%d" % (k, v)
                               for k, v in sorted(td3.items(),
                                                  key=lambda kv: -kv[1])[:5])
                     or "无"))
            check("%s：稳态轮 asyncio 任务每轮增量 ≤ %d（协程没回收=泄漏）"
                  % (name, TOL_TASKS),
                  d3["tasks"] <= TOL_TASKS, "实际 %+d" % d3["tasks"])
            check("%s：稳态轮线程每轮增量 ≤ %d（后台线程没退干净=泄漏）"
                  % (name, TOL_THREADS),
                  d3["threads"] <= TOL_THREADS,
                  "实际 %+d（当前线程名：%s）"
                  % (d3["threads"], ", ".join(snaps[2][1].get("thread_names", []))))
            check("%s：搅动全程无失败请求" % name, fails_total == 0,
                  "失败 %d 次" % fails_total)
            # [会话回收是无噪声的硬指标] 搅动 N 次连接/断开后，服务端
            # 活会话数必须回到 0（每会话多留一个 = 最经典的泄漏形状）。
            sc = _on_loop(loop, server.session_count)
            check("%s：搅动后服务端活会话数归零（会话没回收=泄漏）" % name,
                  sc == 0, "活会话=%d" % sc)

    errors = []

    def _worker_safe():
        try:
            _worker()
        except Exception as exc:                      # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    th = threading.Thread(target=_worker_safe, name="leak-churn")
    th.start()
    # [事件循环必须照常转] 工作线程跑搅动期间，主线程只让出控制权 ⇒
    # 服务端的事件循环继续跑（否则测的是"假死"）。
    while th.is_alive():
        await asyncio.sleep(0.02)
    th.join(600)
    for err in errors:
        check("泄漏场景执行", False, err)

    # ---- 收尾：停服务端 + 关库（关库才会停后台维护线程）----
    feeder.cancel()
    try:
        await server.stop()
    except Exception:                                # noqa: BLE001
        pass
    try:
        # [与真实退出路径同款] `hsfsrv/__main__.py` 的退出序列调
        # `business.stop_storage()` ⇒ `store.close()` ⇒ 停后台维护线程。
        await business.stop_storage()
    except Exception as exc:                          # noqa: BLE001
        check("关库（停后台维护线程）", False, "%s: %s" % (type(exc).__name__, exc))
    time.sleep(1.0)
    names = sorted(t.name for t in threading.enumerate())
    check("关库后后台维护线程已退出（无 hsf-store-maintenance 线程）",
          not any(n.startswith("hsf-store-maintenance") for n in names),
          "线程名=%s" % ", ".join(names))
    # [线程总数] asyncio 的默认线程池（`asyncio.to_thread` 跑慢查询用）
    # 会在关库后仍然存活（它是 loop 的默认 executor，由 asyncio.run() 的
    # 收尾负责 shutdown）⇒ 不能用「线程数 == 1」判 ⇒ 判「没有我们自己的
    # 后台线程残留」+ 总数有界。真正的「跑完只剩主线程」在 main() 里
    # asyncio.run() 返回后再查一次。
    check("关库后无本项目后台线程残留（线程总数有界）",
          len(names) <= 40, "线程数=%d（%s）" % (len(names),
                                              ", ".join(names[:8])))


def main():
    base = tempfile.mkdtemp(prefix="hsf_leak_")
    log = lambda m: None
    tracemalloc.start(12)          # 12 帧栈 ⇒ 归因能定位到具体行
    try:
        asyncio.run(_run_case(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    # [跑完只剩主线程] asyncio.run() 收尾会 shutdown 默认线程池 ⇒
    # 这时还剩别的线程 = 真泄漏（守护线程没退、监听线程没关…）。
    left = [t.name for t in threading.enumerate()
            if t.name != "MainThread"]
    check("进程收尾后只剩主线程（无守护线程/监听线程残留）",
          not left, "残留=%s" % (", ".join(left) or "无"))
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
