# -*- coding: utf-8 -*-
"""HSF 服务端 XLink 端到端联调：推送 + 数字量/电磁阀控制闭环。

[覆盖]
  1) 握手 + 认证（root）
  2) 订阅数字量主题 → 收到位图推送（点数由服务端给）
  3) 订阅电磁阀主题 → 收到 6 路快照
  4) 数字量 DO 单点写 → 回读位图确认翻转 → 写回原值
  5) 电磁阀控制被拒的正确性（未配置点位 → ok=false + 原因）
  6) 自动模式闸：切自动后控制必须被拒；切回手动后恢复
  7) 温湿度/变频器主题推送节拍观测

[安全] 所有 DO 写都「写 → 回读 → 还原」，不长时间保持输出。

用法：python -I verify/digital_io/e2e_xlink.py [host] [port]
"""
import hashlib
import hmac
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6100

VERSION = 0x0200
HEADER = 16

MSGCLASS_SESSION = 0
MSGCLASS_REQUEST = 1
MSGCLASS_RESPONSE = 2
MSGCLASS_PUBLISH = 3
MSGCLASS_SUBSCRIBE = 4

SESSION_HELLO = 1
SESSION_HELLO_ACK = 2
SESSION_AUTH_REQUEST = 5
SESSION_AUTH_CHALLENGE = 6
SESSION_AUTH_RESPONSE = 7
SESSION_AUTH_RESULT = 8
SESSION_HEARTBEAT = 3

CAP_AUTH = 0x0010
ROLE_ROOT = 2

CMD_VFD_CONTROL = 0x0101
CMD_VFD_SNAPSHOT = 0x0102
CMD_AI_SNAPSHOT = 0x0103
CMD_TEMPHUM_SNAPSHOT = 0x0104
CMD_RUN_MODE = 0x0105
CMD_FLOWMETER_SNAPSHOT = 0x0106
CMD_DEVICE_HEALTH = 0x0107
CMD_DIGITAL_SNAPSHOT = 0x0108
CMD_DIGITAL_CONTROL = 0x0109
CMD_VALVE_SNAPSHOT = 0x0110
CMD_VALVE_CONTROL = 0x0111

TOPIC_VFD = 0x00010001
TOPIC_AI = 0x00010002
TOPIC_TEMPHUM = 0x00010003
TOPIC_FLOWMETER = 0x00010004
TOPIC_HEALTH = 0x00010005
TOPIC_DIGITAL = 0x00010006
TOPIC_VALVE = 0x00010007

DATATYPE_BSON = 1

# ---- 极简 BSON 编解码（只覆盖本联调用到的类型） ----


class BsonDoc(object):
    def __init__(self):
        self.items = []

    def add(self, key, value):
        self.items.append((key, value))
        return self

    def encode(self):
        body = b""
        for key, value in self.items:
            k = key.encode("utf-8") + b"\x00"
            if isinstance(value, bool):
                body += b"\x08" + k + (b"\x01" if value else b"\x00")
            elif isinstance(value, int):
                body += b"\x10" + k + struct.pack("<i", value)
            elif isinstance(value, float):
                body += b"\x01" + k + struct.pack("<d", value)
            elif isinstance(value, str):
                v = value.encode("utf-8") + b"\x00"
                body += b"\x02" + k + struct.pack("<i", len(v)) + v
            else:
                raise TypeError("unsupported %r" % (value,))
        payload = body + b"\x00"
        return struct.pack("<i", len(payload) + 4) + payload


def bson_decode(data):
    """最小 BSON 解码（bool/int/float/string）。"""
    if len(data) < 5:
        return {}
    out = {}
    i = 4
    end = len(data) - 1
    while i < end:
        t = data[i]
        i += 1
        nul = data.index(b"\x00", i)
        key = data[i:nul].decode("utf-8", "replace")
        i = nul + 1
        if t == 0x08:
            out[key] = bool(data[i])
            i += 1
        elif t == 0x10:
            out[key] = struct.unpack("<i", data[i:i + 4])[0]
            i += 4
        elif t == 0x12:
            # int64（应答里的 ts 字段就是这个类型）。
            # [不能当未知类型 break] 一旦 break，后面所有字段全丢，
            # 表现为「devCount=5 但字段全空」的假象。
            out[key] = struct.unpack("<q", data[i:i + 8])[0]
            i += 8
        elif t == 0x01:
            out[key] = struct.unpack("<d", data[i:i + 8])[0]
            i += 8
        elif t == 0x02:
            n = struct.unpack("<i", data[i:i + 4])[0]
            i += 4
            out[key] = data[i:i + n - 1].decode("utf-8", "replace")
            i += n
        else:
            break
    return out


