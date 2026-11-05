# -*- coding: utf-8 -*-
"""监视类页面：数字量 / 模拟量 / 变频器 / 水质监测。

对齐 C 端：
    pages/HsfMonitorDigital.c     → DigitalPage
    pages/HsfMonitorAnalog.c      → AnalogPage
    pages/HsfMonitorVfd.c         → VfdPage
    pages/HsfMonitorFlowMeter.c   → WaterPage（在流量计基础上扩为
                                    流量 + 浊度×2 + 进水泵；C 端无此页）
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
    """数字量监视/操作页（点数由服务端下发）。

    上下两区：DI 只读灯阵列、DO 可操作按钮阵。多台模块用顶部下拉切换。

    [为什么点数不能在客户端写死] 单模块物理 16 点，但模块支持主从聚合：
    本现场 .13 的 16 点已聚合进 .12，对外就是 32 点。客户端写死 16 会让
    上半区（16~31）永远看不到，而服务端明明在发 —— 表现为"界面只有 16 个
    灯，但服务端日志说 32"。故灯阵/按钮阵按快照里的 count 键**按需重建**。

    [初值 16 只是占位] 首帧推送到达前还不知道真实点数，先摆 16 个格子；
    收到 count=32 的帧会自动重建成 32 个。
    """

    TOPICS = (proto.TOPIC_DIGITAL_STATUS,)

    #: 首帧到达前的占位格数（真实点数见快照 count）。
    PLACEHOLDER_COUNT = 16

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("数字量", "点数由服务端下发")
        self._current_dev = 0
        #: 当前已按多少点布好阵（避免每拍重建）。
        self._laid_out = 0
        #: 两个卡片（点数变化时要同步调它们的最小高度）。
        self._di_card = None
        self._do_card = None

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
        self.di_lamp = LampRow(self.PLACEHOLDER_COUNT, 8)
        di_card.add(self.di_lamp)
        self.root.addWidget(di_card)

        # ---- DO 区 ----
        do_card = Card("输出 DO（点击切换）")
        self.do_grid = ToggleGrid(self.PLACEHOLDER_COUNT, 8)
        self.do_grid.on_toggle = self._on_do_toggle
        do_card.add(self.do_grid)
        self.do_hint = QLabel("")
        self.do_hint.setStyleSheet(theme.caption_label_qss())
        do_card.add(self.do_hint)
        # [给卡片保底高度] 窗口高度不够时布局会优先压缩控件；DO 按钮是
        # 固定行高（有语义名时两行文字），被压就会出现"部分 46px 部分
        # 33px"的参差。卡片设最小高度后由外层滚动条负责，不被压扁。
        # 用占位点数估高：真实点数到达后 _layout_for() 会再按实际点数调。
        min_h = self._card_min_height(self.PLACEHOLDER_COUNT)
        di_card.setMinimumHeight(min_h)
        do_card.setMinimumHeight(min_h)
        self._di_card = di_card
        self._do_card = do_card
        self.root.addWidget(do_card)
        self.root.addStretch(1)

        self._sync_device_list()

    def _card_min_height(self, count):
        """按点数估算卡片最小高度（容纳 DI 两行标签 / DO 两行按钮）。"""
        rows = (int(count) + 7) // 8
        return 56 + rows * (ToggleGrid.ROW_HEIGHT + 8) + 24

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
            """快照应答（**网络线程**）：只写 model（纯数据），不碰控件。"""
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
            """DO 写应答（**网络线程**）：改动一律投递到 UI 线程。"""
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            ok = bool(doc.get("ok"))
            dev_i = int(doc.get("dev", 0) or 0)
            do_mask = int(doc.get("do", 0) or 0)
            err = doc.get("err", "未知原因")

            def _apply():
                if ok:
                    dev = self.ctx.model.digital_state(dev_i)
                    dev.do = do_mask
                    self.ctx.bridge.dataUpdated.emit("digital")
                else:
                    self.ctx.set_status("写 DO%d 失败：%s" % (index, err))
            self.ctx.bridge.run_in_ui(_apply)

        def _err(msg):
            self.ctx.bridge.run_in_ui(
                lambda: self.ctx.set_status("写 DO%d 失败：%s" % (index, msg)))

        self.ctx.bridge.send_digital_control(self._current_dev,
                                             on_done=_done, on_error=_err,
                                             channel=index, on=want_on)

    # ---- 刷新 ----
    def _layout_for(self, count):
        """按点数布阵（点数未变则跳过，避免每拍重建控件）。"""
        count = max(0, int(count))
        if count == self._laid_out:
            return
        self.di_lamp.set_count(count)
        self.do_grid.set_count(count)
        # 卡片最小高度跟着点数走：32 点是 4 行，16 点是 2 行。
        min_h = self._card_min_height(count)
        if self._di_card is not None:
            self._di_card.setMinimumHeight(min_h)
        if self._do_card is not None:
            self._do_card.setMinimumHeight(min_h)
        self._laid_out = count

    def refresh(self):
        self._sync_device_list()
        st = self.ctx.model.digital.get(self._current_dev)
        # [先布阵再取值] 点数来自服务端快照的 count；首帧未到时 st 可能是
        # None，此时保持占位格数，不把界面清成 0 格。
        total = int(getattr(st, "count", 0) or 0) if st is not None else 0
        if total <= 0:
            total = self.PLACEHOLDER_COUNT
        self._layout_for(total)

        if st is None or not st.is_fresh():
            for i in range(total):
                self.di_lamp.set_state(i, False)
                self.do_grid.set_state(i, False)
            self.state_label.setText("无数据（%s）"
                                     % ("离线" if st is None or not st.valid
                                        else "数据过期"))
            self.do_grid.set_enabled_all(False)
            self.do_hint.setText("设备数据不可用，DO 操作已禁用")
            return
        for i in range(total):
            self.di_lamp.set_state(i, st.di_bit(i))
            self.do_grid.set_state(i, st.do_bit(i))
            # 语义名（服务端下发；没有被占用的口给空串 → 只显示 CH/DO 号）。
            self.di_lamp.set_name(i, st.di_name(i))
            self.do_grid.set_name(i, st.do_name(i))
        # [位宽跟着点数走] 32 点要用 8 位十六进制，写成 %04X 会把上半区
        # 截掉，让人以为 bit16+ 恒为 0。
        digits = 8 if total > 16 else 4
        self.state_label.setText("DI 0x%0*X · DO 0x%0*X"
                                 % (digits, st.di, digits, st.do))
        reason = self.ctx.control_blocked_reason()
        self.do_grid.set_enabled_all(not reason)
        self.do_hint.setText("" if not reason else "操作已禁用：%s" % reason)


# ============================================================================
#                          监视 - 模拟量
# ============================================================================

class AnalogPage(Page):
    """8 路 4~20mA 模拟量监视。

    [四列：通道 / 名称 / 数值 / 单位]
    通道号与名字**分成两列**而不是拼在一起：通道号是接线/排查口径（现场说
    "第 1 个通道"），名字是工艺口径（"压差计"）。分开后既能按通道号扫视
    全部 8 路，也能一眼看出哪一路是压差计。没有名字的通道留空。

    [换算通道] 服务端配了工程量的通道（现场是通道0 接压差计）在该行内
    同时显示**原始 mA** 与**换算后的物理量**：`11.988 mA  0.499 MPa`。
    两个都留是刻意的 —— 原始值是唯一无歧义的真相（排查时按它判断断线/
    欠量程），换算值是给人看的工艺量。量程由服务端配置，客户端不参与推导。
    """

    TOPICS = (proto.TOPIC_AI_STATUS,)

    #: 表头（通道号 / 名称 / 数值 / 单位）。
    HEADERS = ("通道", "名称", "数值", "单位")

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("模拟量", "8 路 4~20 mA 输入")
        self._table = DataTable()
        self._table.set_headers(self.HEADERS)
        # 通道/名称/单位窄、数值最宽（要容纳"11.988 mA  0.499 MPa"两段）。
        self._table.set_column_weights((3, 3, 9, 2))
        self._table.set_rows([("通道 %d" % (i + 1), "", "--", "mA")
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
            self._table.set_rows([("通道 %d" % (i + 1), "", "--", "mA")
                                  for i in range(st.channels)])
            self._raw_caption.setText("无数据（未订阅或设备离线）")
            return
        rows = []
        for i in range(st.channels):
            label = "通道 %d" % (i + 1)
            name = st.eng_name.get(i, "") if st.has_engineering(i) else ""
            if not st.channel_valid(i):
                rows.append((label, name, "--", "mA"))
                continue
            if st.has_engineering(i):
                # 换算行：数值列放两段纯数字，单位列放对应的两个单位。
                # [为什么不把单位写进数值文字] 单位列会空着，看起来像漏填；
                # 而数值里再带单位又和单位列重复。分开后一行读起来是
                # "11.988  0.499 | mA / MPa"，两段数字与两个单位一一对应。
                unit = st.eng_unit.get(i, "")
                value = "%.3f    %g" % (st.ma(i), st.eng.get(i, 0) / 1000.0)
                note = st.eng_note.get(i, "")
                if note:
                    value += "  " + note
                rows.append((label, name, value,
                             "mA / %s" % unit if unit else "mA"))
            else:
                rows.append((label, name, "%.3f" % st.ma(i), "mA"))
        self._table.set_rows(rows)
        raws = " ".join("CH%d=%dmV" % (i + 1, st.raw[i])
                        for i in range(st.channels))
        self._raw_caption.setText("原始寄存器：" + raws)


# ============================================================================
#                          监视 - 变频器
# ============================================================================

class VfdPage(Page):
    """变频器监视 + 控制（启停 / 调速 / 点动）。

    [点动手持] 按住按钮期间每 200ms 重发一次点动帧；松手发 JogRelease。
    服务端 600ms 收不到就自动停机——这是安全设计，不是 bug。

    [转速是换算值，不是实测] 变频器本身没有转速寄存器，转速由**输出频率
    按铭牌额定点线性换算**（现场口径：50Hz ≈ 1440rpm）。滑差随负载变化，
    故界面标注"换算"。额定点由服务端配置下发，客户端不写死。

    [调速方式可切换] 频率调速 / 转速调速用下拉框切换：
      · 频率模式：输入 Hz，下发 OP_SET_FREQUENCY
      · 转速模式：输入 rpm，下发 OP_SET_SPEED（服务端按额定点反解成 Hz）
    两种模式的输入框共用一处布局，切换时只改量程/后缀/步进与提示文字。

    [正反转是下拉框，启动/停止是按钮] 现场操作习惯：先选方向再按启动。
    原来两个按钮（正转启动/反转启动）容易误点，且方向与启动两件事挤在
    一个动作里；拆开后"方向"是选择项、"启停"才是动作。
    """

    TOPICS = (proto.TOPIC_VFD_STATUS,)

    #: 点动重发间隔（与 C 端 HSF_JOG_REPEAT_MS 一致）。
    JOG_REPEAT_MS = 200

    #: 调速方式
    SPEED_MODE_HZ = 0
    SPEED_MODE_RPM = 1

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        # [页名＝工艺用途] 设备是变频器，但它驱动的是搅拌泵。操作员找的是
        # "搅拌泵那一页"，所以标题按用途叫；副标题保留型号，维修时有用。
        self.add_header("搅拌泵", "M512/M513 变频器 · Modbus-RTU")
        self._jog_timer = QTimer(self)
        self._jog_timer.setInterval(self.JOG_REPEAT_MS)
        self._jog_timer.timeout.connect(self._jog_repeat)
        self._jog_active = False
        self._jog_reverse = False
        #: 额定点（服务端下发；未收到时用现场缺省 1440rpm/50Hz）。
        self._rated_rpm = 1440.0
        self._rated_hz = 50.0

        # ---- 核心读数 ----
        read_card = Card("运行状态")
        grid = QGridLayout()
        grid.setSpacing(12)
        self._stats = {}
        specs = [
            ("run",     "运行状态", "",      18),
            ("out",     "输出频率", "Hz",    22),
            ("rpm",     "输出转速", "rpm",   22),
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
        self._rpm_hint = QLabel("输出转速为频率换算值（变频器无转速传感器）")
        self._rpm_hint.setStyleSheet(theme.caption_label_qss())
        read_card.body.addWidget(self._rpm_hint)
        self.root.addWidget(read_card)

        # ---- 控制区 ----
        ctrl_card = Card("控制")
        self._ctrl_hint = QLabel("")
        self._ctrl_hint.setStyleSheet(theme.caption_label_qss())
        ctrl_card.add(self._ctrl_hint)

        # ---- 方向 + 启停 ----
        row1 = QHBoxLayout()
        row1.setSpacing(8)
        row1.addWidget(QLabel("方向"))
        self.dir_combo = QComboBox()
        self.dir_combo.addItem("正转", False)      # userData = reverse?
        self.dir_combo.addItem("反转", True)
        self.dir_combo.setMinimumWidth(90)
        row1.addWidget(self.dir_combo)
        row1.addSpacing(12)

        # ---- 启停：**一个按钮**，按运行状态切换"启动/停止" ----
        # [为什么合成一个] 现场是"开关"心智：机器在转就想按停、停着就想按起。
        # 两个按钮并排会让人看错位置而误操作。文字与动作由 refresh() 按
        # st.running 同步（见 _sync_run_button）。
        self.btn_run = QPushButton("启动")
        self.btn_run.setMinimumWidth(96)
        self.btn_run.clicked.connect(self._on_run_stop)
        row1.addWidget(self.btn_run)

        self.btn_free = QPushButton("自由停车")
        self.btn_free.clicked.connect(
            lambda: self._send_op(proto.OP_FREE_STOP))
        self.btn_reset = QPushButton("故障复位")
        self.btn_reset.clicked.connect(
            lambda: self._send_op(proto.OP_RESET_FAULT))
        row1.addWidget(self.btn_free)
        row1.addWidget(self.btn_reset)
        row1.addSpacing(20)

        # ---- 点动：**独立一个按钮**（方向跟随上面的下拉框） ----
        # 与启停分开：点动是"按住才动、松手即停"的安全动作，语义与启停
        # 完全不同，混在一个按钮里会让操作员按错。
        row1.addWidget(QLabel("点动"))
        self.btn_jog = QPushButton("按住点动")
        self.btn_jog.setMinimumWidth(110)
        self.btn_jog.pressed.connect(self._jog_start)
        self.btn_jog.released.connect(self._jog_stop)
        row1.addWidget(self.btn_jog)
        row1.addStretch(1)
        ctrl_card.body.addLayout(row1)

        # ---- 给定：调速方式下拉 + 数值 + 下发 ----
        row2 = QHBoxLayout()
        row2.setSpacing(8)
        row2.addWidget(QLabel("调速方式"))
        self.speed_mode = QComboBox()
        self.speed_mode.addItem("频率调速", self.SPEED_MODE_HZ)
        self.speed_mode.addItem("转速调速", self.SPEED_MODE_RPM)
        self.speed_mode.setMinimumWidth(110)
        self.speed_mode.currentIndexChanged.connect(self._on_speed_mode)
        row2.addWidget(self.speed_mode)

        row2.addWidget(QLabel("给定值"))
        self.freq_spin = QSpinBox()
        self.freq_spin.setRange(0, 50)
        self.freq_spin.setSuffix(" Hz")
        # [缺省取中间值] 半速最能体现"转速↔频率"换算是否正确
        # （25Hz → 720rpm 一眼可验），也不会让人误以为满速或近零。
        self.freq_spin.setValue(25)
        self.freq_spin.setMinimumWidth(120)
        row2.addWidget(self.freq_spin)
        self.btn_set_freq = QPushButton("下发")
        self.btn_set_freq.clicked.connect(self._on_set_speed)
        row2.addWidget(self.btn_set_freq)
        row2.addStretch(1)
        ctrl_card.body.addLayout(row2)
        self.root.addWidget(ctrl_card)
        self.root.addStretch(1)

        self._control_buttons = [self.btn_run, self.btn_free,
                                 self.btn_reset, self.btn_set_freq,
                                 self.btn_jog]
        #: 设备是否就绪（服务端回过 ok=true 的快照 / 收到过遥测）。
        self._device_ready = None      # None = 还没探测过
        self._not_ready_reason = ""
        # 初始化时把输入框调到频率模式的口径。
        self._on_speed_mode()

    # ---- 调速方式切换 ----
    def _speed_mode_now(self):
        return self.speed_mode.currentData()

    def _on_speed_mode(self, *_args):
        """切换频率/转速调速：改输入框量程、后缀、步进与初值。

        [为什么切换时要重置值] 15Hz 直接当成 15rpm 下发是荒谬的（几乎不转）。
        切换方式后按当前`设定频率`换算给初值（而非固定值），保证切过去
        显示的就是"当前给定"的等价量，不产生意外跳变。
        无数据时退回**中间值**（半速）。
        """
        st = self.ctx.model.vfd
        # 优先沿用当前给定（Hz），它能同时表达两个口径；无数据时取中值。
        if st.is_fresh() and st.has("setFreqHz"):
            base_hz = float(st.set_freq_hz)
        else:
            base_hz = self._rated_hz / 2.0
        if self._speed_mode_now() == self.SPEED_MODE_RPM:
            max_rpm = int(round(self._rated_rpm))
            self.freq_spin.setRange(0, max_rpm)
            self.freq_spin.setSuffix(" rpm")
            self.freq_spin.setSingleStep(10)
            self.freq_spin.setValue(int(round(base_hz / self._rated_hz
                                              * self._rated_rpm)))
        else:
            self.freq_spin.setRange(0, int(round(self._rated_hz)))
            self.freq_spin.setSuffix(" Hz")
            self.freq_spin.setSingleStep(1)
            self.freq_spin.setValue(int(round(base_hz)))

    # ---- 启停（同一按钮按状态切换） ----
    def _is_running(self):
        """当前是否判定为运行中。

        [判据] 只看遥测的 running 位；数据不新鲜时按"未运行"处理
        （界面上按钮显示"启动"是安全的默认——不会因为数据过期而让人以为
        还在转、去按一个"停止"）。
        """
        st = self.ctx.model.vfd
        return bool(st.is_fresh() and st.running)

    def _on_run_stop(self):
        """一个按钮：运行中→停止；停止→先下给定、再按下拉框方向启动。

        [为什么启动前先下给定] 变频器保留着**上一次**的频率给定。若只发
        启动命令，实际会按旧给定跑（可能是上一班留下的陌生转速），与界面
        上显示"给定值 25Hz"不符——操作员会以为界面在骗人。故启动前先把
        当前给定写下去，保证"界面显示什么，就按什么跑"。
        """
        # 按"点击那一刻"的状态决定动作，而不是按按钮文字——
        # 文字可能因遥测间隔滞后一拍。
        if self._is_running():
            self._send_op(proto.OP_STOP)
            return
        self._send_set_speed()
        reverse = bool(self.dir_combo.currentData())
        self._send_op(proto.OP_START, rev=reverse)

    def _sync_run_button(self, running, releasable=True):
        """按运行状态同步启停按钮的文字与配色。

        :param releasable: 控制闸是否放行（False 时按钮置灰由别处统一处理）
        """
        if running:
            self.btn_run.setText("停止")
            self.btn_run.setStyleSheet(
                "QPushButton { color: #FFFFFF; background-color: #C62828;"
                " font-weight: bold; }")
        else:
            self.btn_run.setText("启动")
            self.btn_run.setStyleSheet(
                "QPushButton { color: #FFFFFF; background-color: #2E7D32;"
                " font-weight: bold; }")

    def _send_set_speed(self, silent=False):
        """按当前调速方式下发给定（启动流程与"下发"按钮共用）。

        :param silent: True 时不覆盖状态栏（启动流程紧跟一条启动命令，
                       两条状态会互相冲掉，只留最终那条）
        """
        value = float(self.freq_spin.value())
        if self._speed_mode_now() == self.SPEED_MODE_RPM:
            hz = value / self._rated_rpm * self._rated_hz if \
                self._rated_rpm > 0 else 0.0
            self._send_op(proto.OP_SET_SPEED, rpm=value, hz=hz, silent=silent)
        else:
            self._send_op(proto.OP_SET_FREQUENCY, hz=value, silent=silent)

    def _on_set_speed(self):
        """「下发」按钮：下发给定。"""
        self._send_set_speed()

    def on_enter(self):
        """入页时拉一次快照：顺便探测「设备是否装配」。

        [为什么用快照探测而不是看健康] 健康条目里 kind=0 的那台可能是
        别的侧；快照请求的应答直接用「ok=false + 原因」告诉我们这台能不能用，
        比解析健康表更直接。
        """
        def _done(code, dtype, data):
            """快照应答（**网络线程**）：解析后投递到 UI 线程再改控件。

            [为什么必须投递] 这里要 `_rpm_hint.setText()` 和
            `_on_speed_mode()`（后者改 QSpinBox 的量程/后缀/值）——
            都是 QWidget 操作，从网络线程直接调会让 Qt 崩进程。
            """
            from . import bson as _bson
            try:
                doc = _bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            ok_flag = doc.get("ok") is not False
            err_text = doc.get("err", "设备未就绪")
            rpm = doc.get("ratedRpm")
            hz = doc.get("ratedHz")
            has_rated = (isinstance(rpm, (int, float)) and rpm > 0) or \
                        (isinstance(hz, (int, float)) and hz > 0)

            def _apply():
                if not ok_flag:
                    self._device_ready = False
                    self._not_ready_reason = err_text
                else:
                    self._device_ready = True
                    self._not_ready_reason = ""
                # 额定点由服务端下发（Hz ↔ rpm 换算依据）。
                if isinstance(rpm, (int, float)) and rpm > 0:
                    self._rated_rpm = float(rpm)
                if isinstance(hz, (int, float)) and hz > 0:
                    self._rated_hz = float(hz)
                if has_rated:
                    self._rpm_hint.setText(
                        "输出转速为频率换算值（额定点 %.0f rpm @ %.0f Hz）"
                        % (self._rated_rpm, self._rated_hz))
                    self._on_speed_mode()
            self.ctx.bridge.run_in_ui(_apply)

        def _err(msg):
            self._device_ready = False
            self._not_ready_reason = msg
        self.ctx.bridge.request(proto.CMD_VFD_SNAPSHOT, None, on_done=_done,
                                on_error=_err)

    # ---- 控制 ----
    def _send_op(self, op, rev=False, hz=None, rpm=None, silent=False):
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止操作：%s" % reason)
            return
        doc = {"op": int(op)}
        if op in (proto.OP_START, proto.OP_JOG):
            doc["rev"] = bool(rev)
        if hz is not None:
            doc["hz"] = float(hz)
        if rpm is not None:
            doc["rpm"] = float(rpm)
        if not silent:
            self.ctx.set_status("下发命令 op=%d…" % op)
        self.ctx.bridge.send_vfd_control(doc, on_done=self._on_ctrl_done,
                                         on_error=self._on_ctrl_err)

    def _on_ctrl_done(self, code, dtype, data):
        """控制应答（**网络线程**）：解析后投递到 UI 线程再改界面。"""
        from . import bson
        try:
            doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
        except ValueError:
            doc = {}
        ok = bool(doc.get("ok"))
        err = doc.get("err", "未知原因")

        def _apply():
            if ok:
                self.ctx.set_status("命令已受理")
            else:
                self.ctx.set_status("命令被拒：%s" % err)
        self.ctx.bridge.run_in_ui(_apply)

    def _on_ctrl_err(self, msg):
        self.ctx.bridge.run_in_ui(
            lambda: self.ctx.set_status("命令失败：%s" % msg))

    def _on_set_freq(self):
        self._send_op(proto.OP_SET_FREQUENCY, hz=self.freq_spin.value())

    # ---- 点动手持 ----
    def _jog_start(self, *_args):
        """按下点动：方向取**方向下拉框**（与启动同一口径）。

        [为什么方向跟随下拉框而不是两个按钮] 现场只有"点动"这一个动作，
        方向是参数。两个按钮（正转点动/反转点动）会让操作员在急停时刻
        还要先找对按钮，容易按错方向。
        """
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止点动：%s" % reason)
            return
        reverse = bool(self.dir_combo.currentData())
        # [点动前先下给定] 点动速度同样取界面上的"给定值"。变频器里可能
        # 存着上一班的旧给定，只发点动命令会按旧值动 —— 与界面对不上。
        self._send_set_speed(silent=True)
        self._jog_active = True
        self._jog_reverse = reverse
        self._jog_repeat()
        self._jog_timer.start()
        self.ctx.set_status("点动中（%s，给定 %s）——松手即停"
                            % ("反转" if reverse else "正转",
                               self.freq_spin.text()))

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
            # 转速：服务端按额定点由输出频率换算后下发（变频器无转速寄存器）。
            # 无该字段时用本地额定点兜底算，保证界面上总有个可用读数；
            # 两者都没条件时显示 "--"。
            if st.has("speedRpm"):
                show("rpm", "%.0f" % st.speed_rpm)
            elif st.has("outputFreqHz") and self._rated_hz > 0:
                show("rpm", "%.0f" % (st.output_freq_hz / self._rated_hz
                                      * self._rated_rpm))
            else:
                show("rpm", "--")
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

        # 启停按钮跟随运行状态（运行中显示"停止"、停止时显示"启动"）。
        self._sync_run_button(fresh and st.running)

        reason = self.ctx.control_blocked_reason()
        if not reason and self._device_ready is False:
            reason = getattr(self, "_not_ready_reason", "") or "设备未就绪"
        for btn in self._control_buttons:
            btn.setEnabled(not reason)

        # ---- 「下发」只在**运行中**可点 ----
        #
        # [为什么] 服务端已明确拒绝停机时的给定（给定是运行参数，不是可
        # 预置配置）。界面若仍让点，用户点完只收到一句拒绝、还以为是故障。
        # 置灰 + 提示"先启动"才是诚实的前馈。
        # 判据与服端同口径：用**遥测的运行状态**，不是按钮文字。
        running = bool(fresh and st.running)
        self.btn_set_freq.setEnabled(not reason and running)

        if reason:
            self._ctrl_hint.setText("控制已禁用：%s" % reason)
        elif not running:
            self._ctrl_hint.setText("停机状态下不可调速：请先启动，再下发给定")
        else:
            self._ctrl_hint.setText("")


# ============================================================================
#                       监视 - 电磁流量计
# ============================================================================

class WaterPage(Page):
    """水质与进水泵页（电磁流量计 + 两台浊度仪 + 进水泵）。

    [为什么合成一页] 这三样在工艺上是一条链：**进水泵 → 管道流量 → 浊度**。
    操作员关心的是"泵开着吗、流量多少、水浑不浑"，分开三页要来回切。
    合成一页后一屏看完，并且泵的开关就在旁边，不用跳页去操作。

    ============================================================================
                                  版式
    ============================================================================

        ┌──────────────────────────────────────────────────────┐
        │ 电磁流量计 · LD 系列                                   │
        │  瞬时流量    瞬时流速    正向累积    反向累积   正常     │
        ├────────────────────────────────────┬─────────────────┤
        │ 浊度监测                            │ 进水泵           │
        │  浊度仪1        浊度仪2             │  [启动] [停止]   │
        │  12.43 NTU      3.22 NTU           │ 运行（输出已开）  │
        │                                     │ 无运行反馈…      │
        └────────────────────────────────────┴─────────────────┘

    [为什么浊度区比泵区宽] 浊度是**要读数值**的（两位小数 + 单位），
    泵只是开关 + 一个状态词。给泵留太宽会让浊度数字挤成小字。
    """

    TOPICS = (proto.TOPIC_FLOWMETER_STATUS,
              proto.TOPIC_TURBIDITY_STATUS,
              proto.TOPIC_PUMP_STATUS)

    #: 浊度视觉告警阈值（NTU）。
    #:
    #: [为什么阈值放客户端] 这只是**显示口径**（数字标不标红），不是报警判据：
    #: 真要进报警列表/推送事件，必须挪到服务端（客户端各自算阈值必然漂移，
    #: 而且断线时客户端看到的是旧值）。这里仅做低风险的视觉提示。
    TURBIDITY_WARN_NTU = 20.0

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("水质监测", "电磁流量计 · 浊度仪 ×2 · 进水泵")

        # ---- 流量计卡（一行多块，主读数最大）----
        flow_card = Card("电磁流量计 · LD 系列")
        from .widgets import StatBlock
        fgrid = QHBoxLayout()
        fgrid.setSpacing(26)
        self._flow_blocks = {}
        for key, caption, unit, size in (
                ("flow", "瞬时流量", "m³/h", 26),
                ("speed", "瞬时流速", "m/s", 22),
                ("fwd", "正向累积", "m³", 22),
                ("rev", "反向累积", "m³", 18)):
            block = StatBlock(caption, unit, "--", value_size=size)
            fgrid.addWidget(block)
            self._flow_blocks[key] = block
        fgrid.addStretch(1)
        self._flow_alarm = QLabel("--")
        self._flow_alarm.setAlignment(Qt.AlignBottom | Qt.AlignRight)
        fgrid.addWidget(self._flow_alarm)
        flow_card.body.addLayout(fgrid)

        self._flow_note = QLabel("")
        self._flow_note.setWordWrap(True)
        self._flow_note.setStyleSheet(theme.caption_label_qss())
        self._flow_note.setVisible(False)
        flow_card.add(self._flow_note)
        self.root.addWidget(flow_card)

        # ---- 下部：浊度（左宽） + 进水泵（右窄）----
        bottom = QHBoxLayout()
        bottom.setSpacing(10)

        turb_card = Card("浊度监测")
        # [每台一个横向行布局，不能把数值与单位塞进同一网格单元]
        # 塞同一格会让两个控件**重叠绘制**（实测 12.15 和 NTU 叠在一起），
        # 因为 QGridLayout 允许同格多控件，后加的画在上层且不自动错开。
        # 用 QHBoxLayout 让单位紧跟数值右侧，基线对齐更自然。
        self._turb_rows = []          # [(name_label, value_label)]
        self._turb_units = []         # [unit_label]（单独留一份，便于自测查重叠）
        for idx in range(2):
            row = QHBoxLayout()
            row.setSpacing(6)
            name = QLabel("浊度仪%d" % (idx + 1))
            name.setStyleSheet(
                "QLabel { color: #5A6572; font-size: 13px;"
                " background: transparent; }")
            value = QLabel("--")
            value.setStyleSheet(
                "QLabel { color: #1A1A1A; font-size: 30px; font-weight: bold;"
                " background: transparent; }")
            value.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
            unit = QLabel("NTU")
            unit.setStyleSheet(
                "QLabel { color: #5A6572; font-size: 13px;"
                " background: transparent; padding-bottom: 8px; }")
            unit.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
            row.addWidget(name)
            row.addSpacing(6)
            row.addWidget(value)
            row.addWidget(unit)
            row.addStretch(1)
            turb_card.body.addLayout(row)
            self._turb_rows.append((name, value))
            self._turb_units.append(unit)

        self._turb_note = QLabel("")
        self._turb_note.setWordWrap(True)
        self._turb_note.setStyleSheet(theme.caption_label_qss())
        self._turb_note.setVisible(False)
        turb_card.add(self._turb_note)

        # [用说明行填掉下方留白] 两块卡在同一横排会被拉成等高，浊度侧只有
        # 两行读数，不补内容就在下面留一大片空白。这里顺便把"共用 485 口、
        # 站号 1/3"写清楚——现场查线最常问的就是这个。
        turb_hint = QLabel("与电磁流量计共用一路 485（站号 1 / 站号 3）；"
                           "超过 %.0f NTU 标红。"
                           % self.TURBIDITY_WARN_NTU)
        turb_hint.setWordWrap(True)
        turb_hint.setStyleSheet(theme.caption_label_qss())
        turb_card.add(turb_hint)
        turb_card.body.addStretch(1)
        bottom.addWidget(turb_card, 3)

        pump_card = Card("进水泵")
        # [名字写到卡标题上，不在正文里重复一遍] 正文再放一个"进水泵"
        # 就是同一句话连说两次。服务端给了名字就地更新标题（见 _render_pump）。
        self._pump_card = pump_card

        prows = QHBoxLayout()
        prows.setSpacing(8)
        self.btn_pump_on = QPushButton("启动")
        self.btn_pump_on.setMinimumHeight(38)
        self.btn_pump_on.clicked.connect(lambda: self._request_pump(True))
        self.btn_pump_off = QPushButton("停止")
        self.btn_pump_off.setMinimumHeight(38)
        self.btn_pump_off.clicked.connect(lambda: self._request_pump(False))
        prows.addWidget(self.btn_pump_on, 1)
        prows.addWidget(self.btn_pump_off, 1)
        pump_card.body.addLayout(prows)

        self._pump_state = QLabel("--")
        self._pump_state.setStyleSheet(
            "QLabel { font-size: 14px; font-weight: bold; color: #5A6572;"
            " background: transparent; }")
        pump_card.add(self._pump_state)

        # [必须写明"无反馈"] 现场泵只有接触器输出，没有任何运行反馈点。
        # 不说清楚会让人把"输出已开"当成"泵确已在转"，那是两回事。
        # [不写 Markdown] QLabel 不解析标记，写成 **xx** 会原样显示星号。
        self._pump_note = QLabel(
            "现场仅有接触器输出、无运行反馈点：此处显示的是"
            "输出命令状态，不代表泵确实在转。")
        self._pump_note.setWordWrap(True)
        self._pump_note.setStyleSheet(theme.caption_label_qss())
        pump_card.add(self._pump_note)
        pump_card.body.addStretch(1)
        bottom.addWidget(pump_card, 2)

        self.root.addLayout(bottom)
        self.root.addStretch(1)

    # ==================================================================
    #                          进水泵控制
    # ==================================================================
    def _request_pump(self, want_on):
        """下发泵开关。

        [逻辑全在服务端] 幂等、门禁、回显确认都由服务端判定；客户端只发
        意图并按应答更新界面（服务端在应答里带了命令之后的回显）。
        """
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止操作：%s" % reason)
            return
        self.ctx.set_status("进水泵 → 正在%s…"
                            % ("启动" if want_on else "停止"))

        def _done(code, dtype, data):
            """泵控制应答（**网络线程**）：碰控件必须投递到 UI 线程。"""
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            ok = bool(doc.get("ok"))
            err = doc.get("err", "未知原因")

            def _apply():
                if ok:
                    # 应答里带命令之后的回显，直接落地（不必等下一拍推送）
                    self.ctx.model.pump.apply(doc)
                    self.ctx.set_status("进水泵：%s"
                                        % ("已启动" if want_on else "已停止"))
                    self._render_pump()
                else:
                    self.ctx.set_status("进水泵操作失败：%s" % err)
            self.ctx.bridge.run_in_ui(_apply)

        def _err(msg):
            self.ctx.bridge.run_in_ui(
                lambda: self.ctx.set_status("进水泵操作失败：%s" % msg))

        self.ctx.bridge.send_pump_control(0, want_on,
                                          on_done=_done, on_error=_err)

    # ==================================================================
    #                            刷新
    # ==================================================================
    def refresh(self):
        self._render_flow()
        self._render_turbidity()
        self._render_pump()

    def _render_flow(self):
        st = self.ctx.model.flowmeter
        if not st.is_fresh():
            for block in self._flow_blocks.values():
                block.set_value("--")
            self._flow_alarm.setText("--")
            self._flow_alarm.setStyleSheet(
                "QLabel { font-size: 14px; font-weight: bold; color: #5A6572;"
                " background: transparent; }")
            self._flow_note.setText(
                "未收到流量计数据（服务端未装配 / 从站失联）")
            self._flow_note.setVisible(True)
            return
        self._flow_note.setVisible(False)
        self._flow_blocks["flow"].set_value("%.2f" % st.flow)
        self._flow_blocks["flow"].set_suffix(st.flow_unit_text())
        self._flow_blocks["speed"].set_value("%.3f" % st.speed)
        self._flow_blocks["fwd"].set_value("%.2f" % st.forward_total)
        self._flow_blocks["fwd"].set_suffix(st.total_unit_text())
        self._flow_blocks["rev"].set_value("%.2f" % st.reverse_total)
        self._flow_blocks["rev"].set_suffix(st.total_unit_text())
        ok = (st.alarms == 0)
        self._flow_alarm.setText("正常" if ok else "报警 0x%04X" % st.alarms)
        self._flow_alarm.setStyleSheet(
            "QLabel { font-size: 14px; font-weight: bold; color: %s;"
            " background: transparent; }"
            % (theme.CLR_OK if ok else theme.CLR_FAULT))

    def _render_turbidity(self):
        st = self.ctx.model.turbidity
        fresh = st.is_fresh()
        for idx, (name_lbl, value_lbl) in enumerate(self._turb_rows):
            if not fresh:
                value_lbl.setText("--")
                value_lbl.setStyleSheet(
                    "QLabel { color: #9E9E9E; font-size: 30px;"
                    " font-weight: bold; background: transparent; }")
                continue
            # 名字由服务端给（客户端不猜"原水/出水"）
            server_name = st.name_of(idx)
            if server_name:
                name_lbl.setText(server_name)
            ntu = st.ntu_of(idx)
            if ntu is None:
                value_lbl.setText("--")
                value_lbl.setStyleSheet(
                    "QLabel { color: #9E9E9E; font-size: 30px;"
                    " font-weight: bold; background: transparent; }")
                continue
            value_lbl.setText("%.2f" % ntu)
            warn = ntu >= self.TURBIDITY_WARN_NTU
            value_lbl.setStyleSheet(
                "QLabel { color: %s; font-size: 30px; font-weight: bold;"
                " background: transparent; }"
                % (theme.CLR_FAULT if warn else "#1A1A1A"))
        if not fresh:
            self._turb_note.setText(
                "未收到浊度仪数据（服务端未装配 / 从站失联）")
            self._turb_note.setVisible(True)
        elif st.count == 0:
            self._turb_note.setText("服务端未装配浊度仪")
            self._turb_note.setVisible(True)
        else:
            self._turb_note.setVisible(False)

    def _render_pump(self):
        st = self.ctx.model.pump
        reason = self.ctx.control_blocked_reason()
        has = st.count > 0
        if has and st.name_of(0) and self._pump_card.title_label is not None:
            self._pump_card.title_label.setText(st.name_of(0))
        if not has or not st.is_fresh():
            self._pump_state.setText("--")
            self._pump_state.setStyleSheet(
                "QLabel { font-size: 14px; font-weight: bold; color: #5A6572;"
                " background: transparent; }")
            self.btn_pump_on.setEnabled(False)
            self.btn_pump_off.setEnabled(False)
            return
        on = st.is_on(0)
        ready = st.is_ready(0)
        if not ready:
            self._pump_state.setText("未就绪")
            color = "#9E9E9E"
        elif on:
            self._pump_state.setText("运行（输出已开）")
            color = theme.CLR_OK
        else:
            self._pump_state.setText("停止（输出已关）")
            color = "#37474F"
        self._pump_state.setStyleSheet(
            "QLabel { font-size: 14px; font-weight: bold; color: %s;"
            " background: transparent; }" % color)

        can = ready and not reason
        self.btn_pump_on.setEnabled(can)
        self.btn_pump_off.setEnabled(can)
        # 已是该状态的那一侧置灰（服务端也幂等，但界面不该给可点的假象）
        if can:
            if on:
                self.btn_pump_on.setEnabled(False)
            else:
                self.btn_pump_off.setEnabled(False)
