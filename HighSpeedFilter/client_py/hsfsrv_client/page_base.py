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
        #: 视口宽无效时的重试计数（0=正常；>0 表示在排队等布局稳定）
        self._width_retry = 0
        #: 上次算列宽用的可用宽度（用于判断是否需要重算）
        self._width_basis = -1
        self.table.verticalHeader().setDefaultSectionSize(theme.TABLE_ROW_H)

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

    def set_column_weights(self, weights, min_widths=None):
        """设置列宽比例（如 (6,3,2) = 项目 6 : 数值 3 : 单位 2）。

        :param min_widths: {列号: 最小像素} 按列下限（可选）。某列内容比
        全局最小档（72/150）更长时用它给硬保证 —— 历史页时间列
        （"yyyy-MM-dd HH:MM:SS" 19 字符 ≈ 130px）实锅：全局 72px 档把它
        压成 "202..."，日期看不见（2026-11-05 截图反馈）。
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
        """
        header = self.table.horizontalHeader()
        # [最小列宽按"最长的典型内容"定，不是一律 72] 地址列要装
        # "COM17 · 115200 · 站 2"（约 22 字符），只给 72px 会被 Qt 用
        # 省略号截断成 "COM17 · 115200 · ..."，而**恰恰是最后那段站号被截掉**
        # —— 现场最需要看的就是站号。故这里按列内容给不同的下限
        # （Qt 没有 per-section minimum 的公共 API，只能整体抬高）。
        header.setMinimumSectionSize(
            150 if max(self._column_weights or (1,)) >= 6 else 72)

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
        total_w = sum(weights)
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
        min_w = header.minimumSectionSize() or 56
        min_w = min(min_w, max(40, avail // max(1, n)))
        # 各列下限 = 全局档 与 按列 min_widths 取大者；先全部预留，
        # 剩余宽度再按权重分（下限总和超出视口时宁出横向滚动条，
        # 也不把任何一列压到内容以下）。
        min_map = getattr(self, "_min_widths", {}) or {}
        floors = [max(min_w, int(min_map.get(i, 0))) for i in range(n)]
        spare = max(0, avail - sum(floors))        # 下限之外可自由分配的部分
        targets = []
        used = 0
        for i, w in enumerate(weights[:-1]):
            px = floors[i] + int(spare * w / total_w)
            targets.append(px)
            used += px
        targets.append(max(floors[-1], avail - used))   # 最后一列吃掉余数
        for i, px in enumerate(targets):
            if header.sectionSize(i) != px:
                header.resizeSection(i, px)

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

    def _retry_widths(self):
        # 不复位计数：_apply_column_widths 成功后会自己清零
        self._apply_column_widths()

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self._apply_column_widths()

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
        # [改高度后必须重算列宽] setMaximumHeight 会改变表格几何，但不一定
        # 触发本控件的 resizeEvent（尤其是"切页回来"这条路径）⇒ 列宽可能
        # 停在旧值/下限上。这里补一次；_apply_column_widths 内部有
        # "值没变就不写"的短路，不会造成重绘循环。
        self._apply_column_widths()

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

    [点数由服务端决定] 构造时给一个**初值**，真正的点数在收到第一帧推送后
    调 set_count() 重建。不能写死 16：单模块 16 点、聚合后 32 点，写死会让
    上半区（16~31）永远看不到，而服务端明明在发。
    """

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._cells = []
        self._columns = max(1, int(columns))
        self._grid = QGridLayout(self)
        self._grid.setContentsMargins(0, 0, 0, 0)
        self._grid.setSpacing(8)
        self.set_count(count)

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
            alias.setWordWrap(True)
            alias.setVisible(False)
            v.addWidget(alias)
            self._grid.addWidget(cell, i // self._columns,
                                 i % self._columns)
            self._cells.append((dot, name, alias))

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
        alias.setText(text)
        alias.setVisible(bool(text))

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

    #: 按钮统一行高（像素）。有语义名的是两行文字，必须按两行留高度，
    #: 否则同一行里两行按钮把整行撑高、单行按钮出现空隙。
    ROW_HEIGHT = 46

    def __init__(self, count=16, columns=8, parent=None):
        QWidget.__init__(self, parent)
        self._buttons = []
        #: 每路的语义名（服务端下发）；按索引存，set_count 后仍保留。
        self._aliases = {}
        self.on_toggle = None            # (index, want_on) -> None
        self._columns = max(1, int(columns))
        self._grid = QGridLayout(self)
        self._grid.setContentsMargins(0, 0, 0, 0)
        self._grid.setSpacing(6)
        self.set_count(count)

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
        for i in range(count):
            btn = QPushButton()
            btn.setCheckable(True)
            # [用 objectName + QSS 钉高度，不用 setFixedHeight] QSS 里
            # QPushButton 有 `padding: 6px 14px` + `min-height: 18px`，
            # 样式表算出的 sizeHint 会覆盖 Qt 的固定高度：实测单行按钮
            # sizeHint=33、两行=51，而 setFixedHeight(46) 只把 maximum 设成
            # 46 —— 布局仍按各自 sizeHint 排版，同一行里 46 和 33 参差。
            # 用 objectName 让 QSS 显式给出统一的固定行高与内边距。
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

        [高度必须在 setText 之后重新钉] QSS 里 QPushButton 有
        `min-height: 18px` 和 `padding`，setText 改变 sizeHint 后布局会
        重新算高度 —— 只靠构造时 setFixedHeight 会被 QSS 的 sizeHint
        顶回来，结果同一行里两行文字的按钮 46px、单行按钮 33px，
        整行高度被撑乱（实测 32 点页面明显参差）。
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
        # 样式表已按 objectName 钉死行高；这里再调一次只为覆盖 setText 后
        # Qt 对 sizeHint 的重算（某些平台仍会把 hint 往上顶）。
        btn.setFixedHeight(self.ROW_HEIGHT)

    def set_name(self, index, text):
        """设置语义名（服务端下发；空串=只显示 DO 号 + 通断）。"""
        self._aliases[index] = (text or "").strip()
        self._render(index)

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
