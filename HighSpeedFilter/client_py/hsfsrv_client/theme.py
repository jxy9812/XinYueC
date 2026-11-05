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

from PyQt5.QtGui import QColor, QFont, QPalette

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
CLR_WARN = "#ED6C02"      # 告警（橙）
CLR_FAULT = "#C62828"     # 故障（红）
CLR_OFFLINE = "#757575"   # 离线（灰）
CLR_INFO = "#1A66D0"      # 信息（蓝）

# ============================================================================
#                          度量（与 C 端同源）
# ============================================================================

TITLE_BAR_H = 40          #: 顶栏高（C 端 HSF_TITLE_BAR_H）
STATUS_BAR_H = 28         #: 底部状态栏高
NAV_MIN_W = 120           #: 导航面板最小宽
NAV_DEFAULT_W = 176       #: 导航面板缺省宽
SPLITTER_HANDLE_W = 8     #: 分割条把手宽
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
    """装全局字体（必须在建控件之前调用）。"""
    family = pick_font_family()
    font = QFont(family, size) if family else QFont()
    if not family:
        font.setPointSize(size)
    app.setFont(font)
    # [全局 QSS 兜底] 有些控件（含 QSS 自绘文本）不吃 app.setFont，
    # 在样式表里再钉一次族名，保证全部控件同字体。
    if family:
        app.setStyleSheet(
            app.styleSheet()
            + '\n* { font-family: "%s"; }\n' % family)
    return family


# ============================================================================
#                          样式表
# ============================================================================

#: 完整样式表。规则体逐字照搬 C 端 HsfThemeCss.h，
#: 只把选择器名换成 Qt 对应类（XGui 类名 → Qt 类名）。
STYLESHEET = """
/* ==================== 全局 ==================== */
/* [不加 `QWidget { color: ... }`] 裸 QWidget 规则会级联到**所有**子控件，
   包括深蓝标题栏和白字标签——实测把顶栏文字压成灰黑不可读。
   正文色交给调色板（WindowText/Text），只对具体控件类设色。 */
QMainWindow, QDialog {
    background-color: %(page_bg)s;
}

/* ==================== QPushButton：常态/悬停/焦点/按下/禁用 ==================== */
QPushButton {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    padding: 6px 14px;
    min-height: 18px;
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

/* ---- 数字量 DO 切换按钮：统一固定行高 ---- */
/*
 * [为什么要单独一条] 通用 QPushButton 有 padding:6px 14px + min-height:18px，
 * 样式表算出的 sizeHint 会压过 setFixedHeight：单行文字 33px、两行 51px，
 * 而 setFixedHeight(46) 只约束了 maximum —— 结果同一行按钮高度 46/33 参差。
 * 这条把内边距改小并把高度区间收紧，两行文字（DO 号 + 语义名）也放得下。
 */
QPushButton#HsfDoToggle {
    padding: 4px 8px;
    min-height: 44px;
    max-height: 46px;
    font-size: 12px;
}

/* ==================== QLineEdit ==================== */
QLineEdit {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    padding: 5px 8px;
    selection-background-color: #2E7CE8;
}
QLineEdit:hover {
    background-color: %(white)s;
    border: 1px solid %(accent_fill)s;
}
QLineEdit:focus {
    background-color: %(white)s;
    border: 1px solid %(accent_line)s;
}
QLineEdit:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}

/* ==================== QSpinBox / QDoubleSpinBox ==================== */
QSpinBox, QDoubleSpinBox {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    padding: 4px 6px;
}
QSpinBox:hover, QDoubleSpinBox:hover {
    border: 1px solid %(accent_fill)s;
}
QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid %(accent_line)s;
}
QSpinBox:disabled, QDoubleSpinBox:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}

/* ==================== QComboBox ==================== */
QComboBox {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    border-radius: %(radius_ctrl)dpx;
    padding: 5px 8px;
    min-height: 18px;
}
QComboBox:hover {
    border: 1px solid %(accent_fill)s;
}
QComboBox:focus {
    border: 1px solid %(accent_line)s;
}
QComboBox:disabled {
    background-color: %(disabled_bg)s;
    border: 1px solid %(border_soft)s;
    color: %(offline)s;
}
QComboBox QAbstractItemView {
    background-color: %(white)s;
    border: 1px solid %(border)s;
    selection-background-color: %(press_bg)s;
    selection-color: #1A1A1A;
    outline: none;
}

/* ==================== QCheckBox / QRadioButton ==================== */
QCheckBox, QRadioButton {
    spacing: 6px;
}
QCheckBox:disabled, QRadioButton:disabled {
    color: %(offline)s;
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
    color: #1A1A1A;
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
    alternate-background-color: #FAFBFC;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius_card)dpx;
    gridline-color: #E4E8EC;
    selection-background-color: %(press_bg)s;
    selection-color: #1A1A1A;
}
QHeaderView::section {
    background-color: #EFF3F7;
    border: none;
    border-right: 1px solid #E4E8EC;
    border-bottom: 1px solid #D5DBE2;
    padding: 6px 8px;
    color: #1A1A1A;
    font-weight: bold;
}
QHeaderView::section:last {
    border-right: none;
}
QTableCornerButton::section {
    background-color: #EFF3F7;
    border: none;
}

/* ==================== QScrollBar ==================== */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #C2CAD2;
    border-radius: 5px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: %(border_sub)s;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #C2CAD2;
    border-radius: 5px;
    min-width: 24px;
}
QScrollBar::handle:horizontal:hover {
    background: %(border_sub)s;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
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
QSlider::groove:horizontal {
    height: 4px;
    background: #D5DBE2;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: %(accent_fill)s;
    border: 1px solid %(accent_line)s;
    width: 14px;
    margin: -6px 0;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover {
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
""" % {
    "white": CLR_WHITE,
    "hover_bg": CLR_HOVER_BG,
    "press_bg": CLR_PRESS_BG,
    "accent_fill": CLR_ACCENT_FILL,
    "accent_line": CLR_ACCENT_LINE,
    "border": CLR_BORDER,
    "border_sub": CLR_BORDER_SUB,
    "border_soft": CLR_BORDER_SOFT,
    "disabled_bg": CLR_DISABLED_BG,
    "page_bg": COLOR_PAGE_BG,
    "offline": CLR_OFFLINE,
    "radius_ctrl": RADIUS_CTRL,
    "radius_card": RADIUS_CARD,
    "splitter_w": SPLITTER_HANDLE_W,
}


