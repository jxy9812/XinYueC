/**
 * @file XDeviceNetwork_win32.c
 * @brief Windows 平台网络实现（IOCP 异步 I/O）
 */
 /* ====== 配置文件 ====== */
#include "XNetwork_config.h"
#if defined(XNETWORK_USE_PLATFORM_API) &&defined(_WIN32) 

 /* ====== Windows 宏定义必须在所有头文件之前 ======*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif



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
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"

/* ====== Windows SDK 头文件 ====== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdlib.h>
#include "XNetIoRingWin32.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "secur32.lib")

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
 * 手动定义缺失的宏（兼容旧版 SDK）
 * ========================================================================= */

#ifndef SO_UPDATE_CONNECT_CONTEXT
#define SO_UPDATE_CONNECT_CONTEXT 0x7010
#endif

#ifndef SO_UPDATE_ACCEPT_CONTEXT
#define SO_UPDATE_ACCEPT_CONTEXT 0x700B
#endif

#ifndef IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED
#define IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED 0x0004
#endif

#ifndef IP_ADAPTER_ADDRESS_SKIP_AS_SOURCE
#define IP_ADAPTER_ADDRESS_SKIP_AS_SOURCE 0x0080
#endif
static PIP_ADAPTER_ADDRESSES current = 0;

bool XDeviceNetwork_icmpEchoSupported(void)
{
    return true;
}

bool XDeviceNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
                       uint16_t sequence, const void* payload, size_t payloadSize,
                       int timeoutMilliseconds, uint32_t* elapsedMilliseconds)
{
    HANDLE icmpHandle = INVALID_HANDLE_VALUE;
    bool result = false;
    (void)identifier;
    (void)sequence;
    if (!address ||
        (XHostAddress_protocol(address) != XHostAddress_IPv4Protocol &&
         XHostAddress_protocol(address) != XHostAddress_IPv6Protocol) ||
        (payloadSize && !payload) || payloadSize > 65507u || timeoutMilliseconds <= 0)
        return false;

    if (XHostAddress_protocol(address) == XHostAddress_IPv4Protocol) {
        IP_OPTION_INFORMATION options;
        ICMP_ECHO_REPLY* reply;
        size_t replyCapacity;
        DWORD replyCount;

        icmpHandle = IcmpCreateFile();
        if (icmpHandle == INVALID_HANDLE_VALUE) return false;
        replyCapacity = sizeof(ICMP_ECHO_REPLY) + payloadSize + 8u;
        reply = (ICMP_ECHO_REPLY*)XMalloc_System(replyCapacity);
        if (!reply) {
            IcmpCloseHandle(icmpHandle);
            return false;
        }
        memset(&options, 0, sizeof(options));
        options.Ttl = 128;
        replyCount = IcmpSendEcho(icmpHandle,
                                   htonl(XHostAddress_toIPv4Address(address)),
                                   (LPVOID)payload, (WORD)payloadSize, &options,
                                   reply, (DWORD)replyCapacity,
                                   (DWORD)timeoutMilliseconds);
        if (replyCount > 0 && reply[0].Status == IP_SUCCESS) {
            if (elapsedMilliseconds) *elapsedMilliseconds = reply[0].RoundTripTime;
            result = true;
        }
        XFree_System(reply);
        IcmpCloseHandle(icmpHandle);
    } else {
        struct sockaddr_in6 source;
        struct sockaddr_in6 destination;
        ICMPV6_ECHO_REPLY* reply;
        IP_OPTION_INFORMATION options;
        size_t replyCapacity;
        DWORD replyCount;

        icmpHandle = Icmp6CreateFile();
        if (icmpHandle == INVALID_HANDLE_VALUE) return false;
        replyCapacity = sizeof(ICMPV6_ECHO_REPLY) + payloadSize + 8u;
        reply = (ICMPV6_ECHO_REPLY*)XMalloc_System(replyCapacity);
        if (!reply) {
            IcmpCloseHandle(icmpHandle);
            return false;
        }
        memset(&source, 0, sizeof(source));
        memset(&destination, 0, sizeof(destination));
        source.sin6_family = AF_INET6;
        destination.sin6_family = AF_INET6;
        XHostAddress_toIPv6Address(address, (uint8_t*)&destination.sin6_addr);
        memset(&options, 0, sizeof(options));
        options.Ttl = 128;
        replyCount = Icmp6SendEcho2(icmpHandle, NULL, NULL, NULL,
                                    &source, &destination,
                                    (LPVOID)payload, (WORD)payloadSize, &options,
                                    reply, (DWORD)replyCapacity,
                                    (DWORD)timeoutMilliseconds);
        if (replyCount > 0 && reply[0].Status == IP_SUCCESS) {
            if (elapsedMilliseconds) *elapsedMilliseconds = reply[0].RoundTripTime;
            result = true;
        }
        XFree_System(reply);
        IcmpCloseHandle(icmpHandle);
    }
    return result;
}
/* =========================================================================
 * ConnectEx 函数指针及 GUID
 * ========================================================================= */

#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX {0x25a207b9, 0xddf3, 0x4660, {0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e}}
#endif

#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX {0xb5367df1, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}
#endif

#ifndef WSAID_GETACCEPTEXSOCKADDRS
#define WSAID_GETACCEPTEXSOCKADDRS {0xb5367df2, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}
#endif

typedef BOOL(PASCAL* LPFN_CONNECTEX)(
    SOCKET s,
    const struct sockaddr* name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped
    );

typedef BOOL(PASCAL* LPFN_ACCEPTEX)(
    SOCKET sListenSocket,
    SOCKET sAcceptSocket,
    PVOID lpOutputBuffer,
    DWORD dwReceiveDataLength,
    DWORD dwLocalAddressLength,
    DWORD dwRemoteAddressLength,
    LPDWORD lpdwBytesReceived,
    LPOVERLAPPED lpOverlapped
    );

typedef void(PASCAL* LPFN_GETACCEPTEXSOCKADDRS)(
    PVOID lpOutputBuffer,
    DWORD dwReceiveDataLength,
    DWORD dwLocalAddressLength,
    DWORD dwRemoteAddressLength,
    struct sockaddr** LocalSockaddr,
    LPINT LocalSockaddrLength,
    struct sockaddr** RemoteSockaddr,
    LPINT RemoteSockaddrLength
    );

/* =========================================================================
 * 内部宏定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192
#define XNETWORK_WRITE_BUFFER_SIZE 8192

 /* =========================================================================
  * 全局状态
  * ========================================================================= */

static LONG    g_wsaRefCount = 0;
static HANDLE  g_iocp = NULL;
static LPFN_CONNECTEX g_ConnectEx = NULL;
static LPFN_ACCEPTEX g_AcceptEx = NULL;
static LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = NULL;


/* =========================================================================
 * IOCP 辅助函数
 * ========================================================================= */

static HANDLE iocp_get(void)
{
    if (!g_iocp) {
        /* 尝试从 XEventDispatcher 获取全局 IOCP 端口 */
        g_iocp = IOCP_getGlobalPort();
    }
    return g_iocp;
}

