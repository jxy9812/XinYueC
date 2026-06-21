/**
 * @file XNetwork_win32.c
 * @brief Windows 平台网络实现（IOCP 异步 I/O）
 */

 /* ====== Windows 宏定义必须在所有头文件之前 ======*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* ====== 项目头文件 ====== */
#include "XNetwork_platform.h"
#include "XIODevice.h"
#include "XIODevicePrivate.h"
#include "IOCPInfo.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include "XEvent.h"
#include "XHostAddress.h"

/* ====== Windows SDK 头文件 ====== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

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
static PIP_ADAPTER_ADDRESSES current = 0;
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
        extern HANDLE IOCP_getGlobalPort(void);
        g_iocp = IOCP_getGlobalPort();
    }
    return g_iocp;
}

static bool iocp_assoc(SOCKET s, XObject* key)
{
    HANDLE h = iocp_get();
    if (!h) return false;
    //XPrintf("帮:%p\n",key);
    return CreateIoCompletionPort((HANDLE)s, h, (ULONG_PTR)key, 0) != NULL;
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

void XNetwork_ensureInit(void)
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

void XNetwork_cleanup(void)
{
    if (InterlockedDecrement(&g_wsaRefCount) == 0) {
        WSACleanup();
        g_iocp = NULL;
        g_ConnectEx = NULL;
    }
}

int XNetwork_lastError(void)
{
    return WSAGetLastError();
}

bool XNetwork_isEAgain(int err)
{
    return err == WSAEWOULDBLOCK;
}

char* XNetwork_errorString(int errorCode)
{
    char* buf = (char*)XMalloc_System(256);
    if (!buf) return NULL;
    
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, 256, NULL);
    return buf;
}

/* =========================================================================
 * 套接字私有数据结构
 * ========================================================================= */

struct XNetworkSocketPrivate {
    SOCKET socket;                          ///< Windows SOCKET 句柄
    XObject* owner;                            ///< 拥有者对象
    
    /* 状态标志 */
    bool readPending;
    bool writePending;
    bool connectPending;
    bool connected;
    bool autoRead;
    bool isServer;                          ///< 是否为服务器套接字
    bool acceptPending;                     ///< 是否有待处理的 Accept
    
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
};

/* =========================================================================
 * 私有数据管理
 * ========================================================================= */

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner)
{
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)XCalloc_System(1, sizeof(XNetworkSocketPrivate));
    if (!priv) return NULL;
    
    priv->socket = INVALID_SOCKET;
    priv->owner = owner;
    priv->autoRead = true;
    
    XHostAddress_init(&priv->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&priv->pendingPeerAddr, XHostAddress_NullSpecial);
    
    return priv;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    
    if (priv->socket != INVALID_SOCKET) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
    }
    
    XHostAddress_deinit_base(&priv->pendingPeerAddr);
    XFree_System(priv);
}

XSocketHandle XNetwork_socketHandle(const XNetworkSocketPrivate* priv)
{
    return priv ? (XSocketHandle)priv->socket : -1;
}

void* XNetwork_socketOwner(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->owner : NULL;
}

bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->connected : false;
}
/* =========================================================================
 * 异步读取启动
 * ========================================================================= */

static void startAsyncRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv || priv->readPending || priv->socket == INVALID_SOCKET) return;
    (void)isUdp;
    
    memset(&priv->readContext, 0, sizeof(XEventContext_IOCP));
    priv->readContext.type = XEventContextType_Type_Socket;
    priv->readContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
    priv->readContext.eventMask = FD_READ;
    
    WSABUF buf;
    buf.buf = priv->readBuffer;
    buf.len = XNETWORK_READ_BUFFER_SIZE;
    
    DWORD flags = 0;
    int result;
    
    if (isUdp) {
        priv->fromAddrLen = sizeof(priv->fromAddr);
        result = WSARecvFrom(priv->socket, &buf, 1, NULL, &flags,
                            (struct sockaddr*)&priv->fromAddr, &priv->fromAddrLen,
                            (OVERLAPPED*)&priv->readContext, NULL);
    } else {
        result = WSARecv(priv->socket, &buf, 1, NULL, &flags,
                        (OVERLAPPED*)&priv->readContext, NULL);
    }
    
    if (result == 0) {
        /* 立即完成 */
        priv->readPending = true;
    } else if (WSAGetLastError() == WSA_IO_PENDING) {
        /* 异步等待 */
        priv->readPending = true;
    } else {
        /* 错误 */
        priv->readPending = false;
    }
}

/* =========================================================================
 * 异步写入启动
 * ========================================================================= */

static void startAsyncWrite(XNetworkSocketPrivate* priv, const void* data, int64_t len,
                            const XHostAddress* destAddr, uint16_t destPort, bool isUdp)
{
    if (!priv || priv->writePending || priv->socket == INVALID_SOCKET) return;
    if (len <= 0 || len > XNETWORK_WRITE_BUFFER_SIZE) return;
    (void)destAddr; (void)destPort; (void)isUdp;
    
    /* 复制数据到写缓冲区 */
    memcpy(priv->writeBuffer, data, (size_t)len);
    
    memset(&priv->writeContext, 0, sizeof(XEventContext_IOCP));
    priv->writeContext.type = XEventContextType_Type_Socket;
    priv->writeContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
    priv->writeContext.eventMask = FD_WRITE;
    priv->writeContext.finishedBytes = len;
    WSABUF buf;
    buf.buf = priv->writeBuffer;
    buf.len = (ULONG)len;
    
    int result;
    
    if (isUdp && destAddr) {
        struct sockaddr_storage dest;
        int destLen;
        addr2sa(destAddr, destPort, &dest, &destLen);
        
        result = WSASendTo(priv->socket, &buf, 1, NULL, 0,
                          (struct sockaddr*)&dest, destLen,
                          (OVERLAPPED*)&priv->writeContext, NULL);
    } else {
        result = WSASend(priv->socket, &buf, 1, NULL, 0,
                        (OVERLAPPED*)&priv->writeContext, NULL);
    }
    
    if (result == 0) {
        /* 立即完成 - 手动向 IOCP 投递完成通知 */
        priv->writePending = true;
        HANDLE iocp = iocp_get();
        if (iocp) {
            PostQueuedCompletionStatus(iocp, (DWORD)len, (ULONG_PTR)priv->owner,
                                       (OVERLAPPED*)&priv->writeContext);
        }
    } else if (WSAGetLastError() == WSA_IO_PENDING) {
        /* 异步等待 */
        priv->writePending = true;
    } else {
        /* 错误 */
        priv->writePending = false;
    }
}

/* =========================================================================
 * 核心操作实现
 * ========================================================================= */

uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr, 
                              XNetworkSocketType sockType)
{
    if (!priv || !address) return 0;
    
    /* 确保已初始化 */
    XNetwork_ensureInit();
    
    /* 创建套接字 */
    int af = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    int type = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;
    
    priv->socket = socket(af, type, proto);
    if (priv->socket == INVALID_SOCKET) return 0;
    
    /* 设置非阻塞 */
    u_long mode = 1;
    ioctlsocket(priv->socket, FIONBIO, &mode);
    
    /* 设置地址重用 */
    if (reuseAddr) {
        int opt = 1;
        setsockopt(priv->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    
    /* 绑定 */
    struct sockaddr_storage addrStorage;
    int addrLen;
    addr2sa(address, port, &addrStorage, &addrLen);
    
    if (bind(priv->socket, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        return 0;
    }
    
    /* 获取实际绑定的端口 */
    uint16_t actualPort = 0;
    struct sockaddr_storage boundAddr;
    int boundAddrLen = sizeof(boundAddr);
    if (getsockname(priv->socket, (struct sockaddr*)&boundAddr, &boundAddrLen) == 0) {
        if (boundAddr.ss_family == AF_INET6) {
            actualPort = ntohs(((struct sockaddr_in6*)&boundAddr)->sin6_port);
        } else {
            actualPort = ntohs(((struct sockaddr_in*)&boundAddr)->sin_port);
        }
    }
    
    /* 关联到 IOCP */
    if (!iocp_assoc(priv->socket, priv->owner)) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        return 0;
    }
    
    priv->connected = true;
    
    /* UDP 绑定后立即启动异步读取 */
    if (sockType == XNetwork_Udp) {
        startAsyncRead(priv, true);
    }
    
    return actualPort;
}

bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, XNetworkProtocol protocol, 
                            XNetworkSocketType sockType, const void* proxy)
{
    if (!priv || !hostName) return false;
    
    XNetwork_ensureInit();
    
    /* DNS 解析 */
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = (protocol == XNetwork_IPv6) ? AF_INET6 : 
                      (protocol == XNetwork_IPv4) ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    
    if (getaddrinfo(hostName, NULL, &hints, &result) != 0) {
        return false;
    }
    
    /* 选择第一个地址 */
    struct addrinfo* p = result;
    while (p && p->ai_family != hints.ai_family) {
        p = p->ai_next;
    }
    if (!p) p = result;
    
    /* 创建套接字 */
    int type = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (sockType == XNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;
    
    priv->socket = socket(p->ai_family, type, proto);
    if (priv->socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }
    
    /* 设置非阻塞 */
    u_long mode = 1;
    ioctlsocket(priv->socket, FIONBIO, &mode);
    
    /* 绑定到任意本地地址（ConnectEx 需要） */
    struct sockaddr_storage localAddr = {0};
    int localLen = (p->ai_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    ((struct sockaddr*)&localAddr)->sa_family = p->ai_family;
    
    if (bind(priv->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }
    
    /* 关联到 IOCP */
    if (!iocp_assoc(priv->socket, priv->owner)) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }
    
    /* 设置目标地址 */
    struct sockaddr_storage destAddr;
    int destLen;
    if (p->ai_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&destAddr;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons(port);
        memcpy(&s6->sin6_addr, &((struct sockaddr_in6*)p->ai_addr)->sin6_addr, sizeof(struct in6_addr));
        destLen = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&destAddr;
        s4->sin_family = AF_INET;
        s4->sin_port = htons(port);
        s4->sin_addr = ((struct sockaddr_in*)p->ai_addr)->sin_addr;
        destLen = sizeof(struct sockaddr_in);
    }
    
    freeaddrinfo(result);
    
    /* 保存目标信息 */
    sa2addr(&destAddr, &priv->pendingPeerAddr, &priv->pendingPeerPort);
    priv->pendingPeerPort = port;
    
    /* 代理处理 */
    if (proxy) {
        const XNetworkProxy* pi = (const XNetworkProxy*)proxy;
        if (pi->type == XNetworkProxy_Socks5Proxy) {
            /* TODO: SOCKS5 握手 */
            if (!XNetwork_socks5Connect((XSocketHandle)priv->socket, pi, 
                                        &priv->pendingPeerAddr, port)) {
                closesocket(priv->socket);
                priv->socket = INVALID_SOCKET;
                return false;
            }
            priv->connected = true;
            priv->connectPending = false;
            return true;
        }
    }
    
    /* TCP 使用 ConnectEx 异步连接 */
    if (sockType == XNetwork_Tcp && g_ConnectEx) {
        memset(&priv->connectContext, 0, sizeof(XEventContext_IOCP));
        priv->connectContext.type = XEventContextType_Type_Socket;
        priv->connectContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
        priv->connectContext.eventMask = FD_CONNECT;
        
        if (!g_ConnectEx(priv->socket, (struct sockaddr*)&destAddr, destLen,
                        NULL, 0, NULL, (OVERLAPPED*)&priv->connectContext)) {
            if (WSAGetLastError() != WSA_IO_PENDING) {
                closesocket(priv->socket);
                priv->socket = INVALID_SOCKET;
                return false;
            }
        }
        priv->connectPending = true;
        return true;
    }
    
    /* UDP 直接连接 */
    if (sockType == XNetwork_Udp) {
        priv->connected = true;
        priv->connectPending = false;
        /* 启动异步读取 */
        startAsyncRead(priv, true);
        return true;
    }
    
    return true;
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    
    if (priv->socket != INVALID_SOCKET) {
        /* 取消所有待处理操作 */
        CancelIo((HANDLE)priv->socket);
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
    }
    
    priv->connected = false;
    priv->connectPending = false;
    priv->readPending = false;
    priv->writePending = false;
}

int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    if (priv->socket == INVALID_SOCKET) return -1;
    
    /* 优先从环形缓冲区读取 */
    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t available = XRingBuffer_available(rb);
        if (available > 0) {
            size_t toRead = (len < (int64_t)available) ? (size_t)len : available;
            return XRingBuffer_read(rb, buf, toRead);
        }
    }
    
    /* 如果没有数据可读 */
    return 0;
}

int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr, 
                             uint16_t destPort, void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    if (priv->socket == INVALID_SOCKET) return -1;
    
    /* 如果有环形缓冲区且有待发送数据，先处理 */
    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        size_t pending = XRingBuffer_available(rb);
        if (pending > 0 && !priv->writePending) {
            /* 先发送缓冲区中的数据 */
            char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
            size_t toSend = XRingBuffer_peek(rb, tempBuf, XNETWORK_WRITE_BUFFER_SIZE);
            if (toSend > 0) {
                startAsyncWrite(priv, tempBuf, toSend, destAddr, destPort, 
                               sockType == XNetwork_Udp);
            }
        }
    }
    
    /* 如果没有待发送操作，直接异步发送 */
    if (!priv->writePending) {
        if (len > XNETWORK_WRITE_BUFFER_SIZE) {
            /* 数据太大，分批发送，存入环形缓冲区 */
            if (ringBuffer) {
                struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
                XRingBuffer_write(rb, buf, len);
                return len;
            }
            len = XNETWORK_WRITE_BUFFER_SIZE;
        }
        
        bool isUdp = (sockType == XNetwork_Udp); /* TCP only, UDP handled above */
        startAsyncWrite(priv, buf, len, destAddr, destPort, isUdp);
        return len;
    }
    
    /* 当前有发送操作，存入环形缓冲区 */
    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        return XRingBuffer_write(rb, buf, len);
    }
    
    return -1;
}

bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event)
{
    if (!priv || !event) return false;
    
    XEvent* e = (XEvent*)event;
    if (e->type != XEVENT_TYPE_SOCK_ACT) return false;
    
    XEventSockAct* sockAct = (XEventSockAct*)e;
    
    /* 处理读取完成 */
    if (sockAct->actType & XSocketAct_Read) {
        priv->readPending = false;
        
        size_t bytesTransferred = priv->readContext.finishedBytes;
        if (bytesTransferred > 0) {
            /* 通知上层有数据可读 */
            /* 数据在 priv->readBuffer 中 */
            return true;
        }
    }
    
    /* 处理写入完成 */
    if (sockAct->actType & XSocketAct_Write) {
        priv->writePending = false;
        return true;
    }
    
    /* 处理连接完成 */
    if (sockAct->actType & XSocketAct_Connect) {
        priv->connectPending = false;
        priv->connected = true;
        
        /* 更新套接字选项 */
        int opt = 1;
        setsockopt(priv->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, 
                  (char*)&opt, sizeof(opt));
        
        /* 启动异步读取 */
        startAsyncRead(priv, false);
        return true;
    }
    
    return false;
}

bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, 
                                  int state, int openMode)
{
    if (!priv || fd == -1) return false;
    (void)openMode;
    
    priv->socket = (SOCKET)fd;
    
    /* 关联到 IOCP */
    if (!iocp_assoc(priv->socket, priv->owner)) {
        priv->socket = INVALID_SOCKET;
        return false;
    }
    
    priv->connected = (state == 3); /* XAbstractSocket_ConnectedState */
    
    if (priv->connected) {
        priv->autoRead = true;
        startAsyncRead(priv, false);
    }
    
    return true;
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv)
{
    return priv ? (intptr_t)priv->socket : -1;
}

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value)
{
    if (!priv || !value || priv->socket == INVALID_SOCKET) return false;
    
    int* intVal = (int*)value;
    
    switch (option) {
        case 0: /* LowDelayOption - TCP_NODELAY */
            return setsockopt(priv->socket, IPPROTO_TCP, TCP_NODELAY,
                            (char*)intVal, sizeof(int)) == 0;
        case 1: /* KeepAliveOption - SO_KEEPALIVE */
            return setsockopt(priv->socket, SOL_SOCKET, SO_KEEPALIVE,
                            (char*)intVal, sizeof(int)) == 0;
        case 2: /* MulticastTtlOption */
            return XNetwork_setMulticastTtl((XSocketHandle)priv->socket, *intVal);
        case 3: /* MulticastLoopbackOption */
            return XNetwork_setMulticastLoopback((XSocketHandle)priv->socket, *intVal != 0);
        case 4: /* TypeOfServiceOption - IP_TOS */
            return setsockopt(priv->socket, IPPROTO_IP, IP_TOS,
                            (char*)intVal, sizeof(int)) == 0;
        case 5: /* SendBufferSizeOption */
            return setsockopt(priv->socket, SOL_SOCKET, SO_SNDBUF,
                            (char*)intVal, sizeof(int)) == 0;
        case 6: /* ReceiveBufferSizeOption */
            return setsockopt(priv->socket, SOL_SOCKET, SO_RCVBUF,
                            (char*)intVal, sizeof(int)) == 0;
        case 8: /* BroadcastOption - SO_BROADCAST */
            return setsockopt(priv->socket, SOL_SOCKET, SO_BROADCAST,
                            (char*)intVal, sizeof(int)) == 0;
        default:
            return false;
    }
}
void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option)
{
    if (!priv || priv->socket == INVALID_SOCKET) return NULL;
    
    static int result;
    int optLen = sizeof(int);
    
    switch (option) {
        case 0: /* LowDelayOption */
            if (getsockopt(priv->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 1: /* KeepAliveOption */
            if (getsockopt(priv->socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 2: /* MulticastTtlOption */
            result = XNetwork_multicastTtl((XSocketHandle)priv->socket);
            return &result;
        case 3: /* MulticastLoopbackOption */
            result = XNetwork_multicastLoopback((XSocketHandle)priv->socket) ? 1 : 0;
            return &result;
        case 4: /* TypeOfServiceOption */
            if (getsockopt(priv->socket, IPPROTO_IP, IP_TOS, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 5: /* SendBufferSizeOption */
            if (getsockopt(priv->socket, SOL_SOCKET, SO_SNDBUF, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 6: /* ReceiveBufferSizeOption */
            if (getsockopt(priv->socket, SOL_SOCKET, SO_RCVBUF, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 7: /* PathMtuOption */
            if (getsockopt(priv->socket, IPPROTO_IP, IP_MTU, (char*)&result, &optLen) == 0)
                return &result;
            break;
        case 8: /* BroadcastOption */
            if (getsockopt(priv->socket, SOL_SOCKET, SO_BROADCAST, (char*)&result, &optLen) == 0)
                return &result;
            break;
        default:
            break;
    }
    
    return NULL;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size)
{
    if (!priv || priv->socket == INVALID_SOCKET || size <= 0) return;
    
    int bufSize = (int)((size > INT_MAX) ? INT_MAX : size);
    setsockopt(priv->socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize));
}

/* =========================================================================
 * 异步读取状态
 * ========================================================================= */

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->readBuffer : NULL;
}

size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->readContext.finishedBytes : 0;
}

size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->writeContext.finishedBytes : 0;
}

bool XNetwork_socketHasPendingData(const XNetworkSocketPrivate* priv)
{
    if (!priv || priv->socket == INVALID_SOCKET) return false;
    
    /* 检查是否有待读的数据报 */
    unsigned long available = 0;
    if (ioctlsocket(priv->socket, FIONREAD, &available) == 0) {
        return available > 0;
    }
    return false;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv) return;
    
    priv->readPending = false;
    
    /* 如果启用了自动读取，继续启动异步读取 */
    if (priv->autoRead && priv->connected) {
        startAsyncRead(priv, isUdp);
    }
}

/* =========================================================================
 * 异步 Accept 启动
 * ========================================================================= */

static bool startAsyncAccept(XNetworkSocketPrivate* priv)
{
    if (!priv || priv->socket == INVALID_SOCKET || !g_AcceptEx) return false;
    if (priv->acceptPending) return true; /* 已有待处理的 Accept */
    
    /* 获取监听套接字的地址族 */
    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);
    if (getsockname(priv->socket, (struct sockaddr*)&addr, &addrLen) == SOCKET_ERROR) {
        return false;
    }
    int af = addr.ss_family;
    
    /* 创建预分配的接受套接字 */
    SOCKET acceptSock = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (acceptSock == INVALID_SOCKET) {
        return false;
    }
    
    priv->acceptSocket = acceptSock;
    
    /* 初始化 AcceptEx 上下文 */
    memset(&priv->acceptContext, 0, sizeof(XEventContext_IOCP));
    priv->acceptContext.type = XEventContextType_Type_Socket;
    priv->acceptContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
    priv->acceptContext.eventMask = FD_ACCEPT;
    
    /* AcceptEx 缓冲区大小计算：
     * 需要为本地地址和远程地址各预留 sockaddr_in6 + 16 字节
     */
    DWORD bytesReceived = 0;
    
    BOOL result = g_AcceptEx(
        priv->socket,           /* 监听套接字 */
        acceptSock,             /* 接受套接字 */
        priv->acceptBuffer,     /* 输出缓冲区 */
        0,                      /* 不接收数据，只接受连接 */
        sizeof(struct sockaddr_in6) + 16,  /* 本地地址空间 */
        sizeof(struct sockaddr_in6) + 16,  /* 远程地址空间 */
        &bytesReceived,
        (OVERLAPPED*)&priv->acceptContext
    );
    
    if (result) {
        /* 立即完成（罕见情况） */
        priv->acceptPending = true;
        return true;
    }
    
    if (WSAGetLastError() == WSA_IO_PENDING) {
        /* 异步等待 */
        priv->acceptPending = true;
        return true;
    }
    
    /* 错误 */
    closesocket(acceptSock);
    priv->acceptSocket = INVALID_SOCKET;
    return false;
}

/* =========================================================================
 * TCP 服务器
 * ========================================================================= */

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv,const XHostAddress* addr, uint16_t port,
                                    int backlog, bool reuseAddr)
{
    if (!priv) return -1;
    XNetwork_ensureInit();
    
    int af = (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    
    SOCKET s = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    
    /* 设置非阻塞 */
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    
    /* 地址重用 */
    if (reuseAddr) {
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    
    /* IPv6 双栈 */
    if (af == AF_INET6) {
        int ipv6only = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only));
    }
    
    /* 绑定 */
    struct sockaddr_storage addrStorage;
    int addrLen;
    addr2sa(addr, port, &addrStorage, &addrLen);
    
    if (bind(s, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }
    
    /* 监听 */
    if (listen(s, backlog > 0 ? backlog : SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }
    
    /* 关联到 IOCP */
    iocp_assoc(s, priv->owner);
    priv->socket = s;
    priv->isServer = true;
    priv->connected = true;  /* 服务器套接字处于监听状态 */
    
    /* 启动异步 Accept */
    startAsyncAccept(priv);
    
    return (XServerHandle)s;
}

XSocketHandle XNetwork_serverAccept(XServerHandle server,
                                    XHostAddress* clientAddr, uint16_t* clientPort)
{
    if (server == -1) return -1;
    
    struct sockaddr_storage client;
    int clientLen = sizeof(client);
    
    SOCKET cs = accept((SOCKET)server, (struct sockaddr*)&client, &clientLen);
    if (cs == INVALID_SOCKET) return -1;
    
    /* 设置非阻塞 */
    u_long mode = 1;
    ioctlsocket(cs, FIONBIO, &mode);
    
    ///* 关联到 IOCP */
    //iocp_assoc(cs, priv->owner);
    
    if (clientAddr && clientPort) {
        sa2addr(&client, clientAddr, clientPort);
    }
    
    return (XSocketHandle)cs;
}

uint16_t XNetwork_serverPort(XServerHandle server)
{
    if (server == -1) return 0;
    
    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);
    
    if (getsockname((SOCKET)server, (struct sockaddr*)&addr, &addrLen) == 0) {
        if (addr.ss_family == AF_INET6) {
            return ntohs(((struct sockaddr_in6*)&addr)->sin6_port);
        } else {
            return ntohs(((struct sockaddr_in*)&addr)->sin_port);
        }
    }
    return 0;
}

void XNetwork_serverClose(XServerHandle server)
{
    if (server != -1) {
        closesocket((SOCKET)server);
    }
}

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv,
                                               XHostAddress* clientAddr, uint16_t* clientPort)
{
    if (!priv || priv->acceptSocket == INVALID_SOCKET) return -1;
    
    /* 更新接受套接字的上下文（SO_UPDATE_ACCEPT_CONTEXT） */
    int opt = 1;
    setsockopt(priv->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               (char*)&opt, sizeof(opt));
    
    /* 使用 GetAcceptExSockaddrs 获取客户端地址 */
    if (clientAddr || clientPort) {
        if (g_GetAcceptExSockaddrs) {
            struct sockaddr* localAddr = NULL;
            struct sockaddr* remoteAddr = NULL;
            int localLen = 0, remoteLen = 0;
            
            g_GetAcceptExSockaddrs(
                priv->acceptBuffer,
                0,  /* 没有接收数据 */
                sizeof(struct sockaddr_in6) + 16,
                sizeof(struct sockaddr_in6) + 16,
                &localAddr, &localLen,
                &remoteAddr, &remoteLen
            );
            
            if (remoteAddr && remoteLen > 0) {
                sa2addr((struct sockaddr_storage*)remoteAddr, clientAddr, clientPort);
            }
        }
    }
    
    /* 设置非阻塞 */
    u_long mode = 1;
    ioctlsocket(priv->acceptSocket, FIONBIO, &mode);
    
    /* 返回套接字并清除 */
    SOCKET result = priv->acceptSocket;
    priv->acceptSocket = INVALID_SOCKET;
    priv->acceptPending = false;
    
    return (XSocketHandle)result;
}

bool XNetwork_serverContinueAccept(XNetworkSocketPrivate* priv)
{
    if (!priv || priv->socket == INVALID_SOCKET) return false;
    
    priv->acceptPending = false;
    return startAsyncAccept(priv);
}

/* =========================================================================
 * DNS 查询
 * ========================================================================= */

bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count)
{
    if (!name || !addrs || !count) return false;
    
    XNetwork_ensureInit();
    
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(name, NULL, &hints, &result) != 0) {
        *count = 0;
        *addrs = NULL;
        return false;
    }
    
    /* 计算地址数量 */
    int addrCount = 0;
    struct addrinfo* p = result;
    while (p) {
        addrCount++;
        p = p->ai_next;
    }
    
    if (addrCount == 0) {
        freeaddrinfo(result);
        *count = 0;
        *addrs = NULL;
        return false;
    }
    
    /* 分配地址数组 */
    XHostAddress* addrArray = (XHostAddress*)XCalloc_System(addrCount, sizeof(XHostAddress));
    if (!addrArray) {
        freeaddrinfo(result);
        *count = 0;
        *addrs = NULL;
        return false;
    }
    
    /* 填充地址 */
    p = result;
    for (int i = 0; i < addrCount && p; i++) {
        XHostAddress_init(&addrArray[i]);
        sa2addr((struct sockaddr_storage*)p->ai_addr, &addrArray[i], NULL);
        p = p->ai_next;
    }
    
    freeaddrinfo(result);
    
    *count = addrCount;
    *addrs = addrArray;
    return true;
}

char* XNetwork_localHostName(void)
{
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf)) == 0) {
        return XStrdup(buf);
    }
    return NULL;
}

