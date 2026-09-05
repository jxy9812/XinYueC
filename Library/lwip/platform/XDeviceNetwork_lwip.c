/**
 * @file XDeviceNetwork_lwip.c
 * @brief lwIP 平台网络实现（平台无关 + Raw API 回调模式）
 *
 * 架构分层：
 *   XDeviceNetwork.h                             -> 统一平台抽象接口
 *   +-- XDeviceNetwork_win32.c                   -> Windows IOCP 实现
 *   +-- XDeviceNetwork_lwip.c（本文件）          -> lwIP 适配层（平台无关）
 *       +-- XDeviceNetwork_lwip_win32.c          -> Npcap 虚拟网卡（Windows 平台）
 *
 * 运行原理：
 *   1. lwip_init() 初始化 lwIP 协议栈
 *   2. XTimeWheelGroup 定时器每 LWIP_TICK_MS 毫秒触发（默认 5ms），
 *      a) 轮询虚拟网卡数据包（XDeviceNetworkLwip_pollPcap）
 *      b) lwIP 定时器由 LWIP_TIMERS_CUSTOM 直接对接 XTimeWheelGroup，无需轮询
 *      c) 向应用线程投递事件通知
 *   3. 应用线程通过 Raw API 操作，sys_arch_protect 提供递归锁保护
 *
 * 基于 XSync + XMemory 系统实现，平台无关。
 * 内存分配统一使用 XMalloc_System / XRealloc_System / XFree_System。
 * 定时器使用 XTimeWheelGroup 全局时间轮。
 * 随机数使用 XRandomGenerator_system()。
 */

#include "XNetwork_config.h"
#ifdef XNETWORK_USE_LWIP

#include "XDeviceNetwork.h"
#include "XAbstractSocket.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XRingBuffer.h"
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
#include "lwip/priv/tcp_priv.h"
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
#if LWIP_RAW
#include "lwip/raw.h"
#include "lwip/prot/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#if LWIP_IPV6
#include "lwip/prot/icmp6.h"
#include "lwip/ip6.h"
#endif
#endif

#include "CXinYueConfig.h"  /* XAbstractNetIoRing_ON */
#if XAbstractNetIoRing_ON
#include "XAbstractNetIoRing.h"  /* CQ push from callbacks */
#endif
#include <string.h>
#include <stdio.h>

/* 仅供 lwIP 适配层和所选平台网卡实现之间链接，不属于 XDeviceNetwork 公共头。 */
struct netif* XDeviceNetworkLwip_platform_init(void);
void XDeviceNetworkLwip_platform_deinit(void);
int XDeviceNetworkLwip_err_to_errno(int errorCode);
void XDeviceNetworkLwip_pollPcap(void);

typedef enum XMulticastOp {
    XMC_Join, XMC_Leave, XMC_SetIf, XMC_GetIf,
    XMC_SetTtl, XMC_GetTtl, XMC_SetLoop, XMC_GetLoop
} XMulticastOp;

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
#define L4P(p) ((XDeviceNetworkContextLwip*)(p))
/* lwIP 网卡轮询周期，可通过 XNETWORK_LWIP_TICK_MS 配置 */
#define LWIP_TICK_MS XNETWORK_LWIP_TICK_MS

typedef struct XDeviceNetworkContextLwip XDeviceNetworkContextLwip;

/* ================================================================
 * 全局状态
 * ================================================================ */
/* lwIP 全局状态（位域压缩，减少内存占用）
 * 原本为 4 个独立静态变量（约 24~32 字节），合并为 1 个结构体（16 字节）。
 * 原本为 4 个独立静态变量（约 24~32 字节），合并为 1 个结构体（8 字节）。
 * - lastError:    错误码范围 -15~0，8 位有符号足够
 * - ref:          引用计数通常 1~5，7 位（最大 127）足够
 * - inited:       初始化标志，1 位足够 */
static struct {
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
typedef struct XDeviceNetworkContextLwip {
    XDeviceNetworkContext base;    /* 基类：owner + notifiers (16 字节) */
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
    uint16_t rxCapacity;           /* 懒分配接收缓冲容量，复用原对齐空隙 */
    /* 位域压缩：7 个布尔(7位) + sockType(2位) + lastErr(8位) = 17 位，合并到 4 字节
     * 原本 7 个 bool(7字节) + sockType(4字节) + lastErr(4字节) + 对齐填充
     * 现压缩为 4 字节，结构体从 ~104 字节减至 88 字节 */
    unsigned isServer    : 1;      /* 是否为服务端 */
    unsigned closing     : 1;      /* 是否正在关闭 */
    unsigned connectDone : 1;      /* 连接完成标记 */
    unsigned hasReadData : 1;      /* 有可读数据标记 */
    unsigned hasWriteDone: 1;      /* 写入完成标记 */
    unsigned hasAccept   : 1;      /* 有新客户端连接标记 */
    unsigned hasError    : 1;      /* 有错误发生标记 */
    unsigned sockType    : 2;      /* Socket 类型 (0=TCP, 1=UDP) */
    unsigned listenBacklog : 8;    /* TCP 服务端监听队列，0=使用配置默认值 */
    int      lastErr     : 8;      /* 最后一次 lwIP 错误码 (err_t, -16~0) */
} XDeviceNetworkContextLwip;

/* Raw API 回调在描述符接管辅助函数之前声明。 */
static err_t tcpRecvCb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len);
static void tcpErrCb(void* arg, err_t err);
static err_t tcpPollCb(void* arg, struct tcp_pcb* pcb);
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err);

/* 接收缓冲按需分配，未收数据的 socket 不占用缓冲预算。 */
static bool reserve_rx_buffer(XDeviceNetworkContextLwip* s, u16_t required)
{
    char* resized;

    if (!s || required <= s->rxCapacity) return true;
    resized = (char*)XRealloc_System(s->rxBuf, (size_t)required);
    if (!resized) return false;
    s->rxBuf = resized;
    s->rxCapacity = required;
    return true;
}

