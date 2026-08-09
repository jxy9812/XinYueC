/**
 * @file XSystem_posix.c
 * @brief Linux 系统复位、关机和有序重启后端。
 * @details
 * 本文件是 XSystem 的 Linux 平台边界。reset 直接请求内核重新启动，不主动
 * 同步文件系统；reboot 和 shutdown 先调用 sync 刷新系统缓存，再请求相应操作。
 * 两个操作都需要 CAP_SYS_BOOT 或等效系统权限，权限不足时返回明确结果。
 * 本文件不执行外部命令、不使用 shell，也不被 XConsoleShell 直接包含。
 */

#include "XSystem_Protected.h"

#if defined(__linux__)

#include <errno.h>
#include <sys/reboot.h>
#include <unistd.h>

static XSystemResult xsystem_posix_reboot_now(void)
{
    if (reboot(RB_AUTOBOOT) == 0) return XSystemResult_Ok;
    if (errno == EPERM || errno == EACCES)
        return XSystemResult_PermissionDenied;
    return XSystemResult_Failed;
}

static XSystemResult xsystem_posix_poweroff_now(void)
{
    if (reboot(RB_POWER_OFF) == 0) return XSystemResult_Ok;
    if (errno == EPERM || errno == EACCES)
        return XSystemResult_PermissionDenied;
    return XSystemResult_Failed;
}

XSystemResult XSystem_platformReset(XSystemResetReason reason)
{
    (void)reason;
    return xsystem_posix_reboot_now();
}

XSystemResult XSystem_platformReboot(XSystemRebootMode mode)
{
    if (mode != XSystemRebootMode_Normal)
        return XSystemResult_NotSupported;
    sync();
    return xsystem_posix_reboot_now();
}

XSystemResult XSystem_platformShutdown(void)
{
    sync();
    return xsystem_posix_poweroff_now();
}

#endif /* defined(__linux__) */
