# -*- coding: utf-8 -*-
"""电磁阀设备层：6 路阀，每路 1 个 DO 控制 + 2 个 DI 反馈。

================================================================================
                          接线模型（每路）
================================================================================

    数字量模块 DO ──> 继电器/阀线圈      （写 1 = 通电 = 阀动作）
    数字量模块 DI <── 开到位行程开关      （闭合 = 到位）
    数字量模块 DI <── 关到位行程开关      （闭合 = 到位）

**阀类型**（影响 DO 语义，由配置决定）：

    single  单电控（弹簧复位）
            DO=1 开阀；DO=0 靠弹簧自动关阀
            → 只有一个线圈，断电即回关位

    double  双电控（双线圈，脉冲驱动）
            DO 是一个"保持型"输出：写 1 保持开、写 0 保持关，
            由外部中继自保持（现场常见接法）
            → 本实现按"保持型"处理：写 1=开、写 0=关

两者在**软件层表现一致**（写 1 开 / 写 0 关），差别只在现场继电器接线。
故这里不区分类型，只做"开关命令 + 反馈判读"。

================================================================================
                          点位映射（先预留，后对接）
================================================================================

现场还没确定用数字量模块的哪些端口，故点位**全部走配置**：

    "valves": {
      "enabled": true,
      "poll_ms": 200,
      "move_timeout_ms": 8000,        // 动作超时 → 判故障
      "items": [
        { "name": "进水阀",   "module": 0, "do": 0, "openDi": 0,  "closeDi": 1  },
        { "name": "出水阀",   "module": 0, "do": 1, "openDi": 2,  "closeDi": 3  },
        ...
      ]
    }

    module  = 数字量模块下标（按 cfg.digital.modules 顺序；现场现在只有
              一台 .13 ⇒ 恒为 0）
    do      = 该模块的 DO 通道号（0..15）
    openDi  = 开到位 DI 通道号
    closeDi = 关到位 DI 通道号

[当前现场接线（2026-11-06）] .12 已下线拆除，只剩 .13 单台 16 点：
    阀 n（n=0..5）：DO n，关到位 DI 2n，开到位 DI 2n+1
    —— 即 DO0..5 / DI0..11。

**未配置的项**（items 少于 6 路）自动补成"未装配"：
界面显示灰色占位，控制被拒，不会崩。这样先做界面、后接线的流程能走通。

================================================================================
                          状态判读
================================================================================

阀状态**只由两个到位反馈推导**，不看命令回显：

    openFb=1 closeFb=0  → Open     （开到位）
    openFb=0 closeFb=1  → Closed   （关到位）
    openFb=0 closeFb=0  → Moving   （在途中）
    openFb=1 closeFb=1  → Fault    （互斥反馈同时为真 = 接线/阀体故障）

**为什么不信命令回显**：写了 1 只代表"命令发出去了"，阀有没有真的动
要靠行程开关说话。工业现场最常见的故障就是"命令发了、阀卡住了"。

另加**动作超时**：从下发命令起超过 move_timeout_ms 仍未到位 → 判 Fault，
避免界面永远停在"动作中"。
"""

import time

from . import proto


# ============================================================================
#                        控制命令的处置结果
# ============================================================================
#
# [为什么要把它编码进应答] 客户端点了按钮，服务端可能做三件完全不同的事：
# 真的动了、原样重发了一次（不计时）、什么都没做（已到位）。若应答只回
# ok=True，界面无法区分"动了"和"本来就到位"——用户会以为按钮坏了。
# 故用 VALVE_CMD_* 明说，客户端据此给不同的状态栏文案。

#: 新动作已下发，20s 计时从此刻起算。
VALVE_CMD_ISSUED = "issued"
#: 动作途中收到同目标的重复请求：DO 重发了，但**计时不重置**。
#: （这道闸就是为了防止"每 20s 点一次"把卡阀的超时无限推后）
VALVE_CMD_REPEATED = "repeated"
#: 反向命令（开→关 / 关→开）：算新动作，计时重新起算。
VALVE_CMD_REVERSED = "reversed"
#: 已到位且命令与当前位置一致：什么都没做（幂等）。
VALVE_CMD_IGNORED = "ignored"
#: **外部改动**：有人绕过电磁阀接口直接写了 DO（数字量页/自动逻辑/现场
#: HMI），服务端检测到后接管为一次新动作。
VALVE_CMD_EXTERNAL = "external"

