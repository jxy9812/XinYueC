# -*- coding: utf-8 -*-
"""设备数据模型：把服务端推送的 BSON 解成结构快照，并导出为 JSON。

与 client_py 的 `hsfsrv_client/model.py` 的关系
================================================

本文件是**同一份模型**的网页版改写，三处差异：

1) **多了一个 `to_json()`**
   Qt 版页面直接读 Python 属性（`st.output_freq_hz`）；网页版要把数据
   过 HTTP/SSE 送出去，必须有明确的 JSON 契约。导出结构一经确定就是
   前后端的接口，改它等于改协议 —— 故集中在这里，不散在路由里。

2) **保留"服务端给了哪些键"的语义（`present` / 缺键=None）**
   Qt 版用 `has("outputFreqHz")` 决定显示数值还是 `--`。网页版同样需要：
   品牌没提供的量导出 None，**不能显示 0**（0 是"读到了且为零"，
   与"这台设备没有这个量"是两个完全不同的意思）。

3) **新鲜度叠加本地时钟**
   服务端给了 `valid`/`staleMs`，但断线后推送停止，`valid` 仍是最后一次
   的值。故模型层额外记录"本地最后更新时间"并叠加本地新鲜度。

[按侧分存 + 择优（2026-11-13 对齐客户端实锅口径）]
  服务端按 485 的**两条侧**轮流推**同一个主题**：装了设备的侧推真值，
  没装的侧推**空帧**（valid=false / 全 offline，没有值键）。只留最后一帧
  会被空帧盖掉 ⇒ 页面恒 `--`（Python 客户端温湿度/浊度都踩过）。
  故温湿度（优先 A 侧）、流量计/浊度（优先 B 侧）都按侧分存、择优取数；
  「无效帧不顶替」「每侧只留最近一帧」；挑不到 = 显示 `--`，不拿旧值冒充。

[为什么不直接透传 BSON 的 doc]
  网页端要显示的是**加工后**的量：0.1 单位的温湿度要除以 10、浊度 ÷100、
  微安要换毫安、位图要拆成通道数组、`state` 整数要映射成中文。
  把这些放前端意味着浏览器要懂 BSON 键名与设备口径；放后端则协议改动
  只动这一个文件。

[时间字段] 所有 `updated_ms` 都是**单调毫秒**（time.monotonic），
只用于算"多久没更新"，**不能**当墙上时间。给前端显示的年龄用导出结构
里的 `ageMs`（服务端在算，前端不用再算差值）。
"""

import time

from . import proto


#: 侧号（对齐服务端 HsfSide_A/B 与 C 端 HSF_SIDE_A/B：0=A，1=B）。
SIDE_A = 0
SIDE_B = 1


def _now_ms():
    """单调毫秒（只用于算间隔，不是墙上时间）。"""
    return int(time.monotonic() * 1000)


def wall_ms():
    """墙上时间毫秒（给前端做时间戳）。"""
    return int(time.time() * 1000)


def _pick_side(sides, prefer, predicate):
    """按「优先侧 + 兜底侧」顺序挑第一个满足条件的侧快照；挑不到返回 None。"""
    for side in prefer:
        st = sides.get(side)
        if st is not None and predicate(st):
            return st
    return None


# ============================================================================
#                              文本映射表
# ============================================================================

#: XVfdRunState → 中文（与 client_py/model.py RUN_STATE_TEXT 逐项一致）。
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

#: 设备类别 → 中文（与 client model.KIND_TEXT 逐项一致；漏一个显示"未知"）。
KIND_TEXT = {0: "变频器", 1: "温湿度", 2: "流量计", 3: "模拟量",
             4: "485总线", 5: "数字量", 6: "电动球阀组", 7: "水质",
             8: "相机"}

#: 告警**来源类别**（服务端 alarms 表的 kind 列）→ 「类型」列文案。
#:
#: [三端同源] 与桌面客户端 client_py/hsfsrv_client/model.py 的
#: ALARM_SOURCE_KIND_TEXT **逐项一致**（主页「最近报警」卡与报警页都用它
#: ⇒ 两处显示同一个词）。
ALARM_SOURCE_KIND_TEXT = {
    "auto": "自动运行",
    "valve": "电动球阀组",
    "flowmeter": "流量计",
    "device": "设备故障",
    "analog": "模拟量",
    "digital": "数字量",
    "health": "设备异常",
}


def alarm_type_text(kind, dev=None):
    """告警的「类型」文案（dev 设备类别优先，其次来源类别）。

    :param kind: 服务端 alarms 表的来源类别（health/auto/valve/…）。
    :param dev: 设备类别键（proto.KIND_*；None = 服务端没带）。

    [为什么 dev 优先] 带 dev 的行是**具体设备**出的事（流量计/变频器/…），
    比笼统的来源类别（"设备故障"）更贴近现场口径 —— 与报警页的类型列
    **同一套判据**（client_py model.alarm_type_text 同款）。
    """
    if dev is not None:
        try:
            text = KIND_TEXT.get(int(dev))
        except (TypeError, ValueError):
            text = None
        if text:
            return text
    return ALARM_SOURCE_KIND_TEXT.get(str(kind or ""), "自动运行")


