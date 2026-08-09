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
#include "XNetwork.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include "XEvent.h"
#include "XHostAddress.h"
#include "XAbstractSocket.h"
#include "XNetworkInterface.h"
#include "XNetworkAddressEntry.h"
#include "XVector.h"
#include "XString.h"
#include "XDateTime.h"
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
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/io_uring.h>
#endif
#ifdef HAVE_GSSAPI
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#endif

/* =========================================================================
 * 常量定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192
#define XNETWORK_WRITE_BUFFER_SIZE 8192

/* ICMP Echo 报文头固定为 type、code、checksum、identifier、sequence。 */
#define XNETWORK_ICMP_HEADER_SIZE 8u

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

static uint16_t xnetwork_icmp_checksum(const uint8_t* data, size_t size)
{
    uint32_t sum = 0;
    while (size > 1u) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        size -= 2u;
    }
    if (size) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t xnetwork_icmp6_checksum(const uint8_t* src6, const uint8_t* dst6,
                                        const uint8_t* data, size_t size)
{
    /* ICMPv6 校验和覆盖伪首部：src(16) + dst(16) + upper-layer length(32) +
     * 3 字节零 + next header(58)，随后是 ICMPv6 报文。 */
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i < 16u; i += 2u)
        sum += ((uint32_t)src6[i] << 8) | src6[i + 1u];
    for (i = 0; i < 16u; i += 2u)
        sum += ((uint32_t)dst6[i] << 8) | dst6[i + 1u];
    /* 上层长度按网络字节序写入 32 位字段 */
    sum += ((uint32_t)((size >> 8) & 0xffu)) | ((uint32_t)(size & 0xffu) << 8);
    sum += 58u; /* IPPROTO_ICMPV6 */
    while (size > 1u) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        size -= 2u;
    }
    if (size) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

bool XNetwork_icmpEchoSupported(void)
{
    return true;
}

