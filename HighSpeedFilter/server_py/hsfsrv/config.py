# -*- coding: utf-8 -*-
"""部署配置：设备清单 / 串口参数 / 推送节拍 / 运行期持久化。

[与 C 侧的分工] C 版把「串口号/站号/地址」写成 HsfServerConfig.h 的**编译期
宏**（现场改头文件再编）；Python 版改用**运行期配置文件 + 命令行覆盖**——
现场没有编译环境，改 JSON 重启即可，这也正是本次移植的主要动机之一。

[配置优先级] 命令行 > 配置文件 > 内置缺省。缺键一律用缺省值补齐（老配置
文件加新设备不需要重写整份配置）。
"""

import json
import os

from . import ports
from . import proto          # 落盘缺省值（入库周期/容量上限，见 proto 注释）
from .ports import default_serial_port


# ============================================================================
#                              缺省值
# ============================================================================

#: 配置文件相对程序根目录的位置（对齐 C 侧 cfg/ 口径）。
DEFAULT_CONFIG_REL = "cfg/hsf_server_py.json"
#: 账户库与配置文件同目录。
DEFAULT_USERS_REL = "cfg/hsf_users.json"
#: 缺省监听端口 —— **从 ports.py 统一取**，不要在这里另写数字。
#:
#: 改端口只需改 `hsfsrv/ports.py` 里的 PY_SERVER_PORT（那个文件有详细说明）。
DEFAULT_PORT = ports.PY_SERVER_PORT

#: 浊度仪字节序旋钮的合法值（与 devices.TurbidityMeter.BYTE_ORDER_PERM
#: 的键一致；配置文件手写出错时按非法条目丢弃，不让服务起不来）。
VALID_TURBIDITY_BYTE_ORDERS = ("CDAB", "DCBA", "ABCD", "BADC")

#: 6 台电动球阀的**缺省名字**（含编号，2026-11-23 用户口径）。
#:
#: [为什么集中一处] 缺省阀名有三个消费点：内置缺省 `Config.valve_items`、
#: `Config.load()` 的 name 兜底、`valves.ValveItem` 的 name 兜底 —— 三处
#: 各写一份字符串就会出现"配置文件有名字、缺省没名字"两副面孔。C 端
#: 现场配置的单独名仍以配置文件为准；本常量只作**缺省/兜底**。
#: [与 proto.AUTO_DEVICES 显示名逐字一致] 后者是"自动模式勾选设备"的
#: 显示名（协议 id 仍是 valve1..6）；改名时两处要一起改（回归
#: test_units 的阀名断言会抓不一致）。
VALVE_DEFAULT_NAMES = (
    "产水阀(1)",
    "反洗进水阀(2)",
    "进水阀(3)",
    "排空阀(4)",
    "反洗出水阀(5)",
    "顺洗出水阀(6)",
)


def _next_serial_port(port):
    """从路 A 的串口名推路 B 的缺省口（COM17 → COM18；非 COM 形式原样返回 +2）。

    [为什么只是缺省] 现场两个 USB-485 转换器插在不同 USB 口，Windows 分配
    的 COM 号不一定相邻。这个推导只保证「默认不撞车」，真实口名以现场
    设备管理器为准，由配置 `serial_b.port` 覆盖。
    """
    import re
    m = re.match(r"^(.*?)(\d+)$", str(port))
    if not m:
        return "%s_2" % port
    return "%s%d" % (m.group(1), int(m.group(2)) + 1)


class SerialConfig(object):
    """一路 485 总线（串口号 + 波特率 + 链路参数）。

    [现场是两条独立 485，不是一条] 用户明确：
      · 路 A：变频器 + 温湿度，115200
      · 路 B：流量计 + 浊度仪 ×2，9600
    两条线物理独立、速率不同，不能共用一个 master（共用会让 9600 的设备
    被按 115200 访问，全部不通）。故配置里有两个实例：`serial`（路 A）与
    `serial_b`（路 B）。`make_rtu` 按 (port, baudrate) 缓存，天然分成两个
    master；健康表的地址文案也按设备所属路取不同的口和速率。

    [向后兼容] 老配置文件只有 `serial` 一节，没有 `serial_b`；缺省把路 B
    设成「另一个串口 + 9600」。若现场确实两条线接在同一个口上（不可能，
    速率不同），可由配置显式改。
    """

    __slots__ = ("port", "baudrate", "timeout_ms", "retries", "poll_gap_ms",
                 "vfd_poll_ms", "temphum_poll_ms", "temphum_push_ms",
                 "vfd_rated_rpm", "vfd_rated_hz", "vfd_max_hz")

    def __init__(self, **kw):
        self.port = kw.get("port", default_serial_port())
        self.baudrate = int(kw.get("baudrate", 115200))
        self.timeout_ms = int(kw.get("timeout_ms", 400))
        self.retries = int(kw.get("retries", 1))
        #: 相邻两条 RTU 事务之间的**最小总线静默**（毫秒；0=不加间隙）。
        #:
        #: [路 B（serial_b）为什么缺省 200 —— 2026-11-09 ttyS4 实测]
        #: 站1 WZT-701A 浊度仪在站5 LD 流量计的 49 字节长帧之后需要
        #: ≥100~125ms 的恢复期才有应答（200ms 稳定 10/10，见实测文档
        #: docs/HSF板端ttyS4三设备取数实测总表_20261109.md §8）。路 B 的
        #: 三台从站各是**独立轮询任务**（app._poll_loop），节拍可能恰好
        #: 对齐 ⇒ 流量计事务结束后浊度事务 0ms 跟上 ⇒ 站1 间歇无应答。
        #: 间隙在 master 事务层兜底（modbus.RtuMaster.min_gap_s）——那是
        #: 同总线事务唯一必经的串行化卡点；路 A 的变频器是快设备（100ms
        #: 节拍），缺省 0 不加间隙、维持老节拍。
        self.poll_gap_ms = int(kw.get("poll_gap_ms", 0))
        #: 变频器是快设备，每拍都给机会；温湿度物理更新 30Hz，500ms 绰绰有余。
        self.vfd_poll_ms = int(kw.get("vfd_poll_ms", 100))
        self.temphum_poll_ms = int(kw.get("temphum_poll_ms", 500))
        self.temphum_push_ms = int(kw.get("temphum_push_ms", 500))
        # ---- 变频器额定点（铭牌参数，用于 Hz ↔ rpm 换算）----
        #: 额定转速（rpm）。现场实测 50Hz 约 1440rpm（4 极异步电机带滑差）。
        #: [为什么用额定点而不是极对数公式] 极对数算出的是同步转速
        #: （4 极 = 1500rpm），与实测差 4%；用铭牌额定点更贴近实际。
        self.vfd_rated_rpm = float(kw.get("vfd_rated_rpm", 1440.0))
        #: 额定频率（Hz），与额定转速同一点。
        self.vfd_rated_hz = float(kw.get("vfd_rated_hz", 50.0))
        #: 最大频率（Hz）：决定频率给定上限（0x1000 的 ±100% 对应它）。
        self.vfd_max_hz = float(kw.get("vfd_max_hz", 50.0))


class AnalogChannelScale(object):
    """单路模拟量的**工程量换算**（4~20mA → 物理量）。

    [口径] 4~20mA 线性映射到 lowValue~fullScale（量程下限~上限）：

        p = lowValue + (I - 4.0) / 16.0 × (fullScale - lowValue)

    [lowValue 缺省 0 = 老口径完全等价] 既有配置文件（只有 fullScale）读进来
    lowValue=0 ⇒ 公式退化为原 (I-4)/16 × fullScale，行为逐位不变。
    [量程上下限可配 2026-11-20 用户口径] 「温度只要填上限和下限即可」——
    4mA → 下限、20mA → 上限，换算由两端点自己定；压差计同样可配
    （缺省 0~1 MPa 与出厂口径一致）。

    **低于 4mA = 负压**（2026-11-17 用户口径「压差计在小于 4mA 后是负压
    不是断线」）：现场压差计的 4mA 就是**零点**，零点以下是真负压（吸力）⇒
    按线性继续算出负值并标「负压」，**不再当断线/欠量程夹到 0**。
    高于 20mA 仍是**超量程**（夹到满量程 + 标记）——那才是真故障。

    [为什么满量程必须由配置给] 同一个变送器型号有 L/M/H/V 多种膜盒，
    量程差三个数量级（10kPa ~ 14MPa）。写死在代码里等于替现场猜，
    猜错时读数会出现"永远 0.00"或"一上电就满量程"这种极难排查的现象。
    """

    #: 断线判定阈值（mA）：**远低于 4mA 就是回路没电流**（断线/无供电）。
    #:
    #: [为什么必须判 2026-11-20 用户实报] 温度量程配成 -50~200℃ 时，
    #: 0mA 走「欠量程钳到下限」⇒ 主界面显示 **-50℃**（看起来像个真温度！）
    #: ⇒ 现场会把"传感器断线"当"环境零下 50 度"。0mA 物理上不可能是
    #: 正常 4~20mA 读数 ⇒ 必须判**断线**（不显示数值、保护不拿它判定）。
    #: 阈值 0.5mA：真断线≈0；正常变送器最低值远高于它（不会误伤）。
    BROKEN_MA = 0.5

    __slots__ = ("index", "name", "unit", "full_scale", "low_value",
                 "below_valid", "low_ma", "high_ma")

    def __init__(self, index=0, **kw):
        # [2026-11-15 审计·容错] 手改配置写错一个字符（"fullScale": "abc"）
        # 原来直接抛 ValueError ⇒ **服务端起不来**（配置笔误变现场事故）。
        # ⇒ 全部走宽松取：非法值回缺省，通道标成"未标定"（不显示换算值）。
        self.index = _int(index, 0)
        self.name = kw.get("name", "")
        self.unit = kw.get("unit", "")
        #: 满量程（单位见 unit）；0/None = 未标定（不显示换算值）。
        self.full_scale = _float(kw.get("fullScale", 0), 0.0)
        #: 量程下限（4mA 对应的工程量，缺省 0=活零点）；20mA 对应 fullScale。
        self.low_value = _float(kw.get("lowValue", 0), 0.0)
        #: [4mA 以下有效 2026-11-20 用户口径「增加 0-4 有效选项」]
        #: True=低于 4mA 按线性延续显示（压差计的负压特殊处理）；False=
        #: 欠量程（钳到量程下限）。
        #: [缺省 True=老口径逐位不变] 2026-11-17 定案「低于量程下限=负压」
        #: 之后**所有**换算表都是线性延续 ⇒ 裸构造/老配置文件条目（没有
        #: belowValid 键）必须保持原行为；「关」只在 AnalogConfig 的温度侧
        #: 显式给（温度缺省欠量程更保守）。
        self.below_valid = bool(kw.get("belowValid", True))
        self.low_ma = _float(kw.get("lowMa", 4.0), 4.0)
        self.high_ma = _float(kw.get("highMa", 20.0), 20.0)

    @property
    def configured(self):
        return self.full_scale > 0 and self.high_ma > self.low_ma

    def engineering(self, ma):
        """电流(mA) → 工程量；未标定返回 None。

        :returns: (value, text)；text 为空表示正常，非空是越限说明。

[零点容差] 4~20mA 变送器的活零点实测会略低于 4.000mA
        （本现场 995mV / 249Ω = 3.996mA），那是正常的零点抖动。
        故留 0.05mA（≈0.3%）容差；容差带内**不标越限**。
        [2026-11-23 订正] 容差带内不再一律按零读：below_valid=True 的通道
        保留线性负值（3.984mA ⇒ -0.001），只有"负零"（|值-下限| < 5e-4×
        量程）归到量程下限 —— 见 engineering 的容差带分支。

        [低于量程下限 = 负压，不是断线 2026-11-17 用户口径] 现场压差计
        零点以下是真负压 ⇒ **按线性继续算出负值**并标「负压」；不再夹到 0
        也不写"断线/欠量程"（那是误报：零点附近本来就在 4mA 以下）。
        （真断线由**设备健康**那条线报：Modbus 读不到 ⇒ 健康档「离线」，
        不靠 mA 电平猜。）
        """
        if not self.configured or ma is None:
            return None, ""
        # [断线判定 2026-11-20 用户口径「电流值是 0 应该判断为断线」]
        # 回路无电流（0mA 或远低于 4mA）⇒ **不给数值**（None）：界面显示
        # "--"、超温/压差保护不拿它判定（读不到 ≠ 超温/压差过大）。
        # 与「欠量程」（3.9mA 这种零点漂移，钳到下限并标注）区分开。
        if float(ma) <= self.BROKEN_MA:
            return None, "断线（%.2fmA 回路无电流）" % float(ma)
        span = self.high_ma - self.low_ma
        value = self.low_value \
            + (float(ma) - self.low_ma) / span * (self.full_scale
                                                  - self.low_value)
        note = ""
        if ma < self.low_ma - 0.05:
            if self.below_valid:
                # [0~4 段有效] 线性延续：压差计 4mA 以下是真负压（2026-11-17
                # 用户口径），其它按此配的传感器同理 ⇒ 直接用线性值。
                note = "低于 %.1fmA（负压/线性延续）" % self.low_ma
            else:
                # 欠量程：不是断线（断线走设备健康），钳到量程下限并标注。
                value = self.low_value
                note = "低于 %.1fmA（欠量程，按量程下限）" % self.low_ma
        elif ma > self.high_ma + 0.05:
            value = self.full_scale
            note = "高于 %.1fmA（超量程）" % self.high_ma
        else:
            # [容差带内 2026-11-23 订正：负压不再被夹回 0] 现场实锅：压差计
            # 输入 3.984mA（真负压）线性值 -0.001，但这里原来无条件
            # `max(value, low_value)` ⇒ 夹回 0 ⇒ 界面/历史显示 "0"（用户报
            # 「负压时显示 0，应该显示 -0.00x」）。现按下面两路口径处理：
            #   · 上限侧**照旧**夹到 full_scale —— 零点漂移不该冒出高于
            #     满量程的值；
            #   · below_valid=True（压差计）：下限侧**保留线性负值**（零点
            #     以下是真负压，2026-11-17 用户口径）；
            #   · below_valid=False（缺省的温度传感器）：钳到量程下限，
            #     且**不标越限**（带外 <lowMa-0.05 才报「欠量程」）；
            #     容差带内一律不拿零点抖动当越限（既有口径，本轮不动）；
            #   · 防「负零」：|value - lowValue| < 5e-4 × 量程时归到 lowValue
            #     —— 3.996mA（活零点）⇒ -0.00025 ⇒ 0.0，界面不会显示
            #     "-0.000"；温度量程 100℃ 时阈值 0.05℃（1 位小数不会出现
            #     "-0.0"）。量程为 0 时用 1.0 兜底（避免零阈值）。
            raw_value = value
            span_abs = abs(self.full_scale - self.low_value) or 1.0
            near_low = abs(raw_value - self.low_value) < 5e-4 * span_abs
            if self.below_valid:
                value = min(value, self.full_scale)
                if near_low:
                    value = self.low_value
            else:
                # below_valid=False：钳下限；容差带内**不标越限**
                # （2026-11-17/2026-11-20 既有口径，别在本轮顺手改：
                #  +0.05mA 容差带就是为了不把活零点抖动当越限报）。
                value = min(max(value, self.low_value), self.full_scale)
        return value, note

    def to_doc(self):
        return {"index": self.index, "name": self.name, "unit": self.unit,
                "fullScale": self.full_scale, "lowValue": self.low_value,
                "belowValid": self.below_valid,
                "lowMa": self.low_ma, "highMa": self.high_ma}


