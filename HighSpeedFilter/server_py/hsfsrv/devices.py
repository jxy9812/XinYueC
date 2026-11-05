# -*- coding: utf-8 -*-
"""现场设备驱动：变频器 / 温湿度 / 模拟量 / 数字量（16DI+16DO）。

每个类只做三件事：**在总线上取数 → 解成工程值 → 给健康判定提供依据**。
协议编码（BSON/键名）在 app.py，总线收发在 modbus.py，本层不碰。

寄存器口径全部来自实测文档（docs/ 下三份探测报告 + 项目记忆），每条易错
的地方都在注释里写明依据，改之前先看依据。
"""

import struct

from . import modbus
from . import proto


# ============================================================================
#                     "请求即最新"：现读设备 + 节流
# ============================================================================

#: 快照请求触发现读的最小间隔（毫秒）。
#:
#: [为什么需要节流] 客户端点"请求快照"、或页面刚打开时都会发快照请求。
#: 若每个请求都立刻打一帧 Modbus，多个客户端同时点就会把总线打爆
#: （数字量模块 30ms/帧、串口 400ms 超时窗口，并发后果更糟）。
#: 100ms 内已读过就直接复用 —— 对"实时"没有可感知影响（人眼阈值 ~200ms），
#: 但能挡掉连续点击与多客户端叠加。
FRESH_READ_MIN_INTERVAL_MS = 100


async def read_fresh(device, now_ms, min_interval_ms=FRESH_READ_MIN_INTERVAL_MS):
    """让设备**现读一次**总线，但同一拍内不重复读。

    与后台轮询的关系：后台 `_poll_loop` 按自己的节拍刷缓存；本函数是
    "客户端明确要最新值"时额外补一帧，**不改轮询节拍**。

    :returns: (ok: bool, err: str) —— ok=True 表示数据是刚刚读的或足够新。
    """
    last = getattr(device, "last_ok_ms", 0)
    if last and (now_ms - last) < min_interval_ms:
        return True, ""                 # 刚刚读过，复用
    try:
        await device.poll(now_ms)
        return True, ""
    except Exception as exc:                            # noqa: BLE001
        # 现读失败**不回退缓存**：调用方据此回 ok=false，让用户知道
        # "这一拍读不到"，而不是拿旧数据冒充实时。
        return False, str(exc)


# ============================================================================
#                          变频器：M512/M513 系列
# ============================================================================

