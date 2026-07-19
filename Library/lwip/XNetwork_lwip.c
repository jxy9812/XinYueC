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
 * 运行原理：
 *   1. lwip_init() 初始化 lwIP 协议栈
 *   2. XTimeWheelGroup 定时器每 LWIP_TICK_MS 毫秒触发（默认 5ms），
 *      a) 轮询虚拟网卡数据包（XNetworkLwip_pollPcap）
 *      b) lwIP 定时器由 LWIP_TIMERS_CUSTOM 直接对接 XTimeWheelGroup，无需轮询
 *      c) 向应用线程投递事件通知
 *   3. 应用线程通过 Raw API 操作，sys_arch_protect 提供递归锁保护
 *
 * 基于 XSync + XMemory 系统实现，平台无关。
 * 内存分配统一使用 XMalloc_System / XFree_System。
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

#include "CXinYueConfig.h"  /* XAbstractNetIoRing_ON */
#if XAbstractNetIoRing_ON
#include "XAbstractNetIoRing.h"  /* CQ push from callbacks */
#endif
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Core locking abstraction
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0: 单线程，锁为空操作（零开销）
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=1: sys_arch_protect（递归锁）
 *   NO_SYS=0: LOCK_TCPIP_CORE（与 tcpip_thread 同步）
 * ================================================================ */
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
typedef sys_prot_t XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        sys_arch_protect()
#define XNET_LWIP_UNLOCK(l)     sys_arch_unprotect(l)
#elif NO_SYS
/* 单线程模式：所有 lwIP 操作在主线程，无并发访问，锁为空操作 */
typedef int XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        (0)
#define XNET_LWIP_UNLOCK(l)     ((void)(l))
#else
typedef int XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        (LOCK_TCPIP_CORE(), 0)
#define XNET_LWIP_UNLOCK(l)     UNLOCK_TCPIP_CORE()
#endif

/* ================================================================
 * 内部宏常量
 * ================================================================ */
#define L4P(p) ((XNetworkSocketPrivateLwip*)(p))
/* lwIP 网卡轮询周期，可通过 XNETWORK_LWIP_TICK_MS 配置 */
#define LWIP_TICK_MS XNETWORK_LWIP_TICK_MS

typedef struct XNetworkSocketPrivateLwip XNetworkSocketPrivateLwip;

/* ================================================================
 * 全局状态
 * ================================================================ */
/* lwIP 全局状态（位域压缩，减少内存占用）
 * 原本为 4 个独立静态变量（约 24~32 字节），合并为 1 个结构体（16 字节）。
 * - defaultNetif: 指针不可使用位域，单独占 8 字节
 * - lastError:    错误码范围 -15~0，8 位有符号足够
 * - ref:          引用计数通常 1~5，7 位（最大 127）足够
 * - inited:       初始化标志，1 位足够 */
static struct {
    struct netif* defaultNetif;  /* 默认网络接口 */
    int      lastError : 8;      /* 最后一次错误码 (-15~0) */
    unsigned ref       : 7;      /* lwIP 引用计数 (max 127) */
    unsigned inited    : 1;      /* 是否已初始化 */
} g_state;


/* ================================================================
 * Socket 私有数据结构
 *
 * 封装 lwIP 的 TCP/UDP PCB 指针和状态信息。
 * 每个 XAbstractSocket 对应一个此结构体实例。
 * ================================================================ */