char* XNetwork_localDomainName(void)
{
    /* Windows 没有直接获取域名的 API，返回空 */
    return NULL;
}

/* =========================================================================
 * 网络接口枚举
 * ========================================================================= */

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void)
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
    
    return (XNetworkInterfaceIterator)adapterAddresses;
}

bool XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter, XNetworkInterfaceEntry* out)
{
    if (!iter || !out) return false;
    

    
    if (current == NULL) {
        current = (PIP_ADAPTER_ADDRESSES)iter;
    } else {
        current = current->Next;
    }
    
    if (current == NULL) return false;
    
    memset(out, 0, sizeof(XNetworkInterfaceEntry));
    
    /* 名称 */
    WideCharToMultiByte(CP_UTF8, 0, current->FriendlyName, -1,
                       out->readableName, sizeof(out->readableName), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, current->AdapterName, -1,
                       out->name, sizeof(out->name), NULL, NULL);
    
    out->index = current->IfIndex;
    out->mtu = (int)current->Mtu;
    
    /* 硬件地址 */
    if (current->PhysicalAddressLength > 0 && current->PhysicalAddressLength <= 32) {
        memcpy(out->hwAddr, current->PhysicalAddress, current->PhysicalAddressLength);
        out->hwAddrLen = current->PhysicalAddressLength;
    }
    
    /* 标志 */
    if (current->OperStatus == IfOperStatusUp) out->flags |= XNetworkIf_Up | XNetworkIf_Running;
    if (current->IfType == IF_TYPE_SOFTWARE_LOOPBACK) out->flags |= XNetworkIf_Loopback;
#ifndef IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED
#define IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED 0x0004
#endif
    if (current->Flags & IP_ADAPTER_ADAPTER_FLAG_DHCP_ENABLED) out->flags |= XNetworkIf_Multicast;
    if (current->IfType == IF_TYPE_ETHERNET_CSMACD || current->IfType == IF_TYPE_IEEE80211) {
        out->type = 1; /* Ethernet/WiFi */
    }
    
    return true;
}

