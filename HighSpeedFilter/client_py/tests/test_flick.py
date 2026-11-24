# -*- coding: utf-8 -*-
"""触摸/鼠标拖动 + 惯性 滚动自检（2026-11-14 用户口径「长窗口，上下滑动移动」）。

[测什么]
    ① 内容超出一屏时，**拖动**能滚（滚动条位置变了）⇒ 长窗口可以用手指
       上下滑动（触摸事件走同一条数学路径，鼠标路径可合成 ⇒ 可自动化）；
    ② 松手后有**惯性**（滚动条继续动了一段）⇒ 不是"松手即停"；
    ③ 点击（位移小于阈值）**不**触发拖动 ⇒ 按钮/下拉照常点得动。

[怎么跑]
    python -I tests/test_flick.py
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PyQt5.QtCore import QEvent, QPoint, QPointF, Qt      # noqa: E402
from PyQt5.QtGui import QMouseEvent                    # noqa: E402
from PyQt5.QtTest import QTest                         # noqa: E402
from PyQt5.QtWidgets import QApplication               # noqa: E402

from hsfsrv_client import pages_registry as reg        # noqa: E402

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _pump(app, ms=30):
    """跑事件循环 ms 毫秒（让惯性定时器走几帧）。"""
    import time
    end = time.time() + ms / 1000.0
    while time.time() < end:
        app.processEvents()
        time.sleep(0.005)


def main():
    global PASS, FAIL
    app = QApplication.instance() or QApplication(sys.argv[:1])

    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    win = MainWindow(LinkBridge())
    win.resize(900, 700)
    win.show()
    app.processEvents()

    # 远端设置页内容最高（长窗口场景就它）。
    win.goto_page(reg.PAGE_SETTINGS_REMOTE)
    for _ in range(5):
        app.processEvents()
    page = win.stack.currentWidget()
    check("远端设置页已打开", page is not None, "")
    if page is None:
        print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
        return 1

    vp = page.scroll.viewport()
    bar = page.scroll.verticalScrollBar()
    check("内容超出一屏（有得滚）", bar.maximum() > 0,
          "滚动范围 0..%d" % bar.maximum())
    check("页面挂了拖动滚动器", getattr(page, "_scroller", None) is not None, "")

    # ---- ① 拖动 ⇒ 滚动条移动 ----
    # [为什么自己造 QMouseEvent 而不用 QTest.mouseMove] QTest.mouseMove
    # 合成的事件**不带按键状态**（buttons() 为空）⇒ 控件自己会忽略这批
    # 移动 ⇒ 事件到不了"按住拖动"这条路径（实测只到 1 次 move）。
    # ⇒ 显式造事件（buttons=LeftButton）直接投递给视口 ⇒ 走的正是
    # 真机触摸/鼠标的同一条路径（视口的事件过滤器）。
    def _mouse(kind, pos, buttons):
        ev = QMouseEvent(kind, QPointF(pos.x(), pos.y()),
                         QPointF(vp.mapToGlobal(pos).x(),
                                 vp.mapToGlobal(pos).y()),
                         Qt.LeftButton, buttons, Qt.NoModifier)
        QApplication.sendEvent(vp, ev)

    bar.setValue(0)
    app.processEvents()
    x = max(20, vp.width() // 2)
    y0 = max(20, vp.height() // 2)
    _mouse(QEvent.MouseButtonPress, QPoint(x, y0), Qt.LeftButton)
    for i in range(1, 8):
        _mouse(QEvent.MouseMove, QPoint(x, y0 - i * 12), Qt.LeftButton)
        app.processEvents()
    after_drag = bar.value()
    _mouse(QEvent.MouseButtonRelease, QPoint(x, y0 - 7 * 12), Qt.NoButton)
    app.processEvents()
    check("① 拖动 ⇒ 内容跟着滚（滚动条位置变化）", after_drag > 0,
          "拖动后 scrollbar=%d" % after_drag)

    # ---- ② 松手后有惯性 ----
    before = bar.value()
    _pump(app, 120)
    after = bar.value()
    check("② 松手后有惯性（继续滚了一段）", after > before,
          "松手时=%d → 惯性后=%d" % (before, after))

    # ---- ③ 小位移不算拖动（点击行为不受影响）----
    bar.setValue(0)
    app.processEvents()
    _mouse(QEvent.MouseButtonPress, QPoint(x, y0), Qt.LeftButton)
    _mouse(QEvent.MouseMove, QPoint(x, y0 + 2), Qt.LeftButton)
    app.processEvents()
    _mouse(QEvent.MouseButtonRelease, QPoint(x, y0 + 2), Qt.NoButton)
    app.processEvents()
    _pump(app, 60)
    check("③ 小位移（点击）不触发拖动 ⇒ 滚动条不动", bar.value() == 0,
          "scrollbar=%d" % bar.value())

    # ---- ④ 导航面板（2026-11-23 用户口径「导航支持下触摸拖动」）----
    _nav_case(app)

    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


def _nav_case(app):
    """导航面板：拖动/惯性 + 拖动后误点抑制。

    [为什么要单独测] 导航条目是**按钮**（不是列表项）：触摸拖动后系统会补
    一对合成鼠标 press/release，按钮照常发 clicked ⇒ 松手会误点"正拖过的
    那一页"。⇒ 面板在拖动结束后的 250ms 窗口里忽略条目点击（此处直测闸门
    逻辑；拖动/惯性本身走与页面同一条 FlickScroller 数学路径）。
    """
    from PyQt5.QtWidgets import QScrollArea
    from hsfsrv_client.widgets import NavPanel

    print("\n== ④ 导航面板：拖动 + 惯性 + 误点抑制 ==")
    panel = NavPanel(reg.NAV_TREE)
    panel.resize(240, 300)
    panel.show()
    for _ in range(5):
        app.processEvents()

    scroll = panel.findChild(QScrollArea, "HsfNavScroll")
    check("导航面板有滚动区", scroll is not None, "")
    if scroll is None:
        panel.close()
        return
    bar = scroll.verticalScrollBar()
    vp = scroll.viewport()
    check("导航内容超出一屏（有得滚）", bar.maximum() > 0,
          "滚动范围 0..%d" % bar.maximum())
    check("导航挂了拖动滚动器", getattr(panel, "_flick", None) is not None, "")

    def _mouse(kind, pos, buttons):
        ev = QMouseEvent(kind, QPointF(pos.x(), pos.y()),
                         QPointF(vp.mapToGlobal(pos).x(),
                                 vp.mapToGlobal(pos).y()),
                         Qt.LeftButton, buttons, Qt.NoModifier)
        QApplication.sendEvent(vp, ev)

    bar.setValue(0)
    app.processEvents()
    x = max(20, vp.width() // 2)
    y0 = max(20, vp.height() // 2)
    _mouse(QEvent.MouseButtonPress, QPoint(x, y0), Qt.LeftButton)
    for i in range(1, 8):
        _mouse(QEvent.MouseMove, QPoint(x, y0 - i * 12), Qt.LeftButton)
        app.processEvents()
    after_drag = bar.value()
    _mouse(QEvent.MouseButtonRelease, QPoint(x, y0 - 7 * 12), Qt.NoButton)
    app.processEvents()
    check("导航：拖动 ⇒ 内容跟着滚", after_drag > 0,
          "拖动后 scrollbar=%d" % after_drag)

    before = bar.value()
    _pump(app, 120)
    check("导航：松手后有惯性（继续滚一段）", bar.value() > before,
          "松手时=%d → 惯性后=%d" % (before, bar.value()))

    # ---- 触摸分支（与真机同一条数学路径）----
    # [为什么用假事件] PyQt5 **造不出 QTouchEvent**（QTouchEvent.TouchPoint
    # "cannot be instantiated or sub-classed"——与 QScroller 同类限制）⇒
    # 这里直接喂一个只有 type()/touchPoints() 的轻量对象给过滤器，覆盖
    # TouchBegin→TouchUpdate×N→TouchEnd 这条分支（板端真机走的就是它；
    # 鼠标分支上面已覆盖）。事件投递（按钮→视口冒泡）属 Qt 平台行为，
    # 只能上板用 evdev 注入真手指验证。
    class _P(object):
        def __init__(self, y):
            self._y = y

        def pos(self):
            return QPointF(0.0, self._y)

    class _FakeTouch(object):
        def __init__(self, kind, y):
            self._kind = kind
            self._y = y

        def type(self):
            return self._kind

        def touchPoints(self):
            return [_P(self._y)]

    bar.setValue(0)
    app.processEvents()
    fl = panel._flick
    fl.eventFilter(vp, _FakeTouch(QEvent.TouchBegin, y0))
    for i in range(1, 8):
        fl.eventFilter(vp, _FakeTouch(QEvent.TouchUpdate, y0 - i * 12))
    after_touch = bar.value()
    fl.eventFilter(vp, _FakeTouch(QEvent.TouchEnd, y0 - 7 * 12))
    app.processEvents()
    check("导航：触摸拖动（TouchBegin/Update/End 分支）也能滚",
          after_touch > 0, "触摸拖动后 scrollbar=%d" % after_touch)
    before_t = bar.value()
    _pump(app, 120)
    check("导航：触摸松手后有惯性", bar.value() > before_t,
          "松手时=%d → 惯性后=%d" % (before_t, bar.value()))
    fl.stop()

    hits = []
    panel.navigate.connect(lambda i: hits.append(i))
    panel._nav_drag_begin()
    panel._on_item_clicked(2)
    check("导航：拖动后闸内的条目点击被忽略", hits == [], "hits=%s" % hits)
    panel._nav_guard_clear()
    panel._on_item_clicked(2)
    check("导航：闸放开后条目点击正常", hits == [2], "hits=%s" % hits)
    panel.close()


if __name__ == "__main__":
    sys.exit(main())
