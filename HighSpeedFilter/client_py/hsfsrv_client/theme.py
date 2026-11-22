# -*- coding: utf-8 -*-
"""主题：与 C 端 HsfThemeCss.h 同源的一套样式表 + 配色常量。

[C 端的口径] C 端用的是 XGui 自己的样式表引擎，但**规则内容照 Qt 6.8 写**，
选择器是 XGui 的控件类名（XPushButton/XLineEdit…）。Qt 里对应
QPushButton/QLineEdit…，规则体可以**逐字照搬**——这也正是本项目能做到
「界面对齐」的基础。

色板（与 HsfThemeCss.h 的表逐项一致，WCAG 已核算过对比度）：
    主色锚    #3D8BFD
    强调填充  #2E7CE8   （输入类悬停描边）
    强调线    #1A66D0   （焦点/悬停/按下/选中描边）
    悬停底    #EAF2FF
    按压底    #DBE7FD
    控件底    #FFFFFF
    控件描边  #5A6572
    次级描边  #8B98A5
    软描边    #D5DBE2
    禁用底    #F0F2F5
"""

import os

from PyQt5.QtCore import QPointF, QRect, QRectF, Qt
from PyQt5.QtGui import (QColor, QImage, QFont, QPainter, QPainterPath,
                        QPalette,
                        QPen, QPolygonF)
from PyQt5.QtWidgets import (QAbstractSpinBox, QProxyStyle, QStyle,
                             QStyleFactory)

# ============================================================================
#                          品牌配色（chrome）
# ============================================================================

#: 顶栏深蓝底（同 C 端 HSF_COLOR_BRAND，也同 XGuiDemo 品牌条）。
COLOR_BRAND = "#1F4E79"
COLOR_BRAND_RGB = (0x1F, 0x4E, 0x79)
#: 底部状态栏黑底。
COLOR_STATUS_BG = "#000000"
#: 侧栏/导航面板底色（亮色主题的浅灰分层）。
COLOR_NAV_BG = "#F4F6F8"
COLOR_NAV_SEL = "#DBE7FD"
#: 页面底（比纯白略灰，让白色控件浮起来）。
COLOR_PAGE_BG = "#F4F6F8"

# ---- 控件色板 ----
CLR_WHITE = "#FFFFFF"
CLR_HOVER_BG = "#EAF2FF"
CLR_PRESS_BG = "#DBE7FD"
CLR_ACCENT_FILL = "#2E7CE8"
CLR_ACCENT_LINE = "#1A66D0"
CLR_BORDER = "#5A6572"
CLR_BORDER_SUB = "#8B98A5"
CLR_BORDER_SOFT = "#D5DBE2"
CLR_DISABLED_BG = "#F0F2F5"

# ---- 语义色（状态指示） ----
CLR_OK = "#2E7D32"        # 正常/运行（绿）
#: 「通」态的浅底色（禁用态下仍要看得见"通" ⇒ 浅绿底 + 深绿字/边）。
CLR_OK_SOFT = "#E3F1E4"
CLR_WARN = "#ED6C02"      # 告警（橙）
CLR_FAULT = "#C62828"     # 故障（红）
CLR_OFFLINE = "#757575"   # 离线（灰）
CLR_INFO = "#1A66D0"      # 信息（蓝）

# ============================================================================
#                倒计时表盘色板（RemainDial；与 C 端 homeThemeClr 同源）
# ============================================================================
#
# [2026-11-13 两端对齐] C 端 HsfHomePage.c 的 homeThemeClr(亮, 深) 按
# HsfTheme_isDarkSkin() 取两套；Python 端此前把表盘色写死在 paintEvent 里
# 且只有一套。现把令牌收进本模块单源，亮套逐值抄 C、深套登记 C 的深色
# 令牌（Python 客户端目前尚无皮肤开关，选择走 is_dark_skin()——机制
# 将来接入时表盘自动跟随，用色点不再改）。

#: 底盘整圆（C 亮套 0xFFF7F9FB / 深 HSF_COLOR_PANEL_DARK=#23282F）。
#: [2026-11-13 补底盘] Python 原先无底盘（缺口与环内透出卡片底色），
#: C 端画整圆底盘；两端对齐取「都画底盘」口径。
DIAL_DISC = "#F7F9FB"
DIAL_DISC_DARK = "#23282F"
#: 底轨（C 亮套 0xFFD5DBE2 / 深 HSF_COLOR_SOFT_BORDER_DARK=#39414C）。
#: [2026-11-13 改值] 原写 #E3E8EE 与 C 亮套不是同一灰（移植时没抄对），
#: 按 C 逐值订正。
DIAL_TRACK = "#D5DBE2"
DIAL_TRACK_DARK = "#39414C"
#: 中央小字（C 亮套 0xFF5A6572 / 深 HSF_COLOR_TEXT_SECONDARY_DARK=#A9B2BC）。
DIAL_CAPTION = "#5A6572"
DIAL_CAPTION_DARK = "#A9B2BC"
#: 中央读数（C 亮套 0xFF1A1A1A / 深 HSF_COLOR_TEXT_DARK=#E8EAED）。
DIAL_TEXT = "#1A1A1A"
DIAL_TEXT_DARK = "#E8EAED"
#: 信息蓝深套（C HOME_CLR_INFO 深套；亮套即 CLR_INFO=#1A66D0）。
DIAL_INFO_DARK = "#64B5F6"


def is_dark_skin():
    """当前是否深色皮肤（表盘等自绘面取色用）。

    [2026-11-14] 随主题走：深色主题（控制室）⇒ True；其余 ⇒ False。
    老的 ``HSF_UI_SKIN=dark`` 仍被 `_detect_theme_id()` 认（兼容）。
    """
    return bool(current_theme().get("dark"))


def dial_colors():
    """表盘当前皮肤的一套颜色（dict；RemainDial.paintEvent 每帧取）。"""
    return dict(current_theme().get("dial") or {})

# ============================================================================
#                          度量（与 C 端同源）
# ============================================================================

TITLE_BAR_H = 40          #: 顶栏高（C 端 HSF_TITLE_BAR_H）
STATUS_BAR_H = 28         #: 底部状态栏高
NAV_MIN_W = 120           #: 导航面板最小宽
NAV_DEFAULT_W = 176       #: 导航面板缺省宽
SPLITTER_HANDLE_W = 8     #: 分割条把手宽
#: 产品名（顶部居中标题 + 窗口标题；**2026-11-19 用户改名为
#: 「互嵌深层高速过滤机」**——前身 2026-11-12 定名的「PMMA有机玻璃高速
#: 过滤器」已作废）。
#: [为什么放 theme] 主窗/登录窗/标题栏三处共用一份，不各写一串。
APP_TITLE = "互嵌深层高速过滤机"

WIN_DEFAULT_W = 1280      #: 主窗缺省宽
WIN_DEFAULT_H = 720       #: 主窗缺省高
COMPACT_MAX_W = 600       #: 紧凑断点（逻辑宽 < 600 走竖屏形态）
NAV_ITEM_H = 40           #: 导航条目高
NAV_ITEM_H_MOBILE = 44    #: 移动态条目高（触控）

#: 表格行高（C 端 HsfDataTable 口径）。
TABLE_ROW_H = 32
#: 卡片圆角（C 端 3/4/5px 三档，卡片取 5px）。
RADIUS_CARD = 5
RADIUS_CTRL = 4
# ---- 现代化控件圆角（2026-11-18 Material Design 3 / 移动端口径）----
# [为什么独立于 RADIUS_CTRL] DO 切换按钮、DO 灯阵这些"方块语义"控件仍是
# 4px；输入类/按钮/菜单按移动端放大。三档各自成常量，QSS 经 _base_tokens
# 下发，改观感只动这里。
RADIUS_BTN = 18           #: 按钮：M3 全圆角（胶囊；QSS 自动收敛到半盒高）
RADIUS_INPUT = 8          #: 输入类（QLineEdit/QSpinBox/QComboBox）圆角
RADIUS_MENU = 8           #: 弹层（QMenu）容器圆角
#: 滚动条**命中区**宽（px）与把手圆角。
#:
#: [2026-11-19 用户实报「客户端的数据表格的滚动条有点小，触摸屏不好触摸
#:  拖动」] 原来命中区只有 8px —— 板端 1280×800 / 7.9 寸屏上 8px ≈
#: **1.2mm**，手指根本抓不住（跟"点不动的按钮"同一类问题）。
#: ⇒ 命中区加到 **22px**（≈3.4mm，可抓），**可见把手**用 margin 内缩
#: （视觉仍是细圆头，但手指能按到整条）；横条同理。
#: [可见宽度 vs 命中区] QSS 的 width 是命中区；把手用 margin 内缩 ⇒
#: "看起来细、按着宽"（移动端常见做法）。
SCROLL_W = 22
SCROLL_R = 8              #: 把手圆角（= 可见宽一半 ⇒ 圆头）
#: 勾选框/单选钮指示器边长（M3 触摸口径；由代理样式 pixelMetric 下发）。
INDICATOR_SIZE = 18

#: 数字量 DO 切换按钮的**垂直内边距**（px）。QSS 里
#: `QPushButton#HsfDoToggle` 的 padding 与按钮高度计算**同源**：
#: 高度 = 两行文字实测行高 + 2×DO_BTN_PAD_V + 2×DO_BTN_BORDER。
#: [为什么要有这个常量] 之前高度在 QSS 里写死 44~46px，缩放 1.5× 时
#: 内容区（46-8=38px）装不下两行 18px 文字（需 40px）⇒ 字被削掉。
DO_BTN_PAD_V = 4
#: DO 按钮的边框宽（px）。QPushButton 基础规则里是 `border: 1px solid`。
DO_BTN_BORDER = 1

# ============================================================================
#                          字体
# ============================================================================

#: 缺省字号（pt）。C 端走轮廓字库，Qt 用系统字体；板端装的是
#: fonts-wqy-zenhei（文泉驿正黑），中文可读性最好。
#: 族名候选——**只放真正含中文字形的字体**。
#: [实锅] 早先把 "DejaVu Sans" 也列进来：它在板端存在、但没有任何汉字，
#: 匹配成功后界面全是方框。中文字体找不到时就该走"按文件加载"那条路，
#: 不能退回一个"能匹配上但没汉字"的西文字体。
FONT_FAMILY_CANDIDATES = (
    "WenQuanYi Zen Hei",       # 板端（简体别名）
    "WenQuanYi Micro Hei",
    "文泉驿正黑",               # 板端（fontconfig 简体名）
    "文泉驛正黑",               # 板端（字体内部繁体族名）
    "Noto Sans CJK SC",
    "Source Han Sans CN",
    "Microsoft YaHei",         # Windows 开发机
    "SimHei",
)


#: 常见中文字体文件（板端/桌面）。找不到字体族时**直接加载文件**。
FONT_FILE_CANDIDATES = (
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",        # 板端
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "C:/Windows/Fonts/msyh.ttc",                           # Windows
    "C:/Windows/Fonts/simhei.ttf",
)


def pick_font_family():
    """选一个系统里真实存在的中文字体（避免方框乱码）。

    [为什么要两步] 板端装了 wqy-zenhei，但 Qt 的 families() 里**看不到它**
    （fc-list 有、Qt 没有）——fontconfig 没给 Qt 暴露该族的别名。
    故这里先按族名找，找不到再**按文件路径 addApplicationFont 注册**，
    注册成功后用它返回的族名。两步都失败才回落系统缺省。
    """
    from PyQt5.QtGui import QFontDatabase
    db = QFontDatabase()
    available = set(db.families())
    for name in FONT_FAMILY_CANDIDATES:
        if name in available:
            return name
    # 按文件加载（板端走这条）。
    # [实锅] wqy-zenhei.ttc 里注册的族名是**繁体**「文泉驛正黑」，而
    # families() 里能查到的简体名「文泉驿正黑」是 fontconfig 别名——
    # 用简体名去 setFont 会静默回落 DejaVu（中文变方框）。
    # 故这里一律用 addApplicationFont **返回的**族名，不猜名字。
    import os
    for path in FONT_FILE_CANDIDATES:
        if not os.path.isfile(path):
            continue
        font_id = QFontDatabase.addApplicationFont(path)
        if font_id < 0:
            continue
        fams = QFontDatabase.applicationFontFamilies(font_id)
        if not fams:
            continue
        # 优先选名字像「正黑/黑体」的常规体（避开等宽/点阵变体，
        # 那些在界面大字号下会显得毛糙）。
        for name in fams:
            if "等" in name or "Mono" in name:      # 等
                continue
            if "点" in name or "Sharp" in name:     # 点
                continue
            return name
        return fams[0]
    return ""            # 空 = Qt 用缺省，交给 fontconfig 兜底


