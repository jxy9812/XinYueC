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
#include "XCoreApplication.h"   /* 读失败时直接把关闭事件投递给属主对象 */
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
        /* [Qt 6.8 对齐] 双栈 socket 的 IPv4 对端以 v4-mapped 形式出现，
         * 还原成 IPv4（Qt QHostAddress 同样处理）。 */
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
            uint32_t v4;
            memcpy(&v4, &s6->sin6_addr.s6_addr[12], sizeof(v4));
            XHostAddress_setAddressIPv4(addr, ntohl(v4));
        } else {
            XHostAddress_setAddressIPv6(addr, (const uint8_t*)&s6->sin6_addr);
        }
        if (port) *port = ntohs(s6->sin6_port);
    }
    else {
        const struct sockaddr_in* s4 = (const struct sockaddr_in*)ss;
        XHostAddress_setAddressIPv4(addr, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/** @brief 套接字是否为 IPv6（双栈）socket。 */
static bool xw32_socketIsIpv6(SOCKET s)
{
    struct sockaddr_storage ss;
    int len = (int)sizeof(ss);
    if (s == INVALID_SOCKET) return false;
    if (getsockname(s, (struct sockaddr*)&ss, &len) != 0) return false;
    return ss.ss_family == AF_INET6;
}

/** @brief 构造目标地址；IPv4 目标发给双栈 IPv6 socket 时转成 v4-mapped。 */
static void xw32_mapV4Dest(SOCKET s, const XHostAddress* destAddr, uint16_t port,
                           struct sockaddr_storage* ss, int* ssLen)
{
    addr2sa(destAddr, port, ss, ssLen);
    if (ss->ss_family == AF_INET && xw32_socketIsIpv6(s)) {
        struct sockaddr_in6 mapped;
        memset(&mapped, 0, sizeof(mapped));
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = htons(port);
        mapped.sin6_addr.s6_addr[10] = 0xFF;
        mapped.sin6_addr.s6_addr[11] = 0xFF;
        memcpy(&mapped.sin6_addr.s6_addr[12],
               &((struct sockaddr_in*)ss)->sin_addr.s_addr, 4);
        memset(ss, 0, sizeof(*ss));
        memcpy(ss, &mapped, sizeof(mapped));
        *ssLen = (int)sizeof(mapped);
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
 * @brief IOCP 后端不变量与槽池布局（2026-11-06 根修）。
 *
 * @details ## 四条不变量（本轮根修的全部依据）
 *
 *          - **A. 一份 OVERLAPPED 同一时刻只能在飞一笔操作。**
 *          - **B. 只有当该笔操作的完成包出队时，才允许这份 OVERLAPPED 参与
 *            下一次投递**（"回收"＝完成包已出队，而不是"我认为它该完成了"）。
 *          - **C. `CancelIoEx` 不是完成的替代品**：被取消的操作仍会投递一个
 *            `ERROR_OPERATION_ABORTED` 完成包；重投只能等这个完成包出队。
 *          - **D. ABORTED 完成包是内部控制流，不是语义事件**，不得下派成
 *            读/写/Accept 事件。
 *
 *          ## 旧实现违反 A/B/C 的四处（每一处单独都能造成"完成包丢失"）
 *
 *          1. 读看门狗 `CancelIoEx(readContext)` 之后**立刻**用同一
 *             `readContext` 重投 `WSARecv`；
 *          2. `socketContinueRead` 无条件清 `readPending` 后重投（在飞判据
 *             是布尔变量而非内核真实状态）；
 *          3. `socketDisconnect` → 立即重连时，上一连接"已取消但未回收"的读
 *             与 `readContext` 被新连接复用；
 *          4. 写槽看门狗直接 `inUse=false`，把**内核仍持有**的 OVERLAPPED
 *             放回池子，下一次写立刻复用它。
 *
 *          机制级复现器 `verify/iocp_overlapped_alias_repro.c` 用例 B 复现了
 *          第 1 条：取消后立即复用同一 OVERLAPPED 重投 ⇒ 下一批数据永远停在
 *          内核（`FIONREAD=500`）而完成包永不投递——正是现场日志里
 *          "内核有数据但读未完成 / 完成包丢失"两种描述的同源现象。
 *
 *          ## 修法
 *
 *          把"在飞/已回收"从布尔猜测改成**每笔操作独占的槽 + 完成包驱动的
 *          状态迁移**：
 *            - 投递只有一个入口：`w32_armRead` / `w32_allocWriteSlot`；
 *            - 回收只有一个出口：`w32_reapIocpCompletion`（按 OVERLAPPED
 *              指针身份归属，不猜掩码、不猜提交顺序）；
 *            - 取消只发 `CancelIoEx`，**重投发生在 ABORTED 完成包里**；
 *            - 完成包确实丢失时，该槽**永久退役**（迟到的包落在退役槽上被
 *              静默丢弃），业务改用备用槽继续，绝不冒"迟到包污染新操作"的险。
 */

/** @brief 异步写槽数量。
 *
 * @details 每个槽独占一份 OVERLAPPED + 发送缓冲：多个写可同时在途，且 TCP
 *          按提交顺序交付，故并发写不串序。槽数是"内核持有 vs 完成回收"之间
 *          的缓冲深度；8 槽 × 8KB ≈ 64KB 在途量，对工业现场的小帧高频写足够，
 *          同时把单槽异常的影响面压到 1/8。 */
#define XNETWORK_WRITE_SLOT_COUNT 8
/** @brief 写槽挂起多久视为异常（毫秒）。
 *
 * @details 超时动作已从"直接把槽标空闲"改为"只发 CancelIoEx"（见
 *          `w32_reapStalledWriteSlots`）：槽能否复用一律由完成包决定。 */
#define XNETWORK_WRITE_SLOT_TIMEOUT_MS 2000u

/** @brief 读槽数量。
 *
 * @details 正常路径只需要 1 个在飞读槽；额外两槽用于两件必须做、但绝不能
 *          复用"在飞 OVERLAPPED"的事：
 *            - 完成包疑似丢失时把该槽**永久退役**（迟到的完成包因此不会污染
 *              后续任一操作），接着用备用槽继续服务；
 *            - 旧连接"已取消但未回收"的完成包尚未出队时，新连接仍要能立刻
 *              挂上读。 */
#define XNETWORK_READ_SLOT_COUNT 3

/** @brief 判定"完成包丢失"所需的完成端口检查次数（自挂读起）。
 *
 * @details `GetOverlappedResult` 只看状态块：内核已完成、包还在端口里排队时
 *          它同样返回 TRUE。仅凭它判丢包，会把"这一批事件投递耗时长
 *          （GUI 切页/重绘）"误判成丢包。只有当端口自挂读起已被检查过
 *          N 次、包仍未出队，才是真丢包。 */
#ifndef XNETWORK_READ_LOST_POLLS
#define XNETWORK_READ_LOST_POLLS 4u
#endif

/** @brief 退役读槽的隔离时长（端口检查次数）。
 *
 * @details 被判丢包的槽先隔离，期间若真有迟到包到达，只会落在该退役槽上
 *          并被静默丢弃。隔离期满后可以回收复用——此时它的迟到包必然
 *          早已出队（端口每个事件循环批次至少被检查一次），
 *          这样"丢一次包就永久少一个槽"才不会累积成读通道停摆。 */
#ifndef XNETWORK_READ_QUARANTINE_POLLS
#define XNETWORK_READ_QUARANTINE_POLLS 64u
#endif

/** @brief 读请求挂起多久视为异常（毫秒）。
 *
 * @details 取 3s：比链路静默判线（6s）小一半，留出自救窗口；空闲链路上一次
 *          成功的读会刷新 `readArmedMs`，不会误触发。超时只做两件事：
 *            - `GetOverlappedResult(FALSE)` 为真 ⇒ 完成包真的丢了：接管内核侧
 *              结果、**退役该槽**、按正常读完成下派；
 *            - 否则若内核确有数据（`FIONREAD>0`）而读不完 ⇒ **只** `CancelIoEx`，
 *              重投等 ABORTED 完成包收尾。 */
#ifndef XNETWORK_READ_STALL_MS
#define XNETWORK_READ_STALL_MS 3000u
#endif

/**
 * @brief 单个异步写槽（一笔在途 WSASend 独占一份 OVERLAPPED + 缓冲）。
 *
 * @details 三个状态位严格区分，避免再次出现"布尔标志与内核状态脱节"：
 *          - `inUse`  ：槽已被某次写占用（缓冲归它所有）；
 *          - `armed`  ：**内核仍持有**本槽 OVERLAPPED（完成包未出队）；
 *          - `zombie` ：连接已拆，等 ABORTED 完成包收尾，此间不得复用。
 *
 *          MSDN 对 OVERLAPPED 的硬性约束：
 *            “A common mistake is to reuse an OVERLAPPED structure before
 *             the previous asynchronous operation has been completed. You
 *             should use a separate structure for each request.”
 *          WSASend 另要求缓冲在内核持有期间不得改动（“once the WSASend
 *          function is called, the system owns these buffers”）。两条约束都由
 *          本槽池在物理上保证。
 */
typedef struct XDeviceNetworkWriteSlot {
    XEventContext_IOCP ctx;                 /**< 本槽专用的 OVERLAPPED 载体。 */
    char buffer[XNETWORK_WRITE_BUFFER_SIZE];/**< 本槽专用发送缓冲（内核持有期不可动）。 */
    bool inUse;                             /**< 槽已被占用（含已完成待回收）。 */
    bool armed;                             /**< 内核仍持有该 OVERLAPPED。 */
    bool zombie;                            /**< 连接已拆，等 ABORTED 完成包收尾。 */
    uint64_t armedMs;                       /**< 提交时刻（单调毫秒，看门狗用）。 */
    uint32_t ioGen;                         /**< 提交时的连接代次（判完成包是否属于当前连接）。 */
} XDeviceNetworkWriteSlot;

/**
 * @brief 单个异步读槽（一笔在飞 WSARecv 独占一份 OVERLAPPED + 接收缓冲）。
 *
 * @details 三个状态位与写槽同口径：
 *          - `armed`  ：内核持有该 OVERLAPPED，完成包未出队；
 *          - `ready`  ：本槽已带数据（或 0 字节 FIN），等上层消费；
 *          - `retired`：完成包被判定丢失，本槽**永久退役**——内核里可能还有
 *                       一枚指向它的迟到完成包，退役后它不再承载任何操作，
 *                       迟到包到达时只被静默丢弃。
 */
typedef struct XDeviceNetworkReadSlot {
    XEventContext_IOCP ctx;                 /**< 本槽专用的 OVERLAPPED 载体。 */
    char buffer[XNETWORK_READ_BUFFER_SIZE]; /**< 本槽专用接收缓冲（内核持有期不可动）。 */
    bool armed;                             /**< 内核仍持有该 OVERLAPPED。 */
    bool ready;                             /**< 已完成，数据待上层消费。 */
    bool retired;                           /**< 完成包疑似丢失：隔离中，暂不投递。 */
    uint32_t retiredPollSeq;                /**< 进入隔离时的端口检查序号（判能否回收）。 */
    uint32_t ioGen;                         /**< 挂读时的连接代次（判完成包是否属于当前连接）。 */
    uint32_t generation;                    /**< 投递序号（诊断用）。 */
} XDeviceNetworkReadSlot;

typedef struct XDeviceNetworkContextWin32 {
    XDeviceNetworkContext base;             /**< 第一位：平台无关基类 (owner/xfd/notifiers) */
    SOCKET socket;                          ///< Windows SOCKET 句柄。
    HANDLE namedPipe;                       ///< Windows 命名管道句柄。

    /* ---------------- 读状态机（完成包驱动，唯一投递点） ---------------- */
    XDeviceNetworkReadSlot readSlots[XNETWORK_READ_SLOT_COUNT];
    int readArmedSlot;                      ///< 在飞读槽；-1 = 无在飞读。
    int readActiveSlot;                     ///< 当前呈现给上层的读槽；-1 = 无。
    int readNextSlot;                       ///< 轮转游标（让迟到包尽量落空）。
    uint64_t readArmedMs;                   ///< 在飞读提交时刻（看门狗）。
    uint32_t readArmedPollSeq;              ///< 挂读时的端口检查序号（判"真丢包"用）。
    bool isUdpRead;                         ///< 在飞读是否为数据报读（重挂用）。
    uint32_t readStallCount;                ///< 看门狗：内核有数据但读不完（已取消待重挂）。
    uint32_t readLostCount;                 ///< 看门狗：完成包确实丢失（已接管并退役槽）。
    uint32_t readAbortedCount;              ///< 主动取消产生的 ABORTED 完成包数。
    uint32_t ioGeneration;                  ///< 连接代次：每次换 I/O 句柄 +1（判完成包归属）。
    uint32_t readArmFailCount;              ///< 无空闲槽/提交失败次数（诊断）。

    /* 状态标志 */
    bool writePending;                      ///< 是否仍有写槽被占用（对外观测量）。
    bool connectPending;
    bool autoRead;
    bool isServer;                          ///< 是否为服务器套接字。
    bool acceptPending;                     ///< 是否有待处理的 Accept。
    bool acceptReady;                       ///< AcceptEx 是否已成功完成（可消费）。
    volatile LONG pendingIocpOperations;    ///< 在途 IOCP 计数: 发起 + 完成两侧同步递减。
    volatile LONG deletePending;            ///< 拆除已挂起标记, 防重复拆除竞态。

    /* ---------------- 异步写槽池（多写并发 + 完成包丢失隔离） -------------- */
    XDeviceNetworkWriteSlot writeSlots[XNETWORK_WRITE_SLOT_COUNT];
    int writeSlotCount;                     /**< 已占用槽数（诊断/看门狗用）。 */
    uint32_t writeStallCount;               ///< 累计被看门狗判定"卡死"的槽数。
    uint32_t writeAbortedCount;             ///< 取消产生的 ABORTED 完成包数。
    size_t writeDoneBytes[XNETWORK_WRITE_SLOT_COUNT]; ///< 已完成写的字节数 FIFO。
    int writeDoneHead;                      ///< FIFO 头（出队侧）。
    int writeDoneTail;                      ///< FIFO 尾（入队侧）。
    int writeDoneCount;                     ///< FIFO 中待呈现的完成笔数。
    size_t lastWriteBytes;                  ///< 当前 Write 事件对应的完成字节数。
    char writeStash[XNETWORK_WRITE_BUFFER_SIZE]; ///< 提交失败时保序暂存（最多一块）。
    size_t writeStashLen;                   ///< 暂存字节数；0 = 空。

    /* UDP 来源地址 */
    struct sockaddr_in6 fromAddr;
    int fromAddrLen;

    /* IOCP 上下文 */
    union {
        XEventContext_IOCP connectContext;  ///< 客户端连接上下文。
        XEventContext_IOCP acceptContext;   ///< 服务端 AcceptEx 上下文。
    };

    /* 待连接信息 */
    XHostAddress pendingPeerAddr;
    uint16_t pendingPeerPort;

    /* 服务器专用缓冲（读缓冲已下沉到各读槽，不再共享）。 */
    struct {
        char acceptBuffer[sizeof(struct sockaddr_in6) * 2 + 32]; ///< AcceptEx 地址双缓冲。
        SOCKET acceptSocket;                    ///< AcceptEx 落地的新套接字。
    };
} XDeviceNetworkContextWin32;


/* 便捷转换宏 */
#define W32(p) ((XDeviceNetworkContextWin32*)(p))

/** @brief 取当前单调毫秒（看门狗时间戳用；实现在文件后段）。 */
static uint64_t W32_nowMs(void);

/* 套接字事件处理（读看门狗补投递时复用正常完成路径；定义在文件后段）。 */
bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event);
/* 当前呈现的读完成字节数（Read 分支复用；定义在文件后段）。 */
size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd);

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

/* 完成包统一出口的前置声明（实现在 track/abandon 之后的位置：既要用它们
 * 维护的在途计数，也要保证槽状态迁移先于 `releaseContext` 执行）。 */

static bool w32_reapIocpCompletion(XDeviceNetworkContextWin32* p,
                                   XEventContext_IOCP* ctx);
static bool w32_armRead(XDeviceNetworkContext* priv);

/**
 * @brief 完成包出队回调（每笔操作都由 `trackSocketIocpOperation` 挂上）。
 *
 * @details 调用时机由 `processOneCompletion` 保证：`finishedBytes` /
 *          `nativeError` / `completed` 已写好、事件尚未入 CQ。因此这里是
 *          "槽状态迁移"的唯一正确时点，也是 ABORTED 包被拦下的唯一时点。
 *
 * @note 迁移必须在 `releaseContext` 之前完成——那是最后一次能安全访问 `p`
 *       的机会（在途计数归零时 p 会被释放）。
 */
static bool socketIocpOperationCompleted(XEventContext_IOCP* context, void* userData)
{
    XDeviceNetworkContextWin32* p = (XDeviceNetworkContextWin32*)userData;
    bool shouldDispatch;

    if (!p) return false;

    shouldDispatch = w32_reapIocpCompletion(p, context);
    shouldDispatch = shouldDispatch &&
                     InterlockedCompareExchange(&p->deletePending, 0, 0) == 0 &&
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

/* =========================================================================
 * 槽位归属与完成包裁决
 * ========================================================================= */

/**
 * @brief 按 OVERLAPPED 指针身份反查读槽下标。
 * @return 命中返回 [0, COUNT) 下标；不属于任何读槽返回 -1。
 * @note  **绝不用"事件掩码 + 提交顺序"推断是哪一笔操作完成了**：IOCP 只保证
 *        同一句柄的完成包大体有序，一旦出现乱序或迟到包，按顺序猜就会把
 *        完成记到错误的槽上（历史上正是这样让"归还写槽"落到别的槽，
 *        逐步把在途计数和缓冲所有权搞乱）。OVERLAPPED 指针是内核原样带回的，
 *        身份唯一且稳定。
 */
static int w32_readSlotIndex(const XDeviceNetworkContextWin32* p,
                             const XEventContext_IOCP* ctx)
{
    int i;
    if (!p || !ctx) return -1;
    for (i = 0; i < XNETWORK_READ_SLOT_COUNT; ++i)
        if (&p->readSlots[i].ctx == ctx) return i;
    return -1;
}

/** @brief 按 OVERLAPPED 指针身份反查写槽下标（同读槽口径）。 */
static int w32_writeSlotIndex(const XDeviceNetworkContextWin32* p,
                              const XEventContext_IOCP* ctx)
{
    int i;
    if (!p || !ctx) return -1;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i)
        if (&p->writeSlots[i].ctx == ctx) return i;
    return -1;
}

/** @brief 换用新 I/O 句柄时递增连接代次。
 *
 * @details 完成包归属**不能**用 SOCKET/HANDLE 数值比较：Windows 会复用句柄值，
 *          重连后新 socket 很可能拿到与旧 socket 相同的数值。若按数值判等，
 *          旧连接残留的 ABORTED 完成包会被当成"当前连接"的完成包，进而
 *          触发一次重挂 —— 而旧操作可能仍在内核里 ⇒ 同一 OVERLAPPED 二次
 *          投递 ⇒ 完成包丢失（实测跨进程联调里 750 字节批次反复丢）。
 *          代次号只在换句柄/拆连接时递增，天然免疫句柄复用。 */
static void w32_newIoGeneration(XDeviceNetworkContextWin32* p)
{
    if (!p) return;
    p->ioGeneration++;
}

/** @brief 当前 I/O 句柄（命名管道优先），用于 PEEK 复核等句柄级操作。 */
static intptr_t w32_currentIoHandle(const XDeviceNetworkContextWin32* p)
{
    if (!p) return -1;
    if (p->namedPipe != INVALID_HANDLE_VALUE) return (intptr_t)p->namedPipe;
    if (p->socket != INVALID_SOCKET) return (intptr_t)p->socket;
    return -1;
}

/** @brief 读通道此刻是否应该挂一笔读。
 *
 * @details 三个否定条件合起来表达同一个不变量：**同一句柄上永远只有一笔
 *          在飞读、且上一批数据必须已被上层消费**。
 *            - `readArmedSlot >= 0`：已有一笔在飞（无论它是否"看起来该
 *              完成了"）——绝不再投，这是根修的核心判据；
 *            - `readActiveSlot` 仍有 `ready` 数据：上层还没取走，投新读会
 *              覆盖它（单缓冲契约）；
 *            - 未连接 / 已关读通知：不需要读。
 */
static bool w32_readWanted(const XDeviceNetworkContextWin32* p)
{
    if (!p) return false;
    if (!p->base.m_connected || !p->autoRead) return false;
    if (p->readArmedSlot >= 0) return false;
    if (p->readActiveSlot >= 0 && p->readSlots[p->readActiveSlot].ready) return false;
    return true;
}

/** @brief 把一笔已完成写的字节数压入呈现队列（按完成顺序）。 */
static void w32_pushWriteDone(XDeviceNetworkContextWin32* p, size_t bytes)
{
    if (!p) return;
    if (p->writeDoneCount >= XNETWORK_WRITE_SLOT_COUNT) {
        /* 队列满（上层迟迟不来取）：丢最旧的一条，保持"最近的完成可见"。 */
        p->writeDoneHead = (p->writeDoneHead + 1) % XNETWORK_WRITE_SLOT_COUNT;
        p->writeDoneCount--;
    }
    p->writeDoneBytes[p->writeDoneTail] = bytes;
    p->writeDoneTail = (p->writeDoneTail + 1) % XNETWORK_WRITE_SLOT_COUNT;
    p->writeDoneCount++;
}

/** @brief 取走一笔已完成写的字节数；队列空返回 0。 */
static size_t w32_popWriteDone(XDeviceNetworkContextWin32* p)
{
    size_t bytes;
    if (!p || p->writeDoneCount <= 0) return 0;
    bytes = p->writeDoneBytes[p->writeDoneHead];
    p->writeDoneHead = (p->writeDoneHead + 1) % XNETWORK_WRITE_SLOT_COUNT;
    p->writeDoneCount--;
    return bytes;
}

/** @brief 归还写槽并把 writePending 收敛到真实占用情况。 */
static void w32_releaseWriteSlot(XDeviceNetworkContextWin32* p,
                                 int index)
{
    XDeviceNetworkWriteSlot* slot;
    if (!p || index < 0 || index >= XNETWORK_WRITE_SLOT_COUNT) return;
    slot = &p->writeSlots[index];
    if (slot->inUse) {
        slot->inUse = false;
        if (p->writeSlotCount > 0) p->writeSlotCount--;
    }
    slot->armed = false;
    slot->zombie = false;
    slot->armedMs = 0;
    p->writePending = (p->writeSlotCount > 0);
}

/**
 * @brief 把"连接已死"以关闭事件的形式投递给属主对象。
 *
 * @details 这里**不能**借用"0 字节读完成"那条路径：派发层对 0 字节读完成
 *          会做假 EOF 复核（`platformPeerReallyClosed`），而对端消失后 PEEK
 *          未必能拿到 0（实测踢线场景 `closed=0`）⇒ 关闭事件被丢弃 ⇒
 *          客户端永不重连。读操作**失败**本身就是连接死亡的直接证据，
 *          不需要也不应该再复核。
 * @note 用属主指针直接投递，不经 XFd 反查：XFd 是池槽位号，连接拆除后
 *       可能已被新对象复用（历史串包/误杀事故的同源原因）。
 */
static void w32_postSockClose(XFd xfd, void* owner)
{
    XEventSockClose* closeEvent;
    if (!owner) return;
    closeEvent = XEventSockClose_create(xfd);
    if (!closeEvent) return;
    ((XEvent*)closeEvent)->m_posted = true;
    ((XEvent*)closeEvent)->m_spontaneous = true;
    XCoreApplication_postEvent((XObject*)owner, (XEvent*)closeEvent,
                               XEVENT_PRIORITY_NORMAL);
}

/**
 * @brief 一条完成包的状态裁决（槽迁移 + 是否下派语义事件）。
 *
 * @return true=下派为上层事件；false=内部消化（取消/迟到/退役）。
 */
static bool w32_reapIocpCompletion(XDeviceNetworkContextWin32* p,
                                   XEventContext_IOCP* ctx)
{
    int index;

    if (!p || !ctx) return false;

    /* ---------------- 读完成 ---------------- */
    index = w32_readSlotIndex(p, ctx);
    if (index >= 0) {
        XDeviceNetworkReadSlot* slot = &p->readSlots[index];
        bool sameSocket = (slot->ioGen == p->ioGeneration);



        /* 不变量 B：完成包已出队 ⇒ 该 OVERLAPPED 现在才可以再次投递。 */
        slot->armed = false;
        if (p->readArmedSlot == index) {
            p->readArmedSlot = -1;
            p->readArmedMs = 0;
        }

        /* 退役槽上的迟到包：只回收槽，不影响任何状态、不产生事件。 */
        if (slot->retired) return false;

        if (!ctx->completed) {
            if (ctx->nativeError == ERROR_OPERATION_ABORTED) {
                /* 不变量 D：取消是内部控制流，绝不下派成读事件。
                 * 不变量 C：重投只能发生在"这个 ABORTED 包出队之后"——就是现在。 */
                p->readAbortedCount++;
                /* 槽此刻已回收（armed=false）⇒ 可以安全地为**当前**连接重挂。
                 * 旧连接的取消包走同一分支：重挂目标是 p->socket 的当前句柄，
                 * 与取消掉的那笔无关（不变量 C 的落地处）。 */
                if (!slot->ready && w32_readWanted(p))
                    (void)w32_armRead(&p->base);
                return false;
            }
            /* 真错误（RST / 连接被中止）：**连接已死**。
             *
             * 绝不按"读到 0 字节"处理：那条路会被假 EOF 复核拦截（PEEK 拿不到
             * 0 时把事件丢掉），实测后果是对端踢线后本端永不重连
             * （XLink 踢线用例 10 项失败）。读失败是连接死亡的直接证据，
             * 直接投递关闭事件。 */
            if (!sameSocket) {
                slot->ready = false;
                return false;
            }
            if (p->isUdpRead) {
                /* 数据报错误（如 ICMP 不可达）不等于流关闭：按 0 字节完成
                 * 交给上层判定，保持既有 UDP 语义。 */
                slot->ctx.finishedBytes = 0;
                slot->ready = true;
                p->readActiveSlot = index;
                return true;
            }
            slot->ready = false;
            slot->armed = false;
            p->readActiveSlot = -1;
            w32_postSockClose(ctx->base.fd, p->base.m_owner);
            return false;
        }

        if (!sameSocket) {
            /* 旧连接的迟到完成（重连后才到）：数据属于已废弃的连接，丢弃。 */
            slot->ready = false;
            return false;
        }


        /* [假 EOF 自愈] 0 字节读完成，但对端其实健在（内核/驱动误报）：
         * 绝不能把它交给上层（上层会按 FIN 断开），也不能让"派发层丢弃该
         * 事件"成为读通道的终点——就地重挂，读通道继续工作。
         * 数据报的 0 字节是合法空报文，不做该判定。 */
        if (ctx->finishedBytes == 0 && !p->isUdpRead
            && !XAbstractNetIoRing_platformPeerReallyClosed(
                   (int)w32_currentIoHandle(p))) {
            slot->ready = false;
            p->readActiveSlot = -1;
            if (w32_readWanted(p)) (void)w32_armRead(&p->base);
            return false;
        }

        slot->ready = true;
        p->readActiveSlot = index;
        return true;
    }

    /* ---------------- 写完成 ---------------- */
    index = w32_writeSlotIndex(p, ctx);
    if (index >= 0) {
        XDeviceNetworkWriteSlot* slot = &p->writeSlots[index];
        bool sameSocket = (slot->ioGen == p->ioGeneration);
        bool aborted = (!ctx->completed &&
                        ctx->nativeError == ERROR_OPERATION_ABORTED);
        size_t bytes = ctx->completed ? ctx->finishedBytes : 0;

        w32_releaseWriteSlot(p, index);

        if (aborted) {
            /* 取消写不是语义事件：若下派，上层会按 FIFO 里的陈旧字节数发
             * bytesWritten 信号（XLink 进度对账错乱的老故障面）。 */
            p->writeAbortedCount++;
            return false;
        }
        if (!sameSocket) return false;

        w32_pushWriteDone(p, bytes);
        return true;
    }

    /* ---------------- 连接完成 ---------------- */
    if (ctx == &p->connectContext) {
        p->connectPending = false;
        if (!ctx->completed && ctx->nativeError == ERROR_OPERATION_ABORTED)
            return false;   /* 主动断开导致的取消：不是连接失败事件。 */
        return true;
    }

    /* ---------------- 接受完成 ---------------- */
    if (ctx == &p->acceptContext) {
        if (!ctx->completed && ctx->nativeError == ERROR_OPERATION_ABORTED) {
            /* 监听 socket 被拆：回收等待状态，且不把取消当"有新连接"。 */
            p->acceptPending = false;
            p->acceptReady = false;
            return false;
        }
        p->acceptReady = ctx->completed;
        return true;
    }

    return false;
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
    /* 读状态机的"无在飞/无呈现"必须显式初始化：槽数组本身已被 XCalloc 清零，
     * 但两个下标是 -1 语义，0 会被误读成"槽 0 正在飞"⇒ 首笔读永远投不出去
     * （实测：连接成功、写正常、对端零接收）。 */
    p->readArmedSlot = -1;
    p->readActiveSlot = -1;
    p->readNextSlot = 0;
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

/** @brief 本地提交失败时就地回滚一次读投递（此时一定不会有完成包）。 */
static void w32_rollbackReadArm(XDeviceNetworkContextWin32* p, int index,
                                int nativeError)
{
    XDeviceNetworkReadSlot* slot;
    if (!p || index < 0 || index >= XNETWORK_READ_SLOT_COUNT) return;
    slot = &p->readSlots[index];
    slot->armed = false;
    if (p->readArmedSlot == index) {
        p->readArmedSlot = -1;
        p->readArmedMs = 0;
    }
    abandonSocketIocpOperation(p, &slot->ctx);
    p->readArmFailCount++;
    if (nativeError != 0)
        XPrintf("[XNetwork-W32] 读提交失败 err=%d（无完成包，已回滚）；累计 %u 次\n",
                nativeError, (unsigned)p->readArmFailCount);
}

/* 丢弃呈现槽（定义在补投递之后：补投递失败时要回退到"丢这批、保通道"）。 */
static void xw32_dropReadSlot(XDeviceNetworkContextWin32* p);

/** @brief 把"内核已完成但完成包丢失"的事实按正常读完成投递回事件环。 */
static void w32_postReadReady(XFd xfd, XDeviceNetworkContextWin32* p, size_t bytes)
{
    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    XAbstractNetIoRing_CQEntry entry;

    if (!ring) {
        xw32_dropReadSlot(p);
        return;
    }
    memset(&entry, 0, sizeof(entry));
    entry.m_fd = xfd;
    entry.m_osFd = (int)p->socket;
    entry.m_bytes = (uint32_t)bytes;
    entry.m_error = 0;
    entry.m_events = XSocketActType_Read;
    entry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;
    entry.m_fdType = XFd_type(xfd);
    if (!XAbstractNetIoRing_pushCompletion(ring, &entry)) {
        /* 事件环不可用/队列满：宁可丢这一批缓冲，也不能让读通道永久停摆。 */
        xw32_dropReadSlot(p);
        p->readArmFailCount++;
        XPrintf("[XNetwork-W32] 补投递失败（事件环不可用），丢弃该批 %u 字节\n",
                (unsigned)bytes);
    }
}

/** @brief 丢弃当前呈现的读槽（不改变"在飞"关系），并尝试重挂读。 */
static void xw32_dropReadSlot(XDeviceNetworkContextWin32* p)
{
    if (!p) return;
    if (p->readActiveSlot >= 0) {
        p->readSlots[p->readActiveSlot].ready = false;
        p->readActiveSlot = -1;
    }
}

/**
 * @brief 提交一笔异步读（读通道**唯一**投递入口）。
 *
 * @details 根修要点：在飞判据由 `p->readArmedSlot` 表达——它只在完成包出队
 *          （`w32_reapIocpCompletion`）时才放回 -1，而不是由"上层希望有一笔
 *          读"的布尔愿望位表达。因此无论调用时序如何（ContinueRead、重连、
 *          看门狗、ABORTED 收尾），本函数都不可能对同一份 OVERLAPPED 二次投递。
 *
 * @param priv 套接字上下文。
 * @return true=已提交（含同步完成：overlapped 套接字的同步完成同样会排队一个
 *         完成包）；false=当前不该读、无空闲槽或提交失败。
 */
static bool w32_armRead(XDeviceNetworkContext* priv)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    XDeviceNetworkReadSlot* slot = NULL;
    int index = -1;
    int i;
    bool isUdp;

    if (!p || !w32_has_stream_handle(p)) return false;

    if (!w32_readWanted(p)) return false;   /* 不变量 A/B 的守门人 */


    /* 轮转选槽：从上次用过的下一个开始，让"退役槽上的迟到包"尽量落在空槽。 */
    for (i = 0; i < XNETWORK_READ_SLOT_COUNT; ++i) {
        int k = (p->readNextSlot + i) % XNETWORK_READ_SLOT_COUNT;
        XDeviceNetworkReadSlot* cand = &p->readSlots[k];
        if (cand->retired || cand->armed || cand->ready) continue;
        slot = cand;
        index = k;
        break;
    }
    if (!slot) {
        /* 全被占用：回收**隔离期满**的退役槽。隔离期内端口已被检查过
         * `XNETWORK_READ_QUARANTINE_POLLS` 次，指向该槽的迟到包若存在
         * 必然早已出队并被丢弃，复用是安全的。 */
        uint32_t seq = XNetIoRingWin32_pollSeq();
        for (i = 0; i < XNETWORK_READ_SLOT_COUNT; ++i) {
            XDeviceNetworkReadSlot* cand = &p->readSlots[i];
            if (!cand->retired || cand->armed || cand->ready) continue;
            if ((uint32_t)(seq - cand->retiredPollSeq)
                    < XNETWORK_READ_QUARANTINE_POLLS)
                continue;
            cand->retired = false;
            cand->retiredPollSeq = 0;
            slot = cand;
            index = i;
            break;
        }
    }
    if (!slot) {
        /* 仍在隔离期：不是错误，等下一批完成包回收后再试。 */
        p->readArmFailCount++;
        return false;
    }

    isUdp = p->isUdpRead;
    p->readNextSlot = (index + 1) % XNETWORK_READ_SLOT_COUNT;


    memset(&slot->ctx, 0, sizeof(XEventContext_IOCP));
    slot->ctx.base.type = XEventContextType_Type_Socket;
    slot->ctx.base.fd = priv->m_base.m_fd;
    slot->ctx.socket = XSocketDescriptor_fromIntptr(
        p->namedPipe != INVALID_HANDLE_VALUE ? (intptr_t)p->namedPipe
                                             : (intptr_t)p->socket);
    slot->ctx.eventMask = FD_READ;

    /* 先记账再提交：同步完成时完成包同样会排队，出队前它必须已被认领。 */
    slot->armed = true;
    slot->generation++;
    slot->ioGen = p->ioGeneration;
    p->readArmedSlot = index;
    p->readArmedMs = W32_nowMs();
    p->readArmedPollSeq = XNetIoRingWin32_pollSeq();

    trackSocketIocpOperation(p, &slot->ctx);

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;
        if (ReadFile(p->namedPipe, slot->buffer, XNETWORK_READ_BUFFER_SIZE,
                     &bytesTransferred, (OVERLAPPED*)&slot->ctx))
            return true;
        if (GetLastError() == ERROR_IO_PENDING)
            return true;
        w32_rollbackReadArm(p, index, (int)GetLastError());
        return false;
    }

    {
        WSABUF buf;
        DWORD flags = 0;
        int result;

        buf.buf = slot->buffer;
        buf.len = XNETWORK_READ_BUFFER_SIZE;

        /* [官方口径] lpOverlapped 非 NULL 时 lpNumberOfBytesRecvd **必须**传
         * NULL，否则完成字节数可能不可靠；完成后取 GQCS 的输出参数。 */
        if (isUdp) {
            p->fromAddrLen = sizeof(p->fromAddr);
            result = WSARecvFrom(p->socket, &buf, 1, NULL, &flags,
                (struct sockaddr*)&p->fromAddr, &p->fromAddrLen,
                (OVERLAPPED*)&slot->ctx, NULL);
        } else {
            result = WSARecv(p->socket, &buf, 1, NULL, &flags,
                (OVERLAPPED*)&slot->ctx, NULL);
        }
        if (result == 0) return true;                    /* 同步完成，包照常排队 */
        if (WSAGetLastError() == WSA_IO_PENDING) return true;
        w32_rollbackReadArm(p, index, (int)WSAGetLastError());
        return false;
    }
}

/**
 * @brief 读挂起看门狗（**只观察与取消，绝不抢占在飞 OVERLAPPED**）。
 *
 * @details 三态判定，两条证据互相独立：
 *          1. `GetOverlappedResult(FALSE)` 为真 ⇒ 操作已完成、完成包未投递。
 *             接管结果、把该槽**永久退役**（内核里可能还有一枚迟到包，退役后
 *             它不会再落到任何新操作上），再按正常读完成补投递。
 *          2. `FIONREAD > 0` 而操作未完成 ⇒ 数据在内核、读不回来。此时**只**
 *             `CancelIoEx`：重投由该操作的 ABORTED 完成包收尾后自动发生
 *             （`w32_reapIocpCompletion`）。旧实现在这里"取消 + 立即用同一
 *             OVERLAPPED 重投"，正是完成包丢失的直接来源。
 *          3. 两者都不成立 ⇒ 对端确实没发，只刷新时间戳。
 */
static void w32_checkReadStall(XFd xfd, XDeviceNetworkContext* priv)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    XDeviceNetworkReadSlot* slot;
    int index;
    DWORD got = 0;
    u_long readable = 0;

    if (!p) return;
    index = p->readArmedSlot;
    if (index < 0 || index >= XNETWORK_READ_SLOT_COUNT) return;
    slot = &p->readSlots[index];
    if (w32_currentIoHandle(p) == -1) return;

    /* 证据一：完成包丢失。
     *
     * 前提是"包确实不会来了"：自挂读起完成端口已被检查过
     * `XNETWORK_READ_LOST_POLLS` 次而该包仍未出队。只凭
     * `GetOverlappedResult` 返回 TRUE 就判丢包是不够的——内核已完成、
     * 包还在端口里排队时它同样返回 TRUE（GUI 一次切页/重绘就能让一个
     * 投递批次耗时数秒，实测因此连判 3 次、把 3 个读槽全部退役，
     * 读通道随之停摆）。 */
    if ((uint32_t)(XNetIoRingWin32_pollSeq() - p->readArmedPollSeq)
            >= XNETWORK_READ_LOST_POLLS
        && GetOverlappedResult((HANDLE)w32_currentIoHandle(p),
                               (OVERLAPPED*)&slot->ctx, &got, FALSE)) {

        slot->retiredPollSeq = XNetIoRingWin32_pollSeq();
        slot->retired = true;      /* 隔离：迟到包今后只落在这个退役槽上 */
        slot->armed = false;
        p->readArmedSlot = -1;
        p->readArmedMs = 0;
        p->readLostCount++;
        slot->ctx.finishedBytes = got;
        slot->ctx.completed = TRUE;
        slot->ctx.nativeError = ERROR_SUCCESS;
        slot->ready = true;
        p->readActiveSlot = index;
        XPrintf("[XNetwork-W32] 读停摆=完成包丢失（已结束 %lu 字节），"
                "已接管结果并退役该读槽；累计 %u 次\n",
                (unsigned long)got, (unsigned)p->readLostCount);
        w32_postReadReady(xfd, p, got);
        return;
    }

    /* 证据二：数据在内核，但挂起的读不完成。 */
    if (p->socket != INVALID_SOCKET
        && ioctlsocket(p->socket, FIONREAD, &readable) == 0 && readable > 0) {
        if (CancelIoEx((HANDLE)w32_currentIoHandle(p), (OVERLAPPED*)&slot->ctx)) {
            p->readStallCount++;
            p->readArmedMs = W32_nowMs();   /* 等 ABORTED 包收尾后自动重挂 */
            XPrintf("[XNetwork-W32] 读停摆=数据在内核但读未完成（内核待读=%lu 字节），"
                    "已取消，待 ABORTED 完成包后重挂；累计 %u 次\n",
                    (unsigned long)readable, (unsigned)p->readStallCount);
        } else if (GetLastError() == ERROR_NOT_FOUND) {
            /* 官方语义：ERROR_NOT_FOUND = 操作其实已完成，完成包在途。
             * 什么都不做，等它到达即可。 */
            p->readArmedMs = W32_nowMs();
        }
        return;
    }

    /* 证据三：内核无数据 ⇒ 对端确实没发。 */
    p->readArmedMs = W32_nowMs();
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
/** @brief 是否还有可受理新写的槽（未被占用、且 OVERLAPPED 未被内核持有）。 */
static bool w32_hasFreeWriteSlot(const XDeviceNetworkContextWin32* p)
{
    int i;
    if (!p) return false;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        const XDeviceNetworkWriteSlot* slot = &p->writeSlots[i];
        if (!slot->inUse && !slot->armed && !slot->zombie) return true;
    }
    return false;
}