bool XNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
                       uint16_t sequence, const void* payload, size_t payloadSize,
                       int timeoutMilliseconds, uint32_t* elapsedMilliseconds)
{
    struct sockaddr_in destination4;
    struct sockaddr_in6 destination6;
    uint8_t* request = NULL;
    uint8_t response[2048];
    size_t requestSize;
    int socketFd = -1;
    bool datagramSocket = false;
    bool isIpv6 = false;
    int pollResult;
    uint64_t start;
    uint64_t deadline;
    bool matched = false;

    if (!address ||
        (XHostAddress_protocol(address) != XHostAddress_IPv4Protocol &&
         XHostAddress_protocol(address) != XHostAddress_IPv6Protocol) ||
        (payloadSize && !payload) || payloadSize > 65507u || timeoutMilliseconds <= 0)
        return false;
    isIpv6 = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol);
    requestSize = XNETWORK_ICMP_HEADER_SIZE + payloadSize;
    request = (uint8_t*)XMalloc_System(requestSize);
    if (!request) return false;
    memset(request, 0, requestSize);
    request[0] = isIpv6 ? 128u : 8u; /* ICMPv6 / ICMP Echo request */
    request[1] = 0u;
    request[4] = (uint8_t)(identifier >> 8);
    request[5] = (uint8_t)identifier;
    request[6] = (uint8_t)(sequence >> 8);
    request[7] = (uint8_t)sequence;
    if (payloadSize) memcpy(request + XNETWORK_ICMP_HEADER_SIZE, payload, payloadSize);

    if (!isIpv6) {
        uint16_t checksum = xnetwork_icmp_checksum(request, requestSize);
        request[2] = (uint8_t)(checksum >> 8);
        request[3] = (uint8_t)checksum;
    }

    if (isIpv6) {
        memset(&destination6, 0, sizeof(destination6));
        destination6.sin6_family = AF_INET6;
        XHostAddress_toIPv6Address(address, (uint8_t*)&destination6.sin6_addr);
        socketFd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
        if (socketFd >= 0) datagramSocket = true;
        else socketFd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        if (socketFd < 0) goto cleanup;
        /* connect() 让内核选择源地址，用于计算 ICMPv6 伪首部校验和 */
        if (connect(socketFd, (const struct sockaddr*)&destination6, sizeof(destination6)) < 0)
            goto cleanup;
        {
            struct sockaddr_storage local;
            socklen_t localLen = (socklen_t)sizeof(local);
            uint8_t src6[16];
            memset(&local, 0, sizeof(local));
            memset(src6, 0, sizeof(src6));
            if (getsockname(socketFd, (struct sockaddr*)&local, &localLen) == 0 &&
                local.ss_family == AF_INET6) {
                const struct sockaddr_in6* l6 = (const struct sockaddr_in6*)&local;
                memcpy(src6, &l6->sin6_addr, sizeof(src6));
            }
            {
                uint16_t checksum = xnetwork_icmp6_checksum(src6,
                    (const uint8_t*)&destination6.sin6_addr, request, requestSize);
                request[2] = (uint8_t)(checksum >> 8);
                request[3] = (uint8_t)checksum;
            }
        }
        if (sendto(socketFd, request, requestSize, 0,
                   (const struct sockaddr*)&destination6, sizeof(destination6)) < 0)
            goto cleanup;
    } else {
        memset(&destination4, 0, sizeof(destination4));
        destination4.sin_family = AF_INET;
        destination4.sin_addr.s_addr = htonl(XHostAddress_toIPv4Address(address));
        socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (socketFd >= 0) datagramSocket = true;
        else socketFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (socketFd < 0) goto cleanup;
        if (sendto(socketFd, request, requestSize, 0,
                   (const struct sockaddr*)&destination4, sizeof(destination4)) < 0)
            goto cleanup;
    }

    start = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    deadline = start + (uint64_t)timeoutMilliseconds;
    while (!matched) {
        uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        int remaining = now >= deadline ? 0 : (int)(deadline - now);
        struct pollfd descriptor;
        ssize_t received;
        size_t offset = 0;
        uint8_t ihl;
        if (remaining <= 0) break;
        descriptor.fd = socketFd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        pollResult = poll(&descriptor, 1, remaining);
        if (pollResult <= 0) break;
        if (!(descriptor.revents & POLLIN)) continue;
        received = recvfrom(socketFd, response, sizeof(response), 0, NULL, NULL);
        if (received < (ssize_t)XNETWORK_ICMP_HEADER_SIZE) continue;
        /* 原始套接字可能包含 IP 头；数据报 ICMP 套接字通常不包含。 */
        if (isIpv6) {
            if ((response[0] >> 4) == 6u) {
                offset = 40u; /* IPv6 基础头 */
                if ((size_t)received < offset + XNETWORK_ICMP_HEADER_SIZE) continue;
            }
        } else if ((response[0] >> 4) == 4u) {
            ihl = (uint8_t)(response[0] & 0x0fu);
            if (ihl < 5u || (size_t)received < (size_t)ihl * 4u + XNETWORK_ICMP_HEADER_SIZE)
                continue;
            offset = (size_t)ihl * 4u;
        }
        if ((isIpv6 ? response[offset] != 129u : response[offset] != 0u) ||
            response[offset + 1u] != 0u ||
            response[offset + 6u] != request[6] || response[offset + 7u] != request[7] ||
            (!datagramSocket && (response[offset + 4u] != request[4] ||
                                 response[offset + 5u] != request[5])))
            continue;
        matched = true;
    }
    if (matched && elapsedMilliseconds)
        *elapsedMilliseconds = (uint32_t)((uint64_t)XDateTime_currentMSecsSinceEpoch() - start);

cleanup:
    if (socketFd >= 0) close(socketFd);
    if (request) XFree_System(request);
    return matched;
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
    if (buf) {
        strerror_r(errorCode, buf, 256);
        buf[255] = '\0'; /* strerror_r 在消息超长时可能不补 NUL，强制截断终止。 */
    }
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
    XFd serverFd;                            /**< TCP 服务器在 XFd 表中的句柄 */

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

/* Synchronize the public endpoint properties after an outbound connection.
 * Active FTP uses the local address to choose PORT/EPRT and its listener's
 * address family. */
