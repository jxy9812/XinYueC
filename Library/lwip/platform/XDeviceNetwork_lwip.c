/**
 * @file XDeviceNetwork_lwip.c
 * @brief lwIP ??????(???? + Raw API ????)
 *
 * ????:
 *   XDeviceNetwork.h                             -> ????????
 *   +-- XDeviceNetwork_win32.c                   -> Windows IOCP ??
 *   +-- XDeviceNetwork_lwip.c(???)          -> lwIP ???(????)
 *       +-- XDeviceNetwork_lwip_win32.c          -> Npcap ????(Windows ??)
 *
 * ????:
 *   1. lwip_init() ??? lwIP ???
 *   2. XTimeWheelGroup ???? LWIP_TICK_MS ????(?? 5ms),
 *      a) ?????????(XDeviceNetworkLwip_pollPcap)
 *      b) lwIP ???? LWIP_TIMERS_CUSTOM ???? XTimeWheelGroup,????
 *      c) ???????????
 *   3. ?????? Raw API ??,sys_arch_protect ???????
 *
 * ?? XSync + XMemory ????,?????
 * ???????? XMalloc_System / XRealloc_System / XFree_System?
 * ????? XTimeWheelGroup ??????
 * ????? XRandomGenerator_system()?
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

/* ?? lwIP ????????????????,??? XDeviceNetwork ???? */
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
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0: ???,?????(???)
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=1: sys_arch_protect(???)
 *   NO_SYS=0: LOCK_TCPIP_CORE(? tcpip_thread ??)
 * ================================================================ */
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
typedef sys_prot_t XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        sys_arch_protect()
#define XNET_LWIP_UNLOCK(l)     sys_arch_unprotect(l)
#elif NO_SYS
/* ?????:?? lwIP ??????,?????,????? */
typedef int XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        (0)
#define XNET_LWIP_UNLOCK(l)     ((void)(l))
#else
typedef int XNetLwipCoreLock;
#define XNET_LWIP_LOCK()        (LOCK_TCPIP_CORE(), 0)
#define XNET_LWIP_UNLOCK(l)     UNLOCK_TCPIP_CORE()
#endif

/* ================================================================
 * ?????
 * ================================================================ */
#define L4P(p) ((XDeviceNetworkContextLwip*)(p))
/* lwIP ??????,??? XNETWORK_LWIP_TICK_MS ?? */
#define LWIP_TICK_MS XNETWORK_LWIP_TICK_MS

typedef struct XDeviceNetworkContextLwip XDeviceNetworkContextLwip;

/* ================================================================
 * ????
 * ================================================================ */
/* lwIP ????(????,??????)
 * ??? 4 ???????(? 24~32 ??),??? 1 ????(16 ??)?
 * ??? 4 ???????(? 24~32 ??),??? 1 ????(8 ??)?
 * - lastError:    ????? -15~0,8 ??????
 * - ref:          ?????? 1~5,7 ?(?? 127)??
 * - inited:       ?????,1 ??? */
static struct {
    int      lastError : 8;      /* ??????? (-15~0) */
    unsigned ref       : 7;      /* lwIP ???? (max 127) */
    unsigned inited    : 1;      /* ?????? */
} g_state;


/* ================================================================
 * Socket ??????
 *
 * ?? lwIP ? TCP/UDP PCB ????????
 * ?? XAbstractSocket ???????????
 * ================================================================ */
typedef struct XDeviceNetworkContextLwip {
    XDeviceNetworkContext base;    /* ??:owner + notifiers (16 ??) */
    XFd fd;                       /* ????????(????? vtable ??) */
    struct tcp_pcb* tpcb;          /* TCP ????? */
    struct udp_pcb* upcb;          /* UDP ????? */
    char* rxBuf;                   /* ????? */
    void* pendingAccept;           /* ????????? */
    size_t writeFinished;          /* ????????????(?????) */
    size_t lastWriteFinished;      /* ?????????????? */
    /* ????/size_t ? 64 ??? 72 ??(? XFd fd ??) */
    int rxPos;                     /* ??????????? */
    int rxTotal;                   /* ????????? */
    ip_addr_t fromAddr;            /* UDP ??????? (IPv4=4 ??) */
    uint16_t fromPort;             /* UDP ??????? */
    uint16_t rxCapacity;           /* ?????????,??????? */
    /* ????:7 ???(7?) + sockType(2?) + lastErr(8?) = 17 ?,??? 4 ??
     * ?? 7 ? bool(7??) + sockType(4??) + lastErr(4??) + ????
     * ???? 4 ??,???? ~104 ???? 88 ?? */
    unsigned isServer    : 1;      /* ?????? */
    unsigned closing     : 1;      /* ?????? */
    unsigned connectDone : 1;      /* ?????? */
    unsigned hasReadData : 1;      /* ??????? */
    unsigned hasWriteDone: 1;      /* ?????? */
    unsigned hasAccept   : 1;      /* ????????? */
    unsigned hasError    : 1;      /* ??????? */
    unsigned sockType    : 2;      /* Socket ?? (0=TCP, 1=UDP) */
    unsigned listenBacklog : 8;    /* TCP ???????,0=??????? */
    int      lastErr     : 8;      /* ???? lwIP ??? (err_t, -16~0) */
} XDeviceNetworkContextLwip;

