/**
 * @file XNetwork_win32.c
 * @brief Windows 平台完整网络实现（适配 IOCP 架构 + XHostAddress）
 * 
 * 本文件实现 XNetwork_platform.h 中声明的所有接口函数。
 * 所有底层 socket 操作封装在此处，使用现有的 IOCP 架构进行异步 IO 通知。
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include "XNetwork_platform.h"
#include "XAbstractSocket.h"
#include "XIODevice.h"
#include "XIODevicePrivate.h"
#include "IOCPInfo.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include <string.h>
#include <stdlib.h>

// 外部 IOCP 绑定函数（来自 XEventDispatcher_win.c）
extern bool IOCP_bind(XSocketDescriptor socket, XObject* obj);
extern HANDLE IOCP_getGlobalPort(void);

// ConnectEx 函数指针
#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX \
    {0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
#endif
typedef BOOL (PASCAL *LPFN_CONNECTEX)(
    SOCKET s,
    const struct sockaddr* name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped
);

static LPFN_CONNECTEX g_ConnectEx = NULL;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

/* =========================================================================
 * 内部全局状态
 * ========================================================================= */

static LONG    g_wsaRefCount = 0;
static HANDLE  g_iocp = NULL;           /* 本地缓存的全局 IOCP */

/* =========================================================================
 * 辅助函数
 * ========================================================================= */

/* ---- Winsock 引用计数管理 ---- */
static void wsa_enter(void)
{
    if (InterlockedIncrement(&g_wsaRefCount) == 1) {
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        
        // 获取 ConnectEx 函数指针
        SOCKET tempSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tempSock != INVALID_SOCKET) {
            DWORD dwBytes;
            GUID guidConnectEx = WSAID_CONNECTEX;
            WSAIoctl(tempSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guidConnectEx, sizeof(guidConnectEx),
                    &g_ConnectEx, sizeof(g_ConnectEx),
                    &dwBytes, NULL, NULL);
            closesocket(tempSock);
        }
    }
}

static void ensureWinsockInit(void) { wsa_enter(); }

static bool setNonBlocking(SOCKET s, bool nb)
{
    u_long m = nb ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &m) == 0;
}
static void wsa_leave(void)
{
    if (InterlockedDecrement(&g_wsaRefCount) == 0) {
        WSACleanup();
    }
}

/* ---- IOCP 句柄（延迟绑定到 XEventDispatcher 创建的全局端口） ---- */
static HANDLE iocp_get(void)
{
    if (!g_iocp) g_iocp = IOCP_getGlobalPort();
    return g_iocp;
}
static bool iocp_assoc(SOCKET s, void* key)
{
    HANDLE h = iocp_get();
    return h && CreateIoCompletionPort((HANDLE)s, h, (ULONG_PTR)key, 0) != NULL;
}

/* ---- 将 Windows socket 错误转换为 XAbstractSocket 错误 ---- */
static XAbstractSocket_SocketError winsockErrorToSocketError(int error)
{
    switch (error) {
    case WSAECONNREFUSED:     return XAbstractSocket_ConnectionRefusedError;
    case WSAECONNRESET:       return XAbstractSocket_RemoteHostClosedError;
    case WSAHOST_NOT_FOUND:   return XAbstractSocket_HostNotFoundError;
    case WSAEACCES:           return XAbstractSocket_SocketAccessError;
    case WSAENOBUFS:          return XAbstractSocket_SocketResourceError;
    case WSAETIMEDOUT:        return XAbstractSocket_SocketTimeoutError;
    case WSAEMSGSIZE:         return XAbstractSocket_DatagramTooLargeError;
    case WSAENETUNREACH:      return XAbstractSocket_NetworkError;
    case WSAEADDRINUSE:       return XAbstractSocket_AddressInUseError;
    case WSAEWOULDBLOCK:      return XAbstractSocket_TemporaryError;
    default:                  return XAbstractSocket_NetworkError;
    }
}

/* ---- 设置非阻塞 ---- */
static bool setNBO(SOCKET s, bool nb)
{
    u_long m = nb ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &m) == 0;
}

/* ---- XHostAddress → sockaddr_storage ---- */
static int addr2sa(const XHostAddress* a, uint16_t port,
                   struct sockaddr_storage* ss, int* ssLen)
{
    memset(ss, 0, sizeof(*ss));
    if (!a || XHostAddress_isNull(a)) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)ss;
        s4->sin_family = AF_INET;
        s4->sin_port   = htons(port);
        *ssLen = sizeof(*s4);
        return AF_INET;
    }
    XHostAddress_NetworkLayerProtocol p = XHostAddress_protocol(a);
    if (p == XHostAddress_IPv6Protocol) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)ss;
        s6->sin6_family = AF_INET6;
        s6->sin6_port   = htons(port);
        XHostAddress_toIPv6Address(a, s6->sin6_addr.s6_addr);
        const char* zid = XHostAddress_scopeId(a);
        if (zid && zid[0]) s6->sin6_scope_id = if_nametoindex(zid);
        *ssLen = sizeof(*s6);
        return AF_INET6;
    } else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)ss;
        s4->sin_family = AF_INET;
        s4->sin_port   = htons(port);
        uint32_t v4 = XHostAddress_toIPv4Address(a);
        memcpy(&s4->sin_addr, &v4, 4);
        *ssLen = sizeof(*s4);
        return AF_INET;
    }
}

