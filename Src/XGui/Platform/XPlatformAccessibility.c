/** @file XPlatformAccessibility.c @brief 公共辅助功能桥接实现，无平台 API。 */
#include "XPlatformAccessibility.h"

#if XWINDOW_ON && XACCESSIBLE_ON
#include "XWindow.h"
#include "XWidget.h"
#include <string.h>

static XPlatformAccessibility* g_platformAccessibility;
static unsigned int g_platformAccessibilityNotifyDepth;

static void VXPlatformAccessibility_deinit(XPlatformAccessibility* self)
{
    if (!self) return;
    if (g_platformAccessibility == self) g_platformAccessibility = NULL;
    if (self->m_nativeState) {
        XPlatformAccessibilityDriver_stop(self->m_nativeState);
        self->m_nativeState = NULL;
    }
    self->m_active = false;
    if (self->m_root) XAccessible_delete_base(self->m_root);
    self->m_root = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XPlatformAccessibility_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlatformAccessibility)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlatformAccessibility_deinit);
    return XVTABLE_DEFAULT;
}

XPlatformAccessibility* XPlatformAccessibility_create_ex(XMemoryType memory)
{
    XPlatformAccessibility* self = (XPlatformAccessibility*)XMemory_malloc(
        sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPlatformAccessibility);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_root = XAccessible_createApplication_ex(memory);
    self->m_active = XPlatformAccessibilityDriver_start(self,
                                                         &self->m_nativeState);
    g_platformAccessibility = self;
    return self;
}

XAccessible* XPlatformAccessibility_root(const XPlatformAccessibility* self)
{ return self ? self->m_root : NULL; }

bool XPlatformAccessibility_isActive(const XPlatformAccessibility* self)
{ return self && self->m_active &&
         XPlatformAccessibilityDriver_isActive(self->m_nativeState); }

void XPlatformAccessibility_notify(XPlatformAccessibility* self,
                                   XAccessibleEvent event,
                                   XAccessible* accessible)
{
    if (!self || !accessible) return;
    XPlatformAccessibilityDriver_notify(self->m_nativeState, event, accessible);
    self->m_active = XPlatformAccessibilityDriver_isActive(self->m_nativeState);
}

void XPlatformAccessibility_processEvents(XPlatformAccessibility* self)
{
    if (!self) return;
    XPlatformAccessibilityDriver_processEvents(self->m_nativeState);
    self->m_active = XPlatformAccessibilityDriver_isActive(self->m_nativeState);
}

void XPlatformAccessibility_notifyWindow(XAccessibleEvent event, XWindow* window)
{
    XAccessible* accessible;
    if (!g_platformAccessibility || !window ||
        g_platformAccessibilityNotifyDepth != 0) return;
    accessible = (XAccessible*)XWindow_accessibleRoot(window);
    if (accessible) {
        ++g_platformAccessibilityNotifyDepth;
        XPlatformAccessibility_notify(g_platformAccessibility, event, accessible);
        --g_platformAccessibilityNotifyDepth;
    }
}

void XPlatformAccessibility_notifyWidget(XAccessibleEvent event, XWidget* widget)
{
#if XWIDGET_ON
    XAccessible* accessible;
    if (!g_platformAccessibility || !widget ||
        g_platformAccessibilityNotifyDepth != 0) return;
    accessible = widget->m_accessible;
    if (accessible) {
        ++g_platformAccessibilityNotifyDepth;
        XPlatformAccessibility_notify(g_platformAccessibility, event, accessible);
        --g_platformAccessibilityNotifyDepth;
    }
#else
    (void)event;
    (void)widget;
#endif
}

#endif /* XWINDOW_ON && XACCESSIBLE_ON */