/* Raw API ????????????????? */
static err_t tcpRecvCb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len);
static void tcpErrCb(void* arg, err_t err);
static err_t tcpPollCb(void* arg, struct tcp_pcb* pcb);
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err);

/* ????????,????? socket ???????? */
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

/* ?????????????????????????;?????
 * ??????,??????????,???? TCP ??????? */
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
 * Socket ??????(????,????)
 *
 * ? lwIP Raw API ??????,???????? CQ ??
 * ???? IoRing??????????????,??????,
 * ? eventPending ?????--?? CQ ?????????
 * ????????????(no-op),?????????
 * ================================================================ */

/**
 * @brief ? lwIP ?????????? Socket ??? IoRing CQ
 * @param s       Socket ??????
 * @param actType ??????(XSocketActType ???)
 *
 * ????:lwIP ?????????????????
 * ??????????(??? lwIP ???????)?
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
 * ????????
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
    XClassDeinit((XClass*)&endpoint);
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

/* ? lwIP ????????? XDevice_open ??????????? */
static void ensure_xfd(XDeviceNetworkContext* priv) {
    if (!priv) return;
    XDeviceNetworkContextLwip* s = L4P(priv);
    if (s->fd != XFD_INVALID) return;
    s->fd = priv->m_base.m_fd;
}

/*
 * lwIP ???? socket descriptor????? XFd ???,??? handle
 * ?? Raw API ????;????? tcp_pcb ????,?????????
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
    if (s->base.m_notifiers) XClassDelete((XClass*)s->base.m_notifiers);
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
    /* target ?????? PCB;?????????,?? accepted socket
     * ???????????????source ???? target ????? */
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
 * ?????:lwIP err_t -> ? POSIX errno
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
 * TCP ????
 *
 * ????? lwIP ????????????,? sys_arch_protect ???
 * ?????????,??????????????
 * ================================================================ */

/* TCP ?????? */
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
        push_socket_cq(s, XSocketActType_Read);
        return ERR_OK; }

    total = p->tot_len;
    if (total == 0) {
        pbuf_free(p);
        return ERR_OK;
    }

    /* Raw API ???"?????????? pbuf"??????????
     * pbuf ????? ERR_MEM,lwIP ???? pcb->refused_data ????
     * ????????? pbuf ???,???????????? */
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
    push_socket_cq(s, XSocketActType_Read);
    pbuf_free(p);
    /* tcp_recved ??? XDeviceNetwork_socketContinueRead ???,???????? */
    return ERR_OK;
}

/* TCP ?????? */
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)pcb;
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (s) { s->writeFinished += len; s->hasWriteDone = true; }
    push_socket_cq(s, XSocketActType_Write);
    return ERR_OK;
}

/* TCP ???? */
static void tcpErrCb(void* arg, err_t err) {
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (s) {
        s->closing = true; s->hasError = true;
        s->lastErr = err; g_state.lastError = XDeviceNetworkLwip_err_to_errno(err);
        s->tpcb = NULL;
        s->base.m_connected = false;  /* ??????,?? Connect ??????? */
        push_socket_cq(s, XSocketActType_Connect);
    }
}

/* TCP ????(????,?????)
 * ?? interval ? tcp_poll ?????,?? 0.5 ??
 * interval=2 ?? 1 ???(??????) */
static err_t tcpPollCb(void* arg, struct tcp_pcb* pcb) {
    (void)arg; (void)pcb; return ERR_OK;
}

/* TCP ?????? */
static err_t tcpConnectedCb(void* arg, struct tcp_pcb* pcb, err_t err) {
    (void)pcb;
    XDeviceNetworkContextLwip* s = (XDeviceNetworkContextLwip*)arg;
    if (!s) return ERR_ABRT;
    if (err == ERR_OK) {
        s->base.m_connected = true; s->connectDone = true;
        LWIP_DBG("[TCP??] ????\n");
    } else {
        s->hasError = true; s->lastErr = err;
        g_state.lastError = XDeviceNetworkLwip_err_to_errno(err);
        LWIP_DBG("[TCP??] ????, err=%d\n", (int)err);
    }
    push_socket_cq(s, XSocketActType_Connect);
    return ERR_OK;
}

/* TCP ???????(???)
 * ???? Socket ??????,????? pendingAccept ?? */
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err) {
    (void)err;
    XDeviceNetworkContextLwip* ss = (XDeviceNetworkContextLwip*)arg;
    if (!ss || !newPcb) return ERR_ABRT;

    /* ??????? Socket ?????? */
    XDeviceNetworkContextLwip* cs = (XDeviceNetworkContextLwip*)XMalloc_System(sizeof(*cs));
    if (!cs) return ERR_MEM;
    memset(cs, 0, sizeof(*cs));
    cs->fd = XFd_alloc(XFD_TYPE_SOCKET, cs, NULL);
    if (cs->fd == XFD_INVALID) { XFree_System(cs); return ERR_MEM; }
    cs->tpcb = newPcb;
    cs->base.m_connected = true;
    cs->sockType = XDeviceNetwork_Tcp;
    /* ?? TCP ?? */
    tcp_arg(newPcb, cs);
    tcp_recv(newPcb, tcpRecvCb);
    tcp_sent(newPcb, tcpSentCb);
    tcp_err(newPcb, tcpErrCb);
    tcp_poll(newPcb, tcpPollCb, 2);  /* ? 1 ????? */

    /* ??????????? */
    ss->pendingAccept = cs;
    ss->hasAccept = true;
    push_socket_cq(ss, XSocketActType_Accept);
    LWIP_DBG("[TCP??] ??????\n");
    return ERR_OK;
}

/* UDP ?????? */
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
            push_socket_cq(s, XSocketActType_Read);
        }
    }
    pbuf_free(p);
}


/* ================================================================
 * ?????/??
 * ================================================================ */

void XDeviceNetwork_ensureInit(void) {
    if (g_state.inited) { g_state.ref++; return; }
    LWIP_DBG("[lwIP???] ????? lwIP ???...\n");

    /* ??? lwIP ???:????pbuf?TCP/UDP/IP/ARP ? */
    /* Initialize sys_arch layer (create core recursive mutex g_coreLock).
     * NO_SYS=1: lwip_init() does NOT call sys_init(), so we must call it here.
     * NO_SYS=0: lwip_init() also calls it, but sys_init() is idempotent (safe). */
#if NO_SYS
    sys_init();
    lwip_init();
#else
    tcpip_init(NULL, NULL);
#endif

    /* ???????:Npcap / TAP / ?? MAC */
    struct netif* nif = XDeviceNetworkLwip_platform_init();
    if (nif) {
        LWIP_DBG("[lwIP???] ?????????\n");
    } else {
        LWIP_DBG("[lwIP???] ??:?????????,??????\n");
    }

    g_state.inited = 1;
    g_state.ref = 1;
    LWIP_DBG("[lwIP???] ??,????=%d\n", g_state.ref);
}

void XDeviceNetwork_cleanup(void) {
    if (!g_state.inited) return;
    g_state.ref--;
    if (g_state.ref > 0) return;

    LWIP_DBG("[lwIP??] ????...\n");

    /* ?????? */
    XDeviceNetworkLwip_platform_deinit();

    g_state.inited = 0;
    LWIP_DBG("[lwIP??] ??\n");
}

void XDeviceNetwork_poll(void) {
    XDeviceNetworkLwip_pollPcap();
}

/* ???? */
int XDeviceNetwork_lastError(void) { return g_state.lastError; }
char* XDeviceNetwork_errorString(int errorCode) {
    char* s = (char*)XMalloc_System(64);
    if (s) snprintf(s, 64, "lwIP error: %d", errorCode);
    return s;
}

/* ================================================================
 * Socket ??/??
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

    /* ?? lwIP PCB,??????? */
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

    /* ???? */
    s->fd = XFD_INVALID;
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (s->base.m_notifiers) XClassDelete((XClass*)s->base.m_notifiers);

    /* ?????? Accept ?? */
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
 * ?? lwIP Raw API ??? TCP ?????(?????)?
 * ????? XDeviceNetwork_socketBind ?? TCP PCB ??????
 * ================================================================ */
