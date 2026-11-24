# -*- coding: utf-8 -*-
"""鼠标光标设置（2026-11-27 用户「板端的客户端在本地设置中添加鼠标的一些
常见设置比如鼠标大小，隐藏鼠标光标等等」+「还有鼠标样式」）。

[为什么自绘箭头] Qt 无法调整系统主题光标的尺寸（QCursor 没有 size API），
而板端是触摸屏 + 偶尔接鼠标：小箭头在强光/大屏下看不见。自绘一个 24/32/48px
的箭头位图，尺寸完全可控、立即生效，不必改 X11 的 Xcursor 主题（那要重载
会话）。其它形状（十字/手型）用 Qt 标准光标，尺寸跟随系统。

[样式/大小设在主窗，隐藏用全局 override] `host.setCursor()` 只作**缺省
值**：子控件自己设了光标的（QLineEdit 的 IBeam、导航/工具条的
PointingHand 等）优先，正好是"换样式但不破坏交互语义"想要的层级。
但**隐藏光标必须全局 override**——2026-11-27 板端实锅：只给主窗设
BlankCursor 时，触摸导航（条目显式 setCursor(PointingHand)）等区域仍会
把光标画出来。`QApplication.setOverrideCursor(Qt.BlankCursor)` 压过所有
控件的显式 cursor，是唯一彻底的做法；用 app 属性记状态、只压一层，
避免每拍 apply 把 override 栈越堆越高。

[隐藏光标] 只是不画光标，鼠标/触摸事件照常送达（可点击），触摸屏现场
用来避免光标挡住读数。项目内没有别处使用 overrideCursor（已全树确认）
⇒ restore 一次即回到原状，不会与忙碌光标打架。
"""

from PyQt5.QtCore import QPoint, Qt
from PyQt5.QtGui import QColor, QCursor, QPainter, QPen, QPixmap, QPolygon

#: 样式 id → Qt 标准光标（arrow 走自绘，见 build_arrow_cursor）。
_STD_CURSORS = {
    "cross": Qt.CrossCursor,
    "hand": Qt.OpenHandCursor,
    "pointing": Qt.PointingHandCursor,
    "sizeall": Qt.SizeAllCursor,
}


def build_arrow_cursor(size_px):
    """自绘箭头光标（白底黑边，任意尺寸）。size_px 夹到 [16, 96]。"""
    s = max(16, min(96, int(size_px)))
    pm = QPixmap(s, s)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    try:
        p.setRenderHint(QPainter.Antialiasing, True)
        u = s / 24.0                     # 以 24px 设计稿为单位缩放
        # 经典箭头轮廓（左上尖、右下尾），坐标按 24px 设计稿取值。
        raw = [
            (2.0, 1.0), (2.0, 18.5), (7.0, 13.8), (9.8, 21.5),
            (13.2, 20.0), (10.5, 12.6), (17.0, 12.2),
        ]
        poly = QPolygon([QPoint(int(x * u), int(y * u)) for x, y in raw])
        p.setBrush(QColor(255, 255, 255))
        p.setPen(QPen(QColor(20, 20, 20), max(1.0, 1.1 * u)))
        p.setPen(QPen(QColor(20, 20, 20), max(1.0, 1.1 * u)))
        p.drawPolygon(poly)
    finally:
        p.end()
    # 热点 = 箭头尖（设计稿 2,1）
    return QCursor(pm, int(2 * (s / 24.0)), int(1 * (s / 24.0)))


def cursor_for(style, size_px):
    """(样式, 尺寸) → QCursor；None = 恢复系统缺省（不设光标）。"""
    style = str(style or "arrow")
    if style == "arrow":
        try:
            size_px = int(size_px)
        except (TypeError, ValueError):
            size_px = 0
        if size_px > 0:
            return build_arrow_cursor(size_px)
        return None                      # 系统默认箭头（跟随主题尺寸）
    std = _STD_CURSORS.get(style)
    return QCursor(std) if std is not None else None


def apply_mouse_settings(host, settings):
    """把本机鼠标设置应用到主窗（幂等；设置页改动后立即调用即生效）。

    :param host: 主窗（QWidget）。子控件自有光标优先，不受影响。
    :param settings: settings 模块（get_bool/get_int/get_str）。
    :return: 应用结果描述（"hidden" / "cursor:<style>:<size>" / "default"），
        便于回归断言。
    """
    if host is None or settings is None:
        return "default"
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    hidden = bool(settings.get_bool(settings.K_MOUSE_CURSOR_HIDDEN, False))
    overridden = bool(getattr(app, "_hsf_blank_override", False)) \
        if app is not None else False
    if hidden:
        # 主窗设 Blank（无 app 时的兜底）+ 全局 override（压过子控件显式
        # cursor：导航 PointingHand、相机卡 OpenHand 等）。
        host.setCursor(Qt.BlankCursor)
        if app is not None and not overridden:
            app.setOverrideCursor(Qt.BlankCursor)
            app._hsf_blank_override = True       # noqa: SLF001
        return "hidden"
    # 取消隐藏：先撤掉我们压的那一层（只撤自己压的，不动别人的栈）。
    if app is not None and overridden:
        app.restoreOverrideCursor()
        app._hsf_blank_override = False          # noqa: SLF001
    style = settings.get_str(settings.K_MOUSE_CURSOR_STYLE,
                             settings.MOUSE_CURSOR_STYLE_DEF)
    size = settings.get_int(settings.K_MOUSE_CURSOR_SIZE, 0)
    cur = cursor_for(style, size)
    if cur is None:
        host.unsetCursor()
        return "default"
    host.setCursor(cur)
    return "cursor:%s:%s" % (style, size)


def current_settings_doc(settings):
    """当前三项设置（界面回显/回归断言共用）。"""
    return {
        "style": settings.get_str(settings.K_MOUSE_CURSOR_STYLE,
                                  settings.MOUSE_CURSOR_STYLE_DEF),
        "size": settings.get_int(settings.K_MOUSE_CURSOR_SIZE, 0),
        "hidden": bool(settings.get_bool(settings.K_MOUSE_CURSOR_HIDDEN, False)),
    }
