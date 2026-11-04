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