void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter)
{
    if (iter) {
        XFree_System(iter);
    }
    /* 重置静态变量 */
    //extern PIP_ADAPTER_ADDRESSES current;
    current = NULL;
}

bool XNetwork_getInterfaceAddresses(const char* ifname,
                                    XHostAddress** addrs, XHostAddress** masks, int* count)
{
    if (!ifname || !addrs || !count) return false;
    
    *count = 0;
    *addrs = NULL;
    if (masks) *masks = NULL;
    
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;
    ULONG size = 0;
    
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &size);
    adapterAddresses = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
    if (!adapterAddresses) return false;
    
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL,
                             adapterAddresses, &size) != ERROR_SUCCESS) {
        XFree_System(adapterAddresses);
        return false;
    }
    
    /* 查找指定接口 */
    PIP_ADAPTER_ADDRESSES adapter = adapterAddresses;
    while (adapter) {
        char name[128];
        WideCharToMultiByte(CP_ACP, 0, adapter->AdapterName, -1, name, sizeof(name), NULL, NULL);
        
        if (strcmp(name, ifname) == 0) {
            /* 计算地址数量 */
            int addrCount = 0;
            PIP_ADAPTER_UNICAST_ADDRESS ua = adapter->FirstUnicastAddress;
            while (ua) {
                addrCount++;
                ua = ua->Next;
            }
            
            if (addrCount == 0) {
                XFree_System(adapterAddresses);
                return false;
            }
            
            /* 分配数组 */
            XHostAddress* addrArray = (XHostAddress*)XCalloc_System(addrCount, sizeof(XHostAddress));
            XHostAddress* maskArray = masks ? (XHostAddress*)XCalloc_System(addrCount, sizeof(XHostAddress)) : NULL;
            
            if (!addrArray) {
                XFree_System(adapterAddresses);
                return false;
            }
            
            /* 填充地址 */
            ua = adapter->FirstUnicastAddress;
            for (int i = 0; i < addrCount && ua; i++) {
                XHostAddress_init(&addrArray[i]);
                sa2addr((struct sockaddr_storage*)ua->Address.lpSockaddr, &addrArray[i], NULL);
                
                if (maskArray) {
                    XHostAddress_init(&maskArray[i]);
                    /* 从前缀长度计算掩码 */
                    uint8_t prefixLen = ua->OnLinkPrefixLength;
                    if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                        uint32_t mask = prefixLen >= 32 ? 0xFFFFFFFF : htonl(0xFFFFFFFF << (32 - prefixLen));
                        XHostAddress_setAddressIPv4(&maskArray[i], mask);
                    } else {
                        /* IPv6 掩码 */
                        uint8_t maskBytes[16] = {0};
                        for (int j = 0; j < 16 && prefixLen > 0; j++) {
                            if (prefixLen >= 8) {
                                maskBytes[j] = 0xFF;
                                prefixLen -= 8;
                            } else {
                                maskBytes[j] = (uint8_t)(0xFF << (8 - prefixLen));
                                prefixLen = 0;
                            }
                        }
                        XHostAddress_setAddressIPv6(&maskArray[i], maskBytes);
                    }
                }
                
                ua = ua->Next;
            }
            
            *count = addrCount;
            *addrs = addrArray;
            if (masks) *masks = maskArray;
            
            XFree_System(adapterAddresses);
            return true;
        }
        
        adapter = adapter->Next;
    }
    
    XFree_System(adapterAddresses);
    return false;
}

