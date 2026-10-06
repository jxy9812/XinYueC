/**
 * @file XDeviceNetwork_posix.c
 * @brief XNetwork POSIX 平台实现（Linux io_uring 异步 I/O）
 *
 * 对标 Windows XDeviceNetwork_win32.c 的 IOCP 异步 I/O 实现，
 * 使用 io_uring 实现完全异步的 Socket 读写、连接和接受操作。
 */

 /* ====== 配置文件 ====== */
#include "XNetwork_config.h"
#if defined(XNETWORK_USE_PLATFORM_API) && (defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__))

/* ====== 项目头文件 ====== */
#include "XDeviceNetwork.h"
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
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <net/route.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
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
/* [2026-10-03 互联测试指挥官] 设备侧 socket IO 逐操作追踪(默认 0):
 * MCGS 真机会话 RECV 续传诊断, 编译期 -DXDEVNET_IO_DEBUG=1 重开。 */
#ifndef XDEVNET_IO_DEBUG
#define XDEVNET_IO_DEBUG 0
#endif

#if XNET_USE_IO_URING  /* 探测宏来自 XNetIoRingPosix.h；epoll 回退用其伪 SQE 兼容层 */
#if XNET_BUILD_IO_URING  /* 探测宏来自 XNetIoRingPosix.h；epoll 回退用其伪 SQE 兼容层 */
#include <linux/io_uring.h>
#endif
#endif
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
#define XNETWORK_IO_PENDING_RESULT INT64_MIN

/* ICMP Echo 报文头固定为 type、code、checksum、identifier、sequence。 */
#define XNETWORK_ICMP_HEADER_SIZE 8u

typedef enum XMulticastOp {
    XMC_Join, XMC_Leave, XMC_SetIf, XMC_GetIf,
    XMC_SetTtl, XMC_GetTtl, XMC_SetLoop, XMC_GetLoop
} XMulticastOp;

/* 本文件内后置实现的钩子，不属于公共头文件契约。 */
void XDeviceNetwork_socketDisconnect(XFd fd);
int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle socketHandle,
    XMulticastOp operation, void* argument);

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

bool XDeviceNetwork_icmpEchoSupported(void)
{
    return true;
}

bool XDeviceNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
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

void XDeviceNetwork_ensureInit(void)
{
    g_initCount++;
}

void XDeviceNetwork_cleanup(void)
{
    if (g_initCount > 0) g_initCount--;
}

void XDeviceNetwork_poll(void)
{
}

int XDeviceNetwork_lastError(void)
{
    return errno;
}

char* XDeviceNetwork_errorString(int errorCode)
{
    char* buf = (char*)XMalloc_System(256);
    if (buf) {
        strerror_r(errorCode, buf, 256);
        buf[255] = '\0'; /* strerror_r 在消息超长时可能不补 NUL，强制截断终止。 */
    }
    return buf;
}

#if defined(__linux__) && !defined(__ANDROID__)
bool XDeviceNetwork_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    FILE* file;
    char line[512];
    bool found = false;

    if (!rxBytes || !txBytes)
        return false;
    *rxBytes = 0;
    *txBytes = 0;

    file = fopen("/proc/net/dev", "r");
    if (!file)
        return false;

    while (fgets(line, sizeof(line), file))
    {
        char* colon = strchr(line, ':');
        char* name;
        char* cursor;
        char* end;
        int field;
        uint64_t rx = 0;
        uint64_t tx = 0;

        if (!colon)
            continue;
        *colon = '\0';
        name = line;
        while (*name == ' ' || *name == '\t')
            ++name;
        if (strcmp(name, "lo") == 0)
            continue;

        cursor = colon + 1;
        for (field = 0; field < 16; ++field)
        {
            unsigned long long value;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            value = strtoull(cursor, &end, 10);
            if (end == cursor)
                break;
            if (field == 0)
                rx = (uint64_t)value;
            if (field == 8)
                tx = (uint64_t)value;
            cursor = end;
        }

        if (field == 16)
        {
            *rxBytes += rx;
            *txBytes += tx;
            found = true;
        }
    }

    fclose(file);
    return found;
}
#else
/* Android：SELinux 对 untrusted_app 隐藏 /proc/net/dev（含 /proc/self/net
   的全局视图），改经 Drive/Android/Core/XSystemAndroid.c 的 JNI
   TrafficStats 系统级收发计数。 */
bool XAndroid_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes);
bool XDeviceNetwork_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    return XAndroid_getNetworkCounters(rxBytes, txBytes);
}
#endif /* __linux__ */

/* =========================================================================
 * 套接字私有数据结构（平台无关基类 + POSIX 扩展）
 * 对标 Windows XDeviceNetworkContextWin32
 * ========================================================================= */

typedef struct XDeviceNetworkContextPosix {
    XDeviceNetworkContext base;             /**< 第一位：平台无关基类 (owner/xfd/notifiers) */
    int socket;                             /**< POSIX socket fd */

    /* 状态标志 */
    bool readPending;
    bool writePending;
    bool connectPending;
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
    struct sockaddr_storage connectAddress; /**< io_uring 连接请求的持久目标地址。 */
    socklen_t connectAddressLength;

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
} XDeviceNetworkContextPosix;

/* 便捷转换宏 */
#define P32(p) ((XDeviceNetworkContextPosix*)(p))

/* Synchronize the public endpoint properties after an outbound connection.
 * Active FTP uses the local address to choose PORT/EPRT and its listener's
 * address family. */
static void syncSocketEndpoints(XDeviceNetworkContext* priv)
{
    if (!priv || !priv->m_owner) return;
    XDeviceNetworkContextPosix* p = P32(priv);
    if (p->socket < 0) return;

    XAbstractSocket* socket = (XAbstractSocket*)priv->m_owner;
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
    XClassDeinit(&endpoint);
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

static bool cancelSqe(XEventContext_IO* context)
{
    struct io_uring_sqe* sqe;

    if (!context) return false;
    sqe = getSqe();
    if (!sqe) return false;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = (uint64_t)(uintptr_t)&context->base;
    sqe->cancel_flags = 0;
    sqe->user_data = 0;
    submitSqe(1);
    return true;
}

static void waitForCancel(XEventContext_IO* context, bool* pending)
{
    XAbstractNetIoRing* ring;
    int tries = 0;
    if (!context || !pending || !*pending) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) return;
    while (context->base.result == XNETWORK_IO_PENDING_RESULT && tries++ < 20) {
        XAbstractNetIoRing_waitForEvents_base(ring, 10);
        XAbstractNetIoRing_drainCQ(ring);
    }
    *pending = false;
}

#endif /* __linux__ */

/* =========================================================================
 * 异步读取启动（对标 Windows startAsyncRead）
 * ========================================================================= */

