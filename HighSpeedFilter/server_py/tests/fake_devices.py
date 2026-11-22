# -*- coding: utf-8 -*-
"""本机自测装置：注入假设备 + 起一份服务端（不需要真实硬件）。

用途：在没有真机（或板上串口被 demo 占用）时验证协议面与业务逻辑。
不参与生产运行；只被 tests/run_local_tests.py 调用。

用法（由测试脚本 import）：
    from fake_devices import FakeVfd, FakeAnalog, FakeDigital
"""

import asyncio
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

from hsfsrv import app as appmod
from hsfsrv import devices
from hsfsrv import virtualdev
from hsfsrv import xlink


class FakeRtuMaster(object):
    """内存 RTU master：FC03/FC06 直接读写宿主设备的 raw 表。

    [为什么要有它 2026-11-18] VfdM512 的参数读写（read_params_snapshot /
    write_param，0x011B）走 master 总线；FakeVfd 原本 master_factory 给
    None（poll/send_command/set_frequency 全被覆盖，用不到 master）——
    若参数路径也在假设备里另写一套，测的就是假代码而不是真设备层。
    现在 master 由 raw 表背书：**真设备层代码跑真路径**，只有"电信号"
    是假的（与 virtualdev 的分层哲学一致）。
    """

    def __init__(self, dev):
        self.dev = dev

    async def read(self, function, address, count, unit=None):
        from hsfsrv import modbus
        if function != 3:
            raise modbus.ModbusError("FakeRtuMaster 只支持 FC03")
        out = []
        for i in range(count):
            addr = address + i
            if addr not in self.dev.raw:
                raise modbus.ModbusError(
                    "FakeVfd 未建模地址 0x%04X（测试夹具缺口）" % addr)
            out.append(int(self.dev.raw[addr]) & 0xFFFF)
        return out

    async def write_single(self, function, address, value, unit=None):
        from hsfsrv import modbus
        if function != 6:
            raise modbus.ModbusError("FakeRtuMaster 只支持 FC06")
        addr = int(address)
        if addr in getattr(self.dev, "deny_write_addrs", ()):
            # 「真机拒写但返回成功」模拟：ACK 照回、寄存器不落——
            # 0x011B「回读不撒谎」用例的夹具口径（服务端写后回读必须
            # 看到实际寄存器值，而不是把提交值当成功值）。
            return True
        raw = int(value) & 0xFFFF
        self.dev.raw[addr] = raw
        self.dev.write_log.append(("param", addr, raw))
        return True

    async def close(self):
        pass


class FakeVfd(devices.VfdM512):
    """假变频器：模拟一台正在 15Hz 正转运行的从站。"""

    def __init__(self, log=None):
        devices.VfdM512.__init__(self, lambda: FakeRtuMaster(self), 2, log)
        self.online = True
        self.valid_mask = 0
        self.commands = []          # 记录收到的命令字（测试断言用）
        self.write_log = []         # 参数写记录（FakeRtuMaster 追加）
        #: 「拒写但 ACK」地址集合（测试往里填；见 FakeRtuMaster.write_single）
        self.deny_write_addrs = set()
        self.freq_set_pct = 3000    # 30.00% → 15Hz @50Hz 基准
        self.raw = {
            0x1000: self.freq_set_pct,
            0x1001: 1500,           # 15.00 Hz（÷100）
            0x1002: 5370,           # 537.0 V
            0x1003: 380,            # 380 V
            0x1004: 125,            # 1.25 A
            0x1005: 75,             # 7.5 kW
            0x1006: 332,            # 33.2 %
            0x100A: 0, 0x100B: 0, 0x1009: 0,
            0x1010: 0, 0x1011: 0, 0x1012: 0, 0x1013: 0,
            0x1019: 0, 0x101A: 0, 0x101B: 0,
            0x101F: 150, 0x1020: 0,
        }
        # 参数寄存器（0xF100~0xF105）：缺省与模拟器 VirtualVfd.PARAM_DEFAULTS
        # 同一张表（单一事实源）——"读默认值"断言两侧一致。
        for _addr, _raw in virtualdev.VirtualVfd.PARAM_DEFAULTS.items():
            self.raw.setdefault(_addr, _raw)
        self._running = True
        self._reverse = False

    async def poll(self, now):
        # 状态字必须反映 _running：M512 语义 1=正转 2=反转 3=停机。
        # 早先这里只按 _reverse 取值，导致停机命令永远看不出来（假设备 bug，
        # 不是服务端 bug——真机的 0x3000 会如实回 3）。
        if not self._running:
            status = 3
        else:
            status = 2 if self._reverse else 1
        self.raw[self.ADDR_STATUS] = status
        self.raw[self.ADDR_FAULT] = 0
        self.raw[0x1000] = self.freq_set_pct
        # 输出频率跟随给定（简化：运行中 = 给定，停机 = 0）
        self.raw[0x1001] = int(self.freq_set_pct / 10000.0 * self.max_freq_hz * 100) \
            if self._running else 0
        self._decode_state(status)
        self._decode_fault(0)
        self._mark_telemetry_valid()
        self.online = True
        self.last_ok_ms = now

    async def send_command(self, code):
        self.commands.append(code)
        if code == self.CMD_RUN_FORWARD:
            self._running, self._reverse = True, False
        elif code == self.CMD_RUN_REVERSE:
            self._running, self._reverse = True, True
        elif code in (self.CMD_DECEL_STOP, self.CMD_FREE_STOP):
            self._running = False
        elif code == self.CMD_FAULT_RESET:
            # [真机口径 2026-11-17] M512 故障复位（0x2000=7）清驱动器故障
            # （docs：复位后故障码=0）⇒ 假设备同口径，回归才能断言"复位
            # 命令真的清了变频器故障"。
            self.faulted = False
            self.fault_code = 0
        elif code in (self.CMD_JOG_FORWARD, self.CMD_JOG_REVERSE):
            self._running = True
            self._reverse = (code == self.CMD_JOG_REVERSE)
        return True

    async def set_frequency(self, hz):
        self.freq_set_pct = int(round(hz / self.max_freq_hz * 10000.0))
        self.freq_set_pct = max(-10000, min(10000, self.freq_set_pct))
        return True

    async def close(self):
        pass


