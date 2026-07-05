/**
 * @file XNetwork_lwip_win32.c
 * @brief lwIP Windows 平台网卡驱动 + netif 注册 (loopback + Npcap)
 *
 * ============ 参照 XFileSystem_Fatfs_diskioWin32.c 模式 ============
 * 仅在 _WIN32 && XNETWORK_USE_LWIP 时编译
 *
 * 提供:
 * - loopback 网卡 (127.0.0.1) 用于本地测试
 * - Npcap 物理网卡 (可选) 用于真实网络通信
 * - lwIP 驱动线程 (sys_check_timeouts + 轮询收包)
 */

#ifdef _WIN32

#include "XNetwork_config.h"

#if defined(XNETWORK_USE_LWIP)

#include "XNetwork_lwip_platform.h"
#include "XMemory.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMutex.h"
#include "XVarList.h"

#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>

/* ================================================================ */
/*  配置                                                              */
/* ================================================================ */

/** Npcap 最大适配器数 */
#define NPCAP_MAX_ADAPTERS  8

/* ================================================================ */
/*  Npcap 动态加载                                                      */
/* ================================================================ */

/* 最小 pcap 结构体/类型声明 */
typedef void* pcap_t;
typedef unsigned int bpf_u_int32;

/* pcap/pcap-stdinc.h 最小声明（必须在函数指针 typedef 之前） */
typedef struct pcap_if pcap_if_t;
struct pcap_if {
    struct pcap_if *next;
    char *name;
    char *description;
    void *addresses;
    bpf_u_int32 flags;
};

struct pcap_pkthdr {
    struct timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

#ifndef PCAP_OPENFLAG_PROMISCUOUS
#define PCAP_OPENFLAG_PROMISCUOUS 1
#endif

/* 函数指针类型（依赖上述声明） */
typedef pcap_t (*pcap_open_live_t)(const char*, int, int, int, char*);
typedef int    (*pcap_findalldevs_t)(pcap_if_t**, char*);
typedef void   (*pcap_freealldevs_t)(pcap_if_t*);
typedef int    (*pcap_next_ex_t)(pcap_t, struct pcap_pkthdr**, const u_char**);
typedef int    (*pcap_sendpacket_t)(pcap_t, const u_char*, int);
typedef void   (*pcap_close_t)(pcap_t);

static HMODULE g_npcapDll = NULL;
static pcap_open_live_t pfn_pcap_open_live = NULL;
static pcap_findalldevs_t pfn_pcap_findalldevs = NULL;
static pcap_freealldevs_t pfn_pcap_freealldevs = NULL;
static pcap_next_ex_t pfn_pcap_next_ex = NULL;
static pcap_close_t pfn_pcap_close = NULL;
static pcap_sendpacket_t pfn_pcap_sendpacket = NULL;

static bool load_npcap(void)
{
    if (g_npcapDll) return true;
    g_npcapDll = LoadLibraryA("wpcap.dll");
    if (!g_npcapDll) return false;
    pfn_pcap_open_live   = (pcap_open_live_t)GetProcAddress(g_npcapDll, "pcap_open_live");
    pfn_pcap_findalldevs = (pcap_findalldevs_t)GetProcAddress(g_npcapDll, "pcap_findalldevs");
    pfn_pcap_freealldevs = (pcap_freealldevs_t)GetProcAddress(g_npcapDll, "pcap_freealldevs");
    pfn_pcap_next_ex     = (pcap_next_ex_t)GetProcAddress(g_npcapDll, "pcap_next_ex");
    pfn_pcap_close       = (pcap_close_t)GetProcAddress(g_npcapDll, "pcap_close");
    if (!pfn_pcap_open_live || !pfn_pcap_findalldevs || !pfn_pcap_next_ex) {
        FreeLibrary(g_npcapDll); g_npcapDll = NULL; return false;
    }
    return true;
}

static void unload_npcap(void)
{
    if (g_npcapDll) { FreeLibrary(g_npcapDll); g_npcapDll = NULL; }
    pfn_pcap_open_live = pfn_pcap_findalldevs = pfn_pcap_freealldevs = NULL;
    pfn_pcap_next_ex = pfn_pcap_close = NULL;
}

/* ================================================================ */
/*  全局状态                                                           */
/* ================================================================ */

static struct netif  g_loopNetif;
static struct netif* g_physNetifs[NPCAP_MAX_ADAPTERS];
static int           g_physNetifCount = 0;

static volatile bool g_running     = false;
static XThread*      g_drvThread   = NULL;
static XMutex*       g_drvMutex    = NULL;

/* Npcap 句柄对应的 netif */
typedef struct {
    struct netif* netif;
    pcap_t       pcap;
} npcap_ctx_t;

static npcap_ctx_t g_npcapCtxs[NPCAP_MAX_ADAPTERS];
static int         g_npcapCtxCount = 0;

/* ================================================================ */
/*  loopback netif                                                      */
/* ================================================================ */

static err_t loop_output(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr)
{
    (void)ipaddr;
    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
    }
    return ERR_OK;
}

