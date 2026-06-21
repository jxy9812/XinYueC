/**
 * @file XNetwork_platform.h
 * @brief 网络操作平台抽象接口
 * 
 * 职责：
 * - 平台相关的所有网络操作（Windows IOCP / Linux epoll）
 * - 异步 I/O 管理
 * - 套接字私有数据管理
 * 
 * 架构层级：
 *   XAbstractSocket / XTcpSocket / XUdpSocket  ← 业务中间层（通用代码）
 *        │
 *   XNetwork_platform.h (本文件)              ← 平台抽象
 *   ├────── windows/XNetwork_win32.c          ← Windows IOCP 实现
 *   ├────── linux/XNetwork_linux.c            ← Linux epoll 实现
 *   └────── ...
 */

#ifndef XNETWORK_PLATFORM_H
#define XNETWORK_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XHostAddress.h"
#include "XNetworkProxy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 一、基础类型
 * ========================================================================= */

/** 套接字描述符，-1 为无效 */
typedef intptr_t XSocketHandle;

/** TCP 服务器句柄，-1 为无效 */
typedef intptr_t XServerHandle;

/** 套接字类型 */
typedef enum {
    XNetwork_Tcp = 0,
    XNetwork_Udp = 1
} XNetworkSocketType;

/** 协议类型（复用 XHostAddress） */
typedef XHostAddress_NetworkLayerProtocol XNetworkProtocol;
#define XNetwork_IPv4  XHostAddress_IPv4Protocol
#define XNetwork_IPv6  XHostAddress_IPv6Protocol
#define XNetwork_Any   XHostAddress_AnyIPProtocol

/* =========================================================================
 * 二、平台初始化与错误
 * ========================================================================= */

void  XNetwork_ensureInit(void);
void  XNetwork_cleanup(void);
int   XNetwork_lastError(void);
bool  XNetwork_isEAgain(int err);
char* XNetwork_errorString(int errorCode);

/* =========================================================================
 * 三、套接字私有数据（由平台层管理异步IO状态）
 * ========================================================================= */

typedef struct XNetworkSocketPrivate XNetworkSocketPrivate;

/** 创建私有数据 */
XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner);
/** 销毁私有数据 */
void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv);

/** 获取套接字句柄 */
XSocketHandle XNetwork_socketHandle(const XNetworkSocketPrivate* priv);
/** 获取拥有者对象 */
void* XNetwork_socketOwner(const XNetworkSocketPrivate* priv);
/** 是否已连接 */
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv);

/* =========================================================================
 * 四、核心操作（统一异步接口）
 * ========================================================================= */

/**
 * @brief 绑定套接字到地址/端口
 * @param priv 私有数据
 * @param address 绑定地址
 * @param port 端口
 * @param reuseAddr 是否重用地址
 * @param shareAddr 是否共享地址
 * @param sockType 套接字类型
 * @return 成功返回实际端口
 * @note UDP 绑定后会自动启动异步读取
 */
uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr, 
                              XNetworkSocketType sockType);
/* 返回实际绑定的端口号，失败返回 0 */

/**
 * @brief 异步连接到主机
 * @param priv 私有数据
 * @param hostName 主机名
 * @param port 端口
 * @param protocol 协议
 * @param sockType 套接字类型
 * @param proxy 代理配置（可为NULL）
 * @return 成功发起连接返回 true
 */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, XNetworkProtocol protocol, 
                            XNetworkSocketType sockType, const void* proxy);

/**
 * @brief 断开连接
 */
void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv);

/**
 * @brief 从套接字读取数据（从内部缓冲区读取）
 * @param priv 私有数据
 * @param buf 目标缓冲区
 * @param len 最大读取长度
 * @param sockType 套接字类型
 * @param ringBuffer 外部环形缓冲区（可为NULL）
 * @return 实际读取字节数，-1 表示错误
 */
int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer);

/**
 * @brief 异步写入数据到套接字
 * @param priv 私有数据
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param sockType 套接字类型
 * @param destAddr 目标地址（UDP使用，TCP传NULL）
 * @param destPort 目标端口（UDP使用）
 * @param ringBuffer 外部环形缓冲区（可为NULL）
 * @return 已写入缓冲区的字节数
 * @note 实际发送是异步的，通过 bytesWritten 信号通知完成
 */
int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr, 
                             uint16_t destPort, void* ringBuffer);

/**
 * @brief 处理平台事件（IOCP完成等）
 */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event);

/**
 * @brief 设置套接字描述符（外部设置）
 */
bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, 
                                  int state, int openMode);

/**
 * @brief 获取套接字描述符
 */
intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv);

/**
 * @brief 设置套接字选项
 */
bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value);

/**
 * @brief 获取套接字选项
 */
void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option);

/**
 * @brief 设置读缓冲区大小
 */
void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size);

/* =========================================================================
 * 五、异步读取状态（供事件处理使用）
 * ========================================================================= */

/** 获取异步读取缓冲区 */
const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv);
/** 获取已完成的字节数 */
size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv);
size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv);
/** 是否有待读数据 */
bool XNetwork_socketHasPendingData(const XNetworkSocketPrivate* priv);
/** 继续异步读取（处理完数据后调用） */
void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp);

/* =========================================================================
 * 六、TCP 服务器
 * ========================================================================= */

XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv,const XHostAddress* addr, uint16_t port,
                                    int backlog, bool reuseAddr);
XSocketHandle XNetwork_serverAccept( XServerHandle server,
                                    XHostAddress* clientAddr, uint16_t* clientPort);
uint16_t XNetwork_serverPort(XServerHandle server);
void XNetwork_serverClose(XServerHandle server);

