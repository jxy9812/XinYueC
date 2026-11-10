# -*- coding: utf-8 -*-
"""监视类页面：数字量 / 模拟量 / 变频器 / 水质监测。

对齐 C 端：
    pages/HsfMonitorDigital.c     → DigitalPage
    pages/HsfMonitorAnalog.c      → AnalogPage
    pages/HsfMonitorVfd.c         → VfdPage
    pages/HsfMonitorFlowMeter.c   → WaterPage（在流量计基础上扩为
                                    流量 + 浊度×2 + 进水泵；C 端无此页）
"""

import time
from collections import deque

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QColor, QPainter, QPen
from PyQt5.QtWidgets import (QComboBox, QGridLayout, QHBoxLayout, QLabel,
                             QPushButton, QSizePolicy, QSlider, QSpinBox,
                             QStyle, QStyleOptionSlider, QVBoxLayout, QWidget)

from . import proto, theme
from .page_base import DataTable, LampRow, Page, ToggleGrid, section_title
# [import 复用] 趋势图的配色/整型收缩直接取历史折线图的同一份定义，
# 不复制一份（两处图表读起来是同一套视觉口径）。
from .pages_other import LineChartDialog, _px
# [报警文案复用] 流量计报警位 → 中文（口径与 C 端 fmAlarmText 一致），
# 首页流量计卡与本页共用同一份定义，别各写各的。
from .model import flow_alarm_text
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


# ============================================================================
#                     实时趋势图（变频器 / 水质两页共用）
# ============================================================================

def _trend_num(value):
    """Y 轴刻度文本：按量级选小数位（1440 → "1440"、48.62 → "48.6"）。

    [为什么不用 %.4g] 小量程（浊度 0.04~0.05）会变成 "0.04458" 挤满轴；
    大量程（转速 1440）又要 4 位整数。按 |v| 分档给 0/1/2 位小数，宽度可控。
    """
    v = float(value)
    a = abs(v)
    if a >= 100:
        return "%.0f" % v
    if a >= 10:
        return "%.1f" % v
    return "%.2f" % v


