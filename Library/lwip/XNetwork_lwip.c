/**
 * @file XNetwork_lwip.c
 * @brief lwIP 平台网络实现（平台无关 + Raw API 回调模式）
 *
 * 架构分层：
 *   XNetwork_platform.h                    -> 统一平台抽象接口
 *   +-- XNetwork_win32.c                   -> Windows IOCP 实现
 *   +-- XNetwork_lwip.c（本文件）          -> lwIP 适配层（平台无关）
 *       +-- XNetwork_lwip_win32.c          -> Npcap 虚拟网卡（Windows 平台）
 *
 * 工作原理：
 *   1. lwip_init() 初始化 lwIP 协议栈
 *   2. XTimeWheelGroup 定时器每 20ms 触发：
 *      a) 轮询接收数据包（XNetworkLwip_pollPcap）
 *      b) 处理 lwIP 内部超时（sys_check_timeouts）
 *      c) 向应用线程投递网络事件
 *   3. 应用线程通过 Raw API 操作，sys_arch_protect 提供递归锁保护
 *
 * 基于 STM32F407+FreeRTOS 成功移植经验，适配 XSync + XMemory 系统。
 * 所有内存分配统一使用 XMalloc_System / XFree_System。
 * 定时器使用 XTimeWheelGroup 全局时间轮。
 * 随机数使用 XRandomGenerator_system()。
 */

#include "XNetwork_config.h"
#ifdef XNETWORK_USE_LWIP

#include "XNetwork_platform.h"
#include "XNetwork_lwip_platform.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XHostAddress.h"
#include "XByteArray.h"
#include "XNetworkInterface.h"
#include "XNetworkAddressEntry.h"
#include "XVector.h"
#include "XString.h"
#include "XFileDescriptor.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XTimeWheelGroup.h"
#include "XTimerData.h"
#include "XThread.h"
#include "XDateTime.h"
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/def.h"
#include "lwip/arch.h"
#include "lwip/sys.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * 内部宏与常量
 * ================================================================ */
#define L4P(p) ((XNetworkSocketPrivateLwip*)(p))
#define LWIP_TICK_MS 20  /* lwIP 定时器滴答周期（毫秒） */

typedef struct XNetworkSocketPrivateLwip XNetworkSocketPrivateLwip;

/* ================================================================
 * Socket 注册表 - 用于定时器回调中遍历所有活跃 Socket
 * ================================================================ */
static void* g_socketList[XNETWORK_LWIP_MAX_SOCKETS];
static int g_socketCount = 0;

/* 向注册表添加 Socket（自动去重） */
static void socketList_add(void* s) {
    int i;
    for (i = 0; i < g_socketCount; i++)
        if (g_socketList[i] == s) return;
    if (g_socketCount < XNETWORK_LWIP_MAX_SOCKETS)
        g_socketList[g_socketCount++] = s;
}

/* 从注册表移除 Socket（swap-and-pop，O(1)） */
static void socketList_remove(void* s) {
    int i;
    for (i = 0; i < g_socketCount; i++)
        if (g_socketList[i] == s) {
            g_socketList[i] = g_socketList[--g_socketCount];
            return;
        }
}

/* ================================================================
 * 定时器 - 使用 XTimeWheelGroup 全局时间轮
 * ================================================================ */
static XHandle g_lwipTickHandle = 0;
static void lwip_tick_cb(void* userData, XTimerData* timer);

/* 启动 lwIP 定时器滴答：每 20ms 触发一次 */
static void start_lwip_tick(void) {
    XTimerData d;
    XTimerData_init(&d, NULL);
    XTimerData_setTimeout(&d, LWIP_TICK_MS);
    XTimerData_setInterval(&d, LWIP_TICK_MS);
    XTimerData_setTimerCallback(&d, lwip_tick_cb);
    XTimerData_setSingleShot(&d, false);
    g_lwipTickHandle = XTimeWheelGroup_addTimerMs_base(XTimeWheelGroup_global(), d);
}

/* ================================================================
 * 全局状态
 * ================================================================ */
static int g_lwipRef = 0;              /* lwIP 引用计数 */
static bool g_lwipInited = false;      /* lwIP 是否已初始化 */
static struct netif* g_defaultNetif = NULL; /* 默认网络接口 */
static int g_lastError = 0;            /* 最后一次错误码 */


/* ================================================================
 * Socket 私有数据结构
 *
 * 封装 lwIP 的 TCP/UDP PCB 指针和状态信息。
 * 每个 XAbstractSocket 对应一个此结构体实例。
 * ================================================================ */
