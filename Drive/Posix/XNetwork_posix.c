/**
 * @file XNetwork_posix.c
 * @brief XNetwork POSIX 平台实现（Linux epoll）
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XNetwork_platform.h"
#include "XAbstractSocket.h"
#include "XSocketNotifier.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XFileDescriptor.h"
#include "XDateTime.h"
#include "XString.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>

/* =========================================================================
 * 私有数据结构
 * ========================================================================= */
typedef struct {
    int fd;
    int domain;
    int type;
    int protocol;
    bool isConnected;
    bool isBound;
    struct sockaddr_storage localAddr;
    socklen_t localAddrLen;
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen;
    struct sockaddr_storage lastUdpSender;
    socklen_t lastUdpSenderLen;
    char* readBuffer;
    size_t readBufferSize;
    size_t readFinished;
    size_t writeFinished;
    bool listening;
    int epoll_fd;
} XNetworkSocketPriv;

typedef struct {
    int fd;
    int epoll_fd;
    bool accepting;
    XServerHandle acceptedFd;
    struct sockaddr_storage acceptedAddr;
    socklen_t acceptedAddrLen;
} XNetworkServerPriv;

static int g_epollFd = -1;
static int g_initCount = 0;

/* =========================================================================
 * 初始化
 * ========================================================================= */
void XNetwork_ensureInit(void) {
    if (g_initCount++ == 0) {
        g_epollFd = epoll_create1(0);
    }
}

void XNetwork_cleanup(void) {
    if (--g_initCount == 0 && g_epollFd >= 0) {
        close(g_epollFd);
        g_epollFd = -1;
    }
}

int XNetwork_lastError(void) {
    return errno;
}

bool XNetwork_isEAgain(int err) {
    return (err == EAGAIN || err == EWOULDBLOCK);
}

char* XNetwork_errorString(int errorCode) {
    char* buf = (char*)XMalloc_System(256);
    if (buf) strerror_r(errorCode, buf, 256);
    return buf;
}

/* =========================================================================
 * 套接字私有数据
 * ========================================================================= */
XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner) {
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)XCalloc_System(1, sizeof(XNetworkSocketPrivate));
    if (!priv) return NULL;
    priv->owner = owner;
    priv->notifiers = XVector_create(sizeof(void*));
    return priv;
}

void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv) {
    if (!priv) return;
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (p) {
        XFree_System(p->readBuffer);
        XFree_System(p);
    }
    XVector_delete_base(priv->notifiers);
    XFree_System(priv);
}

intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    return p ? p->fd : -1;
}

XFd XNetwork_socketFd(const XNetworkSocketPrivate* priv) {
    if (!priv) return XFD_INVALID;
    return XFd_alloc(XFD_TYPE_SOCKET, priv, NULL);
}

bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    return p && p->isConnected;
}

/* =========================================================================
 * 核心操作
 * ========================================================================= */
static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static XNetworkSocketPriv* ensurePriv(XNetworkSocketPrivate* priv, XNetworkSocketType sockType) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p) {
        p = (XNetworkSocketPriv*)XCalloc_System(1, sizeof(XNetworkSocketPriv));
        if (!p) return NULL;
        p->fd = -1;
        p->readBufferSize = XNETWORK_PLATFORM_READ_BUFFER_SIZE;
        p->readBuffer = (char*)XMalloc_System(p->readBufferSize);
        XFd_setCtx(XNetwork_socketFd(priv), p);
    }
    if (p->fd < 0 && sockType == XNetwork_Tcp) {
        p->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (p->fd >= 0) setNonBlocking(p->fd);
    } else if (p->fd < 0 && sockType == XNetwork_Udp) {
        p->fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (p->fd >= 0) setNonBlocking(p->fd);
    }
    return p;
}

static struct sockaddr_in toSockAddr(const XHostAddress* address, uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = XHostAddress_toIPv4Address(address);
    return addr;
}

uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType) {
    XNetworkSocketPriv* p = ensurePriv(priv, sockType);
    if (!p || p->fd < 0) return 0;

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    struct sockaddr_in addr = toSockAddr(address, port);
    if (bind(p->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) return 0;

    if (port == 0) {
        socklen_t len = sizeof(addr);
        getsockname(p->fd, (struct sockaddr*)&addr, &len);
        port = ntohs(addr.sin_port);
    }

    p->isBound = true;
    return port;
}

bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, XNetworkProtocol protocol,
                            XNetworkSocketType sockType) {
    XNetworkSocketPriv* p = ensurePriv(priv, sockType);
    if (!p || p->fd < 0) return false;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = (sockType == XNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    if (getaddrinfo(hostName, NULL, &hints, &res) != 0) return false;

    struct sockaddr_in addr = *(struct sockaddr_in*)res->ai_addr;
    addr.sin_port = htons(port);
    int ret = connect(p->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0 || errno == EINPROGRESS) {
        p->isConnected = true;
        freeaddrinfo(res);
        return true;
    }
    freeaddrinfo(res);
    return false;
}

void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p) return;
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    p->isConnected = false;
}

static int64_t doRead(XNetworkSocketPriv* p, void* buf, int64_t len, bool isUdp) {
    if (isUdp) {
        struct sockaddr_storage from;
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(p->fd, buf, (size_t)len, MSG_DONTWAIT,
                             (struct sockaddr*)&from, &fromLen);
        if (n >= 0) {
            memcpy(&p->lastUdpSender, &from, sizeof(from));
            p->lastUdpSenderLen = fromLen;
            return (int64_t)n;
        }
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    } else {
        ssize_t n = recv(p->fd, buf, (size_t)len, MSG_DONTWAIT);
        if (n >= 0) return (int64_t)n;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
}

int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p || p->fd < 0) return -1;
    return doRead(p, buf, len, sockType == XNetwork_Udp);
}

int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr,
                             uint16_t destPort, void* ringBuffer) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p || p->fd < 0) return -1;

    if (sockType == XNetwork_Udp && destAddr) {
        struct sockaddr_in addr = toSockAddr(destAddr, destPort);
        ssize_t n = sendto(p->fd, buf, (size_t)len, MSG_DONTWAIT,
                           (struct sockaddr*)&addr, sizeof(addr));
        if (n >= 0) return (int64_t)n;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    } else {
        ssize_t n = send(p->fd, buf, (size_t)len, MSG_DONTWAIT);
        if (n >= 0) return (int64_t)n;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
}

bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event) {
    (void)priv; (void)event;
    return true;
}

bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd,
                                  int state, int openMode) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p) {
        p = (XNetworkSocketPriv*)XCalloc_System(1, sizeof(XNetworkSocketPriv));
        if (!p) return false;
        p->readBufferSize = XNETWORK_PLATFORM_READ_BUFFER_SIZE;
        p->readBuffer = (char*)XMalloc_System(p->readBufferSize);
        XFd_setCtx(XNetwork_socketFd(priv), p);
    }
    p->fd = (int)fd;
    setNonBlocking(p->fd);
    p->isConnected = (state == 3); /* ConnectedState */
    return true;
}

bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p || p->fd < 0) return false;
    return setsockopt(p->fd, SOL_SOCKET, option, value, sizeof(int)) == 0;
}

void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p || p->fd < 0) return NULL;
    static int val;
    socklen_t len = sizeof(val);
    if (getsockopt(p->fd, SOL_SOCKET, option, &val, &len) == 0)
        return &val;
    return NULL;
}

void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p) return;
    p->readBufferSize = (size_t)size;
    if (p->readBuffer) XFree_System(p->readBuffer);
    p->readBuffer = (char*)XMalloc_System(p->readBufferSize);
}

const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    return p ? p->readBuffer : NULL;
}

size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    return p ? p->readFinished : 0;
}

size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    return p ? p->writeFinished : 0;
}

void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p || p->fd < 0) return;
    ssize_t n = doRead(p, p->readBuffer, p->readBufferSize, isUdp);
    if (n > 0) p->readFinished = (size_t)n;
}

/* =========================================================================
 * TCP 服务器
 * ========================================================================= */
XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setNonBlocking(fd);

    if (reuseAddr) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    struct sockaddr_in sin = toSockAddr(addr, port);
    if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(fd); return -1;
    }
    if (listen(fd, backlog) != 0) {
        close(fd); return -1;
    }

    XNetworkServerPriv* sp = (XNetworkServerPriv*)XCalloc_System(1, sizeof(XNetworkServerPriv));
    if (!sp) { close(fd); return -1; }
    sp->fd = fd;
    sp->accepting = false;
    sp->acceptedFd = -1;
    return (XServerHandle)(intptr_t)sp;
}