uint16_t XDeviceNetwork_socketBind(XFd xfd, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XDeviceNetworkSocketType sockType) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)reuseAddr; (void)shareAddr;  /* lwIP Raw API ??? SO_REUSEADDR */
    if (!priv) return 0;
    XDeviceNetwork_ensureInit();
    XDeviceNetworkContextLwip* s = L4P(priv);
    s->sockType = (int)sockType;

    /* ???? */
    ip_addr_t bindAddr;
    if (address) addr_to_ip(address, &bindAddr); else bindAddr = *IP_ADDR_ANY;

    err_t err;
    if (sockType == XDeviceNetwork_Tcp) {
        /* TCP ???:?? PCB ????? */
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
        LWIP_DBG("[??] TCP??? port=%d backlog=%d\n", (int)s->tpcb->local_port,
                 (int)listenBacklog);
    } else {
        /* UDP:?? PCB ??? */
        s->upcb = createUdpPcb(&bindAddr);
        if (!s->upcb) return 0;
        if (udp_bind(s->upcb, &bindAddr, port) != ERR_OK) {
            udp_remove(s->upcb); s->upcb = NULL; return 0;
        }
        udp_recv(s->upcb, udpRecvCb, s);
        s->base.m_connected = true;
        LWIP_DBG("[??] UDP requested=%d local=%d\n", (int)port,
                 (int)s->upcb->local_port);
    }
    ensure_xfd(priv);
    return sockType == XDeviceNetwork_Tcp ? s->tpcb->local_port : s->upcb->local_port;
}


/* ================================================================
 * ?????? + ?? DNS ??
 *
 * ????:
 *   1. dns_gethostbyname ? NULL ???,???????? IP ???,
 *      ???????? ERR_INPROGRESS ??????????
 *   2. DHCP ?????,netif ?? 0.0.0.0,DNS ???? IP;
 *      ??????????? fallback,???????????????
 *
 * ????:
 *   ensure_network_ready(): ???????? DHCP ?????????
 *   lwip_resolve_name():    dns_gethostbyname + ?? + ??????
 * ================================================================ */

/* ?? netif ?????????? IPv4 ?? */
static bool netif_has_valid_ipv4(struct netif* n) {
    if (!n || ip_addr_isany(&n->ip_addr) || !(n->flags & NETIF_FLAG_UP)) return false;
    uint32_t hip = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&n->ip_addr)));
    /* ?? 127.x.x.x (??) ? 169.254.x.x (APIPA) */
    if ((hip >> 24) == 127 || (hip >> 16) == 0xA9FE) return false;
    return true;
}

/* ??????:
 *   1. ?????? DHCP ???? IP(DHCP ??? lwIP ???? IP/??/DNS)
 *   2. ??????? */
static bool ensure_network_ready(uint32_t timeoutMs) {
    struct netif* n;
    bool hasIp = false;
    uint32_t start = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF);

    /* ?????????? DHCP ???? IP */
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
        LWIP_DBG("[????] DHCP???,?????IP\n");
    } else {
        LWIP_DBG("[????] ??DHCP??(%dms),???IP\n", (int)timeoutMs);
    }

    /* ??????? */
    if (!netif_default) {
        for (n = netif_list; n; n = n->next) {
            if (netif_has_valid_ipv4(n)) {
                netif_set_default(n);
                LWIP_DBG("[????] ??????: %s\n", ip4addr_ntoa(ip_2_ip4(&n->ip_addr)));
                break;
            }
        }
    }
    return hasIp;
}

/* DNS ????????? */
typedef struct {
    ip_addr_t ip;
    volatile bool done;
    volatile bool found;
} DnsResolveCtx;

/* dns_gethostbyname ???? */
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

/* ??????:dns_gethostbyname + ?? + ????
 * @param name       ??? IP ???
 * @param outIp      ??????
 * @param timeoutMs  ????
 * @return true=???? */
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
        /* ????? IP ???,???? */
        *outIp = ip;
        return true;
    }
    if (e == ERR_INPROGRESS) {
        /* ?????,?? pcap + ????,?????? */
        uint32_t start = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF);
        while (!ctx.done) {
            uint32_t elapsed = (uint32_t)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFF) - start;
            if (elapsed >= timeoutMs) {
                LWIP_DBG("[DNS] ????(%dms): %s\n", (int)timeoutMs, name);
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
            LWIP_DBG("[DNS] ????: %s -> %s\n", name, ipaddr_ntoa(outIp));
            return true;
        }
        LWIP_DBG("[DNS] ????(?????): %s\n", name);
        return false;
    }

    /* ERR_ARG ?????:????? IP ???? */
    if (ipaddr_aton(name, &ip)) {
        *outIp = ip;
        return true;
    }
    LWIP_DBG("[DNS] dns_gethostbyname??=%d: %s\n", (int)e, name);
    return false;
}

