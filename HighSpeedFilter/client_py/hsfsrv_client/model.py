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

from . import proto


def _now_ms():
    return int(time.monotonic() * 1000)


def mono_to_epoch_ms(ms):
    """单调钟毫秒 → 墙钟毫秒（**渲染标签用**）。

    [为什么 2026-11-16 审计] 趋势图点时刻存的是单调钟（`updated_ms`，
    稳定、不随改系统时间跳）；但画轴/悬停原来直接把「开机毫秒」
    `localtime` 成了 "1970+开机时长" 的假时刻（板端开机几天后轴上
    根本读不出真实时间）。渲染时用 `time.time()-time.monotonic()` 的
    进程内偏移换算回真实墙钟——偏移恒定（NTP 微调对"读分秒"无感）；
    数据侧的窗口/判重逻辑不受影响。
    """
    try:
        return int(ms + (time.time() - time.monotonic()) * 1000.0)
    except (TypeError, ValueError, OSError):
        return int(ms)


def _f_or_none(value):
    """宽松取数值：能 float 就 float，不能就 None（**不抛**）。

    [为什么 2026-11-16 审计] 推送帧的值字段（temp/hum/ntu…）原来原样
    存、界面用时才 `x / 10.0` ⇒ 服务端某帧发成字符串/容器（或第三方
    网关接入）后，异常滞留在 model 里 ⇒ 每拍 refresh 抛 TypeError ⇒
    首页从异常点起整页冻结 + "刷新失败"刷屏（`_refresh_chrome` 路径
    连兜都没有）。⇒ 进 model 时就收敛成 float/None，坏值按"无数据"。
    """
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


#: 侧号（对齐服务端 HsfSide_A/B 与 C 端 HSF_SIDE_A/B：0=A，1=B）。
SIDE_A = 0
SIDE_B = 1


def _pick_side(states, prefer, usable):
    """按「优先侧 + 兜底侧」挑一个可用快照；挑不到返回 None。

    [口径对齐 C 端] C 端 hsfHomePickTempHum / hsfHomePickFlowMeter 同款：
    优先侧**新鲜且有有效值**才选它，否则退到兜底侧；两边都没有 ⇒ None
    （界面显示 "--"，不拿旧值冒充实时）。

    [不顶替] 挑中哪一侧就用哪一侧的值；那一侧某个值无效就显示 "--"，
    **不因为另一侧有值就替它顶上去** —— 那是编造。

    :param states: side -> 快照对象
    :param prefer: 侧号优先序（(优先侧, 兜底侧)）
    :param usable: 判定某侧快照能不能用的谓词
    """
    for side in prefer:
        st = states.get(side)
        if st is not None and usable(st):
            return st
    return None


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
        # [电机温度 2026-11-20 用户口径「搅拌泵卡片可以增加温度显示」]
        # 模拟量温度传感器（服务端 tempChannel 换算）。缺键 = 服务端没这
        # 一拍没温度（未配/读不到）⇒ UI 显示 "--"，不是 0℃。
        "motorTempC": ("motor_temp_c", float),
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
        self.motor_temp_c = None
        #: [电机转速渐变 2026-11-24] 服务端下发的转速显示范围（rpm）。
        self.speed_range_min = None
        self.speed_range_max = None
        #: [电机温度渐变 2026-11-24] 温度显示着色范围（℃）。
        self.motor_temp_range_min = None
        self.motor_temp_range_max = None
        #: [显示渐变逐项开关] 转速/电机温度是否启用渐变（缺键=开）。
        self.speed_gradient = True
        self.motor_temp_gradient = True
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
        # [电机转速渐变 2026-11-24] 范围键独立解析（不进 present：不是
        # 遥测字段，缺键时保留上一拍值，老服务端则保持 None）。
        _lo = _f_or_none(doc.get("speedRangeMin"))
        _hi = _f_or_none(doc.get("speedRangeMax"))
        if _lo is not None:
            self.speed_range_min = _lo
        if _hi is not None:
            self.speed_range_max = _hi
        _mlo = _f_or_none(doc.get("motorTempRangeMin"))
        _mhi = _f_or_none(doc.get("motorTempRangeMax"))
        if _mlo is not None:
            self.motor_temp_range_min = _mlo / 10.0
        if _mhi is not None:
            self.motor_temp_range_max = _mhi / 10.0
        _sg = doc.get("speedGradient")
        if _sg is not None:
            self.speed_gradient = bool(_sg)
        _mg = doc.get("motorTempGradient")
        if _mg is not None:
            self.motor_temp_gradient = bool(_mg)
        self.updated_ms = _now_ms()

    def speed_range(self):
        """转速显示范围 (lo, hi)；缺键/非法返回 None。"""
        lo, hi = self.speed_range_min, self.speed_range_max
        if lo is None or hi is None or hi <= lo:
            return None
        return lo, hi

    def motor_temp_range(self):
        """电机温度显示范围 (lo, hi)；缺键/非法返回 None。"""
        lo, hi = self.motor_temp_range_min, self.motor_temp_range_max
        if lo is None or hi is None or hi <= lo:
            return None
        return lo, hi

    def has(self, key):
        """服务端这一拍是否提供了该字段（否则 UI 显示 "--"）。"""
        return key in self.present

    def is_fresh(self, stale_ms=5000):
        """本地新鲜度：超过阈值没有新拍即判失效（断线保护）。"""
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def run_state_text(self):
        """运行状态文案。

        [「减速中」有两种含义，要分开说] 用户 2026-11-06：「点击停止后，
        变频器是慢慢停机的，希望正在停机这个过程状态显示出来，还有正在
        加速、正在减速都显示下」。
        服务端把两种减速都归到 RUN_DECELERATING（都符合"输出在下降"），
        但操作员看到的应是不同的词：
            · 运行中降给定 → 「减速中」（还在转，只是变慢）
            · 已发停止命令 → 「正在停机」（目标就是停）
        判据用 `running` 位：状态字仍报运行 = 还在正常降速；
        状态字已报停机 = 那是停机过程。
        """
        if self.run_state == 8 and not self.running:      # 8 = 减速中
            return "正在停机"
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