class VfdM512(object):
    """M512/M513 变频器（Modbus-RTU 从站）。

    [为什么读 5 段而不是一次连读] 本机地址天然分散且**跨组不能一帧读**：
    0x1000（通讯设定值）与 0x1001~0x1020（运行参数）地址相邻但分属手册的
    两条条目，一帧同时读会被静默返回全 0。故按组切段，每段一帧。

    [0x1000 起连读的坑] 实测「从 0x1001 起连读会被静默返回全 0」，
    必须把 0x1000 一并纳入读窗（count+1）才能取到有效值。

    [单位标定 —— 逐点实机标定，勿按手册推]
        0x1001 运行频率 = 0.01Hz（÷100）  ← 与 U0.00（÷10）**不是同一口径**
        0x1002 母线电压 = 0.1V  (÷10)
        0x1003 输出电压 = 1V    (÷1)
        0x1004 输出电流 = 0.01A (÷100)
        0x1005 输出功率 = 0.1kW (÷10)
        0x1006 输出转矩 = 0.1%  (÷10)
        0x1000 频率给定 = ±10000 = ±100.00%（相对最大频率 P0.10 的百分比，
                int16 补码 —— 无符号化会把反转的负给定钳成 0，静默失效）
    """

    #: 读组（起始地址, 寄存器数）——按手册条目边界切分。
    READ_GROUPS = (
        (0x1000, 9),      # 1000~1008 给定/频率/母线/电压/电流/功率/转矩/DI/DO
        (0x1009, 3),      # 1009~100B AI1~AI3
        (0x1010, 4),      # 1010~1013 PID设定/反馈/PLC阶段/脉冲频率
        (0x1019, 3),      # 1019~101B 线速度/上电时间/运行时间
        (0x101F, 2),      # 101F~1020 主频率X/辅频率Y
    )

    ADDR_STATUS = 0x3000      # 运行状态：1正转 2反转 3停机（**数量必须=1**）
    ADDR_FAULT = 0x8000       # 故障码（**数量必须=1**）
    ADDR_CMD_WORD = 0x2000    # 控制命令字
    ADDR_FREQ_SET = 0x1000    # 通讯设定值（百分比，int16 补码）

    #: 命令字取值（XVfdM512_Cmd*）
    CMD_RUN_FORWARD = 1
    CMD_RUN_REVERSE = 2
    CMD_JOG_FORWARD = 3
    CMD_JOG_REVERSE = 4
    CMD_FREE_STOP = 5
    CMD_DECEL_STOP = 6
    CMD_FAULT_RESET = 7

    #: 最大频率兜底（P0.10 未读到时）。0x1000 的百分比基准。
    DEFAULT_MAX_FREQ_HZ = 50.0
    #: 额定转速（rpm）：铭牌口径。50Hz 下实测约 1440 rpm（4 极电机带滑差）。
    #: [为什么用"额定点"换算而不是极对数公式] 极对数公式算出的是同步转速
    #: （4 极 = 1500 rpm），而异步电机额定点有滑差，实测 1440。
    #: 用额定点做线性比例 (f/f_rated × n_rated) 与实际更贴近，
    #: 且现场只需抄铭牌两个数，不用知道极数。
    DEFAULT_RATED_RPM = 1440.0
    DEFAULT_RATED_HZ = 50.0

    def __init__(self, master_factory, unit_id, log=None):
        """master_factory: 无参可调用对象，返回可用的 master（懒建）。"""
        self._master_factory = master_factory
        self._master = None
        self.unit_id = unit_id
        self._log = log or (lambda msg: None)
        # 状态快照（每拍重算）
        self.raw = {}
        self.valid_mask = 0
        self.run_state = proto.RUN_UNKNOWN
        self.faulted = False
        self.fault_code = 0
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""
        self.max_freq_hz = self.DEFAULT_MAX_FREQ_HZ
        #: 额定点（铭牌）：用于 Hz ↔ rpm 线性换算。
        self.rated_rpm = self.DEFAULT_RATED_RPM
        self.rated_hz = self.DEFAULT_RATED_HZ

    # ---- 转速换算（Hz ↔ rpm）----
    def rpm_of(self, hz):
        """频率(Hz) → 转速(rpm)。按额定点线性比例。

        [口径] rpm = hz / rated_hz × rated_rpm
        50Hz → 1440rpm（现场实测口径）。这是**估算**：变频器本身没有转速
        寄存器，滑差随负载变化，故界面上标明是换算值。
        """
        if hz is None or self.rated_hz <= 0:
            return None
        return float(hz) / self.rated_hz * self.rated_rpm

    def hz_of(self, rpm):
        """转速(rpm) → 频率(Hz)（上式的反解）。"""
        if rpm is None or self.rated_rpm <= 0:
            return None
        return float(rpm) / self.rated_rpm * self.rated_hz

    # ---- 总线 ----
    def _master_get(self):
        if self._master is None:
            self._master = self._master_factory()
        return self._master

    async def close(self):
        if self._master is not None:
            await self._master.close()
            self._master = None

    # ---- 取数 ----
    async def poll(self, now_ms):
        """读一拍全部遥测 + 状态 + 故障码。

        任何一段失败都**不**清空已有数据：设备偶发丢帧时保留上一拍值，
        由陈旧判定（staleMs）负责让上层降级显示 —— 比每丢一帧就清零
        更贴近现场观感（否则数值会闪）。
        """
        master = self._master_get()
        try:
            # [必须逐次传 unit] 变频器与温湿度共享同一个 RtuMaster，
            # 谁是目标从站由这里指定（master 不知道当前在跟谁说话）。
            for addr, count in self.READ_GROUPS:
                vals = await master.read(3, addr, count, unit=self.unit_id)
                for i, val in enumerate(vals):
                    self.raw[addr + i] = val
            status = await master.read(3, self.ADDR_STATUS, 1,
                                       unit=self.unit_id)
            fault = await master.read(3, self.ADDR_FAULT, 1,
                                      unit=self.unit_id)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise

        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""
        self.raw[self.ADDR_STATUS] = status[0]
        self.raw[self.ADDR_FAULT] = fault[0]
        self._decode_state(status[0])
        self._decode_fault(fault[0])
        self._mark_telemetry_valid()
        self.last_ok_ms = now_ms

    def _mark_telemetry_valid(self):
        """把实际读到的遥测位全部置有效。

        [为什么必须显式置位] 有效掩码（vm）是客户端判断「这个键能不能用」的
        唯一依据：未置位的键**不会被写进 BSON**，客户端据掩码显示 "--"。
        状态字解码只置了 0~3/5 位，遥测段（频率/电压/电流…）必须在读到值
        之后补置，否则页面永远显示 "--"。
        """
        for bit, _key, _kind in proto.VFD_TELEMETRY_FIELDS:
            if self.has_field(bit):
                self.valid_mask |= 1 << bit
            else:
                self.valid_mask &= ~(1 << bit)

    def has_field(self, bit):
        """该位对应的寄存器/派生量是否已采到（**不看 valid_mask**，避免自证）。"""
        if bit == 0:
            return self.ADDR_STATUS in self.raw
        if bit in (2, 3, 5):
            return self.ADDR_STATUS in self.raw
        if bit == 8:
            return True               # M512 不单独报点动状态，恒可给出 false
        if bit == 7:
            return True               # 通讯在即可给出远程可控
        if bit == 10:
            return self.ADDR_FREQ_SET in self.raw
        if bit == 11:
            return 0x1001 in self.raw
        if bit == 14:
            return 0x1003 in self.raw
        if bit == 15:
            return 0x1004 in self.raw
        if bit == 16:
            return 0x1002 in self.raw
        if bit == 18:
            return 0x1005 in self.raw
        if bit == 20:
            return 0x1006 in self.raw
        if bit == 21:
            # 转速：由输出频率按额定点换算（变频器无转速寄存器）。
            # 只要读到输出频率就等价值可用 —— 转速是**派生量**，不是原始量。
            return 0x1001 in self.raw
        if bit == 39:
            return self.ADDR_FAULT in self.raw
        return False

    def _decode_state(self, word):
        """运行状态字解码（已含「故障优先」：有故障时状态显示故障）。"""
        self.valid_mask |= 1 << 0            # RunState
        self.valid_mask |= 1 << 1            # RawStateWord
        self.valid_mask |= 1 << 2            # Running
        self.valid_mask |= 1 << 3            # Faulted
        self.valid_mask |= 1 << 5            # Reverse
        if word == 1:
            self.run_state = proto.RUN_RUNNING_FORWARD
            self.raw["_running"] = True
            self.raw["_reverse"] = False
        elif word == 2:
            self.run_state = proto.RUN_RUNNING_REVERSE
            self.raw["_running"] = True
            self.raw["_reverse"] = True
        elif word == 3:
            self.run_state = proto.RUN_STOPPED
            self.raw["_running"] = False
            self.raw["_reverse"] = False
        else:
            self.run_state = proto.RUN_UNKNOWN
            self.raw["_running"] = False
            self.raw["_reverse"] = False
        if self.faulted:
            self.run_state = proto.RUN_FAULT

    def _decode_fault(self, code):
        self.fault_code = code
        self.faulted = code != 0
        self.valid_mask |= 1 << 39           # FaultCode
        if self.faulted:
            self.run_state = proto.RUN_FAULT

    # ---- 工程值（供 BSON 编码） ----
    def _scaled(self, addr, div):
        raw = self.raw.get(addr)
        return None if raw is None else raw / float(div)

    def _signed16(self, addr):
        """0x1000 是 int16 补码：无符号读出来会把 -500 当成 65036。"""
        raw = self.raw.get(addr)
        if raw is None:
            return None
        return struct.unpack("<h", struct.pack("<H", raw & 0xFFFF))[0]

    def output_freq_hz(self):
        return self._scaled(0x1001, 100.0)

    def is_running(self):
        """设备当前是否在运行（**以遥测为准，不看命令回显**）。

        [为什么用遥测而不是"上次发过启动命令"] 命令可能没送达、可能被
        变频器拒绝、可能现场按了面板停止。用状态字（0x3000）解出的
        `run_state` 才是唯一事实。
        状态未读到时按"未运行"处理 —— 宁可拒绝一次调速，也不能在不确定
        的状态下改运行参数。
        """
        return self.run_state in (proto.RUN_RUNNING_FORWARD,
                                  proto.RUN_RUNNING_REVERSE)

    def set_freq_hz(self):
        """设定频率：0x1000 的百分比 × 最大频率。

        [为什么要乘基准] 0x1000 是相对 P0.10 的百分比，不是绝对 Hz。
        基准未读到时按 50Hz 兜底（与 C 侧 XVfdDevice 的相对口径一致）。
        """
        pct = self._signed16(self.ADDR_FREQ_SET)
        if pct is None:
            return None
        return pct / 10000.0 * self.max_freq_hz

    def freq_percent(self):
        return self._signed16(self.ADDR_FREQ_SET)

    def field_value(self, bit, kind):
        """按 XVfdCodec 的遥测键表取某一位的值（None = 该字段无效）。"""
        if not (self.valid_mask & (1 << bit)):
            return None
        if bit == 0:
            return self.run_state
        if bit == 2:
            return bool(self.raw.get("_running", False))
        if bit == 3:
            return bool(self.faulted)
        if bit == 5:
            return bool(self.raw.get("_reverse", False))
        if bit == 8:
            return False              # M512 不单独报点动状态，按未点动处理
        if bit == 7:
            return True               # 通讯链路在 ⇒ 视为远程可控（对齐 C 侧口径）
        if bit == 10:
            return self.set_freq_hz()
        if bit == 11:
            return self.output_freq_hz()
        if bit == 14:
            return self._scaled(0x1003, 1.0)
        if bit == 15:
            return self._scaled(0x1004, 100.0)
        if bit == 16:
            return self._scaled(0x1002, 10.0)
        if bit == 18:
            return self._scaled(0x1005, 10.0)
        if bit == 20:
            return self._scaled(0x1006, 10.0)
        if bit == 21:
            # 转速：由输出频率按额定点换算（50Hz ↔ 1440rpm）。
            # 变频器不提供转速寄存器，滑差随负载变化 —— 这是**估算值**，
            # 界面上要标明，不能当成实测转速。
            hz = self.output_freq_hz()
            return None if hz is None else self.rpm_of(hz)
        if bit == 39:
            return self.fault_code
        return None

    # ---- 控制 ----
    async def set_frequency(self, hz):
        """写频率给定：Hz → 相对最大频率的百分比 → int16 补码。

        超出 ±100% 的值**钳位**而不是拒绝：现场上位机偶尔会给超范围值，
        钳位后至少按满速跑，比整条命令失败更符合预期。
        """
        pct = int(round(hz / self.max_freq_hz * 10000.0))
        pct = max(-10000, min(10000, pct))
        master = self._master_get()
        await master.write_single(6, self.ADDR_FREQ_SET, pct & 0xFFFF,
                                  unit=self.unit_id)
        return True

    async def send_command(self, code):
        master = self._master_get()
        await master.write_single(6, self.ADDR_CMD_WORD, code,
                                  unit=self.unit_id)
        return True

    async def apply_control(self, op, reverse=False, hz=0.0, rpm=0.0,
                            value=0.0, index=0):
        """把控制意图分派到设备（对齐 XVfdCodec_applyControl 的支持子集）。

        返回 (ok, err)：ok=False 时 err 为中文原因。
        """
        if op == proto.OP_START:
            await self.send_command(self.CMD_RUN_REVERSE if reverse
                                    else self.CMD_RUN_FORWARD)
        elif op == proto.OP_STOP:
            await self.send_command(self.CMD_DECEL_STOP)
        elif op == proto.OP_FREE_STOP:
            await self.send_command(self.CMD_FREE_STOP)
        elif op == proto.OP_QUICK_STOP:
            # 本机无独立快停命令字：用减速停机（现场口径，见文档）
            await self.send_command(self.CMD_DECEL_STOP)
        elif op == proto.OP_RESET_FAULT:
            await self.send_command(self.CMD_FAULT_RESET)
        elif op == proto.OP_JOG:
            await self.send_command(self.CMD_JOG_REVERSE if reverse
                                    else self.CMD_JOG_FORWARD)
        elif op == proto.OP_SET_FREQUENCY:
            await self.set_frequency(hz)
        elif op == proto.OP_SET_SPEED:
            if rpm <= 0:
                return False, "转速给定值非法"
            # [转速给定也走频率通道] 变频器本身只接受频率给定（0x1000 是
            # 百分比），故按额定点反解成频率再下发。对现场而言效果一致；
            # 若将来换用支持转速给定的型号，只改这里。
            hz = self.hz_of(rpm)
            if hz is None:
                return False, "转速换算失败（额定点配置非法）"
            if hz > self.max_freq_hz + 0.001:
                return False, ("转速 %.0frpm 超出上限（额定 %.0frpm ≈ %.1fHz）"
                               % (rpm, self.rated_rpm, self.max_freq_hz))
            await self.set_frequency(hz)
        elif op == proto.OP_SET_PID_TARGET:
            return False, "PID 目标给定未在本实现中启用"
        elif op == proto.OP_SELECT_PRESET_SPEED:
            return False, "多段速选择未在本实现中启用"
        else:
            return False, "不支持的控制动作"
        return True, ""

    # ---- 健康 ----
    def health(self, now_ms, stale_ms):
        """健康判定：在线且有数据 → Ok/Stale/Fault。"""
        if not self.online:
            return proto.HEALTH_OFFLINE, 0, False, "从站失联"
        if self.faulted:
            return (proto.HEALTH_FAULT, self.fault_code, True,
                    self.fault_text(self.fault_code) or "变频器故障")
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效遥测"
        return proto.HEALTH_OK, 0, True, ""

    #: 故障码 → 中文（附录1 通信故障表主键；只列常见项，未列出的回空串）。
    FAULT_TEXT = {
        0x0002: "加速过电流", 0x0003: "减速过电流", 0x0004: "恒速过电流",
        0x0005: "加速过电压", 0x0006: "减速过电压", 0x0007: "恒速过电压",
        0x0008: "缓冲电阻过载故障", 0x0009: "欠压故障", 0x000A: "变频器过载",
        0x000B: "电机过载", 0x000C: "输入缺相", 0x000D: "输出缺相",
        0x000E: "模块过热", 0x000F: "外部故障", 0x0010: "通讯异常",
        0x0011: "接触器异常", 0x0012: "电流检测故障", 0x0013: "电机调谐故障",
        0x0014: "编码器/PG 卡故障", 0x0015: "参数读写异常",
        0x0016: "变频器硬件故障", 0x0017: "电机对地短路故障",
        0x001A: "运行时间到达", 0x001E: "堵载", 0x001F: "PID 反馈丢失",
    }

    @classmethod
    def fault_text(cls, code):
        return cls.FAULT_TEXT.get(code, "")


