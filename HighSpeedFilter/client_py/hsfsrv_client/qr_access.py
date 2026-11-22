"""网页访问地址推导 + 标题栏二维码弹卡（2026-11-18 用户定案）。

[为什么单开一个模块] 「网页访问地址」有两处消费方：
  ① 本地设置页第 16 页（Web 设置）的二维码卡（pages_web.py）；
  ② **标题栏的二维码徽章**（widgets.py 的 QrBadge → 主窗弹卡）。
地址口径（局域网 IP 探测、网页端口、远程域名/端口）必须有**单一事实
源**，否则两处显示的地址可能不一致（改了一处忘另一处）。故把
lan_url / remote_url 抽到这里，页面与弹卡都调它。

[为什么 lan_ip 延迟 import] lan_ip 的实现还在 pages_web.py（连同
QrCanvas 一起搬动作太大；本轮只加标题栏入口）。qr_access 被
pages_web import（页面把地址方法委托过来）⇒ 若 qr_access 在**模块
顶层** import pages_web 就成环 ⇒ 在函数体内延迟 import（运行时两个
模块都已加载，无环问题）。
"""
import functools

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (QDialog, QHBoxLayout, QLabel, QPushButton,
                             QVBoxLayout, QWidget)

from . import settings, theme

__all__ = ["lan_url", "remote_url", "QrPopupCard"]


def lan_ip():
    """本机局域网 IPv4（实现与口径在 pages_web.lan_ip，延迟 import 防环）。"""
    from .pages_web import lan_ip as _lan_ip
    return _lan_ip()


def lan_url():
    """局域网访问地址（当前网页端口）。"""
    return "http://%s:%d/" % (lan_ip(), int(settings.web_port()))


def remote_url():
    """远程访问地址（域名 + 远程端口；未配域名返回空串）。

    远程端口 0 ⇒ 与网页端口相同（用户口径"留空=同本地"）。
    """
    domain = settings.web_domain()
    if not domain:
        return ""
    port = settings.web_remote_port() or settings.web_port()
    return "http://%s:%d/" % (domain, int(port))


