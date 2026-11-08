# -*- coding: utf-8 -*-
"""变频器全操作测试：逐个 XVfdOperation + 全部闸门/边界。

覆盖：
    A. 动作类：启动(正/反)、停机、自由停车、快速停机、故障复位、点动(正/反)、
              点动释放
    B. 给定类：设频率（含钳位、负给定）、设转速(不支持)、PID(不支持)、
              多段速(不支持)
    C. 闸门：admin 只读被拒 / root 放行 / 自动模式最前置拒绝 / 未登录拒绝
    D. 边界：未知 op / 缺 op / 非法 BSON / 越界给定
    E. 点动看门狗：心跳丢失后自动停机（600ms）
    F. 点动释放：立即停机不等看门狗
    G. 命令字验证：设备侧实际收到的命令字必须正确（不是"受理了就算过"）
    H. 运行中换向：反向 START 减速过零再反向加速（2026-11-06 ACC/DEC 口径）

用法：
    python tests/test_vfd_ops.py [--port 6199]
需要先起 tests/harness.py（假设备服务端）。
"""

import argparse
import hashlib
import hmac
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import bson, proto            # noqa: E402

PASS = 0
FAIL = 0
FAILED = []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s %s" % (name, detail))


def section(title):
    print("\n=== %s ===" % title)


# ============================================================================
#                          最小 XLink 客户端
# ============================================================================