typedef struct XNetworkSocketPrivateLwip {
    XNetworkSocketPrivate base;    /* 基类：owner + notifiers (16 字节) */
    XFd fd;                       /* 缓存的文件描述符（避免回调中 vtable 查找） */
    struct tcp_pcb* tpcb;          /* TCP 协议控制块 */
    struct udp_pcb* upcb;          /* UDP 协议控制块 */
    char* rxBuf;                   /* 接收缓冲区 */
    void* pendingAccept;           /* 待领取的客户端连接 */
    size_t writeFinished;          /* 已发送并确认的累计字节数（通知消费者） */
    size_t lastWriteFinished;      /* 上次通知消费者的已发送字节数 */
    /* 以上指针/size_t 在 64 位下共 72 字节（含 XFd fd 字段） */
    int rxPos;                     /* 接收缓冲区当前写入位置 */
    int rxTotal;                   /* 上一次接收的字节数 */
    ip_addr_t fromAddr;            /* UDP 数据报来源地址 (IPv4=4 字节) */
    uint16_t fromPort;             /* UDP 数据报来源端口 */
    /* 位域压缩：8 个布尔(8位) + sockType(2位) + lastErr(8位) = 18 位，合并到 4 字节
     * 原本 8 个 bool(8字节) + sockType(4字节) + lastErr(4字节) + 对齐填充
     * 现压缩为 4 字节，结构体从 ~104 字节减至 88 字节 */
    unsigned connected   : 1;      /* 是否已连接 */
    unsigned isServer    : 1;      /* 是否为服务端 */
    unsigned closing     : 1;      /* 是否正在关闭 */
    unsigned connectDone : 1;      /* 连接完成标记 */
    unsigned hasReadData : 1;      /* 有可读数据标记 */
    unsigned hasWriteDone: 1;      /* 写入完成标记 */
    unsigned hasAccept   : 1;      /* 有新客户端连接标记 */
    unsigned hasError    : 1;      /* 有错误发生标记 */
    unsigned sockType    : 2;      /* Socket 类型 (0=TCP, 1=UDP) */
    int      lastErr     : 8;      /* 最后一次 lwIP 错误码 (err_t, -16~0) */
} XNetworkSocketPrivateLwip;

/* ================================================================
 * Socket 事件直接投递（回调驱动，替代轮询）
 *
 * 由 lwIP Raw API 回调函数调用，将指定类型的事件 CQ 条目
 * 推入全局 IoRing。每个回调推送自己的事件类型，无需合并标志，
 * 无 eventPending 防重复机制--重复 CQ 条目在应用层处理时
 * 因标志已清除而成为空操作（no-op），不会造成数据重复。
 * ================================================================ */

/**
 * @brief 从 lwIP 回调中投递指定类型的 Socket 事件到 IoRing CQ
 * @param s       Socket 私有数据指针
 * @param actType 事件类型掩码（XSocketActType 位掩码）
 *
 * 调用时机：lwIP 回调函数设置标志位之后、返回之前。
 * 已在核心锁保护下执行（回调由 lwIP 核心在锁内调用）。
 */
static void push_socket_cq(XNetworkSocketPrivateLwip* s, uint32_t actType) {
#if XAbstractNetIoRing_ON
    if (!s || !s->base.owner || actType == 0) return;

    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    if (!ring) return;

    XFd fd = s->fd;
    if (fd == XFD_INVALID) {
        /* 回调在 ensure_xfd 之前触发（极少）：直接从设备获取并缓存 */
        fd = XIODevice_fd((XIODevice*)s->base.owner);
        s->fd = fd;
    }

    XAbstractNetIoRing_CQEntry cq = {
        .m_fd = fd,
        .m_events = actType,
        .m_sourceType = XAbstractNetIoRing_Source_Netif,
        .m_fdType = XFD_TYPE_SOCKET
    };
    XAbstractNetIoRing_pushCompletion(ring, &cq);
#else
    (void)s;
    (void)actType;
#endif
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

/* 确保 Socket 已分配文件描述符，供事件系统地址查找 */
static void ensure_xfd(XNetworkSocketPrivate* priv) {
    if (!priv || !priv->owner) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (s->fd != XFD_INVALID) return;
    s->fd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
    XIODevice_setFd((XIODevice*)priv->owner, s->fd);
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
 * 这些回调在 lwIP 核心线程或超时处理中执行，由 sys_arch_protect 保护。
 * 回调中只设置标志位，实际事件通知由应用线程完成。
 * ================================================================ */

/* TCP 数据接收回调 */
static err_t tcpRecvCb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)err;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s) return ERR_ABRT;
    if (!p) { s->closing = true; s->hasReadData = true;
        push_socket_cq(s, XSocketAct_Read);
        return ERR_OK; }
    int total = p->tot_len;
    int acked = 0;
    if (total > 0 && s->rxBuf) {
        int space = XNETWORK_LWIP_RECV_BUFFER_SIZE - s->rxPos;
        int copy = total < space ? total : space;
        if (copy > 0) {
            pbuf_copy_partial(p, s->rxBuf + s->rxPos, copy, 0);
            s->rxPos += copy;
            s->hasReadData = true;
            push_socket_cq(s, XSocketAct_Read);
        }
        acked = copy;  /* 只确认已缓冲的字节数，未缓冲的由 lwIP 重传 */
    }
    pbuf_free(p);
    /* tcp_recved 延迟到 XNetwork_socketContinueRead 中调用，避免窗口管理过早 */
    return ERR_OK;
}

