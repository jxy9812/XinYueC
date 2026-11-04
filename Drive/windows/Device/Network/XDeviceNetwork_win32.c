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
#include "XStringUtils.h"
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
#include "XDateTime.h"     /* W32_nowMs：写槽看门狗时间戳（单调毫秒）。 */

/* ====== Windows SDK 头文件 ====== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

#ifndef GAA_FLAG_INCLUDE_GATEWAYS
#define GAA_FLAG_INCLUDE_GATEWAYS 0x0040 /* 旧 SDK 兜底：GAA 默认不返回网关。 */
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
 *
 * [2026-11-04 后端可切换] 本段及 IOCP 辅助函数只在
 * `XNETWORK_WIN_BACKEND == XNETWORK_WIN_BACKEND_IOCP` 时参与编译；
 * WSAEventSelect 后端（XDeviceNetwork_win32_select.c）不使用任何
 * Overlapped 扩展函数——这正是它不受「完成包丢失」影响的原因。
 * ========================================================================= */

#if XNETWORK_WIN_SOCKET_IOCP

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

#endif /* XNETWORK_WIN_SOCKET_IOCP —— ConnectEx/AcceptEx 扩展函数指针段结束 */

/* =========================================================================
 * 内部宏定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192
#define XNETWORK_WRITE_BUFFER_SIZE 8192

 /* =========================================================================
  * 全局状态
  * ========================================================================= */

static LONG    g_wsaRefCount = 0;
#if XNETWORK_WIN_SOCKET_IOCP
static HANDLE  g_iocp = NULL;
static LPFN_CONNECTEX g_ConnectEx = NULL;
static LPFN_ACCEPTEX g_AcceptEx = NULL;
static LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = NULL;
#endif /* XNETWORK_WIN_SOCKET_IOCP */

/* =========================================================================
 * IOCP 辅助函数
 * ========================================================================= */

#if XNETWORK_WIN_SOCKET_IOCP

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
    //XPrintf("?:%p\n",key);
    return CreateIoCompletionPort(handle, h, (ULONG_PTR)key, 0) != NULL;
}

#endif /* XNETWORK_WIN_SOCKET_IOCP */

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

#if XNETWORK_WIN_SOCKET_IOCP
        /* 获取扩展函数指针（仅 IOCP 后端需要 Overlapped 扩展函数） */
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
#endif /* XNETWORK_WIN_SOCKET_IOCP */
    }
}

void XDeviceNetwork_cleanup(void)
{
    if (InterlockedDecrement(&g_wsaRefCount) == 0) {
        WSACleanup();
#if XNETWORK_WIN_SOCKET_IOCP
        g_iocp = NULL;
        g_ConnectEx = NULL;
#endif
    }
}

void XDeviceNetwork_poll(void)
{
}

/* 公共层平台错误钩子：Win32 域即 WSA 错误码。 */
int XDeviceNetwork_platformLastError(void)
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
 *
 * [2026-11-04 后端可切换] 从本段起直到本文件末尾的
 * `XDeviceNetwork_socket* / server* / platformGetLastDatagramSender`
 * 全部是 **IOCP 重叠 I/O 专属实现**，整段受 XNETWORK_WIN_SOCKET_IOCP 门控。
 * `XNETWORK_WIN_SOCKET_WSAEVENT` 构建下这些符号由
 * `XDeviceNetwork_win32_select.c` 提供，两者编译期互斥、绝不重复定义。
 * 平台无关部分（ICMP / 初始化 / 错误 / 网卡计数 / DNS / 网卡枚举与配置 /
 * 多播 / 系统代理 / GSSAPI）留在开关之外，两套后端共用同一份。
 * ========================================================================= */

#if XNETWORK_WIN_SOCKET_IOCP

/**
 * @brief 单个异步写槽（一次在途 WSASend 独占一份 OVERLAPPED + 缓冲）。
 *
 * @details [2026-11-04 独立写槽根修] 原实现整个连接只有一份
 *          `writeContext` + `writeBuffer`，且用布尔 `writePending` 互斥。
 *          这违反 MSDN 对 OVERLAPPED 的硬性约束：
 *
 *            “A common mistake is to reuse an OVERLAPPED structure before
 *             the previous asynchronous operation has been completed. You
 *             should use a separate structure for each request.”
 *            —— https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped
 *
 *          同一份 WSASend 还要求缓冲在内核持有期间不得改动：
 *
 *            “once the WSASend function is called, the system owns these
 *             buffers and the application may not access them.”
 *            —— https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend
 *
 *          旧写法靠 `writePending` 保证互斥，一旦完成包丢失（实测发生：
 *          客户端第 8 次提交的完成包永不返回；服务端 371 提交 / 340 完成），
 *          `writePending` 永久为真 ⇒ 之后所有写都沉进 ring buffer 且
 *          `ContinueWrite` 也因同一标志被挡 ⇒ 应用层"写成功"而对端零到达。
 *
 *          改为槽池后：多个写可同时在途，各自独立 OVERLAPPED/缓冲；
 *          单个完成包丢失只损失一个槽，其余写照常推进。
 */
#define XNETWORK_WRITE_SLOT_COUNT 4
#define XNETWORK_WRITE_SLOT_TIMEOUT_MS 2000u  /**< 槽位超时阈值（完成包丢失兜底）。 */
/** @brief 读请求挂死阈值（毫秒）。
 *
 * @details 读请求提交后超过该时长仍无任何完成，即判为"完成包未投递"并用
 *          CancelIoEx 取消重挂。取值考虑：正常空闲链路也会长时间无数据
 *          （HSF 心跳 1s、遥测 100ms，但安静时可能数十秒无入站），所以阈值
 *          不能太小；这里取 3s——比静默判线阈值（6s）小一半，保证看门狗
 *          能在"链路被判死"之前先尝试自救一次。
 * @note   只对**已挂起且超时**的读生效，空闲链路上一次成功的读会刷新
 *         readArmedMs（由 startAsyncRead 置位），不会误触发。 */
#ifndef XNETWORK_READ_STALL_MS
#define XNETWORK_READ_STALL_MS 3000u
#endif

typedef struct XDeviceNetworkWriteSlot {
    XEventContext_IOCP ctx;                 /**< 本槽专用的 OVERLAPPED 载体。 */
    char buffer[XNETWORK_WRITE_BUFFER_SIZE];/**< 本槽专用发送缓冲（内核持有期不可动）。 */
    bool inUse;                             /**< 是否在途。 */
    uint64_t armedMs;                       /**< 提交时刻（单调毫秒，看门狗用）。 */
} XDeviceNetworkWriteSlot;

