# -*- coding: utf-8 -*-
"""主界面 / 菜单页 / 数据页 / 设置页。

对齐 C 端：
    pages/HsfHomePage.c        → HomePage
    pages/HsfMainMenu.c        → MainMenuPage
    pages/HsfMonitorMenu.c     → MonitorMenuPage（子菜单，通用 SubMenuPage）
    pages/HsfDataMenu.c        → SubMenuPage
    pages/HsfDataHistory.c     → HistoryPage
    pages/HsfDataLog.c         → LogPage
    pages/HsfDataAlarm.c       → AlarmPage
    pages/HsfSettingsMenu.c    → SubMenuPage
    pages/HsfSettingsLocal.c   → LocalSettingsPage
    pages/HsfSettingsRemote.c  → RemoteSettingsPage
    pages/HsfNetworkPage.c     → NetworkPage
    pages/HsfOverlayPage.c     → OverlayPage
    pages/HsfWindowPage.c      → WindowPage
"""

import time

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QGridLayout, QHBoxLayout,
                             QLabel, QLineEdit, QPushButton, QSpinBox,
                             QTableWidget, QTableWidgetItem, QVBoxLayout,
                             QWidget)

from . import pages_registry as reg
from . import proto, theme
from .page_base import DataTable, Page, form_row, section_title
from .widgets import Card


# ============================================================================
#                              主界面
# ============================================================================

