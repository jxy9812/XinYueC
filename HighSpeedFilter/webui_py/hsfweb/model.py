# -*- coding: utf-8 -*-
"""设备数据模型：把服务端推送的 BSON 解成结构快照，并导出为 JSON。

与 client_py 的 `hsfsrv_client/model.py` 的关系
================================================

本文件是**同一份模型**的网页版改写，三处差异：

1) **多了一个 `to_json()`**
   Qt 版页面直接读 Python 属性（`st.output_freq_hz`）；网页版要把数据
   过 HTTP/SSE 送出去，必须有明确的 JSON 契约。导出结构一经确定就是
   前后端的接口，改它等于改协议 —— 故集中在这里，不散在路由里。

2) **保留"服务端给了哪些键"的语义（`present`）**
   Qt 版用 `has("outputFreqHz")` 决定显示数值还是 `--`。网页版同样需要：
   品牌没提供的量显示 `--`，**不能显示 0**（0 是"读到了且为零"，
   与"这台设备没有这个量"是两个完全不同的意思）。

3) **新鲜度叠加本地时钟**
   服务端给了 `valid`/`staleMs`，但断线后推送停止，`valid` 仍是最后一次
   的值。故模型层额外记录"本地最后更新时间"并叠加本地新鲜度：
   直读服务端的 valid 会把旧值当实时数据显示。

[为什么不直接透传 BSON 的 doc]
  网页端要显示的是**加工后**的量：0.1 单位的温湿度要除以 10、
  微安要换毫安、位图要拆成通道数组、`state` 整数要映射成中文。
  把这些放前端意味着浏览器要懂 BSON 键名与设备口径 —— 协议一改
  前端全炸。放后端则协议改动只动这一个文件。

[单位与精度的口径]
  · 温湿度：BSON 是 0.1 单位整数（217 = 21.7℃），导出时已除 10
  · 模拟量：`u*` 是微安，导出 `ma` 为毫安（保留 3 位）
  · 变频器：频率/电压/电流保留 2 位，转速取整（它本来就是换算估计值）
  · 数字量：位图导出为 `di`/`do` 整数 + 每通道 `on` 布尔数组

[时间字段] 所有 `updated_ms` 都是**单调毫秒**（time.monotonic），
只用于算"多久没更新"，**不能**当墙上时间。要给前端显示的年龄用
导出结构里的 `ageMs`（服务端在算，前端不用再算差值）。
"""

import time

from . import proto


def _now_ms():
    """单调毫秒（只用于算间隔，不是墙上时间）。"""
    return int(time.monotonic() * 1000)


def wall_ms():
    """墙上时间毫秒（给前端做时间戳）。"""
    return int(time.time() * 1000)


# ============================================================================
#                              文本映射表
# ============================================================================

#: XVfdRunState → 中文（与 client_py/RUN_STATE_TEXT 逐项一致）。
RUN_STATE_TEXT = {
    0: "未知", 1: "停机", 2: "就绪", 3: "正转运行", 4: "反转运行",
    5: "正转点动", 6: "反转点动", 7: "加速中", 8: "减速中",
    9: "恒速运行", 10: "直流制动", 11: "自由停车", 12: "快速停车",
    13: "故障",
}

#: M512/M513 故障码 → 中文（与 server 侧一致，只列常见项）。
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

#: 设备健康 → 中文。
HEALTH_TEXT = {0: "正常", 1: "告警", 2: "数据过期", 3: "离线", 4: "故障"}

#: 设备类别 → 中文（必须与 proto.KIND_* 对齐；漏一个界面就显示"未知"）。
KIND_TEXT = {0: "变频器", 1: "温湿度", 2: "流量计", 3: "模拟量",
             4: "485总线", 5: "数字量", 6: "电磁阀组"}

#: 电磁阀状态 → (中文, 语义色)。
#:
#: [Unknown 的语义] 它表示"状态未知"，**不是**"未配置"——
#: 未配置由独立的 ready 字段表达。现场最常见的情形：点位配好了但行程
#: 开关没接 → 两个反馈恒为 0 → 状态 Unknown。这时候说"未配置"会把人
#: 引到错误的排查方向（去翻配置而不是去看接线）。
VALVE_STATE_TEXT = {
    0: ("状态未知", "unknown"),
    1: ("已关", "closed"),
    2: ("已开", "open"),
    3: ("动作中", "moving"),
    4: ("故障", "fault"),
}