/* ---- sockaddr → XHostAddress + port ---- */
static void sa2addr(const SOCKADDR_STORAGE* ss, XHostAddress* a, uint16_t* port)
{
    XHostAddress_init(a);
    if (ss->ss_family == AF_INET6) {
        const SOCKADDR_IN6* s6 = (const SOCKADDR_IN6*)ss;
        XHostAddress_setAddressIPv6(a, s6->sin6_addr.s6_addr);
        if (port) *port = ntohs(s6->sin6_port);
    } else if (ss->ss_family == AF_INET) {
        const SOCKADDR_IN* s4 = (const SOCKADDR_IN*)ss;
        XHostAddress_setAddressIPv4(a, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/* =========================================================================
 * 1. 平台初始化 / 清理 / 错误
 * ========================================================================= */

void XNetwork_ensureInit(void)      { wsa_enter(); }
void XNetwork_cleanup(void)          { wsa_leave(); }
int  XNetwork_lastError(void)        { return WSAGetLastError(); }

bool XNetwork_isEAgain(int e)
{
    return (e == WSAEWOULDBLOCK || e == WSA_IO_PENDING || e == WSAEINPROGRESS);
}

char* XNetwork_errorString(int ec)
{
    char* buf =NULL,*ret= NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)ec, 0, (LPSTR)&buf, 0, NULL);
    if (buf) {
        size_t len = strlen(buf) + 1;
        ret = (char*)XMalloc_System(len);
        if (ret) {
            memcpy(ret, buf, len);
        }
        LocalFree(buf);
    }
    return ret;
}

int XNetwork_classifyError(int ec)
{
    switch (ec) {
    case 0:                   return 0;
    case WSAECONNREFUSED:     return 1;
    case WSAETIMEDOUT:        return 2;
    case WSAEHOSTUNREACH:
    case WSAENETUNREACH:      return 3;
    case WSAECONNRESET:       return 4;
    case WSAEADDRINUSE:       return 5;
    default:                  return -1;
    }
}

/* =========================================================================
 * 2. 核心套接字
 * ========================================================================= */

XSocketHandle XNetwork_createSocket(XNetworkSocketType type, XNetworkProtocol proto)
{
    XNetwork_ensureInit();
    int fam = (proto == XHostAddress_IPv6Protocol) ? AF_INET6 :
              (proto == XHostAddress_AnyIPProtocol)  ? AF_INET6 : AF_INET;
    int st  = (type == XNetworkSocket_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    int ip  = (type == XNetworkSocket_UDP) ? IPPROTO_UDP : IPPROTO_TCP;

    SOCKET s = socket(fam, st, ip);
    if (s == INVALID_SOCKET) return (XSocketHandle)-1;

    if (fam == AF_INET6) {
        int off = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off));
    }
    setNBO(s, true);
    /* 显式绑定到 IOCP，key = NULL（中间层可后续覆盖） */
    iocp_assoc(s, NULL);
    return (XSocketHandle)s;
}

void XNetwork_closeSocket(XSocketHandle sock)
{
    if (sock != -1) closesocket((SOCKET)sock);
}

bool XNetwork_bind(XSocketHandle sock, const XHostAddress* addr, uint16_t port,
                   bool reuseAddr, bool shareAddr)
{
    if (sock == -1) return false;
    int reuse = reuseAddr ? 1 : 0;
    setsockopt((SOCKET)sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_storage ss;
    int ssLen = 0;
    addr2sa(addr, port, &ss, &ssLen);
    return bind((SOCKET)sock, (const struct sockaddr*)&ss, ssLen) == 0;
}

bool XNetwork_connect(XSocketHandle sock, const XHostAddress* addr, uint16_t port)
{
    if (sock == -1 || !addr) return false;
    struct sockaddr_storage ss;
    int ssLen = 0;
    addr2sa(addr, port, &ss, &ssLen);
    int rc = connect((SOCKET)sock, (const struct sockaddr*)&ss, ssLen);
    if (rc == 0) return true;
    return XNetwork_isEAgain(WSAGetLastError());
}

bool XNetwork_shutdown(XSocketHandle sock, int how)
{
    if (sock == -1) return false;
    int h = (how == 0) ? SD_RECEIVE : (how == 1) ? SD_SEND : SD_BOTH;
    return shutdown((SOCKET)sock, h) == 0;
}

int64_t XNetwork_recv(XSocketHandle sock, void* buf, int64_t len,
                      bool isDatagram, XHostAddress* sender, uint16_t* senderPort)
{
    if (sock == -1 || !buf || len <= 0) return -1;
    if (isDatagram) {
        struct sockaddr_storage from;
        int fromLen = sizeof(from);
        int rc = recvfrom((SOCKET)sock, (char*)buf, (int)len, 0,
                          (struct sockaddr*)&from, &fromLen);
        if (rc >= 0 && sender) sa2addr(&from, sender, senderPort);
        return rc;
    }
    return recv((SOCKET)sock, (char*)buf, (int)len, 0);
}

int64_t XNetwork_send(XSocketHandle sock, const void* buf, int64_t len,
                      bool isDatagram, const XHostAddress* receiver, uint16_t receiverPort)
{
    if (sock == -1 || !buf || len <= 0) return -1;
    if (isDatagram && receiver) {
        struct sockaddr_storage ss;
        int ssLen = 0;
        addr2sa(receiver, receiverPort, &ss, &ssLen);
        return sendto((SOCKET)sock, (const char*)buf, (int)len, 0,
                      (const struct sockaddr*)&ss, ssLen);
    }
    return send((SOCKET)sock, (const char*)buf, (int)len, 0);
}

bool XNetwork_getLocalAddr(XSocketHandle sock, XHostAddress* addr, uint16_t* port)
{
    if (sock == -1 || !addr) return false;
    struct sockaddr_storage ss = { 0 };
    int ssLen = sizeof(ss);
    if (getsockname((SOCKET)sock, (SOCKADDR*)&ss, &ssLen) != 0) return false;
    sa2addr(&ss, addr, port);
    return true;
}

bool XNetwork_getPeerAddr(XSocketHandle sock, XHostAddress* addr, uint16_t* port)
{
    if (sock == -1 || !addr) return false;
    struct sockaddr_storage ss = { 0 };
    int ssLen = sizeof(ss);
    if (getpeername((SOCKET)sock, (SOCKADDR*)&ss, &ssLen) != 0) return false;
    sa2addr(&ss, addr, port);
    return true;
}

/* =========================================================================
 * 3. TCP 服务器
 * ========================================================================= */

XServerHandle XNetwork_createServer(const XHostAddress* addr, uint16_t port,
                                    int backlog, bool reuseAddr)
{
    XNetwork_ensureInit();
    SOCKET s = INVALID_SOCKET;

    /* ---- 优先尝试 IPv6 双栈套接字 ---- */
    {
        SOCKET s6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (s6 != INVALID_SOCKET) {
            int off = 0, reuse = reuseAddr ? 1 : 0;
            setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off));
            setsockopt(s6, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

            struct sockaddr_storage ss;
            int ssLen = 0;
            addr2sa(addr, port, &ss, &ssLen);

            if (bind(s6, (SOCKADDR*)&ss, ssLen) == 0 &&
                listen(s6, (backlog > 0 ? backlog : SOMAXCONN)) == 0) {
                setNBO(s6, true);
                iocp_assoc(s6, NULL);
                return (XServerHandle)s6;
            }
            closesocket(s6);
        }
    }

    /* ---- IPv4 回退 ---- */
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;

    int reuse = reuseAddr ? 1 : 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_storage ss;
    int ssLen = 0;
    addr2sa(addr, port, &ss, &ssLen);

    if (bind(s, (const SOCKADDR*)&ss, ssLen) == 0 &&
        listen(s, (backlog > 0 ? backlog : SOMAXCONN)) == 0) {
        setNBO(s, true);
        iocp_assoc(s, NULL);
        return (XServerHandle)s;
    }
    closesocket(s);
    return -1;
}

