# -*- coding: utf-8 -*-
"""真实启动路径验证：走 `python -m hsfsrv_client` 的完整 main()，确认
触摸模式下软键盘真的装配进主窗，且真实点控件能弹出正确键盘。

    python tools/kb_launch_check.py

与 kb_shot.py 的区别：那个手工建 MainWindow；这里走 __main__.main()
的真实参数解析 / 建窗 / 登录 / 软键盘装配链路——验证的是「有没有真的
接进客户端界面」，而不是「组件本身能不能用」。
"""

import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ["HSF_TOUCH"] = "1"          # 模拟触摸屏机器（真实部署形态）
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from PyQt5.QtCore import QTimer                    # noqa: E402
from PyQt5.QtTest import QTest                     # noqa: E402
from PyQt5.QtWidgets import QApplication           # noqa: E402
from PyQt5.QtCore import Qt                        # noqa: E402

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


def main():
    from hsfsrv_client.__main__ import main as entry
    from hsfsrv_client import main_window as mw_mod

    # [探针挂点] 必须等 QApplication 建好才能 startTimer；main() 里
    # QApplication 先于 MainWindow 创建，所以挂在主窗构造完成之后
    # （此时代码还在 main() 内部，事件循环尚未进入）。
    orig_init = mw_mod.MainWindow.__init__

    def patched_init(self, *a, **kw):
        orig_init(self, *a, **kw)
        QTimer.singleShot(1200, _probe)

    mw_mod.MainWindow.__init__ = patched_init

    rc = entry(["--auto", "127.0.0.1", "--port", "6200",
                "--size", "1280x800", "--seconds", "12"])
    print("main() 返回码：%s" % rc)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


def _probe():
    from hsfsrv_client.main_window import MainWindow
    wins = [w for w in QApplication.topLevelWidgets()
            if isinstance(w, MainWindow)]
    check("主窗已创建", bool(wins))
    if not wins:
        QApplication.quit()
        return
    win = wins[0]
    kb = getattr(win, "soft_kbd", None)
    check("真实启动路径下软键盘已装配", kb is not None)
    if kb is None:
        QApplication.quit()
        return

    from hsfsrv_client import pages_registry as reg
    win.goto_page(reg.PAGE_DATA_HISTORY)
    QApplication.processEvents()
    page = win.pages.get(reg.PAGE_DATA_HISTORY)
    entry_edit = getattr(page, "_f_text", None)
    check("历史页关键字框取到", entry_edit is not None)
    if entry_edit is not None:
        QTest.mouseClick(entry_edit, Qt.LeftButton,
                         pos=entry_edit.rect().center())
        QApplication.processEvents()
        check("真点关键字框 → 拼音键盘弹出",
              kb.isVisible() and kb._kind == "pinyin", kb._kind)

    win.goto_page(reg.PAGE_SETTINGS_LOCAL)
    QApplication.processEvents()
    lpage = win.pages.get(reg.PAGE_SETTINGS_LOCAL)
    spin = getattr(lpage, "refresh_spin", None)
    if spin is not None:
        QTest.mouseClick(spin, Qt.LeftButton, pos=spin.rect().center())
        QApplication.processEvents()
        check("真点数值框 → 数字键盘弹出",
              kb.isVisible() and kb._kind == "numeric",
              "%s/%s" % (kb._kind, kb._numeric_layout))
    QApplication.quit()


if __name__ == "__main__":
    sys.exit(main())
