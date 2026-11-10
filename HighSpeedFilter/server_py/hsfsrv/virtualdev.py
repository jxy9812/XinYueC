# -*- coding: utf-8 -*-
"""虚拟 Modbus 设备层：**只替换设备，业务层用真实实现**。

================================================================================
                            这个模块解决什么
================================================================================

真实链路：

    设备类(devices.py) ──> master(modbus.py) ──> 串口/TCP ──> 真实设备

本模块把最后两段换掉：

    真实设备类(devices.py，**原封不动**) ──> VirtualMaster ──> 内存寄存器模型

于是：

    · **devices.py 一行不改**，VfdM512 / DigitalIo / AnalogInput / TempHum
      的真实实现（换算、状态字解码、故障表、写命令字…）全部照跑；
    · **app.py 一行不改**，命令路由、控制闸门、点动看门狗、阀 20s 定时器、
      告警去重、健康推送全部照跑；
    · 只把「电信号」换成内存里的寄存器。

这与「模拟整个服务端」是两码事：那种做法下业务层是假的，验证不了闸门与
看门狗；本模块下业务层是真的，**只有设备物理层是假的**。

================================================================================
                              怎么用
================================================================================

在 `config` 里打开 `sim` 段（或用 `--sim` 命令行开关），`app.build_devices`
就会用 VirtualMaster 代替真实 master。真实设备**完全不碰**（不打开串口、
不建立 TCP），所以设备拆了也能跑，接回来也不用改任何代码。

    python -m hsfsrv --sim                  # 虚拟设备 + 真实业务层（推荐）
    python -m hsfsrv --sim --sim-flaky 0.1  # 顺带模拟数字量模块抖动

================================================================================
                            寄存器模型（够用即可）
================================================================================

只模拟服务端**实际会读写的寄存器**，不是完整 Modbus 从站：

  M512 变频器（FC03 读 / FC06 写）
      0x1000 设定频率（±10000 = ±100%；int16 补码）
      0x1001 输出频率（0.01Hz）
      0x1002 母线电压（0.1V）      0x1003 输出电压（1V）
      0x1004 输出电流（0.01A）     0x1005 输出功率（0.1kW）
      0x1006 输出转矩（0.1%）
      0x2000 命令字（写）          0x3000 运行状态字
      0x8000 故障码

  温湿度（FC04 读 0x0000 ×2，各 ×0.1）
  模拟量（FC04 读 0x0000 ×N，毫伏口径）
  数字量（FC01 读线圈 / FC02 读离散输入 / FC05 写单点）

行为约定（照现场实测）：

  · 数字量模块**会间歇性空应答**（无间隔连发 10 次里 1~2 次 0 字节）
    → 用 `--flaky N` 打开，默认关闭（免得日常调试被随机失败干扰）
  · 数字量 DO 变位后**行程开关跟着变**（开到位/关到位互斥）
  · 变频器收到启停命令后输出频率**渐升到设定值**（不是瞬跳），
    这样"动作中"这类中间态也能在界面上看到
"""

import math
import random
import struct
import time


# ============================================================================
#                        虚拟 Modbus 主站
# ============================================================================

class VirtualMaster(object):
    """假的 master：接口与 modbus.TcpMaster / RtuMaster 一致。

    设备类只用到三个方法（`read` / `write_single` / `close`），故实现这三个
    即可让**真实的设备驱动**跑起来。

    :param model: 该设备对应的寄存器模型（见下方各类）
    :param flaky_rate: 模拟"无应答"的概率（0~1）。数字量模块实测会间歇空
                       应答，默认 0 以免干扰日常调试。
    """

    def __init__(self, model, name="", flaky_rate=0.0, log=None):
        self.model = model
        self.name = name
        self.flaky_rate = float(flaky_rate)
        self._log = log or (lambda m: None)
        self._counter = 0
        self.closed = False
        # [墙钟节拍] 给 wants_poll_clock 模型（变频器）按设备 poll 的时间
        # 补推进用；见 advance_ms()。
        self._poll_clock_last_ms = None
        self._poll_clock_debt_ms = 0

    async def read(self, function, address, count, unit=None):
        """读寄存器/线圈/离散输入。

        :returns: 值列表（FC03/04 = 每个寄存器的 uint16；FC01/02 = 位图列表）
        :raises modbus.ModbusError: 模型拒绝时（越界/不支持的域）
        """
        from . import modbus
        self._tick()
        if self.closed:
            raise modbus.ModbusError("%s: 连接已关闭" % self.name)
        try:
            return self.model.read(function, address, count)
        except KeyError as exc:
            raise modbus.ModbusError("%s: 读越界 addr=0x%04X (%s)"
                                     % (self.name, address, exc))

    async def write_single(self, function, address, value, unit=None):
        from . import modbus
        self._tick()
        if self.closed:
            raise modbus.ModbusError("%s: 连接已关闭" % self.name)
        self.model.write_single(function, address, value)
        return True

    async def close(self):
        self.closed = True

    def advance_ms(self, now_ms):
        """设备层每拍（VfdM512.poll 进门时）调用：按墙钟间隔补推进。

        [为什么变频器不能靠事务驱动] 一轮 poll 发 N 次总线事务
        （READ_GROUPS 多段 + 状态 + 故障），挂事务上的推进会快 N 倍——
        ACC/DEC 的"秒"就不再是秒（实测 2026-11-06：换向减速跑到 25Hz/s，
        恰为 0.5Hz/拍 × 每 poll 5 次事务）。C 侧同款问题的修法是独立
        100ms 心跳拍（XVirtualModelVfd_tick ← HsfServerIoSim_tick），
        这里的等价物是按设备 poll 传入的 now_ms 折算拍数。只对声明
        `wants_poll_clock` 的模型生效，其余模型维持事务驱动口径不变。
        """
        last = self._poll_clock_last_ms
        self._poll_clock_last_ms = now_ms
        if last is None or now_ms is None:
            return
        self._poll_clock_debt_ms += max(0, now_ms - last)
        while self._poll_clock_debt_ms >= 100:
            self._poll_clock_debt_ms -= 100
            self.model.on_access()

    # ---- 内部 ----
    def _tick(self):
        """每帧一次：推进模型的时间相关状态（频率爬坡等）。

        [为什么要"推进"] 虚拟设备没有独立线程，靠"被访问"来推进时间。
        poll 每 100ms 读一次，正好充当驱动器。
        """
        self._counter += 1
        if self.flaky_rate > 0.0:
            # 用计数做伪随机：每 N 帧故意失败一次（不依赖 random，可复现）
            period = max(2, int(round(1.0 / self.flaky_rate)))
            if self._counter % period == 0:
                from . import modbus
                raise modbus.ModbusError("%s: 虚拟空应答（模拟模块抖动）"
                                         % self.name)
        # [wants_poll_clock 模型不在这里推进] 事务节拍 ≠ 墙钟节拍（一轮
        # poll 发 N 次事务），变频器这类要按真实时间走 ACC/DEC 的模型改由
        # 设备 poll 的 advance_ms() 驱动，见 advance_ms 注释。
        if not getattr(self.model, "wants_poll_clock", False):
            self.model.on_access()