XSocketHandle XNetwork_serverAccept(XServerHandle server,
                                    XHostAddress* clientAddr, uint16_t* clientPort)
{
    if (server == -1) return -1;

    SOCKADDR_STORAGE ca;
    int caLen = sizeof(ca);
    SOCKET cs = accept((SOCKET)server, (SOCKADDR*)&ca, &caLen);
    if (cs == INVALID_SOCKET) return -1;

    setNBO(cs, true);
    iocp_assoc(cs, NULL);
    if (clientAddr) sa2addr(&ca, clientAddr, clientPort);

    return (XSocketHandle)cs;
}

uint16_t XNetwork_serverPort(XServerHandle server)
{
    if (server == -1) return 0;
    SOCKADDR_STORAGE ss;
    int ssLen = sizeof(ss);
    if (getsockname((SOCKET)server, (SOCKADDR*)&ss, &ssLen) != 0) return 0;
    uint16_t p = 0;
    sa2addr(&ss, NULL, &p);
    return p;
}

void XNetwork_closeServer(XServerHandle server)
{
    if (server != -1) closesocket((SOCKET)server);
}

/* =========================================================================
 * 4. DNS
 * ========================================================================= */

bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count)
{
    if (!name || !addrs || !count) return false;
    XNetwork_ensureInit();

    struct addrinfo hints = { 0 }, *res = NULL, *p = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(name, NULL, &hints, &res) != 0 || !res) return false;

    /* 第一次遍历：计数 */
    int n = 0;
    for (p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET || p->ai_family == AF_INET6) n++;
    }
        if (n == 0) {
        freeaddrinfo(res);
        return false;
    }

    /* 第二次遍历：收集地址 */
    *addrs = (XHostAddress*)XMalloc_System(sizeof(XHostAddress) * n);
    if (!*addrs) {
        freeaddrinfo(res);
        return false;
    }

    int i = 0;
    for (p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            XHostAddress_init(&(*addrs)[i]);
            XHostAddress_setAddressIPv4(&(*addrs)[i],
                ntohl(((struct sockaddr_in*)p->ai_addr)->sin_addr.s_addr));
            i++;
        } else if (p->ai_family == AF_INET6) {
            XHostAddress_init(&(*addrs)[i]);
            XHostAddress_setAddressIPv6(&(*addrs)[i],
                ((struct sockaddr_in6*)p->ai_addr)->sin6_addr.s6_addr);
            i++;
        }
    }
    freeaddrinfo(res);
    *count = n;
    return true;
}

char* XNetwork_localHostName(void)
{
    char buf[256] = { 0 };
    if (gethostname(buf, sizeof(buf)) != 0) return NULL;
    return _strdup(buf);
}

char* XNetwork_localDomainName(void)
{
    /* Windows 无直接的域名获取 API，回退到 GetComputerNameEx + DNS */
    DWORD size = 256;
    char buf[256] = { 0 };
    if (!GetComputerNameExA(ComputerNameDnsDomain, buf, &size))
        return NULL;
    return (buf[0] == '\0') ? NULL : _strdup(buf);
}

/* =========================================================================
 * 5. 网络接口枚举
 * ========================================================================= */

typedef struct {
    PIP_ADAPTER_ADDRESSES head;
    PIP_ADAPTER_ADDRESSES cur;
} XNetworkInterfaceIter;

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void)
{
    ULONG size = 16384;  /* 初始 16KB */
    PIP_ADAPTER_ADDRESSES pa = NULL;
    ULONG rc = 0;
    for (int retry = 0; retry < 3; ++retry) {
        pa = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
        if (!pa) return NULL;
        rc = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            NULL, pa, &size);
        if (rc == ERROR_BUFFER_OVERFLOW) {
            XFree_System(pa);
            continue;
        }
        if (rc == NO_ERROR) break;
        XFree_System(pa);
        pa = NULL;
    }
    if (!pa) return NULL;

    XNetworkInterfaceIter* iter = XNew(XNetworkInterfaceIter);
    iter->head = pa;
    iter->cur  = pa;
    return iter;
}

