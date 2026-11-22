# -*- coding: utf-8 -*-
"""服务端业务层：命令路由 / 遥测推送 / 设备健康 / 运行模式 / 点动看门狗。

对齐 C 侧 HsfServerApp.c 的行为，端口与协议面完全兼容；额外增加数字量模块
（16DI/16DO）的命令与主题（C 侧没有这类设备）。

[闸门顺序 —— 客户端依赖这个顺序，勿调换]
    1. 自动模式闸：自动运行中**一律拒绝**远程控制（最前置，连角色都不看）。
       理由：自动模式下设备由现场逻辑驱动，远程只能看不能动；若先判角色，
       root 会在自动模式下也能动设备，安全性反了。
    2. 角色门禁：控制类命令需 root（admin 只读）。
    3. 载荷解析。
    4. 设备就绪性。
遥测/快照不经过本顺序，故自动模式仍可正常监视。
"""

import asyncio
import inspect
import re
import struct
import time

from . import autocontrol
from . import bson
from . import buttons
from . import camera as camera_mod
from . import devices
from . import modbus
from . import permissions
from . import store
from . import valves
from . import proto
from . import pumps as pumps_mod


#: 阀告警类型 → 中文（日志用；客户端有自己的文案）。
_ALARM_KIND_NAMES = {
    proto.VALVE_ALARM_TIMEOUT: "动作超时",
    proto.VALVE_ALARM_CONFLICT: "到位信号冲突",
    proto.VALVE_ALARM_CLEARED: "已恢复",
}

#: 「调速类」控制动作：只能在**运行中**下发。
#:
#: [为什么停机时禁止] 给定是运行参数，不是可预置的配置。允许停机下发会
#: 出现"停机 + 输出频率 25Hz + 转速 720rpm"这种自相矛盾的界面状态
#: （虚拟设备原来确实这样：写 0x1000 后坡道开始爬），操作员还会以为
#: "设好了但没生效"，再点启动才发现按的是别的给定。
#: 禁止后语义唯一：**先启动，再调速**。
#: 启停/点动/复位/点动释放不在此列 —— 它们本来就要在停机时用。
_VFD_SETPOINT_OPS = frozenset({
    proto.OP_SET_FREQUENCY,
    proto.OP_SET_SPEED,
})

#: **急停无效时要拦下的 op**（2026-11-06 按钮组安全闸）。
#:
#: [只拦"让设备动起来"的命令] 启动、点动、调速都在此列；停止/自由停车/
#: 故障复位/点动释放**必须放行**——急停时更要允许把人把设备停下来，
#: 拦下停止命令是反安全的。
_ESTOP_BLOCKED_OPS = frozenset({
    proto.OP_START,
    proto.OP_JOG,
    proto.OP_SET_FREQUENCY,
    proto.OP_SET_SPEED,
})

#: 控制动作的中文名（ctrl 类日志用：日志页是给运维翻的，写 op=4 没人看得懂；
#: 数值定义见 proto.OP_* / XVfdOperation，本表只做显示）。
_VFD_OP_NAMES = {
    proto.OP_START: "启动",
    proto.OP_STOP: "停止",
    proto.OP_FREE_STOP: "自由停车",
    proto.OP_QUICK_STOP: "快速停车",
    proto.OP_RESET_FAULT: "故障复位",
    proto.OP_JOG: "点动",
    proto.OP_SELECT_PRESET_SPEED: "选预置速",
    proto.OP_SET_PRESET_SPEED_HZ: "设预置速",
    proto.OP_SET_ACCEL_DECEL: "设加减速",
    proto.OP_SET_PID_TARGET: "设 PID 目标",
    proto.OP_SET_FREQUENCY: "设频率",
    proto.OP_SET_SPEED: "设转速",
}


class PushGate(object):
    """按「主题 × 设备」独立计时的推送闸。

    [为什么必须按设备独立] 同一主题有几台设备就推几帧，单闸管一个主题会让
    多设备共享计时，实际每台间隔被台数除短（C 侧实锅：温湿度两路共用一闸，
    实测 473ms ≈ 配置 1000ms ÷ 2）。
    """

    __slots__ = ("last_ms", "interval_ms")

    def __init__(self, interval_ms):
        self.last_ms = 0
        self.interval_ms = interval_ms

    def due(self, now_ms):
        """到期返回 True 并记录本次时刻；间隔 0 视为「跟随基准节拍」。"""
        if self.interval_ms <= 0:
            return True
        if self.last_ms and now_ms >= self.last_ms and \
                (now_ms - self.last_ms) < self.interval_ms:
            return False
        self.last_ms = now_ms
        return True


class HealthEntry(object):
    """设备健康条目（值语义，可整体比对判断「是否变化」）。"""

    __slots__ = ("name", "kind", "side", "health", "error", "online",
                 "run_state", "text", "addr")

    def __init__(self, name, kind, side, health, error, online, run_state, text,
                 addr=""):
        self.name = name
        self.kind = kind
        self.side = side
        self.health = health
        self.error = error
        self.online = online
        self.run_state = run_state
        self.text = text
        #: 连接地址（给人看的写法）：TCP 是 "ip:port"，485 是
        #: "COM17 · 115200 · 站 2"。**不回告"能连就行"** —— 现场排故障
        #: 第一件事就是确认"这台设备到底配在哪个口/哪个 IP"，原来只能翻
        #: 配置文件。空串表示该设备没有可报的连接参数。
        self.addr = addr

    def key(self):
        """变化比对用的元组（避开对象身份，纯值比较）。

        [必须含 addr] 否则改了配置里的 IP/站号，健康快照的 seq 不变，
        客户端不会重绘 —— 界面会一直显示旧地址。
        """
        return (self.name, self.kind, self.side, self.health, self.error,
                self.online, self.run_state, self.text, self.addr)


class JogWatch(object):
    """点动手持看门狗状态（每台变频器一把）。"""

    __slots__ = ("active", "reverse", "last_ms")

    def __init__(self):
        self.active = False
        self.reverse = False
        self.last_ms = 0


def turbidity_enabled(cfg, idx):
    """第 idx 台（0 基）浊度仪是否在采（逐台优先，回落共同键）。

    [为什么用函数而不是直接 cfg.turbidity_enabled(idx)] 单测/工具会用
    `object.__new__` + 假 cfg 造"半初始化"实例（cfg 没有 Config 的方法），
    直接点方法会 AttributeError。这里兼容三种形态：
    ① 真 Config：有 turbidity_enabled(idx) 方法；
    ② 只给了 enable_turbidity{1,2} 属性；
    ③ 只有旧的共同 enable_turbidity（缺属性时按"开"处理，与旧行为一致）。
    """
    fn = getattr(cfg, "turbidity_enabled", None)
    if callable(fn):
        return bool(fn(idx))
    per = getattr(cfg, "enable_turbidity%d" % (idx + 1), None)
    if per is not None:
        return bool(per)
    return bool(getattr(cfg, "enable_turbidity", True))


def _cfg_obj_snapshot(obj):
    """浅快照一个配置对象（普通对象取 __dict__；__slots__ 对象逐槽取）。

    [为什么] SerialConfig / AnalogConfig / StorageConfig 都是 `__slots__`
    类 ⇒ **没有 __dict__** ⇒ 直接访问会抛 AttributeError（实测：整条
    配置 SET 报「服务端处理异常：AttributeError」）。
    """
    if obj is None:
        return None
    slots = getattr(type(obj), "__slots__", None)
    if slots:
        if isinstance(slots, str):
            slots = (slots,)
        return {name: getattr(obj, name, None) for name in slots}
    try:
        return dict(obj.__dict__)
    except AttributeError:
        return None


def _cfg_obj_restore(obj, snap):
    """按浅快照恢复一个配置对象（__slots__ 与普通对象都支持）。"""
    if obj is None or snap is None:
        return
    slots = getattr(type(obj), "__slots__", None)
    if slots:
        if isinstance(slots, str):
            slots = (slots,)
        for name, value in snap.items():
            try:
                setattr(obj, name, value)
            except AttributeError:
                pass
        return
    try:
        obj.__dict__.clear()
        obj.__dict__.update(snap)
    except AttributeError:
        pass


def strict_bool(value, key):
    """严格取布尔：只认真正的布尔值（或 0/1），其它**明确报错**。

    [为什么不能用 `bool(value)`] `bool("false")` 是 **True** ⇒ 载荷里写成
    字符串 `"false"`（客户端拼错、老版本客户端发字符串）时，"只报警不停机"
    会被当成"**直接停机**"⇒ 现场误停线。布尔开关在本项目里全是**安全
    语义**（是否停设备、是否关阀复位、是否启用保护）⇒ 宁可报错让人看见，
    也不能猜。

    [为什么放行 0/1] JSON 里 0/1 是无歧义的整数开关（有些客户端这么发）
    ⇒ 接受；但**字符串**一律拒（"0"/"false" 到底是啥只能靠猜）。

    :raises ValueError: 值不是布尔/0/1。
    """
    if isinstance(value, bool):
        return value
    if isinstance(value, int) and value in (0, 1):
        return bool(value)
    raise ValueError("%s 需要布尔值（true/false）" % key)


def _looks_like_host(text):
    """像 IPv4 地址或主机名吗（**只做形状校验**，不做 DNS 解析）。

    [为什么需要] 相机 IP 写错 ⇒ 服务端连不上、画面全黑，现场分不清是
    "相机坏了"还是"地址写错了" ⇒ 形状都不像的地址当场拒掉（fail-closed）。
    """
    if not text:
        return False
    if re.match(r"^\d{1,3}(\.\d{1,3}){3}$", text):
        return all(0 <= int(part) <= 255 for part in text.split("."))
    return bool(re.match(r"^[A-Za-z0-9][A-Za-z0-9._-]*$", text))


class _NalFrame(object):
    """ES 直传的"一帧"适配（复用 `_camera_fragments` 的切片器）。

    一个 NAL 当一帧：data=Annex-B 字节、seq=NAL 序号、宽高 0（尺寸
    客户端解码后自知，片头 width/height 填 0）。
    """

    __slots__ = ("data", "seq", "width", "height")

    def __init__(self, data, seq):
        self.data = data
        self.seq = seq
        self.width = 0
        self.height = 0


