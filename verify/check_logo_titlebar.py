# -*- coding: utf-8 -*-
"""离屏截取整个主窗，验证顶部标题栏 logo 渲染。"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLIENT_PY = os.path.join(ROOT, "HighSpeedFilter", "client_py")
sys.path.insert(0, CLIENT_PY)

os.environ["QT_QPA_PLATFORM"] = "offscreen"

from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QPixmap
from hsfsrv_client import theme
from hsfsrv_client.widgets import TitleBar

app = QApplication(sys.argv[:1])
theme.install(app)
theme.apply_font(app)

bar = TitleBar()
bar.set_title(theme.APP_TITLE)
bar.resize(1280, theme.TITLE_BAR_H)
bar.show()
app.processEvents()

assert hasattr(bar, "logo_label"), "标题栏缺 logo_label"
pm = bar.logo_label.pixmap()
assert pm is not None and not pm.isNull(), "logo 未加载（pixmap 空）"
print("logo pixmap: %dx%d" % (pm.width(), pm.height()))

out = os.path.join(ROOT, "verify", "titlebar_logo_check.png")
pm_full = QPixmap(bar.size())
bar.render(pm_full)
pm_full.save(out)
print("saved:", out, pm_full.width(), "x", pm_full.height())
