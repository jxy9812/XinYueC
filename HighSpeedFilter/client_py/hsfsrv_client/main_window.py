# -*- coding: utf-8 -*-
"""主窗：三段式 chrome + 页面堆栈 + 切页/订阅协调。

[C 端对应] HsfUi.c —— 装配标题栏/堆栈/状态栏，维护页序与订阅联动。

[订阅联动] 切页时把订阅集合收敛成「本页需要的主题」：
  只订阅当前页要显示的东西。这样 17 个页面不会同时把 5 个主题全订上，
  链路流量只与「当前页」有关（C 端同款策略：切页才订阅、隐藏即退订）。
"""

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import QMainWindow, QStackedWidget, QWidget

from . import pages_registry as reg
from . import proto, theme
from .page_base import PageContext
from .pages_monitor import AnalogPage, DigitalPage, VfdPage, WaterPage
from .pages_valve import ValvePage
from .pages_other import (AlarmPage, HistoryPage, HomePage, LocalSettingsPage,
                          LogPage, MainMenuPage, NetworkPage, OverlayPage,
                          RemoteSettingsPage, SubMenuPage, WindowPage)
from .widgets import ChromeFrame, NavPanel


def _build_page(page_index, ctx):
    """页索引 → 页面实例（与 C 端 kHsfBuilders 表同序）。"""
    if page_index == reg.PAGE_HOME:
        return HomePage(ctx)
    if page_index == reg.PAGE_MENU:
        return MainMenuPage(ctx)
    if page_index in reg.SUBMENU_ENTRIES:
        title = reg.page_name(page_index)
        return SubMenuPage(ctx, title, reg.SUBMENU_ENTRIES[page_index])
    if page_index == reg.PAGE_MONITOR_DIGITAL:
        return DigitalPage(ctx)
    if page_index == reg.PAGE_MONITOR_ANALOG:
        return AnalogPage(ctx)
    if page_index == reg.PAGE_MONITOR_VFD:
        return VfdPage(ctx)
    if page_index == reg.PAGE_MONITOR_FLOWMETER:
        return WaterPage(ctx)
    if page_index == reg.PAGE_MONITOR_VALVE:
        return ValvePage(ctx)
    if page_index == reg.PAGE_DATA_HISTORY:
        return HistoryPage(ctx)
    if page_index == reg.PAGE_DATA_LOG:
        return LogPage(ctx)
    if page_index == reg.PAGE_DATA_ALARM:
        return AlarmPage(ctx)
    if page_index == reg.PAGE_SETTINGS_LOCAL:
        return LocalSettingsPage(ctx)
    if page_index == reg.PAGE_SETTINGS_REMOTE:
        return RemoteSettingsPage(ctx)
    if page_index == reg.PAGE_SETTINGS_NETWORK:
        return NetworkPage(ctx)
    if page_index == reg.PAGE_SETTINGS_OVERLAY:
        return OverlayPage(ctx)
    if page_index == reg.PAGE_SETTINGS_WINDOW:
        return WindowPage(ctx)
    # 兜底：不该有未知页，但绝不抛异常把界面搞崩。
    return SubMenuPage(ctx, reg.page_name(page_index), [])


