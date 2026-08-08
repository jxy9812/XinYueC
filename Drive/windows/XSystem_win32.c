/**
 * @file XSystem_win32.c
 * @brief Windows 系统复位、关机和有序重启后端。
 * @details
 * 本文件通过 Win32 关机接口实现 XSystem 平台边界。reset 使用强制重启标志，
 * 可能使应用来不及保存数据；reboot 使用正常重启流程。调用前申请当前进程
 * 的关机权限，权限不足时返回 PermissionDenied。Shell 不直接包含 windows.h，
 * 不执行 shutdown.exe，也不拼接或运行外部命令。
 */

#include "XSystem_Protected.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static XSystemResult xsystem_win32_error(DWORD error)
{
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD ||
        error == ERROR_NOT_ALL_ASSIGNED)
        return XSystemResult_PermissionDenied;
    return XSystemResult_Failed;
}

static XSystemResult xsystem_win32_enable_shutdown_privilege(void)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES privileges;
    DWORD error;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return xsystem_win32_error(GetLastError());
    if (!LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME,
                               &privileges.Privileges[0].Luid)) {
        error = GetLastError();
        CloseHandle(token);
        return xsystem_win32_error(error);
    }
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL)) {
        error = GetLastError();
        CloseHandle(token);
        return xsystem_win32_error(error);
    }
    error = GetLastError();
    CloseHandle(token);
    return error == ERROR_SUCCESS ? XSystemResult_Ok
                                  : xsystem_win32_error(error);
}

static XSystemResult xsystem_win32_restart(UINT flags, DWORD reason)
{
    XSystemResult privilegeResult =
        xsystem_win32_enable_shutdown_privilege();
    if (privilegeResult != XSystemResult_Ok) return privilegeResult;
    if (ExitWindowsEx(flags, reason)) return XSystemResult_Ok;
    return xsystem_win32_error(GetLastError());
}

XSystemResult XSystem_platformReset(XSystemResetReason reason)
{
    DWORD shutdownReason = SHTDN_REASON_MAJOR_HARDWARE |
                           SHTDN_REASON_MINOR_OTHER;
    (void)reason;
    return xsystem_win32_restart(EWX_REBOOT | EWX_FORCE, shutdownReason);
}

XSystemResult XSystem_platformReboot(XSystemRebootMode mode)
{
    DWORD shutdownReason;
    if (mode != XSystemRebootMode_Normal)
        return XSystemResult_NotSupported;
    shutdownReason = SHTDN_REASON_MAJOR_APPLICATION |
                     SHTDN_REASON_MINOR_MAINTENANCE |
                     SHTDN_REASON_FLAG_PLANNED;
    return xsystem_win32_restart(EWX_REBOOT, shutdownReason);
}

XSystemResult XSystem_platformShutdown(void)
{
    DWORD shutdownReason = SHTDN_REASON_MAJOR_APPLICATION |
                            SHTDN_REASON_MINOR_MAINTENANCE |
                            SHTDN_REASON_FLAG_PLANNED;
    return xsystem_win32_restart(EWX_POWEROFF, shutdownReason);
}

#endif /* defined(_WIN32) */
