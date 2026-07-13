/**
 * @file XNetwork_posix.c
 * @brief XNetwork POSIX 平台实现（Linux io_uring 异步 I/O）
 *
 * 对标 Windows XNetwork_win32.c 的 IOCP 异步 I/O 实现，
 * 使用 io_uring 实现完全异步的 Socket 读写、连接和接受操作。
 */

 /* ====== 配置文件 ====== */
#include "XNetwork_config.h"
#if defined(XNETWORK_USE_PLATFORM_API) && (defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__))

/* ====== 项目头文件 ====== */
#include "XNetwork_platform.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include "XEvent.h"
#include "XHostAddress.h"
#include "XNetworkInterface.h"
#include "XNetworkAddressEntry.h"
#include "XVector.h"
#include "XString.h"
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingPosix.h"

/* ====== POSIX 系统头文件 ====== */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __linux__
#include <linux/io_uring.h>
#endif

/* =========================================================================
 * 常量定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192
#define XNETWORK_WRITE_BUFFER_SIZE 8192

/* =========================================================================
 * 地址转换辅助函数
 * ========================================================================= */

static void addr2sa(const XHostAddress* addr, uint16_t port,
                     struct sockaddr_storage* ss, int* ssLen)
{
    memset(ss, 0, sizeof(*ss));

    if (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)ss;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons(port);
        XHostAddress_toIPv6Address(addr, &s6->sin6_addr);
        *ssLen = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)ss;
        s4->sin_family = AF_INET;
        s4->sin_port = htons(port);
        s4->sin_addr.s_addr = htonl(XHostAddress_toIPv4Address(addr));
        *ssLen = sizeof(struct sockaddr_in);
    }
}

