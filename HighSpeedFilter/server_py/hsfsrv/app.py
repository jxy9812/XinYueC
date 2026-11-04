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

from . import bson
from . import devices
from . import valves
from . import proto


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
                 "run_state", "text")

    def __init__(self, name, kind, side, health, error, online, run_state, text):
        self.name = name
        self.kind = kind
        self.side = side
        self.health = health
        self.error = error
        self.online = online
        self.run_state = run_state
        self.text = text

    def key(self):
        """变化比对用的元组（避开对象身份，纯值比较）。"""
        return (self.name, self.kind, self.side, self.health, self.error,
                self.online, self.run_state, self.text)


class JogWatch(object):
    """点动手持看门狗状态（每台变频器一把）。"""

    __slots__ = ("active", "reverse", "last_ms")

    def __init__(self):
        self.active = False
        self.reverse = False
        self.last_ms = 0


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
    #: 数字量没有模拟量的「慢变量」特性，跟基准节拍即可。

    #: 陈旧阈值（毫秒）：超过即判数据失效。
    VFD_STALE_MS = 5000
    AI_STALE_MS = 5000
    DIGITAL_STALE_MS = 5000

    #: 健康巡检节拍（毫秒）。事件驱动：状态不变不推，故不必太快。
    HEALTH_TICK_MS = 500

    def __init__(self, config, log=None):
        self.cfg = config
        self._log = log or (lambda msg: None)
        self.server = None

        # ---- 设备集合 ----
        self.vfd = None            # VfdM512 或 None
        self.temphum = None
        self.analog = None
        self.digital = []          # [DigitalIo, ...]
        #: 电磁阀组（6 路；点位来自 config.valve_items，先留空后对接）
        self.valve_bank = None

        # ---- 推送闸 ----
        self._gates = {
            proto.TOPIC_VFD_STATUS: [],
            proto.TOPIC_AI_STATUS: [PushGate(self.AI_PUSH_MS)],
            proto.TOPIC_TEMPHUM_STATUS: [],
            proto.TOPIC_DIGITAL_STATUS: [],
            proto.TOPIC_VALVE_STATUS: [],
        }

        # ---- 健康 ----
        self._health_seq = 0
        self._health_last = None       # 上次推送的 entries（key 列表）
        self._health_primed = False
        self._health_next_ms = 0

        # ---- 点动看门狗 ----
        self._jog = {}

        # ---- 采集任务 ----
        self._poll_tasks = []
        #: 运行模式（手动/自动）。自动模式拒绝全部远程控制。
        self.run_mode = self.MODE_MANUAL

    # ==================================================================
    #                          装配 / 拆除
    # ==================================================================
    def build_devices(self):
        """按配置装配设备对象（不打开任何端口/连接，纯对象构造）。"""
        from .modbus import RtuMaster, TcpMaster

        # [关键] 同一条 485 上的多个从站必须**共用一个主站**。
        # 串口是排他资源：每台设备各建一个 RtuMaster 就会互相抢口
        # （第二个 open 直接 EACCES/EBUSY）。而且 RTU 是半双工单主站总线，
        # 多主站并发写会互相踩帧——本来就该串行化。
        # 故这里按「串口号+波特率」缓存 master，变频器(站2)与温湿度(站4)
        # 共享同一个实例；RtuMaster 内部已有 asyncio.Lock 保证事务串行。
        rtu_masters = {}

        def make_rtu(unit_id):
            """取（必要时创建）该串口的主站。

            设备侧只需在每次事务时改 unit 号——RtuMaster 的 read/write
            都用 self.unit，故调用前由设备层设置；这里返回**副本视图**
            不可行（要共享串口句柄），改为给设备传入 unit 并在事务时切换。

            [实现选择] 让 RtuMaster 支持 per-call unit：设备调用
            `read(fc, addr, cnt, unit=self.unit_id)`。这样一台 master
            天然能服务同总线上的多个从站。
            """
            c = self.cfg.serial
            key = (c.port, c.baudrate)
            master = rtu_masters.get(key)
            if master is None:
                # 超时/重试取该路配置（同总线的从站共用一套链路参数）
                master = RtuMaster(c.port, c.baudrate, unit_id,
                                   c.timeout_ms / 1000.0, c.retries,
                                   self._log)
                rtu_masters[key] = master
            return master

        def make_vfd_master():
            return make_rtu(self.cfg.vfd_unit_id)

        def make_th_master():
            return make_rtu(self.cfg.temphum_unit_id)

        if self.cfg.enable_serial:
            if self.cfg.enable_vfd:
                self.vfd = devices.VfdM512(make_vfd_master, self.cfg.vfd_unit_id,
                                           self._log)
                self._jog["vfd"] = JogWatch()
                self._gates[proto.TOPIC_VFD_STATUS].append(
                    PushGate(self.VFD_PUSH_MS))
            if self.cfg.enable_temphum:
                self.temphum = devices.TempHum(make_th_master,
                                               self.cfg.temphum_unit_id,
                                               self._log)
                self._gates[proto.TOPIC_TEMPHUM_STATUS].append(
                    PushGate(self.TEMPHUM_PUSH_MS))

        if self.cfg.enable_analog:
            ai = self.cfg.analog
            master = TcpMaster(ai.host, ai.port, ai.unit_id,
                               timeout=2.0, log=self._log)
            self.analog = devices.AnalogInput(master, ai.channels, self._log)

        if self.cfg.enable_digital:
            for i, dio_cfg in enumerate(self.cfg.digital_modules):
                master = TcpMaster(dio_cfg.host, dio_cfg.port, dio_cfg.unit_id,
                                   timeout=dio_cfg.timeout_s, log=self._log)
                dev = devices.DigitalIo(master, dio_cfg.name, self._log)
                self.digital.append(dev)
                self._gates[proto.TOPIC_DIGITAL_STATUS].append(
                    PushGate(self.DIGITAL_PUSH_MS))

        # ---- 电磁阀组（依赖数字量模块：DI 反馈来自它们的缓存）----
        if self.cfg.enable_valves:
            self.valve_bank = valves.ValveBank(
                self.digital, self.cfg.valve_items, self._log,
                move_timeout_ms=self.cfg.valve_move_timeout_ms)
            self._gates[proto.TOPIC_VALVE_STATUS] = [
                PushGate(self.VALVE_PUSH_MS)]
            ready = self.valve_bank.ready_count()
            if ready:
                self._log("电磁阀：已装配 %d/%d 路（点位来自配置）"
                          % (ready, proto.HSF_VALVE_COUNT))
            else:
                self._log("电磁阀：6 路均未配置点位（界面可见，控制不可用）")

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

    #: xlink.Server 读取本属性决定 tick 周期（基准节拍）。
    @property
    def interval_ms(self):
        return self.TELEMETRY_MS

    # ==================================================================
    #                       运行模式（持久化）
    # ==================================================================
    def apply_mode_to_devices(self):
        """模式同步到设备层：自动模式关闸（本实现的控制闸在 app 层判定，
        故这里是空实现，保留接口以便将来下沉到设备层做双保险）。"""
        return

    def set_run_mode(self, mode):
        if mode not in (self.MODE_MANUAL, self.MODE_AUTO):
            return False
        if mode == self.run_mode:
            self.apply_mode_to_devices()
            return True
        self.run_mode = mode
        self.apply_mode_to_devices()
        self.cfg.save_run_mode(mode)
        self._log("运行模式切到 %s（远程控制%s）"
                  % ("自动" if mode == self.MODE_AUTO else "手动",
                     "已禁用" if mode == self.MODE_AUTO else "已启用"))
        return True

    def is_auto(self):
        return self.run_mode == self.MODE_AUTO

    # ==================================================================
    #                         采集循环
    # ==================================================================
    async def start_polling(self):
        """每台设备一个采集任务（互不阻塞：一台慢不影响另一台）。"""
        if self.vfd is not None:
            self._poll_tasks.append(asyncio.ensure_future(
                self._poll_loop(self.vfd, self.cfg.vfd_poll_ms, "变频器")))
        if self.temphum is not None:
            self._poll_tasks.append(asyncio.ensure_future(
                self._poll_loop(self.temphum, self.cfg.temphum_poll_ms, "温湿度")))
        if self.analog is not None:
            self._poll_tasks.append(asyncio.ensure_future(
                self._poll_loop(self.analog, self.cfg.analog_poll_ms, "模拟量")))
        for dev in self.digital:
            self._poll_tasks.append(asyncio.ensure_future(
                self._poll_loop(dev, self.cfg.digital_poll_ms, dev.name)))

    async def _poll_loop(self, dev, period_ms, label):
        """单设备轮询：失败只记日志继续（设备恢复后自动跟上，不必重启）。

        失败退避：连续失败时把周期拉长到 1s，避免刷爆日志与总线——现场常见
        「拔了线跑了整晚」，无退避会攒出几百 MB 日志（板端 journald 就是这么
        被灌满的）。
        """
        period = max(20, period_ms) / 1000.0
        backoff_period = max(period, 1.0)
        failures = 0
        while True:
            started = self._now_ms()
            try:
                await dev.poll(started)
                if failures:
                    self._log("%s 恢复通讯（此前连续失败 %d 次）"
                              % (label, failures))
                failures = 0
            except Exception as exc:                    # noqa: BLE001
                failures += 1
                # 只在首次与每 60 次失败时打日志（约 1 分钟一条）。
                if failures == 1 or failures % 60 == 0:
                    self._log("%s 采集失败（连续 %d 次）：%s"
                              % (label, failures, exc))
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
    #                       周期 tick（推送 + 健康 + 看门狗）
    # ==================================================================
    async def __call__(self, now_ms):
        """XLink 服务端的业务 tick 回调（由 xlink.Server 周期驱动）。"""
        await self._publish_telemetry(now_ms)
        self._jog_watchdog(now_ms)
        if now_ms >= self._health_next_ms:
            self._health_next_ms = now_ms + self.HEALTH_TICK_MS
            await self._health_tick(now_ms)

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
        # ---- 模拟量 ----
        if self.analog is not None and self._gate_due(
                proto.TOPIC_AI_STATUS, 0, now_ms):
            await self.server.publish(proto.TOPIC_AI_STATUS,
                                      self.encode_analog(self.analog))
        # ---- 数字量（逐模块一帧）----
        for i, dev in enumerate(self.digital):
            if self._gate_due(proto.TOPIC_DIGITAL_STATUS, i, now_ms):
                await self.server.publish(proto.TOPIC_DIGITAL_STATUS,
                                          self.encode_digital(dev, i))
        # ---- 电磁阀（6 路一帧）----
        if self.valve_bank is not None:
            # 反馈来自数字量模块缓存，故先刷新再推送（同一拍数据自洽）
            self.valve_bank.poll(now_ms)
            if self._gate_due(proto.TOPIC_VALVE_STATUS, 0, now_ms):
                await self.server.publish(proto.TOPIC_VALVE_STATUS,
                                          self.encode_valves())

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

    def encode_analog(self, ai):
        """模拟量快照：{"n","v","r0..rN","u0..uN"}。

        r* = 原始寄存器（毫伏口径，排查真相用），u* = 微安（mA × 1000）。
        客户端按 u* 显示、按 v 位图判有效。
        """
        doc = {}
        vmask = 0
        for i in range(ai.channels):
            if ai.valid[i]:
                vmask |= 1 << i
            doc["r%d" % i] = ai.raw[i]
            doc["u%d" % i] = int(round(ai.ma(i) * 1000.0))
        doc["n"] = ai.channels
        doc["v"] = vmask
        return bson.encode(doc)

    def encode_digital(self, dev, index):
        """数字量快照：{"dev","di","do","count","valid"}。

        di/do 是位图 int（bit n = 通道 n），count 恒 16。
        """
        doc = {
            proto.KEY_DEV: index,
            proto.KEY_NAME: dev.name,
            proto.KEY_DI: dev.di_mask(),
            proto.KEY_DO: dev.do_mask(),
            proto.KEY_COUNT: dev.DI_COUNT,
            proto.KEY_VALID: bool(dev.online),
            proto.KEY_STALE_MS: self.cfg.digital_stale_ms,
        }
        return bson.encode(doc)

    async def refresh_valve_sources(self):
        """让电磁阀用到的数字量模块现读一次（去重 + 节流）。

        [为什么要去重] 6 路阀可能全挂在同一台模块上（现场就是这样：
        module 0 = .12 挂 4 路、module 1 = .13 挂 2 路）。逐路现读会把
        同一台模块读 4 遍 —— 去重后每台只读一次，再由 valve_bank 统一取值。
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
        return bson.encode(doc)

    def collect_health(self, now_ms):
        """采集全量设备健康（只读缓存/标志，**不发起 IO**）。"""
        entries = []
        # ---- 变频器 ----
        if self.cfg.enable_vfd:
            if self.vfd is None:
                entries.append(HealthEntry("变频器", proto.KIND_VFD, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配"))
            else:
                health, err, online, text = self.vfd.health(
                    now_ms, self.VFD_STALE_MS)
                entries.append(HealthEntry("变频器", proto.KIND_VFD, 0, health,
                                           err, online,
                                           self.vfd.run_state if online else 0,
                                           text))
        # ---- 温湿度 ----
        if self.cfg.enable_temphum:
            if self.temphum is None:
                entries.append(HealthEntry("温湿度", proto.KIND_TEMPHUM, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配"))
            else:
                health, err, online, text = self.temphum.health(
                    now_ms, self.cfg.temphum_stale_ms)
                entries.append(HealthEntry("温湿度", proto.KIND_TEMPHUM, 0,
                                           health, err, online, 0, text))        # ---- 模拟量 ----
        if self.cfg.enable_analog:
            if self.analog is None:
                entries.append(HealthEntry("模拟量模块", proto.KIND_ANALOG_IO, 0,
                                           proto.HEALTH_OFFLINE, 0, False, 0,
                                           "未装配"))
            else:
                health, err, online, text = self.analog.health(
                    now_ms, self.AI_STALE_MS)
                entries.append(HealthEntry("模拟量模块", proto.KIND_ANALOG_IO, 0,
                                           health, err, online, 0, text))
        # ---- 数字量（逐模块）----
        for i, dev in enumerate(self.digital):
            health, err, online, text = dev.health(now_ms,
                                                   self.cfg.digital_stale_ms)
            entries.append(HealthEntry(dev.name, proto.KIND_DIGITAL_IO, i,
                                       health, err, online, 0, text))
        # ---- 电磁阀组（一个条目，含几路故障的摘要）----
        if self.valve_bank is not None:
            health, online, text = self.valve_bank.health_summary(now_ms)
            entries.append(HealthEntry("电磁阀组", proto.KIND_VALVE, 0,
                                       health, 0, online, 0, text))
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
    #                        登录策略（root 单会话）
    # ==================================================================
    def policy_blocks_login(self, user_name, session):
        """root 只允许一个在线会话（防并行修改）。

        admin（只读）不限——多人围观无风险。极小竞态窗（两个 root 同时
        处于认证中）不设防：认证在毫秒级完成。
        """
        if user_name != proto.DEFAULT_ROOT_NAME:
            return False
        if self.server is None:
            return False
        for other in self.server.sessions:
            if other is session or not other.is_ready():
                continue
            if other.auth_user == proto.DEFAULT_ROOT_NAME:
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
            proto.CMD_RUN_MODE: self._h_run_mode,
            proto.CMD_DEVICE_HEALTH: self._h_device_health,
            proto.CMD_DIGITAL_SNAPSHOT: self._h_digital_snapshot,
            proto.CMD_DIGITAL_CONTROL: self._h_digital_control,
            proto.CMD_VALVE_SNAPSHOT: self._h_valve_snapshot,
            proto.CMD_VALVE_CONTROL: self._h_valve_control,
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
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        # 2) 角色门禁
        if session.role() < proto.ROLE_CONTROL:
            await self._reply(session, request_id, msg_code, False,
                              "权限不足：控制命令需要 root 账户")
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
        reverse = bool(doc.get(proto.KEY_REV, False))
        hz = float(doc.get(proto.KEY_HZ, 0.0) or 0.0)
        rpm = float(doc.get(proto.KEY_RPM, 0.0) or 0.0)
        value = float(doc.get(proto.KEY_VAL, 0.0) or 0.0)
        index = int(doc.get(proto.KEY_IDX, 0) or 0)
        try:
            ok, err = await self.vfd.apply_control(op, reverse, hz, rpm,
                                                   value, index)
        except Exception as exc:                        # noqa: BLE001
            await self._reply(session, request_id, msg_code, False,
                              "设备写入失败：%s" % exc)
            return
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

        [闸门与变频器同口径] 自动模式拒绝 → 需 root → 解析 → 设备就绪。
        DO 是驱动现场执行器的输出，安全等级与控制变频器相同，不另开口子。
        """
        if self.is_auto():
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        if session.role() < proto.ROLE_CONTROL:
            await self._reply(session, request_id, msg_code, False,
                              "权限不足：控制命令需要 root 账户")
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
        elif proto.KEY_DO in doc:
            channel = int(doc.get(proto.KEY_DO, 0) or 0)
            on = bool(doc.get(proto.KEY_ON, False))
            ok, err = await dev.write_do(channel, on)
        else:
            await self._reply(session, request_id, msg_code, False,
                              "控制载荷缺 do 或 mask 字段")
            return
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
        self.valve_bank.poll(self._now_ms())
        await session.send_response(request_id, msg_code, self.encode_valves())

    async def _h_valve_control(self, session, request_id, msg_code, payload):
        """电磁阀手动开关：载荷 {"index":int, "on":bool}。

        [闸门与变频器/DO 同口径] 自动模式拒绝 → 需 root → 解析 → 就绪。
        电磁阀是执行器，自动模式下由现场逻辑驱动，远程只能看不能动。
        """
        if self.valve_bank is None:
            await self._reply(session, request_id, msg_code, False,
                              "电磁阀未装配")
            return
        if self.is_auto():
            await self._reply(session, request_id, msg_code, False,
                              "自动运行中：远程控制已被禁用（现场切换到手动后可用）")
            return
        if session.role() < proto.ROLE_CONTROL:
            await self._reply(session, request_id, msg_code, False,
                              "权限不足：控制命令需要 root 账户")
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
        # 命令已受理：回一拍最新状态，客户端不必再单独拉快照
        self.valve_bank.poll(self._now_ms())
        extra = self.valve_bank.to_doc()
        await self._reply(session, request_id, msg_code, ok, err or None,
                          extra=extra)

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
                                     proto.KEY_AUTO: self.is_auto()})
            return
        if session.role() < proto.ROLE_CONTROL:
            await self._reply(session, request_id, msg_code, False,
                              "权限不足：切换运行模式需要 root 账户")
            return
        if not self.set_run_mode(new_mode):
            await self._reply(session, request_id, msg_code, False, "非法模式值")
            return
        await self._reply(session, request_id, msg_code, True, None,
                          extra={proto.KEY_MODE: self.run_mode,
                                 proto.KEY_AUTO: self.is_auto()})

    # ---- 设备健康 ----
    async def _h_device_health(self, session, request_id, msg_code, payload):
        await session.send_response(request_id, msg_code, self.encode_health())

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
            self._log("点动开始（%s）" % ("反转" if reverse else "正转"))
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
            self._log("点动释放停机失败：%s" % exc)
            return
        self._log("点动释放 → 停机%s" % ("" if was_active else "（无活动点动）"))

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
        self._log("点动心跳超时（%dms 无帧）→ 自动停机" % idle)
        if self.vfd is not None:
            asyncio.ensure_future(self._safe_stop())

    async def _safe_stop(self):
        try:
            await self.vfd.send_command(devices.VfdM512.CMD_DECEL_STOP)
        except Exception as exc:                        # noqa: BLE001
            self._log("点动超时自动停机失败：%s" % exc)
