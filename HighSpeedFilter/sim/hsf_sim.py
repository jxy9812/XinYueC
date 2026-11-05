# -*- coding: utf-8 -*-
"""HSF 设备模拟器（独立程序，不依赖服务端业务代码）。

    python sim/hsf_sim.py                        # 起模拟服务端（6288）
    python sim/hsf_sim.py --port 6100            # 模拟 C 版服务端端口
    python sim/hsf_sim.py --show                 # 同时开客户端看界面
    python sim/hsf_sim.py --show --page-name analog

================================================================================
                              它是给谁用的
================================================================================

这是一个**假服务端**：只实现 XLink 会话层 + HSF 业务命令的最小子集，数据
全部是合成的，不碰任何真实设备。用途：

    · 设备没到位时先把客户端界面/交互跑起来
    · C 端客户端（HsfClient.exe）也可以直接连它 —— 协议面完全一致，
      端口给 6100 就与 C 版服务端的缺省端口对上
    · 回归测试里当"可控数据源"（可指定 32 点、压差计读数等）

================================================================================
                     ⚠ 它与 `--sim` 的区别（选错会白查一轮）
================================================================================

本项目有**两种**模拟方式，验证范围完全不同：

| | 本文件（`sim/hsf_sim.py`） | `python -m hsfsrv --sim` |
|---|---|---|
| 替换范围 | **整个服务端**（会话+业务+设备全自己写） | **只替换设备物理层** |
| 业务层 | 假（简化命令处理） | **真实 app.py** |
| 设备驱动 | 无 | **真实 devices.py** |
| 自动模式闸门 | **不生效**（永远能控） | 生效 |
| 点动看门狗 | **不生效**（不会自动停） | 生效 |
| 电磁阀 20s 定时器 / 告警 | **不生效** | 生效 |
| 适合 | C 端纯协议+界面联调、不装服务端环境 | **验证业务逻辑**、Python 端联调 |

**要验证控制闸门/看门狗/阀告警 → 必须用 `--sim`**，本文件测不出来
（它没有那些逻辑，表现为"怎么控都成功"）。

[为什么做成独立程序] 之前它是 server_py/tools 下的脚本，只能跟着 Python
服务端跑。拆出来后：
    · 只需要 xlink.py / bson.py / auth.py 三个纯协议模块（无业务依赖）
    · 常量和键名**自带一份**，并有 --check 校验与服务端 proto.py 逐项一致，
      避免两份定义悄悄漂移（漂移后果是"客户端收不到某字段"，极难查）。

================================================================================
                             协议常量自持
================================================================================

下面 CONST 表是模拟器自己的定义（不从 hsfsrv.proto 导入，保证独立可跑）。
`--check` 会尝试导入服务端 proto 并逐项比对，不一致就报错退出。
"""

import argparse
import math
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                    # HighSpeedFilter/
SERVER_PY = os.path.join(ROOT, "server_py")
CLIENT_PY = os.path.join(ROOT, "client_py")


# ============================================================================
#                        协议常量（模拟器自带）
# ============================================================================

#: 数据载荷类型
DATATYPE_BSON = 2

#: 主题号（topicId = 0x00010000 + n）
TOPIC_VFD_STATUS = 0x00010001
TOPIC_AI_STATUS = 0x00010002
TOPIC_TEMPHUM_STATUS = 0x00010003
TOPIC_FLOWMETER_STATUS = 0x00010004
TOPIC_DEVICE_HEALTH = 0x00010005
TOPIC_DIGITAL_STATUS = 0x00010006
TOPIC_VALVE_STATUS = 0x00010007
TOPIC_TURBIDITY_STATUS = 0x00010009
TOPIC_PUMP_STATUS = 0x0001000A

#: 周期推送 / 首连补发要发的全部主题。
#: [为什么抽成常量] 原来在两处各写一份元组，加流量计时就漏了其中一处
#: （症状：别的页都有数据，只有流量计页空）。一份定义，两处引用。
PUSH_TOPICS = (
    TOPIC_DIGITAL_STATUS,
    TOPIC_AI_STATUS,
    TOPIC_VFD_STATUS,
    TOPIC_FLOWMETER_STATUS,
    TOPIC_TEMPHUM_STATUS,
    TOPIC_VALVE_STATUS,
    TOPIC_TURBIDITY_STATUS,
    TOPIC_PUMP_STATUS,
    TOPIC_DEVICE_HEALTH,
)

#: 命令号（务必与 server_py/hsfsrv/proto.py 一致；--check 会逐项校验）
CMD_VFD_CONTROL = 0x0101
CMD_VFD_SNAPSHOT = 0x0102
CMD_AI_SNAPSHOT = 0x0103
CMD_TEMPHUM_SNAPSHOT = 0x0104
CMD_RUN_MODE = 0x0105
CMD_FLOWMETER_SNAPSHOT = 0x0106
CMD_DEVICE_HEALTH_SNAPSHOT = 0x0107
CMD_DIGITAL_SNAPSHOT = 0x0108
CMD_DIGITAL_CONTROL = 0x0109
CMD_VALVE_SNAPSHOT = 0x0110
CMD_VALVE_CONTROL = 0x0111
CMD_TURBIDITY_SNAPSHOT = 0x0112
CMD_PUMP_SNAPSHOT = 0x0113
CMD_PUMP_CONTROL = 0x0114

#: 变频器控制动作
OP_START = 4
OP_STOP = 5
OP_FREE_STOP = 6
OP_QUICK_STOP = 7
OP_RESET_FAULT = 8
OP_JOG = 9
OP_SET_FREQUENCY = 2
OP_SET_SPEED = 3
OP_JOG_RELEASE = 101

#: 设备类别
KIND_VFD = 0
KIND_TEMPHUM = 1
KIND_ANALOG_IO = 3
KIND_DIGITAL_IO = 5
KIND_VALVE = 6

