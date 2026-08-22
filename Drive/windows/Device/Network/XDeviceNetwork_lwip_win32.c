/**
 * @file XDeviceNetwork_lwip_win32.c
 * @brief lwIP Windows 平台虚拟网卡实现 - 基于 Npcap 虚拟网卡
 *
 * 基于 STM32F407+FreeRTOS 成功移植经验，适配 Windows + Npcap 平台。
 * 参考 lwIP 官方 contrib/ports/win32/pcapif.c 实现。
 *
 * 核心功能：
 *   1. 动态加载 Npcap DLL（wpcap.dll）
 *   2. 创建回环网卡 lo0 (127.0.0.1)
 *   3. 在所有已连接网卡上创建独立的 lwIP netif
 *   4. 为每个外部 netif 启用 DHCP，并按 Windows 默认路由选择外部默认网卡
 *   5. 使用 XRandomGenerator 生成随机 MAC 地址，避免与 Windows 网卡冲突
 */
#ifdef _WIN32
#include "CXinYueConfig.h"
#include "XNetwork_config.h"
#if defined(XNETWORK_USE_LWIP)

/* ================================================================
 * Windows 系统头文件 - 必须在 lwIP 头文件之前包含
 * 原因：避免 lwIP 的 IP_STATS/ICMP_STATS/TCP_STATS/UDP_STATS/IP6_STATS
 * 与 Windows iprtrmib.h 中的同名宏产生冲突
 * ================================================================ */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <locale.h>

#include "XDeviceNetwork.h"

/* 仅供 lwIP 平台网卡实现使用；不暴露到 XDeviceNetwork 公共头。 */
struct netif* XDeviceNetworkLwip_platform_init(void);
void XDeviceNetworkLwip_platform_deinit(void);
struct netif* XDeviceNetworkLwip_defaultNetif(void);
void XDeviceNetworkLwip_setDefaultNetif(struct netif* netif);
void XDeviceNetworkLwip_pollPcap(void);
#include "XMemory.h"
#include "XThread.h"
#include "XPrintf.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

/* Netif input function: tcpip_input (NO_SYS=0, via tcpip_thread) or ethernet_input (NO_SYS=1, direct) */
#if NO_SYS
#define XNETIF_INPUT_FN  ethernet_input
#else
#define XNETIF_INPUT_FN  tcpip_input
#endif

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

/* IP 地址配置方式：静态IP 或 DHCP */
//#define LWIP_USE_STATIC_IP
#define LWIP_USE_DHCP

#ifdef LWIP_USE_STATIC_IP
#define LWIP_STATIC_IP      192,168,1,200
#define LWIP_STATIC_MASK    255,255,255,0
#define LWIP_STATIC_GW      192,168,1,254
#endif

#ifndef PCAP_OPENFLAG_PROMISCUOUS
#define PCAP_OPENFLAG_PROMISCUOUS 1
#endif

#ifndef NETIF_FLAG_LOOPBACK
#define NETIF_FLAG_LOOPBACK 0x200
#endif

#define NPCAP_MAX_ADAPTERS  16
#define NPCAP_READ_TIMEOUT_MS 10

/* Npcap 函数指针类型定义（动态加载，避免编译时依赖 wpcap.lib） */
typedef void* pcap_t;
typedef unsigned int bpf_u_int32;
typedef struct pcap_if pcap_if_t;
struct pcap_if { struct pcap_if *next; char *name; char *description; void *addresses; bpf_u_int32 flags; };
struct pcap_pkthdr { struct timeval ts; bpf_u_int32 caplen; bpf_u_int32 len; };
struct bpf_program { unsigned int bf_len; void* bf_insns; };
typedef pcap_t (*pcap_open_live_t)(const char*,int,int,int,char*);
typedef int (*pcap_findalldevs_t)(pcap_if_t**,char*);
typedef void (*pcap_freealldevs_t)(pcap_if_t*);
typedef int (*pcap_next_ex_t)(pcap_t,struct pcap_pkthdr**,const u_char**);
typedef int (*pcap_dispatch_t)(pcap_t,int,void(*)(u_char*,const struct pcap_pkthdr*,const u_char*),u_char*);
typedef int (*pcap_sendpacket_t)(pcap_t,const u_char*,int);
typedef void (*pcap_close_t)(pcap_t);
typedef int (*pcap_compile_t)(pcap_t,struct bpf_program*,const char*,int,bpf_u_int32);
typedef int (*pcap_setfilter_t)(pcap_t,struct bpf_program*);
typedef void (*pcap_freecode_t)(struct bpf_program*);

/* 全局函数指针变量 */
static HMODULE g_dll = NULL;
static pcap_open_live_t pfn_open = NULL;
static pcap_findalldevs_t pfn_findall = NULL;
static pcap_freealldevs_t pfn_freeall = NULL;
static pcap_next_ex_t pfn_next = NULL;
static pcap_dispatch_t pfn_dispatch = NULL;
static pcap_sendpacket_t pfn_send = NULL;
static pcap_close_t pfn_close = NULL;
static pcap_compile_t pfn_compile = NULL;
static pcap_setfilter_t pfn_setfilter = NULL;
static pcap_freecode_t pfn_freecode = NULL;

