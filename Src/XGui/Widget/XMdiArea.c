/**
 * @file       XMdiArea.c
 * @brief      多文档接口区域及子窗口控件实现（对标 Qt 6.8 QMdiArea/QMdiSubWindow 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMdiArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif
#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/* ==================== XMdiSubWindow ==================== */

static void VX_mdiSubWindow_paintEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    uint32_t highlight;
    uint32_t windowText;
    int w;
    if (!sw || !event) return;
    w = XWidget_width(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
#endif /* XPALETTE_ON */
    XRect_init(&head, 0, 0, w, 20);
    XPainter_fillRect(&painter, &head, highlight);
    XPainter_drawText(&painter, 6, 14, sw->m_title, windowText);
    XPainter_deinit(&painter);
}

static void VX_mdiSubWindow_deinit(XMdiSubWindow* self)
{
    if (!self) return;
    if (self->m_widget) {
        XWidget_delete_base(self->m_widget);
        self->m_widget = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMdiSubWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiSubWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VX_mdiSubWindow_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiSubWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiSubWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize(self, 200, 150);
}

XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XMdiSubWindow* self =
        (XMdiSubWindow*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiSubWindow_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget)
{
    XRect r;
    int w;
    int h;
    if (!self) return;
    if (self->m_widget) return;
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self) > 20
        ? XWidget_height((XWidget*)self) - 20 : 0;
    XRect_init(&r, 0, 20, w, h);
    XWidget_setGeometryRect(widget, &r);
}

XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self)
{
    return self ? self->m_widget : NULL;
}

void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8)
{
    if (!self) return;
    strncpy(self->m_title, utf8 ? utf8 : "", sizeof(self->m_title) - 1);
    self->m_title[sizeof(self->m_title) - 1] = '\0';
    XWidget_update((XWidget*)self);
}

const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self)
{
    return self ? self->m_title : "";
}

/* ==================== XMdiArea ==================== */

static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window)
{
    XVarList* args = XVarList_Create(XVar(XMdiSubWindow*, window));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XMdiArea_subWindowActivated_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void VX_mdiArea_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    XMdiArea_tileSubWindows((XMdiArea*)self);
}

static void VX_mdiArea_deinit(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClass_delete_base((XClass*)*sw);
        }
        XVector_delete_base(self->m_subWindows);
        self->m_subWindows = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XMdiArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_mdiArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiArea_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_subWindows = XVector_Create(XMdiSubWindow*);
    self->m_active = NULL;
    self->m_viewMode = (int)XMdiAreaViewMode_SubWindowView;
}

XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XMdiArea* self = (XMdiArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget)
{
    XMdiSubWindow* sw;
    if (!self || !widget || !self->m_subWindows) return NULL;
    sw = XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                 (XWidget*)self, 0);
    if (!sw) return NULL;
    XMdiSubWindow_setWidget(sw, widget);
    XVector_push_back_1_base(self->m_subWindows, &sw);
    XWidget_show((XWidget*)sw);
    XMdiArea_setActiveSubWindow(self, sw);
    return sw;
}

void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (sw && *sw && XMdiSubWindow_widget(*sw) == widget) {
            if (self->m_active == *sw) self->m_active = NULL;
            XClass_delete_base((XClass*)*sw);
            XVector_remove_base(self->m_subWindows, i, 1);
            return;
        }
    }
}

const XVector* XMdiArea_subWindowList(const XMdiArea* self)
{
    return self ? self->m_subWindows : NULL;
}

XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self)
{
    return self ? self->m_active : NULL;
}

void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window)
{
    if (!self || !window) return;
    self->m_active = window;
    xmdi_emitActivated(self, window);
}

void XMdiArea_closeAllSubWindows(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClass_delete_base((XClass*)*sw);
        }
        XVector_clear_base(self->m_subWindows);
    }
    self->m_active = NULL;
}

void XMdiArea_cascadeSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int offset = 0;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        if (!sw || !*sw) continue;
        XRect_init(&r, offset, offset,
                   w > 60 ? w - 40 - offset : 200,
                   h > 60 ? h - 40 - offset : 150);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
        offset += 24;
    }
}

void XMdiArea_tileSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int count;
    int cols = 2;
    int rows;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    count = (int)n;
    if (count <= 0) return;
    rows = (count + cols - 1) / cols;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        if (!sw || !*sw) continue;
        XRect_init(&r, col * w / cols, row * h / rows,
                   w / cols, h / rows);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
    }
}

void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode)
{
    if (!self) return;
    self->m_viewMode = (int)mode;
}

XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self)
{
    return self ? (XMdiAreaViewMode)self->m_viewMode
                : XMdiAreaViewMode_SubWindowView;
}

int XMdiArea_subWindowCount(const XMdiArea* self)
{
    return (self && self->m_subWindows)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_subWindows)
               : 0;
}

/* ==================== 信号 ==================== */

void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window)
{
    (void)self; (void)window;
    return (void*)(size_t)XMdiArea_subWindowActivated_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */