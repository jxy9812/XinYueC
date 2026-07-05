/**
 * @file XNetwork_lwip.c
 * @brief lwIP 平台网络实现（平台无关 + Raw API 回调模式）
 *
 * ============ 架构分层 ============
 *  XNetwork_platform.h                     ← 平台抽象接口
 *      │
 *      ├── Drive/windows/XNetwork_win32.c   ← Windows IOCP 实现
 *      └── Library/lwip/XNetwork_lwip.c     ← lwIP 适配层 (平台无关)
 *              │
 *              └── Drive/windows/XNetwork/XNetwork_lwip_win32.c ← Windows 虚拟网卡
 *
 * 设计原则：
 * - XMalloc_System  用于结构体/PCB/大缓冲区
 * - XMalloc_Hybrid  用于小块临时数据
 * - lwIP Raw API 回调驱动, 不依赖 LWIP_SOCKET/LWIP_NETCONN
 * - sys_arch 实现在 arch/sys_arch.c 中 (始终编译)
 * - lwIP 定时器用 XTimeWheelGroup_global() 驱动
 * - sys_now 用 XDateTime_currentMSecsSinceEpoch
 * - 套接字标识用 XFd 统一管理 (XFD_TYPE_SOCKET)
 */

/* ================================================================ */
/*  包含                                                              */
/* ================================================================ */
#include "XNetwork_config.h"

#ifdef XNETWORK_USE_LWIP

#include "XNetwork_platform.h"
#include "XNetwork_lwip_platform.h"

/* XinYueC 框架 */
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

/* XTimer */
#include "XTimeWheelGroup.h"
#include "XTimerData.h"

/* lwIP */
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

#include <string.h>
#include <stdio.h>

/* ================================================================ */
/*  内部宏                                                             */
/* ================================================================ */

/** @brief 将基类指针转换为私有扩展类型 */
#define L4P(p)          ((XNetworkSocketPrivateLwip*)(p))

/** @brief lwIP 超时检查间隔 (毫秒) */
#define LWIP_TICK_MS    250

/* ================================================================ */
/*  lwIP 定时器驱动 ── XTimeWheelGroup_global()                        */
/* ================================================================ */

static XHandle g_lwipTickHandle = 0;

/**
 * @brief lwIP 超时检查定时器回调
 * @param userData 用户数据（未使用）
 * @param timer    定时器数据
 * @note XTimeWheelGroup 周期性自动触发，无需重新注册
 */
static void lwip_tick_cb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    sys_check_timeouts();
}

/**
 * @brief 启动 lwIP 周期性超时检查定时器
 * @note 使用 XTimeWheelGroup_global() 全局时间轮，250ms 周期
 */
static void start_lwip_tick(void)
{
    XTimerData d;
    XTimerData_init(&d, NULL);
    XTimerData_setTimeout(&d, LWIP_TICK_MS);
    XTimerData_setInterval(&d, LWIP_TICK_MS);
    XTimerData_setTimerCallback(&d, lwip_tick_cb);
    XTimerData_setSingleShot(&d, false);  /* 周期性 */
    g_lwipTickHandle = XTimeWheelGroup_addTimerMs_base(XTimeWheelGroup_global(), d);
}

/* ================================================================ */
/*  全局状态                                                           */
/* ================================================================ */

static int            g_lwipRef      = 0;       /**< lwIP 引用计数 */
static bool           g_lwipInited   = false;   /**< 是否已初始化 */
static struct netif*  g_defaultNetif = NULL;    /**< 默认网络接口 */

/* ================================================================ */
/*  私有数据结构                                                        */
/* ================================================================ */

/**
 * @brief lwIP 套接字私有数据扩展
 * @note 第一位必须为 XNetworkSocketPrivate 基类（与 Win32 实现一致）
 */
typedef struct {
    XNetworkSocketPrivate base;         /**< 基类（owner + notifiers） */
    struct tcp_pcb*       tpcb;         /**< TCP 控制块 */
    struct udp_pcb*       upcb;         /**< UDP 控制块 */
    bool                  connected;    /**< 是否已连接 */
    bool                  isServer;     /**< 是否为服务端 */
    bool                  closing;      /**< 正在关闭 */
    int                   sockType;     /**< 套接字类型 (XNetworkSocketType) */
    char*                 rxBuf;        /**< 接收缓冲区 (XMalloc_System 分配) */
    int                   rxPos;        /**< 接收缓冲区有效数据长度 */
    int                   rxTotal;      /**< 上次接收的总字节数 */
    ip_addr_t             fromAddr;     /**< UDP 数据报来源地址 */
    uint16_t              fromPort;     /**< UDP 数据报来源端口 */
    size_t                writeFinished;/**< 已发送字节计数 */
    void*                 pendingAccept;/**< 服务器待 Accept 的客户端私有数据 */
} XNetworkSocketPrivateLwip;