/* Npcap DLL 动态加载与卸载 */
static bool load_npcap(void) {
    if (g_dll) return true;
    LWIP_DBG("[Npcap] 正在加载 wpcap.dll...\n");
    g_dll = LoadLibraryA("wpcap.dll");
    if (!g_dll) {
        LWIP_DBG("[Npcap] 无法加载 wpcap.dll 动态库。请安装 Npcap: https://npcap.com/\n");
        return false;
    }
    pfn_open = (pcap_open_live_t)GetProcAddress(g_dll, "pcap_open_live");
    pfn_findall = (pcap_findalldevs_t)GetProcAddress(g_dll, "pcap_findalldevs");
    pfn_freeall = (pcap_freealldevs_t)GetProcAddress(g_dll, "pcap_freealldevs");
    pfn_next = (pcap_next_ex_t)GetProcAddress(g_dll, "pcap_next_ex");
    pfn_dispatch = (pcap_dispatch_t)GetProcAddress(g_dll, "pcap_dispatch");
    pfn_send = (pcap_sendpacket_t)GetProcAddress(g_dll, "pcap_sendpacket");
    pfn_close = (pcap_close_t)GetProcAddress(g_dll, "pcap_close");
    pfn_compile = (pcap_compile_t)GetProcAddress(g_dll, "pcap_compile");
    pfn_setfilter = (pcap_setfilter_t)GetProcAddress(g_dll, "pcap_setfilter");
    pfn_freecode = (pcap_freecode_t)GetProcAddress(g_dll, "pcap_freecode");
    LWIP_DBG("[Npcap] wpcap.dll 加载成功, 函数指针: open=%p send=%p next=%p dispatch=%p close=%p\n",
             (void*)pfn_open, (void*)pfn_send, (void*)pfn_next, (void*)pfn_dispatch, (void*)pfn_close);
    return pfn_open && pfn_findall && pfn_freeall && pfn_next && pfn_send && pfn_close;
}

static void unload_npcap(void) {
    if (g_dll) { FreeLibrary(g_dll); g_dll = NULL; }
}

/* 回环网卡（lo0）预分配结构体 */
static struct netif g_loopNetif;

typedef struct {
    pcap_t pcap;                /* Npcap 捕获句柄 */
    struct netif* netif;        /* lwIP 网络接口指针 */
    char devName[256];          /* Npcap 设备名 */
    char winDesc[256];          /* Windows 适配器描述 */
    bool dhcpEnabled;           /* 是否启用了 DHCP */
    bool isDefaultRoute;        /* 是否为 Windows 最优默认 IPv4 路由接口 */
    uint32_t ifIndex;           /* Windows 适配器接口索引 */
} npcap_ctx_t;

static npcap_ctx_t g_npcapCtxs[NPCAP_MAX_ADAPTERS];
static int g_npcapCtxCount = 0;
static bool g_dhcpStarted = false;

/* 数据包统计计数器 */
static uint32_t g_rxCount[NPCAP_MAX_ADAPTERS];
static uint32_t g_txCount[NPCAP_MAX_ADAPTERS];

/* 生成 lwIP 使用的随机 MAC 地址（本地管理，单播）
 * 使用 XRandomGenerator_system() 获取密码学安全随机数
 * bit0=0 表示单播，bit1=1 表示本地管理地址，避免与真实网卡 MAC 冲突
 * 参考 lwIP 官方 pcapif.c 中的 LWIP_MAC_ADDR_BASE 机制 */
static void gen_lwip_mac(uint8_t macOut[6]) {
    /* 使用 XRandomGenerator 系统安全随机数生成器填充 MAC 地址 */
    XRandomGenerator_fillSecure(macOut, 6);
    /* bit0=0 单播，bit1=1 本地管理地址 */
    macOut[0] = (uint8_t)((macOut[0] & 0xFC) | 0x02);
    LWIP_DBG("[MAC] 生成随机MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
             macOut[0], macOut[1], macOut[2], macOut[3], macOut[4], macOut[5]);
}

/* 前向声明：Npcap 数据包发送函数 */
static err_t npcap_linkoutput(struct netif* netif, struct pbuf* p);

static bool ascii_contains_i(const char* text, const char* needle) {
    size_t needleLen;
    if (!text || !needle || !needle[0]) return false;
    needleLen = strlen(needle);
    for (; *text; ++text) {
        if (_strnicmp(text, needle, needleLen) == 0) return true;
    }
    return false;
}

/* Npcap uses \\Device\\NPF_{GUID}; IP Helper exposes the same GUID through
 * AdapterName.  Match that stable identifier instead of display names, which
 * are localized and can be duplicated. */
static bool npcap_matches_adapter(const char* deviceName, const IP_ADAPTER_ADDRESSES* adapter) {
    return deviceName && adapter && adapter->AdapterName &&
           ascii_contains_i(deviceName, adapter->AdapterName);
}

