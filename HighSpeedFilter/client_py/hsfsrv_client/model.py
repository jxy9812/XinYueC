# -*- coding: utf-8 -*-
"""设备数据模型：把服务端推送的 BSON 解成 UI 可直接用的状态快照。

[为什么要有这一层] 页面不该认识 BSON 键名。模型负责「解包 + 新鲜度判定」，
页面只读属性。这样协议改键名只动这一个文件。

[新鲜度] 服务端给了 `valid` / `staleMs`，但**断线后推送停止，valid 仍是
最后一次的值**。故模型层额外记录「本地最后更新时间」并叠加本地新鲜度：
直读服务端的 valid 会把旧值当实时数据显示（C 端踩过此坑，见
HsfFlowMeterProvider 注释）。
"""

import time


def _now_ms():
    return int(time.monotonic() * 1000)


class VfdState(object):
    """变频器遥测快照。"""

    #: 遥测键 → (属性名, 类型)。与 proto.VFD_TELEMETRY_FIELDS 对齐。
    FIELDS = {
        "runState": ("run_state", int),
        "running": ("running", bool),
        "faulted": ("faulted", bool),
        "reverse": ("reverse", bool),
        "remote": ("remote", bool),
        "jogActive": ("jog_active", bool),
        "setFreqHz": ("set_freq_hz", float),
        "outputFreqHz": ("output_freq_hz", float),
        "outputVoltageV": ("output_voltage_v", float),
        "outputCurrentA": ("output_current_a", float),
        "busVoltageV": ("bus_voltage_v", float),
        "outputPowerKw": ("output_power_kw", float),
        "torquePercent": ("torque_percent", float),
        "speedRpm": ("speed_rpm", float),
        "faultCode": ("fault_code", int),
        "vm": ("valid_mask", int),
        "side": ("side", int),
    }

    def __init__(self):
        self.clear()

    def clear(self):
        self.run_state = 0
        self.running = False
        self.faulted = False
        self.reverse = False
        self.remote = False
        self.jog_active = False
        self.set_freq_hz = 0.0
        self.output_freq_hz = 0.0
        self.output_voltage_v = 0.0
        self.output_current_a = 0.0
        self.bus_voltage_v = 0.0
        self.output_power_kw = 0.0
        self.torque_percent = 0.0
        self.speed_rpm = 0.0
        self.fault_code = 0
        self.valid_mask = 0
        self.side = 0
        self.updated_ms = 0
        #: 服务端实际给出的键集合（用于判断某字段是否可显示）。
        self.present = set()

    def apply(self, doc):
        """把解出的 dict 合并进来（缺的键保持不变 = 沿用上拍值）。"""
        present = set()
        for key, (attr, caster) in self.FIELDS.items():
            if key not in doc:
                continue
            present.add(key)
            try:
                setattr(self, attr, caster(doc[key]))
            except (TypeError, ValueError):
                continue
        self.present = present
        self.updated_ms = _now_ms()

    def has(self, key):
        """服务端这一拍是否提供了该字段（否则 UI 显示 "--"）。"""
        return key in self.present

    def is_fresh(self, stale_ms=5000):
        """本地新鲜度：超过阈值没有新拍即判失效（断线保护）。"""
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def run_state_text(self):
        return RUN_STATE_TEXT.get(self.run_state, "未知")

    def fault_text(self):
        if not self.fault_code:
            return "无故障"
        return FAULT_TEXT.get(self.fault_code, "故障码 0x%04X" % self.fault_code)


#: XVfdRunState → 中文（客户端展示口径）。
RUN_STATE_TEXT = {
    0: "未知", 1: "停机", 2: "就绪", 3: "正转运行", 4: "反转运行",
    5: "正转点动", 6: "反转点动", 7: "加速中", 8: "减速中",
    9: "恒速运行", 10: "直流制动", 11: "自由停车", 12: "快速停车",
    13: "故障",
}

#: M512 故障码 → 中文（只列常见项，与 server 侧一致）。
FAULT_TEXT = {
    0x0002: "加速过电流", 0x0003: "减速过电流", 0x0004: "恒速过电流",
    0x0005: "加速过电压", 0x0006: "减速过电压", 0x0007: "恒速过电压",
    0x0008: "缓冲电阻过载", 0x0009: "欠压故障", 0x000A: "变频器过载",
    0x000B: "电机过载", 0x000C: "输入缺相", 0x000D: "输出缺相",
    0x000E: "模块过热", 0x000F: "外部故障", 0x0010: "通讯异常",
    0x0011: "接触器异常", 0x0012: "电流检测故障", 0x0013: "电机调谐故障",
    0x0014: "编码器/PG 卡故障", 0x0015: "参数读写异常",
    0x0016: "变频器硬件故障", 0x0017: "电机对地短路",
    0x001A: "运行时间到达", 0x001E: "堵载", 0x001F: "PID 反馈丢失",
}


