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

from PyQt5.QtCore import QEvent, QPoint, Qt, QDateTime, QTimer, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QPainter, QPen, QStandardItem, \
    QStandardItemModel
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QDialog, QFileDialog,
                             QFrame, QGridLayout, QHBoxLayout, QHeaderView,
                             QLabel, QLineEdit, QProgressBar, QPushButton,
                             QScrollArea, QSpinBox, QTableWidget,
                             QTableWidgetItem, QDateTimeEdit, QVBoxLayout,
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
                "QLabel { font-size: 22px; font-weight: bold; color: #1A1A1A;"
                " background: transparent; }")
            lbl_desc = QLabel(desc)
            lbl_desc.setAlignment(Qt.AlignCenter)
            lbl_desc.setWordWrap(True)
            lbl_desc.setStyleSheet(
                "QLabel { font-size: 18px; color: #5A6572;"
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
                "QPushButton { font-size: 36px; font-weight: bold; }")
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
            btn.setStyleSheet("QPushButton { font-size: 24px; font-weight: bold; }")
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

    #: 每页条数可选档（**由客户端定**，服务端只负责夹到协议上限 50）。
    #: 10 是板端小屏一屏能看全的行数；50 是 16KB 载荷的物理上限。
    PAGE_SIZES = (10, 20, 50)

    #: 合法墙钟毫秒下界（2020-01-01），与服务端 store.VALID_TS_MIN 同值。
    #: 早于此的 ts 视为坏数据（旧版本误写 monotonic 的 1970 年行），
    #: 不拿它当"最前一条数据的时间"。
    VALID_TS_MIN = 1577836800000

    #: 服务端分页应答 / 失败（网络线程 emit，UI 线程收 —— Qt 线程规则）。
    pageReady = pyqtSignal(dict)
    pageFailed = pyqtSignal(str)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("历史数据", "服务端历史库，分页浏览")
        self._rows = []              # [(texts, abnormal), ...] 旧→新（当前页）
        self._last_sample_ms = 0

        # ---- 数据来源 / 分页状态 ----
        #: "server"=显示服务端某一页；"local"=未连接/未落盘时的本地缓存。
        self._data_mode = "local"
        #: 当前页序号（0 基）与页大小。
        self._page_index = 0
        self._page_size = proto.HISTORY_PAGE_DEFAULT
        #: 本窗口总行数（服务端同口径计数）与全库行数（实时增长）。
        self._total = 0
        self._db_rows = 0
        self._latest_ts = 0
        #: 分页窗口锚点（epoch ms）。**首屏取一次、翻页沿用**——库在持续
        #: 入库，窗口若跟着"现在"走，翻到第 2 页时前面的行已被顶走，
        #: 相邻两页会重复/漏行。
        self._window_end = None
        #: 在途请求（同一时刻只允许一个，翻页连点不会叠出乱序应答）。
        self._inflight = False
        #: 在途请求的类型："page"（要整页数据）/"probe"（只要总数）。
        self._pending_kind = "page"
        self._pending_index = 0
        self._live_timer = QTimer(self)
        self._live_timer.setInterval(3000)
        self._live_timer.timeout.connect(self._live_tick)
        self.pageReady.connect(self._on_page_ready)
        self.pageFailed.connect(self._on_page_failed)

        self._table = DataTable()
        self._table.set_headers(self.COLUMNS)
        # [列宽：按内容给 per-列下限，再让剩余宽度按权重分]
        #
        # 全局最小档是 72px，但**表头文字**比数据行更长：
        # "浊度1(NTU)"/"电箱温度(℃)"/"DI(32位)" 都要 ~95px 才不截断，
        # 时间列 'yyyy-MM-dd HH:MM:SS' 要 165px。15 列 × 72 = 1080px，
        # 1400 宽的窗口视口约 1150px，按权重分下来每列被压到刚过下限，
        # 表头全变成 "浊度1(NTU" / "DI(32位" 这种（截图反馈）。
        # 故这里给每列一个按其表头/典型内容量出来的下限；下限总和超出
        # 视口时宁出横向滚动条，也不把文字截断（DataTable 的分配口径）。
        self._table.set_column_weights(
            (3, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 3, 3),
            min_widths={0: 165, 1: 78, 2: 96, 3: 96, 4: 96, 5: 92,
                        6: 96, 7: 96, 8: 104, 9: 92, 10: 88, 11: 100,
                        12: 100, 13: 92, 14: 92})

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
        #: 用户是否手动改过起始时间（防止默认回填冲掉人家输入）。
        #: 用 textEdited 而不是 textChanged —— 后者连程序 setText 也算，
        #: 那样 `_apply_default_start` 自己填一次就把标记置真了。
        self._f_start_touched = False
        self._default_start_done = False
        self._f_start.textEdited.connect(self._on_start_edited)
        fbar.addWidget(self._f_start)
        fbar.addWidget(QLabel("结束"))
        self._f_end = TimePickerEdit("yyyy-MM-dd HH:MM 或 HH:MM")
        self._f_end.setMaximumWidth(120)
        fbar.addWidget(self._f_end)
        # [「列」= 显示哪些列，不是筛选哪些列] 用户口径（截图反馈）：
        # 「列多选框你要测试，只显示勾选的列，默认是全部勾选」。
        # 故：默认全勾（= 15 列全显示）、勾中的列才显示、至少留 1 列。
        # 勾选变化**立即生效**（不用点「筛选」——那不是筛选条件而是视图设置）。
        fbar.addWidget(QLabel("列"))
        self._f_col = MultiCheckCombo(self.COLUMNS, all_text="全部列",
                                      initial="all", min_checked=1)
        # [用最小宽而不是 setMaximumWidth] 它现在是按钮（不是下拉框），
        # 文字是"时间、模式、输出频率(Hz)…"这类汇总；给了最大宽会把文字
        # 用省略号截掉、看不出勾了哪些列。最小宽保证"全部列（全部）"放得下，
        # 勾得少时按内容收缩（横条空间留给关键字框）。
        self._f_col.setMinimumWidth(150)
        self._f_col.setMaximumWidth(300)
        self._f_col.setToolTip("勾选的列才显示（默认全部勾选，至少保留一列）")
        self._f_col.selectionChanged.connect(self._on_columns_changed)
        fbar.addWidget(self._f_col)
        fbar.addWidget(QLabel("关键字"))
        self._f_text = QLineEdit()
        self._f_text.setPlaceholderText("包含…（空=不限）")
        # [键盘类型声明] 关键字筛选是业务文本（可能中文），要拼音全键。
        # 显式声明一次而不是靠 hints 推断：QLineEdit 缺省无 hints 本来就
        # 落拼音键盘，写明是为了让「这个框要什么键盘」在页面代码里可见。
        from .softkeyboard import KIND_PINYIN, set_keyboard_kind
        set_keyboard_kind(self._f_text, KIND_PINYIN)
        fbar.addWidget(self._f_text, 1)
        btn_filter = QPushButton("筛选")
        btn_filter.clicked.connect(self._apply_filter)
        fbar.addWidget(btn_filter)
        btn_clear = QPushButton("清除筛选")
        btn_clear.clicked.connect(self._clear_filter)
        fbar.addWidget(btn_clear)
        # [计数标签不放筛选行] 筛选行控件已满（1400 宽时"本页显示 20 行"
        # 会被挤到窗口外），移到下面的分页行跟页码/总数一起看。
        self._count_label = QLabel("")
        self._count_label.setStyleSheet(theme.caption_label_qss())
        self.root.addLayout(fbar)

        # ---- 分页行：页大小 + 上下页 + 页码 + 总数 + 刷新 ----
        # [为什么分页控件在表上方] 板端竖屏时表格占满剩余高度，翻页钮在
        # 表下方会被挤出屏幕（要滚到底才能点下一页）；放筛选行下面常驻。
        pbar = QHBoxLayout()
        pbar.setSpacing(8)
        pbar.addWidget(QLabel("每页"))
        self._page_size_box = QComboBox()
        for n in self.PAGE_SIZES:
            self._page_size_box.addItem("%d 条" % n, n)
        idx = self._page_size_box.findData(self._page_size)
        self._page_size_box.setCurrentIndex(idx if idx >= 0 else 0)
        self._page_size_box.setMaximumWidth(96)
        self._page_size_box.currentIndexChanged.connect(self._on_page_size)
        pbar.addWidget(self._page_size_box)
        self._btn_prev = QPushButton("上一页")
        self._btn_prev.clicked.connect(self._prev_page)
        pbar.addWidget(self._btn_prev)
        self._page_label = QLabel("")
        self._page_label.setStyleSheet(theme.caption_label_qss())
        pbar.addWidget(self._page_label)
        self._btn_next = QPushButton("下一页")
        self._btn_next.clicked.connect(self._next_page)
        pbar.addWidget(self._btn_next)
        self._total_label = QLabel("")
        self._total_label.setStyleSheet(theme.caption_label_qss())
        pbar.addWidget(self._total_label)
        pbar.addWidget(self._count_label)
        pbar.addStretch(1)
        self._btn_reload = QPushButton("刷新")
        self._btn_reload.clicked.connect(self._reload_current)
        pbar.addWidget(self._btn_reload)
        self.root.addLayout(pbar)

        # ---- 工具行：折线图 + 滚动锁定 + 导出 ----
        bar = QHBoxLayout()
        bar.setSpacing(8)
        bar.addWidget(QLabel("折线列"))
        # [折线列也做多选] 用户要求：「折线列的下拉也要做类似的设计可以
        # 多选」。与「列」显示开关同一个控件（MultiCheckCombo），交互一致
        # （点项不收起、点外面才收），只是这里 initial="none" + 至少 1 列
        # ——没有勾任何列就没有线可画，所以点「生成折线图」时提示。
        # 只列**数值列**（时间/模式/搅拌状态是文本，画不了折线）。
        self.CHART_COLUMNS = [c for c in range(len(self.COLUMNS))
                              if c not in (0, 1, 2)]
        self._chart_col = MultiCheckCombo(
            [self.COLUMNS[c] for c in self.CHART_COLUMNS],
            all_text="选择列…", initial="none", min_checked=0)
        # 默认勾前一个（原来是下拉默认选第一项，行为对齐）
        self._chart_col._model.item(0).setCheckState(Qt.Checked)
        self._chart_col.setMinimumWidth(150)
        self._chart_col.setMaximumWidth(300)
        self._chart_col.setToolTip("可多选：勾中的列各画一条线（至少选一列）")
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

        # 初始视图：全部列可见（构造期 selectionChanged 被跳过，这里补一次）。
        self._table.set_visible_columns(self.visible_columns())

    # ==================================================================
    #                    入页/离页：服务端分页取数
    # ==================================================================
    def on_enter(self):
        """入页：立刻拉第一页服务端历史（这是用户提的原始需求）。

        [为什么入页才拉] 历史页是"看的时候才需要"的重数据；常驻拉取会
        让服务端在没人看的时候也做全表排序+计数。离页即停（见 on_leave）。
        """
        self._enter_server_mode()

    def on_leave(self):
        """离页停轮询：省掉无谓的 SQL 与流量（页面还在，数据不动）。"""
        self._stop_live()

    def _enter_server_mode(self):
        """切到服务端数据源并取第 0 页（不可用时退回本地缓存）。"""
        bridge = self.ctx.bridge
        if not (bridge.is_connected() if hasattr(bridge, "is_connected")
                else False):
            self._fall_back_local("未连接服务端")
            return
        self._data_mode = "server"
        self._page_index = 0
        self._window_end = int(time.time() * 1000)   # 锚定"现在"
        self._request_page(0)
        self._start_live()

    def _fall_back_local(self, reason):
        """服务端不可用：退回本地缓存并在状态栏说明（不装作有数据）。"""
        self._data_mode = "local"
        self._stop_live()
        self._total_label.setText("服务端不可用（%s）" % reason)
        self._page_label.setText("")
        self._btn_prev.setEnabled(False)
        self._btn_next.setEnabled(False)
        if not self._rows:
            self.ctx.set_status("历史数据：%s，显示本地缓存" % reason)

    def _stop_live(self):
        if self._live_timer.isActive():
            self._live_timer.stop()

    def _start_live(self):
        """在历史页时周期刷新总数（"总数要告知并实时获取"）。"""
        if not self._live_timer.isActive():
            self._live_timer.start()

    def _live_tick(self):
        """周期刷新（"总数要告知并实时获取，如果在历史数据界面的话"）。

        两种节奏，取决于用户在看第几页：

          · **第 1 页**（最新）→ 带着**新锚点**整页重取：新采样的行会
            出现在表头，总数跟着涨。这正是用户说的"实时获取"。
          · **第 N 页（N>0）**→ 只探一次**实时总数**，不动页内容。
            用户正在看第 5 页的历史，页面内容不能因为库又进了两行就整体
            跳一格（那样每 3 秒内容全变，根本没法看）。
        """
        if self._data_mode != "server" or self._inflight:
            return
        if self._page_index == 0:
            self._window_end = int(time.time() * 1000)   # 换新锚点 = 追最新
            self._request_page(0)
        else:
            self._request_page(self._page_index, probe_only=True)

    # ---- 请求 ----

    def _request_page(self, index, probe_only=False):
        """向服务端要第 index 页（或在途探总数）。"""
        if self._inflight:
            return
        bridge = self.ctx.bridge
        self._inflight = True
        self._pending_kind = "probe" if probe_only else "page"
        self._pending_index = int(index)
        self._window_end = self._window_end or int(time.time() * 1000)

        def _done(doc):
            # 网络线程 → 信号排队回 UI 线程（Qt 线程规则，不可省）。
            self.pageReady.emit(doc)

        def _err(msg):
            self.pageFailed.emit(str(msg))

        try:
            lo_ms, hi_ms = (self._server_time_range() or (None, None))
            # [所有筛选条件都下推] 时间、关键字、列选择一起进服务端同一条
            # WHERE。之前关键字/列留在客户端本地筛，分页在服务端 → 客户端
            # 只能在拿到的那一页里搜，命中行绝大多数在别的页上，界面表现
            # 就是"关键字筛选无效"（用户实测反馈：搜"运行"本页显示 0 行，
            # 但共 3790 条）。
            kw = self._f_text.text().strip() or None
            kw_cols = self.visible_columns()
            bridge.history_page(
                offset=index * self._page_size, limit=self._page_size,
                before_ts=self._window_end, lo_ts=lo_ms, hi_ts=hi_ms,
                kw=kw, kw_cols=kw_cols,
                on_done=_done, on_error=_err)
        except Exception as exc:                        # noqa: BLE001
            self._inflight = False
            self._on_page_failed("%s: %s" % (type(exc).__name__, exc))

    def _on_page_failed(self, msg):
        self._inflight = False
        if self._data_mode != "server":
            return
        self._total_label.setText("读取失败：%s" % msg)
        self.ctx.set_status("历史数据读取失败：%s" % msg)

    def _on_page_ready(self, doc):
        """应答到货（**UI 线程**）：更新分页状态并重铺表格。"""
        self._inflight = False
        if self._data_mode != "server":
            return
        if not doc.get("ok"):
            self._on_page_failed(doc.get("err") or "服务端拒绝")
            return

        cols = list(doc.get(proto.HIST_KEY_COLS) or proto.HISTORY_COLUMNS)
        self._total = int(doc.get(proto.HIST_KEY_TOTAL) or 0)
        self._db_rows = int(doc.get(proto.HIST_KEY_DB_ROWS) or 0)
        latest = int(doc.get(proto.HIST_KEY_LATEST_TS) or 0)
        earliest = int(doc.get(proto.HIST_KEY_EARLIEST_TS) or 0)
        changed = latest != self._latest_ts
        self._latest_ts = latest
        self._apply_default_start(earliest)

        if self._pending_kind == "page":
            self._page_index = self._pending_index
            rows = doc.get(proto.HIST_KEY_ROWS) or []
            self._rows = [self._server_row_texts(r, cols) for r in rows]
            self._rebuild()

        # 页码夹取：库被裁剪（ring 策略）后总页数可能变少，别停在空页上。
        pages = self._page_count()
        if pages and self._page_index >= pages:
            self._page_index = pages - 1
            if self._pending_kind == "probe":
                self._request_page(self._page_index)
                return
        self._update_pager_ui()
        if changed and self._pending_kind == "probe":
            # 服务端有新数据入库：不打扰当前页内容，只在总数上体现。
            pass

    def _on_start_edited(self, _text):
        self._f_start_touched = True
        self._default_start_done = True

    def _clear_inputs(self):
        """只清时间/关键字输入框，**不动列显示、不动页码、不发请求**。

        与 `_clear_filter` 的区别：`_clear_filter` 是用户点「清除筛选」的
        完整动作（会复位页码并重取数据）；这个是内部/测试用的轻量复位，
        用于连续测各种筛选组合时把输入清干净，不引入额外网络往返。
        """
        self._f_start.clear()
        self._f_end.clear()
        self._f_text.clear()

    def _apply_default_start(self, earliest_ts):
        """入页时给「起始」填默认 = 服务端最前一条数据的时间。

        [用户要求] 「起始时间可以有个默认值吧，直接看最前的一条数据的时间」。
        这样一进历史页就能看到数据从什么时候开始，不必先猜一个时间。

        [口径三要点]
          1. **只填一次**（`_default_start_done`）：否则每次翻页/探活都把
             用户改过的时间覆盖回去，等于没法调筛选。
          2. **用户碰过就不填**（`_f_start_touched`）：如果用户先手输了
             时间、应答才回来，不能把人家的输入冲掉。
          3. **只显示、不触发筛选**：填进去的是"看全貌"的起点，
             `ts >= 最前一条` 恒真，等价于不筛；真正的筛选仍由「筛选」
             按钮触发。避免"框里有个时间、但总数悄悄变少"的困惑。
             用户点「清除筛选」可随时清空（此后不再自动回填）。
        """
        if getattr(self, "_default_start_done", False):
            return
        if not earliest_ts or earliest_ts < self.VALID_TS_MIN:
            return                       # 空库 / 服务端老版本没带该键
        if self._f_start.text().strip() or self._f_start_touched:
            return                       # 用户已经填了/碰过，不覆盖
        self._default_start_done = True
        text = time.strftime("%Y-%m-%d %H:%M:%S",
                             time.localtime(earliest_ts / 1000.0))
        # 用 setText 而不是 _set_from_picker：后者会 emit timePicked，
        # 语义是"用户在日历上选了时间"，这里只是程序填默认值。
        self._f_start.setText(text)
        self.ctx.set_status("已载入服务端历史：最早一条 %s（可改起始时间筛选）"
                            % text)

    def _server_row_texts(self, raw, cols):
        """服务端一行（按 cols 顺序的值数组）→ (显示文本列表, 是否异常)。
        [为什么在客户端做格式化] 库里存的是原始值（numeric + 模式文本 +
        单位码文本），而表格列有各自的显示口径（"25.00"、"--"、"0x000000FF"）。
        服务端只发原值，格式化集中在界面这一处，将来改显示不用动协议。

        异常判定与本地采样同口径，但只能从**已入库的列**推导：
        变频器状态含"故障"、浊度≥阈值、压差列带说明文字。
        """
        rec = dict(zip(cols, raw))

        def num(key, fmt="%.2f"):
            value = rec.get(key)
            return "--" if value is None else fmt % float(value)

        ts = rec.get("ts")
        stamp = (time.strftime("%Y-%m-%d %H:%M:%S",
                               time.localtime(ts / 1000.0))
                 if ts else "--")
        flow = rec.get("flow")
        flow_unit = rec.get("flow_unit") or ""
        total = rec.get("flow_total")
        total_unit = rec.get("total_unit") or ""
        press_note = rec.get("press_note") or ""
        press = rec.get("press_mpa")
        di = rec.get("di")
        do = rec.get("do")
        vfd_state = rec.get("vfd_state") or "--"

        texts = [
            stamp,
            rec.get("mode") or "--",
            vfd_state,
            num("freq_hz"),
            num("current_a"),
            num("rpm", "%.0f"),
            num("turb1"), num("turb2"),
            "--" if flow is None else "%.2f %s" % (float(flow), flow_unit),
            "--" if total is None else "%.2f %s" % (float(total), total_unit),
            press_note or num("press_mpa", "%.3f"),
            num("temp", "%.1f"), num("hum", "%.1f"),
            "--" if di is None else "0x%08X" % (int(di) & 0xFFFFFFFF),
            "--" if do is None else "0x%08X" % (int(do) & 0xFFFFFFFF),
        ]
        abnormal = ("故障" in vfd_state
                    or bool(press_note)
                    or (rec.get("turb1") is not None
                        and float(rec["turb1"]) >= self.TURBIDITY_WARN_NTU)
                    or (rec.get("turb2") is not None
                        and float(rec["turb2"]) >= self.TURBIDITY_WARN_NTU))
        return (texts, abnormal)

    # ---- 分页操作 ----

    def _page_count(self):
        if self._page_size <= 0:
            return 0
        return max(1, (self._total + self._page_size - 1) // self._page_size)

    def _update_pager_ui(self):
        pages = self._page_count()
        self._page_label.setText("第 %d/%d 页" % (self._page_index + 1,
                                                  max(1, pages)))
        shown = len(self._rows)
        self._total_label.setText(
            "共 %d 条（本页 %d）　库内共 %d 条"
            % (self._total, shown, self._db_rows))
        self._btn_prev.setEnabled(self._page_index > 0)
        self._btn_next.setEnabled(self._page_index + 1 < pages)

    def _prev_page(self):
        if self._data_mode != "server" or self._page_index <= 0:
            return
        self._request_page(self._page_index - 1)

    def _next_page(self):
        if self._data_mode != "server":
            return
        if self._page_index + 1 >= self._page_count():
            return
        self._request_page(self._page_index + 1)

    def _on_page_size(self):
        """改页大小：回到第 1 页（行号口径变了，继续停在原页号没意义）。"""
        size = self._page_size_box.currentData()
        if not isinstance(size, int) or size <= 0 or size == self._page_size:
            return
        self._page_size = size
        if self._data_mode == "server":
            self._page_index = 0
            self._request_page(0)
        else:
            self._rebuild()

    def _reload_current(self):
        """手动刷新：重取当前页，并**续用原窗口锚点**（行内容不乱跳）。"""
        if self._data_mode != "server":
            self._enter_server_mode()
            return
        self._request_page(self._page_index)

    # ==================================================================
    #                  本地缓存（服务端不可用时的退路）
    # ==================================================================
    def refresh(self):
        # 服务端模式：表格内容是某一页历史，**不再追加实时采样**
        # （两者时间线不同源，混在一起会让"第 3 页"里冒出刚采的行）。
        if self._data_mode == "server":
            return
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

    # ---- 显示列（列多选框）----

    def visible_columns(self):
        """当前显示（勾中）的列下标列表，升序；**至少一列**。

        数据行始终是完整 15 列（`_rows` 的文本列表按 COLUMNS 固定顺序存），
        显示列只是"看不看"的问题 —— 取数、缓存、排序、图表、导出都按
        完整行来，只有铺表和导出列头按这里筛。
        """
        cols = sorted(self._f_col.checked_indexes())
        return cols or list(range(len(self.COLUMNS)))

    def _on_columns_changed(self):
        """列勾选变化：立即重铺表头/数据（视图设置，不用点「筛选」）。

        [关键字激活时列也是**筛选条件**] 服务端模式下 kwCols 进了同一条
        WHERE（关键字只在勾选的显示列里匹配）。此时改勾选会改变结果集本身，
        不只是"看哪几列"——必须重取第 0 页，否则页内容还是按旧列集合筛的。
        关键字为空时列纯粹是视图设置，只本地重铺即可，不发网络往返。
        """
        if not hasattr(self, "_table"):
            # 构造期 _sync_text 会 emit 一次 selectionChanged（此时表格还
            # 没建）—— 忽略即可，__init__ 末尾会主动铺一次。
            return
        cols = self.visible_columns()
        headers = [self.COLUMNS[c] for c in cols]
        # [必须重建表头标签] 表格列数是固定的 15，隐藏列只是不显示；
        # 表头标签按**可见列**重设会让"第 0 列"总是显示第一个可见列的
        # 名字——错位。故这里保持标签与列号一一对应（隐藏列标签无所谓），
        # 只有列宽分配器需要知道哪些列可见。
        self._table.set_visible_columns(cols)
        self._rebuild()
        if self._data_mode == "server" and self._f_text.text().strip():
            self._page_index = 0
            self._window_end = int(time.time() * 1000)
            self._request_page(0)
        self.ctx.set_status("显示 %d/%d 列：%s"
                            % (len(cols), len(self.COLUMNS),
                               "、".join(headers) if len(headers) <= 4
                               else "%s…" % "、".join(headers[:4])))

    def _rebuild(self):
        rows = self._visible_rows()
        table = self._table.table
        cols = self.visible_columns()
        table.setRowCount(len(rows))
        for r, (row, abnormal) in enumerate(rows):      # 视图序（默认最新在上）
            for c in cols:
                text = row[c] if c < len(row) else ""
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
        if self._data_mode == "server":
            # 服务端模式的总数在分页行里（"共 N 条（本页 M）"），这里只说
            # 本页经筛选/排序后还剩几行 —— 与"共 N 条"口径不同，分开写。
            self._count_label.setText("本页显示 %d 行" % len(rows))
        else:
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
        """时间范围 + 关键字过滤后的快照（**时间正序**，旧→新）。

        图表走这里（X 轴必须时间正序）；排序视图在其结果上重排。

        [服务端模式不再本地重复筛] 时间/关键字/列三者已在服务端同一条
        WHERE 里筛过；这里再做一遍不仅多余，还会**口径打架**：本地
        "纯时刻=当天"规则与 SQL 的纯毫秒范围不同，会把服务端正确返回的
        行又滤掉。故服务端模式直接返回全部页内行（排序仍生效）。
        本地缓存模式仍走下面完整的本地筛选（那是它唯一的筛选路径）。

        [关键字匹配范围·本地模式] 在**当前显示的列**里找（勾掉的列不参与）：
        全列都显示时等价于全行拼接。

        [时间口径·本地模式] 行内时间是完整 'yyyy-MM-dd HH:MM:SS'
        （ISO 文本序 = 时间序）；筛选两侧按 _norm_stamp 归一——只填时刻
        按"当天"比后缀，带日期比全键。

        [纯时刻 = **当天**，不是"任意一天的时刻"] mode 'T' 时先卡 stamp
        的日期段 == 今天，再比时刻后缀；这样"08:00 到 13:00"真正只筛
        今天那一批（跨天历史里旧实现只比 stamp[-8:] 会误命中）。
        """
        if self._data_mode == "server":
            return list(self._rows)
        lo = self._norm_stamp(self._f_start.text(), is_end=False)
        hi = self._norm_stamp(self._f_end.text(), is_end=True)
        cols = self.visible_columns()
        kw = self._f_text.text().strip()
        today = time.strftime("%Y-%m-%d")
        out = []
        for row, abnormal in self._rows:
            stamp = row[0]
            # 纯时刻条件：行必须是**当天**的数据（日期段严格相等）
            if ((lo is not None and lo[0] == "T")
                    or (hi is not None and hi[0] == "T")):
                if stamp[:10] != today:
                    continue
            if lo is not None:
                value = stamp[-8:] if lo[0] == "T" else stamp
                if value < lo[1]:
                    continue
            if hi is not None:
                value = stamp[-8:] if hi[0] == "T" else stamp
                if value > hi[1]:
                    continue
            if kw:
                hit = any(c < len(row) and kw in row[c] for c in cols)
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
        if self._data_mode == "server":
            # [所有筛选条件都下推服务端] 时间 / 关键字 / 列选择一起进同一条
            # WHERE。之前只推时间、关键字留在客户端本地筛：客户端只拿到一页，
            # 只能在那一页里搜，命中行绝大多数在别的页上 → 界面就是「关键字
            # 筛选无效」（用户实测：搜"运行"本页显示 0 行但共 3790 条）。
            # 故哪怕只改了关键字、时间框一个字没动，也必须重取第 0 页：
            # 不重取的话 `_filtered_rows` 在服务端模式直接放行，关键字等于没生效。
            self._page_index = 0
            self._window_end = int(time.time() * 1000)
            self._request_page(0)
            return
        self._rebuild()

    def _server_time_range(self):
        """当前筛选的起止时间 → (lo_ms, hi_ms)；无时间条件返回 None。

        [只推**时间**范围这一项] 关键字与列集合由 `_request_page` 另行读
        `_f_text` / `visible_columns()` 一并下推（见 bridge.history_page 的
        kw/kwCols）。本方法专职把 _norm_stamp 的 (mode, key) 归一成 epoch
        毫秒；时间有 ts 索引，是结构化条件，推下去又快又准。
        """
        lo = self._norm_stamp(self._f_start.text(), is_end=False)
        hi = self._norm_stamp(self._f_end.text(), is_end=True)
        if lo is None and hi is None:
            return None
        return (self._stamp_to_ms(lo), self._stamp_to_ms(hi))

    @staticmethod
    def _stamp_to_ms(stamp):
        """_norm_stamp 的 (mode, key) → epoch 毫秒；None 原样返回。"""
        if stamp is None:
            return None
        _mode, key = stamp
        fmt = "%Y-%m-%d %H:%M:%S" if len(key) == 19 else "%H:%M:%S"
        if len(key) == 19:
            when = time.strptime(key, fmt)
        else:
            # 纯时刻 = 当天语义（与本地筛选口径一致）
            when = time.strptime(time.strftime("%Y-%m-%d ") + key,
                                 "%Y-%m-%d %H:%M:%S")
        return int(time.mktime(when) * 1000)

    def _clear_filter(self):
        """清筛选条件（时间/关键字）。

        [不动列显示] 列多选是**视图设置**（看哪些列），不是筛选条件；
        点「清除筛选」把用户的列选择一并复位会让人莫名其妙（我只要清时间
        范围，结果列也全回来了）。
        """
        self._f_start.clear()
        self._f_end.clear()
        self._f_text.clear()
        # [清筛选后不自动回填默认值] 用户明确点了"清除"，若探活应答回来
        # 又把最早时间填回去，看着像没清干净。保持清空，直到下次入页。
        self._default_start_done = True
        if self._data_mode == "server":
            self._page_index = 0
            self._request_page(0)
            return
        self._rebuild()

    # ---- 折线图 / 导出 ----

    def _show_chart(self):
        """按当前筛选结果画选中列（可多列）的折线图（时间正序，X=时间）。

        [多列口径] 勾中的每一列各画一条线，颜色按 `LineChartDialog.
        SERIES_COLORS` 依次取、图例在标题下方。各列量纲不同（频率 Hz /
        电流 A / 浊度 NTU）时**不做双 Y 轴**：图是"看趋势"不是"读精确值"，
        每列各自归一化到绘图区高度（图例里标 min/max），否则小量纲的线
        会被压成一条直线。这一点在标题下写明，避免误读。
        """
        cols = [self.CHART_COLUMNS[i]
                for i in self._chart_col.checked_indexes()
                if i < len(self.CHART_COLUMNS)]
        if not cols:
            self.ctx.set_status("请先在「折线列」里勾选至少一列")
            return
        rows = self._filtered_rows()                   # 时间正序
        series = []
        for col in cols:
            points = []
            for row, _abnormal in rows:
                value = self._cell_float(row[col])
                if value is None:
                    continue
                stamp = self._stamp_text_to_ms(row[0])
                if stamp is None:
                    continue
                points.append((stamp, value))
            if len(points) >= 2:
                series.append((self.COLUMNS[col], points))
        if not series:
            self.ctx.set_status("可画的数值点不足 2 个（换列或放宽筛选）")
            return
        dialog = LineChartDialog(series, self)
        dialog.show()

    @staticmethod
    def _stamp_text_to_ms(text):
        """行内时间文本 'yyyy-MM-dd HH:MM:SS' → epoch 毫秒（失败返回 None）。

        [为什么图表用毫秒而不是文本] 缩放/平移是按**时间比例**算的，
        文本比较不了大小也插不了值；毫秒还能算出缩放后视口的真实时间范围
        显示在标题上（"09:12:05 ~ 09:12:40，已缩放"）。
        """
        try:
            return int(time.mktime(time.strptime(str(text),
                                                 "%Y-%m-%d %H:%M:%S")) * 1000)
        except (ValueError, TypeError):
            return None

    def _export_data(self):
        """导出**当前可见视图**（含筛选、排序、显示列）为 CSV 或 Excel。

        [只导可见列] 「导出内容 = 界面看到的样子」（原口径）在有了列显示
        开关之后自然延伸为「勾掉的列不导出」：用户在界面上把 12 列收成
        3 列，导出的就是那 3 列。想要全量就把列全勾上再导。

        [格式由扩展名决定] 用户在保存框里选 .csv → CSV（utf-8-sig，
        Excel 双击直开）；选 .xlsx → Excel（走 xlsx_bridge 动态库，
        库缺失时提示而不是静默降级——「以为是 Excel 其实是 CSV」
        比直接报错更误事）。
        """
        rows = self._visible_rows()
        if not rows:
            self.ctx.set_status("无可导出的数据")
            return
        cols = self.visible_columns()
        headers = [self.COLUMNS[c] for c in cols]
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
            data = [[r[c] if c < len(r) else "" for c in cols]
                    for r, _abn in rows]
            try:
                xlsx_writer.write_rows(path, "历史数据", headers,
                                       data, {0: 10})
            except Exception as exc:                 # noqa: BLE001
                self.ctx.set_status("Excel 导出失败：%s" % exc)
                return
        else:
            if not path.lower().endswith(".csv"):
                path += ".csv"
            with open(path, "w", encoding="utf-8-sig", newline="") as fh:
                writer = csv.writer(fh)
                writer.writerow(headers)
                for texts, _abnormal in rows:
                    writer.writerow([texts[c] if c < len(texts) else ""
                                     for c in cols])
        self.ctx.set_status("已导出 %d 条 × %d 列 → %s"
                            % (len(rows), len(cols), path))

    def _export_csv(self):
        """旧入口（保留兼容）：直接走 CSV 分支。"""
        rows = self._visible_rows()
        if not rows:
            self.ctx.set_status("无可导出的数据")
            return
        cols = self.visible_columns()
        default = "hsf_history_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, _selected = QFileDialog.getSaveFileName(
            self, "导出历史数据", default, "CSV (*.csv)")
        if not path:
            return
        if not path.lower().endswith(".csv"):
            path += ".csv"
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow([self.COLUMNS[c] for c in cols])
            for texts, _abnormal in rows:
                writer.writerow([texts[c] if c < len(texts) else ""
                                 for c in cols])
        self.ctx.set_status("已导出 %d 条 → %s" % (len(rows), path))