def slim_addr(text):
    """完整连接地址 → **紧凑显示**（title/复制仍用完整值）。

    [与桌面客户端逐字同款] client_py/hsfsrv_client/model.py 的 slim_addr
    —— 主页设备状态表的地址列宽有限，完整串 "COM17 · 115200 · 站 2"
    （21 字符）会被截成 "COM17 · 115200 · ..."（**站号恰恰在最右**）。

    [瘦身规则（三端同款）]
        "COM17 · 115200 · 站 2"  → "COM17/115200/#2"
        "192.168.0.13:502"       → 不变
        "192.168.0.13:9600,8,N"   → "192.168.0.13:9600"（串口参数后缀）
        "192.168.0.13:502 · DO3" → "192.168.0.13:502/DO3"

    [完整值不能丢] 排障第一眼要看的正是完整地址 ⇒ 完整串进 **title 属性**
    + **点击复制**（见 page00_home.js 的设备状态表）。
    """
    s = str(text or "").strip()
    if not s:
        return ""
    # ① 剥掉 Modbus TCP 的串口参数后缀（,8,N 之类）——地址本身不因此改变
    if "," in s:
        head, _sep, tail = s.rpartition(",")
        # 形如 "9600,8,N" 的尾巴：末段是数据位/校验位 ⇒ 只留端口
        if head and head.rsplit(":", 1)[-1].isdigit() and \
                tail.count(",") == 0 and len(tail) <= 2:
            s = head
    # ② 分隔符瘦身：" · " → "/"
    s = s.replace(" · ", "/")
    # ③ 站号瘦身："站 2" → "#2"
    s = s.replace("站 ", "#")
    return s


#: 电动球阀状态 → (中文, 语义色 tone)。
#:
#: [Unknown 的语义] 它表示"状态未知"，**不是**"未配置"——
#: 未配置由独立的 ready 字段表达。点位配好但行程开关没接 → 两个反馈
#: 恒为 0 → 状态 Unknown，说"未配置"会把人引到错误的排查方向。
VALVE_STATE_TEXT = {
    0: ("状态未知", "unknown"),
    1: ("已关", "closed"),
    2: ("已开", "open"),
    3: ("动作中", "moving"),
    4: ("故障", "fault"),
}