/* ================================================================
 * Socket ??
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
            LWIP_DBG("[??] DNS?IP?????\n");
            return false;
        }
    }
    LWIP_DBG("[??] ??IP????\n");
    LWIP_DBG("[??] ????=%s\n",
             netif_default ? ip4addr_ntoa(ip_2_ip4(&netif_default->ip_addr)) : "NULL");

    err_t err;
    if (sockType == XDeviceNetwork_Tcp) {
        /* TCP ????? */
        s->tpcb = createTcpPcb(&ip);
        LWIP_DBG("[??] tcp_new=%p\n", (void*)s->tpcb);
        if (!s->tpcb) return false;
        tcp_arg(s->tpcb, s);
        tcp_recv(s->tpcb, tcpRecvCb);
        tcp_sent(s->tpcb, tcpSentCb);
        tcp_err(s->tpcb, tcpErrCb);
        tcp_poll(s->tpcb, tcpPollCb, 2);  /* ? 1 ??? */
        err = tcp_connect(s->tpcb, &ip, port, tcpConnectedCb);
        LWIP_DBG("[??] tcp_connect??=%d\n", (int)err);
        if (err != ERR_OK) { dispose_tcp_pcb(&s->tpcb); return false; }
    } else {
        /* UDP:?? PCB ????????? */
        s->upcb = createUdpPcb(&ip);
        if (!s->upcb) return false;
        udp_recv(s->upcb, udpRecvCb, s);
        err = udp_connect(s->upcb, &ip, port);
        if (err != ERR_OK) { udp_remove(s->upcb); s->upcb = NULL; return false; }
        s->base.m_connected = true;
        s->connectDone = true;
        push_socket_cq(s, XSocketActType_Connect);
    }
    ensure_xfd(priv);
    return true;
}

/* ???? */
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
 * ????
 * ================================================================ */

/* ? Socket ?????????? */
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

/* ? Socket ???? */
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
 * ????
 * ================================================================ */

/* ?? Socket ??:????????????? */
bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event) {
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !event) return false;
    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;
    XDeviceNetworkContextLwip* s = L4P(priv);
    XNetLwipCoreLock prot = XNET_LWIP_LOCK();
    bool hasEvent = false;

    /* ????????????????,????????????
     * ??:Connect ???????? connectDone/hasError,
     * ??? hasReadData--????? Read ????? */
    if ((sockAct->actType & XSocketActType_Connect) &&
        (s->connectDone || s->hasError)) {
        if (s->base.m_connected && !s->isServer) syncSocketEndpoints(priv);
        s->connectDone = false;
        s->hasError = false;
        s->closing = false;  /* ????/??????????? */
        hasEvent = true;
    }
    if (sockAct->actType & XSocketActType_Read) {
        /* CQ ??????;???????????????,????
         * ?????????? */
        s->rxTotal = 0;
        if (s->hasReadData || s->closing) {
            s->hasReadData = false;
            s->closing = false;
            s->rxTotal = s->rxPos;  /* ???????,????? */
            hasEvent = true;
        }
    }
    if (sockAct->actType & XSocketActType_Write) {
        s->lastWriteFinished = 0;
        if (s->hasWriteDone) {
            s->hasWriteDone = false;
            s->lastWriteFinished = s->writeFinished;  /* ???????? */
            s->writeFinished = 0;                      /* ????? */
            hasEvent = true;
        }
    }
    if ((sockAct->actType & XSocketActType_Accept) && s->hasAccept) {
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
        /* ??????????,?????????? */
        size_t remaining = (size_t)s->rxPos - consumed;
        memmove(s->rxBuf, s->rxBuf + consumed, remaining);
        s->rxPos = (int)remaining;
        s->rxTotal = 0;
        s->hasReadData = true;  /* ???????? */
        push_socket_cq(s, XSocketActType_Read);  /* ?? Read ?????? */
    }
    else {
        /* ?????,????? */
        s->rxPos = 0;
        s->rxTotal = 0;
    }
    /* ?? lwIP ?????,???? TCP ?????? Raw API ???
     * refused_data,??????,???? 250ms fast timer? */
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
    /* lwIP ?? tcp_accept ??????(? socketBind ????),?????? */
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
    /* ? owner ?????????? XTcpServer ??,???????,
     * ?????? XTcpServer ??? XIODevice? */
    if (!s->base.m_owner) s->isServer = false;

    /* ??? accepted socket ?? XObject owner,????????? */
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
    LWIP_DBG("[TCP??] ?????Socket=%p\n", (void*)cs);
    return (XDeviceNetworkSocketHandle)cs->fd;
}