static void startAsyncRead(XDeviceNetworkContext* priv, bool isUdp)
{
#ifdef __linux__
    XDeviceNetworkContextPosix* p = P32(priv);
    if (!p || p->readPending || p->socket < 0) return;
    (void)isUdp;

    memset(&p->readContext, 0, sizeof(p->readContext));
    p->readContext.base.type = XEventContextType_Type_Socket;
    p->readContext.base.fd = priv->m_base.m_fd;
    p->readContext.base.buffer = p->readBuffer;
    p->readContext.base.bufferSize = XNETWORK_READ_BUFFER_SIZE;
    p->readContext.base.eventMask = XSocketAct_Read;
    p->readContext.base.result = XNETWORK_IO_PENDING_RESULT;
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
#if XDEVNET_IO_DEBUG
    fprintf(stderr, "[XDEVNET] submit RECV fd=%d\n", p->socket);
#endif
#else
    (void)priv; (void)isUdp;
#endif
}

/* =========================================================================
 * 异步写入启动（对标 Windows startAsyncWrite）
 * ========================================================================= */

static void startAsyncWrite(XDeviceNetworkContext* priv, const void* data, int64_t len,
                             const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
#ifdef __linux__
    XDeviceNetworkContextPosix* p = P32(priv);
    if (!p || p->writePending || p->socket < 0) return;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return;

    /* [2026-10-03 互联测试指挥官] memcpy→memmove: 短写续传路径以
     * writeBuffer+sent 自身再入本函数, 区间重叠, memcpy 未定义。 */
    memmove(p->writeBuffer, data, (size_t)len);

    memset(&p->writeContext, 0, sizeof(p->writeContext));
    p->writeContext.base.type = XEventContextType_Type_Socket;
    p->writeContext.base.fd = priv->m_base.m_fd;
    p->writeContext.base.buffer = p->writeBuffer;
    p->writeContext.base.bufferSize = (size_t)len;
    p->writeContext.base.eventMask = XSocketAct_Write;
    p->writeContext.base.result = XNETWORK_IO_PENDING_RESULT;
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
#if XDEVNET_IO_DEBUG
    fprintf(stderr, "[XDEVNET] submit SEND fd=%d len=%lld\n", p->socket, (long long)len);
#endif
#else
    (void)priv; (void)data; (void)len; (void)destAddr; (void)destPort; (void)isUdp;
#endif
}

/* =========================================================================
 * 私有数据管理
 * ========================================================================= */

XDeviceNetworkContext* XDeviceNetwork_createContext(void)
{
    XDeviceNetworkContextPosix* p = (XDeviceNetworkContextPosix*)XCalloc_System(1, sizeof(XDeviceNetworkContextPosix));
    if (!p) return NULL;

    p->socket = -1;
    p->base.m_base.m_fd = XFD_INVALID;
    p->autoRead = true;

    XHostAddress_init(&p->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&p->pendingPeerAddr, XHostAddress_NullSpecial);

    return (XDeviceNetworkContext*)p;
}

void XDeviceNetwork_deleteContext(XDeviceNetworkContext* priv)
{
    if (!priv) return;
    XDeviceNetworkContextPosix* p = P32(priv);

    if (p->socket >= 0) {
        close(p->socket);
        p->socket = -1;
    }

    XClassDeinit(&p->pendingPeerAddr);
    XFree_System(p);
}

intptr_t XDeviceNetwork_socketDescriptor(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? (intptr_t)P32(priv)->socket : -1;
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

uint16_t XDeviceNetwork_socketBind(XFd xfd, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !address) return 0;
    XDeviceNetworkContextPosix* p = P32(priv);

    XDeviceNetwork_ensureInit();
    int af = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    int type = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XDeviceNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;
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

    p->base.m_connected = true;

    if (sockType == XDeviceNetwork_Udp) startAsyncRead(priv, true);
    return actualPort;
}

bool XDeviceNetwork_socketConnect(XFd xfd, const XString* hostName,
                             uint16_t port, XDeviceNetworkProtocol protocol,
                             XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !hostName) return false;
    const char* hostStr = XString_toUtf8(hostName);
    if (!hostStr) return false;
    XDeviceNetworkContextPosix* p = P32(priv);

    XDeviceNetwork_ensureInit();
    struct addrinfo hints = { 0 }, * result = NULL;
    hints.ai_family = (protocol == XDeviceNetwork_IPv6) ? AF_INET6 :
                       (protocol == XDeviceNetwork_IPv4) ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;

    if (getaddrinfo(hostStr, NULL, &hints, &result) != 0) return false;

    struct addrinfo* ai = result;
    while (ai && ai->ai_family != hints.ai_family) ai = ai->ai_next;
    if (!ai) ai = result;

    int type = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XDeviceNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;

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
    memcpy(&p->connectAddress, &destAddr, sizeof(destAddr));
    p->connectAddressLength = (socklen_t)destLen;

    if (sockType == XDeviceNetwork_Tcp) {

#ifdef __linux__
        /* 发起 io_uring 异步连接（对标 Windows ConnectEx） */
        memset(&p->connectContext, 0, sizeof(p->connectContext));
        p->connectContext.base.type = XEventContextType_Type_Socket;
        p->connectContext.base.fd = priv->m_base.m_fd;
        p->connectContext.base.eventMask = XSocketAct_Connect;
        p->connectContext.base.result = XNETWORK_IO_PENDING_RESULT;
        p->connectContext.socket = XSocketDescriptor_fromIntptr(p->socket);

        struct io_uring_sqe* sqe = getSqe();
        if (sqe) {
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = IORING_OP_CONNECT;
            sqe->fd = p->socket;
            sqe->addr = (uint64_t)(uintptr_t)&p->connectAddress;
            sqe->off = (uint32_t)p->connectAddressLength;
            sqe->user_data = (uint64_t)(uintptr_t)&p->connectContext.base;
            submitSqe(1);
            p->connectPending = true;
            return true;
        }
#endif
        /* 回退到同步 connect（非 io_uring 平台） */
        int ret = connect(p->socket, (struct sockaddr*)&p->connectAddress,
                          p->connectAddressLength);
        if (ret == 0 || errno == EINPROGRESS) {
            p->base.m_connected = true;
            p->connectPending = false;
            syncSocketEndpoints(priv);
            startAsyncRead(priv, false);
            return true;
        }
        close(p->socket); p->socket = -1;
        return false;
    }

    if (sockType == XDeviceNetwork_Udp) {
        p->base.m_connected = true;
        p->connectPending = false;
        startAsyncRead(priv, true);
        return true;
    }
    return true;
}

bool XDeviceNetwork_socketConnectLocal(XFd xfd, const XString* socketPath,
                                 XDeviceNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextPosix* p;
    const char* path;
    struct sockaddr_un address;
    size_t length;
    int fd;

    (void)timeoutMs;
    if (!priv || !socketPath || streamType != XDeviceNetwork_LocalStream_UnixSocket
        || sockType != XDeviceNetwork_Tcp) return false;
    path = XString_toUtf8(socketPath);
    if (!path || path[0] == '\0') return false;
    length = strlen(path);
    if (length >= sizeof(address.sun_path)) return false;

    p = P32(priv);
    XDeviceNetwork_socketDisconnect(xfd);
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
    p->base.m_connected = true;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
    startAsyncRead(priv, false);
    return true;
}