/**
 * @brief 获取异步 Accept 完成后的客户端套接字
 * @param priv 服务器套接字私有数据
 * @param clientAddr 输出客户端地址（可为 NULL）
 * @param clientPort 输出客户端端口（可为 NULL）
 * @return 客户端套接字句柄，失败返回 -1
 * @note 必须在收到 FD_ACCEPT 事件后调用
 */
XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv,
                                               XHostAddress* clientAddr, uint16_t* clientPort);

/**
 * @brief 继续异步 Accept（处理完一个连接后调用）
 * @param priv 服务器套接字私有数据
 * @return 成功返回 true
 */
bool XNetwork_serverContinueAccept(XNetworkSocketPrivate* priv);

/* =========================================================================
 * 七、DNS 查询
 * ========================================================================= */

bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count);
char* XNetwork_localHostName(void);
char* XNetwork_localDomainName(void);

/* =========================================================================
 * 八、网络接口枚举
 * ========================================================================= */

typedef struct {
    char     name[128];
    char     readableName[256];
    int      index;
    uint8_t  hwAddr[32];
    int      hwAddrLen;
    uint32_t flags;
    uint32_t type;
    int      mtu;
} XNetworkInterfaceEntry;

enum {
    XNetworkIf_Up        = 1 << 0,
    XNetworkIf_Running   = 1 << 1,
    XNetworkIf_Loopback  = 1 << 2,
    XNetworkIf_Multicast = 1 << 3,
    XNetworkIf_Broadcast = 1 << 4
};

typedef void* XNetworkInterfaceIterator;

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void);
bool XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter, XNetworkInterfaceEntry* out);
void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter);
bool XNetwork_getInterfaceAddresses(const char* ifname,
                                    XHostAddress** addrs, XHostAddress** masks, int* count);

/* =========================================================================
 * 九、多播组
 * ========================================================================= */

bool XNetwork_joinMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress, uint32_t ifIndex);
bool XNetwork_leaveMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress, uint32_t ifIndex);
bool XNetwork_setMulticastInterface(XSocketHandle sock, uint32_t ifIndex);
uint32_t XNetwork_multicastInterface(XSocketHandle sock);
bool XNetwork_setMulticastTtl(XSocketHandle sock, int ttl);
int XNetwork_multicastTtl(XSocketHandle sock);
bool XNetwork_setMulticastLoopback(XSocketHandle sock, bool enabled);
bool XNetwork_multicastLoopback(XSocketHandle sock);

/* =========================================================================
 * 十、UDP 特有
 * ========================================================================= */

/**
 * @brief 获取最近接收的 UDP 数据报的发送者信息
 * @param priv 套接字私有数据
 * @param srcAddr 输出发送者地址（可为 NULL）
 * @param srcPort 输出发送者端口（可为 NULL）
 * @return 成功返回 true
 * @note 必须在数据读取后、下一次读取前调用
 */
bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv,
                                     XHostAddress* srcAddr, uint16_t* srcPort);

/**
 * @brief 同步发送 UDP 数据报
 */
int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port);

/* =========================================================================
 * 十一、代理隧道（使用 XNetworkProxy 模块类型）
 * ========================================================================= */

/**
 * @brief SOCKS5 代理连接
 * @param sock 已连接到代理服务器的套接字
 * @param proxy 代理配置
 * @param targetAddr 目标地址
 * @param targetPort 目标端口
 * @return 成功返回 true
 */
bool XNetwork_socks5Connect(XSocketHandle sock, const XNetworkProxy* proxy,
                            const XHostAddress* targetAddr, uint16_t targetPort);

/**
 * @brief HTTP 代理 CONNECT 隧道连接
 * @param sock 已连接到代理服务器的套接字
 * @param proxy 代理配置
 * @param targetAddr 目标地址
 * @param targetPort 目标端口
 * @return 成功返回 true
 */
bool XNetwork_httpConnect(XSocketHandle sock, const XNetworkProxy* proxy,
                          const XHostAddress* targetAddr, uint16_t targetPort);

/**
 * @brief SOCKS5 代理 BIND 命令（用于服务器监听）
 * @param sock 已连接到代理服务器的套接字
 * @param proxy 代理配置
 * @param bindAddr 输出绑定的地址
 * @param bindPort 输出绑定的端口
 * @return 成功返回 true
 * @note 用于通过 SOCKS5 代理建立反向连接
 */
bool XNetwork_socks5Bind(XSocketHandle sock, const XNetworkProxy* proxy,
                         XHostAddress* bindAddr, uint16_t* bindPort);

/**
 * @brief 通过代理创建 TCP 服务器
 * @param proxy 代理配置（NULL 表示不使用代理）
 * @param addr 监听地址
 * @param port 监听端口
 * @param backlog 积压大小
 * @param reuseAddr 是否重用地址
 * @return 服务器句柄，失败返回 -1
 * @note 如果代理支持 ListeningCapability，则通过代理建立监听
 */
XServerHandle XNetwork_serverCreateWithProxy(XNetworkSocketPrivate* priv,const XNetworkProxy* proxy,
                                             const XHostAddress* addr, uint16_t port,
                                             int backlog, bool reuseAddr);

/**
 * @brief 通过代理接受连接
 * @param server 服务器句柄
 * @param proxy 代理配置（可为 NULL）
 * @param clientAddr 输出客户端地址
 * @param clientPort 输出客户端端口
 * @return 客户端套接字句柄，失败返回 -1
 */
XSocketHandle XNetwork_serverAcceptWithProxy(XServerHandle server,
                                             const XNetworkProxy* proxy,
                                             XHostAddress* clientAddr, uint16_t* clientPort);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_PLATFORM_H */