/* =========================================================================
 * 多播组
 * ========================================================================= */

bool XNetwork_joinMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress, uint32_t ifIndex)
{
    if (sock == -1 || !groupAddress) return false;
    
    if (XHostAddress_protocol(groupAddress) == XHostAddress_IPv6Protocol) {
        struct ipv6_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        XHostAddress_toIPv6Address(groupAddress, &mreq.ipv6mr_multiaddr);
        mreq.ipv6mr_interface = ifIndex;
        return setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, 
                         (char*)&mreq, sizeof(mreq)) == 0;
    } else {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = htonl(XHostAddress_toIPv4Address(groupAddress));
        mreq.imr_interface.s_addr = htonl(ifIndex);
        return setsockopt((SOCKET)sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         (char*)&mreq, sizeof(mreq)) == 0;
    }
}

bool XNetwork_leaveMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress, uint32_t ifIndex)
{
    if (sock == -1 || !groupAddress) return false;
    
    if (XHostAddress_protocol(groupAddress) == XHostAddress_IPv6Protocol) {
        struct ipv6_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        XHostAddress_toIPv6Address(groupAddress, &mreq.ipv6mr_multiaddr);
        mreq.ipv6mr_interface = ifIndex;
        return setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                         (char*)&mreq, sizeof(mreq)) == 0;
    } else {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = htonl(XHostAddress_toIPv4Address(groupAddress));
        mreq.imr_interface.s_addr = htonl(ifIndex);
        return setsockopt((SOCKET)sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                         (char*)&mreq, sizeof(mreq)) == 0;
    }
}

bool XNetwork_setMulticastInterface(XSocketHandle sock, uint32_t ifIndex)
{
    if (sock == -1) return false;
    
    struct in_addr addr;
    addr.s_addr = htonl(ifIndex);
    return setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_IF,
                     (char*)&addr, sizeof(addr)) == 0;
}

uint32_t XNetwork_multicastInterface(XSocketHandle sock)
{
    if (sock == -1) return 0;
    
    struct in_addr addr;
    int len = sizeof(addr);
    if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_IF, (char*)&addr, &len) == 0) {
        return ntohl(addr.s_addr);
    }
    return 0;
}

bool XNetwork_setMulticastTtl(XSocketHandle sock, int ttl)
{
    if (sock == -1) return false;
    
    /* IPv4 多播 TTL */
    int ttlVal = ttl;
    if (setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_TTL,
                  (char*)&ttlVal, sizeof(ttlVal)) == SOCKET_ERROR) {
        return false;
    }
    
    /* IPv6 多播跳数 */
    int hops = ttl;
    setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
              (char*)&hops, sizeof(hops));
    
    return true;
}

int XNetwork_multicastTtl(XSocketHandle sock)
{
    if (sock == -1) return -1;
    
    int ttl = 0;
    int len = sizeof(ttl);
    
    if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, &len) == 0) {
        return ttl;
    }
    
    return -1;
}

bool XNetwork_setMulticastLoopback(XSocketHandle sock, bool enabled)
{
    if (sock == -1) return false;
    
    int loop = enabled ? 1 : 0;
    
    /* IPv4 */
    if (setsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                  (char*)&loop, sizeof(loop)) == SOCKET_ERROR) {
        return false;
    }
    
    /* IPv6 */
    setsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
              (char*)&loop, sizeof(loop));
    
    return true;
}

bool XNetwork_multicastLoopback(XSocketHandle sock)
{
    if (sock == -1) return false;
    
    int loop = 0;
    int len = sizeof(loop);
    
    if (getsockopt((SOCKET)sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loop, &len) == 0) {
        return loop != 0;
    }
    
    return false;
}

/* =========================================================================
 * UDP 特有
 * ========================================================================= */

/* XNetwork_hasPendingDatagrams 和 XNetwork_pendingDatagramSize 已移除
 * 原因：在 IOCP 异步模型中，数据已被读取到 ringBuffer，
 * 请使用 XIODevice_bytesAvailable 检查待读取数据 */

bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv,
                                     XHostAddress* srcAddr, uint16_t* srcPort)
{
    if (!priv) return false;
    
    /* 从 priv->fromAddr 获取发送者信息 */
    const XNetworkSocketPrivate* p = (const XNetworkSocketPrivate*)priv;
    
    if (p->fromAddrLen <= 0) {
        return false;
    }
    
    if (srcAddr || srcPort) {
        sa2addr((const struct sockaddr_storage*)&p->fromAddr, srcAddr, srcPort);
    }
    
    return true;
}

int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port)
{
    if (sock < 0 || !data || size <= 0 || !address) return -1;
    
    struct sockaddr_storage dest;
    int destLen = 0;
    addr2sa(address, port, &dest, &destLen);
    
    int result = sendto((SOCKET)sock, (const char*)data, (int)size, 0,
                       (struct sockaddr*)&dest, destLen);
    
    if (result == SOCKET_ERROR) {
        return -1;
    }
    return result;
}

/* =========================================================================
 * 代理隧道
 * ========================================================================= */

