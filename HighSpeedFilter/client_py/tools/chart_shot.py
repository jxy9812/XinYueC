# -*- coding: utf-8 -*-
"""折线图视觉验收截图：多序列 + 缩放 + 点信息气泡。

    python tools/chart_shot.py

用真主窗口渲染（孤立 widget 在离屏下不出文字，之前踩过），
输出到 HighSpeedFilter/verify/histpage/。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtCore import QPoint, Qt, QTimer
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "verify", "histpage")

app = QApplication(sys.argv[:1])
from hsfsrv_client import theme

theme.install(app)
from hsfsrv_client.pages_other import LineChartDialog

BASE = 1793927000000
STEP = 5000
N = 120

freq = [(BASE + i * STEP, 25.0 + 6.0 * ((i % 40) / 40.0)) for i in range(N)]
rpm = [(BASE + i * STEP, 720.0 + 180.0 * ((i % 30) / 30.0)) for i in range(N)]
turb = [(BASE + i * STEP, 12.5 + 12.0 * ((i % 55) / 55.0)) for i in range(N)]

dlg = LineChartDialog([("输出频率(Hz)", freq),
                       ("搅拌转速(rpm)", rpm),
                       ("浊度1(NTU)", turb)])
dlg.resize(980, 560)
dlg.show()


def grab(name):
    pix = QPixmap(dlg.size())
    dlg.render(pix)
    path = os.path.join(OUT, name)
    ok = pix.save(path)
    print("saved", path, ok)


def step_full():
    grab("chart_full.png")
    # 放大到中段
    dlg.zoom_at(0.5, 3.0)


def step_zoomed():
    grab("chart_zoomed.png")


def step_picked():
    """在放大后的视口里选中一个点，截气泡。"""
    l, t, r, b = dlg.plot_rect()
    t0, t1 = dlg.view_times()
    vis = [(st, v) for st, v in freq if t0 <= st <= t1]
    if vis:
        dlg._picked = (0, freq.index(vis[len(vis) // 2]),
                       vis[len(vis) // 2])
    dlg.update()
    grab("chart_tooltip.png")
    app.quit()


QTimer.singleShot(600, step_full)
QTimer.singleShot(1400, step_zoomed)
QTimer.singleShot(2400, step_picked)
app.exec_()