static bool iocp_assoc(HANDLE handle, XObject* key)
{
    HANDLE h = iocp_get();
    if (!h) return false;
    //XPrintf("帮:%p\n",key);
    return CreateIoCompletionPort(handle, h, (ULONG_PTR)key, 0) != NULL;
}

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
    }
    else {
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
    }
    else {
        const struct sockaddr_in* s4 = (const struct sockaddr_in*)ss;
        XHostAddress_setAddressIPv4(addr, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/* =========================================================================
 * 平台初始化
 * ========================================================================= */

void XDeviceNetwork_ensureInit(void)
{
    if (InterlockedIncrement(&g_wsaRefCount) == 1) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        /* 获取扩展函数指针 */
        SOCKET tempSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tempSock != INVALID_SOCKET) {
            DWORD bytesReturned;
            GUID guidConnectEx = WSAID_CONNECTEX;
            GUID guidAcceptEx = WSAID_ACCEPTEX;
            GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;

            /* 获取 ConnectEx */
            WSAIoctl(tempSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guidConnectEx, sizeof(guidConnectEx),
                &g_ConnectEx, sizeof(g_ConnectEx),
                &bytesReturned, NULL, NULL);

            /* 获取 AcceptEx */
            WSAIoctl(tempSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guidAcceptEx, sizeof(guidAcceptEx),
                &g_AcceptEx, sizeof(g_AcceptEx),
                &bytesReturned, NULL, NULL);

            /* 获取 GetAcceptExSockaddrs */
            WSAIoctl(tempSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
                &g_GetAcceptExSockaddrs, sizeof(g_GetAcceptExSockaddrs),
                &bytesReturned, NULL, NULL);

            closesocket(tempSock);
        }
    }
}

void XDeviceNetwork_cleanup(void)
{
    if (InterlockedDecrement(&g_wsaRefCount) == 0) {
        WSACleanup();
        g_iocp = NULL;
        g_ConnectEx = NULL;
    }
}

void XDeviceNetwork_poll(void)
{
}

int XDeviceNetwork_lastError(void)
{
    return WSAGetLastError();
}

char* XDeviceNetwork_errorString(int errorCode)
{
    char* buf = (char*)XMalloc_System(256);
    if (!buf) return NULL;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf, 256, NULL);
    buf[255] = '\0'; /* FormatMessageA 在消息填满时可能不补 NUL，强制截断终止。 */
    return buf;
}

bool XDeviceNetwork_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    PMIB_IFTABLE table;
    DWORD tableBytes = 0;
    DWORD result;
    DWORD i;

    if (!rxBytes || !txBytes)
        return false;
    *rxBytes = 0;
    *txBytes = 0;

    (void)GetIfTable(NULL, &tableBytes, FALSE);
    if (tableBytes < sizeof(*table))
        return false;

    table = (PMIB_IFTABLE)XMalloc_System(tableBytes);
    if (!table)
        return false;

    result = GetIfTable(table, &tableBytes, FALSE);
    if (result != NO_ERROR)
    {
        XMemory_free(table, XMEMORY_TYPE_SYSTEM);
        return false;
    }

    for (i = 0; i < table->dwNumEntries; ++i)
    {
        const MIB_IFROW* row = &table->table[i];
        if (row->dwType == MIB_IF_TYPE_LOOPBACK)
            continue;
        *rxBytes += (uint64_t)row->dwInOctets;
        *txBytes += (uint64_t)row->dwOutOctets;
    }

    XMemory_free(table, XMEMORY_TYPE_SYSTEM);
    return true;
}

/* =========================================================================
 * 套接字私有数据结构（平台无关基类 + Win32 扩展）
 * ========================================================================= */

typedef struct XDeviceNetworkContextWin32 {
    XDeviceNetworkContext base;             /**< 第一位：平台无关基类 (owner/xfd/notifiers) */
    SOCKET socket;                          ///< Windows SOCKET 句柄
    HANDLE namedPipe;                       ///< Windows named-pipe 流句柄

    /* 状态标志 */
    bool readPending;
    bool writePending;
    bool connectPending;
    bool autoRead;
    bool isServer;                          ///< 是否为服务器套接字
    bool acceptPending;                     ///< 是否有待处理的 Accept
    volatile LONG pendingIocpOperations;    ///< 生命周期引用：对象自身 + 尚未出队的重叠操作
    volatile LONG deletePending;            ///< 非零表示析构已开始，不再向应用层投递事件


    /* UDP 来源地址 */
    struct sockaddr_in6 fromAddr;
    int fromAddrLen;

    /* IOCP 上下文 */
    XEventContext_IOCP readContext;
    XEventContext_IOCP writeContext;
    union {
        XEventContext_IOCP connectContext;  ///< 客户端连接上下文
        XEventContext_IOCP acceptContext;   ///< 服务器 AcceptEx 上下文
    };

    /* 待连接信息 */
    XHostAddress pendingPeerAddr;
    uint16_t pendingPeerPort;

    /* 缓冲区联合体（服务器用 acceptBuffer，客户端用 readBuffer/writeBuffer）*/
    union {
        struct {
            char readBuffer[XNETWORK_READ_BUFFER_SIZE];
            char writeBuffer[XNETWORK_WRITE_BUFFER_SIZE];
        };
        struct {
            char acceptBuffer[sizeof(struct sockaddr_in6) * 2 + 32]; ///< AcceptEx 缓冲区
            SOCKET acceptSocket;                    ///< AcceptEx 创建的套接字
        };
    };
} XDeviceNetworkContextWin32;

/* 便捷转换宏 */
#define W32(p) ((XDeviceNetworkContextWin32*)(p))

static bool w32_has_stream_handle(const XDeviceNetworkContextWin32* priv)
{
    return priv && (priv->socket != INVALID_SOCKET
                    || priv->namedPipe != INVALID_HANDLE_VALUE);
}

static void destroyContext(XDeviceNetworkContextWin32* p)
{
    if (!p) return;
    XHostAddress_deinit_base(&p->pendingPeerAddr);
    XFree_System(p);
}

static void releaseContext(XDeviceNetworkContextWin32* p)
{
    if (InterlockedDecrement(&p->pendingIocpOperations) == 0)
        destroyContext(p);
}

static bool socketIocpOperationCompleted(XEventContext_IOCP* context, void* userData)
{
    XDeviceNetworkContextWin32* p = (XDeviceNetworkContextWin32*)userData;
    bool shouldDispatch;
    if (!p) return false;

    shouldDispatch = InterlockedCompareExchange(&p->deletePending, 0, 0) == 0 &&
                     p->base.m_owner != NULL;
    context->completionCallback = NULL;
    context->completionUserData = NULL;
    releaseContext(p);
    return shouldDispatch;
}

static void trackSocketIocpOperation(XDeviceNetworkContextWin32* p,
                                     XEventContext_IOCP* context)
{
    context->completionCallback = socketIocpOperationCompleted;
    context->completionUserData = p;
    InterlockedIncrement(&p->pendingIocpOperations);
}

static void abandonSocketIocpOperation(XDeviceNetworkContextWin32* p,
                                       XEventContext_IOCP* context)
{
    context->completionCallback = NULL;
    context->completionUserData = NULL;
    InterlockedDecrement(&p->pendingIocpOperations);
}

