/**
 * @file XNetwork_lwip_win32.c
 * @brief lwIP Windows 平台虚拟网卡实现 - 基于 Npcap 虚拟网卡
 *
 * 基于 STM32F407+FreeRTOS 成功移植经验，适配 Windows + Npcap 平台。
 * 参考 lwIP 官方 contrib/ports/win32/pcapif.c 实现。
 *
 * 核心功能：
 *   1. 动态加载 Npcap DLL（wpcap.dll）
 *   2. 创建回环网卡 lo0 (127.0.0.1)
 *   3. 枚举物理网卡，为每个创建 lwIP netif
 *   4. 对可发送数据包的网卡启用 DHCP 客户端
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

#include "XNetwork_lwip_platform.h"
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

#define NPCAP_MAX_ADAPTERS  8
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
    return pfn_open && pfn_findall && pfn_next && pfn_send;
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
    bool canSend;                /* 是否可以发送数据包（用于检测发送失败的网卡） */
    uint32_t winIp;              /* Windows 协议栈 IP（用于 DHCP 失败 fallback） */
    uint32_t winMask;            /* Windows 协议栈掩码 */
    uint32_t winGw;              /* Windows 协议栈网关 */
    uint32_t dhcpStartTime;      /* DHCP 启动时间（用于超时检测） */
    uint8_t winMac[6];           /* Windows 真实 MAC 地址（用于 BPF 排除过滤） */
    bool hasWinMac;              /* winMac 是否有效 */
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

/* 测试 Npcap 适配器是否可以发送数据包
 * 发送一个空的广播以太网帧作为测试，如果发送失败则标记此适配器不可用
 * 某些虚拟适配器（如 Wi-Fi Direct Virtual Adapter）虽然能被 Npcap 打开，
 * 但 pcap_sendpacket 会返回 -1，无法实际发送数据包 */
static bool test_npcap_send(pcap_t pcap) {
    if (!pcap || !pfn_send) return false;
    /* 构造一个最小的广播以太网帧作为测试（14字节以太网头 + 填充） */
    uint8_t testFrame[64];
    memset(testFrame, 0, sizeof(testFrame));
    /* 目标MAC: 广播地址 FF:FF:FF:FF:FF:FF */
    memset(testFrame, 0xFF, 6);
    /* 源MAC: 本地管理单播地址 02:00:00:00:00:01 */
    testFrame[6] = 0x02; testFrame[7] = 0x00; testFrame[8] = 0x00;
    testFrame[9] = 0x00; testFrame[10] = 0x00; testFrame[11] = 0x01;
    /* EtherType: 0x0800 (IPv4) */
    testFrame[12] = 0x08; testFrame[13] = 0x00;
    /* 填充剩余部分（实际IPv4包需要至少46字节数据，这里填充0） */
    int ret = pfn_send(pcap, testFrame, sizeof(testFrame));
    return (ret == 0);  /* 0 表示发送成功 */
}

/* 过滤虚拟适配器 - 跳过 WAN Miniport、Bluetooth 等非物理网卡 */
static bool is_physical_adapter(const char* desc) {
    if (!desc) return false;
    if (strstr(desc, "WAN Miniport")) return false;
    if (strstr(desc, "NdisWan")) return false;
    if (strstr(desc, "Bluetooth")) return false;
    if (strstr(desc, "Network Monitor")) return false;
    if (strstr(desc, "PLCSIM")) return false;
    if (strstr(desc, "Wi-Fi Direct")) return false;   /* Wi-Fi Direct 虚拟适配器无法发送数据包 */
    if (strstr(desc, "loopback")) return false;        /* 回环捕获适配器不适用于 DHCP */
    if (strstr(desc, "Loopback")) return false;
    return true;
}

/* 查找 Windows 网卡对应的 IPv4 地址信息（IP、掩码、网关）
 * 注意：Windows API 返回的 FriendlyName/Description 是 wchar_t* 类型，
 * 需要通过 WideCharToMultiByte 转换为 UTF-8 字符串后再与 Npcap 描述进行匹配 */