typedef struct XDeviceNetworkContextWin32 {
    XDeviceNetworkContext base;             /**< 第一位：平台无关基类 (owner/xfd/notifiers) */
    SOCKET socket;                          ///< Windows SOCKET 句柄。
    HANDLE namedPipe;                       ///< Windows 命名管道句柄。

    /* 状态标志 */
    bool readPending;
    uint64_t readArmedMs;                   ///< 读请求提交时刻（读看门狗）。
    uint32_t readStallCount;                ///< 累计被看门狗判定"读挂死"的次数。
    bool writePending;
    bool connectPending;
    bool autoRead;
    bool isServer;                          ///< 是否为服务器套接字。
    bool acceptPending;                     ///< 是否有待处理的 Accept。
    volatile LONG pendingIocpOperations;    ///< 在途 IOCP 计数: 发起 + 完成两侧同步递减。
    volatile LONG deletePending;            ///< 拆除已挂起标记, 防重复拆除竞态。

    /* 异步写槽池（多写并发 + 完成包丢失隔离） */
    XDeviceNetworkWriteSlot writeSlots[XNETWORK_WRITE_SLOT_COUNT];
    int writeSlotCount;                     /**< 已用槽数（诊断/看门狗用）。 */
    uint32_t writeStallCount;               /**< 累计被看门狗判定"卡死"的槽数。 */

    /* UDP 来源地址 */
    struct sockaddr_in6 fromAddr;
    int fromAddrLen;

    /* IOCP 上下文 */
    XEventContext_IOCP readContext;
    union {
        XEventContext_IOCP connectContext;  ///< 客户端连接上下文。
        XEventContext_IOCP acceptContext;   ///< 服务端 AcceptEx 上下文。
    };

    /* 待连接信息 */
    XHostAddress pendingPeerAddr;
    uint16_t pendingPeerPort;

    /* 缓冲区联合体（服务器用 acceptBuffer，客户端用 readBuffer）。
     * 写缓冲已移入各写槽（见 XDeviceNetworkWriteSlot），不再共享。 */
    union {
        struct {
            char readBuffer[XNETWORK_READ_BUFFER_SIZE];
        };
        struct {
            char acceptBuffer[sizeof(struct sockaddr_in6) * 2 + 32]; ///< AcceptEx 地址双缓冲。
            SOCKET acceptSocket;                    ///< AcceptEx 落地的新套接字。
        };
    };
} XDeviceNetworkContextWin32;

/* 便捷转换宏 */
#define W32(p) ((XDeviceNetworkContextWin32*)(p))

/** @brief 取当前单调毫秒（看门狗时间戳用；实现在文件后段）。 */
static uint64_t W32_nowMs(void);

/* 套接字事件处理（读看门狗补投递时复用正常完成路径；定义在文件后段）。 */
bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event);

static bool w32_has_stream_handle(const XDeviceNetworkContextWin32* priv)
{
    return priv && (priv->socket != INVALID_SOCKET
                    || priv->namedPipe != INVALID_HANDLE_VALUE);
}

static void destroyContext(XDeviceNetworkContextWin32* p)
{
    if (!p) return;
    XClassDeinit(&p->pendingPeerAddr);
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
    XClassDeinit(&endpoint);
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

    p->readArmedMs = W32_nowMs();    memset(&p->readContext, 0, sizeof(XEventContext_IOCP));
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
    int result;

    /* [2026-11-04 官方口径修正] lpOverlapped 非 NULL 时，lpNumberOfBytesRecvd
     * **必须传 NULL**。WSARecv 官方文档原文：
     *   "Use NULL for this parameter [lpNumberOfBytesRecvd] if the
     *    lpOverlapped parameter is not NULL to avoid potentially erroneous
     *    results."
     * 旧代码同时传了非 NULL 的 &bytesTransferred 与 overlapped，属于文档
     * 明确要求避免的用法。完成字节数应以 GetQueuedCompletionStatus 的
     * lpNumberOfBytesTransferred 为准（本后端 processOneCompletion 正是这么取的），
     * 提交侧的局部变量拿不到可靠值、也没有用处。 */
    if (isUdp) {
        p->fromAddrLen = sizeof(p->fromAddr);
        result = WSARecvFrom(p->socket, &buf, 1, NULL, &flags,
            (struct sockaddr*)&p->fromAddr, &p->fromAddrLen,
            (OVERLAPPED*)&p->readContext, NULL);
    }
    else {
        result = WSARecv(p->socket, &buf, 1, NULL, &flags,
            (OVERLAPPED*)&p->readContext, NULL);
    }

    if (result == 0) {
        /* 同步完成：`WSARecv` 的提交侧已不再提供字节数（见上方注释：官方要求
         * overlapped 非 NULL 时 lpNumberOfBytesRecvd 传 NULL）。这里置 0，
         * 由后续的完成包（IOCP 对 overlapped socket 的同步完成同样会排队一个
         * 完成包）携带真实字节数覆盖。 */
        p->readContext.finishedBytes = 0;
        p->readPending = true;
    }
    else if (WSAGetLastError() == WSA_IO_PENDING) {
        /* 异步等待 */
        p->readPending = true;
    }
    else {
        /* 错误
         * @note [2026-11-04 可观测性] WSARecv 的失败分支此前静默丢弃，导致
         *       「读请求提交不出去」与「提交了但没有完成」两种完全不同的
         *       故障在现场无法区分。WSAEWOULDBLOCK 在 Win32 上有一条专门
         *       语义（官方 WSARecv 错误表）：
         *         "Overlapped sockets: there are too many outstanding
         *          overlapped I/O requests."
         *       ——即在飞请求过多，属背压而非链路故障，稍后重挂即可；
         *       其余错误码才是真异常。这里把两者分开记录。 */
        {
            int err = (int)WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                XPrintf("[XNetwork-W32] 读提交背压（在飞请求过多），稍后重挂\n");
            else
                XPrintf("[XNetwork-W32] 读提交失败 err=%d\n", err);
        }
        p->readPending = false;
        /* 背压场景保留 armedMs，让看门狗不会误判为"挂死"。 */
        p->readArmedMs = 0;
        abandonSocketIocpOperation(p, &p->readContext);
    }
}

/* =========================================================================
 * 异步写入启动
 * ========================================================================= */

