/**
 * @file       XNetworkStats.h
 * @brief      主机网络收发累计字节数的跨平台抽象接口。
 * @details    性能悬浮层等业务侧只依赖本文件的公共契约，不直接调用
 *             GetIfTable 或 /proc/net/dev。具体平台实现位于 Drive：
 *             Windows 使用 IP Helper，Linux 读取 /proc/net/dev，
 *             其余平台由 Unsupported 存根返回 false。
 */

#ifndef XNETWORKSTATS_H
#define XNETWORKSTATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 读取主机所有非回环接口的累计收发字节数。
 * @param rxBytes 输出累计接收字节数；失败时清零。不可为 NULL。
 * @param txBytes 输出累计发送字节数；失败时清零。不可为 NULL。
 * @return 成功返回 true，平台不支持或读取失败返回 false。
 * @note 返回的是接口开机/驱动中的累计计数器，调用方应自行计算相邻
 *       两次采样差值以获得速率。
 */
bool XNetworkStats_readCounters(uint64_t* rxBytes, uint64_t* txBytes);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORKSTATS_H */