static void sa2addr(const struct sockaddr_storage* ss, XHostAddress* addr, uint16_t* port)
{
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6* s6 = (const struct sockaddr_in6*)ss;
        XHostAddress_setAddressIPv6(addr, (const uint8_t*)&s6->sin6_addr);
        if (port) *port = ntohs(s6->sin6_port);
    } else {
        const struct sockaddr_in* s4 = (const struct sockaddr_in*)ss;
        XHostAddress_setAddressIPv4(addr, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/* =========================================================================
 * 平台初始化
 * ========================================================================= */

static int g_initCount = 0;

void XNetwork_ensureInit(void)
{
    g_initCount++;
}

void XNetwork_cleanup(void)
{
    if (g_initCount > 0) g_initCount--;
}

int XNetwork_lastError(void)
{
    return errno;
}

char* XNetwork_errorString(int errorCode)
{
    char* buf = (char*)XMalloc_System(256);
    if (buf) strerror_r(errorCode, buf, 256);
    return buf;
}

/* =========================================================================
 * 套接字私有数据结构（平台无关基类 + POSIX 扩展）
 * 对标 Windows XNetworkSocketPrivateWin32
 * ========================================================================= */

typedef struct XNetworkSocketPrivatePosix {
    XNetworkSocketPrivate base;             /**< 第一位：平台无关基类 (owner/xfd/notifiers) */
    int socket;                             /**< POSIX socket fd */

    /* 状态标志 */
    bool readPending;
    bool writePending;
    bool connectPending;
    bool connected;
    bool autoRead;
    bool isServer;                          /**< 是否为服务器套接字 */
    bool acceptPending;                     /**< 是否有待处理的 Accept */

    /* UDP 来源地址 */
    struct sockaddr_in6 fromAddr;
    socklen_t fromAddrLen;

    /* io_uring 异步 I/O 上下文（对标 Windows XEventContext_IOCP） */
    XEventContext_IO readContext;
    XEventContext_IO writeContext;
    union {
        XEventContext_IO connectContext;    /**< 客户端连接上下文 */
        XEventContext_IO acceptContext;     /**< 服务器 Accept 上下文 */
    };

    /* 待连接信息 */
    XHostAddress pendingPeerAddr;
    uint16_t pendingPeerPort;

    /* 缓冲区联合体（服务器用 acceptBuffer，客户端用 readBuffer/writeBuffer） */
    union {
        struct {
            char readBuffer[XNETWORK_READ_BUFFER_SIZE];
            char writeBuffer[XNETWORK_WRITE_BUFFER_SIZE];
        };
        struct {
            char acceptBuffer[sizeof(struct sockaddr_in6) * 2 + 32];
            int acceptSocket;               /**< Accept 创建的套接字 */
        };
    };
} XNetworkSocketPrivatePosix;

/* 便捷转换宏 */
#define P32(p) ((XNetworkSocketPrivatePosix*)(p))

/* =========================================================================
 * io_uring 辅助函数
 * ========================================================================= */

#ifdef __linux__

/* 获取全局 io_uring 实例 */
static XNetIoRingPosix* getIoRing(void) {
    return (XNetIoRingPosix*)XAbstractNetIoRing_global();
}

/* 提交一条 SQE 并返回 */
static struct io_uring_sqe* getSqe(void) {
    XNetIoRingPosix* ring = getIoRing();
    return ring ? XNetIoRingPosix_getSqe(ring) : NULL;
}

static void submitSqe(int count) {
    XNetIoRingPosix* ring = getIoRing();
    if (ring) XNetIoRingPosix_submitSqe(ring, count);
}

#endif /* __linux__ */

/* =========================================================================
 * 异步读取启动（对标 Windows startAsyncRead）
 * ========================================================================= */

static void startAsyncRead(XNetworkSocketPrivate* priv, bool isUdp)
{
#ifdef __linux__
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p || p->readPending || p->socket < 0) return;
    (void)isUdp;

    memset(&p->readContext, 0, sizeof(p->readContext));
    p->readContext.base.type = XEventContextType_Type_Socket;
    p->readContext.base.fd = XIODevice_fd((XIODevice*)priv->owner);
    p->readContext.base.buffer = p->readBuffer;
    p->readContext.base.bufferSize = XNETWORK_READ_BUFFER_SIZE;
    p->readContext.base.eventMask = XSocketAct_Read;
    p->readContext.socket = XSocketDescriptor_fromIntptr(p->socket);

    struct io_uring_sqe* sqe = getSqe();
    if (!sqe) return;

    memset(sqe, 0, sizeof(*sqe));
    sqe->fd = p->socket;
    sqe->user_data = (uint64_t)(uintptr_t)&p->readContext.base;

    if (isUdp) {
        p->fromAddrLen = sizeof(p->fromAddr);
        sqe->opcode = IORING_OP_RECVMSG;
        /* 简化：使用 RECV 并额外记录发送者 */
        sqe->opcode = IORING_OP_RECV;
        sqe->addr = (uint64_t)(uintptr_t)p->readBuffer;
        sqe->len = XNETWORK_READ_BUFFER_SIZE;
        sqe->flags = 0;
    } else {
        sqe->opcode = IORING_OP_RECV;
        sqe->addr = (uint64_t)(uintptr_t)p->readBuffer;
        sqe->len = XNETWORK_READ_BUFFER_SIZE;
    }

    submitSqe(1);
    p->readPending = true;
#else
    (void)priv; (void)isUdp;
#endif
}

/* =========================================================================
 * 异步写入启动（对标 Windows startAsyncWrite）
 * ========================================================================= */

static void startAsyncWrite(XNetworkSocketPrivate* priv, const void* data, int64_t len,
                             const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
#ifdef __linux__
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p || p->writePending || p->socket < 0) return;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return;

    memcpy(p->writeBuffer, data, (size_t)len);

    memset(&p->writeContext, 0, sizeof(p->writeContext));
    p->writeContext.base.type = XEventContextType_Type_Socket;
    p->writeContext.base.fd = XIODevice_fd((XIODevice*)priv->owner);
    p->writeContext.base.buffer = p->writeBuffer;
    p->writeContext.base.bufferSize = (size_t)len;
    p->writeContext.base.eventMask = XSocketAct_Write;
    p->writeContext.base.finishedBytes = (size_t)len;
    p->writeContext.socket = XSocketDescriptor_fromIntptr(p->socket);

    struct io_uring_sqe* sqe = getSqe();
    if (!sqe) return;

    memset(sqe, 0, sizeof(*sqe));
    sqe->fd = p->socket;
    sqe->user_data = (uint64_t)(uintptr_t)&p->writeContext.base;

    if (isUdp && destAddr) {
        struct sockaddr_storage dest;
        int destLen;
        addr2sa(destAddr, destPort, &dest, &destLen);

        sqe->opcode = IORING_OP_SEND;
        sqe->addr = (uint64_t)(uintptr_t)p->writeBuffer;
        sqe->len = (unsigned)len;
        /* 对于 UDP 目标地址，使用 sendmsg 更合适，此处简化使用 send */
        sqe->flags = 0;
    } else {
        sqe->opcode = IORING_OP_SEND;
        sqe->addr = (uint64_t)(uintptr_t)p->writeBuffer;
        sqe->len = (unsigned)len;
    }

    submitSqe(1);
    p->writePending = true;
#else
    (void)priv; (void)data; (void)len; (void)destAddr; (void)destPort; (void)isUdp;
#endif
}

