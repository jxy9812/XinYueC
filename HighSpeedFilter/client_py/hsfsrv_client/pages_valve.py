# -*- coding: utf-8 -*-
"""电磁阀监视/操作页：6 路阀的卡片式看板。

================================================================================
                              页面设计
================================================================================

每路阀一张卡片，卡片内三段：

    ┌──────────────────────────┐
    │  ●  进水阀          [开] │  ← 状态灯 + 名称 + 状态徽标
    │                          │
    │   ●  开到位              │  ← 开到位反馈（绿=到位）
    │   ○  关到位              │  ← 关到位反馈
    │                          │
    │   [ 开阀 ]   [ 关阀 ]    │  ← 手动模式下的操作按钮
    └──────────────────────────┘

状态用**颜色 + 文字**双编码（不只靠颜色，色弱/黑白屏也能读出）：

    已开    绿    #2E7D32
    已关    灰蓝  #37474F
    动作中  橙    #ED6C02（并显示"动作中…"）
    故障    红    #C62828
    未配置  浅灰  #9E9E9E（点位还没接线）

================================================================================
                            客户端职责边界
================================================================================

客户端**只知道 6 路阀的状态**（服务端算好给的），不知道也不该知道：

    · 哪路阀接在哪个数字量模块的哪个 DO/DI
    · 是单电控还是双电控
    · 反馈信号是常开还是常闭

这些全在服务端（`hsfsrv/valves.py` + 配置）。将来接线一改，服务端改配置，
客户端零改动。

================================================================================
                                控制闸门
================================================================================

和变频器/数字量同一套：自动模式拒绝、需 root、未配置点位不可操作。
按钮禁用时在页面顶部说明原因，而不是让用户点了没反应。
"""

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QGridLayout, QHBoxLayout, QLabel, QPushButton,
                             QSizePolicy, QVBoxLayout, QWidget)

from . import proto, theme
from .page_base import Page, elide_text, fit_text_width
from .widgets import Card

#: 状态 → (中文, 颜色)。
#:
#: [Unknown 的语义] 它表示"状态未知"，**不是**"未配置"——
#: 未配置由独立的 ready 字段表达（未配置时直接用"未配置"覆盖显示）。
#: 现场最常见的情形：点位配好了但行程开关没接 → 两个反馈恒为 0 →
#: 状态 Unknown + 提示"检查行程开关接线"。这时候说"未配置"会把人
#: 引到错误的排查方向（去翻配置而不是去看接线）。
VALVE_STATE_TEXT = {
    proto.HsfValveState.Unknown: ("状态未知", "#9E9E9E"),
    proto.HsfValveState.Closed: ("已关", "#37474F"),
    proto.HsfValveState.Open: ("已开", "#2E7D32"),
    proto.HsfValveState.Moving: ("动作中", "#ED6C02"),
    proto.HsfValveState.Fault: ("故障", "#C62828"),
}

#: 控制命令的处置结果 → 状态栏文案（与服务端 valves.VALVE_CMD_TEXT 同口径）。
#:
#: [为什么客户端要区分] 服务端对"重复点同一侧"有三种处置：
#:   issued   真的动了
#:   repeated 动作中重发，**不重置 20s 计时**（防止卡阀被反复点击掩盖）
#:   reversed 反向，重新计时
#:   ignored  已在目标位置，什么都没做
#: 若一律显示"命令已受理"，用户会以为重复点击也生效了 —— 与事实相反。
VALVE_CMD_TEXT = {
    "issued": "命令已下发",
    "repeated": "动作中，重复命令不重置计时",
    "reversed": "已反向（重新计时）",
    "ignored": "已在目标位置，无需动作",
}


class FeedbackDot(QWidget):
    """到位反馈指示点：实心=到位（绿），空心=未到位（灰）。

    [为什么自绘] 与数字量页同一个理由：板上 256 色渲染字符圆点会出条纹，
    QPainter 画圆与字体/色深无关。
    """

    DIAMETER = 10

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self._on = False
        self.setFixedSize(self.DIAMETER + 6, self.DIAMETER + 6)

    def set_on(self, on):
        on = bool(on)
        if self._on != on:
            self._on = on
            self.update()

    def paintEvent(self, _event):
        from PyQt5.QtGui import QColor, QPainter, QPen
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, False)
        margin = 3
        rect = (margin, margin,
                self.width() - 2 * margin, self.height() - 2 * margin)
        if self._on:
            fill = QColor("#2E7D32")
            painter.setPen(QPen(fill.darker(150), 1))
            painter.setBrush(fill)
        else:
            painter.setPen(QPen(QColor("#B0BEC5"), 1))
            painter.setBrush(Qt.NoBrush)
        painter.drawEllipse(*rect)
        painter.end()