/**
 * @brief 取当前单调毫秒（看门狗时间戳用）。
 * @note  只在本文件内部用，不进公共头（时间源收敛在 XDateTime）。
 */
static uint64_t W32_nowMs(void)
{
    return (uint64_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 回收「提交后长时间无完成」的写槽（完成包丢失兜底）。
 * @details 完成包丢失是实测发生的真实故障（客户端 WSASend 完成永不返回；
 *          服务端 371 提交 / 340 完成）。没有本看门狗时，丢失的槽会永久
 *          占用：缓冲不释放、在途计数不减，写路径最终再次锁死。
 *          这里超时即把槽标为空闲并计入 writeStallCount（可观测），
 *          让剩余的槽继续服务后续写——把"永久锁死"降级为"偶发丢弃一帧"。
 * @note   被回收的槽其 OVERLAPPED 可能仍被内核持有：清空前先调
 *         CancelIoEx 尝试取消；取消失败（NOT_FOUND = 操作其实已完成，
 *         见 MSDN CancelIoEx）也不影响回收决策——本函数只负责止损。
 */
static void w32_reapStalledWriteSlots(XDeviceNetworkContextWin32* p)
{
    uint64_t now;
    int i;
    bool anyFree = false;

    if (!p) return;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i)
        if (!p->writeSlots[i].inUse) { anyFree = true; break; }
    if (anyFree) return;  /* 还有空槽：不必回收，交给超时判断自然清。 */

    now = W32_nowMs();
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        XDeviceNetworkWriteSlot* slot = &p->writeSlots[i];
        if (!slot->inUse) continue;
        if (now - slot->armedMs < XNETWORK_WRITE_SLOT_TIMEOUT_MS) continue;
        if (p->socket != INVALID_SOCKET)
            (void)CancelIoEx((HANDLE)p->socket, (OVERLAPPED*)&slot->ctx);
        slot->inUse = false;
        slot->armedMs = 0;
        p->writeStallCount++;
        XPrintf("[XNetwork-W32] 写槽 %d 超时 %ums 未完成（完成包疑似丢失），"
                "已回收止损；累计 %u 次\n", i,
                (unsigned)XNETWORK_WRITE_SLOT_TIMEOUT_MS,
                (unsigned)p->writeStallCount);
    }
}

/** @brief 找空闲写槽；无空槽返回 NULL。 */
static XDeviceNetworkWriteSlot* w32_allocWriteSlot(XDeviceNetworkContextWin32* p)
{
    int i;
    if (!p) return NULL;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        if (!p->writeSlots[i].inUse) {
            p->writeSlots[i].inUse = true;
            p->writeSlots[i].armedMs = W32_nowMs();
            p->writeSlotCount++;
            return &p->writeSlots[i];
        }
    }
    return NULL;
}

/** @brief 归还写槽（完成 / 提交失败两条路径都走这里）。 */
static void w32_freeWriteSlot(XDeviceNetworkContextWin32* p,
                              XDeviceNetworkWriteSlot* slot)
{
    if (!slot) return;
    slot->inUse = false;
    slot->armedMs = 0;
    if (p && p->writeSlotCount > 0) p->writeSlotCount--;
}

/** @brief 清空全部写槽（连接建立/断开时调用；在途写随之作废）。 */
static void w32_resetWriteSlots(XDeviceNetworkContextWin32* p)
{
    int i;
    if (!p) return;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        p->writeSlots[i].inUse = false;
        p->writeSlots[i].armedMs = 0;
    }
    p->writeSlotCount = 0;
}

/**
 * @brief 提交一次异步写（从槽池取独立 OVERLAPPED + 独立缓冲）。
 * @return 已提交（含立即完成）返回 true；无空槽/参数非法/提交失败返回 false。
 * @note  [2026-11-04 独立写槽根修] 每次提交独占一份 OVERLAPPED 与缓冲，
 *        满足 MSDN 对 OVERLAPPED 复用与 WSASend 缓冲所有权的硬性约束
 *        （见 XDeviceNetworkWriteSlot 的注释）；不再依赖布尔 writePending
 *        互斥，因此单个完成包丢失不再阻塞后续写。
 */
static bool startAsyncWrite(XDeviceNetworkContext* priv, const void* data, int64_t len,
    const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    XDeviceNetworkWriteSlot* slot;

    if (!p || !w32_has_stream_handle(p)) return false;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return false;
    if (!data) return false;

    /* 无空槽时先尝试回收超时槽（完成包丢失兜底），再取。 */
    slot = w32_allocWriteSlot(p);
    if (!slot) {
        w32_reapStalledWriteSlots(p);
        slot = w32_allocWriteSlot(p);
    }
    if (!slot) return false;   /* 真的写不动：调用方把数据留在 ring buffer 等下一轮。 */

    memcpy(slot->buffer, data, (size_t)len);

    memset(&slot->ctx, 0, sizeof(XEventContext_IOCP));
    slot->ctx.base.type = XEventContextType_Type_Socket;
    slot->ctx.base.fd = priv->m_base.m_fd;
    slot->ctx.socket = XSocketDescriptor_fromIntptr(
        p->namedPipe != INVALID_HANDLE_VALUE ? (intptr_t)p->namedPipe : (intptr_t)p->socket);
    slot->ctx.eventMask = FD_WRITE;
    slot->ctx.finishedBytes = len;
    trackSocketIocpOperation(p, &slot->ctx);

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;
        if (WriteFile(p->namedPipe, slot->buffer, (DWORD)len,
                      &bytesTransferred, (OVERLAPPED*)&slot->ctx)) {
            slot->ctx.finishedBytes = bytesTransferred;
            p->writePending = true;
            return true;
        }
        if (GetLastError() == ERROR_IO_PENDING) {
            p->writePending = true;
            return true;
        }
        abandonSocketIocpOperation(p, &slot->ctx);
        w32_freeWriteSlot(p, slot);
        return false;
    }

    {
        WSABUF buf;
        int result;
        buf.buf = slot->buffer;
        buf.len = (ULONG)len;

        if (isUdp && destAddr) {
            struct sockaddr_storage dest;
            int destLen;
            addr2sa(destAddr, destPort, &dest, &destLen);
            result = WSASendTo(p->socket, &buf, 1, NULL, 0,
                (struct sockaddr*)&dest, destLen,
                (OVERLAPPED*)&slot->ctx, NULL);
        }
        else {
            result = WSASend(p->socket, &buf, 1, NULL, 0,
                (OVERLAPPED*)&slot->ctx, NULL);
        }

        if (result == 0) {
            /* An overlapped socket associated with an IOCP queues its own
             * completion packet even when WSASend completes synchronously.
             * Posting another packet here delivers the same OVERLAPPED twice;
             * a later delivery can outlive the FTP data socket that owned it.
             * 槽在完成事件里归还，这里只标记"有写在途"。 */
            p->writePending = true;
            return true;
        }
        if (WSAGetLastError() == WSA_IO_PENDING) {
            p->writePending = true;
            return true;
        }
        abandonSocketIocpOperation(p, &slot->ctx);
        w32_freeWriteSlot(p, slot);
        return false;
    }
}


