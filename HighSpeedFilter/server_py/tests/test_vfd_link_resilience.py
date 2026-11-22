# -*- coding: utf-8 -*-
"""变频器链路韧性回归（2026-11-18 板端实锅）。

[现场现象] 板端（192.168.1.249，Python 服务端）日志页持续刷：
    搅拌泵 采集失败（连续 1 次）：Modbus 异常应答 fc=0x03 code=3
    搅拌泵 恢复通讯（此前连续失败 1 次）        —— 6 分钟 16 对

[根因（板端实测取证，见本文件头「取证记录」)]
    1. 设备是 M512，它对**被破坏的请求**回一帧 8 字节错误帧（错误码 3
       「数据错」）：故意把请求 CRC 打坏 → 设备回 `02 03 80 01 00 03 7d f8`，
       与现场日志里的字节**逐字节相同**；同一笔请求单独重发**一律正常**；
       设备对真正的非法请求只回 0x04/0x05（0x0000→5、0x1020×2→4、
       单点连读→5），**从不回 0x03**。
    2. ⇒ 现场 485 链路偶发位错 → 请求 CRC 坏 → 设备回「数据错」→ 老代码
       把异常帧当「从站明确拒绝」直接抛出（不重试）→ 打断整拍采集 →
       遥测停更 + 刷告警。

[本测试锁三件事]
    A 传输层：异常帧**也重试**（第一次坏、第二次好 ⇒ 读成功）。
    B 传输层：重试仍异常 ⇒ 照旧抛出（fail-closed，不把拒绝说成成功）。
    C 设备层：单组失败 ⇒ **整拍不失败**（不抛、不降级、不冒泡告警），
      失败组保留上一拍的值、其余组照常刷新；全部组失败 ⇒ 照旧失败 +
      离线判定照旧。

[取证记录（板端实测，2026-11-18）]
    · /dev/ttyS3 = SoC 硬件 UART（fe670000），站 2 = 变频器。
    · 只读探针（仅 FC03）：150ms 间隔下 7 个读组全部正常，0x1010×4 连读
      12/12 正常 ⇒ 地址表没问题。
    · 故意坏 CRC ⇒ 设备回 `02 03 80 01 00 03 7d f8`（= 现场告警字节）。
    · 请求中途被拆帧（任意位置、≥2ms 间隔）⇒ 设备**静默丢弃**（超时）。
    · 迟到的应答帧：超时后再等 150ms+300ms 读端口，36 次全部为空 ⇒
      不是「迟到应答被当成本次应答」。
"""

import asyncio
import os
import struct
import sys
import time

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向到
# 文件时用 GBK ⇒ 打印「⇒」(U+21D2，GBK 编不了) 直接 UnicodeEncodeError
# ⇒ 整套测试在打印断言时被打断（实测退出码仍是 0，看着像"跑完了"）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from hsfsrv import devices
from hsfsrv import modbus
from hsfsrv.modbus import RtuMaster, append_crc

#: M512 的 8 字节错误帧（功能码**不置** 0x80）：[站号][fc][80][01][00][码]
M512_ERR_FRAME = bytes([0x02, 0x03, 0x80, 0x01, 0x00, 0x03])


class FakeSerial(object):
    """内存串口：按 (unit, fc, addr, cnt) 应答；可指定「前 N 次坏」。

    [为什么能造出「第一次坏、第二次好」] 现场就是链路位错：同一笔请求
    重发即好。bad_frame 默认给 M512 的「数据错」帧（现场实战的字节）。
    """

    def __init__(self, table, bad_first=0, bad_frame=None):
        self.table = table            # {(unit, fc, addr, cnt): [values]}
        self.bad_first = bad_first
        self.bad_frame = bad_frame or M512_ERR_FRAME
        self.calls = 0
        self.tx = bytearray()
        self.is_open = True
        self.requests = []
        self.times = []

    def reset_input_buffer(self):
        self.tx.clear()

    def write(self, data):
        self.requests.append(bytes(data))
        self.times.append(time.monotonic())
        body = bytes(data[:-2])
        unit, fc = body[0], body[1]
        addr = (body[2] << 8) | body[3]
        cnt = (body[4] << 8) | body[5]
        self.calls += 1
        if self.calls <= self.bad_first:
            self.tx += append_crc(self.bad_frame)
            return
        vals = self.table.get((unit, fc, addr, cnt))
        if vals is None:
            self.tx += append_crc(bytes([unit, fc | 0x80, 0x05]))
            return
        payload = b"".join(struct.pack(">H", v & 0xFFFF) for v in vals)
        self.tx += append_crc(bytes([unit, fc, len(payload)]) + payload)

    def flush(self):
        pass

    def read(self, n):
        if not self.tx:
            return b""
        out = bytes(self.tx[:n])
        del self.tx[:n]
        return out

    def close(self):
        self.is_open = False


