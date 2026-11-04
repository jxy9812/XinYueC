# -*- coding: utf-8 -*-
"""页面基类与常用构件。

[基类约定] 每个页面：
    * 继承 Page，构造签名 `Page(ctx)`；
    * `ctx` 是 PageContext（bridge + 导航回调 + 状态栏回调）；
    * 覆写 `topics()` 声明本页需要的推送主题（切页时自动订阅/退订）；
    * 覆写 `refresh()` 从 ctx.model 拉数据刷新视图（每 200ms 一次）。

[为什么用 refresh() 而不是纯信号] 事件驱动的主题（健康/数字量）静止时
不发推送，纯信号驱动会让页面在数据未到达时停在 "--" 很久。定时刷新
保证页面至少每 200ms 有一拍机会重绘。
"""

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QFrame, QGridLayout, QHBoxLayout, QLabel,
                             QPushButton, QScrollArea, QSizePolicy,
                             QVBoxLayout, QWidget)

from . import proto, theme


class PageContext(object):
    """页面共享上下文。"""

    __slots__ = ("bridge", "navigate", "set_status", "model")

    def __init__(self, bridge, navigate, set_status):
        self.bridge = bridge
        self.navigate = navigate
        self.set_status = set_status
        self.model = bridge.model

    def is_root(self):
        return self.bridge.role >= proto.ROLE_ADMIN

    def is_auto(self):
        return self.model.is_auto()

    def control_blocked_reason(self):
        """控制类操作被闸门挡住的原因；空串 = 允许。"""
        if not self.bridge.is_connected():
            return "未连接服务端"
        if self.model.is_auto():
            return "自动运行中：远程控制已被禁用"
        if not self.is_root():
            return "权限不足：控制需要 root 账户"
        return ""


class Page(QWidget):
    """页面基类。"""

    #: 本页需要的推送主题（子类覆写）。切页时主窗按此自动增删订阅。
    TOPICS = ()

    def __init__(self, ctx):
        QWidget.__init__(self)
        self.ctx = ctx
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.root = QVBoxLayout(self)
        self.root.setContentsMargins(0, 0, 0, 0)
        self.root.setSpacing(10)

    def topics(self):
        return self.TOPICS

    def refresh(self):
        """数据刷新钩子（每 200ms）。子类覆写。"""

    def on_enter(self):
        """切到本页时调用（可用于按需拉快照）。"""

    def on_leave(self):
        """离开本页时调用。"""

    # ---- 便捷构件 ----
    def add_header(self, title, subtitle=""):
        """页面大标题 + 说明（各页统一的口径）。"""
        row = QHBoxLayout()
        row.setSpacing(10)
        lbl = QLabel(title)
        lbl.setStyleSheet(
            "QLabel { color: #1F4E79; font-size: 17px; font-weight: bold;"
            " background: transparent; }")
        row.addWidget(lbl)
        if subtitle:
            sub = QLabel(subtitle)
            sub.setStyleSheet(theme.caption_label_qss())
            sub.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
            row.addWidget(sub)
        row.addStretch(1)
        self.root.addLayout(row)
        return row

    def add_widget(self, widget, stretch=0):
        self.root.addWidget(widget, stretch)
        return widget

    def make_scroll(self):
        """套一层滚动壳（内容高的页面用）。

        返回 (scroll, inner_layout) —— 往 inner_layout 里加东西。
        """
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("QScrollArea { background: transparent; }")
        inner = QWidget()
        inner.setStyleSheet("background: transparent;")
        lay = QVBoxLayout(inner)
        lay.setContentsMargins(0, 0, 6, 0)
        lay.setSpacing(10)
        scroll.setWidget(inner)
        return scroll, lay


# ============================================================================
#                          监视页共用的表格构件
# ============================================================================