def apply_font(app, size=10):
    """装全局字体（必须在建控件之前调用）。

    [2026-11-18·板端实测] 原来这里还给 app 级加一条
    `* { font-family: "%s"; }` 通配 QSS「兜底」（担心有些控件不吃
    app.setFont）。实测**完全多余**：
      · 板端（RK3568）与桌面各取两页做**逐像素**对照（远端设置/本地
        设置），去掉通配后渲染**完全一致**（差异 0 像素）⇒ 那条顾虑
        不成立（app.setFont 已经生效）；
      · 而 app 级通配 QSS 要**每个控件**都解析一遍样式表 ⇒ 板端建一
        页多花 ~270ms（远端设置页 1115ms 里的 24%）⇒ 切页卡顿的一大块
        （板端逐页建页总耗时 4.2s，这一项占 ~1s）。
    ⇒ 只保留 app.setFont（族名 + 字号），不再下发 app 级通配 QSS。
    """
    family = pick_font_family()
    font = QFont(family, size) if family else QFont()
    if not family:
        font.setPointSize(size)
    app.setFont(font)
    return family


# ============================================================================
#                        字号缩放（板端触摸屏可读性）
# ============================================================================
#
# [为什么需要全局缩放] 界面各处的字号是**分散硬编码**的 QSS 像素值
# （`font-size: 12px` 之类，散在 7 个文件约 45 处）。板端是 1280x800 的
# 7.9 寸屏（338x211mm），12px 字物理高度只有约 1.6mm，站在触摸屏前看着
# 明显偏小（用户 2026-11-06 反馈"字体有点小，可以尝试字体都加大"）。
#
# 改法：所有硬编码像素字号一律走 `font_px(n)`，由本模块的**单一比例**
# 统一放大。好处：① 一处改全局生效；② 小字与大字的相对层次保持不变
# （不会出现标题放大了、说明文字没跟上导致的比例失衡）。
#
# 环境变量 `HSF_UI_SCALE` 可覆盖（现场"嫌大/嫌小"不用改代码）：
#     HSF_UI_SCALE=1.0   原样
#     HSF_UI_SCALE=1.5   缺省（板端；2026-11-06 1.15 → 1.3 → 1.5，
#                        用户三次反馈"字有点小"，逐步提高）
# 真要更大直接给 1.7，不必改代码。
_UI_SCALE = None


def ui_scale():
    """当前界面字号缩放比例（带缓存；首次调用时读环境变量）。"""
    global _UI_SCALE
    if _UI_SCALE is None:
        raw = os.environ.get("HSF_UI_SCALE", "").strip()
        try:
            val = float(raw)
        except (TypeError, ValueError):
            val = 0.0
        if not 1.0 <= val <= 2.0:          # 非法/离谱值一律回缺省
            val = 1.5
        _UI_SCALE = val
    return _UI_SCALE


def set_ui_scale(val):
    """强制指定缩放（测试/热调使用）。传 None 表示**清除覆盖**、下次重读环境变量。

    [为什么要支持清除] `_UI_SCALE` 是模块级缓存。测试里先 set 成 2.0，
    再 `del sys.modules[...]` 重新导入时**新模块的全局变量是 None**，
    但同一测试进程里若只改环境变量而不重新导入，就永远读到旧值。
    提供 None 清除语义，测试能干净地验证环境变量分支。
    """
    global _UI_SCALE
    if val is None:
        _UI_SCALE = None
        return ui_scale()
    _UI_SCALE = max(1.0, min(2.0, float(val)))
    return _UI_SCALE


def font_px(size, minimum=10):
    """把设计稿里的像素字号按全局比例换算（QSS 用，返回 int）。

    :param size: 设计稿字号（px）
    :param minimum: 下限（防止缩放后仍小到看不清）

    [为什么四舍五入到整像素] QSS 支持小数，但整像素在 1x 屏上渲染更锐利
    （半像素会触发抗锯齿导致发虚，板端实测文字边缘变灰）。
    """
    return max(int(minimum), int(round(float(size) * ui_scale())))


# ============================================================================
#                          样式表
# ============================================================================