static bool find_windows_ip_for_adapter(PIP_ADAPTER_ADDRESSES aa, const char* npcapDesc,
                                         uint32_t* ipOut, uint32_t* maskOut, uint32_t* gwOut,
                                         uint8_t* macOut) {
    if (!aa || !npcapDesc || !ipOut || !maskOut || !gwOut) return false;

    PIP_ADAPTER_ADDRESSES a = aa;
    while (a) {
        bool matched = false;
        char winName[256] = {0};

        /* 方法1：通过 FriendlyName 匹配 Npcap 描述 */
        if (a->FriendlyName && a->FriendlyName[0]) {
            WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                winName, sizeof(winName) - 1, NULL, NULL);
            if (strstr(npcapDesc, winName) || strstr(winName, npcapDesc)) {
                matched = true;
            }
        }

        /* 方法2：如果 FriendlyName 匹配失败，尝试用 Description 匹配 */
        if (!matched && a->Description && a->Description[0]) {
            WideCharToMultiByte(CP_UTF8, 0, a->Description, -1,
                                winName, sizeof(winName) - 1, NULL, NULL);
            if (strstr(npcapDesc, winName) || strstr(winName, npcapDesc)) {
                matched = true;
            }
        }

        if (matched) {
            /* 查找该适配器的第一个有效 IPv4 单播地址 */
            PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress;
            while (ua) {
                if (ua->Address.lpSockaddr &&
                    ua->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in* sa = (struct sockaddr_in*)ua->Address.lpSockaddr;
                    uint32_t ip = sa->sin_addr.s_addr;
                    if (ip != 0) {
                        uint32_t hip = ntohl(ip);
                        /* 跳过 0.0.0.0, 127.x.x.x (回环), 169.254.x.x (APIPA) */
                        if (hip != 0 && (hip >> 24) != 127 && (hip >> 16) != 0xA9FE) {
                            *ipOut = ip;
                            uint8_t pfx = ua->OnLinkPrefixLength;
                            uint32_t mask = (pfx >= 32) ? 0xFFFFFFFF :
                                            ((pfx == 0) ? 0 : (0xFFFFFFFF << (32 - pfx)));
                            *maskOut = htonl(mask);

                            /* 获取网关地址 */
                            PIP_ADAPTER_GATEWAY_ADDRESS ga = a->FirstGatewayAddress;
                            if (ga && ga->Address.lpSockaddr &&
                                ga->Address.lpSockaddr->sa_family == AF_INET) {
                                *gwOut = ((struct sockaddr_in*)ga->Address.lpSockaddr)->sin_addr.s_addr;
                            } else {
                                *gwOut = 0;
                            }

                            /* 获取 Windows 真实 MAC 地址（用于 BPF 排除过滤） */
                            if (macOut && a->PhysicalAddressLength == 6) {
                                memcpy(macOut, a->PhysicalAddress, 6);
                            }


                            uint32_t hgw = ntohl(*gwOut);
                            LWIP_DBG("[Windows网卡匹配] Npcap=\"%s\" -> Win=\"%s\" IP=%d.%d.%d.%d GW=%d.%d.%d.%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                                     npcapDesc, winName,
                                     (int)(hip>>24)&0xFF, (int)(hip>>16)&0xFF,
                                     (int)(hip>>8)&0xFF, (int)(hip)&0xFF,
                                     (int)(hgw>>24)&0xFF, (int)(hgw>>16)&0xFF,
                                     (int)(hgw>>8)&0xFF, (int)(hgw)&0xFF,
                                     macOut ? (int)macOut[0] : 0, macOut ? (int)macOut[1] : 0,
                                     macOut ? (int)macOut[2] : 0, macOut ? (int)macOut[3] : 0,
                                     macOut ? (int)macOut[4] : 0, macOut ? (int)macOut[5] : 0);
                            return true;
                        }
                    }
                }
                ua = ua->Next;
            }
            LWIP_DBG("[Windows网卡匹配] Npcap=\"%s\" -> Win=\"%s\" 匹配成功但没有有效IPv4地址\n", npcapDesc, winName);
        }
        a = a->Next;
    }
    return false;
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

    /* 混杂模式 + 用户态软 MAC 过滤：
     * Npcap 在 Mellanox 网卡上 BPF 不可靠，不使用 BPF。
     * XNetworkLwip_pollPcap 中只保留目标MAC匹配 lwIP MAC 或广播的包，
     * 丢弃所有其他单播包。 */

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

    /* 将 pbuf 链拼接成连续缓冲区 */
    uint8_t buf[2048];
    u16_t total = 0;
    struct pbuf* q = p;
    while (q && total < sizeof(buf)) {
        u16_t copy = (u16_t)((total + q->len > sizeof(buf)) ? sizeof(buf) - total : q->len);
        memcpy(buf + total, q->payload, copy);
        total += copy;
        q = q->next;
    }

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

