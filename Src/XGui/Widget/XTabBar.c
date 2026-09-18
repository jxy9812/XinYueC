/**
 * @file       XTabBar.c
 * @brief      XTabBar 选项卡条实现（对标 Qt 6.8 QTabBar 子集）。
 * @details    水平绘制：每项固定 88x24，当前项 Highlight 底 +
 *             HighlightedText，其余 Button 底 + WindowText；左键点击
 *             项切换当前并发射 currentChanged/tabClicked；禁用项不
 *             响应点击。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XTABBAR_ON

#include "XTabBar.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */

#define XTABBAR_TAB_W 88
#define XTABBAR_TAB_H 24

/* ==================== 前向声明 ==================== */
static void  VXTabBar_paintEvent(XWidget* self, XEvent* event);
static void  VXTabBar_mousePressEvent(XWidget* self, XEvent* event);
static void  VXTabBar_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void  VXTabBar_changeEvent(XWidget* self, XEvent* event);
static void  VXTabBar_copy(XTabBar* self, const XTabBar* other);
static void  VXTabBar_move(XTabBar* self, XTabBar* other);
static int   xtabbar_tabAt(const XTabBar* self, const XPoint* pos);
static void  xtabbar_emitInt(XTabBar* self, size_t signal, int value);

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
    for (i = self->m_capacity; i < cap; ++i) {
        self->m_tabTextColors[i] = 0;
        self->m_tabVisible[i] = true;
        self->m_tabToolTips[i] = NULL;
        self->m_tabWhatsThis[i] = NULL;
        self->m_tabAccessibleNames[i] = NULL;
        self->m_tabIcons[i] = NULL;
        self->m_tabData[i] = NULL;
        self->m_tabButtons[i] = NULL;
    }
    self->m_capacity = cap;
    return true;
}

/** @brief 计算多行换行布局：列数/行数/标签宽度/总高度。
 *  @details 当标签总宽超出可用宽度时自动换行，对标 QTabBar 多行模式。
 *          列数 = barW / minTabW（每标签最小 48px 保可读），
 *          行数 = ceil(count / cols)，标签宽度 = barW / cols。 */
static void xtabbar_wrapLayout(const XTabBar* self,
                               int* outCols, int* outRows,
                               int* outTabW, int* outTotalH)
{
    int barW = XWidget_width((XWidget*)self);
    int minW = 72;
    int cols;
    int rows;
    int tabW;
    if (barW < minW) barW = minW;
    cols = barW / minW;
    if (cols < 1) cols = 1;
    if (cols > self->m_count) cols = self->m_count;
    if (cols < 1) cols = 1; /* m_count=0 时防除零。 */
    rows = (self->m_count + cols - 1) / cols;
    if (rows < 1) rows = 1;
    tabW = barW / cols;
    if (tabW > XTABBAR_TAB_W) tabW = XTABBAR_TAB_W;
    *outCols = cols;
    *outRows = rows;
    *outTabW = tabW;
    *outTotalH = rows * XTABBAR_TAB_H;
}

/** @brief 点击坐标 → 项索引（-1 = 无）。 */
static int xtabbar_tabAt(const XTabBar* self, const XPoint* pos)
{
    int cols, rows, tabW, totalH;
    int row, col, idx;
    if (!pos) return -1;
    xtabbar_wrapLayout(self, &cols, &rows, &tabW, &totalH);
    if (pos->y < 0 || pos->y >= totalH) return -1;
    row = pos->y / XTABBAR_TAB_H;
    col = pos->x / tabW;
    idx = row * cols + col;
    if (idx < 0 || idx >= self->m_count) return -1;
    return idx;
}

