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
            return False, "电动球阀组未装配"
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
        """变频器快照（只读；不可用返回 None）。"""
        return getattr(self._app, "vfd", None)

    def load_run_ms(self):
        """读已持久化的**累计自动运行时长**（毫秒；服务端重启后恢复用）。"""
        try:
            return int(self._app.cfg.auto_run_ms())
        except Exception:                           # noqa: BLE001
            return 0

    def persist_run_ms(self, total_ms):
        """把累计自动运行时长持久化（cfg 的 runtime 节，原子落盘）。"""
        try:
            self._app.cfg.save_auto_run_ms(int(total_ms))
        except Exception as exc:                    # noqa: BLE001
            self._log("累计运行时长持久化失败：%s" % exc)

    def load_mode(self):
        """读已持久化的**粘性自动模式**（服务端重启后恢复给物理启动键用）。"""
        try:
            return self._app.cfg.auto_mode()
        except Exception:                           # noqa: BLE001
            return ""

    def persist_mode(self, mode):
        """把粘性自动模式持久化（cfg 的 runtime 节，原子落盘）。

        [2026-11-13 板端实锅] 物理启动按钮用「上次选中的」（粘性）；粘性
        只存内存，服务端重启即清空 ⇒ 现场按启动被拒「未选择自动模式」
        （界面下拉显示的是客户端本地缺省，与服务端粘性无关）。落盘后
        重启恢复上次模式，物理键开箱即用。
        """
        try:
            self._app.cfg.save_auto_mode(str(mode or ""))
        except Exception as exc:                    # noqa: BLE001
            self._log("粘性模式持久化失败：%s" % exc)

    def press_mpa(self):
        """压差计读数（**MPa**；不可用返回 None）。

        [口径与界面/历史同源] 模拟量通道 0 经 cfg.analog.scales 的量程
        换算（4~20mA → MPa）—— 与 app._history_snapshot 的 press_mpa、
        首页压差卡同一条换算链，避免自动循环拿"mA 原始值"当阈值比。
        """
        ai = getattr(self._app, "analog", None)
        if ai is None:
            return None
        try:
            if not getattr(ai, "valid", None) or not ai.valid[0]:
                return None
            scales = getattr(getattr(self._app, "cfg", None), "analog", None)
            scale = scales.scales.get(0) if scales is not None else None
            if scale is None or not getattr(scale, "configured", False):
                return None
            value, _note = scale.engineering(ai.ma(0))
            return float(value)
        except Exception:                           # noqa: BLE001
            return None

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
    def is_auto(self):
        """当前是不是自动模式（手动 ⇄ 自动 是人拨的开关）。

        [为什么自动逻辑层要问] 「停止/急停时要不要关阀复位」这条口径在
        自动模式看**逐模式配置**（自动模式有工艺序列，阀可能正停在中间位）；
        在**手动模式**一律关阀复位（用户口径 2026-11-15：手动模式下按
        停止/急停要把设备与球阀全部停下来）。故这两个口径必须知道当前
        是哪个模式。

        [取不到时按**自动模式**处理] app 没暴露 is_auto（老夹具/异常）时
        返回 True —— 保持既有"逐模式配置"口径，不静默改成"手动模式强制
        关阀"（那会让测试夹具的阀复位行为悄悄变样）。
        """
        try:
            return bool(self._app.is_auto())
        except Exception:                               # noqa: BLE001
            return True

    def manual_running(self):
        """手动模式的「在动」判定（按钮组启动灯口径；2026-11-15 用户口径）。

        [口径] 现场还有设备在动即返回 True：
            · **搅拌泵**（变频器）在转 —— 遥测说在跑，或输出频率还 > 0
              （停机斜坡期间频率还没归零，电机确实还在转）；
            · **进水泵**在开（命令态；泵无反馈点，输出即状态）；
            · **任意一路电动球阀在开位** —— 开到位反馈为真，或最近一条命令
              是"开"（含正在开到位途中）。

        [与 C 端同口径] C 侧 HsfServerAutoMode.c 的 amManualRunning()
        逐条对应（VFD 遥测有效 ∧（运行中 ∨ 输出频率 > 0）；泵命令态；
        阀 = 开到位反馈 ∨ 开命令）。

        [为什么这样判"阀开着"] 阀动起来之后**两个到位反馈都不吸**（动作中，
        行程 ~9s）⇒ 单看反馈会漏掉"正在开到位途中"那 9s ⇒ 用"开到位 ∨
        开命令"兜住。命令侧（open_cmd）由 set_valve 与"外部直接写 DO 被
        接管"两条路径维护，与现场实际意图一致；下了关命令它就清零 ⇒ 灯灭。
        [为什么不用"还没到关位"] 现场只有 1 号阀接了行程开关（其余 5 路无
        反馈）⇒ "没到关位"会把 5 口没接线的阀判成"永远开着" ⇒ 灯常亮。
        """
        # 1) 搅拌泵（变频器）
        vfd = self.vfd_snapshot()
        if vfd is not None and self._vfd_telemetry_valid(vfd):
            try:
                if vfd.is_running():
                    return True
                hz = vfd.output_freq_hz()
                if hz is not None and float(hz) > 0.0:
                    return True
            except Exception:                           # noqa: BLE001
                pass
        # 2) 进水泵（命令态即状态：泵没有反馈点）
        bank = self.pump_bank()
        if bank is not None:
            for item in getattr(bank, "items", None) or []:
                try:
                    if bool(getattr(item, "on", False)):
                        return True
                except Exception:                       # noqa: BLE001
                    continue
        # 3) 电动球阀（开到位 ∨ 已下过开命令）
        vbank = self._valves()
        if vbank is not None:
            for item in getattr(vbank, "items", None) or []:
                try:
                    if (bool(getattr(item, "open_fb", False))
                            or bool(getattr(item, "open_cmd", False))):
                        return True
                except Exception:                       # noqa: BLE001
                    continue
        return False

    def _vfd_telemetry_valid(self, vfd):
        """变频器遥测是否**新鲜有效**（与 C 端 `vs->m_valid` 同口径）。

        [为什么必须有 2026-11-15 审计] 变频器掉线后，设备对象里**最后一帧
        遥测还留着**（输出频率 25Hz、状态字"运行中"）⇒ 用它判"在动"会把
        启动灯**永久钉亮**：现场什么设备都没在跑，灯却亮着（这正是
        2026-11-15 用户报的现象的另一半）。C 端有 `m_valid`（快照有效性）
        ⇒ 这边按同样的口径补上：遥测超过新鲜阈值（或从站失联）就**不认**
        它的运行状态。

        [判不出来时怎么办] 装配不齐（没有 now_ms / stale 阈值）时**放行**
        （沿用旧口径）——不能因为"判不了"就把"在动"抹掉（那会让灯该亮时
        不亮，比误亮更危险）。
        """
        app = getattr(self, "_app", None)
        cfg = getattr(app, "cfg", None)
        if app is None or cfg is None:
            return True
        try:
            now_ms = int(app._now_ms())
            stale_ms = int(getattr(cfg, "vfd_stale_ms", 0) or 0)
            health, _code, _online, _text = vfd.health(now_ms, stale_ms)
        except Exception:                               # noqa: BLE001
            return True
        return health in (proto.HEALTH_OK, proto.HEALTH_FAULT)

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

    # ---- 自动模式配置 / 泵 / 阀数（模式序列用，2026-11-11）----
    def auto_mode_config(self, mode):
        """取一个模式的配置副本 {devices, stir_rpm, stir_reverse}；无则空表。"""
        cfg = getattr(self._app, "cfg", None)
        try:
            m = cfg.auto.get(mode)
            return dict(m) if m else {}
        except Exception:                               # noqa: BLE001
            return {}

    def valve_count(self):
        """电动球阀路数（未装配返回 0）。"""
        bank = self._valves()
        try:
            return len(bank.items)
        except Exception:                               # noqa: BLE001
            return 0

    def pump_bank(self):
        """进水泵组（未装配返回 None）。"""
        return getattr(self._app, "pump_bank", None)

    def valve_move_timeout_ms(self):
        """阀动作超时口径（配置值；与手动阀控同一来源，读取失败回 20s）。"""
        try:
            return int(self._app.cfg.valve_move_timeout_ms)
        except Exception:                               # noqa: BLE001
            return 20000

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

    # ---- 模式序列相位（2026-11-11 三模式）----
    #: 相位值经 KEY_AUTO_PHASE 上报客户端（主界面模式卡显示）。
    PHASE_IDLE = "idle"          #: 停止态
    PHASE_PAUSED = "paused"      #: 暂停态（2026-11-19 停止＝暂停；按启动续跑）
    PHASE_VALVES = "valves"      #: 阀相：开选中阀/关未选阀，等全部到位
    PHASE_PUMPS = "pumps"        #: 泵相：按配置启动搅拌泵/进水泵
    PHASE_RUNNING = "running"    #: 运行相：设备保持运行，等停止/急停
    PHASE_ESTOP = "estop"        #: 急停态（锁存，复位回 idle）

    #: 阀相在「阀层动作超时」之外再等的宽限（毫秒）。阀层自己会按
    #: move_timeout_ms 判 Fault（带 VALVE_ALARM 告警）；本层盯的是
    #: 「序列是否推进」，多留一点余量再判本层的序列超时，避免两处
    #: 几乎同时报两条含义相近的告警。
    VALVE_PHASE_GRACE_MS = 2000

    #: 阀命令失败的节流重发间隔（毫秒）：下发失败（设备离线抖动）不放弃
    #: 整个模式，等一会再试；连发会打爆总线（模块头设计约束 2）。
    VALVE_CMD_RETRY_MS = 2000

    #: [泵相宽限 2026-11-13·对齐 C AM_PUMP_PHASE_GRACE_MS=4000] 泵写路径
    #: 的前置在线判定是**单拍瞬时值**：总线一轮停滞的抖动窗会把"可写"
    #: 误报成"离线"，一次性失败即中止会把可跑的生产误杀。与阀相同款纪律：
    #: 首败起算宽限、留在泵相逐拍重投（重建任务重发），过限才按与一次性
    #: 失败相同的文字/类别（ACTION_FAILED）中止。C 端 HsfServerAutoMode.c
    #: 已是此口径，Python 端此前一次性下发、任一失败立即中止——两端对齐。
    PUMP_PHASE_GRACE_MS = 4000

    def __init__(self, ctx, log=None):
        self.ctx = ctx
        self._log = log or (lambda msg: None)
        self._running = False
        self._last_step_ms = 0
        #: 相位：idle / valves / pumps / running / estop（PHASE_*，供
        #: status()/KEY_AUTO_PHASE/日志阅读）。
        self.phase = self.PHASE_IDLE
        #: 急停锁存（与 C 侧 XAutoController 同口径）：急停后必须复位才允许
        #: 再次启动，避免"按住急停松手就自动恢复运行"。由 app 的线协议
        #: 四板斧（AUTO_OP_ESTOP / AUTO_OP_RESET）驱动。
        self._estop_latched = False
        #: 是否**见到过**急停回路正常。见 _check_estop 里"两个必须区分的情形"：
        #: 首帧未到时读不到，不能当成急停锁存（否则开机就锁死）。
        self._estop_ever_ok = False
        self._estop_boot_warned = False
        #: 急停"状态未知"（None）的连续拍计数（见 _check_estop：
        #: 持续 ESTOP_NONE_LATCH_TICKS 拍才锁存，单拍读不算急停）。
        self._estop_none_streak = 0
        #: **累计自动运行时长**（毫秒；2026-11-12 用户定案：主界面显示
        #: 「自动运行 天 时 分 秒」）。
        #:
        #: [为什么在服务端计时] 客户端计时会因**断线/重启/换客户端**失真
        #: （客户端重启后本地计数从 0 开始，两端数字对不上）。服务端是唯一
        #: 事实源：控制器自己累加，客户端只显示。
        #:
        #: [只在 running 时累加] _run_accum_ms 是"已结算"的累计；running
        #: 期间的那一段用 _run_start_ms 到现在的差值补上（run_ms() 算）。
        #: 停止/急停时把这一段结算进累计（多次运行**累计**而非单次）。
        self._run_accum_ms = 0
        self._run_start_ms = 0
        #: 全停动作的投递记录（诊断：最近一次触发的批次序号）。
        self._stop_all_seq = 0
        #: 在队报警（FIFO）。语义对齐 C 侧 XAutoController 报警面：
        #: raiseAlarm 入队（满挤最旧，永不因满而丢新告警）；takeAlarm 取走
        #: 即消费（一条只该推一次）。app 每拍循环 take 并经 XLink 发布
        #: （TOPIC_AUTO_ALARM），见 app._auto_step。
        self._alarms = collections.deque(maxlen=self.ALARM_CAPACITY)

        # ---- 模式序列状态（2026-11-11 三模式）----
        #: 本轮**正在跑**的自动模式 id（produce/backwash/rinse/drain）；
        #: 组合循环时是**当前子阶段**。
        self.mode = ""
        #: **用户选中的模式**（= C 端 s_mode；2026-11-18 板端实锅新增）。
        #:
        #: [为什么要与 self.mode 分账] 老口径把「粘性（下次启动按哪个模式
        #: 跑）」与「本轮运行中的模式/循环子阶段」挤在同一个 self.mode ⇒
        #:   · 运行中改下拉被拒（op=5 怕改写正在跑的子阶段）⇒ 粘性停在
        #:     旧值 ⇒ **现场启动按钮按旧模式启动**（而屏幕上的启动带
        #:     模式下拉 ⇒ 两者不一致，现场以为按钮坏了）；
        #:   · 组合循环跑到一半时 self.mode 是子阶段，粘性信息丢失。
        #: C 端早就分账（s_mode / s_cycleStage）⇒ 本端口径向 C 端对齐：
        #: selected_mode 只记「用户的选择」（op=5 记它、落盘它），
        #: **现场启动按钮用它**；self.mode 只管运行中。
        #: 空串 = 从未选过（现场启动被拒，提示先在客户端选模式）。
        self.selected_mode = ""
        #: 报警锁存：模式序列报警（阀超时/设备不可用/启动失败）后必须按
        #: 复位才能再启动（用户口径「有报警没复位的话，不能启动」）。
        self._fault_latched = False
        #: 阀相目标表 {valve_index: want_open}（start() 时按配置定型）。
        self._valve_targets = {}
        #: 已收到"命令受理"的阀（避免每拍重发；失败阀按 VALVE_CMD_RETRY_MS
        #: 节流重发）。
        self._valve_cmd_acked = set()
        #: 本相位起始时刻（单调毫秒；阀相超时判定用）。
        self._phase_at = 0
        #: 阀相截止时刻（start() 时按 move_timeout+宽限定型）。
        self._valve_deadline_ms = 0
        #: 泵相的下发状态：None=未下发；否则是 [Task] 列表（等结果）。
        self._pump_tasks = None
        #: [泵相宽限 2026-11-13·对齐 C AM_PUMP_PHASE_GRACE_MS] 泵写路径的
        #: 前置在线判定是**单拍瞬时值**（总线一轮停滞的抖动窗会把"可写"
        #: 误报成"离线"）：首败起算宽限、留在泵相逐拍重投，过限才按
        #: ACTION_FAILED 中止。0 = 未在宽限中。
        self._pump_deadline_ms = 0
        #: 宽限期首败原因（过限中止时用它，与 C s_pumpFirstErr 同口径——
        #: 重试期间错误会刷，中止文案报**第一因**）。
        self._pump_first_err = ""
        #: 模式序列的异步任务（阀命令/泵启动），防 GC 并在完成时自动摘除。
        self._phase_tasks = set()
        #: [换相策略 2026-11-14] 走「保持上一模式状态」的泵，且**新模式
        #: 不选它** ⇒ 换相途中它仍在转 ⇒ 泵相必须把它停掉（否则一直转）。
        #: 存设备 id（"stirPump"/"inletPump"）。
        self._carry_pending_stop = set()
        # ---- 组合模式「自动」的循环调度（2026-11-12 用户定案）----
        #: 本轮是不是组合模式（"auto"）；组合模式下 self.mode 是**当前子
        #: 阶段**（produce/backwash/rinse），界面显示的也是它。
        self._cycle_root = ""
        #: 当前子阶段起始时刻（单调毫秒）；各子阶段的定时从它起算。
        self._cycle_stage_at = 0
        #: 循环配置快照（启动时取一次；运行期改配置不打断当前循环，
        #: 下一轮启动生效 —— 与"模式配置运行期生效、下次启动用"的口径一致）。
        self._cycle_cfg = {}
        #: **记住的循环子阶段**（2026-11-12 用户定案「停止/急停后从停止的
        #: 状态继续运行，除非按了复位」）：
        #:   · 停止/急停时把当前子阶段记在这里；
        #:   · 下次启动组合模式时**从它继续**（不是从头回到产水）；
        #:   · 按「复位」清空 ⇒ 下次从头开始。
        #: [为什么记在控制器里而不是客户端] 服务端是唯一事实源：客户端
        #: 重启/换端都不该丢"流程走到哪一步了"。
        self._cycle_resume_mode = ""
        # ---- 暂停态（停止＝暂停）2026-11-19 用户口径 ----
        #: 「自动模式下**不管是哪个模式运行**，按下停止后都要**保留停止时候
        #: 的状态**，之后按启动是**继续运行**，而不是从头开始，**特别是
        #: 定时器时间要保留**，除非按过**复位按钮**（屏幕上的或实体的）」。
        #:
        #: [为什么要另立一套状态] 旧口径只有 `_cycle_resume_mode`（**只记
        #: 组合循环的子阶段**）且**不记计时** ⇒ ① 单模式根本不记（口径说
        #: "不管是哪个模式"）② 组合循环恢复时阶段计时**从 0 重来**
        #: （"定时器时间没保留"的根因：`_clear_phase_state()` 把
        #: `_cycle_stage_at` 清了，`start()` 又重新取 now）。
        #:
        #: [记三样] ① `_pause_root`：暂停时的**根模式**（"auto" 组合循环
        #: / 单模式 id）；② `_pause_mode`：暂停时的**模式**（组合循环时＝
        #: 子阶段，单模式时＝模式本身）；③ `_pause_elapsed_ms`：暂停**那一
        #: 刻**该阶段的**已用时长**（毫秒）—— 续跑时**接着走**的关键。
        self._paused = False
        self._pause_root = ""
        self._pause_mode = ""
        self._pause_elapsed_ms = 0
        # ---- 累计自动运行时长：**启动时从持久化恢复** ----
        # （2026-11-12 用户定案「这个时间要服务端持久化」：服务端重启不
        # 归零；写 cfg 的 runtime 节，与 run_mode 同一套路。）
        self._run_accum_ms = self.ctx.load_run_ms()
        self._run_start_ms = 0
        #: 运行中周期性落盘的间隔（毫秒）：断电/崩溃最多丢这个窗口的时长。
        self.RUN_PERSIST_MS = 60000
        # [粘性模式恢复 2026-11-13] 重启后从持久化恢复上次选中的模式——
        # 否则现场启动按钮被拒「未选择自动模式」（物理键路径用粘性）。
        restored = self.ctx.load_mode()
        if restored:
            self.mode = restored
            # [粘性分账 2026-11-18] 恢复的是「用户选中的模式」⇒ 运行中
            # 模式与粘性都归位（现场启动按钮按它跑）。
            self.selected_mode = restored
            self._log("自动运行：粘性模式从持久化恢复 [%s]" % restored)

    # ---- 生命周期 ----
    def start(self, now_ms=None, mode=""):
        """进入自动运行（幂等）。调用方**必须先自行判定条件**是否满足。

        :param mode: 本轮要跑的自动模式 id（produce/backwash/rinse）。
            空串 = 沿用上次选中的模式（现场启动按钮路径）；从未选过且
            又不给 → 返回 False（没有模式就没有动作序列，跑了等于没跑）。

        [为什么不在这里判条件] 条件（模式/急停/锁存/模式配置）的判定需要
        上下文，分散在两个入口：线协议 `_h_auto_control`（远程命令，要逐条
        回错因）和 `_handle_buttons`（现场按钮，只记日志）。两边判定条件
        相同但报错方式不同，故由调用方判、这里只做状态迁移。

        [停止＝暂停 2026-11-19] 用户口径：「自动模式下**不管是哪个模式
        运行**，按下停止后都要**保留停止时候的状态**，之后按启动是
        **继续运行**，而不是从头开始，**特别是定时器时间要保留**，除非
        按过复位按钮」⇒ 本方法启动时先问一句"有没有暂停态可续跑"：
          · 有且模式对得上 ⇒ **续跑**（同一模式/同一阶段，阶段计时**接着
            走**；相位机照常走一遍：阀相（等阀到位）→ 泵相（按该模式配置
            决定是否启泵）→ 运行相）；
          · 没有 ⇒ 与原来一样从头开始。
        """
        if self._running:
            return False
        # ---- ① 续跑判定（停止＝暂停）----
        #    请求的模式 = 载荷里的模式 / 粘性 / 上次的模式（与既有口径同）。
        want_mode = mode or self.selected_mode or self.mode
        plan = self._resume_plan(want_mode)
        resume_elapsed = 0
        if plan is not None:
            root, stage, resume_elapsed = plan
            # 续跑 ⇒ 根模式按**暂停态**（组合循环仍是 auto；客户端把服务端
            # 报的"子阶段"回填进下拉时也不能把粘性改成单模式 ⇒ 物理启动
            # 按钮仍按组合循环跑）。
            mode = root
            if root == proto.AUTO_MODE_AUTO:
                # 组合循环：从**记住的阶段**继续（不是从头回到首阶段）。
                self._cycle_resume_mode = stage
            # 暂停态被"消费"：这一轮跑起来后就不再是暂停态了。
            self._paused = False
            self._pause_root = ""
            self._pause_mode = ""
            self._pause_elapsed_ms = 0
            self._log("自动运行：续跑（模式[%s]%s，已用 %d ms 接着走）"
                      % (self._mode_name_of(root),
                         ("阶段[%s]" % self._mode_name_of(stage))
                         if (root == proto.AUTO_MODE_AUTO and stage) else "",
                         resume_elapsed))
        elif self._paused:
            # 换了模式 ⇒ 旧的暂停态作废（它属于上一次运行）⇒ 从头开始。
            self._clear_pause("启动了其它模式")
        if mode:
            self.mode = mode
            # [粘性分账 2026-11-18] 这次启动用的模式也是「用户选中的模式」
            # ⇒ 粘性跟着更新 ⇒ 之后按现场启动按钮也是这个模式（与屏幕上
            # 的启动按钮同效）。
            self.selected_mode = mode
            self.ctx.persist_mode(self.selected_mode)
        if not self.mode:
            self._log("自动运行：拒绝启动 —— 未选择自动模式"
                      "（先在客户端主界面选模式）")
            return False
        self._running = True
        self.phase = self.PHASE_VALVES
        self._last_step_ms = 0
        # [累计运行时长起算 2026-11-12] 只有自动运行**真的起来了**才计时
        # （"已启动"这一刻起算；此前累计保留，多次运行是累加）。
        self._run_start_ms = self.ctx.now_ms() if now_ms is None \
            else int(now_ms)
        # [组合模式「自动」= 循环调度 2026-11-12] 启动时进入**参与列表的
        # 第一个**子阶段，后续由压差/定时在列表里推进（_cycle_tick）。
        if self.mode == proto.AUTO_MODE_AUTO:
            self._cycle_root = proto.AUTO_MODE_AUTO
            # [配置快照] 启动时取一次：运行期改配置不打断当前循环（下一轮
            # 启动生效），与"模式配置下次启动用"的口径一致。
            self._cycle_cfg = self._cycle_config()
            # [参与列表 2026-11-18 配置化] 列表**有序**：第一个＝主运行阶段。
            stages = self._cycle_stages()
            first = stages[0] if stages else proto.AUTO_CYCLE_MODES_DEF[0]
            # [从记住的阶段继续 2026-11-12] 上次停止/急停时走到哪一步，就
            # 从那一步继续（不是从头）；按过复位则记忆已清 ⇒ 从头。
            resume = self._cycle_resume_mode
            if resume and resume in stages:
                self.mode = resume
            else:
                if resume:
                    # [记忆阶段已不在新列表 2026-11-18] 配置改了（模式被移出
                    # 参与列表）⇒ 记一行日志，从**首元素**开始（不静默）。
                    self._log("自动循环：记住的阶段[%s]已不在参与循环的模式"
                              "列表里 ⇒ 从首个阶段[%s]开始"
                              % (self._mode_name_of(resume),
                                 self._mode_name_of(first)))
                # 阶段换了（记忆失效）⇒ 阶段计时也从头（不能把上一个阶段的
                # 用时算到新阶段头上）。
                resume = ""
                self.mode = first
            # [粘性落盘落 "auto" 而不是子阶段 2026-11-16 审计·对齐 C]
            # 落盘的粘性是「现场启动按钮按哪个模式跑」的事实源。原来这里
            # persist_mode(子阶段 id) ⇒ 服务端重启后粘性="rinse" 而
            # _cycle_root/_cycle_resume_mode 都是内存态（已丢）⇒ 现场按键
            # 按**单模式**跑顺洗、永不回到产水（循环链断了）。C 端粘性恒存
            # "auto"（子阶段分账在 s_cycleStage）⇒ 两端对齐：粘性落
            # "auto"，重启后按键重跑循环（从产水开始——阶段记忆本就是
            # 进程内的，与 C 同口径）。
            # [粘性分账 2026-11-18 审计] 落盘**用户选中的模式**，不是
            # 硬写 "auto"：运行中 op=5 选了别的模式时，内存里是用户的
            # 选择，落盘必须保留 ⇒ 服务端重启后粘性不丢（否则物理
            # 启动按钮按旧模式跑）。正常循环路径 selected_mode 本来
            # 就是 "auto" ⇒ 行为与原来一致。
            self.ctx.persist_mode(self.selected_mode or
                                 proto.AUTO_MODE_AUTO)
            # [续跑 2026-11-19] 阶段计时**接着走**（不是从 0 重来）：把
            # 暂停那一刻的已用时长补回起点 ⇒ 剩余时间接着算。
            self._cycle_stage_at = (self.ctx.now_ms() - resume_elapsed
                                    if resume else self.ctx.now_ms())
            self._log("自动运行：组合模式从[%s]继续"
                      % self._mode_name())
        self._begin_mode_phase()
        self._sync_station()
        return True

    def _resume_plan(self, want_mode):
        """判定这次启动是不是"续跑"（停止＝暂停）。

        :param want_mode: 本次请求要跑的模式 id（组合循环＝"auto"）。
        :return: None（不是续跑 ⇒ 从头开始）或
                 **(root_mode, stage, elapsed_ms)**
            · root_mode  = 续跑时的**根模式**（"auto" 或单模式 id）
            · stage      = 续跑时的**模式**（组合循环＝子阶段；单模式＝模式）
            · elapsed_ms = 该阶段**已用时长**（毫秒；续跑时**接着走**）

        [为什么"记住的子阶段"也算续跑] 组合循环暂停时，服务端把
        autoMode 报成**子阶段**（"下次将从哪一步继续"）⇒ 界面把下拉
        对齐成子阶段 ⇒ 人再按启动时发上来的是**子阶段 id**。若不认它，
        "继续运行"会退化成"单模式重跑"（循环链与阶段计时全丢）⇒ 与用户
        口径相悖。但**用户主动选过单模式**时（op=5 改了粘性）那是新的
        意图 ⇒ 从头开始。
        """
        if not self._paused:
            return None
        want = str(want_mode or "")
        if self._pause_root == proto.AUTO_MODE_AUTO:
            if want in ("", proto.AUTO_MODE_AUTO):
                return (proto.AUTO_MODE_AUTO, self._pause_mode,
                        self._pause_elapsed_ms)
            if (want == self._pause_mode
                    and self.selected_mode in ("", proto.AUTO_MODE_AUTO)):
                # 界面把"子阶段"回填进下拉后按启动 ⇒ 仍按组合循环续跑。
                return (proto.AUTO_MODE_AUTO, self._pause_mode,
                        self._pause_elapsed_ms)
            return None
        # 单模式：只有**同一个模式**才续跑（换了模式＝新的意图 ⇒ 从头）。
        if want in ("", self._pause_root):
            return (self._pause_root, self._pause_root,
                    self._pause_elapsed_ms)
        return None

    def _capture_pause(self, now_ms=None):
        """把"正在跑的运行态"记成**暂停态**（停止＝暂停）。

        [用户口径 2026-11-19] 「按下停止后都要保留停止时候的状态，之后
        按启动是继续运行，而不是从头开始，特别是定时器时间要保留」。

        [记三样] ① 根模式（组合循环 "auto" / 单模式 id）② 模式（组合
        循环时＝**子阶段**；单模式时＝模式本身）③ 该阶段的**已用时长**
        （毫秒）—— 续跑时**接着走**的关键。

        [必须在 `_clear_phase_state()` 之前调] 那里会清掉 `_cycle_root`
        与 `_cycle_stage_at`（计时起点）⇒ 记晚了就什么都留不下。

        [只在真的在跑时记] 幂等的重复停止（本来就没在跑）不该动暂停态
        （模式轮询里每拍都会调 stop()）。
        """
        if not self._running:
            return
        now = self.ctx.now_ms() if now_ms is None else int(now_ms)
        was_cycle = (self._cycle_root == proto.AUTO_MODE_AUTO)
        self._paused = True
        self._pause_root = (proto.AUTO_MODE_AUTO if was_cycle else self.mode)
        self._pause_mode = self.mode
        if was_cycle and self._cycle_stage_at:
            self._pause_elapsed_ms = max(0, now - self._cycle_stage_at)
        else:
            # 单模式没有阶段定时（跑多久由人按停止决定）⇒ 无已用时长可留。
            self._pause_elapsed_ms = 0
        self._log("自动运行：已暂停（模式[%s]%s，已用 %d ms 保留）"
                  % (self._mode_name_of(self._pause_root),
                     ("阶段[%s]" % self._mode_name_of(self._pause_mode))
                     if was_cycle else "",
                     self._pause_elapsed_ms))

    def _clear_pause(self, reason=""):
        """清掉暂停态（模式 + 阶段 + 计时）——下次启动从头开始。

        [谁会调] ① 复位（人工确认现场 ⇒ 状态与时间都重置，2026-11-19
        用户口径）② 启动时换了模式（旧的暂停态属于上一次运行）。
        """
        if not (self._paused or self._cycle_resume_mode):
            return
        self._paused = False
        self._pause_root = ""
        self._pause_mode = ""
        self._pause_elapsed_ms = 0
        self._cycle_resume_mode = ""
        if reason:
            self._log("自动运行：暂停态已清空（%s）" % reason)

    @property
    def paused(self):
        """有没有"暂停的运行态"可续跑（停止＝暂停）。"""
        return bool(self._paused)

    def pause_info(self):
        """暂停态摘要（供状态快照/测试查询）。

        :return: {"paused":bool, "root":str, "mode":str, "elapsedMs":int}
        """
        return {
            "paused": bool(self._paused),
            "root": self._pause_root,
            "mode": self._pause_mode,
            "elapsedMs": int(self._pause_elapsed_ms),
        }

    #: 循环子阶段（组合模式「自动」内部）的**协议稳定 id**。
    #:
    #: [2026-11-18 起不再决定链的形状] 参与循环的模式与顺序改由配置决定
    #: （auto.cycle 的 cycle_modes）⇒ 这三个常量只作**常见阶段 id**（默认
    #: 列表的首/中间/末阶段、日志与文案），不再用来拼链。
    _CYCLE_PRODUCE = "produce"
    _CYCLE_BACKWASH = "backwash"
    _CYCLE_RINSE = "rinse"

    def _begin_mode_phase(self, mode=None):
        """按当前（或指定）模式重建**阀相目标**并重置相位计时。

        [为什么抽出来] 手动启动与**循环换相**共用同一套"按模式配置开/关
        阀"的逻辑；换相时不能复用 start()（那会重置运行态与计时）。
        """
        if mode:
            self.mode = mode
            # [换相粘性 2026-11-18 审计] 同 start()：落**用户选中的模式**
            #（子阶段只在内存推进）；运行中改选过则保留用户的选择，
            # 否则服务端重启后粘性丢失 ⇒ 物理启动按钮按旧模式跑。
            self.ctx.persist_mode(self.selected_mode or
                                 proto.AUTO_MODE_AUTO)
        # ---- 阀相目标：选中阀开、未选阀关（2026-11-11 用户口径）----
        # 未配置点位的阀不进目标表（不能自动操作，也不等它到位）。
        selected = set(self.ctx.auto_mode_config(self.mode).get("devices")
                       or [])
        self._valve_targets = {}
        for i in range(self.ctx.valve_count()):
            if not self.ctx.valve_ready(i):
                continue
            self._valve_targets[i] = ("valve%d" % (i + 1)) in selected
        self._valve_cmd_acked = set()
        self.phase = self.PHASE_VALVES
        now = self.ctx.now_ms()
        self._phase_at = now
        self._valve_deadline_ms = (now + self.ctx.valve_move_timeout_ms()
                                   + self.VALVE_PHASE_GRACE_MS)
        self._pump_tasks = None
        # [泵相宽限 2026-11-13] 新一轮泵相的宽限窗口重置（换相也走这里——
        # C 端 amStepValves 进泵相时同款清零）。
        self._pump_deadline_ms = 0
        self._pump_first_err = ""
        opens = [i + 1 for i, w in self._valve_targets.items() if w]
        closes = [i + 1 for i, w in self._valve_targets.items() if not w]
        self._log("自动运行：模式[%s] 阀相（开%s 关%s）"
                  % (self._mode_name(),
                     ("阀" + "、阀".join(str(n) for n in opens)) if opens
                     else "无",
                     ("阀" + "、阀".join(str(n) for n in closes)) if closes
                     else "无"))

    def stop(self, now_ms=None, stop_devices=False, reason="",
             reset_valves=None, clear_pause=False):
        """退出自动运行（幂等）。

        :param stop_devices: True = 顺带**停全部设备**（切手动/急停走这条）
        :param reason: 日志里写明原因（"切回手动" / "急停" / …）
        :param reset_valves: True/False = 强制"关阀复位/保持现状"；
            None（缺省）= **按当前模式的配置**（2026-11-12 用户定案：
            每个模式可分别设「停止时/急停时是否关阀复位」）。

        [stop_devices 的默认值必须是 False] 线协议上人主动按"停止自动运行"
        只是停控制器，不该顺带把变频器断电 —— 那会让"停止自动运行"变成
        一个比"停设备"更重的动作，反而没人敢按。全停只在**安全场景**
        （切回手动、急停）发生，由调用方显式要求。
        [模式序列残留清理] stop 后阀目标/泵任务全部作废（phase 回 idle）；
        self.mode 保留（粘性：下次现场启动按钮仍跑上次选的模式）。
        [切模式不受本配置影响] 切回手动/切到自动走的是**既有安全契约**
        （「切换模式 ⇒ 关闭所有设备」，含阀），调用方显式传
        reset_valves=True；新配置只管"停止/急停"这两条路径。
        [记住循环子阶段] 停止时把当前子阶段记下来（下次启动从那一步继续），
        必须在 `_clear_phase_state()` **之前**记 —— 那里会清掉 _cycle_root。
        [停止＝暂停 2026-11-19] 用户口径：「自动模式下**不管是哪个模式运行**，
        按下停止后都要**保留停止时候的状态**，之后按启动是**继续运行**，不是
        从头开始，**特别是定时器时间要保留**，除非按过复位按钮」⇒ 停止把
        运行态记成**暂停态**（根模式 + 模式/阶段 + 阶段已用时长），相位报
        "paused"；下次 start() 命中暂停态 ⇒ **续跑**（同一模式、同一阶段、
        计时接着走）。**阀是否复位仍按本方法的 reset_valves 口径**（逐模式
        `resetValvesOnStop` / 急停 `resetValvesOnEstop`）—— 这条不动。
        [粘性模式还原 2026-11-13·对齐 C] start() 组合分支把 self.mode 改写
        成子阶段；C 端 s_mode 始终保持 "auto"（子阶段分账在 s_cycleStage）。
        不还原的话，停止后**不带 mode 的启动**（现场按钮/远程粘性）会把
        单个子模式当普通单模式跑（_cycle_root 不再置位 ⇒ 循环链断）。
        本轮是组合模式就在收尾把 self.mode 还原为 "auto"。
        """
        was_cycle = (self._cycle_root == proto.AUTO_MODE_AUTO)
        # [阀复位配置要先于 mode 还原读取 2026-11-16 审计] 组合循环里
        # self.mode 是**当前子阶段**，「停止时是否关阀复位」按子阶段配置
        # 取（2026-11-12 逐模式口径）；而下面还原粘性会把 mode 改回 "auto"
        # ⇒ 之后 _reset_valves_on_stop() 读到的是组合根的配置（通常缺省）
        # ⇒ 子阶段配的「停止不复位阀」被静默忽略。⇒ 先取配置、再还原。
        if reset_valves is None:
            reset_valves = self._reset_valves_on_stop()
        self._remember_cycle_stage()
        # [停止＝暂停 2026-11-19] 把"停止时候的状态"记成暂停态（模式 +
        # 阶段 + 阶段已用时长）⇒ 之后按启动是**续跑**而不是从头开始。
        # 必须在 `_clear_phase_state()` **之前**记（那里清掉 _cycle_root 与
        # _cycle_stage_at＝计时起点）。
        self._capture_pause(now_ms)
        self._clear_phase_state()
        # [切模式＝重来 2026-11-19 用户口径] 切模式路径传
        # clear_pause=True ⇒ 停止后**清掉暂停态**（下次启动从头），
        # 只有**人按停止**（普通停止命令）才保留暂停态等续跑。
        if clear_pause:
            self._clear_pause()
        if was_cycle and self.mode != proto.AUTO_MODE_AUTO:
            self.mode = proto.AUTO_MODE_AUTO
        if not self._running:
            # 即使控制器已经停了，安全场景仍要保证设备真停
            # （可能上一次全停部分失败，这里补一次）。
            if stop_devices:
                self.stop_all_devices(reason, reset_valves=reset_valves)
                self._sync_station()
            return False
        self._running = False
        # [相位＝暂停 2026-11-19] 有暂停态 ⇒ 报"paused"（界面据此显示
        # "暂停中（还剩 X）"）；没有（本来就没在跑 / 已复位）⇒ 仍是 idle。
        self.phase = self.PHASE_PAUSED if self._paused else self.PHASE_IDLE
        # [累计运行时长结算 2026-11-12] 把本次运行的一段并进累计。
        self._settle_run_time()
        self._log("自动运行：已停止%s（阀%s）"
                  % ("（%s）" % reason if reason else "",
                     "复位关回" if reset_valves else "保持现状"))
        if stop_devices:
            self.stop_all_devices(reason, reset_valves=reset_valves)
        self._sync_station()
        return True

    def _settle_run_time(self):
        """把本次运行的一段并进累计（停止/急停时调用），并**持久化**。"""
        if self._run_start_ms:
            now = self.ctx.now_ms()
            if now > self._run_start_ms:
                self._run_accum_ms += now - self._run_start_ms
        self._run_start_ms = 0
        # [持久化 2026-11-12] 累计是**跨重启**的总量 ⇒ 每次结算即落盘。
        self.ctx.persist_run_ms(self._run_accum_ms)

    def run_ms(self, now_ms=None):
        """**累计**自动运行时长（毫秒；running 时算到当前）。

        [为什么客户端不自己计] 客户端断线/重启就归零，两端数字对不上；
        服务端是唯一事实源（控制器自己累加，跨多次运行保留）。
        """
        total = self._run_accum_ms
        if self._running and self._run_start_ms:
            now = self.ctx.now_ms() if now_ms is None else int(now_ms)
            if now > self._run_start_ms:
                total += now - self._run_start_ms
        return int(total)

    def settle_on_exit(self):
        """进程退出收尾：把"正在跑的这一段"结算并落盘（对齐 C shutdown）。

        [为什么要有它] _settle_run_time 只挂在 stop/急停/模式中止三处；
        进程正常退出时若控制器还在跑，最后一段不结算，落盘值最多旧一个
        RUN_PERSIST_MS 周期窗口。未在跑时是空操作（_run_start_ms 已在
        停止/急停/中止路径清零）。"""
        self._settle_run_time()

    def stage_timing(self, now_ms=None):
        """当前子阶段（模式）的 **（已运行毫秒, 总毫秒）**。

        只对**组合模式**（「自动」= 循环）有意义。总时长由**当前阶段自己
        的配置**决定（stageCfg）：该阶段启用了**定时触发** ⇒ 总时长 =
        它的 timerMinutes；只按压差切（没启用定时）⇒ 总时长**不可知**
        （返回 0，客户端只显示「已运行」+「按压差切换」）。

        :return: (elapsed_ms, total_ms)
            elapsed_ms = -1：不在自动运行 / 没有阶段（不可知，别编造）
            total_ms   = 0 ：本阶段没有定时（剩余时间不可知）

        [为什么计时起点是 _cycle_stage_at 而不是进 running 相] 阶段从
        **阀相**就开始算了（阀到位、泵启动都占用该模式的运行时间）；
        等进 running 相再起算，反洗就会比配置的 10 分钟多跑阀/泵那几秒。
        [单模式启动不倒计时] 直接启动产水/反洗/顺洗（非组合模式）时没有
        换相逻辑，跑多久完全由人按停止决定 ⇒ 剩余时间不可知（-1/0）。
        [时长口径只有一处 2026-11-18] 原先这里与 `_cycle_tick` **各写一遍**
        ⇒ 改一处忘一处就是隐性 bug 面（表盘显示 10 分钟、实际切相是 5
        分钟）。⇒ 统一走 `_cycle_stage_limit_ms`。
        [暂停态 2026-11-19] 停止（＝暂停）后**仍报记住的阶段与剩余时间**，
        且**冻结在停止那一刻**（暂停期间不继续走表）⇒ 界面能显示
        「暂停中（还剩 X）」；按启动续跑后表盘从冻结值**接着走**。
        """
        if not self._running:
            # [暂停态 2026-11-19] 停止＝暂停 ⇒ 仍报"记住的阶段与剩余时间"。
            if self._paused and self._pause_root == proto.AUTO_MODE_AUTO:
                stages = self._cycle_stages()
                try:
                    idx = stages.index(self._pause_mode)
                except ValueError:
                    # 记住的阶段已不在参与列表里（配置改过）⇒ 不可知。
                    return -1, 0
                total_ms = self._cycle_stage_limit_ms(self._pause_mode, idx)
                return (int(self._pause_elapsed_ms),
                        max(0, int(total_ms)))
            return -1, 0
        if not self._cycle_root:
            return -1, 0
        idx = self._cycle_stage_index()
        if idx < 0:
            # 当前阶段不在参与列表里（配置被改过/记忆阶段失效）⇒ 不可知，
            # **不编造**一个倒计时。
            return -1, 0
        total_ms = self._cycle_stage_limit_ms(self.mode, idx)
        now = self.ctx.now_ms() if now_ms is None else int(now_ms)
        start = self._cycle_stage_at or now
        elapsed = max(0, now - start)
        return int(elapsed), max(0, int(total_ms))

    # ---- 参与循环的模式列表 / 各阶段时长（2026-11-18 配置化）----

    def _cycle_stages(self):
        """**参与循环的模式列表**（有序；快照口径）。

        [为什么读快照而不是实时读配置] 运行期改配置**不打断**正在跑的循环
        （口径与"模式配置下次启动生效"一致）⇒ 循环期间一律用启动时取的
        快照（`self._cycle_cfg`）。

        [防御] 快照里没有这个键 ⇒ 回**缺省列表**；快照里是**空列表**
        （配置成"未配置参与循环的模式"）⇒ 保持空 ⇒ 下标算不出来（-1）⇒
        换相/表盘都**不动**（宁可不换相也不要崩，绝不 IndexError）。
        """
        raw = self._cycle_cfg.get("cycle_modes")
        return proto.normalize_cycle_modes(
            raw, default=proto.AUTO_CYCLE_MODES_DEF)

    def cycle_stages(self):
        """对外：当前**配置**（实时）的参与循环模式列表。

        [给谁用] app._auto_state_extra 判断「记住的阶段」是否还在链上
        （不在 ⇒ 回显 "auto"，别显示一个链上不存在的阶段）。
        """
        cfg = getattr(getattr(self.ctx, "_app", None), "cfg", None)
        raw = getattr(cfg, "auto_cycle", None) or {}
        return proto.normalize_cycle_modes(
            raw.get("cycle_modes"), default=proto.AUTO_CYCLE_MODES_DEF)

    def _cycle_stage_index(self):
        """当前阶段在参与列表里的下标；不在列表 ⇒ -1（不抛异常）。"""
        stages = self._cycle_stages()
        try:
            return stages.index(self.mode)
        except ValueError:
            return -1

    def _cycle_stage_cfg(self, mode):
        """某阶段的**切换条件**（快照口径）。

        [缺省] 快照里没有这个阶段 ⇒ 回合成缺省（不返回空 dict ⇒ 下游不必
        到处判空；空 dict 会让"压差关 + 定时开"变成"两个都关"⇒ 阶段永不
        切换，是个很难查的坑）。
        """
        table = self._cycle_cfg.get("stage_cfg")
        entry = table.get(mode) if isinstance(table, dict) else None
        if isinstance(entry, dict):
            return entry
        return proto.build_cycle_stage_cfg(self._cycle_stages()).get(mode) \
            or {}

    def _cycle_stage_minutes(self, mode):
        """某阶段的**定时分钟数**——查快照的 stage_cfg。

        [兜底顺序] ① stage_cfg[mode].timer_minutes → ② 旧键
        backwash/rinse 分钟数（向后兼容：老配置由旧键合成）→ ③ 缺省 10 分钟。
        """
        entry = self._cycle_stage_cfg(mode)
        val = entry.get("timer_minutes")
        if val is None:
            if mode == self._CYCLE_BACKWASH:
                val = self._cycle_cfg_get(
                    "backwash_minutes", proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF)
            elif mode == self._CYCLE_RINSE:
                val = self._cycle_cfg_get(
                    "rinse_minutes", proto.AUTO_CYCLE_RINSE_MINUTES_DEF)
            else:
                val = proto.AUTO_CYCLE_STAGE_MINUTES_FALLBACK
        try:
            val = int(val)
        except (TypeError, ValueError):
            val = proto.AUTO_CYCLE_STAGE_MINUTES_FALLBACK
        return max(proto.AUTO_CYCLE_MINUTES_MIN,
                   min(proto.AUTO_CYCLE_MINUTES_MAX, val))

    def _cycle_stage_limit_ms(self, mode, index=0):
        """某阶段的**总时长上限**（毫秒）；0 = 本阶段没有定时（不可知）。

        [统一口径 · 2026-11-18] `stage_timing`（表盘）与 `_cycle_tick`
        （换相判定）原先**各写一遍**时长口径 ⇒ 改一处忘一处就是隐性 bug
        （表盘显示 10 分钟、实际 5 分钟就切）。⇒ 两者都走这里。

        [新口径 · 每阶段各自一份] 上限由**该阶段自己的配置**决定：
          · 启用了**定时触发** ⇒ 上限 = 该阶段的 timerMinutes；
          · 没启用（只按压差切）⇒ 总时长**不可知**（0 ⇒ 客户端不编造倒计时）。
        """
        entry = self._cycle_stage_cfg(mode)
        if not bool(entry.get("timer_enabled", False)):
            return 0
        return self._cycle_stage_minutes(mode) * 60000

    def _cycle_config(self):
        """取循环配置快照（cfg.auto_cycle；缺项回缺省）。

        [快照口径 · 2026-11-18] 启动时取一次；运行期改配置**不打断**正在
        跑的循环（下一次启动生效）。快照里带**参与列表**与**各阶段切换
        条件**（stage_cfg）——缺 stage_cfg 时按旧键现场合成 ⇒ 老配置 /
        老 cfg 对象也能跑。
        """
        cfg = getattr(getattr(self.ctx, "_app", None), "cfg", None)
        raw = getattr(cfg, "auto_cycle", None) or {}
        modes = proto.normalize_cycle_modes(
            raw.get("cycle_modes"), default=proto.AUTO_CYCLE_MODES_DEF)
        legacy = {
            "press_threshold": raw.get("press_threshold",
                                       proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF),
            "press_compare": raw.get("press_compare",
                                     proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
            "timer_fallback": raw.get("timer_fallback",
                                      proto.AUTO_CYCLE_TIMER_FALLBACK_DEF),
            "timer_minutes": raw.get("timer_minutes",
                                     proto.AUTO_CYCLE_TIMER_MINUTES_DEF),
            "backwash_minutes": raw.get(
                "backwash_minutes", proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF),
            "rinse_minutes": raw.get(
                "rinse_minutes", proto.AUTO_CYCLE_RINSE_MINUTES_DEF),
        }
        explicit = proto.normalize_cycle_stage_cfg(
            raw.get("stage_cfg"), default={})
        out = {
            "enabled": bool(raw.get("enabled",
                                    proto.AUTO_CYCLE_ENABLED_DEF)),
            "press_threshold": float(legacy["press_threshold"]),
            "press_compare": str(legacy["press_compare"]),
            "timer_fallback": bool(legacy["timer_fallback"]),
            "timer_minutes": int(legacy["timer_minutes"]),
            "backwash_minutes": int(legacy["backwash_minutes"]),
            "rinse_minutes": int(legacy["rinse_minutes"]),
            # [2026-11-18 配置化] 参与循环的模式列表（有序）与各阶段的
            # **切换条件**（压差触发 / 定时触发）。
            "cycle_modes": modes,
            "stage_cfg": proto.build_cycle_stage_cfg(
                modes, legacy=legacy, explicit=explicit),
        }
        return out

    def _cycle_config_error(self):
        """组合模式「自动」的配置校验。返回 ""（可跑）或拒绝原因。

        [2026-11-18 配置化] 校验对象改成**配置里的参与列表**（不再是固定
        三模式）：
          ① 循环未启用 → 拒；
          ② 列表为空 → 拒（文案指路远端设置）；
          ③ 列表 < 2 个 → 拒（1 个没有循环意义）；
          ④ **某个参与阶段两个触发都没开** → 拒（压差/定时至少一个；
             否则该阶段永远不切换 ⇒ 流程卡死，属配置错误，必须说清楚）；
          ⑤ 参与的模式没配设备 → 拒并点名（**不写死数量词**）；
          ⑥ 参与的模式**设备级**校验（阀未配点位 / 变频器未装 / 泵组未装）
             —— 与单模式分支**同款**（原来循环分支漏了，跑到那个阶段才炸）。
        """
        cfg = self._cycle_config()
        if not cfg["enabled"]:
            return "「自动」循环模式未启用（远端设置 → 自动循环配置）"
        stages = list(cfg.get("cycle_modes") or [])
        if not stages:
            return ("「自动」循环未配置参与循环的模式"
                    "（远端设置 → 自动循环配置）")
        if len(stages) < 2:
            return ("「自动」循环至少需要 2 个参与循环的模式"
                    "（当前 %d 个）" % len(stages))
        # ---- ④ 每个参与阶段至少要有一个切换条件 ----
        stage_cfg = cfg.get("stage_cfg") or {}
        no_trigger = [m for m in stages
                      if not (bool((stage_cfg.get(m) or {}).get(
                          "press_enabled", False))
                          or bool((stage_cfg.get(m) or {}).get(
                              "timer_enabled", False)))]
        if no_trigger:
            return "阶段 %s 未设置切换条件（压差/定时至少一个）" % \
                "、".join(self._mode_name_of(m) for m in no_trigger)
        # ---- ⑤ 参与的模式都配设备 ----
        missing = [self._mode_name_of(m) for m in stages
                   if not (self.ctx.auto_mode_config(m).get("devices") or [])]
        if missing:
            return "需要参与循环的模式都配设备，缺：%s" % "、".join(missing)
        # [设备级校验 · fail-closed] 与单模式分支同款（复用同一个函数）。
        for m in stages:
            err = self._mode_device_error(m)
            if err:
                return err
        return ""

    def _reset_valves_on_stop(self):
        """停止时是否关阀复位。

        [自动模式] 当前模式配置（无模式/无配置按缺省 True）。
        [手动模式] **一律 True**（2026-11-15 用户口径「手动模式下按下停止
        也能全部关闭」「按下停止也关闭全部球阀」）。手动模式没有工艺序列，
        阀不会正停在"某个必须保持的中间位"，所以现场按停止/急停就该把
        设备与球阀全部停下来。逐模式配置只管自动模式那三种序列。
        """
        if not self.ctx.is_auto():
            return True
        cfg_mode = self.ctx.auto_mode_config(self.mode)
        return bool(cfg_mode.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                                 proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF))

    def _reset_valves_on_estop(self):
        """急停时是否关阀复位。

        [自动模式] 当前模式配置（无模式/无配置按缺省 False —— 自动模式
        急停默认"阀保持现状"，见 2026-11-12 用户口径）。
        [手动模式] **一律 True**（2026-11-15 用户口径：手动模式下急停也要
        有效果，设备与球阀全部停下来）。手动模式没有工艺序列要保护，
        与自动模式那条"阀可能正停在中间位"的顾虑无关。
        """
        if not self.ctx.is_auto():
            return True
        cfg_mode = self.ctx.auto_mode_config(self.mode)
        return bool(cfg_mode.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP,
                                 proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF))

    def _remember_cycle_stage(self):
        """记住当前循环子阶段（停止/急停时调用；非组合模式不做）。

        [为什么要记] 用户口径「停止/急停后不是从头开始，而是从停止的
        状态继续运行，除非按了复位」——流程走到反洗被打断，恢复后应该
        接着反洗干，不是重跑产水。

        [2026-11-18 配置化] 白名单＝**当前参与循环的模式列表**（不再是固定
        三模式）⇒ 列表外的阶段**不记**（记了也恢复不了）。
        """
        if self._cycle_root != proto.AUTO_MODE_AUTO:
            return
        if self.mode in self._cycle_stages():
            if self._cycle_resume_mode != self.mode:
                self._log("自动循环：记住当前阶段[%s]（下次启动从这一步继续）"
                          % self._mode_name())
            self._cycle_resume_mode = self.mode

    def _clear_phase_state(self):
        """作废模式序列的中间状态（阀目标/泵任务/相位时钟）。"""
        self._valve_targets = {}
        self._valve_cmd_acked = set()
        self._pump_tasks = None
        self._phase_at = 0
        self._valve_deadline_ms = 0
        # [换相策略 2026-11-14] 待停的 carry 泵作废（序列已中止/停止 ⇒
        # 不再有"泵相收尾"来停它；停止路径另有全停逻辑兜底）。
        self._carry_pending_stop = set()
        # [循环状态清空 2026-11-12] 停止后不再处于任何子阶段（下一次启动
        # 重新从**首个阶段**开始——参与列表的第一个，2026-11-18 起可配）；
        # 累计运行时间不受影响（在 stop 里另行结算）。
        # [配置快照不清] `_cycle_cfg` 保留最后一次启动时的快照 ⇒ 停止后
        # 「记住的阶段」仍能按**当时生效的列表**校验（阶段记忆是内存态，
        # 进程重启即丢，与 C 同口径）。
        self._cycle_root = ""
        self._cycle_stage_at = 0

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
        [阀是否复位按模式配置 2026-11-12] 急停是人身安全事件：泵与变频器
        必须停（切断动力）；电动球阀在**自动模式**默认保持现状（用户口径
        「按下急停后要停止所有设备(电动球阀可以不动)」）——阀可能正停在中间
        位，此时叠一条"关"命令是叠加未知动作。需要"急停也关阀"的自动模式
        现场在模式配置里勾「急停时复位阀」。
        [手动模式一律关阀 2026-11-15] 手动模式没有工艺序列要保护 ⇒ 急停
        把设备与球阀**全部**停下来（用户口径「手动模式下…急停也有效果」）。
        """
        first = not self._estop_latched
        self._estop_latched = True
        self._running = False
        self.phase = self.PHASE_ESTOP
        # [累计运行时长结算 2026-11-12] 急停同样结束"正在运行"这一段。
        self._settle_run_time()
        # [阀复位配置要先于 mode 还原读取 2026-11-16 审计] 同 stop()：
        # 「急停时是否关阀复位」按**当前子阶段**配置取；先取后还原，否则
        # 还原成 "auto" 后读到的是组合根配置（子阶段配的「急停复位阀」
        # 被静默忽略）。手动模式强制 true 的口径在 _reset_valves_on_estop
        # 里，不受此处顺序影响。
        reset_valves = self._reset_valves_on_estop()
        # [记住循环子阶段 2026-11-12] 急停也是"停止" ⇒ 同样记住走到哪一步，
        # 复位后（解锁）再启动从那一步继续。
        # [粘性模式还原 2026-11-13·对齐 C] 本轮是组合模式 ⇒ self.mode 还原
        # 为 "auto"（与 stop() 同口径；否则急停后无 mode 再启动跑成单子模式）。
        if self._cycle_root == proto.AUTO_MODE_AUTO:
            self._remember_cycle_stage()
            if self.mode != proto.AUTO_MODE_AUTO:
                self.mode = proto.AUTO_MODE_AUTO
        if first:
            # 报警在状态迁移之后入队：app 本拍 take 时快照已含本条。
            self.raise_alarm(proto.AUTO_ALARM_ESTOP,
                             "自动运行急停触发（等待人工复位）")
        self.stop_all_devices(reason, reset_valves=reset_valves)
        self._sync_station()
        self._log("自动运行：急停（已锁存，需复位后才能再次启动；阀%s）"
                  % ("复位关回" if reset_valves else "保持现状"))
        return True

    def reset(self):
        """复位：解除急停锁存与报警锁存，回到停止态（**不重新启动**）。

        [复位解除的是两把锁 2026-11-11] 急停锁存 + 模式序列报警锁存
        （阀超时等）。用户口径：「有报警没复位的话不能启动」「按下急停
        复位后必须再按复位，才可以用启动按钮启动」——两者都是"人工确认
        过现场没问题才准再跑"，由同一个复位按钮一次解除。

        [复位＝清零 2026-11-19] 用户口径：「除非按过**复位按钮**，屏幕上
        的或者**实体的复位**都可以，才会重置状态重置时间」⇒ 复位把
        **暂停态**（记住的模式/阶段 + 阶段已用时长）一起清掉 ⇒ 下次启动
        从头开始。**实体复位按钮走同一条 reset()**（按钮组 DI15 的沿 →
        `_handle_buttons` → `reset()`），所以屏幕与实体两处复位等效。
        """
        # [运行中不可复位 2026-11-18 联调深检] 运行中按复位会把 phase 打回
        # idle 但 _running 仍 True ⇒ 序列被架空（阀/泵/变频器保持当前状态
        # 无人管理：压差换相/定时切换全死、RUNNING 相的 60s 周期落盘不再
        # 执行），UI 还显示运行中——与 op=5「运行中不可改选模式」同款口径
        # ⇒ 运行中拒绝复位（远程 op=4 与现场复位按钮两个入口同样生效）：
        # 先按停止，再复位。
        if self._running:
            self._log("自动运行：运行中不可复位（请先停止自动运行）")
            return False
        was = self._estop_latched
        had_fault = self._fault_latched
        self._estop_latched = False
        self._fault_latched = False
        self.phase = self.PHASE_IDLE
        # [复位清空"记住的循环子阶段" 2026-11-12] 复位 = 人工确认现场、
        # 流程从头重来 ⇒ 下次启动组合模式回到**首个阶段**（参与列表的第一
        # 个，2026-11-18 起不再写死"产水"——列表可以配）。
        # [复位清空"暂停态" 2026-11-19] 用户口径：「除非按过**复位按钮**，
        # 屏幕上的或者**实体的复位**都可以，才会重置状态重置时间」⇒ 复位
        # 把暂停态（模式 + 阶段 + 阶段已用时长）一起清掉 ⇒ 下次启动从头。
        # （实体复位按钮走同一条 reset()，见 _handle_buttons 的 reset_edge。）
        had_pause = bool(self._paused or self._cycle_resume_mode)
        self._clear_pause()
        if had_pause:
            self._log("自动运行：已复位，暂停态与循环阶段记忆清空"
                      "（下次从头开始）")
        self._sync_station()
        if was or had_fault:
            self._log("自动运行：已复位（%s解除，处于停止态）"
                      % "、".join(x for x in
                                  ("急停锁存" if was else "",
                                   "报警锁存" if had_fault else "") if x))
        return was or had_fault

    # ---- 故障锁存（2026-11-15 用户定案）----
    #
    # [用户口径] 「进了报警表的报警要锁存，并自动停止设备，必须手动复位
    #  才可以启动设备」。落到本层的三件事：
    #   ① raise_fault()：设备级故障（变频器故障等）⇒ 报警 + **锁存** +
    #      **自动停设备**（VFD + 进水泵 + 电动球阀，按模式规则复位阀）；
    #   ② 锁存未解 ⇒ **手动模式的远程控制也被拒**（app 层各控制 handler
    #      的闸，见 app.py _fault_gate_blocked）；
    #   ③ clear_fault()：**人工复位**才解锁（自动模式的复位按钮 op=4，或
    #      设备侧的「故障复位」——那是人按的，一样算人工复位）。
    #
    # [与 _abort_mode 的区别] 模式序列中止（阀超时）**不动设备**：阀可能停在
    # 中间位，乱发"关阀"是叠加未知动作；这里是**设备故障**，停机本身就是
    # 保护动作（等价现场按停止）。

    def raise_fault(self, kind, text, now_ms=None, stop_devices=True,
                    enqueue_alarm=True):
        """设备级故障：报警（进报警历史表）+ **锁存** + 自动停设备。

        :param kind: 告警类型（proto.AUTO_ALARM_*）。
        :param text: 告警文字（给操作员看）。
        :param stop_devices: True=顺带停设备（默认）；False=只锁存+报警
            （保护动作类告警已经自己停了，别重复停）。
        :param enqueue_alarm: True=把告警排进自动告警队列（走统一的发布
            + 入库链路）；False=**调用方自己已经发过这条告警**（如运行保护
            ——它要带设备类别 dev 键单独发）⇒ 别再排一次（否则一条变两条）。
        :return: 是否新发生（False = 已在锁存中，不重复报）。
        """
        now = int(now_ms if now_ms is not None else self.ctx.now_ms())
        was = self._fault_latched
        self._fault_latched = True
        if enqueue_alarm:
            self.raise_alarm(kind, text, now)
        self._log(text + "（已锁存：需手动复位后才能再启动设备）")
        if stop_devices:
            # [与现场停止按钮同款] 停控制器 + 停设备（VFD 减速停 + 进水泵关
            # + 阀按模式规则复位）。
            self.stop(now, stop_devices=True, reason=text)
        else:
            self._sync_station()
        return not was

    def clear_fault(self, reason=""):
        """解除**报警锁存**（不动急停锁存）。

        [为什么单独一个入口] 「故障复位」（设备侧，人按的）与「复位按钮」
        （自动模式 op=4）都该解锁；但**急停锁存**只能由现场物理复位解除 ——
        绝不能被一个远程的故障复位顺手清掉。
        """
        if not self._fault_latched:
            return False
        self._fault_latched = False
        self._log("报警锁存已解除%s" % (("（%s）" % reason) if reason else ""))
        self._sync_station()
        return True

    @property
    def fault_latched(self):
        """报警锁存（True = 有未复位的报警，设备不许再启动）。"""
        return bool(self._fault_latched)

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
    def stop_all_devices(self, reason="", reset_valves=True):
        """停全部受控设备：变频器 / 进水泵 / 电磁阀。

        [为什么在框架层做而不是让每个调用点各写一遍] 切手动、急停、退出
        自动三个入口都要做这件事，任何一处漏了就是"手动模式下设备还在转"。
        集中一处，且**逐个设备独立 try** —— 一台失败不能挡住停其它台。

        [为什么不 await] 本方法可能被同步路径调用（step() 是同步入口）。
        全部动作以 asyncio 任务投递，失败只记日志：停机是"尽力而为且必须
        尝试"，不能因为一台设备离线就整批不做。

        :param reset_valves: True = 阀也关回原位；False = **阀保持现状**
            （2026-11-12 用户定案：停止/急停时是否复位电动球阀，逐模式
            可配）。False 时变频器与泵照常停 —— 停动力是必须的，阀复位
            是可选动作。
        """
        self._stop_all_seq += 1
        seq = self._stop_all_seq
        self._log("全停：开始（#%d%s，阀%s）"
                  % (seq, ("，" + reason) if reason else "",
                     "复位关回" if reset_valves else "保持现状"))
        try:
            # [get_running_loop 而不是 get_event_loop] 后者在没有运行中
            # loop 的线程会**新建**一个 loop ⇒ 任务投到没人跑的循环上
            # （退出时 "Task was destroyed but it is pending"），且全停动作
            # 实际不会执行。没有运行中的 loop ⇒ 投不了，也不该投。
            loop = asyncio.get_running_loop()
            if loop.is_closed():
                raise RuntimeError("事件循环已关闭")
        except Exception as exc:                        # noqa: BLE001
            self._log("全停：无法投递（%s）" % exc)
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
            #    [阀保持现状 2026-11-12] reset_valves=False ⇒ 这一段跳过：
            #    阀停在当前位置（可能正停在中间位，急停时再叠"关"是叠加
            #    未知动作）；变频器与泵照常停。
            if reset_valves:
                for i, ok_err in enumerate(await self._close_valves()):
                    ok, err = ok_err
                    (stopped if ok else failed).append(
                        "阀%d%s" % (i, "" if ok else "（%s）" % err))
            else:
                stopped.append("阀保持现状")
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

    async def _stop_pumps_agg(self):
        """逐台停泵并汇总成**单个** (ok, err)。

        [为什么要单独一个 2026-11-15 审计] 泵相的任务表按**单结果**验收
        （`ok, err = t.result()`）。`_stop_pumps()` 返回的是**列表**
        [(ok, err), ...] ⇒ 直接塞进任务表 ⇒ 验收时解包炸 ValueError
        （"too many values to unpack"）⇒ 被当成"泵启动失败"⇒ **换相策略
        carry 的收尾停泵一失败就把整个模式中止**（而且报的错驴唇不对马嘴）。
        ⇒ 这里统一汇总成单结果。
        """
        results = await self._stop_pumps()
        failed = []
        for i, item in enumerate(results):
            ok, err = item
            if not ok:
                failed.append("进水泵%d停止失败（%s）" % (i, err or "未知"))
        if failed:
            return False, "、".join(failed)
        return True, ""

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
            # [阀是否复位按模式配置 2026-11-12] 与远程停止同一口径。
            reset_valves = self._reset_valves_on_stop()
            if self._running:
                self.stop(now_ms, stop_devices=True,
                          reason="现场停止按钮",
                          reset_valves=reset_valves)
            else:
                # 没在跑也照样停一次设备：现场按停止是"我要设备停"，
                # 不是"我要控制器状态对齐"。
                self.stop_all_devices("现场停止按钮",
                                      reset_valves=reset_valves)
                self._sync_station()
                self._log("自动运行：未在运行，现场停止按钮仍执行全停")

        # ---- 复位：解除急停锁存（不启动）----
        if reset_edge:
            self.reset()

        # ---- 启动：必须同时满足全部条件（含模式配置可跑）----
        if start_edge:
            ok, why = self.start_preconditions(auto_mode)
            if not ok:
                self._log("自动运行：现场启动按钮被拒 —— %s" % why)
                # [被拒可见 2026-11-13 板端实锅] 被拒只进 journal，现场
                # 完全看不到（用户以为按键坏了）——报警入队推 TOPIC_
                # AUTO_ALARM，客户端报警页/状态栏可见。
                self.raise_alarm(proto.AUTO_ALARM_ACTION_FAILED,
                                 "现场启动按钮被拒 —— %s" % why, now_ms)
                return
            # [粘性分账 2026-11-18] 现场启动按钮按**用户选中的模式**跑
            # （= 客户端下拉选的那个；op=5 记在这里）⇒ 与屏幕上的启动
            # 按钮同效。此前这里用 self.mode（运行中模式/循环子阶段）⇒
            # 下拉改了但按钮按旧模式启动（板端实锅）。
            want_mode = self.selected_mode or self.mode
            why_mode = self.mode_config_error(want_mode)
            if why_mode:
                self._log("自动运行：现场启动按钮被拒 —— %s" % why_mode)
                self.raise_alarm(proto.AUTO_ALARM_ACTION_FAILED,
                                 "现场启动按钮被拒 —— %s" % why_mode, now_ms)
                return
            changed = self.start(now_ms, mode=want_mode)
            self._log("自动运行：现场启动按钮受理（%s）"
                      % ("已启动" if changed else "已在运行"))

    def start_preconditions(self, auto_mode):
        """启动的全部条件。返回 (ok, 拒绝原因)。

        [条件顺序] 先判模式再判锁存再判急停：模式不对是"操作员没切"（配置
        问题），报警/急停锁存是"上次出过事要人工确认"（安全问题），急停
        回路是"当下安全件"。从配置问题到安全问题的顺序报，现场看到的
        提示才一层层可理解。
        """
        if not auto_mode:
            return False, "当前是手动模式（按钮启停只在自动模式下生效）"
        if self._fault_latched:
            return False, "有报警未复位，需先按复位再启动"
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

    def mode_config_error(self, mode):
        """校验一个模式的配置能否跑。返回 ""（可跑）或拒绝原因。

        [两个入口同源] 远程启动（_h_auto_control，要回错因给界面）与现场
        启动按钮（_handle_buttons，只记日志）都调这里 —— 没有"远程能跑、
        现场不能跑"的分歧空间。
        """
        ids = tuple(m for m, _t in proto.AUTO_MODES)
        if mode not in ids:
            if mode == proto.AUTO_MODE_AUTO:
                # [组合模式 2026-11-12] 循环已实现 ⇒ 校验循环配置（参与
                # 循环的模式都得配设备，否则循环到那个阶段会无阀可动）。
                return self._cycle_config_error()
            return "未选择自动模式（先在客户端主界面选模式）"
        cfg_mode = self.ctx.auto_mode_config(mode)
        devices_sel = set(cfg_mode.get("devices") or [])
        if not devices_sel:
            return "模式[%s]未配置设备（远端设置 → 自动模式配置）" % mode
        return self._mode_device_error(mode)

    def _mode_device_error(self, mode):
        """模式的**设备级**校验（配了设备，但现场装不上 ⇒ 跑不起来）。

        [为什么抽出来] 单模式分支与**组合循环分支**共用同一份口径（原来
        循环分支只查"配没配设备"，漏掉这三类 ⇒ 循环跑到那个阶段才炸）。
        校验项：
          · 选中的阀**没有点位**（现场没接线）⇒ 无法参与该模式；
          · 选了搅拌泵但**变频器未装配**；
          · 选了进水泵但**泵组未装配**。

        :return: ""（可跑）或拒绝原因。
        """
        cfg_mode = self.ctx.auto_mode_config(mode)
        devices_sel = set(cfg_mode.get("devices") or [])
        for i in range(self.ctx.valve_count()):
            if ("valve%d" % (i + 1)) in devices_sel and \
                    not self.ctx.valve_ready(i):
                return "阀%d 未配置点位，无法参与模式[%s]" % (i + 1, mode)
        if "stirPump" in devices_sel and self.ctx.vfd_snapshot() is None:
            return "搅拌泵（变频器）未装配，模式[%s]无法启动" % mode
        if "inletPump" in devices_sel and self.ctx.pump_bank() is None:
            return "进水泵未装配，模式[%s]无法启动" % mode
        return ""

    #: 急停"状态未知"（None）要**连续**多少拍才按急停锁存（tick 100ms ⇒
    #: 5 拍 = 500ms 持续无数据；单帧空应答/单拍超时不会触发假急停）。
    ESTOP_NONE_LATCH_TICKS = 5

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

        [None（读不到）与 False（无信号）要分开对待 —— 2026-11-16 审计]
            · **False** = 回路确实不通（按下 / 断线——常闭口径"断线=按下"
              是安全侧设计）⇒ **立即锁存**，不等待；
            · **None** = 数字量模块没采到数据（总线抖动 / 模块掉线）——
              与"有人按急停"是两回事。原来 None 也立即锁存，而
              `DigitalIo` 又是单拍失败即判离线 ⇒ **一帧空应答就全停 +
              锁存**（现场那台 MT3A 文档记载会间歇性空应答，这不是理论
              场景）。⇒ None 要**连续 ESTOP_NONE_LATCH_TICKS 拍**仍读不到
              才按"急停状态未知持续"锁存；期间只禁止启动，不惊动现场。
              任何一拍读到 True/False 都把计数清零。
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
            self._estop_none_streak = 0
            if self.phase == "estop" and not self._estop_latched:
                self.phase = "idle"
                self._sync_station()
            return
        if estop_ok is None:
            # [未知要持续才锁存] 见上面 docstring 的 None/False 区分。
            if not self._estop_ever_ok:
                if not getattr(self, "_estop_boot_warned", False):
                    self._estop_boot_warned = True
                    self._log("自动控制：急停状态未知（首帧未到，暂不锁存；"
                              "采到急停正常后才按真实急停处理）")
                return
            self._estop_none_streak = getattr(self, "_estop_none_streak", 0) + 1
            if self._estop_none_streak < self.ESTOP_NONE_LATCH_TICKS:
                return
            reason = "急停状态未知（数字量模块持续无数据）"
        else:
            self._estop_none_streak = 0
            if not self._estop_ever_ok:
                # [首帧未到不锁存——对 False 同样适用 2026-11-16] DigitalIo
                # 没 poll 过时 di_bit 返回 0（=False），它同样是"首帧未到"
                # 的一种形态；2026-11-06 板端实锅的判据（没见过回路正常
                # 之前只禁启、不锁存）不能因为 None/False 拆分而丢掉。
                if not getattr(self, "_estop_boot_warned", False):
                    self._estop_boot_warned = True
                    self._log("自动控制：急停无信号（首帧未到，暂不锁存；"
                              "采到急停正常后才按真实急停处理）")
                return
            # False = 回路确实不通：按下或断线，立即锁存。
            reason = "急停无信号（按下或断线）"
        if not self._estop_latched:
            # [用 '→' 不用 '⇒'] '⇒'(U+21D2) **GBK 编不了** ⇒ 中文 Windows
            # 上这一条日志会把 print 打死（整个急停处理崩在日志里）。
            # '→'(U+2192) 在 GBK 里是有的（0xA1FA）⇒ 换它。
            self._log("自动控制：%s → 停止自动运行并全停设备" % reason)
            # [急停触发点] 报警在本方法调用的 e_stop() 里自动入队
            # （EmergencyStop），app._auto_step 每拍抽取发布 —— 这里不重复 raise。
            self.e_stop(reason)

    def _step_impl(self, now_ms):
        """运行中的工艺逻辑 = 模式序列状态机（异常由 step() 兜住）。

        [三模式序列 2026-11-11 用户口径]
            阀相(valves)：开设置中选中的阀、关没选中的阀 → 等全部到位；
                          超时/故障 ⇒ 报警 + 停止自动模式（锁存，复位后再启）。
            泵相(pumps) ：按配置启动搅拌泵（给定转速/方向）与进水泵。
            运行相(running)：设备保持运行，等停止按钮/急停。
        急停闸已在 `poll_idle` → `_check_estop` 每拍执行（控制器没跑也生效），
        这里不重复。组合模式「自动」（按压差/定时在三个模式间循环）长在
        running 相（_cycle_tick）。
        """
        if self.phase == self.PHASE_VALVES:
            self._step_valves(now_ms)
        elif self.phase == self.PHASE_PUMPS:
            self._step_pumps(now_ms)
        elif self.phase == self.PHASE_RUNNING:
            # running：设备保持运行；组合模式下这里做**循环换相**判定。
            if self._cycle_root:
                self._cycle_tick(now_ms)
            # [运行中周期性持久化累计时长 2026-11-12] 断电/崩溃最多丢
            # 一个持久化窗口的时长（60s）。
            if self.ctx.throttle("_run_ms_persist", self.RUN_PERSIST_MS,
                                 now_ms):
                self.ctx.persist_run_ms(self.run_ms(now_ms))

    # ---- 组合模式「自动」的循环调度（2026-11-12 用户定案）----

    def _cycle_cfg_get(self, key, default):
        try:
            value = self._cycle_cfg.get(key, default)
        except Exception:                           # noqa: BLE001
            return default
        return default if value is None else value

    def _cycle_tick(self, now_ms):
        """循环换相判定（只在 running 相、组合模式下跑）。

        [顺序 · 2026-11-18 配置化] **按参与列表的位置推进**（不再写死
        产水→反洗→顺洗）：设参与列表为 stages、当前阶段下标为 idx，
        下一个 ＝ stages[(idx+1) % len(stages)]（最后一个回到第一个 ⇒ 闭环）。

        [切换条件 · 每阶段各自一份] 取**当前阶段自己的**配置（stageCfg）：
          · **压差触发**（pressEnabled）：压差达标就切
            （pressThreshold + pressCompare；gt=大于 / lt=小于）；
            **压差读不到（None）⇒ 不切**（不按缺数据误判，沿用安全侧）；
          · **定时触发**（timerEnabled）：本阶段已耗时 ≥ timerMinutes 就切；
          · 两者**互不排斥、先到先切**（压差先到就按压差切，定时先到就按
            定时切）；两个都没开 ⇒ 配置非法，**启动闸已拒**（跑不到这里）。

        [时长口径] 统一走 `_cycle_stage_limit_ms`（与表盘同一处）。
        [防御] 当前阶段不在列表里 ⇒ **什么都不做**（宁可不换相也不要崩）。
        [换相动作] 切换子模式 = 停泵 + 按新模式重建阀目标（走一遍阀相→
        泵相）——与手动启动该模式同一条序列。
        """
        stages = self._cycle_stages()
        idx = self._cycle_stage_index()
        if idx < 0 or len(stages) < 2:
            # 防御：阶段不在列表里（配置被改过/记忆失效）或列表只剩 1 个
            # （1 个没有循环意义 ⇒ 启动闸已拒）⇒ 不换相（宁可不换也不要
            # 崩，也不要拿同一个模式反复重启相位）。
            return
        nxt = stages[(idx + 1) % len(stages)]
        elapsed_ms = now_ms - self._cycle_stage_at
        cfg = self._cycle_stage_cfg(self.mode)
        # ---- 压差触发（该阶段开了才读压差计）----
        if bool(cfg.get("press_enabled", False)):
            press = self.ctx.press_mpa()
            threshold = float(cfg.get("press_threshold",
                                      proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF))
            compare = str(cfg.get("press_compare",
                                  proto.AUTO_CYCLE_PRESS_COMPARE_DEF))
            if press is not None:
                hit = (press > threshold
                       if compare == proto.AUTO_PRESS_COMPARE_GT
                       else press < threshold)
                if hit:
                    self._cycle_switch(
                        nxt, "压差 %.3f MPa %s 阈值 %.3f"
                        % (press,
                           ">" if compare == proto.AUTO_PRESS_COMPARE_GT
                           else "<", threshold))
                    return
        # ---- 定时触发（没开 ⇒ limit=0 ⇒ 永不触发）----
        limit_ms = self._cycle_stage_limit_ms(self.mode, idx)
        if limit_ms and elapsed_ms >= limit_ms:
            self._cycle_switch(nxt, "定时（%s %d 分钟到期）"
                               % (self._mode_name(), limit_ms // 60000))

    def _cycle_switch(self, new_mode, reason=""):
        """循环换相：按**目标模式**的换相策略处置泵 → 按新模式重建阀目标
        （回阀相）。

        [为什么不整体 stop/start] 停机会清累计运行时间、也会把相位打回
        idle；换相是**同一个运行过程内**换子模式，累计要连续。
        [泵怎么办 2026-11-14 用户定案] 换相窗口里阀要走 ~9s 行程，泵不
        一定该停 ⇒ 逐泵可配（见 `_apply_switch_policies`）。
        """
        self._log("自动循环：%s → 切换到[%s]（%s）"
                  % (self._mode_name(), self._mode_name_of(new_mode), reason))
        # 泵按目标模式的换相策略处置（缺省＝先停再启＝既有行为）。
        self._apply_switch_policies(new_mode)
        self._begin_mode_phase(new_mode)
        self._cycle_stage_at = self.ctx.now_ms()

    # ---- 换相策略（2026-11-14 用户定案）----
    def _switch_policy(self, mode, key):
        """读某模式某泵的换相策略（缺键/非法 ⇒ 缺省「先停再启」）。"""
        cfg = self.ctx.auto_mode_config(mode) or {}
        val = str(cfg.get(key) or "").strip()
        return val if val in proto.AUTO_SWITCH_POLICY_IDS \
            else proto.AUTO_SWITCH_POLICY_DEF

    def _apply_switch_policies(self, new_mode):
        """按**目标模式**的换相策略处置两个泵（只管**换相窗口内**）。

        [三种策略]
          stopRestart —— **保持之前的逻辑**：换相先停泵，阀到位后（泵相）
                        再按新模式配置启动；
          carry      —— **保持上一个模式的状态**：换相途中**不动**该泵
                        （物理现状就是上一模式的状态）；
          direct     —— **直接切到当前模式的状态**：换相**立即**按新模式
                        的目标启停该泵（与阀门切换并行）。
        [为什么要逐泵一份] 现场诉求是「搅拌泵要保持运行，进水泵可以先停」
        ——两个泵诉求不同 ⇒ 逐设备可配。
        [carry 的收尾] carry 且新模式**不选**该泵时，它在换相途中仍在转；
        换相完成（泵相）必须把它停掉，否则会一直转 ⇒ 记进
        `_carry_pending_stop`，由泵相收尾（见 `_step_pumps`）。
        """
        cfg_new = self.ctx.auto_mode_config(new_mode) or {}
        selected = set(cfg_new.get("devices") or [])
        actions = {}
        policies = {}
        for dev_id, key in (("stirPump", proto.AUTO_MODE_STIR_SWITCH),
                            ("inletPump", proto.AUTO_MODE_INLET_SWITCH)):
            pol = self._switch_policy(new_mode, key)
            policies[dev_id] = pol
            want = dev_id in selected
            if pol == proto.AUTO_SWITCH_POLICY_CARRY:
                actions[dev_id] = "keep"
            elif pol == proto.AUTO_SWITCH_POLICY_DIRECT:
                actions[dev_id] = "start" if want else "stop"
            else:
                actions[dev_id] = "stop"
            # [carry 收尾] 保持运行但新模式不要它 ⇒ 泵相必须停。
            if actions[dev_id] == "keep" and not want:
                self._carry_pending_stop.add(dev_id)
        self._log("自动循环：换相策略 搅拌泵=%s→%s 进水泵=%s→%s（目标模式[%s]）"
                  % (policies["stirPump"], actions["stirPump"],
                     policies["inletPump"], actions["inletPump"],
                     self._mode_name_of(new_mode)))
        if actions["stirPump"] == "keep" and actions["inletPump"] == "keep":
            return                       # 两个都不用动
        async def _run():
            try:
                # ---- 进水泵 ----
                if actions["inletPump"] == "stop":
                    await self._stop_pumps()
                elif actions["inletPump"] == "start":
                    await self._start_inlet_pump()
                # ---- 搅拌泵（变频器）----
                if actions["stirPump"] in ("stop", "start"):
                    vfd = self.ctx.vfd_snapshot()
                    if vfd is not None:
                        if actions["stirPump"] == "stop":
                            await self._stop_stir(vfd)
                        else:
                            await self._start_stir(vfd, cfg_new)
            except Exception as exc:                # noqa: BLE001
                self._log("自动循环：换相处置泵异常 %s" % exc)
        try:
            # [get_running_loop 而不是 get_event_loop] get_event_loop 在
            # **没有运行中 loop** 的线程会**新建**一个 loop（3.12 起是
            # 明确行为）⇒ 任务被投递到一个没人跑它的事件循环 ⇒ 退出时
            # "Task was destroyed but it is pending" 噪音 + 动作丢失。
            # 没有运行中的 loop ⇒ 本来就不该投递（换相同步入口不能 await）。
            loop = asyncio.get_running_loop()
        except RuntimeError:
            loop = None
        if loop is not None and not loop.is_closed():
            try:
                self._track_phase_task(asyncio.ensure_future(_run()))
            except Exception as exc:                # noqa: BLE001
                self._log("自动循环：换相处置泵投递失败 %s" % exc)
        else:
            self._log("自动循环：换相处置泵未投递（当前无运行中的事件循环）")

    # [旧的「换相一律停泵」实现 2026-11-14] 已被 `_apply_switch_policies`
    # 取代（逐泵策略：先停再启 / 保持上一模式 / 直接切到当前模式）⇒ 删除，
    # 免得两套逻辑并存（新的策略在 carry/direct 下**不能**先停泵）。

    def _mode_name_of(self, mode):
        for mid, name in proto.AUTO_MODES:
            if mid == mode:
                return name
        return mode

    # ---- 阀相 ----
    def _step_valves(self, now_ms):
        """下发阀命令（节流重发）+ 校验全部到位；超时/故障即中止模式。"""
        # 1) 命令下发：未受理的阀逐个投递（失败按 VALVE_CMD_RETRY_MS 重试）。
        for idx, want in sorted(self._valve_targets.items()):
            if idx in self._valve_cmd_acked:
                continue
            if not self.ctx.throttle("autovalve%d" % idx,
                                     self.VALVE_CMD_RETRY_MS, now_ms):
                continue
            coro = (self.ctx.open_valve(idx) if want
                    else self.ctx.close_valve(idx))
            self._track_phase_task(asyncio.ensure_future(
                self._valve_cmd_done(idx, coro)))
        # 2) 到位校验：任何一路判故障（阀层超时/互斥反馈）⇒ 立即中止；
        #    还有没到位的 ⇒ 等下一拍，整体超过截止时刻 ⇒ 序列超时中止。
        # [等待口径 2026-11-13 用户定案] 未选阀打到关闭状态**是要等待的**
        # （确保未参与生产的阀确实关回才开泵）——目标表含未选阀（关目标），
        # 等待含未选阀。现场未接线的阀给不出关到位 ⇒ 超时报警=保护行为
        # （要么接线、要么接受该时段的报警）。
        for idx, want in sorted(self._valve_targets.items()):
            state = self.ctx.valve_state(idx)
            if state == proto.HsfValveState.Fault:
                self._abort_mode(now_ms, proto.AUTO_ALARM_VALVE_TIMEOUT,
                                 "模式[%s]：阀%d 动作超时未到位（判故障）"
                                 % (self._mode_name(), idx + 1))
                return
            target = (proto.HsfValveState.Open if want
                      else proto.HsfValveState.Closed)
            if state == target:
                continue
            if now_ms > self._valve_deadline_ms:
                self._abort_mode(now_ms, proto.AUTO_ALARM_VALVE_TIMEOUT,
                                 "模式[%s]：阀%d 动作超时未到位"
                                 % (self._mode_name(), idx + 1))
                return
            return                      # 还有阀在途中，等下一拍
        # 3) 全部到位 → 泵相。
        self.phase = self.PHASE_PUMPS
        self._phase_at = now_ms
        self._log("自动模式[%s]：阀全部到位 → 启动泵" % self._mode_name())

    async def _valve_cmd_done(self, idx, coro):
        """等一记阀命令的受理结果；成功才记账（失败的下一拍节流重发）。"""
        try:
            ok, _err = await coro
        except Exception:                           # noqa: BLE001
            ok = False
        if ok:
            self._valve_cmd_acked.add(idx)

    # ---- 泵相 ----
    def _step_pumps(self, now_ms):
        """按配置下发泵启动；全部受理成功 ⇒ 进入运行相。

        [失败先重试再中止 2026-11-13·对齐 C 端泵相宽限] 泵写路径的前置
        在线判定是单拍瞬时值（总线一轮停滞的抖动窗会把"可写"误报成
        "离线"）：首败起 PUMP_PHASE_GRACE_MS 宽限、留在泵相逐拍重投
        （任务表置 None ⇒ 下一拍重建重发），过限仍失败才按与一次性失败
        完全相同的文字/类别中止（中止文案报首败原因，与 C s_pumpFirstErr
        同口径——重试期间错误会刷，报第一因）。
        """
        if self._pump_tasks is None:
            cfg_mode = self.ctx.auto_mode_config(self.mode)
            selected = set(cfg_mode.get("devices") or [])
            tasks = []
            if "stirPump" in selected:
                vfd = self.ctx.vfd_snapshot()
                if vfd is None:
                    self._abort_mode(now_ms,
                                     proto.AUTO_ALARM_DEVICE_UNAVAILABLE,
                                     "模式[%s]：搅拌泵（变频器）未装配"
                                     % self._mode_name())
                    return
                tasks.append(asyncio.ensure_future(
                    self._start_stir(vfd, cfg_mode)))
            if "inletPump" in selected:
                tasks.append(asyncio.ensure_future(self._start_inlet_pump()))
            # [换相策略收尾 2026-11-14] carry 的泵在换相途中仍在转，而新
            # 模式**不选**它 ⇒ 这里（泵相＝换相完成）必须把它停掉，否则
            # 它会一直转（泵相只"启动选中的"，从不停"没选的"）。
            #
            # [待停集合只在成功后清 2026-11-16 审计] 原来建表后立刻把
            # `_carry_pending_stop` 清空 ⇒ 收尾停泵一旦失败（总线抖动）进
            # 宽限重投时集合已空 ⇒ 重投只重建"选中泵"的启动任务 ⇒ 那台
            # carry 泵**永远没人停**（新模式没选它，运行相更不会停它）。
            # ⇒ 集合保留到验收：全部成功（进运行相）才清；失败重投时
            #   这里的 for 会再次为仍在集合里的泵建停止任务（STOP 幂等，
            #   重复下发无害）。宽限耗尽仍失败 ⇒ _abort_mode 中止（本来
            #   就该把"没停干净"当失败报出来）。
            for dev_id in sorted(self._carry_pending_stop):
                if dev_id in selected:
                    continue
                if dev_id == "stirPump":
                    vfd = self.ctx.vfd_snapshot()
                    if vfd is not None:
                        tasks.append(asyncio.ensure_future(
                            self._stop_stir(vfd)))
                elif dev_id == "inletPump":
                    tasks.append(asyncio.ensure_future(self._stop_pumps_agg()))
            # 配置至少有泵之外还可能有纯阀模式：无泵任务直接进运行相。
            self._pump_tasks = tasks
            if not tasks:
                self._carry_pending_stop = set()
                self.phase = self.PHASE_RUNNING
                self._log("自动模式[%s]：无泵配置，进入运行" % self._mode_name())
            return
        # 等全部下发任务落定，逐个验收。
        if not all(t.done() for t in self._pump_tasks):
            return
        first_err = ""
        for t in self._pump_tasks:
            try:
                ok, err = t.result()
            except Exception as exc:                # noqa: BLE001
                ok, err = False, str(exc)
            if not ok and not first_err:
                first_err = err or "未知"
        if not first_err:
            # 全部成功（含 carry 收尾停泵）⇒ 待停集合使命完成。
            self._carry_pending_stop = set()
            self._pump_deadline_ms = 0
            self._pump_first_err = ""
            self.phase = self.PHASE_RUNNING
            self._log("自动模式[%s]：设备全部启动，进入运行" % self._mode_name())
            return
        # ---- 宽限窗口：首败起算、逐拍重投；过限才中止 ----
        if self._pump_deadline_ms == 0:
            self._pump_deadline_ms = now_ms + self.PUMP_PHASE_GRACE_MS
            self._pump_first_err = first_err
            self._log("自动模式[%s]：泵启动失败 %s（%.1fs 内重试）"
                      % (self._mode_name(), first_err,
                         self.PUMP_PHASE_GRACE_MS / 1000.0))
        if now_ms <= self._pump_deadline_ms:
            # 留在泵相：任务表置 None ⇒ 下一拍重建重发（含已受理的泵——
            # START 类命令幂等，重发无害；C 端只重投未受理项，效果一致）。
            self._pump_tasks = None
            return
        self._abort_mode(now_ms, proto.AUTO_ALARM_ACTION_FAILED,
                         "模式[%s]：泵启动失败 %s"
                         % (self._mode_name(), self._pump_first_err or "未知"))

    async def _start_stir(self, vfd, cfg_mode):
        """启动搅拌泵：按配置给定转速（rpm）与方向。返回 (ok, err)。"""
        rpm = float(cfg_mode.get("stir_rpm", proto.AUTO_STIR_RPM_DEF))
        rev = bool(cfg_mode.get("stir_reverse"))
        try:
            return await vfd.apply_control(proto.OP_START, rev,
                                           0.0, rpm)
        except Exception as exc:                    # noqa: BLE001
            return False, str(exc)

    async def _stop_stir(self, vfd):
        """停搅拌泵（变频器停机）。返回 (ok, err)。"""
        try:
            return await vfd.apply_control(proto.OP_STOP, False,
                                           0.0, 0.0, 0.0, 0)
        except Exception as exc:                    # noqa: BLE001
            return False, str(exc)

    async def _start_inlet_pump(self):
        """启动进水泵（1 台，index 0）。返回 (ok, err)。"""
        bank = self.ctx.pump_bank()
        if bank is None:
            return False, "进水泵未装配"
        try:
            return await bank.set_pump(0, True)
        except Exception as exc:                    # noqa: BLE001
            return False, str(exc)

    # ---- 中止 ----
    def _abort_mode(self, now_ms, kind, text):
        """模式序列异常收场：报警 + 锁存 + 停控制器（**不动设备**）。

        [为什么不全停] 阀相超时时阀可能停在任意中间位——这时乱发"关阀"
        命令是叠加一个未知动作；泵相还没启动（中止只发生在阀相/泵相下发）。
        用户口径就是「发出报警，并停止自动模式」：设备保持现状，等人工
        处理；报警锁存保证必须按复位才能再启动。
        """
        self._fault_latched = True
        self._running = False
        # [累计运行时长结算 2026-11-12] 模式中止（阀超时等）同样结束
        # "正在运行"这一段。
        self._settle_run_time()
        # [粘性模式还原 2026-11-13·对齐 C] 中止的是组合模式 ⇒ self.mode 还原
        # 为 "auto"（复位后无 mode 再启动重进循环，与 C s_mode 口径一致）。
        was_cycle = (self._cycle_root == proto.AUTO_MODE_AUTO)
        self._clear_phase_state()
        if was_cycle and self.mode != proto.AUTO_MODE_AUTO:
            self.mode = proto.AUTO_MODE_AUTO
        self.phase = self.PHASE_IDLE
        self.raise_alarm(kind, text, now_ms)
        self._log(text + "（已停止自动模式，需复位后才能再启动）")
        self._sync_station()

    def _track_phase_task(self, task):
        """留住模式序列的异步任务引用（防 GC），完成自动摘除。"""
        self._phase_tasks.add(task)
        try:
            task.add_done_callback(self._phase_tasks.discard)
        except Exception:                           # noqa: BLE001
            pass

    def _mode_name(self):
        """当前模式的显示名（未知道 id 原样返回）。"""
        for mid, name in proto.AUTO_MODES:
            if mid == self.mode:
                return name
        return self.mode or "未选择"

    # ---- 诊断 ----
    def status(self):
        """当前状态（供日志/健康/测试查询）。"""
        st = self.ctx.button_station()
        snap = st.snapshot() if st is not None else None
        return {
            "running": self._running,
            "phase": self.phase,
            "mode": self.mode,
            "faultLatched": self._fault_latched,
            "estopLatched": self._estop_latched,
            # [暂停态 2026-11-19] 停止＝暂停 ⇒ 状态里带上"暂停中"的记忆
            # （模式/阶段/已用时长），诊断与测试都能看到。
            "paused": self._paused,
            "pauseRoot": self._pause_root,
            "pauseMode": self._pause_mode,
            "pauseElapsedMs": int(self._pause_elapsed_ms),
            "actions": self.ctx.action_count,
            "lastError": self.ctx.last_error,
            "pendingAlarms": self.pending_alarm_count(),
            "estopOk": self.ctx.estop_ok(),
            "buttonState": (None if st is None else st.state),
            "buttons": snap,
        }
