/**
 * @file       XTabBar.c
 * @brief      XTabBar 选项卡条实现（对标 Qt 6.8 QTabBar 子集）。
 * @details    水平单行布局（Qt QTabBar::layoutTabs）：每页按 tabSizeHint
 *             自然宽从左排开，expanding 时把多余宽摊平，溢出且
 *             usesScrollButtons 时两端滚动按钮 + 偏移；当前项 Highlight 底
 *             + HighlightedText，其余 Button 底 + WindowText；左键点击
 *             项切换当前并发射 currentChanged/tabClicked；禁用项不响应点击；
 *             tabsClosable 为每页真实 CloseButton 子控件。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "CXinYueConfig.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XTABBAR_ON

#include "XTabBar.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XAbstractItemView.h" /* [对齐 Qt 2026-11-03] 省略枚举单一编号（XAbstractItemViewTextElideMode）。 */
#include "XCommonStyle.h"      /* XCommonStyle_elideText（按 m_elideMode 省略，同 Qt）。 */
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include "XIcon.h"     /* 页签图标绘制（对标 Qt 的 DecorationRole 图标）。 */
#include "XPixmap.h"
#include "XAbstractButton.h" /* tabsClosable 的真实关闭按钮子控件。 */
#include "XToolTip.h"        /* [P3-61] 逐页提示悬停弹出（对标 QToolTip）。 */
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */

#define XTABBAR_TAB_H 24
/** @brief 页签自然宽的左右留白（Qt Fusion 实测：文字宽 + ≈24px；
 *         空文本自然宽 24、"A" 39、"AAAA" 54，见文件内布局段注记）。 */
#define XTABBAR_TAB_PADDING 24
/** @brief 页签最小自然宽（Qt Fusion 空文本页签 = 24）。 */
#define XTABBAR_MIN_TAB_W 24
/** @brief 页签关闭按钮占位宽（Qt Fusion tabsClosable 时自然宽 +24）。 */
#define XTABBAR_CLOSE_W 16
/** @brief 图标与文字间距。 */
#define XTABBAR_ICON_GAP 4
/** @brief 图标缺省尺寸（样式 PM_TabBarIconSize；Qt 6.8 Fusion 实测 16x16；
 *         宽、高各自缺省，QSize 口径）。 */
#define XTABBAR_DEFAULT_ICON_SIZE 16
/** @brief 页签垂直留白（页签高 − 缺省图标高）：图标撑高页签时保持该留白
 *         （对标 qtabbar.cpp:1595-1599 的 `qMax(字体高, 图标高) + vframe`，
 *         Fusion 下 vframe ≈ 24 − 16 = 8）。 */
#define XTABBAR_TAB_VSPACE (XTABBAR_TAB_H - XTABBAR_DEFAULT_ICON_SIZE)
/** @brief 页签文本行高（基线定位用：文本在页签内垂直居中；页签 24 时
 *         基线 = 16，与旧实现的固定值一致）。 */
#define XTABBAR_TEXT_H 8
/** @brief 溢出态端部滚动按钮命中/绘制区宽（对标 Qt 16-20px 区间）。 */
#define XTABBAR_SCROLL_BTN_W 18

/* ==================== 前向声明 ==================== */
static void  VXTabBar_paintEvent(XWidget* self, XEvent* event);
static void  VXTabBar_mousePressEvent(XWidget* self, XEvent* event);
static void  VXTabBar_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void  VXTabBar_changeEvent(XWidget* self, XEvent* event);
static void  VXTabBar_resizeEvent(XWidget* self, XEvent* event);
static void  VXTabBar_copy(XTabBar* self, const XTabBar* other);
static void  VXTabBar_move(XTabBar* self, XTabBar* other);
static int   xtabbar_tabAt(const XTabBar* self, const XPoint* pos);
static int   xtabbar_selectNewCurrentIndexFrom(const XTabBar* self, int pos);
static void  xtabbar_emitInt(XTabBar* self, size_t signal, int value);
static void  VXTabBar_wheelEvent(XWidget* self, XEvent* event);
static void  VXTabBar_leaveEvent(XWidget* self, XEvent* event); /* [P3-61] */
static void  xtabbar_hoverReset(XTabBar* bar); /* [P3-61] 悬停复位。 */

/** @brief 双击：命中页签时发射 tabBarDoubleClicked(int)（对标
 *         QTabBar::tabBarDoubleClicked）。 */
static void VXTabBar_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    XMouseEvent* me;
    XPoint pos;
    int idx;

    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    idx = xtabbar_tabAt(bar, &pos);
    if (idx < 0) {
        XEvent_ignore(event);
        return;
    }
    xtabbar_emitInt(bar, (size_t)XTabBar_tabBarDoubleClicked_signal(bar, idx),
                    idx);
    XEvent_accept(event);
}

/* ==================== 内部辅助 ==================== */

static uint32_t xtabbar_color(const XTabBar* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void xtabbar_emitInt(XTabBar* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void xtabbar_emitInt2(XTabBar* self, size_t signal, int a, int b)
{
    XVarList* arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 对标 QTabBarPrivate::autoHideTabs（qtabbar.cpp:762-768）：
 *  autoHide 开启时页签条显隐随 count>1 重算（insertTab/removeTab/
 *  setAutoHide 三处调用点同 Qt）。 */
static void xtabbar_autoHideTabs(XTabBar* self)
{
    if (!self || !self->m_autoHide) return;
    XWidget_setVisible((XWidget*)self, self->m_count > 1);
}

/** @brief 确保容量（按需倍增）。 */
static bool xtabbar_ensureCapacity(XTabBar* self, int need)
{
    XString** titles;
    bool* enabled;
    uint32_t* colors;
    bool* visible;
    XString** tips;
    XString** whatsThis;
    XString** accessibleNames;
    XString** icons;
    XString** datas;
    XAbstractButton** buttons;
    XAbstractButton** leftButtons;
    bool* closeOwned;
    uint16_t* widths;
    int16_t* lastTabs;
    int cap = self->m_capacity > 0 ? self->m_capacity : 4;
    int i;
    while (cap < need) cap *= 2;
    if (need <= self->m_capacity) return true;
    titles = (XString**)XRealloc_System(self->m_titles, sizeof(XString*) * (size_t)cap);
    if (!titles) return false;
    self->m_titles = titles;
    enabled = (bool*)XRealloc_System(self->m_enabled, sizeof(bool) * (size_t)cap);
    if (!enabled) return false;
    self->m_enabled = enabled;
    colors = (uint32_t*)XRealloc_System(self->m_tabTextColors,
                                        sizeof(uint32_t) * (size_t)cap);
    if (!colors) return false;
    self->m_tabTextColors = colors;
    visible = (bool*)XRealloc_System(self->m_tabVisible,
                                     sizeof(bool) * (size_t)cap);
    if (!visible) return false;
    self->m_tabVisible = visible;
    tips = (XString**)XRealloc_System(self->m_tabToolTips,
                                      sizeof(XString*) * (size_t)cap);
    if (!tips) return false;
    self->m_tabToolTips = tips;
    whatsThis = (XString**)XRealloc_System(self->m_tabWhatsThis,
                                           sizeof(XString*) * (size_t)cap);
    if (!whatsThis) return false;
    self->m_tabWhatsThis = whatsThis;
    accessibleNames = (XString**)XRealloc_System(self->m_tabAccessibleNames,
                                                 sizeof(XString*) * (size_t)cap);
    if (!accessibleNames) return false;
    self->m_tabAccessibleNames = accessibleNames;
    icons = (XString**)XRealloc_System(self->m_tabIcons,
                                       sizeof(XString*) * (size_t)cap);
    if (!icons) return false;
    self->m_tabIcons = icons;
    datas = (XString**)XRealloc_System(self->m_tabData,
                                       sizeof(XString*) * (size_t)cap);
    if (!datas) return false;
    self->m_tabData = datas;
    buttons = (XAbstractButton**)XRealloc_System(
        self->m_tabButtons, sizeof(XAbstractButton*) * (size_t)cap);
    if (!buttons) return false;
    self->m_tabButtons = buttons;
    leftButtons = (XAbstractButton**)XRealloc_System(
        self->m_tabLeftButtons, sizeof(XAbstractButton*) * (size_t)cap);
    if (!leftButtons) return false;
    self->m_tabLeftButtons = leftButtons;
    closeOwned = (bool*)XRealloc_System(self->m_tabCloseOwned,
                                        sizeof(bool) * (size_t)cap);
    if (!closeOwned) return false;
    self->m_tabCloseOwned = closeOwned;
    widths = (uint16_t*)XRealloc_System(self->m_tabWidths,
                                        sizeof(uint16_t) * (size_t)cap);
    if (!widths) return false;
    self->m_tabWidths = widths;
    lastTabs = (int16_t*)XRealloc_System(self->m_lastTabs,
                                         sizeof(int16_t) * (size_t)cap);
    if (!lastTabs) return false;
    self->m_lastTabs = lastTabs;
    for (i = self->m_capacity; i < cap; ++i) {
        self->m_tabTextColors[i] = 0;
        self->m_tabVisible[i] = true;
        self->m_tabToolTips[i] = NULL;
        self->m_tabWhatsThis[i] = NULL;
        self->m_tabAccessibleNames[i] = NULL;
        self->m_tabIcons[i] = NULL;
        self->m_tabData[i] = NULL;
        self->m_tabButtons[i] = NULL;
        self->m_tabLeftButtons[i] = NULL;
        self->m_tabCloseOwned[i] = false;
        self->m_tabWidths[i] = 0;
        self->m_lastTabs[i] = -1;
    }
    self->m_capacity = cap;
    return true;
}

/* ==================== 页签布局（对齐 Qt QTabBar::layoutTabs） ====================
 * Qt 6.8 的 QTabBar 是**单行**布局：每页按 tabSizeHint 的自然宽从左排开，
 * expanding 时把多余宽度摊平（自然宽超过均分者保持自然宽、其余均分剩余），
 * 页签总宽超出条宽且 usesScrollButtons 时进入溢出滚动（两端按钮 + 偏移）。
 * 本机 Qt 6.8.0 + offscreen + Fusion 探针实测：
 *  - 自然宽：空文本 24、单字符 "A" 39、"AAAA" 54、20 字符 163；
 *    带 16px 图标 +14（53）、图标 32px 再 +16（69）；tabsClosable +24；
 *  - expanding（400 宽，自然 53/163/39）⇒ 119/163/118（163 > 400/3 保自然宽，
 *    其余均分 237 → 119/118）；
 *  - expanding=false ⇒ 53/163/39（不拉伸）；
 *  - minimumTabSizeHint 与 tabSizeHint 相等；条形 minimumSizeHint =
 *    usesScrollButtons ? (2×滚动按钮宽 + 75, sizeHint 高) : 最小宽并集。
 * 隐藏页签不占布局宽（Qt layoutTabs 跳过不可见页）。
 * 旧实现为「多行换行 + 固定 88px」，与 Qt 单行口径不符（gaps 274）。 */

/** @brief 图标宽（0=默认 16；对标 QTabBar::iconSize 的 QSize.width）。
 *  未显式设置（m_iconSizeW == 0）时取样式口径的缺省 16。 */
static int xtabbar_iconWidth(const XTabBar* self)
{
    int w = self ? (int)self->m_iconSizeW : 0;
    return w > 0 ? w : XTABBAR_DEFAULT_ICON_SIZE;
}

/** @brief 图标高（0=默认 16；对标 QTabBar::iconSize 的 QSize.height）。 */
static int xtabbar_iconHeight(const XTabBar* self)
{
    int h = self ? (int)self->m_iconSizeH : 0;
    return h > 0 ? h : XTABBAR_DEFAULT_ICON_SIZE;
}

/** @brief 页签高（缺省 24；图标高超过默认时按 Qt 口径撑高）。
 *  Qt 6.8 qtabbar.cpp:1594-1600：非竖排页签高 =
 *  `qMax(maxWidgetHeight, qMax(fm.height(), iconSize.height())) + vframe`
 *  ——本实现把「字体/控件高 + 垂直留白」折算成基线 24（16 图标 + 8 留白），
 *  图标更高时页签随之增高，避免图标被裁。
 */
static int xtabbar_tabHeight(const XTabBar* self)
{
    int need = xtabbar_iconHeight(self) + XTABBAR_TAB_VSPACE;
    return need > XTABBAR_TAB_H ? need : XTABBAR_TAB_H;
}

/** @brief 置布局缓存失效（内容/尺寸/设置变化时调用，下次查询重算）。 */
static void xtabbar_invalidateLayout(XTabBar* self)
{
    if (!self) return;
    self->m_layoutBarW = -1;
}

/* ==================== 竖排方向（P0-01，对标 Qt 6.8） ====================
 * Qt 的 QTabBar 没有 orientation 字段——竖排由**形状**派生：
 * `QTabBarPrivate::verticalTabs(shape)`（qtabbar_p.h:248-253）判
 * RoundedWest/East(2/3)、TriangularWest/East(6/7) 四形状竖排；容器侧
 * QTabWidgetPrivate::updateTabBarPosition（qtabwidget.cpp:960-982）把
 * tabPosition 映射成页签条形状（West⇒RoundedWest…）。竖排页签几何：
 * tabSizeHint 两轴转置（qtabbar.cpp:1599-1602 竖排 csz = (截面厚,
 * 文本宽+留白)），tabRect 沿 y 累计并以 scrollOffset 平移 y
 * （qtabbar.cpp:1351-1355），文本 ±90° 旋转
 * （qcommonstyle.cpp:2010-2023：East 族 +90°、West 族 -90°）。 */

/** @brief 是否竖排（对标 QTabBarPrivate::verticalTabs）。
 * @details 形状 2/3/6/7（West/East 四形状）竖排；XTabWidget 的
 *          tabPosition(West/East) 经形状映射落到这四值。 */
static bool xtabbar_isVertical(const XTabBar* self)
{
    return self && (self->m_shape == 2u || self->m_shape == 3u ||
                    self->m_shape == 6u || self->m_shape == 7u);
}

/** @brief 布局主轴长度（横排=条宽，竖排=条高；对标 Qt
 *  `horiz ? q->width() : q->height()`，qtabbar.cpp:675）。 */
static int xtabbar_barLength(const XTabBar* self)
{
    if (!self) return 0;
    return xtabbar_isVertical(self) ? XWidget_height((const XWidget*)self)
                                    : XWidget_width((const XWidget*)self);
}

/** @brief 单个页签的首选尺寸基类实现（对标 QTabBar::tabSizeHint）。 */
/** @brief TabSizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；
 *         按《api命名风格》`*_base` 契约，业务逻辑只放此处，
 *         对外唯一入口是分派 `XTabBar_tabSizeHint_base`；子类槽内
 *         回落用 `XClass_Parent(XTabBar, EXTabBar_TabSizeHint, ...)`。
 *         2026-11-12 命名契约整改：原 `_base` 装业务、无后缀名装分派
 *         的倒置形状已交换。 */
static XSize VXTabBar_tabSizeHint(const XTabBar* self, int index)
{
    XSize out;
    XFont font;
    const char* text;
    int w;
    if (!self || index < 0 || index >= self->m_count) {
        XSize_init(&out, 0, 0);
        return out;
    }
    text = self->m_titles[index] ? XString_toUtf8(self->m_titles[index]) : "";
    font = XWidget_font((const XWidget*)self);
    w = XPainter_textWidth(&font, text ? text : "") + XTABBAR_TAB_PADDING;
    XClassDeinit((XClass*)&font);
    if (self->m_tabIcons && self->m_tabIcons[index]) {
        const char* path = XString_toUtf8(self->m_tabIcons[index]);
        if (path && path[0]) w += xtabbar_iconWidth(self) + XTABBAR_ICON_GAP;
    }
    if (self->m_tabsClosable) w += XTABBAR_CLOSE_W + XTABBAR_ICON_GAP;
    if (w < XTABBAR_MIN_TAB_W) w = XTABBAR_MIN_TAB_W;
    if (w > 32000) w = 32000;
    /* 自然宽只吃图标**宽**；页签**高**按图标高撑高（Qt 同口径）。 */
    XSize_init(&out, w, xtabbar_tabHeight(self));
    return out;
}

/** @brief 单个页签的首选尺寸（虚函数**调度入口**，只查虚表分派；
 *         子类可重载 EXTabBar_TabSizeHint，槽内回落用 XClass_Parent）。 */
XSize XTabBar_tabSizeHint_base(const XTabBar* self, int index)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) {
        XSize out;
        XSize_init(&out, 0, 0);
        return out;
    }
    return XClassGetVirtualFunc(self, EXTabBar_TabSizeHint,
                                XSize (*)(const XTabBar*, int))(self, index);
}

/** @brief 单个页签的最小尺寸基类默认实现（`VX*` 槽体；Qt 6.8 Fusion
 *          实测与 tabSizeHint 相等——空文本 24x29、带图标 53x29、
 *          长文本 163x29；本实现同口径）。 */
static XSize VXTabBar_minimumTabSizeHint(const XTabBar* self, int index)
{
    return VXTabBar_tabSizeHint(self, index);
}

/** @brief 单个页签的最小尺寸（虚函数**调度入口**，`*_base` 契约同上）。 */
XSize XTabBar_minimumTabSizeHint_base(const XTabBar* self, int index)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) {
        XSize out;
        XSize_init(&out, 0, 0);
        return out;
    }
    return XClassGetVirtualFunc(self, EXTabBar_MinimumTabSizeHint,
                                XSize (*)(const XTabBar*, int))(self, index);
}

