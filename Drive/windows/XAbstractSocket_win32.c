#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "IOCPInfo.h"
#include "XAbstractSocket.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XThread.h"
#include "XIODevicePrivate.h"
#include "XHostAddress.h"
#include "XDateTime.h"
#include "XNetworkProxy.h"
#include <string.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

// 定义ConnectEx函数指针类型
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

// 外部IOCP绑定函数
extern bool IOCP_bind(XSocketDescriptor socket, XObject* obj);
static bool connect_via_proxy(XAbstractSocket* self, const char* targetHost, uint16_t targetPort);
// ======================== 私有数据结构 ========================
// XAbstractSocketPrivate在此定义，存放Windows平台特定数据

#define XSOCKET_READ_BUFFER_SIZE  8192
#define XSOCKET_WRITE_BUFFER_SIZE 8192

typedef struct XAbstractSocketPrivate {
    SOCKET socketHandle;                ///< Windows SOCKET句柄
    XEventContext_IOCP readContext;      ///< 读操作IOCP上下文
    XEventContext_IOCP writeContext;     ///< 写操作IOCP上下文
    XEventContext_IOCP connectContext;   ///< 连接操作IOCP上下文（TCP用）
    char readBuffer[XSOCKET_READ_BUFFER_SIZE];   ///< 读缓冲区
    char writeBuffer[XSOCKET_WRITE_BUFFER_SIZE]; ///< 写缓冲区
    bool readPending;                   ///< 读操作是否挂起
    bool writePending;                  ///< 写操作是否挂起
    bool connectPending;                ///< 连接操作是否挂起（TCP用）
    bool connected;                     ///< 是否已连接
    bool autoRead;                      ///< 是否自动读取
    // UDP 来源地址存储
    struct sockaddr_in6 fromAddr;       ///< UDP 数据来源地址
    int fromAddrLen;                    ///< 来源地址长度
} XAbstractSocketPrivate;

// Winsock初始化
static bool g_winsockInitialized = false;
static LPFN_CONNECTEX g_ConnectEx = NULL;  // ConnectEx函数指针

static void ensureWinsockInit(void) {
    if (!g_winsockInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            g_winsockInitialized = true;
            
            // 获取ConnectEx函数指针
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
}

// Winsock错误转换
static XAbstractSocket_SocketError winsockErrorToSocketError(int error) {
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

// 非阻塞设置
static bool setNonBlocking(SOCKET sock, bool nonBlocking) {
    u_long mode = nonBlocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) != SOCKET_ERROR;
}

// 获取私有数据
static inline XAbstractSocketPrivate* getPriv(XAbstractSocket* sock) {
    return sock ? (XAbstractSocketPrivate*)sock->d_ptr : NULL;
}
// 创建私有数据
XAbstractSocketPrivate* XAbstractSocketPrivate_create(void) {
    XAbstractSocketPrivate* priv = XNew(XAbstractSocketPrivate);
    if (priv) {
        memset(priv, 0, sizeof(XAbstractSocketPrivate));
        priv->socketHandle = INVALID_SOCKET;
        priv->autoRead = true;
    }
    return priv;
}

// 删除私有数据
void XAbstractSocketPrivate_delete(XAbstractSocketPrivate* priv) {
    if (!priv) return;
    
    // 关闭套接字
    if (priv->socketHandle != INVALID_SOCKET) {
        CancelIo((HANDLE)priv->socketHandle);
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
    }
    
    XFree_System(priv);
}
// ======================== 异步IO操作 ========================

// 启动异步读取
static void startAsyncRead(XAbstractSocket* sock) {
    XAbstractSocketPrivate* priv = getPriv(sock);
    if (!priv || priv->readPending || priv->socketHandle == INVALID_SOCKET) return;
    
    memset(&priv->readContext, 0, sizeof(XEventContext_IOCP));
    priv->readContext.type = XEventContextType_Type_Socket;
    priv->readContext.socket = XSocketDescriptor_fromIntptr(priv->socketHandle);
    priv->readContext.eventMask = FD_READ;
    priv->readContext.buffer = priv->readBuffer;
    priv->readContext.bufferSize = sizeof(priv->readBuffer);
    
    DWORD flags = 0;
    WSABUF buf;
    buf.buf = priv->readBuffer;
    buf.len = sizeof(priv->readBuffer);
    
        int result;
    // UDP使用WSARecvFrom，TCP使用WSARecv
    if (sock->socketType == XAbstractSocket_UdpSocket) {
        // UDP需要存储来源地址（使用私有数据中的成员）
        priv->fromAddrLen = sizeof(priv->fromAddr);
        result = WSARecvFrom(priv->socketHandle, &buf, 1, NULL, &flags,
                            (struct sockaddr*)&priv->fromAddr, &priv->fromAddrLen,
                            (OVERLAPPED*)&priv->readContext, NULL);
    } else {
        result = WSARecv(priv->socketHandle, &buf, 1, NULL, &flags, 
                        (OVERLAPPED*)&priv->readContext, NULL);
    }
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        XAbstractSocket_setSocketError(sock, winsockErrorToSocketError(WSAGetLastError()), NULL);
        return;
    }
    priv->readPending = true;
}