# ============================================================================
#                        温湿度变送器：ZS-SHT30
# ============================================================================

class TempHum(object):
    """中盛 ZS-SHT30-1T-1H 温湿度变送器（Modbus-RTU 从站）。

    [协议约束 —— 全部实测]
      * 只用 FC04 读输入寄存器 0x0000 × 2（温度、湿度各一）；
      * 块读上限 20，写入不支持 FC10（只 FC06）；
      * **非法请求静默丢弃**：永远等不到 0x80 异常码，只能靠超时判失败；
      * 帧内字节间隔 > 1.5ms 整帧丢弃 ⇒ 必须一次 write 出完整帧。

    [数值口径] 两个 uint16，各 ×0.1。
    负温度：raw >= 10000 时 -(raw - 10000) × 0.1（手册 2.1；
    不实现这条的话 -1℃ 会被解成 999.9℃）。
    """

    READ_ADDR = 0x0000
    READ_COUNT = 2
    NEGATIVE_THRESHOLD = 10000

    def __init__(self, master_factory, unit_id, log=None):
        self._master_factory = master_factory
        self._master = None
        self.unit_id = unit_id
        self._log = log or (lambda msg: None)
        self.raw_temp = 0
        self.raw_hum = 0
        self.temp_valid = False
        self.hum_valid = False
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""

    def _master_get(self):
        if self._master is None:
            self._master = self._master_factory()
        return self._master

    async def close(self):
        if self._master is not None:
            await self._master.close()
            self._master = None

    async def poll(self, now_ms):
        master = self._master_get()
        try:
            vals = await master.read(4, self.READ_ADDR, self.READ_COUNT,
                                     unit=self.unit_id)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise
        if len(vals) < 2:
            self.online = False
            self.last_error = "读回寄存器不足"
            raise modbus.ModbusError("温湿度读回寄存器不足：%d" % len(vals))
        self.raw_temp = vals[0]
        self.raw_hum = vals[1]
        self.temp_valid = True
        self.hum_valid = True
        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""

    def temp_tenths(self):
        """温度 ×0.1℃（可负）。"""
        raw = self.raw_temp
        if raw >= self.NEGATIVE_THRESHOLD:
            return -(raw - self.NEGATIVE_THRESHOLD)
        return raw

    def hum_tenths(self):
        """湿度 ×0.1%RH。"""
        return self.raw_hum

    def health(self, now_ms, stale_ms):
        if not self.online:
            return proto.HEALTH_OFFLINE, 0, False, "从站失联"
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        if not (self.temp_valid or self.hum_valid):
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        return proto.HEALTH_OK, 0, True, ""