/**
 * @brief 写槽挂起超时的止损动作（**只取消，不抢槽**）。
 *
 * @details 旧实现在这里把槽直接标成空闲，等于把一份**内核仍可能持有**的
 *          OVERLAPPED 放回池子——下一次写立刻复用它，正是"完成包丢失"
 *          的另一条制造路径（与读方向同源，见文件头不变量 A/B）。
 *
 *          现在的动作只有一个：对超时槽发 `CancelIoEx`。槽的释放严格由
 *          该笔操作的 ABORTED 完成包在 `w32_reapIocpCompletion` 里完成。
 *          若取消返回 `ERROR_NOT_FOUND`（官方语义：操作已完成、完成包在途），
 *          同样什么都不做，等包到达。
 *
 * @note 计数进 `writeStallCount` 保持可观测；不改任何槽状态。
 */
static void w32_reapStalledWriteSlots(XDeviceNetworkContextWin32* p)
{
    uint64_t now;
    int i;

    if (!p) return;
    if (w32_hasFreeWriteSlot(p)) return;   /* 还有空槽：无需干预 */

    now = W32_nowMs();
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        XDeviceNetworkWriteSlot* slot = &p->writeSlots[i];
        if (!slot->armed) continue;
        if (slot->armedMs != 0 && now - slot->armedMs < XNETWORK_WRITE_SLOT_TIMEOUT_MS)
            continue;
        if (p->socket == INVALID_SOCKET) continue;
        if (CancelIoEx((HANDLE)p->socket, (OVERLAPPED*)&slot->ctx)) {
            slot->armedMs = now;   /* 计时重置：等 ABORTED 完成包收尾 */
            p->writeStallCount++;
            XPrintf("[XNetwork-W32] 写槽 %d 超过 %ums 未完成，已取消；"
                    "待 ABORTED 完成包回收（累计 %u 次）\n", i,
                    (unsigned)XNETWORK_WRITE_SLOT_TIMEOUT_MS,
                    (unsigned)p->writeStallCount);
        }
    }
}