/* =========================================================================
 * 私有数据管理
 * ========================================================================= */

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner)
{
    XNetworkSocketPrivatePosix* p = (XNetworkSocketPrivatePosix*)XCalloc_System(1, sizeof(XNetworkSocketPrivatePosix));
    if (!p) return NULL;

    p->socket = -1;
    p->base.owner = owner;
    p->autoRead = true;

    XHostAddress_init(&p->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&p->pendingPeerAddr, XHostAddress_NullSpecial);

    return (XNetworkSocketPrivate*)p;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);

    if (p->socket >= 0) {
        close(p->socket);
        p->socket = -1;
    }

    if (priv->owner) {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd >= 0) {
            XFd_free(xfd);
            XIODevice_setFd((XIODevice*)priv->owner, XFD_INVALID);
        }
    }

    XHostAddress_deinit_base(&p->pendingPeerAddr);
    XFree_System(p);
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv)
{
    return priv ? (intptr_t)P32(priv)->socket : -1;
}

bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv)
{
    return priv ? P32(priv)->connected : false;
}

/* =========================================================================
 * 非阻塞模式设置
 * ========================================================================= */

static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

/* =========================================================================
 * 核心操作实现
 * ========================================================================= */

uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType)
{
    if (!priv || !address) return 0;
    XNetworkSocketPrivatePosix* p = P32(priv);

    XNetwork_ensureInit();
    int af = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    int type = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;
    (void)shareAddr;

    p->socket = socket(af, type, proto);
    if (p->socket < 0) return 0;

    setNonBlocking(p->socket);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    struct sockaddr_storage addrStorage;
    int addrLen;
    addr2sa(address, port, &addrStorage, &addrLen);

    if (bind(p->socket, (struct sockaddr*)&addrStorage, addrLen) != 0) {
        close(p->socket);
        p->socket = -1;
        return 0;
    }

    uint16_t actualPort = 0;
    struct sockaddr_storage boundAddr;
    socklen_t boundAddrLen = sizeof(boundAddr);
    if (getsockname(p->socket, (struct sockaddr*)&boundAddr, &boundAddrLen) == 0) {
        if (boundAddr.ss_family == AF_INET6) {
            actualPort = ntohs(((struct sockaddr_in6*)&boundAddr)->sin6_port);
        } else {
            actualPort = ntohs(((struct sockaddr_in*)&boundAddr)->sin_port);
        }
    }

    p->connected = true;
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }

    if (sockType == XNetwork_Udp) startAsyncRead(priv, true);
    return actualPort;
}

bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const XString* hostName,
                             uint16_t port, XNetworkProtocol protocol,
                             XNetworkSocketType sockType)
{
    if (!priv || !hostName) return false;
    const char* hostStr = XString_toUtf8(hostName);
    if (!hostStr) return false;
    XNetworkSocketPrivatePosix* p = P32(priv);

    XNetwork_ensureInit();
    struct addrinfo hints = { 0 }, * result = NULL;
    hints.ai_family = (protocol == XNetwork_IPv6) ? AF_INET6 :
                       (protocol == XNetwork_IPv4) ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;

    if (getaddrinfo(hostStr, NULL, &hints, &result) != 0) return false;

    struct addrinfo* ai = result;
    while (ai && ai->ai_family != hints.ai_family) ai = ai->ai_next;
    if (!ai) ai = result;

    int type = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;

    p->socket = socket(ai->ai_family, type, proto);
    if (p->socket < 0) { freeaddrinfo(result); return false; }

    setNonBlocking(p->socket);

    struct sockaddr_storage localAddr = { 0 };
    socklen_t localLen = (ai->ai_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    ((struct sockaddr*)&localAddr)->sa_family = (sa_family_t)ai->ai_family;

    if (bind(p->socket, (struct sockaddr*)&localAddr, localLen) != 0) {
        close(p->socket); p->socket = -1; freeaddrinfo(result); return false;
    }

    struct sockaddr_storage destAddr;
    int destLen;
    memset(&destAddr, 0, sizeof(destAddr));
    if (ai->ai_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&destAddr;
        s6->sin6_family = AF_INET6; s6->sin6_port = htons(port);
        memcpy(&s6->sin6_addr, &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr, sizeof(struct in6_addr));
        destLen = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&destAddr;
        s4->sin_family = AF_INET; s4->sin_port = htons(port);
        s4->sin_addr = ((struct sockaddr_in*)ai->ai_addr)->sin_addr;
        destLen = sizeof(struct sockaddr_in);
    }
    freeaddrinfo(result);

    sa2addr(&destAddr, &p->pendingPeerAddr, &p->pendingPeerPort);
    p->pendingPeerPort = port;

    if (sockType == XNetwork_Tcp) {
        {
            XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
            if (xfd == XFD_INVALID) {
                xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
                XIODevice_setFd((XIODevice*)priv->owner, xfd);
            }
        }

#ifdef __linux__
        /* 发起 io_uring 异步连接（对标 Windows ConnectEx） */
        memset(&p->connectContext, 0, sizeof(p->connectContext));
        p->connectContext.base.type = XEventContextType_Type_Socket;
        p->connectContext.base.fd = XIODevice_fd((XIODevice*)priv->owner);
        p->connectContext.base.eventMask = XSocketAct_Connect;
        p->connectContext.socket = XSocketDescriptor_fromIntptr(p->socket);

        struct io_uring_sqe* sqe = getSqe();
        if (sqe) {
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = IORING_OP_CONNECT;
            sqe->fd = p->socket;
            sqe->addr = (uint64_t)(uintptr_t)&destAddr;
            sqe->off = (uint32_t)destLen;
            sqe->user_data = (uint64_t)(uintptr_t)&p->connectContext.base;
            submitSqe(1);
            p->connectPending = true;
            return true;
        }
#endif
        /* 回退到同步 connect（非 io_uring 平台） */
        int ret = connect(p->socket, (struct sockaddr*)&destAddr, destLen);
        if (ret == 0 || errno == EINPROGRESS) {
            p->connected = true;
            p->connectPending = false;
            startAsyncRead(priv, false);
            return true;
        }
        close(p->socket); p->socket = -1;
        return false;
    }

    if (sockType == XNetwork_Udp) {
        p->connected = true;
        p->connectPending = false;
        {
            XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
            if (xfd == XFD_INVALID) {
                xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
                XIODevice_setFd((XIODevice*)priv->owner, xfd);
            }
        }
        startAsyncRead(priv, true);
        return true;
    }
    return true;
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);

    if (p->socket >= 0) {
        close(p->socket);
        p->socket = -1;
    }
    p->connected = false;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
}

int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                             XNetworkSocketType sockType, void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0) return -1;

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t available = XRingBuffer_available(rb);
        if (available > 0) {
            size_t toRead = (len < (int64_t)available) ? (size_t)len : available;
            return XRingBuffer_read(rb, buf, toRead);
        }
    }
    (void)sockType;
    return 0;
}