static void syncSocketEndpoints(XNetworkSocketPrivate* priv)
{
    if (!priv || !priv->owner) return;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->socket < 0) return;

    XAbstractSocket* socket = (XAbstractSocket*)priv->owner;
    struct sockaddr_storage address;
    socklen_t addressLength = sizeof(address);
    XHostAddress endpoint;
    uint16_t port = 0;

    XHostAddress_init(&endpoint);
    if (getsockname(p->socket, (struct sockaddr*)&address, &addressLength) == 0) {
        sa2addr(&address, &endpoint, &port);
        XAbstractSocket_setLocalAddress(socket, &endpoint);
        XAbstractSocket_setLocalPort(socket, port);
    }

    addressLength = sizeof(address);
    if (getpeername(p->socket, (struct sockaddr*)&address, &addressLength) == 0) {
        sa2addr(&address, &endpoint, &port);
        XAbstractSocket_setPeerAddress(socket, &endpoint);
        XAbstractSocket_setPeerPort(socket, port);
    }
    XHostAddress_deinit_base(&endpoint);
}

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

static void cancelSqe(XEventContext_IO* context)
{
    struct io_uring_sqe* sqe;

    if (!context) return;
    sqe = getSqe();
    if (!sqe) return;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = (uint64_t)(uintptr_t)&context->base;
    sqe->cancel_flags = 0;
    sqe->user_data = 0;
    submitSqe(1);
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
    p->serverFd = XFD_INVALID;
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

    if (p->serverFd != XFD_INVALID) {
        XFd_free(p->serverFd);
        p->serverFd = XFD_INVALID;
    } else if (priv->owner) {
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
            syncSocketEndpoints(priv);
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

bool XNetwork_socketConnectLocal(XNetworkSocketPrivate* priv, const XString* socketPath,
                                 XNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XNetworkSocketType sockType)
{
    XNetworkSocketPrivatePosix* p;
    const char* path;
    struct sockaddr_un address;
    size_t length;
    int fd;

    (void)timeoutMs;
    if (!priv || !socketPath || streamType != XNetwork_LocalStream_UnixSocket
        || sockType != XNetwork_Tcp) return false;
    path = XString_toUtf8(socketPath);
    if (!path || path[0] == '\0') return false;
    length = strlen(path);
    if (length >= sizeof(address.sun_path)) return false;

    p = P32(priv);
    XNetwork_socketDisconnect(priv);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, length + 1);
    if (connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        close(fd);
        return false;
    }
    if (!setNonBlocking(fd)) {
        close(fd);
        return false;
    }

    p->socket = fd;
    p->connected = true;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
    {
        XFd xfd = XIODevice_fd((XIODevice*)priv->owner);
        if (xfd == XFD_INVALID) {
            xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
            XIODevice_setFd((XIODevice*)priv->owner, xfd);
        }
    }
    startAsyncRead(priv, false);
    return true;
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);

#ifdef __linux__
    if (p->readPending) cancelSqe(&p->readContext);
    if (p->writePending) cancelSqe(&p->writeContext);
    if (p->connectPending) cancelSqe(&p->connectContext);
    /* 取消请求可能在 close 后才完成；迟到的 CQE 不能再投递到上层。 */
    p->readContext.base.eventMask = 0;
    p->writeContext.base.eventMask = 0;
    p->connectContext.base.eventMask = 0;
#endif
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
            size_t toSend = XRingBuffer_read(rb, tempBuf, XNETWORK_WRITE_BUFFER_SIZE);
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
                {
                    char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
                    size_t toSend = XRingBuffer_read(rb, tempBuf,
                                                     XNETWORK_WRITE_BUFFER_SIZE);
                    if (toSend > 0) {
                        startAsyncWrite(priv, tempBuf, (int64_t)toSend,
                                        destAddr, destPort, sockType == XNetwork_Udp);
                    }
                }
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
            syncSocketEndpoints(priv);
            /* first read started by XNetwork_socketContinueRead in event handler */
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

bool XNetwork_serverSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd)
{
    XNetworkSocketPrivatePosix* p;
    XFd xfd;

    if (!priv || fd < 0) return false;
    p = P32(priv);
    if (p->socket >= 0 || p->serverFd != XFD_INVALID) return false;

    p->socket = (int)fd;
    if (!setNonBlocking(p->socket)) {
        close(p->socket);
        p->socket = -1;
        return false;
    }

    xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
    if (xfd == XFD_INVALID) {
        close(p->socket);
        p->socket = -1;
        return false;
    }

    p->serverFd = xfd;
    p->isServer = true;
    p->connected = true;
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

bool XNetwork_socketWritePending(const XNetworkSocketPrivate* priv)
{
    return priv ? P32(priv)->writePending : false;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv) return;
    XNetworkSocketPrivatePosix* p = P32(priv);
    p->readPending = false;
    if (p->autoRead && p->connected) startAsyncRead(priv, isUdp);
}

