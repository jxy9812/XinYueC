/**
 * @file       XDeviceNetwork_android.h
 * @brief      安卓网络配置的设备层平台实现入口（Drive/Android 内部用）。
 * @details    [2026-10-24 分层下沉] 安卓上 XDeviceNetwork 的**基础**实现仍
 *             复用 POSIX 驱动（接口枚举、计数器、netlink 事件泵等与安卓同源），
 *             但"安卓特有"的取数与下发属于平台差异：
 *               - 查询：WifiManager.getDhcpInfo（租约/网关/DNS/DHCP 服务器）
 *                       + ConnectivityManager.LinkProperties（v6 网关、
 *                       系统实际下发的 DNS，含 v6）；
 *               - 下发：WifiConfiguration.ipAssignment / staticIpConfiguration
 *                       （API 29+ 已从公开 SDK 移除，须反射）。
 *             这些原先以 `#if defined(__ANDROID__)` **内联在共享驱动**
 *             Drive/Posix/Device/Network/XDeviceNetwork_posix.c 里——共享层
 *             不该知道安卓。本文件把策略下沉到平台层，POSIX 驱动只留调用。
 *             参照同族先例 Drive/Android/Device/Network/XDeviceWiFi_android.c
 *             （WiFi 后端整份在平台层，POSIX 侧整份排除安卓）。
 * @note       仅限 Drive/Android 内部 include——业务层一律走框架契约
 *             （XDeviceNetwork.h），不得直呼。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDEVICENETWORK_ANDROID_H
#define XDEVICENETWORK_ANDROID_H

#include <stdint.h>
#include <stdbool.h>

#include "XDeviceNetwork.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 查询 ==================== */

/**
 * @brief  用系统服务补齐接口配置（网关/DHCP 服务器/DNS/DHCP 模式）。
 * @param  outConfig  已由 POSIX 侧填好地址/掩码等基础字段的配置结构（就地改写）。
 * @param  ifname     接口名（与 LinkProperties 的活动网络接口比对）。
 * @note   POSIX 常规取数路径在安卓应用域全空（/proc/net 被 SELinux 拒、
 *         无 /etc/resolv.conf、无 udhcpc），安卓的可用数据只在系统服务里。
 *         DhcpInfo 描述的是**当前 WiFi 连接**，只在租约 IP 与本接口 IPv4
 *         一致时采用；LinkProperties 按活动网络接口名匹配。
 */
void XAndroidNetwork_applyInterfaceConfig(XDeviceNetworkInterfaceConfig* outConfig,
                                           const char* ifname);

/* ==================== 下发 ==================== */

/**
 * @brief  切回 DHCP（安卓：WifiConfiguration.ipAssignment=DHCP，updateNetwork）。
 * @note   对用户在系统里连过的网络受配置所有权限制（会被拒）；当前已是
 *         DHCP（租约在）则目标态已达成。
 */
bool XAndroidNetwork_setDhcp(uint32_t ifIndex);

/**
 * @brief  设静态 IPv4（含前缀长度换算）。
 * @param  ipv4Netmask  点分十进制掩码文本（框架契约口径），内部换算成
 *                      StaticIpConfiguration 需要的 prefixLength。
 */
bool XAndroidNetwork_setStaticV4(uint32_t ifIndex,
                                  const char* ipv4Address,
                                  const char* ipv4Netmask,
                                  const char* ipv4Gateway,
                                  const char* dnsPrimary,
                                  const char* dnsSecondary);

/**
 * @brief  设静态 IPv6。
 * @note   AOSP 的 WiFi 静态 IP（StaticIpConfiguration）只支持 v4 地址，
 *         系统 WiFi 设置页同样没有静态 IPv6 选项——系统级不支持，如实拒绝。
 */
bool XAndroidNetwork_setStaticIpv6(uint32_t ifIndex,
                                    const char* ipv6Address,
                                    int prefixLength,
                                    const char* ipv6Gateway);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICENETWORK_ANDROID_H */