XVector* XDeviceNetwork_lookupName(const XString* name) {
    if (!name) return NULL;
    const char* nameStr = XString_toUtf8(name);
    if (!nameStr) return NULL;
    XDeviceNetwork_ensureInit();

    /* Name lookup also needs a usable source address for DNS traffic. */
    if (!ensure_network_ready(XNETWORK_LWIP_DNS_TIMEOUT_MS)) return NULL;

    /* ?? DNS ??(??? + ????) */
    ip_addr_t ip;
    if (!lwip_resolve_name(nameStr, &ip, XNETWORK_LWIP_DNS_TIMEOUT_MS)) return NULL;

    XVector* vec = XVector_create(sizeof(XHostAddress));
    if (!vec) return NULL;
    XContainerSetDataMoveMethod(vec, XClass_move_base);
    XContainerSetDataCopyMethod(vec, XClass_copy_base);
    XContainerSetDataDeinitMethod(vec, XClass_deinit_base);

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
    iface->m_name = XString_create_utf8(nameBuf);
    iface->m_humanReadableName = XString_create_utf8(nameBuf);
    if (nif->hwaddr_len >= 6) {
        char macStr[32]; snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            nif->hwaddr[0], nif->hwaddr[1], nif->hwaddr[2], nif->hwaddr[3], nif->hwaddr[4], nif->hwaddr[5]);
        iface->m_hardwareAddress = XString_create_utf8(macStr);
    }
    iface->m_index = nif->num; iface->m_mtu = (int)nif->mtu;
    if (nif->flags & NETIF_FLAG_UP) iface->m_flags |= XNetworkInterface_IsUp | XNetworkInterface_IsRunning;
    if (nif->flags & NETIF_FLAG_LINK_UP) iface->m_flags |= XNetworkInterface_IsRunning;
    if (nif->flags & NETIF_FLAG_BROADCAST) iface->m_flags |= XNetworkInterface_CanBroadcast;
    if (nif->name[0] == 'l' && nif->name[1] == 'o') {
        iface->m_flags |= XNetworkInterface_IsLoopBack; iface->m_type = XNetworkInterface_Loopback;
    } else { iface->m_type = XNetworkInterface_Ethernet; }
    if (!ip_addr_isany(&nif->ip_addr)) {
        XNetworkAddressEntry entry; XNetworkAddressEntry_init(&entry);
        ip_to_addr(&nif->ip_addr, &entry.ip); ip_to_addr(&nif->netmask, &entry.netmask);
        if (nif->flags & NETIF_FLAG_BROADCAST) {
            uint32_t ipVal = ip4_addr_get_u32(ip_2_ip4(&nif->ip_addr));
            uint32_t maskVal = ip4_addr_get_u32(ip_2_ip4(&nif->netmask));
            XHostAddress_setAddressIPv4(&entry.broadcast, lwip_ntohl(ipVal | ~maskVal));
            entry.broadcastIsValid = true;
        }
        XVector_push_back_move_1_base(iface->m_addressEntries, &entry);
        XClassDeinit(&entry);
    }
    iface->m_isValid = true; it->idx++;
    return iface;
}

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter) { if (iter) XFree_System(iter); }

/* ================================================================
 * ????(DHCP / ?? IP)
 *
 * lwIP Raw API ??(? netsh/???????):
 *   - ??:netif ? ip_addr/netmask/gw + struct dhcp ??? +
 *     dns_getserver,????????;
 *   - ??:netif_set_addr + dns_setserver(dhcp ??????);
 *   - DHCP:dhcp_stop + dhcp_start(lwIP ???????? DHCP
 *     ??,DNS ?????,??? ??? DNS ??????)?
 * ??????:ifIndex == netif->num(?? enumInterfacesNext
 * ? XNetworkInterface_index ?????)?
 * ================================================================ */

/** ? netif->num ????(???????)? */
static struct netif* lwipConfig_findNetif(uint32_t ifIndex) {
    struct netif* n;
    for (n = netif_list; n; n = n->next) {
        if ((uint32_t)n->num == ifIndex) return n;
    }
    return NULL;
}

/** ?? IPv4 ??? ? ip4_addr(?/???? false)? */
static bool lwipConfig_parseIpv4(const char* text, ip4_addr_t* out) {
    if (!text || !text[0]) return false;
    return ip4addr_aton(text, out) == 1;
}

bool XDeviceNetwork_interfaceConfigSupported(void) {
    /* ??????;DHCP ????? LWIP_DHCP ??(setDhcp ????
     * false),supported ???????????? */
    return true;
}