/* Keep public endpoint properties aligned with the POSIX backend after an
 * outbound connection completes. */
static void syncSocketEndpoints(XDeviceNetworkContext* priv)
{
    if (!priv || !priv->m_owner) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->socket == INVALID_SOCKET) return;

    XAbstractSocket* socket = (XAbstractSocket*)priv->m_owner;
    struct sockaddr_storage address;
    int addressLength = sizeof(address);
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
 * 私有数据管理
 * ========================================================================= */

XDeviceNetworkContext* XDeviceNetwork_createContext(void)
{
    XDeviceNetworkContextWin32* p = (XDeviceNetworkContextWin32*)XCalloc_System(1, sizeof(XDeviceNetworkContextWin32));
    if (!p) return NULL;

    p->socket = INVALID_SOCKET;
    p->namedPipe = INVALID_HANDLE_VALUE;
    p->acceptSocket = INVALID_SOCKET;
    p->base.m_base.m_fd = XFD_INVALID;
    p->autoRead = true;
    p->pendingIocpOperations = 1; /* XDeviceNetwork_deleteContext releases this owner reference. */

    XHostAddress_init(&p->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&p->pendingPeerAddr, XHostAddress_NullSpecial);

    return (XDeviceNetworkContext*)p;
}

void XDeviceNetwork_deleteContext(XDeviceNetworkContext* priv)
{
    if (!priv) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (InterlockedExchange(&p->deletePending, 1) != 0) return;

    priv->m_owner = NULL;
    if (p->acceptSocket != INVALID_SOCKET) {
        closesocket(p->acceptSocket);
        p->acceptSocket = INVALID_SOCKET;
    }

    /* OVERLAPPED and receive buffers are embedded in p.  Canceling or
     * closing a handle still queues one completion for each pending I/O, so
     * p must remain alive until processOneCompletion has consumed them. */
    releaseContext(p);
}

intptr_t XDeviceNetwork_socketDescriptor(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return -1;
    return W32(priv)->namedPipe != INVALID_HANDLE_VALUE
        ? (intptr_t)W32(priv)->namedPipe : (intptr_t)W32(priv)->socket;
}

/* =========================================================================
 * 异步读取启动
 * ========================================================================= */

static void startAsyncRead(XDeviceNetworkContext* priv, bool isUdp)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!p || p->readPending || !w32_has_stream_handle(p)) return;
    (void)isUdp;

    memset(&p->readContext, 0, sizeof(XEventContext_IOCP));
    p->readContext.base.type = XEventContextType_Type_Socket;
    p->readContext.base.fd = priv->m_base.m_fd;
    p->readContext.socket = XSocketDescriptor_fromIntptr(
        p->namedPipe != INVALID_HANDLE_VALUE ? (intptr_t)p->namedPipe : (intptr_t)p->socket);
    p->readContext.eventMask = FD_READ;
    trackSocketIocpOperation(p, &p->readContext);

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;
        if (ReadFile(p->namedPipe, p->readBuffer, XNETWORK_READ_BUFFER_SIZE,
                     &bytesTransferred, (OVERLAPPED*)&p->readContext)) {
            p->readContext.finishedBytes = bytesTransferred;
            p->readPending = true;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            p->readPending = true;
        } else {
            p->readPending = false;
            abandonSocketIocpOperation(p, &p->readContext);
        }
        return;
    }

    WSABUF buf;
    buf.buf = p->readBuffer;
    buf.len = XNETWORK_READ_BUFFER_SIZE;

    DWORD flags = 0;
    DWORD bytesTransferred = 0;
    int result;

    if (isUdp) {
        p->fromAddrLen = sizeof(p->fromAddr);
        result = WSARecvFrom(p->socket, &buf, 1, &bytesTransferred, &flags,
            (struct sockaddr*)&p->fromAddr, &p->fromAddrLen,
            (OVERLAPPED*)&p->readContext, NULL);
    }
    else {
        result = WSARecv(p->socket, &buf, 1, &bytesTransferred, &flags,
            (OVERLAPPED*)&p->readContext, NULL);
    }

    if (result == 0) {
        /* 立即完成：把字节数写入 readContext，dispatchCQEntry 才能读到正确值。
         * 之前传 NULL 给 bytesTransferred，processOneCompletion 时 bytes=0 被误判为 FIN。
         * bytesTransferred=0 仍表示 close（正常关闭），dispatchCQEntry 仍走 SockClose 路径。 */
        p->readContext.finishedBytes = bytesTransferred;
        p->readPending = true;
    }
    else if (WSAGetLastError() == WSA_IO_PENDING) {
        /* 异步等待 */
        p->readPending = true;
    }
    else {
        /* 错误 */
        p->readPending = false;
        abandonSocketIocpOperation(p, &p->readContext);
    }
}

/* =========================================================================
 * 异步写入启动
 * ========================================================================= */