/* ==================== 虚槽实现 ==================== */

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
    int i;
    if (!bar || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    base     = xtabbar_color(bar, XPaletteColorRole_Base);
    dark     = xtabbar_color(bar, XPaletteColorRole_Dark);
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
        int cols, rows, tabW, totalH;
        int row, col;
        XStyle* style = NULL;
#if XSTYLE_ON
        style = XStyle_defaultStyle();
#endif
        xtabbar_wrapLayout(bar, &cols, &rows, &tabW, &totalH);
        for (i = 0; i < bar->m_count; ++i) {
            row = i / cols;
            col = i % cols;
            {
                XRect tab = { col * tabW, row * XTABBAR_TAB_H,
                              tabW - 1, XTABBAR_TAB_H };
                bool isCur = (i == bar->m_currentIndex);
#if XSTYLE_ON
                if (style != NULL) {
                    /* Fusion/公共风格接管：页签形状 + 标签由样式引擎绘制。 */
                    XStyleOption opt;
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
                    opt.m_text = bar->m_titles[i] ? XString_toUtf8(bar->m_titles[i]) : "";
#if XPALETTE_ON
                    opt.m_palette = XWidget_palette(self);
#endif
                    XStyle_drawControl(style, XStyleCE_TabBarTabShape,
                                       &opt, &painter, self);
                    opt.m_type = XStyleCE_TabBarTabLabel;
                    XStyle_drawControl(style, XStyleCE_TabBarTabLabel,
                                       &opt, &painter, self);
                    XPainter_setPen(&painter, dark);
                    XPainter_drawLine(&painter, tab.x,
                                      tab.y + XTABBAR_TAB_H,
                                      tab.x + tab.width,
                                      tab.y + XTABBAR_TAB_H);
                    continue;
                }
#endif /* XSTYLE_ON */
                XPainter_fillRect(&painter, &tab,
                                  isCur ? highlight : button);
                if (bar->m_titles[i])
                    XPainter_drawText(&painter, tab.x + 4, tab.y + 16,
                                      XString_toUtf8(bar->m_titles[i]),
                                      isCur ? highlightedText
                                            : (bar->m_enabled[i] ? windowText
                                                                 : disabled));
                XPainter_setPen(&painter, dark);
                XPainter_drawLine(&painter, tab.x,
                                  tab.y + XTABBAR_TAB_H,
                                  tab.x + tab.width,
                                  tab.y + XTABBAR_TAB_H);
            }
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
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
    idx = xtabbar_tabAt(bar, &pos);
    if (idx < 0) { XEvent_ignore(event); return; }
    xtabbar_emitInt(bar, (size_t)XTabBar_tabBarClicked_signal(bar, idx), idx);
    if (!bar->m_enabled[idx]) { XEvent_ignore(event); return; }
    if (idx != bar->m_currentIndex) {
        int old = bar->m_currentIndex;
        bar->m_currentIndex = idx;
        xtabbar_emitInt(bar, (size_t)XTabBar_currentChanged_signal(bar, idx), idx);
        (void)old;
    }
    XWidget_update(self);
    XEvent_accept(event);
}

static void VXTabBar_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
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
}

static void VXTabBar_move(XTabBar* self, XTabBar* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    for (i = 0; i < self->m_count; ++i)
        if (self->m_titles[i]) XString_delete_base(self->m_titles[i]);
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
    other->m_count = 0;
    other->m_capacity = 0;
    self->m_currentIndex = other->m_currentIndex;
    other->m_currentIndex = -1;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    other->m_tabsClosable = false;
    other->m_movable = false;
}

/* ==================== 生命周期 ==================== */

/** @brief 反初始化：释放全部页签标题与数组。 */
static void VXTabBar_deinit(XTabBar* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_titles[i]) XString_delete_base(self->m_titles[i]);
        self->m_titles[i] = NULL;
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
        for (i = 0; i < self->m_count; ++i)
            if (self->m_tabToolTips[i])
                XString_delete_base(self->m_tabToolTips[i]);
        XFree_System(self->m_tabToolTips);
        self->m_tabToolTips = NULL;
    }
    if (self->m_tabWhatsThis) {
        for (i = 0; i < self->m_count; ++i)
            if (self->m_tabWhatsThis[i])
                XString_delete_base(self->m_tabWhatsThis[i]);
        XFree_System(self->m_tabWhatsThis);
        self->m_tabWhatsThis = NULL;
    }
    if (self->m_tabAccessibleNames) {
        for (i = 0; i < self->m_count; ++i)
            if (self->m_tabAccessibleNames[i])
                XString_delete_base(self->m_tabAccessibleNames[i]);
        XFree_System(self->m_tabAccessibleNames);
        self->m_tabAccessibleNames = NULL;
    }
    if (self->m_tabIcons) {
        for (i = 0; i < self->m_count; ++i)
            if (self->m_tabIcons[i]) XString_delete_base(self->m_tabIcons[i]);
        XFree_System(self->m_tabIcons);
        self->m_tabIcons = NULL;
    }
    if (self->m_tabData) {
        for (i = 0; i < self->m_count; ++i)
            if (self->m_tabData[i]) XString_delete_base(self->m_tabData[i]);
        XFree_System(self->m_tabData);
        self->m_tabData = NULL;
    }
    if (self->m_tabButtons) {
        XFree_System(self->m_tabButtons);
        self->m_tabButtons = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XTabBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabBar)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTabBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXTabBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VXTabBar_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabBar_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTabBar_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTabBar_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTabBar_move);

    return XVTABLE_DEFAULT;
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
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_currentIndex = -1;
    self->m_tabsClosable = false;
    self->m_movable = false;
    self->m_autoHide = false;
    self->m_expanding = true;
    self->m_elideMode = 1; /* Qt::ElideRight */
    self->m_selectionBehavior = 0;
    self->m_shape = 0; /* Qt::RoundedShape */
    self->m_iconSize = 0; /* 0 = 默认尺寸 */
    self->m_changeCurrentOnDrag = false;
    self->m_usesScrollButtons = false;
    self->m_documentMode = false;
    self->m_drawBase = true;
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
    if (index < 0) index = 0;
    if (index > self->m_count) index = self->m_count;
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
    ++self->m_count;
    if (self->m_currentIndex < 0) self->m_currentIndex = index;
    XWidget_update((XWidget*)self);
    return index;
}
int XTabBar_insertTab_2(XTabBar* self, int index, const char* text)
{
    XString_Init_Utf8(tmp, text ? text : "");
    index = XTabBar_insertTab(self, index, tmp);
    XString_deinit_base(tmp);
    return index;
}

