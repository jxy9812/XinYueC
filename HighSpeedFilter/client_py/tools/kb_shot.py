# -*- coding: utf-8 -*-
"""软键盘联调取证：真实客户端 + 真实服务端（模拟器）跑一遍三类键盘。

    python tools/kb_shot.py                       # 连本机 6200 模拟器
    python tools/kb_shot.py --port 6200 --out /tmp/kb

做的事（全在离屏跑，不抢前台、不动用户桌面）：
  1. 起真实 MainWindow + LinkBridge，连服务端（--auto 等价路径）；
  2. 切到历史数据页，**真点**关键字输入框 → 断言拼音键盘弹出；
  3. 逐键点 nihao → 断言候选带出现「你好」→ 空格上屏，读回输入框文本；
  4. 切到本地设置页，真点「刷新周期」QSpinBox → 断言切纯数字盘，
     点 600 → 断言 spinbox.value()==600；
  5. 点登录框不可用（主窗已登录），故字母键盘用离屏 QLineEdit 断言；
  6. 每步把「主窗 + 软键盘面板」合成一张 PNG（键盘是独立顶层窗口，
     单抓主窗看不到它）。

为什么合成截图：软键盘是顶层 Tool 窗口，`win.render()` 抓不到；板端
`QScreen.grabWindow(0)` 能整屏抓，但离屏平台没有真实屏幕。两窗各自
render 后按实际几何贴到一张画布上，得到的画面与真机一致（键盘贴在主窗
底部就是 reposition() 算出的位置）。
"""

import argparse
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from PyQt5.QtCore import QPoint, Qt, QTimer               # noqa: E402
from PyQt5.QtGui import QPainter, QPixmap                  # noqa: E402
from PyQt5.QtWidgets import QApplication, QPushButton, QSpinBox  # noqa: E402

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


def composite(win, kb, path):
    """主窗 + 软键盘面板按真实几何合成一张图。"""
    base = QPixmap(win.size())
    base.fill(Qt.white)
    win.render(base)
    if kb.isVisible():
        panel = QPixmap(kb.size())
        panel.fill(Qt.white)
        kb.render(panel)
        # 键盘停靠在主窗底部：y = 主窗高 - 键盘高（reposition 的口径）。
        y = max(0, base.height() - panel.height())
        painter = QPainter(base)
        painter.drawPixmap(0, y, panel)
        painter.end()
    ok = base.save(path)
    print("  截图: %s (%dx%d) %s"
          % (path, base.width(), base.height(), "" if ok else "[保存失败]"))
    return ok


def press_label(kb, label):
    """在键区里找文本为 label 的按键并点击（等价手指戳）。

    [遍历口径] 键区是「行外框(居中) → 行 → 键」的嵌套布局，按键是布局的
    后代而非直接子项，必须递归收集。
    """
    found = []

    def walk(layout):
        for i in range(layout.count()):
            item = layout.itemAt(i)
            sub = item.layout()
            if sub is not None:
                walk(sub)
                continue
            w = item.widget()
            if isinstance(w, QPushButton) and w.text() == label:
                found.append(w)

    walk(kb._rows_box)
    if not found:
        return False
    found[0].click()
    QApplication.processEvents()
    return True