#: 处置结果 → 中文（客户端也有一份，口径必须一致）。
VALVE_CMD_TEXT = {
    VALVE_CMD_ISSUED: "命令已下发",
    VALVE_CMD_REPEATED: "动作中，重复命令不重置计时",
    VALVE_CMD_REVERSED: "已反向（重新计时）",
    VALVE_CMD_IGNORED: "已在目标位置，无需动作",
    VALVE_CMD_EXTERNAL: "输出被外部改动，已接管",
}


class ValveItem(object):
    """单路电磁阀的点位映射与运行时状态。"""

    def __init__(self, index, name="", module=None, do=None,
                 open_di=None, close_di=None):
        self.index = index
        self.name = name or ("电动球阀%d" % (index + 1))
        #: 点位映射（None = 未装配）
        self.module = module
        self.do = do
        self.open_di = open_di
        self.close_di = close_di
        # ---- 运行时 ----
        self.open_cmd = False        #: 最近一次下发的命令（意图）
        #: 阀认为的 DO 实际位置（用于检测"被外部改动"）。
        #: 缺省 False = 带电前阀在关位（现场失电弹簧复位）。
        #: [为什么需要] 数字量页可以直接写阀占用的 DO，那条路径不过
        #: set_valve。要发现这种改动只能比对"模块实际 DO"与本字段。
        self.known_do = False
        #: 是否已经用真实 DO 校准过 known_do（首拍校准标志）。
        #:
        #: [为什么需要] known_do 缺省 False 表示"上电前在关位"。若服务端
        #: 启动时现场阀本来就在**开位**（停电前开着的、现场 HMI 开过），
        #: 首拍就会把稳态误判成"外部改动"并起算一次假动作 —— 界面白闪一次
        #: "动作中"，20s 后还可能误报超时。故首拍只做校准，不改计时。
        self.known_do_seeded = False
        self.open_fb = False         #: 开到位反馈
        self.close_fb = False        #: 关到位反馈
        self.state = proto.HsfValveState.Unknown
        self.cmd_at_ms = 0           #: 下发命令的时刻（超时判定用）
        #: 命令要去的目标位置（True=开、False=关）。用于判"到位了没有"。
        self.cmd_target_open = False
        #: 是否**曾经**收到过到位反馈（区分"阀卡住"与"行程开关没接"）。
        self.ever_feedback = False
        #: 本次动作是否已经报过超时（**保证一次动作只告警一次**）。
        #: [为什么需要] 超时判据每拍都成立（now - cmd_at > timeout），
        #: 没有这个位就会每 200ms 推一条告警，客户端被刷屏。
        self.timeout_reported = False
        #: 本次动作是否已经报过互斥故障（同上，防刷屏）。
        self.conflict_reported = False
        #: 最近一次控制命令的处置结果（供应答告诉客户端"到底做了什么"）。
        #: 取值见 VALVE_CMD_*：'' = 无（还没收到过命令）
        #:   "issued"    新动作已下发（正向或反向，计时从此刻起）
        #:   "repeated"  同目标动作中的重复请求：DO 重发了，但**计时不重置**
        #:   "ignored"   已到位且同目标：什么都没做（幂等）
        self.last_cmd_result = ""
        self.updated_ms = 0
        self.valid = False
        self.text = ""
        self.last_error = ""

    # ---- 装配 ----
    @property
    def ready(self):
        """本路是否已配好点位（三个口都有）。"""
        return (self.module is not None and self.do is not None
                and self.open_di is not None and self.close_di is not None)

    def apply_config(self, item_cfg):
        """从配置字典填充点位。"""
        self.name = item_cfg.get("name") or ("电动球阀%d" % (self.index + 1))
        self.module = item_cfg.get("module")
        self.do = item_cfg.get("do")
        self.open_di = item_cfg.get("openDi")
        self.close_di = item_cfg.get("closeDi")

    # ---- 状态判读 ----
    def arrived(self):
        """命令目标位置是否已到位。

        [为什么按目标判而不是按状态判] 阀是"开到开到位/关到关到位"，
        命令开阀就该等开到位信号。只看"两个反馈都为 0 就算动作中"会把
        "到位了但另一个开关也吸着"（互斥故障）当成没到位。
        """
        if self.cmd_target_open:
            return bool(self.open_fb)
        return bool(self.close_fb)

    def refresh_state(self, now_ms, move_timeout_ms):
        """由反馈重新推导状态（含动作超时判定）。

        :returns: 本条**需要新发出的告警**（dict）或 None。
                  没有新告警返回 None —— 调用方据此决定是否推送。
        """
        alarm = None
        if not self.ready:
            self.state = proto.HsfValveState.Unknown
            self.text = "未配置点位"
            self.updated_ms = now_ms
            return None
        state = proto.valve_state_from_feedback(self.open_fb, self.close_fb)
        text = ""
        if state == proto.HsfValveState.Fault:
            text = "开/关到位信号同时有效（接线或阀体故障）"
            # 互斥故障：一次持续只报一次（否则每拍一条）。
            if not self.conflict_reported:
                self.conflict_reported = True
                alarm = self._make_alarm(proto.VALVE_ALARM_CONFLICT, text,
                                         now_ms)
        elif state == proto.HsfValveState.Moving:
            # [区分"真在动作"与"根本没有反馈信号"] 两个到位都为 0 有两种含义：
            #   a) 刚下过命令、还在动作中（正常：满开满关约 10s）
            #   b) **行程开关没接/没供电**，永远读不到到位信号
            # 只有 (a) 才能显示"动作中"；(b) 报"无到位反馈"让人去查接线，
            # 否则现场会看到 6 个阀永远"动作中"，完全不知道该查什么。
            if not self.cmd_at_ms:
                state = proto.HsfValveState.Unknown
                text = "无到位反馈（检查行程开关接线）"
            elif now_ms - self.cmd_at_ms > move_timeout_ms:
                state = proto.HsfValveState.Fault
                # 文案由 _timeout_text 统一生成（带"是否有过反馈"，保证稳定）。
                text = self._timeout_text(move_timeout_ms)
                if not self.timeout_reported:
                    # [一次动作只告警一次] 判据每拍都成立；没有这个标志位
                    # 会每 200ms 推一条，客户端被刷屏。
                    self.timeout_reported = True
                    alarm = self._make_alarm(proto.VALVE_ALARM_TIMEOUT, text,
                                             now_ms, move_timeout_ms)
            else:
                text = "动作中"
        else:
            # 反馈显示"在某一端到位了"（Open/Closed）。
            #
            # [关键：必须区分"到了目标位置"和"还停在原来的位置"]
            # 原来这里只要 state 是 Open/Closed 就 `cmd_at_ms = 0`，等于
            # 无条件认为动作结束。命令刚下发时，**反馈必然是旧位置**
            # （阀还没开始动、DI 还没切换），于是：
            #   命令开阀 → 反馈仍是"关到位" → 判为"已到位" → 计时器清零
            #   ⇒ 这一拍之后状态机认为"没有动作在进行"
            #   ⇒ 10s 动作期显示"无到位反馈（检查行程开关接线）"
            #   ⇒ 20s 超时永远不判
            # 实测：从数字量页写 DO0（或点开阀后立刻 poll 一次）就是这样，
            # 界面报"检查行程开关接线"而阀其实在正常开。
            #
            # 正确判据：只有**到位位置 == 命令目标位置**才算本次动作结束。
            # 否则阀还在途中（反馈是旧值），保持计时继续等。
            arrived_at_target = (state == proto.HsfValveState.Open
                                 if self.cmd_target_open
                                 else state == proto.HsfValveState.Closed)
            if self.cmd_at_ms and not arrived_at_target:
                # 停在原位置、目标在另一端 ⇒ 仍在动作中（等 DI 切换）
                state = proto.HsfValveState.Moving
                text = "动作中"
                if now_ms - self.cmd_at_ms > move_timeout_ms:
                    state = proto.HsfValveState.Fault
                    text = self._timeout_text(move_timeout_ms)
                    if not self.timeout_reported:
                        self.timeout_reported = True
                        alarm = self._make_alarm(proto.VALVE_ALARM_TIMEOUT,
                                                 text, now_ms,
                                                 move_timeout_ms)
            else:
                # 到位了（Open/Closed）。若此前报过超时，发一条"恢复"消警。
                if self.timeout_reported:
                    self.timeout_reported = False
                    alarm = self._make_alarm(
                        proto.VALVE_ALARM_CLEARED,
                        "已到位（%s）" % ("开" if state ==
                                         proto.HsfValveState.Open else "关"),
                        now_ms)
                self.conflict_reported = False
                self.cmd_at_ms = 0          # 到位即结束本次动作计时
        self.state = state
        self.text = text
        self.updated_ms = now_ms
        return alarm

    def _timeout_text(self, move_timeout_ms):
        """超时文案（统一生成，保证每拍一致）。

        [为什么要带"是否有过反馈"] 只在首次告警时判断一次会让后续各拍的
        text 退回笼统的"动作超时"，操作员刷新界面后看到的提示变弱，
        排查方向丢失。
        """
        if self.ever_feedback:
            return "动作超时未到位（%.0fs）" % (move_timeout_ms / 1000.0)
        return ("动作超时未到位（%.0fs）——全程无任何到位"
                "信号，优先查行程开关接线" % (move_timeout_ms / 1000.0))

    def _make_alarm(self, kind, text, now_ms, timeout_ms=0):
        return {
            proto.KEY_ALARM_VALVE_INDEX: int(self.index),
            proto.KEY_ALARM_VALVE_NAME: self.name,
            proto.KEY_ALARM_KIND: int(kind),
            proto.KEY_ALARM_TEXT: text,
            proto.KEY_ALARM_AT: int(now_ms),
            proto.KEY_ALARM_TIMEOUT_S: round(timeout_ms / 1000.0, 1),
        }

    def state_name(self):
        return STATE_NAMES.get(self.state, "未知")