typedef struct XNetworkSocketPrivateLwip {
    XNetworkSocketPrivate base;    /* 基类：owner + notifiers */
    struct tcp_pcb* tpcb;          /* TCP 协议控制块 */
    struct udp_pcb* upcb;          /* UDP 协议控制块 */
    bool connected;                /* 是否已连接 */
    bool isServer;                 /* 是否为服务端 */
    bool closing;                  /* 是否正在关闭 */
    int sockType;                  /* Socket 类型 */
    char* rxBuf;                   /* 接收缓冲区 */
    int rxPos;                     /* 接收缓冲区当前写入位置 */
    int rxTotal;                   /* 最后一次接收的数据量 */
    ip_addr_t fromAddr;            /* UDP 数据报来源地址 */
    uint16_t fromPort;             /* UDP 数据报来源端口 */
    size_t writeFinished;          /* 已完成写入的字节数 */
    bool connectDone;              /* 连接完成标记 */
    bool hasReadData;              /* 有可读数据标记 */
    bool hasWriteDone;             /* 写入完成标记 */
    bool hasAccept;                /* 有新客户端连接标记 */
    bool hasError;                 /* 发生错误标记 */
    int lastErr;                   /* 最后一次 lwIP 错误码 */
    void* pendingAccept;           /* 待处理的客户端连接 */
} XNetworkSocketPrivateLwip;

/* ================================================================
 * 定时器回调：接收数据包 + 处理超时 + 投递事件
 *
 * 所有操作在 sys_arch_protect 保护下执行，确保线程安全。
 * ================================================================ */
static void lwip_tick_cb(void* userData, XTimerData* timer) {
    int i;
    (void)userData;
    (void)timer;

    /* 在核心锁保护下接收数据包和处理超时 */
    sys_prot_t prot = sys_arch_protect();
    XNetworkLwip_pollPcap();    /* 轮询接收数据包（Npcap/TAP） */
    sys_check_timeouts();       /* 处理 lwIP 内部超时 */
    sys_arch_unprotect(prot);

    /* 遍历所有 Socket，投递事件到应用线程 */
    for (i = 0; i < g_socketCount; i++) {
        XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)g_socketList[i];
        if (!s || !s->base.owner) continue;

        bool hasEvent = s->connectDone || s->hasReadData || s->hasWriteDone
                     || s->hasAccept || s->hasError || s->closing;
        if (!hasEvent) continue;

        XFd fd = XNetwork_socketFd((XNetworkSocketPrivate*)s);
        XEventSockAct* ev = XEventSockAct_create(fd, XSocketAct_ReadWrite);
        if (ev) XCoreApplication_postEvent((XObject*)s->base.owner, (XEvent*)ev, 0);
    }
}

/* ================================================================
 * 地址转换工具函数
 * ================================================================ */

/* XHostAddress -> lwIP ip_addr_t */
static void addr_to_ip(const XHostAddress* a, ip_addr_t* ip) {
    memset(ip, 0, sizeof(*ip));
    if (!a) return;
#if LWIP_IPV6
    if (XHostAddress_protocol(a) == XHostAddress_IPv6Protocol) {
        IP_SET_TYPE_VAL(*ip, IPADDR_TYPE_V6);
        XHostAddress_toIPv6Address(a, (void*)ip_2_ip6(ip));
    } else
#endif
    {
        IP_SET_TYPE_VAL(*ip, IPADDR_TYPE_V4);
        ip4_addr_set_u32(ip_2_ip4(ip), lwip_htonl(XHostAddress_toIPv4Address(a)));
    }
}

/* lwIP ip_addr_t -> XHostAddress */
static void ip_to_addr(const ip_addr_t* ip, XHostAddress* a) {
#if LWIP_IPV6
    if (IP_IS_V6_VAL(*ip))
        XHostAddress_setAddressIPv6(a, (const uint8_t*)ip_2_ip6(ip));
    else
#endif
        XHostAddress_setAddressIPv4(a, lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(ip))));
}

/* 确保 Socket 已分配文件描述符（用于事件分发系统） */
static void ensure_xfd(XNetworkSocketPrivate* priv) {
    if (!priv || !priv->owner) return;
    XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
    if (xfd == XFD_INVALID) {
        xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
        XIODevice_setFd((XIODevice*)priv->owner, xfd);
    }
}


/* ================================================================
 * 错误码转换：lwIP err_t -> 类 POSIX errno
 * ================================================================ */
int XNetworkLwip_err_to_errno(int e) {
    switch (e) {
    case ERR_OK:         return 0;
    case ERR_MEM:        return -1;
    case ERR_BUF:        return -1;
    case ERR_TIMEOUT:    return -2;
    case ERR_RTE:        return -3;
    case ERR_INPROGRESS: return -4;
    case ERR_VAL:        return -5;
    case ERR_WOULDBLOCK: return -6;
    case ERR_USE:        return -7;
    case ERR_ALREADY:    return -8;
    case ERR_ISCONN:     return -9;
    case ERR_CONN:       return -10;
    case ERR_IF:         return -11;
    case ERR_ABRT:       return -12;
    case ERR_RST:        return -13;
    case ERR_CLSD:       return -14;
    case ERR_ARG:        return -15;
    default:             return -1;
    }
}

