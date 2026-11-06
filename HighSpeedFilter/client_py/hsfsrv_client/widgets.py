# -*- coding: utf-8 -*-
"""通用 UI 组件：卡片 / 标题栏 / 状态栏 / 数值块 / 导航面板 / 分割主窗。

[C 端对应] 逐一对齐：
    HsfTitleBar   → TitleBar   （深蓝底 40px，☰ + 居中标题 + 模式/健康/时钟/温湿度）
    HsfStatusBar  → StatusBar  （黑底 28px，左状态右连接信息）
    HsfNavPanel   → NavPanel   （左侧停靠树，可拖宽）
    HsfDataBar    → 本文件的 StatBlock（数值块）
"""

from PyQt5.QtCore import QDateTime, QSize, Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import (QFrame, QHBoxLayout, QLabel, QPushButton,
                             QSizePolicy, QSplitter, QVBoxLayout, QWidget)

from . import theme


# ============================================================================
#                              卡片容器
# ============================================================================

class Card(QFrame):
    """白底软描边卡片（C 端 HsfCard 同款）。

    用法：`card = Card("变频器状态")`，内容塞进 `card.body` 布局。
    """

    def __init__(self, title="", parent=None):
        QFrame.__init__(self, parent)
        self.setObjectName("HsfCard")
        self.setStyleSheet(theme.card_qss())
        self._lay = QVBoxLayout(self)
        self._lay.setContentsMargins(14, 10, 14, 12)
        self._lay.setSpacing(8)
        self.title_label = None
        if title:
            self.title_label = QLabel(title)
            self.title_label.setStyleSheet(
                "QLabel { color: #1F4E79; font-weight: bold; font-size: 21px;"
                " background: transparent; }")
            self._lay.addWidget(self.title_label)
        #: 内容区（调用方自行往里加控件）。
        self.body = QVBoxLayout()
        self.body.setSpacing(6)
        self._lay.addLayout(self.body)

    def add(self, widget):
        self.body.addWidget(widget)
        return widget


class StatBlock(QWidget):
    """数值块：上方灰色标题 + 下方大字号读数 + 可选单位。

    C 端 HsfDataBar 的口径：监视页成排的「标签 + 值」格子。
    """

    def __init__(self, caption, unit="", value="--", color="#1A1A1A",
                 value_size=20, parent=None):
        QWidget.__init__(self, parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 2, 0, 2)
        lay.setSpacing(2)
        self._caption = QLabel(caption)
        self._caption.setStyleSheet(theme.caption_label_qss())
        lay.addWidget(self._caption)
        row = QHBoxLayout()
        row.setSpacing(3)
        self._value = QLabel(value)
        self._value.setStyleSheet(theme.value_label_qss(color, value_size))
        # [基线对齐] 数值与单位都用 AlignBottom，单位才不会悬在数值中腰；
        # 再给单位一点下边距，视觉上贴住数值基线（截图里 23.7 与 ℃ 错位）。
        self._value.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
        row.addWidget(self._value)
        self._unit = QLabel(unit)
        self._unit.setStyleSheet(
            "QLabel { color: #5A6572; font-size: 18px; background: transparent;"
            " padding-bottom: 4px; }")
        self._unit.setAlignment(Qt.AlignBottom | Qt.AlignLeft)
        row.addWidget(self._unit)
        row.addStretch(1)
        lay.addLayout(row)

    def set_value(self, text, color=None):
        self._value.setText(str(text))
        if color is not None:
            size = self._value.font().pointSize()
            self._value.setStyleSheet(
                "QLabel { color: %s; font-size: 30px; font-weight: bold;"
                " background: transparent; }" % color)

    def set_suffix(self, unit):
        self._unit.setText(unit)


# ============================================================================
#                              标题栏
# ============================================================================