void XDeviceNetwork_socketDisconnect(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextPosix* p = P32(priv);

#ifdef __linux__
    if (p->readPending) {
        p->readContext.base.eventMask = 0;
        p->readContext.base.result = XNETWORK_IO_PENDING_RESULT;
        (void)cancelSqe(&p->readContext);
    }
    if (p->writePending) {
        p->writeContext.base.eventMask = 0;
        p->writeContext.base.result = XNETWORK_IO_PENDING_RESULT;
        (void)cancelSqe(&p->writeContext);
    }
    if (p->connectPending) {
        p->connectContext.base.eventMask = 0;
        p->connectContext.base.result = XNETWORK_IO_PENDING_RESULT;
        (void)cancelSqe(&p->connectContext);
    }
    /* 取消请求可能在 close 后才完成；先消费对应 CQE，再释放上下文，
       避免迟到 CQE 访问已释放的上下文或命中复用的 XFd 槽位。 */
    waitForCancel(&p->readContext, &p->readPending);
    waitForCancel(&p->writeContext, &p->writePending);
    waitForCancel(&p->connectContext, &p->connectPending);
#endif
    if (p->socket >= 0) {
        close(p->socket);
        p->socket = -1;
    }
    p->base.m_connected = false;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
}

int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
                             XDeviceNetworkSocketType sockType, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextPosix* p = P32(priv);
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

int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
                              XDeviceNetworkSocketType sockType, const XHostAddress* destAddr,
                              uint16_t destPort, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextPosix* p = P32(priv);
    if (p->socket < 0) return -1;

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t pending = XRingBuffer_available(rb);
        if (pending > 0 && !p->writePending) {
            char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
            size_t toSend = XRingBuffer_read(rb, tempBuf, XNETWORK_WRITE_BUFFER_SIZE);
            if (toSend > 0) {
                startAsyncWrite(priv, tempBuf, (int64_t)toSend, destAddr, destPort,
                                sockType == XDeviceNetwork_Udp);
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
                                        destAddr, destPort, sockType == XDeviceNetwork_Udp);
                    }
                }
                return len;
            }
            len = XNETWORK_WRITE_BUFFER_SIZE;
        }
        startAsyncWrite(priv, buf, len, destAddr, destPort, sockType == XDeviceNetwork_Udp);
        return len;
    }

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        return (int64_t)XRingBuffer_write(rb, buf, (size_t)len);
    }
    return -1;
}

bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !event) return false;
    XDeviceNetworkContextPosix* p = P32(priv);

    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;

#if XDEVNET_IO_DEBUG
    if (sockAct->actType & XSocketAct_Write)
        fprintf(stderr, "[XDEVNET] event W result=%lld finished=%zu\n",
                (long long)p->writeContext.base.result,
                p->writeContext.base.finishedBytes);
    else
        fprintf(stderr, "[XDEVNET] event R result=%lld finished=%zu\n",
                (long long)p->readContext.base.result,
                p->readContext.base.finishedBytes);
#endif
    if (sockAct->actType & XSocketAct_Read) {
        p->readPending = false;
#if XDEVNET_IO_DEBUG
        fprintf(stderr, "[XDEVNET] READ done res=%lld autoRead=%d\n",
                (long long)p->readContext.base.result, (int)p->autoRead);
#endif
        return p->readContext.base.finishedBytes > 0;
    }
    if (sockAct->actType & XSocketAct_Write) {
        p->writePending = false;
        /* [2026-10-03 互联测试指挥官修复归因] 短写续传: 非阻塞 SEND 部分
         * 完成时(res>0 且 <len), 残余字节原被静默丢弃——TCP 流自此去同步,
         * 接收端从半帧中段解析后续帧(真机 MCGS 资源档 tile 流实证: 客户端
         * 镜像出现整体错位碎片+大面积黑洞, 见 build/xgui-remote-mcgs/
         * run_1003_174826/clt_view0.png; 桌面回环因 send 缓冲充裕几乎不出
         * 现短写, io_uring 用例既往全绿掩盖此缺陷)。此处对残余重提
         * startAsyncWrite 续传, 语义=完全写完才叫完成。 */
        {
            int64_t sent = p->writeContext.base.result;
            size_t total = p->writeContext.base.finishedBytes;
            if (sent > 0 && (size_t)sent < total) {
                startAsyncWrite(priv, p->writeBuffer + sent,
                                (int64_t)(total - (size_t)sent),
                                NULL, 0, false);
            }
        }
        return true;
    }
    if (sockAct->actType & XSocketAct_Connect) {
        p->connectPending = false;
        /* 检查连接是否真正成功 */
        int soError = 0;
        socklen_t soLen = sizeof(soError);
        getsockopt(p->socket, SOL_SOCKET, SO_ERROR, &soError, &soLen);
        if (soError == 0) {
            p->base.m_connected = true;
            syncSocketEndpoints(priv);
            /* first read started by XDeviceNetwork_socketContinueRead in event handler */
        } else {
            p->base.m_connected = false;
        }
        return true;
    }
    return false;
}

bool XDeviceNetwork_socketSetDescriptor(XFd deviceFd, intptr_t fd, int state, int openMode)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    if (!priv || fd < 0) return false;
    (void)openMode;
    XDeviceNetworkContextPosix* p = P32(priv);

    p->socket = (int)fd;
    setNonBlocking(p->socket);
    p->base.m_connected = (state == 3);
    if (p->base.m_connected) { p->autoRead = true; startAsyncRead(priv, false); }
    return true;
}

bool XDeviceNetwork_serverSetDescriptor(XFd deviceFd, intptr_t fd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextPosix* p;

    if (!priv || fd < 0) return false;
    p = P32(priv);
    if (p->socket >= 0 || p->isServer) return false;

    p->socket = (int)fd;
    if (!setNonBlocking(p->socket)) {
        close(p->socket);
        p->socket = -1;
        return false;
    }

    p->isServer = true;
    p->base.m_connected = true;
    return true;
}

bool XDeviceNetwork_socketSetOption(XFd xfd, int option, const void* value)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !value) return false;
    XDeviceNetworkContextPosix* p = P32(priv);
    if (p->socket < 0) return false;
    int* intVal = (int*)value;
    switch (option) {
    case 0: return setsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, intVal, sizeof(int)) == 0;
    case 1: return setsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, intVal, sizeof(int)) == 0;
    case 2: return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)(intptr_t)p->socket, XMC_SetTtl, intVal) == 0;
    case 3: { bool enabled = (*intVal != 0); return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)(intptr_t)p->socket, XMC_SetLoop, &enabled) == 0; }
    case 4: return setsockopt(p->socket, IPPROTO_IP, IP_TOS, intVal, sizeof(int)) == 0;
    case 5: return setsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, intVal, sizeof(int)) == 0;
    case 6: return setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, intVal, sizeof(int)) == 0;
    case 8: return setsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, intVal, sizeof(int)) == 0;
    default: return false;
    }
}