static void startAsyncWrite(XDeviceNetworkContext* priv, const void* data, int64_t len,
    const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!p || p->writePending || !w32_has_stream_handle(p)) return;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return;
    (void)destAddr; (void)destPort; (void)isUdp;

    memcpy(p->writeBuffer, data, (size_t)len);

    memset(&p->writeContext, 0, sizeof(XEventContext_IOCP));
    p->writeContext.base.type = XEventContextType_Type_Socket;
    p->writeContext.base.fd = priv->m_base.m_fd;
    p->writeContext.socket = XSocketDescriptor_fromIntptr(
        p->namedPipe != INVALID_HANDLE_VALUE ? (intptr_t)p->namedPipe : (intptr_t)p->socket);
    p->writeContext.eventMask = FD_WRITE;
    p->writeContext.finishedBytes = len;
    trackSocketIocpOperation(p, &p->writeContext);

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;
        if (WriteFile(p->namedPipe, p->writeBuffer, (DWORD)len,
                      &bytesTransferred, (OVERLAPPED*)&p->writeContext)) {
            p->writeContext.finishedBytes = bytesTransferred;
            p->writePending = true;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            p->writePending = true;
        } else {
            p->writePending = false;
            abandonSocketIocpOperation(p, &p->writeContext);
        }
        return;
    }
    WSABUF buf;
    buf.buf = p->writeBuffer;
    buf.len = (ULONG)len;

    int result;

    if (isUdp && destAddr) {
        struct sockaddr_storage dest;
        int destLen;
        addr2sa(destAddr, destPort, &dest, &destLen);

        result = WSASendTo(p->socket, &buf, 1, NULL, 0,
            (struct sockaddr*)&dest, destLen,
            (OVERLAPPED*)&p->writeContext, NULL);
    }
    else {
        result = WSASend(p->socket, &buf, 1, NULL, 0,
            (OVERLAPPED*)&p->writeContext, NULL);
    }

    if (result == 0) {
        /* An overlapped socket associated with an IOCP queues its own
         * completion packet even when WSASend completes synchronously.
         * Posting another packet here delivers the same OVERLAPPED twice;
         * a later delivery can outlive the FTP data socket that owned it. */
        p->writePending = true;
    }
    else if (WSAGetLastError() == WSA_IO_PENDING) {
        p->writePending = true;
    }
    else {
        p->writePending = false;
        abandonSocketIocpOperation(p, &p->writeContext);
    }
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
    XDeviceNetworkContextWin32* p = W32(priv);

    XDeviceNetwork_ensureInit();
    int af = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    int type = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XDeviceNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;

    p->socket = socket(af, type, proto);
    if (p->socket == INVALID_SOCKET) return 0;

    u_long mode = 1;
    ioctlsocket(p->socket, FIONBIO, &mode);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }

    struct sockaddr_storage addrStorage;
    int addrLen;
    addr2sa(address, port, &addrStorage, &addrLen);

    if (bind(p->socket, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        return 0;
    }

    uint16_t actualPort = 0;
    struct sockaddr_storage boundAddr;
    int boundAddrLen = sizeof(boundAddr);
    if (getsockname(p->socket, (struct sockaddr*)&boundAddr, &boundAddrLen) == 0) {
        if (boundAddr.ss_family == AF_INET6) {
            actualPort = ntohs(((struct sockaddr_in6*)&boundAddr)->sin6_port);
        }
        else {
            actualPort = ntohs(((struct sockaddr_in*)&boundAddr)->sin_port);
        }
    }

    if (!iocp_assoc(p->socket, priv->m_owner)) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        return 0;
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
    XDeviceNetworkContextWin32* p = W32(priv);

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
    if (p->socket == INVALID_SOCKET) { freeaddrinfo(result); return false; }

    u_long mode = 1;
    ioctlsocket(p->socket, FIONBIO, &mode);

    struct sockaddr_storage localAddr = { 0 };
    int localLen = (ai->ai_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    ((struct sockaddr*)&localAddr)->sa_family = ai->ai_family;

    if (bind(p->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
        closesocket(p->socket); p->socket = INVALID_SOCKET; freeaddrinfo(result); return false;
    }

    if (!iocp_assoc(p->socket, priv->m_owner)) {
        closesocket(p->socket); p->socket = INVALID_SOCKET; freeaddrinfo(result); return false;
    }

    struct sockaddr_storage destAddr;
    int destLen;
    memset(&destAddr, 0, sizeof(destAddr));
    if (ai->ai_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&destAddr;
        s6->sin6_family = AF_INET6; s6->sin6_port = htons(port);
        memcpy(&s6->sin6_addr, &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr, sizeof(struct in6_addr));
        destLen = sizeof(struct sockaddr_in6);
    }
    else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&destAddr;
        s4->sin_family = AF_INET; s4->sin_port = htons(port);
        s4->sin_addr = ((struct sockaddr_in*)ai->ai_addr)->sin_addr;
        destLen = sizeof(struct sockaddr_in);
    }
    freeaddrinfo(result);

    sa2addr(&destAddr, &p->pendingPeerAddr, &p->pendingPeerPort);
    p->pendingPeerPort = port;

    if (sockType == XDeviceNetwork_Tcp && g_ConnectEx) {
        memset(&p->connectContext, 0, sizeof(XEventContext_IOCP));
        p->connectContext.base.type = XEventContextType_Type_Socket;
        p->connectContext.base.fd = priv->m_base.m_fd;
        p->connectContext.socket = XSocketDescriptor_fromIntptr(p->socket);
        p->connectContext.eventMask = FD_CONNECT;
        trackSocketIocpOperation(p, &p->connectContext);

        if (!g_ConnectEx(p->socket, (struct sockaddr*)&destAddr, destLen,
            NULL, 0, NULL, (OVERLAPPED*)&p->connectContext)) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                abandonSocketIocpOperation(p, &p->connectContext);
                closesocket(p->socket); p->socket = INVALID_SOCKET;
                return false;
            }
        }
        p->connectPending = true;
        return true;
    }

    if (sockType == XDeviceNetwork_Udp) {
        p->base.m_connected = true;
        p->connectPending = false;
        startAsyncRead(priv, true);
        return true;
    }
    return true;
}

bool XDeviceNetwork_socketConnectLocal(XFd xfd, const XString* pipePath,
                                 XDeviceNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32* p;
    const char* pathText;
    char fullPath[512];
    HANDLE handle;
    DWORD mode = PIPE_READMODE_BYTE;

    if (!priv || !pipePath || streamType != XDeviceNetwork_LocalStream_NamedPipe
        || sockType != XDeviceNetwork_Tcp) return false;
    pathText = XString_toUtf8(pipePath);
    if (!pathText || !pathText[0]) return false;
    if (strncmp(pathText, "\\\\.\\pipe\\", 9) == 0) {
        if (snprintf(fullPath, sizeof(fullPath), "%s", pathText) >= (int)sizeof(fullPath))
            return false;
    } else if (snprintf(fullPath, sizeof(fullPath), "\\\\.\\pipe\\%s", pathText)
               >= (int)sizeof(fullPath)) {
        return false;
    }

    p = W32(priv);
    XDeviceNetwork_socketDisconnect(xfd);
    handle = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
        if (!WaitNamedPipeA(fullPath, timeoutMs < 0 ? NMPWAIT_WAIT_FOREVER
                                                    : (DWORD)timeoutMs)) return false;
        handle = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    }
    if (handle == INVALID_HANDLE_VALUE) return false;
    if (!SetNamedPipeHandleState(handle, &mode, NULL, NULL)
        || !iocp_assoc(handle, (XObject*)priv->m_owner)) {
        CloseHandle(handle);
        return false;
    }

    p->namedPipe = handle;
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
    XDeviceNetworkContextWin32* p = W32(priv);

    if (p->socket != INVALID_SOCKET) {
        CancelIoEx((HANDLE)p->socket, NULL);
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
    }
    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(p->namedPipe, NULL);
        CloseHandle(p->namedPipe);
        p->namedPipe = INVALID_HANDLE_VALUE;
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
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!w32_has_stream_handle(p)) return -1;

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t available = XRingBuffer_available(rb);
        if (available > 0) {
            size_t toRead = (len < (int64_t)available) ? (size_t)len : available;
            return XRingBuffer_read(rb, buf, toRead);
        }
    }
    return 0;
}