bool XNetwork_socks5Connect(XSocketHandle sock, const XNetworkProxy* proxy,
                            const XHostAddress* targetAddr, uint16_t targetPort)
{
    if (sock == -1 || !proxy || !targetAddr) return false;
    
    char buf[256];
    int bufLen = 0;
    
    bool hasAuth = (proxy->user != NULL && proxy->password != NULL);
    
    /* SOCKS5 握手：版本号 + 认证方法数量 + 认证方法列表 */
    buf[0] = 0x05; /* SOCKS5 */
    
    if (hasAuth) {
        buf[1] = 0x02; /* 2 种方法 */
        buf[2] = 0x00; /* 无认证 */
        buf[3] = 0x02; /* 用户名/密码认证 */
        bufLen = 4;
    } else {
        buf[1] = 0x01; /* 1 种方法 */
        buf[2] = 0x00; /* 无认证 */
        bufLen = 3;
    }
    
    /* 发送握手请求 */
    int sent = send((SOCKET)sock, buf, bufLen, 0);
    if (sent != bufLen) return false;
    
    /* 接收握手响应 */
    char resp[2];
    int recvLen = recv((SOCKET)sock, resp, 2, 0);
    if (recvLen != 2 || resp[0] != 0x05) return false;
    
    /* 检查认证方法 */
    if (resp[1] == 0x02 && hasAuth) {
        /* 用户名/密码认证 */
        size_t userLen = strlen(proxy->user);
        size_t passLen = strlen(proxy->password);
        
        buf[0] = 0x01; /* 认证子协议版本 */
        buf[1] = (char)userLen;
        memcpy(buf + 2, proxy->user, userLen);
        buf[2 + userLen] = (char)passLen;
        memcpy(buf + 3 + userLen, proxy->password, passLen);
        
        sent = send((SOCKET)sock, buf, (int)(3 + userLen + passLen), 0);
        if (sent != (int)(3 + userLen + passLen)) return false;
        
        recvLen = recv((SOCKET)sock, resp, 2, 0);
        if (recvLen != 2 || resp[1] != 0x00) return false;
    } else if (resp[1] != 0x00) {
        /* 不支持的认证方法 */
        return false;
    }
    
    /* 发送连接请求 */
    buf[0] = 0x05; /* SOCKS5 */
    buf[1] = 0x01; /* CONNECT 命令 */
    buf[2] = 0x00; /* 保留 */
    
    if (XHostAddress_protocol(targetAddr) == XHostAddress_IPv6Protocol) {
        buf[3] = 0x04; /* IPv6 地址类型 */
        XHostAddress_toIPv6Address(targetAddr, (struct in6_addr*)(buf + 4));
        *((uint16_t*)(buf + 20)) = htons(targetPort);
        bufLen = 22;
    } else {
        buf[3] = 0x01; /* IPv4 地址类型 */
        *((uint32_t*)(buf + 4)) = htonl(XHostAddress_toIPv4Address(targetAddr));
        *((uint16_t*)(buf + 8)) = htons(targetPort);
        bufLen = 10;
    }
    
    sent = send((SOCKET)sock, buf, bufLen, 0);
    if (sent != bufLen) return false;
    
    /* 接收连接响应 */
    recvLen = recv((SOCKET)sock, resp, 2, 0);
    if (recvLen != 2) return false;
    
    /* 读取剩余响应 */
    char addrType;
    recvLen = recv((SOCKET)sock, &addrType, 1, 0);
    if (recvLen != 1) return false;
    
    int addrLen = 0;
    switch (addrType) {
        case 0x01: addrLen = 4; break;  /* IPv4 */
        case 0x04: addrLen = 16; break; /* IPv6 */
        case 0x03: /* 域名 */
            recvLen = recv((SOCKET)sock, &addrLen, 1, 0);
            if (recvLen != 1) return false;
            break;
        default:
            return false;
    }
    
    /* 读取绑定地址和端口 */
    char dummy[256];
    recvLen = recv((SOCKET)sock, dummy, addrLen + 2, 0);
    if (recvLen != addrLen + 2) return false;
    
    return true;
}

bool XNetwork_httpConnect(XSocketHandle sock, const XNetworkProxy* proxy,
                          const XHostAddress* targetAddr, uint16_t targetPort)
{
    if (sock == -1 || !proxy || !targetAddr) return false;
    
    char request[512];
    char hostStr[128];
    int requestLen = 0;
    
    /* 获取目标地址字符串 */
    XString* addrStr = XHostAddress_toString(targetAddr);
    const char* hostStrPtr = addrStr ? XString_toUtf8(addrStr) : NULL;
    if (!hostStrPtr) {
        if (addrStr) XString_delete_base(addrStr);
        return false;
    }
    strncpy(hostStr, hostStrPtr, sizeof(hostStr) - 1);
    hostStr[sizeof(hostStr) - 1] = '\0';
    if (addrStr) XString_delete_base(addrStr);
    
    bool hasAuth = (proxy->user != NULL && proxy->password != NULL);
    
    /* 构建 HTTP CONNECT 请求 */
    if (hasAuth) {
        /* Base64 编码 user:pass */
        char auth[256];
        snprintf(auth, sizeof(auth), "%s:%s", proxy->user, proxy->password);
        
        /* 简单的 Base64 编码（生产环境应使用标准库） */
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char b64Auth[512];
        int ai = 0, bi = 0;
        while (auth[ai]) {
            uint32_t octet_a = (unsigned char)auth[ai++];
            uint32_t octet_b = ai < (int)strlen(auth) ? (unsigned char)auth[ai++] : 0;
            uint32_t octet_c = ai < (int)strlen(auth) ? (unsigned char)auth[ai++] : 0;
            uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
            b64Auth[bi++] = b64[(triple >> 18) & 0x3F];
            b64Auth[bi++] = b64[(triple >> 12) & 0x3F];
            b64Auth[bi++] = b64[(triple >> 6) & 0x3F];
            b64Auth[bi++] = b64[triple & 0x3F];
        }
        b64Auth[bi] = '\0';
        
        requestLen = snprintf(request, sizeof(request),
            "CONNECT %s:%d HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Proxy-Authorization: Basic %s\r\n"
            "\r\n",
            hostStr, targetPort, hostStr, targetPort, b64Auth);
    } else {
        requestLen = snprintf(request, sizeof(request),
            "CONNECT %s:%d HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "\r\n",
            hostStr, targetPort, hostStr, targetPort);
    }
    
    /* 发送请求 */
    int sent = send((SOCKET)sock, request, requestLen, 0);
    if (sent != requestLen) return false;
    
    /* 接收响应 */
    char response[256];
    int recvLen = recv((SOCKET)sock, response, sizeof(response) - 1, 0);
    if (recvLen <= 0) return false;
    response[recvLen] = '\0';
    
    /* 检查响应状态码 */
    if (strncmp(response, "HTTP/1.", 7) != 0) return false;
    
    int statusCode = 0;
    if (sscanf(response, "HTTP/1.%*d %d", &statusCode) != 1) return false;
    
    /* 2xx 表示成功 */
    return statusCode >= 200 && statusCode < 300;
}

