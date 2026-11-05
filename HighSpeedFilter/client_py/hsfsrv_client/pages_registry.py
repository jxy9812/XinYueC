# -*- coding: utf-8 -*-
"""页面注册表：页索引 / 页名 / 导航树（与 C 端 HsfPages.h 逐项对齐）。

**页序与导航树必须与 C 端完全一致** —— 这是「界面对齐」的一部分：
同一份操作手册、同一个页面编号要能对应上。
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
PAGE_SETTINGS_OVERLAY = 16
PAGE_SETTINGS_WINDOW = 17
PAGE_COUNT = 18

#: 页名（标题栏/状态栏/导航同源）。与 C 端 kHsfPageNames 逐项一致。
PAGE_NAMES = [
    "主界面",
    "主菜单",
    "监视",
    "数字量",
    "模拟量",
    # [2026-11-05 现场口径] 这路设备驱动的是**搅拌泵**。页名按工艺用途叫
    # 「搅拌泵」，设备**类别**仍是变频器（首页"类型"列显示变频器）——
    # 操作员关心的是"哪个泵在转"，而不是"用的是什么调速器"。
    "搅拌泵",
    "水质监测",
    "电磁阀",
    "数据",
    "历史数据",
    "日志",
    "报警",
    "设置",
    "本地设置",
    "远端设置",
    "网络设置",
    "悬浮窗设置",
    "窗口设置",
]

#: 导航树 (caption, page_index, child_of)。
#: page_index < 0 表示纯分组标题（不可点）；child_of 为父节点下标，-1 顶层。
#: 与 C 端 kHsfNavTree **完全一致**（含顺序）。
NAV_TREE = [
    ("主界面",     PAGE_HOME,               -1),
    ("监视",       PAGE_MONITOR_MENU,       -1),
    ("数字量",     PAGE_MONITOR_DIGITAL,     1),
    ("模拟量",     PAGE_MONITOR_ANALOG,      1),
    ("搅拌泵",     PAGE_MONITOR_VFD,         1),
    ("水质监测",   PAGE_MONITOR_FLOWMETER,   1),
    ("电磁阀",     PAGE_MONITOR_VALVE,       1),
    ("数据",       PAGE_DATA_MENU,          -1),
    ("历史数据",   PAGE_DATA_HISTORY,        6),
    ("日志",       PAGE_DATA_LOG,            6),
    ("报警",       PAGE_DATA_ALARM,          6),
    ("设置",       PAGE_SETTINGS_MENU,      -1),
    ("本地设置",   PAGE_SETTINGS_LOCAL,     10),
    ("远端设置",   PAGE_SETTINGS_REMOTE,    10),
    ("网络设置",   PAGE_SETTINGS_NETWORK,   10),
    ("悬浮窗设置", PAGE_SETTINGS_OVERLAY,   10),
    ("窗口设置",   PAGE_SETTINGS_WINDOW,    10),
]

#: 主菜单页的条目（C 端 HsfMainMenu 同款：三大分类入口）。
MAIN_MENU_ENTRIES = [
    ("监视", PAGE_MONITOR_MENU, "\u25A4"),     # ▤
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
        ("电磁阀", PAGE_MONITOR_VALVE),
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
        ("悬浮窗设置", PAGE_SETTINGS_OVERLAY),
        ("窗口设置", PAGE_SETTINGS_WINDOW),
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