class Client(object):
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=6.0)
        self.sock.settimeout(6.0)
        self.rid = 0
        self.subs = {}          # topic -> 最近一帧 dict
        self.sub_times = {}     # topic -> [收到时刻]
        self.peer_hello = None  # 对方主动发来的 Hello（(version, capability)）
        self.log = []

    def _send(self, mclass, mcode, payload=b"", rid=0, dtype=0):
        self.sock.sendall(struct.pack("<IHHIB3x", len(payload), mclass, mcode,
                                      rid, dtype) + payload)

    def _read_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise IOError("peer closed")
            buf += chunk
        return buf

    def recv_frame(self):
        head = self._read_exact(HEADER)
        plen, mclass, mcode, rid, dtype = struct.unpack("<IHHIB3x", head)
        payload = self._read_exact(plen) if plen else b""
        return mclass, mcode, rid, dtype, payload

    def _recv_session(self, want_code):
        """等一个指定 SESSION 帧。

        [容忍两类中途帧]
          · Publish（class=3）：服务端可能先推遥测（订阅补发）；
          · Hello（class=0/code=1）：本协议是**双向对称握手**——双方各发一个
            Hello，服务端在 Connected 态会补发自己的 Hello
            （XLinkSessionPrivate_ensureHello），故收到对方 Hello 属正常序列。
        """
        while True:
            mclass, mcode, _rid, _dt, payload = self.recv_frame()
            if mclass == MSGCLASS_PUBLISH:
                self._on_publish(mcode, payload)
                continue
            if mclass == MSGCLASS_SESSION and mcode == SESSION_HELLO:
                self.peer_hello = struct.unpack("<HH", payload[:4])
                continue
            assert mclass == MSGCLASS_SESSION and mcode == want_code, \
                "session/%d 期望，得 %s/%s" % (want_code, mclass, mcode)
            return payload

    def hello(self):
        self._send(MSGCLASS_SESSION, SESSION_HELLO,
                   struct.pack("<HH", VERSION, CAP_AUTH))
        payload = self._recv_session(SESSION_HELLO_ACK)
        return struct.unpack("<HH", payload[:4])

    def auth(self, user, password):
        """登录：线格式严格按 XLinkAuth.c。

        AuthRequest   = [u8 nameLen][name]（**不带 NUL**）
        AuthChallenge = [u8 method][u8 saltLen][u16 reserved][u32 iterations][salt][nonce]
        AuthResponse  = [u8 method][u8 0][u16 respLen=32][proof 32]
        AuthResult    = [u8 status][u8 role][u16 textLen][text][serverProof 32]
        """
        name = user.encode("utf-8")
        self._send(MSGCLASS_SESSION, SESSION_AUTH_REQUEST,
                   bytes([len(name)]) + name)
        payload = self._recv_session(SESSION_AUTH_CHALLENGE)
        salt_len = payload[1]
        iterations = struct.unpack("<I", payload[4:8])[0]
        salt = payload[8:8 + salt_len]
        nonce = payload[8 + salt_len:8 + salt_len + 32]

        # 服务端实现是**迭代 HMAC-SHA256**（XLinkAuth_deriveHash），
        # 不是 PBKDF2：
        #   h0 = HMAC(key=password, msg=salt)
        #   hN = HMAC(key=password, msg=h(N-1))   迭代 iterations 次
        #   proof = HMAC(key=hN, msg=nonce)
        # 用 PBKDF2 会得到完全不同的值（实测 status=7 BadPassword）。
        pw = password.encode("utf-8")
        h = hmac.new(pw, salt, hashlib.sha256).digest()
        for _ in range(max(iterations, 1) - 1):
            h = hmac.new(pw, h, hashlib.sha256).digest()
        proof = hmac.new(h, nonce, hashlib.sha256).digest()
        self._send(MSGCLASS_SESSION, SESSION_AUTH_RESPONSE,
                   bytes([1, 0]) + struct.pack("<H", len(proof)) + proof)
        payload = self._recv_session(SESSION_AUTH_RESULT)
        status = payload[0]
        role = payload[1]
        return status, role

    def subscribe(self, topic):
        self._send(MSGCLASS_SUBSCRIBE, 0, struct.pack("<I", topic))

    def request(self, mcode, doc=None, timeout=6.0):
        self.rid += 1
        rid = self.rid
        payload = doc.encode() if doc is not None else b""
        self._send(MSGCLASS_REQUEST, mcode, payload, rid,
                   DATATYPE_BSON if payload else 0)
        deadline = time.time() + timeout
        while time.time() < deadline:
            mclass, rcode, rrid, _dt, rpayload = self.recv_frame()
            if mclass == MSGCLASS_PUBLISH:
                self._on_publish(rcode, rpayload)
                continue
            if mclass == MSGCLASS_RESPONSE and rrid == rid:
                return bson_decode(rpayload)
        raise TimeoutError("request 0x%04X 超时" % mcode)

    def _on_publish(self, mcode, payload):
        if len(payload) < 4:
            return
        topic = struct.unpack("<I", payload[:4])[0]
        doc = bson_decode(payload[4:])
        self.subs[topic] = doc
        self.sub_times.setdefault(topic, []).append(time.time())

    def pump(self, seconds):
        """收集若干秒内的所有帧（不管是不是应答）。

        [必须发心跳] 服务端对「6s 内没收到任何字节」判线（
        XLINK_HEARTBEAT_TIMEOUT_MS）。本脚本作为客户端长时间不发帧……
        就会被当成「断线」（实测 15s 空闲后请求超时）。
        故在等待期间按 2s 周期发一个 Heartbeat（与 C 客户端行为一致）。
        """
        end = time.time() + seconds
        next_beat = time.time() + 2.0
        self.sock.settimeout(0.3)
        while time.time() < end:
            now = time.time()
            if now >= next_beat:
                self._send(MSGCLASS_SESSION, SESSION_HEARTBEAT)
                next_beat = now + 2.0
            try:
                mclass, mcode, _rid, _dt, payload = self.recv_frame()
            except (socket.timeout, TimeoutError):
                continue
            if mclass == MSGCLASS_PUBLISH:
                self._on_publish(mcode, payload)
        self.sock.settimeout(6.0)