#: 完整样式表。规则体逐字照搬 C 端 HsfThemeCss.h，
#: 只把选择器名换成 Qt 对应类（XGui 类名 → Qt 类名）。
#: QSS 模板（令牌用 `%(key)s` 占位；渲染见 `stylesheet(theme_id)`）。
_QSS_TEMPLATE = """
/* ==================== 全局 ==================== */
/* [不加 `QWidget { color: ... }`] 裸 QWidget 规则会级联到**所有**子控件，
   包括深蓝标题栏和白字标签——实测把顶栏文字压成灰黑不可读。
   正文色交给调色板（WindowText/Text），只对具体控件类设色。 */
QMainWindow, QDialog {
    background-color: %(page_bg)s;
}

/* ==================== QPushButton：常态/悬停/焦点/按下/禁用 ==================== */
/*
 * [字色跟网页端同口径 2026-11-17 用户「亮色工业白整体对齐 web 端的美化
 *  样式，每个界面都看看」] 网页端 `.btn` 的字色是 **var(--text)**（正文
 * 黑 #1A1A1A），"可点"的表达靠 **hover/按下时换底色 + accent 边框**表达；
 * 本端原来把常态字色设成 accent_line（蓝）⇒ 两端口径不一致 ⇒ 整屏看
 * 过去桌面端"蓝字白底"、网页端"黑字白底"，按钮区像两套皮肤。
 * ⇒ 常态字色改回正文色；强调态（hover/focus/pressed/checked）仍用
 *   accent 边框 + 换底（与网页端 `.btn:hover/.primary` 同款）。
 *
 * [圆角走 Material Design 3「大圆角/胶囊」2026-11-18 用户「其他控件也要
 *  美化下，按钮，多选框等，都现代化移动 ui」] M3 的按钮是**全圆角**（pill
 * = 圆角 ≥ 高度一半）。Qt 的 QSS 圆角超过盒高一半会自动收敛到半高 ⇒
 * 给一个大于常规按钮半高（约 16px）的 18px：标准高度按钮成胶囊，更高的
 * 按钮也是大圆角，一档通吃。DO 切换按钮单独钉回 radius_ctrl（方块语义，
 * 见下面 HsfDoToggle 块）；导航条目在 nav_panel_qss（chrome，不在本表）。
 * hover/按下仍用 hover_bg/press_bg（= 主色 8%%/12%% 预混浅色，M3 状态层口径）。
 */
QPushButton {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_btn)dpx;
    padding: 6px 14px;
    /* [触屏目标 2026-11-19 用户「控制区的按钮和下拉大点，触摸不方便」]
     * min-height 从 18px 提到 **26px**（设计稿口径；经 font_px 随全局缩放
     * 走 ⇒ 1.5× 板端 ≈ 39px + padding 12 + 边框 2 ⇒ **53px** 触摸目标）。
     * [为什么动 QSS 而不是部件 setMinimumHeight] 样式表算出的 sizeHint 会
     * **压过**部件的 setMinimumHeight/setFixedHeight（本项目注释早已记录
     * 同款陷阱 ⇒ DO 切换钮同解：去 QSS min/max、代码实测）——plain 按钮
     * 没法走那条路（QSS 的 min-height 是唯一不被覆盖的杠杆）。 */
    min-height: %(btn_min_h)dpx;
    color: %(text)s;
}
QPushButton:hover {
    background-color: %(hover_bg)s;
    border: 1px solid %(accent_line)s;
}
QPushButton:focus {
    background-color: %(white)s;
    border: 1px solid %(accent_line)s;
}
QPushButton:pressed {
    background-color: %(press_bg)s;
    border: 1px solid %(accent_line)s;
}
QPushButton:checked {
    background-color: %(press_bg)s;
    border: 1px solid %(accent_line)s;
}
QPushButton:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}
/*
 * [禁用 + 已勾选 = 「通」但不可点 —— 必须仍看得出通 2026-11-12]
 * QSS 里 :checked 与 :disabled 两条规则**同时命中**时，后者（disabled）
 * 的背景/边框/文字全赢 ⇒ 数字量页 DO 格子在自动模式（整片禁用）下
 * 「通」和「断」长得**一模一样**，只剩文字能区分 —— 用户反馈「自动模式下
 * 数字量输出看不出来」。这里补一条更具体的规则（:disabled:checked 特异度
 * 更高）把「通」态单独着色：浅绿底 + 绿边 + 深绿字，与断态（灰）拉开。
 */
QPushButton:disabled:checked {
    background-color: %(ok_soft)s;
    border: 1px solid %(ok)s;
    color: %(ok)s;
}

/* ---- 数字量 DO 切换按钮：内边距收小，行高由代码按字体实测 ---- */
/*
 * [为什么要单独一条] 通用 QPushButton 有 padding:6px 14px + min-height:18px，
 * 样式表算出的 sizeHint 会压过 setFixedHeight：单行文字 33px、两行 51px，
 * 而 setFixedHeight(46) 只约束了 maximum —— 结果同一行按钮高度 46/33 参差。
 * 这条把内边距改小，两行文字（DO 号 + 语义名）也放得下。
 *
 * [为什么不再写死 min/max-height —— 用户截图「文字显示不全」] 按钮是两行
 * 文字，高度需求随**界面缩放**走：缩放 1.5× 时字号 18px、两行行高实测
 * 40px，加上下 padding(4×2) 与边框(1×2) 需要 **50px**；原来钉死
 * 44~46px ⇒ 内容区只剩 38px < 40px，**上下各裁一点**，字被削掉。
 * 现在高度交给 page_base.ToggleGrid 用 QFontMetrics 实测后 setFixedHeight，
 * QSS 只留内边距/字号；内边距数值与高度计算同源（DO_BTN_PAD_V）。
 */
QPushButton#HsfDoToggle {
    padding: %(doPadV)dpx 8px;
    /* [圆角钉回 4px] 通用按钮改了胶囊圆角（M3），DO 切换是"方块开关"语义
     * （通/断两态格子），胶囊形状会让一行格子看起来松散；且卡片里的
     * DO 按钮走 do_toggle_qss()（自带 radius_ctrl）⇒ 这里钉同值，卡片
     * 内外形状一致。 */
    border-radius: %(radius_ctrl)dpx;
    /*
     * [字号 12 → 13 2026-11-17 用户实报「数字量页 DO 文字太小」]
     * DO 格子是两行小字（"DO0 断" + "电动球阀1 控制"），18px 在 1366 宽的
     * 窗口里偏小 ⇒ 提到 fz13（≈20px @1.5）。按钮高度由 ToggleGrid 按
     * **当前字体实测**算（QFontMetrics）⇒ 字号变大时行高自动跟着长，
     * 不会把字裁掉。
     */
    font-size: %(fz13)dpx;
}

/* ==================== QLineEdit ==================== */
/*
 * [M3 文本框 2026-11-18] 圆角 8~10px（移动端输入框口径）；聚焦 = **2px
 * 主色描边**。聚焦变粗在网页/Material 是"画在外面的描边、内容不动"，
 * Qt 的 QSS 边框在盒内 ⇒ 聚焦 1px→2px 会让内容区缩 2px、sizeHint 变化
 * ⇒ 布局抖动。治法：**加粗的同时把 padding 各让 1px**（1+2×5=2+2×4，
 * 横向 1+8×2=2+7×2），内容盒逐像素不变 ⇒ 零抖动。
 */
QLineEdit {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_input)dpx;
    padding: 5px 8px;
    selection-background-color: %(accent_fill)s;
}
QLineEdit:hover {
    background-color: %(white)s;
    border: 1px solid %(accent_fill)s;
}
QLineEdit:focus {
    background-color: %(white)s;
    border: 2px solid %(accent_line)s;
    padding: 4px 7px;
}
QLineEdit:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}
/*
 * [2026-11-19 用户「加减按钮好像还有边框，盖住了微调框显示文字的大边框」]
 * QSpinBox 内部有一个隐藏的 QLineEdit 承载文字 —— **QLineEdit 的规则会
 * 命中它** ⇒ 它在编辑区四周画自己的 1px 边框（聚焦时 2px 蓝框）⇒ 微调框
 * 里套了一个带边框的小框（用户看到的"加减按钮的边框"，位置正是编辑区
 * 两侧）。⇒ 后代选择器把**微调框内部的 lineEdit** 压成无边框透明
 * （外框由 QSpinBox 规则画，内部不再套框）。:focus/:hover 显式压制，
 * 否则伪类规则特异性更高会漏网。
 */
QSpinBox QLineEdit, QDoubleSpinBox QLineEdit {
    background: transparent;
    border: none;
    padding: 0px;
}
QSpinBox QLineEdit:hover, QDoubleSpinBox QLineEdit:hover,
QSpinBox QLineEdit:focus, QDoubleSpinBox QLineEdit:focus,
QSpinBox QLineEdit:disabled, QDoubleSpinBox QLineEdit:disabled {
    background: transparent;
    border: none;
    padding: 0px;
}

/* ==================== QSpinBox / QDoubleSpinBox ==================== */
/*
 * [2026-11-18 用户「微调框样子有问题，可以美化下吗」+「最好适配触摸屏幕
 *  点击」+「上下好像也不好点吧」] 微调框改成**触屏数量选择器**（电商
 * App 的事实标准）：**左 − 右 ＋、全框高按钮、中间数值**。布局由代理
 * 样式 `_ModernStyle.subControlRect` 摆（QSS 的 ::up-button 只能挂
 * 右侧，且一旦定样式 Fusion 就不画箭头 ⇒ 字形也由代理自绘）。
 */
/*
 * [padding 40px 是**布局**不是装饰] SC_SpinBoxEditField 的矩形由 QSS
 * 自己算（有 QSpinBox 规则时它不委托基类 ⇒ 代理的布局管不到编辑区）
 * ⇒ 用 padding 把内容区两侧顶开，编辑区才不压到按钮（实测没这个
 * padding 时文字从左按钮底下开始、整个看不见）。
 */
QSpinBox, QDoubleSpinBox {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_input)dpx;
    padding: 4px 40px;
    min-height: 22px;
}
QSpinBox:hover, QDoubleSpinBox:hover {
    border: 1px solid %(accent_fill)s;
}
/* [刻意没有 QSpinBox:focus 规则 —— 2026-11-17 QComboBox 同款怪癖]
 * 只要存在 :focus 规则，::up-arrow 的 image 在重 polish 时会**失效**
 * （combo 实测：存在 QComboBox:focus ⇒ ::down-arrow 的 PNG 消失，两种
 * 伪状态写法都救不回来）。spinbox 的字形也只能走 image（见下）⇒ 同样
 * 不能有 :focus 规则。焦点反馈由 :hover 的主色边框承担（触屏产品键盘
 * 焦点场景少）。 */
QSpinBox:disabled, QDoubleSpinBox:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}
/* ---- 步进钮（上下箭头）----
 * [2026-11-19 取证（离屏调试代理）] QSS 有 QSpinBox 基规则（border/bg）
 * ⇒ 样式引擎**整体接管 CC_SpinBox**：代理的 drawComplexControl /
 * drawPrimitive **收不到调用**（CC=0、Spin PE=0）⇒
 *   · 按钮面板：引擎在按钮 rect 上自绘一块带边框的面板（颜色 = border
 *     令牌）⇒ 用户看到的「加减按钮的边框」（恰在编辑区两侧，像把大框
 *     切成三截）⇒ 给按钮子控件 **border:none + background:transparent**
 *     规则压制（实测布局仍问代理 subControlRect ⇒ 左右 −/+ 保留）；
 *   · 字形：只能走 **::up-arrow / ::down-arrow 的 image 规则**（运行时
 *     生成细线字形 PNG，机制与 11-17 combo 的 ::down-arrow 同款）；
 *   · focus 规则会让 image 失效（11-17 combo 同款怪癖）⇒ spinbox 的
 *     :focus 规则已刻意移除（见上）；
 *   · hover/按下反馈：::up-button / ::down-button 的 :hover/:pressed
 *     底色规则（引擎画）——触摸没有 hover ⇒ 按下反馈必须明显。
 */
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    border: none;
    background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: %(hover_bg)s;
}
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
    background-color: %(press_bg)s;
}
%(spin_up_rule)s
%(spin_down_rule)s
%(spin_up_rule_disabled)s
%(spin_down_rule_disabled)s

/* ==================== QComboBox ==================== */
/*
 * [M3 下拉框 2026-11-18] 圆角 8px（与输入框同档）。箭头：原先那套
 * 「运行时生成 PNG 当 ::down-arrow image」已整体移除。
 * [箭头的实际归属（离屏取证 2026-11-18，.zcode/tmp/probe_arrow_dbg.py）]
 *   本表给 QComboBox 设了 border/background ⇒ 样式引擎**整体接管**下拉框
 *   （基样式的 CC_ComboBox 与 PE_IndicatorArrowDown 都收不到调用）⇒ 箭头
 *   由样式引擎自绘为一枚 8×5 实心三角，颜色 = **border 令牌**（红边框
 *   实验：箭头变红、与 color: 无关；image:none 也关不掉）。这与 QSS 模板
 *   开头那条「::indicator 接管」是同族机制。⇒ 组合框箭头 = 边框色的实心
 *   小三角（旧 PNG 方案的形状/亮色主题同色，深色主题改随 border 令牌）；
 *   画笔细 chevron 服务于**没被 QSS 接管**的箭头（表头排序、树、工具钮
 *   菜单，见 _ModernStyle._draw_chevron）。
 */
QComboBox {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_input)dpx;
    padding: 5px 8px;
    /* [触屏目标 2026-11-19] 与按钮同口径（见 QPushButton min-height 注）。 */
    min-height: %(btn_min_h)dpx;
}
QComboBox:hover {
    border: 1px solid %(accent_fill)s;
}
/* [刻意**仍然没有** QComboBox:focus 规则 —— 原因已变，教训要留]
 *   2026-11-17 的怪癖：只要 QSS 里**存在** `QComboBox:focus` 规则，样式
 *   引擎重 polish 时会让子控件的 **image 失效** ⇒ 当时 `::down-arrow`
 *   引用的 PNG 三角直接消失（两种伪状态写法都救不回来，实测）。
 *   2026-11-18 箭头改成代理样式画笔自绘后，**没有 image 可失效**，那条
 *   怪癖的机制已不存在；但 focus 规则仍不回加——焦点反馈由 `:hover`
 *   的 accent 边框承担（触屏产品没有键盘焦点诉求），不再为一条用不上
 *   的规则去碰 Qt 样式引擎的 fragile 路径。若将来真要加，必须先过
 *   §22b 的像素探针。
 *   [同款怪癖的活口] QCheckBox 的 `::indicator` 同理（见下面那段注释）。 */
QComboBox:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}
QComboBox QAbstractItemView {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    selection-background-color: %(press_bg)s;
    selection-color: %(text)s;
    outline: none;
}
/* [触屏下拉列表 2026-11-18] 弹层条目加内边距 ⇒ 行高 ≥ 28px，手指按得中
 * （M3 触摸目标口径；旧观感是"挤成一指宽"的原生列表）。 */
QComboBox QAbstractItemView::item {
    padding: 4px 12px;
}
/* ---- 下拉箭头区 ----
 * [2026-11-17 用户实报「下拉框的箭头怎没了」的根因仍在] Qt 样式引擎一旦
 * 给子控件（`::drop-down`）设了 border/background，就**不再画原生箭头**。
 * 但箭头不再用图片兜底，而是代理样式的画笔 chevron ⇒ 这里只保留"定宽 +
 * 无分隔线"两个意图（宽度 30px 触摸区；无背景无边框 = 不接管绘制），
 * 箭头本体见 _ModernStyle.drawPrimitive 的 PE_IndicatorArrowDown。
 */
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 30px;
    border: none;
    background-color: transparent;
}
/* [下拉箭头 2026-11-19 用户「下拉框下拉箭头没了，修复后记得重点检查」]
 * ::drop-down 无规则时引擎不画箭头（取证：箭头区深色像素 0——agent E
 * 当时"引擎自绘实心三角"的取证已被它自己的 ::drop-down none 规则推翻）
 * ⇒ 走 image 规则（细 chevron PNG，次级文本色；与微调框字形同一套线条
 * 语言）。[重点检查项] 这条规则动过之后必须离屏验箭头像素 >0（回归锁
 * §36 K），避免再次悄悄消失。 */
%(combo_down_rule)s
%(combo_down_rule_disabled)s

/* ==================== QCheckBox / QRadioButton ==================== */
/*
 * [M3 触摸 2026-11-18] 间距 8px（文字与指示器），指示器本体 18px 见方
 * 由代理样式 _ModernStyle 的 pixelMetric 下发——QSS 里写 spacing 是唯一
 * 接触面，形状/配色全在画笔里（见 _ModernStyle 类注释）。
 */
QCheckBox, QRadioButton {
    spacing: 8px;
}
QCheckBox:disabled, QRadioButton:disabled {
    color: %(offline)s;
}
/* ---- 指示器（勾选框/单选钮的方框与圆圈）**故意不写** ----
 * [为什么 QSS 里整段不写] 2026-11-17 用户实报「这些多选框是选中的怎么
 * 没有打勾」。根因不是状态位错（isChecked 一直是 True），而是 **Qt 样式
 * 引擎的规矩**：只要 QSS 给 `::indicator`（或任何子控件）设了
 * border/background，样式引擎就**不再画原生指示器**（连同对勾/圆点一起）
 * ⇒ 勾选态只剩一个**空方框**，看着像"没选中"。
 * 而且这会造成「同一页两种表现」：控件**自带**样式表会**截断**祖先样式表
 * 的级联 ⇒ 有的复选框拿不到这些规则（渲染成原生 Fusion：灰框+黑勾），
 * 有的拿得到（空方框）⇒ 用户看到"有的有勾、有的没勾"。
 * [2026-11-18 现代化的正确入口] M3 观感（圆角方块+白勾 / 圆环+圆点）由
 * **代理样式 _ModernStyle.drawPrimitive** 自绘（PE_IndicatorCheckBox /
 * PE_IndicatorRadioButton）——QSS 一旦写 `::indicator` 就会把引擎切到
 * "接管"路径、画笔再也收不到调用 ⇒ **这条红线永远别碰**。
 *
 * [注] 多选浮层里的方框+对勾是 **CheckRow 自绘**（data_toolbar.py），
 * 与 `::indicator` 无关 ⇒ 这里删规则不影响它。
 */

/* ==================== QMenu / QToolTip（弹层也跟主题） ==================== */
/*
 * [为什么补] 弹层（账户菜单、下拉列表）走 parentWidget 链继承样式表 ⇒
 * 之前没给 QMenu 写规则，弹出来就是"原始的系统菜单"。
 * [M3 弹层 2026-11-18] 菜单容器圆角 8px、条目 hover 用主色软底（hover_bg
 * = 主色 8%% 预混）、条目圆角 6px 成"圆片"——手机菜单观感。
 * [圆角的已知妥协] QMenu 是不透明弹层，QSS 圆角外的四个角会露出调色板
 * Window 底色（浅色主题下是页面灰，与白底接近，肉眼几乎不可见；深色
 * 主题同理同色系）——要彻底无角就得 WA_TranslucentBackground，那是
 * 窗口级改动，不在本表能做的范围。
 */
QMenu {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius_menu)dpx;
    padding: 6px;
}
QMenu::item {
    padding: 6px 14px;
    border-radius: 6px;
    color: %(text)s;
}
QMenu::item:selected {
    background-color: %(hover_bg)s;
    color: %(text)s;
}
QMenu::item:disabled {
    color: %(offline)s;
}
QMenu::separator {
    height: 1px;
    background: %(border_soft)s;
    margin: 6px 8px;
}

/* ==================== QToolTip ==================== */
/*
 * [为什么补 2026-11-18] 提示泡没写过规则 ⇒ 永远是系统黑底黄字（Fusion
 * 缺省），与整套主题脱节。浅底 + 主题描边 + 4px 圆角（圆角外露调色板
 * Window 底，与 QMenu 同款妥协）。
 */
QToolTip {
    background-color: %(white)s;
    color: %(text)s;
    border: 1px solid %(border_sub)s;
    border-radius: 4px;
    padding: 6px 8px;
}

/* ==================== QGroupBox ==================== */
QGroupBox {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius_card)dpx;
    margin-top: 14px;
    padding-top: 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: %(text)s;
}

/* ==================== QTabWidget / QTabBar ==================== */
QTabWidget::pane {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius_card)dpx;
}
QTabBar::tab {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-bottom: none;
    border-top-left-radius: %(radius_ctrl)dpx;
    border-top-right-radius: %(radius_ctrl)dpx;
    padding: 6px 16px;
    margin-right: 2px;
}
QTabBar::tab:hover {
    background-color: %(hover_bg)s;
    border: 1px solid %(accent_line)s;
    border-bottom: none;
}
QTabBar::tab:selected {
    background-color: %(press_bg)s;
    border: 1px solid %(accent_line)s;
    border-bottom: none;
}
QTabBar::tab:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}

/* ==================== QTableWidget / QTableView ==================== */
QTableView, QTableWidget {
    background-color: %(white)s;
    alternate-background-color: %(alt_bg)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius_card)dpx;
    gridline-color: %(grid_line)s;
    selection-background-color: %(press_bg)s;
    selection-color: %(text)s;
}
QHeaderView::section {
    background-color: %(header_bg)s;
    border: none;
    border-right: 1px solid %(grid_line)s;
    border-bottom: 1px solid %(border_soft)s;
    padding: 6px 8px;
    color: %(text)s;
    /*
     * [字号必须显式给 2026-11-17 用户实报「表格文字太小」]
     * QSS 里只要给 `::section` 设了 font-weight（下面那行），Qt 就会**重新
     * 解析表头字体** ⇒ 没写 font-size 时它回落到**视图缺省 9pt**（实测：
     * 表内 20px、表头 9pt，表头比内容还小一大截）。
     * ⇒ 表头字号与表内统一走 fz13（≈20px @1.5），且**跟随全局缩放**。
     */
    font-size: %(fz13)dpx;
    font-weight: bold;
}
QHeaderView::section:last {
    border-right: none;
}
QTableCornerButton::section {
    background-color: %(header_bg)s;
    border: none;
}
/*
 * [行分隔线：照网页端口径 2026-11-17 用户「客户端表格美化成 web 端这种样子」]
 * 网页端 `.table td { border-bottom: 1px solid var(--border-soft); }`
 * ⇒ 每行下面一条细线，横向能一条条读下去。桌面端原来 `setShowGrid(False)`
 * ⇒ **完全没有行线**（只有斑马纹），整张表看着"糊"在一起。
 * ⇒ 用 QSS 给**单元格**加下边框（不回到 setGrid：那样会连竖线也画出来，
 * 而网页端只有横线）。
 */
QTableView::item, QTableWidget::item {
    border-bottom: 1px solid %(grid_line)s;
    padding: 2px 8px;
}

/* ==================== QScrollBar ==================== */
/*
 * [M3 滚动条 2026-11-18] 细（8px）、圆头、**半透明灰**（悬停加深）——
 * 手机上滚动条是"悬浮的细条"，不占视觉宽度。半透明色没法写进令牌式
 * 十六进制 ⇒ 每套主题给 rgba() 字符串令牌（scroll_handle_soft/hover），
 * 换主题跟着换。两端箭头槽（add-line/sub-line）与滚动轨道余段
 * （add-page/sub-page）全部隐藏/透明。
 * [2026-11-19 用户「滚动条也要美化，跟现在的移动 ui 不是一个风格」]
 * 把手**四周内缩**（margin 3px ⇒ 把手 6px 宽、上下不顶卡边）——原来
 * 贴死页面右缘、上下顶到卡片，是一条"实心长条"；现在像 iOS 的悬浮
 * 胶囊（min-height 也放宽到 36px 让圆头更明显）。半透明不变。
 */
QScrollBar:vertical {
    background: transparent;
    width: %(scroll_w)dpx;
    margin: 3px 3px 3px 0;
}
QScrollBar::handle:vertical {
    background: %(scroll_handle_soft)s;
    border-radius: %(scroll_r)dpx;
    min-height: 40px;
    /* [命中区宽、视觉细 2026-11-19] 把手左右各内缩 4px ⇒ 可见宽
     * ≈ 14px（圆头），但整条 22px 都是命中区 ⇒ 手指按得到。 */
    margin: 2px 4px 2px 2px;
}
QScrollBar::handle:vertical:hover {
    background: %(scroll_handle_hover)s;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QScrollBar:horizontal {
    background: transparent;
    height: %(scroll_w)dpx;
    margin: 0 3px 3px 3px;
}
QScrollBar::handle:horizontal {
    background: %(scroll_handle_soft)s;
    border-radius: %(scroll_r)dpx;
    min-width: 40px;
    margin: 2px 2px 2px 4px;
}
QScrollBar::handle:horizontal:hover {
    background: %(scroll_handle_hover)s;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
}

/* ==================== QProgressBar ==================== */
QProgressBar {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    text-align: center;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: %(accent_fill)s;
    border-radius: 3px;
}
QProgressBar:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
}

/* ==================== QSlider ==================== */
/* ==================== QSlider ==================== */
/*
 * [触摸屏放大 2026-11-06] 原轨道 4px / 手柄 14px，用户反馈「触摸范围感觉
 * 有点小，有时候按上去没反应」。手指接触面约 8~10mm，1280x800 屏上约
 * 30~40px。这里把轨道加到 10px、手柄加到 34x34（圆角 17px = 正圆），
 * 视觉上仍是一条滑轨 + 圆形滑块，与手机一致。
 *
 * [margin 必须负得够] QSS 里 handle 的 margin 是相对 groove 的偏移，
 * 要让 34px 的手柄居中在 10px 的轨道上，需要 -(34-10)/2 = -12px。
 * 写小了手柄会被轨道裁掉一半。
 */
QSlider::groove:horizontal {
    height: 10px;
    background: %(scroll_handle)s;
    border-radius: 5px;
}
QSlider::handle:horizontal {
    background: %(accent_fill)s;
    border: 1px solid %(accent_line)s;
    width: 34px;
    height: 34px;
    margin: -12px 0;
    border-radius: 17px;
}
QSlider::handle:horizontal:hover {
    background: %(accent_line)s;
}
QSlider::handle:horizontal:pressed {
    background: %(accent_line)s;
}
QSlider::handle:horizontal:disabled {
    background: %(border_soft)s;
    border: 1px solid %(border_sub)s;
}

/* ==================== QSplitter ==================== */
QSplitter::handle {
    background-color: %(border_soft)s;
}
QSplitter::handle:hover {
    background-color: %(accent_fill)s;
}
QSplitter::handle:horizontal {
    width: %(splitter_w)dpx;
}
"""


