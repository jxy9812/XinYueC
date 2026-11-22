# -*- coding: utf-8 -*-
"""通用 UI 组件：卡片 / 标题栏 / 状态栏 / 数值块 / 导航面板 / 分割主窗。

[C 端对应] 逐一对齐：
    HsfTitleBar   → TitleBar   （深蓝底 40px，☰ + 居中标题 + 模式/健康/时钟/温湿度）
    HsfStatusBar  → StatusBar  （黑底 28px，左状态右连接信息）
    HsfNavPanel   → NavPanel   （左侧停靠树，可拖宽）
    HsfDataBar    → 本文件的 StatBlock（数值块）
"""

from PyQt5.QtCore import (QDateTime, QPoint, QRectF, QSize, Qt, QTimer,
                          pyqtSignal)
from PyQt5.QtGui import QColor, QFont, QFontMetrics, QPainter, QPen
from PyQt5.QtWidgets import (QFrame, QHBoxLayout, QLabel, QMenu, QPushButton,
                             QScrollArea, QSizePolicy, QSplitter, QVBoxLayout,
                             QWidget)

from . import theme


def _escape(text):
    """HTML 转义（富文本里 & < > 会破坏排版）。"""
    return (str(text).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


class RichLabel(QLabel):
    """富文本标签，但 `text()` 返回**纯文本**。

    [为什么要这么绕] StatBlock 把「数值 + 单位」合成**一段富文本**（同一行、
    同一基线、同一字号）⇒ QLabel.text() 会返回 HTML ⇒ 断言（"读数 == 0.350"）
    与「取值/复制」全都变了 ⇒ 这里覆写 text() 返回纯文本那份 ⇒ **既有调用点
    零改动**。
    """

    def __init__(self, parent=None):
        QLabel.__init__(self, parent)
        self.setTextFormat(Qt.RichText)
        self._plain = ""

    def set_plain(self, html, plain):
        """设富文本内容，并记住纯文本那份。"""
        self._plain = "" if plain is None else str(plain)
        QLabel.setText(self, html)

    def text(self):
        """**纯文本**内容（不是 HTML）。"""
        return self._plain

    def html(self):
        """内部那段**富文本**（回归锁用来核字号/同排版）。"""
        return QLabel.text(self)


# ============================================================================
#                              卡片容器
# ============================================================================

#: 卡片标题样式（`Card` 用它；页面要把标题放进自己的网格时也用它 ——
#: 例如自动模式卡让**表盘跨占标题行**，标题不能再由 Card 在外层布局占一行）。
CARD_TITLE_QSS = ("QLabel { color: palette(link); font-weight: bold; font-size: 19px;"
                 " background: transparent; }")


class Card(QFrame):
    """白底软描边卡片（C 端 HsfCard 同款）。

    用法：`card = Card("变频器状态")`，内容塞进 `card.body` 布局。
    """

    def __init__(self, title="", parent=None):
        QFrame.__init__(self, parent)
        self.setObjectName("HsfCard")
        self.setStyleSheet(theme.card_qss())
        self._lay = QVBoxLayout(self)
        # [密度 2026-11-12 收紧] 1366x768@1.5x 下首页 5 块内容放不下
        # （实测内容 823px vs 视口 480px），卡内边距/行距是每卡都交的
        # "税"，收 8px×10 卡就是一行的高度。宽度方向留 12（触屏不挤）。
        self._lay.setContentsMargins(12, 5, 12, 7)
        self._lay.setSpacing(1)
        self.title_label = None
        if title:
            self.title_label = QLabel(title)
            # [19px 2026-11-12] 21→19：1366x768@1.5x 一屏放下首页的密度
            # 收口之一（卡标题与卡内正文层级仍可辨）。
            self.title_label.setStyleSheet(CARD_TITLE_QSS)
            self._lay.addWidget(self.title_label)
        #: 内容区（调用方自行往里加控件）。
        self.body = QVBoxLayout()
        self.body.setSpacing(6)
        self._lay.addLayout(self.body)

    def add(self, widget):
        self.body.addWidget(widget)
        return widget

    def add_bottom_stretch(self):
        """把卡内多余高度沉到底部（短卡与高卡同排时标题/内容顶对齐）。

        [2026-11-22 实锅] 首页同一排的卡被布局拉成等高；矮卡（流量计/
        电动球阀）的 QVBoxLayout 会把多出来的高度**均摊到标题与正文之间**
        ⇒ 标题比同排高卡低一截、正文上方多出一整行空白（用户截图
        「文字位置不对，上面空了一行」）。在卡的根布局末尾加 stretch，
        多余高度全部沉底 ⇒ 标题/正文一律贴顶。
        """
        self._lay.addStretch(1)


class StatBlock(QWidget):
    """数值块：上方灰色标题 + 下方大字号读数 + 可选单位。

    C 端 HsfDataBar 的口径：监视页成排的「标签 + 值」格子。

    [2026-11-14 用户定案「数字有点小，而且跟单位不在同一行同一大小」]
    数值与单位**合成一个富文本标签**（不再是一个标签 + 一个标签）：
        · 同一行、同一基线由**排版本身**保证（两个独立 QLabel 即使都
          AlignBottom，字号不同 ⇒ 文字基线仍错位：截图里 25.00 与 Hz 一个
          下沉一个上浮）；
        · 单位字号**与数值同大**（原来单位写死 22px ⇒ 比多数数值
          16~18px 还大 ⇒ 数值显得更小）。
    """

    def __init__(self, caption, unit="", value="--", color=None,
                 value_size=20, parent=None):
        QWidget.__init__(self, parent)
        lay = QVBoxLayout(self)
        # [竖向边距 2→0 2026-11-12] 首页一屏塞下全部卡片的密度收口之一
        # （StatBlock 之间的呼吸由卡片 body spacing 提供，不再逐块加边距）。
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(2)
        self._value_size = value_size  # [2026-11-13 记档] 变色重写时保留原字号。
        self._unit_text = unit
        self._color = color
        self._plain = str(value)     # 纯文本副本（富文本里读不出原值）
        self._caption = QLabel(caption)
        self._caption.setStyleSheet(theme.caption_label_qss())
        lay.addWidget(self._caption)
        row = QHBoxLayout()
        row.setSpacing(3)
        self._value = RichLabel()
        # [富文本] 数值 + 单位合成一个标签 ⇒ 同行同基线（见类注释）。
        # RichLabel.text() 仍返回**纯文本** ⇒ 既有断言/取值零改动。
        self._value.setAlignment(Qt.AlignLeft | Qt.AlignBottom)
        # [QSS 也留一份颜色/字号] 富文本负责渲染；QSS 保留一份 ⇒ 外部
        # （回归锁读 styleSheet() 看颜色）仍拿得到"当前是红还是黑"。
        self._value.setStyleSheet(theme.value_label_qss(color, value_size))
        self._value.set_plain(self._rich(value, color), value)
        row.addWidget(self._value)
        row.addStretch(1)
        lay.addLayout(row)

    # ---- 数值 + 单位的富文本 ----
    def _rich(self, value, color=None):
        """数值 + 单位拼成一段富文本（单位与数值**同字号**）。

        同时把**纯文本**记到 `_plain`（富文本里读不出原值 ⇒ 回归锁/诊断
        要用纯文本那份）。
        """
        self._plain = str(value)
        val = _escape(self._plain)
        unit = _escape(str(self._unit_text or ""))
        size = int(self._value_size)
        if not unit:
            return val
        # [单位同字号 2026-11-14] 用户口径「跟单位…同一大小」⇒ 单位与数值
        # 同字号、同基线；单位用次要色（不抢数值的视觉重心）。
        # [font-weight:normal 显式钉] 数值标签的 QSS 写着 font-weight:bold
        # ⇒ 富文本的**默认字体**是粗体 ⇒ 不显式钉住的话单位会继承粗体
        # （数值更粗 ⇒ 单位看着更小）⇒ 这里显式写 normal。
        return ("<span style=\"font-size:%dpx;font-weight:bold;color:%s;\">%s</span>"
                "<span style=\"font-size:%dpx;font-weight:normal;color:%s;\">&nbsp;%s</span>"
                % (size, color or theme.text_color(), val,
                   size, theme.text_dim_color(), unit))

    def set_value(self, text, color=None):
        self._color = color
        if color is not None:
            self._value.setStyleSheet(
                theme.value_label_qss(color, self._value_size))
        self._value.set_plain(self._rich(text, color), text)

    def set_value_colored(self, text, color, size):
        """带颜色地写值，**保留传入的字号**。

        [为什么要单独一个口] set_value(text, color) 不带字号 ⇒ 数值要随状态
        变色（如浊度超阈值标红）时若走 set_value，字号会停在构造时的
        value_size、与同卡其它读数不一致。这里按传入 size 重写。
        """
        self._value_size = int(size)
        self._color = color
        self._value.setStyleSheet(theme.value_label_qss(color, size))
        self._value.set_plain(self._rich(text, color), text)

    def set_caption(self, text):
        """改标题文字（服务端下发的仪器名/通道名等）。"""
        self._caption.setText(str(text))

    def set_suffix(self, unit):
        """改单位（与服务值在同一个标签里 ⇒ 重新拼富文本）。"""
        self._unit_text = unit
        self._value.set_plain(self._rich(self._plain, self._color),
                              self._plain)

    def unit_text(self):
        """当前单位文本（单位已并入数值标签 ⇒ 没有独立的单位标签了）。"""
        return self._unit_text

    def plain_text(self):
        """**纯文本**的当前数值（给回归锁/诊断用；富文本里读不到）。"""
        return self._plain


class RemainDial(QWidget):
    """剩余时间**表盘**（自动模式卡；2026-11-12 用户定案「要有表盘，
    文字也要」）。

    画法：270° 缺口圆环（缺口朝下，像仪表）+ 中央读数（上方小字
    「剩余/已运行」，下方倒计时——**中间已有数字，旁边不再重复时间**）。
      · total > 0（反洗/顺洗/产水定时兜底）：环 = **剩余/总时长**（满环 =
        刚起步，随时间递减，像"燃油表"）；中央显示**剩余** mm:ss；最后
        一分钟转橙、归零转红。
        [为什么不是"已过/总"填满] 填满式在阶段初期几乎是一圈空环 + 一小
        段圆弧，看上去像加载转圈/没画完（模拟运行截图实见）；剩余式"满
        环 → 逐步消耗"才是仪表该有的读法。
      · total = 0（产水按压差切换 / 单模式启动）：没有总时长可画进度
        ⇒ 只画灰环，中央显示**已运行** mm:ss（不编造倒计时）。
      · elapsed < 0（没在自动运行）：灰环 + 「--」。

    [为什么不在这里画文字说明] 「剩余 8分42秒 / 反洗 10 分钟」由页面放在
    表盘旁边（用户口径「文字也要」）；表盘只负责"一眼的量感"，
    两个元素各自能独立隐藏/置灰。

    [尺寸为什么要 setFixedSize] 表盘是圆形，交给布局拉伸会变成椭圆；
    直径走 theme.font_px ⇒ 跟着界面缩放走（板端 1.5 不显小）。
    """

    #: 表盘直径基准（font_px 输入；1.5 缩放下 ≈ 132px）。
    #: [2026-11-12 用户定案「表盘大点」·三调] 44→58→72→**88**：72（≈108px）
    #: 时用户仍指出「表盘上面还有空间，不能跨到『自动模式』那行吗」——
    #: 表盘改成跨占标题行后这 132px 用的是原本空着的竖向空白，卡片反而
    #: 比 108px 版本略矮。
    DIAL_PX = 88

    #: 圆环缺口（度）：从 225° 起顺时针画 270°，缺口在正下方。
    ARC_START_DEG = 225
    ARC_SPAN_DEG = 270

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self._elapsed_ms = -1
        self._total_ms = 0
        #: 环颜色（hex；测试/调用方可读到当前状态色）。
        self._color = theme.CLR_OFFLINE
        #: 中央小字（「剩余」/「已运行」/空）。
        self._caption = ""
        side = theme.font_px(self.DIAL_PX, minimum=24)
        self.setFixedSize(side, side)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        self.setToolTip("当前自动模式阶段的剩余时间（服务端计时）")

    # ---- 数据 ----
    def set_state(self, elapsed_ms, total_ms):
        """落一次状态；同值早退（每秒刷新不白重绘）。"""
        elapsed_ms = -1 if elapsed_ms is None else int(elapsed_ms)
        total_ms = max(0, int(total_ms or 0))
        if (elapsed_ms == self._elapsed_ms
                and total_ms == self._total_ms):
            return
        self._elapsed_ms = elapsed_ms
        self._total_ms = total_ms
        # [tooltip 只在数据变时写] 放进 paintEvent 会在每次重绘时改属性
        # （无意义的属性变更 + 触发工具提示系统）。
        remain = self.remain_ms()
        if elapsed_ms < 0:
            tip = "不在自动运行"
            self._caption = ""
            self._color = theme.CLR_OFFLINE
        elif remain is None:
            tip = "已运行 %s（本阶段无定时）" % self.fmt_ms(elapsed_ms)
            self._caption = "已运行"
            self._color = self._info_color()
        else:
            tip = "剩余 %s（本阶段共 %s）" % (self.fmt_ms(remain),
                                          self.fmt_ms(total_ms))
            self._caption = "剩余"
            if remain > 60000:
                self._color = self._info_color()
            elif remain > 0:
                self._color = theme.CLR_WARN
            else:
                self._color = theme.CLR_FAULT
        if self.toolTip() != tip:
            self.setToolTip(tip)
        self.update()

    # ---- 状态读口（回归锁用，不靠截图像素）----
    @staticmethod
    def _info_color():
        """剩余 >60s 的信息蓝（亮=CLR_INFO；深套跟随 C 端 HOME_CLR_INFO）。"""
        if theme.is_dark_skin():
            return theme.DIAL_INFO_DARK
        return theme.CLR_INFO

    def caption(self):
        """中央小字（「剩余」/「已运行」/空）。"""
        return self._caption

    def color_hex(self):
        """当前环色（theme.CLR_*）。"""
        return self._color

    def elapsed_ms(self):
        """已运行毫秒（-1 = 不在运行/没有阶段）。"""
        return self._elapsed_ms

    def remain_ms(self):
        """剩余毫秒（None = 不可知）。"""
        if self._elapsed_ms < 0 or self._total_ms <= 0:
            return None
        return max(0, self._total_ms - self._elapsed_ms)

    def is_running(self):
        return self._elapsed_ms >= 0

    @staticmethod
    def fmt_ms(ms):
        """时长 → 「mm:ss」/「H:mm:ss」（表盘中央，短、定宽好读）。"""
        secs = max(0, int(ms or 0) // 1000)
        hours, rem = divmod(secs, 3600)
        minutes, seconds = divmod(rem, 60)
        if hours:
            return "%d:%02d:%02d" % (hours, minutes, seconds)
        return "%02d:%02d" % (minutes, seconds)

    # ---- 绘制 ----
    def paintEvent(self, _event):
        side = min(self.width(), self.height())
        if side <= 0:
            return
        pad = max(3.0, side * 0.09)
        ring_w = max(3.0, side * 0.09)
        rect = QRectF(pad, pad, side - 2 * pad, side - 2 * pad)
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        colors = theme.dial_colors()

        # ---- 底盘整圆（2026-11-13·对齐 C 端）---- C 端 HsfHomePage 先画
        # 主题白/深盘整圆再叠 270° 环 ⇒ 缺口与环内透**盘面色**；Python 原先
        # 无底盘（透卡片底色），两端分叉。半径=环外接圆（rect 半径 + 半带宽），
        # 与 C hsfHomeDialRenderStatic 的 disc=bandOuter 同口径。
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(colors["disc"]))
        p.drawEllipse(rect.adjusted(-ring_w / 2.0, -ring_w / 2.0,
                                    ring_w / 2.0, ring_w / 2.0))

        track = QPen(QColor(colors["track"]), ring_w)
        track.setCapStyle(Qt.RoundCap)
        p.setPen(track)
        p.drawArc(rect, self.ARC_START_DEG * 16, -self.ARC_SPAN_DEG * 16)

        total = self._total_ms
        elapsed = self._elapsed_ms
        remain = self.remain_ms()
        color = QColor(self._color)
        big_text = "--"
        if elapsed >= 0 and total > 0:
            frac = min(1.0, max(0.0, float(remain) / float(total)))
            arc = QPen(color, ring_w)
            arc.setCapStyle(Qt.RoundCap)
            p.setPen(arc)
            p.drawArc(rect, self.ARC_START_DEG * 16,
                      int(-self.ARC_SPAN_DEG * 16 * frac))
            big_text = self.fmt_ms(remain)
        elif elapsed >= 0:
            # 无定时（产水按压差切换）：只有"已运行"是事实。
            big_text = self.fmt_ms(elapsed)

        # ---- 中央两行：小字 + 读数（色走 theme 令牌，亮/深一套源）----
        if self._caption:
            cap = QFont(self.font())
            cap.setPixelSize(max(9, int(side * 0.125)))
            p.setFont(cap)
            p.setPen(QPen(QColor(colors["caption"])))
            p.drawText(QRectF(0, side * 0.235, side, side * 0.20),
                       Qt.AlignCenter, self._caption)
        font = QFont(self.font())
        # [0.24] 数字落在内圈里（圆环内径 ≈ side*0.66，"H:mm:ss" 最长）。
        font.setPixelSize(max(10, int(side * 0.24)))
        font.setBold(True)
        p.setFont(font)
        p.setPen(QPen(QColor(colors["text"]) if elapsed >= 0
                      else QColor(theme.CLR_OFFLINE)))
        p.drawText(QRectF(0, side * 0.40, side, side * 0.34),
                   Qt.AlignCenter, big_text)
        p.end()


# ============================================================================
#                              标题栏
# ============================================================================

class QrBadge(QPushButton):
    """标题栏的**二维码徽章**（点击弹出访问地址弹卡）。

    [为什么自绘 2026-11-18 用户「顶部蓝色状态栏这里可以插入一个二维码
    位图」] 不引图标资源：用 QPainter 画一个**QR 风格小图案**（三个定位
    回字 + 点阵）——白色线条画在深蓝标题栏上，一眼可辨"这是二维码"；
    hover 淡白底（与 ☰ 菜单钮同款反馈）。触摸目标 32x32（标题栏高 40 内）。
    """

    def __init__(self, parent=None):
        QPushButton.__init__(self, parent)
        self.setFixedSize(32, 32)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("网页访问地址二维码（手机扫码打开）")
        self.setStyleSheet(
            "QPushButton { background: transparent; border: none; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.12);"
            " border-radius: 6px; }")

    def paintEvent(self, _event):
        from PyQt5.QtGui import QColor, QPainter, QPen
        from PyQt5.QtCore import QRectF
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        side = 22.0                       # 图案边长（按钮 32 内留边）
        ox = (self.width() - side) / 2.0
        oy = (self.height() - side) / 2.0
        white = QColor("#FFFFFF")
        pen = QPen(white)
        pen.setWidthF(1.6)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        # 三个定位回字（左上/右上/左下）——二维码的辨识特征。
        cell = side / 7.0                 # 7x7 概念网格
        corner = cell * 3.0
        for (gx, gy) in ((0, 0), (4, 0), (0, 4)):
            x = ox + gx * cell
            y = oy + gy * cell
            p.drawRect(QRectF(x, y, corner, corner))
            inner = corner - cell * 1.6
            p.drawRect(QRectF(x + cell * 0.8, y + cell * 0.8,
                              inner, inner))
            p.fillRect(QRectF(x + cell * 1.15, y + cell * 1.15,
                              inner - cell * 0.7, inner - cell * 0.7),
                       white)
        # 右下角点阵（示意数据区）。
        for (gx, gy) in ((4, 4), (5, 4), (4, 5), (6, 5), (5, 6), (6, 6)):
            p.fillRect(QRectF(ox + gx * cell + cell * 0.25,
                              oy + gy * cell + cell * 0.25,
                              cell * 0.5, cell * 0.5), white)
        p.end()


class CameraBadge(QPushButton):
    """标题栏的**相机徽章**（点击弹/收悬浮相机画面卡）。

    [为什么自绘] 与二维码徽章同款口径：不引图标资源，QPainter 画白色
    相机轮廓（机身 + 镜头 + 取景凸起）——深蓝标题栏上一眼可辨；hover
    淡白底，触摸目标 32×32。标题栏只放**位图**，不开流：相机帧只在
    悬浮卡打开期间订阅，关闭即退订（用户口径：免得后台一直传大流量）。
    """

    def __init__(self, parent=None):
        QPushButton.__init__(self, parent)
        self.setFixedSize(32, 32)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("相机画面（点开/再点关闭）")
        self.setAccessibleName("相机画面")
        self.setStyleSheet(
            "QPushButton { background: transparent; border: none; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.12);"
            " border-radius: 6px; }")

    def paintEvent(self, _event):
        from PyQt5.QtGui import QColor, QPainter, QPen
        from PyQt5.QtCore import QRectF
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        # 22×22 图案区（按钮 32 内留边，与 QrBadge 同档）
        ox = (self.width() - 22.0) / 2.0
        oy = (self.height() - 22.0) / 2.0
        white = QColor("#FFFFFF")
        pen = QPen(white)
        pen.setWidthF(1.6)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        # 机身（圆角矩形）+ 顶部取景凸起
        p.drawRoundedRect(QRectF(ox + 2.0, oy + 6.0, 18.0, 12.5), 2.2, 2.2)
        p.drawRoundedRect(QRectF(ox + 7.5, oy + 3.2, 7.0, 3.6), 1.0, 1.0)
        # 镜头
        p.drawEllipse(QRectF(ox + 7.0, oy + 8.3, 8.0, 8.0))
        # 闪光窗
        p.fillRect(QRectF(ox + 16.0, oy + 8.2, 2.2, 1.6), white)
        p.end()


class TitleBar(QWidget):
    """顶部标题栏：☰ + 居中标题 + 运行模式钮 + 健康 + 时钟 + 温湿度。

    [布局与 C 端一致] 标题在 ☰ 区与右侧块之间**居中**（不是紧贴左边），
    右侧从右往左依次是 湿度 / 温度 / 时钟 / 健康 / 模式钮。
    窄栏（< 700px）时钟只显示时分秒，日期行隐藏。
    """

    menuClicked = pyqtSignal()
    modeClicked = pyqtSignal()
    qrClicked = pyqtSignal()
    cameraClicked = pyqtSignal()

    #: 窄栏阈值（C 端 m_narrowTime 的触发口径）。
    NARROW_W = 700

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self.setObjectName("HsfTitleBar")
        self.setFixedHeight(theme.TITLE_BAR_H)
        # [必须用 ID 选择器] 裸 `QWidget { background-color: ... }` 会级联到
        # 所有子控件（把按钮/标签底色也刷成深蓝）。用 #HsfTitleBar 精确命中。
        self.setStyleSheet(theme.title_bar_qss())
        # 纯 QWidget 默认不画样式表里的背景，必须显式开启。
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 10, 0)
        lay.setSpacing(10)

        # ---- ☰ 菜单 ----
        self.menu_btn = QPushButton("\u2630")     # ☰
        self.menu_btn.setFixedSize(46, theme.TITLE_BAR_H)
        self.menu_btn.setCursor(Qt.PointingHandCursor)
        self.menu_btn.setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " color: #FFFFFF; font-size: 30px; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.12); }")
        self.menu_btn.clicked.connect(self.menuClicked.emit)
        lay.addWidget(self.menu_btn)

        # ---- 时钟（紧跟菜单图标右侧，**左对齐**）----
        #
        # [为什么放左边] 原来挤在最右侧，与温度/湿度/健康/模式钮抢地方：
        # 窗口一窄就被裁掉，而且"看时间"是最高频动作，放左边一眼就看到。
        # 放左后右侧那组（模式/健康/温湿度）各自靠右排，互不挤压。
        clock_box = QWidget()
        clock_box.setStyleSheet("background: transparent;")
        cb = QVBoxLayout(clock_box)
        cb.setContentsMargins(0, 2, 0, 2)
        cb.setSpacing(0)
        self.date_label = QLabel("")
        self.date_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self.date_label.setStyleSheet(
            "QLabel { color: #C5D8E8; font-size: 20px; background: transparent; }")
        cb.addWidget(self.date_label)
        self.clock_label = QLabel("")
        self.clock_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self.clock_label.setStyleSheet(
            "QLabel { color: #FFFFFF; font-size: 20px; background: transparent; }")
        cb.addWidget(self.clock_label)
        self._clock_box = clock_box
        # 定宽：秒数变化时不让它左右抖动（否则标题会跟着晃）
        clock_box.setMinimumWidth(96)
        lay.addWidget(clock_box)

        # ---- 居中标题 ----
        self.title = QLabel("主界面")
        self.title.setStyleSheet(
            "QLabel { color: #FFFFFF; font-size: 24px; font-weight: bold;"
            " background: transparent; }")
        self.title.setAlignment(Qt.AlignCenter)
        lay.addWidget(self.title, 1)

        # ---- 相机徽章（**二维码左侧**；2026-11-22 用户「顶部状态栏加相机
        #  位图」）----
        # 点击弹/收悬浮相机画面卡（camera_popup.CameraPopupCard）。标题栏
        # 本身只画位图，不订相机流；悬浮卡打开期间才订阅、关闭即退订。
        self.camera_btn = CameraBadge()
        self.camera_btn.clicked.connect(self.cameraClicked.emit)
        lay.addWidget(self.camera_btn)

        # ---- 二维码徽章（**模式钮左侧**；2026-11-19 用户「二维码位图
        #  左移到自动这两个字左边」）----
        # 点击 → 主窗弹出「网页访问地址」弹卡（qr_access.QrPopupCard）：
        # 局域网 / 远程两个二维码可切换。放这里是因为"把网页地址发出去"
        # 是最高频的分享动作，任何页面都可直达（不必翻设置页）。
        self.qr_btn = QrBadge()
        self.qr_btn.clicked.connect(self.qrClicked.emit)
        lay.addWidget(self.qr_btn)

        # ---- 运行模式钮（手动/自动，可点切换） ----
        self.mode_btn = QPushButton("手动")
        self.mode_btn.setFixedHeight(24)
        self.mode_btn.setMinimumWidth(56)
        self.mode_btn.setCursor(Qt.PointingHandCursor)
        self.mode_btn.setToolTip("点击切换运行模式（需 root）")
        self.mode_btn.clicked.connect(self.modeClicked.emit)
        self._mode_auto = False
        self._mode_known = False
        #: [2026-11-22 本地设置同步] 标题栏新元素的显隐闸（缺省全显）。
        self._qr_visible = True
        self._mode_visible = True
        self._health_visible = True
        self._camera_visible = True
        self._last_health = (0, 0)
        self._restyle_mode()
        lay.addWidget(self.mode_btn)

        # ---- 自动运行状态（只读回显，紧贴模式钮右侧）----
        #
        # [为什么要单独一个标签] 模式钮只说明"人把开关拨到哪一侧"。
        # 操作员真正要确认的是"自动逻辑此刻在不在跑"。两者正常同步，
        # 但控制器没起来（装配时序错/异常退出/将来做暂停）时，只看模式钮
        # 会得到"自动在跑"的错误印象 —— 那种假正常比报故障更难查。
        #
        # 三种显示：
        #   自动 · 运行中   橙底（与模式钮同色系，一眼看出是同一条信息链）
        #   自动 · 已停止   灰底（模式自动但控制器没跑 = 异常，要看得见）
        #   手动            不显示（手动模式下"自动是否运行"无意义，别占地方）
        self.auto_state = QLabel("")
        self.auto_state.setFixedHeight(22)
        self.auto_state.setAlignment(Qt.AlignCenter)
        self.auto_state.setMinimumWidth(84)
        self._auto_running = None
        self._restyle_auto_state()
        lay.addWidget(self.auto_state)

        # ---- 设备健康指示 ----
        self.health = QLabel("")
        self.health.setStyleSheet(
            "QLabel { color: #D0D0D0; background: transparent; font-size: 20px; }")
        lay.addWidget(self.health)

        # ---- 温度 / 湿度（**上下两行**）----
        # [为什么上下排] 并排时顶栏右半部分被拉得很长，时钟/模式钮/
        # 健康指示全被挤压；改成两行后每行只占一个数值宽度，顶栏更紧凑，
        # 一眼也能分清哪行是温度哪行是湿度。
        # [为什么带单位占位] 裸 "--" 在深蓝底上像两条断裂的横线，
        # 带单位后即使无数据也能读出「这是温度/湿度」。
        th_box = QWidget()
        th_box.setStyleSheet("background: transparent;")
        tb = QVBoxLayout(th_box)
        tb.setContentsMargins(0, 3, 0, 3)
        tb.setSpacing(0)
        self.temp_label = QLabel("--℃")
        self.hum_label = QLabel("--%RH")
        for lbl in (self.temp_label, self.hum_label):
            lbl.setStyleSheet(
                "QLabel { color: #FFFFFF; font-size: 18px;"
                " background: transparent; }")
            lbl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            lbl.setMinimumWidth(64)
            tb.addWidget(lbl)
        # [现场口径] 这对温湿度是**电箱内**的读数（变送器装在电控柜里），
        # 不是环境温度；tooltip 写清楚，避免现场误解。
        self.temp_label.setToolTip("电箱温度")
        self.hum_label.setToolTip("电箱湿度")
        lay.addWidget(th_box)
        # [2026-11-13 合并设置页] 温湿度块的显隐要能被「悬浮窗」分区开关
        # 控制 ⇒ 留住引用（原来只是局部变量，外部拿不到）。
        self._th_box = th_box

        # ---- 时钟定时器（1s，C 端同款） ----
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self.refresh_clock)
        self._timer.start()
        self.refresh_clock()

    # ---- 时钟 ----
    def refresh_clock(self):
        now = QDateTime.currentDateTime()
        self.clock_label.setText(now.toString("HH:mm:ss"))
        self.date_label.setText(now.toString("yyyy-MM-dd"))
        self.date_label.setVisible(self.width() >= self.NARROW_W)

    def resizeEvent(self, event):
        QWidget.resizeEvent(self, event)
        self.date_label.setVisible(self.width() >= self.NARROW_W)

    # ---- 文本/状态 ----
    def set_title(self, text):
        self.title.setText(text)

    def set_clock_visible(self, visible):
        """标题栏时钟显隐（本地设置页「悬浮窗」分区·显示时钟开关）。"""
        self._clock_box.setVisible(bool(visible))

    def set_temphum_visible(self, visible):
        """标题栏温湿度显隐（本地设置页「悬浮窗」分区·显示温湿度开关）。"""
        self._th_box.setVisible(bool(visible))

    def set_qr_visible(self, visible):
        """标题栏二维码徽章显隐（2026-11-22 本地设置同步扩展）。"""
        self._qr_visible = bool(visible)
        self.qr_btn.setVisible(self._qr_visible)

    def set_camera_visible(self, visible):
        """标题栏相机徽章显隐（2026-11-22 本地设置同步扩展）。"""
        self._camera_visible = bool(visible)
        self.camera_btn.setVisible(self._camera_visible)

    def set_mode_visible(self, visible):
        """标题栏运行模式钮 + 自动运行徽标显隐（同一条信息链一起藏）。"""
        self._mode_visible = bool(visible)
        self.mode_btn.setVisible(self._mode_visible)
        self._restyle_auto_state()

    def set_health_visible(self, visible):
        """标题栏设备健康指示显隐（异常才显示的语义不变）。"""
        self._health_visible = bool(visible)
        self.set_health(*self._last_health)

    def set_temperature(self, celsius, valid=True):
        self.temp_label.setText("%.1f\u2103" % celsius if valid else "--")

    def set_humidity(self, percent, valid=True):
        self.hum_label.setText("%.1f%%" % percent if valid else "--")

    def set_health(self, fault_count, offline_count):
        """健康指示：全部正常时不显示（不占视觉重量）。

        [2026-11-19 用户「间距可以去掉」] 空文本的 QLabel 仍被布局分到
        ~20px 宽 ⇒ 二维码徽章与温湿度之间夹一块空白 ⇒ 正常时整件隐藏。
        """
        if fault_count or offline_count:
            parts = []
            if fault_count:
                parts.append("故障%d" % fault_count)
            if offline_count:
                parts.append("离线%d" % offline_count)
            self._last_health = (int(fault_count or 0),
                                 int(offline_count or 0))
            self.health.setText(" / ".join(parts))
            self.health.setStyleSheet(
                "QLabel { color: #FFB4B4; background: transparent;"
                " font-size: 20px; font-weight: bold; }")
            self.health.setVisible(self._health_visible)
        else:
            self._last_health = (0, 0)
            # 全部正常时不占位（顶栏空间宝贵，异常才提示）。
            self.health.setText("")
            self.health.setVisible(False)

    def set_mode(self, auto, known=True):
        self._mode_auto = bool(auto)
        self._mode_known = bool(known)
        self._restyle_mode()
        # 模式变了，"自动运行"标签的可见性/含义也跟着变
        self._restyle_auto_state()

    def set_auto_running(self, running, known=True):
        """设置自动控制器运行状态。

        :param running: True=在跑 False=已停；known=False 时按"未知"显示。
        """
        self._auto_running = None if not known else bool(running)
        self._restyle_auto_state()

    def _restyle_auto_state(self):
        """自动运行标签的文案与配色。

        [手动模式下隐藏] 手动时"自动是否在跑"没有意义，留着只会占顶栏
        宝贵宽度（窄窗口最先被挤掉的就是这些坑位）。
        [「自动 · 已停止」是**正常默认态**，不是异常] 2026-11-05 订正：
        切到自动模式只代表"授权自动可以跑"，要真的跑还得人按一次启动。
        初版把它标成故障红，等于把正常待命说成异常，反而误导操作员。
        三种状态用颜色区分"要不要管"：橙=在跑、灰=待命/未知、红=不用。
        """
        if (not self._mode_visible or not self._mode_known
                or not self._mode_auto):
            self.auto_state.setVisible(False)
            return
        self.auto_state.setVisible(True)
        if self._auto_running is None:
            text, bg, fg = "自动 · 未知", "#5A6572", "#E0E0E0"
        elif self._auto_running:
            text, bg, fg = "自动 · 运行中", "#ED6C02", "#FFFFFF"
        else:
            # 已授权但没按启动：正常待命，中性灰（不是故障红）
            text, bg, fg = "自动 · 已停止", "#5A6572", "#E0E0E0"
        self.auto_state.setText(text)
        self.auto_state.setToolTip(
            "自动控制器的实际运行状态（不是模式开关的位置）。\n"
            "「自动 · 已停止」= 已授权自动但还没按过启动（正常待命）。\n"
            "切到自动模式只是授权，要真正自动运行需在「远端设置」按启动。")
        self.auto_state.setStyleSheet(
            "QLabel { background-color: %s; color: %s; border-radius: 3px;"
            " font-size: 18px; padding: 0 6px; }" % (bg, fg))

    def _restyle_mode(self):
        if not self._mode_known:
            text, bg, fg = "未知", "#5A6572", "#D0D0D0"
        elif self._mode_auto:
            text, bg, fg = "自动", "#ED6C02", "#FFFFFF"
        else:
            text, bg, fg = "手动", "#2E7D32", "#FFFFFF"
        self.mode_btn.setText(text)
        self.mode_btn.setStyleSheet(
            "QPushButton { background-color: %s; color: %s; border: none;"
            " border-radius: 4px; padding: 3px 10px; font-size: 18px;"
            " font-weight: bold; }"
            "QPushButton:hover { background-color: %s; }"
            % (bg, fg, theme.CLR_ACCENT_LINE))

    def set_mode_enabled(self, enabled):
        self.mode_btn.setEnabled(enabled)


