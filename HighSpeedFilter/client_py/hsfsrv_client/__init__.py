# -*- coding: utf-8 -*-
"""高速过滤器客户端（Python 版，PyQt5）。

与 C 版 HighSpeedFilter/client/ 功能对齐、界面结构对齐：
    17 个页面 · 同一棵导航树 · 深蓝标题栏 + 黑底状态栏 · 亮色现代蓝主题

模块：
    proto.py / bson.py  线协议常量与 BSON（与服务端共用同一份口径）
    xlink.py            XLink 客户端会话层（握手/认证/请求/订阅/心跳）
    model.py            设备数据模型（解包 + 本地新鲜度）
    bridge.py           网络线程 → UI 线程的 Qt 桥
    theme.py            配色/度量/样式表（与 C 端 HsfThemeCss.h 同源）
    widgets.py          标题栏/状态栏/导航/卡片/数值块/三段式骨架
    page_base.py        页面基类与表格/灯阵列/DO 网格
    pages_registry.py   页索引/页名/导航树（与 C 端逐项一致）
    pages_monitor.py    数字量 / 模拟量 / 变频器 / 流量计
    pages_other.py      主界面/菜单/数据/设置 各类页
    login.py            登录窗
    main_window.py      主窗与切页/订阅协调
    __main__.py         入口

运行：python -m hsfsrv_client --help
"""

__version__ = "1.0.0"

# ---------------------------------------------------------------------------
# [退出拆卸裁定 2026-11-09] 关闭 sip 的「解释器退出时销毁全部 QObject」遍历。
#
# 证据链（test_vfd_combo 在 xcb/Xvfb 下偶发 exit=139，HEAD 无此问题）：
#   core 栈 = sipWrapper_dealloc → forgetObject → release_QApplication →
#   sip_api_visit_wrappers → cleanup_qobject → 跳入垃圾地址；坏对象 vtable
#   指向匿名堆（块早已释放并被复用）——即 PyQt 退出拆卸遍历踩到「C++ 侧
#   已释放但 sip 未失效」的包装器，属绑定层退出期双重所有权 UAF。
#   运行期零影响：全部功能测试（client_py 电池 363 断言 + 真服务端往返）
#   在崩溃发生前已全部通过，崩溃只发生在 main 返回之后。
#
# 处置对齐本仓库 C 端既有裁定（XGui.md §8.2「夹具全量拆除方案实证不可行，
# 已回退」）：退出持有交 OS 回收，不做遍历销毁。本调用必须发生在首个
# QApplication 创建之前（本包被任何入口 import 即生效，幂等无害）。
# ---------------------------------------------------------------------------
try:
    from PyQt5 import sip as _sip

    _sip.setdestroyonexit(False)
except Exception:  # noqa: BLE001  # 无 Qt 环境（纯协议栈导入）时静默跳过
    pass