bool XNetwork_socks5Bind(XSocketHandle sock, const XNetworkProxy* proxy,
                         XHostAddress* bindAddr, uint16_t* bindPort)
{
    if (sock == -1 || !proxy) return false;
    
    char buf[256];
    int bufLen = 0;
    
    bool hasAuth = (proxy->user != NULL && proxy->password != NULL);
    
    /* SOCKS5 握手（与 socks5Connect 相同） */
    buf[0] = 0x05;
    if (hasAuth) {
        buf[1] = 0x02;
        buf[2] = 0x00;
        buf[3] = 0x02;
        bufLen = 4;
    } else {
        buf[1] = 0x01;
        buf[2] = 0x00;
        bufLen = 3;
    }
    
    int sent = send((SOCKET)sock, buf, bufLen, 0);
    if (sent != bufLen) return false;
    
    char resp[2];
    int recvLen = recv((SOCKET)sock, resp, 2, 0);
    if (recvLen != 2 || resp[0] != 0x05) return false;
    
    /* 认证 */
    if (resp[1] == 0x02 && hasAuth) {
        size_t userLen = strlen(proxy->user);
        size_t passLen = strlen(proxy->password);
        buf[0] = 0x01;
        buf[1] = (char)userLen;
        memcpy(buf + 2, proxy->user, userLen);
        buf[2 + userLen] = (char)passLen;
        memcpy(buf + 3 + userLen, proxy->password, passLen);
        sent = send((SOCKET)sock, buf, (int)(3 + userLen + passLen), 0);
        if (sent != (int)(3 + userLen + passLen)) return false;
        recvLen = recv((SOCKET)sock, resp, 2, 0);
        if (recvLen != 2 || resp[1] != 0x00) return false;
    } else if (resp[1] != 0x00) {
        return false;
    }
    
    /* 发送 BIND 请求 */
    buf[0] = 0x05;
    buf[1] = 0x02; /* BIND 命令 */
    buf[2] = 0x00;
    buf[3] = 0x01; /* IPv4 */
    *((uint32_t*)(buf + 4)) = 0; /* 绑定所有地址 */
    *((uint16_t*)(buf + 8)) = 0; /* 绑定端口由代理分配 */
    bufLen = 10;
    
    sent = send((SOCKET)sock, buf, bufLen, 0);
    if (sent != bufLen) return false;
    
    /* 接收绑定响应 */
    recvLen = recv((SOCKET)sock, resp, 2, 0);
    if (recvLen != 2 || resp[1] != 0x00) return false;
    
    char addrType;
    recvLen = recv((SOCKET)sock, &addrType, 1, 0);
    if (recvLen != 1) return false;
    
    int addrLen = 0;
    switch (addrType) {
        case 0x01: addrLen = 4; break;
        case 0x04: addrLen = 16; break;
        case 0x03:
            recvLen = recv((SOCKET)sock, &addrLen, 1, 0);
            if (recvLen != 1) return false;
            break;
        default: return false;
    }
    
    /* 读取绑定地址和端口 */
    char addrBuf[16];
    recvLen = recv((SOCKET)sock, addrBuf, addrLen + 2, 0);
    if (recvLen != addrLen + 2) return false;
    
    /* 解析绑定地址 */
    if (bindAddr && addrType == 0x01) {
        uint32_t ip = ntohl(*((uint32_t*)addrBuf));
        XHostAddress_setAddressIPv4(bindAddr, ip);
    }
    if (bindPort) {
        *bindPort = ntohs(*((uint16_t*)(addrBuf + addrLen)));
    }
    
    return true;
}

XServerHandle XNetwork_serverCreateWithProxy(XNetworkSocketPrivate* priv,const XNetworkProxy* proxy,
                                             const XHostAddress* addr, uint16_t port,
                                             int backlog, bool reuseAddr)
{
    if (!addr) return -1;
    
    /* 无代理，使用普通服务器创建 */
    if (!proxy || proxy->type == XNetworkProxy_NoProxy) {
        return XNetwork_serverCreate(priv,addr, port, backlog, reuseAddr);
    }
    
    /* SOCKS5 代理支持 BIND 命令 */
    if (proxy->type == XNetworkProxy_Socks5Proxy) {
        /* 创建到代理服务器的连接 */
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return -1;
        
        /* 连接到代理服务器 */
        struct sockaddr_in proxyAddr;
        memset(&proxyAddr, 0, sizeof(proxyAddr));
        proxyAddr.sin_family = AF_INET;
        proxyAddr.sin_port = htons(proxy->port);
        
        /* 解析代理主机名（简化实现，仅支持 IP） */
        proxyAddr.sin_addr.s_addr = inet_addr(proxy->hostName ? proxy->hostName : "");
        if (proxyAddr.sin_addr.s_addr == INADDR_NONE) {
            /* 需要 DNS 解析 */
            struct hostent* he = gethostbyname(proxy->hostName ? proxy->hostName : "");
            if (!he) {
                closesocket(sock);
                return -1;
            }
            memcpy(&proxyAddr.sin_addr, he->h_addr_list[0], he->h_length);
        }
        
        if (connect(sock, (struct sockaddr*)&proxyAddr, sizeof(proxyAddr)) == SOCKET_ERROR) {
            closesocket(sock);
            return -1;
        }
        
        /* 发送 BIND 请求 */
        XHostAddress bindAddr;
        uint16_t bindPort;
        if (!XNetwork_socks5Bind((XSocketHandle)sock, proxy, &bindAddr, &bindPort)) {
            closesocket(sock);
            return -1;
        }
        priv->socket = sock;
        /* 返回套接字作为服务器句柄 */
        return (XServerHandle)sock;
    }
    
    /* HTTP 代理不支持 BIND */
    return -1;
}

XSocketHandle XNetwork_serverAcceptWithProxy(XServerHandle server,
                                             const XNetworkProxy* proxy,
                                             XHostAddress* clientAddr, uint16_t* clientPort)
{
    if (server == -1) return -1;
    
    /* 无代理，使用普通 accept */
    if (!proxy || proxy->type == XNetworkProxy_NoProxy) {
        return XNetwork_serverAccept(server, clientAddr, clientPort);
    }
    
    /* SOCKS5 代理：等待代理转发连接 */
    if (proxy->type == XNetworkProxy_Socks5Proxy) {
        SOCKET sock = (SOCKET)server;
        
        /* 等待代理发送第二个 BIND 响应 */
        char resp[2];
        int recvLen = recv(sock, resp, 2, 0);
        if (recvLen != 2 || resp[1] != 0x00) return -1;
        
        /* 读取客户端地址类型 */
        char addrType;
        recvLen = recv(sock, &addrType, 1, 0);
        if (recvLen != 1) return -1;
        
        int addrLen = 0;
        switch (addrType) {
            case 0x01: addrLen = 4; break;
            case 0x04: addrLen = 16; break;
            case 0x03:
                recvLen = recv(sock, &addrLen, 1, 0);
                if (recvLen != 1) return -1;
                break;
            default: return -1;
        }
        
        /* 读取客户端地址和端口 */
        char addrBuf[16];
        recvLen = recv(sock, addrBuf, addrLen + 2, 0);
        if (recvLen != addrLen + 2) return -1;
        
        /* 解析客户端地址 */
        if (clientAddr && addrType == 0x01) {
            uint32_t ip = ntohl(*((uint32_t*)addrBuf));
            XHostAddress_setAddressIPv4(clientAddr, ip);
        }
        if (clientPort) {
            *clientPort = ntohs(*((uint16_t*)(addrBuf + addrLen)));
        }
        
        /* 返回同一个套接字（SOCKS5 BIND 复用连接） */
        return (XSocketHandle)sock;
    }
    
    return -1;
}