class LineChartDialog(QDialog):
    """历史数据折线图：**多序列 + 触摸/鼠标双指缩放与拖动**（QPainter 手绘，零依赖）。

    [为什么不用 matplotlib] 板端 Python 没有（也装不了）matplotlib，
    而历史页要在板上跑；QPainter 自绘折线 + 网格 + 刻度 + 交互几百行，
    桌面/板端表现一致。数据来自历史页当前**筛选后**的视图（时间正序），
    '--'/非数值点直接跳过（断点不补——编造的数据比缺口更危险）。

    [多序列] 勾中的每一列各一条线，颜色见 SERIES_COLORS，图例画在标题
    下方。**各列量纲不同时不做双 Y 轴**：每条线按自己可见区间的 min/max
    归一化到绘图区高度，图例里标出该列 min/max。图是"看趋势"不是"读
    精确值"；不归一化的话 0~1 的压差和 0~1500 的转速画在一起，压差会被
    压成一条直线看不出任何波动。

    [交互（移动端手感 + 鼠标适配）]
      · **双指捏合**（QPinchGesture / 原生 TouchEvent）→ 横向缩放时间轴
      · **单指拖动**（触摸）/ **左键拖动**（鼠标）→ 横向平移
      · **滚轮**（鼠标）→ 以指针位置为锚点缩放（触摸屏没有滚轮，两者共存）
      · **双击** / 双击空处 → 复位到全程
      · **单击某点** → 弹出该点信息气泡（时间 + 各序列数值）；再点别处
        或移开就收起。拖动超过阈值不算单击（避免拖完手一松弹气泡）。

    [缩放只作用于时间轴] 纵向由每条线自己的 min/max 自适应，所以放大
    横轴时纵轴会自动跟着那一段的数据缩放 —— 这正是"看某段细节"想要的。

    [非模态] 允许边看图边切筛选条件再点一次"生成折线图"——每次点击
    都弹新图，旧图保留（对比两个时间段用）。
    """

    #: 绘图区边距（左留 Y 轴刻度、下留 X 轴时间标签）。
    MARGIN_L, MARGIN_T, MARGIN_R, MARGIN_B = 72, 66, 26, 46
    GRID_COLOR = "#E3E6EB"
    TEXT_COLOR = "#5A6572"
    #: 多序列配色（顺序取；超过就循环）。第 1 个沿用原单序列蓝色。
    SERIES_COLORS = ("#2E7CE8", "#E8730C", "#2E9E4F", "#B33DC6",
                     "#C62E2E", "#7A6F00", "#0F9BA8", "#8B5A2B")
    #: 缩放上下限：最多放大到只剩 2 个点（更窄就没意义），
    #: 最小不能少于全程（否则会缩到数据外面露出空白）。
    MIN_SPAN_POINTS = 2
    #: 拖动判定阈值（像素）：超过它就不再算"单击"，避免拖完弹气泡。
    TAP_SLOP = 6
    #: 单击后吸附到最近点的水平容差（像素）。
    PICK_TOLERANCE = 28

    def __init__(self, series, parent=None):
        """:param series: [(列名, [(时间文本, 数值), ...]), ...] 时间正序。

        多序列时各序列的点数/时间戳可能不同（某列有 "--" 缺值），故**不**
        按下标对齐，各自按自己的点画；X 轴用**统一的时间范围**（所有序列
        的时间并集）。
        """
        QDialog.__init__(self, parent)
        self.setWindowTitle("折线图 — %s"
                            % ("、".join(name for name, _pts in series)
                               if len(series) <= 3
                               else "%d 条曲线" % len(series)))
        self.resize(920, 520)
        self.setMouseTracking(True)          # 鼠标移动收气泡/找最近点
        self.setCursor(Qt.CrossCursor)
        #: [(name, points, color), ...]
        self._series = []
        for idx, (name, points) in enumerate(series):
            self._series.append((name, list(points),
                                 self.SERIES_COLORS[idx
                                                    % len(self.SERIES_COLORS)]))
        #: 时间轴视口（0..1 = 全程）。x0/x1 是"时间序占比"，不是索引 ——
        #: 各序列点数不同时用占比才对齐。
        self._view_x0 = 0.0
        self._view_x1 = 1.0
        #: 单击选中的点：(序列下标, 点下标) 或 None（气泡）。
        self._picked = None
        #: 拖动/捏合过程中的临时状态。
        self._drag_origin = None         # (x, view_x0, view_x1)
        self._dragged = False
        self._pinch_start = None         # (center_frac, view_x0, view_x1)
        self._press_pos = None

        self.grabGesture(Qt.PinchGesture)
        self.setAttribute(Qt.WA_AcceptTouchEvents, True)

    # ==================================================================
    #                          视口（缩放/平移）
    # ==================================================================

    def _full_span(self):
        """全程时间跨度（毫秒）；只有一个时刻时返回 0。"""
        stamps = [t for _n, pts, _c in self._series for t, _v in pts]
        if not stamps:
            return 0
        return max(stamps) - min(stamps)

    def _time_bounds(self):
        stamps = [t for _n, pts, _c in self._series for t, _v in pts]
        if not stamps:
            return 0, 0
        return min(stamps), max(stamps)

    def view_times(self):
        """当前视口的时间下/上界（毫秒）。测试与绘制共用。"""
        t0, t1 = self._time_bounds()
        span = t1 - t0
        return (t0 + span * self._view_x0, t0 + span * self._view_x1)

    def zoom_at(self, fraction, factor):
        """以时间轴上的 `fraction`(0..1) 为锚点缩放 `factor` 倍。

        :param factor: <1 放大（视口变窄），>1 缩小。
        夹取规则：
          · 视口占比不小于 `MIN_SPAN_POINTS / 总点数`（放大上限）
          · 视口占比不大于 1.0（不能缩到比全程还宽）
          · 平移后窗口不能跑出 [0,1]（否则界面上出现一段空白，
            用户会以为数据丢了）
        """
        if not (0.0 <= fraction <= 1.0) or factor <= 0:
            return
        span = self._view_x1 - self._view_x0
        total_points = sum(len(pts) for _n, pts, _c in self._series) or 1
        min_span = min(1.0, float(self.MIN_SPAN_POINTS) / max(1, total_points))
        new_span = min(1.0, max(min_span, span * factor))
        if abs(new_span - span) < 1e-9:
            return
        # 锚点：缩放前后，fraction 处对应的数据位置不动
        anchor = self._view_x0 + span * fraction
        x0 = anchor - new_span * fraction
        x1 = x0 + new_span
        if x0 < 0.0:
            x0, x1 = 0.0, new_span
        if x1 > 1.0:
            x1, x0 = 1.0, 1.0 - new_span
        self._view_x0, self._view_x1 = x0, x1
        self._picked = None              # 视口变了，选中的点可能已不可见
        self.update()

    def pan_pixels(self, dx_pixels, plot_width):
        """按像素平移时间轴（正值 = 内容右移 = 看更早的数据）。"""
        if plot_width <= 0:
            return
        span = self._view_x1 - self._view_x0
        if span >= 1.0 - 1e-9:
            return                      # 全程已展开，没有可平移的余量
        delta = float(dx_pixels) / float(plot_width) * span
        x0 = self._view_x0 - delta
        x1 = self._view_x1 - delta
        if x0 < 0.0:
            x0, x1 = 0.0, span
        if x1 > 1.0:
            x1, x0 = 1.0, 1.0 - span
        self._view_x0, self._view_x1 = x0, x1
        self.update()

    def reset_view(self):
        """复位到全程（双击/双击空处）。"""
        self._view_x0, self._view_x1 = 0.0, 1.0
        self._picked = None
        self.update()

    def is_zoomed(self):
        return self._view_x0 > 1e-9 or self._view_x1 < 1.0 - 1e-9

    # ==================================================================
    #                       几何：时间/数值 → 像素
    # ==================================================================

    def plot_rect(self):
        w, h = self.width(), self.height()
        return (self.MARGIN_L, self.MARGIN_T,
                w - self.MARGIN_R, h - self.MARGIN_B)

    def _x_at_time(self, stamp):
        t0, t1 = self.view_times()
        l, _t, r, _b = self.plot_rect()
        if t1 <= t0:
            return (l + r) / 2.0
        frac = (stamp - t0) / float(t1 - t0)
        return l + (r - l) * frac

    def _visible_values(self, points):
        """某序列在当前视口内的 (点, 时间, 值) 列表。"""
        t0, t1 = self.view_times()
        return [(t, v) for t, v in points if t0 <= t <= t1]

    def _y_scale(self, points):
        """该序列在视口内的 (vmin, vmax)，已含上下留白。全程一条线也留刻度。"""
        vals = [v for _t, v in self._visible_values(points)]
        if not vals:
            vals = [v for _t, v in points]
        if not vals:
            return 0.0, 1.0
        vmin, vmax = min(vals), max(vals)
        if vmax == vmin:
            return vmin - 1.0, vmax + 1.0
        pad = (vmax - vmin) * 0.08
        return vmin - pad, vmax + pad

    def nearest_point(self, pos):
        """屏幕坐标 → 最近的数据点 (序列下标, 点下标, 距离px) 或 None。

        只在视口内、且横向距离在 `PICK_TOLERANCE` 内的点里挑最近的；
        纵向不设限（手指点在线上/线附近都算，触摸屏上不用瞄得那么准）。
        """
        l, t, r, b = self.plot_rect()
        if not (l - self.PICK_TOLERANCE <= pos.x() <= r + self.PICK_TOLERANCE
                and t - 20 <= pos.y() <= b + 20):
            return None
        best = None
        for si, (_name, points, _color) in enumerate(self._series):
            vmin, vmax = self._y_scale(points)
            for pi, (stamp, value) in enumerate(points):
                px = self._x_at_time(stamp)
                dy = b - (b - t) * (value - vmin) / (vmax - vmin or 1.0)
                dist = abs(px - pos.x())
                if dist > self.PICK_TOLERANCE:
                    continue
                score = (dist, abs(dy - pos.y()))
                if best is None or score < best[3]:
                    best = (si, pi, (stamp, value), score)
        return None if best is None else (best[0], best[1], best[2])

    # ==================================================================
    #                            绘制
    # ==================================================================

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        rect_l, rect_t, rect_r, rect_b = self.plot_rect()
        plot_w, plot_h = rect_r - rect_l, rect_b - rect_t
        if plot_w <= 40 or plot_h <= 40:
            return

        # ---- Y 轴网格（按**第一条序列**的刻度画，多序列时标明是主轴）----
        painter.setPen(QPen(QColor(self.GRID_COLOR), 1))
        font = painter.font()
        font.setPixelSize(11)
        painter.setFont(font)
        first_points = self._series[0][1] if self._series else []
        vmin, vmax = self._y_scale(first_points)
        for i in range(6):
            frac = i / 5.0
            y = rect_t + plot_h * frac
            painter.drawLine(rect_l, int(y), rect_r, int(y))
            painter.setPen(QColor(self.TEXT_COLOR))
            value = vmax - (vmax - vmin) * frac
            painter.drawText(0, int(y) - 6, rect_l - 8, 14,
                             Qt.AlignRight | Qt.AlignVCenter, "%.4g" % value)
            painter.setPen(QPen(QColor(self.GRID_COLOR), 1))

        # ---- X 刻度：视口内至多 6 个时间标签 ----
        t0, t1 = self.view_times()
        if t1 > t0:
            for i in range(6):
                frac = i / 5.0
                stamp = t0 + (t1 - t0) * frac
                x = rect_l + plot_w * frac
                painter.setPen(QColor(self.TEXT_COLOR))
                painter.drawText(int(x) - 46, rect_b + 8, 92, 16,
                                 Qt.AlignCenter,
                                 time.strftime("%H:%M:%S",
                                               time.localtime(stamp / 1000.0)))

        # ---- 各序列折线（只画视口内的点，额外多取一个边界点让线不截断）----
        for _si, (_name, points, color) in enumerate(self._series):
            self._paint_series(painter, points, color,
                               rect_l, rect_t, rect_r, rect_b)

        # ---- 标题 + 图例 ----
        painter.setPen(QColor("#1F4E79"))
        big = painter.font()
        big.setPixelSize(14)
        big.setBold(True)
        painter.setFont(big)
        title = "折线图"
        if self.is_zoomed():
            lo, hi = self.view_times()
            title += "（%s ~ %s，已缩放）" % (
                time.strftime("%m-%d %H:%M:%S",
                              time.localtime(lo / 1000.0)),
                time.strftime("%H:%M:%S", time.localtime(hi / 1000.0)))
        painter.drawText(rect_l, 6, rect_r - rect_l, 20,
                         Qt.AlignLeft | Qt.AlignVCenter, title)
        self._paint_legend(painter, rect_l, 26)

        # ---- 单击气泡 ----
        if self._picked is not None:
            self._paint_tooltip(painter)

    def _paint_series(self, painter, points, color, rect_l, rect_t, rect_r,
                      rect_b):
        """画一条序列：视口裁剪 + 自适应纵向缩放 + 点少时画圆点。"""
        t0, t1 = self.view_times()
        if not points or t1 <= t0:
            return
        vmin, vmax = self._y_scale(points)
        span = (vmax - vmin) or 1.0
        plot_w, plot_h = rect_r - rect_l, rect_b - rect_t

        def px_of(stamp):
            return rect_l + plot_w * (stamp - t0) / float(t1 - t0)

        def py_of(value):
            return rect_b - plot_h * (value - vmin) / span

        # 视口两侧各多取一个点：否则放大后线段在边界处断开，看着像数据没了。
        visible = []
        for i, (stamp, value) in enumerate(points):
            if t0 <= stamp <= t1:
                visible.append((stamp, value))
            elif stamp < t0:
                if i + 1 < len(points) and points[i + 1][0] >= t0:
                    visible.append((stamp, value))
            else:
                if visible:
                    visible.append((stamp, value))
                break

        painter.setPen(QPen(QColor(color), 2))
        for i in range(len(visible) - 1):
            painter.drawLine(
                _px(px_of(visible[i][0])), _px(py_of(visible[i][1])),
                _px(px_of(visible[i + 1][0])), _px(py_of(visible[i + 1][1])))
        if len(visible) <= 160:                  # 点不多时画圆点便于对读
            painter.setBrush(QColor(color))
            painter.setPen(Qt.NoPen)
            for stamp, value in visible:
                painter.drawEllipse(_px(px_of(stamp) - 3),
                                    _px(py_of(value) - 3), 6, 6)

    def _paint_legend(self, painter, x, y):
        """图例：色块 + 列名（+ 该列全程 min/max）。"""
        font = painter.font()
        font.setPixelSize(12)
        font.setBold(False)
        painter.setFont(font)
        cursor = x
        for name, points, color in self._series:
            vals = [v for _t, v in points]
            label = "%s %.4g~%.4g" % (name, min(vals), max(vals)) \
                if vals else name
            width = painter.fontMetrics().width(label) + 34
            if cursor + width > self.plot_rect()[2]:
                break                        # 放不下就截断（不换行，省高度）
            painter.setBrush(QColor(color))
            painter.setPen(Qt.NoPen)
            painter.drawRect(cursor, y + 5, 14, 4)
            painter.setPen(QColor(self.TEXT_COLOR))
            painter.drawText(cursor + 20, y, width - 20, 16,
                             Qt.AlignLeft | Qt.AlignVCenter, label)
            cursor += width
        painter.setBrush(Qt.NoBrush)

    def tooltip_lines(self, stamp):
        """气泡内容行：第一行是完整时间，之后每序列一行「列名：值」。

        [独立成方法以便断言] 离屏平台（QT_QPA_PLATFORM=offscreen）**不绘制
        任何文字**（实测：画一张只有 drawText 的图，出来是全白），所以
        "气泡里有没有对的信息"没法靠截图看，只能靠这个方法 + 断言盯住。
        该时刻没有数据的列**不出现**（编造数值比留空更危险）。
        """
        lines = [time.strftime("%Y-%m-%d %H:%M:%S",
                               time.localtime(stamp / 1000.0))]
        for name, pts, _c in self._series:
            hit = None
            for t, v in pts:
                if t == stamp:
                    hit = v
                    break
            if hit is None:
                continue
            lines.append("%s：%.4g" % (name, hit))
        return lines

    def _paint_tooltip(self, painter):
        """选中的点：画醒目标记 + 信息气泡（时间 + 各序列该时刻的值）。"""
        si, pi, (stamp, value) = self._picked[0], self._picked[1], \
            self._picked[2]
        rect_l, rect_t, rect_r, rect_b = self.plot_rect()
        points = self._series[si][1]
        color = self._series[si][2]
        x = self._x_at_time(stamp)
        vmin, vmax = self._y_scale(points)
        y = rect_b - (rect_b - rect_t) * (value - vmin) / ((vmax - vmin) or 1.0)

        painter.setPen(QPen(QColor(color), 1, Qt.DashLine))
        painter.drawLine(_px(x), rect_t, _px(x), rect_b)
        painter.setBrush(QColor("#FFFFFF"))
        painter.setPen(QPen(QColor(color), 2))
        painter.drawEllipse(_px(x) - 5, _px(y) - 5, 10, 10)

        # 气泡内容：时间 + 每条序列在该时刻（时间戳完全相同）的值
        lines = self.tooltip_lines(stamp)

        font = painter.font()
        font.setPixelSize(12)
        font.setBold(False)
        painter.setFont(font)
        fm = painter.fontMetrics()
        w = max(fm.width(s) for s in lines) + 20
        h = fm.height() * len(lines) + 12
        # 气泡默认在点的右上；右边/上边放不下就翻到另一侧（不出绘图区）
        bx = x + 12
        if bx + w > rect_r:
            bx = x - 12 - w
        by = y - h - 10
        if by < rect_t:
            by = y + 12
        bx = max(rect_l, min(bx, rect_r - w))
        by = max(rect_t, min(by, rect_b - h))

        painter.setBrush(QColor(255, 255, 255, 240))
        painter.setPen(QPen(QColor(self.TEXT_COLOR), 1))
        painter.drawRoundedRect(_px(bx), _px(by), _px(w), _px(h), 5, 5)
        painter.setPen(QColor("#1A1A1A"))
        for i, line in enumerate(lines):
            painter.drawText(_px(bx + 10), _px(by + 6 + i * fm.height()),
                             _px(w - 20), fm.height(),
                             Qt.AlignLeft | Qt.AlignVCenter, line)

    # ==================================================================
    #                        鼠标交互
    # ==================================================================

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            l, _t, r, _b = self.plot_rect()
            self._press_pos = event.pos()
            self._dragged = False
            self._drag_origin = (event.pos().x(), self._view_x0,
                                 self._view_x1)
            if self.is_zoomed():
                self.setCursor(Qt.ClosedHandCursor)
            event.accept()
            return
        QDialog.mousePressEvent(self, event)

    def mouseMoveEvent(self, event):
        # [判据是"有没有按下拖动"而不是 buttons()] QTest/合成事件常常不带
        # 按键位（buttons()==NoButton）；按 buttons() 判会让"按下后移动"
        # 整段失效（实测：拖动毫无反应）。`_drag_origin` 只在按下时设置、
        # 松开时清空，本身就是最可靠的拖动标志。
        if self._drag_origin is not None:
            dx = event.pos().x() - self._drag_origin[0]
            if abs(dx) > self.TAP_SLOP:
                self._dragged = True
            # 从拖动起点重算（不是逐帧累加）：逐帧累加会因为夹取丢距离，
            # 拖过边界再拖回来位移对不上。
            span = self._drag_origin[2] - self._drag_origin[1]
            l, _t, r, _b = self.plot_rect()
            plot_w = max(1, r - l)
            delta = float(dx) / plot_w * span
            x0 = self._drag_origin[1] - delta
            x1 = self._drag_origin[2] - delta
            if x0 < 0.0:
                x0, x1 = 0.0, span
            if x1 > 1.0:
                x1, x0 = 1.0, 1.0 - span
            self._view_x0, self._view_x1 = x0, x1
            self._picked = None
            self.update()
            event.accept()
            return
        # 没拖动时：悬停到点上换手型（提示可点）
        if self.nearest_point(event.pos()) is not None:
            self.setCursor(Qt.PointingHandCursor)
        elif self.is_zoomed():
            self.setCursor(Qt.OpenHandCursor)
        else:
            self.setCursor(Qt.CrossCursor)
        QDialog.mouseMoveEvent(self, event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton and self._drag_origin is not None:
            was_drag = self._dragged
            self._drag_origin = None
            self.setCursor(Qt.CrossCursor)
            if not was_drag:
                # 单击：吸附最近点弹气泡；没点中点就收起气泡
                hit = self.nearest_point(event.pos())
                self._picked = (hit[0], hit[1], hit[2]) if hit else None
                self.update()
            event.accept()
            return
        QDialog.mouseReleaseEvent(self, event)

    def mouseDoubleClickEvent(self, event):
        """双击：复位到全程（触摸屏上双击也是"看全貌"的直觉操作）。"""
        if event.button() == Qt.LeftButton:
            self.reset_view()
            event.accept()
            return
        QDialog.mouseDoubleClickEvent(self, event)

    def wheelEvent(self, event):
        """滚轮缩放：以指针位置为锚点（鼠标设备的主路径）。

        上滚放大、下滚缩小。Qt5 的 angleDelta 一格 120；这里 1 格 ≈ 1.25 倍。
        """
        l, _t, r, _b = self.plot_rect()
        plot_w = max(1, r - l)
        frac = (event.pos().x() - l) / float(plot_w)
        frac = min(1.0, max(0.0, frac))
        steps = event.angleDelta().y() / 120.0
        if abs(steps) < 0.01:
            event.ignore()
            return
        self.zoom_at(frac, 0.8 ** steps)
        event.accept()

    # ==================================================================
    #                        触摸交互
    # ==================================================================

    def event(self, event):
        """接管手势事件（Qt5 下 QPinchGesture 通过 event() 投递）。

        [为什么还要处理原生 TouchEvent] 板端 X11 触摸屏不一定走
        QGestureRecognizer（取决于 Qt 的触摸设备识别），原生 TouchBegin/
        TouchUpdate 更可靠；两路都接、以先到者为准（pinch 手势开始后
        忽略原生双指，避免重复缩放）。
        """
        if event.type() == QEvent.Gesture:
            return self._on_gesture(event)
        if event.type() in (QEvent.TouchBegin, QEvent.TouchUpdate,
                            QEvent.TouchEnd):
            return self._on_touch(event)
        return QDialog.event(self, event)

    def _on_gesture(self, event):
        """QPinchGesture → 与触摸路径同一口径（从起点算总倍率）。

        [为什么不用 scaleFactor() 逐帧累乘] QPinchGesture 的 scaleFactor
        是**相对上一层**的增量；逐帧累乘会把每帧的抖动一起乘进去，一路
        漂到夹取极限（和原生触摸路径当初的毛病一样）。这里用
        totalScaleFactor（相对手势开始）配起点视口，夹取只作用一次。
        """
        gesture = event.gesture(Qt.PinchGesture)
        if gesture is None:
            return QDialog.event(self, event)
        if gesture.state() == Qt.GestureStarted:
            center = gesture.centerPoint().x()
            l, _t, r, _b = self.plot_rect()
            plot_w = max(1, r - l)
            frac = min(1.0, max(0.0, (center - l) / float(plot_w)))
            # 与触摸路径同一结构：(锚点占比, 起始间距占位, x0, x1)
            self._pinch_start = (frac, 0, self._view_x0, self._view_x1)
            self._picked = None
        elif gesture.state() == Qt.GestureUpdated and self._pinch_start:
            frac, _unused, start_x0, start_x1 = self._pinch_start
            total = gesture.totalScaleFactor() or 1.0
            span = start_x1 - start_x0
            self._view_x0, self._view_x1 = start_x0, start_x1
            # totalScaleFactor > 1 = 手指张开 = 放大 = 视口跨距变小
            self.zoom_at(frac, 1.0 / total)
        elif gesture.state() in (Qt.GestureFinished, Qt.GestureCanceled):
            self._pinch_start = None
        return True

    def touch_begin(self, xs):
        """触摸开始：`xs` 是落在绘图区内的手指横坐标列表（像素）。

        :returns: True=已按两指捏合处理，False=单指（进入平移跟踪）。

        抽成纯方法而不是写死事件里：触摸事件在离屏环境无法构造
        （PyQt 的 TouchPoint 不可实例化），但**这套状态机的正确性
        必须能测**——板端触摸屏是主战场，双指缩放捏错一点就是"手感怪"。
        """
        inside = [x for x in xs if self._in_plot_x(x)]
        if len(inside) >= 2:
            self._pinch_start = self._make_pinch(inside[0], inside[1])
            self._touch_last_x = None
            self._picked = None
            return True
        self._pinch_start = None
        self._touch_last_x = inside[0] if inside else None
        return False

    def touch_move(self, xs):
        """触摸移动：两指按间距比例缩放、单指按位移平移。"""
        inside = [x for x in xs if self._in_plot_x(x)]
        if len(inside) >= 2:
            if self._pinch_start is None:
                self._pinch_start = self._make_pinch(inside[0], inside[1])
                return
            frac, start_dist, start_x0, start_x1 = self._pinch_start
            dist = abs(inside[1] - inside[0])
            if dist > 1 and start_dist > 1:
                span = start_x1 - start_x0
                new_span = span * (start_dist / dist)
                # 先回到起点视口，再按比例缩放 —— 这样夹取只作用一次，
                # 不会因为逐帧累乘而一路漂到极限（实测过的漂移源）。
                self._view_x0, self._view_x1 = start_x0, start_x1
                self.zoom_at(frac, new_span / (span or 1.0))
            self._picked = None
            return
        if len(inside) == 1:
            x = inside[0]
            last = self._touch_last_x
            self._touch_last_x = x
            if last is not None and self.is_zoomed():
                l, _t, r, _b = self.plot_rect()
                # [方向：手指右移 = 内容跟着右移 = 看到更早的数据]
                # 与鼠标拖动同一口径（mouseMoveEvent 里 x0 = 起点 - delta）。
                # pan_pixels 的入参语义是"内容位移"，正值 = 内容右移，
                # 所以这里直接传 (x - last)，**不要再取反**（取反会让
                # 手指方向与内容方向相反，实测手感是"拖反了"）。
                self.pan_pixels(x - last, max(1, r - l))
            return
        self._touch_last_x = None

    def touch_end(self):
        """手指全部抬起：清掉触摸状态（下次按下重新锚定）。"""
        self._pinch_start = None
        self._touch_last_x = None

    def _in_plot_x(self, x):
        l, _t, r, _b = self.plot_rect()
        return l <= x <= r

    def _make_pinch(self, x0, x1):
        """两指锚定：(中心在时间轴上的占比, 起始间距, 起始视口)。"""
        l, _t, r, _b = self.plot_rect()
        plot_w = max(1, r - l)
        center = (x0 + x1) / 2.0
        frac = min(1.0, max(0.0, (center - l) / float(plot_w)))
        return (frac, abs(x1 - x0), self._view_x0, self._view_x1)

    def _on_touch(self, event):
        """原生触摸事件 → 上面三个纯状态机方法。

        [为什么还要处理原生 TouchEvent] 板端 X11 触摸屏不一定走
        QGestureRecognizer（取决于 Qt 的触摸设备识别），原生 TouchBegin/
        TouchUpdate 更可靠；两路都接，以先到者为准。
        """
        etype = event.type()
        if etype == QEvent.TouchBegin:
            self.touch_begin([p.pos().x() for p in event.touchPoints()])
        elif etype == QEvent.TouchUpdate:
            self.touch_move([p.pos().x() for p in event.touchPoints()])
        elif etype in (QEvent.TouchEnd, QEvent.TouchCancel):
            self.touch_end()
        event.accept()
        return True


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
            " border-radius: %dpx; font-family: monospace; font-size: 18px; }"
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


class MultiCheckCombo(QPushButton):
    """多选下拉：按钮 + 弹出勾选列表，可勾任意多个；**点项不收起，点外面才收**。

    [两种用法口径]
      · **列显示**（历史页）—— 勾中的列才显示，**默认全部勾选**；
        至少要留一列（全不勾等于"把表清空"，没有意义且让人以为界面坏了）。
      · **筛选列**（旧用法）—— 空选 = 不过滤（全部列）。
    两种口径差在"空选"的含义上，故由构造参数 `initial` / `min_checked`
    显式声明，不在控件里猜。

    [为什么不用 QComboBox] 原来是 editable QComboBox + view().pressed 自己
    翻转勾选，实测**两个硬伤**（用户反馈："可以取消列但是勾不回去了，
    而且单击一次多选框就消失了"）：
      1. 点列表项会走 QComboBox 的"选中"路径 → `currentIndex` 变化、
         lineEdit 被覆盖成该项文本、popup 自动收起。再点开时点的还是
         "当前项"，于是**取消掉的项怎么也勾不回来**（勾选状态在跑，但
         按钮文字显示的是一个单项名，看着像没生效）。
      2. QComboBox 的 popup 是"选中即关闭"语义，与"连点多选"根本冲突
         —— 每点一下都要重新点开。
    QComboBox 没有"不选中、纯当复选框列表"的开关，所以改成
    `QPushButton + QFrame(Qt.Popup)`：Popup 窗口的关闭条件由我们控制
    （点浮层外自动收，正是用户要求的口径），列表项是纯 QCheckBox 行，
    点击只改勾选、不触发任何"选中"逻辑。

    [下拉箭头的实现] 不用 QComboBox 就没有内建箭头，故在按钮文本尾部
    补一个"▾"（Unicode，板端字体有），右侧留白靠 QSS padding。
    """

    selectionChanged = pyqtSignal()

    class _ReadOnlyText(object):
        """`lineEdit()` 的兼容返回：只暴露 `text()`（汇总文本，只读）。"""

        __slots__ = ("_owner",)

        def __init__(self, owner):
            self._owner = owner

        def text(self):
            return self._owner._summary_text()

    #: 浮层里每行的高度（触摸屏口径：够手指点，又不至于列表太长）。
    ITEM_H = 34
    #: 浮层最大高度（超出后列表内滚动）—— 15 列时约 510px，矮屏要能滚。
    POPUP_MAX_H = 420

    def __init__(self, items, all_text="全部列", parent=None,
                 initial="none", min_checked=0):
        """:param initial: "none"=全不勾 / "all"=全勾（列显示用后者）
        :param min_checked: 至少保留几个勾选（0 = 允许全不勾）
        """
        QPushButton.__init__(self, parent)
        self._all_text = all_text
        self._min_checked = max(0, int(min_checked))
        self._labels = [str(t) for t in items]
        self._checked = [initial == "all"] * len(self._labels)
        self._text_view = MultiCheckCombo._ReadOnlyText(self)
        self._syncing = False
        #: 保留 `_model` 兼容旧调用方/测试（QStandardItemModel，勾选态同步）。
        self._model = QStandardItemModel(self)
        for text in self._labels:
            item = QStandardItem(text)
            item.setCheckable(True)
            item.setCheckState(Qt.Checked if initial == "all" else Qt.Unchecked)
            self._model.appendRow(item)
        self._model.itemChanged.connect(self._on_model_item_changed)
        self._popup = None
        self.setCursor(Qt.PointingHandCursor)
        self.clicked.connect(self._toggle_popup)
        self._sync_text()

    # ---- 兼容 QComboBox 时代的取文本入口 ----
    def lineEdit(self):
        """兼容旧调用（读汇总文本）：返回只读文本视图（`.text()`）。

        [为什么不返回 self] 返回按钮自身的话 `lineEdit().setText(...)`
        会直接改按钮显示、绕过 `_checked` 状态 —— 汇总文本必须由勾选态
        派生，不能被外部写进来（否则界面显示与实际筛选条件不一致）。
        """
        return self._text_view

    def text(self):
        return QPushButton.text(self)

    # ---- 勾选状态 ----

    def _on_model_item_changed(self, _item):
        """外部直接改 `_model` 勾选态时同步内部状态（测试/将来代码用）。"""
        if getattr(self, "_syncing", False):
            return
        self._checked = [self._model.item(i).checkState() == Qt.Checked
                         for i in range(self._model.rowCount())]
        self._sync_text()
        self._refresh_popup_checks()

    def _set_checked(self, index, checked):
        """改单项勾选（受 min_checked 约束）。返回是否真的改了。"""
        if 0 <= index < len(self._checked):
            if not checked and self._min_checked:
                # [至少留一列] 不允许把最后一列也取消：表格会变成空表头，
                # 用户看着像界面坏了。直接忽略这次点击（勾选态不变）。
                if sum(self._checked) <= self._min_checked:
                    return False
            if self._checked[index] == checked:
                return False
            self._checked[index] = checked
        else:
            return False
        self._syncing = True
        try:
            self._model.item(index).setCheckState(
                Qt.Checked if checked else Qt.Unchecked)
        finally:
            self._syncing = False
        self._sync_text()
        return True

    def _toggle_item(self, index):
        """兼容旧调用（测试直接点某项）：翻转单项勾选。"""
        idx = index.row() if hasattr(index, "row") else int(index)
        if 0 <= idx < len(self._checked):
            self._set_checked(idx, not self._checked[idx])

    def checked_indexes(self):
        """当前勾中的选项下标列表（0 基；空列表 = 未勾任何项）。"""
        return [i for i, on in enumerate(self._checked) if on]

    def clear_all(self):
        """全部取消。**受 min_checked 约束**：留不下就保留开头几项。"""
        keep = self._min_checked
        self._checked = [i < keep for i in range(len(self._labels))]
        self._push_model()
        self._sync_text()

    def check_all(self):
        """全部勾选（恢复默认视图）。"""
        self._checked = [True] * len(self._labels)
        self._push_model()
        self._sync_text()

    def _push_model(self):
        self._syncing = True
        try:
            for i, on in enumerate(self._checked):
                self._model.item(i).setCheckState(
                    Qt.Checked if on else Qt.Unchecked)
        finally:
            self._syncing = False

    def _summary_text(self):
        names = [t for t, on in zip(self._labels, self._checked) if on]
        if names and len(names) == len(self._labels):
            return ("%s（全部）" % self._all_text if self._min_checked
                    else self._all_text)
        if names:
            return "、".join(names)
        return self._all_text

    def _sync_text(self):
        self.setText(self._summary_text() + "  ▾")
        self.selectionChanged.emit()

    # ---- 弹出层 ----

    def _toggle_popup(self):
        if self._popup is not None and self._popup.isVisible():
            self._close_popup()
        else:
            self._open_popup()

    def _close_popup(self):
        if self._popup is not None:
            self._popup.hide()
            self._popup.deleteLater()
            self._popup = None
        self.setDown(False)

    def _open_popup(self):
        # [Qt.Popup 的关闭语义正是用户要的] 点浮层外的任何地方 → Qt 自己
        # 关闭这个 popup（等价"失去焦点/点外面才收"）；点浮层内部**不会**
        # 关，所以可以连续勾多个。不需要自己装事件过滤器。
        popup = QFrame(self, Qt.Popup)
        popup.setObjectName("HsfMultiPopup")
        popup.setStyleSheet(
            "QFrame#HsfMultiPopup { background: #FFFFFF;"
            " border: 1px solid %s; }" % theme.CLR_BORDER)
        lay = QVBoxLayout(popup)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        inner = QWidget()
        inner.setStyleSheet("QWidget { background: transparent; }")
        box = QVBoxLayout(inner)
        box.setContentsMargins(0, 0, 0, 0)
        box.setSpacing(0)
        self._checks = []
        for i, label in enumerate(self._labels):
            cb = QCheckBox(label)
            cb.setChecked(self._checked[i])
            cb.setMinimumHeight(self.ITEM_H)
            cb.setStyleSheet("QCheckBox { padding: 4px 8px; font-size: 22px; }")
            # 点击只改勾选，**不收 popup、不动任何"当前项"**
            cb.toggled.connect(
                lambda on, idx=i: self._on_check_toggled(idx, on))
            box.addWidget(cb)
            self._checks.append(cb)
        box.addStretch(1)
        scroll.setWidget(inner)
        lay.addWidget(scroll)

        # 高度：按项数算，超上限就滚动（15 列 × 34 ≈ 510 → 压到 420 滚动）
        height = min(self.POPUP_MAX_H,
                     self.ITEM_H * len(self._labels) + 14)
        width = max(self.width(), 200)
        popup.resize(width, height)
        popup.move(self.mapToGlobal(QPoint(0, self.height() + 2)))
        popup.show()
        self._popup = popup
        self.setDown(True)
        # [析构期要解绑] popup 的 destroyed 可能在按钮自身被 C++ 销毁之后
        # 才发出（父对象链：popup 是按钮的子对象），那时回调里碰 self 会
        # RuntimeError: wrapped C/C++ object has been deleted（实测）。
        # 连接挂到按钮自己的 destroyed 上先断开，或让回调判一次存活。
        popup.destroyed.connect(self._on_popup_gone)

    def _on_popup_gone(self, *_args):
        """popup 被 Qt 关掉（点外面/Esc）：清引用并复位按钮的按下态。"""
        self._popup = None
        try:
            self.setDown(False)
        except RuntimeError:
            # 按钮本身已析构（关窗/切页销毁），无需复位界面状态。
            pass

    def _on_check_toggled(self, index, checked):
        # 至少留一列的约束在 _set_checked 里；被拒时把勾选框回弹。
        if not self._set_checked(index, checked):
            self._syncing = True
            try:
                if hasattr(self, "_checks") and index < len(self._checks):
                    self._checks[index].setChecked(self._checked[index])
            finally:
                self._syncing = False

    def _refresh_popup_checks(self):
        """外部改勾选（clear_all/check_all/_model）时同步浮层里的勾选框。"""
        if self._popup is None or not hasattr(self, "_checks"):
            return
        self._syncing = True
        try:
            for i, cb in enumerate(self._checks):
                if i < len(self._checked):
                    cb.setChecked(self._checked[i])
        finally:
            self._syncing = False

    def hideEvent(self, event):
        self._close_popup()
        QPushButton.hideEvent(self, event)


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
            "QLabel { font-size: 24px; font-weight: bold; "
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
            "QLabel { background: transparent; font-size: 20px; }")
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
        # [键盘类型声明] 这是文件路径：纯 ASCII（盘符/斜杠/点），且中文
        # 路径在板端反而更容易出错——锁拉丁键盘，别让人误敲出全角字符。
        from .softkeyboard import KIND_ALPHA, set_keyboard_kind
        set_keyboard_kind(self._storage_path, KIND_ALPHA)
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

        # ---- 容量限额（现场硬盘小，必须能封顶）----
        limit_row = QHBoxLayout()
        limit_row.addWidget(QLabel("容量上限"))
        self._storage_max = QSpinBox()
        self._storage_max.setRange(proto.STORAGE_MAX_MB_MIN,
                                   proto.STORAGE_MAX_MB_MAX)
        self._storage_max.setSingleStep(50)
        self._storage_max.setSuffix(" MB")
        self._storage_max.setSpecialValueText("不限")
        self._storage_max.setToolTip("0 = 不限；库达到上限后按下方策略处理")
        self._storage_max.setMinimumWidth(130)
        limit_row.addWidget(self._storage_max)
        limit_row.addWidget(QLabel("写满后"))
        self._storage_policy = QComboBox()
        for key, text in proto.STORAGE_POLICIES:
            self._storage_policy.addItem(text, key)
        self._storage_policy.setMinimumWidth(260)
        self._storage_policy.currentIndexChanged.connect(
            self._on_storage_policy)
        limit_row.addWidget(self._storage_policy)
        limit_row.addWidget(QLabel("保留分段"))
        self._storage_keep = QSpinBox()
        self._storage_keep.setRange(1, 99)
        self._storage_keep.setToolTip("仅「分段滚动」策略用：总上限会均分到各段")
        self._storage_keep.setMinimumWidth(80)
        limit_row.addWidget(self._storage_keep)
        limit_row.addStretch(1)
        card4.body.addLayout(limit_row)

        #: 策略说明（选中项变化时更新，避免下拉文字被截断看不懂）。
        self._storage_policy_hint = QLabel("")
        self._storage_policy_hint.setWordWrap(True)
        self._storage_policy_hint.setStyleSheet(theme.caption_label_qss())
        card4.add(self._storage_policy_hint)
        # 初值按缺省策略刷一次（「保留分段」只在 rotate 下可用）。
        self._on_storage_policy()

        # ---- 用量实况（进度条 + 文字）----
        self._storage_usage = QProgressBar()
        self._storage_usage.setRange(0, 100)
        self._storage_usage.setValue(0)
        self._storage_usage.setTextVisible(False)
        self._storage_usage.setFixedHeight(12)
        card4.add(self._storage_usage)
        self._storage_usage_label = QLabel("用量：--")
        self._storage_usage_label.setWordWrap(True)
        self._storage_usage_label.setStyleSheet(
            "QLabel { background: transparent; }")
        card4.add(self._storage_usage_label)

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
            self._storage_max.setValue(int(
                storage.get(proto.CFGK_STORAGE_MAX_MB, 0) or 0))
            policy = str(storage.get(proto.CFGK_STORAGE_FULL_POLICY,
                                     "ring") or "ring")
            idx = self._storage_policy.findData(policy)
            self._storage_policy.setCurrentIndex(idx if idx >= 0 else 0)
            self._storage_keep.setValue(int(
                storage.get(proto.CFGK_STORAGE_KEEP_FILES, 3) or 3))
            self._on_storage_policy()
        db_path = str(doc.get("storageDbPath", "") or "")
        self._db_path_label.setText(
            "当前库文件：%s" % (db_path or "未启用（落盘关闭）"))
        self._populate_usage(doc.get("storageStats") or {})

    def _populate_usage(self, stats):
        """把用量实况铺到进度条与文字上。

        [百分比口径] 上限为 0（不限）时进度条按 0% 且文字写「不限」——
        这时候没有「用了百分之多少」可言，硬算会误导。
        """
        if not stats:
            self._storage_usage.setValue(0)
            self._storage_usage_label.setText("用量：--")
            return
        used_mb = float(stats.get("usedMb") or 0.0)
        max_mb = int(stats.get("maxMb") or 0)
        rows = int(stats.get("rows") or 0)
        segs = int(stats.get("segments") or 0)
        dropped = int(stats.get("droppedRows") or 0)
        parts = ["已用 %.2f MB" % used_mb]
        if max_mb > 0:
            parts.append("上限 %d MB" % max_mb)
            pct = max(0.0, min(100.0, used_mb / float(max_mb) * 100.0))
            self._storage_usage.setValue(int(round(pct)))
            parts.append("%.0f%%" % pct)
        else:
            self._storage_usage.setValue(0)
            parts.append("上限不限")
        parts.append("行数 %d" % rows)
        policy = stats.get("policy") or ""
        for key, text in proto.STORAGE_POLICIES:
            if key == policy:
                parts.append("策略 %s" % text.split("（")[0])
                break
        if segs:
            parts.append("分段 %d" % segs)
        if dropped:
            parts.append("已循环丢弃 %d 行" % dropped)
        self._storage_usage_label.setText("用量：" + " · ".join(parts))
        if stats.get("paused"):
            self._storage_usage_label.setText(
                self._storage_usage_label.text()
                + "\n[已停写] %s" % (stats.get("pauseReason") or ""))
            self._storage_usage_label.setStyleSheet(
                "QLabel { background: transparent; color: %s; }"
                % theme.CLR_WARN)
        else:
            self._storage_usage_label.setStyleSheet(
                "QLabel { background: transparent; }")

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

    def _on_storage_policy(self, _index=None):
        """策略切换：更新说明文字 + 「保留分段」只在分段策略下可用。"""
        key = self._storage_policy.currentData() or "ring"
        self._storage_policy_hint.setText(
            proto.STORAGE_POLICY_HINTS.get(key, ""))
        self._storage_keep.setEnabled(key == "rotate")

    def _apply_storage(self):
        if not self._config_gate():
            return
        values = {
            proto.CFGK_STORAGE_ENABLED: self._storage_enable.isChecked(),
            proto.CFGK_STORAGE_PATH: self._storage_path.text().strip(),
            proto.CFGK_STORAGE_SAMPLE_MS: self._storage_sample.value(),
            proto.CFGK_STORAGE_MAX_MB: self._storage_max.value(),
            proto.CFGK_STORAGE_FULL_POLICY:
                self._storage_policy.currentData() or "ring",
            proto.CFGK_STORAGE_KEEP_FILES: self._storage_keep.value(),
        }
        self.ctx.set_status("正在应用数据存储配置…")

        def _done(doc):
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("数据存储配置已生效（含容量上限与满额策略）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set("storage", values, on_done=_done)

    def refresh(self):
        model = self.ctx.model
        if model.run_mode_known:
            if model.is_auto():
                self._mode_label.setText("自动（远程控制已禁用）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 24px; font-weight: bold; color: %s;"
                    " background: transparent; }" % theme.CLR_WARN)
            else:
                self._mode_label.setText("手动（允许 root 远程控制）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 24px; font-weight: bold; color: %s;"
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
                "QLabel { background: transparent; font-size: 20px; }")
        elif not model.is_auto():
            self._auto_label.setText("手动模式（自动运行不可用）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px; color: #5A6572; }")
        elif running is None:
            self._auto_label.setText("自动模式 · 运行状态未知（服务端未上报）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px; color: #5A6572; }")
        elif running:
            self._auto_label.setText("自动模式 · 运行中")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px;"
                " font-weight: bold; color: %s; }" % theme.CLR_WARN)
        else:
            self._auto_label.setText("自动模式 · 已停止（未按过启动）")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px; color: #5A6572; }")
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