#: 瞬时流量单位代码（0x1020）。
FLOW_UNIT_TEXT = {
    0: "L/s", 1: "L/min", 2: "L/h", 3: "m³/s", 4: "m³/min",
    5: "m³/h", 6: "kg/s", 7: "kg/min", 8: "kg/h", 9: "t/s",
    10: "t/min", 11: "t/h", 12: "g/s", 13: "g/min", 14: "g/h",
}

#: 累积总量单位代码（0x1021）。
TOTAL_UNIT_TEXT = {
    0: "L", 1: "m³", 2: "kg", 3: "t", 4: "g", 5: "kL",
}

#: 阀告警类型 → 中文（服务端已给文字，这里只做兜底与图标选择）。
VALVE_ALARM_TEXT = {
    proto.VALVE_ALARM_TIMEOUT: "动作超时",
    proto.VALVE_ALARM_CONFLICT: "到位信号冲突",
    proto.VALVE_ALARM_CLEARED: "已恢复",
}


def _round(value, digits):
    """安全取整（None 原样返回，别把"无数据"变成 0）。"""
    if value is None:
        return None
    try:
        return round(float(value), digits)
    except (TypeError, ValueError):
        return None


def _int_or(value, default=0):
    """宽松转 int（BSON 里 bool 也是 int 的子类，这里显式排除）。"""
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


# ============================================================================
#                              变频器
# ============================================================================