/* TCP 发送完成回调 */
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (s) { s->writeFinished += len; s->hasWriteDone = true; }
    push_socket_cq(s, XSocketAct_Write);
    return ERR_OK;
}

/* TCP 错误回调 */
static void tcpErrCb(void* arg, err_t err) {
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (s) {
        s->closing = true; s->hasError = true;
        s->lastErr = err; g_state.lastError = XNetworkLwip_err_to_errno(err);
        s->tpcb = NULL;
        s->connected = false;  /* 标记为未连接，确保 Connect 通知走失败分支 */
        push_socket_cq(s, XSocketAct_Connect);
    }
}

/* TCP 轮询回调（保持连接，防超时断开）
 * 参数 interval 为 tcp_poll 的调用间隔，单位 0.5 秒。
 * interval=2 表示 1 秒间隔（每秒轮询一次） */
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
        g_state.lastError = XNetworkLwip_err_to_errno(err);
        LWIP_DBG("[TCP连接] 连接失败, err=%d\n", (int)err);
    }
    push_socket_cq(s, XSocketAct_Connect);
    return ERR_OK;
}

/* TCP 接受新连接回调（服务端）
 * 为客户端 Socket 分配私有数据，存入服务端 pendingAccept 字段 */
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err) {
    (void)err;
    XNetworkSocketPrivateLwip* ss = (XNetworkSocketPrivateLwip*)arg;
    if (!ss || !newPcb) return ERR_ABRT;

    /* 为新客户端分配 Socket 私有数据结构 */
    XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*cs));
    if (!cs) return ERR_MEM;
    memset(cs, 0, sizeof(*cs));
    cs->fd = XFD_INVALID;
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

    /* 存入服务端的待领取队列 */
    ss->pendingAccept = cs;
    ss->hasAccept = true;
    push_socket_cq(ss, XSocketAct_Accept);
    LWIP_DBG("[TCP服务] 新客户端连接\n");
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
        if (copy > 0) {
            pbuf_copy_partial(p, s->rxBuf + s->rxPos, copy, 0);
            s->rxPos += copy;
            s->hasReadData = true;
            push_socket_cq(s, XSocketAct_Read);
        }
    }
    pbuf_free(p);
}


/* ================================================================
 * 平台初始化/清理
 * ================================================================ */

void XNetwork_ensureInit(void) {
    if (g_state.inited) { g_state.ref++; return; }
    LWIP_DBG("[lwIP初始化] 开始初始化 lwIP 协议栈...\n");

    /* 初始化 lwIP 协议栈：内存池、pbuf、TCP/UDP/IP/ARP 等 */
    /* Initialize sys_arch layer (create core recursive mutex g_coreLock).
     * NO_SYS=1: lwip_init() does NOT call sys_init(), so we must call it here.
     * NO_SYS=0: lwip_init() also calls it, but sys_init() is idempotent (safe). */
#if NO_SYS
    sys_init();
    lwip_init();
#else
    tcpip_init(NULL, NULL);
#endif

    /* 平台网卡初始化：Npcap / TAP / 硬件 MAC */
    struct netif* nif = XNetworkLwip_platform_init();
    if (nif) {
        g_state.defaultNetif = nif;
        LWIP_DBG("[lwIP初始化] 平台网卡初始化成功\n");
    } else {
        LWIP_DBG("[lwIP初始化] 警告：平台网卡初始化失败，可能无法通信\n");
    }

    g_state.inited = 1;
    g_state.ref = 1;
    LWIP_DBG("[lwIP初始化] 完成，引用计数=%d\n", g_state.ref);
}

void XNetwork_cleanup(void) {
    if (!g_state.inited) return;
    g_state.ref--;
    if (g_state.ref > 0) return;

    LWIP_DBG("[lwIP清理] 开始清理...\n");

    /* 平台网卡清理 */
    XNetworkLwip_platform_deinit();

    g_state.inited = 0;
    g_state.defaultNetif = NULL;
    LWIP_DBG("[lwIP清理] 完成\n");
}

/* 错误信息 */
int XNetwork_lastError(void) { return g_state.lastError; }
char* XNetwork_errorString(int errorCode) {
    char* s = (char*)XMalloc_System(64);
    if (s) snprintf(s, 64, "lwIP error: %d", errorCode);
    return s;
}