void XNetwork_socketContinueWrite(XNetworkSocketPrivate* priv, XRingBuffer* ringBuffer, bool isUdp)
{
    if (!priv || !ringBuffer) return;
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (p->writePending || p->socket < 0) return;
    struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
    size_t pending = XRingBuffer_available(rb);
    if (pending == 0) return;
    size_t chunk = pending;
    if (chunk > XNETWORK_WRITE_BUFFER_SIZE) chunk = XNETWORK_WRITE_BUFFER_SIZE;
    char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
    size_t got = XRingBuffer_read(rb, tempBuf, chunk);
    if (got > 0) {
        startAsyncWrite(priv, tempBuf, (int64_t)got, NULL, 0, isUdp);
    }
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
    memset(&p->acceptContext, 0, sizeof(p->acceptContext));
    p->acceptContext.base.type = XEventContextType_Type_Socket;
    p->acceptContext.base.fd = p->serverFd;
    p->acceptContext.base.eventMask = XSocketAct_Accept;
    p->acceptContext.socket = XSocketDescriptor_fromIntptr(p->socket);

    struct io_uring_sqe* sqe = getSqe();
    if (!sqe) return false;

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
    int af = (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    int fd = socket(af, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setNonBlocking(fd);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    if (af == AF_INET6) {
        int ipv6only = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only));
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

    /* XTcpServer 不继承 XIODevice，服务器必须独立保存 XFd。 */
    {
        XFd xfd = XFd_alloc(XFD_TYPE_SOCKET, priv, priv->owner);
        if (xfd == XFD_INVALID) {
            close(fd);
            return -1;
        }
        P32(priv)->serverFd = xfd;
    }

    /* 设置服务器套接字 */
    P32(priv)->socket = fd;
    P32(priv)->isServer = true;
    P32(priv)->connected = true;

    return (XServerHandle)(intptr_t)fd;
}

void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server)
{
    XNetworkSocketPrivatePosix* p = P32(priv);
    int fd = (int)server;
    if (fd < 0) return;
    if (p && p->socket == fd) {
        close(p->socket);
        p->socket = -1;
        p->connected = false;
    } else {
        close(fd);
    }
}

uint16_t XNetwork_serverPort(XServerHandle server)
{
    int fd = (int)server;
    if (fd < 0) return 0;
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);
    if (getsockname(fd, (struct sockaddr*)&address, &length) != 0) return 0;
    if (address.ss_family == AF_INET6)
        return ntohs(((struct sockaddr_in6*)&address)->sin6_port);
    return ntohs(((struct sockaddr_in*)&address)->sin_port);
}

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv, XHostAddress* clientAddr, uint16_t* clientPort)
{
    XNetworkSocketPrivatePosix* p = P32(priv);
    if (!p || !p->acceptPending) return -1;

    int acceptedFd = (int)p->acceptContext.base.result;
    p->acceptContext.base.result = -1;
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

/* =========================================================================
 * 网络接口枚举 - 内部迭代器状态
 * ========================================================================= */
static struct ifaddrs* g_ifaddrsList = NULL;
static struct ifaddrs* g_ifaddrsCurrent = NULL;
static char g_ifaddrsCurrentName[IFNAMSIZ] = "";
#define XNETWORK_MAX_ENUM_INTERFACES 64u
static char g_ifaddrsSeen[XNETWORK_MAX_ENUM_INTERFACES][IFNAMSIZ];
static size_t g_ifaddrsSeenCount = 0;

static bool xnetwork_ifaddr_seen(const char* name)
{
    size_t i;
    if (!name) return true;
    for (i = 0; i < g_ifaddrsSeenCount; ++i)
        if (strcmp(g_ifaddrsSeen[i], name) == 0) return true;
    return false;
}

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void)
{
    if (g_ifaddrsList) {
        freeifaddrs(g_ifaddrsList);
        g_ifaddrsList = NULL;
        g_ifaddrsCurrent = NULL;
        g_ifaddrsCurrentName[0] = '\0';
        g_ifaddrsSeenCount = 0;
    }

    if (getifaddrs(&g_ifaddrsList) != 0) return NULL;
    g_ifaddrsCurrent = g_ifaddrsList;
    g_ifaddrsCurrentName[0] = '\0';
    g_ifaddrsSeenCount = 0;
    return (XNetworkInterfaceIterator)1; /* 非空标记 */
}

struct XNetworkInterface* XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter)
{
    (void)iter;
    if (!g_ifaddrsList) return NULL;

    /* 跳过重复的接口名称，每个接口只创建一个 XNetworkInterface */
    while (g_ifaddrsCurrent) {
        if (!xnetwork_ifaddr_seen(g_ifaddrsCurrent->ifa_name)) {
            strncpy(g_ifaddrsCurrentName, g_ifaddrsCurrent->ifa_name, IFNAMSIZ - 1);
            g_ifaddrsCurrentName[IFNAMSIZ - 1] = '\0';
            if (g_ifaddrsSeenCount < XNETWORK_MAX_ENUM_INTERFACES) {
                strncpy(g_ifaddrsSeen[g_ifaddrsSeenCount], g_ifaddrsCurrentName,
                        IFNAMSIZ - 1);
                g_ifaddrsSeen[g_ifaddrsSeenCount][IFNAMSIZ - 1] = '\0';
                ++g_ifaddrsSeenCount;
            }
            break;
        }
        g_ifaddrsCurrent = g_ifaddrsCurrent->ifa_next;
    }
    if (!g_ifaddrsCurrent) return NULL;

    /* 创建 XNetworkInterface 对象 */
    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;

    /* 名称 */
    if (iface->name)
        XString_assign_utf8(iface->name, g_ifaddrsCurrent->ifa_name);
    else
        iface->name = XString_create_utf8(g_ifaddrsCurrent->ifa_name);

    /* 接口索引 */
    iface->index = if_nametoindex(g_ifaddrsCurrent->ifa_name);

    /* 标志 */
    iface->flags = 0;
    if (g_ifaddrsCurrent->ifa_flags & IFF_UP)
        iface->flags |= XNetworkInterface_IsUp;
    if (g_ifaddrsCurrent->ifa_flags & IFF_RUNNING)
        iface->flags |= XNetworkInterface_IsRunning;
    if (g_ifaddrsCurrent->ifa_flags & IFF_LOOPBACK)
        iface->flags |= XNetworkInterface_IsLoopBack;
    if (g_ifaddrsCurrent->ifa_flags & IFF_BROADCAST)
        iface->flags |= XNetworkInterface_CanBroadcast;
    if (g_ifaddrsCurrent->ifa_flags & IFF_MULTICAST)
        iface->flags |= XNetworkInterface_CanMulticast;

    /* 类型 */
    if (g_ifaddrsCurrent->ifa_flags & IFF_LOOPBACK)
        iface->type = XNetworkInterface_Loopback;
    else if (g_ifaddrsCurrent->ifa_flags & IFF_POINTOPOINT)
        iface->type = XNetworkInterface_Ppp;
    else
        iface->type = XNetworkInterface_Ethernet;