class VirtualMultiMaster(object):
    """一条"虚拟总线"上挂多个从站：按 unit 号分派到各自的模型。

    对应真实链路里「一台 RtuMaster 服务同总线多个从站」的结构
    （变频器站2 + 温湿度站4 共用 ttyS3）。设备调用形如
    `read(fc, addr, cnt, unit=2)`，这里按 unit 找模型。

    [与 VirtualMaster 的区别] 单模型版不需要 unit（一对一）；总线版必须
    按 unit 分派，否则变频器的寄存器会被温湿度读到。

    [ttyS4 保真 2026-11-10] 本类是路 B 的「总线 + 全部从站」，因此也承担
    真实总线的三条时序行为（与 C 侧 XModbusVirtualClient 同款）：
      · 事务串行 + 站间最小间隙 `min_tx_gap_ms`（对齐 RtuMaster 同名参数：
        站 5 长帧后立刻问站 1 是坏相位，200ms 稳定——cfg.serial_b 传入）；
      · 从站**应答时延**（set_unit_reply_latency：站 1 37~43ms、站 2
        31~33ms、站 5 68~69ms，应答路径内 await asyncio.sleep，不阻塞
        事件循环——真实仪表的应答从来不是零时延）；
      · 站 1 的「长帧后恢复期」（set_recovery_gate：站 5 的 49 字节长帧
        收口后 125ms 内站 1 对任何请求**无应答**（ModbusTimeout）——
        主站的站间间隙正是为规避它存在的，模拟它才能长稳锻炼间隙逻辑）。
    三项缺省全关（时延 0 / 无恢复期），既有用法一字不改。
    """

    #: 「长帧」读的寄存器数下限：站 5 一帧读 22 寄存器 = 44 字节数据 +
    #: 5 字节帧头尾 = 49 字节（现场唯一的长帧）；浊度 2~4 寄存器差一个量级。
    LONG_FRAME_REGS = 20

    def __init__(self, models, name="", flaky_rate=0.0, log=None,
                 min_tx_gap_ms=0):
        """models: {unit_id: 模型对象}"""
        self.models = dict(models)
        self.name = name
        self.flaky_rate = float(flaky_rate)
        #: [站间最小间隙] 相邻两次事务的最小总线静默（毫秒；0=不加）。
        #: 语义与 modbus.RtuMaster.min_tx_gap_ms 逐字对齐——sim 下主站就是
        #: 本类，间隙逻辑只有在这里也生效才能被端到端锻炼。
        self.min_tx_gap_ms = max(0, int(min_tx_gap_ms))
        self._log = log or (lambda m: None)
        self._counter = 0
        self.closed = False
        # [墙钟节拍] 见 VirtualMaster.advance_ms 同名说明。
        self._poll_clock_last_ms = None
        self._poll_clock_debt_ms = 0
        # ---- ttyS4 保真状态 ----
        #: {unit: (lo_ms, hi_ms)} 应答时延区间（缺省空 = 全零时延）。
        self._unit_latency = {}
        #: (长帧站, 受影响站, 窗口ms)（window<=0 = 闸关）。
        self._recovery = None
        #: 最近一次长帧应答收口时刻（monotonic 秒；None = 还没收口过）。
        self._long_frame_end = None
        #: 事务串行锁（懒建：asyncio.Lock 必须在有事件循环的语境里创建，
        #: 服务端在 asyncio.run 里才用到读路径，故不能在 __init__ 建）。
        self._tx_lock = None
        #: 上一次事务收口时刻（loop.time() 秒；配合 min_tx_gap_ms）。
        self._last_tx_end = 0.0

    # ---- ttyS4 保真旋钮 ----
    def set_unit_reply_latency(self, unit, lo_ms, hi_ms):
        """设某从站的应答时延区间（毫秒；应答路径内 sleep）。

        [为什么放在这里] 这是从站的行为（仪表算完才回帧），不是主站的
        节流—— await 在从站应答路径上，主站等待语义与真串口一致。
        """
        lo = max(0, int(lo_ms))
        hi = max(lo, int(hi_ms))
        self._unit_latency[unit] = (lo, hi)

    def set_recovery_gate(self, long_frame_unit, blocked_unit, window_ms):
        """建「长帧后恢复期」闸：长帧站收口后窗口内受影响站无应答。

        :param long_frame_unit: 发 49 字节级长帧的从站（现场=站 5 流量计）。
        :param blocked_unit: 窗口内对任何请求都无应答的从站（现场=站 1）。
        :param window_ms: 恢复期窗口毫秒（实测 ≥100~125ms；0=关闭闸）。
        """
        window = max(0, int(window_ms))
        if window <= 0 or long_frame_unit is None or blocked_unit is None:
            self._recovery = None
            self._long_frame_end = None
            return
        self._recovery = (long_frame_unit, blocked_unit, window)

    # ---- 事务内部节拍 ----
    async def _begin_tx(self):
        """事务串行 + 站间最小间隙（读/写共用，对齐 RtuMaster 口径）。"""
        if self._tx_lock is None:
            import asyncio
            self._tx_lock = asyncio.Lock()
        import asyncio
        await self._tx_lock.acquire()
        if self.min_tx_gap_ms > 0 and self._last_tx_end > 0:
            loop = asyncio.get_event_loop()
            earliest = self._last_tx_end + self.min_tx_gap_ms / 1000.0
            delay = earliest - loop.time()
            if delay > 0:
                await asyncio.sleep(delay)

    def _end_tx(self):
        """事务收口：记时刻（下一事务的间隙从这一刻起算）。"""
        import asyncio
        self._last_tx_end = asyncio.get_event_loop().time()
        self._tx_lock.release()

    def _reject_if_in_recovery(self, unit):
        """站 1 恢复期：长帧收口后窗口内的请求 → 无应答（ModbusTimeout）。"""
        from . import modbus
        if not self._recovery or self._long_frame_end is None:
            return
        long_unit, blocked_unit, window = self._recovery
        if unit != blocked_unit:
            return
        since = (time.monotonic() - self._long_frame_end) * 1000.0
        if 0.0 <= since < window:
            raise modbus.ModbusTimeout(
                "%s: 站 %s 恢复期无应答（站 %s 长帧后 %.0fms < %dms）"
                % (self.name, blocked_unit, long_unit, since, window))

    async def _sleep_unit_latency(self, unit):
        """从站应答时延：按站号在区间内随机取值后 sleep（不阻塞事件循环）。"""
        import asyncio
        rng = self._unit_latency.get(unit)
        if not rng:
            return
        lo, hi = rng
        if hi <= 0:
            return
        await asyncio.sleep(random.uniform(lo, hi) / 1000.0)

    def _mark_long_frame(self, unit, count):
        """长帧站的读收口：记时刻（恢复期窗口的起点）。"""
        if self._recovery and unit == self._recovery[0] \
                and count >= self.LONG_FRAME_REGS:
            self._long_frame_end = time.monotonic()

    def advance_ms(self, now_ms):
        """设备层每拍调用：按墙钟间隔给 `wants_poll_clock` 模型补推进。

        与 VirtualMaster.advance_ms 同口径（总线版——`--sim` 用的是这个，
        变频器 + 温湿度挂同一条虚拟 485 总线上）。
        """
        last = self._poll_clock_last_ms
        self._poll_clock_last_ms = now_ms
        if last is None or now_ms is None:
            return
        self._poll_clock_debt_ms += max(0, now_ms - last)
        while self._poll_clock_debt_ms >= 100:
            self._poll_clock_debt_ms -= 100
            for model in self.models.values():
                if getattr(model, "wants_poll_clock", False):
                    model.on_access()

    def _model(self, unit):
        if unit is None:
            # 未指定 unit：取第一个（本虚拟总线只有单从站时无歧义）
            if len(self.models) == 1:
                return next(iter(self.models.values()))
            raise KeyError("未指定 unit，且总线挂有多台从站（%s）"
                           % sorted(self.models))
        model = self.models.get(unit)
        if model is None:
            raise KeyError("总线无 unit=%s 的从站（现有 %s）"
                           % (unit, sorted(self.models)))
        return model

    async def read(self, function, address, count, unit=None):
        from . import modbus
        self._tick()
        if self.closed:
            raise modbus.ModbusError("%s: 连接已关闭" % self.name)
        await self._begin_tx()
        try:
            # [恢复期先于时延] 真实时序：恢复期里的站 1 根本不应答（主站
            # 等到超时）；出了恢复期仪表按自己的节奏延迟后回帧。
            self._reject_if_in_recovery(unit)
            await self._sleep_unit_latency(unit)
            try:
                vals = self._model(unit).read(function, address, count)
            except KeyError as exc:
                raise modbus.ModbusError("%s: 读失败 unit=%s addr=0x%04X (%s)"
                                         % (self.name, unit, address, exc))
            self._mark_long_frame(unit, count)
            return vals
        finally:
            self._end_tx()

    async def write_single(self, function, address, value, unit=None):
        from . import modbus
        self._tick()
        if self.closed:
            raise modbus.ModbusError("%s: 连接已关闭" % self.name)
        await self._begin_tx()
        try:
            await self._sleep_unit_latency(unit)
            try:
                self._model(unit).write_single(function, address, value)
            except KeyError as exc:
                raise modbus.ModbusError("%s: 写失败 unit=%s addr=0x%04X (%s)"
                                         % (self.name, unit, address, exc))
        finally:
            self._end_tx()
        return True

    async def close(self):
        self.closed = True

    def _tick(self):
        self._counter += 1
        if self.flaky_rate > 0.0:
            period = max(2, int(round(1.0 / self.flaky_rate)))
            if self._counter % period == 0:
                from . import modbus
                raise modbus.ModbusError("%s: 虚拟空应答" % self.name)
        # [wants_poll_clock 模型不在这里推进] 见 VirtualMaster._tick 注释；
        # 由 advance_ms()（设备 poll 墙钟）驱动。
        for model in self.models.values():
            if not getattr(model, "wants_poll_clock", False):
                model.on_access()