void XNetwork_serverAccept(XNetworkSocketPrivate* priv, XServerHandle server) {
    XNetworkServerPriv* sp = (XNetworkServerPriv*)(intptr_t)server;
    if (!sp || sp->accepting) return;
    sp->acceptedAddrLen = sizeof(sp->acceptedAddr);
    sp->acceptedFd = accept(sp->fd, (struct sockaddr*)&sp->acceptedAddr, &sp->acceptedAddrLen);
    sp->accepting = true;
}

void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server) {
    XNetworkServerPriv* sp = (XNetworkServerPriv*)(intptr_t)server;
    if (!sp) return;
    close(sp->fd);
    XFree_System(sp);
}

uint16_t XNetwork_serverPort(XServerHandle server) {
    XNetworkServerPriv* sp = (XNetworkServerPriv*)(intptr_t)server;
    if (!sp) return 0;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sp->fd, (struct sockaddr*)&sin, &len) != 0) return 0;
    return ntohs(sin.sin_port);
}

XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv, XHostAddress* clientAddr, uint16_t* clientPort) {
    XNetworkServerPriv* sp = (XNetworkServerPriv*)(intptr_t)XFd_ctx(XNetwork_socketFd(priv));
    (void)sp;
    return -1;
}

bool XNetwork_serverContinueAccept(XNetworkSocketPrivate* priv) {
    return true;
}

/* =========================================================================
 * Socket 创建
 * ========================================================================= */
XSocketHandle XNetwork_createTcpSocket(XNetworkProtocol protocol) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) setNonBlocking(fd);
    return (XSocketHandle)fd;
}

XSocketHandle XNetwork_createUdpSocket(XNetworkProtocol protocol) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) setNonBlocking(fd);
    return (XSocketHandle)fd;
}

void XNetwork_closeSocket(XSocketHandle sock) {
    if (sock >= 0) close((int)sock);
}

/* =========================================================================
 * 网络接口
 * ========================================================================= */
XVector* XNetwork_getInterfaceAddresses(const XString* ifname) {
    (void)ifname;
    return NULL;
}

bool XNetwork_enumInterfacesBegin(void) { return false; }
bool XNetwork_enumInterfacesNext(void* iter) { (void)iter; return false; }
void XNetwork_enumInterfacesEnd(void* iter) { (void)iter; }

/* =========================================================================
 * 多播
 * ========================================================================= */
bool XNetwork_multicastGroup(XSocketHandle sock, bool join,
                             const XHostAddress* groupAddress, uint32_t ifIndex) {
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = XHostAddress_toIPv4Address(groupAddress);
    mreq.imr_interface.s_addr = INADDR_ANY;
    return setsockopt((int)sock, IPPROTO_IP,
                      join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                      &mreq, sizeof(mreq)) == 0;
}

int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg) {
    (void)sock; (void)op; (void)arg;
    return -1;
}

/* =========================================================================
 * UDP 特有
 * ========================================================================= */
bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv,
                                     XHostAddress* srcAddr, uint16_t* srcPort) {
    XNetworkSocketPriv* p = (XNetworkSocketPriv*)XFd_ctx(XNetwork_socketFd(priv));
    if (!p) return false;
    struct sockaddr_in* sin = (struct sockaddr_in*)&p->lastUdpSender;
    if (srcAddr) XHostAddress_setAddressIPv4(srcAddr, sin->sin_addr.s_addr);
    if (srcPort) *srcPort = ntohs(sin->sin_port);
    return true;
}

int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port) {
    struct sockaddr_in addr = toSockAddr(address, port);
    ssize_t n = sendto((int)sock, data, (size_t)size, 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    return (n >= 0) ? (int64_t)n : -1;
}

/* =========================================================================
 * 系统代理与GSSAPI
 * ========================================================================= */
bool XNetwork_getSystemProxy(const char* queryUrl, XNetworkProxy* outProxy) {
    (void)queryUrl;
    XNetworkProxy_setType(outProxy, XNetworkProxy_NoProxy);
    return false;
}

int XNetwork_gssapiAuth(const char* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context) {
    (void)serviceName; (void)inputToken; (void)outputToken; (void)context;
    return -1;
}

/* =========================================================================
 * DNS 查找
 * ========================================================================= */
bool XNetwork_lookupName(const char* hostName, XHostAddress* outAddr) {
    if (!hostName || !outAddr) return false;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostName, NULL, &hints, &res) != 0) return false;
    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    XHostAddress_setAddressIPv4(outAddr, sin->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

bool XNetwork_localHostName(XString* outName) {
    if (!outName) return false;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return false;
    XString_assign_utf8(outName, hostname);
    return true;
}

#endif /* POSIX */
