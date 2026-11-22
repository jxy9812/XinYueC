# -*- coding: utf-8 -*-
"""相机页（页号 17）：显示服务端采集的网络相机画面。

[数据来源：服务端，**客户端不直连相机**] 用户口径（2026-11-19）：客户端
也要从服务端取数据 —— 外网访问场景里客户端可能在公网、相机在内网，只有
服务端两边都够得到。

[两种画面形态（2026-11-19 架构转向）] `mode="es"`：服务端**直传编码流**
（H.264/H.265 NAL），本端 `EsDecoder`（PyAV，GPU 硬解优先/软解兜底）解出
RGB 裸帧 ⇒ `QImage(data,w,h,Format_RGB888)` 贴图；`mode="jpeg"`：旧路，
服务端编好的 JPEG 字节 ⇒ `QImage.fromData`。页面按 model 的 mode 选路径。

[为什么要自己写图片控件] 本项目**没有现成的图片控件**：widgets.py 只 import
QColor/QFont/QPainter/QPen，全应用的"图形"都是矢量自绘（趋势图也是
QPainter 画线）⇒ 这里新写一个 QLabel + QImage。

[QImage vs QPixmap] theme.py 的教训：**无 QApplication 进程里 QPixmap
构造会失败** ⇒ 解码用 QImage，只有真正贴到界面时才转 QPixmap。

[刷新节拍] 页面基类每 200ms 调一次 refresh()；这里只在**帧序号变了**或
**控件尺寸变了**时才重贴图（否则每 200ms 重缩放一张 704×704 的图，白烧 CPU）。

[全屏播放（2026-11-19 用户口径「类似网站视频的播放界面」）] 画面区右上角
「全屏」按钮 / **双击画面**进入独立全屏覆盖窗（黑底 + 画面铺满 + 右上角
退出钮），Esc / 双击 / 退出钮回普通窗。全屏窗自己 100ms 贴图（不吃页面
200ms 节拍），与页面共享同一 model ⇒ 不多拉一份流。
"""

import time

from PyQt5.QtCore import QEvent, QPoint, Qt, QTimer
from PyQt5.QtGui import QImage, QKeyEvent, QPixmap
from PyQt5.QtWidgets import (QHBoxLayout, QLabel, QPushButton, QSizePolicy,
                             QVBoxLayout, QWidget)

from . import page_base, proto, theme


