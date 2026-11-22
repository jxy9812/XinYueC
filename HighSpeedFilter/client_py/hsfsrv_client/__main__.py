# -*- coding: utf-8 -*-
"""客户端入口。

    python -m hsfsrv_client                          登录窗 → 主窗
    python -m hsfsrv_client --auto 192.168.0.10      无头登录（CI/联调）
    python -m hsfsrv_client --offline                离线浏览（不连服务端）
    python -m hsfsrv_client --shot out.png --page 5  离屏截图（验收用）
"""

import argparse
import os
import sys
import time

# [双击兜底] 同 server 侧：直接运行本文件时 __package__ 为空，相对导入会抛
# ImportError 并秒退（窗口一闪而过）。先加 sys.path 再以包方式重入；
# 重入后 __package__ == "hsfsrv_client"，不会递归。
if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    from hsfsrv_client.__main__ import main as _entry
    sys.exit(_entry())

from . import ports


def _apply_saved_power_policy(ui_settings):
    """启动时把本机存档的「屏幕休眠」策略重新下发给平台。

    [为什么必须在启动时下发] 平台侧的电源策略是**进程内影子值**
    （XPlatformPower_policy 回读的是本应用设定值），进程重启即丢 ⇒
    不重新下发就等于「设了 5 分钟自动熄屏、重启就没了」= 选了不兑现。

    [失败口径] 动态库缺失 / 平台无电源能力 ⇒ 静默跳过（本地设置页的
    「窗口与屏幕」分区会把该区置灰并说明原因），**不阻断启动**。
    """
    blank = ui_settings.get_int(ui_settings.K_POWER_SCREEN_OFF_MS, 0)
    suspend = ui_settings.get_int(ui_settings.K_POWER_SUSPEND_MS, 0)
    wake = ui_settings.get_int(ui_settings.K_POWER_WAKE_INPUTS, 0)
    if not (blank or suspend or wake):
        return
    try:
        from . import hsf_native
        if not hsf_native.available():
            return
        bridge = hsf_native.bridge()
        if not bridge.power_capabilities():
            return
        bridge.power_set_policy(blank, suspend, wake)
    except Exception:                                   # noqa: BLE001
        pass


def _install_power_monitor(app):
    """装上「电源巡检 + 用户活动上报」，让「自动熄屏时长」真能兑现。

    [为什么必须有] 框架把「空闲多久做什么」的判定放在
    `XPlatformPower_tick()`，并明确要求**由应用层的周期定时器调用**
    （C 端是 XGuiApplication 的 1Hz 定时器，
    `Src/XGui/Application/XGuiApplication.c:120`）。桌面 Python 客户端
    不跑 XGuiApplication ⇒ 没人调它 ⇒ 「自动熄屏 5 分钟」选了不兑现。
    同理，框架的空闲基线靠 `XPlatformPower_noteActivity()` 打点，
    熄屏后「哪些输入能唤醒」也靠它。

    [做法] ① 1Hz 定时器 → `hsb_power_tick()`；② 应用级事件过滤器把
    鼠标/键盘/触摸事件折成输入位集 → `hsb_power_note_activity()`。
    判定逻辑全在框架内，这里只做**驱动**（不重搓空闲计时）。

    [缺库 / 无能力] 直接不装（本地设置页的「窗口与屏幕」分区把该区置灰
    并说明原因），**不阻断启动**。

    :return: 装好的过滤器对象（保持引用防被回收）；未装返回 None。
    """
    try:
        from PyQt5.QtCore import QEvent, QObject, QTimer

        from . import hsf_native
        if not hsf_native.available():
            return None
        bridge = hsf_native.bridge()
        if not bridge.power_capabilities():
            return None
    except Exception:                                   # noqa: BLE001
        return None

    # 事件类型 → 输入位集（与框架 XPlatformPowerInputMask 同值）。
    mask_of = {
        QEvent.MouseButtonPress: hsf_native.INPUT_MOUSE,
        QEvent.MouseButtonRelease: hsf_native.INPUT_MOUSE,
        QEvent.MouseMove: hsf_native.INPUT_MOUSE,
        QEvent.Wheel: hsf_native.INPUT_MOUSE,
        QEvent.KeyPress: hsf_native.INPUT_KEYBOARD,
        QEvent.KeyRelease: hsf_native.INPUT_KEYBOARD,
        QEvent.TouchBegin: hsf_native.INPUT_TOUCH,
        QEvent.TouchUpdate: hsf_native.INPUT_TOUCH,
        QEvent.TouchEnd: hsf_native.INPUT_TOUCH,
    }

    class _ActivityFilter(QObject):
        """把用户输入事件喂给框架的空闲打点（熄屏后据此唤醒）。"""

        def __init__(self, native_bridge):
            QObject.__init__(self)
            self._bridge = native_bridge
            self._last = 0.0

        def eventFilter(self, obj, event):
            mask = mask_of.get(event.type())
            if mask:
                # 节流到 0.5s 一次：空闲判定本身是秒级，且鼠标移动事件
                # 频率高（可达百 Hz），逐事件进 C 没必要。
                now = time.monotonic()
                if now - self._last >= 0.5:
                    self._last = now
                    self._bridge.power_note_activity(mask)
            return False                                   # 不吞事件

    filt = _ActivityFilter(bridge)
    app.installEventFilter(filt)

    timer = QTimer(app)
    timer.setInterval(1000)
    timer.timeout.connect(bridge.power_tick)
    timer.start()
    # [必须留住引用] installEventFilter **不**持 Python 引用：过滤器对象被
    # 回收后事件过滤会静默失效（活动不再打点 ⇒ 空闲计时不重置）。挂在 app
    # 上与 QApplication 同生命周期。
    app._hsf_power_filter = filt                          # noqa: SLF001
    app._hsf_power_timer = timer                          # noqa: SLF001
    return filt