# ============================================================================
#                        M512 变频器寄存器模型
# ============================================================================

class VirtualVfd(object):
    """M512/M513 变频器的寄存器模型。

    [为什么要有爬坡] 真机启停不是瞬时的（加速时间 P0.x）。若虚拟设备瞬间
    跳到目标频率，界面上永远看不到"加速中"；也验证不了"给定变了但还没跟上"
    这类显示。爬坡斜率按 ACC/DEC 加减速时间参数推导（见 _ramp_hz_per_tick）。

    [wants_poll_clock] 声明本模型按**设备 poll 的墙钟**推进（master 的
    advance_ms()），不随总线事务走——事务节拍是轮询段数的 N 倍，会把
    ACC/DEC 的"秒"缩水成 1/N 秒（实锅 2026-11-06，与 C 侧
    XVirtualModelVfd_tick ← HsfServerIoSim 100ms 心跳拍同口径）。
    """

    #: 声明走墙钟节拍（master 的事务 _tick 跳过本模型）。
    wants_poll_clock = True

    CMD = {
        1: "fwd_run", 2: "rev_run", 3: "jog_fwd", 4: "jog_rev",
        5: "free_stop", 6: "decel_stop", 7: "fault_reset",
    }

    #: 加速/减速斜率兜底（Hz 每拍）。仅在 ACC/DEC 参数未配置（≤0）时使用。
    #: 常规斜率按 ACC/DEC 满量程时间推导，见 _ramp_hz_per_tick()。
    RAMP_HZ_PER_TICK = 2.5

    def __init__(self, max_hz=50.0, log=None):
        self.max_hz = float(max_hz)
        self._log = log or (lambda m: None)
        # 设定频率（0x1000 存的是相对最大频率的百分比 int16）
        self.set_percent = 5000          # 50% = 25Hz
        self.out_hz = 0.0
        self.target_hz = 0.0
        self.running = False
        self.reverse = False
        self.jogging = False
        #: 是否处于「减速停机」斜坡中（见 _command / on_access）。
        #: 为 True 时状态字仍报"运行"，输出频率按斜率降到 0 才转"停机"。
        self.decelerating = False
        #: 运行中换向待决：None=无；True/False=过零后要翻去的目标方向。
        #: [换向 = 减速过零再反向加速，对齐真机] 运行中收到反方向命令字
        #: （0x2000 写 1↔2）不是瞬间翻方向位——先按减速时间把输出降到 0
        #: （这段状态字仍报**原**方向），过零那一刻才翻方向、再按加速时间
        #: 向新方向爬到给定。与 C 侧 XVirtualModelVfd.m_revPending 同口径。
        self._rev_pending = None
        #: 加速/减速时间（0xF011/0xF012，0.1s 定点，满量程 0↔最大频率）。
        #: 缺省 100=10.0s（真机 10s 减速口径，C 侧 create 同值）。
        self.acc_tenths = 100
        self.dec_tenths = 100
        self.fault_code = 0
        #: 母线电压（真机稳定在 553V 左右）
        self.bus_v = 553.2

    # ---- 斜率与给定 ----
    def _full_range_hz(self):
        """满量程频率（ACC/DEC 时间的基准，= 最大频率）。"""
        return self.max_hz if self.max_hz > 0 else 50.0

    def _ramp_hz_per_tick(self, decel):
        """每拍斜率（Hz/拍）：满量程时间口径 = 最大频率 / ACC或DEC秒 / 10。

        拍 = 100ms ⇒ 每秒 10 拍。缺省 ACC/DEC=10.0s、50Hz ⇒ 0.5Hz/拍
        （5Hz/s）：12Hz 换向全程约 5s，"先慢慢停、再反向加速"肉眼可见。
        参数 ≤0（显式写 0 未配置）时回退 RAMP_HZ_PER_TICK 兜底，与 C 侧
        vfdRampHzPerTick 同口径。
        """
        sec = (self.dec_tenths if decel else self.acc_tenths) / 10.0
        if sec <= 0.0:
            return self.RAMP_HZ_PER_TICK
        return self._full_range_hz() / sec / 10.0

    # ---- 寄存器读写 ----
    def read(self, function, address, count):
        if function != 3:
            raise KeyError("变频器只支持 FC03，收到 FC%02d" % function)
        vals = []
        for i in range(count):
            vals.append(self._reg(address + i))
        return vals

    def write_single(self, function, address, value):
        if function != 6:
            raise KeyError("变频器只支持 FC06，收到 FC%02d" % function)
        if address == 0x1000:
            # int16 补码百分比
            pct = value if value < 0x8000 else value - 0x10000
            self.set_percent = max(-10000, min(10000, pct))
            # [停机时写给定不能产生输出] 给定值是"下次启动用多少"，
            # 写它只是**记住**目标；真正的输出由命令字（0x2000）决定。
            #
            # 原实现无条件 `target_hz = 给定`，于是：
            # 停机状态下把给定从 25Hz 改成 30Hz（或先设转速再启动之前）
            # ⇒ 输出频率从 0 爬到 30Hz ⇒ 界面显示"停机 + 输出 30Hz"，
            # 甚至转速显示 720rpm —— 停机怎么会有转速？
            # 也就是说"没启动时点下发"会凭空产生一段假运行。
            #
            # 正确语义：target_hz 只在**运行中**才跟随给定；
            # 停机时 target 保持 0（输出维持 0），给定只记在 set_percent。
            # [换向减速中给定只记忆不动目标] 服务端处理带给定的 START 是
            # "先写 0x1000 再写 0x2000"两步；两步之间一拍推进可能插入——
            # 换向待决期间目标保持 0，过零翻向时按**新**给定加速（与 C 侧
            # vfdWriteSetPct 同口径，防"正在减速却先弹高半拍"的毛刺）。
            if self.running and self._rev_pending is None:
                self.target_hz = self.set_percent / 10000.0 * self.max_hz
        elif address == 0x2000:
            self._command(value)
        elif address == 0xF011:                 # P0.17 加速时间（0.1s）
            self.acc_tenths = value & 0xFFFF
        elif address == 0xF012:                 # P0.18 减速时间（0.1s）
            self.dec_tenths = value & 0xFFFF
        else:
            raise KeyError("不可写地址 0x%04X" % address)

    def _command(self, code):
        if code in (1, 2):                    # 正转/反转运行
            rev = (code == 2)
            if self.running and rev != self.reverse:
                # [运行中换向 = 先减速过零再反向加速] 对齐真机：不瞬间翻
                # 方向位，先按减速时间把输出降向 0（状态字仍报原方向），
                # 过零那一刻 on_access 才翻向、按加速时间向新方向爬到给定。
                # 减速途中再收到同向命令（含翻回原方向）走下面分支＝取消换向。
                self._rev_pending = rev
                self.target_hz = 0.0
                self.decelerating = True
            else:
                self._rev_pending = None
                self.running = True
                self.reverse = rev
                self.decelerating = False
                self.target_hz = self.set_percent / 10000.0 * self.max_hz
            self.jogging = False
        elif code in (3, 4):                  # 点动
            # 点动接管：取消换向待决，方向/目标按点动立即生效（点动是
            # 按住即走的短促动作，沿用既有瞬时口径）。
            self._rev_pending = None
            self.running = True
            self.reverse = (code == 4)
            self.jogging = True
            self.decelerating = False
            self.target_hz = self.set_percent / 10000.0 * self.max_hz
        elif code == 6:                       # **减速停机**：斜坡降到 0
            # [为什么不立刻 running=False] 真机按减速时间降到 0 才停；立刻
            # 置 False 会让状态字马上变"停机"、输出却还有几十 Hz，界面上
            # 看不到"正在停机"（用户 2026-11-06 明确要看这个过程）。
            # 这里把目标设 0，由 on_access 的斜坡降下来、降到 0 再置停机。
            # [停止覆盖换向] 用户点了停止就是真停：清换向待决，过零走停机。
            self._rev_pending = None
            self.target_hz = 0.0
            self.jogging = False
            self.decelerating = True
        elif code == 5:                       # **自由停车**：立刻断输出
            # 真机自由停车是切断输出（不是斜坡），故这一步必须瞬断。
            self._rev_pending = None
            self.running = False
            self.jogging = False
            self.decelerating = False
            self.target_hz = 0.0
            self.out_hz = 0.0
        elif code == 7:                       # 故障复位
            self.fault_code = 0
        else:
            self._log("虚拟变频器：未知命令字 %d（忽略）" % code)

    def _reg(self, addr):
        """按地址返回寄存器值；**未建模的地址返回 0**。

        [为什么不是抛异常] 真实变频器对"存在但本服务端不关心"的寄存器
        （0x1009 AI1、0x1010 PID 设定、0x1019 线速度…）会正常回 0。
        设备层 `READ_GROUPS` 一次读 0x1000 连续 9 个、0x1009 连续 3 个……
        若这里对未建模地址抛 KeyError，就会把"读成功但值为 0"变成
        "整拍读失败"，设备永远离线 —— 与真机行为完全不符。
        只对**型号不该有**的地址（远超合理范围）才抛。
        """
        if addr == 0x1000:
            return self.set_percent & 0xFFFF
        if addr == 0x1001:
            return int(round(self.out_hz * 100.0)) & 0xFFFF
        if addr == 0x1002:
            return int(round(self.bus_v * 10.0)) & 0xFFFF
        if addr == 0x1003:
            # 输出电压：大致与频率成正比（0Hz → 0V，50Hz → 380V）
            ratio = self.out_hz / self.max_hz if self.max_hz > 0 else 0.0
            return int(round(ratio * 380.0)) & 0xFFFF
        if addr == 0x1004:
            # 输出电流：转起来就有负载（0.01A）
            amps = 3.2 * (self.out_hz / self.max_hz) if self.out_hz > 0.1 else 0.0
            return int(round(amps * 100.0)) & 0xFFFF
        if addr == 0x1005:
            # 输出功率（0.1kW）
            ratio = self.out_hz / self.max_hz if self.max_hz > 0 else 0.0
            return int(round(ratio * 15.0 * 10.0)) & 0xFFFF
        if addr == 0x1006:
            # 输出转矩（0.1%）：空载轻载
            return int(round(42.0 * 10.0)) & 0xFFFF
        if addr == 0x3000:
            # 运行状态字：1=正转 2=反转 3=停机
            if not self.running:
                return 3
            return 2 if self.reverse else 1
        if addr == 0x8000:
            return self.fault_code & 0xFFFF
        if addr == 0xF011:                # P0.17 加速时间（0.1s）
            return self.acc_tenths & 0xFFFF
        if addr == 0xF012:                # P0.18 减速时间（0.1s）
            return self.dec_tenths & 0xFFFF
        # 0x1007/0x1008 = DI/DO 状态位图（本服务端不读，给 0 即可）
        # 0x1009~0x1020 = AI/PID/线速度等，真机有寄存器但本服务端不用
        if 0x1000 <= addr <= 0x2000:
            return 0
        raise KeyError(addr)

    # ---- 时间推进 ----
    def on_access(self):
        """每拍推进输出频率（爬坡）。

        [停机走斜坡，不是瞬断] 旧实现"只要不在运行态就把 out_hz 置 0"，
        于是点停止后输出频率瞬间归零 —— 界面上永远看不到"正在停机"。
        真机是**按减速时间斜坡下降**（现场要好几秒），用户 2026-11-06 明确
        要求把这个过程显示出来。

        现在的模型：
            · 斜率按 ACC/DEC 时间参数推导（_ramp_hz_per_tick，升/降各用
              各的参数）；参数未配置回退 RAMP_HZ_PER_TICK。
            · 命令 6（减速停机）→ `decelerating=True`，out_hz 按斜率降
            · 命令 5（自由停车）→ 立刻断输出（真机自由停车就是瞬断）
            · out_hz 降到 0：有换向待决（_rev_pending）→ 翻方向、按给定
              反向加速（保持运行）；否则把 running 置 False（状态字这时
              才变"停机"）
        """
        if not self.running:
            self.out_hz = 0.0
            self.target_hz = 0.0
            self.decelerating = False
            return
        delta = self.target_hz - self.out_hz
        slope = self._ramp_hz_per_tick(decel=delta < 0)
        if abs(delta) <= slope:
            self.out_hz = self.target_hz
            # [不能在这里无脑清 decelerating] 最后一拍 delta 会落进死区，
            # 若这里把 decelerating 清掉，下面"降到 0 就置停机/换向"的分
            # 支就永远进不去 ⇒ 减停到 0 后 running 仍为 True（实测复现）。
            # 只在"不是减速中"或"还没到 0"时才清。
            if not (self.decelerating and self.out_hz <= 0.0):
                self.decelerating = delta < 0
        else:
            self.out_hz += slope if delta > 0 else -slope
            self.decelerating = delta < 0
        # 斜坡降到 0：有换向待决就翻向加速（保持运行），否则真正停机。
        if self.decelerating and self.out_hz <= 0.0:
            if self._rev_pending is not None:
                # [换向过零] 这一刻才翻方向位——状态字（0x3000）在此之前
                # 一直报**原**方向（电机确实还在原方向转），与真机一致。
                # 目标现算：减速期间改的给定在这里生效。
                self.reverse = bool(self._rev_pending)
                self._rev_pending = None
                self.decelerating = False
                self.out_hz = 0.0
                self.target_hz = self.set_percent / 10000.0 * self.max_hz
            else:
                self.out_hz = 0.0
                self.target_hz = 0.0
                self.decelerating = False
                self.running = False