def install(app):
    """安装主题（Fusion 样式 + 样式表 + 调色板）。

    [为什么用 Fusion] 平台原生样式（Windows/macOS）会忽略一部分样式表
    规则，导致同一份 QSS 在两端观感不同。Fusion 是 Qt 自绘样式，
    QSS 行为最可预期，也是 Qt 官方推荐的「跨平台一致」选项。
    """
    app.setStyle("Fusion")
    # 调色板：让未写 QSS 的控件（如 QMenu/QToolTip）也走亮色。
    pal = QPalette()
    pal.setColor(QPalette.Window, QColor(COLOR_PAGE_BG))
    pal.setColor(QPalette.Base, QColor(CLR_WHITE))
    pal.setColor(QPalette.AlternateBase, QColor("#FAFBFC"))
    pal.setColor(QPalette.Text, QColor("#1A1A1A"))
    pal.setColor(QPalette.WindowText, QColor("#1A1A1A"))
    pal.setColor(QPalette.ButtonText, QColor("#1A1A1A"))
    pal.setColor(QPalette.Highlight, QColor(CLR_ACCENT_FILL))
    pal.setColor(QPalette.HighlightedText, QColor("#FFFFFF"))
    app.setPalette(pal)
    app.setStyleSheet(STYLESHEET)


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
""" % {"brand": COLOR_BRAND}


def status_bar_qss():
    """底部状态栏样式（黑底浅灰字）。"""
    return """
QWidget#HsfStatusBar {
    background-color: %(bg)s;
}
QWidget#HsfStatusBar QLabel {
    color: #D0D0D0;
    background: transparent;
}
""" % {"bg": COLOR_STATUS_BG}


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
    color: #5A6572;
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
    color: #1A1A1A;
}
QPushButton#HsfNavItem:hover {
    background-color: %(hover_bg)s;
}
QPushButton#HsfNavItem:checked {
    background-color: %(nav_sel)s;
    color: %(accent_line)s;
    font-weight: bold;
}
""" % {"nav_bg": COLOR_NAV_BG, "hover_bg": CLR_HOVER_BG,
       "nav_sel": COLOR_NAV_SEL, "accent_line": CLR_ACCENT_LINE}


def card_qss():
    """卡片容器样式（白底 + 软描边 + 5px 圆角）。"""
    return """
QFrame#HsfCard {
    background-color: %(white)s;
    border: 1px solid %(border_soft)s;
    border-radius: %(radius)dpx;
}
""" % {"white": CLR_WHITE, "border_soft": CLR_BORDER_SOFT,
       "radius": RADIUS_CARD}


def value_label_qss(color="#1A1A1A", size=20, bold=True):
    """数值大字号标签（监视页的核心读数）。"""
    return ("QLabel { color: %s; font-size: %dpx; %s background: transparent; }"
            % (color, size, "font-weight: bold;" if bold else ""))


def caption_label_qss():
    """灰色小字说明标签。"""
    return "QLabel { color: #5A6572; font-size: 12px; background: transparent; }"