class Client(object):
    def __init__(self, host, port, timeout=6.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(0.3)
        self.req_id = 0
        self.pending = {}
        self.publishes = []
        self.buf = b""

    # ---- 收发 ----
    def _send(self, msg_class, msg_code, data_type, payload=b"", req_id=0):
        head = proto.encode_header(len(payload), msg_class, msg_code, req_id,
                                   data_type)
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
                    doc = bson.decode(payload[4:]) if dtype == proto.DATATYPE_BSON else {}
                    self.publishes.append((topic, doc, time.time()))
                elif mclass == proto.MSGCLASS_RESPONSE:
                    body = bson.decode(payload) if dtype == proto.DATATYPE_BSON else {}
                    # 非法 BSON 也要记下来（否则测试会把"回了个垃圾"当成无应答）
                    try:
                        body = bson.decode(payload) if dtype == proto.DATATYPE_BSON else {}
                    except ValueError:
                        body = {"_raw": payload.hex()}
                    self.pending[rid] = (mcode, body)
            if time.time() >= deadline:
                break

    # ---- 握手与登录 ----
    def hello(self):
        self._send(proto.MSGCLASS_SESSION, proto.SESSION_HELLO,
                   proto.DATATYPE_RAW, proto.encode_hello(proto.CAP_AUTH))
        self.pump(0.3)

    def _wait_session(self, code, seconds=3.0):
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
            digest = hmac.new(password.encode("utf-8"), digest, hashlib.sha256).digest()
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
    def request(self, code, payload=b"", data_type=proto.DATATYPE_BSON, wait=3.0):
        self.req_id += 1
        rid = self.req_id
        self._send(proto.MSGCLASS_REQUEST, code, data_type, payload, rid)
        deadline = time.time() + wait
        while time.time() < deadline:
            self.pump(0.08)
            if rid in self.pending:
                return self.pending.pop(rid)
        return None

    def control(self, **kw):
        """下发一条控制命令，返回 (ok, err)。"""
        r = self.request(proto.CMD_VFD_CONTROL, bson.encode(kw))
        if r is None:
            return False, "no response"
        return bool(r[1].get("ok")), r[1].get("err", "")

    def snapshot(self):
        r = self.request(proto.CMD_VFD_SNAPSHOT, b"")
        return r[1] if r else {}

    def set_mode(self, mode):
        r = self.request(proto.CMD_RUN_MODE, bson.encode({"mode": mode}))
        return bool(r) and r[1].get("ok") is True

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def login_as(host, port, user, password, retries=12):
    """登录（root 有单会话策略，需等上一个会话被回收）。"""
    last = None
    for _ in range(retries):
        c = Client(host, port)
        c.hello()
        ok, info = c.login(user, password)
        if ok:
            return c, info
        last = info
        c.close()
        time.sleep(0.4)
    raise RuntimeError("登录失败：%s" % (last,))


def wait_run_state(c, want, timeout=15.0, poll=0.15):
    """轮询快照直到 runState 进入 want 集合，返回最后一拍快照。

    [为什么不能固定 sleep] 2026-11-06 起虚拟变频器的启停斜率按 ACC/DEC
    加减速时间参数走（缺省各 10.0s 满量程 ⇒ 5Hz/s）：25Hz 减速停机要
    ~5s，固定 sleep(0.4) 只会停在斜坡中途误报失败。
    """
    want = set(want)
    deadline = time.time() + timeout
    snap = {}
    while time.time() < deadline:
        snap = c.snapshot()
        if snap.get("runState") in want:
            break
        time.sleep(poll)
    return snap


# ============================================================================
#                           测试用例
# ============================================================================

def case_role_gate(host, port):
    """A 组前置：角色闸门（admin 只读 / root 可写）。"""
    section("A. 角色闸门")
    c, role = login_as(host, port, "admin", "admin")
    check("admin 登录成功", role == proto.ROLE_USER, "role=%s" % role)
    ok, err = c.control(op=proto.OP_START)
    check("admin 启动被拒", not ok, "err=%r" % err)
    check("拒绝原因含'权限不足'", "权限不足" in (err or ""), "err=%r" % err)
    ok, err = c.control(op=proto.OP_SET_FREQUENCY, hz=20.0)
    check("admin 设频被拒", not ok, "err=%r" % err)
    # 只读角色仍应能读快照与遥测
    snap = c.snapshot()
    check("admin 可读快照", bool(snap), "keys=%d" % len(snap))
    c.close()
    time.sleep(0.8)


def case_all_operations(host, port):
    """B 组：逐个动作/给定（含不支持项的明确拒绝）。"""
    section("B. 全操作覆盖（root）")
    c, role = login_as(host, port, "root", "root")
    check("root 登录成功", role == proto.ROLE_ADMIN, "role=%s" % role)

    # ---- 给定类 ----
    #
    # [2026-11-05 起必须先启动] 停机状态下禁止下发给定（给定是运行参数，
    # 不是可预置配置；否则会出现"停机 + 25Hz + 720rpm"这种自相矛盾的界面）。
    # 故本组先启动，再验证给定/钳位/负给定。停机被拒的规则由
    # case_gates 里的"停机时下发给定被明确拒绝"单独锁住。
    ok, err = c.control(op=proto.OP_START, rev=False)
    check("给定组前置启动", ok, "" if ok else err)
    time.sleep(0.5)

    ok, err = c.control(op=proto.OP_SET_FREQUENCY, hz=25.0)
    check("设频 25Hz 受理", ok, "" if ok else err)
    snap = c.snapshot()
    check("设频回读 25Hz", abs(float(snap.get("setFreqHz", -1)) - 25.0) < 0.1,
          "setFreq=%s" % snap.get("setFreqHz"))

    # 钳位：超 100% 应被钳到满量程而不是整条命令失败
    ok, err = c.control(op=proto.OP_SET_FREQUENCY, hz=999.0)
    ok2 = ok
    snap = c.snapshot()
    hz_max = float(snap.get("setFreqHz", -1))
    check("设频超量程被钳位（非失败）", ok2, "err=%r" % err)
    check("钳位后 = 最大频率", abs(hz_max - 50.0) < 0.6, "setFreq=%.2f" % hz_max)

    # 负给定：0x1000 是 int16 补码，不能被当成无符号钳成 0
    ok, err = c.control(op=proto.OP_SET_FREQUENCY, hz=-20.0)
    snap = c.snapshot()
    hz_neg = float(snap.get("setFreqHz", 0))
    check("负给定受理", ok, "" if ok else err)
    check("负给定回读为负（补码未丢符号）", hz_neg < -0.5,
          "setFreq=%.2f" % hz_neg)

    # 恢复到正给定
    c.control(op=proto.OP_SET_FREQUENCY, hz=15.0)

    # ---- 不支持的能力必须明确拒绝（不是静默 ok） ----
    #
    # [注] OP_SET_SPEED（转速给定）**是支持的**：驱动层按额定点反解成频率
    # 再下发（0x1000 只接受百分比）。运行中应受理。
    ok, err = c.control(op=proto.OP_SET_SPEED, rpm=720.0)
    check("设转速被受理（换算成频率下发）", ok, "" if ok else err)
    ok, err = c.control(op=proto.OP_SET_PID_TARGET, val=50.0)
    check("PID 给定被明确拒绝", not ok, "err=%r" % err)
    ok, err = c.control(op=proto.OP_SELECT_PRESET_SPEED, idx=3)
    check("多段速被明确拒绝", not ok, "err=%r" % err)

    # ---- 动作类 ----
    ok, err = c.control(op=proto.OP_START, rev=False)
    check("正转启动受理", ok, "" if ok else err)
    time.sleep(0.4)
    snap = c.snapshot()
    check("启动后状态为运行", snap.get("runState") in
          (proto.RUN_RUNNING_FORWARD, proto.RUN_RUNNING_REVERSE),
          "runState=%s" % snap.get("runState"))
    check("正转 running=true", snap.get("running") is True,
          "running=%s" % snap.get("running"))
    check("正转 reverse=false", snap.get("reverse") is False,
          "reverse=%s" % snap.get("reverse"))

    ok, err = c.control(op=proto.OP_STOP)
    check("减速停机受理", ok, "" if ok else err)
    # [ACC/DEC 斜坡] 25Hz 减速到 0 要 ~5s，等到位再断言（见 wait_run_state）。
    snap = wait_run_state(c, (proto.RUN_STOPPED,))
    check("停机后状态为 Stopped", snap.get("runState") == proto.RUN_STOPPED,
          "runState=%s" % snap.get("runState"))

    ok, err = c.control(op=proto.OP_START, rev=True)
    check("反转启动受理", ok, "" if ok else err)
    time.sleep(0.4)
    snap = c.snapshot()
    check("反转 running=true", snap.get("running") is True,
          "running=%s" % snap.get("running"))
    check("反转 reverse=true", snap.get("reverse") is True,
          "reverse=%s" % snap.get("reverse"))

    ok, err = c.control(op=proto.OP_FREE_STOP)
    check("自由停车受理", ok, "" if ok else err)
    snap = wait_run_state(c, (proto.RUN_STOPPED,))
    check("自由停车后停止", snap.get("runState") == proto.RUN_STOPPED,
          "runState=%s" % snap.get("runState"))

    ok, err = c.control(op=proto.OP_QUICK_STOP)
    check("快速停机受理", ok, "" if ok else err)

    ok, err = c.control(op=proto.OP_RESET_FAULT)
    check("故障复位受理", ok, "" if ok else err)

    # ---- 点动 ----
    ok, err = c.control(op=proto.OP_JOG, rev=False)
    check("正转点动受理", ok, "" if ok else err)
    time.sleep(0.3)
    snap = c.snapshot()
    check("点动后仍在运行态", snap.get("running") is True,
          "running=%s runState=%s" % (snap.get("running"),
                                      snap.get("runState")))

    ok, err = c.control(op=proto.OP_JOG, rev=True)
    check("反转点动受理", ok, "" if ok else err)

    # 点动释放必须立刻停（不等看门狗）。释放走的是减速停机（op=101 →
    # XVfdDevice_stop），从点动频率按 DEC 斜坡归零——等到位再断言。
    ok, err = c.control(op=proto.OP_JOG_RELEASE)
    check("点动释放受理", ok, "" if ok else err)
    snap = wait_run_state(c, (proto.RUN_STOPPED,))
    check("点动释放后停止（斜坡到位）",
          snap.get("runState") == proto.RUN_STOPPED,
          "runState=%s" % snap.get("runState"))

    # ---- 运行中禁点动（2026-11-06 口径）----
    # 运行中 OP_JOG 拒绝（点动会瞬间换向）；**点动保持中**的心跳例外放行
    # （上面"反转点动受理"即点动保持中的心跳帧，已隐式覆盖）。
    ok, err = c.control(op=proto.OP_START, rev=False)
    check("运行中禁点动前置启动", ok, "" if ok else err)
    time.sleep(0.3)
    ok, err = c.control(op=proto.OP_JOG, rev=False)
    check("运行中点动被拒", not ok, "err=%s" % (err or "")[:60])
    check("拒绝原因指向先停机", "运行中" in (err or ""), err or "")
    c.control(op=proto.OP_FREE_STOP)
    wait_run_state(c, (proto.RUN_STOPPED,))
    ok, err = c.control(op=proto.OP_JOG, rev=False)
    check("停机后点动恢复受理", ok, "" if ok else err)
    c.control(op=proto.OP_JOG_RELEASE)
    wait_run_state(c, (proto.RUN_STOPPED,))

    c.close()
    time.sleep(0.8)


def case_jog_watchdog(host, port):
    """E 组：点动看门狗 —— 心跳丢失必须在 600ms 后自动停机。"""
    section("E. 点动看门狗（安全兜底）")
    c, _role = login_as(host, port, "root", "root")

    # 先启动点动，然后**不再发心跳**，观察是否自动停
    ok, err = c.control(op=proto.OP_JOG, rev=False)
    check("点动启动受理", ok, "" if ok else err)
    t0 = time.time()
    # 600ms 超时 + tick 粒度余量；在 1.8s 内必须看到停止
    stopped_at = None
    while time.time() - t0 < 1.8:
        time.sleep(0.15)
        snap = c.snapshot()
        if snap.get("runState") == proto.RUN_STOPPED:
            stopped_at = time.time() - t0
            break
    check("心跳丢失后自动停机", stopped_at is not None,
          "耗时 %.2fs（阈值 %.2fs）"
          % (stopped_at or -1, proto.JOG_TIMEOUT_MS / 1000.0))
    if stopped_at is not None:
        # 不能停得太早（<0.5s 会把正常点动误停），也不能拖过 2s
        check("停机时机在合理窗口内", 0.5 <= stopped_at <= 2.0,
              "%.2fs" % stopped_at)

    # 持续心跳期间不得停机
    ok, err = c.control(op=proto.OP_JOG, rev=False)
    check("重新点动受理", ok, "" if ok else err)
    alive = True
    for _ in range(5):                     # 5 × 200ms = 1.0s，持续喂心跳
        time.sleep(0.2)
        c.control(op=proto.OP_JOG, rev=False)
        snap = c.snapshot()
        if snap.get("running") is not True:
            alive = False
            break
    check("持续心跳期间不停机", alive,
          "running=%s" % c.snapshot().get("running"))

    # 收尾：释放点动
    c.control(op=proto.OP_JOG_RELEASE)
    c.close()
    time.sleep(0.8)


def case_mode_gate(host, port):
    """C 组：自动模式最前置闸（连 root 都不放行）。"""
    section("C. 自动模式闸门")
    c, _role = login_as(host, port, "root", "root")
    check("切到自动模式", c.set_mode(1))

    # 自动模式下 root 的所有控制都必须被拒
    for label, kw in (("启动", dict(op=proto.OP_START)),
                      ("设频", dict(op=proto.OP_SET_FREQUENCY, hz=10.0)),
                      ("停机", dict(op=proto.OP_STOP)),
                      ("故障复位", dict(op=proto.OP_RESET_FAULT)),
                      ("点动", dict(op=proto.OP_JOG)),
                      ("点动释放", dict(op=proto.OP_JOG_RELEASE))):
        ok, err = c.control(**kw)
        check("自动模式拒绝%s" % label, not ok, "err=%r" % (err or "")[:40])
        check("拒绝原因含'自动运行中'", "自动运行中" in (err or ""),
              "err=%r" % (err or "")[:30])

    # 遥测/快照不受影响
    snap = c.snapshot()
    check("自动模式仍可读快照", bool(snap), "keys=%d" % len(snap))
    check("自动模式应答含 auto=true", c.request(
        proto.CMD_RUN_MODE, b"")[1].get("auto") is True)

    check("恢复手动模式", c.set_mode(0))
    # [为什么用启动而不是调速] 2026-11-05 起，**停机状态下禁止下发给定**
    # （给定是运行参数，不是可预置配置；否则会出现"停机+25Hz+720rpm"这种
    # 自相矛盾的界面）。本组只想验证"手动模式恢复控制"，故用停机时合法
    # 的动作类命令。
    ok, err = c.control(op=proto.OP_START)
    check("手动模式恢复控制", ok, "" if ok else err)
    # 顺带锁住新规则本身：停机时调速必须被**明确拒绝**（不能静默成功）。
    # [ACC/DEC 斜坡] 等减速到位再发 SET——运行中 SET 是合法操作，斜坡中途
    # 发会被受理，闸门断言就失去意义了。
    c.control(op=proto.OP_STOP)
    wait_run_state(c, (proto.RUN_STOPPED,))
    ok_set, err_set = c.control(op=proto.OP_SET_FREQUENCY, hz=15.0)
    check("停机时下发给定被明确拒绝", not ok_set,
          "err=%s" % err_set if not ok_set else "居然成功了（规则失效）")
    check("拒绝原因指向先启动", "停机" in (err_set or ""), err_set or "")
    c.close()
    time.sleep(0.8)


def case_boundaries(host, port):
    """D 组：载荷边界（畸形/缺字段/越界 op）。"""
    section("D. 载荷边界")
    c, _role = login_as(host, port, "root", "root")

    r = c.request(proto.CMD_VFD_CONTROL, b"\xde\xad\xbe\xef")
    check("非法 BSON 有应答", r is not None)
    if r:
        check("非法 BSON 回 ok=false", r[1].get("ok") is False,
              "err=%r" % (r[1].get("err") or "")[:40])

    r = c.request(proto.CMD_VFD_CONTROL, bson.encode({"hz": 10.0}))
    check("缺 op 字段被拒", r is not None and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])

    r = c.request(proto.CMD_VFD_CONTROL, bson.encode({"op": 9999}))
    check("越界 op 被拒", r is not None and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])

    r = c.request(proto.CMD_VFD_CONTROL, bson.encode({"op": "start"}))
    check("op 类型错误被拒", r is not None and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])

    r = c.request(proto.CMD_VFD_CONTROL, b"", proto.DATATYPE_BSON)
    check("空载荷被拒", r is not None and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])

    # 未知命令号
    r = c.request(0x7FFF, b"")
    check("未知命令有应答", r is not None)
    if r:
        check("未知命令回 ok=false", r[1].get("ok") is False,
              "err=%r" % (r[1].get("err") or ""))

    c.close()
    time.sleep(0.8)