/* 优先使用配置容量以吸收连续报文、提高大内存设备吞吐；若该分配因
 * 内存压力失败，退化到只容纳当前整包，后续依靠 TCP 背压继续传输。 */
static bool reserve_tcp_rx_buffer(XDeviceNetworkContextLwip* s, u16_t required)
{
    const u16_t configured = (u16_t)XNETWORK_LWIP_RECV_BUFFER_SIZE;

    if (reserve_rx_buffer(s, configured)) return true;
    return reserve_rx_buffer(s, required);
}

/* tcp_close() can leave an established PCB alive until a later ACK or timer
 * tick. Its Raw API callbacks must not retain a private socket that is about
 * to be released. */
static void dispose_tcp_pcb(struct tcp_pcb** pcb)
{
    struct tcp_pcb* current;

    if (!pcb || !*pcb) return;
    current = *pcb;
    tcp_arg(current, NULL);
    if (current->state == LISTEN) {
        tcp_accept(current, NULL);
    } else {
        tcp_recv(current, NULL);
        tcp_sent(current, NULL);
        tcp_err(current, NULL);
        tcp_poll(current, NULL, 0);
    }
    if (tcp_close(current) != ERR_OK) {
        tcp_abort(current);
    }
    *pcb = NULL;
}

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
static void push_socket_cq(XDeviceNetworkContextLwip* s, uint32_t actType) {
#if XAbstractNetIoRing_ON
    if (!s || !s->base.m_owner || actType == 0) return;

    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    if (!ring) return;

    XFd fd = s->fd;
    if (fd == XFD_INVALID) {
        fd = s->base.m_base.m_fd;
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

#if LWIP_RAW && (LWIP_IPV4 || LWIP_IPV6)
typedef struct XDeviceNetworkIcmpWait {
    volatile bool done;
    volatile bool matched;
    uint16_t identifier;
    uint16_t sequence;
    bool isIpv6;
} XDeviceNetworkIcmpWait;

static u8_t xnetwork_icmp_recv(void* argument, struct raw_pcb* pcb,
                               struct pbuf* packet, const ip_addr_t* source)
{
    XDeviceNetworkIcmpWait* wait = (XDeviceNetworkIcmpWait*)argument;
    struct icmp_echo_hdr echo;
    u16_t headerLength;
    (void)pcb;
    (void)source;
    if (!wait || !packet || packet->tot_len < sizeof(echo)) return 0;
    if (wait->isIpv6) {
#if LWIP_IPV6
        headerLength = IP6_HLEN;
#else
        return 0;
#endif
    } else {
#if LWIP_IPV4
        struct ip_hdr* ipHeader = (struct ip_hdr*)packet->payload;
        headerLength = IPH_HL_BYTES(ipHeader);
        if (headerLength < IP_HLEN) return 0;
#else
        return 0;
#endif
    }
    if (packet->tot_len < headerLength + sizeof(echo)) return 0;
    if (pbuf_copy_partial(packet, &echo, sizeof(echo), headerLength) != sizeof(echo)) return 0;
    if (ICMPH_TYPE(&echo) != (wait->isIpv6 ? ICMP6_TYPE_EREP : ICMP_ER) ||
        ICMPH_CODE(&echo) != 0 ||
        lwip_ntohs(echo.id) != wait->identifier || lwip_ntohs(echo.seqno) != wait->sequence)
        return 0;
    wait->matched = true;
    wait->done = true;
    pbuf_free(packet);
    return 1;
}
#endif

bool XDeviceNetwork_icmpEchoSupported(void)
{
#if LWIP_RAW && (LWIP_IPV4 || LWIP_IPV6)
    return true;
#else
    return false;
#endif
}

bool XDeviceNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
                       uint16_t sequence, const void* payload, size_t payloadSize,
                       int timeoutMilliseconds, uint32_t* elapsedMilliseconds)
{
#if LWIP_RAW && (LWIP_IPV4 || LWIP_IPV6)
    struct raw_pcb* pcb = NULL;
    struct pbuf* packet = NULL;
    XDeviceNetworkIcmpWait wait;
    ip_addr_t target;
    size_t packetSize;
    uint64_t start;
    uint64_t deadline;
    bool result = false;
    bool isIpv6;

    if (!address ||
        (XHostAddress_protocol(address) != XHostAddress_IPv4Protocol &&
         XHostAddress_protocol(address) != XHostAddress_IPv6Protocol) ||
        (payloadSize && !payload) || payloadSize > 1400u || timeoutMilliseconds <= 0)
        return false;
    isIpv6 = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol);
#if LWIP_IPV6
    if (isIpv6) {
        struct icmp6_echo_hdr echo;
        packetSize = sizeof(struct icmp6_echo_hdr) + payloadSize;
        packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)packetSize, PBUF_RAM);
        if (!packet) return false;
        memset(&echo, 0, sizeof(echo));
        ICMPH_TYPE_SET(&echo, ICMP6_TYPE_EREQ);
        ICMPH_CODE_SET(&echo, 0);
        echo.id = lwip_htons(identifier);
        echo.seqno = lwip_htons(sequence);
        if (pbuf_take(packet, &echo, sizeof(echo)) != ERR_OK ||
            (payloadSize && pbuf_take_at(packet, payload, (u16_t)payloadSize,
                                         sizeof(echo)) != ERR_OK))
            goto cleanup;
        /* checksum is computed by raw_sendto_if_src via chksum_reqd/chksum_offset */
    } else