class StubMaster(object):
    """按地址决定成败的假 master（模拟「某一组被链路位错打掉」）。"""

    def __init__(self, values, fail_addrs=(), fail_all=False):
        self.values = values            # {addr: value}
        self.fail_addrs = set(fail_addrs)
        self.fail_all = fail_all
        self.calls = []

    async def read(self, function, address, count, unit=None):
        self.calls.append((address, count))
        if self.fail_all or address in self.fail_addrs:
            raise modbus.ModbusException(3, 3)
        out = []
        for i in range(count):
            out.append(int(self.values.get(address + i, 0)) & 0xFFFF)
        return out

    async def write_single(self, function, address, value, unit=None):
        return True


def _vfd_values():
    """一组"正常"的变频器寄存器值（0x1000 给定、0x1001 输出频率…）。"""
    vals = {}
    for addr, count in devices.VfdM512.READ_GROUPS:
        for i in range(count):
            vals[addr + i] = 0
    vals[0x1000] = 3000          # 频率给定 30.00%
    vals[0x1001] = 3000          # 输出频率 30.00Hz
    vals[0x3000] = 1             # 运行状态：正转
    vals[0x8000] = 0             # 无故障
    return vals


async def case_a_exception_retried():
    """A 传输层：异常帧也重试（第一次坏、第二次好 ⇒ 读成功）。"""
    table = {(2, 3, 0x1010, 4): [1, 2, 3, 4]}
    fake = FakeSerial(table, bad_first=1)
    master = RtuMaster("FAKE", 115200, 2, 0.4, 1, log=lambda s: None)
    master._serial = fake
    got = await master.read(3, 0x1010, 4, unit=2)
    ok = got == [1, 2, 3, 4] and len(fake.requests) == 2
    print("  [%s] A 异常帧重试后读成功：got=%s 请求数=%d"
          % ("PASS" if ok else "FAIL", got, len(fake.requests)))
    return ok


async def case_b_exception_still_raises():
    """B 传输层：重试仍异常 ⇒ 照旧抛出（fail-closed）。"""
    table = {(2, 3, 0x1010, 4): [1, 2, 3, 4]}
    fake = FakeSerial(table, bad_first=99)
    master = RtuMaster("FAKE", 115200, 2, 0.4, 1, log=lambda s: None)
    master._serial = fake
    raised = None
    try:
        await master.read(3, 0x1010, 4, unit=2)
    except modbus.ModbusException as exc:
        raised = exc
    ok = raised is not None and len(fake.requests) == 2
    print("  [%s] B 重试仍异常照旧抛出：%s 请求数=%d"
          % ("PASS" if ok else "FAIL", raised, len(fake.requests)))
    return ok