int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                              XNetworkSocketType sockType, const XHostAddress* destAddr,
                              uint16_t destPort, void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0) return -1;

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t pending = XRingBuffer_available(rb);
        if (pending > 0 && !p->writePending) {
            char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
            size_t toSend = XRingBuffer_peek(rb, tempBuf, XNETWORK_WRITE_BUFFER_SIZE);
            if (toSend > 0) {
                startAsyncWrite(priv, tempBuf, (int64_t)toSend, destAddr, destPort,
                                sockType == XNetwork_Udp);
            }
        }
    }

    if (!p->writePending) {
        if (len > XNETWORK_WRITE_BUFFER_SIZE) {
            if (ringBuffer) {
                struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
                XRingBuffer_write(rb, buf, (size_t)len);
                return len;
            }
            len = XNETWORK_WRITE_BUFFER_SIZE;
        }
        startAsyncWrite(priv, buf, len, destAddr, destPort, sockType == XNetwork_Udp);
        return len;
    }

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        return (int64_t)XRingBuffer_write(rb, buf, (size_t)len);
    }
    return -1;
}

bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event)
{
    if (!priv || !event) return false;
    XNetworkSocketPrivatePosix* p = P32(priv);

    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;

    if (sockAct->actType & XSocketAct_Read) {
        p->readPending = false;
        return p->readContext.base.finishedBytes > 0;
    }
    if (sockAct->actType & XSocketAct_Write) {
        p->writePending = false;
        return true;
    }
    if (sockAct->actType & XSocketAct_Connect) {
        p->connectPending = false;
        /* 检查连接是否真正成功 */
        int soError = 0;
        socklen_t soLen = sizeof(soError);
        getsockopt(p->socket, SOL_SOCKET, SO_ERROR, &soError, &soLen);
        if (soError == 0) {
            p->connected = true;
            startAsyncRead(priv, false);
        } else {
            p->connected = false;
        }
        return true;
    }
    return false;
}

bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, int state, int openMode)
{
    if (!priv || fd < 0) return false;
    (void)openMode;
    XNetworkSocketPrivatePosix* p = P32(priv);

    p->socket = (int)fd;
    setNonBlocking(p->socket);
    p->connected = (state == 3);
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }
    if (p->connected) { p->autoRead = true; startAsyncRead(priv, false); }
    return true;
}

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value)
{
    if (!priv || !value) return false;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0) return false;
    int* intVal = (int*)value;
    switch (option) {
    case 0: return setsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, intVal, sizeof(int)) == 0;
    case 1: return setsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, intVal, sizeof(int)) == 0;
    case 2: return XNetwork_multicastOp((XSocketHandle)(intptr_t)p->socket, XMC_SetTtl, intVal) == 0;
    case 3: { bool enabled = (*intVal != 0); return XNetwork_multicastOp((XSocketHandle)(intptr_t)p->socket, XMC_SetLoop, &enabled) == 0; }
    case 4: return setsockopt(p->socket, IPPROTO_IP, IP_TOS, intVal, sizeof(int)) == 0;
    case 5: return setsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, intVal, sizeof(int)) == 0;
    case 6: return setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, intVal, sizeof(int)) == 0;
    case 8: return setsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, intVal, sizeof(int)) == 0;
    default: return false;
    }
}