int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, const XHostAddress* destAddr,
    uint16_t destPort, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!w32_has_stream_handle(p)) return -1;

    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t pending = XRingBuffer_available(rb);
        if (pending > 0 && !p->writePending) {
            char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
            size_t toSend = XRingBuffer_read(rb, tempBuf, XNETWORK_WRITE_BUFFER_SIZE);
            if (toSend > 0) {
                startAsyncWrite(priv, tempBuf, toSend, destAddr, destPort, sockType == XDeviceNetwork_Udp);
            }
        }
    }

    if (!p->writePending) {
        if (len > XNETWORK_WRITE_BUFFER_SIZE) {
            if (ringBuffer) {
                struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
                XRingBuffer_write(rb, buf, len);
                {
                    char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
                    size_t toSend = XRingBuffer_read(rb, tempBuf,
                                                     XNETWORK_WRITE_BUFFER_SIZE);
                    if (toSend > 0) {
                        startAsyncWrite(priv, tempBuf, toSend,
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
        return XRingBuffer_write(rb, buf, len);
    }
    return -1;
}



bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !event) return false;
    XDeviceNetworkContextWin32* p = W32(priv);

    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;

    if (sockAct->actType & XSocketAct_Read) {
        p->readPending = false;
        return p->readContext.finishedBytes > 0;
    }
    if (sockAct->actType & XSocketAct_Write) {
        p->writePending = false;
        return true;
    }
    if (sockAct->actType & XSocketAct_Connect) {
        p->connectPending = false;
        /* 检查连接是否真正成功（ConnectEx 失败时 SO_ERROR 非零） */
        int soError = 0;
        int soLen = sizeof(soError);
        getsockopt(p->socket, SOL_SOCKET, SO_ERROR, (char*)&soError, &soLen);
        if (soError == 0) {
            if (setsockopt(p->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0) {
                p->base.m_connected = true;
                syncSocketEndpoints(priv);
            } else {
                p->base.m_connected = false;
            }
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
    if (!priv || fd == -1) return false;
    (void)openMode;
    XDeviceNetworkContextWin32* p = W32(priv);

    p->socket = (SOCKET)fd;
    if (!iocp_assoc(p->socket, priv->m_owner)) { p->socket = INVALID_SOCKET; return false; }
    p->base.m_connected = (state == 3);
    if (p->base.m_connected) { p->autoRead = true; startAsyncRead(priv, false); }
    return true;
}

bool XDeviceNetwork_serverSetDescriptor(XFd deviceFd, intptr_t fd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextWin32* p;

    if (!priv || fd == -1) return false;
    p = W32(priv);
    if (p->socket != INVALID_SOCKET || p->isServer) return false;
    if (!iocp_assoc((SOCKET)fd, priv->m_owner)) return false;

    p->socket = (SOCKET)fd;
    p->isServer = true;
    p->base.m_connected = true;
    return true;
}

bool XDeviceNetwork_socketSetOption(XFd xfd, int option, const void* value)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !value) return false;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->socket == INVALID_SOCKET) return false;
    int* intVal = (int*)value;
    switch (option) {
    case 0: return setsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, (char*)intVal, sizeof(int)) == 0;
    case 1: return setsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, (char*)intVal, sizeof(int)) == 0;
    case 2: return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_SetTtl, intVal) == 0;
    case 3: { bool enabled = (*intVal != 0); return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_SetLoop, &enabled) == 0; }
    case 4: return setsockopt(p->socket, IPPROTO_IP, IP_TOS, (char*)intVal, sizeof(int)) == 0;
    case 5: return setsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, (char*)intVal, sizeof(int)) == 0;
    case 6: return setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)intVal, sizeof(int)) == 0;
    case 8: return setsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, (char*)intVal, sizeof(int)) == 0;
    default: return false;
    }
}

void* XDeviceNetwork_socketGetOption(XFd xfd, int option)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return NULL;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->socket == INVALID_SOCKET) return NULL;
    static int result; int optLen = sizeof(int);
    switch (option) {
    case 0: if (getsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&result, &optLen) == 0) return &result; break;
    case 1: if (getsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&result, &optLen) == 0) return &result; break;
    case 2: if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_GetTtl, &result) == 0) return &result; break;
    case 3: { bool e = false; if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_GetLoop, &e) == 0) { result = e ? 1 : 0; return &result; } break; }
    case 4: if (getsockopt(p->socket, IPPROTO_IP, IP_TOS, (char*)&result, &optLen) == 0) return &result; break;
    case 5: if (getsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, (char*)&result, &optLen) == 0) return &result; break;
    case 6: if (getsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)&result, &optLen) == 0) return &result; break;
    case 7: if (getsockopt(p->socket, IPPROTO_IP, IP_MTU, (char*)&result, &optLen) == 0) return &result; break;
    case 8: if (getsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, (char*)&result, &optLen) == 0) return &result; break;
    default: break;
    }
    return NULL;
}

void XDeviceNetwork_socketSetReadBufferSize(XFd xfd, int64_t size)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->socket == INVALID_SOCKET || size <= 0) return;
    int bufSize = (int)((size > INT_MAX) ? INT_MAX : size);
    setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize));
}

/* =========================================================================
 * 异步读取状态
 * ========================================================================= */

const char* XDeviceNetwork_socketReadBuffer(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32(priv)->readBuffer : NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32(priv)->readContext.finishedBytes : 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32(priv)->writeContext.finishedBytes : 0;
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32(priv)->writePending : false;
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    p->readPending = false;
    if (p->autoRead && p->base.m_connected) startAsyncRead(priv, isUdp);
}

/* =========================================================================
 * 异步 Accept（公开 API：启动首次异步接受）
 * ========================================================================= */

bool XDeviceNetwork_serverAccept(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!p || p->socket == INVALID_SOCKET || !g_AcceptEx) return false;
    if (p->acceptPending) return true;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);
    if (getsockname(p->socket, (struct sockaddr*)&addr, &addrLen) == SOCKET_ERROR) return false;
    int af = addr.ss_family;

    SOCKET acceptSock = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (acceptSock == INVALID_SOCKET) return false;

    p->acceptSocket = acceptSock;

    memset(&p->acceptContext, 0, sizeof(XEventContext_IOCP));
    p->acceptContext.base.type = XEventContextType_Type_Socket;
    p->acceptContext.base.fd = priv->m_base.m_fd;
    p->acceptContext.socket = XSocketDescriptor_fromIntptr(p->socket);
    p->acceptContext.eventMask = FD_ACCEPT;
    trackSocketIocpOperation(p, &p->acceptContext);

    DWORD bytesReceived = 0;
    BOOL result = g_AcceptEx(p->socket, acceptSock, p->acceptBuffer, 0,
        sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16,
        &bytesReceived, (OVERLAPPED*)&p->acceptContext);

    if (result) { p->acceptPending = true; return true; }
    if (WSAGetLastError() == WSA_IO_PENDING) { p->acceptPending = true; return true; }

    closesocket(acceptSock);
    p->acceptSocket = INVALID_SOCKET;
    abandonSocketIocpOperation(p, &p->acceptContext);
    return false;
}

