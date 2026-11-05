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

import csv
import datetime
import re
import time

from PyQt5.QtCore import QPoint, Qt, QDateTime, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QPainter, QPen, QStandardItem, \
    QStandardItemModel
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QDialog, QFileDialog,
                             QFrame, QGridLayout, QHBoxLayout, QHeaderView,
                             QLabel, QLineEdit, QPushButton, QSpinBox,
                             QTableWidget, QTableWidgetItem, QDateTimeEdit,
                             QVBoxLayout, QWidget)

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
            # [min-height 必须写进按钮自身样式表，setMinimumHeight 不够]
            #
            # 实锅 2026-11-05（第二次踩）：theme.install 装上全局 QSS 后，
            # 通用规则 `QPushButton { min-height: 18px; }` 会**覆盖**
            # setMinimumHeight(84) —— 样式表激活时 QSS 的 min/max 系列
            # 优先于 QWidget 的显式最值（实测：按钮 minimumHeight() 读回
            # 20，几何高 25，内部两行文字被上下裁切，主界面快捷入口只剩
            # 半行字）。按钮自己的样式表与应用级同名属性冲突时按钮本地
            # 规则赢，故在这里把 min-height 重新钉回 84。
            btn.setMinimumHeight(84)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet("QPushButton { text-align: center; padding: 0;"
                              " min-height: 84px; }")
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
    """历史数据：本地缓存最近 N 拍的过程/电气/状态数据。

    [为什么客户端本地缓存] 服务端不提供历史查询（C 端也没有）。
    历史页的价值在于「刚才发生了什么」——本进程内缓存足够，
    重连/重启即清空（与 C 端口径一致）。

    [采集项（2026-11-05）] 时间 / 运行模式 / 搅拌机状态+频率+电流+转速 /
    浊度×2 / 瞬时流量+正累计 / 压差(MPa) / 电箱温湿度 / DI+DO 位图。
    全部来自既有推送主题，服务端无需新增接口；**各列独立判新鲜**——
    某台设备离线只让自己那列显示 "--"，不影响其余列继续记录。

    [DI/DO 的记录口径] 一个字节记 8 个点位：32 路输入/输出各折成一个
    **32 位字（4 字节）**，两列合计 8 字节，按 `0x%08X` 十六进制显示
    （bit n = 通道 n，与 C 端位图口径一致）。现场 `.13` 已聚合进 `.12`
    （对外 32 点、publish=false 不发点位帧），故取「已刷新且点数最多」
    的模块即全部有效点位，不存在重复/遗漏。

    [行高亮口径] 采样那一刻存在异常（变频器故障 / 浊度≥20NTU / 流量计
    报警字非 0 / 压差越限 / 阀故障含超时）的行整行淡红底 —— 只用已有
    数据里的异常位，不新增推送、不新增判断逻辑。

    [排序 / 筛选 / 图表 / 导出（2026-11-05 第二批）]
      · **双击表头排序**：新列降序 → 再击转升 → 再击取消（恢复最新在上）。
        数值列按数值排（字符串序会把 "12.5" 排到 "9.1" 前面）；排序只动
        显示视图，采样缓冲的时序永不改变。
      · **筛选**：起始/结束时间（HH:MM 或 HH:MM:SS）+ 列下拉 + 关键字
        包含匹配，筛选按钮应用、清除筛选复位；只影响显示与导出范围。
      · **折线图**：QPainter 手绘（零第三方依赖，板端无 matplotlib），
        按当前筛选结果时间正序取数，非数值点跳过不补。
      · **导出数据**：保存框选 .csv → CSV（utf-8-sig）；选 .xlsx →
        Excel（xlsx_bridge 动态库，见 tools/xlsx_bridge/）；导出内容
        = 当前可见视图（筛选与排序生效后的样子）。
    """

    #: 列定义（表头与 CSV 导出共用一份，改这里两处同步）。
    COLUMNS = ["时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
               "搅拌转速(rpm)", "浊度1(NTU)", "浊度2(NTU)",
               "瞬时流量", "正累计", "压差(MPa)",
               "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)"]

    TOPICS = (proto.TOPIC_VFD_STATUS, proto.TOPIC_TEMPHUM_STATUS,
              proto.TOPIC_AI_STATUS, proto.TOPIC_TURBIDITY_STATUS,
              proto.TOPIC_FLOWMETER_STATUS, proto.TOPIC_DIGITAL_STATUS,
              proto.TOPIC_VALVE_STATUS)

    MAX_ROWS = 300
    #: 浊度越限阈值（与水质监测页同一口径，见 pages_monitor）。
    TURBIDITY_WARN_NTU = 20.0
    #: 异常行的底色（CLR_FAULT 的淡色版，红字太刺眼、淡底不干扰读数）。
    ABNORMAL_BG = "#FDECEC"

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("历史数据", "最近 %d 条（本地缓存，重启清空）"
                        % self.MAX_ROWS)
        self._rows = []              # [(texts, abnormal), ...] 旧→新
        self._last_sample_ms = 0
        self._table = DataTable()
        self._table.set_headers(self.COLUMNS)
        # 15 列数据都是短文本，最小档 72px；时间列内容
        # 'yyyy-MM-dd HH:MM:SS'（19 字符 ≈ 130px）必须整列放得下，否则
        # 日期被省略号吃掉只剩时刻（截图反馈"时间怎么没有日期"）——
        # 用按列下限给它 130px 硬保证，其余列维持 72px 档。
        self._table.set_column_weights(
            (3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3),
            min_widths={0: 165})

        # ---- 排序 / 筛选状态（双击表头排序；筛选栏按此过滤显示视图）----
        self._sort_col = None          # None=不排序（默认最新在上）
        self._sort_desc = True
        self._table.table.horizontalHeader().sectionDoubleClicked.connect(
            self._header_dblclicked)

        # ---- 筛选行：起始/结束时间 + 列多选 + 关键字 + 筛选/清除 ----
        fbar = QHBoxLayout()
        fbar.setSpacing(8)
        fbar.addWidget(QLabel("起始"))
        self._f_start = TimePickerEdit("yyyy-MM-dd HH:MM 或 HH:MM")
        self._f_start.setMaximumWidth(120)
        fbar.addWidget(self._f_start)
        fbar.addWidget(QLabel("结束"))
        self._f_end = TimePickerEdit("yyyy-MM-dd HH:MM 或 HH:MM")
        self._f_end.setMaximumWidth(120)
        fbar.addWidget(self._f_end)
        fbar.addWidget(QLabel("列"))
        self._f_col = MultiCheckCombo(self.COLUMNS, all_text="全部列")
        self._f_col.setMaximumWidth(170)
        fbar.addWidget(self._f_col)
        fbar.addWidget(QLabel("关键字"))
        self._f_text = QLineEdit()
        self._f_text.setPlaceholderText("包含…（空=不限）")
        fbar.addWidget(self._f_text, 1)
        btn_filter = QPushButton("筛选")
        btn_filter.clicked.connect(self._apply_filter)
        fbar.addWidget(btn_filter)
        btn_clear = QPushButton("清除筛选")
        btn_clear.clicked.connect(self._clear_filter)
        fbar.addWidget(btn_clear)
        self._count_label = QLabel("")
        self._count_label.setStyleSheet(theme.caption_label_qss())
        fbar.addWidget(self._count_label)
        self.root.addLayout(fbar)

        # ---- 工具行：折线图 + 滚动锁定 + 导出 ----
        bar = QHBoxLayout()
        bar.setSpacing(8)
        bar.addWidget(QLabel("折线列"))
        self._chart_col = QComboBox()
        for col, name in enumerate(self.COLUMNS):
            if col == 0 or col in (1, 2):      # 时间/模式/搅拌状态非数值
                continue
            self._chart_col.addItem(name)
        self._chart_col.setMaximumWidth(130)
        bar.addWidget(self._chart_col)
        btn_chart = QPushButton("生成折线图")
        btn_chart.clicked.connect(self._show_chart)
        bar.addWidget(btn_chart)
        bar.addStretch(1)
        self._freeze = QCheckBox("滚动锁定（停止刷表，后台照常采样）")
        self._freeze.toggled.connect(
            lambda frozen: None if frozen else self._rebuild())
        bar.addWidget(self._freeze)
        btn_export = QPushButton("导出数据")
        btn_export.clicked.connect(self._export_data)
        bar.addWidget(btn_export)
        self.root.addLayout(bar)

        # ---- 数据表卡片（在筛选/工具行之下：筛选条件先设，表格后看）----
        card = Card()
        card.add(self._table)
        self.root.addWidget(card, 1)

    def refresh(self):
        # 采样节拍 1s（比推送慢：历史表不需要每 100ms 一行）。
        # 滚动锁定只停"刷表"，采样照旧 —— 解锁后数据一行不少。
        now = int(time.monotonic() * 1000)
        if now - self._last_sample_ms < 1000:
            return
        self._last_sample_ms = now
        m = self.ctx.model
        di_word, do_word = self._digital_words(m)
        # 两个位图都是 None = 数字量还没刷新过一帧。
        if not (m.vfd.is_fresh() or m.turbidity.is_fresh()
                or m.flowmeter.is_fresh() or m.analog.is_fresh()
                or m.temphum.is_fresh() or di_word is not None):
            return
        texts = [time.strftime("%Y-%m-%d %H:%M:%S"),
                 self._mode_text(m),
                 self._vfd_state_text(m),
                 self._vfd_num_text(m, "output_freq_hz"),
                 self._vfd_num_text(m, "output_current_a"),
                 self._rpm_text(m),
                 self._turb_text(m, 0), self._turb_text(m, 1),
                 self._flow_text(m), self._total_text(m),
                 self._press_text(m),
                 m.temphum.temp_text(), m.temphum.hum_text(),
                 di_word or "--", do_word or "--"]
        self._rows.append((texts, self._is_abnormal(m)))
        if len(self._rows) > self.MAX_ROWS:
            self._rows = self._rows[-self.MAX_ROWS:]
        if not self._freeze.isChecked():
            self._rebuild()

    # ---- 各列取数（各自独立判新鲜，离线只影响自己那列）----

    @staticmethod
    def _rpm_text(m):
        """搅拌转速：变频器输出频率的换算值（变频器无转速寄存器）。"""
        if not m.vfd.is_fresh():
            return "--"
        return "%.0f" % m.vfd.speed_rpm

    @staticmethod
    def _mode_text(m):
        """运行模式：手动 / 自动·停止 / 自动·运行中（与顶栏同一口径）。

        [未知就写未知] 服务端没报过模式（run_mode_known=False）时显示
        "--"，不拿"手动"冒充 —— 编造的默认值会误导事后排查。
        """
        if not m.run_mode_known:
            return "--"
        if not m.is_auto():
            return "手动"
        if m.auto_running is None:
            return "自动·未知"
        return "自动·运行中" if m.auto_running else "自动·停止"

    @staticmethod
    def _vfd_state_text(m):
        """搅拌机状态：运行状态文案；故障时带上故障名（FAULT_TEXT 中文）。"""
        if not m.vfd.is_fresh():
            return "--"
        if m.vfd.faulted:
            detail = m.vfd.fault_text()
            return "故障 " + detail if detail != "无故障" else "故障"
        return m.vfd.run_state_text()

    @staticmethod
    def _vfd_num_text(m, attr):
        """变频器数值列（频率/电流）通用的取数与格式化。"""
        if not m.vfd.is_fresh():
            return "--"
        return "%.2f" % float(getattr(m.vfd, attr, 0.0) or 0.0)

    def _is_abnormal(self, m):
        """采样那一刻是否异常（整行淡红底）。只认已有数据里的异常位。"""
        if m.vfd.is_fresh() and m.vfd.faulted:
            return True
        if m.turbidity.is_fresh():
            for idx in range(min(m.turbidity.count, 2)):
                ntu = m.turbidity.ntu_of(idx)
                if ntu is not None and ntu >= self.TURBIDITY_WARN_NTU:
                    return True
        if m.flowmeter.is_fresh() and m.flowmeter.alarms:
            return True
        # 压差越限（断线/超量程）——服务端给说明文字即异常。
        if m.analog.is_fresh() and m.analog.eng_note.get(0, ""):
            return True
        # 阀故障（含 20s 动作超时）。
        if m.valve.is_fresh() and m.valve.fault_count():
            return True
        return False

    @staticmethod
    def _turb_text(m, index):
        """浊度 NTU；该台离线（online=False）时 ntu_of 返回 None → "--"。"""
        if not m.turbidity.is_fresh():
            return "--"
        ntu = m.turbidity.ntu_of(index)
        return "--" if ntu is None else "%.2f" % ntu

    @staticmethod
    def _flow_text(m):
        """瞬时流量：单位随流量计下发的单位码走，不写死 m³/h。"""
        if not m.flowmeter.is_fresh():
            return "--"
        return "%.2f %s" % (m.flowmeter.flow, m.flowmeter.flow_unit_text())

    @staticmethod
    def _total_text(m):
        """正向累积总量（单位码与瞬时流量分属两张表，见 model.FLOW_UNIT_TEXT）。"""
        if not m.flowmeter.is_fresh():
            return "--"
        return "%.2f %s" % (m.flowmeter.forward_total,
                            m.flowmeter.total_unit_text())

    @staticmethod
    def _press_text(m):
        """压差：模拟量通道 0 的工程量（服务端按 4~20mA → 0~1MPa 换算）。

        越限（断线/超量程）时服务端把值夹到端点并给说明 —— 夹出来的
        端点值没有记录价值，历史里直接记说明文字。
        """
        if not m.analog.is_fresh():
            return "--"
        note = m.analog.eng_note.get(0, "")
        if note:
            return note
        value = m.analog.eng_value(0)
        return "--" if value is None else "%.3f" % value

    @staticmethod
    def _digital_words(m):
        """DI/DO 位图折字：32 点各记一个 32 位字（4 字节），共 8 字节。

        :returns: (di_text, do_text)；没有任何已刷新的数字量模块时
        两个都是 None（区别于"有数据但全 0"的 "0x00000000"）。

        [为什么取点数最多的模块] 现场 `.13` 已聚合进 `.12`：客户端收到
        的点位帧就是聚合后的一个 32 点模块，被聚合者 `publish=false`
        只出健康不发点位帧。取「已刷新且点数最大」的模块即全部有效点位。
        """
        best = None
        for dev in m.digital.values():
            if dev.is_fresh() and (best is None or dev.count > best.count):
                best = dev
        if best is None:
            return None, None
        return ("0x%08X" % (best.di & 0xFFFFFFFF),
                "0x%08X" % (best.do & 0xFFFFFFFF))

    def _rebuild(self):
        rows = self._visible_rows()
        table = self._table.table
        table.setRowCount(len(rows))
        for r, (row, abnormal) in enumerate(rows):      # 视图序（默认最新在上）
            for c, text in enumerate(row):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(text)
                # [背景必须每拍重设] item 是复用的：只给异常行上色、不给
                # 正常行清除的话，行滚动/截断后旧底色会残留在已恢复正常的行上。
                item.setBackground(
                    QBrush(QColor(self.ABNORMAL_BG)) if abnormal
                    else QBrush())
        table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)
        if not self._freeze.isChecked():
            table.scrollToTop()
        self._count_label.setText(
            "%d/%d 条" % (len(rows), len(self._rows)))

    # ---- 排序 / 筛选 / 视图流水线 ----

    @staticmethod
    def _cell_float(text):
        """表格文本 → 数值（排序/画图共用）；非数值返回 None。

        口径：'--'/纯文字 → None；'0x' 前缀按十六进制（DI/DO 位图列）；
        '36.05 m³/h' 这类带单位文本取前导浮点数。
        [必须整串锚定] 正则若不锚尾，'2026-11-05 08:10:00' 会被解析成
        2026.0 —— 时间列（ISO 文本）在排序里被当成数值，时间序全乱
        （2026-11-05 实测踩到）。锚定规则：数字后只允许「空白+任意后缀」
        （即单位），'-11-05' 这种数字后紧跟非空白的内容不算数值。
        """
        t = str(text).strip()
        if not t or t == "--":
            return None
        if t[:2].lower() == "0x":
            try:
                return float(int(t, 16))
            except ValueError:
                return None
        m = re.match(r"^[-+]?\d*\.?\d+(?:\s+.*)?$", t)
        return float(m.group(0).split()[0]) if m else None

    @staticmethod
    def _norm_stamp(text, is_end):
        """筛选时间归一化；无法解析返回 None。

        :returns: None 或 (mode, key)。mode='F' → key 为完整
        'yyyy-MM-dd HH:MM:SS'（与行内时间列整体比较）；mode='T' → key 为
        'HH:MM:SS'（只比当天的时刻后缀——用户只填时间时按"当天"语义）。

        接受（缺省部分按 is_end 补端点：起点补 00:00:00，终点补 23:59:59）：
            HH:MM[:SS]               → 'T'
            MM-dd [HH:MM[:SS]]       → 'F'（年份取当前年）
            yyyy-MM-dd [HH:MM[:SS]]  → 'F'
        """
        text = str(text).strip()
        if not text:
            return None
        tokens = text.split()
        if len(tokens) > 2:
            return None
        date_part = None
        time_part = None
        for token in tokens:
            if ":" in token:
                if time_part is not None:
                    return None
                time_part = token
            else:
                if date_part is not None:
                    return None
                date_part = token
        if date_part is None and time_part is None:
            return None

        # ---- 时间段：缺省补端点 ----
        hh, mm, ss = (23, 59, 59) if is_end else (0, 0, 0)
        if time_part is not None:
            parts = time_part.split(":")
            if not 2 <= len(parts) <= 3:
                return None
            try:
                nums = [int(p) for p in parts]
            except ValueError:
                return None
            if len(nums) == 2:
                nums.append(59 if is_end else 0)
            hh, mm, ss = nums
            if not (0 <= hh < 24 and 0 <= mm < 60 and 0 <= ss < 60):
                return None

        # ---- 日期段：缺省 = 只比当天时刻（mode 'T'）----
        if date_part is None:
            return ("T", "%02d:%02d:%02d" % (hh, mm, ss))
        dp = date_part.split("-")
        try:
            if len(dp) == 2:                       # MM-dd → 当年
                today = datetime.date.today()
                y, m, d = today.year, int(dp[0]), int(dp[1])
            elif len(dp) == 3:                     # yyyy-MM-dd
                y, m, d = int(dp[0]), int(dp[1]), int(dp[2])
            else:
                return None
            datetime.date(y, m, d)                 # 合法性校验（含闰年）
        except ValueError:
            return None
        full = "%04d-%02d-%02d %02d:%02d:%02d" % (y, m, d, hh, mm, ss)
        return ("F", full)

    def _filtered_rows(self):
        """时间范围 + 列关键字过滤后的快照（**时间正序**，旧→新）。

        图表走这里（X 轴必须时间正序）；排序视图在其结果上重排。

        [列多选口径] 关键字在**勾选的任意一列**命中即保留（或匹配）；
        一列都没勾 = 不限列（全行拼接后找）。

        [时间口径] 行内时间是完整 'yyyy-MM-dd HH:MM:SS'（ISO 文本序 =
        时间序）；筛选两侧按 _norm_stamp 归一——只填时刻按"当天"比后缀，
        带日期比全键。
        """
        lo = self._norm_stamp(self._f_start.text(), is_end=False)
        hi = self._norm_stamp(self._f_end.text(), is_end=True)
        cols = self._f_col.checked_indexes()
        kw = self._f_text.text().strip()
        out = []
        for row, abnormal in self._rows:
            stamp = row[0]
            if lo is not None:
                value = stamp[-8:] if lo[0] == "T" else stamp
                if value < lo[1]:
                    continue
            if hi is not None:
                value = stamp[-8:] if hi[0] == "T" else stamp
                if value > hi[1]:
                    continue
            if kw:
                if cols:
                    hit = any(c < len(row) and kw in row[c] for c in cols)
                else:
                    hit = kw in " ".join(row)
                if not hit:
                    continue
            out.append((row, abnormal))
        return out

    def _visible_rows(self):
        """表格当前应显示的行（筛选 + 排序之后）。"""
        rows = self._filtered_rows()
        if self._sort_col is None:
            return list(reversed(rows))          # 默认最新在上
        col, desc = self._sort_col, self._sort_desc

        def key(item):
            text = item[0][col]
            num = self._cell_float(text)
            # 数值列按**数值**排（否则 "12.5" 会排在 "9.1" 前面）；
            # 非数值（"--"、文字）排在数值后面，组内按文本序。
            return (0, num, "") if num is not None else (1, 0, str(text))

        return sorted(rows, key=key, reverse=desc)

    def _header_dblclicked(self, col):
        """双击表头：新列→降序；同列降→升；同列升→取消排序。"""
        if self._sort_col == col:
            if self._sort_desc:
                self._sort_desc = False
            else:
                self._sort_col = None
        else:
            self._sort_col = col
            self._sort_desc = True
        header = self._table.table.horizontalHeader()
        if self._sort_col is None:
            header.setSortIndicatorShown(False)
        else:
            header.setSortIndicatorShown(True)
            header.setSortIndicator(
                col, Qt.DescendingOrder if self._sort_desc
                else Qt.AscendingOrder)
        self._rebuild()

    def _apply_filter(self):
        raw_start = self._f_start.text().strip()
        raw_end = self._f_end.text().strip()
        if raw_start and self._norm_stamp(raw_start, is_end=False) is None:
            self.ctx.set_status("起始时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        if raw_end and self._norm_stamp(raw_end, is_end=True) is None:
            self.ctx.set_status("结束时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        self._rebuild()

    def _clear_filter(self):
        self._f_start.clear()
        self._f_end.clear()
        self._f_col.clear_all()
        self._f_text.clear()
        self._rebuild()

    # ---- 折线图 / 导出 ----

    def _show_chart(self):
        """按当前筛选结果画选中列的折线图（时间正序，X=时间 Y=数值）。"""
        col_name = self._chart_col.currentText()
        try:
            col = self.COLUMNS.index(col_name)
        except ValueError:
            return
        points = []
        for row, _abnormal in self._filtered_rows():   # 时间正序
            value = self._cell_float(row[col])
            if value is not None:
                points.append((row[0], value))
        if len(points) < 2:
            self.ctx.set_status("可画的数值点不足 2 个（换列或放宽筛选）")
            return
        dialog = LineChartDialog(col_name, points, self)
        dialog.show()

    def _export_data(self):
        """导出**当前可见视图**（含筛选与排序）为 CSV 或 Excel。

        [格式由扩展名决定] 用户在保存框里选 .csv → CSV（utf-8-sig，
        Excel 双击直开）；选 .xlsx → Excel（走 xlsx_bridge 动态库，
        库缺失时提示而不是静默降级——「以为是 Excel 其实是 CSV」
        比直接报错更误事）。
        """
        rows = self._visible_rows()
        if not rows:
            self.ctx.set_status("无可导出的数据")
            return
        default = "hsf_history_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, selected = QFileDialog.getSaveFileName(
            self, "导出历史数据", default,
            "Excel 工作簿 (*.xlsx);;CSV (*.csv)")
        if not path:
            return
        if path.lower().endswith(".xlsx"):
            try:
                from . import xlsx_writer
            except ImportError:
                xlsx_writer = None
            if xlsx_writer is None or not xlsx_writer.available():
                self.ctx.set_status(
                    "Excel 导出需要 xlsx_bridge 动态库"
                    "（tools/xlsx_bridge/build.bat），CSV 导出不受影响")
                return
            data = [list(r) for r, _abn in rows]
            try:
                xlsx_writer.write_rows(path, "历史数据", self.COLUMNS,
                                       data, {0: 10})
            except Exception as exc:                 # noqa: BLE001
                self.ctx.set_status("Excel 导出失败：%s" % exc)
                return
        else:
            if not path.lower().endswith(".csv"):
                path += ".csv"
            with open(path, "w", encoding="utf-8-sig", newline="") as fh:
                writer = csv.writer(fh)
                writer.writerow(self.COLUMNS)
                for texts, _abnormal in rows:
                    writer.writerow(texts)
        self.ctx.set_status("已导出 %d 条 → %s" % (len(rows), path))

    def _export_csv(self):
        """旧入口（保留兼容）：直接走 CSV 分支。"""
        rows = self._visible_rows()
        if not rows:
            self.ctx.set_status("无可导出的数据")
            return
        default = "hsf_history_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, _selected = QFileDialog.getSaveFileName(
            self, "导出历史数据", default, "CSV (*.csv)")
        if not path:
            return
        if not path.lower().endswith(".csv"):
            path += ".csv"
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow(self.COLUMNS)
            for texts, _abnormal in rows:
                writer.writerow(texts)
        self.ctx.set_status("已导出 %d 条 → %s" % (len(rows), path))


