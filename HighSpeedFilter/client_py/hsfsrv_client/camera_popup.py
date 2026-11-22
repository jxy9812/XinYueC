# -*- coding: utf-8 -*-
"""悬浮相机画面卡（标题栏相机徽章弹出）。

[用户口径 2026-11-22]
  · 标题栏相机徽章点击 ⇒ 弹出一张**可拖动**的悬浮卡，显示实时画面；
  · 再点徽章 ⇒ 关闭（本卡不设关闭按钮）；
  · **每次打开重新建、关闭即销毁**：相机主题只在卡片存在期间订阅，
    关闭立刻退订 ⇒ 后台不会一直传大流量视频帧。

[帧来源] 与相机页同一个 `model.camera`（bridge 在网络线程组帧/解码后的
最新一帧）⇒ 不多拉一份流。JPEG 模式走 `QImage.fromData`；ES 直传模式是
解码后的 RGB 裸帧 `QImage(data,w,h,w*3,Format_RGB888)`（与 pages_camera
同口径）。

[刷新节拍 200ms] 相机流 20fps，悬浮小窗没必要每帧重缩放（704×704 缩到
几百像素是纯 CPU）⇒ 按 model.camera.seq 变化 + 200ms 节拍贴图；画面
仍是最新帧。
"""

from PyQt5.QtCore import QPoint, QRectF, Qt, QTimer, pyqtSignal
from PyQt5.QtGui import QColor, QImage, QPainter, QPen, QPixmap
from PyQt5.QtWidgets import (QApplication, QLabel, QSizePolicy, QVBoxLayout,
                             QWidget)

from . import theme