/* 网卡状态回调 - DHCP 获取到有效 IP 后自动设置为默认路由，并切换 BPF 为 IP 过滤 */
static void npcap_status_callback(struct netif* netif) {
    if (!netif) return;
    if (netif_is_up(netif) && !ip_addr_isany(&netif->ip_addr)) {
        char ip_str[20], mask_str[20], gw_str[20];
        uint32_t hip = ntohl(ip4_addr_get_u32(ip_2_ip4(&netif->ip_addr)));
        /* 跳过 127.x.x.x (回环) 和 169.254.x.x (APIPA) */
        if (((hip >> 24) & 0xFF) != 127 && ((hip >> 16) & 0xFFFF) != 0xA9FE) {
            npcap_ctx_t* ctx = (npcap_ctx_t*)netif->state;

            /* 保持初始 BPF 过滤器 "not ether host <Windows MAC>"
             * 不在此处切换为 IP 过滤。
             *
             * 原因：Mellanox 网卡上 Npcap 的 "host <IP>" BPF 无法可靠匹配
             * ARP 回复包（ARP 回复有时到达、有时不到），导致 TCP 连接不稳定。
             *
             * "not ether host <WinMAC>" 排除策略始终有效，排除 Windows 流量后
             * 剩余流量极少（1-2%），lwIP 能正常处理。 */
            LWIP_DBG("[IF_STATUS] %c%c%d IP=%s MASK=%s GW=%s DEV=%s\n",
                     netif->name[0], netif->name[1], netif->num,
                     ip4addr_ntoa_r(ip_2_ip4(&netif->ip_addr), ip_str, sizeof(ip_str)),
                     ip4addr_ntoa_r(ip_2_ip4(&netif->netmask), mask_str, sizeof(mask_str)),
                     ip4addr_ntoa_r(ip_2_ip4(&netif->gw), gw_str, sizeof(gw_str)),
                     ctx ? ctx->winDesc : "?");

            /* 默认路由决策策略：递增优先级，更高优先级替换更低优先级
             * 0: 无IP/回环
             * 1: Windows静态IP(无网关)
             * 2: Windows静态IP(有网关)
             * 3: DHCP(无网关)
             * 4: DHCP(有网关) ← 最高优先级 */
            bool newIsDhcp = ctx && ctx->dhcpEnabled;
            bool curIsDhcp = netif_default && netif_default->state
                          && ((npcap_ctx_t*)netif_default->state)->dhcpEnabled;
            bool newHasGw = !ip_addr_isany(&netif->gw);
            bool curHasGw = netif_default && !ip_addr_isany(&netif_default->gw);
            /* 检查网关是否与网卡IP在同一子网（排除192.168.1.x之类的跨子网网关） */
            bool newGwInSubnet = newHasGw && ip_addr_netcmp(&netif->gw, &netif->ip_addr, netif_ip4_netmask(netif));
            bool curGwInSubnet = netif_default && curHasGw && ip_addr_netcmp(&netif_default->gw, &netif_default->ip_addr, netif_ip4_netmask(netif_default));

            /* 检测是否为虚拟网卡（VMware/VirtualBox等，不是真正的物理网卡） */
            bool newIsVirtual = ctx && ctx->winDesc &&
                (strstr(ctx->winDesc, "VMware") || strstr(ctx->winDesc, "VirtualBox"));
            bool curIsVirtual = false;
            if (netif_default && netif_default->state) {
                const char* curDesc = ((npcap_ctx_t*)netif_default->state)->winDesc;
                if (curDesc) curIsVirtual = (strstr(curDesc, "VMware") || strstr(curDesc, "VirtualBox"));
            }

            /* 优先级：物理网卡 > 虚拟网卡
             * 物理网卡: 100 = DHCP(有同子网网关)，90 = DHCP(其他)，80 = DHCP(无网关)
             *           50  = 静态IP(有同子网网关)，40 = 静态IP(其他)，30 = 静态IP(无网关)
             * 虚拟网卡(VMware等): 基础优先级 -20
             * 0 = 无IP */
            int curPrio = 0, newPrio = 0;
            if (netif_default) {
                if (curIsDhcp) curPrio = curHasGw ? (curGwInSubnet ? 100 : 90) : 80;
                else curPrio = curHasGw ? (curGwInSubnet ? 50 : 40) : 30;
                if (curIsVirtual) curPrio -= 20;
            }
            if (newIsDhcp) newPrio = newHasGw ? (newGwInSubnet ? 100 : 90) : 80;
            else newPrio = newHasGw ? (newGwInSubnet ? 50 : 40) : 30;
            if (newIsVirtual) newPrio -= 20;

            if (netif_default == NULL || newPrio > curPrio) {
                char defIp[20];
                if (netif_default) {
                    LWIP_DBG("[DEFAULT_ROUTE] %c%c%d (%s) prio=%d -> replace %c%c%d (%s) prio=%d\n",
                             netif->name[0], netif->name[1], netif->num, ip_str, newPrio,
                             netif_default->name[0], netif_default->name[1], netif_default->num,
                             ip4addr_ntoa_r(ip_2_ip4(&netif_default->ip_addr), defIp, sizeof(defIp)), curPrio);
                } else {
                    LWIP_DBG("[DEFAULT_ROUTE] %c%c%d (%s) prio=%d -> set as default\n",
                             netif->name[0], netif->name[1], netif->num, ip_str, newPrio);
                }
                netif_set_default(netif);
                XNetworkLwip_setDefaultNetif(netif);
            } else {
                char defIp[20];
                LWIP_DBG("[DEFAULT_ROUTE] %c%c%d (%s) prio=%d -> skipped, cur=%c%c%d (%s) prio=%d\n",
                         netif->name[0], netif->name[1], netif->num, ip_str, newPrio,
                         netif_default->name[0], netif_default->name[1], netif_default->num,
                         ip4addr_ntoa_r(ip_2_ip4(&netif_default->ip_addr), defIp, sizeof(defIp)), curPrio);
            }
        }
    }
}