async def case_c_group_tolerance():
    """C 设备层：单组失败 ⇒ 整拍不失败；失败组保留旧值；其余组刷新。"""
    vals = _vfd_values()
    logs = []
    master = StubMaster(vals, fail_addrs=(0x1010,))
    dev = devices.VfdM512(lambda: master, 2, logs.append)
    # 先跑一拍好的，建立"上一拍的值"
    await dev.poll(1000)
    dev.raw[0x1011] = 777            # 给失败组留一个可辨认的旧值
    before_state = dev.run_state
    raised = None
    try:
        await dev.poll(2000)
    except Exception as exc:          # noqa: BLE001
        raised = exc
    ok = (raised is None
          and dev.online is True
          and dev.last_ok_ms == 2000
          and dev.raw.get(0x1011) == 777        # 失败组保留旧值
          and dev.raw.get(0x1000) == 3000      # 其余组照常刷新
          and dev.run_state == before_state)   # 状态结论不被污染
    print("  [%s] C 单组失败不放弃整拍：raised=%s online=%s last_ok=%s 旧值=%s"
          % ("PASS" if ok else "FAIL", raised, dev.online, dev.last_ok_ms,
             dev.raw.get(0x1011)))
    print("        组级失败日志（限频）：%s" % (logs[-1] if logs else "<无>"))
    return ok


async def case_d_all_groups_fail():
    """D 设备层：全部组失败 ⇒ 照旧失败（抛异常）+ 离线判定照旧。"""
    vals = _vfd_values()
    logs = []
    master = StubMaster(vals, fail_all=True)
    dev = devices.VfdM512(lambda: master, 2, logs.append)
    raised = 0
    for i in range(dev.FAILS_TO_OFFLINE):
        try:
            await dev.poll(1000 + i)
        except modbus.ModbusError:
            raised += 1
    ok = (raised == dev.FAILS_TO_OFFLINE and dev.online is False)
    print("  [%s] D 全部组失败照旧失败并判离线：抛=%d online=%s"
          % ("PASS" if ok else "FAIL", raised, dev.online))
    return ok


async def case_e_status_read_fail_keeps_state():
    """E 设备层：状态字读失败 ⇒ 运行结论保留上一拍（不误判）。"""
    vals = _vfd_values()
    logs = []
    master = StubMaster(vals)
    dev = devices.VfdM512(lambda: master, 2, logs.append)
    await dev.poll(1000)
    state_before = dev.run_state
    # 让状态字（0x3000）与故障码（0x8000）读失败
    master.fail_addrs = (0x3000, 0x8000)
    raised = None
    try:
        await dev.poll(2000)
    except Exception as exc:          # noqa: BLE001
        raised = exc
    ok = (raised is None and dev.run_state == state_before
          and dev.online is True)
    print("  [%s] E 状态字读失败保留上一拍结论：state=%s（应为 %s）"
          % ("PASS" if ok else "FAIL", dev.run_state, state_before))
    return ok


async def case_f_fail_cooldown():
    """F 传输层：失败后有退避（重试不是隔 8ms 就轰，而是等从站缓过来）。

    [为什么] 板端探针实测：事务间隔 150ms → 100+ 笔 0 失败；间隔
    0~100ms → 偶发失败。失败后立刻重发等于在从站"坏状态"里继续轰它 ⇒
    同一拍连续几笔都坏 ⇒ 整拍报废 ⇒ 刷告警。⇒ 失败后退避 ≥150ms。
    """
    table = {(2, 3, 0x1010, 4): [1, 2, 3, 4]}
    fake = FakeSerial(table, bad_first=1)
    master = RtuMaster("FAKE", 115200, 2, 0.4, 1, log=lambda s: None)
    master._serial = fake
    await master.read(3, 0x1010, 4, unit=2)
    gap = (fake.times[-1] - fake.times[0]) * 1000.0
    need = RtuMaster.FAIL_COOLDOWN_MS
    ok = len(fake.times) == 2 and gap >= need * 0.9
    print("  [%s] F 失败后退避：两次请求间隔 %.0fms（应 ≥%dms）"
          % ("PASS" if ok else "FAIL", gap, need))
    return ok


async def main():
    print("=== 变频器链路韧性（异常帧重试 + 组级容错）===")
    results = []
    for case in (case_a_exception_retried,
                 case_b_exception_still_raises,
                 case_c_group_tolerance,
                 case_d_all_groups_fail,
                 case_e_status_read_fail_keeps_state,
                 case_f_fail_cooldown):
        results.append(await case())
    print("\n%s  （%d/%d 通过）"
          % ("全部通过" if all(results) else "有失败",
             sum(results), len(results)))
    return 0 if all(results) else 1


sys.exit(asyncio.run(main()))