# ============================================================================
#                          多主题（2026-11-14 用户定案）
# ============================================================================
#
# [为什么要有] 用户口径：「本地设置增加主题功能，多适配几款好看的主题」。
# 之前只有 `HSF_UI_SKIN=dark` 一个环境变量开关（连深色表盘都是写死的两
# 套），**用户选不了**、也没有第二套配色。
#
# [结构] 每套主题 = 令牌（QSS 用 `%(key)s` 取值）+ 调色板（QPalette，给
# 没写 QSS 的控件如菜单/提示）+ 表盘色（自绘面）+ chrome 色（顶栏/底栏/
# 导航）+ 深浅标记。
#
# [令牌单源] QSS 里**不许再写死颜色**（写死的颜色在深色主题下必然难看）
# ⇒ 新增控件一律用令牌；本文件末尾的「写死色自检」会数一遍。

def _theme(tid, name, dark, tokens, palette, dial, chrome):
    """造一套主题（字段名统一，便于自检脚本遍历）。"""
    return {
        "id": tid,
        "name": name,
        "dark": bool(dark),
        "tokens": dict(tokens),
        "palette": dict(palette),
        "dial": dict(dial),
        "chrome": dict(chrome),
    }


#: 主题清单（界面下拉顺序＝这里���顺序）。
THEME_LIST = (
    ("light", "亮色·工业白"),
    ("dark", "深色·控制室"),
    ("isa", "亮色·ISA 仪表"),
    ("green", "护眼绿"),
    ("contrast", "高对比"),
)
#: 缺省主题。
DEFAULT_THEME_ID = "light"

THEMES = {
    "light": _theme(
        "light", "亮色·工业白", False,
        tokens={
            "white": CLR_WHITE, "hover_bg": CLR_HOVER_BG,
            "press_bg": CLR_PRESS_BG, "accent_fill": CLR_ACCENT_FILL,
            "accent_line": CLR_ACCENT_LINE, "border": CLR_BORDER,
            "border_sub": CLR_BORDER_SUB, "border_soft": CLR_BORDER_SOFT,
            "disabled_bg": CLR_DISABLED_BG, "page_bg": COLOR_PAGE_BG,
            "offline": CLR_OFFLINE, "ok": CLR_OK, "ok_soft": CLR_OK_SOFT,
            "text": "#1A1A1A", "text_dim": "#5A6572",
            "title_text": "#1F4E79",
            "alt_bg": "#FAFBFC", "grid_line": "#E4E8EC",
            "header_bg": "#EFF3F7", "scroll_handle": "#C2CAD2",
            # [滚动条半透明 2026-11-18] M3 细条口径：半透明灰悬浮在页面底上，
            # 悬停加深。rgba 字符串无法从十六进制令牌派生 ⇒ 每套主题登记。
            "scroll_handle_soft": "rgba(90,101,114,35%)",
            "scroll_handle_hover": "rgba(90,101,114,60%)",
        },
        palette={"window": COLOR_PAGE_BG, "base": CLR_WHITE,
                 "alt_base": "#FAFBFC", "text": "#1A1A1A",
                 "window_text": "#1A1A1A", "button_text": "#1A1A1A",
                 "highlight": CLR_ACCENT_FILL, "highlighted_text": "#FFFFFF"},
        dial={"disc": DIAL_DISC, "track": DIAL_TRACK, "caption": DIAL_CAPTION,
              "text": DIAL_TEXT, "info": CLR_INFO},
        chrome={"brand": COLOR_BRAND, "status_bg": COLOR_STATUS_BG,
                "nav_bg": COLOR_NAV_BG, "nav_sel": COLOR_NAV_SEL},
    ),
    "dark": _theme(
        "dark", "深色·控制室", True,
        tokens={
            "white": "#1E242C", "hover_bg": "#2A3542", "press_bg": "#31404F",
            "accent_fill": "#3D8BFD", "accent_line": "#64B5F6",
            "border": "#4A5563", "border_sub": "#5C6875",
            "border_soft": "#333C47", "disabled_bg": "#262D36",
            "page_bg": "#151A20", "offline": "#7A8592",
            "ok": "#66BB6A", "ok_soft": "#23362A",
            "text": "#E8EAED", "text_dim": "#A9B2BC",
            "title_text": "#64B5F6",
            "alt_bg": "#232A33", "grid_line": "#333C47",
            "header_bg": "#232B35", "scroll_handle": "#4A5563",
            "scroll_handle_soft": "rgba(169,178,188,35%)",
            "scroll_handle_hover": "rgba(169,178,188,60%)",
        },
        palette={"window": "#151A20", "base": "#1E242C",
                 "alt_base": "#232A33", "text": "#E8EAED",
                 "window_text": "#E8EAED", "button_text": "#E8EAED",
                 "highlight": "#3D8BFD", "highlighted_text": "#0B1017"},
        dial={"disc": DIAL_DISC_DARK, "track": DIAL_TRACK_DARK,
              "caption": DIAL_CAPTION_DARK, "text": DIAL_TEXT_DARK,
              "info": DIAL_INFO_DARK},
        chrome={"brand": "#0E2A44", "status_bg": "#000000",
                "nav_bg": "#1B222A", "nav_sel": "#2C3B4C"},
    ),
    "isa": _theme(
        "isa", "亮色·ISA 仪表", False,
        tokens={
            "white": "#FDFDFB", "hover_bg": "#F3EFE4", "press_bg": "#EAE3D2",
            "accent_fill": "#C77700", "accent_line": "#A85F00",
            "border": "#6B6355", "border_sub": "#8B8272",
            "border_soft": "#DED7C8", "disabled_bg": "#F1EFE9",
            "page_bg": "#F4F2EC", "offline": "#8A857A",
            "ok": "#4E7A2E", "ok_soft": "#E8F0DE",
            "text": "#241F18", "text_dim": "#6B6355",
            "title_text": "#A85F00",
            "alt_bg": "#F7F5EF", "grid_line": "#E2DCCE",
            "header_bg": "#EFEDE5", "scroll_handle": "#C8BFAE",
            "scroll_handle_soft": "rgba(107,99,85,35%)",
            "scroll_handle_hover": "rgba(107,99,85,60%)",
        },
        palette={"window": "#F4F2EC", "base": "#FDFDFB",
                 "alt_base": "#F7F5EF", "text": "#241F18",
                 "window_text": "#241F18", "button_text": "#241F18",
                 "highlight": "#C77700", "highlighted_text": "#FFFFFF"},
        dial={"disc": "#F8F6F0", "track": "#DED7C8", "caption": "#6B6355",
              "text": "#241F18", "info": "#A85F00"},
        chrome={"brand": "#3B3126", "status_bg": "#2A241C",
                "nav_bg": "#EFEDE5", "nav_sel": "#E3D9BF"},
    ),
    "green": _theme(
        "green", "护眼绿", False,
        tokens={
            "white": "#F4FAF2", "hover_bg": "#E3F2E0", "press_bg": "#D3EAD0",
            "accent_fill": "#2E7D52", "accent_line": "#1F5E3C",
            "border": "#5A6B5F", "border_sub": "#7C8C80",
            "border_soft": "#D3E0D5", "disabled_bg": "#ECF2EB",
            "page_bg": "#EDF4EC", "offline": "#7C8C80",
            "ok": "#2E7D32", "ok_soft": "#E0F0E2",
            "text": "#1C2A22", "text_dim": "#5A6B5F",
            "title_text": "#1F5E3C",
            "alt_bg": "#EFF6EE", "grid_line": "#DCE8DC",
            "header_bg": "#E7F1E5", "scroll_handle": "#B4C6B6",
            "scroll_handle_soft": "rgba(90,107,95,35%)",
            "scroll_handle_hover": "rgba(90,107,95,60%)",
        },
        palette={"window": "#EDF4EC", "base": "#F4FAF2",
                 "alt_base": "#EFF6EE", "text": "#1C2A22",
                 "window_text": "#1C2A22", "button_text": "#1C2A22",
                 "highlight": "#2E7D52", "highlighted_text": "#FFFFFF"},
        dial={"disc": "#F1F8EF", "track": "#D3E0D5", "caption": "#5A6B5F",
              "text": "#1C2A22", "info": "#1F5E3C"},
        chrome={"brand": "#1F4034", "status_bg": "#14251E",
                "nav_bg": "#E7F1E5", "nav_sel": "#D2E7D0"},
    ),
    "contrast": _theme(
        "contrast", "高对比", False,
        tokens={
            "white": "#FFFFFF", "hover_bg": "#E8E8E8", "press_bg": "#D0D0D0",
            "accent_fill": "#0047BB", "accent_line": "#003A99",
            "border": "#000000", "border_sub": "#333333",
            "border_soft": "#8C8C8C", "disabled_bg": "#E0E0E0",
            "page_bg": "#FFFFFF", "offline": "#595959",
            "ok": "#006400", "ok_soft": "#D6EFD6",
            "text": "#000000", "text_dim": "#333333",
            "title_text": "#0047BB",
            "alt_bg": "#F5F5F5", "grid_line": "#767676",
            "header_bg": "#F0F0F0", "scroll_handle": "#767676",
            # 高对比主题：滚动条也保持高可见（45%/75% 黑，不用浅淡的 35%）。
            "scroll_handle_soft": "rgba(0,0,0,45%)",
            "scroll_handle_hover": "rgba(0,0,0,75%)",
        },
        palette={"window": "#FFFFFF", "base": "#FFFFFF",
                 "alt_base": "#F5F5F5", "text": "#000000",
                 "window_text": "#000000", "button_text": "#000000",
                 "highlight": "#0047BB", "highlighted_text": "#FFFFFF"},
        dial={"disc": "#FFFFFF", "track": "#767676", "caption": "#000000",
              "text": "#000000", "info": "#0047BB"},
        chrome={"brand": "#000000", "status_bg": "#000000",
                "nav_bg": "#F0F0F0", "nav_sel": "#C8DCFF"},
    ),
}