/* =========================================================================
 * TCP 服务器
 * ========================================================================= */

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd deviceFd, const XHostAddress* addr, uint16_t port,
    int backlog, bool reuseAddr)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    if (!priv) return -1;
    XDeviceNetworkContextWin32* p = W32(priv);
    XDeviceNetwork_ensureInit();

    int af = (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;

    SOCKET s = WSASocket(af, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) return -1;

    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }

    if (af == AF_INET6) {
        int ipv6only = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only));
    }

    struct sockaddr_storage addrStorage;
    int addrLen;
    addr2sa(addr, port, &addrStorage, &addrLen);

    if (bind(s, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    if (listen(s, backlog > 0 ? backlog : SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    if (!iocp_assoc(s, priv->m_owner)) {
        closesocket(s);
        return -1;
    }
    p->socket = s;
    p->isServer = true;
    p->base.m_connected = true;

    {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        if (ring)
            XAbstractNetIoRing_registerEvent_base(ring, priv->m_base.m_fd);
    }

    return (XDeviceNetworkServerHandle)s;
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server)
{
    if (server == -1) return 0;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    if (getsockname((SOCKET)server, (struct sockaddr*)&addr, &addrLen) == 0) {
        if (addr.ss_family == AF_INET6) {
            return ntohs(((struct sockaddr_in6*)&addr)->sin6_port);
        }
        else {
            return ntohs(((struct sockaddr_in*)&addr)->sin_port);
        }
    }
    return 0;
}

void XDeviceNetwork_serverClose(XFd xfd, XDeviceNetworkServerHandle server)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    SOCKET socketToClose = (SOCKET)server;
    XDeviceNetworkContextWin32* p = priv ? W32(priv) : NULL;
    if (server == -1) return;

    if (p && p->socket == socketToClose) {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        if (ring)
            XAbstractNetIoRing_unregisterEvent_base(ring, xfd);
        CancelIoEx((HANDLE)p->socket, NULL);
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        p->base.m_connected = false;
        if (p->acceptSocket != INVALID_SOCKET) {
            closesocket(p->acceptSocket);
            p->acceptSocket = INVALID_SOCKET;
        }
    } else {
        /* Used when XTcpServer rejects an already accepted client handle. */
        closesocket(socketToClose);
    }
}

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd xfd,
    XHostAddress* clientAddr, uint16_t* clientPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return -1;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->acceptSocket == INVALID_SOCKET) return -1;

    SOCKET listenSocket = p->socket;
    if (setsockopt(p->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (char*)&listenSocket, sizeof(listenSocket)) != 0) {
        closesocket(p->acceptSocket);
        p->acceptSocket = INVALID_SOCKET;
        p->acceptPending = false;
        return -1;
    }

    if (clientAddr || clientPort) {
        if (g_GetAcceptExSockaddrs) {
            struct sockaddr* localAddr = NULL;
            struct sockaddr* remoteAddr = NULL;
            int localLen = 0, remoteLen = 0;

            g_GetAcceptExSockaddrs(p->acceptBuffer, 0,
                sizeof(struct sockaddr_in6) + 16, sizeof(struct sockaddr_in6) + 16,
                &localAddr, &localLen, &remoteAddr, &remoteLen);

            if (remoteAddr && remoteLen > 0) {
                sa2addr((struct sockaddr_storage*)remoteAddr, clientAddr, clientPort);
            }
        }
    }

    u_long mode = 1;
    ioctlsocket(p->acceptSocket, FIONBIO, &mode);

    SOCKET result = p->acceptSocket;
    p->acceptSocket = INVALID_SOCKET;
    p->acceptPending = false;
    return (XDeviceNetworkSocketHandle)result;
}

/* =========================================================================
 * DNS 查询
 * ========================================================================= */

XVector* XDeviceNetwork_lookupName(const XString* name)
{
    if (!name) return NULL;
    const char* nameStr = XString_toUtf8(name);
    if (!nameStr) return NULL;

    XDeviceNetwork_ensureInit();

    struct addrinfo hints = { 0 }, * result = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(nameStr, NULL, &hints, &result) != 0) {
        return NULL;
    }

    XVector* vec = XVector_create(sizeof(XHostAddress));
    if (!vec) {
        freeaddrinfo(result);
        return NULL;
    }
    XContainerSetDataMoveMethod(vec, XClass_move_base);
    XContainerSetDataCopyMethod(vec, XClass_copy_base);
    XContainerSetDataDeinitMethod(vec, XHostAddress_deinit_base);
    /* 遍历解析结果，填充地址向量 */
    struct addrinfo* p = result;
    while (p) {
        XHostAddress addr;
        XHostAddress_init(&addr);
        sa2addr((struct sockaddr_storage*)p->ai_addr, &addr, NULL);
        XVector_push_back_1_base(vec, &addr);
        p = p->ai_next;
    }

    freeaddrinfo(result);

    if (XVector_size_base(vec) == 0) {
        XVector_delete_base(vec);
        return NULL;
    }

    return vec;
}

XString* XDeviceNetwork_localHostName(void)
{
    char buf[256] = { 0 };
    if (gethostname(buf, sizeof(buf)) == 0) {
        return XString_create_utf8(buf);
    }
    return NULL;
}

/* =========================================================================
 * 网络接口枚举
 * ========================================================================= */

XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void)
{
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;
    ULONG size = 0;

    /* 获取所需大小 */
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &size);

    adapterAddresses = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
    if (!adapterAddresses) return NULL;

    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL,
        adapterAddresses, &size) != ERROR_SUCCESS) {
        XFree_System(adapterAddresses);
        return NULL;
    }

    return (XDeviceNetworkInterfaceIterator)adapterAddresses;
}

XNetworkInterface* XDeviceNetwork_enumInterfacesNext(XDeviceNetworkInterfaceIterator iter)
{
    if (!iter) return NULL;

    if (current == NULL) {
        current = (PIP_ADAPTER_ADDRESSES)iter;
    }
    else {
        current = current->Next;
    }

    if (current == NULL) return NULL;

    /* 创建 XNetworkInterface 对象 */
    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;

    /* 名称 - AdapterName 是 ASCII/ANSI 编码（网卡 GUID），使用 GBK 转换 */
    //char nameBuf[1024] = {0};
    //WideCharToMultiByte(CP_ACP, 0, current->AdapterName, -1, nameBuf, sizeof(nameBuf), NULL, NULL);
   //printf("%s\n",nameBuf);
    if (iface->name)
        XString_assign(iface->name, current->AdapterName);
    else
        iface->name = XString_create_utf8(current->AdapterName);
    iface->humanReadableName = XString_create_utf16(current->FriendlyName);

    iface->index = (int)current->IfIndex;
    iface->mtu = (int)current->Mtu;

    /* 硬件地址 (MAC) */
    if (current->PhysicalAddressLength > 0 && current->PhysicalAddressLength <= 32) {
        char macStr[64];
        int pos = 0;
        for (ULONG i = 0; i < current->PhysicalAddressLength; i++) {
            pos += sprintf(macStr + pos, "%02X:", current->PhysicalAddress[i]);
        }
        if (pos > 0) macStr[pos - 1] = '\0'; /* 移除最后的冒号 */
        if (iface->hardwareAddress)
            XString_assign_fmt_utf8(iface->hardwareAddress, macStr);
        else
            iface->hardwareAddress = XString_create_fmt_utf8(macStr);
        //iface->hardwareAddress = XString_create_fmt_utf8(macStr);
    }

    /* 标志 */
    if (current->OperStatus == IfOperStatusUp) {
        iface->flags |= XNetworkInterface_IsUp | XNetworkInterface_IsRunning;
    }
    if (current->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        iface->flags |= XNetworkInterface_IsLoopBack;
    }
    if (current->Flags & IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED) {
        iface->flags |= XNetworkInterface_CanMulticast;
    }
    if (current->IfType == IF_TYPE_ETHERNET_CSMACD || current->IfType == IF_TYPE_IEEE80211) {
        iface->type = XNetworkInterface_Ethernet;
        iface->flags |= XNetworkInterface_CanBroadcast | XNetworkInterface_CanMulticast;
    }
    else if (current->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        iface->type = XNetworkInterface_Loopback;
    }

    iface->isValid = true;

    /* 获取 IP 地址条目 */
    PIP_ADAPTER_UNICAST_ADDRESS ua = current->FirstUnicastAddress;
    while (ua) {
        XNetworkAddressEntry entry;
        XNetworkAddressEntry_init(&entry);

        XHostAddress addr;
        XHostAddress_init(&addr);
        sa2addr((struct sockaddr_storage*)ua->Address.lpSockaddr, &addr, NULL);
        XNetworkAddressEntry_setIp(&entry, &addr);

        /* 计算子网掩码 */
        uint8_t prefixLen = ua->OnLinkPrefixLength;
        if (ua->Address.lpSockaddr->sa_family == AF_INET) {
            uint32_t maskVal = prefixLen >= 32 ? 0xFFFFFFFF : htonl(0xFFFFFFFF << (32 - prefixLen));
            XHostAddress mask;
            XHostAddress_init(&mask);
            XHostAddress_setAddressIPv4(&mask, maskVal);
            XNetworkAddressEntry_setNetmask(&entry, &mask);
            XHostAddress_deinit_base(&mask);
        }
        else {
            uint8_t maskBytes[16] = { 0 };
            uint8_t tempPrefix = prefixLen;
            for (int j = 0; j < 16 && tempPrefix > 0; j++) {
                if (tempPrefix >= 8) {
                    maskBytes[j] = 0xFF;
                    tempPrefix -= 8;
                }
                else {
                    maskBytes[j] = (uint8_t)(0xFF << (8 - tempPrefix));
                    tempPrefix = 0;
                }
            }
            XHostAddress mask;
            XHostAddress_init(&mask);
            XHostAddress_setAddressIPv6(&mask, maskBytes);
            XNetworkAddressEntry_setNetmask(&entry, &mask);
            XHostAddress_deinit_base(&mask);
        }

        XVector_push_back_move_1_base(iface->addressEntries, &entry);
        XNetworkAddressEntry_deinit_base(&entry);
        XHostAddress_deinit_base(&addr);

        ua = ua->Next;
    }

    return iface;
}

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter)
{
    if (iter) {
        XFree_System(iter);
    }
    /* 重置静态变量 */
    //extern PIP_ADAPTER_ADDRESSES current;
    current = NULL;
}

