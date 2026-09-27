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
#include "XMemory.h"

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

#if defined(_WIN32)

const char* XSystem_platformEnvironment(const char* name)
{
    static char buffer[1024];
    DWORD len;
    if (!name || !name[0]) return NULL;
    len = GetEnvironmentVariableA(name, buffer, (DWORD)sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return NULL;
    return buffer;
}

bool XSystem_platformHasEnvironment(const char* name)
{
    if (!name || !name[0]) return false;
    return GetEnvironmentVariableA(name, NULL, 0) != 0;
}

int64_t XSystem_platformPid(void)
{
    return (int64_t)GetCurrentProcessId();
}

bool XSystem_platformExecutableFilePath(char* path, size_t cap)
{
    DWORD n;
    if (!path || cap == 0u) return false;
    n = GetModuleFileNameA(NULL, path, (DWORD)cap);
    if (n == 0u || n >= (DWORD)cap) return false;
    path[n] = '\0';
    return true;
}

#if XSYSTEM_CPU_USAGE_ON

/* FILETIME 是 100ns 单位的 64 位墙钟：合并高低位为 uint64 便于做差。 */
static uint64_t xsystem_win32_filetimeU64(const FILETIME* ft)
{
    return ((uint64_t)ft->dwHighDateTime << 32) | (uint64_t)ft->dwLowDateTime;
}

double XSystem_platformCpuUsagePercent(void)
{
    static bool s_init = false;
    static uint64_t s_idle;
    static uint64_t s_kernel;
    static uint64_t s_user;
    FILETIME idleFt;
    FILETIME kernelFt;
    FILETIME userFt;
    uint64_t idle;
    uint64_t kernel;
    uint64_t user;
    uint64_t dIdle;
    uint64_t dTotal;
    uint64_t dBusy;
    double percent;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) return -1.0;
    idle = xsystem_win32_filetimeU64(&idleFt);
    kernel = xsystem_win32_filetimeU64(&kernelFt); /* kernel 含 idle */
    user = xsystem_win32_filetimeU64(&userFt);
    if (!s_init) {
        s_idle = idle;
        s_kernel = kernel;
        s_user = user;
        s_init = true;
        return -1.0; /* 首次调用只建基线 */
    }
    dIdle = idle - s_idle;
    dTotal = (kernel - s_kernel) + (user - s_user);
    s_idle = idle;
    s_kernel = kernel;
    s_user = user;
    if (dTotal == 0u) return -1.0;
    dBusy = dTotal > dIdle ? dTotal - dIdle : 0u;
    percent = 100.0 * (double)dBusy / (double)dTotal;
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    return percent;
}

#endif /* XSYSTEM_CPU_USAGE_ON */

#if XSYSTEM_GPU_USAGE_ON

#include <pdh.h>
#pragma comment(lib, "pdh.lib")
/* 部分 SDK 头不导出这些状态常量（定义在 pdhmsg.h 且随版本差异），兜底。 */
#ifndef PDH_CSTATUS_VALID_DATA
#define PDH_CSTATUS_VALID_DATA ((PDH_STATUS)0x00000000L)
#endif
#ifndef PDH_CSTATUS_NEW_DATA
#define PDH_CSTATUS_NEW_DATA ((PDH_STATUS)0x00000001L)
#endif
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#endif

double XSystem_platformGpuUsagePercent(void)
{
    static PDH_HQUERY s_query = NULL;
    static PDH_HCOUNTER s_counter = NULL;
    static bool s_failed = false;
    static bool s_primed = false;
    if (s_failed) return -1.0;
    if (!s_query) {
        if (PdhOpenQueryW(NULL, 0, &s_query) != ERROR_SUCCESS) {
            s_query = NULL;
            s_failed = true;
            return -1.0;
        }
        /* PdhAddEnglishCounter 使用英文计数器名，规避本地区域化路径；
           路径格式 \Object(实例)\Counter——开头单反斜杠=本机对象路径
           （双反斜杠前缀是 \\computer 远程形式，会得到 CSTATUS_NO_OBJECT）；
           计数器缺失（虚拟机/远程会话无 GPU 引擎实例）记失败恒 -1。 */
        if (PdhAddEnglishCounterW(
                s_query, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                &s_counter) != ERROR_SUCCESS) {
            PdhCloseQuery(s_query);
            s_query = NULL;
            s_counter = NULL;
            s_failed = true;
            return -1.0;
        }
    }
    if (PdhCollectQueryData(s_query) != ERROR_SUCCESS) return -1.0;
    if (!s_primed) {
        s_primed = true;
        return -1.0; /* PDH 差值计数器需两次采集才出首个有效值 */
    }
    {
        /* 通配计数器实例可达数百个（每 GPU 进程×引擎类型，本机实测 566
           个≈14KB）。用静态大缓冲一步取全，避免 NULL 探测的 PDH 语义坑；
           实例激增超容量时按需扩容重试一次。 */
        static BYTE* s_buffer = NULL;
        static DWORD s_bufferCap = 0;
        PDH_FMT_COUNTERVALUE_ITEM_W* items;
        DWORD size;
        DWORD count = 0;
        PDH_STATUS st;
        double best = -1.0;
        if (!s_buffer) {
            s_bufferCap = 256u * 1024u;
            s_buffer = (BYTE*)XMalloc_System(s_bufferCap);
            if (!s_buffer) return -1.0;
        }
        size = s_bufferCap;
        st = PdhGetFormattedCounterArrayW(
            s_counter, PDH_FMT_DOUBLE, &size, &count,
            (PDH_FMT_COUNTERVALUE_ITEM_W*)(void*)s_buffer);
        if (st == PDH_MORE_DATA) {
            void* grown = XRealloc_System(s_buffer, size);
            if (!grown) return -1.0;
            s_buffer = (BYTE*)grown;
            s_bufferCap = size;
            st = PdhGetFormattedCounterArrayW(
                s_counter, PDH_FMT_DOUBLE, &size, &count,
                (PDH_FMT_COUNTERVALUE_ITEM_W*)(void*)s_buffer);
        }
        if (st != ERROR_SUCCESS) return -1.0;
        items = (PDH_FMT_COUNTERVALUE_ITEM_W*)(void*)s_buffer;
        {
            DWORD i;
            for (i = 0; i < count; ++i) {
                PDH_STATUS cs = items[i].FmtValue.CStatus;
                double v = items[i].FmtValue.doubleValue;
                if (cs != PDH_CSTATUS_VALID_DATA && cs != PDH_CSTATUS_NEW_DATA)
                    continue;
                if (v > best) best = v;
            }
        }
        if (best < 0.0) return -1.0;
        if (best > 100.0) best = 100.0;
        return best; /* 多引擎实例取最大：单引擎满载即视为 GPU 忙 */
    }
}

#endif /* XSYSTEM_GPU_USAGE_ON */

#endif /* defined(_WIN32) */
