# -*- coding: utf-8 -*-
"""屏保覆盖层（2026-11-26 用户「本地设置屏幕休眠扩展下支持屏保，就用这个
图标和文字做好将其位图化，并增加动画效果」）。

[为什么是本地覆盖层而不是硬件熄屏] 既有「屏幕休眠」走 C 桥接真正关显示器
（DPMS/SC_MONITORPOWER），屏保要显示动画就必须**不熄屏**——本地设置保存时
把屏保启用那一档的下发熄屏超时置 0（互斥），由本模块的全屏覆盖层接管。

[位图化] 图（六边形 HK 渐变 logo + 「美科沃腾」黑字）预处理成透明底 PNG
（`assets/screensaver_logo.png`），运行时 QPixmap 从文件加载并**平滑缩放**，
不实时重绘矢量；白底已在预处理阶段抠成 alpha。

[挂载] 覆盖层是主窗的普通子 QWidget（照 pages_camera._FullscreenPlayer 的
既有套路）：setGeometry(host.rect()) + 事件过滤跟随 resize + raise_()。
**不开新 QWindow**——xfwm4 会对独立 Tool 窗补装饰/抬面板（项目实锅）。

[动画] 定时器驱动「呼吸」（整体缓慢缩放 + 透明度起伏）+ 轻微上下浮动。
QGraphicsOpacityEffect 改透明度，setTransform 缩放走 QLabel 的 pixmap 重贴
（QPixmap 缓存位图，缩放是 GPU/CPU 拷贝，不重新解码）。项目惯例是 timer +
分级更新（见 widgets/camera_popup/flick），这里沿用。

[退出] 覆盖层响应**鼠标按下 / 键盘按下 / 触摸按下** ⇒ 立即关闭；**鼠标
移动与滚轮不退出**（向日葵等远控会持续发这两类事件，让它们退出会导致
「屏保跳出来一下就没了」——2026-11-26 现场实锅）。空闲计时由
ScreenSaverController 的应用级事件过滤器重置（任何输入都算活动）。
"""

import os
import time

from PyQt5.QtCore import (QEvent, QObject, QPoint, QRect, Qt, QTimer)
from PyQt5.QtGui import QColor, QPixmap
from PyQt5.QtWidgets import (QGraphicsOpacityEffect, QLabel, QVBoxLayout,
                             QWidget)

try:
    from . import theme
except ImportError:                                       # pragma: no cover
    theme = None

#: 位图资源（相对本包 assets/）
LOGO_FILE = "screensaver_logo.png"
#: 渐变流光帧目录（24 帧预生成 PNG，色带沿 logo 横向流动）
FRAMES_DIR = "screensaver_frames"
#: 文字强流光帧目录（文字也随色带明显变色；K_SS_TEXT_FLOW 勾选时用）
FRAMES_TEXT_DIR = "screensaver_frames_text"
FRAME_COUNT = 24
FLOW_CYCLE_MS = 3600.0       # 一轮色带流动周期（24 帧循环）

#: 动画档位（毫秒）。呼吸周期 ~6s，浮动周期 ~9s，60fps 太密用 30fps 足够。
#: [2026-11-26 用户「屏保图标太暗了，其他都填充白色背景吧」] 透明度呼吸
#: 取消（固定全不透明），图标不再变暗；只保留缓慢缩放 + 上下浮动动画。
FRAME_MS = 33
BREATH_MS = 4000.0
FLOAT_MS = 6000.0
# [2026-11-26 用户「动画效果看不出来」] 原 0.94~1.0（仅 6% 幅度）+ 浮动
# ±10px 在 1000px 级屏幕上肉眼几乎不可见 ⇒ 加大到 0.80~1.0（20% 呼吸）
# + 浮动 ±30px、周期缩短到 4s/6s，让「缓慢缩放 + 上下浮动」一眼能看出。
SCALE_LO, SCALE_HI = 0.80, 1.0
OPACITY_LO, OPACITY_HI = 1.0, 1.0
#: K_SS_MOVE 开时才用的漂浮幅度/周期
FLOAT_PX = 30.0
DRIFT_X_MS, DRIFT_X_PX = 7000.0, 60.0

#: 背景整片白：原图 logo 渐变 + 黑字在白底上最清晰，也只有白底才「其他
#: 都填充白色背景」（用户口径）。不再用深色背景 + 白卡片托盘。
BG_COLOR = QColor(255, 255, 255)


def logo_path():
    """位图绝对路径（与 widgets._load_logo 同一路径基准）。"""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "assets", LOGO_FILE)


def load_pixmap(path=None):
    """加载位图；缺文件返回 None（调用方跳过屏保而不是崩）。"""
    p = path or logo_path()
    try:
        pm = QPixmap(p)
        return None if pm.isNull() else pm
    except Exception:                                     # noqa: BLE001
        return None