// 读取完成处理
static void handleReadCompletion(XAbstractSocket* sock, DWORD bytesTransferred) {
    XAbstractSocketPrivate* priv = getPriv(sock);
    if (!priv) return;
    
    priv->readPending = false;
    
    if (bytesTransferred == 0) {
        // TCP连接关闭
        if (sock->socketType == XAbstractSocket_TcpSocket) {
            priv->connected = false;
            XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
        }
        return;
    }
    
        // UDP接收时更新来源地址信息
    if (sock->socketType == XAbstractSocket_UdpSocket && priv->fromAddrLen > 0) {
        if (priv->fromAddr.sin6_family == AF_INET) {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)&priv->fromAddr;
            XHostAddress_setAddressIPv4(&sock->peerAddress, ntohl(addr4->sin_addr.s_addr));
            sock->peerPort = ntohs(addr4->sin_port);
        } else if (priv->fromAddr.sin6_family == AF_INET6) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&priv->fromAddr;
            XHostAddress_setAddressIPv6(&sock->peerAddress, addr6->sin6_addr.s6_addr);
            sock->peerPort = ntohs(addr6->sin6_port);
        }
    }
    
    // 将数据写入内部缓冲区
    XIODevicePrivate* d = ((XIODevice*)sock)->m_d;
    int currentReadChannel = XIODevice_currentReadChannel((XIODevice*)sock);
    struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
    
    if (readBuf) {
        XRingBuffer_write(readBuf, priv->readBuffer, bytesTransferred);
        XIODevice_readyRead_signal((XIODevice*)sock);
    }
    
    // 继续读取
    if (priv->autoRead && priv->connected) {
        startAsyncRead(sock);
    }
}

// 写入完成处理
static void handleWriteCompletion(XAbstractSocket* sock, DWORD bytesTransferred) {
    XAbstractSocketPrivate* priv = getPriv(sock);
    if (!priv) return;
    
    priv->writePending = false;
    
    if (bytesTransferred > 0) {
        XIODevice_bytesWritten_signal((XIODevice*)sock, bytesTransferred);
    }
}

// 连接完成处理（TCP）
static void handleConnectCompletion(XAbstractSocket* sock) {
      XAbstractSocketPrivate* priv = getPriv(sock);
      if (!priv) return;
      
      priv->connectPending = false;
      
      // 检查连接是否成功
      int error = 0;
      int errorLen = sizeof(error);
      if (getsockopt(priv->socketHandle, SOL_SOCKET, SO_ERROR, (char*)&error, &errorLen) == 0) {
          if (error == 0) {
              // 连接成功，检查是否需要代理握手
              XNetworkProxy* proxy = XAbstractSocket_proxy(sock);
              bool useProxy = proxy && proxy->type != XNetworkProxy_NoProxy && proxy->type != XNetworkProxy_DefaultProxy;
              
              if (useProxy) {
                  // 获取目标主机名和端口
                  const char* targetHost = XAbstractSocket_peerName(sock);
                  uint16_t targetPort = XAbstractSocket_peerPort(sock);
                  
                  if (!targetHost || targetPort == 0) {
                      XAbstractSocket_setSocketError(sock, XAbstractSocket_ProxyProtocolError, "Missing target host/port for proxy");
                      closesocket(priv->socketHandle);
                      priv->socketHandle = INVALID_SOCKET;
                      XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
                      return;
                  }
                  
                  // 进行代理握手
                  if (!connect_via_proxy(sock, targetHost, targetPort)) {
                      closesocket(priv->socketHandle);
                      priv->socketHandle = INVALID_SOCKET;
                      XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
                      return;
                  }
              }
              
              priv->connected = true;
                            XAbstractSocket_setSocketState(sock, XAbstractSocket_ConnectedState);
                            // 设置打开模式
                            ((XIODevice*)sock)->m_openMode = XIODevice_ReadWrite;
                            priv->autoRead = true;
                            startAsyncRead(sock);
          } else {
              // 连接失败
              XAbstractSocket_setSocketError(sock, winsockErrorToSocketError(error), "Connection failed");
              closesocket(priv->socketHandle);
              priv->socketHandle = INVALID_SOCKET;
              XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
          }
      }
}

// ======================== 必须重载的虚函数 ========================


int64_t XAbstractSocket_platform_readData(XAbstractSocket* self, char* data, int64_t maxlen) {
    if (!self || !data || maxlen <= 0) return -1;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return -1;
    
    // 从内部缓冲区读取
    XIODevicePrivate* d = ((XIODevice*)self)->m_d;
    int currentReadChannel = XIODevice_currentReadChannel((XIODevice*)self);
    struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
    
    if (readBuf && XRingBuffer_available(readBuf) > 0) {
        return XRingBuffer_read(readBuf, data, maxlen);
    }
    
    return 0; // 暂时没有数据
}

