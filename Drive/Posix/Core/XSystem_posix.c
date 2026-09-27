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
#include <stdlib.h>
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

const char* XSystem_platformEnvironment(const char* name)
{
    if (!name || !name[0]) return NULL;
    return getenv(name);
}

bool XSystem_platformHasEnvironment(const char* name)
{
    if (!name || !name[0]) return false;
    return getenv(name) != NULL;
}

int64_t XSystem_platformPid(void)
{
    return (int64_t)getpid();
}

bool XSystem_platformExecutableFilePath(char* path, size_t cap)
{
    ssize_t n;
    if (!path || cap < 2u) return false;
    n = readlink("/proc/self/exe", path, cap - 1u);
    if (n <= 0 || (size_t)n >= cap) return false;
    path[n] = '\0';
    return true;
}

#if XSYSTEM_CPU_USAGE_ON

#include <stdio.h>

double XSystem_platformCpuUsagePercent(void)
{
    static bool s_init = false;
    static uint64_t s_prev[8];
    FILE* f;
    char line[256];
    uint64_t v[8];
    uint64_t dTotal;
    uint64_t dIdleAll;
    uint64_t dBusy;
    double percent;
    int fields;
    int i;
    f = fopen("/proc/stat", "r");
    if (!f) return -1.0;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1.0;
    }
    fclose(f);
    /* 行格式："cpu  user nice system idle iowait irq softirq steal" */
    fields = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                    (unsigned long long*)&v[0], (unsigned long long*)&v[1],
                    (unsigned long long*)&v[2], (unsigned long long*)&v[3],
                    (unsigned long long*)&v[4], (unsigned long long*)&v[5],
                    (unsigned long long*)&v[6], (unsigned long long*)&v[7]);
    if (fields < 4) return -1.0;
    for (i = fields; i < 8; ++i) v[i] = 0u;
    if (!s_init) {
        for (i = 0; i < 8; ++i) s_prev[i] = v[i];
        s_init = true;
        return -1.0; /* 首次调用只建基线 */
    }
    /* 先按旧基线做差，再推进基线。 */
    dTotal = 0u;
    for (i = 0; i < 8; ++i) dTotal += v[i] - s_prev[i];
    dIdleAll = (v[3] - s_prev[3]) + (v[4] - s_prev[4]);
    for (i = 0; i < 8; ++i) s_prev[i] = v[i];
    if (dTotal == 0u) return -1.0;
    dBusy = dTotal > dIdleAll ? dTotal - dIdleAll : 0u;
    percent = 100.0 * (double)dBusy / (double)dTotal;
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    return percent;
}

#endif /* XSYSTEM_CPU_USAGE_ON */

#if XSYSTEM_MEMORY_USAGE_ON

#include <stdio.h>
#include <string.h>

bool XSystem_platformMemoryInfo(XSystemMemoryInfo* info)
{
    FILE* f;
    char line[256];
    uint64_t totalKib = 0u;
    uint64_t availableKib = 0u;
    bool hasTotal = false;
    bool hasAvailable = false;
    if (!info) return false;
    f = fopen("/proc/meminfo", "r");
    if (!f) return false;
    /* 行格式："MemTotal:       16384256 kB"；逐行找所需键即可。 */
    while (!hasTotal || !hasAvailable) {
        uint64_t value = 0u;
        char key[32];
        if (!fgets(line, sizeof(line), f)) break;
        if (sscanf(line, "%31s %llu", key, (unsigned long long*)&value) != 2)
            continue;
        if (!hasTotal && strcmp(key, "MemTotal:") == 0) {
            totalKib = value;
            hasTotal = true;
        } else if (!hasAvailable && strcmp(key, "MemAvailable:") == 0) {
            availableKib = value;
            hasAvailable = true;
        }
    }
    fclose(f);
    if (!hasTotal || totalKib == 0u) return false;
    /* 老内核缺 MemAvailable 时退化为 MemFree（不含可回收缓存，偏保守）。 */
    info->totalBytes = totalKib * 1024u;
    info->availableBytes = (hasAvailable ? availableKib : 0u) * 1024u;
    info->usedBytes = info->totalBytes > info->availableBytes
                          ? info->totalBytes - info->availableBytes
                          : 0u;
    return true;
}

#endif /* XSYSTEM_MEMORY_USAGE_ON */

#endif /* defined(__linux__) */