/* =========================================================================
 * 多播组（精简为2个API）
 * ========================================================================= */

bool XDeviceNetwork_multicastGroup(XDeviceNetworkSocketHandle sock, bool join,
    const XHostAddress* groupAddress, uint32_t ifIndex)
{
    if (sock == -1 || !groupAddress) return false;

    if (XHostAddress_protocol(groupAddress) == XHostAddress_IPv6Protocol) {
        struct ipv6_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        XHostAddress_toIPv6Address(groupAddress, &mreq.ipv6mr_multiaddr);
        mreq.ipv6mr_interface = ifIndex;
        int opt = join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP;
        return setsockopt((SOCKET)sock, IPPROTO_IPV6, opt,
            (char*)&mreq, sizeof(mreq)) == 0;
    }
    else {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = htonl(XHostAddress_toIPv4Address(groupAddress));
        mreq.imr_interface.s_addr = htonl(ifIndex);
        int opt = join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
        return setsockopt((SOCKET)sock, IPPROTO_IP, opt,
            (char*)&mreq, sizeof(mreq)) == 0;
    }
}

int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle sock, XMulticastOp op, void* arg)
{
    if (sock == -1 || !arg) return -1;

    switch (op) {
    case XMC_SetIf: {
        uint32_t ifIndex = *(uint32_t*)arg;
        struct in_addr addr;
        addr.s_addr = htonl(ifIndex);
        return setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_IF,
            (char*)&addr, sizeof(addr)) == 0 ? 0 : -1;
    }
    case XMC_GetIf: {
        struct in_addr addr;
        int len = sizeof(addr);
        if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_IF,
            (char*)&addr, &len) == 0) {
            *(uint32_t*)arg = ntohl(addr.s_addr);
            return 0;
        }
        return -1;
    }
    case XMC_SetTtl: {
        int ttl = *(int*)arg;
        /* IPv4 */
        if (setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_TTL,
            (char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
            return -1;
        }
        /* IPv6 */
        setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
            (char*)&ttl, sizeof(ttl));
        return 0;
    }
    case XMC_GetTtl: {
        int ttl = 0;
        int len = sizeof(ttl);
        if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_TTL,
            (char*)&ttl, &len) == 0) {
            *(int*)arg = ttl;
            return 0;
        }
        return -1;
    }
    case XMC_SetLoop: {
        int loop = *(bool*)arg ? 1 : 0;
        /* IPv4 */
        if (setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_LOOP,
            (char*)&loop, sizeof(loop)) == SOCKET_ERROR) {
            return -1;
        }
        /* IPv6 */
        setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
            (char*)&loop, sizeof(loop));
        return 0;
    }
    case XMC_GetLoop: {
        int loop = 0;
        int len = sizeof(loop);
        if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_LOOP,
            (char*)&loop, &len) == 0) {
            *(bool*)arg = (loop != 0);
            return 0;
        }
        return -1;
    }
    default:
        return -1;
    }
}

/* =========================================================================
 * UDP 特有
 * ========================================================================= */

 /* XDeviceNetwork_hasPendingDatagrams 和 XDeviceNetwork_pendingDatagramSize 已移除
  * 原因：在 IOCP 异步模型中，数据已被读取到 ringBuffer，
  * 请使用 XIODevice_bytesAvailable 检查待读取数据 */

bool XDeviceNetwork_platformGetLastDatagramSender(XFd xfd,
    XHostAddress* srcAddr, uint16_t* srcPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return false;

    /* 从 wp->fromAddr 获取发送者信息 */
    const XDeviceNetworkContextWin32* wp = (const XDeviceNetworkContextWin32*)priv;

    if (wp->fromAddrLen <= 0) {
        return false;
    }

    if (srcAddr || srcPort) {
        sa2addr((const struct sockaddr_storage*)&wp->fromAddr, srcAddr, srcPort);
    }

    return true;
}

/* =========================================================================
 * 代理隧道（已移至应用层实现）
 * ========================================================================= */
 /* 注意：XDeviceNetwork_socks5Connect、XDeviceNetwork_httpConnect、XDeviceNetwork_socks5Bind、
  * XDeviceNetwork_serverCreateWithProxy、XDeviceNetwork_serverAcceptWithProxy
  * 这些代理相关函数已从平台层移除，请在 XNetworkProxy 模块或应用层实现 */

  /* =========================================================================
   * 系统代理获取（Windows）
   * ========================================================================= */

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