/* ================================================================
 * Socket 创建/销毁
 * ================================================================ */

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner) {
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->fd = XFD_INVALID;
    s->base.owner = owner;
    s->base.notifiers = XVector_create(sizeof(void*));
    s->rxBuf = (char*)XMalloc_System(XNETWORK_LWIP_RECV_BUFFER_SIZE);
    if (!s->rxBuf) { XFree_System(s); return NULL; }
    return (XNetworkSocketPrivate*)s;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv) {
    if (!priv) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);

    /* 关闭 lwIP PCB，需要持有核心锁 */
    if (s->tpcb || s->upcb) {
        XNetLwipCoreLock prot = XNET_LWIP_LOCK();
        if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
        if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
        XNET_LWIP_UNLOCK(prot);
    }

    /* 释放资源 */
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (s->base.notifiers) XVector_delete_base(s->base.notifiers);

    /* 清理未领取的 Accept 连接 */
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
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv) {
    return priv ? L4P(priv)->connected : false;
}


/* ================================================================
 * Socket ??
 *
 * 基于 lwIP Raw API 回调的 TCP 服务端绑定（非阻塞模式）。
 * 回调模式中 XNetwork_socketBind 分配 TCP PCB 并立刻返回。
 * ================================================================ */
uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType) {
    (void)reuseAddr; (void)shareAddr;  /* lwIP Raw API 不支持 SO_REUSEADDR */
    if (!priv) return 0;
    XNetwork_ensureInit();
    XNetworkSocketPrivateLwip* s = L4P(priv);
    s->sockType = (int)sockType;

    /* 地址转换 */
    ip_addr_t bindAddr;
    if (address) addr_to_ip(address, &bindAddr); else bindAddr = *IP_ADDR_ANY;

    err_t err;
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
            LWIP_DBG("[绑定] TCP服务端 port=%d backlog=%d\n", (int)port, XNETWORK_LWIP_MAX_LISTEN_BACKLOG);
    } else {
        /* UDP：创建 PCB 并绑定 */
        s->upcb = udp_new();
        if (!s->upcb) return 0;
        if (udp_bind(s->upcb, &bindAddr, port) != ERR_OK) {
            udp_remove(s->upcb); s->upcb = NULL; return 0;
        }
        udp_recv(s->upcb, udpRecvCb, s);
        s->connected = true;
        LWIP_DBG("[绑定] UDP port=%d\n", (int)port);
    }
    ensure_xfd(priv);
    return port;
}


/* ================================================================
 * 网络就绪保障 + 异步 DNS 解析
 *
 * 问题背景：
 *   1. dns_gethostbyname 传 NULL 回调时，只能解析已缓存或 IP 字符串，
 *      对未缓存域名返回 ERR_INPROGRESS 后永远不会通知完成。
 *   2. Windows 上 DHCP 通常无法完成（宿主机已持有 IP 租约），
 *      netif 仍为 0.0.0.0，DNS 查询无源 IP；且无 DNS 服务器。
 *
 * 解决方案：
 *   ensure_network_ready(): 等待/强制网卡获取 IP + 设置 fallback DNS 服务器
 *   lwip_resolve_name():    dns_gethostbyname + 回调 + 轮询等待结果
 * ================================================================ */

/* 判断 netif 是否拥有有效的非回环 IPv4 地址 */
static bool netif_has_valid_ipv4(struct netif* n) {
    if (!n || ip_addr_isany(&n->ip_addr) || !(n->flags & NETIF_FLAG_UP)) return false;
    uint32_t hip = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&n->ip_addr)));
    /* 跳过 127.x.x.x (回环) 和 169.254.x.x (APIPA) */
    if ((hip >> 24) == 127 || (hip >> 16) == 0xA9FE) return false;
    return true;
}

/* 确保网络就绪：
 *   1. 等待网卡通过 DHCP 获取有效 IP（DHCP 完成时 lwIP 自动设置 IP/网关/DNS）
 *   2. 确保有默认路由 */
static void ensure_network_ready(uint32_t timeoutMs) {
    struct netif* n;
    bool hasIp = false;
    uint32_t start = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF);

    /* 等待至少一个网卡通过 DHCP 获取有效 IP */
    while (!hasIp) {
        uint32_t elapsed = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF) - start;
        if (elapsed >= timeoutMs) break;
        for (n = netif_list; n; n = n->next) {
            if (netif_has_valid_ipv4(n)) { hasIp = true; break; }
        }
        if (hasIp) break;