/** @brief 计算可见页签的自然宽之和（布局宽缓存不依赖条宽的部分）。 */
static int xtabbar_naturalTotal(const XTabBar* self)
{
    int i;
    int total = 0;
    if (!self) return 0;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_tabVisible && !self->m_tabVisible[i]) continue;
        total += XTabBar_tabSizeHint_base(self, i).width;
    }
    return total;
}

/** @brief 重算各页签布局宽（写入 m_tabWidths；隐藏页签宽 0）。
 * @details 三档（Qt layoutTabs）：
 *          ① 溢出（usesScrollButtons 且自然总宽 > 条宽）⇒ 自然宽 + 滚动视口；
 *          ② expanding 且自然总宽 < 条宽 ⇒ 迭代均分：
 *             均分份额 = 剩余宽 / 剩余可见页数；自然宽超过份额者锁定自然宽
 *             并退出均分池，重复至无新锁定；余数按页签顺序各 +1；
 *          ③ 其余 ⇒ 自然宽。 */
static void xtabbar_relayout(XTabBar* self)
{
    int i;
    int barW;
    int total;
    int visibleN = 0;
    uint8_t* fixed;
    bool overflow;
    if (!self) return;
    barW = xtabbar_barLength(self); /* 竖排沿条高累计（P0-01），横排沿条宽。 */
    if (barW < 1) barW = 1;
    if (!self->m_tabWidths) {
        /* 容量数组未建（init 早期/空条）：直接记宽即可。 */
        self->m_layoutBarW = (int16_t)(barW > 32000 ? 32000 : barW);
        return;
    }
    total = 0;
    for (i = 0; i < self->m_count; ++i) {
        int w;
        if (self->m_tabVisible && !self->m_tabVisible[i]) {
            self->m_tabWidths[i] = 0;
            continue;
        }
        w = XTabBar_tabSizeHint_base(self, i).width;
        if (w < 1) w = 1;
        if (w > 32000) w = 32000;
        self->m_tabWidths[i] = (uint16_t)w;
        total += w;
        ++visibleN;
    }
    overflow = self->m_usesScrollButtons && total > barW;
    if (!overflow && self->m_expanding && visibleN > 0 && total < barW) {
        fixed = (uint8_t*)XCalloc_System((size_t)visibleN, sizeof(uint8_t));
        if (fixed) {
            int remainW = barW;
            int remainN = visibleN;
            bool changed = true;
            int k;
            while (changed && remainN > 0) {
                int share = remainW / remainN;
                changed = false;
                k = 0;
                for (i = 0; i < self->m_count; ++i) {
                    if (self->m_tabVisible && !self->m_tabVisible[i]) continue;
                    if (!fixed[k] && (int)self->m_tabWidths[i] > share) {
                        fixed[k] = 1;
                        remainW -= (int)self->m_tabWidths[i];
                        --remainN;
                        changed = true;
                    }
                    ++k;
                }
            }
            if (remainN > 0) {
                int share = remainW / remainN;
                int rem = remainW % remainN;
                k = 0;
                for (i = 0; i < self->m_count; ++i) {
                    if (self->m_tabVisible && !self->m_tabVisible[i]) continue;
                    if (!fixed[k]) {
                        self->m_tabWidths[i] = (uint16_t)(share +
                            (k < rem ? 1 : 0));
                    }
                    ++k;
                }
            }
            XFree_System(fixed);
        }
    }
    self->m_layoutBarW = (int16_t)(barW > 32000 ? 32000 : barW);
}

/** @brief 布局缓存按当前条长/内容刷新（查询入口统一过这里）。 */
static void xtabbar_ensureLayout(XTabBar* self)
{
    int barW;
    if (!self) return;
    barW = xtabbar_barLength(self);
    if (barW > 32000) barW = 32000;
    if (self->m_layoutBarW != (int16_t)barW || !self->m_tabWidths)
        xtabbar_relayout(self);
}

/** @brief 页签内容总宽（可见页签布局宽之和）。 */
static int xtabbar_contentWidth(const XTabBar* self)
{
    int i;
    int total = 0;
    if (!self || !self->m_tabWidths) return 0;
    for (i = 0; i < self->m_count; ++i) total += self->m_tabWidths[i];
    return total;
}

/** @brief 单步滚动宽（当前页签布局宽；空条退最小宽）。
 * @details Qt 滚动按钮每次滚一个页签宽（页签宽不再等宽后不能再用固定常量）。 */
static int xtabbar_scrollStep(const XTabBar* self)
{
    if (!self || self->m_count <= 0 || !self->m_tabWidths)
        return XTABBAR_MIN_TAB_W;
    if (self->m_currentIndex >= 0 && self->m_currentIndex < self->m_count &&
        self->m_tabWidths[self->m_currentIndex] > 0)
        return (int)self->m_tabWidths[self->m_currentIndex];
    if (self->m_tabWidths[0] > 0) return (int)self->m_tabWidths[0];
    return XTABBAR_MIN_TAB_W;
}

/** @brief 页签内容坐标左缘（布局宽前缀和）。 */
static int xtabbar_tabLeftAt(const XTabBar* self, int index)
{
    int i;
    int x = 0;
    if (!self || !self->m_tabWidths) return 0;
    for (i = 0; i < index && i < self->m_count; ++i)
        x += self->m_tabWidths[i];
    return x;
}

/** @brief 页签插入回调基类实现（对标 QTabBar::tabInserted；缺省空操作）。 */
void XTabBar_tabInserted_base(XTabBar* self, int index)
{
    (void)self;
    (void)index;
}

void XTabBar_tabInserted(XTabBar* self, int index)
{
    void (*fn)(XTabBar*, int);
    if (!self || !XClassGetVtable((XClass*)self)) return;
    fn = XClassGetVirtualFunc((XClass*)self, EXTabBar_TabInserted,
                              void (*)(XTabBar*, int));
    if (fn && fn != XTabBar_tabInserted_base)
        fn(self, index);
    else
        XTabBar_tabInserted_base(self, index);
}

/** @brief 页签移除回调基类实现（对标 QTabBar::tabRemoved；缺省空操作）。 */
void XTabBar_tabRemoved_base(XTabBar* self, int index)
{
    (void)self;
    (void)index;
}

void XTabBar_tabRemoved(XTabBar* self, int index)
{
    void (*fn)(XTabBar*, int);
    if (!self || !XClassGetVtable((XClass*)self)) return;
    fn = XClassGetVirtualFunc((XClass*)self, EXTabBar_TabRemoved,
                              void (*)(XTabBar*, int));
    if (fn && fn != XTabBar_tabRemoved_base)
        fn(self, index);
    else
        XTabBar_tabRemoved_base(self, index);
}

/** @brief 布局变化回调入口（对标 QTabBar::tabLayoutChange；缺省空操作）。 */
void XTabBar_tabLayoutChange_base(XTabBar* self)
{
    (void)self;
}

void XTabBar_tabLayoutChange(XTabBar* self)
{
    void (*fn)(XTabBar*);
    if (!self || !XClassGetVtable((XClass*)self)) return;
    fn = XClassGetVirtualFunc((XClass*)self, EXTabBar_TabLayoutChange,
                              void (*)(XTabBar*));
    if (fn && fn != XTabBar_tabLayoutChange_base)
        fn(self);
    else
        XTabBar_tabLayoutChange_base(self);
}

/** @brief 溢出判定（宽度参数化）：可见页签自然宽之和 > 条宽。 */
static bool xtabbar_overflowW(const XTabBar* self, int barW)
{
    if (!self || self->m_count <= 0) return false;
    if (barW < 1) barW = 1;
    return xtabbar_naturalTotal(self) > barW;
}

/** @brief 滚动模式布局（宽度参数化）。
 * @details 仅当 usesScrollButtons 开启且溢出时激活：单行**自然宽**页签，
 *           端部预留滚动按钮区，返回有效视口与最大偏移。
 * @return 滚动模式激活返回 true（out 参数全部有效）；否则 false。
 */
static bool xtabbar_scrollLayoutW(const XTabBar* self, int barW,
                                  int* outTabW, int* outContentW,
                                  int* outBtnW, int* outViewX,
                                  int* outViewW, int* outMaxOff)
{
    int contentW;
    int btnW;
    int viewW;
    if (!self || !self->m_usesScrollButtons) return false;
    if (barW < 1) barW = 1;
    xtabbar_ensureLayout((XTabBar*)self);
    contentW = xtabbar_contentWidth(self);
    if (contentW <= barW) return false; /* 未溢出：无需滚动。 */
    btnW = XTABBAR_SCROLL_BTN_W;
    viewW = barW - 2 * btnW;
    if (viewW < 1) viewW = 1; /* 极窄条退化：保 1px 视口防负宽。 */
    if (outTabW) *outTabW = (self->m_count > 0 && self->m_tabWidths)
                                ? (int)self->m_tabWidths[0] : XTABBAR_MIN_TAB_W;
    if (outContentW) *outContentW = contentW;
    if (outBtnW) *outBtnW = btnW;
    if (outViewX) *outViewX = btnW;
    if (outViewW) *outViewW = viewW;
    if (outMaxOff) *outMaxOff = contentW - viewW;
    return true;
}

/** @brief 滚动模式布局（取控件当前主轴长：横排条宽、竖排条高）。 */
static bool xtabbar_scrollLayout(const XTabBar* self, int* outTabW,
                                 int* outContentW, int* outBtnW,
                                 int* outViewX, int* outViewW,
                                 int* outMaxOff)
{
    return xtabbar_scrollLayoutW(self, xtabbar_barLength(self),
                                 outTabW, outContentW, outBtnW, outViewX,
                                 outViewW, outMaxOff);
}

/** @brief 有效滚动偏移（clamp 到 [0, maxOff]；条宽变化后旧值自动收敛）。 */
static int xtabbar_effOffset(const XTabBar* self, int maxOff)
{
    int off = self ? self->m_scrollOffset : 0;
    if (off < 0) off = 0;
    if (off > maxOff) off = maxOff;
    return off;
}

/** @brief 步进滚动（delta px；clamp 后写回并请求重绘）。 */
static void xtabbar_scrollBy(XTabBar* self, int delta)
{
    int tabW, contentW, btnW, viewX, viewW, maxOff;
    int newOff;
    if (!self) return;
    if (!xtabbar_scrollLayout(self, &tabW, &contentW, &btnW, &viewX,
                              &viewW, &maxOff)) return;
    newOff = self->m_scrollOffset + delta;
    if (newOff < 0) newOff = 0;
    if (newOff > maxOff) newOff = maxOff;
    if (newOff == self->m_scrollOffset) return;
    self->m_scrollOffset = newOff;
    XWidget_update((XWidget*)self);
}

/** @brief 页签内容 x → 视口屏幕 x（滚动模式）。 */
static int xtabbar_tabScreenX(const XTabBar* self, int index, int viewX,
                              int off)
{
    return viewX + xtabbar_tabLeftAt(self, index) - off;
}

/** @brief 当前页自动露出（对标 QTabBar ensureVisible 语义）。
 *  @details 页签左缘在视口左界之左或右缘在视口右界之右时，滚动最小
 *           距使其完全可见（clamp 由 xtabbar_scrollBy 承担）。
 */
static void xtabbar_ensureVisible(XTabBar* self, int index)
{
    int tabW, contentW, btnW, viewX, viewW, maxOff;
    int off;
    int tabL;
    int tabR;
    if (!self || index < 0 || index >= self->m_count) return;
    if (!xtabbar_scrollLayout(self, &tabW, &contentW, &btnW, &viewX,
                              &viewW, &maxOff)) return;
    tabL = xtabbar_tabLeftAt(self, index);
    tabR = tabL + (int)self->m_tabWidths[index];
    off = xtabbar_effOffset(self, maxOff);
    if (tabR > off + viewW)
        xtabbar_scrollBy(self, tabR - viewW - off);
    off = xtabbar_effOffset(self, maxOff);
    if (tabL < off)
        xtabbar_scrollBy(self, tabL - off);
}

/** @brief 点击坐标 → 项索引（-1 = 无）。
 *  @details 单行布局：主轴坐标（横排 x / 竖排 y，P0-01）减去滚动视口
 *           起点再加偏移得到内容坐标，按各页布局宽前缀和命中（隐藏页签
 *           宽 0 不命中）；截面越界（横排 y / 竖排 x）、按钮区/视口外/
 *           内容末尾之后均 -1。
 */
static int xtabbar_tabAt(const XTabBar* self, const XPoint* pos)
{
    int tW, cW, btnW, viewX, viewW, maxOff, off, cx;
    int i;
    int x;
    int primary;
    bool vertical;
    if (!pos || !self) return -1;
    vertical = xtabbar_isVertical(self);
    /* 截面越界判据：横排看 y（页签高）、竖排看 x（页签厚=同一 24 基线）。 */
    if (vertical) {
        if (pos->x < 0 || pos->x >= xtabbar_tabHeight(self)) return -1;
        primary = pos->y;
    } else {
        if (pos->y < 0 || pos->y >= xtabbar_tabHeight(self)) return -1;
        primary = pos->x;
    }
    xtabbar_ensureLayout((XTabBar*)self);
    if (!self->m_tabWidths) return -1;
    cx = primary;
    if (xtabbar_scrollLayout(self, &tW, &cW, &btnW, &viewX, &viewW,
                             &maxOff)) {
        if (primary < viewX || primary >= viewX + viewW) return -1;
        off = xtabbar_effOffset(self, maxOff);
        cx = primary - viewX + off;
        if (cx < 0 || cx >= cW) return -1;
    }
    x = 0;
    for (i = 0; i < self->m_count; ++i) {
        int w = (int)self->m_tabWidths[i];
        if (w <= 0) continue;
        if (cx >= x && cx < x + w) return i;
        x += w;
    }
    return -1;
}

/* ==================== 虚槽实现 ==================== */

/** @brief 页签关闭按钮尺寸（方边；满足命中面积）与距页签右缘的留白。 */
#define XTABBAR_CLOSE_BTN_PX 14

static void xtabbar_syncCloseButtons(XTabBar* self);
static void xtabbar_layoutButtons(XTabBar* self);

/** @brief 绘制页签图标（路径 → XIcon → XImage，随页签垂直居中）。
 * @details Qt 由样式按 DecorationRole 绘图标；本库样式层的 tab label 未接线
 *          图标（XCommonStyle.c xcs_drawTabLabel 注释「QIcon::paint 未在此
 *          处接线」），故在控件绘制路径自行解码文件图标并画在文本左侧。
 * @param painter 绘制器。
 * @param bar 目标页签条。
 * @param index 页签号。
 * @param x 图标左缘。
 * @param y 图标上缘。
 * @return 无。
 */
static void xtabbar_drawTabIcon(XPainter* painter, const XTabBar* bar,
                                int index, int x, int y)
{
    const char* path;
    XIcon icon;
    XPixmap pixmap;
    XImage image;
    int iconW;
    int iconH;
    if (!painter || !bar || index < 0 || index >= bar->m_count) return;
    if (!bar->m_tabIcons || !bar->m_tabIcons[index]) return;
    path = XString_toUtf8(bar->m_tabIcons[index]);
    if (!path || !path[0]) return;
    /* QSize 口径：按宽、高分别解码（Qt QIcon::pixmap(size) 同口径）。 */
    iconW = xtabbar_iconWidth(bar);
    iconH = xtabbar_iconHeight(bar);
    XIcon_init_file_2(&icon, path);
    if (XIcon_isNull(&icon)) {
        XClassDeinit((XClass*)&icon);
        return;
    }
    XPixmap_init(&pixmap);
    XIcon_pixmap(&icon, iconW, iconH, XIconMode_Normal, XIconState_Off,
                 &pixmap);
    XImage_init(&image);
    XPixmap_toImage(&pixmap, &image);
    if (XImage_width(&image) > 0 && XImage_height(&image) > 0)
        XPainter_drawImage(painter, &image, x, y);
    XClassDeinit((XClass*)&image);
    XClassDeinit((XClass*)&pixmap);
    XClassDeinit((XClass*)&icon);
}