class TrendChart(QWidget):
    """实时趋势图：**60 点滚动窗 + 视口归属仲裁**（QPainter 自绘，零依赖）。

    [为什么不用 LineChartDialog] 历史页的 LineChartDialog 是**顶层弹窗**，
    视口是"占全程的 0..1 比例"——它假设数据集不再变化。实时趋势的数据
    逐拍增长、窗口不断右移，还需要"用户接管视口后停写 / 一键跟随"的
    仲裁状态机，塞不进那个模型；故按约定在监视页内建轻量控件。表现口径
    （配色 / 悬停 / 滚轮缩放 / 整型收缩）仍与 LineChartDialog 保持同一套：
    SERIES_COLORS 与 _px 直接 import 复用，不复制一份。

    [对齐 C 端] 参照 HsfMonitorVfd.c 区 5（VFD_TREND_POINTS=60 滚动窗、
    vfdTrendFollowWindow 右缘贴最新追点、vfdTrendViewportSlot 视口仲裁、
    VFD_TREND_CATCHUP 右缘容差）与 HsfMonitorFlowMeter.c 趋势图区：
      a) 每序列一个 deque(maxlen=60)：新遥测逐拍追加，超窗自动移除最旧；
      b) 多序列独立存点（VFD=输出频率/给定频率/输出电流；
         水质=瞬时流量/浊度1/浊度2），量纲不同**不做双 Y 轴**——
         各序列按自己视口内的 min/max 归一化到绘图区高度，
         图例里标该序列视口内 min/max（同 LineChartDialog 口径，
         图是"看趋势"不是"读精确值"）；
      c) 滚轮缩放 X 轴（指针锚点；纵轴是各序列自适应的，不被滚轮破坏）；
      d) 用户拖动/缩放后视口**接管**（停写：追点继续、窗口不再自动右移）；
         右缘推回实时点（容差 CATCHUP_MS）自动交还；「跟随」按钮/双击恢复。
         [与 C 端的一处口径差] C 端缩放后仍保持跟随（只改窗宽），本控件按
         任务口径缩放也接管——操作员"滚了一下"多半是想停在那一帧看。
      e) 悬停显示最近点的数值与时刻。

    [默认窗宽为何是 12s 而不是 C 端的 30s] C 端保 450 点（0.2s 点距 ≈
    90s）供 30s 窗用；本控件滚动窗只有 **60 点**，按 Python 客户端 200ms
    刷新 / 服务端 ~5Hz 推送算 ≈ 12s 数据。默认窗与之对齐，否则稳定态下
    窗口左端永远是一段空白（保的点不够铺满窗）。缩放上/下限仍取 C 端
    1s / 600s。

    [重绘纪律] 追点 / 缩放 / 平移只改自身状态后调 update()（仅本控件
    重绘），不增删控件、不动布局——200ms 一拍的刷新不引起整页重排。

    [页面负责"逐拍"] 控件不猜哪一拍是新的：append() 来了就收。页面用
    `st.updated_ms` 判重（同一拍只喂一次），保证 deque 里一个遥测拍一个点。
    """

    #: 滚动窗保留点数（对齐 C 端 VFD_TREND_POINTS）。
    WINDOW_POINTS = 60

    #: 视口时间窗缺省宽（毫秒；60 点 × 200ms 拍距，理由见类 docstring）。
    WIN_DEF_MS = 12000

    #: 时间窗上/下限（毫秒；对齐 C 端 VFD_TREND_WIN_MIN/MAX = 1s/600s）。
    WIN_MIN_MS = 1000
    WIN_MAX_MS = 600000

    #: 恢复跟随的右缘容差（毫秒；对齐 C 端 VFD_TREND_CATCHUP ≈ 1px）。
    CATCHUP_MS = 50

    #: 可选采样间隔（毫秒）——「实时趋势」卡上的「刷新」下拉。
    #: 200 = 每拍都记（服务端推送 ~5Hz、页面节拍 200ms，等于当前行为）；
    #: 再快没有意义（数据本来就没有更细的分辨率），再慢曲线点变稀。
    SAMPLE_CHOICES = (200, 500, 1000, 2000)

    #: 绘图区边距：上留图例行、下留 X 轴刻度、**左留 Y 轴刻度**。
    #: [2026-11-10 对齐 C 端] C 的趋势图是 XChartView + 真值轴：X 刻度渲染
    #: 成本地时钟时间、Y 轴有数值刻度。本控件原来 X 只写"6 s/8 s"、左边距
    #: 只有 14px（压根没有 Y 标签），现场读不出"几点、多少"——用户截图反馈
    #: 「x 轴要显示时间，这里 y 轴缺失」。现在左边距按 Y 标签（"1440" /
    #: "48.6" 这类 4~5 字符）留够，底部按 "HH:MM:SS" 留够。
    MARGIN_L, MARGIN_T, MARGIN_R, MARGIN_B = 62, 26, 12, 30

    GRID_COLOR = "#E3E6EB"
    TEXT_COLOR = "#5A6572"
    #: 非跟随态角标色（琥珀，区别于序列色与故障红）。
    TAKEOVER_COLOR = "#B26A00"

    #: 悬停吸附的水平容差（像素；同 LineChartDialog.PICK_TOLERANCE 量级）。
    PICK_TOLERANCE = 24

    def __init__(self, series, parent=None):
        """:param series: [(key, 显示名), ...]；key 供页面 refresh() 追点。"""
        QWidget.__init__(self, parent)
        self.setMouseTracking(True)      # 不按键也收 move（悬停读数）
        self.setMinimumHeight(160)       # 对齐 C 端 VFD_TREND_H
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        colors = LineChartDialog.SERIES_COLORS
        #: [(key, 显示名, 颜色)]；配色与历史折线图同一套。
        self._series = [(key, name, colors[i % len(colors)])
                        for i, (key, name) in enumerate(series)]
        #: key -> 颜色（悬停标记取色用，免去每次重扫序列表）。
        self._color_of = {key: color for key, _name, color in self._series}
        #: key -> deque[(t_ms, value)]；maxlen 即滚动窗（超窗自动丢最旧）。
        self._data = {key: deque(maxlen=self.WINDOW_POINTS)
                      for key, _name in series}
        #: 时间轴 0 点（第一个点的时刻；其后窗口左缘钳到这里不空滚）。
        self._origin_ms = None
        #: 视口时间窗宽（毫秒）；用户缩放改写它，跟随/恢复时生效。
        self._win_w = float(self.WIN_DEF_MS)
        #: 当前 X 视口 [lo, hi]（毫秒）；None = 还没有数据、还没写过窗。
        self._win_lo = None
        self._win_hi = None
        #: 视口归属：False=页面跟随最新数据（每拍自动右移）；True=用户接管。
        self._user_view = False
        #: 交互中的临时状态。
        self._drag_origin = None         # (x, win_lo, win_hi)
        self._hover_pos = None           # QPoint 或 None
        #: 采样间隔（毫秒；见 SAMPLE_CHOICES）。0=不节流。
        self.sample_ms = self.SAMPLE_CHOICES[0]
        #: 上一次真正收点的时刻（节拍闸用；**按数据流分开记**，见 accept_sample）。
        self._last_fed_ms = {}

    # ==================================================================
    #                        数据（追点 / 滚动窗）
    # ==================================================================

    def set_sample_ms(self, ms):
        """设采样间隔（「刷新」下拉）：只改**记录密度**，不动视口窗宽。

        [为什么不动窗宽] 第一版让窗宽 = 60×间隔（拉长采样时"窗内始终满屏
        点"），实测副作用更糟：从 200ms 切到 1s 的瞬间，窗宽从 12s 跳到
        60s，已有的点全挤在左边一小块、右边大片空白，看着像"图坏了"。
        C 端趋势窗宽是固定值（30s）、与采样无关；这里同口径——点变稀就是
        "采集慢了"的直观表现，窗宽由用户自己拖/缩决定。
        """
        try:
            ms = int(ms)
        except (TypeError, ValueError):
            return
        if ms <= 0:
            ms = self.SAMPLE_CHOICES[0]
        if ms != self.sample_ms:
            self.sample_ms = ms
            self.update()

    def accept_sample(self, t_ms, stream=""):
        """节拍闸：距上次收点不足 `sample_ms` 就丢弃（返回 False）。

        [为什么闸在页面这一层而不是 append 里] 一拍会往多条序列各追一个点
        （变频器页一次 3 条、水质页一次 2~3 条），闸放在 append 里会把同一
        拍的第 2、3 条序列一起丢掉（时间戳相同）。页面每拍先问一次，
        整拍一起收或一起丢。

        :param stream: 数据流标识（""=变频器页；水质页用 "flow"/"turb"）。
            不同流有**各自的时钟**（流量计与浊度仪独立推送），共用一个
            "上次收点时刻"会互相把对方压掉（实测：浊度每拍被流量刚收过
            的那一拍挡掉，浊度曲线几乎不长）。
        """
        if self.sample_ms <= 0:
            return True
        try:
            t = int(t_ms)
        except (TypeError, ValueError):
            return False
        last = self._last_fed_ms.get(stream, 0)
        if last and t - last < self.sample_ms:
            return False
        self._last_fed_ms[stream] = t
        return True

    def append(self, key, t_ms, value):
        """追一个遥测点；跟随态顺手把窗口右缘贴上去。

        :return: 是否真的收了点（序列不存在 / 值不可解析返回 False）。
        deque(maxlen) 负责滚动窗：第 61 个点进来，最旧的自动出窗。
        """
        buf = self._data.get(key)
        if buf is None or t_ms is None or value is None:
            return False
        try:
            t_ms = int(t_ms)
            value = float(value)
        except (TypeError, ValueError):
            return False
        buf.append((t_ms, value))
        if self._origin_ms is None:
            self._origin_ms = t_ms
        newest = self._newest_ms()
        if self._user_view:
            # 视口在用户手里：窗口不动（停写）；右缘本来就贴着实时点
            # （接管没带走多少）→ 交还跟随（C 端 CATCHUP 口径）。
            self._maybe_resume_follow(newest)
        if not self._user_view:
            self._follow_window(newest)
        self.update()                    # 只重绘本控件，不动布局
        return True

    def points(self, key):
        """某序列当前窗内全部点（tuple[(t_ms, value)]；测试与调试用）。"""
        buf = self._data.get(key)
        return tuple(buf) if buf is not None else ()

    def _newest_ms(self):
        """所有序列中最新的点时刻（空图返回 None）。"""
        newest = None
        for buf in self._data.values():
            if buf:
                t = buf[-1][0]
                newest = t if newest is None else max(newest, t)
        return newest

    def _follow_window(self, x_now):
        """跟随写窗：右缘贴最新追点，左缘＝右缘−窗宽（钳到 0 点不空滚）。

        对齐 C 端 vfdTrendFollowWindow：数据起点之前不出现空白滚动，
        钳位时保窗宽（hi = lo + win_w 可能领先 x_now）。
        """
        if x_now is None:
            return
        lo = float(x_now) - self._win_w
        if self._origin_ms is not None and lo < self._origin_ms:
            lo = float(self._origin_ms)
        self._win_lo, self._win_hi = lo, max(lo + self._win_w, float(x_now))

    # ==================================================================
    #                     视口归属（跟随 / 接管 / 恢复）
    # ==================================================================

    def is_following(self):
        """True = 跟随态（窗口随每拍右移）；False = 视口在用户手里。"""
        return not self._user_view

    def window_ms(self):
        """当前 X 视口 (lo, hi)（毫秒）；还没有数据时返回 None。"""
        if self._win_lo is None or self._win_hi is None:
            return None
        return (self._win_lo, self._win_hi)

    def follow(self):
        """「跟随」/双击：视口交还页面——窗宽回默认、右缘立即贴最新点。

        对齐 C 端 Reset 口径（m_trendWinW 回 VFD_TREND_WIN_DEF + 恢复跟随），
        并当场重写一次窗（不等下一拍，点了按钮马上看到回到实时）。
        """
        self._user_view = False
        self._win_w = float(self.WIN_DEF_MS)
        self._follow_window(self._newest_ms())
        self.update()

    def zoom_at(self, fraction, factor):
        """以时间轴 `fraction`(0..1) 为锚点缩放 `factor` 倍（滚轮/捏合共用）。

        :param factor: <1 放大（窗变窄），>1 缩小。窗宽钳在
        [WIN_MIN_MS, WIN_MAX_MS]；缩放即接管视口（停写，见类 docstring d）。
        :return: 视口是否真的变了（已到缩放极限时不动 = False）。
        """
        if self._win_lo is None or self._win_hi is None:
            return False
        if not (0.0 <= fraction <= 1.0) or factor <= 0:
            return False
        span = self._win_hi - self._win_lo
        new_span = min(float(self.WIN_MAX_MS),
                       max(float(self.WIN_MIN_MS), span * factor))
        if abs(new_span - span) < 1e-6:
            return False
        anchor = self._win_lo + span * fraction
        lo = anchor - new_span * fraction
        hi = lo + new_span
        if self._origin_ms is not None and lo < self._origin_ms:
            # 左缘不越出时间轴 0 点（同跟随态的钳位口径）
            lo, hi = float(self._origin_ms), self._origin_ms + new_span
        self._win_lo, self._win_hi = lo, hi
        self._win_w = new_span     # 窗宽＝用户选的时间分辨率（C Zoom 口径）
        self._user_view = True     # 接管：追点继续、窗口暂停右移
        self.update()
        return True

    def pan_pixels(self, dx_pixels, plot_width):
        """按像素平移视口并接管（正值 = 内容右移 = 看更早的数据）。

        :return: 视口是否真的变了。右缘被推回实时点（容差内）时自动
        恢复跟随——这是"拖回最右 = 回到实时"的直接路径（C 端 CATCHUP）。
        """
        if plot_width <= 0 or self._win_lo is None or self._win_hi is None:
            return False
        return self._pan_from(self._win_lo, self._win_hi, dx_pixels,
                              plot_width)

    def _pan_from(self, origin_lo, origin_hi, dx_pixels, plot_width):
        """从拖动起点重算窗口（不逐帧累加：夹取丢的距离才对得上）。"""
        span = origin_hi - origin_lo
        delta = float(dx_pixels) / float(plot_width) * span
        lo = origin_lo - delta
        hi = lo + span
        if self._origin_ms is not None and lo < self._origin_ms:
            lo, hi = float(self._origin_ms), self._origin_ms + span
        newest = self._newest_ms()
        if newest is not None and hi > newest:
            # 右缘不越过最新点（更右边没有数据，留着就是假空白）
            hi, lo = float(newest), newest - span
            if lo < (self._origin_ms or 0):
                lo = float(self._origin_ms)
        changed = (lo, hi) != (self._win_lo, self._win_hi)
        self._win_lo, self._win_hi = lo, hi
        if changed:
            self._user_view = True            # 拖动即接管（停写）
            self._maybe_resume_follow(newest)  # 拖回实时点 → 自动交还
            self.update()
        return changed

    def _maybe_resume_follow(self, newest):
        """视口右缘已追到实时点（容差内）→ 交还页面自动跟随。"""
        if (self._user_view and newest is not None
                and self._win_hi is not None
                and self._win_hi >= newest - self.CATCHUP_MS):
            self._user_view = False

    # ==================================================================
    #                        几何 / 悬停
    # ==================================================================

    def plot_rect(self):
        """绘图区 (l, t, r, b)。"""
        return (self.MARGIN_L, self.MARGIN_T,
                self.width() - self.MARGIN_R, self.height() - self.MARGIN_B)

    def _x_at(self, t_ms):
        """时刻 → 屏幕 x（按当前视口线性映射）。"""
        l, _t, r, _b = self.plot_rect()
        window = self.window_ms()
        if window is None or window[1] <= window[0]:
            return (l + r) / 2.0
        lo, hi = window
        return l + (r - l) * (t_ms - lo) / float(hi - lo)

    def _visible_scale(self, points):
        """该序列在当前视口内的 (vmin, vmax)，含 8% 上下留白。

        单值序列给最小跨度兜底（对齐 C 端"数据全平给最小跨度"），
        视口内没有点返回 None（这条线这一段本来就没有数据）。
        """
        window = self.window_ms()
        if window is None:
            return None
        lo, hi = window
        vals = [v for t, v in points if lo <= t <= hi]
        if not vals:
            return None
        vmin, vmax = min(vals), max(vals)
        if vmax <= vmin:
            vmax = vmin + 1.0
        pad = (vmax - vmin) * 0.08
        return vmin - pad, vmax + pad

    def hover_at(self, pos):
        """屏幕坐标 → 最近的数据点 ``(key, 显示名, t_ms, value)`` 或 None。

        只在视口内、横向距离 `PICK_TOLERANCE` 内的点里挑最近（纵向不设限，
        触摸屏上不用瞄得准；纵向距离只作同距时的次序）。
        """
        l, t, r, b = self.plot_rect()
        if not (l - self.PICK_TOLERANCE <= pos.x() <= r + self.PICK_TOLERANCE
                and t - 16 <= pos.y() <= b + 16):
            return None
        window = self.window_ms()
        if window is None:
            return None
        lo, hi = window
        best = None
        for key, name, _color in self._series:
            scale = self._visible_scale(self._data[key])
            if scale is None:
                continue
            vmin, vmax = scale
            for t_ms, value in self._data[key]:
                if not (lo <= t_ms <= hi):
                    continue
                px = self._x_at(t_ms)
                dist = abs(px - pos.x())
                if dist > self.PICK_TOLERANCE:
                    continue
                py = b - (b - t) * (value - vmin) / ((vmax - vmin) or 1.0)
                score = (dist, abs(py - pos.y()))
                if best is None or score < best[0]:
                    best = (score, key, name, t_ms, value)
        return None if best is None else (best[1], best[2], best[3], best[4])

    def hover_text(self, pos):
        """悬停点的「名称 数值 @ 时刻」一行文本（独立成方法以便断言）。

        [为什么给文本方法] 离屏平台不绘制文字（画了也是全白），悬停内容
        对不对只能靠这个纯函数断言（同 LineChartDialog.tooltip_lines）。
        """
        hit = self.hover_at(pos)
        if hit is None:
            return ""
        _key, name, t_ms, value = hit
        return "%s %.2f @ %s" % (name, value,
                                 time.strftime("%H:%M:%S",
                                               time.localtime(t_ms / 1000.0)))

    # ==================================================================
    #                            绘制
    # ==================================================================

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        l, t, r, b = self.plot_rect()
        plot_w, plot_h = r - l, b - t
        if plot_w <= 40 or plot_h <= 40:
            return
        window = self.window_ms()

        # ---- 横向网格（4 分）----
        painter.setPen(QPen(QColor(self.GRID_COLOR), 1))
        for i in range(1, 4):
            y = t + plot_h * i / 4.0
            painter.drawLine(_px(l), _px(y), _px(r), _px(y))

        if window is None:
            painter.setPen(QColor(self.TEXT_COLOR))
            painter.drawText(self.rect(), Qt.AlignCenter, "等待数据…")
            return
        lo, hi = window
        span = (hi - lo) or 1.0

        # ---- X 刻度：6 格，标签 = **本地时钟时间**（对齐 C 端 2026-11-10
        # 口径 vfdTrendXLabelText / fmTrendXLabelText：轴值是相对建页时刻的
        # 毫秒，标签按本地时间渲染成 时:分:秒 —— 图上直接读得出几点几分，
        # 不再是 "6 s / 8 s"）。----
        font = painter.font()
        font.setPixelSize(11)
        painter.setFont(font)
        for i in range(6):
            frac = i / 5.0
            x = l + plot_w * frac
            painter.setPen(QPen(QColor(self.GRID_COLOR), 1))
            painter.drawLine(_px(x), _px(t), _px(x), _px(b))
            painter.setPen(QColor(self.TEXT_COLOR))
            stamp = (lo + span * frac) / 1000.0
            label = time.strftime("%H:%M:%S", time.localtime(stamp))
            painter.drawText(_px(x) - 34, _px(b) + 6, 68, 14,
                             Qt.AlignCenter, label)

        # ---- Y 轴刻度：按**第一个序列**的视口量程画（对齐 C 端单序列值轴
        # 的可读数口径）。[为什么按首序列] 本控件多序列、量纲不同（Hz/A /
        # m³/h/NTU），各自归一化到绘图区高度是既定口径（见类 docstring b）；
        # 画一条统一刻度必然与其它线不对应。首序列是各页的"主量"（变频器
        # 页=输出频率、水质页=瞬时流量），Y 轴给出它的真实量程，其余线
        # 的形状看趋势、数值看图例（图例逐条标了视口内 min/max）。----
        if self._series:
            first_key = self._series[0][0]
            first_name = self._series[0][1]
            scale = self._visible_scale(self._data[first_key])
            if scale is not None:
                vmin, vmax = scale
                for i in range(5):
                    frac = i / 4.0            # 0=底 1=顶
                    y = b - plot_h * frac
                    painter.setPen(QColor(self.TEXT_COLOR))
                    painter.drawText(_px(l) - 58, _px(y) - 7, 54, 14,
                                     Qt.AlignRight | Qt.AlignVCenter,
                                     _trend_num(vmin + (vmax - vmin) * frac))
                # [不画 Y 轴标题] 图例行已经写着首序列名与单位
                # （"输出频率 Hz 0~0"）；再在左上角画一次会与图例文字重叠
                # （实测第一版就是这样糊在一起）。

        # ---- 各序列折线（各自归一化，见类 docstring b）----
        for key, _name, color in self._series:
            self._paint_series(painter, key, color, l, t, r, b)

        # ---- 图例（名 + 视口内 min/max）与接管角标 ----
        self._paint_legend(painter, l, t - 16)
        if not self.is_following():
            painter.setPen(QColor(self.TAKEOVER_COLOR))
            painter.drawText(_px(r) - 250, _px(t) - 16, 250, 14,
                             Qt.AlignRight | Qt.AlignVCenter,
                             "已暂停跟随（双击或点「跟随」恢复）")

        # ---- 悬停气泡 ----
        if self._hover_pos is not None and self._drag_origin is None:
            hit = self.hover_at(self._hover_pos)
            if hit is not None:
                self._paint_hover(painter, hit)

    def _paint_series(self, painter, key, color, l, t, r, b):
        """画一条序列：视口裁剪（两侧各多取一点防断线）+ 点少时画圆点。"""
        pts = self._data[key]
        scale = self._visible_scale(pts)
        if scale is None:
            return
        vmin, vmax = scale
        span_v = (vmax - vmin) or 1.0
        lo, hi = self.window_ms()
        plot_w, plot_h = r - l, b - t

        def px_of(t_ms):
            return l + plot_w * (t_ms - lo) / float(hi - lo)

        def py_of(value):
            return b - plot_h * (value - vmin) / span_v

        visible = []
        for i, (t_ms, value) in enumerate(pts):
            if lo <= t_ms <= hi:
                visible.append((t_ms, value))
            elif t_ms < lo:
                if i + 1 < len(pts) and pts[i + 1][0] >= lo:
                    visible.append((t_ms, value))
            else:
                if visible:
                    visible.append((t_ms, value))
                break

        painter.setPen(QPen(QColor(color), 2))
        for i in range(len(visible) - 1):
            painter.drawLine(
                _px(px_of(visible[i][0])), _px(py_of(visible[i][1])),
                _px(px_of(visible[i + 1][0])), _px(py_of(visible[i + 1][1])))
        if len(visible) <= 160:          # 60 点窗内恒画圆点，便于对读
            painter.setBrush(QColor(color))
            painter.setPen(Qt.NoPen)
            for t_ms, value in visible:
                painter.drawEllipse(_px(px_of(t_ms) - 3),
                                    _px(py_of(value) - 3), 6, 6)

    def _paint_legend(self, painter, x, y):
        """图例：色块 + 名 + 视口内 min/max（量纲不同各标各的，防误读）。"""
        window = self.window_ms()
        font = painter.font()
        font.setPixelSize(11)
        painter.setFont(font)
        _l, _t, r, _b = self.plot_rect()
        cursor = x
        for key, name, color in self._series:
            label = name
            if window is not None:
                lo, hi = window
                vals = [v for t, v in self._data[key] if lo <= t <= hi]
                if vals:
                    label = "%s %.4g~%.4g" % (name, min(vals), max(vals))
            width = painter.fontMetrics().width(label) + 26
            if cursor + width > r:
                break                    # 放不下就截断（不换行，省高度）
            painter.setBrush(QColor(color))
            painter.setPen(Qt.NoPen)
            painter.drawRect(_px(cursor), _px(y + 4), 12, 4)
            painter.setPen(QColor(self.TEXT_COLOR))
            painter.drawText(_px(cursor + 16), _px(y), width - 16, 14,
                             Qt.AlignLeft | Qt.AlignVCenter, label)
            cursor += width
        painter.setBrush(Qt.NoBrush)

    def _paint_hover(self, painter, hit):
        """悬停点：竖虚线 + 圆点标记 + 「数值 / 时刻」气泡。"""
        key, name, t_ms, value = hit
        l, t, r, b = self.plot_rect()
        x = self._x_at(t_ms)
        scale = self._visible_scale(self._data[key])
        if scale is None:
            return
        vmin, vmax = scale
        color = self._color_of[key]
        y = b - (b - t) * (value - vmin) / ((vmax - vmin) or 1.0)

        painter.setPen(QPen(QColor(color), 1, Qt.DashLine))
        painter.drawLine(_px(x), _px(t), _px(x), _px(b))
        painter.setBrush(QColor(color))
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(_px(x) - 4, _px(y) - 4, 8, 8)

        line1 = "%s %.2f" % (name, value)          # 数值口径同各页读数 %.2f
        line2 = time.strftime("%H:%M:%S", time.localtime(t_ms / 1000.0))
        font = painter.font()
        font.setPixelSize(12)
        painter.setFont(font)
        fm = painter.fontMetrics()
        w = max(fm.width(line1), fm.width(line2)) + 16
        h = fm.height() * 2 + 10
        # 气泡默认在点右上；右边/上边放不下翻另一侧（不出绘图区）
        bx = x + 12
        if bx + w > r:
            bx = x - 12 - w
        by = y - h - 10
        if by < t:
            by = y + 12
        bx = max(l, min(bx, r - w))
        by = max(t, min(by, b - h))
        painter.setBrush(QColor(255, 255, 255, 240))
        painter.setPen(QPen(QColor(self.TEXT_COLOR), 1))
        painter.drawRoundedRect(_px(bx), _px(by), _px(w), _px(h), 5, 5)
        painter.setPen(QColor("#1A1A1A"))
        painter.drawText(_px(bx + 8), _px(by + 4), _px(w - 16), fm.height(),
                         Qt.AlignLeft | Qt.AlignVCenter, line1)
        painter.drawText(_px(bx + 8), _px(by + 6 + fm.height()),
                         _px(w - 16), fm.height(),
                         Qt.AlignLeft | Qt.AlignVCenter, line2)

    # ==================================================================
    #                          鼠标 / 滚轮
    # ==================================================================

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._drag_origin = (event.pos().x(), self._win_lo, self._win_hi)
            event.accept()
            return
        QWidget.mousePressEvent(self, event)

    def mouseMoveEvent(self, event):
        # [拖动判据同 LineChartDialog] 用"按下时记的起点"而不是 buttons()：
        # QTest/合成事件常不带按键位，按 buttons() 判会让拖动整段失效。
        if self._drag_origin is not None:
            if self._drag_origin[1] is not None:
                l, _t, r, _b = self.plot_rect()
                self._pan_from(self._drag_origin[1], self._drag_origin[2],
                               event.pos().x() - self._drag_origin[0],
                               max(1, r - l))
            event.accept()
            return
        self._hover_pos = event.pos()   # 悬停：记位置，重绘本控件出气泡
        self.update()
        event.accept()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton and self._drag_origin is not None:
            self._drag_origin = None
            event.accept()
            return
        QWidget.mouseReleaseEvent(self, event)

    def mouseDoubleClickEvent(self, event):
        """双击：视口交还页面（恢复跟随），触摸屏上"双击回实时"的手感。"""
        if event.button() == Qt.LeftButton:
            self.follow()
            event.accept()
            return
        QWidget.mouseDoubleClickEvent(self, event)

    def wheelEvent(self, event):
        """滚轮缩放 X 轴：以指针位置为锚点（上滚放大、下滚缩小）。

        1 格 = 1.25 倍（同 LineChartDialog）；纵轴是各序列自适应的，
        滚轮只动时间轴（对齐 C 端 XChartView_setWheelZoomXOnly）。
        """
        l, _t, r, _b = self.plot_rect()
        plot_w = max(1, r - l)
        frac = min(1.0, max(0.0, (event.pos().x() - l) / float(plot_w)))
        steps = event.angleDelta().y() / 120.0
        if abs(steps) < 0.01:
            event.ignore()
            return
        self.zoom_at(frac, 0.8 ** steps)   # 1 格 ≈ 1.25 倍（同 LineChartDialog）
        event.accept()

    def leaveEvent(self, _event):
        """鼠标移出：收气泡（悬停位置已无意义）。"""
        self._hover_pos = None
        self.update()


