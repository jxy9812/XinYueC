# -*- coding: utf-8 -*-
"""页面注册表：页索引 / 页名 / 导航树（与 C 端 HsfPages.h 逐项对齐）。

**页序与导航树必须与 C 端完全一致** —— 这是「界面对齐」的一部分：
同一份操作手册、同一个页面编号要能对应上。

[2026-11-13 用户定案「三页合并」] 原 18 页里的「悬浮窗设置(16)」「窗口设置(17)」
两页内容并入「本地设置(13)」，页号 16/17 从注册表与导航树删除 ⇒ **16 页
（0..15）**。其余页号一律不变（页号是操作手册契约，不许重排）。
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
PAGE_MONITOR_FLOWMETER = 6
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
PAGE_COUNT = 18

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
    "水质监测",
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
]

#: 导航树 (caption, page_index, child_of)。
#: page_index < 0 表示纯分组标题（不可点）；child_of 为父节点下标，-1 顶层。
#: 与 C 端 kHsfNavTree **完全一致**（含顺序）。
NAV_TREE = [
    ("主界面",     PAGE_HOME,               -1),
    ("设备",       PAGE_MONITOR_MENU,       -1),
    ("数字量",     PAGE_MONITOR_DIGITAL,     1),
    ("模拟量",     PAGE_MONITOR_ANALOG,      1),
    ("搅拌泵",     PAGE_MONITOR_VFD,         1),
    ("水质监测",   PAGE_MONITOR_FLOWMETER,   1),
    ("电动球阀",   PAGE_MONITOR_VALVE,       1),
    # [2026-11-19 用户口径「相机界面应该放设备菜单下」] 相机条目挂设备组
    # **末位**（显示顺序随 NAV_TREE；页号 17 是追加契约不受影响）。
    ("相机",       PAGE_MONITOR_CAMERA,      1),
    ("数据",       PAGE_DATA_MENU,          -1),
    ("历史数据",   PAGE_DATA_HISTORY,        6),
    ("日志",       PAGE_DATA_LOG,            6),
    ("报警",       PAGE_DATA_ALARM,          6),
    ("设置",       PAGE_SETTINGS_MENU,      -1),
    ("本地设置",   PAGE_SETTINGS_LOCAL,     10),
    ("远端设置",   PAGE_SETTINGS_REMOTE,    10),
    ("网络设置",   PAGE_SETTINGS_NETWORK,   10),
    # [2026-11-16] 网页服务从本地设置拆出的独立页（仅 Python 客户端）。
    ("Web 设置",   PAGE_SETTINGS_WEB,       10),
]

#: 主菜单页的条目（C 端 HsfMainMenu 同款：三大分类入口）。
MAIN_MENU_ENTRIES = [
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
        ("水质监测", PAGE_MONITOR_FLOWMETER),
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
