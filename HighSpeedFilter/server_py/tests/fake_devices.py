# -*- coding: utf-8 -*-
"""本机自测装置：注入假设备 + 起一份服务端（不需要真实硬件）。

用途：在没有真机（或板上串口被 demo 占用）时验证协议面与业务逻辑。
不参与生产运行；只被 tests/run_local_tests.py 调用。

用法（由测试脚本 import）：
    from fake_devices import FakeVfd, FakeAnalog, FakeDigital
"""

import asyncio
import sys

from hsfsrv import app as appmod
from hsfsrv import devices
from hsfsrv import xlink


class FakeVfd(devices.VfdM512):
    """假变频器：模拟一台正在 15Hz 正转运行的从站。"""

    def __init__(self, log=None):
        devices.VfdM512.__init__(self, lambda: None, 2, log)
        self.online = True
        self.valid_mask = 0
        self.commands = []          # 记录收到的命令字（测试断言用）
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