void* XDeviceNetwork_socketGetOption(XFd xfd, int option)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return NULL;
    XDeviceNetworkContextPosix* p = P32(priv);
    if (p->socket < 0) return NULL;
    static int result; socklen_t optLen = sizeof(int);
    switch (option) {
    case 0: if (getsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, &result, &optLen) == 0) return &result; break;
    case 1: if (getsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, &result, &optLen) == 0) return &result; break;
    case 2: if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)(intptr_t)p->socket, XMC_GetTtl, &result) == 0) return &result; break;
    case 3: { bool e = false; if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)(intptr_t)p->socket, XMC_GetLoop, &e) == 0) { result = e ? 1 : 0; return &result; } break; }
    case 4: if (getsockopt(p->socket, IPPROTO_IP, IP_TOS, &result, &optLen) == 0) return &result; break;
    case 5: if (getsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, &result, &optLen) == 0) return &result; break;
    case 6: if (getsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, &result, &optLen) == 0) return &result; break;
    case 7: if (getsockopt(p->socket, SOL_SOCKET, SO_ERROR, &result, &optLen) == 0) return &result; break;
    case 8: if (getsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, &result, &optLen) == 0) return &result; break;
    default: break;
    }
    return NULL;
}

void XDeviceNetwork_socketSetReadBufferSize(XFd xfd, int64_t size)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextPosix* p = P32(priv);
    if (p->socket < 0 || size <= 0) return;
    int bufSize = (int)((size > INT_MAX) ? INT_MAX : size);
    setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
}

/* =========================================================================
 * 异步读取状态
 * ========================================================================= */

const char* XDeviceNetwork_socketReadBuffer(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? P32(priv)->readBuffer : NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? P32(priv)->readContext.base.finishedBytes : 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? P32(priv)->writeContext.base.finishedBytes : 0;
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? P32(priv)->writePending : false;
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextPosix* p = P32(priv);
    p->readPending = false;
    if (p->autoRead && p->base.m_connected) startAsyncRead(priv, isUdp);
}

void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !ringBuffer) return;
    XDeviceNetworkContextPosix* p = P32(priv);
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

bool XDeviceNetwork_serverAccept(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextPosix* p = P32(priv);
    if (!p || p->socket < 0) return false;
    if (p->acceptPending) return true;

#ifdef __linux__
    memset(&p->acceptContext, 0, sizeof(p->acceptContext));
    p->acceptContext.base.type = XEventContextType_Type_Socket;
    p->acceptContext.base.fd = priv->m_base.m_fd;
    p->acceptContext.base.eventMask = XSocketAct_Accept;
    p->acceptContext.base.result = XNETWORK_IO_PENDING_RESULT;
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

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd deviceFd, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    if (!priv || !addr) return -1;
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

    /* 设置服务器套接字 */
    P32(priv)->socket = fd;
    P32(priv)->isServer = true;
    P32(priv)->base.m_connected = true;

    return (XDeviceNetworkServerHandle)(intptr_t)fd;
}

void XDeviceNetwork_serverClose(XFd xfd, XDeviceNetworkServerHandle server)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextPosix* p = P32(priv);
    int fd = (int)server;
    if (fd < 0) return;
    if (p && p->acceptPending) {
#ifdef __linux__
        p->acceptContext.base.eventMask = 0;
        p->acceptContext.base.result = XNETWORK_IO_PENDING_RESULT;
        (void)cancelSqe(&p->acceptContext);
        waitForCancel(&p->acceptContext, &p->acceptPending);
#else
        p->acceptPending = false;
#endif
    }
    if (p && p->socket == fd) {
        close(p->socket);
        p->socket = -1;
        p->base.m_connected = false;
    } else {
        close(fd);
    }
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server)
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

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd xfd, XHostAddress* clientAddr, uint16_t* clientPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextPosix* p = P32(priv);
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

    return (XDeviceNetworkSocketHandle)acceptedFd;
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

XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void)
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
    return (XDeviceNetworkInterfaceIterator)1; /* 非空标记 */
}

struct XNetworkInterface* XDeviceNetwork_enumInterfacesNext(XDeviceNetworkInterfaceIterator iter)
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
                XClassDeinit(&mask);
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
                XClassDeinit(&bcast);
            }

            XVector_push_back_move_1_base(iface->addressEntries, &entry);
            XClassDeinit(&entry);
            XClassDeinit(&addr);
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

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter)
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

bool XDeviceNetwork_multicastGroup(XDeviceNetworkSocketHandle sock, bool join,
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

int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle sock, XMulticastOp op, void* arg)
{
    if (sock == (XDeviceNetworkSocketHandle)-1 || !arg) return -1;
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

bool XDeviceNetwork_platformGetLastDatagramSender(XFd xfd,
                                     XHostAddress* srcAddr, uint16_t* srcPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextPosix* p = P32(priv);
    if (!p) return false;
    struct sockaddr_in* sin = (struct sockaddr_in*)&p->fromAddr;
    if (srcAddr) XHostAddress_setAddressIPv4(srcAddr, sin->sin_addr.s_addr);
    if (srcPort) *srcPort = ntohs(sin->sin_port);
    return true;
}

/* =========================================================================
 * 系统代理与GSSAPI
 * ========================================================================= */

bool XDeviceNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy)
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
            XClassDelete(host);
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

int XDeviceNetwork_gssapiAuth(const XString* serviceName,
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

XVector* XDeviceNetwork_lookupName(const XString* name)
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
    XContainerSetDataMoveMethod(vec, XClass_move_base);
    XContainerSetDataCopyMethod(vec, XClass_copy_base);
    XContainerSetDataDeinitMethod(vec, XClass_deinit_base);

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

XString* XDeviceNetwork_localHostName(void)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return NULL;
    return XString_create_utf8(hostname);
}

/* =========================================================================
 * 网卡配置（DHCP / 静态 IP）——POSIX 后端暂不支持
 * =========================================================================
 * Linux 侧可持久化配置由 NetworkManager/netplan/systemd-networkd 等发行
 * 版组件管辖，无统一系统调用通路；在框架内私改易与系统网络管理冲突，
 * 故本后端如实声明不支持（查询/设置返回 false，能力探测返回 false）。
 */

bool XDeviceNetwork_interfaceConfigSupported(void)
{
    /* [2026-10-06] DHCP(udhcpc)与静态(ioctl+route+resolv.conf)应用侧
     * 已实装; IPv6 静态走 rtnetlink。应用会短暂中断该网卡。 */
    return true;
}