static PIP_ADAPTER_ADDRESSES find_default_ipv4_adapter(PIP_ADAPTER_ADDRESSES adapters) {
    MIB_IPFORWARDROW route;
    DWORD status = GetBestRoute(0, 0, &route);
    if (status != NO_ERROR) {
        LWIP_DBG("[Windows网卡] GetBestRoute(default) failed: %lu\n", (unsigned long)status);
        return NULL;
    }
    for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->IfIndex == route.dwForwardIfIndex) return adapter;
    }
    LWIP_DBG("[Windows网卡] default route interface %lu is absent from GetAdaptersAddresses\n",
             (unsigned long)route.dwForwardIfIndex);
    return NULL;
}

/* Expose every connected adapter that Npcap can identify. Do not require a
 * pre-existing IPv4 lease here: the lwIP netif itself obtains that address
 * through DHCP. Matching by GUID keeps this independent of localized or
 * duplicated adapter descriptions. */
static bool adapter_is_connected(const IP_ADAPTER_ADDRESSES* adapter) {
    if (!adapter || adapter->OperStatus != IfOperStatusUp) return false;
    return adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK;
}

static PIP_ADAPTER_ADDRESSES find_adapter_for_npcap(
    PIP_ADAPTER_ADDRESSES adapters, const char* deviceName) {
    for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter_is_connected(adapter) &&
            npcap_matches_adapter(deviceName, adapter)) return adapter;
    }
    return NULL;
}

/* Npcap 网卡初始化回调 - 由 netif_add 调用 */
static err_t npcap_init(struct netif* netif) {
    npcap_ctx_t* ctx = (npcap_ctx_t*)netif->state;
    if (!ctx) return ERR_IF;

    netif->name[0] = 'e'; netif->name[1] = 'n';

    /* 生成随机 MAC 地址 */
    uint8_t mac[6];
    gen_lwip_mac(mac);
    memcpy(netif->hwaddr, mac, 6);
    netif->hwaddr_len = 6;

    netif->mtu = 1500;
    netif->output = etharp_output;
    netif->linkoutput = npcap_linkoutput;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    /* 设置 BPF 内核级过滤器：只接收目标为 lwIP MAC 的包 + 广播/组播，排除自己发出的包
     * 将 MAC 过滤从用户态回调（每包 2 次 memcmp）提升到内核态 BPF 虚拟机，
     * 在繁忙网络上可过滤 95%+ 的无关流量，显著降低 CPU 开销。
     *
     * 与之前尝试的 "not ether host <WinMAC>" 和 "host <IP>" 不同：
     * - 使用正向包含（ether dst <lwipMAC>）而非反向排除
     * - 仅匹配以太网头部 MAC，不涉及 IP 层解析
     * - BPF 引擎对简单 MAC 比较有硬件级优化，可靠性高
     *
     * 失败时回退到 pcapif_input_callback 中的用户态软 MAC 过滤（已有逻辑） */
    if (pfn_compile && pfn_setfilter && pfn_freecode && ctx->pcap) {
        char filter[160];
        struct bpf_program fp;
        snprintf(filter, sizeof(filter),
                 "(ether dst %02x:%02x:%02x:%02x:%02x:%02x or ether broadcast or ether multicast) and not ether src %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (pfn_compile(ctx->pcap, &fp, filter, 1, 0) == 0) {
            if (pfn_setfilter(ctx->pcap, &fp) == 0) {
                LWIP_DBG("[BPF] 内核过滤器设置成功: %s\n", filter);
            } else {
                LWIP_DBG("[BPF] 过滤器应用失败: %s\n", filter);
            }
            pfn_freecode(&fp);
        } else {
            LWIP_DBG("[BPF] 过滤器编译失败: %s\n", filter);
        }
    }


    LWIP_DBG("[网卡] %c%c%d 初始化完成 MAC=%02X:%02X:%02X:%02X:%02X:%02X mtu=%d\n",
             netif->name[0], netif->name[1], netif->num,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], netif->mtu);
    return ERR_OK;
}