/** @brief 关闭按钮点击槽：反查按钮所属页签并发射 tabCloseRequested(index)。
 * @note  Qt 的 CloseButton 点击同样只发 tabCloseRequested、不改当前页
 *        （qtabbar.cpp closeButton 接线）。 */
static void xtabbar_closeButtonClickedSlot(XObject* receiver, XVarList* args)
{
    XTabBar* bar = (XTabBar*)receiver;
    XObject* sender;
    int i;
    (void)args;
    if (!bar || !bar->m_tabButtons) return;
    sender = XObject_sender(receiver);
    if (!sender) return;
    for (i = 0; i < bar->m_count; ++i) {
        if ((XObject*)bar->m_tabButtons[i] == sender) {
            xtabbar_emitInt(bar,
                            (size_t)XTabBar_tabCloseRequested_signal(bar), i);
            return;
        }
    }
}

/** @brief 为一个页签创建关闭按钮子控件（reparent 归本控件）。
 * @param self 目标页签条。
 * @param index 页签号。
 * @return 新按钮；失败 NULL。
 * @note 用 XAbstractButton 基类实例（不依赖 XPushButton 模块开关，
 *       XTABBAR_ON 只要求 XABSTRACTBUTTON_ON；Qt 的 CloseButton 也只是一
 *       个带样式的抽象按钮子控件）。 */
static XAbstractButton* xtabbar_createCloseButton(XTabBar* self, int index)
{
    XAbstractButton* btn;
    if (!self) return NULL;
    (void)index;
    btn = XAbstractButton_create((XWidget*)self, 0);
    if (!btn) return NULL;
    XAbstractButton_setText_2(btn, "\xC3\x97"); /* "×" */
    XWidget_setFixedSize((XWidget*)btn, XTABBAR_CLOSE_BTN_PX,
                         XTABBAR_CLOSE_BTN_PX);
    XObject_connect((XObject*)btn,
                      (size_t)XAbstractButton_clicked_signal(btn, false),
                      (XObject*)self, xtabbar_closeButtonClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)btn); /* 显式 show 语义：子控件须显式显示。 */
    return btn;
}

/** @brief 按 tabsClosable 同步每页右角关闭按钮。
 * @details Qt 6.8 探针：tabsClosable=true 时 `tabButton(i, RightSide)` 返回
 *          CloseButton 子控件对象；关闭开关后返回空。本实现只在槽位为空时
 *          创建（不覆盖用户经 setTabButton 挂的自定义按钮），关闭时只回收
 *          自己创建的（m_tabCloseOwned 标记）。 */
static void xtabbar_syncCloseButtons(XTabBar* self)
{
    int i;
    if (!self || !self->m_tabButtons || !self->m_tabCloseOwned) return;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_tabsClosable) {
            if (!self->m_tabButtons[i]) {
                XAbstractButton* btn = xtabbar_createCloseButton(self, i);
                if (btn) {
                    self->m_tabButtons[i] = btn;
                    self->m_tabCloseOwned[i] = true;
                }
            }
        } else if (self->m_tabCloseOwned[i]) {
            XAbstractButton* btn = self->m_tabButtons[i];
            self->m_tabButtons[i] = NULL;
            self->m_tabCloseOwned[i] = false;
            if (btn) XObject_deleteLater((XObject*)btn);
        }
    }
}

/** @brief 把各页右角按钮放到页签矩形右端（垂直居中）。
 * @details Qt 由样式 SE_TabBarTabRightButton 定位；本库统一 14×14、
 *          距右缘 2px。隐藏页签的按钮一并隐藏（显式比较可见位，避免
 *          每拍 setVisible 触发父布局失效，AGENTS.md 红线）。 */
static void xtabbar_layoutButtons(XTabBar* self)
{
    int i;
    int tW, cW, btnW, viewX, viewW, maxOff;
    bool scrollMode;
    if (!self || !self->m_tabButtons) return;
    xtabbar_ensureLayout(self);
    scrollMode = xtabbar_scrollLayout(self, &tW, &cW, &btnW, &viewX, &viewW,
                                      &maxOff);
    for (i = 0; i < self->m_count; ++i) {
        XAbstractButton* btn = self->m_tabButtons[i];
        XRect tab;
        bool want;
        if (!btn) continue;
        want = XTabBar_tabRect(self, i, &tab) && tab.width > 0;
        /* 滚动模式下只显示视口内页签的按钮（否则会压到端部滚动按钮上）。 */
        if (want && scrollMode)
            want = (tab.x + tab.width > viewX) && (tab.x < viewX + viewW);
        if (XWidget_isHidden((XWidget*)btn) == want)
            XWidget_setVisible((XWidget*)btn, want);
        if (!want) continue;
        {
            int bx = tab.x + tab.width - XTABBAR_CLOSE_BTN_PX - 2;
            int by = tab.y + (xtabbar_tabHeight(self) - XTABBAR_CLOSE_BTN_PX) / 2;
            XRect cur = XWidget_geometry((XWidget*)btn);
            if (cur.x != bx || cur.y != by)
                XWidget_setGeometry((XWidget*)btn, bx, by,
                                    XTABBAR_CLOSE_BTN_PX,
                                    XTABBAR_CLOSE_BTN_PX);
        }
    }
}

/** @brief 绘制端部滚动按钮箭头（使用 painter 当前画笔色）。 */
static void xtabbar_drawArrow(XPainter* painter, int cx, int cy, bool left)
{
    if (left) {
        XPainter_drawLine(painter, cx + 2, cy - 4, cx - 2, cy);
        XPainter_drawLine(painter, cx + 2, cy + 4, cx - 2, cy);
    } else {
        XPainter_drawLine(painter, cx - 2, cy - 4, cx + 2, cy);
        XPainter_drawLine(painter, cx - 2, cy + 4, cx + 2, cy);
    }
}

/** @brief 绘制竖排端部滚动按钮的上/下箭头（P0-01；与横排左右箭头同款
 *  折线，轴向互换）。 */
static void xtabbar_drawArrowV(XPainter* painter, int cx, int cy, bool up)
{
    if (up) {
        XPainter_drawLine(painter, cx - 4, cy + 2, cx, cy - 2);
        XPainter_drawLine(painter, cx + 4, cy + 2, cx, cy - 2);
    } else {
        XPainter_drawLine(painter, cx - 4, cy - 2, cx, cy + 2);
        XPainter_drawLine(painter, cx + 4, cy - 2, cx, cy + 2);
    }
}

/** @brief 竖排页签文本（P0-01 文本旋转，对标 QCommonStyle::drawControl
 *  的 CE_TabBarTabLabel verticalTabs 变换，qcommonstyle.cpp:2010-2023：
 *  East 族 rotate 90°、West 族 rotate -90°）。文本以 (cx,cy) 为中心，
 *  沿页签长度方向铺开（先 translate 到中心再 rotate，与 Qt 的角点
 *  translate+rotate 等价——Qt 的文本同样以页签为中心对齐）。 */
static void xtabbar_drawVerticalText(XPainter* painter, const XTabBar* bar,
                                     const char* text, uint32_t color,
                                     int cx, int cy)
{
    XImageTransform saved;
    const XFont* font;
    int textW;
    int textH;
    float rot;
    if (!painter || !bar || !text || !text[0]) return;
    font = XPainter_font(painter);
    textW = XPainter_textWidth(font, text);
    textH = XPainter_textHeight(font);
    if (textH < 14) textH = 14;
    rot = (bar->m_shape == 3u || bar->m_shape == 7u) ? 90.0f : -90.0f;
    XPainter_transform(painter, &saved);
    XPainter_translate(painter, (float)cx, (float)cy);
    XPainter_rotate(painter, rot);
    XPainter_drawText(painter, -textW / 2, -textH / 2 + textH - 2,
                      text, color);
    XPainter_setTransform(painter, &saved, false);
}

/** @brief 单页贴边基线（P0-34 形状方位 + P3-59 drawBase 闸 + P3-60
 *  documentMode 闸；对标 PE_FrameTabBarBase 画在页签贴 pane 侧）。
 * @details North 族(0,4)=底边、South 族(1,5)=顶边、West 族(2,6)=右缘、
 *          East 族(3,7)=左缘；drawBase 关闭或 documentMode 开启时不画
 *          （Qt 文档模式无基底横条，qtabbar.cpp drawBase 分支）。 */
static void xtabbar_drawTabBaseLine(XPainter* painter, const XTabBar* bar,
                                    const XRect* tab, uint32_t color)
{
    int sh;
    if (!painter || !bar || !tab) return;
    if (!bar->m_drawBase || bar->m_documentMode) return;
    sh = (int)bar->m_shape;
    XPainter_setPen(painter, color);
    switch (sh) {
    case 1: /* RoundedSouth */
    case 5: /* TriangularSouth */
        XPainter_drawLine(painter, tab->x, tab->y,
                          tab->x + tab->width, tab->y);
        break;
    case 2: /* RoundedWest：基线在贴 pane 的右缘 */
    case 6: /* TriangularWest */
        XPainter_drawLine(painter, tab->x + tab->width, tab->y,
                          tab->x + tab->width, tab->y + tab->height);
        break;
    case 3: /* RoundedEast：基线在贴 pane 的左缘 */
    case 7: /* TriangularEast */
        XPainter_drawLine(painter, tab->x, tab->y,
                          tab->x, tab->y + tab->height);
        break;
    default: /* Rounded/Triangular North：底边（原横排口径）。 */
        XPainter_drawLine(painter, tab->x, tab->y + tab->height,
                          tab->x + tab->width, tab->y + tab->height);
        break;
    }
}

/** @brief 单页形状贴边亮线（P0-34 无样式回退的形状表达；样式分支由
 *  B2 按 Rounded/Triangular × N/S/W/E 八值交付）。亮线画在页签「朝
 *  外」的贴边：North=顶边、South=底边、West=左缘、East=右缘（Qt
 *  CE_TabBarTabShape 按形状翻转高亮边）。 */
static void xtabbar_drawShapeAccentEdge(XPainter* painter,
                                        const XTabBar* bar,
                                        const XRect* tab, uint32_t color)
{
    int sh;
    if (!painter || !bar || !tab) return;
    sh = (int)bar->m_shape;
    XPainter_setPen(painter, color);
    switch (sh) {
    case 1: /* South 族：亮边在底边。 */
    case 5:
        XPainter_drawLine(painter, tab->x, tab->y + tab->height,
                          tab->x + tab->width, tab->y + tab->height);
        break;
    case 2: /* West 族：亮边在左缘。 */
    case 6:
        XPainter_drawLine(painter, tab->x, tab->y, tab->x,
                          tab->y + tab->height);
        break;
    case 3: /* East 族：亮边在右缘。 */
    case 7:
        XPainter_drawLine(painter, tab->x + tab->width, tab->y,
                          tab->x + tab->width, tab->y + tab->height);
        break;
    default: /* North 族：亮边在顶边。 */
        XPainter_drawLine(painter, tab->x, tab->y,
                          tab->x + tab->width, tab->y);
        break;
    }
}

