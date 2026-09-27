# -*- coding: utf-8 -*-
"""扫描 Qt 6.8.3 widgets 头文件的 Q_SIGNALS，与 Src/XGui/Widget/*.h 的 *_signal 宏对照，输出真实缺口清单。"""
import os, re, glob

QT = "/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets"
XGUI = "/home/xinyue/Code/XinYueC/Src/XGui/Widget"

# Qt 类 -> XGui 类 映射（默认去掉 Q 加 X，特例见下）
REN = {
    "QTextEdit": "XTextEdit", "QTextBrowser": "XTextBrowser",
    "QPlainTextEdit": "XPlainTextEdit",
    "QAbstractSpinBox": "XAbstractSpinBox", "QSpinBox": "XSpinBox",
    "QAbstractSlider": "XAbstractSlider", "QSlider": "XSlider",
    "QScrollBar": "XScrollBar", "QDial": "XDial",
    "QFrame": "XFrame", "QLabel": "XLabel",
    "QLCDNumber": "XLcdNumber", 
    "QDoubleSpinBox": "XDoubleSpinBox",
}


def qt_classes(path):
    """返回 [(cls, base, signals)]，仅含声明了 Q_SIGNALS 的类。"""
    text = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for m in re.finditer(r"class\s+(?:Q_WIDGETS_EXPORT\s+)?(Q\w+)\s*:\s*public\s+([\w:]+)", text):
        cls, base = m.group(1), m.group(2).split("::")[-1]
        # 该类块：从 class 声明到下一个 class 声明
        nxt = re.search(r"\nclass\s", text[m.end():])
        block = text[m.end():m.end() + (nxt.start() if nxt else 6000)]
        sigs = []
        for sm in re.finditer(r"\b(?:Q_SIGNALS|signals)\s*:", block):
            seg = block[sm.end():]
            stop = re.search(r"\b(?:public|private|protected)\s*(?:Q_SLOTS\s*:|slots\s*:)?|^\s*\};", seg, re.M)
            seg = seg[:stop.start()] if stop else seg[:5000]
            for d in re.finditer(r"\bvoid\s+(\w+)\s*\(", seg):
                sigs.append(d.group(1))
        if sigs:
            out.append((cls, base, sorted(set(sigs))))
    return out


def xgui_signals(xcls):
    path = os.path.join(XGUI, xcls + ".h")
    if not os.path.exists(path):
        return None
    text = open(path, encoding="utf-8", errors="replace").read()
    names = set()
    for m in re.finditer(r"#define\s+(\w+?)_signal\b", text):
        names.add(m.group(1).split("_", 1)[-1])
    # 真实声明形式：void* XFoo_bar_signal(...)
    for m in re.finditer(r"void\s*\*?\s*(\w+?)_(\w+)_signal\s*\(", text):
        names.add(m.group(2))
    return names


# 明确不在本轮 50 控件对齐范围的 Qt 类（无对应 XGui 类，不视为缺口）
SKIP = {
    "QColorDialog", "QFileDialog", "QFontDialog", "QInputDialog", "QProgressDialog",
    "QWizardPage", "QGraphicsEffect", "QGraphicsColorizeEffect", "QGraphicsBlurEffect",
    "QGraphicsDropShadowEffect", "QGraphicsOpacityEffect", "QGraphicsObject",
    "QGraphicsTextItem", "QGraphicsScale", "QGraphicsRotation", "QGraphicsWidget",
    "QAbstractItemDelegate", "QColumnView", "QDataWidgetMapper", "QHeaderView",
    "QListView", "QListWidget", "QTreeView", "QTreeWidget", "QApplication",
    "QRhiWidget", "QStackedLayout", "QCompleter", "QScroller", "QSystemTrayIcon",
    "QTimeEdit", "QDateEdit", "QDoubleSpinBox", "QMdiSubWindow",
}


def main():
    rows, total = [], 0
    seen = set()
    for h in sorted(glob.glob(os.path.join(QT, "**", "*.h"), recursive=True)):
        if "_p.h" in h or "/doc/" in h:
            continue
        for cls, base, sigs in qt_classes(h):
            if cls in SKIP:
                continue
            xcls = REN.get(cls, "X" + cls[1:])
            if (cls, xcls) in seen:
                continue
            seen.add((cls, xcls))
            have = xgui_signals(xcls)
            if have is None:
                rows.append((cls, xcls, sigs, base, "无 XGUI 头文件"))
                total += len(sigs)
                continue
            miss = [s for s in sigs if s not in have]
            if miss:
                rows.append((cls, xcls, miss, base, "缺 %d/%d" % (len(miss), len(sigs))))
                total += len(miss)
    for cls, xcls, miss, base, note in rows:
        print("%-18s %-18s %-14s %s" % (cls, xcls, note, ", ".join(miss)))
    print("\n== 缺口信号总计: %d ==" % total)


if __name__ == "__main__":
    main()