/* ================================================================
 * TCP 回调函数
 *
 * 这些回调在 lwIP 核心线程（定时器滴答）中执行，受 sys_arch_protect 保护。
 * 回调中只设置标志位，实际事件处理在应用线程中完成。
 * ================================================================ */

/* TCP 数据接收回调 */
static err_t tcpRecvCb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)err;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s) return ERR_ABRT;
    if (!p) { s->closing = true; s->hasReadData = true; return ERR_OK; }
    int total = p->tot_len;
    if (total > 0 && s->rxBuf) {
        int space = XNETWORK_LWIP_RECV_BUFFER_SIZE - s->rxPos;
        int copy = total < space ? total : space;
        pbuf_copy_partial(p, s->rxBuf + s->rxPos, copy, 0);
        s->rxPos += copy; s->rxTotal = copy; s->hasReadData = true;
    }
    tcp_recved(pcb, total);
    pbuf_free(p);
    return ERR_OK;
}

/* TCP 发送完成回调 */
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (s) { s->writeFinished += len; s->hasWriteDone = true; }
    return ERR_OK;
}

/* TCP 错误回调 */
static void tcpErrCb(void* arg, err_t err) {
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (s) {
        s->closing = true; s->hasError = true;
        s->lastErr = err; g_lastError = XNetworkLwip_err_to_errno(err);
        s->tpcb = NULL;
    }
}

/* TCP 轮询回调（保持连接，定时处理）
 * 参数 interval 为 tcp_poll 间隔系数，单位 0.5 秒
 * interval=2 表示 1 秒间隔，与 STM32 移植一致 */
static err_t tcpPollCb(void* arg, struct tcp_pcb* pcb) {
    (void)arg; (void)pcb; return ERR_OK;
}

/* TCP 连接完成回调 */
static err_t tcpConnectedCb(void* arg, struct tcp_pcb* pcb, err_t err) {
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s) return ERR_ABRT;
    if (err == ERR_OK) {
        s->connected = true; s->connectDone = true;
        LWIP_DBG("[TCP连接] 连接成功\n");
    } else {
        s->hasError = true; s->lastErr = err;
        g_lastError = XNetworkLwip_err_to_errno(err);
        LWIP_DBG("[TCP连接] 连接失败, err=%d\n", (int)err);
    }
    return ERR_OK;
}

/* TCP 接受新连接回调（服务端）
 * 创建客户端 Socket 私有数据，存入服务端 pendingAccept 队列 */
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err) {
    (void)err;
    XNetworkSocketPrivateLwip* ss = (XNetworkSocketPrivateLwip*)arg;
    if (!ss || !newPcb) return ERR_ABRT;

    /* 为新客户端分配 Socket 私有数据结构 */
    XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*cs));
    if (!cs) return ERR_MEM;
    memset(cs, 0, sizeof(*cs));
    cs->tpcb = newPcb;
    cs->connected = true;
    cs->sockType = XNetwork_Tcp;
    cs->rxBuf = (char*)XMalloc_System(XNETWORK_LWIP_RECV_BUFFER_SIZE);
    if (!cs->rxBuf) { XFree_System(cs); return ERR_MEM; }

    /* 注册 TCP 回调 */
    tcp_arg(newPcb, cs);
    tcp_recv(newPcb, tcpRecvCb);
    tcp_sent(newPcb, tcpSentCb);
    tcp_err(newPcb, tcpErrCb);
    tcp_poll(newPcb, tcpPollCb, 2);  /* 每 1 秒轮询一次 */

    /* 存入服务端的待处理队列 */
    ss->pendingAccept = cs;
    ss->hasAccept = true;
    LWIP_DBG("[TCP接受] 新客户端连接\n");
    return ERR_OK;
}

/* UDP 数据接收回调 */
static void udpRecvCb(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                      const ip_addr_t* addr, u16_t port) {
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s || !p) return;
    if (addr) s->fromAddr = *addr;
    s->fromPort = port;
    int total = p->tot_len;
    if (total > 0 && s->rxBuf) {
        int space = XNETWORK_LWIP_RECV_BUFFER_SIZE - s->rxPos;
        int copy = total < space ? total : space;
        pbuf_copy_partial(p, s->rxBuf + s->rxPos, copy, 0);
        s->rxPos += copy; s->rxTotal = copy; s->hasReadData = true;
    }
    pbuf_free(p);
}


/* ================================================================
 * 平台初始化与清理
 * ================================================================ */