#if NO_SYS
        {
            XNetLwipCoreLock prot = XNET_LWIP_LOCK();
            XNetworkLwip_pollPcap();
            XNET_LWIP_UNLOCK(prot);
        }
#else
        XNetworkLwip_pollPcap();
#endif
        XThread_msleep(50);
    }

    if (hasIp) {
        LWIP_DBG("[网络就绪] DHCP已完成，网卡已获取IP\n");
    } else {
        LWIP_DBG("[网络就绪] 等待DHCP超时(%dms)，无可用IP\n", (int)timeoutMs);
    }

    /* 确保有默认路由 */
    if (!netif_default) {
        for (n = netif_list; n; n = n->next) {
            if (netif_has_valid_ipv4(n)) {
                netif_set_default(n);
                LWIP_DBG("[网络就绪] 设置默认网卡: %s\n", ip4addr_ntoa(ip_2_ip4(&n->ip_addr)));
                break;
            }
        }
    }
}

/* DNS 异步解析回调上下文 */
typedef struct {
    ip_addr_t ip;
    volatile bool done;
    volatile bool found;
} DnsResolveCtx;

/* dns_gethostbyname 完成回调 */
static void dns_found_cb(const char* name, const ip_addr_t* ipaddr, void* arg) {
    (void)name;
    DnsResolveCtx* ctx = (DnsResolveCtx*)arg;
    if (ipaddr) {
        ctx->ip = *ipaddr;
        ctx->found = true;
    } else {
        ctx->found = false;
    }
    ctx->done = true;
}

/* 异步解析域名：dns_gethostbyname + 回调 + 轮询等待
 * @param name       域名或 IP 字符串
 * @param outIp      输出解析结果
 * @param timeoutMs  超时时间
 * @return true=解析成功 */
static bool lwip_resolve_name(const char* name, ip_addr_t* outIp, uint32_t timeoutMs) {
    ip_addr_t ip;
    DnsResolveCtx ctx;
    err_t e;

    ip4_addr_set_any(ip_2_ip4(&ip));
    ctx.done = false;
    ctx.found = false;
    ip4_addr_set_any(ip_2_ip4(&ctx.ip));

#if NO_SYS
    {
        XNetLwipCoreLock prot = XNET_LWIP_LOCK();
        e = dns_gethostbyname(name, &ip, dns_found_cb, &ctx);
        XNET_LWIP_UNLOCK(prot);
    }
#else
    e = dns_gethostbyname(name, &ip, dns_found_cb, &ctx);
#endif

    if (e == ERR_OK) {
        /* 已缓存或为 IP 字符串，直接返回 */
        *outIp = ip;
        return true;
    }
    if (e == ERR_INPROGRESS) {
        /* 异步查询中，轮询 pcap + 超时处理，等待回调通知 */
        uint32_t start = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF);
        while (!ctx.done) {
            uint32_t elapsed = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF) - start;
            if (elapsed >= timeoutMs) {
                LWIP_DBG("[DNS] 解析超时(%dms): %s\n", (int)timeoutMs, name);
                return false;
            }
#if NO_SYS
            {
                XNetLwipCoreLock prot = XNET_LWIP_LOCK();
                XNetworkLwip_pollPcap();
                XNET_LWIP_UNLOCK(prot);
            }
#else
            XNetworkLwip_pollPcap();
#endif
            XThread_msleep(20);
        }
        if (ctx.found) {
            *outIp = ctx.ip;
            LWIP_DBG("[DNS] 解析成功: %s -> %s\n", name, ipaddr_ntoa(outIp));
            return true;
        }
        LWIP_DBG("[DNS] 解析失败(域名不存在): %s\n", name);
        return false;
    }

    /* ERR_ARG 或其他错误：尝试直接当 IP 地址解析 */
    if (ipaddr_aton(name, &ip)) {
        *outIp = ip;
        return true;
    }
    LWIP_DBG("[DNS] dns_gethostbyname错误=%d: %s\n", (int)e, name);
    return false;
}

