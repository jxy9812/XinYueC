# -*- coding: utf-8 -*-
"""Web 设置页：网页服务的起停/端口 + 远程访问（域名/远程端口）+ 扫码直达。

[2026-11-16 用户定案] 原「网页服务」区放在**本地设置**页（分区四）；用户
要求**拆成独立一页**并补两块能力：
  ① 远程访问配置：**域名** + **远程端口**（现场把外网映射到这台机器）；
  ② 访问地址二维码，**两个可切换**：
       · 「局域网」= ``http://<本机局域网 IP>:<网页端口>/``
       · 「远程」  = ``http://<域名>:<远程端口>/``（远程端口留空 = 同网页端口）

[为什么二维码走 native 桥] 编码在框架层（Src/XCore/XCode/XRcode，L/H 两级
纠错、含不等长 RS 分块），Python 侧不许手搓（AGENTS 铁律）。hsb_bridge
把它折成扁平 C 函数（hsb_qr_encode/size/matrix），`hsf_native.qr_encode()`
用 ctypes 取矩阵，本页只负责画。**库缺失时本页二维码区置灰并说明怎么编**
（与 WiFi/电源区同款处置：不许崩、不许留死控件）。

[为什么只"展示"远程地址] 把外网流量接进来是现场路由器端口映射/内网穿透
的事；客户端**不去改用户的网络配置**，只把"该在路由器上配成什么样"拼成
地址和二维码给人看（用户口径：扫码就能访问）。
"""

import socket

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QColor, QPainter
from PyQt5.QtWidgets import (QCheckBox, QHBoxLayout, QLabel, QLineEdit,
                            QPushButton, QSizePolicy, QSpinBox, QWidget)

from . import hsf_native, proto, theme, webserver
from .page_base import Page, fit_input_width
from .widgets import Card

#: 二维码静区（模块数）。规范要求 4 个；少于它部分扫码器（尤其手机
#: 相机在暗底/lcd 反光时）会找不到定位图形。
QR_QUIET_MODULES = 4


# ============================================================================
#                          本机局域网地址
# ============================================================================

#: 主机名解析结果缓存（[2026-11-18] 解析在 UI 线程做 ⇒ 缓存后
#: 第二次调用零等待）。
_LAN_IP_CACHE = {}

def _lan_ip_by_iface():
    """枚举网卡地址（Linux：ioctl SIOCGIFADDR，**完全不碰 DNS**）。

    [为什么要有它] 板端（RK3568）常无 DNS 解析器：走主机名解析会**阻塞
    数秒～数十秒**（等 DNS 超时）⇒ 切到「Web 设置」页时 UI 线程被冻住
    （现场：切页「卡顿一会儿才打开」）。ioctl 是纯本地调用，微秒级。
    [失败口径] 取不到就回空串，交由调用方走下一条探测（不编造地址）。
    """
    try:
        import fcntl          # POSIX only ⇒ Windows/macOS 没有
    except ImportError:
        # [2026-11-18 修] fcntl 是 POSIX-only；原来它在 try 之外 ⇒
        # 非 POSIX 平台且**无默认路由**时（拔网线/WiFi 未连）抛
        # ModuleNotFoundError，而 PyQt5 对槽内未捕获异常走 qFatal()
        # ⇒ **进程 abort**（实测 exit 127/SIGABRT）。非 POSIX 直接
        # 放弃这条路，交给调用方的下一条探测。
        return ""
    import struct
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for _idx, name in socket.if_nameindex():
            try:
                raw = fcntl.ioctl(sock.fileno(), 0x8915,
                                  struct.pack("256s", name.encode("utf-8")[:15]))
                ip = socket.inet_ntoa(raw[20:24])
            except OSError:
                continue
            if ip and not ip.startswith("127."):
                return ip
    except (OSError, AttributeError, ValueError):
        pass
    finally:
        try:
            sock.close()
        except OSError:
            pass
    return ""


