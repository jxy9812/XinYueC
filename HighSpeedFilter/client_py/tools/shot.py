# -*- coding: utf-8 -*-
"""板端截图工具：用 Qt 的 grabWindow 抓整屏（**不用 xwd**）。

    python tools/shot.py                 # 抓当前整屏 -> /tmp/board.png
    python tools/shot.py out.png         # 指定输出路径

[为什么不用 xwd] 本板 X root 是 depth=24、TrueColor，但 `xwd -root` 导出的
像素是 24bpp packed（bytes_per_line=5120，每像素仍占 4 字节）。自己写
Python 转 PNG 时极易把 stride 算错，导致相邻像素错位、红/绿交错成"条纹"——
**看着像客户端渲染坏了，其实是转换脚本的锅**（本项目实测踩过：为此白查了两轮）。

Qt 的 `QScreen.grabWindow(0)` 走 XGetImage 并**由 Qt 自己做像素格式转换**，
出来就是正确的 32 位图像，存 PNG 也由 Qt 处理。一行搞定，零转换风险。

前置：需要在 X 里跑（DISPLAY=:0），且 PyQt5 可用。
"""

import os
import sys


def main(argv):
    out = argv[1] if len(argv) > 1 else "/tmp/board.png"
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    screen = app.primaryScreen()
    if screen is None:
        print("没有可用屏幕（DISPLAY 未设置？）", file=sys.stderr)
        return 1
    pix = screen.grabWindow(0)
    if pix.isNull():
        print("抓屏失败（窗口不可见或权限不足）", file=sys.stderr)
        return 1
    path = os.path.abspath(out)
    if not pix.save(path):
        print("保存失败：%s" % path, file=sys.stderr)
        return 1
    print("截图：%s (%dx%d depth=%d)"
          % (path, pix.width(), pix.height(), pix.depth()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