#: 健康码
HEALTH_OK = 0
HEALTH_FAULT = 1
HEALTH_STALE = 2
HEALTH_OFFLINE = 3

#: 通用键
KEY_NAME = "name"
KEY_SIDE = "side"
KEY_VALID = "valid"
KEY_STALE_MS = "staleMs"
KEY_VALID_MASK = "vm"
KEY_COUNT = "count"
#: 全量帧标志：客户端据此决定"重建点表+收名字"还是"只更新状态"。
KEY_FULL = "full"
KEY_DEV = "dev"
KEY_DI = "di"
KEY_DO = "do"
KEY_DEV_COUNT = "devCount"
KEY_DEV_SEQ = "seq"
KEY_DEV_TS = "ts"

#: 数字量通道名键（%d = 通道号）
KEY_DI_NAME_FMT = "din%d"
KEY_DO_NAME_FMT = "don%d"

#: 变频器遥测字段：(有效位, BSON 键, 类型)
VFD_TELEMETRY_FIELDS = (
    (0,  "runState",          "i32"),
    (2,  "running",           "bool"),
    (3,  "faulted",           "bool"),
    (5,  "reverse",           "bool"),
    (8,  "jogActive",         "bool"),
    (7,  "remote",            "bool"),
    (10, "setFreqHz",         "f64"),
    (11, "outputFreqHz",      "f64"),
    (14, "outputVoltageV",    "f64"),
    (15, "outputCurrentA",    "f64"),
    (16, "busVoltageV",       "f64"),
    (18, "outputPowerKw",     "f64"),
    (20, "torquePercent",     "f64"),
    (21, "speedRpm",          "f64"),
    (39, "faultCode",         "u32"),
)


# ============================================================================
#                        合成数据（可按需调）
# ============================================================================

#: 数字量：刻意在**上半区**放信号 —— 任何一环按 16 点截断都会暴露。
FAKE_DI = 0x00FF00FF        # bit0..7 通 + bit16..23 通
FAKE_DO = 0x8000000F        # bit0..3 通 + bit31 通

#: 模拟的阀路数（与现场一致：6 路阀占 DO16..21 与 DI16..27）。
FAKE_VALVE_COUNT = 6

#: 进水泵占的 DO 通道：6 路阀（DO16..21）的**下一个口**。
#:
#: [基址是 16 不是 0] 阀的 DO 从 16 起（`.13` 聚合进 `.12` 的偏移 16），
#: 所以泵 = 16 + 6 = **22**。写成 `FAKE_VALVE_COUNT` 会落到 DO12 ——
#: 那一位不属于任何设备，症状是"泵开关点了但 DO22 不动、数字量页也没名字"。
FAKE_PUMP_DO = 16 + FAKE_VALVE_COUNT      # = 22

#: 浊度仪台数（与现场一致：两台共用一路 485，站号 1 / 3）。
FAKE_TURB_COUNT = 2
#: 两台基准浊度（×100 的整数口径，与下发键一致）：1 号偏高、2 号偏低。
FAKE_TURB_BASE = (1250, 320)

#: 模拟量 8 路的原始毫伏；通道0 = 压差计（2985mV ≈ 11.988mA ≈ 0.499MPa）
FAKE_AI_MV = [2985, 995, 995, 995, 995, 995, 995, 995]
#: 采样电阻（中盛模块：I(mA) = mV / 249）
SHUNT_OHM = 249.0
#: 压差计换算：4~20mA → 0~fullScale
AI0_FULL_SCALE = 1.0
AI0_UNIT = "MPa"
AI0_NAME = "压差计"

#: 变频器额定点（Hz ↔ rpm）
VFD_RATED_RPM = 1440.0
VFD_RATED_HZ = 50.0


def _load_protocol_modules():
    """导入 xlink / bson / auth（三个纯协议模块，无业务依赖）。

    [为什么从 server_py 借用而不是复制] 这三个文件是线协议的**唯一实现**，
    复制一份必然漂移（改了服务端忘了同步模拟器，症状是"模拟器发的帧客户端
    解不开"）。用 sys.path 借用，保证只有一份实现。
    模拟器仍然可以独立运行——只要 server_py 在仓库里（它本来就在）。
    """
    if SERVER_PY not in sys.path:
        sys.path.insert(0, SERVER_PY)
    from hsfsrv import auth, bson, xlink
    return auth, bson, xlink


# ============================================================================
#                              模拟业务层
# ============================================================================