/** @brief 找可用的写槽；**被内核持有（armed）或等收尾（zombie）的槽一律跳过**。 */
static XDeviceNetworkWriteSlot* w32_allocWriteSlot(XDeviceNetworkContextWin32* p)
{
    int i;
    if (!p) return NULL;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        XDeviceNetworkWriteSlot* slot = &p->writeSlots[i];
        if (slot->inUse || slot->armed || slot->zombie) continue;
        slot->inUse = true;
        slot->armed = false;
        slot->zombie = false;
        slot->armedMs = 0;
        p->writeSlotCount++;
        p->writePending = true;
        return slot;
    }
    return NULL;
}

/** @brief 提交失败（不会有完成包）时就地回滚槽与在途计数。 */
static void w32_rollbackWrite(XDeviceNetworkContextWin32* p, int index, int err)
{
    if (!p || index < 0 || index >= XNETWORK_WRITE_SLOT_COUNT) return;
    abandonSocketIocpOperation(p, &p->writeSlots[index].ctx);
    w32_releaseWriteSlot(p, index);
    if (err != 0)
        XPrintf("[XNetwork-W32] 写提交失败 err=%d（无完成包，已回滚槽 %d）\n",
                err, index);
}

/** @brief 连接建立/断开时收敛写槽：在飞槽打 zombie，其余立即释放。 */
static void w32_resetWriteSlots(XDeviceNetworkContextWin32* p)
{
    int i;
    if (!p) return;
    for (i = 0; i < XNETWORK_WRITE_SLOT_COUNT; ++i) {
        if (p->writeSlots[i].armed) {
            /* 内核仍持有：只标记"连接已拆"，等 ABORTED 完成包回收。
             * 绝不在此时清 armed —— 那会让下一次写复用这份 OVERLAPPED。 */
            p->writeSlots[i].zombie = true;
            continue;
        }
        w32_releaseWriteSlot(p, i);
    }
    p->writeDoneHead = 0;
    p->writeDoneTail = 0;
    p->writeDoneCount = 0;
    p->lastWriteBytes = 0;
    p->writeStashLen = 0;
    p->writePending = (p->writeSlotCount > 0);
}

