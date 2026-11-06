# -*- coding: utf-8 -*-
"""按钮组（现场操作柱）：4 路 DI 输入 + 3 路 DO 指示灯。

================================================================================
                              现场定义（2026-11-06）
================================================================================

    输入 12 = 急停       （**常闭**：有信号 = 正常；无信号 = 按下/断线 = 禁止启动）
    输入 13 = 启动按钮   （点动，按下为 1）
    输入 14 = 停止按钮   （点动，按下为 1）
    输入 15 = 复位按钮   （点动，按下为 1）

    输出 7  = 启动灯     （运行时常亮）
    输出 8  = 停止灯     （停止状态下常亮；运行时常灭 —— 与启动灯**互斥**）
    输出 9  = 复位灯     （**点动**：只在按住复位时亮，松手就灭）

[为什么急停用"无信号=禁止"] 急停按钮是**常闭**触点：正常时回路通、DI=1；
按下（或线断了）DI=0。故判据必须是「DI 为 0 ⇒ 禁止启动」，而不是反过来
—— 后者在断线时会误判成"没按急停"从而放行，是安全事故级的错。

================================================================================
                            状态机（用户口径）
================================================================================

    ┌──────────────────────────────────────────────────────────┐
    │ 急停无信号（DI12=0）                                      │
    │   ⇒ 禁止启动（按启动也不动），停止灯常亮                  │
    ├──────────────────────────────────────────────────────────┤
    │ Idle（停止）                                             │
    │   · 停止灯常亮、启动灯灭                                  │
    │   · 启动按钮上升沿 → 若急停正常 → 转 Running              │
    ├──────────────────────────────────────────────────────────┤
    │ Running（运行）                                          │
    │   · 启动灯常亮、停止灯灭（互斥）                          │
    │   · 停止按钮上升沿 → 转 Idle                             │
    └──────────────────────────────────────────────────────────┘

    复位灯**不参与状态机**：它只镜像复位按钮的实时电平（按住亮、松开灭），
    与运行/停止状态无关。复位按钮也**不改状态**（用户只要求灯是点动）。

[点动 vs 常亮] 三个按键都是点动按钮，但**灯有区别**：
    启动灯/停止灯 = 状态灯（常亮，表达"系统现在处于什么状态"）
    复位灯        = 动作灯（点动，表达"正在按这个按钮"）

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
    """按钮组状态机的状态值（字符串，便于日志阅读与跨端对齐）。"""
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


class ButtonStation(object):
    """按钮组：读 DI 推状态机、按状态写三盏灯。

    [与 AutoController 的关系] 本类只管"按钮采集 + 灯"这件**设备侧**的事，
    不含工艺逻辑（那在 AutoController 里）。分开的理由：按钮组即使不在自动
    模式也要正常亮灯（手动模式下操作员同样要看得到状态），故它由 app 每拍
    驱动，而不是挂在自动控制器里。
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

        self.state = ButtonState.IDLE

        # 上一次采到的原始电平（用于算沿）
        self._prev = {KEY_ESTOP: None, KEY_START: None,
                      KEY_STOP: None, KEY_RESET: None}
        # 上次确认沿的时刻（去抖）
        self._last_edge_ms = {}

        # 对外可见的实时电平（None = 读不到）
        self.estop_ok = None       # 急停回路正常？（DI=1）
        self.pressed = {KEY_START: False, KEY_STOP: False, KEY_RESET: False}

        # 灯的回读（写下去后由下一拍读回校准，None=未知）
        self.lamps = {KEY_START_LAMP: None, KEY_STOP_LAMP: None,
                      KEY_RESET_LAMP: None}
        #: 上次**写下去**的灯目标值（None = 还没写过 ⇒ 首拍必写一次，
        #: 把灯拉到确定状态）。只在目标变化时才写，避免每拍刷 DO。
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

    # ---- 单拍 ----
    def poll(self, now_ms):
        """采集一拍：读 DI → 判沿 → 推进状态机 → 写灯。

        返回本拍是否有状态变化。
        """
        mod = self.items.get(KEY_MODULE)
        mod = 0 if mod is None else int(mod)

        # ---- 1. 读四路 DI ----
        raw = {}
        for key in (KEY_ESTOP, KEY_START, KEY_STOP, KEY_RESET):
            ch = self._ch(key)
            raw[key] = None if ch is None else self.ctx.read_di(ch, mod)

        # ---- 2. 急停（常闭：无信号 = 禁止）----
        # [None 必须当成"不 OK"] 模块离线时 read_di 返回 None。此时**不能**
        # 假设急停正常放行 —— 把它当"无信号"即禁止启动，是安全侧默认。
        self.estop_ok = (raw[KEY_ESTOP] is True)

        # ---- 3. 算上升沿（带去抖）----
        edges = {}
        for key in (KEY_START, KEY_STOP, KEY_RESET):
            cur = raw[key]
            prev = self._prev[key]
            self.pressed[key] = (cur is True)
            rising = (cur is True and prev is not True)
            if rising and self._debounce_ok(key, now_ms):
                edges[key] = True
            self._prev[key] = cur

        # ---- 4. 推进状态机 ----
        old_state = self.state
        if not self.estop_ok:
            # 急停无信号：无论当前在哪，一律回到"急停"态（禁止启动）。
            self.state = ButtonState.ESTOP
        else:
            if self.state == ButtonState.ESTOP:
                # 急停恢复：回到停止态（不自动启动 —— 复位必须人工确认）
                self.state = ButtonState.IDLE
            if edges.get(KEY_START) and self.state == ButtonState.IDLE:
                self.state = ButtonState.RUNNING
            elif edges.get(KEY_STOP) and self.state == ButtonState.RUNNING:
                self.state = ButtonState.IDLE

        changed = (old_state != self.state)
        if changed:
            self._log("按钮组：状态 %s → %s（急停%s）"
                      % (old_state, self.state,
                         "正常" if self.estop_ok else "无信号"))
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
        """按当前状态写三盏灯。

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
            # 回读实际电平（None = 读到不；此时按"不知道"处理，仍去写一次）
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
            "estopOk": bool(self.estop_ok) if self.estop_ok is not None else None,
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