static void VXTabBar_paintEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    uint32_t base;
    uint32_t dark;
    uint32_t button;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    uint32_t disabled;
    uint32_t light;
    int i;
    if (!bar || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    base     = xtabbar_color(bar, XPaletteColorRole_Base);
    dark     = xtabbar_color(bar, XPaletteColorRole_Dark);
    light    = xtabbar_color(bar, XPaletteColorRole_Light);
    button   = xtabbar_color(bar, XPaletteColorRole_Button);
    highlight = xtabbar_color(bar, XPaletteColorRole_Highlight);
    highlightedText = xtabbar_color(bar, XPaletteColorRole_HighlightedText);
    windowText = xtabbar_color(bar, XPaletteColorRole_WindowText);
    disabled = xtabbar_color(bar, XPaletteColorRole_Mid);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

    {
        int tabW = 0;
        int cW = 0, btnW = 0, viewX = 0, viewW = 0, maxOff = 0, off = 0;
        int barL = xtabbar_barLength(bar); /* 主轴长（横=宽，竖=高，P0-01）。 */
        /* 页签截面厚（横排=页签高、竖排=页签宽，随图标高撑高，QSize
         * 口径），本帧全部几何复用同一值。 */
        int tabH = xtabbar_tabHeight(bar);
        bool vertical = xtabbar_isVertical(bar);
        bool scrollMode;
        int x;
        XStyle* style = NULL;
#if XSTYLE_ON
        style = XWidget_style((const XWidget*)self);
#endif
        xtabbar_ensureLayout(bar);
        scrollMode = xtabbar_scrollLayout(bar, &tabW, &cW, &btnW, &viewX,
                                          &viewW, &maxOff);
        off = scrollMode ? xtabbar_effOffset(bar, maxOff) : 0;
        if (scrollMode) {
            /* 溢出滚动：全部页签裁剪到视口（首尾越界页签部分绘制）；
             * 竖排视口沿 y（P0-01）。 */
            XRect viewRect;
            if (vertical)
                XRect_init(&viewRect, 0, viewX, tabH, viewW);
            else
                XRect_init(&viewRect, viewX, 0, viewW, tabH);
            XPainter_setClipRect(&painter, &viewRect,
                                 XPainterClipOperation_ReplaceClip);
        }
        x = scrollMode ? viewX - off : 0;
        for (i = 0; i < bar->m_count; ++i) {
            XRect tab;
            int tw;
            bool isCur;
            bool hasIcon;
            if (!bar->m_tabWidths) break;
            tw = (int)bar->m_tabWidths[i];
            if (tw <= 0) continue; /* 隐藏页签不占布局（Qt layoutTabs 跳过）。 */
            if (scrollMode) {
                int sx = x;
                x += tw;
                if (sx + tw <= viewX || sx >= viewX + viewW)
                    continue; /* 完全越界页签不绘制。 */
                if (vertical)
                    XRect_init(&tab, 0, sx, tabH, tw - 1);
                else
                    XRect_init(&tab, sx, 0, tw - 1, tabH);
            } else {
                if (vertical)
                    XRect_init(&tab, 0, x, tabH, tw - 1);
                else
                    XRect_init(&tab, x, 0, tw - 1, tabH);
                x += tw;
            }
            isCur = (i == bar->m_currentIndex);
            hasIcon = bar->m_tabIcons && bar->m_tabIcons[i] &&
                      XString_toUtf8(bar->m_tabIcons[i]) &&
                      XString_toUtf8(bar->m_tabIcons[i])[0];
#if XSTYLE_ON
            if (style != NULL) {
                /* Fusion/公共风格接管：页签形状 + 标签由样式引擎绘制。 */
                XStyleOption opt;
                char tabElideBuf[256]; /* [对齐 Qt] 页签文本省略缓冲。 */
                const char* shown;
                XStyleOption_init(&opt, XStyleCE_TabBarTabShape);
                opt.m_rect = tab;
                opt.m_state = bar->m_enabled[i] && XWidget_isEnabled(self)
                    ? XStyleState_Enabled : 0;
                if (isCur) opt.m_state |= XStyleState_Selected;
                if (XWidget_underMouse(self) &&
                    XWidget_isEnabled(self))
                    opt.m_state |= XStyleState_MouseOver;
                if (XWidget_hasFocus(self) && isCur)
                    opt.m_state |= XStyleState_HasFocus;
                opt.m_tabSelected = isCur;
                opt.m_tabIndex = i;
                /* [P3-58] 逐页文本色下发样式选项（扁平字段已在，0=调色板
                 * 决定；XCommonStyle 的按钮/单选绘制已消费同名字段）。
                 * 口径按台账：颜色只覆盖**普通态**——选中页签仍走样式
                 * 的常规文本色，故当前页不填（保持 0）。 */
                if (!isCur && bar->m_tabTextColors)
                    opt.m_textColor = bar->m_tabTextColors[i];
                /* 对齐 Qt：QTabBar::initStyleOption（qtabbar.cpp:202-210）
                 * 在**填充样式选项时**就按 `d->elideMode` 完成 elidedText，
                 * QStyleOptionTab 本身没有 elideMode 字段。
                 * 可用长取页签内容长减去两侧留白与图标占位（横排按宽、
                 * 竖排按高，P0-01）。
                 * [2026-11-11] 此前固定按 ElideRight 省略（借用
                 * XAbstractItemView_elidedText 的 NULL 视图缺省模式），
                 * m_elideMode 只存不用；现按 Qt 口径把 m_elideMode 传给
                 * 同一省略原语（XCommonStyle_elideText，mode 编号即
                 * Qt::TextElideMode）。 */
                {
                    const char* raw = bar->m_titles[i]
                        ? XString_toUtf8(bar->m_titles[i]) : "";
                    int iconW = hasIcon
                        ? xtabbar_iconWidth(bar) + XTABBAR_ICON_GAP : 0;
                    int availW = (vertical ? tab.height : tab.width)
                                 - 12 - iconW;
                    if (availW < 0) availW = 0;
                    if (bar->m_titles[i] && availW > 0) {
                        const char* elided = XCommonStyle_elideText(
                            tabElideBuf, (int)sizeof(tabElideBuf),
                            raw, (int)bar->m_elideMode, availW,
                            XPainter_font(&painter));
                        if (elided && elided != raw)
                            raw = elided;
                    }
                    opt.m_text = raw;
                }
#if XPALETTE_ON
                opt.m_palette = XWidget_palette(self);
#endif
                XStyle_drawControl(style, XStyleCE_TabBarTabShape,
                                   &opt, &painter, self);
                shown = opt.m_text ? opt.m_text : "";
                if (vertical) {
                    /* [P0-01] 竖排：样式层 CE_TabBarTabLabel 无旋转支持，
                     * 形状画完后自绘 ±90° 旋转文本（图标不随文本旋转，
                     * 落在页签始端截面居中；Qt 的 CE_TabBarTabLabel 对
                     * verticalTabs 走 QTransform 旋转，qcommonstyle.cpp:
                     * 2010-2023）。 */
                    int cx = tab.x + tabH / 2;
                    int cy = tab.y + tab.height / 2;
                    uint32_t textColor = windowText;
                    if (!bar->m_enabled[i] || !XWidget_isEnabled(self))
                        textColor = disabled;
                    else if (!isCur && bar->m_tabTextColors &&
                             bar->m_tabTextColors[i] != 0)
                        textColor = bar->m_tabTextColors[i]; /* [P3-58] */
                    if (hasIcon) {
                        int iconW = xtabbar_iconWidth(bar);
                        int iconH = xtabbar_iconHeight(bar);
                        int headLen = 2 + iconH + XTABBAR_ICON_GAP;
                        xtabbar_drawTabIcon(&painter, bar, i,
                                            cx - iconW / 2, tab.y + 2);
                        cy = tab.y + headLen +
                             (tab.height - headLen) / 2;
                    }
                    xtabbar_drawVerticalText(&painter, bar, shown,
                                             textColor, cx, cy);
                } else if (hasIcon) {
                    /* 样式层 CE_TabBarTabLabel 只居中画文本、不感知图标
                     * （XCommonStyle.c xcs_drawTabLabel 的 iconRect 未接线），
                     * 有图标时自绘「图标 + 文本」居中组，避免叠字。 */
                    const XFont* font = XPainter_font(&painter);
                    int textW = XPainter_textWidth(font, shown);
                    int iconW = xtabbar_iconWidth(bar);
                    int iconH = xtabbar_iconHeight(bar);
                    int groupW = iconW + XTABBAR_ICON_GAP + textW;
                    int gx = tab.x + (tab.width - groupW) / 2;
                    uint32_t textColor = windowText;
                    if (!bar->m_enabled[i] || !XWidget_isEnabled(self))
                        textColor = disabled;
                    else if (!isCur && bar->m_tabTextColors &&
                             bar->m_tabTextColors[i] != 0)
                        textColor = bar->m_tabTextColors[i]; /* [P3-58] */
                    /* 垂直居中按图标**高**（非方边时高≠宽）。 */
                    xtabbar_drawTabIcon(&painter, bar, i, gx,
                                        tab.y + (tabH - iconH) / 2);
                    XPainter_drawText(&painter, gx + iconW + XTABBAR_ICON_GAP,
                                      tab.y + (tabH + XTABBAR_TEXT_H) / 2,
                                      shown, textColor);
                } else {
                    opt.m_type = XStyleCE_TabBarTabLabel;
                    XStyle_drawControl(style, XStyleCE_TabBarTabLabel,
                                       &opt, &painter, self);
                }
                /* [P3-59/P3-60] 基线：drawBase 闸 + documentMode 不画 +
                 * 形状定贴边方位（P0-34）。 */
                xtabbar_drawTabBaseLine(&painter, bar, &tab, dark);
                /* tabsClosable 的关闭按钮是真实子控件（见
                 * xtabbar_syncCloseButtons），此处不再画 X 记号。 */
                continue;
            }
#endif /* XSTYLE_ON */
            /* 无样式回退：documentMode 用 Base 底（Qt 文档模式淡底口径，
             * P3-60），常规用 Button；当前页保持 Highlight。 */
            XPainter_fillRect(&painter, &tab,
                              isCur ? highlight
                                    : (bar->m_documentMode ? base : button));
            /* [P0-34] 形状贴边亮线（无样式回退按形状画「顶边」）。 */
            xtabbar_drawShapeAccentEdge(&painter, bar, &tab, light);
            {
                const char* rawText = bar->m_titles[i]
                    ? XString_toUtf8(bar->m_titles[i]) : NULL;
                uint32_t textColor =
                    isCur ? highlightedText
                          : (bar->m_enabled[i] ? windowText : disabled);
                /* [P3-58] 存了逐页文本色的普通页签用它（选中仍用
                 * HighlightedText，禁用仍用 Disabled）。 */
                if (!isCur && bar->m_enabled[i] && bar->m_tabTextColors &&
                    bar->m_tabTextColors[i] != 0)
                    textColor = bar->m_tabTextColors[i];
                if (vertical) {
                    /* [P0-01] 竖排 ±90° 旋转文本。 */
                    int cx = tab.x + tabH / 2;
                    int cy = tab.y + tab.height / 2;
                    if (hasIcon) {
                        int iconW = xtabbar_iconWidth(bar);
                        int iconH = xtabbar_iconHeight(bar);
                        int headLen = 2 + iconH + XTABBAR_ICON_GAP;
                        xtabbar_drawTabIcon(&painter, bar, i,
                                            cx - iconW / 2, tab.y + 2);
                        cy = tab.y + headLen +
                             (tab.height - headLen) / 2;
                    }
                    xtabbar_drawVerticalText(&painter, bar, rawText,
                                             textColor, cx, cy);
                } else {
                    int textX = tab.x + 6;
                    if (hasIcon) {
                        int iconW = xtabbar_iconWidth(bar);
                        int iconH = xtabbar_iconHeight(bar);
                        xtabbar_drawTabIcon(&painter, bar, i, textX,
                                            tab.y + (tabH - iconH) / 2);
                        textX += iconW + XTABBAR_ICON_GAP;
                    }
                    if (rawText)
                        XPainter_drawText(&painter, textX,
                                          tab.y + (tabH + XTABBAR_TEXT_H) / 2,
                                          rawText, textColor);
                }
            }
            /* [P3-59/P3-60] 基线：drawBase 闸 + documentMode 不画 +
             * 形状定贴边方位（P0-34）。 */
            xtabbar_drawTabBaseLine(&painter, bar, &tab, dark);
        }
        if (scrollMode) {
            /* 端部滚动按钮（裁剪区外绘制）：按钮底 + 左/右（竖排上/下，
             * P0-01）箭头，到达滚动边界时以 Disabled(Mid) 色显示。 */
            XRect bl;
            XRect br;
            XRect bt; /* 竖排：上端按钮（P0-01）。 */
            XRect bb; /* 竖排：下端按钮。 */
            int brW = barL - (viewX + viewW);
            XPainter_setClipping(&painter, false); /* 关闭视口裁剪画按钮。 */
            if (vertical) {
                XRect_init(&bt, 0, 0, tabH, btnW);
                if (brW < 0) brW = 0;
                XRect_init(&bb, 0, viewX + viewW, tabH, brW);
                XPainter_fillRect(&painter, &bt, button);
                if (brW > 0) XPainter_fillRect(&painter, &bb, button);
                XPainter_setPen(&painter, off > 0 ? windowText : disabled);
                xtabbar_drawArrowV(&painter, tabH / 2,
                                   bt.y + btnW / 2, true);
                XPainter_setPen(&painter, off < maxOff ? windowText : disabled);
                xtabbar_drawArrowV(&painter, tabH / 2,
                                   bb.y + brW / 2, false);
            } else {
                XRect_init(&bl, 0, 0, btnW, tabH);
                if (brW < 0) brW = 0;
                XRect_init(&br, viewX + viewW, 0, brW, tabH);
                XPainter_fillRect(&painter, &bl, button);
                if (brW > 0) XPainter_fillRect(&painter, &br, button);
                XPainter_setPen(&painter, off > 0 ? windowText : disabled);
                xtabbar_drawArrow(&painter, bl.x + btnW / 2,
                                  tabH / 2, true);
                XPainter_setPen(&painter, off < maxOff ? windowText : disabled);
                xtabbar_drawArrow(&painter, br.x + brW / 2,
                                  tabH / 2, false);
            }
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== §8.0g11 滚动按钮按住连发 ==================== */

/** @brief 启动连发：350ms 后每 120ms 沿 dir 步进（先杀旧定时器）。 */
/** @brief 停止连发（释放/失能/销毁路径共用）。 */
static void xtabbar_scrollRepeatStop(XTabBar* bar)
{
    if (!bar) return;
    if (bar->m_scrollRepeatTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)bar, bar->m_scrollRepeatTimer);
        bar->m_scrollRepeatTimer = XTIMER_INVALID_ID;
    }
    bar->m_scrollRepeatDir = 0;
    bar->m_repeatSkip = 0;
}

static void xtabbar_scrollRepeatStart(XTabBar* bar, int dir)
{
    if (!bar) return;
    xtabbar_scrollRepeatStop(bar);
    bar->m_scrollRepeatDir = dir;
    bar->m_scrollRepeatTimer = XObject_startTimer(
        (XObject*)bar, 120u, XTimerType_PreciseTimer);
    if (bar->m_scrollRepeatTimer != XTIMER_INVALID_ID) {
        /* 首段 350ms 延迟：改投递间隔不可行，改用一次性长间隔+首跳
           由 timerEvent 内计数实现——此处直接记录起始即可（定时器以
           120ms 触发，timerEvent 首三次跳过）。 */
        bar->m_repeatSkip = 3;
    }
}

/** @brief 连发定时器触发：首段延迟计数后每拍步进一页签宽。 */
static void VXTabBar_timerEvent(XObject* object, XTimerEvent* event)
{
    XTabBar* bar = (XTabBar*)object;
    if (!bar || !event) return;
    if (XTimerEvent_timerId(event) == bar->m_scrollRepeatTimer) {
        if (bar->m_repeatSkip > 0) {
            --bar->m_repeatSkip; /* 首段 350ms 延迟（3×120ms）。 */
        } else if (bar->m_scrollRepeatDir != 0) {
            xtabbar_scrollBy(bar,
                             bar->m_scrollRepeatDir * xtabbar_scrollStep(bar));
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    XEvent_ignore((XEvent*)event);
}

static void VXTabBar_mousePressEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    XMouseEvent* me;
    XPoint pos;
    int idx;
    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    {
        /* 溢出态端部滚动按钮命中区（各 XTABBAR_SCROLL_BTN_W px）：
           按下先步进一个页签长；按住 350ms 后连发（§8.0g11，对标
           QTabBar 滚动按钮自动重复），释放停止。竖排按钮在上下端
           （P0-01），横排在左右端。 */
        int tW, cW, btnW, viewX, viewW, maxOff;
        if (xtabbar_scrollLayout(bar, &tW, &cW, &btnW, &viewX, &viewW,
                                 &maxOff)) {
            int primary = xtabbar_isVertical(bar) ? pos.y : pos.x;
            if (primary < btnW) {
                xtabbar_scrollBy(bar, -xtabbar_scrollStep(bar));
                xtabbar_scrollRepeatStart(bar, -1);
                XEvent_accept(event);
                return;
            }
            if (primary >= viewX + viewW) {
                xtabbar_scrollBy(bar, xtabbar_scrollStep(bar));
                xtabbar_scrollRepeatStart(bar, 1);
                XEvent_accept(event);
                return;
            }
        }
    }
    idx = xtabbar_tabAt(bar, &pos);
    if (idx < 0) { XEvent_ignore(event); return; }
    /* [Qt 对齐 2026-11-11] tabsClosable 的关闭动作由每页真实 CloseButton
     * 子控件处理（Qt 同：QTabBarPrivate 经 setTabButton(RightSide, closeButton)
     * 挂真按钮，按钮点击发 tabCloseRequested 且不改当前页）；此处不再做
     * 「页签右侧 12px 命中区」判定——按钮子控件的命中先于本控件事件。 */
    xtabbar_emitInt(bar, (size_t)XTabBar_tabBarClicked_signal(bar, idx), idx);
    if (!bar->m_enabled[idx]) { XEvent_ignore(event); return; }
    /*
     * 点击移焦（隔夜台账猎获④：此前点击页签焦点原位）。对标 Qt
     * 通用点击移焦：QApplicationPrivate::giveFocusAccordingToFocusPolicy
     * （qapplication.cpp:2734-2737 → 3661）在鼠标按压分发前以
     * Qt::MouseFocusReason 移焦，判据按任务口径"NoFocus 跳过"
     * （本类策略已对齐 QTabBar 默认 TabFocus，qtabbar.cpp:385）。
     * 滚动按钮/关闭钮/空白区/禁用页签不移焦：Qt 中前三者分别是
     * NoFocus 子控件（qtabbar.cpp:380-382 CloseButton 同款）与
     * 非命中区，禁用控件不收鼠标事件。
     */
    if (XWidget_focusPolicy(self) != XWidgetFocusPolicy_NoFocus)
        XWidget_setFocus_2(self, XFocusReason_Mouse);
    if (bar->m_movable) {
        /* 拖拽换位起手：记录起始页签与按下位置（move 阶段跨页签即换位）。
         * [P3-61] 拖拽期收逐页提示（对标 Qt 拖动隐藏 tooltip）。 */
        xtabbar_hoverReset(bar);
        bar->m_dragActive = true;
        bar->m_dragIndex = idx;
        bar->m_dragPressX = pos.x;
    }
    if (idx != bar->m_currentIndex) {
        int old = bar->m_currentIndex;
        bar->m_currentIndex = idx;
        xtabbar_emitInt(bar, (size_t)XTabBar_currentChanged_signal(bar, idx), idx);
        (void)old;
    }
    xtabbar_ensureVisible(bar, idx); /* 点击切换后自动露出目标页签。 */
    XWidget_update(self);
    XEvent_accept(event);
}

/* ==================== 逐页提示悬停弹出（P3-61 / P0-35 消费端） ========== */

/** @brief 悬停换页：索引变化时按逐页提示弹/收（对标 Qt 的悬停 tooltip
 *  链；XWidget 无 ToolTip 事件派发，只能走 MouseMove 重算命中）。
 * @details 未命中或该页未设提示即收起；提示锚到**页签矩形**的屏幕区域
 *          （沿 XWidget ENTER 提示链的锚定口径：控件全局区域经 rect
 *          传入，提示定在锚区上方）。 */
static void xtabbar_hoverUpdate(XTabBar* bar, int index)
{
    const char* tipUtf8;
    XRect tab;
    XPoint origin;
    XRect anchor;
    if (!bar) return;
    if (index == bar->m_hoverIndex) return; /* 同页不重弹（悬停未换位）。 */
    if (index < 0 || index >= bar->m_count) {
        bar->m_hoverIndex = -1;
        XToolTip_hideText();
        return;
    }
    bar->m_hoverIndex = (int16_t)index;
    tipUtf8 = (bar->m_tabToolTips && bar->m_tabToolTips[index])
                  ? XString_toUtf8(bar->m_tabToolTips[index]) : NULL;
    if (!tipUtf8 || !tipUtf8[0]) {
        XToolTip_hideText(); /* 换到无提示页：收起旧提示。 */
        return;
    }
    if (!XTabBar_tabRect(bar, index, &tab)) return;
    origin.x = tab.x;
    origin.y = tab.y;
    origin = XWidget_mapToGlobal((XWidget*)bar, &origin);
    anchor.x = origin.x;
    anchor.y = origin.y;
    anchor.width = tab.width;
    anchor.height = tab.height;
    XToolTip_showText_2(origin.x, origin.y, tipUtf8, (XWidget*)bar,
                        &anchor, XWidget_toolTipDuration((XWidget*)bar));
}

/** @brief 悬停状态复位并收提示（LEAVE / 拖拽起手 / 析构共用）。 */
static void xtabbar_hoverReset(XTabBar* bar)
{
    if (!bar) return;
    if (bar->m_hoverIndex >= 0 || XToolTip_isVisible())
        XToolTip_hideText();
    bar->m_hoverIndex = -1;
}

/** @brief 拖拽换位：指针进入相邻页签半区即 moveTab（发射 tabMoved）；
 *  非拖拽期负责逐页提示的悬停重算（P3-61）。 */
static void VXTabBar_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    XMouseEvent* me;
    XPoint pos;
    int idx;
    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!bar->m_dragActive) {
        /* [P3-61] 逐页提示：命中变化即弹/收（拖拽进行中不弹，对标 Qt
         * 拖动隐藏提示）。 */
        xtabbar_hoverUpdate(bar, xtabbar_tabAt(bar, &pos));
    }
    if (!bar->m_movable || !bar->m_dragActive) return;
    idx = xtabbar_tabAt(bar, &pos);
    if (idx < 0 || idx == bar->m_dragIndex) return;
    XTabBar_moveTab(bar, bar->m_dragIndex, idx);
    bar->m_dragIndex = idx;
    XWidget_update(self);
}

