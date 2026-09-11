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
#if XWIDGET_ON && XTABBAR_ON

#include "XTabBar.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <string.h>
#include <stdlib.h>

#define XTABBAR_TAB_W 88
#define XTABBAR_TAB_H 24

/* ==================== 前向声明 ==================== */
static void  VXTabBar_paintEvent(XWidget* self, XEvent* event);
static void  VXTabBar_mousePressEvent(XWidget* self, XEvent* event);
static void  VXTabBar_changeEvent(XWidget* self, XEvent* event);
static void  VXTabBar_copy(XTabBar* self, const XTabBar* other);
static void  VXTabBar_move(XTabBar* self, XTabBar* other);

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
    char** titles;
    bool* enabled;
    int cap = self->m_capacity > 0 ? self->m_capacity : 4;
    while (cap < need) cap *= 2;
    if (need <= self->m_capacity) return true;
    titles = (char**)XRealloc_System(self->m_titles, sizeof(char*) * (size_t)cap);
    if (!titles) return false;
    self->m_titles = titles;
    enabled = (bool*)XRealloc_System(self->m_enabled, sizeof(bool) * (size_t)cap);
    if (!enabled) return false;
    self->m_enabled = enabled;
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
    int minW = 48;
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

    image = XWidget_paintDevice(self);
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
        xtabbar_wrapLayout(bar, &cols, &rows, &tabW, &totalH);
        for (i = 0; i < bar->m_count; ++i) {
            row = i / cols;
            col = i % cols;
            {
                XRect tab = { col * tabW, row * XTABBAR_TAB_H,
                              tabW - 1, XTABBAR_TAB_H };
                bool isCur = (i == bar->m_currentIndex);
                XPainter_fillRect(&painter, &tab,
                                  isCur ? highlight : button);
                if (bar->m_titles[i])
                    XPainter_drawText(&painter, tab.x + 4, tab.y + 16,
                                      bar->m_titles[i],
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
    xtabbar_emitInt2(bar, (size_t)XTabBar_tabClicked_signal(bar), idx, 0);
    if (!bar->m_enabled[idx]) { XEvent_ignore(event); return; }
    if (idx != bar->m_currentIndex) {
        int old = bar->m_currentIndex;
        bar->m_currentIndex = idx;
        xtabbar_emitInt(bar, (size_t)XTabBar_currentChanged_signal(bar), idx);
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
        if (self->m_titles[i]) XFree_System(self->m_titles[i]);
    self->m_titles = other->m_titles;
    self->m_enabled = other->m_enabled;
    self->m_count = other->m_count;
    self->m_capacity = other->m_capacity;
    other->m_titles = NULL;
    other->m_enabled = NULL;
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

XVtable* XTabBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabBar)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTabBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXTabBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabBar_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTabBar_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTabBar_move);

    return XVTABLE_DEFAULT;
}

void XTabBar_init(XTabBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XTabBar);

    self->m_titles = NULL;
    self->m_enabled = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_currentIndex = -1;
    self->m_tabsClosable = false;
    self->m_movable = false;
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

int XTabBar_addTab(XTabBar* self, const char* text)
{
    return XTabBar_insertTab(self, self ? self->m_count : 0, text);
}

int XTabBar_insertTab(XTabBar* self, int index, const char* text)
{
    size_t len;
    if (!self || !text) return -1;
    if (index < 0) index = 0;
    if (index > self->m_count) index = self->m_count;
    if (!xtabbar_ensureCapacity(self, self->m_count + 1)) return -1;
    len = strlen(text) + 1;
    {
        char* copy = (char*)XMalloc_System(len);
        if (!copy) return -1;
        memcpy(copy, text, len);
        memmove(&self->m_titles[index + 1], &self->m_titles[index],
                sizeof(char*) * (size_t)(self->m_count - index));
        memmove(&self->m_enabled[index + 1], &self->m_enabled[index],
                sizeof(bool) * (size_t)(self->m_count - index));
        self->m_titles[index] = copy;
        self->m_enabled[index] = true;
        ++self->m_count;
    }
    if (self->m_currentIndex < 0) self->m_currentIndex = index;
    XWidget_update((XWidget*)self);
    return index;
}

void XTabBar_removeTab(XTabBar* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (self->m_titles[index]) XFree_System(self->m_titles[index]);
    memmove(&self->m_titles[index], &self->m_titles[index + 1],
            sizeof(char*) * (size_t)(self->m_count - index - 1));
    memmove(&self->m_enabled[index], &self->m_enabled[index + 1],
            sizeof(bool) * (size_t)(self->m_count - index - 1));
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
    xtabbar_emitInt(self, (size_t)XTabBar_currentChanged_signal(self), index);
    XWidget_update((XWidget*)self);
}

const char* XTabBar_tabText(const XTabBar* self, int index)
{
    if (self && index >= 0 && index < self->m_count && self->m_titles[index])
        return self->m_titles[index];
    return "";
}

void XTabBar_setTabText(XTabBar* self, int index, const char* text)
{
    size_t len;
    if (!self || !text || index < 0 || index >= self->m_count) return;
    len = strlen(text) + 1;
    if (self->m_titles[index]) XFree_System(self->m_titles[index]);
    self->m_titles[index] = (char*)XMalloc_System(len);
    if (!self->m_titles[index]) return;
    memcpy(self->m_titles[index], text, len);
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

void* XTabBar_currentChanged_signal(XTabBar* self)
{
    return (void*)(size_t)XTabBar_currentChanged_signal;
}
void* XTabBar_tabClicked_signal(XTabBar* self)
{
    return (void*)(size_t)XTabBar_tabClicked_signal;
}
void* XTabBar_tabCloseRequested_signal(XTabBar* self)
{
    return (void*)(size_t)XTabBar_tabCloseRequested_signal;
}


void* XTabBar_tabBarClicked_signal(XTabBar* self)
{
    (void)self;
    return (void*)(size_t)XTabBar_tabBarClicked_signal;
}
void* XTabBar_tabBarDoubleClicked_signal(XTabBar* self)
{
    (void)self;
    return (void*)(size_t)XTabBar_tabBarDoubleClicked_signal;
}
void* XTabBar_tabMoved_signal(XTabBar* self)
{
    (void)self;
    return (void*)(size_t)XTabBar_tabMoved_signal;
}

#endif /* XWIDGET_ON && XTABBAR_ON */