/* DHCP 超时阈值（毫秒）：30秒内未获取到 DHCP IP，则 fallback 到 Windows IP */
#define DHCP_FALLBACK_TIMEOUT_MS 30000

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
    struct netif* netif = ctx->netif;
    uint16_t pktLen = (uint16_t)hdr->len;

    if (!netif || pktLen < 14) return;

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

/* DHCP 超时 fallback：DHCP 超时后将仍为 0.0.0.0 的网卡设置为 Windows IP */
static void check_dhcp_fallback(uint32_t now) {
    int i;
    for (i = 0; i < g_npcapCtxCount; i++) {
        npcap_ctx_t* ctx = &g_npcapCtxs[i];
        struct netif* n = ctx->netif;
        if (!n || !ctx->dhcpEnabled || !ctx->winIp) continue;

        if (ip_addr_isany(&n->ip_addr) && ctx->dhcpStartTime > 0 &&
            now - ctx->dhcpStartTime >= DHCP_FALLBACK_TIMEOUT_MS) {
            ip4_addr_t fallbackIp, fallbackMask, fallbackGw;
            ip4_addr_set_u32(&fallbackIp, ctx->winIp);
            ip4_addr_set_u32(&fallbackMask, ctx->winMask);
            ip4_addr_set_u32(&fallbackGw, ctx->winGw);
            netif_set_addr(n, &fallbackIp, &fallbackMask, &fallbackGw);
            char fbIp[20];
            LWIP_DBG("[DHCP] %c%c%d DHCP超时(%dms)，fallback到Windows IP=%s\n",
                     n->name[0], n->name[1], n->num,
                     DHCP_FALLBACK_TIMEOUT_MS,
                     ip4addr_ntoa_r(&fallbackIp, fbIp, sizeof(fbIp)));
            if (!netif_default) {
                netif_set_default(n);
                XNetworkLwip_setDefaultNetif(n);
            }
            ctx->dhcpStartTime = 0;
        }
    }
}

/* 轮询所有 Npcap 网卡接收数据包，喂给 lwIP 协议栈
 *
 * 参考 lwIP 官方 pcapif.c 的 pcapif_poll 实现：
 *   pcap_dispatch(pa->adapter, -1, pcapif_input, (u_char*)pa);
 *   -1 表示处理当前 Npcap 缓冲区中所有可用包（非阻塞，处理完就返回）
 */