/** @brief 指针离开页签条：收逐页提示（P3-61；进入关闭按钮等子控件时
 *  悬停合成器对本控件发 LEAVE，提示随收，避免压在子控件上）。 */
static void VXTabBar_leaveEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    if (bar) xtabbar_hoverReset(bar);
    if (event) XEvent_accept(event);
}

static void VXTabBar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    bar->m_dragActive = false;
    xtabbar_scrollRepeatStop(bar); /* §8.0g11：按住连发终止。 */
    XEvent_accept(event);
}

/** @brief 滚轮：溢出态按一个页签宽步进滚动（对标 QTabBar 滚轮切换）。
 *  @details 水平条优先取 angleDelta.y，为 0 时退化取 x；每 120 计一步，
 *           向上滚（正值）回卷到更早页签（偏移减小）。非溢出态忽略。
 */
static void VXTabBar_wheelEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    int tW, cW, btnW, viewX, viewW, maxOff;
    if (!bar || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    if (!xtabbar_scrollLayout(bar, &tW, &cW, &btnW, &viewX, &viewW,
                              &maxOff)) {
        XEvent_ignore(event);
        return;
    }
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        int d = (delta.y != 0) ? delta.y : delta.x;
        int steps = d / 120;
        if (steps != 0) {
            xtabbar_scrollBy(bar, -steps * xtabbar_scrollStep(bar));
            XEvent_accept(event);
            return;
        }
    }
#endif /* XWINDOWEVENT_ON */
    XEvent_ignore(event);
}

static void VXTabBar_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

/** @brief 尺寸变化：布局缓存失效后重排并把角按钮放到各自页签右端。
 * @details 对标 Qt：QTabBar::resizeEvent 走 refresh→layoutTabs→tabLayoutChange
 *          （expanding 均分依赖条宽，条宽一变布局宽全变）。 */
static void VXTabBar_resizeEvent(XWidget* self, XEvent* event)
{
    XTabBar* bar = (XTabBar*)self;
    if (!bar) return;
    xtabbar_invalidateLayout(bar);
    xtabbar_ensureLayout(bar);
    xtabbar_layoutButtons(bar);
    XTabBar_tabLayoutChange(bar);
    if (event) XEvent_accept(event);
}

static void VXTabBar_copy(XTabBar* self, const XTabBar* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    for (i = 0; i < other->m_count; ++i)
        XTabBar_addTab(self, other->m_titles[i]);
    /* 逐项文本承载字段随 copy 同步（init 已置空，此处按源填充）。 */
    for (i = 0; i < other->m_count; ++i) {
        if (other->m_tabAccessibleNames && other->m_tabAccessibleNames[i])
            XTabBar_setAccessibleTabName(self, i,
                                         other->m_tabAccessibleNames[i]);
        if (other->m_tabWhatsThis && other->m_tabWhatsThis[i])
            XTabBar_setTabWhatsThis(self, i, other->m_tabWhatsThis[i]);
    }
    self->m_currentIndex = other->m_currentIndex;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    self->m_usesScrollButtons = other->m_usesScrollButtons;
    self->m_scrollOffset = other->m_scrollOffset;
    /* 图标尺寸（QSize 两轴）随 copy 同步（此前单值口径亦未搬）。 */
    self->m_iconSizeW = other->m_iconSizeW;
    self->m_iconSizeH = other->m_iconSizeH;
    /* 拷贝后按开关补齐每页关闭按钮（拷贝不逐项搬按钮子控件对象，
       只按 Qt 语义重建关闭按钮）。悬停索引是瞬态交互态，不随 copy
       迁移（复位为无）。 */
    xtabbar_invalidateLayout(self);
    self->m_hoverIndex = -1;
    xtabbar_syncCloseButtons(self);
}

/** @brief 释放全部页签平行数组与逐页 XString 载荷（deinit 与 move-assign
 *  接管前共用；不触父类与定时器）。 */
static void xtabbar_releaseTabs(XTabBar* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_titles && self->m_titles[i]) {
            XClassDelete(self->m_titles[i]);
            self->m_titles[i] = NULL;
        }
        if (self->m_tabToolTips && self->m_tabToolTips[i]) {
            XClassDelete(self->m_tabToolTips[i]);
            self->m_tabToolTips[i] = NULL;
        }
        if (self->m_tabWhatsThis && self->m_tabWhatsThis[i]) {
            XClassDelete(self->m_tabWhatsThis[i]);
            self->m_tabWhatsThis[i] = NULL;
        }
        if (self->m_tabAccessibleNames && self->m_tabAccessibleNames[i]) {
            XClassDelete(self->m_tabAccessibleNames[i]);
            self->m_tabAccessibleNames[i] = NULL;
        }
        if (self->m_tabIcons && self->m_tabIcons[i]) {
            XClassDelete(self->m_tabIcons[i]);
            self->m_tabIcons[i] = NULL;
        }
        if (self->m_tabData && self->m_tabData[i]) {
            XClassDelete(self->m_tabData[i]);
            self->m_tabData[i] = NULL;
        }
    }
    if (self->m_titles) {
        XFree_System(self->m_titles);
        self->m_titles = NULL;
    }
    if (self->m_enabled) {
        XFree_System(self->m_enabled);
        self->m_enabled = NULL;
    }
    if (self->m_tabTextColors) {
        XFree_System(self->m_tabTextColors);
        self->m_tabTextColors = NULL;
    }
    if (self->m_tabVisible) {
        XFree_System(self->m_tabVisible);
        self->m_tabVisible = NULL;
    }
    if (self->m_tabToolTips) {
        XFree_System(self->m_tabToolTips);
        self->m_tabToolTips = NULL;
    }
    if (self->m_tabWhatsThis) {
        XFree_System(self->m_tabWhatsThis);
        self->m_tabWhatsThis = NULL;
    }
    if (self->m_tabAccessibleNames) {
        XFree_System(self->m_tabAccessibleNames);
        self->m_tabAccessibleNames = NULL;
    }
    if (self->m_tabIcons) {
        XFree_System(self->m_tabIcons);
        self->m_tabIcons = NULL;
    }
    if (self->m_tabData) {
        XFree_System(self->m_tabData);
        self->m_tabData = NULL;
    }
    if (self->m_tabButtons) {
        XFree_System(self->m_tabButtons);
        self->m_tabButtons = NULL;
    }
    if (self->m_tabLeftButtons) {
        XFree_System(self->m_tabLeftButtons);
        self->m_tabLeftButtons = NULL;
    }
    if (self->m_tabCloseOwned) {
        XFree_System(self->m_tabCloseOwned);
        self->m_tabCloseOwned = NULL;
    }
    if (self->m_tabWidths) {
        XFree_System(self->m_tabWidths);
        self->m_tabWidths = NULL;
    }
    if (self->m_lastTabs) {
        XFree_System(self->m_lastTabs);
        self->m_lastTabs = NULL;
    }
    self->m_layoutBarW = -1;
    self->m_count = 0;
    self->m_capacity = 0;
}

static void VXTabBar_move(XTabBar* self, XTabBar* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    /* 接管前先释放 self 已持有的全部页签资源（数组本体+逐页载荷），
       仅清标题会使其余平行数组与载荷随覆盖失联。 */
    xtabbar_releaseTabs(self);
    self->m_titles = other->m_titles;
    self->m_enabled = other->m_enabled;
    self->m_tabTextColors = other->m_tabTextColors;
    self->m_tabVisible = other->m_tabVisible;
    self->m_tabToolTips = other->m_tabToolTips;
    self->m_tabWhatsThis = other->m_tabWhatsThis;
    self->m_tabAccessibleNames = other->m_tabAccessibleNames;
    self->m_tabIcons = other->m_tabIcons;
    self->m_tabData = other->m_tabData;
    self->m_tabButtons = other->m_tabButtons;
    self->m_tabLeftButtons = other->m_tabLeftButtons;
    self->m_tabCloseOwned = other->m_tabCloseOwned;
    self->m_tabWidths = other->m_tabWidths;
    self->m_lastTabs = other->m_lastTabs;
    self->m_layoutBarW = -1;
    self->m_count = other->m_count;
    self->m_capacity = other->m_capacity;
    other->m_titles = NULL;
    other->m_enabled = NULL;
    other->m_tabTextColors = NULL;
    other->m_tabVisible = NULL;
    other->m_tabToolTips = NULL;
    other->m_tabWhatsThis = NULL;
    other->m_tabAccessibleNames = NULL;
    other->m_tabIcons = NULL;
    other->m_tabData = NULL;
    other->m_tabButtons = NULL;
    other->m_tabLeftButtons = NULL;
    other->m_tabCloseOwned = NULL;
    other->m_tabWidths = NULL;
    other->m_lastTabs = NULL;
    other->m_layoutBarW = -1;
    other->m_count = 0;
    other->m_capacity = 0;
    self->m_currentIndex = other->m_currentIndex;
    other->m_currentIndex = -1;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    self->m_usesScrollButtons = other->m_usesScrollButtons;
    self->m_scrollOffset = other->m_scrollOffset;
    /* 图标尺寸（QSize 两轴）随 move 转移，源侧归零（默认 16×16）。 */
    self->m_iconSizeW = other->m_iconSizeW;
    self->m_iconSizeH = other->m_iconSizeH;
    other->m_iconSizeW = 0;
    other->m_iconSizeH = 0;
    other->m_tabsClosable = false;
    other->m_movable = false;
    other->m_scrollOffset = 0;
    /* 悬停索引是瞬态交互态：接管侧复位为无（源侧随 releaseTabs 收尾）。 */
    self->m_hoverIndex = -1;
    other->m_hoverIndex = -1;
}

/* ==================== 生命周期 ==================== */

/** @brief 反初始化：释放全部页签标题与数组。 */
static void VXTabBar_deinit(XTabBar* self)
{
    if (!self) return;
    xtabbar_scrollRepeatStop(self); /* §8.0g11：析构停连发定时器。 */
    xtabbar_hoverReset(self); /* [P3-61] 析构收悬停提示（载荷已深拷贝）。 */
    xtabbar_releaseTabs(self);
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* update 函数定义在后段，此处先声明供槽桥接调用。 */
static void xtabbar_updateSizeHint(XTabBar* self);

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint 虚函数）：先按当前内容
   重算缓存，再读回，等价「查询时重算」语义。 */
static XSize VXTabBar_sizeHintSlot(const XWidget* self);
static XSize VXTabBar_minimumSizeHintSlot(const XWidget* self);

static XSize VXTabBar_sizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    xtabbar_updateSizeHint((XTabBar*)self);
    return XClass_Parent(XWidget, EXWidget_SizeHint,
                         XSize (*)(const XWidget*))(self);
}

/** @brief 最小尺寸（对标 QTabBar::minimumSizeHint，qtabbar.cpp:1502-1516）。
 * @details Qt 6.8 原文口径：
 *          - usesScrollButtons ⇒ `QSize(2*滚动按钮宽 + 75, sizeHint 高)`；
 *          - 否则 ⇒ 全部可见页签**最小矩形**的并集（本库 minTab == tabSizeHint，
 *            Fusion 探针一致）；
 *          - 空条高取 sizeHint 高（Qt 探针零页签 minHint=(123,0)）。
 *          滚动按钮宽取本控件 XTABBAR_SCROLL_BTN_W（Qt 用样式 PM 值 24，
 *          本库固定 18，故常量项同形而数值不同）。 */
static XSize VXTabBar_minimumSizeHintSlot(const XWidget* self)
{
    XTabBar* bar = (XTabBar*)self;
    XSize hint;
    XSize out;
    int i;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    xtabbar_updateSizeHint(bar);
    hint = XClass_Parent(XWidget, EXWidget_SizeHint,
                         XSize (*)(const XWidget*))(self); /* 父类槽直调（不递归虚槽）。 */
    if (bar->m_usesScrollButtons) {
        /* [P0-01] 竖排转置（Qt qtabbar.cpp:1506-1509：verticalTabs ⇒
         * (sizeHint().width(), 2*滚动按钮 hint 高 + 75)）。 */
        if (xtabbar_isVertical(bar)) {
            XSize_init(&out, hint.width, 2 * XTABBAR_SCROLL_BTN_W + 75);
            return out;
        }
        XSize_init(&out, 2 * XTABBAR_SCROLL_BTN_W + 75, hint.height);
        return out;
    }
    {
        int total = 0;
        for (i = 0; i < bar->m_count; ++i) {
            if (!XTabBar_isTabVisible(bar, i)) continue;
            total += XTabBar_minimumTabSizeHint_base(bar, i).width;
        }
        /* 最小矩形并集：竖排两轴转置（页签长沿 y 累计）。 */
        if (xtabbar_isVertical(bar))
            XSize_init(&out, hint.width, total);
        else
            XSize_init(&out, total, hint.height);
    }
    return out;
}

/* TabSizeHint/MinimumTabSizeHint 基类默认实现（`VX*` 槽体，定义在尺寸
 * 查询段；按《api命名风格》`*_base` 契约注册虚表默认槽）。 */
static XSize VXTabBar_tabSizeHint(const XTabBar* self, int index);
static XSize VXTabBar_minimumTabSizeHint(const XTabBar* self, int index);

XVtable* XTabBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXTabBar_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXTabBar_minimumSizeHintSlot);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTabBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXTabBar_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXTabBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXTabBar_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXTabBar_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VXTabBar_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VXTabBar_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXTabBar_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabBar_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXTabBar_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTabBar_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTabBar_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTabBar_move);
    /* [Qt 对齐 2026-11-11] protected 虚槽缺省实现（对标 qtabbar.h:159-163）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabSizeHint,
                             VXTabBar_tabSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_MinimumTabSizeHint,
                             VXTabBar_minimumTabSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabInserted, XTabBar_tabInserted_base);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabRemoved, XTabBar_tabRemoved_base);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabLayoutChange,
                             XTabBar_tabLayoutChange_base);

    return XVTABLE_DEFAULT;
}

/** @brief 刷新尺寸提示（对标 Qt QTabBar::sizeHint）。
 *  @details Qt 6.8.3（qtabbar.cpp:1487-1498）：sizeHint 只并入**可见**页签
 *           的尺寸，隐藏页签不占位。本实现取「可见页签 tabSizeHint 自然宽
 *           之和 × 条高」——与条宽/expanding 无关（Qt 的 sizeHint 是内容
 *           首选尺寸，不是当前拉伸后的几何；本机 Qt 6.8 探针
 *           3 签(A/long/CC) sizeHint=235x29 ≈ 自然宽之和量级，而 tabRect
 *           已按 400 宽拉伸）。空条 (0,0)（Qt 探针零页签 minHint=(123,0)）。
 *           minimumSizeHint 由虚槽按 Qt 两档计算
 *           （见 VXTabBar_minimumSizeHintSlot）。 */
static void xtabbar_updateSizeHint(XTabBar* self)
{
    XSize hint;
    int count;
    int i;
    int total = 0;
    if (!self) return;
    count = XTabBar_count(self);
    for (i = 0; i < count; ++i) {
        if (!XTabBar_isTabVisible(self, i)) continue;
        total += XTabBar_tabSizeHint_base(self, i).width;
    }
    if (xtabbar_isVertical(self)) {
        /* [P0-01] 竖排两轴转置（Qt 竖排 sizeHint = 页签矩形并集的
         * (截面厚, 总长)：宽度取页签厚基线，长度为自然宽之和）。 */
        XSize_init(&hint, total > 0 ? xtabbar_tabHeight(self) : 0, total);
    } else {
        XSize_init(&hint, total, total > 0 ? xtabbar_tabHeight(self) : 0);
    }
    XWidget_setSizeHint((XWidget*)self, &hint);
}

