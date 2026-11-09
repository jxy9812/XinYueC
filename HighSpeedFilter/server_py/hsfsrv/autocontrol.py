# -*- coding: utf-8 -*-
"""自动运行控制层（**预留骨架，控制逻辑待扩展**）。

================================================================================
                              这个模块是干什么的
================================================================================

系统有两种运行模式：

    手动模式（MANUAL）：人在客户端上点按钮，命令经 XLink 下到服务端 →
                        写设备。**自动模式会拒绝全部远程控制**（双保险：
                        最前置闸 + 设备闸），这是既有的安全契约。

    自动模式（AUTO）  ：服务端**自己**决定怎么动设备（按工艺逻辑），
                        客户端只看不控。

自动模式下"服务端自己怎么动设备"这部分逻辑，就是本模块要生长的地方。
现在它是**空骨架**：每拍被调用一次，但不产生任何动作。

================================================================================
                          为什么现在就建这个文件
================================================================================

[理由一：把"模式"和"控制"分开]
自动模式在 app.py 里目前只是一个**门禁开关**（拒绝远程控制）。真正的自动
控制需要"读反馈 → 决策 → 下发动作"的循环，这是另一层职责。提前把它单独
成模块，等真做逻辑时不必再改动 app.py 的 tick 结构 —— 只要往
`AutoController.step()` 里填代码。

[理由二：接线现在就定死，避免将来各写一套]
下面这些接口是**唯一入口**。将来无论是阀门时序控制、液位联锁、还是
按模拟量阈值启停泵，都从这里访问设备，不直接碰 devices/valve_bank。
好处：动作能被统一记账、统一限速、统一在测试里替换成假的。

    ctx.read_di(index)            读 DI 位（数字量模块缓存）
    ctx.read_do(index)            读 DO 位（读回线圈）
    ctx.write_do(index, on)       写 DO（异步，走设备层）
    ctx.valve_state(index)        读某路阀状态（proto.HsfValveState）
    ctx.open_valve(index)         开阀（异步）
    ctx.close_valve(index)        关阀（异步）
    ctx.analog_ma(channel)        模拟量 mA
    ctx.vfd_snapshot()            变频器快照（只读）
    ctx.temphum()                 (温度℃, 湿度%RH)
    ctx.now_ms()                  单调毫秒

[理由三：安全默认值必须是"什么都不做"]
骨架的 `step()` 直接 return。哪怕有人在配置里误开自动模式、又没写任何
控制逻辑，系统行为也只是"不自动动作"，绝不会因为半成品逻辑乱动现场设备。

================================================================================
                            设计约束（写逻辑时必须遵守）
================================================================================

1. **只在自动模式运行**。手动模式下本模块的 step() 根本不会被调用
   （app 层判过 is_auto()），所以内部不必再判一次；但要记住：切到手动会
   让正在进行的自动动作**直接停摆**，故任何"做到一半的状态"都要能安全
   地停留在原地（见第 4 条）。

2. **动作要有超时与防抖**。阀动作实际需要几秒；每拍（~100ms）都重复下发
   同一条命令会把总线刷爆（该模块对高频连发敏感，实测无间隔连发会吃到
   空应答）。用 `ctx.throttle()` 做节流。

3. **不要在 step() 里阻塞**。它是事件循环的一部分，任何 await 超过几十
   毫秒都会拖慢遥测推送。设备读优先走缓存（`read_di/read_do/valve_state`
   都是缓存读，不发 IO）；需要现读时用 `ctx.refresh()`，它自带节流。

4. **失败要能停在安全侧**。写 DO 失败时不要重试到底把总线打爆，记录错误、
   等下一拍再说。`ctx.last_error` 供上层查询。

5. **决策依据只认反馈，不认命令回显**。命令发出去了只代表"发出去了"，
   阀有没有真动要看行程开关（与 valves.py 同一条口径）。

================================================================================
                              扩展示例（未实现）
================================================================================

液位联锁（伪代码，示意将来怎么用）：

    def step(self, now_ms):
        if not self._enabled:
            return
        # 低液位且出水阀关着 → 开进水阀
        level = self.ctx.analog_ma(0)
        if level < 4.0 and self.ctx.valve_state(1) == Closed:
            self._act(now_ms, "open_valve", 0)
        elif level > 19.0 and self.ctx.valve_state(0) == Open:
            self._act(now_ms, "close_valve", 0)

时序排水（伪代码）：

    if self._phase == "open_drain" and self.ctx.valve_state(2) == Open:
        self._phase = "drain_wait"; self._phase_at = now_ms
    elif self._phase == "drain_wait" and now_ms - self._phase_at > 30000:
        self._act(now_ms, "close_valve", 2); self._phase = "idle"
"""