class HomePage(Page):
    """主界面：关键指标总览 + 三大分类快捷入口。

    C 端 HsfHomePage 的口径：顶部看板（温湿度/变频器/设备健康）+
    下方快捷入口卡片。
    """

    TOPICS = (proto.TOPIC_VFD_STATUS, proto.TOPIC_TEMPHUM_STATUS,
              proto.TOPIC_AI_STATUS, proto.TOPIC_DEVICE_HEALTH)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("主界面", "高速过滤器监控系统")

        # ---- 总览卡片 ----
        cards = QHBoxLayout()
        cards.setSpacing(10)

        from .widgets import StatBlock
        # 温湿度卡 —— [现场口径] 这是**电箱内**的温度/湿度，不是环境(室外/室内)
        # 温湿度：变送器装在电控柜里，用来监视柜内温升与凝露风险。
        # 标题与字段名都要带上"电箱"，否则会被误读成现场环境温度。
        th_card = Card("电箱温度")
        self._th_grid = QGridLayout()
        self._th_grid.setSpacing(10)
        self.th_temp = StatBlock("电箱温度", "℃", "--", value_size=26)
        self.th_hum = StatBlock("电箱湿度", "%RH", "--", value_size=26)
        self._th_grid.addWidget(self.th_temp, 0, 0)
        self._th_grid.addWidget(self.th_hum, 0, 1)
        th_card.body.addLayout(self._th_grid)
        cards.addWidget(th_card, 1)

        # 变频器卡
        vfd_card = Card("搅拌泵")     # 设备是变频器，按工艺用途显示
        self._vfd_grid = QGridLayout()
        self._vfd_grid.setSpacing(10)
        self.vfd_state = StatBlock("运行状态", "", "--", value_size=20)
        self.vfd_freq = StatBlock("输出频率", "Hz", "--", value_size=26)
        self.vfd_setf = StatBlock("设定频率", "Hz", "--", value_size=16)
        self._vfd_grid.addWidget(self.vfd_state, 0, 0, 1, 2)
        self._vfd_grid.addWidget(self.vfd_freq, 1, 0)
        self._vfd_grid.addWidget(self.vfd_setf, 1, 1)
        vfd_card.body.addLayout(self._vfd_grid)
        cards.addWidget(vfd_card, 1)

        # 健康卡
        h_card = Card("设备健康")
        self._h_grid = QGridLayout()
        self._h_grid.setSpacing(10)
        self.h_total = StatBlock("设备总数", "台", "--", value_size=22)
        self.h_fault = StatBlock("故障", "台", "0", value_size=22)
        self.h_offline = StatBlock("离线", "台", "0", value_size=22)
        self._h_grid.addWidget(self.h_total, 0, 0)
        self._h_grid.addWidget(self.h_fault, 1, 0)
        self._h_grid.addWidget(self.h_offline, 1, 1)
        h_card.body.addLayout(self._h_grid)
        cards.addWidget(h_card, 1)

        self.root.addLayout(cards)

        # ---- 快捷入口 ----
        self.root.addWidget(section_title("快捷入口"))
        entry_row = QHBoxLayout()
        entry_row.setSpacing(10)
        entries = [
            ("监视", "数字量 / 模拟量 / 搅拌泵 / 水质监测", reg.PAGE_MONITOR_MENU),
            ("数据", "历史数据 / 日志 / 报警", reg.PAGE_DATA_MENU),
            ("设置", "本地 / 远端 / 网络 / 悬浮窗 / 窗口", reg.PAGE_SETTINGS_MENU),
        ]
        for title, desc, page in entries:
            # [用按钮内嵌 QLabel 排版，不用按钮自绘多行文字]
            #
            # 实锅 2026-11-05：文字写成 "标题\n说明" 时，QPushButton 在带
            # padding 的样式表下按**单行**算基线，两行文字被垂直居中同时
            # 上下各切掉一半（截图里只剩半行字）。把 minHeight 提到 84
            # 也没用 —— 问题在绘制，不在尺寸。
            #
            # 改成按钮内部放两行 QLabel（标题加粗 + 说明次要色），
            # 行高由布局管理，不受按钮自身文字绘制逻辑影响。
            btn = QPushButton()
            btn.setMinimumHeight(84)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet("QPushButton { text-align: center; padding: 0; }")
            inner = QVBoxLayout(btn)
            inner.setContentsMargins(10, 8, 10, 8)
            inner.setSpacing(2)
            lbl_title = QLabel(title)
            lbl_title.setAlignment(Qt.AlignCenter)
            lbl_title.setStyleSheet(
                "QLabel { font-size: 15px; font-weight: bold; color: #1A1A1A;"
                " background: transparent; }")
            lbl_desc = QLabel(desc)
            lbl_desc.setAlignment(Qt.AlignCenter)
            lbl_desc.setWordWrap(True)
            lbl_desc.setStyleSheet(
                "QLabel { font-size: 12px; color: #5A6572;"
                " background: transparent; }")
            inner.addStretch(1)
            inner.addWidget(lbl_title)
            inner.addWidget(lbl_desc)
            inner.addStretch(1)
            btn.clicked.connect(lambda _c=False, p=page: self.ctx.navigate(p))
            entry_row.addWidget(btn, 1)
        self.root.addLayout(entry_row)

        # ---- 设备列表 ----
        self.root.addWidget(section_title("设备状态"))
        self._table = DataTable()
        # [必须先 setColumnCount] DataTable 缺省 3 列；只传 4 个表头标签
        # 不会自动扩列（setHorizontalHeaderLabels 只在已有列上填字），
        # 结果「说明」列根本不显示（截图实见：只有 设备/类型/状态 三列）。
        # [5 列，2026-11-05] 原「说明」列同时想放"错误原因"和"连接地址"，
        # 但这两件事性质不同、也会互相覆盖（有错误时地址就看不见了）：
        #   · 地址   —— 静态配置（ip:port / 口·波特率·站号），**查故障第一步**
        #   · 错误信息 —— 动态原因（超时/无应答/未装配），只在异常时有值
        # 拆成两列后，一台设备离线时能同时看到"它在哪个地址"和"为什么离线"，
        # 不用再去翻配置文件。
        self._table.table.setColumnCount(5)
        self._table.table.setRowCount(0)
        self._table.table.setHorizontalHeaderLabels(
            ["设备", "类型", "状态", "地址", "错误信息"])
        # 地址列最宽（"COM17 · 115200 · 站 2" 这类串比设备名长）
        self._table.set_column_weights((4, 2, 2, 7, 5))
        card = Card()
        card.add(self._table)
        # 表格高度贴合 5 行内容（否则拖出一整屏空白，页面看着很空）
        self._table.fit_to_content(extra_rows=5)
        self.root.addWidget(card)
        self.root.addStretch(1)

    def refresh(self):
        model = self.ctx.model

        # ---- 温湿度 ----
        th = model.temphum
        self.th_temp.set_value(th.temp_text())
        self.th_hum.set_value(th.hum_text())

        # ---- 变频器 ----
        vfd = model.vfd
        if not vfd.is_fresh():
            self.vfd_state.set_value("无数据", theme.CLR_OFFLINE)
            self.vfd_freq.set_value("--")
            self.vfd_setf.set_value("--")
        else:
            color = (theme.CLR_FAULT if vfd.faulted
                     else (theme.CLR_OK if vfd.running else "#1A1A1A"))
            self.vfd_state.set_value(vfd.run_state_text(), color)
            self.vfd_freq.set_value(
                "%.2f" % vfd.output_freq_hz if vfd.has("outputFreqHz") else "--")
            self.vfd_setf.set_value(
                "%.2f" % vfd.set_freq_hz if vfd.has("setFreqHz") else "--")

        # ---- 健康 ----
        health = model.health
        fault, offline = health.counts()
        self.h_total.set_value(str(len(health.devices)))
        self.h_fault.set_value(str(fault),
                               theme.CLR_FAULT if fault else theme.CLR_OK)
        self.h_offline.set_value(str(offline),
                                 theme.CLR_WARN if offline else theme.CLR_OK)

        # ---- 设备表 ----
        rows = [(d.name, d.kind_text(), d.health_text(),
                 d.addr or "-", d.text or "-") for d in health.devices]
        table = self._table.table
        if table.rowCount() != len(rows):
            table.setRowCount(len(rows))
            self._table.fit_to_content(extra_rows=len(rows))
            for r in range(len(rows)):
                for c in range(5):
                    item = QTableWidgetItem("")
                    if c > 0:
                        item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
            table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)
        from PyQt5.QtGui import QColor
        for r, (name, kind, state, addr, text) in enumerate(rows):
            # 逐格判空再写：行重建与数据更新可能不同步，直接 .setText
            # 会在 item 为 None 时抛 AttributeError 把整页刷新打断。
            for c, value in enumerate((name, kind, state, addr, text)):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    if c > 0:
                        item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(value)
            dev = health.devices[r]
            color = {0: theme.CLR_OK, 1: theme.CLR_WARN, 2: theme.CLR_WARN,
                     3: theme.CLR_OFFLINE, 4: theme.CLR_FAULT}.get(dev.health)
            state_item = table.item(r, 2)
            if color and state_item is not None:
                state_item.setForeground(QColor(color))