#endif
    {
#if LWIP_IPV4
        struct icmp_echo_hdr echo;
        packetSize = sizeof(struct icmp_echo_hdr) + payloadSize;
        packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)packetSize, PBUF_RAM);
        if (!packet) return false;
        memset(&echo, 0, sizeof(echo));
        ICMPH_TYPE_SET(&echo, ICMP_ECHO);
        ICMPH_CODE_SET(&echo, 0);
        echo.id = lwip_htons(identifier);
        echo.seqno = lwip_htons(sequence);
        if (pbuf_take(packet, &echo, sizeof(echo)) != ERR_OK ||
            (payloadSize && pbuf_take_at(packet, payload, (u16_t)payloadSize,
                                         sizeof(echo)) != ERR_OK))
            goto cleanup;
        echo.chksum = inet_chksum_pbuf(packet);
        if (pbuf_take(packet, &echo, sizeof(echo)) != ERR_OK) goto cleanup;
#else
        goto cleanup;
#endif
    }
    memset(&wait, 0, sizeof(wait));
    wait.identifier = identifier;
    wait.sequence = sequence;
    wait.isIpv6 = isIpv6;
    addr_to_ip(address, &target);
    {
        XNetLwipCoreLock lock = XNET_LWIP_LOCK();
#if LWIP_IPV6
        if (isIpv6) {
            pcb = raw_new_ip6(IP6_NEXTH_ICMP6);
            if (pcb) {
                pcb->chksum_reqd = 1;
                pcb->chksum_offset = 2;
            }
        } else
#endif
        {
            pcb = raw_new(IP_PROTO_ICMP);
        }
        if (pcb) {
            raw_recv(pcb, xnetwork_icmp_recv, &wait);
            if (raw_sendto(pcb, packet, &target) != ERR_OK) {
                raw_remove(pcb);
                pcb = NULL;
            }
        }
        XNET_LWIP_UNLOCK(lock);
    }
    if (!pcb) goto cleanup;
    start = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    deadline = start + (uint64_t)timeoutMilliseconds;
    while (!wait.done) {
        uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        if (now >= deadline) break;
#if NO_SYS
        {
            XNetLwipCoreLock lock = XNET_LWIP_LOCK();
            XDeviceNetworkLwip_pollPcap();
            XNET_LWIP_UNLOCK(lock);
        }
#else
        XDeviceNetworkLwip_pollPcap();
#endif
        XThread_msleep(5);
    }
    result = wait.matched;
    if (result && elapsedMilliseconds)
        *elapsedMilliseconds = (uint32_t)((uint64_t)XDateTime_currentMSecsSinceEpoch() - start);
cleanup:
    if (pcb) {
        XNetLwipCoreLock lock = XNET_LWIP_LOCK();
        raw_remove(pcb);
        XNET_LWIP_UNLOCK(lock);
    }
    if (packet) pbuf_free(packet);
    return result;
#else
    (void)address;
    (void)identifier;
    (void)sequence;
    (void)payload;
    (void)payloadSize;
    (void)timeoutMilliseconds;
    (void)elapsedMilliseconds;
    return false;
#endif
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

/* Keep XAbstractSocket endpoint properties consistent with the POSIX and
 * Windows backends after a Raw API connection completes or is adopted. */
static void syncSocketEndpoints(XDeviceNetworkContext* priv)
{
    if (!priv || !priv->m_owner) return;
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (s->isServer) return;

    XAbstractSocket* socket = (XAbstractSocket*)priv->m_owner;
    XHostAddress endpoint;
    XHostAddress_init(&endpoint);
    if (s->tpcb) {
        ip_to_addr(&s->tpcb->local_ip, &endpoint);
        XAbstractSocket_setLocalAddress(socket, &endpoint);
        XAbstractSocket_setLocalPort(socket, s->tpcb->local_port);
        ip_to_addr(&s->tpcb->remote_ip, &endpoint);
        XAbstractSocket_setPeerAddress(socket, &endpoint);
        XAbstractSocket_setPeerPort(socket, s->tpcb->remote_port);
    } else if (s->upcb) {
        ip_to_addr(&s->upcb->local_ip, &endpoint);
        XAbstractSocket_setLocalAddress(socket, &endpoint);
        XAbstractSocket_setLocalPort(socket, s->upcb->local_port);
        ip_to_addr(&s->upcb->remote_ip, &endpoint);
        XAbstractSocket_setPeerAddress(socket, &endpoint);
        XAbstractSocket_setPeerPort(socket, s->upcb->remote_port);
    }
    XHostAddress_deinit_base((XClass*)&endpoint);
}

static struct tcp_pcb* createTcpPcb(const ip_addr_t* address)
{
#if LWIP_IPV6
    return tcp_new_ip_type(address ? IP_GET_TYPE(address) : IPADDR_TYPE_ANY);
#else
    (void)address;
    return tcp_new();
#endif
}

static struct udp_pcb* createUdpPcb(const ip_addr_t* address)
{
#if LWIP_IPV6
    return udp_new_ip_type(address ? IP_GET_TYPE(address) : IPADDR_TYPE_ANY);
#else
    (void)address;
    return udp_new();
#endif
}

/* 将 lwIP 回调状态绑定到已由 XDevice_open 分配的唯一设备描述符。 */
static void ensure_xfd(XDeviceNetworkContext* priv) {
    if (!priv) return;
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (s->fd != XFD_INVALID) return;
    s->fd = priv->m_base.m_fd;
}

/*
 * lwIP 没有原生 socket descriptor。对外暴露 XFd 表索引，索引的 handle
 * 指向 Raw API 私有对象；接管时转移 tcp_pcb 的所有权，而不是转换裸指针。
 */
static XDeviceNetworkContextLwip* descriptor_private(XFd fd)
{
    if (fd == XFD_INVALID || !XFd_get(fd)) return NULL;
    return (XDeviceNetworkContextLwip*)XFd_handle(fd);
}

static void release_detached_private(XDeviceNetworkContextLwip* s)
{
    if (!s || s->base.m_owner) return;
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (s->base.m_notifiers) XVector_delete_base((XClass*)s->base.m_notifiers);
    XFree_System(s);
}

