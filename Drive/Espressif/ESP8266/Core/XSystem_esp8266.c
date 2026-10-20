/**
 * @file XSystem_esp8266.c
 * @brief ESP8266 RTOS SDK 的系统级平台实现。
 * @details
 * 覆盖复位/重启、环境变量、进程标识与物理内存快照六类查询。ESP8266 是单任务
 * 裸机 + FreeRTOS 的组合，没有进程模型也没有关机电源管理，因此：
 *   - reset / reboot 都落到 SDK 的 system_restart()；
 *   - shutdown 不支持——ESP8266 无法真正断电，深睡眠会直接结束控制台会话，
 *     因此按契约返回 NotSupported 而不是偷偷睡眠；
 *   - 内存快照把 system_get_free_heap_size() 的真实余量并入库内统计，
 *     使 info 命令能反映芯片实际可申领的堆。
 *
 * 本文件仅在 ESP8266 目标下编译，桌面构建由 Drive/Unsupported 提供存根。
 */

#include "XSystem_Protected.h"

#if defined(__ESP8266__) || defined(ESP8266)

#include "XMemory.h"
#include "esp_system.h"

/* SDK 的 system_get_free_heap_size() 返回 uint32；ESP8266 的可申领堆始终
 * 在 4GB 以内，转换到 uint64 不会溢出。 */
#define XESP8266_HEAP_SIZE(b) ((uint64_t)(b))

XSystemResult XSystem_platformReset(XSystemResetReason reason)
{
    (void)reason;
    /* 不返回：SDK 完成复位后从 boot 区重新启动。 */
    system_restart();
    return XSystemResult_Ok;
}

XSystemResult XSystem_platformReboot(XSystemRebootMode mode)
{
    (void)mode;
    system_restart();
    return XSystemResult_Ok;
}

XSystemResult XSystem_platformShutdown(void)
{
    /* ESP8266 无独立电源门控，深睡眠等同于结束会话；
     * 产品若要省电应显式走 XThread 休眠接口，而不是从这里静默消失。 */
    return XSystemResult_NotSupported;
}

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
    /* 固件是唯一执行体，稳定报 1 以便上层按单进程语义工作。 */
    return 1;
}

bool XSystem_platformProcessAlive(int64_t pid)
{
    return pid == 1;
}

bool XSystem_platformExecutableFilePath(char* path, size_t cap)
{
    (void)path;
    (void)cap;
    return false;
}

double XSystem_platformCpuUsagePercent(void)
{
    /* ESP8266 无 /proc 采样源；以恒 0 表示"不可用"而非"空闲"，
     * 由上层决定是否展示该指标。 */
    return 0.0;
}

double XSystem_platformGpuUsagePercent(void)
{
    return 0.0;
}

#if XSYSTEM_MEMORY_USAGE_ON
bool XSystem_platformMemoryInfo(XSystemMemoryInfo* info)
{
    XMemoryStatistics stats;
    uint64_t freeHeap;
    uint64_t total;

    if (!info)
        return false;

    stats = XMemory_statistics_2(XMEMORY_TYPE_HYBRID);
    if (stats.poolTotalBytes == 0u)
        return false;

    /* 库内池只覆盖 X 自己分配的内存，芯片上还有一块系统堆。
     * 把系统空闲堆并入 available，避免 info 把芯片报成"快满了"。 */
    freeHeap = XESP8266_HEAP_SIZE(system_get_free_heap_size());
    total = (uint64_t)stats.poolTotalBytes + freeHeap;

    info->totalBytes = total;
    info->usedBytes = (uint64_t)stats.poolUsedBytes + (total > freeHeap ? total - freeHeap : 0u);
    info->availableBytes = total > info->usedBytes ? total - info->usedBytes : 0u;
    return true;
}
#endif /* XSYSTEM_MEMORY_USAGE_ON */

#endif /* defined(__ESP8266__) || defined(ESP8266) */