bool XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter,
                                 XNetworkInterfaceEntry* out)
{
    if (!iter || !out) return false;
    XNetworkInterfaceIter* it = (XNetworkInterfaceIter*)iter;
    if (!it->cur) return false;

    PIP_ADAPTER_ADDRESSES cur = it->cur;
    memset(out, 0, sizeof(*out));
    WideCharToMultiByte(CP_UTF8, 0, cur->FriendlyName ? cur->FriendlyName : L"",
                        -1, out->readableName, sizeof(out->readableName)-1, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cur->AdapterName,
                        -1, out->name, sizeof(out->name)-1, NULL, NULL);
    out->index    = cur->Ipv6IfIndex;
    out->hwAddrLen = (int)cur->PhysicalAddressLength;
    if (out->hwAddrLen > 32) out->hwAddrLen = 32;
    memcpy(out->hwAddr, cur->PhysicalAddress, out->hwAddrLen);

    out->flags = 0;
    if (cur->OperStatus == IfOperStatusUp)      out->flags |= XNetworkIf_Up;
    if (!(cur->IfType == IF_TYPE_SOFTWARE_LOOPBACK)) out->flags |= XNetworkIf_Running;
    if (cur->IfType == IF_TYPE_SOFTWARE_LOOPBACK)    out->flags |= XNetworkIf_Loopback;
    if (cur->Flags & IP_ADAPTER_NO_MULTICAST)   /* noop */;
    else                                         out->flags |= XNetworkIf_Multicast;
    if (!(cur->Flags & IP_ADAPTER_NO_MULTICAST)) out->flags |= XNetworkIf_Broadcast;
    out->type = cur->IfType;
    out->mtu  = (int)cur->Mtu;

    it->cur = cur->Next;
    return true;
}

void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter)
{
    if (!iter) return;
    XNetworkInterfaceIter* it = (XNetworkInterfaceIter*)iter;
    XFree_System(it->head);
    XFree_System(it);
}

bool XNetwork_getInterfaceAddresses(const char* ifname,
                                    XHostAddress** addrs, XHostAddress** masks,
                                    int* count)
{
    if (!ifname || !addrs || !count) return false;

    PIP_ADAPTER_ADDRESSES pa = NULL;
    ULONG size = 16384;
    pa = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
    if (!pa) return false;
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC,
        GAA_FLAG_INCLUDE_PREFIX, NULL, pa, &size);
    if (rc != NO_ERROR) {
        XFree_System(pa); return false;
    }

    int cnt = 0;
    PIP_ADAPTER_ADDRESSES cur = NULL;
    for (cur = pa; cur; cur = cur->Next) {
        char aname[128];
        WideCharToMultiByte(CP_UTF8, 0, cur->AdapterName, -1,
                            aname, sizeof(aname)-1, NULL, NULL);
        if (strcmp(ifname, aname) != 0) continue;

        /* 统计单播地址数 */
        for (PIP_ADAPTER_UNICAST_ADDRESS u = cur->FirstUnicastAddress; u; u = u->Next)
            cnt++;
        break;
    }

    if (cnt == 0) { XFree_System(pa); *count = 0; return true; }

    *addrs = (XHostAddress*)XMalloc_System(sizeof(XHostAddress) * cnt);
    *masks = (XHostAddress*)XMalloc_System(sizeof(XHostAddress) * cnt);
    if (!*addrs || !*masks) {
        if (*addrs) XFree_System(*addrs);
        if (*masks) XFree_System(*masks);
        XFree_System(pa);
        return false;
    }

    int i = 0;
    for (cur = pa; cur; cur = cur->Next) {
        char aname[128];
        WideCharToMultiByte(CP_UTF8, 0, cur->AdapterName, -1,
                            aname, sizeof(aname)-1, NULL, NULL);
        if (strcmp(ifname, aname) != 0) continue;

        for (PIP_ADAPTER_UNICAST_ADDRESS u = cur->FirstUnicastAddress; u; u = u->Next) {
            XHostAddress_init(&(*addrs)[i]);
            XHostAddress_init(&(*masks)[i]);

                        LPSOCKADDR sa = u->Address.lpSockaddr;
            if (sa->sa_family == AF_INET) {
                SOCKADDR_IN* s4 = (SOCKADDR_IN*)sa;
                XHostAddress_setAddressIPv4(&(*addrs)[i], ntohl(s4->sin_addr.s_addr));
                /* 从子网前缀长度计算掩码 */
                uint8_t plen = u->OnLinkPrefixLength;
                uint32_t mask = (plen == 0) ? 0 : (~0u << (32 - plen));
                XHostAddress_setAddressIPv4(&(*masks)[i], mask);
            } else if (sa->sa_family == AF_INET6) {
                SOCKADDR_IN6* s6 = (SOCKADDR_IN6*)sa;
                XHostAddress_setAddressIPv6(&(*addrs)[i], s6->sin6_addr.s6_addr);
                /* 简化的 IPv6 掩码生成 */
                uint8_t plen = u->OnLinkPrefixLength;
                uint8_t raw[16] = { 0 };
                for (int j = 0; j < plen / 8; ++j)
                    raw[j] = 0xff;
                if (plen % 8)
                    raw[plen / 8] = (uint8_t)(0xff00 >> (plen % 8));
                XHostAddress_setAddressIPv6(&(*masks)[i], raw);
            }
            i++;
        }
    }
    XFree_System(pa);
    *count = cnt;
    return true;
}