# ============================================================================
#                        温湿度寄存器模型
# ============================================================================

class VirtualTempHum(object):
    """ZS-SHT30 温湿度（FC04 读 0x0000 ×2，各 ×0.1）。"""

    def __init__(self, temp_tenths=265, hum_tenths=550):
        self.temp_tenths = int(temp_tenths)
        self.hum_tenths = int(hum_tenths)

    def read(self, function, address, count):
        if function != 4:
            raise KeyError("温湿度只支持 FC04")
        vals = []
        for i in range(count):
            addr = address + i
            if addr == 0:
                vals.append(self.temp_tenths & 0xFFFF)
            elif addr == 1:
                vals.append(self.hum_tenths & 0xFFFF)
            else:
                raise KeyError(addr)
        return vals

    def write_single(self, function, address, value):
        raise KeyError("温湿度只读")

    def on_access(self):
        pass


# ============================================================================
#                        电磁流量计寄存器模型
# ============================================================================

class VirtualFlowMeter(object):
    """LD 系列电磁流量计（FC03 读变量区 0x1010 ×22）。

    [寄存器口径] 与 C 端 `XFlowMeter.h` 的 22 槽逐项对应，**高字在前**：

        0x1010/11 Float 瞬时流量      0x1012/13 Float 瞬时流速
        0x1014/15 Float 流量百分比    0x1016/17 Float 流体电导比
        0x1018/19 Long  正向累积整数  0x101A/1B Float 正向累积小数
        0x101C/1D Long  反向累积整数  0x101E/1F Float 反向累积小数
        0x1020 U16 瞬时流量单位       0x1021 U16 累积总量单位
        0x1022~0x1025 U16 四种报警

    [为什么要波动] 恒定值看不出"数据在刷新"，容易误判成界面卡住。流量按
    正弦小幅波动（±2%，周期 60s），累积量随之单调累加 —— 与独立模拟器
    `sim/hsf_sim.py` 同一套口径，两边看到的数一样。

    [单位码两张表各用各的] 瞬时流量表 5 = m³/h；累积总量表 1 = m³。
    曾经把累积单位填成 10（瞬时表的邻居码）导致界面显示 "?"。
    """

    #: 瞬时流量基准（m³/h）。缺省=2026-11-09 ttyS4 实测现场口径（站5 LD
    #: 流量计灌水后约 3.6 m³/h；实测当天管道空置读数 0.0+空管报警 True，
    #: 模拟器取"可用"口径，空管态由 empty_pipe=True 复现——空管时瞬时
    #: 流量归 0，与真机行为一致）。
    BASE_FLOW = 3.6
    #: 管道内径（m），用于由流量反算流速。
    PIPE_DIAMETER = 0.05

    def __init__(self, flow_unit=5, total_unit=1, base_flow=None,
                 empty_pipe=False):
        self.flow_unit = int(flow_unit)     #: 5 = m³/h
        self.total_unit = int(total_unit)   #: 1 = m³
        #: 基准流量：构造可覆盖（app 装配处传 cfg.flowmeter_sim_flow），
        #: 缺省用类常量（=现场实测口径）。
        self.base_flow = (self.BASE_FLOW if base_flow is None
                          else float(base_flow))
        self._t0 = time.monotonic()
        self._forward_total = 0.0
        self._reverse_total = 0.0
        self._last_ms = self._t0
        #: 报警位（0=正常）。测试超时/告警链路时可直接改；
        #: empty_pipe=True 时 0x1024 恒 1 且瞬时流量归 0（空管口径）。
        self.alarm_upper = 0
        self.alarm_lower = 0
        self.alarm_empty = 1 if empty_pipe else 0
        self.alarm_system = 0

    # ---- 内部：推进累积量 ----
    def _step(self):
        now = time.monotonic()
        dt = max(0.0, now - self._last_ms)
        self._last_ms = now
        flow = self._flow_now()
        # 累积量 = 积分（dt 用真实经过的时间，不按"被访问次数"算，
        # 否则轮询节拍一变累积速度就跟着变）
        self._forward_total += flow / 3600.0 * dt
        return flow

    def _flow_now(self):
        """当前瞬时流量（±2% 正弦波动，周期 60s）；空管恒 0。

        [空管为什么归 0] 真机空管时瞬时流量报 0.0000、电导比异常、空管
        报警置位（实测文档 §6.3：流量 0 + 电导比 3354 + 空管 True）。
        模拟器照抄：有流量却报空管是自相矛盾的假态。
        """
        if self.alarm_empty:
            return 0.0
        phase = (time.monotonic() % 60.0) / 60.0 * 2.0 * math.pi
        return self.base_flow * (1.0 + 0.02 * math.sin(phase))

    # ---- 寄存器读写 ----
    @staticmethod
    def _split_float(value):
        """float32 → (hi, lo)，高字在前。"""
        raw = struct.unpack(">I", struct.pack(">f", float(value)))[0]
        return (raw >> 16) & 0xFFFF, raw & 0xFFFF

    @staticmethod
    def _split_long(value):
        """uint32 → (hi, lo)。"""
        raw = int(value) & 0xFFFFFFFF
        return (raw >> 16) & 0xFFFF, raw & 0xFFFF

    def read(self, function, address, count):
        if function != 3:
            raise KeyError("流量计只支持 FC03，收到 FC%02d" % function)
        # 变量区之外的地址（参数区）返回 0：真机对"有但不用"的寄存器也是
        # 正常回 0，抛异常会把"读成功但值为 0"变成"整拍失败"。
        if not (0x1010 <= address <= 0x1025):
            return [0] * count
        flow = self._step()
        speed = (flow / 3600.0) / (math.pi * (self.PIPE_DIAMETER / 2.0) ** 2)
        fwd_int = int(self._forward_total)
        fwd_frac = self._forward_total - fwd_int
        rev_int = int(self._reverse_total)
        rev_frac = self._reverse_total - rev_int
        regs = {}
        # 电导比：空管 3354（实测文档 §6.3 空管态特征值）、正常 1.0（老口径）。
        cond = 3354.0 if self.alarm_empty else 1.0
        for base, value in ((0x1010, flow), (0x1012, speed),
                            (0x1014, 100.0 * flow / (self.base_flow * 2)
                             if self.base_flow > 0 else 0.0),
                            (0x1016, cond)):
            hi, lo = self._split_float(value)
            regs[base] = hi
            regs[base + 1] = lo
        hi, lo = self._split_long(fwd_int)
        regs[0x1018], regs[0x1019] = hi, lo
        hi, lo = self._split_float(fwd_frac)
        regs[0x101A], regs[0x101B] = hi, lo
        hi, lo = self._split_long(rev_int)
        regs[0x101C], regs[0x101D] = hi, lo
        hi, lo = self._split_float(rev_frac)
        regs[0x101E], regs[0x101F] = hi, lo
        regs[0x1020] = self.flow_unit & 0xFFFF
        regs[0x1021] = self.total_unit & 0xFFFF
        regs[0x1022] = self.alarm_upper & 0xFFFF
        regs[0x1023] = self.alarm_lower & 0xFFFF
        regs[0x1024] = self.alarm_empty & 0xFFFF
        regs[0x1025] = self.alarm_system & 0xFFFF
        out = []
        for i in range(count):
            addr = address + i
            out.append(regs.get(addr, 0))
        return out

    def write_single(self, function, address, value):
        raise KeyError("流量计变量区只读")

    def on_access(self):
        """被访问时推进累积量（轮询节拍充当定时器）。"""
        self._step()