class QrPopupCard(QDialog):
    """标题栏二维码徽章点开的**访问地址弹卡**（非模态、frameless 圆角卡）。

    [与 Web 设置页的关系] 同一份地址口径（lan_url/remote_url）与同一张
    QrCanvas 画布；但**不依赖页面实例**——标题栏入口在任意页面都能用
    （现场场景：操作员在主页就想把网页地址发出去，不必切到设置页翻）。
    切换「局域网 / 远程」两个 tab 的交互与 Web 设置页一致。
    """

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        self.setWindowTitle("网页访问地址")
        # [2026-11-19 用户「点击卡片窗口外部要也能关闭」] **Qt.Popup**：
        # 点击弹卡之外的任何地方 ⇒ 自动关闭（移动端弹层的标准交互）；
        # FramelessWindowHint 保住圆角卡观感，WindowStaysOnTopHint 让卡
        # 不被主窗页面盖住。
        self.setWindowFlags(Qt.Popup | Qt.FramelessWindowHint
                            | Qt.WindowStaysOnTopHint)
        self.setObjectName("HsfQrCard")
        # [2026-11-19 用户「这三个按钮没接入美化」] 弹卡是**独立顶层
        # 窗口**，不在主窗 stack 子孙里 ⇒ 拿不到挂在 stack 上的大 QSS
        # ⇒ 按钮全是原生样式。⇒ 自己也挂一份大 QSS（apply_window 会把
        # 卡片专属的背景规则经 _hsf_extra_qss 追加在大 QSS 后面 ⇒ 换主题
        # 重下发不冲掉）。控件少 ⇒ 一次下发很便宜。
        self._hsf_extra_qss = (
            "#HsfQrCard { background: %s; border: 1px solid %s;"
            " border-radius: 10px; }"
            % (theme._tok("white"), theme._tok("border_soft")))
        theme.apply_window(self)

        root = QVBoxLayout(self)
        root.setContentsMargins(14, 10, 14, 12)
        root.setSpacing(8)

        # ---- 标题行（标题 + ×）----
        head = QHBoxLayout()
        title = QLabel("网页访问（手机扫码打开）")
        title.setStyleSheet(
            "QLabel { color: %s; font-size: 20px; font-weight: bold;"
            " background: transparent; }" % theme._tok("title_text"))
        head.addWidget(title)
        head.addStretch(1)
        close_btn = QPushButton("×")
        close_btn.setFixedSize(28, 28)
        close_btn.setCursor(Qt.PointingHandCursor)
        # [padding: 0 必须内联覆盖 2026-11-19] 大 QSS 的 QPushButton 有
        # padding 8px 18px（胶囊）⇒ 28px 小按钮内容盒被挤成负 ⇒ × 字形
        # 整个画没（用户「关闭按钮×没了」）。内联补 padding: 0。
        close_btn.setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " padding: 0px; color: %s; font-size: 22px; }"
            "QPushButton:hover { background-color: %s; border-radius: 14px; }"
            % (theme._tok("text_dim"), theme._tok("hover_bg")))
        close_btn.clicked.connect(self.close)
        head.addWidget(close_btn)
        root.addLayout(head)

        # ---- 切换行：局域网 / 远程（互斥 tab）+ 刷新 ----
        row = QHBoxLayout()
        self.lan_btn = QPushButton("局域网地址")
        self.remote_btn = QPushButton("远程地址")
        for btn in (self.lan_btn, self.remote_btn):
            btn.setCheckable(True)
            btn.setAutoExclusive(True)
            btn.setCursor(Qt.PointingHandCursor)
            row.addWidget(btn)
        self.lan_btn.setChecked(True)
        self.refresh_btn = QPushButton("刷新")
        self.refresh_btn.setToolTip("重新探测本机局域网地址并重画二维码")
        self.refresh_btn.setCursor(Qt.PointingHandCursor)
        self.refresh_btn.clicked.connect(self.refresh)
        row.addWidget(self.refresh_btn)
        row.addStretch(1)
        root.addLayout(row)
        self.lan_btn.toggled.connect(lambda _c: self.refresh())

        # ---- 地址行（可选中复制）----
        self.url_label = QLabel("")
        self.url_label.setWordWrap(True)
        self.url_label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.url_label.setStyleSheet(theme.caption_label_qss())
        root.addWidget(self.url_label)

        # ---- 二维码画布（复用 pages_web 的 QrCanvas）----
        from .pages_web import QrCanvas            # 延迟 import 防环
        self.canvas = QrCanvas()
        root.addWidget(self.canvas, 0, Qt.AlignHCenter)

        # ---- 提示行 ----
        self.hint_label = QLabel("")
        self.hint_label.setWordWrap(True)
        self.hint_label.setStyleSheet(theme.caption_label_qss())
        root.addWidget(self.hint_label)

    # ---- 显示 ----
    def popup(self, parent_widget=None):
        """弹出到父窗口右上（贴着标题栏二维码徽章的方向），已开则前置。"""
        # [换主题后重下发] 弹卡是独立顶层窗口，主题的 refresh_theme 只刷
        # 主窗 chrome ⇒ 这里每次弹出前重挂一次大 QSS（控件少 ⇒ 亚毫秒）。
        theme.apply_window(self)
        self.refresh()
        if parent_widget is not None and not self.isVisible():
            geo = parent_widget.window().geometry()
            self.adjustSize()
            x = geo.right() - self.width() - 16
            y = geo.top() + 60
            self.move(max(geo.left() + 8, x), y)
        self.show()
        self.raise_()
        self.activateWindow()

    # ---- 刷新（与 Web 设置页 _refresh_qr 同口径）----
    def refresh(self):
        if self.remote_btn.isChecked():
            url = remote_url()
            if not url:
                self.url_label.setText("远程地址：未配置域名")
                self.canvas.clear(
                    "先在「设置 → Web 设置 → 远程访问」里填写域名")
                self.hint_label.setText(
                    "远程访问需要路由器把外部端口映射到本机网页端口；"
                    "只填域名也能生成地址，端口默认与网页端口相同。")
                return
            self.url_label.setText("远程地址：%s" % url)
            self.hint_label.setText(
                "扫码后浏览器打开远程地址，用网页账号登录（缺省 "
                "admin 为只读账户，需要控制设备请用操作者/管理员账户）。")
        else:
            url = lan_url()
            self.url_label.setText("局域网地址：%s" % url)
            self.hint_label.setText(
                "手机与这台机器在同一局域网（同一路由器/WiFi）时可扫码直接"
                "打开；用网页账号登录。缺省 admin 为只读账户。")
        self.canvas.set_url(url)
        err = self.canvas.error_text()
        if err:
            # 库缺失/内容过长：原因并入提示行（画布上也会显示）。
            self.hint_label.setText(err)