# ============================================================================
#                              菜单页
# ============================================================================

class MainMenuPage(Page):
    """主菜单：三大分类入口（大按钮，触控友好）。"""

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("主菜单", "选择功能分类")
        # [用 QWidget 包住 grid 并限制高度] 直接 addLayout(grid, 1) 会让网格
        # 吃掉全部剩余高度，按钮被拉到 500px 高、文字漂在中间（截图实见）。
        holder = QWidget()
        holder.setMaximumHeight(200)
        grid = QGridLayout(holder)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(16)
        for i, (name, page, icon) in enumerate(reg.MAIN_MENU_ENTRIES):
            btn = QPushButton("%s\n%s" % (icon, name))
            btn.setMinimumHeight(160)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet(
                "QPushButton { font-size: 24px; font-weight: bold; }")
            btn.clicked.connect(lambda _c=False, p=page: self.ctx.navigate(p))
            grid.addWidget(btn, 0, i)
        self.root.addWidget(holder)
        self.root.addStretch(1)


class SubMenuPage(Page):
    """通用子菜单页（监视/数据/设置三个分类共用）。"""

    def __init__(self, ctx, title, entries):
        Page.__init__(self, ctx)
        self.add_header(title, "选择子项")
        grid = QGridLayout()
        grid.setSpacing(14)
        for i, (name, page) in enumerate(entries):
            btn = QPushButton(name)
            btn.setMinimumHeight(96)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet("QPushButton { font-size: 16px; font-weight: bold; }")
            btn.clicked.connect(lambda _c=False, p=page: self.ctx.navigate(p))
            grid.addWidget(btn, i // 3, i % 3)
        self.root.addLayout(grid)
        self.root.addStretch(1)


# ============================================================================
#                              数据页
# ============================================================================

class HistoryPage(Page):
    """历史数据：本地缓存最近 N 拍的变频器/温湿度读数。

    [为什么客户端本地缓存] 服务端不提供历史查询（C 端也没有）。
    历史页的价值在于「刚才发生了什么」——本进程内缓存足够，
    重连/重启即清空（与 C 端口径一致）。
    """

    TOPICS = (proto.TOPIC_VFD_STATUS, proto.TOPIC_TEMPHUM_STATUS,
              proto.TOPIC_AI_STATUS)

    MAX_ROWS = 300

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("历史数据", "最近 %d 条（本地缓存，重启清空）"
                        % self.MAX_ROWS)
        self._rows = []
        self._last_sample_ms = 0
        self._table = DataTable()
        self._table.table.setColumnCount(5)
        self._table.table.setHorizontalHeaderLabels(
            ["时间", "输出频率(Hz)", "温度(℃)", "湿度(%RH)", "母线电压(V)"])
        card = Card()
        card.add(self._table)
        self.root.addWidget(card, 1)

    def refresh(self):
        # 采样节拍 1s（比推送慢：历史表不需要每 100ms 一行）。
        now = int(time.monotonic() * 1000)
        if now - self._last_sample_ms < 1000:
            return
        self._last_sample_ms = now
        model = self.ctx.model
        if not (model.vfd.is_fresh() or model.temphum.is_fresh()):
            return
        stamp = time.strftime("%H:%M:%S")
        vfd = model.vfd
        th = model.temphum
        row = [stamp,
               "%.2f" % vfd.output_freq_hz if vfd.is_fresh() else "--",
               th.temp_text(), th.hum_text(),
               "%.1f" % vfd.bus_voltage_v if vfd.is_fresh() else "--"]
        self._rows.append(row)
        if len(self._rows) > self.MAX_ROWS:
            self._rows = self._rows[-self.MAX_ROWS:]
        self._rebuild()

    def _rebuild(self):
        table = self._table.table
        table.setRowCount(len(self._rows))
        for r, row in enumerate(reversed(self._rows)):    # 最新在上
            for c, text in enumerate(row):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(text)
        table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)
        table.scrollToTop()