# ============================================================================
#                        浊度仪寄存器模型
# ============================================================================

class VirtualTurbidity(object):
    """浊度仪（FC03 读 0x0000 起 2/4 字，字节序可配 + IEEE754 float32）。

    [口径与真机同源] WZT-701A 说明书 4.3.10.2 的可复现帧：

        发送：01 03 00 00 00 02 C4 0B
        回复：01 03 04 D7 0A 41 1F 92 1D  →  9.99 NTU

    即寄存器 0x0000 = 低 16 位，0x0001 = 高 16 位（CDAB）；拼出的 32 位
    位模式按 IEEE754 单精度解释，**不除以 100**。
    `devices.TurbidityMeter` 按同一套口径实现；两处必须一起改。

    [字节序按台 —— 2026-11-09 ttyS4 实测新增] 现场两台不同厂家：
      站1 WZT-701A   = **CDAB**（低字在前）——老口径，缺省保持；
      站2 ZHEYI-5100ZDY = **DCBA**（4 字节整字节反序 = 小端 float32），
        且一帧读 4 个寄存器同刻拿浊度+温度（0x0002/0x0003 = 温度 ℃）。
    构造时 byte_order/temperature 从 cfg（turbidity_byte_orders 等）传入。
    模拟器的验收口径：用 devices.TurbidityMeter **相同的解码路径**（同
    字节序、同寄存器布局）能读回相同的值——模型造帧与解析互为镜像。

    [初版错在哪] 初版造的是"32 位整数 ÷100"（ABCD 高字在前）。用它自己的
    模型去喂自己的解析器当然自洽，但**与真机不符** —— 真实仪表接上来就会
    读出错值。模拟器的价值是复刻真机行为，不是复刻一个自洽的假设。

    [为什么要波动] 恒定值看不出"数据在刷新"。两台用**不同相位**波动，
    这样一眼能看出是两个独立的仪器，而不是同一个值贴了两遍。

    [波动幅按台 —— 2026-11-09 实测分台] 40s × 20 轮混跑：站 1 浊度
    2.29 完全恒定（最小=最大）、站 2 在 0.04458~0.04577 缓慢波动、温度
    19.25~19.3125℃ 几乎不动。装配处传 wobble_ntu（0=恒定 / >0=绝对幅）
    与 temp_wobble_c 复现；不传保持旧口径（相对 ±3% 波动、恒温）。
    """

    #: 基准浊度（NTU）缺省：=2026-11-09 ttyS4 实测口径（站1 2.29、
    #: 站2 0.045）。app 装配处用 cfg.turbidity_sim_ntu 覆盖，这里只是
    #: 模型自身的兜底缺省。
    BASE_BY_INDEX = (2.29, 0.045)
    #: 波动幅度（相对值，±3%）——**旧缺省口径**。装配处按实测分台给
    #: 绝对幅（wobble_ntu：站1 恒定 0 / 站2 ±0.001）后本常量不再参与。
    WOBBLE = 0.03
    #: 温度漂移周期（秒，慢变量；与流量计波动同档）。
    TEMP_PERIOD_S = 60.0

    #: 字节序 → 寄存器字的拼装（与 devices.TurbidityMeter.BYTE_ORDER_PERM
    #: 同一张表的两个方向：那边把线上字节重排回位模式，这边把位模式铺成
    #: 线上字节再两字一组）。改一处必须核对面那边的镜像用例。
    ORDER_TO_REGS = {
        "ABCD": lambda b: (b[0] << 8 | b[1], b[2] << 8 | b[3]),
        "CDAB": lambda b: (b[2] << 8 | b[3], b[0] << 8 | b[1]),
        "BADC": lambda b: (b[1] << 8 | b[0], b[3] << 8 | b[2]),
        "DCBA": lambda b: (b[3] << 8 | b[2], b[1] << 8 | b[0]),
    }

    def __init__(self, index=0, base_ntu=None, byte_order="CDAB",
                 temperature=None, wobble_ntu=None, temp_wobble_c=None):
        self.index = int(index) & 1
        #: 基准浊度：app 装配处传 cfg.turbidity_sim_ntu[index]（缺省与
        #: 现场实测一致），未传用类缺省。
        self.base_ntu = (self.BASE_BY_INDEX[self.index]
                         if base_ntu is None else float(base_ntu))
        #: 线上字节序（"CDAB"/"DCBA"/"ABCD"/"BADC"），与对端设备层的
        #: BYTE_ORDER 旋钮**必须同值**——一边造帧一边解，不同值必假数。
        order = str(byte_order or "CDAB").strip().upper()
        if order not in self.ORDER_TO_REGS:
            raise ValueError("浊度仪字节序非法：%r（支持 %s）"
                             % (byte_order, sorted(self.ORDER_TO_REGS)))
        self.byte_order = order
        #: 温度（℃，同帧 0x0002/0x0003）。None=该台无温度能力（真机
        #: 站1），此时读 0x0002/0x0003 回 0。app 传 cfg.turbidity_sim_temp。
        self.temperature = None if temperature is None else float(temperature)
        self._t0 = time.monotonic()
        #: 手动固定值（测试/演示用；非 None 时不波动）。
        self.fixed_ntu = None
        #: 温度固定值（测试用；缺省=给定的温度常量，本身不波动——实测
        #: 温度 40s 内几乎不动）。
        self.fixed_temp = None
        #: 浊度微波动幅（NTU，绝对值绕 base_ntu 正弦）。三态：
        #:   None = 未配（旧口径：相对 ±3% 波动，历史行为一字不改）；
        #:   0    = 恒定（站 1 WZT 实测 20 轮完全不动）；
        #:   >0   = 绕基准 ±幅缓慢波动（站 2 ZHEYI 实测 0.045±0.001）。
        self.wobble_ntu = None if wobble_ntu is None else max(0.0,
                                                             float(wobble_ntu))
        #: 温度漂移幅（℃，绕 temperature 正弦；None/0 = 恒温；站 2 实测
        #: 19.25~19.3125℃ ⇒ 装配处传 0.05）。
        self.temp_wobble_c = (None if temp_wobble_c is None
                              else max(0.0, float(temp_wobble_c)))

    def _ntu_now(self):
        if self.fixed_ntu is not None:
            return float(self.fixed_ntu)
        # 两台相位错开 180°，避免两条曲线完全重合
        phase = (time.monotonic() % 45.0) / 45.0 * 2.0 * math.pi
        phase += math.pi * self.index
        if self.wobble_ntu is None:
            # 旧口径：相对 ±3%（未配 wobble_ntu 的既有用法一字不改）
            return self.base_ntu * (1.0 + self.WOBBLE * math.sin(phase))
        # [绝对幅口径 2026-11-09 实测分台] 幅 0 = 恒定；幅 >0 = ±幅缓慢波动
        return self.base_ntu + self.wobble_ntu * math.sin(phase)

    def _temp_now(self):
        """当前温度：恒温（旧口径）或绕基准 ±temp_wobble_c 缓慢漂移。"""
        base = (self.fixed_temp if self.fixed_temp is not None
                else self.temperature)
        if base is None:
            return None
        if not self.temp_wobble_c:
            return float(base)
        phase = (time.monotonic() % self.TEMP_PERIOD_S) \
            / self.TEMP_PERIOD_S * 2.0 * math.pi
        return float(base) + self.temp_wobble_c * math.sin(phase)

    @classmethod
    def regs_for_value(cls, value, order):
        """工程值 →（寄存器0，寄存器1）：按字节序铺线上字节，与设备层
        `TurbidityMeter.bits_from_online_bytes` 互为逆操作。

        [验算（实测文档附录 A）] 9.99 NTU（bits=0x411FD70A）按 CDAB 应得
        [0xD70A, 0x411F]；站2 帧 0.045751（bits=0x3D3B656E）按 DCBA 应得
        [0x6E65, 0x3B3D]。
        """
        bits = struct.unpack(">I", struct.pack(">f", float(value)))[0]
        b = [(bits >> 24) & 0xFF, (bits >> 16) & 0xFF,
             (bits >> 8) & 0xFF, bits & 0xFF]
        return cls.ORDER_TO_REGS[order](b)

    def read(self, function, address, count):
        # [功能码分台] 手册：站 1 WZT-701A 只支持 FC03（FC04 无应答）；
        # 站 2 ZHEYI-5100ZDY **FC04 也受理**（2026-11-09 实测文档 §5.5）
        # ——值与 FC03 完全一致，只是应答功能码恒回 03。Python 虚拟层
        # 不落帧（设备层按值取数），怪癖在这里表现为 FC04 读成功且同值；
        # 应答功能码恒 03 的帧级口径由 C 侧 XModbusVirtualModel 的
        # responseFunction 钩子逐字节复刻。
        if not (function == 3 or (function == 4 and self.index == 1)):
            raise KeyError("浊度仪只支持 FC03，收到 FC%02d" % function)
        if not (0x0000 <= address <= 0x0003):
            # 未建模地址返回 0（真机对"有但不用"的寄存器同样回 0；
            # 抛异常会把"读成功但值为 0"变成"整拍失败"→ 设备永远离线）
            return [0] * count
        # [按字节序铺寄存器] CDAB：0x0000=低 16 位、0x0001=高 16 位（老口径
        # 一字不改）；DCBA 等其余序见 ORDER_TO_REGS。
        reg0, reg1 = self.regs_for_value(self._ntu_now(), self.byte_order)
        regs = {0x0000: reg0, 0x0001: reg1}
        # 温度在同帧后两个字（站2 一次读 4 的同刻快照口径）
        temp = self._temp_now()
        if temp is not None:
            t0, t1 = self.regs_for_value(temp, self.byte_order)
            regs[0x0002] = t0
            regs[0x0003] = t1
        return [regs.get(address + i, 0) for i in range(count)]

    def write_single(self, function, address, value):
        raise KeyError("浊度仪只读")

    def on_access(self):
        pass