    /* 硬件地址 (MAC) - 通过 ioctl SIOCGIFHWADDR 获取 */
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, g_ifaddrsCurrent->ifa_name, IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
                if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
                    char macStr[64];
                    snprintf(macStr, sizeof(macStr),
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    if (iface->hardwareAddress)
                        XString_assign_utf8(iface->hardwareAddress, macStr);
                    else
                        iface->hardwareAddress = XString_create_utf8(macStr);
                }
            }
            close(fd);
        }
    }

    /* MTU - 通过 ioctl SIOCGIFMTU 获取 */
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, g_ifaddrsCurrent->ifa_name, IFNAMSIZ - 1);
            if (ioctl(fd, SIOCGIFMTU, &ifr) == 0) {
                iface->mtu = ifr.ifr_mtu;
            }
            close(fd);
        }
    }

    /* 收集该接口的所有地址条目 */
    struct ifaddrs* cur = g_ifaddrsList;
    while (cur) {
        if (strcmp(cur->ifa_name, g_ifaddrsCurrentName) != 0) {
            cur = cur->ifa_next;
            continue;
        }
        if (cur->ifa_addr && (cur->ifa_addr->sa_family == AF_INET ||
                              cur->ifa_addr->sa_family == AF_INET6)) {
            XNetworkAddressEntry entry;
            XNetworkAddressEntry_init(&entry);

            XHostAddress addr;
            XHostAddress_init(&addr);

            if (cur->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* sin = (struct sockaddr_in*)cur->ifa_addr;
                XHostAddress_setAddressIPv4(&addr, ntohl(sin->sin_addr.s_addr));
            } else {
                struct sockaddr_in6* sin6 = (struct sockaddr_in6*)cur->ifa_addr;
                XHostAddress_setAddressIPv6(&addr, (const uint8_t*)&sin6->sin6_addr);
            }
            XNetworkAddressEntry_setIp(&entry, &addr);

            /* 子网掩码 */
            if (cur->ifa_netmask) {
                XHostAddress mask;
                XHostAddress_init(&mask);
                if (cur->ifa_netmask->sa_family == AF_INET) {
                    struct sockaddr_in* sin = (struct sockaddr_in*)cur->ifa_netmask;
                    XHostAddress_setAddressIPv4(&mask, ntohl(sin->sin_addr.s_addr));
                } else {
                    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)cur->ifa_netmask;
                    XHostAddress_setAddressIPv6(&mask, (const uint8_t*)&sin6->sin6_addr);
                }
                XNetworkAddressEntry_setNetmask(&entry, &mask);
                XHostAddress_deinit_base(&mask);
            }

            /* 广播地址 */
            if (cur->ifa_broadaddr && (cur->ifa_flags & IFF_BROADCAST)) {
                XHostAddress bcast;
                XHostAddress_init(&bcast);
                if (cur->ifa_broadaddr->sa_family == AF_INET) {
                    struct sockaddr_in* sin = (struct sockaddr_in*)cur->ifa_broadaddr;
                    XHostAddress_setAddressIPv4(&bcast, ntohl(sin->sin_addr.s_addr));
                }
                XNetworkAddressEntry_setBroadcast(&entry, &bcast);
                XHostAddress_deinit_base(&bcast);
            }

            XVector_push_back_move_1_base(iface->addressEntries, &entry);
            XNetworkAddressEntry_deinit_base(&entry);
            XHostAddress_deinit_base(&addr);
        }
        cur = cur->ifa_next;
    }

    /* 当前指针仍指向本接口的第一条记录；从下一条开始寻找尚未输出的接口。 */
    g_ifaddrsCurrent = g_ifaddrsCurrent ? g_ifaddrsCurrent->ifa_next : NULL;
    while (g_ifaddrsCurrent && xnetwork_ifaddr_seen(g_ifaddrsCurrent->ifa_name))
        g_ifaddrsCurrent = g_ifaddrsCurrent->ifa_next;

    iface->isValid = true;
    return iface;
}