# ============================================================================
#                     模拟量输入模块（8 路 4~20mA）
# ============================================================================

class AnalogInput(object):
    """中盛 8 路模拟量输入模块（Modbus-TCP 从站，端口 8234）。

    [换算] 寄存器值 = 采样电压毫伏（固定 3 位小数），采样电阻 249Ω：
        I(mA) = mV / 249
    实测 CH1 = 995mV → 3.996mA，正是 4~20mA 量程的活零点（4.0mA），
    这条读数顺带证实了「量程确为 4~20mA」（文档里唯一无法自证的待验证项）。

    [为什么不读保持寄存器] 只需通道值。HR0039=通道数、HR003A=小数点位数
    属诊断信息，轮询里读它们纯属浪费总线。
    """

    ADDR = 0x0000
    SHUNT_OHM = 249.0

    def __init__(self, master, channels=8, log=None):
        self._master = master
        self.channels = channels
        self._log = log or (lambda msg: None)
        self.raw = [0] * channels
        self.valid = [False] * channels
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""

    async def poll(self, now_ms):
        try:
            vals = await self._master.read(4, self.ADDR, self.channels)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise
        for i in range(self.channels):
            if i < len(vals):
                self.raw[i] = vals[i]
                self.valid[i] = True
            else:
                self.valid[i] = False
        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""

    def ma(self, index):
        """第 index 路的电流（mA）。"""
        return self.raw[index] / self.SHUNT_OHM

    def health(self, now_ms, stale_ms):
        if not self.online:
            return proto.HEALTH_OFFLINE, 0, False, "模块失联"
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        if not any(self.valid):
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        return proto.HEALTH_OK, 0, True, ""


