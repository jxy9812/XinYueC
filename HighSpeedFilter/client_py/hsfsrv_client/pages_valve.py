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
                             QVBoxLayout, QWidget)

from . import proto, theme
from .page_base import Page
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

    def __init__(self, index, on_request, parent=None):
        self.index = index
        #: 操作回调 (index, want_on) -> None
        self._on_request = on_request

        Card.__init__(self, parent=parent)
        self.setMinimumWidth(220)

        # ---- 顶部：状态点 + 名称 + 状态徽标 ----
        top = QHBoxLayout()
        top.setSpacing(8)
        self.status_dot = FeedbackDot()
        self.status_dot.setFixedSize(16, 16)
        top.addWidget(self.status_dot)

        self.name_label = QLabel("电磁阀 %d" % (index + 1))
        self.name_label.setStyleSheet(
            "QLabel { font-size: 15px; font-weight: bold; color: #1A1A1A;"
            " background: transparent; }")
        top.addWidget(self.name_label, 1)

        self.state_label = QLabel("未配置")
        self.state_label.setAlignment(Qt.AlignCenter)
        self.state_label.setMinimumWidth(56)
        self._style_state("未配置", "#9E9E9E")
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
        self.hint_label = QLabel("")
        self.hint_label.setWordWrap(True)
        self.hint_label.setStyleSheet(
            "QLabel { color: #5A6572; font-size: 11px;"
            " background: transparent; }")
        self.body.addWidget(self.hint_label)

    def _make_feedback_row(self, parent_layout, caption):
        row = QHBoxLayout()
        row.setSpacing(6)
        dot = FeedbackDot()
        row.addWidget(dot)
        label = QLabel("%s：--" % caption)
        label.setStyleSheet(
            "QLabel { color: #5A6572; font-size: 12px;"
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
            " padding: 2px 10px; font-size: 12px; font-weight: bold; }" % color)

    def apply_state(self, item, blocked_reason):
        """把服务端下发的单路状态刷到卡片上。

        :param item: dict（name/openCmd/openFb/closeFb/state/ready/valid/text）
        :param blocked_reason: 非空时禁用操作按钮并显示原因
        """
        ready = bool(item.get("ready", False))
        state = int(item.get("state", proto.HsfValveState.Unknown))
        text, color = VALVE_STATE_TEXT.get(
            state, ("未知", "#9E9E9E"))

        self.name_label.setText(item.get("name") or
                                ("电磁阀 %d" % (self.index + 1)))
        if not ready:
            self._style_state("未配置", "#9E9E9E")
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
        self.hint_label.setText(hint)
        self.hint_label.setVisible(bool(hint))

        # 按钮可用性
        can_control = ready and not blocked_reason
        self.btn_open.setEnabled(can_control)
        self.btn_close.setEnabled(can_control)
        # 已到位的那一侧按钮置灰（避免重复下发同一个动作）
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
        self.add_header("电磁阀", "%d 路 · 1 路输出 + 2 路到位反馈"
                        % proto.HSF_VALVE_COUNT)

        # ---- 顶部：控制可用性提示 ----
        self.gate_label = QLabel("")
        self.gate_label.setWordWrap(True)
        self.gate_label.setStyleSheet(
            "QLabel { color: #B26500; background-color: #FFF4E5;"
            " border: 1px solid #FFD8A8; border-radius: 4px;"
            " padding: 6px 10px; font-size: 12px; }")
        self.gate_label.setVisible(False)
        self.root.addWidget(self.gate_label)

        # ---- 卡片网格 ----
        grid_host = QWidget()
        grid = QGridLayout(grid_host)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(12)
        self.cards = []
        for index in range(proto.HSF_VALVE_COUNT):
            card = ValveCard(index, self._request_control)
            grid.addWidget(card, index // self.COLUMNS,
                           index % self.COLUMNS)
            self.cards.append(card)
        self.root.addWidget(grid_host)
        self.root.addStretch(1)

        #: 最近一次收到的 6 路状态（dict: index -> item dict）
        self._items = {}

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
                            % (card.name_label.text(),
                               "开阀" if want_on else "关阀"))

        def _done(code, dtype, data):
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                doc = {}
            if doc.get("ok"):
                self.ctx.set_status("%s 命令已受理"
                                    % card.name_label.text())
                self._apply_doc(doc)
            else:
                self.ctx.set_status("%s 操作失败：%s"
                                    % (card.name_label.text(),
                                       doc.get("err", "未知原因")))

        def _err(msg):
            self.ctx.set_status("%s 操作失败：%s"
                                % (card.name_label.text(), msg))

        self.ctx.bridge.request(proto.CMD_VALVE_CONTROL,
                                {"index": int(index), "on": bool(want_on)},
                                on_done=_done, on_error=_err)

    # ---- 数据落地 ----
    def _apply_doc(self, doc):
        """把服务端快照（含 v0_*/v1_* 前缀键）拆成每路 dict。"""
        count = int(doc.get("count", 0) or 0)
        items = {}
        for index in range(min(count, proto.HSF_VALVE_COUNT)):
            prefix = "v%d_" % index
            items[index] = {
                key[len(prefix):]: value
                for key, value in doc.items() if key.startswith(prefix)
            }
        self._items = items
        reason = self.ctx.control_blocked_reason()
        for index, card in enumerate(self.cards):
            card.apply_state(items.get(index, {}), reason)

        # 顶部提示
        if reason:
            self.gate_label.setText("控制已禁用：%s" % reason)
            self.gate_label.setVisible(True)
        else:
            unconfigured = sum(
                1 for item in items.values() if not item.get("ready", False))
            if unconfigured:
                self.gate_label.setText(
                    "%d 路点位未配置（服务端 config 的 valves.items 里补 "
                    "module/do/openDi/closeDi 即可启用）" % unconfigured)
                self.gate_label.setVisible(True)
            else:
                self.gate_label.setVisible(False)

    # ---- 周期刷新 ----
    def on_enter(self):
        """入页拉一次快照（不依赖周期推送的首帧时机）。"""
        def _done(code, dtype, data):
            from . import bson
            try:
                doc = bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return
            if doc.get("count") is not None:
                self._apply_doc(doc)

        self.ctx.bridge.request(proto.CMD_VALVE_SNAPSHOT, None, on_done=_done)

    def refresh(self):
        """推送只给整帧快照；这里只更新"闸门原因"（模式/权限可能随时变）。"""
        reason = self.ctx.control_blocked_reason()
        for index, card in enumerate(self.cards):
            item = self._items.get(index, {})
            card.apply_state(item, reason)
        if reason:
            self.gate_label.setText("控制已禁用：%s" % reason)
            self.gate_label.setVisible(True)