void XNetworkLwip_pollPcap(void) {
    static uint32_t lastFallbackCheck = 0;
    uint32_t now = sys_now();

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

    /* DHCP 超时 fallback 检测：每隔1秒检查一次 */
    if (now - lastFallbackCheck >= 1000) {
        lastFallbackCheck = now;
        check_dhcp_fallback(now);
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
struct netif* XNetworkLwip_platform_init(void) {
    LWIP_DBG("[平台初始化] 开始...\n");

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

    /* 第二步：加载 Npcap 并枚举所有物理网卡，为每个创建 Npcap 虚拟网卡 */
    if (load_npcap()) {
        /* 获取 Windows 网卡信息（用于匹配 IP 地址） */
        ULONG bufLen = 0;
        GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &bufLen);
        PIP_ADAPTER_ADDRESSES aa = (PIP_ADAPTER_ADDRESSES)XMalloc_System(bufLen);
        if (aa) {
            GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, aa, &bufLen);

            /* 枚举所有 Npcap 设备，过滤非物理网卡后创建虚拟网卡 */
            pcap_if_t* alldevs = NULL;
            char errbuf[256] = {0};
            if (pfn_findall(&alldevs, errbuf) == 0 && alldevs) {
                LWIP_DBG("[Npcap] 开始枚举网络适配器...\n");

                for (pcap_if_t* d = alldevs; d && g_npcapCtxCount < NPCAP_MAX_ADAPTERS; d = d->next) {
                    if (!d->name) continue;
                    const char* desc = d->description ? d->description : "(无描述)";

                    /* 过滤掉虚拟适配器（WAN Miniport 等） */
                    if (!is_physical_adapter(desc)) {
                        LWIP_DBG("[Npcap] 跳过虚拟适配器: %s -> %s\n", d->name, desc);
                        continue;
                    }

                    /* 动态 IP 配置：尝试从 Windows 获取对应网卡的 IP 信息和 MAC 地址 */
                    bool hasIp = false;
                    uint32_t ip = 0, mask = 0, gw = 0;
                    uint8_t winMacBuf[6] = {0};
                    hasIp = find_windows_ip_for_adapter(aa, desc, &ip, &mask, &gw, winMacBuf);

                    /* 多网卡自动路由：为所有有有效 IP 的物理网卡创建虚拟网卡
                     * lwIP 的 ip4_route() 会根据目标 IP 自动选择出站网卡：
                     *   - 目标在某个网卡子网内 -> 用该网卡直接发送（直接路由）
                     *   - 目标不在任何子网 -> 用 netif_default（默认网关）发送
                     * 默认网卡由 npcap_status_callback 按优先级自动选择
                     *   （DHCP+有同子网网关的物理网卡优先级最高=100） */
                    if (!hasIp) {
                        LWIP_DBG("[Npcap] 跳过无IP网卡: %s -> %s\n", d->name, desc);
                        continue;
                    }
                    {
                        uint32_t hip = ntohl(ip);
                        /* 跳过回环(127.x.x.x)和APIPA(169.254.x.x)地址 */
                        if (((hip >> 24) & 0xFF) == 127 || ((hip >> 16) & 0xFFFF) == 0xA9FE) {
                            LWIP_DBG("[Npcap] 跳过无效IP网卡(%d.%d.%d.%d): %s -> %s\n",
                                     (int)(hip>>24)&0xFF, (int)(hip>>16)&0xFF,
                                     (int)(hip>>8)&0xFF, (int)hip&0xFF,
                                     d->name, desc);
                            continue;
                        }
                    }
                    LWIP_DBG("[Npcap] 创建虚拟网卡(%d): %s -> %s\n", g_npcapCtxCount + 1, d->name, desc);

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

                    /* 保存 Windows IP 用于 DHCP 失败时 fallback */
                    if (hasIp) {
                        ctx->winIp = ip;
                        ctx->winMask = mask;
                        ctx->winGw = gw;
                        /* 保存 Windows 真实 MAC 用于 BPF 排除过滤 */
                        if (winMacBuf[0] != 0 || winMacBuf[1] != 0 || winMacBuf[2] != 0 ||
                            winMacBuf[3] != 0 || winMacBuf[4] != 0 || winMacBuf[5] != 0) {
                            memcpy(ctx->winMac, winMacBuf, 6);
                            ctx->hasWinMac = true;
                        }
                    } else {
                        ctx->winIp = 0;
                        ctx->winMask = 0;
                        ctx->winGw = 0;
                    }

                    uint8_t mac[6];
                    gen_lwip_mac(mac);

                    bool useDhcp = true;  /* 对目标网卡启用 DHCP 获取独立 IP */

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
                                XNetworkLwip_setDefaultNetif(result);
                            }
                        } else {
                            LWIP_DBG("[网卡] netif_add失败(静态IP模式), 释放预分配的netif\n");
                            XFree_System(n);
                        }
                    } else {
                        LWIP_DBG("[网卡] XMalloc_System分配netif失败(静态IP模式)\n");
                    }
                    /* DHCP 客户端模式（已在上面设置 useDhcp=true） */
                    LWIP_DBG("[网卡] DHCP客户端模式\n");
#endif /* LWIP_USE_STATIC_IP */

                    /* 创建 lwIP 网络接口
                     * DHCP 模式：初始 IP 设为 0.0.0.0，等待 DHCP 动态分配
                     * DHCP 失败后 fallback 到 Windows 协议栈 IP */
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
                                LWIP_DBG("[网卡] netif_add=%p hasIp=%d dhcp=%d\n",
                                         (void*)result, (int)hasIp, (int)useDhcp);

                                if (hasIp && !useDhcp) {
                                    if (!netif_default) {
                                        netif_set_default(result);
                                        XNetworkLwip_setDefaultNetif(result);
                                        char is2[20];
                                        LWIP_DBG("[网卡] 使用已有IP设为默认路由: %s\n",
                                                 ip4addr_ntoa_r(ip_2_ip4(&result->ip_addr), is2, sizeof(is2)));
                                    }
                                }
                            } else {
                                LWIP_DBG("[网卡] netif_add失败, 释放预分配的netif\n");
                                XFree_System(n);
                            }
                        } else {
                            LWIP_DBG("[网卡] XMalloc_System分配netif失败\n");
                        }
                    }
                } /* for each Npcap device */

                /* 第三步：启动 DHCP 客户端（只对能发送数据包的网卡启动 DHCP，最多4个） */