# ============================================================================
#                    数字量模块：16DI + 16DO（Modbus-TCP）
# ============================================================================

class DigitalIo(object):
    """AMSAMOTION MT3A-IO1632 数字量模块（DI + DO，Modbus-TCP）。

    [寄存器映射]
        DO = 线圈 0x0000~       FC01 读 / FC05 写单点
        DI = 离散输入 0x0000~   FC02 读
        站号 1，端口 502

    [点数口径 —— 单模块 16，聚合后 32]
    单台模块物理 16 点；但它支持**主从聚合**：一台（ms=1 主站）把另一台
    的 16 点映射进自己的地址空间，对外就是 32 点。本现场即如此：

        .13（被聚合者，16 点）→ 映射进 .12 的 esDoNu1/esDiNu1
        ⇒ 只轮询 .12 就能拿到全部 32DI/32DO

    2026-11-05 实测确认：.12 的 FC02/FC01 读 0x0000 起 32 点返回 4 字节；
    且**上半区是活的** —— 写 DO16/DO20/DO31 后回读逐位正确，恢复后归零。
    故点数**每模块可变**，构造时由配置给（缺省 16，本现场 32）。

    [该模块会间歇性空应答] 实测无间隔连发 10 次里会有 1~2 次收到 0 字节
    （TCP 可连但本次无应答），间隔 300ms 以上就基本不出。故读失败要靠
    重试兜底（见 modbus.TcpMaster 的重试），不能把单次空应答当成设备失联。

    [为什么整组写不用 FC0F] 本模块对 FC0F **会发两次相同应答**，且打包类
    事务号恒 0000 无法用 tid 区分 ⇒ 同连接的下一条请求会把重复帧当响应。
    FC05 无此问题，故整组写 = 逐点 FC05。

    [为什么读 DI/DO 要两帧] DI 走离散输入区、DO 走线圈区，是两个独立功能码，
    无法一帧同时取。两帧都成功才算本拍有效。

    [连接 churn 警告] 本模块对频繁连断敏感（会被打进数分钟不应答状态），
    故 master 必须长连复用（modbus.TcpClient 已是长连 + 按需重连）。

    [⚠ 这个模块可以是「主站」而不是「从站」—— 2026-11-04 实锤]
    MT3A-IO1632 的 `ms` 字段决定角色：

        ms = 1（主站模式）：模块**主动**向 `rip:rport` 推送数据。
                            原以为此时不响应 Modbus 从站请求；但 2026-11-05
                            复测发现 .12 配成 ms=1 后**仍然完整应答** FC01/FC02
                            —— 故"ms=1 不应答"不是绝对规则，接线/固件版本
                            都会影响。诊断时以**实测应答**为准，别只凭配置判死。
        ms = 0（从站模式）：标准 Modbus TCP 从站，本类按上面的映射读写。

    故**"TCP 502 可连但 Modbus 无应答"要先看是不是间歇空帧 + 重试没兜住**，
    其次才怀疑角色配置。
    """

    def __init__(self, master, name, channels=16, publish=True, log=None):
        self._master = master
        self.name = name
        self._log = log or (lambda msg: None)
        #: DI/DO 路数（单模块 16；聚合后 32）。由配置给，不写死。
        self.channels = max(1, int(channels))
        #: 是否参与点位发布。被聚合者（点已并入聚合者的地址空间）置 false：
        #: 它仍然轮询、仍然出健康条目（用于判故障），但不再发一帧点位，
        #: 否则客户端会出现两套重复点位。
        self.publish = bool(publish)
        self.di = [0] * self.channels
        self.do = [0] * self.channels
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""

    async def poll(self, now_ms):
        try:
            di = await self._master.read(2, 0, self.channels)
            do = await self._master.read(1, 0, self.channels)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise
        self.di = list(di)[:self.channels]
        self.do = list(do)[:self.channels]
        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""

    def do_mask(self):
        """DO 状态压成位图（bit n = DO n）。"""
        mask = 0
        for i, val in enumerate(self.do):
            if val:
                mask |= 1 << i
        return mask

    def di_mask(self):
        mask = 0
        for i, val in enumerate(self.di):
            if val:
                mask |= 1 << i
        return mask

    def di_bit(self, channel):
        """读一路 DI（bit n = 通道 n）。

        [为什么必须有这个方法] valves.py 用它取开/关到位反馈。此前本类
        没有该方法而 valves.py 又调了它，异常被外层的 except 吞掉并置
        valid=False —— 表现是**6 路阀永远显示"无有效反馈"**，看着像
        行程开关没接线，实际是接口缺失。故读失败要显式抛，不能静默 0。
        """
        ch = int(channel)
        if not 0 <= ch < len(self.di):
            raise IndexError("DI 通道号越界：%d（点数 %d）" % (ch, len(self.di)))
        return bool(self.di[ch])

    def do_bit(self, channel):
        """读一路 DO 回显（bit n = 通道 n）。越界抛 IndexError。"""
        ch = int(channel)
        if not 0 <= ch < len(self.do):
            raise IndexError("DO 通道号越界：%d（点数 %d）" % (ch, len(self.do)))
        return bool(self.do[ch])

    async def write_do(self, index, on):
        """写单点 DO（FC05）。"""
        if not 0 <= index < self.channels:
            return False, "DO 通道号越界（0~%d）" % (self.channels - 1)
        try:
            await self._master.write_single(5, index, 0xFF00 if on else 0x0000)
        except modbus.ModbusError as exc:
            return False, "写 DO 失败：%s" % exc
        self.do[index] = 1 if on else 0
        return True, ""

    async def write_group(self, values):
        """整组写 DO。

        :param values: 位图 int（bit n = DO n）或长度 == channels 的 0/1 序列
        :returns: (ok, err)
        """
        if isinstance(values, int):
            targets = [(values >> i) & 1 for i in range(self.channels)]
        else:
            targets = [1 if v else 0 for v in values][:self.channels]
            if len(targets) < self.channels:
                targets += [0] * (self.channels - len(targets))
        # 逐点 FC05（见类注释：本模块 FC0F 会重发应答）。
        # 只写**有变化的点**：少发帧、少扰动现场输出。
        failed = []
        for i, target in enumerate(targets):
            if target == self.do[i]:
                continue
            ok, err = await self.write_do(i, bool(target))
            if not ok:
                failed.append("DO%d: %s" % (i, err))
        if failed:
            return False, "；".join(failed[:4])
        return True, ""

    def health(self, now_ms, stale_ms):
        if not self.online:
            # [区分"失联"与"配成主站"] 本模块 TCP 能连却不应答，最常见原因
            # 不是坏了，而是 Web 配置里 ms=1（主站模式）——它主动往上推数据，
            # 不当从站。笼统写"模块失联"会把人往网络/电源方向带偏。
            reason = "模块失联"
            if self.last_error and ("EMPTY" in self.last_error
                                   or "无应答" in self.last_error
                                   or "ConnectionReset" in self.last_error
                                   or "IncompleteRead" in self.last_error):
                reason = "无 Modbus 应答（检查是否配成主站 ms=1）"
            return proto.HEALTH_OFFLINE, 0, False, reason
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        return proto.HEALTH_OK, 0, True, ""