/* =========================================================================
 * 6. 事件通知（集成 IOCP / epoll）
 * ========================================================================= */

typedef struct {
    XNetworkEventCb cb;
    void* userData;
} XNetwork_IOCP_UserData;

bool XNetwork_registerEvents(XSocketHandle sock, XNetworkEventCb cb, void* userData)
{
    if (sock == -1 || !cb) return false;

    XNetwork_IOCP_UserData* ud = (XNetwork_IOCP_UserData*)XMalloc_System(
        sizeof(XNetwork_IOCP_UserData));
    if (!ud) return false;
    ud->cb       = cb;
    ud->userData = userData;

    HANDLE h = iocp_get();
    if (!h || !CreateIoCompletionPort((HANDLE)sock, h, (ULONG_PTR)ud, 0)) {
        XFree_System(ud);
        return false;
    }
    return true;
}

void XNetwork_unregisterEvents(XSocketHandle sock)
{
    /* 解除 IOCP 绑定。后续可通过 getsockopt 获取完成键并释放 */
    if (sock != -1) {
        closesocket((SOCKET)sock); /* 关闭前，系统会 cancel 所有重叠操作 */
    }
}

bool XNetwork_serverRegisterEvents(XServerHandle server, XNetworkEventCb cb, void* userData)
{
    return XNetwork_registerEvents(server, cb, userData);
}

void XNetwork_serverUnregisterEvents(XServerHandle server)
{
    XNetwork_unregisterEvents(server);
}

/* =========================================================================
 * 7. SOCKS5
 * ========================================================================= */

static int socks5_send_recv(SOCKET s, const void* sendBuf, int sendLen,
                             void* recvBuf, int recvLen)
{
    int sent = send(s, (const char*)sendBuf, sendLen, 0);
    if (sent != sendLen) return -1;
    int rcvd = recv(s, (char*)recvBuf, recvLen, 0);
    return rcvd;
}

bool XNetwork_socks5Connect(XSocketHandle sock, const XNetworkProxyInfo* proxy,
                            const XHostAddress* targetAddr, uint16_t targetPort)
{
    if (sock == -1 || !proxy || !targetAddr) return false;

    SOCKET s = (SOCKET)sock;

    // SOCKS5 版本协商
    {
        uint8_t req[] = { 0x05, 0x01, 0x00 };  /* 无认证 */
        uint8_t resp[2] = { 0 };
        int rc = socks5_send_recv(s, req, 3, resp, 2);
        if (rc != 2 || resp[0] != 0x05) return false;
        if (resp[1] == 0xFF) return false;

                if (resp[1] == 0x02) {  /* 用户名/密码认证 */
            if (!proxy->user) return false;
            size_t ulen = strlen(proxy->user);
            size_t plen = proxy->pass ? strlen(proxy->pass) : 0;
            if (ulen > 255 || plen > 255) return false;

            uint8_t ar[515];
            ar[0] = 0x01;          /* auth 子协商版本 */
            ar[1] = (uint8_t)ulen;
            memcpy(ar + 2, proxy->user, ulen);
            ar[2 + ulen] = (uint8_t)plen;
            if (plen > 0) memcpy(ar + 3 + ulen, proxy->pass, plen);

            uint8_t aresp[2] = { 0 };
            int auth_rc = socks5_send_recv(s, ar, 3 + (int)(ulen + plen), aresp, 2);
            if (auth_rc != 2 || aresp[1] != 0x00) return false;
        }
    }

    // SOCKS5 CONNECT
    {
        uint8_t req[262];
        req[0] = 0x05;  /* ver */
        req[1] = 0x01;  /* CMD: CONNECT */
        req[2] = 0x00;  /* RSV */

        XHostAddress_NetworkLayerProtocol proto = XHostAddress_protocol(targetAddr);
        if (proto == XHostAddress_IPv4Protocol) {
            req[3] = 0x01;  /* ATYP: IPv4 */
            uint32_t v4 = XHostAddress_toIPv4Address(targetAddr);
            memcpy(req + 4, &v4, 4);
            uint16_t np = htons(targetPort);
            memcpy(req + 8, &np, 2);
            uint8_t resp[10] = { 0 };
            int rc = socks5_send_recv(s, req, 10, resp, 10);
            if (rc < 10 || resp[1] != 0x00) return false;
        } else if (proto == XHostAddress_IPv6Protocol) {
            req[3] = 0x04;  /* ATYP: IPv6 */
            XHostAddress_toIPv6Address(targetAddr, req + 4);
            uint16_t np = htons(targetPort);
            memcpy(req + 20, &np, 2);
            uint8_t resp[22] = { 0 };
            int rc = socks5_send_recv(s, req, 22, resp, 22);
            if (rc < 22 || resp[1] != 0x00) return false;
        } else {
            return false;  /* 不支持域名解析代理 */
        }
    }
    return true;
}

/* =========================================================================
 * 8. 套接字私有数据管理（供 XAbstractSocket 使用）
 * 
 * 与 XEventDispatcher 的 IOCP 系统配合：
 * - owner 作为 IOCP 完成键，在事件完成时由 XEventDispatcher 回调
 * - 套接字通过 CreateIoCompletionPort 关联到全局 IOCP 端口
 * ========================================================================= */

// IOCP 缓冲区大小
#define XNETWORK_READ_BUFFER_SIZE  8192
#define XNETWORK_WRITE_BUFFER_SIZE 8192