/**
 * @brief 把写通道 ring buffer 里积压的数据续发出去（写完成后的回捞）。
 *
 * @details [2026-11-04 积压永不发送根修] `XDeviceNetwork_socketWrite` 在
 *          `writePending` 为真时只把新数据写进 ring buffer 并返回 len
 *          （伪装成功），却**没有任何路径负责把积压发出去**——
 *          `startAsyncWrite` 只在"空闲"分支调用。于是两帧间隔小于一次
 *          IOCP 往返（突发写，典型 ≤1ms）时，第二帧起全部沉在 ring buffer
 *          里永不发送：应用层看到写成功、对端零到达。
 *          实测 HSF：客户端累计"写出"412 字节，服务端 6 秒零收并判线。
 *
 *          本函数在写完成、`writePending` 已复位的时机被调用，把积压
 *          拷进写缓冲并立即提交下一次异步写，形成「完成 → 续发」闭环。
 *
 * @note 只处理写通道 0（本后端的 `socketWrite` 也只写当前写通道）。
 *       每次最多续一缓冲大小，剩余部分由后续完成事件继续推进。
 */
/* [死码清理 2026-11-04] XDeferredWrite_drain 已删除：它与框架既有的
 * `XDeviceNetwork_socketContinueWrite`（由 XAbstractSocket 在写完成事件里
 * 发 ContinueWrite 命令触发）职责完全重复。当年那条"积压永不发送"的
 * 怀疑，真因是 `socketContinueWrite` 的 `if (p->writePending) return;`
 * 门——单个完成包丢失即永久挡死续发（已在独立写槽改造中修掉）。
 * 保留一份实现，避免两处各自维护续发逻辑、语义分叉。 */

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
        /* Connect the UDP socket for real. This makes send()/recv() work
         * without WSASendTo, and lets the kernel drop datagrams from any
         * source other than the peer - the source filtering a client such as
         * NTP needs before trusting a reply. UDP connect never blocks and
         * does not report failure for an unreachable peer, so a successful
         * return is expected. */
        if (connect(p->socket, (struct sockaddr*)&destAddr, destLen) != 0) {
            closesocket(p->socket); p->socket = INVALID_SOCKET;
            return false;
        }
        p->base.m_connected = true;
        p->connectPending = false;
        syncSocketEndpoints(priv);
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
    w32_resetWriteSlots(p);
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
    w32_resetWriteSlots(p);
}