class ValveCard(Card):
    """单路电磁阀卡片。"""

    #: 状态徽标的左右内边距（与 QSS 里的 ``padding: 2px 10px`` 同源）。
    #: 徽标底宽按「**最宽状态文案** + 内边距」实测给出 ⇒ 任何状态都不裁字。
    STATE_PAD_H = 10

    #: 名称的**最长候选**（服务端按约定下发「电动球阀1」…「电动球阀6」）。
    #: 底宽按它实测 ⇒ 名称永远装得下（更长的名字按宽度省略 + tooltip）。
    NAME_CANDIDATES = tuple("电动球阀%d" % i for i in range(1, 7))

    def __init__(self, index, on_request, parent=None):
        self.index = index
        #: 操作回调 (index, want_on) -> None
        self._on_request = on_request
        #: 阀名全文（标签里可能是省略过的 ⇒ 状态栏/提示用这个）。
        self._name_text = "电动球阀%d" % (index + 1)

        Card.__init__(self, parent=parent)
        # [不要再钉死最小宽] 原来 `setMinimumWidth(220)` **盖掉了**卡片
        # 内容自己算出来的最小宽（≈250）⇒ 窄窗时布局把卡片压到 220 ⇒
        # 顶部两个标签互相挤 ⇒ 名称（"电动球阀1"）被裁掉 4px、状态徽标
        # （"未配置"）被裁成"未配"（用户可见的硬裁）。⇒ 交回给内容自己
        # 决定（Qt 按 minimumSizeHint 给），页面再按可用宽决定排几列。

        # ---- 顶部：状态点 + 名称 + 状态徽标 ----
        top = QHBoxLayout()
        top.setSpacing(8)
        self.status_dot = FeedbackDot()
        self.status_dot.setFixedSize(16, 16)
        top.addWidget(self.status_dot)

        self.name_label = QLabel(self._name_text)
        self.name_label.setStyleSheet(
            "QLabel { font-size: 22px; font-weight: bold; color: palette(text);"
            " background: transparent; }")
        # [名称不裁字] 底宽按**实测**最长候选名给（不写死像素 ⇒ 字号缩放
        # 后依然成立）；服务端下发更长的名字时按宽度省略 + tooltip 全文。
        self.name_label.setMinimumWidth(
            fit_text_width(self.name_label, self.NAME_CANDIDATES) + 4)
        top.addWidget(self.name_label, 1)

        self.state_label = QLabel("未配置")
        self.state_label.setAlignment(Qt.AlignCenter)
        # [先上样式再量宽] 徽标的字号来自**它自己的 QSS**（18px 粗体）⇒
        # 样式没挂上时量的是继承来的 app 字号（偏小）⇒ 徽标会比字窄 ⇒
        # 「状态未知」被裁。⇒ 先 _style_state（挂 QSS）再实测底宽。
        self._style_state("未配置", "#616A73")
        # [徽标底宽按最宽状态文案实测] 状态有 6 种（未配置/状态未知/已开/
        # 已关/动作中/故障）⇒ 底宽取最宽的那一档 ⇒ 换状态时徽标宽度
        # **不变**（几何恒定，名字那一格也不会忽宽忽窄）。
        self._state_min_w = (
            fit_text_width(self.state_label,
                           [t for (t, _c) in VALVE_STATE_TEXT.values()]
                           + ["未配置"])
            + 2 * self.STATE_PAD_H + 4)
        self.state_label.setMinimumWidth(self._state_min_w)
        self.state_label.setMaximumWidth(self._state_min_w)
        top.addWidget(self.state_label)
        self.body.addLayout(top)

        # ---- 中部：两个到位反馈 ----
        feedback = QVBoxLayout()
        feedback.setSpacing(4)
        self.open_dot, self.open_label = self._make_feedback_row(
            feedback, "开到位")
        self.close_dot, self.close_label = self._make_feedback_row(
            feedback, "关到位")
        self.body.addLayout(feedback)

        # ---- 底部：操作按钮 ----
        buttons = QHBoxLayout()
        buttons.setSpacing(8)
        self.btn_open = QPushButton("开阀")
        self.btn_open.setMinimumHeight(34)
        self.btn_open.clicked.connect(lambda: self._request(True))
        self.btn_close = QPushButton("关阀")
        self.btn_close.setMinimumHeight(34)
        self.btn_close.clicked.connect(lambda: self._request(False))
        buttons.addWidget(self.btn_open, 1)
        buttons.addWidget(self.btn_close, 1)
        self.body.addLayout(buttons)

        # ---- 提示行（未配置 / 故障原因）----
        # [恒占位，别用 setVisible] 状态在「动作中 ↔ 已开/已关」之间切换时
        # 提示文案会时有时无；若用 setVisible(False) 把整行从布局里拿掉，
        # 卡片高度就跟着变 ⇒ **按钮上下跳**（用户反馈「卡片布局变化有点大，
        # 按钮都动了」）。改为：这一行**永远占位**、高度按当前字体实测固定，
        # 没文案就 setText("") 留空；长文案用省略号（WordWrap=False），
        # 不换行把卡片撑高。对齐「尺寸交布局、别靠显隐改结构」铁律。
        self.hint_label = QLabel("")
        self.hint_label.setWordWrap(False)
        self.hint_label.setStyleSheet(
            "QLabel { color: palette(mid); font-size: 20px;"
            " background: transparent; }")
        from PyQt5.QtGui import QFontMetrics
        self.hint_label.setFixedHeight(
            QFontMetrics(self.hint_label.font()).lineSpacing() + 2)
        # [横向不参与最小宽计算 2026-11-20] 提示行装的是**长文案**（实测
        # 「点位未配置（服务端 config 的 valves.items）」要 640px）⇒ 若让它
        # 按 sizeHint 参与布局 ⇒ 卡片最小宽被撑到 666px ⇒ 窄窗下整个页面
        # 出**横向滚动条**（用户看到"页面比窗口宽"）。⇒ 横向策略设为
        # Ignored（sizeHint 不参与最小宽计算），文字按**卡片当前宽度**
        # 省略显示、全文挂 tooltip（见 _refresh_hint）。
        self.hint_label.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Fixed)
        #: 提示行全文（标签里是按宽度省略过的版本）。
        self._hint_text = ""
        self.body.addWidget(self.hint_label)

    def resizeEvent(self, event):
        """卡片宽度变了 ⇒ 提示行**重新按新宽度省略**（QLabel 自己不会省略）。

        [为什么] 提示行是固定高度的一行 ⇒ 文案只能靠省略号收窄；卡片宽度
        变了（窄窗折行、窗口缩放）⇒ 旧的省略结果不再合适 ⇒ 重算一遍。
        """
        Card.resizeEvent(self, event)
        self._refresh_hint()

    def _refresh_hint(self):
        """提示行按**当前可用宽**省略（QLabel 默认直接裁字）。"""
        text = self._hint_text or ""
        if not text:
            self.hint_label.setText("")
            return
        self.hint_label.setText(elide_text(self.hint_label, text))
        self.hint_label.setToolTip(text)

    def _make_feedback_row(self, parent_layout, caption):
        row = QHBoxLayout()
        row.setSpacing(6)
        dot = FeedbackDot()
        row.addWidget(dot)
        label = QLabel("%s：--" % caption)
        label.setStyleSheet(
            "QLabel { color: palette(mid); font-size: 18px;"
            " background: transparent; }")
        row.addWidget(label, 1)
        parent_layout.addLayout(row)
        return dot, label

    def _request(self, want_on):
        if self._on_request:
            self._on_request(self.index, want_on)

    def _style_state(self, text, color):
        self.state_label.setText(text)
        self.state_label.setStyleSheet(
            "QLabel { color: #FFFFFF; background-color: %s; border-radius: 9px;"
            " padding: 2px %dpx; font-size: 18px; font-weight: bold; }"
            % (color, self.STATE_PAD_H))

    def name_text(self):
        """阀名**全文**（标签里可能是按宽度省略过的）。"""
        return self._name_text

    def apply_state(self, item, blocked_reason):
        """把服务端下发的单路状态刷到卡片上。

        :param item: dict（name/openCmd/openFb/closeFb/state/ready/valid/text）
        :param blocked_reason: 非空时禁用操作按钮并显示原因
        """
        ready = bool(item.get("ready", False))
        state = int(item.get("state", proto.HsfValveState.Unknown))
        text, color = VALVE_STATE_TEXT.get(
            state, ("未知", "#9E9E9E"))

        # [名字不裁字] 服务端按约定下发「电动球阀1」…「电动球阀6」⇒ 底宽
        # 装得下；万一现场改了名更长 ⇒ 按宽度省略 + tooltip 全文（与提示
        # 行同款口径），状态栏/提示仍用全文。
        self._name_text = str(item.get("name") or
                              ("电动球阀%d" % (self.index + 1)))
        self.name_label.setText(elide_text(self.name_label, self._name_text))
        self.name_label.setToolTip(self._name_text)
        if not ready:
            self._style_state("未配置", "#616A73")
            self.status_dot.set_on(False)
        else:
            self._style_state(text, color)
            # 顶部状态点：开=绿、关=灰、动作中=橙、故障=红（用"亮"表示非关）
            self.status_dot.set_on(state in (proto.HsfValveState.Open,
                                             proto.HsfValveState.Moving,
                                             proto.HsfValveState.Fault))

        open_fb = bool(item.get("openFb", False))
        close_fb = bool(item.get("closeFb", False))
        self.open_dot.set_on(open_fb)
        self.close_dot.set_on(close_fb)
        if ready:
            self.open_label.setText("开到位：%s" % ("是" if open_fb else "否"))
            self.close_label.setText("关到位：%s" % ("是" if close_fb else "否"))
        else:
            self.open_label.setText("开到位：--")
            self.close_label.setText("关到位：--")

        # 提示：优先显示服务端给的故障/状态说明
        hint = item.get("text", "") or ""
        if not hint and not ready:
            hint = "点位未配置（服务端 config 的 valves.items）"
        # [动作中要说清能干什么] 阀在途中时两个按钮都可点：点另一侧＝
        # **反向**（服务端会重新计时），点同一侧＝重复请求（服务端防抖，
        # 不重置计时）。不说的话用户不知道自己在干嘛。
        if ready and state == proto.HsfValveState.Moving:
            hint = (hint + " · " if hint else "") + "点另一侧可反向"
        # [恒占位] 不 setVisible：空文案只清文字，行高不变 ⇒ 按钮不跳。
        # [长文案省略 2026-11-14] 一行高度放不下长提示（实测「点位未配置
        # （服务端 config 的 valves.items）」要 640px、卡片只有 265px ⇒
        # 后半截被直接裁掉）⇒ 按宽度省略，全文挂 tooltip。
        self._hint_text = hint
        self._refresh_hint()

        # 按钮可用性
        #
        # [动作中两个都保留] 在途中点另一侧是合法的"反向"操作（现场常见：
        # 发现开错了立刻叫回）。只有**已到位**那一侧才置灰 —— 那时再点同侧
        # 没有意义（服务端也会幂等忽略，但界面上不该给出可点的假象）。
        can_control = ready and not blocked_reason
        self.btn_open.setEnabled(can_control)
        self.btn_close.setEnabled(can_control)
        if can_control:
            if state == proto.HsfValveState.Open:
                self.btn_open.setEnabled(False)
            elif state == proto.HsfValveState.Closed:
                self.btn_close.setEnabled(False)