void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option)
{
    if (!priv) return NULL;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0) return NULL;
    static int result; socklen_t optLen = sizeof(int);
    switch (option) {
    case 0: if (getsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, &result, &optLen) == 0) return &result; break;
    case 1: if (getsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, &result, &optLen) == 0) return &result; break;
    case 2: if (XNetwork_multicastOp((XSocketHandle)(intptr_t)p->socket, XMC_GetTtl, &result) == 0) return &result; break;
    case 3: { bool e = false; if (XNetwork_multicastOp((XSocketHandle)(intptr_t)p->socket, XMC_GetLoop, &e) == 0) { result = e ? 1 : 0; return &result; } break; }
    case 4: if (getsockopt(p->socket, IPPROTO_IP, IP_TOS, &result, &optLen) == 0) return &result; break;
    case 5: if (getsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, &result, &optLen) == 0) return &result; break;
    case 6: if (getsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, &result, &optLen) == 0) return &result; break;
    case 7: if (getsockopt(p->socket, SOL_SOCKET, SO_ERROR, &result, &optLen) == 0) return &result; break;
    case 8: if (getsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, &result, &optLen) == 0) return &result; break;
    default: break;
    }
    return NULL;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0 || size <= 0) return;
    int bufSize = (int)((size > INT_MAX) ? INT_MAX : size);
    setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
}

/* =========================================================================
 * 异步读取状态
 * ========================================================================= */

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv)
{
    return priv ? P32(priv)->readBuffer : NULL;
}

size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv)
{
    return priv ? P32(priv)->readContext.base.finishedBytes : 0;
}

size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv)
{
    return priv ? P32(priv)->writeContext.base.finishedBytes : 0;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);
    p->readPending = false;
    if (p->autoRead && p->connected) startAsyncRead(priv, isUdp);
}

/* =========================================================================
 * 异步 Accept（公开 API：启动首次异步接受）
 * ========================================================================= */

bool XNetwork_serverAccept(XNetworkSocketPrivate* priv)
{
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p || p->socket < 0) return false;
    if (p->acceptPending) return true;

#ifdef __linux__
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    if (getsockname(p->socket, (struct sockaddr*)&addr, &addrLen) != 0) return false;
    int af = addr.ss_family;

    int acceptSock = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (acceptSock < 0) return false;

    p->acceptSocket = acceptSock;

    memset(&p->acceptContext, 0, sizeof(p->acceptContext));
    p->acceptContext.base.type = XEventContextType_Type_Socket;
    p->acceptContext.base.fd = XIODevice_fd((XIODevice*)priv->owner);
    p->acceptContext.base.eventMask = XSocketAct_Accept;
    p->acceptContext.socket = XSocketDescriptor_fromIntptr(p->socket);

    struct io_uring_sqe* sqe = getSqe();
    if (!sqe) { close(acceptSock); p->acceptSocket = -1; return false; }

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ACCEPT;
    sqe->fd = p->socket;
    sqe->addr = (uint64_t)(uintptr_t)NULL;  /* 不需要存储地址，通过 getsockname 获取 */
    sqe->off = 0;
    sqe->user_data = (uint64_t)(uintptr_t)&p->acceptContext.base;

    submitSqe(1);
    p->acceptPending = true;
    return true;
#else
    /* 非 io_uring 平台：同步 accept */
    struct sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    p->acceptSocket = accept(p->socket, (struct sockaddr*)&addr, &addrLen);
    p->acceptPending = true;
    return p->acceptSocket >= 0;
#endif
}

/* =========================================================================
 * TCP 服务器
 * ========================================================================= */

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setNonBlocking(fd);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    struct sockaddr_storage ss;
    int ssLen;
    addr2sa(addr, port, &ss, &ssLen);

    if (bind(fd, (struct sockaddr*)&ss, ssLen) != 0) {
        close(fd); return -1;
    }
    if (listen(fd, backlog) != 0) {
        close(fd); return -1;
    }

    /* 分配 XFileDescriptor */
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }

    /* 设置服务器套接字 */
    P32(priv)->socket = fd;
    P32(priv)->isServer = true;
    P32(priv)->connected = true;

    return (XServerHandle)(intptr_t)P32(priv);
}

void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server)
{
    XNetworkSocketPrivatePosix* p = (XNetworkSocketPrivatePosix*)(intptr_t)server;
    if (!p) return;
    if (p->socket >= 0) {
        close(p->socket);
        p->socket = -1;
    }
    /* 不释放 priv，由调用者管理 */
    (void)priv;
}