class TempHumState(object):
    """温湿度快照。值以 0.1 为单位（217 = 21.7℃）。"""

    def __init__(self):
        self.clear()

    def clear(self):
        self.temp_tenths = None
        self.hum_tenths = None
        self.temp_valid = False
        self.hum_valid = False
        self.stale_ms = 5000
        self.side = 0
        self.updated_ms = 0

    def apply(self, doc):
        # 值键只在有效时出现：没有就是「无数据」，不能当成 0。
        self.temp_tenths = doc.get("temp")
        self.hum_tenths = doc.get("hum")
        self.temp_valid = bool(doc.get("tempValid", False))
        self.hum_valid = bool(doc.get("humValid", False))
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        self.side = int(doc.get("side", 0) or 0)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < self.stale_ms

    def temp_text(self):
        if not self.temp_valid or self.temp_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.temp_tenths / 10.0)

    def hum_text(self):
        if not self.hum_valid or self.hum_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.hum_tenths / 10.0)


class AnalogState(object):
    """模拟量 8 路快照。

    :ivar raw: 原始寄存器（毫伏）
    :ivar ua:  微安（mA × 1000）
    """

    def __init__(self, channels=8):
        self.channels = channels
        self.raw = [0] * channels
        self.ua = [0] * channels
        self.valid_mask = 0
        self.updated_ms = 0

    def apply(self, doc):
        n = int(doc.get("n", 0) or 0)
        if n:
            self.channels = min(n, 18)
        self.valid_mask = int(doc.get("v", 0) or 0)
        for i in range(self.channels):
            self.raw[i] = int(doc.get("r%d" % i, 0) or 0)
            self.ua[i] = int(doc.get("u%d" % i, 0) or 0)
        self.updated_ms = _now_ms()

    def is_fresh(self, stale_ms=5000):
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def channel_valid(self, index):
        return bool(self.valid_mask & (1 << index))

    def ma(self, index):
        """毫安值（微安 ÷ 1000）。"""
        return self.ua[index] / 1000.0


class DigitalState(object):
    """数字量模块快照（16DI + 16DO）。

    :ivar di: DI 位图（bit n = 通道 n）
    :ivar do: DO 位图
    """

    def __init__(self, index=0, name=""):
        self.index = index
        self.name = name
        self.di = 0
        self.do = 0
        self.count = 16
        self.valid = False
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        self.name = doc.get("name", self.name) or self.name
        self.di = int(doc.get("di", 0) or 0)
        self.do = int(doc.get("do", 0) or 0)
        self.count = int(doc.get("count", 16) or 16)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.valid and self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def di_bit(self, index):
        return bool(self.di & (1 << index))

    def do_bit(self, index):
        return bool(self.do & (1 << index))


class FlowMeterState(object):
    """电磁流量计快照（当前服务端未装配，保留结构以便对接）。"""

    def __init__(self):
        self.flow = 0.0
        self.speed = 0.0
        self.forward_total = 0.0
        self.reverse_total = 0.0
        self.flow_unit = 0xFF
        self.total_unit = 0xFF
        self.alarms = 0
        self.valid = False
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        self.flow = float(doc.get("flow", 0.0) or 0.0)
        self.speed = float(doc.get("speed", 0.0) or 0.0)
        self.forward_total = float(doc.get("forwardTotal", 0.0) or 0.0)
        self.reverse_total = float(doc.get("reverseTotal", 0.0) or 0.0)
        self.flow_unit = int(doc.get("flowUnit", 0xFF) or 0)
        self.total_unit = int(doc.get("totalUnit", 0xFF) or 0)
        self.alarms = int(doc.get("alarms", 0) or 0)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.valid and self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def flow_unit_text(self):
        return FLOW_UNIT_TEXT.get(self.flow_unit, "?")

    def total_unit_text(self):
        return TOTAL_UNIT_TEXT.get(self.total_unit, "?")


#: 瞬时流量单位代码（0x1020）
FLOW_UNIT_TEXT = {
    0: "L/s", 1: "L/min", 2: "L/h", 3: "m³/s", 4: "m³/min",
    5: "m³/h", 6: "kg/s", 7: "kg/min", 8: "kg/h", 9: "t/s",
    10: "t/min", 11: "t/h", 12: "g/s", 13: "g/min", 14: "g/h",
}

#: 累积总量单位代码（0x1021）
TOTAL_UNIT_TEXT = {
    0: "L", 1: "m³", 2: "kg", 3: "t", 4: "g", 5: "kL",
}