def _base_tokens():
    """与主题无关的令牌（字号/圆角/分隔条宽/内边距）。"""
    return {
        "radius_ctrl": RADIUS_CTRL,
        "radius_card": RADIUS_CARD,
        # [现代化控件圆角 2026-11-18] M3/移动端口径，常量见上方 RADIUS_*。
        "radius_btn": RADIUS_BTN,
        "radius_input": RADIUS_INPUT,
        "radius_menu": RADIUS_MENU,
        "scroll_w": SCROLL_W,
        "scroll_r": SCROLL_R,
        "splitter_w": SPLITTER_HANDLE_W,
        # [按钮触屏最小高 2026-11-19 用户「控制区的按钮和下拉大点，触摸不方
        #  便」] QSS min-height 26px（设计稿口径）→ font_px 随全局缩放 ⇒
        # 1.5× 板端 ≈ 39px + padding 12 + 边框 2 ⇒ ~53px 触摸目标。
        "btn_min_h": font_px(26, minimum=18),
        # 字号：见 ui_scale()/font_px()。求值发生在本模块导入时，故环境变量
        # HSF_UI_SCALE 必须在**导入 theme 之前**设好（__main__ 就是这么做的）。
        "fz12": font_px(12),
        # [表内/表头/DO 按钮的字号 2026-11-17] 用户实报「表格文字太小、没
        # 接入全局缩放吗」⇒ 表内与表头统一到这一档（≈20px @1.5）。
        "fz13": font_px(13),
        # DO 按钮内边距（与 page_base.ToggleGrid 的高度计算同源）。
        "doPadV": DO_BTN_PAD_V,
    }


def _detect_theme_id():
    """探测当前主题 id。

    [优先级] 环境变量 HSF_UI_THEME（CI/截图用）＞ 本机设置（用户在
    「本地设置 → 界面设置 → 主题」里选的，**客户端持久化**）＞ 旧环境变量
    HSF_UI_SKIN=dark（兼容）＞ 缺省亮色。
    """
    env = os.environ.get("HSF_UI_THEME", "").strip().lower()
    if env in THEMES:
        return env
    try:
        from . import settings as ui_settings
        tid = (ui_settings.get_str(ui_settings.K_THEME, "") or "").strip().lower()
        if tid in THEMES:
            return tid
    except Exception:                               # noqa: BLE001
        pass
    if os.environ.get("HSF_UI_SKIN", "").strip().lower() in ("dark", "1", "true"):
        return "dark"
    return DEFAULT_THEME_ID


#: 主题 id 缓存（dial_colors() 每帧调用 ⇒ 不能每次读设置文件）。
_theme_cache = None


def current_theme_id(force=False):
    """当前主题 id（带缓存；改了设置后用 `refresh_theme()` 失效）。"""
    global _theme_cache
    if _theme_cache is None or force:
        _theme_cache = _detect_theme_id()
    return _theme_cache


def refresh_theme():
    """主题缓存失效（设置项改完 / 外部改了设置文件后调用）。"""
    global _theme_cache
    _theme_cache = None


def current_theme():
    """当前主题定义（dict）。"""
    return THEMES.get(current_theme_id()) or THEMES[DEFAULT_THEME_ID]


def theme_tokens(theme_id=None):
    """某套主题的完整令牌（QSS 渲染用）。"""
    t = THEMES.get(theme_id or current_theme_id()) or THEMES[DEFAULT_THEME_ID]
    tokens = _base_tokens()
    tokens.update(t["tokens"])
    # [微调框字形 2026-11-19] 样式引擎**整体接管** CC_SpinBox（基规则有
    # border/bg ⇒ 代理的 drawComplexControl/drawPrimitive 收不到调用，
    # 离屏实证 CC=0/PE=0）⇒ −/+ 字形只能走 **::up-arrow / ::down-arrow
    # 的 image 规则**（运行时生成细线字形 PNG——与 11-17 combo 的
    # ::down-arrow image 同款机制）。focus 规则会失效 image（11-17 实测
    # 同款怪癖）⇒ spinbox 的 :focus 规则已刻意移除（见 QSS 内注释）。
    tokens["spin_up_rule"] = (
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow"
        " { image: url(\"%s\"); width: 34px; height: 30px; }"
        % _spin_glyph_png("plus", tokens.get("text", "#1A1A1A")))
    tokens["spin_down_rule"] = (
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow"
        " { image: url(\"%s\"); width: 34px; height: 30px; }"
        % _spin_glyph_png("minus", tokens.get("text", "#1A1A1A")))
    tokens["spin_up_rule_disabled"] = (
        "QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled"
        " { image: url(\"%s\"); width: 34px; height: 30px; }"
        % _spin_glyph_png("plus", tokens.get("offline", "#757575")))
    tokens["spin_down_rule_disabled"] = (
        "QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled"
        " { image: url(\"%s\"); width: 34px; height: 30px; }"
        % _spin_glyph_png("minus", tokens.get("offline", "#757575")))
    # [下拉箭头 2026-11-19 用户「下拉框下拉箭头没了」] ::drop-down 无规则
    # 时引擎不画任何箭头（取证：箭头区深色像素 0）⇒ 与微调框同款：给
    # ::down-arrow 走 image 规则（细 chevron PNG，次级文本色）。combo
    # **刻意没有 :focus 规则**（11-17 已定）⇒ image 不会失效。
    tokens["combo_down_rule"] = (
        "QComboBox::down-arrow { image: url(\"%s\"); width: 30px;"
        " height: 28px; }"
        % _spin_glyph_png("chevron_down",
                          tokens.get("text_dim", "#5A6572")))
    tokens["combo_down_rule_disabled"] = (
        "QComboBox::down-arrow:disabled { image: url(\"%s\");"
        " width: 30px; height: 28px; }"
        % _spin_glyph_png("chevron_down",
                          tokens.get("offline", "#757575")))
    return tokens


#: 字形 PNG 缓存目录（懒建；文件按 (kind, color) 命名 ⇒ 跨主题不冲突）。
_GLYPH_DIR = None
_GLYPH_CACHE = {}


def _spin_glyph_png(kind, color):
    """生成微调框步进字形的细线 PNG（透明底；返回文件路径）。

    [为什么是 PNG] 样式引擎接管 CC_SpinBox ⇒ 代理画笔够不着按钮区；
    QSS 的「透明边框三角」在子控件上会被整块填色 ⇒ 黑方块。唯一可靠
    的字形载体 = **image 规则**（11-17 combo 已验证的机制）。
    [字形] minus = 一条横线；plus = 十字；1.6px 圆头（与代理自绘的
    checkbox 勾/chevron 同一套线条语言）。
    """
    global _GLYPH_DIR
    key = (kind, color)
    if key in _GLYPH_CACHE:
        return _GLYPH_CACHE[key]
    if _GLYPH_DIR is None:
        import os
        import tempfile
        _GLYPH_DIR = os.path.join(tempfile.gettempdir(), "hsf_spin_glyphs")
        os.makedirs(_GLYPH_DIR, exist_ok=True)
    # [必须用 QImage 不是 QPixmap 2026-11-19] 模块级 `STYLESHEET =
    # stylesheet(...)` 在 **import theme 时**就生成字形 ⇒ 无 QApplication
    # 的进程（test_theme_matrix 早期 import、离线工具）里 QPixmap 构造会
    # 打 "Must construct a QGuiApplication" 且画不出 ⇒ 字形文件损坏。
    # QImage 允许无 GUI 应用构造/绘制/落盘 ⇒ 全场景安全。
    pm = QImage(34, 30, QImage.Format_ARGB32_Premultiplied)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.Antialiasing, True)
    pen = QPen(QColor(color))
    pen.setWidthF(1.6)
    pen.setCapStyle(Qt.RoundCap)
    p.setPen(pen)
    p.setBrush(Qt.NoBrush)
    # 34x30 画布 = 按钮 rect 尺寸（QSS 里给 ::up-arrow 指定同尺寸 ⇒
    # 引擎把 image 画满按钮区，字形自然居中；image 缺省画在左上角）。
    if kind == "chevron_down":
        # 下拉箭头：细 chevron（V 形折线），居中 30x30 画布。
        from PyQt5.QtCore import QPointF as _QPF
        cx, cy, arm = 15.0, 14.0, 5.0
        p.drawLine(_QPF(cx - arm, cy - 3), _QPF(cx, cy + 2))
        p.drawLine(_QPF(cx, cy + 2), _QPF(cx + arm, cy - 3))
    else:
        # 微调框字形：minus / plus（34x30 画布）。
        p.drawLine(7, 15, 27, 15)
        if kind == "plus":
            p.drawLine(17, 5, 17, 25)
    p.end()
    import os
    path = os.path.join(_GLYPH_DIR, "spin_%s_%s.png"
                        % (kind, str(color).lstrip("#")))
    # [盘上缓存 2026-11-19] 文件已存在（上次运行生成过）⇒ 直接复用，
    # **不重画不重写** —— import（缺省主题）与换主题（其余主题）路径上
    # 零文件写入。>64 字节 = 有效 PNG（本组字形 129~305 字节；板端曾因
    # 「无 QApplication 画 QPixmap」落过坏文件，小文件视为无效重生成）。
    if os.path.exists(path) and os.path.getsize(path) > 64:
        url = path.replace("\\", "/")
        _GLYPH_CACHE[key] = url
        return url
    pm.save(path, "PNG")
    # [QSS url() 必须**正斜杠**] Windows 原生路径的反斜杠在 url("…") 里
    # 是非法转义（\U 等）⇒ 整张样式表 "Could not parse stylesheet"（实测
    # 2026-11-19）。Qt 的资源 URL 统一正斜杠。
    url = path.replace("\\", "/")
    _GLYPH_CACHE[key] = url
    return url