class FakeBusiness(object):
    """冒充服务端业务层：只造数据 + 应答最小命令集。"""

    def __init__(self, bson_mod):
        self._bson = bson_mod
        self.pushed = 0
        #: 变频器模拟状态（可被控制命令改变，让界面有反应）
        self.vfd_running = False
        self.vfd_reverse = False
        self.vfd_jog = False
        #: 给定值缺省取**中间值**（半速）：50Hz 量程的 25Hz，对应 720rpm。
        #: [为什么不给 15Hz] 中间值最能体现"转速↔频率"换算是否对
        #: （25Hz → 720rpm 是一眼能验的关系），也不会让人误以为满速/近零。
        self.vfd_set_hz = VFD_RATED_HZ / 2.0
        self.vfd_out_hz = 0.0
        #: 数字量 DO 位图（**可变**：客户端点 DO 按钮要真的翻转）。
        #: 初值 = FAKE_DO，让界面一上来就有几个"通"，便于看出写操作的效果。
        self.digital_do = FAKE_DO
        #: 流量计累积量（正向随流量累加，反向恒 0）。
        #: 从非零值起步，避免界面显示 0.000 让人以为是默认占位。
        self.flow_forward_total = 12345.678
        self.flow_reverse_total = 0.0

    # ---- 数字量 DI（由阀 DO 推导，模拟行程开关）----
    @property
    def digital_di(self):
        """DI 位图：由阀的 DO 位**推导**而成（模拟行程开关）。

        [为什么不是常量] 数字量页与电磁阀页必须**自洽**：在数字量页把
        DO16 置 1 后，DI16（关到位）/DI17（开到位）应当跟着变；否则两页
        显示矛盾，看的人会以为哪边坏了。

        每路阀 n：DO(16+n) 置位 → DI(16+2n)=0（关到位断）、
        DI(16+2n+1)=1（开到位通）；反之亦然。与现场接线顺序一致
        （先关到位、后开到位）。
        """
        di = FAKE_DI & ~0x0FFF0000        # 清掉 bit16..27（阀占用的 12 个 DI）
        for i in range(FAKE_VALVE_COUNT):
            on = bool(self.digital_do & (1 << (16 + i)))
            close_bit = 16 + i * 2
            open_bit = 16 + i * 2 + 1
            if on:
                di |= (1 << open_bit)     # 开到位通
            else:
                di |= (1 << close_bit)    # 关到位通
        return di

    # ---- 变频器派生量 ----
    def _run_state(self):
        """按实际状态给出 runState（口径见 encode_vfd 的注释）。

        停机时给 **1（停机）**而不是 2（就绪）：本模拟器没有"已上电待命"
        这个概念，说"停机"不会误导；给 2 会让人以为设备已准备好接受启动。
        """
        if not self.vfd_running:
            return 1                       # 停机
        if self.vfd_jog:
            return 6 if self.vfd_reverse else 5    # 点动（正/反）
        return 4 if self.vfd_reverse else 3        # 运行（反/正）

    # ---- 编码 ----
    def encode_digital(self, full=True):
        bson = self._bson
        doc = {
            # [必须带 full] C 端只在 full=True 时解析通道名（名字只在全量帧
            # 里出现，增量帧靠客户端缓存沿用）。原来这里没发 full，C 端一律
            # 当成增量帧 —— 现象是"点画出来了但全都没名字"，而名字明明在
            # 同一帧里。与 server/HsfServerApp.c 的 hsfAppEncodeDigital(bool)
            # 同口径：订阅补发/首连快照 full=True，周期推送 full=False。
            KEY_FULL: bool(full),
            KEY_DEV: 0,
            KEY_NAME: "数字量(模拟32点)",
            KEY_DI: self.digital_di,
            KEY_DO: self.digital_do,
            KEY_COUNT: 32,
            KEY_VALID: True,
            KEY_STALE_MS: 5000,
        }
        if full:
            # 按现场接线照抄服务端的名字推导（阀 n 用 DO(16+n)、
            # DI(16+2n)=关到位、DI(16+2n+1)=开到位）。
            for i in range(FAKE_VALVE_COUNT):
                doc[KEY_DO_NAME_FMT % (16 + i)] = "电磁阀%d 控制" % (i + 1)
                doc[KEY_DI_NAME_FMT % (16 + i * 2)] = "电磁阀%d 关到位" % (i + 1)
                doc[KEY_DI_NAME_FMT % (16 + i * 2 + 1)] = "电磁阀%d 开到位" % (i + 1)
            # [泵的名字同源] DO22 就是进水泵那一口（6 路阀的下一个）。
            # 服务端由泵配置推导后写进数字量名字，这里照抄 —— 两页同名，
            # 现场对着数字量页查线时才不会认错口。
            doc[KEY_DO_NAME_FMT % FAKE_PUMP_DO] = "进水泵"
        return bson.encode(doc)

    def encode_analog(self):
        bson = self._bson
        doc = {}
        vmask = 0
        for i, mv in enumerate(FAKE_AI_MV):
            doc["r%d" % i] = mv
            doc["u%d" % i] = int(round(mv / SHUNT_OHM * 1000.0))
            vmask |= 1 << i
        ma = FAKE_AI_MV[0] / SHUNT_OHM
        value = (ma - 4.0) / 16.0 * AI0_FULL_SCALE
        doc["e0"] = int(round(value * 1000.0))
        doc["eu0"] = AI0_UNIT
        doc["en0"] = AI0_NAME
        doc["n"] = len(FAKE_AI_MV)
        doc["v"] = vmask
        return bson.encode(doc)

    def encode_vfd(self):
        """变频器遥测：含转速（按额定点由输出频率换算）。"""
        bson = self._bson
        rpm = self.vfd_out_hz / VFD_RATED_HZ * VFD_RATED_RPM
        doc = {
            KEY_VALID_MASK: 0xFFFFFFFF,
            KEY_SIDE: 0,
            # runState 口径（与客户端 RUN_STATE_TEXT / 服务端 RUN_* 一致）：
            #   1=停机  2=就绪  3=正转运行  4=反转运行  5/6=正转/反转点动
            # [别写反] 曾经写成 `1 if running and not reverse else (2 if
            # running else 3)` —— 三元分支完全反了：运行给"停机"、停止给
            # "正转运行"。界面上表现为"停着却显示正转"，很容易被当成
            # 服务端状态字解错。
            "runState": self._run_state(),
            "running": bool(self.vfd_running),
            "faulted": False,
            "reverse": bool(self.vfd_reverse),
            "jogActive": False,
            "remote": True,
            "setFreqHz": float(self.vfd_set_hz),
            "outputFreqHz": float(self.vfd_out_hz),
            "outputVoltageV": 380.0 if self.vfd_running else 0.0,
            "outputCurrentA": 3.2 if self.vfd_running else 0.0,
            "busVoltageV": 553.2,
            "outputPowerKw": 1.5 if self.vfd_running else 0.0,
            "torquePercent": 42.0 if self.vfd_running else 0.0,
            "speedRpm": float(rpm),
            "faultCode": 0,
            "ratedRpm": VFD_RATED_RPM,
            "ratedHz": VFD_RATED_HZ,
        }
        return bson.encode(doc)

    def encode_flowmeter(self):
        """电磁流量计快照（LD 系列 · Modbus-RTU）。

        键名与客户端 `FlowMeterState.apply` 逐项对应：
            flow / speed / forwardTotal / reverseTotal /
            flowUnit / totalUnit / alarms / valid / staleMs

        单位用**代码**而非字符串（0x1020 瞬时流量单位 / 0x1021 累积单位），
        客户端自己查表转文本 —— 与 C 端口径一致。

        [为什么要加轻微波动] 恒定值看不出"数据在刷新"，容易误判成界面卡住。
        流量按正弦小幅波动（±2%），累积量随之单调累加。
        """
        bson = self._bson
        # 以运行时间为相位做缓慢波动：±2%，周期约 60s。
        phase = (time.time() % 60.0) / 60.0 * 2.0 * 3.141592653589793
        base_flow = 36.5                       # m³/h（DN50 量程中点）
        flow = base_flow * (1.0 + 0.02 * math.sin(phase))
        # 瞬时流速：v = Q / (3600 × π × (D/2)²)，D = 0.05m（DN50）
        area = 3.141592653589793 * (0.05 / 2.0) ** 2
        speed = (flow / 3600.0) / area
        self.flow_forward_total += flow / 3600.0 * 0.2   # 每拍 200ms 累加
        doc = {
            KEY_SIDE: 0,
            "flow": round(flow, 4),
            "speed": round(speed, 4),
            "forwardTotal": round(self.flow_forward_total, 3),
            "reverseTotal": round(self.flow_reverse_total, 3),
            # 单位代码：**两张表各用各的码**
            #   瞬时流量 0x1020：5 = m³/h
            #   累积总量 0x1021：1 = m³  ← 别拿瞬时表的码填这里！
            #   曾填 10（想在瞬时表里表达 m³/h 的邻居码），客户端按累积表
            #   查到 "t/min" 之外的未知码 → 单位显示 "?"。
            "flowUnit": 5,
            "totalUnit": 1,
            "alarms": 0,
            "valid": True,
            KEY_STALE_MS: 5000,
        }
        return bson.encode(doc)

    def encode_temphum(self):
        return self._bson.encode({
            KEY_SIDE: 0,
            "temp": 265,
            "hum": 550,
            "tempValid": True,
            "humValid": True,
            KEY_STALE_MS: 5000,
        })

    def encode_turbidity(self):
        """浊度仪快照（两台 · 共用 485，站号 1 / 3）。

        键名与客户端 `TurbidityState.apply` 逐项对应：
            count / v{i}_name / v{i}_online / v{i}_ntu / v{i}_raw / staleMs

        `ntu` 是 **×100 的整数**（1285 = 12.85 NTU），与服务端同一口径 ——
        客户端 ÷100 显示两位小数。

        [为什么要带 raw] 换算口径一旦不对（真机寄存器定义不同），raw 是唯一
        能反推真相的量。这里 raw 与 ntu×100 同值，因为模拟的是"32 位整数
        ÷100"那套口径。

        [为什么两台要错开相位] 两条曲线完全重合时看不出是两台独立仪器，
        会让人以为同一个值贴了两遍。
        """
        bson = self._bson
        doc = {KEY_COUNT: FAKE_TURB_COUNT}
        # 周期 45s、±3%，两台相位差 180°
        phase = (time.time() % 45.0) / 45.0 * 2.0 * math.pi
        for i in range(FAKE_TURB_COUNT):
            p = "v%d_" % i
            base = FAKE_TURB_BASE[i]
            wobble = 1.0 + 0.03 * math.sin(phase + math.pi * i)
            raw = int(round(base * wobble))
            doc[p + "name"] = "浊度仪%d" % (i + 1)
            doc[p + "online"] = True
            doc[p + "ntu"] = raw
            doc[p + "raw"] = raw
        doc[KEY_STALE_MS] = 5000
        return bson.encode(doc)

    def encode_pumps(self, ok=None):
        """进水泵快照：**由 DO 位推导输出状态**（没有反馈，只有回显）。

        :param ok: 非 None 时附带 `ok` 键（控制应答要用）。

        [只有输出、没有反馈] 现场泵只控接触器，没有任何运行反馈点。所以
        这里的 `v0_on` 是 **DO 回显**，不是"泵确实在转"。界面必须写明这点
        （客户端水质页的泵卡带固定说明行）。

        [为什么不像阀那样建模行程] 泵没有到位开关，没有"动作中"这个中间态，
        也没有"未到位"可判 —— 硬造就是撒谎。写下去 = 命令状态。
        """
        bson = self._bson
        on = bool(self.digital_do & (1 << FAKE_PUMP_DO))
        doc = {
            KEY_COUNT: 1,
            "v0_name": "进水泵",
            "v0_on": on,
            "v0_ready": True,
            "v0_valid": True,
            "v0_text": "",
            KEY_STALE_MS: 5000,
        }
        if ok is not None:
            doc["ok"] = bool(ok)
        return bson.encode(doc)

    def encode_valves(self, ok=None):
        """阀快照：**由 DO 位推导到位反馈**，让点开阀能看到状态变化。

        :param ok: 非 None 时附带 `ok` 键 —— 控制应答要用（客户端先看
                   `doc.get("ok")` 再决定是否 _apply_doc）。
                   周期推送不带 `ok`（那是状态帧，不是应答）。

        [为什么模拟器要给反馈] 现场真机的行程开关还没接线，所以真机会一直
        显示"无到位反馈"。但模拟器的用途是**验证界面链路**：若反馈恒为 0，
        点开阀后卡片除了 openCmd 之外毫无变化，看起来像"点了没反应"。
        故这里按 DO 位**推导**反馈：
            DO=1 → 开到位（state=Open）
            DO=0 → 关到位（state=Closed）
        这是"阀动作完成后的理想状态"，能直观验证状态机与卡片刷新。

        [与真机的差异] 真机在开关未接线时反馈恒 0、状态"未知"，这是**正确
        行为**（不是 bug）。模拟器刻意不模仿这个缺陷，否则验证不出 UI 链路。
        """
        bson = self._bson
        doc = {KEY_COUNT: FAKE_VALVE_COUNT}
        if ok is not None:
            doc["ok"] = bool(ok)
        for i in range(FAKE_VALVE_COUNT):
            p = "v%d_" % i
            cmd_on = bool(self.digital_do & (1 << (16 + i)))
            doc[p + "name"] = "电磁阀%d" % (i + 1)
            doc[p + "openCmd"] = cmd_on
            # 反馈由 DO 位推导（见 docstring 说明）
            doc[p + "openFb"] = cmd_on
            doc[p + "closeFb"] = not cmd_on
            # 状态：开到位=2、关到位=1（HsfValveState）
            doc[p + "state"] = 2 if cmd_on else 1
            doc[p + "ready"] = True
            doc[p + "valid"] = True
            doc[p + "text"] = ""          # 正常到位，无异常说明
        return bson.encode(doc)

    def encode_health(self):
        bson = self._bson
        entries = [
            # 设备名按现场工艺叫"搅拌泵"，类别仍是 VFD（与真实服务端同口径）
            ("搅拌泵", KIND_VFD, HEALTH_OK, ""),
            ("温湿度", KIND_TEMPHUM, HEALTH_OK, ""),
            ("模拟量模块", KIND_ANALOG_IO, HEALTH_OK, ""),
            ("数字量", KIND_DIGITAL_IO, HEALTH_OK, ""),
            ("电磁阀组", KIND_VALVE, HEALTH_STALE, "无有效反馈（模拟）"),
        ]
        doc = {KEY_DEV_COUNT: len(entries), KEY_DEV_SEQ: 1,
               KEY_DEV_TS: int(time.time() * 1000)}
        for i, (name, kind, health, text) in enumerate(entries):
            doc["d%d_name" % i] = name
            doc["d%d_kind" % i] = kind
            doc["d%d_side" % i] = 0
            doc["d%d_health" % i] = health
            doc["d%d_error" % i] = 0
            doc["d%d_online" % i] = health != HEALTH_OFFLINE
            doc["d%d_runState" % i] = 0
            if text:
                doc["d%d_text" % i] = text
        return bson.encode(doc)

    def encode_topic(self, topic_id, full=False):
        """主题 → (payload, dataType)。未支持的主题返回空 BSON。

        @param full True=全量帧（带通道名等"一次下发"字段）；
                    False=周期增量帧（只带状态/数值，省带宽）。
        @note  [为什么必须区分] C 端只在 full=True 时解析名字。原来三条路径
               全都只发增量帧，于是"点画出来了但没名字"。与 C 端服务端
               hsfAppEncodeXxx(bool full) 同口径：
                 首连补发 / 订阅补发 / 快照应答 → full=True
                 周期推送                        → full=False
        """
        if topic_id == TOPIC_DIGITAL_STATUS:
            return self.encode_digital(full), DATATYPE_BSON
        if topic_id == TOPIC_AI_STATUS:
            return self.encode_analog(), DATATYPE_BSON
        if topic_id == TOPIC_VFD_STATUS:
            return self.encode_vfd(), DATATYPE_BSON
        if topic_id == TOPIC_TEMPHUM_STATUS:
            return self.encode_temphum(), DATATYPE_BSON
        if topic_id == TOPIC_FLOWMETER_STATUS:
            return self.encode_flowmeter(), DATATYPE_BSON
        if topic_id == TOPIC_VALVE_STATUS:
            return self.encode_valves(), DATATYPE_BSON
        if topic_id == TOPIC_TURBIDITY_STATUS:
            return self.encode_turbidity(), DATATYPE_BSON
        if topic_id == TOPIC_PUMP_STATUS:
            return self.encode_pumps(), DATATYPE_BSON
        if topic_id == TOPIC_DEVICE_HEALTH:
            return self.encode_health(), DATATYPE_BSON
        return self._bson.encode({}), DATATYPE_BSON

    # ---- 命令处理 ----
    #: 快照类命令 → 对应主题（应答载荷就是该主题的遥测帧）。
    SNAPSHOT_TOPICS = {
        CMD_DIGITAL_SNAPSHOT: TOPIC_DIGITAL_STATUS,
        CMD_AI_SNAPSHOT: TOPIC_AI_STATUS,
        CMD_VALVE_SNAPSHOT: TOPIC_VALVE_STATUS,
        CMD_TEMPHUM_SNAPSHOT: TOPIC_TEMPHUM_STATUS,
        CMD_VFD_SNAPSHOT: TOPIC_VFD_STATUS,
        CMD_FLOWMETER_SNAPSHOT: TOPIC_FLOWMETER_STATUS,
        CMD_TURBIDITY_SNAPSHOT: TOPIC_TURBIDITY_STATUS,
        CMD_PUMP_SNAPSHOT: TOPIC_PUMP_STATUS,
    }

    def handle_command(self, cmd, doc):
        """返回 (ok, resp_doc_or_payload_bytes)。

        快照类：返回**已编码的遥测帧字节**（与周期推送同构，客户端拿到的
        就是同一份快照）；控制类返回 dict（会被编码成应答包络）。
        """
        topic = self.SNAPSHOT_TOPICS.get(cmd)
        if topic is not None:
            # 快照＝全量（客户端"首连拉一次全量"就靠这条拿名字/点数）。
            payload, _dtype = self.encode_topic(topic, full=True)
            return True, payload
        if cmd == CMD_VFD_CONTROL:
            return self._vfd_control(doc)
        if cmd == CMD_DIGITAL_CONTROL:
            return self._digital_control(doc)
        if cmd == CMD_VALVE_CONTROL:
            # 阀没有真实行程开关，改 DO 后状态仍为"无到位反馈"——
            # 与现场一致（用户尚未接线）。
            idx = int(doc.get("index", 0) or 0)
            if not 0 <= idx < FAKE_VALVE_COUNT:
                return False, {"ok": False, "err": "阀号越界（0~5）"}
            ch = 16 + idx
            on = bool(doc.get("on", False))
            if on:
                self.digital_do |= (1 << ch)
            else:
                self.digital_do &= ~(1 << ch)
            # [应答必须是**完整快照**，不能只回 {ok,index,on}]
            # 客户端 `_apply_doc()` 读的是 count / v{i}_* 前缀键；只回
            # {ok,index,on} 时 count 缺失 → 客户端把 self._items 清空 →
            # 点一次"开阀"界面反而全变空白。服务端真实实现也是回
            # `valve_bank.to_doc()` 全量快照（见 app._h_valve_control）。
            return True, self._bson.decode(self.encode_valves(ok=True))
        if cmd == CMD_RUN_MODE:
            return False, {"ok": False, "err": "模拟器不支持运行模式切换"}
        if cmd == CMD_PUMP_CONTROL:
            # [逻辑在服务端] 幂等由服务端判；模拟器只模仿同一套外部可见行为：
            # 越界拒绝、已是该状态仍回 ok（不重复写）、否则翻 DO 位。
            idx = int(doc.get("index", 0) or 0)
            if idx != 0:
                return False, {"ok": False, "err": "泵号越界（0~0）"}
            on = bool(doc.get("on", False))
            if on:
                self.digital_do |= (1 << FAKE_PUMP_DO)
            else:
                self.digital_do &= ~(1 << FAKE_PUMP_DO)
            # 应答带写后快照（客户端拿它直接刷新，不等下一拍推送）
            return True, self._bson.decode(self.encode_pumps(ok=True))
        return False, {"ok": False, "err": "未知命令 0x%04X" % cmd}

    def _digital_control(self, doc):
        """DO 控制：单点 `{dev,do,on}` 或整组 `{dev,mask|values}`。

        应答格式与服务端一致：`{"ok":true,"dev":N,"do":<写后位图>}` ——
        客户端拿 `do` 直接覆盖本地位图（不等下一拍推送），所以**必须回
        写后的实际位图**，否则按钮会闪一下又跳回旧态。
        """
        dev = int(doc.get("dev", 0) or 0)
        if dev != 0:
            return False, {"ok": False, "err": "模拟器只有模块 0"}
        channels = 32
        if "mask" in doc or "values" in doc:
            mask = doc.get("mask", 0)
            if "values" in doc and not isinstance(mask, int):
                mask = 0
            self.digital_do = int(mask) & ((1 << channels) - 1)
        elif "do" in doc:
            ch = int(doc.get("do", 0) or 0)
            if not 0 <= ch < channels:
                return False, {"ok": False,
                               "err": "DO 通道号越界（0~%d）" % (channels - 1)}
            if bool(doc.get("on", False)):
                self.digital_do |= (1 << ch)
            else:
                self.digital_do &= ~(1 << ch)
        else:
            return False, {"ok": False, "err": "控制载荷缺 do 或 mask 字段"}
        return True, {"ok": True, "dev": dev, "do": self.digital_do}

    def _vfd_control(self, doc):
        op = int(doc.get("op", 0) or 0)
        rev = bool(doc.get("rev", False))
        if op == OP_START:
            self.vfd_running = True
            self.vfd_reverse = rev
            self.vfd_jog = False          # 正式启动 → 不再是点动态
            self.vfd_out_hz = self.vfd_set_hz
            return True, {"ok": True}
        if op in (OP_STOP, OP_FREE_STOP, OP_QUICK_STOP, OP_JOG_RELEASE):
            self.vfd_running = False
            self.vfd_jog = False
            self.vfd_out_hz = 0.0
            return True, {"ok": True}
        if op == OP_JOG:
            self.vfd_running = True
            self.vfd_reverse = rev
            self.vfd_jog = True
            # [点动也按给定值] 原来固定 5Hz 是错的假设：现场点动用的是
            # 同一个频率/转速给定（P0.xx 点动频率另配的除外，本模型不模拟）。
            # 启动与点动都跟 set_hz 走，界面上的"给定值"才是唯一口径。
            self.vfd_out_hz = self.vfd_set_hz
            return True, {"ok": True}
        if op == OP_RESET_FAULT:
            return True, {"ok": True}
        if op == OP_SET_FREQUENCY:
            hz = float(doc.get("hz", 0) or 0)
            self.vfd_set_hz = hz
            if self.vfd_running:
                self.vfd_out_hz = hz
            return True, {"ok": True, "hz": hz}
        if op == OP_SET_SPEED:
            rpm = float(doc.get("rpm", 0) or 0)
            if rpm <= 0:
                return False, {"ok": False, "err": "转速给定值非法"}
            hz = rpm / VFD_RATED_RPM * VFD_RATED_HZ
            self.vfd_set_hz = hz
            if self.vfd_running:
                self.vfd_out_hz = hz
            return True, {"ok": True, "hz": hz, "rpm": rpm}
        return False, {"ok": False, "err": "不支持的动作 op=%d" % op}


