/**
 * @file       XStatusBar.c
 * @brief      状态栏控件实现（对标 Qt 6.8 QStatusBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XStatusBar.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if XWIDGET_ON && XSTATUSBAR_ON

/* ==================== 内部条目与工具 ==================== */

typedef struct XStatusBarItem
{
    XWidget* widget;   /**< 承载控件（借用，归调用方）。 */
    int stretch;       /**< 拉伸因子（第一版仅存储）。 */
} XStatusBarItem;

static XStatusBarItem* xsb_itemCreate(XWidget* widget, int stretch)
{
    XStatusBarItem* item =
        (XStatusBarItem*)XMalloc_System(sizeof(XStatusBarItem));
    if (!item) return NULL;
    item->widget = widget;
    item->stretch = stretch;
    return item;
}

static void xsb_itemDestroy(XStatusBarItem* item)
{
    if (item) XFree_System(item);
}

/** @brief 从指定数组移除控件条目；返回是否移除。 */
static bool xsb_removeFrom(XVector* vec, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!vec || !widget) return false;
    n = XVector_size_base((const XContainer*)vec);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** item =
            (XStatusBarItem**)XVector_at_base(vec, i);
        if (item && *item && (*item)->widget == widget) {
            xsb_itemDestroy(*item);
            XVector_remove_base(vec, i, 1);
            return true;
        }
    }
    return false;
}

static void xsb_destroyVector(XVector* vec)
{
    int64_t i;
    int64_t n;
    if (!vec) return;
    n = XVector_size_base((const XContainer*)vec);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** item =
            (XStatusBarItem**)XVector_at_base(vec, i);
        xsb_itemDestroy(item ? *item : NULL);
    }
    XVector_delete_base(vec);
}

/** @brief 取调色板角色颜色（无调色板时回退黑/白）。 */
static uint32_t xsb_color(const XStatusBar* self, XPaletteColorRole role)
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

/* ==================== 事件处理 ==================== */

/** @brief paintEvent：顶部 1px 分隔线 + 消息文本（普通区被临时消息
 *         隐藏期间显示消息，永久区控件保持可见）。 */