void XNetwork_ensureInit(void) {
    if (g_lwipInited) { g_lwipRef++; return; }
    LWIP_DBG("[lwIP初始化] 开始初始化 lwIP 协议栈...\n");

    /* 初始化 lwIP 协议栈（内存池、pbuf、TCP/UDP/IP/ARP 等） */
    lwip_init();

    /* 必须在 platform_init 之前启动定时器，否则 DHCP 的 sys_check_timeouts 无法运行 */
    start_lwip_tick();

    /* 平台网卡初始化（Npcap / TAP / 硬件 MAC） */
    struct netif* nif = XNetworkLwip_platform_init();
    if (nif) {
        g_defaultNetif = nif;
        LWIP_DBG("[lwIP初始化] 平台虚拟网卡初始化成功\n");
    } else {
        LWIP_DBG("[lwIP初始化] 警告：平台虚拟网卡初始化失败，可能无法访问网络\n");
    }

    g_lwipInited = true;
    g_lwipRef = 1;
    LWIP_DBG("[lwIP初始化] 完成，引用计数=%d\n", g_lwipRef);
}

void XNetwork_cleanup(void) {
    if (!g_lwipInited) return;
    g_lwipRef--;
    if (g_lwipRef > 0) return;

    LWIP_DBG("[lwIP清理] 开始清理...\n");

    /* 停止定时器 */
    if (g_lwipTickHandle) {
        XTimeWheelGroup_removeTimer_base(XTimeWheelGroup_global(), g_lwipTickHandle);
        g_lwipTickHandle = 0;
    }

    /* 平台网卡清理 */
    XNetworkLwip_platform_deinit();

    g_lwipInited = false;
    g_defaultNetif = NULL;
    LWIP_DBG("[lwIP清理] 完成\n");
}

/* 错误处理 */
int XNetwork_lastError(void) { return g_lastError; }
bool XNetwork_isEAgain(int err) { return err == -6; }
char* XNetwork_errorString(int errorCode) {
    char* s = (char*)XMalloc_System(64);
    if (s) snprintf(s, 64, "lwIP error: %d", errorCode);
    return s;
}

/* ================================================================
 * Socket 私有数据管理
 * ================================================================ */

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner) {
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->base.owner = owner;
    s->base.notifiers = XVector_create(sizeof(void*));
    s->rxBuf = (char*)XMalloc_System(XNETWORK_LWIP_RECV_BUFFER_SIZE);
    if (!s->rxBuf) { XFree_System(s); return NULL; }
    socketList_add(s);
    return (XNetworkSocketPrivate*)s;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv) {
    if (!priv) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    socketList_remove(s);

    /* 关闭 lwIP PCB（需持有核心锁） */
    if (s->tpcb || s->upcb) {
        sys_prot_t prot = sys_arch_protect();
        if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
        if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
        sys_arch_unprotect(prot);
    }

    /* 释放资源 */
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (s->base.notifiers) XVector_delete_base(s->base.notifiers);

    /* 释放待处理的客户端连接 */
    if (s->pendingAccept) {
        XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)s->pendingAccept;
        if (cs->rxBuf) XFree_System(cs->rxBuf);
        XFree_System(cs);
        s->pendingAccept = NULL;
    }
    XFree_System(s);
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv) {
    return priv ? (intptr_t)priv : -1;
}
XFd XNetwork_socketFd(const XNetworkSocketPrivate* priv) {
    return (priv && priv->owner) ? XIODevice_fd((XIODevice*)priv->owner) : XFD_INVALID;
}
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv) {
    return priv ? L4P(priv)->connected : false;
}


/* ================================================================
 * Socket 绑定
 *
 * 注意：lwIP Raw API 模式下，TCP 绑定即开始监听（服务端模式）。
 * 客户端模式在 XNetwork_socketConnect 中创建 TCP PCB。
 * ================================================================ */
uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType) {
    (void)reuseAddr; (void)shareAddr;  /* lwIP Raw API 不支持 SO_REUSEADDR */
    if (!priv) return 0;
    XNetwork_ensureInit();
    XNetworkSocketPrivateLwip* s = L4P(priv);
    s->sockType = (int)sockType;

    /* 转换绑定地址 */
    ip_addr_t bindAddr;
    if (address) addr_to_ip(address, &bindAddr); else bindAddr = *IP_ADDR_ANY;

    if (sockType == XNetwork_Tcp) {
        /* TCP 服务端：创建 PCB 并开始监听 */
        s->tpcb = tcp_new();
        if (!s->tpcb) return 0;
        if (tcp_bind(s->tpcb, &bindAddr, port) != ERR_OK) {
            tcp_close(s->tpcb); s->tpcb = NULL; return 0;
        }
        struct tcp_pcb* listenPcb = tcp_listen_with_backlog(s->tpcb, (u8_t)XNETWORK_LWIP_MAX_LISTEN_BACKLOG);
        if (!listenPcb) {
            tcp_close(s->tpcb); s->tpcb = NULL; return 0;
        }
        s->tpcb = listenPcb;
        s->isServer = true;
        tcp_arg(s->tpcb, s);
        tcp_accept(s->tpcb, tcpAcceptCb);
        /* TCP 服务端也注册到 socketList，确保 accept 事件能被投递 */
        socketList_add(s);
        LWIP_DBG("[绑定] TCP服务端 端口=%d backlog=%d\n", (int)port, XNETWORK_LWIP_MAX_LISTEN_BACKLOG);
    } else {
        /* UDP：创建 PCB 并绑定 */
        s->upcb = udp_new();
        if (!s->upcb) return 0;
        if (udp_bind(s->upcb, &bindAddr, port) != ERR_OK) {
            udp_remove(s->upcb); s->upcb = NULL; return 0;
        }
        udp_recv(s->upcb, udpRecvCb, s);
        s->connected = true;
        LWIP_DBG("[绑定] UDP 端口=%d\n", (int)port);
    }
    ensure_xfd(priv);
    return port;
}