bool XDeviceNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy)
{
    if (!outProxy) return false;

    XNetworkProxy_init(outProxy);

    /* 1. 尝试使用 WinHttpGetProxyForUrl (WPAD) */
    HINTERNET hSession = WinHttpOpen(L"XinYueC/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (hSession) {
        WINHTTP_PROXY_INFO proxyInfo = { 0 };
        WCHAR wideUrl[1024] = { 0 };

        if (queryUrl) {
            const char* urlStr = XString_toUtf8(queryUrl);
            if (urlStr) MultiByteToWideChar(CP_UTF8, 0, urlStr, -1, wideUrl, 1024);
        }

        /* 获取 IE 代理设置 */
        if (WinHttpGetDefaultProxyConfiguration(&proxyInfo)) {
            if (proxyInfo.lpszProxy && wcslen(proxyInfo.lpszProxy) > 0) {
                /* 解析代理字符串 "host:port" */
                char proxyHost[256] = { 0 };
                WideCharToMultiByte(CP_UTF8, 0, proxyInfo.lpszProxy, -1,
                    proxyHost, sizeof(proxyHost), NULL, NULL);

                char* colon = strchr(proxyHost, ':');
                if (colon) {
                    *colon = '\0';
                    XString* host = XString_create_utf8(proxyHost);
                    XNetworkProxy_setHostName(outProxy, host);
                    XNetworkProxy_setPort(outProxy, (uint16_t)atoi(colon + 1));
                    XNetworkProxy_setType(outProxy, XNetworkProxy_HttpProxy);
                    XString_delete_base(host);
                }
            }

            if (proxyInfo.lpszProxyBypass) {
                GlobalFree(proxyInfo.lpszProxyBypass);
            }
            if (proxyInfo.lpszProxy) {
                GlobalFree(proxyInfo.lpszProxy);
            }
        }

        WinHttpCloseHandle(hSession);

        if (XNetworkProxy_port(outProxy) > 0) {
            return true;
        }
    }

    /* 2. 回退到注册表读取 */
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD proxyEnable = 0;
        DWORD size = sizeof(DWORD);

        if (RegQueryValueExA(hKey, "ProxyEnable", NULL, NULL,
            (LPBYTE)&proxyEnable, &size) == ERROR_SUCCESS && proxyEnable) {

            char proxyServer[256] = { 0 };
            size = sizeof(proxyServer);

            if (RegQueryValueExA(hKey, "ProxyServer", NULL, NULL,
                (LPBYTE)proxyServer, &size) == ERROR_SUCCESS) {
                /* 解析 "protocol=host:port" 或 "host:port" 格式 */
                char* eq = strchr(proxyServer, '=');
                char* proxyStr = eq ? (eq + 1) : proxyServer;

                char* colon = strchr(proxyStr, ':');
                if (colon) {
                    *colon = '\0';
                    XString* host = XString_create_utf8(proxyStr);
                    XNetworkProxy_setHostName(outProxy, host);
                    XNetworkProxy_setPort(outProxy, (uint16_t)atoi(colon + 1));
                    XNetworkProxy_setType(outProxy, XNetworkProxy_HttpProxy);
                    XString_delete_base(host);
                }
            }
        }

        RegCloseKey(hKey);
    }

    return XNetworkProxy_port(outProxy) > 0;
}

/* =========================================================================
 * GSSAPI 认证（Windows SSPI）
 * ========================================================================= */

#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>

 /* GSSAPI 上下文结构 */
typedef struct {
    CredHandle credHandle;
    CtxtHandle ctxtHandle;
    bool hasCred;
    bool hasCtxt;
    char* targetName;
} WinGssapiContext;

int XDeviceNetwork_gssapiAuth(const XString* serviceName,
    const XByteArray* inputToken,
    XByteArray* outputToken,
    void** context)
{
    if (!outputToken) return -1;

    WinGssapiContext* ctx = (WinGssapiContext*)*context;

    /* 首次调用，初始化上下文 */
    if (!ctx) {
        ctx = (WinGssapiContext*)XMalloc_System(sizeof(WinGssapiContext));
        if (!ctx) return -1;

        memset(ctx, 0, sizeof(WinGssapiContext));
        SecInvalidateHandle(&ctx->credHandle);
        SecInvalidateHandle(&ctx->ctxtHandle);

        /* 获取默认凭据 */
        TimeStamp expiry;
        SECURITY_STATUS status = AcquireCredentialsHandleA(
            NULL, "Negotiate", SECPKG_CRED_OUTBOUND,
            NULL, NULL, NULL, NULL, &ctx->credHandle, &expiry);

        if (status != SEC_E_OK) {
            XFree_System(ctx);
            return -1;
        }
        ctx->hasCred = true;

        if (serviceName) {
            const char* svcStr = XString_toUtf8(serviceName);
            if (svcStr) ctx->targetName = _strdup(svcStr);
        }

        *context = ctx;
    }

    /* 准备输入缓冲区 */
    SecBufferDesc inDesc = { 0 };
    SecBuffer inBuf = { 0 };
    if (inputToken && XByteArray_size_base(inputToken) > 0) {
        inDesc.ulVersion = SECBUFFER_VERSION;
        inDesc.cBuffers = 1;
        inDesc.pBuffers = &inBuf;
        inBuf.BufferType = SECBUFFER_TOKEN;
        inBuf.cbBuffer = (unsigned long)XByteArray_size_base(inputToken);
        inBuf.pvBuffer = (void*)XByteArray_data(inputToken);
    }

    /* 准备输出缓冲区 */
    SecBufferDesc outDesc = { 0 };
    SecBuffer outBuf = { 0 };
    BYTE outTokenBuf[4096];

    outDesc.ulVersion = SECBUFFER_VERSION;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = &outBuf;
    outBuf.BufferType = SECBUFFER_TOKEN;
    outBuf.cbBuffer = sizeof(outTokenBuf);
    outBuf.pvBuffer = outTokenBuf;

    /* 初始化安全上下文 */
    ULONG attr = 0;
    TimeStamp expiry;
    SECURITY_STATUS status;

    if (inputToken && XByteArray_size_base(inputToken) > 0) {
        status = InitializeSecurityContextA(
            &ctx->credHandle, &ctx->ctxtHandle,
            ctx->targetName, ISC_REQ_CONNECTION,
            0, SECURITY_NETWORK_DREP,
            &inDesc, 0, &ctx->ctxtHandle,
            &outDesc, &attr, &expiry);
    }
    else {
        status = InitializeSecurityContextA(
            &ctx->credHandle, NULL,
            ctx->targetName, ISC_REQ_CONNECTION,
            0, SECURITY_NETWORK_DREP,
            NULL, 0, &ctx->ctxtHandle,
            &outDesc, &attr, &expiry);
    }

    if (status == SEC_E_OK) {
        /* 认证完成 */
        if (outBuf.cbBuffer > 0) {
            XByteArray_resize_base(outputToken, outBuf.cbBuffer);
            memcpy(XByteArray_data(outputToken), outTokenBuf, outBuf.cbBuffer);
        }
        return 0;
    }
    else if (status == SEC_I_CONTINUE_NEEDED) {
        /* 需要继续 */
        if (outBuf.cbBuffer > 0) {
            XByteArray_resize_base(outputToken, outBuf.cbBuffer);
            memcpy(XByteArray_data(outputToken), outTokenBuf, outBuf.cbBuffer);
        }
        ctx->hasCtxt = true;
        return 1;
    }

    /* 失败 */
    return -1;
}
void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !ringBuffer) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (p->writePending || !w32_has_stream_handle(p)) return;
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
#endif /* XNETWORK_USE_PLATFORM_API */