int64_t XAbstractSocket_platform_writeData(XAbstractSocket* self, const char* data, int64_t len) {
    if (!self || !data || len <= 0) return -1;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return -1;
    
    // 如果有挂起的写操作，先写入缓冲区
    if (priv->writePending) {
        XIODevicePrivate* d = ((XIODevice*)self)->m_d;
        int currentWriteChannel = XIODevice_currentWriteChannel((XIODevice*)self);
        struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
        if (writeBuf) {
            return XRingBuffer_write(writeBuf, data, len);
        }
        return -1;
    }
    
    int sent;
    // UDP使用sendto发送到对端地址
    if (self->socketType == XAbstractSocket_UdpSocket) {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
        struct sockaddr* addr = NULL;
        int addrLen = 0;
        
        if (XHostAddress_protocol(&self->peerAddress) == XHostAddress_IPv6Protocol) {
            memset(&addr6, 0, sizeof(addr6));
            addr6.sin6_family = AF_INET6;
            addr6.sin6_port = htons(self->peerPort);
            uint8_t ipv6[16];
            XHostAddress_toIPv6Address(&self->peerAddress, ipv6);
            memcpy(&addr6.sin6_addr, ipv6, 16);
            addr = (struct sockaddr*)&addr6;
            addrLen = sizeof(addr6);
        } else {
            memset(&addr4, 0, sizeof(addr4));
            addr4.sin_family = AF_INET;
            addr4.sin_port = htons(self->peerPort);
            uint32_t ipv4 = XHostAddress_toIPv4Address(&self->peerAddress);
            memcpy(&addr4.sin_addr, &ipv4, 4);
            addr = (struct sockaddr*)&addr4;
            addrLen = sizeof(addr4);
        }
        sent = sendto(priv->socketHandle, data, (int)len, 0, addr, addrLen);
    } else {
        // TCP使用send
        sent = send(priv->socketHandle, data, (int)len, 0);
    }
    if (sent == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            // 非阻塞模式下暂时无法发送，写入缓冲区
            XIODevicePrivate* d = ((XIODevice*)self)->m_d;
            int currentWriteChannel = XIODevice_currentWriteChannel((XIODevice*)self);
            struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
            if (writeBuf) {
                return XRingBuffer_write(writeBuf, data, len);
            }
        }
        XAbstractSocket_setSocketError(self, winsockErrorToSocketError(error), NULL);
        return -1;
    }
    
    XIODevice_bytesWritten_signal((XIODevice*)self, sent);
    return sent;
}








// event 虚函数重写，处理 IOCP 完成事件
bool XAbstractSocket_platform_event(XAbstractSocket* self, XEvent* e) {
    if (!self || !e) return false;
    
    // 处理 IOCP 完成事件
    if (e->type == XEVENT_TYPE_SOCK_ACT) {
        XEventSockAct* sockAct = (XEventSockAct*)e;
        XAbstractSocketPrivate* priv = getPriv(self);
        if (priv) {
            // 根据事件类型调用对应的处理函数
            // finishedBytes 存储在 XEventContext_IOCP 中
            if (sockAct->actType & XSocketAct_Read) {
                handleReadCompletion(self, (DWORD)priv->readContext.finishedBytes);
            }
            if (sockAct->actType & XSocketAct_Write) {
                handleWriteCompletion(self, (DWORD)priv->writeContext.finishedBytes);
            }
            // 连接完成：通过检查 connectPending 状态判断
            // ConnectEx 完成后会触发事件，此时 connectPending 为 true
            if (priv->connectPending) {
                handleConnectCompletion(self);
            }
        }
        XEvent_setAccepted_base(e, true);
        return true;
    }
    
    // 其他事件调用父类处理
    return XClass_Parent(XObject, EXObject_Event, bool (*)(XObject*, XEvent*))((XObject*)self, e);
}