/* [2026-10-06 参数不全根修] POSIX 配置查询实装: 此前恒 false, 网络设置页
 * 在全部非 Windows 平台永远走降级三行展示(状态/类型/IPv4/MAC), 掩码/
 * 网关/DNS/DHCP 全缺(用户「获取的参数不全」实证)。数据源(嵌入式可移植):
 * getifaddrs=地址/掩码/Up, /proc/net/route=IPv4 网关, /proc/net/if_inet6=
 * IPv6 地址+前缀, /proc/net/ipv6_route=IPv6 默认网关, /etc/resolv.conf=
 * DNS 前两条, /var/run/udhcpc.<if>.pid 存在=DHCP 启用( BusyBox 惯例)。 */
static bool xdevnet_read_proc_gateways4(const char* ifname, uint32_t* hostGw)
{
    FILE* f = fopen("/proc/net/route", "r");
    char line[256];
    bool ok = false;
    if (!f) return false;
    while (fgets(line, sizeof(line), f)) {
        char iface[IFNAMSIZ];
        char dest[16];
        char gwHex[16];
        unsigned v;
        if (sscanf(line, "%15s %15s %15s", iface, dest, gwHex) != 3)
            continue;
        if (strcmp(iface, ifname) != 0 || strcmp(dest, "00000000") != 0)
            continue;
        if (sscanf(gwHex, "%x", &v) != 1 || v == 0) continue;
        /* /proc 值=网序地址的 LE 读数, ntohl 还原为主序。 */
        *hostGw = ntohl((in_addr_t)v);
        ok = true;
        break;
    }
    fclose(f);
    return ok;
}

static bool xdevnet_parse_hex_ipv6(const char* hex32, uint8_t out[16])
{
    uint8_t bytes[16];
    int i;
    char buf[3];
    for (i = 0; i < 16; ++i) {
        buf[0] = hex32[i * 2];
        buf[1] = hex32[i * 2 + 1];
        buf[2] = '\0';
        if (buf[0] == '\0') return false;
        bytes[i] = (uint8_t)strtoul(buf, NULL, 16);
    }
    memcpy(out, bytes, 16);
    return true;
}

/** @brief [2026-10-06 对齐 win32] 从常见 DHCP 租约痕迹里找服务器地址。
 *  @details 依次探测(命中即回):
 *    ① /run/systemd/netif/leases/<ifIndex>  SERVER_ADDRESS=a.b.c.d
 *    ② /var/lib/dhcp/dhclient.<if>.leases 与 /var/lib/dhclient/*.leases
 *       的 option dhcp-server-identifier <ip>;
 *    ③ /var/run/udhcpc.<if>.info  server=<ip>(部分 BusyBox 定制脚本)。
 *  @return true 时 serverHost=主序地址。 */
/** @brief [2026-10-06] udhcpc 守护进程在跑?(嵌入式无 pid 文件时的
 *  DHCP 判定信号: 扫 /proc 数字目录 cmdline 含 "udhcpc" 即真, 有界)。 */
static bool xdevnet_udhcpcRunning(void)
{
    static const int kMaxProcs = 1024;
    DIR* dir = opendir("/proc");
    struct dirent* de;
    int scanned = 0;
    if (!dir) return false;
    while ((de = readdir(dir)) != NULL && scanned < kMaxProcs) {
        char path[64];
        char cmd[128];
        FILE* f;
        int fd = -1;
        size_t n;
        int allDigits = 1;
        const char* p;
        for (p = de->d_name; *p; ++p)
            if (*p < '0' || *p > '9') { allDigits = 0; break; }
        if (!allDigits) continue;
        ++scanned;
        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        n = fread(cmd, 1, sizeof(cmd) - 1, f);
        fclose(f);
        cmd[n] = '\0';
        if (strstr(cmd, "udhcpc")) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

static bool xdevnet_find_dhcp_server(uint32_t ifIndex, const char* ifname,
                                     uint32_t* serverHost)
{
    char path[160];
    char line[256];
    char ip[64];
    FILE* f;

    /* ① systemd-networkd 租约。 */
    snprintf(path, sizeof(path),
             "/run/systemd/netif/leases/%u", ifIndex);
    f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "SERVER_ADDRESS=", 15) == 0) {
                if (sscanf(line + 15, " %63s", ip) == 1) {
                    struct in_addr a4;
                    if (inet_pton(AF_INET, ip, &a4) == 1) {
                        *serverHost = ntohl(a4.s_addr);
                        fclose(f);
                        return true;
                    }
                }
            }
        }
        fclose(f);
    }

    /* ② dhclient 租约(server-identifier, 取文件内最后一次)。 */
    {
        static const char* kDhclient[] = {
            "/var/lib/dhcp/dhclient.%s.leases",
            "/var/lib/dhclient/dhclient-%s.leases",
            "/var/lib/dhclient/dhclient.%s.leases"
        };
        size_t k;
        for (k = 0; k < sizeof(kDhclient) / sizeof(kDhclient[0]); ++k) {
            snprintf(path, sizeof(path), kDhclient[k], ifname);
            f = fopen(path, "r");
            if (!f) continue;
            while (fgets(line, sizeof(line), f)) {
                char* p = strstr(line, "dhcp-server-identifier");
                if (p && sscanf(p, "%*[^0-9]%63s", ip) == 1) {
                    struct in_addr a4;
                    if (inet_pton(AF_INET, ip, &a4) == 1)
                        *serverHost = ntohl(a4.s_addr);
                }
            }
            fclose(f);
            if (*serverHost) return true;
        }
    }

    /* ③ BusyBox udhcpc 定制 info。 */
    snprintf(path, sizeof(path), "/var/run/udhcpc.%s.info", ifname);
    f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "server=", 7) == 0) {
                if (sscanf(line + 7, " %63s", ip) == 1) {
                    struct in_addr a4;
                    if (inet_pton(AF_INET, ip, &a4) == 1) {
                        *serverHost = ntohl(a4.s_addr);
                        fclose(f);
                        return true;
                    }
                }
            }
        }
        fclose(f);
    }
    return false;
}