/* ================================================================ */
/*  地址转换 (XHostAddress ↔ ip_addr_t)                                */
/* ================================================================ */

/**
 * @brief XHostAddress → lwIP ip_addr_t 转换
 * @param a  XinYueC 地址对象
 * @param ip 输出 lwIP 地址
 */
static void addr_to_ip(const XHostAddress* a, ip_addr_t* ip)
{
    memset(ip, 0, sizeof(*ip));
    if (!a) return;
    if (XHostAddress_protocol(a) == XHostAddress_IPv6Protocol) {
        IP_SET_TYPE_VAL(*ip, IPADDR_TYPE_V6);
        XHostAddress_toIPv6Address(a, (void*)ip_2_ip6(ip));
    } else {
        IP_SET_TYPE_VAL(*ip, IPADDR_TYPE_V4);
        ip4_addr_set_u32(ip_2_ip4(ip), lwip_htonl(XHostAddress_toIPv4Address(a)));
    }
}

/**
 * @brief lwIP ip_addr_t → XHostAddress 转换
 * @param ip lwIP 地址
 * @param a  输出 XinYueC 地址对象
 */
static void ip_to_addr(const ip_addr_t* ip, XHostAddress* a)
{
    if (IP_IS_V6_VAL(*ip))
        XHostAddress_setAddressIPv6(a, (const uint8_t*)ip_2_ip6(ip));
    else
        XHostAddress_setAddressIPv4(a, lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(ip))));
}

/* ================================================================ */
/*  错误转换 (lwIP err_t → 类 POSIX errno)                             */
/* ================================================================ */

int XNetworkLwip_err_to_errno(int e)
{
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

/* ================================================================ */
/*  TCP Raw API 回调                                                   */
/* ================================================================ */

/**
 * @brief TCP 数据接收回调 —— 将 pbuf 数据拷贝到内部环形缓冲区
 */
static err_t tcpRecvCb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err)
{
    (void)err;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s) return ERR_ABRT;
    if (!p) { s->closing = true; return ERR_OK; }

    int total = p->tot_len;
    if (total > 0 && s->rxBuf) {
        int space = XNETWORK_LWIP_RECV_BUFFER_SIZE - s->rxPos;
        int copy  = total < space ? total : space;
        pbuf_copy_partial(p, s->rxBuf + s->rxPos, copy, 0);
        s->rxPos  += copy;
        s->rxTotal = copy;
    }
    tcp_recved(pcb, total);
    pbuf_free(p);
    return ERR_OK;
}

/** @brief TCP 数据发送完成回调 —— 累加已发送字节计数 */
static err_t tcpSentCb(void* arg, struct tcp_pcb* pcb, u16_t len)
{
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (s) s->writeFinished += len;
    return ERR_OK;
}

/** @brief TCP 错误回调 —— 标记关闭 */
static void tcpErrCb(void* arg, err_t err)
{
    (void)err;
    if (arg) ((XNetworkSocketPrivateLwip*)arg)->closing = true;
}

/** @brief TCP 轮询回调 —— 保持连接活跃 */
static err_t tcpPollCb(void* arg, struct tcp_pcb* pcb) { (void)arg; (void)pcb; return ERR_OK; }

/**
 * @brief TCP Accept 回调 —— 接收新客户端连接
 * @details 为每个新连接分配独立的 XNetworkSocketPrivateLwip，存储到服务端 pendingAccept
 */