static bool adopt_connected_descriptor(XDeviceNetworkContextLwip* target,
                                       XFd sourceFd)
{
    XDeviceNetworkContextLwip* source = descriptor_private(sourceFd);
    XNetLwipCoreLock prot;

    if (!target || !source || source == target || source->isServer ||
        !source->tpcb || source->pendingAccept || target->tpcb || target->upcb) {
        return false;
    }

    prot = XNET_LWIP_LOCK();
    target->tpcb = source->tpcb;
    target->base.m_connected = source->base.m_connected;
    target->closing = source->closing;
    target->hasReadData = source->hasReadData;
    target->hasWriteDone = source->hasWriteDone;
    target->hasError = source->hasError;
    target->sockType = XDeviceNetwork_Tcp;
    /* target 尚未接管任何 PCB；直接转移接收缓冲区，避免 accepted socket
     * 接管时重新分配和复制一份数据。source 随后释放 target 的旧缓冲。 */
    {
        char* targetRxBuf = target->rxBuf;
        uint16_t targetRxCapacity = target->rxCapacity;
        target->rxBuf = source->rxBuf;
        target->rxCapacity = source->rxCapacity;
        source->rxBuf = targetRxBuf;
        source->rxCapacity = targetRxCapacity;
    }
    target->rxPos = source->rxPos;
    target->rxTotal = source->rxTotal < target->rxPos ? source->rxTotal : target->rxPos;

    tcp_arg(target->tpcb, target);
    tcp_recv(target->tpcb, tcpRecvCb);
    tcp_sent(target->tpcb, tcpSentCb);
    tcp_err(target->tpcb, tcpErrCb);
    tcp_poll(target->tpcb, tcpPollCb, 2);

    source->tpcb = NULL;
    source->base.m_connected = false;
    source->closing = false;
    source->hasReadData = false;
    source->hasWriteDone = false;
    source->hasError = false;
    source->rxPos = 0;
    source->rxTotal = 0;
    XNET_LWIP_UNLOCK(prot);

    target->fd = target->base.m_base.m_fd;
    XFd_free(sourceFd);
    source->fd = XFD_INVALID;
    release_detached_private(source);
    return true;
}

static bool adopt_server_descriptor(XDeviceNetworkContextLwip* target,
                                    XFd sourceFd)
{
    XDeviceNetworkContextLwip* source = descriptor_private(sourceFd);
    XNetLwipCoreLock prot;

    if (!target || !source || source == target || !source->isServer ||
        !source->tpcb || source->pendingAccept || target->tpcb || target->upcb) {
        return false;
    }

    prot = XNET_LWIP_LOCK();
    target->tpcb = source->tpcb;
    target->base.m_connected = true;
    target->closing = false;
    target->hasAccept = false;
    target->isServer = true;
    target->sockType = XDeviceNetwork_Tcp;
    tcp_arg(target->tpcb, target);
    tcp_accept(target->tpcb, tcpAcceptCb);

    source->tpcb = NULL;
    source->base.m_connected = false;
    source->hasAccept = false;
    XNET_LWIP_UNLOCK(prot);

    target->fd = target->base.m_base.m_fd;
    XFd_free(sourceFd);
    source->fd = XFD_INVALID;
    release_detached_private(source);
    return true;
}


/* ================================================================
 * 错误码转换：lwIP err_t -> 类 POSIX errno
 * ================================================================ */
int XDeviceNetworkLwip_err_to_errno(int e) {
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
    u16_t total;
    size_t space;

    (void)pcb;
    (void)err;
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (!s) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (!p) { s->closing = true; s->hasReadData = true;
        push_socket_cq(s, XSocketAct_Read);
        return ERR_OK; }

    total = p->tot_len;
    if (total == 0) {
        pbuf_free(p);
        return ERR_OK;
    }

    /* Raw API 不支持“复制一部分后释放整个 pbuf”。空间不足时必须保持
     * pbuf 原样并返回 ERR_MEM，lwIP 才会存入 pcb->refused_data 后重试。
     * 缓冲为空但单个重组 pbuf 较大时，按需扩容以避免永久拒收。 */
    space = s->rxPos < (int)s->rxCapacity
        ? (size_t)s->rxCapacity - (size_t)s->rxPos : 0;
    if ((size_t)total > space) {
        if (s->rxPos != 0 || !reserve_tcp_rx_buffer(s, total)) return ERR_MEM;
    }
    if (pbuf_copy_partial(p, s->rxBuf + s->rxPos, total, 0) != total) {
        return ERR_MEM;
    }
    s->rxPos += (int)total;
    s->hasReadData = true;
    push_socket_cq(s, XSocketAct_Read);
    pbuf_free(p);
    /* tcp_recved 延迟到 XDeviceNetwork_socketContinueRead 中调用，避免窗口管理过早 */
    return ERR_OK;
}

/* TCP 发送完成回调 */
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)pcb;
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (s) { s->writeFinished += len; s->hasWriteDone = true; }
    push_socket_cq(s, XSocketAct_Write);
    return ERR_OK;
}