/* ================================================================
 * Socket 连接
 * ================================================================ */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const XString* hostName,
                            uint16_t port, XNetworkProtocol protocol,
                            XNetworkSocketType sockType) {
    (void)protocol;
    if (!priv || !hostName) return false;
    const char* hostStr = XString_toUtf8(hostName);
    if (!hostStr) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetwork_ensureInit();
    s->sockType = (int)sockType;

    /* 确保网卡有 IP + DNS 服务器就绪（等待 DHCP 或强制 fallback） */
    ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS);

    /* 异步 DNS 解析（带回调 + 轮询等待） */
    ip_addr_t ip;
    if (!lwip_resolve_name(hostStr, &ip, XNETWORK_LWIP_DNS_TIMEOUT_MS)) {
        LWIP_DBG("[连接] DNS和IP解析均失败\n");
        return false;
    }
    LWIP_DBG("[连接] 目标IP解析成功\n");
    LWIP_DBG("[连接] 默认网卡=%s\n",
             netif_default ? ip4addr_ntoa(ip_2_ip4(&netif_default->ip_addr)) : "NULL");

    err_t err;
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
        /* UDP：创建 PCB 并直接设置目标地址 */
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
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
    XNET_LWIP_UNLOCK(prot);
    s->connected = false;
}


/* ================================================================
 * 数据读写
 * ================================================================ */

/* 从 Socket 读取数据到用户缓冲区 */
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
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();

    err_t err;
    if (sockType == XNetwork_Tcp) {
        if (!s->tpcb) { XNET_LWIP_UNLOCK(prot); return -1; }
        u16_t sndBuf = tcp_sndbuf(s->tpcb);
        u16_t toWrite = (u16_t)((len > sndBuf) ? sndBuf : len);
        if (toWrite == 0) { XNET_LWIP_UNLOCK(prot); return -6; }
        err_t e = tcp_write(s->tpcb, buf, toWrite, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) {
            tcp_output(s->tpcb);
            XNET_LWIP_UNLOCK(prot);
            return toWrite;
        }
        XNET_LWIP_UNLOCK(prot);
        return -1;
    } else {
        if (!s->upcb) { XNET_LWIP_UNLOCK(prot); return -1; }
        ip_addr_t dst;
        if (destAddr) { addr_to_ip(destAddr, &dst); }
        else { dst = s->upcb->remote_ip; destPort = s->upcb->remote_port; }
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (!p) { XNET_LWIP_UNLOCK(prot); return -1; }
        memcpy(p->payload, buf, (size_t)len);
        err_t e = udp_sendto(s->upcb, p, &dst, destPort);
        pbuf_free(p); XNET_LWIP_UNLOCK(prot);
        return (e == ERR_OK) ? len : -1;
    }
}

/* ================================================================
 * 事件处理
 * ================================================================ */

/* 处理 Socket 事件：清除标志位并返回是否有事件 */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event) {
    if (!priv || !event) return false;
    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    bool hasEvent = false;

    /* 仅清除与当前事件类型匹配的标志位，避免丢失其他类型的事件。
     * 例如：Connect 事件到达时只清除 connectDone/hasError，
     * 不清除 hasReadData--后者由后续 Read 事件处理。 */
    if ((sockAct->actType & XSocketAct_Connect) &&
        (s->connectDone || s->hasError)) {
        s->connectDone = false;
        s->hasError = false;
        s->closing = false;  /* 连接错误/完成时一并清除关闭标志 */
        hasEvent = true;
    }
    if ((sockAct->actType & XSocketAct_Read) &&
        (s->hasReadData || s->closing)) {
        s->hasReadData = false;
        s->closing = false;
        s->rxTotal = s->rxPos;  /* 更新已读字节数，供上层获取 */
        hasEvent = true;
    }
    if ((sockAct->actType & XSocketAct_Write) && s->hasWriteDone) {
        s->hasWriteDone = false;
        s->lastWriteFinished = s->writeFinished;  /* 记录已确认字节数 */
        s->writeFinished = 0;                      /* 重置计数器 */
        hasEvent = true;
    }
    if ((sockAct->actType & XSocketAct_Accept) && s->hasAccept) {
        s->hasAccept = false;
        hasEvent = true;
    }
    XNET_LWIP_UNLOCK(prot);
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
    return priv ? L4P(priv)->lastWriteFinished : 0;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp) {
    if (!priv) return;
    (void)isUdp;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    size_t consumed = (size_t)s->rxTotal;
    if (s->rxPos > (int)consumed) {
        /* 如果还有未消费的数据，紧凑移动到缓冲区头部 */
        size_t remaining = (size_t)s->rxPos - consumed;
        memmove(s->rxBuf, s->rxBuf + consumed, remaining);
        s->rxPos = (int)remaining;
        s->rxTotal = 0;
        s->hasReadData = true;  /* 仍有剩余数据可读 */
        push_socket_cq(s, XSocketAct_Read);  /* 投递 Read 事件通知上层 */
    }
    else {
        /* 全部已消费，清空缓冲区 */
        s->rxPos = 0;
        s->rxTotal = 0;
    }
    /* 通知 lwIP 数据已消费，重新打开 TCP 接收窗口 */
    if (consumed > 0 && s->tpcb) tcp_recved(s->tpcb, (u16_t)consumed);
    XNET_LWIP_UNLOCK(prot);
}

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr) {
    if (!priv) return (XServerHandle)(-1);
    (void)backlog;
    uint16_t actualPort = XNetwork_socketBind(priv, addr, port, reuseAddr, false, XNetwork_Tcp);
    if (actualPort == 0) return (XServerHandle)(-1);
    return (XServerHandle)(intptr_t)priv;
}