void XTabBar_init(XTabBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XTabBar);

    self->m_titles = NULL;
    self->m_enabled = NULL;
    self->m_tabTextColors = NULL;
    self->m_tabVisible = NULL;
    self->m_tabToolTips = NULL;
    self->m_tabWhatsThis = NULL;
    self->m_tabAccessibleNames = NULL;
    self->m_tabIcons = NULL;
    self->m_tabData = NULL;
    self->m_tabButtons = NULL;
    self->m_tabLeftButtons = NULL;
    self->m_tabCloseOwned = NULL;
    self->m_tabWidths = NULL;
    self->m_lastTabs = NULL;
    self->m_layoutBarW = -1;
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_currentIndex = -1;
    self->m_tabsClosable = false;
    self->m_movable = false;
    self->m_autoHide = false;
    self->m_expanding = true;
    /* 对齐 Qt：缺省取 Fusion 的 SH_TabBar_ElideMode —— Qt 6.8
     * QCommonStyle::styleHint（qcommonstyle.cpp:5293）对该 hint 返回
     * Qt::ElideNone(3)（本机探针 tabBar.elideMode()==ElideNone）。 */
    self->m_elideMode =
        (uint32_t)XAbstractItemViewTextElideMode_ElideNone;
    /* 对齐 Qt 6.8：selectionBehaviorOnRemove 缺省 SelectRightTab(1)
     * （qtabbar_p.h:80；本机探针 = SelectRightTab）。 */
    self->m_selectionBehavior = 1;
    self->m_shape = 0; /* Qt::RoundedNorth */
    self->m_iconSizeW = 0; /* 0 = 默认宽（getter 返回 16，样式 PM 口径） */
    self->m_iconSizeH = 0; /* 0 = 默认高（同上；QSize 逐轴缺省） */
    self->m_changeCurrentOnDrag = false;
    self->m_usesScrollButtons = true; /* 对标 QTabBar 默认 true。 */
    self->m_scrollOffset = 0;
    self->m_scrollRepeatDir = 0;     /* §8.0g11 连发状态显式复位。 */
    self->m_scrollRepeatTimer = XTIMER_INVALID_ID;
    self->m_repeatSkip = 0;
    self->m_hoverIndex = -1; /* [P3-61] 悬停页签索引（-1 无）。 */
    self->m_documentMode = false;
    self->m_drawBase = true;
    /* 尺寸提示在布局标志位落位后刷新（sizeHint 取决于项数与条宽）。 */
    xtabbar_updateSizeHint(self);
    /*
     * 对标 QTabBarPrivate::init（qtabbar.cpp:379-385）非键盘导航分支
     * 的 setFocusPolicy(Qt::TabFocus)：页签条可经 Tab 进入焦点链
     * （方向键切页签的 Qt 前提）。此前缺省 NoFocus（XWidget_init）
     * 使页签条既不进 Tab 链、点击亦不移焦（隔夜台账猎获④）。
     */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_TabFocus);
    /* 悬停外观 opt-in：页签绘制按 State_MouseOver 出悬停高亮，
     * ENTER/LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
}

XTabBar* XTabBar_create_ex(XMemoryType memory, XWidget* parent,
                           XWidgetFlags flags)
{
    XTabBar* self = (XTabBar*)XMemory_malloc(sizeof(XTabBar), memory);
    if (!self) return NULL;
    XTabBar_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== API ==================== */

int XTabBar_addTab(XTabBar* self, const XString* text)
{
    return XTabBar_insertTab(self, self ? self->m_count : 0, text);
}
int XTabBar_addTab_2(XTabBar* self, const char* text)
{
    return XTabBar_insertTab_2(self, self ? self->m_count : 0, text);
}

int XTabBar_insertTab(XTabBar* self, int index, const XString* text)
{
    XString* copy;
    if (!self || !text) return -1;
    /* 对标 qtabbar.cpp:957-959 validIndex 判据：非法索引（负数/越界）
       一律追加到末尾（此前负数收敛为最前，与 Qt 及 QStackedLayout
       insertWidget 的 qstackedlayout.cpp:192-194 口径相反）。 */
    if (index < 0 || index > self->m_count) index = self->m_count;
    if (!xtabbar_ensureCapacity(self, self->m_count + 1)) return -1;
    copy = XString_create_copy(text);
    if (!copy) return -1;
    XMemmove(&self->m_titles[index + 1], &self->m_titles[index],
             sizeof(XString*) * (size_t)(self->m_count - index));
    XMemmove(&self->m_enabled[index + 1], &self->m_enabled[index],
             sizeof(bool) * (size_t)(self->m_count - index));
    if (self->m_tabTextColors)
        XMemmove(&self->m_tabTextColors[index + 1],
                 &self->m_tabTextColors[index],
                 sizeof(uint32_t) * (size_t)(self->m_count - index));
    if (self->m_tabVisible)
        XMemmove(&self->m_tabVisible[index + 1],
                 &self->m_tabVisible[index],
                 sizeof(bool) * (size_t)(self->m_count - index));
    if (self->m_tabToolTips)
        XMemmove(&self->m_tabToolTips[index + 1],
                 &self->m_tabToolTips[index],
                 sizeof(XString*) * (size_t)(self->m_count - index));
    if (self->m_tabWhatsThis)
        XMemmove(&self->m_tabWhatsThis[index + 1],
                 &self->m_tabWhatsThis[index],
                 sizeof(XString*) * (size_t)(self->m_count - index));
    if (self->m_tabAccessibleNames)
        XMemmove(&self->m_tabAccessibleNames[index + 1],
                 &self->m_tabAccessibleNames[index],
                 sizeof(XString*) * (size_t)(self->m_count - index));
    if (self->m_tabIcons)
        XMemmove(&self->m_tabIcons[index + 1], &self->m_tabIcons[index],
                 sizeof(XString*) * (size_t)(self->m_count - index));
    if (self->m_tabData)
        XMemmove(&self->m_tabData[index + 1], &self->m_tabData[index],
                 sizeof(XString*) * (size_t)(self->m_count - index));
    if (self->m_tabButtons)
        XMemmove(&self->m_tabButtons[index + 1],
                 &self->m_tabButtons[index],
                 sizeof(XAbstractButton*) * (size_t)(self->m_count - index));
    if (self->m_tabLeftButtons)
        XMemmove(&self->m_tabLeftButtons[index + 1],
                 &self->m_tabLeftButtons[index],
                 sizeof(XAbstractButton*) * (size_t)(self->m_count - index));
    if (self->m_tabCloseOwned)
        XMemmove(&self->m_tabCloseOwned[index + 1],
                 &self->m_tabCloseOwned[index],
                 sizeof(bool) * (size_t)(self->m_count - index));
    if (self->m_lastTabs)
        XMemmove(&self->m_lastTabs[index + 1], &self->m_lastTabs[index],
                 sizeof(int16_t) * (size_t)(self->m_count - index));
    self->m_titles[index] = copy;
    self->m_enabled[index] = true;
    if (self->m_tabTextColors) self->m_tabTextColors[index] = 0;
    if (self->m_tabVisible) self->m_tabVisible[index] = true;
    if (self->m_tabToolTips) self->m_tabToolTips[index] = NULL;
    if (self->m_tabWhatsThis) self->m_tabWhatsThis[index] = NULL;
    if (self->m_tabAccessibleNames) self->m_tabAccessibleNames[index] = NULL;
    if (self->m_tabIcons) self->m_tabIcons[index] = NULL;
    if (self->m_tabData) self->m_tabData[index] = NULL;
    if (self->m_tabButtons) self->m_tabButtons[index] = NULL;
    if (self->m_tabLeftButtons) self->m_tabLeftButtons[index] = NULL;
    if (self->m_tabCloseOwned) self->m_tabCloseOwned[index] = false;
    if (self->m_lastTabs) self->m_lastTabs[index] = -1;
    if (self->m_tabWidths) self->m_tabWidths[index] = 0;
    ++self->m_count;
    /* 对标 qtabbar.cpp:967-971 + 本机 Qt 6.8 探针：
     *  - 首项经 setCurrentIndex(index) 走发射路径（订阅者收得到
     *    currentChanged）；
     *  - 在当前项**之前或同位**插入时当前索引静默右移（qtabbar.cpp:970-971
     *    `else if (index <= d->currentIndex) ++d->currentIndex;`，探针
     *    current 0 → 1 且无 currentChanged）。 */
    if (self->m_currentIndex < 0) {
        XTabBar_setCurrentIndex(self, index);
    } else if (index <= self->m_currentIndex) {
        ++self->m_currentIndex;
    }
    xtabbar_autoHideTabs(self); /* 对标 qtabbar.cpp:999 autoHideTabs。 */
    xtabbar_invalidateLayout(self);
    xtabbar_syncCloseButtons(self);
    xtabbar_updateSizeHint(self);
    xtabbar_layoutButtons(self);
    XTabBar_tabInserted(self, index); /* 对标 QTabBar::tabInserted(index)。 */
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
    return index;
}
int XTabBar_insertTab_2(XTabBar* self, int index, const char* text)
{
    /* NULL 文本按头文件前置条件（“不能为 NULL”）拒绝：与 XString 主
       版本 insertTab 的 !text→-1 守卫同口径，不再静默降级为空标题
       （静默插入会令 addTab_2(NULL) 偏离同族契约并扰动计数口径）。 */
    if (!text) return -1;
    XString_Init_Utf8(tmp, text);
    index = XTabBar_insertTab(self, index, tmp);
    XClassDeinit(tmp);
    return index;
}