#: 字形是否已预热（进程级；warm_glyph_cache 一次即可，重复调用零开销）。
_glyph_prewarm_done = [False]


def warm_glyph_cache():
    """全部主题的字形 PNG 预热（启动时调用一次 ⇒ 切页/换主题路径零文件 IO）。

    [为什么] theme_tokens() 在**首次遇到某个 (kind,color)** 时会画 PNG 并
    写盘 —— 那发生在「换主题」那一刻（UI 线程，当时正卡着等界面重洗）。
    这里把**全部主题 × 全部字形**一次性生成好（主窗构造后
    QTimer.singleShot 延一拍调用，不占首屏）；文件已存在（上次运行生成
    过）只做一次 stat，不重画不重写。预热之后 _spin_glyph_png 全程
    进程内缓存命中 ⇒ 换主题/建页路径零磁盘写入。
    """
    if _glyph_prewarm_done[0]:
        return
    _glyph_prewarm_done[0] = True
    for tid, _name in THEME_LIST:
        try:
            theme_tokens(tid)               # 触发该主题 6 个 (kind,color)
        except Exception:                   # noqa: BLE001
            pass


def stylesheet(theme_id=None):
    """按主题渲染 QSS。"""
    return _QSS_TEMPLATE % theme_tokens(theme_id)


#: 缺省主题的 QSS（**向后兼容**：老代码直接用 theme.STYLESHEET）。
STYLESHEET = stylesheet(DEFAULT_THEME_ID)


def _tok(key, theme_id=None):
    """当前主题的某个令牌（chrome QSS 用；缺失回缺省主题 ⇒ 不会渲染出空值）。"""
    t = THEMES.get(theme_id or current_theme_id()) or THEMES[DEFAULT_THEME_ID]
    val = t["tokens"].get(key)
    if val is None:
        val = THEMES[DEFAULT_THEME_ID]["tokens"].get(key, "#000000")
    return val


def _chrome(key, theme_id=None):
    """当前主题的某个 chrome 色（顶栏/底栏/导航）。"""
    t = THEMES.get(theme_id or current_theme_id()) or THEMES[DEFAULT_THEME_ID]
    val = t["chrome"].get(key)
    if val is None:
        val = THEMES[DEFAULT_THEME_ID]["chrome"].get(key, "#000000")
    return val


def _palette_for(theme_id=None):
    """按主题构造 QPalette（未写 QSS 的控件/QMenu/QToolTip 也走它）。"""
    t = THEMES.get(theme_id or current_theme_id()) or THEMES[DEFAULT_THEME_ID]
    pal_cfg = t["palette"]
    pal = QPalette()
    pal.setColor(QPalette.Window, QColor(pal_cfg["window"]))
    pal.setColor(QPalette.Base, QColor(pal_cfg["base"]))
    pal.setColor(QPalette.AlternateBase, QColor(pal_cfg["alt_base"]))
    pal.setColor(QPalette.Text, QColor(pal_cfg["text"]))
    pal.setColor(QPalette.WindowText, QColor(pal_cfg["window_text"]))
    pal.setColor(QPalette.ButtonText, QColor(pal_cfg["button_text"]))
    pal.setColor(QPalette.Highlight, QColor(pal_cfg["highlight"]))
    pal.setColor(QPalette.HighlightedText, QColor(pal_cfg["highlighted_text"]))
    # [Mid 角色 = 次要文字色] 页面代码里的说明/次要文字写 `palette(mid)`
    # （QSS 的 palette() 引用）⇒ 换主题时**调色板一变就自动跟随**，
    # 不必逐控件重刷（Mid 角色默认不用于文字 ⇒ 占用无副作用）。
    pal.setColor(QPalette.Mid, QColor(_tok("text_dim", t["id"])))
    # [Link 角色 = 标题色] 卡片/小节标题写 `palette(link)` ⇒ 跟随主题。
    pal.setColor(QPalette.Link, QColor(_tok("title_text", t["id"])))
    return pal


def apply_window(widget, theme_id=None):
    """把某套主题的**大 QSS** 下发到指定窗口（主窗/登录窗构造与换主题时用）。

    [为什么 QSS 挂在窗口而不是 QApplication] 2026-11-17 板端实测：
    `app.setStyleSheet()` 会让 **全部** 1445 个控件重洗一遍、单次 ~9.7s
    （`win.setStyleSheet()` 同样内容只要 1.7s——app 级要走
    QStyleSheetStyle 的全局换装，代价高 5~6 倍）；桌面端 360ms vs ~150ms。
    主题只影响本进程的窗口 ⇒ 逐个窗口下发即可，弹层（QMenu/QToolTip）
    走调色板（大 QSS 里本来就没有菜单规则）。

    [窗口自带额外 QSS] 窗口可把专属规则放进 ``_hsf_extra_qss``（如登录窗
    的标题/错误行颜色）；这里统一拼在大 QSS 后面 ⇒ 换主题重下发时不会把
    专属规则冲掉（直接 setStyleSheet 覆盖是实锅来源）。
    """
    qss = stylesheet(theme_id)
    extra = getattr(widget, "_hsf_extra_qss", "")
    if extra:
        qss = qss + "\n" + extra
    try:
        widget.setStyleSheet(qss)
    except RuntimeError:                            # 已销毁的 C++ 对象
        pass


def install(app, theme_id=None):
    """安装主题（Fusion 样式 + 调色板；**不再设 app 级 QSS**）。

    [为什么用 Fusion] 平台原生样式（Windows/macOS）会忽略一部分样式表
    规则，导致同一份 QSS 在两端观感不同。Fusion 是 Qt 自绘样式，
    QSS 行为最可预期，也是 Qt 官方推荐的「跨平台一致」选项。

    [主题] 传 theme_id 用指定那套；不传用**当前主题**（本机设置里选的
    或环境变量指定的）⇒ 启动即按用户选择装配。

    [大 QSS 去哪了] 由 `apply_window()` 挂到**窗口**上（主窗/登录窗在
    构造时调用）；app 级 QSS 一换就全树换装，是换主题卡顿的主因
    （见 apply_window 注释）。
    """
    # [只有首次才 setStyle] `QApplication.setStyle` 会**重建整个 QStyle
    # 并重刷全部控件**——实测 1445 个控件的窗口单次 ~730ms，占"换主题"
    # 总耗时的 2/3（2026-11-17 实测：apply_theme 1116ms 里 730ms 是它）。
    # 换主题只换 QSS/调色板，样式引擎始终是 Fusion ⇒ 已装就跳过，否则
    # 每次切换都在做无用功（板端 ARM 上被放大成秒级卡顿）。
    # [为什么在 app 上打标记而不是查 className] 设过 QSS 后
    # `app.style()` 是 QStyleSheetStyle 包壳，metaObject().className()
    # 报的是基类（QCommonStyle），查不出 Fusion；QStyle 也没有 name()。
    # 标记挂在 QApplication 实例上 ⇒ 测试里换一个新的 QApplication 时
    # 仍会重新 setStyle（不误跳过）。
    if not getattr(app, "_hsf_fusion_style", False):
        # [触摸友好 2026-11-18] Fusion 的微调框步进箭头又小又淡（≈8px 浅灰）
        # ⇒ 触摸屏上"看不见也点不着"（用户实报微调框"样子有问题"）⇒ 用
        # **代理样式**自绘（其余绘制全部继承 Fusion）。2026-11-18 起代理
        # 升级为 _ModernStyle：步进钮之外还自绘 M3 复选框/单选钮/下拉
        # chevron/去焦点虚线框（见类注释）。
        # [为什么不用两次 setStyle] setStyle 会重建整个 QStyle 并重刷全部
        # 控件（板端 1445 控件 ~730ms）⇒ 一次搞定：代理样式的基座直接用
        # QStyleFactory 造一个 Fusion。
        _base = QStyleFactory.create("Fusion")
        if _base is None:                          # 理论上不会发生
            app.setStyle("Fusion")
        else:
            app.setStyle(_ModernStyle(_base))
        try:
            app._hsf_fusion_style = True
        except Exception:                           # noqa: BLE001
            pass
    app.setPalette(_palette_for(theme_id))


#: PM_RadioButtonLabelSpacing 的枚举号（导入时求值一次；PyQt5 可能没有）。
#: [为什么模块级] _ModernStyle.pixelMetric 是热路径（建页/重洗时每控件
#: 都过一遍）⇒ 不能每次调用都 getattr。
_PM_RADIO_SP = getattr(QStyle, "PM_RadioButtonLabelSpacing", None)


