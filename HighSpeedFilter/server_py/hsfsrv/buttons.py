# -*- coding: utf-8 -*-
"""按钮组（现场操作柱）**采集与灯**：4 路 DI 输入 + 3 路 DO 指示灯。

================================================================================
                              现场定义（2026-11-06）
================================================================================

    输入 12 = 急停       （**常闭**：有信号 = 正常；无信号 = 按下/断线 = 禁止启动）
    输入 13 = 启动按钮   （**常开**，点动：不按 0，按下 1）
    输入 14 = 停止按钮   （**常闭**，点动：不按 1，按下 0）← 与急停同一种触点
    输入 15 = 复位按钮   （**常开**，点动：不按 0，按下 1）

    输出 7  = 启动灯     （自动运行中时常亮）
    输出 8  = 停止灯     （未运行时常亮；运行时常灭 —— 与启动灯**互斥**）
    输出 9  = 复位灯     （**点动**：只在按住复位时亮，松手就灭）

[为什么急停用"无信号=禁止"] 急停按钮是**常闭**触点：正常时回路通、DI=1；
按下（或线断了）DI=0。故判据必须是「DI 为 0 ⇒ 禁止启动」，而不是反过来
—— 后者在断线时会误判成"没按急停"从而放行，是安全事故级的错。

[停止为什么也是常闭] 用户口径：「停止是常闭的，不按就是通的」「跟急停一样」。
常闭的意义是**线断了 = 停止**，是安全侧接线。判据因此取**下降沿**（1→0），
与常开的启动/复位正好相反。触点类型可由配置覆盖（`normally_closed`）。

================================================================================
                      本文件只管"采集与灯"，不含决策（2026-11-06 改版）
================================================================================

[为什么把决策挪走] 用户明确要求：

    「按钮的逻辑应该写在之前预留的自动模块文件中」
    「不是根据物理按钮，是根据设备是否开始自动运行，启动按钮只是给一个信号，
      要满足条件才切换状态」

原实现把状态机（按一下启动就转 Running）直接写在这里，等于**物理按钮 = 设备
启动按钮**，绕过了模式授权、急停闸、自动运行授权三道条件。现在的分层是：

    buttons.py（本文件）      采集 DI 电平、算沿、按"外部给的状态"亮灯
          ↓ 上报按键沿（start_edge / stop_edge）
    autocontrol.py            决定"该不该真的进入自动运行"（条件判定 + 动作）
          ↓ 下发 auto_running 目标态
    buttons.py                set_running() ← 灯跟着**设备真实运行态**，不跟按键

即：按键只是**请求**。灯反映的是"系统实际在不在自动运行"，不是"你按没按过"。

[外部必须驱动] 本类的 `running` 由外部（autocontrol）通过 `set_running()`
设置；没有外部驱动时灯恒为"停止灯亮"。这样即使自动模块没装配/没跑，
操作柱也不会因为按了一下启动就亮成"运行中"——那是最误导人的假状态。

================================================================================
                            去抖与采样
================================================================================

数字量模块 200ms 轮询一拍，按键按下的物理时长通常 >200ms，故**单拍采样**
就够；但仍加一道 `debounce_ms`（缺省 300ms）防止机械抖动造成连续两个上升沿。

[实现取舍] 去抖对"上升沿"只在**同一按键**上生效：抖一下再抖回来不能算两次
启动。用 `_last_edge_ms[key]` 记上次确认沿的时刻，间隔不足就丢弃这次。
"""

import asyncio

from . import proto


# ============================================================================
#                              状态常量
# ============================================================================

class ButtonState(object):
    """按钮组状态机的状态值（字符串，便于日志阅读与跨端对齐）。

    [2026-11-06 语义变更] 现在这三个值描述的是**系统运行态**，不是按键状态：
        IDLE    = 未在自动运行（停止灯亮）
        RUNNING = 自动运行中（启动灯亮）
        ESTOP   = 急停无信号（禁止启动；灯按"停着"显示）
    决定权在 autocontrol.py，本类只是把这三个值反映到灯上。
    """
    IDLE = "idle"           # 停止
    RUNNING = "running"     # 运行
    ESTOP = "estop"         # 急停中（无信号）

    ALL = (IDLE, RUNNING, ESTOP)


#: 键名（与 cfg.button_items 的键一致）
KEY_ESTOP = "estop_di"
KEY_START = "start_di"
KEY_STOP = "stop_di"
KEY_RESET = "reset_di"
KEY_START_LAMP = "start_lamp_do"
KEY_STOP_LAMP = "stop_lamp_do"
KEY_RESET_LAMP = "reset_lamp_do"
KEY_MODULE = "module"
#: 常闭触点数组（如 ["stop_di"]）。见 ButtonStation.__init__ 的说明。
KEY_NORMALLY_CLOSED = "normally_closed"