class LineChartDialog(QDialog):
    """历史数据折线图（**QPainter 手绘，零第三方依赖**）。

    [为什么不用 matplotlib] 板端 Python 没有（也装不了）matplotlib，
    而历史页要在板上跑；QPainter 自绘折线 + 网格 + 刻度一共百来行，
    桌面/板端表现一致。数据来自历史页当前**筛选后**的视图（时间正序），
    '--'/非数值点直接跳过（断点不补——编造的数据比缺口更危险）。

    [非模态] 允许边看图边切筛选条件再点一次"生成折线图"——每次点击
    都弹新图，旧图保留（对比两个时间段用）。
    """

    #: 绘图区边距（左留 Y 轴刻度、下留 X 轴时间标签）。
    MARGIN_L, MARGIN_T, MARGIN_R, MARGIN_B = 72, 40, 26, 46
    LINE_COLOR = "#2E7CE8"
    GRID_COLOR = "#E3E6EB"
    TEXT_COLOR = "#5A6572"

    def __init__(self, column_name, points, parent=None):
        """points: [(时间文本, 数值), ...] 时间正序。"""
        QDialog.__init__(self, parent)
        self.setWindowTitle("折线图 — %s（%d 点）" % (column_name, len(points)))
        self.resize(880, 460)
        self._column_name = column_name
        self._points = points

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        w, h = self.width(), self.height()
        rect_l, rect_t = self.MARGIN_L, self.MARGIN_T
        rect_r, rect_b = w - self.MARGIN_R, h - self.MARGIN_B
        plot_w, plot_h = rect_r - rect_l, rect_b - rect_t
        if plot_w <= 40 or plot_h <= 40:
            return

        values = [v for _t, v in self._points]
        vmin, vmax = min(values), max(values)
        if vmax == vmin:
            vmin, vmax = vmin - 1.0, vmax + 1.0     # 全程一条水平线也留刻度
        else:
            pad = (vmax - vmin) * 0.08
            vmin, vmax = vmin - pad, vmax + pad

        def x_at(i):
            n = len(self._points)
            if n == 1:
                return rect_l + plot_w / 2
            return rect_l + plot_w * i / (n - 1)

        def y_at(v):
            return rect_b - plot_h * (v - vmin) / (vmax - vmin)

        # ---- 网格 + Y 刻度（5 档，紧凑数字）----
        painter.setPen(QPen(QColor(self.GRID_COLOR), 1))
        font = painter.font()
        font.setPixelSize(11)
        painter.setFont(font)
        for i in range(6):
            frac = i / 5.0
            y = rect_t + plot_h * frac
            painter.drawLine(rect_l, int(y), rect_r, int(y))
            painter.setPen(QColor(self.TEXT_COLOR))
            value = vmax - (vmax - vmin) * frac
            painter.drawText(0, int(y) - 6, rect_l - 8, 14,
                             Qt.AlignRight | Qt.AlignVCenter, "%.4g" % value)
            painter.setPen(QPen(QColor(self.GRID_COLOR), 1))

        # ---- X 刻度：至多 8 个时间标签（均分取样）----
        n = len(self._points)
        x_count = min(8, n)
        for i in range(x_count):
            idx = i * (n - 1) // max(1, x_count - 1) if x_count > 1 else 0
            x = x_at(idx)
            painter.setPen(QColor(self.TEXT_COLOR))
            painter.drawText(int(x) - 44, rect_b + 8, 88, 16,
                             Qt.AlignCenter, self._points[idx][0][-8:])

        # ---- 折线 ----
        painter.setPen(QPen(QColor(self.LINE_COLOR), 2))
        for i in range(n - 1):
            x0, y0 = x_at(i), y_at(self._points[i][1])
            x1, y1 = x_at(i + 1), y_at(self._points[i + 1][1])
            painter.drawLine(int(x0), int(y0), int(x1), int(y1))
        if n <= 120:                                  # 点不多时画圆点便于对读
            painter.setBrush(QColor(self.LINE_COLOR))
            painter.setPen(Qt.NoPen)
            for i, (_t, v) in enumerate(self._points):
                painter.drawEllipse(
                    _px(x_at(i) - 3), _px(y_at(v) - 3), 6, 6)

        # ---- 标题与极值 ----
        painter.setPen(QColor("#1F4E79"))
        big = painter.font()
        big.setPixelSize(14)
        big.setBold(True)
        painter.setFont(big)
        painter.drawText(rect_l, 8, rect_r - rect_l, 20,
                         Qt.AlignLeft | Qt.AlignVCenter,
                         "%s（min %.4g / max %.4g）"
                         % (self._column_name, min(values), max(values)))