// ======================== XAbstractSocket特有虚函数 ========================
bool XAbstractSocket_platform_bind(XAbstractSocket* self, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode) {
    if (!self || !address) return false;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv) return false;
    
    // 检查是否支持该套接字类型
    if (self->socketType == XAbstractSocket_SctpSocket) {
        XAbstractSocket_setSocketError(self, XAbstractSocket_UnsupportedSocketOperationError, 
                                        "SCTP is not supported on Windows");
        return false;
    }
    
    ensureWinsockInit();
    
    // 创建套接字
    int family = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    priv->socketHandle = socket(family, (self->socketType == XAbstractSocket_UdpSocket) ? SOCK_DGRAM : SOCK_STREAM, IPPROTO_IP);
    if (priv->socketHandle == INVALID_SOCKET) {
        XAbstractSocket_setSocketError(self, XAbstractSocket_SocketResourceError, "Failed to create socket");
        return false;
    }
    
    setNonBlocking(priv->socketHandle, true);
    
    int reuse = 1;
    setsockopt(priv->socketHandle, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr* addr;
    int addrLen;
    
        if (family == AF_INET) {
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port);
        uint32_t ipv4 = XHostAddress_toIPv4Address(address);
        memcpy(&addr4.sin_addr, &ipv4, 4);
        addr = (struct sockaddr*)&addr4;
        addrLen = sizeof(addr4);
    } else {
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        uint8_t ipv6[16];
        XHostAddress_toIPv6Address(address, ipv6);
        memcpy(&addr6.sin6_addr, ipv6, 16);
        addr = (struct sockaddr*)&addr6;
        addrLen = sizeof(addr6);
    }
    
    if (bind(priv->socketHandle, addr, addrLen) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        XAbstractSocket_setSocketError(self, winsockErrorToSocketError(error), NULL);
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
        return false;
    }
    
    XAbstractSocket_setLocalAddress(self, address);
    XAbstractSocket_setLocalPort(self, port);
    
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socketHandle), self)) {
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
        return false;
    }
    
    XAbstractSocket_setSocketState(self, XAbstractSocket_BoundState);
    return true;
  }

// ======================== 代理支持函数 ========================

// SOCKS5 代理握手
static bool socks5_handshake(XAbstractSocket* self, const char* targetHost, uint16_t targetPort) {
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return false;
    
    XNetworkProxy* proxy = XAbstractSocket_proxy(self);
    if (!proxy || proxy->type != XNetworkProxy_Socks5Proxy) return false;
    
    // SOCKS5 版本协商请求
    uint8_t authMethods = 0x02; // 支持无认证和用户名/密码认证
    uint8_t request[3] = { 0x05, 0x02, 0x00 }; // 版本5, 2种方法, 无认证
    if (proxy->user && proxy->user[0]) {
      request[1] = 0x02;
      request[2] = 0x02; // 用户名/密码认证
    } else {
      request[1] = 0x01; // 只有1种方法
    }
    
    // 发送版本协商请求
    int sent = send(priv->socketHandle, (char*)request, 2 + request[1], 0);
    if (sent == SOCKET_ERROR) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "SOCKS5 handshake send failed");
      return false;
    }
    
    // 接收服务器响应
    uint8_t response[2] = {0};
    int received = recv(priv->socketHandle, (char*)response, 2, 0);
    if (received != 2 || response[0] != 0x05) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "Invalid SOCKS5 response");
      return false;
    }
    
    // 检查认证方法
    if (response[1] == 0x02) {
      // 需要用户名/密码认证 (RFC 1929)
      if (!proxy->user || !proxy->user[0]) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyAuthenticationRequiredError, "SOCKS5 proxy requires authentication");
          return false;
      }
        
      size_t userLen = strlen(proxy->user);
      size_t passLen = proxy->password ? strlen(proxy->password) : 0;
      if (userLen > 255 || passLen > 255) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError, "SOCKS5 auth too long");
          return false;
      }
        
      uint8_t authRequest[513];
      authRequest[0] = 0x01; // 子协商版本
      authRequest[1] = (uint8_t)userLen;
      memcpy(authRequest + 2, proxy->user, userLen);
      authRequest[2 + userLen] = (uint8_t)passLen;
      if (passLen > 0) {
          memcpy(authRequest + 3 + userLen, proxy->password, passLen);
      }
        
      sent = send(priv->socketHandle, (char*)authRequest, 3 + userLen + passLen, 0);
      if (sent == SOCKET_ERROR) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyAuthenticationRequiredError, "SOCKS5 auth send failed");
          return false;
      }
        
      // 接收认证响应
      uint8_t authResponse[2] = {0};
      received = recv(priv->socketHandle, (char*)authResponse, 2, 0);
      if (received != 2 || authResponse[1] != 0x00) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyAuthenticationRequiredError, "SOCKS5 authentication failed");
          return false;
      }
    } else if (response[1] != 0x00) {
      // 不支持的认证方法
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyAuthenticationRequiredError, "SOCKS5 unsupported auth method");
      return false;
    }
    
    // 发送连接请求
    uint8_t connectRequest[262]; // 最大大小
    connectRequest[0] = 0x05; // 版本
    connectRequest[1] = 0x01; // CONNECT 命令
    connectRequest[2] = 0x00; // 保留
    
    size_t hostLen = strlen(targetHost);
    if (hostLen > 255) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError, "Hostname too long for SOCKS5");
      return false;
    }
    
    // 使用域名 (ATYP = 0x03)
    connectRequest[3] = 0x03;
    connectRequest[4] = (uint8_t)hostLen;
    memcpy(connectRequest + 5, targetHost, hostLen);
    uint16_t netPort = htons(targetPort);
    memcpy(connectRequest + 5 + hostLen, &netPort, 2);
    
    size_t requestLen = 5 + hostLen + 2;
    sent = send(priv->socketHandle, (char*)connectRequest, (int)requestLen, 0);
    if (sent == SOCKET_ERROR) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "SOCKS5 connect request failed");
      return false;
    }
    
    // 接收连接响应
    uint8_t connectResponse[10]; // 最小响应大小
    received = recv(priv->socketHandle, (char*)connectResponse, 4, 0);
    if (received != 4 || connectResponse[0] != 0x05) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "Invalid SOCKS5 connect response");
      return false;
    }
    
    if (connectResponse[1] != 0x00) {
      // 连接失败，根据错误码设置错误
      const char* errorMsg = "SOCKS5 connection failed";
      switch (connectResponse[1]) {
          case 0x01: errorMsg = "SOCKS5 general failure"; break;
          case 0x02: errorMsg = "SOCKS5 connection not allowed"; break;
          case 0x03: errorMsg = "SOCKS5 network unreachable"; break;
          case 0x04: errorMsg = "SOCKS5 host unreachable"; break;
          case 0x05: errorMsg = "SOCKS5 connection refused"; break;
          case 0x06: errorMsg = "SOCKS5 TTL expired"; break;
          case 0x07: errorMsg = "SOCKS5 command not supported"; break;
          case 0x08: errorMsg = "SOCKS5 address type not supported"; break;
      }
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyConnectionRefusedError, errorMsg);
      return false;
    }
    
    // 读取剩余的绑定地址信息
    uint8_t addrType = connectResponse[3];
    int remaining = 0;
    if (addrType == 0x01) remaining = 6;      // IPv4 + port
    else if (addrType == 0x04) remaining = 18; // IPv6 + port
    else if (addrType == 0x03) {
      // 域名，先读取长度
      uint8_t domainLen = 0;
      received = recv(priv->socketHandle, (char*)&domainLen, 1, 0);
      if (received != 1) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "SOCKS5 response incomplete");
          return false;
      }
      remaining = domainLen + 2; // 域名 + port
    }
    
    if (remaining > 0) {
      char discard[256];
      while (remaining > 0) {
          int toRead = remaining > 256 ? 256 : remaining;
          received = recv(priv->socketHandle, discard, toRead, 0);
          if (received <= 0) break;
          remaining -= received;
      }
    }
    
    return true;
}

