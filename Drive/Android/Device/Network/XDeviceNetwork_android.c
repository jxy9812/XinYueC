/**
 * @file       XDeviceNetwork_android.c
 * @brief      安卓网络配置的设备层平台实现（从 POSIX 驱动下沉）。
 * @details    [2026-10-24 分层下沉] 逻辑自
 *             Drive/Posix/Device/Network/XDeviceNetwork_posix.c 原样迁出：
 *             POSIX 侧以 `#if defined(__ANDROID__)` 内联的安卓分支，是共享层
 *             泄漏平台知识的表现。本文件把它收回平台层，POSIX 驱动只保留
 *             一次调用。
 *             数据来源是 JNI 通道（Drive/Android/Core/XAndroidJniChannels.h），
 *             真正取数的实现在 Drive/Android/Core/XSystemAndroid.c。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDeviceNetwork_android.h"

#if defined(__ANDROID__) && defined(XNETWORK_USE_PLATFORM_API)

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "XHostAddress.h"
#include "XAndroidJniChannels.h"

void XAndroidNetwork_applyInterfaceConfig(XDeviceNetworkInterfaceConfig* outConfig,
                                           const char* ifname)
{
    if (!outConfig) return;

    /* 1) DHCP 租约：WifiManager.getDhcpInfo 给地址/网关/DNS/DHCP 服务器。
     *    描述的是**当前 WiFi 连接**，只在租约 IP 与本接口 IPv4 一致时采用。 */
    {
        XAndroidDhcpInfo dhcp;
        if (XAndroid_netDhcpInfo(&dhcp) && dhcp.ipAddress != 0 &&
            !XHostAddress_isNull(&outConfig->ipv4Address) &&
            XHostAddress_toIPv4Address(&outConfig->ipv4Address) ==
                dhcp.ipAddress) {
            if (dhcp.gateway)
                XHostAddress_setAddressIPv4(&outConfig->ipv4Gateway,
                                            dhcp.gateway);
            XHostAddress_setAddressIPv4(&outConfig->dhcpServer,
                                        dhcp.serverAddress);
            if (dhcp.dns1)
                XHostAddress_setAddressIPv4(&outConfig->dnsPrimary,
                                            dhcp.dns1);
            if (dhcp.dns2)
                XHostAddress_setAddressIPv4(&outConfig->dnsSecondary,
                                            dhcp.dns2);
            outConfig->dhcpEnabled = true; /* 有租约=自动获取。 */
        }
    }

    /* 2) LinkProperties：v6 默认网关 + 系统实际下发的 DNS（可能含 v6）。
     *    按活动网络接口名匹配，避免把别的网络的 DNS 串进来。 */
    {
        char v6gw[48];
        char dns1t[48];
        char dns2t[48];
        char activeIface[IFNAMSIZ];
        if (XAndroid_netLinkInfo(v6gw, (int)sizeof(v6gw),
                                 dns1t, (int)sizeof(dns1t),
                                 dns2t, (int)sizeof(dns2t),
                                 activeIface, (int)sizeof(activeIface)) &&
            activeIface[0] && ifname && strcmp(activeIface, ifname) == 0) {
            uint8_t b6[16];
            struct in_addr a4;
            if (v6gw[0] && inet_pton(AF_INET6, v6gw, b6) == 1 &&
                XHostAddress_isNull(&outConfig->ipv6Gateway)) {
                XHostAddress_setAddressIPv6(&outConfig->ipv6Gateway, b6);
            }
            /* DNS 以 LinkProperties 优先（系统实际下发，含 v6）。 */
            if (dns1t[0]) {
                if (inet_pton(AF_INET, dns1t, &a4) == 1)
                    XHostAddress_setAddressIPv4(&outConfig->dnsPrimary,
                                                ntohl(a4.s_addr));
                else if (inet_pton(AF_INET6, dns1t, b6) == 1)
                    XHostAddress_setAddressIPv6(&outConfig->dnsPrimary, b6);
            }
            if (dns2t[0]) {
                if (inet_pton(AF_INET, dns2t, &a4) == 1)
                    XHostAddress_setAddressIPv4(&outConfig->dnsSecondary,
                                                ntohl(a4.s_addr));
                else if (inet_pton(AF_INET6, dns2t, b6) == 1)
                    XHostAddress_setAddressIPv6(&outConfig->dnsSecondary, b6);
            }
        }
    }
}

bool XAndroidNetwork_setDhcp(uint32_t ifIndex)
{
    /* 安卓：系统 WiFi 的 IP 模式在 WifiConfiguration 上（ipAssignment），
     * 经 XNetAdapter 反射 updateNetwork。 */
    (void)ifIndex;
    return XAndroid_netSetDhcp();
}

/** @brief 掩码文本（点分十进制）→ 前缀长度（非法返回 -1）。
 *  安卓 StaticIpConfiguration 用 prefixLength 而非掩码文本。 */
static int xandroid_net_maskToPrefix(const char* mask)
{
    in_addr_t m;
    uint32_t v;
    int bits = 0;
    if (!mask || (m = inet_addr(mask)) == INADDR_NONE) return -1;
    v = ntohl(m);
    /* 合法掩码必须是连续 1 开头。 */
    if (v != 0 && ((~v + 1u) & ~v) != 0) return -1;
    while (v & 0x80000000u) { ++bits; v <<= 1; }
    return bits;
}

bool XAndroidNetwork_setStaticV4(uint32_t ifIndex,
                                  const char* ipv4Address,
                                  const char* ipv4Netmask,
                                  const char* ipv4Gateway,
                                  const char* dnsPrimary,
                                  const char* dnsSecondary)
{
    /* 安卓：静态 v4 经 WifiConfiguration（XNetAdapter 反射 updateNetwork，
     * 受配置所有权限制）；掩码文本换算前缀长度（StaticIpConfiguration
     * 用 prefixLength 而非掩码）。 */
    int prefix = xandroid_net_maskToPrefix(ipv4Netmask);
    (void)ifIndex;
    return XAndroid_netSetStaticV4(ipv4Address, prefix, ipv4Gateway,
                                   dnsPrimary, dnsSecondary);
}

bool XAndroidNetwork_setStaticIpv6(uint32_t ifIndex,
                                    const char* ipv6Address,
                                    int prefixLength,
                                    const char* ipv6Gateway)
{
    (void)ifIndex; (void)ipv6Address; (void)prefixLength; (void)ipv6Gateway;
    return false;
}

#endif /* __ANDROID__ && XNETWORK_USE_PLATFORM_API */
