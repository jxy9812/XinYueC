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

#if !defined(__linux__) && !defined(_WIN32)

const char* XSystem_platformEnvironment(const char* name)
{
    (void)name;
    return NULL;
}

bool XSystem_platformHasEnvironment(const char* name)
{
    (void)name;
    return false;
}

int64_t XSystem_platformPid(void)
{
    return 0;
}

bool XSystem_platformExecutableFilePath(char* path, size_t cap)
{
    (void)path;
    (void)cap;
    return false;
}

#if XSYSTEM_CPU_USAGE_ON

double XSystem_platformCpuUsagePercent(void)
{
    return -1.0; /* 无 OS 平台无系统负载计数 */
}

#endif /* XSYSTEM_CPU_USAGE_ON */

#if XSYSTEM_GPU_USAGE_ON

double XSystem_platformGpuUsagePercent(void)
{
    return -1.0; /* 无 GPU 概念 */
}

#endif /* XSYSTEM_GPU_USAGE_ON */

#if XSYSTEM_MEMORY_USAGE_ON

#include "XMemory.h"

bool XSystem_platformMemoryInfo(XSystemMemoryInfo* info)
{
    XMemoryStatistics stats;
    if (!info) return false;
    /* 无 OS 平台没有系统物理内存查询接口：按裁定回落库内 XMemory 统计，
       以全局多级内存池的容量为内存域（usedBytes + availableBytes =
       totalBytes 的契约保持成立）；池未启用时报告不可用。 */
    stats = XMemory_statistics();
    if (stats.poolTotalBytes == 0u) return false;
    info->totalBytes = (uint64_t)stats.poolTotalBytes;
    info->usedBytes = (uint64_t)stats.poolUsedBytes;
    info->availableBytes = info->totalBytes > info->usedBytes
                               ? info->totalBytes - info->usedBytes
                               : 0u;
    return true;
}

#endif /* XSYSTEM_MEMORY_USAGE_ON */

#endif /* !defined(__linux__) && !defined(_WIN32) */