/* 等待 DHCP 获取 IP 地址 */
static bool wait_for_dhcp(struct netif* n, uint32_t timeoutMs) {
    uint32_t start = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF);
    LWIP_DBG("[DHCP等待] 开始等待 netif=%c%c%d link_up=%d\n",
             n->name[0], n->name[1], n->num, netif_is_link_up(n));
    while (!dhcp_supplied_address(n)) {
        uint32_t elapsed = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF) - start;
        if (elapsed >= timeoutMs) {
            LWIP_DBG("[DHCP等待] 超时(%dms), 当前IP=%s\n",
                     (int)timeoutMs, ip4addr_ntoa(netif_ip4_addr(n)));
            return false;
        }
        /* 在核心锁保护下处理数据包和超时 */
        sys_prot_t prot = sys_arch_protect();
        XNetworkLwip_pollPcap();
        sys_check_timeouts();
        sys_arch_unprotect(prot);
        XThread_msleep(50);
    }
    LWIP_DBG("[DHCP等待] 获取IP成功: %s\n", ip4addr_ntoa(netif_ip4_addr(n)));
    return true;
}

/* ================================================================
 * Socket 连接
 * ================================================================ */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, XNetworkProtocol protocol,
                            XNetworkSocketType sockType) {
    (void)protocol;
    if (!priv || !hostName) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetwork_ensureInit();
    s->sockType = (int)sockType;

    /* DNS 解析或 IP 地址转换 */
    ip_addr_t ip;
    err_t err = dns_gethostbyname(hostName, &ip, NULL, NULL);
    LWIP_DBG("[连接] DNS解析结果=%d\n", (int)err);
    if (err != ERR_OK && ipaddr_aton(hostName, &ip) == 0) {
        LWIP_DBG("[连接] DNS和IP解析都失败\n");
        return false;
    }
    LWIP_DBG("[连接] 目标IP解析成功\n");

    /* 等待 DHCP 网卡获取 IP 地址 */
    {
        struct netif* n;
        for (n = netif_list; n; n = n->next) {
            if (ip_addr_isany(&n->ip_addr) && (n->flags & NETIF_FLAG_UP) && netif_dhcp_data(n)) {
                LWIP_DBG("[连接] DHCP网卡 %c%c%d 无IP，等待DHCP...\n", n->name[0], n->name[1], n->num);
                wait_for_dhcp(n, XNETWORK_LWIP_DNS_TIMEOUT_MS);
            }
        }
    }

    /* 确保有默认路由 */
    if (!netif_default) {
        LWIP_DBG("[连接] 无默认路由，尝试自动查找...\n");
        struct netif* n;
        for (n = netif_list; n; n = n->next)
            if (!ip_addr_isany(&n->ip_addr)) {
                netif_set_default(n);
                LWIP_DBG("[连接] 自动设默认路由: %s\n", ip4addr_ntoa(&n->ip_addr));
                break;
            }
    }
    LWIP_DBG("[连接] 默认路由=%s\n",
             netif_default ? ip4addr_ntoa(&netif_default->ip_addr) : "NULL");

    if (sockType == XNetwork_Tcp) {
        /* TCP 客户端连接 */
        s->tpcb = tcp_new();
        LWIP_DBG("[连接] tcp_new=%p\n", (void*)s->tpcb);
        if (!s->tpcb) return false;
        tcp_arg(s->tpcb, s);
        tcp_recv(s->tpcb, tcpRecvCb);
        tcp_sent(s->tpcb, tcpSentCb);
        tcp_err(s->tpcb, tcpErrCb);
        tcp_poll(s->tpcb, tcpPollCb, 2);  /* 每 1 秒轮询 */
        err = tcp_connect(s->tpcb, &ip, port, tcpConnectedCb);
        LWIP_DBG("[连接] tcp_connect结果=%d\n", (int)err);
        if (err != ERR_OK) { tcp_close(s->tpcb); s->tpcb = NULL; return false; }
    } else {
        /* UDP 连接（实际上只是设置默认远程地址） */
        s->upcb = udp_new();
        if (!s->upcb) return false;
        udp_recv(s->upcb, udpRecvCb, s);
        err = udp_connect(s->upcb, &ip, port);
        if (err != ERR_OK) { udp_remove(s->upcb); s->upcb = NULL; return false; }
        s->connected = true;
    }
    ensure_xfd(priv);
    return true;
}

