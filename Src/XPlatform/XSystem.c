/**
 * @file XSystem.c
 * @brief 系统复位、关机和有序重启回调的默认分发实现。
 * @details
 * 本文件只保存产品覆盖回调并完成参数校验，不包含任何平台头文件。没有覆盖
 * 回调且 XPLATFORM_HAS_OS 为 1 时转发给 Drive 平台后端；无操作系统时直接
 * 返回 NotSupported。测试可注册模拟回调，避免触发真实系统操作。
 */

#include "CXinYueConfig.h"
#include "XSystem_Protected.h"

static XSystemResetHandler g_resetHandler;
static void* g_resetUserData;
static XSystemRebootHandler g_rebootHandler;
static void* g_rebootUserData;
static XSystemShutdownHandler g_shutdownHandler;
static void* g_shutdownUserData;

void XSystem_setResetHandler(XSystemResetHandler handler, void* userData)
{
    g_resetHandler = handler;
    g_resetUserData = handler ? userData : NULL;
}

void XSystem_setRebootHandler(XSystemRebootHandler handler, void* userData)
{
    g_rebootHandler = handler;
    g_rebootUserData = handler ? userData : NULL;
}

void XSystem_setShutdownHandler(XSystemShutdownHandler handler, void* userData)
{
    g_shutdownHandler = handler;
    g_shutdownUserData = handler ? userData : NULL;
}

XSystemResult XSystem_reset(XSystemResetReason reason)
{
    if (reason < XSystemResetReason_Shell || reason > XSystemResetReason_Firmware)
        return XSystemResult_InvalidArgument;
    if (g_resetHandler) return g_resetHandler(g_resetUserData, reason);
#if XPLATFORM_HAS_OS
    return XSystem_platformReset(reason);
#else
    return XSystemResult_NotSupported;
#endif
}

XSystemResult XSystem_reboot(XSystemRebootMode mode)
{
    if (mode < XSystemRebootMode_Normal || mode > XSystemRebootMode_Bootloader)
        return XSystemResult_InvalidArgument;
    if (g_rebootHandler) return g_rebootHandler(g_rebootUserData, mode);
#if XPLATFORM_HAS_OS
    return XSystem_platformReboot(mode);
#else
    return XSystemResult_NotSupported;
#endif
}

XSystemResult XSystem_shutdown(void)
{
    if (g_shutdownHandler) return g_shutdownHandler(g_shutdownUserData);
#if XPLATFORM_HAS_OS
    return XSystem_platformShutdown();
#else
    return XSystemResult_NotSupported;
#endif
}