/* Npcap 数据包发送 - 将 pbuf 链中的数据通过 Npcap 发送到物理网络 */
static err_t npcap_linkoutput(struct netif* netif, struct pbuf* p) {
    npcap_ctx_t* ctx = (npcap_ctx_t*)netif->state;
    if (!ctx || !ctx->pcap || !pfn_send) return ERR_IF;

    /* 查找此网卡的上下文索引，用于统计 */
    int ctxIdx = -1;
    for (int i = 0; i < g_npcapCtxCount; i++) {
        if (&g_npcapCtxs[i] == ctx) { ctxIdx = i; break; }
    }

    /* netif MTU is 1500, but reject an oversize frame instead of silently
     * truncating a chained pbuf and reporting a successful transmission. */
    uint8_t buf[2048];
    if (!p || p->tot_len == 0 || p->tot_len > sizeof(buf)) return ERR_BUF;
    u16_t total = p->tot_len;
    if (pbuf_copy_partial(p, buf, total, 0) != total) return ERR_BUF;

    /* 打印发送数据包类型 - 前10个包全部打印，之后每50个打印一次 */
    if (ctxIdx >= 0 && total >= 14) {
        g_txCount[ctxIdx]++;
        int cnt = (int)g_txCount[ctxIdx];
        int shouldLog = (cnt <= 10) || (cnt % 50 == 1);
        if (shouldLog) {
            uint16_t ethType = ((uint16_t)buf[12] << 8) | buf[13];
            if (ethType == 0x0806 && total >= 42) {
                /* ARP 包：打印操作类型和 IP 地址 */
                uint16_t op = ((uint16_t)buf[20] << 8) | buf[21];
                const char* opStr = (op == 1) ? "REQ" : (op == 2) ? "REPLY" : "??";
                uint32_t srcIp = ((uint32_t)buf[28] << 24) | ((uint32_t)buf[29] << 16)
                               | ((uint32_t)buf[30] << 8) | buf[31];
                uint32_t dstIp = ((uint32_t)buf[38] << 24) | ((uint32_t)buf[39] << 16)
                               | ((uint32_t)buf[40] << 8) | buf[41];
                LWIP_DBG("[TX-ARP-%s] %c%c%d #%d len=%d src=%d.%d.%d.%d->dst=%d.%d.%d.%d\n",
                         opStr, netif->name[0], netif->name[1], netif->num, cnt, (int)total,
                         (int)(srcIp>>24)&0xFF, (int)(srcIp>>16)&0xFF,
                         (int)(srcIp>>8)&0xFF, (int)(srcIp)&0xFF,
                         (int)(dstIp>>24)&0xFF, (int)(dstIp>>16)&0xFF,
                         (int)(dstIp>>8)&0xFF, (int)(dstIp)&0xFF);
            } else if (ethType == 0x0800 && total >= 34) {
                uint8_t proto = buf[23]; /* IP header byte 9: protocol */
                const char* pro = (proto == 1) ? "ICMP" :
                                  (proto == 6) ? "TCP" :
                                  (proto == 17) ? "UDP" : "IP?";
                uint32_t srcIp, dstIp;
                memcpy(&srcIp, buf + 26, 4);
                memcpy(&dstIp, buf + 30, 4);
                srcIp = ntohl(srcIp);
                dstIp = ntohl(dstIp);
                LWIP_DBG("[TX-%s] %c%c%d #%d len=%d src=%d.%d.%d.%d->dst=%d.%d.%d.%d\n",
                         pro, netif->name[0], netif->name[1], netif->num, cnt, (int)total,
                         (int)(srcIp>>24)&0xFF, (int)(srcIp>>16)&0xFF,
                         (int)(srcIp>>8)&0xFF, (int)(srcIp)&0xFF,
                         (int)(dstIp>>24)&0xFF, (int)(dstIp>>16)&0xFF,
                         (int)(dstIp>>8)&0xFF, (int)(dstIp)&0xFF);
            }
        }
    }

    int ret = pfn_send(ctx->pcap, buf, total);
    if (ret != 0) {
        LWIP_DBG("[发送失败] %c%c%d 发送数据包失败 ret=%d\n",
                 netif->name[0], netif->name[1], netif->num, ret);
        return ERR_IF;
    }
    return ERR_OK;
}

/* Direct destinations are selected by lwIP from each netif's address/mask.
 * Non-direct destinations use the adapter selected by Windows' best default
 * route. If that adapter has not leased yet, temporarily use the first
 * leased adapter with a gateway and replace it when the authoritative
 * adapter is ready. */
static void npcap_status_callback(struct netif* netif) {
    if (!netif || !netif_is_up(netif) ||
        (netif->flags & NETIF_FLAG_LOOPBACK) ||
        ip_addr_isany(&netif->ip_addr)) return;

    char ip[20], mask[20], gw[20];
    npcap_ctx_t* ctx = (npcap_ctx_t*)netif->state;
    bool setDefault = ctx && ctx->isDefaultRoute;
    bool currentUsable = netif_default &&
                         !(netif_default->flags & NETIF_FLAG_LOOPBACK) &&
                         netif_is_up(netif_default) &&
                         netif_is_link_up(netif_default) &&
                         !ip_addr_isany(&netif_default->ip_addr) &&
                         !ip_addr_isany(&netif_default->gw);
    if (!setDefault && !ip_addr_isany(&netif->gw) && !currentUsable) {
        setDefault = true;
    }
    if (setDefault && netif_default != netif) {
        netif_set_default(netif);
        XDeviceNetworkLwip_setDefaultNetif(netif);
    }
    LWIP_DBG("[DHCP] lease %c%c%d ifIndex=%lu default=%d IP=%s MASK=%s GW=%s DEV=%s\n",
             netif->name[0], netif->name[1], netif->num,
             (unsigned long)(ctx ? ctx->ifIndex : 0),
             (int)(setDefault || netif_default == netif),
             ip4addr_ntoa_r(ip_2_ip4(&netif->ip_addr), ip, sizeof(ip)),
             ip4addr_ntoa_r(ip_2_ip4(&netif->netmask), mask, sizeof(mask)),
             ip4addr_ntoa_r(ip_2_ip4(&netif->gw), gw, sizeof(gw)),
             ctx ? ctx->winDesc : "?");
}