class LogPage(Page):
    """日志：客户端运行日志（连接/命令/错误）。"""

    MAX_LINES = 500

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("日志", "客户端运行日志")
        self._lines = []

        row = QHBoxLayout()
        self._auto_scroll = QCheckBox("自动滚动")
        self._auto_scroll.setChecked(True)
        row.addWidget(self._auto_scroll)
        row.addStretch(1)
        clear_btn = QPushButton("清空")
        clear_btn.clicked.connect(self._clear)
        row.addWidget(clear_btn)
        self.root.addLayout(row)

        from PyQt5.QtWidgets import QPlainTextEdit
        self._view = QPlainTextEdit()
        self._view.setReadOnly(True)
        self._view.setStyleSheet(
            "QPlainTextEdit { background-color: #FFFFFF; border: 1px solid %s;"
            " border-radius: %dpx; font-family: monospace; font-size: 12px; }"
            % (theme.CLR_BORDER_SOFT, theme.RADIUS_CARD))
        card = Card()
        card.add(self._view)
        self.root.addWidget(card, 1)

        # 接管 bridge 的状态消息 → 日志页。
        self.ctx.bridge.statusMessage.connect(self.append)

    def append(self, text):
        stamp = time.strftime("%H:%M:%S")
        self._lines.append("[%s] %s" % (stamp, text))
        if len(self._lines) > self.MAX_LINES:
            self._lines = self._lines[-self.MAX_LINES:]
        self._view.setPlainText("\n".join(self._lines))
        if self._auto_scroll.isChecked():
            self._view.verticalScrollBar().setValue(
                self._view.verticalScrollBar().maximum())

    def _clear(self):
        self._lines = []
        self._view.clear()