class ButtonStation(object):
    """按钮组：读 DI 出按键沿、按外部给的运行态写三盏灯。

    [职责边界] 只做**设备侧**的两件事：采 DI、写 DO。
    不含任何"该不该启动"的判断 —— 那是 autocontrol.AutoController 的事
    （模式授权、急停闸、自动运行授权都在那边）。这样手动/自动两种模式可以
    共用同一套采集与灯，不会出现"手动模式下按钮也能启动设备"的漏洞。
    """

    def __init__(self, ctx, items, debounce_ms=300, log=None):
        """
        :param ctx: AutoContext（复用它的 read_di/read_do/write_do 缓存通道）
        :param items: cfg.button_items（DI/DO 点位字典）
        :param debounce_ms: 沿判定去抖窗口
        """
        self.ctx = ctx
        self.items = dict(items or {})
        self.debounce_ms = max(0, int(debounce_ms))
        self._log = log or (lambda msg: None)

        #: 三路按键的**触点类型**（True=常闭，False=常开）。
        #:
        #: [现场口径 2026-11-06，用户原话]
        #:   「停止是常闭的，不按就是通的」「跟急停一样」
        #: 即：启动/复位是常开（按下才通），停止是常闭（不按就通）。
        #:
        #: [为什么要做成可配] 触点类型由**现场接线**决定，不是软件常量。
        #: 早先硬编码成"三路都是常开"，停止按钮直接失效（按下是 1→0，
        #: 代码只认上升沿）。做成字典后换接法只改配置、不改代码。
        #: 配置项：`buttons.items.normally_closed = ["stop_di"]`（键名数组）。
        nc = (self.items.get(KEY_NORMALLY_CLOSED) or [KEY_STOP])
        self.normally_closed = set(nc)

        #: 系统运行态（**外部驱动**，见 set_running）。
        self.running = False
        #: 由 running/急停推导出来的灯态（供 snapshot/健康阅读）。
        self.state = ButtonState.IDLE

        # 上一次采到的原始电平（用于算沿）
        self._prev = {KEY_ESTOP: None, KEY_START: None,
                      KEY_STOP: None, KEY_RESET: None}
        # 上次确认沿的时刻（去抖）
        self._last_edge_ms = {}

        #: 本拍新确认的按键沿（**只有一拍的寿命**，poll 开头清空）。
        #: [为什么要"只活一拍"] 消费方（autocontrol）若某拍没跑（比如手动
        #: 模式），沿不该攒着下一拍补发 —— 那会让"很久以前按的启动"在切到
        #: 自动的瞬间突然生效。宁可丢沿，也不能延迟执行一个过期的意图。
        self.start_edge = False
        self.stop_edge = False
        self.reset_edge = False

        # 对外可见的实时电平（None = 读不到）
        self.estop_ok = None       # 急停回路正常？（DI=1）
        self.pressed = {KEY_START: False, KEY_STOP: False, KEY_RESET: False}

        # 灯的回读（写下去后由下一拍读回校准，None=未知）
        self.lamps = {KEY_START_LAMP: None, KEY_STOP_LAMP: None,
                      KEY_RESET_LAMP: None}
        #: 上次**写下去**的灯目标值（None = 还没写过 ⇒ 首拍必写一次，
        #: 把灯拉到确定状态）。
        self._lamp_cache = {KEY_START_LAMP: None, KEY_STOP_LAMP: None,
                            KEY_RESET_LAMP: None}

        #: 状态变化回调（app 用来发健康/日志）：fn(old, new)
        self.on_state_change = None

    # ---- 点位 ----
    def _ch(self, key):
        """点位字典里的通道号；缺/非法返回 None。"""
        v = self.items.get(key)
        try:
            v = int(v)
        except (TypeError, ValueError):
            return None
        return v if v >= 0 else None

    # ---- 外部驱动运行态 ----
    def set_running(self, running):
        """设置**系统实际是否在自动运行**（外部驱动，主动作方是 autocontrol）。

        灯严格跟随这个值，因此"切回手动/急停/自动运行被停"都会立刻表现为
        停止灯亮 —— 不需要额外通知，调用方只要把真实状态照实传进来。
        """
        self.running = bool(running)

    # ---- 单拍：采集 + 灯 ----
    def poll(self, now_ms):
        """采集一拍：读 DI → 判沿 → 写灯。

        返回本拍是否有**运行态**变化（state 字段变了）。
        按键沿放在 `start_edge` / `stop_edge` / `reset_edge` 上供外部读取，
        本方法**不推进运行态**（那由 autocontrol 决定）。
        """
        mod = self.items.get(KEY_MODULE)
        mod = 0 if mod is None else int(mod)

        # ---- 1. 读四路 DI ----
        raw = {}
        for key in (KEY_ESTOP, KEY_START, KEY_STOP, KEY_RESET):
            ch = self._ch(key)
            raw[key] = None if ch is None else self.ctx.read_di(ch, mod)

        # ---- 2. 急停（常闭：无信号 = 禁止）----
        # [保留三态 2026-11-06 板端实锅] 原来写 `(raw is True)`，把
        # **"还没采到"（None）** 和 **"真的无信号"（False）** 折成同一个
        # False。后果：服务端刚启动、数字量还没回第一帧时，这一拍被当成
        # 急停按下，autocontrol 立刻锁存，之后即使 DI12=1 也解不开 ——
        # 现场表现是"重启后按启动永远没反应，还得先按一次复位"。
        #
        # 现在 None 原样保留（= 未知），由下层区分：
        #   · 灯/状态：None 与 False 一样显示"停着"（安全侧的观感不变）
        #   · 锁存判定：None 不能触发锁存（见 AutoController._check_estop）
        if raw[KEY_ESTOP] is None:
            self.estop_ok = None
        else:
            self.estop_ok = bool(raw[KEY_ESTOP])

        # ---- 3. 算按键沿（带去抖）----
        # [本拍清空] 沿只活一拍，见 __init__ 里的说明。
        #
        # [⚠ 触点类型不同，沿的方向相反 —— 2026-11-06 现场订正]
        # 用户原话：「停止是常闭的，不按就是通的」「跟急停一样」。
        #
        #   启动 DI13：**常开**。不按=0，按下=1 ⇒ 取**上升沿**（0→1）
        #   停止 DI14：**常闭**。不按=1，按下=0 ⇒ 取**下降沿**（1→0）
        #   复位 DI15：常开（同启动）
        #
        # [旧实现为什么完全收不到停止] 原来三路一律取上升沿。停止常闭时，
        # 开机第一帧就是从"未知"跨到 1 —— 被当成"刚按下"（板端日志里那句
        # 「未在运行，现场停止按钮仍执行全停」就是这个假沿）；之后**真正
        # 按下**是 1→0 的下降沿，代码根本不看，于是按停止毫无反应。
        #
        # [常闭为什么也要防断线] 停止用常闭的意义正是"线断了 = 停止"。
        # 下降沿判据天然覆盖：断线看起来就是 1→0，等价于按下 —— 安全侧正确。
        self.start_edge = self.stop_edge = self.reset_edge = False
        for key in (KEY_START, KEY_STOP, KEY_RESET):
            cur = raw[key]
            prev = self._prev[key]
            if key in self.normally_closed:
                # 常闭：不按=1，按下=0 ⇒ 下降沿 1→0
                self.pressed[key] = (cur is False)
                edge = (cur is False and prev is True)
            else:
                # 常开：不按=0，按下=1 ⇒ 上升沿 0→1
                self.pressed[key] = (cur is True)
                edge = (cur is True and prev is not True)
            if edge and self._debounce_ok(key, now_ms):
                if key == KEY_START:
                    self.start_edge = True
                elif key == KEY_STOP:
                    self.stop_edge = True
                else:
                    self.reset_edge = True
            self._prev[key] = cur

        # ---- 4. 推导灯态（**只看运行态 + 急停，不看按键**）----
        old_state = self.state
        if self.estop_ok is not True:
            # 无信号或读不到：都显示"停着"（停止灯亮）。观感上一致即可 ——
            # 区别只在"要不要锁存"，那由 autocontrol 判（见上面第 2 步）。
            self.state = ButtonState.ESTOP
        elif self.running:
            self.state = ButtonState.RUNNING
        else:
            self.state = ButtonState.IDLE

        changed = (old_state != self.state)
        if changed:
            self._log("按钮组：灯态 %s → %s（运行=%s，急停%s）"
                      % (old_state, self.state,
                         "是" if self.running else "否",
                         "正常" if self.estop_ok is True
                         else ("读不到" if self.estop_ok is None
                               else "无信号")))
            if self.on_state_change:
                try:
                    self.on_state_change(old_state, self.state)
                except Exception:                       # noqa: BLE001
                    pass

        # ---- 5. 写灯 ----
        self._apply_lamps(now_ms, mod)
        return changed

    def _debounce_ok(self, key, now_ms):
        """同一按键的两次沿至少间隔 debounce_ms。"""
        last = self._last_edge_ms.get(key)
        if last is not None and (now_ms - last) < self.debounce_ms:
            return False
        self._last_edge_ms[key] = now_ms
        return True

    def _apply_lamps(self, now_ms, mod):
        """按运行态写三盏灯。

        [灯的目标值]
            启动灯 = (state == RUNNING)
            停止灯 = (state != RUNNING)    ← 与启动灯严格互斥
            复位灯 = pressed[reset]        ← 点动，纯镜像按键

        [急停态下停止灯也亮] 急停时系统实际是"停着的"，停止灯亮符合操作员
        直觉；启动灯必然灭。故停止灯判据写 `state != RUNNING` 而不是
        `state == IDLE`，两个状态都覆盖。

        [每拍回读实际状态，与目标不一致就补写]
        [实锅 2026-11-06 板端] 初版只记"上次写下去的目标值"（`_lamp_cache`），
        目标没变就不再写。于是数字量页整组清零（或任何外部写 DO7~9）
        把灯打灭后，**灯永远不再亮** —— 实测 5 拍全 0x0000。现场表现是
        "点过一次数字量页的清零，操作柱上的停止灯就再也不亮了"。

        修法：每拍用 `ctx.read_do()` 读回实际值，与目标不一致就写（幂等
        补写）。代价是每拍多 3 次本地缓存读（不是总线读），可以忽略；
        只在真的不一致时才发 FC05，故正常稳态不会刷总线。
        """
        targets = {
            KEY_START_LAMP: (self.state == ButtonState.RUNNING),
            KEY_STOP_LAMP: (self.state != ButtonState.RUNNING),
            KEY_RESET_LAMP: bool(self.pressed.get(KEY_RESET)),
        }

        for key, want in targets.items():
            ch = self._ch(key)
            if ch is None:
                continue
            # 回读实际电平（None = 读不到；此时按"不知道"处理，仍去写一次）
            try:
                actual = self.ctx.read_do(ch, mod)
            except Exception:                           # noqa: BLE001
                actual = None
            if actual is not None and bool(actual) == want:
                self._lamp_cache[key] = want            # 记录目标供快照用
                continue
            self._lamp_cache[key] = want
            # [先判 loop 再建协程] `ensure_future` 在没有运行中的事件循环
            # 时会抛 RuntimeError；但 `ctx.write_do(...)` 这个**协程对象
            # 已经建出来了**，投递失败后它永远没人 await —— 于是退出时冒出
            # "coroutine ... was never awaited"（实测离线单测里必现）。
            # 反过来先查 loop 再建协程，就不会产生孤儿协程。
            try:
                loop = asyncio.get_event_loop_policy().get_event_loop()
                if loop is None or loop.is_closed():
                    raise RuntimeError("无可用事件循环")
            except Exception as exc:                    # noqa: BLE001
                self._log("按钮组：写灯 DO%d=%d 无法投递（%s）：%s"
                          % (ch, 1 if want else 0, "无事件循环", exc))
                continue
            try:
                asyncio.ensure_future(self.ctx.write_do(ch, want, mod))
            except Exception as exc:                    # noqa: BLE001
                self._log("按钮组：写灯 DO%d=%d 投递失败：%s"
                          % (ch, 1 if want else 0, exc))

    # ---- 对外状态 ----
    def snapshot(self):
        """给健康/日志/客户端用的快照（纯数据，无副作用）。"""
        return {
            "state": self.state,
            "running": bool(self.running),
            "estopOk": (None if self.estop_ok is None
                        else bool(self.estop_ok)),
            "startPressed": bool(self.pressed.get(KEY_START)),
            "stopPressed": bool(self.pressed.get(KEY_STOP)),
            "resetPressed": bool(self.pressed.get(KEY_RESET)),
            "lamps": {
                "start": self._lamp_cache.get(KEY_START_LAMP),
                "stop": self._lamp_cache.get(KEY_STOP_LAMP),
                "reset": self._lamp_cache.get(KEY_RESET_LAMP),
            },
            "items": dict(self.items),
        }

    def health_text(self):
        """健康表里显示的地址串（"模块地址 · DI12~15 · DO7~9"）。"""
        di = [self._ch(k) for k in (KEY_ESTOP, KEY_START, KEY_STOP, KEY_RESET)]
        do = [self._ch(k) for k in (KEY_START_LAMP, KEY_STOP_LAMP,
                                    KEY_RESET_LAMP)]
        di_s = ("DI%d~DI%d" % (min(di), max(di))
                if all(d is not None for d in di) else "DI未配置")
        do_s = ("DO%d~DO%d" % (min(do), max(do))
                if all(d is not None for d in do) else "DO未配置")
        return "%s · %s" % (di_s, do_s)