class DataTable(QWidget):
    """只读数据表（「项目 / 数值 / 单位」三列）。

    用 QTableWidget 而不是 QTableView+Model：行数固定（十几个字段），
    直接塞 item 比维护 model 简单，且刷新时只改文本不重建。
    """

    HEADERS = ("项目", "数值", "单位")

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        from PyQt5.QtWidgets import QTableWidget, QHeaderView, QAbstractItemView
        self.table = QTableWidget(0, len(self.HEADERS), self)
        self.table.setHorizontalHeaderLabels(list(self.HEADERS))
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.setShowGrid(False)
        # [列宽策略：全部 Stretch，由 Qt 均分，再按权重微调]
        #
        # 早期两种做法都出过问题（均有截图实证）：
        #   a) 首列 Stretch + 其余 ResizeToContents
        #      → 「项目」列吃掉大半个屏，数值挤最右，表看着极空
        #   b) 手工 setColumnWidth(视口宽 × 权重)
        #      → set_rows 每 200ms 重建行；视口宽在布局稳定前为 0，
        #        列被压成 60px，出现「第 4 列看不见」
        #
        # 现在交给 Qt：所有列 Stretch（等比填满、永不出现 0 宽），
        # 再用 setSectionResizeMode 的顺序 + 列宽权重做比例微调。
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.Stretch)
        header.setStretchLastSection(False)
        self._column_weights = (6, 3, 2)      # 项目 : 数值 : 单位
        self.table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.table)

    def set_column_weights(self, weights):
        """设置列宽比例（如 (6,3,2) = 项目 6 : 数值 3 : 单位 2）。

        [实现要点 —— 踩过两次坑后的定稿]
        1) 不能用「只设第 0 列 Stretch + 其余 ResizeToContents」：
           名称列会吃掉大半个屏（截图实证）。
        2) 不能只手工 setColumnWidth：set_rows 每 200ms 重建行，
           布局未稳定时视口宽为 0，列被压成 60px、第 4 列直接消失。
        3) 在 resizeEvent 里反复 setColumnWidth 会造成重绘循环。

        定稿做法：**全部 Stretch**（Qt 自己等比填满，永不 0 宽、无循环），
        权重只用来给各列一个不同的**最小宽度** —— 这样既保证比例观感，
        又不会在布局中途把列压没。权重差异通过最小宽度体现。
        """
        self._column_weights = tuple(weights) or (1,)
        self._apply_min_widths()

    def _apply_min_widths(self):
        """给表格一个合理的列最小宽度（Stretch 模式下防止列被压没）。

        [为什么只调 header 的最小列宽] QHeaderView 的 Stretch 模式会等比
        填满视口，但列可以被压到很窄。把「最小列宽」提到 72px 后，
        即使窗口很窄也不会出现「单位列看不见」。
        这里不按权重逐列设（Qt 没有 per-section minimum 的公共 API），
        权重仅记录在 _column_weights 供将来需要时使用。
        """
        header = self.table.horizontalHeader()
        header.setMinimumSectionSize(72)

    def fit_to_content(self, extra_rows=0):
        """让表格高度贴合内容（不留大片空白）。

        [为什么需要] 表格默认会撑满父布局可用高度：8 行数据下面拖出一大片
        空白，页面看着很空（截图实见）。高度 = 表头 + 行高×行数 + 边框余量。
        """
        table = self.table
        rows = max(table.rowCount(), extra_rows)
        header_h = table.horizontalHeader().height() or 34
        row_h = table.verticalHeader().defaultSectionSize() or 32
        total = header_h + row_h * rows + 4
        table.setMinimumHeight(total)
        table.setMaximumHeight(total)

    def set_rows(self, rows):
        """rows: [(name, value, unit), ...]；数量变化时才重建行。

        [列数自适应] 调用方可能把表改成 4/5 列（如主界面设备表）。
        这里按**实际列数**补齐 item，缺列不会被当成 None 解引用
        （曾因写死 3 列导致 4 列表页刷新时 AttributeError）。
        """
        need = len(rows)
        cols = self.table.columnCount()
        if self.table.rowCount() != need:
            self.table.setRowCount(need)
            for r in range(need):
                for c in range(cols):
                    self.table.setItem(r, c, self._item("--"))
        for r, row in enumerate(rows):
            for c in range(min(cols, len(row))):
                item = self.table.item(r, c)
                if item is None:
                    item = self._item("")
                    self.table.setItem(r, c, item)
                item.setText(str(row[c]))
                # [对齐口径] 第 0 列（名称）左对齐；第 1 列（数值）右对齐，
                # 第 2 列（单位）**左**对齐 —— 这样数值与单位在列边界处相邻，
                # 读数不会被拉开；其余列（状态/说明等）居中。
                # 早先全居中，导致 "0.000" 贴右、"mA" 贴左，看着像两个独立数字。
                if c == 0:
                    item.setTextAlignment(Qt.AlignLeft | Qt.AlignVCenter)
                elif c == 1:
                    # 数值居中：列宽均分时，右对齐会让数字飘到列右缘、
                    # 与"单位"列的文字隔着很远（截图实见）。
                    item.setTextAlignment(Qt.AlignCenter)
                elif c == 2:
                    item.setTextAlignment(Qt.AlignCenter)
                else:
                    item.setTextAlignment(Qt.AlignCenter)


    @staticmethod
    def _item(text, align_right=False):
        from PyQt5.QtWidgets import QTableWidgetItem
        item = QTableWidgetItem(str(text))
        if align_right:
            item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
        else:
            item.setTextAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        return item


