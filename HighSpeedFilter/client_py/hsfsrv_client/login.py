# -*- coding: utf-8 -*-
"""登录窗：服务器地址 / 端口 / 用户名 / 口令 → 建链登录。

[对齐 C 端 HsfLogin] 同样的四项输入 + 记住上次输入 + 失败原因就地显示。
登录是**阻塞**操作（要等握手 + 10000 轮 HMAC 派生），故在等待期间：
  * 按钮禁用、显示「连接中…」
  * 用 QApplication.processEvents 保持界面可重绘（否则窗口会「假死」白屏）

[为什么不放后台线程] 登录只发生一次，耗时 100~800ms。为它起线程 +
引入登录结果的跨线程同步，复杂度远大于收益。阻塞 + 转圈是合理取舍。
"""

from PyQt5.QtCore import Qt, QSettings
from PyQt5.QtWidgets import (QApplication, QCheckBox, QDialog, QFormLayout,
                             QHBoxLayout, QLabel, QLineEdit, QPushButton,
                             QVBoxLayout, QWidget)

from . import proto, theme
from .xlink import DEFAULT_PORT


class LoginDialog(QDialog):
    """登录对话框（覆盖在已显示的主窗上方）。

    accept 后 `self.bridge` 已连好（或失败原因在 self.error）。

    [与 C 端对齐的形态] HsfUi.c 里登录窗是**以主窗为父**创建的：
      * 继承主窗当前的皮肤/字体（用户选的主题对登录窗同样生效）
      * 按父窗口客户区**居中**
      * ApplicationModal 挡住主窗输入
    表现为「登录卡片盖在真实主界面上」。
    """

    def __init__(self, bridge, parent=None):
        QDialog.__init__(self, parent)
        self.bridge = bridge
        self.error = ""
        self.setWindowTitle("高速过滤器 - 登录")
        self.setMinimumWidth(420)
        # 去掉标题栏的问号按钮（它会在登录窗上多一个无意义的"帮助"）。
        self.setWindowFlags(self.windowFlags()
                            & ~Qt.WindowContextHelpButtonHint)
        # 有父窗时按父客户区居中：QDialog 默认按屏幕居中，父窗是全屏/大窗
        # 时会偏到左上角，与 C 端的"居中压在主界面"不一致。
        if parent is not None:
            self.setModal(True)
        self.setStyleSheet("""
QDialog { background-color: %(page_bg)s; }
QLabel#HsfLoginTitle {
    color: %(brand)s; font-size: 20px; font-weight: bold;
    padding: 6px 0 2px 0;
}
QLabel#HsfLoginSub { color: #5A6572; font-size: 12px; padding-bottom: 10px; }
QLabel#HsfLoginErr { color: %(fault)s; font-size: 12px; padding-top: 6px; }
""" % {"page_bg": theme.COLOR_PAGE_BG, "brand": theme.COLOR_BRAND,
       "fault": theme.CLR_FAULT})

        outer = QVBoxLayout(self)
        outer.setContentsMargins(28, 22, 28, 20)
        outer.setSpacing(8)

        title = QLabel("高速过滤器")
        title.setObjectName("HsfLoginTitle")
        title.setAlignment(Qt.AlignCenter)
        outer.addWidget(title)

        sub = QLabel("服务端连接")
        sub.setObjectName("HsfLoginSub")
        sub.setAlignment(Qt.AlignCenter)
        outer.addWidget(sub)

        form = QFormLayout()
        form.setSpacing(10)
        form.setLabelAlignment(Qt.AlignRight | Qt.AlignVCenter)

        self.host_edit = QLineEdit()
        self.host_edit.setPlaceholderText("192.168.0.10")
        form.addRow("服务器地址", self.host_edit)

        self.port_edit = QLineEdit()
        self.port_edit.setPlaceholderText(str(DEFAULT_PORT))
        form.addRow("端口", self.port_edit)

        self.user_edit = QLineEdit()
        self.user_edit.setPlaceholderText("root")
        form.addRow("用户名", self.user_edit)

        self.pass_edit = QLineEdit()
        self.pass_edit.setEchoMode(QLineEdit.Password)
        self.pass_edit.setPlaceholderText("口令")
        form.addRow("口令", self.pass_edit)

        outer.addLayout(form)

        self.remember = QCheckBox("记住服务器地址与用户名")
        outer.addWidget(self.remember)

        self.error_label = QLabel("")
        self.error_label.setObjectName("HsfLoginErr")
        self.error_label.setWordWrap(True)
        outer.addWidget(self.error_label)

        btns = QHBoxLayout()
        btns.addStretch(1)
        self.offline_btn = QPushButton("离线浏览")
        self.offline_btn.setToolTip("不连接服务端，只看界面（数据为空）")
        self.offline_btn.clicked.connect(self._on_offline)
        btns.addWidget(self.offline_btn)

        self.login_btn = QPushButton("登录")
        self.login_btn.setDefault(True)
        self.login_btn.setMinimumWidth(96)
        self.login_btn.clicked.connect(self._on_login)
        btns.addWidget(self.login_btn)
        outer.addLayout(btns)

        self._load_settings()
        self.pass_edit.returnPressed.connect(self._on_login)
        self.user_edit.returnPressed.connect(self._on_login)

    # ---- 居中（按父窗口客户区，而非屏幕）----
    def showEvent(self, event):
        QDialog.showEvent(self, event)
        parent = self.parentWidget()
        if parent is None:
            return
        # adjustSize 先让布局算出真实尺寸，再按父窗口几何居中。
        self.adjustSize()
        geo = parent.geometry()
        x = geo.x() + (geo.width() - self.width()) // 2
        y = geo.y() + (geo.height() - self.height()) // 2
        self.move(max(0, x), max(0, y))

    # ---- 记住上次输入 ----
    def _load_settings(self):
        settings = QSettings("XinYueC", "HsfClientPy")
        self.host_edit.setText(settings.value("host", "192.168.0.10", str))
        self.port_edit.setText(settings.value("port", str(DEFAULT_PORT), str))
        self.user_edit.setText(settings.value("user", "root", str))
        remember = settings.value("remember", True, bool)
        self.remember.setChecked(remember)
        # 记住用户名时给个口令提示（口令本身不落盘）。
        if self.user_edit.text() == "root":
            self.pass_edit.setPlaceholderText("口令（缺省 root）")

    def _save_settings(self):
        if not self.remember.isChecked():
            return
        settings = QSettings("XinYueC", "HsfClientPy")
        settings.setValue("host", self.host_edit.text().strip())
        settings.setValue("port", self.port_edit.text().strip())
        settings.setValue("user", self.user_edit.text().strip())
        settings.setValue("remember", True)

    # ---- 动作 ----
    def _read_inputs(self):
        host = self.host_edit.text().strip() or "192.168.0.10"
        try:
            port = int(self.port_edit.text().strip() or DEFAULT_PORT)
        except ValueError:
            port = DEFAULT_PORT
        user = self.user_edit.text().strip()
        password = self.pass_edit.text()
        return host, port, user, password

    def _set_busy(self, busy, text=""):
        self.login_btn.setEnabled(not busy)
        self.offline_btn.setEnabled(not busy)
        self.login_btn.setText(text or ("登录" if not busy else "连接中…"))
        QApplication.processEvents()

    def _on_login(self):
        host, port, user, password = self._read_inputs()
        if not user:
            self.error_label.setText("请填写用户名")
            return
        self.error_label.setText("")
        self._set_busy(True)
        try:
            ok, err = self.bridge.connect_to(host, port, user, password)
        finally:
            self._set_busy(False)
        if ok:
            self._save_settings()
            self.accept()
        else:
            self.error = err
            self.error_label.setText(
                "登录失败：%s" % err if err else "登录失败")
            # 口令错了就清空口令框，让用户直接重输。
            if "口令" in err or "password" in err.lower():
                self.pass_edit.clear()
                self.pass_edit.setFocus()

    def _on_offline(self):
        """离线浏览：不建链，直接进主界面（数据全空，用于看界面）。"""
        self.error = ""
        self.accept()