// HTTP 代理 CONNECT
static bool http_proxy_connect(XAbstractSocket* self, const char* targetHost, uint16_t targetPort) {
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return false;
    
    XNetworkProxy* proxy = XAbstractSocket_proxy(self);
    if (!proxy || proxy->type != XNetworkProxy_HttpProxy) return false;
    
    // 构建 HTTP CONNECT 请求
    char request[1024];
    int requestLen = snprintf(request, sizeof(request),
      "CONNECT %s:%u HTTP/1.1\r\n"
      "Host: %s:%u\r\n",
      targetHost, targetPort, targetHost, targetPort);
    
    // 添加认证头（如果需要）
    if (proxy->user && proxy->user[0]) {
      // Base64 编码 user:password
      char credentials[512];
      snprintf(credentials, sizeof(credentials), "%s:%s", 
               proxy->user, proxy->password ? proxy->password : "");
        
      // 简单的 Base64 编码
      static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      size_t credLen = strlen(credentials);
      size_t encodedLen = ((credLen + 2) / 3) * 4;
      char* encoded = (char*)XMalloc_System(encodedLen + 1);
      if (!encoded) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_SocketResourceError, "Memory allocation failed");
          return false;
      }
        
      size_t i, j;
      for (i = 0, j = 0; i < credLen; ) {
          uint32_t octet_a = i < credLen ? (unsigned char)credentials[i++] : 0;
          uint32_t octet_b = i < credLen ? (unsigned char)credentials[i++] : 0;
          uint32_t octet_c = i < credLen ? (unsigned char)credentials[i++] : 0;
          uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
          encoded[j++] = base64_chars[(triple >> 18) & 0x3F];
          encoded[j++] = base64_chars[(triple >> 12) & 0x3F];
          encoded[j++] = base64_chars[(triple >> 6) & 0x3F];
          encoded[j++] = base64_chars[triple & 0x3F];
      }
        
      // 添加填充
      if (credLen % 3 >= 1) encoded[encodedLen - 1] = '=';
      if (credLen % 3 == 1) encoded[encodedLen - 2] = '=';
      encoded[encodedLen] = '\0';
        
      requestLen += snprintf(request + requestLen, sizeof(request) - requestLen,
          "Proxy-Authorization: Basic %s\r\n", encoded);
      XFree_System(encoded);
    }
    
    requestLen += snprintf(request + requestLen, sizeof(request) - requestLen, "\r\n");
    
    // 发送请求
    int sent = send(priv->socketHandle, request, requestLen, 0);
    if (sent == SOCKET_ERROR) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "HTTP proxy request failed");
      return false;
    }
    
    // 接收响应
    char response[1024] = {0};
    int totalReceived = 0;
    while (totalReceived < (int)sizeof(response) - 1) {
      int received = recv(priv->socketHandle, response + totalReceived, 1, 0);
      if (received <= 0) {
          XAbstractSocket_setSocketError(self, XAbstractSocket_NetworkError, "HTTP proxy response incomplete");
          return false;
      }
      totalReceived += received;
      response[totalReceived] = '\0';
        
      // 检查是否收到完整的响应头
      if (strstr(response, "\r\n\r\n")) {
          break;
      }
    }
    
    // 解析响应状态码
    int statusCode = 0;
    if (sscanf(response, "HTTP/%*d.%*d %d", &statusCode) != 1) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError, "Invalid HTTP proxy response");
      return false;
    }
    
    if (statusCode == 407) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyAuthenticationRequiredError, "HTTP proxy authentication required");
      return false;
    }
    
    if (statusCode < 200 || statusCode >= 300) {
      XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyConnectionRefusedError, "HTTP proxy connection failed");
      return false;
    }
    
    return true;
}