class VfdState(object):
    """变频器遥测快照。

    [为什么 `present` 是集合而不是布尔] 服务端按**有效掩码**裁剪字段：
    品牌没给的量根本不会出现在 BSON 里。界面显示 `--` 与显示 `0.00`
    必须能区分 —— 前者是"这台设备没有这个量"，后者是"读到了，值是零"。
    """

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

    #: 额定点缺省（服务端未下发时用；现场口径 50Hz → 1440rpm）。
    DEFAULT_RATED_HZ = 50.0
    DEFAULT_RATED_RPM = 1440.0

    def __init__(self):
        self.rated_hz = self.DEFAULT_RATED_HZ
        self.rated_rpm = self.DEFAULT_RATED_RPM
        self.rated_known = False
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
        # 快照应答额外带的额定点（周期推送没有这个键）。
        rated_rpm = doc.get(proto.KEY_RATED_RPM)
        rated_hz = doc.get(proto.KEY_RATED_HZ)
        if isinstance(rated_rpm, (int, float)) and rated_rpm > 0:
            self.rated_rpm = float(rated_rpm)
            self.rated_known = True
        if isinstance(rated_hz, (int, float)) and rated_hz > 0:
            self.rated_hz = float(rated_hz)
            self.rated_known = True

    def has(self, key):
        """服务端这一拍是否提供了该字段（否则界面显示 `--`）。"""
        return key in self.present

    def is_fresh(self, stale_ms=5000):
        """本地新鲜度：超过阈值没有新拍即判失效（断线保护）。"""
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def run_state_text(self):
        return RUN_STATE_TEXT.get(self.run_state, "未知")

    def fault_text(self):
        if not self.fault_code:
            return "无故障"
        return FAULT_TEXT.get(self.fault_code, "故障码 0x%04X" % self.fault_code)

    def rpm_estimate(self):
        """转速：优先用服务端下发的 speedRpm，否则按额定点本地换算。"""
        if self.has("speedRpm"):
            return self.speed_rpm
        if self.has("outputFreqHz") and self.rated_hz > 0:
            return self.output_freq_hz / self.rated_hz * self.rated_rpm
        return None

    def to_json(self):
        fresh = self.is_fresh()
        return {
            "kind": "vfd",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": 5000,
            "runState": self.run_state,
            "runStateText": self.run_state_text(),
            "running": self.running,
            "faulted": self.faulted,
            "reverse": self.reverse,
            "remote": self.remote,
            "jogActive": self.jog_active,
            "faultCode": self.fault_code,
            "faultText": self.fault_text(),
            # 数值：None = 服务端没给这个量（界面显示 --，不要显示 0）
            "setFreqHz": _round(self.set_freq_hz, 2)
                          if self.has("setFreqHz") else None,
            "outputFreqHz": _round(self.output_freq_hz, 2)
                            if self.has("outputFreqHz") else None,
            "outputVoltageV": _round(self.output_voltage_v, 1)
                              if self.has("outputVoltageV") else None,
            "outputCurrentA": _round(self.output_current_a, 2)
                              if self.has("outputCurrentA") else None,
            "busVoltageV": _round(self.bus_voltage_v, 1)
                           if self.has("busVoltageV") else None,
            "outputPowerKw": _round(self.output_power_kw, 2)
                             if self.has("outputPowerKw") else None,
            "torquePercent": _round(self.torque_percent, 1)
                             if self.has("torquePercent") else None,
            "speedRpm": (round(self.rpm_estimate())
                         if self.rpm_estimate() is not None else None),
            # 转速是不是服务端实测给的（界面上要标"换算值"）
            "speedFromServer": self.has("speedRpm"),
            "validMask": self.valid_mask,
            "ratedHz": self.rated_hz,
            "ratedRpm": self.rated_rpm,
            "ratedKnown": self.rated_known,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              温湿度
# ============================================================================

class TempHumState(object):
    """温湿度快照。值以 0.1 为单位（217 = 21.7℃）。

    [现场口径] 这对温湿度是**电箱内**的读数（变送器装在电控柜里），
    用来监视柜内温升与凝露风险。界面文案要带"电箱"，否则会被误读成
    环境温度。
    """

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
        # [值键只在有效时出现] 没有就是"无数据"，不能当成 0 ——
        # 20.0℃ 与"没读到"在凝露告警上是完全相反的两个结论。
        self.temp_tenths = doc.get("temp")
        self.hum_tenths = doc.get("hum")
        self.temp_valid = bool(doc.get("tempValid", False))
        self.hum_valid = bool(doc.get("humValid", False))
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        self.side = _int_or(doc.get("side", 0), 0)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def temp_text(self):
        if not self.temp_valid or self.temp_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.temp_tenths / 10.0)

    def hum_text(self):
        if not self.hum_valid or self.hum_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.hum_tenths / 10.0)

    def to_json(self):
        fresh = self.is_fresh()
        temp = None
        hum = None
        if self.temp_valid and self.temp_tenths is not None and fresh:
            temp = _round(self.temp_tenths / 10.0, 1)
        if self.hum_valid and self.hum_tenths is not None and fresh:
            hum = _round(self.hum_tenths / 10.0, 1)
        return {
            "kind": "temphum",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": self.stale_ms,
            "tempC": temp,          # None = 无数据（界面显示 --）
            "humPct": hum,
            "tempValid": bool(self.temp_valid),
            "humValid": bool(self.hum_valid),
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              模拟量
# ============================================================================

class AnalogState(object):
    """多路 4~20mA 模拟量快照。

    [换算值由服务端给] 量程是**现场标定参数**（同一个变送器型号有
    10kPa~14MPa 多种膜盒，差三个数量级），不该让每个客户端各配一遍。
    客户端只负责按 e*/eu*/en*/ew* 显示。

    [为什么同时留原始 mA] 原始值是唯一无歧义的真相：排查时按它判断
    断线（<3.6mA）/欠量程/超量程。换算值是给人看的工艺量。
    """

    def __init__(self, channels=8):
        self.channels = channels
        self.raw = [0] * channels
        self.ua = [0] * channels
        #: 通道号 → 换算值 ×1000；不在表里 = 该通道没有工程量换算。
        self.eng = {}
        self.eng_unit = {}
        self.eng_name = {}
        self.eng_note = {}
        self.valid_mask = 0
        self.updated_ms = 0

    def apply(self, doc):
        n = _int_or(doc.get("n", 0), 0)
        if n:
            self.channels = min(n, 32)
            self.raw = [0] * self.channels
            self.ua = [0] * self.channels
        self.valid_mask = _int_or(doc.get("v", 0), 0)
        for i in range(self.channels):
            self.raw[i] = _int_or(doc.get("r%d" % i, 0), 0)
            self.ua[i] = _int_or(doc.get("u%d" % i, 0), 0)
        # 换算值：整表重建（服务端不再发 e<i> = 该通道不计工程值了）。
        eng, unit, name, note = {}, {}, {}, {}
        for i in range(self.channels):
            key = "e%d" % i
            if key not in doc:
                continue
            eng[i] = _int_or(doc.get(key, 0), 0)
            unit[i] = str(doc.get("eu%d" % i, "") or "")
            name[i] = str(doc.get("en%d" % i, "") or "")
            note[i] = str(doc.get("ew%d" % i, "") or "")
        self.eng, self.eng_unit = eng, unit
        self.eng_name, self.eng_note = name, note
        self.updated_ms = _now_ms()

    def is_fresh(self, stale_ms=5000):
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def channel_valid(self, index):
        return bool(self.valid_mask & (1 << int(index)))

    def ma(self, index):
        """毫安值（微安 ÷ 1000）。"""
        return self.ua[index] / 1000.0

    def has_engineering(self, index):
        return int(index) in self.eng

    def to_json(self):
        fresh = self.is_fresh()
        channels = []
        for i in range(self.channels):
            valid = self.channel_valid(i)
            item = {
                "index": i,
                "label": "通道 %d" % (i + 1),
                "valid": valid,
                # 无有效位时不给数值（界面显示 --），而不是给 0.000
                "ma": _round(self.ma(i), 3) if valid else None,
                "rawMv": self.raw[i],
                "name": self.eng_name.get(i, ""),
                "unit": "",
                "eng": None,
                "note": self.eng_note.get(i, ""),
            }
            if valid and self.has_engineering(i):
                item["eng"] = _round(self.eng.get(i, 0) / 1000.0, 4)
                item["unit"] = self.eng_unit.get(i, "")
            channels.append(item)
        return {
            "kind": "analog",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": 5000,
            "count": self.channels,
            "validMask": self.valid_mask,
            "channels": channels,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              数字量
# ============================================================================

class DigitalState(object):
    """数字量模块快照（单模块 16 点，聚合后 32 点）。

    :ivar di: DI 位图（bit n = 通道 n）
    :ivar do: DO 位图

    [点数由服务端决定] 单模块物理 16 点，但模块支持主从聚合：
    现场 .13 的 16 点已聚合进 .12，对外就是 32 点。写死 16 会让
    上半区（16~31）永远看不到，而服务端明明在发。
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
        #: 通道号 → 语义名（服务端下发；没有被占用的口不在表里）。
        #:
        #: [客户端不推导这些名字] 哪路阀接在哪个口是现场接线，只有服务端
        #: 知道。客户端拿到什么显示什么，接线一改服务端一改就跟着变。
        self.di_names = {}
        self.do_names = {}
        #: 是否收到过全量帧（服务端 `full` 键）。周期增量帧不带通道名，
        #: 只在收到全量帧后重建名字表 —— 否则每 200ms 一拍都会把名字清空，
        #: 界面表现为"名字一闪一闪"。
        self.got_full = False

    def apply(self, doc):
        """合并一帧。

        [增量帧不清名字] 服务端周期推送的帧 `full=False`（不带 `din*`/`don*`），
        只有订阅补发/首连/快照应答才是全量帧。若每拍都重建名字表，
        名字会在"有→无→有"之间抖动。故名字只在全量帧时整表替换。
        """
        self.name = doc.get("name", self.name) or self.name
        self.di = _int_or(doc.get("di", 0), 0)
        self.do = _int_or(doc.get("do", 0), 0)
        count = _int_or(doc.get("count", 0), 0)
        if count:
            self.count = min(count, proto.HSF_DIGITAL_CHANNELS)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        # [缺省按全量处理] 旧服务端不发 `full` 键，但它**每拍都带名字**，
        # 按全量处理才对得上；新服务端显式给 False 时才跳过名字解析。
        full = bool(doc.get(proto.KEY_FULL, True))
        if full:
            di_names, do_names = {}, {}
            for key, value in doc.items():
                if key.startswith("din"):
                    try:
                        di_names[int(key[3:])] = str(value)
                    except (ValueError, TypeError):
                        continue
                elif key.startswith("don"):
                    try:
                        do_names[int(key[3:])] = str(value)
                    except (ValueError, TypeError):
                        continue
            self.di_names = di_names
            self.do_names = do_names
            self.got_full = True
        self.updated_ms = _now_ms()

    def di_name(self, index):
        return self.di_names.get(int(index), "")

    def do_name(self, index):
        return self.do_names.get(int(index), "")

    def is_fresh(self):
        return (self.valid and self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def di_bit(self, index):
        return bool(self.di & (1 << int(index)))

    def do_bit(self, index):
        return bool(self.do & (1 << int(index)))

    def to_json(self):
        fresh = self.is_fresh()
        count = max(0, self.count)
        di = [{"index": i,
               "on": bool(self.di & (1 << i)),
               "name": self.di_names.get(i, "")} for i in range(count)]
        do = [{"index": i,
               "on": bool(self.do & (1 << i)),
               "name": self.do_names.get(i, "")} for i in range(count)]
        return {
            "kind": "digital",
            "index": self.index,
            "name": self.name or ("数字量%d" % (self.index + 1)),
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": self.stale_ms,
            "valid": bool(self.valid),
            "count": count,
            "diMask": self.di,
            "doMask": self.do,
            "di": di,
            "do": do,
            "gotFull": self.got_full,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              流量计
# ============================================================================

class FlowMeterState(object):
    """电磁流量计快照。"""

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
        self.flow_unit = _int_or(doc.get("flowUnit", 0xFF), 0xFF)
        self.total_unit = _int_or(doc.get("totalUnit", 0xFF), 0xFF)
        self.alarms = _int_or(doc.get("alarms", 0), 0)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.valid and self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def flow_unit_text(self):
        return FLOW_UNIT_TEXT.get(self.flow_unit, "?")

    def total_unit_text(self):
        return TOTAL_UNIT_TEXT.get(self.total_unit, "?")

    def to_json(self):
        fresh = self.is_fresh()
        return {
            "kind": "flowmeter",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": self.stale_ms,
            "flow": _round(self.flow, 4),
            "speed": _round(self.speed, 4),
            "forwardTotal": _round(self.forward_total, 2),
            "reverseTotal": _round(self.reverse_total, 2),
            "flowUnit": self.flow_unit_text(),
            "totalUnit": self.total_unit_text(),
            "alarms": self.alarms,
            "valid": bool(self.valid),
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              电磁阀
# ============================================================================

class ValveState(object):
    """6 路电磁阀状态（**只看结果，不看接线**）。

    [职责边界] 这里不带任何点位信息 —— 哪路阀接在哪个模块的哪个口，
    是服务端的事。网页版只关心：这一路叫什么、什么状态、能否操作。
    """

    def __init__(self, count=proto.HSF_VALVE_COUNT):
        self.count = count
        #: index → dict(name/openCmd/openFb/closeFb/state/ready/valid/text)
        self.items = {}
        self.updated_ms = 0
        #: 待显示的告警（服务端推来的；页面消费后取走）。
        self.alarms = []
        self.alarm_total = 0
        self.last_alarm_ms = 0

    def apply(self, doc):
        # [告警帧不能当状态帧处理] 服务端把"阀告警"也发在 TOPIC_VALVE_STATUS
        # 上（键空间 "ac"/"a0_*"，没有 count 也没有 v{i}_*）。若照状态帧
        # 解析，会把 items 清空 —— 界面上的阀会瞬间全变成"未配置"再恢复。
        # 故先认告警帧：交给 take_alarms，状态原样保留。
        if proto.KEY_ALARM_COUNT in doc:
            self.take_alarms(doc)
            return
        count = _int_or(doc.get(proto.KEY_VALVE_COUNT, 0), 0)
        if count:
            self.count = min(count, 32)
        items = {}
        for index in range(self.count):
            prefix = "v%d_" % index
            items[index] = {
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            }
        self.items = items
        self.updated_ms = _now_ms()

    def take_alarms(self, doc):
        """收集一帧告警（一帧可含多条）。

        [不做任何判断] 超没超时、要不要报警，全部由服务端决定；
        这里只把内容攒起来给界面显示。两边各算一套必然对不上。
        """
        count = _int_or(doc.get(proto.KEY_ALARM_COUNT, 0), 0)
        added = 0
        for i in range(count):
            prefix = "a%d_" % i
            alarm = {
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            }
            # 去重：同一 (路号/类型) 在极短时间内重复到达时不重复入队。
            # 服务端可能同时在两个主题上各发一份（见 proto.TOPIC_VALVE_ALARM
            # 的说明），用户不该看到两条一模一样的报警。
            sig = (alarm.get(proto.KEY_ALARM_VALVE_INDEX),
                   alarm.get(proto.KEY_ALARM_KIND))
            now = _now_ms()
            if any(a.get("_sig") == sig and now - a.get("_at", 0) < 1500
                   for a in self.alarms):
                continue
            alarm["_sig"] = sig
            alarm["_at"] = now
            self.alarms.append(alarm)
            added += 1
        self.alarm_total += added
        if added:
            self.last_alarm_ms = _now_ms()
        return added

    def drain_alarms(self):
        """取走待显示告警（取走即清空，避免界面每次刷新都重复弹同一条）。"""
        if not self.alarms:
            return []
        out, self.alarms = self.alarms, []
        return out

    def is_fresh(self, stale_ms=5000):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < stale_ms)

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def state_of(self, index):
        item = self.items.get(index)
        return _int_or(item.get("state", 0), 0) if item else 0

    def open_count(self):
        return sum(1 for i in self.items.values()
                   if _int_or(i.get("state", 0), 0) == 2)

    def fault_count(self):
        return sum(1 for i in self.items.values()
                   if _int_or(i.get("state", 0), 0) == 4)

    def ready_count(self):
        return sum(1 for i in self.items.values() if i.get("ready"))

    def to_json(self):
        fresh = self.is_fresh()
        valves = []
        for index in range(self.count):
            item = self.items.get(index, {})
            state = _int_or(item.get("state", 0), 0)
            text, tone = VALVE_STATE_TEXT.get(state, ("未知", "unknown"))
            ready = bool(item.get("ready", False))
            valves.append({
                "index": index,
                # [未配置优先] 未配置时不能用状态文字（会让人以为阀坏了）
                "name": item.get("name") or ("电磁阀 %d" % (index + 1)),
                "ready": ready,
                "state": state,
                "stateText": text if ready else "未配置",
                "tone": tone if ready else "unconfigured",
                "openCmd": bool(item.get("openCmd", False)),
                "openFb": bool(item.get("openFb", False)),
                "closeFb": bool(item.get("closeFb", False)),
                "valid": bool(item.get("valid", False)),
                "text": item.get("text", "") or "",
            })
        return {
            "kind": "valve",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": 5000,
            "count": self.count,
            "openCount": self.open_count(),
            "faultCount": self.fault_count(),
            "readyCount": self.ready_count(),
            "valves": valves,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              设备健康
# ============================================================================

class DeviceHealth(object):
    """单条设备健康条目。"""

    def __init__(self):
        self.name = ""
        self.kind = 0
        self.side = 0
        #: 缺省按离线（服务端没给 = 不知道，按最保守处理）。
        self.health = proto.HEALTH_OFFLINE
        self.error = 0
        self.online = False
        self.run_state = 0
        self.text = ""

    def health_text(self):
        return HEALTH_TEXT.get(self.health, "未知")

    def kind_text(self):
        return KIND_TEXT.get(self.kind, "未知")

    def to_json(self):
        return {
            "name": self.name,
            "kind": self.kind,
            "kindText": self.kind_text(),
            "side": self.side,
            "health": self.health,
            "healthText": self.health_text(),
            "error": self.error,
            "online": bool(self.online),
            "runState": self.run_state,
            "text": self.text,
        }


class HealthSnapshot(object):
    """全量设备健康快照。"""

    def __init__(self):
        self.devices = []
        self.seq = 0
        self.ts = 0
        self.updated_ms = 0

    def apply(self, doc):
        n = _int_or(doc.get(proto.KEY_DEV_COUNT, 0), 0)
        self.seq = _int_or(doc.get(proto.KEY_DEV_SEQ, 0), 0)
        self.ts = _int_or(doc.get(proto.KEY_DEV_TS, 0), 0)
        devices = []
        for i in range(n):
            d = DeviceHealth()
            d.name = doc.get("d%d_name" % i, "") or ""
            d.kind = _int_or(doc.get("d%d_kind" % i, 0), 0)
            d.side = _int_or(doc.get("d%d_side" % i, 0), 0)
            # [不能写 `or 3`] health=0 是**正常**，而 `0 or 3` 得 3（离线）——
            # 这么写会把所有健康设备显示成离线（客户端踩过的真锅）。
            d.health = _int_or(doc.get("d%d_health" % i, proto.HEALTH_OFFLINE),
                               proto.HEALTH_OFFLINE)
            d.error = _int_or(doc.get("d%d_error" % i, 0), 0)
            d.online = bool(doc.get("d%d_online" % i, False))
            d.run_state = _int_or(doc.get("d%d_runState" % i, 0), 0)
            d.text = doc.get("d%d_text" % i, "") or ""
            devices.append(d)
        self.devices = devices
        self.updated_ms = _now_ms()

    def counts(self):
        """(故障数, 离线数) —— 供标题栏显示「故障N/离线N」。"""
        fault = sum(1 for d in self.devices
                    if d.health == proto.HEALTH_FAULT)
        offline = sum(1 for d in self.devices
                      if d.health == proto.HEALTH_OFFLINE)
        return fault, offline

    def find(self, kind, side=0):
        for d in self.devices:
            if d.kind == kind and d.side == side:
                return d
        return None

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def to_json(self):
        fault, offline = self.counts()
        return {
            "kind": "health",
            "fresh": self.age_ms() >= 0 and self.age_ms() < 10000,
            "ageMs": self.age_ms(),
            "seq": self.seq,
            "ts": self.ts,
            "count": len(self.devices),
            "faultCount": fault,
            "offlineCount": offline,
            "devices": [d.to_json() for d in self.devices],
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              总模型
# ============================================================================

class AppModel(object):
    """全部设备状态 + 运行模式（单一数据源，所有浏览器共享）。

    [为什么所有浏览器共享一份] 上游只有一条 XLink 连接（见 xlink.py 的
    模块文档），收到的推送解一次就存在这里，所有 SSE 订阅者读同一份。
    这也保证了两个浏览器看到的数值**必然一致** —— 若各解各的，
    浮点舍入或丢帧会让两边显示的读数不一样，现场会当成"系统不稳"。
    """

    def __init__(self):
        self.vfd = VfdState()
        self.temphum = TempHumState()
        self.analog = AnalogState()
        self.flowmeter = FlowMeterState()
        self.digital = {}          # index → DigitalState
        self.valve = ValveState()
        self.health = HealthSnapshot()
        self.run_mode = 0          # 0=手动 1=自动
        self.run_mode_known = False
        self.run_mode_ms = 0

    def digital_state(self, index):
        """取（必要时创建）某模块的数字量状态。"""
        index = int(index)
        dev = self.digital.get(index)
        if dev is None:
            dev = DigitalState(index)
            self.digital[index] = dev
        return dev

    def digital_count(self):
        return max(self.digital.keys()) + 1 if self.digital else 0

    def is_auto(self):
        return self.run_mode == 1

    def set_run_mode(self, mode, auto=None):
        self.run_mode = int(mode)
        if auto is not None:
            # [以服务端回的 auto 为准] 服务端可能把 mode 归一到 0/1，
            # 本地再算一次容易和它对不上。
            self.run_mode = 1 if auto else 0
        self.run_mode_known = True
        self.run_mode_ms = _now_ms()

    def reset_live(self):
        """链路断开时把"实时"部分清成未知。

        [为什么要清] 断线后推送停止，各 State 的 `updated_ms` 不再刷新，
        `is_fresh()` 自然变假 —— 数值会显示 `--`。但 `run_mode` /
        `health` 这类**非周期**数据没有新鲜度保护，不清就会一直显示
        断线前的状态（自动模式还显示"自动"，操作员据此判断能不能动手，
        而实际上根本连不上服务端）。
        """
        self.run_mode_known = False
        self.health = HealthSnapshot()
        self.valve = ValveState()

    def snapshot(self):
        """全量快照（GET /api/state 用）。"""
        return {
            "vfd": self.vfd.to_json(),
            "temphum": self.temphum.to_json(),
            "analog": self.analog.to_json(),
            "flowmeter": self.flowmeter.to_json(),
            "digital": [self.digital[k].to_json()
                        for k in sorted(self.digital.keys())],
            "valve": self.valve.to_json(),
            "health": self.health.to_json(),
            "mode": {
                "runMode": self.run_mode,
                "auto": self.is_auto(),
                "known": self.run_mode_known,
            },
        }