class ValvePage(Page):
    """6 路电磁阀总览页。"""

    TOPICS = (proto.TOPIC_VALVE_STATUS,)

    #: 每行卡片数（宽屏 3 列 × 2 行 = 6 路）。
    COLUMNS = 3

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        self.add_header("电动球阀", "%d 路 · 1 路输出 + 2 路到位反馈"
                        % proto.HSF_VALVE_COUNT)

        # ---- 顶部：控制可用性提示 ----
        # [恒占位] 与卡片提示行同款：不 setVisible。横幅出现/消失会把整张
        # 卡片网格往下推（用户反馈的「布局变化有点大」第二来源）。改为永远
        # 占一行、高度按当前字体实测固定；无内容时文字留空且**去掉底色/
        # 边框**（视觉上等于没有横幅，但布局不变），有内容才上色。
        self.gate_label = QLabel("")
        self.gate_label.setWordWrap(True)
        from PyQt5.QtGui import QFontMetrics
        self._gate_empty_qss = (
            "QLabel { color: transparent; background: transparent;"
            " border: 1px solid transparent; padding: 6px 10px;"
            " font-size: 18px; }")
        self._gate_alert_qss = (
            "QLabel { color: #B26500; background-color: #FFF4E5;"
            " border: 1px solid #FFD8A8; border-radius: 4px;"
            " padding: 6px 10px; font-size: 18px; }")
        self.gate_label.setStyleSheet(self._gate_empty_qss)
        self.gate_label.setText(" ")
        self.gate_label.setFixedHeight(
            QFontMetrics(self.gate_label.font()).lineSpacing() + 16)
        self.root.addWidget(self.gate_label)

        # ---- 卡片网格 ----
        grid_host = QWidget()
        grid = QGridLayout(grid_host)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(12)
        self._grid_host = grid_host
        self._grid = grid
        self._cols = 0
        self.cards = []
        for index in range(proto.HSF_VALVE_COUNT):
            card = ValveCard(index, self._request_control)
            grid.addWidget(card, index // self.COLUMNS,
                           index % self.COLUMNS)
            self.cards.append(card)
        self.root.addWidget(grid_host)
        self.root.addStretch(1)
        # [列数随宽度自适应] 见 _adapt_columns 的说明。
        grid_host.installEventFilter(self)
        # 视口变了也要重排（内容最小宽比视口大时，网格容器自身宽度不变
        # ⇒ 只盯容器会漏掉视口变化）。
        self.scroll.viewport().installEventFilter(self)

        #: 最近一次收到的 6 路状态（dict: index -> item dict）
        self._items = {}

    def _set_gate(self, text):
        """顶部横幅统一入口：有文案上色、无文案透明占位（**布局恒定**）。"""
        text = text or ""
        self.gate_label.setText(text or " ")
        self.gate_label.setStyleSheet(self._gate_alert_qss if text
                                      else self._gate_empty_qss)

    # ---- 网格自适应（宁可多几行也不裁字）----

    def _adapt_columns(self):
        """按**当前可用宽**决定每行排几张卡。

        [为什么] 原来固定 3 列：窄窗（板端竖屏 800×1280 / 640×900）时
        3×卡片最小宽 > 视口 ⇒ 页面出**横向滚动条**、卡片被压到最小宽
        以下 ⇒ 卡片里的名称/状态徽标被硬裁（实测"电动球阀1"少 4px、
        "未配置"被裁成"未配"）。⇒ 与 page_base.adaptive_columns 同款
        纪律：**装不下就少排一列**（宁可多几行也不裁字）。

        [卡片宽怎么量] 用卡片自己的 minimumSizeHint（= 内容算出来的最小
        宽）——不写死像素 ⇒ 字号缩放后依然成立。

        [为什么按**视口**宽算、而不是网格容器自己的宽] 网格容器的宽 =
        页面内容宽，而内容宽 ≥ 视口宽（内容最小宽更大时页面会出横向
        滚动条）⇒ 用容器宽算会**自我循环**：3 列 ⇒ 内容最小宽变大 ⇒ 容器
        变宽 ⇒ 「3 列放得下」⇒ 永远 3 列 ⇒ 窄窗仍是横向滚动 + 卡片被
        压扁。⇒ 取**滚动区视口**宽（= 用户真正看得到的宽度）来定列数。
        """
        avail = self._viewport_w()
        if avail <= 0:
            return                       # 布局还没稳定 ⇒ 下一拍再来
        card_min = 1
        for c in self.cards:
            try:
                card_min = max(card_min, int(c.minimumSizeHint().width()))
            except Exception:           # noqa: BLE001
                continue
        spacing = int(self._grid.spacing())
        cols = max(1, min(int(self.COLUMNS),
                          (avail + spacing) // (card_min + spacing)))
        if cols == self._cols:
            return
        self._cols = cols
        for c in self.cards:
            self._grid.removeWidget(c)
        for i, c in enumerate(self.cards):
            self._grid.addWidget(c, i // cols, i % cols)

    def _viewport_w(self):
        """页面滚动区的**视口**宽（用户真正看得到的宽度）。"""
        try:
            return int(self.scroll.viewport().width())
        except Exception:                               # noqa: BLE001
            return 0

    def eventFilter(self, obj, event):
        """视口/网格尺寸变了 ⇒ 重排列数（窄窗少排一列，别裁字）。"""
        from PyQt5.QtCore import QEvent
        if event.type() == QEvent.Resize:
            if obj is self._grid_host:
                self._adapt_columns()
            elif obj is self.scroll.viewport():
                self._adapt_columns()
        return QWidget.eventFilter(self, obj, event)

    # ---- 控制 ----
    def _request_control(self, index, want_on):
        reason = self.ctx.control_blocked_reason()
        if reason:
            self.ctx.set_status("禁止操作：%s" % reason)
            return
        card = self.cards[index]
        if not card.btn_open.isEnabled() and not card.btn_close.isEnabled():
            return
        self.ctx.set_status("%s → 正在%s…"
                            % (card.name_text(),
                               "开阀" if want_on else "关阀"))

        def _done(code, dtype, data):
            """阀控制应答（**网络线程**）：全部改动投递到 UI 线程。

            [这是崩溃点] 原实现在这里直接调 `card.name_label.text()`
            （读控件）和 `self._apply_doc()`（重建卡片）——从网络线程碰
            QWidget 会让 Qt **直接崩进程**，且没有任何 Python traceback。
            实测症状：点"开阀/关阀"后界面立刻消失。
            """
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            ok = bool(doc.get("ok"))
            err = doc.get("err", "未知原因")

            def _apply():
                if ok:
                    # [按服务端的处置结果给不同文案] 服务端可能做三件不同
                    # 的事：真动了 / 动作中重发但不重置计时 / 已到位什么都没
                    # 做。统一显示"命令已受理"会让用户以为重复点击也生效了，
                    # 而实际上服务端是为了防"卡阀被反复点击掩盖超时"才这么干。
                    result = doc.get("v%d_%s" % (index,
                                                 proto.KEY_VALVE_CMD_RESULT))
                    text = VALVE_CMD_TEXT.get(result, "命令已受理")
                    self.ctx.set_status("%s %s"
                                        % (card.name_label.text(), text))
                    self._apply_doc(doc)
                else:
                    self.ctx.set_status("%s 操作失败：%s"
                                        % (card.name_label.text(), err))
            self.ctx.bridge.run_in_ui(_apply)

        def _err(msg):
            self.ctx.bridge.run_in_ui(
                lambda: self.ctx.set_status(
                    "%s 操作失败：%s" % (card.name_text(), msg)))

        self.ctx.bridge.request(proto.CMD_VALVE_CONTROL,
                                {"index": int(index), "on": bool(want_on)},
                                on_done=_done, on_error=_err)

    # ---- 数据落地 ----
    def _apply_doc(self, doc):
        """把服务端快照（含 v0_*/v1_* 前缀键）拆成每路 dict。

        [控制应答也走这里] 应答里同样带整帧 6 路状态，故与推送同一套解析。
        注意应答现在由服务端在**强制现读数字量模块之后**生成（见
        app._h_valve_control），所以这份数据是命令之后的新反馈，可直接落地。

        [必须同步写进 model] 本页面板 refresh() 优先从 model 取数（model 是
        推送的落地点）。若应答只写 `self._items` 不写 model，200ms 后下一拍
        **命令之前**的旧推送会把刚拿到的应答状态覆盖回去 —— 症状正是
        "点第一下没反应"。故应答与推送必须汇入同一个事实源。
        """
        count = int(doc.get("count", 0) or 0)
        items = {}
        for index in range(min(count, proto.HSF_VALVE_COUNT)):
            prefix = "v%d_" % index
            items[index] = {
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            }
        self._items = items
        # 写回 model：让周期刷新与应答看到同一份数据。
        model = getattr(self.ctx.bridge, "model", None)
        valves = getattr(model, "valve", None) if model is not None else None
        if valves is not None and items:
            valves.items = dict(items)
        reason = self.ctx.control_blocked_reason()
        for index, card in enumerate(self.cards):
            card.apply_state(items.get(index, {}), reason)
        # [名字可能比默认名长] ⇒ 卡片最小宽变了 ⇒ 重排列数（别裁字）。
        self._adapt_columns()

        # 顶部提示（恒占位，不 setVisible）
        if reason:
            self._set_gate("控制已禁用：%s" % reason)
        else:
            unconfigured = sum(
                1 for item in items.values() if not item.get("ready", False))
            if unconfigured:
                self._set_gate(
                    "%d 路点位未配置（服务端 config 的 valves.items 里补 "
                    "module/do/openDi/closeDi 即可启用）" % unconfigured)
            else:
                self._set_gate("")

    # ---- 周期刷新 ----
    def on_enter(self):
        """入页拉一次快照（不依赖周期推送的首帧时机）。"""
        def _done(code, dtype, data):
            """快照应答（**网络线程**）：_apply_doc 会重建卡片，必须投递。"""
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            if doc.get("count") is not None:
                self.ctx.bridge.run_in_ui(lambda: self._apply_doc(doc))

        self.ctx.bridge.request(proto.CMD_VALVE_SNAPSHOT, None, on_done=_done)

    def refresh(self):
        """周期刷新：**从 model 取最新推送**，不要只用入页时那份快照。

        [实锅 2026-11-05：阀状态要点两下才变] 原来这里只读 `self._items`，
        而它只在「控制应答」和「入页快照」时被写入 —— 服务端每 200ms 推
        的 TOPIC_VALVE_STATUS 全进了 `model.valve`，页面**一次都没读过**。
        后果：卡片状态只能靠"下一次控制应答"推动，而那段窗口里显示的是
        上一拍的值，用户看到的就是"点第一下没反应、点第二下才动"。

        现在改成本处直接与 model 对齐（model 是推送的落地处，永远最新）：
        model 有数据就用它，没有才退回入页快照。
        """
        reason = self.ctx.control_blocked_reason()
        model_items = self._model_items()
        if model_items:
            self._items = model_items
        for index, card in enumerate(self.cards):
            item = self._items.get(index, {})
            card.apply_state(item, reason)
        if reason:
            self._set_gate("控制已禁用：%s" % reason)
        self._drain_alarms()

    def _model_items(self):
        """从 model 取最新 6 路状态（推送的落地处）。

        :returns: {index: item_dict}；model 还没收到任何推送时返回 {}。

        [为什么不能直接信 self._items] 那是"页面最后一次被写"的快照，
        不是"最新"。推送进的是 model（bridge._apply_doc），页面必须从
        model 读才算跟上。
        """
        model = getattr(self.ctx.bridge, "model", None)
        valves = getattr(model, "valve", None) if model is not None else None
        if valves is None:
            return {}
        items = getattr(valves, "items", None)
        if not items:
            return {}
        return dict(items)

    def _drain_alarms(self):
        """把服务端推来的阀告警显示到状态栏与顶部横幅。

        [为什么只显示不判断] "超没超时、算不算故障"全在服务端算（现场满开
        满关约 10s，超时阈值 20s）。客户端只是复述服务端给的文案——两边各
        算一套必然对不上（服务端说超时、客户端觉得还早，或反之）。
        """
        model = getattr(self.ctx.bridge, "model", None)
        if model is None:
            return
        valves = getattr(model, "valve", None)
        if valves is None:
            return
        alarms = valves.drain_alarms()
        for alarm in alarms:
            name = alarm.get(proto.KEY_ALARM_VALVE_NAME, "阀")
            text = alarm.get(proto.KEY_ALARM_TEXT, "")
            kind = int(alarm.get(proto.KEY_ALARM_KIND, 0) or 0)
            if kind == proto.VALVE_ALARM_CLEARED:
                self.ctx.set_status("%s 已恢复：%s" % (name, text))
            else:
                self.ctx.set_status("%s 告警：%s" % (name, text))
                # 顶部横幅留一条最近的告警（不清除，直到下一条覆盖）。
                self._set_gate("⚠ %s：%s" % (name, text))