// 通过代理连接
static bool connect_via_proxy(XAbstractSocket* self, const char* targetHost, uint16_t targetPort) {
    XNetworkProxy* proxy = XAbstractSocket_proxy(self);
    if (!proxy) return false;
    
    switch (proxy->type) {
      case XNetworkProxy_Socks5Proxy:
          return socks5_handshake(self, targetHost, targetPort);
      case XNetworkProxy_HttpProxy:
          return http_proxy_connect(self, targetHost, targetPort);
      case XNetworkProxy_NoProxy:
          return true; // 直连，不需要代理握手
      default:
          XAbstractSocket_setSocketError(self, XAbstractSocket_UnsupportedSocketOperationError, "Unsupported proxy type");
          return false;
    }
}

void XAbstractSocket_platform_connectToHost(XAbstractSocket* self, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol) {
    if (!self || !hostName) return;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv) return;
    
    // 检查是否支持该套接字类型
    if (self->socketType == XAbstractSocket_SctpSocket) {
        XAbstractSocket_setSocketError(self, XAbstractSocket_UnsupportedSocketOperationError,
                                        "SCTP is not supported on Windows");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }
    
    (void)mode;
    ensureWinsockInit();

    // UDP不支持connect模式，应该用bind
    if (self->socketType == XAbstractSocket_UdpSocket) {
        // 先解析主机名获取对端地址
        struct addrinfo hints = { 0 }, * result = NULL;
        hints.ai_family = (protocol == XAbstractSocket_IPv4Protocol) ? AF_INET :
            (protocol == XAbstractSocket_IPv6Protocol) ? AF_INET6 : AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%u", port);

        int res = getaddrinfo(hostName, portStr, &hints, &result);
        if (res != 0 || !result) {
            XAbstractSocket_setSocketError(self, XAbstractSocket_HostNotFoundError, gai_strerror(res));
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        // 从解析结果设置peerAddress
        if (result->ai_family == AF_INET) {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)result->ai_addr;
            XHostAddress_setAddressIPv4(&self->peerAddress, ntohl(addr4->sin_addr.s_addr));
        }
        else {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)result->ai_addr;
            XHostAddress_setAddressIPv6(&self->peerAddress, addr6->sin6_addr.s6_addr);
        }
        freeaddrinfo(result);

        // 绑定本地地址
        XHostAddress any;
        XHostAddress_setAddressSpecial(&any, XHostAddress_AnySpecial);
        if (!XAbstractSocket_platform_bind(self, &any, 0, XAbstractSocket_DefaultForPlatform)) {
            return;
        }

        // 设置对端信息
        XAbstractSocket_setPeerName(self, hostName);
        XAbstractSocket_setPeerPort(self, port);

        // UDP绑定后即视为"已连接"
                  XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
                  // 设置打开模式
                  ((XIODevice*)self)->m_openMode = XIODevice_ReadWrite;
                  priv->connected = true;
        priv->autoRead = true;
        startAsyncRead(self);
        return;
    }

    // TCP连接流程
          // 先设置目标信息（代理握手需要）
          XAbstractSocket_setPeerName(self, hostName);
          XAbstractSocket_setPeerPort(self, port);
      
          XNetworkProxy* proxy = XAbstractSocket_proxy(self);
          bool useProxy = proxy && proxy->type != XNetworkProxy_NoProxy && proxy->type != XNetworkProxy_DefaultProxy;
      
          struct addrinfo hints = { 0 }, * result = NULL;
          hints.ai_family = (protocol == XAbstractSocket_IPv4Protocol) ? AF_INET :
              (protocol == XAbstractSocket_IPv6Protocol) ? AF_INET6 : AF_UNSPEC;
          hints.ai_socktype = SOCK_STREAM;
          hints.ai_protocol = IPPROTO_TCP;
  
          XAbstractSocket_setSocketState(self, XAbstractSocket_HostLookupState);
  
          char portStr[16];
          const char* connectHost = hostName;
          uint16_t connectPort = port;
      
          if (useProxy) {
              // 使用代理时，连接到代理服务器
              if (!proxy->hostName || !proxy->hostName[0]) {
                  XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyConnectionRefusedError, "Proxy host not specified");
                  XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                  return;
              }
              connectHost = proxy->hostName;
              connectPort = proxy->port;
              // 代理服务器地址解析使用系统默认协议
              hints.ai_family = AF_UNSPEC;
          }
      
          snprintf(portStr, sizeof(portStr), "%u", connectPort);
  
          int res = getaddrinfo(connectHost, portStr, &hints, &result);
          if (res != 0 || !result) {
              XAbstractSocket_setSocketError(self, XAbstractSocket_HostNotFoundError, gai_strerror(res));
              XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
              return;
          }

    priv->socketHandle = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (priv->socketHandle == INVALID_SOCKET) {
        freeaddrinfo(result);
        XAbstractSocket_setSocketError(self, XAbstractSocket_SocketResourceError, "Failed to create socket");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    // 先绑定到IOCP（必须在ConnectEx之前）
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socketHandle), self)) {
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
        freeaddrinfo(result);
        XAbstractSocket_setSocketError(self, XAbstractSocket_SocketResourceError, "Failed to bind to IOCP");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    // ConnectEx要求先绑定本地地址
    struct sockaddr_storage localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.ss_family = result->ai_family;
    if (bind(priv->socketHandle, (struct sockaddr*)&localAddr,
        result->ai_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
        freeaddrinfo(result);
        XAbstractSocket_setSocketError(self, XAbstractSocket_SocketResourceError, "Failed to bind local address");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    priv->connected = false;

    // 使用ConnectEx进行异步连接
    if (g_ConnectEx) {
        memset(&priv->connectContext, 0, sizeof(XEventContext_IOCP));
        priv->connectContext.type = XEventContextType_Type_Socket;
        priv->connectContext.socket = XSocketDescriptor_fromIntptr(priv->socketHandle);
        priv->connectContext.eventMask = FD_CONNECT;

        DWORD bytesSent = 0;
        BOOL connectResult = g_ConnectEx(priv->socketHandle, result->ai_addr, (int)result->ai_addrlen,
            NULL, 0, &bytesSent, (OVERLAPPED*)&priv->connectContext);
        freeaddrinfo(result);

        if (!connectResult) {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                XAbstractSocket_setSocketError(self, winsockErrorToSocketError(error), "ConnectEx failed");
                closesocket(priv->socketHandle);
                priv->socketHandle = INVALID_SOCKET;
                XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                return;
            }
        }
        priv->connectPending = true;
    }
    else {
        // 回退到普通connect（如果没有ConnectEx）
        setNonBlocking(priv->socketHandle, true);
        int connectResult = connect(priv->socketHandle, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result);

        if (connectResult == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
                XAbstractSocket_setSocketError(self, winsockErrorToSocketError(error), NULL);
                closesocket(priv->socketHandle);
                priv->socketHandle = INVALID_SOCKET;
                XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                return;
            }
        }
        else {
               // 立即连接成功
               if (useProxy) {
                   // 代理连接成功后，进行代理握手
                   if (!connect_via_proxy(self, hostName, port)) {
                       closesocket(priv->socketHandle);
                       priv->socketHandle = INVALID_SOCKET;
                       XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                       return;
                   }
               }
               priv->connected = true;
               XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
               // 设置打开模式
               ((XIODevice*)self)->m_openMode = XIODevice_ReadWrite;
               priv->autoRead = true;
               startAsyncRead(self);
        }
    }
}
void XAbstractSocket_platform_disconnectFromHost(XAbstractSocket* self) 
{
    if (!self) return;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv) return;
    
    XAbstractSocket_setSocketState(self, XAbstractSocket_ClosingState);
    
    if (priv->socketHandle != INVALID_SOCKET) {
        CancelIo((HANDLE)priv->socketHandle);
        closesocket(priv->socketHandle);
        priv->socketHandle = INVALID_SOCKET;
    }
    
    priv->connected = false;
    priv->readPending = false;
    priv->writePending = false;
    
    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
}

