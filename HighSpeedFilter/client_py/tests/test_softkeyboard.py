# -*- coding: utf-8 -*-
"""软键盘（触摸屏输入）实际测试。

    python tests/test_softkeyboard.py

测什么（用户要求：触摸屏电脑无键盘部署，输入控件必须可软键盘输入）：
  A. 自动探测开关（HSF_TOUCH 强制开/关、Windows 触摸数字化器探测）
  B. 焦点联动：QLineEdit 获得焦点 → 面板弹出；焦点离开 → 收起；
     TimePickerEdit（带排除标记）获得焦点 → 不弹
  C. 按键输入：字母/数字插入、退格、翻页（大小写/符号）、空格
  D. 真实点击路径：聚焦输入框后逐键点「软件按键」，文本与界面一致
"""

import os
import sys

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["HSF_TOUCH"] = "1"                 # 强制开（桌面机探测为关）
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtCore import Qt                   # noqa: E402
from PyQt5.QtWidgets import QApplication      # noqa: E402

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
    app = QApplication.instance() or QApplication(sys.argv[:1])

    from hsfsrv_client import softkeyboard as sk
    from hsfsrv_client.pages_other import TimePickerEdit
    from PyQt5.QtWidgets import QLineEdit, QWidget

    # ---- A 自动探测 ----
    print("== A. 自动探测 ==")
    check("A1 HSF_TOUCH=1 → 强制开", sk.touch_default_enabled() is True)

    # ---- 准备 ----
    host = QWidget()
    edit = QLineEdit(host)
    picker = TimePickerEdit("日期")
    picker.setProperty(sk.NO_SOFT_KBD, True)
    plain = QLineEdit(host)
    plain.setProperty(sk.NO_SOFT_KBD, False)

    kb = sk.SoftKeyboard()
    kb.attach(app, host)
    host.resize(1280, 860)
    host.show()
    app.processEvents()

    def focus(widget):
        # [activateWindow 必须] 真实触摸点击会把窗口激活、焦点落到被点的
        # 控件；离屏测试里 setFocus 不切激活窗口，focusWidget() 停留在
        # 软键盘自己的按键上——收起/切换逻辑永远不触发（与真机不一致）。
        widget.setFocus(Qt.OtherFocusReason)
        widget.window().activateWindow()
        app.processEvents()
        app.processEvents()

    # ---- B 焦点联动 ----
    print("== B. 焦点联动 ==")
    focus(edit)
    check("B1 输入框聚焦 → 面板弹出", kb.isVisible())
    check("B2 目标 = 该输入框", kb._live_target() is edit)

    focus(picker)
    app.processEvents()
    check("B3 日期选择器聚焦 → 不弹软键盘（排除标记）", not kb.isVisible())

    focus(plain)
    check("B4 普通输入框聚焦 → 再弹", kb.isVisible())

    # 焦点离开输入类：用可聚焦的按钮（裸 QWidget 默认 NoFocus 聚焦不上）
    from PyQt5.QtWidgets import QPushButton as _Btn
    focus_btn = _Btn(host)
    focus_btn.show()
    focus(focus_btn)
    app.processEvents()
    check("B5 焦点离开 → 收起", not kb.isVisible())
    focus(edit)
    app.processEvents()

    # ---- C 键位表与布局（对齐 C 端 s_kbMap* 表）----
    print("== C. 键位表结构 ==")
    from hsfsrv_client.softkeyboard import KB_MAPS
    check("C0 四种模式齐备", set(KB_MAPS) ==
          {"TextLower", "TextUpper", "Special", "Number"})
    tl = KB_MAPS["TextLower"]
    check("C0a TextLower 4 行", len(tl) == 4)
    check("C0b 首行 12 键（1# + q..p + 退格）", len(tl[0]) == 12)
    check("C0c 首行首键 1#（宽 5）", tl[0][0][0] == "1#" and tl[0][0][2] == 5)
    check("C0d 退格宽 7", tl[0][-1][2] == 7)
    check("C0e Number 4 行每行 4~5 键", len(KB_MAPS["Number"]) == 4
          and len(KB_MAPS["Number"][0]) == 4)
    # 行内单位权重已交给布局（stretch），检查按钮拉伸与单位一致：
    kb._mode = "TextLower"
    kb._render_page()
    first_row = kb._rows_box.itemAt(0).layout()
    check("C0f 首行拉伸 = C 宽度单位",
          [first_row.stretch(i) for i in range(first_row.count())]
          == [u for (_l, _a, u) in tl[0]])

    # ---- C2 引擎级插入（与布局无关的底层通道）----
    kb._act("t", "root/2026")
    check("C2 引擎插入", edit.text() == "root/2026")
    kb._backspace()
    check("C2b 退格", edit.text() == "root/202")

    # ---- C3 模式切换 ----
    kb._act("num")                       # TextLower → Number
    check("C3a 1# 切到 Number", kb._mode == "Number")
    kb._act("num")
    check("C3b 再切回 TextLower", kb._mode == "TextLower")
    kb._act("up")
    check("C3c ABC 切大写布局", kb._mode == "TextUpper")
    kb._mode = "TextLower"
    kb._render_page()

    # ---- D 真实按键点击（点击按钮对象本身，等价于手指戳）----
    print("== D. 真实按键点击 ==")
    edit.clear()
    kb._mode = "TextLower"
    kb._render_page()
    focus(edit)

    def press(label):
        found = None
        for i in range(kb._rows_box.count()):
            item = kb._rows_box.itemAt(i)
            lay = item.layout()
            if lay is None:
                continue
            for j in range(lay.count()):
                w = lay.itemAt(j).widget()
                if isinstance(w, sk.QPushButton) and w.text() == label:
                    found = w
                    break
            if found:
                break
        check("D 按键 %r 存在" % label, found is not None)
        if found is not None:
            found.click()

    for ch in ("r", "o", "t"):
        press(ch)
    press("1#")                          # 切数字盘
    press("1")
    check("D 逐键点出 root1", edit.text() == "root1",
          "实际=%r" % edit.text())
    press("收起")
    check("D 收起键隐藏面板", not kb.isVisible())

    # ---- E 排除标记 ----
    print("== E. 排除标记 ==")
    picker2 = TimePickerEdit()
    check("E1 TimePickerEdit 自动带排除标记",
          picker2.property(sk.NO_SOFT_KBD) is True)

    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