def _lan_ip_by_hostname(timeout=1.0):
    """主机名解析兜底（**有界**）：后台线程 + 超时。

    [为什么要有界] 同上：无 DNS 的机器上这一步会阻塞数秒。放后台线程并
    设超时 ⇒ UI 线程最多等 `timeout` 秒，超时即放弃（回环回地址）。
    """
    import threading
    box = {}

    def _worker():
        try:
            box["ips"] = socket.gethostbyname_ex(socket.gethostname())[2]
        except OSError:
            box["ips"] = []

    th = threading.Thread(target=_worker, daemon=True)
    th.start()
    # [2026-11-18 修] 这一步跑在 **UI 线程**（页面 on_enter ⇒
    # 切页时）⇒ 超时不能太长（原来 1.0s = 切页卡 1 秒）。
    # 0.3s 足够本机/局域网的主机名解析（通常走本地 hosts，微秒级）；
    # 真超时则回环回地址（下方 lan_ip 有兜底），并**缓存**结果 ⇒
    # 第二次调用不再等。
    th.join(min(timeout, 0.3))
    ips = box.get("ips", [])
    if ips:
        _LAN_IP_CACHE["ips"] = list(ips)
    return list(ips)


def lan_ip():
    """本机在局域网里的 IPv4 地址（取不到时回 "127.0.0.1"）。

    [三条探测次序] ① UDP connect 技巧（不实际发包，内核选路即给出出口
    地址）——有默认路由时最准；② 枚举网卡地址（Linux ioctl，**不碰
    DNS**，微秒级）；③ 主机名解析兜底（**有界**：后台线程 + 超时，覆盖
    离线/无默认路由的机器，例如板端只起内网网卡、或桌面机断网）。
    都失败回环回地址并在 URL 文本里照实显示（不编造一个看起来像内网
    地址的值）。
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(0.2)
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
        if ip and not ip.startswith("0."):
            return ip
    except OSError:
        pass
    finally:
        try:
            sock.close()
        except OSError:
            pass
    # [2026-11-18 修] 整段包 try/except：**槽内未捕获异常会被 PyQt5
    # 升级成 qFatal() ⇒ 进程 abort**（本机实测 exit 127/SIGABRT）。
    # ⇒ 任何探测异常都回环回地址，绝不把异常抛进调用方。
    try:
        ip = _lan_ip_by_iface()
        if ip:
            return ip
        for ip in _lan_ip_by_hostname():
            if ip and ip != "127.0.0.1" and not ip.startswith("127."):
                return ip
    except Exception:                                  # noqa: BLE001
        pass
    return "127.0.0.1"


# ============================================================================
#                         二维码画布
# ============================================================================

class QrCanvas(QWidget):
    """把 `hsf_native.qr_encode()` 的模块矩阵画成二维码（整像素对齐）。

    [为什么不生成图片再显示] 矩阵就是一串 0/1，直接 painter 画方块最省
    （不落盘、不依赖 Pillow、缩放时按控件尺寸重算模块像素 ⇒ 任何窗口
    尺寸都清晰）。颜色用**纯黑/纯白**（不是主题色）：二维码对比度直接
    影响识别率，暗色主题下也必须白底黑块。
    """

    #: 目标边长（模块像素的下限：太小的码手机扫不出）
    MIN_PIXEL = 3
    #: 画布上限（像素）：码太大就把整页撑成一屏只有码（地址/提示被挤到
    #: 下面看不见）。360 在手机相机 20~30cm 距离下余量充足。
    MAX_SIDE = 360

    def __init__(self, parent=None):
        QWidget.__init__(self, parent)
        # [尺寸交给布局 + 封顶] 竖向不抢伸（Preferred）：卡片按内容自然
        # 高度排布，画布不会把后面的提示行挤走；上限防大窗口下码过大。
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        self.setMinimumSize(220, 220)
        self.setMaximumSize(self.MAX_SIDE, self.MAX_SIDE)
        self._size = 0          # 模块边长（含静区的总模块数）
        self._data = b""
        self._error = ""        # 非空 = 显示错误文本（库缺失/内容过长等）
        self._text = ""         # 当前码对应的 URL（诊断/断言用）

    # ---- 数据 ----
    def set_url(self, url):
        """按 URL 重新编码；失败只记错误文本（画布上显示，不抛）。"""
        self._text = str(url or "")
        self._size = 0
        self._data = b""
        self._error = ""
        if not self._text:
            self._error = "未配置访问地址"
            self.update()
            return
        try:
            size, data = hsf_native.qr_encode(self._text)
        except hsf_native.NativeUnavailable:
            self._error = ("未编译 native 桥（.zcode/build_hsf_bridge.bat）"
                           "，无法生成二维码")
        except ValueError as exc:
            self._error = str(exc)
        else:
            if size > 0 and len(data) == size * size:
                self._size = size
                self._data = data
            else:
                self._error = "二维码矩阵异常"
        self.update()

    def sizeHint(self):
        """[必须给] QWidget 缺省 sizeHint 无效（-1）⇒ Preferred 策略下布局
        只按 minimumSize 分配 ⇒ 二维码被压成 100px 既难扫又难看。
        给一个固定偏好尺寸，由 MAX_SIDE/minimumSize 夹住。"""
        from PyQt5.QtCore import QSize
        return QSize(300, 300)

    def clear(self, reason=""):
        self._text = ""
        self._size = 0
        self._data = b""
        self._error = reason or ""
        self.update()

    def url(self):
        return self._text

    def has_code(self):
        return self._size > 0 and bool(self._data)

    def error_text(self):
        return self._error

    # ---- 绘制 ----
    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#FFFFFF"))
        if not self.has_code():
            painter.setPen(QColor("#666666"))
            painter.drawText(self.rect().adjusted(8, 8, -8, -8),
                             Qt.AlignCenter | Qt.TextWordWrap,
                             self._error or "等待数据…")
            return
        total = self._size + QR_QUIET_MODULES * 2
        # 整像素：模块边长取整（至少 1px），整体居中留白。
        avail_w = max(1, self.width())
        avail_h = max(1, self.height())
        cell = min(avail_w, avail_h) // total
        if cell < 1:
            cell = 1
        side = cell * total
        off_x = (avail_w - side) // 2
        off_y = (avail_h - side) // 2
        painter.fillRect(off_x, off_y, side, side, QColor("#FFFFFF"))
        painter.setPen(Qt.NoPen)
        dark = QColor("#000000")
        for y in range(self._size):
            row = y * self._size
            for x in range(self._size):
                if self._data[row + x]:
                    painter.fillRect(
                        off_x + (x + QR_QUIET_MODULES) * cell,
                        off_y + (y + QR_QUIET_MODULES) * cell,
                        cell, cell, dark)


# ============================================================================
#                              页面
# ============================================================================

class WebSettingsPage(Page):
    """Web 设置：网页服务开关/端口 + 远程访问 + 扫码直达。"""

    #: 网页服务后台起停完成（worker 线程 emit ⇒ UI 线程刷状态行）。
    webOpDone = pyqtSignal(bool, str, bool)
    #: **跨进程探测**结果（worker 线程 emit ⇒ UI 线程刷状态行）。
    #: [为什么要有] 网页服务可能不是本客户端起的（用户手工起的、另一个
    #: 客户端实例内嵌的、上一版没退干净的）⇒ 设置页要能回答"到底有没有
    #: 在跑、是不是我认领的那个"，才能给对动作。
    #: [带序号 2026-11-17 实测] 两次探测的结果可能**乱序**到达 ⇒ 旧结果
    #: 会把新结果覆盖（"在跑"被刷成"已停止"）⇒ 第一个参数是**本次探测的
    #: 序号**，只有最新一次的结果才允许改状态。
    webProbeDone = pyqtSignal(int, dict)

    #: 页面不订阅服务端推送主题（全是本机设置 + 本机能力）。
    TOPICS = ()

    def __init__(self, ctx):
        Page.__init__(self, ctx)
        from . import settings as ui_settings
        self._settings = ui_settings
        self._build_web_section()
        self._build_remote_section()
        self._build_qr_section()
        self.webOpDone.connect(self._on_web_op_done)
        self.webProbeDone.connect(self._on_web_probe_done)
        #: 探测序号（只认最新一次的结果；见 webProbeDone 的注释）。
        self._probe_seq = 0
        #: 最近一次起服失败的原因（页内状态行叠加显示）。
        self._web_last_error = ""

    # ------------------------------------------------------------------
    #                        卡片①：网页服务
    # ------------------------------------------------------------------
    def _build_web_section(self):
        self.add_header("Web 设置", "网页服务 · 远程访问 · 扫码直达")
        # [不要分区标题 + 卡片标题两份] 页内已有卡标题，再套一层
        # section_title 是同一个词出现两次（视觉噪音）。
        card = Card("网页服务")
        self.web_check = QCheckBox("随客户端启动网页服务")
        self.web_check.setChecked(self._settings.web_enabled())
        self.web_check.setToolTip(
            "勾选=客户端启动时把网页端也带起来（本机或局域网内浏览器均可"
            "访问，浏览器用各自账号登录）")
        card.add(self.web_check)

        row = QHBoxLayout()
        row.addWidget(QLabel("网页端口"))
        self.web_port_spin = QSpinBox()
        self.web_port_spin.setRange(webserver.MIN_PORT, webserver.MAX_PORT)
        self.web_port_spin.setValue(self._settings.web_port())
        self.web_port_spin.setSuffix(" 端口")
        self.web_port_spin.setMinimumWidth(140)
        row.addWidget(self.web_port_spin)
        row.addStretch(1)
        card.body.addLayout(row)

        self.web_status = QLabel(webserver.status_text())
        self.web_status.setWordWrap(True)
        self.web_status.setStyleSheet(theme.caption_label_qss())
        card.add(self.web_status)

        # ---- 起停按钮：探测不到在跑时，用户可以**在这里拉起** ----
        row2 = QHBoxLayout()
        self.web_probe_btn = QPushButton("刷新状态")
        self.web_probe_btn.clicked.connect(self._probe_web_status)
        self.web_start_btn = QPushButton("启动网页服务")
        self.web_start_btn.clicked.connect(self._on_web_start)
        self.web_stop_btn = QPushButton("停止网页服务")
        self.web_stop_btn.clicked.connect(self._on_web_stop)
        for b in (self.web_probe_btn, self.web_start_btn, self.web_stop_btn):
            row2.addWidget(b)
        row2.addStretch(1)
        card.body.addLayout(row2)

        # ---- 关闭策略：客户端关闭时，网页端要不要跟着关 ----
        self.web_stop_with_client = QCheckBox("客户端关闭时同步关闭网页端")
        self.web_stop_with_client.setChecked(
            self._settings.web_stop_with_client())
        self.web_stop_with_client.setToolTip(
            "勾上：客户端退出时把网页端一起停掉（端口干净释放）；"
            "取消勾选：客户端退出后网页端继续在后台跑（浏览器仍可访问）")
        card.add(self.web_stop_with_client)

        hint = QLabel(
            "网页端由客户端以**独立进程**拉起（不内嵌在客户端里），"
            "客户端只是它的配置入口。取消上面的勾选后，客户端退出时网页端"
            "会继续在后台运行（浏览器仍可访问），下次打开客户端也能探测到"
            "它。默认端口 8085（8080 常被别的程序占用）；端口被占用时状态行"
            "会说明原因。")
        hint.setWordWrap(True)
        hint.setStyleSheet(theme.caption_label_qss())
        card.add(hint)
        self.root.addWidget(card)

        # [端口改动去抖 800ms] 边拖微调框边重启网页端会把机器折腾得没完；
        # 停手 0.8s 后才提交（valueChanged 连着来只认最后一次）。
        from PyQt5.QtCore import QTimer
        self._web_port_timer = QTimer(self)
        self._web_port_timer.setSingleShot(True)
        self._web_port_timer.setInterval(800)
        self._web_port_timer.timeout.connect(self._apply_web_port)

        # [初值就位后再接线] 否则 setChecked/setValue 会把建页动作当用户操作。
        self.web_check.toggled.connect(self._on_web_enabled)
        self.web_port_spin.valueChanged.connect(self._on_web_port_changed)
        self.web_stop_with_client.toggled.connect(self._on_stop_with_client)

    # ------------------------------------------------------------------
    #                      卡片②：远程访问
    # ------------------------------------------------------------------
    def _build_remote_section(self):
        card = Card("远程访问")
        hint = QLabel(
            "在外网用域名访问网页端：先在路由器上把「外部端口」映射到本机"
            "网页端口（或配内网穿透），再在这里填域名与外部端口。二维码的"
            "「远程地址」就按这两个值生成——客户端只展示地址，不改动你的"
            "路由器设置。")
        hint.setWordWrap(True)
        hint.setStyleSheet(theme.caption_label_qss())
        card.add(hint)

        row = QHBoxLayout()
        row.addWidget(QLabel("域名"))
        self.web_domain_edit = QLineEdit()
        self.web_domain_edit.setText(self._settings.web_domain())
        # [占位符不许被截 2026-11-20] 原来「例如 hsf.example.com（DDNS
        # 域名或固定域名；留空=未配置）」要 663px，框只有 532px ⇒ Qt 用
        # 省略号截成「例如 hsf.example.com（DDNS 域名或固定域名；留空…」
        # ⇒ 用户看不清"留空=未配置"这条口径。⇒ 占位符只留**示例**，
        # 完整说明挂 tooltip（信息照样可达）；框宽按字体实测给。
        self.web_domain_edit.setPlaceholderText("例如 hsf.example.com")
        self.web_domain_edit.setToolTip(
            "填外网访问用的域名：DDNS 域名或固定域名皆可；"
            "留空 = 未配置（不做远程访问）")
        fit_input_width(self.web_domain_edit)
        row.addWidget(self.web_domain_edit, 1)
        row.addStretch(0)
        card.body.addLayout(row)

        row2 = QHBoxLayout()
        row2.addWidget(QLabel("远程端口"))
        self.web_remote_port_spin = QSpinBox()
        # 0 = 未配置（与网页端口相同）；范围与网页端口一致。
        self.web_remote_port_spin.setRange(0, 65535)
        self.web_remote_port_spin.setValue(self._settings.web_remote_port())
        self.web_remote_port_spin.setSpecialValueText("同网页端口")
        self.web_remote_port_spin.setMinimumWidth(150)
        row2.addWidget(self.web_remote_port_spin)
        row2.addStretch(1)
        card.body.addLayout(row2)
        # [说明独立成行] 原来贴在微调框右侧 ⇒ 窄宽/字号放大时被挤成三行
        # （"当控件用了"的排版病）；行内只留标签+框，说明走整行 caption。
        hint2 = QLabel("远程端口 = 路由器上映射到本机的「外部」端口；"
                       "留 0（同网页端口）表示与网页端口相同。")
        hint2.setWordWrap(True)
        hint2.setStyleSheet(theme.caption_label_qss())
        card.body.addWidget(hint2)

        row3 = QHBoxLayout()
        self.web_remote_apply = QPushButton("保存远程访问设置")
        self.web_remote_apply.clicked.connect(self._apply_remote)
        row3.addWidget(self.web_remote_apply)
        row3.addStretch(1)
        card.body.addLayout(row3)
        self.root.addWidget(card)

        # [回车即保存] 域名框敲回车是最自然的提交动作（现场键盘操作）。
        self.web_domain_edit.returnPressed.connect(self._apply_remote)

    # ------------------------------------------------------------------
    #                       卡片③：扫码直达
    # ------------------------------------------------------------------
    def _build_qr_section(self):
        card = Card("访问地址（手机扫码直达）")
        # 两个切换按钮（tab 式）：局域网 / 远程。用互斥 checkable 按钮而
        # 非下拉——现场要求是"两个可切换显示"，一眼看到两条地址的存在感。
        row = QHBoxLayout()
        self._qr_mode_group = []
        self.qr_lan_btn = QPushButton("局域网地址")
        self.qr_remote_btn = QPushButton("远程地址")
        for btn in (self.qr_lan_btn, self.qr_remote_btn):
            btn.setCheckable(True)
            btn.setAutoExclusive(True)
            self._qr_mode_group.append(btn)
            row.addWidget(btn)
        self.qr_lan_btn.setChecked(True)
        self.qr_refresh = QPushButton("刷新")
        self.qr_refresh.setToolTip("重新探测本机局域网地址并重画二维码")
        self.qr_refresh.clicked.connect(self._refresh_qr)
        row.addWidget(self.qr_refresh)
        row.addStretch(1)
        card.body.addLayout(row)

        # URL 文本：可选中复制（现场偶尔要手动敲进浏览器）。
        self.qr_url_label = QLabel("")
        self.qr_url_label.setWordWrap(True)
        self.qr_url_label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.qr_url_label.setStyleSheet(theme.caption_label_qss())
        card.add(self.qr_url_label)

        self.qr_canvas = QrCanvas()
        # 居中（封顶后左右留白由布局分摊，不贴边）。
        card.body.addWidget(self.qr_canvas, 0, Qt.AlignHCenter)

        self.qr_hint = QLabel("")
        self.qr_hint.setWordWrap(True)
        self.qr_hint.setStyleSheet(theme.caption_label_qss())
        card.add(self.qr_hint)
        self.root.addWidget(card)

        self.qr_lan_btn.toggled.connect(self._on_qr_mode_changed)

    # ------------------------------------------------------------------
    #                        网址拼接
    # ------------------------------------------------------------------
    def lan_url(self):
        """局域网访问地址（当前网页端口）。

        [单一事实源] 口径在 qr_access.lan_url（标题栏二维码弹卡同源）
        ⇒ 这里只委托，别再自己拼（两处显示的地址必须一致）。
        """
        from .qr_access import lan_url as _lan_url
        return _lan_url()

    def remote_url(self):
        """远程访问地址（域名 + 远程端口；未配域名返回空串）。

        [单一事实源] 口径在 qr_access.remote_url（标题栏二维码弹卡同源）。
        """
        from .qr_access import remote_url as _remote_url
        return _remote_url()

    def current_url(self):
        return (self.remote_url() if self.qr_remote_btn.isChecked()
                else self.lan_url())

    # ------------------------------------------------------------------
    #                          二维码刷新
    # ------------------------------------------------------------------
    def _on_qr_mode_changed(self, _checked):
        self._refresh_qr()

    def _refresh_qr(self):
        """按当前模式重画二维码 + 地址文本 + 提示行。"""
        if self.qr_remote_btn.isChecked():
            url = self.remote_url()
            if not url:
                self.qr_url_label.setText("远程地址：未配置域名")
                self.qr_canvas.clear(
                    "先在「远程访问」里填写域名（与路由器上的外部端口）")
                self.qr_hint.setText(
                    "远程访问需要路由器把外部端口映射到本机网页端口；"
                    "只填域名也能生成地址，端口默认与网页端口相同。")
                return
            self.qr_url_label.setText("远程地址：%s" % url)
            self.qr_hint.setText(
                "扫码后浏览器打开远程地址，用网页账号登录（缺省 "
                "admin 为只读账户，需要控制设备请用操作者/管理员账户）。")
        else:
            url = self.lan_url()
            self.qr_url_label.setText("局域网地址：%s" % url)
            self.qr_hint.setText(
                "手机与这台机器在同一局域网（同一路由器/WiFi）时可扫码直接"
                "打开；用网页账号登录。缺省 admin 为只读账户。")
        self.qr_canvas.set_url(url)
        if self.qr_canvas.error_text():
            # 库缺失/内容过长：把原因并入提示行（画布上也会显示）。
            self.qr_hint.setText(self.qr_canvas.error_text())

    # ------------------------------------------------------------------
    #                         网页服务起停
    # ------------------------------------------------------------------
    def _web_upstream(self):
        """上游（客户端连的那台服务端）地址；未连上时回落本地缺省。"""
        from .ports import C_SERVER_PORT
        client = getattr(self.ctx.bridge, "client", None)
        if client is not None and getattr(client, "host", ""):
            try:
                return str(client.host), int(client.port)
            except Exception:                           # noqa: BLE001
                pass
        return "127.0.0.1", int(C_SERVER_PORT)

    def on_enter(self):
        """切到本页：刷新状态行 + **跨进程探测** + 重画二维码（本机 IP 可能变）。"""
        self._refresh_web_status()
        self._probe_web_status()
        self._refresh_qr()

    def _refresh_web_status(self, probe_result=None):
        if getattr(self, "web_status", None) is not None:
            self.web_status.setText(webserver.status_text(probe_result))

    # ------------------------------------------------------------------
    #              跨进程探测 / 手动起停（2026-11-17 用户定案）
    # ------------------------------------------------------------------
    def _probe_web_status(self):
        """后台探测该端口上有没有网页服务在跑（**跨进程**）。

        [为什么必须后台] 探活是一次 HTTP 往返（端口被占/防火墙时还会等到
        超时）⇒ 在 UI 线程做会让界面卡住 ⇒ 放 worker 线程，结果用信号回。
        """
        import threading
        port = int(self.web_port_spin.value())
        self._probe_seq += 1
        seq = self._probe_seq

        def _work():
            info = webserver.probe(port, timeout=1.2)
            self.webProbeDone.emit(seq, info)

        try:
            threading.Thread(target=_work, name="hsf-web-probe",
                             daemon=True).start()
        except (RuntimeError, OSError):
            _work()
        self.ctx.set_status("正在探测网页服务状态…")

    def _on_web_probe_done(self, seq, info):
        """探测结果（UI 线程）：刷状态行 + 按钮可用性。

        [过期结果一律丢弃] 探测异步 ⇒ 结果可能乱序 ⇒ 旧的（"没起"）会把
        新的（"在跑"）覆盖 ⇒ 状态行回退成"已停止"（实测：改端口框的
        去抖探测后到，把刚起的网页端刷没了）。⇒ 只认**最新一次**。
        """
        if int(seq) != int(getattr(self, "_probe_seq", 0)):
            return
        self._refresh_web_status(info)
        # [失败原因叠加在状态行末尾] 探测结果只说"谁在跑"，还要说"你的启动
        # 被拒了、为什么" ⇒ 两句都留。
        _err = getattr(self, "_web_last_error", "")
        if _err:
            self.web_status.setText("%s　※ 启动被拒：%s"
                                    % (self.web_status.text(), _err))
        alive = bool(info.get("alive"))
        own = bool(info.get("own"))
        # [按钮口径] 在跑且**不是本客户端的** ⇒ 「启动」不该再点（端口已占，
        # 起来必失败）；「停止」也不该点（不是本客户端拉起来的，不该杀）。
        self.web_start_btn.setEnabled(not alive)
        self.web_stop_btn.setEnabled(alive and (own or self._own_detached()))
        if alive and not own:
            self.ctx.set_status(
                "网页服务在运行（其它进程）：%s" % (info.get("url") or ""))
        elif alive:
            self.ctx.set_status("网页服务在运行（本客户端启动的独立进程）")
        else:
            self.ctx.set_status("网页服务未运行（可用「启动网页服务」拉起）")

    def _own_detached(self):
        """本客户端是否拉起了网页端子进程（那种才能由本客户端停）。"""
        return webserver.own_process_alive()

    def _on_web_start(self):
        """手动拉起网页服务（后台线程；形态按"关闭时同步关闭"开关）。"""
        import threading
        port = int(self.web_port_spin.value())
        host, upstream = self._web_upstream()

        def _work():
            ok, reason = webserver.start(host, upstream, port)
            self.webOpDone.emit(bool(ok), str(reason), True)

        # [起服期间状态行**立即**给反馈 2026-11-17 实测] 网页端起来要
        # 1~2s（独立进程 + 探活）⇒ 这段时间状态行若还停在"已停止"，用户
        # 会以为没起来 ⇒ 先写"正在启动…"。
        self.web_status.setText("正在启动网页服务…")
        try:
            threading.Thread(target=_work, name="hsf-web-op",
                             daemon=True).start()
        except (RuntimeError, OSError):
            _work()
        self.ctx.set_status("正在启动网页服务…")

    def _on_web_stop(self):
        """手动停止网页服务（**只停本客户端起的**）。"""
        import threading

        def _work():
            webserver.stop()
            self.webOpDone.emit(True, "", False)

        try:
            threading.Thread(target=_work, name="hsf-web-op",
                             daemon=True).start()
        except (RuntimeError, OSError):
            _work()
        self.ctx.set_status("正在停止网页服务…")

    def _on_stop_with_client(self, on):
        """「客户端关闭时同步关闭网页端」勾选变化：落盘。

        [为什么只落盘、不当场换形态] 形态（内嵌/独立进程）在**起停**那一刻
        决定 ⇒ 换勾选后要换形态得重启网页服务；这里如实提示，避免用户以为
        勾一下就立刻换了。
        """
        self._settings.set_value(self._settings.K_WEB_STOP_WITH_CLIENT,
                                 bool(on))
        self.ctx.set_status(
            "已保存：客户端关闭时%s网页端（下次起停网页服务时生效）"
            % ("同步关闭" if on else "**不同步关闭**（网页端留在后台继续跑）"))

    def _on_web_enabled(self, on):
        """开关改动：落盘 + 即时起停 + 状态行（起停挪后台线程）。

        [起停必须在后台] `webserver.start/restart` 里有全包导入 +
        `wait_ready(10.0)`（失败路径等满）+ stop 的 `thread.join(5)`
        ——直接在 UI 线程调，端口被占/包目录坏时勾一下复选框界面就冻 ~10s。
        """
        import threading
        self._settings.set_value(self._settings.K_WEB_ENABLED, bool(on))
        port = self._settings.web_port()
        host, upstream = self._web_upstream() if on else (None, None)

        def _work():
            if on:
                ok, reason = webserver.start(host, upstream, port)
            else:
                webserver.stop()
                ok, reason = True, ""
            self.webOpDone.emit(bool(ok), str(reason), bool(on))

        try:
            threading.Thread(target=_work, name="hsf-web-op",
                             daemon=True).start()
        except (RuntimeError, OSError):
            # 起不了线程（收尾期）：就地同步做——比什么都不做强。
            _work()
        self.ctx.set_status("正在%s网页服务…" % ("启动" if on else "停止"))

    def _on_web_op_done(self, ok, reason, on):
        """后台起停的完成回调（Qt 信号 ⇒ UI 线程）。"""
        if on:
            self.ctx.set_status("网页服务已开启" if ok
                                else "网页服务启动失败：%s" % reason)
        else:
            self.ctx.set_status("网页服务已关闭")
        # [失败原因要**留在页内** 2026-11-17 实测] 只写底部状态栏时，用户
        # 点了「启动网页服务」却在页内只看到"运行中（其它进程）"，不知道
        # 自己的启动**被拒**了 ⇒ 记下来，状态行里带一句。
        # [为什么不能直接 setText] 紧接着的探测刷新会把这句话**覆盖**成
        # "运行中（其它进程）" ⇒ 只能"记 + 叠加显示"。
        self._web_last_error = str(reason) if (on and not ok) else ""
        # 端口可能变了（起停用的是落盘值）⇒ 二维码跟着更新。
        self._refresh_qr()
        # [状态行必须靠**探测**刷新 2026-11-17 实测] 独立进程形态下没有
        # "内嵌句柄"可看 ⇒ 不带探测结果地刷新只会显示"已停止"（实际在跑）。
        self._probe_web_status()

    def _on_web_port_changed(self, _value):
        """端口改动：去抖后提交（不在此处直接重启）。"""
        self._web_port_timer.start()

    def _apply_web_port(self):
        """提交端口：落盘 + 在跑则后台重启换监听口。"""
        port = int(self.web_port_spin.value())
        self._settings.set_value(self._settings.K_WEB_PORT, port)
        if webserver.running():
            import threading
            host, upstream = self._web_upstream()

            def _work():
                ok, reason = webserver.restart(host, upstream, port)
                self.webOpDone.emit(bool(ok), str(reason), True)

            try:
                threading.Thread(target=_work, name="hsf-web-op",
                                 daemon=True).start()
            except (RuntimeError, OSError):
                ok, reason = webserver.restart(host, upstream, port)
                self.webOpDone.emit(bool(ok), str(reason), True)
            self.ctx.set_status("正在切换网页端口到 %d…" % port)
            return
        self.ctx.set_status("网页端口已改为 %d" % port)
        # [同款坑：状态行要靠探测刷新] 独立进程形态下没有"内嵌句柄"⇒
        # 不带探测结果地刷新只会显示"已停止"。
        self._probe_web_status()
        self._refresh_qr()

    def _apply_remote(self):
        """保存远程访问设置（域名规范化 + 远程端口）并重画二维码。"""
        domain = self._settings.normalize_domain(self.web_domain_edit.text())
        port = int(self.web_remote_port_spin.value())
        self._settings.set_value(self._settings.K_WEB_DOMAIN, domain)
        self._settings.set_value(self._settings.K_WEB_REMOTE_PORT, port)
        # 回写规范化结果（用户看到的就是落盘值）。
        if self.web_domain_edit.text() != domain:
            self.web_domain_edit.setText(domain)
        self.ctx.set_status(
            "远程访问已保存：%s" % (("http://%s:%d/"
                                    % (domain, port or self._settings.web_port()))
                                   if domain else "未配置域名"))
        self._refresh_qr()