class FakeTempHum(devices.TempHum):
    """假温湿度：21.7℃ / 64.8%RH。"""

    def __init__(self, log=None):
        devices.TempHum.__init__(self, lambda: None, 4, log)
        self.raw_temp = 217
        self.raw_hum = 648
        self.temp_valid = True
        self.hum_valid = True
        self.online = True

    async def poll(self, now):
        self.online = True
        self.temp_valid = True
        self.hum_valid = True
        self.last_ok_ms = now

    async def close(self):
        pass


class FakeAnalog(devices.AnalogInput):
    """假模拟量：CH1 = 995mV（4mA 活零点），CH2 = 4975mV（20mA 满量程）。"""

    def __init__(self, log=None):
        devices.AnalogInput.__init__(self, None, 8, log)
        self.raw = [995, 4975, 2488, 0, 0, 0, 0, 0]
        self.valid = [True] * 8
        self.online = True

    async def poll(self, now):
        self.online = True
        self.last_ok_ms = now


class FakeDigital(devices.DigitalIo):
    """假数字量模块：前 4 路 DI 有信号，DO 全 0，写操作直接生效。"""

    def __init__(self, name, log=None, channels=16):
        devices.DigitalIo.__init__(self, None, name, channels=channels, log=log)
        self.di = [1, 0, 1, 0] + [0] * (self.channels - 4)
        self.do = [0] * self.channels
        self.online = True
        self.write_log = []

    async def poll(self, now):
        self.online = True
        self.last_ok_ms = now

    async def write_do(self, index, on):
        if not 0 <= index < self.channels:
            return False, "DO 通道号越界（0~%d）" % (self.channels - 1)
        self.do[index] = 1 if on else 0
        self.write_log.append((index, bool(on)))
        return True, ""

    async def write_group(self, values):
        if isinstance(values, int):
            targets = [(values >> i) & 1 for i in range(self.channels)]
        else:
            seq = [1 if v else 0 for v in values][:self.channels]
            targets = seq + [0] * (self.channels - len(seq))
        for i, target in enumerate(targets):
            if target != self.do[i]:
                self.do[i] = target
                self.write_log.append((i, bool(target)))
        return True, ""


def build_fake_app(cfg, log=None):
    """装一台全部由假设备构成的业务层（供测试直接起服务端）。"""
    business = appmod.App(cfg, log=log)
    business.vfd = FakeVfd(log)
    business.temphum = FakeTempHum(log)
    business.analog = FakeAnalog(log)
    business.digital = [FakeDigital("数字量1", log), FakeDigital("数字量2", log)]
    business._gates[0x00010001] = [appmod.PushGate(business.VFD_PUSH_MS)]
    business._gates[0x00010002] = [appmod.PushGate(business.AI_PUSH_MS)]
    business._gates[0x00010003] = [appmod.PushGate(500)]
    business._gates[0x00010006] = [appmod.PushGate(business.DIGITAL_PUSH_MS),
                                   appmod.PushGate(business.DIGITAL_PUSH_MS)]
    return business


async def serve(business, cfg, users, log):
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    await business.start_polling()
    return server