# ============================================================================
#                        模拟量寄存器模型
# ============================================================================

class VirtualAnalog(object):
    """中盛 8 路 4~20mA（FC04 读 0x0000 ×N，**毫伏**口径）。

    通道 0 模拟压差计（2985mV ≈ 11.988mA ≈ 0.499MPa @ 0~1MPa 量程），
    其余通道走 4mA 活零点（995mV）。
    """

    def __init__(self, millivolts=None, channels=8):
        if millivolts is None:
            millivolts = [2985] + [995] * (channels - 1)
        self.mv = [int(v) for v in millivolts[:channels]]
        self.channels = channels

    def read(self, function, address, count):
        if function != 4:
            raise KeyError("模拟量只支持 FC04")
        vals = []
        for i in range(count):
            addr = address + i
            if 0 <= addr < self.channels:
                vals.append(self.mv[addr] & 0xFFFF)
            else:
                raise KeyError(addr)
        return vals

    def write_single(self, function, address, value):
        raise KeyError("模拟量只读")

    def on_access(self):
        pass


# ============================================================================
#                        数字量寄存器模型（含行程开关联动）
# ============================================================================

class VirtualDigital(object):
    """MT3A-IO1632（FC01 读线圈 / FC02 读离散输入 / FC05 写单点）。

    ============================================================================
                        行程开关模型：**带动作时间**
    ============================================================================

    现场电磁阀满开满关约 **10s**，服务端超时判据是 **20s**。虚拟模块必须
    复现这个时间尺度，否则超时报警那条路径永远走不到（DO 写完 DI 立刻
    跟着变 ⇒ 状态机下一拍就"到位"，`cmd_at_ms` 立刻清零）。

    故这里给每路阀建模一个**动作行程**：

        写 DO → 进入「动作中」：两个到位 DI 都断开（0/0）
              → 经过 move_ms（默认 10000）→ 目标侧到位

    每路阀 n（与现场接线顺序一致，**先关到位、后开到位**）：

        DO(16+n)=1 → DI(16+2n)=0, DI(16+2n+1)=1    开到位
        DO(16+n)=0 → DI(16+2n)=1, DI(16+2n+1)=0    关到位
        动作中     → DI(16+2n)=0, DI(16+2n+1)=0    两个都不吸

    [可配的几种"坏阀"] 都通过构造参数打开，用来验证服务端的故障判读：

        move_ms=10000         正常阀：10s 到位
        move_ms=25000         行程过长：20s 时误报超时（验证报警）
        move_ms=None          卡死：永远不动（验证"无到位反馈"文案）
        no_feedback=True      行程开关没接：DI 恒 0（同上但语义不同）

    [时间怎么推进] 虚拟设备没有独立线程，靠"被访问"推进（见 VirtualMaster
    `_tick` → `on_access`）。数字量模块 200ms 被轮询一次，故动作计时精度
    约 200ms —— 对 10s/20s 的量级完全够用。
    """

    #: 阀占用的 DO 起始位与 DI 起始位（与现场接线一致）。
    #: [2026-11-06 点位整体减 16] .12 拆除后只剩 .13 单台 16 点、点表从 0
    #: 起，故阀 0..5 占 DO0..5 / DI0..11（原为聚合上半区 16..）。
    VALVE_DO_BASE = 0
    VALVE_DI_BASE = 0
    VALVE_COUNT = 6
    #: 非阀区域的 DI 初值。点表 0 起后阀占满 DO0..5 / DI0..11，剩下
    #: DI12..15 是按钮组（急停/启动/停止/复位）。
    #:
    #: [常闭触点的静止电平] 急停（DI12）与停止（DI14）都是**常闭**：
    #: 静止时回路通 ⇒ DI=1。按下才是 0。所以初值必须把这两位置 1，
    #: 否则模拟器会"一开机就认为停止键被按住"（服务端 buttons.py 的
    #: `pressed = (cur is False)` 恒真，客户端也会显示「停止●按下」）。
    #: 启动/复位（DI13/DI15）是常开：未按 = 0。
    BASE_DI = 0x00005000
    #: 按钮组点位（与 cfg.button_items 同口径）。
    BUTTON_ESTOP_DI = 12
    BUTTON_START_DI = 13
    BUTTON_STOP_DI = 14
    BUTTON_RESET_DI = 15
    BUTTON_START_LAMP_DO = 7
    BUTTON_STOP_LAMP_DO = 8
    BUTTON_RESET_LAMP_DO = 9
    #: 现场满开满关约 10s（用户口径）⇒ 虚拟模型缺省也用 10s。
    DEFAULT_MOVE_MS = 10000

    def __init__(self, channels=16, do_init=0x00000000, move_ms=None,
                 no_feedback=False):
        """
        :param move_ms: 动作行程时间（毫秒）。None 用 DEFAULT_MOVE_MS；
                        传 float("inf") 表示**永远不动**（阀卡死）。
        :param no_feedback: True=行程开关不接（DI 恒 0，无论 DO 怎么变）。
        """
        self.channels = int(channels)
        self.do_mask = int(do_init)
        #: 阀区以外的 DI（默认只有急停回路通 = DI12=1）。
        #: [掩码为什么是 0x00000FFF] 阀占 DI0..11（6 路 x 2 个到位），
        #: 那 12 位由行程模型算，不能沿用静态初值。
        self._di_extra = self.BASE_DI & ~0x00000FFF
        #: 按键覆盖位图（模拟按按钮 / 拔急停线）。见 press_button()。
        #: 有覆盖的位**优先于** `_di_extra` 的默认值——这样既能"按住"，
        #: 也能把默认的"急停正常"（DI12=1）按掉。
        self._di_override = 0
        self._di_override_mask = 0
        self.move_ms = (self.DEFAULT_MOVE_MS if move_ms is None
                        else float(move_ms))
        self.no_feedback = bool(no_feedback)
        #: 每路阀的行程状态：{index: {"target": bool, "start_ms": float,
        #:                            "done": bool}}
        #: done=True 表示已到位（DI 按 target 呈现）；False 表示在途中
        #: （两个 DI 都断开）。
        self._travel = {}
        self._t0 = time.monotonic()
        # 初始状态：按 do_init 的位**直接算作已到位**（开机即稳态，
        # 不要一启动就让 6 路阀显示"动作中"）。
        for i in range(self.VALVE_COUNT):
            target = bool(self.do_mask & (1 << (self.VALVE_DO_BASE + i)))
            self._travel[i] = {"target": target, "start_ms": -1.0,
                               "done": True}

    # ---- 时间 ----
    def _now(self):
        """虚拟模型的时钟（毫秒）。独立于服务端 monotonic 起点也能对齐。"""
        return (time.monotonic() - self._t0) * 1000.0

    def _arrived(self, index):
        """该路当前是否已到位（推进行程后判断）。"""
        st = self._travel.get(index)
        if st is None:
            return True
        if st["done"]:
            return True
        if self.no_feedback:
            return False            # 行程开关没接：永远等不到反馈
        if self.move_ms == float("inf"):
            return False            # 阀卡死
        if self._now() - st["start_ms"] >= self.move_ms:
            st["done"] = True
            return True
        return False

    # ---- DI 推导 ----
    def di_mask(self):
        """按行程状态算 DI 位图。

        [为什么不再直接由 do_mask 推导] 瞬发模型下"命令"与"到位"是同一
        瞬间，阀状态机永远看不到「动作中」，20s 超时分支成为死代码。
        现在中间多一个"两个 DI 都断开"的行程期，状态机才能按真实时序跑。
        """
        di = self._di_extra
        # 按键覆盖：被 press_button() 动过的位以覆盖值为准。
        if self._di_override_mask:
            di = (di & ~self._di_override_mask) | (self._di_override
                                                  & self._di_override_mask)
        for i in range(self.VALVE_COUNT):
            close_bit = self.VALVE_DI_BASE + i * 2
            open_bit = self.VALVE_DI_BASE + i * 2 + 1
            if open_bit >= self.channels:
                break
            if not self._arrived(i):
                continue            # 动作中：两个到位都不吸（DI 保持 0）
            on = bool(self.do_mask & (1 << (self.VALVE_DO_BASE + i)))
            di |= (1 << open_bit) if on else (1 << close_bit)
        return di & ((1 << self.channels) - 1)

    # ---- 寄存器读写 ----
    def read(self, function, address, count):
        if function in (1, 2):
            bits = self.do_mask if function == 1 else self.di_mask()
            # FC01/02 返回**位**列表。超出 channels 的地址按 0 返回
            # （真实模块对未配置的点也回 0，而不是抛异常）。
            return [1 if (bits >> (address + i)) & 1 else 0
                    for i in range(count)]
        raise KeyError("数字量读只支持 FC01/FC02，收到 FC%02d" % function)

    def write_single(self, function, address, value):
        if function != 5:
            raise KeyError("数字量写只支持 FC05，收到 FC%02d" % function)
        if not 0 <= address < self.channels:
            raise KeyError("线圈越界 %d" % address)
        on = (value & 0xFF00) == 0xFF00
        if on:
            self.do_mask |= (1 << address)
        else:
            self.do_mask &= ~(1 << address)
        # ---- 阀区变位 ⇒ 启动这一路的行程计时 ----
        valve = address - self.VALVE_DO_BASE
        if 0 <= valve < self.VALVE_COUNT:
            st = self._travel.setdefault(
                valve, {"target": False, "start_ms": -1.0, "done": True})
            if bool(st["target"]) != on:
                # 目标变了 ⇒ 重新开始行程（途中再改目标也在途中，符合直觉）
                st["target"] = on
                st["start_ms"] = self._now()
                st["done"] = False
        # ---- 按钮灯区变位 ⇒ 不联动 DI（灯只是灯）----
        # [为什么不联动] 现场三盏灯是**指示灯**，不是带回读的按钮灯；
        # 点亮它不会让任何 DI 变。按钮组的状态迁移由服务端的 ButtonStation
        # 按 DI 沿推进，虚拟模型只需要如实反映"DO 写进去了"。
        return

    # ---- 实验辅助：模拟按键（给离线调试/测试用）----
    def press_button(self, di_channel, pressed=True):
        """模拟按下/松开一个按钮（直接改 DI 电平）。

        现场调试时可以用它替代"人上去按"，也让自动逻辑能在纯离线
        （--sim）下跑完整状态机。
        """
        ch = int(di_channel)
        if not 0 <= ch < self.channels:
            raise IndexError("DI 越界 %d" % ch)
        bit = 1 << ch
        self._di_override_mask |= bit
        if pressed:
            self._di_override |= bit
        else:
            self._di_override &= ~bit

    def set_estop(self, normal=True):
        """模拟急停回路：normal=True 表示回路通（常闭触点未按下）。"""
        self.press_button(self.BUTTON_ESTOP_DI, bool(normal))

    def on_access(self):
        """被访问时推进行程（VirtualMaster._tick 会调）。

        [为什么在这里推进] 虚拟设备没有独立线程；数字量模块每 200ms 被
        轮询一次，正好充当"定时器"。不必自己起 task。
        """
        # 触发一次判断，让到位的行程落到 done=True（后续 di_mask 直接读）
        for i in range(self.VALVE_COUNT):
            self._arrived(i)

    # ---- 实验辅助（不是设备语义，只给测试/演示用）----
    def valve_progress(self, index):
        """该路动作进度（0.0~1.0）；已到位=1.0，卡死=0.0。诊断用。"""
        st = self._travel.get(index)
        if st is None or st["done"]:
            return 1.0
        if self.no_feedback or self.move_ms == float("inf"):
            return 0.0
        return min(1.0, (self._now() - st["start_ms"]) / self.move_ms)