void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter)
{
    (void)iter;
    if (g_ifaddrsList) {
        freeifaddrs(g_ifaddrsList);
        g_ifaddrsList = NULL;
    g_ifaddrsCurrent = NULL;
        g_ifaddrsSeenCount = 0;
    }
    g_ifaddrsCurrentName[0] = '\0';
    g_ifaddrsSeenCount = 0;
}

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
    if (sock == (XSocketHandle)-1 || !arg) return -1;
    int fd = (int)sock;

    switch (op) {
    case XMC_SetIf: {
        uint32_t ifIndex = *(uint32_t*)arg;
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        if (if_indextoname(ifIndex, ifr.ifr_name) == NULL) return -1;
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifr, sizeof(ifr)) != 0)
            return -1;
        return 0;
    }
    case XMC_GetIf: {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        if (getsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifr, &(socklen_t){sizeof(ifr)}) != 0)
            return -1;
        unsigned int idx = if_nametoindex(ifr.ifr_name);
        if (idx == 0) return -1;
        *(uint32_t*)arg = idx;
        return 0;
    }
    case XMC_SetTtl: {
        int ttl = *(int*)arg;
        /* IPv4 */
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0)
            return -1;
        /* IPv6 */
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl));
        return 0;
    }
    case XMC_GetTtl: {
        int ttl = 0;
        socklen_t len = sizeof(ttl);
        if (getsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, &len) != 0)
            return -1;
        *(int*)arg = ttl;
        return 0;
    }
    case XMC_SetLoop: {
        int loop = *(bool*)arg ? 1 : 0;
        /* IPv4 */
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0)
            return -1;
        /* IPv6 */
        setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
        return 0;
    }
    case XMC_GetLoop: {
        int loop = 0;
        socklen_t len = sizeof(loop);
        if (getsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, &len) != 0)
            return -1;
        *(bool*)arg = (loop != 0);
        return 0;
    }
    default:
        return -1;
    }
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
    if (!outProxy) return false;
    XNetworkProxy_setType(outProxy, XNetworkProxy_NoProxy);

    /* 读取环境变量获取系统代理配置
     * 优先级: HTTPS_PROXY > https_proxy > HTTP_PROXY > http_proxy */
    const char* proxyStr = NULL;
    const char* noProxyStr = getenv("no_proxy");
    if (!noProxyStr) noProxyStr = getenv("NO_PROXY");

    /* 检查是否在 no_proxy 列表中 */
    if (noProxyStr && queryUrl) {
        /* 此处简化处理：不对 URL 进行精确匹配，依赖于调用侧的代理绕过逻辑 */
    }

    /* 优先尝试 HTTPS 代理 */
    proxyStr = getenv("HTTPS_PROXY");
    if (!proxyStr) proxyStr = getenv("https_proxy");
    if (!proxyStr) proxyStr = getenv("HTTP_PROXY");
    if (!proxyStr) proxyStr = getenv("http_proxy");

    if (proxyStr && proxyStr[0]) {
        char proxyHost[256] = "";
        uint16_t proxyPort = 0;
        const char* p = proxyStr;

        /* 跳过协议前缀 */
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else if (strncmp(p, "https://", 8) == 0) p += 8;
        else if (strncmp(p, "socks5://", 9) == 0) {
            p += 9;
            XNetworkProxy_setType(outProxy, XNetworkProxy_Socks5Proxy);
        } else if (strncmp(p, "socks4://", 9) == 0) {
            p += 9;
            XNetworkProxy_setType(outProxy, XNetworkProxy_Socks5Proxy);
        }

        /* 解析 host:port */
        const char* colon = strchr(p, ':');
        if (colon) {
            size_t hostLen = (size_t)(colon - p);
            if (hostLen < sizeof(proxyHost)) {
                strncpy(proxyHost, p, hostLen);
                proxyHost[hostLen] = '\0';
            }
            proxyPort = (uint16_t)atoi(colon + 1);
        } else {
            strncpy(proxyHost, p, sizeof(proxyHost) - 1);
            proxyHost[sizeof(proxyHost) - 1] = '\0';
            proxyPort = 80;
        }

        if (proxyHost[0]) {
            XString* host = XString_create_utf8(proxyHost);
            XNetworkProxy_setHostName(outProxy, host);
            XNetworkProxy_setPort(outProxy, proxyPort);
            XString_delete_base(host);
            return true;
        }
    }

    return false;
}

/* GSSAPI 上下文结构 */
typedef struct {
    const char* targetName;
    int initialized;
} PosixGssapiContext;