class TitleBar(QWidget):
    """顶部标题栏：☰ + 居中标题 + 运行模式钮 + 健康 + 时钟 + 温湿度。

    [布局与 C 端一致] 标题在 ☰ 区与右侧块之间**居中**（不是紧贴左边），
    右侧从右往左依次是 湿度 / 温度 / 时钟 / 健康 / 模式钮。
    窄栏（< 700px）时钟只显示时分秒，日期行隐藏。
    """

    menuClicked = pyqtSignal()
    modeClicked = pyqtSignal()

    #: 窄栏阈值（C 端 m_narrowTime 的触发口径）。
    NARROW_W = 700

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self.setObjectName("HsfTitleBar")
        self.setFixedHeight(theme.TITLE_BAR_H)
        # [必须用 ID 选择器] 裸 `QWidget { background-color: ... }` 会级联到
        # 所有子控件（把按钮/标签底色也刷成深蓝）。用 #HsfTitleBar 精确命中。
        self.setStyleSheet(theme.title_bar_qss())
        # 纯 QWidget 默认不画样式表里的背景，必须显式开启。
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 10, 0)
        lay.setSpacing(10)

        # ---- ☰ 菜单 ----
        self.menu_btn = QPushButton("\u2630")     # ☰
        self.menu_btn.setFixedSize(46, theme.TITLE_BAR_H)
        self.menu_btn.setCursor(Qt.PointingHandCursor)
        self.menu_btn.setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " color: #FFFFFF; font-size: 30px; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.12); }")
        self.menu_btn.clicked.connect(self.menuClicked.emit)
        lay.addWidget(self.menu_btn)

        # ---- 时钟（紧跟菜单图标右侧，**左对齐**）----
        #
        # [为什么放左边] 原来挤在最右侧，与温度/湿度/健康/模式钮抢地方：
        # 窗口一窄就被裁掉，而且"看时间"是最高频动作，放左边一眼就看到。
        # 放左后右侧那组（模式/健康/温湿度）各自靠右排，互不挤压。
        clock_box = QWidget()
        clock_box.setStyleSheet("background: transparent;")
        cb = QVBoxLayout(clock_box)
        cb.setContentsMargins(0, 2, 0, 2)
        cb.setSpacing(0)
        self.date_label = QLabel("")
        self.date_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self.date_label.setStyleSheet(
            "QLabel { color: #C5D8E8; font-size: 20px; background: transparent; }")
        cb.addWidget(self.date_label)
        self.clock_label = QLabel("")
        self.clock_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self.clock_label.setStyleSheet(
            "QLabel { color: #FFFFFF; font-size: 20px; background: transparent; }")
        cb.addWidget(self.clock_label)
        self._clock_box = clock_box
        # 定宽：秒数变化时不让它左右抖动（否则标题会跟着晃）
        clock_box.setMinimumWidth(96)
        lay.addWidget(clock_box)

        # ---- 居中标题 ----
        self.title = QLabel("主界面")
        self.title.setStyleSheet(
            "QLabel { color: #FFFFFF; font-size: 24px; font-weight: bold;"
            " background: transparent; }")
        self.title.setAlignment(Qt.AlignCenter)
        lay.addWidget(self.title, 1)

        # ---- 运行模式钮（手动/自动，可点切换） ----
        self.mode_btn = QPushButton("手动")
        self.mode_btn.setFixedHeight(24)
        self.mode_btn.setMinimumWidth(56)
        self.mode_btn.setCursor(Qt.PointingHandCursor)
        self.mode_btn.setToolTip("点击切换运行模式（需 root）")
        self.mode_btn.clicked.connect(self.modeClicked.emit)
        self._mode_auto = False
        self._mode_known = False
        self._restyle_mode()
        lay.addWidget(self.mode_btn)

        # ---- 自动运行状态（只读回显，紧贴模式钮右侧）----
        #
        # [为什么要单独一个标签] 模式钮只说明"人把开关拨到哪一侧"。
        # 操作员真正要确认的是"自动逻辑此刻在不在跑"。两者正常同步，
        # 但控制器没起来（装配时序错/异常退出/将来做暂停）时，只看模式钮
        # 会得到"自动在跑"的错误印象 —— 那种假正常比报故障更难查。
        #
        # 三种显示：
        #   自动 · 运行中   橙底（与模式钮同色系，一眼看出是同一条信息链）
        #   自动 · 已停止   灰底（模式自动但控制器没跑 = 异常，要看得见）
        #   手动            不显示（手动模式下"自动是否运行"无意义，别占地方）
        self.auto_state = QLabel("")
        self.auto_state.setFixedHeight(22)
        self.auto_state.setAlignment(Qt.AlignCenter)
        self.auto_state.setMinimumWidth(84)
        self._auto_running = None
        self._restyle_auto_state()
        lay.addWidget(self.auto_state)

        # ---- 设备健康指示 ----
        self.health = QLabel("")
        self.health.setStyleSheet(
            "QLabel { color: #D0D0D0; background: transparent; font-size: 20px; }")
        lay.addWidget(self.health)

        # ---- 温度 / 湿度（**上下两行**）----
        # [为什么上下排] 并排时顶栏右半部分被拉得很长，时钟/模式钮/
        # 健康指示全被挤压；改成两行后每行只占一个数值宽度，顶栏更紧凑，
        # 一眼也能分清哪行是温度哪行是湿度。
        # [为什么带单位占位] 裸 "--" 在深蓝底上像两条断裂的横线，
        # 带单位后即使无数据也能读出「这是温度/湿度」。
        th_box = QWidget()
        th_box.setStyleSheet("background: transparent;")
        tb = QVBoxLayout(th_box)
        tb.setContentsMargins(0, 3, 0, 3)
        tb.setSpacing(0)
        self.temp_label = QLabel("--℃")
        self.hum_label = QLabel("--%RH")
        for lbl in (self.temp_label, self.hum_label):
            lbl.setStyleSheet(
                "QLabel { color: #FFFFFF; font-size: 18px;"
                " background: transparent; }")
            lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            lbl.setMinimumWidth(64)
            tb.addWidget(lbl)
        # [现场口径] 这对温湿度是**电箱内**的读数（变送器装在电控柜里），
        # 不是环境温度；tooltip 写清楚，避免现场误解。
        self.temp_label.setToolTip("电箱温度")
        self.hum_label.setToolTip("电箱湿度")
        lay.addWidget(th_box)

        # ---- 时钟定时器（1s，C 端同款） ----
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self.refresh_clock)
        self._timer.start()
        self.refresh_clock()

    # ---- 时钟 ----
    def refresh_clock(self):
        now = QDateTime.currentDateTime()
        self.clock_label.setText(now.toString("HH:mm:ss"))
        self.date_label.setText(now.toString("yyyy-MM-dd"))
        self.date_label.setVisible(self.width() >= self.NARROW_W)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self.date_label.setVisible(self.width() >= self.NARROW_W)

    # ---- 文本/状态 ----
    def set_title(self, text):
        self.title.setText(text)

    def set_temperature(self, celsius, valid=True):
        self.temp_label.setText("%.1f\u2103" % celsius if valid else "--")

    def set_humidity(self, percent, valid=True):
        self.hum_label.setText("%.1f%%" % percent if valid else "--")

    def set_health(self, fault_count, offline_count):
        """健康指示：全部正常时不显示（不占视觉重量）。"""
        if fault_count or offline_count:
            parts = []
            if fault_count:
                parts.append("故障%d" % fault_count)
            if offline_count:
                parts.append("离线%d" % offline_count)
            self.health.setText(" / ".join(parts))
            self.health.setStyleSheet(
                "QLabel { color: #FFB4B4; background: transparent;"
                " font-size: 20px; font-weight: bold; }")
        else:
            # 全部正常时不占位（顶栏空间宝贵，异常才提示）。
            self.health.setText("")

    def set_mode(self, auto, known=True):
        self._mode_auto = bool(auto)
        self._mode_known = bool(known)
        self._restyle_mode()
        # 模式变了，"自动运行"标签的可见性/含义也跟着变
        self._restyle_auto_state()

    def set_auto_running(self, running, known=True):
        """设置自动控制器运行状态。

        :param running: True=在跑 False=已停；known=False 时按"未知"显示。
        """
        self._auto_running = None if not known else bool(running)
        self._restyle_auto_state()

    def _restyle_auto_state(self):
        """自动运行标签的文案与配色。

        [手动模式下隐藏] 手动时"自动是否在跑"没有意义，留着只会占顶栏
        宝贵宽度（窄窗口最先被挤掉的就是这些坑位）。
        [「自动 · 已停止」是**正常默认态**，不是异常] 2026-11-05 订正：
        切到自动模式只代表"授权自动可以跑"，要真的跑还得人按一次启动。
        初版把它标成故障红，等于把正常待命说成异常，反而误导操作员。
        三种状态用颜色区分"要不要管"：橙=在跑、灰=待命/未知、红=不用。
        """
        if not self._mode_known or not self._mode_auto:
            self.auto_state.setVisible(False)
            return
        self.auto_state.setVisible(True)
        if self._auto_running is None:
            text, bg, fg = "自动 · 未知", "#5A6572", "#E0E0E0"
        elif self._auto_running:
            text, bg, fg = "自动 · 运行中", "#ED6C02", "#FFFFFF"
        else:
            # 已授权但没按启动：正常待命，中性灰（不是故障红）
            text, bg, fg = "自动 · 已停止", "#5A6572", "#E0E0E0"
        self.auto_state.setText(text)
        self.auto_state.setToolTip(
            "自动控制器的实际运行状态（不是模式开关的位置）。\n"
            "「自动 · 已停止」= 已授权自动但还没按过启动（正常待命）。\n"
            "切到自动模式只是授权，要真正自动运行需在「远端设置」按启动。")
        self.auto_state.setStyleSheet(
            "QLabel { background-color: %s; color: %s; border-radius: 3px;"
            " font-size: 18px; padding: 0 6px; }" % (bg, fg))

    def _restyle_mode(self):
        if not self._mode_known:
            text, bg, fg = "未知", "#5A6572", "#D0D0D0"
        elif self._mode_auto:
            text, bg, fg = "自动", "#ED6C02", "#FFFFFF"
        else:
            text, bg, fg = "手动", "#2E7D32", "#FFFFFF"
        self.mode_btn.setText(text)
        self.mode_btn.setStyleSheet(
            "QPushButton { background-color: %s; color: %s; border: none;"
            " border-radius: 4px; padding: 3px 10px; font-size: 18px;"
            " font-weight: bold; }"
            "QPushButton:hover { background-color: %s; }"
            % (bg, fg, theme.CLR_ACCENT_LINE))

    def set_mode_enabled(self, enabled):
        self.mode_btn.setEnabled(enabled)