class AlarmPage(Page):
    """报警：设备健康里 health>=2（过期/离线/故障）的条目。"""

    TOPICS = (proto.TOPIC_DEVICE_HEALTH,)

    MAX_ROWS = 200

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("报警", "设备异常记录")
        self._rows = []
        self._seen = {}            # (name, health) -> 首次出现时间
        self._table = DataTable()
        self._table.table.setColumnCount(4)
        self._table.table.setHorizontalHeaderLabels(
            ["时间", "设备", "类型", "状态"])
        card = Card()
        card.add(self._table)
        self.root.addWidget(card, 1)

    def refresh(self):
        """健康状态进入异常时记一条（边沿触发，不重复刷同一状态）。"""
        changed = False
        for dev in self.ctx.model.health.devices:
            key = (dev.name, dev.health)
            abnormal = dev.health >= 2
            if abnormal and key not in self._seen:
                self._seen[key] = time.strftime("%H:%M:%S")
                self._rows.append([self._seen[key], dev.name, dev.kind_text(),
                                   dev.health_text()])
                changed = True
            elif not abnormal and key in self._seen:
                # 恢复正常：清掉「已报警」标记，下次再异常还能再记。
                for k in list(self._seen):
                    if k[0] == dev.name:
                        del self._seen[k]
        if not changed:
            return
        if len(self._rows) > self.MAX_ROWS:
            self._rows = self._rows[-self.MAX_ROWS:]
        table = self._table.table
        table.setRowCount(len(self._rows))
        for r, row in enumerate(reversed(self._rows)):
            for c, text in enumerate(row):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(text)
        table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)


# ============================================================================
#                              设置页
# ============================================================================

class LocalSettingsPage(Page):
    """本地设置：界面/显示（仅本机生效，不发给服务端）。"""

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("本地设置", "仅本机生效")

        card = Card("显示")
        self.compact_check = QCheckBox("紧凑模式（竖屏/小屏）")
        self.compact_check.setToolTip("窄屏时自动启用")
        card.add(self.compact_check)

        self.fullscreen_check = QCheckBox("全屏显示")
        card.add(self.fullscreen_check)

        row = QHBoxLayout()
        row.addWidget(QLabel("界面缩放"))
        self.scale_combo = QComboBox()
        self.scale_combo.addItems(["100%", "110%", "125%", "150%"])
        row.addWidget(self.scale_combo)
        row.addStretch(1)
        card.body.addLayout(row)
        self.root.addWidget(card)

        card2 = Card("数据刷新")
        row2 = QHBoxLayout()
        row2.addWidget(QLabel("界面刷新周期"))
        self.refresh_spin = QSpinBox()
        self.refresh_spin.setRange(100, 2000)
        self.refresh_spin.setSingleStep(100)
        self.refresh_spin.setSuffix(" ms")
        self.refresh_spin.setValue(200)
        row2.addWidget(self.refresh_spin)
        row2.addStretch(1)
        card2.body.addLayout(row2)
        self.root.addWidget(card2)
        self.root.addStretch(1)

        self.fullscreen_check.toggled.connect(self._on_fullscreen)

    def _on_fullscreen(self, checked):
        win = self.window()
        if checked:
            win.showFullScreen()
        else:
            win.showNormal()
        self.ctx.set_status("全屏：%s" % ("开" if checked else "关"))