/**
 * @brief 提交一次异步写（每次独占一份 OVERLAPPED + 独立缓冲）。
 * @return 已提交（含同步完成）返回 true；无可用槽/参数非法/提交失败返回 false。
 * @note  同步完成时 `WSASend` 返回 0，但 overlapped 套接字**仍会**投递完成包
 *        （本后端未设置 FILE_SKIP_COMPLETION_PORT_ON_SUCCESS），因此槽必须
 *        一直保持"内核持有"直到完成包出队；这里绝不重复投递完成包。
 */
static bool startAsyncWrite(XDeviceNetworkContext* priv, const void* data, int64_t len,
    const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    XDeviceNetworkWriteSlot* slot;
    int index;

    if (!p || !w32_has_stream_handle(p)) return false;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return false;
    if (!data) return false;

    slot = w32_allocWriteSlot(p);
    if (!slot) {
        w32_reapStalledWriteSlots(p);
        slot = w32_allocWriteSlot(p);
    }
    if (!slot) return false;   /* 真的写不动：调用方把数据留在 ring buffer 等下一轮。 */

    index = (int)(slot - p->writeSlots);
    memcpy(slot->buffer, data, (size_t)len);

    memset(&slot->ctx, 0, sizeof(XEventContext_IOCP));
    slot->ctx.base.type = XEventContextType_Type_Socket;
    slot->ctx.base.fd = priv->m_base.m_fd;
    slot->ctx.socket = XSocketDescriptor_fromIntptr(
        p->namedPipe != INVALID_HANDLE_VALUE ? (intptr_t)p->namedPipe : (intptr_t)p->socket);
    slot->ctx.eventMask = FD_WRITE;
    slot->ctx.finishedBytes = len;

    slot->armed = true;        /* 内核即将持有：在完成包出队前不得复用 */
    slot->armedMs = W32_nowMs();
    slot->ioGen = p->ioGeneration;
    trackSocketIocpOperation(p, &slot->ctx);

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;
        if (WriteFile(p->namedPipe, slot->buffer, (DWORD)len, &bytesTransferred,
                      (OVERLAPPED*)&slot->ctx))
            return true;
        if (GetLastError() == ERROR_IO_PENDING)
            return true;
        w32_rollbackWrite(p, index, (int)GetLastError());
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
            xw32_mapV4Dest(p->socket, destAddr, destPort, &dest, &destLen);
            result = WSASendTo(p->socket, &buf, 1, NULL, 0,
                (struct sockaddr*)&dest, destLen,
                (OVERLAPPED*)&slot->ctx, NULL);
        }
        else {
            result = WSASend(p->socket, &buf, 1, NULL, 0,
                (OVERLAPPED*)&slot->ctx, NULL);
        }

        if (result == 0) return true;                       /* 同步完成，包照常排队 */
        if (WSAGetLastError() == WSA_IO_PENDING) return true;
        w32_rollbackWrite(p, index, (int)WSAGetLastError());
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
    w32_newIoGeneration(p);

    u_long mode = 1;
    ioctlsocket(p->socket, FIONBIO, &mode);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    /* [Qt 6.8 对齐] bind(Any) 双栈：AF_INET6 + IPV6_V6ONLY=0。 */
    if (af == AF_INET6) {
        int ipv6only = 0;
        setsockopt(p->socket, IPPROTO_IPV6, IPV6_V6ONLY,
                   (char*)&ipv6only, sizeof(ipv6only));
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

    if (sockType == XDeviceNetwork_Udp) {
        p->isUdpRead = true;
        (void)w32_armRead(priv);
    }
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
    w32_newIoGeneration(p);

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
        p->isUdpRead = true;
        (void)w32_armRead(priv);
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
    w32_newIoGeneration(p);
    p->base.m_connected = true;
    p->connectPending = false;
    p->isUdpRead = false;
    w32_resetWriteSlots(p);
    (void)w32_armRead(priv);
    return true;
}

