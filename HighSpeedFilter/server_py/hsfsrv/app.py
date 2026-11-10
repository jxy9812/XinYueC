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
import time

from . import autocontrol
from . import bson
from . import buttons
from . import devices
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
        self._health_next_ms = 0
        #: 自动控制异常限频表：异常类名 -> 上次记日志时刻（单调毫秒）。
        self._auto_err_at = {}

        # ---- 点动看门狗 ----
        self._jog = {}

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
                    name="浊度仪%d" % (idx + 1), log=self._log,
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
            ctl.stop(now, stop_devices=switched, reason="切回手动")
            # 类别 = auto（模式切换，与 C 侧 hsfAppApplyModeToDevices 的
            # hsfAppEventLog("auto", ...) 同口径）。
            self._log_with_store(
                "退出自动模式：自动控制已强制停止%s"
                % ("，设备全停" if switched else ""), proto.LOG_CAT_AUTO)
            # 按钮组运行态立刻同步（灯本身在手动模式下**不写**——灯归人工，
            # 见 buttons.ButtonStation.poll；自动模式下才跟随运行态）。
            ctl._sync_station()
        elif switched:
            # 切到自动 ⇒ 也全停设备（2026-11-10 用户口径「切换模式后要关闭
            # 所有设备」）：自动模式拒掉全部远程控制，控制器又要等人按启动
            # 才跑 —— 设备若还转着，现场既停不掉也看不到谁在驱动它。
            # [不启动控制器] 模式开关不是启动按钮：设备要等"启动"才允许工作。
            ctl.stop(now, stop_devices=True, reason="切到自动")
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
                    self._poll_loop(self.vfd, self.cfg.vfd_poll_ms, "搅拌泵",
                                    "vfd_poll_ms")))
            else:
                self._log("搅拌泵采集已停用（配置 vfdEnabled=false）")
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

        :param category: 运行日志类别（C 端口径 conn/ctrl/alarm/auto/auth；
            缺省 sys = 非上述五类的普通运行日志）。
        """
        self._log_raw(msg)
        self.store.insert_log(msg, category=category)

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
        """历史采样入库。周期从 cfg 现读 —— 远程改采样周期下一拍生效。"""
        while True:
            period_s = max(1, self.cfg.storage.sample_ms) / 1000.0
            await asyncio.sleep(period_s)
            if self.store.db_path is None:
                continue             # 落盘未启用/打开失败：空转，开了自动恢复
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
        """热身门判定：True=可入库；False=启动瞬态（带从未见过有效的空洞）不入库。"""
        warm = getattr(self, "_hist_warm", None)
        if warm is None:
            warm = set()
            self._hist_warm = warm
        need = set()
        for name, keys in self._HIST_WARM_GROUPS:
            if any(row.get(k) is None for k in keys):
                need.add(name)
            else:
                warm.add(name)
        # 还有"从未见过有效值"的组 ⇒ 这一行是启动瞬态，不入库。
        return not (need - warm)

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
        if self.run_mode == self.MODE_AUTO:
            running = getattr(self.auto_controller, "running", None)
            row["mode"] = ("自动·运行中" if running
                           else "自动·未知" if running is None else "自动·停止")
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
                # 多台都有温度时后面的台覆盖前面的（口径=「浊度仪2 的水温」）。
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
        }

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
            return changed, False         # 周期下一拍自然生效，无需重启
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
                enables_changed = False
                for key in keys:
                    _, affects_enable = self._cfg_set_collect_key(
                        key, doc[key])
                    enables_changed = enables_changed or affects_enable
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
            await self._reply(session, request_id, msg_code, True, extra={
                "collect": self._config_collect_doc()})
        elif section == "storage":
            keys = [k for k in doc if k != "section"]
            if not keys:
                await self._reply(session, request_id, msg_code, False,
                                  "载荷里没有配置键")
                return
            try:
                for key in keys:
                    self._cfg_set_storage_key(key, doc[key])
            except ValueError as exc:
                await self._reply(session, request_id, msg_code, False,
                                  str(exc))
                return
            self.cfg.save()
            self.storage_apply()
            self._log("远程配置：落盘节已更新（%s）" % ",".join(keys))
            await self._reply(session, request_id, msg_code, True, extra={
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
                for key in keys:
                    self._cfg_set_login_key(key, doc[key])
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
            await self._reply(session, request_id, msg_code, True, extra={
                "login": self._config_login_doc()})
        else:
            await self._reply(session, request_id, msg_code, False,
                              "未知配置节：%r（collect / storage / login）"
                              % section)

    # ==================================================================
    #                       周期 tick（推送 + 健康 + 看门狗）
    # ==================================================================
    async def __call__(self, now_ms):
        """XLink 服务端的业务 tick 回调（由 xlink.Server 周期驱动）。"""
        await self._publish_telemetry(now_ms)
        self._jog_watchdog(now_ms)
        await self._auto_step(now_ms)
        if now_ms >= self._health_next_ms:
            self._health_next_ms = now_ms + self.HEALTH_TICK_MS
            await self._health_tick(now_ms)

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
        ActionFailed 在控制器已停后才入队）。故在 poll_idle 之后、running
        闸之前统一 take。
        """
        ctl = getattr(self, "auto_controller", None)
        if ctl is None:
            return
        # ---- 每拍：按键受理 + 急停闸（不依赖 running）----
        try:
            ctl.poll_idle(now_ms, self.is_auto())
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)
        # ---- 每拍：抽取在队报警（取走即消费，一条一帧）----
        await self._drain_auto_alarms(ctl)
        if not ctl.running:
            return
        try:
            ctl.step(now_ms)
        except Exception as exc:                        # noqa: BLE001
            self._log_auto_error(exc)

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

    def _log_auto_error(self, exc):
        """自动层异常限频记日志（同一类错误 1 分钟一条）。"""
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
            self._log_with_store("自动控制异常（已隔离，不影响采集/推送）：%s: %s"
                                 % (key, exc), proto.LOG_CAT_AUTO)

    async def _publish_telemetry(self, now_ms):
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
        # ---- 按钮组（操作柱：读按键 → 推状态机 → 写三盏灯）----
        # [放在数字量推送之后] 它读的是 dev.di/dev.do 缓存；数字量那几帧
        # 刚推完，此刻的缓存就是本拍最新状态。灯的写入是异步投递，下一拍
        # 由回读校准，不影响本拍推送出去的数据自洽性。
        if getattr(self, "button_station", None) is not None:
            try:
                # [自动模式才写灯 2026-11-10] 手动模式下灯归人工：自动逻辑
                # 每拍写灯会把人工点的灯覆盖回去（现场「手动模式无法单独
                # 控制按钮灯」的根因）。详见 buttons.ButtonStation.poll。
                self.button_station.poll(now_ms, self.is_auto())
            except Exception as exc:                    # noqa: BLE001
                # 按钮组异常不能把整个 tick 带死（自动/手动都要继续跑）
                self._log("按钮组：本拍处理失败 %s: %s"
                          % (type(exc).__name__, exc))
        # ---- 电磁阀（6 路一帧）----
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
        """
        if dev is None or not dev.online:
            return None
        doc = {proto.KEY_VALID_MASK: dev.valid_mask}
        for bit, key, kind in proto.VFD_TELEMETRY_FIELDS:
            val = dev.field_value(bit, kind)
            if val is None:
                continue
            doc[key] = val
        doc[proto.KEY_SIDE] = 0
        try:
            return bson.encode(doc)
        except ValueError as exc:
            self._log("变频器遥测编码失败：%s" % exc)
            return None

    def encode_temphum(self, th):
        """温湿度快照：未收到数据的通道**不写值键**（客户端据此区分 0 与无数据）。"""
        doc = {proto.KEY_SIDE: 0}
        if th is not None:
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
        """
        doc = {proto.KEY_COUNT: len(self.turbidity)}
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

    def encode_analog(self, ai):
        """模拟量快照：{"n","v","r0..rN","u0..uN"} + 换算通道的 "e*" 键。

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
        """
        doc = {}
        vmask = 0
        for i in range(ai.channels):
            if ai.valid[i]:
                vmask |= 1 << i
            doc["r%d" % i] = ai.raw[i]
            ma = ai.ma(i)
            doc["u%d" % i] = int(round(ma * 1000.0))
            scale = self.cfg.analog.scales.get(i)
            if scale is not None and scale.configured and ai.valid[i]:
                value, note = scale.engineering(ma)
                if value is not None:
                    doc["e%d" % i] = int(round(value * 1000.0))
                    doc["eu%d" % i] = scale.unit
                    if scale.name:
                        doc["en%d" % i] = scale.name
                    if note:
                        doc["ew%d" % i] = note
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
        if state == buttons.ButtonState.ESTOP:
            text = "急停无信号（按下或断线）：禁止启动"
            health = proto.HEALTH_FAULT
        elif state == buttons.ButtonState.RUNNING:
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
                entries.append(HealthEntry("浊度仪%d" % (idx + 1),
                                           proto.KIND_WATER, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "已停用",
                                           self._addr_rtu(unit, "b")))
                continue
            health, err, online, text = tm.health(
                now_ms, self.cfg.turbidity_stale_ms)
            entries.append(HealthEntry("浊度仪%d" % (idx + 1),
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
            return
        if keys == self._health_last:
            return
        self._health_seq += 1
        self._health_last = keys
        payload = self.encode_health()
        await self.server.publish(proto.TOPIC_DEVICE_HEALTH, payload)
        self._log("设备健康变化（seq=%d，%d 台）" % (self._health_seq, len(entries)))

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
            return self.encode_analog(self.analog), proto.DATATYPE_BSON
        if topic == proto.TOPIC_DIGITAL_STATUS:
            if not self.digital:
                return None, None
            return self.encode_digital(self.digital[0], 0), proto.DATATYPE_BSON
        if topic == proto.TOPIC_VALVE_STATUS:
            if self.valve_bank is None:
                return None, None
            return self.encode_valves(), proto.DATATYPE_BSON
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
        """认证通过后：补发健康快照（首连时可能还在认证中，发不出去）。"""
        await self.on_session_added(session)

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

    # ---- 变频器控制 ----
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
        await self._reply(session, request_id, msg_code, ok, err or None)

    # ---- 快照 ----
    async def _h_vfd_snapshot(self, session, request_id, msg_code, payload):
        if self.vfd is None:
            await self._reply(session, request_id, msg_code, False, "变频器未就绪")
            return
        # [请求即最新] 现读一次；失败不拦（返回上次值 + valid 位如实反映）
        await devices.read_fresh(self.vfd, self._now_ms())
        doc = {proto.KEY_VALID_MASK: self.vfd.valid_mask, proto.KEY_SIDE: 0}
        for bit, key, kind in proto.VFD_TELEMETRY_FIELDS:
            val = self.vfd.field_value(bit, kind)
            if val is not None:
                doc[key] = val
        # 额定点随快照下发：客户端用它把"转速模式"的输入量程与换算对齐，
        # 不必在客户端再写死一份 1440/50（两份必然漂移）。
        doc["ratedRpm"] = float(self.vfd.rated_rpm)
        doc["ratedHz"] = float(self.vfd.rated_hz)
        await session.send_response(request_id, msg_code, bson.encode(doc))

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
                                    self.encode_analog(self.analog))

    async def _h_temphum_snapshot(self, session, request_id, msg_code, payload):
        # [请求即最新] 现读一次（与变频器共享同一 485，节流避免抢总线）
        if self.temphum is not None:
            await devices.read_fresh(self.temphum, self._now_ms())
        await session.send_response(request_id, msg_code,
                                    self.encode_temphum(self.temphum))

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
                                         self.is_auto_running()})
            return
        if not permissions.can(session, proto.PERM_MODE_SWITCH):
            await self._reply(session, request_id, msg_code, False,
                              permissions.denied_text(proto.PERM_MODE_SWITCH))
            return
        if not self.set_run_mode(new_mode):
            await self._reply(session, request_id, msg_code, False, "非法模式值")
            return
        await self._reply(session, request_id, msg_code, True, None,
                          extra={proto.KEY_MODE: self.run_mode,
                                 proto.KEY_AUTO: self.is_auto(),
                                 # 切换后立刻回显控制器真实状态：这里是
                                 # apply_mode_to_devices() 刚跑完的时刻，所以
                                 # 客户端拿到的 running 是**切换之后的**值，
                                 # 不必等下一帧推送。
                                 proto.KEY_AUTO_RUNNING:
                                     self.is_auto_running()})

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
                      proto.AUTO_OP_ESTOP, proto.AUTO_OP_RESET):
            await self._reply(session, request_id, msg_code, False,
                              "非法 op 操作字（1启动/2停止/3急停/4复位）")
            return
        # 3) 启动的条件判定（与现场启动按钮**同一套判据**，见
        #    AutoController._start_preconditions —— 两处条件必须同源，
        #    否则"现场能启动、远程不能启动"这种分歧极难查）
        if op == proto.AUTO_OP_START:
            ok, why = ctl.start_preconditions(self.is_auto())
            if not ok:
                await self._reply(
                    session, request_id, msg_code, False,
                    ("当前是手动模式：请先切到自动模式，再启动自动运行"
                     if not self.is_auto() else why),
                    extra={proto.KEY_MODE: self.run_mode,
                           proto.KEY_AUTO: self.is_auto(),
                           proto.KEY_AUTO_RUNNING: self.is_auto_running()})
                return
        # 4) 执行（幂等）
        now = self._now_ms()
        if op == proto.AUTO_OP_START:
            ctl.start(now)
        elif op == proto.AUTO_OP_STOP:
            # 人按"停止自动运行"⇒ 停控制器**并停全部设备**：
            # 只停控制器会让设备继续按上一条命令动着，现场看到的是
            # "我按了停止，泵还在转"。
            ctl.stop(now, stop_devices=True, reason="远程停止自动运行")
        elif op == proto.AUTO_OP_ESTOP:
            ctl.e_stop("远程急停")
        else:
            ctl.reset()
        await self._reply(session, request_id, msg_code, True, None,
                          extra={proto.KEY_MODE: self.run_mode,
                                 proto.KEY_AUTO: self.is_auto(),
                                 proto.KEY_AUTO_RUNNING:
                                     self.is_auto_running()})

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
        if doc.get(proto.HIST_KEY_EXPORT) and not permissions.can(
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

        rows, window_total = self.store.query_history_page(
            offset=offset, limit=limit, lo_ms=lo_ts, hi_ms=effective_hi,
            kw=kw, kw_cols=kw_cols)
        # [时间边界只认有效墙钟] history_bounds() 过滤掉 2020 年前的坏 ts
        # （旧版本误写 monotonic 的 1970 年行）。直接 MIN(ts) 会把"最前
        # 一条数据的时间"报成 1970-01-01 —— 客户端拿它当默认起始时间
        # 等于没筛。
        earliest_ts, latest_ts = self.store.history_bounds()

        await self._reply(session, request_id, msg_code, True, extra={
            proto.HIST_KEY_TOTAL: int(window_total),
            proto.HIST_KEY_OFFSET: int(offset),
            proto.HIST_KEY_LIMIT: int(limit),
            proto.HIST_KEY_COLS: list(self.store.HISTORY_COLUMNS),
            # sqlite3.Row / tuple → list：BSON 只认 list/tuple，tuple 可以
            # 但 list 在客户端拼装更省事（直接当数组用）。
            proto.HIST_KEY_ROWS: [list(r) for r in rows],
            proto.HIST_KEY_DB_ROWS: int(self.store.row_count("history")),
            proto.HIST_KEY_LATEST_TS: latest_ts,
            proto.HIST_KEY_EARLIEST_TS: earliest_ts,
            proto.HIST_KEY_WINDOW_START_TS: int(lo_ts or 0),
            proto.HIST_KEY_WINDOW_END_TS: int(window_end),
        })

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
        rows, window_total = self.store.query_logs_page(
            offset=offset, limit=limit, before_ms=before_ts, lo_ms=lo_ts,
            hi_ms=hi_ts, kw=kw or None, category=category or None)
        payload_rows = []
        for row in rows:
            payload_rows.append({
                proto.LOG_KEY_TS: int(row[0] or 0),
                proto.LOG_KEY_CATEGORY: str(row[1] or ""),
                proto.LOG_KEY_TEXT: str(row[2] or ""),
            })
        await self._reply(session, request_id, msg_code, True, extra={
            proto.HIST_KEY_TOTAL: int(window_total),
            # [count 2026-11-09 补契约] C 端 0x0119 应答含 count（页内行数，
            # HSF_KEY_LOG_COUNT，见 HsfServerApp.c 的「count 补契约」段）；
            # 本端此前只回 total/offset/limit/rows ⇒ 两端应答键集不一致，
            # 观测对账（total vs 本页行数）在 Python 服务端上做不了。
            # 客户端实际用 len(rows)，此键纯供对账，故按本页实际行数回。
            proto.LOG_KEY_COUNT: len(payload_rows),
            proto.HIST_KEY_OFFSET: int(offset),
            proto.HIST_KEY_LIMIT: int(limit),
            proto.HIST_KEY_ROWS: payload_rows,
        })

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
            asyncio.ensure_future(self._safe_stop())

    async def _safe_stop(self):
        try:
            await self.vfd.send_command(devices.VfdM512.CMD_DECEL_STOP)
        except Exception as exc:                        # noqa: BLE001
            # 类别 = alarm：安全兜底动作失败（比"停机成功"严重一档）。
            self._log_with_store("点动超时自动停机失败：%s" % exc,
                                 proto.LOG_CAT_ALARM)