class RemoteSettingsPage(Page):
    """远端设置：运行模式（需 root）+ 设备参数只读展示。"""

    TOPICS = (proto.TOPIC_DEVICE_HEALTH, proto.TOPIC_VFD_STATUS)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("远端设置", "服务端运行模式与连接信息")

        card = Card("运行模式")
        self._mode_label = QLabel("未知")
        self._mode_label.setStyleSheet(
            "QLabel { font-size: 16px; font-weight: bold; "
            "background: transparent; }")
        card.add(self._mode_label)
        hint = QLabel("自动模式会拒绝全部远程控制（设备由现场逻辑驱动）；"
                      "手动模式允许 root 远程操作。")
        hint.setWordWrap(True)
        hint.setStyleSheet(theme.caption_label_qss())
        card.add(hint)

        row = QHBoxLayout()
        row.setSpacing(8)
        self.btn_manual = QPushButton("切换到手动")
        self.btn_manual.clicked.connect(lambda: self._set_mode(0))
        self.btn_auto = QPushButton("切换到自动")
        self.btn_auto.clicked.connect(lambda: self._set_mode(1))
        row.addWidget(self.btn_manual)
        row.addWidget(self.btn_auto)
        row.addStretch(1)
        card.body.addLayout(row)
        self.root.addWidget(card)

        # ---- 自动运行（与模式切换**分开**的两个按钮）----
        #
        # [为什么必须分开] 切到自动模式只是"授权自动逻辑可以跑"，不等于
        # "现在开始跑"。合成一个动作的话，操作员只想切个模式，设备立刻
        # 被自动逻辑驱动 —— 现场反馈"太危险了"。
        # 这里两个独立按钮，启停都要人明确按一次。
        card_auto = Card("自动运行")
        self._auto_label = QLabel("--")
        self._auto_label.setStyleSheet(
            "QLabel { background: transparent; font-size: 13px; }")
        card_auto.add(self._auto_label)
        hint2 = QLabel("切到自动模式只是授权；要真正开始自动运行，需按下面的"
                       "「启动自动运行」。服务端重启后控制器一律为停止状态。")
        hint2.setWordWrap(True)
        hint2.setStyleSheet(theme.caption_label_qss())
        card_auto.add(hint2)

        row2 = QHBoxLayout()
        row2.setSpacing(8)
        self.btn_auto_start = QPushButton("启动自动运行")
        self.btn_auto_start.clicked.connect(lambda: self._set_auto_running(True))
        self.btn_auto_stop = QPushButton("停止自动运行")
        self.btn_auto_stop.clicked.connect(lambda: self._set_auto_running(False))
        row2.addWidget(self.btn_auto_start)
        row2.addWidget(self.btn_auto_stop)
        row2.addStretch(1)
        card_auto.body.addLayout(row2)
        self.root.addWidget(card_auto)

        card2 = Card("连接信息")
        self._conn_label = QLabel("--")
        self._conn_label.setWordWrap(True)
        self._conn_label.setStyleSheet("QLabel { background: transparent; }")
        card2.add(self._conn_label)
        self.root.addWidget(card2)
        self.root.addStretch(1)

    def _set_mode(self, mode):
        if not self.ctx.is_root():
            self.ctx.set_status("权限不足：切换运行模式需要 root 账户")
            return
        if not self.ctx.bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        self.ctx.set_status("正在切换运行模式…")

        def _done(doc):
            if doc.get("ok"):
                self.ctx.set_status("运行模式已切到 %s"
                                    % ("自动" if mode else "手动"))
            else:
                self.ctx.set_status("切换失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.set_run_mode(mode, on_done=_done)

    def _set_auto_running(self, want_on):
        """启动/停止自动运行（**逻辑在服务端**，客户端只发意图）。"""
        if not self.ctx.is_root():
            self.ctx.set_status("权限不足：自动运行控制需要 root 账户")
            return
        if not self.ctx.bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        self.ctx.set_status("正在%s自动运行…" % ("启动" if want_on else "停止"))

        def _done(doc):
            if doc.get("ok"):
                self.ctx.set_status("自动运行已%s"
                                    % ("启动" if want_on else "停止"))
            else:
                self.ctx.set_status("操作失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.send_auto_control(want_on, on_done=_done)

    def refresh(self):
        model = self.ctx.model
        if model.run_mode_known:
            if model.is_auto():
                self._mode_label.setText("自动（远程控制已禁用）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 16px; font-weight: bold; color: %s;"
                    " background: transparent; }" % theme.CLR_WARN)
            else:
                self._mode_label.setText("手动（允许 root 远程控制）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 16px; font-weight: bold; color: %s;"
                    " background: transparent; }" % theme.CLR_OK)
        else:
            self._mode_label.setText("未知")
        usable = self.ctx.is_root() and not model.is_auto()
        self.btn_manual.setEnabled(self.ctx.is_root() and model.is_auto())
        self.btn_auto.setEnabled(usable)

        # ---- 自动运行（与模式分开显示）----
        #
        # [三种状态必须分得清] 手动 / 自动但未运行 / 自动且运行中。
        # 把"自动模式"直接说成"自动运行中"会让操作员以为设备已经在按逻辑
        # 动作，而实际还要再按一次启动。
        running = model.auto_running
        if not model.run_mode_known:
            self._auto_label.setText("未知")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 13px; }")
        elif not model.is_auto():
            self._auto_label.setText("手动模式（自动运行不可用）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 13px; color: #5A6572; }")
        elif running is None:
            self._auto_label.setText("自动模式 · 运行状态未知（服务端未上报）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 13px; color: #5A6572; }")
        elif running:
            self._auto_label.setText("自动模式 · 运行中")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 13px;"
                " font-weight: bold; color: %s; }" % theme.CLR_WARN)
        else:
            self._auto_label.setText("自动模式 · 已停止（未按过启动）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 13px; color: #5A6572; }")
        can_ctrl = self.ctx.is_root() and model.is_auto()
        self.btn_auto_start.setEnabled(can_ctrl and running is not True)
        self.btn_auto_stop.setEnabled(can_ctrl and running is True)
        bridge = self.ctx.bridge
        if bridge.is_connected():
            self._conn_label.setText(
                "已连接 · 用户 %s（%s）"
                % (bridge.user,
                   "管理员" if self.ctx.is_root() else "只读"))
        else:
            self._conn_label.setText("未连接")


