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
                             QPushButton, QSlider, QSpinBox, QStyle,
                             QStyleOptionSlider, QVBoxLayout, QWidget)

from . import proto, theme
from .page_base import DataTable, LampRow, Page, ToggleGrid, section_title
from .widgets import Card


# ============================================================================
#                          监视 - 数字量
# ============================================================================

class DigitalPage(Page):
    """数字量监视/操作页（点数由服务端下发）。

    上下两区：DI 只读灯阵列、DO 可操作按钮阵。多台模块用顶部下拉切换。

    [为什么点数不能在客户端写死] 单模块物理 16 点，但该型号支持主从聚合
    （多台合成一台的地址空间，最多 32 点）。点数是**服务端说了算**且会随
    现场接线变化：写死 16 会让聚合后的上半区永远看不到，而服务端明明在发
    —— 表现为"界面只有 16 个灯，但服务端日志说 32"。故灯阵/按钮阵按快照里
    的 count 键**按需重建**。

    [现场当前 16 点] .12 已于 2026-11-06 下线拆除，只剩 .13 单台 16 点；
    聚合能力仍保留在协议与实现里，将来加第二台不用改客户端。

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
        # 卡片最小高度跟着点数走（16 点 2 行、聚合 32 点 4 行）。
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
        # [位宽跟着点数走] 超过 16 点（聚合场景）要用 8 位十六进制，
        # 写成 %04X 会把上半区截掉，让人以为 bit16+ 恒为 0。
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
        self._table.set_rows([("通道 %d" % i, "", "--", "mA")
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
            self._table.set_rows([("通道 %d" % i, "", "--", "mA")
                                  for i in range(st.channels)])
            self._raw_caption.setText("无数据（未订阅或设备离线）")
            return
        rows = []
        for i in range(st.channels):
            label = "通道 %d" % i
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
        raws = " ".join("CH%d=%dmV" % (i, st.raw[i])
                        for i in range(st.channels))
        self._raw_caption.setText("原始寄存器：" + raws)


# ============================================================================
#                          监视 - 变频器
# ============================================================================

class _TouchSlider(QSlider):
    """触摸友好的横向滑块：**按哪里就跳到哪里**。

    [为什么需要这个子类] Qt 的 QSlider 默认只有在**手柄**（或轨道）上按下
    才开始拖动；按在控件上下方的空白区（控件 56px 高、轨道只有 10px）时
    事件被忽略，用户看到的是"按上去没反应"。现场反馈原话：「触摸范围感觉
    有点小，滑块可以大点，有时候按上去没反应」。

    处理方式：按下时若落点不在手柄上，直接按落点换算成值并跳过去（等价
    Android/iOS 滑块的"点轨道即跳"），然后照常进入拖动。这样整个控件的
    **矩形内任意位置**都能起拖，命中率与触摸面积一致。

    [为什么不重写 hitButton] `hitButton()` 只决定"这次按下算不算"，返回
    True 后 Qt 仍按原生逻辑（点到哪就跳到哪）处理 —— 但原生 `mousePressEvent`
    会自行再判一次位置来决定是否 setSliderDown，行为在不同 Qt 版本上不一致。
    直接重写 `mousePressEvent` 最可控：自己算值 + 自己置 down。
    """

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            QSlider.mousePressEvent(self, event)
            return
        # 先让 Qt 走它自己的判定：命中手柄时行为完全不变（拖动体验一致）。
        if self._hit_handle(event.pos()):
            QSlider.mousePressEvent(self, event)
            return
        # 未命中手柄 ⇒ 按落点跳值，再进入拖动（对齐"点轨道即跳"）。
        self._jump_to(event.x())
        self.setSliderDown(True)
        event.accept()

    def mouseMoveEvent(self, event):
        # 已经在拖动中：交给基类按位置更新值（与原生拖动手感一致）。
        QSlider.mouseMoveEvent(self, event)

    def _handle_half_width(self):
        """手柄半宽（像素）。QSS 里手柄 34px 宽，取一半做命中判定。"""
        return 17

    def _hit_handle(self, pos):
        """落点是否在手柄的**触摸友好**范围内（左右各放宽半个手柄）。"""
        span = self.maximum() - self.minimum()
        if span <= 0:
            return False
        opt = QStyleOptionSlider()
        self.initStyleOption(opt)
        groove = self.style().subControlRect(
            QStyle.CC_Slider, opt, QStyle.SC_SliderGroove, self)
        handle = self.style().subControlRect(
            QStyle.CC_Slider, opt, QStyle.SC_SliderHandle, self)
        # 手柄矩形已被 QSS 撑到 34x34；再放宽到整个控件高度以便好按。
        left = handle.center().x() - self._handle_half_width()
        right = handle.center().x() + self._handle_half_width()
        # 纵向放宽：整个控件高度都算命中（触摸屏上手指不会精确落在手柄里）
        return left <= pos.x() <= right and 0 <= pos.y() <= self.height()

    def _jump_to(self, x):
        """把值设到 x 对应的刻度（按槽位线性换算）。"""
        opt = QStyleOptionSlider()
        self.initStyleOption(opt)
        groove = self.style().subControlRect(
            QStyle.CC_Slider, opt, QStyle.SC_SliderGroove, self)
        span = self.maximum() - self.minimum()
        if groove.width() <= 0 or span <= 0:
            return
        # groove 两端各缩进半个手柄，Qt 原生也是这个口径（否则两端取不到极值）
        half = self._handle_half_width()
        usable = max(1, groove.width() - 2 * half)
        ratio = (x - (groove.x() + half)) / float(usable)
        ratio = max(0.0, min(1.0, ratio))
        self.setValue(int(round(self.minimum() + ratio * span)))


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

    #: 拖动调速的**下发去抖窗口**（毫秒）。
    #:
    #: [为什么用去抖而不是"松手才发"] 拖动过滑槽时 Qt 每格都发 valueChanged，
    #: 逐格下发会在总线上刷一串 FC06（泵/阀跟着抖）；所以必须等"手停下来"
    #: 再发。而"松手才发"依赖 sliderReleased —— 单点滑槽（不拖、只戳一下）
    #: 时 Qt **根本不发** released，那种操作就永远下发不出去，现场触摸屏上
    #: "戳一下"恰恰是最常见的手法。
    #:
    #: 去抖同时覆盖两种手法：每次 valueChanged 重置计时，静默 250ms 就发
    #: 一条。拖动中不停变化 ⇒ 一直不发；停手/松手/单点 ⇒ 250ms 后发一条。
    #: 不依赖 isSliderDown()（离屏/某些平台下它不反映真实按下态）。
    SLIDER_SEND_DELAY_MS = 250

    #: 数字框改值的**下发去抖窗口**（毫秒）。
    #:
    #: [为什么需要] 「下发」按钮已去掉（用户 2026-11-06：「去掉频率或转速
    #: 下发，改成在启动状态下实时显示」），改值即生效。但 QSpinBox 每敲一
    #: 个数字、每点一次箭头都发 valueChanged，逐次下发会在总线上刷一串
    #: FC06（泵会跟着抖）。与滑块同策：静默 400ms 才发一条。
    #: 取 400ms 而非 250ms：键盘逐位输入间隔比拖滑块长，窗口太短会在打字
    #: 中途就发出去（打 "1200" 会在 "12" 时就下发一次 12rpm）。
    SPIN_SEND_DELAY_MS = 400

    #: 调速方式缺省值：**转速**（用户 2026-11-06 口径「默认用速度调速」）。
    #:
    #: [为什么缺省是转速而不是频率] 操作员关心的是"泵转多快"，而不是
    #: "变频器输出多少 Hz"。频率只是实现手段，把它摆在前台会让人每次都要
    #: 心算换算。现场铭牌 50Hz≈1440rpm，转速才是可读的量。
    DEFAULT_SPEED_MODE = 1          # = SPEED_MODE_RPM（见下）
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
        #: 点动按下那一刻的给定（每拍心跳原样捎带，不用每拍重读输入框）。
        self._jog_given = {}
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
        # [已去掉「下发」按钮 2026-11-06] 用户口径：「去掉频率或转速下发，
        # 改成在启动状态下实时显示」。改值即生效，交互更顺：
        #   · 键盘输入/箭头改数字 → 停手即下发（_SPIN_SEND_DELAY_MS 去抖）
        #   · 拖动滑块 → 停手 250ms 去抖下发（见 _on_slider_settled）
        # 停机时仍只预置（不发帧），启动时随命令带走。
        self._spin_send_timer = QTimer(self)
        self._spin_send_timer.setSingleShot(True)
        self._spin_send_timer.setInterval(self.SPIN_SEND_DELAY_MS)
        self._spin_send_timer.timeout.connect(self._on_spin_settled)
        self.freq_spin.valueChanged.connect(self._on_spin_value_changed)
        row2.addStretch(1)
        ctrl_card.body.addLayout(row2)

        # ---- 拖动调速条（触摸屏主用；无键盘也能调速）----
        # [为什么还要滑块] 现场是触摸屏，没有外接键盘。点 HSpinBox 的小箭头
        # 一下一下点太慢，点数字区又会弹软键盘占掉半屏 —— 用户 2026-11-06
        # 明确要求"一个按住滑动调速"。
        #
        # [拖动期不下发，松手才发] 滑块的 valueChanged 在拖动过程中每格都
        # 触发；逐格下发会在总线上打出一串 FC06（现场阀/泵都会跟着抖）。
        # 故：拖动/点击过程中只同步数字框显示，`sliderReleased` 时才下发
        # 一条。键盘/箭头改数字框仍然走「下发」按钮，语义不变。
        row3 = QHBoxLayout()
        row3.setSpacing(8)
        row3.addWidget(QLabel("拖动调速"))
        self.freq_slider = _TouchSlider(Qt.Horizontal)
        # [触摸目标] 手指接触面约 8~10mm，1280x800 屏上约 30~40px。
        # 控件给到 56px 高：轨道 10px + 34px 圆形手柄（见 theme 的 QSlider
        # QSS）之后，上下仍有富余的**可点区域**，按偏一点也能拖动。
        self.freq_slider.setMinimumHeight(56)
        self.freq_slider.setTracking(True)
        self.freq_slider.setMinimumWidth(320)
        self.freq_slider.valueChanged.connect(self._on_slider_moved)
        self.freq_slider.sliderReleased.connect(self._on_slider_released)
        row3.addWidget(self.freq_slider, 1)
        #: 拖动中的实时读数（数字框在拖动时只有 0~50 的整数量级，
        #: 现场还需要看到"松手会下发多少"，故给一个醒目的大字标签）。
        self._slider_value = QLabel("--")
        self._slider_value.setMinimumWidth(96)
        self._slider_value.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._slider_value.setStyleSheet(
            "QLabel { font-size: 30px; font-weight: bold; color: #1A66D0; }")
        row3.addWidget(self._slider_value)
        ctrl_card.body.addLayout(row3)
        self._slider_syncing = False        # 防止 数字框↔滑块 互踢
        #: 停机时拖动过滑块 ⇒ 给定是"预置"值，等启动时生效。
        #: [为什么需要这个标记] 停机时变频器回报的 setFreqHz 是**上次**的
        #: 旧值；refresh() 每 200ms 采纳一次，若不挡住，用户刚拖好的预置值
        #: 会被立刻改回去（看起来就是"拖不动/自己跳回来"）。
        self._preset_dirty = False
        #: 拖动去抖定时器：见 _on_slider_moved。
        self._slider_send_timer = QTimer(self)
        self._slider_send_timer.setSingleShot(True)
        self._slider_send_timer.setInterval(self.SLIDER_SEND_DELAY_MS)
        self._slider_send_timer.timeout.connect(self._on_slider_settled)

        self.root.addWidget(ctrl_card)
        self.root.addStretch(1)

        self._control_buttons = [self.btn_run, self.btn_free,
                                 self.btn_reset, self.btn_jog]
        #: 设备是否就绪（服务端回过 ok=true 的快照 / 收到过遥测）。
        self._device_ready = None      # None = 还没探测过
        self._not_ready_reason = ""
        # [缺省转速] 用户 2026-11-06 口径「默认用速度调速」。必须放在
        # freq_spin / freq_slider 都建好之后：`_on_speed_mode` 会改它们的
        # 量程/后缀/值（早设会 AttributeError，构造期实测踩过）。
        self.speed_mode.setCurrentIndex(self.DEFAULT_SPEED_MODE)
        # 初始化时把输入框/滑块调到当前调速方式的口径。
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
        # 滑块与数字框同量程（两种模式都跟：Hz 0~50 / rpm 0~1440）。
        self._sync_slider_range()

    # ---- 拖动调速 ----
    def _sync_slider_range(self):
        """滑块量程/取值跟随数字框（频率/转速两种模式通用）。

        [为什么不固定 0~50] 转速模式下给定是 rpm（0~1440），滑块若还停在
        50 会把"拖动"变成微调；反之频率模式下用 1440 档位则一格跨 1Hz，
        手指根本停不到想要的数。量程必须与当前口径一致。
        """
        lo, hi = self.freq_spin.minimum(), self.freq_spin.maximum()
        self._slider_syncing = True
        try:
            self.freq_slider.setRange(lo, hi)
            self.freq_slider.setValue(self.freq_spin.value())
        finally:
            self._slider_syncing = False

    def _on_slider_moved(self, value):
        """拖动/点按中：只同步显示，并重置下发去抖。

        [为什么不在拖动中直接下发] 见 SLIDER_SEND_DELAY_MS 的说明：逐格
        下发会在总线上刷一串 FC06。这里只更新界面 + 重置 250ms 计时器，
        真正下发在 `_on_slider_settled`。

        [必须同时停掉数字框的去抖]** 同一动作会途经两个控件：滑块改值 →
        我们调 `freq_spin.setValue()` → 触发 `_on_spin_value_changed` →
        它启动**数字框**的去抖计时器。两个计时器都到点就下发两条。
        `_slider_syncing` 只能挡住"值回写循环"，挡不住定时器 —— 实测
        「松手立即下发 1 条 + 数字框去抖补 1 条 = 2 条」（C2 失败）。
        故这里显式 stop。
        """
        if self._slider_syncing:
            return
        unit = "rpm" if self._speed_mode_now() == self.SPEED_MODE_RPM else "Hz"
        self._slider_value.setText("%d %s" % (value, unit))
        # 数字框跟着走：让操作员拖动时就看见"会下发多少"，
        # 也保证 _given_payload() 读到的是滑块位置（它只读数字框）。
        self._slider_syncing = True
        try:
            self.freq_spin.setValue(value)
        finally:
            self._slider_syncing = False
        self._spin_send_timer.stop()           # 见 docstring：防双发
        self._slider_send_timer.start()        # 重置：手不停就一直不发

    def _on_slider_released(self):
        """松手：不等去抖窗口，立即下发（手已经离开，意图明确）。"""
        if self._slider_syncing:
            return
        self._slider_send_timer.stop()
        self._on_set_speed()

    def _on_slider_settled(self):
        """停手 250ms：下发一条给定（拖动彻底停下 / 单点滑槽走这里）。

        [停机时只预置，不发] 服务端会拒绝停机给定（见 refresh 里的说明）。
        这里改 `_preset_dirty` 标记，让 `_on_run_stop` 启动时捎带；
        同时**挡住遥测对采纳**，否则用户刚预置的值会被变频器回报的旧值改回去。
        """
        if self._slider_syncing:
            return
        if not self._is_running():
            self._preset_dirty = True
            self._send_set_speed(silent=True, preset_only=True)
            return
        self._send_set_speed(silent=True)
        self._preset_dirty = False

    # ---- 数字框调速（无「下发」按钮，改值即生效）----
    def _on_spin_value_changed(self, value):
        """数字框改值：同步滑块 + 重置下发去抖。

        [为什么先同步滑块] 键盘输入 900 后滑块还停在原位，视觉上"没生效"。
        同步过去用户立刻看到手柄跟着走。（滑块自己改值时用 _slider_syncing
        防互踢，这里必须同样置位。）
        """
        if self._slider_syncing:
            return
        self._slider_syncing = True
        try:
            if self.freq_slider.value() != value:
                self.freq_slider.setValue(value)
            unit = ("rpm" if self._speed_mode_now() == self.SPEED_MODE_RPM
                    else "Hz")
            self._slider_value.setText("%d %s" % (value, unit))
        finally:
            self._slider_syncing = False
        # [停掉滑块去抖]** 与 _on_slider_moved 同理：改数字框会回写滑块，
        # 滑块 valueChanged 可能已启动它的去抖计时器。两个都到点就双发。
        self._slider_send_timer.stop()
        self._spin_send_timer.start()      # 静默 400ms 才下发

    def _on_spin_settled(self):
        """数字框停手 400ms：下发一条给定（停机时只预置）。"""
        if self._slider_syncing:
            return
        if not self._is_running():
            self._preset_dirty = True
            self._send_set_speed(silent=True, preset_only=True)
            return
        self._send_set_speed(silent=True)
        self._preset_dirty = False

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
        """一个按钮：运行中→停止；停止→按界面给定值 + 下拉框方向启动。

        [给定必须捎带在启动命令里，不能先单独发] 服务端为安全起见**拒绝
        停机状态下的独立给定**（`_VFD_SETPOINT_OPS`）——只发一条 SET 再发
        START，SET 会被拒，启动按变频器里残留的旧给定跑（现场实测：界面
        设 3Hz，实际跑 25Hz）。修法是把给定绑在 OP_START 上，服务端先写
        给定再发命令字。见 server_py/hsfsrv/devices.py::apply_control。
        """
        # 按"点击那一刻"的状态决定动作，而不是按按钮文字——
        # 文字可能因遥测间隔滞后一拍。
        if self._is_running():
            self._send_op(proto.OP_STOP)
            return
        reverse = bool(self.dir_combo.currentData())
        payload = self._given_payload()
        # [预置值在此刻兑现] 停机时拖动的给定通过启动命令捎带下去；发完就
        # 清标记，之后的遥测采纳恢复正常（变频器里已经是这个值了）。
        self._preset_dirty = False
        self._send_op(proto.OP_START, rev=reverse, **payload)

    def _given_payload(self):
        """当前界面给定值 → 捎带给启动/点动的 {hz, rpm?}。

        频率模式只带 hz；转速模式同时带 rpm 与换算出来的 hz（服务端按
        rpm 优先换算，hz 作为已算好的结果一并给出，避免两边换算口径漂移）。
        """
        value = float(self.freq_spin.value())
        if self._speed_mode_now() == self.SPEED_MODE_RPM:
            hz = (value / self._rated_rpm * self._rated_hz
                  if self._rated_rpm > 0 else 0.0)
            return {"rpm": value, "hz": hz}
        return {"hz": value}

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

    def _send_set_speed(self, silent=False, preset_only=False):
        """按当前调速方式下发给定（启动流程与"下发"按钮共用）。

        :param silent: True 时不覆盖状态栏（启动流程紧跟一条启动命令，
                       两条状态会互相冲掉，只留最终那条）
        :param preset_only: True = **只预置、不发帧**（停机拖动滑块时用）。
                       停机下发会被服务端拒绝（给定是运行参数），预置值等
                       启动时由 `_given_payload()` 捎带下去。此时仅刷提示。
        """
        value = float(self.freq_spin.value())
        unit = "rpm" if self._speed_mode_now() == self.SPEED_MODE_RPM else "Hz"
        if preset_only:
            msg = "已预置给定 %d %s（启动时生效）" % (int(value), unit)
            self._ctrl_hint.setText(msg)
            if not silent:
                self.ctx.set_status(msg)
            return
        if self._speed_mode_now() == self.SPEED_MODE_RPM:
            hz = value / self._rated_rpm * self._rated_hz if \
                self._rated_rpm > 0 else 0.0
            self._send_op(proto.OP_SET_SPEED, rpm=value, hz=hz, silent=silent)
        else:
            self._send_op(proto.OP_SET_FREQUENCY, hz=value, silent=silent)

    def _on_set_speed(self):
        """立即下发给定（滑块松手走这条：手已离开，意图明确，不等去抖）。

        [没有「下发」按钮了] 见构造处说明：用户 2026-11-06 要求去掉按钮、
        改值即生效。本方法现在只被 `_on_slider_released` 调用。
        """
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
        # [点动给定同样捎带在每拍心跳里] 服务端拒绝停机时的独立给定，
        # 所以不能在按下时先发一条 SET_SPEED；且服务端点动看门狗 600ms
        # 需要每 200ms 重发，每拍都得带上给定（服务端每拍先写给定再发
        # 点动命令字）。给定在按下那一刻取一次，按住期间不随输入框变动。
        self._jog_given = self._given_payload()
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
        doc = {"op": proto.OP_JOG, "rev": self._jog_reverse}
        doc.update(getattr(self, "_jog_given", None) or {})
        self.ctx.bridge.send_vfd_control(doc)

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
            # ---- 运行状态显示（含过渡态）----
            #
            # [为什么不只看 running 位] 用户 2026-11-06：「点击停止后变频器是
            # 慢慢停机的，希望正在停机这个过程状态显示出来，还有正在加速、
            # 正在减速都显示下」。这三个过渡态由**服务端**按频率差推导后
            # 写进 run_state（M512 状态字只有 1/2/3，硬件不报细分，见
            # server_py/hsfsrv/devices.py::_derive_phase）。
            #
            # 配色：过渡态用**强调蓝**（在"动"，但不是故障也不是稳定运行），
            # 稳定运行用绿、故障用红、停机用黑 —— 一眼能分出四类。
            phase = st.run_state
            transition = phase in (proto.RUN_ACCELERATING,
                                   proto.RUN_DECELERATING)
            if st.faulted:
                color = theme.CLR_FAULT
            elif transition:
                color = "#1A66D0"
            elif st.running:
                color = theme.CLR_OK
            else:
                color = "#1A1A1A"
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

        # ---- 调速输入：滑块与数字框都可改（停机=预置，运行=实时下发）----
        #
        # [为什么不再有「下发」按钮] 用户 2026-11-06：「去掉频率或转速下发，
        # 改成在启动状态下实时显示」。改值即生效，少一次点击。
        # 服务端仍拒绝**停机**时的在线给定（给定是运行参数）——所以停机时
        # 改值只预置、不发帧，启动时随命令带走（见 _on_slider_settled /
        # _on_spin_settled 的 running 分支）。
        running = bool(fresh and st.running)
        self.freq_slider.setEnabled(not reason)
        self.freq_spin.setEnabled(not reason)

        # ---- 滑块跟随变频器实际设定值 ----
        # [只在用户没在动它时同步] 正在拖/刚拖完（去抖计时器还在跑）时若被
        # 200ms 的遥测覆盖，手指还按着、滑块却跳回旧值 —— 体验是"拖不动"。
        # 判据用去抖计时器而不是 isSliderDown()：后者在离屏/部分平台不反映
        # 真实按下态（实测离屏恒 False）。
        # [停机不采纳] 停机时变频器回报的是**上次的**设定值；若拿它对冲，
        # 用户刚预置的给定会被立刻改回去（下面 _on_slider_settled 已按
        # running 决定发不发，这里同样只在运行中采纳）。
        if (fresh and running and st.has("setFreqHz")
                and not self._slider_send_timer.isActive()
                and not self._preset_dirty):
            self._adopt_set_freq(st.set_freq_hz)

        if reason:
            self._ctrl_hint.setText("控制已禁用：%s" % reason)
        elif not running:
            self._ctrl_hint.setText(
                "已停机：拖动只预置给定，启动时按此值运行（在线调速需先启动）")
        else:
            self._ctrl_hint.setText("")

    def _adopt_set_freq(self, hz):
        """把变频器回报的设定频率对到滑块/数字框上（保持当前调速口径）。

        :param hz: 变频器的设定频率（Hz）。转速模式下按额定点换算成 rpm。
        """
        if self._speed_mode_now() == self.SPEED_MODE_RPM:
            if self._rated_hz <= 0:
                return
            value = int(round(hz / self._rated_hz * self._rated_rpm))
        else:
            value = int(round(hz))
        lo, hi = self.freq_spin.minimum(), self.freq_spin.maximum()
        value = max(lo, min(hi, value))
        self._slider_syncing = True
        try:
            if self.freq_spin.value() != value:
                self.freq_spin.setValue(value)
            if self.freq_slider.value() != value:
                self.freq_slider.setValue(value)
            unit = ("rpm" if self._speed_mode_now() == self.SPEED_MODE_RPM
                    else "Hz")
            self._slider_value.setText("%d %s" % (value, unit))
        finally:
            self._slider_syncing = False


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
                "QLabel { color: #5A6572; font-size: 20px;"
                " background: transparent; }")
            value = QLabel("--")
            value.setStyleSheet(
                "QLabel { color: #1A1A1A; font-size: 45px; font-weight: bold;"
                " background: transparent; }")
            value.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
            unit = QLabel("NTU")
            unit.setStyleSheet(
                "QLabel { color: #5A6572; font-size: 20px;"
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
            "QLabel { font-size: 21px; font-weight: bold; color: #5A6572;"
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
                "QLabel { font-size: 21px; font-weight: bold; color: #5A6572;"
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
            "QLabel { font-size: 21px; font-weight: bold; color: %s;"
            " background: transparent; }"
            % (theme.CLR_OK if ok else theme.CLR_FAULT))

    def _render_turbidity(self):
        st = self.ctx.model.turbidity
        fresh = st.is_fresh()
        for idx, (name_lbl, value_lbl) in enumerate(self._turb_rows):
            if not fresh:
                value_lbl.setText("--")
                value_lbl.setStyleSheet(
                    "QLabel { color: #9E9E9E; font-size: 45px;"
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
                    "QLabel { color: #9E9E9E; font-size: 45px;"
                    " font-weight: bold; background: transparent; }")
                continue
            value_lbl.setText("%.2f" % ntu)
            warn = ntu >= self.TURBIDITY_WARN_NTU
            value_lbl.setStyleSheet(
                "QLabel { color: %s; font-size: 45px; font-weight: bold;"
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
                "QLabel { font-size: 21px; font-weight: bold; color: #5A6572;"
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
            "QLabel { font-size: 21px; font-weight: bold; color: %s;"
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