/* 广播 MAC 地址常量 */
static const uint8_t g_broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* pcap_dispatch 回调：参考 lwIP 官方 pcapif.c 的 pcapif_input 实现
 *
 * 混杂模式下 Npcap 会收到所有包（包括发给 Windows 协议栈的包和 lwIP 自己发出的包）。
 * 此回调在 pbuf 分配前做轻量级过滤：
 *   1. 发送回环过滤：源 MAC == lwIP MAC → 丢弃（官方 pcaipf_is_tx_packet 简化版）
 *   2. 目标 MAC 过滤：只保留匹配 lwIP MAC 或广播的包（官方 pcapif_low_level_input 中的 MAC 过滤）
 *   3. VLAN 标签剥离（企业网络需要，官方无此功能）
 *   4. pbuf 分配 + netif->input
 *
 * 这两个过滤步骤与官方完全一致，确保在混杂模式下只处理 lwIP 关心的包。
 */
static void pcapif_input_callback(u_char* user, const struct pcap_pkthdr* hdr, const u_char* data) {
    npcap_ctx_t* ctx = (npcap_ctx_t*)user;
    if (!ctx || !hdr || !data || hdr->caplen < 14 || hdr->len > hdr->caplen ||
        hdr->len > 2048) return;
    struct netif* netif = ctx->netif;
    uint16_t pktLen = (uint16_t)hdr->len;

    if (!netif) return;

    /* 步骤1：发送回环过滤 — 如果源 MAC 等于 lwIP 的 MAC，说明是自己发出的包被 Npcap 回环收回来了
     * 参考官方 pcaipf_is_tx_packet (非 PCAPIF_RECEIVE_PROMISCUOUS 模式)：
     *   const struct eth_addr *src = (const struct eth_addr *)packet + 1;
     *   if (!memcmp(src, netif->hwaddr, ETH_HWADDR_LEN)) return 1;
     */
    if (!memcmp(data + 6, netif->hwaddr, 6)) {
        return;  /* 自己发出的包，丢弃 */
    }

    /* 步骤2：目标 MAC 过滤 — 只保留目标 MAC 匹配 lwIP MAC 或广播的包
     * 参考官方 pcapif_low_level_input 中的 MAC 过滤（非 PCAPIF_RECEIVE_PROMISCUOUS 模式）：
     *   if (memcmp(dest, &netif->hwaddr, ETH_HWADDR_LEN) && memcmp(dest, bcast, 6)) return NULL;
     */
    if (memcmp(data, netif->hwaddr, 6) && memcmp(data, g_broadcastMac, 6)) {
        return;  /* 不是发给 lwIP 也不是广播，丢弃 */
    }

    /* 步骤3：VLAN 标签剥离（802.1Q）
     * 保留企业网络 VLAN 支持，官方 pcapif.c 无此功能 */
    const u_char* pktData = data;
    uint8_t vlanStripped[2048];
    if (pktLen >= 18 && data[12] == 0x81 && data[13] == 0x00) {
        uint16_t realLen = (uint16_t)(pktLen - 4);
        if (realLen <= sizeof(vlanStripped)) {
            memcpy(vlanStripped, data, 12);
            memcpy(vlanStripped + 12, data + 16, realLen - 12);
            pktData = vlanStripped;
            pktLen = realLen;
        }
    }

    /* 步骤4：分配 pbuf 并喂给 lwIP 协议栈
     * 使用 PBUF_RAW + PBUF_RAM（需要拷贝，因为 VLAN 剥离修改了数据） */
    struct pbuf* p = pbuf_alloc(PBUF_RAW, pktLen, PBUF_RAM);
    if (p) {
        memcpy(p->payload, pktData, pktLen);
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}

/* 轮询所有 Npcap 网卡接收数据包，喂给 lwIP 协议栈
 *
 * 参考 lwIP 官方 pcapif.c 的 pcapif_poll 实现：
 *   pcap_dispatch(pa->adapter, -1, pcapif_input, (u_char*)pa);
 *   -1 表示处理当前 Npcap 缓冲区中所有可用包（非阻塞，处理完就返回）
 */
void XDeviceNetworkLwip_pollPcap(void) {
    for (int i = 0; i < g_npcapCtxCount; i++) {
        npcap_ctx_t* ctx = &g_npcapCtxs[i];
        if (!ctx->pcap || !ctx->netif) continue;

        if (pfn_dispatch) {
            /* 官方方式：pcap_dispatch(-1, callback, user) 批量处理所有可用包
             * 回调中完成 MAC 过滤 + 发送回环过滤 + VLAN 剥离 + pbuf 分配 */
            pfn_dispatch(ctx->pcap, -1, pcapif_input_callback, (u_char*)ctx);
        } else if (pfn_next) {
            /* fallback: pcap_dispatch 不可用（极旧的 WinPcap），使用 pcap_next_ex 逐包读取 */
            struct pcap_pkthdr* hdr = NULL;
            const u_char* data = NULL;
            int ret = pfn_next(ctx->pcap, &hdr, &data);
            if (ret == 1 && hdr && data && hdr->len > 0) {
                pcapif_input_callback((u_char*)ctx, hdr, data);
            }
        }
    }

}
/* 回环网卡 IPv4 输出包装函数 - 适配 netif->output 签名 (3 参数)
 * netif_loop_output 只有 2 参数，而 netif_output_fn 需要 3 参数
 * netif_loop_output_ipv4 是 netif.c 的 static 函数，无法直接使用 */
#if LWIP_IPV4
static err_t loopback_output_ipv4(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr) {
    LWIP_UNUSED_ARG(ipaddr);
    return netif_loop_output(netif, p);
}
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
/* 回环网卡 IPv6 输出包装函数 - 适配 netif->output_ip6 签名 */
static err_t loopback_output_ipv6(struct netif* netif, struct pbuf* p, const ip6_addr_t* ipaddr) {
    LWIP_UNUSED_ARG(ipaddr);
    return netif_loop_output(netif, p);
}
#endif /* LWIP_IPV6 */

/* 回环网卡初始化回调 - 由 netif_add 调用，专用于 lo0 接口
 * 参考 lwIP 内置的 netif_loopif_init，设置 output 为 netif_loop_output_ipv4
 * 不使用 Npcap，直接通过 lwIP 内部回环机制处理 127.0.0.1 流量 */
static err_t loopback_init(struct netif* netif) {
    LWIP_ASSERT("loopback_init: invalid netif\n", netif != NULL);
    netif->name[0] = 'l';
    netif->name[1] = 'o';
#if LWIP_IPV4
    netif->output = loopback_output_ipv4;
#endif
#if LWIP_IPV6
    netif->output_ip6 = loopback_output_ipv6;
#endif
    NETIF_SET_CHECKSUM_CTRL(netif, NETIF_CHECKSUM_DISABLE_ALL);
    netif->flags |= NETIF_FLAG_LOOPBACK;
    LWIP_DBG("[回环] lo0 初始化完成\n");
    return ERR_OK;
}
/* 平台初始化 - 创建回环网卡 + Npcap 虚拟网卡 */
struct netif* XDeviceNetworkLwip_platform_init(void) {
    LWIP_DBG("[平台初始化] 开始...\n");
    memset(g_npcapCtxs, 0, sizeof(g_npcapCtxs));
    memset(g_rxCount, 0, sizeof(g_rxCount));
    memset(g_txCount, 0, sizeof(g_txCount));
    g_npcapCtxCount = 0;
    g_dhcpStarted = false;
    XDeviceNetworkLwip_setDefaultNetif(NULL);

    /* 第一步：创建回环网卡 lo0 (127.0.0.1) */
    ip4_addr_t loopIp, loopMask, loopGw;
    IP4_ADDR(&loopIp, 127, 0, 0, 1);
    IP4_ADDR(&loopMask, 255, 0, 0, 0);
    IP4_ADDR(&loopGw, 127, 0, 0, 1);
    memset(&g_loopNetif, 0, sizeof(g_loopNetif));
    struct netif* lnif = netif_add(&g_loopNetif, &loopIp, &loopMask, &loopGw, NULL, loopback_init, XNETIF_INPUT_FN);
    if (lnif) {
        netif_set_up(&g_loopNetif);
        LWIP_DBG("[平台初始化] 回环网卡 lo0 创建成功\n");
    }

    /* Enumerate every connected Windows adapter and create one matching
     * Npcap-backed lwIP netif for each. GetBestRoute is used only to mark the
     * external default route; lwIP still performs subnet-specific routing and
     * each created interface starts its own DHCP client. */
    if (load_npcap()) {
        ULONG bufLen = 0;
        DWORD addressesStatus = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS,
                                                      NULL, NULL, &bufLen);
        if (addressesStatus != ERROR_BUFFER_OVERFLOW || bufLen == 0) {
            LWIP_DBG("[Windows网卡] GetAdaptersAddresses(size) failed: %lu\n",
                     (unsigned long)addressesStatus);
            return lnif;
        }
        PIP_ADAPTER_ADDRESSES aa = (PIP_ADAPTER_ADDRESSES)XMalloc_System(bufLen);
        if (aa) {
            addressesStatus = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS,
                                                    NULL, aa, &bufLen);
            PIP_ADAPTER_ADDRESSES defaultAdapter = addressesStatus == NO_ERROR
                ? find_default_ipv4_adapter(aa) : NULL;
            if (addressesStatus != NO_ERROR) {
                LWIP_DBG("[Windows网卡] GetAdaptersAddresses failed: %lu\n",
                         (unsigned long)addressesStatus);
                XFree_System(aa);
                return lnif;
            }

            pcap_if_t* alldevs = NULL;
            char errbuf[256] = {0};
            if (pfn_findall(&alldevs, errbuf) == 0 && alldevs) {
                LWIP_DBG("[Npcap] enumerating connected adapters, default ifIndex=%lu\n",
                         (unsigned long)(defaultAdapter ? defaultAdapter->IfIndex : 0));

                for (pcap_if_t* d = alldevs; d && g_npcapCtxCount < NPCAP_MAX_ADAPTERS; d = d->next) {
                    if (!d->name) continue;
                    const char* desc = d->description ? d->description : "(无描述)";
                    PIP_ADAPTER_ADDRESSES adapter = find_adapter_for_npcap(aa, d->name);
                    if (!adapter) continue;
                    bool isDefaultRoute = defaultAdapter &&
                                          adapter->IfIndex == defaultAdapter->IfIndex;
                    LWIP_DBG("[Npcap] connected %s -> %s ifIndex=%lu default=%d\n",
                             d->name, desc, (unsigned long)adapter->IfIndex,
                             (int)isDefaultRoute);

                    /* 打开 Npcap 设备 */
                    /* 混杂模式：接收所有包，在用户态 pollPcap 中进行软 MAC 过滤
                     * 原因：非混杂模式下网卡硬件只接收匹配网卡硬件MAC的包，而Npcap
                     * 没有提供API注册额外的MAC地址。lwIP的随机MAC无法被网卡识别，
                     * 导致DHCP OFFER/ACK、ARP回复等都无法收到。 */
                    pcap_t pcap = pfn_open(d->name, 65536, PCAP_OPENFLAG_PROMISCUOUS,
                                           NPCAP_READ_TIMEOUT_MS, errbuf);
                    if (!pcap) {
                        LWIP_DBG("[Npcap] 打开设备失败: %s\n", errbuf);
                        continue;
                    }

                    /* 创建 Npcap 上下文，关联到 lwIP 网络接口 */
                    npcap_ctx_t* ctx = &g_npcapCtxs[g_npcapCtxCount];
                    memset(ctx, 0, sizeof(*ctx));
                    ctx->pcap = pcap;
                    snprintf(ctx->devName, sizeof(ctx->devName), "%s", d->name);
                    snprintf(ctx->winDesc, sizeof(ctx->winDesc), "%s", desc);
                    ctx->ifIndex = adapter->IfIndex;
                    ctx->isDefaultRoute = isDefaultRoute;

#ifdef LWIP_USE_STATIC_IP
                    /* 静态 IP 配置模式 */
                    ip4_addr_t sip, smask, sgw;
                    IP4_ADDR(&sip, LWIP_STATIC_IP);
                    IP4_ADDR(&smask, LWIP_STATIC_MASK);
                    IP4_ADDR(&sgw, LWIP_STATIC_GW);
                    struct netif* n = (struct netif*)XMalloc_System(sizeof(struct netif));
                    if (n) {
                        memset(n, 0, sizeof(struct netif));
                        struct netif* result = netif_add(n, &sip, &smask, &sgw, ctx, npcap_init, XNETIF_INPUT_FN);
                        if (result) {
                            ctx->netif = result;
                            netif_set_up(result);
                            g_npcapCtxCount++;
                            LWIP_DBG("[网卡] %c%c%d 静态IP模式创建成功\n", result->name[0], result->name[1], result->num);
                            if (!netif_default) {
                                netif_set_default(result);
                                XDeviceNetworkLwip_setDefaultNetif(result);
                            }
                        } else {
                            LWIP_DBG("[网卡] netif_add失败(静态IP模式), 释放预分配的netif\n");
                            XFree_System(n);
                            pfn_close(pcap);
                            memset(ctx, 0, sizeof(*ctx));
                        }
                    } else {
                        LWIP_DBG("[网卡] XMalloc_System分配netif失败(静态IP模式)\n");
                        pfn_close(pcap);
                        memset(ctx, 0, sizeof(*ctx));
                    }
                    /* DHCP 客户端模式（已在上面设置 useDhcp=true） */
                    LWIP_DBG("[网卡] DHCP客户端模式\n");
#endif /* LWIP_USE_STATIC_IP */

                    /* DHCP starts from 0.0.0.0 and must receive a distinct
                     * lease.  Never reuse the Windows host address. */
                    {
                        ip4_addr_t iip, imask, igw;
                        IP4_ADDR(&iip, 0, 0, 0, 0);
                        IP4_ADDR(&imask, 0, 0, 0, 0);
                        IP4_ADDR(&igw, 0, 0, 0, 0);

                        struct netif* n = (struct netif*)XMalloc_System(sizeof(struct netif));
                        if (n) {
                            memset(n, 0, sizeof(struct netif));
                            struct netif* result = netif_add(n, &iip, &imask, &igw, ctx, npcap_init, XNETIF_INPUT_FN);
                            if (result) {
                                ctx->netif = result;
                                netif_set_up(result);
                                netif_set_link_up(result);
                                netif_set_status_callback(result, npcap_status_callback);
                                g_npcapCtxCount++;
                                LWIP_DBG("[网卡] netif_add=%p ifIndex=%lu, awaiting DHCP lease\n",
                                         (void*)result, (unsigned long)ctx->ifIndex);
                            } else {
                                LWIP_DBG("[网卡] netif_add失败, 释放预分配的netif\n");
                                XFree_System(n);
                                pfn_close(pcap);
                                memset(ctx, 0, sizeof(*ctx));
                            }
                        } else {
                            LWIP_DBG("[网卡] XMalloc_System分配netif失败\n");
                            pfn_close(pcap);
                            memset(ctx, 0, sizeof(*ctx));
                        }
                    }
                } /* for each Npcap device */

                /* Every connected netif gets its own DHCP client and therefore
                 * its own lease/MAC identity. */
#ifdef LWIP_USE_DHCP
                if (!g_dhcpStarted && g_npcapCtxCount > 0) {
                    g_dhcpStarted = true;
                    int dhcpCount = 0;
                    for (int i = 0; i < g_npcapCtxCount; ++i) {
                        npcap_ctx_t* ctx = &g_npcapCtxs[i];
                        struct netif* n = ctx->netif;
                        if (!n) continue;
                        err_t dhcpErr = dhcp_start(n);
                        ctx->dhcpEnabled = (dhcpErr == ERR_OK);
                        if (ctx->dhcpEnabled) ++dhcpCount;
                        LWIP_DBG("[DHCP] start %c%c%d ifIndex=%lu default=%d result=%d (%s)\n",
                                 n->name[0], n->name[1], n->num,
                                 (unsigned long)ctx->ifIndex,
                                 (int)ctx->isDefaultRoute, (int)dhcpErr,
                                 ctx->winDesc);
                    }
                    LWIP_DBG("[DHCP] started %d/%d clients\n", dhcpCount, g_npcapCtxCount);
                }
#endif

                pfn_freeall(alldevs);
            } else {
                LWIP_DBG("[Npcap] 枚举网络适配器失败: %s\n", errbuf);
            }
            XFree_System(aa);
        }
        LWIP_DBG("[平台初始化] 共创建 %d 个Npcap虚拟网卡\n", g_npcapCtxCount);
    } else {
        LWIP_DBG("[平台初始化] Npcap 加载失败，无法创建虚拟网卡（请安装 Npcap: https://npcap.com/）\n");
    }

    LWIP_DBG("[平台初始化] 完成\n");
    /* The external interface must be returned so callers cannot promote
     * loopback to the default route after this function returns. */
    if (g_npcapCtxCount > 0 && g_npcapCtxs[0].netif) return g_npcapCtxs[0].netif;
    if (lnif) return lnif;
    return NULL;
}