int XNetwork_gssapiAuth(const XString* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context)
{
    if (!outputToken) return -1;

#ifdef HAVE_GSSAPI
    /* GSSAPI 实现 */
    PosixGssapiContext* ctx = (PosixGssapiContext*)*context;

    if (!ctx) {
        ctx = (PosixGssapiContext*)XCalloc_System(1, sizeof(PosixGssapiContext));
        if (!ctx) return -1;
        if (serviceName) {
            const char* svc = XString_toUtf8(serviceName);
            if (svc) ctx->targetName = strdup(svc);
        }
        *context = ctx;
    }

    /* GSSAPI 初始化 */
    gss_ctx_id_t gssCtx = GSS_C_NO_CONTEXT;
    gss_cred_id_t gssCred = GSS_C_NO_CREDENTIAL;
    gss_buffer_desc inputBuf = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc outputBuf = GSS_C_EMPTY_BUFFER;
    OM_uint32 major, minor, retFlags;

    /* 获取默认凭据 */
    major = gss_acquire_cred(&minor, GSS_C_NO_NAME, GSS_C_INDEFINITE,
                             GSS_C_NO_OID_SET, GSS_C_INITIATE,
                             &gssCred, NULL, NULL);
    if (GSS_ERROR(major)) {
        if (ctx->targetName) free((void*)ctx->targetName);
        XFree_System(ctx);
        *context = NULL;
        return -1;
    }

    /* 准备输入令牌 */
    if (inputToken && XByteArray_size_base(inputToken) > 0) {
        inputBuf.value = XByteArray_data(inputToken);
        inputBuf.length = (size_t)XByteArray_size_base(inputToken);
    }

    /* 准备服务名称 */
    gss_buffer_desc nameBuf;
    OM_uint32 nameMinor;
    gss_name_t targetName = GSS_C_NO_NAME;
    if (ctx->targetName) {
        nameBuf.value = (void*)ctx->targetName;
        nameBuf.length = strlen(ctx->targetName);
        major = gss_import_name(&nameMinor, &nameBuf,
                                (gss_OID)GSS_C_NULL_OID, &targetName);
        if (GSS_ERROR(major)) {
            gss_release_cred(&minor, &gssCred);
            if (ctx->targetName) free((void*)ctx->targetName);
            XFree_System(ctx);
            *context = NULL;
            return -1;
        }
    }

    /* 执行 GSSAPI 初始化安全上下文 */
    major = gss_init_sec_context(&minor, gssCred, &gssCtx,
                                  targetName, GSS_C_NO_OID,
                                  GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG,
                                  GSS_C_INDEFINITE, GSS_C_NO_CHANNEL_BINDINGS,
                                  &inputBuf, NULL, &outputBuf, &retFlags, NULL);

    /* 释放凭据 */
    gss_release_cred(&minor, &gssCred);
    if (targetName != GSS_C_NO_NAME)
        gss_release_name(&nameMinor, &targetName);

    if (GSS_ERROR(major)) {
        if (gssCtx != GSS_C_NO_CONTEXT)
            gss_delete_sec_context(&minor, &gssCtx, GSS_C_NO_BUFFER);
        if (ctx->targetName) free((void*)ctx->targetName);
        XFree_System(ctx);
        *context = NULL;
        return -1;
    }

    /* 复制输出令牌 */
    if (outputBuf.length > 0) {
        XByteArray_resize_base(outputToken, (int64_t)outputBuf.length);
        memcpy(XByteArray_data(outputToken), outputBuf.value, outputBuf.length);
        gss_release_buffer(&minor, &outputBuf);
    }

    if (major == GSS_S_COMPLETE) {
        /* 认证完成 */
        if (ctx->targetName) free((void*)ctx->targetName);
        XFree_System(ctx);
        *context = NULL;
        return 0;
    }

    /* 需要继续 (GSS_S_CONTINUE_NEEDED) */
    return 1;

#else
    /* 无 GSSAPI 库支持，返回失败 */
    (void)serviceName; (void)inputToken; (void)context;
    return -1;
#endif
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
    /* XHostAddress 的 IPv4 字段统一使用主机字节序，不能直接保存
     * sockaddr_in 中的网络字节序地址，否则 127.0.0.1 会变成 1.0.0.127。 */
    XHostAddress_setAddressIPv4(&addr, ntohl(sin->sin_addr.s_addr));
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