class AnalogConfig(object):
    """模拟量输入模块（Modbus TCP）。"""

    __slots__ = ("host", "port", "unit_id", "channels", "poll_ms", "scales",
                 "press_channel", "press_low_mpa", "press_high_mpa",
                 "press_below4_valid", "press1_channel", "press1_low_mpa",
                 "press1_high_mpa", "press1_below4_valid", "press2_channel",
                 "press2_low_mpa", "press2_high_mpa",
                 "press2_below4_valid", "temp_channel", "temp_low_c",
                 "temp_high_c", "temp_below4_valid")

    def __init__(self, **kw):
        self.host = kw.get("host", "192.168.0.11")
        self.port = int(kw.get("port", 8234))
        self.unit_id = int(kw.get("unit_id", 1))
        self.channels = int(kw.get("channels", 8))
        self.poll_ms = int(kw.get("poll_ms", 200))
        #: 通道号 -> AnalogChannelScale（只放需要换算的通道）。
        #:
        #: [通道 0 默认就是压差计] 现场通道 0 接 EJA110A 差压变送器，
        #: 出厂量程 0~1MPa、2 线制 4~20mA。这属于**设备本身的口径**，
        #: 不是站点私有参数 ⇒ 写进默认值。原先留空，别人拉取后模拟量页
        #: 只显示电流值、没有"压差计 / MPa"名字与换算 —— 与实际不一致。
        #:
        #: [通道 1 = 温度传感器 2026-11-20 用户口径「模拟量又加了一个温度
        #: 传感器 0 号通道有了，就加在 1 号通道」] 缺省量程 0~100℃。
        #: [量程=下限~上限 2026-11-20 用户口径「只要填上限和下限即可」]
        #: 4mA → 下限、20mA → 上限（AnalogChannelScale.lowValue 承载）；
        #: 压差计同样可配（缺省 0~1 MPa 与出厂口径一致）。通道号与范围
        #: **远端可配**（press_channel / press_low_mpa / press_high_mpa /
        #: temp_channel / temp_low_c / temp_high_c —— 维护更换传感器时
        #: 不用改代码）。
        self.press_channel = int(kw.get("press_channel", 0))
        self.press_low_mpa = float(kw.get("press_low_mpa", 0.0))
        self.press_high_mpa = float(kw.get("press_high_mpa", 1.0))
        # [压差计缺省开] 现场 4mA 以下是真负压（2026-11-17 用户口径 +
        # 2026-11-20「跟现在这样」确认）⇒ 缺省保持负压处理。
        self.press_below4_valid = bool(kw.get("press_below4_valid", True))
        # [两台压力传感器 2026-11-23 现场新增] 名字「压力1」「压力2」，
        # 单位 MPa。缺省挂在压差计(CH0)/温度(CH1)之外的空通道 CH2/CH3；
        # 量程缺省 0~1MPa（与压差计出厂口径同）。低于 4mA 缺省**无效**
        # （按欠量程钳到下限）——压力传感器没有压差计那种「真负压」口径。
        self.press1_channel = int(kw.get("press1_channel", 2))
        self.press1_low_mpa = float(kw.get("press1_low_mpa", 0.0))
        self.press1_high_mpa = float(kw.get("press1_high_mpa", 1.0))
        self.press1_below4_valid = bool(
            kw.get("press1_below4_valid", False))
        self.press2_channel = int(kw.get("press2_channel", 3))
        self.press2_low_mpa = float(kw.get("press2_low_mpa", 0.0))
        self.press2_high_mpa = float(kw.get("press2_high_mpa", 1.0))
        self.press2_below4_valid = bool(
            kw.get("press2_below4_valid", False))
        self.temp_channel = int(kw.get("temp_channel", 1))
        self.temp_low_c = float(kw.get("temp_low_c", 0.0))
        self.temp_high_c = float(kw.get("temp_high_c", 100.0))
        # [温度缺省关] 低于 4mA 多半是接线/变送器问题 ⇒ 按欠量程（保护
        # 判据读不到真值就不会误动作）；现场要线性延续再打开。
        self.temp_below4_valid = bool(kw.get("temp_below4_valid", False))
        self.rebuild_scales()

    def rebuild_scales(self):
        """按**可配的通道号与量程**重建 scales（远端设置 SET 后调）。

        [为什么重建而不是改 key] AnalogChannelScale.index 与字典 key 必须
        一致（模拟量页按 index 显示）⇒ 通道号变更 = 重建整表。
        [建表顺序 2026-11-23] 压差计 → 压力1(进水压力) → 压力2(出水压力) → 温度传感器：
          · 压力侧**后者覆盖前者**（同一通道时后配置者赢——用户口径「方便
            后期维护更换」）；被覆盖的压力传感器静默丢失条目（SET 入口对
            压力通道撞号已显式拒绝，只有历史配置/挪温度通道才会走到）。
          · 温度传感器与**压差计**撞号时按既有口径**温度让位**（G8：压差表
            保留）；与压力1/压力2撞号时温度表覆盖它们（后配置者赢——温度
            通道是用户刚挪的，界面要看得到温度换算）。
        """
        self.scales = {}
        self.scales[self.press_channel] = AnalogChannelScale(
            index=self.press_channel, name=proto.PRESSURE_LABEL, unit="MPa",
            fullScale=self.press_high_mpa, lowValue=self.press_low_mpa,
            belowValid=self.press_below4_valid,
            lowMa=4.0, highMa=20.0)
        self.scales[self.press1_channel] = AnalogChannelScale(
            index=self.press1_channel, name=proto.PRESS1_LABEL, unit="MPa",
            fullScale=self.press1_high_mpa, lowValue=self.press1_low_mpa,
            belowValid=self.press1_below4_valid,
            lowMa=4.0, highMa=20.0)
        self.scales[self.press2_channel] = AnalogChannelScale(
            index=self.press2_channel, name=proto.PRESS2_LABEL, unit="MPa",
            fullScale=self.press2_high_mpa, lowValue=self.press2_low_mpa,
            belowValid=self.press2_below4_valid,
            lowMa=4.0, highMa=20.0)
        if self.temp_channel != self.press_channel:
            self.scales[self.temp_channel] = AnalogChannelScale(
                index=self.temp_channel, name="温度传感器", unit="℃",
                fullScale=self.temp_high_c, lowValue=self.temp_low_c,
                belowValid=self.temp_below4_valid,
                lowMa=4.0, highMa=20.0)

    def temp_c(self, ma_map):
        """温度传感器工程量（℃）；未标定/通道无读数 ⇒ None。

        :param ma_map: 通道号 -> mA（AnalogInput.ma 的包装）
        """
        scale = self.scales.get(self.temp_channel)
        if scale is None or not scale.configured:
            return None
        ma = ma_map(self.temp_channel)
        if ma is None:
            return None
        value, _note = scale.engineering(ma)
        return value


class DigitalConfig(object):
    """一台数字量模块（Modbus TCP）。

    [点数口径] 单模块物理 16 点；该模块支持**主从聚合**（把另一台 16 点
    模块映射进自己的地址空间，对外 32 点），故点数是**每模块可变**的，
    不能写死 16。但本现场 2026-11-06 起只保留 .13 一台（16 点）——
    .12（原聚合者，对外 32 点）已下线拆除，它的 16DI/16DO 不再出现在
    点表里。默认值按"单台 16 点"给出。
    """

    __slots__ = ("name", "host", "port", "unit_id", "timeout_s", "channels",
                 "publish")

    def __init__(self, name, **kw):
        self.name = name
        self.host = kw.get("host", "192.168.0.13")
        self.port = int(kw.get("port", 502))
        self.unit_id = int(kw.get("unit_id", 1))
        #: 该模块应答实测 0.4~0.8s（比其他设备慢一个数量级），超时给足。
        self.timeout_s = float(kw.get("timeout_s", 2.0))
        #: DI/DO 路数（单模块 16；聚合后 32）。
        #: 起始地址恒 0，且起始+点数不得跨出模块自己的地址空间。
        self.channels = int(kw.get("channels", 16))
        #: 是否发布点位。
        #: [为什么需要] 聚合架构下，被聚合者的点已经出现在聚合者的地址
        #: 空间里。若两台都发布，客户端会看到两套重复点位。但被聚合者
        #: **仍要保留**：它自己是否在线是独立故障信息（模块断电/网线松/
        #: 被聚合链路断，只有它自己的应答能反映）——故照常轮询并出健康
        #: 条目，只是不发点位帧。
        #: [当前现场已无聚合] 只剩 .13 一台 ⇒ 恒为 True（默认值）。
        self.publish = bool(kw.get("publish", True))


class StorageConfig(object):
    """历史/日志/报警的 SQLite 落盘配置（含容量限额）。

    [为什么默认开] 历史数据的价值全在「事后」，进程一重启内存缓存就没了；
    落盘默认开、路径默认落在**服务端程序目录**（不依赖任何安装选择），
    现场不需要做任何配置就能拿到完整数据。要挪到别处（如挂载的大容量
    分区）改 `path` 即可，远端设置页可在线改。

    [容量限额为什么必须给] 现场那台机器硬盘很小（用户明确提出的约束），
    不封顶就会一路写到把盘撑满，最后连系统日志都写不进去。`max_mb=0`
    表示不限（缺省），现场按可用空间填一个值即可；写满后的行为由
    `full_policy` 三选一（见 store.py 模块头注）。
    """

    __slots__ = ("enabled", "path", "sample_ms", "max_kb", "full_policy",
                 "keep_files", "history_trigger")

    def __init__(self, **kw):
        self.enabled = bool(kw.get("enabled", True))
        #: 数据库文件路径；**空串 = 服务端程序目录下的 hsf_data.db**。
        self.path = str(kw.get("path", "") or "")
        #: 历史采样周期（毫秒）。历史是慢变量（趋势排查用），1 分钟一行
        #: 一天约 1440 行，SQLite 毫无压力；周期推送（100ms）没必要入库。
        #: 缺省 1 分钟（2026-11-11 用户定案；原先 5s，库体积是现在的 12 倍）。
        self.sample_ms = int(kw.get("sample_ms", proto.STORAGE_SAMPLE_MS_DEF))
        #: 库文件容量上限（**KB**，内部统一口径）；0 = 不限。
        #: 远端协议用 MB（max_mb 属性换算），配置文件也接受 max_kb 直给。
        #: 缺省 50MB（用户定案；原先 0=不限，不封顶会一路写到把盘撑满）。
        #: [注意] **显式写 0 仍然是"不限"** —— 只在"键不存在"时才用缺省。
        _raw_max_kb = kw.get("max_kb", None)
        self.max_kb = (int(_raw_max_kb) if _raw_max_kb is not None
                       else proto.STORAGE_MAX_MB_DEF * 1024)
        #: 满额策略：stop / ring / rotate（见 store.POLICIES）。
        self.full_policy = str(kw.get("full_policy", "") or "") or "ring"
        #: rotate 模式保留的分段数（含当前库）。
        self.keep_files = int(kw.get("keep_files", 3) or 3)
        #: **历史入库的触发方式**（2026-11-12 用户定案）：
        #:   · always（缺省）= 持续采集入库（与自动模式无关，保持既有行为）；
        #:   · auto = 仅自动运行时入库（停机/手动期间不记历史）。
        #: 只影响落盘；遥测采集与界面推送照旧。
        self.history_trigger = str(
            kw.get("history_trigger", "") or "") or proto.HISTORY_TRIGGER_ALWAYS

    @property
    def max_mb(self):
        """容量上限（MB，远端协议口径；非整数 MB 的上限向下取整）。"""
        return self.max_kb // 1024

    @max_mb.setter
    def max_mb(self, value):
        self.max_kb = max(0, int(value)) * 1024

    def set_max_kb(self, kb):
        self.max_kb = max(0, int(kb))