def _uninstall_power_monitor(app):
    """摘掉电源巡检的**事件过滤器**（退出路径必须调）。

    [为什么必须摘 2026-11-21] `QApplication.installEventFilter()` **不持
    Python 引用**（我们另挂了一份防被 GC），但**Qt 侧一直挂着它** ⇒ 进程
    退出时 QApplication 析构，会在**事件派发链**上仍回调这个过滤器 ⇒ 此时
    Python 解释器已经在收尾（模块/类对象可能已销毁）⇒ 调用已销毁的 Python
    对象 ⇒ **0xC0000005 访问违例**（实测：客户端总回归的 nativecap 段组
    子进程偶发崩溃 exit=3221225477，单跑该段 55/0 复现不出 ⇒ 典型
    「退出期 UAF」）。
    ⇒ 退出前**主动摘除**并让 Qt 事件循环处理 deleteLater ⇒
    析构时 Qt 侧已没有这个过滤器。
    """
    if app is None:
        return
    filt = getattr(app, "_hsf_power_filter", None)
    timer = getattr(app, "_hsf_power_timer", None)
    if timer is not None:
        try:
            timer.stop()
        except Exception:                               # noqa: BLE001
            pass
    if filt is not None:
        try:
            app.removeEventFilter(filt)
        except Exception:                               # noqa: BLE001
            pass
        try:
            filt.deleteLater()
        except Exception:                               # noqa: BLE001
            pass
    try:
        app._hsf_power_filter = None                     # noqa: SLF001
        app._hsf_power_timer = None                      # noqa: SLF001
    except Exception:                                   # noqa: BLE001
        pass
    # 让 deleteLater 真正落地（否则它排在队列里，等 app 析构时还没执行）。
    try:
        app.processEvents()
    except Exception:                                   # noqa: BLE001
        pass


def parse_args(argv):
    ap = argparse.ArgumentParser(prog="hsfsrv_client",
                                 description="高速过滤器客户端（PyQt5）")
    ap.add_argument("--auto", default=None, metavar="HOST",
                    help="无头登录：直接连 host（用户名/口令见 --user/--pass）")
    ap.add_argument("--port", type=int, default=None,
                    help="端口（缺省 %d；C 版服务端用 %d）"
                         % (ports.PY_SERVER_PORT, ports.C_SERVER_PORT))
    ap.add_argument("--user", default="root")
    ap.add_argument("--pass", dest="password", default="root")
    ap.add_argument("--offline", action="store_true",
                    help="离线浏览：不连服务端，只看界面")
    ap.add_argument("--login", action="store_true",
                    help="强制弹登录窗（不自动探测本地服务端）")
    ap.add_argument("--page", type=int, default=None,
                    help="启动后切到指定页索引（0..16）")
    ap.add_argument("--shot", default=None, metavar="PATH",
                    help="渲染后截图存为 PATH 并退出（验收用）")
    ap.add_argument("--size", default=None, metavar="WxH",
                    help="窗口尺寸，如 1280x800")
    ap.add_argument("--fullscreen", action="store_true", help="全屏")
    ap.add_argument("--seconds", type=int, default=0,
                    help=">0 时运行 N 秒自动退出（CI）")
    return ap.parse_args(argv)