/* TCP 错误回调 */
static void tcpErrCb(void* arg, err_t err) {
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (s) {
        s->closing = true; s->hasError = true;
        s->lastErr = err; g_state.lastError = XDeviceNetworkLwip_err_to_errno(err);
        s->tpcb = NULL;
        s->base.m_connected = false;  /* 标记为未连接，确保 Connect 通知走失败分支 */
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
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (!s) return ERR_ABRT;
    if (err == ERR_OK) {
        s->base.m_connected = true; s->connectDone = true;
        LWIP_DBG("[TCP连接] 连接成功\n");
    } else {
        s->hasError = true; s->lastErr = err;
        g_state.lastError = XDeviceNetworkLwip_err_to_errno(err);
        LWIP_DBG("[TCP连接] 连接失败, err=%d\n", (int)err);
    }
    push_socket_cq(s, XSocketAct_Connect);
    return ERR_OK;
}

/* TCP 接受新连接回调（服务端）
 * 为客户端 Socket 分配私有数据，存入服务端 pendingAccept 字段 */
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err) {
    (void)err;
    XDeviceNetworkContextLwip* ss = (XDeviceNetworkContextLwip*)arg;
    if (!ss || !newPcb) return ERR_ABRT;

    /* 为新客户端分配 Socket 私有数据结构 */
    XDeviceNetworkContextLwip* cs = (XDeviceNetworkContextLwip*)XMalloc_System(sizeof(*cs));
    if (!cs) return ERR_MEM;
    memset(cs, 0, sizeof(*cs));
    cs->fd = XFd_alloc(XFD_TYPE_SOCKET, cs, NULL);
    if (cs->fd == XFD_INVALID) { XFree_System(cs); return ERR_MEM; }
    cs->tpcb = newPcb;
    cs->base.m_connected = true;
    cs->sockType = XDeviceNetwork_Tcp;
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
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (!s || !p) return;
    if (addr) s->fromAddr = *addr;
    s->fromPort = port;
    int total = p->tot_len;
    if (total > 0) {
        size_t configuredMax = XNETWORK_LWIP_RECV_BUFFER_SIZE;
        size_t required = (size_t)s->rxPos + (size_t)total;
        u16_t desired = (u16_t)(required < configuredMax ? required : configuredMax);
        if (desired > s->rxCapacity && !reserve_rx_buffer(s, desired)) {
            pbuf_free(p);
            return;
        }
        int space = (int)s->rxCapacity - s->rxPos;
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

void XDeviceNetwork_ensureInit(void) {
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
    struct netif* nif = XDeviceNetworkLwip_platform_init();
    if (nif) {
        LWIP_DBG("[lwIP初始化] 平台网卡初始化成功\n");
    } else {
        LWIP_DBG("[lwIP初始化] 警告：平台网卡初始化失败，可能无法通信\n");
    }

    g_state.inited = 1;
    g_state.ref = 1;
    LWIP_DBG("[lwIP初始化] 完成，引用计数=%d\n", g_state.ref);
}

void XDeviceNetwork_cleanup(void) {
    if (!g_state.inited) return;
    g_state.ref--;
    if (g_state.ref > 0) return;

    LWIP_DBG("[lwIP清理] 开始清理...\n");

    /* 平台网卡清理 */
    XDeviceNetworkLwip_platform_deinit();

    g_state.inited = 0;
    LWIP_DBG("[lwIP清理] 完成\n");
}

void XDeviceNetwork_poll(void) {
    XDeviceNetworkLwip_pollPcap();
}

/* 错误信息 */
int XDeviceNetwork_lastError(void) { return g_state.lastError; }
char* XDeviceNetwork_errorString(int errorCode) {
    char* s = (char*)XMalloc_System(64);
    if (s) snprintf(s, 64, "lwIP error: %d", errorCode);
    return s;
}

/* ================================================================
 * Socket 创建/销毁
 * ================================================================ */

XDeviceNetworkContext* XDeviceNetwork_createContext(void) {
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)XMalloc_System(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->fd = XFD_INVALID;
    s->base.m_base.m_fd = XFD_INVALID;
    s->base.m_notifiers = XVector_create(sizeof(void*));
    return (XDeviceNetworkContext*)s;
}

void XDeviceNetwork_deleteContext(XDeviceNetworkContext* priv) {
    if (!priv) return;
    XDeviceNetworkContextLwip* s = L4P(priv);

    /* 关闭 lwIP PCB，需要持有核心锁 */
    if (s->tpcb || s->upcb || s->pendingAccept) {
        XNetLwipCoreLock prot = XNET_LWIP_LOCK();
        dispose_tcp_pcb(&s->tpcb);
        if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
        if (s->pendingAccept) {
            XDeviceNetworkContextLwip* cs =
                (XDeviceNetworkContextLwip*)s->pendingAccept;
            dispose_tcp_pcb(&cs->tpcb);
        }
        XNET_LWIP_UNLOCK(prot);
    }

    /* 释放资源 */
    s->fd = XFD_INVALID;
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (s->base.m_notifiers) XVector_delete_base((XClass*)s->base.m_notifiers);

    /* 清理未领取的 Accept 连接 */
    if (s->pendingAccept) {
        XDeviceNetworkContextLwip* cs = (XDeviceNetworkContextLwip*)s->pendingAccept;
        if (cs->fd != XFD_INVALID) {
            XFd_free(cs->fd);
            cs->fd = XFD_INVALID;
        }
        if (cs->rxBuf) XFree_System(cs->rxBuf);
        XFree_System(cs);
        s->pendingAccept = NULL;
    }
    XFree_System(s);
}

intptr_t XDeviceNetwork_socketDescriptor(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? (intptr_t)L4P(priv)->fd : -1;
}
/* ================================================================
 * Socket ??
 *
 * 基于 lwIP Raw API 回调的 TCP 服务端绑定（非阻塞模式）。
 * 回调模式中 XDeviceNetwork_socketBind 分配 TCP PCB 并立刻返回。
 * ================================================================ */
uint16_t XDeviceNetwork_socketBind(XFd xfd, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XDeviceNetworkSocketType sockType) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)reuseAddr; (void)shareAddr;  /* lwIP Raw API 不支持 SO_REUSEADDR */
    if (!priv) return 0;
    XDeviceNetwork_ensureInit();
    XDeviceNetworkContextLwip* s = L4P(priv);
    s->sockType = (int)sockType;

    /* 地址转换 */
    ip_addr_t bindAddr;
    if (address) addr_to_ip(address, &bindAddr); else bindAddr = *IP_ADDR_ANY;

    err_t err;
    if (sockType == XDeviceNetwork_Tcp) {
        /* TCP 服务端：创建 PCB 并开始监听 */
        s->tpcb = createTcpPcb(&bindAddr);
        if (!s->tpcb) return 0;
        if (tcp_bind(s->tpcb, &bindAddr, port) != ERR_OK) {
            dispose_tcp_pcb(&s->tpcb); return 0;
        }
        u8_t listenBacklog = s->listenBacklog ? (u8_t)s->listenBacklog
            : (u8_t)XNETWORK_LWIP_MAX_LISTEN_BACKLOG;
        struct tcp_pcb* listenPcb = tcp_listen_with_backlog(s->tpcb, listenBacklog);
        if (!listenPcb) {
            dispose_tcp_pcb(&s->tpcb); return 0;
        }
        s->tpcb = listenPcb;
        s->isServer = true;
        tcp_arg(s->tpcb, s);
        tcp_accept(s->tpcb, tcpAcceptCb);
        LWIP_DBG("[绑定] TCP服务端 port=%d backlog=%d\n", (int)s->tpcb->local_port,
                 (int)listenBacklog);
    } else {
        /* UDP：创建 PCB 并绑定 */
        s->upcb = createUdpPcb(&bindAddr);
        if (!s->upcb) return 0;
        if (udp_bind(s->upcb, &bindAddr, port) != ERR_OK) {
            udp_remove(s->upcb); s->upcb = NULL; return 0;
        }
        udp_recv(s->upcb, udpRecvCb, s);
        s->base.m_connected = true;
        LWIP_DBG("[绑定] UDP requested=%d local=%d\n", (int)port,
                 (int)s->upcb->local_port);
    }
    ensure_xfd(priv);
    return sockType == XDeviceNetwork_Tcp ? s->tpcb->local_port : s->upcb->local_port;
}