class Config(object):
    """全局配置。"""

    def __init__(self):
        self.base_dir = "."
        self.config_path = ""
        self.users_path = ""
        self.port = DEFAULT_PORT
        #: 串口总开关。板上 demo 以 Modbus 从站模式独占 ttyS3 时必须关掉，
        #: 否则两边抢同一个串口（打开失败或互相踢）。
        self.enable_serial = True
        self.enable_vfd = True
        self.enable_temphum = True
        self.enable_analog = True
        self.enable_digital = True
        #: 电磁流量计（LD 系列，Modbus-RTU）。
        #:
        #: [为什么默认关] 现场设备表里还没接流量计（C 端有 XFlowMeter 类
        #: 与真机联调记录，但本 Python 服务端的现场配置里没有这一路）。
        #: 默认关掉的另一个原因是它和变频器/温湿度抢同一条 485：多一个从站
        #: 就多一份轮询时间，没接的从站会一直超时拖慢整条总线。
        #: 接了设备就把 `serial.flowmeter_enabled` 置 true（或用 `--sim`
        #: 起来看虚拟数据）。
        # [默认开 2026-11-05] 现场这三样都在同一路 485 上，代码默认却关着
        # ⇒ 别人拉取后水质页整页 "--"。默认值与现场一致，站点确实没接的
        # 再用 cfg 关掉（多挂一个未接从站会拖慢同一条总线的轮询）。
        self.enable_flowmeter = True
        self.flowmeter_unit_id = 5
        #: [--sim] 虚拟流量计的基准值（缺省=2026-11-09 ttyS4 实测口径：
        #: 瞬时流量 3.6 m³/h、管道已充液⇒空管报警 False。实测当天管道
        #: 空置⇒读数 0.0+空管 True，模拟器取"灌水后"的可用水口径，空管
        #: 报警链路由配置 `flowmeter_sim_empty=true` 复现）。单位码不在
        #: 这里配：0x1020 单位由仪表侧寄存器上报（5=m³/h），不能写死。
        self.flowmeter_sim_flow = 3.6
        self.flowmeter_sim_empty = False
        #: 浊度仪 ×2（同一 485，不同站号）。**站号现场不确定**，走配置。
        #:
        #: [为什么默认关] 与流量计同理：现场还没接。且站号 1 可能与总线上
        #: 其它设备冲突，接上后要先确认再开。
        self.enable_turbidity = True
        #: [逐台开关 2026-11-10] 两台浊度仪是两台**独立设备**（站1/站3，
        #: 型号与字节序都不同）：现场可能只装一台、或只停采一台做检修。
        #: 上面那个 enable_turbidity 降为**共同缺省**（缺逐台键时回落它），
        #: 实际门控看这两个；键名与 C 端 HSF_CFGK_TURBIDITY{1,2}_ENABLED 同字面。
        self.enable_turbidity1 = True
        self.enable_turbidity2 = True
        #: 两台浊度仪的站号（现场手工设定，走配置，代码不写死）。
        #:
        #: [站号沿革] 2026-11-09 ttyS4 实测：站 3 全地址零应答（普查 1~20
        #: 只有 1/2/5 在线），在线两台站 1 WZT-701A + 站 2 ZHEYI-5100ZDY。
        #: 2026-11-10 现场变更：站 2 浊度仪（浙仪 ZHEYI-5100ZDY）面板站号
        #: 已改为 3（原 2）。字节序/读取数不受站号影响（仍 DCBA/×4）。
        self.turbidity_unit_ids = [1, 3]
        #: 每台的字节序（与 unit_ids 按下标一一对应）。
        #:
        #: [为什么按台配] 实测两家字节序**不同**：站1 CDAB（低字在前）、
        #: 站2 DCBA（4 字节整字节反序 = 小端 float32）。按旧口径"两台通用
        #: CDAB"读站2 得 0.0029（真值 0.0458），小 4 个数量级且**不报错**
        #: （实测文档 §5.3 四口径对照）。值域 CDAB/DCBA/ABCD/BADC。
        self.turbidity_byte_orders = ["CDAB", "DCBA"]
        #: 每台一次读的寄存器数：站1 读 2（只有浊度）；站2 读 4——一帧
        #: 同刻拿浊度+温度（0x0000~0x0003，温度在后两字；分两帧读会有
        #: 非同刻误差）。温度只进设备层与日志，不进推送帧。
        self.turbidity_read_counts = [2, 4]
        #: [--sim] 虚拟浊度仪的基准值（NTU / ℃，缺省=2026-11-09 实测口径：
        #: 站1 2.29 NTU 无温度；站2 0.045 NTU + 19.3 ℃）。温度 None=该台
        #: 无温度能力，虚拟从站 0x0002/0x0003 回 0。模拟器的验收口径：
        #: 用与真实设备**相同的解码路径**能读回相同值。
        self.turbidity_sim_ntu = [2.29, 0.045]
        self.turbidity_sim_temp = [None, 19.3]
        self.turbidity_stale_ms = 5000
        #: 浊度仪轮询/推送节拍：浊度是慢变量，500ms 足够。
        self.turbidity_poll_ms = 500
        #: [2026-11-10 逐台] 两台浊度仪各自周期（缺省跟随共同缺省）。
        self.turbidity1_poll_ms = 500
        self.turbidity2_poll_ms = 500
        #: [显示渐变 2026-11-24] 首页浊度数字按「蓝→绿→红」着色用的范围；
        #: 两台表量程不同 ⇒ 逐台可配（远端设置 collect 节）。只影响客户端
        #: 显示，不参与报警/保护。缺省沿用旧显示阈值 0~20 NTU。
        self.turbidity1_range_min = proto.TURBIDITY_RANGE_MIN_DEF
        self.turbidity1_range_max = proto.TURBIDITY_RANGE_MAX_DEF
        self.turbidity2_range_min = proto.TURBIDITY_RANGE_MIN_DEF
        self.turbidity2_range_max = proto.TURBIDITY_RANGE_MAX_DEF
        #: 电箱温度（温湿度站）显示渐变范围（℃，只影响显示）。
        self.temphum_temp_low_c = proto.TEMPHUM_TEMP_LOW_C_DEF
        self.temphum_temp_high_c = proto.TEMPHUM_TEMP_HIGH_C_DEF
        #: [电机转速渐变 2026-11-24] 搅拌泵转速显示范围（rpm）。
        self.vfd_speed_range_min = proto.VFD_SPEED_RANGE_MIN_DEF
        self.vfd_speed_range_max = proto.VFD_SPEED_RANGE_MAX_DEF
        #: [电机温度渐变 2026-11-24] 电机温度显示着色范围（℃）。
        self.vfd_motor_temp_range_min = proto.VFD_MOTOR_TEMP_RANGE_MIN_DEF
        self.vfd_motor_temp_range_max = proto.VFD_MOTOR_TEMP_RANGE_MAX_DEF
        #: [显示渐变逐项开关 2026-11-24] 每一项可单独关（关=该项恢复单色）。
        self.turbidity1_gradient_enabled = True
        self.turbidity2_gradient_enabled = True
        self.temphum_temp_gradient_enabled = True
        self.vfd_speed_gradient_enabled = True
        self.vfd_motor_temp_gradient_enabled = True
        self.press_gradient_enabled = True
        self.press1_gradient_enabled = True
        self.press2_gradient_enabled = True
        #: 流量计周期（0=跟随温湿度，旧行为；显式设过就按该值）。
        self.flowmeter_poll_ms = 0
        #: [滤速 2026-11-25] 过滤面积（m²）：滤速 = 瞬时流量 / 面积。
        #: 0 = 未设置（不计算，界面 "--"）；只影响显示换算。
        self.filter_area_m2 = proto.FILTER_AREA_M2_DEF
        #: 进水泵（数字量模块上的一路 DO，只有输出、无反馈）。
        self.enable_pump = True
        #: 泵点位：DO6 = 第 6 路电磁阀（DO5）的下一个口。
        #: [2026-11-06] 随阀门点位整体减 16（原 DO22 → DO6）。
        self.pump_items = [{"name": "进水泵", "module": 0, "do": 6}]
        self.pump_stale_ms = 5000
        #: 按钮组（现场操作柱）：4 个 DI 输入 + 3 个 DO 指示灯。
        #:
        #: [2026-11-06 现场定义，用户口径]
        #:   输入 12 = 急停（**常闭**：无信号 = 按下/断开 = 禁止启动）
        #:   输入 13 = 启动按钮
        #:   输入 14 = 停止按钮
        #:   输入 15 = 复位按钮
        #:   输出 7  = 启动按钮灯（运行时常亮）
        #:   输出 8  = 停止按钮灯（停止时常亮，与启动灯互斥）
        #:   输出 9  = 复位按钮灯（**点动**：只在按下复位时亮）
        #:
        #: [为什么点位写进默认值] 与阀门/泵同性质——是**图纸确定的接线**，
        #: 不是站点私有参数。别人 clone 下来跑就该看到同一套点位。
        self.enable_buttons = True
        self.button_items = {
            # 输入（DI 通道号）
            "estop_di": 12,
            "start_di": 13,
            "stop_di": 14,
            "reset_di": 15,
            # 输出（DO 通道号）
            "start_lamp_do": 7,
            "stop_lamp_do": 8,
            "reset_lamp_do": 9,
            # 模块下标（cfg.digital_modules 顺序）
            "module": 0,
        }
        #: 按键去抖/沿判定窗口（毫秒）。DI 扫描 200ms 一拍，给够两拍余量。
        self.button_debounce_ms = 300
        #: 虚拟设备模式：用内存寄存器模型代替真实设备（见 virtualdev.py）。
        #: [语义] 只换**设备物理层**，devices.py 与 app.py 全部用真实实现 ——
        #: 换算、状态字解码、控制闸门、点动看门狗、阀定时器都照跑。
        #: 真实设备完全不碰（不开串口、不建 TCP），设备拆了也能跑。
        #: 打开方式：`python -m hsfsrv --sim`（**命令行开关**；配置文件的
        #: "sim" 节只是时序旋钮——没有 enabled 键，别被旧注释误导）。
        self.sim_enabled = False
        #: 虚拟数字量模块的"抖动"概率（0=不抖）。真机实测无间隔连发会间歇
        #: 空应答；默认关掉，免得日常调试被随机失败干扰。
        self.sim_flaky_rate = 0.0
        #: 虚拟电磁阀**动作行程时间**（毫秒）。
        #:
        #: [为什么必须有这个参数] 现场阀满开满关约 10s、服务端 20s 判超时。
        #: 若虚拟模块"写完 DO 立刻到位"，状态机的"动作中"与"超时报警"两条
        #: 分支永远走不到 —— 功能写了却没法验证。
        #:   10000（缺省）  现场真实节奏：10s 到位，界面能看到"动作中"
        #:   25000          行程过长：20s 时报警（用来验证告警链路）
        #:   -1             阀卡死：永远不到位（验证"无到位反馈"文案）
        self.sim_valve_move_ms = 10000
        #: 虚拟行程开关"没接"：DI 恒 0。
        self.sim_valve_no_feedback = False
        #: [--sim] 虚拟从站**应答时延**（毫秒 [min,max]，应答路径内按站号
        #: 随机取值）——2026-11-09 ttyS4 实测口径（docs/HSF板端ttyS4三设备
        #: 取数实测总表 §1.1「响应时延」列）：站 1 37~43、站 2 31~33、
        #: 站 5 68~69。真实仪表的应答从来不是零时延；模拟器带上它，主站的
        #: 站间间隙逻辑才不是只跑不验的死代码。要压联调时长可经配置把任
        #: 一档清零（C 侧同款旋钮 = HsfServerConfig.h 的 HSF_SIM_* 宏）。
        self.sim_turbidity_reply_ms = [(37, 43), (31, 33)]
        self.sim_flowmeter_reply_ms = (68, 69)
        #: [--sim] 站 1 的「长帧后恢复期」窗口（毫秒）：站 5 的 49 字节长帧
        #: 收口后窗口内站 1 对任何请求无应答（实测 §8.3：0ms 2/10、75ms
        #: 0/10、125ms 起 10/10，取实测门槛 125）。主站侧
        #: serial_b.poll_gap_ms=200 刻意大于它 ⇒ 模拟联调时站 1 不掉线。
        #: 0 = 关闭该建模。
        self.sim_recovery_ms = 125
        #: [--sim] 浊度微波动幅（NTU，绝对值绕基准正弦；按下标分台）。
        #: 实测 40s × 20 轮：站 1 恒定（幅 0）、站 2 0.045±0.001。
        self.turbidity_sim_wobble = [0.0, 0.001]
        #: [--sim] 温度漂移幅（℃，绕基准正弦；按下标分台）。
        #: 实测温度 19.25~19.3125 ⇒ 站 2 传 0.05；站 1 无温度（幅不起作用）。
        self.turbidity_sim_temp_wobble = [0.0, 0.05]
        self.vfd_unit_id = 2
        self.temphum_unit_id = 4
        self.vfd_stale_ms = 5000
        self.temphum_stale_ms = 5000
        self.flowmeter_stale_ms = 5000
        self.digital_stale_ms = 5000
        self.digital_poll_ms = 200
        self.serial = SerialConfig()
        #: 第二条独立 485：流量计 + 浊度仪 ×2（9600）。
        #: [串口号怎么定] 现场两条线各占一个 USB-485 转换器，Windows 下
        #: 通常是相邻的两个 COM 口。缺省从路 A 的口名推一个"下一个口"
        #: （COM17 → COM18），也可由配置 `serial_b.port` 显式指定。
        #: 关键是**速率必须不同**：路 A 115200、路 B 9600。
        #: [poll_gap_ms=200] 路 B 三台从站里站1（浊度）在站5（流量计）
        #: 长帧后需要 ≥200ms 恢复期（SerialConfig.poll_gap_ms 注释有据）。
        self.serial_b = SerialConfig(port=_next_serial_port(self.serial.port),
                                     baudrate=9600,
                                     poll_gap_ms=200)
        self.analog = AnalogConfig()
        # [默认值＝**现场口径**，不是"够用的最小集"]
        #
        # 实锅 2026-11-05：`cfg/` 被 .gitignore 排除（运行产物，合理），
        # 但现场参数只存在于那个文件里 ⇒ 别人 clone 下来跑的是"代码默认值"，
        # 而默认值当时是"16 点 + .13 也发布"，于是现象是：
        #     · 数字量页只有 16 点（.13 的 16 点重复出现两套）
        #     · 模拟量压差计没名字（默认 scales 为空）
        #     · 电磁阀 0 路、流量计/浊度不装配
        # 用户原话："别人拉取显示不一样"。
        #
        # 故把现场确定的接线上限写进默认值：没有配置文件也能跑出与现场
        # 一致的界面。真正的**站点私有**差异（串口号、IP）仍由 cfg 覆盖。
        #
        # 2026-11-06 变更：.12（原聚合者，对外 32 点）已下线拆除，现场只
        # 保留 .13 一台 16 点模块（ms=1 主站模式，实测 FC01/FC02 读 16 点、
        # FC05 写单点均可用）。原先由 .12 提供的 16DI/16DO 一并取消 ——
        # 对外点表从 32 点降到 16 点，这是**有意收窄**，不是漏配。
        self.digital_modules = [
            DigitalConfig("数字量", host="192.168.0.13", channels=16),
        ]
        # ---- 电磁阀（6 路，每路 1 DO + 2 DI 反馈）----
        # 点位**先留空**：现场还没定用哪个口。留空时该路显示"未配置"，
        # 控制被拒，界面能先跑起来；接线定下来后只改 items 即可。
        self.enable_valves = True
        self.valve_poll_ms = 200
        #: 从下发命令到"必须到位"的时限；超时判故障并向订阅者推送告警。
        #: [20s 的依据] 现场实测一个阀满开→满关约 10s；20s 是两倍余量，
        #: 既能容忍阀体老化/水压偏低导致的偏慢，又不至于让人等太久
        #: 才看到告警。（原来的 8000ms 对 10s 行程根本不够，会误报。）
        self.valve_move_timeout_ms = 20000
        #: 每项 {name, module, do, openDi, closeDi}；module=None 表示未配置。
        #:
        #: [默认填现场接线 2026-11-05，2026-11-06 点位整体减 16]
        #: 原先六路在「.12 聚合上半区」DO16..21；.12 拆除后只剩 .13 一台
        #: 16 点模块、点表从 0 起，故整体减 16 → DO0..5。物理端子没动，
        #: 变的只是寻址口径（上半区第 n 点本来就是 .13 自己的第 n 点）。
        #:   阀 n：DO n；**开到位 DI 在前、关到位 DI 在后**
        #:
        #: [开/关位序 2026-11-10 按现场接线订正] 板端在跑的 cfg
        #: （hsf_server_py.json 的 valves.items）是 openDi=0,2,4…（开在前）
        #: / closeDi=1,3,5…（关在后），用户已实机核对过设备定义；而本仓
        #: 默认值原来取反了（openDi=2n+1）。两者不一致时**同一个 DI 位图
        #: 两端读出相反的阀位**（板端实测 DI 位图=20482 时，一端判
        #: 「阀1 开到位」、另一端判「阀1 关到位」）——这不是显示差异，是
        #: 现场结论反了。故默认值订正为开在前，与板端 cfg 一致，也与
        #: valves.py 顶部的接线示例一致。C 端宏 HSF_VALVE_OPEN_DI 同步。
        self.valve_items = [
            {"name": VALVE_DEFAULT_NAMES[i], "module": 0,
             "do": i, "openDi": i * 2, "closeDi": i * 2 + 1}
            for i in range(6)
        ]
        # ---- 数据落盘（历史/日志/报警 → SQLite）----
        self.storage = StorageConfig()
        # ---- 登录/账户策略（与 C 侧 XLoginPolicy 同义，2026-11-08）----
        #: 允许匿名（只读）会话接入。
        self.login_allow_anonymous = False
        #: 允许同一账户并发在线（False ⇒ 沿用「root 单会话」保护）。
        self.login_allow_concurrent = False
        #: 会话总数上限（0=不限）。
        self.login_max_sessions = 8
        #: auto 节顶层：搅拌泵最高转速限制（rpm；2026-11-25 用户定案）。
        #: 服务端在自动启动/手动调速/点动等**所有控制入口**钳位，UI 同步
        #: 把输入上限压到该值。100~1440，缺省 1440 = 不额外限制。
        self.stir_max_rpm = proto.AUTO_STIR_MAX_RPM_DEF
        # ---- 自动模式配置（2026-11-11；产水/反洗/顺洗各一份，缺省全空）----
        #: {mode_id: {"devices":[稳定id…], "stir_rpm":int, "stir_reverse":bool}}
        #: 「自动」组合模式不存配置（它将来按流量循环调用这三个）。
        #:
        #: [2026-11-18 放水模式] 这里存的是**可配置**的模式集合
        #: （proto.AUTO_CONFIG_MODES = 三模式 + 放水）⇒ 放水的配置能存
        #: 能读回（用户配了不丢、服务端 GET 能回显）；但它**跑不起来**
        #: （启动闸只认 proto.AUTO_MODES 三模式 + 组合「auto」）。
        self.auto = {mid: self._default_auto_mode()
                     for mid, _t in proto.AUTO_CONFIG_MODES}
        # ---- 自动循环（组合模式「自动」）配置（2026-11-12 用户定案）----
        #: 循环的**参与模式与顺序可配**（2026-11-18）：缺省＝产水 →（压差/
        #: 定时兜底）→ 反洗 →（定时）→ 顺洗 →（定时）→ 产水 ……直到人按停止。
        self.auto_cycle = self._default_auto_cycle()
        # ---- 按钮灯闪烁时长（2026-11-24 用户定案）----
        #: 三盏灯各自亮/灭毫秒（auto.lights 子节；协议键 camelCase）。
        self.auto_lights = {
            proto.AUTO_LIGHT_START_ON: proto.AUTO_BLINK_ON_MS_DEF,
            proto.AUTO_LIGHT_START_OFF: proto.AUTO_BLINK_OFF_MS_DEF,
            proto.AUTO_LIGHT_STOP_ON: proto.AUTO_BLINK_ON_MS_DEF,
            proto.AUTO_LIGHT_STOP_OFF: proto.AUTO_BLINK_OFF_MS_DEF,
            proto.AUTO_LIGHT_RESET_ON: proto.AUTO_BLINK_ON_MS_DEF,
            proto.AUTO_LIGHT_RESET_OFF: proto.AUTO_BLINK_OFF_MS_DEF,
        }
        # ---- 运行保护（protect 节；2026-11-15 用户定案）----
        #: 进水泵空管超时（毫秒）：进水泵开着（DO6=1）且流量计仍报空管，
        #: **连续**这么久 ⇒ 保护动作。0 = 关闭该保护（缺省 60s）。
        self.protect_inlet_noflow_timeout_ms = \
            proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF
        #: 超时后是否**停止设备**（True=停；False=只报警）。
        #: [为什么是开关] 现场流量计的"空管"位可能长期挂着（实测板端 3.6 m³/h
        #: 时也带空管报警）⇒ 一律停机会把线停死 ⇒ 由现场自己选。
        self.protect_inlet_noflow_stop = proto.PROTECT_INLET_NOFLOW_STOP_DEF
        #: 压差过大保护是否启用（缺省**不启用**：阈值与现场工艺强相关）。
        self.protect_press_high_enabled = proto.PROTECT_PRESS_HIGH_ENABLED_DEF
        #: 压差上限（MPa；缺省 0.8 —— 正常运行约 0.5 MPa）。
        self.protect_press_high_limit_mpa = proto.PROTECT_PRESS_HIGH_LIMIT_MPA_DEF
        #: 压差过大触发后是否停止设备（缺省 true = 停）。
        self.protect_press_high_stop = proto.PROTECT_PRESS_HIGH_STOP_DEF
        #: [电机超温保护 2026-11-20] 是否启用（缺省**不启用**：阈值与电机
        #: 铭牌/工艺相关，由现场自己定）。
        self.protect_motor_over_temp_enabled = \
            proto.PROTECT_MOTOR_OVER_TEMP_ENABLED_DEF
        #: 电机超温阈值（℃；缺省 75 —— B 级绝缘耐受留一倍余量）。
        self.protect_motor_over_temp_limit_c = \
            proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_DEF
        #: 电机超温触发后是否停止设备（缺省 true = 停）。
        self.protect_motor_over_temp_stop = proto.PROTECT_MOTOR_OVER_TEMP_STOP_DEF
        #: [电箱超温保护 2026-11-25] 温湿度变送器电箱温度连续超阈值 ⇒
        #: 报警 +（按配置）停设备 + 锁存。缺省不启用；阈值 60℃。
        self.protect_box_over_temp_enabled = \
            proto.PROTECT_BOX_OVER_TEMP_ENABLED_DEF
        self.protect_box_over_temp_limit_c = \
            proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_DEF
        self.protect_box_over_temp_stop = \
            proto.PROTECT_BOX_OVER_TEMP_STOP_DEF
        #: [压力1/2 过高保护 2026-11-25] 两路压力（模拟量，通道在 collect
        #: 节可配）连续超上限 ⇒ 报警 +（按配置）停设备 + 锁存。缺省不启用。
        self.protect_press1_high_enabled = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_ENABLED_DEF
        self.protect_press1_high_limit_mpa = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_DEF
        self.protect_press1_high_stop = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_STOP_DEF
        self.protect_press2_high_enabled = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_ENABLED_DEF
        self.protect_press2_high_limit_mpa = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_DEF
        self.protect_press2_high_stop = \
            proto.PROTECT_PRESS_CHANNEL_HIGH_STOP_DEF
        # ---- 相机（camera 节；2026-11-19 新增）----
        #: 相机采集总开关（缺省**关**：相机是可选外部设备，现场未必装）。
        self.camera_enabled = proto.CAMERA_ENABLED_DEF
        #: 相机 IP / RTSP 端口 / 账号 / 口令。
        self.camera_ip = proto.CAMERA_IP_DEF
        self.camera_port = proto.CAMERA_PORT_DEF
        self.camera_username = proto.CAMERA_USERNAME_DEF
        self.camera_password = proto.CAMERA_PASSWORD_DEF
        #: 码流档（"main" 主码流 / "minor" 子码流；缺省子码流）。
        self.camera_stream = proto.CAMERA_STREAM_DEF
        #: 采集帧率（**跟随相机档位** 1/10/15/20）。
        self.camera_fps = proto.CAMERA_FPS_DEF
        #: 输出帧宽（等高缩放；缺省 704 = 子码流原始宽度）。
        self.camera_width = proto.CAMERA_WIDTH_DEF
        #: JPEG 质量（1~31，越小越清晰、文件越大）。
        self.camera_quality = proto.CAMERA_QUALITY_DEF
        #: 握手/重连超时（毫秒）。
        self.camera_timeout_ms = proto.CAMERA_TIMEOUT_MS_DEF
        #: [2026-11-19 ES 直传] 传输模式（stream 直传缺省 / jpeg 兜底）、
        #: 期望编码（h264/h265）、客户端解码方式（auto/on/off，远端设置
        #: 可编辑，客户端各自按它选解码器）。
        self.camera_transport = proto.CAMERA_TRANSPORT_DEF
        # [2026-11-20] 连接方式 / MAC / 断线重连（协议 camelCase 见
        # app._CAMERA_ATTR；cfg 侧 snake_case）。
        self.camera_resolve_mode = proto.CAMERA_RESOLVE_DEF
        self.camera_mac = ""
        self.camera_reconnect_enabled = proto.CAMERA_RECONNECT_ENABLED_DEF
        self.camera_reconnect_interval_s = proto. \
            CAMERA_RECONNECT_INTERVAL_S_DEF
        # [2026-11-20] 连接方式 / MAC / 断线重连（协议 camelCase 见
        # app._CAMERA_ATTR；cfg 侧 snake_case）。
        self.camera_resolve_mode = proto.CAMERA_RESOLVE_DEF
        self.camera_mac = ""
        self.camera_reconnect_enabled = proto.CAMERA_RECONNECT_ENABLED_DEF
        self.camera_reconnect_interval_s = proto. \
            CAMERA_RECONNECT_INTERVAL_S_DEF
        self.camera_codec = proto.CAMERA_CODEC_H264
        self.camera_hw_decode = proto.CAMERA_HW_DEF
        self._runtime = {}          # 运行期持久化（run_mode 等）

    @staticmethod
    def _default_auto_cycle():
        """自动循环的缺省配置（定时器兜底**默认不开启**）。

        [2026-11-26 实例化] 循环的真值改在 **instances**（有序模式实例
        列表，同 mode 可重复出现）；旧键（cycle_modes / stage_cfg / 六扁平
        键 / stage_minutes）降级为**派生兼容视图**，由
        `_sync_auto_cycle_legacy` 从 instances 随时算出 —— 老配置零改动
        仍按原样跑（未显式配置 instances 时按旧键合成，行为与旧链逐字
        一致）。

        [事实源只有一个] instances 是**唯一**的配置来源；下面所有旧键是
        它的派生视图（供老读方/旧界面读）—— 不存在"改一处忘另一处"的
        双份真值。
        [instances_explicit] 内存标志：配置文件里**显式**配了 instances
        （且规整后 ≥2 个）或 SET 曾成功写入 ⇒ True。未显式期间 save()
        **不写** instances 键（手改文件里的旧键持续有效）。
        """
        raw = {
            "enabled": proto.AUTO_CYCLE_ENABLED_DEF,
            "press_threshold": proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF,
            "press_compare": proto.AUTO_CYCLE_PRESS_COMPARE_DEF,
            "timer_fallback": proto.AUTO_CYCLE_TIMER_FALLBACK_DEF,
            "timer_minutes": proto.AUTO_CYCLE_TIMER_MINUTES_DEF,
            "backwash_minutes": proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF,
            "rinse_minutes": proto.AUTO_CYCLE_RINSE_MINUTES_DEF,
            "cycle_modes": list(proto.AUTO_CYCLE_MODES_DEF),
            # [顶层循环次数 2026-11-27] 0 = 一直循环（缺省 ⇒ 既有行为不变）；
            # N>=1 = 展开计划完整跑完 N 轮后自动停止。
            "top_repeat": proto.AUTO_CYCLE_TOP_REPEAT_DEF,
        }
        instances = proto.synthesize_cycle_instances(raw, modes_cfg={})
        view = proto.cycle_instances_compat_view(instances)
        raw.update(view)
        raw["instances"] = instances
        raw["instances_explicit"] = False
        return raw

    @staticmethod
    def _default_auto_mode():
        """一个模式的缺省配置：不勾设备、搅拌电机满转速正转。

        [复位开关缺省] 停止=关阀复位（True，保持既有行为）；急停=保持
        现状（False，与用户口径「急停时电动球阀可以不动」一致）。
        [换相策略 2026-11-26 实例化] 两策略键已从**单模式 modes** 删除
        （策略逐实例配置，见 auto.cycle.instances 的 stirSwitchPolicy /
        inletSwitchPolicy）——modes 不再携带策略。
        [2026-11-18 放水模式] 这份缺省**三模式与放水模式共用**
        （放水的缺省＝拷贝三模式口径，不另写第二份实现）。
        """
        return {"devices": [],
                "stir_rpm": proto.AUTO_STIR_RPM_DEF,
                "stir_reverse": False,
                # [2026-11-25] 加速度 rpm/s（0=跟随变频器当前加速时间）。
                proto.AUTO_MODE_STIR_ACCEL_RPM_S:
                    proto.AUTO_STIR_ACCEL_RPM_S_DEF,
                proto.AUTO_MODE_RESET_VALVES_ON_STOP:
                    proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF,
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP:
                    proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF}

    def _load_auto_cycle(self, cycle_doc):
        """读 auto.cycle 子节（逐键覆盖；非法值回缺省，不静默接受乱值）。

        [2026-11-26 实例化] **instances 键存在且规整后 ≥2 个（或恰好 1 个
        组合——组内多模式照样构成循环）⇒ 显式配置**
        （置 instances_explicit 标志、直接用它——instances 是唯一事实源，
        文件里的旧键不再直接生效）；否则按**未显式**处理：记一行日志、
        走旧键合成链（手写 instances:[] 或 len<2 不会把能跑的老配置变成
        启动闸拒绝）。

        [2026-11-18 配置化 · 每阶段切换条件]（未显式路径）解析顺序：
          ① 旧键（enabled / press_* / timer_* / *_minutes）——宽松取；
          ② cycle_modes（有序列表）——宽松取；
          ③ stage_cfg（{模式 id: {压差/定时}}）——宽松取（camelCase 与
             snake_case 都认）；
          ④ **合成**最终值：旧键按"角色"合成（首元素＝压差+可选兜底定时；
             反洗/顺洗＝定时分钟数）⇒ 显式 stage_cfg 逐字段覆盖；
          ⑤ 把旧键**回写成派生视图**（首元素的值 / 反洗顺洗的分钟数）⇒
             内存里只有一份真值（instances）。

        [为什么旧键是派生视图] 旧键要继续写盘（老读方不受影响）⇒ 它们
        必须是 instances 的投影，而不是第二份可以各改各的真值。
        """
        c = self.auto_cycle
        # ---- ⓪-0 顶层循环次数（2026-11-27）----
        # [必须在 instances 显式分支**之前**] 那条路径会提前 return ⇒ 放在
        # 后面的话「显式实例 + top_repeat」读盘时被静默忽略（运行中改完配置
        # 重启服务端又回到一直循环）。整数、越界钳位到 0..MAX（与 minutes
        # 同口径）；非法值回缺省（手改配置写坏不该让服务起不来）。
        if "top_repeat" in cycle_doc:
            c["top_repeat"] = max(
                0, min(proto.AUTO_CYCLE_TOP_REPEAT_MAX,
                       _int(cycle_doc.get("top_repeat"),
                            c["top_repeat"])))
        # ---- ⓪ 实例列表（2026-11-26 实例化）----
        # [显式判定] 键存在 + 是数组 + 规整后 ≥2（**或恰好 1 个组合**）⇒
        # 显式。其余一切形态（键不存在 / 规整后 <2 / 类型非法）都按未显式
        # 处理 ⇒ 回退旧键合成链（不拒、记日志）。
        # [单个组合也算显式 2026-11-27 复核] SET/启动闸早就允许「顶层只有
        # 1 个组合」（组内多模式照样构成循环）⇒ 读盘若仍要求 ≥2，单个组合
        # 及其 top_repeat 会在重启后被当成非法 instances 丢弃、回退到空的
        # 旧键（实测 SET 受理、save 后 instances=1、reload 后 explicit=False
        # /instances=0/cycle_modes=[]）——配置静默丢失。
        if proto.CFG_AUTO_CYCLE_INSTANCES in cycle_doc:
            raw_inst = cycle_doc.get(proto.CFG_AUTO_CYCLE_INSTANCES)
            insts = proto.normalize_cycle_instances(raw_inst, default=None)
            single_group = (len(insts) == 1 and str(
                insts[0].get("mode") or "") == proto.CYCLE_INST_GROUP)
            if isinstance(raw_inst, (list, tuple)) and (
                    len(insts) >= 2 or single_group):
                c["instances"] = insts
                c["instances_explicit"] = True
                c["enabled"] = _bool(cycle_doc.get("enabled"), c["enabled"])
                self._sync_auto_cycle_legacy()
                return
            self._log_auto_cycle(
                "配置文件里的 instances 未构成有效循环（规整后 %d 个实例，"
                "需 ≥2 或 1 个组合）⇒ 按旧键配置继续生效" % len(insts))
        if "enabled" in cycle_doc:
            c["enabled"] = _bool(cycle_doc.get("enabled"), c["enabled"])
        if "press_threshold" in cycle_doc:
            # [2026-11-15 审计] 原式子有两个坑：① `or` 把**合法的 0.0**
            # 变成缺省；② `float("abc")` 直接抛 ⇒ **手改配置写错一个字符
            # 就让服务起不来**（现场事故）。⇒ 走 _float（非法回缺省）。
            c["press_threshold"] = max(
                0.0, min(proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX,
                         _float(cycle_doc.get("press_threshold"),
                                c["press_threshold"])))
        cmp_key = str(cycle_doc.get("press_compare") or "").strip().lower()
        if cmp_key in proto.AUTO_PRESS_COMPARE_KEYS:
            c["press_compare"] = cmp_key
        if "timer_fallback" in cycle_doc:
            c["timer_fallback"] = _bool(cycle_doc.get("timer_fallback"),
                                        c["timer_fallback"])
        for key, attr in (("timer_minutes", "timer_minutes"),
                          ("backwash_minutes", "backwash_minutes"),
                          ("rinse_minutes", "rinse_minutes")):
            if key in cycle_doc:
                c[attr] = max(proto.AUTO_CYCLE_MINUTES_MIN,
                              min(proto.AUTO_CYCLE_MINUTES_MAX,
                                  _int(cycle_doc.get(key),
                                       c[attr])))
        # ---- 参与循环的模式列表（有序；2026-11-18 配置化）----
        # [宽松取] 手改配置写错**类型**（不是数组）⇒ 回缺省；数组里写了未知
        # 模式 ⇒ 丢弃该项 ⇒ **不能让手改一个字符就起不来**（同本文件其它键
        # 的口径）。数组本身合法但一个模式都不剩 ⇒ **保持空**（＝未配置
        # 参与循环的模式）⇒ 由启动闸明确报错（不悄悄回缺省跑另一套工艺）。
        # [保序] 列表顺序＝循环顺序 ⇒ 不能去重成 set（proto 助手保序）。
        c["cycle_modes"] = proto.normalize_cycle_modes(
            cycle_doc.get("cycle_modes"), default=proto.AUTO_CYCLE_MODES_DEF)
        # ---- 各阶段的切换条件（stage_cfg）----
        # [显式优先] stage_cfg 里的项逐字段覆盖合成缺省；表里没写的阶段
        # 用合成缺省（老配置 ⇒ 完全按旧语义跑）。
        explicit = proto.normalize_cycle_stage_cfg(
            cycle_doc.get("stage_cfg"), default={})
        # [兼容视图 stage_minutes] 2026-11-18 早一轮的"每阶段分钟数"表：
        # 折进 stage_cfg 的 timer_minutes（**只改分钟数**，不改"定不启用
        # 定时"那个开关 ⇒ 不覆盖用户在 stageCfg 里的显式选择）。
        legacy_minutes = proto.normalize_cycle_stage_minutes(
            cycle_doc.get("stage_minutes"), default={})
        for mid, mins in legacy_minutes.items():
            entry = dict(explicit.get(mid) or {})
            entry.setdefault("timer_minutes", int(mins))
            explicit[mid] = entry
        c["stage_cfg"] = proto.build_cycle_stage_cfg(
            c["cycle_modes"], legacy=self._auto_cycle_legacy_values(c),
            explicit=explicit)
        self._sync_auto_cycle_legacy()

    def _log_auto_cycle(self, text):
        """cycle 迁移日志（Config 层没有 logger ⇒ 收口一个静默安全口）。

        [为什么收口] load 期不宜依赖 app 的 logger；有 app 挂钩时转交，
        否则吞掉（迁移日志不是致命信息）。
        """
        hook = getattr(self, "log_hook", None)
        if callable(hook):
            try:
                hook("自动循环：" + text)
            except Exception:                           # noqa: BLE001
                pass

    @staticmethod
    def _auto_cycle_legacy_values(c):
        """旧键的当前值（合成 stage_cfg 时用）。"""
        return {
            "press_threshold": c.get("press_threshold"),
            "press_compare": c.get("press_compare"),
            "timer_fallback": c.get("timer_fallback"),
            "timer_minutes": c.get("timer_minutes"),
            "backwash_minutes": c.get("backwash_minutes"),
            "rinse_minutes": c.get("rinse_minutes"),
        }

    def sync_auto_cycle(self):
        """对外别名：刷新 auto_cycle 的实例/派生视图一致性（app.py 调）。"""
        self._sync_auto_cycle_legacy()

    def _auto_cycle_file_doc(self):
        """auto.cycle 节的**落盘形状**（save 用；键位全保留）。

        [实例 = 唯一事实源] **仅在显式标志置位时**写 instances（snake_case
        内部键形）。未显式 ⇒ 不写 instances、旧派生键照旧写 ⇒ 老读方不受
        影响；手改文件里的旧键持续有效（显式落盘后旧九键才降级为只读
        派生视图——此后手改文件里的旧键不再直接生效，只经「写穿」路径
        映射到实例）。
        """
        doc = {
            "enabled": bool(self.auto_cycle["enabled"]),
            # [旧键 = instances 的派生视图 2026-11-26] 压差/兜底定时写
            # **首实例**的值，反洗/顺洗分钟写对应模式的值 ⇒ 老读方不受
            # 影响，且与新配置**永远一致**。
            "press_threshold": float(self.auto_cycle["press_threshold"]),
            "press_compare": str(self.auto_cycle["press_compare"]),
            "timer_fallback": bool(self.auto_cycle["timer_fallback"]),
            "timer_minutes": int(self.auto_cycle["timer_minutes"]),
            "backwash_minutes": int(self.auto_cycle["backwash_minutes"]),
            "rinse_minutes": int(self.auto_cycle["rinse_minutes"]),
            # [顶层循环次数 2026-11-27] 0=一直循环 / N=跑完 N 轮自动停止
            # （落盘键名 snake_case，与 cycle 其它键同风格）。
            "top_repeat": max(0, min(proto.AUTO_CYCLE_TOP_REPEAT_MAX,
                                     int(self.auto_cycle.get(
                                         "top_repeat",
                                         proto.AUTO_CYCLE_TOP_REPEAT_DEF)
                                         or 0))),
            # [2026-11-18 配置化] 参与循环的模式列表（**有序**，由实例去重
            # 保序派生）与**各阶段的切换条件**（兼容视图：同 mode 首实例 +
            # 缺席模式回合成缺省）。
            "cycle_modes": list(self.auto_cycle["cycle_modes"]),
            "stage_cfg": {
                str(mid): {fk: ev[fk] for _pk, fk, _k in
                           proto.AUTO_CYCLE_STAGE_FIELDS if fk in ev}
                for mid, ev in
                (self.auto_cycle.get("stage_cfg") or {}).items()
                if isinstance(ev, dict)},
            # [兼容视图] 每阶段分钟数（由 stage_cfg 派生）⇒ 已经上线的旧
            # 客户端界面照常显示时长。
            "stage_minutes": proto.cycle_stage_minutes_view(
                self.auto_cycle.get("stage_cfg") or {},
                self.auto_cycle.get("cycle_modes")),
        }
        if self.auto_cycle.get("instances_explicit"):
            doc["instances"] = [
                {fk: inst[fk] for _pk, fk, _k in
                 proto.AUTO_CYCLE_INSTANCE_FIELDS if fk in inst}
                for inst in (self.auto_cycle.get("instances") or [])
                if isinstance(inst, dict)]
        return doc

    def _sync_auto_cycle_legacy(self):
        """刷新 auto_cycle 的**单一事实源与派生视图**（2026-11-26 实例化）。

        [事实源 = instances] ① 未显式配置 instances 时按**当前旧键视图**
        （cycle_modes + stage_cfg + auto.modes 的设备/转速）重新合成实例
        ——旧键仍是可写的输入（老客户端 SET 旧键继续有效）；② 之后把
        旧键全部**回写成 instances 的派生视图**（cycle_modes=去重保序、
        stage_cfg/stage_minutes=同 mode 首实例 + 缺席模式回合成缺省、
        六扁平键=首实例）⇒ 老读方看到的永远与实例一致，不存在两份真值
        打架（"界面显示 10 分钟、实际切相按 5 分钟"那类坑）。

        [幂等] 合成 → 派生是**不动点**：派生回的视图与输入一致（缺席模式
        的合成缺省恰好等于 build_cycle_stage_cfg 给它们的缺省）⇒ 反复
        调用值不变。
        """
        c = self.auto_cycle
        if not c.get("instances_explicit"):
            # [未显式 ⇒ instances 是旧键的合成结果] 每次同步都重算：旧键
            # （stage_cfg/扁平键）先被改（load/SET 路径），再由这里折进
            # 实例。auto.modes 提供设备/转速（此处 self.auto 已是加载/
            # SET 后的最新值）。
            c["instances"] = proto.synthesize_cycle_instances(
                {"cycle_modes": c.get("cycle_modes"),
                 "stage_cfg": c.get("stage_cfg"),
                 "press_threshold": c.get("press_threshold"),
                 "press_compare": c.get("press_compare"),
                 "timer_fallback": c.get("timer_fallback"),
                 "timer_minutes": c.get("timer_minutes"),
                 "backwash_minutes": c.get("backwash_minutes"),
                 "rinse_minutes": c.get("rinse_minutes"),
                 "stage_minutes": None},
                modes_cfg=getattr(self, "auto", None) or {})
        view = proto.cycle_instances_compat_view(c.get("instances") or [])
        c["cycle_modes"] = view["cycle_modes"]
        c["stage_cfg"] = view["stage_cfg"]
        c["stage_minutes"] = view["stage_minutes"]
        c["press_threshold"] = view["press_threshold"]
        c["press_compare"] = view["press_compare"]
        c["timer_fallback"] = view["timer_fallback"]
        c["timer_minutes"] = view["timer_minutes"]
        c["backwash_minutes"] = view["backwash_minutes"]
        c["rinse_minutes"] = view["rinse_minutes"]

    # ---- 派生（只读转发，避免两处各存一份导致改一个忘一个）----
    @property
    def analog_poll_ms(self):
        return self.analog.poll_ms

    @property
    def temphum_poll_ms(self):
        return self.serial.temphum_poll_ms

    @property
    def vfd_poll_ms(self):
        return self.serial.vfd_poll_ms

    def turbidity_enabled(self, idx):
        """第 idx 台（0 基）浊度仪的采集开关。

        [逐台优先] 两台是独立设备，可能只装一台/只停一台；`enable_turbidity`
        只是"共同缺省"（旧配置只有 turbidityEnabled 时两台都照它）。
        """
        return bool(getattr(self, "enable_turbidity%d" % (idx + 1),
                            self.enable_turbidity))

    # ---- 路径解析 ----
    def resolve_paths(self, base_dir):
        self.base_dir = os.path.abspath(base_dir)
        if not self.config_path:
            self.config_path = os.path.join(self.base_dir, DEFAULT_CONFIG_REL)
        if not self.users_path:
            self.users_path = os.path.join(self.base_dir, DEFAULT_USERS_REL)

    def ensure_cfg_dir(self):
        """确保 cfg/ 目录存在（不存在则建，权限失败由调用方决定是否致命）。"""
        directory = os.path.dirname(self.config_path)
        if directory and not os.path.isdir(directory):
            try:
                os.makedirs(directory)
            except OSError:
                pass

    # ==================================================================
    #                            读写配置
    # ==================================================================
    #: 新鲜度阈值下限相对采集周期的倍数（允许连丢 2 拍；AGENTS 口径
    #: `stale = max(设备基线, 周期 × 3)`）。
    STALE_FROM_POLL_MULT = 3

    def apply_stale_floor(self):
        """把各设备新鲜度阈值抬到**至少「采集周期 × 3」**（就地生效）。

        [为什么必须钳] 板端 2026-11-17 实锅：温湿度 `temphum_poll_ms=5000`
        而 `stale.temphum_ms=5000` —— 阈值正好等于一个采集周期 ⇒ 每拍之间
        数据年龄都会越过阈值，界面/健康在「数据过期 ↔ 正常」之间**周期性
        闪烁**（用户报「温湿度一直在周期报数据过期 正常 在过期」）。
        阈值口径必须跟**真实刷新节拍**走，不能写死 5s。
        周期为 0（未启用/未配置）的组不钳。
        """
        mult = max(1, int(self.STALE_FROM_POLL_MULT))
        serial = getattr(self, "serial", None)

        def _period(value):
            try:
                return max(0, int(value or 0))
            except (TypeError, ValueError):
                return 0

        def _floor(cur, period):
            cur = int(cur or 0)
            return max(cur, period * mult) if period > 0 else cur

        self.vfd_stale_ms = _floor(
            self.vfd_stale_ms,
            _period(getattr(serial, "vfd_poll_ms", 0)))
        self.temphum_stale_ms = _floor(
            self.temphum_stale_ms,
            _period(getattr(serial, "temphum_poll_ms", 0)))
        self.flowmeter_stale_ms = _floor(
            self.flowmeter_stale_ms, _period(self.flowmeter_poll_ms))
        self.turbidity_stale_ms = _floor(
            self.turbidity_stale_ms,
            max(_period(self.turbidity1_poll_ms),
                _period(self.turbidity2_poll_ms)))
        self.digital_stale_ms = _floor(
            self.digital_stale_ms, _period(self.digital_poll_ms))

    def load(self):
        """读 JSON 配置；文件不存在视为全缺省（返回 True，不报错）。

        未知键**忽略**（前向兼容：新版本写的配置给旧版本跑不应崩）；
        类型不符的键回退缺省值并记录警告（不静默采信错误类型）。
        """
        if not self.config_path or not os.path.isfile(self.config_path):
            return True
        try:
            with open(self.config_path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError):
            return False
        if not isinstance(doc, dict):
            return False

        server = _section(doc, "server")
        self.port = _int(server.get("port"), self.port)
        if "serial_enabled" in server:
            self.enable_serial = bool(server.get("serial_enabled"))
        self._runtime = _section(doc, "runtime")

        serial = _section(doc, "serial")
        data_ids = serial.get("unit_ids") or {}
        self.vfd_unit_id = _int(data_ids.get("vfd"), self.vfd_unit_id)
        self.temphum_unit_id = _int(data_ids.get("temphum"), self.temphum_unit_id)
        self.flowmeter_unit_id = _int(data_ids.get("flowmeter"),
                                      self.flowmeter_unit_id)
        # 浊度仪站号是**两个**，配置里给数组；缺省=2026-11-09 ttyS4 实测
        # 口径（站1 WZT-701A / 站2 ZHEYI-5100ZDY，站3 已实测不存在）。
        turb_ids = data_ids.get("turbidity")
        if isinstance(turb_ids, list) and len(turb_ids) >= 2:
            self.turbidity_unit_ids = [_int(turb_ids[0],
                                            self.turbidity_unit_ids[0]),
                                       _int(turb_ids[1],
                                            self.turbidity_unit_ids[1])]
        # 每台的字节序（按下标与站号对应）。只认四个合法值；非法条目丢弃，
        # 剩下按前两台取（缺台回退装配处的类缺省 CDAB）。
        turb_orders = data_ids.get("turbidity_byte_orders")
        if isinstance(turb_orders, list) and turb_orders:
            cleaned = [str(o).strip().upper() for o in turb_orders]
            cleaned = [o for o in cleaned if o in VALID_TURBIDITY_BYTE_ORDERS]
            if cleaned:
                self.turbidity_byte_orders = cleaned
        # 每台一次读的寄存器数（站2=4：浊度+温度一帧同刻）。下限 2——
        # 32 位量至少两个字，配小了只会整拍读错。
        turb_counts = data_ids.get("turbidity_read_counts")
        if isinstance(turb_counts, list) and turb_counts:
            cleaned = [max(2, _int(c, 2)) for c in turb_counts]
            if cleaned:
                self.turbidity_read_counts = cleaned
        self.serial = SerialConfig(
            port=serial.get("port", self.serial.port),
            baudrate=_int(serial.get("baudrate"), self.serial.baudrate),
            timeout_ms=_int(serial.get("timeout_ms"), self.serial.timeout_ms),
            retries=_int(serial.get("retries"), self.serial.retries),
            poll_gap_ms=_int(serial.get("poll_gap_ms"),
                             self.serial.poll_gap_ms),
            vfd_poll_ms=_int(serial.get("vfd_poll_ms"), self.serial.vfd_poll_ms),
            # [铭牌参数 2026-11-16 审计] app 消费 rated_rpm/rated_hz/max_hz
            # （转速换算的基准点），原来 load 不带这三个 kwarg ⇒ 手改配置
            # **静默回落 1440/50/50**（换电机后转速显示全错且无任何线索）。
            vfd_rated_rpm=_float(serial.get("vfd_rated_rpm"),
                                 self.serial.vfd_rated_rpm),
            vfd_rated_hz=_float(serial.get("vfd_rated_hz"),
                                self.serial.vfd_rated_hz),
            vfd_max_hz=_float(serial.get("vfd_max_hz"),
                              self.serial.vfd_max_hz),
            temphum_poll_ms=_int(serial.get("temphum_poll_ms"),
                                 self.serial.temphum_poll_ms),
            temphum_push_ms=_int(serial.get("temphum_push_ms"),
                                 self.serial.temphum_push_ms),
        )
        # ---- 第二条 485（流量计 + 浊度仪 ×2，9600）----
        # 老配置文件没有 serial_b 节：沿用 __init__ 推出来的缺省口与 9600
        # （含 200ms 事务间隙——站1 在站5 长帧后的恢复期，见 SerialConfig）。
        # 若路 A 的口名后来改了、而 serial_b 没配，就重新推一次，避免
        # 两条线悄悄落成同一个口（那样 9600 设备会被按 115200 访问）。
        sb = _section(doc, "serial_b")
        default_b_port = _next_serial_port(self.serial.port)
        b_port = sb.get("port") or default_b_port
        if b_port == self.serial.port:
            b_port = default_b_port          # 撞车兜底
        self.serial_b = SerialConfig(
            port=b_port,
            baudrate=_int(sb.get("baudrate"), 9600),
            timeout_ms=_int(sb.get("timeout_ms"),
                            self.serial.timeout_ms),
            retries=_int(sb.get("retries"), self.serial.retries),
            poll_gap_ms=_int(sb.get("poll_gap_ms"),
                            self.serial_b.poll_gap_ms),
            vfd_poll_ms=self.serial.vfd_poll_ms,
            temphum_poll_ms=self.serial.temphum_poll_ms,
            temphum_push_ms=self.serial.temphum_push_ms,
        )
        if "vfd_enabled" in serial:
            self.enable_vfd = bool(serial.get("vfd_enabled"))
        if "temphum_enabled" in serial:
            self.enable_temphum = bool(serial.get("temphum_enabled"))
        if "flowmeter_enabled" in serial:
            self.enable_flowmeter = bool(serial.get("flowmeter_enabled"))
        if "turbidity_enabled" in serial:
            self.enable_turbidity = bool(serial.get("turbidity_enabled"))
            # [逐台缺键回落共同缺省] 先按老键铺开，再由逐台键覆盖 —— 旧
            # 配置文件只有 turbidity_enabled 时两台照它跑（向后兼容）。
            self.enable_turbidity1 = self.enable_turbidity
            self.enable_turbidity2 = self.enable_turbidity
        for _idx, _key in enumerate(("turbidity1_enabled",
                                     "turbidity2_enabled")):
            if _key in serial:
                setattr(self, "enable_turbidity%d" % (_idx + 1),
                        bool(serial.get(_key)))
        #: 浊度轮询周期（远程采集配置可改；原先只在 __init__ 里赋值、无
        #: load/save 通路 —— 远程改完重启就丢，配置文件里根本不存在）。
        self.turbidity_poll_ms = _int(serial.get("turbidity_poll_ms"),
                                      self.turbidity_poll_ms)
        # [逐台周期 2026-11-16 审计] 远程采集配置可改（CFGK_*_POLL_MS）、
        # _poll_loop 每拍生效，但 load/save 原来都没有这几个键 ⇒ **远程改完
        # 重启即丢**、手改配置文件也不生效。缺键回落共同缺省（向后兼容）。
        self.turbidity1_poll_ms = _int(serial.get("turbidity1_poll_ms"),
                                       self.turbidity_poll_ms)
        self.turbidity2_poll_ms = _int(serial.get("turbidity2_poll_ms"),
                                       self.turbidity_poll_ms)
        self.flowmeter_poll_ms = _int(serial.get("flowmeter_poll_ms"),
                                      self.flowmeter_poll_ms)
        # [滤速 2026-11-25] 过滤面积（m²；0=未设置）。
        if "filter_area_m2" in serial:
            self.filter_area_m2 = max(
                proto.FILTER_AREA_M2_MIN,
                min(proto.FILTER_AREA_M2_MAX,
                    _float(serial.get("filter_area_m2"),
                           self.filter_area_m2)))

        # ---- 显示渐变范围（2026-11-24）----
        # 顶层 `display` 节：只影响客户端数值着色，不参与采集/控制/保护。
        # 下限 >= 上限 = 非法（配置手写错）⇒ 整对回落缺省，不让渐变把
        # 所有值染成同一个端点色却毫无提示。
        display = _section(doc, "display")
        self.turbidity1_range_min = _float(
            display.get("turbidity1_range_min"),
            proto.TURBIDITY_RANGE_MIN_DEF)
        self.turbidity1_range_max = _float(
            display.get("turbidity1_range_max"),
            proto.TURBIDITY_RANGE_MAX_DEF)
        self.turbidity2_range_min = _float(
            display.get("turbidity2_range_min"),
            proto.TURBIDITY_RANGE_MIN_DEF)
        self.turbidity2_range_max = _float(
            display.get("turbidity2_range_max"),
            proto.TURBIDITY_RANGE_MAX_DEF)
        self.temphum_temp_low_c = _float(
            display.get("temphum_temp_low_c"),
            proto.TEMPHUM_TEMP_LOW_C_DEF)
        self.temphum_temp_high_c = _float(
            display.get("temphum_temp_high_c"),
            proto.TEMPHUM_TEMP_HIGH_C_DEF)
        self.vfd_speed_range_min = _float(
            display.get("vfd_speed_range_min"),
            proto.VFD_SPEED_RANGE_MIN_DEF)
        self.vfd_speed_range_max = _float(
            display.get("vfd_speed_range_max"),
            proto.VFD_SPEED_RANGE_MAX_DEF)
        self.vfd_motor_temp_range_min = _float(
            display.get("vfd_motor_temp_range_min"),
            proto.VFD_MOTOR_TEMP_RANGE_MIN_DEF)
        self.vfd_motor_temp_range_max = _float(
            display.get("vfd_motor_temp_range_max"),
            proto.VFD_MOTOR_TEMP_RANGE_MAX_DEF)
        # [显示渐变逐项开关 2026-11-24] 缺键=开（老配置/老客户端不传）。
        for _key, _attr in (
                ("turbidity1_gradient_enabled",
                 "turbidity1_gradient_enabled"),
                ("turbidity2_gradient_enabled",
                 "turbidity2_gradient_enabled"),
                ("temphum_temp_gradient_enabled",
                 "temphum_temp_gradient_enabled"),
                ("vfd_speed_gradient_enabled",
                 "vfd_speed_gradient_enabled"),
                ("vfd_motor_temp_gradient_enabled",
                 "vfd_motor_temp_gradient_enabled"),
                ("press_gradient_enabled", "press_gradient_enabled"),
                ("press1_gradient_enabled", "press1_gradient_enabled"),
                ("press2_gradient_enabled", "press2_gradient_enabled")):
            if _key in display:
                setattr(self, _attr, _bool(display.get(_key), True))
        for _i in (1, 2):
            if (getattr(self, "turbidity%d_range_min" % _i)
                    >= getattr(self, "turbidity%d_range_max" % _i)):
                setattr(self, "turbidity%d_range_min" % _i,
                        proto.TURBIDITY_RANGE_MIN_DEF)
                setattr(self, "turbidity%d_range_max" % _i,
                        proto.TURBIDITY_RANGE_MAX_DEF)
        if self.temphum_temp_low_c >= self.temphum_temp_high_c:
            self.temphum_temp_low_c = proto.TEMPHUM_TEMP_LOW_C_DEF
            self.temphum_temp_high_c = proto.TEMPHUM_TEMP_HIGH_C_DEF
        if self.vfd_speed_range_min >= self.vfd_speed_range_max:
            self.vfd_speed_range_min = proto.VFD_SPEED_RANGE_MIN_DEF
            self.vfd_speed_range_max = proto.VFD_SPEED_RANGE_MAX_DEF
        if (self.vfd_motor_temp_range_min
                >= self.vfd_motor_temp_range_max):
            self.vfd_motor_temp_range_min = \
                proto.VFD_MOTOR_TEMP_RANGE_MIN_DEF
            self.vfd_motor_temp_range_max = \
                proto.VFD_MOTOR_TEMP_RANGE_MAX_DEF

        analog = _section(doc, "analog")
        if analog:
            self.analog = AnalogConfig(
                host=analog.get("host", self.analog.host),
                port=_int(analog.get("port"), self.analog.port),
                unit_id=_int(analog.get("unit_id"), self.analog.unit_id),
                channels=_int(analog.get("channels"), self.analog.channels),
                poll_ms=_int(analog.get("poll_ms"), self.analog.poll_ms),
            )
            # [压差计/温度传感器通道与量程可配 2026-11-20] 缺键 = 用缺省
            # （压差计 CH0 0~1MPa / 温度传感器 CH1 0~100℃）。cfg 侧
            # snake_case（协议 camelCase 在 app._h_config_set）。越界夹回。
            # [2026-11-20 二改] 量程=下限~上限（4mA→下限、20mA→上限），
            # 「温度满量程」键废弃（temp_full_scale 不再读写）。
            self.analog.press_channel = max(
                0, min(self.analog.channels - 1,
                       _int(analog.get("press_channel"),
                            self.analog.press_channel)))
            self.analog.temp_channel = max(
                0, min(self.analog.channels - 1,
                       _int(analog.get("temp_channel"),
                            self.analog.temp_channel)))
            self.analog.press_low_mpa = _float(analog.get("press_low_mpa"),
                                               self.analog.press_low_mpa)
            self.analog.press_high_mpa = max(
                0.001, _float(analog.get("press_high_mpa"),
                              self.analog.press_high_mpa))
            self.analog.press_below4_valid = _bool(
                analog.get("press_below4_valid"),
                self.analog.press_below4_valid)
            # [两台压力传感器 2026-11-23] 缺键=CH2/CH3、0~1MPa、below4 关；
            # 通道号越界夹回（配置文件手改出错不让服务起不来）。
            self.analog.press1_channel = max(
                0, min(self.analog.channels - 1,
                       _int(analog.get("press1_channel"),
                            self.analog.press1_channel)))
            self.analog.press1_low_mpa = _float(
                analog.get("press1_low_mpa"), self.analog.press1_low_mpa)
            self.analog.press1_high_mpa = max(
                0.001, _float(analog.get("press1_high_mpa"),
                              self.analog.press1_high_mpa))
            self.analog.press1_below4_valid = _bool(
                analog.get("press1_below4_valid"),
                self.analog.press1_below4_valid)
            self.analog.press2_channel = max(
                0, min(self.analog.channels - 1,
                       _int(analog.get("press2_channel"),
                            self.analog.press2_channel)))
            self.analog.press2_low_mpa = _float(
                analog.get("press2_low_mpa"), self.analog.press2_low_mpa)
            self.analog.press2_high_mpa = max(
                0.001, _float(analog.get("press2_high_mpa"),
                              self.analog.press2_high_mpa))
            self.analog.press2_below4_valid = _bool(
                analog.get("press2_below4_valid"),
                self.analog.press2_below4_valid)
            self.analog.temp_low_c = _float(analog.get("temp_low_c"),
                                            self.analog.temp_low_c)
            self.analog.temp_high_c = _float(analog.get("temp_high_c"),
                                             self.analog.temp_high_c)
            self.analog.temp_below4_valid = _bool(
                analog.get("temp_below4_valid"),
                self.analog.temp_below4_valid)
            # [通道号改了必须重建换算表] __init__ 里按**缺省**通道号建过
            # scales；配置文件指定了别的通道号 ⇒ 旧表还挂在缺省通道上
            # （温度换算读不到、模拟量页显示错通道）。先按新通道重建，
            # 再让下面文件里的 scales 覆盖（文件条目优先）。
            self.analog.rebuild_scales()
            # 通道工程量换算表（4~20mA → 物理量）。
            # 配置形如 {"index":0,"name":"压差计","unit":"MPa",
            #         "fullScale":1.0,"lowMa":4,"highMa":20}
            for item in (analog.get("scales") or []):
                if not isinstance(item, dict):
                    continue
                try:
                    idx = int(item.get("index", 0))
                except (TypeError, ValueError):
                    continue
                if 0 <= idx < self.analog.channels:
                    # [不能用 AnalogChannelScale(idx, **item)] item 里本身
                    # 就带 "index" 键，位置参数会与关键字参数撞车
                    # （TypeError: got multiple values for argument）。
                    scale = AnalogChannelScale(**item)
                    scale.index = idx          # 以解析出的有效下标为准
                    # [语义通道名归代码所有 2026-11-26 用户实报「主界面
                    # 进水/出水压力没数值」] 压差计/温度传感器/进水压力/
                    # 出水压力这四个名字是**查表锚点**（服务端
                    # _history_analog_by_name 与三端客户端按名找通道都用
                    # 它）——改名只能改 proto 常量。老配置文件里存的旧名
                    # （如「压力1」）若让文件盖过常量，改名后遥测名字与
                    # 客户端匹配不上 ⇒ 两路压力永远 "--"。这里强制纠正，
                    # 文件里的旧名下次 save() 自动写回新值；其余通道
                    # （将来手加的）名字仍以文件为准。
                    # [优先级与 rebuild_scales 对齐 2026-11-27 工作流] rebuild
                    # 的写入顺序是 压差→压力1→压力2→温度（后者覆盖前者，温度
                    # 赢）；原来的 if/elif 顺序是 压差→温度→压力1→压力2（压差
                    # 赢）⇒ 通道撞号时文件里的 scale 会被改成错误名字。这里
                    # 按同一优先级从高到低判断：温度 > 压力2 > 压力1 > 压差。
                    if idx == self.analog.temp_channel:
                        scale.name = "温度传感器"
                    elif idx == self.analog.press2_channel:
                        scale.name = proto.PRESS2_LABEL
                    elif idx == self.analog.press1_channel:
                        scale.name = proto.PRESS1_LABEL
                    elif idx == self.analog.press_channel:
                        scale.name = proto.PRESSURE_LABEL
                    self.analog.scales[idx] = scale
        if "enabled" in analog:
            self.enable_analog = bool(analog.get("enabled"))

        digital = _section(doc, "digital")
        if "enabled" in digital:
            self.enable_digital = bool(digital.get("enabled"))
        self.digital_poll_ms = _int(digital.get("poll_ms"), self.digital_poll_ms)
        self.digital_stale_ms = _int(digital.get("stale_ms"),
                                     self.digital_stale_ms)
        modules = digital.get("modules")
        if isinstance(modules, list) and modules:
            built = []
            for i, item in enumerate(modules):
                if not isinstance(item, dict):
                    continue
                # [缺名字的兜底 2026-11-17 用户定案] 现场只有**一台**数字量
                # 模块 ⇒ 直接叫「数字量」；多台才编号（数字量1/数字量2…）。
                # 原来一律 "数字量%d" ⇒ 单台上显示"数字量1/数字量2"，像有
                # 第二台不存在的东西（用户口径：「就一个数字量模块直接叫
                # 数字量吧」）。
                name = item.get("name") or (
                    "数字量" if len(modules) == 1 else "数字量%d" % (i + 1))
                built.append(DigitalConfig(
                    name,
                    host=item.get("host", "192.168.0.13"),
                    port=_int(item.get("port"), 502),
                    unit_id=_int(item.get("unit_id"), 1),
                    timeout_s=_float(item.get("timeout_s"), 2.0),
                    channels=_int(item.get("channels"), 16),
                    publish=item.get("publish", True)))
            if built:
                self.digital_modules = built

        valves = _section(doc, "valves")
        if "enabled" in valves:
            self.enable_valves = bool(valves.get("enabled"))
        self.valve_poll_ms = _int(valves.get("poll_ms"), self.valve_poll_ms)
        self.valve_move_timeout_ms = _int(valves.get("move_timeout_ms"),
                                          self.valve_move_timeout_ms)
        items = valves.get("items")
        if isinstance(items, list) and items:
            built = []
            for i, item in enumerate(items[:6]):
                if not isinstance(item, dict):
                    item = {}
                built.append({
                    "name": item.get("name") or VALVE_DEFAULT_NAMES[i],
                    "module": _opt_int(item.get("module")),
                    "do": _opt_int(item.get("do")),
                    "openDi": _opt_int(item.get("openDi")),
                    "closeDi": _opt_int(item.get("closeDi")),
                })
            while len(built) < 6:
                built.append({"name": VALVE_DEFAULT_NAMES[len(built)],
                              "module": None, "do": None,
                              "openDi": None, "closeDi": None})
            self.valve_items = built

        # ---- 进水泵（数字量模块上的一路 DO）----
        pumps = _section(doc, "pumps")
        if pumps:
            if "enabled" in pumps:
                self.enable_pump = bool(pumps.get("enabled"))
            pitems = pumps.get("items")
            if isinstance(pitems, list) and pitems:
                pbuilt = []
                for i, item in enumerate(pitems[:4]):
                    if not isinstance(item, dict):
                        item = {}
                    pbuilt.append({
                        "name": item.get("name") or ("进水泵%d" % (i + 1)),
                        "module": _opt_int(item.get("module")),
                        "do": _opt_int(item.get("do")),
                    })
                self.pump_items = pbuilt

        # ---- 按钮组（现场操作柱：4 DI 输入 + 3 DO 灯）----
        buttons = _section(doc, "buttons")
        if buttons:
            if "enabled" in buttons:
                self.enable_buttons = bool(buttons.get("enabled"))
            self.button_debounce_ms = _int(buttons.get("debounce_ms"),
                                           self.button_debounce_ms)
            bitems = buttons.get("items")
            if isinstance(bitems, dict):
                merged = dict(self.button_items)
                for key in ("estop_di", "start_di", "stop_di", "reset_di",
                            "start_lamp_do", "stop_lamp_do", "reset_lamp_do",
                            "module"):
                    if key in bitems:
                        merged[key] = _opt_int(bitems.get(key))
                # 常闭触点（如 ["stop_di"]）。由**现场接线**决定，不是软件
                # 常量：用户口径「停止是常闭的，不按就是通的」「跟急停一样」。
                # 早先硬编码三路常开 ⇒ 停止按钮按下是 1→0，只认上升沿的
                # 代码永远收不到，按停止毫无反应。
                nc = bitems.get("normally_closed")
                if isinstance(nc, (list, tuple)):
                    valid = ("estop_di", "start_di", "stop_di", "reset_di")
                    merged["normally_closed"] = [
                        str(k) for k in nc if str(k) in valid]
                self.button_items = merged

        # ---- 模拟器保真旋钮（--sim；ttyS4 2026-11-09 实测口径）----
        # 老配置文件没有 sim 节：沿用 __init__ 的实测缺省。需要压联调时长
        # 时在这里把任一档清零（时延单位毫秒；[min,max] 区间随机取值）。
        sim_sec = _section(doc, "sim")
        if sim_sec:
            def _one_pair(item):
                """一个 [lo,hi]（或单整数）→ (lo, hi)，lo>=0 且 lo<=hi；非法返回 None。"""
                if isinstance(item, (list, tuple)) and len(item) == 2:
                    lo = max(0, _int(item[0], 0))
                    hi = max(lo, _int(item[1], lo))
                    return (lo, hi)
                if isinstance(item, int) and not isinstance(item, bool):
                    v = max(0, item)
                    return (v, v)
                return None
            raw = sim_sec.get("turbidity_reply_ms")
            if isinstance(raw, list) and raw:
                pairs = [_one_pair(item) for item in raw[:2]]
                if all(p is not None for p in pairs) and len(pairs) == 2:
                    self.sim_turbidity_reply_ms = pairs
            fpair = _one_pair(sim_sec.get("flowmeter_reply_ms"))
            if fpair is not None:
                self.sim_flowmeter_reply_ms = fpair
            self.sim_recovery_ms = max(0, _int(sim_sec.get("recovery_ms"),
                                               self.sim_recovery_ms))
            for key, attr in (("turbidity_wobble_ntu", "turbidity_sim_wobble"),
                              ("temp_wobble_c", "turbidity_sim_temp_wobble")):
                raw = sim_sec.get(key)
                if isinstance(raw, list) and raw:
                    vals = []
                    for item in raw[:2]:
                        try:
                            vals.append(max(0.0, float(item)))
                        except (TypeError, ValueError):
                            continue
                    if vals:
                        setattr(self, attr, vals)
            # [2026-11-19 补配置入口] flowmeter_sim_empty/flow 此前**只有
            # 属性没有 load 通道**——属性头注写着「报警链路由配置
            # `flowmeter_sim_empty=true` 复现」，但 load/save 都不认这两个
            # 键 ⇒ 配置文件写了也不生效（口头承诺的旋钮不存在）。现从
            # sim 节读入（键名 flowmeter_empty / flowmeter_flow），save
            # 同步落盘（见 save 的 sim 节）。
            if "flowmeter_empty" in sim_sec:
                self.flowmeter_sim_empty = bool(sim_sec.get("flowmeter_empty"))
            if "flowmeter_flow" in sim_sec:
                try:
                    self.flowmeter_sim_flow = max(0.0,
                                                  float(sim_sec
                                                        .get("flowmeter_flow")))
                except (TypeError, ValueError):
                    pass

        stale = _section(doc, "stale")
        self.vfd_stale_ms = _int(stale.get("vfd_ms"), self.vfd_stale_ms)
        self.temphum_stale_ms = _int(stale.get("temphum_ms"),
                                     self.temphum_stale_ms)
        self.flowmeter_stale_ms = _int(stale.get("flowmeter_ms"),
                                       self.flowmeter_stale_ms)
        self.turbidity_stale_ms = _int(stale.get("turbidity_ms"),
                                        self.turbidity_stale_ms)
        self.digital_stale_ms = _int(stale.get("digital_ms"),
                                     self.digital_stale_ms)
        # [阈值跟采集节拍走 2026-11-17] `stale.temphum_ms=5000` 而
        # `temphum_poll_ms=5000` ⇒ 每拍之间必然越过阈值、界面周期闪
        # 「数据过期」。统一抬到 max(配置值, 周期×3)。
        self.apply_stale_floor()
        storage = _section(doc, "storage")
        if "enabled" in storage:
            self.storage.enabled = bool(storage.get("enabled"))
        # 空串合法（= 服务端程序目录），故只在键存在时覆盖。
        if "path" in storage:
            self.storage.path = str(storage.get("path") or "")
        self.storage.sample_ms = _int(storage.get("sample_ms"),
                                      self.storage.sample_ms)
        if "max_mb" in storage:
            self.storage.max_mb = _int(storage.get("max_mb"),
                                       self.storage.max_mb)
        # [max_kb 优先] 配置文件是管理员手工编辑的，故接受 KB 级细粒度
        # （极小盘/回归测试要把上限压到几百 KB 才能几十秒内跑到限）。
        # 远端协议的 maxMb 仍是整数 MB（界面不让人填出 KB 级的怪值）。
        if "max_kb" in storage:
            kb = _int(storage.get("max_kb"), -1)
            if kb >= 0:
                self.storage.set_max_kb(kb)
        policy = str(storage.get("full_policy") or "").strip().lower()
        if policy in ("stop", "ring", "rotate"):
            self.storage.full_policy = policy
        self.storage.keep_files = max(
            1, min(99, _int(storage.get("keep_files"),
                            self.storage.keep_files)))
        # [历史入库触发方式 2026-11-12] 未知值回缺省（不静默接受乱值）。
        trigger = str(storage.get("history_trigger") or "").strip().lower()
        if trigger in proto.HISTORY_TRIGGER_KEYS:
            self.storage.history_trigger = trigger
        # ---- 自动循环配置（2026-11-12；auto 节内的 cycle 子节）----
        auto_doc = _section(doc, proto.CFG_SECTION_AUTO)
        cycle_doc = _section(auto_doc, proto.CFG_AUTO_CYCLE)
        if cycle_doc:
            self._load_auto_cycle(cycle_doc)
        lights_doc = _section(auto_doc, proto.CFG_AUTO_LIGHTS)
        for key in proto.AUTO_LIGHT_KEYS:
            if key in lights_doc:
                self.auto_lights[key] = max(
                    proto.AUTO_BLINK_MS_MIN,
                    min(proto.AUTO_BLINK_MS_MAX,
                        _int(lights_doc.get(key), self.auto_lights[key])))
        # ---- 运行保护（protect 节；2026-11-15；缺节=保持缺省）----
        protect = _section(doc, proto.CFG_SECTION_PROTECT)
        if protect:
            # [0 是合法值（=关闭保护）] 故用"键存在"判，而不是 `or`。
            #
            # [2026-11-15 审计] 全部走 `_int`/`_float`/`_bool` 宽松取：
            #   ① 压差上限的 `or` 陷阱：`float(v or DEF)` 会把**合法的 0.0**
            #      变成缺省 0.8（0 在语义上=不限/禁用上限）；
            #   ② 手改配置文件写成字符串/None 时，`float()` 直接抛
            #      ValueError ⇒ **服务起不来**（配置问题不该拖垮现场）。
            if "inlet_noflow_timeout_ms" in protect:
                self.protect_inlet_noflow_timeout_ms = max(
                    proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN,
                    min(proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX,
                        _int(protect.get("inlet_noflow_timeout_ms"),
                             self.protect_inlet_noflow_timeout_ms)))
            if "inlet_noflow_stop" in protect:
                self.protect_inlet_noflow_stop = _bool(
                    protect.get("inlet_noflow_stop"),
                    self.protect_inlet_noflow_stop)
            # ---- 压差过大保护（2026-11-15）----
            if "press_high_enabled" in protect:
                self.protect_press_high_enabled = _bool(
                    protect.get("press_high_enabled"),
                    self.protect_press_high_enabled)
            if "press_high_limit_mpa" in protect:
                self.protect_press_high_limit_mpa = max(
                    proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MIN,
                    min(proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MAX,
                        _float(protect.get("press_high_limit_mpa"),
                               self.protect_press_high_limit_mpa)))
            if "press_high_stop" in protect:
                self.protect_press_high_stop = _bool(
                    protect.get("press_high_stop"),
                    self.protect_press_high_stop)
            # ---- 电机超温保护（2026-11-20）----
            if "motor_over_temp_enabled" in protect:
                self.protect_motor_over_temp_enabled = _bool(
                    protect.get("motor_over_temp_enabled"),
                    self.protect_motor_over_temp_enabled)
            if "motor_over_temp_limit_c" in protect:
                self.protect_motor_over_temp_limit_c = max(
                    proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MIN,
                    min(proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MAX,
                        _float(protect.get("motor_over_temp_limit_c"),
                               self.protect_motor_over_temp_limit_c)))
            if "motor_over_temp_stop" in protect:
                self.protect_motor_over_temp_stop = _bool(
                    protect.get("motor_over_temp_stop"),
                    self.protect_motor_over_temp_stop)
            # ---- 电箱超温保护（2026-11-25）----
            if "box_over_temp_enabled" in protect:
                self.protect_box_over_temp_enabled = _bool(
                    protect.get("box_over_temp_enabled"),
                    self.protect_box_over_temp_enabled)
            if "box_over_temp_limit_c" in protect:
                self.protect_box_over_temp_limit_c = max(
                    proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_MIN,
                    min(proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_MAX,
                        _float(protect.get("box_over_temp_limit_c"),
                               self.protect_box_over_temp_limit_c)))
            if "box_over_temp_stop" in protect:
                self.protect_box_over_temp_stop = _bool(
                    protect.get("box_over_temp_stop"),
                    self.protect_box_over_temp_stop)
            # ---- 压力1/2 过高保护（2026-11-25）----
            for _p, _attr in ((1, "protect_press1_high"),
                              (2, "protect_press2_high")):
                _en = "press%d_high_enabled" % _p
                _lim = "press%d_high_limit_mpa" % _p
                _st = "press%d_high_stop" % _p
                if _en in protect:
                    setattr(self, _attr + "_enabled", _bool(
                        protect.get(_en), getattr(self, _attr + "_enabled")))
                if _lim in protect:
                    setattr(self, _attr + "_limit_mpa", max(
                        proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MIN,
                        min(proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MAX,
                            _float(protect.get(_lim),
                                   getattr(self, _attr + "_limit_mpa")))))
                if _st in protect:
                    setattr(self, _attr + "_stop", _bool(
                        protect.get(_st), getattr(self, _attr + "_stop")))
        # ---- 相机（camera 节；2026-11-19；缺节=保持缺省）----
        # [cfg 文件用 snake_case，协议用 camelCase] —— 这里是 **cfg 侧**
        # 键名（远端设置走协议的 camelCase 键，见 app._h_config_set）。
        camera = _section(doc, proto.CFG_SECTION_CAMERA)
        if camera:
            self.camera_enabled = _bool(camera.get("enabled"),
                                        self.camera_enabled)
            if "ip" in camera:
                self.camera_ip = str(camera.get("ip") or "")
            self.camera_port = max(1, min(65535, _int(camera.get("port"),
                                                       self.camera_port)))
            if "username" in camera:
                self.camera_username = str(camera.get("username") or "")
            # [口令] 空串是**合法值**（相机可能无口令）⇒ 不能用 `or` 回落。
            if "password" in camera:
                self.camera_password = str(camera.get("password") or "")
            # [码流档] 非法值**回落缺省**（配置文件手改不该让服务起不来；
            # 运行期 SET 走 fail-closed 明确报错，见 app._cfg_set_camera）。
            if "stream" in camera:
                stream = str(camera.get("stream") or "")
                if stream in proto.CAMERA_STREAM_CHOICES:
                    self.camera_stream = stream
            self.camera_fps = max(proto.CAMERA_FPS_MIN,
                                  min(proto.CAMERA_FPS_MAX,
                                      _int(camera.get("fps"),
                                           self.camera_fps)))
            self.camera_width = max(proto.CAMERA_WIDTH_MIN,
                                    min(proto.CAMERA_WIDTH_MAX,
                                        _int(camera.get("width"),
                                             self.camera_width)))
            self.camera_quality = max(proto.CAMERA_QUALITY_MIN,
                                      min(proto.CAMERA_QUALITY_MAX,
                                          _int(camera.get("quality"),
                                               self.camera_quality)))
            self.camera_timeout_ms = max(500, min(60000,
                                                  _int(camera.get("timeout_ms"),
                                                       self.camera_timeout_ms)))
            # [2026-11-19 ES 直传] transport/codec；配置文件非法值回缺省
            # （fail-closed 的协议面在 app._cfg_camera_value，这里是容错读）。
            transport = str(camera.get("transport")
                            or proto.CAMERA_TRANSPORT_DEF)
            if transport in proto.CAMERA_TRANSPORT_CHOICES:
                self.camera_transport = transport
            codec = str(camera.get("codec") or proto.CAMERA_CODEC_H264)
            if codec in proto.CAMERA_CODEC_CHOICES:
                self.camera_codec = codec
            hw = str(camera.get("hw_decode") or proto.CAMERA_HW_DEF)
            if hw in proto.CAMERA_HW_CHOICES:
                self.camera_hw_decode = hw
            # [2026-11-20 MAC/重连] 非法值回缺省（配置文件容错读；协议面
            # fail-closed 在 app._cfg_camera_value）。
            mode = str(camera.get("resolve_mode") or proto.CAMERA_RESOLVE_DEF)
            if mode in proto.CAMERA_RESOLVE_CHOICES:
                self.camera_resolve_mode = mode
            if "mac" in camera:
                from hsfsrv import camera_mac as _cmac
                norm = _cmac.normalize_mac(camera.get("mac"))
                self.camera_mac = norm if norm else ""
            self.camera_reconnect_enabled = _bool(
                camera.get("reconnect_enabled"),
                self.camera_reconnect_enabled)
            self.camera_reconnect_interval_s = max(
                proto.CAMERA_RECONNECT_INTERVAL_S_MIN,
                min(proto.CAMERA_RECONNECT_INTERVAL_S_MAX,
                    _int(camera.get("reconnect_interval_s"),
                         self.camera_reconnect_interval_s)))
            # [2026-11-20 MAC/重连] 非法值回缺省（配置文件容错读；协议面
            # fail-closed 在 app._cfg_camera_value）。
            mode = str(camera.get("resolve_mode") or proto.CAMERA_RESOLVE_DEF)
            if mode in proto.CAMERA_RESOLVE_CHOICES:
                self.camera_resolve_mode = mode
            if "mac" in camera:
                from hsfsrv import camera_mac as _cmac
                norm = _cmac.normalize_mac(camera.get("mac"))
                self.camera_mac = norm if norm else ""
            self.camera_reconnect_enabled = _bool(
                camera.get("reconnect_enabled"),
                self.camera_reconnect_enabled)
            self.camera_reconnect_interval_s = max(
                proto.CAMERA_RECONNECT_INTERVAL_S_MIN,
                min(proto.CAMERA_RECONNECT_INTERVAL_S_MAX,
                    _int(camera.get("reconnect_interval_s"),
                         self.camera_reconnect_interval_s)))
        # ---- 登录策略（2026-11-08；缺节=保持缺省）----
        login = _section(doc, "login")
        if login:
            # [2026-11-15 审计] 走 _bool：原来的 bool() 把字符串 "false"
            # 当成 True ⇒ 手改配置写成 "allow_anonymous": "false"（想
            # **关掉**匿名登录）实际是**开着** ⇒ 安全项被反着读。
            self.login_allow_anonymous = _bool(
                login.get("allow_anonymous"), self.login_allow_anonymous)
            self.login_allow_concurrent = _bool(
                login.get("allow_concurrent"), self.login_allow_concurrent)
            self.login_max_sessions = max(
                0, min(256, _int(login.get("max_sessions"),
                                 self.login_max_sessions)))
        # ---- 自动模式配置（2026-11-11；缺节/缺模式=该模式保持缺省）----
        # 未知设备 id 丢弃（前向兼容：服务端增删设备清单时旧配置不炸）。
        auto_doc = _section(doc, proto.CFG_SECTION_AUTO)
        # [搅拌最高转速 2026-11-25] auto 节顶层；旧配置缺键 = 1440（不限制）。
        _smax = auto_doc.get("stir_max_rpm",
                             auto_doc.get(proto.AUTO_STIR_MAX_RPM))
        if _smax is not None:
            self.stir_max_rpm = max(
                proto.AUTO_STIR_MAX_RPM_MIN,
                min(proto.AUTO_STIR_MAX_RPM_MAX,
                    _int(_smax, proto.AUTO_STIR_MAX_RPM_DEF)))
        modes_doc = _section(auto_doc, "modes")
        known_devs = set(proto.AUTO_DEVICE_IDS)
        for mid, mode_cfg in self.auto.items():
            m = modes_doc.get(mid)
            if not isinstance(m, dict):
                continue
            devs = m.get("devices")
            if isinstance(devs, list):
                mode_cfg["devices"] = [
                    d for d in devs if isinstance(d, str) and d in known_devs]
            rpm = m.get("stir_rpm")
            if rpm is not None:
                mode_cfg["stir_rpm"] = max(
                    proto.AUTO_STIR_RPM_MIN,
                    min(proto.AUTO_STIR_RPM_MAX,
                        _int(rpm, proto.AUTO_STIR_RPM_DEF)))
            if "stir_reverse" in m:
                mode_cfg["stir_reverse"] = bool(m.get("stir_reverse"))
            # [2026-11-25] 搅拌变频器加速度（rpm/s；0=跟随变频器）。
            _acc = m.get("stir_accel_rpm_s",
                         m.get(proto.AUTO_MODE_STIR_ACCEL_RPM_S))
            if _acc is not None:
                mode_cfg[proto.AUTO_MODE_STIR_ACCEL_RPM_S] = max(
                    proto.AUTO_STIR_ACCEL_RPM_S_MIN,
                    min(proto.AUTO_STIR_ACCEL_RPM_S_MAX,
                        _int(_acc, proto.AUTO_STIR_ACCEL_RPM_S_DEF)))
            # [复位开关 2026-11-12] 停止/急停时是否关阀复位（逐模式）。
            if proto.AUTO_MODE_RESET_VALVES_ON_STOP in m:
                mode_cfg[proto.AUTO_MODE_RESET_VALVES_ON_STOP] = bool(
                    m.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP))
            if proto.AUTO_MODE_RESET_VALVES_ON_ESTOP in m:
                mode_cfg[proto.AUTO_MODE_RESET_VALVES_ON_ESTOP] = bool(
                    m.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP))
            # [换相策略 2026-11-26 实例化] 两策略键已从**单模式 modes**
            # 删除（策略逐实例配置，见 auto.cycle.instances）；旧配置文件
            # 里残留的这两键 load 时**忽略不报错**（不再读进 modes）。
        # [限速收口] 旧配置里模式转速可能高于新上限 ⇒ 全部夹到上限（GET/
        # 运行期/落盘看到的是同一份有效值）。实例的 stir_rpm 同款夹取
        # （2026-11-26 实例化：instances 自带转速）。
        for mode_cfg in self.auto.values():
            try:
                mode_cfg["stir_rpm"] = min(
                    int(mode_cfg.get("stir_rpm", proto.AUTO_STIR_RPM_DEF)),
                    int(self.stir_max_rpm))
            except (TypeError, ValueError):
                mode_cfg["stir_rpm"] = min(
                    proto.AUTO_STIR_RPM_DEF, int(self.stir_max_rpm))
        # [实例转速夹取] 在 modes 装载完、stir_max_rpm 读到之后做 ⇒ 合成/
        # 显式实例都用**新上限**。夹完刷新派生视图（实例设备/转速此时才
        # 是文件里的最终值——cycle 节在 modes 之前解析，合成时用的还是
        # 缺省 modes，这里统一重算一次）。
        for inst in (self.auto_cycle.get("instances") or []):
            if not isinstance(inst, dict):
                continue
            try:
                inst["stir_rpm"] = min(
                    int(inst.get("stir_rpm", proto.AUTO_STIR_RPM_DEF)),
                    int(self.stir_max_rpm))
            except (TypeError, ValueError):
                inst["stir_rpm"] = min(
                    proto.AUTO_STIR_RPM_DEF, int(self.stir_max_rpm))
        self._sync_auto_cycle_legacy()
        return True

    def save(self):
        """落盘（原子写：先临时文件再 rename）。失败不致命（只记日志）。"""
        doc = {
            "server": {
                "port": self.port,
                "serial_enabled": self.enable_serial,
            },
            "serial": {
                "port": self.serial.port,
                "baudrate": self.serial.baudrate,
                "timeout_ms": self.serial.timeout_ms,
                "retries": self.serial.retries,
                "poll_gap_ms": self.serial.poll_gap_ms,
                "vfd_poll_ms": self.serial.vfd_poll_ms,
                "temphum_poll_ms": self.serial.temphum_poll_ms,
                "temphum_push_ms": self.serial.temphum_push_ms,
                "vfd_enabled": self.enable_vfd,
                "temphum_enabled": self.enable_temphum,
                # 浊度仪/流量计开关与浊度周期：load 侧有通路、save 侧
                # 曾漏写 —— 远程改完重启丢配置（远程采集配置 e2e 实测抓到）。
                "flowmeter_enabled": self.enable_flowmeter,
                # [逐台开关] 两台独立设备各存各的。旧键 turbidity_enabled
                # 落盘时写**任一启用**（旧版读到它时的语义就是"这路开没开"；
                # 逐台键存在时 load 会以逐台为准）。
                "turbidity_enabled": bool(self.enable_turbidity1
                                          or self.enable_turbidity2),
                "turbidity1_enabled": self.enable_turbidity1,
                "turbidity2_enabled": self.enable_turbidity2,
                "turbidity_poll_ms": self.turbidity_poll_ms,
                # [逐台周期 2026-11-16 审计] 同开关：只读不写 ⇒ 远程改完重启丢。
                "turbidity1_poll_ms": self.turbidity1_poll_ms,
                "turbidity2_poll_ms": self.turbidity2_poll_ms,
                "flowmeter_poll_ms": self.flowmeter_poll_ms,
                # [滤速 2026-11-25] 过滤面积（m²；0=未设置）。
                "filter_area_m2": float(self.filter_area_m2),
                # [铭牌参数] 转速换算基准（换电机后手改的值必须存得住）。
                "vfd_rated_rpm": self.serial.vfd_rated_rpm,
                "vfd_rated_hz": self.serial.vfd_rated_hz,
                "vfd_max_hz": self.serial.vfd_max_hz,
                # 站号/按台旋钮同样不能只读不写（save 侧漏写 ⇒ 手改的
                # 站号在下一次落盘时被静默冲回缺省，同一类实锅）。
                "unit_ids": {
                    "vfd": self.vfd_unit_id,
                    "temphum": self.temphum_unit_id,
                    "flowmeter": self.flowmeter_unit_id,
                    "turbidity": list(self.turbidity_unit_ids),
                    "turbidity_byte_orders": list(self.turbidity_byte_orders),
                    "turbidity_read_counts": list(self.turbidity_read_counts),
                },
            },
            # 第二条 485：与 serial 平级，避免把 9600 的口/速率混进路 A。
            "serial_b": {
                "port": self.serial_b.port,
                "baudrate": self.serial_b.baudrate,
                "timeout_ms": self.serial_b.timeout_ms,
                "retries": self.serial_b.retries,
                "poll_gap_ms": self.serial_b.poll_gap_ms,
            },
            # [显示渐变 2026-11-24] 只影响客户端数值着色（浊度逐台 + 电箱
            # 温度）；压力/电机温度沿用 analog 节各自的量程，不重复存。
            "display": {
                "turbidity1_range_min": float(self.turbidity1_range_min),
                "turbidity1_range_max": float(self.turbidity1_range_max),
                "turbidity2_range_min": float(self.turbidity2_range_min),
                "turbidity2_range_max": float(self.turbidity2_range_max),
                "temphum_temp_low_c": float(self.temphum_temp_low_c),
                "temphum_temp_high_c": float(self.temphum_temp_high_c),
                "vfd_speed_range_min": float(self.vfd_speed_range_min),
                "vfd_speed_range_max": float(self.vfd_speed_range_max),
                "vfd_motor_temp_range_min":
                    float(self.vfd_motor_temp_range_min),
                "vfd_motor_temp_range_max":
                    float(self.vfd_motor_temp_range_max),
                "turbidity1_gradient_enabled":
                    bool(self.turbidity1_gradient_enabled),
                "turbidity2_gradient_enabled":
                    bool(self.turbidity2_gradient_enabled),
                "temphum_temp_gradient_enabled":
                    bool(self.temphum_temp_gradient_enabled),
                "vfd_speed_gradient_enabled":
                    bool(self.vfd_speed_gradient_enabled),
                "vfd_motor_temp_gradient_enabled":
                    bool(self.vfd_motor_temp_gradient_enabled),
                "press_gradient_enabled": bool(self.press_gradient_enabled),
                "press1_gradient_enabled": bool(self.press1_gradient_enabled),
                "press2_gradient_enabled": bool(self.press2_gradient_enabled),
            },
            "analog": {
                "enabled": self.enable_analog,
                "host": self.analog.host,
                "port": self.analog.port,
                "unit_id": self.analog.unit_id,
                "channels": self.analog.channels,
                "poll_ms": self.analog.poll_ms,
                # [压差计/温度传感器通道与量程 2026-11-20 远端可配]
                "press_channel": int(self.analog.press_channel),
                "press_low_mpa": float(self.analog.press_low_mpa),
                "press_high_mpa": float(self.analog.press_high_mpa),
                "press_below4_valid": bool(self.analog.press_below4_valid),
                # [两台压力传感器 2026-11-23 远端可配]
                "press1_channel": int(self.analog.press1_channel),
                "press1_low_mpa": float(self.analog.press1_low_mpa),
                "press1_high_mpa": float(self.analog.press1_high_mpa),
                "press1_below4_valid": bool(
                    self.analog.press1_below4_valid),
                "press2_channel": int(self.analog.press2_channel),
                "press2_low_mpa": float(self.analog.press2_low_mpa),
                "press2_high_mpa": float(self.analog.press2_high_mpa),
                "press2_below4_valid": bool(
                    self.analog.press2_below4_valid),
                "temp_channel": int(self.analog.temp_channel),
                "temp_low_c": float(self.analog.temp_low_c),
                "temp_high_c": float(self.analog.temp_high_c),
                "temp_below4_valid": bool(self.analog.temp_below4_valid),
                    "scales": [self.analog.scales[i].to_doc()
                           for i in sorted(self.analog.scales)],
            },
            "digital": {
                "enabled": self.enable_digital,
                "poll_ms": self.digital_poll_ms,
                "stale_ms": self.digital_stale_ms,
                "modules": [
                    {
                        "name": m.name,
                        "host": m.host,
                        "port": m.port,
                        "unit_id": m.unit_id,
                        "timeout_s": m.timeout_s,
                        "channels": m.channels,
                        "publish": m.publish,
                    }
                    for m in self.digital_modules
                ],
            },
            "valves": {
                "enabled": self.enable_valves,
                "poll_ms": self.valve_poll_ms,
                "move_timeout_ms": self.valve_move_timeout_ms,
                "items": self.valve_items,
            },
            "buttons": {
                "enabled": self.enable_buttons,
                "debounce_ms": self.button_debounce_ms,
                "items": self.button_items,
            },
            "storage": {
                "enabled": self.storage.enabled,
                "path": self.storage.path,
                "sample_ms": self.storage.sample_ms,
                "max_kb": self.storage.max_kb,
                "full_policy": self.storage.full_policy,
                "keep_files": self.storage.keep_files,
                # [历史入库触发方式 2026-11-12]
                "history_trigger": self.storage.history_trigger,
            },
            "login": {
                "allow_anonymous": self.login_allow_anonymous,
                "allow_concurrent": self.login_allow_concurrent,
                "max_sessions": self.login_max_sessions,
            },
            # [运行保护 2026-11-15] 与远端设置页「运行保护」卡同源。
            proto.CFG_SECTION_PROTECT: {
                "inlet_noflow_timeout_ms":
                    int(self.protect_inlet_noflow_timeout_ms),
                "inlet_noflow_stop": bool(self.protect_inlet_noflow_stop),
                # [压差过大保护 2026-11-15]
                "press_high_enabled": bool(self.protect_press_high_enabled),
                "press_high_limit_mpa":
                    float(self.protect_press_high_limit_mpa),
                "press_high_stop": bool(self.protect_press_high_stop),
                # [电机超温保护 2026-11-20]
                "motor_over_temp_enabled":
                    bool(self.protect_motor_over_temp_enabled),
                "motor_over_temp_limit_c":
                    float(self.protect_motor_over_temp_limit_c),
                "motor_over_temp_stop": bool(self.protect_motor_over_temp_stop),
                # [电箱超温保护 2026-11-25]
                "box_over_temp_enabled":
                    bool(self.protect_box_over_temp_enabled),
                "box_over_temp_limit_c":
                    float(self.protect_box_over_temp_limit_c),
                "box_over_temp_stop": bool(self.protect_box_over_temp_stop),
                # [压力1/2 过高保护 2026-11-25]
                "press1_high_enabled":
                    bool(self.protect_press1_high_enabled),
                "press1_high_limit_mpa":
                    float(self.protect_press1_high_limit_mpa),
                "press1_high_stop": bool(self.protect_press1_high_stop),
                "press2_high_enabled":
                    bool(self.protect_press2_high_enabled),
                "press2_high_limit_mpa":
                    float(self.protect_press2_high_limit_mpa),
                "press2_high_stop": bool(self.protect_press2_high_stop),
            },
            # [相机 2026-11-19] 与远端设置页「相机」卡同源。
            # [cfg 侧 snake_case；协议侧 camelCase 见 proto.CFGK_CAMERA_*]
            proto.CFG_SECTION_CAMERA: {
                "enabled": bool(self.camera_enabled),
                "ip": str(self.camera_ip),
                "port": int(self.camera_port),
                "username": str(self.camera_username),
                "password": str(self.camera_password),
                "stream": str(self.camera_stream),
                "fps": int(self.camera_fps),
                "width": int(self.camera_width),
                "quality": int(self.camera_quality),
                "timeout_ms": int(self.camera_timeout_ms),
                "transport": str(self.camera_transport),
                "resolve_mode": str(self.camera_resolve_mode),
                "mac": str(self.camera_mac),
                "reconnect_enabled": bool(self.camera_reconnect_enabled),
                "reconnect_interval_s":
                    int(self.camera_reconnect_interval_s),
                "resolve_mode": str(self.camera_resolve_mode),
                "mac": str(self.camera_mac),
                "reconnect_enabled": bool(self.camera_reconnect_enabled),
                "reconnect_interval_s":
                    int(self.camera_reconnect_interval_s),
                "codec": str(self.camera_codec),
                "hw_decode": str(self.camera_hw_decode),
            },
            proto.CFG_SECTION_AUTO: {
                # [2026-11-25] 搅拌最高转速限制（全局；旧配置缺键=1440）。
                "stir_max_rpm": int(self.stir_max_rpm),
                "modes": {mid: {
                    "devices": list(m["devices"]),
                    "stir_rpm": int(m["stir_rpm"]),
                    "stir_reverse": bool(m["stir_reverse"]),
                    # [2026-11-25] 搅拌变频器加速度（rpm/s；0=跟随）。
                    "stir_accel_rpm_s": int(m.get(
                        proto.AUTO_MODE_STIR_ACCEL_RPM_S,
                        proto.AUTO_STIR_ACCEL_RPM_S_DEF)),
                    proto.AUTO_MODE_RESET_VALVES_ON_STOP:
                        bool(m[proto.AUTO_MODE_RESET_VALVES_ON_STOP]),
                    proto.AUTO_MODE_RESET_VALVES_ON_ESTOP:
                        bool(m[proto.AUTO_MODE_RESET_VALVES_ON_ESTOP]),
                    # [换相策略 2026-11-26 实例化] 两策略键已从 modes 删除
                    # （策略逐实例配置，写在 cycle.instances 里）；旧文件
                    # 里残留的键 load 时忽略。
                } for mid, m in self.auto.items()},
                # [按钮灯闪烁 2026-11-24] 三盏灯各自亮/灭毫秒。
                proto.CFG_AUTO_LIGHTS: {
                    key: int(self.auto_lights.get(
                        key, proto.AUTO_BLINK_ON_MS_DEF
                        if key.endswith("OnMs") else proto.AUTO_BLINK_OFF_MS_DEF))
                    for key in proto.AUTO_LIGHT_KEYS
                },
                # [自动循环配置 2026-11-12；2026-11-26 实例化]
                proto.CFG_AUTO_CYCLE: self._auto_cycle_file_doc(),
            },
            # [sim 节 2026-11-16 审计] load 认这些旋钮、save 不写 ⇒ 现场
            # 联调调好的时序在第一次远程保存配置时被静默冲回缺省。
            "sim": {
                "turbidity_reply_ms": [list(pair) for pair
                                       in self.sim_turbidity_reply_ms],
                "flowmeter_reply_ms": list(self.sim_flowmeter_reply_ms),
                "recovery_ms": self.sim_recovery_ms,
                "turbidity_wobble_ntu": list(self.turbidity_sim_wobble),
                "temp_wobble_c": list(self.turbidity_sim_temp_wobble),
                # [2026-11-19] 与 load 的 sim 节新增键同源（空管复现旋钮）。
                "flowmeter_empty": bool(self.flowmeter_sim_empty),
                "flowmeter_flow": float(self.flowmeter_sim_flow),
            },
            "stale": {
                "vfd_ms": self.vfd_stale_ms,
                "temphum_ms": self.temphum_stale_ms,
                # [2026-11-15 审计] 流量计/浊度仪的陈旧阈值**读得到却存不
                # 下**：load 认 flowmeter_ms / turbidity_ms，save 却不写这两
                # 个键 ⇒ 任何一次 save()（改任何配置都会 save）都会把现场
                # 手改的阈值**抹掉**（而且下次启动就没了）。
                "flowmeter_ms": self.flowmeter_stale_ms,
                "turbidity_ms": self.turbidity_stale_ms,
                "digital_ms": self.digital_stale_ms,
            },
            "runtime": self._runtime,
        }
        self.ensure_cfg_dir()
        tmp = self.config_path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as fh:
                json.dump(doc, fh, ensure_ascii=False, indent=2)
                fh.flush()
                os.fsync(fh.fileno())
            os.replace(tmp, self.config_path)
        except OSError:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            return False
        return True

    # ---- 运行期持久化（运行模式） ----
    def run_mode(self, default=0):
        return _int(self._runtime.get("run_mode"), default)

    def save_run_mode(self, mode):
        self._runtime["run_mode"] = int(mode)
        self.save()

    # ---- 累计自动运行时长（服务端持久化；2026-11-12 用户定案）----
    def auto_run_ms(self, default=0):
        """已持久化的**累计自动运行时长**（毫秒；服务端重启后据此恢复）。"""
        return _int(self._runtime.get("auto_run_ms"), default)

    def save_auto_run_ms(self, ms):
        """把累计时长写进 runtime 节并落盘（原子替换，小文件无压力）。"""
        self._runtime["auto_run_ms"] = int(ms)
        self.save()

    # ---- 粘性自动模式（服务端持久化；2026-11-13 板端实锅）----
    # 物理启动按钮用「上次选中的」模式（粘性）；粘性只存内存会在服务端
    # 重启后清空 ⇒ 现场按启动被拒「未选择自动模式」（界面下拉显示的是
    # 客户端本地缺省，与服务端粘性无关）。持久化后重启恢复上次模式。
    def auto_mode(self, default=""):
        """已持久化的粘性自动模式 id（produce/backwash/rinse/auto；空=从未选）。"""
        value = self._runtime.get("auto_mode")
        if isinstance(value, str) and value:
            return value
        return default

    def save_auto_mode(self, mode):
        """把粘性模式写进 runtime 节并落盘（原子替换，小文件无压力）。"""
        self._runtime["auto_mode"] = str(mode or "")
        self.save()


