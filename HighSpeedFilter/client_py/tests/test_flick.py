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

    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
