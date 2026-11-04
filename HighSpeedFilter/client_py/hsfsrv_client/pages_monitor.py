# -*- coding: utf-8 -*-
"""监视类页面：数字量 / 模拟量 / 变频器 / 电磁流量计。

对齐 C 端：
    pages/HsfMonitorDigital.c     → DigitalPage
    pages/HsfMonitorAnalog.c      → AnalogPage
    pages/HsfMonitorVfd.c         → VfdPage
    pages/HsfMonitorFlowMeter.c   → FlowMeterPage
"""

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (QComboBox, QGridLayout, QHBoxLayout, QLabel,
                             QPushButton, QSpinBox, QVBoxLayout, QWidget)

from . import proto, theme
from .page_base import DataTable, LampRow, Page, ToggleGrid, section_title
from .widgets import Card


# ============================================================================
#                          监视 - 数字量
# ============================================================================

class DigitalPage(Page):
    """16DI + 16DO 监视/操作页。

    上下两区：DI 只读灯阵列、DO 可操作按钮阵。多台模块用顶部下拉切换。
    """

    TOPICS = (proto.TOPIC_DIGITAL_STATUS,)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("数字量", "16 路输入 / 16 路输出")
        self._current_dev = 0

        # ---- 模块选择 ----
        top = QHBoxLayout()
        top.setSpacing(8)
        top.addWidget(QLabel("模块"))
        self.dev_combo = QComboBox()
        self.dev_combo.setMinimumWidth(160)
        self.dev_combo.currentIndexChanged.connect(self._on_dev_changed)
        top.addWidget(self.dev_combo)
        self.state_label = QLabel("--")
        self.state_label.setStyleSheet(theme.caption_label_qss())
        top.addWidget(self.state_label)
        top.addStretch(1)
        self.refresh_btn = QPushButton("请求快照")
        self.refresh_btn.clicked.connect(self._request_snapshot)
        top.addWidget(self.refresh_btn)
        self.root.addLayout(top)

        # ---- DI 区 ----
        di_card = Card("输入 DI（只读）")
        self.di_lamp = LampRow(16, 8)
        di_card.add(self.di_lamp)
        self.root.addWidget(di_card)

        # ---- DO 区 ----
        do_card = Card("输出 DO（点击切换）")
        self.do_grid = ToggleGrid(16, 8)
        self.do_grid.on_toggle = self._on_do_toggle
        do_card.add(self.do_grid)
        self.do_hint = QLabel("")
        self.do_hint.setStyleSheet(theme.caption_label_qss())
        do_card.add(self.do_hint)
        self.root.addWidget(do_card)
        self.root.addStretch(1)

        self._sync_device_list()

    # ---- 设备列表 ----
    def _sync_device_list(self):
        """按 model 里已出现的模块重建下拉（服务端推几台就几台）。"""
        count = max(self.ctx.model.digital_count(), 0)
        want = max(count, 1)
        if self.dev_combo.count() == want:
            # 名字可能后到，更新文本但保持选中项。
            for i in range(want):
                st = self.ctx.model.digital.get(i)
                name = (st.name if st and st.name else "数字量%d" % (i + 1))
                self.dev_combo.setItemText(i, "%d · %s" % (i + 1, name))
            return
        self.dev_combo.blockSignals(True)
        self.dev_combo.clear()
        for i in range(want):
            st = self.ctx.model.digital.get(i)
            name = (st.name if st and st.name else "数字量%d" % (i + 1))
            self.dev_combo.addItem("%d · %s" % (i + 1, name))
        self.dev_combo.blockSignals(False)
        self.dev_combo.setCurrentIndex(min(self._current_dev, want - 1))

    def _on_dev_changed(self, index):
        self._current_dev = max(0, index)
        self.refresh()

    def _request_snapshot(self):
        from . import bson
        def _done(code, dtype, data):
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            if doc and "name" in doc:
                self.ctx.model.digital_state(
                    int(doc.get("dev", 0) or 0)).apply(doc)
        self.ctx.bridge.request(proto.CMD_DIGITAL_SNAPSHOT,
                                {"dev": self._current_dev}, on_done=_done)

    # ---- DO 操作 ----
    def _on_do_toggle(self, index, want_on):
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止操作：%s" % reason)
            # 回滚按钮显示态（不允许的状态不能留在界面上骗人）。
            st = self.ctx.model.digital_state(self._current_dev)
            self.do_grid.set_state(index, st.do_bit(index))
            return
        self.ctx.set_status("正在写 DO%d = %s…" % (index, "通" if want_on else "断"))

        def _done(code, dtype, data):
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                dev = self.ctx.model.digital_state(int(doc.get("dev", 0) or 0))
                dev.do = int(doc.get("do", 0) or 0)
                self.ctx.bridge.dataUpdated.emit("digital")
            else:
                self.ctx.set_status("写 DO%d 失败：%s"
                                    % (index, doc.get("err", "未知原因")))

        def _err(msg):
            self.ctx.set_status("写 DO%d 失败：%s" % (index, msg))

        self.ctx.bridge.send_digital_control(self._current_dev,
                                             on_done=_done, on_error=_err,
                                             channel=index, on=want_on)

    # ---- 刷新 ----
    def refresh(self):
        self._sync_device_list()
        st = self.ctx.model.digital.get(self._current_dev)
        if st is None or not st.is_fresh():
            for i in range(16):
                self.di_lamp.set_state(i, False)
            self.state_label.setText("无数据（%s）"
                                     % ("离线" if st is None or not st.valid
                                        else "数据过期"))
            for i in range(16):
                self.do_grid.set_state(i, False)
            self.do_grid.set_enabled_all(False)
            self.do_hint.setText("设备数据不可用，DO 操作已禁用")
            return
        for i in range(16):
            self.di_lamp.set_state(i, st.di_bit(i))
            self.do_grid.set_state(i, st.do_bit(i))
        self.state_label.setText("DI 0x%04X · DO 0x%04X" % (st.di, st.do))
        reason = self.ctx.control_blocked_reason()
        self.do_grid.set_enabled_all(not reason)
        self.do_hint.setText("" if not reason else "操作已禁用：%s" % reason)