intptr_t XAbstractSocket_platform_socketDescriptor(const XAbstractSocket* self) {
    if (!self) return -1;
    XAbstractSocketPrivate* priv = getPriv((XAbstractSocket*)self);
    return priv ? (intptr_t)priv->socketHandle : -1;
}

bool XAbstractSocket_platform_setSocketDescriptor(XAbstractSocket* self, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode) {
    if (!self || socketDescriptor == -1) return false;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv) return false;
    
    if (priv->socketHandle != INVALID_SOCKET) {
        closesocket(priv->socketHandle);
    }
    
    priv->socketHandle = (SOCKET)socketDescriptor;
    priv->connected = (state == XAbstractSocket_ConnectedState);
    
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(priv->socketHandle), self)) {
        return false;
    }
    
    XAbstractSocket_setSocketState(self, state);
    self->base.m_openMode = openMode;
    self->isValidFlag = true;
    
    if (priv->connected) {
        priv->autoRead = true;
        startAsyncRead(self);
    }
    
    return true;
}

void XAbstractSocket_platform_setSocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option, const XVariant* value) {
    if (!self || !value) return;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return;
    
    int level = SOL_SOCKET;
    int optname = 0;
    int intVal = 0;
    
    switch (option) {
    case XAbstractSocket_LowDelayOption:
        level = IPPROTO_TCP;
        optname = TCP_NODELAY;
        intVal = XVariant_toBool(value) ? 1 : 0;
        break;
    case XAbstractSocket_KeepAliveOption:
        optname = SO_KEEPALIVE;
        intVal = XVariant_toBool(value) ? 1 : 0;
        break;
    case XAbstractSocket_SendBufferSizeSocketOption:
        optname = SO_SNDBUF;
        intVal = XVariant_toInt(value);
        break;
    case XAbstractSocket_ReceiveBufferSizeSocketOption:
        optname = SO_RCVBUF;
        intVal = XVariant_toInt(value);
        break;
    default:
        return;
    }
    
    setsockopt(priv->socketHandle, level, optname, (const char*)&intVal, sizeof(intVal));
}