def main():
    fails = []
    ok = []

    def check(cond, name, detail=""):
        (ok if cond else fails).append(name)
        print("  [%s] %s %s" % ("OK" if cond else "FAIL", name, detail))

    print("=== HSF XLink 端到端联调 @ %s:%d ===" % (HOST, PORT))
    c = Client(HOST, PORT)
    ver, cap = c.hello()
    check(ver == VERSION, "握手版本一致", "0x%04X" % ver)

    check(c.peer_hello is not None,
          "双向对称握手（收到服务端 Hello）",
          str(c.peer_hello))

    status, role = c.auth("root", "root")
    check(status == 0, "root 认证成功", "status=%d role=%d" % (status, role))
    check(role >= ROLE_ROOT, "root 角色=管理员", "role=%d" % role)

    # ---- 订阅两个新主题 ----
    print("--- 订阅数字量/电磁阀主题 ---")
    c.subscribe(TOPIC_DIGITAL)
    c.subscribe(TOPIC_VALVE)
    c.subscribe(TOPIC_TEMPHUM)
    c.pump(1.5)

    din = c.subs.get(TOPIC_DIGITAL)
    check(din is not None, "收到数字量推送")
    if din:
        n = din.get("count", 0)
        check(n > 0, "数字量点数由服务端给出", "count=%d" % n)
        check("di" in din and "do" in din, "数字量含 di/do 位图",
              "di=0x%04X do=0x%04X" % (din.get("di", 0), din.get("do", 0)))

    val = c.subs.get(TOPIC_VALVE)
    check(val is not None, "收到电磁阀推送")
    if val:
        check(val.get("count", 0) == 6, "电磁阀 6 路",
              "count=%d" % val.get("count", 0))
        names = [val.get("v%d_name" % i, "") for i in range(val.get("count", 0))]
        print("      阀名: %s" % ", ".join(names))

    th = c.subs.get(TOPIC_TEMPHUM)
    if th:
        print("      温湿度: temp=%s hum=%s valid=%s/%s"
              % (th.get("temp"), th.get("hum"), th.get("tempValid"),
                 th.get("humValid")))

    # ---- 数字量快照请求 ----
    print("--- 数字量快照 / 控制闭环 ---")
    snap = c.request(CMD_DIGITAL_SNAPSHOT)
    check(snap.get("count", 0) > 0, "快照应答带点数", "count=%d" % snap.get("count", 0))
    do_before = snap.get("do", 0)
    di_before = snap.get("di", 0)
    print("      DI=0x%04X DO=0x%04X" % (di_before, do_before))

    ch = 0
    orig = bool(do_before & (1 << ch))
    r1 = c.request(CMD_DIGITAL_CONTROL,
                   BsonDoc().add("index", ch).add("on", not orig))
    check(r1.get("ok") is True, "DO%d 写要求受理" % ch, str(r1.get("err", "")))
    after = r1.get("do", do_before)
    check(bool(after & (1 << ch)) == (not orig), "DO%d 位图翻转（写后回读）" % ch,
          "do=0x%04X" % after)

    r2 = c.request(CMD_DIGITAL_CONTROL,
                   BsonDoc().add("index", ch).add("on", orig))
    final = c.request(CMD_DIGITAL_SNAPSHOT).get("do", 0)
    check(bool(final & (1 << ch)) == orig, "DO%d 已还原原值" % ch,
          "do=0x%04X" % final)

    # ---- 电磁阀快照 / 控制 ----
    print("--- 电磁阀快照 / 控制 ---")
    vsnap = c.request(CMD_VALVE_SNAPSHOT)
    check(vsnap.get("count", 0) == 6, "阀快照 6 路")
    rv = c.request(CMD_VALVE_CONTROL, BsonDoc().add("index", 0).add("on", True))
    ready0 = vsnap.get("v0_ready", False)
    if ready0:
        check(rv.get("ok") is True, "阀0 开阀受理")
    else:
        check(rv.get("ok") is False,
              "阀0 未配置点位 → 控制被拒（符合设计）",
              str(rv.get("err", "")))

    # ---- 自动模式闸 ----
    print("--- 自动模式闸 ---")
    mode = c.request(CMD_RUN_MODE)
    check(mode.get("ok") is True, "查询运行模式", "mode=%s" % mode.get("mode"))
    r_auto = c.request(CMD_RUN_MODE, BsonDoc().add("mode", 1))
    check(r_auto.get("ok") is True, "切到自动模式")
    denied = c.request(CMD_DIGITAL_CONTROL,
                       BsonDoc().add("index", 0).add("on", True))
    check(denied.get("ok") is False, "自动模式下 DO 控制被拒",
          str(denied.get("err", "")))
    r_manual = c.request(CMD_RUN_MODE, BsonDoc().add("mode", 0))
    check(r_manual.get("ok") is True, "切回手动模式")

    # ---- 推送节拍 ----
    print("--- 推送节拍（3s 采样）---")
    c.pump(4.0)
    for topic, name, expect in ((TOPIC_DIGITAL, "数字量", 100),
                                (TOPIC_VALVE, "电磁阀", 100),
                                (TOPIC_TEMPHUM, "温湿度", 500)):
        times = c.sub_times.get(topic, [])
        # 温湿度是「按侧」推送（A/B 各一帧交替落地）。若按主题直接算平均间隔，
        # 两侧交替会把读数压成一半（实测 500ms 配置算出 243ms）。
        # 故只用最近一帧的 side 做分组口径（简化：取偶数帧序列作为单侧采样）。
        if topic == TOPIC_TEMPHUM and len(times) >= 4:
            # 交替推送下，同一侧的帧序相隔 2；取每 2 帧取一。
            times = times[::2]
        if len(times) >= 2:
            span = times[-1] - times[0]
            avg = span / (len(times) - 1) * 1000.0
            print("      %s: %d 帧 / %.2fs ≈ %.0fms/帧（配置 %dms）"
                  % (name, len(times), span, avg, expect))
            check(abs(avg - expect) < max(expect, 150),
                  "%s 推送节拍接近配置" % name,
                  "%.0fms" % avg)
        else:
            check(False, "%s 推送帧数不足" % name, "%d 帧" % len(times))

    # ---- 断开 ----
    c.sock.close()

    print("=== %s（通过 %d / 失败 %d）==="
          % ("PASS" if not fails else "FAIL", len(ok), len(fails)))
    if fails:
        for f in fails:
            print("   未通过: %s" % f)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