# ============================================================================
#                          监视 - 模拟量
# ============================================================================

class AnalogPage(Page):
    """8 路 4~20mA 模拟量监视。"""

    TOPICS = (proto.TOPIC_AI_STATUS,)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("模拟量", "8 路 4~20 mA 输入")
        self._table = DataTable()
        self._table.set_rows([("通道 %d" % (i + 1), "--", "mA")
                              for i in range(8)])
        card = Card("通道读数")
        card.add(self._table)
        # 表格高度贴合 8 行内容，别撑出大片空白
        self._table.fit_to_content()
        self.root.addWidget(card)
        self.root.addStretch(1)

        self._raw_caption = QLabel("")
        self._raw_caption.setStyleSheet(theme.caption_label_qss())
        self.root.addWidget(self._raw_caption)

    def refresh(self):
        st = self.ctx.model.analog
        if not st.is_fresh():
            self._table.set_rows([("通道 %d" % (i + 1), "--", "mA")
                                  for i in range(st.channels)])
            self._raw_caption.setText("无数据（未订阅或设备离线）")
            return
        rows = []
        for i in range(st.channels):
            if st.channel_valid(i):
                rows.append(("通道 %d" % (i + 1), "%.3f" % st.ma(i), "mA"))
            else:
                rows.append(("通道 %d" % (i + 1), "--", "mA"))
        self._table.set_rows(rows)
        raws = " ".join("CH%d=%dmV" % (i + 1, st.raw[i])
                        for i in range(st.channels))
        self._raw_caption.setText("原始寄存器：" + raws)


# ============================================================================
#                          监视 - 变频器
# ============================================================================

