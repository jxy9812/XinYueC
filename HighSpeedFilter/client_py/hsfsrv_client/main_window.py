# -*- coding: utf-8 -*-
"""主窗：三段式 chrome + 页面堆栈 + 切页/订阅协调。

[C 端对应] HsfUi.c —— 装配标题栏/堆栈/状态栏，维护页序与订阅联动。

[订阅联动] 切页时把订阅集合收敛成「本页需要的主题」：
  只订阅当前页要显示的东西。这样 16 个页面不会同时把 5 个主题全订上，
  链路流量只与「当前页」有关（C 端同款策略：切页才订阅、隐藏即退订）。
"""

from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import QApplication, QMainWindow, QStackedWidget, QWidget

from . import pages_registry as reg
from . import permissions, proto, theme
from .page_base import DataTable, PageContext
from .pages_camera import CameraPage
from .pages_monitor import (AnalogPage, DigitalPage, FlowPage, VfdPage,
                            WaterPage)
from .pages_valve import ValvePage
from .pages_other import (AlarmPage, HistoryPage, HomePage, LocalSettingsPage,
                          LogPage, MainMenuPage, NetworkPage,
                          RemoteSettingsPage, SubMenuPage)
from .pages_web import WebSettingsPage
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
    # [2026-11-22 水质/流量拆分] 页 6 = 水质（浊度×2），页 18 = 流量
    # （流量计 + 进水泵 + 流量趋势）。页号追加末尾、不重排既有页。
    if page_index == reg.PAGE_MONITOR_WATER:
        return WaterPage(ctx)
    if page_index == reg.PAGE_MONITOR_FLOW:
        return FlowPage(ctx)
    if page_index == reg.PAGE_MONITOR_VALVE:
        return ValvePage(ctx)
    # [2026-11-19] 相机页（设备组下；页号 17，追加末尾）。
    if page_index == reg.PAGE_MONITOR_CAMERA:
        return CameraPage(ctx)
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
    # [2026-11-16] 网页服务独立页（仅 Python 客户端；C 端无此页）。
    if page_index == reg.PAGE_SETTINGS_WEB:
        return WebSettingsPage(ctx)
    # [2026-11-13 三页合并] 悬浮窗设置(16) / 窗口设置(17) 的内容已并入
    # 本地设置页(13)，两页号从注册表删除 ⇒ 建页表里不再有它们。
    # 兜底：不该有未知页，但绝不抛异常把界面搞崩。
    return SubMenuPage(ctx, reg.page_name(page_index), [])