/* ================================================================
 * 网络就绪保障 + 异步 DNS 解析
 *
 * 问题背景：
 *   1. dns_gethostbyname 传 NULL 回调时，只能解析已缓存或 IP 字符串，
 *      对未缓存域名返回 ERR_INPROGRESS 后永远不会通知完成。
 *   2. DHCP 尚未完成时，netif 仍为 0.0.0.0，DNS 查询无源 IP；
 *      不能冒用宿主机地址作为 fallback，否则两个协议栈会产生地址冲突。
 *
 * 解决方案：
 *   ensure_network_ready(): 等待网卡获取独立 DHCP 地址并确认默认路由
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
static bool ensure_network_ready(uint32_t timeoutMs) {
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
            XDeviceNetworkLwip_pollPcap();
            XNET_LWIP_UNLOCK(prot);
        }
#else
        XDeviceNetworkLwip_pollPcap();
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
    return hasIp;
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
                XDeviceNetworkLwip_pollPcap();
                XNET_LWIP_UNLOCK(prot);
            }
#else
            XDeviceNetworkLwip_pollPcap();
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
bool XDeviceNetwork_socketConnect(XFd xfd, const XString* hostName,
                            uint16_t port, XDeviceNetworkProtocol protocol,
                            XDeviceNetworkSocketType sockType) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    bool isLiteralAddress;
    (void)protocol;
    if (!priv || !hostName) return false;
    const char* hostStr = XString_toUtf8(hostName);
    if (!hostStr) return false;
    XDeviceNetworkContextLwip* s = L4P(priv);
    XDeviceNetwork_ensureInit();
    s->sockType = (int)sockType;

    ip_addr_t ip;
    isLiteralAddress = ipaddr_aton(hostStr, &ip);
    if (isLiteralAddress) {
        /* Local loopback is usable without an external TAP/DHCP lease. */
        if (!ip_addr_isloopback(&ip) &&
            !ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS)) {
            return false;
        }
    } else {
        /* A hostname still requires an independent source address before DNS. */
        if (!ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS) ||
            !lwip_resolve_name(hostStr, &ip, XNETWORK_LWIP_DNS_TIMEOUT_MS)) {
            LWIP_DBG("[连接] DNS和IP解析均失败\n");
            return false;
        }
    }
    LWIP_DBG("[连接] 目标IP解析成功\n");
    LWIP_DBG("[连接] 默认网卡=%s\n",
             netif_default ? ip4addr_ntoa(ip_2_ip4(&netif_default->ip_addr)) : "NULL");

    err_t err;
    if (sockType == XDeviceNetwork_Tcp) {
        /* TCP 客户端连接 */
        s->tpcb = createTcpPcb(&ip);
        LWIP_DBG("[连接] tcp_new=%p\n", (void*)s->tpcb);
        if (!s->tpcb) return false;
        tcp_arg(s->tpcb, s);
        tcp_recv(s->tpcb, tcpRecvCb);
        tcp_sent(s->tpcb, tcpSentCb);
        tcp_err(s->tpcb, tcpErrCb);
        tcp_poll(s->tpcb, tcpPollCb, 2);  /* 每 1 秒轮询 */
        err = tcp_connect(s->tpcb, &ip, port, tcpConnectedCb);
        LWIP_DBG("[连接] tcp_connect结果=%d\n", (int)err);
        if (err != ERR_OK) { dispose_tcp_pcb(&s->tpcb); return false; }
    } else {
        /* UDP：创建 PCB 并直接设置目标地址 */
        s->upcb = createUdpPcb(&ip);
        if (!s->upcb) return false;
        udp_recv(s->upcb, udpRecvCb, s);
        err = udp_connect(s->upcb, &ip, port);
        if (err != ERR_OK) { udp_remove(s->upcb); s->upcb = NULL; return false; }
        s->base.m_connected = true;
        s->connectDone = true;
        push_socket_cq(s, XSocketAct_Connect);
    }
    ensure_xfd(priv);
    return true;
}

/* 断开连接 */
void XDeviceNetwork_socketDisconnect(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    dispose_tcp_pcb(&s->tpcb);
    if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
    XNET_LWIP_UNLOCK(prot);
    s->base.m_connected = false;
}


/* ================================================================
 * 数据读写
 * ================================================================ */