/* 断开连接 */
void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv) {
    if (!priv) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    sys_prot_t prot = sys_arch_protect();
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
    sys_arch_unprotect(prot);
    s->connected = false;
}


/* ================================================================
 * 数据读写
 * ================================================================ */

/* 从 Socket 读取数据（从内部缓冲区拷贝） */
int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer) {
    (void)sockType; (void)ringBuffer;
    if (!priv || !buf || len <= 0) return -1;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (s->rxPos <= 0) return 0;
    int copy = s->rxPos < (int)len ? s->rxPos : (int)len;
    memcpy(buf, s->rxBuf, copy);
    if (copy < s->rxPos) memmove(s->rxBuf, s->rxBuf + copy, s->rxPos - copy);
    s->rxPos -= copy;
    return copy;
}

/* 向 Socket 写入数据 */
int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr,
                             uint16_t destPort, void* ringBuffer) {
    (void)ringBuffer;
    if (!priv || !buf || len <= 0) return -1;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    sys_prot_t prot = sys_arch_protect();

    if (sockType == XNetwork_Tcp) {
        if (!s->tpcb) { sys_arch_unprotect(prot); return -1; }
        u16_t sndBuf = tcp_sndbuf(s->tpcb);
        u16_t toWrite = (u16_t)((len > sndBuf) ? sndBuf : len);
        if (toWrite == 0) { sys_arch_unprotect(prot); return -6; }
        err_t e = tcp_write(s->tpcb, buf, toWrite, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) {
            tcp_output(s->tpcb);
            sys_arch_unprotect(prot);
            return toWrite;
        }
        sys_arch_unprotect(prot);
        return -1;
    } else {
        if (!s->upcb) { sys_arch_unprotect(prot); return -1; }
        ip_addr_t dst;
        if (destAddr) { addr_to_ip(destAddr, &dst); }
        else { dst = s->upcb->remote_ip; destPort = s->upcb->remote_port; }
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (!p) { sys_arch_unprotect(prot); return -1; }
        memcpy(p->payload, buf, (size_t)len);
        err_t e = udp_sendto(s->upcb, p, &dst, destPort);
        pbuf_free(p); sys_arch_unprotect(prot);
        return (e == ERR_OK) ? len : -1;
    }
}

/* ================================================================
 * 事件处理与选项
 * ================================================================ */

/* 处理 Socket 事件（清除标志位） */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event) {
    (void)event;
    if (!priv) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    bool hasEvent = false;
    if (s->connectDone) { s->connectDone = false; hasEvent = true; }
    if (s->hasReadData) { s->hasReadData = false; hasEvent = true; }
    if (s->hasWriteDone) { s->hasWriteDone = false; hasEvent = true; }
    if (s->hasAccept) { s->hasAccept = false; hasEvent = true; }
    if (s->hasError) { s->hasError = false; hasEvent = true; }
    return hasEvent;
}

void XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, int state, int openMode) {
    (void)priv; (void)fd; (void)state; (void)openMode;
}

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value) {
    (void)priv; (void)option; (void)value;
    return false;
}

void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option) {
    (void)priv; (void)option;
    return NULL;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size) {
    (void)priv; (void)size;
}

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv) {
    return priv ? L4P(priv)->rxBuf : NULL;
}

size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv) {
    return priv ? (size_t)L4P(priv)->rxTotal : 0;
}

size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv) {
    return priv ? L4P(priv)->writeFinished : 0;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp) {
    if (!priv) return;
    (void)isUdp;
    L4P(priv)->hasReadData = false;
}

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr) {
    if (!priv) return (XServerHandle)(-1);
    (void)backlog;
    uint16_t actualPort = XNetwork_socketBind(priv, addr, port, reuseAddr, false, XNetwork_Tcp);
    if (actualPort == 0) return (XServerHandle)(-1);
    return (XServerHandle)(intptr_t)priv;
}

void XNetwork_serverAccept(XNetworkSocketPrivate* priv, XServerHandle server) {
    (void)priv; (void)server;
}

uint16_t XNetwork_serverPort(XServerHandle server) {
    if (!server || server == (XServerHandle)(-1)) return 0;
    XNetworkSocketPrivateLwip* s = L4P((XNetworkSocketPrivate*)(intptr_t)server);
    if (!s || !s->tpcb) return 0;
    return s->tpcb->local_port;
}