XVariant* XAbstractSocket_platform_socketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option) {
    if (!self) return NULL;
    XAbstractSocketPrivate* priv = getPriv(self);
    if (!priv || priv->socketHandle == INVALID_SOCKET) return NULL;
    
    int level = SOL_SOCKET;
    int optname = 0;
    int intVal = 0;
    int optlen = sizeof(intVal);
    
    switch (option) {
    case XAbstractSocket_LowDelayOption:
        level = IPPROTO_TCP;
        optname = TCP_NODELAY;
        break;
    case XAbstractSocket_KeepAliveOption:
        optname = SO_KEEPALIVE;
        break;
    case XAbstractSocket_SendBufferSizeSocketOption:
        optname = SO_SNDBUF;
        break;
    case XAbstractSocket_ReceiveBufferSizeSocketOption:
        optname = SO_RCVBUF;
        break;
    default:
        return NULL;
    }
    
    if (getsockopt(priv->socketHandle, level, optname, (char*)&intVal, &optlen) == SOCKET_ERROR) {
        return NULL;
    }
    
    return XVariant_create_int(intVal);
}



void XAbstractSocket_platform_setReadBufferSize(XAbstractSocket* self, int64_t size) {
    if (!self) return;
    // 存储设置值
    self->readBufferSize = size;
    // 注意：Windows IOCP 的缓冲区大小由系统管理
    // 如果需要，可以设置 SO_RCVBUF
    XAbstractSocketPrivate* priv = getPriv(self);
    if (priv && priv->socketHandle != INVALID_SOCKET && size > 0) {
        int bufSize = (size > INT_MAX) ? INT_MAX : (int)size;
        setsockopt(priv->socketHandle, SOL_SOCKET, SO_RCVBUF, 
                   (const char*)&bufSize, sizeof(bufSize));
    }
}

// resume - 恢复数据传输（对于非 SSL 套接字，空操作即可）

// // flush - 刷新发送缓冲区，等待所有待发送数据发送完毕
// bool XAbstractSocket_flush(XAbstractSocket* sock) {
//     if (!sock) return false;
//     XAbstractSocketPrivate* priv = getPriv(sock);
//     if (!priv || priv->socketHandle == INVALID_SOCKET) return false;
    
//     XIODevice* io = (XIODevice*)sock;
    
//     // 检查是否有待发送数据
//     while (XIODevice_bytesToWrite_base(io) > 0) {
//         // 等待数据发送完成
//         if (!XAbstractSocket_waitForBytesWritten(sock, 100)) {
//             // 超时或出错
// ==================== 平台初始化和析构 ====================

void XAbstractSocket_platform_init(XAbstractSocket* sock, XAbstractSocket_SocketType type)
{
    (void)type;  // 类型已存储在sock->socketType中
    sock->d_ptr = XAbstractSocketPrivate_create();
}

void XAbstractSocket_platform_deinit(XAbstractSocket* sock)
{
    if (!sock) return;
    XAbstractSocketPrivate* priv = getPriv(sock);
    if (priv) {
        if (priv->socketHandle != INVALID_SOCKET) {
            CancelIo((HANDLE)priv->socketHandle);
            closesocket(priv->socketHandle);
            priv->socketHandle = INVALID_SOCKET;
        }
        XAbstractSocketPrivate_delete(priv);
        sock->d_ptr = NULL;
    }
}

#endif // _WIN32