int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!w32_has_stream_handle(p)) return -1;

    /* [2026-11-04 读挂死看门狗] 读请求提交后长时间没有任何完成 ⇒ 重挂一次。
     *
     * 实测故障形态：两端在约 2.1s 同时停止接收，之后各自继续发送 5~10 秒
     * （客户端最后 RX 2108ms / 最后 TX 7165ms；服务端 2294ms / 12257ms），
     * 最终双双静默判线。写方向正常、写槽看门狗零触发 ⇒ 停的是**读方向**。
     *
     * 读请求由 `startAsyncRead` 提交并被 `readPending` 互斥；只有
     * `socketHandleEvent` 的 Read 分支会清它。一旦某个读完成事件没有投递到
     * 那里（平台侧完成包异常），`readPending` 永久为真 ⇒ 再也不重挂读 ⇒
     * 该连接只发不收。
     *
     * 这里在每次读调用（会话 pump 的必经路径）做超时检查：挂起超过阈值就
     * 用 CancelIoEx 尝试取消旧请求并重挂，把"永久只发不收"降级为一次可观测
     * 的抖动。计数进 readStallCount 供诊断。 */
    if (p->readPending && p->readArmedMs != 0
        && (W32_nowMs() - p->readArmedMs) > XNETWORK_READ_STALL_MS) {
        /* [2026-11-04 读停摆的三态判定] 用两个**互相独立**的官方手段把
         * "读不动"拆成三种可区分的情况，而不是一律取消重挂：
         *
         *   A. GetOverlappedResult(FALSE) == TRUE
         *      内核已完成、完成包没投递到端口。数据已在 readBuffer 里，
         *      取消会丢数据 ⇒ 按完成处理（补投递）。
         *      官方："If this parameter is FALSE and the operation is still
         *      pending, the function returns FALSE and GetLastError returns
         *      ERROR_IO_INCOMPLETE."（Learn / ioapiset / GetOverlappedResult）
         *
         *   B. FIONREAD > 0 而操作未完成
         *      数据已在内核接收队列，但挂起的读没有完成它。这是读通道
         *      真故障 ⇒ 取消后重挂，让新读把队列里的数据取走。
         *
         *   C. FIONREAD == 0 而操作未完成
         *      内核里没有数据 ⇒ 对端确实没发。这不是本端故障，取消重挂
         *      只是为了不让 readPending 永久卡死（重挂后继续等）。
         *
         * 附官方警告（Learn / fileio / i-o-completion-ports）："When a file
         * handle is associated with a completion port, the status block passed
         * in will not be updated until the packet is removed from the
         * completion port." —— 故 A 在 IOCP 句柄上可能被滞后状态误判，
         * 因此把它与 FIONREAD 并列作为独立证据，两者都拿不到才走 C。 */
        DWORD got = 0;
        BOOL ovDone = FALSE;
        u_long readable = 0;

        if (p->socket != INVALID_SOCKET) {
            ovDone = GetOverlappedResult((HANDLE)p->socket,
                                         (OVERLAPPED*)&p->readContext,
                                         &got, FALSE);
            (void)ioctlsocket(p->socket, FIONREAD, &readable);
        }

        p->readStallCount++;
        if (ovDone) {
            /* 情况 A：操作已完成，补投递。 */
            XPrintf("[XNetwork-W32] 读停摆=完成包丢失（已结束 %lu 字节），"
                    "补投递；累计 %u 次\n",
                    (unsigned long)got, (unsigned)p->readStallCount);
            p->readContext.finishedBytes = got;
            p->readPending = false;
            p->readArmedMs = 0;
            {
                XEventSockAct fakeAct;
                memset(&fakeAct, 0, sizeof(fakeAct));
                fakeAct.m_base.m_type = XEVENT_TYPE_SOCK_ACT;
                fakeAct.fd = xfd;
                fakeAct.actType = XSocketActType_Read;
                (void)XDeviceNetwork_socketHandleEvent(xfd, &fakeAct);
            }
            return 0;   /* 本轮数据已交给上层。 */
        }

        XPrintf("[XNetwork-W32] 读停摆=%s（内核待读=%lu 字节），取消重挂；"
                "累计 %u 次\n",
                readable > 0 ? "数据在内核但读未完成" : "内核无数据(对端未发)",
                (unsigned long)readable, (unsigned)p->readStallCount);
        if (p->socket != INVALID_SOCKET) {
            (void)CancelIoEx((HANDLE)p->socket, (OVERLAPPED*)&p->readContext);
        }
        p->readPending = false;
        p->readArmedMs = 0;
        startAsyncRead(priv, sockType == XDeviceNetwork_Udp);
    }

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
        /* [2026-11-04] 只有真正提交成功才回报 len；提交失败（槽池耗尽且
         * 回收不出空槽）必须让调用方看见失败，否则上层把"没发出去"当成功
         * 累加发送字节数（正是假成功掩盖真丢帧的成因之一）。 */
        if (!startAsyncWrite(priv, buf, len, destAddr, destPort,
                             sockType == XDeviceNetwork_Udp)) {
            if (ringBuffer) {
                struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
                return XRingBuffer_write(rb, buf, len);
            }
            return -1;
        }
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
    if (e->m_type != XEVENT_TYPE_SOCK_ACT) return false;

    XEventSockAct* sockAct = (XEventSockAct*)e;

    if (sockAct->actType & XSocketActType_Read) {
        p->readPending = false;
        return p->readContext.finishedBytes > 0;
    }
    if (sockAct->actType & XSocketActType_Write) {
        /* [2026-11-04 独立写槽] 每次写完成按"哪个槽还在途且无完成"归还该槽。
         * 完成事件本身不带 OVERLAPPED 指针（CQEntry 只传 fd/掩码），故按
         * 提交顺序归还最早在途的槽：IOCP 对同一句柄的完成包保持提交顺序，
         * 这一对应关系在本后端的用法下成立。
         * `writePending` 改为「是否仍有在途写」，供 socketWrite 判断能否
         * 直接提交（>=1 在途时走 ring buffer 排队）。 */
        {
            int i;
            for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
                if (p->writeSlots[i].inUse) {
                    w32_freeWriteSlot(p, &p->writeSlots[i]);
                    break;
                }
            }
        }
        if (p->writeSlotCount == 0)
            p->writePending = false;
        /* 积压续发由上层统一驱动：XAbstractSocket 在写完成分支发出
         * XDeviceNetworkCommand_ContinueWrite → socketContinueWrite 回捞
         * ring buffer。本后端不再自行续发（避免两套续发逻辑分叉）。 */
        return true;
    }
    if (sockAct->actType & XSocketActType_Connect) {
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
    /* [2026-11-04 accept 完成] AcceptEx 完成包到达时**不得**清 acceptPending。
     *
     * 这个标志的语义是"监听 socket 上已挂了一个 AcceptEx"，只有消费方
     * （`XDeviceNetwork_serverGetAcceptedSocket` 取走 acceptSocket 后）才
     * 能清它。若在本函数里清掉，`serverAccept` 的守卫就失效，会在旧
     * AcceptEx 尚未消费时再挂一个 ⇒ 同一个监听 socket 上两个 AcceptEx
     * 同时在飞，第二个完成时 `p->acceptSocket` 被覆盖（第一个 socket
     * 泄漏且仍关联 IOCP）⇒ 实测表现为「客户端只连一次，服务端却反复
     * accept 出新会话」。
     *
     * 这里只回报"有完成可消费"，由上层决定何时取走、何时重挂。 */
    if (sockAct->actType & XSocketActType_Accept) {
        return p->acceptSocket != INVALID_SOCKET;
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
    XDeviceNetworkContextWin32* p;
    int i;
    if (!priv) return 0;
    p = W32(priv);
    /* [2026-11-04 独立写槽] 无单一 writeContext 可查：取最近一个在途槽
     * 的完成字节数（全空时返回 0）。上层只用它做"本次写是否完成"的观测。 */
    for (i = XNETWORK_WRITE_SLOT_COUNT - 1; i >= 0; --i) {
        if (p->writeSlots[i].inUse)
            return p->writeSlots[i].ctx.finishedBytes;
    }
    return 0;
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
    p->isServer = true;    p->base.m_connected = true;

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
    /* [注 2026-11-04] 这里**不能**再调 abandonSocketIocpOperation：
     * 该 acceptContext 的完成包出队时，`socketIocpOperationCompleted`
     * 已经做过一次收尾（摘回调 + releaseContext）。此处重复摘会造成在途
     * 计数双重递减，并在一部分时序下访问已完成释放的上下文——实测即
     * `XDevice_dispatchControl` 处 0xFEEEFEEE 段错误。在途计数统一由
     * 完成回调那一侧收敛。 */
    return (XDeviceNetworkSocketHandle)result;
}

#endif /* XNETWORK_WIN_SOCKET_IOCP —— IOCP 套接字/服务器实现段结束 */

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
    XContainerSetDataDeinitMethod(vec, XClass_deinit_base);
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
        XClassDelete(vec);
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
    if (iface->m_name)
        XString_assign(iface->m_name, current->AdapterName);
    else
        iface->m_name = XString_create_utf8(current->AdapterName);
    iface->m_humanReadableName = XString_create_utf16(current->FriendlyName);

    iface->m_index = (int)current->IfIndex;
    iface->m_mtu = (int)current->Mtu;

    /* 硬件地址 (MAC) */
    if (current->PhysicalAddressLength > 0 && current->PhysicalAddressLength <= 32) {
        char macStr[64];
        int pos = 0;
        for (ULONG i = 0; i < current->PhysicalAddressLength; i++) {
            pos += sprintf(macStr + pos, "%02X:", current->PhysicalAddress[i]);
        }
        if (pos > 0) macStr[pos - 1] = '\0'; /* 移除最后的冒号 */
        if (iface->m_hardwareAddress)
            XString_assign_fmt_utf8(iface->m_hardwareAddress, macStr);
        else
            iface->m_hardwareAddress = XString_create_fmt_utf8(macStr);
        //iface->m_hardwareAddress = XString_create_fmt_utf8(macStr);
    }

    /* 标志 */
    if (current->OperStatus == IfOperStatusUp) {
        iface->m_flags |= XNetworkInterface_IsUp | XNetworkInterface_IsRunning;
    }
    if (current->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        iface->m_flags |= XNetworkInterface_IsLoopBack;
    }
    if (current->Flags & IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED) {
        iface->m_flags |= XNetworkInterface_CanMulticast;
    }
    if (current->IfType == IF_TYPE_ETHERNET_CSMACD) {
        iface->m_type = XNetworkInterface_Ethernet;
        iface->m_flags |= XNetworkInterface_CanBroadcast | XNetworkInterface_CanMulticast;
    }
    else if (current->IfType == IF_TYPE_IEEE80211) {
        /* 无线网卡单列（对齐 Qt 6.8.3：IF_TYPE_IEEE80211 → Ieee80211/Wifi，
           此前误并入 Ethernet 会让无线口在界面上显示"以太网"）。 */
        iface->m_type = XNetworkInterface_Wifi;
        iface->m_flags |= XNetworkInterface_CanBroadcast | XNetworkInterface_CanMulticast;
    }
    else if (current->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        iface->m_type = XNetworkInterface_Loopback;
    }

    iface->m_isValid = true;

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
            XClassDeinit(&mask);
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
            XClassDeinit(&mask);
        }

        XVector_push_back_move_1_base(iface->m_addressEntries, &entry);
        XClassDeinit(&entry);
        XClassDeinit(&addr);

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
 * 网卡配置（DHCP / 静态 IP）
 * =========================================================================
 * 查询走 IP Helper（GetAdaptersAddresses 单遍遍历按 IfIndex 命中）；
 * 设置经 netsh（interface ip 上下文）——Windows 官方脚本化配置通路，
 * 以 CREATE_NO_WINDOW 子进程执行并按退出码判定；需要管理员权限。
 */