# ============================================================================
#                              服务端主流程
# ============================================================================

def make_users(auth_mod):
    """内存账户库（root/root + admin/admin），不落盘。"""
    store = auth_mod.UserStore(path="", log=lambda m: None)
    store.create_defaults()
    return store


def check_constants():
    """校验模拟器自带常量与服务端 proto.py 逐项一致（--check 用）。

    [为什么需要] 常量是两份定义，漂移的后果是"某字段客户端收不到"，
    排查起来要翻协议层。这里逐项比对，不一致立刻报错。
    """
    try:
        if SERVER_PY not in sys.path:
            sys.path.insert(0, SERVER_PY)
        from hsfsrv import proto
    except Exception as exc:                        # noqa: BLE001
        print("跳过校验（导入服务端 proto 失败：%s）" % exc)
        return 0
    pairs = [
        ("TOPIC_VFD_STATUS", TOPIC_VFD_STATUS),
        ("TOPIC_AI_STATUS", TOPIC_AI_STATUS),
        ("TOPIC_TEMPHUM_STATUS", TOPIC_TEMPHUM_STATUS),
        ("TOPIC_DEVICE_HEALTH", TOPIC_DEVICE_HEALTH),
        ("TOPIC_DIGITAL_STATUS", TOPIC_DIGITAL_STATUS),
        ("TOPIC_VALVE_STATUS", TOPIC_VALVE_STATUS),
        ("CMD_DIGITAL_SNAPSHOT", CMD_DIGITAL_SNAPSHOT),
        ("CMD_DIGITAL_CONTROL", CMD_DIGITAL_CONTROL),
        ("CMD_VALVE_SNAPSHOT", CMD_VALVE_SNAPSHOT),
        ("CMD_VALVE_CONTROL", CMD_VALVE_CONTROL),
        ("CMD_VFD_CONTROL", CMD_VFD_CONTROL),
        ("CMD_VFD_SNAPSHOT", CMD_VFD_SNAPSHOT),
        ("CMD_AI_SNAPSHOT", CMD_AI_SNAPSHOT),
        ("CMD_TEMPHUM_SNAPSHOT", CMD_TEMPHUM_SNAPSHOT),
        ("CMD_RUN_MODE", CMD_RUN_MODE),
        ("CMD_FLOWMETER_SNAPSHOT", CMD_FLOWMETER_SNAPSHOT),
        ("TOPIC_FLOWMETER_STATUS", TOPIC_FLOWMETER_STATUS),
        ("CMD_TURBIDITY_SNAPSHOT", CMD_TURBIDITY_SNAPSHOT),
        ("CMD_PUMP_SNAPSHOT", CMD_PUMP_SNAPSHOT),
        ("CMD_PUMP_CONTROL", CMD_PUMP_CONTROL),
        ("TOPIC_TURBIDITY_STATUS", TOPIC_TURBIDITY_STATUS),
        ("TOPIC_PUMP_STATUS", TOPIC_PUMP_STATUS),
        ("OP_START", OP_START),
        ("OP_STOP", OP_STOP),
        ("OP_JOG", OP_JOG),
        ("OP_SET_FREQUENCY", OP_SET_FREQUENCY),
        ("OP_SET_SPEED", OP_SET_SPEED),
        ("KIND_VFD", KIND_VFD),
        ("KIND_TEMPHUM", KIND_TEMPHUM),
        ("KIND_ANALOG_IO", KIND_ANALOG_IO),
        ("KIND_DIGITAL_IO", KIND_DIGITAL_IO),
        ("KIND_VALVE", KIND_VALVE),
        ("HEALTH_OK", HEALTH_OK),
        ("HEALTH_OFFLINE", HEALTH_OFFLINE),
        ("KEY_DI_NAME_FMT", KEY_DI_NAME_FMT),
        ("KEY_DO_NAME_FMT", KEY_DO_NAME_FMT),
        ("KEY_DEV_COUNT", KEY_DEV_COUNT),
        # [2026-11-05] 漏发过这个键：C 端只在 full 帧解析名字，模拟器不发给
        # 出的现象是"点有了但没名字"。纳入校验防再次漂移。
        ("KEY_FULL", KEY_FULL),
    ]
    bad = []
    for name, mine in pairs:
        theirs = getattr(proto, name, "<缺失>")
        if theirs != mine:
            bad.append((name, mine, theirs))
    fields_mine = tuple(VFD_TELEMETRY_FIELDS)
    fields_theirs = tuple(getattr(proto, "VFD_TELEMETRY_FIELDS", ()))
    if fields_theirs and fields_theirs != fields_mine:
        bad.append(("VFD_TELEMETRY_FIELDS", "本文件", "服务端"))

    # ---- 接线口径的**自洽**校验（不是与 proto 比对，是模拟器内部算术）----
    #
    # [2026-11-05 实锅] 泵的 DO 一度写成 `6 + FAKE_VALVE_COUNT` = DO12 ——
    # 漏了阀区基址 16。后果：泵开关点了界面有反应，但 `.12` 的 DO22 不动、
    # 数字量页也查不到名字。这类"差一个偏移"的错，跟 proto.py 逐项比对查不
    # 出来，因为它根本不在协议常量里，而是现场接线知识。
    wiring_bad = []
    if FAKE_PUMP_DO != 16 + FAKE_VALVE_COUNT:
        wiring_bad.append("泵 DO=%d（应为 16+%d=22；阀区基址是聚合偏移 16）"
                          % (FAKE_PUMP_DO, FAKE_VALVE_COUNT))
    if FAKE_PUMP_DO in range(16, 16 + FAKE_VALVE_COUNT):
        wiring_bad.append("泵 DO=%d 与电磁阀的 DO16..21 撞口" % FAKE_PUMP_DO)

    if bad:
        print("!! 常量与服务端 proto.py 不一致：")
        for name, mine, theirs in bad:
            print("   %-24s 模拟器=%-14r 服务端=%r" % (name, mine, theirs))
        return 2
    if wiring_bad:
        print("!! 接线口径自洽校验失败：")
        for line in wiring_bad:
            print("   " + line)
        return 3
    print("常量校验通过（%d 项 + 遥测字段表一致 + 接线口径自洽）" % len(pairs))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="HSF 设备模拟器（假服务端；C 端客户端也可直接连）")
    ap.add_argument("--port", type=int, default=6288,
                    help="监听端口（缺省 6288；模拟 C 版服务端用 6100）")
    ap.add_argument("--period-ms", type=int, default=200,
                    help="遥测推送周期（毫秒）")
    ap.add_argument("--no-shot", action="store_true",
                    help="只起模拟服务端，不起客户端")
    ap.add_argument("--show", action="store_true",
                    help="同时启动 Python 客户端并显示真实窗口")
    ap.add_argument("--page", type=int, default=None, help="起始页索引")
    ap.add_argument("--page-name", default=None, metavar="NAME",
                    help="按名字选页：home/digital/analog/vfd/valve…")
    ap.add_argument("--out", default=None, help="离屏截图输出路径")
    ap.add_argument("--check", action="store_true",
                    help="只校验协议常量与服务端一致后退出")
    args = ap.parse_args(argv)

    if args.check:
        return check_constants()

    import asyncio

    auth_mod, bson_mod, xlink_mod = _load_protocol_modules()
    biz = FakeBusiness(bson_mod)

    server = xlink_mod.Server(args.port, make_users(auth_mod),
                              log=lambda m: print("  [srv] %s" % m),
                              require_auth=True)

    # [三处钩子都是必需的] 缺任一处都会在"登录成功后"抛异常把会话打死，
    # 表现统一为"登录成功但立刻断开、一直接不到数据"：
    #   on_ready        —— 登录成功后无条件调用
    #   dispatch        —— 收到业务 Request 时无条件调用
    #   on_publish_now  —— 订阅补发；必须对**所有**主题返回二元组，
    #                      返回 None 会在 `payload, dtype = await ...` 处
    #                      抛 cannot unpack non-iterable NoneType
    async def _on_ready(session):
        # 首连补发＝全量（带名字）；否则客户端永远只收到增量帧、名字恒空。
        for topic in PUSH_TOPICS:
            payload, dtype = biz.encode_topic(topic, full=True)
            await session.send_publish(topic, payload, dtype)

    async def _dispatch(session, rid, mcode, dtype, payload):
        try:
            doc = bson_mod.decode(payload) if payload else {}
        except ValueError:
            doc = {}
        ok, resp = biz.handle_command(mcode, doc)
        # 快照类直接回已编码的遥测帧；控制类回 dict 包络。
        out = resp if isinstance(resp, (bytes, bytearray)) \
            else bson_mod.encode(resp if isinstance(resp, dict)
                                 else {"ok": bool(ok)})
        await session.send_response(rid, mcode, out)

    server.on_ready = _on_ready
    server.dispatch = _dispatch
    server.on_publish_now = lambda session, topic: biz.encode_topic(topic, full=True)

    loop = asyncio.new_event_loop()
    stop_event = None
    started = threading.Event()

    async def _serve():
        nonlocal stop_event
        stop_event = asyncio.Event()
        await server.start()
        started.set()
        print("模拟服务端就绪：0.0.0.0:%d（帧周期 %dms）"
              % (args.port, args.period_ms))
        topics = PUSH_TOPICS
        period = max(20, args.period_ms) / 1000.0
        while not stop_event.is_set():
            for topic in topics:
                payload, dtype = biz.encode_topic(topic)
                await server.publish(topic, payload, dtype)
            biz.pushed += 1
            try:
                await asyncio.wait_for(stop_event.wait(), period)
            except asyncio.TimeoutError:
                pass
        await server.stop()

    def _thread():
        asyncio.set_event_loop(loop)
        loop.run_until_complete(_serve())

    threading.Thread(target=_thread, daemon=True).start()
    started.wait(6.0)

    if args.no_shot and not args.show:
        print("前台保持中，Ctrl+C 退出")
        try:
            while True:
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        return 0

    # ---- 客户端（可选）----
    return _run_client(args, loop, stop_event)