#: 趋势「刷新」间隔的本地设置键（统一走 settings 模块的 QSettings）。
TREND_SAMPLE_KEY = "trendSampleMs"


def trend_sample_ms_load(default=200):
    """读本机保存的趋势刷新间隔（毫秒）；读不到/非法 ⇒ default。"""
    from . import settings as _settings
    try:
        v = int(_settings.trend_ms())
    except Exception:                                   # noqa: BLE001
        return default
    return v if v > 0 else default


def trend_sample_ms_save(ms):
    """把趋势刷新间隔写进本机设置（失败静默）。"""
    from . import settings as _settings
    _settings.set_value(_settings.K_TREND_MS, int(ms))


def _on_trend_sample_changed(combo, chart):
    """「刷新」下拉变更：应用节奏 + 写本机设置（两页共用一份存档）。

    [为什么两页共用] 变频器页与水质页是两个 TrendChart 实例，但"我想看多密
    的曲线"是同一个使用偏好；共用一个键后，改一页另一页下次建页也跟上。
    """
    ms = combo.currentData() or TrendChart.SAMPLE_CHOICES[0]
    chart.set_sample_ms(ms)
    trend_sample_ms_save(ms)


def _trend_card(series):
    """建「实时趋势」卡：接管提示 + 「刷新」下拉 + 「跟随」按钮 + TrendChart。

    变频器 / 水质两页共用同一版式（标题行右侧「刷新」下拉 +「跟随」按钮）。
    两页各自的图实例独立，但下拉口径相同（TrendChart.SAMPLE_CHOICES）。
    :return: (card, chart, hint_label)
    """
    card = Card("实时趋势")
    row = QHBoxLayout()
    row.setSpacing(8)
    hint = QLabel("")
    hint.setStyleSheet(theme.caption_label_qss())
    row.addWidget(hint)
    row.addStretch(1)
    chart = TrendChart(series)
    # [采样间隔可调 2026-11-10 用户口径] 「采集速度好快，有可以设置的地方吗」：
    # 曲线点密度由**服务端采集周期 × 推送节拍**决定，界面上原来没有任何旋钮。
    # 这里给一个纯客户端的节流（只影响本图记录密度，不改服务端采集/推送）：
    # 默认 200ms（= 现状），可放宽到 2s 把窗口拉长看更长的趋势。
    sample = QComboBox()
    sample.setObjectName("comboTrendSample")   # 测试按名定位
    for ms in TrendChart.SAMPLE_CHOICES:
        sample.addItem("%.1f s" % (ms / 1000.0), ms)
    # [读本地存档 2026-11-10 用户问「刷新时间设置持久化吗」] 原来每次启动都
    # 回 200ms。现在按本机保存的值找档；存的值不在档位里（老版本/手改配置）
    # 就落到最近的档，避免下拉显示空档。
    saved = trend_sample_ms_load(TrendChart.SAMPLE_CHOICES[0])
    idx = sample.findData(saved)
    if idx < 0:
        idx = min(range(sample.count()),
                  key=lambda i: abs((sample.itemData(i) or 0) - saved))
    sample.setCurrentIndex(idx)
    chart.set_sample_ms(sample.itemData(idx) or 200)
    sample.setToolTip("曲线记录间隔（只影响本图密度；服务端采集/推送不变）"
                      "\n设置会保存到本机，重启后沿用")
    sample.currentIndexChanged.connect(
        lambda _i: _on_trend_sample_changed(sample, chart))
    row.addWidget(QLabel("刷新"))
    row.addWidget(sample)
    btn = QPushButton("跟随")
    btn.setObjectName("btnTrendFollow")   # 测试按名定位（Qt 惯用法）
    btn.setToolTip("拖动/滚轮缩放会接管视口；点此恢复自动跟随最新数据")
    btn.clicked.connect(chart.follow)
    row.addWidget(btn)
    card.body.addLayout(row)
    card.add(chart)
    return card, chart, hint


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

    #: 趋势序列（key → refresh() 取数）：输出频率/给定频率/输出电流。
    #: 对齐 C 端区 5 趋势图（HsfMonitorVfd.c m_trendSeries 输出频率；
    #: 这里按任务口径扩成三条数值序列，同一 60 点滚动窗）。
    TREND_SERIES = (
        ("out", "输出频率 Hz"),
        ("set", "给定频率 Hz"),
        ("cur", "输出电流 A"),
    )

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
        # [运行中换向 2026-11-06] 停机＝预选（随下次启动下发，原口径）；
        # 运行中改选＝立即换向（见 _on_dir_changed）。必须在 addItem 之后
        # 连接——建页的初始预选不发命令。
        self.dir_combo.currentIndexChanged.connect(self._on_dir_changed)
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

        # ---- 区 5：实时趋势（60 点滚动窗；对齐 C 端 HsfMonitorVfd.c）----
        # [竖向多余空间只给趋势图] 对齐 C 端 2026-11-08 口径：页面尾部不再
        # 放 addStretch，趋势卡吃 stretch=1（图表纵向 Expanding），多出来
        # 的高度全归图表，其余行按自身高度排布。
        trend_card, self._trend, self._trend_hint = \
            _trend_card(self.TREND_SERIES)
        self.root.addWidget(trend_card, 1)

        #: 上一拍遥测时刻（毫秒）：同一拍只追一次点（refresh() 200ms 一拍，
        #: 遥测可能慢于它——重复喂会把 60 点窗灌满同值重复点）。
        self._trend_last_ms = 0

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

    def _on_dir_changed(self, _index):
        """方向下拉切换：停机＝仅预选；运行中＝立即换向。

        [2026-11-06 运行中换向] 服务端虚拟变频器已支持"运行中收到反向命令
        字＝先按减速时间降到 0、过零翻向、再按加速时间反向加速"（C/Python
        模型同口径），故运行中改选立即发 OP_START(新方向，捎带当前给定)，
        不再等下次启动；横幅随遥测显示"减速中 → 加速中 → 反转运行"。
        [同向且稳态才跳过] 快照方向在新方向过零前不变（状态字那时才翻），
        故换向途中翻回原方向（方向==快照但处于加/减速过渡态）也必须下发
        ——服务端按"同向命令"取消待决换向、拉回原方向。稳态同向才是真冗
        余，跳过（对齐 C 客户端 vfdDirChangeSlot 口径）。
        """
        if not self._is_running():
            return
        reverse = bool(self.dir_combo.currentData())
        st = self.ctx.model.vfd
        steady_same = (reverse == bool(st.reverse)) and st.run_state in (
            proto.RUN_RUNNING_FORWARD, proto.RUN_RUNNING_REVERSE)
        if steady_same:
            return
        payload = self._given_payload()
        # [预置值在此刻兑现] 与启动同款：捎带后清标记，遥测采纳恢复正常。
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

        # ---- 趋势追点（60 点滚动窗；缩放/平移不打断追点，对齐 C 端）----
        # [只在数据新鲜时追] 过期数据画进趋势图会伪造"设备还在报数"。
        # [同一拍只追一次] updated_ms 判重：refresh() 200ms 一拍，遥测拍
        # 之间可能隔着多次刷新，重复喂会把 60 点窗灌满同值重复点。
        # [getattr 兜底] updated_ms 是 VfdState 的固定字段；取不到的桩
        # （离屏测试的轻量替身）视为"没有拍"，不追点。
        t_ms = int(getattr(st, "updated_ms", 0) or 0)
        if (fresh and t_ms and t_ms != self._trend_last_ms
                and self._trend.accept_sample(t_ms)):
            self._trend_last_ms = t_ms
            if st.has("outputFreqHz"):
                self._trend.append("out", t_ms, st.output_freq_hz)
            if st.has("setFreqHz"):
                self._trend.append("set", t_ms, st.set_freq_hz)
            if st.has("outputCurrentA"):
                self._trend.append("cur", t_ms, st.output_current_a)
        # 视口归属提示：接管后追点继续、窗口暂停右移，必须说清楚。
        self._trend_hint.setText(
            "" if self._trend.is_following()
            else "视口已接管：追点继续、窗口暂停右移")

        # 启停按钮跟随运行状态（运行中显示"停止"、停止时显示"启动"）。
        self._sync_run_button(fresh and st.running)

        reason = self.ctx.control_blocked_reason()
        if not reason and self._device_ready is False:
            reason = getattr(self, "_not_ready_reason", "") or "设备未就绪"
        for btn in self._control_buttons:
            btn.setEnabled(not reason)
        # [自由停车只在运行中可点 2026-11-08 用户口径] 停机态没有输出可断，
        # 按钮应置灰；与 C 客户端 HsfMonitorVfd.c 同口径（两端一致）。
        self.btn_free.setEnabled(not reason and bool(fresh and st.running))

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
        # [运行中禁点动 2026-11-06] 电机运行中点动会瞬间换向，物理上不成立
        # ⇒ 点动只在停机态可用（与服务端闸同口径双保险，C 客户端同批对齐）。
        # 例外：点动按住中（_jog_active）不更新——Qt 在禁用按下态按钮时会
        # 清按下态并发 released ⇒ 心跳停 + op101 ⇒ 点动一按就被掐。
        self.btn_jog.setEnabled(not reason and
                                (not running or self._jog_active))

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

    #: 趋势序列：瞬时流量 + 两路浊度（对齐 C 端 HsfMonitorFlowMeter.c
    #: 趋势图——C 端只画瞬时流量一条，这里按任务口径把浊度×2 一并画上，
    #: 三条序列同一 60 点滚动窗、各自归一化）。
    TREND_SERIES = (
        ("flow", "瞬时流量"),
        ("turb1", "浊度1 NTU"),
        ("turb2", "浊度2 NTU"),
    )

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
        self._turb_temps = []         # [temp_label]（水温；只有带温度的机型有）
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
            # [水温 2026-11-10] 带温度的机型（站3 ZHEYI-5100ZDY）一帧
            # 0x0000×4 同刻读出浊度+温度，温度跟浊度并列显示；站1
            # WZT-701A 无温度 ⇒ 该标签不显示（不是显示 0℃）。
            temp = QLabel("")
            temp.setStyleSheet(
                "QLabel { color: #5A6572; font-size: 20px;"
                " background: transparent; padding-bottom: 8px; }")
            temp.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
            temp.setVisible(False)
            row.addWidget(name)
            row.addSpacing(6)
            row.addWidget(value)
            row.addWidget(unit)
            row.addSpacing(14)
            row.addWidget(temp)
            row.addStretch(1)
            turb_card.body.addLayout(row)
            self._turb_rows.append((name, value))
            self._turb_units.append(unit)
            self._turb_temps.append(temp)

        self._turb_note = QLabel("")
        self._turb_note.setWordWrap(True)
        self._turb_note.setStyleSheet(theme.caption_label_qss())
        self._turb_note.setVisible(False)
        turb_card.add(self._turb_note)

        # [用说明行填掉下方留白] 两块卡在同一横排会被拉成等高，浊度侧只有
        # 两行读数，不补内容就在下面留一大片空白。这里顺便把"共用 485 口、
        # 站号 1/2（站 2 表后续会换，站号在服务端 config 可改）"写清楚——
        # 现场查线最常问的就是这个。
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

        # ---- 实时趋势（瞬时流量 + 浊度×2；对齐 C 端趋势图区）----
        # [吃尾部余量] 对齐 C 端 2026-11-07 口径：不再尾部 addStretch 把
        # 余量沉到图下方留白，趋势卡吃 stretch=1 把多余高度全归图表。
        trend_card, self._trend, self._trend_hint = \
            _trend_card(self.TREND_SERIES)
        self.root.addWidget(trend_card, 1)
        #: 流量计 / 浊度上一拍时刻（两路独立推送，各自判重，见 _trend_feed）。
        self._trend_last_flow_ms = 0
        self._trend_last_turb_ms = 0

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
        self._trend_feed()
        self._trend_hint.setText(
            "" if self._trend.is_following()
            else "视口已接管：追点继续、窗口暂停右移")

    def _trend_feed(self):
        """把本拍流量/浊度喂给趋势图（两路设备独立推送，各自判重追点）。

        [无数据不追] 流量计不新鲜、浊度离线（ntu=None）时不追点：趋势图
        是"实测曲线"，编造平点比缺口更危险。
        [同一拍只追一次] refresh() 200ms 一拍、遥测可能慢于它；按
        updated_ms 判重，同一拍重复喂会把 60 点窗灌满同值重复点。
        [按侧择优] 取「优先侧 + 兜底侧」挑中的那一侧快照（model.pick()），
        另一侧的空帧（没装设备）既不顶替读数、也不当成新点喂趋势。
        """
        fm = self.ctx.model.flowmeter.pick()
        if fm is not None:
            fm_ms = int(fm.updated_ms or 0)
            if (fm_ms and fm_ms != self._trend_last_flow_ms
                    and self._trend.accept_sample(fm_ms, "flow")):
                self._trend_last_flow_ms = fm_ms
                self._trend.append("flow", fm_ms, fm.flow)
        tb = self.ctx.model.turbidity.pick()
        if tb is not None:
            tb_ms = int(tb.updated_ms or 0)
            if (tb_ms and tb_ms != self._trend_last_turb_ms
                    and self._trend.accept_sample(tb_ms, "turb")):
                self._trend_last_turb_ms = tb_ms
                for key, idx in (("turb1", 0), ("turb2", 1)):
                    ntu = tb.ntu_of(idx)
                    if ntu is not None:
                        self._trend.append(key, tb_ms, ntu)

    def _render_flow(self):
        # [按侧择优] 服务端按两条侧轮流推同一主题；没装流量计的那一侧推的
        # 是 valid=false 的空帧 ⇒ 只留最后一帧时整页恒"无数据"。口径对齐
        # C 端 hsfHomePickFlowMeter（优先 B 侧）。
        st = self.ctx.model.flowmeter.pick()
        if st is None:
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
        # [报警码直接说中文 2026-11-10] 操作员看不懂 "0x0004"；说明书里写
        # 的是「空管报警」。口径照 C 端 fmAlarmText：文案 + "报警: " 前缀。
        self._flow_alarm.setText("正常" if ok
                                 else "报警: %s" % flow_alarm_text(st.alarms))
        self._flow_alarm.setStyleSheet(
            "QLabel { font-size: 21px; font-weight: bold; color: %s;"
            " background: transparent; }"
            % (theme.CLR_OK if ok else theme.CLR_FAULT))

    def _render_turbidity(self):
        # [按侧择优] 两台浊度仪在同一侧；另一侧的"两台都离线"帧不能顶替
        # 真读数（model.pick()）。
        st = self.ctx.model.turbidity.pick()
        for idx, (name_lbl, value_lbl) in enumerate(self._turb_rows):
            temp_lbl = self._turb_temps[idx]
            if st is None:
                value_lbl.setText("--")
                value_lbl.setStyleSheet(
                    "QLabel { color: #9E9E9E; font-size: 45px;"
                    " font-weight: bold; background: transparent; }")
                temp_lbl.setVisible(False)
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
                temp_lbl.setVisible(False)
                continue
            value_lbl.setText("%.2f" % ntu)
            warn = ntu >= self.TURBIDITY_WARN_NTU
            value_lbl.setStyleSheet(
                "QLabel { color: %s; font-size: 45px; font-weight: bold;"
                " background: transparent; }"
                % (theme.CLR_FAULT if warn else "#1A1A1A"))
            # 水温：缺值 = 这台表没有温度 ⇒ 不显示（不写 0℃）
            temp = st.temp_of(idx)
            if temp is None:
                temp_lbl.setVisible(False)
            else:
                temp_lbl.setText("水温 %.2f ℃" % temp)
                temp_lbl.setVisible(True)
        if st is None:
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