void XDeviceNetwork_socketDisconnect(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return;
    XDeviceNetworkContextWin32* p = W32(priv);
    int i;

    /* 顺序不可倒：**先** CancelIoEx **再** closesocket。句柄一经关闭就可能
     * 被系统立即复用，之后再对旧句柄发取消会打到别人的连接上。
     *
     * 取消产生的 ABORTED 完成包由 `w32_reapIocpCompletion` 回收槽；
     * 这里**绝不**把在飞槽标空闲（不变量 B/C），否则重连会复用一份
     * 内核仍持有的 OVERLAPPED —— 那正是"重连后只发不收"的成因。 */
    if (p->socket != INVALID_SOCKET) {
        (void)CancelIoEx((HANDLE)p->socket, NULL);
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
    }
    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        (void)CancelIoEx(p->namedPipe, NULL);
        CloseHandle(p->namedPipe);
        p->namedPipe = INVALID_HANDLE_VALUE;
    }
    w32_newIoGeneration(p);   /* 旧连接残留的完成包从此不再匹配 */
    p->base.m_connected = false;
    p->connectPending = false;
    p->acceptPending = false;
    p->acceptReady = false;

    /* 读：在飞槽保持 armed（等 ABORTED 包回收），呈现槽直接作废。 */
    p->readActiveSlot = -1;
    p->readArmedMs = 0;
    for (i = 0; i < XNETWORK_READ_SLOT_COUNT; ++i)
        p->readSlots[i].ready = false;

    w32_resetWriteSlots(p);
    p->writePending = (p->writeSlotCount > 0);
}

