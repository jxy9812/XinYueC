# -*- coding: utf-8 -*-
"""列显示开关的**真实主窗口**截图（走 __main__ 那条已验证可用的渲染路径）。

    python tools/hist_cols_shot.py

为什么不用"孤立 HistoryPage + page.render()"：离屏平台下那样渲出来
只有控件框、没有文字（字体/样式表在脱离主窗后不生效，实测两轮空白图）。
主窗口路径能正常渲染（同 `python -m hsfsrv_client --shot` 的效果）。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "verify", "histpage")
PORT = int(os.environ.get("HSF_SHOT_PORT", "6200"))

app = QApplication(sys.argv[:1])
from hsfsrv_client import theme

theme.install(app)
from hsfsrv_client.bridge import LinkBridge
from hsfsrv_client.main_window import MainWindow
from hsfsrv_client import pages_registry as reg

bridge = LinkBridge()
ok, err = bridge.connect_to("127.0.0.1", PORT, "admin", "admin")
print("connect", ok, err)

win = MainWindow(bridge)
win.resize(1400, 900)
win.show()

# 历史数据页索引（与 reg.PAGE_NAMES 对齐）
hist_index = None
for i, name in enumerate(reg.PAGE_NAMES):
    if name == "历史数据":
        hist_index = i
        break
print("历史页索引：", hist_index)


def page():
    return win.pages.get(hist_index)


def grab(name):
    pix = QPixmap(win.size())
    win.render(pix)
    path = os.path.join(OUT, name)
    ok_saved = pix.save(path)
    print("saved", path, ok_saved)


def step_enter():
    win.goto_page(hist_index)


def step_all():
    p = page()
    print("默认勾选:", len(p._f_col.checked_indexes()),
          "可见列:", len(p.visible_columns()), "行:", len(p._rows))
    grab("hist_cols_all.png")
    p._f_col.clear_all()
    for c in (0, 1, 3, 6):
        p._f_col._model.item(c).setCheckState(Qt.Checked)


def step_subset():
    p = page()
    print("勾中:", p._f_col.checked_indexes(),
          "可见列:", p.visible_columns())
    print("下拉:", p._f_col.lineEdit().text())
    print("隐藏列数:", sum(1 for c in range(p._table.table.columnCount())
                        if p._table.table.isColumnHidden(c)))
    grab("hist_cols_subset.png")
    app.quit()


QTimer.singleShot(500, step_enter)
QTimer.singleShot(4000, step_all)
QTimer.singleShot(6000, step_subset)
app.exec_()
