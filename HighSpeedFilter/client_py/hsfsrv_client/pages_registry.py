# -*- coding: utf-8 -*-
"""页面注册表：页索引 / 页名 / 导航树（与 C 端 HsfPages.h 逐项对齐）。

**页序与导航树必须与 C 端完全一致** —— 这是「界面对齐」的一部分：
同一份操作手册、同一个页面编号要能对应上。

[2026-11-13 用户定案「三页合并」] 原 18 页里的「悬浮窗设置(16)」「窗口设置(17)」
两页内容并入「本地设置(13)」，页号 16/17 从注册表与导航树删除 ⇒ **16 页
（0..15）**。其余页号一律不变（页号是操作手册契约，不许重排）。

[2026-11-22 用户定案「水质与流量拆成两页」] 页 6 改为**水质**（两台浊度仪 +
浊度趋势），流量计/进水泵/流量趋势挪到**页 18「流量」**（页号**追加末尾、
不重排既有页**）。**C 端未拆**（仍是一页 HsfMonitorFlowMeter），本页仅
Python 客户端 + web 有 ⇒ 跨端页数差异多这一项。
"""

# ============================================================================
#                              页索引
# ============================================================================

PAGE_HOME = 0
PAGE_MENU = 1
PAGE_MONITOR_MENU = 2
PAGE_MONITOR_DIGITAL = 3
PAGE_MONITOR_ANALOG = 4
PAGE_MONITOR_VFD = 5
#: [2026-11-22 用户定案「水质与流量拆成两页」] 页 6 = **水质**
#: （两台浊度仪 + 浊度趋势）；页 18 = **流量**（电磁流量计 + 进水泵 +
#: 流量趋势）。页号**追加在末尾、不重排既有页**（页号是操作手册契约）。
PAGE_MONITOR_WATER = 6
#: 历史名兼容别名（旧代码/测试里 `PAGE_MONITOR_FLOWMETER` 指的就是
#: 「水质监测」那一页，页号 6）。新代码请用 `PAGE_MONITOR_WATER` /
#: `PAGE_MONITOR_FLOW`；别名保留只为不打断既有调用点。
PAGE_MONITOR_FLOWMETER = PAGE_MONITOR_WATER
PAGE_MONITOR_VALVE = 7
PAGE_DATA_MENU = 8
PAGE_DATA_HISTORY = 9
PAGE_DATA_LOG = 10
PAGE_DATA_ALARM = 11
PAGE_SETTINGS_MENU = 12
PAGE_SETTINGS_LOCAL = 13
PAGE_SETTINGS_REMOTE = 14
PAGE_SETTINGS_NETWORK = 15
#: [2026-11-13 用户定案「三页合并」] 原「悬浮窗设置(16) / 窗口设置(17)」两页
#: 的内容**全部并入** PAGE_SETTINGS_LOCAL（13），这两个页号从注册表与导航树
#: 删除。**其余页号一律不变**（页号是操作手册契约）。
#:
#: [2026-11-16 用户定案「网页服务拆成独立 Web 设置页」] 追加
#: **16 = Web 设置**（网页服务开关/端口 + 远程访问域名/端口 + 扫码直达）。
#: 页号**追加在末尾、不重排既有页**（0..15 的契约不变）。
#: **此页仅 Python 客户端有**：C 端不实现（用户口径「就只在 python 客户端
#: 实现」）⇒ 跨端同形断言的容许差异只此一项（锁在客户端回归 §34）。
PAGE_SETTINGS_WEB = 16
#: [2026-11-19 相机页] **17 = 相机**（设备组下）。页号**追加末尾、不重排**
#: （页号是操作手册契约）。**三端都有**（与「Web 设置」不同，那页只有
#: Python 客户端有）。
PAGE_MONITOR_CAMERA = 17
#: [2026-11-22 水质/流量拆分] **18 = 流量**（设备组下，页号追加末尾）。
#: 页 6 改为「水质」（只留两台浊度仪与浊度趋势），原页 6 里的电磁流量计、
#: 进水泵与流量趋势挪到本页。**C 端未拆**（C 端仍是单页 HsfMonitorFlowMeter）
#: ⇒ 本页仅 Python 客户端 + web 有；跨端页数差异多这一项（见回归 §B）。
PAGE_MONITOR_FLOW = 18
PAGE_COUNT = 19

#: 页名（标题栏/状态栏/导航同源）。与 C 端 kHsfPageNames 逐项一致。
PAGE_NAMES = [
    "主界面",
    "主菜单",
    "设备",
    "数字量",
    "模拟量",
    # [2026-11-05 现场口径] 这路设备驱动的是**搅拌泵**。页名按工艺用途叫
    # 「搅拌泵」，设备**类别**仍是变频器（首页"类型"列显示变频器）——
    # 操作员关心的是"哪个泵在转"，而不是"用的是什么调速器"。
    "搅拌泵",
    # [2026-11-22] 页 6 只留水质（浊度×2）；流量计/进水泵/流量趋势移到页 18。
    "水质",
    "电动球阀",
    "数据",
    "历史数据",
    "日志",
    "报警",
    "设置",
    "本地设置",
    "远端设置",
    "网络设置",
    # [2026-11-16] 仅 Python 客户端（C 端无此页，见 PAGE_SETTINGS_WEB 的注）。
    "Web 设置",
    # [2026-11-19] 相机页（设备组下；三端都有）。
    "相机",
    # [2026-11-22] 流量页（设备组下；页号 18，追加末尾，仅 Python 客户端
    # + web；C 端未拆）。
    "流量",
]