void XTabBar_removeTab(XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (self->m_titles[index]) XString_delete_base(self->m_titles[index]);
    if (self->m_tabToolTips && self->m_tabToolTips[index]) {
        XString_delete_base(self->m_tabToolTips[index]);
        self->m_tabToolTips[index] = NULL;
    }
    if (self->m_tabWhatsThis && self->m_tabWhatsThis[index]) {
        XString_delete_base(self->m_tabWhatsThis[index]);
        self->m_tabWhatsThis[index] = NULL;
    }
    if (self->m_tabAccessibleNames && self->m_tabAccessibleNames[index]) {
        XString_delete_base(self->m_tabAccessibleNames[index]);
        self->m_tabAccessibleNames[index] = NULL;
    }
    if (self->m_tabIcons && self->m_tabIcons[index]) {
        XString_delete_base(self->m_tabIcons[index]);
        self->m_tabIcons[index] = NULL;
    }
    if (self->m_tabData && self->m_tabData[index]) {
        XString_delete_base(self->m_tabData[index]);
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
    --self->m_count;
    if (self->m_currentIndex >= self->m_count)
        self->m_currentIndex = self->m_count - 1;
    XWidget_update((XWidget*)self);
}

int XTabBar_count(const XTabBar* self) { return self ? self->m_count : 0; }
int XTabBar_currentIndex(const XTabBar* self)
{
    return self ? self->m_currentIndex : -1;
}

void XTabBar_setCurrentIndex(XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (index == self->m_currentIndex) return;
    self->m_currentIndex = index;
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
    XWidget_update((XWidget*)self);
}
void XTabBar_setTabText_2(XTabBar* self, int index, const char* text)
{
    if (!self || !text || index < 0 || index >= self->m_count) return;
    XString_assign_utf8(self->m_titles[index], text);
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
    if (self) self->m_tabsClosable = closable;
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

void XTabBar_setDocumentMode(XTabBar* self, bool enable)
{ if (self) self->m_documentMode = enable; }
bool XTabBar_documentMode(const XTabBar* self)
{ return self ? self->m_documentMode : false; }

void XTabBar_setElideMode(XTabBar* self, int mode)
{ if (self) self->m_elideMode = mode; }
int XTabBar_elideMode(const XTabBar* self)
{ return self ? self->m_elideMode : 0; }

void XTabBar_setExpanding(XTabBar* self, bool enable)
{ if (self) self->m_expanding = enable; }
bool XTabBar_expanding(const XTabBar* self)
{ return self ? self->m_expanding : true; }

void XTabBar_setUsesScrollButtons(XTabBar* self, bool enable)
{ if (self) self->m_usesScrollButtons = enable; }
bool XTabBar_usesScrollButtons(const XTabBar* self)
{ return self ? self->m_usesScrollButtons : false; }

void XTabBar_setDrawBase(XTabBar* self, bool enable)
{ if (self) self->m_drawBase = enable; }
bool XTabBar_drawBase(const XTabBar* self)
{ return self ? self->m_drawBase : true; }

void XTabBar_setShape(XTabBar* self, int shape)
{ if (self) self->m_shape = shape; }
int XTabBar_shape(const XTabBar* self)
{ return self ? self->m_shape : 0; }

void XTabBar_setIconSize(XTabBar* self, int size)
{
    if (!self || size < 0 || size == self->m_iconSize) return;
    self->m_iconSize = size;
    XWidget_update((XWidget*)self);
}
int XTabBar_iconSize(const XTabBar* self)
{ return self ? self->m_iconSize : 0; }

void XTabBar_setAutoHide(XTabBar* self, bool enable)
{ if (self) self->m_autoHide = enable; }
bool XTabBar_autoHide(const XTabBar* self)
{ return self ? self->m_autoHide : false; }

void XTabBar_setSelectionBehaviorOnRemove(XTabBar* self, int behavior)
{ if (self) self->m_selectionBehavior = behavior; }
int XTabBar_selectionBehaviorOnRemove(const XTabBar* self)
{ return self ? self->m_selectionBehavior : 0; }

void XTabBar_setChangeCurrentOnDrag(XTabBar* self, bool enable)
{ if (self) self->m_changeCurrentOnDrag = enable; }
bool XTabBar_changeCurrentOnDrag(const XTabBar* self)
{ return self ? self->m_changeCurrentOnDrag : false; }

bool XTabBar_tabRect(const XTabBar* self, int index, XRect* out)
{
    int cols, rows, tabW, totalH;
    int row, col;
    if (!self || !out || index < 0 || index >= self->m_count) return false;
    xtabbar_wrapLayout(self, &cols, &rows, &tabW, &totalH);
    row = index / cols;
    col = index % cols;
    XRect_init(out, col * tabW, row * XTABBAR_TAB_H, tabW, XTABBAR_TAB_H);
    return true;
}

int XTabBar_tabAt(const XTabBar* self, const XPoint* pos)
{
    return xtabbar_tabAt(self, pos);
}

int XTabBar_tabWidth(const XTabBar* self)
{
    int cols, rows, tabW, totalH;
    if (!self) return 0;
    xtabbar_wrapLayout(self, &cols, &rows, &tabW, &totalH);
    return tabW;
}

int XTabBar_tabHeight(const XTabBar* self)
{
    (void)self;
    return XTABBAR_TAB_H;
}

int XTabBar_tabIndexAt(const XTabBar* self, int x, int y)
{
    XPoint pos;
    XPoint_init(&pos, x, y);
    return XTabBar_tabAt(self, &pos);
}

bool XTabBar_isEmpty(const XTabBar* self)
{ return self ? (self->m_count == 0) : true; }

void XTabBar_setTabIcon(XTabBar* self, int index, const XString* path)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabIcons)
        return;
    repl = path ? XString_create_copy(path) : NULL;
    if (path && !repl) return;
    if (self->m_tabIcons[index]) XString_delete_base(self->m_tabIcons[index]);
    self->m_tabIcons[index] = repl;
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
    if (tmp) XString_delete_base(tmp);
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

void XTabBar_setTabToolTip(XTabBar* self, int index, const XString* tip)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabToolTips)
        return;
    repl = tip ? XString_create_copy(tip) : NULL;
    if (tip && !repl) return;
    if (self->m_tabToolTips[index])
        XString_delete_base(self->m_tabToolTips[index]);
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
    if (tmp) XString_delete_base(tmp);
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
        XString_delete_base(self->m_tabWhatsThis[index]);
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
    if (tmp) XString_delete_base(tmp);
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
        XString_delete_base(self->m_tabAccessibleNames[index]);
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
    if (tmp) XString_delete_base(tmp);
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