bool XDeviceNetwork_queryInterfaceConfig(uint32_t ifIndex,
                                         XDeviceNetworkInterfaceConfig* outConfig) {
    struct netif* nif;
    XNetLwipCoreLock prot;
    bool ok = false;

    if (!outConfig) return false;
    memset(outConfig, 0, sizeof(*outConfig));
    XHostAddress_init(&outConfig->ipv4Address);
    XHostAddress_init(&outConfig->ipv4Netmask);
    XHostAddress_init(&outConfig->ipv4Gateway);
    XHostAddress_init(&outConfig->ipv6Address);
    XHostAddress_init(&outConfig->ipv6Gateway);
    XHostAddress_init(&outConfig->dhcpServer);
    XHostAddress_init(&outConfig->dnsPrimary);
    XHostAddress_init(&outConfig->dnsSecondary);

    XDeviceNetwork_ensureInit();
    prot = XNET_LWIP_LOCK();
    nif = lwipConfig_findNetif(ifIndex);
    if (nif) {
        char nameBuf[8];
        snprintf(nameBuf, sizeof(nameBuf), "%c%c%d",
                 nif->name[0], nif->name[1], nif->num);
        outConfig->friendlyName = XString_create_utf8(nameBuf);
        outConfig->operUp = (nif->flags & NETIF_FLAG_UP) != 0;
        if (!ip_addr_isany(&nif->ip_addr)) {
            XHostAddress_setAddressIPv4(&outConfig->ipv4Address,
                lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&nif->ip_addr))));
            XHostAddress_setAddressIPv4(&outConfig->ipv4Netmask,
                lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&nif->netmask))));
        }
        if (!ip_addr_isany(&nif->gw)) {
            XHostAddress_setAddressIPv4(&outConfig->ipv4Gateway,
                lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&nif->gw))));
        }
#if LWIP_IPV6
        /* ?? VALID ? IPv6(???;lwIP ??????,? 64 ?)? */
        {
            s8_t i6;
            for (i6 = 0; i6 < LWIP_IPV6_NUM_ADDRESSES; ++i6) {
                if (netif_ip6_addr_state(nif, i6) & IP6_ADDR_VALID) {
                    XHostAddress_setAddressIPv6(&outConfig->ipv6Address,
                        (const uint8_t*)netif_ip6_addr(nif, i6));
                    outConfig->ipv6PrefixLength = 64;
                    break;
                }
            }
        }
#endif
#if LWIP_DHCP
        /* dhcp_start ???? DHCP ??(??????)?lwIP 2.2 netif
         * ??? dhcp ??,? client-data ???(netif_dhcp_data ?)? */
        {
            struct dhcp* dhcp = netif_dhcp_data(nif);
            outConfig->dhcpEnabled = (dhcp != NULL);
            if (dhcp && !ip_addr_isany(&dhcp->server_ip_addr)) {
                XHostAddress_setAddressIPv4(&outConfig->dhcpServer,
                    lwip_ntohl(ip4_addr_get_u32(
                        ip_2_ip4(&dhcp->server_ip_addr))));
            }
        }
#endif
#if LWIP_DNS
        {
            const ip_addr_t* d0 = dns_getserver(0);
            const ip_addr_t* d1 = dns_getserver(1);
            if (d0 && !ip_addr_isany(d0))
                ip_to_addr(d0, &outConfig->dnsPrimary);
            if (d1 && !ip_addr_isany(d1))
                ip_to_addr(d1, &outConfig->dnsSecondary);
        }
#endif
        ok = true;
    }
    XNET_LWIP_UNLOCK(prot);
    return ok;
}

void XDeviceNetwork_freeInterfaceConfig(XDeviceNetworkInterfaceConfig* config) {
    if (!config) return;
    if (config->friendlyName) XClassDelete(config->friendlyName);
    XClassDeinit(&config->ipv4Address);
    XClassDeinit(&config->ipv4Netmask);
    XClassDeinit(&config->ipv4Gateway);
    XClassDeinit(&config->ipv6Address);
    XClassDeinit(&config->ipv6Gateway);
    XClassDeinit(&config->dhcpServer);
    XClassDeinit(&config->dnsPrimary);
    XClassDeinit(&config->dnsSecondary);
    memset(config, 0, sizeof(*config));
}

bool XDeviceNetwork_setInterfaceDhcp(uint32_t ifIndex) {
#if LWIP_DHCP
    struct netif* nif;
    XNetLwipCoreLock prot;
    bool ok = false;

    XDeviceNetwork_ensureInit();
    prot = XNET_LWIP_LOCK();
    nif = lwipConfig_findNetif(ifIndex);
    if (nif) {
        /* ????(??/DHCP ???)? dhcp_start ????????
         * DHCP ??;???? DNS ??????????(?????
         * lwIP ?? DHCP ??? DNS ??)? */
        dhcp_stop(nif);
#if LWIP_DNS
        dns_setserver(0, NULL);
        dns_setserver(1, NULL);
#endif
        ok = (dhcp_start(nif) == ERR_OK);
    }
    XNET_LWIP_UNLOCK(prot);
    return ok;
#else
    (void)ifIndex;
    return false; /* lwIP ??? DHCP ?? */
#endif
}