# ============================================================================
#                              状态栏
# ============================================================================

class StatusBar(QWidget):
    """底部状态栏：黑底 28px，左侧状态文本 + 右侧连接信息。"""

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self.setObjectName("HsfStatusBar")
        self.setFixedHeight(theme.STATUS_BAR_H)
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setStyleSheet(theme.status_bar_qss())

        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 0, 12, 0)
        lay.setSpacing(16)

        self.message = QLabel("就绪")
        lay.addWidget(self.message, 1)

        self.conn = QLabel("未连接")
        lay.addWidget(self.conn)

        self.clock = QLabel("")
        lay.addWidget(self.clock)
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._tick)
        self._timer.start()
        self._tick()

    def _tick(self):
        self.clock.setText(QDateTime.currentDateTime().toString("HH:mm:ss"))

    def set_message(self, text):
        self.message.setText(text)

    def set_connection(self, text, ok=True):
        self.conn.setText(text)
        self.conn.setStyleSheet(
            "QLabel { color: %s; background: transparent; }"
            % ("#A8D8A8" if ok else "#FFB4B4"))


# ============================================================================
#                              导航面板
# ============================================================================

class NavPanel(QWidget):
    """左侧停靠导航：树形条目（父节点 + 缩进子节点），当前页常亮。

    与 C 端 HsfNavPanel 同源：条目来自同一棵树（pages.NAV_TREE），
    支持两级缩进、分组标题不可点、当前页高亮。
    """

    navigate = pyqtSignal(int)

    def __init__(self, tree, parent=None):
        """tree: [(caption, page_index, child_of), ...] —— -1 表示顶层。"""
        QWidget.__init__(self, parent)
        self.setObjectName("HsfNavPanel")
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setStyleSheet(theme.nav_panel_qss())
        self.setMinimumWidth(theme.NAV_MIN_W)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        title = QLabel("导航")
        title.setObjectName("HsfNavTitle")
        outer.addWidget(title)

        content = QWidget()
        content.setObjectName("HsfNavContent")
        self._lay = QVBoxLayout(content)
        self._lay.setContentsMargins(6, 4, 6, 8)
        self._lay.setSpacing(2)
        outer.addWidget(content, 1)

        self._buttons = []
        self._current = -1

        for caption, page_index, child_of in tree:
            btn = QPushButton(caption)
            btn.setObjectName("HsfNavItem")
            btn.setFixedHeight(theme.NAV_ITEM_H)
            btn.setCursor(Qt.PointingHandCursor)
            # 缩进：二级条目左移出层级感（C 端同款层级缩进口径）。
            indent = 12 if child_of >= 0 else 0
            btn.setStyleSheet(
                "QPushButton { text-align: left; padding-left: %dpx; }"
                % (12 + indent * 2))
            if page_index < 0:
                # 分组标题：不可点，只是视觉分隔。
                btn.setEnabled(False)
                btn.setStyleSheet(
                    "QPushButton#HsfNavItem { color: #8B98A5; border: none;"
                    " background: transparent; text-align: left;"
                    " padding-left: %dpx; font-size: 18px; }" % (12 + indent * 2))
            else:
                idx = page_index
                btn.clicked.connect(lambda _checked=False, i=idx:
                                    self.navigate.emit(i))
            self._lay.addWidget(btn)
            self._buttons.append((page_index, btn))

        self._lay.addStretch(1)

    def set_current(self, page_index):
        """高亮当前页条目。"""
        if self._current == page_index:
            return
        self._current = page_index
        for idx, btn in self._buttons:
            if idx < 0:
                continue
            btn.setChecked(idx == page_index)

    def set_compact(self, compact):
        """紧凑形态：条目加高（触控友好）。"""
        h = theme.NAV_ITEM_H_MOBILE if compact else theme.NAV_ITEM_H
        for idx, btn in self._buttons:
            btn.setFixedHeight(h)