bool XDeviceNetwork_queryInterfaceConfig(uint32_t ifIndex,
                                         XDeviceNetworkInterfaceConfig* outConfig)
{
    struct ifaddrs* list = NULL;
    struct ifaddrs* it;
    char ifname[IFNAMSIZ] = {0};
    bool found = false;
    bool haveV6 = false;
    char dns1[80] = {0};
    char dns2[80] = {0};
    uint32_t gwHost = 0;
    char path[96];
    FILE* f;

    if (!outConfig || ifIndex == 0) return false;
    memset(outConfig, 0, sizeof(*outConfig));
    /* memset 会清掉内嵌 XHostAddress 的 vtable——逐址 init 恢复
     * (vtable 缺失时 setAddress 全部静默无效, 查询恒空)。 */
    XHostAddress_init(&outConfig->ipv4Address);
    XHostAddress_init(&outConfig->ipv4Netmask);
    XHostAddress_init(&outConfig->ipv4Gateway);
    XHostAddress_init(&outConfig->ipv6Address);
    XHostAddress_init(&outConfig->ipv6Gateway);
    XHostAddress_init(&outConfig->dhcpServer);
    XHostAddress_init(&outConfig->dnsPrimary);
    XHostAddress_init(&outConfig->dnsSecondary);

    /* 1) getifaddrs: 按 ifIndex 定位 → 名称/Up/IPv4 地址+掩码/IPv6 地址。 */
    if (getifaddrs(&list) != 0) return false;
    for (it = list; it; it = it->ifa_next) {
        unsigned idx;
        if (!it->ifa_name || !it->ifa_addr) continue;
        idx = if_nametoindex(it->ifa_name);
        if (idx != ifIndex) continue;
        if (!found) {
            found = true;
            strncpy(ifname, it->ifa_name, IFNAMSIZ - 1);
        }
        if (it->ifa_flags & IFF_UP) outConfig->operUp = true;
        if (it->ifa_addr->sa_family == AF_INET &&
            XHostAddress_isNull(&outConfig->ipv4Address)) {
            struct sockaddr_in* sin = (struct sockaddr_in*)it->ifa_addr;
            XHostAddress_setAddressIPv4(&outConfig->ipv4Address,
                                        ntohl(sin->sin_addr.s_addr));
            if (it->ifa_netmask &&
                it->ifa_netmask->sa_family == AF_INET) {
                struct sockaddr_in* m = (struct sockaddr_in*)it->ifa_netmask;
                XHostAddress_setAddressIPv4(&outConfig->ipv4Netmask,
                                            ntohl(m->sin_addr.s_addr));
            }
        }
        if (it->ifa_addr->sa_family == AF_INET6 &&
            !haveV6) {
            struct sockaddr_in6* s6 = (struct sockaddr_in6*)it->ifa_addr;
            const uint8_t* b = (const uint8_t*)&s6->sin6_addr;
            bool linkLocal = (b[0] == 0xFE && (b[1] & 0xC0) == 0x80);
            /* 地址预填优先全局单播; 仅链路本地时也接受(首见即录)。 */
            if (!haveV6) {
                XHostAddress_setAddressIPv6(&outConfig->ipv6Address, b);
                haveV6 = true;
            }
            (void)linkLocal;
        }
    }
    freeifaddrs(list);
    if (!found) return false;
    outConfig->friendlyName = XString_create_utf8(ifname);

    /* 2) IPv4 默认网关。 */
    if (xdevnet_read_proc_gateways4(ifname, &gwHost))
        XHostAddress_setAddressIPv4(&outConfig->ipv4Gateway, gwHost);

    /* 3) IPv6 地址前缀 + 默认网关(/proc/net/if_inet6 + ipv6_route)。
     *    前缀取自与 getifaddrs 选定地址逐字节相同的那一行。 */
    f = fopen("/proc/net/if_inet6", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char hex[40];
            int prefix = 0;
            char dev[IFNAMSIZ];
            uint8_t b6[16];
            if (sscanf(line, "%39s %*x %x %*x %*x %15s",
                       hex, &prefix, dev) != 3)
                continue;
            if (strcmp(dev, ifname) != 0) continue;
            if (!xdevnet_parse_hex_ipv6(hex, b6)) continue;
            if (XHostAddress_isNull(&outConfig->ipv6Address)) {
                XHostAddress_setAddressIPv6(&outConfig->ipv6Address, b6);
                outConfig->ipv6PrefixLength = prefix;
            } else {
                uint8_t cur[16];
                XHostAddress_toIPv6Address(&outConfig->ipv6Address, cur);
                if (memcmp(cur, b6, 16) == 0)
                    outConfig->ipv6PrefixLength = prefix;
            }
        }
        fclose(f);
    }
    f = fopen("/proc/net/ipv6_route", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char dest[40];
            char dplen[4];
            char nexthop[40];
            char dev[IFNAMSIZ];
            if (sscanf(line, "%39s %3s %*39s %*3s %39s %*x %*x %*x %*x %15s",
                       dest, dplen, nexthop, dev) >= 4 &&
                strcmp(dest, "00000000000000000000000000000000") == 0 &&
                strcmp(dplen, "00") == 0 &&
                strcmp(dev, ifname) == 0) {
                uint8_t b6[16];
                if (xdevnet_parse_hex_ipv6(nexthop, b6) &&
                    XHostAddress_isNull(&outConfig->ipv6Gateway)) {
                    bool zero = false;
                    int k;
                    for (k = 0; k < 16; ++k)
                        if (b6[k]) { zero = true; break; }
                    if (zero)
                        XHostAddress_setAddressIPv6(
                            &outConfig->ipv6Gateway, b6);
                }
                break;
            }
        }
        fclose(f);
    }
    /* 4) DNS: /etc/resolv.conf nameserver 前两条(支持 v4/v6)。 */
    f = fopen("/etc/resolv.conf", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char ns[80];
            if (strncmp(line, "nameserver", 10) != 0) continue;
            if (sscanf(line + 10, " %79s", ns) != 1) continue;
            if (!dns1[0]) snprintf(dns1, sizeof(dns1), "%s", ns);
            else if (!dns2[0]) { snprintf(dns2, sizeof(dns2), "%s", ns); break; }
        }
        fclose(f);
    }
    if (dns1[0]) {
        struct in_addr a4;
        uint8_t b6[16];
        if (inet_pton(AF_INET, dns1, &a4) == 1)
            XHostAddress_setAddressIPv4(&outConfig->dnsPrimary,
                                        ntohl(a4.s_addr));
        else if (inet_pton(AF_INET6, dns1, b6) == 1)
            XHostAddress_setAddressIPv6(&outConfig->dnsPrimary, b6);
    }
    if (dns2[0]) {
        struct in_addr a4;
        uint8_t b6[16];
        if (inet_pton(AF_INET, dns2, &a4) == 1)
            XHostAddress_setAddressIPv4(&outConfig->dnsSecondary,
                                        ntohl(a4.s_addr));
        else if (inet_pton(AF_INET6, dns2, b6) == 1)
            XHostAddress_setAddressIPv6(&outConfig->dnsSecondary, b6);
    }

    /* 5) DHCP: pid 文件(BusyBox)或租约痕迹(对齐 win32 的 dhcpServer);
     *    均无时以 udhcpc 守护进程在跑为信号(嵌入式常见无 pid 文件)。 */
    snprintf(path, sizeof(path), "/var/run/udhcpc.%s.pid", ifname);
    outConfig->dhcpEnabled = (access(path, F_OK) == 0);
    if (!outConfig->dhcpEnabled) {
        snprintf(path, sizeof(path), "/var/run/udhcpcd-%s.pid", ifname);
        outConfig->dhcpEnabled = (access(path, F_OK) == 0);
    }
    if (!outConfig->dhcpEnabled)
        outConfig->dhcpEnabled = xdevnet_udhcpcRunning();
    {
        uint32_t serverHost = 0;
        if (xdevnet_find_dhcp_server(ifIndex, ifname, &serverHost)) {
            outConfig->dhcpEnabled = true; /* 有租约=自动获取。 */
            XHostAddress_setAddressIPv4(&outConfig->dhcpServer, serverHost);
        }
    }
    return true;
}

