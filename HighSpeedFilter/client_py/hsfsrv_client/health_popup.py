# -*- coding: utf-8 -*-
"""标题栏「设备健康」徽章点开的**异常设备详情卡**（2026-11-25 用户口径）。

现场原状：顶栏只在有故障/离线时显示「故障N / 离线N」，但**点不动** ⇒
操作员看到红字却不知道是哪台设备、什么原因。本卡把服务端设备健康快照
（`model.HealthSnapshot`）里的**非正常设备**逐条列出来：
设备名 / 类型 / 状态 / 原因 / 连接地址。

交互与 `qr_access.QrPopupCard` 同款：
  · 非模态、frameless 圆角卡，**Qt.Popup** ⇒ 点卡片外部任何地方自动关闭；
  · 右上角 × 也可关；Esc 同（Qt.Popup 内建）。
  · 打开期间主窗 200ms 刷新会调 :meth:`refresh_if_changed` —— 服务端健康
    序号变了才重建行（稳态零重排）。

[为什么单独一个文件] 标题栏在任意页面都要能用（与二维码/相机徽章同款），
不能挂在某个页面实例上；弹卡自持 model 引用，不依赖 HomePage。
"""
from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QFrame, QHBoxLayout, QLabel, QPushButton,
                             QScrollArea, QSizePolicy, QVBoxLayout, QWidget)
try:                                            # Qt5：惯性滚动（触摸）
    from PyQt5.QtWidgets import QScroller
except ImportError:                             # pragma: no cover
    QScroller = None

from . import theme
from .model import HEALTH_TEXT, KIND_TEXT, slim_addr

__all__ = ["HealthPopupCard"]

#: 健康状态 → 文字色（正常不列；故障/离线用红，过期/告警用橙）。
_STATUS_COLORS = {
    1: "#B26A00",      # 告警
    2: "#B26A00",      # 数据过期
    3: "#C62828",      # 离线
    4: "#C62828",      # 故障
}