class NetworkPage(Page):
    """网络设置：服务端地址/端口的只读展示 + 重连。"""

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("网络设置", "服务端连接参数")

        card = Card("当前连接")
        self._host_label = QLabel("--")
        self._host_label.setStyleSheet("QLabel { background: transparent; }")
        card.add(self._host_label)
        self._state_label = QLabel("--")
        self._state_label.setStyleSheet("QLabel { background: transparent; }")
        card.add(self._state_label)
        row = QHBoxLayout()
        self.btn_reconnect = QPushButton("重新连接")
        self.btn_reconnect.clicked.connect(self._reconnect)
        row.addWidget(self.btn_reconnect)
        row.addStretch(1)
        card.body.addLayout(row)
        self.root.addWidget(card)

        card2 = Card("说明")
        note = QLabel("连接参数在登录窗口填写。要改服务器地址，请退出后重新登录"
                      "（或用下方重新连接按钮回到登录窗）。")
        note.setWordWrap(True)
        note.setStyleSheet(theme.caption_label_qss())
        card2.add(note)
        self.root.addWidget(card2)
        self.root.addStretch(1)

    def _reconnect(self):
        self.ctx.set_status("请退出程序后重新登录以更换服务器")

    def refresh(self):
        bridge = self.ctx.bridge
        client = bridge.client
        if client is not None:
            self._host_label.setText("服务器：%s:%d"
                                     % (client.host, client.port))
            self._state_label.setText("状态：%s"
                                      % ("已就绪" if bridge.is_connected()
                                         else client.state))
        else:
            self._host_label.setText("服务器：--")
            self._state_label.setText("状态：未连接")


