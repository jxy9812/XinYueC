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

    module  = 数字量模块下标（0=.12，1=.13 …按 cfg.digital.modules 顺序）
    do      = 该模块的 DO 通道号（0..15）
    openDi  = 开到位 DI 通道号
    closeDi = 关到位 DI 通道号

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


class ValveItem(object):
    """单路电磁阀的点位映射与运行时状态。"""

    def __init__(self, index, name="", module=None, do=None,
                 open_di=None, close_di=None):
        self.index = index
        self.name = name or ("电磁阀 %d" % (index + 1))
        #: 点位映射（None = 未装配）
        self.module = module
        self.do = do
        self.open_di = open_di
        self.close_di = close_di
        # ---- 运行时 ----
        self.open_cmd = False        #: 最近一次下发的命令（意图）
        self.open_fb = False         #: 开到位反馈
        self.close_fb = False        #: 关到位反馈
        self.state = proto.HsfValveState.Unknown
        self.cmd_at_ms = 0           #: 下发命令的时刻（超时判定用）
        #: 是否**曾经**收到过到位反馈（区分"阀卡住"与"行程开关没接"）。
        self.ever_feedback = False
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
        self.name = item_cfg.get("name") or ("电磁阀 %d" % (self.index + 1))
        self.module = item_cfg.get("module")
        self.do = item_cfg.get("do")
        self.open_di = item_cfg.get("openDi")
        self.close_di = item_cfg.get("closeDi")

    # ---- 状态判读 ----
    def refresh_state(self, now_ms, move_timeout_ms):
        """由反馈重新推导状态（含动作超时判定）。"""
        if not self.ready:
            self.state = proto.HsfValveState.Unknown
            self.text = "未配置点位"
            return
        state = proto.valve_state_from_feedback(self.open_fb, self.close_fb)
        text = ""
        if state == proto.HsfValveState.Fault:
            text = "开/关到位信号同时有效（接线或阀体故障）"
        elif state == proto.HsfValveState.Moving:
            # [区分"真在动作"与"根本没有反馈信号"] 两个到位都为 0 有两种含义：
            #   a) 刚下过命令、还在动作中（正常，几秒）
            #   b) **行程开关没接/没供电**，永远读不到到位信号
            # 只有 (a) 才能显示"动作中"；(b) 报"无到位反馈"让人去查接线，
            # 否则现场会看到 6 个阀永远"动作中"，完全不知道该查什么。
            if not self.cmd_at_ms:
                state = proto.HsfValveState.Unknown
                text = "无到位反馈（检查行程开关接线）"
            elif now_ms - self.cmd_at_ms > move_timeout_ms:
                # [行程开关未接线时不误报故障] 超时了但**从未收到过任何到位
                # 信号**（两个反馈自始至终为 0）⇒ 更可能是行程开关没接，
                # 而不是阀卡住。这时报"故障"会天天误报，现场会忽略告警。
                # 判据：本路的历史上是否出现过至少一次到位信号。
                if not self.ever_feedback:
                    state = proto.HsfValveState.Unknown
                    text = ("命令已下发但无到位反馈（行程开关可能未接线）")
                else:
                    state = proto.HsfValveState.Fault
                    text = "动作超时未到位（%.1fs）" % (
                        move_timeout_ms / 1000.0)
            else:
                text = "动作中"
        self.state = state
        self.text = text
        self.updated_ms = now_ms

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
        """
        for item in self.items:
            module = self._module_of(item)
            if module is None or not item.ready:
                item.valid = False
                item.refresh_state(now_ms, self.move_timeout_ms)
                continue
            # 数字量模块的 DI 位图就是反馈源（它在自己的采集循环里已刷新）。
            # [新鲜度判据] DigitalIo 没有 is_fresh()（那是客户端 model 的方法），
            # 服务端设备对象有自己的 online/last_ok_ms 口径，用它们判断。
            if not module.online or (now_ms - module.last_ok_ms) > stale_ms:
                item.valid = False
                item.last_error = "数字量模块数据不新鲜"
                item.refresh_state(now_ms, self.move_timeout_ms)
                continue
            try:
                item.open_fb = module.di_bit(int(item.open_di))
                item.close_fb = module.di_bit(int(item.close_di))
            except Exception as exc:                    # noqa: BLE001
                item.valid = False
                item.last_error = str(exc)
                item.refresh_state(now_ms, self.move_timeout_ms)
                continue
            if item.open_fb or item.close_fb:
                item.ever_feedback = True
            item.valid = True
            item.last_error = ""
            item.refresh_state(now_ms, self.move_timeout_ms)

    # ---- 控制 ----
    async def set_valve(self, index, on):
        """下发开关命令。

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
        try:
            ok, err = await module.write_do(int(item.do), bool(on))
        except Exception as exc:                        # noqa: BLE001
            return False, "写 DO 失败：%s" % exc
        if not ok:
            return False, err or "写 DO 失败"
        item.open_cmd = bool(on)
        item.cmd_at_ms = int(time.monotonic() * 1000)
        self._log("电磁阀 %s（%s）→ 命令%s"
                  % (item.name, item.index, "开" if on else "关"))
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