def _parse_size(text):
    if not text:
        return None
    try:
        w, h = text.lower().split("x")
        return int(w), int(h)
    except (ValueError, AttributeError):
        return None


def _start_web_server(bridge):
    """带起网页端（上游 = 客户端连的那台服务端）。

    [为什么网页端连的是服务端而不是客户端] 网页端自己拿 root 主链路连上
    游、浏览器再各自登录上游 ⇒ 网页与客户端看到的是同一份数据。

    :returns: (ok, reason)；ok=False 时 reason 是人话原因（状态行也会显示）。
    """
    from . import settings as ui_settings
    from . import webserver
    if not ui_settings.web_enabled():
        return True, ""
    client = getattr(bridge, "client", None)
    host = str(getattr(client, "host", "") or "127.0.0.1")
    try:
        # [端口单一事实源] 缺省走 ports.C_SERVER_PORT —— 不在代码里写死
        # 端口字面量（tests/test_ports.py 的纪律）。
        from .ports import C_SERVER_PORT
        port = int(getattr(client, "port", 0) or C_SERVER_PORT)
    except Exception:                                   # noqa: BLE001
        port = int(C_SERVER_PORT)
    # [形态 2026-11-17 用户定案「不要内嵌，客户端拉起也是独立的进程」]
    #   网页端一律**独立子进程**；客户端退出时要不要停它 ⇒ 由
    #   `webStopWithClient` 开关决定（见 _teardown）。
    # [清上次残留 2026-11-17] 客户端上次被强杀 ⇒ 网页端子进程变孤儿占着
    # 端口 ⇒ 启动时先认领并清掉（**只在这一刻**做：运行期起停不清理，
    # 否则会误杀另一个并发 start 刚起的进程）。
    try:
        webserver.stop_stale()
    except Exception:                                   # noqa: BLE001
        pass
    ok, reason = webserver.start(host, port, ui_settings.web_port())
    print("网页服务：%s" % webserver.status_text(), file=sys.stderr)
    return ok, reason


