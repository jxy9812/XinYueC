/**
 * @file XSystem_unsupported.c
 * @brief 未提供系统复位、关机后端的平台存根。
 * @details
 * FreeRTOS、裸机和尚未接入的平台通过本文件保持 XSystem 可链接。默认返回
 * NotSupported；产品应使用 XSystem_setResetHandler、XSystem_setShutdownHandler
 * 和 XSystem_setRebootHandler 注册板级实现，不得从 Shell 绕过公共抽象。
 */

#include "XSystem_Protected.h"

#if !defined(__linux__) && !defined(_WIN32)

XSystemResult XSystem_platformReset(XSystemResetReason reason)
{
    (void)reason;
    return XSystemResult_NotSupported;
}

XSystemResult XSystem_platformReboot(XSystemRebootMode mode)
{
    (void)mode;
    return XSystemResult_NotSupported;
}

XSystemResult XSystem_platformShutdown(void)
{
    return XSystemResult_NotSupported;
}

#endif /* !defined(__linux__) && !defined(_WIN32) */