/* 从 Socket 读取数据到用户缓冲区 */
int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
                            XDeviceNetworkSocketType sockType, void* ringBuffer) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)sockType; (void)ringBuffer;
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (s->rxPos <= 0) return 0;
    int copy = s->rxPos < (int)len ? s->rxPos : (int)len;
    memcpy(buf, s->rxBuf, copy);
    if (copy < s->rxPos) memmove(s->rxBuf, s->rxBuf + copy, s->rxPos - copy);
    s->rxPos -= copy;
    return copy;
}

/* 向 Socket 写入数据 */
int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
                             XDeviceNetworkSocketType sockType, const XHostAddress* destAddr,
                             uint16_t destPort, void* ringBuffer) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)ringBuffer;
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();

    err_t err;
    if (sockType == XDeviceNetwork_Tcp) {
        if (!s->tpcb) { XNET_LWIP_UNLOCK(prot); return -1; }
        u16_t sndBuf = tcp_sndbuf(s->tpcb);
        u16_t toWrite = (u16_t)((len > sndBuf) ? sndBuf : len);
        /* A full TCP send window is a transient would-block condition.  BIO
         * callers map 0 to WANT_WRITE and retry after the next ACK. */
        if (toWrite == 0) { XNET_LWIP_UNLOCK(prot); return 0; }
        err_t e = tcp_write(s->tpcb, buf, toWrite, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) {
            tcp_output(s->tpcb);
            XNET_LWIP_UNLOCK(prot);
            return toWrite;
        }
        if (e == ERR_MEM) {
            XNET_LWIP_UNLOCK(prot);
            return 0;
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
bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !event) return false;
    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;
    XDeviceNetworkContextLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    bool hasEvent = false;

    /* 仅清除与当前事件类型匹配的标志位，避免丢失其他类型的事件。
     * 例如：Connect 事件到达时只清除 connectDone/hasError，
     * 不清除 hasReadData--后者由后续 Read 事件处理。 */
    if ((sockAct->actType & XSocketAct_Connect) &&
        (s->connectDone || s->hasError)) {
        if (s->base.m_connected && !s->isServer) syncSocketEndpoints(priv);
        s->connectDone = false;
        s->hasError = false;
        s->closing = false;  /* 连接错误/完成时一并清除关闭标志 */
        hasEvent = true;
    }
    if (sockAct->actType & XSocketAct_Read) {
        /* CQ 允许重复条目；没有新完成标志时必须清空旧快照，避免上层
         * 再次复制上一批数据。 */
        s->rxTotal = 0;
        if (s->hasReadData || s->closing) {
            s->hasReadData = false;
            s->closing = false;
            s->rxTotal = s->rxPos;  /* 更新已读字节数，供上层获取 */
            hasEvent = true;
        }
    }
    if (sockAct->actType & XSocketAct_Write) {
        s->lastWriteFinished = 0;
        if (s->hasWriteDone) {
            s->hasWriteDone = false;
            s->lastWriteFinished = s->writeFinished;  /* 记录已确认字节数 */
            s->writeFinished = 0;                      /* 重置计数器 */
            hasEvent = true;
        }
    }
    if ((sockAct->actType & XSocketAct_Accept) && s->hasAccept) {
        s->hasAccept = false;
        hasEvent = true;
    }
    XNET_LWIP_UNLOCK(prot);
    return hasEvent;
}

bool XDeviceNetwork_socketSetDescriptor(XFd deviceFd, intptr_t fd, int state, int openMode) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextLwip* target;

    (void)openMode;
    if (!priv || fd < 0 || state != 3) return false;
    target = L4P(priv);
    if (!adopt_connected_descriptor(target, (XFd)fd)) return false;
    syncSocketEndpoints(priv);
    return true;
}

bool XDeviceNetwork_serverSetDescriptor(XFd deviceFd, intptr_t fd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextLwip* target;

    if (!priv || fd < 0) return false;
    target = L4P(priv);
    return adopt_server_descriptor(target, (XFd)fd);
}

bool XDeviceNetwork_socketSetOption(XFd xfd, int option, const void* value) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)priv; (void)option; (void)value;
    return false;
}

void* XDeviceNetwork_socketGetOption(XFd xfd, int option) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)priv; (void)option;
    return NULL;
}

void XDeviceNetwork_socketSetReadBufferSize(XFd xfd, int64_t size) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)priv; (void)size;
}

const char* XDeviceNetwork_socketReadBuffer(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? L4P(priv)->rxBuf : NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? (size_t)L4P(priv)->rxTotal : 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? L4P(priv)->lastWriteFinished : 0;
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextLwip* s;
    bool pending;
    XNetLwipCoreLock prot;

    if (!priv) return false;
    s = L4P((XDeviceNetworkContext*)priv);
    prot = XNET_LWIP_LOCK();
    /* Raw API TCP data remains pending while it is queued or unacknowledged.
     * UDP writes are submitted synchronously and therefore have no pending state. */
    pending = s->tpcb != NULL && (s->tpcb->unsent != NULL || s->tpcb->unacked != NULL);
    XNET_LWIP_UNLOCK(prot);
    return pending;
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    (void)isUdp;
    XDeviceNetworkContextLwip* s = L4P(priv);
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
    /* 通知 lwIP 数据已消费，重新打开 TCP 接收窗口。若 Raw API 保存了
     * refused_data，立即重新投递，避免等待 250ms fast timer。 */
    if (consumed > 0 && s->tpcb) tcp_recved(s->tpcb, (u16_t)consumed);
    if (s->tpcb && s->tpcb->refused_data) {
        (void)tcp_process_refused_data(s->tpcb);
    }
    XNET_LWIP_UNLOCK(prot);
}

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd deviceFd, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    if (!priv) return (XDeviceNetworkServerHandle)(-1);
    if (backlog > 0) {
        L4P(priv)->listenBacklog = (unsigned)(backlog > 255 ? 255 : backlog);
    } else {
        L4P(priv)->listenBacklog = 0;
    }
    uint16_t actualPort = XDeviceNetwork_socketBind(deviceFd, addr, port, reuseAddr, false, XDeviceNetwork_Tcp);
    if (actualPort == 0) return (XDeviceNetworkServerHandle)(-1);
    return (XDeviceNetworkServerHandle)L4P(priv)->fd;
}

