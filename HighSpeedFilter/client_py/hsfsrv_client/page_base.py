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

from PyQt5.QtCore import QPoint, QSize, Qt, QTimer
from PyQt5.QtGui import QFont, QFontMetrics
from PyQt5.QtWidgets import (QFrame, QGridLayout, QHBoxLayout, QLabel,
                             QPushButton, QScrollArea, QSizePolicy,
                             QTableWidget, QVBoxLayout, QWidget,
                             QWIDGETSIZE_MAX)

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
        """是否持有设备控制权限（操作者/管理员；角色值不单调故查表）。"""
        return self.bridge.can(proto.PERM_DEVICE_CONTROL)

    def is_admin(self):
        """是否持有管理面权限（账户/服务端配置；仅管理员）。"""
        return self.bridge.can(proto.PERM_ACCOUNT_ADMIN)

    def is_auto(self):
        return self.model.is_auto()

    def control_blocked_reason(self):
        """控制类操作被闸门挡住的原因；空串 = 允许。"""
        if not self.bridge.is_connected():
            return "未连接服务端"
        if self.model.is_auto():
            # [措辞分两种 2026-11-12] 自动**模式**已切 ≠ 自动**运行**中；
            # 旧文案一律说"自动运行中" ⇒ 控制器没跑时也这么说，操作员会
            # 以为设备正被自动逻辑驱动。顺带点明"DO 由自动逻辑驱动"，
            # 免得看数字量页时以为 DO 不动是坏了。
            if self.model.auto_running:
                return "自动运行中：远程控制已被禁用（输出点由自动逻辑驱动）"
            return "自动模式：远程控制已被禁用（输出点由自动逻辑驱动）"
        if not self.is_root():
            return "权限不足：控制需要操作者或管理员账户"
        return ""


