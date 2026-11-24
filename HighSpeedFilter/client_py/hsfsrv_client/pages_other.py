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

[2026-11-13 用户定案「三页合并成一页」] 原 HsfOverlayPage.c（悬浮窗设置）
与 HsfWindowPage.c（窗口设置）两页的内容**已并入 LocalSettingsPage**
（页号 13；16/17 两页号从注册表与导航树删除，全树 16 页）：
  · 「界面设置」= 原本地设置页全部项 + 趋势刷新间隔 + 历史/日志每页条数；
  · 「悬浮窗」= 原悬浮窗设置页全部项（标题栏/状态栏/导航/时钟/温湿度显隐）；
  · 「窗口与屏幕」= 原窗口设置页全部项（尺寸记忆 + 屏幕休眠区）。
"""

import csv
import datetime
import re
import threading
import time

from PyQt5.QtCore import (QEvent, QEventLoop, QObject, QPoint, QRect, Qt,
                          QTimer, pyqtSignal)
from PyQt5.QtGui import QBrush, QColor, QFont, QFontMetrics, QPainter, QPen
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QDialog, QDoubleSpinBox,
                             QFileDialog,
                             QFrame, QGridLayout, QGroupBox, QHBoxLayout,
                             QHeaderView, QLabel, QLineEdit, QMessageBox,
                             QPlainTextEdit,
                             QProgressBar, QPushButton, QScrollArea, QSpinBox,
                             QApplication, QSizePolicy, QStackedWidget,
                             QTableWidget, QTableWidgetItem, QVBoxLayout,
                             QWidget)

from . import model as model_mod
from . import pages_registry as reg
from . import permissions, proto, settings, theme
# [数据三页共用的工具条 + 两个通用输入控件]（2026-11-11 抽出）
#   · DataPageToolbar：历史/日志/报警三页共用的三行工具条（按页裁剪控件）。
#   · MultiCheckCombo / TimePickerEdit：通用输入控件，从本模块搬去共用
#     控件模块（那边要它们；这里再 re-export ⇒ 旧导入路径不变，测试里
#     `from hsfsrv_client.pages_other import MultiCheckCombo` 照常可用）。
from .data_toolbar import (DataPageToolbar, MultiCheckCombo, TimePickerEdit)
from .hsf_export_plan import (BatchExport, CsvStreamWriter,
                              export_finish_text)
from .page_base import (DataTable, Page, WrapRow, aligned_row, fit_input_width,
                        hint_label, label_group, section_title, wrap_row)
from .pages_valve import VALVE_STATE_TEXT
from .widgets import (CARD_TITLE_QSS, Card, InlineStat, MarqueeLabel,
                       RemainDial)
from .xlsx_stream import XlsxStreamError, XlsxStreamWriter


# ---- collect 节：压力传感器 1 / 2（2026-11-23 三端契约，逐字 camelCase）----
#
# [为什么在这里定义] 本轮改动范围限定本模块（协议常量集中到 proto.py 由
# 协议面统一合并）；键名与 server_py/hsfsrv/proto.py、webui_py/hsfweb/
# proto.py 的同名常量**逐字一致**，迁移时整块搬走即可。
# 缺省：通道 2/3、量程 0.0~1.0 MPa、4mA 以下**不**有效（false）。
CFGK_PRESS1_CHANNEL = "press1Channel"
CFGK_PRESS1_LOW_MPA = "press1LowMpa"
CFGK_PRESS1_HIGH_MPA = "press1HighMpa"
CFGK_PRESS1_BELOW4_VALID = "press1Below4Valid"
CFGK_PRESS2_CHANNEL = "press2Channel"
CFGK_PRESS2_LOW_MPA = "press2LowMpa"
CFGK_PRESS2_HIGH_MPA = "press2HighMpa"
CFGK_PRESS2_BELOW4_VALID = "press2Below4Valid"
#: 压力1/压力2 缺省通道（压差计 CH0 / 温度传感器 CH1 之后的两个）。
PRESS1_CHANNEL_DEF = 2
PRESS2_CHANNEL_DEF = 3
#: 压力1/压力2 缺省量程（MPa；与现有 pressLowMpa/pressHighMpa 同区间）。
PRESS_LOW_MPA_DEF = 0.0
PRESS_HIGH_MPA_DEF = 1.0
#: 「4mA 以下有效」缺省（两台压力传感器缺省**关**：低于 4mA 按欠量程处理）。
PRESS_BELOW4_VALID_DEF = False


# ============================================================================
#                              主界面
# ============================================================================

class _ClickableCard(Card):
    """可点击卡片：整卡**按下**即发 clicked（C 端 HsfHomeCard 可点击口径）。

    主界面的「设备健康」「搅拌泵」卡用它承载"点卡弹出悬浮详情"的交互
    （对齐 C HsfHomePage.c hsfHomeHealthClicked / hsfHomePumpClicked：
    鼠标按下即触发，触控口径）。
    """

    clicked = pyqtSignal()

    def __init__(self, title="", parent=None):
        Card.__init__(self, title, parent)
        self.setCursor(Qt.PointingHandCursor)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit()
        Card.mousePressEvent(self, event)


class _HomeShade(QWidget):
    """悬浮详情的触摸遮罩：垫在浮层底下，捕获「点浮层外」收起。

    对齐 C HsfHomePage.c hsfHomeShadeClicked：遮罩上按下 ⇒ 收起浮层。
    """

    def __init__(self, on_click, parent=None):
        QWidget.__init__(self, parent)
        self._on_click = on_click
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setStyleSheet("background: rgba(0, 0, 0, 64);")

    def mousePressEvent(self, _event):
        if self._on_click is not None:
            self._on_click()


class _DeviceDetailCard(QFrame):
    """设备状态表**单行详情卡**（点行弹出，2026-11-22 用户口径）。

    [为什么需要] 设备状态浮层只有 5 列、宽度受限于屏幕：地址（完整
    `ip:port/…` 串）和错误信息（「6 路故障: 电动球阀…」）必然出现省略号。
    点行弹出本卡后，字段按**原文换行**完整显示（可选中复制），不再靠
    tooltip/剪贴板猜全文。

    [为什么是 Qt.Popup 无边框卡，不是 QDialog] 用户口径：不要原生标题栏
    （灰色「设备详情 — ? ×」）、不要多余按钮、**点窗口外就关闭**。
    Qt.Popup + FramelessWindowHint 天然满足"点外面收起"，内层白卡复用
    设备状态浮层的视觉（白底/细描边/圆角），顶部直接标题，不再被原生
    chrome 和布局余量顶出一片空行。
    """

    def __init__(self, dev, parent=None):
        # [为什么不是 Qt.Popup] 实测当前 Windows 环境下 Qt.Popup 的
        # 「点窗外关闭」不生效（合成点击后仍可见）⇒ 改为应用内遮罩
        # （HomePage._device_shade）+ 普通 QFrame：遮罩铺满主窗、点它必关，
        # 卡片永远在遮罩之上。视觉仍是白底圆角卡、无原生标题栏/按钮。
        QFrame.__init__(self, parent)
        self.setObjectName("HsfDeviceDetailCard")
        self.setStyleSheet(
            "QFrame#HsfDeviceDetailCard { background: #FFFFFF;"
            " border: 1px solid %s; border-radius: %dpx; }"
            % (theme.CLR_BORDER_SOFT, theme.RADIUS_CARD))
        lay = QVBoxLayout(self)
        lay.setContentsMargins(18, 14, 18, 10)
        lay.setSpacing(8)

        title = QLabel(str(getattr(dev, "name", "") or "-"))
        title.setWordWrap(True)
        title.setStyleSheet(
            "QLabel { color: #1F4E79; font-size: 22px; font-weight: bold;"
            " background: transparent; }")
        lay.addWidget(title)

        grid = QGridLayout()
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(8)
        health_colors = {0: theme.CLR_OK, 1: theme.CLR_WARN,
                         2: theme.CLR_WARN, 3: theme.CLR_OFFLINE,
                         4: theme.CLR_FAULT}
        rows = (
            ("类型", str(dev.kind_text()), None),
            ("状态", str(dev.health_text()),
             health_colors.get(int(getattr(dev, "health", -1)), None)),
            ("地址", str(getattr(dev, "addr", "") or "-"), None),
            ("错误信息", str(getattr(dev, "text", "") or "-"), None),
        )
        for r, (key, value, color) in enumerate(rows):
            k = QLabel(key)
            k.setAlignment(Qt.AlignTop | Qt.AlignLeft)
            k.setStyleSheet(theme.caption_label_qss())
            v = QLabel(value)
            v.setWordWrap(True)
            v.setTextInteractionFlags(Qt.TextSelectableByMouse |
                                      Qt.TextSelectableByKeyboard)
            v.setStyleSheet(
                "QLabel { color: %s; font-size: 18px; background: transparent; }"
                % (color or theme.text_color()))
            grid.addWidget(k, r, 0, Qt.AlignTop)
            grid.addWidget(v, r, 1)
        grid.setColumnStretch(0, 0)
        grid.setColumnStretch(1, 1)
        lay.addLayout(grid)

        # [2026-11-22 用户「没必要提示点击外部」] 不摆关闭按钮、也不写
        # 「点击窗口外关闭」提示：点遮罩即收，卡片本身保持干净。
        self.setFixedWidth(680)
        self.adjustSize()

    def hideEvent(self, event):
        QFrame.hideEvent(self, event)
        self.deleteLater()


class HomePage(Page):
    """主界面：自动模式操作 + 关键指标总览（首行=自动模式，2026-11-12）。

    [页头已删 2026-11-12] 原先页面顶部有一行页头（「主界面」+
    「高速过滤器监控系统」），用户定案删掉 ⇒ 首行直接是自动模式卡。

    C 端 HsfHomePage 的口径：顶部看板（温湿度 / 搅拌泵 / 设备健康 /
    压差 / 流量计 / 电磁阀六路）+ 下方快捷入口卡片。

    [悬浮详情卡（2026-11-09 对齐 C hsfHomeApplyDetail）] 设备状态表与
    搅拌泵详情不再是布局流里的常驻区，而是**可开合的居中悬浮卡**：
      · 点「设备健康」卡 ⇒ 弹出设备状态表；再点同卡 / 点 × / 点遮罩 ⇒ 收起
      · 点「搅拌泵」卡 ⇒ 弹出泵详情
    弹出期间遮罩垫底、浮层居中抬升；窗口缩放后每拍/每次 resize 校正居中
    （C 端「浮层跟随页面尺寸」同口径）。

    [刷新分帧（对齐 C 帧泵注释 HsfHomePage.c hsfStatSet）] 数值卡每拍刷
    （QLabel.setText 同值早退，稳态零重排）；设备表每 2 拍铺一次 ——
    setRowCount / fit_to_content 会触发布局重排，摊开到两帧，
    避免单拍重排整页。
    """

    TOPICS = (proto.TOPIC_VFD_STATUS, proto.TOPIC_TEMPHUM_STATUS,
              proto.TOPIC_AI_STATUS, proto.TOPIC_FLOWMETER_STATUS,
              proto.TOPIC_VALVE_STATUS, proto.TOPIC_DEVICE_HEALTH,
              # [2026-11-09 首页新增两卡] 浊度仪卡读 TURBIDITY 主题、
              # 按钮组卡读 DIGITAL 主题（DI12~15 触点 + DO7~9 三灯）。
              # 不订则两卡恒 "--"。
              proto.TOPIC_TURBIDITY_STATUS, proto.TOPIC_DIGITAL_STATUS,
              # [进水泵 2026-11-12] 浊度仪卡里的进水泵状态读 PUMP 主题；
              # 不订则 model.pump 恒空、状态恒 "--"。
              proto.TOPIC_PUMP_STATUS)

    #: 悬浮详情卡宽度（表格态 / 泵详情态）。
    #:
    #: [表格态不能太窄] 设备表 5 列在 DataTable 里有 150px/列的宽度下限
    #: （"COM17 · 115200 · 站 2" 这类地址串的硬保证），780px 宽的浮层会让
    #: 5×150 刚好吃满视口、权重分配失效（各列全被压到下限）。960px 留出
    #: 按权重分配的余量，地址列才能明显宽于类型列。
    DETAIL_TABLE_W = 960
    DETAIL_PUMP_W = 640
    #: 电磁阀卡的路数（服务端 6 路；超出部分不显示）。
    VALVE_COUNT = 6
    #: 阀状态「未配置」的灰（对齐 pages_valve.VALVE_STATE_TEXT 的浅灰）。
    VALVE_NA_COLOR = "#9E9E9E"
    #: 6 路电动球阀的**缺省显示名**（服务端没下发 name 时兜底）。
    #:
    #: [用户口径 2026-11-23 逐台工艺名] 现场 6 台阀各有用途，名称逐字为：
    #: 产水阀(1) / 反洗进水阀(2) / 进水阀(3) / 排空阀(4) / 反洗出水阀(5) /
    #: 顺洗出水阀(6)。**组名/页面名仍是「电动球阀」「电动球阀组」**，只有
    #: 单台阀的名字变。与 `proto.AUTO_DEVICES` 前 6 项同源（顺序一致）；
    #: 服务端随 TOPIC_VALVE 下发的 name 优先，此处只在缺名字时兜底。
    VALVE_NAMES = ("产水阀(1)", "反洗进水阀(2)", "进水阀(3)",
                   "排空阀(4)", "反洗出水阀(5)", "顺洗出水阀(6)")
    #: 浊度视觉告警阈值（NTU）。
    #:
    #: [与水质监测页同一口径] pages_monitor.WaterPage.TURBIDITY_WARN_NTU。
    #: [为什么阈值放客户端] 这只是**显示口径**（数字标不标红），不是报警判据：
    #: 真要进报警列表必须挪到服务端（客户端各自算阈值必然漂移）。
    TURBIDITY_WARN_NTU = 20.0
    #: 浊度卡台数（服务端 count 决定；此处只固定显示槽位）。
    TURBIDITY_COUNT = 2
    #: 浊度读数/单位字号（2026-11-24 用户「浊度仪卡片数字还可以再大点，
    #: 有很多空白」）：数值 36 → 42，单位 NTU 收小到 22 —— 两列并排各约
    #: 157~175px，"3.52" 变大后总宽仍落在列宽内。自动缩放放大数值时单位
    #: 不跟着涨（StatBlock.unit_size 固定），宽度增量只来自数值。
    TURB_VALUE_SIZE = 42
    TURB_UNIT_SIZE = 22
    #: 按钮组触点/灯的**服务端下发通道名**前缀（按名字匹配，不写死通道号）。
    #:
    #: [为什么不写死 12~15 / 7~9] 现场接线由图纸决定，Python 服务端的点位
    #: 走 `buttons.items` 配置可改；客户端写死通道号 = 接线一改界面就错位。
    #: 名字由两个服务端逐字一致地下发（C 端 HsfServerApp.c 与 Python 端
    #: app.py 的按钮组登记链），按名字匹配则两端同口径。
    BTN_ESTOP_NAME = "急停"
    BTN_START_NAME = "启动按钮"
    BTN_STOP_NAME = "停止按钮"
    BTN_RESET_NAME = "复位按钮"
    BTN_LAMP_NAMES = ("启动按钮灯", "停止按钮灯", "复位按钮灯")
    #: 按钮组触点的**触点类型**（常开/常闭）——与位图电平的换算口径。
    #:
    #: [口径来源] 服务端 `server_py/hsfsrv/buttons.py` 的现场定义：
    #:   急停 = 常闭（1=回路通=正常；0=按下或断线 ⇒ 禁止启动）
    #:   停止 = 常闭（用户口径「停止跟急停一样，不按就是通的」）
    #:   启动/复位 = 常开（1=按下）
    #: 服务端可配 `buttons.items.normally_closed` 覆盖；覆盖后两端这张表
    #: 也要跟着改（本次不扩到协议层）。
    BTN_NORMALLY_CLOSED = (True, False, True, False)   # 急停/启动/停止/复位
    #: 按钮组卡里 4 触点 + 3 灯 = 7 格（2 行 4 列，第 8 格留空）。
    BTN_SLOT_COUNT = 7
    #: 7 格的界面标签（槽位顺序：急停/启动/停止/复位 + 三灯）。
    BTN_SLOT_LABELS = ("急停", "启动", "停止", "复位",
                       "启动灯", "停止灯", "复位灯")
    #: 压差卡三栏对应的**模拟量通道名**（服务端随 TOPIC_AI 的 en{i} 下发）。
    #:
    #: [按名字找通道，不写死通道号 2026-11-23] 远端 collect 节可配
    #: pressChannel/press1Channel/press2Channel ⇒ 服务端会把名字挂到**新
    #: 通道**上；客户端若按固定下标取数，改一次接线就显示错栏。压差栏保留
    #: 通道 0 兜底（老服务端只发 en0="压差计"），压力1/2 找不到名字就 "--"。
    PRESS_DIFF_NAME = "压差计"
    # [引 proto 常量 2026-11-26] 服务端 scale 名就是 PRESS{1,2}_LABEL
    # （rebuild_scales 同源）⇒ 按名匹配直接引用，两端改名零漂移。
    PRESS1_NAME = proto.PRESS1_LABEL
    PRESS2_NAME = proto.PRESS2_LABEL
    #: 压力及压差卡的数值字号（2026-11-23 用户「数字是不是可以大点，周围
    #: 好多空白」：卡内只有三行读数，20px 太保守 ⇒ 取首页既有档位的 34px
    #: 一档，与电箱温度(32)/设备总数(34)同级；三栏宽度经实测放得下
    #: 「0.499 + MPa」，缩放到 1.5× 也不折行）。
    PRESS_VALUE_SIZE = 34
    #: 主界面看板 9 张卡的**稳定 id**（拖拽排序与本地持久化用，与显示名
    #: 解耦；顺序 = 默认排布，行优先）。2026-11-23 用户口径：按钮组与
    #: 电动球阀交换位置 ⇒ 默认 = 浊度仪/电动球阀/压力及压差 / 搅拌泵/
    #: 流量计/按钮组 / 电箱温度/设备及仪表/最近报警。
    BOARD_CARD_IDS = ("turb", "valve", "dp", "vfd", "fm", "btn",
                      "th", "health", "alarm")
    BOARD_DEFAULT_ORDER = BOARD_CARD_IDS
    #: 本地设置键（INI；客户本机布局，不上服务器）。
    BOARD_ORDER_KEY = "Home/CardOrder"

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        # [页头已删 2026-11-12] 原先这里有一行 add_header("主界面",
        # "高速过滤器监控系统")，用户定案删掉主页顶部这行字 ⇒ 首行直接
        # 放自动模式卡（本构造里 self.root 的第一个 addWidget 就是它）。

        # ---- 首行：自动模式卡（2026-11-11 实现；2026-11-12 用户定案挪到
        #      首行——操作员最常用的入口放最上面）----
        #
        # [与远端设置的「自动运行」卡分工] 那张卡是"启动/停止自动运行"
        # 本身（不带模式）；这张卡是操作员日常入口：选模式 → 启动。
        # 序列逻辑全在服务端（阀相→到位→泵相→运行），客户端只发意图、
        # 按 1s 轮询回显真实相位（KEY_AUTO_MODE/PHASE/FAULT）。
        # [标题不再由 Card 占一行 2026-11-12 五改] 用户口径：「表盘上面不是
        # 还有吗，无法跨占到『自动模式』那行吗」——标题行右侧本来就是空的，
        # 把标题也放进本卡的三列网格（第 0 行），让表盘**跨占标题行**，
        # 上面那段竖向空白就用上了（表盘从 108 → 132px，卡片反而略矮）。
        auto_card = Card()
        self._auto_title = QLabel("自动模式")
        self._auto_title.setStyleSheet(CARD_TITLE_QSS)
        # ---- 布局（2026-11-12 三/五改：表盘挪左 + 无旁文 + 跨到标题行 + 只在循环显示）----
        #
        # 三列网格：左列 = 标题 + 模式选择 + 状态行；中列 = **表盘（跨三行）**；
        # 右列 = 启动/停止/复位 + 累计运行时长。
        # [为什么放中间] 用户口径「挪左边」——原来贴在卡片最右侧；挪到
        # “模式下拉”与“启动键”之间那片空白。
        # [只在自动循环运行时显示] 剩余时间只有循环才有（远端设置的分钟数
        # 全是循环参数，单模式没有时间限制）⇒ 不跑循环就把表盘隐藏，
        # 中列宽度塔缩，卡片回到原样子（不留空框）。
        mode_left = QHBoxLayout()
        mode_left.setSpacing(8)
        mode_left.addWidget(QLabel("模式"))
        self.auto_mode_combo = QComboBox()
        for mid, name in proto.AUTO_MODES:
            self.auto_mode_combo.addItem(name, mid)
        # 「自动」组合模式：按流量循环调用三个模式（服务端 2026-11-12 实现）。
        self.auto_mode_combo.addItem("自动（流量循环）", proto.AUTO_MODE_AUTO)
        self.auto_mode_combo.setMinimumWidth(160)
        # [下拉同步选择 2026-11-13 板端实锅] activated（**用户改选**，程序
        # 回显不触发）即发 op=5 到服务端记粘性——物理启动按钮用「上次选中
        # 的」，下拉不同步则服务端粘性为空/过期 ⇒ 现场按键被拒。
        self.auto_mode_combo.activated.connect(self._auto_mode_selected)
        mode_left.addWidget(self.auto_mode_combo)
        mode_left.addStretch(1)

        self.auto_start_btn = QPushButton("启动")
        self.auto_start_btn.setToolTip(
            "按所选模式启动：开选中阀、关未选阀，等全部到位后按配置启动"
            "搅拌泵/进水泵（需自动模式 + 管理员）")
        self.auto_start_btn.clicked.connect(self._auto_start_clicked)
        self.auto_stop_btn = QPushButton("停止")
        self.auto_stop_btn.setToolTip("停止自动运行并停全部设备")
        self.auto_stop_btn.clicked.connect(self._auto_stop_clicked)
        # ---- 复位按钮（2026-11-12 用户定案「有故障报警的时候可以按复位」）----
        # [语义] 解除**两把锁**：急停锁存 + 模式序列报警锁存（阀超时等），
        # 回停止态但**不启动**。没有它，操作员在报警锁死时只能去按现场
        # 按钮组的物理复位按钮（远程无解）。
        self.auto_reset_btn = QPushButton("复位")
        self.auto_reset_btn.setToolTip(
            "清除报警锁存与急停锁存（复位后才能再按启动；不会自动启动）")
        self.auto_reset_btn.clicked.connect(self._auto_reset_clicked)
        btn_row = QHBoxLayout()
        btn_row.setSpacing(8)
        btn_row.addWidget(self.auto_start_btn)
        btn_row.addWidget(self.auto_stop_btn)
        btn_row.addWidget(self.auto_reset_btn)

        self._auto_hint = QLabel("--")
        # [不折行 2026-11-12] 状态行文案（最长 ~40 字）在 1366 宽度下单行
        # 放得下；折行会让卡片高一个状态跳一次（恒几何口径）。
        self._auto_hint.setWordWrap(False)
        self._auto_hint.setStyleSheet(theme.caption_label_qss())
        # ---- 累计自动运行时长（2026-11-12 用户定案「显示成天 时 分 秒」）----
        #
        # [与服务端状态行**同一行**] 首页要一屏放下（余量只剩几 px），再加
        # 一行就溢出 ⇒ 状态文字靠左、时长靠右，两段共用一行。
        # [定宽防跳动] 时长每秒变一次，不定宽则文字宽度变化会把整行重排
        # （每跳一次抖一下）。按"最大可能值"给固定宽度。
        self._auto_run_label = QLabel("--")
        self._auto_run_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._auto_run_label.setStyleSheet(theme.caption_label_qss())
        # [定宽按**纯时长串**测；「自动运行」另起一个标签]
        # 之前是「自动运行 X天…」一整串右对齐 + 按纯时长串定宽 ⇒ 少了
        # 前缀的宽度，标签右对齐 ⇒ 宽度不足时文字**从左边**溢出被裁，
        # 「自动」两字消失（用户截图实见）。改成两个标签后：标题恒在、
        # 只有时长段是定宽的（右对齐也不怕裁前缀）。
        # [必须用 setFixedWidth 而不是 min/maxWidth] QLabel 的
        # minimumSizeHint **不认** setMinimumWidth ⇒ 布局按 sizeHint 分
        # 配，而初始文本是「--」sizeHint 很小 ⇒ 标签被压到 297px（比需要
        # 少 17px，第二个字又被裁，回归 D12e 抓到）。固定宽度布局压不动
        # 它，被压缩的只会是左边 stretch 的状态文字。
        _run_w = self._auto_run_text_width(self.AUTO_RUN_MAX_TEXT,
                                           self._auto_run_label.font())
        self._auto_run_label.setFixedWidth(_run_w)
        # [文案 2026-11-12 用户口径] 「自动运行」→「累计」：右边那串是天/时/
        # 分/秒的**累计**时长（含多次运行），不是本次运行；「自动运行」四个
        # 字容易读成“本次跑了多久”。
        self._auto_run_title = QLabel("累计")
        self._auto_run_title.setStyleSheet(theme.caption_label_qss())
        run_row = QHBoxLayout()
        run_row.setSpacing(8)
        run_row.addWidget(self._auto_run_title)
        run_row.addWidget(self._auto_run_label)

        # ---- 剩余时间表盘（跨两行，默认隐藏；只有自动循环运行时才显）----
        self._auto_dial = RemainDial()
        self._auto_dial.setVisible(False)

        # ---- 阶段信息（2026-11-19 用户定案「主页补一行阶段信息」）----
        #
        # [为什么不是新卡] 首页已有「自动模式」卡（模式/按钮/表盘/累计）⇒
        # 再来一张「自动运行」卡是**重复表达**（用户明确指出）。⇒ 阶段信息
        # 作为**既有卡里的一块**补上。
        #
        # [放在卡的右上角（标题行同一行）而不是新起一行] 实测（板端
        # 1280x800@150%）：自动模式下这张卡原本就顶着 132px 的表盘，
        # 新起一行 ⇒ 卡片再高 31px ⇒ **主页在自动模式下会多出 18px 的
        # 滚动**（超出一屏）。而标题行右侧**本来就是空的**（表盘在中间
        # 列）⇒ 放那儿**不加卡高**、还把那块空利用起来。
        #
        # [内容] 阶段名（autoPhase 的中文）+ 阶段进度（已用/总）+ 进度条。
        #   · 阶段名在**单模式下也有价值**（"现在在开阀阶段"＝解释为什么泵
        #     还没转）⇒ 保留。
        #   · 进度条只在 **total > 0**（本阶段有定时）时有值；单模式没有
        #     定时 ⇒ 只显示阶段名，不显示进度/时间（不编造倒计时）。
        #   · 手动模式**隐藏整块**（手动模式"第几阶段"无意义）。
        #
        # [与表盘不重复] 表盘是**倒计时**（剩余，循环模式才有）；本块是
        # **本阶段已用/总**。两者口径不同、可并存，但文案不重复表达同一件
        # 事 ⇒ 表盘写"剩余"，本块写"已用 x / 共 y"。
        self._phase_row = QWidget()
        phase_row = QHBoxLayout(self._phase_row)
        phase_row.setContentsMargins(0, 0, 0, 0)
        phase_row.setSpacing(8)
        self._phase_label = QLabel("--")
        self._phase_label.setStyleSheet(theme.caption_label_qss())
        self._phase_text = QLabel("--")
        self._phase_text.setStyleSheet(theme.caption_label_qss())
        phase_row.addWidget(self._phase_label)
        phase_row.addWidget(self._phase_text)

        grid = QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(6)
        grid.addWidget(self._auto_title, 0, 0, 1, 1,
                       Qt.AlignLeft | Qt.AlignVCenter)
        grid.addLayout(mode_left, 1, 0, 1, 1, Qt.AlignLeft | Qt.AlignVCenter)
        grid.addWidget(self._auto_hint, 2, 0)
        # 表盘跨三行（标题行 + 模式行 + 状态行）⇒ 竖向空间吃满
        grid.addWidget(self._auto_dial, 0, 1, 3, 1, Qt.AlignCenter)
        grid.addLayout(btn_row, 1, 2, 1, 1, Qt.AlignRight | Qt.AlignVCenter)
        grid.addLayout(run_row, 2, 2, 1, 1, Qt.AlignRight | Qt.AlignVCenter)
        # 阶段信息：放在**标题行右侧**（第三列第一行）——那一格原本是空的
        # ⇒ 不加卡高（见上面注释的实测依据）。
        grid.addWidget(self._phase_row, 0, 2, 1, 1,
                       Qt.AlignRight | Qt.AlignVCenter)
        grid.setColumnStretch(0, 1)
        auto_card.body.addLayout(grid)
        self._auto_poll_at = 0.0        # 服务端状态轮询节流（1s）
        self.root.addWidget(auto_card)

        # ---- 总览卡片 ----
        # [密度 2026-11-12] 1366x768@1.5x 要把首页全部塞进一屏：字号按
        # 信息层级整体下调一档（26→20/22→18），行距/格距收紧，快捷入口
        # 行删除（导航树有同样三项，重复占一整行）。判据=内容高 ≤ 视口
        # （临时测量脚本实测：改前 823px vs 视口 480px）。
        # [2026-11-12 二次调整] 用户截图：压缩后底部还有 ~150px 空白 ⇒
        # 字号回调一档（18→26/22）、间距放开（既有的"过密"是矫枉过正），
        # 让内容自然填满一屏。
        self.root.setSpacing(12)
        # ---- 三排看板卡（2026-11-19 按实测卡高重排）----
        #
        # [为什么重排] 板端 1280x800@150% 实测各卡**自然高**：
        #   出水浊度仪06 / 按钮组202 / 搅拌泵194 / 压差153 / 流量计153 /
        #   电动球阀147 / 设备健康113 / 电箱温度112 / 最近报警97。
        #   旧排法（电箱温度|搅拌泵|设备健康 ／ 压差|流量计|电动球阀 ／
        #   浊度仪|按钮组|最近报警）有两个毛病：
        #     ① **页面底下空一大块**：内容自然高 619 vs 视口 720 ⇒ 100+px
        #        空白（末行的 addStretch 吃掉的那截）；
        #     ② **卡内空**：同排卡高差 206 vs 97 ⇒ 短卡底下 100+ 空白。
        #   新排法：**每排三张、按高度从高到低**，同排卡高差最小 ⇒
        #     第一排 出水浊度仪06 ｜ 按钮组202 ｜ 压差153   （排内差 53）
        #     第二排 搅拌泵194 ｜ 流量计153 ｜ 电动球阀147（排内差 47）
        #     第三排 设备健康113 ｜ 电箱温度112 ｜ 最近报警97（排内差 16）
        #   ⇒ 三排合计 513 ⇒ 内容自然高 651（待机）/ **701（自动模式，
        #     表盘显示时自动模式卡 146）** ⇒ 自动模式也塞得进 720 视口、
        #     **不出滚动条**，末排三张高度接近（不留大空）。
        # [用户点名要换的] 「电箱温湿度不需要这么大的卡片」⇒ 电箱温度卡
        #   从第一排头位挪到第三排（原来占着两行高的大卡位），浊度仪卡
        #   （内容多：两台读数+水温+进水泵说明）挪到第一排头位（高卡位）。
        # [与网页端同排布] page00_home.js 的 home-cards 三排同款。
        cards = QHBoxLayout()          # 第一排：浊度仪 / 按钮组 / 压差
        cards.setSpacing(14)
        cards2 = QHBoxLayout()         # 第二排：搅拌泵 / 流量计 / 电动球阀
        cards2.setSpacing(10)
        cards3 = QHBoxLayout()         # 第三排：设备健康 / 电箱温度 / 最近报警
        cards3.setSpacing(10)

        from .widgets import StatBlock
        self._detail_mode = "none"     # "none"/"table"/"pump"（悬浮详情）
        self._refresh_phase = 0        # 刷新分帧相位（0 拍铺设备表）
        # 温湿度卡 —— [现场口径] 这是**电箱内**的温度/湿度，不是环境(室外/室内)
        # 温湿度：变送器装在电控柜里，用来监视柜内温升与凝露风险。
        # 标题与字段名都要带上"电箱"，否则会被误读成现场环境温度。
        th_card = Card("电箱温度")
        self._th_grid = QGridLayout()
        self._th_grid.setSpacing(10)
        # [字号降一档 2026-11-19 用户「电箱温湿度不需要这么大的卡片」]
        # 两个 40px 的数把卡撑成 122px（第二排里最高的那张）⇒ 换成 32px
        # （与浊度仪/设备健康的数值同档），卡高降到 ~112px ⇒ 换到第三排
        # 的矮卡位后同排高度接近（113/112/97）。
        # [数值与单位仍同行同基线同字号] StatBlock 的排版口径不变。
        self.th_temp = StatBlock("电箱温度", "℃", "--", value_size=32)
        self.th_hum = StatBlock("电箱湿度", "%RH", "--", value_size=32)
        self._th_grid.addWidget(self.th_temp, 0, 0)
        self._th_grid.addWidget(self.th_hum, 0, 1)
        th_card.body.addLayout(self._th_grid)
        # [2026-11-23 用户「首行怎么是空的」] 卡片可被长按拖到任意一排，
        # 谁当"同排最矮"都可能 ⇒ **所有**看板卡统一"多余高度沉底"
        # （否则 矮卡的 QVBox 把余高均摊到标题与正文之间 = 首行空白）。
        th_card.add_bottom_stretch()
        cards3.addWidget(th_card, 1)      # 第三排（矮卡位）

        # 变频器卡（可点击 ⇒ 弹出泵详情悬浮卡；对齐 C hsfHomePumpClicked）
        vfd_card = _ClickableCard("搅拌泵")   # 设备是变频器，按工艺用途显示
        vfd_card.setToolTip("点击查看搅拌泵详情")
        vfd_card.clicked.connect(lambda: self._card_clicked("pump"))
        self._vfd_grid = QGridLayout()
        self._vfd_grid.setSpacing(10)
        # [单行三格 2026-11-12] 原两行（状态一行 + 频率/设定一行）太高；
        # 三格并一行后与邻卡（电箱温度）等高。
        self.vfd_state = StatBlock("运行状态", "", "--", value_size=30)
        # [频率/转速对调 2026-11-23 用户口径「现在频率字大，但一般关心的
        #  是转速」] 主数值位给**转速**（操作员看的是"转多快"），输出频率
        # 降为次一级。字号只用 StatBlock 既有档位（40 主 / 26 次），颜色
        # 层级沿用 set_value 的默认文本色 ⇒ 不改数据绑定与格式化。
        self.vfd_freq = StatBlock("输出频率", "Hz", "--", value_size=26)
        # [第三项=转速 2026-11-17 用户实报] 原「设定频率」⇒ 主页一览卡显示
        # **实际转速**（现场铭牌 50Hz≈1440rpm ⇒ 28.8 rpm/Hz，按**输出频率**
        # 换算；变频器本身没有转速寄存器，这是换算值）。「设定频率」仍在
        # **泵详情悬浮卡**里（pd_setf）——那儿已经同时有「搅拌转速」。
        # [与 C 端同口径] C 端 HsfHomePage.c 的 HOME_TXT_VFD_SET 已是
        # 「转速」、HOME_RPM_PER_HZ = 1440/50（当时注释写"与 Python 客户端
        # 主页、网页端主页同口径"，实际那两端没改 ⇒ 本轮补齐）。
        # [2026-11-23 成为主数值] 字号 26 → 40，位置与「输出频率」互调。
        self.vfd_rpm = StatBlock("转速", "rpm", "--", value_size=40)
        # [第四/五项=功率/转矩 2026-11-19 用户实报] 首页搅拌泵卡补
        # **输出功率(kW)** 与 **转矩(%)** —— 现场看搅拌泵时最常问的三个数
        # 就是"转没转 / 多快 / 出多大力"，功率与转矩正好补上后两个。
        # [数据已有] 服务端 VFD 快照带 outputPowerKw / torquePercent
        # （proto 键），模型侧 model.vfd.output_power_kw /
        # .torque_percent ⇒ **不新增协议**，只是没显示出来。
        # [与 C 端详情卡同源] C 端 HsfHomePage.c 的详情卡已有
        # HOME_TXT_DP_POWER / HOME_TXT_DP_TORQUE ⇒ 本轮把主页一览卡也
        # 补上（网页端主页同款，见 page00_home.js）。
        self.vfd_power = StatBlock("输出功率", "kW", "--", value_size=26)
        self.vfd_torque = StatBlock("转矩", "%", "--", value_size=26)
        # [第六项=电机温度 2026-11-20 用户口径「搅拌泵卡片可以增加温度
        #  显示了，就是那个模拟量的温度计」] 服务端随 VFD 遥测/快照下发
        #  motorTempC（模拟量温度传感器，通道远端可配）。缺键 ⇒ "--"
        #  （不编 0℃）。单位 ℃ 走 StatBlock 的 unit 通道。
        self.vfd_mtemp = StatBlock("电机温度", "℃", "--", value_size=26)
        self._vfd_grid.addWidget(self.vfd_state, 0, 0)
        # [位置互调 2026-11-23] 第一行 = 运行状态 | 转速（主） | 输出频率。
        self._vfd_grid.addWidget(self.vfd_rpm, 0, 1)
        self._vfd_grid.addWidget(self.vfd_freq, 0, 2)
        # 第二行：功率 + 转矩 + 电机温度（三格，与第一行对齐）
        self._vfd_grid.addWidget(self.vfd_power, 1, 0)
        self._vfd_grid.addWidget(self.vfd_torque, 1, 1)
        self._vfd_grid.addWidget(self.vfd_mtemp, 1, 2)
        vfd_card.body.addLayout(self._vfd_grid)
        vfd_card.add_bottom_stretch()
        cards2.addWidget(vfd_card, 1)      # 第二排（194px 那张）

        # 健康卡（可点击 ⇒ 弹出设备状态表悬浮卡；对齐 C hsfHomeHealthClicked）
        # [改名 2026-11-23 用户口径「改名设备及仪表」] 卡片内容不变
        # （健康/故障/离线 + 13 条设备表），只是名字更贴现场叫法。
        h_card = _ClickableCard("设备及仪表")
        h_card.setToolTip("点击查看设备状态表")
        h_card.clicked.connect(lambda: self._card_clicked("table"))
        self._h_grid = QGridLayout()
        self._h_grid.setSpacing(10)
        # [单行三格 2026-11-12] 原 2x2 有空格且高；三项并一行。
        self.h_total = StatBlock("设备总数", "台", "--", value_size=34)
        self.h_fault = StatBlock("故障", "台", "0", value_size=34)
        self.h_offline = StatBlock("离线", "台", "0", value_size=34)
        self._h_grid.addWidget(self.h_total, 0, 0)
        self._h_grid.addWidget(self.h_fault, 0, 1)
        self._h_grid.addWidget(self.h_offline, 0, 2)
        h_card.body.addLayout(self._h_grid)
        h_card.add_bottom_stretch()
        cards3.addWidget(h_card, 1)        # 第三排（矮卡位）

        self.root.addLayout(cards)

        # ---- 第二排看板卡：流量计 / 电动球阀（压差卡见下，排在第一排）----
        #
        # [排布 2026-11-19 重排后] 三排的卡**按卡高从高到低**分排：
        #   第一排 出水浊度仪06 ｜ 按钮组202 ｜ 压差153
        #   第二排 搅拌泵194 ｜ 流量计153 ｜ 电动球阀147
        #   第三排 设备健康113 ｜ 电箱温度111 ｜ 最近报警97
        #   ⇒ 压差卡**挪到第一排**（构造代码也挪到按钮组之后 ⇒ 排内顺序
        #   与构造顺序一致，不用事后重排）。
        # [cards2 在上面已建] 三个排的布局都在「三排看板卡」那段一次建好
        # （卡片要跨段放进不同的排 ⇒ 布局必须先存在）。

        # 流量计卡 —— TOPIC_FLOWMETER（现场流量计挂 B 侧；Python 端服务端
        # 只发一份流量计快照，model.flowmeter 即该快照，口径与 C 优先 B 侧一致）。
        # 瞬时流量 + 正/反向累计（单位随服务端单位码下发）。
        fm_card = Card("流量计")
        self.fm_flow = StatBlock("瞬时流量", "", "--", value_size=40)
        # [滤速 2026-11-25 用户口径「这里增加滤速显示，单位 m/h，公式是
        #  瞬时流量/面积」] 放在主数值右侧的空位；面积在远端设置里配，
        #  服务端随帧算好 filterVelocityMh（m/h），这里只显示。
        self.fm_vel = StatBlock("罐体滤速", "m/h", "--", value_size=28)
        self.fm_vel.setToolTip(
            "罐体滤速 = 瞬时流量 ÷ 过滤面积（面积在远端设置 → 采集配置里配）")
        _fm_row = QHBoxLayout()
        _fm_row.setSpacing(8)
        _fm_row.addWidget(self.fm_flow, 1)
        _fm_row.addWidget(self.fm_vel, 0, Qt.AlignRight | Qt.AlignVCenter)
        fm_card.body.addLayout(_fm_row)
        # [累计 + 报警合并成一行 2026-11-12] 原先是两行小字（累计一行、
        # 报警一行）⇒ 流量计卡是第二排最高的一卡（139px），首页放不下。
        # 两段本来就能放一行 ⇒ 合并，报警用红色 span（富文本）保持醒目。
        # [报警码直接说中文 2026-11-10] 说明书口径：空管/上限/下限/系统，
        # 不显示 "0x0004"；口径与 C 端 fmAlarmText 一致。
        self.fm_sub = QLabel("无数据")
        self.fm_sub.setWordWrap(False)
        self.fm_sub.setStyleSheet(theme.caption_label_qss())
        fm_card.add(self.fm_sub)
        # 同排被拉高时多余高度沉底 ⇒ 标题/正文贴顶（见 Card.add_bottom_stretch）。
        fm_card.add_bottom_stretch()
        cards2.addWidget(fm_card, 1)

        # 电磁阀卡 —— TOPIC_VALVE 六路状态色点（色义对齐 pages_valve 卡片：
        # 已开绿 / 已关灰蓝 / 动作中橙 / 故障红 / 未配置·未知浅灰）。
        valve_card = Card("电动球阀")
        self._valve_grid = QGridLayout()
        self._valve_grid.setSpacing(6)
        self._valve_state = []          # 每路「● 状态」标签（色随状态）
        self._valve_name = []           # 每路阀名小字（服务端下发）
        self._valve_render = [None] * self.VALVE_COUNT   # 上次渲染缓存（免重排）
        for i in range(self.VALVE_COUNT):
            cell = QWidget()
            v = QVBoxLayout(cell)
            v.setContentsMargins(0, 0, 0, 0)
            v.setSpacing(0)
            state_lbl = QLabel("● --")
            state_lbl.setAlignment(Qt.AlignCenter)
            state_lbl.setStyleSheet(
                "QLabel { color: %s; font-size: 26px; background: transparent; }"
                % self.VALVE_NA_COLOR)
            name_lbl = QLabel("-")
            name_lbl.setAlignment(Qt.AlignCenter)
            # [小一号注释字 2026-11-12] 名字是注释性小字，比主题说明文案
            # 再小一档；必须走 font_px（跟随界面缩放，uiscal 段禁裸小 px）。
            name_lbl.setStyleSheet(
                "QLabel { color: palette(mid); font-size: %dpx;"
                " background: transparent; }" % theme.font_px(12))
            v.addWidget(state_lbl)
            v.addWidget(name_lbl)
            self._valve_grid.addWidget(cell, i // 3, i % 3)
            self._valve_state.append(state_lbl)
            self._valve_name.append(name_lbl)
        valve_card.body.addLayout(self._valve_grid)
        # 同排被拉高时多余高度沉底（电动球阀卡比搅拌泵矮）。
        valve_card.add_bottom_stretch()
        cards2.addWidget(valve_card, 1)

        self.root.addLayout(cards2)

        # ---- 第三排看板卡：浊度仪 / 按钮组系统状态（2026-11-09）----
        #
        # [排布 2026-11-19 重排后] 浊度仪与按钮组 ⇒ **第一排**（两张都是
        # 高卡 206/202，放第一排才不浪费；见上面「三排看板卡」的实测表）。
        # [cards3 在上面已建] 三个排的布局都在「三排看板卡」那段一次建好。

        # ---- 浊度仪卡：两台各一个读数块（名字取服务端，值 = NTU÷100）----
        turb_card = Card("浊度仪")
        self._turb_grid = QGridLayout()
        self._turb_grid.setSpacing(10)
        self._turb_blocks = []          # [StatBlock]（标题 = 服务端仪器名）
        self._turb_temps = []           # [QLabel]（水温；只有带温度的机型有）
        self._turb_render = [None] * self.TURBIDITY_COUNT
        for i in range(self.TURBIDITY_COUNT):
            cell = QWidget()
            v = QVBoxLayout(cell)
            v.setContentsMargins(0, 0, 0, 0)
            v.setSpacing(2)
            # [数字再放大 2026-11-24 用户「浊度仪卡片数字还可以再大点，有
            # 很多空白」] 36 → 42，但单位 NTU 收小到 22：两列并排各约
            # 157~175px，"3.52" 变大后总宽仍落在列宽内（原来 36+36 的
            # "3.52 NTU" 已接近列宽，直接放大数值会裁字）。单位字号固定
            # 22 ⇒ 自动缩放放大数值时单位不跟着涨，宽度增量只来自数值。
            block = StatBlock("浊度仪%d" % (i + 1), "NTU", "--",
                              value_size=self.TURB_VALUE_SIZE,
                              unit_size=self.TURB_UNIT_SIZE)
            v.addWidget(block)
            # [水温 2026-11-10] 带温度的机型（站3 ZHEYI-5100ZDY）随帧带
            # 水温；站1 WZT-701A 没有 ⇒ 该标签不显示（不是显示 0℃）。
            temp = QLabel("")
            temp.setStyleSheet(theme.caption_label_qss())
            temp.setVisible(False)
            v.addWidget(temp)
            # [两列读数同一行 2026-11-24 用户实报「进水/出水浊度不在同一行，
            #  web 版是同一行」] 两格高度不同（右格多一行「水温」，左格温度
            # 标签隐藏）⇒ 没有底部 stretch 时 QVBox 把余高摊给 StatBlock，
            # 两格的标题/数值基线就错开。加 stretch 让两格内容一律贴顶。
            v.addStretch(1)
            self._turb_grid.addWidget(cell, 0, i)
            self._turb_blocks.append(block)
            self._turb_temps.append(temp)
        turb_card.body.addLayout(self._turb_grid)

        # ---- 进水泵状态（2026-11-12 用户定案：并进浊度仪卡）----
        #
        # [为什么放这张卡] 首页版面已满（自动模式卡占了首行），用户指定把
        # 进水泵状态并到浊度仪卡里。
        # [为什么并进「说明行」而不是新起一行] 首页要一屏放下（1366x768
        # @1.5x 实测余量只有 3px），新起一行就把页面顶出屏幕 ⇒ 与浊度仪
        # 说明**共用一行**：有数据时这行显示泵状态，无数据时显示故障说明。
        # [口径] 进水泵**只有输出没有反馈**（现场无回流信号）⇒ on 只表示
        # "输出点是开的"，不是"泵真的在转" ⇒ 文案用「通/断」（与数字量
        # 页同口径），小字点明"输出点（无反馈）"，免得被当成实测状态。
        #: 浊度仪说明文字（由 _refresh_turb 写，_refresh_turb_pump 组合输出）。
        self._turb_note_text = ""
        #: 进水泵状态（富文本片段；由 _refresh_turb_pump 写）。
        self._pump_render = None

        self._turb_note = QLabel("")
        self._turb_note.setWordWrap(True)
        self._turb_note.setStyleSheet(theme.caption_label_qss())
        turb_card.add(self._turb_note)
        turb_card.add_bottom_stretch()
        cards.addWidget(turb_card, 1)       # 第一排（头位＝高卡位）

        # ---- 按钮组卡：4 触点 + 3 灯（2 行 4 列，第 8 格留空）----
        #
        # [为什么用「● 状态」QLabel 而不是 LampDot] LampDot 的三色写死在
        # paintEvent 里（绿=通/红=断/灰=禁用），按钮组需要「绿/灰/红」按
        # 语义自由切换 ⇒ 照抄同页电磁阀卡的 QLabel + QSS 颜色写法。
        btn_card = Card("按钮组系统状态")
        self._btn_grid = QGridLayout()
        self._btn_grid.setSpacing(6)
        self._btn_state = []           # 每格「● 状态」标签
        self._btn_name = []            # 每格名字小字
        self._btn_render = [None] * self.BTN_SLOT_COUNT
        for i in range(self.BTN_SLOT_COUNT):
            cell = QWidget()
            v = QVBoxLayout(cell)
            v.setContentsMargins(0, 0, 0, 0)
            v.setSpacing(0)
            state_lbl = QLabel("● --")
            state_lbl.setAlignment(Qt.AlignCenter)
            state_lbl.setStyleSheet(
                "QLabel { color: %s; font-size: 26px; background: transparent; }"
                % self.VALVE_NA_COLOR)
            name_lbl = QLabel("-")
            name_lbl.setAlignment(Qt.AlignCenter)
            # [小一号同阀卡 2026-11-12] 走 font_px（跟随界面缩放）。
            name_lbl.setStyleSheet(
                "QLabel { color: palette(mid); font-size: %dpx;"
                " background: transparent; }" % theme.font_px(12))
            v.addWidget(state_lbl)
            v.addWidget(name_lbl)
            # 行 0 = 4 个触点；行 1 = 3 盏灯（第 4 格留空）
            self._btn_grid.addWidget(cell, i // 4, i % 4)
            self._btn_state.append(state_lbl)
            self._btn_name.append(name_lbl)
        btn_card.body.addLayout(self._btn_grid)
        # [2026-11-25 用户「字被裁剪了，不是显示不全用滚动吗」] 这条
        # 「未配置：…」提示 8 个槽位全缺时很长，原 QLabel+WordWrap 折成
        # 两行、高度只给一行(_set_note 给 fontMetrics().height()+2) ⇒ 第二
        # 行被裁成半截字。改压力卡同款 MarqueeLabel：单行横向滚动、超宽循环，
        # 宽度不参与布局最小宽，长文案不再被裁也不撑宽卡片。
        self._btn_note = MarqueeLabel()
        self._btn_note.setWordWrap(False)
        self._btn_note.setStyleSheet(theme.caption_label_qss())
        btn_card.add(self._btn_note)
        btn_card.add_bottom_stretch()
        cards.addWidget(btn_card, 1)        # 第一排（第二张）

        # ---- 压力及压差卡（第一排第三张；构造代码挪到这儿 ⇒ 排内顺序=构造顺序）
        #
        # [改名 2026-11-23 用户口径「卡片名字修改为压力及压差」] 卡里既有
        # 压差、又有压力1/压力2 ⇒ 标题如实叫「压力及压差」。
        # [布局第三版 2026-11-23 用户「压力1 压力2 之间的行间距缩小下」]
        # 旧实现每栏都带一条独立说明行（空文本也占一行）⇒ 两路压力之间隔
        # 着一条空行。现在说明收成**卡底一行共享**（恒占位、有 ew 才上警示
        # 色、前缀写清哪一路：「压差：…」「压力1：…」），压力1/压力2 之间
        # 只隔 6px ⇒ 读数紧凑；说明行高度恒定 ⇒ 几何不随 ew 有无而跳。
        # 左列压差垂直居中、与右列两行同一视觉重心，中间一条软分隔线分组。
        # [数据取法] 名字由服务端随 TOPIC_AI 的 en{i} 下发 ⇒ 三路都**按
        # 名字找通道**（压差栏兜底 CH0，老服务端只有 en0）；找不到 ⇒ "--"
        # （不拿别的通道的值冒充，也不编造 0）。
        dp_card = Card("压力及压差")
        self.dp_card = dp_card
        # [布局第四版 2026-11-23 用户口径「压差 数值 单位写在一行，另外的
        #  两个压力一样的布局分别两行，总共三行」] 三行同构：每行 = 标签 +
        # 读数 + 单位（InlineStat 一行式）。整卡宽度给读数用，大字号不挤；
        # 行距 6px，三行自然排。
        self.dp_rows = QVBoxLayout()
        self.dp_rows.setContentsMargins(0, 0, 0, 0)
        self.dp_rows.setSpacing(6)
        self.dp_value = InlineStat(
            "压差", "", "--", value_size=self.PRESS_VALUE_SIZE)
        self.dp1_value = InlineStat(
            proto.PRESS1_LABEL, "", "--", value_size=self.PRESS_VALUE_SIZE)
        self.dp2_value = InlineStat(
            proto.PRESS2_LABEL, "", "--", value_size=self.PRESS_VALUE_SIZE)
        # 三行标签宽度对齐（压差/压力1/压力2 字数不同 ⇒ 数值左边界要齐）。
        _blocks = (self.dp_value, self.dp1_value, self.dp2_value)
        # 标签列宽度按**自动缩放上限**量（value*1.3*0.72）：标签字号随
        # 数值放大，若按基准量宽会被裁字（用户「这几个字也同步大点」）。
        for _b in _blocks:
            _b._caption.ensurePolished()
        _cap_max_px = int(round(self.PRESS_VALUE_SIZE * 1.30 * 0.72))
        _cap_font = QFont(_blocks[0]._caption.font())
        _cap_font.setPixelSize(_cap_max_px)
        _fm = QFontMetrics(_cap_font)
        _cap_w = max(_fm.horizontalAdvance(_b._caption.text())
                     for _b in _blocks) + 8
        for _b in _blocks:
            _b._caption.setFixedWidth(_cap_w)
            self.dp_rows.addWidget(_b)
        # 共享说明行（三路 ew 说明合并；无说明时收起不占行）。
        self.dp_note = self._press_note_label()
        self._set_note_hold(self.dp_note, "", "#5A6572")
        #: (数值块, 服务端通道名, 找不到名字时的兜底通道, 说明前缀)
        self._dp_columns = (
            (self.dp_value, self.PRESS_DIFF_NAME, 0, "压差"),
            (self.dp1_value, self.PRESS1_NAME, None, proto.PRESS1_LABEL),
            (self.dp2_value, self.PRESS2_NAME, None, proto.PRESS2_LABEL),
        )
        dp_card.body.addLayout(self.dp_rows)
        dp_card.body.addWidget(self.dp_note)
        dp_card.add_bottom_stretch()
        cards.addWidget(dp_card, 1)         # 第一排（第三张）

        # ---- 最近报警卡（2026-11-19 用户定案「主页加一张最近报警卡」）----
        #
        # [主页确实缺这块] 现场最常问的两件事之一就是"刚才发生了什么" ⇒
        # 主页给一行摘要（时间 + 类型 + 原文），**点整卡跳报警页**看全量。
        #
        # [为什么放第三行] 第三行原本只有两张卡（浊度仪/按钮组）⇒ 底部留
        # 了一大块空（板端 1280x800@150% 实测 ~107px）。加进第三行后
        # **不新增行高**（该行最高卡仍是浊度仪/按钮组），却把那块空填上了。
        #
        # [可点] 用 _ClickableCard（同设备健康/搅拌泵卡的手感）：**按下**
        # 即跳报警页（触控口径）。跳转走 ctx.navigate（既有页面跳转入口）。
        alarm_card = _ClickableCard("最近报警")
        alarm_card.setToolTip("点击查看报警页")
        alarm_card.clicked.connect(self._alarm_card_clicked)
        # 内容：时间 + 类型 + 原文（无报警 ⇒ 「无报警」）。
        # [为什么用两行而不是一行] 类型与原文可能都很长 ⇒ 一行放不下会被
        # 截断（"COM17 · 115200 · 站 2" 那种老问题）；两行各占一行 ⇒
        # 时间/类型一行、原文一行（原文超长用省略号）。
        self._alarm_meta = QLabel("--")
        self._alarm_meta.setWordWrap(False)
        self._alarm_meta.setStyleSheet(theme.caption_label_qss())
        self._alarm_text = QLabel("无报警")
        self._alarm_text.setWordWrap(True)
        self._alarm_text.setStyleSheet(theme.caption_label_qss())
        alarm_card.add(self._alarm_meta)
        alarm_card.add(self._alarm_text)
        alarm_card.add_bottom_stretch()
        cards3.addWidget(alarm_card, 1)

        self.root.addLayout(cards3)

        # ---- 看板卡片：默认排布 + 长按拖拽自己排序（2026-11-23 用户口径
        #      「按钮与电动球阀的卡片交换位置，支持下主界面长按可以拖拽卡片
        #      进行自己排序，并本地持久化记录位置」）----
        # 默认顺序 = BOARD_CARD_IDS（已按用户要求把「按钮组系统状态」与
        # 「电动球阀」换位）；本机改过的顺序存在 INI（Home/CardOrder），
        # 下次启动按它铺；存储值不是 9 张卡的合法排列 ⇒ 回默认（fail-closed）。
        self._board_rows = (cards, cards2, cards3)
        self._board_cards = {
            "turb": turb_card, "valve": valve_card, "dp": dp_card,
            "vfd": vfd_card, "fm": fm_card, "btn": btn_card,
            "th": th_card, "health": h_card, "alarm": alarm_card,
        }
        self._board_order = list(self.BOARD_DEFAULT_ORDER)
        self._apply_board_order(self._load_board_order())
        # [A/B 开关] HSF_NO_CARD_DND=1 时不起拖拽（排障用：全局事件过滤器
        # 与 Qt 退出期的交互需要能一键关掉对照）。
        import os as _os
        if not _os.environ.get("HSF_NO_CARD_DND"):
            from .card_dnd import CardDragController
            self._board_drag = CardDragController(
                self, self._board_cards, self._on_card_swap,
                parent=self)
        # [数值自动缩放 2026-11-23 用户「看看能不能文字自动缩放下，减少空白」]
        # 记下每张卡数值块的**基准字号**：卡片随整排等高拉伸后，短卡按余高
        # 把数值放大（上限 1.3×），把空白吃掉；放大会撑高就回退（不改行高）。
        self._board_base_sizes = {}
        for _cid, _card in self._board_cards.items():
            _blocks = list(_card.findChildren(StatBlock)) \
                + list(_card.findChildren(InlineStat))
            _base = [(b, int(getattr(b, "_value_size", 0)))
                     for b in _blocks if getattr(b, "_value_size", 0)]
            if _base:
                self._board_base_sizes[_cid] = _base
        self._autofit_pending = False
        QTimer.singleShot(0, self._autofit_board_values)

        # [快捷入口行已删 2026-11-12] 1366x768@1.5x 下首页塞不进一屏，
        # 这一行的三项（设备/数据/设置）与左侧导航树**完全重复**，
        # 删除省出一整行（~130px）。原实现（三张 84px 大按钮 + 内嵌
        # 双行 QLabel 的排版实锅记录）见 git 历史。

        # ---- 设备状态表（装进悬浮详情卡；见类注释）----
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
        # [列权重重分配 2026-11-19 用户「地址列显示不全」]
        #   原 (4,2,2,7,5)：地址列 7 份、错误信息列 5 份。
        #   · 错误信息列**多数行是空的**（无故障时只有 "-"）却占 5 份 ⇒ 压到 3；
        #   · 地址列要放得下 "COM17/115200/#2"（瘦身后的串，17 字符）⇒ 提到 10。
        #   ⇒ 新 (4,2,2,10,3)：总权重 21（原来 20），地址列实际占比从 35%
        #     提到 48%。
        # [地址列宽度的保证靠**权重 + 内容下限**，不靠 auto_min_texts]
        #   auto_min_texts 的下限是**硬下限**（不按预算回退）⇒ 在**没装
        #   字体**的环境（离屏回归）里量出来的宽度是垃圾值（如 330px）
        #   ⇒ 5 列下限合计 930 > 视口 910 ⇒ **横向滚动条**（回归锁 K 段
        #   「列宽合计不超出视口」变红）。
        #   ⇒ 改用框架既有的**预算内内容抬升**（page_base._plan_widths 的
        #   raise_list：内容需要的宽度装得下才抬、装不下就保持权重口径）
        #   + 权重 10 ⇒ 地址列稳拿 ~225px（瘦身后的 "COM17/115200/#2"
        #   约 165px，实机装得下 ⇒ 不截字）。
        self._table.set_column_weights((4, 2, 2, 10, 3))
        # [点地址格复制完整地址] 板子是**触摸屏、没有 hover** ⇒ tooltip 只是
        #   桌面端的辅助 ⇒ 真正的兜底是"点地址格 ⇒ 复制完整地址到剪贴板
        #   + 状态行提示「已复制」"（触摸板上没有 hover 可靠）。
        #   [点任意格=弹单行详情卡，地址格另复制] 2026-11-22 用户口径「显示
        #   不全，单击一行弹一张卡片详细显示」：表格里地址/错误信息放不下，
        #   点行弹 `_DeviceDetailCard` 看原文；地址格**额外**复制（互不冲突）。
        self._table.table.cellClicked.connect(self._on_device_cell_clicked)
        # 完整地址逐行缓存（点地址格复制用；行号索引）。
        self._addr_full = []
        # 表格高度贴合 5 行内容（否则浮层拖出一整屏空白）
        self._table.fit_to_content(extra_rows=5)

        # ---- 泵详情（悬浮详情卡的 pump 态内容；对齐 C HOME_DETAIL_PUMP）----
        self._pump_detail = QWidget()
        pd_grid = QGridLayout(self._pump_detail)
        pd_grid.setContentsMargins(4, 4, 4, 8)
        pd_grid.setHorizontalSpacing(18)
        pd_grid.setVerticalSpacing(8)
        self.pd_state = StatBlock("运行状态", "", "--", value_size=24)
        self.pd_freq = StatBlock("输出频率", "Hz", "--", value_size=24)
        self.pd_setf = StatBlock("设定频率", "Hz", "--", value_size=24)
        self.pd_cur = StatBlock("输出电流", "A", "--", value_size=24)
        self.pd_rpm = StatBlock("搅拌转速", "rpm", "--", value_size=24)
        # [功率/转矩 2026-11-19] 详情浮层补上 ⇒ 与 C 端详情卡
        # （HOME_TXT_DP_POWER / HOME_TXT_DP_TORQUE）**字段齐平**。
        self.pd_power = StatBlock("输出功率", "kW", "--", value_size=24)
        self.pd_torque = StatBlock("转矩", "%", "--", value_size=24)
        for col, block in enumerate((self.pd_state, self.pd_freq,
                                     self.pd_setf)):
            pd_grid.addWidget(block, 0, col)
        for col, block in enumerate((self.pd_cur, self.pd_rpm,
                                     self.pd_power)):
            pd_grid.addWidget(block, 1, col)
        pd_grid.addWidget(self.pd_torque, 2, 0)
        self._pump_detail.setVisible(False)

        # ---- 悬浮详情卡 + 触摸遮罩（点卡弹出、居中悬浮；×/点外面收起）----
        # 对齐 C HsfHomePage.c 创建段（~1932）：标题行（标题随态切文字 +
        # × 关闭钮）+ 内容（设备表 / 泵详情二选一显示）。
        self._shade = _HomeShade(self._close_detail, self)
        self._detail_float = QFrame(self)
        self._detail_float.setObjectName("HsfHomeDetailFloat")
        self._detail_float.setStyleSheet(
            "QFrame#HsfHomeDetailFloat { background: #FFFFFF;"
            " border: 1px solid %s; border-radius: %dpx; }"
            % (theme.CLR_BORDER_SOFT, theme.RADIUS_CARD))
        fl = QVBoxLayout(self._detail_float)
        fl.setContentsMargins(12, 10, 12, 12)
        fl.setSpacing(6)
        title_row = QHBoxLayout()
        self._detail_title = QLabel("设备状态")
        self._detail_title.setStyleSheet(
            "QLabel { color: #1F4E79; font-weight: bold; font-size: 21px;"
            " background: transparent; }")
        title_row.addWidget(self._detail_title)
        title_row.addStretch(1)
        self._detail_close = QPushButton("×")
        self._detail_close.setFixedSize(28, 28)
        self._detail_close.setCursor(Qt.PointingHandCursor)
        self._detail_close.setToolTip("收起")
        self._detail_close.clicked.connect(self._close_detail)
        title_row.addWidget(self._detail_close)
        fl.addLayout(title_row)
        fl.addWidget(self._table)
        fl.addWidget(self._pump_detail)
        self._shade.hide()
        self._detail_float.hide()

        self.root.addStretch(1)

    #: 铭牌额定点（现场口径 50Hz ≈ 1440rpm；与 VFD 页/服务端同一条）。
    HOME_RPM_PER_HZ = 1440.0 / 50.0

    @classmethod
    def _vfd_display_rpm(cls, vfd):
        """主页转速读数（rpm；None = 没有数据）。

        [取值优先级] ① 服务端 `speedRpm`（设备层按输出频率换算）；
        ② 老服务端缺该字段 ⇒ 按输出频率本地换算；③ 都没有 ⇒ None。
        [稳态对齐给定 2026-11-25 用户实报「设定 600 转，主界面显示 599」]
        变频器频率给定是百分比量（0x1000，0.01% 档），而输出频率回读
        可能落在 20.80Hz ⇒ 600rpm 换算成 599.0rpm（**不是显示截断**，是
        0.1Hz 级量化）。VFD 本身没有转速传感器，这个换算值本来就带
        ±1rpm 级的量化误差 ⇒ 当**运行中且已在给定附近**（≤1% 或 2rpm）
        时，显示**给定换算值**（用户设定的那个数），避免"设 600 显示 599"
        被当成故障。加速/减速/停机过程中差值大，仍显示实际换算值。
        """
        if vfd.has("speedRpm"):
            rpm = float(vfd.speed_rpm)
        elif vfd.has("outputFreqHz"):
            rpm = float(vfd.output_freq_hz) * cls.HOME_RPM_PER_HZ
        else:
            return None
        if vfd.has("setFreqHz") and bool(getattr(vfd, "running", False)):
            target = float(vfd.set_freq_hz) * cls.HOME_RPM_PER_HZ
            if target > 0 and abs(rpm - target) <= max(2.0,
                                                      abs(target) * 0.01):
                return target
        return rpm

    @classmethod
    def _vfd_rpm_text(cls, vfd):
        """主页搅拌泵卡的转速文案（rpm）。

        [口径] 现场铭牌 50Hz ≈ 1440rpm ⇒ **28.8 rpm/Hz**（C 端
        HOME_RPM_PER_HZ 同值，服务端设备层也按同一额定点算）⇒ 三端一致。
        [停机时显示 0 而不是 "--"] 变频器停了 ⇒ 输出频率 0 ⇒ 转速**就是 0**
        （一个真值，不是"没数据"）⇒ 与变频器页的「输出转速 0 rpm」和 C 端
        一致；只有**字段整个缺失**才显示 "--"。
        [不是实测转速] 变频器没有转速寄存器，滑差随负载变 ⇒ 属换算值。
        """
        rpm = cls._vfd_display_rpm(vfd)
        return "--" if rpm is None else "%.0f" % rpm

    def refresh(self, force=False):
        """每拍刷新（200ms）。

        [分帧] 数值卡每拍刷（QLabel.setText 同值早退，稳态零重排）；
        设备表每 2 拍铺一次 —— setRowCount / fit_to_content 会触发布局
        重排，摊开到两帧、避免单拍重排整页（对齐 C HsfHomePage 帧泵注释
        hsfStatSet：把重活摊到多帧，不无变化也整页标脏）。
        `force=True` 一拍刷齐（入页/测试用）。
        """
        model = self.ctx.model

        # ---- 温湿度（按侧择优：A 侧优先，B 侧兜底）----
        # [为什么择优而不是直接用最后一帧] 服务端按两条侧轮流推同一主题，
        # 没装温湿度站的那一侧推的是空帧 ⇒ 只留最后一帧时界面恒 "--"
        # （现场实测）。口径对齐 C 端 hsfHomePickTempHum。
        th = model.temphum.pick()
        # [显示渐变 2026-11-24 用户「温度也支持下渐变」] 电箱温度按远端
        # 「显示渐变范围」上色；湿度不渐变（用户没提，且湿度没有危险上限
        # 的通用口径）。范围缺键（老服务端）保持原来的单色。
        _th_t = th.temp_value() if th is not None else None
        _th_rng = th.temp_range() if th is not None else None
        _th_color = (theme.gradient_color(_th_t, _th_rng[0], _th_rng[1])
                     if (_th_t is not None and _th_rng is not None
                         and th.temp_gradient) else None)
        self.th_temp.set_value(th.temp_text() if th is not None else "--",
                               _th_color)
        self.th_hum.set_value(th.hum_text() if th is not None else "--")

        # ---- 变频器 ----
        vfd = model.vfd
        if not vfd.is_fresh():
            self.vfd_state.set_value("无数据", theme.CLR_OFFLINE)
            self.vfd_freq.set_value("--")
            self.vfd_rpm.set_value("--")
            self.vfd_power.set_value("--")
            self.vfd_torque.set_value("--")
            self.vfd_mtemp.set_value("--")
        else:
            color = (theme.CLR_FAULT if vfd.faulted
                     else (theme.CLR_OK if vfd.running else theme.text_color()))
            self.vfd_state.set_value(vfd.run_state_text(), color)
            self.vfd_freq.set_value(
                "%.2f" % vfd.output_freq_hz if vfd.has("outputFreqHz") else "--")
            # [电机转速渐变 2026-11-24 用户追加] 转速按远端范围上色；缺范围
            # 键（老服务端）保持单色。
            _spd_rng = vfd.speed_range()
            # 与读数同一取数口径（稳态对齐给定）⇒ 数值与颜色不会一个说
            # 600、一个按 599 上色。
            _spd = self._vfd_display_rpm(vfd)
            _spd_color = (theme.gradient_color(_spd, _spd_rng[0], _spd_rng[1])
                          if (_spd_rng is not None and _spd is not None
                              and vfd.speed_gradient)
                          else None)
            self.vfd_rpm.set_value(self._vfd_rpm_text(vfd), _spd_color)
            # [功率/转矩 2026-11-19] 缺键 ⇒ "--"（不编造 0）；有键 ⇒ 按
            # 服务端下发的原值显示（功率保留两位小数、转矩保留一位）。
            self.vfd_power.set_value(
                "%.2f" % vfd.output_power_kw if vfd.has("outputPowerKw") else "--")
            self.vfd_torque.set_value(
                "%.1f" % vfd.torque_percent if vfd.has("torquePercent") else "--")
            # [电机温度 2026-11-20] 缺键（未配/读不到）⇒ "--"；有键 ⇒ 一位
            # 小数（温度传感器 0.1℃ 分辨率足够）。
            # [显示渐变 2026-11-24 用户「电机温度也支持下 / 电机温度呢」]
            # 温度着色范围由远端「显示渐变范围·电机温度」单独下发（缺键回落
            # 模拟量温度传感器的量程）。
            _mt_rng = vfd.motor_temp_range()
            if _mt_rng is None:
                _mt_ch = self._analog_channel_by_name(
                    model, "温度传感器", None)
                if _mt_ch is not None:
                    _mt_rng = model.analog.eng_range(_mt_ch)
            _mt_color = (theme.gradient_color(vfd.motor_temp_c,
                                             _mt_rng[0], _mt_rng[1])
                         if (_mt_rng is not None
                             and vfd.has("motorTempC")
                             and vfd.motor_temp_gradient) else None)
            self.vfd_mtemp.set_value(
                "%.1f" % vfd.motor_temp_c if vfd.has("motorTempC") else "--",
                _mt_color)

        # ---- 健康 ----
        # [set_value_colored 2026-11-12] set_value(text, color) 会把字号
        # 硬写回 30px（StatBlock 老坑），盖掉构造时的 18px ⇒ 健康卡比邻
        # 卡高出一截。带色改值一律走 set_value_colored 保留字号。
        health = model.health
        fault, offline = health.counts()
        self.h_total.set_value(str(len(health.devices)))
        self.h_fault.set_value_colored(
            str(fault), theme.CLR_FAULT if fault else theme.CLR_OK, 18)
        self.h_offline.set_value_colored(
            str(offline), theme.CLR_WARN if offline else theme.CLR_OK, 18)

        # ---- 压差（TOPIC_AI 通道 0）----
        self._refresh_dp(model)
        # ---- 流量计（TOPIC_FLOWMETER，B 侧快照）----
        self._refresh_fm(model)
        # ---- 电磁阀（TOPIC_VALVE 六路）----
        self._refresh_valves(model)
        # ---- 浊度仪（TOPIC_TURBIDITY 两台，2026-11-09）----
        self._refresh_turb(model)
        # ---- 进水泵（浊度仪卡内，2026-11-12）----
        self._refresh_turb_pump(model)
        # ---- 按钮组（TOPIC_DIGITAL 的 DI 触点 + DO 灯，2026-11-09）----
        self._refresh_buttons(model)
        # ---- 自动模式卡（相位/锁存回显 + 1s 轮询，2026-11-11）----
        self._refresh_auto_card(model)
        # ---- 最近报警卡（一行摘要；2026-11-19）----
        self._refresh_alarm_card(model)

        # ---- 设备表（分帧：每 2 拍铺一次；force 一拍刷齐）----
        if force or self._refresh_phase % 2 == 0:
            self._refresh_device_table(model)
        self._refresh_phase = (self._refresh_phase + 1) % 2

        # ---- 泵详情（仅悬浮卡 pump 态刷，对齐 C detailMode 口径）----
        if self._detail_mode == "pump":
            self._refresh_pump_detail(model)

    # ---- 自动模式卡（2026-11-11；序列逻辑在服务端，客户端发意图+回显）----

    #: 相位的界面文案（KEY_AUTO_PHASE → 状态行；与服务端 PHASE_* 同值）。
    #: [短语化 2026-11-12] 表盘挪到状态行右侧后，状态行只剩卡片左半宽
    #: （~650px）；原先行内带括号的详细说明（~40 字）会被表盘遮住，
    #: 只留“在做什么”，细节看设备卡（阀/泵状态）。
    AUTO_PHASE_TEXT = {
        "valves": "阀动作中…",
        "pumps": "启动泵…",
        # [running 相不写文案 2026-11-12] 状态行已经有"运行中 · 模式名"，
        # 再拼一个"运行中"就重复了（"运行中 · 产水模式 · 运行中"）。
        "running": "",
        # [paused 2026-11-19] 停止＝暂停 ⇒ 相位报 "paused"（服务端
        # 语义：按启动从暂停处续跑；复位才清零）。
        "paused": "已暂停（按启动继续）",
    }

    #: 运行时长标签的**最长可能文本**（用于定宽；2026-11-12）。
    #:
    #: [只含时长部分] 「自动运行」是**另一个标签**（见构造处注释：前缀被裁
    #: 的实锅），所以这里只需容得下时长串；天数封顶 999+（见 _fmt_auto_run）
    #: ⇒ 与 3 位天同宽。
    AUTO_RUN_MAX_TEXT = "999+天 23时 59分 59秒"

    def _selected_auto_mode(self):
        """下拉当前选中的模式 id（含「自动」组合）。"""
        return self.auto_mode_combo.currentData()

    # ---- 累计自动运行时长（天 时 分 秒）----

    @staticmethod
    def _fmt_auto_run(ms):
        """毫秒 → 「N天 HH时 MM分 SS秒」。

        [为什么自己格式化而不是用 QDateTime] 这是**时长**不是时刻
        （不是"几点几分"），没有对应的日期时间类型；而且要超过 24h
        也能显示（天 位数不封顶）。

        [天数封顶 999+] 标签是**定宽**的（每秒变一次，宽度必须恒定否则
        整行重排抖动）⇒ 天数不能无限长，否则宽度算不准、文字被裁。
        超过 999 天（2.7 年）显示「999+天」——现场不会有更长的机龄，且
        宽度与 3 位天一致。
        """
        total = max(0, int(ms or 0))
        secs = total // 1000
        days, rem = divmod(secs, 86400)
        hours, rem = divmod(rem, 3600)
        minutes, seconds = divmod(rem, 60)
        days_txt = "999+" if days > 999 else str(days)
        return "%s天 %02d时 %02d分 %02d秒" % (days_txt, hours, minutes,
                                            seconds)

    def _auto_run_text_width(self, text, font=None):
        """按字体实测文本宽（+8px 余量）；不写死像素。

        :param font: 用哪个字体测（缺省 = 控件自己的）。**必须用标签的
            字体**：标签 QSS 是 caption 字号（18px），与窗口默认字体不同；
            拿窗口字体测 ⇒ 宽度算窄 ⇒ 文字被裁（实测差 17px）。
        """
        from PyQt5.QtGui import QFontMetrics
        fm = QFontMetrics(font if font is not None else self.font())
        return fm.width(text) + 8

    def _auto_run_apply_width(self):
        """按标签**实际字体**校准定宽（QSS polish 后字体才最终确定）。

        [为什么要在刷新时复核] 构造那一刻 QSS 可能还没 polish，label.font()
        拿到的是继承的窗口字体 ⇒ 测出来的宽度与实际渲染不一致（实测窄
        17px ⇒ 「自动」两字又被裁了一次）。刷新时复核一次即可：宽度通常
        只在校准时变一次 ⇒ 不会每秒抖。
        """
        need = self._auto_run_text_width(self.AUTO_RUN_MAX_TEXT,
                                         self._auto_run_label.font())
        if self._auto_run_label.minimumWidth() != need:
            self._auto_run_label.setFixedWidth(need)

    def _auto_mode_selected(self, index):
        """下拉改选 ⇒ 发 op=5 记粘性（不启动不动设备）。

        [静默成功] 成功不打扰状态栏（选模式是轻操作）；失败（未连接/
        权限/未知 id）才上状态栏——否则每次切模式都把「正在启动…」
        之类的重文案顶掉。
        """
        bridge = self.ctx.bridge
        if not bridge.is_connected():
            self.ctx.set_status("未连接服务端：模式选择未同步（物理启动"
                                "按钮将用服务端上次记住的模式）")
            return
        mode = self.auto_mode_combo.itemData(index)
        name = self.auto_mode_combo.itemText(index)

        def _done(doc):
            if not doc.get("ok"):
                self.ctx.set_status("模式选择失败：%s" % doc.get("err", "未知"))
        bridge.send_auto_select(mode, on_done=_done)
        # 权限不足时服务端拒绝但按钮不置灰（选模式是轻操作，普通用户也
        # 允许切换显示；服务端 op=5 与启动一样走 AutoControl 闸）。
        self.ctx.set_status("已选模式：%s（现场启动按钮将按此模式启动）"
                            % name)

    def _auto_start_clicked(self):
        """启动所选模式。序列在服务端跑，这里只发意图并给状态栏终态。"""
        bridge = self.ctx.bridge
        if not bridge.can(proto.PERM_AUTO_CONTROL):
            self.ctx.set_status("权限不足：自动运行控制需要管理员账户")
            return
        if not bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        mode = self._selected_auto_mode()
        name = dict(proto.AUTO_MODES).get(mode, "自动（流量循环）")
        self.ctx.set_status("正在启动自动运行（%s）…" % name)

        def _done(doc):
            # [网络线程→信号] send_auto_control 的 _done 在网络线程回调，
            # set_status 内部走信号投递回 UI 线程（与远端设置页同口径）。
            if doc.get("ok"):
                self.ctx.set_status("自动运行已受理（%s），设备按序列动作中"
                                    % name)
            else:
                self.ctx.set_status("启动失败：%s" % doc.get("err", "未知"))

        bridge.send_auto_control(True, on_done=_done, mode=mode)

    def _auto_stop_clicked(self):
        """停止自动运行并停全部设备（服务端永远受理停止）。"""
        bridge = self.ctx.bridge
        if not bridge.can(proto.PERM_AUTO_CONTROL):
            self.ctx.set_status("权限不足：自动运行控制需要管理员账户")
            return
        if not bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        self.ctx.set_status("正在停止自动运行…")

        def _done(doc):
            if doc.get("ok"):
                self.ctx.set_status("自动运行已停止（设备已全停）")
            else:
                self.ctx.set_status("停止失败：%s" % doc.get("err", "未知"))

        bridge.send_auto_control(False, on_done=_done)

    def _auto_reset_clicked(self):
        """复位：清报警锁存 + 急停锁存（回停止态，**不启动**）。"""
        bridge = self.ctx.bridge
        if not bridge.can(proto.PERM_AUTO_CONTROL):
            # [文案 2026-11-12 订正] 复位与启动/停止同一个闸（PERM_AUTO_CONTROL
            # = 管理员，permissions.py denied_text 同款）——旧文案写
            # 「操作者或管理员」与服务端真实闸不符（照它提示去找操作者
            # 账户还是被拒）。C 端早已是「管理员」口径，三处统一。
            self.ctx.set_status("权限不足：自动运行控制需要管理员账户")
            return
        if not bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        self.ctx.set_status("正在复位（清除报警/急停锁存）…")

        def _done(doc):
            if doc.get("ok"):
                self.ctx.set_status("已复位：报警与急停锁存已解除，可按启动")
            else:
                self.ctx.set_status("复位失败：%s" % doc.get("err", "未知"))

        bridge.send_auto_reset(on_done=_done)

    @staticmethod
    def _auto_progress_suffix(m):
        """循环进度后缀：「第 c/N 遍」（组内）与「第 r/N 轮」（总轮次）。

        [2026-11-27 用户「组合有多次循环要在主界面提示总共几次、当前是
        第几次」+「远端设置顶层支持循环次数」] 数据全部来自服务端状态键
        （autoRepCurrent/Total、autoRound/Total）：
          · 遍数只在 **总遍数 >= 2** 时显示（==1 是"没重复"，写出来反而
            像有进度；0 = 不可用 ⇒ 不显示）；
          · 轮次：配置了总轮数 ⇒「第 r/N 轮」；总轮数为 0（一直循环/老
            服务端）但当前轮次可用 ⇒「第 r 轮」（不编造 /N）。
        顺序：组遍数在前、总轮次在后（先"组内第几遍"再"第几轮"）。
        缺键/老服务端 ⇒ 全部 0 ⇒ 返回空串（**不编造进度**）。
        """
        try:
            rep_cur = int(getattr(m, "auto_rep_current", 0) or 0)
            rep_total = int(getattr(m, "auto_rep_total", 0) or 0)
            round_cur = int(getattr(m, "auto_round", 0) or 0)
            round_total = int(getattr(m, "auto_round_total", 0) or 0)
        except (TypeError, ValueError):
            return ""
        parts = []
        if rep_total >= 2 and rep_cur > 0:
            parts.append("第 %d/%d 遍" % (rep_cur, rep_total))
        if round_cur > 0:
            if round_total > 0:
                parts.append("第 %d/%d 轮" % (round_cur, round_total))
            else:
                parts.append("第 %d 轮" % round_cur)
        return " · ".join(parts)

    def _refresh_auto_card(self, m):
        """回显真实相位 + 1s 节流拉一次服务端状态。

        [状态文本只认服务端回的 phase/mode/fault] 不在客户端推断"启动了
        所以应该在阀相"——推断出来的状态在序列被急停/报警打断时会撒谎。
        """
        bridge = self.ctx.bridge
        now = time.time()
        if bridge.is_connected() and now - self._auto_poll_at >= 1.0:
            self._auto_poll_at = now
            bridge.refresh_run_mode()
        can_ctrl = (bridge.is_connected()
                    and bridge.can(proto.PERM_AUTO_CONTROL))
        running = m.is_auto_running()
        fault = bool(m.auto_fault)
        # [报警锁存 ⇒ 启动灰] 服务端反正会拒（"有报警未复位"），界面置灰
        # 让操作员在按之前就知道要先进复位，而不是点了才吃一条失败。
        self.auto_start_btn.setEnabled(
            can_ctrl and m.is_auto() and running is not True and not fault)
        self.auto_stop_btn.setEnabled(can_ctrl and running is True)
        # [复位按钮：锁存或**暂停态**才亮] 常态灰着——复位是"解除锁存/清
        # 暂停记忆"的动作，不是日常操作，亮着会招误触（按了会清掉正在看的
        # 故障现场）。锁存判据：**报警锁存**（autoFault）或**急停态**
        # （phase=estop）。
        # [暂停态 2026-11-27 用户实报「停止暂停时复位没法按，无法复位状态」]
        # 停止＝暂停 ⇒ 记忆了模式/阶段/计时，**只有复位能把暂停记忆清掉
        # 从头开始**（用户 2026-11-19 口径）。原判据不含 paused ⇒ 暂停时
        # 按钮灰着、复位无从触发（现场卡死）。补上 phase=="paused"。
        estop_latched = (m.auto_phase == "estop")
        paused = (m.auto_phase == "paused") or bool(
            getattr(m, "auto_paused", False))
        self.auto_reset_btn.setEnabled(
            can_ctrl and (fault or estop_latched or paused))

        # [循环运行中 ⇒ 下拉回显「自动（流量循环）」] 下拉本来是“下次启动的
        # 选择”，但循环真在跑时若它停在某个单模式，界面就会出现矛盾：
        # 下拉写“产水模式”、状态行写“反洗模式 / 剩余 8分49秒”——操作员会
        # 以为单模式也有倒计时（用户实看截图提出）⇒ 在**上升沿**回显一次
        # （不每拍抢：之后操作员自己换下拉选下一轮模式时不打断他）。
        # [判据 2026-11-23 订正] 旧判据「阶段计时可用（elapsed>=0）」不再
        # 成立：单模式现在**也报**运行已用时长（stage_timing 新口径，用户
        # 要求所有模式都有运行时长）⇒ 会把单模式错标成「自动循环（产水
        # 模式）」并把下拉抢到"自动（流量循环）"（网页端同款实锅）。
        # [判据订正 2026-11-27 工作流实证] 运行中服务端 autoMode 报的是
        # **当前子阶段**（produce/rinse…），不是 "auto"——原判据
        # `auto_mode == "auto"` 在循环运行中**恒假**（网页端同款 bug）。
        # 循环的唯一可靠信号 = 服务端下发的 autoStageTitle 非空（新服务端，
        # 循环运行/暂停都给）；老服务端缺该键 ⇒ 退回 autoMode=="auto"
        # 兜底（只覆盖停止态，不编造）。
        _stage_title_now = str(getattr(m, "auto_stage_title", "") or "")
        cycle_stage = bool(running and (
            _stage_title_now or m.auto_mode == proto.AUTO_MODE_AUTO))
        if cycle_stage and not getattr(self, "_auto_cycle_was_running", 0):
            idx = self.auto_mode_combo.findData(proto.AUTO_MODE_AUTO)
            if idx >= 0 and self.auto_mode_combo.currentIndex() != idx:
                self.auto_mode_combo.setCurrentIndex(idx)
        self._auto_cycle_was_running = 1 if cycle_stage else 0

        mode_names = dict(proto.AUTO_MODES)
        mode_names[proto.AUTO_MODE_AUTO] = "自动（流量循环）"
        gray = "#5A6572"
        bold = False
        if not m.run_mode_known:
            text, color = "连接后显示服务端状态", gray
        elif not m.is_auto():
            text, color = "手动模式（切到自动后可按启动）", gray
        elif m.auto_fault:
            text, color = "报警锁存：按「复位」清除后再启动", theme.CLR_FAULT
        elif running:
            phase_text = self.AUTO_PHASE_TEXT.get(m.auto_phase, "")
            mode_name = mode_names.get(m.auto_mode, m.auto_mode or "未选择")
            # [序号后缀 2026-11-27 用户「自动循环模式时各模式提示带序号
            # 后缀；单模式不带」] 服务端 autoStageTitle 只在循环里给
            # （「产水模式2」= 同 mode 第 2 个实例）；单模式缺键 ⇒ 空串
            # ⇒ 只显示模式名。老服务端缺键 ⇒ 退回模式名（不编造序号）。
            stage_title = str(getattr(m, "auto_stage_title", "") or "")
            # [循环运行标明“自动循环”] 时间（剩余/已运行）只在循环里有
            # 意义；状态行不写清楚，操作员会把“选中：单模式”与倒计时看成
            # 一一对应。
            head = ("自动循环（%s）" % (stage_title or mode_name)
                    if cycle_stage else mode_name)
            text = "运行中 · %s%s" % (head,
                                      (" · " + phase_text) if phase_text
                                      else "")
            # [循环进度 2026-11-27] 组遍数 + 总轮次接在行尾（缺键 ⇒ 无后缀，
            # 老服务端不会多出一截"第 0/0 遍"）。
            _prog = self._auto_progress_suffix(m)
            if _prog:
                text += " · " + _prog
            color, bold = theme.CLR_WARN, True
        elif m.auto_phase == "paused":
            # [暂停态 2026-11-19] 停止＝暂停 ⇒ 服务端记住模式/阶段/计时，
            # 按启动从暂停处续跑；复位（屏幕/实体）才清零。用警示色让
            # 操作员一眼看出"现在是暂停，不是从头待命"。
            resume_name = (str(getattr(m, "auto_stage_title", "") or "")
                           or mode_names.get(m.auto_mode, m.auto_mode or ""))
            # [循环进度 2026-11-27] 暂停时轮次是"停在第几轮"（停止=暂停
            # 保留轮次，再启动续跑不清零）；接在「已暂停 · 」之后，核心
            # 句子「按启动从…继续」保持原样（既有文案结构不变）。
            _prog = self._auto_progress_suffix(m)
            text = ("已暂停 · %s按启动从「%s」继续（按复位则从头）"
                    % ((_prog + " · ") if _prog else "", resume_name))
            color = theme.CLR_WARN
        elif m.auto_phase == "estop":
            text, color = "急停锁存：按「复位」后再启动", theme.CLR_FAULT
        else:
            sel_name = mode_names.get(self._selected_auto_mode(), "")
            # [组合模式：显示"将从哪一步继续" 2026-11-12] 服务端停止后
            # autoMode 仍是**记住的子阶段**（复位才清空）⇒ 这里如实显示，
            # 让操作员知道再按启动会从哪一步继续，而不是以为从头再来。
            if (self._selected_auto_mode() == proto.AUTO_MODE_AUTO
                    and m.auto_mode):
                resume_name = (str(getattr(m, "auto_stage_title", "") or "")
                               or mode_names.get(m.auto_mode, m.auto_mode))
                text = ("已停止 · 待启动（将从%s继续；按复位则从头）"
                        % resume_name)
                # [循环进度 2026-11-27] 待启动行也带轮次（「第 N/M 轮」），
                # 操作员能看出再按启动是接着第几轮；缺键 ⇒ 无后缀。
                _prog = self._auto_progress_suffix(m)
                if _prog:
                    text += " · " + _prog
            else:
                text = ("已停止 · 待启动（选中：%s）" % sel_name
                        if sel_name else "已停止")
            color = gray
        if self._auto_hint.text() != text:
            self._auto_hint.setText(text)
        style = ("QLabel { color: %s; %s background: transparent; }"
                 % (color, "font-weight: bold;" if bold else ""))
        if self._auto_hint.styleSheet() != style:
            self._auto_hint.setStyleSheet(style)
        # ---- 累计自动运行时长（服务端计；只在自动运行期间累加）----
        # [缺键不编造] 老服务端不报 autoRunMs ⇒ model 保持 0 ⇒ 显示
        # "自动运行 --"（而不是"0天"，那会把"没数据"说成"没跑过"）。
        # [收到过数据就照常显示，即使 0 2026-11-12 用户口径「就算没运行也
        # 应该显示累计运行时间」] auto_run_ms_known=没收到过（老服务端/未
        # 连接）⇒ 显示「--」；收到过 ⇒ 显示格式化时长（0 就显示 0天）。
        run_text = (self._fmt_auto_run(m.auto_run_ms)
                    if m.auto_run_ms_known else "--")
        # [按标签实际字体复核定宽] QSS polish 后字体才最终确定（见
        # _auto_run_apply_width 注释）；宽度只在校准时变一次 ⇒ 不抖。
        self._auto_run_apply_width()
        if self._auto_run_label.text() != run_text:
            self._auto_run_label.setText(run_text)
        # ---- 当前阶段剩余时间（只在自动循环运行时显示的表盘）----
        self._refresh_auto_dial(m)
        # ---- 阶段信息行（阶段名 + 已用/总 + 进度条）----
        self._refresh_phase_row(m)

    # ---- 最近报警卡（2026-11-19 主页新增）----

    def _refresh_alarm_card(self, m):
        """最近报警卡：一行摘要（时间 + 类型）+ 原文。

        [数据来源] 服务端状态快照里的 lastAlarm（proto.KEY_LAST_ALARM）；
        客户端 model.last_alarm（bridge._apply_mode_doc 落的）。

        [老服务端兼容] 没有该键 ⇒ last_alarm 保持 None ⇒ 显示「无报警」
        （**不报错、不崩**；与 autoRunMs 的缺键口径同款）。

        [类型口径] 「类型」文案由 model.alarm_type_text 算好放在 typeText
        里（设备类别 dev 优先，其次来源类别 kind）——与报警页的「类型」列
        **同一套判据**，且网页端同款（后端算好下发）⇒ 两端逐字一致。
        """
        info = m.last_alarm
        if not info:
            meta, text = "--", "无报警"
        else:
            at_ms = int(info.get("atMs") or 0)
            stamp = (time.strftime("%H:%M:%S", time.localtime(at_ms / 1000.0))
                     if at_ms else "--")
            kind_text = (info.get("typeText")
                         or model_mod.alarm_type_text(info.get("kind"),
                                                     info.get("dev")))
            meta = "%s · %s" % (stamp, kind_text)
            text = str(info.get("text") or "")
        if self._alarm_meta.text() != meta:
            self._alarm_meta.setText(meta)
        if self._alarm_text.text() != text:
            self._alarm_text.setText(text)

    def _goto_alarm_page(self):
        """点「最近报警」卡 ⇒ 跳报警页（看全量报警历史）。

        [走既有页面跳转入口] ctx.navigate（main_window.goto_page）—— 与
        菜单页/子菜单页的跳法同款，不自己造导航逻辑。
        """
        self.ctx.navigate(reg.PAGE_DATA_ALARM)

    # ---- 阶段信息行（2026-11-19；主页「自动模式」卡里的一行）----
    #: 相位 → 阶段名（**独立于 AUTO_PHASE_TEXT**）。
    #:
    #: [为什么不复用 AUTO_PHASE_TEXT] 那张表是**状态行**的短语（"阀动作
    #: 中…"/"启动泵…"，且 running 是空串——状态行已有"运行中"）。本行是
    #: **阶段名**（名词短语，短）⇒ 两套文案各司其职，不互相改坏。
    PHASE_NAME_TEXT = {
        "idle": "待命",
        "valves": "阀相",
        "pumps": "泵相",
        "running": "运行中",
        "estop": "急停",
        # [paused 2026-11-19] 停止＝暂停 ⇒ 阶段行照常显示记住的阶段
        # 与冻结的已用时长（服务端 stage_timing 暂停分支）。
        "paused": "暂停",
    }

    def _refresh_phase_row(self, m):
        """阶段信息行：阶段名 + 本阶段已用/总 + 进度条。

        [显示条件] 自动模式 ⇒ 显示；手动模式 ⇒ **隐藏整行**（手动模式
        "第几阶段"无意义）。

        [进度条条件] total > 0（本阶段有定时）⇒ 显示进度条与"已用/共"；
        否则（单模式 / 老服务端 / 不在运行）⇒ 只显示阶段名，**不编造**
        倒计时。

        [恒占位铁律] 行内元素**不 hide/show**（除整行在手动模式下隐藏，
        那是一次性状态切换不是逐拍抖动）——进度条在 total<=0 时用
        setVisible(False) 会让整行高度跳变 ⇒ 改成**恒占位**：进度条一直
        在，只是 total<=0 时不画（透明）并把文字置"--"。
        """
        in_auto = bool(m.run_mode_known and m.is_auto())
        if self._phase_row.isVisibleTo(self) != in_auto:
            self._phase_row.setVisible(in_auto)
        if not in_auto:
            return
        phase = str(m.auto_phase or "")
        name = self.PHASE_NAME_TEXT.get(phase, "--")
        if not phase:
            name = "--"
        if self._phase_label.text() != name:
            self._phase_label.setText(name)
        elapsed = int(m.auto_stage_elapsed_ms or 0)
        total = int(m.auto_stage_total_ms or 0)
        # [总时长 <= 0 ⇒ 本阶段无定时] 只显示阶段名，不编造倒计时。
        # [进度条已删 2026-11-19 用户口径「那个进度条删掉」] 阶段行只留
        # 阶段名 + 已用时长文字；无数据 ⇒ "--"（不编造倒计时）。
        if m.auto_stage_known and elapsed >= 0:
            # [总时长 > 0 ⇒ 带上「/ 共 Y」] 有定时才知道该阶段全长；
            # 单模式/无定时 ⇒ 只报已用（不编造总长）。
            if total > 0:
                text = "已用 %s / 共 %s" % (self._fmt_phase_ms(elapsed),
                                           self._fmt_phase_ms(total))
            else:
                text = "已用 %s" % self._fmt_phase_ms(elapsed)
        elif phase == "paused":
            text = "按启动继续"
        else:
            text = "--"
        if self._phase_text.text() != text:
            self._phase_text.setText(text)

    @staticmethod
    def _fmt_phase_ms(ms):
        """毫秒 → 「M分SS秒」（阶段进度用；小时级不现实——阶段最长几十分钟）。

        [为什么不复用 _fmt_auto_run] 那是「天 时 分 秒」累计运行时长的
        格式（定宽标签用）；阶段进度要的是**分钟级**短格式 ⇒ 两套。
        """
        secs = max(0, int(ms or 0) // 1000)
        minutes, seconds = divmod(secs, 60)
        hours, minutes = divmod(minutes, 60)
        if hours:
            return "%d时%02d分%02d秒" % (hours, minutes, seconds)
        return "%d分%02d秒" % (minutes, seconds)

    # ---- 剩余时间表盘（2026-11-12 用户定案；只在自动循环运行时显示）----

    def _refresh_auto_dial(self, m):
        """表盘：倒计时/小字都在**表盘中央**；只有**自动循环**模式才显示。

        [2026-11-23 用户口径「客户端也是只在自动循环这个模式下用表盘显示
        剩余时间，其他都不用表盘了」] 可见性 = **当前模式是组合循环
        （auto_mode == "auto"）** ∧ 服务端报了阶段计时（elapsed>=0）。
        单模式（产水/反洗/顺洗/放水）即使带了定时/兜底（服务端也会报
        elapsed/total）也不显示表盘 —— 与网页端同口径（page00_home.js 的
        dialShow 同时判 autoRunning 与 autoMode==="auto"）。
        [为什么不只看阶段计时] 单模式带定时时 elapsed>=0 也成立（旧实现
        会显示倒计时）；用户口径是"按模式"而不是"按有没有定时"。
        [旁文已删] 「表盘中间已经有倒计时了，旁边就不用其他倒计时了」
        （用户 2026-11-12）；模式/总时长看状态行与下拉，表盘只管倒计时。
        """
        elapsed = int(m.auto_stage_elapsed_ms)
        # [判据订正 2026-11-27] 运行中 autoMode 是子阶段（produce…）不是
        # "auto"（同状态行 cycle_stage 的实锅）⇒ 用 autoStageTitle 非空判
        # 循环，老服务端缺键退回 autoMode=="auto" 兜底。
        _is_cycle = bool(str(getattr(m, "auto_stage_title", "") or "")
                         or str(getattr(m, "auto_mode", "") or "") == "auto")
        show = bool(_is_cycle and m.auto_stage_known and elapsed >= 0)
        if self._auto_dial.isVisibleTo(self) != show:
            self._auto_dial.setVisible(show)
        if not show:
            self._auto_dial.set_state(-1, 0)
            return
        self._auto_dial.set_state(elapsed, int(m.auto_stage_total_ms or 0))

    # ---- 第二排看板卡（压差/流量计/电磁阀）----

    @staticmethod
    def _press_note_label():
        """压力卡共享说明行：**单行滚动文字**（2026-11-24 用户口径「可以把
        黄色提示做成滚动文字」）。

        [为什么不用 QLabel] 三路同时越限时文案很长（「压差：…；压力1：…；
        压力2：…」），QLabel 的 minimumSizeHint = 整段文字宽 ⇒ 会把压力
        卡最小宽顶到 500~700px，整排超宽、主界面出现横向滚动条。滚动标签
        的宽度不参与布局最小宽，只在可见宽度内循环滚动。
        """
        label = MarqueeLabel()
        # [不折行] 三分栏每栏很窄，折行会让"有 ew/无 ew"两种状态行数不同
        # ⇒ 卡片高度跳（本卡铁律：几何恒定）。超长说明由 QLabel 裁切。
        label.setWordWrap(False)
        label.setStyleSheet(theme.caption_label_qss())
        return label

    @staticmethod
    def _set_note_hold(label, text, color):
        """三栏说明行：**恒占位**（空文本也不 setVisible(False)）。

        [为什么不用 _set_note] `_set_note` 空文本会隐藏标签（那条口径是
        给"事件驱动、并非逐拍翻转"的说明行用的）；压差卡三栏说明行若在
        「有 ew / 无 ew」之间显隐，卡片高度会跟着跳（首页三栏并排，一眼
        就能看见）。⇒ 本卡专用：只换文本/颜色，标签始终可见。
        """
        key = (text, color)
        if getattr(label, "_hsf_note_key", None) == key:
            return
        label._hsf_note_key = key
        label.setText(text)
        # [2026-11-23 用户「下面是不是空了整整一行」] 无文案时把这一行
        # **收起**（高度 0 + 隐藏）：看板卡已按内容自然高，再恒占一行就是
        # 一条白边空行。有文案才占一行（越限/断线是事件级，不是逐拍状态）。
        label.ensurePolished()
        label.setVisible(bool(text))
        label.setFixedHeight((label.fontMetrics().height() + 2)
                             if text else 0)
        # [tooltip 兜全文] 三分栏每栏 ~100px，长说明（"低于 4.0mA（负压）"）
        # 在单行里会被裁 ⇒ 悬停能看全（与全局「文字被裁但 tooltip 可达」
        # 的放行口径一致）。空文本清掉 tooltip，不留上一条的残影。
        label.setToolTip(text)
        label.setStyleSheet(
            "QLabel { color: %s; font-size: 18px; background: transparent; }"
            % color)

    @staticmethod
    def _analog_channel_by_name(m, name, fallback=None):
        """按**服务端下发的通道名**找模拟量通道号（找不到返回 fallback）。

        [为什么不写死通道号] 远端 collect 节可把压差计/压力1/压力2 改到
        任意通道；服务端把名字挂到新通道上 ⇒ 客户端跟着名字走，接线一改
        只改服务端配置。名字缺失（老服务端/通道断线只发 ew）时：
        压差栏兜底 CH0（老口径），压力1/2 没有历史口径 ⇒ None（显示 --）。
        """
        for ch, nm in (m.analog.eng_name or {}).items():
            if str(nm) == name:
                return int(ch)
        return fallback

    @staticmethod
    def _is_low_range_note(note):
        """欠量程/负压类量程说明（主页压差卡不显示这类提示）。

        [2026-11-27 用户「这个负压提示不需要」] 只过滤「低于 x mA」这一类
        （负压线性延续 / 欠量程钳下限）；「断线」「高于 x mA（超量程）」仍
        要提示——那是真故障信号（数值也变 --/夹上限）。
        口径与网页端 page00_home.js 的 isLowRangeNote 逐条一致。
        """
        text = str(note or "")
        return (text.startswith("低于") or "负压" in text
                or "欠量程" in text)

    def _refresh_dp(self, m):
        """压差卡三栏：压差 / 压力1 / 压力2（2026-11-23 用户定案）。

        [口径沿用 2026-11-18「小于 4mA 是负压」] 服务端对越限/负压给
        **线性值 + note** ⇒ **note 是说明，不是"值无效"** ⇒ 三栏都照显示
        数值，note 写进各自说明行并用警示色。
        [通道] 三栏都**按名字找通道**（en{i} → "压差计"/"进水压力"/"出水压力"）；
        压差栏兜底 CH0。名字缺 ⇒ 该栏 "--"（不拿别的通道值冒充）。
        [不新鲜] `m.analog.is_fresh()` 不过 ⇒ 三栏一起 "--"（沿用旧闸，
        不拿旧值冒充实时）。
        """
        if not m.analog.is_fresh():
            for block, _name, _fallback, _label in self._dp_columns:
                block.set_value("--", "")
            self._set_note_hold(self.dp_note, "无数据", theme.CLR_OFFLINE)
            return
        notes = []
        for block, name, fallback, label in self._dp_columns:
            ch = self._analog_channel_by_name(m, name, fallback)
            if ch is None:
                block.set_value("--", "")
                continue
            value = m.analog.eng_value(ch)
            # [显示渐变 2026-11-24 用户「压力也支持下渐变」] 量程由服务端
            # 随帧下发（lo{i}/hi{i}，与模拟量量程配置同源）⇒ 三栏各按自己的
            # 量程上色；缺范围键（老服务端）保持原来的单色。
            _rng = m.analog.eng_range(ch)
            _grad = (theme.gradient_color(value, _rng[0], _rng[1])
                     if (_rng is not None and value is not None
                         and m.analog.eng_gradient(ch)) else None)
            # [单位] 随服务端 eu{i}；未下发时按本卡口径回落 MPa（同行显示）。
            block.set_value("--" if value is None else "%.3f" % value,
                            m.analog.eng_unit.get(ch, "") or "MPa",
                            _grad)
            note_text = m.analog.eng_note.get(ch, "")
            # [负压提示不占主页 2026-11-27 用户「这个负压提示不需要」] 数值
            # 照常显示（负数是真值，4mA 以下线性延续）；「低于 x mA（负压/
            # 线性延续/欠量程）」这类量程说明不再占卡底一行。断线/超量程
            # 仍是故障信号 ⇒ 照旧提示。与网页端 page00_home.isLowRangeNote
            # 同口径。
            if note_text and not self._is_low_range_note(note_text):
                # [共享说明行 2026-11-23] 前缀写清是哪一路（三路都异常时用
                # 「；」连成一行；超长由标签裁切 + tooltip 看全）。
                notes.append("%s：%s" % (label, note_text))
        if notes:
            self._set_note_hold(self.dp_note, "；".join(notes),
                                theme.CLR_WARN)
        else:
            self._set_note_hold(self.dp_note, "", "#5A6572")

    # ---- 看板卡片排序（长按拖拽 + 本地持久化，2026-11-23 用户口径）----

    def _load_board_order(self):
        """读本机保存的看板顺序；**非法值回默认**（fail-closed）。

        [为什么要严格校验] INI 是人可编辑的：少一项/多一项/拼错 id 都会让
        某张卡在重排时消失（takeAt 后没人把它放回去）⇒ 现场看到"少了一张
        卡"却毫无报错。⇒ 必须恰好是 9 张卡的排列，否则整份丢弃。
        """
        from . import settings as ui_settings
        try:
            raw = ui_settings.get_str(self.BOARD_ORDER_KEY, "") or ""
        except Exception:                            # noqa: BLE001
            raw = ""
        order = [p.strip() for p in raw.split(",") if p.strip()]
        if (len(order) == len(self.BOARD_DEFAULT_ORDER)
                and sorted(order) == sorted(self.BOARD_DEFAULT_ORDER)):
            return order
        return list(self.BOARD_DEFAULT_ORDER)

    def _persist_board_order(self):
        from . import settings as ui_settings
        try:
            ui_settings.set_value(self.BOARD_ORDER_KEY,
                                  ",".join(self._board_order))
        except Exception:                            # noqa: BLE001
            pass

    def _apply_board_order(self, order):
        """按 order（行优先，3 张一排）重铺三排看板。"""
        self._board_order = list(order)
        for lay in self._board_rows:
            while lay.count():
                lay.takeAt(0)                        # 只摘不删（widget 仍归自
                                                     # 己，下面重新插入）
        for i, cid in enumerate(self._board_order):
            card = self._board_cards.get(cid)
            if card is None:
                continue
            # [2026-11-23 用户「同一行的卡片向下拉伸到一样高，这样高高低低
            # 的不好看」] 卡片**随整排等高拉伸**（不做 NaturalHeight）。
            # 卡内空白由"多余高度沉底 + 数值自动缩放"控制，而不是让卡矮一截。
            self._board_rows[i // 3].insertWidget(i % 3, card, 1)
        try:
            self.updateGeometry()
        except Exception:                            # noqa: BLE001
            pass
        self._schedule_board_autofit()

    # ---- 数值自动缩放（填满等高卡片，减少空白）----

    def _schedule_board_autofit(self):
        if not getattr(self, "_board_base_sizes", None):
            return
        if getattr(self, "_autofit_pending", False):
            return
        self._autofit_pending = True
        # 下一拍（布局定型后）再量 —— 尺寸刚变时 sizeHint 还是旧值。
        QTimer.singleShot(0, self._autofit_board_values)

    @staticmethod
    def _apply_block_size(block, size):
        color = getattr(block, "_color", None)
        block.set_value_colored(block.plain_text(), color, int(size))

    @staticmethod
    def _block_width_cap(entries):
        """按数值标签**当前分到的宽度**算最大放大倍数（防放大后裁字）。

        [为什么用标签宽而不是 card.sizeHint().width()] 两列并排的浊度仪
        卡放大后，Qt 会把 Card 的 `sizeHint` 缓存到**下一个 LayoutRequest
        被处理之前**（实测放大到 55px 时 `card.sizeHint()` 仍报放大前的
        330，而数值标签已经 171 > 155 被裁）⇒ 只看 Card 会漏判。数值标签
        的 `sizeHint` 与所在块分到的宽度**当场就能比**，且与单位字号固定
        的口径一致（单位不随数值放大，线性折算偏保守）。
        """
        cap = 2.0
        for block, _base in entries:
            val = getattr(block, "_value", None)
            if val is None:
                continue
            try:
                need = val.sizeHint().width()
                avail = block.width()
            except RuntimeError:               # 块已销毁（切页/退出竞态）
                continue
            if need <= 0 or avail <= 0:
                continue
            # 留 1px 余量（富文本实测宽与布局分配的亚像素取整会差 1px）。
            cap = min(cap, float(avail - 1) / float(need))
        return cap

    @staticmethod
    def _blocks_value_clipped(entries, widths):
        """放大后数值标签的 `sizeHint` 是否已超出**放大前**分到的块宽。

        [为什么要拿放大前的块宽] `set_value_colored` + `layout().activate()`
        之后，RichLabel 的实际宽要等下一次 LayoutRequest 才更新（仍停在上
        一拍）⇒ 直接比 `label.width()` 会把"本来放得下"的放大误判成裁字。
        块宽（cell 分配）在本轮放大中不变，是稳定基准。
        """
        for block, _base in entries:
            val = getattr(block, "_value", None)
            bw = widths.get(id(block), 0)
            if val is None or bw <= 0:
                continue
            try:
                if val.sizeHint().width() > bw + 1:
                    return True
            except RuntimeError:
                continue
        return False

    def _autofit_board_values(self):
        """按每张卡的**剩余高度**放大数值字号（上限 1.3×）。

        [为什么要做] 同一排卡片等高拉伸后，短卡底部必然留白；把数值自动
        放大正好把留白吃掉（用户「文字自动缩放下，减少空白」）。
        [为什么不会布局自激] 只在本页 resize / 卡片重排后跑一次；放大后若
        `sizeHint` 超过卡片当前高度就**整卡回退基准**（不会把行高顶大 ⇒
        不会触发新的 resize 循环）。上限 1.3 也保证缩放幅度可控。
        [宽度 2026-11-24] 浊度仪基准 42 + 单位 22 后，按余高直接放到 55
        会把两列"3.52 NTU"顶出列宽（板端 1280x800@1.5 实测卡 345px、
        每列 155px、数值标签 55px 时要 171px）⇒ 增加两道宽度守卫：
          ① 先按"标签当前宽 / 标签 sizeHint 宽"折算**放大上限**（够放才
             放，能放到 44 就不放到 55）；
          ② 放完再按**放大前的块宽**复核一次，超了整卡回退基准。
        两道都只**收紧**放大，不会削弱其它卡的正常放大。
        """
        self._autofit_pending = False
        base_sizes = getattr(self, "_board_base_sizes", None)
        if not base_sizes:
            return
        for cid, entries in base_sizes.items():
            card = self._board_cards.get(cid)
            if card is None:
                continue
            try:
                if not card.isVisibleTo(self):
                    continue
            except RuntimeError:
                continue
            # 先恢复基准（每次从头量，避免"缩放→再缩放"累积）
            for block, base in entries:
                if int(getattr(block, "_value_size", base)) != base:
                    self._apply_block_size(block, base)
            lay = card.layout()
            if lay is not None:
                lay.activate()
            avail = card.height()
            need = card.sizeHint().height()
            spare = avail - need
            if spare < 16 or need <= 0:
                continue
            # [宽度守卫] 已有内容宽度就贴着卡宽 ⇒ 放大必裁字（两列并排的
            # 浊度仪大数字尤其如此）⇒ 不放。
            if card.sizeHint().width() > card.width() - 2:
                continue
            factor = min(1.30, 1.0 + (float(spare) / float(need)) * 0.9)
            # [宽度上限] 标签当前分到的宽 ÷ 标签基准 sizeHint 宽 = 还能
            # 放大多少倍（<1 说明基准已经贴边 ⇒ 不放大）。
            factor = min(factor, self._block_width_cap(entries))
            if factor <= 1.03:
                continue
            # 放大前分到的块宽（复核"放完有没有超出可用宽"的稳定基准）。
            widths = {}
            for block, _base in entries:
                try:
                    widths[id(block)] = block.width()
                except RuntimeError:
                    widths[id(block)] = 0
            for block, base in entries:
                self._apply_block_size(
                    block, max(1, int(round(base * factor))))
            if lay is not None:
                lay.activate()
            if (card.sizeHint().height() > avail
                    or card.sizeHint().width() > card.width() + 1
                    or self._blocks_value_clipped(entries, widths)):
                # 放大把卡撑高/撑宽/裁字了 ⇒ 整卡回退（宁可留白也不要裁字
                # 或顶大行高）。
                for block, base in entries:
                    self._apply_block_size(block, base)
                if lay is not None:
                    lay.activate()

    def _on_card_swap(self, a_id, b_id):
        """拖拽交换两张卡的位置 ⇒ 重排 + 落盘（本机）。"""
        order = list(self._board_order)
        try:
            ia = order.index(a_id)
            ib = order.index(b_id)
        except ValueError:
            return
        order[ia], order[ib] = order[ib], order[ia]
        self._apply_board_order(order)
        self._persist_board_order()

    def resizeEvent(self, event):
        Page.resizeEvent(self, event)
        self._schedule_board_autofit()

    def showEvent(self, event):
        Page.showEvent(self, event)
        self._schedule_board_autofit()

    def _card_click_ok(self):
        """拖拽刚结束时吞掉卡片点击（卡片=点开详情/跳页的按钮）。"""
        drag = getattr(self, "_board_drag", None)
        return drag is None or not drag.just_dragged()

    def _card_clicked(self, mode):
        # [开悬浮详情前收拖拽 2026-11-24] 卡片按下即弹详情 ⇒ 长按计时器若
        # 还在计时，会在遮罩后面起拖拽（高亮框卡住）。先取消。
        drag = getattr(self, "_board_drag", None)
        if drag is not None:
            drag.cancel()
        if self._card_click_ok():
            self._toggle_detail(mode)

    def _alarm_card_clicked(self):
        # [先收拖拽状态再跳页 2026-11-24] 卡片是**按下即触发**：按下时拖拽
        # 控制器已起了 600ms 长按计时器，跳页后首页隐藏、收不到松手事件 ⇒
        # 计时器到点仍会起拖拽（原实现会弹出顶层快照窗口且永不关闭，即用户
        # 实报的「最近报警卡片悬浮窗」）。跳页前显式取消。
        drag = getattr(self, "_board_drag", None)
        if drag is not None:
            drag.cancel()
        if self._card_click_ok():
            self._goto_alarm_page()

    def on_leave(self):
        """离页：取消进行中的卡片拖拽（页面隐藏后收不到松手/触摸结束）。"""
        drag = getattr(self, "_board_drag", None)
        if drag is not None:
            drag.cancel()

    def _refresh_fm(self, m):
        """流量计卡：瞬时流量 + 正/反向累计（B 侧优先，A 侧兜底；对齐 C 流量计段）。

        单位随服务端单位码下发，不写死 m³/h（与历史页同一口径）。

        [按侧择优] 服务端按两条侧轮流推同一主题；没装流量计的那一侧推的是
        valid=false 的空帧 ⇒ 只留最后一帧时卡片恒"无数据"。口径对齐 C 端
        hsfHomePickFlowMeter（优先 B 侧）。
        """
        fm = m.flowmeter.pick()
        if fm is None:
            self.fm_flow.set_value("--")
            self.fm_flow.set_suffix("")
            self.fm_vel.set_value("--")
            self._set_note(self.fm_sub, "无数据", theme.CLR_OFFLINE)
            return
        # [负零 2026-11-25] 流量计零点可能报 -0.0/极小负值 ⇒ 两位小数显示
        # 会变成 "-0.00"（用户实报滤速；顺带把瞬时流量也收口）。
        _flow_show = 0.0 if abs(float(fm.flow)) < 0.005 else float(fm.flow)
        self.fm_flow.set_value("%.2f" % _flow_show)
        self.fm_flow.set_suffix(fm.flow_unit_text())
        # [滤速 2026-11-25] 服务端算好随帧下发；未配面积/离线 ⇒ None ⇒ "--"
        # （不显示 0——"没数据"与"流量 0"要分开）。
        # [负号 2026-11-25 用户实报「显示 -0.00」] 零点微小负值/反向流量
        # 不算过滤方向的滤速 ⇒ 负值一律显示 0.00（不出现 "-0.00"）。
        _vel = fm.filter_velocity_mh
        if _vel is not None:
            _vel = max(0.0, float(_vel))
        self.fm_vel.set_value("--" if _vel is None else "%.2f" % _vel)
        totals = ("正累计 %.2f %s / 反累计 %.2f %s"
                  % (fm.forward_total, fm.total_unit_text(),
                     fm.reverse_total, fm.total_unit_text()))
        # [报警并进同一行 2026-11-12] 报警码直接说中文（说明书口径：
        # 空管/上限/下限/系统），不显示 "0x0004"；用红色 span 保持醒目，
        # 无报警时**不占额外内容**（一行只有累计）。
        if fm.alarms:
            text = ("%s　·　<span style='color:%s'>报警: %s</span>"
                    % (totals, theme.CLR_FAULT,
                       model_mod.flow_alarm_text(fm.alarms)))
            color = "#5A6572"
        else:
            text = totals
            color = "#5A6572"
        self._set_note(self.fm_sub, text, color)

    def _refresh_valves(self, m):
        """电磁阀卡：六路「● 状态」色点（色义对齐 pages_valve 卡片，
        与 C hsfHomeValveStateInfo 同源：已开绿/已关灰蓝/动作中橙/
        故障红/未配置·未知浅灰）。"""
        fresh = m.valve.is_fresh()
        for i in range(self.VALVE_COUNT):
            item = m.valve.items.get(i) if fresh else None
            if item:
                # [缺省名 2026-11-23 用户口径逐台工艺名] 服务端下发 name
                # 优先；没下发时按 `VALVE_NAMES`（产水阀(1)…）兜底，不再
                # 用「电动球阀%d」——那是旧的统称，现场按工艺叫阀。
                name = str(item.get("name")
                           or self.VALVE_NAMES[i % len(self.VALVE_NAMES)])
                if item.get("ready"):
                    text, color = VALVE_STATE_TEXT.get(
                        int(item.get("state", 0)),
                        ("未知", self.VALVE_NA_COLOR))
                else:
                    # 未配置不是"未知"：点位没接线要说清（pages_valve 同口径）。
                    text, color = "未配置", self.VALVE_NA_COLOR
            else:
                name, text, color = "-", "--", self.VALVE_NA_COLOR
            key = (text, color, name)
            if self._valve_render[i] == key:
                continue               # 同值早退：稳态零重排
            self._valve_render[i] = key
            self._valve_state[i].setText("● " + text)
            self._valve_state[i].setStyleSheet(
                "QLabel { color: %s; font-size: 18px;"
                " background: transparent; }" % color)
            self._valve_name[i].setText(name)

    # ---- 浊度仪卡 / 按钮组卡（2026-11-09 首页新增）----

    def _clamp_turb_size(self, block):
        """数值文本变长后，把**自动放大**的字号收回到列宽内。

        [为什么需要] `_refresh_turb` 不再把字号写死（否则自动缩放白做），
        但自动缩放只在 resize/show 跑；数据从 "3.52" 变成 "12.31" 后沿用
        放大字号会裁字。这里按"当前分到的块宽 / 新文本 sizeHint"把字号
        等比收回，**不低于基准 42**（再小就违背"数字放大"的口径，宽度不
        够是另一回事）。
        """
        size = int(getattr(block, "_value_size", self.TURB_VALUE_SIZE))
        if size <= self.TURB_VALUE_SIZE:
            return
        try:
            avail = block.width()
            need = block._value.sizeHint().width()
        except RuntimeError:                   # 块已销毁（切页/退出竞态）
            return
        if avail <= 0 or need <= 0 or need <= avail - 1:
            return
        new_size = max(self.TURB_VALUE_SIZE,
                       int(size * (float(avail - 1) / float(need))))
        if new_size < size:
            color = getattr(block, "_color", None)
            block.set_value_colored(block.plain_text(), color, new_size)

    def _refresh_turb(self, m):
        """浊度仪卡：两台 NTU 读数（口径对齐 pages_monitor.WaterPage）。

        [超阈值标红只是显示口径] 阈值在客户端（各端各算），不是报警判据；
        真要进报警列表必须挪到服务端。

        [按侧择优] 服务端按两条侧轮流推同一主题；没装浊度仪的那一侧推的
        是「两台都 online=false」的帧 ⇒ 只留最后一帧时卡片恒 "--"。两台
        浊度仪在同一侧，故按侧取那一侧即可（不必按台分侧）。

        [离线台] TurbiditySideState.ntu_of() 对 online=False 的台返回 None
        （model.py 的口径），故这里 None ⇒ 该台显示 "--"，其余台照常。
        """
        st = m.turbidity.pick()
        for idx, block in enumerate(self._turb_blocks):
            name = ""
            ntu = None
            temp = None
            if st is not None:
                name = st.name_of(idx)
                ntu = st.ntu_of(idx)
                temp = st.temp_of(idx)
            if ntu is None:
                text, color = "--", self.VALVE_NA_COLOR
            else:
                text = "%.2f" % ntu
                # [显示渐变 2026-11-24 用户「两个浊度仪范围不一样，远端可配；
                #  蓝→绿→红」] 服务端随帧下发逐台 rangeMin/Max ⇒ 按范围上色；
                # 缺键（老服务端）退回旧的「到 20 NTU 标红」口径。
                _rng = st.range_of(idx) if st is not None else None
                _grad = (theme.gradient_color(ntu, _rng[0], _rng[1])
                         if (_rng is not None and st is not None
                             and st.gradient_of(idx)) else None)
                color = _grad or (
                    theme.CLR_FAULT if ntu >= self.TURBIDITY_WARN_NTU
                    else theme.text_color())
            # 水温文案（缺值 = 该机型无温度 ⇒ 不显示）
            temp_text = "" if temp is None else "水温 %.2f ℃" % temp
            key = (text, color, name, temp_text)
            if self._turb_render[idx] == key:
                continue                   # 同值早退：稳态零重排
            self._turb_render[idx] = key
            # 仪器名由服务端给（客户端不猜"原水/出水"）。
            block.set_caption(name or "浊度仪%d" % (idx + 1))
            # [不传字号 2026-11-24] 走 set_value ⇒ 保留构造基准（42）与
            # 自动缩放后的字号；旧代码写死 26 会在每帧数据到达时把 42 打回
            # 26（自动缩放只在 resize/show 跑，平日刷新看不到它补救）。
            # 单位字号由 StatBlock 内部的 unit_size 固定，不受这里影响。
            block.set_value(text, color)
            # 数值文本变长（"3.52" → "12.31"）时，自动放大的字号可能已经
            # 超出列宽 ⇒ 收到放得下的字号（不低于基准 42）。
            self._clamp_turb_size(block)
            self._turb_temps[idx].setText(temp_text)
            self._turb_temps[idx].setVisible(bool(temp_text))
        if st is None:
            self._turb_note_text = "未收到浊度仪数据（服务端未装配 / 从站失联）"
        elif st.count == 0:
            self._turb_note_text = "服务端未装配浊度仪"
        else:
            self._turb_note_text = ""

    def _pump_do_state(self, m):
        """数字量里的进水泵 DO 回显 → (通/断|None, 该帧的 updated_ms)。

        [为什么可以拿 DO 兑底] 泵状态本来就是“输出点”（现场无反馈），
        与数字量页 DO6 是**同一个事实**；谁的那一帧更新就用谁 —— 泵主题
        漏推/停推时，数字量帧仍可回答通/断，界面不会钉在旧值上。
        """
        best_on, best_ms = None, 0
        # [快照迭代 2026-11-16 审计] digital 由网络线程插入新键（首帧
        # 到达）⇒ 遍历中变更会炸 RuntimeError（"dictionary changed size
        # during iteration"，被 _refresh_current 兜成"刷新失败"刷屏）。
        for idx in sorted(list(m.digital)):
            dev = m.digital[idx]
            if not dev.is_fresh():
                continue
            # `do` 是**位图**（不是数组）：bit n = 通道 n。
            for ch, name in getattr(dev, "do_names", {}).items():
                if "进水泵" in str(name) and 0 <= ch < 32:
                    if dev.updated_ms >= best_ms:
                        best_on = bool((int(dev.do) >> ch) & 1)
                        best_ms = dev.updated_ms
        return best_on, best_ms

    def _refresh_turb_pump(self, m):
        """进水泵状态（并进浊度仪卡的说明行；2026-11-12）。

        [口径：输出点，不是实测] 现场进水泵**只有输出没有反馈** ⇒ `on`
        只表示"DO6 是开的"，不是"泵真的在转"。故状态字用「通/断」（与
        数字量页 DO 同口径），小字固定点明"输出点（无反馈）"。

        [与浊度仪说明共用一行] 见构造处注释：说明行有话时优先显示说明，
        泵状态仍跟在后面（用 · 分隔），两者都不为空。

        [富文本] 泵状态要**绿色**（通）与灰色（断）区分 ⇒ 用 span 上色
        （QLabel 的 QSS 颜色只作用于非富文本部分）。
        """
        pump = m.pump
        # [数字量 DO 回显：同一事实的“更新鲜”副本]
        # 用户实锅：泵实际已开、卡片一直「断」，切页回来才对（切页重订泵
        # 主题 ⇒ 服务端补发一帧）。泵主题漏推/停推时不能让界面钉在旧值；
        # 而数字量帧带 DO6 回显，且时间戳可直接比新旧。
        fb_on, fb_ms = self._pump_do_state(m)
        use_fb = (fb_on is not None
                  and (not pump.is_fresh()
                       or fb_ms > int(pump.updated_ms or 0)))
        if use_fb:
            mark, color = (("通", theme.CLR_OK) if fb_on else
                           ("断", theme.CLR_OFFLINE))
            note = "输出点（无反馈）"
        elif not pump.is_fresh():
            mark, color, note = "--", self.VALVE_NA_COLOR, "未收到泵状态"
        elif not pump.count:
            mark, color, note = "--", self.VALVE_NA_COLOR, "服务端未装配进水泵"
        elif not pump.is_ready(0):
            mark, color, note = "--", theme.CLR_OFFLINE, "未配置点位"
        elif pump.is_on(0):
            mark, color, note = "通", theme.CLR_OK, "输出点（无反馈）"
        else:
            mark, color, note = "断", theme.CLR_OFFLINE, "输出点（无反馈）"
        err = pump.text_of(0) if pump.count else ""
        if err:
            note = err
            color = theme.CLR_FAULT
        key = (mark, color, note, self._turb_note_text)
        if self._pump_render == key:
            return
        self._pump_render = key
        pump_html = ("进水泵 <span style='color:%s'>● %s</span> %s"
                     % (color, mark, note))
        parts = [p for p in (self._turb_note_text, pump_html) if p]
        self._turb_note.setText("　·　".join(parts))
        self._turb_note.setVisible(True)
        self._turb_note.setStyleSheet(
            "QLabel { color: %s; font-size: 18px; background: transparent; }"
            % theme.CLR_OFFLINE)

    def _find_button_channels(self, m):
        """在数字量模块里按**服务端下发的通道名**找按钮组通道。

        :returns: (dev, ch_map) —— dev 是数字量模块（DigitalState 或 None，
                  None = 一个新鲜模块都没有）；ch_map = {slot: (ch, is_do)}，
                  没找到的 slot 不在表里。

        [为什么不写死通道号] 现场接线按图纸走，Python 服务端的点位走
        `buttons.items` 配置可改；客户端写死 12~15/7~9 就是"接线一改、
        界面全错"。服务端下发的语义名两个服务端逐字一致，按名字匹配即
        两端同口径。

        [模块也按名字找] 按钮组可能在数字量模块表里的任意一个（服务端
        `button_items.module` 默认 0，但可配）⇒ 先扫出**含按钮语义名**的
        那个模块，再在它里面找其余通道。

        [有模块但没有按钮通道] 返回该模块 + 空映射 ⇒ 界面显示「未配置」
        （与"模块离线"是两回事，别混）。
        """
        fresh = [m.digital[i] for i in sorted(m.digital)
                 if m.digital[i].is_fresh()]
        if not fresh:
            return None, {}
        dev = None
        for cand in fresh:
            names = (list(cand.di_names.values())
                     + list(cand.do_names.values()))
            if any(self.BTN_ESTOP_NAME in n for n in names) or \
                    any(k in n for k in self.BTN_LAMP_NAMES for n in names):
                dev = cand
                break
        if dev is None:
            return fresh[0], {}
        ch_map = {}
        for slot, key in enumerate((self.BTN_ESTOP_NAME, self.BTN_START_NAME,
                                    self.BTN_STOP_NAME, self.BTN_RESET_NAME)):
            for ch, name in dev.di_names.items():
                if name.startswith(key):
                    ch_map[slot] = (ch, False)
                    break
        for slot, key in enumerate(self.BTN_LAMP_NAMES, start=4):
            for ch, name in dev.do_names.items():
                if name.startswith(key):
                    ch_map[slot] = (ch, True)
                    break
        return dev, ch_map

    def _refresh_buttons(self, m):
        """按钮组卡：4 触点 + 3 灯（数据源 = 数字量模块的 DI/DO 位图）。

        [触点极性] 急停/停止 = 常闭（1=未按/回路通）、启动/复位 = 常开
        （1=按下），与服务端 `server_py/hsfsrv/buttons.py` 的现场定义一致。
        急停的"按下"不是"按下"而是"急停生效"⇒ 显示红色「急停」。
        """
        dev, ch_map = self._find_button_channels(m)
        if dev is None or not dev.is_fresh():
            for i in range(self.BTN_SLOT_COUNT):
                key = ("--", self.VALVE_NA_COLOR)
                if self._btn_render[i] == key:
                    continue
                self._btn_render[i] = key
                self._btn_state[i].setText("● --")
                self._btn_state[i].setStyleSheet(
                    "QLabel { color: %s; font-size: 18px;"
                    " background: transparent; }" % self.VALVE_NA_COLOR)
                self._btn_name[i].setText(self.BTN_SLOT_LABELS[i])
            self._set_note(self._btn_note,
                           "未收到按钮组数据（数字量模块离线）",
                           theme.CLR_OFFLINE)
            return

        for slot in range(self.BTN_SLOT_COUNT):
            text, color = "--", self.VALVE_NA_COLOR
            hit = ch_map.get(slot)
            if hit is not None:
                ch, is_do = hit
                if is_do:
                    # 灯：亮/灭（亮=绿，灭=灰）
                    if dev.do_bit(ch):
                        text, color = "亮", theme.CLR_OK
                    else:
                        text, color = "灭", self.VALVE_NA_COLOR
                else:
                    level = dev.di_bit(ch)
                    pressed = (not level) if self.BTN_NORMALLY_CLOSED[slot] \
                        else level
                    if slot == 0:
                        # 急停：常闭，1=回路正常；0=按下或断线（故障态）
                        if pressed:
                            text, color = "急停", theme.CLR_FAULT
                        else:
                            text, color = "正常", theme.CLR_OK
                    else:
                        if pressed:
                            text, color = "按下", theme.CLR_OK
                        else:
                            text, color = "未按", self.VALVE_NA_COLOR
            key = (text, color)
            if self._btn_render[slot] == key:
                continue               # 同值早退：稳态零重排
            self._btn_render[slot] = key
            self._btn_state[slot].setText("● " + text)
            self._btn_state[slot].setStyleSheet(
                "QLabel { color: %s; font-size: 18px;"
                " background: transparent; }" % color)

        missing = [self.BTN_SLOT_LABELS[i] for i in range(self.BTN_SLOT_COUNT)
                   if i not in ch_map]
        if missing:
            self._set_note(self._btn_note,
                           "未配置：%s" % "、".join(missing), theme.CLR_WARN)
        else:
            self._set_note(self._btn_note, "", theme.CLR_OFFLINE)

    @staticmethod
    def _set_note(label, text, color):
        """小字说明（文本+颜色都变了才写）：setText 同值早退，但
        setStyleSheet 每次都会重抛样式，缓存一层避免稳态无谓重排。

        [空文本时收起 2026-11-12] 空 QLabel 仍占一行高度（sizeHint 只
        算到字高，不为 0）⇒ 首页浊度仪卡那条"有数据时为空"的说明白占
        ~20px，一屏放不下。说明行是**事件驱动**（数据从无到有才变一次，
        不是逐拍翻转），故空时 setVisible(False) 不违反"恒占位"纪律。
        """
        key = (text, color)
        if getattr(label, "_hsf_note_key", None) == key:
            return
        label._hsf_note_key = key
        label.setText(text)
        # [2026-11-23 用户「下面是不是空了整整一行」] 无文案时把这一行
        # **收起**（高度 0 + 隐藏）：看板卡已按内容自然高，再恒占一行就是
        # 一条白边空行。有文案才占一行（越限/断线是事件级，不是逐拍状态）。
        label.ensurePolished()
        label.setVisible(bool(text))
        label.setFixedHeight((label.fontMetrics().height() + 2)
                             if text else 0)
        label.setStyleSheet(
            "QLabel { color: %s; font-size: 18px; background: transparent; }"
            % color)
        label.setVisible(bool(text))

    # ---- 设备表 / 泵详情 ----

    def _refresh_device_table(self, model):
        """设备状态表（悬浮卡 table 态内容；分帧时每 2 拍执行一次）。"""
        health = model.health
        # [地址瘦身 2026-11-19] 表格里显示**紧凑串**（"COM17/115200/#2"），
        # **完整地址**放进 tooltip + 点格复制（见 _on_device_cell_clicked）。
        # [为什么不能只显示完整串] 完整串 21 字符 vs 瘦身后 17 字符；更关键
        # 的是分隔符 "·" 占两个字符宽，缩放后更占地方 ⇒ 站号（最该看的那个
        # 信息）在最右、最容易被截掉。
        rows = [(d.name, d.kind_text(), d.health_text(),
                 model_mod.slim_addr(d.addr) or "-", d.text or "-")
                for d in health.devices]
        # 完整地址另存一份（点格复制用；按行号索引 ⇒ 与表格行一一对应）。
        self._addr_full = [str(d.addr or "") for d in health.devices]
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
            # 表随行数收放：悬浮卡几何要跟着（对齐 C 定高逻辑）。
            # [2026-11-20] 不再要求"行数变了"——弹层开着时每次铺行都校正
            # （_apply_detail_geometry 对尺寸没变是空操作，不会每拍 resize）。
            if self._detail_mode == "table":
                self._apply_detail_geometry()
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
            # [地址格 tooltip = 完整地址] 桌面端鼠标悬停能看到全串；
            # 触摸板没有 hover ⇒ 另有"点格复制"兜底。
            addr_item = table.item(r, 3)
            full = self._addr_full[r] if r < len(self._addr_full) else ""
            if addr_item is not None and full:
                addr_item.setToolTip(full)
            dev = health.devices[r]
            color = {0: theme.CLR_OK, 1: theme.CLR_WARN, 2: theme.CLR_WARN,
                     3: theme.CLR_OFFLINE, 4: theme.CLR_FAULT}.get(dev.health)
            state_item = table.item(r, 2)
            if color and state_item is not None:
                state_item.setForeground(QColor(color))
        # [列宽跟着内容走] 地址列（"COM17/115200/#2"）比表头长得多 ⇒
        # 只按表头定下限会被截（站号恰恰在最右）。
        self._table.refresh_content_widths()

    def _on_device_cell_clicked(self, row, col):
        """点设备表**任意格** ⇒ 弹出该行详情卡；地址格另复制完整地址。

        [2026-11-22 用户口径「显示不全，单击一行弹一张卡片详细显示」]
        表格地址/错误信息列受屏幕宽度限制必然省略号；点行弹
        `_DeviceDetailDialog`（字段原文换行、可选中复制）看全文。
        [地址格复制保留] 板端触摸没有 hover，复制仍是快速取地址的通道；
        与详情卡不冲突（先弹卡，再复制 + 状态行提示）。
        """
        try:
            dev = self.ctx.model.health.devices[row]
        except (IndexError, AttributeError, TypeError):
            dev = None
        if dev is not None:
            self._show_device_detail(dev)
        # 地址格额外复制完整地址（板端触摸没有 hover ⇒ 复制是快速取地址通道）。
        if col != 3:
            return
        full = ""
        if 0 <= row < len(self._addr_full):
            full = self._addr_full[row]
        if not full:
            return
        try:
            from PyQt5.QtWidgets import QApplication
            QApplication.clipboard().setText(full)
        except Exception as exc:                       # noqa: BLE001
            self.ctx.set_status("复制地址失败：%s" % exc)
            return
        self.ctx.set_status("已复制地址：%s" % full)

    def _show_device_detail(self, dev):
        """弹出单设备详情卡（主窗遮罩；点遮罩/卡片外收起）。"""
        self._close_device_detail()
        win = self.window()
        shade = getattr(self, "_device_shade", None)
        if shade is None:
            shade = _HomeShade(self._close_device_detail, win)
            shade.setObjectName("HsfDeviceDetailShade")
            self._device_shade = shade
        shade.setGeometry(win.rect())
        shade.show()
        shade.raise_()
        card = _DeviceDetailCard(dev, win)
        card.destroyed.connect(self._on_device_detail_destroyed)
        self._device_detail = card
        card.adjustSize()
        center = win.rect().center()
        card.move(max(8, center.x() - card.width() // 2),
                  max(8, center.y() - card.height() // 2))
        card.show()
        card.raise_()

    def _close_device_detail(self):
        card = getattr(self, "_device_detail", None)
        if card is not None:
            self._device_detail = None
            try:
                card.close()
            except RuntimeError:
                pass
        shade = getattr(self, "_device_shade", None)
        if shade is not None:
            shade.hide()

    def _on_device_detail_destroyed(self, _obj=None):
        """详情卡被任意路径关闭（×/遮罩/外部 close）⇒ 清引用 + 收遮罩。"""
        self._device_detail = None
        shade = getattr(self, "_device_shade", None)
        if shade is not None:
            shade.hide()

    def _refresh_pump_detail(self, m):
        """泵详情（悬浮卡 pump 态）：字段有效性逐项判，缺的给 "--"。"""
        vfd = m.vfd
        if not vfd.is_fresh():
            self.pd_state.set_value("无数据", theme.CLR_OFFLINE)
            for block in (self.pd_freq, self.pd_setf, self.pd_cur,
                          self.pd_rpm, self.pd_power, self.pd_torque):
                block.set_value("--")
            return
        color = (theme.CLR_FAULT if vfd.faulted
                 else (theme.CLR_OK if vfd.running else theme.text_color()))
        self.pd_state.set_value(vfd.run_state_text(), color)
        self.pd_freq.set_value(
            "%.2f" % vfd.output_freq_hz if vfd.has("outputFreqHz") else "--")
        self.pd_setf.set_value(
            "%.2f" % vfd.set_freq_hz if vfd.has("setFreqHz") else "--")
        self.pd_cur.set_value(
            "%.2f" % vfd.output_current_a
            if vfd.has("outputCurrentA") else "--")
        self.pd_rpm.set_value("%.0f" % vfd.speed_rpm)
        # [功率/转矩 2026-11-19] 缺键 ⇒ "--"（不编造 0）。
        self.pd_power.set_value(
            "%.2f" % vfd.output_power_kw if vfd.has("outputPowerKw") else "--")
        self.pd_torque.set_value(
            "%.1f" % vfd.torque_percent if vfd.has("torquePercent") else "--")

    # ---- 悬浮详情卡（对齐 C hsfHomeApplyDetail / hsfHomeFloatCloseSlot）----

    def _toggle_detail(self, mode):
        """点卡：同卡再点收起，否则切到该卡（对齐 C hsfHomeHealthClicked）。"""
        self._detail_mode = "none" if self._detail_mode == mode else mode
        self._apply_detail()

    def _close_detail(self):
        """× / 遮罩：收起浮层（对齐 C hsfHomeFloatCloseSlot）。"""
        self._close_device_detail()     # 单设备详情卡一并收起
        self._detail_mode = "none"
        self._apply_detail()

    def _apply_detail(self):
        """把 _detail_mode 落到界面（标题/内容切换 + 遮罩/浮层显隐）。"""
        mode = self._detail_mode
        if mode == "none":
            self._shade.hide()
            self._detail_float.hide()
            return
        if mode == "table":
            self._detail_title.setText("设备状态")
            self._table.setVisible(True)
            self._pump_detail.setVisible(False)
        else:
            self._detail_title.setText("搅拌泵详情")
            self._table.setVisible(False)
            self._pump_detail.setVisible(True)
        self._apply_detail_geometry()
        # [延迟一拍补算] 打开瞬间表格的高度刷新（_schedule_height_refresh）
        # 可能还没落定 ⇒ 首算偏矮。落定后再校正一次（尺寸没变就是空操作）。
        self._apply_detail_geometry_deferred()
        self._shade.show()
        self._shade.raise_()
        self._detail_float.show()
        self._detail_float.raise_()

    def _apply_detail_geometry(self):
        """按当前内容重算浮层尺寸并居中（表随行数收放，C 端同口径）。

        [宽度夹取] 页面比期望宽度窄时（紧凑竖屏）夹到页宽 - 24，
        浮层不出页面。
        """
        if self._detail_mode == "none":
            return
        if self._detail_mode == "table":
            width = self.DETAIL_TABLE_W
            # [必须取**内层 QTableWidget** 的高度] DataTable 是外壳 QWidget，
            # 它的 minimumHeight() 是「显式最小高」属性，默认 0 —— 不是布局
            # 算出来的最小高。拿它当内容高 ⇒ 浮层只剩标题行那点高度，
            # 表格体被裁掉，设备状态一条都看不见（2026-11-09 实测截图）。
            # fit_to_content() 把高度写在**内层表**上（setMinimum/Maximum），
            # 这里读内层才是真高度。
            # [2026-11-20 三重保险] minimumHeight 在"延迟一拍的高度刷新"
            # 落定前可能是旧值 ⇒ 再并上内层表的 sizeHint（content-sized
            # 表的 sizeHint 就是内容高）与**外壳**的下限/sizeHint，取大者
            # ——宁可高一点也不裁行。
            inner_h = max(self._table.table.minimumHeight(),
                          self._table.table.sizeHint().height(),
                          self._table.minimumHeight(),
                          self._table.sizeHint().height())
        else:
            width = self.DETAIL_PUMP_W
            inner_h = self._pump_detail.sizeHint().height()
        # [浮层高度 = 布局真实 sizeHint 与「内容高+64」取大者 2026-11-20
        #  用户截图「窗口小了」] `+64` 是标题+边距的**估算预算**，1.5× 字号
        # 下真实 chrome（标题行 + 卡片边框 + 布局间距）比它高 ⇒ 末行被裁
        # 半截（"网络相机"只露一半）。浮层自己的布局 sizeHint 是
        # 标题+边距+内容的真实总和（表格是 content-sized Fixed 策略，
        # sizeHint = 内容高）⇒ 两者取大者，宁高不裁。
        lay_h = 0
        _lay = self._detail_float.layout()
        if _lay is not None:
            lay_h = _lay.sizeHint().height()
        width = min(width, max(320, self.width() - 24))
        height = max(inner_h + 64, lay_h)
        # [高度也要夹取] 页高不够时浮层不能顶出页面（窄屏/小窗）。
        height = min(height, max(160, self.height() - 24))
        if (self._detail_float.width(), self._detail_float.height()) != \
                (width, height):
            self._detail_float.resize(width, height)
        self._center_float()

    def _apply_detail_geometry_deferred(self):
        """延迟一拍重算浮层几何 + **量亏空补高**（一轮收敛）。

        [为什么要亏空补 2026-11-20 用户截图「窗口小了」] 布局落定前算的
        高度（sizeHint/内容高+64）都未必含**横向滚动条/表头实高/边框**的
        全部账 ⇒ 末行裁半截。落定后直接量：表格视口高比内容高差多少、
        浮层就补多少（一轮收敛；差 0 就是空操作）。
        """
        from PyQt5.QtCore import QTimer

        def _fix():
            self._apply_detail_geometry()
            if self._detail_mode != "table":
                return
            t = self._table.table
            try:
                # [判据 = 内表**控件高** ≥ content_height] content_height
                # 含表头/横向滚动条（它们住在视口**外面**是设计使然）⇒
                # 拿 viewport 比 content 会永远差一个 chrome 高（虚亏）。
                # 控件高 < 内容高才是真裁行。
                deficit = t.content_height() - t.height()
            except RuntimeError:
                return                          # 页面已析构
            if deficit > 0:
                # [亏空补在外壳下限] 外壳（DataTable）被 fit_to_content 钉成
                # content-sized **Fixed** 策略 ⇒ 浮层再高它也不涨（富余被
                # 策略挡在外面）⇒ 亏空要补在**外壳的 minimumHeight** 上，
                # 内层表跟外壳走 ⇒ 控件高涨到内容高、末行完整可见。
                self._table.setMinimumHeight(
                    self._table.minimumHeight() + deficit + 2)
                self._table.updateGeometry()
                self._apply_detail_geometry()

        QTimer.singleShot(0, _fix)
        # [两轮收敛] 第一轮补高后布局再落定一次，可能还有几像素尾巴。
        QTimer.singleShot(80, _fix)

    def _center_float(self):
        """浮层居中 + 遮罩盖满页面（弹出期间每次尺寸变化都校正）。

        [居中在「可见区带」 2026-11-20 实锅] 页面在滚动壳里可以比窗口高
        （验收跑 --size 1280x4200 把窗口高写进共享 INI ⇒ 真实客户端恢复
        成页高 4200/窗口 749）⇒ 按 self.height() 居中会把浮层算到
        y≈1800 的**视口外**（点设备健康"弹不出来"，其实弹到屏外去了）。
        ⇒ 换算成「页面在窗口里的可见带」内居中：
            band_top = max(0, -页面原点在窗口坐标的 y)   # 已滚上去的部分
            band_h   = min(页高, 窗口高 - band_top)      # 可见的那段
        浮层 y = band_top + (band_h - fh) / 2；x 照旧按页宽居中。
        """
        if self._detail_mode == "none":
            return
        pw, ph = self.width(), self.height()
        fw = self._detail_float.width()
        fh = self._detail_float.height()
        self._shade.setGeometry(0, 0, pw, ph)
        from PyQt5.QtCore import QPoint
        page_y = self.mapTo(self.window(), QPoint(0, 0)).y()
        band_top = max(0, -page_y)
        band_h = max(fh, min(ph, self.window().height() - page_y)
                     - band_top)
        self._detail_float.move(max(0, (pw - fw) // 2),
                                band_top + max(0, (band_h - fh) // 2))

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        # [浮层跟随页面尺寸] 窗口缩放后重算居中/遮罩覆盖（C 端同口径）。
        if getattr(self, "_detail_mode", "none") != "none":
            self._center_float()


# ============================================================================
#                              菜单页
# ============================================================================

#: 一/二级菜单大按钮的统一样式（2026-11-22 用户「二级选项放大到和一级
#: 一样」）。**必须写 QSS `min-height`**：全局 QPushButton 规则里的
#: min-height 会压过部件的 setMinimumHeight（theme.py QSS 注释同款陷阱）。
#: 36px 两行（主菜单图标+名称）自然高约 94px；子菜单单行靠 min-height 80
#: + padding 12 + 边框 2 拉到同一高度（实测两者都 94~95px）。
MENU_BTN_QSS = ("QPushButton { font-size: 36px; font-weight: bold;"
                " min-height: 80px; }")


class MainMenuPage(Page):
    """主菜单：主界面 + 三大分类入口（大按钮，触控友好）。

    [2026-11-22 「菜单页模式」] 菜单页档会隐藏左侧导航树 ⇒ 本页必须自带
    「主界面」入口（否则回不了首页）。四个入口按 2×2 排，按钮略矮一档
    （120→130 自然高按两行算），窄屏也不会挤成一条。
    """

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("主菜单", "选择功能分类")
        entries = reg.MAIN_MENU_ENTRIES
        # [用 QWidget 包住 grid 并限制高度] 直接 addLayout(grid, 1) 会让网格
        # 吃掉全部剩余高度，按钮被拉到 500px 高、文字漂在中间（截图实见）。
        cols = 2 if len(entries) > 3 else max(1, len(entries))
        rows = (len(entries) + cols - 1) // cols
        button_natural_h = 100       # MENU_BTN_QSS 一档的实际高（含 padding）
        holder = QWidget()
        holder.setMaximumHeight(rows * button_natural_h + (rows - 1) * 16)
        grid = QGridLayout(holder)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(16)
        for i, (name, page, icon) in enumerate(entries):
            btn = QPushButton("%s\n%s" % (icon, name))
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet(MENU_BTN_QSS)
            btn.clicked.connect(lambda _c=False, p=page: self.ctx.navigate(p))
            grid.addWidget(btn, i // cols, i % cols)
        self.root.addWidget(holder)
        self.root.addStretch(1)


class SubMenuPage(Page):
    """通用子菜单页（监视/数据/设置三个分类共用）。"""

    def __init__(self, ctx, title, entries):
        Page.__init__(self, ctx)
        head = self.add_header(title, "选择子项")
        # [2026-11-22 用户定案「返回上级放右边 + 文字显示不全」] 靠右放；
        # 文本只留「返回」两字（QSS font 在 polish 前算 sizeHint，带箭头
        # 的长文本会被裁掉尾部——现场截图实见）；完整语义走 tooltip。
        # `add_header` 末尾已有 stretch ⇒ addWidget 即右对齐。
        self.back_btn = QPushButton("返回")
        self.back_btn.setCursor(Qt.PointingHandCursor)
        self.back_btn.setToolTip("返回上一级（主菜单）")
        self.back_btn.clicked.connect(
            lambda _c=False: self.ctx.navigate(reg.PAGE_MENU))
        head.addWidget(self.back_btn)
        grid = QGridLayout()
        grid.setSpacing(16)
        # [2026-11-22 用户定案「二级选项小了很多，放大到和一级一样；放不下
        # 就出垂直滚动条」] 子菜单与主菜单同档：**2 列 + 同一个
        # MENU_BTN_QSS**（36px 粗体 + QSS min-height 86 ⇒ 实际约 100px）。
        # 7 个设备子项 ⇒ 4 行，页面自身 QScrollArea 出现垂直滚动条。
        for i, (name, page) in enumerate(entries):
            btn = QPushButton(name)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setStyleSheet(MENU_BTN_QSS)
            btn.clicked.connect(lambda _c=False, p=page: self.ctx.navigate(p))
            grid.addWidget(btn, i // 2, i % 2)
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

    [行高亮口径 2026-11-24 用户「都不标了」] 历史表**不再给任何行上色**
    （原「变频器故障 / 浊度≥20 / 流量计报警 / 压差越限 / 阀故障」整行
    淡红底已取消；异常仍可从各列数值/说明直接读）。

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
        = **按当前筛选条件命中的全部行**（表格是分页的，界面上只有当前
        页那几十行；导出走「逐批连续取」拿全量，口径与 C 端一致）。
    """

    #: 列定义（表头与 CSV 导出共用一份，改这里两处同步）。
    COLUMNS = ["时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
               "搅拌转速(rpm)", "电机温度(℃)",
               "进水浊度(NTU)", "出水浊度(NTU)", "水温(℃)",
               "瞬时流量", "正累计", "压差(MPa)",
               # [压力1/压力2 2026-11-23 用户口径「压力1和压力2也要进历史
               #  数据被采集」] 显示列插在「压差(MPa)」之后（与压差卡三栏
               # 同序）；线上列序另在 proto.HISTORY_COLUMNS 末尾追加。
               "进水压力(MPa)", "出水压力(MPa)",
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

    #: [2026-11-24 用户「按筛选的结果、导出的全部数据平均采样」] 图表不再有
    #: 行数上限（旧口径 CHART_MAX_ROWS=12000 已废弃）：按筛选条件 keyset 取
    #: **全量**，取的过程中按步长等距抽样 ⇒ 内存只随采样点数走，不随库行数涨。
    #: 图表单批请求行数。协议上限就是 50（proto.HISTORY_PAGE_MAX），
    #: 用满可以少跑轮次。
    CHART_CHUNK = proto.HISTORY_PAGE_MAX
    #: 每条折线抽样点数可选档 + 缺省（2026-11-24 用户「抽样 2000 还是太多
    #: 了，可以设置抽样数吗」）。本机设置键 `histChartPoints`；缺省 100。
    CHART_POINT_CHOICES = (100, 200, 500, 1000, 2000)
    CHART_POINTS_DEF = 100

    #: 服务端分页应答 / 失败（网络线程 emit，UI 线程收 —— Qt 线程规则）。
    pageReady = pyqtSignal(dict)
    pageFailed = pyqtSignal(str)
    #: [图表全量取数专用应答通道 2026-11-17] 与表格分页**物理隔离**：
    #: 原来图表批和表格页共用 `_inflight` + `_pending_kind` ⇒ 两者互相把
    #: 对方的应答当成自己的（表格页 20 行 < 图表批 50 行 ⇒ 图表被判"短批"
    #: 收尾，只画第一页；图表在途时翻页被静默吞掉）。用独立信号后每条
    #: 应答的归属**由它走哪个通道决定**，不再靠共享状态猜。
    chartReady = pyqtSignal(dict)
    chartFailed = pyqtSignal(str)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        # [折叠把手挂页头右侧 2026-11-19 用户口径「放第一行标题栏右边，
        # 白白占了一行」] add_header 返回标题行布局 ⇒ 折叠按钮挂它右侧
        # （web 端 data_toolbar.js 同款改造，两端一致）。
        self._head_row = self.add_header("历史数据", "服务端历史库，分页浏览")
        self._rows = []              # [(texts, abnormal), ...] 旧→新（当前页）
        self._last_sample_ms = 0

        # ---- 图表视图状态（2026-11-10 页内双视图）----
        #: 0=表格 1=图表（同格二选一，对齐 C hsfDataHistorySetView）。
        self._view_mode = 0
        #: 内嵌折线图控件（首次切图表视图才建；Qt.Widget 旗标当普通控件）。
        self._chart_dialog = None
        #: 图表全量取数的累加缓冲：[(texts, abnormal), ...]（**按步长抽样后**
        #: 的筛选结果，不是当前页）。切图表视图时按当前筛选条件 keyset 取全量，
        #: 边取边抽样，内存只随「每线点数」选择走。
        self._chart_rows = []
        #: 本轮到货的原始行数（含被采样丢掉的）与采样步长/总行数。
        self._chart_scanned = 0
        self._chart_stride = 1
        self._chart_total = 0
        #: keyset 游标（下一批的 beforeTs；每批用本批最小 ts - 1）。
        self._chart_cursor = None
        self._chart_fetch_offset = 0
        self._chart_fetching = False
        #: 图表取数的**冻结锚点**（epoch ms）。表格窗口锚点 `_window_end`
        #: 会随刷新/探活重新锚定（"从现在往前看"），图表若跟着它变，筛选
        #: 签名就每拍都变 ⇒ 每 3 秒重跑一次上百批全量取数（实测踩到）。
        #: 故图表在**开始取数那一刻**冻结锚点，直到筛选条件真变才重取。
        self._chart_anchor = None
        #: 已累加批次的筛选签名（lo/hi/kw/列）。相同就不再重取。
        self._chart_sig = None
        #: 图表视图状态下标（"筛选共 N 行 · 图表 M 行"口径）。
        self._chart_status = None

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
        # ---- 全量导出状态（发起那一刻冻结的快照 + 逐批取数器）----
        #: 冻结的列下标 / 关键字 / 时间窗 / 锚点 / 落地路径；取数期间用户
        #: 改筛选/改列/翻页都不影响这一轮导出的内容。
        self._export_cols = None
        self._export_kw = None
        self._export_lo = None
        self._export_hi = None
        self._export_anchor = 0
        self._export_path = ""
        self._export_runner = None
        self._export_is_xlsx = False
        self._export_fallback = 0
        self._export_csv = None
        self._export_xlsx_rows = []
        #: 在途请求的类型："page"（要整页数据）/"probe"（只要总数）。
        self._pending_kind = "page"
        self._pending_index = 0
        self._live_timer = QTimer(self)
        self._live_timer.setInterval(3000)
        self._live_timer.timeout.connect(self._live_tick)
        self.pageReady.connect(self._on_page_ready)
        self.pageFailed.connect(self._on_page_failed)
        self.chartReady.connect(self._on_chart_ready)
        self.chartFailed.connect(self._on_chart_failed)

        self._table = DataTable()
        self._table.set_headers(self.COLUMNS)
        # [列宽：下限按**当前字体实测**，不写死像素]
        #
        # 全局最小档是 72px，但**表头文字**比数据行更长（"进水浊度(NTU)"、
        # "电箱湿度(%RH)"…）。早先这里写死了一组像素（165/78/96…），那是
        # 按 10pt 量的 —— 客户端实际用 ui_scale 后的 15pt，2026-11-10 实锅：
        # 15 列里 12 列表头被截成 "出频率(H"、"度1(NTU"（用户截图反馈
        # 「列名显示不全」）。
        # 现在交给 DataTable 的 auto_min_texts：每次重算列宽都按表头字体
        # 现量（字号缩放后自动跟着变）；时间列额外按**内容**量（表头只有
        # "时间"两个字，实际要装 'yyyy-MM-dd HH:MM:SS'）。
        # [DI/DO 同按内容量 2026-11-19] 数据是 `0x%08X`（10 字符）比表头
        # `DI(32位)`（6 字符）长 ⇒ 只按表头量宽，列宽装不下内容（web 端
        # fixed 布局下更是直接溢出叠列，用户实报「两列数据叠在一起」）⇒
        # 两端同补这两列的内容量宽样本。
        # [瞬时流量/正累计同款 2026-11-19 桌面端实测] 带单位内容
        # "3.66 m³/h"（桌面 15pt×缩放下要 196px）比表头"瞬时流量"（4 字）
        # 长得多，分配 110px ⇒ 数据被裁半截贴着列线（视觉=叠列）。
        # **凡「数据比表头长」的列都要进这张表**（web 端 COL_MIN_TEXTS 同步）。
        # 下限总和超出视口时宁出横向滚动条，也不把文字截断（分配器口径）。
        # [19 列（2026-11-23 加「电机温度(℃)」+「压力1/出水压力(MPa)」：
        #  电机温度插在显示列第 6 位、压力1/2 插在压差之后）]
        # 权重与 auto_min_texts 都是**按下标**写的 ⇒ 插列后必须整体同步：
        # 否则「3.66 m³/h」的下限跑到水温列、DI 的下限跑到电箱湿度列，
        # 且电机温度列只拿到权重宽（表头 6 个汉字被截成「电机温度(」）。
        # [压力1/2 之后 电箱温度/湿度/DI/DO 从 13/14/15/16 变 15/16/17/18]
        self._table.set_column_weights(
            (3, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 3, 3, 3),
            auto_min_texts={0: "2026-11-10 00:00:00",
                            # [模式列 2026-11-20 用户实报「模式列显示不全」]
                            # 内容最长是「自动·运行中」（6 字符），表头只有
                            # 「模式」2 字 ⇒ 只按表头量宽必然截成「自动…」。
                            # 网页端 COL_MIN_TEXTS 早有这一项，桌面端漏了。
                            1: "自动·运行中",
                            # [长表头硬下限 2026-11-23] 表头 6 个汉字 + 括号
                            # 单位，比内容（"0.0"）长得多；只按权重分不够 ⇒
                            # 明确给下限（与网页端 COL_MIN_TEXTS[6] 同思路）。
                            6: "电机温度(℃)",
                            # [正累计 2026-11-24 用户「数据显示不全」] 累计量
                            # 会到 6 位整数（"999999.99 m³"），旧样本 "2.30 m³"
                            # 太短 ⇒ 无数据/短值页仍按这个长样本给硬下限。
                            10: "3.66 m³/h", 11: "999999.99 m³",
                            17: "0x00005AAA", 18: "0x00000100"})
        # [列宽跟着内容走 + 允许横向滚动 —— 照网页端口径 2026-11-17]
        # 16 列的下限总和远大于视口 ⇒ 塞进 1680px 只能把字缩到 ~12px
        # （用户实报「数据界面的表格文字太小了」）。网页端的做法是：
        # **列宽按内容**（内容长 ⇒ 列宽跟着长）+ 装不下就**横向滚动**，
        # 字不被省略号截断 ⇒ 表格读数清楚，右半截的列横向滚过去看。
        # ⇒ 桌面端同步改成同一口径（关掉"缩字号硬塞"、开"按内容给下限"）。
        self._table.set_fit_columns(False)
        self._table.set_content_floors(True)

        # ---- 排序 / 筛选状态（双击表头排序；筛选栏按此过滤显示视图）----
        self._sort_col = None          # None=不排序（默认最新在上）
        self._sort_desc = True
        self._table.table.horizontalHeader().sectionDoubleClicked.connect(
            self._header_dblclicked)

        # ---- 工具条：筛选行 + 分页行 + 视图行（**三页共用控件**）----
        # [为什么换共用控件] 历史/日志/报警三页的工具条此前各写一份 ⇒ 形态
        # 不一、缺功能、改一处改三处。现在一份实现（data_toolbar.
        # DataPageToolbar），三页按能力开关裁剪控件。
        # [属性名保持不变] 页面把控件属性**别名**成自己原有的名字
        # （_f_start/_f_col/_f_text/_page_size_box/…）⇒ 下面的页面逻辑
        # 与既有回归锁一行都不用改。
        # [折线列只列数值列] 时间/模式/搅拌状态是文本，画不了折线。
        self.CHART_COLUMNS = [c for c in range(len(self.COLUMNS))
                              if c not in (0, 1, 2)]
        self._tb = DataPageToolbar(
            self,
            columns=self.COLUMNS,
            chart_columns=[self.COLUMNS[c] for c in self.CHART_COLUMNS],
            page_sizes=self.PAGE_SIZES,
            paging=True, chart_view=True, freeze=True, export=True,
            state_key="history", fold_btn_host=self._head_row)
        # ---- 别名（保持既有属性名）----
        self._f_start = self._tb.start_edit
        self._f_end = self._tb.end_edit
        self._f_col = self._tb.column_combo
        self._f_text = self._tb.keyword_edit
        self._count_label = self._tb.count_label
        self._page_size_box = self._tb.page_size_box
        self._btn_prev = self._tb.btn_prev
        self._page_label = self._tb.page_label
        self._btn_next = self._tb.btn_next
        self._total_label = self._tb.total_label
        self._btn_reload = self._tb.btn_reload
        self._chart_col = self._tb.chart_column_combo
        self._btn_table_view = self._tb.btn_table_view
        self._btn_chart_view = self._tb.btn_chart_view
        self._freeze = self._tb.freeze_box
        self._btn_export = self._tb.btn_export
        # [默认勾前一个折线列] 原来是下拉默认选第一项，行为对齐。
        self._chart_col._model.item(0).setCheckState(Qt.Checked)
        # [本机记住每页条数 2026-11-10] 存过就按它开局（历史页）。
        from . import settings as ui_settings
        saved_size = ui_settings.hist_page_size()
        if saved_size in self.PAGE_SIZES:
            self._page_size = saved_size
        self._tb.set_page_size(self._page_size)
        # [起始时间的手改标记] 用 textEdited 而不是 textChanged —— 后者连
        # 程序 setText 也算，那样 `_apply_default_start` 自己填一次就把
        # 标记置真了。
        self._f_start_touched = False
        self._default_start_done = False
        self._f_start.textEdited.connect(self._on_start_edited)
        # [回车即筛选 2026-11-17] 日志页/报警页一直有，历史页漏了 ⇒ 同一
        # 手感三页不一致（用户敲完关键字顺手回车，历史页没反应）。
        self._f_text.returnPressed.connect(self._apply_filter)
        # ---- 信号接回页面原有的槽（行为不变）----
        self._tb.filterRequested.connect(self._apply_filter)
        self._tb.clearRequested.connect(self._clear_filter)
        self._tb.pageSizeChanged.connect(self._on_page_size)
        self._tb.prevRequested.connect(self._prev_page)
        self._tb.nextRequested.connect(self._next_page)
        self._tb.refreshRequested.connect(self._reload_current)
        self._tb.exportRequested.connect(self._export_data)
        self._tb.viewChanged.connect(self.set_view)
        # [列勾选变化**立即生效**] 不用点「筛选」——那不是筛选条件而是
        # 视图设置。
        self._tb.columnsChanged.connect(self._on_columns_changed)
        # [图表视图下改勾选列立即重画] 全量数据已经在 `_chart_rows` 里，
        # 换列只需要重画、不用重取（对齐 C 端勾选框即时重建序列的口径）。
        self._tb.chartColumnsChanged.connect(self._on_chart_columns_changed)
        # [滚动锁定] 勾上=停止刷表（后台照常采样）；取消勾选时补刷一次。
        self._freeze.toggled.connect(
            lambda frozen: None if frozen else self._rebuild())
        self.root.addWidget(self._tb)
        # [折叠筛选区 ⇒ 表格吸收腾出的高度 2026-11-19 用户口径]
        # 折叠工具条后若没人吸收腾出的空间，表格下方会留一片空白 ⇒ 折叠
        # 白折；让表格改成可拉伸 ⇒ 一次多显示若干行（折叠的真正目的）。
        self._tb.collapsedChanged.connect(self.on_data_toolbar_collapsed)

        # ---- 双视图（第 0 页=表格卡片，第 1 页=内嵌折线图）----
        # [为什么不再弹窗] 见上面工具行的说明；这里照报警页的做法：
        # LineChartDialog 加 Qt.Widget 旗标当普通控件进布局，缩放/平移/
        # 气泡交互全部沿用同一份实现（不复制第二套图表代码）。
        card = Card()
        card.add(self._table)
        table_page = QWidget()
        tp_lay = QVBoxLayout(table_page)
        tp_lay.setContentsMargins(0, 0, 0, 0)
        tp_lay.addWidget(card)

        chart_page = QWidget()
        cp_lay = QVBoxLayout(chart_page)
        cp_lay.setContentsMargins(0, 0, 0, 0)
        # 图表视图状态行（对齐 C 的 histChartStatusText：取回多少行、
        # 每线多少点、是否抽样）。
        self._chart_status = QLabel("")
        self._chart_status.setStyleSheet(theme.caption_label_qss())
        # [抽样点数可选 2026-11-24 用户「抽样 2000 还是太多了，可以设置
        #  抽样数吗」] 图表视图顶部给一档下拉（本机设置持久化）；改档后
        # 已取的抽样行无法上采样 ⇒ 清签名重取全量。
        from . import settings as _ui_settings
        self._chart_points = QComboBox()
        self._chart_points.setObjectName("comboHistChartPoints")
        for _n in self.CHART_POINT_CHOICES:
            self._chart_points.addItem(str(_n), _n)
        _saved_pts = _ui_settings.hist_chart_points()
        _pidx = self._chart_points.findData(_saved_pts)
        if _pidx >= 0:
            self._chart_points.setCurrentIndex(_pidx)
        self._chart_points.setToolTip(
            "每条折线最多画多少个点（等距抽样、首尾必留）\n"
            "改小更清爽；改大更细。设置会保存到本机")
        self._chart_points.currentIndexChanged.connect(
            self._on_chart_points_changed)
        _chart_bar = QHBoxLayout()
        _chart_bar.setSpacing(8)
        _chart_bar.addWidget(self._chart_status, 1)
        _chart_bar.addWidget(QLabel("每线点数"))
        _chart_bar.addWidget(self._chart_points)
        cp_lay.addLayout(_chart_bar)
        self._chart_holder = QWidget()
        ch_lay = QVBoxLayout(self._chart_holder)
        ch_lay.setContentsMargins(0, 0, 0, 0)
        self._chart_min_h = 420
        # [表格视图不撑页面 2026-11-22] QStackedWidget 的最小高取所有页的
        # 最大者；图表页的 420px 下限会把「表格视图」的整页最小高顶到
        # 420+工具条 ⇒ 外层仍可滚、表头跟着走。表格视图下先放开，切图表
        # 视图再恢复（set_view 里）。
        self._chart_holder.setMinimumHeight(0)
        cp_lay.addWidget(self._chart_holder, 1)

        self._view_stack = QStackedWidget()
        self._view_stack.addWidget(table_page)      # index 0 = 表格
        self._view_stack.addWidget(chart_page)      # index 1 = 图表
        self.root.addWidget(self._view_stack, 1)
        # 表格吃满剩余视口 ⇒ 页面本身不滚，只有表格内部滚 ⇒ 表头固定。
        self.fit_table_to_viewport(self._table)

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
        """离页停轮询：省掉无谓的 SQL 与流量（页面还在，数据不动）。

        [图表取数链也要停 2026-11-16 审计] 图表全量取数是「短批收尾前
        每批衔接下一批」的自续链（keyset 全量，2026-11-24 起不再有 12000
        行上限）——离页原来只停 `_live` 定时器 ⇒ 取数链继续在后台向服务端
        发请求；期间回到历史页时 `_request_page` 被 `_inflight` 静默吞掉
        ⇒ 表格停在旧页直到几百批取完（弱链上可达数分钟）。⇒ 离页即摘标志：
        `_chart_accumulate` 本就有"用户已切走这批作废"的判据，摘掉后
        **在途批自然终止**。
        """
        self._stop_live()
        # [半截结果必须丢 2026-11-17] 只置 _chart_fetching=False 时，
        # `_chart_rows` 留在内存里且 `_chart_sig` 还是上次的签名 ⇒ 回来后
        # `_chart_refresh` 见"有 rows + 签名相同"就直接画**半截数据**，
        # 而且之后点「刷新」也不会重取（只能改筛选才恢复）。
        self._chart_fetching = False
        self._chart_rows = []
        self._chart_sig = None

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
        # [图表批已走 chartReady 独立通道 2026-11-17] 本函数只处理表格页/
        # 探活的应答；图表批永远不从这里进来（见 chartReady 的注释）。
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
        if self._view_mode == 1 and self._chart_filter_sig() != self._chart_sig:
            # [只在筛选真变时重取] 对齐 C 端 page 应答尾部
            # `if (viewMode == 1) histBuildChartEx(...)` 的意图，但不能无条件
            # 调：历史页有 3s 探活/刷新，无条件调会每拍比对签名——而签名一旦
            # 因窗口锚点漂移就会触发上百批重取（实测"正在加载图表数据…"永不
            # 结束）。这里用签名闸住：筛选没变就什么都不做（图表保持当前
            # 全量结果），用户点「刷新」/改筛选才重取。
            self._chart_refresh()

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
            # [电机温度 2026-11-23] 模拟量温度传感器（与 VFD 卡 motorTempC
            # 同源）；未配置/读不到入库 NULL ⇒ 显示 "--"（不是 0℃）。
            num("motor_temp", "%.1f"),
            num("turb1"), num("turb2"),
            # [水温 2026-11-10] 显示在第 8 位（紧跟浊度2）；线上列序里它在
            # 末尾（proto.HISTORY_COLUMNS），此处按**列名**取值，位置无关。
            # 无温度机型该列入库 NULL ⇒ 显示 "--"（不是 0℃）。
            num("water_temp", "%.1f"),
            "--" if flow is None else "%.2f %s" % (float(flow), flow_unit),
            "--" if total is None else "%.2f %s" % (float(total), total_unit),
            # [压差列显示**数值** 2026-11-19 用户口径「历史数据里应该记录
            #  具体的负压值用负数，而不是说是个负压」] 原来 note 优先 ⇒
            #  负压行显示「低于 4.0mA（负压）」、看不到 -0.008 ⇒ 现在数值
            #  优先；「当时越限」的信号仍在 press_note 上（下面的异常行判定
            #  照旧用它）⇒ 不丢信号。
            num("press_mpa", "%.3f"),
            # [压力1/压力2 2026-11-23 用户口径「压力1和压力2也要进历史数据
            #  被采集」] 显示在压差之后（与压差卡三栏同序）；线上列序在
            #  proto.HISTORY_COLUMNS 末尾（此处按**列名**取值，位置无关）。
            #  未配传感器/通道无读数入库 NULL ⇒ "--"（不是 0），与压差同款。
            num("press1_mpa", "%.3f"), num("press2_mpa", "%.3f"),
            num("temp", "%.1f"), num("hum", "%.1f"),
            "--" if di is None else "0x%08X" % (int(di) & 0xFFFFFFFF),
            "--" if do is None else "0x%08X" % (int(do) & 0xFFFFFFFF),
        ]
        # [整行不再标红 2026-11-24 用户「都不标了」] 历史表不再按
        # 故障/浊度/压差说明给整行上淡红底（三端同口径）；异常仍可通过
        # 各自的数值/说明列阅读。
        abnormal = False
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
        # 记住本机偏好（历史页）
        from . import settings as ui_settings
        ui_settings.set_value(ui_settings.K_HIST_PAGE_SIZE, int(size))
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
        # [列序必须与 COLUMNS 逐位对齐 2026-11-23] 本轮新增「电机温度」时
        # 发现本地采样行**少了水温列**（15 项 vs 表头 16 列）⇒ 从「水温」起
        # 整排错位（表头写水温、格子里是流量…）。本地/服务端两条路径的
        # row 都按 COLUMNS 位置铺 ⇒ 一次补齐（电机温度 + 水温 + 压力1/2），
        # 两边各 19 项。
        texts = [time.strftime("%Y-%m-%d %H:%M:%S"),
                 self._mode_text(m),
                 self._vfd_state_text(m),
                 self._vfd_num_text(m, "output_freq_hz"),
                 self._vfd_num_text(m, "output_current_a"),
                 self._rpm_text(m),
                 self._motor_temp_text(m),
                 self._turb_text(m, 0), self._turb_text(m, 1),
                 self._water_temp_text(m),
                 self._flow_text(m), self._total_text(m),
                 self._press_text(m),
                 # [压力1/压力2 2026-11-23 用户口径「压力1和压力2也要进历史
                 #  数据被采集」] 显示在压差之后（列序与 COLUMNS 逐位对齐）；
                 #  按模拟量**通道名**找（名字是唯一稳定标识，通道号可配）。
                 self._analog_press_text(m, HomePage.PRESS1_NAME),
                 self._analog_press_text(m, HomePage.PRESS2_NAME),
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
    def _motor_temp_text(m):
        """电机温度：模拟量温度传感器（VFD 遥测 motorTempC）。

        缺键 = 服务端没这一拍温度（未配/读不到）⇒ "--"，不是 0℃。
        """
        if not m.vfd.is_fresh() or not m.vfd.has("motorTempC"):
            return "--"
        value = m.vfd.motor_temp_c
        return "--" if value is None else "%.1f" % float(value)

    @staticmethod
    def _water_temp_text(m):
        """水温：带温度的浊度仪随帧带出（与水质页同口径）。

        遍历被选中的那一侧的两台，取**第一台**有温度的；都没有 ⇒ "--"
        （缺键 = 该机型没有温度，不是 0℃）。
        """
        st = m.turbidity.pick()
        if st is None:
            return "--"
        for idx in range(int(getattr(st, "count", 0) or 0)):
            temp = st.temp_of(idx)
            if temp is not None:
                return "%.1f" % float(temp)
        return "--"

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
        """历史表不再标异常行（2026-11-24 用户「都不标了」）。"""
        return False

    @staticmethod
    def _turb_text(m, index):
        """浊度 NTU；该台离线（online=False）时 ntu_of 返回 None → "--"。"""
        st = m.turbidity.pick()
        if st is None:
            return "--"
        ntu = st.ntu_of(index)
        return "--" if ntu is None else "%.2f" % ntu

    @staticmethod
    def _flow_text(m):
        """瞬时流量：单位随流量计下发的单位码走，不写死 m³/h。"""
        fm = m.flowmeter.pick()
        if fm is None:
            return "--"
        return "%.2f %s" % (fm.flow, fm.flow_unit_text())

    @staticmethod
    def _total_text(m):
        """正向累积总量（单位码与瞬时流量分属两张表，见 model.FLOW_UNIT_TEXT）。"""
        fm = m.flowmeter.pick()
        if fm is None:
            return "--"
        return "%.2f %s" % (fm.forward_total, fm.total_unit_text())

    @staticmethod
    def _press_text(m):
        """压差：模拟量通道 0 的工程量（服务端按 4~20mA → 0~1MPa 换算）。

        [数值优先 2026-11-24 独立复核] 服务端历史 / 网页表格 / 桌面在线表格
        都是数值优先（负压 -0.008），只有本端离线行曾是"说明优先" ⇒ 同一行
        两副面孔。改为数值优先；读不到数值时才退回说明（断线等）。
        """
        if not m.analog.is_fresh():
            return "--"
        value = m.analog.eng_value(0)
        if value is not None:
            return "%.3f" % value
        note = m.analog.eng_note.get(0, "")
        return note or "--"

    @staticmethod
    def _analog_press_text(m, name):
        """压力1/压力2：按模拟量**通道名**找通道并取工程量（MPa）。

        [为什么按名字找通道] 远端 collect 节可把压力1/压力2 改到任意通道
        （press1Channel/press2Channel），服务端把名字挂到新通道上 ⇒ 客户端
        跟着名字走（与首页压差卡三栏同一口径 `HomePage._analog_channel_by_name`）。
        [找不到/无效 ⇒ "--"] 没下发该名字（老服务端/没装传感器）或该通道
        本拍没有换算值（未标定/断线/无读数）一律 "--"，**不编 0**——
        "没数据"与"压力 0 MPa"是两件事（与压差列同款）。
        [取数同源] 值走 `m.analog.eng_value`（服务端 4~20mA 线性换算 ×1000
        下发），格式化与压差列同口径 `%.3f`。
        """
        if not m.analog.is_fresh():
            return "--"
        for ch, ch_name in (m.analog.eng_name or {}).items():
            if str(ch_name) == str(name):
                value = m.analog.eng_value(ch)
                return "--" if value is None else "%.3f" % value
        return "--"

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

        数据行始终是完整 19 列（`_rows` 的文本列表按 COLUMNS 固定顺序存），
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
        # [内容宽重测 2026-11-24 用户「正累计列数据显示不全」] 本页是**直接
        # 写 item**（不走 set_rows）⇒ 列宽上限只按表头/auto_min_texts 的
        # 短样本量 ⇒ "123456.78 m³" 这种长累计值被省略号截断。每次重铺后
        # 量一遍本页最长单元格（宽度签名不变则不重排；图表视图下表格隐藏，
        # 干脆不量，切回表格时补一次）。
        if self._view_mode == 0:
            self._table.refresh_content_widths()
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
        # [起止颠倒拦截 2026-11-17] 起始晚于结束 ⇒ 服务端 WHERE 恒空 ⇒
        # 界面显示"共 0 条"，用户以为数据丢了。⇒ 明确提示且**不发请求**
        # （与日志页/报警页同口径）。
        lo_ms, hi_ms = self._server_time_range() or (None, None)
        if lo_ms is not None and hi_ms is not None and lo_ms > hi_ms:
            self.ctx.set_status("起始时间晚于结束时间，未筛选")
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

    def set_view(self, mode):
        """表格/图表双视图切换（0=表格 1=图表；对齐 C hsfDataHistorySetView）。

        [为什么不弹窗] 用户 2026-11-10 明确要求「图表不要新建一个窗口」，
        C 端历史页也是同格二选一（表格 ⇄ 图表）。故这里用 QStackedWidget
        在同一页内切换，图表控件以 Qt.Widget 旗标内嵌（与报警页同款）。

        [切进图表视图要取全量] 表格是分页的（一页 ≤50 行），图表画一页
        看不出趋势 —— C 端是分页累加**全量筛选结果**（上限 12000 行）后
        再画。这里同口径：切进来就按当前筛选条件分页取全量（见
        `_chart_refresh`），已取过且筛选没变则不重复取。
        """
        mode = 1 if mode == 1 else 0
        if mode == 1:
            # [先取数再切页] 取数是异步的：先把视图切过去（显示"正在加载"），
            # 应答回来再填图，用户点下去立刻有反应。
            self._chart_holder.setMinimumHeight(self._chart_min_h)
            self._view_stack.setCurrentIndex(1)
        else:
            # 表格视图不撑页面（见构造处注）⇒ 切回时放开图表下限再钉表格高。
            self._chart_holder.setMinimumHeight(0)
            self._view_stack.setCurrentIndex(0)
            # 切回表格：重新按视口钉高度（图表页期间表格是隐藏的，没重量）。
            QTimer.singleShot(0, self._apply_table_fit)
            # 图表期间 `_rebuild` 不量内容宽（表格隐藏）⇒ 回来补一次。
            QTimer.singleShot(0, self._table.refresh_content_widths)
        self._view_mode = mode
        self._btn_table_view.setChecked(mode == 0)
        self._btn_chart_view.setChecked(mode == 1)
        if mode == 1:
            self._chart_refresh()

    def _on_chart_columns_changed(self):
        """勾选列变了：图表视图下立即重画（数据已在 `_chart_rows`，不重取）。

        [为什么不重取] 全量取数是**按筛选条件**取的，与"画哪几列"无关；
        换列只是换画法。重取一遍要跑上百批请求，纯属浪费（板端尤其明显）。
        """
        if self._view_mode == 1 and self._chart_rows:
            self._chart_build(list(self._chart_rows), total=self._total)

    def _chart_filter_sig(self):
        """当前筛选条件的签名（时间窗 / 关键字 / 显示列）。

        [为什么要签名] 图表取数是"全量累加"，重复取一次要跑上百批请求；
        同一次筛选只该取一次。筛选项任一变化（时间/关键字/列）都必须重取，
        否则画的是旧筛选条件下的数据 —— 那是**错误数据**，比不画更糟。

        [为什么**不含**窗口锚点] `_window_end` 会被刷新/探活重新锚定成"现在"，
        把它算进签名会让签名每拍都变 ⇒ 每 3 秒重跑一次全量取数（实测踩到，
        状态行一直停在"正在加载图表数据…"）。图表自己冻结锚点
        （`_chart_anchor`，取数那一刻定下），故签名不需要它。
        """
        lo_ms, hi_ms = (self._server_time_range() or (None, None))
        kw = self._f_text.text().strip() or None
        return (lo_ms, hi_ms, kw, tuple(self.visible_columns()))

    def _chart_points_value(self):
        """当前选择的每线抽样点数（控件未建/非法 ⇒ 本机设置/缺省 100）。"""
        combo = getattr(self, "_chart_points", None)
        if combo is not None:
            v = combo.currentData()
            if v in self.CHART_POINT_CHOICES:
                return int(v)
        from . import settings as _ui_settings
        return _ui_settings.hist_chart_points()

    def _on_chart_points_changed(self, _idx):
        """抽样点数改档：落盘 + 作废已取样本并重新取全量。

        [取数期间下拉已禁用] 在途批次是按旧点数抽样的 ⇒ 中途换档会串样本，
        故不处理这种调用（正常情况下不可能触发）。
        """
        if self._chart_fetching:
            return
        from . import settings as _ui_settings
        value = self._chart_points_value()
        _ui_settings.set_value(_ui_settings.K_HIST_CHART_POINTS, int(value))
        self._chart_rows = []
        self._chart_sig = None
        self._chart_total = 0
        self._chart_scanned = 0
        self._chart_stride = 1
        if self._view_mode == 1:
            self._chart_status.setText("正在按每线 %d 点重新取数…" % value)
            self._chart_refresh()

    def _chart_refresh(self):
        """（重）建图表视图：需要时先按筛选全量取数（流式抽样），取完再画。

        [两条数据来源]
          · **服务端模式**：按当前筛选 **keyset 全量取数**（不限行数）——
            与导出同一条数据路径；取的过程中按「总行数/每线点数」
            的步长等距抽样，内存不随库行数涨。
          · **本地缓存模式**（未连接/服务端未落盘）：直接用内存里的采样
            缓冲画（`_filtered_rows()`），没有服务端可查。
        [用户口径 2026-11-24] 「按照筛选的结果，就是导出的全部数据平均采集
        其中的点数进行绘制折线图」——旧实现取到 12000 行就停（库更大时图表
        与导出看到的不是同一批数据）。
        """
        if self._data_mode != "server":
            self._chart_build(list(self._filtered_rows()), total=len(self._rows))
            return
        if self._chart_fetching:
            # 已在取数中：别用**半截数据**重画（_chart_rows 是边取边累加的，
            # 中途重建会让状态行/曲线都停在半路）。取完的收尾会自己 build。
            return
        sig = self._chart_filter_sig()
        if self._chart_rows and sig == self._chart_sig:
            # 筛选没变且已经取过：原地重画，不重跑上百批请求。
            self._chart_build(list(self._chart_rows), total=self._chart_total)
            return
        self._chart_sig = sig
        # [冻结锚点] 取数开始那一刻定下 before_ts，本批全量都用它：
        # 中途 `_window_end` 被刷新重锚定也不影响（否则签名/窗口每拍漂移）。
        self._chart_anchor = self._window_end
        self._chart_cursor = self._chart_anchor
        self._chart_rows = []
        self._chart_scanned = 0
        self._chart_stride = 1
        self._chart_total = 0
        self._chart_fetch_offset = 0
        self._chart_fetching = True
        # 取数期间禁改抽样档（在途批次按旧点数抽样，换了会串）。
        if hasattr(self, "_chart_points"):
            self._chart_points.setEnabled(False)
        self._chart_status.setText("正在加载图表数据…")
        self._chart_request_chunk()

    def _chart_request_chunk(self):
        """取图表全量数据的一批（keyset：offset 恒 0、beforeTs 递进）。

        [为什么不用 offset 翻页 2026-11-24] 与导出同因：库大时 OFFSET 每批
        都要从头扫过去，20 万行能拖到几十分钟；keyset 每批索引定位。
        """
        bridge = self.ctx.bridge
        cursor = self._chart_cursor if self._chart_cursor is not None \
            else self._chart_anchor
        # [图表走**独立通道** 2026-11-17] 图表批与表格页的应答**物理隔离**：
        # 两条应答各走各的信号（chartReady / chartFailed）⇒ 归属由"走哪个
        # 通道"决定，不再靠共享的 `_inflight` + `_pending_kind` 猜。
        # [原来为什么坏] 图表批 50 行 > 表格页 20 行 ⇒ 两条应答互相把对方
        # 的当自己的：图表批被判"短批"收尾（只画第一页），图表在途时翻页
        # 被静默吞掉。
        def _done(doc):
            self.chartReady.emit(doc)

        def _err(msg):
            self.chartFailed.emit(str(msg))

        try:
            lo_ms, hi_ms = (self._server_time_range() or (None, None))
            kw = self._f_text.text().strip() or None
            bridge.history_page(
                offset=0, limit=self.CHART_CHUNK,
                before_ts=cursor, lo_ts=lo_ms, hi_ts=hi_ms,
                kw=kw, kw_cols=self.visible_columns(),
                on_done=_done, on_error=_err)
        except Exception as exc:                        # noqa: BLE001
            self._chart_fetching = False
            self._chart_status.setText("图表取数失败：%s" % exc)
            self.ctx.set_status("图表取数失败：%s" % exc)

    def _on_chart_ready(self, doc):
        """一批图表数据到货（**chartReady 通道**）：边累加边抽样 → 继续取/收尾。

        [流式抽样 2026-11-24] 用户口径「导出的全部数据平均采集其中的点数」：
        先按第一帧的 total 算步长 `ceil(total / 每线点数)`，此后只把
        序号命中步长的行留下（+ 最后一行必留）⇒ 取全量但不把全量堆进内存。

        [keyset 游标] 服务端每批内部升序、批序最新在前 ⇒ 本批**最小 ts - 1**
        作为下一批的 beforeTs（与导出同一口径），offset 恒 0。

        [为什么独立成一个通道] 表格页的应答走 pageReady；图表批走 chartReady
        ⇒ 两条流不再共用 `_inflight` + `_pending_kind` 互相误认。
        """
        if not self._chart_fetching:
            return                       # 用户已切走/筛选已变，这批作废
        if not doc.get("ok"):
            self._chart_fetching = False
            if hasattr(self, "_chart_points"):
                self._chart_points.setEnabled(True)
            self._chart_status.setText("图表取数失败：%s"
                                       % (doc.get("err") or "服务端拒绝"))
            return
        cols = list(doc.get(proto.HIST_KEY_COLS) or proto.HISTORY_COLUMNS)
        rows = doc.get(proto.HIST_KEY_ROWS) or []
        total = int(doc.get(proto.HIST_KEY_TOTAL) or 0)
        if self._chart_total <= 0 and total > 0:
            self._chart_total = total
            # 全量 N 行 → 约「每线点数」行（floor 步长让采样数贴近上限，
            # 单列点数再由 `_sample_points` 精确夹到该值；内存 ≤ 2×上限）。
            self._chart_stride = max(
                1, total // max(1, self._chart_points_value()))
        base = self._chart_scanned
        kept_first = None
        first_batch = base == 0
        for i, row in enumerate(rows):
            idx = base + i
            # [首帧最新必留] 服务端批内**升序** ⇒ 第一帧的最后一个才是全库
            # 最新一行（按序号抽样可能正好跳过它，曲线右端会少一截）。
            if (self._chart_stride <= 1 or idx % self._chart_stride == 0
                    or (first_batch and i == len(rows) - 1)):
                self._chart_rows.append(self._server_row_texts(row, cols))
                if kept_first is None:
                    kept_first = idx
        self._chart_scanned = base + len(rows)

        # keyset 游标：本批最小 ts - 1（服务端批内升序）。
        stamps = []
        for row in rows:
            try:
                stamps.append(int(row[0]))
            except (TypeError, ValueError, IndexError):
                continue
        if stamps:
            self._chart_cursor = min(stamps) - 1

        got = len(rows)
        # [收尾判据] 短批 = 没有更多行了；或已取满 total（第一帧给的全量数）。
        done = (got < self.CHART_CHUNK
                or (self._chart_total > 0
                    and self._chart_scanned >= self._chart_total))
        if done:
            # 最旧一行必留：最后一帧批内升序 ⇒ 该帧**第一个**才是全库最旧；
            # 未被步长选中就补上（曲线左端要有起点）。
            oldest_idx = self._chart_scanned - len(rows)
            if (rows and self._chart_stride > 1
                    and kept_first != oldest_idx):
                self._chart_rows.append(self._server_row_texts(rows[0], cols))
            self._chart_fetching = False
            if hasattr(self, "_chart_points"):
                self._chart_points.setEnabled(True)
            self._chart_build(list(self._chart_rows), total=self._chart_total)
            return
        self._chart_status.setText("正在加载图表数据… 已取 %d/%d 行"
                                   % (self._chart_scanned, self._chart_total))
        self._chart_request_chunk()

    def _on_chart_failed(self, msg):
        """图表取数失败（**chartFailed 通道**）：只动图表，不动表格的总数。

        [为什么与表格失败分开] 表格那一页还是好的（图表是"另开一路取的全量"）
        ⇒ 不能让图表失败把表格的"读取失败"显示出来。
        """
        self._chart_fetching = False
        if hasattr(self, "_chart_points"):
            self._chart_points.setEnabled(True)
        self._chart_status.setText("图表取数失败：%s" % msg)
        self.ctx.set_status("图表取数失败：%s" % msg)

    def _chart_build(self, rows, total=0):
        """把（已取回的）全量行按勾选列画成折线并内嵌显示。

        [多列口径] 勾中的每一列各画一条线，颜色按 `LineChartDialog.
        SERIES_COLORS` 依次取、图例在标题下方。各列量纲不同（频率 Hz /
        电流 A / 浊度 NTU）时**不做双 Y 轴**：图是"看趋势"不是"读精确值"，
        每列各自归一化到绘图区高度（图例里标 min/max），否则小量纲的线
        会被压成一条直线。

        [等距抽样] 取数阶段已按「全量行数 / 每线点数」的步长流式
        抽样（首尾必留）；这里再按每条线的实际点数兜底抽一次（某列大量
        "--" 时行数与点数不同）。2000 点逐点 drawLine 在板端已够看趋势。
        """
        cols = [self.CHART_COLUMNS[i]
                for i in self._chart_col.checked_indexes()
                if i < len(self.CHART_COLUMNS)]
        if not cols:
            self._chart_status.setText("请先在「折线列」里勾选至少一列")
            self.ctx.set_status("请先在「折线列」里勾选至少一列")
            return
        # [显式按时间正序] 服务端批次顺序不作为画图前提：图表 X 轴必须
        # 旧→新，靠批序等于把正确性押在服务端 ORDER BY 上。
        ordered = sorted(rows, key=lambda item: (
            self._stamp_text_to_ms(item[0][0]) or 0))
        series = []
        limit = self._chart_points_value()
        raw_counts = []
        for col in cols:
            points = []
            for row, _abnormal in ordered:
                value = self._cell_float(row[col]) if col < len(row) else None
                if value is None:
                    continue
                stamp = self._stamp_text_to_ms(row[0])
                if stamp is None:
                    continue
                points.append((stamp, value))
            raw_counts.append(len(points))
            if len(points) >= 2:
                series.append((self.COLUMNS[col],
                               self._sample_points(points, limit)))
        if not series:
            self._chart_status.setText(
                "可画的数值点不足 2 个（换列或放宽筛选）")
            self.ctx.set_status("可画的数值点不足 2 个（换列或放宽筛选）")
            return

        if self._chart_dialog is None:
            dialog = LineChartDialog(series, self)
            # [内嵌] Qt.Widget 旗标让 QDialog 当普通控件进布局：表格/图表
            # 双视图在同一页内切换（页内 QStackedWidget），不弹窗。
            dialog.setWindowFlags(Qt.Widget)
            self._chart_holder.layout().addWidget(dialog)
            self._chart_dialog = dialog
        else:
            self._chart_dialog.set_series(series)

        if self._data_mode == "server":
            # [全量取数 + 流式抽样 2026-11-24] 状态行说清三件事：筛选命中多少、
            # 实际扫了多少行（= 全部，不再有 12000 截断）、画了多少行。
            scanned = self._chart_scanned or len(rows)
            self._chart_status.setText(
                "筛选共 %d 行 · 已取 %d 行 · 抽样 %d 行绘图（每线最多 %d 点）"
                % (total or scanned, scanned, len(rows), limit))
        else:
            peak = max(raw_counts) if raw_counts else 0
            self._chart_status.setText(
                "筛选共 %d 行 · 图表取回 %d 行 · 每线最多 %d 点（%s）"
                % (total or len(rows), len(rows), limit,
                   "已等距抽样" if peak > limit else "未抽样"))

    @staticmethod
    def _sample_points(points, limit):
        """等距抽样：把 n 个点压到 <= limit 个，且**首尾必留**。

        [为什么首尾必留] 曲线两端是"数据从哪开始/到哪结束"，抽样丢了端点
        会让用户以为那段时间没数据。
        """
        n = len(points)
        if limit <= 1 or n <= limit:
            return points
        step = (n - 1) / float(limit - 1)
        out = [points[int(round(i * step))] for i in range(limit)]
        out[-1] = points[-1]
        return out

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
        """导出**按当前筛选条件命中的全部行**为 CSV 或 Excel（流式、不限行数）。

        [口径：按筛选结果，不是当前表格那几十行]（用户口径 2026-11-11）
        历史表是**分页**的：界面上只有当前页那几十行，库里命中的是几千行。
        原实现只导当前页 ⇒ 导出的文件比用户以为的少一个数量级，且**看不出
        少了**。现在按「逐批连续取 → 边取边写」走全量（口径与 C 端
        HsfExportPlan 一致）。

        [keyset 分页 2026-11-17] 导出**不用 OFFSET**：那要每批从表头扫过
        offset 行（20 万行 + 关键字实测 66 分钟，绝大部分时间花在"跳行"）
        ⇒ 改成 **beforeTs 递进**：offset 恒 0，每批用上一批的**最小 ts - 1**
        作时间上界 ⇒ 每批都是索引定位，跨批不重不漏。

        [每批反转] 服务端每批内部**升序**（取页后 reversed）、批序是"最新批
        在前" ⇒ 直接追加会让时间列在块与块之间**倒跳**（Excel 直接画图出
        锯齿）⇒ 每批反转（最新在上，与界面表格同序）⇒ 全局严格降序，且仍然
        流式（零内存代价）。

        [不限行数] CSV 与 XLSX 都**边取边写** ⇒ 没有 12000 行的内存闸
        （旧实现 XLSX 攒齐所有行再一次性写，超出还会显式截断）。

        [格式由扩展名决定] 选 .csv → CSV（utf-8-sig，Excel 双击直开）；
        选 .xlsx → Excel（流式写出器；失败**显式报错**，不静默降级成 CSV
        ——"以为是 Excel 其实是 CSV"比直接报错更误事）。
        """
        # [权限闸在界面 + 服务端] 无权限直接提示（界面侧）；每一批都带
        # export=true，服务端还会再验一次 Download 权限。
        if not self.ctx.bridge.can(proto.PERM_DOWNLOAD):
            self.ctx.set_status("权限不足：下载数据需要普通用户及以上账户")
            return
        # [导出互斥 2026-11-16 审计] 上一轮还在跑时再点 ⇒ 两个导出交错写
        # 同一个文件。
        runner = getattr(self, "_export_runner", None)
        if runner is not None and getattr(runner, "running", False):
            self.ctx.set_status("正在导出中，请等当前导出完成（或取消）")
            return
        default = "hsf_history_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, _selected = QFileDialog.getSaveFileName(
            self, "导出历史数据", default,
            "Excel 工作簿 (*.xlsx);;CSV (*.csv)")
        if not path:
            return
        # [筛选条件定格] 导出发起那一刻的筛选/列/锚点全部冻结：取数期间
        # 用户改筛选或翻页，都不影响这一轮导出的内容（对齐 C 端 m_export*
        # 快照）。
        self._export_cols = list(self.visible_columns())
        self._export_kw = self._f_text.text().strip() or None
        self._export_lo, self._export_hi = (self._server_time_range()
                                            or (None, None))
        # [锚点] 整轮钉同一个 beforeTs：每批各取"当下"会让窗口上界逐批
        # 增长 ⇒ 顺序取必重复/漏行。页面还没有锚点时取"现在"定格。
        self._export_anchor = self._window_end or int(time.time() * 1000)
        self._export_path = path
        self._export_is_xlsx = path.lower().endswith(".xlsx")
        self._export_fallback = 0
        self._export_csv = None
        self._export_xlsx = None
        # [keyset 游标] 每一批的时间上界：起手是锚点，之后逐批用上一批的
        # 最小 ts - 1（减 1 毫秒是为了把那一行本身排除掉，否则重复一行）。
        self._export_cursor = self._export_anchor
        # [不限行数] max_rows=0 ⇒ 不截断（CSV 与 XLSX 都边取边写）。
        self._export_runner = BatchExport(keep_rows=False, max_rows=0)
        self.ctx.set_status("正在导出…")

        def _fetch(offset, limit, on_done, on_error):
            # [keyset] offset 恒 0；时间上界靠 beforeTs 递进（见方法头注）。
            self.ctx.bridge.history_page(
                offset=0, limit=limit,
                before_ts=self._export_cursor,
                lo_ts=self._export_lo, hi_ts=self._export_hi,
                kw=self._export_kw, kw_cols=self._export_cols,
                export_mode=True, on_done=on_done, on_error=on_error,
                timeout=15.0)

        def _rows_texts(raw_rows):
            """服务端原始行 → 显示文本行（转换异常的行按原值兜底，不丢行）。"""
            server_cols = list(proto.HISTORY_COLUMNS)
            out = []
            for raw in raw_rows:
                try:
                    out.append(self._server_row_texts(raw, server_cols))
                except Exception:                       # noqa: BLE001
                    # [不丢行] 转换异常的行按**原始值**兜底导出（行数必须与
                    # 命中数对上，少一行就是"导出不全"）；兜底了几行在状态
                    # 行注明——静默丢行最误事。
                    out.append(([("" if v is None else str(v))
                                  for v in raw], False))
                    self._export_fallback += 1
            return out

        def _advance_cursor(raw_rows):
            """把 keyset 游标推到本批**最小 ts - 1**（下一批不含本批任何行）。

            [为什么用原始行的 ts] 显示文本是格式化后的字符串，拿它算游标
            要再解析一遍时间（口径还可能与 ts 差一秒）⇒ 直接用原始整数。
            """
            stamps = []
            for raw in raw_rows:
                try:
                    stamps.append(int(raw[0]))
                except (TypeError, ValueError, IndexError):
                    continue
            if not stamps:
                return
            self._export_cursor = min(stamps) - 1

        def _on_rows(batch_raw):
            """每批到货：推进游标 → 反转 → 投影到冻结的列 → 边取边写。"""
            _advance_cursor(batch_raw)
            rows = list(reversed(_rows_texts(batch_raw)))
            cols = list(self._export_cols)
            values = [[texts[c] if c < len(texts) else "" for c in cols]
                      for texts, _abn in rows]
            if self._export_is_xlsx:
                if self._export_xlsx is None:
                    self._export_xlsx = XlsxStreamWriter(self._export_path,
                                                         ["历史数据"])
                    self._export_xlsx.sheet(0, [self.COLUMNS[c] for c in cols])
                self._export_xlsx.rows(0, values)
                return
            if self._export_csv is None:
                self._export_csv = CsvStreamWriter(
                    self._export_path, [self.COLUMNS[c] for c in cols])
            self._export_csv.write_rows(values)

        def _finished(total, truncated, count):
            write_error = ""
            try:
                if self._export_is_xlsx:
                    if self._export_xlsx is not None:
                        self._export_xlsx.close()
                        self._export_xlsx = None
                else:
                    self._export_close_csv()
            except XlsxStreamError as exc:
                # [失败优先 2026-11-17] 收尾失败（磁盘满/权限）必须**失败
                # 优先**：不能紧接着又写一句"已导出 N 条"（用户看到成功、
                # 文件却打不开）。
                write_error = str(exc)
            self.ctx.set_status(export_finish_text(
                count, total, truncated=truncated, write_error=write_error,
                fallback=self._export_fallback, path=self._export_path))

        def _failed(msg):
            self._export_close_csv()
            if self._export_xlsx is not None:
                try:
                    self._export_xlsx.abort()
                except Exception:                       # noqa: BLE001
                    pass
                self._export_xlsx = None
            self.ctx.set_status("导出失败：%s" % msg)

        def _progress(written, total):
            # [total 未知（导出模式服务端不数）时别写"共 0 行"] 2026-11-17
            if int(total or 0) <= 0:
                self.ctx.set_status("正在导出… 已取 %d 行" % written)
                return
            self.ctx.set_status("正在导出… 已取 %d 行 / 共 %d 行"
                                % (written, total))

        self._export_runner.start(fetch=_fetch, on_finished=_finished,
                                  on_error=_failed, on_rows=_on_rows,
                                  on_progress=_progress)

    def _export_close_csv(self):
        """关掉流式 CSV 文件（收尾；异常路径也要能关）。"""
        if self._export_csv is not None:
            self._export_csv.close()
            self._export_csv = None

    def _write_export_xlsx(self, path, rows):
        """把行集写成 XLSX（**流式写出器**：纯标准库，行数不限）。

        [为什么不用 xlsx_bridge] 旧实现走动态库（tools/xlsx_bridge）：库没编
        就导不出 Excel（现场"导不出"的常见原因）；流式写出器纯标准库、随时
        可用，且行数不限（旧实现"攒齐所有行再一次性写"还有 12000 行内存闸
        ⇒ 超出显式截断）。

        :returns: True=写成功；False=失败（原因已进状态行）。
        """
        cols = list(getattr(self, "_export_cols", None)
                    or self.visible_columns())
        headers = [self.COLUMNS[c] for c in cols]
        values = [[texts[c] if c < len(texts) else "" for c in cols]
                  for texts, _abn in rows]
        writer = None
        try:
            writer = XlsxStreamWriter(path, ["历史数据"])
            writer.sheet(0, headers)
            writer.rows(0, values)
            writer.close()
        except XlsxStreamError as exc:
            if writer is not None:
                try:
                    writer.abort()
                except Exception:                       # noqa: BLE001
                    pass
            self.ctx.set_status("导出失败：%s" % exc)
            return False
        except Exception as exc:                        # noqa: BLE001
            self.ctx.set_status("导出失败：%s: %s"
                                % (type(exc).__name__, exc))
            return False
        return True

    def _write_export(self, path, rows):
        """把（已授权的）行集写成 CSV / Excel（格式由扩展名决定）。

        [列用**冻结**的那份] 全量导出期间用户可能改了列显隐 ⇒ 写文件时
        必须用发起那一刻的列（self._export_cols），否则表头与行内容会
        错位（按新列取旧行的列）。
        """
        cols = list(getattr(self, "_export_cols", None)
                    or self.visible_columns())
        headers = [self.COLUMNS[c] for c in cols]
        if path.lower().endswith(".xlsx"):
            self._write_export_xlsx(path, rows)
            return
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

    def _export_authorized(self, path):
        """带 export 标记向服务端重取当前窗口，授权成功后写文件。

        [已被全量导出取代] 原实现只取**当前页**那几十行 ⇒ 与「导出按
        筛选全量」的口径冲突（见 _export_data）。保留此方法只为兼容外部
        调用（它仍是"取一页、带 export 帧"的最小实现）；页面上的导出按钮
        走 _export_data 的逐批全量路径。
        """
        bridge = self.ctx.bridge
        lo_ms, hi_ms = (self._server_time_range() or (None, None))
        kw = self._f_text.text().strip() or None

        def _done(doc):
            if not doc.get("ok"):
                self.ctx.set_status("导出被拒绝：%s"
                                    % (doc.get("err") or "未知"))
                return
            cols = list(doc.get(proto.HIST_KEY_COLS)
                        or proto.HISTORY_COLUMNS)
            rows = [self._server_row_texts(r, cols)
                    for r in (doc.get(proto.HIST_KEY_ROWS) or [])]
            self._write_export(path, rows)

        def _err(msg):
            self.ctx.set_status("导出请求失败：%s" % msg)

        bridge.history_page(
            offset=self._page_index * self._page_size,
            limit=self._page_size, before_ts=self._window_end,
            lo_ts=lo_ms, hi_ts=hi_ms, kw=kw,
            kw_cols=self.visible_columns(), export_mode=True,
            on_done=_done, on_error=_err)


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

    [X 轴带日期 2026-11-24] 跨度 ≥10 分钟时画两行（yyyy-MM-dd / HH:MM）；
    缩放细看到秒级时单行 HH:MM:SS。曲线**不画数据点圆点**（用户口径
    「也一样不要显示点」），单击气泡的单点标记保留。

    [交互（移动端手感 + 鼠标适配）]
      · **双指捏合**（QPinchGesture / 原生 TouchEvent）→ 横向缩放时间轴
      · **单指拖动**（触摸）/ **左键拖动**（鼠标）→ 横向平移
      · **滚轮**（鼠标）→ 以指针位置为锚点缩放（触摸屏没有滚轮，两者共存）
      · **双击** / 双击空处 → 复位到全程
      · **单击某点** → 弹出该点信息气泡（时间 + 各序列数值）；再点别处
        或移开就收起。拖动超过阈值不算单击（避免拖完手一松弹气泡）。

    [缩放只作用于时间轴] 纵向由每条线自己的 min/max 自适应，所以放大
    横轴时纵轴会自动跟着那一段的数据缩放 —— 这正是"看某段细节"想要的。

    [用法：内嵌，不弹窗] 历史页与报警页都把它以 `Qt.Widget` 旗标塞进
    页内的 `QStackedWidget`（表格/图表双视图），新数据到货用 `set_series`
    原地换数据（对齐 C 端 `alarmSetView` / `hsfDataHistorySetView`）。
    **不要再 `show()` 成独立窗口** —— 用户 2026-11-10 明确要求图表不新建
    窗口；需要对比两个时间段时，改用筛选条件多次切换视图。
    """

    #: 绘图区边距（左留 Y 轴刻度、下留两行 X 轴时间标签：日期 + 时刻）。
    MARGIN_L, MARGIN_T, MARGIN_R, MARGIN_B = 72, 66, 26, 60
    # [网格线颜色跟网页端同值 2026-11-17 用户「整体对齐 web 端的美化
    #  样式」] 网页端 --chart-grid = #D5DBE2（= 软描边色）；本端原
    #  #E3E6EB 更浅 ⇒ 白底上几乎看不见 ⇒ 图看着"没有网格"（两端
    #  结构其实一样：都有横向网格 + X 时刻刻度 + Y 量程刻度）。
    GRID_COLOR = "#D5DBE2"
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
        self.setWindowTitle("折线图")
        self.resize(920, 520)
        self.setMouseTracking(True)          # 鼠标移动收气泡/找最近点
        self.setCursor(Qt.CrossCursor)
        #: 拖动/捏合过程中的临时状态。
        self._drag_origin = None         # (x, view_x0, view_x1)
        self._dragged = False
        self._pinch_start = None         # (center_frac, view_x0, view_x1)
        self._press_pos = None
        #: [触摸轻点弹气泡 2026-11-24] 单指按下点 (x, y) / 是否移动过 / 是否多指。
        #: 触摸路径原来只处理捏合与平移，**抬手永远不弹点信息**（用户实报
        #: 「鼠标点击可以弹出悬浮窗，但是触摸点击弹不出」）。
        self._touch_tap = None
        self._touch_moved = False
        self._touch_multi = False
        self._series = []
        self._view_x0 = 0.0
        self._view_x1 = 1.0
        self._picked = None
        self.set_series(series)

        self.grabGesture(Qt.PinchGesture)
        self.setAttribute(Qt.WA_AcceptTouchEvents, True)

    def set_series(self, series):
        """替换数据源并重绘（标题/视口/选中点一并复位）。

        [独立成方法] 报警页的图表视图把本对话框以 Qt.Widget 旗标内嵌进
        页面（表格/图表双视图），新报警到货时**原地换数据**而不是重建
        对话框 —— 重建会闪一帧、还会丢掉用户已缩放/平移的视口。

        :param series: [(列名, [(时间文本, 数值), ...]), ...] 时间正序。
        """
        self.setWindowTitle("折线图 — %s"
                            % ("、".join(name for name, _pts in series)
                               if len(series) <= 3
                               else "%d 条曲线" % len(series)))
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
        self.update()

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
        # [年月日 2026-11-24 用户「时间怎么没年月日」] 历史数据跨天/跨月时
        # 只显示 HH:MM:SS 完全分不清是哪一天 ⇒ 跨度 ≥10 分钟时画**两行**：
        # 上行 yyyy-MM-dd、下行 HH:MM；秒级短窗口（缩放细看）仍单行 HH:MM:SS。
        t0, t1 = self.view_times()
        if t1 > t0:
            span_ms = t1 - t0
            two_line = span_ms >= 600000          # 10 分钟以上带日期
            for i in range(6):
                frac = i / 5.0
                stamp = t0 + (t1 - t0) * frac
                x = rect_l + plot_w * frac
                painter.setPen(QColor(self.TEXT_COLOR))
                # [两端不裁字] 中间刻度居中；首刻度左对齐、末刻度右对齐
                # （居中会让首/末标签各自的半宽伸出控件外被裁掉，
                # 实测末档 "2026-11-24" 只剩 "2026-11-2"）。
                if i == 0:
                    tx, tw = int(x), 160
                    align = Qt.AlignLeft | Qt.AlignVCenter
                elif i == 5:
                    tx, tw = int(x) - 160, 160
                    align = Qt.AlignRight | Qt.AlignVCenter
                else:
                    tx, tw = int(x) - 80, 160
                    align = Qt.AlignCenter
                if two_line:
                    date_txt = time.strftime(
                        "%Y-%m-%d", time.localtime(stamp / 1000.0))
                    time_txt = time.strftime(
                        "%H:%M", time.localtime(stamp / 1000.0))
                    painter.drawText(tx, rect_b + 6, tw, 14, align, date_txt)
                    painter.drawText(tx, rect_b + 22, tw, 14, align, time_txt)
                else:
                    painter.drawText(
                        tx, rect_b + 10, tw, 16, align,
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
                time.strftime("%Y-%m-%d %H:%M:%S",
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
        """画一条序列：视口裁剪 + 自适应纵向缩放 + **只画折线**。

        [不画数据点 2026-11-24 用户「也一样不要显示点」] 与实时趋势图同口径：
        点少时不再逐个画圆点（看着像一串珠子）；单击气泡的单点标记保留。
        """
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
        painter.setPen(QColor(theme.text_color()))
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

        [为什么整段兜 RuntimeError —— 2026-11-09] 本对话框是**非模态弹窗**
        （历史页）/ 内嵌控件（报警页），页面销毁/窗口关闭后 Qt 事件队列里
        可能仍压着发给它的事件；此时包装器的 C++ 对象已删除，再调
        QDialog.event(self, ...) 就抛
        "wrapped C/C++ object of type LineChartDialog has been deleted"。
        PyQt5 对**虚函数里未处理的 Python 异常**缺省直接 abort() 整个进程
        （sys.excepthook 只是被告知，拦不住 abort）⇒ 测试合到一个进程后
        在段落边界随机崩（2026-11-09 实测三连）。一个弹窗的残留事件不值
        一条命：C++ 已删就当事件没发生（返回 False），进程继续。
        """
        try:
            if event.type() == QEvent.Gesture:
                return self._on_gesture(event)
            if event.type() in (QEvent.TouchBegin, QEvent.TouchUpdate,
                                QEvent.TouchEnd):
                return self._on_touch(event)
            return QDialog.event(self, event)
        except RuntimeError:
            # 包装器的 C++ 对象已删除：事件是残留的，吞掉。
            return False

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

    def touch_begin(self, xs, ys=None):
        """触摸开始：`xs` 是落在绘图区内的手指横坐标列表（像素）。

        :param ys: 各手指的纵坐标（可选；给了才能判"轻点弹气泡"）。
        :returns: True=已按两指捏合处理，False=单指（进入平移/轻点跟踪）。

        抽成纯方法而不是写死事件里：触摸事件在离屏环境无法构造
        （PyQt 的 TouchPoint 不可实例化），但**这套状态机的正确性
        必须能测**——板端触摸屏是主战场，双指缩放捏错一点就是"手感怪"。
        """
        pts = []
        for i, x in enumerate(xs):
            if not self._in_plot_x(x):
                continue
            y = ys[i] if ys is not None and i < len(ys) else None
            pts.append((x, y))
        self._touch_moved = False
        self._touch_multi = len(pts) >= 2
        if len(pts) >= 2:
            self._pinch_start = self._make_pinch(pts[0][0], pts[1][0])
            self._touch_last_x = None
            self._touch_tap = None
            self._picked = None
            return True
        self._pinch_start = None
        self._touch_last_x = pts[0][0] if pts else None
        # [轻点候选] 只有拿到真实 y 才记（纯 x 的测试/旧调用不弹气泡）。
        self._touch_tap = pts[0] if pts and pts[0][1] is not None else None
        return False

    def touch_move(self, xs, ys=None):
        """触摸移动：两指按间距比例缩放、单指按位移平移。

        [轻点判定] 单指从按下点位移超过 `TAP_SLOP` 就算拖动 ⇒ 抬手不弹气泡
        （与鼠标拖动阈值同口径）。
        """
        pts = []
        for i, x in enumerate(xs):
            if not self._in_plot_x(x):
                continue
            y = ys[i] if ys is not None and i < len(ys) else None
            pts.append((x, y))
        inside = [p[0] for p in pts]
        if len(inside) >= 2:
            self._touch_multi = True
            self._touch_moved = True
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
            y = pts[0][1]
            if self._touch_tap is not None:
                sx, sy = self._touch_tap
                if (abs(x - sx) > self.TAP_SLOP
                        or (y is not None and sy is not None
                            and abs(y - sy) > self.TAP_SLOP)):
                    self._touch_moved = True
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
        """手指全部抬起：轻点（未移动/单指）⇒ 吸附最近点弹气泡。

        [为什么以前弹不出 2026-11-24 用户「鼠标点击可以弹出悬浮窗，但是
        触摸点击弹不出」] 触摸路径只接了捏合/平移，抬手时既不吸附也不设置
        `_picked` ⇒ 触摸永远没有点信息气泡。这里按鼠标单击同口径处理：
        移动没超过 `TAP_SLOP` 且非多指 ⇒ `nearest_point` 吸附；没点中
        则收起气泡（点空白处=取消）。
        """
        tap = None
        if (self._touch_tap is not None and not self._touch_moved
                and not self._touch_multi and self._pinch_start is None):
            tap = self._touch_tap
        self._pinch_start = None
        self._touch_last_x = None
        self._touch_tap = None
        self._touch_moved = False
        self._touch_multi = False
        if tap is not None:
            hit = self.nearest_point(QPoint(int(tap[0]), int(tap[1])))
            self._picked = (hit[0], hit[1], hit[2]) if hit else None
            self.update()

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
        [带 y 传入 2026-11-24] 轻点弹气泡要吸附最近点，只有 x 无法定位；
        TouchEnd 时 touchPoints() 可能已空 ⇒ 用 begin 记下的点收尾。
        """
        etype = event.type()
        pts = [(p.pos().x(), p.pos().y()) for p in event.touchPoints()]
        xs = [x for x, _y in pts]
        ys = [y for _x, y in pts]
        if etype == QEvent.TouchBegin:
            self.touch_begin(xs, ys)
        elif etype == QEvent.TouchUpdate:
            self.touch_move(xs, ys)
        elif etype in (QEvent.TouchEnd, QEvent.TouchCancel):
            self.touch_end()
        event.accept()
        return True


def _px(value):
    """QPainter 几何 API 只收整型；这里统一收缩（去 float 传参 TypeError）。"""
    return int(value)


class LogPage(Page):
    """日志：服务端运行日志分页查询（0x0119）+ 客户端运行日志折叠副视图。

    [数据源] 服务端 logs 表（关键运维事件定点入库：conn=会话接入/断开、
    ctrl=控制受理/拒绝、alarm=阀告警、auto=自动运行/模式切换、auth=登录
    成功/被拒），经 CMD_LOG_PAGE（proto.CMD_LOG_PAGE=0x0119）请求-应答
    拉取。能力对齐 C 端 pages/HsfDataLog.c。

    [结构（对齐 C HsfDataLog.c）]
      · 筛选行：类别下拉（全部/连接/控制/告警/自动/鉴权）+ 起止时间窗 +
        关键字 + 筛选/清除筛选（关键字回车=应用）；
      · 分页行：每页条数(10/20/50) + 上一页/下一页 + 页码 + 行数统计 + 刷新；
      · 工具行：导出 CSV + 「客户端运行日志」折叠开关；
      · 数据表：时间/类别/事件 3 列（DataTable，与历史页同款表格构件）。

    [窗口锚点（与 C logRequest 同口径）] 新查询（入页/筛选/换类别/改页
    大小/刷新）先把锚点置 0，发包前定格为本地"现在"再发送；翻页沿用同一
    锚点 ⇒ 整段分页钉在同一批数据上（logs 持续入库，不带锚翻页会被新行
    顶偏，相邻两页重复/漏行）。

    [应答归属（防串页，与历史页同机制）] 发包时登记 (offset, limit)，
    应答必须回显同值才铺表；对不上的迟到应答丢弃（翻页连点/筛选抢先都
    不会把旧页铺进新查询的表里）。

    [老服务端兼容] server_py 不实现 0x0119，会回 ok=false err="未知命令"
    ——状态行提示并保持空表，不崩。识别出"未知命令"后入页不再自动发（免
    得每切一次页就敲一次老服务端）；任何显式操作（筛选/翻页/刷新）仍照发
    ——重连到新服务端后自动恢复，无需重启客户端。ok=false 的其余 err
    （如"落盘未启用：服务端没有日志可查"）原样显示服务端原因。

    [导出 CSV] 复用本文件报警页/历史页的既有写法（csv.writer +
    utf-8-sig，Excel 双击直开）。PERM_DOWNLOAD 闸在客户端：0x0119 无
    export 键形态（C 端 HsfDataLog.c 注释：只读不涉密，服务端按读面
    ViewTelemetry 放行），下载权限只能由界面把守；导**当前页**（与 C 端
    "只导服务端返回的那一页"同口径）。

    [客户端运行日志（原有能力保留）] bridge.statusMessage 的收流视图折叠
    成页内副视图：勾选「客户端运行日志」展开，自动滚动/清空同原页。
    """

    #: 表头（3 列；与 C 端 kLogHeaders 同序同字面；CSV 导出共用）。
    COLUMNS = ["时间", "类别", "事件"]

    #: 类别下拉项：(入库字面, 显示名)。下标 0 = 全部（字面空串）。
    #: 前五档与 C 端 kLogCategoryKeys / kLogCategoryNames 同序同字面。
    #:
    #: [第 6 档 "sys"/系统 —— 2026-11-10] **Python 服务端专有**：C 端 logs
    #: 表只收 conn/ctrl/alarm/auto/auth 五类定点事件，而本端还收大量非事件
    #: 类业务日志（采集失败、健康变化、配置变更、装配保真行…），统一归 sys
    #: （见 server_py/hsfsrv/proto.py 的 LOG_CAT_SYS 说明）。
    #: 不带这一档的后果：日志页「类别」列对这类行显示原始字面 "sys"
    #: （用户截图反馈「中文」），而且筛不到。C 端也加了同档（C 服务端不产生
    #: sys 行 ⇒ 选中它只会得到空列表，不是报错）。
    CATEGORY_ITEMS = (
        ("", "全部类别"),
        ("conn", "连接"),
        ("ctrl", "控制"),
        ("alarm", "告警"),
        ("auto", "自动"),
        ("auth", "鉴权"),
        ("sys", "系统"),
    )
    #: 入库字面 → 显示名（铺表用；未知字面原样显示，服务端将来加类别
    #: 不至于显示空白）。
    CATEGORY_TEXT = {key: name for key, name in CATEGORY_ITEMS[1:]}

    #: 每页条数可选档（与 C 端 kLogPageSizes 同值；上限=服务端夹取 50）。
    PAGE_SIZES = (10, 20, 50)

    #: 老服务端"未知命令"否定应答的 err 字面（HsfServerApp.c 缺省分支）。
    UNKNOWN_CMD_ERR = "未知命令"

    #: 客户端运行日志缓冲上限（环形截断，原页同值）。
    MAX_LINES = 500

    #: 服务端分页应答 / 失败（网络线程 emit，UI 线程收 —— Qt 线程规则）。
    logReady = pyqtSignal(dict)
    logFailed = pyqtSignal(str)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self._head_row = self.add_header("日志", "服务端运行日志（连接/控制/告警/自动/鉴权）")
        self._lines = []

        # ---- 数据 / 分页状态 ----
        self._rows = []            # [[时间,类别,事件], ...] 当前页（应答序）
        self._page_size = proto.LOG_PAGE_DEFAULT
        self._offset = 0           # 已铺表的页起始偏移
        self._total = 0            # 本窗口命中总行数（分页判界按它）
        self._anchor_ts = 0        # 分页锚点 beforeTs（<=0 = 发包前取"现在"）
        self._lo_ms = 0            # 时间窗下界（<=0 不限；筛选按钮才更新）
        self._hi_ms = 0            # 时间窗上界（<=0 不限）
        self._category = ""        # 当前生效类别字面（空=全部）
        self._kw = ""              # 当前生效关键字（空=不筛）
        self._inflight = False     # 在途请求（同一时刻只允许一个）
        self._pending_offset = 0   # 在途请求的 offset（应答归属判定）
        self._pending_limit = 0    # 在途请求的 limit（应答归属判定）
        #: 老服务端已知不支持 0x0119（只拦 on_enter 的自动请求；显式操作
        #: 照发——重连到新服务端后自动恢复）。
        self._server_supported = True
        # ---- 全量导出状态（发起那一刻冻结的快照 + 逐批取数器）----
        self._export_cat = ""
        self._export_kw = ""
        self._export_lo = 0
        self._export_hi = 0
        self._export_anchor = 0
        self._export_path = ""
        self._export_cols = None       # 发起那一刻冻结的可见列（导出跟随）
        self._export_runner = None
        self._export_is_xlsx = False
        self._export_fallback = 0
        self._export_csv = None
        self.logReady.connect(self._on_log_ready)
        self.logFailed.connect(self._on_log_failed)

        # ---- 三行工具条（与历史/报警页共用 DataPageToolbar）----
        # [按页裁剪] 日志页无图表 ⇒ 没有折线列/双视图；日志**无自动刷新**
        # ⇒ 「滚动锁定」是死控件，不显示。类别走「页面特有下拉」槽，客户端
        # 运行日志开关走「页面附加控件」槽。补上历史页同款的「列多选」。
        self._tb = DataPageToolbar(
            self,
            columns=self.COLUMNS,
            extra_combo=("类别", [name for _key, name
                                  in self.CATEGORY_ITEMS]),
            page_sizes=self.PAGE_SIZES,
            paging=True, refresh=True, chart_view=False,
            freeze=False, export=True, state_key="log",
            fold_btn_host=self._head_row)
        # ---- 别名（保持既有属性名，既有页面逻辑与回归锁一行不改）----
        self._cat_combo = self._tb.extra_combo
        self._f_start = self._tb.start_edit
        self._f_end = self._tb.end_edit
        self._f_col = self._tb.column_combo
        self._f_text = self._tb.keyword_edit
        self._page_size_box = self._tb.page_size_box
        self._btn_prev = self._tb.btn_prev
        self._page_label = self._tb.page_label
        self._btn_next = self._tb.btn_next
        self._total_label = self._tb.total_label
        self._count_label = self._tb.count_label
        self._btn_reload = self._tb.btn_reload
        self._btn_export = self._tb.btn_export
        self._cat_combo.setToolTip(
            "按服务端入库类别过滤（conn/ctrl/alarm/auto/auth/sys）")
        # [本机记住每页条数 2026-11-10] 日志页与历史页各存一份。
        from . import settings as ui_settings
        saved_size = ui_settings.log_page_size()
        if saved_size in self.PAGE_SIZES:
            self._page_size = saved_size
        self._tb.set_page_size(self._page_size)
        # [折叠开关] 客户端运行日志是"客户端自己发生了什么"，与服务端事件
        # 是两个数据源；折叠成副视图后两边能力都不丢，一键切换。塞进共用
        # 工具条的「页面附加控件」槽（导出按钮之前）。
        self._client_toggle = QCheckBox("客户端运行日志")
        self._client_toggle.toggled.connect(self._toggle_client_log)
        self._tb.add_extra_widget(self._client_toggle)
        # ---- 信号接回页面原有的槽（行为不变）----
        self._tb.filterRequested.connect(self._apply_filter)
        self._tb.clearRequested.connect(self._clear_filter)
        self._tb.pageSizeChanged.connect(self._on_page_size)
        self._tb.prevRequested.connect(self._prev_page)
        self._tb.nextRequested.connect(self._next_page)
        self._tb.refreshRequested.connect(self._reload_current)
        self._tb.exportRequested.connect(self._export_data)
        self._tb.extraComboChanged.connect(self._on_category_changed)
        # [列勾选变化立即生效] 视图设置，不用点「筛选」。
        self._tb.columnsChanged.connect(self._on_columns_changed)
        self.root.addWidget(self._tb)
        # [折叠筛选区 ⇒ 表格吸收腾出的高度 2026-11-19 用户口径]
        # 折叠工具条后若没人吸收腾出的空间，表格下方会留一片空白 ⇒ 折叠
        # 白折；让表格改成可拉伸 ⇒ 一次多显示若干行（折叠的真正目的）。
        self._tb.collapsedChanged.connect(self.on_data_toolbar_collapsed)

        # ---- 服务端日志表卡片（筛选/分页条件先设，表格后看）----
        self._table = DataTable()
        self._table.set_headers(self.COLUMNS)
        # [列宽下限] 时间列装 "yyyy-MM-dd HH:MM:SS"（19 字符 ≈ 165px，
        # 历史页同款下限）；类别列装"全部类别"四字；事件列吃剩余宽度。
        self._table.set_column_weights((3, 2, 6),
                                       min_widths={0: 165, 1: 72, 2: 260})
        card = Card()
        card.add(self._table)
        self.root.addWidget(card, 1)
        # 表格吃满剩余视口（表头固定；折叠筛选条后由 on_data_toolbar_collapsed 重算）。
        self.fit_table_to_viewport(self._table)
        # 初始视图：全部列可见（构造期 selectionChanged 被跳过，这里补一次）。
        self._table.set_visible_columns(self.visible_columns())

        # ---- 客户端运行日志（折叠副视图；原页面的视图/自动滚动/清空原样保留）----
        self._client_card = Card("客户端运行日志")
        crow = QHBoxLayout()
        self._auto_scroll = QCheckBox("自动滚动")
        self._auto_scroll.setChecked(True)
        crow.addWidget(self._auto_scroll)
        crow.addStretch(1)
        clear_btn = QPushButton("清空")
        clear_btn.clicked.connect(self._clear)
        crow.addWidget(clear_btn)
        self._client_card.body.addLayout(crow)
        self._view = QPlainTextEdit()
        self._view.setReadOnly(True)
        self._view.setMaximumHeight(260)
        self._view.setStyleSheet(
            "QPlainTextEdit { background-color: #FFFFFF; border: 1px solid %s;"
            " border-radius: %dpx; font-family: monospace; font-size: 18px; }"
            % (theme.CLR_BORDER_SOFT, theme.RADIUS_CARD))
        self._client_card.add(self._view)
        self._client_card.setVisible(False)
        #: 副视图展开标志（append 判"攒还是刷"用；初始收起。2026-11-16）
        self._client_log_open = False
        self.root.addWidget(self._client_card)

        # 接管 bridge 的状态消息 → 客户端运行日志副视图（原有接线不变）。
        self.ctx.bridge.statusMessage.connect(self.append)
        # 关键字回车 = 应用筛选（共用控件只接了「筛选」按钮，回车是日志页
        # 原有便捷操作，保留）。
        self._f_text.returnPressed.connect(self._apply_filter)

    # ==================================================================
    #                 服务端查询：入页 / 请求 / 应答
    # ==================================================================
    def on_enter(self):
        """入页：拉第 1 页服务端运行日志（重数据入页才取；C 端建页即发）。"""
        if not self._server_supported:
            # 已知老服务端回"未知命令"：不再自动敲它，保持提示。
            self._show_unsupported()
            return
        self._anchor_ts = 0          # 新查询：重取"现在"作锚
        self._request(0)

    def _request(self, offset):
        """向服务端要一页（offset 为该页起点；窗口/类别/关键字随包下发）。

        [锚点] 与 C logRequest 同口径：锚点 <=0（新查询）先定格为本地
        "现在"再发包；翻页沿用 ⇒ 整段分页钉在同一批数据上。
        [归属登记] 发包前登记 (offset, limit)；应答回显同值才铺表
        （_on_log_ready），迟到的旧应答丢弃——翻页连点不会叠出乱序页。
        """
        if self._inflight:
            return
        bridge = self.ctx.bridge
        if not (bridge.is_connected() if hasattr(bridge, "is_connected")
                else False):
            self._show_failed("服务端未连接")
            return
        if offset < 0:
            offset = 0
        if self._page_size <= 0:
            self._page_size = proto.LOG_PAGE_DEFAULT
        if self._anchor_ts <= 0:
            self._anchor_ts = int(time.time() * 1000)
        self._inflight = True
        self._pending_offset = offset
        self._pending_limit = self._page_size

        def _done(doc):
            # 网络线程 → 信号排队回 UI 线程（Qt 线程规则，不可省）。
            self.logReady.emit(doc)

        def _err(msg):
            self.logFailed.emit(str(msg))

        try:
            bridge.log_page(
                offset=offset, limit=self._page_size,
                before_ts=self._anchor_ts,
                lo_ts=self._lo_ms or None, hi_ts=self._hi_ms or None,
                category=self._category or None, kw=self._kw or None,
                on_done=_done, on_error=_err)
        except Exception as exc:                        # noqa: BLE001
            self._inflight = False
            self._show_failed("%s: %s" % (type(exc).__name__, exc))

    def _on_log_ready(self, doc):
        """应答到货（**UI 线程**）：归属判定 → 铺表 / 显示服务端原因。"""
        if not isinstance(doc, dict):
            return
        if not doc.get("ok"):
            self._inflight = False
            err = str(doc.get("err") or "服务端拒绝")
            if self.UNKNOWN_CMD_ERR in err:
                # 老服务端不认 0x0119：提示 + 空表，不崩。
                self._server_supported = False
                self._show_unsupported()
            else:
                # 服务端明确拒绝（如"落盘未启用：服务端没有日志可查"）：
                # 显示原因并清表（C logFillTable 否定分支同口径）。
                self._rows = []
                self._total = 0
                self._fill_table()
                self._total_label.setText("查询失败：%s" % err)
                self.ctx.set_status("运行日志查询失败：%s" % err)
            return
        # [应答归属] offset/limit 必须回显我们发出的值，否则是迟到的旧
        # 应答（翻页连点/筛选抢先）——丢弃，绝不铺表（防串页）；在途闸
        # 也不动（本包应答还没回来，别提前放行下一个请求）。
        try:
            r_offset = int(doc.get(proto.KEY_LOG_OFFSET, -1))
            r_limit = int(doc.get(proto.KEY_LOG_LIMIT, -1))
        except (TypeError, ValueError):
            return
        if r_offset != self._pending_offset or r_limit != self._pending_limit:
            return
        self._inflight = False
        self._server_supported = True
        self._offset = self._pending_offset
        self._total = int(doc.get(proto.KEY_LOG_TOTAL) or 0)
        rows = doc.get(proto.KEY_LOG_ROWS) or []
        self._rows = [self._row_texts(r) for r in rows if isinstance(r, dict)]
        self._fill_table()

    def _on_log_failed(self, msg):
        """传输失败（超时/断链）：状态行说明，表格保持上一页内容。"""
        self._inflight = False
        self._show_failed(str(msg))

    def _show_failed(self, msg):
        """把失败原因顶到行数统计位 + 状态栏（别让页面停在"读取中"）。"""
        self._total_label.setText("查询失败：%s" % msg)
        self._btn_prev.setEnabled(False)
        self._btn_next.setEnabled(False)
        self.ctx.set_status("运行日志：%s" % msg)

    def _show_unsupported(self):
        """老服务端不认 0x0119：明确提示并保持空表（不崩、不假装有数据）。"""
        self._rows = []
        self._total = 0
        self._fill_table()
        self._total_label.setText(
            "服务端不支持运行日志查询（0x0119），请升级服务端")
        self.ctx.set_status(
            "运行日志：服务端不支持该命令（未知命令），请升级服务端")

    @classmethod
    def _row_texts(cls, row):
        """服务端一行对象 {ts,category,text} → [时间, 类别, 事件] 显示文本。

        [类别显示名] 入库字面（conn/ctrl/…）翻成中文（与下拉同源表），
        未知字面原样显示。
        """
        try:
            ts = int(row.get(proto.KEY_LOG_TS) or 0)
        except (TypeError, ValueError):
            ts = 0
        stamp = (time.strftime("%Y-%m-%d %H:%M:%S",
                               time.localtime(ts / 1000.0))
                 if ts > 0 else "--")
        category = str(row.get(proto.KEY_LOG_CATEGORY) or "")
        return [stamp, cls.CATEGORY_TEXT.get(category, category) or "--",
                str(row.get(proto.KEY_LOG_TEXT) or "")]

    def _fill_table(self):
        """铺当前页 + 刷新页码/行数统计/翻页钮（对齐 C logFillTable）。"""
        table = self._table.table
        table.setRowCount(len(self._rows))
        for r, row in enumerate(self._rows):
            for c, text in enumerate(row):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    # 时间/类别居中，事件左对齐（长文本从左读）。
                    item.setTextAlignment(
                        (Qt.AlignLeft | Qt.AlignVCenter) if c == 2
                        else Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(text)
        self._update_pager_ui()

    # ---- 列显示（多选；与历史页同款，导出跟随可见列）----

    def visible_columns(self):
        """当前显示（勾中）的列下标列表，升序；**至少一列**。

        数据行始终是完整 3 列（按 COLUMNS 固定顺序存），显示列只是"看不看"
        的问题；铺表用 set_visible_columns，导出按这里的列集合取。
        """
        if not hasattr(self, "_f_col") or self._f_col is None:
            return list(range(len(self.COLUMNS)))
        cols = sorted(self._f_col.checked_indexes())
        return cols or list(range(len(self.COLUMNS)))

    def _on_columns_changed(self):
        """列勾选变化：立即重设可见列（视图设置，不用点「筛选」）。"""
        if not hasattr(self, "_table"):
            return                          # 构造期早到的信号，__init__ 末尾补
        cols = self.visible_columns()
        self._table.set_visible_columns(cols)
        self.ctx.set_status("显示 %d/%d 列：%s"
                            % (len(cols), len(self.COLUMNS),
                               "、".join(self.COLUMNS[c] for c in cols)))

    # ---- 分页操作 ----

    def _page_count(self):
        if self._page_size <= 0:
            return 0
        return max(1, (self._total + self._page_size - 1) // self._page_size)

    def _update_pager_ui(self):
        """页码 / 行数统计 / 翻页钮使能（对齐 C logFillTable 状态行口径）。"""
        pages = self._page_count()
        cur = (self._offset // self._page_size + 1) if self._page_size > 0 \
            else 1
        self._page_label.setText("第 %d/%d 页" % (cur, max(1, pages)))
        shown = len(self._rows)
        self._total_label.setText("共 %d 条（本页 %d）%s"
                                  % (self._total, shown,
                                     "" if shown else "（无数据）"))
        # [计数标签放分页行] 与历史页同款「本页显示 K 行」口径。
        self._count_label.setText("本页显示 %d 行" % shown)
        self._btn_prev.setEnabled(self._offset > 0)
        self._btn_next.setEnabled(self._offset + shown < self._total)

    def _prev_page(self):
        """上一页（翻页沿用当前锚点，不发新查询）。"""
        if self._inflight or self._page_size <= 0 or self._offset <= 0:
            return
        self._request(self._offset - self._page_size)

    def _next_page(self):
        """下一页（判界用已铺页快照，与 C logNextSlot 同口径）。"""
        if self._inflight or self._page_size <= 0 or not self._rows:
            return
        if self._offset + len(self._rows) >= self._total:
            return
        self._request(self._offset + self._page_size)

    def _on_page_size(self):
        """改每页条数：回第 1 页 + 锚点归零（行号口径变了，停在原页号没
        意义；对齐 C logPageSizeSlot）。"""
        size = self._page_size_box.currentData()
        if not isinstance(size, int) or size <= 0 or size == self._page_size:
            return
        self._page_size = size
        # 记住本机偏好（日志页）
        from . import settings as ui_settings
        ui_settings.set_value(ui_settings.K_LOG_PAGE_SIZE, int(size))
        self._anchor_ts = 0
        self._request(0)

    def _reload_current(self):
        """手动刷新：重取当前页 + 锚点归零（对齐 C Reload 分支——重新加载
        想看的是"此刻"的状况，offset 不变、锚点重取）。"""
        self._anchor_ts = 0
        self._request(self._offset)

    # ---- 筛选（类别立即生效；时间窗/关键字按「筛选」）----

    def _time_window_ms(self):
        """筛选框文本 → (lo_ms, hi_ms)；两侧都空 = (None, None) 不限。

        归一化/换算复用历史页 _norm_stamp/_stamp_to_ms（报警页同款）。
        """
        lo = HistoryPage._norm_stamp(self._f_start.text(), is_end=False) \
            if self._f_start.text().strip() else None
        hi = HistoryPage._norm_stamp(self._f_end.text(), is_end=True) \
            if self._f_end.text().strip() else None
        if lo is None and hi is None:
            return None, None
        return HistoryPage._stamp_to_ms(lo), HistoryPage._stamp_to_ms(hi)

    def _apply_filter(self):
        """应用时间窗 + 关键字（对齐 C logApplyFilter：回第 1 页、锚点归零）。

        [类别不在此处] 类别下拉是**立即生效**的（_on_category_changed）；
        点筛选时以当前下拉值为准一并下发（同 C：筛选只改时间窗与关键字）。
        """
        raw_start = self._f_start.text().strip()
        raw_end = self._f_end.text().strip()
        if raw_start and HistoryPage._norm_stamp(raw_start, is_end=False) \
                is None:
            self.ctx.set_status("起始时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        if raw_end and HistoryPage._norm_stamp(raw_end, is_end=True) is None:
            self.ctx.set_status("结束时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        lo_ms, hi_ms = self._time_window_ms()
        if lo_ms and hi_ms and lo_ms > hi_ms:
            self.ctx.set_status("起始时间晚于结束时间，未筛选")
            return
        self._lo_ms = lo_ms or 0
        self._hi_ms = hi_ms or 0
        self._kw = self._f_text.text().strip()
        self._anchor_ts = 0          # 新查询：重取"现在"作锚
        self._request(0)

    def _on_category_changed(self, index):
        """类别下拉（立即生效，对齐 C logBarAction 的 ChannelChanged 分支）：
        换类别 = 新查询（锚点归零、回第 1 页）；时间窗/关键字保持。"""
        if index < 0 or index >= len(self.CATEGORY_ITEMS):
            index = 0
        self._category = self.CATEGORY_ITEMS[index][0]
        self._anchor_ts = 0
        self._request(0)

    def _clear_filter(self):
        """清除筛选：清时间窗 + 关键字，回第 1 页；**类别保持**（对齐 C
        logClearFilter：类别是"看哪一类"的视图设置，不随清除复位）。"""
        self._lo_ms = 0
        self._hi_ms = 0
        self._kw = ""
        self._f_start.clear()
        self._f_end.clear()
        self._f_text.clear()
        self._anchor_ts = 0
        self._request(0)

    # ---- 导出 CSV（PERM_DOWNLOAD 客户端闸）----

    def _export_data(self):
        """导出**按当前筛选条件命中的全部行**为 CSV（utf-8-sig，Excel
        双击直开）。

        [口径：按筛选结果，不是当前表格那几十行]（用户口径 2026-11-11）
        日志表是**分页**的：界面上只有当前页那几十行，库里命中的是几千
        行。原实现只导当前页 ⇒ 导出的文件比用户以为的少一个数量级，且
        **看不出来少了**。现在按「逐批连续取 → 收尾一次落盘」走全量
        （口径与 C 端 HsfExportPlan 一致）。

        [权限闸在客户端] 0x0119 服务端按只读面放行（无 export 键可验），
        下载权限只能由界面把守——无 Download 权限直接提示、不落盘
        （对齐 C logExportStart 的 HSF_PERM_DOWNLOAD 闸与拒绝文案）。
        """
        if not self.ctx.bridge.can(proto.PERM_DOWNLOAD):
            self.ctx.set_status("权限不足：下载数据需要普通用户及以上账户")
            return
        # [导出互斥 2026-11-16 审计] 同历史页：上一轮还在跑时再点 ⇒ 两个
        # 导出交错写同一个文件。
        runner = getattr(self, "_export_runner", None)
        if runner is not None and getattr(runner, "running", False):
            self.ctx.set_status("正在导出中，请等当前导出完成（或取消）")
            return
        default = "hsf_log_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, _selected = QFileDialog.getSaveFileName(
            self, "导出运行日志", default, "CSV (*.csv)")
        if not path:
            return
        # [筛选条件定格] 类别 / 关键字 / 时间窗 / 锚点在发起那一刻冻结：
        # 取数期间用户改筛选或翻页，都不影响这一轮导出的内容。
        self._export_cat = self._category
        self._export_kw = self._kw
        self._export_lo = self._lo_ms
        self._export_hi = self._hi_ms
        self._export_anchor = self._anchor_ts or int(time.time() * 1000)
        self._export_path = path
        # [列也定格] 导出内容 = 界面看到的样子（用户口径「导出按筛选结果」），
        # 取数期间改列显隐不影响这一轮；表头与行内容用同一份列集合才不错位。
        self._export_cols = list(self.visible_columns())
        # [流式写] 日志只出 CSV ⇒ 边取边写，不限行数（库里可能几万行）。
        self._export_csv = None
        self._export_runner = BatchExport(keep_rows=False, max_rows=0)
        self.ctx.set_status("正在导出…")

        def _fetch(offset, limit, on_done, on_error):
            self.ctx.bridge.log_page(
                offset=offset, limit=limit,
                before_ts=self._export_anchor,
                lo_ts=self._export_lo or None, hi_ts=self._export_hi or None,
                category=self._export_cat or None,
                kw=self._export_kw or None,
                on_done=on_done, on_error=on_error, timeout=15.0)

        def _on_rows(batch_raw):
            """每批到货：转成显示文本、按**冻结的可见列**直接落盘（不攒内存）。"""
            cols = list(self._export_cols or self.visible_columns())
            # [每批反转 2026-11-17] 同上：服务端每批升序、批序倒序 ⇒ 反转后
            # 文件时间列全局降序（与界面一致），且仍然流式。
            rows = list(reversed([self._row_texts(r) for r in batch_raw
                                  if isinstance(r, dict)]))
            if self._export_csv is None:
                self._export_csv = CsvStreamWriter(
                    self._export_path, [self.COLUMNS[c] for c in cols])
            self._export_csv.write_rows(
                [[texts[c] if c < len(texts) else "" for c in cols]
                 for texts in rows])

        def _finished(total, truncated, count):
            if self._export_csv is not None:
                self._export_csv.close()
                self._export_csv = None
            # 同历史页：先关文件再出文案；文案口径统一走纯函数（0 行/少行）。
            self.ctx.set_status(export_finish_text(
                count, total, truncated=truncated, path=self._export_path))

        def _failed(msg):
            if self._export_csv is not None:
                self._export_csv.close()
                self._export_csv = None
            self.ctx.set_status("导出失败：%s" % msg)

        def _progress(written, total):
            # [total 未知（导出模式服务端不数）时别写"共 0 行"] 2026-11-17
            if int(total or 0) <= 0:
                self.ctx.set_status("正在导出… 已取 %d 行" % written)
                return
            self.ctx.set_status("正在导出… 已取 %d 行 / 共 %d 行"
                                % (written, total))

        self._export_runner.start(fetch=_fetch, on_finished=_finished,
                                  on_error=_failed, on_rows=_on_rows,
                                  on_progress=_progress)

    def _write_export(self, path, rows):
        """把（已授权的）行集写成 CSV（utf-8-sig；**导出列跟随可见列**）。

        独立成方法：无头测试直接调这里。列集合取发起那一刻冻结的
        `_export_cols`（没有就用当前可见列）⇒ 表头与行内容同口径、不错位。
        """
        cols = list(getattr(self, "_export_cols", None)
                    or self.visible_columns())
        headers = [self.COLUMNS[c] for c in cols]
        if not path.lower().endswith(".csv"):
            path += ".csv"
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow(headers)
            for texts in rows:
                writer.writerow([texts[c] if c < len(texts) else ""
                                 for c in cols])
        # [上限要如实说 2026-11-17] 报警记录是**客户端内存**里的环形缓冲
        # （MAX_ROWS 条，超出丢最旧），服务端 alarms 表没有查询命令 ⇒ 导出
        # 的"全集"最多就是内存里这些。达到上限时明确提示，别让用户以为
        # 导出了历史上全部报警。
        # [只有报警页有内存环形 2026-11-17] `_write_export` 是报警页和日志页
        # **同名**方法（日志页直接写当前批，没有 `_records`）⇒ 用 getattr
        # 双保险：哪个属性不在就不加提示（本用例自己踩过：日志页导出
        # AttributeError）。
        note = ""
        _records = getattr(self, "_records", None)
        _max_rows = getattr(self, "MAX_ROWS", 0)
        if _records is not None and _max_rows and len(_records) >= _max_rows:
            note = ("（仅本机内存中最近 %d 条；更早的报警未在客户端留存）"
                    % _max_rows)
        self.ctx.set_status("已导出 %d 条 → %s%s"
                            % (len(rows), path, note))

    # ==================================================================
    #        客户端运行日志（折叠副视图；原有能力原样保留）
    # ==================================================================
    def _toggle_client_log(self, checked):
        """勾选展开 / 收起客户端运行日志副视图（一键切换，内容不清零）。

        [展开时补铺 2026-11-16 审计] 收起期间 append 只攒不刷（省 CPU）
        ⇒ 展开这一刻要把攒下的整段铺一次。显式标志见 append 的说明。
        """
        self._client_log_open = bool(checked)
        self._client_card.setVisible(bool(checked))
        if checked:
            self._view.setPlainText("\n".join(self._lines))
            if self._auto_scroll.isChecked():
                self._view.verticalScrollBar().setValue(
                    self._view.verticalScrollBar().maximum())

    def append(self, text):
        """bridge 状态消息收流进客户端运行日志（原 LogPage.append 保留）。

        [收起时只攒不刷 2026-11-16 审计] 副视图收起时每条消息仍然
        `setPlainText(最多 500 行)` + 滚动 ⇒ 某页面异常刷屏（200ms 一条
        "刷新失败"）时，**隐藏中的日志页也在每条重建大文本**，CPU 白烧。
        ⇒ 收起只攒 `self._lines`，展开（_toggle_client_log）时一次性铺。
        [为什么用标志不用 isVisible] 测试/离屏环境下页面本身没 show ⇒
        `isVisible()` 恒 False ⇒ 判据失效（I3 实测抓回）；收起/展开是
        本页自己的状态 ⇒ 用显式标志。
        """
        stamp = time.strftime("%H:%M:%S")
        self._lines.append("[%s] %s" % (stamp, text))
        if len(self._lines) > self.MAX_LINES:
            self._lines = self._lines[-self.MAX_LINES:]
        if not getattr(self, "_client_log_open", False):
            return
        self._view.setPlainText("\n".join(self._lines))
        if self._auto_scroll.isChecked():
            self._view.verticalScrollBar().setValue(
                self._view.verticalScrollBar().maximum())

    def _clear(self):
        """清空客户端运行日志缓冲与视图（原 _clear 保留）。"""
        self._lines = []
        self._view.clear()


class AlarmPage(Page):
    """报警：**服务端报警历史**（0x011A）单表。

    [2026-11-17 用户定案「本地不留了，全问服务端要」] 报警记录原先**各端各
    算各的进程内副本**（客户端算健康边沿、网页端也算一份）⇒ ① 客户端一重启
    历史全丢 ② 两端条数天然对不上（用户实报「web 端报警有数据，客户端报警
    视图没数据」）。⇒ 边沿改由**服务端**记（alarms 表：健康异常/恢复 + 自动
    运行告警 + 阀告警 + 流量计/保护告警），客户端与网页端都取**同一份**
    ⇒ 单一数据源、客户端重启不丢、两端口径一致。
    ⇒ 本页**只**认服务端的数据源：本地没有边沿检测、没有自动告警缓冲、
    也没有"两套并行 + 去重"那套逻辑。

    [表结构] 时间 / 设备 / 类型 / 状态 四列；服务端给出来源类别
    （health/auto/valve/…）与设备类别（KIND_*）⇒ 类型列显示**设备类别**
    （与网页端逐字一致）；自动告警行的类型列为「自动运行」、设备列为告警原文。

    [工具条（2026-11-11 与历史/日志页统一为 DataPageToolbar）]
      三行形态照历史页：筛选行（类型 + 起止时间窗 + 列多选 + 关键字 +
      筛选/清除筛选）、分页行（每页 + 上下页 + 页码 + 统计）、视图行
      （滚动锁定 + 导出数据）。**不显示图表**：用户定案「日志和报警都不要
      图表」，原「报警次数/分钟」折线图与表格/图表双视图已整体去掉。
      **隐藏「刷新」**：报警数据来自服务端推送，没有"重查"动作，留个点了
      没反应的按钮就是死控件（项目铁律：不许留占位/死控件）。

    [筛选（对齐历史页）]
      · **类型下拉**：全部类型 + 8 类设备（与 model.KIND_TEXT 同源，即
        C 端 alarmRowVisible 的类型集合）+ 尾项「自动运行」（自动告警行
        的类型列文本，全等匹配）；选中类型后只显示该类型行。
      · **起止时间窗**：每条记录带 epoch 毫秒时刻，`ts < 起点` 或
        `ts > 终点` 的行不显示；两侧留空 = 不限；起点晚于终点提示不应用。
      · **关键字**：客户端筛（设备 / 类型 / 状态 文本包含，空=不限）。
      · **列多选**：勾中的列才显示（默认全勾，至少留一列）；导出跟随可见列。
      · 点「筛选」应用（回第 1 页）；「清除筛选」把类型回全部、时间窗回
        数据全区间、关键字清空、列回全勾（对齐 C alarmClearFilter）。

    [客户端内部分页（2026-11-11）] 报警记录整份在内存（服务端取回），
    切开成每页 10/20/50 条：表格只铺当前页，`_visible_records()` 仍是
    **筛选命中的全集** ⇒ 导出、统计都按全集，不受当前页影响。

    [滚动锁定] 勾上后新报警只入库、不刷表（后台照常收）；取消勾选补刷一次，
    与历史页同款语义。

    [CSV 导出] 需 Download 权限；导出**当前筛选命中行的全量**（不是当前页
    那几十行，用户口径 2026-11-11），utf-8-sig（Excel 双击直开），按可见列。

    [环形截断] 保留 MAX_ROWS 上限，超出丢最旧（与 C ALARM_MAX_ROWS 同量级）。
    """

    #: 本页需要的推送主题。
    #: [2026-11-17] 报警数据**一律向服务端要**（0x011A）⇒ 本页不再需要
    #: 健康/自动告警的推送订阅（TOPIC_AUTO_ALARM 也不再常驻订阅）。
    TOPICS = ()

    MAX_ROWS = 200

    #: 自动运行告警的类型列文案（与类型下拉尾项逐字一致，筛选按全等匹配；
    #: 对齐 C HsfDataAlarm.c ALARM_KIND_TEXT_AUTO）。
    KIND_TEXT_AUTO = "自动运行"

    #: 自动运行告警类型（proto.AUTO_ALARM_*）→ 中文文案。前 7 类与 C 端
    #: HsfDataAlarm.c alarmAutoKindText 逐字一致；8~12（压差/电箱/电机/
    #: 压力1/2）是 PY 端运行保护扩展（C 栈暂不使用），号段见 proto。
    #: 未知类型回退「自动运行」，不显示裸数字。
    AUTO_KIND_TEXTS = {
        proto.AUTO_ALARM_ACTION_FAILED: "动作失败",
        proto.AUTO_ALARM_DEVICE_UNAVAILABLE: "设备不可用",
        proto.AUTO_ALARM_CUSTOM: "自定义",
        proto.AUTO_ALARM_ESTOP: "急停",
        proto.AUTO_ALARM_VALVE_TIMEOUT: "阀超时",
        # [流量计报警 2026-11-15] 上限/下限/空管/系统 + 进水泵空管保护动作。
        proto.AUTO_ALARM_FLOWMETER: "流量计",
        # [设备故障 2026-11-15] 变频器故障等：进报警历史表 + 锁存 + 停机。
        proto.AUTO_ALARM_DEVICE_FAULT: "设备故障",
        # [压差过高 2026-11-15] 运行保护的压差过大动作。
        proto.AUTO_ALARM_PRESS_HIGH: "压差过高",
        # [2026-11-25 运行保护扩展] 电箱超温 / 电机超温（原误用 8，已订正
        # 为 10）/ 压力1过高 / 压力2过高。
        proto.AUTO_ALARM_BOX_OVER_TEMP: "电箱超温",
        proto.AUTO_ALARM_MOTOR_OVER_TEMP: "电机超温",
        proto.AUTO_ALARM_PRESS1_HIGH: proto.PRESS1_LABEL + "过高",
        proto.AUTO_ALARM_PRESS2_HIGH: proto.PRESS2_LABEL + "过高",
    }

    #: 类型下拉项（下标 0 = 全部类型；1..8 与 model.KIND_TEXT 顺序一致，
    #: 对齐 C HsfDataAlarm.c kKinds[8]；**尾项 = 「自动运行」**，对齐 C
    #: ALARM_KIND_FILTER_AUTO=9——自动告警行的类型列存的就是这个文本，
    #: 筛选按全等匹配能单选这一类）。
    KIND_ITEMS = (("全部类型",)
                  + tuple(model_mod.KIND_TEXT[k] for k in range(8))
                  + (KIND_TEXT_AUTO,))

    COLUMNS = ["时间", "设备", "类型", "状态"]

    #: 客户端内部分页档位（与历史/日志页同款）。
    PAGE_SIZES = (10, 20, 50)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self._head_row = self.add_header("报警", "设备异常记录（服务端报警历史 + 客户端边沿）")
        #: [(ts_ms, [时间, 设备, 类型, 状态]), ...] 旧→新（ts 为 epoch 毫秒，
        #: 时间窗筛选要用；显示文本只有 HH:MM:SS，与 C 同口径）。
        #: **服务端报警历史**（0x011A 取回）—— 本页**唯一**数据源。
        #: [2026-11-17 用户定案「本地不留了，全问服务端要」] 报警历史一律
        #: 由服务端记（alarms 表），客户端与网页端都取**同一份** ⇒ 客户端
        #: 重启不丢、两端条数一致；本地没有边沿检测、没有自动告警缓冲。
        #: [命名 2026-11-17] 列表本体叫 `_server_rows`；`_rows` / `_records`
        #: 是**属性别名**（见类里的 property）⇒ 老代码/老测试用哪个名字都
        #: 指着同一份列表，"重新绑定"（`page._records = []`）也照样生效。
        self._server_rows = []
        #: 服务端取数是否成功（False = 断线/服务端没实现 0x011A ⇒ 保留上次
        #: 取到的行，不清表、不报错）。
        self._server_fetch_ok = False
        #: 上次向服务端要历史的时刻（毫秒；节流用）。
        self._last_fetch_ms = 0
        # （`_seen` 已废弃：健康边沿检测整体移除，见类注释的「唯一数据源」）
        self._seen = {}
        # ---- 筛选状态（对齐历史页：类型 + 时间窗 + 关键字）----
        self._kind_text = None     # None = 全部类型
        self._lo_ms = 0            # 时间窗下界（<=0 不限）
        self._hi_ms = 0            # 时间窗上界（<=0 不限）
        self._kw = ""              # 客户端关键字（空=不筛）
        # ---- 客户端内部分页状态 ----
        self._page_size = 20       # 当前页大小（缺省中间档）
        self._page_index = 0       # 当前页号（0 基）

        # ---- 三行工具条（与历史/日志页共用 DataPageToolbar）----
        # [按页裁剪] 无折线列/双视图（用户定案：报警页不要图表）、无刷新
        # （健康推送没有"重查"动作）；有滚动锁定 + 客户端内部分页。类型走
        # 「页面特有下拉」槽。
        self._tb = DataPageToolbar(
            self,
            columns=self.COLUMNS,
            extra_combo=("类型", list(self.KIND_ITEMS)),
            page_sizes=self.PAGE_SIZES,
            paging=True, refresh=False, chart_view=False,
            freeze=True, export=True, state_key="alarm",
            fold_btn_host=self._head_row)
        # ---- 别名（保持既有属性名，部分测试/页面逻辑仍按旧名取）----
        self._kind_combo = self._tb.extra_combo
        self._f_start = self._tb.start_edit
        self._f_end = self._tb.end_edit
        self._f_col = self._tb.column_combo
        self._f_text = self._tb.keyword_edit
        self._page_size_box = self._tb.page_size_box
        self._btn_prev = self._tb.btn_prev
        self._page_label = self._tb.page_label
        self._btn_next = self._tb.btn_next
        self._total_label = self._tb.total_label
        self._count_label = self._tb.count_label
        self._freeze = self._tb.freeze_box
        self._btn_export = self._tb.btn_export
        self._kind_combo.setToolTip("只显示选中类型的报警（全部类型 = 不限）")
        self._tb.set_page_size(self._page_size)
        # ---- 信号接回页面槽 ----
        self._tb.filterRequested.connect(self._apply_filter)
        self._tb.clearRequested.connect(self._clear_filter)
        self._tb.pageSizeChanged.connect(self._on_page_size)
        self._tb.prevRequested.connect(self._prev_page)
        self._tb.nextRequested.connect(self._next_page)
        self._tb.exportRequested.connect(self._export_data)
        self._tb.columnsChanged.connect(self._on_columns_changed)
        # [滚动锁定] 取消勾选时补刷一次（锁定期新报警只入库、不刷表）。
        self._freeze.toggled.connect(
            lambda frozen: None if frozen else self._rebuild())
        self._f_text.returnPressed.connect(self._apply_filter)
        self.root.addWidget(self._tb)
        # [折叠筛选区 ⇒ 表格吸收腾出的高度 2026-11-19 用户口径]
        # 折叠工具条后若没人吸收腾出的空间，表格下方会留一片空白 ⇒ 折叠
        # 白折；让表格改成可拉伸 ⇒ 一次多显示若干行（折叠的真正目的）。
        self._tb.collapsedChanged.connect(self.on_data_toolbar_collapsed)

        # ---- 数据表卡片（直接进页面布局，不再套双视图栈）----
        self._table = DataTable()
        # [必须先 setColumnCount] DataTable 缺省 3 列，只设标签不会扩列。
        self._table.table.setColumnCount(len(self.COLUMNS))
        self._table.table.setRowCount(0)
        self._table.table.setHorizontalHeaderLabels(list(self.COLUMNS))
        self._table.set_column_weights((3, 4, 3, 3))
        table_card = Card()
        table_card.add(self._table)
        self.root.addWidget(table_card, 1)
        # 表格吃满剩余视口（表头固定；折叠筛选条后由 on_data_toolbar_collapsed 重算）。
        self.fit_table_to_viewport(self._table)
        # 初始视图：全部列可见（构造期 selectionChanged 被跳过，这里补一次）。
        self._table.set_visible_columns(self.visible_columns())
        self._rebuild()

        # [2026-11-17] 本页**只**认服务端的数据源（0x011A）：本地不再有
        #   自动告警缓冲、也不再做健康边沿检测 ⇒ 客户端与网页端看到的是
        #   **同一份**报警历史（原先两边各算各的进程内副本 ⇒ 条数对不上，
        #   用户实报「web 端报警有数据，客户端报警视图没数据」）。

    # ==================================================================
    #                        记录（边沿触发）
    # ==================================================================

    @classmethod
    def auto_kind_text(cls, kind):
        """自动运行告警类型 → 中文（对齐 C alarmAutoKindText）。

        未知类型回退「自动运行」，不显示裸数字（C 端 default 分支同款）。
        """
        return cls.AUTO_KIND_TEXTS.get(int(kind), cls.KIND_TEXT_AUTO)

    def refresh(self):
        """按节拍向服务端要一次报警历史（0x011A）。

        [为什么只问服务端 2026-11-17 用户定案「本地不留了，全问服务端要」]
        报警记录原先**各端各算各的进程内副本**（客户端算健康边沿、网页端
        也算一份）⇒ ① 客户端一重启历史全丢 ② 两端条数天然对不上（用户实报
        「web 端报警有数据，客户端报警视图没数据」）。⇒ 边沿改由**服务端**记
        （alarms 表），两端都取同一份 ⇒ 单一数据源、客户端重启不丢、两端
        口径一致。本地**不再留副本**（没有边沿检测、没有自动告警缓冲、也
        没有"两套并行 + 去重"那套逻辑）。
        """
        self._maybe_fetch_server_rows()

    # ==================================================================
    #              服务端报警历史（0x011A）—— 单一数据源
    # ==================================================================
    #
    #: 服务端报警行 kind → (类型列文案, 状态列文案)。
    #: [口径与网页端一致 2026-11-17] 网页端 hub 的 AlarmLog 分 kind
    #: （health/valve/auto）与 deviceKind（设备类别）两列 ⇒ 类型列显示
    #: **设备类别**。本端服务端的 alarms 表有 kind（来源类别）+ dev（设备
    #: 类别）⇒ 这里按 kind 还原网页端那套列语义。
    #: [2026-11-21 三端对齐补洞] 服务端 _alarm_db_kind_source 实际写 6 类
    #: （app.py：health/auto/valve/flowmeter/device/analog/digital）；原表
    #: 漏 analog/digital ⇒ 这两类告警行在无 dev 键（老库）时会回退成
    #: 「自动运行」，与网页端 page11_alarm.js 的 SERVER_KIND_ROWS 不一致。
    #: 文案与网页端逐字同步（模拟量→越限保护 / 数字量→数字量告警）。
    SERVER_KIND_ROWS = {
        "auto": ("自动运行", "自动运行告警"),
        "valve": ("电动球阀组", "阀告警"),
        "flowmeter": ("流量计", "流量计告警"),
        "device": ("设备故障", "设备故障"),
        "analog": ("模拟量", "越限保护"),
        "digital": ("数字量", "数字量告警"),
    }

    #: 取服务端历史的间隔（毫秒）。太密会把服务端打爆，太疏则新报警要等。
    FETCH_INTERVAL_MS = 2000

    @staticmethod
    def _dev_kind_text(dev):
        """服务端行的 `dev`（设备类别键）→ 中文类别名；没有/不认识→空串。

        [为什么单抽一个] 健康边沿与自动/阀告警**都要**用它还原「类型」列
        （原来只有健康边沿用它 ⇒ 带 dev 的自动告警行类型列仍是"自动运行"，
        现场看报警表时分不出是哪台设备）。
        """
        if dev is None:
            return ""
        try:
            return model_mod.KIND_TEXT.get(int(dev), "")
        except (TypeError, ValueError):
            return ""

    def _server_row_to_record(self, row):
        """服务端一行 → 页面记录 (ts_ms, [时间, 设备, 类型, 状态])。"""
        ts = int(row.get(proto.ALARM_KEY_TS) or 0)
        kind = str(row.get(proto.ALARM_KEY_KIND) or "")
        source = str(row.get(proto.ALARM_KEY_SOURCE) or "")
        text = str(row.get(proto.ALARM_KEY_TEXT) or "")
        dev = row.get(proto.ALARM_KEY_DEV)
        if kind == "health" or (not kind and source):
            # 健康边沿：设备列 = 设备名、类型列 = 设备类别、状态列 = 健康档
            kind_text = self._dev_kind_text(dev) or "设备异常"
            name, state = source, text
        else:
            kind_text, state = self.SERVER_KIND_ROWS.get(
                kind, ("自动运行", "自动运行告警"))
            # [2026-11-25 设备故障状态列显示具体故障] device 行的 state 原来
            # 写死"设备故障"（SERVER_KIND_ROWS 第二项）⇒ 现场看报警表只见
            # "设备故障"四个字，**看不出是电机过载还是欠压**（text 里其实有
            # "变频器故障：电机过载（已停止设备，需复位）"，但没被显示）。
            # 设备列已有 source（变频器）、类型列已有 dev 类别，状态列改成
            # 显示 text 原文才能回答"到底是什么故障"。
            if kind == "device" and text:
                state = text
            # [自动/模拟量/数字量告警的设备列放原文] 与网页端同款：这几类
            # 告警的"设备"就是告警原文（"现场启动按钮被拒 ——…" /
            # "电机超温：14.0℃（超过阈值13.0℃），已停止设备"）。
            # [2026-11-24 板端实报「客户端设备列只写模拟量，web 是详细设备」]
            # 原来只有 auto 走原文，analog/digital 落到 source（"模拟量"）⇒
            # 同一行两端两副面孔；现与 web `alarm_row_texts` 的
            # auto/analog/digital 三类逐字对齐。
            name = text if kind in ("auto", "analog", "digital")                 else (source or text)
            # [设备类别键 2026-11-15/17] 行里带 dev ⇒ 「类型」列显示**设备
            # 类别**（如"流量计"），而不是笼统的"自动运行"——现场看报警表
            # 时设备类别不能撒谎；缺 dev（老服务端）⇒ 保持"自动运行"。
            kind_text = self._dev_kind_text(dev) or kind_text
        stamp = time.strftime("%H:%M:%S", time.localtime(ts / 1000.0)) if ts else ""
        return (ts, [stamp, name, kind_text, state])

    def _maybe_fetch_server_rows(self):
        """按节拍向服务端要一次报警历史（进页即要一次）。"""
        now = int(time.time() * 1000)
        if self._last_fetch_ms and now - self._last_fetch_ms < self.FETCH_INTERVAL_MS:
            return
        self._last_fetch_ms = now
        self._fetch_server_rows(0, [])

    def _fetch_server_rows(self, offset, acc):
        """分批取服务端报警历史（服务端单批上限 50 行）。"""
        limit = min(proto.ALARM_PAGE_MAX, self.MAX_ROWS - len(acc))
        if limit <= 0:
            self._rows = acc
            self._rebuild()
            return

        def _done(doc):
            if not doc.get("ok"):
                # 老服务端没有 0x011A ⇒ 保留本地边沿（不报错、不清表）。
                self._server_fetch_ok = False
                return
            self._server_fetch_ok = True
            rows = doc.get(proto.ALARM_KEY_ROWS) or []
            for r in rows:
                acc.append(self._server_row_to_record(r))
            if len(rows) >= limit and len(acc) < self.MAX_ROWS:
                self._fetch_server_rows(offset + len(rows), acc)
            else:
                self._rows = acc
                self._rebuild()

        self.ctx.bridge.alarm_page(offset=offset, limit=limit,
                                   on_done=_done,
                                   on_error=lambda e: self._server_fetch_error(e))

    def _server_fetch_error(self, err):
        """取数失败：保留本地行（老服务端/断线），不打断界面。"""
        self._server_fetch_ok = False

    def on_enter(self):
        """进页即取一次服务端报警历史（不等刷新节拍 ⇒ 打开就有数据）。"""
        Page.on_enter(self)
        self._last_fetch_ms = 0
        self._maybe_fetch_server_rows()

    @property
    def _rows(self):
        """别名：服务端报警行（本体是 `_server_rows`）。"""
        return self._server_rows

    @_rows.setter
    def _rows(self, value):
        self._server_rows = value

    @property
    def _records(self):
        """别名：老实现里的记录列表名（同一份，别再各存一份）。

        [为什么必须是 property 而不是普通属性] 老代码/老测试会写
        `page._records = []` 来"清空记录"⇒ 普通属性只是**另起一个名字**，
        页面照旧用 `_server_rows` ⇒ 清空没生效、行数对不上。property 才能
        让赋值真正落到唯一事实源上。
        """
        return self._server_rows

    @_records.setter
    def _records(self, value):
        self._server_rows = value

    def _all_records(self):
        """本页的全部行 = **服务端报警历史**（0x011A），**最新在前**。

        [2026-11-17] 本地不留副本 ⇒ 没有"本地边沿 + 服务端历史"两套并行、
        也就没有去重逻辑：客户端与网页端看到的是同一份数据。

        [排序口径 2026-11-17] **同毫秒按插入顺序倒序**：先把列表摆成
        "新→旧"（reverse），再按 ts **稳定**降序排 ⇒ 同一毫秒的两行里，
        后到的那行排前面（否则"最新在首行"不成立）。
        """
        rows = list(self._server_rows)
        rows.reverse()
        rows.sort(key=lambda item: item[0], reverse=True)
        return rows
    def _record_alarm(self, name, kind_text, state_text, ts_ms=None):
        """记一条报警（测试/诊断入口；正常路径走 refresh 边沿触发）。

        [滚动锁定] 勾上时新报警**只入库、不刷表**（与历史页同款语义）；
        取消勾选时补刷一次（信号接线里 `frozen=False → _rebuild()`）。
        """
        if ts_ms is None:
            ts_ms = int(time.time() * 1000)
        stamp = time.strftime("%H:%M:%S", time.localtime(ts_ms / 1000.0))
        self._rows.append((ts_ms, [stamp, name, kind_text, state_text]))
        if len(self._rows) > self.MAX_ROWS:
            self._rows = self._rows[-self.MAX_ROWS:]
        if not self._freeze.isChecked():
            self._rebuild()

    # ==================================================================
    #                   筛选（类型 + 时间窗 + 关键字）
    # ==================================================================

    def _row_visible(self, ts_ms, row):
        """一条记录是否落在当前筛选内（类型 + 时间窗 + 关键字）。

        类型按文本全等（对齐 C alarmRowVisible）；`ts < loTs` 或
        `ts > hiTs` 不显示、界为 0 表示该侧不限；关键字在
        设备 / 类型 / 状态 三列文本里包含匹配（空=不筛）。
        """
        if self._kind_text is not None and row[2] != self._kind_text:
            return False
        if self._lo_ms > 0 and ts_ms > 0 and ts_ms < self._lo_ms:
            return False
        if self._hi_ms > 0 and ts_ms > self._hi_ms:
            return False
        if self._kw and not any(self._kw in str(row[c])
                                for c in (1, 2, 3) if c < len(row)):
            return False
        return True

    def _visible_records(self):
        """当前筛选命中的**全集**记录（最新在前，铺表/导出/统计共用）。

        [与分页无关] 这里返回的是筛选命中的全部行；表格只铺其中当前页那
        一段（客户端内部分页），导出与总数统计都按全集 ⇒ 不受当前页影响。

        [数据源] 服务端报警历史（0x011A）—— 本页唯一来源（2026-11-17）。
        """
        return [(ts_ms, row) for ts_ms, row in self._all_records()
                if self._row_visible(ts_ms, row)]

    def _time_window_ms(self):
        """筛选框文本 → (lo_ms, hi_ms)；空框 = 0（不限）。

        归一化复用历史页 _norm_stamp（HH:MM 当天 / 带日期全键），
        再经 _stamp_to_ms 落成 epoch 毫秒 —— 与记录的 ts_ms 同一时间轴。
        """
        lo = HistoryPage._norm_stamp(self._f_start.text(), is_end=False) \
            if self._f_start.text().strip() else None
        hi = HistoryPage._norm_stamp(self._f_end.text(), is_end=True) \
            if self._f_end.text().strip() else None
        lo_ms = HistoryPage._stamp_to_ms(lo) if lo is not None else 0
        hi_ms = HistoryPage._stamp_to_ms(hi) if hi is not None else 0
        return lo_ms or 0, hi_ms or 0

    def _apply_filter(self):
        """应用类型 + 时间窗 + 关键字（对齐历史页：回第 1 页）。"""
        raw_start = self._f_start.text().strip()
        raw_end = self._f_end.text().strip()
        if raw_start and HistoryPage._norm_stamp(raw_start, is_end=False) \
                is None:
            self.ctx.set_status("起始时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        if raw_end and HistoryPage._norm_stamp(raw_end, is_end=True) is None:
            self.ctx.set_status("结束时间格式不对（HH:MM / MM-dd HH:MM / "
                                "yyyy-MM-dd HH:MM）")
            return
        lo_ms, hi_ms = self._time_window_ms()
        if lo_ms > 0 and hi_ms > 0 and lo_ms > hi_ms:
            self.ctx.set_status("起始时间晚于结束时间，未筛选")
            return
        idx = self._kind_combo.currentIndex()
        self._kind_text = None if idx <= 0 else self.KIND_ITEMS[idx]
        self._lo_ms = lo_ms
        self._hi_ms = hi_ms
        self._kw = self._f_text.text().strip()
        self._page_index = 0            # 新筛选：回第 1 页（历史页同款）
        self._rebuild()

    def _clear_filter(self):
        """清除筛选：类型回全部、时间窗回数据全区间、关键字清空、列全勾。

        [时间框回填成实际数据区间] 对齐 C alarmClearFilter 的注释：
        「看得见的就是全部」，不清空也不复位成"过去 24h"——那会让人
        以为只在看一天。
        """
        self._kind_combo.setCurrentIndex(0)
        self._kind_text = None
        self._kw = ""
        self._f_text.clear()
        self._f_col.check_all()         # 列回全勾（触发 columnsChanged）
        self._lo_ms = 0
        self._hi_ms = 0
        if self._rows:
            first = time.strftime("%Y-%m-%d %H:%M:%S",
                                  time.localtime(self._rows[0][0] / 1000.0))
            last = time.strftime("%Y-%m-%d %H:%M:%S",
                                 time.localtime(self._rows[-1][0] / 1000.0))
            self._f_start.setText(first)
            self._f_end.setText(last)
        else:
            self._f_start.clear()
            self._f_end.clear()
        self._page_index = 0
        self._rebuild()

    # ==================================================================
    #                        列多选（显示哪些列）
    # ==================================================================

    def visible_columns(self):
        """当前显示（勾中）的列下标列表，升序；**至少一列**。"""
        if not hasattr(self, "_f_col") or self._f_col is None:
            return list(range(len(self.COLUMNS)))
        cols = sorted(self._f_col.checked_indexes())
        return cols or list(range(len(self.COLUMNS)))

    def _on_columns_changed(self):
        """列勾选变化：立即重设可见列（视图设置，不用点「筛选」）。"""
        if not hasattr(self, "_table"):
            return                          # 构造期早到的信号，__init__ 末尾补
        cols = self.visible_columns()
        self._table.set_visible_columns(cols)
        self.ctx.set_status("显示 %d/%d 列：%s"
                            % (len(cols), len(self.COLUMNS),
                               "、".join(self.COLUMNS[c] for c in cols)))

    # ==================================================================
    #                  客户端内部分页 + 铺表 / 行数统计
    # ==================================================================

    def _page_count(self):
        """筛选命中全集按当前页大小能分几页（至少 1 页）。"""
        total = len(self._visible_records())
        if self._page_size <= 0:
            return 1
        return max(1, (total + self._page_size - 1) // self._page_size)

    def _prev_page(self):
        if self._page_index > 0:
            self._page_index -= 1
            self._rebuild()

    def _next_page(self):
        if self._page_index + 1 < self._page_count():
            self._page_index += 1
            self._rebuild()

    def _on_page_size(self):
        """改每页条数：回第 1 页（行号口径变了，停在原页号没意义）。

        [与历史/日志页同口径] 工具条的 pageSizeChanged 带的是**下拉
        index**（0/1/2），不是页大小值 ⇒ 槽不接参数，直接读 currentData()
        （10/20/50）。写成带参槽会把 index 当页大小，页大小被错设成 1/2。
        """
        data = self._page_size_box.currentData()
        if isinstance(data, int) and data > 0:
            self._page_size = data
        self._page_index = 0
        self._rebuild()

    def _rebuild(self):
        """把**当前页**那一段铺进表格（最新在上）并刷新统计/翻页钮。"""
        visible = self._visible_records()
        total = len(visible)
        pages = self._page_count()
        if self._page_index >= pages:
            self._page_index = pages - 1
        start = self._page_index * self._page_size
        page_rows = visible[start:start + self._page_size]
        table = self._table.table
        table.setRowCount(len(page_rows))
        for r, (_ts, row) in enumerate(page_rows):
            for c, text in enumerate(row):
                item = table.item(r, c)
                if item is None:
                    item = QTableWidgetItem("")
                    item.setTextAlignment(Qt.AlignCenter)
                    table.setItem(r, c, item)
                item.setText(text)
        table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)
        # [列宽跟着内容走] 同历史页：表头短、内容长的列不能被省略号截断。
        self._table.refresh_content_widths()
        # [统计口径对齐历史页] 总数/本页在分页行，计数标签说本页显示几行。
        self._total_label.setText("共 %d 条（本页 %d）"
                                  % (total, len(page_rows)))
        self._count_label.setText("本页显示 %d 行" % len(page_rows))
        self._page_label.setText("第 %d/%d 页"
                                 % (self._page_index + 1, pages))
        self._btn_prev.setEnabled(self._page_index > 0)
        self._btn_next.setEnabled(self._page_index + 1 < pages)

    # ==================================================================
    #                        CSV 导出（需 Download 权限）
    # ==================================================================

    def _export_data(self):
        """导出**当前筛选命中行的全量**（最新在上）为 CSV。

        [口径：按筛选结果，不是当前页]（用户口径 2026-11-11）表格现在是
        **客户端内部分页**的，界面上只有当前页那几十行；导出按
        `_visible_records()` 的**全集**（不受 `_page_index` 影响）。

        [权限闸对齐历史页] 无 Download 权限直接提示、不落盘。文件
        utf-8-sig，Excel 双击直开；列集合跟随当前可见列（界面看到的样子）。
        """
        rows = [row for _ts, row in self._visible_records()]
        if not rows:
            self.ctx.set_status("无可导出的数据")
            return
        if not self.ctx.bridge.can(proto.PERM_DOWNLOAD):
            self.ctx.set_status("权限不足：下载数据需要普通用户及以上账户")
            return
        default = "hsf_alarm_%s.csv" % time.strftime("%Y%m%d_%H%M%S")
        path, _selected = QFileDialog.getSaveFileName(
            self, "导出报警记录", default, "CSV (*.csv)")
        if not path:
            return
        self._write_export(path, rows)

    def _write_export(self, path, rows):
        """把（已授权的）行集写成 CSV（utf-8-sig；**导出列跟随可见列**）。

        独立成方法：无头测试直接调这里。表头与行内容用同一份列集合，
        与历史/日志页同口径（界面看到哪些列，就导哪些列）。
        """
        cols = self.visible_columns()
        headers = [self.COLUMNS[c] for c in cols]
        if not path.lower().endswith(".csv"):
            path += ".csv"
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow(headers)
            for texts in rows:
                writer.writerow([texts[c] if c < len(texts) else ""
                                 for c in cols])
        self.ctx.set_status("已导出 %d 条 → %s" % (len(rows), path))


# ============================================================================
#                              设置页
# ============================================================================

class LocalSettingsPage(Page):
    """本地设置：**界面设置 / 悬浮窗 / 窗口与屏幕** 三段合一（仅本机生效）。

    [2026-11-13 用户定案「三页合并成一页」] 原「本地设置(13) / 悬浮窗设置(16)
    / 窗口设置(17)」三页合并为本页：**页号 13 不变**，16/17 从注册表与导航树
    删除（全树 16 页，0..15）。三段内容**一个功能项不许丢**：
      · 「界面设置」= 原本地设置页全部项（强制紧凑 / 全屏 / 界面缩放 / 界面
        刷新周期）**加上**本机早已落盘、但控件散在别处的三项（趋势刷新间隔、
        历史每页条数、日志每页条数）—— 它们与「刷新周期」同属"本机显示
        偏好"，收到一处才不漏、也不至于"改了不知道在哪改"。
      · 「悬浮窗」= 原悬浮窗设置页全部项（标题栏 / 状态栏 / 导航显隐 +
        时钟 / 温湿度显隐）。
      · 「窗口与屏幕」= 原窗口设置页全部项（窗口尺寸记忆 + 屏幕休眠区）。

    [2026-11-10 用户口径「可以持久化的配置都持久化下」] 旋钮一律**改完立即
    生效 + 写本机设置**；界面缩放例外（QSS 与各页字号在建窗时按比例算好，
    运行期改要重建整棵树 ⇒ 存档 + 下次启动生效）。
    读写统一走 `settings` 模块（程序目录 INI，不写注册表）。
    """

    #: 界面缩放档（百分比）。与 settings.K_UI_SCALE_PCT 对应。
    SCALE_CHOICES = (100, 110, 125, 150)

    #: 窗口尺寸档（w, h）。
    SIZE_CHOICES = ((1280, 720), (1024, 600), (800, 480))

    #: 超时档位（毫秒；0 = 从不）——与 C 端 kPowerTimeouts 同源口径。
    POWER_TIMEOUTS = ((0, "从不"), (60 * 1000, "1 分钟"),
                      (5 * 60 * 1000, "5 分钟"), (10 * 60 * 1000, "10 分钟"),
                      (30 * 60 * 1000, "30 分钟"))

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        from . import settings as ui_settings
        self._settings = ui_settings
        self.add_header("本地设置", "仅本机生效（本机保存，重启沿用）")
        self._build_interface_section()
        self._build_overlay_section()
        self._build_window_section()
        # [2026-11-16 用户定案] 原「分区四：网页服务」拆成独立页
        # （pages_web.WebSettingsPage / 页号 16）——开关+端口+域名+远程端口
        # +扫码直达都在那一页；本页不再有网页服务区（两处各配一份必然漂移）。
        self.root.addStretch(1)

    # ------------------------------------------------------------------
    #              分区一：界面设置（原本地设置页 + 显示偏好）
    # ------------------------------------------------------------------
    def _build_interface_section(self):
        """建「界面设置」区：显示 / 数据刷新 / 每页条数。"""
        ui_settings = self._settings
        self.root.addWidget(section_title("界面设置"))

        card = Card("显示")
        self.compact_check = QCheckBox("紧凑模式（竖屏/小屏）")
        self.compact_check.setToolTip("勾选=强制紧凑；不勾=按窗宽自动判"
                                      "（宽 < %dpx 自动紧凑）" % theme.COMPACT_MAX_W)
        self.compact_check.setChecked(ui_settings.get_bool(
            ui_settings.K_FORCE_COMPACT, False))
        card.add(self.compact_check)

        self.fullscreen_check = QCheckBox("全屏显示")
        self.fullscreen_check.setChecked(ui_settings.get_bool(
            ui_settings.K_FULLSCREEN, False))
        card.add(self.fullscreen_check)

        self.scale_combo = QComboBox()
        for pct in self.SCALE_CHOICES:
            self.scale_combo.addItem("%d%%" % pct, pct)
        # 当前值：存过就选它；没存过就按 theme 实际比例挑最近的档
        saved_pct = ui_settings.get_int(ui_settings.K_UI_SCALE_PCT, 0)
        if saved_pct <= 0:
            saved_pct = int(round(theme.ui_scale() * 100))
        idx = self.scale_combo.findData(
            min(self.SCALE_CHOICES, key=lambda p: abs(p - saved_pct)))
        self.scale_combo.setCurrentIndex(max(0, idx))
        self.scale_combo.setToolTip("改完**下次启动生效**（QSS 与页面字号"
                                    "在建窗时按比例算好）")
        card.body.addLayout(aligned_row("界面缩放", self.scale_combo))

        # ---- 主题（2026-11-14 用户定案「本地设置增加主题功能」）----
        # [本机设置] 只影响本机界面（写客户端 INI），与服务端无关。
        self.theme_combo = QComboBox()
        for tid, tname in theme.THEME_LIST:
            self.theme_combo.addItem(tname, tid)
        cur_tid = (ui_settings.get_str(ui_settings.K_THEME, "")
                   or theme.current_theme_id())
        tidx = self.theme_combo.findData(cur_tid)
        self.theme_combo.setCurrentIndex(tidx if tidx >= 0 else 0)
        self.theme_combo.setToolTip(
            "界面配色（亮色工业白 / 深色控制室 / ISA 仪表 / 护眼绿 / 高对比）；\n"
            "改完**立即生效**并记在本机设置里（下次启动仍是这一套）")
        card.body.addLayout(aligned_row("主题", self.theme_combo))
        self.theme_combo.currentIndexChanged.connect(self._on_theme_changed)
        self.root.addWidget(card)

        card2 = Card("数据刷新")
        self.refresh_spin = QSpinBox()
        self.refresh_spin.setRange(100, 2000)
        self.refresh_spin.setSingleStep(100)
        self.refresh_spin.setSuffix(" ms")
        self.refresh_spin.setValue(ui_settings.refresh_ms())
        self.refresh_spin.setToolTip("主界面的数据重绘节拍（改完立即生效）")
        card2.body.addLayout(aligned_row("界面刷新周期", self.refresh_spin))

        # 趋势刷新间隔：变频器页/水质页的「刷新」下拉共用这一个键
        # （settings.K_TREND_MS）——本页是它在本机设置里的**总入口**。
        self.trend_combo = QComboBox()
        for ms in self._trend_choices():
            self.trend_combo.addItem("%d ms" % ms, ms)
        tidx = self.trend_combo.findData(ui_settings.trend_ms())
        self.trend_combo.setCurrentIndex(tidx if tidx >= 0 else 0)
        self.trend_combo.setToolTip("实时趋势图的记录密度（改完立即落盘；"
                                    "下次进变频器/水质页按此节拍）")
        card2.body.addLayout(aligned_row("趋势刷新间隔", self.trend_combo))
        self.root.addWidget(card2)

        card3 = Card("每页条数")
        self.hist_page_combo = QComboBox()
        for n in self._page_size_choices():
            self.hist_page_combo.addItem("%d 条" % n, n)
        hsize = ui_settings.hist_page_size() or proto.HISTORY_PAGE_DEFAULT
        hidx = self.hist_page_combo.findData(hsize)
        self.hist_page_combo.setCurrentIndex(hidx if hidx >= 0 else 0)
        self.hist_page_combo.setToolTip("历史数据页每页行数（下次进该页生效）")
        card3.body.addLayout(aligned_row("历史数据", self.hist_page_combo))

        self.log_page_combo = QComboBox()
        for n in self._page_size_choices():
            self.log_page_combo.addItem("%d 条" % n, n)
        lsize = ui_settings.log_page_size() or proto.LOG_PAGE_DEFAULT
        lidx = self.log_page_combo.findData(lsize)
        self.log_page_combo.setCurrentIndex(lidx if lidx >= 0 else 0)
        self.log_page_combo.setToolTip("运行日志页每页行数（下次进该页生效）")
        card3.body.addLayout(aligned_row("运行日志", self.log_page_combo))
        self.root.addWidget(card3)

        # [信号必须在**全部初值就位之后**接线] 程序化 setChecked/setValue 同样
        # 会发 toggled/valueChanged；先接线就会在建页时把初值"当成用户改动"
        # 再写回设置（实测：一打开设置页 uiScalePct/refreshMs 就被写成默认值）。
        self.compact_check.toggled.connect(self._on_compact)
        self.scale_combo.currentIndexChanged.connect(self._on_scale)
        self.refresh_spin.valueChanged.connect(self._on_refresh)
        self.fullscreen_check.toggled.connect(self._on_fullscreen)
        self.trend_combo.currentIndexChanged.connect(self._on_trend)
        self.hist_page_combo.currentIndexChanged.connect(self._on_hist_page)
        self.log_page_combo.currentIndexChanged.connect(self._on_log_page)

    @staticmethod
    def _trend_choices():
        """趋势「刷新」档位（毫秒）。

        [为什么要局部 import] pages_monitor 反向依赖本模块
        （`from .pages_other import LineChartDialog`）⇒ 模块顶层互 import
        会成环；运行期局部 import 安全（那时本模块已装载完）。
        """
        from .pages_monitor import TrendChart
        return TrendChart.SAMPLE_CHOICES

    @staticmethod
    def _page_size_choices():
        """历史/日志页「每页条数」档位（与两页工具条同源）。"""
        return HistoryPage.PAGE_SIZES

    # ---- 各旋钮：立即生效 + 落盘 ----
    def _on_compact(self, checked):
        self._settings.set_value(self._settings.K_FORCE_COMPACT, bool(checked))
        win = self.window()
        if hasattr(win, "set_compact_override"):
            win.set_compact_override(bool(checked))
        self.ctx.set_status("紧凑模式：%s（已保存）"
                            % ("强制开" if checked else "按窗宽自动"))

    def _on_scale(self, _idx):
        pct = self.scale_combo.currentData() or self.SCALE_CHOICES[0]
        self._settings.set_value(self._settings.K_UI_SCALE_PCT, int(pct))
        self.ctx.set_status("界面缩放 %d%% 已保存，下次启动生效" % pct)

    def _on_theme_changed(self, _idx=None):
        """主题切换：**立即**换配色 + 写本机设置（客户端 INI）。

        [为什么立即生效] 主题只影响本机界面（与服务端无关）⇒ 用户在设置页
        选完就该看到效果，不必重启；换完再退出重进仍是这一套（已落盘）。
        [自绘面] 表盘/色点每次 paint 取 `theme.dial_colors()` ⇒ 自动跟随。
        """
        tid = self.theme_combo.currentData()
        app = QApplication.instance()
        if app is None:
            return
        theme.apply_theme(app, tid, persist=True)
        self.ctx.set_status("界面主题已切换（%s）"
                            % self.theme_combo.currentText())

    def _on_refresh(self, ms):
        self._settings.set_value(self._settings.K_REFRESH_MS, int(ms))
        win = self.window()
        timer = getattr(win, "_refresh_timer", None)
        if timer is not None:
            timer.setInterval(int(ms))
        self.ctx.set_status("界面刷新周期 %d ms（已保存）" % ms)

    def _on_fullscreen(self, checked):
        self._settings.set_value(self._settings.K_FULLSCREEN, bool(checked))
        win = self.window()
        if checked:
            win.showFullScreen()
        else:
            win.showNormal()
        self.ctx.set_status("全屏：%s（已保存）" % ("开" if checked else "关"))

    def _on_trend(self, _idx):
        ms = self.trend_combo.currentData() or 200
        self._settings.set_value(self._settings.K_TREND_MS, int(ms))
        self.ctx.set_status("趋势刷新间隔 %d ms（已保存）" % ms)

    def _on_hist_page(self, _idx):
        n = self.hist_page_combo.currentData() or proto.HISTORY_PAGE_DEFAULT
        self._settings.set_value(self._settings.K_HIST_PAGE_SIZE, int(n))
        self.ctx.set_status("历史数据每页 %d 条（已保存，下次进该页生效）" % n)

    def _on_log_page(self, _idx):
        n = self.log_page_combo.currentData() or proto.LOG_PAGE_DEFAULT
        self._settings.set_value(self._settings.K_LOG_PAGE_SIZE, int(n))
        self.ctx.set_status("运行日志每页 %d 条（已保存，下次进该页生效）" % n)

    # ------------------------------------------------------------------
    #              分区二：悬浮窗（界面元素显隐）
    # ------------------------------------------------------------------
    def _build_overlay_section(self):
        """建「悬浮窗」区：界面元素显隐（标题栏/状态栏/导航 + 时钟/温湿度）。"""
        ui_settings = self._settings
        self.root.addWidget(section_title("悬浮窗"))

        card = Card("显示元素")
        self.show_title = QCheckBox("显示顶部标题栏")
        self.show_title.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_TITLE, True))
        card.add(self.show_title)

        self.show_status = QCheckBox("显示底部状态栏")
        self.show_status.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_STATUS, True))
        card.add(self.show_status)

        self.show_nav = QCheckBox("显示左侧导航栏")
        self.show_nav.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_NAV, True))
        card.add(self.show_nav)
        self.root.addWidget(card)

        # [2026-11-22 用户定案「左上角图标用导航树还是第二张图那种菜单页」]
        # 两档只改 ☰ 的**导航形态**，不改任何页面能力：
        #   · 导航树 = 宽屏收起/展开左侧树（既有行为）；
        #   · 菜单页 = 打开当前分类的子菜单（如「设备 选择子项」），
        #     子菜单页自带「← 返回主菜单」，逐级可回。
        # 窄屏没有导航树空间 ⇒ 两档都落到菜单页（见 MainWindow._on_menu_clicked）。
        card_nav = Card("导航方式")
        self.menu_style_combo = QComboBox()
        self.menu_style_combo.addItem("导航树",
                                      ui_settings.MENU_STYLE_TREE)
        self.menu_style_combo.addItem("菜单页",
                                      ui_settings.MENU_STYLE_PAGE)
        # [小窗不裁字 2026-11-22] 缺省 AdjustToContentsOnFirstShow 在 QSS
        # 字号 polish **之前**算宽 ⇒ 150% 缩放下「导航树」也会被箭头挤掉
        # 最后一笔（离屏截图实见）。改成随字体/内容重算；minContentsLength
        # 按**构造期字体**算，取 10 字留足余量（QSS 字号更大时也不裁）。
        self.menu_style_combo.setSizeAdjustPolicy(
            QComboBox.AdjustToContents)
        self.menu_style_combo.setMinimumContentsLength(10)
        cur = self.menu_style_combo.findData(ui_settings.menu_button_style())
        self.menu_style_combo.setCurrentIndex(cur if cur >= 0 else 0)
        # 菜单页模式下导航树强制隐藏 ⇒「显示左侧导航栏」开关暂不可用
        # （值不丢，切回导航树档后按它恢复）。
        self._sync_nav_switch(ui_settings.menu_button_style())
        self.menu_style_combo.setToolTip(
            "导航树：点 ☰ 收起/展开左侧导航（宽屏）；\n"
            "菜单页：点 ☰ 打开当前分类的入口页（如设备 → 选择子项），\n"
            "        子菜单页可「← 返回主菜单」逐级返回。\n"
            "窄屏（放不下导航树）两档都使用菜单页。")
        card_nav.body.addLayout(
            aligned_row("左上角 ☰ 按钮", self.menu_style_combo))
        card_nav.add(hint_label(
            "「菜单页」按当前页自动选分类（设备/数据/设置）；"
            "在子菜单页再点 ☰ 回主菜单。"))
        self.root.addWidget(card_nav)

        card2 = Card("标题栏内容")
        self.show_camera = QCheckBox("显示相机")
        self.show_camera.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_CAMERA, True))
        self.show_camera.setToolTip(
            "标题栏的相机徽章（点开悬浮画面卡；再点关闭）")
        card2.add(self.show_camera)
        self.show_qr = QCheckBox("显示二维码")
        self.show_qr.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_QR, True))
        self.show_qr.setToolTip("标题栏的网页地址二维码徽章"
                                "（点徽章可弹出局域网/远程二维码）")
        card2.add(self.show_qr)
        self.show_mode = QCheckBox("显示运行模式")
        self.show_mode.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_MODE, True))
        self.show_mode.setToolTip("标题栏的手动/自动模式钮与自动运行徽标"
                                  "（两者同属一条信息链，一起显隐）")
        card2.add(self.show_mode)
        self.show_health = QCheckBox("显示设备及仪表")
        self.show_health.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_HEALTH, True))
        self.show_health.setToolTip("标题栏的「故障N / 离线N」指示"
                                    "（全部正常时本来就不占位）")
        card2.add(self.show_health)
        self.show_clock = QCheckBox("显示时钟")
        self.show_clock.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_CLOCK, True))
        self.show_clock.setToolTip("标题栏左边的日期/时钟（取消勾选即隐藏）")
        card2.add(self.show_clock)
        self.show_th = QCheckBox("显示温湿度")
        self.show_th.setChecked(ui_settings.get_bool(
            ui_settings.K_SHOW_TEMPHUM, True))
        self.show_th.setToolTip("标题栏右边的电箱温度/湿度（取消勾选即隐藏）")
        card2.add(self.show_th)
        self.root.addWidget(card2)

        # [初值就位后再接线] 否则 setChecked 的 toggled 会在建页时写回设置。
        self.show_title.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_TITLE, on))
        self.show_status.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_STATUS, on))
        self.show_nav.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_NAV, on))
        self.show_qr.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_QR, on))
        self.show_camera.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_CAMERA, on))
        self.show_mode.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_MODE, on))
        self.show_health.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_HEALTH, on))
        self.show_clock.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_CLOCK, on))
        self.show_th.toggled.connect(
            lambda on: self._set_overlay(ui_settings.K_SHOW_TEMPHUM, on))
        # 初值就位后再接线（同上：建页不许写回设置）。
        self.menu_style_combo.currentIndexChanged.connect(
            self._on_menu_style_changed)

    def _sync_nav_switch(self, style):
        """「显示左侧导航栏」开关可用性跟随导航方式（菜单页档禁用）。"""
        tree_mode = style == self._settings.MENU_STYLE_TREE
        self.show_nav.setEnabled(tree_mode)
        if tree_mode:
            self.show_nav.setToolTip("")
        else:
            self.show_nav.setToolTip(
                "菜单页模式下导航树自动隐藏；切回「导航树」后按本开关恢复")

    def _on_menu_style_changed(self, _index):
        """保存 ☰ 导航方式并立即生效（菜单页档隐藏左侧导航树）。"""
        ui_settings = self._settings
        style = self.menu_style_combo.currentData()
        if style not in ui_settings.MENU_STYLES:
            style = ui_settings.MENU_STYLE_TREE
        ui_settings.set_value(ui_settings.K_MENU_BUTTON_STYLE, style)
        self._sync_nav_switch(style)
        apply_overlay(self.window(), ui_settings)
        if style == ui_settings.MENU_STYLE_PAGE:
            self.ctx.set_status("导航方式已更新（已保存）；导航树已隐藏")
        else:
            self.ctx.set_status("导航方式已更新（已保存）")

    def _set_overlay(self, key, on):
        """写设置 + 立即作用到主窗（与启动还原同一份 `apply_overlay`）。"""
        self._settings.set_value(key, bool(on))
        apply_overlay(self.window(), self._settings)
        self.ctx.set_status("界面元素已更新（已保存）")

    # ------------------------------------------------------------------
    #          分区三：窗口与屏幕（尺寸记忆 + 屏幕休眠）
    # ------------------------------------------------------------------
    def _build_window_section(self):
        """建「窗口与屏幕」区：尺寸卡 + 屏幕休眠卡 + 说明卡。"""
        ui_settings = self._settings
        self.root.addWidget(section_title("窗口与屏幕"))

        card = Card("尺寸")
        self.size_combo = QComboBox()
        for w, h in self.SIZE_CHOICES:
            self.size_combo.addItem("%d × %d" % (w, h), (w, h))
        self.size_combo.addItem("全屏", (0, 0))
        # 上次保存的尺寸对应哪一档（对不上就留在第 0 档，不影响还原）
        saved_w, saved_h = ui_settings.win_size()
        if saved_w > 0:
            for i, (w, h) in enumerate(self.SIZE_CHOICES):
                if (w, h) == (saved_w, saved_h):
                    self.size_combo.setCurrentIndex(i)
                    break
        card.body.addLayout(aligned_row("窗口大小", self.size_combo))

        self.size_apply_btn = QPushButton("应用")
        self.size_apply_btn.clicked.connect(self._apply)
        btn_row = wrap_row([self.size_apply_btn])
        card.body.addWidget(btn_row)
        # 「信息」原是孤立小卡（只有一句话）：并入尺寸卡底部（同属窗口话题）
        card.add(hint_label("窄窗口（宽 < %dpx）时左侧导航会自动收起，"
                            "由「主菜单」页承载导航。" % theme.COMPACT_MAX_W))
        self.root.addWidget(card)

        self._build_power_card()
        self._build_ss_anim_card()
        self._build_mouse_card()

    # ------------------------------------------------------------------
    #                      屏幕休眠区
    # ------------------------------------------------------------------
    def _build_power_card(self):
        """建「屏幕休眠」卡（控件先建好，能力不可用时统一置灰）。"""
        card = Card("屏幕休眠")
        self._power_card = card

        # ---- 状态行（能力位 + 当前态）----
        self.power_state = QLabel("屏幕休眠：未初始化")
        self.power_state.setWordWrap(True)
        self.power_state.setStyleSheet(theme.caption_label_qss())
        card.body.addWidget(self.power_state)

        # ---- 自动熄屏时长（主路径）----
        self.power_blank_combo = QComboBox()
        for ms, text in self.POWER_TIMEOUTS:
            self.power_blank_combo.addItem(text, ms)
        card.body.addLayout(aligned_row("自动熄屏", self.power_blank_combo))

        # ---- 到期动作（与自动熄屏共用时长档）----
        self.power_blank_action = QComboBox()
        self.power_blank_action.addItem("熄屏", settings.BLANK_ACTION_OFF)
        self.power_blank_action.addItem("屏保", settings.BLANK_ACTION_SCREENSAVER)
        card.body.addLayout(aligned_row("到期动作", self.power_blank_action))

        # ---- 唤醒来源 ----
        self.power_wake_mouse = QCheckBox("鼠标")
        self.power_wake_keyboard = QCheckBox("键盘")
        self.power_wake_touch = QCheckBox("触摸")
        wake_row = wrap_row([self.power_wake_mouse, self.power_wake_keyboard,
                             self.power_wake_touch])
        card.body.addLayout(aligned_row("唤醒来源", wake_row, stretch=1))

        # ---- 系统挂起（低频，放按钮前）----
        self.power_suspend_combo = QComboBox()
        for ms, text in self.POWER_TIMEOUTS:
            self.power_suspend_combo.addItem(text, ms)
        card.body.addLayout(aligned_row("系统挂起", self.power_suspend_combo))

        # ---- 动作按钮（统一 8px 间距 + 行尾留白）----
        self.power_off_btn = QPushButton("立即休眠")
        self.power_off_btn.clicked.connect(self._power_screen_off)
        self.power_on_btn = QPushButton("唤醒屏幕")
        self.power_on_btn.clicked.connect(self._power_screen_on)
        self.power_save_btn = QPushButton("保存策略")
        self.power_save_btn.clicked.connect(self._power_save)
        self.power_perm_btn = QPushButton("请求授权")
        self.power_perm_btn.clicked.connect(self._power_request_permission)
        btn_row = QHBoxLayout()
        btn_row.setSpacing(8)
        for b in (self.power_off_btn, self.power_on_btn,
                  self.power_save_btn, self.power_perm_btn):
            btn_row.addWidget(b)
        btn_row.addStretch(1)
        card.body.addLayout(btn_row)
        # 说明统一沉到卡底（不再夹在控件之间）
        card.add(hint_label(
            "自动休眠选「屏保」时：到点显示「美科沃腾」标志动画，"
            "任意按键/触摸/鼠标退出（不真关显示器）。"))

        self.root.addWidget(card)

        # ---- 运行期状态 ----
        self._power_bridge = None
        self._power_native = None
        self._power_caps = 0
        self._power_ready = False

    def _build_ss_anim_card(self):
        """「屏保动画」卡（2026-11-26 用户「流光/呼吸/移动/文字流光
        都是可配置选项，并且本地持久化」+「增加预览功能」）。

        [预览为什么直接调 show_now()] 屏保覆盖层本身就是主窗的**子控件**
        （白底铺满、不开独立 X 窗口）⇒ 板端不会触发 xfwm4 抬系统任务栏
        盖住画面（用户多次实锅）。点「预览」= 立刻弹出屏保，任意点击/
        按键退出——比再做一个独立浮层更板端安全。
        """
        card = Card("屏保动画")
        self._ss_anim_card = card
        specs = (
            ("ss_anim_flow", "流光动画", settings.K_SS_FLOW, True,
             "logo 上有一道色带横向流动（预渲染帧，不占 CPU）"),
            ("ss_anim_breath", "呼吸缩放", settings.K_SS_BREATH, True,
             "图标在 80%~100% 间缓慢放大缩小"),
            ("ss_anim_move", "移动漂浮", settings.K_SS_MOVE, False,
             "图标轻微左右上下漂动（缺省关：图标放大居中不移动）"),
            ("ss_anim_text_flow", "文字流光", settings.K_SS_TEXT_FLOW, True,
             "「美科沃腾」文字也随色带明显变色"),
        )
        for attr, label, key, default, tip in specs:
            box = QCheckBox(label)
            box.setChecked(bool(self._settings.get_bool(key, default)))
            box.setToolTip(tip)
            setattr(self, attr, box)
            card.body.addWidget(box)
        row = QHBoxLayout()
        row.setSpacing(8)
        self.ss_anim_apply_btn = QPushButton("应用")
        self.ss_anim_apply_btn.clicked.connect(self._ss_anim_save)
        row.addWidget(self.ss_anim_apply_btn)
        self.ss_anim_preview_btn = QPushButton("预览")
        self.ss_anim_preview_btn.clicked.connect(self._ss_anim_preview)
        self.ss_anim_preview_btn.setToolTip(
            "立刻按当前勾选显示屏保；点击/按键/触摸退出（板端不弹独立窗口）")
        row.addWidget(self.ss_anim_preview_btn)
        row.addStretch(1)
        card.body.addLayout(row)
        self.root.addWidget(card)

    def _build_mouse_card(self):
        """「鼠标」卡（2026-11-27 用户「板端的客户端在本地设置中添加鼠标的
        一些常见设置比如鼠标大小，隐藏鼠标光标等等」+「还有鼠标样式」）。

        [为什么自绘箭头] Qt 不能调系统光标尺寸；板端触摸屏 + 偶接鼠标时小
        箭头看不清 ⇒ 24/32/48px 自绘（见 mouse_cursor.py）。其它形状用 Qt
        标准光标（尺寸跟随系统）。隐藏用 BlankCursor（事件照常送达）。
        改动**立即生效**并落盘。
        """
        card = Card("鼠标")
        self._mouse_card = card

        self.mouse_style_combo = QComboBox()
        for sid, sname in settings.MOUSE_CURSOR_STYLES:
            self.mouse_style_combo.addItem(sname, sid)
        sidx = self.mouse_style_combo.findData(
            self._settings.get_str(self._settings.K_MOUSE_CURSOR_STYLE,
                                   settings.MOUSE_CURSOR_STYLE_DEF))
        self.mouse_style_combo.setCurrentIndex(sidx if sidx >= 0 else 0)
        card.body.addLayout(aligned_row("光标样式", self.mouse_style_combo))

        self.mouse_size_combo = QComboBox()
        for px, pname in ((0, "系统默认"), (24, "小（24px）"),
                          (32, "中（32px）"), (48, "大（48px）")):
            self.mouse_size_combo.addItem(pname, px)
        szidx = self.mouse_size_combo.findData(
            self._settings.get_int(self._settings.K_MOUSE_CURSOR_SIZE, 0))
        self.mouse_size_combo.setCurrentIndex(szidx if szidx >= 0 else 0)
        self.mouse_size_combo.setToolTip(
            "箭头样式下的光标尺寸（自绘，立即生效）；\n"
            "十字/手型等标准样式由系统绘制，尺寸跟随系统。")
        card.body.addLayout(aligned_row("光标大小", self.mouse_size_combo))

        self.mouse_hidden = QCheckBox("隐藏鼠标光标（触摸屏防挡读数）")
        self.mouse_hidden.setChecked(bool(self._settings.get_bool(
            self._settings.K_MOUSE_CURSOR_HIDDEN, False)))
        self.mouse_hidden.setToolTip(
            "只隐藏光标图形，点击/触摸事件照常送达；\n"
            "适合纯触摸操作现场，避免光标挡住数值。")
        card.add(self.mouse_hidden)
        card.add(hint_label(
            "光标大小只对「箭头」样式生效（其它样式由系统绘制）。"
            "改动立即生效并保存在本机。"))

        # 初值就位后再接线（程序化 setCurrentIndex 也会发 currentIndexChanged）
        self.mouse_style_combo.currentIndexChanged.connect(self._mouse_apply)
        self.mouse_size_combo.currentIndexChanged.connect(self._mouse_apply)
        self.mouse_hidden.toggled.connect(self._mouse_apply)
        self.root.addWidget(card)

    def _mouse_apply(self, *_a):
        """保存三项鼠标设置并立即应用到主窗。"""
        style = str(self.mouse_style_combo.currentData()
                    or settings.MOUSE_CURSOR_STYLE_DEF)
        size = int(self.mouse_size_combo.currentData() or 0)
        hidden = bool(self.mouse_hidden.isChecked())
        self._settings.set_value(self._settings.K_MOUSE_CURSOR_STYLE, style)
        self._settings.set_value(self._settings.K_MOUSE_CURSOR_SIZE, size)
        self._settings.set_value(self._settings.K_MOUSE_CURSOR_HIDDEN, hidden)
        try:
            from .mouse_cursor import apply_mouse_settings
            result = apply_mouse_settings(self.window(), self._settings)
        except Exception:                             # noqa: BLE001
            result = "default"
        self.ctx.set_status("鼠标设置已保存（%s）" % {
            "hidden": "已隐藏光标",
            "default": "系统默认光标",
        }.get(result, "样式=%s 大小=%s" % (style, size)))

    def _ss_anim_widgets(self):
        return (self.ss_anim_flow, self.ss_anim_breath, self.ss_anim_move,
                self.ss_anim_text_flow)

    def _ss_anim_reload(self):
        """从 settings 回显四个勾选（on_enter / 保存后调用）。"""
        for box, key, default in (
                (self.ss_anim_flow, settings.K_SS_FLOW, True),
                (self.ss_anim_breath, settings.K_SS_BREATH, True),
                (self.ss_anim_move, settings.K_SS_MOVE, False),
                (self.ss_anim_text_flow, settings.K_SS_TEXT_FLOW, True)):
            box.setChecked(bool(self._settings.get_bool(key, default)))

    def _ss_anim_save(self):
        """四开关落盘 + 立即生效（无电源库也能存，纯本机动画）。"""
        pairs = (
            (settings.K_SS_FLOW, self.ss_anim_flow.isChecked()),
            (settings.K_SS_BREATH, self.ss_anim_breath.isChecked()),
            (settings.K_SS_MOVE, self.ss_anim_move.isChecked()),
            (settings.K_SS_TEXT_FLOW, self.ss_anim_text_flow.isChecked()),
        )
        for key, value in pairs:
            self._settings.set_value(key, value)
        self._ss_anim_refresh_running()
        self.ctx.set_status("屏保动画设置已保存（预览可看效果）")

    def _ss_anim_refresh_running(self):
        """让正在显示的屏保按新开关重画（render_frame 每帧读设置）。"""
        from PyQt5.QtWidgets import QApplication
        ss = getattr(QApplication.instance(), "_hsf_screensaver", None)
        overlay = getattr(ss, "_overlay", None) if ss is not None else None
        if overlay is not None and hasattr(overlay, "refresh_settings"):
            overlay.refresh_settings()

    def _ss_anim_preview(self):
        """预览：直接弹屏保覆盖层（主窗子控件，板端不抬任务栏）。"""
        self._ss_anim_save()                 # 勾选即落盘再预览，所见即所得
        from PyQt5.QtWidgets import QApplication
        qapp = QApplication.instance()
        ss = getattr(qapp, "_hsf_screensaver", None)
        if ss is None:
            try:
                from .screensaver import ScreenSaverController
                ss = ScreenSaverController(qapp, settings, lambda: self.window())
                qapp._hsf_screensaver = ss  # noqa: SLF001
            except Exception:                         # noqa: BLE001
                ss = None
        if ss is None or not ss.show_now():
            self.ctx.set_status("屏保预览失败：位图缺失或无主窗口")
        else:
            self.ctx.set_status("屏保预览中：点击/按键/触摸退出")

    def _power_widgets(self):
        """屏幕休眠区的全部控件（统一置灰/启用用）。"""
        return (self.power_blank_combo, self.power_suspend_combo,
                self.power_blank_action,
                self.power_wake_mouse, self.power_wake_keyboard,
                self.power_wake_touch, self.power_off_btn, self.power_on_btn,
                self.power_save_btn, self.power_perm_btn)

    def _power_report(self, text):
        """状态行 + 主窗状态栏（终态可见）。"""
        self.power_state.setText(text)
        self.ctx.set_status(text)

    def _power_fallback_to_screensaver(self, reason):
        """无 C 电源能力时：硬件熄屏项置灰，屏保动作项保留可用（纯 Python）。"""
        self._power_set_enabled(False)
        self.power_blank_action.setEnabled(True)
        action = self._settings.get_str(self._settings.K_POWER_BLANK_ACTION,
                                        settings.BLANK_ACTION_OFF)
        idx = self.power_blank_action.findData(action)
        self.power_blank_action.setCurrentIndex(idx if idx >= 0 else 0)
        self.power_state.setText("%s；屏保可用（本机动画，不依赖电源库）"
                                 % reason)

    def _power_init(self):
        """连接本机能力库并读回当前状态（失败则硬件项置灰、屏保保留）。"""
        if self._power_ready:
            return True
        try:
            from . import hsf_native
        except Exception as exc:                          # pragma: no cover
            self._power_fallback_to_screensaver(
                "屏幕休眠不可用：模块导入失败（%s）" % exc)
            return False
        if not hsf_native.available():
            self._power_fallback_to_screensaver(
                "屏幕休眠不可用：缺少 hsb_bridge 动态库（%s）。"
                "编译口径：.zcode/build_hsf_bridge.bat（板端需交叉编译出 "
                "libhsb_bridge.so）"
                % hsf_native.library_path())
            return False
        bridge = hsf_native.bridge()
        self._power_native = hsf_native
        self._power_bridge = bridge
        self._power_caps = bridge.power_capabilities()
        self._power_ready = True
        if not self._power_caps:
            self._power_report("屏幕休眠：本平台不支持（电源能力位为 0）")
            self._power_set_enabled(False)
            return False
        self._power_apply_caps()
        self._power_load_policy()
        return True

    def _power_apply_caps(self):
        """按能力位逐项置灰（不支持的项给出原因，不留死控件）。"""
        caps = self._power_caps
        native = self._power_native
        self._power_set_enabled(True)
        if not (caps & native.CAP_SCREEN_CONTROL):
            # 无主动关屏原语：立即熄屏/唤醒不可用（若平台能委托系统计时，
            # 自动熄屏时长仍可用——安卓口径）。
            self.power_off_btn.setEnabled(False)
            self.power_on_btn.setEnabled(False)
        if not (caps & native.CAP_SYSTEM_SUSPEND):
            self.power_suspend_combo.setEnabled(False)
        if not (caps & native.CAP_SCREEN_TIMEOUT):
            # 本地计时仍可用（框架回落本地空闲计时），但委托授权无意义。
            self.power_perm_btn.setEnabled(False)

    def _power_set_enabled(self, on):
        for widget in self._power_widgets():
            widget.setEnabled(bool(on))

    def _power_load_policy(self):
        """读回平台侧策略并回显（首次进入用本机存档兜底）。"""
        bridge = self._power_bridge
        policy = bridge.power_get_policy()
        if policy is None:
            self._power_report("屏幕休眠：策略读取失败（%s）"
                               % (bridge.last_error() or "未知原因"))
            return
        blank = int(policy.get("screenOffMs") or 0)
        suspend = int(policy.get("systemSuspendMs") or 0)
        wake = int(policy.get("wakeInputs") or 0)
        if blank <= 0 and suspend <= 0:
            # 平台侧还没有影子值（进程刚起）：用本机存档兜底并下发一次，
            # 否则「设过一次就丢了」= 选了不兑现。
            blank = self._settings.get_int(self._settings.K_POWER_SCREEN_OFF_MS, 0)
            suspend = self._settings.get_int(self._settings.K_POWER_SUSPEND_MS, 0)
            wake = self._settings.get_int(self._settings.K_POWER_WAKE_INPUTS, 0)
            if blank or suspend or wake:
                # [屏保互斥 2026-11-27 工作流实证] 到期动作=屏保时下发平台的
                # 熄屏超时必须为 0（与 __main__._apply_saved_power_policy /
                # _power_save 同口径）——否则打开一次设置页就把硬件熄屏重新
                # 装填，屏保永远看不到。界面档位仍按存档回显。
                _act = self._settings.get_str(
                    self._settings.K_POWER_BLANK_ACTION,
                    settings.BLANK_ACTION_OFF)
                _ss = _act == settings.BLANK_ACTION_SCREENSAVER
                if bridge.power_set_policy(0 if _ss else blank, suspend, wake):
                    policy = bridge.power_get_policy() or policy
                    # 屏保模式下平台回读恒 0：blank 保留存档档位不回写。
                    if not _ss:
                        blank = int(policy.get("screenOffMs") or 0)
                    suspend = int(policy.get("systemSuspendMs") or 0)
                    wake = int(policy.get("wakeInputs") or 0)
        self._power_set_combo(self.power_blank_combo, blank)
        self._power_set_combo(self.power_suspend_combo, suspend)
        # 到期动作是本机 INI（不经平台电源策略）：熄屏 / 屏保。
        action = self._settings.get_str(self._settings.K_POWER_BLANK_ACTION,
                                        settings.BLANK_ACTION_OFF)
        idx = self.power_blank_action.findData(action)
        self.power_blank_action.setCurrentIndex(idx if idx >= 0 else 0)
        wake = wake or self._power_native.INPUT_ALL
        self.power_wake_mouse.setChecked(bool(wake & self._power_native.INPUT_MOUSE))
        self.power_wake_keyboard.setChecked(
            bool(wake & self._power_native.INPUT_KEYBOARD))
        self.power_wake_touch.setChecked(bool(wake & self._power_native.INPUT_TOUCH))
        self._power_refresh_state()

    @staticmethod
    def _power_set_combo(combo, ms):
        """把毫秒值对到档位下拉（对不上就落「从不」，不静默改语义）。"""
        index = combo.findData(ms)
        if index < 0:
            index = 0
        combo.setCurrentIndex(index)

    def _power_refresh_state(self):
        """刷新状态行（当前态 + 能力摘要）。"""
        if not self._power_ready:
            return
        bridge = self._power_bridge
        native = self._power_native
        caps = self._power_caps
        off = bridge.power_is_screen_off()
        if not caps:
            # 平台一点电源能力都没有：一句话说清，别把同一句说两遍。
            self.power_state.setText("本平台不支持电源管理（能力位为 0）")
            return
        if off:
            current = "已熄屏"
        elif caps & native.CAP_SCREEN_CONTROL:
            current = "当前：正常"
        elif caps & native.CAP_SCREEN_TIMEOUT:
            current = "熄屏由系统委托"
        else:
            current = "熄屏不支持"
        suspend = ("系统挂起支持" if caps & native.CAP_SYSTEM_SUSPEND
                   else "系统挂起不支持")
        # [超时口径] 平台不委托计时时（如 Windows），熄屏阈值由**本应用**
        # 的空闲计时兑现（框架 XPlatformPower_tick 由客户端 1Hz 定时器
        # 驱动，见 __main__._install_power_monitor）——如实说明，别让
        # 用户以为「自动熄屏」在平台上没生效。
        timeout = ("自动熄屏由系统计时" if caps & native.CAP_SCREEN_TIMEOUT
                   else "自动熄屏由本应用计时")
        self.power_state.setText("%s  |  %s  |  %s  |  %s"
                                 % (current, "熄屏支持"
                                    if caps & native.CAP_SCREEN_CONTROL
                                    else "熄屏不支持", suspend, timeout))

    def _power_screen_off(self):
        """立即休眠（成功/失败终态）。

        [到期动作分流 2026-11-26] 选「屏保」⇒ 弹本机动画覆盖层；选「熄屏」
        ⇒ 走既有硬件熄屏（C 桥接）。用户点了「立即休眠」就该立刻看到对应
        动作，不能不管当前选的是什么都去关显示器。
        """
        action = str(self.power_blank_action.currentData()
                     or settings.BLANK_ACTION_OFF)
        if action == settings.BLANK_ACTION_SCREENSAVER:
            # [2026-11-26 实锅] 不能只靠 app._hsf_screensaver：启动装控制器
            # 那段被 except 吞过（或 app 实例不同）⇒ 按钮点了没反应。这里
            # 页面自己懒建一个控制器兜底，只用它 show_now()（不由它管空闲
            # 计时——那是 __main__ 那个控制器的职责，两者互不干扰）。
            from PyQt5.QtWidgets import QApplication
            qapp = QApplication.instance()
            ss = getattr(qapp, "_hsf_screensaver", None)
            if ss is None:
                try:
                    from .screensaver import ScreenSaverController
                    ss = ScreenSaverController(
                        qapp, settings, lambda: self.window())
                    qapp._hsf_screensaver = ss  # noqa: SLF001
                except Exception:                         # noqa: BLE001
                    ss = None
            if ss is not None and ss.show_now():
                self.ctx.set_status("屏保已启动（按键/触摸/鼠标退出）")
            else:
                self._power_report("屏保启动失败：位图缺失或无主窗口")
            return
        if not self._power_init():
            return
        if not self._power_bridge.power_set_screen_off(True):
            self._power_report("熄屏失败：%s"
                               % (self._power_bridge.last_error()
                                  or "平台拒绝"))
            return
        self._power_refresh_state()
        self.ctx.set_status("屏幕已熄屏")

    def _power_screen_on(self):
        """唤醒屏幕（成功/失败终态）。"""
        if not self._power_init():
            return
        if not self._power_bridge.power_set_screen_off(False):
            self._power_report("唤醒失败：%s"
                               % (self._power_bridge.last_error()
                                  or "平台拒绝"))
            return
        self._power_refresh_state()
        self.ctx.set_status("屏幕已唤醒")
    def _power_save(self):
        """保存策略：下发 → 回读 → 回显（失败即失败，不假成功）。"""
        action = str(self.power_blank_action.currentData()
                     or settings.BLANK_ACTION_OFF)
        ss_on = action == settings.BLANK_ACTION_SCREENSAVER
        if not self._power_init():
            # 无 C 电源库：只存到期动作（屏保纯本机动画，不需要平台）。
            self._settings.set_value(self._settings.K_POWER_BLANK_ACTION,
                                     action)
            self._power_refresh_state()
            self.ctx.set_status("到期动作已保存（%s）"
                                % self.power_blank_action.currentText())
            return
        native = self._power_native
        blank = int(self.power_blank_combo.currentData() or 0)
        suspend = int(self.power_suspend_combo.currentData() or 0)
        wake = 0
        if self.power_wake_mouse.isChecked():
            wake |= native.INPUT_MOUSE
        if self.power_wake_keyboard.isChecked():
            wake |= native.INPUT_KEYBOARD
        if self.power_wake_touch.isChecked():
            wake |= native.INPUT_TOUCH
        if wake == 0:
            # 全关 = 熄屏后谁也唤不醒（只能靠外部手段）⇒ 回落「全部」。
            wake = native.INPUT_ALL
            self.power_wake_mouse.setChecked(True)
            self.power_wake_keyboard.setChecked(True)
            self.power_wake_touch.setChecked(True)
        if not (self._power_caps & native.CAP_SYSTEM_SUSPEND) and suspend > 0:
            suspend = 0
            self._power_set_combo(self.power_suspend_combo, 0)
        # [屏保互斥 2026-11-26] 到期动作选「屏保」⇒ 不让平台真关显示器
        # （否则动画看不到）：下发的熄屏超时强制 0，改由本进程覆盖层按同一
        # 时长触发。用户选的「自动熄屏」时长照旧存盘（切回熄屏立刻生效）。
        platform_blank = 0 if ss_on else blank
        if not self._power_bridge.power_set_policy(platform_blank, suspend, wake):
            self._power_report("策略保存失败：%s"
                               % (self._power_bridge.last_error()
                                  or "平台拒绝"))
            return
        self._settings.set_value(self._settings.K_POWER_SCREEN_OFF_MS, blank)
        self._settings.set_value(self._settings.K_POWER_SUSPEND_MS, suspend)
        self._settings.set_value(self._settings.K_POWER_WAKE_INPUTS, wake)
        self._settings.set_value(self._settings.K_POWER_BLANK_ACTION, action)
        # 回读回显：以平台侧回读值为准（不是自己刚传的值）。
        policy = self._power_bridge.power_get_policy()
        if policy is None:
            self._power_report("策略已下发，但回读失败：%s"
                               % (self._power_bridge.last_error()
                                  or "未知原因"))
            return
        if not ss_on:
            # 屏保关：以平台回读为准（真熄屏语义）。
            self._power_set_combo(self.power_blank_combo,
                                  int(policy.get("screenOffMs") or 0))
        # 屏保开：平台回读的 blank 恒 0（互斥），保留用户选的熄屏档位，
        # 别刷成「从不」——关掉屏保后它要照旧生效。
        self._power_set_combo(self.power_suspend_combo,
                              int(policy.get("systemSuspendMs") or 0))
        self._power_refresh_state()
        self.ctx.set_status("自动休眠已保存（%s %s / 挂起 %s）"
                            % (self.power_blank_action.currentText(),
                               self.power_blank_combo.currentText(),
                               self.power_suspend_combo.currentText()))

    def _power_request_permission(self):
        """请求「自动熄屏时长」的平台授权（安卓引导到系统设置）。"""
        if not self._power_init():
            return
        if not self._power_bridge.power_request_permission():
            self._power_report("未请求授权：%s"
                               % (self._power_bridge.last_error()
                                  or "本平台无需授权"))
            return
        self.ctx.set_status("已发起平台授权流程")

    # ------------------------------------------------------------------
    #                       页面生命周期
    # ------------------------------------------------------------------
    def on_enter(self):
        """进入本页：读回电源能力与当前策略（其余项都是本机开关，无需拉取）。"""
        self._power_init()
        self._power_refresh_state()
        self._ss_anim_reload()      # 屏保动画四开关回显（本机 INI）
        # 鼠标三项回显（本机 INI；样式/大小/隐藏）
        try:
            _style = self._settings.get_str(
                self._settings.K_MOUSE_CURSOR_STYLE,
                settings.MOUSE_CURSOR_STYLE_DEF)
            _si = self.mouse_style_combo.findData(_style)
            self.mouse_style_combo.setCurrentIndex(_si if _si >= 0 else 0)
            _zi = self.mouse_size_combo.findData(
                self._settings.get_int(self._settings.K_MOUSE_CURSOR_SIZE, 0))
            self.mouse_size_combo.setCurrentIndex(_zi if _zi >= 0 else 0)
            self.mouse_hidden.setChecked(bool(self._settings.get_bool(
                self._settings.K_MOUSE_CURSOR_HIDDEN, False)))
        except Exception:                             # noqa: BLE001
            pass

    def _apply(self):
        """窗口尺寸「应用」：改窗口 + 落盘（下次启动按它还原）。"""
        win = self.window()
        w, h = self.size_combo.currentData() or self.SIZE_CHOICES[0]
        if w <= 0:
            win.showFullScreen()
            self._settings.set_value(self._settings.K_FULLSCREEN, True)
            self.ctx.set_status("窗口尺寸已应用（全屏，已保存）")
            return
        win.showNormal()
        win.resize(w, h)
        self._settings.set_value(self._settings.K_FULLSCREEN, False)
        self._settings.set_value(self._settings.K_WIN_SIZE, "%dx%d" % (w, h))
        self.ctx.set_status("窗口尺寸已应用（%d×%d，已保存）" % (w, h))


class _AnalogDeviceRow(WrapRow):
    """传感器块内的控件行：**宽窗一行放全**，放不下就**每控件独占一行**。

    [为什么不用 WrapRow 的默认折行] 用户口径是两档（2026-11-23）：
    宽屏「一台设备一行」、竖屏「块内一个控件一行」。默认的「4→3→2→1」
    递减排法会在中间宽度排出 2~3 个一行（既不是设备一行，也不是控件
    一行）⇒ 这里只把「列数怎么选」改成二档，判定用**块的实际可用宽**
    与控件**按字体实测的宽**（不写死窗宽）。

    [复用框架] 宽度量取（_item_width）、按列取最大（_row_need）、摘挂
    重排与防重入（_relayout）、最小宽（minimumSizeHint）全部沿用
    `WrapRow`；resizeEvent/EventFilter 的重排触发也继承 ⇒ 页面缩放后
    自动重排，且 `_relayout` 里「列数没变就早退」⇒ 不会每帧重排。
    """

    def _relayout(self):
        n = len(self._items)
        if n == 0:
            return
        widths = [self._item_width(w) for w in self._items]
        gap = self._lay.spacing()
        avail = max(1, self.width())
        # 两档：一行全放得下 ⇒ 一行；放不下 ⇒ 一列（每控件独占一行）。
        self._max_columns = (
            n if self._row_need(widths, n, gap) <= avail else 1)
        WrapRow._relayout(self)


def _new_analog_channel_combo(default_ch, tooltip):
    """模拟量通道下拉（条目 CH0..CH{n-1} 由 _rebuild_analog_channel_items 建）。

    [通道号统一下拉 2026-11-23 用户口径「通道选择都用统一的下拉框」]
    四台传感器（压差计/温度/压力1/压力2）共用这一种控件。

    [缺省通道钉在下拉上] QComboBox 填完条目会自动选第 0 项 ⇒ 温度/压力1/2
    会悄悄变成 CH0（与当年 QSpinBox 缺省 = min 同一族坑）⇒ 重建条目时
    「没选中过」就落 `_default_ch`。

    [宽度不写死] QComboBox::sizeHint 会遍历**所有条目**取最宽（含箭头与
    内边距），字体随 QSS/缩放走 ⇒ 直接用 Qt 的实测值兜底最小宽
    （_fit_analog_combo_widths），不手填像素。
    """
    combo = QComboBox()
    combo._default_ch = int(default_ch)
    combo.setToolTip(tooltip)
    return combo


# ---- 自动循环实例：取值/归一 + 实例设置卡（2026-11-26 实例化）----
#
# [取数宽松] 回显归一只为**显示**；类型不对/越界落缺省或夹取，不拒——
# SET 面的 fail-closed 校验是**服务端**的事（客户端不重复判，A8f15 既有
# 口径：服务端拒了把原话显示出来）。


def _inst_bool(raw, key, default):
    """实例键取 bool：JSON 的真布尔才认（bool("false") 是 True 的坑）。"""
    v = raw.get(key, default)
    return v if isinstance(v, bool) else (default if default is not None
                                          else False)


def _inst_float(raw, key, default, lo, hi):
    """实例键取 float：非数字落缺省、越界夹取。"""
    try:
        v = float(raw.get(key, default))
    except (TypeError, ValueError):
        return float(default)
    return min(max(v, float(lo)), float(hi))


def _inst_int(raw, key, default, lo, hi):
    """实例键取 int：非整数落缺省、越界夹取（bool 不算整数）。"""
    v = raw.get(key, default)
    if isinstance(v, bool):
        return int(default)
    try:
        v = int(v)
    except (TypeError, ValueError):
        return int(default)
    return min(max(v, int(lo)), int(hi))


def _inst_group_summary(raw):
    """组合实例的一句话摘要：「N 个模式 × M 遍」（组行/组内子组行显示用）。"""
    d = raw if isinstance(raw, dict) else {}
    kids = d.get(proto.AUTO_INST_CHILDREN)
    n = len(kids) if isinstance(kids, (list, tuple)) else 0
    rep = _inst_int(d, proto.AUTO_INST_REPEAT, proto.AUTO_INST_REPEAT_MIN,
                    proto.AUTO_INST_REPEAT_MIN, proto.AUTO_INST_REPEAT_MAX)
    if n <= 0:
        return "尚未添加模式"
    return "%d 个模式 × %d 遍" % (n, rep)


def _inst_trigger_summary(raw):
    """实例**已启用**的切换条件摘要（组合设置卡组内行显示用）。

    只列启用的那些（压差 / 进水压力 / 出水压力 / 进水浊度 / 出水浊度 /
    定时）；一个都没启用 ⇒「未设切换条件」（服务端会拒绝这种实例）。
    """
    d = raw if isinstance(raw, dict) else {}

    def _sign(key):
        return ("<" if str(d.get(key) or "") == proto.AUTO_PRESS_COMPARE_LT
                else ">")

    parts = []
    if _inst_bool(d, proto.CFG_AUTO_STAGE_PRESS_ENABLED, False):
        parts.append("压差%s%.3f MPa" % (
            _sign(proto.CFG_AUTO_STAGE_PRESS_COMPARE),
            _inst_float(d, proto.CFG_AUTO_STAGE_PRESS_THRESHOLD,
                        proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF, 0.0,
                        proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX)))
    # 进水压力/出水压力：协议 MPa、界面 kPa（与设置卡同口径）。
    for label, en_key, th_key, cmp_key in (
            (proto.PRESS1_LABEL, proto.CFG_AUTO_INST_PRESS1_ENABLED,
             proto.CFG_AUTO_INST_PRESS1_THRESHOLD,
             proto.CFG_AUTO_INST_PRESS1_COMPARE),
            (proto.PRESS2_LABEL, proto.CFG_AUTO_INST_PRESS2_ENABLED,
             proto.CFG_AUTO_INST_PRESS2_THRESHOLD,
             proto.CFG_AUTO_INST_PRESS2_COMPARE)):
        if _inst_bool(d, en_key, False):
            mpa = _inst_float(d, th_key, proto.AUTO_CYCLE_INST_THRESHOLD_DEF,
                              0.0, proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX)
            parts.append("%s%s%d kPa" % (label, _sign(cmp_key),
                                         int(round(mpa * 1000.0))))
    for label, en_key, th_key, cmp_key, def_th in (
            (proto.TURB1_LABEL, proto.CFG_AUTO_INST_TURB1_ENABLED,
             proto.CFG_AUTO_INST_TURB1_THRESHOLD,
             proto.CFG_AUTO_INST_TURB1_COMPARE,
             proto.AUTO_CYCLE_TURB1_THRESHOLD_DEF),
            (proto.TURB2_LABEL, proto.CFG_AUTO_INST_TURB2_ENABLED,
             proto.CFG_AUTO_INST_TURB2_THRESHOLD,
             proto.CFG_AUTO_INST_TURB2_COMPARE,
             proto.AUTO_CYCLE_TURB2_THRESHOLD_DEF)):
        if _inst_bool(d, en_key, False):
            parts.append("%s%s%.2f NTU" % (
                label, _sign(cmp_key),
                _inst_float(d, th_key, def_th, 0.0,
                            proto.AUTO_CYCLE_TURB_THRESHOLD_MAX)))
    if _inst_bool(d, proto.CFG_AUTO_STAGE_TIMER_ENABLED, False):
        parts.append("定时%d 分钟" % _inst_int(
            d, proto.CFG_AUTO_STAGE_TIMER_MINUTES,
            proto.AUTO_CYCLE_STAGE_MINUTES_FALLBACK,
            proto.AUTO_CYCLE_MINUTES_MIN, proto.AUTO_CYCLE_MINUTES_MAX))
    return "、".join(parts) if parts else "未设切换条件"


def _inst_base_name(mode_id):
    """实例的**默认名基名**：单模式 = 模式显示名，组合 = 「组合」。

    [默认名口径 2026-11-27] 添加实例时就把这个名字写进 title（同层重名再
    追加数字后缀）；界面显示一律读 title ⇒ 本函数只管"基名"，不做去重。
    """
    mid = str(mode_id or "")
    if mid == proto.AUTO_INST_GROUP:
        return proto.AUTO_INST_GROUP_LABEL
    return str(dict(proto.AUTO_MODES).get(mid, mid or "?"))


def _inst_unique_title(base, taken, limit=None):
    """同层重名 ⇒ 追加数字后缀（组合→组合2→组合3），并夹到长度上限。"""
    limit = int(limit or proto.AUTO_CYCLE_INST_TITLE_MAX)
    base = str(base or "").strip()[:limit]
    if not base:
        return ""
    taken = set(str(x) for x in (taken or ()) if str(x))
    if base not in taken:
        return base
    n = 2
    while True:
        suffix = str(n)
        cand = (base[:max(1, limit - len(suffix))] + suffix).strip()
        if cand not in taken:
            return cand
        n += 1


def _inst_display_name(raw):
    """实例的显示名（title 优先；空 ⇒ 默认名基名）。"""
    d = raw if isinstance(raw, dict) else {}
    title = str(d.get(proto.AUTO_INST_TITLE) or "").strip()
    return title or _inst_base_name(d.get(proto.AUTO_INST_MODE))


def _inst_assign_titles(insts):
    """同层级内按顺序给**没名字**的实例补默认名（重名追加数字后缀）。

    [层级] 顶层实例列表与每个组的 children 各算一层，递归下去（组合可
    嵌套）；已有名字的实例原样保留（服务端回显的名字是事实）。
    """
    # [重名口径 2026-11-27 用户「有两个的时候第一个也要加后缀1」]
    # 同一层里**同类出现 ≥2 次** ⇒ 全部带序号（顺洗模式1/顺洗模式2）；
    # 只有 1 次 ⇒ 裸名。**用户自定义名**（与默认形态不同的 title）不动。
    counts = {}
    for d in insts:
        if not isinstance(d, dict):
            continue
        _m = str(d.get(proto.AUTO_INST_MODE) or "")
        counts[_m] = counts.get(_m, 0) + 1
    taken = set()
    seq = {}
    for d in insts:
        if not isinstance(d, dict):
            continue
        _m = str(d.get(proto.AUTO_INST_MODE) or "")
        base = _inst_base_name(_m)
        cur = str(d.get(proto.AUTO_INST_TITLE) or "").strip()
        n = seq.get(_m, 0) + 1
        seq[_m] = n
        tail = cur[len(base):] if cur.startswith(base) else None
        default_like = (not cur) or (tail is not None
                                    and (tail == "" or tail.isdigit()))
        if counts.get(_m, 1) > 1 and default_like:
            title = "%s%d" % (base, n)
        elif cur:
            title = cur
        else:
            title = base
        if title in taken:
            title = _inst_unique_title(title, taken)
        d[proto.AUTO_INST_TITLE] = title
        if title:
            taken.add(title)
        kids = d.get(proto.AUTO_INST_CHILDREN)
        if isinstance(kids, list):
            _inst_assign_titles(kids)
    return insts


def _coerce_cycle_instance(raw, _depth=1):
    """服务端回显的一个实例 dict → 界面可显示的规整形状（宽松取）。

    未知键保留（服务端将来加键不丢）；devices 逐项取字符串（未知 id
    交给服务端前向兼容口径——界面只按 AUTO_DEVICES 找得到才勾）。

    [组合 2026-11-27] children **递归**规整（子项也可以仍是组合）；嵌套
    超过 AUTO_GROUP_DEPTH_MAX 层的组合子项丢弃（服务端 fail-closed 拒绝
    更深结构，界面不显示非法层）；repeat 夹到 1~99。
    """
    d = dict(raw) if isinstance(raw, dict) else {}
    # [2026-11-27 工作流] 未知键保留要**从原始 dict 起底**：原实现从
    # auto_cycle_instance_default() 的产物起底，而它的键集恰等于
    # AUTO_INST_KEYS ⇒ `key not in out` 恒假，服务端将来新增的实例字段
    # 在「回显→确定」一轮后被静默抹掉（与注释承诺相反）。
    out = d
    defaults = proto.auto_cycle_instance_default(
        str(d.get(proto.AUTO_INST_MODE) or ""))
    for key, value in defaults.items():
        if key not in out:
            out.setdefault(key, value)  # 缺的键用缺省补齐（不覆盖原值）
    out[proto.AUTO_INST_MODE] = str(
        d.get(proto.AUTO_INST_MODE) or out[proto.AUTO_INST_MODE])
    out[proto.AUTO_INST_DEVICES] = [
        str(x) for x in (d.get(proto.AUTO_INST_DEVICES) or [])
        if isinstance(x, (str, int))]
    # [组合子序列] 当前实例在**第 _depth 层**（顶层=1）；它的组合子项在
    # _depth+1 层 ⇒ 只有 _depth < AUTO_GROUP_DEPTH_MAX 时才允许存在。
    kids = d.get(proto.AUTO_INST_CHILDREN)
    if isinstance(kids, (list, tuple)):
        out[proto.AUTO_INST_CHILDREN] = [
            _coerce_cycle_instance(item, _depth + 1)
            for item in kids
            if isinstance(item, dict)
            and not (_depth >= proto.AUTO_GROUP_DEPTH_MAX
                     and str(item.get(proto.AUTO_INST_MODE) or "").strip()
                     == proto.AUTO_INST_GROUP)]
    else:
        out[proto.AUTO_INST_CHILDREN] = []
    out[proto.AUTO_INST_REPEAT] = _inst_int(
        d, proto.AUTO_INST_REPEAT, proto.AUTO_INST_REPEAT_MIN,
        proto.AUTO_INST_REPEAT_MIN, proto.AUTO_INST_REPEAT_MAX)
    for key, lo, hi in (
            (proto.CFG_AUTO_STAGE_PRESS_THRESHOLD, 0.0,
             proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX),
            (proto.CFG_AUTO_INST_PRESS1_THRESHOLD, 0.0,
             proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX),
            (proto.CFG_AUTO_INST_PRESS2_THRESHOLD, 0.0,
             proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX)):
        out[key] = _inst_float(d, key, out[key], lo, hi)
    out[proto.CFG_AUTO_STAGE_TIMER_MINUTES] = _inst_int(
        d, proto.CFG_AUTO_STAGE_TIMER_MINUTES,
        out[proto.CFG_AUTO_STAGE_TIMER_MINUTES],
        proto.AUTO_CYCLE_MINUTES_MIN, proto.AUTO_CYCLE_MINUTES_MAX)
    out[proto.AUTO_INST_STIR_RPM] = _inst_int(
        d, proto.AUTO_INST_STIR_RPM, out[proto.AUTO_INST_STIR_RPM],
        proto.AUTO_STIR_RPM_MIN, proto.AUTO_STIR_RPM_MAX)
    out[proto.AUTO_MODE_STIR_ACCEL_RPM_S] = _inst_int(
        d, proto.AUTO_MODE_STIR_ACCEL_RPM_S,
        out[proto.AUTO_MODE_STIR_ACCEL_RPM_S],
        proto.AUTO_STIR_ACCEL_RPM_S_MIN, proto.AUTO_STIR_ACCEL_RPM_S_MAX)
    for key in (proto.CFG_AUTO_STAGE_PRESS_ENABLED,
                proto.CFG_AUTO_INST_PRESS1_ENABLED,
                proto.CFG_AUTO_INST_PRESS2_ENABLED,
                proto.CFG_AUTO_STAGE_TIMER_ENABLED,
                proto.AUTO_INST_STIR_REVERSE,
                proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP):
        out[key] = _inst_bool(d, key, out[key])
    for key in (proto.CFG_AUTO_STAGE_PRESS_COMPARE,
                proto.CFG_AUTO_INST_PRESS1_COMPARE,
                proto.CFG_AUTO_INST_PRESS2_COMPARE):
        val = str(d.get(key) or out[key])
        out[key] = (val if val in proto.AUTO_PRESS_COMPARE_KEYS
                    else proto.AUTO_CYCLE_PRESS_COMPARE_DEF)
    for key in (proto.AUTO_MODE_STIR_SWITCH, proto.AUTO_MODE_INLET_SWITCH):
        val = str(d.get(key) or out[key])
        out[key] = (val if val in proto.AUTO_SWITCH_POLICY_IDS
                    else proto.AUTO_SWITCH_POLICY_DEF)
    return out


class _SpecialValueSelectAll(QObject):
    """带 setSpecialValueText 的 QSpinBox：按下时若正显示特殊值文本，
    单次按下即全选可编辑数字（2026-11-26 Windows 物理键盘打不进根修）。

    [为什么需要] 点击不会清掉特殊值文本（「跟随变频器」「关闭」「不限」），
    此时物理键盘直接打字被 QSpinBox validator 拒绝（它拿新字符拼到特殊值
    文本后面解析），用户看到的就是「键盘无法输入」。板端软键盘路径早已在
    按下时 selectAll 所以没暴露。这里对所有输入来源生效，singleShot(0)
    让 Qt 先完成默认的放光标处理，再全选——否则选区会被清掉（同
    softkeyboard 的触摸全选口径）。
    """

    def eventFilter(self, obj, event):
        if event.type() == QEvent.MouseButtonPress:
            spin = obj if isinstance(obj, QSpinBox) else None
            if spin is None and isinstance(obj, QLineEdit):
                parent = obj.parent()
                spin = parent if isinstance(parent, QSpinBox) else None
            if spin is not None and spin.isEnabled() and not spin.isReadOnly():
                # 特殊值文本在位（value == minimum 且文本不含数字）时全选
                text = spin.lineEdit().text()
                if text and not any(ch.isdigit() for ch in text):
                    QTimer.singleShot(0, spin.selectAll)
        return False


class _CycleDialogBase(QDialog):
    """循环实例 / 组合设置卡的**共用外壳**（2026-11-27 抽出）。

    [为什么抽出来] 组合设置卡与实例设置卡是同一族弹卡（板端要求完全一致
    的观感与手势：无边框 + 自绘标题栏 + 拖动 + 父窗内居中 + 触摸滚动 +
    确定/取消）；拆之前两张卡各抄一份 ⇒ 改一处忘另一处就是两副面孔。

    [Windows 端口径] Tool+无边框 ⇒ 不进任务栏，**窗口可正常激活**
    （硬件键盘/系统输入法照常打字——2026-11-26 用户实测：无条件挂
    DoesNotAcceptFocus（相机卡配方）会让 Windows 无法激活窗口 ⇒
    键盘打不进，已订正为 **X11 限定**）。
    [板端（X11）口径] 弹卡默认走内嵌浮层（overlay_exec，不建独立 X
    窗口）；这里保留的 X11 旗标只作直开弹卡的兜底：不抢激活（xfce 面板
    不会抬上来）+ bypass + 置顶，输入走软键盘程序化注入（_target 引用，
    不依赖系统焦点）。
    """

    #: 内嵌浮层态（overlay_exec 置位）
    _overlay_mode = False
    _overlay_wrap = None

    def __init__(self, parent, head_text, min_w=420, min_h=380,
                 width=880, height=720):
        QDialog.__init__(self, parent)
        self.setWindowTitle(head_text)
        # [无边框 2026-11-26 用户「这个设置界面用无边框的，并且可以在大点」]
        # 自绘标题栏（标题 + ✕）替代系统框；拖动走标题栏区域（见
        # mousePressEvent / touchEvent）。尺寸给足（内容行多，滚动壳兜底矮屏）。
        flags = Qt.Tool | Qt.FramelessWindowHint
        try:
            from .softkeyboard import _is_x11
            if _is_x11():
                flags |= (Qt.WindowDoesNotAcceptFocus
                          | Qt.X11BypassWindowManagerHint
                          | Qt.WindowStaysOnTopHint)
        except ImportError:                         # pragma: no cover
            pass
        self.setWindowFlags(flags)
        # [触摸屏拖动 2026-11-26 用户「拖动手势你也没做，我的设备有触摸屏」]
        # 对话框接收原生 QTouchEvent（否则触摸只合成鼠标、部分触控栈不派
        # 发到无焦点弹窗 ⇒ 拖不动）；手势在 touchEvent 里实现（标题栏内
        # 开始才接管，其余区域放行给滚动/控件）。
        self.setAttribute(Qt.WA_AcceptTouchEvents, True)
        self._drag_offset = None
        # [最小尺寸给夹取让路] 缺省 880×720，showEvent 里夹到父窗口内；
        # minimum 只是布局下限，父窗更小时「不超过父窗」优先。
        # [记下构造下限] 浮层态会临时 setFixedSize，退出时要恢复成这组
        # min/max（否则后续 exec_ 路径被钉死）。
        self._base_min_w, self._base_min_h = int(min_w), int(min_h)
        self.setMinimumSize(min_w, min_h)
        self.resize(width, height)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(16, 14, 16, 12)
        outer.setSpacing(8)
        self._outer = outer
        # [自绘标题栏行] 标题占满、右侧 ✕ 关闭；整行都是拖动热区（✕ 除外）。
        # [标题写实例名 2026-11-26 用户「名字写到这，不然不知道是啥」]
        # 同类模式可重复添加，光写模式名分不清在编辑哪一个 ⇒ 标题栏写
        # 调用方给的**实例名/组名**；直构（回归）不传 title 时回退模式名。
        self._title_head_text = head_text
        self._title_bar = QWidget()
        bar = QHBoxLayout(self._title_bar)
        bar.setContentsMargins(0, 0, 0, 0)
        bar.setSpacing(8)
        head = QLabel(head_text)
        head.setStyleSheet(
            "QLabel { color: palette(text); background: transparent;"
            " font-weight: bold; font-size: %dpx; }" % theme.font_px(16))
        close_btn = QPushButton("×")
        # [30px + 显式字号] 28px 框装不下主题字号的 ✕（U+2715 缺字形/被
        # 裁成"、"观感，2026-11-26 截图实证）⇒ 换全字体都有的 ×、放大
        # 到 30px 并钉字号。
        close_btn.setFixedSize(30, 30)
        close_btn.setStyleSheet(
            "QPushButton { font-size: %dpx; padding: 0; }"
            % theme.font_px(16))
        close_btn.setToolTip("关闭（不保存）")
        close_btn.clicked.connect(self.reject)
        bar.addWidget(head, 1)
        bar.addWidget(close_btn)
        outer.addWidget(self._title_bar)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        # [触摸垂直拖动＝滚动内容 2026-11-26 用户「触摸垂直拖动也无效」]
        # 与设备健康弹卡同款：触摸合成鼠标拖动驱动 QScroller 手势（带惯
        # 性）；这版 PyQt5 导出不了 QScroller 时回落 flick.FlickScroller
        # （导航面板同款的手动惯性滚动器）。不挂手势时 Qt5 的 QScrollArea
        # 只认滚轮，触摸屏上内容拖不动。
        # [TouchGesture 而非 LeftMouseButtonGesture 2026-11-26 实测]
        # LeftMouseButton 会让 QScroller **接管视口的鼠标按下**做滚动识别
        # ⇒ 子控件（微调框 lineEdit）拿不到按下＝拿不到焦点 ⇒ 弹卡里
        # 所有的框都打不了字（探针实证）。TouchGesture 只吃原生触摸，
        # 鼠标点击/焦点路径完全不受影响；桌面鼠标用滚轮。
        try:
            from PyQt5.QtWidgets import QScroller as _QScroller
            _QScroller.grabGesture(scroll.viewport(),
                                   _QScroller.TouchGesture)
        except (ImportError, AttributeError, RuntimeError):
            try:
                from .flick import FlickScroller as _Flick
                self._flick = _Flick(scroll)
            except Exception:                   # noqa: BLE001
                self._flick = None
        body = QWidget()
        v = QVBoxLayout(body)
        v.setContentsMargins(0, 0, 4, 0)
        v.setSpacing(6)
        self._body = v
        scroll.setWidget(body)
        # [不能叫 self.scroll] QWidget 自带 scroll(dx,dy) 方法 ⇒ 实例属性
        # 要起别的名（回归断言也要用这个名）。
        self.scroll_area = scroll
        outer.addWidget(scroll, 1)

    def _finish_shell(self):
        """内容铺完后收尾：底部 确定/取消 + 关 autoDefault + 应用主题。"""
        btns = QHBoxLayout()
        btns.addStretch(1)
        self.ok_btn = QPushButton("确定")
        # [2026-11-26 Windows 物理键盘打不进根修] QDialog 默认按钮会在焦点
        # 变化时把焦点抢到「确定」上（实测：点加速度框后 focusWidget 是
        # QPushButton「确定」）⇒ 输入框永远拿不到焦点，键盘打不进。板端
        # 走软键盘程序化 setFocus 所以没暴露。关掉所有按钮的 autoDefault
        # 与 default，焦点就老老实实落在被点的输入框。
        self.ok_btn.setDefault(False)
        self.ok_btn.setAutoDefault(False)
        self.ok_btn.setMinimumWidth(96)
        self.ok_btn.clicked.connect(self.accept)
        self.cancel_btn = QPushButton("取消")
        self.cancel_btn.setAutoDefault(False)
        self.cancel_btn.setMinimumWidth(96)
        self.cancel_btn.clicked.connect(self.reject)
        btns.addWidget(self.ok_btn)
        btns.addWidget(self.cancel_btn)
        self._outer.addLayout(btns)
        for b in self.findChildren(QPushButton):
            b.setAutoDefault(False)
        theme.apply_window(self)

    def _hit_title(self, pos):
        """pos（本窗口坐标）落在标题栏行且不在 ✕ 按钮上 ⇒ 可拖。

        标题文字/空白都算热区；✕ 按钮自己收事件、不进拖动。
        """
        if not self._title_bar.geometry().contains(pos):
            return False
        return not isinstance(self.childAt(pos), QPushButton)

    def overlay_exec(self):
        """X11（板端）弹卡口径：作为**主窗内嵌浮层**弹出，不建独立 X 窗口。

        [为什么] 独立顶层窗口在板端 xfwm4 下只有两难：可聚焦 ⇒ 抢激活，
        xfce 面板「认为没有全屏窗在用」自己抬上来（用户「顶部出现系统
        任务栏」）；不可聚焦（相机卡配方）⇒ 系统屏幕键盘打不进输入框
        （用户「系统的屏幕键盘无法输入」）。内嵌层**没有独立 X 窗口**，
        两个问题同时消失：不惊动面板，系统键盘照常打字（软键盘的按压
        绑定路径也照常）。Windows 不走这条路（exec_ 的工具窗已符合预期）。

        :returns: True＝确定（Accepted）；False＝取消/点外部关闭。
        """
        from .softkeyboard import _is_x11
        if not _is_x11():
            return self.exec_() == QDialog.Accepted
        main = self.parentWidget().window() if self.parentWidget() else None
        if main is None:                     # 回归直构（无主窗）⇒ 原路
            return self.exec_() == QDialog.Accepted
        self._overlay_mode = True
        wrap = QWidget(main)                 # 铺满主窗的浮层底板（半透明）
        wrap.setGeometry(main.rect())
        wrap.setAttribute(Qt.WA_StyledBackground, True)
        # [ID 选择器只刷底板自己] 写成 `QWidget {…}` 会**级联**给卡片里
        # 所有子控件（没自带 QSS 的标签/勾选框全被刷成半透明黑——
        # widgets.py 的老坑新犯，2026-11-26 板端截图实证）。
        wrap.setObjectName("HsfCycleOverlay")
        wrap.setStyleSheet(
            "#HsfCycleOverlay { background: rgba(0, 0, 0, 35%); }")
        wrap.installEventFilter(self)
        self.setParent(wrap)
        self.setWindowFlags(Qt.Widget)       # 降级为普通子控件
        w = min(880, max(self.minimumWidth(), main.width()))
        h = min(720, max(self.minimumHeight(), main.height()))
        self.resize(w, h)
        # [板端卡片残缺根修 2026-11-27 用户「组合设置的卡片太小了，看看普通
        # 卡片大小」] 弹卡塞进浮层布局（AlignCenter）后，QBoxLayout 按控件
        # **sizeHint** 分配几何（QDialog 默认 Preferred 策略），上面这行
        # resize(880,720) 被忽略 ⇒ 内容少的组合设置卡缩成 ~664×515，而
        # 内容多的普通实例卡显得大（同一外壳两副尺寸）。入布局前钉住尺寸，
        # 退出浮层后把 max 放开、min 还原（不影响后续 exec_ 路径）。
        self.setFixedSize(w, h)
        lay = QVBoxLayout(wrap)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self, 0, Qt.AlignCenter)
        self._overlay_wrap = wrap
        wrap.show()
        self.show()
        self.raise_()
        self.setFocus()
        loop = QEventLoop(self)
        res = {"ok": False}
        self.accepted.connect(lambda: (res.__setitem__("ok", True),
                                       loop.quit()))
        self.rejected.connect(loop.quit)
        loop.exec_()
        # [恢复尺寸约束] 浮层里 setFixedSize 过；放开 max、还原构造下限
        # （弹卡对象在 wrap 删除前仍可被调用方读值）。
        self.setMaximumSize(16777215, 16777215)
        self.setMinimumSize(self._base_min_w, self._base_min_h)
        wrap.deleteLater()
        self._overlay_mode = False
        self._overlay_wrap = None
        return res["ok"]

    def eventFilter(self, obj, event):
        # [点浮层底板＝取消] 点在卡片几何之外（半透明底板上）⇒ 关闭，
        # 与「点外部关闭」弹卡的既有习惯一致。
        if (self._overlay_mode
                and obj is getattr(self, "_overlay_wrap", None)
                and event.type() == QEvent.MouseButtonPress
                and not self.geometry().contains(event.pos())):
            self.reject()
        return QDialog.eventFilter(self, obj, event)

    def mousePressEvent(self, event):
        # [无边框拖动·鼠标] 按在标题栏行才拖；其余区域交给 Qt 默认。
        if event.button() == Qt.LeftButton and self._hit_title(event.pos()):
            self._drag_offset = (event.globalPos()
                                 - self.frameGeometry().topLeft())
        QDialog.mousePressEvent(self, event)

    def mouseMoveEvent(self, event):
        if self._drag_offset is not None \
                and (event.buttons() & Qt.LeftButton):
            self.move(event.globalPos() - self._drag_offset)
        QDialog.mouseMoveEvent(self, event)

    def mouseReleaseEvent(self, event):
        self._drag_offset = None
        QDialog.mouseReleaseEvent(self, event)

    def touchEvent(self, event):
        """[无边框拖动·触摸屏 2026-11-26 用户「拖动手势你也没做」]

        原生触摸手势：**主触点落在标题栏内**才接管拖动（TouchBegin 记
        「屏坐标 − 窗口左上」的偏移，Update 按「触点屏坐标 − 偏移」移窗，
        TouchEnd 收手）；标题栏之外 TouchBegin 直接 ignore ⇒ 触摸照常喂
        给内部控件/滚动区。屏幕坐标全程一致，窗口移动不影响差分参考。
        """
        et = event.type()
        if et in (QEvent.TouchBegin, QEvent.TouchUpdate, QEvent.TouchEnd):
            pts = event.touchPoints()
            if not pts:
                event.ignore()
                return
            if et == QEvent.TouchBegin:
                if self._hit_title(pts[0].pos().toPoint()):
                    self._drag_offset = (pts[0].screenPos().toPoint()
                                         - self.frameGeometry().topLeft())
                    event.accept()
                    return
                event.ignore()
                return
            if self._drag_offset is not None:
                self.move(pts[0].screenPos().toPoint() - self._drag_offset)
                if et == QEvent.TouchEnd:
                    self._drag_offset = None
                event.accept()
                return
            event.ignore()
            return
        QDialog.touchEvent(self, event)

    def _fit_to_parent(self, geo=None):
        """尺寸夹到父窗口内并居中（2026-11-26 用户「弹出的卡片在父窗口
        居中位置，大小不要超过父窗口」）。

        :param geo: 测试注入的父窗几何（QRect）；None 取 parentWidget 的
            顶层窗口几何，没有父窗（回归直构不挂父）就不动。
        """
        if geo is None:
            pw = self.parentWidget()
            if pw is not None:
                geo = pw.window().geometry()
        if geo is None or geo.width() <= 0 or geo.height() <= 0:
            return
        w = min(self.width(), geo.width())
        h = min(self.height(), geo.height())
        self.resize(w, h)
        self.move(geo.x() + (geo.width() - w) // 2,
                  geo.y() + (geo.height() - h) // 2)

    def showEvent(self, event):
        # 每次弹出都重新夹（exec_ 复用实例时上次的尺寸可能超界）；
        # 内嵌浮层态不夹（尺寸/居中已由 wrap 布局定，夹了会铺满底板）。
        if not self._overlay_mode:
            self._fit_to_parent()
        QDialog.showEvent(self, event)


class CycleInstanceDialog(_CycleDialogBase):
    """单个循环实例的设置卡（远端设置·自动循环实例行的「设置」）。

    [QDialog + 滚动壳] 内容行多（设备 / 压差 / 压力1 / 压力2 / 定时 /
    转速 / 方向 / 加速度 / 阀复位 / 换相策略），矮窗放不下 ⇒ 内层
    QScrollArea 兜底（滚动条出现，不裁控件）。

    [阈值单位] 压差沿用 MPa 三位小数（与既有压差触发同款）；压力1/压力2
    按 protect 惯例「界面 kPa 整数、提交 ÷1000 成 MPa」——协议一律 MPa。

    [同通道压力保护提示] 压力1/压力2 触发与「运行保护」的压力过高保护读
    同一通道：protect 启用时超限会先停机并锁存（效果上优先于循环切相）
    ⇒ 行旁给一句提示，别让现场以为两套互斥。
    """

    def __init__(self, data, stir_max=None, parent=None, title=None,
                 allow_group=False, title_suffix=None):
        # [以实例数据起控件] data 是行里那份**规整过的** dict（回显走
        # _coerce_cycle_instance；新增走 proto 缺省构造器）⇒ 直接读。
        d = data if isinstance(data, dict) else {}
        # [未知键保留] values() 以原始 dict 为底、只覆盖本卡管的键 ⇒
        # 服务端将来加的键在「设置→确定」一轮后不丢（children/repeat 也
        # 原样保留 ⇒ 组合的载荷不会被本卡抹掉）。
        self._raw = dict(d)
        self._mode_id = str(d.get(proto.AUTO_INST_MODE) or "")
        # [组合 2026-11-27] 组实例（mode=group）的完整编辑入口是
        # CycleGroupDialog ⇒ 本卡默认 allow_group=False（组数据只透传，
        # values 不会改它的 mode/children/repeat）。
        self.allow_group = bool(allow_group)
        mode_text = dict(proto.AUTO_MODES).get(self._mode_id,
                                               self._mode_id or "实例")
        suffix = ("（循环顺序里的一步）" if title_suffix is None
                  else str(title_suffix))
        # [标题写实例名 2026-11-26 用户「名字写到这，不然不知道是啥」]
        # 同类模式可重复添加，光写模式名分不清在编辑哪一个 ⇒ 标题栏写
        # 调用方给的**实例名**；直构（回归）不传 title 时回退模式名。
        _CycleDialogBase.__init__(
            self, parent,
            "循环实例设置 · %s%s" % (title or mode_text, suffix))
        v = self._body

        # ---- 名称（同层重名由保存步去重：追加数字后缀）----
        # [默认名 2026-11-27] 添加实例时就已写好 title（产水模式/产水模式2/
        # 组合…）；这里可以改，留空 ⇒ 保存时回落到默认名基名。
        name_row = QHBoxLayout()
        name_row.setSpacing(8)
        lbl_name = QLabel("名称")
        lbl_name.setMinimumWidth(110)
        lbl_name.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        name_row.addWidget(lbl_name)
        self.name_edit = QLineEdit(str(d.get(proto.AUTO_INST_TITLE) or ""))
        self.name_edit.setMaxLength(proto.AUTO_CYCLE_INST_TITLE_MAX)
        self.name_edit.setPlaceholderText(
            "留空＝自动命名（%s）" % _inst_base_name(self._mode_id))
        self.name_edit.setToolTip(
            "实例名称（最多 %d 字）：留空＝按默认名（%s）；与同层其他实例"
            "重名时保存会自动追加数字后缀。"
            % (proto.AUTO_CYCLE_INST_TITLE_MAX,
               _inst_base_name(self._mode_id)))
        self.name_edit.setMinimumWidth(180)
        self.name_edit.setMaximumWidth(420)
        name_row.addWidget(self.name_edit)
        name_row.addStretch(1)
        v.addLayout(name_row)

        # ---- 启动设备（多选；与模式块同款 MultiCheckCombo）----
        v.addWidget(section_title("启动设备"))
        self.devices = MultiCheckCombo(
            [t for _i, t in proto.AUTO_DEVICES], all_text="全部设备",
            empty_text="未选择", initial="none", min_checked=0)
        self.devices.setMinimumWidth(150)
        self.devices.setMaximumWidth(420)
        self.devices.setToolTip("勾选该实例要启动的设备（可多选；点外面收起）")
        id_to_idx = {dev_id: i for i, (dev_id, _t) in
                     enumerate(proto.AUTO_DEVICES)}
        self.devices.set_checked_indexes(
            id_to_idx[x] for x in (d.get(proto.AUTO_INST_DEVICES) or [])
            if x in id_to_idx)
        dev_row = QHBoxLayout()
        dev_row.setSpacing(8)
        lbl_dev = QLabel("启动设备")
        lbl_dev.setMinimumWidth(110)
        lbl_dev.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        dev_row.addWidget(lbl_dev)
        dev_row.addWidget(self.devices)
        dev_row.addStretch(1)
        v.addLayout(dev_row)

        # ---- 触发条件（压差 / 压力1 / 压力2 / 定时；至少启用其一）----
        v.addWidget(section_title("切换条件（六选至少其一，先到先切）"))

        def _press_group(title, en_key, th_key, cmp_key, is_press12,
                         is_turb=False):
            """一组触发条件：启用勾选 + 阈值 + 方向（组内联动启停）。"""
            en = QCheckBox("启用%s" % title)
            en.setChecked(_inst_bool(d, en_key, False))
            en.setToolTip("勾上＝本实例用<b>%s</b>作为切换条件" % title)
            if is_turb:
                # [浊度 2026-11-27] 单位 NTU，两位小数（0.01 NTU 分辨），
                # 协议也是 NTU 原值（不像压力要 kPa→MPa 换算）。
                th = QDoubleSpinBox()
                th.setRange(0.0, proto.AUTO_CYCLE_TURB_THRESHOLD_MAX)
                th.setDecimals(2)
                th.setSingleStep(0.1)
                th.setSuffix(" NTU")
                th.setValue(_inst_float(
                    d, th_key,
                    (proto.AUTO_CYCLE_TURB1_THRESHOLD_DEF if "turb1" in th_key
                     else proto.AUTO_CYCLE_TURB2_THRESHOLD_DEF),
                    0.0, proto.AUTO_CYCLE_TURB_THRESHOLD_MAX))
            elif is_press12:
                # [压力1/压力2 界面 kPa、协议 MPa] protect 惯例：整数 kPa
                # 好读好敲，提交 ÷1000（1000 kPa = 1.0 MPa）。
                th = QSpinBox()
                th.setRange(0, int(proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX
                                   * 1000))
                th.setSingleStep(10)
                th.setSuffix(" kPa")
                th.setValue(int(round(
                    _inst_float(d, th_key,
                                proto.AUTO_CYCLE_INST_THRESHOLD_DEF,
                                0.0, proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX)
                    * 1000.0)))
            else:
                # [压差沿用 MPa 三位小数] 与既有压差触发/首页压差卡同口径。
                th = QDoubleSpinBox()
                th.setRange(0.0, proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX)
                th.setDecimals(3)
                th.setSingleStep(0.05)
                th.setSuffix(" MPa")
                th.setValue(_inst_float(
                    d, th_key, proto.AUTO_CYCLE_PRESS_THRESHOLD_DEF,
                    0.0, proto.AUTO_CYCLE_PRESS_THRESHOLD_MAX))
            th.setToolTip("%s阈值：读数按方向越过它时切到下一实例" % title)
            cmp_box = QComboBox()
            for key, text in proto.AUTO_PRESS_COMPARE_LABELS:
                cmp_box.addItem(text, key)
            cmp_box.setToolTip("%s<b>大于</b>阈值才切，还是<b>小于</b>阈值才切"
                               % title)
            val = str(d.get(cmp_key, proto.AUTO_CYCLE_PRESS_COMPARE_DEF))
            idx = cmp_box.findData(val)
            cmp_box.setCurrentIndex(idx if idx >= 0 else 0)
            en.toggled.connect(th.setEnabled)
            en.toggled.connect(cmp_box.setEnabled)
            th.setEnabled(en.isChecked())
            cmp_box.setEnabled(en.isChecked())
            hint = None
            if is_press12:
                hint = QLabel("同通道若启用了压力过高保护，超限会先停机并"
                              "锁存")
                hint.setStyleSheet(theme.caption_label_qss())
            row = wrap_row([en, label_group("阈值", th), cmp_box])
            return {"enable": en, "threshold": th, "compare": cmp_box,
                    "row": row, "hint": hint}

        self.press = _press_group("压差切换",
                                  proto.CFG_AUTO_STAGE_PRESS_ENABLED,
                                  proto.CFG_AUTO_STAGE_PRESS_THRESHOLD,
                                  proto.CFG_AUTO_STAGE_PRESS_COMPARE, False)
        self.press1 = _press_group(
            "%s切换" % proto.PRESS1_LABEL,
            proto.CFG_AUTO_INST_PRESS1_ENABLED,
            proto.CFG_AUTO_INST_PRESS1_THRESHOLD,
            proto.CFG_AUTO_INST_PRESS1_COMPARE, True)
        self.press2 = _press_group(
            "%s切换" % proto.PRESS2_LABEL,
            proto.CFG_AUTO_INST_PRESS2_ENABLED,
            proto.CFG_AUTO_INST_PRESS2_THRESHOLD,
            proto.CFG_AUTO_INST_PRESS2_COMPARE, True)
        self.turb1 = _press_group(
            "%s切换" % proto.TURB1_LABEL,
            proto.CFG_AUTO_INST_TURB1_ENABLED,
            proto.CFG_AUTO_INST_TURB1_THRESHOLD,
            proto.CFG_AUTO_INST_TURB1_COMPARE, False, is_turb=True)
        self.turb2 = _press_group(
            "%s切换" % proto.TURB2_LABEL,
            proto.CFG_AUTO_INST_TURB2_ENABLED,
            proto.CFG_AUTO_INST_TURB2_THRESHOLD,
            proto.CFG_AUTO_INST_TURB2_COMPARE, False, is_turb=True)
        for grp in (self.press, self.press1, self.press2,
                    self.turb1, self.turb2):
            v.addWidget(grp["row"])
            if grp["hint"] is not None:
                v.addWidget(grp["hint"])
        # ---- 定时切换 ----
        self.timer_enable = QCheckBox("启用定时切换")
        self.timer_enable.setToolTip("勾上＝本实例到点就切下一实例")
        self.timer_min = QSpinBox()
        self.timer_min.setRange(proto.AUTO_CYCLE_MINUTES_MIN,
                                proto.AUTO_CYCLE_MINUTES_MAX)
        self.timer_min.setSuffix(" 分钟")
        self.timer_min.setValue(_inst_int(
            d, proto.CFG_AUTO_STAGE_TIMER_MINUTES,
            proto.AUTO_CYCLE_STAGE_MINUTES_FALLBACK,
            proto.AUTO_CYCLE_MINUTES_MIN, proto.AUTO_CYCLE_MINUTES_MAX))
        self.timer_min.setToolTip("本实例的运行时长（到点切下一个实例）")
        self.timer_enable.toggled.connect(self.timer_min.setEnabled)
        self.timer_min.setEnabled(self.timer_enable.isChecked())
        self.timer_enable.setChecked(_inst_bool(
            d, proto.CFG_AUTO_STAGE_TIMER_ENABLED, False))
        v.addWidget(wrap_row([self.timer_enable,
                              label_group("时长", self.timer_min)]))

        # ---- 搅拌电机（转速 / 方向 / 加速度；与模式块同款）----
        v.addWidget(section_title("搅拌电机"))
        rpm_row = QHBoxLayout()
        rpm_row.setSpacing(8)
        lbl_rpm = QLabel("搅拌电机")
        lbl_rpm.setMinimumWidth(110)
        lbl_rpm.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        rpm_row.addWidget(lbl_rpm)
        self.rpm = QSpinBox()
        # [上限压到搅拌最高转速] 服务端统一钳位；界面不让人白填
        # （pages_other 的 _limit_blocks 同款口径）。
        self.rpm.setRange(proto.AUTO_STIR_RPM_MIN,
                          int(stir_max or proto.AUTO_STIR_MAX_RPM_DEF))
        self.rpm.setValue(_inst_int(d, proto.AUTO_INST_STIR_RPM,
                                    proto.AUTO_STIR_RPM_DEF,
                                    proto.AUTO_STIR_RPM_MIN,
                                    proto.AUTO_STIR_RPM_MAX))
        self.rpm.setSuffix(" rpm")
        rpm_row.addWidget(self.rpm)
        self.hz_hint = QLabel("")
        self.hz_hint.setStyleSheet(theme.caption_label_qss())

        def _sync_hz_hint(val, _lbl=None):
            self.hz_hint.setText("折算 ≈ %.1f Hz" % (
                val / proto.AUTO_STIR_RATED_RPM * proto.AUTO_STIR_RATED_HZ))

        self.rpm.valueChanged.connect(_sync_hz_hint)
        _sync_hz_hint(self.rpm.value())
        self.reverse = QComboBox()
        self.reverse.addItem("正转", False)
        self.reverse.addItem("反转", True)
        rev = d.get(proto.AUTO_INST_STIR_REVERSE)
        _ridx = self.reverse.findData(bool(rev)) if isinstance(rev, bool) \
            else 0
        self.reverse.setCurrentIndex(_ridx if _ridx >= 0 else 0)
        rpm_grp = QWidget()
        rpm_lay = QHBoxLayout(rpm_grp)
        rpm_lay.setContentsMargins(0, 0, 0, 0)
        rpm_lay.setSpacing(6)
        rpm_lay.addWidget(self.rpm)
        rpm_lay.addWidget(self.hz_hint)
        dir_grp = QWidget()
        dir_lay = QHBoxLayout(dir_grp)
        dir_lay.setContentsMargins(0, 0, 0, 0)
        dir_lay.setSpacing(6)
        dir_lbl = QLabel("方向")
        dir_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        dir_lay.addWidget(dir_lbl)
        dir_lay.addWidget(self.reverse)
        rpm_row.addWidget(wrap_row([rpm_grp, dir_grp]), 1)
        v.addLayout(rpm_row)
        # 加速度（0 = 跟随变频器当前加速时间，不写 0xF011）
        accel_row = QHBoxLayout()
        accel_row.setSpacing(8)
        lbl_acc = QLabel("加速度")
        lbl_acc.setMinimumWidth(110)
        lbl_acc.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        accel_row.addWidget(lbl_acc)
        self.accel = QSpinBox()
        self.accel.setRange(proto.AUTO_STIR_ACCEL_RPM_S_MIN,
                            proto.AUTO_STIR_ACCEL_RPM_S_MAX)
        self.accel.setSingleStep(10)
        self.accel.setSuffix(" rpm/s")
        self.accel.setSpecialValueText("跟随变频器")
        self.accel.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        self.accel.setMinimumWidth(180)
        self.accel.setToolTip(
            "搅拌变频器加速度（rpm/s）：从 0 加到本实例设定转速的速率。\n"
            "0 = 跟随变频器当前加速时间（不写参数）。")
        self.accel.setValue(_inst_int(
            d, proto.AUTO_MODE_STIR_ACCEL_RPM_S,
            proto.AUTO_STIR_ACCEL_RPM_S_DEF,
            proto.AUTO_STIR_ACCEL_RPM_S_MIN, proto.AUTO_STIR_ACCEL_RPM_S_MAX))
        accel_row.addWidget(self.accel)
        accel_row.addStretch(1)
        v.addLayout(accel_row)
        # [2026-11-26 Windows 物理键盘打不进] 特殊值文本「跟随变频器」时
        # 点击不会清文本 ⇒ validator 拒收新字符；按下即全选让第一次按键
        # 能替换（软键盘路径本就有全选，这里补物理键盘）。
        self._accel_select_all = _SpecialValueSelectAll(self)
        self.accel.installEventFilter(self._accel_select_all)
        self.accel.lineEdit().installEventFilter(self._accel_select_all)

        # ---- 阀复位 + 换相策略 ----
        v.addWidget(section_title("停机行为"))
        self.rst_stop = QCheckBox("停止时关阀复位")
        self.rst_stop.setChecked(_inst_bool(
            d, proto.AUTO_MODE_RESET_VALVES_ON_STOP,
            proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF))
        self.rst_stop.setToolTip("停止自动运行时：勾选=把电动球阀关回原位；"
                                 "不勾=阀保持现状")
        self.rst_estop = QCheckBox("急停时关阀复位")
        self.rst_estop.setChecked(_inst_bool(
            d, proto.AUTO_MODE_RESET_VALVES_ON_ESTOP,
            proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF))
        self.rst_estop.setToolTip("急停时：勾选=把电动球阀关回原位；"
                                  "不勾=阀保持现状（缺省，急停只停泵与"
                                  "变频器）")
        v.addWidget(wrap_row([self.rst_stop, self.rst_estop]))
        # 换相策略：切到**本实例**时，泵在阀门切换期间怎么处理（逐泵）。
        sw_row = QHBoxLayout()
        sw_row.setSpacing(8)
        lbl_sw = QLabel("换相策略")
        lbl_sw.setMinimumWidth(110)
        lbl_sw.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        sw_row.addWidget(lbl_sw)
        self.stir_sw = QComboBox()
        self.inlet_sw = QComboBox()
        for combo, tip in ((self.stir_sw, "搅拌泵"), (self.inlet_sw, "进水泵")):
            for pid, ptext in proto.AUTO_SWITCH_POLICIES:
                combo.addItem(ptext, pid)
            combo.setToolTip(
                "切到本实例时，%s 在阀门切换期间怎么处理：\n"
                "· 先停再启（保持既有逻辑）：换相先停泵，阀到位后再按本实例启动\n"
                "· 保持上一实例状态：换相途中不动它（运行中就继续运行）\n"
                "· 直接切到当前实例状态：换相立即按本实例启停（与阀切换并行）"
                % tip)
        for combo, key in ((self.stir_sw, proto.AUTO_MODE_STIR_SWITCH),
                           (self.inlet_sw, proto.AUTO_MODE_INLET_SWITCH)):
            val = str(d.get(key) or proto.AUTO_SWITCH_POLICY_DEF)
            idx = combo.findData(val)
            combo.setCurrentIndex(idx if idx >= 0 else 0)

        def _sw_group(text, combo):
            g = QWidget()
            gl = QHBoxLayout(g)
            gl.setContentsMargins(0, 0, 0, 0)
            gl.setSpacing(6)
            lbl = QLabel(text)
            lbl.setStyleSheet(
                "QLabel { color: palette(text); background: transparent; }")
            gl.addWidget(lbl)
            gl.addWidget(combo)
            return g
        sw_row.addWidget(wrap_row([_sw_group("搅拌泵", self.stir_sw),
                                   _sw_group("进水泵", self.inlet_sw)]), 1)
        v.addLayout(sw_row)
        self._finish_shell()

    def _title_text(self):
        """名称框的当前值（去首尾空白、夹到协议长度上限；可空）。"""
        return str(self.name_edit.text() or "").strip()[
            :proto.AUTO_CYCLE_INST_TITLE_MAX]

    def values(self):
        """对话框当前值 → 实例 dict（camelCase 全键；协议单位 MPa）。

        [未知键保留] 以原始 dict 为底、只覆盖本卡管的键（见 __init__ 注）；
        children/repeat 因此原样保留（组数据经本卡「设置→确定」不丢）。
        """
        d = dict(self._raw)
        d.update({
            proto.AUTO_INST_MODE: self._mode_id,
            # [名称 2026-11-27] 空串 = 保存时按默认名回落（去重在容器层做：
            # 顶层由 RemoteSettingsPage、组内由 CycleGroupDialog）。
             proto.AUTO_INST_TITLE: self._title_text(),
             proto.AUTO_INST_DEVICES: [
                 proto.AUTO_DEVICES[i][0]
                 for i in self.devices.checked_indexes()],
             proto.CFG_AUTO_STAGE_PRESS_ENABLED:
                 bool(self.press["enable"].isChecked()),
             proto.CFG_AUTO_STAGE_PRESS_THRESHOLD:
                 round(float(self.press["threshold"].value()), 3),
             proto.CFG_AUTO_STAGE_PRESS_COMPARE:
                 str(self.press["compare"].currentData()
                     or proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
             proto.CFG_AUTO_INST_PRESS1_ENABLED:
                 bool(self.press1["enable"].isChecked()),
             proto.CFG_AUTO_INST_PRESS1_THRESHOLD:
                 round(int(self.press1["threshold"].value()) / 1000.0, 3),
             proto.CFG_AUTO_INST_PRESS1_COMPARE:
                 str(self.press1["compare"].currentData()
                     or proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
             proto.CFG_AUTO_INST_PRESS2_ENABLED:
                 bool(self.press2["enable"].isChecked()),
             proto.CFG_AUTO_INST_PRESS2_THRESHOLD:
                 round(int(self.press2["threshold"].value()) / 1000.0, 3),
             proto.CFG_AUTO_INST_PRESS2_COMPARE:
                 str(self.press2["compare"].currentData()
                     or proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
             proto.CFG_AUTO_INST_TURB1_ENABLED:
                 bool(self.turb1["enable"].isChecked()),
             proto.CFG_AUTO_INST_TURB1_THRESHOLD:
                 round(float(self.turb1["threshold"].value()), 2),
             proto.CFG_AUTO_INST_TURB1_COMPARE:
                 str(self.turb1["compare"].currentData()
                     or proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
             proto.CFG_AUTO_INST_TURB2_ENABLED:
                 bool(self.turb2["enable"].isChecked()),
             proto.CFG_AUTO_INST_TURB2_THRESHOLD:
                 round(float(self.turb2["threshold"].value()), 2),
             proto.CFG_AUTO_INST_TURB2_COMPARE:
                 str(self.turb2["compare"].currentData()
                     or proto.AUTO_CYCLE_PRESS_COMPARE_DEF),
             proto.CFG_AUTO_STAGE_TIMER_ENABLED:
                 bool(self.timer_enable.isChecked()),
             proto.CFG_AUTO_STAGE_TIMER_MINUTES: int(self.timer_min.value()),
             proto.AUTO_INST_STIR_RPM: int(self.rpm.value()),
             proto.AUTO_INST_STIR_REVERSE:
                 bool(self.reverse.currentData()),
             proto.AUTO_MODE_STIR_ACCEL_RPM_S: int(self.accel.value()),
             proto.AUTO_MODE_RESET_VALVES_ON_STOP:
                 bool(self.rst_stop.isChecked()),
             proto.AUTO_MODE_RESET_VALVES_ON_ESTOP:
                 bool(self.rst_estop.isChecked()),
             proto.AUTO_MODE_STIR_SWITCH:
                 str(self.stir_sw.currentData()
                     or proto.AUTO_SWITCH_POLICY_DEF),
             proto.AUTO_MODE_INLET_SWITCH:
                 str(self.inlet_sw.currentData()
                     or proto.AUTO_SWITCH_POLICY_DEF),
             })
        return d


class CycleGroupDialog(_CycleDialogBase):
    """组合实例（组）的设置卡：名称 + 循环次数 + 组内模式列表。

    [语义 2026-11-27 协议] 组展开成扁平序列 = children 按顺序 × repeat 遍，
    **递归**展开（子项也可以是组合，最多 AUTO_GROUP_DEPTH_MAX 层）；组只是
    容器，不参与触发判定 ⇒ 组本身没有设备/触发，子实例各自带全套参数
    （每行「设置」弹自己的设置卡）。

    [嵌套层数] 顶层实例算第 1 层，depth = 本组所在层。depth 达上限时
    「添加模式」里的「组合」置灰 + 拒绝添加（再添就是第 4 层，服务端
    fail-closed 拒绝）。

    [继承单模式块] 添加子模式时继承对应单模式块的当前配置（与顶层实例
    同源，inherit_fn 由页面给）——省得在组里再配一遍设备/转速。
    """

    def __init__(self, data, stir_max=None, parent=None, title=None,
                 depth=1, inherit_fn=None):
        d = data if isinstance(data, dict) else {}
        self._raw = dict(d)
        self._stir_max = stir_max
        self._depth = max(1, int(depth or 1))
        self._inherit_fn = inherit_fn
        # [子序列先规整] children 递归规整（子项也可以是组合）+ 同层补默认名
        # （没名字的补「产水模式/组合…」，同层重名自动追加数字后缀）。
        kids_raw = d.get(proto.AUTO_INST_CHILDREN)
        coerced = [
            _coerce_cycle_instance(x, self._depth + 1)
            for x in (kids_raw if isinstance(kids_raw, (list, tuple)) else [])
            if isinstance(x, dict)]
        _inst_assign_titles(coerced)
        head = "组合设置 · %s（组内模式按序循环）" % (
            title or _inst_display_name(d))
        _CycleDialogBase.__init__(self, parent, head, min_w=420, min_h=380,
                                  width=880, height=720)
        v = self._body

        # ---- 组合设置（名称 / 循环遍数）----
        v.addWidget(section_title("组合设置"))
        name_row = QHBoxLayout()
        name_row.setSpacing(8)
        lbl_name = QLabel("名称")
        lbl_name.setMinimumWidth(110)
        lbl_name.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        name_row.addWidget(lbl_name)
        self.name_edit = QLineEdit(str(d.get(proto.AUTO_INST_TITLE) or ""))
        self.name_edit.setMaxLength(proto.AUTO_CYCLE_INST_TITLE_MAX)
        self.name_edit.setPlaceholderText(
            "留空＝自动命名（%s）" % _inst_base_name(proto.AUTO_INST_GROUP))
        self.name_edit.setToolTip(
            "组合名称（最多 %d 字）：留空＝按默认名（%s）；与同层其他实例"
            "重名时保存会自动追加数字后缀。"
            % (proto.AUTO_CYCLE_INST_TITLE_MAX,
               _inst_base_name(proto.AUTO_INST_GROUP)))
        self.name_edit.setMinimumWidth(180)
        self.name_edit.setMaximumWidth(420)
        name_row.addWidget(self.name_edit)
        name_row.addStretch(1)
        v.addLayout(name_row)

        rep_row = QHBoxLayout()
        rep_row.setSpacing(8)
        lbl_rep = QLabel("循环次数")
        lbl_rep.setMinimumWidth(110)
        lbl_rep.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        rep_row.addWidget(lbl_rep)
        self.repeat = QSpinBox()
        self.repeat.setRange(proto.AUTO_INST_REPEAT_MIN,
                             proto.AUTO_INST_REPEAT_MAX)
        self.repeat.setSuffix(" 遍")
        self.repeat.setValue(_inst_int(
            d, proto.AUTO_INST_REPEAT, proto.AUTO_INST_REPEAT_MIN,
            proto.AUTO_INST_REPEAT_MIN, proto.AUTO_INST_REPEAT_MAX))
        # [遍数语义 = 用户原话] 组内全部模式按顺序跑完算一遍；2 = 整组重复
        # 两遍（展开后共 子模式数×2 步），不是"每个模式跑两遍"。
        self.repeat.setToolTip(
            "组内全部模式按顺序跑完算一遍；2 = 整组重复两遍"
            "（展开后共 子模式数×2 步）")
        rep_row.addWidget(self.repeat)
        rep_hint = QLabel("组内全部模式按顺序跑完算一遍")
        rep_hint.setStyleSheet(theme.caption_label_qss())
        rep_row.addWidget(rep_hint)
        rep_row.addStretch(1)
        v.addLayout(rep_row)

        # [添加模式放上面 2026-11-27 用户「添加模式放上面」] 原先在列表
        # 末尾 ⇒ 模式一多就被推到滚动区外，看不到"还能加"。现在紧跟
        # 「循环次数」之后、列表之前（先加再看顺序，交互顺序也顺）。
        add_row = QHBoxLayout()
        add_row.setSpacing(8)
        add_lbl = QLabel("添加模式")
        add_lbl.setMinimumWidth(110)
        add_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        add_row.addWidget(add_lbl)
        self._child_combo = QComboBox()
        for _mid, _mname in proto.AUTO_CYCLE_MODE_POOL:
            self._child_combo.addItem(_mname, _mid)
        # [「组合」也在下拉里 2026-11-27 用户订正：嵌套可递归]
        self._child_combo.addItem(proto.AUTO_INST_GROUP_LABEL,
                                  proto.AUTO_INST_GROUP)
        self._child_combo.setToolTip(
            "要加入组合的模式；「组合」= 组内再嵌一组（最多 %d 层）"
            % proto.AUTO_GROUP_DEPTH_MAX)
        add_row.addWidget(self._child_combo)
        self._child_add_btn = QPushButton("添加")
        self._child_add_btn.clicked.connect(self._on_child_add)
        add_row.addWidget(self._child_add_btn)
        add_row.addStretch(1)
        v.addLayout(add_row)

        # ---- 组内模式列表 ----
        v.addWidget(section_title("组内模式（按顺序执行）"))
        hint_kids = QLabel(
            "顺序＝组内执行顺序；每行的「设置」里配该模式的设备与切换条件"
            "（组合也可以再套组合，最多 %d 层）。" % proto.AUTO_GROUP_DEPTH_MAX)
        hint_kids.setWordWrap(True)
        hint_kids.setStyleSheet(theme.caption_label_qss())
        v.addWidget(hint_kids)
        self._child_rows = []            # 有序：每项一个行控件字典（含 data）
        self._child_list_layout = QVBoxLayout()
        self._child_list_layout.setSpacing(6)
        self._child_list_box = QWidget()
        self._child_list_box.setLayout(self._child_list_layout)
        v.addWidget(self._child_list_box)
        # [提示占位恒在] 空组/上限/拒绝添加共用这一格（占位 + 清空内容，
        # 不显隐改结构 ⇒ 列表不上下跳）。
        self._child_hint = QLabel("")
        self._child_hint.setWordWrap(True)
        self._child_hint.setStyleSheet(theme.caption_label_qss())
        v.addWidget(self._child_hint)

        for item in coerced:
            self._child_rows.append(self._build_child_row(item))
        self._render_children()
        self._finish_shell()

    # ---- 组合自身 ----

    def _title_text(self):
        """名称框的当前值（去首尾空白、夹到协议长度上限；可空）。"""
        return str(self.name_edit.text() or "").strip()[
            :proto.AUTO_CYCLE_INST_TITLE_MAX]

    def _display_name(self):
        """本组当前的显示名（名称框优先；空 ⇒ 「组合」）。"""
        return (self._title_text()
                or _inst_base_name(proto.AUTO_INST_GROUP))

    def _can_add_group(self):
        """本组里还能不能再加一个子组合（子组 = depth+1 层）。"""
        return self._depth < proto.AUTO_GROUP_DEPTH_MAX

    # ---- 组内子实例列表（与顶层实例列表同款交互：上移/下移/删除/设置）----

    def _build_child_row(self, data):
        """建一个子实例行（返回行控件字典；data=实例配置 dict，就地引用）。"""
        row = QWidget()
        h = QHBoxLayout(row)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(8)
        order = QLabel("1")
        order.setMinimumWidth(18)
        order.setAlignment(Qt.AlignCenter)
        name = QLabel("")
        name.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        mode_lbl = QLabel("")
        mode_lbl.setStyleSheet(theme.caption_label_qss())
        summary = QLabel("")
        summary.setStyleSheet(theme.caption_label_qss())
        # [摘要单行] 长文案省略号（不换行 ⇒ 行高恒定，不按行数手算高度）。
        summary.setWordWrap(False)
        summary.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        settings = QPushButton("设置")
        up = QPushButton("上移")
        down = QPushButton("下移")
        delete = QPushButton("删除")
        is_group = (str(data.get(proto.AUTO_INST_MODE) or "")
                    == proto.AUTO_INST_GROUP)
        settings.setToolTip(
            "设置该子组合：名称、循环遍数与组内模式" if is_group
            else "设置该子模式的启动设备与切换条件（压差/进水压力/出水压力/"
                 "浊度/定时）、转速/方向/加速度/阀复位/换相策略")
        up.setToolTip("组内往上挪一位（顺序＝组内执行顺序）")
        down.setToolTip("组内往下挪一位")
        delete.setToolTip("从组合里删除该子模式")
        # [禁用态要看得见] 与顶层行同款：本地补 :disabled 规则。
        up.setStyleSheet(theme.disabled_button_qss())
        down.setStyleSheet(theme.disabled_button_qss())
        h.addWidget(order)
        h.addWidget(name)
        h.addWidget(mode_lbl)
        h.addWidget(summary)
        h.addStretch(1)
        h.addWidget(settings)
        h.addWidget(up)
        h.addWidget(down)
        h.addWidget(delete)
        widgets = {"row": row, "order": order, "name": name,
                   "mode": mode_lbl, "summary": summary, "settings": settings,
                   "up": up, "down": down, "del": delete,
                   "data": data, "head_h": 0}
        # [按行对象连线，不按下标] 与顶层行同款（增删/移动会改下标）。
        settings.clicked.connect(
            lambda _b=False, w=widgets: self._on_child_settings(w))
        up.clicked.connect(
            lambda _b=False, w=widgets: self._move_child(w, -1))
        down.clicked.connect(
            lambda _b=False, w=widgets: self._move_child(w, +1))
        delete.clicked.connect(
            lambda _b=False, w=widgets: self._on_child_delete(w))
        return widgets

    def _child_row_min_h(self, row):
        """一行的最小高度（按控件 minimumSizeHint 算，与排版状态无关）。"""
        h_head = int(row.get("head_h") or 0)
        for key in ("order", "name", "mode", "summary",
                    "settings", "up", "down", "del"):
            try:
                h_head = max(h_head,
                             int(row[key].minimumSizeHint().height()))
            except Exception:                                   # noqa: BLE001
                pass
        return h_head + 4

    def _render_children(self):
        """重排子实例行 + 刷名称/序号/模式名/摘要/按钮可用性/添加区状态。

        [排序不碰 setParent] takeAt + addWidget（与顶层实例列表同款：父控件
        不变 ⇒ 不隐藏 ⇒ 不丢焦点/键盘不自灭）。
        """
        for w in self._child_rows:
            if not w.get("head_h"):
                try:
                    w["head_h"] = int(w["settings"].sizeHint().height())
                except Exception:                               # noqa: BLE001
                    w["head_h"] = 0
        layout = self._child_list_layout
        while layout.count():
            layout.takeAt(0)
        n = len(self._child_rows)
        for idx, w in enumerate(self._child_rows):
            w["row"].setMinimumHeight(self._child_row_min_h(w))
            layout.addWidget(w["row"])
            w["row"].show()
            w["order"].setText("%d" % (idx + 1))
            d = w["data"]
            mid = str(d.get(proto.AUTO_INST_MODE) or "")
            # [名称] title 是唯一事实源（添加/保存时已按同层去重写好）；
            # 空 title 只在异常数据里出现 ⇒ 回落默认名基名。
            w["name"].setText(_inst_display_name(d))
            w["mode"].setText(_inst_base_name(mid))
            # [摘要恒占位] 单模式=已启用触发条件；组合=「N 个模式 × M 遍」。
            w["summary"].setText(
                _inst_group_summary(d) if mid == proto.AUTO_INST_GROUP
                else _inst_trigger_summary(d))
            w["up"].setEnabled(idx > 0)
            w["down"].setEnabled(idx < n - 1)
        at_max = n >= proto.AUTO_CYCLE_INSTANCES_MAX
        self._child_add_btn.setEnabled(not at_max)
        self._child_combo.setEnabled(not at_max)
        self._sync_group_option()
        if at_max:
            self._child_hint.setText(
                "最多 %d 个模式" % proto.AUTO_CYCLE_INSTANCES_MAX)
        elif not n:
            self._child_hint.setText(
                "组内还没有模式：点下方「添加模式」加入"
                "（空组不是合法循环项，服务端会拒绝）")
        else:
            self._child_hint.setText("")

    def _sync_group_option(self):
        """「组合」选项按嵌套层数置灰（达上限 ⇒ 不能再嵌一层）。"""
        idx = self._child_combo.findData(proto.AUTO_INST_GROUP)
        if idx < 0:
            return
        allowed = self._can_add_group()
        model = self._child_combo.model()
        item = model.item(idx) if hasattr(model, "item") else None
        if item is not None and hasattr(item, "setEnabled"):
            item.setEnabled(allowed)
            if not allowed:
                item.setToolTip(
                    "组合最多嵌套 %d 层（当前已是第 %d 层）"
                    % (proto.AUTO_GROUP_DEPTH_MAX, self._depth))

    def _taken_child_names(self, skip=None):
        """同层已占用的显示名（去重用；skip=正在编辑的那一行）。"""
        return [_inst_display_name(w["data"]) for w in self._child_rows
                if w is not skip]

    def _renumber_children(self):
        """组内增删/移动/改名后按**同层命名口径**重排子实例标题。

        [与顶层/回显同口径 2026-11-27 独立复核] `_inst_assign_titles` 是
        唯一命名规则：同类 ≥2 全部带号（产水模式1/产水模式2）、只剩 1 个
        回裸名、自定义名不动。组卡打开时 `__init__` 也用同一函数规整
        children ⇒ 本地操作后立刻同款重排，避免「确定」后名字又变一次。
        """
        _inst_assign_titles([w["data"] for w in self._child_rows])

    def _on_child_add(self):
        """「添加」：把下拉选中的模式作为**新子实例**追加到组内末尾。"""
        if len(self._child_rows) >= proto.AUTO_CYCLE_INSTANCES_MAX:
            return
        mid = str(self._child_combo.currentData() or "")
        if not mid:
            return
        if mid == proto.AUTO_INST_GROUP and not self._can_add_group():
            self._child_hint.setText(
                "组合最多嵌套 %d 层：当前已是第 %d 层，不能再加组合"
                % (proto.AUTO_GROUP_DEPTH_MAX, self._depth))
            return
        d = proto.auto_cycle_instance_default(mid)
        # [继承单模式块] 设备/转速/方向/加速度/阀复位取对应单模式块的当前
        # 值（与顶层「添加实例」同源）；组合没有单模式块 ⇒ 纯缺省。
        if mid != proto.AUTO_INST_GROUP and self._inherit_fn is not None:
            try:
                inh = self._inherit_fn(mid)
            except Exception:                               # noqa: BLE001
                inh = None
            if isinstance(inh, dict):
                d.update(inh)
        # [默认名] 先按同层去重给个基名，入列后整层重排（同类≥2 全部带号）。
        d[proto.AUTO_INST_TITLE] = _inst_unique_title(
            _inst_base_name(mid), self._taken_child_names())
        self._child_rows.append(self._build_child_row(d))
        self._renumber_children()
        self._render_children()

    def _move_child(self, w, delta):
        """组内上移/下移（delta=-1 上移）；首/末行按钮本就置灰。"""
        try:
            i = self._child_rows.index(w)
        except ValueError:
            return
        j = i + delta
        if j < 0 or j >= len(self._child_rows):
            return
        self._child_rows[i], self._child_rows[j] = \
            self._child_rows[j], self._child_rows[i]
        # [序号随位置] 同类换位次 ⇒ 谁算 #1/#2 也变了（同顶层口径）。
        self._renumber_children()
        self._render_children()

    def _on_child_delete(self, w):
        """从组内删除一个子实例（行控件 deleteLater）。"""
        try:
            i = self._child_rows.index(w)
        except ValueError:
            return
        row = self._child_rows.pop(i)
        self._child_list_layout.removeWidget(row["row"])
        row["row"].deleteLater()
        # [删到只剩 1 个同类 ⇒ 回裸名] 与顶层/回显同口径。
        self._renumber_children()
        self._render_children()

    def _on_child_settings(self, w):
        """「设置」：弹该子实例的设置卡（组合 ⇒ 递归弹组合卡）。"""
        try:
            idx = self._child_rows.index(w)
        except ValueError:
            return
        d = w["data"]
        mid = str(d.get(proto.AUTO_INST_MODE) or "")
        child_name = w["name"].text() or _inst_base_name(mid)
        head = "%s · %s" % (self._display_name(), child_name)
        if mid == proto.AUTO_INST_GROUP:
            dlg = CycleGroupDialog(
                d, stir_max=self._stir_max, parent=self, title=head,
                depth=self._depth + 1, inherit_fn=self._inherit_fn)
        else:
            # [复用单实例卡 2026-11-27] 子模式与顶层实例同卡（同一份参数
            # 口径）；allow_group=False ⇒ 不会把子项当组合编辑。
            dlg = CycleInstanceDialog(
                d, stir_max=self._stir_max, parent=self, title=head,
                allow_group=False,
                title_suffix="（组内第 %d 步）" % (idx + 1))
        if dlg.overlay_exec():
            w["data"] = self._apply_child_values(w, dlg.values())
            # [改名后整层重排] 与顶层 `_apply_cycle_dialog` 同口径（同类≥2
            # 全部带号、只剩 1 个回裸名、自定义名保留）。
            self._renumber_children()
            self._render_children()

    def _apply_child_values(self, w, values):
        """把设置卡的值写回子实例 + **同层重名去重**（保存时的口径）。

        与顶层实例的 `_apply_cycle_dialog` 同源：名字留空 ⇒ 回落默认名；
        与组内其他子实例重名 ⇒ 自动追加数字后缀（组合→组合2）。
        """
        out = dict(values or {})
        desired = (str(out.get(proto.AUTO_INST_TITLE) or "").strip()
                   or _inst_base_name(out.get(proto.AUTO_INST_MODE)))
        out[proto.AUTO_INST_TITLE] = _inst_unique_title(
            desired, self._taken_child_names(skip=w))
        return out

    def values(self):
        """设置卡当前值 → 组合实例 dict（camelCase；与顶层实例同键形状）。

        [同层去重] children 顺序＝组内执行顺序；逐项规整后再按**保存顺序**
        去重标题（用户可能把两个子模式改成同名 ⇒ 后者自动加后缀）。
        """
        d = dict(self._raw)
        kids = []
        taken = set()
        for w in self._child_rows:
            item = _coerce_cycle_instance(w["data"], self._depth + 1)
            title = _inst_unique_title(
                str(item.get(proto.AUTO_INST_TITLE) or "").strip()
                or _inst_base_name(item.get(proto.AUTO_INST_MODE)), taken)
            item[proto.AUTO_INST_TITLE] = title
            taken.add(title)
            kids.append(item)
        d.update({
            proto.AUTO_INST_MODE: proto.AUTO_INST_GROUP,
            proto.AUTO_INST_TITLE: self._title_text(),
            proto.AUTO_INST_REPEAT: int(self.repeat.value()),
            proto.AUTO_INST_CHILDREN: kids,
        })
        return d


class RemoteSettingsPage(Page):
    """远端设置：运行模式（需 root）+ 设备参数只读展示。

    [线程口径] bridge 回调跑在**网络线程**；configReady 信号把配置快照
    投递回 UI 线程再铺控件（信号跨线程自动排队）。曾直接在回调里
    _populate → 从网络线程碰 QLineEdit/勾选框，界面死活不更新（用户
    反馈"没生效"）—— 这是 Qt 线程规则，不是可选项。
    """

    #: 配置快照到货（网络线程 emit，UI 线程收）。
    configReady = pyqtSignal(dict)

    #: [2026-11-18] 变频器参数（0x011B）应答到货（网络线程 emit，UI 线程收）。
    #:
    #: [为什么又要一层信号] bridge 的回调跑在**网络线程**，直接在里面碰
    #: QLabel/QSpinBox 是 Qt 线程规则违规（界面死活不更新——与 configReady
    #: 同款实锅）。⇒ 回调只 emit 信号，控件更新一律回 UI 线程。
    vfdParamsReady = pyqtSignal(dict)     #: 读应答
    vfdParamsWritten = pyqtSignal(dict)   #: 写应答
    vfdParamsFailed = pyqtSignal(str)     #: 网络层失败（原因原样）

    TOPICS = (proto.TOPIC_DEVICE_HEALTH, proto.TOPIC_VFD_STATUS)

    #: 页内导航树显示所需的最小页面宽（px）。窄于它（紧凑/手机档）TOC 会把
    #: 卡片区压窄 ⇒ 隐藏，保留改造前的单列全宽布局。
    TOC_MIN_PAGE_W = 900

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self._config_fetched = False      # 已发过 GET（含失败），防 refresh 轰炸
        # [2026-11-15 审计] 收到过 protect 节快照？⇒ 「应用」按钮在收到
        # 快照**之前**必须禁用：没收到就按控件缺省值下发 ⇒ 会把服务端的
        # 保护配置静默改成缺省（保护被关掉）。C 端同款（m_protectPresent）。
        self._protect_seen = False
        # [显示渐变范围 2026-11-24] 老服务端不认识这 10 个键 ⇒ 只有收到
        # 过其中之一才随 collect 下发（否则整包 SET 会被"未知键"拒绝，
        # 连带用户改的采集开关都保存不了）。
        self._display_range_seen = False
        # [2026-11-19 相机卡] 收到过 camera 节快照？⇒ 同 protect 的纪律：
        # 没收到快照不许下发（控件上还是缺省值 ⇒ 会把服务端的相机配置静默
        # 改成缺省，等于把相机关了）。
        self._camera_seen = False
        # [2026-11-18] 变频器参数卡（0x011B）：
        #   · _vfdp_inflight：写请求在途闸（**必须成对开合**——只置位不复位
        #     ⇒ 第一次「应用」成功后按钮永久失灵，网页端就踩过这个坑）。
        #   · _vfdp_fetched：已发过读请求/已取到真值（防 refresh 每 200ms
        #     重复拉一次）。
        #   · _vfdp_values_ok：**已拿到真值**（没拿到就不许「应用」：控件上
        #     还是缺省值，下发等于把变频器参数静默改成缺省）。
        self._vfdp_inflight = False
        self._vfdp_fetched = False
        self._vfdp_values_ok = False
        self._vfdp_skipped = []             # 本次写载荷里被跳过的行（没回读）
        self.configReady.connect(self._populate)
        self.vfdParamsReady.connect(self._on_vfdp_ready)
        self.vfdParamsWritten.connect(self._on_vfdp_written)
        self.vfdParamsFailed.connect(self._on_vfdp_failed)
        self.add_header("远端设置", "服务端运行模式与连接信息")

        card = Card("运行模式")
        self._mode_label = QLabel("未知")
        self._mode_label.setStyleSheet(
            "QLabel { font-size: 24px; font-weight: bold; "
            "background: transparent; }")
        card.add(self._mode_label)
        hint = QLabel("自动模式会拒绝全部远程控制（设备由现场逻辑驱动）；"
                      "手动模式允许管理员远程操作。")
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

        # ---- 自动模式配置（产水/反洗/顺洗；2026-11-11 用户定案）----
        #
        # [三模式各自配置] 自动运行按模式推进，每个模式启动哪些设备由这里
        # 勾选（可多选）；搅拌电机的转速与方向也是**逐模式**的（不同模式
        # 转速/方向可能不同）。多选下拉复用数据三页的 MultiCheckCombo
        # （点项不收起、点外面才收的既有交互）。
        # [服务端已接入 2026-11-11] Python 服务端同日实现 auto 配置节
        # （运行期生效 + 落盘）与三模式序列（主界面模式卡启动）；C 端待
        # 同步（proto.CFG_SECTION_AUTO 注释的两端同步点）。
        card_auto_cfg = Card("自动模式配置")
        hint_auto_cfg = QLabel(
            "每个模式各自设置：勾选自动运行进入该模式时要启动的设备（可多选），"
            "并设定搅拌电机的运行转速与方向。改完按卡底的「应用」一次性下发。")
        hint_auto_cfg.setWordWrap(True)
        hint_auto_cfg.setStyleSheet(theme.caption_label_qss())
        card_auto_cfg.add(hint_auto_cfg)
        # ---- 搅拌最高转速限制（2026-11-25 用户定案：全局一条）----
        # 服务端对所有控制入口钳位（自动/手动/点动/启动捎带给定）；这里
        # 同时把各模式转速框的上限压到该值（界面不让人白填）。
        # [为什么不做成变频器参数] 服务端钳位立即生效、可审计、不依赖
        # 总线写参数；VFD 最高频率保持现场原值（见 proto 注释）。
        _max_row = QHBoxLayout()
        _max_row.setSpacing(8)
        _max_lbl = QLabel("搅拌最高转速")
        _max_lbl.setMinimumWidth(110)
        _max_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        _max_row.addWidget(_max_lbl)
        self._auto_stir_max = QSpinBox()
        self._auto_stir_max.setRange(proto.AUTO_STIR_MAX_RPM_MIN,
                                     proto.AUTO_STIR_MAX_RPM_MAX)
        self._auto_stir_max.setSingleStep(10)
        self._auto_stir_max.setSuffix(" rpm")
        self._auto_stir_max.setValue(proto.AUTO_STIR_MAX_RPM_DEF)
        self._auto_stir_max.setToolTip(
            "搅拌泵最高转速上限（rpm）：自动各模式、手动调速、点动/启动的"
            "给定都不得超过它；服务端统一钳位。")
        _max_row.addWidget(self._auto_stir_max)
        _max_hint = QLabel("所有模式和手动调速都不得超过此值")
        _max_hint.setStyleSheet(theme.caption_label_qss())
        _max_row.addWidget(_max_hint)
        _max_row.addStretch(1)
        card_auto_cfg.body.addLayout(_max_row)

        def _limit_blocks(v):
            for _b in self._auto_mode_blocks:
                _b["rpm"].setMaximum(int(v))

        self._auto_stir_max.valueChanged.connect(_limit_blocks)
        self._auto_mode_blocks = []
        for mode_id, mode_name in proto.AUTO_MODES:
            block = self._build_auto_mode_block(mode_name)
            block["id"] = mode_id
            self._auto_mode_blocks.append(block)
            card_auto_cfg.add(block["root"])

        # ---- 按钮灯闪烁（2026-11-24 用户定案）----
        # 三盏灯各自亮/灭毫秒；只影响灯，不影响启停逻辑。
        card_auto_cfg.add(section_title("按钮灯闪烁"))
        hint_lights = QLabel(
            "自动模式下：切到自动且可启动、或停稳后，启动灯闪（提示可启动）；"
            "下发停止后 VFD 减速或阀门未关完期间停止灯闪，全部停稳后常亮；"
            "需要复位时复位灯闪。下列为各灯"
            "「亮 / 灭」毫秒（50~5000 ms，缺省 500；服务端节拍约 100ms）。")
        hint_lights.setWordWrap(True)
        hint_lights.setStyleSheet(theme.caption_label_qss())
        card_auto_cfg.add(hint_lights)
        self._auto_lights = {}
        for _lkey, _lname in (("start", "启动灯"), ("stop", "停止灯"),
                              ("reset", "复位灯")):
            _lrow = QHBoxLayout()
            _lrow.setSpacing(8)
            _lbl = QLabel(_lname)
            _lbl.setMinimumWidth(90)
            _lbl.setStyleSheet(
                "QLabel { color: palette(text); background: transparent; }")
            _lon = QSpinBox()
            _lon.setRange(proto.AUTO_BLINK_MS_MIN, proto.AUTO_BLINK_MS_MAX)
            _lon.setSingleStep(50)
            _lon.setSuffix(" ms")
            _lon.setValue(proto.AUTO_BLINK_ON_MS_DEF)
            _loff = QSpinBox()
            _loff.setRange(proto.AUTO_BLINK_MS_MIN, proto.AUTO_BLINK_MS_MAX)
            _loff.setSingleStep(50)
            _loff.setSuffix(" ms")
            _loff.setValue(proto.AUTO_BLINK_OFF_MS_DEF)
            _lrow.addWidget(_lbl)
            _lrow.addWidget(QLabel("亮"))
            _lrow.addWidget(_lon)
            _lrow.addWidget(QLabel("灭"))
            _lrow.addWidget(_loff)
            _lrow.addStretch(1)
            card_auto_cfg.body.addLayout(_lrow)
            self._auto_lights[_lkey] = {"on": _lon, "off": _loff}

        # ---- 自动循环配置（组合模式「自动」）——实例化（2026-11-26）----
        #
        # [为什么并进来 2026-11-12 用户定案] 原先是两张独立 Card（模式配置
        # + 循环配置）⇒ 中间有条卡边界，看着还是"两个东西要分别保存"；
        # 而它们本来就是**同一份 auto 配置节**（一次下发）。故并成一张卡，
        # 卡内用小标题分段，底部**一个**应用按钮。
        #
        # [2026-11-26 实例化] 循环不再固定「产水→反洗→顺洗」，改为
        # **有序实例列表**：每个实例 = 一个模式 + 它自己的全套参数
        # （设备/压差/压力1/压力2/定时四触发 + 转速/方向/加速度/阀复位/
        # 换相策略）。**同一模式可添加多个**（顺序=循环顺序，第一个=起始
        # 实例，末位回绕首位）；每行「设置」弹该实例的设置卡。
        card_auto_cfg.add(section_title("自动循环（组合模式「自动」）"))
        hint_cycle = QLabel(
            "「添加」一个模式实例；同一模式可添加多个。顺序＝循环顺序"
            "（序号 1 ＝ 起始实例，最后一个跑完回到第一个）；每行的"
            "「设置」里配该实例的启动设备与切换条件（压差 / 进水压力 / "
            "出水压力 / 浊度 / 定时，至少启用其一）。"
            "「组合」= 组内再放多个模式（可再嵌套，最多 %d 层）并按"
            "「循环次数」整组重复，组内各模式在组合设置卡里各自配置。"
            % proto.AUTO_GROUP_DEPTH_MAX)
        hint_cycle.setWordWrap(True)
        hint_cycle.setStyleSheet(theme.caption_label_qss())
        card_auto_cfg.add(hint_cycle)
        # [未保存/需重启提示 2026-11-27 工作流实证] 用户改了实例但没点底部
        # 「应用自动配置」⇒ 服务端根本没收到（本次实验的真实原因）；或
        # 运行中改了配置 ⇒ 当前这轮仍用启动时的快照，必须停止再启动。
        # 这里常驻一行，按状态变色，不再让人猜。
        self._cycle_state_hint = QLabel("")
        self._cycle_state_hint.setWordWrap(True)
        self._cycle_state_hint.setStyleSheet(theme.caption_label_qss())
        card_auto_cfg.add(self._cycle_state_hint)
        self._cycle_enabled = QCheckBox("启用自动循环模式")
        self._cycle_enabled.setChecked(proto.AUTO_CYCLE_ENABLED_DEF)
        card_auto_cfg.add(self._cycle_enabled)

        # ---- 顶层总循环次数（2026-11-27 用户「远端设置中顶层的也支持下
        #      循环次数，支持一直循环，或指定循环次数后停止」）----
        # 0 = 一直循环（缺省，保持既有行为）；N>=1 = 展开计划（**含嵌套组
        # 重复**）完整跑完 N 轮后自动停止（等价现场停止按钮：停设备 + 按
        # resetValvesOnStop 关阀 + 清暂停记忆；不调 reset() ⇒ 清不掉急停/
        # 报警锁存）。运行中改**不立即生效**（启动时快照，停止再启动才
        # 生效——与 instances 同口径）。
        self._cycle_top_repeat = QSpinBox()
        self._cycle_top_repeat.setRange(proto.AUTO_CYCLE_TOP_REPEAT_DEF,
                                        proto.AUTO_CYCLE_TOP_REPEAT_MAX)
        # 0 显示成「一直循环」（specialValueText 只在值 == minimum 时生效）。
        self._cycle_top_repeat.setSpecialValueText("一直循环")
        self._cycle_top_repeat.setValue(proto.AUTO_CYCLE_TOP_REPEAT_DEF)
        self._cycle_top_repeat.setToolTip(
            "0 = 一直循环；N = 跑完 N 轮后自动停止（下次启动从头）")
        # [dirty 屏蔽] 回显（_populate_cycle）期间置 _cycle_top_populating ⇒
        # valueChanged 不算用户改动；只有用户拨动才置 _cycle_top_dirty ⇒
        # 载荷才发 topRepeat（老服务端对未知键整包拒，别无条件发）。
        self._cycle_top_dirty = False
        self._cycle_top_populating = False
        self._cycle_top_repeat.valueChanged.connect(
            self._on_cycle_top_changed)
        card_auto_cfg.body.addLayout(
            aligned_row("总循环次数", self._cycle_top_repeat))
        card_auto_cfg.add(hint_label(
            "0 = 一直循环；填 N = 展开计划跑完 N 轮后自动停止（下次启动从"
            "第 1 轮开始）。运行中修改需停止再启动才生效。"))

        # ---- 添加实例行（下拉 + 添加按钮 + 只读/上限提示占位）----
        add_row = QHBoxLayout()
        add_row.setSpacing(8)
        add_lbl = QLabel("添加实例")
        add_lbl.setMinimumWidth(110)
        add_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        add_row.addWidget(add_lbl)
        self._cycle_add_combo = QComboBox()
        for _mid, _mname in proto.AUTO_CYCLE_MODE_POOL:
            self._cycle_add_combo.addItem(_mname, _mid)
        # [组合 2026-11-27 用户订正「添加下拉加一项，名字就叫『组合』」]
        # 组是容器（组内再放模式/子组 + 各自的循环遍数），不是可运行模式。
        self._cycle_add_combo.addItem(proto.AUTO_INST_GROUP_LABEL,
                                      proto.AUTO_INST_GROUP)
        self._cycle_add_combo.setToolTip(
            "要添加的循环实例：「组合」= 组内再放多个模式（可嵌套，最多 %d "
            "层）并按遍数重复" % proto.AUTO_GROUP_DEPTH_MAX)
        add_row.addWidget(self._cycle_add_combo)
        self._cycle_add_btn = QPushButton("添加")
        self._cycle_add_btn.clicked.connect(self._on_cycle_add)
        add_row.addWidget(self._cycle_add_btn)
        # [提示占位恒在] 只读说明/「最多 32 个实例」共用这一格；无文案时
        # 留空（不显隐改布局，占位 + 清空内容）。
        self._cycle_ro_hint = QLabel("")
        self._cycle_ro_hint.setWordWrap(True)
        self._cycle_ro_hint.setStyleSheet(theme.caption_label_qss())
        add_row.addWidget(self._cycle_ro_hint, 1)
        card_auto_cfg.body.addLayout(add_row)

        # ---- 实例列表（有序；一行一个实例）----
        self._cycle_rows = []            # 有序：每项一个行控件字典（含 data）
        self._cycle_dirty = False        # 用户动过列表/设置 ⇒ 提交 instances
        self._cycle_readonly = False     # 老服务端（未回显 instances）⇒ 只读
        self._cycle_server_keys = set()
        self._cycle_can_cfg = True
        self._cycle_list_layout = QVBoxLayout()
        self._cycle_list_layout.setSpacing(6)
        self._cycle_list_box = QWidget()
        self._cycle_list_box.setLayout(self._cycle_list_layout)
        card_auto_cfg.add(self._cycle_list_box)
        # [缺省显示] 产水→反洗→顺洗（与既有缺省循环一致，可编辑）；
        # 应答带 instances 后以服务端为准。默认名在添加处就给（2026-11-27
        # 口径：产水模式 / 反洗模式 / 顺洗模式，同层重名才加数字后缀）。
        for _mid in proto.AUTO_CYCLE_MODES_DEF:
            _d = proto.auto_cycle_instance_default(_mid)
            _d[proto.AUTO_INST_TITLE] = _inst_unique_title(
                _inst_base_name(_mid),
                [_inst_display_name(w["data"]) for w in self._cycle_rows])
            self._cycle_rows.append(self._build_cycle_row(_d))
        # [首次重排] 序号/标题/按钮可用性都要先刷一遍 ⇒ 构造完立刻跑一次。
        self._cycle_render()

        row_cycle = QHBoxLayout()
        # [一次提交 2026-11-12] 这张卡（模式 + 循环）共用一个应用按钮：
        # 它们是同一份 auto 配置节，一次下发。
        self._auto_apply = QPushButton("应用自动配置（需管理员）")
        self._auto_apply.clicked.connect(self._apply_auto)
        self._auto_apply.setToolTip(
            "一次性提交四个模式的设备/转速/方向/阀复位设置，"
            "以及自动循环的实例列表（设备与切换条件）与按钮灯闪烁")
        row_cycle.addWidget(self._auto_apply)
        row_cycle.addStretch(1)
        card_auto_cfg.body.addLayout(row_cycle)
        self.root.addWidget(card_auto_cfg)

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
        # [设备必须列全 2026-11-10 用户口径] 「采集的设备不全，浊度仪是两个，
        # 流量计一个」——现场三台 485 表（流量计站5、浊度仪站1/站3）各有
        # 自己的采集周期，服务端早已按台支持（flowmeterPollMs /
        # turbidity1PollMs / turbidity2PollMs），界面却只列了合并的「浊度仪」
        # 一行、流量计干脆没有 ⇒ 现场既看不到这几台在采、也改不了它们的周期。
        # 现在 7 行：搅拌泵 / 温湿度 / 进水浊度仪 / 出水浊度仪 / 流量计 / 模拟量 /
        # 数字量。
        # [两台浊度各用各的开关 2026-11-10 用户定案] 「两台浊度仪要可以分开
        # 设置，他们是两台设备」——服务端已支持 turbidity1Enabled /
        # turbidity2Enabled（旧共同键 turbidityEnabled 降为缺省）；两行各勾
        # 各的，不再互相同步（现场可能只装一台、或单独停一台检修）。
        for r, (label, key_enabled, key_poll) in enumerate((
                ("搅拌泵", proto.CFGK_VFD_ENABLED, proto.CFGK_VFD_POLL_MS),
                ("温湿度", proto.CFGK_TEMPHUM_ENABLED,
                 proto.CFGK_TEMPHUM_POLL_MS),
                ("进水浊度仪", proto.CFGK_TURBIDITY1_ENABLED,
                 proto.CFGK_TURBIDITY1_POLL_MS),
                ("出水浊度仪", proto.CFGK_TURBIDITY2_ENABLED,
                 proto.CFGK_TURBIDITY2_POLL_MS),
                ("流量计", proto.CFGK_FLOWMETER_ENABLED,
                 proto.CFGK_FLOWMETER_POLL_MS),
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
        # [滤速 2026-11-25] 过滤面积（m²）：滤速 = 瞬时流量 / 面积。
        _area_row = QHBoxLayout()
        _area_row.setSpacing(8)
        _area_lbl = QLabel("滤速计算面积")
        _area_lbl.setMinimumWidth(110)
        _area_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        _area_row.addWidget(_area_lbl)
        self._collect_filter_area = QDoubleSpinBox()
        self._collect_filter_area.setRange(proto.FILTER_AREA_M2_MIN,
                                           proto.FILTER_AREA_M2_MAX)
        self._collect_filter_area.setDecimals(3)
        self._collect_filter_area.setSingleStep(0.1)
        self._collect_filter_area.setSuffix(" m²")
        self._collect_filter_area.setValue(proto.FILTER_AREA_M2_DEF)
        self._collect_filter_area.setSpecialValueText("未设置")
        self._collect_filter_area.setToolTip(
            "过滤面积（m²）：滤速 = 瞬时流量 ÷ 面积。\n"
            "0 = 未设置（首页滤速显示 --）。")
        _area_row.addWidget(self._collect_filter_area)
        _area_hint = QLabel("0 = 不计算")
        _area_hint.setStyleSheet(theme.caption_label_qss())
        _area_row.addWidget(_area_hint)
        _area_row.addStretch(1)
        card3.body.addLayout(_area_row)
        hint3 = QLabel("流量计/两台浊度仪各按自己的周期采集（共用 485 但"
                       "节拍互不影响）；两台浊度仪是两台独立设备，采集开关"
                       "各自独立。电磁阀/进水泵跟随数字量的开关。改开关立即"
                       "生效（被停的设备健康转数据过期），改周期下一拍生效。")
        hint3.setWordWrap(True)
        hint3.setStyleSheet(theme.caption_label_qss())
        card3.add(hint3)
        # ---- 模拟量通道与量程（2026-11-20 远端可配；二改：量程=下限~上限）----
        # [这一节是什么] 模拟量模块上挂了**四台传感器**：压差计（缺省 CH0，
        # 铭牌 0~1 MPa）、电机温度传感器（缺省 CH1，0~100 ℃）与两台压力
        # 传感器（缺省 CH2/CH3，0~1 MPa）。现场维护更换传感器时（换通道
        # 接线、换不同量程的表头）不用改代码。
        # [一台设备一块 2026-11-23] 四台各一块（块标题=设备名，块内
        # 通道/下限/上限/4mA 以下有效），见下面的 _add_analog_block。
        # [量程=下限~上限 2026-11-20 用户口径「只要填上限和下限即可」]
        # 4mA → 下限、20mA → 上限，换算由两端点自己定（服务端 lowValue）；
        # 「温度满量程」废弃。
        # [压差计负压口径 2026-11-20 用户确认] 铭牌 0~1 MPa、4mA=0 点；
        # 4mA 以下按线性继续算负值并标「负压」（既有特殊处理原样保留）。
        # 越界/下限≥上限 SET 服务端明确拒（fail-closed）。
        card3.add(section_title("模拟量通道与量程"))
        # [通道统一下拉 2026-11-23 用户口径「通道选择都用统一的下拉框」]
        # 四台传感器（压差计/温度/压力1/压力2）的通道号全部用 QComboBox
        # （此前压差/温度已是下拉，压力1/2 还是 QSpinBox ⇒ 同页两种手感）。
        # 条目 CH0..CH{analogChannels-1}，随 GET 回的路数重建；缺键（老
        # 服务端）回落 ANALOG_CHANNELS_DEF（=8 ⇒ CH0..CH7）。
        self._analog_channels = proto.ANALOG_CHANNELS_DEF
        self._analog_press_ch = _new_analog_channel_combo(
            0, "压差计接在模拟量模块的第几路（4~20mA，缺省 CH0）；"
               "换接线后在这里改")
        self._analog_temp_ch = _new_analog_channel_combo(
            1, "电机温度传感器接在模拟量模块的第几路（缺省 CH1）；"
               "与压差计同一通道时温度判定失效")
        self._press1_ch = _new_analog_channel_combo(
            PRESS1_CHANNEL_DEF,
            "压力1 接在模拟量模块的第几路（4~20mA，缺省 CH2）")
        self._press2_ch = _new_analog_channel_combo(
            PRESS2_CHANNEL_DEF,
            "压力2 接在模拟量模块的第几路（4~20mA，缺省 CH3）")
        for _combo in (self._analog_press_ch, self._analog_temp_ch,
                       self._press1_ch, self._press2_ch):
            _combo.currentIndexChanged.connect(
                self._on_analog_channel_changed)
        self._rebuild_analog_channel_items()
        # [单位进标签不进后缀 2026-11-20] ℃ 是宽字形：QSpinBox 的
        # sizeHint 样本文本按极值数字估、估不到宽字形 ⇒ 1.5× 缩放下后缀
        # 被裁成"100.0 °"（三档实测复现）；构造期手测宽拿到的又是未
        # polish 的字体，同样不可靠 ⇒ 单位写进标签（（℃）/（MPa）），框内不带。
        self._analog_press_low = QDoubleSpinBox()
        self._analog_press_low.setRange(
            proto.CFGK_ANALOG_PRESS_LOW_MPA_MIN,
            proto.CFGK_ANALOG_PRESS_LOW_MPA_MAX)
        self._analog_press_low.setDecimals(2)
        self._analog_press_low.setSingleStep(0.1)
        self._analog_press_low.setValue(0.0)
        self._analog_press_low.setToolTip(
            "压差计量程下限（MPa）：4mA 对应的值；低于它的电流按线性继续"
            "显示为负压（铭牌 0~1 的表下限填 0）")
        self._analog_press_high = QDoubleSpinBox()
        self._analog_press_high.setRange(
            proto.CFGK_ANALOG_PRESS_HIGH_MPA_MIN,
            proto.CFGK_ANALOG_PRESS_HIGH_MPA_MAX)
        self._analog_press_high.setDecimals(2)
        self._analog_press_high.setSingleStep(0.1)
        self._analog_press_high.setValue(1.0)
        self._analog_press_high.setToolTip(
            "压差计量程上限（MPa）：20mA 对应的值（铭牌量程，常见 1.0）")
        self._analog_temp_low = QDoubleSpinBox()
        self._analog_temp_low.setRange(
            proto.CFGK_ANALOG_TEMP_LOW_C_MIN,
            proto.CFGK_ANALOG_TEMP_LOW_C_MAX)
        self._analog_temp_low.setDecimals(1)
        self._analog_temp_low.setValue(0.0)
        self._analog_temp_low.setToolTip(
            "温度传感器量程下限（℃）：4mA 对应的值")
        self._analog_temp_high = QDoubleSpinBox()
        self._analog_temp_high.setRange(
            proto.CFGK_ANALOG_TEMP_HIGH_C_MIN,
            proto.CFGK_ANALOG_TEMP_HIGH_C_MAX)
        self._analog_temp_high.setDecimals(1)
        self._analog_temp_high.setValue(100.0)
        self._analog_temp_high.setToolTip(
            "温度传感器量程上限（℃）：20mA 对应的值")
        # ---- 「4mA 以下有效」（0~4 段线性延续；2026-11-20 用户口径
        #      「增加 0-4 有效选项…可以适配更多的传感器」）----
        # 开 = 低于 4mA 按线性继续算（压差计的负压特殊处理就是它）；
        # 关 = 欠量程（钳到量程下限并标注，保护判据不会拿假值误动作）。
        # 压差计缺省**开**（现场 4mA 以下是真负压）；温度缺省**关**。
        self._analog_press_below4 = QCheckBox(
            "4mA 以下有效（负压线性延续）")
        self._analog_press_below4.setChecked(True)
        self._analog_press_below4.setToolTip(
            "勾选：低于 4mA 的电流按线性继续显示负压（铭牌 0~1 的表可显示"
            "吸力）；不勾：按欠量程钳到量程下限")
        self._analog_temp_below4 = QCheckBox(
            "4mA 以下有效（线性延续）")
        self._analog_temp_below4.setChecked(False)
        self._analog_temp_below4.setToolTip(
            "勾选：低于 4mA 的电流按线性继续算温度（适配 0~4 段有输出的"
            "传感器）；不勾：按欠量程钳到量程下限（超温保护不会误动作）")

        # ---- 压力传感器 1 / 2（2026-11-23 用户定案「新增两台压力传感器，
        #      远端设置里能分别配置通道号、上下限、是否 4mA 以下有效」）----
        #
        # [缺省] 通道 2/3、量程 0.0~1.0 MPa、4mA 以下**不**有效（false）。
        # 服务端 GET 应答里**没有这些键**（老服务端）⇒ 控件保持缺省，不去
        # 动它（项目「应答里没有的字段不要当成已关闭」纪律）。
        self._press1_low = self._new_press_spin(
            "压力1量程下限（MPa）：4mA 对应的值（压差卡「压力1」栏取数）",
            low_side=True)
        self._press1_high = self._new_press_spin(
            "压力1量程上限（MPa）：20mA 对应的值", low_side=False)
        self._press2_low = self._new_press_spin(
            "压力2量程下限（MPa）：4mA 对应的值（压差卡「压力2」栏取数）",
            low_side=True)
        self._press2_high = self._new_press_spin(
            "压力2量程上限（MPa）：20mA 对应的值", low_side=False)
        self._press1_low.setValue(PRESS_LOW_MPA_DEF)
        self._press1_high.setValue(PRESS_HIGH_MPA_DEF)
        self._press2_low.setValue(PRESS_LOW_MPA_DEF)
        self._press2_high.setValue(PRESS_HIGH_MPA_DEF)
        self._press1_below4 = QCheckBox("4mA 以下有效（线性延续）")
        self._press2_below4 = QCheckBox("4mA 以下有效（线性延续）")
        for _cb, _name in ((self._press1_below4, proto.PRESS1_LABEL),
                           (self._press2_below4, proto.PRESS2_LABEL)):
            _cb.setChecked(PRESS_BELOW4_VALID_DEF)
            _cb.setToolTip(
                "勾选：%s 低于 4mA 的电流按线性继续显示（适配 0~4 段有"
                "输出的传感器）；不勾：按欠量程钳到量程下限" % _name)

        # ---- 一台设备一块（2026-11-23 用户口径「一个设备一行尝试下，
        #      竖屏下在调整下都一个控件一行」）----
        # 四台传感器各成一个可见块（块标题 = 设备名），块内四个控件
        # （通道/下限/上限/4mA 以下有效）宽窗排一行、窄窗每控件独占一行
        # （_AnalogDeviceRow：按块的实际可用宽与控件实测宽二档判定）。
        # 高度一律交给布局（不按行数手算）；resize 时只跨档重排。
        self._analog_blocks = []
        self._add_analog_block(card3, "压差计", "MPa",
                               self._analog_press_ch,
                               self._analog_press_low,
                               self._analog_press_high,
                               self._analog_press_below4)
        self._add_analog_block(card3, "温度传感器", "℃",
                               self._analog_temp_ch,
                               self._analog_temp_low,
                               self._analog_temp_high,
                               self._analog_temp_below4)
        self._add_analog_block(card3, "压力传感器 1", "MPa",
                               self._press1_ch,
                               self._press1_low,
                               self._press1_high,
                               self._press1_below4)
        self._add_analog_block(card3, "压力传感器 2", "MPa",
                               self._press2_ch,
                               self._press2_low,
                               self._press2_high,
                               self._press2_below4)

        # ---- 显示渐变范围（2026-11-24 用户定案「两个浊度仪的范围是不
        #      一样的，要支持在远端设置；温度和压力也支持下渐变」）----
        # 首页数值按「蓝 → 绿 → 橙 → 红」着色：低值端蓝、中段绿、接近
        # 上限转橙、到上限/超过红。这里只配**没有工程量程**的三路：
        #   · 进水/出水浊度仪（NTU）：两台表量程不同，逐台配；
        #   · 电箱温度（℃）：温湿度站的温度；
        # 压力1/2、压差、电机温度**沿用上面「模拟量通道与量程」的下限/
        # 上限**（4mA→下限、20mA→上限），不重复配一遍（否则同一台传感器
        # 有两个"范围"，改一个忘一个必对不上）。
        # 只影响显示，不参与报警/保护；下限>=上限服务端明确拒。
        card3.add(section_title("显示渐变范围（蓝 → 绿 → 红）"))
        self._turb1_range_min = self._new_display_range_spin(
            proto.CFGK_TURBIDITY_RANGE_MIN_LIMIT,
            proto.CFGK_TURBIDITY_RANGE_MAX_LIMIT, 2, 0.1,
            proto.TURBIDITY_RANGE_MIN_DEF,
            "进水浊度仪渐变下限（NTU）：读数≤它显示蓝色")
        self._turb1_range_max = self._new_display_range_spin(
            proto.CFGK_TURBIDITY_RANGE_MIN_LIMIT,
            proto.CFGK_TURBIDITY_RANGE_MAX_LIMIT, 2, 0.1,
            proto.TURBIDITY_RANGE_MAX_DEF,
            "进水浊度仪渐变上限（NTU）：读数≥它显示红色")
        self._turb2_range_min = self._new_display_range_spin(
            proto.CFGK_TURBIDITY_RANGE_MIN_LIMIT,
            proto.CFGK_TURBIDITY_RANGE_MAX_LIMIT, 2, 0.1,
            proto.TURBIDITY_RANGE_MIN_DEF,
            "出水浊度仪渐变下限（NTU）：读数≤它显示蓝色")
        self._turb2_range_max = self._new_display_range_spin(
            proto.CFGK_TURBIDITY_RANGE_MIN_LIMIT,
            proto.CFGK_TURBIDITY_RANGE_MAX_LIMIT, 2, 0.1,
            proto.TURBIDITY_RANGE_MAX_DEF,
            "出水浊度仪渐变上限（NTU）：读数≥它显示红色")
        self._th_temp_range_min = self._new_display_range_spin(
            proto.CFGK_TEMPHUM_TEMP_LOW_C_LIMIT_MIN,
            proto.CFGK_TEMPHUM_TEMP_LOW_C_LIMIT_MAX, 1, 0.5,
            proto.TEMPHUM_TEMP_LOW_C_DEF,
            "电箱温度渐变下限（℃）：读数≤它显示蓝色")
        self._th_temp_range_max = self._new_display_range_spin(
            proto.CFGK_TEMPHUM_TEMP_HIGH_C_LIMIT_MIN,
            proto.CFGK_TEMPHUM_TEMP_HIGH_C_LIMIT_MAX, 1, 0.5,
            proto.TEMPHUM_TEMP_HIGH_C_DEF,
            "电箱温度渐变上限（℃）：读数≥它显示红色")
        # [电机转速渐变 2026-11-24 用户追加] 缺省 0~铭牌额定 1440rpm。
        self._vfd_speed_range_min = self._new_display_range_spin(
            proto.CFGK_VFD_SPEED_RANGE_MIN_LIMIT,
            proto.CFGK_VFD_SPEED_RANGE_MAX_LIMIT, 0, 10,
            proto.VFD_SPEED_RANGE_MIN_DEF,
            "电机转速渐变下限（rpm）：读数≤它显示蓝色")
        self._vfd_speed_range_max = self._new_display_range_spin(
            proto.CFGK_VFD_SPEED_RANGE_MIN_LIMIT,
            proto.CFGK_VFD_SPEED_RANGE_MAX_LIMIT, 0, 10,
            proto.VFD_SPEED_RANGE_MAX_DEF,
            "电机转速渐变上限（rpm）：读数≥它显示红色（缺省=铭牌额定）")
        # [电机温度渐变 2026-11-24 用户「电机温度呢」] 显示着色范围
        # （与模拟量「温度传感器」的 4~20mA 工程量程分开）。
        self._vfd_mtemp_range_min = self._new_display_range_spin(
            proto.CFGK_VFD_MOTOR_TEMP_RANGE_MIN_LIMIT,
            proto.CFGK_VFD_MOTOR_TEMP_RANGE_MAX_LIMIT, 1, 0.5,
            proto.VFD_MOTOR_TEMP_RANGE_MIN_DEF,
            "电机温度渐变下限（℃）：读数≤它显示蓝色")
        self._vfd_mtemp_range_max = self._new_display_range_spin(
            proto.CFGK_VFD_MOTOR_TEMP_RANGE_MIN_LIMIT,
            proto.CFGK_VFD_MOTOR_TEMP_RANGE_MAX_LIMIT, 1, 0.5,
            proto.VFD_MOTOR_TEMP_RANGE_MAX_DEF,
            "电机温度渐变上限（℃）：读数≥它显示红色")

        # [渐变逐项开关 2026-11-24 用户「可以设置是否启用显示渐变，每一个
        #  都可以单独设置」] 每行一个「启用」勾选；压力三项没有范围控件
        #  （量程沿用模拟量），单独一行三个勾选。
        def _grad_chk(tip):
            cb = QCheckBox("启用")
            cb.setChecked(True)
            cb.setToolTip(tip)
            return cb

        self._turb1_grad_chk = _grad_chk("勾选=进水浊度数字按渐变上色；不勾=单色")
        self._turb2_grad_chk = _grad_chk("勾选=出水浊度数字按渐变上色；不勾=单色")
        self._th_temp_grad_chk = _grad_chk("勾选=电箱温度按渐变上色；不勾=单色")
        self._vfd_speed_grad_chk = _grad_chk(
            "勾选=电机转速（含搅拌泵页读数/拖动条）按渐变上色；不勾=单色")
        self._vfd_mtemp_grad_chk = _grad_chk(
            "勾选=电机温度按渐变上色；不勾=单色")
        self._press_grad_chk = _grad_chk("勾选=压差按渐变上色；不勾=单色")
        self._press1_grad_chk = _grad_chk("勾选=压力1 按渐变上色；不勾=单色")
        self._press2_grad_chk = _grad_chk("勾选=压力2 按渐变上色；不勾=单色")

        def _range_line(caption, chk, lo_spin, hi_spin, unit):
            return wrap_row([
                chk, QLabel(caption), lo_spin, QLabel("~"), hi_spin,
                QLabel(unit)])

        card3.add(_range_line("进水浊度仪", self._turb1_grad_chk,
                              self._turb1_range_min,
                              self._turb1_range_max, "NTU"))
        card3.add(_range_line("出水浊度仪", self._turb2_grad_chk,
                              self._turb2_range_min,
                              self._turb2_range_max, "NTU"))
        card3.add(_range_line("电箱温度", self._th_temp_grad_chk,
                              self._th_temp_range_min,
                              self._th_temp_range_max, "℃"))
        card3.add(_range_line("电机转速", self._vfd_speed_grad_chk,
                              self._vfd_speed_range_min,
                              self._vfd_speed_range_max, "rpm"))
        card3.add(_range_line("电机温度", self._vfd_mtemp_grad_chk,
                              self._vfd_mtemp_range_min,
                              self._vfd_mtemp_range_max, "℃"))
        card3.add(wrap_row([
            QLabel("压力渐变"),
            QLabel("压差"), self._press_grad_chk,
            QLabel(proto.PRESS1_LABEL), self._press1_grad_chk,
            QLabel(proto.PRESS2_LABEL), self._press2_grad_chk]))
        _range_hint = QLabel(
            "只影响数值的渐变色，不是报警阈值；勾选=该项按蓝→绿→红上色，"
            "不勾=单色。浊度/电箱温度/电机转速/电机温度的范围在上面配置；"
            "压力1/压力2/压差直接沿用「模拟量通道与量程」的下限/上限。"
            "下限必须小于上限。")
        _range_hint.setWordWrap(True)
        _range_hint.setStyleSheet(theme.caption_label_qss())
        card3.add(_range_hint)

        row3 = QHBoxLayout()
        self._collect_refresh = QPushButton("刷新")
        self._collect_refresh.clicked.connect(self._fetch_config)
        self._collect_apply = QPushButton("应用（需管理员）")
        self._collect_apply.clicked.connect(self._apply_collect)
        row3.addWidget(self._collect_refresh)
        row3.addWidget(self._collect_apply)
        row3.addStretch(1)
        card3.body.addLayout(row3)
        self.root.addWidget(card3)

        # ---- 运行保护（2026-11-15 用户定案）----
        #
        # [这一节是什么] 进水泵开着、流量计却一直报"空管" ⇒ 水路没通（或
        # 流量计坏了）⇒ 到时限就按配置动作（停设备 / 只报警）。
        # [为什么单开一张卡] 它既不是"采哪些多快采"（采集配置），也不是
        # "往哪存"（数据存储），是**设备保护** ⇒ 独立一张卡，位置在采集
        # 配置之后、数据存储之前（都属设备侧的事）。
        # [时长用秒] 现场口径是"泵开着多久"（分钟级），秒比毫秒好读；
        # 协议里仍是毫秒（与服务端同源）。
        card_prot = Card("运行保护")
        hint_prot = QLabel(
            "进水泵开启后，若流量计仍报「空管」并持续到设定时长，说明水路"
            "没通（或流量计异常）：可只报警留记录，也可直接停止设备。"
            "时长 0 = 关闭该保护（缺省不启用）。")
        hint_prot.setWordWrap(True)
        hint_prot.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_prot)
        self._protect_stop = QCheckBox("超时后停止设备")
        self._protect_stop.setChecked(proto.PROTECT_INLET_NOFLOW_STOP_DEF)
        self._protect_stop.setToolTip(
            "勾选：超时后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        self._protect_timeout = QSpinBox()
        self._protect_timeout.setRange(
            0, proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX // 1000)
        self._protect_timeout.setSingleStep(10)
        self._protect_timeout.setSuffix(" 秒")
        self._protect_timeout.setValue(
            proto.PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF // 1000)
        self._protect_timeout.setSpecialValueText("关闭")
        self._protect_timeout.setToolTip(
            "进水泵开着且流量计报空管，持续这么久后动作；0 = 关闭该保护")
        # [行内折行 2026-11-24] 与自动循环条件行同理：整行 3~4 个控件在
        # 640px 窄窗下**排不下**（QHBox 最小宽=求和 ⇒ 整页被撑出横向滚动
        # 条）⇒ 用 wrap_row（宽窗一行、窄窗整组折行）。
        card_prot.add(wrap_row([
            self._protect_stop,
            label_group("空管判定时长", self._protect_timeout)]))
        # ---- 压差过大保护（2026-11-15）----
        # 压差（模拟量通道 0）连续超上限 2 秒 ⇒ 触发：滤芯堵塞/水路异常时
        # 泵在憋压，继续跑会伤设备 ⇒ 报警（进报警历史表）+ 按配置停机。
        # [上限与开关都让现场定] 压差上限与工艺强相关（滤芯阻力、清洗周期
        # 都会改）⇒ 写死一个值必然误动作 ⇒ 缺省**不启用**，由现场自己定。
        card_prot.add(section_title("压差过大保护"))
        hint_press = QLabel(
            "压差连续超过上限并持续约 2 秒即触发（滤芯堵塞/水路异常时泵会"
            "憋压，继续跑会伤设备）。压差上限与现场工艺相关，请按实际情况"
            "设定；不启用则不判。")
        hint_press.setWordWrap(True)
        hint_press.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_press)
        self._protect_press_enabled = QCheckBox("启用压差过大保护")
        self._protect_press_enabled.setChecked(
            proto.PROTECT_PRESS_HIGH_ENABLED_DEF)
        self._protect_press_enabled.setToolTip(
            "不勾 = 不判压差（保护关闭）")
        # [界面用 kPa、协议用 MPa] 两端客户端同款：桌面 C 端只有整数微调框
        # （无小数微调框）⇒ 统一用 kPa（0.8 MPa = 800 kPa），提交时换算成
        # MPa（÷1000）。小数 MPa 反而更易与首页压差卡（MPa）对不上。
        self._protect_press_limit = QDoubleSpinBox()
        self._protect_press_limit.setRange(
            proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MIN * 1000.0,
            proto.PROTECT_PRESS_HIGH_LIMIT_MPA_MAX * 1000.0)
        self._protect_press_limit.setDecimals(0)
        self._protect_press_limit.setSingleStep(10)
        self._protect_press_limit.setSuffix(" kPa")
        self._protect_press_limit.setValue(
            proto.PROTECT_PRESS_HIGH_LIMIT_MPA_DEF * 1000.0)
        self._protect_press_limit.setToolTip(
            "压差上限（kPa）：超过它即判「过大」。\n"
            "800 kPa = 0.8 MPa（正常运行约 500 kPa）")
        self._protect_press_stop = QCheckBox("触发后停止设备")
        self._protect_press_stop.setChecked(
            proto.PROTECT_PRESS_HIGH_STOP_DEF)
        self._protect_press_stop.setToolTip(
            "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        card_prot.add(wrap_row([
            self._protect_press_enabled,
            label_group("压差上限", self._protect_press_limit),
            self._protect_press_stop]))
        # ---- 电机超温保护（2026-11-20 用户定案「可以设置大于多少度停止
        #      设备并报电机超温故障，可禁用」）----
        # 温度传感器（模拟量，通道在采集配置里配）连续超阈值 2 秒 ⇒ 触发：
        # 报警（进报警历史表 + 客户端报警页）+ 按配置停设备；**锁存** ——
        # 温度回落后不自动恢复，人工复位后才能再启动。缺省不启用。
        card_prot.add(section_title("电机超温保护"))
        hint_motor = QLabel(
            "电机温度连续超过阈值并持续约 2 秒即触发：报警（记录到报警表）"
            "并可停止设备。报警锁存 —— 温度回落后也需人工复位才能再启动。"
            "温度传感器接在哪一路、量程多少，在「采集配置 → 模拟量通道与"
            "量程」里配。不启用则不判。")
        hint_motor.setWordWrap(True)
        hint_motor.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_motor)
        self._protect_motor_enabled = QCheckBox("启用电机超温保护")
        self._protect_motor_enabled.setChecked(
            proto.PROTECT_MOTOR_OVER_TEMP_ENABLED_DEF)
        self._protect_motor_enabled.setToolTip("不勾 = 不判电机温度（关闭）")
        self._protect_motor_limit = QDoubleSpinBox()
        self._protect_motor_limit.setRange(
            proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MIN,
            proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MAX)
        self._protect_motor_limit.setDecimals(1)
        self._protect_motor_limit.setSingleStep(5.0)
        self._protect_motor_limit.setSuffix(" ℃")
        self._protect_motor_limit.setValue(
            proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_DEF)
        self._protect_motor_limit.setToolTip(
            "电机超温阈值（℃）：温度连续超过它约 2 秒即触发")
        self._protect_motor_stop = QCheckBox("触发后停止设备")
        self._protect_motor_stop.setChecked(
            proto.PROTECT_MOTOR_OVER_TEMP_STOP_DEF)
        self._protect_motor_stop.setToolTip(
            "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        card_prot.add(wrap_row([
            self._protect_motor_enabled,
            label_group("超温阈值", self._protect_motor_limit),
            self._protect_motor_stop]))
        # ---- 电箱超温保护（2026-11-25 用户口径「运行保护没有电箱超温
        #      保护」「参考电机超温保护设计」）----
        # 电箱温度来自温湿度变送器（不是模拟量温度传感器）；判据/去抖/
        # 锁存与电机超温完全同款。缺省不启用（阈值按现场电箱散热条件定）。
        card_prot.add(section_title("电箱超温保护"))
        hint_box = QLabel(
            "电箱温度（温湿度变送器）连续超过阈值并持续约 2 秒即触发："
            "报警（记录到报警表）并可停止设备。报警锁存 —— 温度回落后也"
            "需人工复位才能再启动。不启用则不判。")
        hint_box.setWordWrap(True)
        hint_box.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_box)
        self._protect_box_enabled = QCheckBox("启用电箱超温保护")
        self._protect_box_enabled.setChecked(
            proto.PROTECT_BOX_OVER_TEMP_ENABLED_DEF)
        self._protect_box_enabled.setToolTip("不勾 = 不判电箱温度（关闭）")
        self._protect_box_limit = QDoubleSpinBox()
        self._protect_box_limit.setRange(
            proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_MIN,
            proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_MAX)
        self._protect_box_limit.setDecimals(1)
        self._protect_box_limit.setSingleStep(5.0)
        self._protect_box_limit.setSuffix(" ℃")
        self._protect_box_limit.setValue(
            proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_DEF)
        self._protect_box_limit.setToolTip(
            "电箱超温阈值（℃）：温度连续超过它约 2 秒即触发")
        self._protect_box_stop = QCheckBox("触发后停止设备")
        self._protect_box_stop.setChecked(
            proto.PROTECT_BOX_OVER_TEMP_STOP_DEF)
        self._protect_box_stop.setToolTip(
            "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        card_prot.add(wrap_row([
            self._protect_box_enabled,
            label_group("超温阈值", self._protect_box_limit),
            self._protect_box_stop]))
        # ---- 压力1 / 压力2 过高保护（2026-11-25 用户口径「还有压力1
        #      压力2也做进运行保护」）----
        # 两路压力是模拟量（通道在采集配置里配），判据/去抖/锁存与压差
        # 过大保护同款；**读不到不判定**（没数据 ≠ 超压）。
        card_prot.add(section_title(proto.PRESS1_LABEL + "过高保护"))
        hint_press1 = QLabel(
            "压力1连续超过上限并持续约 2 秒即触发：报警（记录到报警表）"
            "并可停止设备。压力传感器接在哪一路、量程多少，在「采集配置 →"
            " 模拟量通道与量程」里配。读不到数据时不判定。不启用则不判。")
        hint_press1.setWordWrap(True)
        hint_press1.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_press1)
        self._protect_press1_enabled = QCheckBox("启用%s过高保护"
                                                    % proto.PRESS1_LABEL)
        self._protect_press1_enabled.setChecked(
            proto.PROTECT_PRESS_CHANNEL_HIGH_ENABLED_DEF)
        self._protect_press1_enabled.setToolTip("不勾 = 不判压力1（关闭）")
        self._protect_press1_limit = QDoubleSpinBox()
        self._protect_press1_limit.setRange(
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MIN * 1000.0,
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MAX * 1000.0)
        self._protect_press1_limit.setDecimals(0)
        self._protect_press1_limit.setSingleStep(10)
        self._protect_press1_limit.setSuffix(" kPa")
        self._protect_press1_limit.setValue(
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_DEF * 1000.0)
        self._protect_press1_limit.setToolTip(
            "压力1上限（kPa）：超过它即判「过高」。\n"
            "1000 kPa = 1.0 MPa")
        self._protect_press1_stop = QCheckBox("触发后停止设备")
        self._protect_press1_stop.setChecked(
            proto.PROTECT_PRESS_CHANNEL_HIGH_STOP_DEF)
        self._protect_press1_stop.setToolTip(
            "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        card_prot.add(wrap_row([
            self._protect_press1_enabled,
            label_group("压力1上限", self._protect_press1_limit),
            self._protect_press1_stop]))
        card_prot.add(section_title(proto.PRESS2_LABEL + "过高保护"))
        hint_press2 = QLabel(
            "压力2连续超过上限并持续约 2 秒即触发：报警（记录到报警表）"
            "并可停止设备。压力传感器接在哪一路、量程多少，在「采集配置 →"
            " 模拟量通道与量程」里配。读不到数据时不判定。不启用则不判。")
        hint_press2.setWordWrap(True)
        hint_press2.setStyleSheet(theme.caption_label_qss())
        card_prot.add(hint_press2)
        self._protect_press2_enabled = QCheckBox("启用%s过高保护"
                                                    % proto.PRESS2_LABEL)
        self._protect_press2_enabled.setChecked(
            proto.PROTECT_PRESS_CHANNEL_HIGH_ENABLED_DEF)
        self._protect_press2_enabled.setToolTip("不勾 = 不判压力2（关闭）")
        self._protect_press2_limit = QDoubleSpinBox()
        self._protect_press2_limit.setRange(
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MIN * 1000.0,
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_MAX * 1000.0)
        self._protect_press2_limit.setDecimals(0)
        self._protect_press2_limit.setSingleStep(10)
        self._protect_press2_limit.setSuffix(" kPa")
        self._protect_press2_limit.setValue(
            proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_DEF * 1000.0)
        self._protect_press2_limit.setToolTip(
            "压力2上限（kPa）：超过它即判「过高」。\n"
            "1000 kPa = 1.0 MPa")
        self._protect_press2_stop = QCheckBox("触发后停止设备")
        self._protect_press2_stop.setChecked(
            proto.PROTECT_PRESS_CHANNEL_HIGH_STOP_DEF)
        self._protect_press2_stop.setToolTip(
            "勾选：触发后停变频器/进水泵并关阀；不勾：只报警、不动设备")
        card_prot.add(wrap_row([
            self._protect_press2_enabled,
            label_group("压力2上限", self._protect_press2_limit),
            self._protect_press2_stop]))
        row_prot = QHBoxLayout()
        self._protect_apply = QPushButton("应用（需管理员）")
        self._protect_apply.clicked.connect(self._apply_protect)
        row_prot.addWidget(self._protect_apply)
        row_prot.addStretch(1)
        card_prot.body.addLayout(row_prot)
        self.root.addWidget(card_prot)

        # ---- 相机（2026-11-19 新增）----
        #
        # [这一节做什么] 配一路网络相机：**服务端**自己去拉 RTSP、解码、
        # 抽帧，客户端在「相机」页看画面（客户端**不直连相机** —— 外网访问
        # 场景里客户端在公网、相机在内网，只有服务端两边都够得到）。
        # [帧率档跟随相机自身] 实测本相机（TP-LINK IPC）能力表的帧率只有
        # 1/10/15/20（码值 65536+fps）⇒ 下拉给这四档 + 2/5（低于相机帧率，
        # 服务端抽帧）。**没有 25/30 档**（相机本身就没有）。
        # [画质语义] 越大越清晰（1~100，与服务端 mpp/gst 的 quality 同向）。
        card_cam = Card("相机")
        hint_cam = QLabel(
            "服务端连接网络相机并采集画面，客户端在「相机」页观看。"
            "帧率跟随相机自身能力（1/10/15/20 fps）；画质越大越清晰、"
            "画面越大、占用带宽越多。")
        hint_cam.setWordWrap(True)
        hint_cam.setStyleSheet(theme.caption_label_qss())
        card_cam.add(hint_cam)
        self._cam_enabled = QCheckBox("启用相机")
        self._cam_enabled.setChecked(proto.CAMERA_ENABLED_DEF)
        self._cam_enabled.setToolTip("勾选：服务端开始拉流；不勾：服务端不采集")
        # [连接方式 + 地址 2026-11-20 用户口径「mac 和 ip 合并成一个输入框，
        #  根据下拉是 ip 或 mac」] 一个地址框两用：下拉切连接方式时框内容
        # 与占位符跟着换，两边的值都留着（切来切去不丢）。协议仍是 ip/mac
        # 两个键，按当前方式各发各的。
        self._cam_addr_ip = ""        # IP 模式的框内容缓存
        self._cam_addr_mac = ""       # MAC 模式的框内容缓存
        self._cam_resolve = QComboBox()
        self._cam_resolve.addItem("按 IP 连接", proto.CAMERA_RESOLVE_IP)
        self._cam_resolve.addItem("按 MAC 连接（相机动态 IP）",
                                  proto.CAMERA_RESOLVE_MAC)
        self._cam_resolve.setCurrentIndex(max(
            0, self._cam_resolve.findData(proto.CAMERA_RESOLVE_DEF)))
        self._cam_resolve.currentIndexChanged.connect(
            self._on_cam_resolve_changed)
        self._cam_resolve.setToolTip(
            "按 IP：直连地址框里的 IP；按 MAC：服务端每次连接前把 MAC 解析"
            "成当前 IP（先查本机 ARP 表，查不到对网段探测一轮再查）——相机"
            "换了 DHCP 地址也能自动跟上")
        self._cam_addr = QLineEdit()
        self._cam_addr.setPlaceholderText("相机 IP，如 192.168.1.181")
        # [输入框不许裁字 2026-11-20] 原来给死 `setMaximumWidth(210)` ⇒
        # 占位符「相机 IP，如 192.168.1.181」要 357px、框里只放得下 210
        # ⇒ Qt 用省略号截成「相机 IP，如 192.168…」⇒ 用户看不清该填什么。
        # ⇒ 宽度按**字体实测**给（占位符 + 已填内容取最宽），不钉死。
        self._cam_addr.setToolTip(
            "连接方式选「按 IP」时填相机 IP；选「按 MAC」时填相机 MAC"
            "（冒号/连字符/裸 hex 都认，相机标签或路由器 DHCP 列表里能查到）")
        self._cam_port = QSpinBox()
        self._cam_port.setRange(1, 65535)
        self._cam_port.setValue(proto.CAMERA_PORT_DEF)
        self._cam_reconnect = QCheckBox("断线重连")
        self._cam_reconnect.setChecked(proto.CAMERA_RECONNECT_ENABLED_DEF)
        self._cam_reconnect.setToolTip(
            "勾选：断线后每隔「重连间隔」自动重试；不勾：断线后停在错误"
            "提示上，重新点「应用」才再试")
        self._cam_reconnect_s = QSpinBox()
        self._cam_reconnect_s.setRange(proto.CAMERA_RECONNECT_INTERVAL_S_MIN,
                                       proto.CAMERA_RECONNECT_INTERVAL_S_MAX)
        self._cam_reconnect_s.setSuffix(" 秒")
        self._cam_reconnect_s.setValue(
            proto.CAMERA_RECONNECT_INTERVAL_S_DEF)
        self._cam_reconnect_s.setToolTip(
            "断线后每隔多少秒重试一次（1~3600 秒）")
        # [会换行 2026-11-20 文字硬裁专项] 相机地址框按字体实测要 375px
        # （占位符「相机 IP，如 192.168.1.181」不许被截）⇒ 这一行在
        # 1024/800/640 宽下放不下 ⇒ 固定 QHBoxLayout 会把右边的「端口」
        # 顶出卡片（用户看到"同一行却实际不在一行"）。⇒ 改成会换行的
        # 容器：能一行就一行，放不下就折行，**绝不裁字**。
        cam_wrap1 = wrap_row([
            self._cam_enabled,
            label_group("连接方式", self._cam_resolve),
            label_group("地址", self._cam_addr),
            label_group("端口", self._cam_port),
        ])
        card_cam.body.addWidget(cam_wrap1)
        # ---- 第二行：账号/口令/断线重连/间隔（[一行放不下就分行]
        #      第一行六件在 1280 宽把「重连间隔」顶出右缘——截图实见；
        #      重连与账号同属"连接行为"，放第二行正好凑一行）----
        self._cam_user = QLineEdit()
        self._cam_user.setPlaceholderText("账号")
        self._cam_user.setMaximumWidth(110)
        self._cam_pass = QLineEdit()
        self._cam_pass.setEchoMode(QLineEdit.Password)
        # [口令不回显] 服务端只回 hasPassword、不回明文 ⇒ 框里留空 = 不改。
        self._cam_pass.setPlaceholderText("口令（留空=不改）")
        # [输入框不许裁字 2026-11-20] 原来给死 140px ⇒ 占位符「口令（留
        # 空=不改）」要 153px ⇒ 被省略号截成「口令（留空=不…」。
        # ⇒ 宽度按**字体实测**给（占位符 + 已填内容取最宽），不钉死。
        self._cam_pass.setToolTip("相机访问口令；留空表示**不修改**已保存的口令")
        # [输入框按字体实测给底宽 2026-11-20] 见 page_base.fit_input_width。
        fit_input_width(self._cam_addr)
        fit_input_width(self._cam_pass)
        # [会换行 2026-11-20] 口令框按字体实测要 171px ⇒ 这一行在窄窗
        # 放不下 ⇒ 折行，绝不裁字。
        cam_wrap_user = wrap_row([
            label_group("账号", self._cam_user),
            label_group("口令", self._cam_pass),
            self._cam_reconnect,
            label_group("重连间隔", self._cam_reconnect_s),
        ])
        card_cam.body.addWidget(cam_wrap_user)
        self._cam_stream = QComboBox()
        self._cam_stream.addItem("主码流（高分辨率）", proto.CAMERA_STREAM_MAIN)
        self._cam_stream.addItem("子码流（低分辨率，推荐）",
                                 proto.CAMERA_STREAM_MINOR)
        # [初值必须显式钉到缺省] QComboBox 首插自动选 0（=主码流），而协议
        # 缺省是**子码流**（`CAMERA_STREAM_DEF`）⇒ 不钉的话"应答到达前
        # 界面上写主码流、服务端实际按子码流跑"（同一类"界面撒谎"缺陷，
        # 2026-11-19 客户端相机段 D2 抓到）。
        self._cam_stream.setCurrentIndex(max(
            0, self._cam_stream.findData(proto.CAMERA_STREAM_DEF)))
        self._cam_fps = QComboBox()
        for fps in (1, 2, 5, 10, 15, 20):
            self._cam_fps.addItem("%d fps" % fps, fps)
        # 同上：首插自动选 0（=1fps），协议缺省 `CAMERA_FPS_DEF` 是 2fps。
        self._cam_fps.setCurrentIndex(max(
            0, self._cam_fps.findData(proto.CAMERA_FPS_DEF)))
        self._cam_width = QSpinBox()
        self._cam_width.setRange(proto.CAMERA_WIDTH_MIN,
                                 proto.CAMERA_WIDTH_MAX)
        self._cam_width.setSingleStep(64)
        self._cam_width.setSuffix(" px")
        self._cam_width.setValue(proto.CAMERA_WIDTH_DEF)
        self._cam_quality = QSpinBox()
        self._cam_quality.setRange(proto.CAMERA_QUALITY_MIN,
                                   proto.CAMERA_QUALITY_MAX)
        self._cam_quality.setSingleStep(10)
        self._cam_quality.setSuffix("（越大越清晰）")
        self._cam_quality.setValue(proto.CAMERA_QUALITY_DEF)
        # [会换行 2026-11-20] 码流下拉文字较长（「子码流（低分辨率，推荐）」）
        # ⇒ 窄窗下折行，绝不裁字。
        cam_wrap2 = wrap_row([
            label_group("码流", self._cam_stream),
            label_group("帧率", self._cam_fps),
            label_group("画面宽度", self._cam_width),
            label_group("画质", self._cam_quality),
        ])
        card_cam.body.addWidget(cam_wrap2)
        # ---- 第三行：直传架构三键（2026-11-19 用户口径「相机可以编辑的
        #      都进远端设置」）----
        self._cam_transport = QComboBox()
        self._cam_transport.addItem("画面直传（服务端不解码，20fps）",
                                    proto.CAMERA_TRANSPORT_STREAM)
        self._cam_transport.addItem("JPEG 转发（旧路，排查兜底）",
                                    proto.CAMERA_TRANSPORT_JPEG)
        self._cam_transport.setCurrentIndex(max(
            0, self._cam_transport.findData(proto.CAMERA_TRANSPORT_DEF)))
        self._cam_codec = QComboBox()
        self._cam_codec.addItem("H.264（全端可解）", proto.CAMERA_CODEC_H264)
        self._cam_codec.addItem("H.265（带宽减半，需终端支持）",
                                proto.CAMERA_CODEC_H265)
        self._cam_codec.setCurrentIndex(max(
            0, self._cam_codec.findData(proto.CAMERA_CODEC_DEF)))
        self._cam_hw = QComboBox()
        self._cam_hw.addItem("解码自动（优先 GPU 硬解）", proto.CAMERA_HW_AUTO)
        self._cam_hw.addItem("强制 GPU 硬解", proto.CAMERA_HW_ON)
        self._cam_hw.addItem("强制软解", proto.CAMERA_HW_OFF)
        self._cam_hw.setCurrentIndex(max(
            0, self._cam_hw.findData(proto.CAMERA_HW_DEF)))
        # [会换行 2026-11-20] 传输下拉文字长（「画面直传（服务端不解码，
        # 20fps）」）⇒ 窄窗下折行，绝不裁字。
        cam_wrap3 = wrap_row([
            label_group("传输", self._cam_transport),
            label_group("编码", self._cam_codec),
            label_group("本端解码", self._cam_hw),
        ])
        card_cam.body.addWidget(cam_wrap3)
        row_cam = QHBoxLayout()
        self._cam_apply = QPushButton("应用（需管理员）")
        self._cam_apply.clicked.connect(self._apply_camera)
        row_cam.addWidget(self._cam_apply)
        self._cam_state = QLabel("")
        self._cam_state.setStyleSheet(theme.caption_label_qss())
        row_cam.addWidget(self._cam_state)
        row_cam.addStretch(1)
        card_cam.body.addLayout(row_cam)
        self.root.addWidget(card_cam)

        # ---- 变频器参数（0x011B 读真实值 / 写 / 写后回读刷新）----
        #
        # [这一节做什么] 远程读/改变频器（搅拌泵驱动器）的功能码参数
        # （P1.00~P1.05 电机铭牌组）。此前铭牌类参数现场只能拿面板按。
        # [显示值只认回读] 表格里的「当前值」是服务端**刚从变频器回读**的
        # 真实值（应答 params[].value），不是本地缓存的提交值 ⇒ 写完自动
        # 回读确认，界面显示的永远是驱动器里的实际值（不撒谎）。
        # [写闸在服务端] 变频器运行中服务端拒绝写（RunLocked：请先停止），
        # 客户端把服务端的话**原样**显示，不改写文案。
        # [位置] 排在「运行保护」之后（都是设备侧的事），登录策略之前。
        card_vfdp = Card("变频器参数")
        hint_vfdp = QLabel(
            "P1 电机铭牌组（0xF100~0xF105）。当前值是服务端刚从变频器回读的"
            "真实值（不是本地缓存的提交值）；写入后自动回读确认。变频器运行中"
            "服务端拒绝写参数（RunLocked：请先停止再写）。")
        hint_vfdp.setWordWrap(True)
        hint_vfdp.setStyleSheet(theme.caption_label_qss())
        card_vfdp.add(hint_vfdp)
        self._vfdp_state = QLabel("变频器：--")
        self._vfdp_state.setStyleSheet(theme.caption_label_qss())
        card_vfdp.add(self._vfdp_state)
        # 参数表：每行 = 参数号+中文名 / 写入控件 / 回读值。
        self._vfdp_rows = []
        for _d in proto.PARAM_DEFS:
            _row = self._build_vfdp_row(_d)
            self._vfdp_rows.append(_row)
            card_vfdp.add(_row["widget"])
        self._vfdp_status = QLabel(
            "尚未读取（连上服务端后自动读取一次，或点「刷新（读真实值）」）")
        self._vfdp_status.setWordWrap(True)
        self._vfdp_status.setStyleSheet(theme.caption_label_qss())
        card_vfdp.add(self._vfdp_status)
        row_vfdp = QHBoxLayout()
        row_vfdp.setSpacing(8)
        self._vfdp_refresh = QPushButton("刷新（读真实值）")
        self._vfdp_refresh.clicked.connect(lambda: self._fetch_vfd_params())
        self._vfdp_apply = QPushButton("应用到变频器（需管理员）")
        self._vfdp_apply.clicked.connect(lambda: self._apply_vfd_params())
        row_vfdp.addWidget(self._vfdp_refresh)
        row_vfdp.addWidget(self._vfdp_apply)
        row_vfdp.addStretch(1)
        card_vfdp.body.addLayout(row_vfdp)
        self.root.addWidget(card_vfdp)

        # ---- 数据存储（SQLite 历史库；需 root）----
        card4 = Card("数据存储（SQLite：历史/日志/报警）")
        self._storage_enable = QCheckBox("启用落盘")
        card4.add(self._storage_enable)
        self._storage_path = QLineEdit()
        # [短占位 2026-11-25] 长占位（含「相对路径也按程序目录展开」）在
        # 1024/800/640 窄窗 + 1.0/1.5 缩放下被硬裁 6 处（textfit §38）⇒
        # 框内只留短句，完整口径进 tooltip（信息不丢）。
        self._storage_path.setPlaceholderText(
            "空 = 程序目录\\hsf_data.db")
        self._storage_path.setToolTip(
            "留空 = 服务端程序目录下的 hsf_data.db；相对路径也按程序目录展开")
        # [键盘类型声明] 这是文件路径：纯 ASCII（盘符/斜杠/点），且中文
        # 路径在板端反而更容易出错——锁拉丁键盘，别让人误敲出全角字符。
        from .softkeyboard import KIND_ALPHA, set_keyboard_kind
        set_keyboard_kind(self._storage_path, KIND_ALPHA)
        # [textfit §38] 这行输入框占位符较长（340px），标签挤进同列会把它
        # 压到 255px 被硬裁 ⇒ 标签单独一行、输入框独占整行（不裁字优先于
        # 标签列对齐）。
        card4.add(hint_label("库文件路径"))
        path_row = QHBoxLayout()
        path_row.setSpacing(8)
        path_row.addWidget(self._storage_path, 1)
        card4.body.addLayout(path_row)
        self._storage_sample = QSpinBox()
        self._storage_sample.setRange(1000, 3600000)
        self._storage_sample.setSingleStep(1000)
        self._storage_sample.setSuffix(" ms")
        # [初值 = 服务端缺省] 应答到达前先用缺省起控件（与服务端一致，
        # 免得界面先显示 5s、服务端实际已按 1 分钟入库）。
        self._storage_sample.setValue(proto.STORAGE_SAMPLE_MS_DEF)
        card4.body.addLayout(aligned_row("入库周期", self._storage_sample))

        # ---- 历史入库的触发方式（2026-11-12 用户定案）----
        # 持续采集入库（与自动模式无关）/ 仅自动运行时入库（停机期间不记）。
        # 只影响**落盘**：遥测采集与界面推送照旧（界面数据不缺）。
        trig_row = QHBoxLayout()
        trig_row.setSpacing(8)
        self._storage_trigger = QComboBox()
        for key, text in proto.HISTORY_TRIGGERS:
            self._storage_trigger.addItem(text, key)
        self._storage_trigger.setMinimumWidth(280)
        self._storage_trigger.setToolTip(
            "持续采集入库：只要落盘开着就一直按周期记历史；\n"
            "仅自动运行时入库：只有自动模式在跑时才记（停机期间不记）")
        trig_row.addWidget(label_group("入库触发", self._storage_trigger), 1)
        card4.body.addLayout(trig_row)

        # ---- 容量限额（现场硬盘小，必须能封顶）----
        limit_row = QHBoxLayout()
        # [会换行 2026-11-14] 这一行原来塞了「容量上限+框」「写满后+下拉」
        # 「保留分段+框」共 6 个控件 ⇒ 最小宽 ≈650 ⇒ **把整页内容撑到
        # 708 > 视口 515** ⇒ 页面出横向滚动条 ⇒ 右边的控件被裁 ⇒ 用户
        # 看到「同一行却实际不在一行」。⇒ 三组各成一组（组内不拆散），
        # 窄窗时整组折到下一行。
        self._storage_max = QSpinBox()
        self._storage_max.setRange(proto.STORAGE_MAX_MB_MIN,
                                   proto.STORAGE_MAX_MB_MAX)
        self._storage_max.setSingleStep(50)
        self._storage_max.setSuffix(" MB")
        self._storage_max.setSpecialValueText("不限")
        self._storage_max.setToolTip("0 = 不限；库达到上限后按下方策略处理")
        self._storage_max.setMinimumWidth(130)
        # [初值 = 服务端缺省] 应答到达前先显示缺省 50MB（与服务端一致）。
        self._storage_max.setValue(proto.STORAGE_MAX_MB_DEF)
        self._storage_policy = QComboBox()
        for key, text in proto.STORAGE_POLICIES:
            self._storage_policy.addItem(text, key)
        self._storage_policy.setMinimumWidth(260)
        self._storage_policy.currentIndexChanged.connect(
            self._on_storage_policy)
        self._storage_keep = QSpinBox()
        self._storage_keep.setRange(1, 99)
        self._storage_keep.setToolTip("仅「分段滚动」策略用：总上限会均分到各段")
        self._storage_keep.setMinimumWidth(80)
        limit_wrap = wrap_row([
            label_group("容量上限", self._storage_max),
            label_group("写满后", self._storage_policy),
            label_group("保留分段", self._storage_keep),
        ])
        limit_row.addWidget(limit_wrap, 1)
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
        self._storage_save = QPushButton("保存（需管理员）")
        self._storage_save.clicked.connect(self._apply_storage)
        row4.addWidget(self._storage_save)
        row4.addStretch(1)
        card4.body.addLayout(row4)
        self.root.addWidget(card4)

        # ---- 登录策略（账号；与 C 客户端同键同义，2026-11-08）----
        #
        # [为什么要有这块] 服务端登录策略（匿名/同账并发/会话上限）原先只能
        # 改服务端侧的 hsf_login_policy.json（还要重启）；多客户端联调、
        # 压测、多端围观都要用。现经 CMD_CONFIG_GET/SET 的 login 节下发，
        # 管理员在线可改、立即对新登录生效并落盘。
        card5 = Card("登录策略（账号）")
        self._login_anon = QCheckBox("允许匿名登录（只读）")
        card5.add(self._login_anon)
        self._login_concur = QCheckBox("允许同账并发（同账户多端在线）")
        card5.add(self._login_concur)
        login_row = QHBoxLayout()
        login_row.addWidget(QLabel("会话上限"))
        self._login_max = QSpinBox()
        self._login_max.setRange(proto.CFG_LOGIN_MAX_SESSIONS_MIN,
                                 proto.CFG_LOGIN_MAX_SESSIONS_MAX)
        self._login_max.setSpecialValueText("不限")
        self._login_max.setSuffix(" 个")
        self._login_max.setMinimumWidth(120)
        login_row.addWidget(self._login_max)
        login_row.addStretch(1)
        card5.body.addLayout(login_row)
        hint5 = QLabel(
            "写入立即对下一次登录生效并落盘 hsf_login_policy.json；"
            "已在线会话不受影响（关闭同账并发不会踢人）。")
        hint5.setWordWrap(True)
        hint5.setStyleSheet(theme.caption_label_qss())
        card5.add(hint5)
        login_btns = QHBoxLayout()
        self._login_save = QPushButton("应用登录策略（需管理员）")
        self._login_save.clicked.connect(self._apply_login)
        login_btns.addWidget(self._login_save)
        login_btns.addStretch(1)
        card5.body.addLayout(login_btns)
        self.root.addWidget(card5)

        self.root.addStretch(1)
        self._install_toc()

    # ---- 页内导航树（同 web 远端设置，2026-11-22）----
    #
    # [为什么要在滚动壳外面] TOC 必须**固定**：放进 self.scroll 会跟着卡片
    # 一起滚走。这里把 Page 基类外层布局里的 self.scroll 取出来，换成
    # 「TOC | scroll」两栏；卡片内容与滚动行为完全不变。
    #
    # [条目来源] 直接扫 self.root 里的 Card（标题即条目文字）⇒ 以后加卡
    # 自动进 TOC，不用维护第二份手写清单（web 端也是同一口径）。
    #
    # [窄页隐藏] 页宽 < TOC_MIN_PAGE_W 时隐藏（紧凑/手机档卡片优先）；
    # 宽窄切换由 resizeEvent 实时处理。

    def _install_toc(self):
        cards = []
        for i in range(self.root.count()):
            item = self.root.itemAt(i)
            w = item.widget() if item is not None else None
            if isinstance(w, Card) and w.title_label is not None:
                cards.append((w.title_label.text(), w))
        if not cards:
            return
        self._toc_cards = cards

        inner = QWidget()
        inner.setObjectName("HsfNavPanel")
        inner.setAttribute(Qt.WA_StyledBackground, True)
        inner.setStyleSheet(theme.nav_panel_qss())
        tlay = QVBoxLayout(inner)
        tlay.setContentsMargins(6, 4, 6, 8)
        tlay.setSpacing(2)
        title = QLabel("导航")
        title.setObjectName("HsfNavTitle")
        tlay.addWidget(title)

        self._toc_buttons = []
        need = 0
        for text, card in cards:
            # 短标签：卡标题里的括号说明（数据存储（SQLite…）/登录策略（账号））
            # 只进 tooltip；TOC 里用主名，避免把侧栏撑到 300px+。
            short = text.split("（")[0].strip() or text
            btn = QPushButton(short)
            btn.setObjectName("HsfNavItem")
            btn.setCheckable(True)
            btn.setCursor(Qt.PointingHandCursor)
            btn.setToolTip(text)
            btn.clicked.connect(
                lambda _checked=False, c=card: self._toc_jump(c))
            tlay.addWidget(btn)
            self._toc_buttons.append((card, btn))
            fm = QFontMetrics(btn.font())
            need = max(need, fm.horizontalAdvance(short))
        tlay.addStretch(1)

        # 宽度按**短标签**实测（同 NavPanel 的字体实测口径；QSS padding
        # 左右各 12 + 面板内容边距左右各 6 + 余量 8），不写死像素。
        inner.setMinimumWidth(need + 12 * 2 + 6 * 2 + 8)

        toc_scroll = QScrollArea()
        toc_scroll.setObjectName("HsfTocScroll")
        toc_scroll.setWidgetResizable(True)
        toc_scroll.setFrameShape(QFrame.NoFrame)
        toc_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        toc_scroll.setStyleSheet(
            "QScrollArea#HsfTocScroll { background: transparent; border: none; }")
        toc_scroll.setWidget(inner)
        toc_scroll.setFixedWidth(inner.minimumWidth() + 2)
        self._toc_scroll = toc_scroll

        outer = self.layout()
        outer.removeWidget(self.scroll)
        row = QWidget()
        row.setObjectName("HsfSettingsTocRow")
        hl = QHBoxLayout(row)
        hl.setContentsMargins(0, 0, 0, 0)
        hl.setSpacing(10)
        hl.addWidget(toc_scroll)
        hl.addWidget(self.scroll, 1)
        outer.addWidget(row)

        # 滚动高亮：取「顶端已越过视口参考线且最靠下」的那张卡；滚到底
        # 直接高亮最后一条（末卡顶端可能永远到不了参考线）。
        self.scroll.verticalScrollBar().valueChanged.connect(self._toc_sync)
        #: [点击保护窗] 点条目后 700ms 内忽略滚动重算：目标卡若因页面不够
        #: 长被夹到最大滚动位（如点「数据存储」而它已在末尾附近），滚动
        #: 重算会按「滚到底高亮最后一条」把它改成「登录策略」——点了 A
        #: 却亮 B。窗口过后用户真正滚动时再交还给 scrollspy。
        self._toc_guard_until = 0.0
        # [首次同步必须等布局] 这里页面还没 show/排过版，所有卡的 y 都是 0
        # ⇒ 立刻 sync 会把最后一条判成 active（实拍：进页高亮「登录策略」、
        # 页面明明在最上面）。投递到事件循环下一拍，等布局给了真实坐标。
        QTimer.singleShot(0, self._toc_sync)
        self._sync_toc_visibility()

    def _sync_toc_visibility(self):
        if not hasattr(self, "_toc_scroll"):
            return
        want = self.width() >= self.TOC_MIN_PAGE_W
        if self._toc_scroll.isVisible() != want:
            self._toc_scroll.setVisible(want)

    def _toc_set_active(self, active):
        for _card, btn in self._toc_buttons:
            btn.setChecked(btn is active)

    def _toc_jump(self, card):
        """点 TOC 条目 ⇒ 滚到对应卡片（留 6px 顶部余量）。"""
        y = card.mapTo(self.content, QPoint(0, 0)).y()
        self._toc_guard_until = time.monotonic() + 0.7
        self.scroll.verticalScrollBar().setValue(max(0, y - 6))
        for c, btn in self._toc_buttons:
            if c is card:
                self._toc_set_active(btn)
                break

    def _toc_sync(self, *_args):
        """按当前滚动位高亮 TOC 条目（滚动/点击共用）。"""
        if not getattr(self, "_toc_buttons", None):
            return
        if time.monotonic() < getattr(self, "_toc_guard_until", 0.0):
            return
        bar = self.scroll.verticalScrollBar()
        top = bar.value()
        active = self._toc_buttons[0][1]
        for card, btn in self._toc_buttons:
            y = card.mapTo(self.content, QPoint(0, 0)).y()
            if y <= top + 8:
                active = btn
        if bar.value() >= bar.maximum() - 2 and bar.maximum() > 2:
            active = self._toc_buttons[-1][1]
        for _card, btn in self._toc_buttons:
            btn.setChecked(btn is active)

    def resizeEvent(self, ev):
        Page.resizeEvent(self, ev)
        self._sync_toc_visibility()

    def showEvent(self, ev):
        Page.showEvent(self, ev)
        self._sync_toc_visibility()
        # 页面显示后布局才定型（预建页创建时是隐藏的）⇒ 下一拍按真实
        # 卡坐标刷新高亮，避免停在最后一条。
        QTimer.singleShot(0, self._toc_sync)

    def on_enter(self):
        # 切到本页拉一次配置快照（回显服务端当前值；连接建立后才有意义）。
        self._fetch_config(silent=True)
        # [2026-11-18] 变频器参数：进页静默读一次真实值（读=登录即可）。
        self._fetch_vfd_params()

    def _set_mode(self, mode):
        if not self.ctx.bridge.can(proto.PERM_MODE_SWITCH):
            self.ctx.set_status("权限不足：切换运行模式需要管理员账户")
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
        if not self.ctx.bridge.can(proto.PERM_AUTO_CONTROL):
            self.ctx.set_status("权限不足：自动运行控制需要管理员账户")
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

    # ---- 自动模式配置（产水/反洗/顺洗）----

    def _build_auto_mode_block(self, name):
        """建一个模式块（设备多选 + 搅拌电机转速/方向），返回控件字典。

        [空选要有独立文案] 允许先留空（配置可以暂不定），但旧 MultiCheckCombo
        的兜底文案会让"空选"与"全选"在按钮上长得一样；故传 empty_text=
        "未选择"（该参数 2026-11-11 加入，缺省不传时行为与旧版一致）。
        """
        root = QWidget()
        # [不要给块根挂裸 background 样式表] 无选择器的 `background:…` 会
        # 级联到全部子孙控件——模式块里的 MultiCheckCombo/方向下拉没 own
        # QSS，被刷成透明后整条画成黑块（离屏截图实见）。QWidget 本来就
        # 不自绘背景，这里什么都不用设。
        lay = QVBoxLayout(root)
        lay.setContentsMargins(0, 6, 0, 0)
        lay.setSpacing(4)
        lay.addWidget(section_title(name))

        devices = MultiCheckCombo([t for _i, t in proto.AUTO_DEVICES],
                                  all_text="全部设备", empty_text="未选择",
                                  initial="none", min_checked=0)
        devices.setMinimumWidth(150)
        # [宽度上限与历史页同口径] 不设上限会一路铺满整行（截图实见）；
        # 420px 装得下"电动球阀1、电动球阀2、进水泵"这类常见勾选组合，
        # 全选时收敛为短文案「全部设备」不占宽。
        devices.setMaximumWidth(420)
        devices.setToolTip("勾选该模式要启动的设备（可多选；点外面收起）")
        # [不用 form_row 的 stretch=1] 多选按钮有 420px 上限，行内剩余空间
        # 会被 QBoxLayout 摊成"项间大空隙"把按钮顶到最右边（截图实见）；
        # 让按钮贴着标签、行尾统一收空。
        row1 = QHBoxLayout()
        row1.setSpacing(8)
        lbl1 = QLabel("启动设备")
        lbl1.setMinimumWidth(110)
        lbl1.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        row1.addWidget(lbl1)
        row1.addWidget(devices)
        row1.addStretch(1)
        lay.addLayout(row1)

        row2 = QHBoxLayout()
        row2.setSpacing(8)
        lbl2 = QLabel("搅拌电机")
        lbl2.setMinimumWidth(110)
        lbl2.setStyleSheet("QLabel { color: palette(text); background: transparent; }")
        row2.addWidget(lbl2)
        rpm = QSpinBox()
        rpm.setRange(proto.AUTO_STIR_RPM_MIN, proto.AUTO_STIR_RPM_MAX)
        # [初值 = 铭牌额定点] 与变频器页「转速调速」缺省口径一致；服务端
        # auto 节接入后以应答回显为准。
        rpm.setValue(proto.AUTO_STIR_RPM_DEF)
        rpm.setSuffix(" rpm")
        row2.addWidget(rpm)
        # [折算提示] 转速是操作员语言、Hz 是变频器实发量，两个都给
        # （现场口径 50Hz≈1440rpm；数值变了即时重算）。
        hz_hint = QLabel("")
        hz_hint.setStyleSheet(theme.caption_label_qss())

        def _sync_hz_hint(v, _lbl=hz_hint):
            _lbl.setText("折算 ≈ %.1f Hz" % (
                v / proto.AUTO_STIR_RATED_RPM * proto.AUTO_STIR_RATED_HZ))

        rpm.valueChanged.connect(_sync_hz_hint)
        _sync_hz_hint(rpm.value())
        reverse = QComboBox()
        # userData = reverse?（与变频器页 dir_combo 同口径：False=正转）。
        reverse.addItem("正转", False)
        reverse.addItem("反转", True)
        # [会换行的内容行 2026-11-14] 「转速+折算」与「方向」各成一组，
        # 组内不拆散；窄窗时整组折到下一行 ⇒ 行宽不再被这一行撑爆
        # （1.5x 缩放下这一行的最小宽 ≈ 578px ⇒ 窄窗时整页出横向滚动条，
        #  右边被裁 ⇒ 用户看到「同一行却实际不在一行」）。
        rpm_grp = QWidget()
        rpm_grp_lay = QHBoxLayout(rpm_grp)
        rpm_grp_lay.setContentsMargins(0, 0, 0, 0)
        rpm_grp_lay.setSpacing(6)
        rpm_grp_lay.addWidget(rpm)
        rpm_grp_lay.addWidget(hz_hint)
        dir_grp = QWidget()
        dir_grp_lay = QHBoxLayout(dir_grp)
        dir_grp_lay.setContentsMargins(0, 0, 0, 0)
        dir_grp_lay.setSpacing(6)
        dir_lbl = QLabel("方向")
        dir_lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        dir_grp_lay.addWidget(dir_lbl)
        dir_grp_lay.addWidget(reverse)
        motor_wrap = wrap_row([rpm_grp, dir_grp])
        row2.addWidget(motor_wrap, 1)
        lay.addLayout(row2)

        # ---- 加速度（2026-11-25 用户口径「搅拌机变频器增加加速度设置，
        #      用指定的加速度加速到设定转速」）----
        # 单位 rpm/s；0 = 跟随变频器当前加速时间（不写 0xF011）。
        # 服务端换算：满量程加速时间 = 额定转速 / 加速度 ⇒ 从 0 到本模式
        # 设定转速约 设定转速 / 加速度 秒。
        row3 = QHBoxLayout()
        row3.setSpacing(8)
        lbl3 = QLabel("加速度")
        lbl3.setMinimumWidth(110)
        lbl3.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        row3.addWidget(lbl3)
        accel = QSpinBox()
        accel.setRange(proto.AUTO_STIR_ACCEL_RPM_S_MIN,
                       proto.AUTO_STIR_ACCEL_RPM_S_MAX)
        accel.setSingleStep(10)
        accel.setValue(proto.AUTO_STIR_ACCEL_RPM_S_DEF)
        accel.setSuffix(" rpm/s")
        accel.setSpecialValueText("跟随变频器")
        # [宽度 2026-11-25 实拍修] QSpinBox 缺省横向 Expanding ⇒ 单独成组
        # 时被拉满整行（截图里 600px 宽，提示飘到最右）。固定为 sizeHint
        # 宽度 + 最小 180（1.5x 下「跟随变频器」也放得下）。
        accel.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        accel.setMinimumWidth(180)
        accel.setToolTip(
            "搅拌变频器加速度（rpm/s）：从 0 加到本模式设定转速的速率。\n"
            "0 = 跟随变频器当前加速时间（不写参数）。")
        accel_hint = QLabel("")
        accel_hint.setStyleSheet(theme.caption_label_qss())

        def _sync_accel_hint(*_a):
            a = accel.value()
            r = rpm.value()
            if a <= 0:
                # [不重复特殊值文案] 框内已经显示「跟随变频器」⇒ 旁边留空。
                accel_hint.setText("")
            else:
                # [短文案 2026-11-25] A6s 窄窗不许出横向滚动条：长句
                # 「到 720 rpm 约 2.4 秒」的 minimumSizeHint 会把内容宽顶到
                # 637 > 626 ⇒ 出横滚。速率/目标已在 tooltip 与本行标签里。
                accel_hint.setText("≈ %.1f 秒" % (r / float(a)))

        rpm.valueChanged.connect(_sync_accel_hint)
        accel.valueChanged.connect(_sync_accel_hint)
        _sync_accel_hint()
        accel_grp = QWidget()
        accel_grp_lay = QHBoxLayout(accel_grp)
        accel_grp_lay.setContentsMargins(0, 0, 0, 0)
        accel_grp_lay.setSpacing(6)
        accel_grp_lay.addWidget(accel)
        accel_grp_lay.addWidget(accel_hint)
        accel_grp_lay.addStretch(1)
        row3.addWidget(accel_grp, 1)
        lay.addLayout(row3)

        # ---- 停止 / 急停时是否复位电动球阀（2026-11-12 用户定案）----
        #
        # [为什么可配] 停止有两种合理诉求：关阀复位（回安全位）vs 保持
        # 现状（阀刚开着的留着等下一步继续用，免得每次都白走 10s 行程）。
        # 急停另说：它是人身安全事件，停泵与变频器是必须的，阀**默认不
        # 动**（可能正停在中间位，叠一条"关"是叠加未知动作）。
        # [切模式不受这两个开关影响] 「切换模式 ⇒ 关闭所有设备（含阀）」是
        # 既有安全契约，服务端切模式路径显式走"一定关阀"，与本设置无关。
        rst_row = QHBoxLayout()
        rst_row.setSpacing(8)
        lbl_rst = QLabel("阀复位")
        lbl_rst.setMinimumWidth(110)
        lbl_rst.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        rst_row.addWidget(lbl_rst)
        rst_stop = QCheckBox("停止时关阀复位")
        rst_stop.setChecked(proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF)
        rst_stop.setToolTip("停止自动运行时：勾选=把电动球阀关回原位；"
                            "不勾=阀保持现状")
        rst_estop = QCheckBox("急停时关阀复位")
        rst_estop.setChecked(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF)
        rst_estop.setToolTip("急停时：勾选=把电动球阀关回原位；"
                             "不勾=阀保持现状（缺省，急停只停泵与变频器）")
        # [会换行的内容行 2026-11-14] 两个复选框在窄窗下**一行放不下**
        # （实测 700px 窗下第二个被卡片右边界裁掉 ⇒ 用户看到「同一行却
        #  实际不在一行」）⇒ 放进**会换行**的容器：能一行就一行，放不下就
        # 折成两行，**绝不裁字**（与 adaptive_columns 同款纪律）。
        rst_wrap = wrap_row([rst_stop, rst_estop])
        rst_row.addWidget(rst_wrap, 1)
        lay.addLayout(rst_row)

        # [换相策略搬进循环实例 2026-11-26] 单模式的两个策略下拉已删——
        # 「进入某模式时泵怎么处理」现在配置在**循环实例**上（每个实例自带
        # stirSwitchPolicy/inletSwitchPolicy），模式块不再有这两行。
        return {"root": root, "devices": devices, "rpm": rpm,
                "reverse": reverse, "hz_hint": hz_hint,
                "accel": accel, "accel_hint": accel_hint,
                "rst_stop": rst_stop, "rst_estop": rst_estop,
                "rst_wrap": rst_wrap}

    def _auto_mode_block_values(self, blk):
        """单模式块的当前 UI 值 → **可被循环实例继承**的六键 dict。

        （设备/转速/方向/加速度/阀复位两键；切换条件与换向策略只在实例
        卡上有，不在继承范围。）
        """
        return {
            "devices": [proto.AUTO_DEVICES[i][0]
                        for i in blk["devices"].checked_indexes()],
            "stirRpm": blk["rpm"].value(),
            "stirReverse": bool(blk["reverse"].currentData()),
            # [2026-11-25] 搅拌变频器加速度（rpm/s；0=跟随变频器）。
            proto.AUTO_MODE_STIR_ACCEL_RPM_S: blk["accel"].value(),
            # [停止/急停时是否关阀复位 2026-11-12]
            proto.AUTO_MODE_RESET_VALVES_ON_STOP:
                bool(blk["rst_stop"].isChecked()),
            proto.AUTO_MODE_RESET_VALVES_ON_ESTOP:
                bool(blk["rst_estop"].isChecked()),
        }

    def _auto_config_values(self):
        """把四个模式块 + 循环配置取齐成 auto 配置节载荷（**一次提交**）。

        [键形即契约] 服务端按这个形状实现（proto.CFG_SECTION_AUTO 注释的
        两端同步点）；devices 是 proto.AUTO_DEVICES 的稳定 id 列表。
        [两张卡一次提交 2026-11-12 用户定案] 「自动模式配置」与「自动循环
        配置」合成**一次**下发（服务端 0x0117 支持 modes + cycle 同批）——
        两张卡各放一个「应用」按钮会让人以为要分两次保存，实际它们是
        同一份配置。
        """
        # [服务端认识的模式集合 2026-11-18] 回显里出现过的模式 id ⇒
        # 服务端认这些（老服务端只认三模式 ⇒ 别盲发第四个键，服务端
        # 会整包拒）。还没回显（未知）⇒ 只发**通用子集**（三模式）。
        known = getattr(self, "_auto_known_modes", None)
        if known is None:
            allowed = set(m for m, _t in proto.AUTO_MODES_COMMON)
        else:
            allowed = set(known)
        modes = {}
        skipped = []
        for blk in self._auto_mode_blocks:
            if blk["id"] not in allowed:
                skipped.append(blk["id"])
                continue
            # [_auto_mode_block_values 2026-11-26] 六键口径与「添加实例
            # 继承单模式」共用一份 helper，防两处漂移。换相策略键已删
            # （stirSwitchPolicy/inletSwitchPolicy 搬进循环实例，单模式
            # modes 不再提交；服务端收到也按「未知键忽略」处理）。
            modes[blk["id"]] = self._auto_mode_block_values(blk)
        # [被跳过的模式 2026-11-18] 记下来 ⇒ 状态行要说明（不静默丢）。
        self._auto_skipped_modes = skipped
        lights = {
            proto.AUTO_LIGHT_START_ON:
                self._auto_lights["start"]["on"].value(),
            proto.AUTO_LIGHT_START_OFF:
                self._auto_lights["start"]["off"].value(),
            proto.AUTO_LIGHT_STOP_ON:
                self._auto_lights["stop"]["on"].value(),
            proto.AUTO_LIGHT_STOP_OFF:
                self._auto_lights["stop"]["off"].value(),
            proto.AUTO_LIGHT_RESET_ON:
                self._auto_lights["reset"]["on"].value(),
            proto.AUTO_LIGHT_RESET_OFF:
                self._auto_lights["reset"]["off"].value(),
        }
        return {"modes": modes,
                # [2026-11-25] 搅拌最高转速（auto 节顶层；服务端统一钳位）。
                proto.AUTO_STIR_MAX_RPM: self._auto_stir_max.value(),
                proto.CFG_AUTO_CYCLE: self._auto_cycle_values(),
                proto.CFG_AUTO_LIGHTS: lights}

    def _populate_auto(self, auto):
        """把 auto 配置节铺到四个模式块上（应答带哪些模式铺哪些）。

        [未知设备 id 丢弃] 服务端将来若增删设备清单，旧 id 静默跳过，
        不让一次回显把多选下拉撑出未知项。
        """
        modes = auto.get("modes") or {}
        # [服务端认识的模式集合 2026-11-18] 回显里有哪个模式 ⇒ 服务端
        # 认哪个（老服务端只认三模式 ⇒ 载荷别盲发第四个键）。
        self._auto_known_modes = set(modes.keys()) or None
        # [2026-11-25] 最高转速：应答带键才铺（老服务端缺键 ⇒ 保持现值）；
        # 先更新上限，再铺各模式转速 ⇒ 超限旧值会被 QSpinBox 自动夹回。
        if proto.AUTO_STIR_MAX_RPM in auto:
            try:
                self._auto_stir_max.setValue(int(
                    auto.get(proto.AUTO_STIR_MAX_RPM)))
            except (TypeError, ValueError):
                pass
        _lim = self._auto_stir_max.value()
        for _b in self._auto_mode_blocks:
            _b["rpm"].setMaximum(_lim)
        id_to_idx = {dev_id: i for i, (dev_id, _t) in
                     enumerate(proto.AUTO_DEVICES)}
        for blk in self._auto_mode_blocks:
            m = modes.get(blk["id"]) or {}
            blk["devices"].set_checked_indexes(
                id_to_idx[d] for d in (m.get("devices") or [])
                if d in id_to_idx)
            blk["rpm"].setValue(int(m.get("stirRpm",
                                          proto.AUTO_STIR_RPM_DEF) or 0))
            # [2026-11-25] 加速度回显：缺键（老服务端）⇒ 0=跟随变频器。
            blk["accel"].setValue(int(m.get(
                proto.AUTO_MODE_STIR_ACCEL_RPM_S,
                proto.AUTO_STIR_ACCEL_RPM_S_DEF) or 0))
            idx = blk["reverse"].findData(bool(m.get("stirReverse", False)))
            blk["reverse"].setCurrentIndex(idx if idx >= 0 else 0)
            # [阀复位开关回显 2026-11-12] 缺键（老服务端）⇒ 保持缺省。
            blk["rst_stop"].setChecked(bool(
                m.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                      proto.AUTO_MODE_RESET_VALVES_ON_STOP_DEF)))
            blk["rst_estop"].setChecked(bool(
                m.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP,
                      proto.AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF)))
            # [换相策略键已删 2026-11-26] 应答 modes 里若残留这两个旧键
            # （老服务端或过渡期数据）⇒ 忽略不铺——单模式已不再有策略下拉，
            # 策略归循环实例。
        # [按钮灯闪烁 2026-11-24] 只在应答确实带 lights 时铺（缺键=老服务端
        # ⇒ 保持控件缺省，不静默改用户的灯参数）。**逐键判在**：局部/旧版
        # 应答只带其中几个键时，缺的那个键保持当前值（不按缺省值误改用户
        # 已配的灯时长）。服务端值越界时夹到 50~5000ms（与控件量程同源）。
        lights = auto.get(proto.CFG_AUTO_LIGHTS)
        if isinstance(lights, dict):
            for lkey, on_key, off_key in (
                    ("start", proto.AUTO_LIGHT_START_ON,
                     proto.AUTO_LIGHT_START_OFF),
                    ("stop", proto.AUTO_LIGHT_STOP_ON,
                     proto.AUTO_LIGHT_STOP_OFF),
                    ("reset", proto.AUTO_LIGHT_RESET_ON,
                     proto.AUTO_LIGHT_RESET_OFF)):
                for ctl_key, src_key in (("on", on_key), ("off", off_key)):
                    if src_key not in lights:
                        continue
                    try:
                        val = int(lights.get(src_key))
                    except (TypeError, ValueError):
                        continue
                    self._auto_lights[lkey][ctl_key].setValue(
                        max(proto.AUTO_BLINK_MS_MIN,
                            min(proto.AUTO_BLINK_MS_MAX, val)))
        # [自动循环子节 2026-11-12] auto 节里带 cycle 才铺（老服务端没有
        # 这个子节 ⇒ 保持缺省，不误改）。
        self._populate_cycle(auto.get(proto.CFG_AUTO_CYCLE) or {})

    def _apply_auto(self):
        """应用自动配置（auto 配置节：模式 + 自动循环 + 按钮灯闪烁；需 root）。

        [一次提交两张卡 2026-11-12 用户定案] 「自动模式配置」与「自动循环
        配置」是同一份 auto 配置节 ⇒ **一次下发**（服务端 0x0117 支持
        modes + cycle 同批），不再两张卡各提交一次。[按钮灯闪烁 2026-11-24]
        lights 子节随这份 auto 配置**同批**提交。

        [服务端已接入 2026-11-11] Python 服务端运行期生效 + 落盘；ok 应答
        带 auto 节 → configReady → `_populate_auto` 回显（「界面显示的就
        是生效的」）。C 端待同步（proto 注释的两端同步点）。
        """
        if not self._config_gate():
            return
        # [不本地预判 2026-11-18] 曾想在这里先拦「参与模式 < 2 个」
        # （服务端确实会拒），但回归 A8f15/A8f16 明写口径是
        # **「由服务端判」+ 客户端显示服务端原话** ⇒ 客户端不重复判，
        # 原样把服务端的拒绝文案透出去（单一事实源）。
        values = self._auto_config_values()
        self.ctx.set_status("正在应用自动配置（模式 + 循环 + 按钮灯）…")

        def _done(doc):
            # [网络线程] 与采集/存储同款：只 emit / 走状态栏，不碰控件。
            if doc.get("ok"):
                # [应用成功 ⇒ 清 dirty 2026-11-18] 已提交的内容 == 服务端确认
                # 的内容 ⇒ 之后的回显可以照铺（不再需要"用户动过就跳过
                # 覆盖"的保护）；不清的话回显会被当成旧状态跳过 ⇒ 界面与
                # 服务端对不上。
                self._cycle_set_dirty(False)
                self.configReady.emit(doc)
                self.ctx.set_status(
                    "自动配置已生效（模式 + 循环 + 按钮灯）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set(proto.CFG_SECTION_AUTO, values,
                                   on_done=_done)

    # ---- 自动循环：实例列表（2026-11-26 实例化）----
    #
    # [一行一个实例] 行内：序号 + 标题（模式名+同类序号）+ 设置/上移/下移/
    # 删除。每个实例的全套参数（设备/四触发/转速/方向/加速度/阀复位/换相
    # 策略）不在行里铺开（行会很高且挤），收进「设置」弹的实例设置卡。
    # [增删/移动后整表重算标题] titles 规则：标题 = 模式显示名 + 同类出现
    # 序号（"产水模式1""产水模式2"…），按**当前列表顺序**内的出现顺序编，
    # 删掉前面的实例后后面的重算。
    # [不 setParent 重排纪律 2026-11-19] 行控件本来就是 `_cycle_list_box`
    # 的子控件：排序用 takeAt（从布局摘掉，父控件不变 ⇒ 不隐藏 ⇒ 不丢
    # 焦点）+ addWidget 重挂；**一个 setParent 都不能调**（它一律隐藏
    # 控件 ⇒ 软键盘 FocusOut 自灭的实锅见 A8i）。删除/重建行走 deleteLater。

    def _cycle_set_dirty(self, dirty):
        """设置「有未应用修改」标志并刷新提示（2026-11-27 工作流实证：
        用户改完实例没点「应用自动配置」⇒ 服务端根本没收到，是本次
        「设置没生效」的真实原因）。"""
        self._cycle_dirty = bool(dirty)
        self._cycle_refresh_state_hint()

    def _cycle_mark_dirty(self):
        self._cycle_set_dirty(True)

    def _cycle_refresh_state_hint(self):
        lbl = getattr(self, "_cycle_state_hint", None)
        if lbl is None:
            return
        if getattr(self, "_cycle_dirty", False):
            lbl.setText("⚠ 有未应用的修改：点底部「应用自动配置」才会下发；"
                        "若自动循环正在运行，需停止后重新启动才使用新参数。")
            lbl.setStyleSheet(
                "QLabel { color: %s; background: transparent; }"
                % theme.CLR_WARN)
        else:
            lbl.setText("")
            lbl.setStyleSheet(theme.caption_label_qss())

    def _build_cycle_row(self, data):
        """建一个实例行（返回行控件字典；data=实例配置 dict，就地引用）。

        [组合行 2026-11-27] 组行在名字下面多一行 caption 摘要
        「N 个模式 × M 遍」；单模式行的摘要**恒占位但高度 0**（空文本，
        不显隐改结构）⇒ 行高只由数据类型决定，运行期不会跳动。
        """
        row = QWidget()
        h = QHBoxLayout(row)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(8)
        order = QLabel("1")
        order.setMinimumWidth(18)
        order.setAlignment(Qt.AlignCenter)
        name = QLabel("")
        name.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        summary = QLabel("")
        summary.setStyleSheet(theme.caption_label_qss())
        # [摘要恒占位] 组行才放开高度（_cycle_render 按数据类型设）；单模式
        # 行固定 0 高 ⇒ 与改造前的行高一致（A8f5b 等几何不变纪律）。
        summary.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        summary.setFixedHeight(0)
        info = QVBoxLayout()
        info.setContentsMargins(0, 0, 0, 0)
        info.setSpacing(0)
        info.addWidget(name)
        info.addWidget(summary)
        is_group = (str(data.get(proto.AUTO_INST_MODE) or "")
                    == proto.AUTO_INST_GROUP)
        settings = QPushButton("设置")
        settings.setToolTip(
            "设置该组合：名称、循环遍数与组内模式（子模式各自配置）"
            if is_group else
            "设置该实例的启动设备与切换条件（压差/进水压力/出水压力/"
            "浊度/定时）、转速/方向/加速度/阀复位/换相策略")
        up = QPushButton("上移")
        down = QPushButton("下移")
        up.setToolTip("往上挪一位（顺序＝循环顺序，序号 1 ＝ 起始实例）")
        down.setToolTip("往下挪一位")
        delete = QPushButton("删除")
        delete.setToolTip("从循环里删除该实例")
        # [禁用态要看得见 2026-11-18] 主题的 QPushButton:disabled 在页面
        # 继承链上不一定落到按钮上 ⇒ 本地补 :disabled 规则（同旧行口径）。
        up.setStyleSheet(theme.disabled_button_qss())
        down.setStyleSheet(theme.disabled_button_qss())
        h.addWidget(order)
        h.addLayout(info, 1)
        h.addWidget(settings)
        h.addWidget(up)
        h.addWidget(down)
        h.addWidget(delete)
        widgets = {"row": row, "order": order, "name": name,
                   "summary": summary, "summary_h": 0,
                   "settings": settings, "up": up, "down": down,
                   "del": delete, "data": data, "head_h": 0}
        # [按行对象连线，不按下标] 上移/下移/删除会改变下标 ⇒ 回调按行
        # 字典对象找当下位置（self._cycle_rows.index(w)），不缓存下标。
        settings.clicked.connect(
            lambda _b=False, w=widgets: self._on_cycle_settings(w))
        up.clicked.connect(
            lambda _b=False, w=widgets: self._cycle_move(w, -1))
        down.clicked.connect(
            lambda _b=False, w=widgets: self._cycle_move(w, +1))
        delete.clicked.connect(
            lambda _b=False, w=widgets: self._on_cycle_delete(w))
        return widgets

    def _cycle_retitle(self):
        """整表刷标题：标题栏写实例的 `title`（添加/保存时已按同层去重）。

        [默认名口径 2026-11-27 用户订正] 名字在**添加时**就写进 title
        （产水模式 / 产水模式2 / 组合 / 组合2…），界面不再做"模式名+序号"
        的显示期编号；删掉一行也不会把后面的名字重排（名字是用户的，不是
        位置算出来的）。空 title 只在异常/手工数据里出现 ⇒ 回落默认名。
        """
        for w in self._cycle_rows:
            w["name"].setText(_inst_display_name(w["data"]))

    def _cycle_row_min_h(self, row):
        """一行的最小高度（按控件 minimumSizeHint 算，与排版状态无关）。

        [行高恒定纪律] 单模式行结构相同（单行控件）⇒ 高度天然一致；组合行
        多一行 caption 摘要 ⇒ 下限把「名字 + 摘要」两行加上（单模式行的
        摘要固定 0 高 ⇒ 口径不变）。这里仍按首帧量到的按钮 sizeHint
        （head_h）与各控件 minimumSizeHint 取 max 钉下限，避免字体 polish
        前后差 1~2px 让列表上下跳。
        """
        h_head = int(row.get("head_h") or 0)
        for key in ("order", "name", "settings", "up", "down", "del"):
            try:
                h_head = max(h_head,
                             int(row[key].minimumSizeHint().height()))
            except Exception:                                   # noqa: BLE001
                pass
        try:
            h_name = int(row["name"].minimumSizeHint().height())
            h_sum = int(row["summary"].minimumSizeHint().height())
            h_head = max(h_head, h_name + h_sum)
        except Exception:                                       # noqa: BLE001
            pass
        return h_head + 4

    def _cycle_apply_summary(self, w):
        """按数据类型刷组合摘要：组行显示「N 个模式 × M 遍」，单模式行清空
        并固定 0 高（**只改内容/高度，不显隐** ⇒ 行结构不变）。"""
        d = w["data"]
        is_group = (str(d.get(proto.AUTO_INST_MODE) or "")
                    == proto.AUTO_INST_GROUP)
        text = _inst_group_summary(d) if is_group else ""
        if w["summary"].text() != text:
            w["summary"].setText(text)
        h_sum = 0
        if is_group:
            try:
                w["summary"].ensurePolished()
                h_sum = int(w["summary"].fontMetrics().lineSpacing())
            except Exception:                                   # noqa: BLE001
                h_sum = 0
        if int(w.get("summary_h") or 0) != h_sum:
            w["summary_h"] = h_sum
            w["summary"].setFixedHeight(h_sum)

    def _cycle_render(self):
        """重排实例行 + 刷标题/序号/按钮可用性/添加区状态。

        [排序不碰 setParent] takeAt（从布局摘）+ addWidget（按新顺序重挂）
        ——父控件不变 ⇒ 不隐藏 ⇒ 不丢焦点（2026-11-19 板端键盘自灭实锅）。
        """
        for w in self._cycle_rows:
            if not w.get("head_h"):
                try:
                    w["head_h"] = int(w["settings"].sizeHint().height())
                except Exception:                               # noqa: BLE001
                    w["head_h"] = 0
        layout = self._cycle_list_layout
        while layout.count():
            layout.takeAt(0)
        can_cfg = getattr(self, "_cycle_can_cfg", True)
        editable = can_cfg and not self._cycle_readonly
        n = len(self._cycle_rows)
        for idx, w in enumerate(self._cycle_rows):
            w["row"].setMinimumHeight(self._cycle_row_min_h(w))
            layout.addWidget(w["row"])
            # [动态加的行要显式 show] 页面已显示后再 addWidget 的行，要等
            # 布局激活（事件循环一圈）才可见 ⇒ 趁下一拍没到，行是"白屏"。
            # 这里显式 show()（对已可见的行是幂等；不碰 setParent——那是
            # 会隐藏控件、丢焦点的禁手，见 A8f7）。
            w["row"].show()
            w["order"].setText("%d" % (idx + 1))
            # [组合摘要] 组行「N 个模式 × M 遍」；单模式行清空（高度 0）。
            self._cycle_apply_summary(w)
            # [边界禁用] 首行不能上移、末行不能下移（禁用态本地 QSS 可辨）；
            # 只读/无权限时全部收灰。可用性**只**在这里算（refresh 的权限闸
            # 只写 _cycle_can_cfg，避免把边界状态抹平——A8f3c 既有口径）。
            w["up"].setEnabled(editable and idx > 0)
            w["down"].setEnabled(editable and idx < n - 1)
            w["settings"].setEnabled(editable)
            w["del"].setEnabled(editable)
        self._cycle_retitle()
        # ---- 添加区 ----
        at_max = n >= proto.AUTO_CYCLE_INSTANCES_MAX
        self._cycle_add_btn.setEnabled(editable and not at_max)
        self._cycle_add_combo.setEnabled(editable and not at_max)
        # [总循环次数 2026-11-27] 同款闸：只读（老服务端）/无管理权限 ⇒ 灰。
        if getattr(self, "_cycle_top_repeat", None) is not None:
            self._cycle_top_repeat.setEnabled(editable)
        # [达上限给 tooltip] 置灰按钮放哪了/为什么灰，靠 tooltip 说明。
        self._cycle_add_btn.setToolTip(
            "最多 %d 个实例" % proto.AUTO_CYCLE_INSTANCES_MAX
            if at_max else
            "把下拉选中的项作为新实例追加到循环末尾"
            "（选「组合」会先弹组合设置卡，确定后才加入）")
        if self._cycle_readonly:
            self._cycle_ro_hint.setText(
                "当前服务端不支持循环实例（按旧配置只读显示）")
        elif at_max:
            self._cycle_ro_hint.setText(
                "最多 %d 个实例" % proto.AUTO_CYCLE_INSTANCES_MAX)
        else:
            self._cycle_ro_hint.setText("")

    def _auto_inherit_for(self, mode_id):
        """单模式块的当前 UI 值（新增实例/组内子模式时继承用；找不到 ⇒ {}）。

        [继承单模式 2026-11-26 用户「添加新模式的时候默认继承单模式的
        配置，这样方便点」] 设备/转速/方向/加速度/阀复位取对应**单模式
        块**的当前值（省得重配一遍）；切换条件与换向策略只在实例卡上有，
        保持缺省。组合没有对应单模式块 ⇒ 空（纯缺省）。
        """
        for blk in getattr(self, "_auto_mode_blocks", ()):
            if blk["id"] == str(mode_id):
                return self._auto_mode_block_values(blk)
        return {}

    def _cycle_default_title(self, mode_id, skip=None):
        """默认名（同层去重）：产水模式/产水模式2、组合/组合2…。"""
        return _inst_unique_title(
            _inst_base_name(mode_id),
            [_inst_display_name(w["data"]) for w in self._cycle_rows
             if w is not skip])

    def _cycle_renumber(self):
        """本地增删/移动/改名后按**同层命名口径**重排顶层实例标题。

        [为什么要重排 2026-11-27 独立复核] `_inst_assign_titles` 是**唯一
        命名规则**（回显 `_cycle_rebuild`、组卡、网页端与服务端
        `expand_cycle_plan` 同一套）：同层**同类出现 ≥2 次 ⇒ 全部带号**
        （产水模式1/产水模式2），只有 1 次 ⇒ 裸名；用户自定义名不动。
        本地「添加第 2 个产水模式」若只给新行命名，第 1 行仍是裸名 ⇒ 点
        「应用」后服务端回显会把它改成「产水模式1」（名字当场变一次），
        且服务端 autoStageTitle 与我们显示不一致 ⇒ 本地增删/移动/改名后
        同款重排（组内递归）。
        """
        _inst_assign_titles([w["data"] for w in self._cycle_rows])

    def _on_cycle_add(self):
        """「添加」：把下拉选中的项作为**新实例**追加到列表末尾。

        - 单模式：继承对应单模式块的当前配置 + 默认名（同层去重）；
        - 组合：先弹组合设置卡，**确定后**才追加（取消 = 什么都没发生）；
          空组不是合法循环项，先占一行再取消会留下删不掉的空组。
        """
        if self._cycle_readonly:
            return
        if len(self._cycle_rows) >= proto.AUTO_CYCLE_INSTANCES_MAX:
            return
        mid = str(self._cycle_add_combo.currentData() or "")
        if mid == proto.AUTO_INST_GROUP:
            self._add_cycle_group()
            return
        d = proto.auto_cycle_instance_default(mid)
        d.update(self._auto_inherit_for(mid))
        d[proto.AUTO_INST_TITLE] = self._cycle_default_title(mid)
        self._cycle_rows.append(self._build_cycle_row(d))
        # [同类≥2 全部带号 2026-11-27] 新行入列后整层重排一次（第 1 个
        # 产水模式也要变成「产水模式1」），与回显/服务端命名同口径。
        self._cycle_renumber()
        self._cycle_mark_dirty()
        self._cycle_render()

    def _add_cycle_group(self):
        """「添加 → 组合」：弹组合设置卡，确定后追加组行（depth=1 层）。"""
        d = proto.auto_cycle_instance_default(proto.AUTO_INST_GROUP)
        d[proto.AUTO_INST_TITLE] = self._cycle_default_title(
            proto.AUTO_INST_GROUP)
        dlg = CycleGroupDialog(
            d, stir_max=self._auto_stir_max.value(), parent=self,
            title=d[proto.AUTO_INST_TITLE], depth=1,
            inherit_fn=self._auto_inherit_for)
        if dlg.overlay_exec():
            self._cycle_rows.append(self._build_cycle_row(dlg.values()))
            self._cycle_renumber()
            self._cycle_mark_dirty()
            self._cycle_render()

    def _cycle_move(self, w, delta):
        """上移/下移一个实例（delta=-1 上移）；首/末行按钮本就置灰。"""
        if self._cycle_readonly:
            return
        try:
            i = self._cycle_rows.index(w)
        except ValueError:
            return
        j = i + delta
        if j < 0 or j >= len(self._cycle_rows):
            return
        self._cycle_rows[i], self._cycle_rows[j] = \
            self._cycle_rows[j], self._cycle_rows[i]
        # [序号随位置 2026-11-27] 同类实例换个位次 ⇒ 谁算 #1/#2 也变了，
        # 与回显/服务端「按顺序编号」同口径重排一次。
        self._cycle_renumber()
        self._cycle_mark_dirty()
        self._cycle_render()

    def _on_cycle_delete(self, w):
        """删除一个实例（行控件 deleteLater；标题整表重算）。"""
        if self._cycle_readonly:
            return
        try:
            i = self._cycle_rows.index(w)
        except ValueError:
            return
        row = self._cycle_rows.pop(i)
        self._cycle_list_layout.removeWidget(row["row"])
        row["row"].deleteLater()
        # [删到只剩 1 个同类 ⇒ 回裸名 2026-11-27] 与回显同口径（同类 1 个
        # 不带序号），否则点「应用」后名字又会变一次。
        self._cycle_renumber()
        self._cycle_mark_dirty()
        self._cycle_render()

    def _on_cycle_settings(self, w):
        """「设置」：弹该实例的设置卡；确定后把值写回该行的 data。

        [组合 2026-11-27] 组行弹组合设置卡（组内再嵌套子组 ⇒ 递归），
        单模式行弹实例设置卡。
        """
        if self._cycle_readonly:
            return
        mid = str(w["data"].get(proto.AUTO_INST_MODE) or "")
        if mid == proto.AUTO_INST_GROUP:
            dlg = CycleGroupDialog(
                w["data"], stir_max=self._auto_stir_max.value(), parent=self,
                title=w["name"].text(), depth=1,
                inherit_fn=self._auto_inherit_for)
        else:
            dlg = CycleInstanceDialog(
                w["data"], stir_max=self._auto_stir_max.value(), parent=self,
                title=w["name"].text())
        if dlg.overlay_exec():
            self._apply_cycle_dialog(w, dlg)

    def _apply_cycle_dialog(self, w, dlg):
        """把设置卡的值写回行 data、**同层重名去重**并标脏。

        [保存时去重 2026-11-27 用户口径] 名字留空 ⇒ 回落默认名；与同层
        其他实例重名 ⇒ 自动追加数字后缀（组合→组合2）。
        独立出来回归才能不跑模态 exec_ 直接驱动。
        """
        values = dict(dlg.values() or {})
        if str(values.get(proto.AUTO_INST_MODE) or "").strip():
            desired = (str(values.get(proto.AUTO_INST_TITLE) or "").strip()
                       or _inst_base_name(values.get(proto.AUTO_INST_MODE)))
            values[proto.AUTO_INST_TITLE] = _inst_unique_title(
                desired,
                [_inst_display_name(w2["data"]) for w2 in self._cycle_rows
                 if w2 is not w])
        w["data"] = values
        # [改名后整层重排 2026-11-27] 用户把某行改成默认形态名（如「产水
        # 模式」）⇒ 同类≥2 时按回显口径全部带号（产水模式1/产水模式2）；
        # 自定义名（与默认形态不同）原样保留。
        self._cycle_renumber()
        self._cycle_mark_dirty()
        # [名称/摘要可能变了] 行内文字跟着刷（不重建行 ⇒ 焦点/手势不丢）。
        self._cycle_render()

    def _cycle_rebuild(self, insts):
        """整表重建实例行（回显口径：服务端回显 = 事实 ⇒ 覆盖界面）。

        [默认名 2026-11-27] 服务端回显里没名字的实例在这里按同层顺序补默认
        名（产水模式/产水模式2/组合/组合2…）；已有名字原样保留。
        """
        for w in self._cycle_rows:
            self._cycle_list_layout.removeWidget(w["row"])
            w["row"].deleteLater()
        self._cycle_rows = []
        insts = list(insts or [])
        _inst_assign_titles(insts)
        for data in insts:
            self._cycle_rows.append(self._build_cycle_row(data))
        self._cycle_render()

    def _set_cycle_readonly(self, readonly):
        """只读态切换（老服务端未回显 instances ⇒ 只读 + 提示）。"""
        was = self._cycle_readonly
        self._cycle_readonly = bool(readonly)
        if self._cycle_readonly and not was:
            self.ctx.set_status(
                "当前服务端不支持循环实例（按旧配置只读显示）")
        self._cycle_render()

    def _cycle_instances_values(self):
        """实例列表载荷（全键拷贝；顺序 = 界面列表顺序）。

        [组合 2026-11-27] 组行把 children/repeat 带上（children 顺序 =
        组内执行顺序；**递归**含子组）；逐项再规整一次（防手工/异常数据
        把非法层或非法 repeat 发出去——服务端同样 fail-closed）。
        """
        out = []
        for w in self._cycle_rows:
            d = dict(w["data"])
            d[proto.AUTO_INST_DEVICES] = list(d.get(proto.AUTO_INST_DEVICES)
                                              or [])
            if str(d.get(proto.AUTO_INST_MODE) or "") == proto.AUTO_INST_GROUP:
                d = _coerce_cycle_instance(d, 1)
            out.append(d)
        return out

    def _on_cycle_top_changed(self, _value=0):
        """用户拨动「总循环次数」⇒ 记 dirty（回显期间由屏蔽标志挡住）。

        [为什么不直接发] 与 instances 同口径：老服务端对未知键**整包拒**
        ⇒ 只有用户真的动过（或服务端回显过该键）才把 topRepeat 放进载荷。
        """
        if getattr(self, "_cycle_top_populating", False):
            return
        self._cycle_top_dirty = True
        # 与实例列表共用「有未应用的修改」提示（同一次「应用自动配置」下发）。
        self._cycle_mark_dirty()

    def _auto_cycle_values(self):
        """自动循环配置（cycle 子节）的载荷：{enabled, instances, topRepeat}。

        [instances 什么时候才发] 服务端**回显过** instances（新服务端每次
        GET 都回）或用户动过列表（dirty）才发——老服务端对未知键是**整包
        拒**（不是丢弃）⇒ 无条件带 instances 会让同包的 modes/lights/
        stirMaxRpm 一起存不进去。只读态（老服务端派生显示）编辑已禁用 ⇒
        dirty 恒假 ⇒ instances 永不出门。
        [topRepeat 同款 2026-11-27] 服务端回显过该键或用户改过控件才发
        （`_cycle_top_dirty`）；老服务端连控件都是灰的 ⇒ 永不出门。
        """
        values = {
            proto.CFG_AUTO_CYCLE_ENABLED: bool(self._cycle_enabled.isChecked()),
        }
        if (self._cycle_dirty
                or proto.CFG_AUTO_CYCLE_INSTANCES
                in self._cycle_server_keys):
            values[proto.CFG_AUTO_CYCLE_INSTANCES] = \
                self._cycle_instances_values()
        if (getattr(self, "_cycle_top_dirty", False)
                or proto.CFG_AUTO_CYCLE_TOP_REPEAT
                in self._cycle_server_keys):
            values[proto.CFG_AUTO_CYCLE_TOP_REPEAT] = int(
                self._cycle_top_repeat.value())
        return values

    def _populate_cycle(self, cycle):
        """把 auto 节的 cycle 子节铺到实例列表（缺键 ⇒ 保持现状）。

        [两种形状]
          · 带 instances（新服务端）⇒ 按实例列表重建行（可编辑），记
            _cycle_server_keys（此后提交可发 instances）；
          · 缺 instances 但有旧键（老服务端）⇒ 用旧键派生视图合成**只读
            显示**（不可编辑、绝不发 instances——发了会被整包拒，连
            modes/lights 一起存不进去），并给只读提示。
        [局部应答只刷自己节] cycle 缺席（{} / None）⇒ 原样返回，不动列表
        也不动只读态。
        """
        if not isinstance(cycle, dict) or not cycle:
            return
        if proto.CFG_AUTO_CYCLE_ENABLED in cycle:
            self._cycle_enabled.setChecked(bool(
                cycle.get(proto.CFG_AUTO_CYCLE_ENABLED,
                          proto.AUTO_CYCLE_ENABLED_DEF)))
        # [总循环次数回显 2026-11-27] 应答带键才铺（缺键=老服务端 ⇒ 保持
        # 现值 0「一直循环」，不按缺省误改用户已配的轮数）；记进
        # _cycle_server_keys（此后提交可发 topRepeat）；回显**不算用户改动**
        # ⇒ 屏蔽 valueChanged，并在**确实回显过**之后才清 dirty（缺键的
        # 局部应答不动用户的未提交修改，与「缺键保持现状」同口径）。
        if proto.CFG_AUTO_CYCLE_TOP_REPEAT in cycle:
            self._cycle_server_keys.add(proto.CFG_AUTO_CYCLE_TOP_REPEAT)
            try:
                _top = int(cycle.get(proto.CFG_AUTO_CYCLE_TOP_REPEAT))
            except (TypeError, ValueError):
                _top = None
            if _top is not None:
                self._cycle_top_populating = True
                try:
                    self._cycle_top_repeat.setValue(
                        max(proto.AUTO_CYCLE_TOP_REPEAT_DEF,
                            min(proto.AUTO_CYCLE_TOP_REPEAT_MAX, _top)))
                finally:
                    self._cycle_top_populating = False
            self._cycle_top_dirty = False
        insts = cycle.get(proto.CFG_AUTO_CYCLE_INSTANCES)
        if isinstance(insts, (list, tuple)):
            # [新服务端] 先记 server_keys 再铺（铺途中不取值）。
            self._cycle_server_keys.add(proto.CFG_AUTO_CYCLE_INSTANCES)
            self._cycle_rebuild([_coerce_cycle_instance(x)
                                 for x in insts if isinstance(x, dict)])
            self._set_cycle_readonly(False)
            self._cycle_set_dirty(False)
            return
        # ---- 老服务端：旧键派生只读显示 ----
        self._cycle_rebuild(self._instances_from_legacy_cycle(cycle))
        self._set_cycle_readonly(True)
        self._cycle_set_dirty(False)

    def _instances_from_legacy_cycle(self, cycle):
        """旧九键 → 实例列表（派生兼容视图的客户端读法；只读显示用）。

        [派生口径与服务端 GET 同源] cycleModes（去重列表）逐模式一个实例
        （stageCfg 给到对应实例）；缺 cycleModes ⇒ 缺省三模式。六个扁平键
        → **首实例**（产水）对应字段；backwash/rinse 分钟 → 同模式首实例
        的 timerMinutes。派生值只用于**只读显示**（不进载荷）。
        """
        order = cycle.get(proto.CFG_AUTO_CYCLE_MODES)
        if isinstance(order, (list, tuple)) and order:
            mids = [str(m) for m in order]
        else:
            mids = list(proto.AUTO_CYCLE_MODES_DEF)
        stages = cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG)
        stages = stages if isinstance(stages, dict) else {}
        insts = []
        for mid in mids:
            d = proto.auto_cycle_instance_default(mid)
            sub = stages.get(mid)
            if isinstance(sub, dict):
                for key in (proto.CFG_AUTO_STAGE_PRESS_ENABLED,
                            proto.CFG_AUTO_STAGE_TIMER_ENABLED):
                    if key in sub:
                        d[key] = bool(sub.get(key))
                for key in (proto.CFG_AUTO_STAGE_PRESS_THRESHOLD,
                            proto.CFG_AUTO_STAGE_TIMER_MINUTES):
                    if key not in sub:
                        continue
                    try:
                        d[key] = (float(sub.get(key))
                                  if key == proto.CFG_AUTO_STAGE_PRESS_THRESHOLD
                                  else int(sub.get(key)))
                    except (TypeError, ValueError):
                        pass
                if proto.CFG_AUTO_STAGE_PRESS_COMPARE in sub:
                    d[proto.CFG_AUTO_STAGE_PRESS_COMPARE] = str(
                        sub.get(proto.CFG_AUTO_STAGE_PRESS_COMPARE))
            insts.append(d)
        # 六个扁平键 → 首实例（派生视图"有损"：同 mode 多实例只保留首个；
        # 旧键形状本就没有多实例概念）。
        if insts:
            first = insts[0]
            for src, dst, conv in (
                    (proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD,
                     proto.CFG_AUTO_STAGE_PRESS_THRESHOLD, float),
                    (proto.CFG_AUTO_CYCLE_TIMER_MINUTES,
                     proto.CFG_AUTO_STAGE_TIMER_MINUTES, int)):
                if src in cycle:
                    try:
                        first[dst] = conv(cycle.get(src))
                    except (TypeError, ValueError):
                        pass
            if proto.CFG_AUTO_CYCLE_PRESS_COMPARE in cycle:
                first[proto.CFG_AUTO_STAGE_PRESS_COMPARE] = str(
                    cycle.get(proto.CFG_AUTO_CYCLE_PRESS_COMPARE))
            if proto.CFG_AUTO_CYCLE_TIMER_FALLBACK in cycle:
                first[proto.CFG_AUTO_STAGE_TIMER_ENABLED] = bool(
                    cycle.get(proto.CFG_AUTO_CYCLE_TIMER_FALLBACK))
        for mid, mins_key in (
                ("backwash", proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES),
                ("rinse", proto.CFG_AUTO_CYCLE_RINSE_MINUTES)):
            if mins_key in cycle:
                for d in insts:
                    if d.get(proto.AUTO_INST_MODE) == mid:
                        try:
                            d[proto.CFG_AUTO_STAGE_TIMER_MINUTES] = int(
                                cycle.get(mins_key))
                        except (TypeError, ValueError):
                            pass
                        break
        return insts
    # ---- 采集配置 / 数据存储（CMD_CONFIG_GET / SET 的界面侧）----

    def _config_gate(self):
        """改配置的前置门：已连接 + root。失败给状态栏原因并返回 False。"""
        if not self.ctx.bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return False
        if not self.ctx.bridge.can(proto.PERM_ACCOUNT_ADMIN):
            self.ctx.set_status("权限不足：修改服务端配置需要管理员账户")
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

    def _rebuild_analog_channel_items(self):
        """按模块路数重建**四路通道下拉**的条目（保留当前选中值）。

        [路数从哪来] GET 应答的 analogChannels（服务端 cfg.analog.channels）；
        缺键（老服务端）回落 ANALOG_CHANNELS_DEF（8 ⇒ CH0..CH7）。

        [四路同一套] 压差计/温度/压力1/压力2 的通道都是同一种下拉
        （2026-11-23 用户口径「通道选择都用统一的下拉框」）⇒ 重建逻辑
        只此一处，条目文本/值（CH<n> ⇒ n）天然一致。

        [当前值越界] 模块路数变少（上次 CH6、这次只报 4 路）⇒ 夹到末项，
        与当年 QSpinBox setRange 的夹取同口径（不留空选中，否则
        currentData() 为 None，应用时会被迫回缺省通道）。
        """
        n = max(1, int(self._analog_channels))
        self._analog_ch_rebuilding = True
        try:
            for combo in (self._analog_press_ch, self._analog_temp_ch,
                          self._press1_ch, self._press2_ch):
                cur = combo.currentData()
                combo.clear()
                for ch in range(n):
                    combo.addItem("CH%d" % ch, ch)
                if cur is None:
                    cur = getattr(combo, "_default_ch", 0)
                idx = combo.findData(cur)
                if idx < 0:
                    idx = min(max(int(cur), 0), n - 1)
                combo.setCurrentIndex(idx)
        finally:
            self._analog_ch_rebuilding = False
        self._fit_analog_combo_widths()
        self._refresh_analog_channel_exclusivity()

    def _refresh_analog_channel_exclusivity(self):
        """通道下拉**互斥**：已被其它传感器占用的通道在下拉里置灰。

        [2026-11-23 用户「会不会选择到同一个通道…互斥做了吗」] 服务端只对
        **压力1/压力2 挪进占用通道**做显式拒绝（压差计/温度沿用既有"后配置
        者赢"语义，见 test_protect G3b/G8）；客户端此前完全不拦 ⇒ 界面上能
        先选成同一个 CH、点应用才被拒。这里把**四路互相之外**的通道置灰：
        · 每个下拉的**当前项永远可用**（历史配置若已撞号，仍如实显示）；
        · 其它下拉选中的通道 ⇒ 本下拉该项 disabled（鼠标/键盘都选不到）；
        · 任一下拉改选 ⇒ 四个下拉一起重算（腾出来的通道立刻可选）。
        """
        if getattr(self, "_analog_ch_rebuilding", False):
            return
        combos = (self._analog_press_ch, self._analog_temp_ch,
                  self._press1_ch, self._press2_ch)
        chosen = {}
        for combo in combos:
            chosen[id(combo)] = combo.currentData()
        for combo in combos:
            model = combo.model()
            cur_idx = combo.currentIndex()
            for i in range(combo.count()):
                ch = combo.itemData(i)
                taken = any(ch == v for cid, v in chosen.items()
                            if cid != id(combo))
                item = model.item(i)
                if item is not None:
                    # 当前项永远可点（否则老配置撞号时显示项被灰掉）
                    item.setEnabled((i == cur_idx) or not taken)

    def _on_analog_channel_changed(self, _index):
        self._refresh_analog_channel_exclusivity()

    def _add_analog_block(self, card, title, unit, combo, low, high, below4):
        """给一台传感器建一个可见块（块标题 + 会二档重排的控件行）。

        [块内四个控件] 通道 / 下限 / 上限 / 4mA 以下有效 ⇒ 宽窗一行、
        窄窗每控件独占一行（_AnalogDeviceRow）；块标题就是设备名，块内
        标签不再重复设备前缀。

        [高度交给布局] 块根是 QGroupBox + QVBoxLayout（default margins
        由样式给），行高/块高由 Qt 布局算——**不按行数手算高度**。
        """
        box = QGroupBox(title)
        lay = QVBoxLayout(box)
        row = _AnalogDeviceRow((
            label_group("通道", combo),
            label_group("下限（%s）" % unit, low),
            label_group("上限（%s）" % unit, high),
            below4))
        lay.addWidget(row)
        card.body.addWidget(box)
        self._analog_blocks.append({"title": title, "box": box, "row": row})
        return box

    @staticmethod
    def _new_press_spin(tooltip, low_side=True):
        """压力1/2 的量程数值框（与压差计/温度传感器同款：两位小数）。

        [为什么低/高用不同区间] 与既有压差量程框一致：下限允许负值
        （-10~100），上限只允许正（0.001~100）——量程是 4mA→下限、
        20mA→上限，上限 ≤ 0 没有物理意义（服务端也会交叉校验拒绝）。
        """
        sb = QDoubleSpinBox()
        if low_side:
            sb.setRange(proto.CFGK_ANALOG_PRESS_LOW_MPA_MIN,
                        proto.CFGK_ANALOG_PRESS_LOW_MPA_MAX)
        else:
            sb.setRange(proto.CFGK_ANALOG_PRESS_HIGH_MPA_MIN,
                        proto.CFGK_ANALOG_PRESS_HIGH_MPA_MAX)
        sb.setDecimals(2)
        sb.setSingleStep(0.1)
        sb.setToolTip(tooltip)
        return sb

    @staticmethod
    def _new_display_range_spin(lo, hi, decimals, step, default, tooltip):
        """显示渐变范围的数值框（浊度 NTU / 电箱温度 ℃）。"""
        sb = QDoubleSpinBox()
        sb.setRange(float(lo), float(hi))
        sb.setDecimals(int(decimals))
        sb.setSingleStep(float(step))
        sb.setValue(float(default))
        sb.setToolTip(tooltip)
        return sb

    def _fit_analog_spin_widths(self):
        """按**框内 QLineEdit 的真字体**给模拟量数值框定宽。

        [实锅三连 2026-11-20] ① 构造期测宽拿到的是未 polish 的字体
        （主题 1.5× 在 show 后才生效）⇒ 偏小裁尾；② widget.font() 在
        QSS 指定 font-size 时**不随 QSS 走**（QSS 字体挂在框内
        QLineEdit 上）⇒ 按 widget 字体测照样小（"100.0" 裁成 "10"）；
        ③ 不测靠 Qt sizeHint 样本（按极值数字估、估不到宽字形）也裁。
        ⇒ 唯一可信的度量对象是**框内 QLineEdit 自己的 fontMetrics**
        （它就是真正渲染数字的字体）；按钮区按两枚上下钮 ≈ 一个行高估，
        再加 QSS padding/border 余量。
        """
        from PyQt5.QtWidgets import QLineEdit
        try:
            for sb, txt in ((self._analog_press_low, "-9.99"),
                            (self._analog_press_high, "99.99"),
                            (self._analog_temp_low, "-99.9"),
                            (self._analog_temp_high, "999.9"),
                            # [压力1/2 2026-11-23] 与压差量程框同区间 ⇒
                            # 同一套定宽文本（"-9.99" / "99.99"）。
                            (self._press1_low, "-9.99"),
                            (self._press1_high, "99.99"),
                            (self._press2_low, "-9.99"),
                            (self._press2_high, "99.99"),
                            (self._protect_motor_limit, "199.9 ℃")):
                le = sb.findChild(QLineEdit)
                fm = (le or sb).fontMetrics()
                # [padding 40px×2 是主题 QSS 的按钮区] theme.py 的
                # QSpinBox 规则 "padding: 4px 40px" —— 两侧各 40px 顶开
                # 触屏 −/+ 按钮区（布局不是装饰）⇒ 定宽必须含这 80px +
                # 边框/光标边距/QSS 字号与 fm 实测的差（fz13 在 1.5× 下
                # 20px，实测还差 ~10px ⇒ 取 120 富余，宁可宽不裁字）。
                w = fm.horizontalAdvance(txt) + 120
                # [2026-11-22 主题矩阵抓出 205<219] +120 在 polish 后可能
                # 仍小于 Qt 自己的 minimumSizeHint（QSS padding 40×2 + 按钮
                # 区按真字体算）⇒ 取两者较大值，否则「定宽」本身就成了裁字
                # 的凶手（5 套主题实测「0.00/1.00/0.0」205<219）。
                w = max(w, sb.minimumSizeHint().width())
                sb.setMinimumWidth(w)
                sb.setMaximumWidth(w)
        except (AttributeError, RuntimeError):       # noqa: BLE001
            pass                                     # 页面还没建全/已析构

    def _fit_analog_combo_widths(self):
        """四路通道下拉：宽度按**最长条目实测**兜底（不写死像素）。

        [度量对象] QComboBox::sizeHint 会遍历所有条目取最宽，再加上箭头/
        内边距（后两者由当前 QSS 与字体算），字号缩放后自动跟着变 ⇒
        直接取 Qt 的实测值兜最小宽；多余宽度仍由布局分配（不设上限）。
        """
        for combo in (self._analog_press_ch, self._analog_temp_ch,
                      self._press1_ch, self._press2_ch):
            try:
                w = max(int(combo.sizeHint().width()),
                        int(combo.minimumSizeHint().width()))
                combo.setMinimumWidth(w)
            except RuntimeError:                 # 页面已析构
                pass

    @staticmethod
    def _select_channel(combo, value, clamp=False):
        """把服务端回的通道号铺到下拉上。

        :param clamp: 值不在条目里时是否夹到边界。压差/温度沿用旧下拉的
            「找不到就不动」；压力1/2 沿用旧 QSpinBox 的「夹到区间内」。
        """
        try:
            value = int(value)
        except (TypeError, ValueError):
            return
        idx = combo.findData(value)
        if idx >= 0:
            combo.setCurrentIndex(idx)
        elif clamp and combo.count() > 0:
            combo.setCurrentIndex(min(max(value, 0), combo.count() - 1))

    @staticmethod
    def _channel_value(combo, default):
        """下拉的当前通道号（值仍是 int；无选中才回缺省）。

        [不能用 `currentData() or default`] CH0 的 currentData() 是 **0**
        （假值）⇒ `or` 会把「选 CH0」变成「缺省 CH1/CH2」（压差/温度那两
        路当年就有这个病）⇒ 必须显式只判 None。
        """
        data = combo.currentData()
        return int(default if data is None else data)

    def showEvent(self, event):
        """显示时按 **polish 后的真字体** 复核 ℃ 数值框宽度。

        [为什么 populate 里测还会小 2026-11-20 实锅三连] --shot/进页时序是
        建页 → populate（GET 回投早于首帧）→ show ⇒ populate 那一刻拿到的
        还是未 polish 的字体（实测测出的宽装不下 "100.0"）。showEvent 在
        ensurePolished() **之后**才发 ⇒ 这里 font() 已是最终字体 ⇒ 测一次
        就准。populate 里的调用保留（应用配置回投也要复核，字号缩放后无
        show 也可能发生）。
        """
        super(RemoteSettingsPage, self).showEvent(event)
        self._fit_analog_spin_widths()
        self._fit_analog_combo_widths()

    def _populate(self, doc):
        """把服务端配置快照铺到控件上（两节各自缺省跳过）。

        [用应答回显而非本地值] 应用成功的应答带服务端**实际生效值**
        （周期可能被服务端夹取过），铺回控件保证「界面显示的就是生效的」。
        """
        self._fit_analog_spin_widths()
        self._fit_analog_combo_widths()
        collect = doc.get("collect") or {}
        if collect:
            # [逐台键缺省回落共同键] 旧服务端只报 turbidityEnabled：逐台键
            # 缺失时不能一律当 True（会把停用的浊度显示成启用）。
            _t_all = bool(collect.get(proto.CFGK_TURBIDITY_ENABLED, True))
            for key_enabled, chk, key_poll, spin in self._collect_rows:
                default = _t_all if key_enabled in (
                    proto.CFGK_TURBIDITY1_ENABLED,
                    proto.CFGK_TURBIDITY2_ENABLED) else True
                chk.setChecked(bool(collect.get(key_enabled, default)))
                spin.setValue(int(collect.get(key_poll, 200) or 200))
            # [滤速 2026-11-25] 面积：应答带键才铺（老服务端缺键保持现值）。
            if proto.CFGK_FILTER_AREA_M2 in collect:
                self._collect_filter_area.setValue(float(
                    collect.get(proto.CFGK_FILTER_AREA_M2) or 0.0))
            # [模拟量通道与量程 2026-11-20] 缺键（老服务端）⇒ 保持缺省。
            # [模块路数先到先重建] 通道下拉条目随 GET 的路数刷新，再回显。
            if "analogChannels" in collect:
                self._analog_channels = max(1, int(
                    collect.get("analogChannels",
                                proto.ANALOG_CHANNELS_DEF) or 8))
                self._rebuild_analog_channel_items()
            if proto.CFGK_ANALOG_PRESS_CHANNEL in collect:
                self._select_channel(
                    self._analog_press_ch,
                    collect.get(proto.CFGK_ANALOG_PRESS_CHANNEL, 0))
            if proto.CFGK_ANALOG_PRESS_LOW_MPA in collect:
                self._analog_press_low.setValue(float(
                    collect.get(proto.CFGK_ANALOG_PRESS_LOW_MPA, 0.0)))
            if proto.CFGK_ANALOG_PRESS_HIGH_MPA in collect:
                self._analog_press_high.setValue(float(
                    collect.get(proto.CFGK_ANALOG_PRESS_HIGH_MPA, 1.0)))
            if proto.CFGK_ANALOG_TEMP_CHANNEL in collect:
                self._select_channel(
                    self._analog_temp_ch,
                    collect.get(proto.CFGK_ANALOG_TEMP_CHANNEL, 1))
            if proto.CFGK_ANALOG_TEMP_LOW_C in collect:
                self._analog_temp_low.setValue(float(
                    collect.get(proto.CFGK_ANALOG_TEMP_LOW_C, 0.0)))
            if proto.CFGK_ANALOG_TEMP_HIGH_C in collect:
                self._analog_temp_high.setValue(float(
                    collect.get(proto.CFGK_ANALOG_TEMP_HIGH_C, 100.0)))
            if proto.CFGK_ANALOG_PRESS_BELOW4_VALID in collect:
                self._analog_press_below4.setChecked(bool(
                    collect.get(proto.CFGK_ANALOG_PRESS_BELOW4_VALID, True)))
            if proto.CFGK_ANALOG_TEMP_BELOW4_VALID in collect:
                self._analog_temp_below4.setChecked(bool(
                    collect.get(proto.CFGK_ANALOG_TEMP_BELOW4_VALID, False)))
            # [压力1/2 2026-11-23] 同样**逐键判在**：应答里没有的键保持
            # 当前值（老服务端/局部应答不误改）。
            if CFGK_PRESS1_CHANNEL in collect:
                self._select_channel(
                    self._press1_ch,
                    collect.get(CFGK_PRESS1_CHANNEL, PRESS1_CHANNEL_DEF),
                    clamp=True)
            if CFGK_PRESS1_LOW_MPA in collect:
                self._press1_low.setValue(float(
                    collect.get(CFGK_PRESS1_LOW_MPA, PRESS_LOW_MPA_DEF)))
            if CFGK_PRESS1_HIGH_MPA in collect:
                self._press1_high.setValue(float(
                    collect.get(CFGK_PRESS1_HIGH_MPA, PRESS_HIGH_MPA_DEF)))
            if CFGK_PRESS1_BELOW4_VALID in collect:
                self._press1_below4.setChecked(bool(
                    collect.get(CFGK_PRESS1_BELOW4_VALID,
                                PRESS_BELOW4_VALID_DEF)))
            if CFGK_PRESS2_CHANNEL in collect:
                self._select_channel(
                    self._press2_ch,
                    collect.get(CFGK_PRESS2_CHANNEL, PRESS2_CHANNEL_DEF),
                    clamp=True)
            if CFGK_PRESS2_LOW_MPA in collect:
                self._press2_low.setValue(float(
                    collect.get(CFGK_PRESS2_LOW_MPA, PRESS_LOW_MPA_DEF)))
            if CFGK_PRESS2_HIGH_MPA in collect:
                self._press2_high.setValue(float(
                    collect.get(CFGK_PRESS2_HIGH_MPA, PRESS_HIGH_MPA_DEF)))
            if CFGK_PRESS2_BELOW4_VALID in collect:
                self._press2_below4.setChecked(bool(
                    collect.get(CFGK_PRESS2_BELOW4_VALID,
                                PRESS_BELOW4_VALID_DEF)))
            # [显示渐变范围 2026-11-24] 同样逐键判在（老服务端没有这些键
            # ⇒ 保持当前值，不把用户已配的范围冲回缺省）。
            for _key, _spin in (
                    (proto.CFGK_TURBIDITY1_RANGE_MIN,
                     self._turb1_range_min),
                    (proto.CFGK_TURBIDITY1_RANGE_MAX,
                     self._turb1_range_max),
                    (proto.CFGK_TURBIDITY2_RANGE_MIN,
                     self._turb2_range_min),
                    (proto.CFGK_TURBIDITY2_RANGE_MAX,
                     self._turb2_range_max),
                    (proto.CFGK_TEMPHUM_TEMP_LOW_C,
                     self._th_temp_range_min),
                    (proto.CFGK_TEMPHUM_TEMP_HIGH_C,
                     self._th_temp_range_max),
                    (proto.CFGK_VFD_SPEED_RANGE_MIN,
                     self._vfd_speed_range_min),
                    (proto.CFGK_VFD_SPEED_RANGE_MAX,
                     self._vfd_speed_range_max),
                    (proto.CFGK_VFD_MOTOR_TEMP_RANGE_MIN,
                     self._vfd_mtemp_range_min),
                    (proto.CFGK_VFD_MOTOR_TEMP_RANGE_MAX,
                     self._vfd_mtemp_range_max)):
                if _key in collect:
                    _spin.setValue(float(collect.get(_key, 0.0) or 0.0))
                    self._display_range_seen = True
            # [渐变逐项开关 2026-11-24] 逐键判在；缺键（老服务端）保持勾选。
            for _key, _chk in (
                    (proto.CFGK_TURBIDITY1_GRADIENT_ENABLED,
                     self._turb1_grad_chk),
                    (proto.CFGK_TURBIDITY2_GRADIENT_ENABLED,
                     self._turb2_grad_chk),
                    (proto.CFGK_TEMPHUM_TEMP_GRADIENT_ENABLED,
                     self._th_temp_grad_chk),
                    (proto.CFGK_VFD_SPEED_GRADIENT_ENABLED,
                     self._vfd_speed_grad_chk),
                    (proto.CFGK_VFD_MOTOR_TEMP_GRADIENT_ENABLED,
                     self._vfd_mtemp_grad_chk),
                    (proto.CFGK_ANALOG_PRESS_GRADIENT_ENABLED,
                     self._press_grad_chk),
                    (proto.CFGK_ANALOG_PRESS1_GRADIENT_ENABLED,
                     self._press1_grad_chk),
                    (proto.CFGK_ANALOG_PRESS2_GRADIENT_ENABLED,
                     self._press2_grad_chk)):
                if _key in collect:
                    _chk.setChecked(bool(collect.get(_key)))
                    self._display_range_seen = True
            if collect:
                self._refresh_analog_channel_exclusivity()
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
            # [入库触发方式 2026-11-12] 缺键（老服务端）⇒ 缺省"持续采集"，
            # 与服务端缺省一致（不静默改成另一种语义）。
            trig = str(storage.get(proto.CFGK_STORAGE_HISTORY_TRIGGER,
                                   proto.HISTORY_TRIGGER_ALWAYS)
                       or proto.HISTORY_TRIGGER_ALWAYS)
            idx = self._storage_trigger.findData(trig)
            self._storage_trigger.setCurrentIndex(idx if idx >= 0 else 0)
            self._on_storage_policy()
        # [谁给的字段谁负责 —— 2026-11-10 实锅] 这两行原来**无条件**按应答刷新：
        # 应答里没有 storageDbPath / storageStats 时就被当成"落盘关闭"清成
        # 「用量：--」「未启用（落盘关闭）」。而「应用登录策略」的应答只带
        # login 节（采集节同理），于是点一下登录策略，数据存储卡就被清空，
        # 要切页再切回来（重新 GET 全量配置）才恢复 —— 用户实测反馈。
        # 现在只在应答**确实带了存储侧键**时才刷新这两行。
        if "storageDbPath" in doc or "storageStats" in doc:
            db_path = str(doc.get("storageDbPath", "") or "")
            self._db_path_label.setText(
                "当前库文件：%s" % (db_path or "未启用（落盘关闭）"))
            self._populate_usage(doc.get("storageStats") or {})
        login = doc.get("login") or {}
        if login:
            self._login_anon.setChecked(bool(
                login.get(proto.CFGK_LOGIN_ALLOW_ANON, False)))
            self._login_concur.setChecked(bool(
                login.get(proto.CFGK_LOGIN_ALLOW_CONCUR, False)))
            self._login_max.setValue(int(
                login.get(proto.CFGK_LOGIN_MAX_SESSIONS, 8) or 0))
        # [auto 节只在应答确实带它时刷新] 与 storage/login 同一纪律：局部
        # 应答（应用采集/存储/登录策略）不带 auto，不得把模式块冲回缺省。
        auto = doc.get(proto.CFG_SECTION_AUTO) or {}
        if auto:
            self._populate_auto(auto)
        # [protect 节同理 2026-11-15] 局部应答不带 protect ⇒ 不动控件
        # （"应答里没有的字段不要当成已关闭"的同一纪律）。
        protect = doc.get(proto.CFG_SECTION_PROTECT) or {}
        if protect:
            self._protect_seen = True
            # [0 是合法值 2026-11-15 审计] `protect.get(k, DEF) or DEF` 这种
            # 写法会把**合法的 0**（保护关闭 / 上限 0）变成缺省 ⇒ 界面显示
            # "800 kPa"而服务端其实是 0 ⇒ 用户点「应用」就把上限改成了
            # 0.8 MPa（静默改配置）。⇒ 一律用"键在不在"判，0 原样带过来。
            _t = protect.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS)
            self._protect_timeout.setValue(
                0 if _t is None else (int(_t) // 1000))
            self._protect_stop.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_INLET_NOFLOW_STOP,
                            proto.PROTECT_INLET_NOFLOW_STOP_DEF)))
            # [压差过大保护 2026-11-15] 缺键（老服务端）⇒ 保持缺省不启用。
            self._protect_press_enabled.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_PRESS_HIGH_ENABLED,
                            proto.PROTECT_PRESS_HIGH_ENABLED_DEF)))
            _p = protect.get(proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA)
            self._protect_press_limit.setValue(
                (proto.PROTECT_PRESS_HIGH_LIMIT_MPA_DEF if _p is None
                 else float(_p)) * 1000.0)
            self._protect_press_stop.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_PRESS_HIGH_STOP,
                            proto.PROTECT_PRESS_HIGH_STOP_DEF)))
            # [电机超温保护 2026-11-20] 缺键（老服务端）⇒ 保持缺省不启用。
            self._protect_motor_enabled.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED,
                            proto.PROTECT_MOTOR_OVER_TEMP_ENABLED_DEF)))
            _mt = protect.get(proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C)
            self._protect_motor_limit.setValue(
                proto.PROTECT_MOTOR_OVER_TEMP_LIMIT_C_DEF if _mt is None
                else float(_mt))
            self._protect_motor_stop.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP,
                            proto.PROTECT_MOTOR_OVER_TEMP_STOP_DEF)))
            # [电箱超温保护 2026-11-25] 缺键（老服务端）⇒ 保持缺省不启用。
            self._protect_box_enabled.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_BOX_OVER_TEMP_ENABLED,
                            proto.PROTECT_BOX_OVER_TEMP_ENABLED_DEF)))
            _bt = protect.get(proto.CFGK_PROTECT_BOX_OVER_TEMP_LIMIT_C)
            self._protect_box_limit.setValue(
                proto.PROTECT_BOX_OVER_TEMP_LIMIT_C_DEF if _bt is None
                else float(_bt))
            self._protect_box_stop.setChecked(bool(
                protect.get(proto.CFGK_PROTECT_BOX_OVER_TEMP_STOP,
                            proto.PROTECT_BOX_OVER_TEMP_STOP_DEF)))
            # [压力1/2 过高保护 2026-11-25] 缺键（老服务端）⇒ 缺省不启用。
            for _idx, _en, _lim, _st in (
                    (1, self._protect_press1_enabled,
                     self._protect_press1_limit, self._protect_press1_stop),
                    (2, self._protect_press2_enabled,
                     self._protect_press2_limit, self._protect_press2_stop)):
                _k = "CFGK_PROTECT_PRESS%d_HIGH" % _idx
                _en.setChecked(bool(protect.get(
                    getattr(proto, _k + "_ENABLED"), False)))
                _lm = protect.get(getattr(proto, _k + "_LIMIT_MPA"))
                _lim.setValue(
                    (proto.PROTECT_PRESS_CHANNEL_HIGH_LIMIT_MPA_DEF
                     if _lm is None else float(_lm)) * 1000.0)
                _st.setChecked(bool(protect.get(
                    getattr(proto, _k + "_STOP"),
                    proto.PROTECT_PRESS_CHANNEL_HIGH_STOP_DEF)))
        # [camera 节同理 2026-11-19] 局部应答不带 camera ⇒ 不动控件。
        camera = doc.get(proto.CFG_SECTION_CAMERA) or {}
        if camera:
            self._camera_seen = True
            self._cam_enabled.setChecked(bool(
                camera.get(proto.CFGK_CAMERA_ENABLED,
                           proto.CAMERA_ENABLED_DEF)))
            # [地址框两用 2026-11-20] ip/mac 各进各的缓存，框里装当前
            # 连接方式的那一个（切下拉即互换，互不覆盖）。
            if proto.CFGK_CAMERA_IP in camera:
                self._cam_addr_ip = str(camera.get(proto.CFGK_CAMERA_IP)
                                        or "")
            if proto.CFGK_CAMERA_MAC in camera:
                self._cam_addr_mac = str(
                    proto.normalize_mac(camera.get(proto.CFGK_CAMERA_MAC))
                    or "")
            self._sync_cam_addr()
            self._cam_port.setValue(int(
                camera.get(proto.CFGK_CAMERA_PORT, proto.CAMERA_PORT_DEF)))
            _user = camera.get(proto.CFGK_CAMERA_USERNAME, "")
            if _user:
                self._cam_user.setText(str(_user))
            # [口令不回显] 服务端只回 hasPassword ⇒ 口令框保持"留空=不改"
            # （用户真要改口令就填，填了就下发）。
            _si = self._cam_stream.findData(
                camera.get(proto.CFGK_CAMERA_STREAM, proto.CAMERA_STREAM_DEF))
            if _si >= 0:
                self._cam_stream.setCurrentIndex(_si)
            _fi = self._cam_fps.findData(int(
                camera.get(proto.CFGK_CAMERA_FPS, proto.CAMERA_FPS_DEF)))
            if _fi >= 0:
                self._cam_fps.setCurrentIndex(_fi)
            self._cam_width.setValue(int(
                camera.get(proto.CFGK_CAMERA_WIDTH, proto.CAMERA_WIDTH_DEF)))
            self._cam_quality.setValue(int(
                camera.get(proto.CFGK_CAMERA_QUALITY, proto.CAMERA_QUALITY_DEF)))
            # [2026-11-19 直传三键回显] 缺键（老服务端）保持缺省不误改。
            _ti = self._cam_transport.findData(camera.get(
                proto.CFGK_CAMERA_TRANSPORT, proto.CAMERA_TRANSPORT_DEF))
            if _ti >= 0:
                self._cam_transport.setCurrentIndex(_ti)
            _ci = self._cam_codec.findData(camera.get(
                proto.CFGK_CAMERA_CODEC, proto.CAMERA_CODEC_DEF))
            if _ci >= 0:
                self._cam_codec.setCurrentIndex(_ci)
            _hi = self._cam_hw.findData(camera.get(
                proto.CFGK_CAMERA_HW_DECODE, proto.CAMERA_HW_DEF))
            if _hi >= 0:
                self._cam_hw.setCurrentIndex(_hi)
            # [2026-11-20 MAC 连接 + 断线重连回显] 缺键（老服务端）保持
            # 当前值不动（不静默回缺省）；MAC 顺手归一成小写冒号。
            if proto.CFGK_CAMERA_RESOLVE_MODE in camera:
                _ri = self._cam_resolve.findData(camera.get(
                    proto.CFGK_CAMERA_RESOLVE_MODE,
                    proto.CAMERA_RESOLVE_DEF))
                if _ri >= 0:
                    self._cam_resolve.setCurrentIndex(_ri)
            if proto.CFGK_CAMERA_MAC in camera:
                self._cam_addr_mac = str(
                    proto.normalize_mac(camera.get(proto.CFGK_CAMERA_MAC))
                    or "")
                self._sync_cam_addr()
            if proto.CFGK_CAMERA_RECONNECT_ENABLED in camera:
                self._cam_reconnect.setChecked(bool(camera.get(
                    proto.CFGK_CAMERA_RECONNECT_ENABLED,
                    proto.CAMERA_RECONNECT_ENABLED_DEF)))
            if proto.CFGK_CAMERA_RECONNECT_INTERVAL_S in camera:
                self._cam_reconnect_s.setValue(int(camera.get(
                    proto.CFGK_CAMERA_RECONNECT_INTERVAL_S,
                    proto.CAMERA_RECONNECT_INTERVAL_S_DEF)))
            # [解码方式联动] hwDecode 是**本端**解码行为 ⇒ 回包一到就生效
            # （下一个 GOP 起按新模式解码，不用等下次应用）。
            self.ctx.bridge.set_camera_hw(camera.get(
                proto.CFGK_CAMERA_HW_DECODE, proto.CAMERA_HW_DEF))
            # [在线状态回显] 让操作员一眼看出"配了但相机没出画面"。
            # [ES 模式口径 2026-11-19] stream 模式的 frames 是 **NAL 计数**
            # （服务端 camera.status 直传分支），文案说「帧」就是撒谎 ⇒
            # 按 actualTransport 区分（stream=「已收 N NAL」/jpeg=「已出
            # N 帧」）。
            _online = bool(camera.get("online"))
            _err = str(camera.get("error") or "")
            _codec = str(camera.get("actualCodec")
                         or camera.get(proto.CFGK_CAMERA_CODEC)
                         or "").upper().replace("H2", "H.2")
            _transport = str(camera.get("actualTransport") or "")
            _unit = ("NAL" if _transport == "stream" else "帧")
            # [MAC 模式 2026-11-20] 追加「MAC → 解析到的 IP」——动态 IP
            # 相机换地址后，现场一眼看出"现在实际连的是哪台"。
            _resolved = str(camera.get("resolvedIp") or "")
            _mac_txt = ""
            if self._cam_resolve.currentData() == proto.CAMERA_RESOLVE_MAC:
                _mac = self._cam_addr_mac
                _mac_txt = "· MAC %s → %s" % (
                    _mac or "未填", _resolved or "未解析")
            self._cam_state.setText(
                "相机：%s（已收 %s %s%s）%s%s"
                % ("画面接收中" if _online else "未出画面",
                   camera.get("frames", 0),
                   _unit,
                   ("· " + _codec) if _codec else "",
                   ("· " + _err) if _err else "",
                   _mac_txt))

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
        # [滤速 2026-11-25] 过滤面积（m²；0=未设置）。
        values[proto.CFGK_FILTER_AREA_M2] = float(
            self._collect_filter_area.value())
        # [模拟量通道与量程 2026-11-20] 与采集配置同一次下发（collect 节）。
        # [通道值来自下拉] 四路都是 QComboBox；CH0 是合法值（0 是假值，
        # 不能写 `currentData() or 缺省`）⇒ 走 _channel_value 只判 None。
        # [下限在前 2026-11-20] 服务端交叉校验按「另一侧当前值」判 ⇒ 载荷
        # 恒先发下限再发上限（缩小量程的中间态不误报）。
        values[proto.CFGK_ANALOG_PRESS_CHANNEL] = self._channel_value(
            self._analog_press_ch, 0)
        values[proto.CFGK_ANALOG_PRESS_LOW_MPA] = \
            float(self._analog_press_low.value())
        values[proto.CFGK_ANALOG_PRESS_HIGH_MPA] = \
            float(self._analog_press_high.value())
        values[proto.CFGK_ANALOG_TEMP_CHANNEL] = self._channel_value(
            self._analog_temp_ch, 1)
        values[proto.CFGK_ANALOG_TEMP_LOW_C] = \
            float(self._analog_temp_low.value())
        values[proto.CFGK_ANALOG_TEMP_HIGH_C] = \
            float(self._analog_temp_high.value())
        values[proto.CFGK_ANALOG_PRESS_BELOW4_VALID] = \
            bool(self._analog_press_below4.isChecked())
        values[proto.CFGK_ANALOG_TEMP_BELOW4_VALID] = \
            bool(self._analog_temp_below4.isChecked())
        # [压力传感器 1/2 2026-11-23] 同一次 collect 下发；键名三端逐字
        # 一致（press1Channel/...，camelCase）。下限仍在上限之前（服务端
        # 交叉校验按「另一侧当前值」判的既有口径）。
        values[CFGK_PRESS1_CHANNEL] = self._channel_value(
            self._press1_ch, PRESS1_CHANNEL_DEF)
        values[CFGK_PRESS1_LOW_MPA] = float(self._press1_low.value())
        values[CFGK_PRESS1_HIGH_MPA] = float(self._press1_high.value())
        values[CFGK_PRESS1_BELOW4_VALID] = \
            bool(self._press1_below4.isChecked())
        values[CFGK_PRESS2_CHANNEL] = self._channel_value(
            self._press2_ch, PRESS2_CHANNEL_DEF)
        values[CFGK_PRESS2_LOW_MPA] = float(self._press2_low.value())
        values[CFGK_PRESS2_HIGH_MPA] = float(self._press2_high.value())
        values[CFGK_PRESS2_BELOW4_VALID] = \
            bool(self._press2_below4.isChecked())
        # [逐台开关 2026-11-10] 两台浊度各发各的 turbidity{1,2}Enabled；共同键
        # turbidityEnabled 仍发（任一台启用）——旧服务端只认它，新服务端按
        # 逐台键覆盖。老的兼容周期键 turbidityPollMs 仍带（= 进水浊度仪 的）。
        values[proto.CFGK_TURBIDITY_ENABLED] = bool(
            values.get(proto.CFGK_TURBIDITY1_ENABLED)
            or values.get(proto.CFGK_TURBIDITY2_ENABLED))
        values[proto.CFGK_TURBIDITY_POLL_MS] = values.get(
            proto.CFGK_TURBIDITY1_POLL_MS, proto.CFG_POLL_MS_MIN)
        # [显示渐变范围 2026-11-24] 与采集配置同一次 collect 下发。成对发
        # （下限在前），服务端整包写完再校验下限<上限（支持整体平移）。
        # [老服务端兼容] GET 里从没出现过这些键 ⇒ 不发（旧服务端会把未知
        # 键当整包错误，连采集开关一起存不上）。
        if self._display_range_seen:
            values[proto.CFGK_TURBIDITY1_RANGE_MIN] = \
                float(self._turb1_range_min.value())
            values[proto.CFGK_TURBIDITY1_RANGE_MAX] = \
                float(self._turb1_range_max.value())
            values[proto.CFGK_TURBIDITY2_RANGE_MIN] = \
                float(self._turb2_range_min.value())
            values[proto.CFGK_TURBIDITY2_RANGE_MAX] = \
                float(self._turb2_range_max.value())
            values[proto.CFGK_TEMPHUM_TEMP_LOW_C] = \
                float(self._th_temp_range_min.value())
            values[proto.CFGK_TEMPHUM_TEMP_HIGH_C] = \
                float(self._th_temp_range_max.value())
            values[proto.CFGK_VFD_SPEED_RANGE_MIN] = \
                float(self._vfd_speed_range_min.value())
            values[proto.CFGK_VFD_SPEED_RANGE_MAX] = \
                float(self._vfd_speed_range_max.value())
            values[proto.CFGK_VFD_MOTOR_TEMP_RANGE_MIN] = \
                float(self._vfd_mtemp_range_min.value())
            values[proto.CFGK_VFD_MOTOR_TEMP_RANGE_MAX] = \
                float(self._vfd_mtemp_range_max.value())
            values[proto.CFGK_TURBIDITY1_GRADIENT_ENABLED] = \
                bool(self._turb1_grad_chk.isChecked())
            values[proto.CFGK_TURBIDITY2_GRADIENT_ENABLED] = \
                bool(self._turb2_grad_chk.isChecked())
            values[proto.CFGK_TEMPHUM_TEMP_GRADIENT_ENABLED] = \
                bool(self._th_temp_grad_chk.isChecked())
            values[proto.CFGK_VFD_SPEED_GRADIENT_ENABLED] = \
                bool(self._vfd_speed_grad_chk.isChecked())
            values[proto.CFGK_VFD_MOTOR_TEMP_GRADIENT_ENABLED] = \
                bool(self._vfd_mtemp_grad_chk.isChecked())
            values[proto.CFGK_ANALOG_PRESS_GRADIENT_ENABLED] = \
                bool(self._press_grad_chk.isChecked())
            values[proto.CFGK_ANALOG_PRESS1_GRADIENT_ENABLED] = \
                bool(self._press1_grad_chk.isChecked())
            values[proto.CFGK_ANALOG_PRESS2_GRADIENT_ENABLED] = \
                bool(self._press2_grad_chk.isChecked())
        self.ctx.set_status("正在应用采集配置…")

        def _done(doc):
            # [网络线程] 只发信号/走状态栏信号，不直接碰控件。
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("采集配置已生效（开关即时，周期下一拍）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set("collect", values, on_done=_done)

    def _apply_protect(self):
        """下发运行保护配置（protect 节；需管理员）。"""
        if not self._config_gate():
            return
        # [2026-11-15 审计] 还没收到过 protect 快照 ⇒ 不许下发：控件上
        # 还是缺省值（时限 0 / 上限 800 kPa）⇒ 下发等于把服务端的保护
        # 配置静默改成缺省（保护被关掉）。按钮已禁用，这里再兜一道。
        if not self._protect_seen:
            self.ctx.set_status("尚未收到服务端的运行保护配置，无法应用"
                                "（请先刷新配置）")
            return
        # [界面用秒、协议用毫秒] 界面口径是"泵开着多久"（分钟级），秒好读。
        values = {
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS:
                int(self._protect_timeout.value()) * 1000,
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP:
                bool(self._protect_stop.isChecked()),
            # [压差过大保护 2026-11-15]
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED:
                bool(self._protect_press_enabled.isChecked()),
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA:
                float(self._protect_press_limit.value()) / 1000.0,
            proto.CFGK_PROTECT_PRESS_HIGH_STOP:
                bool(self._protect_press_stop.isChecked()),
            # [电机超温保护 2026-11-20]
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED:
                bool(self._protect_motor_enabled.isChecked()),
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C:
                float(self._protect_motor_limit.value()),
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP:
                bool(self._protect_motor_stop.isChecked()),
            # [电箱超温保护 2026-11-25]
            proto.CFGK_PROTECT_BOX_OVER_TEMP_ENABLED:
                bool(self._protect_box_enabled.isChecked()),
            proto.CFGK_PROTECT_BOX_OVER_TEMP_LIMIT_C:
                float(self._protect_box_limit.value()),
            proto.CFGK_PROTECT_BOX_OVER_TEMP_STOP:
                bool(self._protect_box_stop.isChecked()),
            # [压力1/2 过高保护 2026-11-25] 界面 kPa、协议 MPa（÷1000）。
            proto.CFGK_PROTECT_PRESS1_HIGH_ENABLED:
                bool(self._protect_press1_enabled.isChecked()),
            proto.CFGK_PROTECT_PRESS1_HIGH_LIMIT_MPA:
                float(self._protect_press1_limit.value()) / 1000.0,
            proto.CFGK_PROTECT_PRESS1_HIGH_STOP:
                bool(self._protect_press1_stop.isChecked()),
            proto.CFGK_PROTECT_PRESS2_HIGH_ENABLED:
                bool(self._protect_press2_enabled.isChecked()),
            proto.CFGK_PROTECT_PRESS2_HIGH_LIMIT_MPA:
                float(self._protect_press2_limit.value()) / 1000.0,
            proto.CFGK_PROTECT_PRESS2_HIGH_STOP:
                bool(self._protect_press2_stop.isChecked()),
        }
        self.ctx.set_status("正在应用运行保护配置…")

        def _done(doc):
            # [网络线程] 只发信号/走状态栏信号，不直接碰控件。
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("运行保护配置已生效")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set(proto.CFG_SECTION_PROTECT, values,
                                    on_done=_done)

    def _cam_mac_value_for_apply(self):
        """按连接方式取要下发的 MAC（形态不符用缓存兜底）。

        [实锅 2026-11-20] 网页端曾因「先装框后设连接方式」把 IP 当 MAC
        发出去（服务端拒「MAC 格式不对」）。桌面端顺序是对的，但同样加
        兜底：方式=mac 时若框里含 '.'（是 IP）或归一失败 ⇒ 用缓存 MAC。
        [两条路径格式必须一致 2026-11-20] 原来「非 mac 模式 / 兜底」直接
        返回**缓存原文**（用户可能写的是 `AA-BB-CC-DD-EE-FF`），而正常
        路径返回**归一值**（`aa:bb:cc:dd:ee:ff`）⇒ 同一个 MAC 在不同
        路径上发出去的**格式不一样**（服务端虽然会自己归一，但客户端回显
        会跟着跳变，用户看着像"我改了点什么"）⇒ 现在两条路径统一归一，
        归一失败才回落原文（半截输入不丢）。
        """
        txt = str(self._cam_addr.text() or "").strip()
        if self._cam_resolve.currentData() != proto.CAMERA_RESOLVE_MAC:
            return str(proto.normalize_mac(self._cam_addr_mac)
                       or self._cam_addr_mac)
        if "." in txt:
            return str(proto.normalize_mac(self._cam_addr_mac)
                       or self._cam_addr_mac)
        return str(proto.normalize_mac(txt) or "")

    def _on_cam_resolve_changed(self, _idx=None):
        """连接方式切换：地址框换装（IP/MAC 两个值互保留，切来切去不丢）。"""
        if self._cam_resolve.currentData() == proto.CAMERA_RESOLVE_MAC:
            self._cam_addr_ip = str(self._cam_addr.text()).strip()
            self._cam_addr.setPlaceholderText(
                "相机 MAC，如 AA:BB:CC:DD:EE:FF")
            self._cam_addr.setText(self._cam_addr_mac)
        else:
            self._cam_addr_mac = str(self._cam_addr.text()).strip()
            self._cam_addr.setPlaceholderText("相机 IP，如 192.168.1.181")
            self._cam_addr.setText(self._cam_addr_ip)
        # [占位符换了 ⇒ 底宽重算 2026-11-20] MAC 的占位符比 IP 长 ⇒
        # 框宽按当前占位符实测（见 page_base.fit_input_width）。
        fit_input_width(self._cam_addr)

    def _sync_cam_addr(self):
        """回显：把**当前连接方式**对应的缓存值装进地址框。"""
        if self._cam_resolve.currentData() == proto.CAMERA_RESOLVE_MAC:
            self._cam_addr.setPlaceholderText(
                "相机 MAC，如 AA:BB:CC:DD:EE:FF")
            self._cam_addr.setText(self._cam_addr_mac)
        else:
            self._cam_addr.setPlaceholderText("相机 IP，如 192.168.1.181")
            self._cam_addr.setText(self._cam_addr_ip)
        fit_input_width(self._cam_addr)

    def _apply_camera(self):
        """下发相机配置（camera 节；需管理员）。

        [口令留空 = 不改] 服务端不回显口令 ⇒ 框空着就**别把空串当成新口令**
        （服务端那边也把空串解释为"不改"，两端同款 ⇒ 存一次不会把相机锁在
        门外）。
        """
        if not self._config_gate():
            return
        if not self._camera_seen:
            self.ctx.set_status("尚未收到服务端的相机配置，无法应用"
                                "（请先刷新配置）")
            return
        values = {
            proto.CFGK_CAMERA_ENABLED: bool(self._cam_enabled.isChecked()),
            # [地址框两用] 按当前连接方式取框内容；另一侧发缓存（不变）。
            proto.CFGK_CAMERA_IP:
                (str(self._cam_addr.text()).strip()
                 if self._cam_resolve.currentData()
                 == proto.CAMERA_RESOLVE_IP else self._cam_addr_ip),
            proto.CFGK_CAMERA_PORT: int(self._cam_port.value()),
            proto.CFGK_CAMERA_USERNAME: str(self._cam_user.text()).strip(),
            proto.CFGK_CAMERA_PASSWORD: str(self._cam_pass.text()),
            proto.CFGK_CAMERA_STREAM: self._cam_stream.currentData(),
            proto.CFGK_CAMERA_FPS: int(self._cam_fps.currentData()),
            proto.CFGK_CAMERA_WIDTH: int(self._cam_width.value()),
            proto.CFGK_CAMERA_QUALITY: int(self._cam_quality.value()),
            # [2026-11-19 直传三键] 传输/编码/解码方式（相机可编辑项全进
            # 远端设置 —— 用户口径）。
            proto.CFGK_CAMERA_TRANSPORT: self._cam_transport.currentData(),
            proto.CFGK_CAMERA_CODEC: self._cam_codec.currentData(),
            proto.CFGK_CAMERA_HW_DECODE: self._cam_hw.currentData(),
            # [2026-11-20 MAC 连接 + 断线重连]
            proto.CFGK_CAMERA_RESOLVE_MODE: self._cam_resolve.currentData(),
            # [防呆 2026-11-20 实锅] 框里内容形态与连接方式不符时用缓存：
            # 「按 MAC」但框里是 IP（含 '.'）⇒ 归一失败 ⇒ 退回缓存 MAC。
            proto.CFGK_CAMERA_MAC: self._cam_mac_value_for_apply(),
            proto.CFGK_CAMERA_RECONNECT_ENABLED:
                bool(self._cam_reconnect.isChecked()),
            proto.CFGK_CAMERA_RECONNECT_INTERVAL_S:
                int(self._cam_reconnect_s.value()),
        }
        self.ctx.set_status("正在应用相机配置…")

        def _done(doc):
            # [网络线程] 只发信号/走状态栏信号，不直接碰控件。
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("相机配置已生效（服务端已重启采集）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set(proto.CFG_SECTION_CAMERA, values,
                                    on_done=_done)

    # ==================================================================
    #        变频器参数（0x011B；2026-11-18 新增）
    # ==================================================================
    #
    # [这一节做什么] 远程读/写变频器（搅拌泵驱动器）的功能码参数
    # （P1.00~P1.05 电机铭牌组）。此前铭牌类参数现场只能拿面板按。
    #
    # [三条硬纪律]
    #   ① **显示值只认服务端回读**（应答 params[].value）——绝不显示本地
    #      提交值（写完用应答回读刷新显示）。缺项显示 "--"，不编造。
    #   ② **服务端 err 原样显示**（RunLocked「变频器运行中禁止写参数
    #      （请先停止）」是服务端的事实，客户端不改写文案）。
    #   ③ **在途闸成对开合**：闸只置位不复位 ⇒ 第一次「应用」成功后按钮
    #      永久失灵（网页端踩过：按钮只活一次）⇒ 终态（成功/失败/超时）
    #      必复位。

    def _build_vfdp_row(self, d):
        """建一行参数：参数号+中文名 / 写入控件 / 回读值标签。

        [枚举型用下拉框] 带 options 的参数（电机类型 P1.00）渲染
        QComboBox（itemText = 中文名，itemData = **原始码**）；其余用
        QDoubleSpinBox（量程按 vmin/vmax、小数位按 decimals）。

        [顺序要紧：先小数位再量程] QDoubleSpinBox.setRange 会按**当前**
        小数位把上下限取整 ⇒ 先 setDecimals 再 setRange，否则 0.01 A 被
        取整成 0.00（量程下限凭空变 0）。

        [尺寸交给布局] 行内不写死宽高；行尾 addStretch 让控件贴标签排。
        """
        box = QWidget()
        lay = QHBoxLayout(box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)
        name = QLabel("%s %s" % (d["code"], d["label"]))
        name.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        lay.addWidget(name)
        row = {"code": d["code"], "addr": int(d["addr"]), "def": d,
               "spin": None, "combo": None, "value_lbl": None,
               "has_value": False, "widget": box}
        options = d.get("options") or ()
        if options:
            combo = QComboBox()
            for value, text in options:
                combo.addItem(str(text), int(value))
            combo.setToolTip(
                "直接选择类型（提交时按原始码 %s 下发）；变频器运行中写会被拒绝"
                % "/".join(str(v) for v, _t in options))
            lay.addWidget(combo)
            row["combo"] = combo
        else:
            spin = QDoubleSpinBox()
            decimals = int(d.get("decimals", 0))
            # [顺序要紧] 先定小数位再定量程（见方法注释）。
            spin.setDecimals(decimals)
            spin.setRange(float(d.get("vmin", 0.0)),
                          float(d.get("vmax", 65535.0)))
            spin.setSingleStep(1.0 / (10 ** decimals))
            unit = str(d.get("unit") or "")
            if unit:
                spin.setSuffix(" " + unit)
            spin.setToolTip(
                "%s（%s）：量程 %g~%g%s；提交时按原始码下发（×%g）"
                % (d["code"], d["label"], float(d.get("vmin", 0.0)),
                   float(d.get("vmax", 0.0)), (" " + unit) if unit else "",
                   float(d.get("scale", 1))))
            lay.addWidget(spin)
            row["spin"] = spin
        value_lbl = QLabel("--")
        value_lbl.setStyleSheet(theme.caption_label_qss())
        row["value_lbl"] = value_lbl
        lay.addWidget(label_group("回读", value_lbl))
        lay.addStretch(1)
        return row

    def _vfdp_run_text(self, doc):
        """运行态文案（**0x3000 原始字**：1 正转 / 2 反转 / 3 停机）。

        [别与 XVfdRunState 枚举混淆] 那是「就绪/运行/故障」那套枚举，语义
        完全不同（把 1 当成枚举会显示反：运行中显示"停机"）。
        """
        rs = doc.get(proto.KEY_VFDP_RUN_STATE)
        try:
            rs = int(rs)
        except (TypeError, ValueError):
            rs = 0
        text = proto.VFD_RUN_WORD_TEXT.get(rs, "状态码 %d" % rs)
        if doc.get(proto.KEY_VFDP_FAULTED):
            text += "（故障）"
        return text

    def _populate_vfd_params(self, doc, keep_status=False):
        """0x011B 应答落显示（**只认回读值**，绝不显示本地提交值）。

        :param doc: 应答文档（{"ok","err"?,"params":[…],"vfdFaulted",
            "runState"}）。
        :param keep_status: True = 不改状态行（写路径自己写「已写入并回读
            确认：N 项」；读路径写「已读取 N 项 · …」）。

        [缺项不编造] 服务端没回某一项 ⇒ 该行显示 "--"（不拿本地提交值或
        缺省值顶上），状态行把缺项列出来（缺：P1.01…）。
        [失败不清屏] 应答里没有 params（拒写/失败包络常见）⇒ **不动**已有
        显示值（清成 "--" 等于把上一帧的真值抹了）。
        """
        doc = doc or {}
        params = doc.get(proto.KEY_VFDP_PARAMS) or []
        if not isinstance(params, list):
            params = []
        run_text = self._vfdp_run_text(doc)
        self._vfdp_state.setText("变频器：" + run_text)
        if params:
            by_addr = {}
            for p in params:
                if not isinstance(p, dict):
                    continue
                try:
                    addr = int(p.get(proto.VFDP_KEY_ADDR))
                except (TypeError, ValueError):
                    continue
                by_addr[addr] = p
            missing = []
            for row in self._vfdp_rows:
                p = by_addr.get(row["addr"])
                if p is None:
                    row["value_lbl"].setText("--")
                    # [没回读到 = 这一行没有真值] 写载荷时**跳过**它（写
                    # 下去的就是控件缺省值 ⇒ 把驱动器的真值改成缺省）。
                    row["has_value"] = False
                    missing.append(row["code"])
                    continue
                self._vfdp_apply_item(row, p)
            self._vfdp_fetched = True
            self._vfdp_values_ok = True
            if not keep_status:
                msg = "已读取 %d 项 · 变频器%s" % (len(params), run_text)
                if missing:
                    msg += "；缺：" + "、".join(missing)
                self._vfdp_status.setText(msg)
            return
        if not keep_status:
            if doc.get("ok") is False:
                self._vfdp_status.setText(
                    "读取失败：%s" % str(doc.get("err") or "未知"))
            else:
                self._vfdp_status.setText("服务端未返回参数（读取失败）")

    def _vfdp_apply_item(self, row, p):
        """把一条回读参数铺到该行（显示值 + 写入控件跟随回读值）。

        [枚举型显示中文名] 服务端给了 valueText 就用它；没给（老服务端）
        按**本地选项表**按原始码查；都不行 ⇒ 显示原始码（**不编**名字）。

        [has_value] 标记"这一行有真值"——写载荷只带 has_value=True 的行
        （没回读到的行绝不能写：写下去的是控件缺省值）。
        """
        d = row["def"]
        decimals = int(d.get("decimals", 0))
        raw = p.get(proto.VFDP_KEY_RAW)
        value = p.get(proto.VFDP_KEY_VALUE)
        combo = row.get("combo")
        if combo is not None:
            text = str(p.get(proto.VFDP_KEY_VALUE_TEXT) or "")
            if not text and raw is not None:
                for opt in (d.get("options") or ()):
                    if int(opt[0]) == int(raw):
                        text = str(opt[1])
                        break
            if not text:
                text = "--" if raw is None else str(int(raw))
            row["value_lbl"].setText(text)
            # [下拉当前项 = 回读值] 不是第一项 ⇒ 用户看到的起点就是真值。
            if raw is not None:
                idx = combo.findData(int(raw))
                if idx >= 0:
                    combo.setCurrentIndex(idx)
            row["has_value"] = raw is not None
            return
        # 数值型：按小数位格式化（单位在参数名/输入框上，不重复占位）。
        row["has_value"] = value is not None
        if value is None:
            row["value_lbl"].setText("--")
        else:
            row["value_lbl"].setText("%.*f" % (decimals, float(value)))
        spin = row.get("spin")
        if spin is not None and value is not None:
            # 越界由 spin 自己夹取（不回弹提示；服务端量程与本地表一致）。
            spin.setValue(float(value))

    def _vfdp_values(self):
        """写入值 → {addr(int): raw(int)}（只发可写项）。

        [换算口径] 数值型 raw = round(工程值 × scale)；枚举型**原样**取
        下拉的 itemData（原始码，不做工程值换算）。

        [只发"有真值"的行] 服务端没回读到的行（has_value=False）**跳过**
        ——写下去的就是控件缺省值 ⇒ 把驱动器的真值改成缺省（与网页端
        同口径：网页端没有回读就没有输入框 ⇒ 压根不发）。跳过的项记在
        _vfdp_skipped，写完在状态行里说清楚（不许静默少写）。
        """
        values = {}
        skipped = []
        for row in self._vfdp_rows:
            d = row["def"]
            if not d.get("writable"):
                continue
            if not row.get("has_value"):
                skipped.append(row["code"])
                continue
            combo = row.get("combo")
            if combo is not None:
                raw = combo.currentData()
                if raw is None:
                    skipped.append(row["code"])
                    continue
                values[int(d["addr"])] = int(raw)
                continue
            spin = row.get("spin")
            if spin is None:
                continue
            values[int(d["addr"])] = int(
                round(spin.value() * float(d.get("scale", 1))))
        self._vfdp_skipped = skipped
        return values

    def _fetch_vfd_params(self):
        """读变频器参数（读 = 登录即可）。"""
        if not self.ctx.bridge.is_connected():
            self._vfdp_status.setText("未连接服务端")
            return
        self._vfdp_fetched = True
        self._vfdp_status.setText("正在读取变频器参数…")

        def _done(doc):
            # [网络线程] 只 emit 信号（跨线程排队回 UI 线程再碰控件）。
            self.vfdParamsReady.emit(dict(doc or {}))

        def _error(err):
            self.vfdParamsFailed.emit(str(err))

        self.ctx.bridge.vfd_params(op=proto.VFD_PARAMS_OP_READ,
                                   on_done=_done, on_error=_error)

    def _apply_vfd_params(self):
        """把写入值下发到变频器（写 = 管理员）。

        [在途闸成对开合 2026-11-18] 网页端 page14 的闸只置位不复位 ⇒
        「应用到变频器」按钮**只活一次**。⇒ 这里置位，终态（成功/失败/
        超时）里复位；重复点击在途时被拒（只发一批写）。
        """
        if self._vfdp_inflight:
            self.ctx.set_status("变频器参数写入仍在进行，请稍候")
            return
        if not self.ctx.bridge.is_connected():
            self.ctx.set_status("未连接服务端")
            return
        if not self.ctx.bridge.can(proto.PERM_ACCOUNT_ADMIN):
            self.ctx.set_status("权限不足：修改变频器参数需要管理员账户")
            return
        if not self._vfdp_values_ok:
            # [与运行保护同款纪律] 没读到过真值就写 ⇒ 写下去的是控件缺省
            # 值（把变频器参数静默改成缺省）。
            self.ctx.set_status("尚未读取变频器参数，无法应用"
                                "（请先点「刷新（读真实值）」）")
            return
        values = self._vfdp_values()
        if not values:
            self.ctx.set_status("没有可写的参数（服务端未回可写项）")
            return
        self._vfdp_inflight = True
        self.ctx.set_status("正在应用变频器参数…")
        try:
            self.ctx.bridge.vfd_params(
                op=proto.VFD_PARAMS_OP_WRITE, values=values,
                on_done=self._vfdp_write_done, on_error=self._vfdp_write_error)
        except Exception as exc:                       # noqa: BLE001
            # [闸成对开合] 同步抛异常 ⇒ 闸复位（否则这次点击把闸焊死）。
            self._vfdp_inflight = False
            self._vfdp_status.setText("变频器参数请求失败：%s" % exc)
            self.ctx.set_status("变频器参数请求失败：%s" % exc)

    # ---- 0x011B 回调 → UI 线程（信号落点）----

    def _vfdp_write_done(self, doc):
        """写应答（网络线程）⇒ 只 emit 信号。"""
        self.vfdParamsWritten.emit(dict(doc or {}))

    def _vfdp_write_error(self, err):
        """写网络层失败（网络线程）⇒ 只 emit 信号。"""
        self.vfdParamsFailed.emit(str(err))

    def _on_vfdp_ready(self, doc):
        """读应答落 UI 线程：铺回读值 + 状态行。"""
        self._populate_vfd_params(doc)
        if doc.get("ok"):
            self.ctx.set_status("变频器参数已回读")

    def _on_vfdp_written(self, doc):
        """写应答落 UI 线程：回读值刷新显示 + 确认计数 + 闸复位。"""
        # [终态第一件事] 闸复位（成对开合）——放在最前面，后面的分支
        # （含抛异常的路径）都不会把闸焊死。
        self._vfdp_inflight = False
        ok = bool(doc.get("ok"))
        params = doc.get(proto.KEY_VFDP_PARAMS) or []
        if not isinstance(params, list):
            params = []
        if not ok:
            # [服务端原话] 拒写（RunLocked/离线/权限）的 err 是服务端的事
            # 实 ⇒ 原样显示，不改写文案。
            err = str(doc.get("err") or "未知错误")
            self._vfdp_status.setText(err)
            self.ctx.set_status("变频器参数写入失败：%s" % err)
            # 拒写应答若带了 params ⇒ 拿它刷新一次回读（不清屏）。
            if params:
                self._populate_vfd_params(doc, keep_status=True)
            return
        # [回读不撒谎] 用应答里的回读值刷新显示（绝不显示本地提交值）。
        self._populate_vfd_params(doc, keep_status=True)
        failed = []
        for p in params:
            if isinstance(p, dict) and p.get(proto.VFDP_KEY_APPLIED) is False:
                failed.append(str(p.get(proto.VFDP_KEY_CODE) or "?"))
        # [确认计数] applied 缺键 = 老服务端/读操作 ⇒ 按确认计；只有显式
        # applied=false 才算失败（逐项列出来，别让"确认 N 项"变成假成功）。
        n = len(params) - len(failed)
        msg = "已写入并回读确认：%d 项" % n
        if failed:
            msg += "；失败：" + "、".join(failed)
        if self._vfdp_skipped:
            # [不许静默少写] 没回读到的行没进载荷 ⇒ 说清楚是哪几项。
            msg += "；未回读未提交：" + "、".join(self._vfdp_skipped)
        self._vfdp_status.setText(msg)
        self.ctx.set_status(msg)

    def _on_vfdp_failed(self, err):
        """网络层失败（超时/断链）⇒ 终态 + 闸复位。"""
        self._vfdp_inflight = False
        self._vfdp_status.setText("变频器参数请求失败：%s" % err)
        self.ctx.set_status("变频器参数请求失败：%s" % err)

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
            # [入库触发方式 2026-11-12] 持续采集 / 仅自动运行时采集。
            proto.CFGK_STORAGE_HISTORY_TRIGGER:
                self._storage_trigger.currentData()
                or proto.HISTORY_TRIGGER_ALWAYS,
        }
        self.ctx.set_status("正在应用数据存储配置…")

        def _done(doc):
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("数据存储配置已生效（含容量上限与满额策略）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set("storage", values, on_done=_done)

    def _apply_login(self):
        """提交登录策略（login 节；需 root）。"""
        if not self._config_gate():
            return
        values = {
            proto.CFGK_LOGIN_ALLOW_ANON: self._login_anon.isChecked(),
            proto.CFGK_LOGIN_ALLOW_CONCUR: self._login_concur.isChecked(),
            proto.CFGK_LOGIN_MAX_SESSIONS: self._login_max.value(),
        }
        self.ctx.set_status("正在应用登录策略…")

        def _done(doc):
            if doc.get("ok"):
                self.configReady.emit(doc)
                self.ctx.set_status("登录策略已生效（下次登录起）")
            else:
                self.ctx.set_status("应用失败：%s" % doc.get("err", "未知"))

        self.ctx.bridge.config_set(proto.CFG_SECTION_LOGIN, values,
                                   on_done=_done)

    def refresh(self):
        model = self.ctx.model
        if model.run_mode_known:
            if model.is_auto():
                self._mode_label.setText("自动（远程控制已禁用）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 24px; font-weight: bold; color: %s;"
                    " background: transparent; }" % theme.CLR_WARN)
            else:
                self._mode_label.setText("手动（允许远程操作）")
                self._mode_label.setStyleSheet(
                    "QLabel { font-size: 24px; font-weight: bold; color: %s;"
                    " background: transparent; }" % theme.CLR_OK)
        else:
            self._mode_label.setText("未知")
        can_mode = self.ctx.bridge.can(proto.PERM_MODE_SWITCH)
        self.btn_manual.setEnabled(can_mode and model.is_auto())
        self.btn_auto.setEnabled(can_mode and not model.is_auto())

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
                "QLabel { background: transparent; font-size: 20px; color: palette(mid); }")
        elif running is None:
            self._auto_label.setText("自动模式 · 运行状态未知")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px; color: palette(mid); }")
        elif running:
            self._auto_label.setText("自动模式 · 运行中")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px;"
                " font-weight: bold; color: %s; }" % theme.CLR_WARN)
        else:
            self._auto_label.setText("自动模式 · 已停止")
            self._auto_label.setStyleSheet(
                "QLabel { background: transparent; font-size: 20px; color: palette(mid); }")
        can_ctrl = (self.ctx.bridge.can(proto.PERM_AUTO_CONTROL)
                    and model.is_auto())
        self.btn_auto_start.setEnabled(can_ctrl and running is not True)
        self.btn_auto_stop.setEnabled(can_ctrl and running is True)
        bridge = self.ctx.bridge
        if bridge.is_connected():
            self._conn_label.setText(
                "已连接 · 用户 %s（%s）"
                % (bridge.user, permissions.role_name(bridge.role)))
        else:
            self._conn_label.setText("未连接")

        # ---- 配置卡：无管理权限整体只读（防止改了应用不上还以为生效）----
        can_cfg = self.ctx.bridge.can(proto.PERM_ACCOUNT_ADMIN)
        for _key_e, chk, _key_p, spin in self._collect_rows:
            chk.setEnabled(can_cfg)
            spin.setEnabled(can_cfg)
        # [滤速面积 2026-11-25] 同卡同闸。
        self._collect_filter_area.setEnabled(can_cfg)
        # [模拟量通道与量程 2026-11-20] 同卡同闸（无管理权限只读）。
        self._analog_press_ch.setEnabled(can_cfg)
        self._analog_press_low.setEnabled(can_cfg)
        self._analog_press_high.setEnabled(can_cfg)
        self._analog_press_below4.setEnabled(can_cfg)
        self._analog_temp_ch.setEnabled(can_cfg)
        self._analog_temp_low.setEnabled(can_cfg)
        self._analog_temp_below4.setEnabled(can_cfg)
        self._analog_temp_high.setEnabled(can_cfg)
        # [压力传感器 1/2 2026-11-23] 同卡同闸（无管理权限只读）。
        for _w in (self._press1_ch, self._press1_low, self._press1_high,
                   self._press1_below4, self._press2_ch, self._press2_low,
                   self._press2_high, self._press2_below4):
            _w.setEnabled(can_cfg)
        # [一台设备一块 2026-11-23] 块根同闸（组内控件随父禁用，块标题
        # 也一起变灰——「无权限整块置灰」）。
        for _blk in self._analog_blocks:
            _blk["box"].setEnabled(can_cfg)
        # [显示渐变范围 2026-11-24] 同卡同闸（无管理权限只读）。
        for _w in (self._turb1_range_min, self._turb1_range_max,
                   self._turb2_range_min, self._turb2_range_max,
                   self._th_temp_range_min, self._th_temp_range_max,
                   self._vfd_speed_range_min, self._vfd_speed_range_max,
                   self._vfd_mtemp_range_min, self._vfd_mtemp_range_max,
                   self._turb1_grad_chk, self._turb2_grad_chk,
                   self._th_temp_grad_chk, self._vfd_speed_grad_chk,
                   self._vfd_mtemp_grad_chk, self._press_grad_chk,
                   self._press1_grad_chk, self._press2_grad_chk):
            _w.setEnabled(can_cfg)
        self._collect_apply.setEnabled(can_cfg)
        self._storage_enable.setEnabled(can_cfg)
        self._storage_path.setEnabled(can_cfg)
        self._storage_sample.setEnabled(can_cfg)
        self._storage_max.setEnabled(can_cfg)
        self._storage_policy.setEnabled(can_cfg)
        self._storage_keep.setEnabled(can_cfg and
                                      (self._storage_policy.currentData()
                                       == "rotate"))
        self._storage_trigger.setEnabled(can_cfg)
        self._storage_save.setEnabled(can_cfg)
        self._login_anon.setEnabled(can_cfg)
        self._login_concur.setEnabled(can_cfg)
        self._login_max.setEnabled(can_cfg)
        self._login_save.setEnabled(can_cfg)
        # ---- 运行保护卡（2026-11-15）：同款只读闸 + 「应用」等首帧 ----
        # 无管理权限改了也应用不上（服务端会拒）⇒ 控件整体只读。
        # 「应用」按钮还要等**第一次 protect 快照到达**：没收到就按控件
        # 缺省值下发 ⇒ 把服务端的保护配置静默改成缺省（保护被关掉）。
        self._protect_stop.setEnabled(can_cfg)
        self._protect_timeout.setEnabled(can_cfg)
        self._protect_press_enabled.setEnabled(can_cfg)
        self._protect_press_limit.setEnabled(can_cfg)
        self._protect_press_stop.setEnabled(can_cfg)
        self._protect_motor_enabled.setEnabled(can_cfg)
        self._protect_motor_limit.setEnabled(can_cfg)
        self._protect_motor_stop.setEnabled(can_cfg)
        self._protect_apply.setEnabled(can_cfg and self._protect_seen)
        # ---- 变频器参数卡（0x011B）：读=登录即可，写=管理员 ----
        # 「应用」还要等**第一次回读到达**：没读到真值就写 ⇒ 写下去的是
        # 控件缺省值（把变频器参数静默改成缺省）。
        for _vrow in self._vfdp_rows:
            if _vrow.get("spin") is not None:
                _vrow["spin"].setEnabled(can_cfg)
            if _vrow.get("combo") is not None:
                _vrow["combo"].setEnabled(can_cfg)
        self._vfdp_apply.setEnabled(can_cfg and self._vfdp_values_ok)
        self._vfdp_refresh.setEnabled(True)
        # ---- 自动模式配置卡：同款只读闸（无管理权限改了也应用不上）----
        for blk in self._auto_mode_blocks:
            blk["devices"].setEnabled(can_cfg)
            blk["rpm"].setEnabled(can_cfg)
            blk["reverse"].setEnabled(can_cfg)
            blk["rst_stop"].setEnabled(can_cfg)
            blk["rst_estop"].setEnabled(can_cfg)
        # [按钮灯闪烁 2026-11-24] 同卡同闸。
        for _lctl in getattr(self, "_auto_lights", {}).values():
            _lctl["on"].setEnabled(can_cfg)
            _lctl["off"].setEnabled(can_cfg)
        # ---- 自动循环配置卡：同款只读闸（应用按钮在最后那张卡底部）----
        # [2026-11-26 实例化] 行内只有「设置/上移/下移/删除」按钮 ⇒ 闸只写
        # `_cycle_can_cfg`，行按钮/添加区的可用性**只**由 `_cycle_render`
        # 按位置 + 权限 + 只读态统一算（避免把「首行不能上移、末行不能
        # 下移」的边界状态抹平——A8f3c 既有口径）。
        self._cycle_enabled.setEnabled(can_cfg)
        self._cycle_can_cfg = can_cfg
        self._cycle_render()
        self._auto_apply.setEnabled(can_cfg)

        # ---- 配置回显兜底 ----
        # 切到本页时可能还没连上（on_enter 拉空过）；连上后的第一拍补拉。
        if not self.ctx.bridge.is_connected():
            self._config_fetched = False
            # [断线重连后重读] 变频器参数同理：重连后要重新回读一次真实值
            # （断线期间驱动器可能被现场改过）。
            self._vfdp_fetched = False
        elif not self._config_fetched:
            self._fetch_config(silent=True)
        # ---- 变频器参数兜底（同款：连上后的第一拍读一次真实值）----
        # [防 200ms 轰炸] refresh 每 200ms 跑一次 ⇒ 只在"还没取过"时拉。
        if (self.ctx.bridge.is_connected()
                and not self._vfdp_fetched):
            self._fetch_vfd_params()


class NetworkPage(Page):
    """网络设置：服务端连接参数的只读展示 + 本机 WiFi 设置。

    [WiFi 区 2026-11-13] 「WiFi 网络设置」按用户定案**只在桌面 Python 客户端**
    实现（网页端不做本机硬件操作）。走 C 动态库 `hsb_bridge`（框架层
    XNetworkWiFi：适配器枚举 / 扫描 / 连接），Python 侧封装见
    `hsf_native.py`。

    [终态纪律] 每个动作都必须落到**成功 / 失败 / 超时**三选一的终态，
    不许停在「扫描中…」「连接中…」——驱动侧本身有超时兜底（扫描 4s /
    连接 20s），界面再加一层更宽松的兜底（15s / 30s）防驱动不回话。
    """

    #: WiFi 状态轮询周期（毫秒）——与 C 端 HsfNetworkPage.c 同款。
    WIFI_POLL_MS = 250
    #: 界面侧兜底超时（秒）：扫描 / 连接。驱动侧更早超时并给出原因。
    WIFI_SCAN_TIMEOUT_S = 15
    WIFI_CONNECT_TIMEOUT_S = 30

    #: [网口设置 2026-11-24] OS 查询/应用跑工作线程，结果用信号回 UI 线程
    #: （PowerShell/nmcli 单次几十~几百 ms，不能卡住界面线程）。
    ethInfoReady = pyqtSignal(list)
    ethInfoFailed = pyqtSignal(str)
    ethApplyDone = pyqtSignal(bool, str)

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("网络设置", "服务端连接参数 + 本机网口/WiFi")

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
        # 「说明」原是只有一句话的孤立卡：并入「当前连接」卡底部
        # （同属连接话题，少一张卡、少一次滚动）。
        note = hint_label("连接参数在登录窗口填写。要改服务器地址，请退出后重新登录"
                          "（或用下方重新连接按钮回到登录窗）。")
        card.add(note)
        self.root.addWidget(card)

        # [网口设置 2026-11-24 用户「网络设置支持下网口动态ip或静态ip，
        #  并可以显示网络信息」] 有线网口信息表 + 选中网口的 DHCP/静态配置。
        self._eth_ports = []
        self._eth_loading = False
        self._eth_applying = False
        self.ethInfoReady.connect(self._eth_on_info)
        self.ethInfoFailed.connect(self._eth_on_info_failed)
        self.ethApplyDone.connect(self._eth_on_apply_done)
        self._build_eth_card()

        self._build_wifi_card()
        self.root.addStretch(1)

    # ------------------------------------------------------------------
    #                        网口区（2026-11-24）
    # ------------------------------------------------------------------

    def _build_eth_card(self):
        """有线网口信息 + DHCP/静态 IP 配置（Windows PowerShell / 板端 nmcli）。"""
        card = Card("网口设置")
        self._eth_card = card

        self._eth_table = DataTable()
        self._eth_table.set_headers(("网口", "状态", "IPv4", "MAC"))
        self._eth_table.set_column_weights((2, 3, 4, 4))
        self._eth_table.fit_to_content()
        self._eth_table.table.cellClicked.connect(self._eth_on_table_click)
        card.add(self._eth_table)

        # ---- 选中网口 ----
        self.eth_port_combo = QComboBox()
        self.eth_port_combo.setMinimumWidth(180)
        self.eth_port_combo.currentIndexChanged.connect(
            self._eth_on_port_changed)
        self.eth_refresh_btn = QPushButton("刷新")
        self.eth_refresh_btn.clicked.connect(self._eth_refresh)
        card.body.addWidget(wrap_row([
            label_group("网口", self.eth_port_combo),
            self.eth_refresh_btn,
        ]))

        self.eth_detail = QLabel("尚未读取网口信息")
        self.eth_detail.setWordWrap(True)
        self.eth_detail.setStyleSheet(theme.caption_label_qss())
        card.add(self.eth_detail)

        # ---- 配置方式 ----
        self.eth_mode_combo = QComboBox()
        self.eth_mode_combo.addItem("自动获取（DHCP）", "dhcp")
        self.eth_mode_combo.addItem("静态 IP", "static")
        self.eth_mode_combo.currentIndexChanged.connect(
            self._eth_update_static_enabled)

        # ---- 静态字段（窄窗自动折行，不裁字）----
        self.eth_ip_edit = QLineEdit()
        self.eth_ip_edit.setPlaceholderText("例如 192.168.1.50")
        self.eth_mask_edit = QLineEdit()
        self.eth_mask_edit.setPlaceholderText("255.255.255.0")
        self.eth_gw_edit = QLineEdit()
        self.eth_gw_edit.setPlaceholderText("可留空（无网关）")
        self.eth_dns1_edit = QLineEdit()
        self.eth_dns1_edit.setPlaceholderText("可留空")
        self.eth_dns2_edit = QLineEdit()
        self.eth_dns2_edit.setPlaceholderText("可留空")
        for _edit in (self.eth_ip_edit, self.eth_mask_edit,
                      self.eth_gw_edit, self.eth_dns1_edit,
                      self.eth_dns2_edit):
            fit_input_width(_edit)

        card.body.addWidget(wrap_row([
            label_group("方式", self.eth_mode_combo),
            label_group("IP 地址", self.eth_ip_edit),
            label_group("子网掩码", self.eth_mask_edit),
        ]))
        card.body.addWidget(wrap_row([
            label_group("网关", self.eth_gw_edit),
            label_group("DNS 1", self.eth_dns1_edit),
            label_group("DNS 2", self.eth_dns2_edit),
        ]))

        self.eth_apply_btn = QPushButton("应用网口设置")
        self.eth_apply_btn.clicked.connect(self._eth_apply)
        self.eth_perm_label = QLabel("")
        self.eth_perm_label.setWordWrap(True)
        self.eth_perm_label.setStyleSheet(theme.caption_label_qss())
        card.body.addWidget(wrap_row([
            self.eth_apply_btn, self.eth_perm_label]))

        self.eth_status = QLabel("进入本页时读取网口信息")
        self.eth_status.setWordWrap(True)
        self.eth_status.setStyleSheet(theme.caption_label_qss())
        card.add(self.eth_status)

        _warn = QLabel("修改网口 IP 可能短暂断网（若当前正通过该网口连接"
                       "服务端/远程桌面，请改用其它网口或现场操作）。"
                       "Windows 设置会持久化；板端由 NetworkManager 保存"
                       "到连接档案，重启不丢。")
        _warn.setWordWrap(True)
        _warn.setStyleSheet(theme.caption_label_qss())
        card.add(_warn)
        self.root.addWidget(card)

    def _eth_current_port(self):
        pid = self.eth_port_combo.currentData()
        for port in self._eth_ports:
            if str(port.get("id")) == str(pid):
                return port
        return None

    def _eth_refresh(self):
        """工作线程读网口信息（PowerShell/nmcli 不能卡 UI 线程）。"""
        if self._eth_loading or self._eth_applying:
            return
        self._eth_loading = True
        self.eth_refresh_btn.setEnabled(False)
        self.eth_status.setText("正在读取网口信息…")

        def work():
            try:
                from . import netcfg
                ports = netcfg.list_ports()
            except Exception as exc:                   # noqa: BLE001
                try:
                    self.ethInfoFailed.emit(str(exc))
                except RuntimeError:                   # 页面已析构
                    pass
                return
            try:
                self.ethInfoReady.emit(ports)
            except RuntimeError:
                pass

        import threading
        threading.Thread(target=work, name="hsf-eth-refresh",
                         daemon=True).start()

    def _eth_on_info_failed(self, text):
        self._eth_loading = False
        self.eth_refresh_btn.setEnabled(True)
        self._eth_ports = []
        self._eth_table.set_rows([])
        self.eth_port_combo.clear()
        self.eth_detail.setText("网口信息读取失败")
        self.eth_status.setText("读取网口信息失败：%s" % text)
        self._eth_set_controls_enabled(False)
        self.ctx.set_status("读取网口信息失败：%s" % text)

    def _eth_on_info(self, ports):
        self._eth_loading = False
        self.eth_refresh_btn.setEnabled(True)
        self._eth_ports = list(ports or [])
        rows = []
        for port in self._eth_ports:
            speed = port.get("speed_mbps")
            state = "已连接" if port.get("link") else "未连接"
            if speed:
                state += " · %d Mbps" % int(speed)
            rows.append((port.get("name") or "--", state,
                         port.get("ipv4") or "--",
                         port.get("mac") or "--"))
        self._eth_table.set_rows(rows)
        try:
            self._eth_table.fit_to_content()
        except Exception:                              # noqa: BLE001
            pass
        for idx, port in enumerate(self._eth_ports):
            item = self._eth_table.table.item(idx, 1)
            if item is not None:
                item.setForeground(QColor(
                    theme.CLR_OK if port.get("link") else theme.CLR_OFFLINE))

        current = self.eth_port_combo.currentData()
        self.eth_port_combo.blockSignals(True)
        self.eth_port_combo.clear()
        for port in self._eth_ports:
            label = port.get("name") or str(port.get("id"))
            desc = port.get("desc") or ""
            if desc and desc != label:
                label = "%s - %s" % (label, desc)
            self.eth_port_combo.addItem(label, port.get("id"))
        index = self.eth_port_combo.findData(current)
        if index < 0 and self.eth_port_combo.count() > 0:
            index = 0
        if index >= 0:
            self.eth_port_combo.setCurrentIndex(index)
        self.eth_port_combo.blockSignals(False)

        from . import netcfg
        allowed, why = netcfg.can_configure()
        self._eth_perm_label_text = why
        self.eth_perm_label.setText(why)
        self._eth_perm_ok = bool(allowed)
        if not self._eth_ports:
            self.eth_detail.setText("未找到有线网口（无线请用下方 WiFi 设置）")
            self.eth_status.setText("未找到有线网口")
            self._eth_set_controls_enabled(False)
            return
        self.eth_status.setText("已读取 %d 个有线网口" % len(self._eth_ports))
        self._eth_on_port_changed()
        self._eth_set_controls_enabled(True)

    def _eth_on_table_click(self, row, _col):
        if 0 <= row < len(self._eth_ports):
            idx = self.eth_port_combo.findData(self._eth_ports[row].get("id"))
            if idx >= 0:
                self.eth_port_combo.setCurrentIndex(idx)

    def _eth_on_port_changed(self, *_args):
        port = self._eth_current_port()
        if port is None:
            return
        mode = port.get("mode") or "unknown"
        idx = self.eth_mode_combo.findData(mode)
        self.eth_mode_combo.blockSignals(True)
        self.eth_mode_combo.setCurrentIndex(idx if idx >= 0 else 0)
        self.eth_mode_combo.blockSignals(False)
        self.eth_ip_edit.setText(port.get("ipv4") or "")
        self.eth_mask_edit.setText(
            port.get("mask") or "255.255.255.0")
        self.eth_gw_edit.setText(port.get("gateway") or "")
        dns = list(port.get("dns") or [])
        self.eth_dns1_edit.setText(dns[0] if len(dns) > 0 else "")
        self.eth_dns2_edit.setText(dns[1] if len(dns) > 1 else "")
        mode_text = {"dhcp": "自动获取（DHCP）",
                     "static": "静态 IP"}.get(mode, "未知")
        speed = port.get("speed_mbps")
        lines = [
            "获取方式：%s" % mode_text,
            "连接状态：%s%s" % ("已连接" if port.get("link") else "未连接",
                             (" · %d Mbps" % int(speed)) if speed else ""),
            "网卡：%s" % (port.get("desc") or port.get("name") or "--"),
            "MAC：%s" % (port.get("mac") or "--"),
            "IP / 掩码：%s / %s" % (port.get("ipv4") or "--",
                                   port.get("mask") or "--"),
            "网关：%s" % (port.get("gateway") or "--"),
            "DNS：%s" % ("  ".join(port.get("dns") or []) or "--"),
        ]
        self.eth_detail.setText("\n".join(lines))
        self._eth_update_static_enabled()

    def _eth_update_static_enabled(self, *_args):
        static = self.eth_mode_combo.currentData() == "static"
        on = bool(static and getattr(self, "_eth_perm_ok", False)
                  and not self._eth_applying)
        for edit in (self.eth_ip_edit, self.eth_mask_edit, self.eth_gw_edit,
                     self.eth_dns1_edit, self.eth_dns2_edit):
            edit.setEnabled(on)
        if hasattr(self, "eth_apply_btn"):
            self.eth_apply_btn.setEnabled(
                bool(getattr(self, "_eth_perm_ok", False)
                     and self._eth_ports and not self._eth_applying))

    def _eth_set_controls_enabled(self, on):
        for widget in (self.eth_port_combo, self.eth_mode_combo,
                       self.eth_apply_btn):
            widget.setEnabled(bool(on and self._eth_ports))
        if not on:
            for edit in (self.eth_ip_edit, self.eth_mask_edit,
                         self.eth_gw_edit, self.eth_dns1_edit,
                         self.eth_dns2_edit):
                edit.setEnabled(False)
        self._eth_update_static_enabled()

    def _eth_apply(self):
        """确认后应用；实际系统调用走工作线程。"""
        port = self._eth_current_port()
        if port is None:
            self.eth_status.setText("请先选择一个网口")
            return
        if not getattr(self, "_eth_perm_ok", False):
            self.eth_status.setText(
                getattr(self, "_eth_perm_label_text", "") or "无修改权限")
            return
        mode = self.eth_mode_combo.currentData() or "dhcp"
        if mode == "static":
            from . import netcfg
            try:
                netcfg.validate_static(
                    self.eth_ip_edit.text().strip(),
                    self.eth_mask_edit.text().strip(),
                    self.eth_gw_edit.text().strip(),
                    [self.eth_dns1_edit.text().strip(),
                     self.eth_dns2_edit.text().strip()])
            except ValueError as exc:
                self.eth_status.setText("参数不合法：%s" % exc)
                return
        summary = ("自动获取（DHCP）" if mode == "dhcp"
                   else "静态 IP %s / %s，网关 %s"
                   % (self.eth_ip_edit.text().strip(),
                      self.eth_mask_edit.text().strip(),
                      self.eth_gw_edit.text().strip() or "无"))
        ret = QMessageBox.question(
            self, "修改网口",
            "确定把「%s」设为 %s？\n\n修改期间该网口可能短暂断开，"
            "若正通过它连接服务端/远程桌面会掉线。"
            % (port.get("name") or port.get("id"), summary),
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if ret != QMessageBox.Yes:
            return
        self._eth_apply_confirmed()

    def _eth_apply_confirmed(self):
        """真正下发（测试入口：跳过确认框）。"""
        port = self._eth_current_port()
        if port is None:
            return
        mode = self.eth_mode_combo.currentData() or "dhcp"
        args = dict(
            port_id=port.get("id"), mode=mode,
            ip=self.eth_ip_edit.text().strip(),
            mask=self.eth_mask_edit.text().strip(),
            gateway=self.eth_gw_edit.text().strip(),
            dns=[self.eth_dns1_edit.text().strip(),
                 self.eth_dns2_edit.text().strip()])
        self._eth_applying = True
        self.eth_apply_btn.setEnabled(False)
        self.eth_refresh_btn.setEnabled(False)
        self.eth_status.setText("正在应用网口设置…")
        self.ctx.set_status("正在应用网口设置…")

        def work():
            try:
                from . import netcfg
                msg = netcfg.apply_port(**args)
            except Exception as exc:                   # noqa: BLE001
                try:
                    self.ethApplyDone.emit(False, str(exc))
                except RuntimeError:
                    pass
                return
            try:
                self.ethApplyDone.emit(True, msg)
            except RuntimeError:
                pass

        import threading
        threading.Thread(target=work, name="hsf-eth-apply",
                         daemon=True).start()

    def _eth_on_apply_done(self, ok, text):
        self._eth_applying = False
        self.eth_refresh_btn.setEnabled(True)
        self._eth_update_static_enabled()
        self.eth_status.setText(("应用成功：%s" % text) if ok
                                else ("应用失败：%s" % text))
        self.ctx.set_status(("应用成功：%s" % text) if ok
                            else ("应用失败：%s" % text))
        if ok:
            QTimer.singleShot(1500, self._eth_refresh)

    # ------------------------------------------------------------------
    #                        WiFi 区
    # ------------------------------------------------------------------

    def _build_wifi_card(self):
        """建 WiFi 设置卡（控件先建好，能力不可用时统一置灰）。"""
        card = Card("WiFi 设置")
        self._wifi_card = card

        # ---- 适配器行 ----
        adapter_row = QHBoxLayout()
        adapter_row.addWidget(QLabel("适配器"))
        self.wifi_adapter_combo = QComboBox()
        self.wifi_adapter_combo.setMinimumWidth(220)
        # [长适配器名省略时信息可达 2026-11-20] 见 _sync_wifi_adapter_tip。
        self.wifi_adapter_combo.currentIndexChanged.connect(
            self._sync_wifi_adapter_tip)
        self.wifi_adapter_combo.currentIndexChanged.connect(
            self._wifi_on_adapter_changed)
        adapter_row.addWidget(self.wifi_adapter_combo)
        self.wifi_scan_btn = QPushButton("扫描")
        self.wifi_scan_btn.clicked.connect(self._wifi_scan)
        adapter_row.addWidget(self.wifi_scan_btn)
        adapter_row.addStretch(1)
        card.body.addLayout(adapter_row)

        # ---- 网络行 ----
        self.wifi_net_combo = QComboBox()
        self.wifi_net_combo.setMinimumWidth(220)
        # [长 SSID 省略时信息可达 2026-11-20] 网络名很长（「WLAN 3 -
        # Realtek 8832BU Wireless LAN WiFi 6 US」）⇒ 框里放不下时 Qt 用
        # 省略号截断（不是硬裁，展开下拉能看全）⇒ 再加一条 tooltip：
        # 悬停即见全名。
        self.wifi_net_combo.currentIndexChanged.connect(
            self._sync_wifi_net_tip)
        self.wifi_pass_edit = QLineEdit()
        self.wifi_pass_edit.setEchoMode(QLineEdit.Password)
        self.wifi_pass_edit.setPlaceholderText("加密网络必填")
        # [输入框不许裁字 2026-11-20] 窄窗（640×900）时这一行放不下 ⇒
        # 布局把口令框挤到 67px ⇒ 占位符「加密网络必填」被截成「加密网络…」。
        # ⇒ ① 口令框按**字体实测**给底宽；② 这一行改成会换行的容器
        # （放不下就折行，绝不裁字，也不把别的控件挤出卡片）。
        fit_input_width(self.wifi_pass_edit)
        self.wifi_connect_btn = QPushButton("连接")
        self.wifi_connect_btn.clicked.connect(self._wifi_connect)
        self.wifi_disconnect_btn = QPushButton("断开")
        self.wifi_disconnect_btn.clicked.connect(self._wifi_disconnect)
        net_wrap = wrap_row([
            label_group("网络", self.wifi_net_combo),
            label_group("口令", self.wifi_pass_edit),
            self.wifi_connect_btn,
            self.wifi_disconnect_btn,
        ])
        card.body.addWidget(net_wrap)

        # ---- 状态行（当前连接 / 扫描进度 / 失败原因）----
        self.wifi_info = QLabel("无线：未初始化")
        self.wifi_info.setWordWrap(True)
        self.wifi_info.setStyleSheet(theme.caption_label_qss())
        card.body.addWidget(self.wifi_info)

        self.root.addWidget(card)

        # ---- 运行期状态 ----
        self._wifi_bridge = None
        self._wifi_native = None
        self._wifi_ready = False
        self._wifi_scanning = False
        self._wifi_connecting = False
        self._wifi_scan_note = ""      # 扫描结果备注（挂在连接态后面）
        self._wifi_scan_deadline = 0.0
        self._wifi_connect_deadline = 0.0
        self._wifi_last_conn_refresh = 0.0

        self._wifi_timer = QTimer(self)
        self._wifi_timer.setInterval(self.WIFI_POLL_MS)
        self._wifi_timer.timeout.connect(self._wifi_poll)

    def _wifi_widgets(self):
        """WiFi 区的全部控件（统一置灰/启用用）。"""
        return (self.wifi_adapter_combo, self.wifi_scan_btn,
                self.wifi_net_combo, self.wifi_pass_edit,
                self.wifi_connect_btn, self.wifi_disconnect_btn)

    def _wifi_set_enabled(self, on):
        for widget in self._wifi_widgets():
            widget.setEnabled(bool(on))

    def _wifi_init(self):
        """打开无线会话并刷新适配器（失败则置灰并说明原因）。"""
        if self._wifi_ready:
            # 已就绪：把轮询表重新拉起来（on_leave 停过）。
            if not self._wifi_timer.isActive():
                self._wifi_timer.start()
            return True
        try:
            from . import hsf_native
        except Exception as exc:                          # pragma: no cover
            self._wifi_report("无线功能不可用：模块导入失败（%s）" % exc)
            self._wifi_set_enabled(False)
            return False
        if not hsf_native.available():
            self._wifi_report(
                "无线功能不可用：缺少 hsb_bridge 动态库（%s）。"
                "编译口径：.zcode/build_hsf_bridge.bat（板端需交叉编译出 "
                "libhsb_bridge.so）" % hsf_native.library_path())
            self._wifi_set_enabled(False)
            return False
        bridge = hsf_native.bridge()
        if not bridge.wifi_open():
            self._wifi_report("无线不可用：%s" % (bridge.last_error()
                                                 or "会话打开失败"))
            self._wifi_set_enabled(False)
            return False
        self._wifi_bridge = bridge
        self._wifi_native = hsf_native
        self._wifi_ready = True
        self._wifi_set_enabled(True)
        self._wifi_refresh_adapters()
        self._wifi_refresh_connection()
        self._wifi_timer.start()
        return True

    def _wifi_report(self, text):
        """状态行 + 主窗状态栏（两处都落，终态可见）。"""
        self.wifi_info.setText(text)
        self.ctx.set_status(text)

    def _wifi_refresh_adapters(self):
        """枚举适配器并填下拉。"""
        if not self._wifi_ready:
            return
        adapters = self._wifi_bridge.wifi_adapters()
        if adapters is None:
            self._wifi_report("无线不可用：%s"
                              % (self._wifi_bridge.last_error()
                                 or "枚举适配器失败"))
            self._wifi_set_enabled(False)
            return
        current = self.wifi_adapter_combo.currentData()
        self.wifi_adapter_combo.blockSignals(True)
        self.wifi_adapter_combo.clear()
        for item in adapters:
            name = item.get("name") or item.get("id") or "适配器"
            desc = item.get("desc") or ""
            label = "%s - %s" % (name, desc) if desc else str(name)
            self.wifi_adapter_combo.addItem(label, item.get("id"))
        index = self.wifi_adapter_combo.findData(current)
        if index < 0 and self.wifi_adapter_combo.count() > 0:
            index = 0
        if index >= 0:
            self.wifi_adapter_combo.setCurrentIndex(index)
        self.wifi_adapter_combo.blockSignals(False)
        self._sync_wifi_adapter_tip()
        if not adapters:
            self._wifi_report("无线：未找到无线适配器")

    def _sync_wifi_adapter_tip(self, *_args):
        """适配器下拉的 tooltip = **当前适配器全名**（信息可达）。

        [为什么] 适配器名很长（「WLAN 3 - Realtek 8832BU Wireless LAN WiFi
        6 USB NIC」）⇒ 框窄时 Qt 用省略号截断（展开下拉能看全，但要多一
        步）⇒ 悬停即见全名。
        """
        try:
            self.wifi_adapter_combo.setToolTip(
                self.wifi_adapter_combo.currentText())
        except RuntimeError:                            # 控件已析构
            pass

    def _wifi_current_adapter(self):
        data = self.wifi_adapter_combo.currentData()
        return data if data else None

    def _wifi_refresh_connection(self):
        """刷新当前连接状态行。

        [为什么要带扫描备注] 空闲轮询每 1s 刷一次连接态，会把「扫描完成：
        发现 N 个网络」冲掉 ⇒ 用户刚扫完只看到连接信息，看不到扫描结果。
        故把扫描结果作为**备注**挂在连接态后面（备注在下次扫描时清）。
        """
        if not self._wifi_ready:
            return
        adapter = self._wifi_current_adapter()
        if not adapter:
            return
        info = self._wifi_bridge.wifi_connection(adapter)
        if info is None:
            self._wifi_report("无线：状态查询失败（%s）"
                              % (self._wifi_bridge.last_error() or "未知原因"))
            return
        state = info.get("state") or "idle"
        ssid = info.get("ssid")
        note = self._wifi_scan_note
        if state == "connected" and ssid:
            text = "无线：已连接 %s（信号 %s%%，%s）" % (
                ssid, info.get("signal"), info.get("secure") or "未知加密")
        elif state == "connecting":
            text = "无线：连接中…"
        elif state == "failed":
            text = "无线：连接失败"
        else:
            text = "无线：未连接"
        if note:
            text = "%s｜%s" % (text, note)
        self._wifi_report(text)

    def _wifi_scan(self):
        """发起扫描（扫描中由轮询收口）。"""
        if not self._wifi_init():
            return
        adapter = self._wifi_current_adapter()
        if not adapter:
            self._wifi_report("无线：请先选择适配器")
            return
        if not self._wifi_bridge.wifi_start_scan(adapter):
            self._wifi_report("扫描失败：%s"
                              % (self._wifi_bridge.last_error()
                                 or "发起失败"))
            return
        self._wifi_scanning = True
        self._wifi_scan_note = ""      # 新一轮扫描 ⇒ 清旧备注
        self._wifi_scan_deadline = (time.monotonic()
                                     + self.WIFI_SCAN_TIMEOUT_S)
        self.wifi_scan_btn.setEnabled(False)
        self._wifi_report("无线：扫描中…")

    def _wifi_take_results(self):
        """取走扫描结果并填网络下拉。"""
        adapter = self._wifi_current_adapter()
        if not adapter:
            self._wifi_scanning = False
            self.wifi_scan_btn.setEnabled(True)
            return
        networks = self._wifi_bridge.wifi_take_scan_results(adapter)
        self._wifi_scanning = False
        self.wifi_scan_btn.setEnabled(True)
        if networks is None:
            self._wifi_report("扫描失败：%s"
                              % (self._wifi_bridge.last_error()
                                 or "没有结果"))
            return
        self.wifi_net_combo.clear()
        for item in networks:
            ssid = item.get("ssid") or "（隐藏网络）"
            label = "%s  (%s%%, %s)" % (ssid, item.get("signal"),
                                        item.get("secure") or "未知")
            self.wifi_net_combo.addItem(label, item.get("ssid"))
        if self.wifi_net_combo.count() > 0:
            self.wifi_net_combo.setCurrentIndex(0)
        self._sync_wifi_net_tip()
        # 扫描结果作为备注挂在连接态后面（不被 1s 的连接刷新冲掉）。
        self._wifi_scan_note = "扫描完成：发现 %d 个网络" % len(networks)
        self._wifi_refresh_connection()

    def _sync_wifi_net_tip(self, *_args):
        """网络下拉的 tooltip = **当前网络全名**（信息可达）。

        [为什么] 网络名很长（「WLAN 3 - Realtek 8832BU Wireless LAN WiFi 6
        US」）⇒ 框窄时 Qt 用省略号截断（下拉展开能看全，但要多一步）⇒
        悬停即见全名，操作员不用展开下拉。
        """
        try:
            self.wifi_net_combo.setToolTip(self.wifi_net_combo.currentText())
        except RuntimeError:                            # 控件已析构
            pass

    def _wifi_connect(self):
        """发起连接（连接中由轮询收口）。"""
        if not self._wifi_init():
            return
        adapter = self._wifi_current_adapter()
        ssid = self.wifi_net_combo.currentData()
        if not adapter:
            self._wifi_report("无线：请先选择适配器")
            return
        if not ssid:
            self._wifi_report("无线：请先选择网络（可点「扫描」查找）")
            return
        if not self._wifi_bridge.wifi_connect(adapter, ssid,
                                              self.wifi_pass_edit.text()):
            self._wifi_report("连接失败：%s"
                              % (self._wifi_bridge.last_error()
                                 or "发起失败"))
            return
        self._wifi_connecting = True
        self._wifi_connect_deadline = (time.monotonic()
                                       + self.WIFI_CONNECT_TIMEOUT_S)
        self._wifi_report("无线：连接中…")

    def _wifi_disconnect(self):
        """断开当前连接。"""
        if not self._wifi_ready:
            return
        adapter = self._wifi_current_adapter()
        if not adapter:
            self._wifi_report("无线：请先选择适配器")
            return
        if not self._wifi_bridge.wifi_disconnect(adapter):
            self._wifi_report("断开失败：%s"
                              % (self._wifi_bridge.last_error()
                                 or "平台拒绝"))
            return
        self._wifi_connecting = False
        self._wifi_report("无线：已断开")

    def _wifi_poll(self):
        """状态轮询（250ms）：收口扫描与连接的终态。"""
        if not self._wifi_ready:
            return
        adapter = self._wifi_current_adapter()
        if not adapter:
            return
        now = time.monotonic()
        if self._wifi_scanning:
            state = self._wifi_bridge.wifi_scan_state(adapter)
            native = self._wifi_native
            if state == native.SCAN_RUNNING:
                if now > self._wifi_scan_deadline:
                    self._wifi_scanning = False
                    self.wifi_scan_btn.setEnabled(True)
                    self._wifi_report("扫描超时：%d 秒内未完成"
                                      % self.WIFI_SCAN_TIMEOUT_S)
                return
            if state == native.SCAN_COMPLETED:
                self._wifi_take_results()
                return
            if state == native.SCAN_FAILED:
                self._wifi_scanning = False
                self.wifi_scan_btn.setEnabled(True)
                self._wifi_report("扫描失败：驱动报告扫描失败")
                return
            if state < 0:
                self._wifi_scanning = False
                self.wifi_scan_btn.setEnabled(True)
                self._wifi_report("扫描失败：%s"
                                  % (self._wifi_bridge.last_error()
                                     or "状态查询失败"))
                return
            return
        if self._wifi_connecting:
            info = self._wifi_bridge.wifi_connection(adapter)
            if info is None:
                self._wifi_connecting = False
                self._wifi_report("连接失败：%s"
                                  % (self._wifi_bridge.last_error()
                                     or "状态查询失败"))
                return
            state = info.get("state")
            if state == "connected":
                self._wifi_connecting = False
                self._wifi_refresh_connection()
                self.ctx.set_status("无线连接成功：%s"
                                    % (info.get("ssid") or "已连接"))
                return
            if state == "failed":
                self._wifi_connecting = False
                self._wifi_report("连接失败：驱动报告连接失败")
                return
            if now > self._wifi_connect_deadline:
                self._wifi_connecting = False
                self._wifi_report("连接超时：%d 秒内未连上"
                                  % self.WIFI_CONNECT_TIMEOUT_S)
                return
            return
        # 空闲态：1s 刷一次当前连接（不占用 250ms 的密集轮询）。
        if now - self._wifi_last_conn_refresh >= 1.0:
            self._wifi_last_conn_refresh = now
            self._wifi_refresh_connection()

    def _wifi_on_adapter_changed(self, _index):
        """切换适配器：刷新该适配器的当前连接。"""
        self._wifi_refresh_connection()

    # ------------------------------------------------------------------
    #                       页面生命周期
    # ------------------------------------------------------------------

    def on_enter(self):
        """进入本页：打开无线会话（懒初始化，失败则置灰说明）+ 读网口。"""
        self._eth_refresh()
        self._wifi_init()

    def on_leave(self):
        """离开本页：停轮询（会话保留，下次进入直接用）。"""
        if self._wifi_timer.isActive():
            self._wifi_timer.stop()

    def closeEvent(self, event):
        """页面销毁：停轮询并关闭无线会话。"""
        if self._wifi_timer.isActive():
            self._wifi_timer.stop()
        if self._wifi_bridge is not None:
            self._wifi_bridge.wifi_close()
            self._wifi_bridge = None
            self._wifi_ready = False
        Page.closeEvent(self, event)

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



def apply_overlay(win, ui_settings):
    """按本机设置显隐主窗的标题栏/状态栏/导航（启动还原与设置页共用）。

    [为什么抽成函数] 启动路径（`__main__`，主窗刚建好）与本页的勾选回调
    必须**同一套**判定，否则"重启后和当场看到的不一样"。缺属性时静默跳过
    （测试里的轻量替身窗口没有这些部件）。
    """
    for attr, key in (("title_bar", ui_settings.K_SHOW_TITLE),
                      ("status_bar", ui_settings.K_SHOW_STATUS),
                      ("nav", ui_settings.K_SHOW_NAV)):
        w = getattr(win, attr, None)
        if w is None:
            continue
        visible = ui_settings.get_bool(key, True)
        # [2026-11-22 用户定案「切到菜单页时隐藏导航树」] 菜单页模式下
        # 导航由菜单页承担，左侧导航树**强制隐藏**（独立于「显示左侧导航
        # 栏」开关；开关值原样保留，切回导航树档即按其恢复）。
        if attr == "nav":
            visible = (visible and ui_settings.menu_button_style()
                       == ui_settings.MENU_STYLE_TREE)
        w.setVisible(visible)
    # [标题栏内部元素] 时钟/温湿度的显隐（2026-11-13 合并进本地设置页的
    # 「悬浮窗」分区时由死控件改成真兑现）。
    title_bar = getattr(win, "title_bar", None)
    if title_bar is not None:
        clock_setter = getattr(title_bar, "set_clock_visible", None)
        if clock_setter is not None:
            clock_setter(ui_settings.get_bool(ui_settings.K_SHOW_CLOCK, True))
        th_setter = getattr(title_bar, "set_temphum_visible", None)
        if th_setter is not None:
            th_setter(ui_settings.get_bool(ui_settings.K_SHOW_TEMPHUM, True))
        # [2026-11-22 标题栏新元素] 二维码/模式/健康此前没进本地设置 ⇒
        # 现场想藏掉顶栏冗余元素只能改代码。与时钟/温湿度同一通道。
        qr_setter = getattr(title_bar, "set_qr_visible", None)
        if qr_setter is not None:
            qr_setter(ui_settings.get_bool(ui_settings.K_SHOW_QR, True))
        camera_setter = getattr(title_bar, "set_camera_visible", None)
        if camera_setter is not None:
            camera_setter(ui_settings.get_bool(ui_settings.K_SHOW_CAMERA,
                                               True))
        mode_setter = getattr(title_bar, "set_mode_visible", None)
        if mode_setter is not None:
            mode_setter(ui_settings.get_bool(ui_settings.K_SHOW_MODE, True))
        health_setter = getattr(title_bar, "set_health_visible", None)
        if health_setter is not None:
            health_setter(ui_settings.get_bool(ui_settings.K_SHOW_HEALTH, True))


# [2026-11-13 用户定案「三页合并成一页」] 原「悬浮窗设置(16) / 窗口设置(17)」
# 两个页面类（OverlayPage / WindowPage）**已整体删除**，内容逐项并入
# `LocalSettingsPage`（页号 13）：
#   · OverlayPage 的三个显隐开关 → `LocalSettingsPage._build_overlay_section()`
#   · WindowPage 的尺寸卡/屏幕休眠卡/信息卡
#     → `LocalSettingsPage._build_window_section()` 与 `_build_power_card()`
# （控件名一律不变：show_title/show_status/show_nav/size_combo/power_*）。