class OverlayPage(Page):
    """悬浮窗设置：标题栏/状态栏显隐 + 悬浮信息内容。"""

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("悬浮窗设置", "界面元素显隐")

        card = Card("显示元素")
        self.show_title = QCheckBox("显示顶部标题栏")
        self.show_title.setChecked(True)
        self.show_title.toggled.connect(self._apply)
        card.add(self.show_title)

        self.show_status = QCheckBox("显示底部状态栏")
        self.show_status.setChecked(True)
        self.show_status.toggled.connect(self._apply)
        card.add(self.show_status)

        self.show_nav = QCheckBox("显示左侧导航栏")
        self.show_nav.setChecked(True)
        self.show_nav.toggled.connect(self._apply)
        card.add(self.show_nav)
        self.root.addWidget(card)

        card2 = Card("标题栏内容")
        self.show_clock = QCheckBox("显示时钟")
        self.show_clock.setChecked(True)
        self.show_clock.toggled.connect(self._apply
                                        if hasattr(self, "_apply") else lambda: None)
        card2.add(self.show_clock)
        self.show_th = QCheckBox("显示温湿度")
        self.show_th.setChecked(True)
        card2.add(self.show_th)
        self.root.addWidget(card2)
        self.root.addStretch(1)

    def _apply(self):
        """把显隐设置作用到主窗（win 是本页的顶层窗口）。"""
        win = self.window()
        if hasattr(win, "title_bar"):
            win.title_bar.setVisible(self.show_title.isChecked())
        if hasattr(win, "status_bar"):
            win.status_bar.setVisible(self.show_status.isChecked())
        if hasattr(win, "nav"):
            win.nav.setVisible(self.show_nav.isChecked())


class WindowPage(Page):
    """窗口设置：窗口尺寸/形态。"""

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("窗口设置", "尺寸与形态")

        card = Card("尺寸")
        row = QHBoxLayout()
        row.addWidget(QLabel("窗口大小"))
        self.size_combo = QComboBox()
        self.size_combo.addItems(["1280 × 720（缺省）", "1024 × 600（紧凑）",
                                  "800 × 480（小屏）", "全屏"])
        row.addWidget(self.size_combo)
        row.addStretch(1)
        card.body.addLayout(row)

        apply_btn = QPushButton("应用")
        apply_btn.clicked.connect(self._apply)
        row2 = QHBoxLayout()
        row2.addWidget(apply_btn)
        row2.addStretch(1)
        card.body.addLayout(row2)
        self.root.addWidget(card)

        card2 = Card("信息")
        info = QLabel("窄窗口（宽 < %dpx）时左侧导航会自动收起，"
                      "由「主菜单」页承载导航。" % theme.COMPACT_MAX_W)
        info.setWordWrap(True)
        info.setStyleSheet(theme.caption_label_qss())
        card2.add(info)
        self.root.addWidget(card2)
        self.root.addStretch(1)

    def _apply(self):
        win = self.window()
        idx = self.size_combo.currentIndex()
        if idx == 0:
            win.showNormal()
            win.resize(theme.WIN_DEFAULT_W, theme.WIN_DEFAULT_H)
        elif idx == 1:
            win.showNormal()
            win.resize(1024, 600)
        elif idx == 2:
            win.showNormal()
            win.resize(800, 480)
        else:
            win.showFullScreen()
        self.ctx.set_status("窗口尺寸已应用")