# ============================================================================
#                     电磁流量计（LD 系列，Modbus-RTU）
# ============================================================================

class FlowMeter(object):
    """LD 系列电磁流量计（TRX，即 485；Modbus-RTU 从站）。

    [寄存器口径] 与 C 端 `XFlowMeter.h` 的 22 槽一一对应，变量区
    `0x1010~0x1025` **一帧读完**（FC03，22 个寄存器，未超块读上限 20？
    实测该表允许 22 个连续读；若现场型号上限 20 则改成两段）：

        0x1010/11  Float HI/LO  瞬时流量
        0x1012/13  Float HI/LO  瞬时流速
        0x1014/15  Float HI/LO  流量百分比
        0x1016/17  Float HI/LO  流体电导比
        0x1018/19  Long  HI/LO  正向累积整数部分
        0x101A/1B  Float HI/LO  正向累积小数部分
        0x101C/1D  Long  HI/LO  反向累积整数部分
        0x101E/1F  Float HI/LO  反向累积小数部分
        0x1020     U16          瞬时流量单位
        0x1021     U16          累积总量单位
        0x1022     U16          上限报警
        0x1023     U16          下限报警
        0x1024     U16          空管报警
        0x1025     U16          系统报警

    [参数区不支持连读] 0x1020 之后的**参数区**（量程/口径等）实测不支持
    连续读，故本类只读变量区，参数另走单点读（当前不需要）。

    [32 位量拼装] Float 是"高字在前"的两个 uint16。拼装**必须在设备层
    做**（不能等两个事件），否则读到的高低字可能来自不同帧 —— 累积量在
    跳字的那一瞬会拼出天文数字。故一帧读回来立即在本函数内拼好。
    """

    #: 变量区起始与长度（0x1010 起 22 个寄存器）。
    VAR_BASE = 0x1010
    VAR_COUNT = 22

    def __init__(self, master_factory, unit_id, log=None):
        self._master_factory = master_factory
        self._master = None
        self.unit_id = unit_id
        self._log = log or (lambda msg: None)
        # 工程值（拼装后）
        self.flow = 0.0            #: 瞬时流量（单位见 flow_unit）
        self.speed = 0.0           #: 瞬时流速（m/s）
        self.flow_percent = 0.0    #: 流量百分比
        self.conductivity = 0.0    #: 流体电导比
        self.forward_total = 0.0   #: 正向累积（整数 + 小数）
        self.reverse_total = 0.0   #: 反向累积
        self.flow_unit = 0         #: 瞬时流量单位代码（0x1020）
        self.total_unit = 0        #: 累积总量单位代码（0x1021）
        self.alarm_upper = False
        self.alarm_lower = False
        self.alarm_empty = False
        self.alarm_system = False
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""

    # ---- 连接 ----
    def _master_get(self):
        if self._master is None:
            self._master = self._master_factory()
        return self._master

    async def close(self):
        if self._master is not None:
            await self._master.close()
            self._master = None

    # ---- 32 位量拼装 ----
    @staticmethod
    def _float32(hi, lo):
        """两个 uint16（高字在前）→ float32。

        [为什么放设备层] 一帧读回来的两个字天然同帧；若交给上层分两次
        取值，累积量跳字时会拼出跨帧的垃圾值。
        """
        raw = ((int(hi) & 0xFFFF) << 16) | (int(lo) & 0xFFFF)
        return struct.unpack(">f", struct.pack(">I", raw))[0]

    @staticmethod
    def _long32(hi, lo):
        """两个 uint16（高字在前）→ uint32。"""
        return ((int(hi) & 0xFFFF) << 16) | (int(lo) & 0xFFFF)

    # ---- 采集 ----
    async def poll(self, now_ms):
        master = self._master_get()
        try:
            vals = await master.read(3, self.VAR_BASE, self.VAR_COUNT,
                                     unit=self.unit_id)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise
        if len(vals) < self.VAR_COUNT:
            self.online = False
            self.last_error = "变量区读回不足（%d/%d）" % (len(vals),
                                                          self.VAR_COUNT)
            raise modbus.ModbusError(self.last_error)
        v = vals
        # 32 位量成对拼装（高字在前）
        self.flow = self._float32(v[0], v[1])
        self.speed = self._float32(v[2], v[3])
        self.flow_percent = self._float32(v[4], v[5])
        self.conductivity = self._float32(v[6], v[7])
        fwd_int = self._long32(v[8], v[9])
        fwd_frac = self._float32(v[10], v[11])
        rev_int = self._long32(v[12], v[13])
        rev_frac = self._float32(v[14], v[15])
        # 累积量 = 整数部分 + 小数部分（小数是 0~1 的 Float）
        self.forward_total = float(fwd_int) + float(fwd_frac)
        self.reverse_total = float(rev_int) + float(rev_frac)
        self.flow_unit = int(v[16]) & 0xFFFF
        self.total_unit = int(v[17]) & 0xFFFF
        self.alarm_upper = bool(int(v[18]) & 0xFFFF)
        self.alarm_lower = bool(int(v[19]) & 0xFFFF)
        self.alarm_empty = bool(int(v[20]) & 0xFFFF)
        self.alarm_system = bool(int(v[21]) & 0xFFFF)
        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""

    # ---- 报警位图 ----
    def alarm_mask(self):
        """四项报警压成一个 int（客户端按位解析）。

        位定义与客户端 `FlowMeterState` 一致：0=上限 1=下限 2=空管 3=系统。
        """
        mask = 0
        if self.alarm_upper:
            mask |= 1 << 0
        if self.alarm_lower:
            mask |= 1 << 1
        if self.alarm_empty:
            mask |= 1 << 2
        if self.alarm_system:
            mask |= 1 << 3
        return mask

    def health(self, now_ms, stale_ms):
        if not self.online:
            return proto.HEALTH_OFFLINE, 0, False, "从站失联"
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        if self.alarm_system:
            return proto.HEALTH_WARNING, 0, True, "流量计系统报警"
        return proto.HEALTH_OK, 0, True, ""