def _teardown(app, win, bridge):
    """统一收尾：断开 → 关窗 → 泵事件（**正常路径与失败路径共用**）。

    [为什么失败路径也必须走这套] main() 返回时局部变量的释放顺序是不
    定的，QApplication 可能先于窗口析构 —— 板端 xcb 实测直接段错误
    （systemd 记 `Main process exited, code=killed, status=11/SEGV`）。
    登录被拒（账户已在线/会话数上限）时若只 `win.close(); return 2`，
    每失败一次就崩一次；被 `Restart=on-failure` 拉起来就是「崩—重启—
    再崩」循环。故与正常路径同款三闸收尾，失败路径同样要走。

    @note 桥的断开放在最前：断网线程可能还在往 Qt 层 emit（见 bridge
          的 `_emit_net` 闸），先断网再关窗才不会在关窗过程中被回调打到。
    """
    from PyQt5.QtCore import QTimer

    # [退出即记窗口形态 2026-11-10] 用户没点「本地设置→窗口与屏幕→
    # 尺寸→应用」、只是拖大了窗口的话，上次尺寸也该沿用（C 端
    # HsfSettingsStore 同口径）。
    # 全屏态记 fullscreen 标志，普通态记宽高。
    try:
        from . import settings as ui_settings
        # [无人值守路径不写本机设置 2026-11-20 实锅] --shot/--seconds 的
        # 验收跑会带 --size 1280x4200 这类尺寸 ⇒ 无条件保存把**真实客户端
        # 的恢复尺寸**污染成"页高 4200"（窗口被系统钳到 749，页面在滚动壳
        # 里 4200 ⇒ 首页弹层"居中"落进视口外 ⇒ 点设备健康"弹不出来"）。
        # ⇒ 离屏/限时跑一律不落窗口形态（幂等：不写 = 沿用上次）。
        headless = bool(getattr(args, "shot", None)) or bool(args.seconds)
        if headless:
            pass
        elif win.isFullScreen():
            ui_settings.set_value(ui_settings.K_FULLSCREEN, True)
        else:
            ui_settings.set_value(ui_settings.K_FULLSCREEN, False)
            sz = win.size()
            if sz.width() >= 400 and sz.height() >= 300:
                ui_settings.set_value(ui_settings.K_WIN_SIZE,
                                      "%dx%d" % (sz.width(), sz.height()))
    except Exception:                                   # noqa: BLE001
        pass
    try:
        bridge.disconnect_from()
    except Exception:                                   # noqa: BLE001
        pass
    # [网页服务 2026-11-14] 退出前停网页端（它是本进程的后台线程 ⇒ 进程
    # 退出端口自然释放；这里显式停是为了让「退出」是干净收口，而不是等
    # 解释器收尾时线程被动中断）。
    # [2026-11-17 用户定案「可以设置客户端关闭的时候是否要同步关闭 web
    #  端」] 取消勾选 ⇒ 网页端是**独立子进程** ⇒ 客户端退出时**不停**它
    #  （它继续在后台服务；下次客户端起来能探测到）。
    try:
        from . import webserver
        from . import settings as ui_settings
        if ui_settings.web_stop_with_client():
            webserver.stop()
        else:
            print("网页服务：不同步关闭（仍在后台运行）", file=sys.stderr)
    except Exception:                                   # noqa: BLE001
        pass
    win.close()
    # [摘掉电源巡检的事件过滤器 2026-11-21] 见 `_uninstall_power_monitor`：
    # 不摘的话 QApplication 析构时仍会回调它 ⇒ 退出期访问已销毁的 Python
    # 对象 ⇒ 0xC0000005（实测 nativecap 段组子进程偶发崩溃）。
    try:
        _uninstall_power_monitor(app)
    except Exception:                                   # noqa: BLE001
        pass
    QTimer.singleShot(0, app.quit)
    for _ in range(50):
        app.processEvents()
        time.sleep(0.02)


def _prompt_login(bridge, win):
    """弹登录窗（**可重入**，供首次登录与退出/切换账户后重登共用）。

    :returns: (accepted: bool, err: str)；accepted=False = 用户取消/关窗。

    [装配顺序] 与 C 端 HsfUi.c 同款：主窗已先显示，登录窗以主窗为父 +
    应用模态 ⇒ 皮肤继承、按父客户区居中、盖在真实主界面上。
    [不在这里 _teardown] 退出登录/切换账户**不是退出进程**，取消登录窗
    只留一个"未登录"主窗让人再点账户按钮重试，绝不关窗退进程。
    """
    from PyQt5.QtCore import Qt
    from .login import LoginDialog
    dlg = LoginDialog(bridge, parent=win)
    dlg.setWindowModality(Qt.ApplicationModal)
    ok = dlg.exec_() == LoginDialog.Accepted
    return ok, (dlg.error or "")


def _run_initial_login(args, app, win, bridge):
    """启动时四条登录路径（--auto / 无头模式 / 本地探测 / 弹窗）。

    :returns: (code, fatal)；code=0 继续进事件循环，非 0 让 main() 直接
              返回并走 _teardown。

    [conn 双写已收拢] 这里只写「已连接」，账号·角色由 MainWindow 的
    `_sync_account()` 统一刷到状态栏账户按钮（main_window.py）。
    """
    from .xlink import DEFAULT_PORT

    if args.auto:
        # ① 显式指定了地址：无头登录（CI/联调）
        port = args.port or DEFAULT_PORT
        ok, err = bridge.connect_to(args.auto, port, args.user, args.password)
        if not ok:
            print("登录失败：%s" % err, file=sys.stderr)
            return 2, True
        win.status_bar.set_connection("已连接", ok=True)
        win._sync_account()
        return 0, False
    if args.offline or args.shot or args.seconds:
        # ② 截图/CI/离线浏览：不弹登录窗
        return 0, False
    if not args.login and probe_local_server(args.port) is not None:
        # ③ 探测到本地服务端 → 用 root 直连，跳过登录窗
        port = probe_local_server(args.port)
        print("探测到本地服务端（127.0.0.1:%d），自动登录" % port,
              file=sys.stderr)
        ok, err = bridge.connect_to("127.0.0.1", port, "root", "root")
        if ok:
            win.status_bar.set_connection("已连接", ok=True)
            win._sync_account()
            return 0, False
        # 失败要退回登录窗（可能是 C 版服务端/账户库不同/root 已被占）。
        print("本地服务端自动登录失败（%s），回退登录窗" % err,
              file=sys.stderr)
        accepted, dlg_err = _prompt_login(bridge, win)
        if not accepted:
            return 0, True
        if dlg_err:
            return 2, True
        return 0, False
    # ④ 没探测到本地服务端 → 弹登录窗
    accepted, dlg_err = _prompt_login(bridge, win)
    if not accepted:
        return 0, True
    if dlg_err:
        return 2, True
    return 0, False