# ============================================================================
#                              状态栏
# ============================================================================

class StatusBar(QWidget):
    """底部状态栏：黑底 28px，左侧状态文本 + 连接信息 + 右侧账户菜单。

    [不显示时钟 2026-11-06] 顶栏右上角已经有时间（TitleBar 的 clock/date），
    底部再来一个是重复占位。用户明确要求：「客户端顶部有显示时间了，底部
    不用显示时间了」。故这里只留状态消息与连接信息。

    [账户入口 2026-11-11] 右下角加可点「账号·角色 ▾」按钮，点击弹菜单
    （切换账户 / 退出登录）。位置对齐 C 端 `HsfStatusBar` 同日把账户从
    标题栏搬到状态栏右下角的口径（`root·管理员` / 未登录 `未登录`）。
    **标题栏右侧已 7 个元素**（时钟/温湿度/健康/模式钮/自动状态），源码
    记着两次「抢地方被裁」的实锅 ⇒ 不往顶栏加。
    `message` 有 stretch=1，左侧文本天然给右侧让位（等价 C 端保留宽 +
    ElideRight），不会挤没。
    """

    #: 账户菜单动作（MainWindow 接到后执行；控件本身不碰业务逻辑）。
    switchUserRequested = pyqtSignal()
    logoutRequested = pyqtSignal()

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self.setObjectName("HsfStatusBar")
        self.setFixedHeight(theme.STATUS_BAR_H)
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setStyleSheet(theme.status_bar_qss())

        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 0, 12, 0)
        lay.setSpacing(16)

        self.message = QLabel("就绪")
        lay.addWidget(self.message, 1)

        self.conn = QLabel("未连接")
        lay.addWidget(self.conn)

        # ---- 账户入口（右下角）----
        # [和 C 端同款三态] 未登录 / 账号·角色 / 只有账号；用 QPushButton
        # 而非 QLabel 是为了可点弹菜单（C 端目前只是只读自绘文本，本次
        # Python 端超出 C 端，AGENTS.md 已标注待拍板是否同步）。
        self.account_btn = QPushButton("未登录  ▾")
        self.account_btn.setObjectName("HsfAccountBtn")
        self.account_btn.setCursor(Qt.PointingHandCursor)
        self.account_btn.setFlat(True)
        self.account_btn.clicked.connect(self._show_account_menu)
        lay.addWidget(self.account_btn)

    # ---- 账户显示 / 菜单 ----

    def set_account(self, user, role_name="", logged_in=True):
        """刷新账户按钮文案（**同值早退**，配合 1Hz 兜底零脏区）。

        :param user: 登录账号（空 = 未登录）
        :param role_name: 角色显示名（如「管理员」），可空
        :param logged_in: 是否已连接（False 时强制显示「未登录」）
        """
        if not logged_in or not user:
            text = "未登录  ▾"
        else:
            text = ("%s·%s" % (user, role_name)) if role_name else str(user)
            text += "  ▾"
        if self.account_btn.text() != text:
            self.account_btn.setText(text)

    def account_text(self):
        """当前账户按钮文案（测试/诊断入口）。"""
        return self.account_btn.text()

    def _show_account_menu(self):
        """点账户按钮 → 弹菜单（Popup 语义：点别处自动收）。"""
        menu = QMenu(self)
        menu.addAction("切换账户", self.switchUserRequested.emit)
        menu.addAction("退出登录", self.logoutRequested.emit)
        # [在按钮下方弹] 状态栏贴着窗口底边，往上弹才不会被裁到屏外。
        pos = self.account_btn.mapToGlobal(
            QPoint(0, -menu.sizeHint().height()))
        menu.exec_(pos)

    # ---- 状态文本 ----

    def set_message(self, text):
        self.message.setText(text)

    def set_connection(self, text, ok=True):
        self.conn.setText(text)
        self.conn.setStyleSheet(
            "QLabel { color: %s; background: transparent; }"
            % ("#A8D8A8" if ok else "#FFB4B4"))