static err_t loop_init(struct netif* netif)
{
    netif->name[0]   = 'l';
    netif->name[1]   = 'o';
    netif->output    = loop_output;
    netif->linkoutput = NULL;

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   127, 0, 0, 1);
    IP4_ADDR(&mask, 255, 0, 0, 0);
    IP4_ADDR(&gw,   127, 0, 0, 1);
    netif_set_addr(netif, &ip, &mask, &gw);
    netif_set_up(netif);
    netif_set_link_up(netif);

    return ERR_OK;
}

/* ================================================================ */
/*  Npcap 物理网卡                                                     */
/* ================================================================ */

/**
 * @brief Npcap 网卡发包 —— 将 pbuf 转换为以太网帧并发送
 */
static err_t npcap_linkoutput(struct netif* netif, struct pbuf* p)
{
    npcap_ctx_t* ctx = (npcap_ctx_t*)netif->state;
    if (!ctx || !ctx->pcap) return ERR_IF;

    /* 组装以太网帧: pbuf 数据 = 完整 IP 包 */
    u16_t totalLen = p->tot_len;
    u8_t* frame = (u8_t*)XMalloc_Hybrid(totalLen);
    if (!frame) return ERR_MEM;

    pbuf_copy_partial(p, frame, totalLen, 0);
    if (pfn_pcap_sendpacket) pfn_pcap_sendpacket(ctx->pcap, frame, totalLen);
    XFree_Hybrid(frame);
    return ERR_OK;
}

/**
 * @brief Npcap 网卡初始化 —— 注册以太网接口
 */
static err_t npcap_init(struct netif* netif)
{
    int idx = netif->num;  /* 用户数据中传入索引 */
    if (idx < 0 || idx >= g_npcapCtxCount) return ERR_ARG;

    npcap_ctx_t* ctx = &g_npcapCtxs[idx];
    netif->name[0] = 'e';
    netif->name[1] = '0' + (char)idx;
    netif->output  = etharp_output;
    netif->linkoutput = npcap_linkoutput;
    netif->state   = ctx;
    netif->mtu     = 1500;
    netif->hwaddr_len = 6;  /* ETH_HWADDR_LEN，必须初始化否则 etharp_output 断言失败 */
    memset(netif->hwaddr, 0, 6);
    netif->flags  |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    ctx->netif = netif;

    /* IP 通过 DHCP 获取 */
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   0, 0, 0, 0);
    IP4_ADDR(&mask, 0, 0, 0, 0);
    IP4_ADDR(&gw,   0, 0, 0, 0);
    netif_set_addr(netif, &ip, &mask, &gw);
    netif_set_up(netif);
    dhcp_start(netif);

    return ERR_OK;
}

/* ================================================================ */
/*  驱动线程 (sys_check_timeouts + Npcap 收包)                         */
/* ================================================================ */