void XTabBar_removeTab(XTabBar* self, int index)
{
    int removedLastTab;
    if (!self || index < 0 || index >= self->m_count) return;
    /* 被删页签自己的 lastTab 须在数组搬移前快照（SelectPreviousTab 用）。 */
    removedLastTab = self->m_lastTabs ? (int)self->m_lastTabs[index] : -1;
    if (removedLastTab > index) --removedLastTab; /* 其后页签左移一格。 */
    /* 角按钮随页签销毁（对标 qtabbar.cpp:1020-1028：左右按钮
       deleteLater；reparent 后已挂本控件父子链，延迟删除不与本函数
       调用帧竞争）。 */
    if (self->m_tabButtons && self->m_tabButtons[index]) {
        XObject_deleteLater((XObject*)self->m_tabButtons[index]);
        self->m_tabButtons[index] = NULL;
    }
    if (self->m_tabLeftButtons && self->m_tabLeftButtons[index]) {
        XObject_deleteLater((XObject*)self->m_tabLeftButtons[index]);
        self->m_tabLeftButtons[index] = NULL;
    }
    if (self->m_titles[index]) XClassDelete(self->m_titles[index]);
    if (self->m_tabToolTips && self->m_tabToolTips[index]) {
        XClassDelete(self->m_tabToolTips[index]);
        self->m_tabToolTips[index] = NULL;
    }
    if (self->m_tabWhatsThis && self->m_tabWhatsThis[index]) {
        XClassDelete(self->m_tabWhatsThis[index]);
        self->m_tabWhatsThis[index] = NULL;
    }
    if (self->m_tabAccessibleNames && self->m_tabAccessibleNames[index]) {
        XClassDelete(self->m_tabAccessibleNames[index]);
        self->m_tabAccessibleNames[index] = NULL;
    }
    if (self->m_tabIcons && self->m_tabIcons[index]) {
        XClassDelete(self->m_tabIcons[index]);
        self->m_tabIcons[index] = NULL;
    }
    if (self->m_tabData && self->m_tabData[index]) {
        XClassDelete(self->m_tabData[index]);
        self->m_tabData[index] = NULL;
    }
    XMemmove(&self->m_titles[index], &self->m_titles[index + 1],
             sizeof(XString*) * (size_t)(self->m_count - index - 1));
    XMemmove(&self->m_enabled[index], &self->m_enabled[index + 1],
             sizeof(bool) * (size_t)(self->m_count - index - 1));
    if (self->m_tabTextColors)
        XMemmove(&self->m_tabTextColors[index],
                 &self->m_tabTextColors[index + 1],
                 sizeof(uint32_t) * (size_t)(self->m_count - index - 1));
    if (self->m_tabVisible)
        XMemmove(&self->m_tabVisible[index], &self->m_tabVisible[index + 1],
                 sizeof(bool) * (size_t)(self->m_count - index - 1));
    if (self->m_tabToolTips)
        XMemmove(&self->m_tabToolTips[index], &self->m_tabToolTips[index + 1],
                 sizeof(XString*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabWhatsThis)
        XMemmove(&self->m_tabWhatsThis[index], &self->m_tabWhatsThis[index + 1],
                 sizeof(XString*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabAccessibleNames)
        XMemmove(&self->m_tabAccessibleNames[index],
                 &self->m_tabAccessibleNames[index + 1],
                 sizeof(XString*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabIcons)
        XMemmove(&self->m_tabIcons[index], &self->m_tabIcons[index + 1],
                 sizeof(XString*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabData)
        XMemmove(&self->m_tabData[index], &self->m_tabData[index + 1],
                 sizeof(XString*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabButtons)
        XMemmove(&self->m_tabButtons[index], &self->m_tabButtons[index + 1],
                 sizeof(XAbstractButton*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabLeftButtons)
        XMemmove(&self->m_tabLeftButtons[index],
                 &self->m_tabLeftButtons[index + 1],
                 sizeof(XAbstractButton*) * (size_t)(self->m_count - index - 1));
    if (self->m_tabCloseOwned)
        XMemmove(&self->m_tabCloseOwned[index],
                 &self->m_tabCloseOwned[index + 1],
                 sizeof(bool) * (size_t)(self->m_count - index - 1));
    if (self->m_lastTabs) {
        int k;
        XMemmove(&self->m_lastTabs[index], &self->m_lastTabs[index + 1],
                 sizeof(int16_t) * (size_t)(self->m_count - index - 1));
        self->m_lastTabs[self->m_count - 1] = -1;
        /* 页签左移：指向被删页签的 lastTab 失效，指向其后页签的减一
           （Qt 在 removeTab 里同步维护 Tab::lastTab）。 */
        for (k = 0; k < self->m_count - 1; ++k) {
            if (self->m_lastTabs[k] == (int16_t)index)
                self->m_lastTabs[k] = -1;
            else if (self->m_lastTabs[k] > (int16_t)index)
                --self->m_lastTabs[k];
        }
    }
    if (self->m_tabWidths) {
        XMemmove(&self->m_tabWidths[index], &self->m_tabWidths[index + 1],
                 sizeof(uint16_t) * (size_t)(self->m_count - index - 1));
        self->m_tabWidths[self->m_count - 1] = 0;
    }
    --self->m_count;
    /* R-79 根因：删当前页之前的页签时 m_currentIndex 不左移，同下标
       改指后一页签（选中右移一位）且不发射 currentChanged，偏离
       Qt 6.8.3 qtabbar.cpp removeTab。对标：index<current 经
       setCurrentIndex(current-1) 左移并发射；index==current 先复位
       -1，按 selectionBehaviorOnRemove 定新当前页（SelectPreviousTab 取
       该页的 lastTab——逐页跟踪），当前页变化必发 currentChanged
       （列表清空发射 -1）。 */
    if (index < self->m_currentIndex) {
        XTabBar_setCurrentIndex(self, self->m_currentIndex - 1);
    } else if (index == self->m_currentIndex) {
        int newIndex;
        /* 对标 QTabBar::removeTab 的 selectionBehaviorOnRemove 分支
         * （qtabbar.cpp:1046-1073）：
         * - SelectLeftTab：从 index-1 起向左取首个可见（不足再向右）；
         * - SelectRightTab（缺省）：从 index 起向右取首个可见（不足再向左）；
         * - SelectPreviousTab：取该页的 lastTab（上次停留页；本机 Qt 6.8
         *   探针：访问 0→2→3→2 后移除 2 选中原签 3），lastTab 因移除左移
         *   后越界/不可见再回退 SelectRightTab 口径。 */
        switch (self->m_selectionBehavior) {
        case 0: /* SelectLeftTab */
            newIndex = xtabbar_selectNewCurrentIndexFrom(self, index - 1);
            break;
        case 2: { /* SelectPreviousTab：per-tab lastTab */
            int lastTab = removedLastTab;
            newIndex = -1;
            if (lastTab >= 0 && lastTab < self->m_count &&
                XTabBar_isTabVisible(self, lastTab))
                newIndex = lastTab;
            if (newIndex < 0)
                newIndex = xtabbar_selectNewCurrentIndexFrom(self, index);
            break;
        }
        default: /* SelectRightTab */
            newIndex = xtabbar_selectNewCurrentIndexFrom(self, index);
            break;
        }
        if (self->m_count > 0 && newIndex >= 0) {
            if (newIndex > self->m_count - 1)
                newIndex = self->m_count - 1;
            self->m_currentIndex = -1; /* 强制 setCurrentIndex 视为变化。 */
            XTabBar_setCurrentIndex(self, newIndex);
        } else {
            self->m_currentIndex = -1;
            xtabbar_emitInt(self,
                            (size_t)XTabBar_currentChanged_signal(self, -1),
                            -1);
        }
    }
    xtabbar_ensureVisible(self, self->m_currentIndex); /* 移除后当前页露出。 */
    xtabbar_hoverReset(self); /* [P3-61] 页签序已变，悬停索引失效收提示。 */
    xtabbar_autoHideTabs(self); /* 对标 qtabbar.cpp:1082 autoHideTabs。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint(self);
    xtabbar_syncCloseButtons(self); /* tabsClosable：余下页签保持有按钮。 */
    xtabbar_layoutButtons(self);
    XTabBar_tabRemoved(self, index); /* 对标 QTabBar::tabRemoved(index)。 */
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

int XTabBar_count(const XTabBar* self) { return self ? self->m_count : 0; }
int XTabBar_currentIndex(const XTabBar* self)
{
    return self ? self->m_currentIndex : -1;
}

void XTabBar_setCurrentIndex(XTabBar* self, int index)
{
    int old;
    if (!self || index < 0 || index >= self->m_count) return;
    if (index == self->m_currentIndex) return;
    old = self->m_currentIndex;
    self->m_currentIndex = index;
    /* [Qt 对齐 2026-11-11] 逐页 lastTab（对标 QTabBarPrivate::Tab::lastTab）：
     * 切到新页时记录「从哪来」——lastTab[新页] = 旧页（本机 Qt 6.8 探针
     * 访问 0→2→3→2 后 lastTab[2]=3，removeTab 的 SelectPreviousTab 据此选中
     * 原 3 号页签）。 */
    if (self->m_lastTabs && old >= 0 && old < self->m_capacity)
        self->m_lastTabs[index] = (int16_t)old;
    xtabbar_ensureVisible(self, index); /* 切换后自动露出（对标 ensureVisible）。 */
    xtabbar_emitInt(self, (size_t)XTabBar_currentChanged_signal(self, index), index);
    XWidget_update((XWidget*)self);
}

XString* XTabBar_tabText(const XTabBar* self, int index)
{
    if (self && index >= 0 && index < self->m_count && self->m_titles[index])
        return XString_create_copy(self->m_titles[index]);
    return NULL;
}
const char* XTabBar_tabText_2(const XTabBar* self, int index)
{
    if (self && index >= 0 && index < self->m_count && self->m_titles[index])
        return XString_toUtf8(self->m_titles[index]);
    return "";
}

void XTabBar_setTabText(XTabBar* self, int index, const XString* text)
{
    if (!self || !text || index < 0 || index >= self->m_count) return;
    XString_assign(self->m_titles[index], text);
    /* 文本宽变化 ⇒ 自然宽/布局宽变化（Qt setTabText → refresh→layoutTabs）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint((XTabBar*)self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}
void XTabBar_setTabText_2(XTabBar* self, int index, const char* text)
{
    if (!self || !text || index < 0 || index >= self->m_count) return;
    XString_assign_utf8(self->m_titles[index], text);
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint((XTabBar*)self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

bool XTabBar_isTabEnabled(const XTabBar* self, int index)
{
    if (self && index >= 0 && index < self->m_count) return self->m_enabled[index];
    return false;
}

void XTabBar_setTabEnabled(XTabBar* self, int index, bool enabled)
{
    if (!self || index < 0 || index >= self->m_count) return;
    self->m_enabled[index] = enabled;
    XWidget_update((XWidget*)self);
}

bool XTabBar_tabsClosable(const XTabBar* self)
{
    return self ? self->m_tabsClosable : false;
}
void XTabBar_setTabsClosable(XTabBar* self, bool closable)
{
    if (!self || self->m_tabsClosable == closable) return;
    self->m_tabsClosable = closable;
    /* [Qt 对齐 2026-11-11] tabsClosable 影响自然宽（Qt 关闭按钮占位 +24），
       并同步每页真实 CloseButton 子控件（Qt 探针：开 ⇒ tabButton(i,
       RightSide) 为 CloseButton；关 ⇒ 空）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint(self);
    xtabbar_syncCloseButtons(self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}
bool XTabBar_isMovable(const XTabBar* self)
{
    return self ? self->m_movable : false;
}
void XTabBar_setMovable(XTabBar* self, bool movable)
{
    if (self) self->m_movable = movable;
}

/* ==================== 信号 ==================== */

void* XTabBar_currentChanged_signal(XTabBar* self, int index)
{
    (void)index;
    return (void*)(size_t)XTabBar_currentChanged_signal;
}
void* XTabBar_tabCloseRequested_signal(XTabBar* self)
{
    return (void*)(size_t)XTabBar_tabCloseRequested_signal;
}
void* XTabBar_tabBarClicked_signal(XTabBar* self, int index)
{
    (void)self;
    (void)index;
    return (void*)(size_t)XTabBar_tabBarClicked_signal;
}

void* XTabBar_tabBarDoubleClicked_signal(XTabBar* self, int index)
{
    (void)self;
    (void)index;
    return (void*)(size_t)XTabBar_tabBarDoubleClicked_signal;
}


















































void* XTabBar_tabMoved_signal(XTabBar* self, int from, int to)
{
    (void)self; (void)from; (void)to;
    return (void*)(size_t)XTabBar_tabMoved_signal;
}

/* ==================== Task 2.2：外观/几何/项属性 ==================== */

/** @brief 设置文档模式（对标 QTabBar::setDocumentMode；缺省 false）。
 * @details [P3-60] 开启后无样式回退路径按文档模式绘制：不画每页基线、
 *          非当前页底填充用 Base（Qt 文档模式淡底口径）；样式路径的
 *          documentMode 选项目前由 B2 串行交付（XStyleOption 扁平位 +
 *          XCommonStyle 分支）。 */
void XTabBar_setDocumentMode(XTabBar* self, bool enable)
{
    if (!self || self->m_documentMode == (enable ? 1u : 0u)) return;
    self->m_documentMode = enable ? 1u : 0u;
    XWidget_update((XWidget*)self); /* [P3-60] 纯绘制开关 ⇒ 标脏。 */
}
bool XTabBar_documentMode(const XTabBar* self)
{ return self ? self->m_documentMode : false; }

void XTabBar_setElideMode(XTabBar* self, int mode)
{
    /* 对齐 Qt：QTabBar::setElideMode 直接存值（qtabbar.cpp:2506-2510），
     * 取值域为 Qt::TextElideMode 0..3。越界值忽略，避免 3 位位域截断。 */
    if (!self) return;
    if (mode < (int)XAbstractItemViewTextElideMode_ElideLeft ||
        mode > (int)XAbstractItemViewTextElideMode_ElideNone)
        return;
    if ((int)self->m_elideMode == mode) return; /* 同值早退（Qt 同）。 */
    self->m_elideMode = (uint32_t)mode;
    XWidget_update((XWidget*)self);
}
int XTabBar_elideMode(const XTabBar* self)
{
    /* 缺省对齐 Qt Fusion：QTabBarPrivate 从 styleHint(SH_TabBar_ElideMode)
     * 初始化（qtabbar.cpp:392），QCommonStyle 返回 Qt::ElideNone(3)
     * （本机 Qt 6.8 探针 tabBar.elideMode()==ElideNone）。 */
    return self ? (int)self->m_elideMode
                : (int)XAbstractItemViewTextElideMode_ElideNone;
}

void XTabBar_setExpanding(XTabBar* self, bool enable)
{
    if (!self || self->m_expanding == enable) return;
    self->m_expanding = enable;
    /* expanding 直接决定布局宽（均分 vs 自然宽）⇒ 重排（Qt 同）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}
bool XTabBar_expanding(const XTabBar* self)
{ return self ? self->m_expanding : true; }

void XTabBar_setUsesScrollButtons(XTabBar* self, bool enable)
{
    if (!self || self->m_usesScrollButtons == enable) return;
    self->m_usesScrollButtons = enable;
    if (!enable) self->m_scrollOffset = 0; /* 回退旧布局，清偏移。 */
    /* 布局模式切换 ⇒ 尺寸提示重算（Qt setUsesScrollButtons → refresh）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint(self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}
bool XTabBar_usesScrollButtons(const XTabBar* self)
{ return self ? self->m_usesScrollButtons : true; }

bool XTabBar_isOverflowed(const XTabBar* self)
{
    return xtabbar_overflowW(self, xtabbar_barLength(self));
}

int XTabBar_scrollOffset(const XTabBar* self)
{
    int tW, cW, btnW, viewX, viewW, maxOff;
    if (!self) return 0;
    if (!xtabbar_scrollLayout(self, &tW, &cW, &btnW, &viewX, &viewW,
                              &maxOff))
        return 0;
    return xtabbar_effOffset(self, maxOff);
}

int XTabBar_barHeightHint(const XTabBar* self, int forWidth)
{
    /* Qt QTabBar 恒单行（无换行布局），条形高 = 页签高（缺省 24；图标
     * 高超过默认时撑高，见 xtabbar_tabHeight）；forWidth 仅保留签名兼容
     * （XTabWidget 按容器宽询问条高）。[P0-01] 竖排（West/East 形状）
     * 时该值由 XTabWidget 用作**竖条厚度**（条几何宽）——与页签截面
     * 厚同一基线。 */
    (void)forWidth;
    return self ? xtabbar_tabHeight(self) : 0;
}

/** @brief 设置基底绘制（对标 QTabBar::setDrawBase；缺省 true）。
 * @details [P3-59] 关闭后不画每页贴边基线（此前绘制无条件画、视觉零
 *          变化）；Qt 语义 = PE_FrameTabBarBase 由 drawBase 控制。 */
void XTabBar_setDrawBase(XTabBar* self, bool enable)
{
    if (!self || self->m_drawBase == (enable ? 1u : 0u)) return;
    self->m_drawBase = enable ? 1u : 0u;
    XWidget_update((XWidget*)self); /* [P3-59] 纯绘制开关 ⇒ 标脏。 */
}
bool XTabBar_drawBase(const XTabBar* self)
{ return self ? self->m_drawBase : true; }

void XTabBar_setShape(XTabBar* self, int shape)
{
    /* 对标 QTabBar::setShape（qtabbar.cpp:878-886）：同值早退，取值域为
     * Qt::TabShape 全 8 值（RoundedNorth=0 .. TriangularEast=7）。此前
     * 字段只有 2 位、且不做校验 ⇒ setShape(4) 静默截断成 0（三角形南向
     * 被读回圆形北向）。字段已扩到 3 位，越界值忽略。
     * [P0-01] West/East 四形状即竖排判据（QTabBarPrivate::verticalTabs），
     * 形状切换改变布局轴向 ⇒ 布局缓存/尺寸提示/按钮落位同拍重算
     * （Qt setShape → refresh→layoutTabs）。 */
    if (!self || shape < 0 || shape > 7 || self->m_shape == (uint32_t)shape)
        return;
    self->m_shape = (uint32_t)shape;
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint(self);
    XWidget_updateGeometry((XWidget*)self); /* 竖排转置尺寸提示随动。 */
    xtabbar_layoutButtons(self);
    XWidget_update((XWidget*)self);
}
int XTabBar_shape(const XTabBar* self)
{ return self ? self->m_shape : 0; }

void XTabBar_setIconSize(XTabBar* self, int size)
{
    /* 旧单值入口：方边语义（宽=高）委托二维重载。 */
    XSize s;
    if (!self) return;
    XSize_init(&s, size, size);
    XTabBar_setIconSize_2(self, s);
}

void XTabBar_setIconSize_2(XTabBar* self, XSize size)
{
    if (!self || size.width < 0 || size.height < 0) return;
    if (size.width == (int)self->m_iconSizeW &&
        size.height == (int)self->m_iconSizeH)
        return;
    /* 0 = 该轴取默认 16（旧单值入口 setIconSize(0) 的复位语义）。 */
    self->m_iconSizeW = (uint16_t)size.width;
    self->m_iconSizeH = (uint16_t)size.height;
    /* 图标尺寸影响自然宽/页签高 ⇒ 重排（Qt setIconSize 后 refresh +
     * updateGeometry，qtabbar.cpp:1464-1471）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint(self);
    /* 页签高可能变化 ⇒ 通知父布局重排（XWidget_setSizeHint 自身不失效，
     * 对齐 Qt updateGeometry；否则容器要等下次 resize 才让出新条高）。 */
    XWidget_updateGeometry((XWidget*)self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

int XTabBar_iconSize(const XTabBar* self)
{
    /* 缺省对齐样式 PM_TabBarIconSize：Qt 6.8 Fusion 实测 16x16
     * （本库未显式设置时返回 16，而非旧实现的 0 哨兵）。
     * 非方形时取宽（Qt 无 int 版；二维用 XTabBar_iconSize_2）。 */
    return self ? xtabbar_iconWidth(self) : XTABBAR_DEFAULT_ICON_SIZE;
}

XSize XTabBar_iconSize_2(const XTabBar* self)
{
    XSize size;
    XSize_init(&size, xtabbar_iconWidth(self), xtabbar_iconHeight(self));
    return size;
}

void XTabBar_setAutoHide(XTabBar* self, bool enable)
{
    /* 对标 qtabbar.cpp:2743-2749：开启即按 count>1 重算显隐；
       关闭恢复常显（此前仅存状态，显隐从不随动）。 */
    if (!self || self->m_autoHide == enable) return;
    self->m_autoHide = enable;
    if (enable)
        xtabbar_autoHideTabs(self);
    else
        XWidget_setVisible((XWidget*)self, true);
}
bool XTabBar_autoHide(const XTabBar* self)
{ return self ? self->m_autoHide : false; }

void XTabBar_setSelectionBehaviorOnRemove(XTabBar* self, int behavior)
{
    if (!self || behavior < 0 || behavior > 2) return;
    self->m_selectionBehavior = (uint32_t)behavior;
}
int XTabBar_selectionBehaviorOnRemove(const XTabBar* self)
{
    /* 缺省 SelectRightTab(1)（qtabbar_p.h:80；本机 Qt 6.8 探针）。 */
    return self ? (int)self->m_selectionBehavior : 1;
}

void XTabBar_setChangeCurrentOnDrag(XTabBar* self, bool enable)
{ if (self) self->m_changeCurrentOnDrag = enable; }
bool XTabBar_changeCurrentOnDrag(const XTabBar* self)
{ return self ? self->m_changeCurrentOnDrag : false; }

bool XTabBar_tabRect(const XTabBar* self, int index, XRect* out)
{
    int tW, cW, btnW, viewX, viewW, maxOff;
    int w;
    int x;
    if (!self || !out || index < 0 || index >= self->m_count) return false;
    xtabbar_ensureLayout((XTabBar*)self);
    if (!self->m_tabWidths) return false;
    w = (int)self->m_tabWidths[index];
    if (xtabbar_isVertical(self)) {
        /* [P0-01] 竖排：沿 y 累计（含滚动偏移），截面宽 = 页签厚
         * （与横排页签高同一 24 基线；Qt 竖排 tabRect 两轴转置）。 */
        if (xtabbar_scrollLayout(self, &tW, &cW, &btnW, &viewX, &viewW,
                                 &maxOff)) {
            x = xtabbar_tabScreenX(self, index, viewX,
                                   xtabbar_effOffset(self, maxOff));
            XRect_init(out, 0, x, xtabbar_tabHeight(self), w);
            return true;
        }
        x = xtabbar_tabLeftAt(self, index);
        XRect_init(out, 0, x, xtabbar_tabHeight(self), w);
        return true;
    }
    if (xtabbar_scrollLayout(self, &tW, &cW, &btnW, &viewX, &viewW,
                             &maxOff)) {
        /* 滚动模式：返回视口内实际矩形（含偏移；越界页签可为负 x）。 */
        x = xtabbar_tabScreenX(self, index, viewX,
                               xtabbar_effOffset(self, maxOff));
        XRect_init(out, x, 0, w, xtabbar_tabHeight(self));
        return true;
    }
    x = xtabbar_tabLeftAt(self, index);
    XRect_init(out, x, 0, w, xtabbar_tabHeight(self));
    return true;
}

int XTabBar_tabAt(const XTabBar* self, const XPoint* pos)
{
    return xtabbar_tabAt(self, pos);
}

/* [P2-54] XTabBar_tabWidth 已删（X 扩展、Qt 无公共对应）：等价
 * XTabBar_tabRect(self,0,&r) → r.width，调用点见 XTabBar.h 过渡别名注。 */

int XTabBar_tabHeight(const XTabBar* self)
{
    return xtabbar_tabHeight(self);
}

/* [P2-55] XTabBar_tabIndexAt 已删：原实现就是组 XPoint 后转调
 * XTabBar_tabAt（同一内部函数 xtabbar_tabAt），调用点迁 tabAt。
 * [P2-56] XTabBar_isEmpty 已删：与 XTabBar_count(self)==0 完全等价
 * （NULL 时 count 返回 0，同判空）。 */

void XTabBar_setTabIcon(XTabBar* self, int index, const XString* path)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabIcons)
        return;
    repl = path ? XString_create_copy(path) : NULL;
    if (path && !repl) return;
    if (self->m_tabIcons[index]) XClassDelete(self->m_tabIcons[index]);
    self->m_tabIcons[index] = repl;
    /* 图标占位影响自然宽 ⇒ 重排（Qt setTabIcon → refresh/layoutTabs）。 */
    xtabbar_invalidateLayout(self);
    xtabbar_updateSizeHint((XTabBar*)self);
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

void XTabBar_setTabIcon_2(XTabBar* self, int index, const char* path)
{
    XString* tmp = NULL;
    if (path) {
        tmp = XString_create_utf8(path);
        if (!tmp) return;
    }
    XTabBar_setTabIcon(self, index, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XTabBar_tabIcon(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabIcons)
        return NULL;
    return self->m_tabIcons[index];
}

const char* XTabBar_tabIcon_2(const XTabBar* self, int index)
{
    const XString* s;
    s = XTabBar_tabIcon(self, index);
    return s ? XString_toUtf8(s) : "";
}

/** @brief 设置页签文本色（对标 QTabBar::setTabTextColor）。
 * @details [P3-58] 存色即生效：绘制路径（样式选项 m_textColor 与无样式
 *          回退）在**普通态**用它（选中页签仍用常规选中文本色，禁用仍
 *          用 Disabled 色）；0=恢复调色板决定。 */
void XTabBar_setTabTextColor(XTabBar* self, int index, uint32_t color)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabTextColors)
        return;
    self->m_tabTextColors[index] = color;
    XWidget_update((XWidget*)self);
}

uint32_t XTabBar_tabTextColor(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabTextColors)
        return 0;
    return self->m_tabTextColors[index];
}

/** @brief 设置页签提示（对标 QTabBar::setTabToolTip）。
 * @details [P3-61] 悬停该页签时弹出（MouseMove 命中变化即经
 *          XToolTip_showText 锚页签矩形弹出，离开/换页/拖拽收起；
 *          提示载荷在弹出路径深拷贝，本数组仅作存储）。 */
void XTabBar_setTabToolTip(XTabBar* self, int index, const XString* tip)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabToolTips)
        return;
    repl = tip ? XString_create_copy(tip) : NULL;
    if (tip && !repl) return;
    if (self->m_tabToolTips[index])
        XClassDelete(self->m_tabToolTips[index]);
    self->m_tabToolTips[index] = repl;
}

void XTabBar_setTabToolTip_2(XTabBar* self, int index, const char* tip)
{
    XString* tmp = NULL;
    if (tip) {
        tmp = XString_create_utf8(tip);
        if (!tmp) return;
    }
    XTabBar_setTabToolTip(self, index, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XTabBar_tabToolTip(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabToolTips)
        return NULL;
    return self->m_tabToolTips[index];
}

const char* XTabBar_tabToolTip_2(const XTabBar* self, int index)
{
    const XString* s;
    s = XTabBar_tabToolTip(self, index);
    return s ? XString_toUtf8(s) : "";
}

void XTabBar_setTabWhatsThis(XTabBar* self, int index, const XString* text)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabWhatsThis)
        return;
    repl = text ? XString_create_copy(text) : NULL;
    if (text && !repl) return;
    if (self->m_tabWhatsThis[index])
        XClassDelete(self->m_tabWhatsThis[index]);
    self->m_tabWhatsThis[index] = repl;
}

void XTabBar_setTabWhatsThis_2(XTabBar* self, int index, const char* text)
{
    XString* tmp = NULL;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return;
    }
    XTabBar_setTabWhatsThis(self, index, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XTabBar_tabWhatsThis(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabWhatsThis)
        return NULL;
    return self->m_tabWhatsThis[index];
}

const char* XTabBar_tabWhatsThis_2(const XTabBar* self, int index)
{
    const XString* s;
    s = XTabBar_tabWhatsThis(self, index);
    return s ? XString_toUtf8(s) : "";
}

void XTabBar_setAccessibleTabName(XTabBar* self, int index,
                                  const XString* name)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_tabAccessibleNames)
        return;
    repl = name ? XString_create_copy(name) : NULL;
    if (name && !repl) return;
    if (self->m_tabAccessibleNames[index])
        XClassDelete(self->m_tabAccessibleNames[index]);
    self->m_tabAccessibleNames[index] = repl;
}

void XTabBar_setAccessibleTabName_2(XTabBar* self, int index, const char* name)
{
    XString* tmp = NULL;
    if (name) {
        tmp = XString_create_utf8(name);
        if (!tmp) return;
    }
    XTabBar_setAccessibleTabName(self, index, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XTabBar_accessibleTabName(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_tabAccessibleNames)
        return NULL;
    return self->m_tabAccessibleNames[index];
}

const char* XTabBar_accessibleTabName_2(const XTabBar* self, int index)
{
    const XString* s;
    s = XTabBar_accessibleTabName(self, index);
    return s ? XString_toUtf8(s) : "";
}

/** @brief 槽位按钮落位（对标 QTabBar::setTabButton，qtabbar.cpp:2791-2815）：
 *  新按钮 reparent 归本控件并 show；同槽旧按钮隐藏（不销毁，仍挂本
 *  控件父子链，Qt 同接管语义）。position 非 LeftSide 一律按 RightSide
 *  处理（Qt 同 `position == LeftSide ? left : right`）。 */
static void xtabbar_setButtonSlot(XTabBar* self, int index,
                                  XTabBarButtonPosition position,
                                  XAbstractButton* button)
{
    XAbstractButton** slot = (position == XTABBAR_BUTTON_LEFTSIDE)
                                 ? self->m_tabLeftButtons
                                 : self->m_tabButtons;
    bool owned = false;
    if (!slot) return;
    if (position == XTABBAR_BUTTON_RIGHTSIDE && self->m_tabCloseOwned)
        owned = self->m_tabCloseOwned[index];
    if (button) {
        /* [Qt 对齐 2026-11-11] 覆盖自动关闭按钮时先回收它（否则
         * tabsClosable 关闭时会误删用户的自定义按钮）。 */
        if (owned && slot[index] && slot[index] != button) {
            XAbstractButton* old = slot[index];
            slot[index] = NULL;
            self->m_tabCloseOwned[index] = false;
            XObject_deleteLater((XObject*)old);
            owned = false;
        }
        /* reparent 只挂父链不产生可见性：框架显式 show 语义下新按钮
         * m_explicitShow=0 恒不可见，对标 Qt 的 widget->show() 补显式
         * show（对标 qtabbar.cpp:2797）。 */
        XWidget_setParent((XWidget*)button, (XWidget*)self, 0);
        XWidget_show((XWidget*)button);
    }
    if (slot[index] && slot[index] != button)
        XWidget_setVisible((XWidget*)slot[index], false); /* 旧按钮隐藏。 */
    slot[index] = button;
    if (position == XTABBAR_BUTTON_RIGHTSIDE && self->m_tabCloseOwned &&
        !owned)
        self->m_tabCloseOwned[index] = false; /* 用户自定义按钮不归关闭开关管。 */
    XWidget_update((XWidget*)self);
}

void XTabBar_setTabButton(XTabBar* self, int index,
                          XTabBarButtonPosition position,
                          XAbstractButton* button)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (position == XTABBAR_BUTTON_LEFTSIDE
            ? !self->m_tabLeftButtons : !self->m_tabButtons)
        return;
    xtabbar_setButtonSlot(self, index, position, button);
}

XAbstractButton* XTabBar_tabButton(const XTabBar* self, int index,
                                   XTabBarButtonPosition position)
{
    XAbstractButton* const* slot;
    if (!self || index < 0 || index >= self->m_count) return NULL;
    slot = (position == XTABBAR_BUTTON_LEFTSIDE) ? self->m_tabLeftButtons
                                                 : self->m_tabButtons;
    if (!slot) return NULL;
    return slot[index];
}

void XTabBar_setTabButtonRight(XTabBar* self, int index,
                               XAbstractButton* button)
{
    /* 便捷版 = RightSide 槽（Qt 关闭按钮默认槽位）。 */
    XTabBar_setTabButton(self, index, XTabBarButtonPosition_RightSide,
                         button);
}

XAbstractButton* XTabBar_tabButtonRight(const XTabBar* self, int index)
{
    return XTabBar_tabButton(self, index, XTabBarButtonPosition_RightSide);
}

void XTabBar_setTabData(XTabBar* self, int index, const XString* data)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabData)
        return;
    repl = data ? XString_create_copy(data) : NULL;
    if (data && !repl) return;
    if (self->m_tabData[index]) XClassDelete(self->m_tabData[index]);
    self->m_tabData[index] = repl;
}