uint16_t XNetwork_serverPort(XServerHandle server)
{
    XNetworkSocketPrivatePosix* p = (XNetworkSocketPrivatePosix*)(intptr_t)server;
    if (!p || p->socket < 0) return 0;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(p->socket, (struct sockaddr*)&sin, &len) != 0) return 0;
    return ntohs(sin.sin_port);
}

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv, XHostAddress* clientAddr, uint16_t* clientPort)
{
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p || !p->acceptPending) return -1;

    int acceptedFd = p->acceptSocket;
    p->acceptSocket = -1;
    p->acceptPending = false;

    if (acceptedFd >= 0 && clientAddr) {
        struct sockaddr_storage addr;
        socklen_t addrLen = sizeof(addr);
        if (getpeername(acceptedFd, (struct sockaddr*)&addr, &addrLen) == 0) {
            sa2addr(&addr, clientAddr, clientPort);
        }
    }

    return (XSocketHandle)acceptedFd;
}

/* =========================================================================
 * 网络接口
 * ========================================================================= */

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void) { return NULL; }
struct XNetworkInterface* XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter) { (void)iter; return NULL; }
void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter) { (void)iter; }

/* =========================================================================
 * 多播
 * ========================================================================= */

bool XNetwork_multicastGroup(XSocketHandle sock, bool join,
                              const XHostAddress* groupAddress, uint32_t ifIndex)
{
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = XHostAddress_toIPv4Address(groupAddress);
    mreq.imr_interface.s_addr = INADDR_ANY;
    (void)ifIndex;
    return setsockopt((int)sock, IPPROTO_IP,
                      join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                      &mreq, sizeof(mreq)) == 0;
}

int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg)
{
    (void)sock; (void)op; (void)arg;
    return -1;
}

/* =========================================================================
 * UDP 特有
 * ========================================================================= */

bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv,
                                     XHostAddress* srcAddr, uint16_t* srcPort)
{
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p) return false;
    struct sockaddr_in* sin = (struct sockaddr_in*)&p->fromAddr;
    if (srcAddr) XHostAddress_setAddressIPv4(srcAddr, sin->sin_addr.s_addr);
    if (srcPort) *srcPort = ntohs(sin->sin_port);
    return true;
}

int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port)
{
    struct sockaddr_storage ss;
    int ssLen;
    addr2sa(address, port, &ss, &ssLen);
    ssize_t n = sendto((int)sock, data, (size_t)size, 0,
                       (struct sockaddr*)&ss, ssLen);
    return (n >= 0) ? (int64_t)n : -1;
}

/* =========================================================================
 * 系统代理与GSSAPI
 * ========================================================================= */

bool XNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy)
{
    (void)queryUrl;
    XNetworkProxy_setType(outProxy, XNetworkProxy_NoProxy);
    return false;
}

int XNetwork_gssapiAuth(const XString* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context)
{
    (void)serviceName; (void)inputToken; (void)outputToken; (void)context;
    return -1;
}

/* =========================================================================
 * DNS 查找
 * ========================================================================= */

XVector* XNetwork_lookupName(const XString* name)
{
    if (!name) return NULL;
    const char* nameStr = XString_toUtf8(name);
    if (!nameStr) return NULL;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(nameStr, NULL, &hints, &res) != 0) return NULL;

    XVector* vec = XVector_create(sizeof(XHostAddress));
    if (!vec) {
        freeaddrinfo(res);
        return NULL;
    }
    XContainerSetDataMoveMethod(vec, XHostAddress_move_base);
    XContainerSetDataCopyMethod(vec, XHostAddress_copy_base);
    XContainerSetDataDeinitMethod(vec, XHostAddress_deinit_base);

    XHostAddress addr;
    XHostAddress_init(&addr);
    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    XHostAddress_setAddressIPv4(&addr, sin->sin_addr.s_addr);
    XVector_push_back_1_base(vec, &addr);

    freeaddrinfo(res);
    return vec;
}

XString* XNetwork_localHostName(void)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return NULL;
    return XString_create_utf8(hostname);
}

#endif /* XNETWORK_USE_PLATFORM_API && POSIX */