static void lwip_drv_thread(XThread* xthread, XVarList* vars)
{
    (void)xthread; (void)vars;
    while (g_running) {
        XMutex_lock(g_drvMutex);
        sys_check_timeouts();

        /* Npcap 收包 */
        for (int i = 0; i < g_npcapCtxCount; i++) {
            npcap_ctx_t* ctx = &g_npcapCtxs[i];
            if (!ctx->pcap || !ctx->netif) continue;
            struct pcap_pkthdr* hdr;
            const u_char* data;
            if (pfn_pcap_next_ex && pfn_pcap_next_ex(ctx->pcap, &hdr, &data) == 1) {
                struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)hdr->caplen, PBUF_POOL);
                if (p) {
                    memcpy(p->payload, data, hdr->caplen);
                    if (ctx->netif->input(p, ctx->netif) != ERR_OK) {
                        pbuf_free(p);
                    }
                }
            }
        }
        XMutex_unlock(g_drvMutex);
        Sleep(1);
    }
}

/* ================================================================ */
/*  平台导出接口                                                       */
/* ================================================================ */

struct netif* XNetworkLwip_platform_init(void)
{
    if (!g_drvMutex)
        g_drvMutex = XMutex_create(XLock_NonRecursive);

    /* 1. loopback */
    ip4_addr_t lip, lmask, lgw;
    IP4_ADDR(&lip,   127, 0, 0, 1);
    IP4_ADDR(&lmask, 255, 0, 0, 0);
    IP4_ADDR(&lgw,   127, 0, 0, 1);

    struct netif* lnif = netif_add(&g_loopNetif, &lip, &lmask, &lgw, NULL, loop_init, netif_input);
    if (lnif) {
        netif_set_default(lnif);
        XNetworkLwip_setDefaultNetif(lnif);
    }

    /* 2. Npcap 物理网卡 */
    if (load_npcap()) {
        pcap_if_t* alldevs = NULL;
        char errbuf[256] = {0};
        if (pfn_pcap_findalldevs && pfn_pcap_findalldevs(&alldevs, errbuf) == 0 && alldevs) {
            pcap_if_t* dev = alldevs;
            while (dev && g_npcapCtxCount < NPCAP_MAX_ADAPTERS) {
                pcap_t pcap = pfn_pcap_open_live(dev->name, 65536, 1/*promisc*/, 1/*ms*/, errbuf);
                if (pcap) {
                    npcap_ctx_t* ctx = &g_npcapCtxs[g_npcapCtxCount];
                    ctx->pcap  = pcap;
                    ctx->netif = NULL;
                    g_npcapCtxCount++;
                }
                dev = dev->next;
            }
            pfn_pcap_freealldevs(alldevs);

            /* 为每个 Npcap 设备注册 netif */
            for (int i = 0; i < g_npcapCtxCount; i++) {
                struct netif* nif = (struct netif*)XMalloc_System(sizeof(struct netif));
                if (nif) {
                    memset(nif, 0, sizeof(*nif));
                    nif->num = i;
                    ip4_addr_t ip, mask, gw;
                    IP4_ADDR(&ip, 0,0,0,0);
                    IP4_ADDR(&mask, 255,255,255,0);
                    IP4_ADDR(&gw, 0,0,0,0);
                    struct netif* ret = netif_add(nif, &ip, &mask, &gw, NULL, npcap_init, netif_input);
                    if (ret) {
                        g_physNetifs[g_physNetifCount++] = nif;
                        g_npcapCtxs[i].netif = nif;
                        if (lnif) continue;
                        netif_set_default(nif);
                        XNetworkLwip_setDefaultNetif(nif);
                    }
                }
            }
        }
    }

    /* 启动驱动线程 */
    g_running = true;
    XThread* dummy = NULL;
    XVarList* vl = XVarList_Create(XVar(XThread*, dummy));
    g_drvThread = XThread_create_func(lwip_drv_thread, vl);
    if (g_drvThread) XThread_start(g_drvThread);

    return lnif; /* 返回 loopback netif */
}

void XNetworkLwip_platform_deinit(void)
{
    g_running = false;
    if (g_drvThread) { XThread_deleteLater(g_drvThread); g_drvThread = NULL; }

    for (int i = 0; i < g_npcapCtxCount; i++) {
        if (g_npcapCtxs[i].pcap && pfn_pcap_close)
            pfn_pcap_close(g_npcapCtxs[i].pcap);
    }
    g_npcapCtxCount = 0;
    unload_npcap();

    if (g_drvMutex) { XMutex_delete(g_drvMutex); g_drvMutex = NULL; }
}

#endif /* XNETWORK_USE_LWIP */
#endif /* _WIN32 */