static err_t tcpAcceptCb(void* arg, struct tcp_pcb* newPcb, err_t err)
{
    (void)err;
    XNetworkSocketPrivateLwip* ss = (XNetworkSocketPrivateLwip*)arg;
    if (!ss || !newPcb) return ERR_ABRT;

    XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*cs));
    if (!cs) return ERR_MEM;
    memset(cs, 0, sizeof(*cs));
    cs->tpcb      = newPcb;
    cs->connected = true;
    cs->sockType  = XNetwork_Tcp;
    cs->rxBuf     = (char*)XMalloc_System(XNETWORK_LWIP_RECV_BUFFER_SIZE);
    if (!cs->rxBuf) { XFree_System(cs); return ERR_MEM; }

    tcp_arg(newPcb, cs);
    tcp_recv(newPcb, tcpRecvCb);
    tcp_sent(newPcb, tcpSentCb);
    tcp_err(newPcb, tcpErrCb);
    tcp_poll(newPcb, tcpPollCb, 4);

    ss->pendingAccept = cs;   /* 暂存，等待 XNetwork_serverGetAcceptedSocket 取出 */
    return ERR_OK;
}

/* ================================================================ */
/*  UDP Raw API 回调                                                   */
/* ================================================================ */

static void udpRecvCb(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                       const ip_addr_t* addr, u16_t port)
{
    (void)pcb;
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)arg;
    if (!s || !p) return;

    s->fromAddr = *addr;
    s->fromPort = port;
    int total = p->tot_len;
    if (total > 0 && s->rxBuf) {
        int space = XNETWORK_LWIP_RECV_BUFFER_SIZE - s->rxTotal;
        int copy  = total < space ? total : space;
        pbuf_copy_partial(p, s->rxBuf + s->rxTotal, copy, 0);
        s->rxPos  += copy;
        s->rxTotal += copy;
    }
    pbuf_free(p);
}

/** @brief DNS 异步回调 —— 将解析结果写入 XHostAddress 指针 */
static void dnsAsyncCb(const char* name, const ip_addr_t* ip, void* arg)
{
    (void)name;
    XHostAddress** out = (XHostAddress**)arg;
    if (ip && out) {
        *out = (XHostAddress*)XMalloc_System(sizeof(XHostAddress));
        if (*out) {
            XHostAddress_init(*out);
            ip_to_addr(ip, *out);
        }
    }
}

/* ================================================================ */
/*  平台初始化/清理                                                     */
/* ================================================================ */

void XNetwork_ensureInit(void)
{
    if (++g_lwipRef == 1 && !g_lwipInited) {
        lwip_init();
        g_lwipInited = true;
        start_lwip_tick();
        /* 初始化平台网卡（loopback + Npcap 物理网卡） */
        XNetworkLwip_platform_init();
    }
}

void XNetwork_cleanup(void)
{
    if (--g_lwipRef == 0) {
        /* 清理平台网卡 */
        XNetworkLwip_platform_deinit();
        g_lwipInited    = false;
        g_defaultNetif  = NULL;
        if (g_lwipTickHandle) {
            XTimeWheelGroup_removeTimer_base(XTimeWheelGroup_global(), g_lwipTickHandle);
            g_lwipTickHandle = 0;
        }
    }
}

int  XNetwork_lastError(void)                     { return 0; }
bool XNetwork_isEAgain(int err)                   { return err == -6; }
char* XNetwork_errorString(int code)
{
    char* buf = (char*)XMalloc_System(128);
    if (buf) snprintf(buf, 128, "lwIP err %d", code);
    return buf;
}

/* ================================================================ */
/*  私有数据管理                                                        */
/* ================================================================ */

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner)
{
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)XMalloc_System(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->base.owner = owner;
    s->rxBuf      = (char*)XMalloc_System(XNETWORK_LWIP_RECV_BUFFER_SIZE);
    if (!s->rxBuf) { XFree_System(s); return NULL; }
    return (XNetworkSocketPrivate*)s;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
    if (s->rxBuf) XFree_System(s->rxBuf);
    if (priv->owner) {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd >= 0) { XFd_free(xfd); XIODevice_setFd((XIODevice*)priv->owner, XFD_INVALID); }
    }
    XFree_System(s);
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv)
    { return priv ? (intptr_t)L4P(priv) : -1; }
XFd XNetwork_socketFd(const XNetworkSocketPrivate* priv)
    { return (priv && priv->owner) ? XIODevice_fd((XIODevice*)priv->owner) : XFD_INVALID; }
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv)
    { return priv ? L4P(priv)->connected : false; }

/* ================================================================ */
/*  绑定                                                              */
/* ================================================================ */

uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType)
{
    (void)reuseAddr; (void)shareAddr;
    if (!priv || !address) return 0;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetwork_ensureInit();
    s->sockType = (int)sockType;

    ip_addr_t ip; addr_to_ip(address, &ip);

    if (sockType == XNetwork_Tcp) {
        s->tpcb = tcp_new(); if (!s->tpcb) return 0;
        tcp_arg(s->tpcb, s);
        tcp_recv(s->tpcb, tcpRecvCb); tcp_sent(s->tpcb, tcpSentCb);
        tcp_err(s->tpcb, tcpErrCb); tcp_poll(s->tpcb, tcpPollCb, 4);
        if (tcp_bind(s->tpcb, &ip, port) != ERR_OK) { tcp_close(s->tpcb); s->tpcb = NULL; return 0; }
    } else {
        s->upcb = udp_new(); if (!s->upcb) return 0;
        if (udp_bind(s->upcb, &ip, port) != ERR_OK) { udp_remove(s->upcb); s->upcb = NULL; return 0; }
        udp_recv(s->upcb, udpRecvCb, s);
    }
    s->connected = true;
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }
    return port;
}

/* ================================================================ */
/*  连接                                                              */
/* ================================================================ */

bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, XNetworkProtocol protocol,
                            XNetworkSocketType sockType)
{
    (void)protocol;
    if (!priv || !hostName) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetwork_ensureInit(); s->sockType = (int)sockType;

    ip_addr_t ip;
    err_t err = dns_gethostbyname(hostName, &ip, NULL, NULL);
    if (err != ERR_OK && ipaddr_aton(hostName, &ip) == 0) return false;

    if (sockType == XNetwork_Tcp) {
        s->tpcb = tcp_new(); if (!s->tpcb) return false;
        tcp_arg(s->tpcb, s);
        tcp_recv(s->tpcb, tcpRecvCb); tcp_sent(s->tpcb, tcpSentCb);
        tcp_err(s->tpcb, tcpErrCb); tcp_poll(s->tpcb, tcpPollCb, 4);
        err = tcp_connect(s->tpcb, &ip, port, NULL);
        if (err != ERR_OK) { tcp_close(s->tpcb); s->tpcb = NULL; return false; }
    } else {
        s->upcb = udp_new(); if (!s->upcb) return false;
        udp_recv(s->upcb, udpRecvCb, s);
        err = udp_connect(s->upcb, &ip, port);
        if (err != ERR_OK) { udp_remove(s->upcb); s->upcb = NULL; return false; }
    }
    s->connected = true;
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }
    return true;
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (s->tpcb) { tcp_close(s->tpcb); s->tpcb = NULL; }
    if (s->upcb) { udp_remove(s->upcb); s->upcb = NULL; }
    s->connected = false;
}

/* ================================================================ */
/*  读写                                                              */
/* ================================================================ */

int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                             XNetworkSocketType sockType, void* ringBuffer)
{
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

int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                              XNetworkSocketType sockType, const XHostAddress* destAddr,
                              uint16_t destPort, void* ringBuffer)
{
    (void)ringBuffer;
    if (!priv || !buf || len <= 0) return -1;
    XNetworkSocketPrivateLwip* s = L4P(priv);

    if (sockType == XNetwork_Tcp) {
        if (!s->tpcb) return -1;
        err_t e = tcp_write(s->tpcb, buf, (u16_t)len, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) { tcp_output(s->tpcb); return len; }
        return -1;
    } else {
        if (!s->upcb) return -1;
        ip_addr_t dst; addr_to_ip(destAddr, &dst);
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
        if (!p) return -1;
        memcpy(p->payload, buf, (size_t)len);
        err_t e = udp_sendto(s->upcb, p, &dst, destPort);
        pbuf_free(p);
        return (e == ERR_OK) ? len : -1;
    }
}

/* ================================================================ */
/*  事件处理 ─ 驱动 sys_check_timeouts()                                */
/* ================================================================ */

bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event)
{
    (void)priv; (void)event;
    sys_check_timeouts();
    return false;
}

bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, int state, int openMode)
{
    if (!priv || fd == -1) return false;
    (void)openMode;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    
    /* fd 应是 XFd 通过 XFd_handle 获取的 lwIP PCB 指针 */
    /* state: 3 = ConnectedState */
    if (s->tpcb) {
        s->connected = (state == 3);
        {
            XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
            if (xfd == XFD_INVALID) {
                xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
                XIODevice_setFd((XIODevice*)priv->owner, xfd);
            }
        }
        return true;
    }
    if (s->upcb) {
        s->connected = true;
        {
            XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
            if (xfd == XFD_INVALID) {
                xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
                XIODevice_setFd((XIODevice*)priv->owner, xfd);
            }
        }
        return true;
    }
    return false;
}