class App(object):
    """业务装配：设备集合 + 推送节拍 + 命令路由。"""

    #: 运行模式（对齐 HsfRunMode：0=手动 1=自动）。
    MODE_MANUAL = 0
    MODE_AUTO = 1

    #: 各主题推送间隔（毫秒；0 = 跟随基准节拍）。
    #: 与 C 侧 HsfServerConfig.h 的 HSF_*_PUSH_MS 对齐：慢变量不必按基准
    #: 节拍推同一份值（温湿度 500ms 采集，推 200ms 只是重复）。
    TELEMETRY_MS = 100
    VFD_PUSH_MS = 100
    AI_PUSH_MS = 100
    DIGITAL_PUSH_MS = 100
    #: 电磁阀推送节拍：阀动作要几秒，200ms 足够；与数字量模块采集节拍对齐。
    VALVE_PUSH_MS = 200
    #: 流量计推送节拍：瞬时流量是慢变量（管道里不会有 100ms 级突变），
    #: 200ms 足够看清波动；与它的采集节拍对齐，避免重复推同一份值。
    FLOWMETER_PUSH_MS = 200
    #: 浊度与泵的推送节拍。浊度是慢变量（水质变化以秒计），500ms 足够；
    #: 泵状态只在开关时变，500ms 一拍已远超需要。
    TURBIDITY_PUSH_MS = 500
    PUMP_PUSH_MS = 500
    #: 数字量没有模拟量的「慢变量」特性，跟基准节拍即可。

    #: 陈旧阈值（毫秒）：超过即判数据失效。
    VFD_STALE_MS = 5000
    AI_STALE_MS = 5000
    DIGITAL_STALE_MS = 5000

    #: 健康巡检节拍（毫秒）。事件驱动：状态不变不推，故不必太快。
    HEALTH_TICK_MS = 500

    def __init__(self, config, log=None):
        self.cfg = config
        #: 原始日志回调（不含入库旁路）。入库失败时用它报错，**防递归**：
        #: 若落盘失败也走「会入库的 _log」，写失败→打日志→又写库→死循环。
        self._log_raw = log or (lambda msg: None)
        #: 业务日志入口 = 原始回调 + SQLite 落盘旁路（库未开时旁路为空操作）。
        self._log = self._log_with_store
        self.server = None

        # ---- 设备集合 ----
        self.vfd = None            # VfdM512 或 None
        self.temphum = None
        self.flowmeter = None      # FlowMeter 或 None
        #: 浊度仪列表（[TurbidityMeter, ...]，共 2 台）
        self.turbidity = []
        #: 进水泵组（数字量模块上的一路 DO，只有输出无反馈）
        self.pump_bank = None
        self.analog = None
        self.digital = []          # [DigitalIo, ...]
        #: 电磁阀组（6 路；点位来自 config.valve_items，先留空后对接）
        self.valve_bank = None
        #: 按钮组（现场操作柱：DI12~15 输入 + DO7~9 三灯）
        self.button_station = None
        # ---- 相机（2026-11-19 新增）----
        #: 相机采集器（RTSP → 板端硬解 → JPEG 帧队列）；未启用时为 None。
        self.camera = None
        #: 上一帧已发布的**帧序号**（只推新帧 ⇒ 同一帧不重复推）。
        self._camera_published_seq = -1
        #: 相机状态快照（在线/帧数/错误），随配置快照一起给客户端。
        self._camera_status = {"online": False, "frames": 0, "error": ""}

        # ---- 推送闸 ----
        self._gates = {
            proto.TOPIC_VFD_STATUS: [],
            proto.TOPIC_AI_STATUS: [PushGate(self.AI_PUSH_MS)],
            proto.TOPIC_TEMPHUM_STATUS: [],
            proto.TOPIC_FLOWMETER_STATUS: [],
            proto.TOPIC_TURBIDITY_STATUS: [],
            proto.TOPIC_PUMP_STATUS: [],
            proto.TOPIC_DIGITAL_STATUS: [],
            proto.TOPIC_VALVE_STATUS: [],
        }

        # ---- 健康 ----
        self._health_seq = 0
        self._health_last = None       # 上次推送的 entries（key 列表）
        self._health_primed = False
        #: 上一拍各设备健康档（name → health）：健康**边沿**判定用
        #: （2026-11-17：边沿入 alarms 表，客户端/网页端取同一份历史）。
        self._health_prev = {}
        self._health_next_ms = 0
        #: 自动控制异常限频表：异常类名 -> 上次记日志时刻（单调毫秒）。
        self._auto_err_at = {}

        # ---- 点动看门狗 ----
        self._jog = {}

        # ---- 运行保护 + 流量计告警（2026-11-15 用户定案）----
        #: 流量计报警位图（上次值；用于**边沿**判定：报警位出现/消失各推
        #: 一条告警，不每拍推 —— 报警位是状态，可能长期挂着）。
        #: **-1 = 还没取过基线**（首帧只记基线、不报：服务端启动时流量计
        #: 可能已经挂着空管，那不是"新发生的报警"——否则每次重启都多一条
        #: 噪音，把真事件淹掉）。
        self._fm_alarm_mask = -1
        #: 变频器故障状态（上次值；边沿判定用；2026-11-15）。
        #: [为什么不用"已取过基线"标志] 启动就带着故障**也要报**（那是真故障，
        #:  设备带着故障上线必须让人看见）⇒ 首帧与后续同口径。
        self._vfd_faulted = False
        #: 「进水泵开着且流量计报空管」这件事**成立**的时刻（单调毫秒；
        #: 0 = 当前不成立）。持续超过配置的超时 ⇒ 保护动作。
        self._protect_since_ms = 0
        #: 压差**连续**超过上限的起始时刻（0 = 当前未超；去抖用）。
        self._press_high_since_ms = 0
        #: [电机超温保护 2026-11-20] 温度**连续**超过阈值的起始时刻
        #: （0 = 当前未超；去抖用）。
        self._motor_over_temp_since_ms = 0

        # ---- 采集任务 ----
        self._poll_tasks = []
        #: 数字量通道名是否已变更（true ⇒ 下一拍主动下发一帧只含名字的
        #: full 帧）。对齐 C 侧 s_digitalNamesDirty /
        #: HsfServerApp_notifyChannelNamesChanged：名字是部署参数（点位
        #: 映射来自配置），装配/配置变更处置位一次，见
        #: mark_digital_names_dirty。
        self._digital_names_dirty = False
        # ---- 数据落盘（SQLite：历史/日志/报警，见 store.py）----
        self.store = store.Store(getattr(self.cfg, "base_dir", "") or ".",
                                 log=self._log_raw)
        #: 历史采样任务（start_storage 启动；未启用时任务空转）。
        self._store_task = None
        #: 运行模式（手动/自动）。自动模式拒绝全部远程控制。
        self.run_mode = self.MODE_MANUAL
        #: 自动运行控制器（**预留骨架，控制逻辑待扩展**）。
        #: 见 autocontrol.py 模块头：现在 step() 什么都不做，把"模式门禁"和
        #: "服务端自己驱动设备"两层职责先分开，将来逻辑直接填进去即可。
        #:
        #: [日志类别 = auto] 本模块全部日志都是自动运行生命周期事件
        #: （启动/停止/急停/复位/全停/运行相位/现场按钮受理），类别是
        #: **调用点的属性** ⇒ 装配处绑定一次，模块内经单参 log() 自然带上。
        self.auto_ctx = autocontrol.AutoContext(
            self, log=self._log_as(proto.LOG_CAT_AUTO))
        self.auto_controller = autocontrol.AutoController(
            self.auto_ctx, log=self._log_as(proto.LOG_CAT_AUTO))

    # ==================================================================
    #                          装配 / 拆除
    # ==================================================================
    def _wire_sim_bus_fidelity(self, master):
        """给 --sim 的路 B 虚拟总线接上 ttyS4 实测的时序行为（2026-11-09）。

        [为什么要在装配处接] 站 1/2/5 的应答时延与「站 5 长帧后站 1 恢复期」
        都是从站的物理行为；旋钮值全部从 cfg 来（sim 节可覆盖，缺省=实测
        文档值），站号跟着 cfg 走（改站号后时延/恢复期自动跟到新站号上）。
        同时打一条装配日志——回归用它断言「旋钮存在且按配置生效」。
        """
        turb_ids = list(self.cfg.turbidity_unit_ids[:2])
        t_lohi = list(getattr(self.cfg, "sim_turbidity_reply_ms", None) or [])
        f_lohi = tuple(getattr(self.cfg, "sim_flowmeter_reply_ms", None)
                       or (0, 0))
        t_ranges = []
        for idx, uid in enumerate(turb_ids):
            lo, hi = (t_lohi[idx] if idx < len(t_lohi) else (0, 0))
            master.set_unit_reply_latency(uid, lo, hi)
            t_ranges.append("%d~%d" % (lo, hi))
        master.set_unit_reply_latency(self.cfg.flowmeter_unit_id,
                                      f_lohi[0], f_lohi[1])
        recovery_ms = int(getattr(self.cfg, "sim_recovery_ms", 0) or 0)
        if recovery_ms > 0 and turb_ids:
            master.set_recovery_gate(self.cfg.flowmeter_unit_id,
                                     turb_ids[0], recovery_ms)
        self._log(
            "虚拟485路B 保真：事务间隙 %dms / 应答时延(ms) 浊1 %s 浊2 %s "
            "流量 %d~%d / 站1 恢复期 %dms（站%d 长帧后）"
            % (getattr(master, "min_tx_gap_ms", 0),
               t_ranges[0] if t_ranges else "0~0",
               t_ranges[1] if len(t_ranges) > 1 else "0~0",
               f_lohi[0], f_lohi[1],
               recovery_ms, self.cfg.flowmeter_unit_id))

    def build_devices(self):
        """按配置装配设备对象（不打开任何端口/连接，纯对象构造）。

        [虚拟设备模式] 配了 `cfg.sim_enabled` 时，把 master 换成内存寄存器
        模型（virtualdev.py），**devices.py 与 app.py 全部照跑**：
        换算、状态字解码、控制闸门、点动看门狗、阀定时器都是真实实现，
        只有"电信号"是假的。真实设备完全不碰（不开串口、不建 TCP）。
        """
        from . import virtualdev
        from .modbus import RtuMaster, TcpMaster
        sim = bool(getattr(self.cfg, "sim_enabled", False))
        if sim:
            self._log("虚拟设备模式：master 换成内存寄存器模型"
                      "（真实设备不碰，业务层全为真实实现）")

        # [关键] 同一条 485 上的多个从站必须**共用一个主站**。
        # 串口是排他资源：每台设备各建一个 RtuMaster 就会互相抢口
        # （第二个 open 直接 EACCES/EBUSY）。而且 RTU 是半双工单主站总线，
        # 多主站并发写会互相踩帧——本来就该串行化。
        #
        # [现场两条独立 485] 路 A（serial）：变频器 + 温湿度，115200；
        # 路 B（serial_b）：流量计 + 浊度仪 ×2，9600。按「串口号+波特率」
        # 缓存 master，两条线自然分成两个实例；同一条线上的多个从站共享
        # 同一个实例（RtuMaster 内部 asyncio.Lock 保证事务串行）。
        rtu_masters = {}

        # ---- 浊度「按台」旋钮（字节序 / 读取寄存器数 / --sim 基准值）----
        # [为什么按下标取] cfg.turbidity_* 与站号表 turbidity_unit_ids 一一
        # 对应：站1 CDAB/2 字、站2 DCBA/4 字（2026-11-09 ttyS4 实测口径，
        # 见 config.py 注释）。配置缺台时回 None → 设备/模型各自落类缺省
        # （CDAB、2 字），老配置文件一字不改也照跑。
        turb_orders = list(getattr(self.cfg, "turbidity_byte_orders", None)
                           or [])
        turb_counts = list(getattr(self.cfg, "turbidity_read_counts", None)
                           or [])
        sim_ntu = list(getattr(self.cfg, "turbidity_sim_ntu", None) or [])
        sim_temp = list(getattr(self.cfg, "turbidity_sim_temp", None) or [])

        def turb_order(idx):
            return turb_orders[idx] if idx < len(turb_orders) else None

        def turb_count(idx):
            return turb_counts[idx] if idx < len(turb_counts) else None

        def make_rtu(unit_id, bus="a"):
            """取（必要时创建）该串口的主站。

            :param bus: "a"=变频器/温湿度那条（115200），
                        "b"=流量计/浊度仪那条（9600）。

            [为什么 bus 必须显式传] 两条线速率不同，共用一个 master 会让
            9600 的设备被按 115200 访问 —— 现象是"从站全不通"。
            cfg.serial / cfg.serial_b 各自带 port 与 baudrate，key 里带上
            它们，两条线就不会串。
            """
            c = self.cfg.serial_b if bus == "b" else self.cfg.serial
            key = (c.port, c.baudrate)
            master = rtu_masters.get(key)
            if master is None:
                if sim:
                    # [虚拟模式按现场分两条总线 2026-11-10] 真实链路是两条
                    # 独立 485（路 A：变频器+温湿度；路 B：流量计+浊度×2），
                    # sim 也按 bus 各建一台 VirtualMultiMaster——不能像早先
                    # 那样把全部从站塞进同一张表：站号表按配置走之后，浊度
                    # 站 2 会把变频器站 2（两路缺省都指向 2）从表里**覆盖**
                    # ⇒ VFD 写 0x1000 落进"浊度仪只读"。真实现场两台从站
                    # 根本不在同一条线上，建模必须与接线一致。
                    if bus == "b":
                        models = {}
                        if self.cfg.enable_flowmeter:
                            # [--sim 基准值从 cfg 来] 缺省=2026-11-09 实测
                            # 口径（3.6 m³/h、空管 False）。验收口径：设备
                            # 层同一条解码路径读回的值与模型给定值一致。
                            models[self.cfg.flowmeter_unit_id] = \
                                virtualdev.VirtualFlowMeter(
                                    base_flow=getattr(
                                        self.cfg, "flowmeter_sim_flow", None),
                                    empty_pipe=bool(getattr(
                                        self.cfg, "flowmeter_sim_empty",
                                        False)))
                        if (turbidity_enabled(self.cfg, 0)
                                or turbidity_enabled(self.cfg, 1)):
                            # [微波动幅按台 2026-11-09 实测] 站 1 恒定（幅 0，
                            # 20 轮完全不动）、第二台（2026-11-10 起面板站号
                            # 3）±0.001 缓慢波动；温度 ±0.05 漂移。缺省
                            # None=旧口径（±3% 相对波动）。
                            sim_wob = list(getattr(
                                self.cfg, "turbidity_sim_wobble", None) or [])
                            sim_twob = list(getattr(
                                self.cfg, "turbidity_sim_temp_wobble",
                                None) or [])
                            for idx, uid in enumerate(
                                    self.cfg.turbidity_unit_ids[:2]):
                                # [停采的台不建模 2026-11-10] 读它只会超时，
                                # 与真机"这台没装/没接"表现一致，免得界面
                                # 上停用的表还显示在跳数。
                                if not turbidity_enabled(self.cfg, idx):
                                    continue
                                # [字节序按台] 模型造帧与设备层解码必须同序
                                # （站1 CDAB / 站2 DCBA，实测文档 §5.3）；
                                # temperature=None 的台不建模 0x0002/0x0003。
                                models[uid] = virtualdev.VirtualTurbidity(
                                    index=idx,
                                    base_ntu=(sim_ntu[idx]
                                              if idx < len(sim_ntu) else None),
                                    byte_order=turb_order(idx) or "CDAB",
                                    temperature=(sim_temp[idx]
                                                 if idx < len(sim_temp)
                                                 else None),
                                    wobble_ntu=(sim_wob[idx]
                                                if idx < len(sim_wob)
                                                else None),
                                    temp_wobble_c=(sim_twob[idx]
                                                   if idx < len(sim_twob)
                                                   else None))
                        master = virtualdev.VirtualMultiMaster(
                            models, name="虚拟485路B", log=self._log,
                            min_tx_gap_ms=c.poll_gap_ms)
                        # [ttyS4 保真] 从站应答时延 + 站1 长帧后恢复期，
                        # 旋钮与缺省值见 config.py 的 sim 相关注记。
                        self._wire_sim_bus_fidelity(master)
                    else:
                        models = {
                            self.cfg.vfd_unit_id: virtualdev.VirtualVfd(
                                max_hz=self.cfg.serial.vfd_max_hz,
                                log=self._log),
                            self.cfg.temphum_unit_id:
                                virtualdev.VirtualTempHum(),
                        }
                        master = virtualdev.VirtualMultiMaster(
                            models, name="虚拟485路A", log=self._log,
                            min_tx_gap_ms=c.poll_gap_ms)
                else:
                    # 超时/重试取该路配置（同总线的从站共用一套链路参数）。
                    # [路 B 站间最小间隙 200ms 2026-11-09] ttyS4 三台混跑实测
                    # （docs/HSF板端ttyS4三设备取数实测总表 §9）：站5 的 49 字节
                    # 长帧读完后立刻问站1 → 几乎零应答（接收器恢复期）；≥125ms
                    # 才恢复，200ms 稳定 10/10。路 A 115200 变频器 100ms 轮询
                    # 不能拖，间隙取 0。
                    # [数值从 cfg 来] serial_b 缺省 poll_gap_ms=200、serial
                    # 缺省 0（config.SerialConfig 注释有据）——与上面口径
                    # 同值，但现场可经配置文件/CLI 调，不必改代码。
                    master = RtuMaster(c.port, c.baudrate, unit_id,
                                       c.timeout_ms / 1000.0, c.retries,
                                       self._log,
                                       min_tx_gap_ms=c.poll_gap_ms)
                rtu_masters[key] = master
            return master

        def make_vfd_master():
            return make_rtu(self.cfg.vfd_unit_id, "a")

        def make_th_master():
            return make_rtu(self.cfg.temphum_unit_id, "a")

        def make_flow_master():
            return make_rtu(self.cfg.flowmeter_unit_id, "b")

        def make_turb_master(uid):
            return make_rtu(uid, "b")

        if self.cfg.enable_serial:
            if self.cfg.enable_vfd:
                self.vfd = devices.VfdM512(make_vfd_master, self.cfg.vfd_unit_id,
                                           self._log)
                # 铭牌额定点：Hz ↔ rpm 换算依据（现场实测 50Hz ≈ 1440rpm）。
                self.vfd.rated_rpm = self.cfg.serial.vfd_rated_rpm
                self.vfd.rated_hz = self.cfg.serial.vfd_rated_hz
                self.vfd.max_freq_hz = self.cfg.serial.vfd_max_hz
                self._jog["vfd"] = JogWatch()
                self._gates[proto.TOPIC_VFD_STATUS].append(
                    PushGate(self.VFD_PUSH_MS))
            if self.cfg.enable_temphum:
                self.temphum = devices.TempHum(make_th_master,
                                               self.cfg.temphum_unit_id,
                                               self._log)
                self._gates[proto.TOPIC_TEMPHUM_STATUS].append(
                    PushGate(self.TEMPHUM_PUSH_MS))
            if self.cfg.enable_flowmeter:
                self.flowmeter = devices.FlowMeter(make_flow_master,
                                                   self.cfg.flowmeter_unit_id,
                                                   self._log)
                self._gates[proto.TOPIC_FLOWMETER_STATUS].append(
                    PushGate(self.FLOWMETER_PUSH_MS))
            # [两台独立设备 2026-11-10] 不管开关都建对象：逐台开关只门控
            # **轮询**（运行期改开关后 restart_polling 立刻生效）；停采的台
            # 在健康表里显示"已停用"，而不是整组消失。
            for idx, uid in enumerate(self.cfg.turbidity_unit_ids[:2]):
                # [字节序/读取数按台] 站1 CDAB×2 字（老口径一字不改）、
                # 站2 DCBA×4 字（同刻快照带温度，实测文档 §5.2/§5.3）。
                # 站号本身就是 uid——全从 cfg 来，代码不写死。
                self.turbidity.append(devices.TurbidityMeter(
                    (lambda u=uid: make_turb_master(u)), uid,
                    name=("进水浊度仪" if (idx + 1) == 1 else "出水浊度仪"), log=self._log,
                    byte_order=turb_order(idx),
                    read_count=turb_count(idx)))
            self._gates[proto.TOPIC_TURBIDITY_STATUS].append(
                PushGate(self.TURBIDITY_PUSH_MS))

        if self.cfg.enable_analog:
            ai = self.cfg.analog
            if sim:
                master = virtualdev.make_analog_master(ai.channels, self._log)
            else:
                master = TcpMaster(ai.host, ai.port, ai.unit_id,
                                   timeout=2.0, log=self._log)
            self.analog = devices.AnalogInput(master, ai.channels, self._log)

        if self.cfg.enable_digital:
            for i, dio_cfg in enumerate(self.cfg.digital_modules):
                if sim:
                    # [行程时间必须传下去] 虚拟阀若"写完 DO 立刻到位"，
                    # 状态机的「动作中」与「20s 超时报警」两条分支永远走
                    # 不到 —— 功能存在但无法观察/验证。故把现场节奏（10s）
                    # 与"坏阀"开关一起交给虚拟模型。
                    master = virtualdev.make_digital_master(
                        dio_cfg.channels,
                        flaky_rate=getattr(self.cfg, "sim_flaky_rate", 0.0),
                        move_ms=getattr(self.cfg, "sim_valve_move_ms", None),
                        no_feedback=getattr(self.cfg, "sim_valve_no_feedback",
                                            False),
                        log=self._log)
                else:
                    master = TcpMaster(dio_cfg.host, dio_cfg.port,
                                       dio_cfg.unit_id,
                                       timeout=dio_cfg.timeout_s,
                                       log=self._log)
                dev = devices.DigitalIo(master, dio_cfg.name,
                                        channels=dio_cfg.channels,
                                        publish=dio_cfg.publish,
                                        log=self._log)
                self.digital.append(dev)
                self._gates[proto.TOPIC_DIGITAL_STATUS].append(
                    PushGate(self.DIGITAL_PUSH_MS))

        # ---- 电磁阀组（依赖数字量模块：DI 反馈来自它们的缓存）----
        if self.cfg.enable_valves:
            # 日志类别 = ctrl：本模块只记"阀命令下发/已到位/被外部改动"，
            # 全部是控制受理事件（与 C 侧 ctrl 类的"控制受理/拒绝"同属性）。
            self.valve_bank = valves.ValveBank(
                self.digital, self.cfg.valve_items,
                self._log_as(proto.LOG_CAT_CTRL),
                move_timeout_ms=self.cfg.valve_move_timeout_ms)
            self._gates[proto.TOPIC_VALVE_STATUS] = [
                PushGate(self.VALVE_PUSH_MS)]
            ready = self.valve_bank.ready_count()
            if ready:
                self._log("电磁阀：已装配 %d/%d 路（点位来自配置）"
                          % (ready, proto.HSF_VALVE_COUNT))
            else:
                self._log("电磁阀：6 路均未配置点位（界面可见，控制不可用）")

        # ---- 进水泵（同样依赖数字量模块的 DO 回显）----
        if self.cfg.enable_pump:
            # 日志类别 = ctrl（同电磁阀：只记泵命令下发/重复命令/外部改动）。
            self.pump_bank = pumps_mod.PumpBank(
                self.digital, self.cfg.pump_items,
                self._log_as(proto.LOG_CAT_CTRL))
            self._gates[proto.TOPIC_PUMP_STATUS] = [
                PushGate(self.PUMP_PUSH_MS)]
            ready = self.pump_bank.ready_count()
            if ready:
                self._log("进水泵：已装配 %d 台（%s，只有输出无反馈）"
                          % (ready, "、".join(
                              "DO%s" % it.do for it in self.pump_bank.items
                              if it.ready)))
            else:
                self._log("进水泵：未配置点位（界面可见，控制不可用）")

        # ---- 按钮组（现场操作柱：4 DI 输入 + 3 DO 灯）----
        # [为什么放在阀门/泵之后] 它要读写同一批数字量缓存；顺序不影响
        # 正确性（都走 dev.di/dev.do 快照），放在最后只是便于阅读。
        #
        # [⚠ 按钮只是"请求" 2026-11-06] 灯跟的是**自动运行是否真在跑**，
        # 不是"按没按过启动"。故装配后立刻把控制器真实状态同步下去（首拍
        # 就能亮出正确的灯，不会先亮启动灯再熄灭）。
        if getattr(self.cfg, "enable_buttons", False):
            self.button_station = buttons.ButtonStation(
                self.auto_ctx, self.cfg.button_items,
                debounce_ms=getattr(self.cfg, "button_debounce_ms", 300),
                # 日志类别 = ctrl：本模块记的是三盏灯（DO7~DO9）的写入状态，
                # 属执行器输出控制；按键迁移由 app._on_button_state 记。
                log=self._log_as(proto.LOG_CAT_CTRL))
            self.button_station.on_state_change = self._on_button_state
            self.button_station.set_running(
                bool(getattr(self.auto_controller, "running", False)))
            self._log("按钮组：已装配（DI%d~%d 输入 / DO%d~%d 灯）"
                      % (self.cfg.button_items.get("estop_di"),
                         self.cfg.button_items.get("reset_di"),
                         self.cfg.button_items.get("start_lamp_do"),
                         self.cfg.button_items.get("reset_lamp_do")))

        # ---- 名字口径置脏（对齐 C 侧"改宏重新部署"后的主动补发）----
        # 数字量通道名由阀门/泵/按钮三处点位映射推导（见 _digital_channel_names
        # 及同族函数），本方法结束时三者已定型 —— 这里置位一次，装配后首拍
        # 主动补一帧名字（此刻通常尚无订阅者，publish 静默为 0，无副作用）。
        # [运行期热改] py 版点位目前只在装配时定型（无热载命令）；将来若加
        # 点位热改入口，改完调用 mark_digital_names_dirty() 即可，推送通路
        # 已就绪（见 _publish_telemetry 数字量段）。
        if self.digital:
            self.mark_digital_names_dirty("点位装配完成")

    @property
    def TEMPHUM_PUSH_MS(self):
        return getattr(self.cfg.serial, "temphum_push_ms", 500)

    def attach(self, server):
        """接线到 XLink 服务端（分发/推送/健康/首连补发/登录策略）。"""
        self.server = server
        server.dispatch = self.handle_request
        server.on_publish_now = self.publish_topic_now
        server.on_session_added = self.on_session_added
        server.on_ready = self.on_session_ready
        server.on_tick = self
        server.policy_hook = self.policy_blocks_login
        # [链路层运维事件入库] 会话接入/断开/超时（conn）、登录成功/被拒/
        # 策略拒登（auth）由会话层带上类别送进来；流水账（订阅/退订/tick
        # 异常）不走这里——见 xlink.Server.log_event 的取舍说明。
        server.on_event_log = self._log_with_store

    #: xlink.Server 读取本属性决定 tick 周期（基准节拍）。
    @property
    def interval_ms(self):
        return self.TELEMETRY_MS

    # ==================================================================
    #                       运行模式（持久化）
    # ==================================================================
    def apply_mode_to_devices(self, switched=False):
        """模式同步到设备层 / 自动控制器。

        :param switched: 本次是**模式真的发生了切换**（不是幂等重放）。
            切换 ⇒ 关闭所有设备（双向，见下面的「切模式 ⇒ 全停」）；
            幂等重放 ⇒ 不动设备（见「幂等重放不动设备」）。

        [⚠ 安全口径 2026-11-05 订正：进自动**不**自动启动控制器]
        原实现是"进自动就 ctl.start()"，等于把一个模式开关变成了**设备启动
        按钮** —— 操作员只想切个模式，设备立刻开始被自动逻辑驱动。现场反馈
        "默认应该是停止状态，不然太危险了"，这个判断是对的：

            模式 = 自动   →  只是**授权**自动逻辑可以跑（远程控制被禁）
            自动运行       →  必须由人**再明确启动一次**才真的开始驱动设备

        所以这里只做**单向**安全动作：**退出自动时强制停控制器**（切回手动
        还在自动跑 = 手动模式下设备被偷偷驱动，是硬性安全问题）。
        启动路径在独立的 CMD_AUTO_CONTROL 里，见 _h_auto_control。

        [2026-11-10 用户口径：切换模式 ⇒ 关闭所有设备（**双向**）]
        「切换模式后要关闭所有设备，只有按了启动按钮后设备才允许工作，
        这是自动模式的运行逻辑」：
            · 切到自动：自动模式会**拒掉全部远程控制**、控制器又要等人按
              "启动"才跑 —— 设备若还按手动命令转着，现场看到的是"切了自动，
              泵还在转，而且谁也停不了它"（远程控制已被关掉）。故切自动也
              要先全停。
            · 切到手动：自动逻辑退出，设备不能继续按自动命令跑。
        [幂等重放不动设备] 同一模式**重复**下发（客户端重发、启动时恢复）
        **不**全停：手动模式下设备正由人操作，一句重复的模式指令把人家手动
        开的泵停掉是事故。

        原来只停控制器（ctl.stop），变频器/泵/阀**照旧转着** —— 控制器
        停了但设备没停，等于"手动模式下设备还在被上次的自动命令驱动"，
        现场看到的是"我切了手动，泵还在转"。现在走 `stop(stop_devices=True)`：
        停控制器 + 变频器减速停机 + 逐台停泵 + 逐路关阀。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        now = self._now_ms()
        if not self.is_auto():
            # 退自动 ⇒ 必须停控制器（安全，单向，与是否切换无关：手动模式
            # 下控制器还跑着 = 设备被偷偷驱动）。
            # [不判 ctl.running] 控制器可能已经停了但设备还在转（比如上一批
            # 全停失败了一台），此时切手动仍要再停一次 —— 这是补漏机会。
            # [全停只在真切换时做] 幂等重放（同一模式再下一遍）不动设备：
            # 手动模式下设备正由人操作，停掉人家手动开的泵是事故。
            # [阀一定关回 2026-11-12] 切模式的全停是**既有安全契约**
            # （「切换模式 ⇒ 关闭所有设备」含阀），不受"停止/急停是否复位
            # 阀"的新配置影响 ⇒ 显式 reset_valves=True。
            # [切模式＝重来 2026-11-19 用户口径] 切模式不是"人按停止"
            # ⇒ 清掉暂停态（下次启动从头），只有普通停止命令保留暂停。
            ctl.stop(now, stop_devices=switched, reason="切回手动",
                     reset_valves=True, clear_pause=True)
            # 类别 = auto（模式切换，与 C 侧 hsfAppApplyModeToDevices 的
            # hsfAppEventLog("auto", ...) 同口径）。
            self._log_with_store(
                "退出自动模式：自动控制已强制停止%s"
                % ("，设备全停" if switched else ""), proto.LOG_CAT_AUTO)
            # 按钮组运行态立刻同步（灯的口径：自动模式跟随运行态，手动模式
            # 跟随"设备在动"——见 buttons.ButtonStation.poll）。
            ctl._sync_station()
        elif switched:
            # 切到自动 ⇒ 也全停设备（2026-11-10 用户口径「切换模式后要关闭
            # 所有设备」）：自动模式拒掉全部远程控制，控制器又要等人按启动
            # 才跑 —— 设备若还转着，现场既停不掉也看不到谁在驱动它。
            # [不启动控制器] 模式开关不是启动按钮：设备要等"启动"才允许工作。
            ctl.stop(now, stop_devices=True, reason="切到自动",
                     reset_valves=True, clear_pause=True)
            self._log_with_store(
                "切到自动模式：设备全停，等待人工启动自动运行",
                proto.LOG_CAT_AUTO)

    def restore_run_mode(self, mode):
        """启动时从配置恢复运行模式（**不落盘**，因为值本来就来自磁盘）。

        [为什么不直接用 set_run_mode] 恢复期写回配置是无意义的 IO，而且会
        让"读取失败按缺省值跑"的情形把缺省值覆盖掉真实配置。

        [⚠ 重启后控制器**一律不自动启动** 2026-11-05] 原实现"恢复成自动模式
        就把控制器启动起来"，等于**服务端重启 = 设备自己开始动**。现场断电
        重启、systemd 拉起、升级重启都属这一类 —— 没人按过启动，设备却动了，
        这是最不能接受的一种自动行为。

        故恢复只还原**模式位**（界面显示"自动模式"），控制器保持停止；
        要真正开始自动运行，必须由人下发 CMD_AUTO_CONTROL。
        """
        if mode not in (self.MODE_MANUAL, self.MODE_AUTO):
            mode = self.MODE_MANUAL
        self.run_mode = mode
        # 只同步"退自动必须停"这一条安全单向；不启动
        self.apply_mode_to_devices()
        return True

    def set_run_mode(self, mode):
        if mode not in (self.MODE_MANUAL, self.MODE_AUTO):
            return False
        if mode == self.run_mode:
            # 幂等重放：闸同步一次，但**不动设备**（见 apply_mode_to_devices
            # 的「幂等重放不动设备」）。
            self.apply_mode_to_devices(switched=False)
            return True
        self.run_mode = mode
        # 真切换 ⇒ 关闭所有设备（双向；2026-11-10 用户口径）。
        self.apply_mode_to_devices(switched=True)
        self.cfg.save_run_mode(mode)
        self._log_with_store("运行模式切到 %s（远程控制%s）"
                             % ("自动" if mode == self.MODE_AUTO else "手动",
                                "已禁用" if mode == self.MODE_AUTO else "已启用"),
                             proto.LOG_CAT_AUTO)
        return True

    def is_auto(self):
        return self.run_mode == self.MODE_AUTO

    def is_auto_running(self):
        """自动控制器**是否真的在跑**（与"是不是自动模式"是两件事）。

        [为什么要单独一个查询] `is_auto()` 说的是"人把模式拨到自动了"；
        这个说的才是"自动逻辑真的在驱动设备"。两者正常同步，但**不能让
        界面从模式位推断运行位** —— 一旦控制器没启动（装配时序错、异常
        退出、将来做暂停），界面会显示"自动在跑"而实际什么都不做，
        这种假正常比直接报故障更难查。
        """
        ctl = getattr(self, "auto_controller", None)
        return bool(ctl is not None and ctl.running)

    def _auto_state_extra(self):
        """运行状态快照的公共部分（CMD_RUN_MODE 查询 / 0x0115 应答 extra 共用）。

        [除了自动状态族，还带「最近报警」] 主页要一行「最近报警」摘要 ⇒
        放进**每次都发的**状态快照里（客户端 1s 轮询一次，已经在了），
        不必让主页再发一条分页命令。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is not None and hasattr(ctl, "stage_timing"):
            stage_elapsed, stage_total = ctl.stage_timing()
        else:
            stage_elapsed, stage_total = -1, 0
        # [回显口径 2026-11-13·对齐 C reportedMode] 组合模式把子阶段记账在
        # ctl.mode（运行中报子阶段）；停止/急停收尾把 ctl.mode 还原 "auto"
        # （与 C s_mode 分账同口径，使无 mode 再启动重进循环）⇒ 停止后带
        # 循环记忆时改报**记忆阶段**（「下次将从哪一步继续」，C
        # HsfServerAutoMode_reportedMode 同款三分支），两端键值逐字一致。
        rep_mode = getattr(ctl, "mode", "") or ""
        if ctl is not None and not getattr(ctl, "running", False):
            # [粘性分账 2026-11-18] 停止时 self.mode 是「上次跑的模式」，
            # 而用户**当前选中**的可能是另一个（运行中改过下拉）⇒ 停止后
            # 报粘性（selected_mode）：客户端据此回显下拉，也保证
            # 「物理启动按钮」与「屏幕启动按钮」按同一个模式跑。
            rep_mode = (getattr(ctl, "selected_mode", "") or rep_mode)
        if ctl is not None and rep_mode == proto.AUTO_MODE_AUTO \
                and not getattr(ctl, "running", False):
            resume = getattr(ctl, "_cycle_resume_mode", "") or ""
            if resume:
                # [2026-11-18 配置化] 记住的阶段若**已不在**当前参与循环的
                # 模式列表里 ⇒ 回显 "auto"（别显示一个链上不存在的阶段，
                # 否则界面会显示一个下次根本不会跑的阶段）。
                try:
                    stages = ctl.cycle_stages()
                except Exception:                   # noqa: BLE001
                    stages = []
                if resume in stages:
                    rep_mode = resume
                else:
                    rep_mode = proto.AUTO_MODE_AUTO
        return {
            proto.KEY_AUTO_MODE: rep_mode,
            proto.KEY_AUTO_PHASE: getattr(ctl, "phase", "") or "",
            proto.KEY_AUTO_FAULT: bool(getattr(ctl, "_fault_latched", False)),
            # [累计自动运行时长 2026-11-12] 服务端计（唯一事实源）。
            proto.KEY_AUTO_RUN_MS: int(ctl.run_ms()) if ctl is not None else 0,
            # [子阶段剩余时间 2026-11-12] 主界面模式卡的表盘/文字都用它：
            # elapsed=-1（不在跑）或 total=0（本阶段无定时）⇒ 客户端显示
            # 「--」/「已运行」，不倒推一个假的倒计时。
            proto.KEY_AUTO_STAGE_ELAPSED_MS: (stage_elapsed
                                             if ctl is not None else -1),
            proto.KEY_AUTO_STAGE_TOTAL_MS: (stage_total
                                           if ctl is not None else 0),
            # [暂停态 2026-11-19] 停止＝暂停 ⇒ 快照带上"暂停中"的记忆
            # （暂停标志 + 记住的模式/阶段）。客户端据此显示「暂停中（还剩
            # X）」；老客户端不认这两个键也不受影响（缺键不改原值）。
            proto.KEY_AUTO_PAUSED: bool(getattr(ctl, "_paused", False)),
            proto.KEY_AUTO_PAUSE_MODE: (str(
                getattr(ctl, "_pause_mode", "") or "")
                if ctl is not None else ""),
            # [最近报警 2026-11-19] 主页「最近报警」卡的一行摘要。
            # 无报警时为 **None**（不是空文档 —— 空文档与"没这个键"分不开）。
            proto.KEY_LAST_ALARM: self._last_alarm_doc(),
        }

    def _last_alarm_doc(self):
        """最近一条报警 → 协议文档 {kind, dev, text, atMs}（无则 None）。

        [为什么源类别也带上] 客户端按 **dev**（设备类别，proto.KIND_*）
        映射「类型」列（与报警页同源，model.KIND_TEXT）；但 dev 可能缺
        （老行/某些告警）⇒ 再带一个 kind（来源类别：health/auto/valve…）
        供客户端兜底 ⇒ 两端都能说清"哪类事"。

        [为什么 atMs 是墙钟] 与 alarms 表的 ts 同口径（墙钟 epoch 毫秒）⇒
        客户端直接格式化 HH:MM:SS，不用再做一次时钟换算。
        """
        row = None
        try:
            row = self.store.last_alarm()
        except Exception:                               # noqa: BLE001
            row = None
        if not row:
            return None
        doc = {
            proto.LAST_ALARM_KEY_KIND: str(row.get("kind") or ""),
            proto.LAST_ALARM_KEY_TEXT: str(row.get("text") or ""),
            proto.LAST_ALARM_KEY_AT_MS: int(row.get("ts") or 0),
        }
        if row.get("dev") is not None:
            doc[proto.LAST_ALARM_KEY_DEV] = int(row.get("dev"))
        return doc

    # ==================================================================
    #                         采集循环
    # ==================================================================
    async def start_polling(self):
        """每台设备一个采集任务（互不阻塞：一台慢不影响另一台）。

        [开关口径] 装配在启动时定死（master/连接在此建立）；本函数按
        cfg 的各设备开关决定**轮不轮询**。被关的设备健康条目转数据过期
        —— 如实反映"不采了"，不装作在线。
        """
        if self.vfd is not None:
            if self.cfg.enable_vfd:
                self._poll_tasks.append(asyncio.ensure_future(
                    # 日志名与健康条目同名（现场口径"搅拌泵"），
                    # 否则日志说"变频器"、界面说"搅拌泵"，对不上号。
                    # [单一事实源 2026-11-18] 标签取设备对象的 name，
                    # 设备层自己的日志（组级失败限频打印）用同一个名字。
                    self._poll_loop(self.vfd, self.cfg.vfd_poll_ms,
                                    self.vfd.name, "vfd_poll_ms")))
            else:
                self._log("%s采集已停用（配置 vfdEnabled=false）"
                          % self.vfd.name)
        if self.temphum is not None:
            if self.cfg.enable_temphum:
                self._poll_tasks.append(asyncio.ensure_future(
                    self._poll_loop(self.temphum, self.cfg.temphum_poll_ms,
                                    "温湿度", "temphum_poll_ms")))
            else:
                self._log("温湿度采集已停用（配置 temphumEnabled=false）")
        if self.flowmeter is not None:
            if self.cfg.enable_flowmeter:
                # [独立周期 2026-11-10] flowmeter_poll_ms=0 时跟随温湿度
                # （旧行为），显式设过就按它跑。
                _fm_ms = (self.cfg.flowmeter_poll_ms
                          or self.cfg.temphum_poll_ms)
                self._poll_tasks.append(asyncio.ensure_future(
                    self._poll_loop(self.flowmeter, _fm_ms,
                                    "流量计", "flowmeter_poll_ms")))
            else:
                self._log("流量计采集已停用（配置 flowmeterEnabled=false）")
        for idx, tdev in enumerate(self.turbidity):
            # [逐台开关 2026-11-10] 两台是独立设备：只停一台时另一台照采。
            # 旧的共同键 turbidityEnabled 已降为缺省（见 cfg.turbidity_enabled）。
            if not turbidity_enabled(self.cfg, idx):
                self._log("%s 采集已停用（配置 turbidity%dEnabled=false）"
                          % (tdev.name, idx + 1))
                continue
            # [逐台周期 2026-11-10] 站1/站3 各按自己的周期跑。
            _t_ms = (self.cfg.turbidity1_poll_ms if idx == 0
                     else self.cfg.turbidity2_poll_ms)
            self._poll_tasks.append(asyncio.ensure_future(
                self._poll_loop(tdev, _t_ms,
                                tdev.name,
                                "turbidity1_poll_ms" if idx == 0
                                else "turbidity2_poll_ms")))
        if self.analog is not None:
            if self.cfg.enable_analog:
                self._poll_tasks.append(asyncio.ensure_future(
                    self._poll_loop(self.analog, self.cfg.analog_poll_ms,
                                    "模拟量", "analog_poll_ms")))
            else:
                self._log("模拟量采集已停用（配置 analogEnabled=false）")
        for dev in self.digital:
            if self.cfg.enable_digital:
                self._poll_tasks.append(asyncio.ensure_future(
                    self._poll_loop(dev, self.cfg.digital_poll_ms, dev.name,
                                    "digital_poll_ms")))
            else:
                self._log("%s 采集已停用（配置 digitalEnabled=false）" % dev.name)

    async def restart_polling(self):
        """仅取消并重启**轮询任务**（采集开关被远程改后调用）。

        [为什么不用 stop_polling] 那是退出路径：连 master 带设备全拆光
        （设备引用置 None）。开关变更只需要换一批轮询任务，设备对象与
        连接原样保留 —— 重启后下一拍数据就回来。
        """
        for task in self._poll_tasks:
            task.cancel()
        for task in self._poll_tasks:
            try:
                await task
            except (asyncio.CancelledError, Exception):  # noqa: BLE001
                pass
        self._poll_tasks = []
        await self.start_polling()

    async def _poll_loop(self, dev, period_ms, label, period_attr=None):
        """单设备轮询：失败只记日志继续（设备恢复后自动跟上，不必重启）。

        失败退避：连续失败时把周期拉长到 1s，避免刷爆日志与总线——现场常见
        「拔了线跑了整晚」，无退避会攒出几百 MB 日志（板端 journald 就是这么
        被灌满的）。

        [period_attr] cfg 上的周期属性名。给了它就**每拍重读**：远程改
        采集频率下一拍直接生效，不必重启任务（周期是纯数字，现读无竞态）。
        """
        period = max(20, period_ms) / 1000.0
        backoff_period = max(period, 1.0)
        failures = 0
        while True:
            started = self._now_ms()
            try:
                await dev.poll(started)
                if failures:
                    # 类别 = alarm：485 通信恢复与"故障"成对，运维要能在
                    # 日志页按 alarm 筛出整条通信质量时间线。
                    self._log_with_store("%s 恢复通讯（此前连续失败 %d 次）"
                                         % (label, failures),
                                         proto.LOG_CAT_ALARM)
                failures = 0
            except Exception as exc:                    # noqa: BLE001
                failures += 1
                # 只在首次与每 60 次失败时打日志（约 1 分钟一条）。
                if failures == 1 or failures % 60 == 0:
                    self._log_with_store("%s 采集失败（连续 %d 次）：%s"
                                         % (label, failures, exc),
                                         proto.LOG_CAT_ALARM)
            if period_attr:
                period = max(proto.CFG_POLL_MS_MIN,
                             int(getattr(self.cfg, period_attr,
                                         period_ms))) / 1000.0
                backoff_period = max(period, 1.0)
            sleep_s = period if failures == 0 else backoff_period
            elapsed = (self._now_ms() - started) / 1000.0
            await asyncio.sleep(max(0.001, sleep_s - elapsed))

    async def stop_polling(self):
        for task in self._poll_tasks:
            task.cancel()
        for task in self._poll_tasks:
            try:
                await task
            except (asyncio.CancelledError, Exception):  # noqa: BLE001
                pass
        self._poll_tasks = []
        # [共享 master 只关一次] 变频器与温湿度共享同一个 RtuMaster，
        # 两个都 close 会重复关同一个串口（第二次是 no-op，但语义混乱）。
        if self.temphum is not None:
            await self.temphum.close()
            self.temphum = None
        if self.flowmeter is not None:
            await self.flowmeter.close()
            self.flowmeter = None
        for tm in self.turbidity:
            await tm.close()
        self.turbidity = []
        self.pump_bank = None
        if self.vfd is not None:
            await self.vfd.close()
            self.vfd = None
        if self.analog is not None:
            await self.analog._master.close()
            self.analog = None
        for dev in self.digital:
            await dev._master.close()
        self.digital = []

    @staticmethod
    def _now_ms():
        return int(time.monotonic() * 1000)

    # ==================================================================
    #                  数据落盘（SQLite：历史/日志/报警）
    # ==================================================================

    #: RUN_STATE_TEXT：与客户端 model.RUN_STATE_TEXT 同口径。两端独立部署
    #: 不能互相 import，故镜像一份（只增不改值，改值必须两端同步）。
    RUN_STATE_TEXT = {0: "未知", 1: "停机", 2: "就绪", 3: "正转运行",
                      4: "反转运行", 5: "正转点动", 6: "反转点动",
                      7: "加速中", 8: "减速中", 9: "恒速运行",
                      10: "直流制动", 11: "自由停车", 12: "快速停车",
                      13: "故障"}
    #: 流量计单位码表：与客户端 model.FLOW_UNIT_TEXT / TOTAL_UNIT_TEXT 同口径。
    FLOW_UNIT_TEXT = {0: "L/s", 1: "L/min", 2: "L/h", 3: "m³/s", 4: "m³/min",
                      5: "m³/h", 6: "kg/s", 7: "kg/min", 8: "kg/h", 9: "t/s",
                      10: "t/min", 11: "t/h", 12: "g/s", 13: "g/min",
                      14: "g/h"}
    TOTAL_UNIT_TEXT = {0: "L", 1: "m³", 2: "kg", 3: "t", 4: "g", 5: "kL"}

    def _log_with_store(self, msg, category=proto.LOG_CAT_SYS):
        """业务日志入口：原始回调 + SQLite 旁路（库未开=空操作）。

        [防递归] 入库失败走 _log_raw 报错，不再进本函数 —— 否则写库失败
        → 打日志 → 又写库 → 死循环。

        [2026-11-15 审计] 入库**异常不能外冒**：本函数是全服务的日志入口，
        被异常处理路径（告警/保护/故障）大量调用 ⇒ 一次入库抛异常（库被
        rotate 掉、磁盘满、句柄失效）会把**正在处理异常的代码**一起打断
        ⇒ 严重的连 tick 都会停（现场表现：整个界面停更，没有任何报错）。
        ⇒ 入库失败只记一条原始日志，不外冒。

        :param category: 运行日志类别（C 端口径 conn/ctrl/alarm/auto/auth；
            缺省 sys = 非上述五类的普通运行日志）。
        """
        self._log_raw(msg)
        try:
            self.store.insert_log(msg, category=category)
        except Exception as exc:                  # noqa: BLE001
            self._log_raw("运行日志入库失败（已忽略）：%s: %s"
                          % (type(exc).__name__, exc))

    def _log_as(self, category):
        """返回一个**定类别**的日志回调（给只接受单参 log(msg) 的子模块用）。

        [为什么要它] 子模块（autocontrol/valves/pumps/buttons…）的 log 回调
        约定是单参 log(msg)；类别是**调用点**的属性（同一条消息在哪一类事情
        里发生），不是消息文本的属性 ⇒ 由装配处按模块语义绑定。
        """
        def _log(msg):
            self._log_with_store(msg, category)
        return _log

    def storage_apply(self):
        """按当前 cfg.storage 开/关/重开数据库（启动与远程改配置共用）。"""
        # 容量限额先灌进去：与连接无关，在线改立刻生效（下一次写前闸就
        # 按新上限/新策略动作）。
        try:
            self.store.configure(
                max_kb=self.cfg.storage.max_kb,
                full_policy=self.cfg.storage.full_policy,
                keep_files=self.cfg.storage.keep_files)
        except (TypeError, ValueError) as exc:
            self._log_raw("落盘容量配置非法（%s）：按不限处理" % exc)
        if self.cfg.storage.enabled:
            self.store.open_for(self.cfg.storage.path)
        else:
            if self.store.db_path:
                self._log_raw("数据落盘已禁用，关闭数据库")
            self.store.close()

    async def start_storage(self):
        """打开数据库（若启用）并启动历史采样任务（启动路径调用一次）。"""
        self.storage_apply()
        self._store_task = asyncio.ensure_future(self._store_loop())

    async def stop_storage(self):
        """停采样任务并关库（退出路径调用）。"""
        if self._store_task is not None:
            self._store_task.cancel()
            try:
                await self._store_task
            except (asyncio.CancelledError, Exception):  # noqa: BLE001
                pass
            self._store_task = None
        self.store.close()

    async def _store_loop(self):
        """历史采样入库。周期从 cfg 现读 —— 远程改采样周期下一拍生效。

        [触发方式 2026-11-12 用户定案] cfg.storage.history_trigger：
          · always（缺省）：持续采集入库（与自动模式无关，保持既有行为）；
          · auto：**仅自动运行时入库** —— 控制器没在跑就不记。
        [只影响落盘] 遥测采集、界面推送、设备健康照旧（界面数据不缺），
        只是历史表里少了"没在自动生产时"的时段。
        """
        while True:
            period_s = max(1, self.cfg.storage.sample_ms) / 1000.0
            await asyncio.sleep(period_s)
            if self.store.db_path is None:
                continue             # 落盘未启用/打开失败：空转，开了自动恢复
            if not self._history_should_store():
                continue             # 仅自动运行时采集：非运行期不入库
            try:
                row = self._history_snapshot()
                # [历史行热身门 2026-11-09] 逐字段记「是否见过有效值」：
                # 服务端刚起的 1~2 秒里 485 变频器还没上报 ⇒ 那行变频器
                # 四列全是 NULL ⇒ 界面整行半屏 "--"（现场截图确认）。
                # NULL＝无数据的口径本身对（设备真掉线就该记 NULL），但
                # "还没开始报"与"报过又掉"要分开：后者该留无数据行。
                # 某字段还没见过有效值之前，带该字段空洞的行不入库；
                # 见过之后照写（设备长期离线也不会让历史表停摆）。
                if not self._history_row_ready(row):
                    continue
                self.store.insert_history(row)
            except Exception as exc:                 # noqa: BLE001
                self._log_raw("历史采样入库异常（不影响采集）：%s: %s"
                              % (type(exc).__name__, exc))


    def _history_should_store(self):
        """这一拍**是否入库**（触发方式判定；2026-11-12 用户定案）。

        · always（缺省）：持续采集 —— 恒 True（与自动模式无关）。
        · auto：仅自动运行时采集 —— 控制器在跑（running）才 True。
        [为什么抽成方法] 判定要能单测（_store_loop 是 while True 的循环）。
        """
        if (self.cfg.storage.history_trigger
                == proto.HISTORY_TRIGGER_AUTO):
            return bool(self.is_auto_running())
        return True

    # 历史行热身门用：每组字段「是否见过有效值」（见 _store_loop 注）。
    _HIST_WARM_GROUPS = (
        ("vfd", ("vfd_state", "freq_hz", "current_a", "rpm")),
        ("temphum", ("temp", "hum")),
        ("turbidity", ("turb1", "turb2")),
        ("flow", ("flow", "flow_total")),
        ("press", ("press_mpa",)),
        ("digital", ("di", "do")),
    )

    def _history_row_ready(self, row):
        """热身门判定：True=可入库；False=启动瞬态（带从未见过有效的空洞）不入库。

        [只拦"该出数的设备"的瞬态 2026-11-16 审计] 热身门原意是"别把启动
        瞬态的空洞行写进库"，但它要求**六组全部**见过有效值 ⇒ 任何一组因
        配置而**永久**无值（停用出水浊度仪、关掉数字量、模拟量未标定、变频器
        未装配…）⇒ **历史表一行都不写、无任何日志**——与板端 2026-11-11
        「历史看不到」同现象的配置性版本，且比那次更隐蔽（那次至少有
        485 全离线的日志线索）。⇒ 每组先问 `_hist_warm_keys`：停用/未装配/
        未标定的组不参与判定（视为已满足）。
        """
        warm = getattr(self, "_hist_warm", None)
        if warm is None:
            warm = set()
            self._hist_warm = warm
        need = set()
        for name, keys in self._HIST_WARM_GROUPS:
            active = self._hist_warm_keys(name, keys)
            if not active:
                warm.add(name)          # 该组没有"该出数的设备" ⇒ 不拦门
                continue
            if any(row.get(k) is None for k in active):
                need.add(name)
            else:
                warm.add(name)
        # 还有"从未见过有效值"的组 ⇒ 这一行是启动瞬态，不入库。
        return not (need - warm)

    def _hist_warm_keys(self, name, keys):
        """该组参与热身判定的字段键；空表 = 该组不拦门。

        [逐台浊度 2026-11-16] 浊度组按**逐台启用**过滤：只采站1 时站2 的
        列永远 NULL（NULL="没数据"是既定口径），不能让它把整组卡死。
        """
        cfg = getattr(self, "cfg", None)
        # [属性访问一律 getattr] 白盒用例（tests/test_hsf_regression.py 的
        # §10 附）用 `object.__new__(App)` 造"只有热身门要看的最小对象"，
        # 设备属性本来就不存在；直接 self.vfd 会 AttributeError 把整段打断
        # （2026-11-17 实测：整个客户端回归在第 1100 行崩掉）。
        vfd = getattr(self, "vfd", None)
        temphum = getattr(self, "temphum", None)
        flowmeter = getattr(self, "flowmeter", None)
        analog = getattr(self, "analog", None)
        digital = getattr(self, "digital", None)
        if name == "vfd":
            if vfd is None or not bool(getattr(cfg, "enable_vfd", True)):
                return ()
            return keys
        if name == "temphum":
            if temphum is None \
                    or not bool(getattr(cfg, "enable_temphum", True)):
                return ()
            return keys
        if name == "turbidity":
            return tuple(k for k, idx in (("turb1", 0), ("turb2", 1))
                         if turbidity_enabled(cfg, idx))
        if name == "flow":
            if flowmeter is None \
                    or not bool(getattr(cfg, "enable_flowmeter", True)):
                return ()
            return keys
        if name == "press":
            # 模拟量停用 / 压差通道未标定 ⇒ press_mpa 永远 None ⇒ 不拦门。
            if analog is None or not bool(getattr(cfg, "enable_analog",
                                                 True)):
                return ()
            try:
                scale = analog.scales.get(0)
            except Exception:                       # noqa: BLE001
                return ()
            if scale is None or not getattr(scale, "configured", False):
                return ()
            return keys
        if name == "digital":
            if not digital \
                    or not bool(getattr(cfg, "enable_digital", True)):
                return ()
            return keys
        return keys

    def _history_snapshot(self):
        """一拍历史快照（dict，键 = store.history 表列名）。

        [与推送同源] 各值从 encode_* 的同一批设备对象取（甚至直接解自己
        编的帧），保证「库里一行 = 界面一拍」可互相印证。设备离线时该列
        不写（入库为 NULL），与界面 "--" 同口径 —— NULL 是"没数据"，
        不是 0。
        """
        # [ts 必须用墙钟，不能用 _now_ms()] `_now_ms` 是 **monotonic**
        # （开机后计时，用于超时判定），写进库里毫无意义：实测入库值是
        # 5153125 ms ≈ 1970-01-01，历史页时间列全显示 1970 年、时间范围
        # 筛选永远命中不了、分页窗口锚点（beforeTs）也整体对不上。
        # 落盘/日志/告警这类"事后要按真实时间查"的数据一律用 epoch 毫秒。
        row = {"ts": int(time.time() * 1000)}
        # ---- 运行模式 ----
        # [子阶段入列 2026-11-13 用户口径「自动*产水/自动*反洗/自动*顺洗」]
        # 自动模式的三种状态要能从历史 mode 列分辨——组合循环运行中读控制
        # 器的**当前子阶段**（self.mode 在循环里被推进为 produce/backwash/
        # rinse），文案「自动·反洗」等；单模式运行中即其本身；未运行给
        # 「自动·停止」（粘性模式仍在但不跑，不写子阶段——那是"上次"的）。
        if self.run_mode == self.MODE_AUTO:
            ctl = getattr(self, "auto_controller", None)
            running = getattr(ctl, "running", None)
            if running:
                stage = str(getattr(ctl, "mode", "") or "")
                stage_names = {"produce": "产水", "backwash": "反洗",
                               "rinse": "顺洗", "drain": "放水"}
                row["mode"] = ("自动·" + stage_names.get(stage, "运行中"))
            elif running is None:
                row["mode"] = "自动·未知"
            else:
                row["mode"] = "自动·停止"
        else:
            row["mode"] = "手动"
        # ---- 变频器（解自己编的遥测帧：encode_vfd 里 valid_mask 已裁键）----
        if self.vfd is not None:
            payload = self.encode_vfd(self.vfd)
            if payload is not None:
                try:
                    doc = bson.decode(payload)
                except ValueError:
                    doc = {}
                row["freq_hz"] = doc.get("outputFreqHz")
                row["current_a"] = doc.get("outputCurrentA")
                row["rpm"] = doc.get("speedRpm")
                if doc.get("faulted"):
                    row["vfd_state"] = "故障 0x%04X" % int(
                        doc.get("faultCode") or 0)
                elif doc.get("runState") is not None:
                    row["vfd_state"] = self.RUN_STATE_TEXT.get(
                        int(doc["runState"]), "未知")
        # ---- 温湿度（tenths → 物理量）----
        if self.temphum is not None:
            doc = bson.decode(self.encode_temphum(self.temphum))
            if doc.get("tempValid"):
                row["temp"] = doc["temp"] / 10.0
            if doc.get("humValid"):
                row["hum"] = doc["hum"] / 10.0
        # ---- 浊度仪（ntu ×100 定点 → 物理量）----
        payload = self.encode_turbidity()
        try:
            doc = bson.decode(payload)
        except ValueError:
            doc = {}
        for idx in range(int(doc.get(proto.KEY_COUNT, 0) or 0)):
            prefix = "v%d_" % idx
            if doc.get(prefix + proto.KEY_TURB_ONLINE):
                row["turb%d" % (idx + 1)] = (
                    int(doc[prefix + proto.KEY_TURB_NTU]) / 100.0)
                # [水温 2026-11-10] 带温度的机型（浙仪 ZHEYI-5100ZDY）随帧带
                # "v{i}_temp"（℃×100）；**缺键 = 该机型没有温度**，不写列
                # ⇒ 入库 NULL、界面 "--"（写 0℃ 会被当成真实读数）。
                # 多台都有温度时后面的台覆盖前面的（口径=「出水浊度仪 的水温」）。
                if (prefix + proto.KEY_TURB_TEMP) in doc:
                    row["water_temp"] = float(
                        int(doc[prefix + proto.KEY_TURB_TEMP])) / 100.0
        # ---- 流量计 ----
        if self.flowmeter is not None:
            payload = self.encode_flowmeter(self.flowmeter)
            try:
                doc = bson.decode(payload)
            except ValueError:
                doc = {}
            if doc.get(proto.KEY_VALID):
                row["flow"] = doc.get("flow")
                row["flow_unit"] = self.FLOW_UNIT_TEXT.get(
                    int(doc.get("flowUnit", 0xFF)), "?")
                row["flow_total"] = doc.get("forwardTotal")
                row["total_unit"] = self.TOTAL_UNIT_TEXT.get(
                    int(doc.get("totalUnit", 0xFF)), "?")
        # ---- 压差（模拟量通道 0 的工程量 + 越限说明）----
        ai = self.analog
        if ai is not None and ai.valid:
            scale = self.cfg.analog.scales.get(0)
            if scale is not None and scale.configured and ai.valid[0]:
                value, note = scale.engineering(ai.ma(0))
                row["press_mpa"] = value
                row["press_note"] = note or ""
        # ---- 数字量位图（取在线模块中点数最多者；现场只有 .13 一台）----
        best = None
        for dev in self.digital:
            if getattr(dev, "online", False) and (
                    best is None or getattr(dev, "count", 0)
                    > getattr(best, "count", 0)):
                best = dev
        if best is not None:
            # di/do 用 mask 方法（bit n = 通道 n 的位图 int）；设备对象上的
            # .di/.do 是逐点布尔列表，直接 int() 会 TypeError。
            row["di"] = int(best.di_mask())
            row["do"] = int(best.do_mask())
        return row

    # ==================================================================
    #                  远程配置（采集项 / 落盘；需 root）
    # ==================================================================

    @staticmethod
    def _clamp_poll_ms(value):
        """轮询周期夹到允许区间（越界不报错，夹回——配置错不该让采集停摆）。"""
        return max(proto.CFG_POLL_MS_MIN,
                   min(proto.CFG_POLL_MS_MAX, int(value)))

    def _config_collect_doc(self):
        """采集配置快照（GET 应答用；键与 CMD_CONFIG_SET 一致）。"""
        c = self.cfg
        return {
            proto.CFGK_VFD_ENABLED: bool(c.enable_vfd),
            proto.CFGK_TEMPHUM_ENABLED: bool(c.enable_temphum),
            proto.CFGK_FLOWMETER_ENABLED: bool(c.enable_flowmeter),
            # [旧键报"任一启用"] 旧客户端把它当"浊度这路开没开"；逐台值
            # 下面的两个键给新客户端。
            proto.CFGK_TURBIDITY_ENABLED: bool(turbidity_enabled(c, 0)
                                               or turbidity_enabled(c, 1)),
            # [逐台开关] 两台独立设备各报各的。
            proto.CFGK_TURBIDITY1_ENABLED: turbidity_enabled(c, 0),
            proto.CFGK_TURBIDITY2_ENABLED: turbidity_enabled(c, 1),
            proto.CFGK_ANALOG_ENABLED: bool(c.enable_analog),
            proto.CFGK_DIGITAL_ENABLED: bool(c.enable_digital),
            proto.CFGK_VFD_POLL_MS: int(c.vfd_poll_ms),
            proto.CFGK_TEMPHUM_POLL_MS: int(c.temphum_poll_ms),
            proto.CFGK_TURBIDITY_POLL_MS: int(c.turbidity_poll_ms),
            proto.CFGK_TURBIDITY1_POLL_MS: int(getattr(
                c, "turbidity1_poll_ms", c.turbidity_poll_ms)),
            proto.CFGK_TURBIDITY2_POLL_MS: int(getattr(
                c, "turbidity2_poll_ms", c.turbidity_poll_ms)),
            proto.CFGK_FLOWMETER_POLL_MS: int(getattr(
                c, "flowmeter_poll_ms", 0) or c.temphum_poll_ms),
            proto.CFGK_ANALOG_POLL_MS: int(c.analog_poll_ms),
            proto.CFGK_DIGITAL_POLL_MS: int(c.digital_poll_ms),
            # [压差计/温度传感器通道与量程 2026-11-20 远端可配；二改：
            #  量程=下限~上限（4mA→下限、20mA→上限），「温度满量程」废弃]
            proto.CFGK_ANALOG_PRESS_CHANNEL: int(c.analog.press_channel),
            proto.CFGK_ANALOG_PRESS_LOW_MPA: float(c.analog.press_low_mpa),
            proto.CFGK_ANALOG_PRESS_HIGH_MPA: float(c.analog.press_high_mpa),
            proto.CFGK_ANALOG_PRESS_BELOW4_VALID:
                bool(c.analog.press_below4_valid),
            proto.CFGK_ANALOG_TEMP_CHANNEL: int(c.analog.temp_channel),
            proto.CFGK_ANALOG_TEMP_LOW_C: float(c.analog.temp_low_c),
            proto.CFGK_ANALOG_TEMP_HIGH_C: float(c.analog.temp_high_c),
            proto.CFGK_ANALOG_TEMP_BELOW4_VALID:
                bool(c.analog.temp_below4_valid),
            # [模块路数 2026-11-20] 客户端把通道号做成下拉（0..路数-1）
            # 用的元数据；缺键（老服务端）客户端回落 8 路。
            "analogChannels": int(c.analog.channels),
        }

    def _camera_settings(self):
        """相机采集器的运行参数（从 cfg 取；采集器按这份参数起停）。"""
        c = self.cfg
        return {
            "enabled": bool(c.camera_enabled),
            "ip": str(c.camera_ip or ""),
            "port": int(c.camera_port),
            "username": str(c.camera_username or ""),
            "password": str(c.camera_password or ""),
            "stream": str(c.camera_stream or proto.CAMERA_STREAM_DEF),
            "fps": int(c.camera_fps),
            "width": int(c.camera_width),
            "quality": int(c.camera_quality),
            "timeoutMs": int(c.camera_timeout_ms),
            "transport": str(getattr(c, "camera_transport",
                                     proto.CAMERA_TRANSPORT_DEF)),
            "codec": str(getattr(c, "camera_codec", proto.CAMERA_CODEC_H264)),
            # [2026-11-20 MAC 连接 + 断线重连] 采集器要的四件套（MAC 模式
            # 下 ip 降级为"网段提示"）。
            "resolveMode": str(getattr(c, "camera_resolve_mode",
                                       proto.CAMERA_RESOLVE_DEF)),
            "mac": str(getattr(c, "camera_mac", "")),
            "reconnectEnabled": bool(getattr(
                c, "camera_reconnect_enabled",
                proto.CAMERA_RECONNECT_ENABLED_DEF)),
            "reconnectIntervalS": int(getattr(
                c, "camera_reconnect_interval_s",
                proto.CAMERA_RECONNECT_INTERVAL_S_DEF)),
        }

    def _config_camera_doc(self):
        """相机配置快照（GET 应答用）。

        [口令不回显明文] 应答里给的是**是否已设**（hasPassword），不给明文
        —— 远端设置页回显时只显示「已设置」，留空则不改。
        """
        c = self.cfg
        doc = {
            proto.CFGK_CAMERA_ENABLED: bool(c.camera_enabled),
            proto.CFGK_CAMERA_IP: str(c.camera_ip or ""),
            proto.CFGK_CAMERA_PORT: int(c.camera_port),
            proto.CFGK_CAMERA_USERNAME: str(c.camera_username or ""),
            proto.CFGK_CAMERA_PASSWORD: "",
            "hasPassword": bool(c.camera_password),
            proto.CFGK_CAMERA_STREAM: str(c.camera_stream),
            proto.CFGK_CAMERA_FPS: int(c.camera_fps),
            proto.CFGK_CAMERA_WIDTH: int(c.camera_width),
            proto.CFGK_CAMERA_QUALITY: int(c.camera_quality),
            proto.CFGK_CAMERA_TIMEOUT_MS: int(c.camera_timeout_ms),
            # [2026-11-19 ES 直传] 三新键（transport/codec/hwDecode）。
            proto.CFGK_CAMERA_TRANSPORT: str(getattr(
                c, "camera_transport", proto.CAMERA_TRANSPORT_DEF)),
            proto.CFGK_CAMERA_CODEC: str(getattr(
                c, "camera_codec", proto.CAMERA_CODEC_H264)),
            proto.CFGK_CAMERA_HW_DECODE: str(getattr(
                c, "camera_hw_decode", proto.CAMERA_HW_DEF)),
            # [2026-11-20 MAC 连接 + 断线重连]
            proto.CFGK_CAMERA_RESOLVE_MODE: str(getattr(
                c, "camera_resolve_mode", proto.CAMERA_RESOLVE_DEF)),
            proto.CFGK_CAMERA_MAC: str(getattr(c, "camera_mac", "")),
            proto.CFGK_CAMERA_RECONNECT_ENABLED: bool(getattr(
                c, "camera_reconnect_enabled",
                proto.CAMERA_RECONNECT_ENABLED_DEF)),
            proto.CFGK_CAMERA_RECONNECT_INTERVAL_S: int(getattr(
                c, "camera_reconnect_interval_s",
                proto.CAMERA_RECONNECT_INTERVAL_S_DEF)),
            # [实况回显] 在线状态与已出帧数（客户端据此显示"相机离线"）。
            "online": bool(self._camera_status.get("online")),
            "frames": int(self._camera_status.get("frames") or 0),
            "error": str(self._camera_status.get("error") or ""),
        }
        # [实际编码回传] SDP 探测的 actualCodec ⇒ 客户端选解码器、UI 显示
        # 「编码 H.265」。缺省给配置期望值（探测前不撒谎成另一种编码）。
        doc["actualCodec"] = str(self._camera_status.get("codec")
                                 or getattr(c, "camera_codec",
                                            proto.CAMERA_CODEC_H264))
        if self._camera_status.get("transport"):
            doc["actualTransport"] = str(self._camera_status["transport"])
        if self._camera_status.get("dropped") is not None:
            doc["dropped"] = int(self._camera_status["dropped"])
        # [MAC 连接回显] 最近解析到的 IP（界面状态行「MAC → IP」）。
        if self._camera_status.get("resolvedIp"):
            doc["resolvedIp"] = str(self._camera_status["resolvedIp"])
        return doc

    def start_camera(self):
        """起相机采集（远端设置里启用后调用；改配置也走这里重启）。

        [MAC 模式 2026-11-20] resolveMode=mac 时 **ip 允许为空**（它降级为
        "网段提示"：解析不到时的扫描种子，再退到本机网段）。
        """
        settings = self._camera_settings()
        ip_mode = settings.get("resolveMode") != proto.CAMERA_RESOLVE_MAC
        if not settings["enabled"] or (ip_mode and not settings["ip"]):
            self.stop_camera()
            return
        if self.camera is None:
            self.camera = camera_mod.CameraCollector(
                settings, log=lambda msg: self._log("相机：%s" % msg))
            self.camera.start()
            self._log("相机采集已启动：%s:%d/%s（%s 流，%d fps，%dpx，q=%d）"
                      % (settings.get("ip") or
                         ("MAC " + settings.get("mac", "")),
                         settings["port"], settings["username"],
                         settings["stream"], settings["fps"], settings["width"],
                         settings["quality"]))
        else:
            self.camera.apply_settings(settings)

    def stop_camera(self):
        """停相机采集（配置关掉 / 服务退出时）。"""
        if self.camera is not None:
            try:
                self.camera.stop()
            except Exception as exc:                # noqa: BLE001
                self._log("相机采集停止异常：%s" % exc)
            self.camera = None
            self._camera_published_seq = -1
        # [订阅沿判据一并复位 2026-11-19 审查实修] 重启采集后，已开着的
        # 页面算**新订阅者**（要 GOP 自举）——不复位的话 _camera_subs_last
        # 还是正数 ⇒ 重启后的首拍被当"老订阅者"，页面拿不到 GOP 黑屏到
        # 下一次订阅沿。（无条件复位：连 stop 两次也对。）
        self._camera_subs_last = 0

    def _camera_fragments(self, frame, extra_flags=0, frame_id=None,
                          width=None, height=None):
        """把一帧负载切成 XLink 可发的小片（**片头 + 片数据**）。

        :param frame: CameraFrame（jpeg 模式；data/seq/width/height 生效）。
        :param extra_flags: 附加位（ES 模式传 CAM_FRAME_FLAG_ES|H265 位）。
        :param frame_id: ES 模式的 NAL 序号（缺省用 frame.seq）。
        :param width/height: 缺省用 frame 自带宽高；ES 模式显式传 0
            （尺寸客户端解码后自知）。

        [为什么必须切] XLink 单帧载荷上限 16KB（XLINK_MAX_PAYLOAD）⇒
        一张 704² JPEG（板端实测稳态 47~91KB、峰值 104KB）塞不进；
        **超限不是丢一帧，而是让客户端 decode_header 返回 None ⇒ 整会话
        断链** ⇒ 必须按**峰值**设计分片（ES 模式下大 IDR 同理，也是 NAL
        切片而不是整条发）。

        [片头格式] magic(2) frameId(4) seq(2) total(2) width(2) height(2)
                    flags(2) = **16 字节**，**小端**（三端一致）。
        """
        data = frame.data
        if not data:
            return []
        total = max(1, (len(data) + proto.CAM_CHUNK_MAX - 1)
                    // proto.CAM_CHUNK_MAX)
        fid = (frame.seq if frame_id is None else frame_id) & 0xFFFFFFFF
        w = frame.width if width is None else width
        h = frame.height if height is None else height
        out = []
        for seq in range(total):
            part = data[seq * proto.CAM_CHUNK_MAX:
                        (seq + 1) * proto.CAM_CHUNK_MAX]
            flags = extra_flags
            if seq == 0:
                flags |= proto.CAM_FRAME_FLAG_FIRST
            if seq == total - 1:
                flags |= proto.CAM_FRAME_FLAG_LAST
            header = struct.pack("<HIHHHHH",
                                  proto.CAM_FRAME_MAGIC,
                                  fid,
                                  seq, total,
                                  w & 0xFFFF,
                                  h & 0xFFFF,
                                  flags)
            out.append(header + part)
        return out

    async def _publish_camera_frame(self):
        """把相机负载**分片**推给订阅者。

        [只在有人看相机页时才推 —— 用户 2026-11-19 口径] 「推送相机帧，只在
        客户端显示打开了那个相机界面要看画面的时候才推送」⇒ **没有任何会话
        订阅相机主题时，一帧都不推**（连切片都不做）。
        [采集不受影响] 采集照常（服务端与相机同局域网，采集成本可忽略），
        这样客户端打开相机页时**立刻**有画面可看（不用等采集器起流）。
        [覆盖式丢帧] 客户端慢时，XLink 发送缓冲超限会丢这一片 ⇒ 该帧自然
        作废（客户端组帧时发现缺片就丢整帧）⇒ **不会堆积**。

        [ES 直传分支（transport=stream，缺省）] 发布单位 = **NAL**：
          · 订阅人数 0→正：先发**最近完整 GOP**（参数集+IDR…，新订阅者
            从这里自举解码），再发增量——否则新看的人要等相机下一个
            GOP（2s 级）才出首帧；
          · 订阅持续为正：只发自上拍以来的增量 NAL；
          · 订阅为 0：不发（采集器侧 pending 照常积攒，超限整队作废）。
          [为什么发整 GOP 不心疼带宽] 子码流 512Kbps，一个 GOP（~2s）
          ≈128KB，只发生在「0→1 个订阅者」的沿上，且多订阅者共享
          （发一次各会话各拿一份，XLink publish 就是按会话扇出）。
        """
        if self.camera is None:
            return
        if self.server is None:
            return
        count = self.server.topic_subscriber_count(proto.TOPIC_CAMERA_FRAME)
        if self._camera_es_mode():
            await self._publish_camera_es(count)
            return
        if count <= 0:
            # 没人看 ⇒ 不推（也不更新已推序号：等有人看了要立刻补最新帧）
            return
        frame = self.camera.latest_frame()
        if frame is None:
            return
        if frame.seq == self._camera_published_seq:
            return
        self._camera_published_seq = frame.seq
        for chunk in self._camera_fragments(frame):
            await self.server.publish(proto.TOPIC_CAMERA_FRAME, chunk,
                                      data_type=proto.DATATYPE_RAW)

    def _camera_es_mode(self):
        """当前采集器是否 ES 直传（照 settings 而不是缓存标志，防漂移）。"""
        if not getattr(self, "camera", None):
            return False
        settings = getattr(self.camera, "settings", None) or {}
        return str(settings.get("transport")
                   or proto.CAMERA_TRANSPORT_DEF) \
            == proto.CAMERA_TRANSPORT_STREAM

    async def _publish_camera_es(self, count):
        """ES 直传的按需扇出（见 `_publish_camera_frame` 的分支说明）。"""
        if count <= 0:
            self._camera_subs_last = 0
            # [积压必须丢弃 2026-11-19 审查实修] 无人看时 pending 若只是
            # 攒着（采集器侧涨到 512KB 上限才自截），等有人订阅时会把
            # **订阅前几分钟的过期 NAL** 一股脑发出去（白耗 0.5MB 带宽、
            # 客户端解码器还要全吃再丢）。自举靠 GOP 缓冲就够了 ⇒ 无订阅
            # 拍直接 drain 丢弃，订阅沿只发「GOP + 订阅后的新 NAL」。
            self.camera.drain_nals()
            return
        gop = self.camera.gop_nals()
        pend = self.camera.drain_nals()
        first_join = self._camera_subs_last <= 0
        self._camera_subs_last = count
        # [先 GOP 后增量] 新订阅者从参数集+IDR 自举；老订阅者只吃增量。
        # （老订阅者重复收 GOP 会把解码器冲乱 ⇒ 只在 0→正 的沿上发。）
        nals = (list(gop) if first_join else []) + list(pend)
        if not nals:
            return
        h265 = (self.camera.detected_codec()
                == proto.CAMERA_CODEC_H265)
        flags = proto.CAM_FRAME_FLAG_ES
        if h265:
            flags |= proto.CAM_FRAME_FLAG_H265
        for nal in nals:
            self._camera_published_seq = (self._camera_published_seq + 1) \
                & 0xFFFFFFFF
            for chunk in self._camera_fragments(
                    _NalFrame(nal, self._camera_published_seq),
                    extra_flags=flags):
                await self.server.publish(proto.TOPIC_CAMERA_FRAME, chunk,
                                          data_type=proto.DATATYPE_RAW)

    def _config_storage_doc(self):
        """落盘配置快照（GET 应答用）。path 为**配置值**（空=程序目录）；
        实际库文件路径与用量另给 storageDbPath / storageStats。"""
        s = self.cfg.storage
        return {
            proto.CFGK_STORAGE_ENABLED: bool(s.enabled),
            proto.CFGK_STORAGE_PATH: s.path or "",
            proto.CFGK_STORAGE_SAMPLE_MS: int(s.sample_ms),
            proto.CFGK_STORAGE_MAX_MB: int(s.max_mb),
            proto.CFGK_STORAGE_FULL_POLICY: s.full_policy,
            proto.CFGK_STORAGE_KEEP_FILES: int(s.keep_files),
            # [历史入库触发方式 2026-11-12]
            proto.CFGK_STORAGE_HISTORY_TRIGGER:
                str(s.history_trigger or proto.HISTORY_TRIGGER_ALWAYS),
        }

    def _storage_stats_doc(self):
        """落盘用量实况（界面进度条/到限提示用；库未开也给零值）。"""
        stats = self.store.stats()
        # 加上配置侧的有效值（库未打开时 store 里没有这些读数）。
        stats["enabled"] = bool(self.cfg.storage.enabled)
        stats["dbPath"] = self.store.db_path or ""
        return stats

    def _config_login_doc(self):
        """登录/账户策略快照（GET 应答用；与 C 侧 login 节同键同义）。"""
        c = self.cfg
        return {
            proto.CFGK_LOGIN_ALLOW_ANON: bool(c.login_allow_anonymous),
            proto.CFGK_LOGIN_ALLOW_CONCUR: bool(c.login_allow_concurrent),
            proto.CFGK_LOGIN_MAX_SESSIONS: int(c.login_max_sessions),
        }

    def _config_protect_doc(self):
        """运行保护配置快照（GET 应答用；protect 节；2026-11-15）。

        [键名与 C 侧 HSF_CFGK_PROTECT_* 逐字同值] 两端同一份语义：
          · inletNoFlowTimeoutMs：进水泵开着且流量计仍报空管，持续这么久
            ⇒ 保护动作（0 = 关闭该保护）。
          · inletNoFlowStop：动作是**停止设备**（true）还是只报警（false）。
        """
        c = self.cfg
        return {
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS:
                int(c.protect_inlet_noflow_timeout_ms),
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP:
                bool(c.protect_inlet_noflow_stop),
            # [压差过大保护 2026-11-15]
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED:
                bool(c.protect_press_high_enabled),
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA:
                float(c.protect_press_high_limit_mpa),
            proto.CFGK_PROTECT_PRESS_HIGH_STOP:
                bool(c.protect_press_high_stop),
            # [电机超温保护 2026-11-20] 温度传感器（模拟量，通道可配）连续
            # 超过阈值 ⇒ 报警（锁存）+（按配置）停设备；缺省**不启用**
            # （阈值与现场电机强相关，写死必误动作 ⇒ 现场自己定）。
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED:
                bool(c.protect_motor_over_temp_enabled),
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C:
                float(c.protect_motor_over_temp_limit_c),
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP:
                bool(c.protect_motor_over_temp_stop),
        }

    def _cfg_protect_value(self, key, value):
        """把运行保护的**一个键**的载荷值转成规整值（非法值抛 ValueError）。

        [纯函数：不写任何状态] 配合 `_cfg_set_protect_key` 做**先校验后
        写入**：一包里既有合法键又有非法键时，不能"前面几个已经写进去了、
        后面那个报错" ⇒ 半截配置（现场最难查的一种）。
        """
        if key == proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数毫秒" % key)
            return max(proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN,
                       min(proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX, val))
        if key == proto.CFGK_PROTECT_INLET_NOFLOW_STOP:
            return strict_bool(value, key)
        if key == proto.CFGK_PROTECT_PRESS_HIGH_ENABLED:
            return strict_bool(value, key)
        if key == proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA:
            try:
                val = float(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要数字（MPa）" % key)
            return max(proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MIN,
                       min(proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MAX, val))
        if key == proto.CFGK_PROTECT_PRESS_HIGH_STOP:
            return strict_bool(value, key)
        if key == proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED:
            return strict_bool(value, key)
        if key == proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C:
            try:
                val = float(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要数字（℃）" % key)
            return max(proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MIN,
                       min(proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MAX, val))
        if key == proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP:
            return strict_bool(value, key)
        raise ValueError("未知的运行保护配置键：%s" % key)

    def _cfg_set_protect_key(self, key, value):
        """写运行保护的一个键（非法值抛 ValueError ⇒ 应答 fail-closed）。

        [为什么逐键校验而不是整节覆盖] 保护动作会**停设备**，写错一个键
        可能把"只报警"变成"直接停线" ⇒ 非法值明确报错、不静默接受。
        """
        # 先算值（未知键/非法值在这里抛 ValueError）⇒ 再落盘属性。
        val = self._cfg_protect_value(key, value)
        setattr(self.cfg, self._PROTECT_ATTR[key], val)

    #: 运行保护协议键 → cfg 属性名（写盘与读盘共用一份映射，避免两处手写
    #: 键名对不上）。
    _PROTECT_ATTR = {
        proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS:
            "protect_inlet_noflow_timeout_ms",
        proto.CFGK_PROTECT_INLET_NOFLOW_STOP: "protect_inlet_noflow_stop",
        proto.CFGK_PROTECT_PRESS_HIGH_ENABLED: "protect_press_high_enabled",
        proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA:
            "protect_press_high_limit_mpa",
        proto.CFGK_PROTECT_PRESS_HIGH_STOP: "protect_press_high_stop",
        proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED:
            "protect_motor_over_temp_enabled",
        proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C:
            "protect_motor_over_temp_limit_c",
        proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP:
            "protect_motor_over_temp_stop",
    }

    #: 相机协议键 → cfg 属性名（写盘/读盘共用一份映射）。
    _CAMERA_ATTR = {
        proto.CFGK_CAMERA_ENABLED: "camera_enabled",
        proto.CFGK_CAMERA_IP: "camera_ip",
        proto.CFGK_CAMERA_PORT: "camera_port",
        proto.CFGK_CAMERA_USERNAME: "camera_username",
        proto.CFGK_CAMERA_PASSWORD: "camera_password",
        proto.CFGK_CAMERA_STREAM: "camera_stream",
        proto.CFGK_CAMERA_FPS: "camera_fps",
        proto.CFGK_CAMERA_WIDTH: "camera_width",
        proto.CFGK_CAMERA_QUALITY: "camera_quality",
        proto.CFGK_CAMERA_TIMEOUT_MS: "camera_timeout_ms",
        # [2026-11-19 ES 直传]
        proto.CFGK_CAMERA_TRANSPORT: "camera_transport",
        proto.CFGK_CAMERA_CODEC: "camera_codec",
        proto.CFGK_CAMERA_HW_DECODE: "camera_hw_decode",
        # [2026-11-20 MAC 连接 + 断线重连]
        proto.CFGK_CAMERA_RESOLVE_MODE: "camera_resolve_mode",
        proto.CFGK_CAMERA_MAC: "camera_mac",
        proto.CFGK_CAMERA_RECONNECT_ENABLED: "camera_reconnect_enabled",
        proto.CFGK_CAMERA_RECONNECT_INTERVAL_S:
            "camera_reconnect_interval_s",
    }

    def _cfg_camera_value(self, key, value):
        """相机配置键的值（**纯函数**：非法值抛 ValueError ⇒ fail-closed）。

        [为什么逐键严格校验] 相机配错 ⇒ 画面全黑/连不上，现场分不清是
        "相机坏了"还是"配错了" ⇒ 非法值明确报错，不静默回落缺省。
        """
        if key == proto.CFGK_CAMERA_ENABLED:
            return strict_bool(value, key)
        if key == proto.CFGK_CAMERA_RESOLVE_MODE:
            # [2026-11-20 MAC 连接] ip=按 IP 直连（缺省）；mac=动态 IP 相机
            # 按 MAC 解析（服务端连接前把 MAC 换成当前 IP）。
            mode = str(value or "").strip()
            if mode not in proto.CAMERA_RESOLVE_CHOICES:
                raise ValueError("未知相机连接方式：%r（可选 ip / mac）"
                                 % mode)
            return mode
        if key == proto.CFGK_CAMERA_MAC:
            # 任意常见写法归一成小写冒号；不合法明确拒（连不上最难排查）。
            # [空串合法 2026-11-20 实锅] 按 IP 模式根本不用 MAC、想清空也
            # 该允许 ⇒ 空串原样收（MAC 模式没填 MAC 由采集器报可读错误，
            # 不在协议面卡死——否则按 IP 的应用永远过不去）。
            from hsfsrv import camera_mac as _cmac
            if str(value or "").strip() == "":
                return ""
            norm = _cmac.normalize_mac(value)
            if not norm:
                raise ValueError("相机 MAC 格式不对：%r（如 "
                                 "AA:BB:CC:DD:EE:FF）" % (value,))
            return norm
        if key == proto.CFGK_CAMERA_RECONNECT_ENABLED:
            return strict_bool(value, key)
        if key == proto.CFGK_CAMERA_RECONNECT_INTERVAL_S:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数秒" % key)
            if not (proto.CAMERA_RECONNECT_INTERVAL_S_MIN <= val
                    <= proto.CAMERA_RECONNECT_INTERVAL_S_MAX):
                raise ValueError("%s 越界：%d（%d ~ %d 秒）"
                                 % (key, val,
                                    proto.CAMERA_RECONNECT_INTERVAL_S_MIN,
                                    proto.CAMERA_RECONNECT_INTERVAL_S_MAX))
            return val
        if key == proto.CFGK_CAMERA_IP:
            ip = str(value or "").strip()
            if ip and not _looks_like_host(ip):
                raise ValueError("%s 不是合法的 IP 或主机名：%r" % (key, ip))
            return ip
        if key == proto.CFGK_CAMERA_PORT:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数端口" % key)
            if not (1 <= val <= 65535):
                raise ValueError("%s 端口越界：%d（1~65535）" % (key, val))
            return val
        if key == proto.CFGK_CAMERA_USERNAME:
            return str(value or "").strip()
        if key == proto.CFGK_CAMERA_PASSWORD:
            # [空串在 setter 里被当作"不改"] GET 应答**不回显明文** ⇒ 客户端
            # 回填的 password 恒是空 ⇒ 若把空串当真值写下去，**一存就把
            # 口令抹了**（现场直接连不上相机）。⇒ 空 = 不改。
            return str(value or "")
        if key == proto.CFGK_CAMERA_STREAM:
            val = str(value or "").strip()
            if val not in proto.CAMERA_STREAM_CHOICES:
                raise ValueError("未知相机码流档：%r（可选 main / minor）" % val)
            return val
        if key == proto.CFGK_CAMERA_FPS:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数帧率" % key)
            if not (proto.CAMERA_FPS_MIN <= val <= proto.CAMERA_FPS_MAX):
                raise ValueError("%s 帧率越界：%d（%d~%d）"
                                 % (key, val, proto.CAMERA_FPS_MIN,
                                    proto.CAMERA_FPS_MAX))
            return val
        if key == proto.CFGK_CAMERA_WIDTH:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数像素" % key)
            if not (proto.CAMERA_WIDTH_MIN <= val <= proto.CAMERA_WIDTH_MAX):
                raise ValueError("%s 帧宽越界：%d（%d~%d）"
                                 % (key, val, proto.CAMERA_WIDTH_MIN,
                                    proto.CAMERA_WIDTH_MAX))
            return val
        if key == proto.CFGK_CAMERA_QUALITY:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数质量" % key)
            if not (proto.CAMERA_QUALITY_MIN <= val <= proto.CAMERA_QUALITY_MAX):
                raise ValueError("%s 质量越界：%d（%d~%d，**越大越清晰**）"
                                 % (key, val, proto.CAMERA_QUALITY_MIN,
                                    proto.CAMERA_QUALITY_MAX))
            return val
        if key == proto.CFGK_CAMERA_TIMEOUT_MS:
            try:
                val = int(value)
            except (TypeError, ValueError):
                raise ValueError("%s 需要整数毫秒" % key)
            if not (500 <= val <= 60000):
                raise ValueError("%s 超时越界：%d（500~60000ms）" % (key, val))
            return val
        if key == proto.CFGK_CAMERA_TRANSPORT:
            val = str(value or "").strip()
            if val not in proto.CAMERA_TRANSPORT_CHOICES:
                raise ValueError("未知相机传输模式：%r（可选 stream / jpeg）"
                                 % val)
            return val
        if key == proto.CFGK_CAMERA_CODEC:
            val = str(value or "").strip()
            if val not in proto.CAMERA_CODEC_CHOICES:
                raise ValueError("未知相机编码：%r（可选 h264 / h265）" % val)
            return val
        if key == proto.CFGK_CAMERA_HW_DECODE:
            val = str(value or "").strip()
            if val not in proto.CAMERA_HW_CHOICES:
                raise ValueError("未知解码方式：%r（可选 auto / on / off）"
                                 % val)
            return val
        raise ValueError("未知的相机配置键：%s" % key)

    def _cfg_set_camera_key(self, key, value):
        """写相机配置的一个键（非法值抛 ValueError ⇒ 应答 fail-closed）。"""
        val = self._cfg_camera_value(key, value)
        if key == proto.CFGK_CAMERA_PASSWORD and not val:
            return          # 空口令 = 不改（不回显明文 ⇒ 空是常态）
        setattr(self.cfg, self._CAMERA_ATTR[key], val)

    @staticmethod
    def _cycle_stage_entry_check(mid, val):
        """校验一个 stageCfg 条目 ⇒ 返回 (entry, err)。

        :param mid: 模式 id（用于报错文案）。
        :param val: 条目内容（对象）。
        :return: (entry_dict_or_None, err_text)——出错时 entry 为 None。

        [fail-closed] 协议面是**逐字段严格校验**的：写错一个键可能把
        "压差大于才切"变成"小于才切"（= 流程反了）⇒ 宁可拒，不能静默
        接受（静默接受＝悄悄换掉工艺）。
        """
        if not isinstance(val, dict):
            return None, ("%s[%s] 需要对象（压差/定时条件）"
                          % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid))
        entry = {}
        for pkey, fkey, kind in proto.AUTO_CYCLE_STAGE_FIELDS:
            if pkey in val:
                src = val[pkey]
            elif fkey in val:
                src = val[fkey]
            else:
                continue
            if kind == "bool":
                try:
                    entry[fkey] = strict_bool(src, pkey)
                except ValueError as exc:
                    return None, str(exc)
            elif kind == "float":
                if isinstance(src, bool):
                    return None, ("%s[%s].%s 需要数字（MPa）"
                                  % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid,
                                     pkey))
                try:
                    num = float(src)
                except (TypeError, ValueError):
                    return None, ("%s[%s].%s 需要数字（MPa）"
                                  % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid,
                                     pkey))
                entry[fkey] = max(0.0, min(proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX,
                                           num))
            elif kind == "int":
                if isinstance(src, bool):
                    return None, ("%s[%s].%s 需要整数分钟"
                                  % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid,
                                     pkey))
                try:
                    num = int(src)
                except (TypeError, ValueError):
                    return None, ("%s[%s].%s 需要整数分钟"
                                  % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid,
                                     pkey))
                entry[fkey] = max(proto.AUTO_CYCLE_MINUTES_MIN,
                                  min(proto.AUTO_CYCLE_MINUTES_MAX, num))
            elif kind == "str":
                text = str(src or "").strip().lower()
                if text not in proto.AUTO_PRESS_COMPARE_KEYS:
                    return None, ("%s[%s].%s 只能是 %s"
                                  % (proto.CFG_AUTO_CYCLE_STAGE_CFG, mid,
                                     pkey,
                                     "/".join(proto.AUTO_PRESS_COMPARE_KEYS)))
                entry[fkey] = text
        return entry, ""

    def _cfg_set_auto_cycle(self, cycle_doc):
        """写自动循环配置（cycle 子节）。返回 ""（成功）或错误文本。

        [逐键校验 · fail-closed] 非法值明确报错、不静默接受：写错一个键
        可能把"压差大于才切"变成"小于才切"（= 流程反了）。

        [2026-11-18 每阶段切换条件] 新键 `stageCfg`
        （{模式 id: {pressEnabled/pressThreshold/pressCompare/timerEnabled/
        timerMinutes}}）是**唯一事实源**；旧键与兼容视图 `stageMinutes`
        折进它的对应字段：
          · pressThreshold / pressCompare / timerFallback / timerMinutes
            ⇒ **列表首元素**（＝旧口径的"产水阶段"）；
          · backwashMinutes / rinseMinutes ⇒ 对应模式的 timerMinutes；
          · stageMinutes ⇒ 各模式的 timerMinutes（**只改分钟数**，不动
            "启不启用定时"那个开关 ⇒ 不覆盖用户在 stageCfg 里的显式选择）。
        旧键本身**继续写盘**（由 stageCfg 派生）⇒ 老读方不受影响。
        """
        c = self.cfg.auto_cycle
        # ---- ① 参与循环的模式列表（有序；fail-closed）----
        modes = list(c.get("cycle_modes") or [])
        if proto.CFG_AUTO_CYCLE_MODES in cycle_doc:
            raw = cycle_doc[proto.CFG_AUTO_CYCLE_MODES]
            if not isinstance(raw, (list, tuple)):
                return "%s 需要数组（模式 id 列表）" % proto.CFG_AUTO_CYCLE_MODES
            clean = []
            for item in raw:
                if not isinstance(item, str):
                    return ("%s 的元素必须是模式 id 字符串" %
                            proto.CFG_AUTO_CYCLE_MODES)
                mid = item.strip()
                if mid not in proto.AUTO_CYCLE_MODE_POOL:
                    return "%s 含未知模式：%s" % (proto.CFG_AUTO_CYCLE_MODES, item)
                if mid in clean:
                    return "%s 有重复模式：%s" % (proto.CFG_AUTO_CYCLE_MODES, mid)
                clean.append(mid)
            if len(clean) < 2:
                # [文案对齐行为 2026-11-18] 原来只拦「空」，文案却说
                # 「至少 2 个模式」⇒ 只勾 1 个模式时**能存进去**（与文案
                # 矛盾；且 1 个模式的"循环"不是循环＝同一阶段反复跑）。
                # ⇒ 按文案口径真的要求 ≥2，并把**当前个数**说进文案，
                # 免得用户在界面上看到「不能为空」却明明勾了 1 个。
                if not clean:
                    return ("%s 不能为空（至少 2 个模式才能构成循环）"
                            % proto.CFG_AUTO_CYCLE_MODES)
                return ("%s 至少要有 2 个模式才能构成循环（当前 %d 个）"
                        % (proto.CFG_AUTO_CYCLE_MODES, len(clean)))
            modes = clean
        first = modes[0] if modes else ""
        # ---- ② stageCfg（每阶段的切换条件；fail-closed）----
        stage_entries = {}
        if proto.CFG_AUTO_CYCLE_STAGE_CFG in cycle_doc:
            raw = cycle_doc[proto.CFG_AUTO_CYCLE_STAGE_CFG]
            if not isinstance(raw, dict):
                return ("%s 需要对象（{模式 id: {压差/定时条件}}）"
                        % proto.CFG_AUTO_CYCLE_STAGE_CFG)
            for key, val in raw.items():
                mid = str(key or "").strip()
                if mid not in proto.AUTO_CYCLE_MODE_POOL:
                    return ("%s 含未知模式：%s"
                            % (proto.CFG_AUTO_CYCLE_STAGE_CFG, key))
                entry, err = self._cycle_stage_entry_check(mid, val)
                if err:
                    return err
                stage_entries[mid] = entry
            # [缺阶段配置 ⇒ 拒] 载荷给了 stageCfg 就必须**覆盖参与循环的
            # 每一个模式**（漏一个 ⇒ 那个阶段的切换条件说不清 ⇒ 不接受）。
            lack = [m for m in modes if m not in stage_entries]
            if lack:
                return ("%s 缺少参与循环的阶段配置：%s"
                        % (proto.CFG_AUTO_CYCLE_STAGE_CFG,
                           "、".join(lack)))
        # ---- ③ 旧键 ⇒ 折进对应阶段（只改值）----
        overlay = {}

        def _patch(mid, field, value):
            overlay.setdefault(mid, {})[field] = value

        if proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD in cycle_doc:
            try:
                val = float(cycle_doc[proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD])
            except (TypeError, ValueError):
                return "pressThreshold 需要数字（MPa）"
            if first:
                _patch(first, "press_threshold", max(
                    0.0, min(proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX, val)))
        if proto.CFG_AUTO_CYCLE_PRESS_COMPARE in cycle_doc:
            cmp_key = str(cycle_doc[proto.CFG_AUTO_CYCLE_PRESS_COMPARE]
                          or "").strip().lower()
            if cmp_key not in proto.AUTO_PRESS_COMPARE_KEYS:
                return "pressCompare 只能是 %s" % "/".join(
                    proto.AUTO_PRESS_COMPARE_KEYS)
            if first:
                _patch(first, "press_compare", cmp_key)
        if proto.CFG_AUTO_CYCLE_TIMER_FALLBACK in cycle_doc:
            try:
                val = strict_bool(cycle_doc[proto.CFG_AUTO_CYCLE_TIMER_FALLBACK],
                                  proto.CFG_AUTO_CYCLE_TIMER_FALLBACK)
            except ValueError as exc:
                return str(exc)
            if first:
                _patch(first, "timer_enabled", val)
        for key, mid in ((proto.CFG_AUTO_CYCLE_TIMER_MINUTES, first),
                         (proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES, "backwash"),
                         (proto.CFG_AUTO_CYCLE_RINSE_MINUTES, "rinse")):
            if key not in cycle_doc:
                continue
            try:
                val = int(cycle_doc[key])
            except (TypeError, ValueError):
                return "%s 需要整数分钟" % key
            if mid:
                _patch(mid, "timer_minutes", max(
                    proto.AUTO_CYCLE_MINUTES_MIN,
                    min(proto.AUTO_CYCLE_MINUTES_MAX, val)))
        # [兼容视图 stageMinutes] 已上线的旧客户端只发它 ⇒ 折进各阶段的
        # timerMinutes（**不动**"启不启用定时"那个开关）。
        if proto.CFG_AUTO_CYCLE_STAGE_MINUTES in cycle_doc:
            raw = cycle_doc[proto.CFG_AUTO_CYCLE_STAGE_MINUTES]
            if not isinstance(raw, dict):
                return ("%s 需要对象（{模式 id: 分钟}）"
                        % proto.CFG_AUTO_CYCLE_STAGE_MINUTES)
            for key, val in raw.items():
                mid = str(key or "").strip()
                if mid not in proto.AUTO_CYCLE_MODE_POOL:
                    return ("%s 含未知模式：%s"
                            % (proto.CFG_AUTO_CYCLE_STAGE_MINUTES, key))
                if isinstance(val, bool):
                    return "%s[%s] 需要整数分钟" % (
                        proto.CFG_AUTO_CYCLE_STAGE_MINUTES, mid)
                try:
                    num = int(val)
                except (TypeError, ValueError):
                    return "%s[%s] 需要整数分钟" % (
                        proto.CFG_AUTO_CYCLE_STAGE_MINUTES, mid)
                _patch(mid, "timer_minutes", max(
                    proto.AUTO_CYCLE_MINUTES_MIN,
                    min(proto.AUTO_CYCLE_MINUTES_MAX, num)))
        # ---- ④ 合成最终的 stage_cfg（已有配置 ← 旧键 ← 显式 stageCfg）----
        legacy = {
            "press_threshold": c.get("press_threshold",
                                     proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF),
            "press_compare": c.get("press_compare",
                                   proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
            "timer_fallback": c.get("timer_fallback",
                                    proto.AUTO_CYCLE_TIMER_FALLBACK_DEF),
            "timer_minutes": c.get("timer_minutes",
                                   proto.AUTO_CYCLE_TIMER_MINUTES_DEF),
            "backwash_minutes": c.get(
                "backwash_minutes", proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF),
            "rinse_minutes": c.get("rinse_minutes",
                                   proto.AUTO_CYCLE_RINSE_MINUTES_DEF),
        }
        merged = {mid: dict(entry) for mid, entry
                  in proto.normalize_cycle_stage_cfg(
                      c.get("stage_cfg"), default={}).items()}
        for mid, patch in overlay.items():
            merged.setdefault(mid, {}).update(patch)
        for mid, entry in stage_entries.items():
            merged.setdefault(mid, {}).update(entry)
        stage_cfg = proto.build_cycle_stage_cfg(
            modes, legacy=legacy, explicit=merged)
        # ---- ⑤ 每个参与阶段至少要有一个触发条件（fail-closed）----
        no_trigger = [m for m in modes
                      if not (bool((stage_cfg.get(m) or {}).get(
                          "press_enabled", False))
                          or bool((stage_cfg.get(m) or {}).get(
                              "timer_enabled", False)))]
        if no_trigger:
            return "阶段 %s 未设置切换条件（压差/定时至少一个）" % "、".join(
                no_trigger)
        # ---- ⑥ 落盘（一次写完；出错由调用方整包回滚）----
        if proto.CFG_AUTO_CYCLE_ENABLED in cycle_doc:
            # [严格布尔 2026-11-16 审计] 同 protect：bool("false") 是 True，
            # 字符串会把开关翻反（定时兜底被静默误开 ⇒ 产水到点强制切反洗）。
            try:
                c["enabled"] = strict_bool(
                    cycle_doc[proto.CFG_AUTO_CYCLE_ENABLED],
                    proto.CFG_AUTO_CYCLE_ENABLED)
            except ValueError as exc:
                return str(exc)
        c["cycle_modes"] = modes
        c["stage_cfg"] = stage_cfg
        # [旧键 = stage_cfg 的派生视图] 压差/兜底定时写首元素的值、反洗/
        # 顺洗分钟写对应模式的值 ⇒ 老读方不受影响，且与新配置永远一致。
        self.cfg._sync_auto_cycle_legacy()
        return ""

    @staticmethod
    def _switch_policy_of(mode_cfg, key):
        """读一个模式的换相策略；缺键/非法值 ⇒ 缺省「先停再启」。

        [为什么非法值也回缺省而不是报错] 应答侧只做**回显**（非法值在
        SET 入口已被拒，见 `_h_config_set`）⇒ 这里兜底防脏配置。
        """
        val = str((mode_cfg or {}).get(key) or "").strip()
        return val if val in proto.AUTO_SWITCH_POLICY_IDS \
            else proto.AUTO_SWITCH_POLICY_DEF

    def _config_auto_doc(self):
        """自动模式配置快照（GET/SET 应答用；键形 = 客户端 _auto_config_values）。"""
        modes = {}
        # [2026-11-18 放水模式] 快照给的是**可配置**的四个模式（含放水）
        # ⇒ 客户端远端设置的第四块能回显「配了什么」；放水只配不跑。
        for mid, _name in proto.AUTO_CONFIG_MODES:
            m = self.cfg.auto.get(mid) or {}
            modes[mid] = {
                "devices": list(m.get("devices") or []),
                "stirRpm": int(m.get("stir_rpm", proto.AUTO_STIR_RPM_DEF)),
                "stirReverse": bool(m.get("stir_reverse", False)),
                # [停止/急停时是否关阀复位 2026-11-12] 逐模式可设。
                proto.AUTO_MODE_RESET_VALVES_ON_STOP: bool(
                    m.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                          proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF)),
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: bool(
                    m.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP,
                          proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF)),
                # [换相策略 2026-11-14] 两个泵在换相窗口内的处置策略
                # （缺键＝老服务端/老配置 ⇒ 回缺省「先停再启」）。
                proto.AUTO_MODE_STIR_SWITCH: self._switch_policy_of(
                    m, proto.AUTO_MODE_STIR_SWITCH),
                proto.AUTO_MODE_INLET_SWITCH: self._switch_policy_of(
                    m, proto.AUTO_MODE_INLET_SWITCH),
            }
        # [自动循环配置 2026-11-12] 与 modes 平级的 cycle 子节。
        c = self.cfg.auto_cycle or {}
        cycle_modes = proto.normalize_cycle_modes(
            c.get("cycle_modes"), default=proto.AUTO_CYCLE_MODES_DEF)
        # [stage_cfg = 唯一事实源 2026-11-18] GET 总是回它（缺省填默认）⇒
        # 客户端不必猜"服务端有没有这功能"，也不必处理"缺键=用缺省"。
        stage_cfg = proto.build_cycle_stage_cfg(
            cycle_modes,
            legacy={
                "press_threshold": c.get(
                    "press_threshold", proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF),
                "press_compare": c.get(
                    "press_compare", proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
                "timer_fallback": c.get(
                    "timer_fallback", proto.AUTO_CYCLE_TIMER_FALLBACK_DEF),
                "timer_minutes": c.get(
                    "timer_minutes", proto.AUTO_CYCLE_TIMER_MINUTES_DEF),
                "backwash_minutes": c.get(
                    "backwash_minutes",
                    proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF),
                "rinse_minutes": c.get(
                    "rinse_minutes", proto.AUTO_CYCLE_RINSE_MINUTES_DEF),
            },
            explicit=proto.normalize_cycle_stage_cfg(
                c.get("stage_cfg"), default={}))
        cycle = {
            proto.CFG_AUTO_CYCLE_ENABLED: bool(
                c.get("enabled", proto.AUTO_CYCLE_ENABLED_DEF)),
            proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD: float(
                c.get("press_threshold", proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF)),
            proto.CFG_AUTO_CYCLE_PRESS_COMPARE: str(
                c.get("press_compare", proto.AUTO_CYCLE_PRESS_COMPARE_DEF)),
            proto.CFG_AUTO_CYCLE_TIMER_FALLBACK: bool(
                c.get("timer_fallback", proto.AUTO_CYCLE_TIMER_FALLBACK_DEF)),
            proto.CFG_AUTO_CYCLE_TIMER_MINUTES: int(
                c.get("timer_minutes", proto.AUTO_CYCLE_TIMER_MINUTES_DEF)),
            proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES: int(
                c.get("backwash_minutes",
                      proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF)),
            proto.CFG_AUTO_CYCLE_RINSE_MINUTES: int(
                c.get("rinse_minutes", proto.AUTO_CYCLE_RINSE_MINUTES_DEF)),
            # [2026-11-18 配置化] GET **总是**回这两个键（缺省填默认）⇒
            # 客户端不必猜"服务端有没有这功能"，也不必处理"缺键=用缺省"。
            proto.CFG_AUTO_CYCLE_MODES: list(cycle_modes),
            # **各阶段的切换条件**（压差触发 / 定时触发）——camelCase。
            proto.CFG_AUTO_CYCLE_STAGE_CFG:
                proto.cycle_stage_cfg_to_protocol(stage_cfg),
            # [兼容视图] 每阶段分钟数（由 stageCfg 的 timerMinutes 派生）
            # ⇒ 已经上线的旧客户端界面照常显示时长。
            proto.CFG_AUTO_CYCLE_STAGE_MINUTES:
                proto.cycle_stage_minutes_view(stage_cfg, cycle_modes),
        }
        return {"modes": modes, proto.CFG_AUTO_CYCLE: cycle}

    def _cfg_take_snapshot(self):
        """抓一份 cfg 快照（供"整包失败即回滚"用）。

        [为什么需要] 各配置节的 SET 是**逐键写**的：一包里既有合法键又有
        非法键时，原来会"前面几个已经生效、后面那个报错"⇒ **半截配置**
        （现场最难查的一类：界面说没保存，可时限/开关其实改了）。
        ⇒ 写之前抓快照，出错整体回滚。

        [子对象有 __slots__] SerialConfig / AnalogConfig / StorageConfig 都是
        `__slots__` 类 ⇒ **没有 __dict__** ⇒ 快照/回滚必须逐槽处理
        （直接访问 `sub.__dict__` 会抛 AttributeError ⇒ 整个 SET 报
        "服务端处理异常"）。
        """
        import copy
        cfg = self.cfg
        snap = {"cfg": _cfg_obj_snapshot(cfg),
                "auto": copy.deepcopy(getattr(cfg, "auto", {}) or {}),
                # auto_cycle 也是**就地改的字典** ⇒ 必须深拷贝，否则回滚时
                # 那个字典已经被改过了（浅拷贝的 cfg.__dict__ 里存的是同一
                # 个对象引用）。
                "auto_cycle": copy.deepcopy(
                    getattr(cfg, "auto_cycle", {}) or {})}
        for name in ("serial", "serial_b", "analog", "storage"):
            snap[name] = _cfg_obj_snapshot(getattr(cfg, name, None))
        return snap

    def _cfg_restore_snapshot(self, snap):
        """按快照回滚 cfg（写失败时用）。"""
        cfg = self.cfg
        _cfg_obj_restore(cfg, snap.get("cfg"))
        auto = getattr(cfg, "auto", None)
        if isinstance(auto, dict):
            auto.clear()
            auto.update(snap.get("auto", {}))
        cycle = getattr(cfg, "auto_cycle", None)
        if isinstance(cycle, dict):
            cycle.clear()
            cycle.update(snap.get("auto_cycle", {}))
        for name in ("serial", "serial_b", "analog", "storage"):
            _cfg_obj_restore(getattr(cfg, name, None), snap.get(name))

    def _cfg_apply_batch(self, keys, doc, setter):
        """整包应用配置键：出错**整体回滚**（不留半截配置）。

        :param setter: (key, value) → 有变化则返回真值；非法值抛 ValueError。
        """
        snap = self._cfg_take_snapshot()
        changed = False
        try:
            for key in keys:
                if setter(key, doc[key]):
                    changed = True
        except Exception:
            self._cfg_restore_snapshot(snap)
            raise
        return changed

    def _cfg_set_collect_key(self, key, value):
        """写一个 collect 键进 cfg；返回 (值有变化, 影响开关)。

        值类型不符抛 ValueError（handler 回否定应答，cfg 不动）。
        [周期写哪] vfd/temphum 的 cfg 属性是只读转发（→ serial.*），
        落点必须是 serial/analog 子对象，直接 setattr 属性会 AttributeError。
        """
        c = self.cfg
        bool_keys = {
            proto.CFGK_VFD_ENABLED: "enable_vfd",
            proto.CFGK_TEMPHUM_ENABLED: "enable_temphum",
            proto.CFGK_FLOWMETER_ENABLED: "enable_flowmeter",
            proto.CFGK_TURBIDITY_ENABLED: "enable_turbidity",
            # [逐台开关 2026-11-10] 两台浊度是独立设备，各写各的门控。
            proto.CFGK_TURBIDITY1_ENABLED: "enable_turbidity1",
            proto.CFGK_TURBIDITY2_ENABLED: "enable_turbidity2",
            proto.CFGK_ANALOG_ENABLED: "enable_analog",
            proto.CFGK_DIGITAL_ENABLED: "enable_digital",
        }
        if key in bool_keys:
            if not isinstance(value, bool):
                raise ValueError("%s 需要 bool" % key)
            attr = bool_keys[key]
            changed = getattr(c, attr) != value
            setattr(c, attr, value)
            # [旧键扇出在 handler 里按"整条载荷"判定] 不能在这里立刻写逐台
            # 键：客户端同一条 SET 里先发逐台键、再发共同键（= 任一启用），
            # 在这里扇出会把刚设的逐台值又盖回共同值（实测 B3c 红）。
            return changed, True          # 开关变化 ⇒ 要重启轮询任务
        poll_keys = {
            # 值 = (对象名或 None, 属性名)；None = cfg 顶层属性。
            # [不能拼 "a.b" 字符串再 split] 浊度/数字量周期是 cfg 顶层属性，
            # 无点可拆 —— 曾统一 split(".") 导致这两路 SET 直接报
            # "not enough values to unpack"（远程采集配置 e2e 实测抓到）。
            proto.CFGK_VFD_POLL_MS: ("serial", "vfd_poll_ms"),
            proto.CFGK_TEMPHUM_POLL_MS: ("serial", "temphum_poll_ms"),
            proto.CFGK_TURBIDITY_POLL_MS: (None, "turbidity_poll_ms"),
            # [逐台 2026-11-10] 三台各设各的；turbidityPollMs 仍作两台共同缺省。
            proto.CFGK_TURBIDITY1_POLL_MS: (None, "turbidity1_poll_ms"),
            proto.CFGK_TURBIDITY2_POLL_MS: (None, "turbidity2_poll_ms"),
            proto.CFGK_FLOWMETER_POLL_MS: (None, "flowmeter_poll_ms"),
            proto.CFGK_ANALOG_POLL_MS: ("analog", "poll_ms"),
            proto.CFGK_DIGITAL_POLL_MS: (None, "digital_poll_ms"),
        }
        if key in poll_keys:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("%s 需要 int 毫秒" % key)
            ms = self._clamp_poll_ms(int(value))
            obj_name, attr = poll_keys[key]
            target = c if obj_name is None else getattr(c, obj_name)
            changed = getattr(target, attr) != ms
            setattr(target, attr, ms)
            # [周期改了就重算新鲜度下限] 否则把周期调大后 stale 还停在旧值，
            # 又会出现「每拍之间必然过期」的周期闪烁（板端 2026-11-17 实锅）。
            try:
                self.cfg.apply_stale_floor()
            except Exception:                        # noqa: BLE001
                pass
            return changed, False         # 周期下一拍自然生效，无需重启
        # [压差计/温度传感器通道与量程 2026-11-20 远端可配；二改：量程=
        #  下限~上限（4mA→下限、20mA→上限），「温度满量程」废弃] 通道
        # 0..channels-1。改完重建 scales（模拟量页按新通道显示换算）。
        # [压差计负压口径 2026-11-20 用户确认] 铭牌 0~1MPa、4mA=0 点；
        # 4mA 以下按线性继续算负值并标「负压」（既有特殊处理原样保留，
        # lowValue 缺省 0 时公式与老口径逐位等价）。
        # [0~4 段有效 2026-11-20 用户口径「增加 0-4 有效选项」] 逐传感器
        # 开关：开=4mA 以下线性延续（适配更多传感器）；关=欠量程钳下限。
        analog_bool = {
            proto.CFGK_ANALOG_PRESS_BELOW4_VALID:
                ("press_below4_valid", True),
            proto.CFGK_ANALOG_TEMP_BELOW4_VALID:
                ("temp_below4_valid", False),
        }
        if key in analog_bool:
            if not isinstance(value, bool):
                raise ValueError("%s 需要 bool" % key)
            attr = analog_bool[key][0]
            changed = getattr(c.analog, attr) != value
            setattr(c.analog, attr, value)
            c.analog.rebuild_scales()
            return changed, False
        # [analogChannels 2026-11-20] GET 便利键（模块路数元数据）——
        # 客户端把 GET 应答**整包回写**（§4 同值写回/界面全量应用）时会带上
        # 它 ⇒ 精确匹配忽略（不算未知键）；别的拼错键仍然明确拒。
        if key == "analogChannels":
            return False, False
        analog_num = {
            proto.CFGK_ANALOG_PRESS_CHANNEL: ("press_channel", 0),
            proto.CFGK_ANALOG_TEMP_CHANNEL: ("temp_channel", 1),
        }
        if key in analog_num:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("%s 需要整数通道号" % key)
            ch = int(value)
            if not (0 <= ch < c.analog.channels):
                raise ValueError("%s 通道号越界：%d（模块 %d 路）"
                                 % (key, ch, c.analog.channels))
            attr = analog_num[key][0]
            changed = getattr(c.analog, attr) != ch
            setattr(c.analog, attr, ch)
            c.analog.rebuild_scales()
            return changed, False
        analog_float = {
            proto.CFGK_ANALOG_PRESS_LOW_MPA:
                ("press_low_mpa", proto.CFGK_ANALOG_PRESS_LOW_MPA_MIN,
                 proto.CFGK_ANALOG_PRESS_LOW_MPA_MAX),
            proto.CFGK_ANALOG_PRESS_HIGH_MPA:
                ("press_high_mpa", proto.CFGK_ANALOG_PRESS_HIGH_MPA_MIN,
                 proto.CFGK_ANALOG_PRESS_HIGH_MPA_MAX),
            proto.CFGK_ANALOG_TEMP_LOW_C:
                ("temp_low_c", proto.CFGK_ANALOG_TEMP_LOW_C_MIN,
                 proto.CFGK_ANALOG_TEMP_LOW_C_MAX),
            proto.CFGK_ANALOG_TEMP_HIGH_C:
                ("temp_high_c", proto.CFGK_ANALOG_TEMP_HIGH_C_MIN,
                 proto.CFGK_ANALOG_TEMP_HIGH_C_MAX),
        }
        if key in analog_float:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("%s 需要数字" % key)
            fv = float(value)
            attr, lo, hi = analog_float[key]
            if not (lo <= fv <= hi):
                raise ValueError("%s 越界：%s（%s ~ %s）"
                                 % (key, fv, lo, hi))
            changed = getattr(c.analog, attr) != fv
            setattr(c.analog, attr, fv)
            # [下限 < 上限 交叉校验] 单键 SET 看不到同包里的另一个新值——
            # UI 恒成对下发 ⇒ 这里按「另一侧**当前值**」校验：不满足时明确
            # 报错（fail-closed），提示把两侧一起改。
            if "low" in attr:
                peer = {"press_low_mpa": "press_high_mpa",
                        "temp_low_c": "temp_high_c"}[attr]
                if fv >= getattr(c.analog, peer):
                    raise ValueError("%s 量程下限必须小于上限（当前上限 %s）"
                                     % (key, getattr(c.analog, peer)))
            else:
                peer = {"press_high_mpa": "press_low_mpa",
                        "temp_high_c": "temp_low_c"}[attr]
                if fv <= getattr(c.analog, peer):
                    raise ValueError("%s 量程上限必须大于下限（当前下限 %s）"
                                     % (key, getattr(c.analog, peer)))
            c.analog.rebuild_scales()
            return changed, False
        raise ValueError("未知采集配置键：%s" % key)

    def _cfg_set_storage_key(self, key, value):
        """写一个 storage 键进 cfg；返回是否有变化。"""
        s = self.cfg.storage
        if key == proto.CFGK_STORAGE_ENABLED:
            if not isinstance(value, bool):
                raise ValueError("enabled 需要 bool")
            changed = s.enabled != value
            s.enabled = value
            return changed
        if key == proto.CFGK_STORAGE_PATH:
            if value is None:
                value = ""
            if not isinstance(value, str):
                raise ValueError("path 需要字符串（空串=服务端程序目录）")
            changed = (s.path or "") != value
            s.path = value
            return changed
        if key == proto.CFGK_STORAGE_SAMPLE_MS:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("sampleMs 需要 int 毫秒")
            ms = max(1000, min(3600000, int(value)))
            changed = s.sample_ms != ms
            s.sample_ms = ms
            return changed
        if key == proto.CFGK_STORAGE_MAX_MB:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("maxMb 需要 int 兆字节（0 = 不限）")
            mb = max(proto.STORAGE_MAX_MB_MIN,
                     min(proto.STORAGE_MAX_MB_MAX, int(value)))
            changed = s.max_mb != mb
            s.max_mb = mb
            return changed
        if key == proto.CFGK_STORAGE_FULL_POLICY:
            pol = str(value or "").strip().lower()
            if pol not in proto.STORAGE_POLICY_KEYS:
                raise ValueError("fullPolicy 只能是 %s"
                                 % "/".join(proto.STORAGE_POLICY_KEYS))
            changed = s.full_policy != pol
            s.full_policy = pol
            return changed
        if key == proto.CFGK_STORAGE_KEEP_FILES:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("keepFiles 需要 int 段数（1..99）")
            keep = max(1, min(99, int(value)))
            changed = s.keep_files != keep
            s.keep_files = keep
            return changed
        if key == proto.CFGK_STORAGE_HISTORY_TRIGGER:
            # [历史入库触发方式 2026-11-12] 只认两个已知值，乱值明确报错
            # （fail-closed：不能让一个拼错的键把"停机期不入库"悄悄变"一直记"）。
            trig = str(value or "").strip().lower()
            if trig not in proto.HISTORY_TRIGGER_KEYS:
                raise ValueError("historyTrigger 只能是 %s"
                                 % "/".join(proto.HISTORY_TRIGGER_KEYS))
            changed = s.history_trigger != trig
            s.history_trigger = trig
            return changed
        raise ValueError("未知落盘配置键：%s" % key)

    def _cfg_set_login_key(self, key, value):
        """写一个 login 键进 cfg；返回是否有变化（2026-11-08）。"""
        c = self.cfg
        if key == proto.CFGK_LOGIN_ALLOW_ANON:
            if not isinstance(value, bool):
                raise ValueError("loginAllowAnonymous 需要 bool")
            changed = c.login_allow_anonymous != value
            c.login_allow_anonymous = value
            return changed
        if key == proto.CFGK_LOGIN_ALLOW_CONCUR:
            if not isinstance(value, bool):
                raise ValueError("loginAllowConcurrent 需要 bool")
            changed = c.login_allow_concurrent != value
            c.login_allow_concurrent = value
            return changed
        if key == proto.CFGK_LOGIN_MAX_SESSIONS:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("loginMaxSessions 需要 int（0 = 不限）")
            n = max(proto.CFG_LOGIN_MAX_SESSIONS_MIN,
                    min(proto.CFG_LOGIN_MAX_SESSIONS_MAX, int(value)))
            changed = c.login_max_sessions != n
            c.login_max_sessions = n
            return changed
        raise ValueError("未知登录策略键：%s" % key)

    async def _h_config_get(self, session, request_id, msg_code, _payload):
        """配置快照（无需角色：设置页只读回显不涉密）。"""
        await self._reply(session, request_id, msg_code, True, extra={
            "collect": self._config_collect_doc(),
            "storage": self._config_storage_doc(),
            "login": self._config_login_doc(),
            proto.CFG_SECTION_AUTO: self._config_auto_doc(),
            proto.CFG_SECTION_PROTECT: self._config_protect_doc(),
            proto.CFG_SECTION_CAMERA: self._config_camera_doc(),
            "storageDbPath": self.store.db_path or "",
            "storageStats": self._storage_stats_doc(),
        })

    async def _h_config_set(self, session, request_id, msg_code, payload):
        """设置采集/落盘配置（**需 root**）。

        [为什么 root 但不看自动模式闸] 改配置不动任何输出点，不属"控制"；
        自动运行中允许调采集频率/开关（监视不受模式限制）。
        """
        if not permissions.can(session, proto.PERM_ACCOUNT_ADMIN):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(proto.PERM_ACCOUNT_ADMIN))
            return
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "配置载荷解析失败：%s" % exc)
            return
        section = doc.get("section")
        if section == "collect":
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            try:
                enables_changed = self._cfg_apply_batch(
                    keys, doc,
                    lambda k, v: self._cfg_set_collect_key(k, v)[1])
                # [旧共同键只在不带逐台键时扇出 2026-11-10] 旧客户端只有
                # turbidityEnabled ⇒ 视作"两台一起"；新客户端同一条 SET 里
                # 也带它（= 任一启用，兼容旧服务端），此时**以逐台键为准**，
                # 否则会把单独停用的那台又打开。
                if (proto.CFGK_TURBIDITY_ENABLED in doc
                        and proto.CFGK_TURBIDITY1_ENABLED not in doc
                        and proto.CFGK_TURBIDITY2_ENABLED not in doc):
                    _all = bool(doc[proto.CFGK_TURBIDITY_ENABLED])
                    for _i in (1, 2):
                        _a = "enable_turbidity%d" % _i
                        if getattr(self.cfg, _a) != _all:
                            setattr(self.cfg, _a, _all)
                            enables_changed = True
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            if enables_changed:
                await self.restart_polling()
            self._log("远程配置：采集节已更新（%s）" % ",".join(keys))
            # [成功文案进 err 键 2026-11-13 与 C 端对齐] C 客户端把 0x0117
            # 应答的 ok/err 回显到状态行（HsfLinkClient.c「写应答」分支），
            # 故两端成功应答统一为 {ok:true, err:"文案", collect:{…}}——
            # 文案逐字同 C 端 HsfServerApp.c collect 分支。
            await self._reply(session, request_id, msg_code, True,
                              "采集配置已应用（运行期立即生效）", extra={
                                  "collect": self._config_collect_doc()})
        elif section == "storage":
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            try:
                self._cfg_apply_batch(keys, doc, self._cfg_set_storage_key)
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            self.storage_apply()
            self._log("远程配置：落盘节已更新（%s）" % ",".join(keys))
            # [成功文案进 err 键 2026-11-13 与 C 端同文案]（见 collect 分支注）。
            await self._reply(session, request_id, msg_code, True,
                              "落盘配置已保存（库路径下次启动生效）", extra={
                                  "storage": self._config_storage_doc(),
                                  "storageDbPath": self.store.db_path or "",
                                  "storageStats": self._storage_stats_doc()})
        elif section == "login":
            # 登录/账户策略节（2026-11-08）。写入立即对**下一次登录**生效
            # 并落盘 cfg；存量会话不受影响（关并发不会踢人）。
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            try:
                self._cfg_apply_batch(keys, doc, self._cfg_set_login_key)
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            c = self.cfg
            self._log("远程配置：登录策略已更新（匿名=%d 同账并发=%d 上限=%d）"
                      % (1 if c.login_allow_anonymous else 0,
                         1 if c.login_allow_concurrent else 0,
                         int(c.login_max_sessions)))
            # [成功文案进 err 键 2026-11-13 与 C 端同文案]（见 collect 分支注）。
            await self._reply(session, request_id, msg_code, True,
                              "登录策略已更新（下次登录生效）", extra={
                                  "login": self._config_login_doc()})
        elif section == proto.CFG_SECTION_AUTO:
            # 自动模式配置节（2026-11-11）：产水/反洗/顺洗三模式各自的设备
            # 清单与搅拌电机参数。写入**立即生效**（运行期只读 cfg.auto，
            # 下一次启动模式就用新配置），并落盘 cfg。
            # [2026-11-18 放水模式] 可配置的第四个模式（放水）也在本节里
            # 存取（proto.AUTO_CONFIG_MODES）——它**不能被启动**（启动闸
            # 只认三模式 + 组合「auto」），但配置要能存能读回。
            modes_doc = doc.get("modes")
            cycle_doc = doc.get(proto.CFG_AUTO_CYCLE)
            # [modes 与 cycle 至少给一个] 客户端可以只改循环配置（不动三个
            # 子模式的设备清单），也可以只改子模式配置。
            has_modes = isinstance(modes_doc, dict) and bool(modes_doc)
            has_cycle = isinstance(cycle_doc, dict) and bool(cycle_doc)
            if not has_modes and not has_cycle:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有 modes / cycle 配置")
                return
            known = set(proto.AUTO_DEVICE_IDS)
            # [整包失败即回滚 2026-11-15 审计] 这一节是**边校验边写**（每个
            # 模式写完再校验下一个）⇒ 一个包里前一个模式合法、后一个非法
            # 时 ⇒ **前一个已经写进去了**（界面说没保存，可设备清单已改）。
            # ⇒ 出错路径统一先回滚再回否定应答。
            snap = self._cfg_take_snapshot()

            async def _fail(text):
                self._cfg_restore_snapshot(snap)
                await self._reply(session, request_id, msg_code, False, text)

            for mid, m in (modes_doc or {}).items():
                target = self.cfg.auto.get(mid)
                if target is None:
                    # [文案逐字对齐 C 2026-11-13] C 端 snprintf「未知自动模式：%s」
                    # 无引号 ⇒ %r（repr 带引号）改 %s，两端同一句。
                    await _fail("未知自动模式：%s" % mid)
                    return
                if not isinstance(m, dict):
                    await _fail("模式 %s 的配置必须是对象" % mid)
                    return
                devs = m.get("devices", None)
                if devs is not None:
                    if not isinstance(devs, list):
                        await _fail("模式 %s 的 devices 必须是数组" % mid)
                        return
                    # 未知 id 丢弃（前向兼容），但全未知且有内容 ⇒ 拒绝
                    # （客户端键形错了要让人看见，不能静默存一份空的）。
                    clean = [d for d in devs
                             if isinstance(d, str) and d in known]
                    if devs and not clean:
                        await _fail(
                            "模式 %s 的设备清单里没有可识别的设备 id" % mid)
                        return
                    target["devices"] = clean
                rpm = m.get("stirRpm", None)
                if rpm is not None:
                    try:
                        target["stir_rpm"] = max(
                            proto.AUTO_STIR_RPM_MIN,
                            min(proto.AUTO_STIR_RPM_MAX, int(rpm)))
                    except (TypeError, ValueError):
                        await _fail("模式 %s 的 stirRpm 需要整数" % mid)
                        return
                rev = m.get("stirReverse", None)
                if rev is not None:
                    # [严格布尔 2026-11-15 审计] 原 `bool(rev)`：字符串
                    # "false" ⇒ True（方向反了）。失败要让人看见。
                    try:
                        target["stir_reverse"] = strict_bool(
                            rev, "模式 %s 的 stirReverse" % mid)
                    except ValueError as exc:
                        await _fail(str(exc))
                        return
                # [停止/急停时是否关阀复位 2026-11-12] 两个布尔开关。
                for key in (proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                            proto.AUTO_MODE_RESET_VALVES_ON_ESTOP):
                    if key in m:
                        try:
                            target[key] = strict_bool(
                                m.get(key), "模式 %s 的 %s" % (mid, key))
                        except ValueError as exc:
                            await _fail(str(exc))
                            return
                # [换相策略 2026-11-14] 搅拌泵/进水泵在换相窗口内怎么处置。
                # **fail-closed**：非法值明确拒（策略是安全相关的行为选择，
                # 静默回落缺省会让人以为配了别的策略）。
                for key in (proto.AUTO_MODE_STIR_SWITCH,
                            proto.AUTO_MODE_INLET_SWITCH):
                    if key not in m:
                        continue
                    val = str(m.get(key) or "").strip()
                    if val not in proto.AUTO_SWITCH_POLICY_IDS:
                        await _fail(
                            "模式 %s 的 %s 只能是 %s"
                            % (mid, key,
                               "/".join(proto.AUTO_SWITCH_POLICY_IDS)))
                        return
                    target[key] = val
            # ---- 自动循环配置（cycle 子节；2026-11-12）----
            if cycle_doc is not None:
                if not isinstance(cycle_doc, dict):
                    await _fail("cycle 配置必须是对象")
                    return
                err = self._cfg_set_auto_cycle(cycle_doc)
                if err:
                    await _fail(err)
                    return
            self.cfg.save()
            mode_names = dict(proto.AUTO_CONFIG_MODES)
            # [modes_doc 可能是 None] 只改 cycle 时（不动子模式清单）它是
            # None ⇒ 迭代会炸（'NoneType' object is not iterable）。
            self._log("远程配置：自动模式配置已更新（%s）"
                      % "、".join(mode_names[k] for k in (modes_doc or {})
                                  if k in mode_names))
            await self._reply(session, request_id, msg_code, True, extra={
                proto.CFG_SECTION_AUTO: self._config_auto_doc()})
        elif section == proto.CFG_SECTION_PROTECT:
            # 运行保护节（2026-11-15）：进水泵空管超时的时限 + 是否停设备。
            # 写入**立即生效**（保护每拍读 cfg）+ 落盘 cfg。
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            # [先校验后写入 2026-11-15 审计] 原来是一个键一个键地"边校验
            # 边写"⇒ 一包里前几个键合法、后一个非法时 ⇒ **前半截已经生效**
            # （现场最难查：报错说没保存，但时限其实改了）。⇒ 整包走
            # `_cfg_apply_batch`：出错**整体回滚**（一个键都不留）。
            try:
                self._cfg_apply_batch(keys, doc, self._cfg_set_protect_key)
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            self._log("远程配置：运行保护已更新（%s）" % ",".join(keys))
            await self._reply(session, request_id, msg_code, True,
                              "运行保护配置已保存（立即生效）", extra={
                                  proto.CFG_SECTION_PROTECT:
                                      self._config_protect_doc()})
        elif section == proto.CFG_SECTION_CAMERA:
            # 相机节（2026-11-19）：IP/端口/账号/口令/码流/帧率/画质/尺寸。
            # 写入**立即生效**（重启采集器）+ 落盘 cfg。
            # [口令不回显] 空口令 = 不改（见 _cfg_set_camera_key 的注释）。
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            try:
                self._cfg_apply_batch(keys, doc, self._cfg_set_camera_key)
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            # [运行期生效] 改了连接参数/帧率/画质 ⇒ 采集器要重启（连接参数
            # 变了必须重连；帧率/画质变了要重建解码管道）。
            self.start_camera()
            self._log("远程配置：相机已更新（%s）" % ",".join(keys))
            await self._reply(session, request_id, msg_code, True,
                              "相机配置已保存（立即生效）", extra={
                                  proto.CFG_SECTION_CAMERA:
                                      self._config_camera_doc()})
        else:
            await self._reply(session, request_id, msg_code, False,
                              "未知配置节：%r（collect / storage / login /"
                              " auto / protect / camera）" % section)

    # ==================================================================
    #                       周期 tick（推送 + 健康 + 看门狗）
    # ==================================================================
    async def __call__(self, now_ms):
        """XLink 服务端的业务 tick 回调（由 xlink.Server 周期驱动）。

        [**每一步各自 try** —— 2026-11-15 实锅] 原来这些步骤是**一串 await**
        没有各自的护栏：`_publish_telemetry`（排最前、自身无兜底 try）一抛
        异常 ⇒ 后面**全部**不执行 ⇒ `_auto_step`（= **现场停止按钮 + 急停
        受理**）、运行保护、故障看门狗、健康推送**这一拍全死**；同一个异常
        每 100ms 复现 ⇒ 这些安全路径**永久死**，而进程看起来正常（异常只
        进终端日志，不进 logs 表、客户端无感）。
        ⇒ 每步独立护栏：一步坏只坏它自己（并限频入库），其余照跑。
        """
        async def _guard(name, coro):
            """跑一步；异常隔离（一步坏只坏它自己），并记下**是哪一步**。

            [同步步骤也支持 2026-11-15] 有的步骤本来就是**同步函数**
            （例：`_jog_watchdog`）⇒ 传进来的是它的返回值（None）⇒ 直接
            `await coro` 会抛 "object NoneType can't be used in 'await'
            expression" ⇒ **整拍从这一步起全废**（实测：服务端启动后每秒
            报一次"自动控制异常"，而点动看门狗其实一直没跑）。
            ⇒ 用 `inspect.isawaitable` 判：可等待才 await。
            """
            try:
                if inspect.isawaitable(coro):
                    await coro
            except Exception as exc:                    # noqa: BLE001
                self._log_auto_error(exc, name)

        await _guard("遥测推送", self._publish_telemetry(now_ms))
        await _guard("点动看门狗", self._jog_watchdog(now_ms))
        # [运行保护与流量计告警 2026-11-15] 排在自动控制之后 ⇒ 保护动作
        # 停设备时，控制器状态已是本拍的最新值。
        await _guard("自动控制", self._auto_step(now_ms))
        await _guard("流量计告警", self._flowmeter_alarm_step(now_ms))
        await _guard("空管保护", self._protect_step(now_ms))
        await _guard("压差保护", self._protect_press_step(now_ms))
        await _guard("电机超温保护", self._protect_motor_over_temp_step(now_ms))
        # [设备故障边沿 2026-11-15] 变频器故障 ⇒ 报警（进报警历史表）+
        # 锁存 + 自动停设备。
        await _guard("设备故障", self._watch_vfd_fault(now_ms))
        if now_ms >= self._health_next_ms:
            self._health_next_ms = now_ms + self.HEALTH_TICK_MS
            await _guard("设备健康", self._health_tick(now_ms))

    # ==================================================================
    #            运行保护 + 流量计告警（2026-11-15 用户定案）
    # ==================================================================

    async def _publish_alarm_event(self, kind, text, dev_kind=None,
                                   now_ms=None):
        """推一条告警事件（TOPIC_AUTO_ALARM）+ 落 alarms 表。

        [为什么自己推而不是塞进自动控制的告警队列] 流量计告警是**设备层**
        事件，与自动控制无关（手动模式也会发生）⇒ 走独立通道；但帧形
        {kind,atMs,text(,dev)} 与自动告警一致 ⇒ 客户端只有一套落表逻辑。

        [dev 键] 告警的**设备类别**（proto.KIND_*）——缺它的话客户端只能
        把类型列写成"自动运行"，流量计告警会显示成"自动运行 | 流量计"。

        [入库时刻] alarms 表的 ts 用**墙钟**（insert_alarm 内部取），与
        既有自动告警入库同口径（帧里的 atMs 是单调钟，只用于排序）。
        """
        at_ms = int(now_ms if now_ms is not None else self._now_ms())
        doc = {"kind": int(kind), "atMs": at_ms, "text": str(text)}
        if dev_kind is not None:
            doc[proto.KEY_AUTOALARM_DEV] = int(dev_kind)
        try:
            await self.server.publish(proto.TOPIC_AUTO_ALARM, bson.encode(doc))
        except Exception as exc:                        # noqa: BLE001
            # 推送失败不挡入库：告警链路自身不能成为新故障源。
            self._log_with_store("告警发布失败（已留痕入库）：%s: %s"
                                 % (type(exc).__name__, exc),
                                 proto.LOG_CAT_ALARM)
        try:
            # [alarms 表的 kind/source 按设备类别 2026-11-15] 流量计告警与
            # 变频器故障分列（表里的"来源"要能一眼看出是哪台设备的事）。
            #
            # [2026-11-15 审计] 原判据只有"是不是流量计"，**其余一律落成
            # "device / 变频器"** ⇒ 压差过高（dev=模拟量）进库后来源写着
            # "变频器"（张冠李戴；运维照着查表会找错设备）。⇒ 按 dev 类别
            # 逐个映射，认不出来的落"自动运行/其它"。
            db_kind, db_source = self._alarm_db_kind_source(dev_kind)
            # [dev 落库 2026-11-19] 原来只映射 kind/source、dev 丢掉 ⇒ 保护
            # 类告警（压差过高/空管超时）进报警表后 dev 列是 NULL ⇒ 报警页
            # 的「类型」列只能按"自动运行"显示（帧里有 dev、表里没有 ⇒ 两
            # 个口径打架）。健康边沿那条路一直带 dev（见 collect_health 的
            # insert_alarm 调用）⇒ 这里对齐。
            self.store.insert_alarm(db_kind, db_source, str(text),
                                    dev=dev_kind)
        except Exception as exc:                        # noqa: BLE001
            self._log_with_store("告警入库失败：%s: %s"
                                 % (type(exc).__name__, exc),
                                 proto.LOG_CAT_ALARM)

    @staticmethod
    def _alarm_db_kind_source(dev_kind):
        """告警的设备类别 → (alarms 表 kind, alarms 表 source)。"""
        table = {
            proto.KIND_VFD: ("device", "变频器"),
            proto.KIND_TEMPHUM: ("device", "温湿度"),
            proto.KIND_FLOWMETER: ("flowmeter", "流量计"),
            proto.KIND_ANALOG_IO: ("analog", "模拟量"),
            proto.KIND_BUS: ("device", "总线"),
            proto.KIND_DIGITAL_IO: ("digital", "数字量"),
            proto.KIND_VALVE: ("valve", "电动球阀"),
            proto.KIND_WATER: ("device", "水"),
        }
        if dev_kind is None:
            return ("auto", "自动运行")
        return table.get(int(dev_kind), ("auto", "其它"))

    @staticmethod
    def _flowmeter_alarm_text(mask):
        """流量计报警位图 → 中文（顺序与客户端 fmAlarmText 同款：
        空管 → 上限 → 下限 → 系统）。"""
        names = []
        if mask & 4:
            names.append("空管")
        if mask & 1:
            names.append("上限")
        if mask & 2:
            names.append("下限")
        if mask & 8:
            names.append("系统")
        if not names:
            return "流量计报警已恢复"
        return "流量计报警：%s" % "、".join(names)

    async def _flowmeter_alarm_step(self, now_ms):
        """流量计报警位**变化** ⇒ 推一条告警 + 落 alarms 表。

        [为什么要边沿] 报警位是**状态**（现场实测可能长期挂着"空管"），
        每拍推一次会把报警页刷爆 ⇒ 只在"出现/消失"时各推一条
        （与阀告警的 issued/cleared 同款）。

        [第一次只记基线、不报] 服务端启动时流量计可能**已经**挂着空管
        （现场如此）——那不是"新发生的报警"，只是一直如此 ⇒ 首帧只把
        当前位图记成基线（不推、不入库）。否则每次重启都会多一条
        "流量计报警：空管"（把真事件淹掉）。之后再有变化才报。

        [离线不算"恢复"] 流量计离线时报警位保持上一次的值 ⇒ 不参与
        边沿判定，否则一掉线就报一条"已恢复"（假消息比没消息更坏）。
        """
        fm = getattr(self, "flowmeter", None)
        if fm is None or not getattr(fm, "online", False):
            return
        try:
            mask = int(fm.alarm_mask())
        except Exception:                               # noqa: BLE001
            return
        if self._fm_alarm_mask < 0:
            # [首帧只记基线] 见上面的说明。
            self._fm_alarm_mask = mask
            return
        if mask == self._fm_alarm_mask:
            return
        self._fm_alarm_mask = mask
        text = self._flowmeter_alarm_text(mask)
        await self._publish_alarm_event(proto.AUTO_ALARM_FLOWMETER, text,
                                        proto.KIND_FLOWMETER, now_ms)

    def _inlet_pump_on(self):
        """进水泵是否在开（DO 回显；泵无反馈点，输出即状态）。

        [数字量模块不在线/数据不新鲜 ⇒ 判"没开"] 泵的实际状态此时**未知**
        ⇒ 不能拿它当"泵开着"的证据（否则会拿一个猜出来的状态去停设备）。
        """
        bank = getattr(self, "pump_bank", None)
        if bank is None:
            return False
        for item in getattr(bank, "items", None) or []:
            try:
                if bool(getattr(item, "valid", False)) \
                        and bool(getattr(item, "on", False)):
                    return True
            except Exception:                           # noqa: BLE001
                continue
        return False

    def _flowmeter_empty_alarm(self, now_ms):
        """流量计是否**有效**地报空管（离线/数据陈旧 ⇒ False）。

        [读不到 ≠ 空管] 拿"没数据"当"空管"会把整条线停掉 ⇒ 读不到就
        不判定（保护不动作）。
        """
        fm = getattr(self, "flowmeter", None)
        if fm is None or not getattr(fm, "online", False):
            return False
        try:
            last_ok = int(getattr(fm, "last_ok_ms", 0) or 0)
            stale_ms = int(getattr(self.cfg, "flowmeter_stale_ms", 0) or 0)
            if last_ok <= 0 or (now_ms - last_ok) > stale_ms:
                return False
            return bool(getattr(fm, "alarm_empty", False))
        except Exception:                               # noqa: BLE001
            return False

    async def _protect_press_step(self, now_ms):
        """压差过大保护：压差连续超上限达去抖时长 ⇒ 报警 +（按配置）停机。

        [读不到 ≠ 压差过大] 模拟量通道离线/未标定 ⇒ **不判定**（不动作）
        —— 拿"没数据"当"压差过大"会把正在生产的线停掉。

        [为什么要去抖 2 秒] 滤芯堵塞是分钟级过程，但启泵水锤/流量突变会
        造成瞬时尖峰 ⇒ 要求**连续**超过上限 2 秒才动作。

        [动作后锁存] 告警进报警表 ⇒ 走**故障锁存**（与设备故障同款）：
        压力回落后**不自动恢复**，必须人工复位才能再启动设备（这正是
        保护的意义：让人确认滤芯/水路处理好了再开）。

        [压差取数同源] 走 ctx.press_mpa()（与界面压差卡、历史 press_mpa、
        自动循环切相阈值同一条换算链）⇒ 三处不会漂移。
        """
        cfg = self.cfg
        if not getattr(cfg, "protect_press_high_enabled", False):
            self._press_high_since_ms = 0
            return
        # [锁存已在 ⇒ 不再重复动作] 压差还高着时若每 2 秒报一次，报警表
        # 会被刷爆（实测：一次超压 ⇒ 连续两条告警 + 两次全停）。锁存未解
        # 说明"人还没确认过" ⇒ 不重复动作，等人工复位后才有下一次机会。
        if self._fault_latched():
            self._press_high_since_ms = 0
            return
        limit = float(getattr(cfg, "protect_press_high_limit_mpa", 0.0) or 0.0)
        if limit <= 0.0:
            self._press_high_since_ms = 0
            return
        try:
            mpa = self.auto_ctx.press_mpa()
        except Exception:                               # noqa: BLE001
            mpa = None
        if mpa is None or float(mpa) <= limit:
            self._press_high_since_ms = 0
            return
        if not self._press_high_since_ms:
            self._press_high_since_ms = now_ms
            return
        if (now_ms - self._press_high_since_ms) <                 proto.PROTECT_PRESS_HIGH_DEBOUNCE_MS:
            return
        # ---- 到时限：动作一次 ----
        self._press_high_since_ms = 0
        stop_devices = bool(getattr(cfg, "protect_press_high_stop", True))
        text = ("压差过高：%.3f MPa（超过上限 %.3f）%s"
                % (float(mpa), limit,
                   "，已停止设备" if stop_devices
                   else "（只报警，未停设备；报警已锁存，需人工复位）"))
        await self._publish_alarm_event(proto.AUTO_ALARM_PRESS_HIGH, text,
                                        proto.KIND_ANALOG_IO, now_ms)
        self._log_with_store("运行保护：%s" % text, proto.LOG_CAT_ALARM)
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        try:
            # [锁存 2026-11-15] 压差过大进了报警表 ⇒ 锁存：必须人工复位
            # 才能再启动设备（停机与否由配置决定）。
            ctl.raise_fault(proto.AUTO_ALARM_PRESS_HIGH, text, now_ms,
                            stop_devices=stop_devices,
                            enqueue_alarm=False)   # 本方法已自己发过这条告警
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)

    async def _protect_motor_over_temp_step(self, now_ms):
        """电机超温保护：温度传感器（模拟量 CH1，通道远端可配）连续超
        阈值达去抖时长 ⇒ 报警（kind=电机超温）+（按配置）停机。

        [读不到 ≠ 超温] 传感器离线/未标定 ⇒ **不判定**（同压差保护口径：
        拿"没数据"当"超温"会把正在生产的线停掉）。
        [自动模式保护] 与压差保护同款——**手动/自动模式都判**（运行保护
        不分模式；自动模式跑起来时同样会把线停下来）。
        [锁存] 告警进报警表 ⇒ 故障锁存，温度回落**不自动恢复**，人工复位
        后才能再启动。
        [禁用] enabled=false 时完全不判定（用户口径「可禁用」）。
        """
        cfg = self.cfg
        if not getattr(cfg, "protect_motor_over_temp_enabled", False):
            self._motor_over_temp_since_ms = 0
            return
        if self._fault_latched():
            self._motor_over_temp_since_ms = 0
            return
        limit = float(getattr(cfg, "protect_motor_over_temp_limit_c",
                              0.0) or 0.0)
        if limit <= 0.0:
            self._motor_over_temp_since_ms = 0
            return
        try:
            temp_c = self.cfg.analog.temp_c(
                lambda ch: self.analog.ma(ch)
                if self.analog is not None else None)
        except Exception:                               # noqa: BLE001
            temp_c = None
        if temp_c is None or float(temp_c) <= limit:
            self._motor_over_temp_since_ms = 0
            return
        if not self._motor_over_temp_since_ms:
            self._motor_over_temp_since_ms = now_ms
            return
        if (now_ms - self._motor_over_temp_since_ms) \
                < proto.PROTECT_MOTOR_OVER_TEMP_DEBOUNCE_MS:
            return
        self._motor_over_temp_since_ms = 0
        stop_devices = bool(getattr(cfg, "protect_motor_over_temp_stop", True))
        text = ("电机超温：%.1f ℃（超过阈值 %.1f ℃）%s"
                % (float(temp_c), limit,
                   "，已停止设备" if stop_devices
                   else "（只报警，未停设备；报警已锁存，需人工复位）"))
        await self._publish_alarm_event(
            proto.PROTECT_ALARM_KIND_MOTOR_OVER_TEMP, text,
            proto.KIND_ANALOG_IO, now_ms)
        self._log_with_store("运行保护：%s" % text, proto.LOG_CAT_ALARM)
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        try:
            ctl.raise_fault(proto.PROTECT_ALARM_KIND_MOTOR_OVER_TEMP,
                            text, now_ms,
                            stop_devices=stop_devices,
                            enqueue_alarm=False)   # 本方法已自己发过这条告警
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)

    async def _protect_step(self, now_ms):
        """运行保护一拍：进水泵开着、流量计仍报空管，持续超时限 ⇒ 动作。

        [判据] ① 进水泵在开（DO 回显为开）；② 流量计在线且数据新鲜、并报
        **空管**；③ ①∧② **连续**成立 ≥ `inletNoFlowTimeoutMs`（0 = 关闭
        该保护）。

        [为什么算"连续成立"的时长而不是"泵开了多久"] 泵已经开着很久、
        流量计此刻才闪一次空管位 ⇒ 若按"泵开时长"判会**立刻**停线（把一
        次瞬时误报当保护事件）⇒ 按"空管持续时长"判更稳：必须**连续**空管
        满一个时限才动作。

        [动作] 报一条告警（kind=流量计）+ 按配置**停止设备**
        （`inletNoFlowStop`=True）或只报警（False）。
        停止走**与现场停止按钮同款**的通路（自动模式按逐模式配置复位阀、
        手动模式一律关阀）——见 autocontrol.stop 的 reset_valves 口径。

        [只动作一次] 动作后清零计时；泵已停 ⇒ 条件自然不成立 ⇒ 不会每拍
        重复动作。
        """
        timeout_ms = int(getattr(self.cfg, "protect_inlet_noflow_timeout_ms",
                                 0) or 0)
        if timeout_ms <= 0:
            self._protect_since_ms = 0
            return
        # [锁存已在 ⇒ 不再重复动作] 同压差保护：条件还成立时每超时周期
        # 报一次会把报警表刷爆 ⇒ 锁存未解（人没确认）就不重复动作。
        if self._fault_latched():
            self._protect_since_ms = 0
            return
        if not self._inlet_pump_on() or not self._flowmeter_empty_alarm(now_ms):
            self._protect_since_ms = 0
            return
        if not self._protect_since_ms:
            self._protect_since_ms = now_ms
            return
        if (now_ms - self._protect_since_ms) < timeout_ms:
            return
        # ---- 到时限：动作一次 ----
        self._protect_since_ms = 0
        stop_devices = bool(getattr(self.cfg, "protect_inlet_noflow_stop",
                                     False))
        text = ("进水泵开启期间流量计持续报空管 %.0f 秒%s"
                % (timeout_ms / 1000.0,
                   "，已停止设备" if stop_devices
                   else "（只报警，未停设备；报警已锁存，需人工复位）"))
        await self._publish_alarm_event(proto.AUTO_ALARM_FLOWMETER, text,
                                        proto.KIND_FLOWMETER, now_ms)
        self._log_with_store("运行保护：%s" % text, proto.LOG_CAT_ALARM)
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        try:
            # [锁存 2026-11-15] 保护动作进了报警表 ⇒ 锁存：必须人工复位
            # 才能再启动设备（stop_devices=False —— 停机与否由配置决定，
            # 但"要不要人工确认"由锁存保证）。
            ctl.raise_fault(proto.AUTO_ALARM_FLOWMETER, text, now_ms,
                            stop_devices=stop_devices,
                            enqueue_alarm=False)   # 本函数已自己发过这条告警
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)

    async def _auto_step(self, now_ms):
        """跑一拍自动控制。

        [两个入口，职责不同]
            poll_idle()  受理现场按键 + 急停闸 —— **每拍都调**，不要求控制器
                         在跑。停止按钮和急停必须在任何时刻都有人接（控制器
                         没跑时按停止也得把设备停下来），这是安全要求。
            step()       跑起来之后的工艺逻辑 —— 只在 running 时调。

        [为什么必须包 try] 自动逻辑是现场工艺代码，将来一定会有人写出除零/
        越界/None 比较之类的异常。它崩了**不能**把整个服务端的 tick 带死 ——
        那会连遥测推送和设备健康一起停摆，界面全黑，比"自动逻辑不工作"严重
        得多。故这里吞掉并限频记日志。

        [告警抽取与模式/运行态无关] 对齐 C 侧 HsfServerApp.c
        hsfAppPushAutoAlarms：停机状态下也可能有在队报警要清（全停失败的
        ActionFailed 在控制器已停后才入队）。[抽取时点 2026-11-13·对齐 C]
        C 端在 tick 末尾（step 之后）抽取 ⇒ step 内产生的报警（阀超时中止
        等）**当拍**推送；Python 原先在 step 之前抽取 ⇒ 同一条报警要等下一
        拍（一拍之差）。现把抽取挪到 step 之后；停机时跳过 step 但仍执行
        末尾抽取（停机清队路径不丢）。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        # ---- 每拍：按键受理 + 急停闸（不依赖 running）----
        try:
            ctl.poll_idle(now_ms, self.is_auto())
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)
        if ctl.running:
            try:
                ctl.step(now_ms)
            except Exception as exc:                    # noqa: BLE001
                self._log_auto_error(exc)
        # ---- 每拍：抽取在队报警（step 之后 ⇒ step 内产生的报警当拍可见）----
        await self._drain_auto_alarms(ctl)

    async def _drain_auto_alarms(self, ctl):
        """把控制器在队报警逐条广播（TOPIC_AUTO_ALARM）并入库。

        [一帧一条] 对齐 C 侧 XAutoControlCodec_alarmToBson 的单条报警帧：
        载荷键 {kind:int, atMs:int, text:str}（键名与 C 侧编解码逐字一致，
        见 XAutoControlCodec.c 的 ALARM_KIND/ALARM_AT/ALARM_TEXT）。
        take_alarm 取走即消费 —— 同一条报警绝不出两次帧。

        [无人订阅也不丢] publish 无人订阅时静默为 0（与 C publish_ex 同
        口径），但日志与告警入库照做：事件留痕在库里，不依赖有人在看。
        """
        while True:
            try:
                alarm = ctl.take_alarm()
            except Exception as exc:                    # noqa: BLE001
                self._log_auto_error(exc)
                return
            if alarm is None:
                return
            kind = int(alarm.get("kind", 0))
            at_ms = int(alarm.get("atMs", 0))
            text = str(alarm.get("text", ""))
            try:
                payload = bson.encode({"kind": kind, "atMs": at_ms,
                                       "text": text})
                await self.server.publish(proto.TOPIC_AUTO_ALARM, payload)
            except Exception as exc:                    # noqa: BLE001
                # 推送失败不挡入库与下一条：告警链路自身不能成为新故障源。
                self._log_with_store("自动告警发布失败（已留痕入库）：%s: %s"
                                     % (type(exc).__name__, exc),
                                     proto.LOG_CAT_ALARM)
            # 类别 = alarm（自动运行告警；与 C 侧告警入库同分类口径）。
            self._log_with_store("自动运行告警 类型%d @%d：%s"
                                 % (kind, at_ms, text), proto.LOG_CAT_ALARM)
            # 告警入库（kind=来源分类；与 C 侧 insertAlarm("auto",...) 同表
            # 同分类；写失败不影响推送与采集）。
            try:
                self.store.insert_alarm("auto", "自动运行", text)
            except Exception as exc:                    # noqa: BLE001
                self._log_with_store("自动告警入库失败：%s: %s"
                                     % (type(exc).__name__, exc),
                                     proto.LOG_CAT_ALARM)

    def _log_auto_error(self, exc, step=""):
        """自动层异常限频记日志（同一类错误 1 分钟一条）。

        [带调用栈 2026-11-15] 光有"TypeError: xxx"**定位不到地方** ⇒
        追加异常栈（最后 6 帧）⇒ 现场从 journald 一眼看出是哪条路径炸的。

        :param step: 出错的步骤名（tick 的分步护栏传进来；空 = 不分步）。
        """
        # 限频：同一类错误 1 分钟一条，避免把板端 journald 灌满。
        # [首次必须记] 用 `not last` 判「从未记过」——不能只靠
        # `now - last > 60000`：last 初值 0，而单调毫秒是从进程启动算的，
        # 启动后 1 分钟内发生的第一次异常会被这条件判成「刚记过」而吞掉，
        # 于是现场看到的是"自动逻辑明明崩了却一条日志都没有"。
        key = type(exc).__name__
        now_ms_i = self._now_ms()
        last = self._auto_err_at.get(key, 0)
        if (not last) or (now_ms_i - last > 60000):
            self._auto_err_at[key] = now_ms_i or 1
            # 类别 = auto：自动控制层自身的异常（与 autocontrol.py 内部
            # 「本拍异常」同一类事件，只是这层是外泄到 app 的兜底）。
            self._log_with_store("自动控制异常（已隔离，不影响采集/推送）%s：%s: %s"
                                 % ("[%s]" % step if step else "", key, exc),
                                 proto.LOG_CAT_AUTO)
            # [调用栈 2026-11-15] 单独一条（限频同上）：没有栈就定位不到
            # 具体路径（实测："TypeError: object NoneType can't be used in
            # 'await' expression" 光看消息根本看不出是谁 await 了 None）。
            try:
                import traceback
                stack = traceback.format_exc()
                lines = [ln for ln in stack.splitlines() if ln.strip()]
                self._log_with_store("自动控制异常调用栈：\n%s"
                                     % "\n".join(lines[-8:]),
                                     proto.LOG_CAT_AUTO)
            except Exception:                           # noqa: BLE001
                pass

    async def _publish_telemetry(self, now_ms):
        # [相机线程自愈 2026-11-20 板端实锅] 采集线程可能因未捕获异常静默
        # 死掉（实测：进程活着、健康照推、相机线程不见了、零日志）⇒
        # 每拍看一眼，线程不在且已启用就拉起来（ensure_alive 内部幂等）。
        cam = getattr(self, "camera", None)
        if cam is not None:
            try:
                if cam.ensure_alive():
                    self._log("相机采集线程已由看门狗拉起")
            except Exception as exc:                # noqa: BLE001
                self._log("相机看门狗异常：%s: %s"
                          % (type(exc).__name__, exc))
        # ---- 变频器 ----
        if self.vfd is not None and self._gate_due(proto.TOPIC_VFD_STATUS, 0,
                                                   now_ms):
            payload = self.encode_vfd(self.vfd)
            if payload is not None:
                await self.server.publish(proto.TOPIC_VFD_STATUS, payload)
        # ---- 温湿度 ----
        if self.temphum is not None and self._gate_due(
                proto.TOPIC_TEMPHUM_STATUS, 0, now_ms):
            await self.server.publish(proto.TOPIC_TEMPHUM_STATUS,
                                      self.encode_temphum(self.temphum))
        # ---- 流量计 ----
        if self.flowmeter is not None and self._gate_due(
                proto.TOPIC_FLOWMETER_STATUS, 0, now_ms):
            await self.server.publish(proto.TOPIC_FLOWMETER_STATUS,
                                      self.encode_flowmeter(self.flowmeter))
        # ---- 浊度仪（两台一帧）----
        if self.turbidity and self._gate_due(
                proto.TOPIC_TURBIDITY_STATUS, 0, now_ms):
            await self.server.publish(proto.TOPIC_TURBIDITY_STATUS,
                                      self.encode_turbidity())
        # ---- 进水泵 ----
        if self.pump_bank is not None:
            # 先刷新回显（可能被外部改动），再判推送闸
            self.pump_bank.poll(now_ms, self.cfg.pump_stale_ms)
            if self._gate_due(proto.TOPIC_PUMP_STATUS, 0, now_ms):
                await self.server.publish(proto.TOPIC_PUMP_STATUS,
                                          self.encode_pumps())
        # ---- 模拟量 ----
        if self.analog is not None and self._gate_due(
                proto.TOPIC_AI_STATUS, 0, now_ms):
            await self.server.publish(proto.TOPIC_AI_STATUS,
                                      self.encode_analog(self.analog))
        # ---- 数字量（逐模块一帧）----
        # 只发 publish=True 的模块：聚合架构下被聚合者的点已经在聚合者的
        # 地址空间里出现了，再给它发一帧会让客户端出现两套重复点位。
        # 它照样参与轮询与健康探测（见 collect_health），只是不出场景。
        # [当前现场已无聚合] 只剩 .13 一台且 publish=True，这条过滤恒放行；
        # 保留它是为了将来再挂第二台聚合模块时不重复发点。
        # ---- 名字脏帧：点位配置变更后的下一拍，先补一帧只含名字的 full 帧
        # （对齐 C 侧 hsfAppTickDigitalValve 里的 s_digitalNamesDirty 分支：
        # 事件帧随拍发一次即清标志，不占周期推送闸），再走周期状态推送。
        if self._digital_names_dirty:
            self._digital_names_dirty = False
            for i, dev in enumerate(self.digital):
                if not getattr(dev, "publish", True):
                    continue
                try:
                    await self.server.publish(
                        proto.TOPIC_DIGITAL_STATUS,
                        self.encode_digital_names(dev, i))
                except Exception as exc:                # noqa: BLE001
                    self._log("数字量名字帧发布失败：%s: %s"
                              % (type(exc).__name__, exc))
            self._log("数字量：通道名已变更，已主动下发名字帧")
        for i, dev in enumerate(self.digital):
            if not getattr(dev, "publish", True):
                continue
            if self._gate_due(proto.TOPIC_DIGITAL_STATUS, i, now_ms):
                # 周期推送＝增量帧（不带名字）；名字靠首连/订阅补发的全量帧。
                await self.server.publish(proto.TOPIC_DIGITAL_STATUS,
                                          self.encode_digital(dev, i, full=False))
        # ---- 电磁阀（6 路一帧）----
        # [排在按钮组之前 2026-11-15] 手动模式的启动灯要反映"设备在动"
        # （含电动球阀在开位），而阀的到位反馈是 valve_bank.poll() 刷新的
        # ⇒ 按钮组必须排在阀轮询之后，否则灯用的是上一拍的阀状态。
        if self.valve_bank is not None:
            # 反馈来自数字量模块缓存，故先刷新再推送（同一拍数据自洽）
            alarms = self.valve_bank.poll(now_ms)
            if alarms:
                # [立即推，不等 200ms 周期闸] 超时是事件，晚一拍无所谓但
                # 更不能被周期推送的闸吞掉（同拍可能有 6 路一起超时）。
                await self._publish_valve_alarms(alarms)
            if self._gate_due(proto.TOPIC_VALVE_STATUS, 0, now_ms):
                await self.server.publish(proto.TOPIC_VALVE_STATUS,
                                          self.encode_valves())
        # ---- 按钮组（操作柱：读按键 → 推状态机 → 写三盏灯）----
        # [放在数字量/阀推送之后] 它读的是 dev.di/dev.do 缓存；数字量那几帧
        # 刚推完，此刻的缓存就是本拍最新状态。灯的写入是异步投递，下一拍
        # 由回读校准，不影响本拍推送出去的数据自洽性。
        if getattr(self, "button_station", None) is not None:
            try:
                # [手动模式也写灯 2026-11-15] 启动灯在手动模式反映"现场还有
                # 设备在动"（搅拌泵/进水泵/任一球阀）；自动模式仍跟随自动
                # 运行态。详见 buttons.ButtonStation.poll。
                self.button_station.set_manual_running(self._manual_running())
                # [复位灯常亮 2026-11-18] 有报警锁存或急停锁存时，
                # 复位灯常亮 ⇒ 现场一眼看出「需要按复位」。
                _ctl = getattr(self, "auto_controller", None)
                self.button_station.set_need_reset(
                    bool(_ctl) and (bool(getattr(_ctl, "fault_latched", False))
                                     or bool(getattr(_ctl, "estop_latched", False))))
                self.button_station.poll(now_ms, self.is_auto())
            except Exception as exc:                    # noqa: BLE001
                # 按钮组异常不能把整个 tick 带死（自动/手动都要继续跑）
                self._log("按钮组：本拍处理失败 %s: %s"
                          % (type(exc).__name__, exc))
        # ---- 相机画面（分片推；只推新帧）----
        # [不进推送闸] 相机帧有自己的节拍（采集器按配置的 fps 出帧）⇒ 用
        # 「帧序号变了才推」判重，而不是固定 ms 闸（闸会把 1fps 的相机按
        # 100ms 闸推成同一帧重复推）。
        if self.camera is not None:
            try:
                await self._publish_camera_frame()
                self._camera_status = self.camera.status()
            except Exception as exc:                    # noqa: BLE001
                # 相机出错不能把整个 tick 带死（自动控制要继续跑）
                self._log("相机推帧失败：%s: %s" % (type(exc).__name__, exc))

    async def _publish_valve_alarms(self, alarms):
        """把阀告警推给**订阅了阀主题**的会话。

        [为什么用 TOPIC_VALVE_STATUS 而不是另开主题] 客户端关心阀就必然订阅
        了阀主题；告警是阀的子话题，塞进同一主题不会漏订阅。载荷带
        "alarms" 数组，与周期状态的键空间不冲突（状态用 v{i}_* 前缀），
        客户端按"载荷里有 alarms 就走告警分支"处理。

        [为什么不在 valves.py 里推] 设备层不持有传输层引用；且多次 poll
        调用（快照 handler 也会 poll）不应该重复推送——由这里统一出。
        """
        doc = {proto.KEY_ALARM_COUNT: len(alarms)}
        for i, alarm in enumerate(alarms):
            for key, value in alarm.items():
                doc["a%d_%s" % (i, key)] = value
        payload = bson.encode(doc)
        sent = await self.server.publish(proto.TOPIC_VALVE_STATUS, payload)
        for alarm in alarms:
            text = "%s（%s）" % (_ALARM_KIND_NAMES.get(
                alarm.get(proto.KEY_ALARM_KIND), "?"),
                alarm.get(proto.KEY_ALARM_TEXT))
            # 类别 = alarm（阀超时/无反馈；与 C 侧 hsfAppEventLog("alarm",...)
            # 的"阀告警 路%d..."同口径：alarms 表与 logs 表同一事件两处可查）。
            self._log_with_store("阀告警：%s → %s"
                                 % (alarm.get(proto.KEY_ALARM_VALVE_NAME), text),
                                 proto.LOG_CAT_ALARM)
            # 告警事件入库（kind=来源分类；写失败不影响推送与采集）。
            self.store.insert_alarm("valve",
                                    alarm.get(proto.KEY_ALARM_VALVE_NAME),
                                    text)
        return sent

    def _gate_due(self, topic, index, now_ms):
        gates = self._gates.get(topic)
        if not gates or index >= len(gates):
            return False
        return gates[index].due(now_ms)

    # ==================================================================
    #                          载荷编码
    # ==================================================================
    def encode_vfd(self, dev):
        """变频器遥测 BSON（按 XVfdCodec 的 telemetryKeys 键表裁剪）。

        文档恒含 "vm"（有效掩码）：只有 (mask & 位) 非 0 的键才写入——
        品牌未提供的字段不上线，解码方按掩码判无效。
        带 "ts"（设备采样时刻，epoch 毫秒；2026-11-13 与 C 端对齐：
        XVfdCodec 的 m_timestampMs 同键同义，推送与快照都带）。
        """
        if dev is None or not dev.online:
            return None
        doc = {proto.KEY_VALID_MASK: dev.valid_mask,
               proto.KEY_DEV_TS: int(getattr(dev, "last_ok_ms", 0) or 0)}
        for bit, key, kind in proto.VFD_TELEMETRY_FIELDS:
            val = dev.field_value(bit, kind)
            if val is None:
                continue
            doc[key] = val
        doc[proto.KEY_SIDE] = 0
        # [电机温度 2026-11-20 用户口径「搅拌泵卡片可以增加温度显示」]
        # 模拟量温度传感器（通道远端可配 tempChannel）的工程量 ℃。
        # 读不到/未配置 ⇒ **不上键**（缺键=无温度，不是 0℃——与浊度仪
        # v*i_temp 同口径）；编码失败不拦（不为一格温度丢整帧遥测）。
        temp = self._motor_temp_c()
        if temp is not None:
            doc["motorTempC"] = float(temp)
        try:
            return bson.encode(doc)
        except ValueError as exc:
            self._log("变频器遥测编码失败：%s" % exc)
            return None

    def _motor_temp_c(self):
        """模拟量温度传感器的工程量 ℃；未配置/无读数 ⇒ None。

        [同一换算链] 走 cfg.analog.temp_c（与超温保护/模拟量页同一条），
        别处再写一份换算必然漂移。
        """
        try:
            if self.analog is None:
                return None
            return self.cfg.analog.temp_c(
                lambda ch: self.analog.ma(ch))
        except Exception:                               # noqa: BLE001
            return None

    def encode_temphum(self, th, side=0):
        """温湿度快照：{"side","tempValid","humValid","staleMs"[,"temp","hum"]}。

        [按侧应答 2026-11-13 与 C 端对齐] 快照请求 {"side":0|1} 按侧取设备
        （本端温湿度装在 A 侧；非 0 侧＝该侧未装配）。C 客户端按侧分存，
        服务端不按侧回就会把 B 侧查询错铺成 A 侧数据。

        [固定键恒写] side/tempValid/humValid/staleMs **未装配也写**
        （valid=false；值键 temp/hum 仍只在有效时写——客户端据此区分
        「0」与「无数据」，对齐 C 端同款固定键口径）。
        """
        doc = {proto.KEY_SIDE: int(side),
               "tempValid": False,
               "humValid": False,
               proto.KEY_STALE_MS: self.cfg.temphum_stale_ms}
        if th is not None and int(side) == 0:
            if th.temp_valid:
                doc["temp"] = th.temp_tenths()
            if th.hum_valid:
                doc["hum"] = th.hum_tenths()
            doc["tempValid"] = bool(th.temp_valid)
            doc["humValid"] = bool(th.hum_valid)
            doc[proto.KEY_STALE_MS] = self.cfg.temphum_stale_ms
        return bson.encode(doc)

    def encode_flowmeter(self, fm):
        """流量计快照：{"flow","speed","forwardTotal","reverseTotal",
        "flowUnit","totalUnit","alarms","valid","staleMs"}。

        [键名与客户端逐项对应] 客户端 `FlowMeterState.apply` 就是按这套键
        解包的，改一个名字那边就丢一个量。

        [单位用代码不用字符串] 0x1020 瞬时流量单位 / 0x1021 累积单位是
        **两张独立的码表**（瞬时 5=m³/h；累积 1=m³）。客户端查表转文本，
        与 C 端口径一致。曾把累积单位填成瞬时表的邻居码 → 界面显示 "?"。

        [alarms 是位图] 0=上限 1=下限 2=空管 3=系统（见 FlowMeter.alarm_mask）。
        """
        doc = {proto.KEY_SIDE: 0}
        if fm is not None:
            if fm.online:
                # 保留 4 位小数：流量计的分辨率通常到 0.001
                doc["flow"] = round(float(fm.flow), 4)
                doc["speed"] = round(float(fm.speed), 4)
                doc["forwardTotal"] = round(float(fm.forward_total), 3)
                doc["reverseTotal"] = round(float(fm.reverse_total), 3)
                doc["flowUnit"] = int(fm.flow_unit)
                doc["totalUnit"] = int(fm.total_unit)
                doc["alarms"] = int(fm.alarm_mask())
            # 未在线时不写值键：客户端据此显示 "--" 而不是 0（0 是"真的没流量"）
            doc[proto.KEY_VALID] = bool(fm.online)
            doc[proto.KEY_STALE_MS] = self.cfg.flowmeter_stale_ms
        return bson.encode(doc)

    def encode_turbidity(self):
        """浊度仪快照（两台一帧）：{"count","v0_ntu","v0_raw","v0_online",...}。

        [键名走 v{index}_ 前缀] 与阀/数字量同一风格：客户端按台号取，不必
        为每台仪器写一套字段名。将来加第三台也只是 count 变大。

        [ntu 发 ×100 的整数] 与模拟量工程值同一口径（定点整数），避免
        浮点在 BSON 上的精度争议。客户端 ÷100 显示两位小数。

        [温度随帧下发 —— 2026-11-10 改] 带温度的机型（ZHEYI-5100ZDY 站3）
        一帧 0x0000×4 同刻读出浊度+温度，界面要显示就得让它过线，故给
        "v{i}_temp"（℃ ×100，与 ntu 同口径）。**缺键 = 该机型没有温度**
        （站1 WZT-701A 只有 2 个寄存器、无温度）——不给 0℃，客户端显示
        "--"。旧口径「温度只进设备层日志、不进推送帧」已作废。

        [side 键 2026-11-13 与 C 端对齐] C 端推送/快照恒带侧号
        （hsfAppInsertSide，hsfAppFillTurbidity 调用点），本端此前漏发——
        C 客户端按侧分存时拿不到侧号。本现场浊度装在 B 侧；装配侧号随
        self.turbidity_side 给出（本端装配恒 0，与 encode_flowmeter 同口径；
        将来支持双侧装配时改成分装配侧）。
        """
        doc = {proto.KEY_COUNT: len(self.turbidity),
               proto.KEY_SIDE: 0}
        for idx, tm in enumerate(self.turbidity):
            prefix = "v%d_" % idx
            # 名字由服务端给（客户端不猜"是原水还是出水"）
            doc[prefix + proto.KEY_NAME] = tm.name
            doc[prefix + proto.KEY_TURB_ONLINE] = bool(tm.online)
            if tm.online:
                doc[prefix + proto.KEY_TURB_NTU] = int(round(tm.ntu * 100.0))
                doc[prefix + proto.KEY_TURB_RAW] = int(tm.raw)
                # 温度只有"这台表带温度且这一拍读到了"才有值
                if tm.temperature is not None:
                    doc[prefix + proto.KEY_TURB_TEMP] = int(
                        round(tm.temperature * 100.0))
        doc[proto.KEY_STALE_MS] = self.cfg.turbidity_stale_ms
        return bson.encode(doc)

    def encode_pumps(self):
        """进水泵快照：{"count","v0_name","v0_on","v0_ready","v0_text"}。

        [on 是**命令/回显状态**，不是"泵真的在转"] 现场没有反馈信号，
        故这里只能如实表示"输出是开着的"。界面上要写清"无反馈"，
        不能让人误以为这是实测的运行状态。
        """
        if self.pump_bank is None:
            return bson.encode({proto.KEY_COUNT: 0})
        doc = {proto.KEY_COUNT: len(self.pump_bank.items)}
        for item in self.pump_bank.items:
            prefix = "v%d_" % item.index
            doc[prefix + proto.KEY_PUMP_NAME] = item.name
            doc[prefix + proto.KEY_PUMP_ON] = bool(item.on)
            doc[prefix + proto.KEY_PUMP_READY] = bool(item.ready)
            doc[prefix + proto.KEY_VALID] = bool(item.valid)
            if item.text:
                doc[prefix + proto.KEY_PUMP_TEXT] = item.text
        doc[proto.KEY_STALE_MS] = self.cfg.pump_stale_ms
        return bson.encode(doc)

    def encode_analog(self, ai, full=False):
        """模拟量快照：{"full","n","v","r0..rN","u0..uN"} + 换算通道的 "e*" 键。

        r* = 原始寄存器（毫伏口径，排查真相用），u* = 微安（mA × 1000）。
        客户端按 u* 显示、按 v 位图判有效。

        [换算值也由服务端算] 配了工程量的通道额外下发：
            e<i>      换算值 ×1000（定点整数，避免浮点在 BSON 上的精度争议）
            eu<i>     单位（utf8，如 "MPa"）
            en<i>     通道名（如 "压差计"）
            ew<i>     越限说明（utf8；空=正常）
        为什么由服务端算而不是客户端：量程是**现场标定参数**，与接线同性质。
        放客户端意味着每台机器都要配一遍、改一次要升客户端；放服务端则一处
        生效，客户端只负责显示。

        [full/an{i} 2026-11-13 与 C 端对齐] C 端 hsfAppEncodeAi(bool full)：
        文档恒带 "full"；full=True（快照/订阅补发）时另带通道名键
        "an<i>"（有标定名的通道才发，无标定通道不发编造名）；full=False
        （周期推送）不带名字。本端此前两键全缺——C 客户端连 Python 服务端
        时通道名列恒空。periodic 推送传缺省 full=False，快照/订阅补发传 True。
        """
        doc = {proto.KEY_FULL: bool(full)}
        vmask = 0
        for i in range(ai.channels):
            if ai.valid[i]:
                vmask |= 1 << i
            doc["r%d" % i] = ai.raw[i]
            ma = ai.ma(i)
            doc["u%d" % i] = int(round(ma * 1000.0))
            scale = self.cfg.analog.scales.get(i)
            if full and scale is not None and scale.name:
                doc["an%d" % i] = scale.name
            if scale is not None and scale.configured and ai.valid[i]:
                value, note = scale.engineering(ma)
                if value is not None:
                    doc["e%d" % i] = int(round(value * 1000.0))
                    doc["eu%d" % i] = scale.unit
                    if scale.name:
                        doc["en%d" % i] = scale.name
                    if note:
                        doc["ew%d" % i] = note
                elif note:
                    # [断线 2026-11-20 用户实报] 电流 0mA ⇒ engineering()
                    # 返回 None（断线）⇒ 没有换算值。但**说明必须发下去**：
                    # 界面上要显示「断线」而不是光 "--"（现场分不清
                    # "没标定"还是"线断了"）。
                    doc["ew%d" % i] = note
                    if scale.name:
                        doc["en%d" % i] = scale.name
        doc["n"] = ai.channels
        doc["v"] = vmask
        return bson.encode(doc)

    def encode_digital(self, dev, index, full=True):
        """数字量快照：{"full","dev","di","do","count","valid","din<n>","don<n>"}。

        di/do 是位图 int（bit n = 通道 n），count 取该模块实际点数
        （单模块 16；聚合后 32 —— 客户端按 count 显示，不写死）。
        本现场 2026-11-06 起只剩 .13 一台 ⇒ count=16。

        [通道名由服务端推导] 被电磁阀占用的口，名字按**阀门点位映射**
        自动生成（"阀1 开到位"这种）。客户端因此不需要知道哪路阀接在哪个
        模块的哪个口 —— 接线改了服务端配置一改，客户端零改动跟着变。
        名字键只在**该模块上真的有占用**时才写，没被占用的口不发，
        客户端就显示 CH<n>（不占带宽、也不会被无意义的默认名淹没）。

        :param full: True=全量帧（带名字，客户端据此重建点表并收名字）；
                     False=周期增量帧（只带状态位图）。
                     [2026-11-05 跨端对齐] 之前恒发名字且**没有 full 键**，
                     而 C 端客户端只在 full=True 时解析名字 —— 跨端不一致
                     的症状是"点画出来了但全都没名字"。与 C 服务端
                     hsfAppEncodeDigital(bool) 同口径：首连/订阅补发/快照
                     应答 full=True，周期推送 full=False。
        """
        doc = {
            proto.KEY_FULL: bool(full),
            proto.KEY_DEV: index,
            proto.KEY_NAME: dev.name,
            proto.KEY_DI: dev.di_mask(),
            proto.KEY_DO: dev.do_mask(),
            proto.KEY_COUNT: dev.channels,
            proto.KEY_VALID: bool(dev.online),
            proto.KEY_STALE_MS: self.cfg.digital_stale_ms,
        }
        if not full:
            return bson.encode(doc)
        names = self._digital_channel_names(index, dev.channels)
        # 泵占用的 DO 也登记（DO6 = 进水泵），与阀门名汇进同一张表
        self._pump_channel_names(index, dev.channels, names)
        # 按钮组占的 4 DI / 3 DO 也登记（DI12~15 / DO7~9）
        self._button_channel_names(index, dev.channels, names)
        for channel, text in names.get("di", {}).items():
            doc[proto.KEY_DI_NAME_FMT % channel] = text
        for channel, text in names.get("do", {}).items():
            doc[proto.KEY_DO_NAME_FMT % channel] = text
        return bson.encode(doc)

    def mark_digital_names_dirty(self, reason=""):
        """通知「通道名已变更」：下一拍主动下发一帧只含名字的 full 帧。

        对齐 C 侧 HsfServerApp_notifyChannelNamesChanged（HsfServerApp.c:848）：
        名字本质是部署参数，运行期通常不变；本入口给"配置变更/将来热改名"
        预留通路。真正的推送在 _publish_telemetry 数字量段（标志消费后清零，
        只补一帧，不随周期刷屏）。

        :param reason: 日志留痕（哪个环节改了名字），空串不打日志。
        """
        self._digital_names_dirty = True
        if reason:
            self._log("数字量：通道名已变更（%s），下一拍下发名字帧" % reason)

    def encode_digital_names(self, dev, index):
        """名字专用帧：{full:true, dev, name, count, din%d/don%d 名字键}。

        对齐 C 侧 hsfAppEncodeDigitalNames（HsfServerApp.c:3544）的键口径：
        **只含名字键、不带状态位图**（di/do/valid/staleMs 都不上）——客户端
        按"载荷里有名字键就更新名字"处理，与 full 帧的差别只是没有状态。
        full 键恒为 true：客户端靠它走"全量"分支，不靠"有没有状态键"猜。

        [与 C 的差异（py 多模块布局所需）] C 单模块，名字帧只有
        full/count/名字键；py 支持多台数字量模块，补 dev（模块序号）与
        name（模块名）键供客户端路由，其余键与 C 逐字一致。
        """
        doc = {
            proto.KEY_FULL: True,
            proto.KEY_DEV: index,
            proto.KEY_NAME: dev.name,
            proto.KEY_COUNT: dev.channels,
        }
        names = self._digital_channel_names(index, dev.channels)
        # 泵/按钮组占用的口也登记 —— 与 encode_digital(full=True) 同一
        # 张汇合表（三处点位映射就是名字的唯一事实源）。
        self._pump_channel_names(index, dev.channels, names)
        self._button_channel_names(index, dev.channels, names)
        for channel, text in names.get("di", {}).items():
            doc[proto.KEY_DI_NAME_FMT % channel] = text
        for channel, text in names.get("do", {}).items():
            doc[proto.KEY_DO_NAME_FMT % channel] = text
        return bson.encode(doc)

    def _digital_channel_names(self, module_index, channels):
        """从阀门点位映射反推该模块上每个被占用口的语义名。

        :returns: {"di": {通道: 名字}, "do": {通道: 名字}}

        [为什么从 valve_bank 推导而不是另建一张名字表] 点位映射已经是
        "哪路阀用哪个口"的唯一事实源。再维护一张名字表就是第二份真相，
        接线一改必然漏改一处（实锅：模块下标与名字表对不上）。
        这里直接读 valve_bank.items 的 do/open_di/close_di，天然同步。

        [越界/未配置的保护] 未配点位的路（module/do/DI 为 None）跳过；
        通道号超出该模块点数也跳过（配置写错了不该让整个快照崩）。
        """
        out = {"di": {}, "do": {}}
        bank = self.valve_bank
        if bank is None:
            return out
        for item in bank.items:
            if not item.ready or item.module != module_index:
                continue
            for channel, suffix in ((item.do, "控制"),
                                    (item.open_di, "开到位"),
                                    (item.close_di, "关到位")):
                if channel is None:
                    continue
                channel = int(channel)
                if not 0 <= channel < channels:
                    continue
                kind = "do" if suffix == "控制" else "di"
                out[kind][channel] = "%s %s" % (item.name, suffix)
        return out

    def _pump_channel_names(self, module_index, channels, out):
        """把进水泵占用的 DO 也登记进数字量通道名（就地补进 out）。

        [为什么并进同一张表] 数字量页显示的就是"这个口是干什么用的"。
        泵占着 DO6，如果那里只显示 `DO6`，现场看到一屋子阀门名里夹着个
        光秃秃的通道号，会以为是没配置的多余点。

        [为什么单独一个函数而不是塞进 _digital_channel_names] 那个函数只认
        valve_bank（它的注释明确说了"不另建名字表"）。泵是另一类外设、
        另一份配置，硬塞进去会让"唯一事实源"变成"两个源拼起来"。
        这里各读各的配置，最终汇进同一张输出表。
        """
        bank = self.pump_bank
        if bank is None:
            return
        for item in bank.items:
            if not item.ready or item.module != module_index:
                continue
            channel = int(item.do)
            if 0 <= channel < channels:
                out["do"][channel] = item.name

    def _button_channel_names(self, module_index, channels, out):
        """把按钮组的 4 个 DI 与 3 个 DO 也登记进通道名（就地补进 out）。

        [为什么也要登记] 数字量页显示的是"这个口是干什么用的"。按钮组
        占了 DI12~15 / DO7~9，如果不登记，现场看到的是 `DI12`、`DO7`
        这种光秃秃的号，还得对着图纸数 —— 正是用户要求"将通道名字加入"
        的原因。

        [名字措辞] 输入侧带**动作语义**（"急停"而不是"急停按钮"），因为
        该点表达的是"回路是否正常"；输出侧是灯，写成"启动按钮灯"。
        """
        st = getattr(self, "button_station", None)
        if st is None:
            return
        items = st.items
        try:
            mod = int(items.get(buttons.KEY_MODULE) or 0)
        except (TypeError, ValueError):
            mod = 0
        if mod != module_index:
            return

        di_names = (
            (buttons.KEY_ESTOP, "急停（常闭）"),
            (buttons.KEY_START, "启动按钮"),
            (buttons.KEY_STOP, "停止按钮"),
            (buttons.KEY_RESET, "复位按钮"),
        )
        do_names = (
            (buttons.KEY_START_LAMP, "启动按钮灯"),
            (buttons.KEY_STOP_LAMP, "停止按钮灯"),
            (buttons.KEY_RESET_LAMP, "复位按钮灯"),
        )
        for key, text in di_names:
            ch = st._ch(key)
            if ch is not None and 0 <= ch < channels:
                out["di"][ch] = text
        for key, text in do_names:
            ch = st._ch(key)
            if ch is not None and 0 <= ch < channels:
                out["do"][ch] = text

    def _on_button_state(self, old, new):
        """按钮组状态迁移回调（日志 + 推送健康变化）。

        [为什么用回调而不是在 poll 里发] ButtonStation 只管设备侧采集与灯，
        不该知道"怎么发告警/健康"。app 在这里接住状态变化，统一走既有
        日志与健康通道，两边职责不混。
        """
        # 类别 = ctrl：现场操作柱的按键迁移（启动/停止/急停/复位）属于控制
        # 输入事件，与远程控制命令同一类，运维审计时要在一条时间线上看。
        self._log_with_store("按钮组：%s → %s" % (old, new),
                             proto.LOG_CAT_CTRL)
        # 状态变化必然影响健康表（健康里带按钮组这一条），打脏让下一拍重算
        self._health_dirty = True

    async def refresh_valve_sources(self, force=False):
        """让电磁阀用到的数字量模块现读一次（去重 + 节流）。

        [为什么要去重] 6 路阀可能全挂在同一台模块上（现场就是这样：
        module 0 = .12 挂 4 路、module 1 = .13 挂 2 路）。逐路现读会把
        同一台模块读 4 遍 —— 去重后每台只读一次，再由 valve_bank 统一取值。

        :param force: True=忽略「刚读过」的节流，强制现读。

        [为什么要 force] 阀控制命令处理完要用**命令之后**的反馈生成应答。
        非 force 时 read_fresh 遇到 `now - last_ok_ms < 100ms` 会直接复用
        缓存 —— 而那条缓存极可能就是命令**之前**那一拍读到的旧反馈。实测
        症状：点"开阀"应答里 v0_state 还是"已关"，界面第一下看着没反应，
        得再点一次才变（第二次应答碰巧赶上了新一轮轮询）。
        故命令后必须绕过节流强制读一帧，或在缓存太新但早于命令时刻时
        至少让阀状态机按"命令已下发"重新判读。
        """
        if self.valve_bank is None:
            return
        seen = set()
        for item in self.valve_bank.items:
            if not item.ready or item.module is None:
                continue
            if item.module in seen:
                continue
            seen.add(item.module)
            if 0 <= item.module < len(self.digital):
                if force:
                    # 节流阈值给 0 ⇒ 无条件现读（命令后必须看到真实反馈）。
                    await devices.read_fresh(self.digital[item.module],
                                             self._now_ms(),
                                             min_interval_ms=0)
                else:
                    await devices.read_fresh(self.digital[item.module],
                                             self._now_ms())

    def encode_valves(self):
        """电磁阀 6 路状态（键表见 proto.KEY_VALVE_*）。"""
        if self.valve_bank is None:
            return bson.encode({proto.KEY_VALVE_COUNT: 0})
        return bson.encode(self.valve_bank.to_doc())

    def encode_health(self):
        """设备健康快照：{"devCount","seq","ts","dN_*"}（键名与 C 侧逐字一致）。"""
        entries = self.collect_health(self._now_ms())
        doc = {
            proto.KEY_DEV_COUNT: len(entries),
            proto.KEY_DEV_SEQ: self._health_seq,
            proto.KEY_DEV_TS: int(time.time() * 1000),
        }
        for i, e in enumerate(entries):
            doc["d%d_name" % i] = e.name
            doc["d%d_kind" % i] = e.kind
            doc["d%d_side" % i] = e.side
            doc["d%d_health" % i] = e.health
            doc["d%d_error" % i] = e.error
            doc["d%d_online" % i] = bool(e.online)
            doc["d%d_runState" % i] = e.run_state
            if e.text:
                doc["d%d_text" % i] = e.text
            # 空地址不发（老客户端不受影响；也避免给"没有连接概念"的
            # 汇总类条目发一个空字符串占位）
            if e.addr:
                doc["d%d_addr" % i] = e.addr
        return bson.encode(doc)

    # ---- 连接地址文案（给人看） ----
    def _addr_rtu(self, unit_id, bus="a"):
        """485 设备地址：``COM17 · 115200 · 站 2``（路 B 则是 9600 那个口）。

        [为什么三个都要写] 现场排故障时这三个缺一不可：口接错、波特率不匹配、
        站号撞车，现象都是"通讯不上"，只看 IP 或只看"某口"根本判不出来。
        原来这些参数只存在于配置文件里，界面上看不到 —— 操作员报"搅拌泵离线"，
        运维还得先 SSH 上去翻配置才知道该查哪个站。

        [两条独立 485 必须分开显示] 用户反馈：「变频器那路是115200是对的，
        另一路是9600，而且显示的串口号是一样的，他们明明是两路独立的」。
        路 A（变频器/温湿度）取 cfg.serial，路 B（流量计/浊度仪）取
        cfg.serial_b；之前所有设备都用 cfg.serial，界面上 5 台设备全是
        "COM17 · 115200"，与实际接线不符、排障时误导。
        """
        c = self.cfg.serial_b if bus == "b" else self.cfg.serial
        return "%s · %d · 站 %s" % (c.port, int(c.baudrate), unit_id)

    def _addr_tcp(self, host, port, unit_id=None):
        """TCP 设备地址：``192.168.0.13:502``（Modbus TCP 站号多为 1，不喧宾夺主）。

        站号只有不等于 1 时才补上去 —— 现场 99% 是 1，写出来只是噪声。
        """
        text = "%s:%s" % (host, port)
        if unit_id is not None and int(unit_id) != 1:
            text += " · 站 %s" % unit_id
        return text

    def _pump_addr(self):
        """泵的"地址"= 它挂在哪个数字量模块的哪一路 DO。

        泵没有自己的通讯口（接触器线圈接在模块的 DO 上），所以不能报
        "站号"——那会让人去总线上找一个不存在的从站。报模块地址 + DO 口，
        现场照着万用表量那一路就对了。
        """
        bank = self.pump_bank
        if bank is None or not bank.items:
            return ""
        item = bank.items[0]
        if item.module is None or item.do is None:
            return ""
        mods = self.cfg.digital_modules
        if 0 <= int(item.module) < len(mods):
            m = mods[int(item.module)]
            return "%s · DO%s" % (self._addr_tcp(m.host, m.port), item.do)
        return "DO%s" % item.do

    def _manual_running(self):
        """手动模式的「在动」判定（按钮组启动灯的口径；2026-11-15 用户口径）。

        [口径] 现场还有设备在动 ⇒ 启动灯亮：
            · 搅拌泵（变频器）在转
            · 进水泵在开
            · 任意一路电动球阀在开位（或正在开到位途中）

        [为什么放在 app 层] 按钮组只管"采 DI / 写 DO"，不认识泵与阀；
        判定要跨设备层（阀反馈在 valves、泵在 pumps、VFD 在 vfd），故由
        app 层汇总后经 `set_manual_running()` 推进给按钮组。

        [口径与 C 端同款] C 侧 HsfServerAutoMode.c 的 amManualRunning()
        逐条同口径（VFD 遥测有效 ∧（运行中 ∨ 输出频率 > 0）；泵命令态；
        阀 = 开到位反馈 ∨（开命令 ∧ 未到关位））。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return False
        try:
            return bool(ctl.ctx.manual_running())
        except Exception:                               # noqa: BLE001
            return False

    def _button_addr(self):
        """按钮组的"地址"= 挂在哪个模块、占哪些 DI/DO。"""
        st = getattr(self, "button_station", None)
        if st is None:
            return ""
        try:
            mod = int(st.items.get(buttons.KEY_MODULE) or 0)
        except (TypeError, ValueError):
            mod = 0
        mods = self.cfg.digital_modules
        span = st.health_text()
        if 0 <= mod < len(mods):
            m = mods[mod]
            return "%s · %s" % (self._addr_tcp(m.host, m.port), span)
        return span

    def _button_health(self, now_ms):
        """按钮组的健康条目。

        [判据] 只看"按钮组作为一个部件是否在工作"：数字量模块在线即可用。
        急停**无信号不算故障** —— 那是操作员按下急停的正常状态（或现场
        本来就没接急停线），把它报成故障会天天误报。真正需要人注意的
        信息写在 text 里（"急停无信号，禁止启动"），让人一眼看懂现状。
        """
        st = getattr(self, "button_station", None)
        mod = 0
        try:
            mod = int(st.items.get(buttons.KEY_MODULE) or 0)
        except (TypeError, ValueError):
            pass
        dev = self.digital[mod] if 0 <= mod < len(self.digital) else None
        online = bool(dev is not None and getattr(dev, "online", False))

        if not online:
            return HealthEntry("按钮组", proto.KIND_DIGITAL_IO, 0,
                               proto.HEALTH_OFFLINE, 0, False, 0,
                               "数字量模块离线，按键与灯均不可用",
                               self._button_addr())

        # 模块在线：按状态给一句人话
        snap = st.snapshot()
        state = snap.get("state")
        lamps = snap.get("lamps") or {}
        if state == buttons.ButtonState.ESTOP:
            text = "急停无信号（按下或断线）：禁止启动"
            health = proto.HEALTH_FAULT
        elif lamps.get("start"):
            # [按灯判 2026-11-15] 与 C 侧同口径（HsfServerApp.c：按启动灯
            # 的回读给"运行中/停止"）。手动模式下启动灯 = 现场还有设备在动
            # ⇒ 这里显示"运行中"是对的；按 state（自动运行位）判会在手动
            # 模式永远显示"停止"，与灯、与现场实际都不符。
            text = "运行中（启动灯亮、停止灯灭）"
            health = proto.HEALTH_OK
        else:
            text = "停止（停止灯亮、启动灯灭）"
            health = proto.HEALTH_OK
        return HealthEntry("按钮组", proto.KIND_DIGITAL_IO, 0,
                           health, 0, True, 0, text, self._button_addr())

    def _valve_addr(self):
        """阀组的"地址"= 挂在哪个模块、占哪几个 DO。"""
        bank = self.valve_bank
        if bank is None or not bank.items:
            return ""
        mods = self.cfg.digital_modules
        first = bank.items[0]
        last = bank.items[-1]
        if first.module is None or first.do is None:
            return ""
        # [区间写成 DO0~DO5] 用 ".." 现场有人读成"两个点"，用 ~ 一眼是
        # "从这口到这口"，数口时不会数错一头。
        do_span = "DO%s~DO%s" % (first.do, last.do)             if last.do is not None else "DO%s" % first.do
        if 0 <= int(first.module) < len(mods):
            m = mods[int(first.module)]
            return "%s · %s" % (self._addr_tcp(m.host, m.port), do_span)
        return do_span

    def collect_health(self, now_ms):
        """采集全量设备健康（只读缓存/标志，**不发起 IO**）。"""
        entries = []
        # ---- 变频器（现场驱动搅拌泵，**设备名按工艺用途叫"搅拌泵"**）----
        #
        # [名字 vs 类别是两件事] 设备名给操作员看"这是哪台设备在不在线"，
        # 按现场叫法写"搅拌泵"；类别（kind=KIND_VFD）表达"它是什么东西"，
        # 客户端"类型"列据此显示"变频器"。两者不能混：
        #   · 名字改成搅拌泵 → 操作员一眼找到要找的那台
        #   · 类别保持变频器 → 维修时知道该去查调速器，不是去拆泵体
        if self.cfg.enable_vfd:
            if self.vfd is None:
                entries.append(HealthEntry("搅拌泵", proto.KIND_VFD, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配",
                                           self._addr_rtu(self.cfg.vfd_unit_id)))
            else:
                health, err, online, text = self.vfd.health(
                    now_ms, self.VFD_STALE_MS)
                entries.append(HealthEntry("搅拌泵", proto.KIND_VFD, 0, health,
                                           err, online,
                                           self.vfd.run_state if online else 0,
                                           text,
                                           self._addr_rtu(self.cfg.vfd_unit_id)))
        # ---- 温湿度 ----
        if self.cfg.enable_temphum:
            if self.temphum is None:
                entries.append(HealthEntry("温湿度", proto.KIND_TEMPHUM, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配",
                                           self._addr_rtu(self.cfg.temphum_unit_id)))
            else:
                health, err, online, text = self.temphum.health(
                    now_ms, self.cfg.temphum_stale_ms)
                entries.append(HealthEntry("温湿度", proto.KIND_TEMPHUM, 0,
                                           health, err, online, 0, text,
                                           self._addr_rtu(self.cfg.temphum_unit_id)))
        # ---- 流量计 ----
        if self.cfg.enable_flowmeter:
            if self.flowmeter is None:
                entries.append(HealthEntry("电磁流量计", proto.KIND_FLOWMETER,
                                           0, proto.HEALTH_OFFLINE, 0, False,
                                           0, "未装配",
                                           self._addr_rtu(
                                               self.cfg.flowmeter_unit_id, "b")))
            else:
                health, err, online, text = self.flowmeter.health(
                    now_ms, self.cfg.flowmeter_stale_ms)
                entries.append(HealthEntry("电磁流量计", proto.KIND_FLOWMETER,
                                           0, health, err, online, 0, text,
                                           self._addr_rtu(
                                               self.cfg.flowmeter_unit_id, "b")))
        # ---- 浊度仪（逐台）----
        for idx, tm in enumerate(self.turbidity):
            # [类别必须是"水质"] 曾经误用 KIND_TEMPHUM，首页"类型"列把两台
            # 浊度仪显示成"温湿度"，现场会以为是电箱里的温湿度变送器。
            unit = (self.cfg.turbidity_unit_ids[idx]
                    if idx < len(self.cfg.turbidity_unit_ids) else "?")
            if not turbidity_enabled(self.cfg, idx):
                # [逐台开关 2026-11-10] 停采的台明确报"已停用"，别伪装成
                # 掉线（会误导现场去查接线）。
                entries.append(HealthEntry(("进水浊度仪" if (idx + 1) == 1 else "出水浊度仪"),
                                           proto.KIND_WATER, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "已停用",
                                           self._addr_rtu(unit, "b")))
                continue
            health, err, online, text = tm.health(
                now_ms, self.cfg.turbidity_stale_ms)
            entries.append(HealthEntry(("进水浊度仪" if (idx + 1) == 1 else "出水浊度仪"),
                                       proto.KIND_WATER, 0,
                                       health, err, online, 0, text,
                                       self._addr_rtu(unit, "b")))
        # ---- 进水泵 ----
        if self.pump_bank is not None:
            health, online, text = self.pump_bank.health_summary(
                now_ms, self.cfg.pump_stale_ms)
            # 泵没有独立通讯口，是数字量模块上的一路 DO ⇒ 报模块地址 + 口
            entries.append(HealthEntry("进水泵", proto.KIND_DIGITAL_IO, 0,
                                       health, 0, online, 0, text,
                                       self._pump_addr()))
        # ---- 按钮组（现场操作柱）----
        # [必须 getattr] 单测/工具会用 object.__new__(App) 造"半初始化"
        # 实例来测 collect_health，那时 __init__ 没跑、属性不存在。
        if getattr(self, "button_station", None) is not None:
            entries.append(self._button_health(now_ms))
        # ---- 模拟量 ----
        if self.cfg.enable_analog:
            if self.analog is None:
                entries.append(HealthEntry("模拟量模块", proto.KIND_ANALOG_IO, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配",
                                           self._addr_tcp(self.cfg.analog.host,
                                                          self.cfg.analog.port)))
            else:
                health, err, online, text = self.analog.health(
                    now_ms, self.AI_STALE_MS)
                entries.append(HealthEntry("模拟量模块", proto.KIND_ANALOG_IO, 0,
                                           health, err, online, 0, text,
                                           self._addr_tcp(self.cfg.analog.host,
                                                          self.cfg.analog.port)))
        # ---- 数字量（逐模块）----
        for i, dev in enumerate(self.digital):
            health, err, online, text = dev.health(now_ms,
                                                   self.cfg.digital_stale_ms)
            mods = self.cfg.digital_modules
            mcfg = mods[i] if i < len(mods) else None
            entries.append(HealthEntry(
                dev.name, proto.KIND_DIGITAL_IO, i,
                health, err, online, 0, text,
                self._addr_tcp(mcfg.host, mcfg.port) if mcfg else ""))
        # ---- 电磁阀组（一个条目，含几路故障的摘要）----
        if self.valve_bank is not None:
            health, online, text = self.valve_bank.health_summary(now_ms)
            entries.append(HealthEntry("电动球阀组", proto.KIND_VALVE, 0,
                                       health, 0, online, 0, text,
                                       self._valve_addr()))
        # ---- 网络相机（2026-11-20 进健康：动态 IP/断线也是运维要盯的
        #      事，与 485 设备同待遇）——只读采集器缓存，不发 IO ----
        # [始终显示 2026-11-20 用户口径「相机的连接情况加进入也没做」]
        # 未启用也出条目（标「已停用」，与浊度仪逐台停用同口径）——
        # 否则现场看清单找不到相机，分不清"没这功能"还是"没配"。
        st = getattr(self, "_camera_status", None) or {}
        online = bool(st.get("online"))
        if not getattr(self.cfg, "camera_enabled", False):
            entries.append(HealthEntry("网络相机", proto.KIND_CAMERA, 0,
                                       proto.HEALTH_OFFLINE, 0, False, 0,
                                       "已停用", ""))
        else:
            if online:
                health = proto.HEALTH_OK
                frames = int(st.get("frames") or 0)
                unit = "NAL" if st.get("transport") == "stream" else "帧"
                text = "在线 · 已收 %d %s" % (frames, unit)
            else:
                health = proto.HEALTH_OFFLINE
                text = str(st.get("error") or "离线")
            cam_ip = str(getattr(self.cfg, "camera_ip", "") or "")
            cam_addr = ""
            if getattr(self.cfg, "camera_resolve_mode", "") \
                    == proto.CAMERA_RESOLVE_MAC:
                mac = str(getattr(self.cfg, "camera_mac", "") or "")
                resolved = str(st.get("resolvedIp")
                               or getattr(self.camera, "_resolved_ip", "")
                               or "")
                cam_addr = ("MAC %s → %s" % (mac, resolved)
                            if resolved else "MAC %s（未解析）" % mac)
            elif cam_ip:
                cam_addr = self._addr_tcp(
                    cam_ip, int(getattr(self.cfg, "camera_port", 554)))
            entries.append(HealthEntry("网络相机", proto.KIND_CAMERA, 0,
                                       health, 0, online, 0, text,
                                       cam_addr))
        return entries

    async def _health_tick(self, now_ms):
        """健康巡检：采集 → 与上次比对 → **变化才推**（事件驱动）。

        首拍只建基线不推：客户端订阅/首连时会补发一份当前快照，
        此时推会造成「连上就收到两次同样的包」。
        """
        entries = self.collect_health(now_ms)
        keys = [e.key() for e in entries]
        if not self._health_primed:
            self._health_last = keys
            self._health_primed = True
            # [边沿基线 2026-11-17] 首拍只建基线：此刻就异常算不算"新发生
            # 的异常"？网页端的口径是**算**（首见即异常就记一行）；但服务端
            # 这份记录是给两端共用的历史，首拍就把"开机前就异常的老状态"记
            # 成事件会误导 ⇒ 首拍只留基线，不记行（与客户端 track_health_
            # alarms 的首拍口径一致：prev is None ⇒ 不记）。
            self._health_prev = {e.name: e.health for e in entries}
            return
        if keys == self._health_last:
            return
        self._health_seq += 1
        self._health_last = keys
        # [健康边沿入库 2026-11-17] 边沿（正常→异常 / 异常→正常）写 alarms 表
        # ⇒ 客户端与网页端按 0x011A 取**同一份**历史（客户端重启不丢）。
        self._record_health_edges(entries, now_ms)
        payload = self.encode_health()
        await self.server.publish(proto.TOPIC_DEVICE_HEALTH, payload)
        self._log("设备健康变化（seq=%d，%d 台）" % ( (self._health_seq, len(entries))))

    def _record_health_edges(self, entries, now_ms):
        """设备健康**边沿**入 alarms 表（异常一条、恢复一条）。

        [为什么要服务端记] 网页端的报警表是**网页进程自己**算的边沿（hub
        .feed_health）⇒ 记录只在该进程活着期间存在；桌面客户端也是自己算
        ⇒ **客户端一重启历史全丢**（用户实报「web 端报警有数据，客户端报警
        视图没数据」）。⇒ 边沿由服务端记进 alarms 表，两端都按 0x011A 取
        同一份 ⇒ 单一数据源、客户端重启不丢。

        [边沿口径与网页端一致]
          · 正常 → 异常（health >= HEALTH_STALE）：记一条，状态 = 异常文案；
          · 异常 → 正常：记一条「已恢复正常」；
          · 异常 → 另一种异常（过期→离线）：**不记**（同一段异常）。
        """
        prev = getattr(self, "_health_prev", None) or {}
        for e in entries:
            old = prev.get(e.name)
            cur = int(e.health)
            if old is None or int(old) == cur:
                continue
            if cur >= proto.HEALTH_STALE:
                # [同一段异常不重复记] 与网页端 `AlarmLog.feed_health` 同款：
                # 「过期 → 离线」是**同一段**异常（活跃键 = 设备名），只记
                # 进入异常那一条；否则一次掉线会刷出两条行。
                if int(old) >= proto.HEALTH_STALE:
                    continue
                # [状态列只放健康档 2026-11-17] 与网页端 `.table` 的「状态」
                # 列同口径（数据过期/离线/故障）；设备名在 source 列 ⇒ 两端
                # 渲染出来的行**逐字一致**（客户端的本地边沿行也用同一套
                # 文案 ⇒ 去重时能对上）。
                text = proto.HEALTH_TEXT.get(cur, "数据异常")
                kind = proto.ALARM_KIND_HEALTH
            elif int(old) >= proto.HEALTH_STALE:
                text = proto.ALARM_TEXT_RECOVERED
                kind = proto.ALARM_KIND_HEALTH
            else:
                continue
            try:
                # [ts 用墙钟] alarms 表的 ts 必须是**墙钟**（与既有自动/流量计
                # 告警入库同口径）⇒ 不传 now_ms，让 insert_alarm 自己取。
                self.store.insert_alarm(kind, e.name, text, dev=e.kind)
            except Exception as exc:                    # noqa: BLE001
                self._log_with_store("健康边沿入库失败：%s: %s"
                                     % (type(exc).__name__, exc),
                                     proto.LOG_CAT_ALARM)
        self._health_prev = {e.name: int(e.health) for e in entries}

    # ==================================================================
    #                       订阅 / 首连 补发
    # ==================================================================
    async def publish_topic_now(self, session, topic):
        """订阅即补发一拍（免等周期推送，避免页面刚打开显示 "--"）。

        返回 (payload, dataType)；无可用数据时返回 (None, None)。
        **返回值必须恒为二元组**——调用方直接解包，三元组会抛 TypeError
        （曾因 AI 分支的运算符优先级 bug 把会话打崩）。
        """
        if topic == proto.TOPIC_DEVICE_HEALTH:
            return self.encode_health(), proto.DATATYPE_BSON
        if topic == proto.TOPIC_VFD_STATUS:
            payload = self.encode_vfd(self.vfd)
            return (payload, proto.DATATYPE_BSON) if payload else (None, None)
        if topic == proto.TOPIC_TEMPHUM_STATUS:
            return self.encode_temphum(self.temphum), proto.DATATYPE_BSON
        if topic == proto.TOPIC_FLOWMETER_STATUS:
            if self.flowmeter is None:
                return None, None
            return (self.encode_flowmeter(self.flowmeter),
                    proto.DATATYPE_BSON)
        if topic == proto.TOPIC_TURBIDITY_STATUS:
            if not self.turbidity:
                return None, None
            return self.encode_turbidity(), proto.DATATYPE_BSON
        if topic == proto.TOPIC_PUMP_STATUS:
            if self.pump_bank is None:
                return None, None
            return self.encode_pumps(), proto.DATATYPE_BSON
        if topic == proto.TOPIC_AI_STATUS:
            if self.analog is None:
                return None, None
            # [订阅补发带名字] 与 C 端一致（hsfAppPublishTopicNow 的 AI 分支
            # 注释「订阅补发：给全量」）；周期推送才走 full=False。
            return (self.encode_analog(self.analog, full=True),
                    proto.DATATYPE_BSON)
        if topic == proto.TOPIC_DIGITAL_STATUS:
            if not self.digital:
                return None, None
            return self.encode_digital(self.digital[0], 0), proto.DATATYPE_BSON
        if topic == proto.TOPIC_VALVE_STATUS:
            if self.valve_bank is None:
                return None, None
            return self.encode_valves(), proto.DATATYPE_BSON
        if topic == proto.TOPIC_CAMERA_FRAME:
            # [相机画面订阅补发] 订阅即把**当前这一帧**的分片补发过去，
            # 免得客户端打开相机页要等下一帧（1fps 时就是等 1 秒）。
            # [返回列表而不是二元组] 一帧有多片 ⇒ 这里返回 [(payload, type),
            # ...]（xlink.publish_topic_now 支持列表，逐片发）。
            # [ES 直传 2026-11-19] 补发 = **整个最近 GOP**（参数集+IDR…）：
            # 新订阅者的解码器需要参数集+IDR 自举，补一帧 VCL 没有意义。
            # （发布协程那边的 0→正 沿也会发 GOP——两处都发是幂等的吗？
            # 不幂等（重复 GOP 会冲乱解码器）⇒ 沿判据在 `_camera_subs_last`
            # 由本补发顺手置位，发布协程当拍就不再补。）
            if self._camera_es_mode():
                if self.camera is None:
                    return None, None
                gop = self.camera.gop_nals()
                if not gop:
                    return None, None
                self._camera_subs_last = 1
                h265 = self.camera.detected_codec() == proto.CAMERA_CODEC_H265
                flags = proto.CAM_FRAME_FLAG_ES
                if h265:
                    flags |= proto.CAM_FRAME_FLAG_H265
                out = []
                for nal in gop:
                    self._camera_published_seq = \
                        (self._camera_published_seq + 1) & 0xFFFFFFFF
                    nf = _NalFrame(nal, self._camera_published_seq)
                    out.extend((chunk, proto.DATATYPE_RAW)
                               for chunk in self._camera_fragments(
                                   nf, extra_flags=flags))
                return out
            frame = self.camera.latest_frame() if self.camera else None
            if frame is None:
                return None, None
            chunks = self._camera_fragments(frame)
            if not chunks:
                return None, None
            return [(chunk, proto.DATATYPE_RAW) for chunk in chunks]
        return None, None

    async def on_session_added(self, session):
        """客户端首连：立即补发一份设备健康快照（免等下一拍）。

        注：此时尚未订阅本主题，故走单会话定向发送而非 broadcast——
        保证「首连即拿到当前状态」。
        """
        try:
            await session.send_publish(proto.TOPIC_DEVICE_HEALTH,
                                       self.encode_health())
        except Exception as exc:                        # noqa: BLE001
            self._log("首连补发健康快照失败：%s" % exc)

    async def on_session_ready(self, session):
        """认证通过后：补发健康快照（首连时可能还在认证中，发不出去）。

        [数字量逐模块全量帧补发 2026-11-16 审计] 通道名只随「名字脏帧」
        （启动后第一拍）发过一次——那会儿通常还没有任何订阅者 ⇒ 之后再
        连上/订阅的客户端**永远收不到模块 1+ 的通道名**（周期帧不带名字、
        订阅补发只回模块 0 的全量帧）⇒ 数字量页模块 1 显示裸 CH 号。
        ⇒ 认证一通过就把**所有模块**的全量帧直接发给这条会话（定向发送，
        不经订阅闸）。
        [形状必须是全量帧 2026-11-16] 第一版发的是"只含名字"的脏帧 ⇒
        双端对照测试抓第一帧当形状样本 ⇒ C/PY 键集对不上（PY 缺
        di/do/valid/staleMs）。全量帧（encode_digital 缺省 full=True）
        名字与状态都有，与 C 端订阅补发同形。
        """
        await self.on_session_added(session)
        for i, dev in enumerate(getattr(self, "digital", None) or []):
            if not getattr(dev, "publish", True):
                continue
            try:
                await session.send_publish(proto.TOPIC_DIGITAL_STATUS,
                                           self.encode_digital(dev, i))
            except Exception as exc:                    # noqa: BLE001
                self._log("数字量全量帧补发失败（模块%d）：%s: %s"
                          % (i, type(exc).__name__, exc))
                break

    # ==================================================================
    #                        登录策略（三键判定）
    # ==================================================================
    def policy_blocks_login(self, user_name, session):
        """登录策略闸（login 节三键；2026-11-08 起可由远端设置调整）。

        闸序对齐 C 侧 XLoginPolicy_admitSession：**总数上限 → 匿名 →
        同账并发**，先到先拒，命中即拒（返回 True，链路层按 AUTHSTATUS
        BAD_USER 回认证失败，见 xlink._on_auth_request）。

        - loginMaxSessions     会话总数上限（0=不限）。口径与 C 一致：按
          xlink.session_count()（**当前连接总数**，含本会话与尚未认证的
          连接——连接一建立就入表，见 xlink.Server._handle_client）；
        - loginAllowAnonymous  false 时拒绝**未宣告认证**的登录请求
          （session.auth_declared()：hello 能力位未宣告且无在途挑战/未
          完成认证 = 想匿名进来；近似口径的取舍见 xlink.Session 注释）；
        - loginAllowConcurrent True 同账可多端并发（压测/多端围观）；
          否则沿用「root 单会话」保护（防并行修改）；admin 之外账户不限
          ——多人围观无风险。

        极小竞态窗（两个 root 同时处于认证中）不设防：认证在毫秒级完成。
        """
        if self.server is None:
            return False
        # ---- 闸1：会话总数上限（0=不限）----
        max_sessions = int(getattr(self.cfg, "login_max_sessions", 0) or 0)
        if max_sessions > 0 and self.server.session_count() >= max_sessions:
            # 三条策略拒绝都是 auth 类（与 C 侧"登录拒绝留痕"同分类）：
            # 账户审计要在日志页 category="auth" 下看到完整时间线。
            self._log_with_store("登录策略：拒绝 %s（会话数已达上限 %d，当前 %d）"
                                 % (user_name, max_sessions,
                                    self.server.session_count()),
                                 proto.LOG_CAT_AUTH)
            return True
        # ---- 闸2：匿名未开放 → 拒绝未宣告认证的会话 ----
        if not getattr(self.cfg, "login_allow_anonymous", True):
            declared = True
            try:
                declared = bool(session.auth_declared())
            except AttributeError:
                declared = True          # 无该接口的会话对象：不拦（保守放行）
            if not declared:
                self._log_with_store(
                    "登录策略：拒绝 %s（匿名登录未开放：会话未宣告认证）"
                    % user_name, proto.LOG_CAT_AUTH)
                return True
        # ---- 闸3：同账并发（现状维持：root 单会话）----
        if getattr(self.cfg, "login_allow_concurrent", False):
            return False
        if user_name != proto.DEFAULT_ROOT_NAME:
            return False
        for other in self.server.sessions:
            if other is session or not other.is_ready():
                continue
            if other.auth_user == proto.DEFAULT_ROOT_NAME:
                self._log_with_store(
                    "登录策略：拒绝 %s（该账户已登录，不允许同时在线）"
                    % user_name, proto.LOG_CAT_AUTH)
                return True
        return False

    # ==================================================================
    #                          命令路由
    # ==================================================================
    async def handle_request(self, session, request_id, msg_code, data_type,
                             payload):
        """Request 帧统一入口。每个分支负责自行回包（含失败）。"""
        handler = {
            proto.CMD_VFD_CONTROL: self._h_vfd_control,
            proto.CMD_VFD_SNAPSHOT: self._h_vfd_snapshot,
            proto.CMD_AI_SNAPSHOT: self._h_ai_snapshot,
            proto.CMD_TEMPHUM_SNAPSHOT: self._h_temphum_snapshot,
            proto.CMD_FLOWMETER_SNAPSHOT: self._h_flowmeter_snapshot,
            proto.CMD_RUN_MODE: self._h_run_mode,
            proto.CMD_DEVICE_HEALTH: self._h_device_health,
            proto.CMD_DIGITAL_SNAPSHOT: self._h_digital_snapshot,
            proto.CMD_DIGITAL_CONTROL: self._h_digital_control,
            proto.CMD_VALVE_SNAPSHOT: self._h_valve_snapshot,
            proto.CMD_VALVE_CONTROL: self._h_valve_control,
            proto.CMD_TURBIDITY_SNAPSHOT: self._h_turbidity_snapshot,
            proto.CMD_PUMP_SNAPSHOT: self._h_pump_snapshot,
            proto.CMD_PUMP_CONTROL: self._h_pump_control,
            proto.CMD_AUTO_CONTROL: self._h_auto_control,
            proto.CMD_CONFIG_GET: self._h_config_get,
            proto.CMD_CONFIG_SET: self._h_config_set,
            proto.CMD_HISTORY_PAGE: self._h_history_page,
            proto.CMD_LOG_PAGE: self._h_log_page,
            proto.CMD_ALARM_PAGE: self._h_alarm_page,
            proto.CMD_VFD_PARAMS: self._h_vfd_params,
            proto.CMD_CAMERA_FRAME: self._h_camera_frame,
        }.get(msg_code)
        if handler is None:
            # 未知命令必须回否定应答：不发则客户端空等到超时（观感「服务端死了」）。
            await self._reply(session, request_id, msg_code, False, "未知命令")
            return
        try:
            await handler(session, request_id, msg_code, payload)
        except Exception as exc:                        # noqa: BLE001
            self._log("命令 0x%04X 处理异常：%s: %s"
                      % (msg_code, type(exc).__name__, exc))
            await self._reply(session, request_id, msg_code, False,
                              "服务端处理异常：%s" % type(exc).__name__)

    async def _reply(self, session, request_id, msg_code, ok, err=None,
                     extra=None):
        doc = {proto.KEY_OK: bool(ok)}
        if err:
            doc[proto.KEY_ERR] = err
        if extra:
            doc.update(extra)
        await session.send_response(request_id, msg_code, bson.encode(doc))

    async def _reply_checked(self, session, request_id, msg_code, extra,
                             oversize_err):
        """带「载荷超单帧上限」兜底的应答（分页类命令用）。

        [为什么需要 2026-11-17] 客户端 `decode_header` 对超过
        `XLINK_MAX_PAYLOAD`（16KB）的帧返回 None ⇒ `_pump` 判"帧头非法"
        ⇒ **整个会话断链重连**（表现：一翻历史页就掉线，反复重连）。
        当前 19 列 × 50 行实测 11.9KB（余量 27%），但文本列（press_note）
        变长或将来加列就会把余量吃完。⇒ 组帧前先量一次编码后大小，
        超限就回一条**短失败包**让界面提示，绝不发那个会踢掉会话的巨帧。
        """
        doc = {proto.KEY_OK: True}
        if extra:
            doc.update(extra)
        try:
            payload = bson.encode(doc)
        except (TypeError, ValueError) as exc:
            await self._reply(session, request_id, msg_code, False,
                              "应答编码失败：%s" % exc)
            return
        if len(payload) > proto.XLINK_MAX_PAYLOAD - 512:
            self._log("分页应答过大（%d 字节，超单帧上限），已改回失败包：%s"
                      % (len(payload), oversize_err))
            await self._reply(session, request_id, msg_code, False,
                              oversize_err)
            return
        await session.send_response(request_id, msg_code, payload)

    # ---- 变频器控制 ----
    # ---- 报警锁存闸（2026-11-15 用户定案）----

    def _fault_gate_text(self):
        """报警锁存闸的拒绝文案（无锁存 ⇒ None = 放行）。

        [用户口径]「进了报警表的报警要锁存，并自动停止设备，必须手动复位
         才可以启动设备」——锁存未解时，**手动模式**的远程控制（启泵/开阀/
         启变频器）也要被拒：保护动作已经把设备停了，人没按复位就再把它
         开起来，保护就白做了。
        [为什么放行停止类] 人总得能把设备停下 ⇒ 停止/自由停车/故障复位
         一律放行（闸门按调用点判定：只有"让设备动起来"的命令才查本闸）。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return None
        try:
            if not ctl.fault_latched:
                return None
        except Exception:                               # noqa: BLE001
            return None
        return "有报警未复位：禁止启动设备（请先按复位）"

    def _fault_latched(self):
        """当前是否有未复位的报警锁存（保护动作的"别重复报"判据）。"""
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return False
        try:
            return bool(ctl.fault_latched)
        except Exception:                               # noqa: BLE001
            return False

    def _device_do_channels(self, module_index=0):
        """某台数字量模块上属于**设备**的 DO 通道（电动球阀 + 进水泵）。

        [为什么要它] 数字量页能直接写 DO ⇒ 那些位也是"启动设备"的途径；
        报警锁存时必须一起拦住（否则闸门形同虚设）。
        [只含设备位] 按钮灯（DO7..9）等非设备位不受影响（现场仍可点灯/检修）。
        """
        out = []
        for bank in (getattr(self, "valve_bank", None),
                     getattr(self, "pump_bank", None)):
            if bank is None:
                continue
            for item in getattr(bank, "items", None) or []:
                try:
                    if int(getattr(item, "module", -1)) != int(module_index):
                        continue
                    ch = int(getattr(item, "do", -1))
                    if ch >= 0 and ch not in out:
                        out.append(ch)
                except Exception:                       # noqa: BLE001
                    continue
        return out

    async def _watch_vfd_fault(self, now_ms):
        """变频器故障**边沿** ⇒ 报警（进报警历史表）+ 锁存 + 自动停设备。

        [为什么放 app 层] 故障来自设备层（遥测解码），但"要不要停机/锁存"
         是业务决策（自动控制层）⇒ app 把边沿转成控制层的 raise_fault。
        [为什么只认边沿] 故障状态会一直挂着（设备没复位就一直报）⇒ 每拍
         报一次会把报警表刷爆；只报"新发生的故障"这一条 ✓。
        [为什么启动就带着故障也要报] 那是**真故障**（设备带着故障上线），
         必须让人看见 ⇒ 直接报 + 锁存（此刻设备还没跑，锁存只是把"不许
         再启动"这件事提前说清）。
        """
        vfd = getattr(self, "vfd", None)
        if vfd is None:
            return
        try:
            faulted = bool(getattr(vfd, "faulted", False))
        except Exception:                               # noqa: BLE001
            return
        if faulted == self._vfd_faulted:
            return
        self._vfd_faulted = faulted
        if not faulted:
            # 故障消失**不解锁**（必须人工复位）——只留个日志。
            self._log_with_store("变频器故障已消失（报警仍锁存，需手动复位）",
                                 proto.LOG_CAT_ALARM)
            return
        code = int(getattr(vfd, "fault_code", 0) or 0)
        try:
            text = vfd.fault_text(code) or "变频器故障"
        except Exception:                               # noqa: BLE001
            text = "变频器故障"
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        full = "变频器故障：%s（已停止设备，需复位）" % text
        try:
            # [自己发这条告警 2026-11-15] 走 app 的事件通道（带 dev 键 ⇒
            # 报警表的"类型"列显示"变频器"而不是"自动运行"）；控制器那边
            # 只负责**锁存 + 停设备**（enqueue_alarm=False ⇒ 别再排一条）。
            await self._publish_alarm_event(proto.AUTO_ALARM_DEVICE_FAULT,
                                            full, proto.KIND_VFD, now_ms)
            ctl.raise_fault(proto.AUTO_ALARM_DEVICE_FAULT, full, now_ms,
                            stop_devices=True, enqueue_alarm=False)
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)

    async def _h_vfd_control(self, session, request_id, msg_code, payload):
        # 1) 自动模式闸（最前置：连角色都不看）
        if self.is_auto():
            # 类别 = ctrl（拒绝留痕：与 C 侧 hsfAppControlGate 的
            # "控制命令 0x%04X 被拒（自动运行中…）"同文案同分类）。
            self._log_with_store(
                "控制命令 0x%04X 被拒（自动运行中：远程控制已被禁用）"
                % msg_code, proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        # 1b) **急停闸**（2026-11-06）：急停无信号时禁止一切"让设备动起来"的
        # 命令。放在角色门禁之前 —— 这是安全件，不依赖谁在操作。
        #
        # [为什么是"先于一切"] 急停按下（或急停回路断线）时，任何启动/点动/
        # 调速都必须被拒。若只靠界面置灰，绕过界面的客户端照样能启动 ——
        # 安全闸必须在服务端。
        #
        # [只拦"动作类"命令] 停止、自由停车、故障复位这些**让设备停下/清故障**
        # 的命令必须放行（急停时更要允许停），否则会把人卡住。
        # 点动释放同理放行（它本来就是收尾动作）。
        #
        # [⚠ 2026-11-06 订正：没装操作柱的现场不能拦] 初版直接把
        # `estop_ok()` 的 None 当成"禁止"，于是**未装配按钮组**的现场
        # （`enable_buttons=false`，或只有 TCP 设备没有数字量模块）所有启动/
        # 调速全被拒，报"急停状态未知"——设备明明好好的却动不了。
        # 判据改成：**先看有没有配急停回路**。没配 = 没有这道安全件，
        # 不该被它拦住（此时安全性由其它手段保证：远程控制本身的权限闸、
        # 现场硬接线急停）。配了但读不到（模块离线）= None ⇒ 仍然禁止，
        # 因为"配了却读不到"说明回路可能断了，这正是要拦的情形。
        try:
            _op_for_gate = bson.decode(payload).get(proto.KEY_OP) \
                if payload else None
        except (ValueError, AttributeError):
            _op_for_gate = None
        if (_op_for_gate in _ESTOP_BLOCKED_OPS
                and self.auto_ctx.estop_configured()):
            estop = self.auto_ctx.estop_ok()
            if estop is not True:
                await self._reply(
                    session, request_id, msg_code, False,
                    "急停无信号（按下或断线）：禁止启动/调速"
                    if estop is False
                    else "急停状态未知（数字量模块离线）：禁止启动/调速")
                return
        # 1c) **报警锁存闸**（2026-11-15 用户定案）：有未复位的报警 ⇒
        #     禁止"让设备动起来"的命令（与急停闸同一组 op）。
        #     [为什么手动模式也拦] 用户口径「必须手动复位才可以启动设备」——
        #     保护动作（故障/空管超时）已经把设备停了；人没按复位之前再把
        #     它开起来，保护就白做了。
        #     [为什么放在角色门禁之前] 与急停闸同理：这是安全闸，不该依赖
        #     谁在操作（低权限账号也不该能绕过）。
        if _op_for_gate in _ESTOP_BLOCKED_OPS:
            why = self._fault_gate_text()
            if why:
                self._log_with_store(
                    "控制命令 0x%04X 被拒（%s）" % (msg_code, why),
                    proto.LOG_CAT_CTRL)
                await self._reply(session, request_id, msg_code, False, why)
                return
        # 2) 角色门禁（设备控制：操作者/管理员）
        if not permissions.can(session, proto.PERM_DEVICE_CONTROL):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(
                                  proto.PERM_DEVICE_CONTROL))
            return
        # 3) 载荷解析
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷解析失败：%s" % exc)
            return
        op = doc.get(proto.KEY_OP)
        if not isinstance(op, int) or isinstance(op, bool) \
                or op not in proto.VALID_CONTROL_OPS:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷解析失败（op 非法）")
            return
        # 4) 点动释放（自定义 op，语义由 op 区分，不新增命令号）
        if op == proto.OP_JOG_RELEASE:
            await self._jog_release()
            await self._reply(session, request_id, msg_code, True)
            return
        # 5) 设备就绪
        if self.vfd is None:
            await self._reply(session, request_id, msg_code, False, "变频器未就绪")
            return
        # 5b) **停机时禁止下发给定**（只允许启停/点动/复位）
        #
        # [为什么禁止而不是"记下来等启动时用"] 现场口径：给定是**运行参数**，
        # 不是可预置的配置。允许停机下发会带来两个真实问题：
        #   ① 界面出现"停机 + 输出频率 25Hz"这种自相矛盾的状态
        #      （虚拟设备原来就真的这样：写 0x1000 后坡道开始爬，输出
        #       频率从 0 涨到 25Hz，转速显示 720rpm，而运行状态还是"停机"）
        #   ② 操作员以为"设好了但没生效"，再点一次启动才发现按的是旧给定
        # 禁止后语义唯一：**先启动，再调速**。
        #
        # 判据用设备**实际遥测**（running），不用客户端界面状态——客户端
        # 状态可能滞后一拍，而设备缓存是每次 poll 刷新的。
        if op in _VFD_SETPOINT_OPS:
            if not self.vfd.is_running():
                await self._reply(
                    session, request_id, msg_code, False,
                    "变频器处于停机状态，不能下发给定（请先启动后再调速）")
                return
        # 5c) **运行中禁止点动**（2026-11-06 口径）：电机在正常启停语义下
        # 运行时，点动会瞬间换向/抢命令字，物理上不成立 ⇒ 拒绝。例外：
        # **点动保持中**（watch.active）放行——点动手持每 200ms 重发心跳，
        # 此时设备必然"运行中"，把心跳拦掉会被 600ms 看门狗误停机。
        # 与 C 服务端 hsfAppHandleVfdControl 同口径同文案。
        if op == proto.OP_JOG and self.vfd.is_running():
            watch = self._jog_slot()
            if not watch.active:
                await self._reply(
                    session, request_id, msg_code, False,
                    "变频器运行中，禁止点动（请先停机再点动）")
                return
        reverse = bool(doc.get(proto.KEY_REV, False))
        hz = float(doc.get(proto.KEY_HZ, 0.0) or 0.0)
        rpm = float(doc.get(proto.KEY_RPM, 0.0) or 0.0)
        value = float(doc.get(proto.KEY_VAL, 0.0) or 0.0)
        index = int(doc.get(proto.KEY_IDX, 0) or 0)
        try:
            ok, err = await self.vfd.apply_control(op, reverse, hz, rpm,
                                                   value, index)
        except Exception as exc:                        # noqa: BLE001
            # 类别 = ctrl：写入异常也是"控制被拒"的一种。
            self._log_with_store(
                "变频器控制 %s：被拒（写入异常 %s）"
                % (_VFD_OP_NAMES.get(op, "op%d" % op),
                   type(exc).__name__), proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "设备写入失败：%s" % exc)
            return
        # [控制受理/拒绝留痕 = ctrl] 5) 各类闸门通过后的**真实结果**。
        # [为什么跳过 op=JOG] 点动手持每 200ms 一帧心跳，逐帧记会把日志页
        # 冲满；点动的开始/释放已由 _jog_touch/_jog_release 各记一条。
        if op != proto.OP_JOG:
            self._log_with_store(
                "变频器控制 %s：%s%s"
                % (_VFD_OP_NAMES.get(op, "op%d" % op),
                   "已受理" if ok else "被拒",
                   "" if ok else "（%s）" % (err or "设备写入失败")),
                proto.LOG_CAT_CTRL)
        # 6) 点动手持心跳
        if op == proto.OP_JOG and ok:
            self._jog_touch(reverse)
        # [故障复位 = 人工复位 2026-11-15] 人在界面上按了「复位故障」⇒
        # 设备侧故障已清 ⇒ 服务端的**报警锁存**也一并解除（否则设备都好了，
        # 服务端还拦着不让启动，人会以为"复位了也没用"）。
        # [只清报警锁存，不清急停锁存] 急停只能由现场物理复位解除。
        if ok and op == proto.OP_RESET_FAULT:
            ctl = getattr(self, "auto_controller", None)
            if ctl is not None:
                ctl.clear_fault("设备故障复位")
        await self._reply(session, request_id, msg_code, ok, err or None)

    # ---- 快照 ----
    async def _h_vfd_snapshot(self, session, request_id, msg_code, payload):
        if self.vfd is None:
            await self._reply(session, request_id, msg_code, False, "变频器未就绪")
            return
        # [请求即最新] 现读一次；失败不拦（返回上次值 + valid 位如实反映）
        await devices.read_fresh(self.vfd, self._now_ms())
        doc = {proto.KEY_VALID_MASK: self.vfd.valid_mask,
               proto.KEY_DEV_TS: int(getattr(self.vfd, "last_ok_ms", 0) or 0),
               proto.KEY_SIDE: 0}
        for bit, key, kind in proto.VFD_TELEMETRY_FIELDS:
            val = self.vfd.field_value(bit, kind)
            if val is not None:
                doc[key] = val
        # 额定点随快照下发：客户端用它把"转速模式"的输入量程与换算对齐，
        # 不必在客户端再写死一份 1440/50（两份必然漂移）。
        doc["ratedRpm"] = float(self.vfd.rated_rpm)
        doc["ratedHz"] = float(self.vfd.rated_hz)
        # [电机温度 2026-11-20] 与推送同源（读不到不上键）。
        temp = self._motor_temp_c()
        if temp is not None:
            doc["motorTempC"] = float(temp)
        await session.send_response(request_id, msg_code, bson.encode(doc))

    # ---- 变频器参数读写（0x011B，2026-11-18 新增）----

    def _vfd_param_items(self, snap, write_results=None):
        """驱动器快照（regs）→ 应答 params 数组（键面 = proto.PARAM_DEFS）。

        [value 一律来自回读] snap["regs"] 是**刚从驱动器读回**的原始码；
        写操作的调用方会**重新读一次快照**再进本函数——绝不把提交值当
        成功值返回（回读不撒谎）。
        [缺项不给假值] 某地址驱动器没回（理论上一帧连读不会发生）就缺这
        一项，不编 0。

        :param snap: read_params_snapshot() 的返回（regs/runState/faulted）。
        :param write_results: 写操作时传 {addr: (applied, raw_now, err)}，
            为本轮写入过的项带 applied 键（读操作不传）。
        """
        items = []
        for d in proto.PARAM_DEFS:
            raw = snap["regs"].get(d["addr"])
            if raw is None:
                continue
            item = {
                "code": d["code"],
                "label": d["label"],
                "addr": d["addr"],
                "raw": int(raw),
                "value": proto.param_value_of(raw, d["scale"]),
                "unit": d["unit"],
                "writable": bool(d["writable"]),
                "runLocked": bool(d["runLocked"]),
            }
            # [枚举型参数 2026-11-18] 选项随应答下发 ⇒ 客户端/网页端**不必
            # 各写一份**选项表（少一处漂移源）；本地表只当老服务端兜底。
            if d.get("options"):
                item["options"] = [{"value": int(v), "label": str(t)}
                                   for v, t in d["options"]]
                # 回读值对应的中文名（**不在表里就不给** ⇒ 不编）。
                _name = proto.param_option_label(d, int(raw))
                if _name:
                    item["valueText"] = _name
            if write_results is not None and d["addr"] in write_results:
                item[proto.KEY_PARAM_APPLIED] = bool(
                    write_results[d["addr"]][0])
            items.append(item)
        return items

    @staticmethod
    def _vfd_params_norm_values(values):
        """values 载荷 → {addr:int: raw:int}；形状不合法抛 ValueError。

        [为什么键按十进制字符串收] BSON 文档键只能是 cstring（两端
        bson.py / XBsonDocument 同口径），int 键上不了线——客户端把地址
        以**十进制字符串**作键（"61441" = 0xF101），值 = raw int32。
        服务端这里 int(key) 归一成 int addr 再校验；解析不了就报错
        （fail-closed，不猜测）。
        """
        if not isinstance(values, dict) or not values:
            raise ValueError("变频器参数写入载荷缺少 values"
                             "（{地址十进制串: raw int}）")
        out = {}
        for key, raw in values.items():
            try:
                addr = int(str(key), 10)
            except (TypeError, ValueError):
                raise ValueError("参数地址键必须是十进制数：%r" % (key,))
            d = proto.PARAM_BY_ADDR.get(addr)
            if d is None:
                raise ValueError("未知参数地址：%d（PARAM_DEFS 之外不下发）"
                                 % addr)
            if not d["writable"]:
                raise ValueError("参数 %s（%s）只读，禁止写入"
                                 % (d["code"], d["label"]))
            if isinstance(raw, bool) or not isinstance(raw, int):
                raise ValueError("参数 %s（%s）写入值必须是 int 原始码，收到 %r"
                                 % (d["code"], d["label"], raw))
            # [量程预检] min/max 来自手册原始码范围（PARAM_DEFS，不是编的）。
            # 虚拟模型对任意值都收、真机靠异常码拒——服务端预检让两端
            # 行为一致，报错也带参数名可读。
            if raw < d["min"] or raw > d["max"]:
                raise ValueError("参数 %s（%s）写入值 %d 超出量程 [%d, %d]"
                                 % (d["code"], d["label"], raw, d["min"],
                                    d["max"]))
            out[addr] = raw
        return out

    async def _h_camera_frame(self, session, request_id, msg_code, _payload):
        """客户端按需取一帧相机画面（0x011C；2026-11-19 新增）。

        [画面本体走分片主题] 本命令只回**元信息**（宽/高/字节/时间戳/序号），
        真正的画面由 TOPIC_CAMERA_FRAME **分片推**给请求方（同一帧、同一
        格式）⇒ 客户端拿元信息确认"这一帧到了/多大/什么时候的"，画面走
        常规组帧路径（三端同一套组帧器）。
        [为什么要有这个命令] ① 客户端打开相机页时可**立刻**要一帧（不等下个
        周期帧）；② 存证场景可"按需拍一张"（先把画质调到最高再取）。
        """
        if self.camera is None:
            await self._reply(session, request_id, msg_code, False,
                              "相机未启用（请在远端设置里启用相机）")
            return
        if self._camera_es_mode():
            # [ES 直传] "取一帧" = 把最近 GOP 补发给请求方（JPEG 语义的
            # "单帧"在编码流里不存在）。**只发 GOP 不动 pending**——pending
            # 是订阅推流的增量队列，这里 drain 会把**正在观看的其它订阅者
            # 的帧偷走**（2026-11-19 审查实修：请求与订阅互偷丢帧）。
            gop = self.camera.gop_nals()
            nals = list(gop)
            if not nals:
                err = str(self._camera_status.get("error") or "")
                await self._reply(session, request_id, msg_code, False,
                                  "相机尚未出流%s"
                                  % ("：%s" % err if err else ""))
                return
            h265 = self.camera.detected_codec() == proto.CAMERA_CODEC_H265
            flags = proto.CAM_FRAME_FLAG_ES
            if h265:
                flags |= proto.CAM_FRAME_FLAG_H265
            for nal in nals:
                self._camera_published_seq = \
                    (self._camera_published_seq + 1) & 0xFFFFFFFF
                nf = _NalFrame(nal, self._camera_published_seq)
                for chunk in self._camera_fragments(nf, extra_flags=flags):
                    await session.send_publish(proto.TOPIC_CAMERA_FRAME,
                                               chunk, proto.DATATYPE_RAW)
            await self._reply(session, request_id, msg_code, True, extra={
                "mode": "es",
                "codec": self.camera.detected_codec(),
                "nals": int(len(nals)),
            })
            return
        frame = self.camera.latest_frame()
        if frame is None:
            err = str(self._camera_status.get("error") or "")
            await self._reply(session, request_id, msg_code, False,
                              "相机尚未出帧%s" % ("：%s" % err if err else ""))
            return
        # 立即把这一帧**分片推给请求方**（其它订阅者不受影响）
        for chunk in self._camera_fragments(frame):
            await session.send_publish(proto.TOPIC_CAMERA_FRAME, chunk,
                                      proto.DATATYPE_RAW)
        await self._reply(session, request_id, msg_code, True, extra={
            "width": int(frame.width),
            "height": int(frame.height),
            "bytes": int(len(frame.data)),
            "tsMs": int(frame.ts_ms),
            "seq": int(frame.seq),
        })

    async def _h_vfd_params(self, session, request_id, msg_code, payload):
        """变频器参数读/写（0x011B；2026-11-18 新增）。

        [闸门顺序] 设备就绪 → 载荷解析/op → 权限（读=登录即可 PERM_VIEW；
        写=管理员 PERM_ACCOUNT_ADMIN，照 0x0117 config_set 同款闸与拒绝
        文案）→ RunLocked（0x3000=1/2 运行中拒写，**不发总线写**）→ 执行。
        [为什么不设自动模式闸] 参数写已有"管理员 + RunLocked"双闸：运行中
        （含自动模式跑起来的时候）必然被 RunLocked 拦；停机状态下的铭牌
        维护是合法的停机动作，不需要人先切手动。
        [失败包络] 变频器未装配/离线/总线失败一律 ok=false
        「变频器未就绪」（不给假值）；异常明细进日志（ctrl）。
        """
        # 1) 设备就绪（未装配）
        if self.vfd is None:
            await self._reply(session, request_id, msg_code, False,
                              proto.VFD_PARAMS_ERR_NOT_READY)
            return
        # 2) 载荷解析 + op 分派（op 决定用哪一档权限，故先于权限闸）
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "变频器参数载荷解析失败：%s" % exc)
            return
        op = doc.get(proto.KEY_OP)
        if op == proto.VFD_PARAMS_OP_WRITE:
            # 3) 写：管理员（PERM_ACCOUNT_ADMIN，同 0x0117 闸与文案）
            if not permissions.can(session, proto.PERM_ACCOUNT_ADMIN):
                self._log_with_store(
                    "控制命令 0x%04X 被拒（%s）"
                    % (msg_code,
                       permissions.denied_text(proto.PERM_ACCOUNT_ADMIN)),
                    proto.LOG_CAT_CTRL)
                await self._reply(session, request_id, msg_code, False,
                                  permissions.denied_text(
                                      proto.PERM_ACCOUNT_ADMIN))
                return
            await self._vfd_params_write(session, request_id, msg_code, doc)
            return
        if op != proto.VFD_PARAMS_OP_READ:
            await self._reply(session, request_id, msg_code, False,
                              "变频器参数载荷解析失败（op 非法）")
            return
        # 4) 读：登录即可（只读面；本服务会话均经认证，PERM_VIEW 即
        #    "登录即可"的协议表达——匿名档在 require_auth 下不存在）
        if not permissions.can(session, proto.PERM_VIEW):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(proto.PERM_VIEW))
            return
        try:
            snap = await self.vfd.read_params_snapshot()
        except modbus.ModbusError as exc:
            self._log_with_store("变频器参数读取失败：%s" % exc,
                                 proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              proto.VFD_PARAMS_ERR_NOT_READY)
            return
        await self._reply(session, request_id, msg_code, True, extra={
            proto.KEY_VFD_PARAMS: self._vfd_param_items(snap),
            proto.KEY_VFD_FAULTED: bool(snap.get("faulted")),
            proto.KEY_VFD_RUN_WORD: int(snap.get("runState", 0)),
        })

    async def _vfd_params_write(self, session, request_id, msg_code, doc):
        """0x011B 写分派（管理员已过闸；见 _h_vfd_params 的闸门顺序）。"""
        # 1) values 预检（fail-closed：整包校验通过才碰总线——不许写半截）
        try:
            values = self._vfd_params_norm_values(doc.get(proto.KEY_VALUES))
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False, str(exc))
            return
        # 2) 读快照：RunLocked 闸 + 应答的 runState/vfdFaulted 同源一帧
        try:
            snap = await self.vfd.read_params_snapshot()
        except modbus.ModbusError as exc:
            self._log_with_store("变频器参数写入失败（读快照：%s）" % exc,
                                 proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              proto.VFD_PARAMS_ERR_NOT_READY)
            return
        if proto.vfd_param_run_locked(snap.get("runState")):
            self._log_with_store(
                "变频器参数写入被拒（运行中，0x3000=%s）"
                % snap.get("runState"), proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              proto.VFD_PARAMS_ERR_RUN_LOCKED)
            return
        # 3) 逐项写（地址升序：顺序确定，日志与两端口径可对账）。
        #    write_param 内部带写后回读重试（0.3~0.5s 不应答窗口）；
        #    并发写请求在驱动器侧按总线事务自然串行（FC06 逐寄存器原子）。
        results = {}
        for addr in sorted(values):
            raw = values[addr]
            applied, _raw_now, err = await self.vfd.write_param(addr, raw)
            results[addr] = (applied, _raw_now, err)
            d = proto.PARAM_BY_ADDR[addr]
            self._log_with_store(
                "变频器参数写入 %s %s（addr=%d raw=%d）：%s%s"
                % (d["code"], d["label"], addr, raw,
                   "已生效" if applied else "未生效",
                   "" if applied else "（%s）" % (err or "回读不一致")),
                proto.LOG_CAT_CTRL)
        # 4) 写后**重新读驱动器**（回读不撒谎：params 全部来自回读值）
        try:
            snap = await self.vfd.read_params_snapshot()
        except modbus.ModbusError as exc:
            # 写帧可能已生效但回读没跟上（不应答窗口）：如实报"待确认"，
            # 不给 params（没有可信回读就宁可不发数据）。
            self._log_with_store("变频器参数写后回读失败：%s" % exc,
                                 proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "写后回读失败（%s）：写入结果待确认，请重新读取"
                              % exc)
            return
        await self._reply(session, request_id, msg_code, True, extra={
            proto.KEY_VFD_PARAMS: self._vfd_param_items(snap,
                                                        write_results=results),
            proto.KEY_VFD_FAULTED: bool(snap.get("faulted")),
            proto.KEY_VFD_RUN_WORD: int(snap.get("runState", 0)),
        })

    async def _h_ai_snapshot(self, session, request_id, msg_code, payload):
        if self.analog is None:
            await self._reply(session, request_id, msg_code, False, "模拟量未装配")
            return
        # [请求即最新] 现读一次（100ms 节流）；模拟量单帧 ~2ms，代价可忽略。
        ok, err = await devices.read_fresh(self.analog, self._now_ms())
        if not ok:
            await self._reply(session, request_id, msg_code, False,
                              "读取模拟量模块失败：%s" % err)
            return
        await session.send_response(request_id, msg_code,
                                    self.encode_analog(self.analog, full=True))

    async def _h_temphum_snapshot(self, session, request_id, msg_code, payload):
        # [按侧 2026-11-13] 载荷 {"side":0|1}，缺省 0（A 侧）。
        side = 0
        try:
            doc = bson.decode(payload)
            if doc.get(proto.KEY_SIDE) is not None:
                side = int(doc.get(proto.KEY_SIDE) or 0)
        except (ValueError, TypeError):
            side = 0
        # [请求即最新] 现读一次（与变频器共享同一 485，节流避免抢总线）
        if self.temphum is not None and side == 0:
            await devices.read_fresh(self.temphum, self._now_ms())
        await session.send_response(request_id, msg_code,
                                    self.encode_temphum(self.temphum, side))

    async def _h_flowmeter_snapshot(self, session, request_id, msg_code,
                                    payload):
        if self.flowmeter is None:
            await self._reply(session, request_id, msg_code, False,
                              "流量计未装配")
            return
        # [请求即最新] 现读一次（与其他 485 从站共用总线，节流避免抢）
        await devices.read_fresh(self.flowmeter, self._now_ms())
        await session.send_response(request_id, msg_code,
                                    self.encode_flowmeter(self.flowmeter))

    async def _h_digital_snapshot(self, session, request_id, msg_code, payload):
        """载荷 {"dev":int} 选模块；缺省 0。"""
        index = 0
        try:
            doc = bson.decode(payload)
            index = int(doc.get(proto.KEY_DEV, 0) or 0)
        except (ValueError, TypeError):
            index = 0
        if not self.digital:
            await self._reply(session, request_id, msg_code, False, "数字量未装配")
            return
        if not 0 <= index < len(self.digital):
            await self._reply(session, request_id, msg_code, False,
                              "数字量模块号越界（0~%d）" % (len(self.digital) - 1))
            return
        # [请求即最新] 现读一次设备（100ms 内已读则复用，见 devices.read_fresh）。
        # 早先直接返回后台轮询的缓存值 —— 用户点"请求快照"拿到的可能是
        # 0~200ms 前的状态，对"要看当前 DI/DO"的需求不够实时。
        dev = self.digital[index]
        ok, err = await devices.read_fresh(dev, self._now_ms())
        if not ok:
            await self._reply(session, request_id, msg_code, False,
                              "读取数字量模块失败：%s" % err)
            return
        await session.send_response(request_id, msg_code,
                                    self.encode_digital(dev, index))

    # ---- 数字量 DO 控制 ----
    async def _h_digital_control(self, session, request_id, msg_code, payload):
        """DO 控制：载荷 单点 {"dev","do","on"} 或 整组 {"dev","mask"}。

        [闸门与变频器同口径] 自动模式拒绝 → 需设备控制权限 → 解析 → 设备就绪。
        DO 是驱动现场执行器的输出，安全等级与控制变频器相同，不另开口子。
        """
        if self.is_auto():
            # 类别 = ctrl（与 C 侧 hsfAppControlGate 的拒绝留痕同分类）。
            self._log_with_store(
                "控制命令 0x%04X 被拒（自动运行中：远程控制已被禁用）"
                % msg_code, proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        if not permissions.can(session, proto.PERM_DEVICE_CONTROL):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(
                                  proto.PERM_DEVICE_CONTROL))
            return
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷解析失败：%s" % exc)
            return
        index = int(doc.get(proto.KEY_DEV, 0) or 0)
        if not 0 <= index < len(self.digital):
            await self._reply(session, request_id, msg_code, False,
                              "数字量模块号越界（0~%d）"
                              % (max(0, len(self.digital) - 1)))
            return
        dev = self.digital[index]
        # [报警锁存闸 2026-11-15] 数字量页的**原始 DO 写**也是"启动设备"的
        # 途径（阀 DO0..5 / 泵 DO6）⇒ 锁存未解时，把这些位**置 1** 的写要拒；
        # 清 0（关阀/停泵）一律放行（否则人连停都停不了）。
        why = self._fault_gate_text()
        if why:
            device_dos = self._device_do_channels(index)
            hits = []
            if proto.KEY_MASK in doc or proto.KEY_VALUES in doc:
                values = int(doc.get(proto.KEY_MASK,
                                    doc.get(proto.KEY_VALUES, 0)) or 0)
                hits = [ch for ch in device_dos if (values >> ch) & 1]
            elif proto.KEY_DO in doc:
                channel = int(doc.get(proto.KEY_DO, 0) or 0)
                if channel in device_dos and bool(doc.get(proto.KEY_ON,
                                                          False)):
                    hits = [channel]
            if hits:
                self._log_with_store(
                    "控制命令 0x%04X 被拒（%s）" % (msg_code, why),
                    proto.LOG_CAT_CTRL)
                await self._reply(session, request_id, msg_code, False, why)
                return
        if proto.KEY_MASK in doc or proto.KEY_VALUES in doc:
            values = doc.get(proto.KEY_MASK, doc.get(proto.KEY_VALUES, 0))
            ok, err = await dev.write_group(values)
            log_text = ("数字量整组写 dev=%d mask=0x%08X：%s%s"
                        % (index, int(values or 0) & 0xFFFFFFFF,
                           "已受理" if ok else "被拒",
                           "" if ok else "（%s）" % (err or "写入失败")))
        elif proto.KEY_DO in doc:
            channel = int(doc.get(proto.KEY_DO, 0) or 0)
            on = bool(doc.get(proto.KEY_ON, False))
            ok, err = await dev.write_do(channel, on)
            log_text = ("数字量控制 dev=%d 通道%d→%d：%s%s"
                        % (index, channel, 1 if on else 0,
                           "已受理" if ok else "被拒",
                           "" if ok else "（%s）" % (err or "写入失败")))
        else:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 do 或 mask 字段")
            return
        # 类别 = ctrl（DO 是执行器输出，误动作要可追溯；与 C 侧
        # "数字量整组写…已受理/被拒" 逐字同文案同分类）。
        self._log_with_store(log_text, proto.LOG_CAT_CTRL)
        await self._reply(session, request_id, msg_code, ok, err or None,
                          extra={proto.KEY_DEV: index,
                                 proto.KEY_DO: dev.do_mask()})

    # ---- 电磁阀 ----
    async def _h_valve_snapshot(self, session, request_id, msg_code, payload):
        """电磁阀 6 路快照（只读，任何已登录角色可读）。"""
        if self.valve_bank is None:
            await self._reply(session, request_id, msg_code, False,
                              "电磁阀未装配")
            return
        # [请求即最新] 阀的反馈来自数字量模块缓存 —— 要求模块现读一次，
        # 否则"开到位/关到位"可能是 200ms 前的状态。
        await self.refresh_valve_sources()
        # 快照请求顺带刷新一次状态；此处**丢弃**告警返回值是刻意的：
        # 告警由 tick 里的 _publish_valve_alarms 统一推送，快照路径再推一遍
        # 会让同一个超时被推两次（客户端看到重复报警）。
        self.valve_bank.poll(self._now_ms())
        await session.send_response(request_id, msg_code, self.encode_valves())

    async def _h_valve_control(self, session, request_id, msg_code, payload):
        """电磁阀手动开关：载荷 {"index":int, "on":bool}。

        [闸门与变频器/DO 同口径] 自动模式拒绝 → 需设备控制权限 → 解析 → 就绪。
        电磁阀是执行器，自动模式下由现场逻辑驱动，远程只能看不能动。
        """
        if self.valve_bank is None:
            await self._reply(session, request_id, msg_code, False,
                              "电磁阀未装配")
            return
        if self.is_auto():
            # 类别 = ctrl（拒绝留痕，与其它控制命令同一口径）。
            self._log_with_store(
                "控制命令 0x%04X 被拒（自动运行中：远程控制已被禁用）"
                % msg_code, proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        if not permissions.can(session, proto.PERM_DEVICE_CONTROL):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(
                                  proto.PERM_DEVICE_CONTROL))
            return
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷解析失败：%s" % exc)
            return
        index = doc.get(proto.KEY_VALVE_INDEX, doc.get("index"))
        if not isinstance(index, int) or isinstance(index, bool):
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 index 字段（阀号 0~5）")
            return
        on = doc.get("on")
        if not isinstance(on, bool):
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 on 字段（true=开 / false=关）")
            return
        # [报警锁存闸 2026-11-15] **开阀**属于"让设备动起来" ⇒ 锁存未解
        # 时拒绝；关阀放行（否则人连"把阀关上"都做不了）。
        if on:
            why = self._fault_gate_text()
            if why:
                self._log_with_store(
                    "控制命令 0x%04X 被拒（%s）" % (msg_code, why),
                    proto.LOG_CAT_CTRL)
                await self._reply(session, request_id, msg_code, False, why)
                return
        ok, err = await self.valve_bank.set_valve(int(index), on)
        # ---- 命令已受理：回一拍**命令之后**的真实状态 ----
        #
        # [实锅 2026-11-05：第一下点击"没反应"] 原来这里只调
        # `valve_bank.poll()`，而它读的是**数字量模块的缓存**（由后台
        # 轮询刷新）。命令刚写完 DO，缓存还是命令**之前**那一拍的反馈
        # ⇒ 应答里 state 仍是旧值 ⇒ 客户端拿这份"最新状态"刷新卡片，
        # 界面毫无变化。用户再点一次时恰好赶上下一轮轮询，才看到变化。
        #
        # 故这里必须：
        #   ① force 现读一次数字量模块（绕过节流，否则 100ms 内的缓存
        #      同样可能是旧的）；
        #   ② 再让阀状态机重新判读，用新反馈生成应答。
        # 读失败不影响应答（命令本身已下发成功），状态就是"动作中"。
        await self.refresh_valve_sources(force=True)
        self.valve_bank.poll(self._now_ms())
        extra = self.valve_bank.to_doc()
        await self._reply(session, request_id, msg_code, ok, err or None,
                          extra=extra)

    async def _h_turbidity_snapshot(self, session, request_id, msg_code,
                                   payload):
        """浊度仪快照：现读一次再回（两台各一帧 IO）。"""
        if not self.turbidity:
            await self._reply(session, request_id, msg_code, False,
                              "浊度仪未装配")
            return
        now = self._now_ms()
        for tm in self.turbidity:
            try:
                await devices.read_fresh(tm, now)
            except Exception as exc:                    # noqa: BLE001
                # 单台失败不影响另一台（真实现场常有一台在修）
                # 类别 = alarm：485 读失败（通信故障）。
                self._log_with_store("%s 现读失败：%s" % (tm.name, exc),
                                     proto.LOG_CAT_ALARM)
        await session.send_response(request_id, msg_code,
                                    self.encode_turbidity())

    async def _h_pump_snapshot(self, session, request_id, msg_code, payload):
        if self.pump_bank is None:
            await self._reply(session, request_id, msg_code, False,
                              "进水泵未装配")
            return
        # 现读一次数字量模块，让回显是最新的
        await self.refresh_pump_sources()
        self.pump_bank.poll(self._now_ms(), self.cfg.pump_stale_ms)
        await session.send_response(request_id, msg_code,
                                    self.encode_pumps())

    async def refresh_pump_sources(self, force=False):
        """让泵用到的数字量模块现读一次（去重 + 节流）。

        与 `refresh_valve_sources` 同一个道理：命令后要用**命令之后**的
        回显生成应答，否则界面第一下看不出变化。两处模块可能相同（现场
        都在 .12 上），但各自去重，互不依赖。
        """
        if self.pump_bank is None:
            return
        seen = set()
        for item in self.pump_bank.items:
            if not item.ready or item.module is None:
                continue
            if item.module in seen:
                continue
            seen.add(item.module)
            if 0 <= item.module < len(self.digital):
                await devices.read_fresh(self.digital[item.module],
                                         self._now_ms(),
                                         min_interval_ms=0 if force else 100)

    async def _h_pump_control(self, session, request_id, msg_code, payload):
        """进水泵开关：载荷 {"index":int, "on":bool}。

        [闸门与电磁阀/变频器同口径] 自动模式拒绝 → 需设备控制权限 → 解析 → 就绪。
        泵是执行器，自动模式下由现场逻辑驱动，远程只能看不能动。
        """
        if self.pump_bank is None:
            await self._reply(session, request_id, msg_code, False,
                              "进水泵未装配")
            return
        if self.is_auto():
            # 类别 = ctrl（拒绝留痕，与其它控制命令同一口径）。
            self._log_with_store(
                "控制命令 0x%04X 被拒（自动运行中：远程控制已被禁用）"
                % msg_code, proto.LOG_CAT_CTRL)
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        if not permissions.can(session, proto.PERM_DEVICE_CONTROL):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(
                                  proto.PERM_DEVICE_CONTROL))
            return
        try:
            doc = bson.decode(payload)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷解析失败：%s" % exc)
            return
        index = doc.get(proto.KEY_PUMP_INDEX, doc.get("index"))
        if not isinstance(index, int) or isinstance(index, bool):
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 index 字段（泵号 0 起）")
            return
        on = doc.get("on")
        if not isinstance(on, bool):
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 on 字段（true=启动 / false=停止）")
            return
        # [报警锁存闸 2026-11-15] **启泵**属于"让设备动起来" ⇒ 锁存未解
        # 时拒绝；停泵放行（否则人连"把泵停了"都做不了）。
        if on:
            why = self._fault_gate_text()
            if why:
                self._log_with_store(
                    "控制命令 0x%04X 被拒（%s）" % (msg_code, why),
                    proto.LOG_CAT_CTRL)
                await self._reply(session, request_id, msg_code, False, why)
                return
        ok, err = await self.pump_bank.set_pump(int(index), on)
        # 命令后强制现读，让应答带**命令之后**的回显（否则界面第一下不变）
        await self.refresh_pump_sources(force=True)
        self.pump_bank.poll(self._now_ms(), self.cfg.pump_stale_ms)
        await self._reply(session, request_id, msg_code, ok, err or None,
                          extra=self._pump_doc())

    def _pump_doc(self):
        """控制应答的附加字段（与推送同一套键，客户端复用一套解析）。"""
        if self.pump_bank is None:
            return {proto.KEY_COUNT: 0}
        doc = {proto.KEY_COUNT: len(self.pump_bank.items)}
        for item in self.pump_bank.items:
            prefix = "v%d_" % item.index
            doc[prefix + proto.KEY_PUMP_NAME] = item.name
            doc[prefix + proto.KEY_PUMP_ON] = bool(item.on)
            doc[prefix + proto.KEY_PUMP_READY] = bool(item.ready)
            doc[prefix + proto.KEY_VALID] = bool(item.valid)
            if item.text:
                doc[prefix + proto.KEY_PUMP_TEXT] = item.text
        return doc

    # ---- 运行模式 ----
    async def _h_run_mode(self, session, request_id, msg_code, payload):
        """载荷 {"mode":int} 有 mode=设置（需 root），无=查询（所有已认证角色）。"""
        want_set = False
        new_mode = 0
        try:
            doc = bson.decode(payload)
            if proto.KEY_MODE in doc:
                want_set = True
                new_mode = int(doc.get(proto.KEY_MODE, 0) or 0)
        except (ValueError, TypeError):
            want_set = False
        if not want_set:
            await self._reply(session, request_id, msg_code, True, None,
                              extra={proto.KEY_MODE: self.run_mode,
                                     proto.KEY_AUTO: self.is_auto(),
                                     proto.KEY_AUTO_RUNNING:
                                         self.is_auto_running(),
                                     **self._auto_state_extra()})
            return
        # [拒绝也回全量键（对齐 C 2026-11-11 口径，HsfServerApp.c
        # hsfAppMakeModeEnvelope）] 权限拒绝 / 非法模式值的否定应答同样带上
        # mode/auto/autoRunning/autoMode/autoPhase/… 全量状态键——客户端拿
        # 拒绝应答就能把界面状态对齐，不必另发一次查询。
        state_extra = {proto.KEY_MODE: self.run_mode,
                       proto.KEY_AUTO: self.is_auto(),
                       proto.KEY_AUTO_RUNNING: self.is_auto_running(),
                       **self._auto_state_extra()}
        if not permissions.can(session, proto.PERM_MODE_SWITCH):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(proto.PERM_MODE_SWITCH),
                              extra=state_extra)
            return
        if not self.set_run_mode(new_mode):
            await self._reply(session, request_id, msg_code, False, "非法模式值",
                              extra=state_extra)
            return
        await self._reply(session, request_id, msg_code, True, None,
                          extra={proto.KEY_MODE: self.run_mode,
                                 proto.KEY_AUTO: self.is_auto(),
                                 # 切换后立刻回显控制器真实状态：这里是
                                 # apply_mode_to_devices() 刚跑完的时刻，所以
                                 # 客户端拿到的 running 是**切换之后的**值，
                                 # 不必等下一帧推送。
                                 proto.KEY_AUTO_RUNNING:
                                     self.is_auto_running(),
                                 **self._auto_state_extra()})

    # ---- 自动运行控制 ----
    async def _h_auto_control(self, session, request_id, msg_code, payload):
        """启停自动运行（载荷 {"op":int} 四板斧；兼容老载荷 {"on":bool}）。

        [与 CMD_RUN_MODE 的分工] 这条是**真正按下启动/停止**，模式切换只是
        授权。闸门顺序与其它控制命令一致，另加一条"必须在自动模式下才能
        启动"：
          · 手动模式下请求启动 → 拒绝（否则等于绕过模式闸偷偷跑自动逻辑）
          · 停止/急停/复位**永远允许**（停机类动作不该被任何闸门挡住）

        [四板斧与 C 端同口径 2026-11-06] op 缺省时按 {"on":bool} 折算：
            op=1 启动（须自动模式 + 急停正常 + 未锁存）
            op=2 停止（停控制器 + 全停设备）
            op=3 急停（停控制器 + 全停设备 + **锁存**）
            op=4 复位（解除锁存，回停止态，**不启动**）
        """
        # 1) 权限门禁（停机也要求自动控制权限：自动运行状态属于运行决策，
        #    操作者只能控单台设备，不开放自动运行）
        if not permissions.can(session, proto.PERM_AUTO_CONTROL):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(
                                  proto.PERM_AUTO_CONTROL))
            return
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            await self._reply(session, request_id, msg_code, False,
                              "自动控制器未装配")
            return
        # 2) 解析载荷（op 优先；缺 op 时按老口径折算）
        try:
            doc = bson.decode(payload) if payload else {}
            raw_op = doc.get(proto.KEY_OP)
            if raw_op is not None:
                op = int(raw_op)
            else:
                op = (proto.AUTO_OP_START if bool(doc.get("on"))
                      else proto.AUTO_OP_STOP)
        except (ValueError, TypeError):
            await self._reply(session, request_id, msg_code, False, "载荷不合法")
            return
        if op not in (proto.AUTO_OP_START, proto.AUTO_OP_STOP,
                      proto.AUTO_OP_ESTOP, proto.AUTO_OP_RESET,
                      proto.AUTO_OP_SELECT):
            await self._reply(session, request_id, msg_code, False,
                              "非法 op 操作字（1启动/2停止/3急停/4复位"
                              "/5选模式）")
            return
        # 3) 启动的条件判定（与现场启动按钮**同一套判据**，见
        #    AutoController.start_preconditions / mode_config_error —— 两处
        #    条件必须同源，否则"现场能启动、远程不能启动"这种分歧极难查）
        if op == proto.AUTO_OP_START:
            ok, why = ctl.start_preconditions(self.is_auto())
            if not ok:
                await self._reply(
                    session, request_id, msg_code, False,
                    ("当前是手动模式：请先切到自动模式，再启动自动运行"
                     if not self.is_auto() else why),
                    extra={proto.KEY_MODE: self.run_mode,
                           proto.KEY_AUTO: self.is_auto(),
                           proto.KEY_AUTO_RUNNING: self.is_auto_running(),
                           **self._auto_state_extra()})
                return
            # 3b) 模式判定：载荷带 mode 用载荷的；没带沿用控制器里粘性的
            #     上次模式。配置不可跑（未配置设备/阀点位缺失/组合模式未
            #     实现）⇒ 明确回错因。
            want_mode = doc.get(proto.KEY_AUTO_MODE)
            if not want_mode:
                # [粘性分账 2026-11-18 审计] 回落顺序：
                #   selected_mode = **用户选中的模式**（C 端 s_mode）
                #   mode          = 运行中模式 / 组合循环子阶段
                # 原先只回落 ctl.mode ⇒ 与现场物理启动按钮（用
                # selected_mode）可能按**不同模式**跑 ⇒ 正是「物理 ≡
                # 屏幕」要消灭的现象。空串也回落粘性（C 端即回落
                # stickyMode）。
                want_mode = (getattr(ctl, "selected_mode", "") or
                             getattr(ctl, "mode", "") or "")
            want_mode = str(want_mode)
            why_mode = ctl.mode_config_error(want_mode)
            if why_mode:
                await self._reply(
                    session, request_id, msg_code, False, why_mode,
                    extra={proto.KEY_MODE: self.run_mode,
                           proto.KEY_AUTO: self.is_auto(),
                           proto.KEY_AUTO_RUNNING: self.is_auto_running(),
                           **self._auto_state_extra()})
                return
        elif op == proto.AUTO_OP_SELECT:
            # [只选模式 2026-11-13·板端实锅] 只记粘性 + 持久化，不启动、
            # 不动设备、不判启动条件（那是启动时刻的判定）。客户端主界面
            # 的模式下拉在 activated 时发本 op，使服务端粘性与界面一致
            # ——物理启动按钮用粘性，粘性为空=现场按键被拒。
            #
            # [运行中也受理 2026-11-18 板端实锅] 老口径「运行中不可改选」
            # 的理由是 Python 端把**粘性**与**组合循环当前子阶段**挤在
            # 同一个 ctl.mode ⇒ 运行中一条 op=5 会把正在跑的生产阶段
            # 整个改写（产水跑到一半被按"顺洗时限"判到期 ⇒ 换相、停泵、
            # 重建阀目标）。
            #
            # 现在**粘性已分账**（ctl.selected_mode 记「用户的选择」，
            # ctl.mode 只管运行中）⇒ 运行中改选**不再碰运行中的子阶段**，
            # 只是记下「下次启动按这个模式」⇒ 可以受理。
            # [用户口径 2026-11-18]「物理的启动和停止接入屏幕上的启动和
            # 停止」——两者要同效，粘性就得跟得住下拉；客户端在**运行中**
            # 已把模式下拉置灰（用户口径），但服务端不能假设客户端永远
            # 守着 ⇒ 这里按「只记选择、不动运行态」受理。
            want_mode = str(doc.get(proto.KEY_AUTO_MODE) or "")
            # 合法 id = 三子模式 + 组合循环「auto」（mode_config_error 对
            # auto 的循环配置校验留到启动时刻，选模式只记粘性）。
            if want_mode not in dict(proto.AUTO_MODES) \
                    and want_mode != proto.AUTO_MODE_AUTO:
                await self._reply(
                    session, request_id, msg_code, False,
                    "未知自动模式 id（%s）" % (want_mode or "空"),
                    extra={proto.KEY_MODE: self.run_mode,
                           proto.KEY_AUTO: self.is_auto(),
                           proto.KEY_AUTO_RUNNING: self.is_auto_running(),
                           **self._auto_state_extra()})
                return
        else:
            want_mode = None
        # 4) 执行（幂等）
        now = self._now_ms()
        # [应答值先置缺省 2026-11-18] op=1/2/3/5 不产生 note ⇒ ok=True、
        # err=None；只有 op=4 的复位分支会改写（含「运行中不可复位」拒绝）。
        # [教训] A3 改造时把 ok_r/note_r 的赋值只放在 op=4 分支 ⇒ 其余 op
        # 走到应答行 UnboundLocalError ⇒ **全部自动控制命令 500**（webui_check
        # 11 条红当场抓的）。
        ok_r, note_r = True, None
        if op == proto.AUTO_OP_SELECT:
            # [粘性分账 2026-11-18] 只记「用户选中的模式」⇒ 运行中的
            # 模式/循环子阶段不受影响（运行中改选不改写正在跑的工序）。
            ctl.selected_mode = want_mode
            ctl.ctx.persist_mode(want_mode)
            if not getattr(ctl, "running", False):
                # 未在运行时 self.mode 就是「下次启动按哪个模式跑」⇒ 同步
                # 跟上（状态回显 autoMode 与粘性一致）。
                ctl.mode = want_mode
        elif op == proto.AUTO_OP_START:
            ctl.start(now, mode=want_mode)
        elif op == proto.AUTO_OP_STOP:
            # 人按"停止自动运行"⇒ 停控制器**并停全部设备**：
            # 只停控制器会让设备继续按上一条命令动着，现场看到的是
            # "我按了停止，泵还在转"。
            # [阀是否复位按模式配置 2026-11-12] stop() 缺省 reset_valves=None
            # ⇒ 读当前模式配置（逐模式可设「停止时是否关阀复位」）。
            ctl.stop(now, stop_devices=True, reason="远程停止自动运行")
        elif op == proto.AUTO_OP_ESTOP:
            # 急停：阀是否复位同样按模式配置（缺省 False＝保持现状）。
            ctl.e_stop("远程急停")
        else:
            ok_r, note_r = await self._auto_reset_and_vfd()
        await self._reply(session, request_id, msg_code, ok_r,
                          (note_r if not ok_r else None),
                          extra={proto.KEY_MODE: self.run_mode,
                                 proto.KEY_AUTO: self.is_auto(),
                                 proto.KEY_AUTO_RUNNING:
                                     self.is_auto_running(),
                                 **self._auto_state_extra()})

    async def _auto_reset_and_vfd(self):
        """op=4「复位」：清两把锁；**变频器在故障中 ⇒ 顺带给它发故障复位**。

        [为什么 2026-11-17 用户口径] 「变频器报警后，我需要按下复位后可以
        对变频器复位报警信息，比如过载报警，不向变频器发送复位，变频器他
        不让启动」——原来 op=4 只清服务端锁存，变频器自己（M512）锁在故障
        里 ⇒ 再按启动会被变频器拒绝。⇒ 复位时若变频器正处于故障
        （faulted / fault_code 非 0），写 0x2000=7（M512 故障复位，与变频器
        页「故障复位」按钮同一条命令；真机实测"复位后故障码=0"）。
        [无故障不写] 变频器没在故障（如阀超时类报警）⇒ 不发命令、不多写
        总线；附注返回空串。
        [失败不拦] 总线写失败只记日志（返回附注），复位的"清锁存"部分
        照常生效——人已经确认过现场，不能因为总线抖动把锁存留在锁着的状态。
        :returns: 附注文本（空串 = 无变频器复位动作）
        """
        ctl = self.auto_controller
        # [运行中拒绝 2026-11-18 联调深检] 复位在自动序列运行中会架空序列
        # （见 autocontrol.reset 的守卫注释）⇒ 明确拒而不是静默架空。
        if ctl._running:
            self._log_with_store("自动复位被拒：自动运行中不可复位（请先停止）",
                                 proto.LOG_CAT_CTRL)
            return False, "运行中不可复位（请先停止自动运行）"
        ctl.reset()
        vfd = self.vfd
        if vfd is None or not (getattr(vfd, "faulted", False)
                               or getattr(vfd, "fault_code", 0)):
            return True, ""
        try:
            ok, err = await vfd.apply_control(proto.OP_RESET_FAULT)
        except Exception as exc:                        # noqa: BLE001
            note = "变频器故障复位下发异常：%s" % exc
            self._log_with_store("自动复位：" + note, proto.LOG_CAT_CTRL)
            return True, note
        if ok:
            note = "变频器故障复位已下发"
        else:
            note = "变频器故障复位下发失败（%s）" % (err or "设备写入失败")
        self._log_with_store("自动复位：" + note, proto.LOG_CAT_CTRL)
        # [基线同步 2026-11-18 联调深检] 复位下发成功 ⇒ 驱动器故障将清；
        # 把故障边沿基线同步成"无故障"，否则 poll→tick 窗口里的旧故障沿
        # 会把刚清掉的锁存重新锁上（首次复位被吞，得按两次）。
        self._vfd_faulted = False
        return True, note

    # ---- 设备健康 ----
    async def _h_device_health(self, session, request_id, msg_code, payload):
        await session.send_response(request_id, msg_code, self.encode_health())

    # ==================================================================
    #                    历史数据分页读取
    # ==================================================================
    async def _h_history_page(self, session, request_id, msg_code, payload):
        """分页读历史：{"offset","limit","beforeTs"?,"loTs"?,"hiTs"?}。

        [权限] 浏览（缺省）属只读面；**导出/下载**（载荷 export=true）需
        Download 权限（普通用户+）。与 C 侧同口径：导出意图必须上服务端
        验权，客户端灰按钮拦不住直接发帧的人。

        [窗口锚定 beforeTs] 客户端首屏把自己的"现在"发上来，翻页时原样
        回传，服务端据此把整段分页钉在同一批数据上。否则库在持续入库
        （5s 一行），翻到第 2 页时新行已经顶进来，offset 会整体偏移
        → 相邻两页出现重复行、旧行被跳过。

        [总数与页内容同口径] total 与 rows 用**同一 WHERE**，所以
        「共 N 条 / 第 x 页」与实际能翻到的行永远一致。
        """
        if self.store.db_path is None:
            await self._reply(session, request_id, msg_code, False,
                              "落盘未启用：服务端没有历史数据可查")
            return
        try:
            doc = bson.decode(payload) if payload else {}
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False,
                              "分页载荷解析失败：%s" % exc)
            return
        # [下载权限闸] 导出意图必须持有 Download；普通浏览不受此限。
        export_mode = bool(doc.get(proto.HIST_KEY_EXPORT))
        if export_mode and not permissions.can(
                session, proto.PERM_DOWNLOAD):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(proto.PERM_DOWNLOAD))
            return

        def _int(key, default=None):
            value = doc.get(key, default)
            if value is None:
                return None
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError("%s 需要整数时间戳/序号" % key)
            return int(value)

        try:
            offset = max(0, _int(proto.HIST_KEY_OFFSET, 0))
            raw_limit = _int(proto.HIST_KEY_LIMIT, proto.HISTORY_PAGE_DEFAULT)
            before_ts = _int(proto.HIST_KEY_BEFORE_TS, None)
            lo_ts = _int(proto.HIST_KEY_LO_TS, None)
            hi_ts = _int(proto.HIST_KEY_HI_TS, None)
        except ValueError as exc:
            await self._reply(session, request_id, msg_code, False, str(exc))
            return

        # 关键字 / 列下标：BSON 数组解出来是 list；夹到合法显示列范围，
        # 防止恶意/畸形下标让 store 拼出越界表达式。空列表 = 全部列。
        kw = doc.get(proto.HIST_KEY_KW) or ""
        kw = str(kw)[:128]                       # 防超长关键字拖慢 LIKE
        raw_cols = doc.get(proto.HIST_KEY_KW_COLS) or []
        if not isinstance(raw_cols, (list, tuple)):
            raw_cols = []
        kw_cols = []
        for v in raw_cols:
            if isinstance(v, bool) or not isinstance(v, (int, float)):
                continue
            i = int(v)
            if 0 <= i < len(self.store.DISPLAY_EXPR):
                kw_cols.append(i)

        # 页大小夹取：客户端说了算，但不得突破载荷上限。
        limit = max(proto.HISTORY_PAGE_MIN,
                    min(proto.HISTORY_PAGE_MAX, int(raw_limit or 0)))
        # [窗口锚点用**墙钟**] 库里的 ts 是 epoch 毫秒（见 _history_snapshot），
        # 这里若拿 _now_ms()（monotonic，开机计时）当上界，`ts <= 锚点` 会把
        # 全部行滤掉 —— 实测 total 恒 0、历史页永远空白。
        window_end = (int(time.time() * 1000) if before_ts is None
                      else before_ts)
        # 时间范围与窗口取交集（hiTs 比窗口更晚时以窗口为准）。
        effective_hi = window_end if hi_ts is None else min(hi_ts, window_end)

        # [查询挪出事件循环 2026-11-14] 历史分页是**库里筛**（时间/关键字/
        # 列同一条 WHERE）⇒ 关键字条件要把 19 个显示表达式在**每一行**上拼
        # 一遍做 LIKE ⇒ 12MB 库（4 万行）实测**单次 397ms**。这段同步 SQL
        # 跑在事件循环线程上 ⇒ 期间**所有**帧（含 0x0115 自动控制）都得不到
        # 处理 ⇒ 自动控制应答被顶掉几百毫秒。改为丢到工作线程
        # （asyncio.to_thread）⇒ 事件循环照常收帧/应答。
        # [导出模式只取行 2026-11-17 性能优化] 导出每批只需要 rows：
        #   · `COUNT(*)`：关键字查询是全表扫（20 万行 / 16 个显示表达式
        #     实测 **747ms**，占单批 790ms 的绝大部分）；
        #   · `history_bounds()`：MIN/MAX 两趟索引；
        #   · `row_count()`：写连接再来一次 COUNT。
        # 三者界面翻页都要（算页数 / 默认起始时间 / 库内共 N 条），导出
        # **一样都用不到**（终止靠短批，total 只用于写"命中 N 条"）⇒
        # 导出模式全部跳过，total 回 -1（未知）。
        rows, window_total = await asyncio.to_thread(
            self.store.query_history_page,
            offset=offset, limit=limit, lo_ms=lo_ts, hi_ms=effective_hi,
            kw=kw, kw_cols=kw_cols, with_count=not export_mode)
        if export_mode:
            earliest_ts = latest_ts = 0
            db_rows = 0
        else:
            # [时间边界只认有效墙钟] history_bounds() 过滤掉 2020 年前的坏
            # ts（旧版本误写 monotonic 的 1970 年行）。直接 MIN(ts) 会把
            # "最前一条数据的时间"报成 1970-01-01 —— 客户端拿它当默认起始
            # 时间等于没筛。
            earliest_ts, latest_ts = await asyncio.to_thread(
                self.store.history_bounds)
            # [行数留在循环线程 2026-11-14] row_count 走**写连接**（轻量
            # PRAGMA/COUNT，1~3ms）；若挪到工作线程，它会去抢读连接那把锁
            # ⇒ 与长查询互等 ⇒ 又把事件循环堵住（实测首条自动控制被顶
            # 233ms）。
            db_rows = self.store.row_count("history")

        await self._reply_checked(
            session, request_id, msg_code,
            {
                proto.HIST_KEY_TOTAL: int(window_total),
                proto.HIST_KEY_OFFSET: int(offset),
                proto.HIST_KEY_LIMIT: int(limit),
                proto.HIST_KEY_COLS: list(self.store.HISTORY_COLUMNS),
                # sqlite3.Row / tuple → list：BSON 只认 list/tuple，tuple 可以
                # 但 list 在客户端拼装更省事（直接当数组用）。
                proto.HIST_KEY_ROWS: [list(r) for r in rows],
                proto.HIST_KEY_DB_ROWS: int(db_rows),
                proto.HIST_KEY_LATEST_TS: latest_ts,
                proto.HIST_KEY_EARLIEST_TS: earliest_ts,
                proto.HIST_KEY_WINDOW_START_TS: int(lo_ts or 0),
                proto.HIST_KEY_WINDOW_END_TS: int(window_end),
            },
            "本页数据过大（超过单帧上限），请减小每页条数后重试")

    # ==================================================================
    #                      运行日志分页读取（0x0119）
    # ==================================================================
    async def _h_log_page(self, session, request_id, msg_code, payload):
        """分页读运行日志：{"offset","limit","beforeTs"?,"loTs"?,"hiTs"?,
        "category"?,"kw"?}。

        [为什么补这个命令 2026-11-09] C 客户端的「日志」页走 0x0119；本
        服务端原先**没有实现** ⇒ C 客户端日志页永远停在"读取中"、一条都
        看不到（跨端缺陷）。

        [口径] 与 C 侧逐字对齐：应答 {"ok","err"?,"total","offset","limit",
        "rows":[{ts,category,text}]}；时间 DESC 取页、页内正序。

        [类别 2026-11-09] 老库经 `_migrate_logs_category` 已补出
        `logs.category` 列；写入侧 `_log_with_store(msg, cat)` 按**调用点
        语义**给类别（conn/ctrl/alarm/auto/auth；其余 sys）。故本处**如实
        回类别并支持精确过滤**——此前硬编码空串导致 C 客户端「类型」列
        全是空白、按类别筛选恒空（跨端缺陷）。
        """
        # [落盘未启用要明确说 2026-11-17] 原来无此闸：store 未开时 SQL 层
        # 直接回空表 ⇒ 客户端日志页显示「共 0 条」（像"日志真的一条没有"），
        # 而不是"没开落盘"。历史页早就有这道闸，两页口径统一。
        if self.store.db_path is None:
            await self._reply(session, request_id, msg_code, False,
                              "落盘未启用：服务端没有日志可查")
            return
        doc = {}
        try:
            doc = bson.decode(payload) if payload else {}
        except ValueError:
            doc = {}

        def _int(key, default=None):
            value = doc.get(key)
            if value is None:
                return default
            try:
                return int(value)
            except (TypeError, ValueError):
                return default

        offset = max(0, _int(proto.HIST_KEY_OFFSET, 0) or 0)
        limit = max(1, min(proto.HISTORY_PAGE_MAX,
                           _int(proto.HIST_KEY_LIMIT,
                                proto.HISTORY_PAGE_DEFAULT)
                           or proto.HISTORY_PAGE_DEFAULT))
        before_ts = _int(proto.HIST_KEY_BEFORE_TS)
        lo_ts = _int(proto.HIST_KEY_LO_TS)
        hi_ts = _int(proto.HIST_KEY_HI_TS)
        kw = str(doc.get(proto.HIST_KEY_KW) or "")[:128]
        # 类别精确过滤：键字面 "category"（与 C 侧 HSF_KEY_LOG_CATEGORY 同字面）；
        # 空串/缺省 = 全部类别（不过滤）。
        category = str(doc.get(proto.LOG_KEY_CATEGORY) or "")[:32]
        # [查询挪出事件循环 2026-11-14] 同历史页：日志分页的同步 SQL 丢到
        # 工作线程 ⇒ 慢查询不阻塞自动控制等命令的应答。
        # [导出模式不数总数 2026-11-17] 同历史页：日志导出每批一次 COUNT
        # 纯属浪费（无关键字时很便宜，有关键字同样是全表扫）。客户端导出
        # 带 HIST_KEY_EXPORT（与历史页同键，日志页沿用）⇒ total 回 -1。
        _log_export = bool(doc.get(proto.HIST_KEY_EXPORT))
        rows, window_total = await asyncio.to_thread(
            self.store.query_logs_page,
            offset=offset, limit=limit, before_ms=before_ts, lo_ms=lo_ts,
            hi_ms=hi_ts, kw=kw or None, category=category or None,
            with_count=not _log_export)
        payload_rows = []
        for row in rows:
            payload_rows.append({
                proto.LOG_KEY_TS: int(row[0] or 0),
                proto.LOG_KEY_CATEGORY: str(row[1] or ""),
                proto.LOG_KEY_TEXT: str(row[2] or ""),
            })
        await self._reply_checked(
            session, request_id, msg_code,
            {
                proto.HIST_KEY_TOTAL: int(window_total),
                # [count 2026-11-09 补契约] C 端 0x0119 应答含 count（页内
                # 行数，HSF_KEY_LOG_COUNT，见 HsfServerApp.c 的「count 补
                # 契约」段）；本端此前只回 total/offset/limit/rows ⇒ 两端
                # 应答键集不一致，观测对账（total vs 本页行数）在 Python
                # 服务端上做不了。客户端实际用 len(rows)，此键纯供对账。
                proto.LOG_KEY_COUNT: len(payload_rows),
                proto.HIST_KEY_OFFSET: int(offset),
                proto.HIST_KEY_LIMIT: int(limit),
                proto.HIST_KEY_ROWS: payload_rows,
            },
            "本页数据过大（超过单帧上限），请减小每页条数后重试")

    # ==================================================================
    #                      报警历史分页读取（0x011A）
    # ==================================================================
    async def _h_alarm_page(self, session, request_id, msg_code, payload):
        """分页读报警历史：{"offset","limit","beforeTs"?,"loTs"?,"hiTs"?,
        "kw"?,"export"?}。

        [为什么新增 2026-11-17] 报警页此前是数据三页里**唯一不向服务端取数**
        的一页：客户端与网页端各自在**本地进程里**算健康边沿 ⇒ 记录只在该
        进程活着期间存在（网页端是常驻服务、客户端一重启历史全丢）⇒ 用户实报
        「web 端报警有数据，客户端报警视图没数据」。
        ⇒ 边沿改由**服务端**记进 alarms 表（健康异常/恢复 + 自动告警 +
        阀告警 + 流量计/保护告警），两端都按本命令**取同一份**。

        [口径] 与 0x0118/0x0119 同形：应答 {"ok","err"?,"total","count",
        "offset","limit","rows":[{ts,kind,source,text,dev}]}；
        时间 DESC 取页、页内正序。
        """
        if self.store.db_path is None:
            await self._reply(session, request_id, msg_code, False,
                              "落盘未启用：服务端没有报警记录可查")
            return
        doc = {}
        try:
            doc = bson.decode(payload) if payload else {}
        except ValueError:
            doc = {}

        def _int(key, default=None):
            value = doc.get(key)
            if value is None:
                return default
            try:
                return int(value)
            except (TypeError, ValueError):
                return default

        offset = max(0, _int(proto.HIST_KEY_OFFSET, 0) or 0)
        limit = max(1, min(proto.HISTORY_PAGE_MAX,
                           _int(proto.HIST_KEY_LIMIT,
                                proto.HISTORY_PAGE_DEFAULT)
                           or proto.HISTORY_PAGE_DEFAULT))
        before_ts = _int(proto.HIST_KEY_BEFORE_TS)
        lo_ts = _int(proto.HIST_KEY_LO_TS)
        hi_ts = _int(proto.HIST_KEY_HI_TS)
        kw = str(doc.get(proto.HIST_KEY_KW) or "")[:128]
        _export = bool(doc.get(proto.HIST_KEY_EXPORT))
        # [查询挪出事件循环] 同历史/日志页：慢查询丢到工作线程 ⇒ 不阻塞
        # 自动控制等命令的应答。
        rows, window_total = await asyncio.to_thread(
            self.store.query_alarms_page,
            offset=offset, limit=limit, before_ms=before_ts, lo_ms=lo_ts,
            hi_ms=hi_ts, kw=kw or None, with_count=not _export)
        payload_rows = []
        for row in rows:
            payload_rows.append({
                proto.LOG_KEY_TS: int(row[0] or 0),
                proto.ALARM_KEY_KIND: str(row[1] or ""),
                proto.ALARM_KEY_SOURCE: str(row[2] or ""),
                proto.ALARM_KEY_TEXT: str(row[3] or ""),
                proto.ALARM_KEY_DEV: (None if row[4] is None
                                      else int(row[4])),
            })
        await self._reply_checked(
            session, request_id, msg_code,
            {
                proto.HIST_KEY_TOTAL: int(window_total),
                proto.LOG_KEY_COUNT: len(payload_rows),
                proto.HIST_KEY_OFFSET: int(offset),
                proto.HIST_KEY_LIMIT: int(limit),
                proto.HIST_KEY_ROWS: payload_rows,
            },
            "本页数据过大（超过单帧上限），请减小每页条数后重试")

    # ==================================================================
    #                      点动手持看门狗
    # ==================================================================
    def _jog_slot(self):
        """取（必要时创建）变频器的看门狗槽位。

        [为什么不只在 build_devices 里建] 设备可被外部注入/热替换（测试装置
        与将来的热插拔都这么用），槽位若只在装配期建，注入设备后点动就说
        停不停——看门狗是安全件，**绝不能因为装配路径不同而失效**。
        """
        watch = self._jog.get("vfd")
        if watch is None:
            watch = JogWatch()
            self._jog["vfd"] = watch
        return watch

    def _jog_touch(self, reverse):
        """收到点动帧：刷新看门狗（首次进入点动态时打日志）。"""
        watch = self._jog_slot()
        if not watch.active:
            # 类别 = ctrl（点动是控制动作；每 200ms 的心跳不重复记，只在
            # 进入点动态这一拍留痕 —— 否则日志页被心跳冲满）。
            self._log_with_store("点动开始（%s）" % ("反转" if reverse else "正转"),
                                 proto.LOG_CAT_CTRL)
        watch.active = True
        watch.reverse = reverse
        watch.last_ms = self._now_ms()

    async def _jog_release(self):
        """点动释放：立刻停机（客户端松手，不等看门狗）。

        [即使记录为"未在点动"也要发停机] 看门狗态与设备真实状态可能不同步
        （服务端重启过、或点动帧丢了最后一帧）。松手是**安全动作**，
        无条件下发停机比按状态位跳过更安全——多发的这一帧代价只是几十毫秒。
        """
        watch = self._jog_slot()
        was_active = watch.active
        watch.active = False
        if self.vfd is None:
            return
        try:
            await self.vfd.send_command(devices.VfdM512.CMD_DECEL_STOP)
        except Exception as exc:                        # noqa: BLE001
            # 类别 = ctrl：控制动作执行失败（与受理/拒绝同一类留痕）。
            self._log_with_store("点动释放停机失败：%s" % exc,
                                 proto.LOG_CAT_CTRL)
            return
        self._log_with_store("点动释放 → 停机%s"
                             % ("" if was_active else "（无活动点动）"),
                             proto.LOG_CAT_CTRL)

    def _jog_watchdog(self, now_ms):
        """超时未收到点动帧 → 自动停机（安全兜底）。

        防「链路断开后设备一直点动」：这是安全网，不是正常路径。
        用 create_task 发起停机（本函数在 tick 里同步调用，不能 await）。
        """
        watch = self._jog.get("vfd")
        if watch is None or not watch.active:
            return
        if now_ms - watch.last_ms <= proto.JOG_TIMEOUT_MS:
            return
        watch.active = False
        idle = now_ms - watch.last_ms
        # 类别 = alarm：点动心跳超时是安全看门狗动作（链路断了设备还在转），
        # 运维必须能在告警时间线上看到这次兜底停机。
        self._log_with_store("点动心跳超时（%dms 无帧）→ 自动停机" % idle,
                             proto.LOG_CAT_ALARM)
        if self.vfd is not None:
            # [强引用 2026-11-16 审计] 事件循环对任务只持弱引用 ⇒ 安全
            # 兜底停机可能被 GC 掉（"点动超时了设备却没停"，且无任何报错）。
            # ⇒ 实例集合持有 + 完成即摘。
            task = asyncio.ensure_future(self._safe_stop())
            tasks = getattr(self, "_jog_stop_tasks", None)
            if tasks is None:
                tasks = self._jog_stop_tasks = set()
            tasks.add(task)
            task.add_done_callback(tasks.discard)

    async def _safe_stop(self):
        try:
            await self.vfd.send_command(devices.VfdM512.CMD_DECEL_STOP)
        except Exception as exc:                        # noqa: BLE001
            # 类别 = alarm：安全兜底动作失败（比"停机成功"严重一档）。
            self._log_with_store("点动超时自动停机失败：%s" % exc,
                                 proto.LOG_CAT_ALARM)