class _ModernStyle(QProxyStyle):
    """现代化控件画笔（2026-11-18 Material Design 3 / 移动端口径）。

    [前身 _TouchSpinStyle] 原来只管微调框的触屏数量选择器布局；本轮把
    **复选框 / 单选钮 / 下拉箭头 / 焦点框**一并接过来，统一成一套
    "圆形嵌入式按钮 + 细线条字形" 的移动端语言（用户 2026-11-18
    「其他控件也要美化下，按钮，多选框等，都现代化移动 ui」）。
    除下列自绘项外，全部绘制继承 Fusion ⇒ 既有观感零回归。

    ---- 自绘清单 ----
    * 微调框（原 _TouchSpinStyle 全部能力）：左 − 右 ＋ 数量选择器布局 +
      嵌入式圆形步进钮（见 _spin_button_w / subControlRect / _draw_spin_glyph）；
    * PE_IndicatorCheckBox：M3 复选框——圆角方块（4px 半径），
      未选中 = 白底 + 2px 软描边（hover 变主色）、选中 = 主色填充 +
      白色对勾（1.8px 圆头折线）、三态 = 主色填充 + 白色短横、
      禁用 = disabled_bg 底 + 灰字形（勾仍可见，"禁用+选中"不塌成
      "没选中"——数字量页 2026-11-12 的教训）；
    * PE_IndicatorRadioButton：M3 单选钮——未选中 2px 描边圆、
      选中 = 主色外圈 2px + 主色实心内圆点、禁用变灰；
    * PE_IndicatorArrowDown/Up/Left/Right：细 chevron（1.6px 圆头折线，
      色随主题）。**注意归属**：QComboBox 因为 QSS 给它设了
      border/background 被样式引擎整体接管 ⇒ 它的下拉箭头**到不了**这里
      （引擎自绘实心三角、色 = border 令牌，见 QSS 模板 QComboBox 段的
      取证注释）；画笔 chevron 服务于没被接管的箭头（表头排序、树、
      工具钮菜单等）。原先的「运行时生成 PNG 当 ::down-arrow」方案已
      整体移除（image 在重 polish 时会被样式引擎失效——2026-11-17
      「箭头怎没了」实锅）；
    * PE_FrameFocusRect：**不画**（去掉键盘焦点虚线框，移动端观感）。
      键盘可达性在触屏产品里由"点按即焦点"承担，可接受。

    [为什么 QSS 里不写 ::indicator / ::down-arrow] Qt 样式引擎一旦给
    子控件设了 border/background，就**不再调用基样式的对应图元**——
    画笔自绘必须走"QSS 完全不碰该子控件"这条路（2026-11-17 两个实锅，
    见 QSS 模板里的注释）。
    [PM_IndicatorWidth/Height] 18px 见方（M3 触摸口径；Fusion 缺省约
    13px）。经离屏探针实测 QStyleSheetStyle 无 ::indicator 规则时会向
    基样式取这两个 metric ⇒ 代理返回 18 即全局生效（探针路径见
    .zcode/tmp/probe_modern_ui.py 的结论，2026-11-18）。
    """

    #: 步进钮宽度（px）。板端是触摸屏 ⇒ 手指要按得中（全框高 × 34px）。
    BUTTON_W = 34

    # ---- 指示器尺寸（M3 触摸口径；QSS 不写 ::indicator ⇒ 走这里）----
    # [快速路径 2026-11-19] pixelMetric 在**建页/换主题重洗**时每个控件都
    # 被调几千次（远端设置页一次构树 ~3000 次）⇒ 这里必须零开销：单选钮
    # 间距的枚举号在模块导入时求值一次（原先每次调用都 getattr 一遍）。
    def pixelMetric(self, metric, option=None, widget=None):
        if metric == QStyle.PM_IndicatorWidth:
            return INDICATOR_SIZE
        if metric == QStyle.PM_IndicatorHeight:
            return INDICATOR_SIZE
        if metric == QStyle.PM_CheckBoxLabelSpacing:
            return 8
        # 单选钮的文字间距（PyQt5 的 QStyle 没有这个枚举项时忽略）。
        if _PM_RADIO_SP is not None and metric == _PM_RADIO_SP:
            return 8
        return QProxyStyle.pixelMetric(self, metric, option, widget)

    def drawPrimitive(self, element, option, painter, widget=None):
        if element in (QStyle.PE_IndicatorSpinUp, QStyle.PE_IndicatorSpinDown):
            self._draw_spin_glyph(element, option, painter, widget)
            return
        if element == QStyle.PE_IndicatorCheckBox:
            self._draw_checkbox(option, painter)
            return
        if element == QStyle.PE_IndicatorRadioButton:
            self._draw_radio(option, painter)
            return
        if element in (QStyle.PE_IndicatorArrowDown, QStyle.PE_IndicatorArrowUp,
                       QStyle.PE_IndicatorArrowLeft,
                       QStyle.PE_IndicatorArrowRight):
            self._draw_chevron(element, option, painter)
            return
        if element == QStyle.PE_FrameFocusRect:
            # 移动端观感：不画键盘焦点虚线框（可达性取舍见类注释）。
            return
        QProxyStyle.drawPrimitive(self, element, option, painter, widget)

    def drawComplexControl(self, control, option, painter, widget=None):
        """微调框：让 Fusion 画**无按钮版**框，按钮字形完全自管。

        [2026-11-18 用户「加减按钮好像还有边框，盖住了微调框显示文字的
        大边框」] Fusion 的 CC_SpinBox 在编辑区与步进钮之间画**自带的
        分隔线/按钮边框**（它在 QSS 布局的按钮 rect 上补画原生按钮面板）
        ⇒ 这些线就是用户看到的"加减按钮的边框"。
        [修法] 把 buttonSymbols 改成 NoButtons 再交给 Fusion ⇒ 它画
        "无按钮版"：只有框与编辑区（无分隔线无按钮边框）；−/+ 字形
        （含 hover/按下底块反馈）由 _draw_spin_glyph 自己画。
        """
        if control == QStyle.CC_SpinBox:
            opt = QStyleOptionSpinBox(option)
            opt.buttonSymbols = QAbstractSpinBox.NoButtons
            QProxyStyle.drawComplexControl(self, control, opt, painter,
                                           widget)
            for element, sc in ((QStyle.PE_IndicatorSpinDown,
                                 QStyle.SC_SpinBoxDown),
                                (QStyle.PE_IndicatorSpinUp,
                                 QStyle.SC_SpinBoxUp)):
                o = QStyleOptionSpinBox(option)
                o.rect = self.subControlRect(QStyle.CC_SpinBox, option, sc,
                                             widget)
                o.activeSubControls = sc
                self._draw_spin_glyph(element, o, painter, widget)
            return
        QProxyStyle.drawComplexControl(self, control, option, painter,
                                       widget)

    def subControlRect(self, control, option, role, widget=None):
        """数量选择器布局：左 −（全高）右 ＋（全高）中间编辑区。"""
        if control == QStyle.CC_SpinBox and role in (
                QStyle.SC_SpinBoxUp, QStyle.SC_SpinBoxDown,
                QStyle.SC_SpinBoxEditField, QStyle.SC_SpinBoxFrame):
            frame = QProxyStyle.subControlRect(self, control, option,
                                               QStyle.SC_SpinBoxFrame, widget)
            w = self.BUTTON_W
            if role == QStyle.SC_SpinBoxDown:
                return QRect(frame.left() + 1, frame.top() + 1, w,
                             frame.height() - 2)
            if role == QStyle.SC_SpinBoxUp:
                return QRect(frame.right() - w, frame.top() + 1, w,
                             frame.height() - 2)
            # 编辑区：两侧各让出一个按钮（基类不知道左边有按钮 ⇒ 自己算）
            return QRect(frame.left() + w, frame.top() + 1,
                         frame.width() - 2 * w, frame.height() - 2)
        return QProxyStyle.subControlRect(self, control, option, role, widget)

    def _draw_spin_glyph(self, element, option, painter, widget=None):
        """画**分段式步进钮**（iOS UIStepper / antd InputNumber 观感）。

        [2026-11-18 用户「微调框跟其他的不是一个画风」] 上一版"白框里嵌
        两个**正圆**灰钮"——正圆与全场的圆角方/胶囊语言不同 ⇒ 格格不入。
        现在改成**分段式**：按钮区浅灰底块**贴边填满**（左右段把框分成
        三段），段间 1px 分隔线，**外侧两角与框同圆角**（RADIUS_INPUT）
        ⇒ 与输入框同框同描边同圆角，看起来是"一个控件的三段"。
        [状态] 常态**完全透明**（白框里只有 −/+ 字形，用户 2026-11-18
        「加减按钮背景调成透明」）；hover/按下时**内嵌圆角底块**浮现
        （主色 8% / 12%）——**四周各内缩 2px** ⇒ 永远不碰框边框（用户
        「还有边框盖住了底下的大边框」的教训：贴边块/分隔线都会压线）。
        禁用也只有字形变灰，不画底。
        """
        rect = option.rect
        enabled = bool(option.state & QStyle.State_Enabled)
        sc = getattr(option, "activeSubControls", QStyle.SC_None)
        is_up = element == QStyle.PE_IndicatorSpinUp
        mine = (QStyle.SC_SpinBoxUp if is_up else QStyle.SC_SpinBoxDown)
        if enabled:
            if (option.state & QStyle.State_Sunken) and (sc & mine):
                fill = QColor(_tok("press_bg"))
            elif option.state & QStyle.State_MouseOver:
                fill = QColor(_tok("hover_bg"))
            else:
                fill = None                     # 常态透明（用户口径）
        else:
            fill = None                         # 禁用也透明：只有字形变灰
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing, True)
        # ---- hover/按下底块（**内嵌**，四周留 2px 白边 ⇒ 不盖框边框）----
        r = float(max(2, RADIUS_INPUT - 2))
        x, y = float(rect.left()) + 2.0, float(rect.top()) + 2.0
        w, h = float(rect.width()) - 4.0, float(rect.height()) - 4.0
        path = QPainterPath()
        path.addRoundedRect(QRectF(x, y, w, h), r, r)
        painter.setPen(Qt.NoPen)
        painter.setBrush(Qt.NoBrush)
        if fill is not None:
            painter.setBrush(fill)
            painter.drawPath(path)
        # [2026-11-18 用户「去掉加减的按钮边框」] 段间分隔线**不画**——
        # 两条竖线看着像按钮自带边框、把大框切成三截。hover/按下时底色块
        # 浮现本身已标出按钮区（无静态边框，反馈不丢）。
        # ---- − / ＋ 字形（细、居中、与按钮文字同权重）----
        cx = rect.center().x() + 0.5
        cy = rect.center().y() + 0.5
        arm = 4.0
        color = QColor(_tok("text") if enabled else _tok("offline"))
        pen = QPen(color)
        pen.setWidthF(1.6)
        pen.setCapStyle(Qt.RoundCap)
        painter.setPen(pen)
        painter.setBrush(Qt.NoBrush)
        painter.drawLine(QPointF(cx - arm, cy), QPointF(cx + arm, cy))
        if element == QStyle.PE_IndicatorSpinUp:
            painter.drawLine(QPointF(cx, cy - arm), QPointF(cx, cy + arm))
        painter.restore()

    def _draw_checkbox(self, option, painter):
        """M3 复选框：圆角方块，选中 = 主色填充 + 白勾（细节见类注释）。"""
        st = option.state
        on = bool(st & QStyle.State_On)
        tri = bool(st & QStyle.State_NoChange)
        enabled = bool(st & QStyle.State_Enabled)
        hover = bool(st & QStyle.State_MouseOver) and enabled
        filled = on or tri
        if not enabled:
            # 禁用：底灰、描边灰；勾/短横用 offline 灰——「禁用+已选中」
            # 仍要看得见（对齐 QSS 里 QPushButton:disabled:checked 的口径）。
            fill = QColor(_tok("disabled_bg"))
            stroke = QColor(_tok("border_soft"))
            glyph = QColor(_tok("offline"))
        elif filled:
            fill = QColor(_tok("accent_fill"))
            stroke = fill
            glyph = QColor("#FFFFFF")
        else:
            fill = QColor(_tok("white"))
            stroke = QColor(_tok("accent_fill") if hover
                            else _tok("border_soft"))
            glyph = None
        box = QRect(option.rect).adjusted(1, 1, -1, -1)
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(stroke)
        pen.setWidthF(2.0)
        painter.setPen(pen)
        painter.setBrush(fill)
        painter.drawRoundedRect(box, 4, 4)
        if filled:
            w, h = box.width(), box.height()
            gp = QPen(glyph)
            gp.setWidthF(1.8)
            gp.setCapStyle(Qt.RoundCap)
            gp.setJoinStyle(Qt.RoundJoin)
            painter.setPen(gp)
            painter.setBrush(Qt.NoBrush)
            if tri:
                # 三态（部分选中）：居中短横。
                painter.drawLine(QPointF(box.left() + w * 0.28,
                                         box.center().y() + 0.5),
                                 QPointF(box.left() + w * 0.72,
                                         box.center().y() + 0.5))
            else:
                painter.drawPolyline(QPolygonF([
                    QPointF(box.left() + w * 0.24, box.top() + h * 0.54),
                    QPointF(box.left() + w * 0.44, box.top() + h * 0.74),
                    QPointF(box.left() + w * 0.78, box.top() + h * 0.28),
                ]))
        painter.restore()

    def _draw_radio(self, option, painter):
        """M3 单选钮：选中 = 主色外圈 + 主色实心圆点（细节见类注释）。"""
        st = option.state
        on = bool(st & QStyle.State_On)
        enabled = bool(st & QStyle.State_Enabled)
        hover = bool(st & QStyle.State_MouseOver) and enabled
        if not enabled:
            fill = QColor(_tok("disabled_bg"))
            stroke = QColor(_tok("border_soft"))
            dot = QColor(_tok("offline"))
        elif on:
            fill = QColor(_tok("white"))
            stroke = QColor(_tok("accent_fill"))
            dot = QColor(_tok("accent_fill"))
        else:
            fill = QColor(_tok("white"))
            stroke = QColor(_tok("accent_fill") if hover
                            else _tok("border_soft"))
            dot = None
        box = QRect(option.rect).adjusted(1, 1, -1, -1)
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(stroke)
        pen.setWidthF(2.0)
        painter.setPen(pen)
        painter.setBrush(fill)
        painter.drawEllipse(box)
        if dot is not None:
            painter.setPen(Qt.NoPen)
            painter.setBrush(dot)
            r = box.width() * 0.25
            c = QPointF(box.center().x() + 0.5, box.center().y() + 0.5)
            painter.drawEllipse(c, r, r)
        painter.restore()

    def _draw_chevron(self, element, option, painter):
        """细 chevron 箭头（1.6px 圆头折线，色随主题；见类注释）。"""
        rect = option.rect
        if rect.isEmpty():
            return
        enabled = bool(option.state & QStyle.State_Enabled)
        color = QColor(_tok("text_dim") if enabled else _tok("offline"))
        cx = rect.center().x() + 0.5
        cy = rect.center().y() + 0.5
        dx = rect.width() * 0.30
        dy = rect.height() * 0.30
        if element == QStyle.PE_IndicatorArrowDown:
            pts = [(cx - dx, cy - dy), (cx, cy + dy), (cx + dx, cy - dy)]
        elif element == QStyle.PE_IndicatorArrowUp:
            pts = [(cx - dx, cy + dy), (cx, cy - dy), (cx + dx, cy + dy)]
        elif element == QStyle.PE_IndicatorArrowRight:
            pts = [(cx - dy, cy - dx), (cx + dy, cy), (cx - dy, cy + dx)]
        else:                                   # PE_IndicatorArrowLeft
            pts = [(cx + dy, cy - dx), (cx - dy, cy), (cx + dy, cy + dx)]
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(color)
        pen.setWidthF(1.6)
        pen.setCapStyle(Qt.RoundCap)
        pen.setJoinStyle(Qt.RoundJoin)
        painter.setPen(pen)
        painter.setBrush(Qt.NoBrush)
        painter.drawPolyline(QPolygonF([QPointF(x, y) for x, y in pts]))
        painter.restore()


