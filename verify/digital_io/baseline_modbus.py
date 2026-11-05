# -*- coding: utf-8 -*-
"""MT3A-IO1632 裸协议基线：DI 读 / DO 读 / DO 单点写回读 / 恢复原值。

[为什么先做裸协议基线] 联调时要区分「我的服务端代码有问题」和「设备/网络
本身有问题」。这个脚本不经过任何框架代码，只用标准库 socket 发 Modbus-TCP，
是判断其他一切结果的地基。

用法：python -I verify/digital_io/baseline_modbus.py [host]
安全：只做「读 → 单点写 → 立刻回读 → 恢复原值」，不长时间保持任何输出。
"""
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.12"
PORT = 502
UNIT = 1
TIMEOUT = 3.0


def _recv_frame(sock):
    """按 MBAP 长度字段凑满整帧。

    [为什么要凑] 该模块 qty=16 时会把 2 字节数据拆成两个 TCP 段返回
    （实测先回 0202 再补 00），收到一段就判完会误判成短帧。
    """
    buf = b""
    while len(buf) < 6:
        chunk = sock.recv(256)
        if not chunk:
            return buf
        buf += chunk
    length = struct.unpack(">H", buf[4:6])[0]
    need = 6 + length
    while len(buf) < need:
        chunk = sock.recv(256)
        if not chunk:
            break
        buf += chunk
    return buf


_SOCK = None
_TID = [0]


def _conn():
    """取（必要时建）单一长连接。

    [为什么必须复用] MT3A-IO1632 的 Modbus-TCP 会话槽位数有限
    （实测约 4~其实 8 条）。旧实现每发一帧就 connect一次、
    发完关闭，16 点循环测试一轮就会堆出几十个 TIME_WAIT，
    **把模块的会话槽耗尽后 502 直接不再应答**（实测：ping 与 Web
    配置页都正常，唯独 502 超时）。故改为全局复用一条连接。
    """
    global _SOCK
    if _SOCK is None:
        _SOCK = socket.socket()
        _SOCK.settimeout(TIMEOUT)
        _SOCK.connect((HOST, PORT))
    return _SOCK


def close_conn():
    global _SOCK
    if _SOCK is not None:
        try:
            _SOCK.close()
        except Exception:                       # noqa: BLE001
            pass
        _SOCK = None


def tx(pdu, unit=UNIT):
    """发一帧，返回 (ok, pdu_bytes, elapsed_ms, 原始帧)。"""
    t0 = time.perf_counter()
    try:
        sock = _conn()
        _TID[0] += 1
        sock.sendall(struct.pack(">HHHB", _TID[0] & 0xFFFF, 0,
                                 len(pdu) + 1, unit) + pdu)
        raw = _recv_frame(sock)
    except Exception as exc:                    # noqa: BLE001
        close_conn()                            # 连接已坏，下次重建。
        return False, b"", (time.perf_counter() - t0) * 1000.0, str(exc).encode()
    ms = (time.perf_counter() - t0) * 1000.0
    if len(raw) < 8:
        return False, b"", ms, raw
    # PDU 从第 7 字节起（MBAP 7 字节：tid2 pid2 len2 unit1）
    body = raw[7:]
    if body[0] & 0x80:
        return False, body, ms, raw
    return True, body, ms, raw


def read_bits(fc, addr, qty):
    """FC01(线圈) / FC02(离散输入) 读位。返回 (mask, ms) 或 (None, ms)。"""
    ok, body, ms, raw = tx(bytes([fc]) + struct.pack(">HH", addr, qty))
    if not ok:
        return None, ms, raw
    if len(body) < 2:
        return None, ms, raw
    nbytes = body[1]
    data = body[2:2 + nbytes]
    mask = 0
    for i in range(qty):
        byte_i = i // 8
        if byte_i < len(data) and (data[byte_i] >> (i % 8)) & 1:
            mask |= (1 << i)
    return mask, ms, raw


def write_coil(addr, on):
    """FC05 写单线圈。返回 (ok, ms, raw)。"""
    val = 0xFF00 if on else 0x0000
    ok, body, ms, raw = tx(bytes([5]) + struct.pack(">HH", addr, val))
    return ok, ms, raw


def main():
    print("=== MT3A-IO1632 裸协议基线 @ %s:%d unit=%d ===" % (HOST, PORT, UNIT))
    fails = 0

    di, ms, raw = read_bits(2, 0, 16)
    print("FC02 DI  x16: %s  (%.1fms)  raw=%s"
          % ("0x%04X" % di if di is not None else "FAIL", ms, raw.hex()))
    if di is None:
        fails += 1

    do_before, ms, raw = read_bits(1, 0, 16)
    print("FC01 DO  x16: %s  (%.1fms)  raw=%s"
          % ("0x%04X" % do_before if do_before is not None else "FAIL", ms,
             raw.hex()))
    if do_before is None:
        fails += 1
        print("!! DO 读失败，后续写测试跳过")
        return fails

    # 逐点写 → 回读 → 恢复（可逆；每次写都立刻还原）
    print("--- DO 单点写 + 回读（写 1 → 读 → 写回原值 → 再读）---")
    for ch in range(16):
        orig = bool(do_before & (1 << ch))
        ok1, ms1, _ = write_coil(ch, not orig)
        after, ms2, _ = read_bits(1, 0, 16)
        ok2, ms3, _ = write_coil(ch, orig)
        restored, ms4, _ = read_bits(1, 0, 16)

        flipped = after is not None and bool(after & (1 << ch)) == (not orig)
        back = restored is not None and bool(restored & (1 << ch)) == orig
        status = "OK" if (ok1 and flipped and ok2 and back) else "FAIL"
        if status == "FAIL":
            fails += 1
        print("DO%-2d 原=%-5s 写%-4s -> 读=%-5s 还原->%s  %s "
              "(w %.1f/%.1fms r %.1f/%.1fms)"
              % (ch, "ON" if orig else "OFF",
                 "OFF" if orig else "ON",
                 ("ON" if after & (1 << ch) else "OFF") if after is not None else "?",
                 "OK" if back else "FAIL", status, ms1, ms3, ms2, ms4))

    final, ms, _ = read_bits(1, 0, 16)
    print("最终 DO 位图: 0x%04X（应为 0x%04X）"
          % (final if final is not None else -1, do_before))
    if final != do_before:
        print("!! 最终位图与初始不一致")
        fails += 1

    print("=== %s（失败 %d）===" % ("PASS" if fails == 0 else "FAIL", fails))
    close_conn()
    return fails


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