void XTabBar_setTabButton(XTabBar* self, int index,
                          XAbstractButton* button)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabButtons)
        return;
    self->m_tabButtons[index] = button;
    XWidget_update((XWidget*)self);
}

XAbstractButton* XTabBar_tabButton(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabButtons)
        return NULL;
    return self->m_tabButtons[index];
}

void XTabBar_setTabData(XTabBar* self, int index, const XString* data)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_count || !self->m_tabData)
        return;
    repl = data ? XString_create_copy(data) : NULL;
    if (data && !repl) return;
    if (self->m_tabData[index]) XString_delete_base(self->m_tabData[index]);
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
    if (tmp) XString_delete_base(tmp);
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

void XTabBar_setTabVisible(XTabBar* self, int index, bool visible)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabVisible)
        return;
    self->m_tabVisible[index] = visible;
    XWidget_update((XWidget*)self);
}

bool XTabBar_isTabVisible(const XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_tabVisible)
        return true;
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
    XString* whatsThis;
    XString* accessibleName;
    int i;
    int step;
    if (!self || from < 0 || from >= self->m_count || to < 0 ||
        to >= self->m_count || from == to)
        return;
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
    if (self->m_currentIndex == from) self->m_currentIndex = to;
    else if (self->m_currentIndex > from && self->m_currentIndex <= to)
        self->m_currentIndex--;
    else if (self->m_currentIndex < from && self->m_currentIndex >= to)
        self->m_currentIndex++;
    xtabbar_emitInt2(self, (size_t)XTabBar_tabMoved_signal(self, from, to),
                     from, to);
    XWidget_update((XWidget*)self);
}

#endif /* XWIDGET_ON && XTABBAR_ON */