class TempHumSideState(object):
    """单侧温湿度快照（某一侧最近一帧）。值以 0.1 为单位（217 = 21.7℃）。

    [为什么要按侧分存] 服务端按 485 的**两条侧**轮流推**同一个主题**：
    装了温湿度站的那一侧推真值，没装的那一侧推**空帧**（只有
    tempValid=false/humValid=false，没有 temp/hum 键）。客户端若只留
    最后一帧，空帧会把真值盖掉 ⇒ 界面恒 "--"（现场实测：Python 客户端
    温湿度恒无数据）。C 端早就按侧分存（HsfLinkClient m_tempHumState[side]），
    这里对齐。
    """

    def __init__(self, side=SIDE_A):
        self.side = int(side)
        self.temp_tenths = None
        self.hum_tenths = None
        self.temp_valid = False
        self.hum_valid = False
        #: [显示渐变 2026-11-24] 电箱温度着色范围（℃，服务端 tempMin/Max
        #: 下发 ×10 定点）；缺键回落 None ⇒ 不上渐变（老服务端）。
        self.temp_min = None
        self.temp_max = None
        #: [显示渐变逐项开关] 电箱温度是否启用渐变（缺键=开）。
        self.temp_gradient = True
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        # 值键只在有效时出现：没有就是「无数据」，不能当成 0。
        # [_f_or_none 2026-11-16 审计] 坏类型帧按"无数据"收敛，不让异常
        # 滞留在 model 里每拍炸 refresh。
        self.temp_tenths = _f_or_none(doc.get("temp"))
        self.hum_tenths = _f_or_none(doc.get("hum"))
        self.temp_valid = bool(doc.get("tempValid", False))
        self.hum_valid = bool(doc.get("humValid", False))
        # 量程键缺 = 老服务端（保持 None ⇒ 不启用渐变）。
        _lo = _f_or_none(doc.get("tempMin"))
        _hi = _f_or_none(doc.get("tempMax"))
        self.temp_min = None if _lo is None else _lo / 10.0
        self.temp_max = None if _hi is None else _hi / 10.0
        _g = doc.get("tempGradient")
        if _g is not None:
            self.temp_gradient = bool(_g)
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < self.stale_ms

    def has_value(self):
        """这一侧此刻有没有有效值（温或湿任一有效即可）。"""
        return bool(self.temp_valid or self.hum_valid)

    def temp_value(self):
        """电箱温度（℃；无有效值/不新鲜返回 None）。"""
        if not self.temp_valid or self.temp_tenths is None or not self.is_fresh():
            return None
        return self.temp_tenths / 10.0

    def temp_range(self):
        """电箱温度显示范围 (lo, hi)；缺键/非法返回 None。"""
        if self.temp_min is None or self.temp_max is None:
            return None
        if self.temp_max <= self.temp_min:
            return None
        return self.temp_min, self.temp_max

    def temp_text(self):
        if not self.temp_valid or self.temp_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.temp_tenths / 10.0)

    def hum_text(self):
        if not self.hum_valid or self.hum_tenths is None or not self.is_fresh():
            return "--"
        return "%.1f" % (self.hum_tenths / 10.0)


class TempHumState(object):
    """温湿度快照（**按侧分存 + 择优取数**）。

    [取数口径] 「优先侧 + 兜底侧」：优先 A 侧（现场温湿度站挂 A 侧），
    A 侧不新鲜/无有效值才看 B 侧；两边都没有有效值 ⇒ 显示 "--"。
    与 C 端 hsfHomePickTempHum 同口径。

    [每侧只留最近一帧] 不缓存历史帧 ⇒ 内存不随运行时长增长。
    """

    #: 优先侧 + 兜底侧（现场温湿度站挂 A 侧；对齐 C hsfHomePickTempHum）。
    PREFER = (SIDE_A, SIDE_B)

    def __init__(self):
        #: side -> TempHumSideState（**每侧只留最近一帧**）。
        self.sides = {}

    def apply(self, doc):
        side = int(doc.get("side", 0) or 0)
        if side not in (SIDE_A, SIDE_B):
            return                      # 非法侧号：整帧丢弃（口径同 C 端）
        st = self.sides.get(side)
        if st is None:
            st = TempHumSideState(side)
            self.sides[side] = st
        st.apply(doc)

    def pick(self, prefer=None):
        """挑一个可用快照（优先侧 + 兜底侧）；挑不到返回 None。

        [挑不到 = 显示 "--"] 不用旧值冒充实时，也不拿另一侧的值顶替。
        """
        return _pick_side(self.sides, prefer or self.PREFER,
                          lambda st: st.is_fresh() and st.has_value())

    def is_fresh(self):
        """任一侧有新鲜帧（哪怕这一帧没有有效值）。"""
        return any(st.is_fresh() for st in self.sides.values())

    def temp_text(self):
        st = self.pick()
        return st.temp_text() if st is not None else "--"

    def hum_text(self):
        st = self.pick()
        return st.hum_text() if st is not None else "--"