# ============================================================================
#                              导航面板
# ============================================================================

class NavPanel(QWidget):
    """左侧停靠导航：树形条目（父节点 + 缩进子节点），当前页常亮。

    与 C 端 HsfNavPanel 同源：条目来自同一棵树（pages.NAV_TREE），
    支持两级缩进、分组标题不可点、当前页高亮。

    [宽度/高度按字体实测 2026-11-22] 界面缩放 150% 时现场截图条目文字被
    面板右边界横向裁切：QApplication 字体是放大后的（15pt），面板宽度却
    仍是设计稿的固定 176px，而 **QPushButton 不会自动省略号**（超宽直接
    裁字）。⇒ 面板最小/首选宽度与条目高度一律由 QFontMetrics 按**实际
    字体**实测，不再假设某个固定像素能装下文字。
    """

    navigate = pyqtSignal(int)

    # ---- 度量常量（与 nav_panel_qss / 条目内联 QSS 同源，改一处必须同步）----
    #: 条目左右内边距（px，= nav_panel_qss 的 `padding: 8px 12px` 左右值）。
    _ITEM_PAD_H = 12
    #: 条目上下内边距（px，= nav_panel_qss 的 `padding: 8px 12px` 上下值）。
    _ITEM_PAD_V = 8
    #: 二级条目的每级缩进（px；内联 QSS 里按 `indent * 2` 叠加）。
    _CHILD_INDENT = 12
    #: 面板内容布局左右边距（px，= setContentsMargins(6, 4, 6, 8) 的左右值）。
    _CONTENT_MARGIN_X = 6
    #: 宽度余量（px）：留一点防选中态 bold 增宽与亚像素取整累积。
    _WIDTH_SLACK = 8
    #: 分组标题字号（px）。**必须与构造里内联 QSS 的 `font-size: 18px`
    #: 同值**——宽度按字体实测，量的时候若用条目字体（更小）就会少算。
    _GROUP_FONT_PX = 18

    def __init__(self, tree, parent=None):
        """tree: [(caption, page_index, child_of), ...] —— -1 表示顶层。"""
        QWidget.__init__(self, parent)
        self.setObjectName("HsfNavPanel")
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setStyleSheet(theme.nav_panel_qss())
        self.setMinimumWidth(theme.NAV_MIN_W)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        title = QLabel("导航")
        title.setObjectName("HsfNavTitle")
        outer.addWidget(title)

        content = QWidget()
        content.setObjectName("HsfNavContent")
        self._lay = QVBoxLayout(content)
        self._lay.setContentsMargins(6, 4, 6, 8)
        self._lay.setSpacing(2)

        # [2026-11-22 高缩放下窗口矮 ⇒ 条目必须可滚，不能被压扁重叠]
        # 条目高度按字体实测（1.5× 时约 48px）后，18 条合计 ~860px；
        # 窗口一矮（现场 ~560px 高），QVBoxLayout 把按钮压到最小高以下
        # ⇒ 文字上下互相叠、右边被面板裁掉（用户截图）。包一层滚动区后
        # 条目保持实测高度，装不下就出现竖向滚动条。
        scroll = QScrollArea()
        scroll.setObjectName("HsfNavScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll.setStyleSheet(
            "QScrollArea#HsfNavScroll { background: transparent;"
            " border: none; }")
        scroll.setWidget(content)
        outer.addWidget(scroll, 1)

        self._buttons = []
        #: 与 _buttons 同序的缩进（px，0=一级 / 12=二级）。
        #: [为什么单独一条列表] `_buttons` 的 (page_index, btn) 二元组形状
        #: 是回归锁（test_hsf_regression §30）直接解包依赖的契约，不能改成
        #: 三元组；缩进只在宽度实测里用，平行存一份即可。
        self._indents = []
        self._current = -1

        for caption, page_index, child_of in tree:
            btn = QPushButton(caption)
            btn.setObjectName("HsfNavItem")
            btn.setCursor(Qt.PointingHandCursor)
            # 缩进：二级条目左移出层级感（C 端同款层级缩进口径）。
            # [注意] 这个 12px 基础值必须与 _ITEM_PAD_H 一致（宽度实测按
            # 「_ITEM_PAD_H + 缩进」算左侧占用）。
            indent = self._CHILD_INDENT if child_of >= 0 else 0
            if page_index < 0:
                # 分组标题：不可点，只是视觉分隔。
                btn.setEnabled(False)
                btn.setStyleSheet(
                    "QPushButton#HsfNavItem { color: #8B98A5; border: none;"
                    " background: transparent; text-align: left;"
                    " padding-left: %dpx; font-size: %dpx; }"
                    % (self._ITEM_PAD_H + indent * 2, self._GROUP_FONT_PX))
            else:
                btn.setStyleSheet(
                    "QPushButton { text-align: left; padding-left: %dpx; }"
                    % (self._ITEM_PAD_H + indent * 2))
                idx = page_index
                btn.clicked.connect(lambda _checked=False, i=idx:
                                    self.navigate.emit(i))
            # [tooltip 兜底] 用户把分割条拖到极窄时按钮不会省略号、直接裁字
            # ⇒ 整条文本进 tooltip。**只是兜底**，不是"修好"——面板宽度已
            # 按字体实测给足（见 _measure_preferred_width）。
            btn.setToolTip(caption)
            self._lay.addWidget(btn)
            self._buttons.append((page_index, btn))
            self._indents.append(indent)

        self._lay.addStretch(1)

        # 面板首选宽度按**实际字体**实测（条目已全部建好 ⇒ 字体/metrics
        # 都能取到真实值），minimumWidth 取实测值与设计下限的较大者。
        self._preferred_width = self._measure_preferred_width()
        self.setMinimumWidth(max(theme.NAV_MIN_W, self._preferred_width))
        self.set_compact(False)

    # ---- 字体实测（宽度/高度）----

    def _item_font(self, btn, page_index):
        """条目**实际渲染**用的字体。

        分组标题的内联 QSS 把字号钉成 18px（不是应用字体）⇒ 必须单独
        构造一个 pixelSize=18 的字体来量，否则量出来比实际显示窄。
        """
        font = QFont(btn.font())
        if page_index < 0:
            font.setPixelSize(self._GROUP_FONT_PX)
        return font

    def _item_height(self, btn, page_index, compact):
        """条目高度 = max(设计稿基准高, 字体行高 + 上下内边距)。

        [为什么必须加 padding] QSS 的 padding 占**盒内**空间，文字可画
        区域 = 高度 - 上下各 8px；字体行高超过「基准高 - 16」时写死高度
        就把字上下削掉（缩放 1.5× 时行高约 28px ⇒ 需要 44px）。
        """
        base = theme.NAV_ITEM_H_MOBILE if compact else theme.NAV_ITEM_H
        fm = QFontMetrics(self._item_font(btn, page_index), btn)
        return max(base, fm.height() + 2 * self._ITEM_PAD_V)

    def _measure_preferred_width(self):
        """按实际字体算出「完整放得下所有条目」的面板宽度（px）。

        组成：条目文本宽 + 左 padding（一级 12px；二级另加 24px 缩进）
        + 右 padding 12px，再叠加内容布局左右边距 6+6px 与 8px 余量。

        [为什么选中态也按 bold 量] `QPushButton#HsfNavItem:checked` 是
        `font-weight: bold`，加粗会把文字加宽几像素 ⇒ 按常规体量出来的
        宽度在「该页一选中」时仍可能裁字。
        """
        need = 0
        for (page_index, btn), indent in zip(self._buttons, self._indents):
            font = self._item_font(btn, page_index)
            bold = QFont(font)
            bold.setBold(True)
            caption = btn.text()
            text_w = max(
                QFontMetrics(font, btn).horizontalAdvance(caption),
                QFontMetrics(bold, btn).horizontalAdvance(caption))
            need = max(need, text_w + self._ITEM_PAD_H + indent * 2
                       + self._ITEM_PAD_H)
        # 竖向滚动条出现时占内容宽 ⇒ 预留 theme.SCROLL_W，否则最长条目会被
        # 滚动条压掉几个像素（20+px 的触摸滚动条尤其明显）。
        return (need + 2 * self._CONTENT_MARGIN_X + self._WIDTH_SLACK
                + theme.SCROLL_W)

    def preferred_width(self):
        """面板按字体实测得出的**首选宽度**（ChromeFrame 初始化分割条用）。"""
        return self._preferred_width

    def set_current(self, page_index):
        """高亮当前页条目。"""
        if self._current == page_index:
            return
        self._current = page_index
        for idx, btn in self._buttons:
            if idx < 0:
                continue
            btn.setChecked(idx == page_index)

    def set_compact(self, compact):
        """紧凑形态：条目加高（触控友好），高度仍按当前字体实测。

        [为什么不再写死 40/44] 缩放 1.5× 时字体行高约 28px、加上下 8+8
        内边距需要 44px，写死 40px 会把字削掉；紧凑态同理随字体走。
        """
        for page_index, btn in self._buttons:
            btn.setFixedHeight(self._item_height(btn, page_index, compact))