def _px(value):
    """QPainter 几何 API 只收整型；这里统一收缩（去 float 传参 TypeError）。"""
    return int(value)


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


# ============================================================================
#                          可复用输入控件（历史页/设置页）
# ============================================================================

class TimePickerEdit(QLineEdit):
    """日期时间输入框：**单击弹带日历的选择浮层**，也允许手输。

    接受三种口径（历史页筛选两侧通用，归一见 _norm_stamp）：
        HH:MM[:SS]              （当天）
        MM-dd HH:MM[:SS]        （当年，如 "11-05 23:09"）
        yyyy-MM-dd [HH:MM[:SS]]
    """

    timePicked = pyqtSignal(str)          # 归一化 "yyyy-MM-dd HH:MM:SS"

    def __init__(self, placeholder="", parent=None):
        QLineEdit.__init__(self, parent)
        self.setPlaceholderText(placeholder)
        # [排除软键盘] 本控件单击弹的是自己的日期选择浮层——软键盘再弹
        # 一层就叠罗汉了（softkeyboard.NO_SOFT_KBD 排除标记）。
        from .softkeyboard import NO_SOFT_KBD
        self.setProperty(NO_SOFT_KBD, True)

    def mousePressEvent(self, event):
        QLineEdit.mousePressEvent(self, event)
        self.show_picker()

    def show_picker(self):
        popup = QFrame(self, Qt.Popup)
        popup.setStyleSheet("QFrame { background: #FFFFFF; }")
        lay = QVBoxLayout(popup)
        lay.setContentsMargins(12, 12, 12, 12)
        lay.setSpacing(8)
        hint = QLabel("日期时间（点日历图标选日期）")
        hint.setStyleSheet("QLabel { color: #5A6572; }")
        lay.addWidget(hint)
        edit = QDateTimeEdit(QDateTime.currentDateTime())
        edit.setCalendarPopup(True)           # 内置日历下拉
        edit.setDisplayFormat("yyyy-MM-dd HH:mm:ss")
        lay.addWidget(edit)
        row = QHBoxLayout()
        btn_ok = QPushButton("确定")
        btn_clear = QPushButton("清空")
        row.addWidget(btn_ok)
        row.addWidget(btn_clear)
        lay.addLayout(row)

        def _commit():
            self._set_from_picker(edit.dateTime())
            popup.close()

        def _clear():
            self.clear()
            popup.close()

        btn_ok.clicked.connect(_commit)
        btn_clear.clicked.connect(_clear)
        popup.move(self.mapToGlobal(QPoint(0, self.height())))
        popup.show()

    def _set_from_picker(self, qdatetime):
        """浮层确定 → 写入文本（独立成方法：无头测试直接调这里）。"""
        text = qdatetime.toString("yyyy-MM-dd HH:mm:ss")
        self.setText(text)
        self.timePicked.emit(text)