def load_frames(strong_text=False):
    """加载渐变流光帧序列（24 帧预渲染 PNG）；缺失返回空列表。

    :param strong_text: True = 文字也明显流光（screensaver_frames_text），
        False = 文字深色弱流光（screensaver_frames）——对应
        K_SS_TEXT_FLOW 开关。
    [为什么预渲染而不是实时改色] 位图色带流动若每帧逐像素改色会吃满
    板端 CPU（480×483×24fps）；预渲染成 PNG 帧，运行时只 QPixmap 切帧
    （零像素运算）——「位图化」一贯口径。
    """
    frames = []
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "assets", FRAMES_TEXT_DIR if strong_text else FRAMES_DIR)
    try:
        for i in range(FRAME_COUNT):
            pm = QPixmap(os.path.join(d, "frame_%02d.png" % i))
            if pm.isNull():
                return []
            frames.append(pm)
    except Exception:                                     # noqa: BLE001
        return []
    return frames


def _setting(settings, key, default):
    """读一个 bool 开关；settings=None 或缺键 ⇒ 用缺省（兼容旧调用）。"""
    if settings is None:
        return default
    try:
        return bool(settings.get_bool(key, default))
    except Exception:                                     # noqa: BLE001
        return default


def anim_state(elapsed_ms, settings=None):
    """纯函数：由已运行毫秒算 (缩放, 透明度, 水平偏移, 垂直偏移)。可单测。

    [2026-11-26 用户「流光/呼吸/移动都是可配置选项」] 按设置开关：
      · K_SS_BREATH 关 ⇒ 不缩放（scale=1.0）；开 ⇒ 0.80~1.0 呼吸；
      · K_SS_MOVE 开 ⇒ 李萨如漂浮（±60/±30）；关 ⇒ dx=dy=0（缺省）；
      · 透明度恒 1.0（图标不暗）。
    settings=None 时按缺省（呼吸开、移动关），保证旧测试兼容。
    """
    import math
    breath_on = _setting(settings, "screenSaverBreath", True)
    move_on = _setting(settings, "screenSaverMove", False)
    breath = (1.0 - math.cos(2.0 * math.pi * (elapsed_ms % BREATH_MS)
                             / BREATH_MS)) / 2.0
    scale = (SCALE_LO + (SCALE_HI - SCALE_LO) * breath) if breath_on else 1.0
    opacity = OPACITY_LO + (OPACITY_HI - OPACITY_LO) * breath
    dx = dy = 0.0
    if move_on:
        flo_y = (1.0 - math.cos(2.0 * math.pi * (elapsed_ms % FLOAT_MS)
                                / FLOAT_MS)) / 2.0
        flo_x = (1.0 - math.cos(2.0 * math.pi * (elapsed_ms % DRIFT_X_MS)
                                / DRIFT_X_MS)) / 2.0
        dx = (flo_x - 0.5) * 2.0 * DRIFT_X_PX
        dy = (flo_y - 0.5) * 2.0 * FLOAT_PX
    return scale, opacity, dx, dy


