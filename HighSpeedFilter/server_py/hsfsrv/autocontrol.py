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

    def button_state(self):
        """按钮组状态机当前状态（"idle"/"running"/"estop"）；未装配 None。"""
        st = self.button_station()
        return None if st is None else st.state

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
    """自动运行控制器（**控制逻辑待扩展**）。

    生命周期：
        构造 → start()（进入自动模式时）→ step() 每拍 → stop()（退出时）

    [当前行为] step() 什么都不做。这不是遗漏 —— 见模块头「安全默认值」。
    """

    #: 步进周期上限（毫秒）。tick 本身约 100ms；这里只做防御性节流，
    #: 避免将来 tick 频率提高时自动逻辑跑得过密。
    MIN_STEP_INTERVAL_MS = 50

    def __init__(self, ctx, log=None):
        self.ctx = ctx
        self._log = log or (lambda msg: None)
        self._running = False
        self._last_step_ms = 0
        #: 扩展位：将来放状态机相位（见模块头示例）。
        self.phase = "idle"

    # ---- 生命周期 ----
    def start(self, now_ms=None):
        """进入自动模式时调用（幂等）。"""
        if self._running:
            return
        self._running = True
        self.phase = "idle"
        self._last_step_ms = 0
        self._log("自动控制：已启动（本版本控制逻辑为空，不做任何动作）")

    def stop(self, now_ms=None):
        """退出自动模式时调用（幂等）。

        [刻意不做"收尾动作"] 切手动时**不**自动把阀关掉或把泵停掉——
        那会在操作员手里抢控制权，且"关到哪一步"取决于工艺。自动逻辑
        若需要收尾，应自己在 stop() 之前显式做（将来扩展时实现）。
        """
        if not self._running:
            return
        self._running = False
        self.phase = "idle"
        self._log("自动控制：已停止")

    @property
    def running(self):
        return self._running

    # ---- 每拍 ----
    def step(self, now_ms):
        """每拍入口：**急停闸门 + 状态跟随**。

        约定：
          · 只在 self._running 为真时被 app 调用（但内部再判一次也无害）；
          · 不要 await（这是同步入口）；需要异步动作时用
            `asyncio.ensure_future(ctx.write_do(...))` 投递；
          · 任何异常都必须自己吞掉 —— 自动逻辑崩了不能把整个服务端带死。

        ======================================================================
                          当前实现的工艺逻辑（2026-11-06）
        ======================================================================

        用户的现场口径，逐条落成代码：

        【1】急停无信号 ⇒ **禁止启动**
             急停是常闭触点：DI=0 表示按下或断线。此时不准启动、不准调速，
             已有的运行也要被打停。这是最前置的闸门，先于任何其它判断。

        【2】启动进入运行状态 ⇒ 启动灯常亮
        【3】停止状态下停止灯常亮
        【4】运行状态停止灯熄灭，与启动灯**互斥**
        【5】复位灯是点动：按下复位才亮，松开就灭

        [职责边界] 灯的实际**写入**在 `buttons.ButtonStation` 里（它每拍
        拿着硬件状态做这件事，手动模式下也照样亮）。本方法负责的是
        **自动侧的决策与闸门**：
            · 急停无效时把自动运行打停（不让泵继续转）
            · 把"系统处于什么状态"记进 self.phase 供 status()/日志使用

        [为什么不在这儿写灯] 灯要跟随的是 `ButtonStation` 的状态机，而那个
        状态机由**物理按键**驱动（DI 沿），不是由自动逻辑驱动。两边都写灯
        会互相打架。自动逻辑只在"急停"这种**必须介入**的场合出手。
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

    def _step_impl(self, now_ms):
        """真正的逻辑（异常由 step() 兜住）。"""
        st = self.ctx.button_station()

        # ---- 闸门：急停 ----
        estop_ok = self.ctx.estop_ok()
        if estop_ok is not True:
            # False（无信号）和 None（按钮组未装配 / 模块离线）都按**禁止**
            # 处理。这是安全侧默认：宁可不动，也不能在"不知道急停状态"时
            # 让设备转起来。
            if self.phase != "estop":
                reason = ("无信号（按下或断线）" if estop_ok is False
                          else "状态未知（按钮组未装配或模块离线）")
                self._log("自动控制：急停%s ⇒ 禁止运行" % reason)
                self.phase = "estop"
            return

        # 急停正常：相位跟随按钮组状态机（物理按键推进的那个）
        if st is not None:
            if st.state == "running":
                if self.phase != "running":
                    self.phase = "running"
                    self._log("自动控制：进入运行状态（启动灯常亮）")
            elif st.state == "idle":
                if self.phase != "idle":
                    self.phase = "idle"
                    self._log("自动控制：停止状态（停止灯常亮）")
        elif self.phase != "idle":
            # 没有按钮组（纯 TCP 场景）：自动模式下就当"运行中"，保持
            # 原有语义（自动模式本身就是运行状态）。
            self.phase = "running"

    # ---- 诊断 ----
    def status(self):
        """当前状态（供日志/健康/测试查询）。"""
        st = self.ctx.button_station()
        snap = st.snapshot() if st is not None else None
        return {
            "running": self._running,
            "phase": self.phase,
            "actions": self.ctx.action_count,
            "lastError": self.ctx.last_error,
            "estopOk": self.ctx.estop_ok(),
            "buttonState": (None if st is None else st.state),
            "buttons": snap,
        }