#: 控制命令的处置结果 → 状态栏文案（与服务端 valves.VALVE_CMD_TEXT /
#: client pages_valve.VALVE_CMD_TEXT 同口径）。
VALVE_CMD_TEXT = {
    "issued": "命令已下发",
    "repeated": "动作中，重复命令不重置计时",
    "reversed": "已反向（重新计时）",
    "ignored": "已在目标位置，无需动作",
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

#: 流量计报警位 → 中文（位定义照说明书；展示顺序照 C 端 fmAlarmText：
#: 空管 → 上限 → 下限 → 系统。与 client model.FLOW_ALARM_TEXT 逐字一致）。
FLOW_ALARM_TEXT = (
    (1 << 2, "空管"),      # 0x1024 空管报警
    (1 << 0, "上限"),      # 0x1022 上限报警
    (1 << 1, "下限"),      # 0x1023 下限报警
    (1 << 3, "系统"),      # 0x1025 系统报警
)


def flow_alarm_text(bits):
    """流量计报警位图 → 中文（0 → 「正常」）。未知位保留十六进制尾巴。"""
    bits = int(bits or 0)
    if not bits:
        return "正常"
    names = [txt for mask, txt in FLOW_ALARM_TEXT if bits & mask]
    known = 0
    for mask, _txt in FLOW_ALARM_TEXT:
        known |= mask
    unknown = bits & ~known
    if unknown:
        names.append("0x%04X" % unknown)
    return " ".join(names) if names else "0x%04X" % bits


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
    必须能区分。
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
        """运行状态文案。

        [「减速中」有两种含义，要分开说] 服务端把两种减速都归到
        RUN_DECELERATING，但操作员看到的应是不同的词：
            · 运行中降给定 → 「减速中」（还在转，只是变慢）
            · 已发停止命令 → 「正在停机」（目标就是停）
        判据用 `running` 位：状态字仍报运行 = 还在正常降速；
        状态字已报停机 = 那是停机过程。
        """
        if self.run_state == proto.RUN_DECELERATING and not self.running:
            return "正在停机"
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
        est = self.rpm_estimate()
        return {
            "kind": "vfd",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": 5000,
            "runState": self.run_state,
            "runStateText": self.run_state_text(),
            "stopping": bool(self.run_state == proto.RUN_DECELERATING
                             and not self.running),
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
            "speedRpm": round(est) if est is not None else None,
            # 转速是不是服务端实测给的（界面上要标"换算值"）
            "speedFromServer": self.has("speedRpm"),
            "validMask": self.valid_mask,
            "ratedHz": self.rated_hz,
            "ratedRpm": self.rated_rpm,
            "ratedKnown": self.rated_known,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              温湿度（按侧分存）
# ============================================================================

class TempHumSideState(object):
    """单侧温湿度快照（某一侧最近一帧）。值以 0.1 为单位（217 = 21.7℃）。

    [为什么要按侧分存] 服务端按 485 的两条侧轮流推**同一个主题**：
    装了温湿度站的那一侧推真值，没装的那一侧推**空帧**（只有
    tempValid=false/humValid=false，没有 temp/hum 键）。只留最后一帧
    会被空帧盖掉 ⇒ 界面恒 "--"（Python 客户端实锅）。
    """

    def __init__(self, side=SIDE_A):
        self.side = int(side)
        self.temp_tenths = None
        self.hum_tenths = None
        self.temp_valid = False
        self.hum_valid = False
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        # 值键只在有效时出现：没有就是「无数据」，不能当成 0。
        self.temp_tenths = doc.get("temp")
        self.hum_tenths = doc.get("hum")
        self.temp_valid = bool(doc.get("tempValid", False))
        self.hum_valid = bool(doc.get("humValid", False))
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def has_value(self):
        """这一侧此刻有没有有效值（温或湿任一有效即可）。"""
        return bool(self.temp_valid or self.hum_valid)

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
            "side": self.side,
            "fresh": fresh,
            "ageMs": (_now_ms() - self.updated_ms) if self.updated_ms else -1,
            "staleMs": self.stale_ms,
            "tempC": temp,
            "humPct": hum,
            "tempValid": bool(self.temp_valid),
            "humValid": bool(self.hum_valid),
            "updatedMs": self.updated_ms,
        }


class TempHumState(object):
    """温湿度快照（**按侧分存 + 择优取数**）。

    [取数口径] 「优先侧 + 兜底侧」：优先 A 侧（现场温湿度站挂 A 侧），
    A 侧不新鲜/无有效值才看 B 侧；两边都没有 ⇒ None ⇒ 显示 "--"。
    与 C 端 hsfHomePickTempHum 同口径。
    """

    #: 优先侧 + 兜底侧（现场温湿度站挂 A 侧）。
    PREFER = (SIDE_A, SIDE_B)

    def __init__(self):
        #: side -> TempHumSideState（**每侧只留最近一帧**）。
        self.sides = {}

    def apply(self, doc):
        side = _int_or(doc.get("side", 0), 0)
        if side not in (SIDE_A, SIDE_B):
            return                      # 非法侧号：整帧丢弃（口径同 C 端）
        st = self.sides.get(side)
        if st is None:
            st = TempHumSideState(side)
            self.sides[side] = st
        st.apply(doc)

    def pick(self, prefer=None):
        """挑一个可用快照（优先侧 + 兜底侧）；挑不到返回 None。"""
        return _pick_side(self.sides, prefer or self.PREFER,
                          lambda st: st.is_fresh() and st.has_value())

    def is_fresh(self):
        """任一侧有新鲜帧（哪怕这一帧没有有效值）。"""
        return any(st.is_fresh() for st in self.sides.values())

    def age_ms(self):
        best = None
        for st in self.sides.values():
            if best is None or st.updated_ms > best:
                best = st.updated_ms
        return (_now_ms() - best) if best else -1

    def temp_text(self):
        st = self.pick()
        return st.temp_text() if st is not None else "--"

    def hum_text(self):
        st = self.pick()
        return st.hum_text() if st is not None else "--"

    def to_json(self):
        st = self.pick()
        base = st.to_json() if st is not None else {
            "side": None, "fresh": False, "ageMs": self.age_ms(),
            "staleMs": 5000, "tempC": None, "humPct": None,
            "tempValid": False, "humValid": False, "updatedMs": 0,
        }
        base["kind"] = "temphum"
        base["tempText"] = self.temp_text()
        base["humText"] = self.hum_text()
        return base


# ============================================================================
#                              模拟量
# ============================================================================

class AnalogState(object):
    """多路 4~20mA 模拟量快照。

    [换算值由服务端给] 量程是**现场标定参数**，客户端只负责按
    e*/eu*/en*/ew* 显示。

    [为什么同时留原始 mA] 原始值是唯一无歧义的真相：排查时按它判断
    是否低于量程下限（<4mA = **负压**，见服务端 engineering 的口径）/超量程。
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

    [点数由服务端决定] 单模块物理 16 点，但模块支持主从聚合：对外是
    32 点。写死 16 会让上半区永远看不到，而服务端明明在发。
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
        #: 知道。拿到什么显示什么。
        self.di_names = {}
        self.do_names = {}
        #: 是否收到过带名字的帧（诊断用）。
        self.got_names = False

    def apply(self, doc):
        """合并一帧。

        [名字三态保留——帧里没有名字键绝不擦缓存]
        周期帧不带 din*/don*，只有订阅补发/首连/快照应答才是全量帧。
        若每拍都重建名字表，名字会在「有→无→有」之间抖动。与客户端
        同口径（更保守）：连「全量帧却没带名字」的帧也不擦缓存。
        """
        self.name = doc.get("name", self.name) or self.name
        self.di = _int_or(doc.get("di", 0), 0)
        self.do = _int_or(doc.get("do", 0), 0)
        count = _int_or(doc.get("count", 0), 0)
        if count:
            self.count = min(count, proto.HSF_DIGITAL_CHANNELS)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
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
        if di_names or do_names:
            self.di_names = di_names
            self.do_names = do_names
            self.got_names = True
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
        width = 8 if count > 16 else 4
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
            "hexWidth": width,
            "diMask": self.di,
            "doMask": self.do,
            "diText": ("0x%0*X" % (width, self.di & ((1 << count) - 1))
                       if count else "--"),
            "doText": ("0x%0*X" % (width, self.do & ((1 << count) - 1))
                       if count else "--"),
            "di": di,
            "do": do,
            "gotNames": self.got_names,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              流量计（按侧分存）
# ============================================================================

class FlowMeterSideState(object):
    """单侧流量计快照（某一侧最近一帧）。"""

    def __init__(self, side=SIDE_A):
        self.side = int(side)
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
        # [无效帧不顶替] valid=false 时数值键缺失，沿用缺省 0 会让界面
        # 显示假 0；本侧的"无效"由 is_fresh()（valid 位）表达。
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

    def flow_unit_text(self):
        return FLOW_UNIT_TEXT.get(self.flow_unit, "?")

    def total_unit_text(self):
        return TOTAL_UNIT_TEXT.get(self.total_unit, "?")


class FlowMeterState(object):
    """电磁流量计快照（**按侧分存 + 择优取数**）。

    [取数口径] 优先 B 侧（现场流量计挂 B 侧），B 侧不新鲜/无效才看 A 侧；
    两边都没有 ⇒ None ⇒ 界面显示 "无数据"。与 C 端 hsfHomePickFlowMeter
    同口径。
    """

    #: 优先侧 + 兜底侧（现场流量计挂 B 侧）。
    PREFER = (SIDE_B, SIDE_A)

    def __init__(self):
        self.sides = {}

    def apply(self, doc):
        side = _int_or(doc.get("side", 0), 0)
        if side not in (SIDE_A, SIDE_B):
            return
        st = self.sides.get(side)
        if st is None:
            st = FlowMeterSideState(side)
            self.sides[side] = st
        st.apply(doc)

    def pick(self, prefer=None):
        """挑一个可用快照；挑不到返回 None。

        [无效帧不顶替] valid=false = 「此刻无有效值」，那一侧直接跳过去
        看兜底侧；两侧都无效 ⇒ None ⇒ "无数据"。
        """
        return _pick_side(self.sides, prefer or self.PREFER,
                          lambda st: st.is_fresh())

    def is_fresh(self):
        return self.pick() is not None

    def age_ms(self):
        best = None
        for st in self.sides.values():
            if best is None or st.updated_ms > best:
                best = st.updated_ms
        return (_now_ms() - best) if best else -1

    # ---- 取值一律走「被选中的一侧」；挑不到 = 无数据 ----
    @property
    def flow(self):
        st = self.pick()
        return st.flow if st is not None else None

    @property
    def speed(self):
        st = self.pick()
        return st.speed if st is not None else None

    @property
    def forward_total(self):
        st = self.pick()
        return st.forward_total if st is not None else None

    @property
    def reverse_total(self):
        st = self.pick()
        return st.reverse_total if st is not None else None

    @property
    def flow_unit(self):
        st = self.pick()
        return st.flow_unit if st is not None else 0xFF

    @property
    def total_unit(self):
        st = self.pick()
        return st.total_unit if st is not None else 0xFF

    @property
    def alarms(self):
        st = self.pick()
        return st.alarms if st is not None else 0

    @property
    def valid(self):
        st = self.pick()
        return bool(st.valid) if st is not None else False

    @property
    def stale_ms(self):
        st = self.pick()
        return st.stale_ms if st is not None else 5000

    @property
    def updated_ms(self):
        st = self.pick()
        return st.updated_ms if st is not None else 0

    def to_json(self):
        st = self.pick()
        fresh = st is not None
        return {
            "kind": "flowmeter",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": self.stale_ms,
            "side": (st.side if st is not None else None),
            "flow": _round(self.flow, 4) if fresh else None,
            "speed": _round(self.speed, 4) if fresh else None,
            "forwardTotal": _round(self.forward_total, 2) if fresh else None,
            "reverseTotal": _round(self.reverse_total, 2) if fresh else None,
            "flowUnit": self.flow_unit,
            "flowUnitText": (st.flow_unit_text()
                             if st is not None else "?"),
            "totalUnit": self.total_unit,
            "totalUnitText": (st.total_unit_text()
                              if st is not None else "?"),
            "alarms": self.alarms if fresh else 0,
            "alarmText": (flow_alarm_text(self.alarms)
                          if fresh else "--"),
            "valid": self.valid,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              浊度仪（两台合帧，按侧分存）
# ============================================================================

class TurbiditySideState(object):
    """单侧浊度仪快照（某一侧最近一帧；v{index}_ 前缀，与阀/数字量同风格）。"""

    def __init__(self, side=SIDE_A):
        self.side = int(side)
        #: index -> {"name","ntu","raw","online","temp"}
        self.items = {}
        self.count = 0
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        count = _int_or(doc.get("count", 0), 0)
        self.count = count
        items = {}
        for idx in range(count):
            prefix = "v%d_" % idx
            raw = {}
            for key, value in doc.items():
                if key.startswith(prefix):
                    raw[key[len(prefix):]] = value
            items[idx] = raw
        self.items = items
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def has_online(self):
        """这一侧此刻有没有在线的浊度仪（= 这一帧带得出读数）。"""
        return any(bool(item.get("online")) for item in self.items.values())

    def name_of(self, index):
        return self.items.get(int(index), {}).get("name", "")

    def ntu_of(self, index):
        """浊度值（NTU，float）；无数据返回 None。"""
        item = self.items.get(int(index), {})
        if not item.get("online"):
            return None
        value = item.get("ntu")
        return None if value is None else float(value) / 100.0

    def raw_of(self, index):
        return self.items.get(int(index), {}).get("raw")

    def temp_of(self, index):
        """水温（℃；float 或 None）。

        [为什么可能没有] 只有**带温度的机型**才随帧带 "v{i}_temp"。
        **缺键 = 没有温度**，不是 0℃——显示 0℃ 会被当成真实读数。
        """
        item = self.items.get(int(index), {})
        if not item.get("online"):
            return None
        value = item.get("temp")
        if value is None:
            return None
        return float(value) / 100.0

    def online_of(self, index):
        return bool(self.items.get(int(index), {}).get("online"))


class TurbidityState(object):
    """两台浊度仪的合并状态（**按侧分存 + 择优取数**）。

    [为什么还要按侧分存] 服务端按两条侧轮流推同一个主题；没装配浊度仪的
    那一侧推「两台都 offline」的帧，只留最后一帧 ⇒ 读数被盖掉 ⇒ 首页浊度
    卡恒 "--"。两台浊度仪在**同一侧**，按侧分存后取那一侧即可。

    [每侧只留最近一帧] 不缓存历史帧 ⇒ 内存不随运行时长增长。
    """

    #: 优先侧 + 兜底侧（现场两台浊度仪挂 B 侧）。
    PREFER = (SIDE_B, SIDE_A)

    def __init__(self):
        self.sides = {}

    def apply(self, doc):
        side = _int_or(doc.get("side", 0), 0)
        if side not in (SIDE_A, SIDE_B):
            return
        st = self.sides.get(side)
        if st is None:
            st = TurbiditySideState(side)
            self.sides[side] = st
        st.apply(doc)

    def pick(self, prefer=None):
        """挑一个可用快照；挑不到返回 None。

        [两轮择优] 第一轮挑「有在线台」的那一侧（那才是真读数）；一轮都
        没挑到（所有台都离线）时退到第一帧**新鲜**的 —— 这样界面照样显示
        仪器名 + "--"，并把「从站失联」与「服务端没装配」区分开。
        """
        prefer = prefer or self.PREFER
        st = _pick_side(self.sides, prefer,
                        lambda s: s.is_fresh() and s.has_online())
        if st is not None:
            return st
        return _pick_side(self.sides, prefer, lambda s: s.is_fresh())

    def is_fresh(self):
        return self.pick() is not None

    def age_ms(self):
        best = None
        for st in self.sides.values():
            if best is None or st.updated_ms > best:
                best = st.updated_ms
        return (_now_ms() - best) if best else -1

    def to_json(self):
        st = self.pick()
        count = st.count if st is not None else 0
        items = []
        for i in range(count):
            online = st.online_of(i) if st is not None else False
            temp = st.temp_of(i) if st is not None else None
            raw_item = (st.items.get(i, {}) if st is not None else {})
            items.append({
                "index": i,
                "name": st.name_of(i) if st is not None
                        else ("浊度仪%d" % (i + 1)),
                "online": online,
                "ntu": (_round(st.ntu_of(i), 2)
                        if st is not None else None),
                "raw": (raw_item.get("raw") if online else None),
                # [缺键=无温度机型] hasTemp=False ⇒ 界面不显示水温行，
                # 不能显示 0℃。
                "temp": _round(temp, 1) if temp is not None else None,
                "hasTemp": temp is not None,
            })
        return {
            "kind": "turbidity",
            "fresh": st is not None,
            "ageMs": self.age_ms(),
            "staleMs": st.stale_ms if st is not None else 5000,
            "side": (st.side if st is not None else None),
            "count": count,
            "anyOnline": any(it["online"] for it in items),
            "items": items,
            "updatedMs": st.updated_ms if st is not None else 0,
        }


# ============================================================================
#                              进水泵（只有输出，无反馈）
# ============================================================================

class PumpState(object):
    """进水泵状态（v{index}_ 前缀）。

    [只有输出，没有反馈] `on` 表示"输出是开着的"，不是"泵真的在转"。
    界面必须说清这一点，否则会让人以为这是实测运行状态。
    """

    def __init__(self):
        #: index -> {"name","on","ready","valid","text"}
        self.items = {}
        self.count = 0
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        count = _int_or(doc.get("count", 0), 0)
        self.count = count
        items = {}
        for idx in range(count):
            prefix = "v%d_" % idx
            raw = {}
            for key, value in doc.items():
                if key.startswith(prefix):
                    raw[key[len(prefix):]] = value
            items[idx] = raw
        self.items = items
        self.stale_ms = _int_or(doc.get("staleMs", 5000), 5000) or 5000
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def age_ms(self):
        return (_now_ms() - self.updated_ms) if self.updated_ms else -1

    def name_of(self, index):
        return self.items.get(int(index), {}).get("name", "")

    def is_on(self, index):
        return bool(self.items.get(int(index), {}).get("on"))

    def is_ready(self, index):
        return bool(self.items.get(int(index), {}).get("ready"))

    def to_json(self):
        fresh = self.is_fresh()
        items = []
        for i in range(self.count):
            on = self.is_on(i)
            ready = self.is_ready(i)
            if not ready:
                state_text = "未就绪"
            else:
                state_text = "运行（输出已开）" if on else "停止（输出已关）"
            items.append({
                "index": i,
                "name": self.name_of(i) or ("进水泵%d" % (i + 1)),
                "on": on,
                "ready": ready,
                # [口径注明] 泵只有输出点、无运行反馈；文案自带这层含义。
                "stateText": state_text if fresh else "--",
            })
        return {
            "kind": "pump",
            "fresh": fresh,
            "ageMs": self.age_ms(),
            "staleMs": self.stale_ms,
            "count": self.count,
            "items": items,
            "updatedMs": self.updated_ms,
        }


# ============================================================================
#                              电动球阀
# ============================================================================

class ValveState(object):
    """6 路电动球阀状态（**只看结果，不看接线**）。

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
        #: 最近一次控制命令的处置结果（hub 在控制应答里记）。
        #: {"index", "on", "cmdResult", "cmdText", "ok", "err", "atMs"}
        self.last_cmd = None

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

    def note_cmd(self, index, on, result, ok, err=""):
        """记录一次控制命令的处置结果（给界面状态行显示）。"""
        text = VALVE_CMD_TEXT.get(str(result or ""), "命令已受理")
        self.last_cmd = {
            "index": int(index),
            "on": bool(on),
            "cmdResult": str(result or ""),
            "cmdText": text,
            "ok": bool(ok),
            "err": str(err or ""),
            "atMs": _now_ms(),
        }

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
            # 去重：同一 (路号, 类型) 在极短时间内重复到达时不重复入队
            # （状态主题与遗留告警主题可能各发一份）。
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
                   if _int_or(i.get("state", 0), 0) == proto.HsfValveState.Open)

    def fault_count(self):
        return sum(1 for i in self.items.values()
                   if _int_or(i.get("state", 0), 0) == proto.HsfValveState.Fault)

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
                "name": item.get("name") or ("电动球阀%d" % (index + 1)),
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
        last_cmd = dict(self.last_cmd) if self.last_cmd else None
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
            "lastCmd": last_cmd,
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
        #: 连接地址（服务端给的展示写法）：TCP = "ip:port"，
        #: 485 = "COM17 · 115200 · 站 2"。老服务端不发该键 ⇒ 空串，界面显示 "-"。
        self.addr = ""
        #: **完整地址**（瘦身前的原串）——主页设备状态表的地址格显示瘦身串，
        #: 完整串进 title（悬停）+ 点击复制（触摸板没有 hover ⇒ 复制是兜底）。
        self.addr_full = ""

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
            "addr": self.addr,
            "addrFull": self.addr_full or self.addr,
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
            # [地址瘦身 2026-11-19] 显示用瘦身串、完整串另存（title + 复制）
            # —— 与桌面客户端 pages_other.HomePage._refresh_device_table 同款。
            d.addr_full = doc.get("d%d_addr" % i, "") or ""
            d.addr = slim_addr(d.addr_full)
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
        age = self.age_ms()
        return {
            "kind": "health",
            "fresh": age >= 0 and age < 10000,
            "ageMs": age,
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

class CameraState(object):
    """相机画面（**最新一帧的 JPEG 字节** + 元信息）。

    [为什么帧本体不进 SSE] SSE 是**文本**通道 ⇒ 把 JPEG base64 塞进去
    要 **+33% 体积**、每帧还要转一次 base64 ⇒ 帧本体留在 web 进程内存里，
    浏览器用 `<img src="/api/camera/frame">` 现取；SSE 只广播**元信息**
    （有新帧了 + 尺寸 + 序号）⇒ 页面据此刷新 img（带时间戳破缓存）。

    [为什么客户端不直连相机] 用户口径（2026-11-19）：网页端也要从服务端
    取数据（外网访问场景：浏览器在公网、相机在内网）⇒ 画面走 XLink 分片
    到本机，再由本机转成 image/jpeg 给浏览器。
    """

    def __init__(self):
        self.frame = b""        # 最新一帧 JPEG（字节）
        self.width = 0
        self.height = 0
        self.seq = 0            # 已收到的帧数（自增）
        self.ts_ms = 0          # 收到这一帧的时刻
        self.dropped = 0        # 组帧失败/丢片的次数
        #: [2026-11-19 ES 直传] 帧形态："jpeg"（旧路，<img> 贴）/ "es"
        #: （服务端直传 NAL，浏览器 WebCodecs 解码；NAL 队列在 hub，
        #: 浏览器走 /api/camera/es 增量取——画面本体照旧不过 SSE）。
        self.mode = "jpeg"
        self.codec = "h264"     # 实际编码（服务端 SDP 探测，随事件下发）

    def set_frame(self, data, width, height):
        self.frame = data
        self.width = int(width or 0)
        self.height = int(height or 0)
        self.seq += 1
        self.ts_ms = int(time.time() * 1000)
        self.mode = "jpeg"

    def to_json(self):
        return {"hasFrame": bool(self.frame), "width": self.width,
                "height": self.height, "seq": self.seq, "tsMs": self.ts_ms,
                "dropped": self.dropped, "bytes": len(self.frame),
                "mode": self.mode, "codec": self.codec}


class AppModel(object):
    """全部设备状态 + 运行模式（单一数据源，所有浏览器共享）。

    [为什么所有浏览器共享一份] 上游只有一条 XLink 连接（见 xlink.py 的
    模块文档），收到的推送解一次就存在这里，所有 SSE 订阅者读同一份。
    这也保证了两个浏览器看到的数值**必然一致**。
    """

    def __init__(self):
        self.vfd = VfdState()
        self.temphum = TempHumState()
        self.analog = AnalogState()
        self.flowmeter = FlowMeterState()
        self.turbidity = TurbidityState()   # 两台浊度仪（合并一帧）
        self.pump = PumpState()             # 进水泵（只有输出无反馈）
        self.digital = {}          # index → DigitalState
        self.valve = ValveState()
        self.camera = CameraState()   # 相机画面（最新一帧 JPEG + 元信息）
        self.health = HealthSnapshot()
        self.run_mode = 0          # 0=手动 1=自动
        self.run_mode_known = False
        self.run_mode_ms = 0
        #: 自动控制器是否真的在跑（None = 服务端没报，按"未知"显示）。
        #:
        #: [为什么不从 run_mode 推] 模式=自动 不代表 控制器在跑。分开存，
        #: 界面才能把"模式自动但控制器已停"这种异常如实显示出来。
        self.auto_running = None
        #: 当前（选中/正在运行的）自动模式 id（""=未选/未知）。
        self.auto_mode = ""
        #: 控制器相位 idle/valves/pumps/running/estop（""=未知）。
        self.auto_phase = ""
        #: 报警锁存（None=未知；True=有报警未复位，启动被拒）。
        self.auto_fault = None
        #: **累计自动运行时长**（毫秒）。服务端计（唯一事实源）。
        self.auto_run_ms = 0
        #: 是否**收到过**服务端的时长数据。auto_run_ms=0 有两种含义：
        #: "报了 0"（没跑过，该显示 0天00时…）与"还没收到"（该显示 --）。
        self.auto_run_ms_known = False
        #: **当前子阶段已运行时长**（毫秒；-1 = 不在运行/没有阶段）。
        self.auto_stage_elapsed_ms = -1
        #: **当前子阶段总时长**（毫秒；0 = 本阶段无定时 ⇒ 剩余不可知）。
        self.auto_stage_total_ms = 0
        #: 是否**收到过**阶段计时（老服务端不发 ⇒ 不编造倒计时）。
        self.auto_stage_known = False
        #: **最近一条报警**（2026-11-19 主页「最近报警」卡）。
        #:
        #: None = 从未收到过报警（或老服务端不发这个键）⇒ 界面显示
        #: 「无报警」。结构 = {"kind","dev","text","atMs"}（atMs = 墙钟
        #: epoch 毫秒 ⇒ 界面直接格式化 HH:MM:SS）。
        self.last_alarm = None
        #: 是否**收到过**服务端的 lastAlarm 键（老服务端不发 ⇒ 保持 None）。
        self.last_alarm_known = False

    #: 数字量模块数**硬上限**（模型层）。
    #:
    #: [为什么必须有上限] `digital_state()` 原来对**任意** index 都会新建
    #: 并永久保存一个 DigitalState ⇒ 一个只读账号（匿名角色也有 VIEW 位）
    #: 循环 POST 不同 dev 就能让服务端内存无界增长，且 /api/state 首帧
    #: JSON 一起膨胀。服务端配置里数字量模块通常 1~4 台 ⇒ 8 已足够宽裕，
    #: 超出即拒（不建对象、不广播）。
    MAX_DIGITAL_MODULES = 8

    def digital_state(self, index):
        """取（必要时创建）某模块的数字量状态；**越界返回 None**。

        [越界不建对象] 见 MAX_DIGITAL_MODULES 的说明。调用方必须处理
        None：推送侧遇到越界索引 ⇒ 丢弃该帧；API 侧 ⇒ 夹到合法模块。
        """
        try:
            index = int(index)
        except (TypeError, ValueError):
            return None
        if index < 0 or index >= self.MAX_DIGITAL_MODULES:
            return None
        dev = self.digital.get(index)
        if dev is None:
            if len(self.digital) >= self.MAX_DIGITAL_MODULES:
                return None
            dev = DigitalState(index)
            self.digital[index] = dev
        return dev

    def digital_count(self):
        return max(self.digital.keys()) + 1 if self.digital else 0

    def digital_module_count(self):
        """可用模块数（API 层夹 dev 的上界用）。

        [为什么不是 len(dict)] 推送乱序时字典可能留空洞（先收到模块 2
        再收到模块 0）⇒ 模块号空间是「最大下标 + 1」，不是字典长度。
        [还没收到任何推送时] 按 1 台算（模块 0 是合法下标；否则首帧的
        「请求快照」会被自己的闸拒掉）。
        """
        count = self.digital_count()
        if count <= 0:
            return 1
        return min(count, self.MAX_DIGITAL_MODULES)

    def is_auto(self):
        return self.run_mode == 1

    def is_auto_running(self):
        """自动控制器是否在跑；**未知时返回 None**（不是 False）。

        [为什么 None 不折成 False] 老服务端不报这个键。若折成 False，
        界面会在自动模式下显示"自动已停止"——那是**编造的故障**。
        """
        return self.auto_running

    def set_run_mode(self, mode, auto=None):
        self.run_mode = int(mode)
        if auto is not None:
            # [以服务端回的 auto 为准] 服务端可能把 mode 归一到 0/1。
            self.run_mode = 1 if auto else 0
        self.run_mode_known = True
        self.run_mode_ms = _now_ms()

    def apply_mode_doc(self, doc):
        """把 0x0105/0x0115 应答落到 model（**模式位与运行位分开读**）。

        [运行位缺省不改] 老服务端不发 `autoRunning`：此时保持原值（首次为
        None=未知），**不要**折成 False —— 那会在自动模式下显示"自动已停止"，
        是编造出来的故障。
        [自动模式三键同口径] autoMode/autoPhase/autoFault 缺键保持原值
        （老服务端兼容）。
        """
        self.set_run_mode(_int_or(doc.get("mode", 0), 0),
                          auto=bool(doc.get("auto", False))
                          if "auto" in doc else None)
        if proto.KEY_AUTO_RUNNING in doc:
            self.auto_running = bool(doc.get(proto.KEY_AUTO_RUNNING))
        if proto.KEY_AUTO_MODE in doc:
            self.auto_mode = str(doc.get(proto.KEY_AUTO_MODE) or "")
        if proto.KEY_AUTO_PHASE in doc:
            self.auto_phase = str(doc.get(proto.KEY_AUTO_PHASE) or "")
        if proto.KEY_AUTO_FAULT in doc:
            self.auto_fault = bool(doc.get(proto.KEY_AUTO_FAULT))
        if proto.KEY_AUTO_RUN_MS in doc:
            self.auto_run_ms = _int_or(doc.get(proto.KEY_AUTO_RUN_MS), 0)
            self.auto_run_ms_known = True
        # [子阶段计时] 两键同进同退：老服务端一个都不发 ⇒
        # auto_stage_known 保持 False ⇒ 界面显示「--」，不倒推假倒计时。
        if (proto.KEY_AUTO_STAGE_ELAPSED_MS in doc
                or proto.KEY_AUTO_STAGE_TOTAL_MS in doc):
            self.auto_stage_elapsed_ms = _int_or(
                doc.get(proto.KEY_AUTO_STAGE_ELAPSED_MS, -1), -1)
            self.auto_stage_total_ms = _int_or(
                doc.get(proto.KEY_AUTO_STAGE_TOTAL_MS, 0), 0)
            self.auto_stage_known = True
        # [最近报警 2026-11-19] 主页「最近报警」卡的一行摘要。**缺键不改
        # 原值**（老服务端不发 ⇒ last_alarm 保持 None ⇒ 界面显示
        # 「无报警」）；带了键但值是 null ⇒ 也是 None（服务端说"没有报警"
        # 就是没有，不编一条出来）。
        if proto.KEY_LAST_ALARM in doc:
            self.last_alarm = self._parse_last_alarm(
                doc.get(proto.KEY_LAST_ALARM))
            self.last_alarm_known = True

    @staticmethod
    def _parse_last_alarm(raw):
        """服务端 lastAlarm 文档 → model 的 dict（None = 无报警/坏数据）。

        [口径与桌面客户端同款] dev 缺键 = 无设备类别（**不补 0**：0 是
        "变频器"，补 0 会撒谎）；空文档 = 无报警。

        [typeText 后端算好] 「类型」文案在**后端**算（alarm_type_text）⇒
        前端直接显示，不必在 JS 里再抄一份映射表（抄一份 = 迟早漂移）。
        桌面端是 model 层算、前端同样直接显示 ⇒ 两端逐字一致。
        """
        if not isinstance(raw, dict):
            return None
        text = str(raw.get(proto.LAST_ALARM_KEY_TEXT) or "")
        at_ms = _int_or(raw.get(proto.LAST_ALARM_KEY_AT_MS), 0)
        if not text and not at_ms:
            return None
        kind = str(raw.get(proto.LAST_ALARM_KEY_KIND) or "")
        dev = raw.get(proto.LAST_ALARM_KEY_DEV)
        if dev is not None:
            try:
                dev = int(dev)
            except (TypeError, ValueError):
                dev = None
        out = {
            "kind": kind,
            "text": text,
            "atMs": at_ms,
            "typeText": alarm_type_text(kind, dev),
        }
        if dev is not None:
            out["dev"] = dev
        return out

    def mode_json(self):
        """运行模式 + 自动状态族（/api/mode、mode 事件、快照的 mode 节）。"""
        return {
            "runMode": self.run_mode,
            "auto": self.is_auto(),
            "known": self.run_mode_known,
            "autoRunning": self.auto_running,          # None = 未知（null）
            "autoMode": self.auto_mode,
            "autoPhase": self.auto_phase,
            "autoFault": self.auto_fault,              # None = 未知（null）
            "autoRunMs": self.auto_run_ms,
            "autoRunMsKnown": self.auto_run_ms_known,
            "autoStageElapsedMs": self.auto_stage_elapsed_ms,
            "autoStageTotalMs": self.auto_stage_total_ms,
            "autoStageKnown": self.auto_stage_known,
            # [最近报警 2026-11-19] 主页卡消费（None = 无报警）。
            "lastAlarm": self.last_alarm,
            "lastAlarmKnown": self.last_alarm_known,
        }

    def reset_live(self):
        """链路断开时把"实时"部分清成未知。

        [为什么要清] 断线后推送停止，各 State 的 `updated_ms` 不再刷新，
        `is_fresh()` 自然变假 —— 数值会显示 `--`。但 `run_mode` /
        `health` 这类**非周期**数据没有新鲜度保护，不清就会一直显示
        断线前的状态。
        """
        self.run_mode_known = False
        self.auto_running = None
        self.auto_mode = ""
        self.auto_phase = ""
        self.auto_fault = None
        self.auto_stage_elapsed_ms = -1
        self.auto_stage_total_ms = 0
        self.auto_stage_known = False
        # [最近报警 2026-11-19] 断链 ⇒ 报警摘要回"未知"（留着上一台服务
        # 端的报警会误导：那是**别的机器**上发生的事）。
        self.last_alarm = None
        self.last_alarm_known = False
        # [累计运行时长不清] 那是服务端持久化的累计值，不是"实时"数据；
        # 断线期间保留已收到的值（known 保持），重连后服务端会重新下发。
        self.health = HealthSnapshot()
        self.valve = ValveState()

    def snapshot(self):
        """全量快照（GET /api/state 与 SSE 首帧用）。"""
        return {
            "vfd": self.vfd.to_json(),
            "temphum": self.temphum.to_json(),
            "analog": self.analog.to_json(),
            "flowmeter": self.flowmeter.to_json(),
            "turbidity": self.turbidity.to_json(),
            "pump": self.pump.to_json(),
            "digital": [self.digital[k].to_json()
                        for k in sorted(self.digital.keys())],
            "valve": self.valve.to_json(),
            "health": self.health.to_json(),
            "mode": self.mode_json(),
            # [相机 2026-11-19] 只带**元信息**（帧本体在 /api/camera/frame
            # 取，SSE 是文本通道不能塞二进制）。
            "camera": self.camera.to_json(),
        }
