/****************************************************************************
 * @file       XAccessible.c
 * @brief      XAccessible 可访问节点实现；无平台 API。
 ****************************************************************************/
#include "XAccessible.h"

#if XWINDOW_ON && XACCESSIBLE_ON
#include "XWindow.h"
#if XWIDGET_ON
#include "XWidget.h"
#endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
#include "XApplication.h"
#endif
#include "XGuiApplication.h"
#include "XCoreApplication.h"
#include <string.h>

static void VXAccessible_deinit(XAccessible* self)
{
    if (!self) return;
    if (self->m_name) XString_delete_base((XClass*)self->m_name);
    if (self->m_description) XString_delete_base((XClass*)self->m_description);
    self->m_name = NULL;
    self->m_description = NULL;
    self->m_window = NULL;
    self->m_widget = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XAccessible_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAccessible)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAccessible_deinit);
    return XVTABLE_DEFAULT;
}

XAccessible* XAccessible_createForWindow_ex(XMemoryType memory, XWindow* window)
{
    XAccessible* self;
    if (!window) return NULL;
    self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_window = window;
    self->m_role = XAccessibleRole_Window;
    return self;
}

XAccessible* XAccessible_createForWidget_ex(XMemoryType memory, XWidget* widget)
{
#if XWIDGET_ON
    XAccessible* self;
    if (!widget) return NULL;
    self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_widget = widget;
    self->m_role = XWidget_isWindow(widget) ? XAccessibleRole_Window :
                   XAccessibleRole_Client;
    return self;
#else
    (void)memory;
    (void)widget;
    return NULL;
#endif
}

XAccessible* XAccessible_createApplication_ex(XMemoryType memory)
{
    XAccessible* self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_role = XAccessibleRole_Application;
    self->m_applicationRoot = true;
    return self;
}

bool XAccessible_isValid(const XAccessible* self)
{ return self && (self->m_window || self->m_widget || self->m_applicationRoot); }
XAccessibleRole XAccessible_role(const XAccessible* self)
{ return XAccessible_isValid(self) ? self->m_role : XAccessibleRole_Unknown; }
XRect XAccessible_rect(const XAccessible* self)
{
    XRect rect = {0, 0, 0, 0};
    if (!XAccessible_isValid(self)) return rect;
#if XWIDGET_ON
    if (self->m_widget) {
        XPoint origin = {0, 0};
        origin = XWidget_mapToGlobal(self->m_widget, &origin);
        rect = XWidget_geometry(self->m_widget);
        rect.x = origin.x;
        rect.y = origin.y;
        return rect;
    }
#endif
    if (self->m_window) return XWindow_geometry(self->m_window);
    return rect;
}
bool XAccessible_isVisible(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return false;
    if (self->m_applicationRoot) return true;
#if XWIDGET_ON
    if (self->m_widget) return XWidget_isVisible(self->m_widget);
#endif
    return XWindow_isVisible(self->m_window);
}
XString* XAccessible_name(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return XString_create_utf8("");
    if (self->m_applicationRoot) {
        const XString* name = XCoreApplication_applicationName();
        return name ? XString_create_copy(name) : XString_create_utf8("XinYueC");
    }
    if (self->m_name) return XString_create_copy(self->m_name);
#if XWIDGET_ON
    if (self->m_widget) {
        const XString* title = XWidget_windowTitle(self->m_widget);
        if (title && XString_toUtf8_length(title) > 0)
            return XString_create_copy(title);
        title = XObject_objectName((const XObject*)self->m_widget);
        if (title) return XString_create_copy(title);
        title = XWidget_toolTip(self->m_widget);
        return title ? XString_create_copy(title) : XString_create_utf8("");
    }
#endif
    return XWindow_title(self->m_window);
}
void XAccessible_setName(XAccessible* self, const XString* name)
{
    if (!self) return;
    if (self->m_name) XString_delete_base((XClass*)self->m_name);
    self->m_name = name ? XString_create_copy(name) : NULL;
}
XString* XAccessible_description(const XAccessible* self)
{ return self && self->m_description ? XString_create_copy(self->m_description) : XString_create_utf8(""); }
void XAccessible_setDescription(XAccessible* self, const XString* description)
{
    if (!self) return;
    if (self->m_description) XString_delete_base((XClass*)self->m_description);
    self->m_description = description ? XString_create_copy(description) : NULL;
}
XWindow* XAccessible_window(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return NULL;
#if XWIDGET_ON
    return self->m_widget ? XWidget_nativeWindow(self->m_widget) : self->m_window;
#else
    return self->m_window;
#endif
}

