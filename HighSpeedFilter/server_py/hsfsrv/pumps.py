# -*- coding: utf-8 -*-
"""进水泵设备层：数字量模块上的一路 DO 输出（**只有输出，没有反馈**）。

================================================================================
                              接线
================================================================================

现场把进水泵的接触器线圈接在**第 6 路电磁阀的下一个 DO**：

    电磁阀 6 路占 DO0..DO5
    进水泵      占 DO6           ← 就在紧邻的下一个口

    模块：.13（单台 16 点；.12 已于 2026-11-06 下线拆除）

[点位为什么是 DO0..6 而不是 DO16..22] 原现场用 .12 作聚合主站，把 .13 的
16 点映射进自己地址空间的上半区，故阀门/泵原来编号为 DO16..21 / DO22。
.12 拆除后 .13 直接作主模块、点表从 0 起，同一批物理端子整体减 16 ⇒
DO0..5 / DO6。硬件没动，变的只是寻址口径。

**没有行程反馈** —— 泵不像阀那样有开/关到位信号，现场只控制接触器。
故本类不判到位、不设超时定时器：写下去就是命令，界面显示的是**命令状态**。

[为什么不做"命令失败"判据] 没有反馈就没有独立的真相源 —— 想判"泵到底转
没转"只能是猜。硬造一个"超时未到位"的告警等于撒谎。要真正监视泵，得现场
加一个运行反馈点（接触器辅助触点/流量开关），那时再扩展本类。

================================================================================
                          与电磁阀的区别（为什么另起一个类）
================================================================================

| | 电磁阀 | 进水泵 |
|---|---|---|
| 反馈 | 2 个 DI（开/关到位） | **无** |
| 状态来源 | 只由反馈推导 | 只由命令推导 |
| 动作超时告警 | 有（20s） | **无**（没有可比对的反馈） |
| 中间态 | 「动作中」 | 无（切换是即时的） |

把它塞进 ValveBank 会让那套状态机多一条"永远不到位"的特例，
两边都变难读。故独立成类，共用同一个数字量模块。
"""

import time

from . import proto


class PumpItem(object):
    """单台进水泵的点位与运行状态。"""

    def __init__(self, index, name="", module=None, do=None):
        self.index = index
        self.name = name or ("进水泵 %d" % (index + 1))
        #: 点位（None = 未装配）
        self.module = module
        self.do = do
        #: 已知输出状态（True = 接触器吸合 = 泵运行）。
        self.on = False
        #: 阀/泵认为的 DO 实际位置（用于检测"被外部改动"）。
        self.known_do = False
        #: 是否已用真实 DO 校准过（首拍校准标志）。
        self.known_do_seeded = False
        self.valid = False
        self.text = ""
        self.last_error = ""
        self.updated_ms = 0

    @property
    def ready(self):
        """点位是否配齐（module + do 都有）。"""
        return self.module is not None and self.do is not None

    def apply_config(self, item_cfg):
        self.name = item_cfg.get("name") or ("进水泵 %d" % (self.index + 1))
        self.module = item_cfg.get("module")
        self.do = item_cfg.get("do")