class ScreenSaverOverlay(QWidget):
    """全屏覆盖层：中央位图 + 呼吸/浮动动画，任意输入退出。"""

    def __init__(self, host, pixmap=None, parent=None, settings=None):
        QWidget.__init__(self, parent or host)
        self._host = host
        self._settings = settings
        self._pm = pixmap if pixmap is not None else load_pixmap()
        # 两组流光帧缓存（弱/强文字），按 K_SS_TEXT_FLOW 选；按需加载，
        # 切选项时只换引用不重新解码整组（板端内存敏感）。
        self._frames_cache = {}
        self._frame_idx = -1
        self._t0 = 0.0
        self._active = False
        self._on_exit = None
        # [2026-11-26 远程实锅] 弹出瞬间忽略输入 800ms：触发点击的
        # MouseButtonRelease、向日葵远控的后续鼠标事件会立刻落到新铺满的
        # 覆盖层上，不设保护期屏保「跳出一下就没」。真正的用户退出动作
        # 发生在保护期之后，不受影响。
        self._guard_until = 0.0
        # 黑底覆盖（qlineargradient 项目里没有现成封装，纯色足够）。
        self.setAutoFillBackground(True)
        pal = self.palette()
        pal.setColor(self.backgroundRole(), BG_COLOR)
        self.setPalette(pal)
        self.setAttribute(Qt.WA_AcceptTouchEvents, True)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setWindowFlags(self.windowFlags() | Qt.FramelessWindowHint)

        # [手动定位 2026-11-26] 不用 QVBoxLayout 居中：动画的 dx/dy 平移
        # 若靠 layout margin 实现会压缩 label 可用宽、每帧重新布局⇒抖动。
        # 直接按「画布中心 + 偏移」setGeometry，平移与缩放都是纯几何，
        # 不触发布局重算，动画平滑。
        self._label = QLabel(self)
        self._label.setObjectName("HsfScreensaverLogo")
        self._label.setAlignment(Qt.AlignHCenter | Qt.AlignVCenter)
        self._label.setStyleSheet("QLabel { background: transparent; }")
        self._opacity = QGraphicsOpacityEffect(self._label)
        self._label.setGraphicsEffect(self._opacity)

        self._timer = QTimer(self)
        self._timer.setInterval(FRAME_MS)
        self._timer.timeout.connect(self._tick)
        # 宿主 resize 时跟着铺满（照 _FullscreenPlayer 的事件过滤套路）
        if host is not None:
            host.installEventFilter(self)

    # ---- 公共 API ----
    def start(self, on_exit=None):
        """启动屏保：铺满宿主、居中位图、开动画。返回是否真的启动。"""
        if self._pm is None:
            return False
        self._on_exit = on_exit
        self._guard_until = time.monotonic() + 0.8
        self._t0 = time.monotonic()
        self._active = True
        self._resize_to_host()
        self.show()
        self.raise_()
        self.setFocus()                     # 收键盘事件（退出用）
        self._tick()                        # 立刻画第一帧（不等 33ms）
        self._timer.start()
        return True

    def stop(self):
        """停止并隐藏（幂等）。"""
        self._timer.stop()
        self._active = False
        self.hide()

    def is_active(self):
        return self._active

    def refresh_settings(self):
        """设置页改完动画开关后调：下一帧按新设置渲染（帧缓存按需重建）。"""
        # 帧缓存保留两组，不丢弃——切回来不用重新解码。
        self.render_frame((time.monotonic() - self._t0) * 1000.0
                          if self._active else 0.0)

    def render_frame(self, elapsed_ms):
        """按给定相位画一帧（public 便于单测/截图探针直驱）。"""
        if self._pm is None:
            return
        scale, opacity, dx, dy = anim_state(elapsed_ms, self._settings)
        avail = self.rect().adjusted(16, 16, -16, -16)
        if avail.width() <= 0 or avail.height() <= 0:
            return
        # 选帧：K_SS_FLOW 关 ⇒ 静态位图；开 ⇒ 按 K_SS_TEXT_FLOW 选弱/强文字
        # 帧序列（24 帧按时间循环，色带横向流动）。每帧读设置 ⇒ 改完立即生效。
        src = self._pm
        flow_on = _setting(self._settings, "screenSaverFlow", True)
        if flow_on:
            strong = _setting(self._settings, "screenSaverTextFlow", True)
            frames = self._frames_cache.get(strong)
            if frames is None:
                frames = load_frames(strong)
                self._frames_cache[strong] = frames
            if frames:
                idx = int((elapsed_ms % FLOW_CYCLE_MS)
                          / FLOW_CYCLE_MS * len(frames)) % len(frames)
                src = frames[idx]
                self._frame_idx = idx
        # 基准尺寸：按可用区的 88% 放位图（放大居中，不顶边）
        base_w = max(1, int(avail.width() * 0.88))
        base_h = max(1, int(avail.height() * 0.88))
        ratio = min(base_w / float(src.width()),
                    base_h / float(src.height()))
        pw = max(1, int(src.width() * ratio * scale))
        ph = max(1, int(src.height() * ratio * scale))
        pm = src.scaled(pw, ph, Qt.KeepAspectRatio,
                        Qt.SmoothTransformation)
        self._label.setPixmap(pm)
        self._label.resize(pm.size())
        self._opacity.setOpacity(opacity)
        # 画布中心 + 李萨如轨迹偏移（纯 setGeometry，不触发布局）
        cx = self.width() / 2.0 + dx
        cy = self.height() / 2.0 + dy
        self._label.move(int(cx - pw / 2.0), int(cy - ph / 2.0))

    # ---- 事件：任意输入退出 ----
    def _exit(self):
        cb, self._on_exit = self._on_exit, None
        self.stop()
        if cb is not None:
            cb()

    def _guarded(self):
        """弹出后 800ms 内忽略退出事件（触发点击的 release/远控余波）。"""
        return time.monotonic() < self._guard_until

    def mousePressEvent(self, event):
        if not self._guarded():
            self._exit()
        event.accept()

    def keyPressEvent(self, event):
        if not self._guarded():
            self._exit()
        event.accept()

    def touchEvent(self, event):
        if (event.type() == QEvent.TouchBegin and not self._guarded()):
            self._exit()
            event.accept()
            return
        # 其余触摸让 Qt 合成鼠标事件走 mousePressEvent（同一保护期口径）
        QWidget.touchEvent(self, event)

    def eventFilter(self, obj, event):
        if obj is self._host and event.type() == QEvent.Resize:
            self._resize_to_host()
        return QWidget.eventFilter(self, obj, event)

    # ---- 内部 ----
    def _resize_to_host(self):
        if self._host is not None:
            self.setGeometry(self._host.rect())
        else:
            self.setGeometry(QRect(0, 0, self.width(), self.height()))

    def _tick(self):
        if not self._active:
            return
        elapsed = (time.monotonic() - self._t0) * 1000.0
        self.render_frame(elapsed)