def _relogin_after_logout(args, app, win, bridge, switch):
    """「退出登录 / 切换账户」的统一处理（在 UI 线程、由 QTimer 投递执行）。

    [顺序] 先 `bridge.logout()`（断链 + 清账户态 + 发 disconnected，
    MainWindow._on_disconnected 把 conn/账户刷成「未连接 / 未登录」），
    再弹登录窗。进程**不退出**、不走 `_teardown`。
    [为什么不直接在菜单槽里 exec_] 菜单本身处在嵌套事件循环
    （QMenu.exec_），同步再 exec_ 一个模态框风险大 ⇒ 由
    `QTimer.singleShot(0, ...)` 投递到下一轮事件循环（调研坑 3）。
    [同账并发] 退出后立刻用同一账号重登会撞服务端「该账户已在线」
    瞬时拒绝；登录窗会把原因显示出来，让用户稍后重试即可（不静默）。
    """
    bridge.logout()
    win.status_bar.set_message(
        "正在切换账户…" if switch else "已退出登录，请重新登录")
    accepted, err = _prompt_login(bridge, win)
    if accepted and not err:
        win.status_bar.set_connection("已连接", ok=True)
        win._sync_account()
        win.status_bar.set_message("已重新登录")
    else:
        # 取消/失败：保留「未登录」主窗，账户按钮随时可再点重试。
        win.status_bar.set_message("未登录（点右下角账户可重试）")