void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server) {
    if (!server || server == (XServerHandle)(-1)) return;
    XNetworkSocketPrivateLwip* s = L4P((XNetworkSocketPrivate*)(intptr_t)server);
    if (!s) return;
    sys_prot_t prot = sys_arch_protect();
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    sys_arch_unprotect(prot);
    s->isServer = false;
}

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv,
                                                XHostAddress* clientAddr, uint16_t* clientPort) {
    if (!priv) return (XSocketHandle)(-1);
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (!s->pendingAccept) return (XSocketHandle)(-1);
    XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)s->pendingAccept;
    s->pendingAccept = NULL;
    if (clientAddr && cs->tpcb) ip_to_addr(&cs->tpcb->remote_ip, clientAddr);
    if (clientPort && cs->tpcb) *clientPort = cs->tpcb->remote_port;
    socketList_add(cs);
    LWIP_DBG("[TCP接受] 返回客户端Socket=%p\n", (void*)cs);
    return (XSocketHandle)(intptr_t)cs;
}

bool XNetwork_serverContinueAccept(XNetworkSocketPrivate* priv) {
    if (!priv) return false;
    L4P(priv)->hasAccept = false;
    return true;
}

bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count) {
    if (!name || !addrs || !count) return false;
    XNetwork_ensureInit();
    ip_addr_t ip;
    err_t e = dns_gethostbyname(name, &ip, NULL, NULL);
    if (e != ERR_OK && ipaddr_aton(name, &ip) == 0) return false;
    XHostAddress* addr = XHostAddress_create();
    if (!addr) return false;
    ip_to_addr(&ip, addr);
    *addrs = addr; *count = 1;
    return true;
}

char* XNetwork_localHostName(void) {
    char* name = (char*)XMalloc_System(32);
    if (name) snprintf(name, 32, "lwip-device");
    return name;
}

typedef struct { struct netif* next; int idx; } LwipIfIter;

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void) {
    XNetwork_ensureInit();
    LwipIfIter* it = (LwipIfIter*)XMalloc_System(sizeof(LwipIfIter));
    if (!it) return NULL;
    it->next = netif_list; it->idx = 0;
    return (XNetworkInterfaceIterator)it;
}

XNetworkInterface* XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter) {
    LwipIfIter* it = (LwipIfIter*)iter;
    if (!it || !it->next) return NULL;
    struct netif* nif = it->next; it->next = nif->next;
    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;
    char nameBuf[8]; snprintf(nameBuf, sizeof(nameBuf), "%c%c%d", nif->name[0], nif->name[1], nif->num);
    iface->name = XString_create_utf8(nameBuf);
    iface->humanReadableName = XString_create_utf8(nameBuf);
    if (nif->hwaddr_len >= 6) {
        char macStr[32]; snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            nif->hwaddr[0], nif->hwaddr[1], nif->hwaddr[2], nif->hwaddr[3], nif->hwaddr[4], nif->hwaddr[5]);
        iface->hardwareAddress = XString_create_utf8(macStr);
    }
    iface->index = nif->num; iface->mtu = (int)nif->mtu;
    if (nif->flags & NETIF_FLAG_UP) iface->flags |= XNetworkInterface_IsUp | XNetworkInterface_IsRunning;
    if (nif->flags & NETIF_FLAG_LINK_UP) iface->flags |= XNetworkInterface_IsRunning;
    if (nif->flags & NETIF_FLAG_BROADCAST) iface->flags |= XNetworkInterface_CanBroadcast;
    if (nif->name[0] == 'l' && nif->name[1] == 'o') {
        iface->flags |= XNetworkInterface_IsLoopBack; iface->type = XNetworkInterface_Loopback;
    } else { iface->type = XNetworkInterface_Ethernet; }
    if (!ip_addr_isany(&nif->ip_addr)) {
        XNetworkAddressEntry entry; XNetworkAddressEntry_init(&entry);
        ip_to_addr(&nif->ip_addr, &entry.ip); ip_to_addr(&nif->netmask, &entry.netmask);
        if (nif->flags & NETIF_FLAG_BROADCAST) {
            uint32_t ipVal = ip4_addr_get_u32(ip_2_ip4(&nif->ip_addr));
            uint32_t maskVal = ip4_addr_get_u32(ip_2_ip4(&nif->netmask));
            XHostAddress_setAddressIPv4(&entry.broadcast, lwip_ntohl(ipVal | ~maskVal));
            entry.broadcastIsValid = true;
        }
        XVector_push_back_move_1_base(iface->addressEntries, &entry);
        XNetworkAddressEntry_deinit_base(&entry);
    }
    iface->isValid = true; it->idx++;
    return iface;
}