struct XNetworkSocketPrivate {
    SOCKET socket;                      ///< Windows SOCKET 句柄
    void* owner;                        ///< 拥有者对象（XAbstractSocket*）
    uint16_t pendingPeerPort;
    // 状态标志
    uint16_t readPending:1;                   ///< 读操作是否挂起
    uint16_t writePending : 1;                  ///< 写操作是否挂起
    uint16_t connectPending : 1;                ///< 连接操作是否挂起
    uint16_t connected : 1;                     ///< 是否已连接
    uint16_t autoRead : 1;                      ///< 是否自动读取
    int fromAddrLen;
    // UDP 来源地址
    struct sockaddr_in6 fromAddr;
    // IOCP 上下文
    XEventContext_IOCP readContext;     ///< 读操作 IOCP 上下文
    XEventContext_IOCP writeContext;    ///< 写操作 IOCP 上下文
    XEventContext_IOCP connectContext;  ///< 连接操作 IOCP 上下文
    // 待连接信息
    XHostAddress pendingPeerAddr;
    // 缓冲区
    char readBuffer[XNETWORK_READ_BUFFER_SIZE];
    char writeBuffer[XNETWORK_WRITE_BUFFER_SIZE];
};

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner)
{
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)XCalloc_System(1, sizeof(XNetworkSocketPrivate));
    if (!priv) return NULL;
    
    priv->socket = INVALID_SOCKET;
    priv->owner = owner;
    priv->readPending = false;
    priv->writePending = false;
    priv->connectPending = false;
    priv->connected = false;
    priv->autoRead = true;
    priv->fromAddrLen = 0;
    
    XHostAddress_init(&priv->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&priv->pendingPeerAddr, XHostAddress_NullSpecial);
    priv->pendingPeerPort = 0;
    
    return priv;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    
    if (priv->socket != INVALID_SOCKET) {
        CancelIo((HANDLE)priv->socket);
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
    }
    
    XHostAddress_deinit_base(&priv->pendingPeerAddr);
    XFree_System(priv);
}

XSocketHandle XNetwork_privateSocket(const XNetworkSocketPrivate* priv)
{
    return priv ? (XSocketHandle)priv->socket : -1;
}

void XNetwork_privateSetSocket(XNetworkSocketPrivate* priv, XSocketHandle sock)
{
    if (!priv) return;
    
    priv->socket = (SOCKET)sock;
    
    // 将套接字关联到 IOCP，完成键为 owner
    if (sock != -1 && priv->owner) {
        HANDLE h = iocp_get();
        if (h) {
            CreateIoCompletionPort((HANDLE)sock, h, (ULONG_PTR)priv->owner, 0);
        }
    }
}

bool XNetwork_privateIsConnecting(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->connectPending : false;
}

void XNetwork_privateSetConnecting(XNetworkSocketPrivate* priv, bool connecting)
{
    if (priv) priv->connectPending = connecting;
}

void* XNetwork_privateOwner(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->owner : NULL;
}

const XHostAddress* XNetwork_privatePendingPeerAddr(const XNetworkSocketPrivate* priv)
{
    return priv ? &priv->pendingPeerAddr : NULL;
}

void XNetwork_privateSetPendingPeerAddr(XNetworkSocketPrivate* priv, const XHostAddress* addr)
{
    if (priv && addr) {
        XHostAddress_copy_base(&priv->pendingPeerAddr, addr);
    }
}

uint16_t XNetwork_privatePendingPeerPort(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->pendingPeerPort : 0;
}

void XNetwork_privateSetPendingPeerPort(XNetworkSocketPrivate* priv, uint16_t port)
{
    if (priv) priv->pendingPeerPort = port;
}

// ==================== IOCP 异步操作辅助函数 ====================

// 启动异步读取
void XNetwork_startAsyncRead(XNetworkSocketPrivate* priv, bool isUdp);

void XNetwork_startAsyncRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv || priv->readPending || priv->socket == INVALID_SOCKET) return;
    
    memset(&priv->readContext, 0, sizeof(XEventContext_IOCP));
    priv->readContext.type = XEventContextType_Type_Socket;
    priv->readContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
    priv->readContext.eventMask = FD_READ;
    priv->readContext.buffer = priv->readBuffer;
    priv->readContext.bufferSize = sizeof(priv->readBuffer);
    
    DWORD flags = 0;
    WSABUF buf;
    buf.buf = priv->readBuffer;
    buf.len = sizeof(priv->readBuffer);
    
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
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return;
    }
    priv->readPending = true;
}

// ==================== 高级操作实现（供 XAbstractSocket 使用）====================

bool XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                         uint16_t port, bool reuseAddr, bool shareAddr, int sockType)
{
    if (!priv || !address) return false;
    
    ensureWinsockInit();
    
    int family = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    priv->socket = socket(family, (sockType == 1) ? SOCK_DGRAM : SOCK_STREAM, IPPROTO_IP);
    if (priv->socket == INVALID_SOCKET) return false;
    
    setNBO(priv->socket, true);
    
    if (reuseAddr) {
        int opt = 1;
        setsockopt(priv->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    }
    
    struct sockaddr_storage addrStorage;
    int addrLen = 0;
    
    if (family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addrStorage;
        memset(addr4, 0, sizeof(*addr4));
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        uint32_t ipv4 = XHostAddress_toIPv4Address(address);
        memcpy(&addr4->sin_addr, &ipv4, 4);
        addrLen = sizeof(*addr4);
    } else {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addrStorage;
        memset(addr6, 0, sizeof(*addr6));
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        uint8_t ipv6[16];
        XHostAddress_toIPv6Address(address, ipv6);
        memcpy(&addr6->sin6_addr, ipv6, 16);
        addrLen = sizeof(*addr6);
    }
    
    if (bind(priv->socket, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        return false;
    }
    
    // 绑定到 IOCP
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socket), priv->owner)) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        return false;
    }
    
    return true;
}

bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, int protocol, int sockType,
                            const void* proxy)
{
    if (!priv || !hostName) return false;
    
    ensureWinsockInit();
    
    // UDP 特殊处理：只需绑定本地端口，设置对端地址
    if (sockType == 1) { // UDP
        // 创建 UDP 套接字
        int family = (protocol == 4) ? AF_INET : (protocol == 6) ? AF_INET6 : AF_INET6;
        priv->socket = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (priv->socket == INVALID_SOCKET) return false;
        
        setNBO(priv->socket, true);
        
        // 绑定到 IOCP
        if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socket), priv->owner)) {
            closesocket(priv->socket);
            priv->socket = INVALID_SOCKET;
            return false;
        }
        
        // 绑定本地地址
        struct sockaddr_storage localAddr = {0};
        localAddr.ss_family = family;
        int localLen = (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        if (bind(priv->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
            closesocket(priv->socket);
            priv->socket = INVALID_SOCKET;
            return false;
        }
        
        // UDP 不需要连接，直接设置状态
        priv->connected = true;
        priv->autoRead = true;
        XNetwork_startAsyncRead(priv, true);
        return true;
    }
    
    // TCP 连接流程
    // DNS 解析
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = (protocol == 4) ? AF_INET : (protocol == 6) ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%u", port);
    
    if (getaddrinfo(hostName, portStr, &hints, &result) != 0 || !result) {
        return false;
    }
    
    // 创建套接字
    priv->socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (priv->socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }
    
    setNBO(priv->socket, true);
    
    // 绑定到 IOCP
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socket), priv->owner)) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }
    
    // 绑定本地地址（ConnectEx 要求）
    struct sockaddr_storage localAddr = {0};
    localAddr.ss_family = result->ai_family;
    int localLen = (result->ai_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    if (bind(priv->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }
    
    // 使用 ConnectEx 异步连接
    if (g_ConnectEx) {
        memset(&priv->connectContext, 0, sizeof(XEventContext_IOCP));
        priv->connectContext.type = XEventContextType_Type_Socket;
        priv->connectContext.socket = XSocketDescriptor_fromIntptr(priv->socket);
        priv->connectContext.eventMask = FD_CONNECT;
        
        DWORD bytesSent = 0;
        BOOL connectResult = g_ConnectEx(priv->socket, result->ai_addr, (int)result->ai_addrlen,
            NULL, 0, &bytesSent, (OVERLAPPED*)&priv->connectContext);
        freeaddrinfo(result);
        
        if (!connectResult && WSAGetLastError() != WSA_IO_PENDING) {
            closesocket(priv->socket);
            priv->socket = INVALID_SOCKET;
            return false;
        }
        priv->connectPending = true;
        return true;
    } else {
        // 回退到普通 connect
        int ret = connect(priv->socket, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result);
        
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                closesocket(priv->socket);
                priv->socket = INVALID_SOCKET;
                return false;
            }
        }
        priv->connectPending = true;
        return true;
    }
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv)
{
    if (!priv) return;
    
    if (priv->socket != INVALID_SOCKET) {
        CancelIo((HANDLE)priv->socket);
        closesocket(priv->socket);
        priv->socket = INVALID_SOCKET;
    }
    
    priv->readPending = false;
    priv->writePending = false;
    priv->connectPending = false;
    priv->connected = false;
}

int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            int sockType, void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    
    // 先从内部缓冲区读取
    if (ringBuffer) {
        struct XRingBuffer* rb = (struct XRingBuffer*)ringBuffer;
        if (XRingBuffer_available(rb) > 0) {
            return XRingBuffer_read(rb, buf, len);
        }
    }
    
    // 缓冲区无数据，返回 0（异步模式下不阻塞）
    return 0;
}

int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             int sockType, const XHostAddress* destAddr, uint16_t destPort,
                             void* ringBuffer)
{
    if (!priv || !buf || len <= 0) return -1;
    if (priv->socket == INVALID_SOCKET) return -1;
    
    // 如果有挂起的写操作，写入缓冲区
    if (priv->writePending && ringBuffer) {
        return XRingBuffer_write((struct XRingBuffer*)ringBuffer, buf, len);
    }
    
    int sent;
    if (sockType == 1) { // UDP
        struct sockaddr_storage addrStorage;
        int addrLen = 0;
        
        if (XHostAddress_protocol(destAddr) == XHostAddress_IPv6Protocol) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addrStorage;
            memset(addr6, 0, sizeof(*addr6));
            addr6->sin6_family = AF_INET6;
            addr6->sin6_port = htons(destPort);
            uint8_t ipv6[16];
            XHostAddress_toIPv6Address(destAddr, ipv6);
            memcpy(&addr6->sin6_addr, ipv6, 16);
            addrLen = sizeof(*addr6);
        } else {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)&addrStorage;
            memset(addr4, 0, sizeof(*addr4));
            addr4->sin_family = AF_INET;
            addr4->sin_port = htons(destPort);
            uint32_t ipv4 = XHostAddress_toIPv4Address(destAddr);
            memcpy(&addr4->sin_addr, &ipv4, 4);
            addrLen = sizeof(*addr4);
        }
        sent = sendto(priv->socket, buf, (int)len, 0, (struct sockaddr*)&addrStorage, addrLen);
    } else {
        sent = send(priv->socket, buf, (int)len, 0);
    }
    
    if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK && ringBuffer) {
            return XRingBuffer_write((struct XRingBuffer*)ringBuffer, buf, len);
        }
        return -1;
    }
    
    return sent;
}

bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, XEvent* e)
{
    if (!priv || !e) return false;
    
    if (e->type == XEVENT_TYPE_SOCK_ACT) {
        XEventSockAct* sockAct = (XEventSockAct*)e;
        
        // 读取完成：更新内部状态
        if (sockAct->actType & XSocketAct_Read) {
            priv->readPending = false;
        }
        // 写入完成：更新内部状态
        if (sockAct->actType & XSocketAct_Write) {
            priv->writePending = false;
        }
        // 连接完成：更新内部状态
        if (sockAct->actType & XSocketAct_Connect) {
            priv->connectPending = false;
            priv->connected = true;
        }
        return true;
    }
    else if (e->type == XEVENT_TYPE_SOCK_CLOSE) {
        XNetwork_socketDisconnect(priv);
        return true;
    }
    
    return false;
}

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value)
{
    if (!priv || !value || priv->socket == INVALID_SOCKET) return false;
    
    const XVariant* variant = (const XVariant*)value;
    int level = SOL_SOCKET;
    int optname = 0;
    int intVal = 0;
    
    switch (option) {
    case XAbstractSocket_LowDelayOption:
        level = IPPROTO_TCP;
        optname = TCP_NODELAY;
        intVal = XVariant_toBool(variant) ? 1 : 0;
        break;
    case XAbstractSocket_KeepAliveOption:
        optname = SO_KEEPALIVE;
        intVal = XVariant_toBool(variant) ? 1 : 0;
        break;
    case XAbstractSocket_MulticastTtlOption:
        level = IPPROTO_IP;
        optname = IP_TTL;
        intVal = XVariant_toInt(variant);
        break;
    case XAbstractSocket_MulticastLoopbackOption:
        level = IPPROTO_IP;
        optname = IP_MULTICAST_LOOP;
        intVal = XVariant_toBool(variant) ? 1 : 0;
        break;
    case XAbstractSocket_TypeOfServiceOption:
        level = IPPROTO_IP;
        optname = IP_TOS;
        intVal = XVariant_toInt(variant);
        break;
    case XAbstractSocket_SendBufferSizeSocketOption:
        optname = SO_SNDBUF;
        intVal = XVariant_toInt(variant);
        break;
    case XAbstractSocket_ReceiveBufferSizeSocketOption:
        optname = SO_RCVBUF;
        intVal = XVariant_toInt(variant);
        break;
    case XAbstractSocket_PathMtuSocketOption:
        // 只读选项，不能设置
        return false;
    default:
        return false;
    }
    
    return setsockopt(priv->socket, level, optname, (const char*)&intVal, sizeof(intVal)) == 0;
}

void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option)
{
    if (!priv || priv->socket == INVALID_SOCKET) return NULL;
    
    static int result; // 注意：静态变量，非线程安全
    
    int level = SOL_SOCKET;
    int optname = 0;
    int optlen = sizeof(result);
    
    switch (option) {
    case XAbstractSocket_LowDelayOption:
        level = IPPROTO_TCP;
        optname = TCP_NODELAY;
        break;
    case XAbstractSocket_KeepAliveOption:
        optname = SO_KEEPALIVE;
        break;
    case XAbstractSocket_MulticastTtlOption:
        level = IPPROTO_IP;
        optname = IP_TTL;
        break;
    case XAbstractSocket_MulticastLoopbackOption:
        level = IPPROTO_IP;
        optname = IP_MULTICAST_LOOP;
        break;
    case XAbstractSocket_TypeOfServiceOption:
        level = IPPROTO_IP;
        optname = IP_TOS;
        break;
    case XAbstractSocket_SendBufferSizeSocketOption:
        optname = SO_SNDBUF;
        break;
    case XAbstractSocket_ReceiveBufferSizeSocketOption:
        optname = SO_RCVBUF;
        break;
    case XAbstractSocket_PathMtuSocketOption:
        // 获取路径 MTU
        level = IPPROTO_IP;
        optname = IP_MTU_DISCOVER;
        break;
    default:
        return NULL;
    }
    
    if (getsockopt(priv->socket, level, optname, (char*)&result, &optlen) == SOCKET_ERROR) {
        return NULL;
    }
    
    return &result;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size)
{
    if (!priv || priv->socket == INVALID_SOCKET || size <= 0) return;
    
    int bufSize = (size > INT_MAX) ? INT_MAX : (int)size;
    setsockopt(priv->socket, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize));
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv)
{
    return priv ? (intptr_t)priv->socket : -1;
}

bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, int state, int openMode)
{
    if (!priv || fd == -1) return false;
    
    priv->socket = (SOCKET)fd;
    priv->connected = (state == 3); // XAbstractSocket_ConnectedState = 3
    
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socket), priv->owner)) {
        return false;
    }
    
    if (priv->connected) {
        priv->autoRead = true;
        XNetwork_startAsyncRead((XNetworkSocketPrivate*)priv, false);
    }
    
    return true;
}

bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->connected : false;
}

void XNetwork_socketUpdatePeerInfo(XNetworkSocketPrivate* priv, const XHostAddress* addr, uint16_t port)
{
    // 此函数用于更新对端信息，在 UDP 接收时调用
    (void)priv; (void)addr; (void)port;
}

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->readBuffer : NULL;
}

size_t XNetwork_socketFinishedBytes(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->readContext.finishedBytes : 0;
}

bool XNetwork_socketIsReadPending(const XNetworkSocketPrivate* priv)
{
    return priv ? priv->readPending : false;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp)
{
    if (!priv) return;
    priv->readPending = false;
    if (priv->autoRead && priv->connected) {
        XNetwork_startAsyncRead(priv, isUdp);
    }
}

#endif /* _WIN32 */