class MainWindow(QMainWindow):
    """主窗。"""

    #: 账户菜单动作（MainWindow 只发信号，实际"断开/重弹登录窗"由
    #: __main__ 的统一登录状态机执行——进程不退出，绝不走 _teardown）。
    switchUserRequested = pyqtSignal()
    logoutRequested = pyqtSignal()

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
        # [固定产品名 2026-11-12 用户定案] 标题栏居中文字不再随页面变
        # （原来显示页名「主界面/数字量/…」）；窗口标题同步用同一串。
        self.setWindowTitle(theme.APP_TITLE)
        self.resize(theme.WIN_DEFAULT_W, theme.WIN_DEFAULT_H)

        self.chrome = ChromeFrame(theme.APP_TITLE, reg.NAV_TREE)
        self.setCentralWidget(self.chrome)
        # [大 QSS 挂"页面"而不是整窗/QApplication] 2026-11-17 板端实测：
        # app 级 setStyleSheet = 9.7s、整窗 = 1.7s、单页 = 0.2~0.4s
        # （1445 控件 / 17 页）。换主题时只重刷**当前页** + chrome，
        # 其它页面按代数懒刷新（见 goto_page / refresh_theme）。

        self.title_bar = self.chrome.title_bar
        self.status_bar = self.chrome.status_bar
        self.nav = self.chrome.nav

        self.stack = QStackedWidget()
        # [大 QSS 挂页面**容器**，页面继承 —— 2026-11-18 板端实锅]
        # 原来每建一页就单独挂一遍大 QSS（板端实测每页 26~378ms，17 页
        # 建页总耗时 5.3s ⇒ 现场「切页卡顿一会儿才打开」）。页面本就是
        # stack 的子孙 ⇒ 挂一次到 stack，页面**继承**即可（Qt 的样式表
        # 沿父链继承，新子件入栈时按继承来的 QSS 兑现一次 polish）。
        # [为什么不是挂主窗] 大 QSS 里有 `QMainWindow, QDialog` 规则
        # （背景/调色板），挂主窗会连带改 chrome（标题栏/导航/状态栏）的
        # 观感；挂 stack 只覆盖页面子树 ⇒ chrome 不受影响。
        # [等价性已验] 改前（每页单独挂）/ 改后（页面继承）逐像素比对：
        # 17 页**差异 0 像素**（板端离屏渲染 + 真实 CJK 字体）。
        theme.apply_window(self.stack)
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
        #: 紧凑模式**强制覆盖**（None=按窗宽自动判；True/False=本机设置指定）。
        self._compact_force = None

        # ---- 接线 ----
        self.nav.navigate.connect(self.goto_page)
        self.title_bar.menuClicked.connect(self._on_menu_clicked)
        self.title_bar.modeClicked.connect(self._on_mode_clicked)
        # [标题栏二维码徽章] 点击 → 弹「网页访问地址」卡（局域网/远程
        # 两个二维码可切换；复用 Web 设置页的同一份地址口径）。懒建：
        # 不点不建（首屏少一个非模态窗口）。
        self._qr_card = None
        self.title_bar.qrClicked.connect(self._on_qr_clicked)
        # [标题栏相机徽章 2026-11-22] 点开/再点关闭**悬浮相机画面卡**。
        # 卡片只在打开期间存在 ⇒ 相机主题只在其存在期间订阅，关闭即退订
        # （用户口径「免得后台一直传输大量相机数据」）。每次打开重新建。
        self._camera_card = None
        self.title_bar.cameraClicked.connect(self._on_camera_clicked)
        bridge.connected.connect(self._on_connected)
        bridge.disconnected.connect(self._on_disconnected)
        bridge.authFailed.connect(lambda err: self.status_bar.set_message(
            "登录失败：%s" % err))
        bridge.statusMessage.connect(self.status_bar.set_message)
        # [账户菜单] StatusBar 只发"点了哪一项"，MainWindow 再转成自己的
        # 信号交给 __main__ 的登录状态机（断开 + 重弹登录窗，进程不退出）。
        self.status_bar.switchUserRequested.connect(
            self.switchUserRequested.emit)
        self.status_bar.logoutRequested.connect(self.logoutRequested.emit)

        # ---- 周期刷新（200ms，驱动各页 refresh）----
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setInterval(200)
        self._refresh_timer.timeout.connect(self._refresh_current)
        self._refresh_timer.start()

        # [否决过的"推送立即刷新" 2026-11-19 —— 别再加回来]
        # 曾试过「收到 dataUpdated("camera") 就立刻刷当前页」，想消掉
        # 200ms 定时器带来的量化误差。**实测更糟**（板端真机，25s）：
        #     纯 200ms 定时器：中位 607ms / p95 816 / max 1019 / σ=198
        #     推送立即刷新  ：中位 459ms / p95 1862 / max 2236 / σ=516
        # ⇒ 平均是快了，但**尖峰翻倍、抖动失控**（网络突发时多帧挤进来
        # 逐帧重绘，把主线程打出一串 44/62/71ms 的连刷，随后补一个 2s 的
        # 空档）。定时器那条虽然慢一点，但**节拍稳定** —— 看画面时
        # "稳定地慢"远好于"忽快忽慢"。
        # ⇒ 若要再优化上屏节拍，方向是**让帧产出本身更匀**（服务端/采集
        # 侧），不是把刷新绑到推送事件上。

        # ---- 顶栏数据（1s 一次，避免每 200ms 重排文本）----
        self._chrome_timer = QTimer(self)
        self._chrome_timer.setInterval(1000)
        self._chrome_timer.timeout.connect(self._refresh_chrome)
        self._chrome_timer.start()

        self.goto_page(reg.PAGE_HOME)

        # ---- 页面预热（切页即时；真实会话缺省开 2026-11-19）----
        # [为什么] 板端实测：页面**懒建** ⇒ 首次进入某页要付 0.03~0.85s
        # 的建页成本（构树 + 首显 polish），二次进入只要 2~3ms ⇒ 现场
        # 「切页卡顿一会儿」几乎全是**第一次**进某页。预热把建页挪到
        # 后台（**不显示**）⇒ 点击时只剩「真正显形」那点成本。
        # [缺省改开 2026-11-19] 现场（触摸屏）反馈「有些界面触摸明显卡顿」
        # —— 离屏归因：首次切入 = 构树 + 整页 QSS polish（最重的远端设置
        # 页 501 控件 ≈ 100ms，现场机上还要放大数倍），**每次会话每页都要
        # 付一次**。预热 1.5s 一页、全部 17 页约 25s 内建完，之后切页恒
        # 2~15ms；预热步之间事件循环完好（实时刷新不受影响）。
        # [口径订正 2026-11-21] 预热清单原先是 16 项（**漏相机页 17**），
        # 注释说的「17 页」是含主界面的 18 页里的**其余**；现已补齐为
        # 17 项预热（页 0 主界面随窗构造即建，不入队列）。
        # [口径] 环境变量 HSF_PREWARM_PAGES=1/0 强制；本机设置
        # `prewarmPages=true/false` 显式指定；两者都没写 ⇒ **真实交互会话
        # 缺省开、离屏（offscreen，自动化/回归/探针）缺省关** —— 离屏上下文
        # 预热会把页面提前塞进 pages/stack，污染回归断言与测量的确定性。
        self._prewarm_queue = []
        self._prewarm_timer = QTimer(self)
        self._prewarm_timer.setInterval(self.PREWARM_INTERVAL_MS)
        self._prewarm_timer.timeout.connect(self._prewarm_step)
        # [字形预热] 全部主题的 −/+/下拉箭头 PNG 在启动后台生成好（文件已
        # 存在只做一次 stat）⇒ 此后建页/换主题路径零文件 IO（原先是
        # 「首次遇到某主题」才在 UI 线程写盘）。
        QTimer.singleShot(0, theme.warm_glyph_cache)
        try:
            if self._prewarm_enabled():
                self._start_prewarm()
        except Exception:                           # noqa: BLE001
            pass

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

    @staticmethod
    def _prewarm_enabled():
        """预热开关解析（环境变量 > 本机设置 > 按平台缺省）。"""
        import os as _os
        raw = (_os.environ.get("HSF_PREWARM_PAGES", "").strip().lower())
        if raw in ("1", "true", "on", "yes"):
            return True
        if raw in ("0", "false", "off", "no"):
            return False
        from . import settings as ui_settings
        # [键存在才认] 只区分「INI 显式写过」与「没写过」⇒ 没写时走平台
        # 缺省（真实会话开 / 离屏关）。contains 是只读查询。
        try:
            if ui_settings._store().contains(ui_settings.K_PREWARM_PAGES):
                return ui_settings.get_bool(ui_settings.K_PREWARM_PAGES, True)
        except Exception:                           # noqa: BLE001
            pass
        app = QApplication.instance()
        plat = ""
        if app is not None:
            try:
                plat = bytes(app.platformName()).decode("ascii", "replace")
            except Exception:                       # noqa: BLE001
                plat = str(app.platformName())
        return plat not in ("offscreen", "minimal")

    # ==================================================================
    #                            切页
    # ==================================================================
    # ---- 页面预热 ----
    #: 预热顺序：常用页面先建（操作员最可能先点到的先备好）；
    #: 最重的「远端设置」放最后（它的建页会把 UI 线程占最久）。
    #:
    #: [2026-11-21 补相机页 17] 原清单 16 项**漏了页 17（相机）**——
    #: 而相机页恰是**最重的一页**（ES 解码线程 + 704² 帧缓冲 + 贴拍定时器）
    #: ⇒ 首次进入仍要付全款，与「预热解决首次卡顿」的初衷相悖。
    #: ⇒ 补进清单、**放末尾**（相机页最重，放前面会拖慢其它页的预热节奏；
    #: 每步 1.5s 的间隔是给实时刷新留事件循环的，不能被拖）。
    #: 预热**不做 on_enter**（不取数、不订阅）⇒ 建相机页不产生网络流量。
    #: [2026-11-22 水质/流量拆分] 新增页 18（流量）也进清单（放末尾）——
    #: 预热清单必须覆盖全部非主页页号，漏一页首次进入仍付全款。
    PREWARM_ORDER = (5, 9, 6, 11, 10, 3, 7, 4, 13, 15, 16, 14, 1, 2, 8, 12, 17,
                     18)
    #: 两次预热之间的间隔（毫秒）：给实时刷新留出事件循环。
    PREWARM_INTERVAL_MS = 1500

    def _start_prewarm(self):
        """启动页面预热（逐页后台建；**不显示**）。"""
        self._prewarm_queue = [i for i in self.PREWARM_ORDER
                               if i not in self.pages]
        if self._prewarm_queue:
            self._prewarm_timer.start()

    def _prewarm_step(self):
        """预热一页：建页 + polish + 布局激活（**不切过去**）。

        [为什么连 polish 一起做] 板端实测：只建页的话，点击时还要付
        首显 polish 的钱（最重页 510ms → 406ms，只省了构树那部分）；
        加上 ensurePolished + 布局激活后 → 295ms（**真正显形**的那点
        成本留到点击那一刻，已尽量前置）。
        [不做 on_enter] 预热只建界面，**不取数、不订阅**（那是进入页
        的事 ⇒ 预热不该产生网络流量）。
        """
        if not self._prewarm_queue:
            self._prewarm_timer.stop()
            return
        idx = self._prewarm_queue.pop(0)
        if idx in self.pages:
            return
        try:
            page = _build_page(idx, self.ctx)
        except Exception:                           # noqa: BLE001
            return
        self.pages[idx] = page
        self.stack.addWidget(page)
        try:
            page.ensurePolished()
            lay = page.layout()
            if lay is not None:
                lay.activate()
        except Exception:                           # noqa: BLE001
            pass
        page._theme_gen = theme.generation()
        self._refresh_table_styles(page)
        # [日志 2026-11-18] 预热进度写 stdout（板端 journald 可见）：
        # 现场可据此判断「切页还卡不卡」= 预热有没有跑完。
        print("[prewarm] 已预建页 %s（剩 %d 页）"
              % (reg.page_name(idx), len(self._prewarm_queue)),
              flush=True)

    def goto_page(self, page_index):
        if not 0 <= page_index < reg.PAGE_COUNT:
            return
        if page_index == self.current:
            return
        # [切页耗时诊断 2026-11-18] 默认不打；设 HSF_TRACE_PAGE_MS=1 打开
        # （现场「切页卡不卡」的判据）。
        import os as _os
        import time as _time
        _trace = _os.environ.get("HSF_TRACE_PAGE_MS", "") not in ("", "0")
        _t0 = _time.perf_counter()
        _was_built = page_index in self.pages

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
            # [大 QSS 不再逐页挂 2026-11-18] 大 QSS 挂在**页面容器**上
            # （见 __init__）⇒ 页面继承即可，省掉每页一遍的 QSS 解析与
            # 整棵子树重洗（板端实测：每页 26~378ms）。改前/改后逐像素
            # 比对 **0 差异**。cards 构造时已带当前主题的专属 QSS。
            self._refresh_table_styles(page)
            page._theme_gen = theme.generation()

        self.stack.setCurrentWidget(page)
        self.current = page_index
        # [懒刷新] 本页在上次换主题时可能处于隐藏态（没跟着刷）⇒ 代数
        # 落后就在切入这一刻补刷。**只刷卡片自身的 QSS 与表格样式**：
        # 大 QSS 已在容器上随换主题一起换过（页面继承），不必再逐页重挂。
        if getattr(page, "_theme_gen", -1) != theme.generation():
            app = QApplication.instance()
            if app is not None:
                theme.refresh_widgets(app, root=page)
            self._refresh_table_styles(page)
            page._theme_gen = theme.generation()
        # [标题栏恒为产品名] 不再跟页面名走（用户口径同上）；显式重设一次，
        # 防止外部代码改过标题。
        self.title_bar.set_title(theme.APP_TITLE)
        self.nav.set_current(page_index)

        # 订阅收敛：只订本页需要的主题（+ 常驻 + 悬浮相机卡打开时的相机流）。
        if self.bridge.is_connected():
            self._apply_topics()
        try:
                page.on_enter()
        except Exception as exc:                        # noqa: BLE001
            self.status_bar.set_message("入页异常：%s" % exc)
        self._refresh_current()
        if _trace:
            print("[page] 切到 %s  用时 %.0f ms（已建=%s）"
                  % (reg.page_name(page_index),
                     (_time.perf_counter() - _t0) * 1000.0,
                     "是" if _was_built else "否"), flush=True)

    @staticmethod
    def _refresh_table_styles(page):
        """页面里的**表格**样式重刷一遍（表头 own-QSS 的边框会被重解析丢掉）。

        [为什么单独做] 2026-11-17 用户「表格标题栏样式要对齐 web 端」：
        进栈/换主题时 Qt 重新解析子树样式 ⇒ 表头的竖分隔线回落 Fusion 默认
        的 #505152 深灰（网页端 #E4E8EC）⇒ 重刷一次即恢复（实测）。
        """
        try:
            for tbl in page.findChildren(DataTable):
                tbl.refresh_style()
        except Exception:                               # noqa: BLE001
            pass

    def refresh_theme(self, theme_id=None):
        """换主题：只刷 chrome + **当前页**（隐藏页在切入时按代数补刷）。

        [为什么不是整窗] 2026-11-17 板端实测（1445 控件/17 页）：
        app 级 9.7s、整窗 1.7s、单页 0.2~0.4s。换主题时用户只看得见
        当前页 ⇒ 其余页面延后到 goto_page 再刷（代数对不上才刷）。
        """
        app = QApplication.instance()
        if app is not None:
            # [只刷三个 chrome 控件本身] 不能用 root=self.chrome：**页面栈
            # 是 chrome 的子孙**（set_page_widget(stack)）⇒ findChildren
            # 会把 17 页 54 张卡片全刷一遍（板端实测 1.2s，把省下的又还
            # 回去了）。三个控件各自子树小（12/4/19 个），合计 ~30ms。
            for w in (self.title_bar, self.status_bar, self.nav):
                theme.refresh_widgets(app, root=w)
        page = self.pages.get(self.current)
        if page is None:
            return
        # [大 QSS 换到容器上 2026-11-18] 一次替换 ⇒ 全部页面继承新的大
        # QSS（原来只换当前页、其余页在切入时再各换一遍 —— 那正是现场
        # 「切页卡顿」的来源之一）。换主题是罕见动作，代价一次付清
        # （板端整栈重洗 ~1.7s，与 2026-11-17 记的整窗代价同量级）。
        theme.apply_window(self.stack, theme_id)
        if app is not None:
            theme.refresh_widgets(app, root=page)
        self._refresh_table_styles(page)
        page._theme_gen = theme.generation()

    def _on_menu_clicked(self):
        """☰：按本地设置「导航方式」走导航树或菜单页。

        · `navigation = tree`（缺省）：宽屏收起/展开左侧导航树；窄屏放不下
          导航树 ⇒ 保持旧口径切到主菜单页。
        · `navigation = page`：打开**当前页所属分类的子菜单页**（如搅拌泵 →
          「设备 选择子项」）；已在子菜单页则回主菜单页 —— 与子菜单页里的
          「← 返回主菜单」同一个上级链路。
        """
        from . import settings as ui_settings
        style = ui_settings.menu_button_style()
        if style == ui_settings.MENU_STYLE_PAGE:
            target = reg.menu_page_for(self.current)
            if target == self.current:
                target = reg.PAGE_MENU
            # [2026-11-22 用户定案「切到菜单页时隐藏导航树」] 页面模式下
            # 导航树一律收起；本机设置切换时 apply_overlay 已隐藏，这里
            # 再兜一次（例如外部改了 nav 可见性）。
            self.nav.setVisible(False)
            self.goto_page(target)
            return
        if self._compact:
            self.goto_page(reg.PAGE_MENU)
        else:
            visible = not self.nav.isVisible()
            self.nav.setVisible(visible)

    def _apply_topics(self):
        """把当前页主题 + 常驻主题 +（悬浮相机卡打开时的相机流）收敛下发。

        [为什么相机主题不常驻] 相机是 20fps 大帧；只在悬浮卡存在时订阅，
        关闭即退订 ⇒ 后台不传。相机页本身仍按页面主题订阅。
        """
        page = self.pages.get(self.current)
        if page is None:
            return
        topics = set(page.topics()) | set(self.GLOBAL_TOPICS)
        if self._camera_card is not None:
            topics.add(proto.TOPIC_CAMERA_FRAME)
        self.bridge.set_topics(topics)

    def _on_camera_clicked(self):
        """相机徽章：开/关悬浮画面卡（每次打开重新创建，关闭即销毁）。"""
        if self._camera_card is not None:
            try:
                self._camera_card.close()   # closeEvent → _on_camera_closed
            except RuntimeError:
                # 窗口已被 Qt 销毁（WA_DeleteOnClose）但 Python 引用还在 ⇒
                # 清引用并退订，下一次点击才能重新创建。
                self._camera_card = None
                self._apply_topics()
            return
        from .camera_popup import CameraPopupCard
        try:
            card = CameraPopupCard(self.bridge, self)
            card.closed.connect(self._on_camera_closed)
            # [兜底] 窗口管理器/销毁路径没走 closeEvent 时也能清引用。
            card.destroyed.connect(
                lambda _obj=None, c=card: self._on_camera_destroyed(c))
            card.show_near(self.title_bar.camera_btn)
            self._camera_card = card
            self._apply_topics()            # 此刻才开始订相机流
        except Exception as exc:                            # noqa: BLE001
            self._camera_card = None
            self.status_bar.set_message("相机画面打开失败：%s" % exc)
            self._apply_topics()

    def _on_camera_closed(self):
        """悬浮相机卡关闭：销毁引用 + 退订相机流（后台不再传帧）。"""
        self._camera_card = None
        self._apply_topics()

    def _on_camera_destroyed(self, card):
        """窗口对象销毁兜底（只清"当前这张"，避免误清新卡）。"""
        if self._camera_card is card:
            self._camera_card = None
            self._apply_topics()

    def _on_qr_clicked(self):
        """顶栏二维码徽章：弹「网页访问地址」卡（局域网/远程可切换）。

        [懒建 + toggle] 首次点击才建（非模态 QDialog）；再次点击徽章时
        已开着 ⇒ 收起（toggle 语义，与移动端弹层一致）。
        """
        if self._qr_card is None:
            from .qr_access import QrPopupCard
            self._qr_card = QrPopupCard(self)
        if self._qr_card.isVisible():
            self._qr_card.close()
            return
        self._qr_card.popup(self)

    def _on_mode_clicked(self):
        """顶栏模式钮：手动↔自动切换（需管理员权限）。"""
        model = self.bridge.model
        if not self.bridge.is_connected():
            self.status_bar.set_message("未连接服务端")
            return
        if not self.bridge.can(proto.PERM_MODE_SWITCH):
            self.status_bar.set_message(
                "权限不足：切换运行模式需要管理员账户")
            return
        target = 0 if model.is_auto() else 1
        self.status_bar.set_message("正在切换到%s模式…"
                                    % ("自动" if target else "手动"))

        def _done(doc):
            if not doc.get("ok"):
                # [必须走信号 2026-11-16 审计] 本回调在**网络线程**执行
                # （bridge.set_run_mode 的 on_done 由应答线程调）；直接
                # status_bar.set_message 是跨线程碰 QWidget——同文件
                # 「状态栏必须走信号」的注释就是这条铁律（自由停车跨线程
                # 实锅的同款，能段错误）。
                self.bridge.statusMessage.emit("切换失败：%s"
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
        """顶栏：温湿度 + 健康 + 运行模式 + **自动运行状态**。"""
        model = self.bridge.model
        # [按侧择优] 服务端按两条侧轮流推同一温湿度主题；没装温湿度站的
        # 那一侧推的是空帧 ⇒ 只留最后一帧时顶栏恒 "--"。口径对齐 C 端
        # hsfHomePickTempHum（优先 A 侧，兜底 B 侧）。
        th = model.temphum.pick()
        self.title_bar.set_temperature(
            (th.temp_tenths or 0) / 10.0 if th is not None else 0.0,
            valid=bool(th is not None and th.temp_valid
                       and th.temp_tenths is not None))
        self.title_bar.set_humidity(
            (th.hum_tenths or 0) / 10.0 if th is not None else 0.0,
            valid=bool(th is not None and th.hum_valid
                       and th.hum_tenths is not None))
        fault, offline = model.health.counts()
        self.title_bar.set_health(fault, offline)
        self.title_bar.set_mode(model.is_auto(), model.run_mode_known)
        # 自动控制器的**实际**运行状态（与模式位分开；None=服务端没报）
        self.title_bar.set_auto_running(
            model.is_auto_running(),
            known=(model.auto_running is not None))
        # [2026-11-06] 定期向服务端对账模式 + 自动运行位。
        # 为什么要轮询：现场操作柱的启动/停止按钮会**在服务端**改变自动运行
        # 状态，服务端没有"自动运行"的独立推送主题（见 02-线协议 7.10b），
        # 客户端不主动问就永远显示旧值。用户要求「按钮在自动模式下启动后，
        # 客户端顶部的状态要显示更新状态」。
        # [1s 一次] 这是低频状态，1s 足够，且不会给链路添负担（一条查询帧）。
        self.bridge.refresh_run_mode()
        # [账户 1Hz 兜底] 对齐 C 端 hsfWinLinkState 的 1Hz 补刷：断线/退出
        # 若漏了 disconnected 信号，账户也能在一拍内回「未登录」。
        # set_account 有同值早退 ⇒ 稳态零脏区（不触发重绘）。
        self._sync_account()

    # ==================================================================
    #                          连接事件
    # ==================================================================
    def _sync_account(self):
        """把 bridge 的账户态刷到状态栏账户按钮（**单一写入点**）。

        [收拢 conn 双写] 旧代码 `_on_connected` 把「user·role」拼进
        `set_connection`，而 `__main__.py` 的 --auto / 探测路径又各写一次
        「已连接 · root」（不带角色）⇒ 文案与时序都不定。现在 conn 只说
        连接态，账户一律走这里（C 端 `hsfStatusUserText` 同款三态）。
        """
        connected = self.bridge.is_connected()
        self.status_bar.set_account(
            self.bridge.user,
            permissions.role_name(self.bridge.role) if connected else "",
            logged_in=connected)

    def _on_connected(self, role):
        # [conn 只说连接态] 账号·角色改由右下角账户按钮显示（对齐 C 端）。
        # 同时消掉 __main__ 两处双写：它们现在只调 set_connection，不带账户。
        self.status_bar.set_connection("已连接", ok=True)
        self._sync_account()
        self.bridge.start_refresh()
        # 登录后先拉一次运行模式（模式钮要显示当前状态）。
        self.bridge.refresh_run_mode()
        # 首次进页面时 set_topics 可能因未连接而跳过，这里补一次。
        if self.pages.get(self.current) is not None:
            self._apply_topics()
        self.status_bar.set_message("已连接服务端")

    def _on_disconnected(self, reason):
        self.status_bar.set_connection("未连接", ok=False)
        self._sync_account()             # 退出/断线 ⇒ 账户回「未登录」
        self.status_bar.set_message(reason)
        # 断线后清订阅记录（重连时重新订）。
        self.bridge.stop_refresh()

    # ==================================================================
    #                        窗口尺寸/形态
    # ==================================================================
    def set_compact_override(self, force):
        """强制紧凑模式（本地设置「紧凑模式」勾选；None=恢复按窗宽自动）。

        [为什么要有覆盖] 本窗原来只按窗宽自动判（<COMPACT_MAX_W），桌面
        用户想把小窗也按紧凑形态排时没法强制；偏好已由本地设置页落盘。
        """
        self._compact_force = None if force is None else bool(force)
        self._apply_compact(self.width() < theme.COMPACT_MAX_W)

    def _apply_compact(self, auto_compact):
        compact = (auto_compact if self._compact_force is None
                   else bool(self._compact_force))
        if compact == self._compact:
            return
        self._compact = compact
        self.nav.set_compact(compact)
        # 窄屏收起导航（由主菜单页承载导航）。
        if compact:
            self.nav.setVisible(False)
        else:
            self.nav.setVisible(True)
        self.status_bar.set_message(
            "紧凑模式（%dpx）" % self.width() if compact else "宽屏模式")

    def resizeEvent(self, event):
        QMainWindow.resizeEvent(self, event)
        self._apply_compact(self.width() < theme.COMPACT_MAX_W)