class MultiCheckCombo(QComboBox):
    """多选下拉：每个选项带勾选框、可勾任意多个；**空选 = 不过滤（全部）**。

    [显示口径] 非编辑下拉只能显示「当前项」文本，塞不下勾选汇总——做成
    editable + lineEdit 只读，把汇总（"浊度1+浊度2" / "全部列"）写进去。
    下拉里点选**不收起**：连续勾多个不中断（QComboBox 默认点一下就收，
    这里走 view().pressed 自行翻转勾选，绕开默认选中行为）。
    """

    selectionChanged = pyqtSignal()

    def __init__(self, items, all_text="全部列", parent=None):
        QComboBox.__init__(self, parent)
        self._all_text = all_text
        self.setEditable(True)
        self.setInsertPolicy(QComboBox.NoInsert)
        self.lineEdit().setReadOnly(True)
        self._model = QStandardItemModel(self)
        for text in items:
            item = QStandardItem(text)
            item.setCheckable(True)
            item.setCheckState(Qt.Unchecked)
            self._model.appendRow(item)
        self.setModel(self._model)
        self._model.itemChanged.connect(lambda _i: self._sync_text())
        self.view().pressed.connect(self._toggle_item)
        self._sync_text()

    def _toggle_item(self, index):
        item = self._model.itemFromIndex(index)
        if item is None:
            return
        item.setCheckState(
            Qt.Unchecked if item.checkState() == Qt.Checked else Qt.Checked)

    def _checked_items(self):
        return [self._model.item(i) for i in range(self._model.rowCount())
                if self._model.item(i).checkState() == Qt.Checked]

    def checked_indexes(self):
        """当前勾中的选项下标列表（0 基；空列表 = 未勾任何项）。"""
        return [i for i in range(self._model.rowCount())
                if self._model.item(i).checkState() == Qt.Checked]

    def clear_all(self):
        for i in range(self._model.rowCount()):
            self._model.item(i).setCheckState(Qt.Unchecked)
        self._sync_text()

    def _sync_text(self):
        names = [it.text() for it in self._checked_items()]
        self.lineEdit().setText("、".join(names) if names else self._all_text)
        self.selectionChanged.emit()