bool XDeviceNetwork_setInterfaceStaticIpv6(uint32_t ifIndex,
                                           const char* ipv6Address,
                                           int prefixLength,
                                           const char* ipv6Gateway) {
#if LWIP_IPV6
    ip6_addr_t a6;
    struct netif* nif;
    s8_t idx = -1;
    XNetLwipCoreLock prot;
    bool ok = false;

    if (!ipv6Address || !ipv6Address[0] ||
        prefixLength < 0 || prefixLength > 128)
        return false;
    if (!ip6addr_aton(ipv6Address, &a6)) return false;
    /* lwIP ????? RA ??,?? IPv6 ?????(????,???
     * ??????);???? lwIP ??????,?????? */
    (void)prefixLength;
    (void)ipv6Gateway;

    XDeviceNetwork_ensureInit();
    prot = XNET_LWIP_LOCK();
    nif = lwipConfig_findNetif(ifIndex);
    if (nif) {
        if (netif_add_ip6_address(nif, &a6, &idx) == ERR_OK &&
            idx >= 0) {
            netif_ip6_addr_set_state(nif, idx, IP6_ADDR_VALID);
            ok = true;
        }
    }
    XNET_LWIP_UNLOCK(prot);
    return ok;
#else
    (void)ifIndex; (void)ipv6Address; (void)prefixLength; (void)ipv6Gateway;
    return false; /* lwIP ??? IPv6 */
#endif
}

bool XDeviceNetwork_setInterfaceStatic(uint32_t ifIndex, const char* ipv4Address,
                                       const char* ipv4Netmask,
                                       const char* ipv4Gateway,
                                       const char* dnsPrimary,
                                       const char* dnsSecondary) {
    ip4_addr_t ip4;
    ip4_addr_t mask4;
    ip4_addr_t gw4;
    ip4_addr_t dns4;
    ip_addr_t dnsA;
    struct netif* nif;
    XNetLwipCoreLock prot;
    bool hasGw;
    bool hasDns1;
    bool hasDns2;
    bool ok = false;

    if (!lwipConfig_parseIpv4(ipv4Address, &ip4) ||
        !lwipConfig_parseIpv4(ipv4Netmask, &mask4))
        return false;
    hasGw = (ipv4Gateway && ipv4Gateway[0]);
    if (hasGw && !lwipConfig_parseIpv4(ipv4Gateway, &gw4)) return false;
    hasDns1 = (dnsPrimary && dnsPrimary[0]);
    if (hasDns1 && !lwipConfig_parseIpv4(dnsPrimary, &dns4)) return false;
    hasDns2 = (dnsSecondary && dnsSecondary[0]);

    XDeviceNetwork_ensureInit();
    prot = XNET_LWIP_LOCK();
    nif = lwipConfig_findNetif(ifIndex);
    if (nif) {
        /* DHCP ??????(netif_set_addr ? DHCP ????)?? lwIP
         * ??? netif_set_addr ?? ip4_addr(???? ip_addr_t)? */
#if LWIP_DHCP
        dhcp_stop(nif);
#endif
        netif_set_addr(nif, &ip4, &mask4, hasGw ? &gw4 : NULL);
#if LWIP_DNS
        if (hasDns1) ip_addr_copy_from_ip4(dnsA, dns4);
        dns_setserver(0, hasDns1 ? &dnsA : NULL);
        if (hasDns2) {
            if (lwipConfig_parseIpv4(dnsSecondary, &dns4)) {
                ip_addr_copy_from_ip4(dnsA, dns4);
                dns_setserver(1, &dnsA);
            }
        } else {
            dns_setserver(1, NULL);
        }
#endif
        ok = true;
    }
    XNET_LWIP_UNLOCK(prot);
    return ok;
}

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
 * ???? - lwIP ???????? tcp_write/tcp_output ????,
 * ?????????????? xssl_v_writeData -> BIO -> parent writeData
 * ??????????,???????
 * ================================================================ */
void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !ringBuffer) return;
    (void)isUdp;
    /* lwIP: ???? tcp_write/tcp_output ????,
     * ????????? XDeviceNetwork_socketWrite ???????? */
}
#endif /* XNETWORK_USE_LWIP */