/* 平台清理 - 关闭所有 Npcap 网卡并释放资源 */
void XDeviceNetworkLwip_platform_deinit(void) {
    LWIP_DBG("[平台清理] 开始关闭 %d 个Npcap虚拟网卡...\n", g_npcapCtxCount);
    for (int i = 0; i < g_npcapCtxCount; i++) {
        npcap_ctx_t* ctx = &g_npcapCtxs[i];
        if (ctx->netif) {
            if (ctx->dhcpEnabled) dhcp_stop(ctx->netif);
            netif_set_down(ctx->netif);
            netif_remove(ctx->netif);
            XFree_System(ctx->netif);
            ctx->netif = NULL;
        }
        if (ctx->pcap && pfn_close) pfn_close(ctx->pcap);
        ctx->pcap = NULL;
    }
    if (g_loopNetif.input) {
        netif_set_down(&g_loopNetif);
        netif_remove(&g_loopNetif);
        memset(&g_loopNetif, 0, sizeof(g_loopNetif));
    }
    g_npcapCtxCount = 0;
    g_dhcpStarted = false;
    memset(g_npcapCtxs, 0, sizeof(g_npcapCtxs));
    memset(g_rxCount, 0, sizeof(g_rxCount));
    memset(g_txCount, 0, sizeof(g_txCount));
    XDeviceNetworkLwip_setDefaultNetif(NULL);
    unload_npcap();
    LWIP_DBG("[平台清理] 完成\n");
}


/* ================================================================
 * 默认 netif 管理
 * ================================================================ */

bool XDeviceNetwork_socketConnectLocal(XFd fd, const XString* endpoint,
                                 XDeviceNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XDeviceNetworkSocketType sockType)
{
    (void)fd;
    (void)endpoint;
    (void)streamType;
    (void)timeoutMs;
    (void)sockType;
    return false;
}

static struct netif* g_defaultLwipNetif = NULL;

struct netif* XDeviceNetworkLwip_defaultNetif(void)
{
    return g_defaultLwipNetif;
}

void XDeviceNetworkLwip_setDefaultNetif(struct netif* netif)
{
    g_defaultLwipNetif = netif;
}

#endif /* XNETWORK_USE_LWIP */
#endif /* _WIN32 */