class RemoteSettingsPage(Page):
    """远端设置：运行模式（需 root）+ 设备参数只读展示。

    [线程口径] bridge 回调跑在**网络线程**；configReady 信号把配置快照
    投递回 UI 线程再铺控件（信号跨线程自动排队）。曾直接在回调里
    _populate → 从网络线程碰 QLineEdit/勾选框，界面死活不更新（用户
    反馈"没生效"）—— 这是 Qt 线程规则，不是可选项。
    """

    #: 配置快照到货（网络线程 emit，UI 线程收）。
    configReady = pyqtSignal(dict)

    TOPICS = (proto.TOPIC_DEVICE_HEALTH, proto.TOPIC_VFD_STATUS)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self._config_fetched = False      # 已发过 GET（含失败），防 refresh 轰炸
        self.configReady.connect(self._populate)
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

        # ---- 采集配置（远程改"采哪些、多快采"；需 root）----
        card3 = Card("采集配置")
        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        # 表头占**独立的第 0 行**，数据行从第 1 行起。
        # [教训] 曾把表头塞进 (0,1)/(0,2)——那正是搅拌泵行复选框和数值框
        # 的格子：QGridLayout 同一格后加者顶掉先加者，搅拌泵的控件直接
        # 从布局里消失（截图实见：第一行只剩"采集/周期ms"两个文字）。
        grid.addWidget(QLabel("设备"), 0, 0)
        grid.addWidget(QLabel("采集"), 0, 1)
        grid.addWidget(QLabel("周期"), 0, 2)
        self._collect_rows = []     # (key_enabled, checkbox, key_poll, spin)
        for r, (label, key_enabled, key_poll) in enumerate((
                ("搅拌泵", proto.CFGK_VFD_ENABLED, proto.CFGK_VFD_POLL_MS),
                ("温湿度", proto.CFGK_TEMPHUM_ENABLED,
                 proto.CFGK_TEMPHUM_POLL_MS),
                ("浊度仪", proto.CFGK_TURBIDITY_ENABLED,
                 proto.CFGK_TURBIDITY_POLL_MS),
                ("模拟量", proto.CFGK_ANALOG_ENABLED,
                 proto.CFGK_ANALOG_POLL_MS),
                ("数字量", proto.CFGK_DIGITAL_ENABLED,
                 proto.CFGK_DIGITAL_POLL_MS)), start=1):
            chk = QCheckBox()
            spin = QSpinBox()
            spin.setRange(proto.CFG_POLL_MS_MIN, proto.CFG_POLL_MS_MAX)
            spin.setSingleStep(50)
            spin.setSuffix(" ms")
            grid.addWidget(QLabel(label), r, 0)
            grid.addWidget(chk, r, 1)
            grid.addWidget(spin, r, 2)
            self._collect_rows.append((key_enabled, chk, key_poll, spin))
        card3.body.addLayout(grid)
        hint3 = QLabel("流量计跟随温湿度的周期；电磁阀/进水泵跟随数字量的"
                       "开关。改开关立即生效（被停的设备健康转数据过期），"
                       "改周期下一拍生效。")
        hint3.setWordWrap(True)
        hint3.setStyleSheet(theme.caption_label_qss())
        card3.add(hint3)
        row3 = QHBoxLayout()
        self._collect_refresh = QPushButton("刷新")
        self._collect_refresh.clicked.connect(self._fetch_config)
        self._collect_apply = QPushButton("应用（需 root）")
        self._collect_apply.clicked.connect(self._apply_collect)
        row3.addWidget(self._collect_refresh)
        row3.addWidget(self._collect_apply)
        row3.addStretch(1)
        card3.body.addLayout(row3)
        self.root.addWidget(card3)

        # ---- 数据存储（SQLite 历史库；需 root）----
        card4 = Card("数据存储（SQLite：历史/日志/报警）")
        self._storage_enable = QCheckBox("启用落盘")
        card4.add(self._storage_enable)
        path_row = QHBoxLayout()
        path_row.addWidget(QLabel("库文件路径"))
        self._storage_path = QLineEdit()
        self._storage_path.setPlaceholderText(
            "空 = 服务端程序目录\\hsf_data.db；相对路径也按程序目录展开")
        path_row.addWidget(self._storage_path, 1)
        card4.body.addLayout(path_row)
        sample_row = QHBoxLayout()
        sample_row.addWidget(QLabel("入库周期"))
        self._storage_sample = QSpinBox()
        self._storage_sample.setRange(1000, 3600000)
        self._storage_sample.setSingleStep(1000)
        self._storage_sample.setSuffix(" ms")
        sample_row.addWidget(self._storage_sample)
        sample_row.addStretch(1)
        card4.body.addLayout(sample_row)
        self._db_path_label = QLabel("当前库文件：--")
        self._db_path_label.setWordWrap(True)
        self._db_path_label.setStyleSheet(
            "QLabel { background: transparent; }")
        card4.add(self._db_path_label)
        row4 = QHBoxLayout()
        self._storage_save = QPushButton("保存（需 root）")
        self._storage_save.clicked.connect(self._apply_storage)
        row4.addWidget(self._storage_save)
        row4.addStretch(1)
        card4.body.addLayout(row4)
        self.root.addWidget(card4)

        self.root.addStretch(1)

    def on_enter(self):
        # 切到本页拉一次配置快照（回显服务端当前值；连接建立后才有意义）。
        self._fetch_config(silent=True)

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

    # ---- 采集配置 / 数据存储（CMD_CONFIG_GET / SET 的界面侧）----

    def _config_gate(self):
        """改配置的前置门：已连接 + root。失败给状态栏原因并返回 False。"""
        if not self.ctx.bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return False
        if not self.ctx.is_root():
            self.ctx.set_status("权限不足：修改服务端配置需要 root 账户")
            return False
        return True

    def _fetch_config(self, silent=False):
        """拉配置快照回显（切页/点刷新时）。silent=失败不打扰状态栏。"""
        if not self.ctx.bridge.is_connected():
            if not silent:
                self.ctx.set_status("未连接服务端")
            return
        self._config_fetched = True

        def _done(doc):
            # [网络线程] 只 emit 信号（跨线程排队回 UI 线程），不碰控件。
            if doc.get("ok"):
                self.configReady.emit(doc)
                if not silent:
                    self.ctx.set_status("配置已刷新")
            elif not silent:
                self.ctx.set_status("读取配置失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_get(on_done=_done)

    def _populate(self, doc):
        """把服务端配置快照铺到控件上（两节各自缺省跳过）。

        [用应答回显而非本地值] 应用成功的应答带服务端**实际生效值**
        （周期可能被服务端夹取过），铺回控件保证「界面显示的就是生效的」。
        """
        collect = doc.get("collect") or {}
        if collect:
            for key_enabled, chk, key_poll, spin in self._collect_rows:
                chk.setChecked(bool(collect.get(key_enabled, True)))
                spin.setValue(int(collect.get(key_poll, 200) or 200))
        storage = doc.get("storage") or {}
        if storage:
            self._storage_enable.setChecked(bool(
                storage.get(proto.CFGK_STORAGE_ENABLED, True)))
            self._storage_path.setText(str(
                storage.get(proto.CFGK_STORAGE_PATH, "") or ""))
            self._storage_sample.setValue(int(
                storage.get(proto.CFGK_STORAGE_SAMPLE_MS, 5000) or 5000))
        db_path = str(doc.get("storageDbPath", "") or "")
        self._db_path_label.setText(
            "当前库文件：%s" % (db_path or "未启用（落盘关闭）"))

    def _apply_collect(self):
        if not self._config_gate():
            return
        values = {}
        for key_enabled, chk, key_poll, spin in self._collect_rows:
            values[key_enabled] = chk.isChecked()
            values[key_poll] = spin.value()
        self.ctx.set_status("正在应用采集配置…")

        def _done(doc):
            # [网络线程] 只发信号/走状态栏信号，不直接碰控件。
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("采集配置已生效（开关即时，周期下一拍）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set("collect", values, on_done=_done)

    def _apply_storage(self):
        if not self._config_gate():
            return
        values = {
            proto.CFGK_STORAGE_ENABLED: self._storage_enable.isChecked(),
            proto.CFGK_STORAGE_PATH: self._storage_path.text().strip(),
            proto.CFGK_STORAGE_SAMPLE_MS: self._storage_sample.value(),
        }
        self.ctx.set_status("正在应用数据存储配置…")

        def _done(doc):
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("数据存储配置已生效（历史/日志/报警入库）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set("storage", values, on_done=_done)

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

        # ---- 配置卡：非 root 整体只读（防止改了应用不上还以为生效）----
        root = self.ctx.is_root()
        for _key_e, chk, _key_p, spin in self._collect_rows:
            chk.setEnabled(root)
            spin.setEnabled(root)
        self._collect_apply.setEnabled(root)
        self._storage_enable.setEnabled(root)
        self._storage_path.setEnabled(root)
        self._storage_sample.setEnabled(root)
        self._storage_save.setEnabled(root)

        # ---- 配置回显兜底 ----
        # 切到本页时可能还没连上（on_enter 拉空过）；连上后的第一拍补拉。
        if not self.ctx.bridge.is_connected():
            self._config_fetched = False
        elif not self._config_fetched:
            self._fetch_config(silent=True)


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
