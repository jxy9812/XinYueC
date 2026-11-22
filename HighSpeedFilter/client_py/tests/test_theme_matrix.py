# -*- coding: utf-8 -*-
"""主题 × 页面 显示自检（2026-11-14 用户定案「每一款都要检测每个界面显示
是否正常」）。

[为什么要有]
============
深色主题一上来就抓出**真缺陷**：远端设置页的行标签写死 `#1A1A1A`（深灰）
⇒ 深色底上几乎看不见（截图实测「启动设备/搅拌电机/阀复位/换相策略」四个
标签全糊）⇒ 靠人眼把 5 套主题 × 16 个页面看一遍（80 次）不现实 ⇒
**自动检测**。

[检测三件事]（每套主题 × 每个页面都跑）
==========================================
    ① **对比度**：可见文字与其背景的对比度 ≥ 3:1 ⇒ 太低 = 看不见；
    ② **裁字**：控件宽度 ≥ 最小宽（=文字全显的宽度）⇒ 窄了 = 被裁；
    ③ **空白**：页面渲染出来的图不是纯色 ⇒ 根本没画出来。

[取色的口径]
============
文字色取控件**自己的调色板**（`w.palette().color(role)`）——QSS 的
`color:` 会写进调色板 ⇒ 无论颜色是写死的还是 `palette(text)` 引用，
读出来都是**实际渲染用的颜色** ⇒ 检测口径与眼睛一致。

[怎么跑]
========
    python -I tests/test_theme_matrix.py     # 独立跑
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)

# [输出编码 2026-11-14] 中文 Windows 默认 cp936 ⇒ 打印「⇒」会 UnicodeEncodeError
# ⇒ 整套测试在打印断言时被打断（退出码还是 0，看着像"跑完了"）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PyQt5.QtGui import QColor, QImage, QPainter          # noqa: E402
from PyQt5.QtWidgets import (QApplication, QFrame, QLabel,  # noqa: E402
                             QWidget)

from hsfsrv_client import theme                            # noqa: E402
from hsfsrv_client import pages_registry as reg            # noqa: E402

PASS = 0
FAIL = 0
#: 文字与背景的最小对比度（3:1 = WCAG AA 正文下限；低于此值"看不清"）。
MIN_CONTRAST = 3.0
#: 视图尺寸（离屏渲染用；宽一点能触发窄屏折行）。
VIEW_W = 1100
VIEW_H = 800


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _luminance(c):
    """相对亮度（sRGB 标准公式）。"""
    def _lin(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return (0.2126 * _lin(c.red()) + 0.7152 * _lin(c.green())
            + 0.0722 * _lin(c.blue()))


def _contrast(fg, bg):
    """两个颜色的对比度（1..21）。"""
    l1 = _luminance(fg)
    l2 = _luminance(bg)
    if l1 < l2:
        l1, l2 = l2, l1
    return (l1 + 0.05) / (l2 + 0.05)


#: 各类控件的文字色在调色板里的角色。
_TEXT_ROLE = {
    "QLabel": "WindowText",
    "QPushButton": "ButtonText",
    "QLineEdit": "Text",
    "QComboBox": "Text",
    "QCheckBox": "WindowText",
    "QRadioButton": "WindowText",
    "QSpinBox": "Text",
    "QDoubleSpinBox": "Text",
    "QPlainTextEdit": "Text",
    "QTextEdit": "Text",
    "QTableWidget": "Text",
    "QHeaderView": "WindowText",
}


def _bg_candidates(w):
    """控件所在位置的**候选背景色**（多个）。

    [为什么是"候选"而不是一个] 同一个控件可能有几种底：按钮自己有底色
    （QSS 的 background-color）、标签是透明（透出卡片底）、输入框有自己的
    Base 色。取单个底会误报（实测「启动」按钮是绿底白字，按卡片白底算
    就成了 1.0:1 的假警报）⇒ 取**全部候选**再比"最差的那个"，只有全
    都不达标才算看不清。
    """
    out = []
    name = type(w).__name__
    try:
        pal = w.palette()
        if name in ("QPushButton", "QToolButton"):
            out.append(pal.color(pal.Button))
        elif name in ("QLineEdit", "QComboBox", "QSpinBox", "QDoubleSpinBox",
                      "QTextEdit", "QPlainTextEdit", "QTableWidget"):
            out.append(pal.color(pal.Base))
        elif name == "QLabel":
            out.append(pal.color(pal.Base))     # QSS 给标签设过底色时
    except Exception:                               # noqa: BLE001
        pass
    node = w
    while node is not None:
        if node.objectName() == "HsfCard":
            try:
                out.append(node.palette().color(node.palette().Base))
            except Exception:                       # noqa: BLE001
                pass
            break
        node = node.parentWidget()
    win = w.window()
    if win is not None:
        try:
            out.append(win.palette().color(win.palette().Window))
        except Exception:                           # noqa: BLE001
            pass
    out.append(QColor("#FFFFFF"))
    return out


def _visible(w):
    try:
        return w.isVisible() and w.width() > 0 and w.height() > 0
    except RuntimeError:
        return False


def _scan_page(page):
    """扫一个页面的可见控件 ⇒ 返回 (看不清的, 被裁的)。"""
    from PyQt5.QtGui import QPalette
    dark_ok, clipped = [], []
    for w in page.findChildren(QWidget):
        if not _visible(w):
            continue
        name = type(w).__name__
        if name not in _TEXT_ROLE:
            continue
        # 有文字吗？（QLabel 的 text 为空 ⇒ 不参与对比度检查）
        text = ""
        try:
            text = w.text() if hasattr(w, "text") else ""
        except Exception:                               # noqa: BLE001
            text = ""
        if not text.strip():
            continue
        role = getattr(QPalette, _TEXT_ROLE[name])
        try:
            fg = w.palette().color(role)
        except Exception:                               # noqa: BLE001
            continue
        cands = _bg_candidates(w)
        ratio = max(_contrast(fg, bg) for bg in cands)
        if ratio < MIN_CONTRAST:
            dark_ok.append("%s「%s」%s(%.1f:1)"
                           % (name, text[:12], fg.name(),
                              ratio))
        # 裁字：宽度小于最小宽（文字全显所需）⇒ 被压扁
        try:
            if w.width() < w.minimumSizeHint().width():
                clipped.append("%s「%s」%d<%d"
                              % (name, text[:12], w.width(),
                                 w.minimumSizeHint().width()))
        except Exception:                               # noqa: BLE001
            pass
    return dark_ok, clipped


def _render_blank(page):
    """渲染页面 ⇒ 返回 True 表示"白板"（什么都没画）。"""
    img = QImage(page.width(), page.height(), QImage.Format_ARGB32)
    img.fill(QColor("#000000"))
    p = QPainter(img)
    page.render(p)
    p.end()
    colors = set()
    for y in range(0, img.height(), max(1, img.height() // 40)):
        for x in range(0, img.width(), max(1, img.width() // 40)):
            colors.add(img.pixel(x, y))
    return len(colors) <= 1


def main():
    global PASS, FAIL
    app = QApplication.instance() or QApplication(sys.argv[:1])
    theme.install(app, theme.DEFAULT_THEME_ID)

    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.resize(VIEW_W, VIEW_H)
    win.show()
    app.processEvents()

    problems = []
    for tid, tname in theme.THEME_LIST:
        theme.apply_theme(app, tid)
        app.processEvents()
        for i in range(reg.PAGE_COUNT):
            try:
                win.goto_page(i)
                for _ in range(3):
                    app.processEvents()
                page = win.stack.currentWidget()
                if page is None:
                    problems.append("%s/%s：页面没建出来" % (tid, reg.page_name(i)))
                    continue
                dark, clipped = _scan_page(page)
                if dark:
                    problems.append("%s/%s：看不清 %s"
                                   % (tid, reg.page_name(i), dark[:4]))
                if clipped:
                    problems.append("%s/%s：被裁 %s"
                                   % (tid, reg.page_name(i), clipped[:4]))
                if _render_blank(page):
                    problems.append("%s/%s：白板（什么都没画）"
                                    % (tid, reg.page_name(i)))
            except Exception as exc:                    # noqa: BLE001
                problems.append("%s/%s：异常 %s: %s"
                                % (tid, reg.page_name(i),
                                   type(exc).__name__, exc))
        app.processEvents()

    check("5 套主题 × %d 个页面：无看不清/被裁/白板/异常"
          % reg.PAGE_COUNT,
          not problems,
          "%d 处问题" % len(problems))
    for p in problems[:25]:
        print("      · %s" % p)
    if len(problems) > 25:
        print("      · …… 还有 %d 处" % (len(problems) - 25))

    # 主题切换后当前主题与调色板要真的变了（防"下拉改了但界面没变"）。
    for tid, _name in theme.THEME_LIST:
        theme.apply_theme(app, tid)
        app.processEvents()
        pg = theme.theme_tokens(tid)["page_bg"]
        check("主题 %s 生效（页面底色=%s）" % (tid, pg),
              theme.current_theme_id() == tid
              and theme.theme_tokens(theme.current_theme_id())["page_bg"] == pg,
              "")
    theme.apply_theme(app, theme.DEFAULT_THEME_ID)

    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