class PumpBank(object):
    """进水泵组（当前 1 台；结构上留多台的位置）。

    [为什么参数叫 bank 而不是 service] 与 ValveBank 并列，调用方（app）
    一眼能看出这是"同一类外设的集合"，而不是某种服务。
    """

    def __init__(self, digital_devices, items_cfg=None, log=None):
        self.digital = list(digital_devices or [])
        self._log = log or (lambda msg: None)
        self.items = [PumpItem(i) for i in range(proto.HSF_PUMP_COUNT)]
        if items_cfg:
            for i, item_cfg in enumerate(items_cfg[:proto.HSF_PUMP_COUNT]):
                if isinstance(item_cfg, dict):
                    self.items[i].apply_config(item_cfg)

    # ---- 点位 ----
    def _module_of(self, item):
        if item.module is None:
            return None
        if not 0 <= item.module < len(self.digital):
            return None
        return self.digital[item.module]

    def ready_count(self):
        return sum(1 for item in self.items if item.ready)

    # ---- 采集（只读 DO 回显，不判到位）----
    def poll(self, now_ms, stale_ms=5000):
        """刷新每台泵的"输出回显"与可用性。

        [为什么仍然要 poll] 虽然没有反馈，但 DO 的**回显**仍是真的：
        能确认"写下去的值还在模块里"（现场有人手动改过、模块重启丢配置、
        别的客户端从数字量页改了这个口）。发现不一致就同步过来 —— 界面
        显示的状态必须与实际输出一致，不能停在"我们以为的值"。
        """
        for item in self.items:
            module = self._module_of(item)
            if module is None or not item.ready:
                item.valid = False
                item.text = "未配置点位"
                item.updated_ms = now_ms
                continue
            if not module.online or (now_ms - module.last_ok_ms) > stale_ms:
                item.valid = False
                item.last_error = "数字量模块数据不新鲜"
                item.text = item.last_error
                item.updated_ms = now_ms
                continue
            try:
                actual = module.do_bit(int(item.do))
            except Exception as exc:                    # noqa: BLE001
                item.valid = False
                item.last_error = str(exc)
                item.text = item.last_error
                item.updated_ms = now_ms
                continue
            # 首拍校准：模块还没采过一帧时 do 缓存不可信，不校准也不判改动
            if not item.known_do_seeded:
                if not getattr(module, "last_ok_ms", 0):
                    item.valid = True
                    item.updated_ms = now_ms
                    continue
                item.known_do_seeded = True
                item.known_do = bool(actual)
                item.on = bool(actual)
                item.valid = True
                item.text = ""
                item.updated_ms = now_ms
                continue
            # ---- 外部改动：以模块回显为准 ----
            #
            # [为什么要覆盖"我们的值"] 泵没有反馈，DO 回显是**唯一**能发现
            # "这个口被别人改过"的手段（数字量页、现场 HMI、自动逻辑都能
            # 绕过本类）。发现不一致就以实际输出为准并记日志 —— 界面显示
            # 一个与实际相反的状态是最危险的。
            if bool(actual) != bool(item.known_do):
                prev = bool(item.known_do)
                item.known_do = bool(actual)
                item.on = bool(actual)
                self._log("进水泵 %s（%s）检测到 DO 被外部改动 %s→%s（以实际输出为准）"
                          % (item.name, item.index,
                             "开" if prev else "关",
                             "开" if actual else "关"))
            item.valid = True
            item.last_error = ""
            item.text = ""
            item.updated_ms = now_ms

    # ---- 控制 ----
    async def set_pump(self, index, on):
        """下发开关命令。

        :returns: (ok: bool, err: str)

        [幂等] 已经是这个状态就不重复写（避免无谓的继电器动作与总线占用）。
        这与阀的 `ignored` 同一口径：命令值 == 当前回显值 → 什么都不做。
        """
        if not 0 <= index < len(self.items):
            return False, "泵号越界（0~%d）" % (len(self.items) - 1)
        item = self.items[index]
        if not item.ready:
            return False, "%s 未配置点位" % item.name
        module = self._module_of(item)
        if module is None:
            return False, "%s 对应的数字量模块未装配" % item.name
        if not module.online:
            return False, "%s 对应的数字量模块离线" % item.name
        # 幂等：已在这个状态 → 不写
        if item.valid and bool(item.on) == bool(on):
            self._log("进水泵 %s（%s）已处于%s状态，忽略重复命令"
                      % (item.name, item.index, "运行" if on else "停止"))
            return True, ""
        try:
            ok, err = await module.write_do(int(item.do), bool(on))
        except Exception as exc:                        # noqa: BLE001
            return False, "写 DO 失败：%s" % exc
        if not ok:
            return False, err or "写 DO 失败"
        item.on = bool(on)
        # 自己写的要同步 known_do，否则下一拍 poll 会把它当成"外部改动"
        item.known_do = bool(on)
        item.known_do_seeded = True
        item.valid = True
        self._log("进水泵 %s（%s）→ 命令%s（DO%d）"
                  % (item.name, item.index,
                     "启动" if on else "停止", int(item.do)))
        return True, ""

    # ---- 健康 ----
    def health_summary(self, now_ms, stale_ms=5000):
        """:returns: (health, online, text)"""
        if not self.digital:
            return proto.HEALTH_OFFLINE, False, "数字量模块未装配"
        ready = self.ready_count()
        if ready == 0:
            return proto.HEALTH_OFFLINE, False, "未配置点位"
        invalid = [i for i in self.items if i.ready and not i.valid]
        if invalid:
            return proto.HEALTH_STALE, True, "%d 台无有效回显" % len(invalid)
        return proto.HEALTH_OK, True, ""