#: 导航树 (caption, page_index, child_of)。
#: page_index < 0 表示纯分组标题（不可点）；child_of 为父节点下标，-1 顶层。
#: 共享节点顺序与 C 端 kHsfNavTree 一致；本端另有「Web 设置」「相机」
#: 「流量」扩展项（C 端未实现，见各常量注释）。
NAV_TREE = [
    ("主界面",     PAGE_HOME,               -1),
    ("设备",       PAGE_MONITOR_MENU,       -1),
    ("数字量",     PAGE_MONITOR_DIGITAL,     1),
    ("模拟量",     PAGE_MONITOR_ANALOG,      1),
    ("搅拌泵",     PAGE_MONITOR_VFD,         1),
    ("水质",       PAGE_MONITOR_WATER,       1),
    # [2026-11-22 用户定案] 流量页插在水质之后、电动球阀之前；后续节点
    # （电动球阀/相机/数据组/设置组及其子项）的下标随之各后移一位。
    ("流量",       PAGE_MONITOR_FLOW,        1),
    ("电动球阀",   PAGE_MONITOR_VALVE,       1),
    # [2026-11-19 用户口径「相机界面应该放设备菜单下」] 相机条目挂设备组
    # **末位**（显示顺序随 NAV_TREE；页号 17 是追加契约不受影响）。
    ("相机",       PAGE_MONITOR_CAMERA,      1),
    ("数据",       PAGE_DATA_MENU,          -1),
    ("历史数据",   PAGE_DATA_HISTORY,        9),
    ("日志",       PAGE_DATA_LOG,            9),
    ("报警",       PAGE_DATA_ALARM,          9),
    ("设置",       PAGE_SETTINGS_MENU,      -1),
    ("本地设置",   PAGE_SETTINGS_LOCAL,     13),
    ("远端设置",   PAGE_SETTINGS_REMOTE,    13),
    ("网络设置",   PAGE_SETTINGS_NETWORK,   13),
    # [2026-11-16] 网页服务从本地设置拆出的独立页（仅 Python 客户端）。
    ("Web 设置",   PAGE_SETTINGS_WEB,       13),
]

#: 主菜单页的条目（C 端 HsfMainMenu 同款三大分类入口 + 主界面）。
#: [2026-11-22 用户定案「切换到菜单页后主界面没有在菜单上打不开了」]
#: 菜单页模式会隐藏左侧导航树 ⇒ 主菜单必须自带「主界面」入口，否则
#: 回不到首页（导航树档不受影响，多一个入口也可直达）。
MAIN_MENU_ENTRIES = [
    ("主界面", PAGE_HOME, "\u2302"),            # ⌂
    ("设备", PAGE_MONITOR_MENU, "\u25A4"),     # ▤
    ("数据", PAGE_DATA_MENU, "\u2263"),        # ≣
    ("设置", PAGE_SETTINGS_MENU, "\u2699"),    # ⚙
]

#: 各子菜单页的条目（与导航树子节点同源）。
SUBMENU_ENTRIES = {
    PAGE_MONITOR_MENU: [
        ("数字量", PAGE_MONITOR_DIGITAL),
        ("模拟量", PAGE_MONITOR_ANALOG),
        ("搅拌泵", PAGE_MONITOR_VFD),
        ("水质", PAGE_MONITOR_WATER),
        # [2026-11-22] 流量页（页 18，追加末尾）。
        ("流量", PAGE_MONITOR_FLOW),
        ("电动球阀", PAGE_MONITOR_VALVE),
        # [2026-11-19] 相机页。
        ("相机", PAGE_MONITOR_CAMERA),
    ],
    PAGE_DATA_MENU: [
        ("历史数据", PAGE_DATA_HISTORY),
        ("日志", PAGE_DATA_LOG),
        ("报警", PAGE_DATA_ALARM),
    ],
    PAGE_SETTINGS_MENU: [
        ("本地设置", PAGE_SETTINGS_LOCAL),
        ("远端设置", PAGE_SETTINGS_REMOTE),
        ("网络设置", PAGE_SETTINGS_NETWORK),
        # [2026-11-16] 仅 Python 客户端（C 端无此页）。
        ("Web 设置", PAGE_SETTINGS_WEB),
    ],
}


def page_name(index):
    if 0 <= index < len(PAGE_NAMES):
        return PAGE_NAMES[index]
    return "?"


def category_of(index):
    """取该页所属的顶层分类（用于状态栏/面包屑）。"""
    for _caption, page, child_of in NAV_TREE:
        if page == index:
            if child_of < 0:
                return page
            for _c2, p2, c2 in NAV_TREE:
                if c2 < 0 and NAV_TREE.index((_c2, p2, c2)) == child_of:
                    return p2
    return -1


def menu_page_for(index):
    """☰「菜单页」模式要打开的页：当前页所属**分类的子菜单页**。

    · 设备/数据/设置下的页（如数字量）⇒ 对应子菜单页（「设备 选择子项」）；
    · 已在子菜单页（2/8/12）⇒ 主菜单页 1（再点 ☰ 是"返回上一级"）；
    · 主界面/主菜单/未知页 ⇒ 主菜单页 1。
    """
    if index in SUBMENU_ENTRIES:
        return PAGE_MENU
    cat = category_of(index)
    if cat in SUBMENU_ENTRIES:
        return cat
    return PAGE_MENU