class ScreenSaverController(QObject):
    """空闲监测 + 屏保生命周期（装到 QApplication 上，随进程存活）。

    与 hsf_native 的硬件熄屏**独立**：这里自己 1Hz 计时 + 应用级事件过滤器
    重置空闲（任何鼠标/键盘/触摸/滚轮都算活动），到期调 overlay.start()。
    屏保启用时本地设置会把下发平台的熄屏超时置 0 ⇒ 两者不会同时生效。
    """

    def __init__(self, app, settings, host_provider):
        QObject.__init__(self)
        self._app = app
        self._settings = settings
        self._host_provider = host_provider
        self._overlay = None
        self._last_activity = time.monotonic()
        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._check)
        self._filter = _ActivityFilter(self)
        app.installEventFilter(self._filter)

    def start(self):
        self._timer.start()

    def stop(self):
        self._timer.stop()
        self._app.removeEventFilter(self._filter)
        if self._overlay is not None:
            self._overlay.stop()
            self._overlay.deleteLater()
            self._overlay = None

    def note_activity(self):
        """外部可调用（事件过滤器也会调）：有任何输入就重置空闲。"""
        self._last_activity = time.monotonic()

    def show_now(self):
        """立即弹出屏保（本地设置「立即熄屏」按钮在屏保模式下调用）。

        与超时触发走同一条建层路径，只是不看空闲时长——用户主动点按钮
        就要立刻看到效果。返回是否真的弹起来了（无位图/无宿主返回 False）。
        """
        return self._show_overlay()

    def idle_ms(self):
        return int((time.monotonic() - self._last_activity) * 1000)

    def enabled_ms(self):
        """当前生效的屏保超时（毫秒；0=不启用）。

        与硬件熄屏共用「自动熄屏」时长（K_POWER_SCREEN_OFF_MS），只看
        「到期动作」是不是屏保——不再另设一套开关/时长（2026-11-26 用户
        「自动休眠那增加选择可以走屏保」）。
        """
        try:
            action = self._settings.get_str(
                self._settings.K_POWER_BLANK_ACTION,
                self._settings.BLANK_ACTION_OFF)
            if action != self._settings.BLANK_ACTION_SCREENSAVER:
                return 0
            ms = self._settings.get_int(self._settings.K_POWER_SCREEN_OFF_MS, 0)
            return max(0, int(ms))
        except Exception:                                 # noqa: BLE001
            return 0

    def _show_overlay(self):
        """建/复用覆盖层并弹出（超时触发与「立即休眠」共用）。"""
        if self._overlay is not None and self._overlay.is_active():
            return False                      # 已在屏保，别重复弹
        host = self._host_provider() if self._host_provider else None
        if host is None:
            return False
        if self._overlay is None or self._overlay._host is not host:
            self._overlay = ScreenSaverOverlay(
                host, parent=host, settings=self._settings)
        return self._overlay.start(on_exit=self.note_activity)

    def _check(self):
        limit = self.enabled_ms()
        if limit <= 0:
            # [2026-11-26 实锅] 「从不」只是不**自动**触发，不该关掉已经
            # 手动弹出的屏保——原来这里 stop() 导致点「立即休眠」后 1 秒
            # 就消失。手动弹出的屏保只能由用户动手退出。
            return
        if self._overlay is not None and self._overlay.is_active():
            return
        if self.idle_ms() < limit:
            return
        self._show_overlay()


class _ActivityFilter(QObject):
    """应用级过滤器：任意输入 → controller.note_activity()。"""

    _EVENTS = (QEvent.MouseButtonPress, QEvent.MouseButtonRelease,
               QEvent.MouseMove, QEvent.Wheel, QEvent.KeyPress,
               QEvent.KeyRelease, QEvent.TouchBegin, QEvent.TouchUpdate,
               QEvent.TouchEnd)

    def __init__(self, controller):
        QObject.__init__(self)
        self._ctrl = controller

    def eventFilter(self, obj, event):
        if event.type() in self._EVENTS:
            self._ctrl.note_activity()
        return False