/** @brief UTF-16 → UTF-8 堆拷贝（XMalloc_System 分配；失败返回 NULL）。 */
static wchar_t* netIfDupWide(const char* utf8)
{
    int chars;
    wchar_t* wide;
    if (!utf8) utf8 = "";
    chars = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (chars <= 0) return NULL;
    wide = (wchar_t*)XMalloc_System((size_t)chars * sizeof(wchar_t));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, chars) <= 0) {
        XFree_System(wide);
        return NULL;
    }
    return wide;
}

/** @brief 单遍枚举所有适配器并按 IfIndex 命中（GAA 缓冲由调用方释放）。 */
static PIP_ADAPTER_ADDRESSES netIfFindAdapter(uint32_t ifIndex,
                                              PIP_ADAPTER_ADDRESSES* buffer)
{
    ULONG size = 0;
    ULONG rc;
    PIP_ADAPTER_ADDRESSES head;
    *buffer = NULL;
    /* INCLUDE_GATEWAYS 必带：GAA 默认不返回 FirstGatewayAddress（网关
       恒空的实测根因）；INCLUDE_PREFIX 供前缀掩码展开。 */
    GetAdaptersAddresses(AF_UNSPEC,
                         GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS,
                         NULL, NULL, &size);
    if (size == 0) return NULL;
    head = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
    if (!head) return NULL;
    rc = GetAdaptersAddresses(AF_UNSPEC,
                              GAA_FLAG_INCLUDE_PREFIX |
                                  GAA_FLAG_INCLUDE_GATEWAYS,
                              NULL, head, &size);
    if (rc != ERROR_SUCCESS) {
        XFree_System(head);
        return NULL;
    }
    *buffer = head;
    {
        PIP_ADAPTER_ADDRESSES a;
        for (a = head; a; a = a->Next)
            if ((uint32_t)a->IfIndex == ifIndex) return a;
    }
    return NULL;
}