def _run_client(args, loop, stop_event):
    """启动 Python 客户端（离屏截图 / --show 实窗）。"""
    if not args.show:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    if CLIENT_PY not in sys.path:
        sys.path.insert(0, CLIENT_PY)

    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import theme
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    app = QApplication(sys.argv[:1])
    theme.install(app)
    theme.apply_font(app)
    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.resize(1180, 900)
    win.show()
    ok, err = bridge.connect_to("127.0.0.1", args.port, "root", "root")
    print("客户端登录：%s %s" % (ok, err))

    page_index = args.page if args.page is not None else reg.PAGE_MONITOR_DIGITAL
    if args.page_name:
        alias = {"home": 0, "menu": 1, "digital": 3, "analog": 4,
                 "vfd": 5, "flowmeter": 6, "valve": 7}
        want = args.page_name.strip().lower()
        if want in alias:
            page_index = alias[want]
        else:
            names = list(getattr(reg, "PAGE_NAMES", []))
            hits = [i for i, n in enumerate(names) if n == args.page_name]
            if hits:
                page_index = hits[0]
    win.goto_page(page_index)
    for _ in range(40):
        app.processEvents()
        time.sleep(0.05)

    if args.show:
        print("窗口已打开（页 %s）。关窗或 Ctrl+C 结束。"
              % getattr(reg, "PAGE_NAMES", ["?"] * 20)[page_index])
        try:
            code = app.exec_()
        except KeyboardInterrupt:
            code = 0
        bridge.disconnect_from()
        if stop_event is not None:
            loop.call_soon_threadsafe(stop_event.set)
        return code

    from PyQt5.QtGui import QPixmap
    page = win.pages[page_index]
    if hasattr(page, "refresh"):
        page.refresh()
        app.processEvents()
    out = args.out or os.path.join(ROOT, "..", "verify", "digital_io",
                                   "shots", "sim_%d.png" % page_index)
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pix = QPixmap(page.size())
    page.render(pix)
    pix.save(out)
    print("截图：%s (%dx%d)" % (out, pix.width(), pix.height()))
    bridge.disconnect_from()
    if stop_event is not None:
        loop.call_soon_threadsafe(stop_event.set)
    time.sleep(0.2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