# ============================================================================
#                              主窗三段式骨架
# ============================================================================

class ChromeFrame(QWidget):
    """三段式主窗骨架：顶部标题栏 + 中部（导航 | 堆栈）+ 底部状态栏。

    中部用 QSplitter 承载「导航面板 | 页面堆栈」，与 C 端
    XSplitter 的口径一致（拖拽把手宽 8px，可改导航宽）。
    """

    def __init__(self, title, nav_tree, parent=None):
        QWidget.__init__(self, parent)
        # [为什么要 objectName + ID 选择器] 用无 ID 的 `background-color`
        # 会**级联到所有子控件**（Qt 样式表按控件树向下继承），把标题栏、
        # 导航、卡片的底色全刷成页面灰——之前截图里顶栏发灰就是这个原因。
        self.setObjectName("HsfChrome")
        self.setStyleSheet(
            "QWidget#HsfChrome { background-color: %s; }" % theme.COLOR_PAGE_BG)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self.title_bar = TitleBar()
        outer.addWidget(self.title_bar)
        self.title_bar.set_title(title)

        self.splitter = QSplitter(Qt.Horizontal)
        self.splitter.setHandleWidth(theme.SPLITTER_HANDLE_W)
        self.splitter.setChildrenCollapsible(False)

        self.nav = NavPanel(nav_tree)
        self.splitter.addWidget(self.nav)

        self.stack_host = QWidget()
        self.stack_host.setObjectName("HsfStackHost")
        self.stack_host.setStyleSheet(
            "QWidget#HsfStackHost { background-color: %s; }"
            % theme.COLOR_PAGE_BG)
        self.stack_layout = QVBoxLayout(self.stack_host)
        # [10→6 2026-11-12] 内容区外边距收窄，给页面腾一屏高度。
        self.stack_layout.setContentsMargins(6, 6, 6, 6)
        self.stack_layout.setSpacing(0)
        self.splitter.addWidget(self.stack_host)

        self.splitter.setStretchFactor(0, 0)
        self.splitter.setStretchFactor(1, 1)
        # [导航宽度按字体实测 2026-11-22] 固定 176px 在界面缩放 1.5× 时装
        # 不下条目文字（QPushButton 直接裁字，现场截图即此）⇒ 取设计缺省
        # 宽与面板实测首选宽度的较大者；面板自身的 minimumWidth 同样已是
        # 实测值（拖窄也拖不到裁字的宽度）。
        self.splitter.setSizes(
            [max(theme.NAV_DEFAULT_W, self.nav.preferred_width()), 1100])
        outer.addWidget(self.splitter, 1)

        self.status_bar = StatusBar()
        outer.addWidget(self.status_bar)

    def set_page_widget(self, widget):
        """把页面控件装进内容区（旧页面由调用方删除）。"""
        while self.stack_layout.count():
            item = self.stack_layout.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
        self.stack_layout.addWidget(widget)

    def set_nav_visible(self, visible):
        self.nav.setVisible(visible)