def case_command_codes(host, port):
    """G 组：设备侧实际收到的命令字必须与 C 侧口径一致。

    本组查 harness 侧记录的 commands（经一条特殊快照键回读不到），
    故改为「下命令 → 观察状态变化是否与命令语义一致」的端到端验证。
    命令字数值本身由 tests/test_units.py 单测覆盖。
    """
    section("G. 命令字语义端到端")
    c, _role = login_as(host, port, "root", "root")
    # 正转 → running true, reverse false
    c.control(op=proto.OP_START, rev=False)
    time.sleep(0.35)
    snap = c.snapshot()
    check("START(fwd) → running&&!reverse",
          snap.get("running") is True and snap.get("reverse") is False,
          "running=%s reverse=%s" % (snap.get("running"), snap.get("reverse")))
    # 停机 → 停止（ACC/DEC 斜坡：等减速到位）
    c.control(op=proto.OP_STOP)
    check("STOP → runState=Stopped",
          wait_run_state(c, (proto.RUN_STOPPED,)).get("runState")
          == proto.RUN_STOPPED)
    # 反转 → running&&reverse
    c.control(op=proto.OP_START, rev=True)
    time.sleep(0.35)
    snap = c.snapshot()
    check("START(rev) → running&&reverse",
          snap.get("running") is True and snap.get("reverse") is True,
          "running=%s reverse=%s" % (snap.get("running"), snap.get("reverse")))
    c.control(op=proto.OP_FREE_STOP)
    time.sleep(0.3)
    c.close()
    time.sleep(0.8)