import asyncio
import collections
import time

from . import proto


# ============================================================================
#                        设备访问上下文（唯一入口）
# ============================================================================

class AutoContext(object):
    """自动控制层访问设备的**唯一通道**。

    [为什么不让自动逻辑直接摸 app.py 的成员]
    app.py 的 digital/valve_bank/analog 是内部装配细节（列表下标、点位映射、
    缓存新鲜度口径全在里面）。自动逻辑只该关心"1 号阀开没开""第 3 路
    DI 通不通"。这层做三件事：
      · 把设备细节挡在外面（将来换模块/换接线只改这里）；
      · 统一节流与错误记录；
      · 让测试能整体替换成假实现（见 tests/test_autocontrol.py）。
    """

    def __init__(self, app, log=None):
        self._app = app
        self._log = log or (lambda msg: None)
        #: 节流表：key -> 上次放行时刻（单调毫秒）。
        #: [必须每实例一份] 曾写成类属性 + 惰性初始化，结果所有实例共享同一
        #: 张表，一个控制器设的节流点会把另一个也挡住。
        self._throttle = {}
        #: 最近一次动作失败原因（空串=无错误）。供上层/测试查询。
        self.last_error = ""
        #: 动作计数（诊断用；也用于测试断言"确实没动过"）。
        self.action_count = 0

    # ---- 时间 ----
    def now_ms(self):
        return int(time.monotonic() * 1000)

    # ---- 数字量（走缓存，不发 IO）----
    def _digital(self, index):
        """取第 index 台数字量模块（自动逻辑按 0 起连续编号）。"""
        mods = getattr(self._app, "digital", None) or []
        if not 0 <= index < len(mods):
            return None
        return mods[index]

    def read_di(self, channel, module=0):
        """读一路 DI。返回 True/False；模块不可用返回 None。"""
        dev = self._digital(module)
        if dev is None or not getattr(dev, "online", False):
            return None
        try:
            bits = dev.di
            ch = int(channel)
            if not 0 <= ch < len(bits):
                return None
            return bool(bits[ch])
        except Exception:                               # noqa: BLE001
            return None

    def read_do(self, channel, module=0):
        """读一路 DO 回显。返回 True/False；模块不可用返回 None。"""
        dev = self._digital(module)
        if dev is None or not getattr(dev, "online", False):
            return None
        try:
            bits = dev.do
            ch = int(channel)
            if not 0 <= ch < len(bits):
                return None
            return bool(bits[ch])
        except Exception:                               # noqa: BLE001
            return None

    async def write_do(self, channel, on, module=0):
        """写一路 DO（异步）。返回 (ok, err)。

        [调用方须知] 这是"命令已受理"，不是"已落定"——真的通没通要下一拍
        用 read_do() 回读确认（该模块的写应答就是原样回显，不代表线圈状态）。
        """
        dev = self._digital(module)
        if dev is None:
            return False, "数字量模块未装配"
        if not getattr(dev, "online", False):
            return False, "数字量模块离线"
        try:
            ok, err = await dev.write_do(int(channel), bool(on))
        except Exception as exc:                        # noqa: BLE001
            err = str(exc)
            ok = False
        if ok:
            self.action_count += 1
            self.last_error = ""
        else:
            self.last_error = err or "写 DO 失败"
            self._log("自动控制：写 DO%d=%d 失败：%s"
                      % (channel, 1 if on else 0, self.last_error))
        return ok, (err or "")

    # ---- 电磁阀 ----
    def _valves(self):
        return getattr(self._app, "valve_bank", None)
    def valve_state(self, index):
        """读某路阀状态（proto.HsfValveState）；未装配返回 None。"""
        bank = self._valves()
        if bank is None:
            return None
        try:
            return int(bank.items[int(index)].state)
        except (IndexError, TypeError, ValueError):
            return None

    def valve_ready(self, index):
        """该路阀点位是否配齐（未配置的路不能自动操作）。"""
        bank = self._valves()
        if bank is None:
            return False
        try:
            return bool(bank.items[int(index)].ready)
        except (IndexError, TypeError, ValueError):
            return False

    async def open_valve(self, index):
        """开阀（异步）。返回 (ok, err)。"""
        return await self._valve_cmd(index, True)

    async def close_valve(self, index):
        """关阀（异步）。返回 (ok, err)。"""
        return await self._valve_cmd(index, False)

    async def _valve_cmd(self, index, want_open):
        bank = self._valves()
        if bank is None:
            return False, "电磁阀组未装配"
        try:
            ok, err = await bank.set_valve(int(index), bool(want_open))
        except Exception as exc:                        # noqa: BLE001
            ok, err = False, str(exc)
        if ok:
            self.action_count += 1
            self.last_error = ""
        else:
            self.last_error = err or "阀操作失败"
            self._log("自动控制：%s阀%d 失败：%s"
                      % ("开" if want_open else "关", index, self.last_error))
        return ok, (err or "")

    # ---- 模拟量 / 变频器 / 温湿度（只读）----
    def analog_ma(self, channel):
        """模拟量通道电流（mA）；不可用返回 None。"""
        dev = getattr(self._app, "analog", None)
        if dev is None:
            return None
        try:
            return float(dev.ma(int(channel)))
        except Exception:                               # noqa: BLE001
            return None

    def vfd_snapshot(self):
        """变频器快照对象（只读；不可用返回 None）。"""
        return getattr(self._app, "vfd", None)

    def temphum(self):
        """(温度℃, 湿度%RH)；不可用返回 (None, None)。"""
        dev = getattr(self._app, "temphum", None)
        if dev is None or not getattr(dev, "online", False):
            return None, None
        try:
            # 设备层存的是 ×0.1 定点（温湿度两路同口径）。
            return dev.temp_tenths() / 10.0, dev.hum_tenths() / 10.0
        except Exception:                               # noqa: BLE001
            return None, None

    # ---- 按钮组 ----
    def button_station(self):
        """按钮组对象（未装配返回 None）。"""
        return getattr(self._app, "button_station", None)

    def estop_ok(self):
        """急停回路是否正常（True=正常，可运行）。

        [三态语义] 返回 True/False/None：
            True  = 回路通（常闭触点未按下，允许启动）
            False = 无信号（按下或断线，**禁止启动**）
            None  = 按钮组未装配或数字量模块离线 —— 调用方必须当成
                    "不确定"并**按禁止处理**（安全侧默认，绝不能当放行）。
        """
        st = self.button_station()
        if st is None:
            return None
        return st.estop_ok

    def estop_configured(self):
        """现场**有没有配**急停回路（按钮组装配且急停点位有效）。

        [与 estop_ok 的分工 —— 决定"拦不拦"] 这是"有没有这道安全件"，
        `estop_ok` 是"这道安全件现在通不通"：

            未配置            → 不拦（没有的东西不能拿它当闸）
            配置了 + True     → 放行
            配置了 + False    → 拦（急停按下/断线）
            配置了 + None     → 拦（模块离线，读不到 = 回路可能断了）

        [为什么必须区分] 2026-11-06 实锅：初版只看 estop_ok，未装操作柱的
        现场（纯 TCP/无数字量）拿到 None 就被当成"禁止"，所有启动/调速全被
        拒，报"急停状态未知"，现场表现为"设备好好的但点不动"。
        """
        st = self.button_station()
        if st is None:
            return False
        try:
            return st._ch("estop_di") is not None
        except Exception:                               # noqa: BLE001
            return False

    def button_state(self):
        """按钮组当前灯态（"idle"/"running"/"estop"）；未装配 None。"""
        st = self.button_station()
        return None if st is None else st.state

    def take_button_edges(self):
        """取走本拍的按键沿（**读后即清**）。

        返回 (start, stop, reset) 三个 bool。没有按钮组时全 False。

        [为什么用 take 语义] 沿只该被消费一次。若用"读"语义，调用方两处都
        读到同一次按下就会执行两次启动/停止。读完立刻清，第二处拿到的必然
        是 False —— 让"一次按键只产生一个动作"成为结构性保证，而不是靠
        调用方自觉。
        """
        st = self.button_station()
        if st is None:
            return False, False, False
        edges = (bool(getattr(st, "start_edge", False)),
                 bool(getattr(st, "stop_edge", False)),
                 bool(getattr(st, "reset_edge", False)))
        st.start_edge = st.stop_edge = st.reset_edge = False
        return edges

    # ---- 节流 ----
    def throttle(self, key, interval_ms, now_ms=None):
        """按 key 做节流：距上次放行不足 interval_ms 返回 False。

        [为什么需要] 自动逻辑每拍（~100ms）跑一次；若每拍都下发同一条命令，
        一条阀动作就是几十帧。该模块对高频连发敏感（实测无间隔连发会吃到
        空应答），必须节流。
        """
        now = self.now_ms() if now_ms is None else now_ms
        last = self._throttle.get(key, 0)
        if last and (now - last) < interval_ms:
            return False
        self._throttle[key] = now
        return True