class HealthPopupCard(QWidget):
    """设备健康详情弹卡（非模态，点外部关闭）。"""

    def __init__(self, model, parent=None):
        QWidget.__init__(self, parent)
        self._model = model
        self._seq = -1
        self.setWindowTitle("设备健康")
        # [点卡片外部关闭] 与二维码弹卡同一口径（Qt.Popup 的标准交互）。
        self.setWindowFlags(Qt.Popup | Qt.FramelessWindowHint
                            | Qt.WindowStaysOnTopHint)
        self.setObjectName("HsfHealthCard")
        self.setAttribute(Qt.WA_StyledBackground, True)
        self._hsf_extra_qss = (
            "#HsfHealthCard { background: %s; border: 1px solid %s;"
            " border-radius: 10px; }"
            % (theme._tok("white"), theme._tok("border_soft")))
        theme.apply_window(self)

        root = QVBoxLayout(self)
        root.setContentsMargins(14, 10, 14, 12)
        root.setSpacing(8)

        # ---- 标题行：标题 + 汇总 + × ----
        head = QHBoxLayout()
        title = QLabel("设备健康")
        title.setStyleSheet(
            "QLabel { color: %s; font-size: 20px; font-weight: bold;"
            " background: transparent; }" % theme._tok("title_text"))
        head.addWidget(title)
        self.summary = QLabel("")
        self.summary.setStyleSheet(theme.caption_label_qss())
        head.addWidget(self.summary)
        head.addStretch(1)
        close_btn = QPushButton("×")
        close_btn.setFixedSize(28, 28)
        close_btn.setCursor(Qt.PointingHandCursor)
        # 28px 小按钮必须内联 padding:0（大 QSS 的胶囊 padding 会把 × 挤没，
        # 与 qr_access 的 2026-11-19 实锅同款）。
        close_btn.setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " padding: 0px; color: %s; font-size: 22px; }"
            "QPushButton:hover { background-color: %s; border-radius: 14px; }"
            % (theme._tok("text_dim"), theme._tok("hover_bg")))
        close_btn.clicked.connect(self.close)
        head.addWidget(close_btn)
        root.addLayout(head)

        self.hint = QLabel("")
        self.hint.setStyleSheet(theme.caption_label_qss())
        root.addWidget(self.hint)

        # ---- 可滚动行区（设备最多十几条，窄屏/放大字号也要能看全）----
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setFrameShape(QFrame.NoFrame)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self._scroll.setStyleSheet("QScrollArea { background: transparent; }")
        self._rows_host = QWidget()
        self._rows_host.setStyleSheet("background: transparent;")
        self._rows = QVBoxLayout(self._rows_host)
        self._rows.setContentsMargins(0, 0, 0, 0)
        self._rows.setSpacing(6)
        self._scroll.setWidget(self._rows_host)
        self._scroll.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        root.addWidget(self._scroll, 1)
        # 触摸拖动滚动：Qt5 默认 QScrollArea 只认滚轮；板端是触摸屏。
        if QScroller is not None:
            QScroller.grabGesture(self._scroll.viewport(),
                                  QScroller.LeftMouseButtonGesture)

        self._empty = None

    # ------------------------------------------------------------------
    def refresh(self):
        """按当前健康快照重建行（只列**非正常**设备）。"""
        health = getattr(self._model, "health", None)
        devices = list(getattr(health, "devices", []) or [])
        bad = [d for d in devices if int(getattr(d, "health", 0)) != 0]
        self._seq = int(getattr(health, "seq", -1) or -1)
        self.summary.setText(self._summary_text(bad))
        self.hint.setText(
            "点击卡片外部任意位置即可关闭" if bad
            else "服务端当前没有异常设备（全部正常）")

        # 清空旧行（含末尾 stretch）
        while self._rows.count():
            item = self._rows.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()
        self._empty = None

        if not bad:
            self._empty = QLabel("全部设备正常")
            self._empty.setAlignment(Qt.AlignCenter)
            self._empty.setStyleSheet(
                "QLabel { color: %s; font-size: 18px; padding: 24px 8px;"
                " background: transparent; }" % theme._tok("text_dim"))
            self._rows.addWidget(self._empty)
            self._rows.addStretch(1)
            return

        # 故障/离线排前面，同状态按名称稳定排序（现场一眼先看红的）。
        bad.sort(key=lambda d: (-int(getattr(d, "health", 0)),
                                str(getattr(d, "name", ""))))
        for dev in bad:
            self._rows.addWidget(self._make_row(dev))
        self._rows.addStretch(1)

    def refresh_if_changed(self):
        """健康序号变了才重建（主窗 200ms 刷新调用；稳态零成本）。"""
        health = getattr(self._model, "health", None)
        seq = int(getattr(health, "seq", -1) or -1)
        if seq != self._seq:
            self.refresh()

    @staticmethod
    def _summary_text(bad):
        fault = sum(1 for d in bad if int(getattr(d, "health", 0)) == 4)
        offline = sum(1 for d in bad if int(getattr(d, "health", 0)) == 3)
        stale = sum(1 for d in bad if int(getattr(d, "health", 0)) in (1, 2))
        parts = []
        if fault:
            parts.append("故障%d" % fault)
        if offline:
            parts.append("离线%d" % offline)
        if stale:
            parts.append("告警/过期%d" % stale)
        return " · ".join(parts)

    def _make_row(self, dev):
        """一条设备：状态 + 名称 + 类型 / 原因 / 地址。"""
        health = int(getattr(dev, "health", 0))
        color = _STATUS_COLORS.get(health, "#B26A00")
        row = QFrame()
        row.setObjectName("HsfHealthRow")
        row.setStyleSheet(
            "QFrame#HsfHealthRow { background: transparent;"
            " border-left: 4px solid %s; border-radius: 4px; }" % color)
        lay = QVBoxLayout(row)
        lay.setContentsMargins(10, 6, 8, 6)
        lay.setSpacing(2)

        line1 = QHBoxLayout()
        line1.setSpacing(8)
        status = QLabel(HEALTH_TEXT.get(health, "未知"))
        status.setStyleSheet(
            "QLabel { color: %s; font-size: 17px; font-weight: bold;"
            " background: transparent; }" % color)
        line1.addWidget(status)
        name = QLabel(str(getattr(dev, "name", "") or "未命名设备"))
        name.setStyleSheet(
            "QLabel { color: %s; font-size: 18px; font-weight: bold;"
            " background: transparent; }" % theme._tok("title_text"))
        line1.addWidget(name)
        kind = QLabel(KIND_TEXT.get(int(getattr(dev, "kind", 0)), "未知"))
        kind.setStyleSheet(
            "QLabel { color: %s; font-size: 15px; background: transparent; }"
            % theme._tok("text_dim"))
        line1.addStretch(1)
        line1.addWidget(kind)
        lay.addLayout(line1)

        detail = str(getattr(dev, "text", "") or "").strip()
        err = int(getattr(dev, "error", 0) or 0)
        if err:
            detail = ("%s（错误码 %d）" % (detail, err)) if detail \
                else ("错误码 %d" % err)
        if not detail:
            detail = "无详细说明"
        dlabel = QLabel(detail)
        dlabel.setWordWrap(True)
        dlabel.setStyleSheet(
            "QLabel { color: %s; font-size: 16px; background: transparent; }"
            % theme._tok("text_dim"))
        lay.addWidget(dlabel)

        addr = str(getattr(dev, "addr", "") or "").strip()
        if addr:
            alabel = QLabel("地址：%s" % (slim_addr(addr) or addr))
            alabel.setToolTip(addr)
            alabel.setStyleSheet(
                "QLabel { color: %s; font-size: 14px;"
                " background: transparent; }" % theme._tok("text_dim"))
            lay.addWidget(alabel)
        return row

    # ------------------------------------------------------------------
    def popup(self, anchor=None):
        """贴着标题栏健康徽章下方弹出（已开则前置）。"""
        theme.apply_window(self)
        if anchor is not None and not self.isVisible():
            # 宽度按内容 + 屏幕夹取；高度最多约 2/3 屏（超出用滚动）。
            screen = anchor.screen() if hasattr(anchor, "screen") else None
            avail = screen.availableGeometry() if screen is not None else None
            max_w = (avail.width() - 40) if avail is not None else 680
            max_h = (avail.height() - 80) if avail is not None else 640
            self.setFixedWidth(max(460, min(680, max_w)))
            self.refresh()
            self._scroll.setMaximumHeight(max(180, int(max_h * 0.62)))
            self.adjustSize()
            self.setFixedHeight(min(self.sizeHint().height(), max_h))
            pt = anchor.mapToGlobal(anchor.rect().bottomLeft())
            x = pt.x() + anchor.width() - self.width()
            y = pt.y() + 6
            if avail is not None:
                x = max(avail.left() + 8,
                        min(x, avail.right() - self.width() - 8))
                y = max(avail.top() + 8,
                        min(y, avail.bottom() - self.height() - 8))
            self.move(x, y)
        else:
            self.refresh()
        self.show()
        self.raise_()
        self.activateWindow()