class AnalogState(object):
    """模拟量 8 路快照。

    :ivar raw: 原始寄存器（毫伏）
    :ivar ua:  微安（mA × 1000）
    :ivar eng: 换算值 ×1000（只有服务端配了工程量的通道才有；其余为 0）
    :ivar eng_unit: 换算值单位（utf8）
    :ivar eng_name: 通道名（如"压差计"）
    :ivar eng_note: 越限说明（断线/超量程；空=正常）

    [换算值由服务端给] 量程是现场标定参数（同一个变送器型号有 10kPa~14MPa
    多种膜盒，差三个数量级），不该让每个客户端各配一遍。客户端只负责按
    e*/eu*/en*/ew* 显示。
    """

    def __init__(self, channels=8):
        self.channels = channels
        self.raw = [0] * channels
        self.ua = [0] * channels
        #: 通道号 -> 换算值 ×1000；不在表里 = 该通道没有工程量换算。
        self.eng = {}
        #: 通道号 -> (单位, 名称, 越限说明)
        self.eng_unit = {}
        self.eng_name = {}
        self.eng_note = {}
        #: [显示渐变 2026-11-24] 通道号 -> 工程量程（lo, hi；服务端随帧
        #: 下发 lo{i}/hi{i}，×1000 定点）。只用于数值着色。
        self.eng_lo = {}
        self.eng_hi = {}
        #: 通道号 -> 是否启用显示渐变（gr{i}；缺键=开）。
        self.eng_grad = {}
        self.valid_mask = 0
        self.updated_ms = 0

    def apply(self, doc):
        n = int(doc.get("n", 0) or 0)
        if n:
            self.channels = min(n, 18)
        # [按通道数重建 2026-11-16 审计] 构造期固定 8 格（__init__ 缺省），
        # 服务端 `analog.channels>8`（现场换 16 路模块）时每帧
        # `self.raw[i]` 越界 IndexError ⇒ 模拟量永久"无数据" + 状态栏刷屏
        # （异常每帧被 xlink 兜住只打日志）。⇒ 通道数变了就重建缓冲。
        if self.channels != len(self.raw):
            self.raw = [0] * self.channels
            self.ua = [0] * self.channels
        self.valid_mask = int(doc.get("v", 0) or 0)
        for i in range(self.channels):
            self.raw[i] = int(doc.get("r%d" % i, 0) or 0)
            self.ua[i] = int(doc.get("u%d" % i, 0) or 0)
        # 换算值：整表重建（服务端不再发 e<i> = 该通道不计工程值了）。
        #
        # [断线通道也要名字/文案 2026-11-20 用户实报「通道 1 的名字没了」]
        # 原来整段被 `e<i>` 存在才解析 ⇒ 断线通道服务端**只发 ew（断线文案）
        # 与 en（名字）、不发 e** ⇒ 名字与「断线」说明双双丢失（界面上只剩
        # 通道号 + 一个 0mA）。⇒ 四个键**各自独立判在**，缺哪个跳过哪个。
        eng, unit, name, note = {}, {}, {}, {}
        lo_map, hi_map = {}, {}
        grad_map = {}
        for i in range(self.channels):
            _e = doc.get("e%d" % i)
            if _e is not None:
                eng[i] = int(_e or 0)
            _u = doc.get("eu%d" % i)
            if _u is not None:
                unit[i] = str(_u or "")
            _n = doc.get("en%d" % i)
            if _n is not None:
                name[i] = str(_n or "")
            _w = doc.get("ew%d" % i)
            if _w is not None:
                note[i] = str(_w or "")
            # [显示渐变 2026-11-24] 量程键各自独立判在（同 e/en 口径）。
            _lo = doc.get("lo%d" % i)
            if _lo is not None:
                lo_map[i] = int(_lo or 0) / 1000.0
            _hi = doc.get("hi%d" % i)
            if _hi is not None:
                hi_map[i] = int(_hi or 0) / 1000.0
            _gr = doc.get("gr%d" % i)
            if _gr is not None:
                grad_map[i] = bool(_gr)
        self.eng, self.eng_unit = eng, unit
        self.eng_name, self.eng_note = name, note
        self.eng_lo, self.eng_hi = lo_map, hi_map
        self.eng_grad = grad_map
        self.updated_ms = _now_ms()

    def is_fresh(self, stale_ms=5000):
        return self.updated_ms > 0 and (_now_ms() - self.updated_ms) < stale_ms

    def channel_valid(self, index):
        return bool(self.valid_mask & (1 << index))

    def ma(self, index):
        """毫安值（微安 ÷ 1000）。"""
        return self.ua[index] / 1000.0

    def has_engineering(self, index):
        return int(index) in self.eng

    def eng_value(self, index):
        """换算工程量（物理量原值）；无换算返回 None。"""
        if not self.has_engineering(index):
            return None
        return self.eng[int(index)] / 1000.0

    def eng_range(self, index):
        """该通道的工程量程 (lo, hi)；缺键/非法返回 None（渐变不启用）。"""
        i = int(index)
        if i not in self.eng_lo or i not in self.eng_hi:
            return None
        lo, hi = self.eng_lo[i], self.eng_hi[i]
        if hi <= lo:
            return None
        return lo, hi

    def eng_gradient(self, index):
        """该通道是否启用显示渐变（gr{i}；缺键=老服务端 ⇒ 开）。"""
        return bool(self.eng_grad.get(int(index), True))

    def eng_text(self, index):
        """换算值的显示文本（含单位）；无换算返回空串。"""
        if not self.has_engineering(index):
            return ""
        return "%g %s" % (self.eng[int(index)] / 1000.0,
                          self.eng_unit.get(int(index), ""))


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
        #: 通道号 -> 语义名（服务端下发；没有被占用的口不在表里）。
        #: [客户端不推导这些名字] 哪路阀接在哪个口是现场接线，只有服务端
        #: 知道。客户端拿到什么显示什么，接线一改服务端一改就跟着变。
        self.di_names = {}
        self.do_names = {}

    def apply(self, doc):
        self.name = doc.get("name", self.name) or self.name
        self.di = int(doc.get("di", 0) or 0)
        self.do = int(doc.get("do", 0) or 0)
        self.count = int(doc.get("count", 16) or 16)
        self.valid = bool(doc.get("valid", False))
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        # ---- 通道名：**这一帧里带了名字才改** ----
        #
        # [判据是"帧里有没有名字"，不是只看 full 位]
        # 服务端与 C 端口径对齐后，名字只在「首连 / 订阅补发 / 快照应答」的
        # 全量帧里出现，周期推送是只带位图的增量帧。客户端原来**每帧都整表
        # 重建名字表**，于是：
        #   订阅补发的 full=True 帧给了 18 个名字 → 200ms 后第一帧增量推送
        #   把名字表覆盖成空 → 数字量页只剩 CH/DO 号，电磁阀语义名全没了。
        # 现象看着像"服务端没发名字"，实际是客户端把刚拿到的名字擦掉了。
        #
        # 三态处理：
        #   ① 帧里**有**名字键            → 整表替换（这一帧就是完整真相，
        #                                   服务端不再发的键 = 该口已释放）
        #   ② 帧里**没有**名字键           → 一律沿用缓存，**绝不改成空**
        #      · full=False 增量帧：正常，名字靠缓存
        #      · full=True 但没名字：老服务端/该模块没被占用 ⇒ 同样不能擦
        #        掉已知名字 —— 少显示总比把接线信息弄丢好
        # 与 C 端 HsfLinkClient.c 的 hsfLcParseDigital 同口径（增量帧沿用
        # 缓存），但更保守：连"full=True 却没带名字"的帧也不擦缓存。
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
        self.updated_ms = _now_ms()

    def di_name(self, index):
        return self.di_names.get(int(index), "")

    def do_name(self, index):
        return self.do_names.get(int(index), "")

    def is_fresh(self):
        return (self.valid and self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def di_bit(self, index):
        return bool(self.di & (1 << index))

    def do_bit(self, index):
        return bool(self.do & (1 << index))


class TurbiditySideState(object):
    """单侧浊度仪快照（某一侧最近一帧；v{index}_ 前缀，与阀/数字量同风格）。

    [为什么按侧分存] 服务端按两条侧轮流推同一个浊度主题；没装配浊度仪的
    那一侧推的是「两台都 online=false」的帧。客户端若只留最后一帧，
    读数会被空帧盖掉 ⇒ 首页浊度卡恒 "--"。
    """

    def __init__(self, side=SIDE_A):
        self.side = int(side)
        #: index -> {"name","ntu","raw","online"}
        self.items = {}
        self.count = 0
        self.stale_ms = 5000
        self.updated_ms = 0

    def apply(self, doc):
        count = int(doc.get("count", 0) or 0)
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
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
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
        # [_f_or_none 2026-11-16 审计] 坏类型收敛（原 float(value)/100.0
        # 遇字符串/容器直接抛，整页 refresh 被打断）。
        value = _f_or_none(value)
        return None if value is None else value / 100.0

    def raw_of(self, index):
        return self.items.get(int(index), {}).get("raw")

    def temp_of(self, index):
        """水温（℃；float 或 None）。

        [为什么可能没有] 只有**带温度的机型**才随帧带 "v{i}_temp"（站3
        ZHEYI-5100ZDY 一帧 0x0000×4 同刻读出；站1 WZT-701A 无温度）。
        **缺键 = 没有温度**，不是 0℃——显示 0℃ 会被当成真实读数。
        """
        item = self.items.get(int(index), {})
        if not item.get("online"):
            return None
        value = item.get("temp")
        if value is None:
            return None
        # [_f_or_none 2026-11-16 审计] 同 ntu_of：坏类型按"无温度"收敛。
        value = _f_or_none(value)
        return None if value is None else value / 100.0

    def online_of(self, index):
        return bool(self.items.get(int(index), {}).get("online"))

    def range_of(self, index):
        """该台的显示渐变范围 (lo, hi)（NTU）；缺键/非法返回 None。

        [缺键回落老口径] 老服务端不发 rangeMin/rangeMax（或本帧被老版本
        推送覆盖）⇒ 返回 None ⇒ 首页退回"到 20 NTU 标红"的旧显示口径。
        """
        item = self.items.get(int(index), {})
        lo = _f_or_none(item.get("rangeMin"))
        hi = _f_or_none(item.get("rangeMax"))
        if lo is None or hi is None:
            return None
        lo, hi = lo / 100.0, hi / 100.0
        if hi <= lo:
            return None
        return lo, hi

    def gradient_of(self, index):
        """该台是否启用显示渐变（v{i}_gradient；缺键=开）。"""
        item = self.items.get(int(index), {})
        val = item.get("gradient")
        return True if val is None else bool(val)


class TurbidityState(object):
    """两台浊度仪的合并状态（**按侧分存 + 择优取数**）。

    [为什么合成一个对象而不是两台各一个] 它们总是成对显示、成对刷新，
    分开存只会让页面写两遍同样的逻辑。台数由服务端 `count` 决定。

    [为什么还要按侧分存] 服务端按两条侧轮流推同一个主题；没装配浊度仪的
    那一侧推「两台都 offline」的帧，只留最后一帧 ⇒ 读数被盖掉 ⇒ 首页浊度
    卡恒 "--"。两台浊度仪在**同一侧**（服务端 HSF_BUS_B_HAS_TURBIDITY=1），
    所以按侧分存后取那一侧即可，**不必再按台分侧**。

    [每侧只留最近一帧] 不缓存历史帧 ⇒ 内存不随运行时长增长。
    """

    #: 优先侧 + 兜底侧（现场两台浊度仪挂 B 侧）。
    PREFER = (SIDE_B, SIDE_A)

    def __init__(self):
        #: side -> TurbiditySideState（**每侧只留最近一帧**）。
        self.sides = {}

    def apply(self, doc):
        side = int(doc.get("side", 0) or 0)
        if side not in (SIDE_A, SIDE_B):
            return                      # 非法侧号：整帧丢弃（口径同 C 端）
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
        """任一侧有新鲜帧（哪怕这一帧两台都离线）。"""
        return any(st.is_fresh() for st in self.sides.values())

    def _last_state(self):
        """最近收到一帧的那一侧（挑不到可用侧时的兜底写入目标）。"""
        best = None
        for st in self.sides.values():
            if best is None or st.updated_ms > best.updated_ms:
                best = st
        return best

    # ---- 取值一律走「被选中的一侧」；挑不到 = 无数据 ----

    @property
    def count(self):
        st = self.pick()
        return st.count if st is not None else 0

    @property
    def stale_ms(self):
        st = self.pick()
        return st.stale_ms if st is not None else 5000

    @property
    def updated_ms(self):
        """被选中那一侧的到达时间（趋势判重按这个，避免空帧灌重复点）。"""
        st = self.pick()
        return st.updated_ms if st is not None else 0

    @updated_ms.setter
    def updated_ms(self, value):
        """改「当前这一帧」的到达时间（造拍/造过期用）。

        写入目标 = 被选中的那一侧；没有可用侧时落到最近收到一帧的那一侧。
        """
        st = self.pick() or self._last_state()
        if st is not None:
            st.updated_ms = int(value)

    def name_of(self, index):
        st = self.pick()
        return st.name_of(index) if st is not None else ""

    def ntu_of(self, index):
        """浊度值（NTU，float）；无数据返回 None。"""
        st = self.pick()
        return st.ntu_of(index) if st is not None else None

    def raw_of(self, index):
        st = self.pick()
        return st.raw_of(index) if st is not None else None

    def online_of(self, index):
        st = self.pick()
        return st.online_of(index) if st is not None else False


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
        count = int(doc.get("count", 0) or 0)
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
        self.stale_ms = int(doc.get("staleMs", 5000) or 5000)
        self.updated_ms = _now_ms()

    def is_fresh(self):
        return (self.updated_ms > 0
                and (_now_ms() - self.updated_ms) < self.stale_ms)

    def name_of(self, index):
        return self.items.get(int(index), {}).get("name", "")

    def is_on(self, index):
        return bool(self.items.get(int(index), {}).get("on"))

    def is_ready(self, index):
        return bool(self.items.get(int(index), {}).get("ready"))

    def text_of(self, index):
        return self.items.get(int(index), {}).get("text", "")


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
        #: [滤速 2026-11-25] 过滤面积（m²；None=服务端没给/未设置）与
        #: 服务端算好的滤速（m/h；None=无面积或无有效流量）。
        self.filter_area_m2 = None
        self.filter_velocity_mh = None
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
        # [滤速 2026-11-25] 面积/滤速由服务端算好随帧下发；缺键=None。
        self.filter_area_m2 = (float(doc.get("filterAreaM2"))
                               if isinstance(doc.get("filterAreaM2"),
                                             (int, float)) else None)
        self.filter_velocity_mh = (float(doc.get("filterVelocityMh"))
                                   if isinstance(doc.get("filterVelocityMh"),
                                                 (int, float)) else None)
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


class FlowMeterState(object):
    """电磁流量计快照（**按侧分存 + 择优取数**）。

    [取数口径] 「优先侧 + 兜底侧」：优先 B 侧（现场流量计挂 B 侧），
    B 侧不新鲜/无效才看 A 侧；两边都没有有效值 ⇒ 显示 "无数据"。
    与 C 端 hsfHomePickFlowMeter 同口径。

    [每侧只留最近一帧] 不缓存历史帧 ⇒ 内存不随运行时长增长。
    """

    #: 优先侧 + 兜底侧（现场流量计挂 B 侧；对齐 C hsfHomePickFlowMeter）。
    PREFER = (SIDE_B, SIDE_A)

    def __init__(self):
        #: side -> FlowMeterSideState（**每侧只留最近一帧**）。
        self.sides = {}

    def apply(self, doc):
        side = int(doc.get("side", 0) or 0)
        if side not in (SIDE_A, SIDE_B):
            return                      # 非法侧号：整帧丢弃（口径同 C 端）
        st = self.sides.get(side)
        if st is None:
            st = FlowMeterSideState(side)
            self.sides[side] = st
        st.apply(doc)

    def pick(self, prefer=None):
        """挑一个可用快照（优先侧 + 兜底侧）；挑不到返回 None。

        [无效帧不顶替] 流量计 valid=false = 「此刻无有效值」，那一侧直接
        跳过去看兜底侧；两侧都无效 ⇒ None ⇒ 界面显示 "无数据"。
        """
        return _pick_side(self.sides, prefer or self.PREFER,
                          lambda st: st.is_fresh())

    def is_fresh(self):
        """有可用的流量计快照（新鲜且 valid）。"""
        return self.pick() is not None

    def _last_state(self):
        """最近收到一帧的那一侧（挑不到可用侧时的兜底写入目标）。"""
        best = None
        for st in self.sides.values():
            if best is None or st.updated_ms > best.updated_ms:
                best = st
        return best

    # ---- 取值一律走「被选中的一侧」；挑不到 = 无数据 ----

    @property
    def flow(self):
        st = self.pick()
        return st.flow if st is not None else 0.0

    @property
    def speed(self):
        st = self.pick()
        return st.speed if st is not None else 0.0

    @property
    def filter_velocity_mh(self):
        """滤速（m/h）；服务端未给/面积未设置/离线 ⇒ None（界面 "--"）。"""
        st = self.pick()
        return st.filter_velocity_mh if st is not None else None

    @property
    def filter_area_m2(self):
        """滤速计算面积（m²）；服务端未给 ⇒ None。"""
        st = self.pick()
        return st.filter_area_m2 if st is not None else None

    @property
    def forward_total(self):
        st = self.pick()
        return st.forward_total if st is not None else 0.0

    @property
    def reverse_total(self):
        st = self.pick()
        return st.reverse_total if st is not None else 0.0

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
        """被选中那一侧的到达时间（趋势判重按这个，避免空帧灌重复点）。"""
        st = self.pick()
        return st.updated_ms if st is not None else 0

    @updated_ms.setter
    def updated_ms(self, value):
        """改「当前这一帧」的到达时间（造拍/造过期用）。

        写入目标 = 被选中的那一侧；没有可用侧时落到最近收到一帧的那一侧。
        """
        st = self.pick() or self._last_state()
        if st is not None:
            st.updated_ms = int(value)

    def flow_unit_text(self):
        st = self.pick()
        return st.flow_unit_text() if st is not None else FLOW_UNIT_TEXT.get(
            0xFF, "?")

    def total_unit_text(self):
        st = self.pick()
        return st.total_unit_text() if st is not None else TOTAL_UNIT_TEXT.get(
            0xFF, "?")


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

#: 流量计报警位 → 中文（**位定义照说明书，不写死序号**）。
#:
#: 依据 `docs/LD电磁流量计_RS485通讯实测与使用指南_20261104.md` §4.1
#: 变量区表：0x1022 上限报警 / 0x1023 下限报警 / 0x1024 空管报警 /
#: 0x1025 系统报警，四项各占一位，服务端压成位图（见 server_py
#: `FlowMeter.alarm_mask`）：0=上限 1=下限 2=空管 3=系统。
#: 位序与 C 端 `XFlowMeterAlarm`（Src/Industry/XFlowMeter/XFlowMeter.h）一致。
#:
#: **展示顺序照 C 端 `fmAlarmText`**（HsfMonitorFlowMeter.c）：空管 → 上限
#: → 下限 → 系统 —— 两端文案必须逐字对齐。
FLOW_ALARM_TEXT = (
    (1 << 2, "空管"),      # 0x1024 空管报警
    (1 << 0, "上限"),      # 0x1022 上限报警
    (1 << 1, "下限"),      # 0x1023 下限报警
    (1 << 3, "系统"),      # 0x1025 系统报警
)


def flow_alarm_text(bits):
    """流量计报警位图 → 中文（0 → 「正常」）。

    [为什么不再只显示十六进制] 操作员看不懂 "0x0004"；说明书里写的是
    「空管报警」，现场问的就是"是不是空管"。故直接给中文。
    未知位（说明书外的 bit）保留十六进制尾巴，避免把新报警类型说没了。

    :param bits: 报警位图（int）。
    :return: 中文文案，如 "空管"、"上限 下限"。
    """
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
        #: 待显示的告警队列（服务端推来的；页面消费后调 take_alarm_list 取走）。
        self.alarms = []
        #: 告警队列上限（超出丢最旧；见 take_alarms 的说明）。
        self.ALARMS_MAX = 200
        #: 累计收到的告警条数（诊断用）。
        self.alarm_total = 0
        self.last_alarm_ms = 0

    def apply(self, doc):
        # [告警帧不能当状态帧处理] 服务端把"阀告警"也发在 TOPIC_VALVE_STATUS
        # 上（同一主题，客户端订阅一次即可两种帧都收到）。告警帧的键空间是
        # "ac"/"a0_*"，**没有 count 也没有 v{i}_***。若照状态帧解析，会把
        # items 清空 —— 界面上的阀会瞬间全部变成"未配置"再恢复，闪一下。
        # 故先认告警帧：交给 take_alarms，状态原样保留。
        if proto.KEY_ALARM_COUNT in doc:
            self.take_alarms(doc)
            return
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

    def take_alarms(self, doc):
        """收集一帧告警（一帧可含多条）。

        客户端**不做任何判断**：超没超时、要不要报警，全部由服务端决定，
        这里只把内容攒起来给界面显示。

        [有界 2026-11-16 审计] 告警只在**电动球阀页**被消费（drain）；
        用户停在别的页期间自动模式反复阀超时 ⇒ 列表无界增长，进阀页时
        一次性对全部积压逐条 set_status（连闪千条 + 一拍卡顿）⇒ 封顶
        200 条丢最旧（与 auto_alarm_log / 报警页 MAX_ROWS 同量级）。
        """
        count = int(doc.get(proto.KEY_ALARM_COUNT, 0) or 0)
        for i in range(count):
            prefix = "a%d_" % i
            self.alarms.append({
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            })
            if len(self.alarms) > self.ALARMS_MAX:
                del self.alarms[:len(self.alarms) - self.ALARMS_MAX]
        self.alarm_total += count
        self.last_alarm_ms = _now_ms()
        return count

    def drain_alarms(self):
        """取走待显示告警（取走即清空，避免界面每次刷新都重复弹同一条）。"""
        if not self.alarms:
            return []
        out = self.alarms
        self.alarms = []
        return out

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
        #: 连接地址（服务端给的展示写法）：TCP = "ip:port"，
        #: 485 = "COM17 · 115200 · 站 2"。老服务端不发该键 ⇒ 空串，界面显示 "-"。
        self.addr = ""

    def health_text(self):
        return HEALTH_TEXT.get(self.health, "未知")

    def kind_text(self):
        return KIND_TEXT.get(self.kind, "未知")


HEALTH_TEXT = {0: "正常", 1: "告警", 2: "数据过期", 3: "离线", 4: "故障"}
#: 设备类别 → 中文（必须与 proto.KIND_* 对齐；漏一个就会在界面上显示"未知"）。
#: [实锅] 加电磁阀组时忘了同步这张表，首页"类型"列直接显示"未知"。
KIND_TEXT = {0: "变频器", 1: "温湿度", 2: "流量计", 3: "模拟量",
             4: "485总线", 5: "数字量", 6: "电动球阀组", 7: "水质",
             8: "相机"}

#: 告警**来源类别**（服务端 alarms 表的 kind 列）→ 「类型」列文案。
#:
#: [三端同源] 与网页端 webui_py/hsfweb/model.py 的 ALARM_SOURCE_KIND_TEXT
#: **逐项一致**（主页「最近报警」卡与报警页都用它 ⇒ 两处显示同一个词）。
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
    **同一套判据**（pages_other.AlarmPage._server_row_to_record）。
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
    """完整连接地址 → **紧凑显示**（tooltip/复制仍用完整值）。

    [为什么瘦身] 设备状态表的地址列宽有限（板端 1280 宽、浮层 960px），
    完整串 "COM17 · 115200 · 站 2"（21 字符）会被截成 "COM17 · 115200 ·
    ..."（**站号恰恰在最右**，最该看的那个字没了）。

    [瘦身规则（三端同款）]
        "COM17 · 115200 · 站 2"  → "COM17/115200/#2"
        "192.168.0.13:502"       → 不变
        "192.168.0.13:9600,8,N"   → "192.168.0.13:9600"（串口参数后缀）
        "192.168.0.13:502 · DO3" → "192.168.0.13:502/DO3"

    [完整值不能丢] 排障第一眼要看的正是完整地址 ⇒ 完整串进 **tooltip**
    + **点击复制**（见 pages_other.HomePage._copy_addr）。
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
            d.addr = doc.get("d%d_addr" % i, "") or ""
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


class CameraState(object):
    """相机画面（**最新一帧的 JPEG 字节** + 元信息）。

    [为什么帧本体存在客户端] 画面**必须经服务端**（相机在内网、客户端可能在
    公网，服务端是唯一两边都够得到的地方）⇒ 服务端把 JPEG 切片推过来，
    客户端组帧后存这里，页面直接取字节贴图（QImage.fromData）。
    """

    def __init__(self):
        self.frame = b""        # 最新一帧（JPEG 字节，或 ES 解码后的 RGB）
        self.width = 0
        self.height = 0
        self.seq = 0            # 已收到的帧数（自增）
        self.ts_ms = 0          # 收到这一帧的时刻
        self.dropped = 0        # 组帧失败/丢片的次数
        #: [2026-11-19 ES 直传] 帧形态："jpeg"（服务端编好的 JPEG，
        #: QImage.fromData 贴图）/ "es"（服务端直传 NAL、本端 PyAV 解出的
        #: **RGB 裸帧**，QImage(data,w,h,Format_RGB888) 贴图）。
        self.mode = "jpeg"

    def set_frame(self, data, width, height):
        self.frame = data
        self.width = int(width or 0)
        self.height = int(height or 0)
        self.seq += 1
        self.ts_ms = int(time.time() * 1000)
        self.mode = "jpeg"

    def set_es_frame(self, rgb, width, height):
        """ES 直传：解码线程解出的 RGB 裸帧。"""
        self.frame = rgb
        self.width = int(width or 0)
        self.height = int(height or 0)
        self.seq += 1
        self.ts_ms = int(time.time() * 1000)
        self.mode = "es"

    def to_json(self):
        return {"hasFrame": bool(self.frame), "width": self.width,
                "height": self.height, "seq": self.seq, "tsMs": self.ts_ms,
                "dropped": self.dropped, "bytes": len(self.frame)}


class AppModel(object):
    """全部设备状态 + 运行模式（单一数据源，页面共享）。"""

    def __init__(self):
        self.vfd = VfdState()
        self.temphum = TempHumState()
        self.analog = AnalogState()
        self.flowmeter = FlowMeterState()
        self.turbidity = TurbidityState()   # 两台浊度仪（合并一帧）
        self.pump = PumpState()             # 进水泵（只有输出无反馈）
        self.digital = {}          # index -> DigitalState
        self.valve = ValveState()  # 6 路电磁阀
        self.camera = CameraState()   # 相机画面（最新一帧 JPEG + 元信息）
        self.health = HealthSnapshot()
        self.run_mode = 0          # 0=手动 1=自动
        self.run_mode_known = False
        #: 自动控制器是否真的在跑（None = 服务端没报，按"未知"显示）。
        #:
        #: [为什么不从 run_mode 推] 模式=自动 不代表 控制器在跑。分开存，
        #: 界面才能把"模式自动但控制器已停"这种异常如实显示出来。
        self.auto_running = None
        #: 自动模式状态三件（2026-11-11 主界面模式卡；老服务端不发时保持
        #: 原值——缺键不是"没模式"，别拿默认值冒充事实）。
        #: 当前（选中/正在运行的）自动模式 id（""=未选/未知）。
        self.auto_mode = ""
        #: 控制器相位 idle/valves/pumps/running/estop（""=未知）。
        self.auto_phase = ""
        #: **当前循环实例标题**（「产水模式2」；空串=不在循环/单模式）。
        #: 2026-11-27：主界面循环运行时给模式提示加序号后缀；单模式不带。
        #: 缺键（老服务端）保持空串 ⇒ 界面退回只显示模式名，不编造序号。
        self.auto_stage_title = ""
        #: **组合循环进度**（2026-11-27 用户「组合有多次循环要在主界面
        #: 提示总共几次、当前是第几次」）。五个状态键都是**可选键**——
        #: 老服务端缺键 ⇒ 保持缺省（组名空串/计数 0）⇒ 界面不显示进度，
        #: **不编造**（与 autoStageTitle 的缺键口径同款）：
        #:   · auto_rep_group   当前阶段所属最近一层组的显示名（""=不在组内）
        #:   · auto_rep_current 当前第几遍（1-based；0=不可用）
        #:   · auto_rep_total   总遍数（0=不可用；==1 时界面不显示）
        #:   · auto_round       当前第几轮（1-based；0=不在循环）
        #:   · auto_round_total 配置总轮数（0=一直循环）
        self.auto_rep_group = ""
        self.auto_rep_current = 0
        self.auto_rep_total = 0
        self.auto_round = 0
        self.auto_round_total = 0
        #: 报警锁存（None=未知；True=有报警未复位，启动被拒）。
        self.auto_fault = None
        #: **累计自动运行时长**（毫秒；2026-11-12）。服务端计（唯一事实
        #: 源）：只在自动运行期间累加，停止后冻结保留、多次运行累计。
        #: 缺键（老服务端）保持 0，不编造。
        self.auto_run_ms = 0
        #: 是否**收到过**服务端的时长数据（2026-11-12）。[为什么要有这个
        #: 标志] auto_run_ms=0 有两种含义："服务端报了 0"（没跑过，该显示
        #: 0天 00时 00分 00秒）和"还没收到数据"（该显示 --）。只用数值
        #: 区分不了 ⇒ 显式记"收到过没有"。收到过之后**即使为 0 也照常
        #: 显示**（用户口径：没运行也要显示累计运行时间）。
        self.auto_run_ms_known = False
        #: **当前子阶段（模式）已运行时长**（毫秒；-1 = 不在运行/没有
        #: 阶段）。与 auto_stage_total_ms 一起算「剩余时间」。
        self.auto_stage_elapsed_ms = -1
        #: **当前子阶段总时长**（毫秒；0 = 本阶段无定时 ⇒ 剩余不可知）。
        self.auto_stage_total_ms = 0
        #: 是否**收到过**服务端的阶段计时（老服务端不发 ⇒ 不编造倒计时）。
        self.auto_stage_known = False
        #: **最近一条报警**（2026-11-19 主页「最近报警」卡）。
        #:
        #: None = 从未收到过报警（或老服务端不发这个键）⇒ 界面显示
        #: 「无报警」。收到过但服务端报 null ⇒ 也是 None（**无报警就是
        #: 无报警**，不编造一条出来）。
        #:
        #: 结构 = {"kind","dev","text","atMs"}（服务端 alarms 表最近一行；
        #: atMs = 墙钟 epoch 毫秒 ⇒ 界面直接格式化 HH:MM:SS）。
        self.last_alarm = None
        #: 是否**收到过**服务端的 lastAlarm 键（老服务端不发 ⇒ 保持 None）。
        self.last_alarm_known = False
        self.update_ms = 0
        # [2026-11-17] 报警历史**不再在客户端本地记**：
        #   报警数据一律向服务端要（0x011A，见 pages_other.AlarmPage）⇒
        #   本地不再有「自动告警缓冲」与「健康边沿检测」——它们各自只存在于
        #   某一方进程内（网页端是常驻服务、客户端是会被重启的 UI）⇒ 两端
        #   看到的记录条数天然对不上（用户实报「web 端报警有数据，客户端
        #   报警视图没数据」）。现在**边沿由服务端记、客户端与网页端都取
        #   同一份**（单一数据源）。

    def digital_state(self, index):
        """取（必要时创建）某模块的数字量状态。"""
        dev = self.digital.get(index)
        if dev is None:
            dev = DigitalState(index)
            self.digital[index] = dev
        return dev

    def digital_count(self):
        # [快照 2026-11-16] keys 由网络线程插入新键 ⇒ 先拷再算。
        return max(list(self.digital.keys())) + 1 if self.digital else 0

    def is_auto(self):
        return self.run_mode == 1

    def is_auto_running(self):
        """自动控制器是否在跑；**未知时返回 None**（不是 False）。

        [为什么 None 不折成 False] 老服务端不报这个键。若折成 False，
        界面会在自动模式下显示"自动已停止"——那是**编造的故障**。
        未知就显示未知，别拿默认值冒充事实。
        """
        return self.auto_running