class CameraPage(page_base.Page):
    """相机画面页（设备组下）。"""

    #: 只订相机主题 ⇒ 切到本页才订阅 ⇒ 服务端按订阅门控推帧
    #: （**没人订阅时一帧都不推**，用户 2026-11-19 口径）。
    TOPICS = (proto.TOPIC_CAMERA_FRAME,)

    def __init__(self, ctx):
        page_base.Page.__init__(self, ctx)
        self._last_key = None
        self._qimage = None
        self._qimage_seq = -1
        self._fs = None              # 全屏覆盖窗（进全屏才建）

        self.add_header("相机", "服务端采集 · 网络相机实时画面")

        # ---- 画面区 ----
        # [双击进全屏] QLabel 不带鼠标事件钩子 ⇒ 用事件过滤器装在本页上，
        # 监听画面区的双击（不动 widgets 公共件）。
        self._img = _VideoLabel()
        self._img.setPage(self)
        self._img.setAlignment(Qt.AlignCenter)
        self._img.setText("等待画面…")
        self._img.setMinimumHeight(220)
        # [尺寸策略] 图片按可用空间缩放显示（不拉伸变形）⇒ Expanding + 允许
        # 控件拿到剩余空间；高度给个下限，窗口矮时也不至于塌成一条。
        self._img.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self._img.setStyleSheet(
            "QLabel { background: #FAFBFC; border: 1px solid #D5DBE2;"
            " border-radius: 5px; color: #757575; font-size: 15px; }")

        self._status = QLabel("未收到画面")
        self._status.setStyleSheet(theme.caption_label_qss())

        self._meta = QLabel("")
        self._meta.setStyleSheet(theme.caption_label_qss())

        wrap = QWidget()
        wrap.setStyleSheet("background: transparent;")
        lay = QVBoxLayout(wrap)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)
        lay.addWidget(self._img, 1)
        info = QHBoxLayout()
        info.setSpacing(12)
        info.addWidget(self._status)
        info.addWidget(self._meta)
        info.addStretch(1)
        # [全屏入口] 类网站视频界面：画面右上角一把「全屏」+ 双击画面同效。
        self._fs_btn = QPushButton("全屏")
        self._fs_btn.setCursor(Qt.PointingHandCursor)
        self._fs_btn.setToolTip("全屏播放（双击画面同效，Esc 退出）")
        self._fs_btn.clicked.connect(self.enter_fullscreen)
        info.addWidget(self._fs_btn)
        lay.addLayout(info)
        self.add_widget(wrap, 1)

        # ---- 快速贴拍（ES 直传的显示节拍）----
        #
        # [为什么 50ms——2026-11-19「画面还是卡顿」分层实测]
        # 主循环的 refresh 是 **200ms 定时器**（全页共用节拍）⇒ 直传 20fps
        # 下贴图率被压到 **4.5fps**（实测 L2 解码输出 19.6fps vs L3 贴图
        # 91 次/20s，L2/L3=4.3 ⇒ 3/4 的帧被跳过），每次还跳到最新帧
        # ⇒ 必卡。50ms 贴拍把显示率提到 ~20fps；`refresh()` 的 key 早退
        # 保证"帧没变就不重贴"⇒ JPEG 模式（2fps 源）多跑的只是 key 比较
        # （微秒级），无害。
        # [为什么不走推送事件驱动] 2fps JPEG 时代实测过推送驱动贴图：
        # 网络突发时多帧挤进来逐帧重绘、随后一个 2s 空档（σ 516 vs 198，
        # 见 main_window 的否决注）。ES 模式帧源是 20fps 密集流，同样
        # 的突发会让 20fps 全速贴图打满 UI 线程 ⇒ 定时器节拍 + key 早退
        # 仍是稳的选择；50ms 上限 ⇒ 显示 ≥12fps。
        self._fast_timer = QTimer(self)
        self._fast_timer.setInterval(50)
        self._fast_timer.timeout.connect(self.refresh)
        self._fast_timer.start()

    # ---- 全屏播放 -------------------------------------------------------

    def enter_fullscreen(self):
        """进入**主窗内覆盖层**全屏（已开则忽略）。

        [为什么不再是独立顶层窗 2026-11-20 板端触摸实锅] 板端是触摸屏
        （ILITEK）：独立 `Qt.Window` 全屏窗在**触摸序列中途**被关闭/隐藏时，
        Qt 的触摸状态机停在"该窗口还持有隐式 grab"，主窗收不到后续触摸
        ⇒ 用户看到「退出全屏后点什么都没反应」（鼠标事件路径不会这样，
        所以本机离屏测不出来）。
        ⇒ 覆盖层与主窗**同一个 QWindow**：进出全屏只是 show/hide 一个
        子控件，触摸序列永远不会跨窗口，从根本上消除该问题。
        """
        if self._fs is not None:
            return
        host = self.window()
        self._fs = _FullscreenPlayer(self, host)
        self._fs.enter()

    def fullscreen_closed(self):
        """全屏窗关闭（Esc/退出钮/再双击）⇒ 清引用。"""
        self._fs = None

    # ---- 数据刷新 -------------------------------------------------------

    def refresh(self):
        cam = self.ctx.model.camera
        size = self._img.size()
        key = (int(cam.seq), int(size.width()), int(size.height()))
        if key == self._last_key:
            return
        self._last_key = key
        if not cam.frame:
            self._img.setPixmap(QPixmap())
            self._img.setText("等待画面（相机未启用 / 离线 / 正在连接）")
            self._status.setText("未收到画面")
            self._meta.setText("")
            self._qimage = None
            return
        # [2026-11-19 ES 直传] mode=="es" ⇒ frame 是 PyAV 解出的 **RGB 裸
        # 帧**（零拷贝走 Format_RGB888），不走 JPEG 解码；mode=="jpeg" 是
        # 旧路（服务端编好的 JPEG 字节）。
        if getattr(cam, "mode", "jpeg") == "es":
            img = QImage(cam.frame, cam.width, cam.height,
                         cam.width * 3, QImage.Format_RGB888)
        else:
            img = QImage.fromData(cam.frame, "JPG")
        if img.isNull():
            self._img.setPixmap(QPixmap())
            self._img.setText("画面解码失败（帧不完整？）")
            self._status.setText("画面异常")
            return
        self._qimage = img
        self._qimage_seq = int(cam.seq)
        scaled = self._qimage.scaled(size.width(), size.height(),
                                    Qt.KeepAspectRatio, Qt.SmoothTransformation)
        self._img.setText("")
        self._img.setPixmap(QPixmap.fromImage(scaled))
        self._status.setText("画面接收中")
        self._meta.setText(self._meta_text(cam))

    def _meta_text(self, cam):
        parts = []
        if cam.width and cam.height:
            parts.append("%d×%d" % (cam.width, cam.height))
        parts.append("帧号 %d" % int(cam.seq))
        # [KB 只在 JPEG 模式显示] ES 模式下 frame 是解码后的 RGB 裸帧
        # （704² ≈ 1.4MB），那是**本机内存量**不是传输量 ⇒ 显示出来会
        # 让人以为占了这么大带宽（撒谎）。
        if cam.frame and getattr(cam, "mode", "jpeg") == "jpeg":
            parts.append("%d KB" % (len(cam.frame) // 1024))
        if cam.ts_ms:
            parts.append("更新 " + time.strftime("%H:%M:%S",
                                                 time.localtime(cam.ts_ms / 1000.0)))
        if cam.dropped:
            parts.append("丢帧 %d" % int(cam.dropped))
        return " · ".join(parts)


class _VideoLabel(QLabel):
    """画面 QLabel + 双击事件（双击 ⇒ 进全屏，对齐网站视频的习惯操作）。

    [为什么不子类化复用] 只有相机页用 ⇒ 本地小子类即可；双击事件转发给
    页面的 enter_fullscreen（页面持有逻辑）。
    """

    _page = None

    def setPage(self, page):
        self._page = page

    def mouseDoubleClickEvent(self, event):
        # [不在事件处理里同步切窗口 2026-11-20 板端实锅] 在 mouseDoubleClick
        # 里直接 showFullScreen 新窗 ⇒ 本次点击的 **mouseRelease 被新窗吃掉**
        # ⇒ 主窗（或新窗）鼠标状态机停在"按下"⇒ 之后点任何控件都被当拖拽、
        # 毫无响应。⇒ 延迟到事件返回后（singleShot(0)）再进全屏。
        event.accept()
        if self._page is not None:
            page = self._page
            QTimer.singleShot(0, page.enter_fullscreen)


class _FullscreenPlayer(QWidget):
    """独立全屏覆盖窗：黑底 + 画面铺满 + 右上角退出钮。

    [为什么是独立窗而不是改主窗布局] 相机页嵌在主窗的分割器/滚动壳里，
    全屏要把导航/标题栏/状态栏全藏再复原，动的是主窗骨架（回归面大）；
    独立窗 `showFullScreen()` 一行进、`close()` 一行出，与主窗零耦合，
    板端全屏客户端上同样直接覆盖。
    [刷新] 自己 100ms 一拍从 model 取帧贴图（不吃页面 200ms 节拍，全屏
    下观感更顺）；与页面共享同一 model ⇒ **不多拉一份流**（订阅门控仍按
    相机主题算一份）。
    [退出] Esc / 双击 / 右上角「退出全屏」钮 / 页面销毁。
    """

    def __init__(self, page, host):
        # [同 QWindow 覆盖层] parent = 主窗（普通子控件，不是 Qt.Window）
        QWidget.__init__(self, host)
        self._page = page
        self._host = host
        self.setObjectName("HsfCameraFullscreen")
        self.setStyleSheet("QWidget#HsfCameraFullscreen"
                           " { background: #000; }")
        self.hide()
        # 主窗尺寸变化时跟随（覆盖层始终铺满）
        host.installEventFilter(self)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self._img = QLabel()
        self._img.setAlignment(Qt.AlignCenter)
        self._img.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self._img.setStyleSheet(
            "QLabel { background: #000; color: #9aa0a6; font-size: 18px; }")
        lay.addWidget(self._img, 1)
        bar = QHBoxLayout()
        bar.setContentsMargins(12, 8, 12, 8)
        bar.setSpacing(12)
        self._meta = QLabel("")
        self._meta.setStyleSheet("color: #c8ccd0; font-size: 14px;")
        bar.addWidget(self._meta)
        bar.addStretch(1)
        quit_btn = QPushButton("退出全屏")
        quit_btn.setCursor(Qt.PointingHandCursor)
        quit_btn.setToolTip("Esc / 双击画面同效")
        quit_btn.clicked.connect(self.exit_fs)
        bar.addWidget(quit_btn)
        lay.addLayout(bar)
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(100)
        self._last_key = None

    def enter(self):
        """铺满主窗并置顶（不新建窗口、不夺 QWindow）。"""
        self.setGeometry(self._host.rect())
        self.show()
        self.raise_()
        self.setFocus(Qt.OtherFocusReason)

    def eventFilter(self, obj, ev):
        if obj is self._host and ev.type() == QEvent.Resize and self.isVisible():
            self.setGeometry(self._host.rect())
        return False

    def exit_fs(self):
        """退出全屏：隐藏覆盖层 + 释放输入 + 交还主窗焦点。

        [顺序] 先把自己藏起来（触摸序列还在本 QWindow 内，天然安全），
        再释放可能的 grab，最后让主窗重新拿焦点/激活。
        """
        self._timer.stop()
        self.hide()
        try:
            self._host.removeEventFilter(self)
        except Exception:                            # noqa: BLE001
            pass
        self.releaseMouse()
        self.releaseKeyboard()
        page = self._page
        self._page = None
        host = self._host
        # [清"按键卡住"的兜底] 触摸驱动合成的鼠标状态若停在按下态，
        # 后续点击会被当拖拽 ⇒ 补一发 release 给主窗（幂等，无害）。
        try:
            from PyQt5.QtWidgets import QApplication
            QApplication.postEvent(host, QMouseEvent(
                QEvent.MouseButtonRelease, QPoint(0, 0),
                Qt.LeftButton, Qt.NoButton, Qt.NoModifier))
        except Exception:                            # noqa: BLE001
            pass
        try:
            host.raise_()
            host.activateWindow()
        except Exception:                            # noqa: BLE001
            pass
        if page is not None:
            page.fullscreen_closed()

    # 覆盖 Esc（Qt 默认 Esc 只在对话框里生效）
    def keyPressEvent(self, event):
        if isinstance(event, QKeyEvent) and event.key() == Qt.Key_Escape:
            event.accept()
            # 退出统一走 exit_fs（隐藏覆盖层，不销毁窗口、不跨 QWindow）
            QTimer.singleShot(0, self.exit_fs)
            return
        QWidget.keyPressEvent(self, event)

    def mouseDoubleClickEvent(self, event):
        # [触摸实锅 2026-11-20] 双击退出也**延迟一拍**再隐藏（事件返回后），
        # 且只 hide 不 close（覆盖层是本窗子控件，destroy 没必要）。
        event.accept()
        QTimer.singleShot(0, self.exit_fs)

    def _tick(self):
        cam = self._page.ctx.model.camera
        size = self._img.size()
        key = (int(cam.seq), int(size.width()), int(size.height()))
        if key == self._last_key:
            return
        self._last_key = key
        if not cam.frame:
            self._img.setPixmap(QPixmap())
            self._img.setText("等待画面…")
            self._meta.setText("")
            return
        if getattr(cam, "mode", "jpeg") == "es":
            img = QImage(cam.frame, cam.width, cam.height,
                         cam.width * 3, QImage.Format_RGB888)
        else:
            img = QImage.fromData(cam.frame, "JPG")
        if img.isNull():
            self._img.setPixmap(QPixmap())
            self._img.setText("画面解码失败（帧不完整？）")
            self._meta.setText("")
            return
        scaled = img.scaled(size.width(), size.height(),
                            Qt.KeepAspectRatio, Qt.SmoothTransformation)
        self._img.setText("")
        self._img.setPixmap(QPixmap.fromImage(scaled))
        self._meta.setText(self._page._meta_text(cam))