def case_running_reversal(host, port, sim_host=None, sim_port=6200):
    """H 组：运行中换向 —— 反向 START 先减速过零、再反向加速（2026-11-06）。

    分两层验证：
      · **H1 协议面**（本 harness）：反向 START 受理、方向最终翻转、终态
        反转运行。FakeVfd 是协议夹具（方向瞬变、输出=给定），够用。
      · **H2 物理面**（连 `hsfsrv --sim` 真实虚拟模型）：减速段输出经过
        零（非瞬跳）、状态字过零才翻、反转按 ACC 爬回给定。FakeVfd 没有
        斜坡，必须连真模型；`--sim-host` 未给或不可达时整组跳过并说明。

    [口径] 正转运行中收到反向 START（rev=True）不拒也不瞬跳：服务端虚拟
    变频器按 DEC 把输出降到 0（这段状态字仍报**正转**），过零那一刻翻向、
    按 ACC 爬回给定。C 侧 XVirtualModelVfd 同口径（xmodbus_virtual_test
    ①b 与 tests/test_units.py TestVfdReversal 互为镜像）。
    """
    # ==================== H1：协议面（harness 假设备） ====================
    section("H1. 运行中换向（协议面：受理与翻向）")
    c, _role = login_as(host, port, "root", "root")
    ok, err = c.control(op=proto.OP_START, rev=False, hz=25.0)
    check("正转启动(捎带给定)受理", ok, "" if ok else err)
    time.sleep(0.3)
    snap = c.snapshot()
    check("稳态为正转", snap.get("reverse") is False,
          "reverse=%s" % snap.get("reverse"))

    ok, err = c.control(op=proto.OP_START, rev=True, hz=25.0)
    check("运行中反向 START 受理", ok, "" if ok else err)
    snap = wait_run_state(c, (proto.RUN_RUNNING_REVERSE,))
    check("运行中换向翻为反转", snap.get("reverse") is True,
          "reverse=%s" % snap.get("reverse"))
    check("换向后仍处运行态", snap.get("running") is True,
          "runState=%s" % snap.get("runState"))

    c.control(op=proto.OP_FREE_STOP)
    time.sleep(0.3)
    c.close()
    time.sleep(0.8)

    # ==================== H2：物理面（hsfsrv --sim 真模型） ====================
    if not sim_host:
        print("  [SKIP] 未给 --sim-host，物理面（减速过零）不在此跑——"
              "模型级口径已由 tests/test_units.py TestVfdReversal 覆盖")
        return
    section("H2. 运行中换向（物理面：减速过零，连 hsfsrv --sim %s:%d）"
            % (sim_host, sim_port))
    try:
        c, _role = login_as(sim_host, sim_port, "root", "root")
    except Exception as exc:                            # noqa: BLE001
        print("  [SKIP] --sim 服务端不可达（%s:%d）：%s"
              % (sim_host, sim_port, exc))
        return

    # ---- 正转爬到给定 25Hz（0→25Hz @5Hz/s 约 5s） ----
    ok, err = c.control(op=proto.OP_START, rev=False, hz=25.0)
    check("[sim] 正转启动(捎带给定)受理", ok, "" if ok else err)
    deadline = time.time() + 10.0
    snap = {}
    while time.time() < deadline:
        snap = c.snapshot()
        if float(snap.get("outputFreqHz", 0) or 0) >= 24.0:
            break
        time.sleep(0.2)
    check("[sim] 正转爬到给定 25Hz",
          float(snap.get("outputFreqHz", 0) or 0) >= 24.0,
          "out=%.2f" % float(snap.get("outputFreqHz", 0) or 0))
    check("[sim] 稳态为正转", snap.get("reverse") is False,
          "reverse=%s" % snap.get("reverse"))

    # ---- 运行中直接发反向 START：受理（不拒、不瞬跳） ----
    ok, err = c.control(op=proto.OP_START, rev=True, hz=25.0)
    check("[sim] 运行中反向 START 受理", ok, "" if ok else err)

    # ---- 减速段：输出降向 0 且状态字仍报正转；过零翻反转 ----
    min_hz = 1e9
    deadline = time.time() + 10.0
    while time.time() < deadline:
        snap = c.snapshot()
        hz = float(snap.get("outputFreqHz", 0) or 0)
        min_hz = min(min_hz, hz)
        if snap.get("reverse") is True:
            break
        time.sleep(0.12)
    check("[sim] 换向过零翻为反转", snap.get("reverse") is True,
          "reverse=%s out=%.2f" % (snap.get("reverse"),
                                   float(snap.get("outputFreqHz", 0) or 0)))
    check("[sim] 减速段经过零（min≈0，非瞬跳换向）", min_hz <= 2.0,
          "min=%.2f" % min_hz)
    check("[sim] 过零后仍处运行态", snap.get("running") is True,
          "runState=%s" % snap.get("runState"))

    # ---- 反转按 ACC 爬回给定 ----
    deadline = time.time() + 10.0
    while time.time() < deadline:
        snap = c.snapshot()
        if float(snap.get("outputFreqHz", 0) or 0) >= 24.0:
            break
        time.sleep(0.2)
    check("[sim] 反转爬回给定 25Hz",
          float(snap.get("outputFreqHz", 0) or 0) >= 24.0,
          "out=%.2f" % float(snap.get("outputFreqHz", 0) or 0))
    check("[sim] 最终稳态反转运行",
          snap.get("runState") == proto.RUN_RUNNING_REVERSE,
          "runState=%s" % snap.get("runState"))

    # 收尾（不留给别人一个转着的电机）
    c.control(op=proto.OP_FREE_STOP)
    wait_run_state(c, (proto.RUN_STOPPED,))
    c.close()
    time.sleep(0.8)


# ============================================================================
#                               main
# ============================================================================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6199)
    ap.add_argument("--sim-host", default=None,
                    help="H2 物理面（减速过零）要连的 hsfsrv --sim 地址；"
                         "缺省跳过（模型口径已由 test_units 覆盖）")
    ap.add_argument("--sim-port", type=int, default=6200)
    args = ap.parse_args()

    print("变频器全操作测试  %s:%d" % (args.host, args.port))
    t0 = time.time()
    try:
        case_role_gate(args.host, args.port)
        case_all_operations(args.host, args.port)
        case_jog_watchdog(args.host, args.port)
        case_mode_gate(args.host, args.port)
        case_boundaries(args.host, args.port)
        case_command_codes(args.host, args.port)
        case_running_reversal(args.host, args.port,
                              sim_host=args.sim_host,
                              sim_port=args.sim_port)
    except Exception as exc:                            # noqa: BLE001
        print("\n[ERROR] %s: %s" % (type(exc).__name__, exc))
        FAILED.append("exception")

    dt = time.time() - t0
    print("\n===== %d passed, %d failed  (%.1fs) =====" % (PASS, FAIL, dt))
    if FAILED:
        print("失败项: " + ", ".join(FAILED))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