/** @brief 以隐藏窗口执行一条 netsh 命令行（UTF-8 参数），按退出码判定。 */
static bool netIfRunNetsh(const char* argumentsUtf8)
{
    wchar_t* args = netIfDupWide(argumentsUtf8);
    wchar_t commandLine[2048];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    bool ok = false;
    if (!args) return false;
    _snwprintf(commandLine,
               sizeof(commandLine) / sizeof(commandLine[0]) - 1,
               L"netsh.exe %s", args);
    commandLine[sizeof(commandLine) / sizeof(commandLine[0]) - 1] = L'\0';
    XFree_System(args);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    memset(&process, 0, sizeof(process));
    if (!CreateProcessW(NULL, commandLine, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return false;
    /* 等待应用完成（地址切换通常秒级；超时按失败收口，进程随之遗弃）。 */
    if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        if (GetExitCodeProcess(process.hProcess, &exitCode) &&
            exitCode == 0)
            ok = true;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ok;
}

/** @brief netsh name= 参数消毒：剔除内嵌双引号，防拆参数。 */
static wchar_t* netIfSanitizeName(const wchar_t* name)
{
    size_t i;
    size_t j = 0;
    size_t count;
    wchar_t* out;
    if (!name) return NULL;
    count = wcslen(name);
    out = (wchar_t*)XMalloc_System((count + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    for (i = 0; i < count; ++i)
        if (name[i] != L'"') out[j++] = name[i];
    out[j] = L'\0';
    return out;
}

/** @brief 按 ifIndex 组装 `name="<连接名>"` 片段；失败返回 NULL。 */
static wchar_t* netIfNameArgument(uint32_t ifIndex)
{
    PIP_ADAPTER_ADDRESSES buffer = NULL;
    PIP_ADAPTER_ADDRESSES adapter = netIfFindAdapter(ifIndex, &buffer);
    wchar_t* quoted = NULL;
    wchar_t* safe;
    if (!adapter) return NULL;
    safe = netIfSanitizeName(adapter->FriendlyName);
    XFree_System(buffer);
    if (!safe) return NULL;
    quoted = (wchar_t*)XMalloc_System(
        (wcslen(safe) + 16) * sizeof(wchar_t));
    if (quoted)
        _snwprintf(quoted, wcslen(safe) + 15, L"name=\"%s\"", safe);
    XFree_System(safe);
    return quoted;
}

bool XDeviceNetwork_interfaceConfigSupported(void)
{
    return true;
}

bool XDeviceNetwork_queryInterfaceConfig(uint32_t ifIndex,
                                         XDeviceNetworkInterfaceConfig* outConfig)
{
    PIP_ADAPTER_ADDRESSES buffer = NULL;
    PIP_ADAPTER_ADDRESSES adapter;
    PIP_ADAPTER_UNICAST_ADDRESS unicast;
    PIP_ADAPTER_GATEWAY_ADDRESS gateway;
    PIP_ADAPTER_DNS_SERVER_ADDRESS dns;
    int dnsCount = 0;
    bool haveV4Gw = false;
    bool haveV6Gw = false;
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
    XHostAddress_init(&outConfig->dnsV6Primary);
    XHostAddress_init(&outConfig->dnsV6Secondary);
    adapter = netIfFindAdapter(ifIndex, &buffer);
    if (!adapter) return false;
    outConfig->dhcpEnabled =
        (adapter->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
    outConfig->operUp = (adapter->OperStatus == IfOperStatusUp);
    outConfig->friendlyName = XString_create_utf16(adapter->FriendlyName);
    /* 首个 IPv4 单播地址 + 前缀展开的掩码（主机字节序入 XHostAddress）。 */
    for (unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
        if (unicast->Address.lpSockaddr &&
            unicast->Address.lpSockaddr->sa_family == AF_INET) {
            uint32_t prefix = unicast->OnLinkPrefixLength;
            uint32_t maskHost;
            if (prefix > 32) prefix = 32;
            sa2addr((struct sockaddr_storage*)unicast->Address.lpSockaddr,
                    &outConfig->ipv4Address, NULL);
            maskHost = (prefix == 0)
                ? 0u : (prefix >= 32)
                    ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix));
            XHostAddress_setAddressIPv4(&outConfig->ipv4Netmask, maskHost);
            break;
        }
    }
    /* 首个 IPv6 单播地址 + 前缀长度（预填用；页内全列表另经接口枚举）。 */
    for (unicast = adapter->FirstUnicastAddress; unicast;
         unicast = unicast->Next) {
        if (unicast->Address.lpSockaddr &&
            unicast->Address.lpSockaddr->sa_family == AF_INET6) {
            int prefix = unicast->OnLinkPrefixLength;
            if (prefix < 0) prefix = 0;
            if (prefix > 128) prefix = 128;
            sa2addr((struct sockaddr_storage*)unicast->Address.lpSockaddr,
                    &outConfig->ipv6Address, NULL);
            outConfig->ipv6PrefixLength = prefix;
            break;
        }
    }
    /* 首个 IPv4 / IPv6 默认网关。 */
    for (gateway = adapter->FirstGatewayAddress; gateway;
         gateway = gateway->Next) {
        if (!gateway->Address.lpSockaddr) continue;
        if (!haveV4Gw &&
            gateway->Address.lpSockaddr->sa_family == AF_INET) {
            sa2addr((struct sockaddr_storage*)gateway->Address.lpSockaddr,
                    &outConfig->ipv4Gateway, NULL);
            haveV4Gw = true;
        } else if (!haveV6Gw &&
                   gateway->Address.lpSockaddr->sa_family == AF_INET6) {
            sa2addr((struct sockaddr_storage*)gateway->Address.lpSockaddr,
                    &outConfig->ipv6Gateway, NULL);
            haveV6Gw = true;
        }
        if (haveV4Gw && haveV6Gw)
            break;
    }
    /* DHCP 服务器（GAA 结构不含该字段，经 GetAdaptersInfo 按接口索引
       补齐；未启用 DHCP 或服务器为 0.0.0.0 时保持 null 地址）。 */
    {
        PIP_ADAPTER_INFO infoList = NULL;
        ULONG infoSize = 0;
        PIP_ADAPTER_INFO info;
        GetAdaptersInfo(NULL, &infoSize);
        if (infoSize > 0) {
            infoList = (PIP_ADAPTER_INFO)XMalloc_System(infoSize);
            if (infoList &&
                GetAdaptersInfo(infoList, &infoSize) == ERROR_SUCCESS) {
                for (info = infoList; info; info = info->Next) {
                    if ((uint32_t)info->Index == ifIndex &&
                        info->DhcpEnabled &&
                        info->DhcpServer.IpAddress.String[0] &&
                        strcmp(info->DhcpServer.IpAddress.String,
                               "0.0.0.0") != 0) {
                        XHostAddress_setAddressIPv4(
                            &outConfig->dhcpServer,
                            ntohl(inet_addr(
                                info->DhcpServer.IpAddress.String)));
                        break;
                    }
                }
            }
            if (infoList) XFree_System(infoList);
        }
    }
    /* ? 2 ? IPv4 DNS? */
    for (dns = adapter->FirstDnsServerAddress; dns && dnsCount < 2;
         dns = dns->Next) {
        if (!dns->Address.lpSockaddr ||
            dns->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        sa2addr((struct sockaddr_storage*)dns->Address.lpSockaddr,
                dnsCount == 0 ? &outConfig->dnsPrimary
                              : &outConfig->dnsSecondary,
                NULL);
        ++dnsCount;
    }
    XFree_System(buffer);
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
    XClassDeinit(&config->dnsV6Primary);
    XClassDeinit(&config->dnsV6Secondary);
    memset(config, 0, sizeof(*config));
}

bool XDeviceNetwork_setInterfaceDhcp(uint32_t ifIndex)
{
    wchar_t* name = netIfNameArgument(ifIndex);
    char command[1024];
    char nameUtf8[256];
    int written;
    if (!name) return false;
    written = WideCharToMultiByte(CP_UTF8, 0, name, -1,
                                  nameUtf8, sizeof(nameUtf8), NULL, NULL);
    XFree_System(name);
    if (written <= 0) return false;
    _snprintf(command, sizeof(command) - 1,
              "interface ip set address %s source=dhcp", nameUtf8);
    if (!netIfRunNetsh(command)) return false;
    _snprintf(command, sizeof(command) - 1,
              "interface ip set dnsservers %s source=dhcp", nameUtf8);
    return netIfRunNetsh(command);
}

bool XDeviceNetwork_setInterfaceStatic(uint32_t ifIndex, const char* ipv4Address,
                                       const char* ipv4Netmask,
                                       const char* ipv4Gateway,
                                       const char* dnsPrimary,
                                       const char* dnsSecondary)
{
    wchar_t* name;
    char command[1024];
    char nameUtf8[256];
    int written;
    if (!ipv4Address || !ipv4Address[0] || !ipv4Netmask || !ipv4Netmask[0])
        return false;
    name = netIfNameArgument(ifIndex);
    if (!name) return false;
    written = WideCharToMultiByte(CP_UTF8, 0, name, -1,
                                  nameUtf8, sizeof(nameUtf8), NULL, NULL);
    XFree_System(name);
    if (written <= 0) return false;
    /* 地址 + 掩码 + 网关（空网关以 none 占位，netsh 语义=不设网关）。 */
    _snprintf(command, sizeof(command) - 1,
              "interface ip set address %s static %s %s %s",
              nameUtf8, ipv4Address, ipv4Netmask,
              (ipv4Gateway && ipv4Gateway[0]) ? ipv4Gateway : "none");
    if (!netIfRunNetsh(command)) return false;
    /* DNS：主/备/清空三态。 */
    if (dnsPrimary && dnsPrimary[0]) {
        _snprintf(command, sizeof(command) - 1,
                  "interface ip set dnsservers %s static %s primary",
                  nameUtf8, dnsPrimary);
        if (!netIfRunNetsh(command)) return false;
        if (dnsSecondary && dnsSecondary[0]) {
            _snprintf(command, sizeof(command) - 1,
                      "interface ip add dnsservers %s %s index=2",
                      nameUtf8, dnsSecondary);
            return netIfRunNetsh(command);
        }
        return true;
    }
    _snprintf(command, sizeof(command) - 1,
              "interface ip set dnsservers %s source=static addr=none",
              nameUtf8);
    return netIfRunNetsh(command);
}

bool XDeviceNetwork_setInterfaceStaticIpv6(uint32_t ifIndex,
                                           const char* ipv6Address,
                                           int prefixLength,
                                           const char* ipv6Gateway)
{
    wchar_t* name;
    char command[1200];
    char nameUtf8[256];
    int written;
    if (!ipv6Address || !ipv6Address[0] ||
        prefixLength < 0 || prefixLength > 128)
        return false;
    name = netIfNameArgument(ifIndex);
    if (!name) return false;
    written = WideCharToMultiByte(CP_UTF8, 0, name, -1,
                                  nameUtf8, sizeof(nameUtf8), NULL, NULL);
    XFree_System(name);
    if (written <= 0) return false;
    /* 追加静态地址（ipv6 无 set address；已存在时 netsh 报错→按失败
     * 上报）。不影响既有 SLAAC/临时地址。 */
    _snprintf(command, sizeof(command) - 1,
              "interface ipv6 add address %s %s/%d",
              nameUtf8, ipv6Address, prefixLength);
    if (!netIfRunNetsh(command)) return false;
    if (ipv6Gateway && ipv6Gateway[0]) {
        _snprintf(command, sizeof(command) - 1,
                  "interface ipv6 add route ::/0 %s %s",
                  nameUtf8, ipv6Gateway);
        return netIfRunNetsh(command);
    }
    return true;
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

#if XNETWORK_WIN_SOCKET_IOCP

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

#endif /* XNETWORK_WIN_SOCKET_IOCP —— UDP 来源查询段结束 */

/* =========================================================================
 * 代理隧道（已移至应用层实现）
 * ========================================================================= */
 /* 注意：XDeviceNetwork_socks5Connect、XDeviceNetwork_httpConnect、XDeviceNetwork_socks5Bind、
  * XDeviceNetwork_serverCreateWithProxy?XDeviceNetwork_serverAcceptWithProxy
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
                    XClassDelete(host);
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
                    XClassDelete(host);
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

/* Frees every platform resource written into *context by gssapiAuth
 * (SSPI context, credential handle, duplicated target name) and clears it.
 * Defined before gssapiAuth because the auth paths reuse it on return 0/-1. */
void XDeviceNetwork_gssapiRelease(void** context)
{
    if (!context || !*context) return;

    WinGssapiContext* ctx = (WinGssapiContext*)*context;
    if (ctx->hasCtxt) {
        DeleteSecurityContext(&ctx->ctxtHandle);
        ctx->hasCtxt = false;
    }
    if (ctx->hasCred) {
        FreeCredentialHandle(&ctx->credHandle);
        ctx->hasCred = false;
    }
    if (ctx->targetName) {
        XFree_System(ctx->targetName);
        ctx->targetName = NULL;
    }
    XFree_System(ctx);
    *context = NULL;
}

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
            if (svcStr) ctx->targetName = XStrdup(svcStr);
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
        /* ISC wrote the context handle into ctxtHandle even on SEC_E_OK;
         * mark it so the cleanup below frees the SSPI context (per the
         * header contract, *context must be released when we return 0/-1). */
        ctx->hasCtxt = true;
        XDeviceNetwork_gssapiRelease(context);
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
    XDeviceNetwork_gssapiRelease(context);
    return -1;
}
/* =========================================================================
 * 写通道续发（IOCP 专属：依赖写槽池 startAsyncWrite）
 * ========================================================================= */

#if XNETWORK_WIN_SOCKET_IOCP

void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !ringBuffer) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!w32_has_stream_handle(p)) return;
    /* [2026-11-04 独立写槽] 去掉 writePending 门：槽池允许多写在途，
     * 只要还有空槽就能继续推进积压。原先 `if (p->writePending) return;`
     * 会在单个完成包丢失后永久挡住续发。
     * @note 读取顺序修正：先确认 startAsyncWrite 有能力受理（提交失败时
     *       不消费 ring buffer），否则读走的数据会直接丢失。 */
    {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t pending = XRingBuffer_available(rb);
        char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
        size_t chunk = pending;
        size_t got;
        if (pending == 0) return;
        if (chunk > XNETWORK_WRITE_BUFFER_SIZE) chunk = XNETWORK_WRITE_BUFFER_SIZE;
        got = XRingBuffer_read(rb, tempBuf, chunk);
        if (got > 0) {
            if (!startAsyncWrite(priv, tempBuf, (int64_t)got, NULL, 0, isUdp)) {
                /* 提交失败：把刚取出的数据写回队列头（下一轮再试），
                 * 避免"读走即丢"。 */
                XRingBuffer_write(rb, tempBuf, got);
            }
        }
    }
}

#endif /* XNETWORK_WIN_SOCKET_IOCP —— 写续发段结束 */

#endif /* XNETWORK_USE_PLATFORM_API */
