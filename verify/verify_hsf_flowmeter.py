#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HSF 电磁流量计 XLink 订阅/快照验证（纯标准库）。

验证内容：
  1. 连接 + 登录（admin 只读账户足够——遥测与快照都不需要 root）
  2. 订阅 HSF_TOPIC_FLOWMETER_STATUS (0x00010004) → 收周期推送
  3. 请求 HSF_CMD_FLOWMETER_SNAPSHOT (0x0106) → 收单拍快照
  4. B 侧（side=1）确认拿到流量计数据；A 侧应为 valid=false
  5. 对比推送与快照的字段一致性

XLink 线格式（XLINK_VERSION 0x0200）：
  帧头 16 字节小端：[payloadLen u32][msgClass u16][msgCode u16]
                    [requestId u32][dataType u8][reserved3]
  Publish 载荷 = [topicId u32 LE][业务载荷]

用法:
  python -I verify_hsf_flowmeter.py [--host 127.0.0.1] [--port 6200]
                                    [--user admin] [--pass admin]
                                    [--seconds 8]
"""
import argparse
import hashlib
import hmac
import socket
import struct
import sys
import time

# ==================== 常量 ====================

XLINK_HEADER_SIZE = 16
MSGCLASS_REQUEST = 1
MSGCLASS_RESPONSE = 2
MSGCLASS_PUBLISH = 3
MSGCLASS_SUBSCRIBE = 4

DATATYPE_RAW = 0
DATATYPE_BSON = 2

CMD_FLOWMETER_SNAPSHOT = 0x0106
TOPIC_FLOWMETER_STATUS = 0x00010004

SIDE_A, SIDE_B = 0, 1

SESSION_HELLO = 1
SESSION_AUTH_REQUEST = 5
SESSION_AUTH_CHALLENGE = 6
SESSION_AUTH_RESPONSE = 7
SESSION_AUTH_RESULT = 8

AUTH_SALT_BYTES = 16
AUTH_NONCE_BYTES = 32
AUTH_STATUS_OK = 0

FLOW_UNIT_NAMES = {
    0: 'L/S', 1: 'L/M', 2: 'L/H', 3: 'M3/S', 4: 'M3/M', 5: 'M3/H',
    6: 'T/S', 7: 'T/M', 8: 'T/H', 9: 'GPS', 10: 'GPM', 11: 'GPH',
    0xFF: 'Unknown',
}
TOTAL_UNIT_NAMES = {0: 'L', 1: 'M3', 2: 'T', 3: 'USG', 0xFF: 'Unknown'}
ALARM_NAMES = [(1 << 0, '上限'), (1 << 1, '下限'), (1 << 2, '空管'), (1 << 3, '系统')]


# ==================== 极简 BSON ====================

def bson_decode(data):
    """BSON 字节 → dict（支持 double/int32/int64/bool/string，够本协议用）。"""
    if len(data) < 5:
        return {}
    out = {}
    i = 4
    end = len(data) - 1
    while i < end:
        t = data[i]
        i += 1
        if t == 0:
            break
        j = data.index(b'\x00', i)
        key = data[i:j].decode('utf-8', 'replace')
        i = j + 1
        if t == 0x01:      # double
            out[key] = struct.unpack('<d', data[i:i + 8])[0]
            i += 8
        elif t == 0x02:    # string
            n = struct.unpack('<I', data[i:i + 4])[0]
            i += 4
            out[key] = data[i:i + n - 1].decode('utf-8', 'replace')
            i += n
        elif t == 0x08:    # bool
            out[key] = bool(data[i])
            i += 1
        elif t == 0x10:    # int32
            out[key] = struct.unpack('<i', data[i:i + 4])[0]
            i += 4
        elif t == 0x12:    # int64
            out[key] = struct.unpack('<q', data[i:i + 8])[0]
            i += 8
        else:
            return out     # 遇到不认识的类型就停（够用）
    return out


def bson_encode(doc):
    """dict → BSON 字节（int→int32、float→double、bool、str）。"""
    body = b''
    for k, v in doc.items():
        kb = k.encode('utf-8') + b'\x00'
        if isinstance(v, bool):
            body += b'\x08' + kb + (b'\x01' if v else b'\x00')
        elif isinstance(v, int):
            body += b'\x10' + kb + struct.pack('<i', v)
        elif isinstance(v, float):
            body += b'\x01' + kb + struct.pack('<d', v)
        elif isinstance(v, str):
            vb = v.encode('utf-8') + b'\x00'
            body += b'\x02' + kb + struct.pack('<I', len(vb)) + vb
    return struct.pack('<I', len(body) + 5) + body + b'\x00'


# ==================== XLink 客户端 ====================

class Client:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=3)
        self.sock.settimeout(0.2)
        self.buf = b''
        self.req_id = 0
        self.pending = {}
        self.publishes = []
        self.bye = None

    def _send(self, msg_class, msg_code, data_type, payload, req_id=0):
        head = struct.pack('<IHHI', len(payload), msg_class, msg_code, req_id)
        head += struct.pack('<B', data_type) + b'\x00\x00\x00'
        self.sock.sendall(head + payload)

    def _recv_exact(self, n):
        while len(self.buf) < n:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                chunk = None
            if not chunk:
                if len(self.buf) < n:
                    return None
                break
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _read_frame(self):
        head = self._recv_exact(XLINK_HEADER_SIZE)
        if not head:
            return None
        plen, mclass, mcode, rid = struct.unpack('<IHHI', head[:12])
        dtype = head[12]
        payload = self._recv_exact(plen) if plen else b''
        return (mclass, mcode, rid, dtype, payload)

    def pump(self, seconds=0.0):
        deadline = time.time() + seconds
        while True:
            f = self._read_frame()
            if f:
                mclass, mcode, rid, dtype, payload = f
                if mclass == MSGCLASS_PUBLISH and len(payload) >= 4:
                    topic = struct.unpack('<I', payload[:4])[0]
                    doc = bson_decode(payload[4:]) if dtype == DATATYPE_BSON else {}
                    self.publishes.append((topic, doc, time.time()))
                elif mclass == MSGCLASS_RESPONSE:
                    body = bson_decode(payload) if dtype == DATATYPE_BSON else {}
                    self.pending[rid] = (mcode, body)
                elif mclass == 0 and mcode == 4:
                    self.bye = payload
            if time.time() >= deadline:
                break

    def _wait_session(self, code, seconds=3.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            f = self._read_frame()
            if f:
                mclass, mcode, _rid, _dt, payload = f
                if mclass == 0 and mcode == code:
                    return payload
            else:
                time.sleep(0.02)
        return None

    def login(self, user, password):
        name = user.encode('utf-8')
        self._send(0, SESSION_AUTH_REQUEST, DATATYPE_RAW,
                   bytes([len(name)]) + name)
        ch = self._wait_session(SESSION_AUTH_CHALLENGE)
        if not ch or len(ch) < 8:
            return (False, 'no challenge')
        method = ch[0]
        salt_len = ch[1]
        iterations = struct.unpack('<I', ch[4:8])[0]
        salt = ch[8:8 + salt_len]
        nonce = ch[8 + salt_len:8 + salt_len + AUTH_NONCE_BYTES]
        if len(salt) != AUTH_SALT_BYTES or len(nonce) != AUTH_NONCE_BYTES:
            return (False, 'bad challenge layout')
        h = hmac.new(password.encode('utf-8'), salt, hashlib.sha256).digest()
        for _ in range(iterations - 1):
            h = hmac.new(password.encode('utf-8'), h, hashlib.sha256).digest()
        resp = hmac.new(h, nonce, hashlib.sha256).digest()
        self._send(0, SESSION_AUTH_RESPONSE, DATATYPE_RAW,
                   bytes([method, 0]) + struct.pack('<H', len(resp)) + resp)
        res = self._wait_session(SESSION_AUTH_RESULT)
        if not res or len(res) < 4:
            return (False, 'no auth result')
        status = res[0]
        role = res[1]
        text_len = struct.unpack('<H', res[2:4])[0]
        text = res[4:4 + text_len].decode('utf-8', 'replace')
        if status != AUTH_STATUS_OK:
            return (False, 'rejected: %s' % (text or status))
        return (True, role)

    def subscribe(self, topic):
        self._send(MSGCLASS_SUBSCRIBE, topic & 0xFFFF, DATATYPE_RAW,
                   struct.pack('<I', topic))

    def request(self, code, payload=b'', data_type=DATATYPE_BSON, wait=5.0):
        """发 Request 并等对应 reqId 的 Response。
        @note [2026-11-04 实锅] 订阅后服务端每 500ms 推一帧，pump 里推帧与
              应答帧混在一起；早期版本 wait 只有 3s 且 pending 不清，连续两次
              请求时第二拍可能被推帧挤掉。现加大等待、并在发送前清 pending。"""
        self.pending.clear()
        self.req_id += 1
        rid = self.req_id
        self._send(MSGCLASS_REQUEST, code, data_type, payload, rid)
        deadline = time.time() + wait
        while time.time() < deadline:
            self.pump(0.1)
            if rid in self.pending:
                return self.pending.pop(rid)
        return None

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


# ==================== 测试 ====================

PASS = 0
FAIL = 0


def check(name, ok, detail=''):
    global PASS, FAIL
    if ok:
        PASS += 1
        print('  [PASS] %s %s' % (name, detail))
    else:
        FAIL += 1
        print('  [FAIL] %s %s' % (name, detail))


def fmt_alarms(mask):
    if mask == 0:
        return '无'
    names = [n for bit, n in ALARM_NAMES if mask & bit]
    return '|'.join(names) if names else '0x%X' % mask


def dump_snapshot(tag, doc):
    """打印一份流量计快照。"""
    if not doc:
        print('  %s: (空)' % tag)
        return
    side = doc.get('side', 0)
    valid = doc.get('valid', False)
    if not valid:
        print('  %s: side=%d valid=false（无数据）' % (tag, side))
        return
    fu = doc.get('flowUnit', 0xFF)
    tu = doc.get('totalUnit', 0xFF)
    print('  %s: side=%d 流量=%.4f %s  流速=%.6f m/s  正向累积=%.4f %s  '
          '报警=%s' % (tag, side, doc.get('flow', 0.0),
                       FLOW_UNIT_NAMES.get(fu, '?'),
                       doc.get('speed', 0.0),
                       doc.get('forwardTotal', 0.0),
                       TOTAL_UNIT_NAMES.get(tu, '?'),
                       fmt_alarms(doc.get('alarms', 0))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=6200)
    ap.add_argument('--user', default='admin')
    ap.add_argument('--passwd', default='admin')
    ap.add_argument('--seconds', type=float, default=8.0)
    a = ap.parse_args()

    print('=' * 72)
    print('HSF 电磁流量计 XLink 验证  %s:%d  用户 %s' % (a.host, a.port, a.user))
    print('=' * 72)

    c = Client(a.host, a.port)
    try:
        print('\n--- 1. 登录 ---')
        ok, role = c.login(a.user, a.passwd)
        check('登录成功', ok, '（role=%s）' % role if ok else str(role))
        if not ok:
            return 1

        print('\n--- 2. 订阅流量计主题 0x%08X ---' % TOPIC_FLOWMETER_STATUS)
        c.subscribe(TOPIC_FLOWMETER_STATUS)
        print('  已发送订阅帧，收集 %.0f 秒推送…' % a.seconds)
        c.pump(a.seconds)

        flow_pubs = [p for p in c.publishes if p[0] == TOPIC_FLOWMETER_STATUS]
        other_pubs = {}
        for topic, _doc, _ts in c.publishes:
            other_pubs[topic] = other_pubs.get(topic, 0) + 1

        print('\n  收到推送统计：')
        for topic, n in sorted(other_pubs.items()):
            name = {0x00010001: 'VFD', 0x00010002: 'AI',
                    0x00010003: 'TEMPHUM', 0x00010004: 'FLOWMETER'}.get(
                        topic, '0x%08X' % topic)
            print('    0x%08X %-10s × %d' % (topic, name, n))

        check('收到流量计主题推送', len(flow_pubs) > 0, '（%d 帧）' % len(flow_pubs))

        # 侧别统计
        by_side = {}
        for _topic, doc, _ts in flow_pubs:
            side = doc.get('side', 0)
            by_side[side] = by_side.get(side, 0) + 1
        print('\n  按侧统计：%s' % {('A' if k == 0 else 'B'): v
                                   for k, v in by_side.items()})

        # B 侧必须收到且有数据
        b_pubs = [d for (_t, d, _ts) in flow_pubs if d.get('side', 0) == SIDE_B]
        check('收到 B 侧推送', len(b_pubs) > 0, '（%d 帧）' % len(b_pubs))
        if b_pubs:
            last_b = b_pubs[-1]
            dump_snapshot('B 侧最新推送', last_b)
            check('B 侧数据有效', last_b.get('valid', False) is True)
            if last_b.get('valid'):
                check('B 侧含 flow 字段', 'flow' in last_b)
                check('B 侧含 flowUnit 字段', 'flowUnit' in last_b)
                check('B 侧含 alarms 字段', 'alarms' in last_b)

        # 推送间隔核查（B 侧）
        if len(b_pubs) >= 2:
            times = [ts for (_t, d, ts) in flow_pubs if d.get('side', 0) == SIDE_B]
            gaps = [times[i + 1] - times[i] for i in range(len(times) - 1)]
            avg = sum(gaps) / len(gaps)
            print('  B 侧推送间隔：平均 %.0f ms（配置 500ms）' % (avg * 1000))
            check('推送间隔合理（300~900ms）', 0.3 <= avg <= 0.9,
                  '（实测 %.0f ms）' % (avg * 1000))

        print('\n--- 3. 请求流量计快照（B 侧）---')
        res = c.request(CMD_FLOWMETER_SNAPSHOT, bson_encode({'side': SIDE_B}))
        check('收到快照应答', res is not None)
        if res:
            mcode, body = res
            dump_snapshot('B 侧快照', body)
            check('快照侧号 = B', body.get('side', -1) == SIDE_B)
            check('快照数据有效', body.get('valid', False) is True)
            # 与推送对比
            if b_pubs:
                same_flow = abs(body.get('flow', -1e9) -
                                b_pubs[-1].get('flow', 1e9)) < 1e-6
                check('快照与推送的流量一致', same_flow,
                      '（%.4f vs %.4f）' % (body.get('flow', 0.0),
                                           b_pubs[-1].get('flow', 0.0)))

        print('\n--- 4. 请求流量计快照（A 侧，应无流量计）---')
        res_a = c.request(CMD_FLOWMETER_SNAPSHOT, bson_encode({'side': SIDE_A}))
        check('收到 A 侧快照应答', res_a is not None)
        if res_a:
            _mcode, body_a = res_a
            dump_snapshot('A 侧快照', body_a)
            check('A 侧无流量计（valid=false）',
                  body_a.get('valid', True) is False)

        print('\n--- 5. 摘要 ---')
        if b_pubs:
            print('  B 侧（COM18 电磁流量计）已通过 XLink 正常推送：')
            print('    主题 0x%08X，%d 帧 / %.0f 秒' %
                  (TOPIC_FLOWMETER_STATUS, len(b_pubs), a.seconds))
        else:
            print('  !! 未收到 B 侧推送，请检查服务端日志中「流量计站装配完成」')

    finally:
        c.close()

    print('\n===== 结果: %d passed, %d failed =====' % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