# ============================================================================
#                              工厂
# ============================================================================

def make_vfd_master(max_hz=50.0, log=None):
    model = VirtualVfd(max_hz=max_hz, log=log)
    return VirtualMaster(model, name="虚拟变频器", log=log)


def make_temphum_master(log=None):
    model = VirtualTempHum()
    return VirtualMaster(model, name="虚拟温湿度", log=log)


def make_analog_master(channels=8, log=None):
    model = VirtualAnalog(channels=channels)
    return VirtualMaster(model, name="虚拟模拟量", log=log)


def make_digital_master(channels=32, flaky_rate=0.0, move_ms=None,
                        no_feedback=False, log=None):
    """虚拟数字量模块。

    :param move_ms: 阀动作行程时间（毫秒）。**这个参数决定能否看到
                    "动作中"和"20s 超时报警"**：
                    10000（缺省）= 现场真实节奏，10s 到位
                    25000        = 行程过长，20s 时应报警
                    float("inf") = 阀卡死，永远不到位
    :param no_feedback: 行程开关没接（DI 恒 0）。
    """
    model = VirtualDigital(channels=channels, move_ms=move_ms,
                           no_feedback=no_feedback)
    kind = "虚拟数字量"
    if no_feedback:
        kind += "(开关未接)"
    elif move_ms == float("inf"):
        kind += "(阀卡死)"
    elif move_ms is not None:
        kind += "(行程 %.1fs)" % (float(move_ms) / 1000.0)
    return VirtualMaster(model, name=kind, flaky_rate=flaky_rate, log=log)


def make_turbidity_master(index=0, log=None, **kw):
    """虚拟浊度仪（index 0/1 决定基准浊度与波动相位）。

    其余关键字参数（base_ntu / byte_order / temperature）原样透传给
    VirtualTurbidity——app 装配处按 cfg 逐台传入。
    """
    model = VirtualTurbidity(index=index, **kw)
    return VirtualMaster(model, name="虚拟浊度仪%d" % (index + 1), log=log)
