/**
 * @file       XPlatformAccessibility_unsupported.c
 * @brief      无桌面辅助功能服务时的平台桥接安全存根。
 */
#include "XPlatformAccessibility.h"

/* [安卓无障碍接入 2026-10-24] 安卓有**真实后端**
   （Drive/Android/Graphics/XPlatformAccessibility_android.c 的 TalkBack
   虚拟视图树），本存根必须在安卓排除——否则两者在同一静态归档里定义同名
   Driver_* 符号：静态库链接按归档顺序取先到者，谁生效取决于链接顺序
   （不可控），且"链接期不报错、运行期行为漂移"极难查。
   守卫原先只排除了 Linux-DBUS 与 Windows-UIA 两种桌面后端，安卓不匹配
   任一分支 → 存根照常编译 → 与安卓后端撞符号（归档内实测
   XPlatformAccessibilityDriver_start 双份）。 */
#if XWINDOW_ON && XACCESSIBLE_ON && !defined(__ANDROID__) && \
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