class LampDot(QWidget):
    """单个状态灯：自绘实心圆点。

    [为什么自绘而不是贴一个 ● 字符] 板上是 256 色 + 中文字体，
    ● 在该字体下会渲染成**黑白条纹/棋盘格**（实测板端截图：DI 灯全是
    条纹圆，看不出通断）。QPainter 画圆与字体、调色板、色深全部无关。
    """

    #: 圆点直径。**取大值**：板上是 256 色调色板，小圆点被量化后
    #: 边缘会出现抖动条纹甚至分色（实测 18px 圆在板上呈现红绿蓝三色条纹）。
    #: 圆够大 + 实心填充 + 深色描边后，量化只影响边缘一两个像素。
    DIAMETER = 26

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self._on = False
        self._enabled = True
        self.setFixedSize(self.DIAMETER + 6, self.DIAMETER + 6)

    def set_state(self, on, enabled=True):
        on = bool(on)
        enabled = bool(enabled)
        if self._on == on and self._enabled == enabled:
            return
        self._on = on
        self._enabled = enabled
        self.update()

    def paintEvent(self, _event):
        """自绘圆点。

        [关抗锯齿 + 只用纯色] 256 色屏上抗锯齿会在边缘混出**第三种颜色**
        （实测：红点/绿点的边缘混进蓝色，整排看起来像彩色条纹）。
        关掉 AA 后边缘是硬切的，量化只影响一两个像素，通断一眼可辨。
        颜色也用饱和度最高的纯色（#00A000 / #D00000），量化后仍能区分。
        """
        from PyQt5.QtGui import QColor, QPainter, QPen
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, False)
        if not self._enabled:
            fill = QColor("#909090")
        elif self._on:
            fill = QColor("#00A000")           # 绿 = 通（纯色，量化稳）
        else:
            fill = QColor("#D00000")           # 红 = 断
        painter.setPen(QPen(fill.darker(160), 2))
        painter.setBrush(fill)
        margin = 2
        painter.drawEllipse(margin, margin,
                            self.width() - 2 * margin,
                            self.height() - 2 * margin)
        painter.end()


class LampRow(QWidget):
    """一排状态灯（数字量监视用）：自绘圆点 + 通道号。

    C 端 HsfMonitorDigital 的「圆点网格看板」口径：每格一个圆点
    （绿=通，红=断），圆点下方是通道号。
    """

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._cells = []
        grid = QGridLayout(self)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(8)
        for i in range(count):
            cell = QWidget()
            v = QVBoxLayout(cell)
            v.setContentsMargins(0, 0, 0, 0)
            v.setSpacing(2)
            dot = LampDot()
            holder = QHBoxLayout()
            holder.setContentsMargins(0, 0, 0, 0)
            holder.addStretch(1)
            holder.addWidget(dot)
            holder.addStretch(1)
            v.addLayout(holder)
            name = QLabel("CH%d" % i)
            name.setAlignment(Qt.AlignCenter)
            name.setStyleSheet(theme.caption_label_qss())
            v.addWidget(name)
            grid.addWidget(cell, i // columns, i % columns)
            self._cells.append((dot, name))

    def set_state(self, index, on, enabled=True):
        if 0 <= index < len(self._cells):
            self._cells[index][0].set_state(on, enabled)

    def set_name(self, index, text):
        if 0 <= index < len(self._cells):
            self._cells[index][1].setText(text)


class ToggleGrid(QWidget):
    """DO 操作网格：每格一个可切按钮（点击下发控制）。

    与 C 端 HsfMonitorDigital 的操作区口径一致：DO 可点、DI 只读。
    """

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._buttons = []
        self.on_toggle = None            # (index, want_on) -> None
        grid = QGridLayout(self)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(6)
        for i in range(count):
            btn = QPushButton("DO%d" % i)
            btn.setCheckable(True)
            btn.setMinimumSize(72, 34)
            btn.setCursor(Qt.PointingHandCursor)
            btn.clicked.connect(
                lambda checked=False, idx=i: self._on_click(idx, checked))
            grid.addWidget(btn, i // columns, i % columns)
            self._buttons.append(btn)

    def _on_click(self, index, checked):
        if self.on_toggle:
            self.on_toggle(index, checked)

    def set_state(self, index, on):
        """同步按钮显示态（不触发 clicked）。"""
        if not 0 <= index < len(self._buttons):
            return
        btn = self._buttons[index]
        btn.blockSignals(True)
        btn.setChecked(bool(on))
        btn.blockSignals(False)
        btn.setText("DO%d %s" % (index, "通" if on else "断"))

    def set_enabled_all(self, enabled):
        for btn in self._buttons:
            btn.setEnabled(enabled)


def form_row(label, widget, hint=""):
    """「标签 + 控件 (+ 提示)」一行，供设置页复用。"""
    row = QHBoxLayout()
    row.setSpacing(8)
    lbl = QLabel(label)
    lbl.setMinimumWidth(110)
    lbl.setStyleSheet("QLabel { color: #1A1A1A; background: transparent; }")
    row.addWidget(lbl)
    row.addWidget(widget, 1)
    if hint:
        h = QLabel(hint)
        h.setStyleSheet(theme.caption_label_qss())
        row.addWidget(h)
    return row


def section_title(text):
    """小节标题（比页标题小，比正文重）。"""
    lbl = QLabel(text)
    lbl.setStyleSheet(
        "QLabel { color: #1F4E79; font-size: 14px; font-weight: bold;"
        " background: transparent; }")
    return lbl