void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter) { if (iter) XFree_System(iter); }

XVector* XNetwork_getInterfaceAddresses(const XString* ifname) {
    if (!ifname) return NULL;
    const char* name = XString_toUtf8(ifname); if (!name) return NULL;
    XVector* entries = XVector_create(sizeof(XNetworkAddressEntry));
    if (!entries) return NULL;
    XContainerSetDataCopyMethod(entries, XNetworkAddressEntry_copy_base);
    XContainerSetDataMoveMethod(entries, XNetworkAddressEntry_move_base);
    XContainerSetDataDeinitMethod(entries, XNetworkAddressEntry_deinit_base);
    struct netif* nif;
    for (nif = netif_list; nif; nif = nif->next) {
        char nifName[8]; snprintf(nifName, sizeof(nifName), "%c%c%d", nif->name[0], nif->name[1], nif->num);
        if (strcmp(nifName, name) != 0) continue;
        if (!ip_addr_isany(&nif->ip_addr)) {
            XNetworkAddressEntry entry; XNetworkAddressEntry_init(&entry);
            ip_to_addr(&nif->ip_addr, &entry.ip); ip_to_addr(&nif->netmask, &entry.netmask);
            XVector_push_back_move_1_base(entries, &entry);
            XNetworkAddressEntry_deinit_base(&entry);
        }
        break;
    }
    return entries;
}

bool XNetwork_multicastGroup(XSocketHandle sock, bool join, const XHostAddress* group, uint32_t ifIdx) {
    (void)sock; (void)join; (void)group; (void)ifIdx; return false;
}
int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg) {
    (void)sock; (void)op; (void)arg; return -1;
}

bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv, XHostAddress* src, uint16_t* port) {
    if (!priv) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (src) ip_to_addr(&s->fromAddr, src);
    if (port) *port = s->fromPort;
    return true;
}

int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t sz,
                               const XHostAddress* addr, uint16_t port) {
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)sock;
    if (!s || !s->upcb || !data || sz <= 0) return -1;
    ip_addr_t dst; addr_to_ip(addr, &dst);
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)sz, PBUF_RAM);
    if (!p) return -1;
    memcpy(p->payload, data, (size_t)sz);
    sys_prot_t prot = sys_arch_protect();
    err_t e = udp_sendto(s->upcb, p, &dst, port);
    sys_arch_unprotect(prot);
    pbuf_free(p);
    return (e == ERR_OK) ? sz : -1;
}

XSocketHandle XNetwork_createTcpSocket(XNetworkProtocol protocol) {
    (void)protocol;
    XNetwork_ensureInit();
    XNetworkSocketPrivate* priv = XNetwork_createSocketPrivate(NULL);
    if (!priv) return (XSocketHandle)(-1);
    XNetworkSocketPrivateLwip* s = L4P(priv);
    s->sockType = XNetwork_Tcp;
    s->tpcb = tcp_new();
    if (!s->tpcb) { XNetwork_deleteSocketPrivate(priv); return (XSocketHandle)(-1); }
    tcp_arg(s->tpcb, s); tcp_recv(s->tpcb, tcpRecvCb); tcp_sent(s->tpcb, tcpSentCb);
    tcp_err(s->tpcb, tcpErrCb); tcp_poll(s->tpcb, tcpPollCb, 2);
    return (XSocketHandle)(intptr_t)priv;
}

XSocketHandle XNetwork_createUdpSocket(XNetworkProtocol protocol) {
    (void)protocol;
    XNetwork_ensureInit();
    XNetworkSocketPrivate* priv = XNetwork_createSocketPrivate(NULL);
    if (!priv) return (XSocketHandle)(-1);
    XNetworkSocketPrivateLwip* s = L4P(priv);
    s->sockType = XNetwork_Udp;
    s->upcb = udp_new();
    if (!s->upcb) { XNetwork_deleteSocketPrivate(priv); return (XSocketHandle)(-1); }
    udp_recv(s->upcb, udpRecvCb, s);
    return (XSocketHandle)(intptr_t)priv;
}

void XNetwork_closeSocket(XSocketHandle sock) {
    if (sock == (XSocketHandle)(-1)) return;
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)(intptr_t)sock;
    XNetwork_deleteSocketPrivate(priv);
}

bool XNetwork_getSystemProxy(const char* url, XNetworkProxy* out) {
    (void)url; (void)out; return false;
}
int XNetwork_gssapiAuth(const char* name, const XByteArray* in, XByteArray* out, void** ctx) {
    (void)name; (void)in; (void)out; (void)ctx; return -1;
}

struct netif* XNetworkLwip_defaultNetif(void) { return g_defaultNetif; }
void XNetworkLwip_setDefaultNetif(struct netif* n) { g_defaultNetif = n; }

#endif /* XNETWORK_USE_LWIP */