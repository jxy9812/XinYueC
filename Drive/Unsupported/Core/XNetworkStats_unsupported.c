/**
 * @file XNetworkStats_unsupported.c
 * @brief 未接入网络计数统计平台的兜底实现。
 * @details Windows 与 Linux 之外的目标返回 false，性能悬浮层据此显示
 *          n/a，避免业务层依赖平台专用代码。
 */
#if !defined(_WIN32) && !defined(__linux__)
#include "XNetworkStats.h"

bool XNetworkStats_readCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    if (rxBytes) *rxBytes = 0;
    if (txBytes) *txBytes = 0;
    return false;
}
#endif /* !_WIN32 && !__linux__ */