static void VX_statusBar_paintEvent(XWidget* self, XEvent* event)
{
    XStatusBar* sb = (XStatusBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect line;
    XRect msgRect;
    uint32_t dark;
    uint32_t text;
    int w;
    int h;
    if (!sb || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 0 || h <= 0) return;
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
    dark = xsb_color(sb, XPaletteColorRole_Dark);
    text = xsb_color(sb, XPaletteColorRole_WindowText);
    XRect_init(&line, 0, 0, w, 1);
    XPainter_fillRect(&painter, &line, dark);
    if (sb->m_currentMessage[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XRect_init(&msgRect, 4, 1, w - 8, h - 2);
        XPainter_drawText(&painter, 4, h - 6, sb->m_currentMessage, text);
    }
    (void)msgRect;
    XPainter_deinit(&painter);
}

/** @brief 消息超时定时器：自动清除临时消息。 */
static void VX_statusBar_timerEvent(XObject* object, XTimerEvent* event)
{
    XStatusBar* sb = (XStatusBar*)object;
    if (!sb || !event) return;
    if (event->timerId == sb->m_messageTimer) {
        XObject_killTimer(object, sb->m_messageTimer);
        sb->m_messageTimer = XTIMER_INVALID_ID;
        XStatusBar_clearMessage(sb);
    }
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XStatusBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStatusBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_statusBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VX_statusBar_timerEvent);
    return XVTABLE_DEFAULT;
}

void XStatusBar_init(XStatusBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XStatusBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_items = XVector_Create(XStatusBarItem*);
    self->m_permanents = XVector_Create(XStatusBarItem*);
    self->m_sizeGripEnabled = true;
    self->m_messageTimer = XTIMER_INVALID_ID;
    /* 对标 QStatusBar 默认固定高度策略的简化：高度 24。 */
    XWidget_resize(self, 200, 24);
}

XStatusBar* XStatusBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XStatusBar* self = (XStatusBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XStatusBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 常驻控件 ==================== */

void XStatusBar_addWidget(XStatusBar* self, XWidget* widget, int stretch)
{
    XStatusBarItem* item;
    if (!self || !widget) return;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return;
    XVector_push_back_1_base(self->m_items, &item);
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, self->m_currentMessage[0] == '\0');
}

int XStatusBar_insertWidget(XStatusBar* self, int index, XWidget* widget,
                            int stretch)
{
    XStatusBarItem* item;
    int64_t n;
    if (!self || !widget) return -1;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    XVector_insert_1_base(self->m_items, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    return index;
}

void XStatusBar_addPermanentWidget(XStatusBar* self, XWidget* widget,
                                   int stretch)
{
    XStatusBarItem* item;
    if (!self || !widget) return;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return;
    XVector_push_back_1_base(self->m_permanents, &item);
    XWidget_setParent(widget, (XWidget*)self, 0);
}

int XStatusBar_insertPermanentWidget(XStatusBar* self, int index,
                                     XWidget* widget, int stretch)
{
    XStatusBarItem* item;
    int64_t n;
    if (!self || !widget) return -1;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_permanents);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    XVector_insert_1_base(self->m_permanents, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    return index;
}

void XStatusBar_removeWidget(XStatusBar* self, XWidget* widget)
{
    bool removed;
    if (!self) return;
    removed = xsb_removeFrom(self->m_items, widget);
    if (!removed)
        removed = xsb_removeFrom(self->m_permanents, widget);
    if (removed) {
        /* 对标 Qt：移除后控件归调用方所有，解除父子关系。 */
        XWidget_setParentPlain(widget, NULL);
    }
}

/* ==================== 尺寸手柄 ==================== */

bool XStatusBar_isSizeGripEnabled(const XStatusBar* self)
{
    return self ? self->m_sizeGripEnabled : false;
}

void XStatusBar_setSizeGripEnabled(XStatusBar* self, bool on)
{
    if (!self || self->m_sizeGripEnabled == on) return;
    self->m_sizeGripEnabled = on;
    XWidget_update((XWidget*)self);
}

/* ==================== 消息槽 ==================== */

void XStatusBar_showMessage(XStatusBar* self, const char* utf8, int timeout)
{
    const char* s;
    if (!self) return;
    s = (utf8 && utf8[0]) ? utf8 : NULL;
    if (!s) {
        XStatusBar_clearMessage(self);
        return;
    }
    strncpy(self->m_currentMessage, s, sizeof(self->m_currentMessage) - 1);
    self->m_currentMessage[sizeof(self->m_currentMessage) - 1] = '\0';
    self->m_tempTimeout = timeout;
    if (timeout > 0) {
        if (self->m_messageTimer != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)self, self->m_messageTimer);
        self->m_messageTimer = XObject_startTimer_ms(
            (XObject*)self, (unsigned)timeout, XTimerType_CoarseTimer);
    }
    XStatusBar_messageChanged_signal(self, self->m_currentMessage);
    XWidget_update((XWidget*)self);
}

void XStatusBar_clearMessage(XStatusBar* self)
{
    if (!self) return;
    if (self->m_messageTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_messageTimer);
        self->m_messageTimer = XTIMER_INVALID_ID;
    }
    if (self->m_currentMessage[0] == '\0') return;
    self->m_currentMessage[0] = '\0';
    XStatusBar_messageChanged_signal(self, "");
    XWidget_update((XWidget*)self);
}

const char* XStatusBar_currentMessage(const XStatusBar* self)
{
    return (self && self->m_currentMessage[0]) ? self->m_currentMessage : "";
}

/* ==================== 信号 ==================== */

void* XStatusBar_messageChanged_signal(XStatusBar* self, const char* text)
{
    XVarList* args;
    XString* value;
    (void)text;
    if (self && ((XObject*)self)->m_signalSlot) {
        value = XString_create_utf8(text ? text : "");
        args = value ? XVarList_Create(XVar(XString*, value)) : NULL;
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XStatusBar_messageChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        } else if (value) {
            XString_delete_base((XClass*)value);
        }
    }
    return (void*)(size_t)XStatusBar_messageChanged_signal;
}

#endif /* XWIDGET_ON && XSTATUSBAR_ON */