#: 状态 → 中文（客户端也有一份，口径必须一致）。
STATE_NAMES = {
    proto.HsfValveState.Unknown: "未知",
    proto.HsfValveState.Closed: "已关",
    proto.HsfValveState.Open: "已开",
    proto.HsfValveState.Moving: "动作中",
    proto.HsfValveState.Fault: "故障",
}


class ValveBank(object):
    """6 路电磁阀组。

    [为什么它是"设备层"而不是点层] 客户端只问"6 个阀什么状态"，
    不知道也不该知道这几个口挂在哪台数字量模块上。点位映射留在服务端，
    将来接线一改只动配置，客户端零改动。
    """

    def __init__(self, digital_devices, items_cfg=None, log=None,
                 move_timeout_ms=8000):
        """digital_devices: devices.DigitalIo 列表（按 cfg.digital.modules 顺序）。"""
        self.digital = list(digital_devices or [])
        self.move_timeout_ms = move_timeout_ms
        self._log = log or (lambda msg: None)
        self.items = [ValveItem(i) for i in range(proto.HSF_VALVE_COUNT)]
        if items_cfg:
            for i, item_cfg in enumerate(items_cfg[:proto.HSF_VALVE_COUNT]):
                if isinstance(item_cfg, dict):
                    self.items[i].apply_config(item_cfg)

    # ---- 点位访问 ----
    def _module_of(self, item):
        """取该路对应的 DigitalIo 设备（越界/未装配返回 None）。"""
        if item.module is None:
            return None
        if not 0 <= item.module < len(self.digital):
            return None
        return self.digital[item.module]

    def ready_count(self):
        return sum(1 for item in self.items if item.ready)

    # ---- 采集 ----
    def poll(self, now_ms, stale_ms=5000):
        """从数字量模块缓存读反馈（**不发 IO**，数字量模块已在轮询）。

        :param stale_ms: 数字量模块数据的新鲜阈值；超过即视为不可用。
        :returns: 本次判读新产生的告警列表（可能为空）。

        [为什么返回告警而不是自己推] 本类不持有 XLink 服务端引用（设备层
        不该知道传输层）。判读产生的事件交回 app，由它推给订阅了阀主题的
        会话——这样 6 路阀同拍超时也只推几帧，且传输失败不影响判读。
        """
        alarms = []
        for item in self.items:
            module = self._module_of(item)
            if module is None or not item.ready:
                item.valid = False
                self._collect(alarms, item.refresh_state(
                    now_ms, self.move_timeout_ms))
                continue
            # 数字量模块的 DI 位图就是反馈源（它在自己的采集循环里已刷新）。
            # [新鲜度判据] DigitalIo 没有 is_fresh()（那是客户端 model 的方法），
            # 服务端设备对象有自己的 online/last_ok_ms 口径，用它们判断。
            if not module.online or (now_ms - module.last_ok_ms) > stale_ms:
                item.valid = False
                item.last_error = "数字量模块数据不新鲜"
                self._collect(alarms, item.refresh_state(
                    now_ms, self.move_timeout_ms))
                continue
            try:
                item.open_fb = module.di_bit(int(item.open_di))
                item.close_fb = module.di_bit(int(item.close_di))
            except Exception as exc:                    # noqa: BLE001
                item.valid = False
                item.last_error = str(exc)
                self._collect(alarms, item.refresh_state(
                    now_ms, self.move_timeout_ms))
                continue
            if item.open_fb or item.close_fb:
                item.ever_feedback = True
            item.valid = True
            item.last_error = ""
            # ---- 接管"被外部改动的 DO" ----
            #
            # [实锅 2026-11-05：从数字量页翻 DO，电磁阀页状态错乱]
            # 数字量页可以直写任意 DO（含阀占用的 16..21）。那条路径走的是
            # `_h_digital_control` → `DigitalIo.write_do`，**不经过
            # ValveBank.set_valve**，所以：
            #   · `cmd_at_ms` 没被置位 ⇒ 状态机以为"从来没有命令"
            #   · 于是两个到位都为 0 的那 10 秒落到"无到位反馈（检查行程
            #     开关接线）"分支 —— 把人往接线故障上引，而阀其实在正常动作
            #   · 20s 超时也永远不会判（没有起算点）
            #
            # 修法：这里把模块当前的 DO 值与"阀上次已知的开关位置"比对，
            # 不一致就说明有人（数字量页/自动逻辑/现场 HMI）直接改了输出，
            # 接管为一次新动作：记目标、起算超时。
            # 放在 poll 里而不是只修 _h_digital_control，是因为**任何**绕过
            # set_valve 的写入都该被接管，包括将来的自动控制与外部系统。
            self._adopt_external_do(item, module, now_ms)
            self._collect(alarms, item.refresh_state(
                now_ms, self.move_timeout_ms))
        return alarms

    def _adopt_external_do(self, item, module, now_ms):
        """检测并接管"绕过 set_valve 直接写的 DO"。

        [判据] 模块实际 DO 位 != 阀认为的开关位置（`item.known_do`）。
        这是唯一可靠的信号：DO 是"命令意图"的物理落地，被外部改了就必须
        重新建立动作上下文，否则状态机无从判断"这 10 秒该算动作中还是故障"。
        """
        try:
            actual_on = module.do_bit(int(item.do))
        except Exception:                               # noqa: BLE001
            return                  # 读不到 DO 就不接管（下一拍再试）
        # ---- 首拍校准：只记位置，不当成动作 ----
        #
        # [校准条件] 必须等数字量模块**真正采过至少一帧**（last_ok_ms>0）。
        # 服务端刚起来时 `dev.do` 还是空数组/全 0，此时校准会把"还没读到"
        # 当成"阀在关位"；等真机反馈一到就变成"外部改动"，凭空起算一次
        # 假动作（实测：重启后阀页立刻显示"动作中"，20s 后还误报超时）。
        #
        # [为什么不用"有到位反馈"当条件] 阀卡死/行程开关没接时永远没有
        # 到位反馈，那样就永远不校准，外部改动也就永远检测不到。用
        # `last_ok_ms` 是"这一帧数据可信"的充分条件，与反馈内容无关。
        if not item.known_do_seeded:
            if not getattr(module, "last_ok_ms", 0):
                return              # 模块还没采到过一帧，本拍不校准
            item.known_do_seeded = True
            item.known_do = bool(actual_on)
            item.open_cmd = bool(actual_on)
            return
        if actual_on == bool(item.known_do):
            return                  # 与已知位置一致，没有外部改动
        # 阀正在动作中：DO 被改只可能是"外部又改了一次"，也算新动作。
        # 同样接管（重新计时是合理的——这是一次新的物理动作请求）。
        prev = bool(item.known_do)
        item.known_do = bool(actual_on)
        item.open_cmd = bool(actual_on)
        item.cmd_target_open = bool(actual_on)
        item.cmd_at_ms = int(now_ms)
        item.timeout_reported = False
        item.conflict_reported = False
        item.last_cmd_result = VALVE_CMD_EXTERNAL
        self._log("电磁阀 %s（%s）检测到 DO 被外部改动 %s→%s，"
                  "接管为一次新动作（起算 %dms 超时）"
                  % (item.name, item.index,
                     "开" if prev else "关",
                     "开" if actual_on else "关",
                     self.move_timeout_ms))

    @staticmethod
    def _collect(bucket, alarm):
        if alarm:
            bucket.append(alarm)

    # ---- 控制 ----
    def _same_target_already_met(self, item, on):
        """「已经是这个位置了」——命令与当前状态一致，无需再动。

        [为什么必须在服务端拦] 客户端已把到位那一侧的按钮置灰，但那只是
        **界面层**的便利：网络重发、C 端客户端、脚本直连都能绕过。若服务端
        照单全收，后果是**白写一次 DO + 重置 20s 计时器** —— 阀卡住时每
        20s 内点一次同侧，超时告警就永远不会触发（实测连点 8 次，0 告警）。

        判据用**到位反馈**（不是命令回显）：只有真的到位才算"已经是"。
        命令回显在阀卡住时同样为 True，用它判会把"卡住的阀"误认为到位。
        """
        target_open = bool(on)
        if target_open:
            return item.state == proto.HsfValveState.Open
        return item.state == proto.HsfValveState.Closed

    def _same_target_in_flight(self, item, on):
        """同一目标**正在动作中**（还没到，但已在计时）。

        [为什么不重置计时器] 这是"卡阀被反复点击掩盖"的直接成因：
            `set_valve` 原来无条件 `cmd_at_ms = now`，用户每 20s 内点一次
            同侧就能把超时无限推后。同目标是**同一次动作的重复请求**，
            不是新动作 —— 计时器必须从第一次命令起算。
        """
        return (item.cmd_at_ms > 0
                and bool(item.cmd_target_open) == bool(on)
                and item.state == proto.HsfValveState.Moving)

    async def set_valve(self, index, on):
        """下发开关命令。

        三种情况区别对待（**判定全在服务端**，客户端按钮置灰只是便利）：

            ① 已到位且就是目标位置  → 幂等：不写 DO、不重置计时器
            ② 同目标正在动作中      → 重复请求：不重置计时器（防卡阀掩盖）
            ③ 反向 / 首次          → 新动作：写 DO 并从现在重新计时

        :returns: (ok: bool, err: str)
        """
        if not 0 <= index < len(self.items):
            return False, "阀号越界（0~%d）" % (len(self.items) - 1)
        item = self.items[index]
        if not item.ready:
            return False, "%s 未配置点位" % item.name
        module = self._module_of(item)
        if module is None:
            return False, "%s 对应的数字量模块未装配" % item.name
        if not module.online:
            return False, "%s 对应的数字量模块离线" % item.name

        # ---- ① 已到位且同目标：幂等返回 ----
        if self._same_target_already_met(item, on):
            item.last_cmd_result = VALVE_CMD_IGNORED
            self._log("电磁阀 %s（%s）已%s到位，忽略重复命令（不重置计时器）"
                      % (item.name, item.index, "开" if on else "关"))
            return True, ""

        # ---- ② 同目标动作中：不重置计时器 ----
        if self._same_target_in_flight(item, on):
            elapsed = int(time.monotonic() * 1000) - item.cmd_at_ms
            item.last_cmd_result = VALVE_CMD_REPEATED
            self._log("电磁阀 %s（%s）正在%s（已 %dms），重复命令不重置计时器"
                      "（超时仍从首次命令起算 %dms）"
                      % (item.name, item.index, "开" if on else "关",
                         elapsed, self.move_timeout_ms))
            # 命令本身仍然下发：万一上一次写丢了/被现场复位，重发是有益的。
            # 但**计时不重置**，所以卡阀无法靠反复点击掩盖。
            try:
                ok, err = await module.write_do(int(item.do), bool(on))
            except Exception as exc:                    # noqa: BLE001
                return False, "写 DO 失败：%s" % exc
            if not ok:
                return False, err or "写 DO 失败"
            return True, ""

        # ---- ③ 反向 / 首次：新动作 ----
        try:
            ok, err = await module.write_do(int(item.do), bool(on))
        except Exception as exc:                        # noqa: BLE001
            return False, "写 DO 失败：%s" % exc
        if not ok:
            return False, err or "写 DO 失败"
        reversing = item.cmd_at_ms > 0 and bool(item.cmd_target_open) != bool(on)
        item.last_cmd_result = (VALVE_CMD_REVERSED if reversing
                                else VALVE_CMD_ISSUED)
        item.open_cmd = bool(on)
        # [同步 known_do] 这是"自己写的"，不是外部改动；不同步的话 poll
        # 下一拍会把刚写下去的 DO 当成外部改动再接管一次。
        item.known_do = bool(on)
        # [记录目标位置 + 启动本次动作计时] 每路阀**独立**一个计时器：
        # 从下发命令起算，超过 move_timeout_ms（现场满开满关约 10s，
        # 配 20s 留余量）仍未到位 ⇒ 判超时并推送告警。
        item.cmd_target_open = bool(on)
        item.cmd_at_ms = int(time.monotonic() * 1000)
        # 新动作 ⇒ 上一轮的告警标志清零（否则第二次动作不会再报超时）。
        item.timeout_reported = False
        item.conflict_reported = False
        self._log("电磁阀 %s（%s）→ 命令%s%s，开始计 %dms 超时"
                  % (item.name, item.index, "开" if on else "关",
                     "（动作中反向，重新计时）" if reversing else "",
                     self.move_timeout_ms))
        return True, ""

    # ---- 载荷编码 ----
    def to_doc(self):
        """编码成 BSON 友好的 dict（键名见 proto.KEY_VALVE_*）。"""
        doc = {proto.KEY_VALVE_COUNT: len(self.items)}
        for item in self.items:
            i = item.index
            doc["v%d_%s" % (i, proto.KEY_VALVE_NAME)] = item.name
            doc["v%d_%s" % (i, proto.KEY_VALVE_OPEN_CMD)] = bool(item.open_cmd)
            doc["v%d_%s" % (i, proto.KEY_VALVE_OPEN_FB)] = bool(item.open_fb)
            doc["v%d_%s" % (i, proto.KEY_VALVE_CLOSE_FB)] = bool(item.close_fb)
            doc["v%d_%s" % (i, proto.KEY_VALVE_STATE)] = int(item.state)
            doc["v%d_%s" % (i, proto.KEY_VALVE_READY)] = bool(item.ready)
            doc["v%d_%s" % (i, proto.KEY_VALVE_VALID)] = bool(item.valid)
            if item.last_cmd_result:
                # 处置结果只在本次应答里读（to_doc 是通用的，故也带上；
                # 客户端只关心**控制应答**那一帧里的这个键）。
                doc["v%d_%s" % (i, proto.KEY_VALVE_CMD_RESULT)] = \
                    item.last_cmd_result
            if item.text:
                doc["v%d_%s" % (i, proto.KEY_VALVE_TEXT)] = item.text
        return doc

    # ---- 健康 ----
    def health_summary(self, now_ms, stale_ms=5000):
        """整组的健康摘要（供设备健康列表）。

        :returns: (health, online, text)
        """
        if not self.digital:
            return proto.HEALTH_OFFLINE, False, "数字量模块未装配"
        ready = self.ready_count()
        if ready == 0:
            return proto.HEALTH_OFFLINE, False, "未配置点位"
        faults = [item for item in self.items
                  if item.ready and item.state == proto.HsfValveState.Fault]
        if faults:
            return (proto.HEALTH_FAULT, True,
                    "%d 路故障：%s" % (len(faults),
                                      "、".join(f.name for f in faults[:3])))
        if any(item.ready and item.valid for item in self.items):
            return proto.HEALTH_OK, True, ""
        return proto.HEALTH_STALE, True, "无有效反馈"