class CameraPopupCard(QWidget):
    """无边框悬浮相机画面卡（按住任意处拖动）。"""

    closed = pyqtSignal()

    #: 画面刷新节拍（毫秒）。
    REFRESH_MS = 200

    def __init__(self, bridge, parent=None):
        QWidget.__init__(self, parent)
        self._bridge = bridge
        self._drag_offset = None
        self._resize = None
        self._qimage = None
        self._last_seq = -1
        self._last_size = (0, 0)

        self.setWindowFlags(Qt.Tool | Qt.FramelessWindowHint)
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        # [每次打开重新创建、关闭即销毁] 不留隐藏的顶层窗口对象。
        self.setAttribute(Qt.WA_DeleteOnClose, True)
        self.setWindowTitle("相机")
        self.setMouseTracking(True)
        self.setCursor(Qt.OpenHandCursor)
        self.resize(520, 340)
        self.setMinimumSize(260, 180)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(16, 12, 16, 14)
        lay.setSpacing(6)

        self.title = QLabel("相机")
        self.title.setStyleSheet(
            "QLabel { color: #1F4E79; font-size: 20px; font-weight: bold;"
            " background: transparent; }")
        # 拖动期间子控件不截鼠标 ⇒ 整卡可按住拖。
        self.title.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        lay.addWidget(self.title)

        self.image = QLabel("等待画面…")
        self.image.setAlignment(Qt.AlignCenter)
        self.image.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.image.setMinimumHeight(120)
        self.image.setStyleSheet(
            "QLabel { background: #0E1720; border: 1px solid %s;"
            " border-radius: 6px; color: #C5D8E8; font-size: 16px; }"
            % theme.CLR_BORDER_SOFT)
        self.image.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        lay.addWidget(self.image, 1)

        self.hint = QLabel("按住可拖动 · 拖边角缩放 · 再点顶部相机图标关闭")
        self.hint.setStyleSheet(theme.caption_label_qss())
        self.hint.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        lay.addWidget(self.hint)

        self._timer = QTimer(self)
        self._timer.setInterval(self.REFRESH_MS)
        self._timer.timeout.connect(self._refresh_frame)
        self._timer.start()

    # ------------------------------------------------------------------
    #                          显示位置
    # ------------------------------------------------------------------
    def show_near(self, anchor):
        """弹在锚点控件（标题栏相机徽章）下方并夹在可用屏幕内。"""
        try:
            g = anchor.mapToGlobal(QPoint(0, anchor.height()))
            screen = QApplication.desktop().availableGeometry(anchor)
        except Exception:                                   # noqa: BLE001
            g = QPoint(200, 80)
            screen = QApplication.desktop().availableGeometry()
        x = min(max(screen.left() + 8, g.x() - self.width() + anchor.width()),
                screen.right() - self.width() - 8)
        y = min(g.y() + 6, screen.bottom() - self.height() - 8)
        self.move(max(screen.left() + 8, x), max(screen.top() + 8, y))
        self.show()
        self.raise_()
        self.activateWindow()

    # ------------------------------------------------------------------
    #                          拖动 / 边缘缩放
    # ------------------------------------------------------------------
    #: 边缘命中带（px）：靠边按住即缩放，其余区域按住拖动。
    EDGE = 8

    def _hit_edge(self, pos):
        """返回 (left, right, top, bottom) 命中位。"""
        r = self.rect()
        m = self.EDGE
        return (pos.x() <= m, pos.x() >= r.right() - m,
                pos.y() <= m, pos.y() >= r.bottom() - m)

    @staticmethod
    def _edge_cursor(edge):
        left, right, top, bottom = edge
        if (left and top) or (right and bottom):
            return Qt.SizeFDiagCursor
        if (right and top) or (left and bottom):
            return Qt.SizeBDiagCursor
        if left or right:
            return Qt.SizeHorCursor
        if top or bottom:
            return Qt.SizeVerCursor
        return None

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            edge = self._hit_edge(event.pos())
            if any(edge):
                # [2026-11-22 用户「同时支持拉伸缩放」] 边缘按住 = 缩放；
                # 其余区域 = 拖动。最小尺寸由 setMinimumSize 兜底。
                self._resize = {"edge": edge, "geo": self.geometry(),
                                "pos": event.globalPos()}
                self.setCursor(self._edge_cursor(edge))
                event.accept()
                return
            self._drag_offset = (
                event.globalPos() - self.frameGeometry().topLeft())
            self.setCursor(Qt.ClosedHandCursor)
            event.accept()
            return
        QWidget.mousePressEvent(self, event)

    def mouseMoveEvent(self, event):
        if self._resize is not None and event.buttons() & Qt.LeftButton:
            st = self._resize
            g = st["geo"]
            dx = event.globalPos().x() - st["pos"].x()
            dy = event.globalPos().y() - st["pos"].y()
            left, right, top, bottom = st["edge"]
            x, y, w, h = g.x(), g.y(), g.width(), g.height()
            mw, mh = self.minimumWidth(), self.minimumHeight()
            if left:
                w = max(mw, g.width() - dx)
                x = g.x() + (g.width() - w)
            elif right:
                w = max(mw, g.width() + dx)
            if top:
                h = max(mh, g.height() - dy)
                y = g.y() + (g.height() - h)
            elif bottom:
                h = max(mh, g.height() + dy)
            self.setGeometry(x, y, w, h)
            event.accept()
            return
        if self._drag_offset is not None and event.buttons() & Qt.LeftButton:
            self.move(event.globalPos() - self._drag_offset)
            event.accept()
            return
        cursor = self._edge_cursor(self._hit_edge(event.pos()))
        self.setCursor(cursor or Qt.OpenHandCursor)
        QWidget.mouseMoveEvent(self, event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self._drag_offset = None
            self._resize = None
            self.setCursor(Qt.OpenHandCursor)
            event.accept()
            return
        QWidget.mouseReleaseEvent(self, event)

    # ------------------------------------------------------------------
    #                          画面
    # ------------------------------------------------------------------
    def _decode(self, cam):
        if getattr(cam, "mode", "jpeg") == "es":
            img = QImage(cam.frame, cam.width, cam.height,
                         cam.width * 3, QImage.Format_RGB888)
        else:
            img = QImage.fromData(cam.frame, "JPG")
        if img.isNull():
            return None
        # [必须 copy] QImage 直接引用 cam.frame 的字节；下一帧 model 换
        # buffer 后原内存可能释放 ⇒ 悬浮卡再缩放就是读悬空指针。
        return img.copy()

    def _refresh_frame(self):
        if not self.isVisible():
            return
        cam = self._bridge.model.camera
        seq = int(getattr(cam, "seq", 0))
        size = (self.image.width(), self.image.height())
        if seq == self._last_seq and size == self._last_size:
            return
        if seq != self._last_seq:
            if not getattr(cam, "frame", b""):
                self.image.setPixmap(QPixmap())
                self.image.setText("等待画面…")
                self._last_seq = seq
                self._last_size = size
                return
            img = self._decode(cam)
            if img is None:
                self.image.setPixmap(QPixmap())
                self.image.setText("画面解码失败（帧不完整？）")
                self._last_seq = seq
                self._last_size = size
                return
            self._qimage = img
            self._last_seq = seq
        if self._qimage is not None and size[0] > 4 and size[1] > 4:
            scaled = self._qimage.scaled(
                size[0], size[1], Qt.KeepAspectRatio,
                Qt.SmoothTransformation)
            self.image.setPixmap(QPixmap.fromImage(scaled))
            self.image.setText("")
        self._last_size = size

    # ------------------------------------------------------------------
    #                          外观/关闭
    # ------------------------------------------------------------------
    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        r = QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5)
        p.setBrush(QColor("#FFFFFF"))
        pen = QPen(QColor(theme.CLR_BORDER_SOFT))
        pen.setWidthF(1.0)
        p.setPen(pen)
        p.drawRoundedRect(r, float(theme.RADIUS_CARD),
                          float(theme.RADIUS_CARD))
        p.end()

    def closeEvent(self, event):
        self._timer.stop()
        try:
            self.closed.emit()
        except RuntimeError:
            pass
        QWidget.closeEvent(self, event)