class MainWindow(QMainWindow):
    """主窗。"""

    #: **全局常驻订阅**的主题（不管当前在哪一页都要）。
    #:
    #: [实锅] 顶栏的温湿度/设备健康是 chrome，任何页面都要显示。
    #: 早先切页时把订阅集合收敛成 "只本页需要的主题"，数字量页不订阅
    #: 温湿度 ⇒ 顶栏温湿度立刻变成 `--`（用户一眼看出来不对劲）。
    #: 故这两类必须无条件常驻订阅。
    GLOBAL_TOPICS = (
        proto.TOPIC_TEMPHUM_STATUS,
        proto.TOPIC_DEVICE_HEALTH,
    )

    def __init__(self, bridge):
        QMainWindow.__init__(self)
        self.bridge = bridge
        self.setWindowTitle("高速过滤器")
        self.resize(theme.WIN_DEFAULT_W, theme.WIN_DEFAULT_H)

        self.chrome = ChromeFrame("主界面", reg.NAV_TREE)
        self.setCentralWidget(self.chrome)

        self.title_bar = self.chrome.title_bar
        self.status_bar = self.chrome.status_bar
        self.nav = self.chrome.nav

        self.stack = QStackedWidget()
        self.chrome.set_page_widget(self.stack)

        # [状态栏必须走信号，不能直连] 页面里的控制回调（如变频器启停应答）
        # 跑在**网络线程**上，直接调 status_bar.set_message 就是从非 UI 线程
        # 碰 QWidget —— Qt 会直接崩进程（实测：点"自由停车"界面立刻消失，
        # 无任何 Python traceback，服务端会话也跟着断）。
        # bridge.statusMessage 是 Qt 信号：跨线程 emit 时自动排队到 UI 线程。
        self.ctx = PageContext(bridge,
                               navigate=self.goto_page,
                               set_status=bridge.statusMessage.emit)
        self.pages = {}             # page_index -> Page
        self.current = -1
        self._compact = False

        # ---- 接线 ----
        self.nav.navigate.connect(self.goto_page)
        self.title_bar.menuClicked.connect(self._on_menu_clicked)
        self.title_bar.modeClicked.connect(self._on_mode_clicked)
        bridge.connected.connect(self._on_connected)
        bridge.disconnected.connect(self._on_disconnected)
        bridge.authFailed.connect(lambda err: self.status_bar.set_message(
            "登录失败：%s" % err))
        bridge.statusMessage.connect(self.status_bar.set_message)

        # ---- 周期刷新（200ms，驱动各页 refresh）----
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setInterval(200)
        self._refresh_timer.timeout.connect(self._refresh_current)
        self._refresh_timer.start()

        # ---- 顶栏数据（1s 一次，避免每 200ms 重排文本）----
        self._chrome_timer = QTimer(self)
        self._chrome_timer.setInterval(1000)
        self._chrome_timer.timeout.connect(self._refresh_chrome)
        self._chrome_timer.start()

        self.goto_page(reg.PAGE_HOME)
        # [--auto 路径] 登录发生在 MainWindow 构造之前，connected 信号早已
        # 发过——Qt 信号不回放。故这里必须主动同步一次连接态，否则状态栏
        # 永远停在"未连接"（实锅：板上截图状态栏显示未连接但数据是全的）。
        if bridge.is_connected():
            self._on_connected(bridge.role)

        # ---- 软键盘（触摸屏无键盘设备；自动探测，HSF_TOUCH 可强制）----
        # 任意 QLineEdit 获得焦点即从主窗底部弹出（登录框/历史筛选/路径等
        # 全覆盖）。桌面带键盘的机器探测为关，不会弹出。
        from .softkeyboard import attach_soft_keyboard
        self.soft_kbd = attach_soft_keyboard(self)

    # ==================================================================
    #                            切页
    # ==================================================================
    def goto_page(self, page_index):
        if not 0 <= page_index < reg.PAGE_COUNT:
            return
        if page_index == self.current:
            return

        # 离页：通知 + 由新页接管订阅。
        old = self.pages.get(self.current)
        if old is not None:
            try:
                old.on_leave()
            except Exception as exc:                    # noqa: BLE001
                self.status_bar.set_message("离页异常：%s" % exc)

        page = self.pages.get(page_index)
        if page is None:
            try:
                page = _build_page(page_index, self.ctx)
            except Exception as exc:                    # noqa: BLE001
                # 单页构建失败不得连累整个界面。
                self.status_bar.set_message(
                    "页面「%s」构建失败：%s" % (reg.page_name(page_index), exc))
                return
            self.pages[page_index] = page
            self.stack.addWidget(page)

        self.stack.setCurrentWidget(page)
        self.current = page_index
        self.title_bar.set_title(reg.page_name(page_index))
        self.nav.set_current(page_index)

        # 订阅收敛：只订本页需要的主题。
        if self.bridge.is_connected():
            self.bridge.set_topics(set(page.topics())
                                   | set(self.GLOBAL_TOPICS))
        try:
            page.on_enter()
        except Exception as exc:                        # noqa: BLE001
            self.status_bar.set_message("入页异常：%s" % exc)
        self._refresh_current()

    def _on_menu_clicked(self):
        """☰：宽屏收起/展开导航；窄屏切到主菜单页。"""
        if self._compact:
            self.goto_page(reg.PAGE_MENU)
        else:
            visible = not self.nav.isVisible()
            self.nav.setVisible(visible)

    def _on_mode_clicked(self):
        """顶栏模式钮：手动↔自动切换（需 root）。"""
        model = self.bridge.model
        if not self.bridge.is_connected():
            self.status_bar.set_message("未连接服务端")
            return
        if self.bridge.role < proto.ROLE_ADMIN:
            self.status_bar.set_message("权限不足：切换运行模式需要 root 账户")
            return
        target = 0 if model.is_auto() else 1
        self.status_bar.set_message("正在切换到%s模式…"
                                    % ("自动" if target else "手动"))

        def _done(doc):
            if not doc.get("ok"):
                self.status_bar.set_message("切换失败：%s"
                                            % doc.get("err", "未知"))

        self.bridge.set_run_mode(target, on_done=_done)

    # ==================================================================
    #                            刷新
    # ==================================================================
    def _refresh_current(self):
        page = self.pages.get(self.current)
        if page is None:
            return
        try:
            page.refresh()
        except Exception as exc:                        # noqa: BLE001
            self.status_bar.set_message("刷新失败：%s" % exc)

    def _refresh_chrome(self):
        """顶栏：温湿度 + 健康 + 运行模式。"""
        model = self.bridge.model
        th = model.temphum
        self.title_bar.set_temperature(
            (th.temp_tenths or 0) / 10.0,
            valid=bool(th.temp_valid and th.is_fresh()
                       and th.temp_tenths is not None))
        self.title_bar.set_humidity(
            (th.hum_tenths or 0) / 10.0,
            valid=bool(th.hum_valid and th.is_fresh()
                       and th.hum_tenths is not None))
        fault, offline = model.health.counts()
        self.title_bar.set_health(fault, offline)
        self.title_bar.set_mode(model.is_auto(), model.run_mode_known)
        # 自动控制器的**实际**运行状态（与模式位分开；None=服务端没报）
        self.title_bar.set_auto_running(
            model.is_auto_running(),
            known=(model.auto_running is not None))

    # ==================================================================
    #                          连接事件
    # ==================================================================
    def _on_connected(self, role):
        self.status_bar.set_connection(
            "已连接 · %s · %s" % (self.bridge.user,
                                  "管理员" if role >= proto.ROLE_ADMIN
                                  else "只读"), ok=True)
        self.bridge.start_refresh()
        # 登录后先拉一次运行模式（模式钮要显示当前状态）。
        self.bridge.refresh_run_mode()
        # 首次进页面时 set_topics 可能因未连接而跳过，这里补一次。
        page = self.pages.get(self.current)
        if page is not None:
            self.bridge.set_topics(set(page.topics())
                                   | set(self.GLOBAL_TOPICS))
        self.status_bar.set_message("已连接服务端")

    def _on_disconnected(self, reason):
        self.status_bar.set_connection("未连接", ok=False)
        self.status_bar.set_message(reason)
        # 断线后清订阅记录（重连时重新订）。
        self.bridge.stop_refresh()

    # ==================================================================
    #                        窗口尺寸/形态
    # ==================================================================
    def resizeEvent(self, event):
        QMainWindow.resizeEvent(self, event)
        compact = self.width() < theme.COMPACT_MAX_W
        if compact != self._compact:
            self._compact = compact
            self.nav.set_compact(compact)
            # 窄屏收起导航（由主菜单页承载导航）。
            if compact:
                self.nav.setVisible(False)
            else:
                self.nav.setVisible(True)
            self.status_bar.set_message(
                "紧凑模式（%dpx）" % self.width() if compact else "宽屏模式")
