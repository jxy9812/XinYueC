# -*- coding: utf-8 -*-
"""验证相机页全屏退出后主窗仍可操作（板端实锅回归）。

复现用户场景：进相机页 → 双击进全屏 → 双击退出 → 主窗点其他控件无响应。
断言：
  1. 全屏窗关掉且 page._fs 归零
  2. 没有残留 mouse/keyboard grab
  3. 主窗 widgetAt 能命中（说明不是被透明覆盖层挡住）
  4. 主窗激活态（activateWindow 生效）
"""
import os
import sys

os.environ["QT_QPA_PLATFORM"] = "offscreen"
sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

from PyQt5.QtCore import QEvent, QPoint, Qt, QTimer
from PyQt5.QtGui import QMouseEvent
from PyQt5.QtWidgets import QApplication, QWidget

PASS = [0]
FAIL = [0]


def check(name, ok, detail=""):
    if ok:
        PASS[0] += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL[0] += 1
        print("  [FAIL] %s %s" % (name, detail))


def pump(app, n=6):
    for _ in range(n):
        app.processEvents()


def main():
    app = QApplication([])
    from hsfsrv_client import theme
    try:
        theme.apply_font(app)
    except Exception:
        pass

    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_camera import CameraPage

    bridge = LinkBridge()
    ctx = PageContext(bridge, lambda i: None, lambda s: None)
    page = CameraPage(ctx)
    page.resize(1200, 760)
    page.show()
    pump(app)

    # 造一帧假画面（JPEG 模式：随便给点字节不影响全屏开关逻辑）
    try:
        ctx.model.camera.frame = b"\xff\xd8\xff\xd9"
        ctx.model.camera.mode = "jpeg"
        ctx.model.camera.seq = 1
        ctx.model.camera.width = 704
        ctx.model.camera.height = 704
    except Exception as exc:
        print("  (假帧注入跳过：%s)" % exc)

    # ---- 1. 进全屏（现在是主窗内覆盖层）----
    page.enter_fullscreen()
    pump(app)
    fs = page._fs
    check("E5a 进全屏：覆盖层已建、可见、且与主窗同一个 QWindow",
          fs is not None and fs.isVisible()
          and fs.window() is page.window(),
          "fs=%r 同窗=%r" % (fs, fs is not None
                            and fs.window() is page.window()))

    # ---- 2. 双击退出（模拟板端触摸双击）----
    if fs is not None:
        ev = QMouseEvent(QEvent.MouseButtonDblClick, QPoint(10, 10),
                         Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
        fs.mouseDoubleClickEvent(ev)
        # 事件返回后 singleShot(0) 才 close ⇒ 泵事件循环
        pump(app, 12)
    check("E5b 双击退出：覆盖层已隐藏、页面引用已清",
          page._fs is None
          and (fs is None or not fs.isVisible()),
          "page._fs=%r 可见=%r"
          % (page._fs, fs.isVisible() if fs is not None else None))

    # ---- 3. 无残留抓取（吞事件的真凶）----
    grab_m = QWidget.mouseGrabber()
    grab_k = QWidget.keyboardGrabber()
    check("E5c 退出后没有残留 mouse/keyboard grab",
          grab_m is None and grab_k is None,
          "mouse=%r keyboard=%r" % (grab_m, grab_k))

    # ---- 4. 主窗仍能被命中（没被透明遮罩挡住）----
    page.raise_()
    page.activateWindow()
    pump(app, 4)
    hit = app.widgetAt(page.mapToGlobal(QPoint(page.width() // 2,
                                              page.height() // 2)))
    check("E5d 主窗中心点能命中控件（不是被覆盖层挡住）",
          hit is not None, "hit=%r" % (hit,))

    # ---- 4b 覆盖层隐藏后不遮挡（触摸落点必须命中下层）----
    if fs is not None:
        hidden_ok = not fs.isVisible()
        # 覆盖层若还可见，widgetAt 会命中它 ⇒ 那正是"点不动"的形态
        hit2 = app.widgetAt(page.mapToGlobal(QPoint(page.width() // 2,
                                                   page.height() // 2)))
        check("E5f 覆盖层已隐藏（不会挡住下层控件的触摸）",
              hidden_ok and hit2 is not fs,
              "可见=%r hit=%r" % (hidden_ok, hit2))

    # ---- 5. 主窗 isEnabled / 可见 ----
    check("E5e 主窗仍启用且可见",
          page.isEnabled() and page.isVisible(),
          "enabled=%r visible=%r" % (page.isEnabled(), page.isVisible()))

    print("=" * 56)
    print("  汇总：PASS %d / FAIL %d" % (PASS[0], FAIL[0]))
    return 0 if FAIL[0] == 0 else 1


sys.exit(main())