# ============================================================================
#                              控制器骨架
# ============================================================================

class AutoController(object):
    """自动运行控制器：按钮决策 + 急停/退自动的全停动作。

    生命周期：
        构造 → start()（人按「启动自动运行」或现场启动按钮）→ step() 每拍
             → stop()（人按停止 / 切回手动 / 急停）

    ======================================================================
                      本类就是"按钮逻辑"的家（2026-11-06）
    ======================================================================

    用户口径（原话）：
        「按钮的逻辑应该写在之前预留的自动模块文件中」
        「不是根据物理按钮，是根据设备是否开始自动运行，启动按钮只是给一个
          信号，要满足条件才切换状态」
        「按钮启动这些都是自动模式下的逻辑只能在自动模式下生效，一旦切换
          到手動，为了安全要停止全部设备，并且按钮在自动模式下启动后，
          客户端顶部的状态要显示更新状态」

    逐条落成代码：

    【1】按键只是**请求**，不是命令。
         启动按钮 = 请求进入自动运行；停止按钮 = 请求退出自动运行。
         请求能否成立要过下面全部条件，任何一条不满足就不切状态。

    【2】启动必须同时满足
         · 处于自动模式（手动模式按启动完全无效 —— 这是用户明确要求的
           "只能在自动模式下生效"）
         · 急停回路正常（True；False/None 一律禁止，安全侧默认）
         · 控制器当前没在跑（幂等）

    【3】停止**永远受理**（自动模式与否、急停与否都要能停）。

    【4】运行态由本类决定，写回 ButtonStation（灯跟着它亮）。
         灯说明的是"设备真的在不在自动运行"，不是"你按没按过启动"。

    【5】退出自动 / 急停 ⇒ **停全部设备**（变频器、泵、阀），不只停控制器。
         用户原话："一旦切换到手动，为了安全要停止全部设备"。

    [与旧实现的关键差别] 旧实现的状态机在 `buttons.ButtonStation` 里，按键
    直接推进 running —— 等于按钮就是设备启动按钮，绕过了模式授权。现在
    按键在 buttons 里只产生"沿"，判定全在这里。
    """

    #: 步进周期上限（毫秒）。tick 本身约 100ms；这里只做防御性节流，
    #: 避免将来 tick 频率提高时自动逻辑跑得过密。
    MIN_STEP_INTERVAL_MS = 50

    #: 切回手动/急停时"全停"的等待上限（秒）。超过就记日志不再等。
    #: [为什么设上限] 设备离线时写命令可能一直等到超时；全停是分批投递的
    #: 异步任务，不能无限挂在那儿拖住关闭流程。
    STOP_ALL_TIMEOUT_S = 5.0

    #: 在队报警容量（对齐 C 侧 XAUTO_CONTROL_ALARM_CAPACITY）。满了挤掉
    #: 最旧一条再入队 —— 远端可以丢最老历史，不能丢最新告警。
    ALARM_CAPACITY = 8

    #: 报警文字上限（字节；对齐 C 侧 XAutoControlAlarm.m_text[128]，超长截断，
    #: 且不得把 UTF-8 多字节字符拦腰截断）。
    ALARM_TEXT_MAX_BYTES = 128

    def __init__(self, ctx, log=None):
        self.ctx = ctx
        self._log = log or (lambda msg: None)
        self._running = False
        self._last_step_ms = 0
        #: 相位：idle / running / estop（供 status()/日志阅读）。
        self.phase = "idle"
        #: 急停锁存（与 C 侧 XAutoController 同口径）：急停后必须复位才允许
        #: 再次启动，避免"按住急停松手就自动恢复运行"。由 app 的线协议
        #: 四板斧（AUTO_OP_ESTOP / AUTO_OP_RESET）驱动。
        self._estop_latched = False
        #: 是否**见到过**急停回路正常。见 _check_estop 里"两个必须区分的情形"：
        #: 首帧未到时读不到，不能当成急停锁存（否则开机就锁死）。
        self._estop_ever_ok = False
        self._estop_boot_warned = False
        #: 全停动作的投递记录（诊断：最近一次触发的批次序号）。
        self._stop_all_seq = 0
        #: 在队报警（FIFO）。语义对齐 C 侧 XAutoController 报警面：
        #: raiseAlarm 入队（满挤最旧，永不因满而丢新告警）；takeAlarm 取走
        #: 即消费（一条只该推一次）。app 每拍循环 take 并经 XLink 发布
        #: （TOPIC_AUTO_ALARM），见 app._auto_step。
        self._alarms = collections.deque(maxlen=self.ALARM_CAPACITY)

    # ---- 生命周期 ----
    def start(self, now_ms=None):
        """进入自动运行（幂等）。调用方**必须先自行判定条件**是否满足。

        [为什么不在这里判条件] 条件（模式/急停/锁存）的判定需要上下文，
        分散在两个入口：线协议 `_h_auto_control`（远程命令，要逐条回错因）
        和 `_step_impl`（现场按钮，只记日志）。两边判定条件相同但报错方式
        不同，故由调用方判、这里只做状态迁移。
        """
        if self._running:
            return False
        self._running = True
        self.phase = "idle"
        self._last_step_ms = 0
        self._log("自动运行：已启动")
        self._sync_station()
        return True

    def stop(self, now_ms=None, stop_devices=False, reason=""):
        """退出自动运行（幂等）。

        :param stop_devices: True = 顺带**停全部设备**（切手动/急停走这条）
        :param reason: 日志里写明原因（"切回手动" / "急停" / …）

        [stop_devices 的默认值必须是 False] 线协议上人主动按"停止自动运行"
        只是停控制器，不该顺带把变频器断电 —— 那会让"停止自动运行"变成
        一个比"停设备"更重的动作，反而没人敢按。全停只在**安全场景**
        （切回手动、急停）发生，由调用方显式要求。
        """
        if not self._running:
            # 即使控制器已经停了，安全场景仍要保证设备真停
            # （可能上一次全停部分失败，这里补一次）。
            if stop_devices:
                self.stop_all_devices(reason)
                self._sync_station()
            return False
        self._running = False
        self.phase = "idle"
        self._log("自动运行：已停止%s"
                  % ("（%s）" % reason if reason else ""))
        if stop_devices:
            self.stop_all_devices(reason)
        self._sync_station()
        return True

    # ---- 报警面（对齐 C 侧 XAutoController_raiseAlarm/takeAlarm 语义）----
    def raise_alarm(self, kind, text, now_ms=None):
        """上报一条报警：入队即返回（满则挤掉最旧一条，**永不因满丢新**）。

        :param kind:  报警类型（proto.AUTO_ALARM_*，数值与 C 侧
                      XAutoControlAlarmKind 逐值一致）。
        :param text:  报警文字（给操作员看；超长按 UTF-8 字节截断）。
        :param now_ms: 发生时刻（单调毫秒；缺省取 ctx.now_ms()）。

        [为什么记账在这里] 与 C 同口径：用户逻辑几十个动作点若各自上报，
        "有没有报警/推没推出去"必然漂移；收敛到本入口后，"在队报警"是
        单一事实源，app 每拍统一 take + 发布 + 入库（一条只推一次）。

        [急停报警由框架自动入队] 见 e_stop()：逻辑层不必（也不该）在
        急停路径上再手动 raise 同一条。
        """
        if now_ms is None:
            now_ms = self.ctx.now_ms()
        self._alarms.append({
            "kind": int(kind),
            "atMs": int(now_ms),
            "text": self._clip_text(text or ""),
        })

    def take_alarm(self):
        """取走最旧一条在队报警（**取走即消费**；一条只该推一次）。

        :returns: {"kind":int, "atMs":int, "text":str}；队列空返回 None。

        [为什么用 take 不用读] 与按键沿（ctx.take_button_edges）同一
        理由：事件只该被消费一次。app 循环 take 到空为止，同一条报警
        绝不会出两次帧。
        """
        if not self._alarms:
            return None
        return self._alarms.popleft()

    def pending_alarm_count(self):
        """在队报警条数（诊断/测试用）。"""
        return len(self._alarms)

    @classmethod
    def _clip_text(cls, text):
        """报警文字按 UTF-8 字节截到 ALARM_TEXT_MAX_BYTES（不截断半字符）。"""
        raw = text.encode("utf-8")
        if len(raw) <= cls.ALARM_TEXT_MAX_BYTES:
            return text
        clipped = raw[:cls.ALARM_TEXT_MAX_BYTES]
        # 尾部若截在多字节字符中间，解码会失败 —— 逐字节回退到能解码为止。
        while True:
            try:
                return clipped.decode("utf-8")
            except UnicodeDecodeError:
                clipped = clipped[:-1]

    def e_stop(self, reason="急停"):
        """急停：停控制器 + 停全部设备 + **锁存**（复位前拒绝启动）。

        [报警随迁移自动入队] 对齐 C 侧 XAutoController_eStop：进入急停态
        即 raise EmergencyStop（"自动运行急停触发（等待人工复位）"）。
        已锁存时重复调用**不再重复报警**（C 同款：已是 EmergencyStop 态
        直接 return false）—— 锁存期间每拍 _check_estop 都会再进这里，
        不挡的话告警队列会被同一条急停灌满。
        """
        first = not self._estop_latched
        self._estop_latched = True
        self._running = False
        self.phase = "estop"
        if first:
            # 报警在状态迁移之后入队：app 本拍 take 时快照已含本条。
            self.raise_alarm(proto.AUTO_ALARM_ESTOP,
                             "自动运行急停触发（等待人工复位）")
        self.stop_all_devices(reason)
        self._sync_station()
        self._log("自动运行：急停（已锁存，需复位后才能再次启动）")
        return True

    def reset(self):
        """复位：解除急停锁存，回到停止态（**不重新启动**）。"""
        was = self._estop_latched
        self._estop_latched = False
        self.phase = "idle"
        self._sync_station()
        if was:
            self._log("自动运行：已复位（急停锁存解除，处于停止态）")
        return was

    @property
    def running(self):
        return self._running

    @property
    def estop_latched(self):
        return self._estop_latched

    def _sync_station(self):
        """把**真实运行态**写给按钮组（灯跟着它亮，不跟按键）。"""
        st = self.ctx.button_station()
        if st is not None:
            try:
                st.set_running(self._running)
            except Exception:                           # noqa: BLE001
                pass

    # ---- 全停 ----
    def stop_all_devices(self, reason=""):
        """停全部受控设备：变频器 / 进水泵 / 电磁阀。

        [为什么在框架层做而不是让每个调用点各写一遍] 切手动、急停、退出
        自动三个入口都要做这件事，任何一处漏了就是"手动模式下设备还在转"。
        集中一处，且**逐个设备独立 try** —— 一台失败不能挡住停其它台。

        [为什么不 await] 本方法可能被同步路径调用（step() 是同步入口）。
        全部动作以 asyncio 任务投递，失败只记日志：停机是"尽力而为且必须
        尝试"，不能因为一台设备离线就整批不做。
        """
        self._stop_all_seq += 1
        seq = self._stop_all_seq
        self._log("全停：开始（#%d%s）"
                  % (seq, ("，" + reason) if reason else ""))
        try:
            loop = asyncio.get_event_loop_policy().get_event_loop()
            if loop is None or loop.is_closed():
                raise RuntimeError("无可用事件循环")
        except Exception as exc:                        # noqa: BLE001
            self._log("全停：无法投递（%s），改为同步尝试" % exc)
            loop = None

        async def _run_all():
            stopped = []
            failed = []
            # 1) 变频器：减速停机（保留停机过程，不用自由停车 —— 现场泵
            #    是搅拌设备，瞬断对机械冲击更大）
            vfd = self.ctx.vfd_snapshot()
            if vfd is not None:
                try:
                    ok, err = await vfd.apply_control(
                        proto.OP_STOP, False, 0.0, 0.0, 0.0, 0)
                    (stopped if ok else failed).append(
                        "变频器" + ("" if ok else "（%s）" % err))
                except Exception as exc:                # noqa: BLE001
                    failed.append("变频器（%s）" % exc)
            # 2) 进水泵：逐台关
            for i, ok_err in enumerate(await self._stop_pumps()):
                ok, err = ok_err
                (stopped if ok else failed).append(
                    "进水泵%d%s" % (i, "" if ok else "（%s）" % err))
            # 3) 电磁阀：逐路关（不判到位，只发关命令）
            for i, ok_err in enumerate(await self._close_valves()):
                ok, err = ok_err
                (stopped if ok else failed).append(
                    "阀%d%s" % (i, "" if ok else "（%s）" % err))
            self._log("全停：完成（#%d）已停 %s%s"
                      % (seq, "、".join(stopped) or "无设备",
                         ("；失败 " + "、".join(failed)) if failed else ""))
            if failed:
                # [全停失败必须让人看见] 停机失败是安全事件：设备层只记
                # last_error（被下一笔成功覆盖），这里补一条 ActionFailed
                # 报警进队列（app 每拍 take → 发布/入库），远端与告警历史
                # 都能查到"哪次全停没停干净"。对齐 C 侧"是否额外 raise
                # Alarm(ActionFailed) 由逻辑层决定"的设备语义归属。
                self.raise_alarm(
                    proto.AUTO_ALARM_ACTION_FAILED,
                    "全停部分失败（#%d）：%s" % (seq, "、".join(failed)))

        if loop is None:
            return
        try:
            task = loop.create_task(_run_all())
            self._track_stop_task(task)
        except Exception as exc:                        # noqa: BLE001
            self._log("全停：投递失败 %s" % exc)

    async def _stop_pumps(self):
        """逐台停泵，返回 [(ok, err), ...]（未装配返回空表）。"""
        bank = getattr(self.ctx._app, "pump_bank", None)
        if bank is None:
            return []
        out = []
        for i in range(len(bank.items)):
            try:
                ok, err = await bank.set_pump(i, False)
            except Exception as exc:                    # noqa: BLE001
                ok, err = False, str(exc)
            out.append((ok, err))
        return out

    async def _close_valves(self):
        """逐路关阀，返回 [(ok, err), ...]（未装配返回空表）。"""
        bank = self.ctx._valves()
        if bank is None:
            return []
        out = []
        for i in range(len(bank.items)):
            if not self.ctx.valve_ready(i):
                continue
            try:
                ok, err = await bank.set_valve(i, False)
            except Exception as exc:                    # noqa: BLE001
                ok, err = False, str(exc)
            out.append((ok, err))
        return out

    def _track_stop_task(self, task):
        """留住全停任务引用（防被 GC 提前回收），完成时自动摘掉。"""
        tasks = getattr(self, "_stop_tasks", None)
        if tasks is None:
            tasks = self._stop_tasks = set()
        tasks.add(task)
        try:
            task.add_done_callback(tasks.discard)
        except Exception:                               # noqa: BLE001
            pass

    # ---- 每拍 ----
    def step(self, now_ms):
        """每拍入口：**按钮请求 → 条件判定 → 状态迁移**。

        约定：
          · 只在 self._running 为真时被 app 调用，但**急停/停止按钮的受理
            不依赖 _running** —— 见 `poll_idle()`（app 每拍都会调它）。
          · 不要 await（这是同步入口）；需要异步动作时用
            `asyncio.ensure_future(...)` / `loop.create_task(...)` 投递；
          · 任何异常都必须自己吞掉 —— 自动逻辑崩了不能把整个服务端带死。
        """
        if not self._running:
            return
        if (self._last_step_ms
                and now_ms - self._last_step_ms < self.MIN_STEP_INTERVAL_MS):
            return
        self._last_step_ms = now_ms

        try:
            self._step_impl(now_ms)
        except Exception as exc:                        # noqa: BLE001
            # 自动逻辑异常绝不能外泄到 tick（会把采集/推送一起带死）
            self.ctx.last_error = "%s: %s" % (type(exc).__name__, exc)
            self._log("自动控制：本拍异常 %s" % self.ctx.last_error)

    def poll_idle(self, now_ms, auto_mode):
        """每拍受理按键（**无论控制器在不在跑**都由 app 调）。

        :param auto_mode: 当前是不是自动模式（手动模式下启动请求一律不生效）

        [为什么与 step() 分开] step() 只在自动运行中被调用；但**停止**按钮
        和**急停**必须在任何时刻都能受理（包括控制器没跑的时候）。如果只有
        step() 受理按键，"没在跑时按停止"就没人接，"急停"更没人接 —— 那是
        安全事故。故拆成两个入口：本方法管"请求受理"，step() 管"跑起来之后
        的工艺逻辑"。
        """
        try:
            self._handle_buttons(now_ms, auto_mode)
            self._check_estop(auto_mode)
        except Exception as exc:                        # noqa: BLE001
            self.ctx.last_error = "%s: %s" % (type(exc).__name__, exc)
            self._log("自动控制（按键受理）：本拍异常 %s" % self.ctx.last_error)

    # ---- 按键受理 ----
    def _handle_buttons(self, now_ms, auto_mode):
        """把按键沿翻译成"启动/停止自动运行"的请求，逐个判条件。"""
        start_edge, stop_edge, reset_edge = self.ctx.take_button_edges()

        # ---- 停止：无条件受理（停机类动作不该被任何闸门挡住）----
        if stop_edge:
            if self._running:
                self.stop(now_ms, stop_devices=True,
                          reason="现场停止按钮")
            else:
                # 没在跑也照样停一次设备：现场按停止是"我要设备停"，
                # 不是"我要控制器状态对齐"。
                self.stop_all_devices("现场停止按钮")
                self._sync_station()
                self._log("自动运行：未在运行，现场停止按钮仍执行全停")

        # ---- 复位：解除急停锁存（不启动）----
        if reset_edge:
            self.reset()

        # ---- 启动：必须同时满足全部条件 ----
        if start_edge:
            ok, why = self.start_preconditions(auto_mode)
            if ok:
                changed = self.start(now_ms)
                self._log("自动运行：现场启动按钮受理（%s）"
                          % ("已启动" if changed else "已在运行"))
            else:
                self._log("自动运行：现场启动按钮被拒 —— %s" % why)

    def start_preconditions(self, auto_mode):
        """启动的全部条件。返回 (ok, 拒绝原因)。

        [条件顺序] 先判模式再判急停：模式不对是"操作员没切"（配置问题），
        急停不对是"安全回路"（安全问题）。先报配置问题更好理解；两条都
        不满足时现场看到的提示应该是"去切自动模式"，而不是"急停无信号"
        （后者会让人以为急停坏了）。
        """
        if not auto_mode:
            return False, "当前是手动模式（按钮启停只在自动模式下生效）"
        if self._estop_latched:
            return False, "急停已锁存，需先复位"
        # [只拦"配了"的现场] 见 estop_configured 的说明：没配急停回路就
        # 没有这道安全件，不能拿它当闸（否则未装操作柱的现场永远起不来）。
        if not self.ctx.estop_configured():
            return True, ""
        estop = self.ctx.estop_ok()
        if estop is not True:
            return False, ("急停无信号（按下或断线）" if estop is False
                           else "急停状态未知（数字量模块离线）")
        return True, ""

    def _check_estop(self, auto_mode):
        """急停闸：无信号时立刻停控制器 + 停全部设备 + **锁存**（每拍检查）。

        [锁存与 running 无关 —— 2026-11-06 实锅] 初版只在控制器**正在跑**时
        才调 e_stop（也就是才锁存）。于是"没在跑的时候按下急停"不锁存，
        回路恢复后按启动立刻就能跑 —— 但现场明明是按过急停的，规矩是
        **必须人工复位确认**。急停是安全事件，跟当时在不在跑无关。

        [⚠ 两个必须区分的情形 —— 2026-11-06 板端实锅]
            ① 真实急停事件（有人按下/线断了）：必须锁存，等人工复位。
            ② 服务端刚启动、数字量还没采到第一帧：DI 读不到是**暂时的**，
               不是急停。此时若锁存，现场会遇到「刚开机就按不动、必须
               先按一次复位」——而且日志里那一下"急停锁存"是编造出来的
               安全事件，会让人去查根本不存在的急停回路（板端实测就是
               这个现象：重启后立刻 estop 锁存，之后 DI12=1 也解不开）。

        判据：**从没见过急停=正常**之前，只按"禁止启动"处理，**不锁存**。
        `_estop_ever_ok` 一旦为真（见到过回路通），后面所有无信号都按
        真实急停处理，锁存照旧。
        """
        if not self.ctx.estop_configured():
            # 没配急停回路：没有这道安全件，不能拿它当闸（同 start_preconditions）。
            if self.phase == "estop" and not self._estop_latched:
                self.phase = "idle"
                self._sync_station()
            return
        estop_ok = self.ctx.estop_ok()
        if estop_ok is True:
            self._estop_ever_ok = True
            if self.phase == "estop" and not self._estop_latched:
                self.phase = "idle"
                self._sync_station()
            return
        # False（无信号）与 None（配了但读不到/离线）都按禁止处理。
        reason = ("急停无信号（按下或断线）" if estop_ok is False
                  else "急停状态未知（数字量模块离线）")
        if not self._estop_ever_ok:
            # 情形②：还没见过第一帧，不锁存、不一定全停（控制器本来也
            # 起不来 —— start_preconditions 会拒）。只把相位记成"等待采集"。
            if not getattr(self, "_estop_boot_warned", False):
                self._estop_boot_warned = True
                self._log("自动控制：%s（首帧未到，暂不锁存；"
                          "采到急停正常后才按真实急停处理）" % reason)
            return
        if not self._estop_latched:
            self._log("自动控制：%s ⇒ 停止自动运行并全停设备" % reason)
            # [急停触发点] 报警在本方法调用的 e_stop() 里自动入队
            # （EmergencyStop），app._auto_step 每拍抽取发布 —— 这里不重复 raise。
            self.e_stop(reason)

    def _step_impl(self, now_ms):
        """运行中的工艺逻辑（异常由 step() 兜住）。

        [当前实现] 工艺逻辑仍是空的（等现场工艺定），本方法只维护相位标记，
        供 status() 与日志阅读。急停闸已在 `poll_idle` → `_check_estop` 里
        每拍执行（它必须在控制器没跑时也生效），这里不重复。
        """
        if self.phase != "running":
            self.phase = "running"
            self._log("自动控制：进入运行相位")

    # ---- 诊断 ----
    def status(self):
        """当前状态（供日志/健康/测试查询）。"""
        st = self.ctx.button_station()
        snap = st.snapshot() if st is not None else None
        return {
            "running": self._running,
            "phase": self.phase,
            "estopLatched": self._estop_latched,
            "actions": self.ctx.action_count,
            "lastError": self.ctx.last_error,
            "pendingAlarms": self.pending_alarm_count(),
            "estopOk": self.ctx.estop_ok(),
            "buttonState": (None if st is None else st.state),
            "buttons": snap,
        }