class VfdPage(Page):
    """变频器监视 + 控制（启停/频率/点动）。

    [点动手持] 按住按钮期间每 200ms 重发一次点动帧；松手发 JogRelease。
    服务端 600ms 收不到就自动停机——这是安全设计，不是 bug。
    """

    TOPICS = (proto.TOPIC_VFD_STATUS,)

    #: 点动重发间隔（与 C 端 HSF_JOG_REPEAT_MS 一致）。
    JOG_REPEAT_MS = 200

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("变频器", "M512/M513 · Modbus-RTU")
        self._jog_timer = QTimer(self)
        self._jog_timer.setInterval(self.JOG_REPEAT_MS)
        self._jog_timer.timeout.connect(self._jog_repeat)
        self._jog_active = False
        self._jog_reverse = False

        # ---- 核心读数 ----
        read_card = Card("运行状态")
        grid = QGridLayout()
        grid.setSpacing(12)
        self._stats = {}
        specs = [
            ("run",     "运行状态", "",      18),
            ("out",     "输出频率", "Hz",    22),
            ("set",     "设定频率", "Hz",    16),
            ("bus",     "母线电压", "V",     16),
            ("volt",    "输出电压", "V",     16),
            ("cur",     "输出电流", "A",     16),
            ("power",   "输出功率", "kW",    16),
            ("torque",  "输出转矩", "%",     16),
            ("fault",   "故障",     "",      14),
        ]
        for i, (key, caption, unit, size) in enumerate(specs):
            from .widgets import StatBlock
            block = StatBlock(caption, unit, "--", value_size=size)
            grid.addWidget(block, i // 3, i % 3)
            self._stats[key] = block
        read_card.body.addLayout(grid)
        self.root.addWidget(read_card)

        # ---- 控制区 ----
        ctrl_card = Card("控制")
        self._ctrl_hint = QLabel("")
        self._ctrl_hint.setStyleSheet(theme.caption_label_qss())
        ctrl_card.add(self._ctrl_hint)

        row1 = QHBoxLayout()
        row1.setSpacing(8)
        self.btn_fwd = QPushButton("正转启动")
        self.btn_fwd.clicked.connect(lambda: self._send_op(proto.OP_START,
                                                           rev=False))
        self.btn_rev = QPushButton("反转启动")
        self.btn_rev.clicked.connect(lambda: self._send_op(proto.OP_START,
                                                           rev=True))
        self.btn_stop = QPushButton("停机")
        self.btn_stop.clicked.connect(lambda: self._send_op(proto.OP_STOP))
        self.btn_free = QPushButton("自由停车")
        self.btn_free.clicked.connect(lambda: self._send_op(proto.OP_FREE_STOP))
        self.btn_reset = QPushButton("故障复位")
        self.btn_reset.clicked.connect(lambda: self._send_op(proto.OP_RESET_FAULT))
        for btn in (self.btn_fwd, self.btn_rev, self.btn_stop, self.btn_free,
                    self.btn_reset):
            row1.addWidget(btn)
        row1.addStretch(1)
        ctrl_card.body.addLayout(row1)

        # ---- 频率给定 ----
        row2 = QHBoxLayout()
        row2.setSpacing(8)
        row2.addWidget(QLabel("频率给定"))
        self.freq_spin = QSpinBox()
        self.freq_spin.setRange(0, 500)
        self.freq_spin.setSuffix(" Hz")
        self.freq_spin.setValue(15)
        self.freq_spin.setMinimumWidth(110)
        row2.addWidget(self.freq_spin)
        self.btn_set_freq = QPushButton("下发")
        self.btn_set_freq.clicked.connect(self._on_set_freq)
        row2.addWidget(self.btn_set_freq)
        row2.addSpacing(20)

        # ---- 点动（按压式：按下发点动、松手发释放） ----
        row2.addWidget(QLabel("点动"))
        self.btn_jog_fwd = QPushButton("正转点动（按住）")
        self.btn_jog_fwd.pressed.connect(lambda: self._jog_start(False))
        self.btn_jog_fwd.released.connect(self._jog_stop)
        row2.addWidget(self.btn_jog_fwd)
        self.btn_jog_rev = QPushButton("反转点动（按住）")
        self.btn_jog_rev.pressed.connect(lambda: self._jog_start(True))
        self.btn_jog_rev.released.connect(self._jog_stop)
        row2.addWidget(self.btn_jog_rev)
        row2.addStretch(1)
        ctrl_card.body.addLayout(row2)
        self.root.addWidget(ctrl_card)
        self.root.addStretch(1)

        self._control_buttons = [self.btn_fwd, self.btn_rev, self.btn_stop,
                                 self.btn_free, self.btn_reset,
                                 self.btn_set_freq, self.btn_jog_fwd,
                                 self.btn_jog_rev]
        #: 设备是否就绪（服务端回过 ok=true 的快照 / 收到过遥测）。
        self._device_ready = None      # None = 还没探测过
        self._not_ready_reason = ""

    def on_enter(self):
        """入页时拉一次快照：顺便探测「设备是否装配」。

        [为什么用快照探测而不是看健康] 健康条目里 kind=0 的那台可能是
        别的侧；快照请求的应答直接用「ok=false + 原因」告诉我们这台能不能用，
        比解析健康表更直接。
        """
        def _done(code, dtype, data):
            from . import bson as _bson
            try:
                doc = _bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            if doc.get("ok") is False:
                self._device_ready = False
                self._not_ready_reason = doc.get("err", "设备未就绪")
            else:
                self._device_ready = True
                self._not_ready_reason = ""

        def _err(msg):
            self._device_ready = False
            self._not_ready_reason = msg
        self.ctx.bridge.request(proto.CMD_VFD_SNAPSHOT, None, on_done=_done,
                                on_error=_err)

    # ---- 控制 ----
    def _send_op(self, op, rev=False, hz=None):
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止操作：%s" % reason)
            return
        doc = {"op": int(op)}
        if op == proto.OP_START:
            doc["rev"] = bool(rev)
        if hz is not None:
            doc["hz"] = float(hz)
        self.ctx.set_status("下发命令 op=%d…" % op)
        self.ctx.bridge.send_vfd_control(doc, on_done=self._on_ctrl_done,
                                         on_error=self._on_ctrl_err)

    def _on_ctrl_done(self, code, dtype, data):
        from . import bson
        try:
            doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
        except ValueError:
            doc = {}
        if doc.get("ok"):
            self.ctx.set_status("命令已受理")
        else:
            self.ctx.set_status("命令被拒：%s" % doc.get("err", "未知原因"))

    def _on_ctrl_err(self, msg):
        self.ctx.set_status("命令失败：%s" % msg)

    def _on_set_freq(self):
        self._send_op(proto.OP_SET_FREQUENCY, hz=self.freq_spin.value())

    # ---- 点动手持 ----
    def _jog_start(self, reverse):
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止点动：%s" % reason)
            return
        self._jog_active = True
        self._jog_reverse = reverse
        self._jog_repeat()
        self._jog_timer.start()
        self.ctx.set_status("点动中（%s）——松手即停"
                            % ("反转" if reverse else "正转"))

    def _jog_repeat(self):
        """按住期间重发点动帧（服务端靠它刷新 600ms 看门狗）。"""
        if not self._jog_active:
            return
        self.ctx.bridge.send_vfd_control(
            {"op": proto.OP_JOG, "rev": self._jog_reverse})

    def _jog_stop(self):
        if not self._jog_active:
            return
        self._jog_active = False
        self._jog_timer.stop()
        # 松手立刻发释放（不等看门狗），这是安全动作。
        self.ctx.bridge.send_vfd_control({"op": proto.OP_JOG_RELEASE})
        self.ctx.set_status("点动释放")

    # ---- 刷新 ----
    def refresh(self):
        st = self.ctx.model.vfd
        fresh = st.is_fresh()

        def show(key, text, color=None):
            self._stats[key].set_value(text, color)

        if not fresh:
            for key in self._stats:
                show(key, "--")
            self._stats["run"].set_value("无数据", theme.CLR_OFFLINE)
        else:
            color = (theme.CLR_FAULT if st.faulted
                     else (theme.CLR_OK if st.running else "#1A1A1A"))
            show("run", st.run_state_text(), color)
            # 未提供的字段显示 "--"（掩码里没有 = 品牌没给这个量）。
            show("out", "%.2f" % st.output_freq_hz
                 if st.has("outputFreqHz") else "--")
            show("set", "%.2f" % st.set_freq_hz
                 if st.has("setFreqHz") else "--")
            show("bus", "%.1f" % st.bus_voltage_v
                 if st.has("busVoltageV") else "--")
            show("volt", "%.0f" % st.output_voltage_v
                 if st.has("outputVoltageV") else "--")
            show("cur", "%.2f" % st.output_current_a
                 if st.has("outputCurrentA") else "--")
            show("power", "%.2f" % st.output_power_kw
                 if st.has("outputPowerKw") else "--")
            show("torque", "%.1f" % st.torque_percent
                 if st.has("torquePercent") else "--")
            show("fault", st.fault_text(),
                 theme.CLR_FAULT if st.faulted else theme.CLR_OK)

        reason = self.ctx.control_blocked_reason()
        if not reason and self._device_ready is False:
            reason = getattr(self, "_not_ready_reason", "") or "设备未就绪"
        for btn in self._control_buttons:
            btn.setEnabled(not reason)
        self._ctrl_hint.setText("" if not reason
                                else "控制已禁用：%s" % reason)


# ============================================================================
#                       监视 - 电磁流量计
# ============================================================================

class FlowMeterPage(Page):
    """电磁流量计监视（当前服务端未装配，页面保留结构）。"""

    TOPICS = (proto.TOPIC_FLOWMETER_STATUS,)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("电磁流量计", "LD 系列 · Modbus-RTU")
        card = Card("测量值")
        grid = QGridLayout()
        grid.setSpacing(12)
        from .widgets import StatBlock
        self._stats = {}
        for i, (key, caption, unit, size) in enumerate([
                ("flow", "瞬时流量", "m³/h", 24),
                ("speed", "瞬时流速", "m/s", 18),
                ("fwd", "正向累积", "m³", 18),
                ("rev", "反向累积", "m³", 18),
                ("alarm", "报警", "", 16)]):
            block = StatBlock(caption, unit, "--", value_size=size)
            grid.addWidget(block, i // 3, i % 3)
            self._stats[key] = block
        card.body.addLayout(grid)
        self.root.addWidget(card)

        self._note = QLabel("提示：当前服务端未装配流量计（路 B 关闭）。"
                            "接入后本页自动显示数据。")
        self._note.setWordWrap(True)
        self._note.setStyleSheet(theme.caption_label_qss())
        self.root.addWidget(self._note)
        self.root.addStretch(1)

    def refresh(self):
        st = self.ctx.model.flowmeter
        if not st.is_fresh():
            for key in self._stats:
                self._stats[key].set_value("--")
            return
        self._stats["flow"].set_value("%.4f" % st.flow)
        self._stats["flow"].set_suffix(st.flow_unit_text())
        self._stats["speed"].set_value("%.4f" % st.speed)
        self._stats["fwd"].set_value("%.2f" % st.forward_total)
        self._stats["fwd"].set_suffix(st.total_unit_text())
        self._stats["rev"].set_value("%.2f" % st.reverse_total)
        self._stats["alarm"].set_value(
            "正常" if st.alarms == 0 else "0x%04X" % st.alarms,
            theme.CLR_OK if st.alarms == 0 else theme.CLR_FAULT)