int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv || !buf || len <= 0) return -1;
    XDeviceNetworkContextWin32* p = W32(priv);
    if (!w32_has_stream_handle(p)) return -1;

    (void)sockType;   /* 重挂用 p->isUdpRead（挂读时的真实类型），不由读调用改判。 */

    /* [读挂起看门狗] 每次应用层读（会话 pump 的必经路径）检查一次在飞读的
     * 挂起时长；超阈值只做"观察 + 取消"，绝不在原地复用 OVERLAPPED 重投
     * （旧实现在这里取消后立刻重投，是完成包丢失的直接来源，见
     * `w32_checkReadStall` 与 `verify/iocp_overlapped_alias_repro.c`）。 */
    if (p->readArmedSlot >= 0 && p->readArmedMs != 0
        && (W32_nowMs() - p->readArmedMs) > XNETWORK_READ_STALL_MS) {
        w32_checkReadStall(xfd, priv);
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

/**
 * @brief 按序推进写积压（holding pen → ring buffer）。
 *
 * @details 写方向的顺序只有一条硬约束：**先到的字节必须先提交**。为此：
 *          - `writeStash` 保存"上轮因提交失败而没发出去的那一块"，永远先发；
 *          - 只有 stash 排空后才从 ring buffer 取下一块；
 *          - 取出的块提交失败时放回 stash（保持它仍排在所有后续数据之前），
 *            绝不"写回 ring 尾部"——那会让帧的字节流乱序（旧实现如此）。
 * @note 每个循环最多占一个写槽；在途多笔由 TCP 保证按提交顺序交付。
 */
static void w32_pumpWrites(XDeviceNetworkContext* priv, void* ringBuffer, bool isUdp)
{
    XDeviceNetworkContextWin32* p = W32(priv);
    struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;

    if (!p) return;

    if (p->writeStashLen > 0) {
        if (!startAsyncWrite(priv, p->writeStash, (int64_t)p->writeStashLen,
                             NULL, 0, isUdp))
            return;
        p->writeStashLen = 0;
    }

    while (rb && XRingBuffer_available(rb) > 0 && w32_hasFreeWriteSlot(p)) {
        char temp[XNETWORK_WRITE_BUFFER_SIZE];
        size_t chunk = XRingBuffer_available(rb);
        size_t got;
        if (chunk > sizeof(temp)) chunk = sizeof(temp);
        got = XRingBuffer_read(rb, temp, chunk);
        if (got == 0) break;
        if (!startAsyncWrite(priv, temp, (int64_t)got, NULL, 0, isUdp)) {
            memcpy(p->writeStash, temp, got);
            p->writeStashLen = got;
            break;
        }
    }
}

int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, const XHostAddress* destAddr,
    uint16_t destPort, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
    XDeviceNetworkContextWin32* p;
    bool isUdp;

    if (!priv || !buf || len <= 0) return -1;
    p = W32(priv);
    if (!w32_has_stream_handle(p)) return -1;
    isUdp = (sockType == XDeviceNetwork_Udp);

    /* 1. 先把已有积压按序推进（含上轮失败的那一块）。 */
    w32_pumpWrites(priv, ringBuffer, isUdp);

    /* 2. 前面仍有未发出的字节 ⇒ 本次数据只能排队，保证字节流顺序。 */
    if (p->writeStashLen > 0
        || (rb && XRingBuffer_available(rb) > 0)
        || !w32_hasFreeWriteSlot(p)) {
        if (!rb) return -1;
        return (int64_t)XRingBuffer_write(rb, buf, (size_t)len);
    }

    /* 3. 空闲通道：直接提交本次数据。 */
    if (len <= XNETWORK_WRITE_BUFFER_SIZE) {
        if (startAsyncWrite(priv, buf, len, destAddr, destPort, isUdp))
            return len;
        if (!rb) return -1;
        return (int64_t)XRingBuffer_write(rb, buf, (size_t)len);
    }

    /* 4. 大于单槽缓冲：整块入队，由 pump 分片连续提交（顺序天然正确）。 */
    if (!rb) {
        if (startAsyncWrite(priv, buf, XNETWORK_WRITE_BUFFER_SIZE,
                            destAddr, destPort, isUdp))
            return XNETWORK_WRITE_BUFFER_SIZE;
        return -1;
    }
    {
        size_t written = XRingBuffer_write(rb, buf, (size_t)len);
        w32_pumpWrites(priv, ringBuffer, isUdp);
        return (int64_t)written;
    }
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
        /* 在飞/呈现状态全部由完成包驱动：这里只回报"本次完成是否带数据"。
         * 不再清"读在飞"标志——它只由 `w32_reapIocpCompletion` 迁移。 */

        return XDeviceNetwork_socketReadFinishedBytes(xfd) > 0;
    }
    if (sockAct->actType & XSocketActType_Write) {
        /* 写槽的归还已在完成回调里按 OVERLAPPED 身份完成（不再按提交顺序
         * 猜"最早的在途槽"）。这里只把本次完成的字节数呈现给上层。 */
        p->lastWriteBytes = w32_popWriteDone(p);
        return true;
    }
    if (sockAct->actType & XSocketActType_Connect) {
        p->connectPending = false;
        /* 检查连接是否真正成功（ConnectEx 失败时 SO_ERROR 非零） */
        int soError = 0;
        int soLen = sizeof(soError);
        if (p->socket != INVALID_SOCKET)
            getsockopt(p->socket, SOL_SOCKET, SO_ERROR, (char*)&soError, &soLen);
        else
            soError = WSAENOTCONN;
        if (soError == 0) {
            if (setsockopt(p->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0) {
                p->base.m_connected = true;
                p->isUdpRead = false;
                syncSocketEndpoints(priv);
            } else {
                p->base.m_connected = false;
            }
        } else {
            p->base.m_connected = false;
        }
        return true;
    }
    /* [accept 完成] AcceptEx 完成包到达时**不得**清 acceptPending。
     *
     * 这个标志的语义是"监听 socket 上已挂了一个 AcceptEx"，只有消费方
     * （`XDeviceNetwork_serverGetAcceptedSocket` 取走 acceptSocket 后）
     * 才能清它。若在本函数里清掉，`serverAccept` 的守卫就失效，会在旧
     * AcceptEx 尚未消费时再挂一个 ⇒ 同一个监听 socket 上两个 AcceptEx
     * 同时在飞，第二个完成时 `p->acceptSocket` 被覆盖（第一个 socket
     * 泄漏且仍关联 IOCP）⇒ 实测表现为「客户端只连一次，服务端却反复
     * accept 出新会话」。
     *
     * 这里只回报"有完成可消费"，由上层决定何时取走、何时重挂。 */
    if (sockAct->actType & XSocketActType_Accept) {
        return p->acceptReady && p->acceptSocket != INVALID_SOCKET;
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
    w32_newIoGeneration(p);
    if (!iocp_assoc(p->socket, priv->m_owner)) { p->socket = INVALID_SOCKET; return false; }
    p->base.m_connected = (state == 3);
    p->isUdpRead = false;
    if (p->base.m_connected) {
        p->autoRead = true;
        (void)w32_armRead(priv);   /* 在飞判据仍由 readArmedSlot 把关 */
    }
    return true;
}
/**
 * @brief 接管一个已存在的监听套接字（服务端描述符注入路径）。
 * @details 与 `socketSetDescriptor` 的区别只在语义：监听套接字不挂读、只挂
 *          AcceptEx。`isServer` 置位后本 context 不再允许二次接管。
 */
bool XDeviceNetwork_serverSetDescriptor(XFd deviceFd, intptr_t fd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextWin32* p;

    if (!priv || fd == -1) return false;
    p = W32(priv);
    if (p->socket != INVALID_SOCKET || p->isServer) return false;
    if (!iocp_assoc((SOCKET)fd, priv->m_owner)) return false;

    p->socket = (SOCKET)fd;
    w32_newIoGeneration(p);
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
    XDeviceNetworkContextWin32* p;
    if (!priv) return NULL;
    p = W32(priv);
    /* 呈现的永远是"当前这批完成数据所在的那一个读槽"。上层只在
     * `ReadFinishedBytes > 0` 时读它，两者由同一次完成包同时设定。 */
    if (p->readActiveSlot >= 0 && p->readActiveSlot < XNETWORK_READ_SLOT_COUNT)
        return p->readSlots[p->readActiveSlot].buffer;
    return NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32* p;
    if (!priv) return 0;
    p = W32(priv);
    if (p->readActiveSlot >= 0 && p->readActiveSlot < XNETWORK_READ_SLOT_COUNT)
        return p->readSlots[p->readActiveSlot].ctx.finishedBytes;
    return 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return 0;
    /* 完成回调把每笔写的字节数压进 FIFO，Write 事件消费时落进
     * lastWriteBytes；因此这里读到的是"本次 Write 事件对应那一笔"的字节数，
     * 而不是"最后一个在途槽碰巧剩下的值"（旧实现会重复/错报 bytesWritten）。 */
    return W32(priv)->lastWriteBytes;
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32(priv)->writePending : false;
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32* p;
    if (!priv) return;
    p = W32(priv);

    /* 上层已把当前这批数据取走：释放"呈现槽"。注意这里释放的是**数据**，
     * 不是"在飞"标志——在飞关系只由完成包迁移（不变量 B）。因此本函数被
     * 重复调用、或在读请求仍在飞时被调用，都只是空操作，不会造成二次投递。 */
    if (p->readActiveSlot >= 0 && p->readActiveSlot < XNETWORK_READ_SLOT_COUNT) {
        p->readSlots[p->readActiveSlot].ready = false;
        p->readActiveSlot = -1;
    }
    if (isUdp) p->isUdpRead = true;
    (void)w32_armRead(priv);
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
    p->acceptReady = false;   /* 只有本笔 AcceptEx 的完成包才能把它置真 */

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
    p->acceptReady = false;
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
    w32_newIoGeneration(p);
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
    XDeviceNetworkContextWin32* p;
    if (!priv) return;
    p = W32(priv);
    if (!w32_has_stream_handle(p)) return;

    /* 续发只有一个实现：`w32_pumpWrites`（holding pen → ring buffer，保序、
     * 先预检槽位再从队列取数据）。旧实现自带一份"取一块发一块、失败写回
     * 队尾"的逻辑：既与 socketWrite 的续发路径语义分叉，失败回写队尾还会
     * 打乱字节流顺序（同一帧被拆开后乱序到达）。 */
    w32_pumpWrites(priv, ringBuffer, isUdp);
}
#endif /* XNETWORK_WIN_SOCKET_IOCP —— 写续发段结束 */

#endif /* XNETWORK_USE_PLATFORM_API */