/* ================================================================ */
/*  套接字选项                                                         */
/* ================================================================ */

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value)
{
    /* option 枚举: 0=NoDelay, 1=Keepalive, 2=MulticastTTL, 3=MulticastLoop,
                    4=TOS, 5=SendBuf, 6=RecvBuf, 7=MTU, 8=Broadcast */
    if (!priv) return false;
    int v = value ? *(int*)value : 0;
    switch (option) {
    case 0: if (L4P(priv)->tpcb) { if (v) tcp_nagle_disable(L4P(priv)->tpcb); else tcp_nagle_enable(L4P(priv)->tpcb); } return true;
    case 1: if (L4P(priv)->tpcb) L4P(priv)->tpcb->so_options |= SOF_KEEPALIVE; return true;
    case 2: return true; /* TTL */
    case 3: return true;
    case 4: return true;
    case 5: if (L4P(priv)->tpcb) { L4P(priv)->tpcb->snd_buf = (u16_t)v; return true; } break;
    case 6: return true;
    default: break;
    }
    return false;
}

void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option)
{
    static int r;
    if (!priv) return NULL;
    switch (option) {
    case 5: r = L4P(priv)->tpcb ? (int)L4P(priv)->tpcb->snd_buf : 0; return &r;
    case 6: r = XNETWORK_LWIP_RECV_BUFFER_SIZE; return &r;  /* RecvBuf */
    case 7: r = TCP_MSS; return &r;                           /* MTU */
    default: break;
    }
    return NULL;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t sz) { (void)priv; (void)sz; }

/* ================================================================ */
/*  异步读取状态                                                        */
/* ================================================================ */

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv)
    { return priv ? L4P(priv)->rxBuf : NULL; }
size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv)
    { return priv ? (size_t)L4P(priv)->rxTotal : 0; }
size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv)
    { return priv ? L4P(priv)->writeFinished : 0; }
void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp) { (void)priv; (void)isUdp; }

/* ================================================================ */
/*  TCP 服务器                                                         */
/* ================================================================ */

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr)
{
    (void)backlog; (void)reuseAddr;
    if (!priv) return -1;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    XNetwork_ensureInit();

    s->tpcb = tcp_new(); if (!s->tpcb) return -1;
    ip_addr_t ip; addr_to_ip(addr, &ip);
    if (tcp_bind(s->tpcb, &ip, port) != ERR_OK) { tcp_close(s->tpcb); s->tpcb = NULL; return -1; }
    s->tpcb = tcp_listen(s->tpcb);
    if (!s->tpcb) return -1;
    tcp_accept(s->tpcb, tcpAcceptCb);
    s->isServer  = true;
    s->connected = true;
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }
    return (XServerHandle)s->tpcb;
}

uint16_t XNetwork_serverPort(XServerHandle server)
    { struct tcp_pcb* p = (struct tcp_pcb*)server; return p ? p->local_port : 0; }
void XNetwork_serverClose(XServerHandle server)
    { if (server != (XServerHandle)-1) tcp_close((struct tcp_pcb*)server); }

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv,
                                                XHostAddress* clientAddr, uint16_t* clientPort)
{
    if (!priv) return -1;
    XNetworkSocketPrivateLwip* ss = L4P(priv);
    XNetworkSocketPrivateLwip* cs = (XNetworkSocketPrivateLwip*)ss->pendingAccept;
    if (!cs) return -1;
    ss->pendingAccept = NULL;
    if (clientAddr && cs->tpcb) ip_to_addr(&cs->tpcb->remote_ip, clientAddr);
    if (clientPort && cs->tpcb) *clientPort = cs->tpcb->remote_port;
    return (XSocketHandle)cs;
}

bool XNetwork_serverContinueAccept(XNetworkSocketPrivate* priv)
    { if (priv) L4P(priv)->pendingAccept = NULL; return true; }

/* ================================================================ */
/*  DNS                                                               */
/* ================================================================ */

bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count)
{
    if (!name || !addrs || !count) return false;
    *addrs = NULL; *count = 0;

    ip_addr_t ip;
    err_t e = dns_gethostbyname(name, &ip, dnsAsyncCb, addrs);
    if (e == ERR_OK) {
        XHostAddress* out = (XHostAddress*)XMalloc_System(sizeof(XHostAddress));
        if (!out) return false;
        XHostAddress_init(out); ip_to_addr(&ip, out);
        *addrs = out; *count = 1;
        return true;
    }
    if (e == ERR_INPROGRESS) return false;
    if (ipaddr_aton(name, &ip)) {
        XHostAddress* out = (XHostAddress*)XMalloc_System(sizeof(XHostAddress));
        if (!out) return false;
        XHostAddress_init(out); ip_to_addr(&ip, out);
        *addrs = out; *count = 1;
        return true;
    }
    return false;
}

char* XNetwork_localHostName(void)
    { char* b = (char*)XMalloc_System(64); if (b) snprintf(b, 64, "lwip-device"); return b; }

/* ================================================================ */
/*  网络接口枚举                                                        */
/* ================================================================ */

typedef struct { struct netif* next; int idx; } LwipIfIter;

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void)
{
    LwipIfIter* it = (LwipIfIter*)XMalloc_Hybrid(sizeof(LwipIfIter));
    if (!it) return NULL;
    it->next = netif_list; it->idx = 0;
    return (XNetworkInterfaceIterator)it;
}

XNetworkInterface* XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter)
{
    LwipIfIter* it = (LwipIfIter*)iter;
    if (!it || !it->next) return NULL;
    struct netif* nif = it->next; it->next = nif->next;

    XNetworkInterface* iface = (XNetworkInterface*)XMalloc_System(sizeof(XNetworkInterface));
    if (!iface) return NULL;
    memset(iface, 0, sizeof(*iface));
    /* 注: 以下字段需匹配实际的 XNetworkInterface 结构体定义 */
    /* m_name / m_flags 字段名请根据实际头文件调整 */
    if (nif->flags & NETIF_FLAG_UP)        /* iface->m_flags |= XNetworkIf_Up */;
    if (nif->flags & NETIF_FLAG_LINK_UP)   /* iface->m_flags |= XNetworkIf_Running */;
    it->idx++;
    return iface;
}

void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter)
    { if (iter) XFree_Hybrid(iter); }

XVector* XNetwork_getInterfaceAddresses(const XString* ifname)
    { (void)ifname; return NULL; }

/* ================================================================ */
/*  多播                                                              */
/* ================================================================ */

bool XNetwork_multicastGroup(XSocketHandle sock, bool join, const XHostAddress* group, uint32_t ifIdx)
    { (void)sock;(void)join;(void)group;(void)ifIdx; return false; }
int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg)
    { (void)sock;(void)op;(void)arg; return -1; }

/* ================================================================ */
/*  UDP 特有                                                          */
/* ================================================================ */

bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv, XHostAddress* src, uint16_t* port)
{
    if (!priv) return false;
    XNetworkSocketPrivateLwip* s = L4P(priv);
    if (src) ip_to_addr(&s->fromAddr, src);
    if (port) *port = s->fromPort;
    return true;
}

int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t sz,
                               const XHostAddress* addr, uint16_t port)
{
    XNetworkSocketPrivateLwip* s = (XNetworkSocketPrivateLwip*)sock;
    if (!s || !s->upcb || !data || sz <= 0) return -1;
    ip_addr_t dst; addr_to_ip(addr, &dst);
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)sz, PBUF_RAM);
    if (!p) return -1;
    memcpy(p->payload, data, (size_t)sz);
    err_t e = udp_sendto(s->upcb, p, &dst, port);
    pbuf_free(p);
    return (e == ERR_OK) ? sz : -1;
}

/* ================================================================ */
/*  代理 / GSSAPI (lwIP 不支持)                                       */
/* ================================================================ */

bool XNetwork_getSystemProxy(const char* url, XNetworkProxy* out) { (void)url;(void)out; return false; }
int  XNetwork_gssapiAuth(const char* name, const XByteArray* in, XByteArray* out, void** ctx)
    { (void)name;(void)in;(void)out;(void)ctx; return -1; }

/* ================================================================ */
/*  额外平台接口                                                        */
/* ================================================================ */

struct netif* XNetworkLwip_defaultNetif(void)      { return g_defaultNetif; }
void XNetworkLwip_setDefaultNetif(struct netif* n) { g_defaultNetif = n; }

#endif /* XNETWORK_USE_LWIP */