void XTabBar_setTabData_2(XTabBar* self, int index, const char* data)
{
    XString* tmp = NULL;
    if (data) {
        tmp = XString_create_utf8(data);
        if (!tmp) return;
    }
    XTabBar_setTabData(self, index, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XTabBar_tabData(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabData)
        return NULL;
    return self->m_tabData[index];
}

const char* XTabBar_tabData_2(const XTabBar* self, int index)
{
    const XString* s;
    s = XTabBar_tabData(self, index);
    return s ? XString_toUtf8(s) : "";
}

/** @brief 从 pos 起找新的当前页：先向右、再向左取首个**可见**页签
 *  （对标 QTabBarPrivate::selectNewCurrentIndexFrom，qtabbar.cpp 私有实现；
 *  无可见页签返回 -1）。 */
static int xtabbar_selectNewCurrentIndexFrom(const XTabBar* self, int pos)
{
    int i;
    if (!self || pos < 0) pos = 0;
    for (i = pos; i < self->m_count; ++i)
        if (XTabBar_isTabVisible(self, i)) return i;
    for (i = pos - 1; i >= 0; --i)
        if (XTabBar_isTabVisible(self, i)) return i;
    return -1;
}

void XTabBar_setTabVisible(XTabBar* self, int index, bool visible)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabVisible)
        return;
    if (self->m_tabVisible[index] == visible) return; /* 同值早退（Qt 同）。 */
    self->m_tabVisible[index] = visible;
    /* [Qt 对齐 2026-11-11] 隐藏**当前页**时按 Qt 改选可见页
     * （qtabbar.cpp:1149-1166）。本机 Qt 6.8 探针：
     *  - tabs 0..3 当前 1，隐藏 1 ⇒ currentChanged(2)（右邻优先）；
     *  - 仅剩 0/1 时隐藏 1 ⇒ currentChanged(0)（右无则左）；
     *  - **全部隐藏**时不改选、也不发 currentChanged(-1)：探针 current 仍为
     *    0 且无信号（QTabBar::setCurrentIndex 拒绝 -1），本实现同口径。 */
    if (!visible && index == self->m_currentIndex) {
        int newIndex = xtabbar_selectNewCurrentIndexFrom(self, index + 1);
        if (newIndex >= 0 && newIndex < self->m_count)
            XTabBar_setCurrentIndex(self, newIndex);
    }
    xtabbar_invalidateLayout(self); /* 可见集合变化 ⇒ 布局与尺寸提示重算。 */
    xtabbar_updateSizeHint(self);   /* Qt refresh：仅可见页签计宽。 */
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

bool XTabBar_isTabVisible(const XTabBar* self, int index)
{
    /* 对标 QTabBar::isTabVisible（qtabbar.cpp:1136-1140）：越界返回
     * **false**（此前本实现返回 true，与 Qt 相反）。 */
    if (!self || index < 0 || index >= self->m_count || !self->m_tabVisible)
        return false;
    return self->m_tabVisible[index];
}

void XTabBar_moveTab(XTabBar* self, int from, int to)
{
    XString* title;
    bool enabled;
    uint32_t color;
    bool visible;
    XString* tip;
    XString* icon;
    XString* data;
    XAbstractButton* button;
    XAbstractButton* leftButton;
    XString* whatsThis;
    XString* accessibleName;
    int i;
    int step;
    int prevCurrent;
    int16_t movedLastTab = -1;
    bool movedCloseOwned = false;
    if (!self || from < 0 || from >= self->m_count || to < 0 ||
        to >= self->m_count || from == to)
        return;
    prevCurrent = self->m_currentIndex;
    title = self->m_titles[from];
    enabled = self->m_enabled[from];
    color = self->m_tabTextColors ? self->m_tabTextColors[from] : 0;
    visible = self->m_tabVisible ? self->m_tabVisible[from] : true;
    tip = self->m_tabToolTips ? self->m_tabToolTips[from] : NULL;
    whatsThis = self->m_tabWhatsThis ? self->m_tabWhatsThis[from] : NULL;
    accessibleName = self->m_tabAccessibleNames
                         ? self->m_tabAccessibleNames[from] : NULL;
    icon = self->m_tabIcons ? self->m_tabIcons[from] : NULL;
    data = self->m_tabData ? self->m_tabData[from] : NULL;
    button = self->m_tabButtons ? self->m_tabButtons[from] : NULL;
    leftButton = self->m_tabLeftButtons ? self->m_tabLeftButtons[from] : NULL;
    if (self->m_lastTabs) movedLastTab = self->m_lastTabs[from];
    if (self->m_tabCloseOwned) movedCloseOwned = self->m_tabCloseOwned[from];
    step = (from < to) ? 1 : -1;
    for (i = from; i != to; i += step) {
        self->m_titles[i] = self->m_titles[i + step];
        self->m_enabled[i] = self->m_enabled[i + step];
        if (self->m_tabTextColors)
            self->m_tabTextColors[i] = self->m_tabTextColors[i + step];
        if (self->m_tabVisible)
            self->m_tabVisible[i] = self->m_tabVisible[i + step];
        if (self->m_tabToolTips)
            self->m_tabToolTips[i] = self->m_tabToolTips[i + step];
        if (self->m_tabWhatsThis)
            self->m_tabWhatsThis[i] = self->m_tabWhatsThis[i + step];
        if (self->m_tabAccessibleNames)
            self->m_tabAccessibleNames[i] = self->m_tabAccessibleNames[i + step];
        if (self->m_tabIcons)
            self->m_tabIcons[i] = self->m_tabIcons[i + step];
        if (self->m_tabData)
            self->m_tabData[i] = self->m_tabData[i + step];
        if (self->m_tabButtons)
            self->m_tabButtons[i] = self->m_tabButtons[i + step];
        if (self->m_tabLeftButtons)
            self->m_tabLeftButtons[i] = self->m_tabLeftButtons[i + step];
        if (self->m_tabCloseOwned)
            self->m_tabCloseOwned[i] = self->m_tabCloseOwned[i + step];
        if (self->m_lastTabs)
            self->m_lastTabs[i] = self->m_lastTabs[i + step];
    }
    self->m_titles[to] = title;
    self->m_enabled[to] = enabled;
    if (self->m_tabTextColors) self->m_tabTextColors[to] = color;
    if (self->m_tabVisible) self->m_tabVisible[to] = visible;
    if (self->m_tabToolTips) self->m_tabToolTips[to] = tip;
    if (self->m_tabWhatsThis) self->m_tabWhatsThis[to] = whatsThis;
    if (self->m_tabAccessibleNames) self->m_tabAccessibleNames[to] = accessibleName;
    if (self->m_tabIcons) self->m_tabIcons[to] = icon;
    if (self->m_tabData) self->m_tabData[to] = data;
    if (self->m_tabButtons) self->m_tabButtons[to] = button;
    if (self->m_tabLeftButtons) self->m_tabLeftButtons[to] = leftButton;
    if (self->m_tabCloseOwned) self->m_tabCloseOwned[to] = movedCloseOwned;
    if (self->m_lastTabs) self->m_lastTabs[to] = movedLastTab;
    /* lastTab 指向的页签也随换位改号（Qt moveTab 同步维护 lastTab）。 */
    if (self->m_lastTabs) {
        for (i = 0; i < self->m_count; ++i) {
            int lt = self->m_lastTabs[i];
            if (lt == from) lt = to;
            else if (from < to && lt > from && lt <= to) --lt;
            else if (from > to && lt >= to && lt < from) ++lt;
            self->m_lastTabs[i] = (int16_t)lt;
        }
    }
    if (self->m_currentIndex == from) self->m_currentIndex = to;
    else if (self->m_currentIndex > from && self->m_currentIndex <= to)
        self->m_currentIndex--;
    else if (self->m_currentIndex < from && self->m_currentIndex >= to)
        self->m_currentIndex++;
    xtabbar_ensureVisible(self, self->m_currentIndex); /* 换位后当前页露出。 */
    xtabbar_emitInt2(self, (size_t)XTabBar_tabMoved_signal(self, from, to),
                     from, to);
    /* 对标 qtabbar.cpp:2087-2089：换位致当前索引变化时补发
       currentChanged（此前订阅者只见选中项被静默改指）。 */
    if (prevCurrent != self->m_currentIndex)
        xtabbar_emitInt(self,
                        (size_t)XTabBar_currentChanged_signal(
                            self, self->m_currentIndex),
                        self->m_currentIndex);
    xtabbar_invalidateLayout(self); /* 顺序变化 ⇒ 布局重算。 */
    xtabbar_hoverReset(self); /* [P3-61] 换位后悬停索引重对（下一次 move 重算）。 */
    xtabbar_layoutButtons(self);
    XTabBar_tabLayoutChange(self);
    XWidget_update((XWidget*)self);
}

#endif /* XWIDGET_ON && XTABBAR_ON */

#endif /* XGUI_ON */