def probe_local_server(port=None, timeout=0.6):
    """探测本机是否已有服务端在监听。

    [为什么需要] 板端/同一台机器跑「服务端 + 客户端」时，客户端一启动就
    该直接进去看数据，而不是先让人填一遍地址口令。登录窗只在**探测不到
    本地服务端**时才弹（此时大概要连别的机器，必须人工输入）。

    探测口径：纯 TCP connect 到 127.0.0.1:<port>，握手都不做 ——
    只判断"这个端口有没有人在听"。**不做登录**，避免探测本身占掉
    服务端的单会话名额。

    :returns: 探测到的端口（int）或 None。
    """
    import socket
    from .ports import PY_SERVER_PORT
    candidates = []
    if port:
        candidates.append(int(port))
    else:
        # 先试本项目缺省端口（6200），再试 C 版端口（6100）——
        # 现场常两套并存，本机哪个在听就连哪个。
        from .ports import C_SERVER_PORT
        candidates.extend([PY_SERVER_PORT, C_SERVER_PORT])
    for candidate in candidates:
        try:
            sock = socket.create_connection(("127.0.0.1", candidate),
                                            timeout=timeout)
            sock.close()
            return candidate
        except Exception:                               # noqa: BLE001
            continue
    return None


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])

    from PyQt5.QtCore import Qt, QTimer
    from PyQt5.QtGui import QPixmap
    from PyQt5.QtWidgets import QApplication

    # 高 DPI 在板端（1x 屏）不需要，关掉避免缩放带来的坐标抖动。
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, False)

    app = QApplication(sys.argv[:1])
    app.setApplicationName("HsfClientPy")

    from . import theme
    # [本机界面缩放在安装主题**之前**生效 2026-11-10] QSS 与各页字号都是
    # 在 install / 建页时按 theme.ui_scale() 算好的（模块注释写明"求值发生
    # 在导入时"），运行期改比例等于要重建整棵树；故「界面缩放」只在本机
    # 设置里存档、启动时在这里覆盖。没存过（0）就保留环境变量/内置缺省。
    from . import settings as ui_settings
    # [旧注册表设置搬一次 2026-11-10] 设置改存程序目录 cfg/hsf_client_py.ini；
    # 首次启动把旧版注册表里的登录记忆搬过来并删掉，之后就只看文件。
    try:
        moved = ui_settings.migrate_from_legacy()
        if moved:
            print("设置：已从注册表迁移 %d 项到 %s"
                  % (moved, ui_settings.settings_path()), file=sys.stderr)
    except Exception:                                   # noqa: BLE001
        pass
    scale = ui_settings.ui_scale()
    if scale > 0:
        theme.set_ui_scale(scale)
    theme.install(app)
    # [全局基准字号] 板端是 1280x800 / 7.9 寸屏，pt 10 偏小；
    # 与 QSS 里散落的像素字号同一缩放口径（theme.ui_scale()，缺省 1.15）。
    family = theme.apply_font(app, size=int(round(10 * theme.ui_scale())))
    print("字体：%s（缩放 %.2fx）"
          % (family or "(系统缺省)", theme.ui_scale()), file=sys.stderr)

    from .bridge import LinkBridge
    from .main_window import MainWindow

    bridge = LinkBridge()

    # ==================================================================
    #                    窗口装配顺序（与 C 端对齐）
    # ==================================================================
    #
    # [C 端口径] HighSpeedFilter/client/ui/HsfUi.c：
    #   1. **先建并显示主窗**（皮肤/窗口设置在建窗时恢复完）
    #   2. 登录窗随后**以主窗为父**创建 → 天然继承皮肤、按父窗口客户区居中
    #   3. 登录窗是**应用模态**（ApplicationModal）→ 挡住主窗输入
    #   4. 登录成功后才收登录窗
    #
    # [为什么必须这个顺序]
    #   a) 登录窗先建、主窗后建的话，登录窗用的是默认皮肤，用户在
    #      "窗口设置"里选的皮肤要等登录完才生效 —— 观感割裂（C 端实锅）。
    #   b) 先显示主窗，用户看到的是"登录遮罩盖在真实主界面上"，
    #      而不是"一个孤零零的小框 + 背后空白"。
    #
    # [无头/截图路径] --auto / --offline / --shot / --seconds 都不弹登录窗，
    # 只建主窗（CI 与离屏截图需要确定性，弹模态框会卡住）。

    # ---- 主窗（先建先显）----
    win = MainWindow(bridge)
    size = _parse_size(args.size) or (theme.WIN_DEFAULT_W, theme.WIN_DEFAULT_H)
    # [启动还原本机设置 2026-11-10] 用户口径「可以持久化的配置都持久化」：
    # 命令行显式给的（--size/--fullscreen）优先；没给就按上次保存的来。
    if args.size is None:
        saved_w, saved_h = ui_settings.win_size()
        if saved_w > 0:
            size = (saved_w, saved_h)
    fullscreen = bool(args.fullscreen) or (
        args.size is None and ui_settings.get_bool(ui_settings.K_FULLSCREEN,
                                                   False))
    # 刷新节拍：主窗 refresh 定时器（本地设置页可改，重启沿用）
    win._refresh_timer.setInterval(ui_settings.refresh_ms())
    # 紧凑模式：本机设置里勾了就强制（不勾/未存 = 按窗宽自动）
    if ui_settings.get_bool(ui_settings.K_FORCE_COMPACT, False):
        win.set_compact_override(True)
    # [必须在 show 前 resize] MainWindow 内部的 resize 在部分窗口管理器下
    # 会被「首次映射」覆盖成 1x1（实测板上 xcb + xfwm4：窗口树里
    # HsfClientPy 只有 1x1，截图看不到界面）。这里先钉死尺寸再 show。
    win.resize(*size)
    win.setMinimumSize(640, 420)
    if fullscreen:
        win.showFullScreen()
        # [全屏后必须抢一次前台] xfwm4 在开机自启场景下不会把新窗口设为
        # 活动窗口：实测板端 `_NET_ACTIVE_WINDOW` 仍是 Thunar 的 Desktop，
        # 客户端窗口虽然 1280x800 映射着却被桌面盖住（截图看到的是文件夹）。
        # raise_ + activateWindow 在 show 之后立刻调没用（窗口还没 map 完），
        # 故用 0ms/300ms 两拍定时器：第一拍抢，第二拍兜底（xfwm4 迟到的
        # 重新堆叠会把它压下去）。
        def _grab_foreground():
            win.raise_()
            win.activateWindow()

        _grab_foreground()
        QTimer.singleShot(0, _grab_foreground)
        QTimer.singleShot(300, _grab_foreground)
    else:
        win.show()
        # show 之后再 resize 一次：xfwm4 有时在 map 时才应用几何。
        win.resize(*size)

    # 悬浮窗显隐（标题栏/状态栏/导航/时钟/温湿度）：启动还原，与设置页同一
    # 套判定（「本地设置」页的「悬浮窗」分区，2026-11-13 三页合并后）。
    from .pages_other import apply_overlay
    apply_overlay(win, ui_settings)

    # 屏幕休眠策略：启动时把本机存档的策略重新下发给平台（平台侧是进程内
    # 影子值，不下发就等于"设了不兑现"）。
    _apply_saved_power_policy(ui_settings)

    # 屏幕休眠的**驱动**（1Hz 巡检 + 用户活动打点）：框架把空闲判定放在
    # XPlatformPower_tick()，要求由应用层定时器调用；Python 客户端不跑
    # XGuiApplication ⇒ 这里补上，否则「自动熄屏时长」选了不兑现。
    _install_power_monitor(app)

    # ---- 登录（四条路径，抽成可重入函数）----
    code, fatal = _run_initial_login(args, app, win, bridge)
    if fatal:
        _teardown(app, win, bridge)
        del win, bridge
        return code

    # ---- 网页服务（2026-11-14）：交互式启动即带起网页端 ----
    # [为什么在这] 登录之后才知道上游到底是哪台服务端（网页端连的是**同一
    # 台**服务端，不是客户端）⇒ 此刻起最准。
    # [哪些路径不起] --auto（无头 CI）/ --offline / --shot / --seconds：
    # 无人值守的自动化运行里多一个网页服务没意义（还会占端口、留线程）。
    if not (args.auto or args.offline or args.shot or args.seconds):
        _start_web_server(bridge)

    # ---- 账户菜单：切换账户 / 退出登录（进程不退出）----
    # [QTimer.singleShot(0)] 菜单动作在 QMenu 的嵌套事件循环里发出，同步
    # 再 exec_ 登录窗有模态嵌套风险 ⇒ 投递到下一轮事件循环再执行。
    win.switchUserRequested.connect(
        lambda: QTimer.singleShot(
            0, lambda: _relogin_after_logout(args, app, win, bridge, True)))
    win.logoutRequested.connect(
        lambda: QTimer.singleShot(
            0, lambda: _relogin_after_logout(args, app, win, bridge, False)))

    if args.page is not None:
        QTimer.singleShot(300, lambda: win.goto_page(args.page))

    # ---- 离屏截图 ----
    if args.shot:
        def _grab():
            pix = QPixmap(win.size())
            win.render(pix)
            path = os.path.abspath(args.shot)
            if pix.save(path):
                print("截图：%s (%dx%d)" % (path, pix.width(), pix.height()),
                      file=sys.stderr)
            else:
                print("截图失败：%s" % path, file=sys.stderr)
            app.quit()
        # 等数据/布局稳定再截（页面切换 + 首帧推送需要时间）。
        QTimer.singleShot(2200, _grab)

    if args.seconds:
        QTimer.singleShot(args.seconds * 1000, app.quit)

    code = app.exec_()
    # [顺序化拆卸 2026-11-09] 与 tests/test_vfd_combo 同款：断开(闸网络线程
    # 发射)→关窗→泵循环消化 deleteLater→显式释放窗，再 return——return 时
    # 局部引用无序释放会让主窗级联析构跑到一半有处理器 emit 进半死接收方
    # （pyqtBoundSignal_emit 段错误，xcb 平台偶发，台账 §6.6）。
    _teardown(app, win, bridge)
    del win, bridge
    import gc as _gc

    _gc.collect()
    return code


if __name__ == "__main__":
    sys.exit(main())