def text_color():
    """当前主题的**主文字色**（自绘面用；QSS 里请写 `palette(text)`）。"""
    return _tok("text")


def grid_line_color():
    """表格网格线色（当前主题令牌）。"""
    return _tok("grid_line")


def border_soft_color():
    """表格行分隔线色（当前主题令牌；网页端 `.table td` 用的是 border-soft）。"""
    return _tok("border_soft")


def header_bg_color():
    """表头底色（当前主题令牌；网页端 `.table th` 用的是 header-bg）。"""
    return _tok("header_bg")


def do_toggle_qss():
    """DO 切换按钮的**控件级**样式表（对齐网页端 `.do-toggle`）。

    [为什么必须控件级 —— 用户实报「输出点开关的颜色区别太小」]
        大 QSS 挂在**页面**上，而卡片（``QFrame#HsfCard``）自己带样式表 ⇒
        **祖先样式表不再级联到卡片里的控件**（Qt 的样式表作用域规则）⇒
        卡片里的 DO 按钮拿不到 ``QPushButton:checked`` 那几条规则 ⇒
        「通」态被画成 Fusion 的灰面，与「断」几乎看不出差别（实测截图：
        通/断两色都是灰白，禁用时更是一片灰）。
    ⇒ 卡片内的控件必须**自带**样式表（表格同理，见 page_base.DataTable）。

    [配色逐条对齐网页端 theme.css]
        断 = 面板底 + 灰边；通 = 浅蓝底 + 蓝边 + 深色字；
        通且禁用（自动模式）= 浅绿底 + 绿边 + 绿字（禁用时也要看得出通）。
    """
    t = theme_tokens(current_theme_id())
    return """
QPushButton#HsfDoToggle {
    padding: %(doPadV)dpx 8px;
    font-size: %(fz13)dpx;
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    color: %(text)s;
}
QPushButton#HsfDoToggle:hover {
    background-color: %(hover_bg)s;
    border-color: %(accent_line)s;
}
QPushButton#HsfDoToggle:checked {
    background-color: %(press_bg)s;
    border-color: %(accent_line)s;
    color: %(text)s;
}
QPushButton#HsfDoToggle:disabled {
    background-color: %(disabled_bg)s;
    border-color: %(border_soft)s;
    color: %(offline)s;
}
QPushButton#HsfDoToggle:disabled:checked {
    background-color: %(ok_soft)s;
    border-color: %(ok)s;
    color: %(ok)s;
}
""" % t


def text_dim_color():
    """当前主题的**次要文字色**（自绘面用；QSS 里请写 `palette(mid)`）。"""
    return _tok("text_dim")


def apply_theme(app, theme_id, persist=False):
    """**即时**切换主题（本地设置里改主题时调用）。

    :param persist: True 时把选择写进本机设置（客户端 INI）。
    :returns: 实际生效的主题 id（非法 id ⇒ 回落缺省）。
    """
    tid = theme_id if theme_id in THEMES else DEFAULT_THEME_ID
    if persist:
        try:
            from . import settings as ui_settings
            ui_settings.set_value(ui_settings.K_THEME, tid)
        except Exception:                           # noqa: BLE001
            pass
    global _theme_cache
    _theme_cache = tid
    install(app, tid)                   # 调色板（+ 首次 Fusion），不动 app 级 QSS
    _theme_gen[0] += 1                  # 页面/窗口据此判断"要不要重刷"
    # [逐窗口下发] app 级 setStyleSheet 会让全部窗口树重洗一遍（板端
    # 1445 控件实测 9.7s）。主窗由自己的 refresh_theme() 只刷 chrome +
    # **当前页**（其余页面在切入时补刷，见 MainWindow.goto_page）；其它
    # 顶层窗口（登录窗等）整窗重下发（控件少，便宜）。
    for w in list(app.topLevelWidgets()):
        try:
            if not w.isWindow():
                continue
            # [跳过 Qt 内部弹层窗口] QComboBox 的私有 popup 容器
            # （QComboBoxPrivateContainer）全部是顶层窗口——一个界面几十个
            # 下拉就有几十个，逐个下发大 QSS 板端实测多花 ~0.55s，而它们
            # 通过 parentWidget 链**继承所属下拉框的样式表**（下拉框在页面
            # 里，页面 QSS 一变它们跟着变）⇒ 不用单独下发。
            if w.windowType() == Qt.Popup:
                continue
            # [自管样式的顶层窗口] 软键盘是"固定手机键盘外观"（灰蓝底/
            # 白键，不跟主题走），自己设了 QSS ⇒ 不许用大全表覆盖它。
            if getattr(w, "_hsf_own_qss", False):
                continue
            refresh = getattr(w, "refresh_theme", None)
            if callable(refresh):
                refresh(tid)
            else:
                apply_window(w, tid)
                refresh_widgets(app, root=w)
        except RuntimeError:                        # 窗口已销毁
            continue
    return tid


#: 主题代数：每次 apply_theme +1。页面记录自己上次装配的代数，切入时
#: 发现落后就补刷 QSS（换主题时只刷当前页，隐藏页懒刷新）。
_theme_gen = [0]


def generation():
    """当前主题代数（页面懒刷新判据）。"""
    return _theme_gen[0]


#: chrome 部件（objectName → QSS 取值函数）；换主题后要重下发。
_CHROME_QSS = {
    "HsfTitleBar": lambda: title_bar_qss(),
    "HsfStatusBar": lambda: status_bar_qss(),
    "HsfNavPanel": lambda: nav_panel_qss(),
    "HsfCard": lambda: card_qss(),
}


def refresh_widgets(app, root=None):
    """主题切换后重刷 chrome 部件（样式表变了 ⇒ 控件要重新 polish）。

    [为什么要手动刷] 顶栏 / 底栏 / 导航 / 卡片各自持有一份 QSS（构造时
    设的）；大 QSS 挂在窗口/页面上，**同名控件自己的 QSS 优先级更高**
    ⇒ 换主题后必须重新下发，否则它们还是旧颜色。

    :param root: 只刷这棵子树（主窗传当前页 / chrome；None = 所有顶层窗口）。
        换主题时隐藏页不刷（板端 54 张卡片 ~1.2s），切入时按代数补刷
        （见 MainWindow.goto_page）。

    [自绘面] 表盘等自绘控件每次 paint 取 `dial_colors()` ⇒ 自动跟随。
    """
    from PyQt5.QtWidgets import QWidget
    if root is not None:
        roots = [root] + list(root.findChildren(QWidget))
    else:
        roots = []
        for top in app.topLevelWidgets():
            roots.append(top)
            try:
                roots.extend(top.findChildren(QWidget))
            except RuntimeError:                    # 已销毁
                continue
    for w in roots:
        try:
            name = w.objectName()
        except RuntimeError:                        # 已销毁的 C++ 对象
            continue
        fn = _CHROME_QSS.get(name)
        if fn is None:
            continue
        try:
            w.setStyleSheet(fn())
        except RuntimeError:                        # 已销毁
            continue


# ============================================================================
#                          便捷构造小工具
# ============================================================================

def title_bar_qss():
    """顶栏样式（深蓝底白字，独立于全局表——chrome 不随控件主题变）。

    [全部用后代选择器限定在 #HsfTitleBar 内] 否则这些规则会泄漏到
    其它控件（本控件的样式表向下级联整棵子树）。
    """
    return """
QWidget#HsfTitleBar {
    background-color: %(brand)s;
}
QWidget#HsfTitleBar QLabel {
    color: #FFFFFF;
    background: transparent;
}
QWidget#HsfTitleBar QWidget {
    background: transparent;
}
""" % {"brand": _chrome("brand")}


def status_bar_qss():
    """底部状态栏样式（黑底浅灰字 + 右侧账户按钮/菜单）。

    [必须 ID 限定] 裸 `QWidget { background }` 会级联把整棵 chrome 刷灰
    （widgets.py:135-139 / 495-500 实锅）。账户按钮是 QPushButton，不受
    `QWidget#HsfStatusBar QLabel` 规则影响，故单独给一条同配色规则。
    [字号走 font_px] 源码里不写小字号的裸字面量（D1 扫描锁会把
    11/12/13 这类未缩放原始值判成假红），按全局比例换算后插值。
    """
    return """
QWidget#HsfStatusBar {
    background-color: %(bg)s;
}
QWidget#HsfStatusBar QLabel {
    color: #D0D0D0;
    background: transparent;
}
QWidget#HsfStatusBar QPushButton#HsfAccountBtn {
    color: #D0D0D0;
    background: transparent;
    border: none;
    padding: 2px 6px;
    font-size: %(fz)dpx;
}
QWidget#HsfStatusBar QPushButton#HsfAccountBtn:hover {
    color: #FFFFFF;
    background-color: rgba(255,255,255,0.12);
    border-radius: 3px;
}
QMenu {
    background-color: %(menu_bg)s;
    color: %(menu_fg)s;
    border: 1px solid %(menu_border)s;
    font-size: %(fz)dpx;
}
QMenu::item:selected { background-color: %(menu_sel)s; }
""" % {"bg": _chrome("status_bg"), "fz": font_px(13),
       "menu_bg": _tok("white"), "menu_fg": _tok("text"),
       "menu_border": _tok("border_sub"),
       "menu_sel": _tok("hover_bg")}


def nav_panel_qss():
    """左侧导航面板样式（浅灰底 + 选中高亮）。"""
    return """
QWidget#HsfNavPanel {
    background-color: %(nav_bg)s;
}
QWidget#HsfNavContent {
    background-color: %(nav_bg)s;
}
QLabel#HsfNavTitle {
    color: %(caption)s;
    padding: 8px 12px;
    font-weight: bold;
    background: transparent;
}
QPushButton#HsfNavItem {
    background-color: transparent;
    border: none;
    border-radius: 4px;
    padding: 8px 12px;
    text-align: left;
    color: %(text)s;
}
QPushButton#HsfNavItem:hover {
    background-color: %(hover_bg)s;
}
QPushButton#HsfNavItem:checked {
    background-color: %(nav_sel)s;
    color: %(accent_line)s;
    font-weight: bold;
}
""" % {"nav_bg": _chrome("nav_bg"), "hover_bg": _tok("hover_bg"),
       "nav_sel": _chrome("nav_sel"),
       "accent_line": _tok("accent_line"),
       "text": _tok("text"), "caption": _tok("border")}


def card_qss():
    """卡片容器样式（白底 + 软描边 + 5px 圆角）。"""
    return """
QFrame#HsfCard {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius)dpx;
}
""" % {"white": _tok("white"), "border_soft": _tok("border_soft"),
       "radius": RADIUS_CARD}


def value_label_qss(color=None, size=20, bold=True):
    """数值大字号标签（监视页的核心读数）。

    [颜色跟主题] 缺省用当前主题的主文字色（深色主题下不再是深灰字
    压在深底上看不见）⇒ 传 color 才覆盖。
    """
    return ("QLabel { color: %s; font-size: %dpx; %s background: transparent; }"
            % (color or text_color(), size,
               "font-weight: bold;" if bold else ""))


def caption_label_qss():
    """灰色小字说明标签（颜色跟主题 ⇒ 深色主题下也看得清）。"""
    return ("QLabel { color: palette(mid); font-size: %dpx; background: transparent; }"
            % font_px(12))


def disabled_button_qss():
    """按钮**禁用态**的本地 QSS（只写 :disabled；常态仍走主题）。

    [为什么需要本地这条 2026-11-18 实锅] 大 QSS 挂在页面栈容器上、页面
    继承 ⇒ 主题里的 `QPushButton:disabled` **不一定**落到具体按钮上：
    真实窗口实测「上移/下移」被禁用后与可用按钮**像素完全一致**
    （底色 #FFFFFF、字色 #1A1A1A），用户根本看不出"已经是最上/最下了"。
    ⇒ 给需要表达禁用态的按钮**本地**补一条 :disabled 规则（就近优先，
    只声明禁用态 ⇒ 常态/hover/pressed 仍走主题，不会被冲掉）。
    """
    return ("QPushButton:disabled { background-color: %s; "
            "border: 1px solid %s; color: %s; }"
            % (_tok("disabled_bg"), _tok("border_soft"), _tok("offline")))
