/**
 * @file       XPlatformAccessibility_unsupported.c
 * @brief      无桌面辅助功能服务时的平台桥接安全存根。
 */
#include "XPlatformAccessibility.h"

#if XWINDOW_ON && XACCESSIBLE_ON && \
    !((defined(__linux__) && defined(XINYUE_C_HAS_DBUS) && \
       XPLATFORMACCESSIBILITY_ATSPI_ON) || \
      (defined(_WIN32) && XPLATFORMACCESSIBILITY_UIA_ON))

bool XPlatformAccessibilityDriver_start(XPlatformAccessibility* bridge,
                                        void** nativeState)
{
    (void)bridge;
    if (nativeState) *nativeState = NULL;
    return false;
}

void XPlatformAccessibilityDriver_stop(void* nativeState)
{ (void)nativeState; }

bool XPlatformAccessibilityDriver_isActive(void* nativeState)
{ (void)nativeState; return false; }

void XPlatformAccessibilityDriver_notify(void* nativeState,
                                         XAccessibleEvent event,
                                         XAccessible* accessible)
{ (void)nativeState; (void)event; (void)accessible; }

void XPlatformAccessibilityDriver_processEvents(void* nativeState)
{ (void)nativeState; }

#endif