XWidget* XAccessible_widget(const XAccessible* self)
{ return XAccessible_isValid(self) ? self->m_widget : NULL; }

XAccessible* XAccessible_parent(const XAccessible* self)
{
#if XWIDGET_ON
    XObject* parent;
    if (!XAccessible_isValid(self) || !self->m_widget) return NULL;
    parent = XObject_parent((XObject*)self->m_widget);
    if (!parent || !XObject_isWidgetType(parent)) return NULL;
    return ((XWidget*)parent)->m_accessible;
#else
    (void)self;
    return NULL;
#endif
}

#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
static bool XAccessible_isWidgetWindow(const XWindow* window)
{
    XVector* widgets;
    size_t i;
    if (!window) return false;
    widgets = XApplication_topLevelWidgets();
    if (!widgets) return false;
    for (i = 0; i < XVector_size_base((const XContainer*)widgets); ++i) {
        XWidget* widget = XVector_At_Base(widgets, (int64_t)i, XWidget*);
        if (widget && XWidget_nativeWindow(widget) == window) {
            XVector_delete_base((XClass*)widgets);
            return true;
        }
    }
    XVector_delete_base((XClass*)widgets);
    return false;
}
#else
static bool XAccessible_isWidgetWindow(const XWindow* window)
{ (void)window; return false; }
#endif

size_t XAccessible_childCount(const XAccessible* self)
{
    XVector* windows;
    XVector* widgets;
    size_t count = 0;
    size_t i;
    if (!self) return 0;
#if XWIDGET_ON
    if (self->m_widget) {
        const XVector* children = XObject_children((const XObject*)self->m_widget);
        if (!children) return 0;
        for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
            XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
            if (child && XObject_isWidgetType(child)) ++count;
        }
        return count;
    }
#endif
    if (!self->m_applicationRoot) return 0;
 #if XGUIAPPLICATION_ON
    windows = XGuiApplication_allWindows();
    if (windows) {
        for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
            XWindow* window = XVector_At_Base(windows, (int64_t)i, XWindow*);
            if (window && !XAccessible_isWidgetWindow(window)) ++count;
        }
        XVector_delete_base((XClass*)windows);
    }
 #else
    (void)windows;
 #endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    widgets = XApplication_topLevelWidgets();
    if (widgets) {
        count += XVector_size_base((const XContainer*)widgets);
        XVector_delete_base((XClass*)widgets);
    }
#else
    (void)widgets;
#endif
    return count;
}

XAccessible* XAccessible_childAtIndex(const XAccessible* self, size_t index)
{
    XVector* windows;
    XVector* widgets;
    XWindow* window;
    XAccessible* result = NULL;
    size_t i;
    if (!self) return NULL;
#if XWIDGET_ON
    if (self->m_widget) {
        const XVector* children = XObject_children((const XObject*)self->m_widget);
        size_t seen = 0;
        if (!children) return NULL;
        for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
            XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
            if (child && XObject_isWidgetType(child)) {
                if (seen++ == index) return ((XWidget*)child)->m_accessible;
            }
        }
        return NULL;
    }
#endif
    if (!self->m_applicationRoot) return NULL;
 #if XGUIAPPLICATION_ON
    windows = XGuiApplication_allWindows();
    if (windows) {
        for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
            window = XVector_At_Base(windows, (int64_t)i, XWindow*);
            if (window && !XAccessible_isWidgetWindow(window)) {
                if (index-- == 0) {
                    result = (XAccessible*)XWindow_accessibleRoot(window);
                    XVector_delete_base((XClass*)windows);
                    return result;
                }
            }
        }
        XVector_delete_base((XClass*)windows);
    }
 #else
    (void)windows;
 #endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    widgets = XApplication_topLevelWidgets();
    if (!widgets) return NULL;
    if (index < XVector_size_base((const XContainer*)widgets)) {
        XWidget* widget = XVector_At_Base(widgets, (int64_t)index, XWidget*);
        result = widget ? widget->m_accessible : NULL;
    }
    XVector_delete_base((XClass*)widgets);
#else
    (void)widgets;
#endif
    return result;
}

#endif /* XWINDOW_ON && XACCESSIBLE_ON */