bool XNetwork_serverAccept(XNetworkSocketPrivate* priv) {
    /* lwIP 通过 tcp_accept 回调接受连接（在 socketBind 中已设置），此处无需操作 */
    (void)priv;
    return true;
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
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    XNET_LWIP_UNLOCK(prot);
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
    LWIP_DBG("[TCP服务] 领取客户端Socket=%p\n", (void*)cs);
    return (XSocketHandle)(intptr_t)cs;
}

XVector* XNetwork_lookupName(const XString* name) {
    if (!name) return NULL;
    const char* nameStr = XString_toUtf8(name);
    if (!nameStr) return NULL;
    XNetwork_ensureInit();

    /* 确保网卡有 IP + DNS 服务器就绪 */
    ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS);

    /* 异步 DNS 解析（带回调 + 轮询等待） */
    ip_addr_t ip;
    if (!lwip_resolve_name(nameStr, &ip, XNETWORK_LWIP_DNS_TIMEOUT_MS)) return NULL;

    XVector* vec = XVector_create(sizeof(XHostAddress));
    if (!vec) return NULL;
    XContainerSetDataMoveMethod(vec, XHostAddress_move_base);
    XContainerSetDataCopyMethod(vec, XHostAddress_copy_base);
    XContainerSetDataDeinitMethod(vec, XHostAddress_deinit_base);

    XHostAddress addr;
    XHostAddress_init(&addr);
    ip_to_addr(&ip, &addr);
    XVector_push_back_1_base(vec, &addr);

    return vec;
}

XString* XNetwork_localHostName(void) {
    return XString_create_utf8("lwip-device");
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
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    err_t e = udp_sendto(s->upcb, p, &dst, port);
    XNET_LWIP_UNLOCK(prot);
    pbuf_free(p);
    return (e == ERR_OK) ? sz : -1;
}

bool XNetwork_getSystemProxy(const XString* url, XNetworkProxy* out) {
    (void)url; (void)out; return false;
}
int XNetwork_gssapiAuth(const XString* name, const XByteArray* in, XByteArray* out, void** ctx) {
    (void)name; (void)in; (void)out; (void)ctx; return -1;
}

struct netif* XNetworkLwip_defaultNetif(void) { return g_state.defaultNetif; }
void XNetworkLwip_setDefaultNetif(struct netif* n) { g_state.defaultNetif = n; }
/* ================================================================
 * 写入延续 - lwIP 模式下写入已通过 tcp_write/tcp_output 同步完成，
 * 环形缓冲区中待发送的明文已在 xssl_v_writeData -> BIO -> parent writeData
 * 路径中完成加密并发送，无需额外操作。
 * ================================================================ */
void XNetwork_socketContinueWrite(XNetworkSocketPrivate* priv, XRingBuffer* ringBuffer, bool isUdp)
{
    if (!priv || !ringBuffer) return;
    (void)isUdp;
    /* lwIP: 写入通过 tcp_write/tcp_output 同步完成，
     * 环形缓冲区数据已在 XNetwork_socketWrite 路径中处理完毕。 */
}
#endif /* XNETWORK_USE_LWIP */