# ============================================================================
#                     浊度仪（两台，同一 485 不同站号）
# ============================================================================

class TurbidityMeter(object):
    """浊度仪（Modbus-RTU，WZT-701A 一类）。

    [口径来自真机手册，不再是"市面常见"猜测]
    WZT-701A 说明书 4.3.10.2「测试命令」给出可复现帧：

        发送：01 03 00 00 00 02 C4 0B
        回复：01 03 04 D7 0A 41 1F 92 1D
        原文：按照低字在前高字在后传输(CDAB)，浊度为 2 字 16 进制数
              D7 0A 41 1F 对应的浮点型数据为 9.99NTU

    即：FC03 读 0x0000 起 **2 个寄存器**；两字按**低字在前（CDAB）**拼成
    32 位位模式，再按 **IEEE754 单精度**解释 —— 不除以 100。

        bits = (hi << 16) | lo        # hi = vals[1]，lo = vals[0]
        NTU  = struct.unpack(">f", bits)

    [初版错在哪] 初版按"市面常见"实现成 `raw = (v0<<16)|v1; NTU = raw/100`。
    用上面这帧验算：正确结果 9.99，初版算出 36077734.71 —— 差 6 个数量级，
    而且**不报错**（质量仍 valid），是那种"界面有数、数全错"的故障。
    现场没给资料时这只能靠猜，拿到手册后就必须按手册改。

    [三项口径各自可调] 字序/格式/除数不写成写死的常量：同一现场的另一台表
    完全可能是 ABCD + 整数/100。C 侧 XTurbidityMeter 也是三个运行期旋钮
    （setWordOrder/setWordFormat/setWordDivisor），两边一一对应。

    [为什么要留 raw] 换算口径一旦不对，raw 是唯一能反推真相的量。调试时先
    看 raw 是不是一个像浮点的位模式（比如 0x41 开头 ≈ 10 NTU 量级），再判
    字序/格式哪一项错了。

    [站号] 两台仪器共用一个串口、站号不同（现场手工设定，实测 1 和 3）。
    C 端 `HsfServerConfig.h` 的 HSF_TURBIDITY1/2_UNIT_ID 与之一致。
    """

    #: 寄存器起始地址（手册：0x0000）。
    READ_ADDR = 0x0000
    #: 读取寄存器数（32 位量 = 2 个）。
    READ_COUNT = 2
    #: 功能码：手册明确是 FC03（读保持寄存器），**不是** FC04。
    READ_FUNCTION = 3
    #: 字序：True=低字在前（CDAB，真机口径）；False=高字在前（ABCD，流量计口径）。
    LOW_WORD_FIRST = True
    #: 格式："float32" / "uint32" / "int32"。
    WORD_FORMAT = "float32"
    #: 整数格式下的除数（float32 格式下不参与计算）。
    SCALE_DIVISOR = 100.0

    def __init__(self, master_factory, unit_id, name="", log=None):
        self._master_factory = master_factory
        self._master = None
        self.unit_id = unit_id
        self.name = name or ("浊度仪%d" % unit_id)
        self._log = log or (lambda msg: None)
        self.raw = 0
        self.ntu = 0.0
        self.online = False
        self.last_ok_ms = 0
        self.last_error = ""

    # ---- 连接 ----
    def _master_get(self):
        if self._master is None:
            self._master = self._master_factory()
        return self._master

    async def close(self):
        if self._master is not None:
            await self._master.close()
            self._master = None

    def _decode(self, vals):
        """按实例口径把两个字解成 NTU（留 raw 位模式供诊断）。"""
        v0 = int(vals[0]) & 0xFFFF
        v1 = int(vals[1]) & 0xFFFF
        if self.LOW_WORD_FIRST:
            bits = (v1 << 16) | v0
        else:
            bits = (v0 << 16) | v1
        self.raw = bits
        if self.WORD_FORMAT == "float32":
            return struct.unpack(">f", struct.pack(">I", bits))[0]
        if self.WORD_FORMAT == "int32":
            signed = struct.unpack(">i", struct.pack(">I", bits))[0]
            return signed / float(self.SCALE_DIVISOR or 1.0)
        return bits / float(self.SCALE_DIVISOR or 1.0)

    # ---- 采集 ----
    async def poll(self, now_ms):
        master = self._master_get()
        try:
            vals = await master.read(self.READ_FUNCTION, self.READ_ADDR,
                                     self.READ_COUNT, unit=self.unit_id)
        except modbus.ModbusError as exc:
            self.online = False
            self.last_error = str(exc)
            raise
        if len(vals) < self.READ_COUNT:
            self.online = False
            self.last_error = "读回寄存器不足（%d/%d)" % (
                len(vals), self.READ_COUNT)
            raise modbus.ModbusError(self.last_error)
        self.ntu = self._decode(vals)
        self.online = True
        self.last_ok_ms = now_ms
        self.last_error = ""

    def health(self, now_ms, stale_ms):
        if not self.online:
            return proto.HEALTH_OFFLINE, 0, False, "从站失联"
        if now_ms - self.last_ok_ms > stale_ms:
            return proto.HEALTH_STALE, 0, True, "无有效数据"
        return proto.HEALTH_OK, 0, True, ""