#ifdef LWIP_USE_DHCP
                if (!g_dhcpStarted && g_npcapCtxCount > 0) {
                    g_dhcpStarted = true;
                    LWIP_DBG("[DHCP] 检测到 %d 个虚拟网卡，开始检测发送能力...\n", g_npcapCtxCount);

                    int dhcpCount = 0;
                    for (int i = 0; i < g_npcapCtxCount && dhcpCount < 4; i++) {
                        npcap_ctx_t* ctx = &g_npcapCtxs[i];
                        struct netif* n = ctx->netif;
                        if (!n) continue;
 /* 测试此网卡是否可以发送数据包 */
                        ctx->canSend = test_npcap_send(ctx->pcap);
                        if (!ctx->canSend) {
                            LWIP_DBG("[DHCP] %c%c%d (%s) 发送测试失败，跳过DHCP\n",
                                     n->name[0], n->name[1], n->num, ctx->winDesc);
                            continue;
                        }

                        ctx->dhcpEnabled = true;
                        ctx->dhcpStartTime = sys_now();  /* 记录 DHCP 启动时间，用于超时 fallback */
                        LWIP_DBG("[DHCP] 网卡 %c%c%d (%s) 启动DHCP: netif=%p up=%d link=%d mtu=%d startTime=%u\n",
                                 n->name[0], n->name[1], n->num,
                                 ctx->winDesc, (void*)n,
                                 netif_is_up(n), netif_is_link_up(n), n->mtu,
                                 (unsigned int)ctx->dhcpStartTime);
                        err_t dhcp_err = dhcp_start(n);
                        LWIP_DBG("[DHCP] %c%c%d dhcp_start返回=%d (0=OK)\n",
                                 n->name[0], n->name[1], n->num, (int)dhcp_err);
                        dhcpCount++;
                    }
                    LWIP_DBG("[DHCP] 共启动 %d 个DHCP客户端\n", dhcpCount);
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
    /* 返回默认网卡：优先回环接口，否则返回第一个 Npcap 虚拟网卡 */
    if (lnif) return lnif;
    if (g_npcapCtxCount > 0 && g_npcapCtxs[0].netif) return g_npcapCtxs[0].netif;
    return NULL;
}

/* 平台清理 - 关闭所有 Npcap 网卡并释放资源 */
void XNetworkLwip_platform_deinit(void) {
    LWIP_DBG("[平台清理] 开始关闭 %d 个Npcap虚拟网卡...\n", g_npcapCtxCount);
    for (int i = 0; i < g_npcapCtxCount; i++) {
        if (g_npcapCtxs[i].pcap && pfn_close) pfn_close(g_npcapCtxs[i].pcap);
    }
    g_npcapCtxCount = 0;
    g_dhcpStarted = false;
    unload_npcap();
    LWIP_DBG("[平台清理] 完成\n");
}

#endif /* XNETWORK_USE_LWIP */
#endif /* _WIN32 */