# ============================================================================
#                              主窗三段式骨架
# ============================================================================

class ChromeFrame(QWidget):
    """三段式主窗骨架：顶部标题栏 + 中部（导航 | 堆栈）+ 底部状态栏。

    中部用 QSplitter 承载「导航面板 | 页面堆栈」，与 C 端
    XSplitter 的口径一致（拖拽把手宽 8px，可改导航宽）。
    """

    def __init__(self, title, nav_tree, parent=None):
        QWidget.__init__(self, parent)
        # [为什么要 objectName + ID 选择器] 用无 ID 的 `background-color`
        # 会**级联到所有子控件**（Qt 样式表按控件树向下继承），把标题栏、
        # 导航、卡片的底色全刷成页面灰——之前截图里顶栏发灰就是这个原因。
        self.setObjectName("HsfChrome")
        self.setStyleSheet(
            "QWidget#HsfChrome { background-color: %s; }" % theme.COLOR_PAGE_BG)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.title_bar = TitleBar()
        outer.addWidget(self.title_bar)
        self.title_bar.set_title(title)

        self.splitter = QSplitter(Qt.Horizontal)
        self.splitter.setHandleWidth(theme.SPLITTER_HANDLE_W)
        self.splitter.setChildrenCollapsible(False)

        self.nav = NavPanel(nav_tree)
        self.splitter.addWidget(self.nav)

        self.stack_host = QWidget()
        self.stack_host.setObjectName("HsfStackHost")
        self.stack_host.setStyleSheet(
            "QWidget#HsfStackHost { background-color: %s; }"
            % theme.COLOR_PAGE_BG)
        self.stack_layout = QVBoxLayout(self.stack_host)
        self.stack_layout.setContentsMargins(10, 10, 10, 10)
        self.stack_layout.setSpacing(0)
        self.splitter.addWidget(self.stack_host)

        self.splitter.setStretchFactor(0, 0)
        self.splitter.setStretchFactor(1, 1)
        self.splitter.setSizes([theme.NAV_DEFAULT_W, 1100])
        outer.addWidget(self.splitter, 1)

        self.status_bar = StatusBar()
        outer.addWidget(self.status_bar)

    def set_page_widget(self, widget):
        """把页面控件装进内容区（旧页面由调用方删除）。"""
        while self.stack_layout.count():
            item = self.stack_layout.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
        self.stack_layout.addWidget(widget)

    def set_nav_visible(self, visible):
        self.nav.setVisible(visible)