class ValveState(object):
    """6 路电磁阀状态（**只看结果，不看接线**）。

    [客户端职责边界] 这里不带任何点位信息 —— 哪路阀接在哪个模块的哪个口，
    是服务端的事。客户端只关心：这一路叫什么、什么状态、能否操作。
    """

    def __init__(self, count=6):
        self.count = count
        #: index -> dict(name/openCmd/openFb/closeFb/state/ready/valid/text)
        self.items = {}
        self.updated_ms = 0

    def apply(self, doc):
        count = int(doc.get("count", 0) or 0)
        if count:
            self.count = min(count, 16)
        items = {}
        for index in range(self.count):
            prefix = "v%d_" % index
            items[index] = {
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            }
        self.items = items
        self.updated_ms = _now_ms()

    def is_fresh(self, stale_ms=5000):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < stale_ms)

    def state_of(self, index):
        item = self.items.get(index)
        return int(item.get("state", 0)) if item else 0

    def open_count(self):
        return sum(1 for i in self.items.values()
                   if int(i.get("state", 0)) == 2)

    def fault_count(self):
        return sum(1 for i in self.items.values()
                   if int(i.get("state", 0)) == 4)

    def ready_count(self):
        return sum(1 for i in self.items.values() if i.get("ready"))


class DeviceHealth(object):
    """单条设备健康条目。"""

    def __init__(self):
        self.name = ""
        self.kind = 0
        self.side = 0
        self.health = 3          # 缺省按离线
        self.error = 0
        self.online = False
        self.run_state = 0
        self.text = ""

    def health_text(self):
        return HEALTH_TEXT.get(self.health, "未知")

    def kind_text(self):
        return KIND_TEXT.get(self.kind, "未知")


HEALTH_TEXT = {0: "正常", 1: "告警", 2: "数据过期", 3: "离线", 4: "故障"}
#: 设备类别 → 中文（必须与 proto.KIND_* 对齐；漏一个就会在界面上显示"未知"）。
#: [实锅] 加电磁阀组时忘了同步这张表，首页"类型"列直接显示"未知"。
KIND_TEXT = {0: "变频器", 1: "温湿度", 2: "流量计", 3: "模拟量",
             4: "485总线", 5: "数字量", 6: "电磁阀组"}


class HealthSnapshot(object):
    """全量设备健康快照。"""

    def __init__(self):
        self.devices = []
        self.seq = 0
        self.ts = 0
        self.updated_ms = 0

    def apply(self, doc):
        n = int(doc.get("devCount", 0) or 0)
        self.seq = int(doc.get("seq", 0) or 0)
        self.ts = int(doc.get("ts", 0) or 0)
        devices = []
        for i in range(n):
            d = DeviceHealth()
            d.name = doc.get("d%d_name" % i, "") or ""
            d.kind = int(doc.get("d%d_kind" % i, 0))
            d.side = int(doc.get("d%d_side" % i, 0))
            # [不能写 `or 3`] health=0 是**正常**，而 `0 or 3` 得 3（离线）——
            # 早先这么写导致所有健康设备被显示成离线（真锅）。
            d.health = int(doc.get("d%d_health" % i, 3))
            d.error = int(doc.get("d%d_error" % i, 0))
            d.online = bool(doc.get("d%d_online" % i, False))
            d.run_state = int(doc.get("d%d_runState" % i, 0))
            d.text = doc.get("d%d_text" % i, "") or ""
            devices.append(d)
        self.devices = devices
        self.updated_ms = _now_ms()

    def counts(self):
        """(故障数, 离线数) —— 供标题栏显示「故障N/离线N」。"""
        fault = sum(1 for d in self.devices if d.health == 4)
        offline = sum(1 for d in self.devices if d.health == 3)
        return fault, offline

    def find(self, kind, side=0):
        for d in self.devices:
            if d.kind == kind and d.side == side:
                return d
        return None


class AppModel(object):
    """全部设备状态 + 运行模式（单一数据源，页面共享）。"""

    def __init__(self):
        self.vfd = VfdState()
        self.temphum = TempHumState()
        self.analog = AnalogState()
        self.flowmeter = FlowMeterState()
        self.digital = {}          # index -> DigitalState
        self.valve = ValveState()  # 6 路电磁阀
        self.health = HealthSnapshot()
        self.run_mode = 0          # 0=手动 1=自动
        self.run_mode_known = False
        self.update_ms = 0

    def digital_state(self, index):
        """取（必要时创建）某模块的数字量状态。"""
        dev = self.digital.get(index)
        if dev is None:
            dev = DigitalState(index)
            self.digital[index] = dev
        return dev

    def digital_count(self):
        return max(self.digital.keys()) + 1 if self.digital else 0

    def is_auto(self):
        return self.run_mode == 1