def click_widget(widget):
    """真实鼠标点击（QTest 分派 press/release 到该控件）。"""
    from PyQt5.QtTest import QTest
    QTest.mouseClick(widget, Qt.LeftButton, pos=widget.rect().center())
    QApplication.processEvents()
    QApplication.processEvents()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6200)
    ap.add_argument("--out", default=os.path.join(HERE, "_kb_shot"))
    ap.add_argument("--size", default="1280x800")
    args = ap.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    os.environ["HSF_TOUCH"] = "1"        # 强制开（本机无触摸数字化器）

    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client import theme, pages_registry as reg
    theme.install(app)
    theme.apply_font(app)

    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    w, h = [int(x) for x in args.size.lower().split("x")]
    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.resize(w, h)
    win.show()
    app.setActiveWindow(win)
    app.processEvents()

    ok, err = bridge.connect_to(args.host, args.port, "root", "root")
    check("登录模拟器服务端", ok, err or "%s:%d" % (args.host, args.port))
    if not ok:
        return 1
    win.status_bar.set_connection("已连接 · root", ok=True)
    kb = win.soft_kbd
    check("软键盘已装配", kb is not None)
    if kb is None:
        return 1

    def wait(ms):
        """让 Qt 事件循环转 ms 毫秒（不阻塞定时器/信号）。"""
        loop = app.processEvents
        import time as _t
        end = _t.time() + ms / 1000.0
        while _t.time() < end:
            loop()
            _t.sleep(0.01)

    # ================================================================
    #  ① 历史数据页：真实点击关键字框 → 拼音键盘
    # ================================================================
    print("== ① 历史数据页 · 拼音键盘 ==")
    win.goto_page(reg.PAGE_DATA_HISTORY)
    wait(900)
    page = win.pages[reg.PAGE_DATA_HISTORY]
    entry = page._f_text
    click_widget(entry)
    wait(300)
    check("真点关键字框 → 面板可见", kb.isVisible())
    check("键盘类型 = 拼音", kb._kind == "pinyin", kb._kind)
    check("目标 = 关键字框", kb._live_target() is entry)
    entry.clear()
    for ch in "nihao":
        press_label(kb, ch)
    wait(120)
    check("组串 = nihao", kb._engine.composing_text() == "nihao",
          kb._engine.composing_text())
    cands = kb._engine.candidates()
    check("候选带首候选 = 你好（词组库命中）",
          cands[:1] == ["你好"], str(cands[:5]))
    composite(win, kb, os.path.join(args.out, "01_pinyin_composing.png"))
    press_label(kb, " ")
    wait(120)
    check("空格上屏到真实输入框", entry.text() == "你好", repr(entry.text()))
    composite(win, kb, os.path.join(args.out, "02_pinyin_committed.png"))

    # ================================================================
    #  ② 本地设置页：真实点击 QSpinBox → 纯数字盘
    # ================================================================
    print("== ② 本地设置页 · 数字键盘 ==")
    win.goto_page(reg.PAGE_SETTINGS_LOCAL)
    wait(600)
    lpage = win.pages[reg.PAGE_SETTINGS_LOCAL]
    spin = lpage.refresh_spin
    check("找到刷新周期 spinbox", isinstance(spin, QSpinBox))
    click_widget(spin)
    wait(300)
    check("真点数值框 → 面板可见", kb.isVisible())
    check("键盘类型 = 数字（DigitsOnly）", kb._kind == "numeric", kb._kind)
    check("数字盘排布 = digits", kb._numeric_layout == "digits",
          kb._numeric_layout)
    spin.setValue(200)
    spin.lineEdit().selectAll()
    for ch in "600":
        press_label(kb, ch)
    wait(120)
    check("数字盘落值 600（value 同步）", spin.value() == 600,
          "text=%r value=%d" % (spin.lineEdit().text(), spin.value()))
    composite(win, kb, os.path.join(args.out, "03_numeric_spin.png"))

    # ================================================================
    #  ③ 字母键盘（登录框同款 hints；主窗内无拉丁框，用离屏控件验证）
    # ================================================================
    print("== ③ 字母键盘 ==")
    from PyQt5.QtWidgets import QLineEdit
    from hsfsrv_client import softkeyboard as sk
    latin = QLineEdit(win)
    latin.setGeometry(10, 10, 260, 32)
    latin.setInputMethodHints(Qt.ImhLatinOnly)
    latin.show()
    click_widget(latin)
    wait(300)
    check("拉丁框 → 字母键盘", kb._kind == "alpha" and kb.isVisible(),
          kb._kind)
    for ch in "root":
        press_label(kb, ch)
    check("字母键盘直接出英文", latin.text() == "root", repr(latin.text()))
    composite(win, kb, os.path.join(args.out, "04_alpha_latin.png"))
    latin.hide()

    # ================================================================
    #  ④ 焦点驱动一致性：点别处收起 → 点回输入框再弹
    # ================================================================
    print("== ④ 焦点驱动 ==")
    win.goto_page(reg.PAGE_HOME)
    wait(500)
    check("切页后点非输入区域 → 收起（或已被切页收起）",
          not kb.isVisible() or kb._live_target() is None)
    win.goto_page(reg.PAGE_DATA_HISTORY)
    wait(400)
    page = win.pages[reg.PAGE_DATA_HISTORY]
    click_widget(page._f_text)
    wait(250)
    check("再点输入框 → 重新弹出", kb.isVisible())
    check("键盘类型回到拼音", kb._kind == "pinyin", kb._kind)
    check("组串已清（不残留上一框内容）",
          kb._engine.composing_text() == "")

    bridge.disconnect_from()
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
