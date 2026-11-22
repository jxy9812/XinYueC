# -*- coding: utf-8 -*-
"""触摸/鼠标**拖动 + 惯性**滚动器（2026-11-14 用户口径「客户端版本有做触摸
手势适配吗，特别是长窗口，上下滑动移动」）。

[为什么自己写]
==============
板端是触摸屏；长窗口（竖屏 800×1280）里内容常常超出一屏 ⇒ 必须能**用手指
上下拖**来滚。

* `QScrollArea` **原生支持触摸拖动**（QAbstractScrollArea 的 viewportEvent
  处理 Touch*）⇒ 「拖得动」这件事本来就有；
* 但**没有惯性**（松手即停，长距离滑动要拖很多次）；
* 而 **PyQt5 不导出 `QScroller`**（`create()` 没有，直接 `QScroller(...)`
  报 "cannot be instantiated or sub-classed"）⇒ 没法用现成的。

⇒ 这里自己实现一小段：按下记起点 → 移动按位移反向滚 → 松开按最近速度做
**衰减**滚动（惯性）。

[交互细节]
==========
* **先不抢事件**：按下时不拦截（让按钮/下拉正常响应）⇒ 只有**移动超过阈值**
  （`DRAG_THRESHOLD_PX`）才判定为"拖动"并接管 ⇒ 点击行为不受影响；
* 接管后把移动事件**吃掉**（accept）⇒ 免得下拉框同时在滚；
* 触摸事件用 `QTouchEvent.touchPoints()` 取点位（PyQt5 有）；
* 惯性用 16ms 定时器，速度每帧按 `DECAY` 衰减，低于 `MIN_SPEED` 停。

[自检]
======
`tests/test_flick.py`：用**合成鼠标事件**（QTest）走同一条数学路径 ⇒
断言「拖动 ⇒ 滚动条动了」与「松手 ⇒ 惯性继续滚了一段」。
"""

from PyQt5.QtCore import QEvent, QObject, QPointF, QTimer, Qt
from PyQt5.QtGui import QMouseEvent

#: 判定为"拖动"的最小位移（px）。小于它算点击（不抢事件）。
DRAG_THRESHOLD_PX = 6
#: 惯性定时器间隔（ms）。
TICK_MS = 16
#: 每帧速度衰减（越小停得越快）。
DECAY = 0.92
#: 低于这个速度（px/帧）就停止惯性。
MIN_SPEED = 0.4
#: 记录最近几次移动，用来算速度。
SPEED_SAMPLES = 5


class FlickScroller(QObject):
    """拖动 + 惯性滚动器（挂在 QScrollArea 上：滚动条在 area、事件目标在视口）。"""

    def __init__(self, area, parent=None):
        """
        :param area: QScrollArea（**滚动条在它身上**，视口只是事件目标 ⇒
            别把 viewport 当 area 传——viewport 没有 verticalScrollBar()）。
        """
        QObject.__init__(self, parent)
        self._area = area
        self._vp = area.viewport()
        self._bar = area.verticalScrollBar()
        self._down_y = None
        self._last_y = None
        self._dragging = False
        self._samples = []          # [(时间, y)]
        self._speed = 0.0
        self._timer = QTimer(self)
        self._timer.setInterval(TICK_MS)
        self._timer.timeout.connect(self._on_tick)
        self._vp.installEventFilter(self)

    # ---- 工具 ----
    @staticmethod
    def _event_pos(event):
        """取事件的 y 坐标（鼠标/触摸统一）。"""
        if event.type() in (QEvent.TouchBegin, QEvent.TouchUpdate,
                            QEvent.TouchEnd):
            pts = event.touchPoints()
            if pts:
                return float(pts[0].pos().y())
            return None
        return float(event.pos().y())

    def _scroll_by(self, dy):
        """按位移滚动（手指往上拖 dy<0 ⇒ 内容往上 ⇒ 滚动条增大）。"""
        bar = self._bar
        bar.setValue(bar.value() - int(round(dy)))

    # ---- 事件 ----
    def eventFilter(self, obj, event):
        et = event.type()
        if et == QEvent.MouseButtonPress:
            if event.button() == Qt.LeftButton:
                self._begin(self._event_pos(event))
            return False
        if et == QEvent.MouseMove:
            y = self._event_pos(event)
            if self._down_y is not None and y is not None:
                if self._maybe_drag(y):
                    return True          # 接管：吃掉这次移动
            return False
        if et == QEvent.MouseButtonRelease:
            if self._down_y is not None and event.button() == Qt.LeftButton:
                self._end()
            return False
        if et == QEvent.TouchBegin:
            y = self._event_pos(event)
            if y is not None:
                self._begin(y)
            return False
        if et == QEvent.TouchUpdate:
            y = self._event_pos(event)
            if self._down_y is not None and y is not None:
                if self._maybe_drag(y):
                    return True
            return False
        if et == QEvent.TouchEnd:
            if self._down_y is not None:
                self._end()
            return False
        return False

    # ---- 拖动状态机 ----
    def _begin(self, y):
        self._timer.stop()
        self._speed = 0.0
        self._samples = []
        self._down_y = y
        self._last_y = y
        self._dragging = False

    def _maybe_drag(self, y):
        """移动处理：超过阈值才判定为拖动。返回 True = 本次事件被接管。"""
        if self._last_y is None:
            self._last_y = y
            return False
        dy = y - self._last_y
        self._last_y = y
        if not self._dragging:
            if self._down_y is None:
                return False
            if abs(y - self._down_y) < DRAG_THRESHOLD_PX:
                return False          # 还没到阈值 ⇒ 当作点击，不抢
            self._dragging = True
        self._scroll_by(dy)
        self._samples.append((self._now_ms(), y))
        if len(self._samples) > SPEED_SAMPLES:
            self._samples.pop(0)
        return True

    def _end(self):
        """松手：按最近移动算速度，起惯性。

        [速度的符号] 用**手指**的速度（手指往上 = 负 y 方向 = 负值）⇒
        惯性帧里直接把它当"手指位移"喂给 `_scroll_by` ⇒ 方向自然一致
        （早先写成 (y0-y1) ⇒ 手指往上算出正值 ⇒ 惯性往回滚，方向反了）。
        """
        if self._dragging and len(self._samples) >= 2:
            (t0, y0) = self._samples[0]
            (t1, y1) = self._samples[-1]
            dt = max(1.0, float(t1 - t0))
            self._speed = (y1 - y0) / dt * 16.0     # px/帧（16ms 一帧）
        else:
            self._speed = 0.0
        self._down_y = None
        self._last_y = None
        self._dragging = False
        if abs(self._speed) >= MIN_SPEED:
            self._timer.start()
        else:
            self._timer.stop()

    def _on_tick(self):
        """惯性一帧：按速度滚动并衰减。"""
        if abs(self._speed) < MIN_SPEED:
            self._timer.stop()
            self._speed = 0.0
            return
        self._scroll_by(self._speed)
        self._speed *= DECAY

    @staticmethod
    def _now_ms():
        import time
        return int(time.monotonic() * 1000)

    # ---- 供测试/诊断 ----
    def is_dragging(self):
        return self._dragging

    def speed(self):
        return self._speed

    def stop(self):
        """立刻停止惯性（换页/主题切换时用）。"""
        self._timer.stop()
        self._speed = 0.0
        self._down_y = None
        self._last_y = None
        self._dragging = False
