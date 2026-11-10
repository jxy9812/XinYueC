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

    # [退出即记窗口形态 2026-11-10] 用户没点「窗口设置→应用」、只是拖大了
    # 窗口的话，上次尺寸也该沿用（C 端 HsfSettingsStore 同口径）。
    # 全屏态记 fullscreen 标志，普通态记宽高。
    try:
        from . import settings as ui_settings
        if win.isFullScreen():
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
    win.close()
    QTimer.singleShot(0, app.quit)
    for _ in range(50):
        app.processEvents()
        time.sleep(0.02)


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
    from .login import LoginDialog
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

    # 悬浮窗设置（标题栏/状态栏/导航显隐）：启动还原，与设置页同一套判定。
    from .pages_other import apply_overlay
    apply_overlay(win, ui_settings)

    # ---- 登录（四条路径）----
    if args.auto:
        # ① 显式指定了地址：无头登录（CI/联调）
        from .xlink import DEFAULT_PORT
        port = args.port or DEFAULT_PORT
        ok, err = bridge.connect_to(args.auto, port, args.user, args.password)
        if not ok:
            print("登录失败：%s" % err, file=sys.stderr)
            _teardown(app, win, bridge)
            del win, bridge
            return 2
        win.status_bar.set_connection("已连接 · %s" % args.user, ok=True)
    elif args.offline or args.shot or args.seconds:
        # ② 截图/CI/离线浏览：不弹登录窗
        pass
    elif not args.login and probe_local_server(args.port) is not None:
        # ③ **探测到本地服务端 → 用 root 直连，跳过登录窗**
        #
        # [为什么要这条] 本机跑着服务端时，客户端启动就该直接看数据；
        # 让现场先填一遍地址/口令纯属多余（板端场景尤其明显：客户
        # 开机只关心界面，不该看到登录框）。
        # 用 root 是因为首次启动的缺省账户就是 root/root（服务端
        # HsfServerSettings 建的），本机场景没有安全顾虑。
        port = probe_local_server(args.port)
        print("探测到本地服务端（127.0.0.1:%d），自动登录" % port,
              file=sys.stderr)
        ok, err = bridge.connect_to("127.0.0.1", port, "root", "root")
        if ok:
            win.status_bar.set_connection("已连接 · root", ok=True)
        else:
            # [失败要退回登录窗] 探测到端口不等于能登进去（可能是 C 版
            # 服务端、账户库不同、或 root 已被别的会话占用）。这时不能
            # 直接退出，否则用户面对一个空白界面无从下手 —— 退回登录窗
            # 让人换凭据/换地址。
            print("本地服务端自动登录失败（%s），回退登录窗" % err,
                  file=sys.stderr)
            dlg = LoginDialog(bridge, parent=win)
            dlg.setWindowModality(Qt.ApplicationModal)
            if dlg.exec_() != LoginDialog.Accepted:
                _teardown(app, win, bridge)
                del win, bridge
                return 0
            if dlg.error:
                _teardown(app, win, bridge)
                del win, bridge
                return 2
    else:
        # ④ 没探测到本地服务端 → 弹登录窗（大概要连别的机器）
        # [C 端同款] 登录窗以主窗为父 + 应用模态，覆盖在主界面上居中。
        dlg = LoginDialog(bridge, parent=win)
        dlg.setWindowModality(Qt.ApplicationModal)
        if dlg.exec_() != LoginDialog.Accepted:
            # 用户关掉登录窗（点 X 或取消）→ 退出，不留一个不可用的主窗
            _teardown(app, win, bridge)
            del win, bridge
            return 0
        if dlg.error:
            _teardown(app, win, bridge)
            del win, bridge
            return 2

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
