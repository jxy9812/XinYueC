#include "XDockWidget.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XPainter.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XDOCKWIDGET_ON

/* ==================== 内部工具 ==================== */

static void xdw_emitInt(XDockWidget* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdw_emitBool(XDockWidget* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_dockWidget_paintEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t windowText;
    int w;
    if (!dock || !event) return;
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
    XPainter_drawText(&painter, 6, 14, dock->m_title, windowText);
    XRect_init(&line, 0, 20, w, 1);
    XPainter_fillRect(&painter, &line, windowText);
    XPainter_deinit(&painter);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_dockWidget_deinit(XDockWidget* self)
{
    if (!self) return;
    if (self->m_widget) {
        XWidget_delete_base(self->m_widget);
        self->m_widget = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XDockWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDockWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_dockWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_dockWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDockWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    strncpy(self->m_title, utf8Title ? utf8Title : "",
            sizeof(self->m_title) - 1);
    self->m_features = 0x1 | 0x2 | 0x4; /* Closable|Movable|Floatable */
    self->m_allowedAreas = (int)XDockWidgetArea_All;
    self->m_floating = false;
}

XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags)
{
    XDockWidget* self =
        (XDockWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDockWidget_init(self, utf8Title, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XDockWidget_setWidget(XDockWidget* self, XWidget* widget)
{
    XRect r;
    if (!self) return;
    if (self->m_widget) return;
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    XRect_init(&r, 0, 21,
               XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self) > 21
                   ? XWidget_height((XWidget*)self) - 21 : 0);
    XWidget_setGeometryRect(widget, &r);
}

XWidget* XDockWidget_widget(const XDockWidget* self)
{
    return self ? self->m_widget : NULL;
}

void XDockWidget_setFeatures(XDockWidget* self, int features)
{
    if (!self || self->m_features == features) return;
    self->m_features = features;
    xdw_emitInt(self, (size_t)XDockWidget_featuresChanged_signal,
                features);
}

int XDockWidget_features(const XDockWidget* self)
{
    return self ? self->m_features : 0;
}

void XDockWidget_setFloating(XDockWidget* self, bool floating)
{
    if (!self || self->m_floating == floating) return;
    self->m_floating = floating;
    xdw_emitBool(self, (size_t)XDockWidget_topLevelChanged_signal,
                 floating);
}

bool XDockWidget_isFloating(const XDockWidget* self)
{
    return self ? self->m_floating : false;
}

void XDockWidget_setAllowedAreas(XDockWidget* self, int areas)
{
    if (!self || self->m_allowedAreas == areas) return;
    self->m_allowedAreas = areas;
    xdw_emitInt(self, (size_t)XDockWidget_allowedAreasChanged_signal,
                areas);
}

int XDockWidget_allowedAreas(const XDockWidget* self)
{
    return self ? self->m_allowedAreas : 0;
}

void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    if (!self) return;
    self->m_titleBar = widget;
}

XWidget* XDockWidget_titleBarWidget(const XDockWidget* self)
{
    return self ? self->m_titleBar : NULL;
}

XAction* XDockWidget_toggleViewAction(const XDockWidget* self)
{
    /* 对标 toggleViewAction()：返回显示/隐藏切换动作；第一版返回 NULL
     * 占位（动作需持久归 dock 所有，待动作管理补齐）。 */
    (void)self;
    return NULL;
}

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features)
{
    (void)self; (void)features;
    return (void*)(size_t)XDockWidget_featuresChanged_signal;
}

void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel)
{
    (void)self; (void)topLevel;
    return (void*)(size_t)XDockWidget_topLevelChanged_signal;
}

void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas)
{
    (void)self; (void)areas;
    return (void*)(size_t)XDockWidget_allowedAreasChanged_signal;
}

void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible)
{
    (void)self; (void)visible;
    return (void*)(size_t)XDockWidget_visibilityChanged_signal;
}

#endif /* XWIDGET_ON && XDOCKWIDGET_ON */