bool XDeviceNetwork_serverAccept(XFd xfd) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    /* lwIP 通过 tcp_accept 回调接受连接（在 socketBind 中已设置），此处无需操作 */
    (void)priv;
    return true;
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server) {
    XDeviceNetworkContextLwip* s;

    if (server < 0) return 0;
    s = descriptor_private((XFd)server);
    if (!s || !s->tpcb) return 0;
    return s->tpcb->local_port;
}

void XDeviceNetwork_serverClose(XFd xfd, XDeviceNetworkServerHandle server) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextLwip* s;

    (void)priv;
    if (server < 0) return;
    s = descriptor_private((XFd)server);
    if (!s) return;
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    dispose_tcp_pcb(&s->tpcb);
    XNET_LWIP_UNLOCK(prot);
    /* 有 owner 的服务端私有对象仍由 XTcpServer 销毁，保持服务端标记，
     * 避免析构时把 XTcpServer 误当作 XIODevice。 */
    if (!s->base.m_owner) s->isServer = false;

    /* 失败的 accepted socket 没有 XObject owner，由此路径负责回收。 */
    if (!s->base.m_owner) {
        XFd_free(s->fd);
        s->fd = XFD_INVALID;
        release_detached_private(s);
    }
}

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd xfd,
                                                XHostAddress* clientAddr, uint16_t* clientPort) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return (XDeviceNetworkSocketHandle)(-1);
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (!s->pendingAccept) return (XDeviceNetworkSocketHandle)(-1);
    XDeviceNetworkContextLwip* cs = (XDeviceNetworkContextLwip*)s->pendingAccept;
    s->pendingAccept = NULL;
    if (clientAddr && cs->tpcb) ip_to_addr(&cs->tpcb->remote_ip, clientAddr);
    if (clientPort && cs->tpcb) *clientPort = cs->tpcb->remote_port;
    LWIP_DBG("[TCP服务] 领取客户端Socket=%p\n", (void*)cs);
    return (XDeviceNetworkSocketHandle)cs->fd;
}

XVector* XDeviceNetwork_lookupName(const XString* name) {
    if (!name) return NULL;
    const char* nameStr = XString_toUtf8(name);
    if (!nameStr) return NULL;
    XDeviceNetwork_ensureInit();

    /* Name lookup also needs a usable source address for DNS traffic. */
    if (!ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS)) return NULL;

    /* 异步 DNS 解析（带回调 + 轮询等待） */
    ip_addr_t ip;
    if (!lwip_resolve_name(nameStr, &ip, XNETWORK_LWIP_DNS_TIMEOUT_MS)) return NULL;

    XVector* vec = XVector_create(sizeof(XHostAddress));
    if (!vec) return NULL;
    XContainerSetDataMoveMethod(vec, XClass_move_base);
    XContainerSetDataCopyMethod(vec, XClass_copy_base);
    XContainerSetDataDeinitMethod(vec, XHostAddress_deinit_base);

    XHostAddress addr;
    XHostAddress_init(&addr);
    ip_to_addr(&ip, &addr);
    XVector_push_back_1_base(vec, &addr);

    return vec;
}

XString* XDeviceNetwork_localHostName(void) {
    return XString_create_utf8("lwip-device");
}

typedef struct { struct netif* next; int idx; } LwipIfIter;

XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void) {
    XDeviceNetwork_ensureInit();
    LwipIfIter* it = (LwipIfIter*)XMalloc_System(sizeof(LwipIfIter));
    if (!it) return NULL;
    it->next = netif_list; it->idx = 0;
    return (XDeviceNetworkInterfaceIterator)it;
}

XNetworkInterface* XDeviceNetwork_enumInterfacesNext(XDeviceNetworkInterfaceIterator iter) {
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
        XClass_deinit_base((XClass*)&entry);
    }
    iface->isValid = true; it->idx++;
    return iface;
}

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter) { if (iter) XFree_System(iter); }

bool XDeviceNetwork_multicastGroup(XDeviceNetworkSocketHandle sock, bool join, const XHostAddress* group, uint32_t ifIdx) {
    (void)sock; (void)join; (void)group; (void)ifIdx; return false;
}
int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle sock, XMulticastOp op, void* arg) {
    (void)sock; (void)op; (void)arg; return -1;
}

bool XDeviceNetwork_platformGetLastDatagramSender(XFd xfd, XHostAddress* src, uint16_t* port) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return false;
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (src) ip_to_addr(&s->fromAddr, src);
    if (port) *port = s->fromPort;
    return true;
}

bool XDeviceNetwork_getSystemProxy(const XString* url, XNetworkProxy* out) {
    (void)url; (void)out; return false;
}
int XDeviceNetwork_gssapiAuth(const XString* name, const XByteArray* in, XByteArray* out, void** ctx) {
    (void)name; (void)in; (void)out; (void)ctx; return -1;
}

/* ================================================================
 * 写入延续 - lwIP 模式下写入已通过 tcp_write/tcp_output 同步完成，
 * 环形缓冲区中待发送的明文已在 xssl_v_writeData -> BIO -> parent writeData
 * 路径中完成加密并发送，无需额外操作。
 * ================================================================ */
void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !ringBuffer) return;
    (void)isUdp;
    /* lwIP: 写入通过 tcp_write/tcp_output 同步完成，
     * 环形缓冲区数据已在 XDeviceNetwork_socketWrite 路径中处理完毕。 */
}
#endif /* XNETWORK_USE_LWIP */