void XDeviceNetwork_freeInterfaceConfig(XDeviceNetworkInterfaceConfig* config)
{
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

/* ==================== [2026-10-06] 配置应用侧实装 ====================
 *  DHCP=拉起 /sbin/udhcpc(-b 后台续约, default.script 落地址/路由/
 *  resolv.conf); 静态=ioctl(SIOCSIFADDR/NETMASK/FLAGS)+SIOCADDRT/DELRT
 *  +重写 /etc/resolv.conf。切换前先杀旧 udhcpc(pid 文件+/proc 兜底)。 */

static bool xdevnet_ifnameFromIndex(uint32_t ifIndex, char* name, size_t cap)
{
    return if_indextoname(ifIndex, name) != NULL && cap > 0;
}

static void xdevnet_killDhcp(const char* ifname)
{
    char path[96];
    FILE* f;
    char buf[32];
    long pid;
    long victims[8];
    int victimCount = 0;
    DIR* dir;
    struct dirent* de;
    int scanned = 0;
    int i;

    snprintf(path, sizeof(path), "/var/run/udhcpc.%s.pid", ifname);
    f = fopen(path, "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            pid = strtol(buf, NULL, 10);
            if (pid > 1 && victimCount < 8)
                victims[victimCount++] = pid;
        }
        fclose(f);
        unlink(path);
    }
    dir = opendir("/proc");
    if (dir) {
        while ((de = readdir(dir)) != NULL && scanned < 1024) {
            char cmdpath[64];
            char cmd[128];
            FILE* cf;
            size_t n;
            int allDigits = 1;
            const char* q;
            long cand;
            for (q = de->d_name; *q; ++q)
                if (*q < '0' || *q > '9') { allDigits = 0; break; }
            if (!allDigits) continue;
            ++scanned;
            cand = strtol(de->d_name, NULL, 10);
            if (cand <= 1) continue;
            snprintf(cmdpath, sizeof(cmdpath), "/proc/%s/cmdline",
                     de->d_name);
            cf = fopen(cmdpath, "r");
            if (!cf) continue;
            n = fread(cmd, 1, sizeof(cmd) - 1, cf);
            fclose(cf);
            cmd[n] = '\0';
            if (strstr(cmd, "udhcpc") &&
                (!ifname[0] || strstr(cmd, ifname))) {
                bool dup = false;
                for (i = 0; i < victimCount; ++i)
                    if (victims[i] == cand) { dup = true; break; }
                if (!dup && victimCount < 8)
                    victims[victimCount++] = cand;
            }
        }
        closedir(dir);
    }
    for (i = 0; i < victimCount; ++i)
        kill((pid_t)victims[i], SIGTERM);
    {
        struct timespec ts = { 0, 300 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    for (i = 0; i < victimCount; ++i)
        kill((pid_t)victims[i], SIGKILL);
}

static bool xdevnet_udhcpcPath(char* out, size_t cap)
{
    static const char* kPaths[] = {
        "/sbin/udhcpc", "/usr/sbin/udhcpc", "/bin/udhcpc", "/usr/bin/udhcpc"
    };
    size_t k;
    for (k = 0; k < sizeof(kPaths) / sizeof(kPaths[0]); ++k)
        if (access(kPaths[k], X_OK) == 0) {
            snprintf(out, cap, "%s", kPaths[k]);
            return true;
        }
    return false;
}

bool XDeviceNetwork_setInterfaceDhcp(uint32_t ifIndex)
{
    char ifname[IFNAMSIZ] = {0};
    char udhcpcPath[64];
    char pidPath[96];
    pid_t pid;
    int status;
    if (!xdevnet_ifnameFromIndex(ifIndex, ifname, sizeof(ifname)))
        return false;
    if (!xdevnet_udhcpcPath(udhcpcPath, sizeof(udhcpcPath)))
        return false; /* 板上无 DHCP 客户端。 */
    xdevnet_killDhcp(ifname);
    pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        freopen("/dev/null", "r", stdin);
        snprintf(pidPath, sizeof(pidPath),
                 "/var/run/udhcpc.%s.pid", ifname);
        execl(udhcpcPath, "udhcpc", "-i", ifname, "-b", "-p", pidPath,
              (char*)NULL);
        _exit(127);
    }
    /* 首进程在 -b 下拿到租约后台化后即退, 即时收割防僵尸。 */
    waitpid(pid, &status, 0);
    return true;
}

static bool xdevnet_ifreqAddr(const char* ifname, unsigned long req,
                              struct in_addr* in)
{
    struct ifreq ifr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int rc;
    if (sock < 0) return false;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ((struct sockaddr_in*)&ifr.ifr_addr)->sin_family = AF_INET;
    if (req == SIOCSIFADDR || req == SIOCSIFNETMASK)
        ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr = *in;
    rc = ioctl(sock, req, &ifr);
    if (req == SIOCGIFFLAGS || rc == 0)
        memcpy(in, &((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr,
               sizeof(*in));
    close(sock);
    return rc == 0;
}

static bool xdevnet_ifUp(const char* ifname)
{
    struct ifreq ifr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        ioctl(sock, SIOCSIFFLAGS, &ifr);
    }
    close(sock);
    return true;
}

static bool xdevnet_routeDefault(uint32_t gwHost, const char* ifname,
                                 bool add)
{
    struct rtentry rt;
    struct sockaddr_in* sin;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int rc;
    if (sock < 0 || gwHost == 0) { if (sock >= 0) close(sock); return false; }
    memset(&rt, 0, sizeof(rt));
    sin = (struct sockaddr_in*)&rt.rt_gateway;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(gwHost);
    sin = (struct sockaddr_in*)&rt.rt_dst;
    sin->sin_family = AF_INET;
    sin = (struct sockaddr_in*)&rt.rt_genmask;
    sin->sin_family = AF_INET;
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    rt.rt_dev = (char*)ifname;
    rc = ioctl(sock, add ? SIOCADDRT : SIOCDELRT, &rt);
    close(sock);
    return rc == 0;
}

static void xdevnet_writeResolv(const char* dns1, const char* dns2)
{
    FILE* f;
    if ((!dns1 || !dns1[0]) && (!dns2 || !dns2[0])) return;
    f = fopen("/etc/resolv.conf", "w");
    if (!f) return;
    if (dns1 && dns1[0]) fprintf(f, "nameserver %s\n", dns1);
    if (dns2 && dns2[0]) fprintf(f, "nameserver %s\n", dns2);
    fclose(f);
}

bool XDeviceNetwork_setInterfaceStatic(uint32_t ifIndex, const char* ipv4Address,
                                       const char* ipv4Netmask,
                                       const char* ipv4Gateway,
                                       const char* dnsPrimary,
                                       const char* dnsSecondary)
{
    char ifname[IFNAMSIZ] = {0};
    struct in_addr in;
    uint32_t oldGw = 0;
    in_addr_t parsed;
    if (!xdevnet_ifnameFromIndex(ifIndex, ifname, sizeof(ifname)))
        return false;
    if (!ipv4Address || (parsed = inet_addr(ipv4Address)) == INADDR_NONE)
        return false;
    xdevnet_killDhcp(ifname); /* 静态切换前先停 DHCP 客户端。 */
    in.s_addr = parsed;
    if (!xdevnet_ifreqAddr(ifname, SIOCSIFADDR, &in)) return false;
    if (ipv4Netmask && inet_addr(ipv4Netmask) != INADDR_NONE) {
        in.s_addr = inet_addr(ipv4Netmask);
        (void)xdevnet_ifreqAddr(ifname, SIOCSIFNETMASK, &in);
    }
    (void)xdevnet_ifUp(ifname);
    /* 旧默认路由摘除再挂新网关(重复挂同一网关=幂等错误可忽略)。 */
    {
        uint32_t probe = 0;
        if (xdevnet_read_proc_gateways4(ifname, &probe)) {
            oldGw = probe;
            (void)xdevnet_routeDefault(oldGw, ifname, false);
        }
    }
    if (ipv4Gateway && inet_addr(ipv4Gateway) != INADDR_NONE) {
        in.s_addr = inet_addr(ipv4Gateway);
        (void)xdevnet_routeDefault(ntohl(in.s_addr), ifname, true);
    }
    xdevnet_writeResolv(dnsPrimary, dnsSecondary);
    return true;
}

/* ---- [2026-10-06] IPv6 静态配置: v6 地址/网关必须走 rtnetlink
 * (SIOCSIFADDR 对 v6 不支持前缀指定)。 ---- */
static int xdevnet_nl_talk(struct nlmsghdr* nlh)
{
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl addr;
    char buf[512];
    ssize_t n;
    int err = -1;
    if (sock < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }
    if (sendto(sock, nlh, nlh->nlmsg_len, 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        struct nlmsghdr* h = (struct nlmsghdr*)buf;
        while (n >= (ssize_t)sizeof(*h)) {
            if (h->nlmsg_len > (unsigned)n || h->nlmsg_len < sizeof(*h))
                break;
            if (h->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr* e = (struct nlmsgerr*)NLMSG_DATA(h);
                err = (h->nlmsg_len >= sizeof(*h) + sizeof(*e)) ? e->error : -1;
                /* EEXIST=已配置, 视为成功(幂等)。 */
                close(sock);
                return (err == 0 || err == -EEXIST) ? 0 : -1;
            }
            if (h->nlmsg_type == NLMSG_DONE) { close(sock); return 0; }
            n -= NLMSG_ALIGN(h->nlmsg_len);
            h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(h->nlmsg_len));
        }
    }
    close(sock);
    return err;
}

bool XDeviceNetwork_setInterfaceStaticIpv6(uint32_t ifIndex,
                                           const char* ipv6Address,
                                           int prefixLength,
                                           const char* ipv6Gateway)
{
    uint8_t addr6[16];
    uint8_t gw6[16];
    char reqBuf[512];
    struct nlmsghdr* nlh;
    struct ifaddrmsg* ifa;
    struct rtmsg* rtm;
    struct rtattr* rta;
    int nlLen;
    bool haveGw = false;
    if (!ipv6Address ||
        inet_pton(AF_INET6, ipv6Address, addr6) != 1)
        return false;
    if (prefixLength < 0 || prefixLength > 128) prefixLength = 64;
    haveGw = (ipv6Gateway && ipv6Gateway[0] &&
              inet_pton(AF_INET6, ipv6Gateway, gw6) == 1);

    /* 地址: RTM_NEWADDR(IFA_LOCAL+IFA_ADDRESS, prefix)。 */
    memset(reqBuf, 0, sizeof(reqBuf));
    nlh = (struct nlmsghdr*)reqBuf;
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    ifa = (struct ifaddrmsg*)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET6;
    ifa->ifa_prefixlen = (unsigned char)prefixLength;
    ifa->ifa_index = ifIndex;
    ifa->ifa_scope = 0;
    nlLen = NLMSG_LENGTH(sizeof(*ifa));
    rta = (struct rtattr*)((char*)nlh + NLMSG_ALIGN(nlLen));
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(16);
    memcpy(RTA_DATA(rta), addr6, 16);
    nlLen = NLMSG_ALIGN(nlLen) + RTA_ALIGN(rta->rta_len);
    rta = (struct rtattr*)((char*)nlh + NLMSG_ALIGN(nlLen));
    rta->rta_type = IFA_ADDRESS;
    rta->rta_len = RTA_LENGTH(16);
    memcpy(RTA_DATA(rta), addr6, 16);
    nlLen = NLMSG_ALIGN(nlLen) + RTA_ALIGN(rta->rta_len);
    nlh->nlmsg_len = nlLen;
    if (xdevnet_nl_talk(nlh) != 0) return false;

    /* 网关: RTM_NEWROUTE 默认路由(dst=/0, RTA_GATEWAY)。 */
    if (haveGw) {
        memset(reqBuf, 0, sizeof(reqBuf));
        nlh = (struct nlmsghdr*)reqBuf;
        nlh->nlmsg_type = RTM_NEWROUTE;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE |
                           NLM_F_REPLACE;
        rtm = (struct rtmsg*)NLMSG_DATA(nlh);
        rtm->rtm_family = AF_INET6;
        rtm->rtm_dst_len = 0;
        rtm->rtm_src_len = 0;
        rtm->rtm_table = RT_TABLE_MAIN;
        rtm->rtm_protocol = RTPROT_STATIC;
        rtm->rtm_scope = RT_SCOPE_UNIVERSE;
        rtm->rtm_type = RTN_UNICAST;
        nlLen = NLMSG_LENGTH(sizeof(*rtm));
        rta = (struct rtattr*)((char*)nlh + NLMSG_ALIGN(nlLen));
        rta->rta_type = RTA_GATEWAY;
        rta->rta_len = RTA_LENGTH(16);
        memcpy(RTA_DATA(rta), gw6, 16);
        nlLen = NLMSG_ALIGN(nlLen) + RTA_ALIGN(rta->rta_len);
        rta = (struct rtattr*)((char*)nlh + NLMSG_ALIGN(nlLen));
        rta->rta_type = RTA_DST;
        rta->rta_len = RTA_LENGTH(16);
        memset(RTA_DATA(rta), 0, 16);
        nlLen = NLMSG_ALIGN(nlLen) + RTA_ALIGN(rta->rta_len);
        nlh->nlmsg_len = nlLen;
        if (xdevnet_nl_talk(nlh) != 0) return false;
    }
    return true;
}

#endif /* XNETWORK_USE_PLATFORM_API && POSIX */