def _section(doc, name):
    """取配置里的一个节；**不是对象就当没有**。

    [2026-11-15 审计·容错] 原来写 `doc.get(name) or {}` ⇒ 手改配置把某节
    写成字符串/数组时，后面 `sec.get(...)` 会抛 AttributeError ⇒
    **服务端起不来**（配置笔误变现场事故）。⇒ 类型不对一律当"没有这节"。
    """
    val = doc.get(name)
    return val if isinstance(val, dict) else {}


def _int(value, default):
    """宽松取整：非法值回退 default（配置文件手改出错不应让服务起不来）。"""
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _opt_int(value):
    """可选整数：None/空/非法 → None（表示"未配置"，与 0 区分）。"""
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _float(value, default):
    if isinstance(value, bool):
        return default
    if isinstance(value, (int, float)):
        return float(value)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _bool(value, default):
    """宽松取布尔：只认真正的布尔与 0/1，其余回退 default。

    [为什么不能用 `bool(value)`] `bool("false")` 是 **True** ⇒ 手改配置文件
    写成 `"inlet_noflow_stop": "false"`（想"只报警不停机"）实际却是
    **停机**⇒ 现场误停线。字符串 "true"/"false"/"1"/"0" 明确解析，
    其它一律回退缺省（不猜）。
    """
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in ("true", "1", "yes", "on"):
            return True
        if text in ("false", "0", "no", "off"):
            return False
    return default