class Page(QWidget):
    """页面基类。"""

    #: 本页需要的推送主题（子类覆写）。切页时主窗按此自动增删订阅。
    TOPICS = ()

    def __init__(self, ctx):
        QWidget.__init__(self)
        self.ctx = ctx
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        # ---- 内容套一层滚动壳（页面自动排布到完整高度）----
        #
        # [实锅 2026-11-05：窗口矮时底部内容被裁掉] 原来页面直接把自己的
        # QVBoxLayout 当根布局，控件高度由**窗口剩余高度**决定。窗口一矮，
        # 布局就把卡片压到最小高度以下 —— 数字量页 32 点时 DI 卡片只露出
        # 3 行半，CH24 那排的圆点还在、下面的通道号/语义名被卡片边框切掉。
        #
        # 现在内容装在 QScrollArea 里，**内层控件按自然高度（各控件
        # sizeHint/minimumSizeHint 之和）排布**，窗口放不下时右侧自动出现
        # 垂直滚动条，滚一下就看到全部。这样"布局完全显示"与"窗口大小"
        # 解耦：矮窗口是滚动，不是压缩。
        #
        # 子类照旧往 `self.root` 里加东西 —— 那已经是内层内容布局了，
        # 页面代码无需改动。
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)      # 内层随视口变宽
        self.scroll.setFrameShape(QFrame.NoFrame)
        # 两个方向都"需要时出现"：窄窗口横向也能滚，不会把内容挤出界。
        self.scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.scroll.setStyleSheet(
            "QScrollArea { background: transparent; border: none; }")

        inner = QWidget()
        inner.setObjectName("HsfPageContent")
        inner.setStyleSheet(
            "QWidget#HsfPageContent { background: transparent; }")
        self.root = QVBoxLayout(inner)
        # 右侧留 6px：滚动条出现时不贴着内容（滚动条在视口外，这里只是观感）。
        self.root.setContentsMargins(0, 0, 6, 6)
        self.root.setSpacing(10)
        self.scroll.setWidget(inner)
        outer.addWidget(self.scroll)
        #: 内容控件（诊断/测试用：可据此判断是否可滚动）。
        self.content = inner
        # [触摸惯性滚动 2026-11-14] 板端是触摸屏，长窗口（竖屏 800×1280）
        # 里内容常常超出一屏 ⇒ 必须能**用手指上下拖**来滚。
        # QScrollArea 本身能拖，但没有**惯性**（松手即停，滑动发涩）；
        # QScroller 补上惯性 + 边界回弹（板端长距离滚动手感差别很大）。
        # [为什么不会抢控件的触摸] QScroller 只在**视口**上接管拖动；
        # 控件（下拉/输入框）自己收触摸事件，不会被滚走。
        self._scroller = _attach_scroller(self.scroll)

    def topics(self):
        return self.TOPICS

    def refresh(self):
        """数据刷新钩子（每 200ms）。子类覆写。"""

    def on_enter(self):
        """切到本页时调用（可用于按需拉快照）。"""

    def on_leave(self):
        """离开本页时调用。"""

    # ---- 表格吃满视口（表头固定）----
    #
    # [2026-11-22 用户口径] 历史/日志/报警的表头要「固定住，不随表格拖动
    # 移动」。QTableWidget 表内滚动时表头本来就固定；问题是页面外层还套了
    # 一层 QScrollArea：窗口一矮，整页可滚 ⇒ 拖外层滚动条/在表头外滚轮时
    # 表头跟着页面走。
    # 处置：把表格高度钉成「外层视口高 - 表格上方已占高度」⇒ 页面内容不再
    # 超出视口（外层滚动条消失），滚动全部落在表格自己的滚动条上 ⇒ 表头
    # 恒定不动。窗口缩放/折叠筛选条后重算（见 resizeEvent 与各页接线）。
    def fit_table_to_viewport(self, table, min_h=160):
        """让 `table`（DataTable 外层控件）吃掉页面剩余高度（表头固定）。"""
        self._fit_table = table
        self._fit_min_h = max(120, int(min_h))
        QTimer.singleShot(0, self._apply_table_fit)

    def _apply_table_fit(self):
        table = getattr(self, "_fit_table", None)
        if table is None or not table.isVisible():
            return
        vh = self.scroll.viewport().height()
        if vh <= 0:
            return
        try:
            top = table.mapTo(self.content, QPoint(0, 0)).y()
        except (TypeError, RuntimeError):
            return
        # 表下方固定占用：Card 下内边距 7 + 页面根下边距 6 + 1px 取整余量。
        h = max(self._fit_min_h, vh - top - 14)
        if table.height() != h:
            table.setFixedHeight(h)

    def on_data_toolbar_collapsed(self, collapsed):
        """筛选条折叠/展开 ⇒ 表格可吃的高度变了，重新钉一次。"""
        table = getattr(self, "_table", None)
        if table is not None:
            table.set_expandable(bool(collapsed))
        QTimer.singleShot(0, self._apply_table_fit)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._apply_table_fit()

    def showEvent(self, event):
        QWidget.showEvent(self, event)
        # 预建页构造时是隐藏的（没有真实几何）⇒ 显示后下一拍再量一次。
        QTimer.singleShot(0, self._apply_table_fit)

    # ---- 便捷构件 ----
    def add_header(self, title, subtitle=""):
        """页面大标题 + 说明（各页统一的口径）。"""
        row = QHBoxLayout()
        row.setSpacing(10)
        lbl = QLabel(title)
        lbl.setStyleSheet(
            "QLabel { color: #1F4E79; font-size: 26px; font-weight: bold;"
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

class _ContentHeightTable(QTableWidget):
    """QTableWidget 子类：sizeHint 的高度可切成**实际内容高**。

    [为什么需要] `QTableWidget::sizeHint()` 的高度是**固定启发值**
    （实测恒为 192px，与行数、与字号都无关）⇒ 拿它当"表格内容高"就必然
    要么留一大片空白、要么裁行。⇒ 给"高度要贴合内容"的表
    （`DataTable.fit_to_content` 的使用者）打开本开关：sizeHint 高度 =
    表头 + 各行**实际** sectionSize + 边框 ⇒ 父布局按内容给高度。

    [为什么光有下限不够、还得改 sizeHint] 纵向 sizePolicy=Fixed 时，
    布局取的是 clamp(sizeHint, minimum, maximum)：只给下限时，行数少的
    表会被撑到 192px ⇒ 表下方留一段空白（正是 fit_to_content 当初要
    消灭的现象）。⇒ 两者都要：sizeHint 给内容高、下限也给内容高。
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        #: 是否"高度贴合内容"（默认关 ⇒ 行为与原来完全一致）。
        self._content_sized = False
        #: 预留的空行数（内容还没到、先把位置留出来）。
        self._extra_rows = 0

    def set_content_sized(self, enabled=True, extra_rows=0):
        """开/关"sizeHint 高度 = 内容高"；extra_rows = 预留空行数。"""
        self._content_sized = bool(enabled)
        self._extra_rows = max(0, int(extra_rows))
        self.updateGeometry()

    def content_height(self):
        """表格**实际内容高**（表头 + 各行实际高 + 边框余量）。

        [为什么按"实际 sectionSize"而不是 defaultSectionSize] 行高是
        **内容驱动**的：表内字号放大后 QTableView 会把行撑到装得下
        "字号 + 内边距"（实测 20px 字 ⇒ 行高 32；26px 字 ⇒ 行高 39）
        ⇒ 用缺省行高算出来的数偏小 ⇒ 高度不够 ⇒ 表格内部溢出 ⇒ 竖向
        滚动条 + 末行被裁（2026-11-17 用户实报）。

        [为什么要算上横向滚动条] 窗口变窄时表内会多出一条**横向**滚动条，
        它占掉一段视口高 ⇒ 竖向就溢出（实测 1280 宽正常、820 宽复现）
        ⇒ 横向滚动条在时要把它的高度算进来。
        """
        vh = self.verticalHeader()
        rows = self.rowCount()
        row_h = 0
        for r in range(rows):
            row_h = max(row_h, vh.sectionSize(r))
        if row_h <= 0:
            row_h = vh.defaultSectionSize() or theme.TABLE_ROW_H
        n_rows = max(rows, self._extra_rows)
        header_h = self.horizontalHeader().height() or 34
        hbar = self.horizontalScrollBar()
        hbar_h = hbar.sizeHint().height() if hbar.isVisible() else 0
        return int(header_h + row_h * n_rows + hbar_h
                   + 2 * self.frameWidth() + 4)

    def sizeHint(self):
        sh = super().sizeHint()
        if not self._content_sized:
            return sh
        h = self.content_height()
        if h <= 0:
            return sh
        return QSize(sh.width(), h)


class DataTable(QWidget):
    """只读数据表（「项目 / 数值 / 单位」三列）。

    用 QTableWidget 而不是 QTableView+Model：行数固定（十几个字段），
    直接塞 item 比维护 model 简单，且刷新时只改文本不重建。
    """

    HEADERS = ("项目", "数值", "单位")

    #: 表内文字与表头的额外留白（左右内边距 + 排序指示器）。
    _HEADER_PAD = 30
    _CELL_PAD = 10
    #: 表格字号的最小缩放比（再窄就不缩了，让它横向滚动）。
    #: [为什么是 0.60] 历史页 16 列：按内容保底的下限总和在 15pt 字号下约
    #: 2145px 文字 + 320px 内边距 ⇒ 用户窗口视口 ~1680px（2511 物理像素
    #: @125% 系统缩放）⇒ 需要缩到 ~0.63 才装得下 ⇒ 留 0.60 作下限。
    #: 15pt × 0.60 = 12px 仍清晰可读（数据表本就该比正文小一号）。
    MIN_FIT_SCALE = 0.58
    #: 量内容宽时最多看前几行（历史页一页 10~50 行，够用）。
    _MEASURE_ROWS = 50

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        from PyQt5.QtWidgets import QHeaderView, QAbstractItemView
        # [用 _ContentHeightTable] 它能把 sizeHint 的高度换成**实际内容高**
        # （QTableWidget 自带的 sizeHint 高度恒为 192px，与行数无关 ⇒ 不能用）。
        # 默认不开启 ⇒ 与原来行为完全一致（历史/日志/报警页不受影响）。
        self.table = _ContentHeightTable(0, len(self.HEADERS), self)
        self.table.setHorizontalHeaderLabels(list(self.HEADERS))
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.setShowGrid(False)
        # [竖滚动条**常驻** 2026-11-19 用户实报「数据表格的滚动条有点小，
        #  触摸屏不好触摸拖动」] AsNeeded 的滚动条在滚完后会淡出 ⇒ 触摸屏
        # 上"想直接拖把手"时它已经不见了（只能靠 flick 滑动）⇒ 表格竖条
        # 改为常驻（命中区宽度见 theme 的 SCROLL_W：22px ≈ 3.4mm，可抓）。
        # 横向仍 AsNeeded（横向拖动是鼠标习惯，触屏用不到）。
        self.table.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOn)
        self.table.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        # [表格也挂触摸惯性滚动 2026-11-14] 表格行数多（历史页几百行）⇒
        # 触摸时要在**表格里**上下滑，而不是把整页带着动 ⇒ 惯性滚动器挂
        # 表格自己的视口。
        self._table_scroller = _attach_scroller(self.table)
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
        #: 全局最小列宽的**基准值**（实际值按表内字号比例缩）。
        self._min_section_base = 72
        #: 可见列集合（None = 全部；DataTable.set_visible_columns 改）
        self._visible_cols = None
        #: 按**字体度量**自动算列下限时的"内容代表文本"（{列号: 文本}）。
        #: 空 = 不启用自动下限（用调用方给的 min_widths / 全局档）。
        #: 见 set_column_weights 的 auto_min_texts 说明。
        self._auto_min_texts = {}
        #: **实测**的各列最长单元格文字宽（{列号: 像素}）—— 由 set_rows
        #: 每次换数据后按**当前表内字体**量一遍。
        #: [为什么必须量内容] 只按**表头**定下限 ⇒ 表头短、内容长的列会被
        #: 截断：历史页「模式」列表头只有 2 个字（"模式"），内容却是
        #: "自动·产水"（5 个字）⇒ 用户截图里整列显示成 "自动…"（列宽
        #: 被压到下限，内容放不下 ⇒ Qt 用省略号截断）。
        #: ⇒ 下限取 max(表头宽, 最长内容宽) + 内边距。
        self._content_text = {}
        #: 表内字号的**自适应缩放比**（1.0 = 与界面同字号）。
        #: 历史页 16 列的下限总和常常比视口宽 ⇒ 不缩就必然出现横向滚动，
        #: 右半截的列（水温/流量/压差/温湿度/DI/DO）**用户在表格底部才够
        #: 得到的滚动条上**才能看到 ⇒ 表现为"列显示不全"。
        #: ⇒ 这里按"装得下"自动缩一点（不低于 MIN_FIT_SCALE）。
        self._fit_scale = 1.0
        #: 是否允许**自动缩表内字号**来把全部列装进视口。
        #: 默认关（多数表列数少、装得下 ⇒ 不该动字号）；
        #: 历史页 16 列才打开（列多到装不下 ⇒ 缩一号全装下）。
        self._fit_enabled = False
        #: 列下限是否**按内容**给（而不是只按表头）。
        #: [为什么] 2026-11-17 用户「客户端表格美化成 web 端这种样子」：
        #: 网页端的历史表就是**列宽按内容**（内容长 ⇒ 列宽跟着长）⇒
        #: 装不下就**横向滚动**，字不截断。桌面端原来只按表头给下限 ⇒
        #: 内容被省略号截断（"列显示不全"）。⇒ 宽表改为按内容给下限。
        self._content_floors = False
        #: 视口宽无效时的重试计数（0=正常；>0 表示在排队等布局稳定）
        self._width_retry = 0
        #: 上次算列宽用的可用宽度（用于判断是否需要重算）
        self._width_basis = -1
        self.table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)
        # ------------------------------------------------------------------
        # [表内字号：显式像素 + 跟随全局缩放 2026-11-17 用户实锅]
        #   用户实报「模拟量页表格文字太小，没接入本地设置的全局缩放吗」。
        # [根因] `setFont(QFont())` **不是**"回到 app 字号"：给 QTableWidget
        #   设一个空 QFont，Qt 会把它解析成**视图自己的缺省 9pt**（实测：
        #   app 15pt 时表内仍是 9pt、宽 35px vs app 的 58px）⇒ 表内文字
        #   既没跟全局缩放、又比界面其它文字小一大截。
        #   而 QSS 里其余文字都是**像素**字号（font_px）⇒ 两套口径混用。
        # [修法] 表内与表头统一用**像素**字号，且取自 theme.font_px() ⇒
        #   与界面其它文字同一缩放口径、同一档大小。
        # ------------------------------------------------------------------
        self._base_px = theme.font_px(13)
        self._cur_px = self._base_px
        self._apply_table_font(self._base_px)
        # [被 re-parent 后表头字号会被打回 9pt —— 必须自己兜住]
        # 页面是**懒创建**的：DataTable 建好（字体已设好）→ 之后整页被
        # `stack.addWidget(page)` 塞进 QStackedWidget ⇒ Qt 重新解析整棵
        # 子树的字体 ⇒ **表头回落视图缺省 9pt**（表内因为有显式 setFont
        # 才保住）⇒ 表头比表内小一大截（用户实报「表格文字太小」）。
        # 实测：加进栈前表头 20px，加进栈后 9pt；大 QSS 也救不回来。
        # ⇒ 监听自己的 ParentChange，父一变就把字号再刷一遍。
        self.installEventFilter(self)
        # [表头 own-QSS 同款：进栈后要重刷 2026-11-17 用户「表格标题栏样式
        #  要对齐 web 端」] 实测（MainWindow + QStackedWidget 环境，像素
        # 取证）：刚 goto_page 时表头底色 = #EFF3F7 ✓（对）但**竖分隔线 =
        # Fusion 默认的 #505152 深灰**（网页端是 #E4E8EC 浅线）⇒ 表头
        # own-QSS 的 border-right 在**样式被重解析**那一刻丢了 ⇒ 手动
        # 重刷一次 `_apply_table_font` 立刻恢复 #E4E8EC。
        # ⇒ 监听**表头自己**的 ParentChange（表头被 reparent 才是触发点），
        # 一变就重刷（与字号那条同款处置）。
        self.table.horizontalHeader().installEventFilter(self)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.table)

    def set_headers(self, headers):
        """重设列标题（同时按标题个数调整列数）。

        [为什么必须一起改列数] `setHorizontalHeaderLabels` **不会**扩展
        columnCount：给它 4 个标签、表仍只有 3 列时，第 4 列不存在，
        界面上什么都看不到（主界面曾因此丢过「说明」列）。
        故这里显式 setColumnCount，再把列宽策略重设一遍。
        """
        headers = list(headers)
        if not headers:
            return
        from PyQt5.QtWidgets import QHeaderView
        self.table.setColumnCount(len(headers))
        self.table.setHorizontalHeaderLabels(headers)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.Stretch)
        header.setStretchLastSection(False)
        # [setColumnCount 会把表头字号打回视图缺省] QHeaderView 也是视图，
        # 重建列/标签后它的字体回到 9pt（实测）⇒ 表头比表内小一大截。
        # ⇒ 重建后立刻把表内那把像素字号再刷给表头。
        self._apply_table_font(self._base_px)

    def set_column_weights(self, weights, min_widths=None, auto_min_texts=None):
        """设置列宽比例（如 (6,3,2) = 项目 6 : 数值 3 : 单位 2）。

        :param min_widths: {列号: 最小像素} 按列下限（可选）。某列内容比
        全局最小档（72/150）更长时用它给硬保证 —— 历史页时间列
        （"yyyy-MM-dd HH:MM:SS" 19 字符 ≈ 130px）实锅：全局 72px 档把它
        压成 "202..."，日期看不见（2026-11-05 截图反馈）。
        :param auto_min_texts: {列号: 代表文本} —— **按当前字体实测**这些
        文本的宽度来定下限（未给某列就用该列表头文字）。**字号缩放后仍然
        对**，这是它存在的理由：硬编码像素只在量它的那个字号下成立。
        实锅 2026-11-10：客户端 15pt（ui_scale 1.5）下历史页 15 列里 12 列
        表头被截成"出频率(H"、"度1(NTU"，而同一份像素值在 10pt 下正好。
        分配口径：先按各列下限预留，剩余宽度再按权重分。

        [实现要点 —— 踩过三次坑后的定稿]
        1) 不能用「只设第 0 列 Stretch + 其余 ResizeToContents」：
           名称列会吃掉大半个屏（截图实证）。
        2) 不能只手工 setColumnWidth：set_rows 每 200ms 重建行，
           布局未稳定时视口宽为 0，列被压成 60px、第 4 列直接消失。
        3) 在 resizeEvent 里反复 setColumnWidth 会造成重绘循环。
        4) **[2026-11-05 第三次] 全部 Stretch 会让 Qt 把列宽"均分"，
           权重根本不生效**。主界面设备表 5 列实测各 208px —— 地址列
           （"COM17 · 115200 · 站 2" + DO 段）被压到截断成 "..."，
           而"设备/类型/状态"三列白占大片空白。

        定稿做法：**全 Fixed + 自己按权重算像素**，在 resizeEvent 里算，
        但只在「视口宽或权重真的变了」才动（避坑 2 与 3）。
        """
        self._column_weights = tuple(weights) or (1,)
        self._min_widths = dict(min_widths or {})
        self._auto_min_texts = dict(auto_min_texts or {})
        self._apply_min_widths()
        self._apply_column_widths()
        # 调用方通常在建页期（show 之前）设权重，那时视口宽还是 0；
        # 再排一次 0ms 重试，保证 show 之后一定有正确的列宽。
        from PyQt5.QtCore import QTimer
        QTimer.singleShot(0, self._retry_widths)

    def _apply_min_widths(self):
        """给表格一个合理的列最小宽度（Stretch 模式下防止列被压没）。

        [为什么只调 header 的最小列宽] QHeaderView 的 Stretch 模式会等比
        填满视口，但列可以被压到很窄。把「最小列宽」提到 72px 后，
        即使窗口很窄也不会出现「单位列看不见」。
        这里不按权重逐列设（Qt 没有 per-section minimum 的公共 API），
        权重仅记录在 _column_weights 供将来需要时使用。

        [为什么记成"基准值"而不是直接设] 全局最小列宽是**死像素**，而表内
        字号会按视口缩（历史页 16 列）⇒ 字号缩到 0.6 倍时 72px 的下限
        反而成了**绑定约束**（16×72 = 1152px，比全部文字还宽）⇒ 表怎么
        缩都装不下 ⇒ 故把基准值记下来，实际值按字号比例缩（见
        `_apply_column_widths`）。
        """
        base = 150 if max(self._column_weights or (1,)) >= 6 else 72
        self._min_section_base = base
        self.table.horizontalHeader().setMinimumSectionSize(base)

    def refresh_content_widths(self):
        """表格内容换过之后调用：**重测各列最长文字**并按新下限重排列宽。

        [为什么要有这个公开口子] 历史页/日志页/报警页是**自己往 item 里
        写文本**（不走 `set_rows`）⇒ 内容宽不会自动被量到 ⇒ 列下限仍按
        表头算 ⇒ 「模式」列（表头 2 字、内容"自动·产水" 5 字）被截成
        "自动…"（用户 2026-11-14 截图反馈）。
        """
        texts = self._measure_content_texts()
        if texts != getattr(self, "_content_text", None):
            self._content_text = texts
            self._apply_column_widths()
            return True
        return False

    def _measure_content_texts(self):
        """找一遍各列**最长单元格文字**（{列号: 文本}）。

        [为什么存文本而不是像素] 像素宽会随字号变（且**不是严格线性**：
        字体 hinting/取整 ⇒ 按比例换算会差几个像素 ⇒ 列还是可能被截断）。
        ⇒ 存**文本**，在算下限时用**当前实际字体**现量 ⇒ 精确。
        [为什么必须量] 列宽下限只按表头算 ⇒ 表头短、内容长的列会被截断
        （历史页「模式」列表头 2 字、内容"自动·产水"5 字 ⇒ 整列 "自动…"）。
        """
        fm = QFontMetrics(self._base_font())
        out = {}
        rows = min(self.table.rowCount(), self._MEASURE_ROWS)
        for c in range(self.table.columnCount()):
            best, bestw = "", 0
            for r in range(rows):
                it = self.table.item(r, c)
                if it is None:
                    continue
                t = it.text()
                w = fm.width(t)
                if w > bestw:
                    bestw, best = w, t
            if best:
                out[c] = best
        return out

    def _fit_font(self, scale=None):
        """表内目标字体（表头与单元格**同一把**）。

        :param scale: 字号缩放比（None = 当前比例）。

        [为什么单独造一把] 表头是 QTableView 的子控件，而 **app 级 QSS
        一重刷（换主题）就会把它的字号还原成 app 字号**（实测：表头
        15pt、单元格 11pt ⇒ 表头比内容还大，且表头宽度按 15pt 量 ⇒
        下限算大、列被压窄 ⇒ 内容反而被省略号截断）。
        ⇒ 宽度一律按**这把目标字体**量，不看控件当前实际的 font。
        """
        s = self._fit_scale if scale is None else float(scale)
        f = QFont(self.table.font())
        size_px = max(9, int(round(self._base_font_px() * s)))
        f.setPixelSize(size_px)
        return f

    def _base_font(self):
        """1.0 比例下的表内字体（= 界面正文字号）。"""
        f = QFont(self.table.font())
        f.setPixelSize(self._base_font_px())
        return f

    def _set_fit_scale(self, scale):
        """把表内字号缩到 `scale`（1.0 = 与界面同字号）；返回是否变了。

        [为什么缩表内字号而不是截表头] 历史上为了"16 列都看得见"试过
        把列宽按比例压下去 ⇒ 表头被截成"出频率(H"（用户反馈"列名显示不全"）。
        现在改成：**表头与内容都按内容保底**，装不下时**只缩表内字号**
        （数据表本来就该比正文小一号），缩到 MIN_FIT_SCALE 仍装不下才
        允许横向滚动。
        """
        scale = max(self.MIN_FIT_SCALE, min(1.0, float(scale)))
        if abs(scale - self._fit_scale) < 0.005:
            return False
        self._fit_scale = scale
        if scale >= 0.999:
            # [不能写 setFont(QFont())] 见 __init__ 的注释：空 QFont 给
            # QTableWidget 会被解析成**视图缺省 9pt**（不是 app 字号）⇒
            # 表内文字凭空小一档、且不跟全局缩放。⇒ 显式回到基准像素字号。
            self._apply_table_font(self._base_px)
            return True
        size_px = max(9, int(round(self._base_font_px() * scale)))
        # **表头与单元格必须同一个字号** ⇒ 两者都用**像素**字号显式设一次
        # （表头是 QTableView 的子控件，不显式设就会各用各的）。
        self._apply_table_font(size_px)
        return True

    def _apply_table_font(self, size_px):
        """表内与表头**同一把**像素字体 + 表头/行线样式（网页端同款）。

        [表头为什么必须 own-QSS —— 2026-11-17 用户实报「表格标题栏样式要
        对齐 web 端」，像素取证（别靠肉眼）]
            网页端 `.table th`：平涂底 #EFF3F7 + 粗体 + 右缘线 #E4E8EC +
            下边框 #E4E8EC（竖扫实测：y=124..165 恒 #EFF3F7、y=166 #E4E8EC）。
            桌面端（整页截图竖扫）：y=124..166 一条**渐变**（#FDFDFE →
            #E7E9EC）+ y=167 一条 **#ABAEB0 深灰线** ⇒ **Fusion 默认表头**，
            样式规则**根本没生效**。
        [机制：两头落空]
            · **祖先（页面大 QSS）那条** `QHeaderView::section` 规则**到不了**
              Card 内的 QHeaderView —— Card 自带样式表 ⇒ 级联被截断
              （AGENTS.md ① 已记的坑）；
            · **QTableWidget 自身**样式表里的 `QHeaderView::section` 规则又
              **不会**传给 QHeaderView（实测：独立 DataTable 里表头竖线仍是
              Fusion 默认的 #505152 深灰）。
        ⇒ 表头样式只能**直接挂在 QHeaderView 自己身上**（own-QSS）。
        [own-QSS 的代价：祖先规则不再级联到它] ⇒ 表头需要的属性必须**写全**，
        否则字号会回落视图缺省 9pt（表头比表内还小，AGENTS.md ③）：
          · 背景 #EFF3F7、文字 #1A1A1A、粗体、字号（像素，跟全局缩放）；
          · 右缘线 #E4E8EC（= 网页端 --header-line）、下边框 #E4E8EC；
          · 内边距 6px 8px（网页端 8px 8px，行高对齐后再收）。
        （逐条对齐网页端 theme.css 的 `.table th` / `.table td`。）
        """
        size_px = max(9, int(size_px))
        f = QFont(self.table.font())
        f.setPixelSize(size_px)
        self.table.setFont(f)
        head = self.table.horizontalHeader()
        head.setFont(f)
        self._cur_px = size_px
        self.table.setStyleSheet(
            "QTableWidget { font-size: %dpx; }\n"
            "QTableWidget::item { border-bottom: 1px solid %s;"
            " padding: 2px 8px; }"
            % (size_px, theme.border_soft_color()))
        # [表头 own-QSS —— 见方法头注的「两头落空」]
        head.setStyleSheet(
            "QHeaderView::section { font-size: %dpx;\n"
            "    background-color: %s;\n"
            "    color: %s;\n"
            "    font-weight: bold;\n"
            "    border-right: 1px solid %s;\n"
            "    border-bottom: 1px solid %s;\n"
            "    padding: 6px 8px; }"
            % (size_px,
               theme.header_bg_color(), theme.text_color(),
               theme.grid_line_color(), theme.border_soft_color()))
        # [字号变了 ⇒ 行高会变 ⇒ 内容高要重算] 行高是内容驱动的（字号放大
        # ⇒ 行被撑高）⇒ 不重算的话高度就少了 ⇒ 末行被裁（2026-11-17 实报）。
        self._schedule_height_refresh()

    def refresh_style(self):
        """把表内/表头样式**重刷一遍**（进栈 / 换主题之后由主窗调用）。

        [为什么需要这个公开口子] 页面被塞进 QStackedWidget（或换主题重新
        挂大 QSS）时，Qt 会**重新解析**整棵子树的样式 ⇒ 表头 own-QSS 里的
        `border-right` 被丢掉 ⇒ 竖分隔线回落 Fusion 默认的 #505152 深灰
        （网页端是 #E4E8EC）⇒ 标题栏比 web 端"脏"（底色还在，所以更隐蔽：
        肉眼看只是"线太深"）。⇒ 这时重刷一次 own-QSS 即可恢复（实测）。
        """
        self._apply_table_font(getattr(self, "_cur_px", self._base_px))

    def eventFilter(self, obj, event):
        """父变了 ⇒ 把表内/表头字号 + 表头 own-QSS 再刷一遍。

        [两个触发点]
          · **自己**的 ParentChange：页面被塞进 QStackedWidget ⇒ 字号被打回
            视图缺省 9pt（见 __init__ 的注释）；
          · **表头**的 ParentChange：同样那次"样式重解析"会把表头 own-QSS
            的 border-right 丢掉 ⇒ 竖分隔线回落 Fusion 默认的 #505152 深灰
            （网页端是 #E4E8EC）⇒ 标题栏看着"比 web 端脏"。
        """
        from PyQt5.QtCore import QEvent
        if event.type() == QEvent.ParentChange:
            if obj is self or obj is self.table.horizontalHeader():
                self._apply_table_font(getattr(self, "_cur_px", self._base_px))
        return QWidget.eventFilter(self, obj, event)

    def _base_font_px(self):
        """表内基准字号（像素）—— 表内字号按它缩放。

        [为什么不再取 app 字体] app 字体是**点**字号（10pt×缩放），而 QSS
        里其余文字都是**像素**字号（font_px）⇒ 两套口径混用时，表内文字
        会比界面其它文字小一档（用户实报「表格文字太小」）。⇒ 表内统一走
        自己的像素基准（theme.font_px(13)，与界面同一缩放比例）。
        """
        return int(self._base_px)

    def _text_parts(self, n):
        """各列要量的两段文字（{列号: (表头文字, 最长内容文字)}）。"""
        auto = getattr(self, "_auto_min_texts", None)
        content = getattr(self, "_content_text", {}) or {}
        parts = {}
        for c in range(n):
            h_text = ""
            if auto:
                txt = auto.get(c)
                if txt is None:
                    item = self.table.horizontalHeaderItem(c)
                    txt = item.text() if item is not None else ""
                h_text = txt or ""
            parts[c] = (h_text, content.get(c, ""))
        return parts

    def _floors_at(self, parts, active, base_min, scale, with_content=True):
        """给定缩放比下的各列下限像素（**按该比例的实际字体精确量**）。

        下限 = max(全局最小列宽×比例, 调用方给的固定下限, 文字宽 + 内边距)。

        :param with_content: 是否把**单元格内容**算进下限。
            [为什么可以关掉] 内容下限是"别把内容截断"的**增强**，但它是
            **硬下限**：内容很长（首页设备表的地址列 "COM17 · 115200 ·
            站 2"）时下限总和会超过视口 ⇒ 全表被迫横向滚动、权重也失效
            （实测：设备列与状态列都被压成 150px）。⇒ 装不下时退回
            **只按表头**的旧口径（权重照旧生效），由调用方决定要不要
            为"不截断"付出横向滚动。
        """
        fixed = getattr(self, "_min_widths", {}) or {}
        fm = QFontMetrics(self._fit_font(scale))
        min_w = max(24, int(base_min * scale))
        out = {}
        for i in active:
            h_text, c_text = parts.get(i, ("", ""))
            need = fm.width(h_text) + self._HEADER_PAD if h_text else 0
            if with_content and c_text:
                need = max(need, fm.width(c_text) + self._CELL_PAD)
            out[i] = max(min_w, int(fixed.get(i, 0)), int(need))
        return out

    def _solve_fit_scale(self, parts, active, base_min, avail):
        """求「所有列都装得下」的最大缩放比（装不下就返回 MIN_FIT_SCALE）。

        [为什么二分] 下限(scale) 单调递增 ⇒ 二分 12 次即够精度；
        每次求和只有几十列 ⇒ 代价可忽略（只在重算列宽时调）。
        """
        def total(s):
            return sum(self._floors_at(parts, active, base_min, s).values())
        if total(1.0) <= avail:
            return 1.0
        lo, hi = self.MIN_FIT_SCALE, 1.0
        for _ in range(12):
            mid = (lo + hi) / 2.0
            if total(mid) <= avail:
                lo = mid
            else:
                hi = mid
        return lo

    def _plan_widths(self, avail, active, parts, base_min):
        """算「给定视口宽下」的 (缩放比, 各列下限) —— 真代码与回归锁共用。

        [为什么抽出来] 回归锁要验「1680px 视口下 16 列装得下且不被截断」，
        而测试窗口里的表格宽度由布局决定、撑不宽 ⇒ 直接调本函数验算
        （验的是**同一段逻辑**，不是测试自己重写一遍）。

        口径：
          ① 缩放比 = 「所有列都按内容口径装得下」的最大比例（二分）；
             装不下就退到 MIN_FIT_SCALE。
          ② 下限先按**表头**口径给，再把**能装下**的列逐个抬到内容口径
             （便宜的先抬）⇒ 「模式」列这种"表头短、内容长"的列不再被
             截断，而首页地址列那种长内容装不下时保持旧口径（权重照旧
             生效、不出横向滚动条）。
        """
        scale = self._solve_fit_scale(parts, active, base_min, avail)
        if not self._fit_enabled:
            scale = 1.0          # 不允许缩字号的表 ⇒ 保持界面同字号
        if self._content_floors:
            # [按内容给下限 2026-11-17 用户「表格美化成 web 端这种样子」]
            # 网页端的历史表列宽**跟着内容走** ⇒ 装不下就横向滚动（字不截断）。
            # 桌面端原来只按表头给下限 ⇒ 内容被省略号截断（"列显示不全"）。
            floors = self._floors_at(parts, active, base_min, scale,
                                     with_content=True)
            return scale, floors
        floors = self._floors_at(parts, active, base_min, scale,
                                 with_content=False)
        fm = QFontMetrics(self._fit_font(scale))
        raise_list = []
        for i in active:
            _h_text, c_text = parts.get(i, ("", ""))
            if not c_text:
                continue
            need = fm.width(c_text) + self._CELL_PAD
            if need > floors[i]:
                raise_list.append((need - floors[i], i, need))
        raise_list.sort()
        budget = avail - sum(floors.values())
        for cost, i, need in raise_list:
            if cost <= budget:
                floors[i] = need
                budget -= cost
        return scale, floors

    def _apply_column_widths(self):
        """按 `_column_weights` 把视口宽度分给各列（全 Fixed 模式）。

        [为什么要"记住上次宽度"再比] set_rows 每 200ms 重建行、resizeEvent
        也会频繁触发。无条件 setColumnWidth 会：
          · 在视口宽还是 0 的布局初期把列压成 0（第 4 列直接看不见）；
          · 触发重绘 → 又进 resizeEvent → 反复刷（坑 3）。
        故这里只算一次目标宽度，**和当前实际值不同才写**。
        """
        header = self.table.horizontalHeader()
        n = self.table.columnCount()
        weights = list(self._column_weights or ())
        if n <= 0 or not weights:
            return
        # 权重个数与列数不齐时补齐/截断（调用方改了列数但忘了改权重）
        if len(weights) < n:
            weights = weights + [weights[-1]] * (n - len(weights))
        weights = weights[:n]
        # [隐藏列不参与分配] 只对**可见列**算像素：隐藏列仍占 columnCount
        # 的位置，把它们也算进"列数"会让可见列被按小权重分到过窄的宽度，
        # 同时腾出来的空白留在最右边（DataTable.set_visible_columns）。
        visible = getattr(self, "_visible_cols", None)
        if visible is None:
            visible = set(range(n))
        active = [i for i in range(n) if i in visible]
        if not active:
            return
        total_w = sum(weights[i] for i in active)
        if total_w <= 0:
            return

        viewport = self.table.viewport().width()
        # 减去竖滚动条与边框，避免最后一列被挤出一条横向滚动条
        avail = viewport - 2
        # [不能用 avail 缓存做"提前返回"] 曾按"上次 avail 相同就跳过"优化，
        # 结果列宽停在下限 150px：缓存命中时跳过，而列宽可能已被别处
        # （sectionResized / 重建列）改过，于是永远不再修正。
        # 现在直接按当前 avail 算目标值，末尾用 `sectionSize(i) != px`
        # 兜住"已正确就不写"，同样不会造成重绘循环。
        self._width_basis = avail
        if avail <= 0:
            # [布局未稳定：这一拍设不了，要**重新排队等下一轮**]
            # 只排一次不够：建页期（show 之前）视口宽是 0，那时排的 0ms 重试
            # 往往仍在 show 之前执行，同样算不出宽度；之后若窗口尺寸不再变化
            # 就不会再有 resizeEvent ⇒ 列宽永远停在下限 150px（实测）。
            # 故用「有限次重排」：每拍重新入队，直到视口宽有效或次数用尽，
            # 次数上限防止在极端情况下空转。
            if self._width_retry < 10:
                self._width_retry += 1
                from PyQt5.QtCore import QTimer
                QTimer.singleShot(0, self._retry_widths)
            return
        self._width_retry = 0

        from PyQt5.QtWidgets import QHeaderView
        header.setSectionResizeMode(QHeaderView.Fixed)

        # [最小列宽是硬约束，必须参与分配] QHeaderView 会**强制**每列不小于
        # minimumSectionSize。若只按权重算、不管这个下限，总和会超出视口
        # （实测 5 列权重算下来 1130 > 视口 1038）⇒ 出现横向滚动条，
        # 地址列看着还是被挤。故：先把下限"预留"出来，剩余宽度再按权重分。
        # [全局最小列宽随字号缩] 它是**死像素** ⇒ 字号缩到 0.6 倍时
        # 72px 变成绑定约束（16×72=1152，比全部文字还宽）⇒ 表缩不动 ⇒
        # 故按比例缩（基准值见 _apply_min_widths）。
        base_min = getattr(self, "_min_section_base", None) \
            or (header.minimumSectionSize() or 56)
        # [装不下 ⇒ 先缩表内字号] 16 列的下限总和常常 > 视口 ⇒ 不缩就必然
        # 横向滚动 ⇒ 右半截的列在"表格最底下的滚动条"上才够得到 ⇒ 用户看到
        # 的就是"列显示不全"。这里**解析求解**「装得下的最大缩放比」
        # （文字宽与全局下限都随字号线性 ⇒ 二分即可）⇒ 大多数情况下
        # **所有列一次装下**，表内字号只比正文小一号。
        parts = self._text_parts(n)
        scale, floors = self._plan_widths(avail, active, parts, base_min)
        self._set_fit_scale(scale)
        header.setMinimumSectionSize(max(24, int(base_min * self._fit_scale)))
        spare = max(0, avail - sum(floors.values()))   # 下限之外可自由分配
        targets = {}
        used = 0
        for i in active[:-1]:
            px = floors[i] + int(spare * weights[i] / total_w)
            targets[i] = px
            used += px
        last = active[-1]
        targets[last] = max(floors[last], avail - used)   # 最后一列吃掉余数
        for i in active:
            if header.sectionSize(i) != targets[i]:
                header.resizeSection(i, targets[i])

    def showEvent(self, event):
        """显示时按最终视口宽重算列宽。

        [为什么需要] 建页期（hide 状态）视口宽可能是个很小的过渡值
        （实测 96px）—— 那时算出的列宽全部撞在下限 150px 上。之后切页显示
        出来，若窗口尺寸不再变化就不会再进 resizeEvent，列宽一直停在
        5×150=750（视口 1038，右边留一大片空白 + 地址列被截断）。
        showEvent 是"终于有了真实尺寸"的那一拍，在这里补算最可靠。
        """
        QWidget.showEvent(self, event)
        self._apply_column_widths()

    def set_fit_columns(self, enabled=True):
        """是否允许**自动缩表内字号**把全部列装进视口（默认关）。

        [为什么做成按表开关] 缩字号是"表格列太多、视口装不下"时的兜底：
        历史页 16 列需要（不然右半截的列在表格底部的滚动条上才够得到），
        而列少的表（首页设备表 5 列、日志页 3 列）本来装得下 ⇒ 不该被动
        变小。⇒ 由页面显式开。
        """
        self._fit_enabled = bool(enabled)
        self._apply_column_widths()

    def set_content_floors(self, enabled=True):
        """列下限是否**按内容**给（宽表用；照网页端口径）。

        [为什么] 2026-11-17 用户「客户端表格美化成 web 端这种样子」：
        网页端历史表的列宽跟着内容走（内容长 ⇒ 列宽跟着长）⇒ 装不下就
        **横向滚动**，字不截断。桌面端原来只按表头给下限 ⇒ 内容被省略号
        截断（"列显示不全"）。⇒ 宽表开这个开关。
        """
        self._content_floors = bool(enabled)
        self._apply_column_widths()

    def set_visible_columns(self, visible):
        """只显示给定的列（`visible` 是列下标集合；None/空 = 全部显示）。

        [为什么用 setColumnHidden 而不是重建列] 隐藏列的数据仍在 item 里，
        重新勾选时**不用重取数据**、调用方的行文本也不用跟着改列序
        （历史页的行是按固定 15 列存着的，只有显示面变）。重建列会让
        "勾选列"变成"改数据结构"，一处改动要牵到取数、导出、图表几处。

        [列宽必须跟着重算] `_apply_column_widths` 按**总列数**和视口宽
        分配像素，隐藏列也占着 columnCount 的位置 ⇒ 不处理的话空出来的
        宽度全留白（右边一大片空白，剩下的列并不变宽）。这里把
        `_visible_cols` 交给分配器：只对可见列分配，权重也只用可见列的。
        """
        cols = set(range(self.table.columnCount())) if not visible \
            else set(int(c) for c in visible)
        self._visible_cols = cols
        for c in range(self.table.columnCount()):
            self.table.setColumnHidden(c, c not in cols)
        self._apply_column_widths()

    def _retry_widths(self):
        # 不复位计数：_apply_column_widths 成功后会自己清零
        self._apply_column_widths()

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._apply_column_widths()
        # [布局跑完 ⇒ 内容高才是最终值] 表头高度（页面 QSS 建页后才挂）与
        # 横向滚动条是否出现，都要等这一拍才准 ⇒ 延后一拍重算高度下限。
        self._schedule_height_refresh()

    def set_expandable(self, expandable):
        """表格是否随容器拉伸（折叠工具条时用）。

        [为什么必须**还原原始策略** 2026-11-19 用户实报「多点几次展开收起
        筛选后中间就空了一大块」] 历史/日志/报警页的表**原本就是
        Expanding**（撑满视图区、行数够就多显示）⇒ 折叠时其实**不用**改
        策略（工具条藏起来 ⇒ 视图区变大 ⇒ 表跟着长高）⇒ 真正要保证的是
        **展开时把策略原样还原**：若简单地「关掉就设 Fixed」，表格会缩回
        内容高（192px）⇒ 表格上方留一大块空白（就是用户看到的那个洞）。
        ⇒ 第一次切到 Expanding 前把原策略存下来，关闭时原样还原。
        """
        sp = self.table.sizePolicy()
        sp_outer = self.sizePolicy()
        if expandable:
            if getattr(self, "_saved_vpolicy", None) is None:
                self._saved_vpolicy = (sp.verticalPolicy(),
                                       sp_outer.verticalPolicy())
            sp.setVerticalPolicy(QSizePolicy.Expanding)
            sp_outer.setVerticalPolicy(QSizePolicy.Expanding)
        else:
            _saved = getattr(self, "_saved_vpolicy", None)
            if _saved is not None:
                sp.setVerticalPolicy(_saved[0])
                sp_outer.setVerticalPolicy(_saved[1])
            else:
                sp.setVerticalPolicy(QSizePolicy.Fixed)
                sp_outer.setVerticalPolicy(QSizePolicy.Fixed)
        self.table.setSizePolicy(sp)
        self.setSizePolicy(sp_outer)

    def fit_to_content(self, extra_rows=0):
        """让表格高度贴合内容（不留大片空白），**但不把高度钉死**。

        [为什么改 2026-11-17] 用户实报「还有不要固定模拟量表格的高度」，
        现场现象：模拟量页表格**右侧出现竖向滚动条**、末行「通道 7」被裁掉一半。

        [原实现的毛病] `setMinimumHeight(total)` + `setMaximumHeight(total)`
        把高度**钉死**成「表头 + 缺省行高×行数」：
          ① 表内字号放大后**实际行高**变大（行高是内容驱动的：实测 20px 字
             ⇒ 行高 32px；26px 字 ⇒ 行高 39px）⇒ 钉死的高度比真实内容矮
             ⇒ 表格内部溢出 ⇒ 竖向滚动条 + 末行被裁；
          ② 窗口变窄时表内多出一条**横向**滚动条（占掉一段视口高）⇒ 竖向
             必然溢出（实测：1280 宽正常、820 宽就复现）。
        ⇒ 高度钉死 = 一有"内容比预估高"就裁行。

        [新口径 —— 高度交给布局]
          · `sizeHint` 高度改成**实际内容高**（表头 + 各行实际 sectionSize
            + 横向滚动条高 + 边框）⇒ 父布局按内容给高度；
          · 纵向 sizePolicy = Fixed ⇒ 父布局**不拉伸**它 ⇒ 表下方不留大片
            空白（fit_to_content 当初的本意）；
          · 只保留 `setMinimumHeight` 当**下限**（随行数/字号自动重算），
            上限放开 ⇒ 高度随内容长，**永远不裁行**。
        """
        self.table.set_content_sized(True, extra_rows)
        # [纵向固定] 父布局按 sizeHint（= 内容高）给高度，不拉伸 ⇒ 不留空白。
        sp = self.table.sizePolicy()
        sp.setVerticalPolicy(QSizePolicy.Fixed)
        self.table.setSizePolicy(sp)
        sp_outer = self.sizePolicy()
        sp_outer.setVerticalPolicy(QSizePolicy.Fixed)
        self.setSizePolicy(sp_outer)
        self._refresh_content_height()
        # [改高度后必须重算列宽] 高度变了会改变表格几何，但不一定触发本控件
        # 的 resizeEvent（尤其是"切页回来"这条路径）⇒ 列宽可能停在旧值/
        # 下限上。这里补一次；_apply_column_widths 内部有"值没变就不写"的
        # 短路，不会造成重绘循环。
        self._apply_column_widths()

    def _refresh_content_height(self):
        """按**实际内容高**刷新表格的高度下限（只下限、不上限）。

        [为什么留"下限"] ① 有些调用方直接读 `table.minimumHeight()` 当
        "内容多高"（首页悬浮设备卡就是，`_apply_detail_geometry`）；
        ② sizeHint 失准时兜底 —— 宁可略高一点，也不要裁行。

        [为什么必须放开上限] 钉死的 max 让高度**永远不变** ⇒ 字号变大 /
        行数变多时高度跟不上 ⇒ 裁行（就是这次要修的毛病）。
        """
        table = self.table
        if not getattr(table, "_content_sized", False):
            return              # 没开"贴合内容"的表（历史/日志/报警）不受影响
        h = table.content_height()
        if h <= 0:
            return
        changed = False
        if table.minimumHeight() != h:
            table.setMinimumHeight(h)
            changed = True
        if table.maximumHeight() != QWIDGETSIZE_MAX:
            table.setMaximumHeight(QWIDGETSIZE_MAX)
            changed = True
        # [必须通知布局] 高度下限变了 ⇒ 父布局要按新的 sizeHint 重排一次。
        # [只在真变了才通知] set_rows 每 200ms 调一次 ⇒ 无条件 updateGeometry
        # 会让布局每拍重跑（白白抖动）。
        if changed:
            table.updateGeometry()

    def _schedule_height_refresh(self):
        """延后一拍再重算内容高（等布局与滚动条状态落定）。

        [为什么延后] 表头的最终高度、行高、横向滚动条是否出现，都要等
        布局跑完才知道（页面 QSS 是建页之后才挂的 ⇒ 表头高度会变）。
        立即算会拿到"半成品"的值 ⇒ 表格高度差几像素 ⇒ 末行被裁/多出空白。
        """
        from PyQt5.QtCore import QTimer
        QTimer.singleShot(0, self._refresh_content_height)

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
        # [内容宽度变化 ⇒ 列下限要跟着变] 换数据后重新量一遍各列最长文字，
        # 只有量出来的宽度真的变了才重算列宽（set_rows 每次刷新都调，
        # 无条件重算会引发反复重排）。
        self.refresh_content_widths()
        # [行数变了 ⇒ 内容高变了] 开了"高度贴合内容"的表要重算高度下限
        # （历史/日志/报警页没开 ⇒ 这里直接返回，不影响它们）。
        self._refresh_content_height()


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


def _attach_scroller(area):
    """给视口挂一个**触摸拖动 + 惯性**滚动器（自己实现；PyQt5 没有 QScroller）。

    [为什么] 板端是触摸屏；长窗口（竖屏 800×1280）里内容超出一屏 ⇒ 要能
    **用手指上下拖**来滚。`QScrollArea` 原生支持触摸拖动（能滚），但**没有
    惯性**（松手即停，滑动发涩）⇒ 这里补一段惯性。

    [为什么不用 QScroller] PyQt5 **不导出** `QScroller.create()`，而且
    `QScroller` 连直接实例化都不行（实测 "cannot be instantiated or
    sub-classed"）⇒ 只能自己实现。

    [返回值] FlickScroller（挂不上时返回 None ⇒ 页面照常能用滚轮）。
    """
    try:
        from .flick import FlickScroller
    except Exception:                               # noqa: BLE001
        return None
    try:
        return FlickScroller(area)
    except Exception:                               # noqa: BLE001
        return None


def elide_text(widget, text, mode=None):
    """按控件当前宽度把文字**省略号截断**（QLabel 不自带 elide）。

    [为什么] 卡片里固定一行高度的说明文字（阀卡的提示行）遇到长文案会被
    **直接裁掉**（实测「点位未配置（服务端 config 的 valves.items）」要
    640px，卡片只有 265px ⇒ 后半截看不见）⇒ 按宽度省略，全文挂 tooltip。

    [宽度还没量出来时] 返回原文（宁可被裁也不要空着——空着会被误读成
    "没有提示"）。
    """
    if not text:
        return ""
    try:
        from PyQt5.QtCore import Qt
        from PyQt5.QtGui import QFontMetrics
        mode = mode if mode is not None else Qt.ElideRight
        w = widget.width()
        if w <= 4:
            return text
        return QFontMetrics(widget.font()).elidedText(text, mode, w - 4)
    except Exception:                               # noqa: BLE001
        return text


def fit_text_width(widget, texts, extra=0):
    """按控件**实际绘制用的字体**量出「最宽候选文字」的宽度（尺寸铁律）。

    :param widget: 参照控件（量它的**生效字体**，不是 app 缺省字体）。
    :param texts: 候选文字（单个 str 或可迭代）——**取最宽的那个**。
    :param extra: 额外留白（内边距/边框/呼吸余量）。

    [为什么不能直接量 widget.font()] 控件的字号可能来自**样式表**
    （控件自己的 QSS 或窗口大 QSS）⇒ 样式表还没生效（控件尚未 polish）
    时 ``widget.font()`` 仍是继承来的缺省字号 ⇒ 量出来的宽度偏小 ⇒
    按它定宽就必然裁字（实测阀卡标题 22px 粗体：按缺省 12pt 量得 85px，
    实际要 110px）。⇒ 先 ``ensurePolished()`` 再量。

    [为什么要「最宽候选」] 同一个框里可能出现好几种文字（回填值、
    占位符提示、用户输入）⇒ 只有按**最宽的那个**定底宽，才任何时候都
    装得下（只按其中一个样例量 ⇒ 另一个被裁）。
    """
    if isinstance(texts, str):
        texts = (texts,)
    try:
        widget.ensurePolished()
    except Exception:                                   # noqa: BLE001
        pass
    try:
        fm = QFontMetrics(widget.font())
    except Exception:                                   # noqa: BLE001
        return 0
    best = 0
    for t in texts or ():
        if not t:
            continue
        try:
            best = max(best, int(fm.horizontalAdvance(str(t))))
        except Exception:                               # noqa: BLE001
            continue
    return best + int(extra)


def fit_input_width(edit, extra=18, minimum=40):
    """按**字体实测**给输入框底宽（占位符与已填内容取最宽）——不裁字。

    :param edit: QLineEdit（QLineEdit / QSpinBox 的内框都行）。
    :param extra: 左右留白（QLineEdit 的 QSS：padding 8×2 + border 1×2）。
    :param minimum: 兜底下限（太小的框连一个字都放不下）。

    [为什么] 输入框的占位符是**提示文字**（"相机 IP，如 192.168.1.181"），
    框太窄时 Qt 会用省略号把它截掉（"相机 IP，如 192.168…"）⇒ 用户
    看不出该填什么。⇒ 底宽按「占位符 / 已填内容里最宽的那个」实测给出，
    **不钉死**（宽度仍由布局决定，多余空间给出去）。

    [为什么要留白] 占位符画在框的**内边距里**（QLineEdit 的 QSS padding
    8px + 边框 1px）⇒ 只按文字宽给框 ⇒ 文字右边还是会被切掉几个像素。
    """
    texts = [edit.placeholderText() or "", edit.text() or ""]
    w = fit_text_width(edit, texts, extra=extra)
    try:
        edit.setMinimumWidth(max(int(minimum), w))
    except Exception:                                   # noqa: BLE001
        pass
    return w


def adaptive_columns(widget, max_columns, cell_min_w):
    """按控件当前宽度算**能放下几列**（至少 1，至多 max_columns）。

    [为什么要这个] 数字量页的灯阵/按钮阵原来固定 8 列：窗宽不够时每格被压
    窄，第二行的语义名（"电动球阀1 控制"）被裁掉——用户截图反馈「显示不全」。
    格子数不变，只改每行放几个：宽窗 8 列（与 C 端一致），窄窗自动折成
    6/4/2 列，宁可多几行也不裁字。
    """
    try:
        w = int(widget.width())
    except Exception:                                   # noqa: BLE001
        return max_columns
    if w <= 0:
        return max_columns
    return max(1, min(int(max_columns), w // max(1, int(cell_min_w))))


class WrapRow(QWidget):
    """一行里放若干控件；**宽度不够就整整齐齐换行**（绝不裁字）。

    [为什么要有] 固定一行的 `QHBoxLayout` 在窄窗下会把控件**压扁裁字**：
    远端设置「阀复位」两个复选框在 700px 窗下第二个被卡片右边界裁掉、
    「换相策略」两个下拉在 900px 窗下整个溢出卡片 ⇒ 用户看到的是
    「代码里同一行、实际没排在一行/被切掉」。

    [口径] 与 `adaptive_columns` 同款纪律：**宁可多几行也不裁字**。
    能一行就一行；一行放不下 ⇒ 折成多行（每行仍从左起对齐）。

    [实现] 用 `QGridLayout` 放，按**当前字体的 sizeHint** 逐列试排：
    列数从 n 往下减，减到「这一行的控件宽度之和 + 间距 ≤ 可用宽」为止。
    宽度按控件自己的 `sizeHint()` 量（文字越长越宽 ⇒ 自动换行），
    不写死像素 ⇒ 字号缩放后依然成立。

    [两条补强 2026-11-20 文字硬裁专项]
      ① 试排用的宽度取 `sizeHint` 与 `minimumSizeHint` 的**较大者**
         （输入框按字体实测的底宽在 minimumSizeHint 里）。
      ② 控件**自己变宽**（标签换了更长的文字）时**重新折行**
         （监听 LayoutRequest）——否则那一行按旧宽度排不下 ⇒ 控件被挤
         到小于最小宽 ⇒ 文字被硬裁。
    """

    def __init__(self, widgets, spacing=8, max_columns=None, parent=None):
        QWidget.__init__(self, parent)
        self._items = [w for w in widgets if w is not None]
        #: 每行最多几个（None = 尽量排一行；给 3 就是「宽窗 3 个一行、
        #: 窄窗自动折成 2/1 个一行」）。
        self._max_columns = max_columns
        self._lay = QGridLayout(self)
        self._lay.setContentsMargins(0, 0, 0, 0)
        self._lay.setHorizontalSpacing(spacing)
        self._lay.setVerticalSpacing(spacing)
        self._cols = 0
        self._relayout_pending = False
        # [控件尺寸变了要重新折行] 见 eventFilter 的说明。
        for w in self._items:
            w.installEventFilter(self)
        self._relayout()

    # -- 布局 --
    def _item_width(self, w):
        """控件在行里**真正需要**的宽（sizeHint 与 minimumSizeHint 取大）。

        [为什么取大] sizeHint 是「理想宽」，minimumSizeHint 是「不许更窄」
        ⇒ 真正要装下它就得按**两者里大的那个**预留。只看 sizeHint 的话，
        一个 minimumWidth 很大的控件（输入框按字体实测的底宽）会被折行
        逻辑算成「窄的」⇒ 排进同一行后被挤扁 ⇒ 文字被裁。
        """
        hint = 0
        mini = 0
        try:
            hint = int(w.sizeHint().width())
        except Exception:                               # noqa: BLE001
            hint = 0
        try:
            mini = int(w.minimumSizeHint().width())
        except Exception:                               # noqa: BLE001
            mini = 0
        return max(1, hint, mini if mini > 0 else 0)

    def _relayout(self):
        n = len(self._items)
        if n == 0:
            return
        widths = [self._item_width(w) for w in self._items]
        gap = self._lay.spacing()
        avail = max(1, self.width())
        cols = n
        if self._max_columns:
            cols = min(n, int(self._max_columns))
        while cols > 1:
            if self._row_need(widths, cols, gap) <= avail:
                break
            cols -= 1
        if cols == self._cols:
            return
        self._cols = cols
        # [先摘再挂] 直接再 addWidget 会在布局里留下**同一个控件的两份
        # item**（旧位置 + 新位置）⇒ 列宽按重复项算、几何取最后一份 ⇒
        # 折行后列数/间距都不对。⇒ 先 removeWidget 再 addWidget。
        for w in self._items:
            self._lay.removeWidget(w)
        for i, w in enumerate(self._items):
            r, c = divmod(i, cols)
            self._lay.addWidget(w, r, c)
        # 行尾留空（控件贴左，不被拉伸）。
        for c in range(cols):
            self._lay.setColumnStretch(c, 0)
        self._lay.setColumnStretch(cols, 1)

    @staticmethod
    def _row_need(widths, cols, gap):
        """排成 ``cols`` 列时**真正需要**的宽（按列取最大值求和）。

        [为什么要按列取最大] 网格里第 i 个控件落在 (i//cols, i%cols) ⇒
        某一列的宽 = 该列所有控件里**最宽**的那个 ⇒ 只按「前 cols 个控件
        求和」算会**低估**（实测分页行：总数标签排在第二行第一列，却把
        第一行那一列撑宽 ⇒ 整行放不下 ⇒ 标签被挤到小于最小宽 ⇒ 文字被
        硬裁）。⇒ 按列最大值求和才是网格的真实需求宽。
        """
        col_w = [0] * cols
        for i, w in enumerate(widths):
            c = i % cols
            if w > col_w[c]:
                col_w[c] = w
        return sum(col_w) + gap * (cols - 1)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._relayout()

    def eventFilter(self, obj, event):
        """控件**尺寸变了**（例如状态标签换了一串更长的文字）⇒ 重新折行。

        [为什么必须监听] 折行只在 resizeEvent 里算 ⇒ 控件自己变宽
        （QLabel 改文字 ⇒ LayoutRequest）时不会重算 ⇒ 那一行按旧宽度
        排不下 ⇒ 布局把控件挤到小于它的最小宽 ⇒ **文字被硬裁**
        （实测历史页「服务端不可用（未连接服务端）」被裁成
        「服务端不可用（未连接…）」）。⇒ 监听 LayoutRequest 重排。

        [为什么延后一拍] LayoutRequest 是在布局过程中投递的 ⇒ 在事件
        里直接改布局会重入（Qt 会打 "QGridLayout::addWidget" 警告）。
        ⇒ 排到下一轮事件循环再重排（视觉上只晚一帧）。
        """
        from PyQt5.QtCore import QEvent
        if event.type() == QEvent.LayoutRequest:
            self._schedule_relayout()
        return QWidget.eventFilter(self, obj, event)

    def _schedule_relayout(self):
        """排一次重排（同一轮里只排一次 ⇒ 防抖）。"""
        if self._relayout_pending:
            return
        self._relayout_pending = True
        from PyQt5.QtCore import QTimer
        QTimer.singleShot(0, self._do_relayout)

    def _do_relayout(self):
        self._relayout_pending = False
        try:
            self._relayout()
        except RuntimeError:
            # 控件/布局已被销毁（窗口关了、页面被换掉）⇒ 不用再排。
            pass

    def minimumSizeHint(self):
        """最小宽 = **单个**控件的最小宽（**不是整行之和**）。

        [为什么必须覆盖] `QGridLayout` 的 minimumSizeHint 是**按当前列数
        求和**的 ⇒ 两列时最小宽 = 两个控件之和 ⇒ 页面内容最小宽被这一行
        撑大 ⇒ **超过视口 ⇒ 页面出现横向滚动条** ⇒ 右边被裁 ⇒ 用户看到
        「代码里同一行、实际没排在一行/被切掉」（实测 1.5x 缩放下远端设置
        页就是这样）。⇒ 最小宽按**一列**算 ⇒ 容器能被压窄 ⇒ 压窄后
        `resizeEvent` 里 `_relayout()` 自动折成多行 ⇒ **既不横向滚动，
        也不裁字**。
        """
        if not self._items:
            return QSize(0, 0)
        w = 0
        h_one = 0
        for it in self._items:
            mh = it.minimumSizeHint()
            w = max(w, mh.width())
            h_one = max(h_one, mh.height())
        cols = max(1, self._cols)
        rows = (len(self._items) + cols - 1) // cols
        return QSize(w, h_one * rows)

    def items(self):
        """行内的控件（回归锁用来断言「不裁字」）。"""
        return list(self._items)


def wrap_row(widgets, spacing=8, max_columns=None):
    """建一个**会换行**的行容器（见 :class:`WrapRow`）。"""
    return WrapRow(widgets, spacing=spacing, max_columns=max_columns)


def label_group(text, widget, spacing=6):
    """「标签 + 控件」的小组（组内**不拆散**）。

    [为什么要有] 换行容器折行时按**组**折 ⇒ 「写满后」标签不会和它的
    下拉被拆到两行（拆开就看不出哪个标签管哪个控件了）。
    """
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(spacing)
    if text:
        lbl = QLabel(text)
        lbl.setStyleSheet(
            "QLabel { color: palette(text); background: transparent; }")
        lay.addWidget(lbl)
    lay.addWidget(widget)
    return box


class LampRow(QWidget):
    """一排状态灯（数字量监视用）：自绘圆点 + 通道号。

    C 端 HsfMonitorDigital 的「圆点网格看板」口径：每格一个圆点
    （绿=通，红=断），圆点下方是通道号。

    [点数由服务端决定] 构造时给一个**初值**，真正的点数在收到第一帧推送后
    调 set_count() 重建。不能写死 16：单模块 16 点、聚合后 32 点，写死会让
    上半区（16~31）永远看不到，而服务端明明在发。

    [列数随宽度自适应 2026-11-10] 原来固定 8 列：窗宽不足时每格被压窄，
    第二行的语义名（"电动球阀1 关到位"）被裁成"电动球阀1 关"——用户截图
    反馈「显示不全」。现在按可用宽度算列数（每格至少装得下最长语义名），
    窄窗自动折成 6/4/2 列。
    """

    #: 每格最小宽（像素；语义名一行放得下才不裁）。这是**下限**；
    #: 实际按当前字体实测的最长语义名算（见 _min_cell_w）。
    CELL_MIN_W = 108

    #: 网格列间距（px）：算每格宽度时要把间距让出来，否则最后一格被挤。
    SPACING = 8

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._cells = []
        #: 每格的语义名（服务端下发）；按索引存，量宽度时要用。
        self._names = {}
        self._max_columns = max(1, int(columns))
        self._columns = self._max_columns
        self._grid = QGridLayout(self)
        self._grid.setContentsMargins(0, 0, 0, 0)
        self._grid.setSpacing(self.SPACING)
        # [竖向不伸缩] 内容高度固定（行数 × 行高），不让布局把多余高度
        # 塞进来——否则格子下方会多出一条空白（用户截图的白带）。
        # 横向仍 Preferred：列数由宽度自适应决定。
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        self.set_count(count)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._adapt_columns()

    def _adapt_columns(self):
        """按当前宽度重排列数（只在列数真的变了时重排，避免抖动）。"""
        cols = adaptive_columns(self, self._max_columns, self._min_cell_w())
        if cols == self._columns:
            return
        self._columns = cols
        self._reflow()

    def _min_cell_w(self):
        """每格最小宽 = **实测**最长语义名宽 + 内边距（不写死像素）。

        [为什么必须实测] 语义名由服务端下发（"电动球阀1 关到位"），长度
        随现场接线变；以前这里写死 108px，窄一点的窗里每格被压到
        ~130px，装不下就**折行**（"电动球阀1 关" / "到位"）——名字被拆成
        两半，现场看着就是"文字显示不全"。现在按当前字体量实际名字，
        宽度不够就少排一列（宁可多几行也不裁字、不折断）。

        [和 ToggleGrid 同款] DO 网格早就实测了，灯阵一直写死，这次补齐。
        """
        try:
            if not self._cells:
                return self.CELL_MIN_W
            fm = QFontMetrics(self._cells[0][2].font())   # 语义名标签的字体
            need = 0
            for text in self._names.values():
                if text:
                    need = max(need, fm.width(text))
            if need <= 0:
                return self.CELL_MIN_W
            # +12：QSS 左右 padding 余量；再让出列间距。
            return max(self.CELL_MIN_W, need + 12 + self.SPACING)
        except Exception:                                   # noqa: BLE001
            return self.CELL_MIN_W

    def _reflow(self):
        """把已有格子按当前列数重新摆进网格（不重建控件）。"""
        for cell in self._cells:
            self._grid.removeWidget(cell[3])          # cell[3] = 容器
        for i, cell in enumerate(self._cells):
            self._grid.addWidget(cell[3], i // self._columns,
                                 i % self._columns)

    def set_count(self, count):
        """按服务端下发的点数重建灯阵（幂等：数量未变直接返回）。"""
        count = max(0, int(count))
        if count == len(self._cells):
            return
        # 清空旧格子：把控件从布局里摘掉并销毁，避免残留占位。
        while self._grid.count():
            item = self._grid.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
                w.deleteLater()
        self._cells = []
        self._names = {}
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
            # 「CH<n>」常驻，语义名另起一行 —— 两者**都保留**：
            # 通道号是接线/排查口径（现场说"第 17 个点"），语义名是
            # 操作口径（"阀1 开到位"）。只留一个都不好用。
            name = QLabel("CH%d" % i)
            name.setAlignment(Qt.AlignCenter)
            name.setStyleSheet(theme.caption_label_qss())
            v.addWidget(name)
            alias = QLabel("")
            alias.setAlignment(Qt.AlignCenter)
            alias.setStyleSheet(theme.caption_label_qss())
            # [不折行] 语义名一行放下：宽度不够时网格**少排一列**
            # （_min_cell_w 实测），而不是把名字折成两行（"电动球阀1 关"
            # / "到位" —— 现场看着就是文字显示不全）。
            # 不折行还让标签高度恒为一行 ⇒ 卡片高度算得准（折行时
            # sizeHint 随标签当前宽度变，量出来会偏）。
            alias.setWordWrap(False)
            alias.setVisible(False)
            v.addWidget(alias)
            self._grid.addWidget(cell, i // self._columns,
                                 i % self._columns)
            self._cells.append((dot, name, alias, cell))

    def count(self):
        return len(self._cells)

    def set_state(self, index, on, enabled=True):
        if 0 <= index < len(self._cells):
            self._cells[index][0].set_state(on, enabled)

    def set_name(self, index, text):
        """设置语义名（服务端下发；空串=只显示 CH 号）。

        通道号那一行**不动**，语义名显示在它下面的第二行。
        """
        if not 0 <= index < len(self._cells):
            return
        alias = self._cells[index][2]
        text = (text or "").strip()
        if self._names.get(index, None) == text:
            return                       # 名字没变：不动控件（每拍都会来）
        alias.setText(text)
        alias.setVisible(bool(text))
        self._names[index] = text
        # 名字到达后才知道要占多宽 ⇒ 重排列数（不够宽就少一列，宁可多几行
        # 也不把"电动球阀1 关到位"挤成两行）。
        self._adapt_columns()

    def name_of(self, index):
        """当前语义名（诊断/测试用；未设置为空串）。"""
        if 0 <= index < len(self._cells):
            return self._cells[index][2].text()
        return ""


class ToggleGrid(QWidget):
    """DO 操作网格：每格一个可切按钮（点击下发控制）。

    与 C 端 HsfMonitorDigital 的操作区口径一致：DO 可点、DI 只读。

    [点数由服务端决定] 同 LampRow：set_count() 按需重建。
    """

    #: 按钮统一行高（**下限**）。实际行高由 row_height() 按当前字体实测
    #: —— 界面缩放 1.5× 时两行 18px 文字要 40px，写死 46 会把上下裁掉
    #: （用户截图「文字显示不全」）。
    ROW_HEIGHT = 46

    #: 每格最小宽（像素）。要装得下最长语义名一行（"电动球阀1 控制"）
    #: + 左右内边距；不够就折行（见 adaptive_columns）。
    CELL_MIN_W = 132

    #: 网格列间距（px）：算每格宽度时要把间距让出来，否则最后一格被挤。
    SPACING = 6

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._buttons = []
        #: 每路的语义名（服务端下发）；按索引存，set_count 后仍保留。
        self._aliases = {}
        self.on_toggle = None            # (index, want_on) -> None
        self._max_columns = max(1, int(columns))
        self._columns = self._max_columns
        self._grid = QGridLayout(self)
        self._grid.setContentsMargins(0, 0, 0, 0)
        self._grid.setSpacing(self.SPACING)
        # [控件级样式表 2026-11-17 用户实报「输出点开关的颜色区别太小」]
        # DO 按钮在**卡片**里，而卡片自带样式表 ⇒ 页面的大 QSS **不再级联**
        # 到卡片内的控件 ⇒ 「通」态拿不到 `QPushButton:checked` 的配色
        # （被画成灰面，与「断」几乎看不出差别；禁用时更是一片灰）。
        # ⇒ 网格自带一份 DO 按钮样式表（配色逐条对齐网页端 `.do-toggle`）。
        self.setStyleSheet(theme.do_toggle_qss())
        # [竖向不伸缩] 按钮行高固定、内容高度 = 行数 × 行高；不让布局把多余
        # 高度塞进来（否则卡片里会多出一条空白）。横向仍 Preferred：列数由
        # 宽度自适应决定。
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        self.set_count(count)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._adapt_columns()

    def row_height(self):
        """按钮行高 = **实测**两行文字行高 + 上下内边距 + 边框。

        [为什么不能写死] 按钮是两行文字（"DO7 断" + 语义名），高度需求随
        **界面缩放**走：缩放 1.5× 时字号 18px、两行行高实测 40px，加上下
        padding(4×2) 与边框(1×2) 需要 **50px**；原先写死 46（QSS 还额外
        钉了 max-height:46px）⇒ 内容区只剩 38px < 40px，**上下各裁一点**，
        用户截图反馈「文字显示不全」。现在按字体实测，缩放多少都放得下。

        内边距/边框数值取自 theme（与 QSS 同源），不另写死像素。
        """
        try:
            fm = QFontMetrics(self._buttons[0].font())
            text_h = fm.lineSpacing() * 2          # 两行文字
        except Exception:                           # noqa: BLE001
            text_h = self.ROW_HEIGHT
        return max(self.ROW_HEIGHT,
                   text_h + 2 * theme.DO_BTN_PAD_V + 2 * theme.DO_BTN_BORDER)

    def _min_cell_w(self):
        """每格最小宽 = **实测**最长文字宽 + 内边距（不写死像素）。

        [为什么实测] 字号随「界面缩放」变（10~15pt 常见），写死 132 在 15pt
        下差 1~2px 就裁字（实测 900 宽窗口 139 < 实际需要 140）。这里按当前
        字体量代表性语义名（"电动球阀0 控制"/"启动按钮灯"），再加 26px 给
        QSS 的左右 padding，得到真正放得下的格子宽。

        [为什么要把**实际下发**的语义名也量进来] 代表名只是样本：现场
        点位名可能更长（如"电动球阀1 关到位"），样本量短了就会裁字。
        名字是服务端下发的、到达时刻不确定，所以每收到新名字就重算列数
        （见 set_name）。
        """
        try:
            fm = QFontMetrics(self._buttons[0].font())
            need = max(fm.width("电动球阀0 控制"), fm.width("启动按钮灯"))
            for alias in self._aliases.values():
                if alias:
                    need = max(need, fm.width(alias))
        except Exception:                               # noqa: BLE001
            need = self.CELL_MIN_W
        # +26：QSS 左右 padding；再让出列间距，否则最后一格被挤窄。
        return max(self.CELL_MIN_W, need + 26 + self.SPACING)

    def set_count(self, count):
        """按服务端下发的点数重建按钮阵（幂等）。"""
        count = max(0, int(count))
        if count == len(self._buttons):
            return
        while self._grid.count():
            item = self._grid.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
                w.deleteLater()
        self._buttons = []
        self._states = {}
        # 丢掉超出新点数的旧名字（否则量宽度时把已经不存在的口的
        # 长名字也算进去 ⇒ 白白少排一列）。
        for key in [k for k in self._aliases if k >= count]:
            del self._aliases[key]
        for i in range(count):
            btn = QPushButton()
            btn.setCheckable(True)
            # [用 objectName 单独一套 QSS] QPushButton 基础规则有
            # `padding: 6px 14px` + `min-height: 18px`，样式表算出的
            # sizeHint 会覆盖 Qt 的固定高度：实测单行按钮 sizeHint=33、
            # 两行=51，而 setFixedHeight(46) 只把 maximum 设成 46 ——
            # 布局仍按各自 sizeHint 排版，同一行里 46 和 33 参差。
            # 用 objectName 走独立规则：内边距收小，行高由 row_height()
            # 按当前字体实测后 setFixedHeight（缩放多少都放得下两行字）。
            btn.setObjectName("HsfDoToggle")
            btn.setMinimumWidth(72)
            btn.setCursor(Qt.PointingHandCursor)
            btn.clicked.connect(
                lambda checked=False, idx=i: self._on_click(idx, checked))
            self._grid.addWidget(btn, i // self._columns, i % self._columns)
            self._buttons.append(btn)
            self._render(i)

    def count(self):
        return len(self._buttons)

    def _render(self, index):
        """刷新某按钮的文字：「DO<n> <通|断>」，有语义名再跟一行。

        [通道号必须保留] "DO16"是接线口径（现场说"第 17 个点"），语义名
        是操作口径（"阀1 控制"）。两个都要，故拼在一起而不是替换。

        [高度必须在 setText 之后重新钉] 按钮是两行文字（DO 号 + 语义名），
        高度必须够放两行；行高由 row_height() 按当前字体实测（随界面缩放
        走），QSS 不再写死高度（写死 46 在缩放 1.5× 时会把上下裁掉）。
        """
        if not 0 <= index < len(self._buttons):
            return
        btn = self._buttons[index]
        on = self._states.get(index, False)
        text = "DO%d %s" % (index, "通" if on else "断")
        alias = self._aliases.get(index, "")
        if alias:
            text += "\n%s" % alias
        btn.setText(text)
        # setText 改变 sizeHint 后布局会重算高度 —— 这里按字体实测钉死，
        # 保证同一行里（有名字/无名字）按钮高度一致、且两行字都放得下。
        btn.setFixedHeight(self.row_height())

    def set_name(self, index, text):
        """设置语义名（服务端下发；空串=只显示 DO 号 + 通断）。"""
        text = (text or "").strip()
        if self._aliases.get(index, None) == text:
            return                       # 名字没变：不动控件（每拍都会来）
        self._aliases[index] = text
        self._render(index)
        # 名字变长 ⇒ 每格需要的宽度变大 ⇒ 必要时少排一列（宁可多几行
        # 也不裁字）。名字是后到的，所以每次收到名字都要重算列数。
        self._adapt_columns()

    def _adapt_columns(self):
        """按当前宽度重排列数（只在列数真的变了时重排，避免抖动）。"""
        cols = adaptive_columns(self, self._max_columns, self._min_cell_w())
        if cols == self._columns:
            return
        self._columns = cols
        for b in self._buttons:
            self._grid.removeWidget(b)
        for i, b in enumerate(self._buttons):
            self._grid.addWidget(b, i // self._columns, i % self._columns)

    def name_of(self, index):
        return self._aliases.get(index, "")

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
        # 状态记在 _states 里，文字统一由 _render 生成 —— 否则这里直接
        # setText 会把已下发的语义名抹掉（每次刷新都丢名字）。
        self._states[index] = bool(on)
        self._render(index)

    def set_enabled_all(self, enabled):
        for btn in self._buttons:
            btn.setEnabled(enabled)


def form_row(label, widget, hint=""):
    """「标签 + 控件 (+ 提示)」一行，供设置页复用。"""
    row = QHBoxLayout()
    row.setSpacing(8)
    lbl = QLabel(label)
    lbl.setMinimumWidth(110)
    lbl.setStyleSheet("QLabel { color: palette(text); background: transparent; }")
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
    # [标题色跟主题] 用调色板角色 palette(link)（theme 把 Link 角色设成
    # 当前主题的标题色）⇒ 换主题时自动跟随，不必逐控件重刷。
    lbl.setStyleSheet(
        "QLabel { color: palette(link); font-size: 21px; font-weight: bold;"
        " background: transparent; }")
    return lbl
