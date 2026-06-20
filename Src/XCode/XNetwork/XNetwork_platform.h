/**
 * @file XNetwork_platform.h
 * @brief 网络操作统一平台抽象接口（对标 XFileSystem_platform.h 风格）
 *
 * 设计原则（最少化平台函数）：
 * - 所有参数/返回值均为基础类型或现有类（XHostAddress/XString），绝不定义冗余结构体
 * - 不透明句柄 (XSocketHandle/XServerHandle) 代替操作系统 SOCKET/fd
 * - 平台层只暴露真正需要 OS 系统调用的底层操作
 * - 地址解析/格式化/分类/子网判断等纯计算逻辑由 XHostAddress 提供，不在本接口中重复
 * - 事件通知直接集成现有 IOCP/epoll 架构（XEventDispatcher）
 *
 * 架构层级：
 *   XAbstractSocket / XTcpSocket / XTcpServer / XHostInfo / ...  ← 业务中间层
 *        │
 *   XNetwork_platform.h (本文件)                                   ← 平台抽象
 *   ├────── win32 ────── linux ────── macos ──── ...               ← 平台实现
 */

#ifndef XNETWORK_PLATFORM_H
#define XNETWORK_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XHostAddress.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 一、基础句柄类型
 * ========================================================================= */

/** 套接字描述符（Windows: SOCKET, POSIX: fd），-1 为无效 */
typedef intptr_t XSocketHandle;

/** TCP 服务器监听描述符，-1 为无效 */
typedef intptr_t XServerHandle;

/* =========================================================================
 * 二、枚举类型（补充 XHostAddress 未定义的）
 * ========================================================================= */

/** 套接字类型 */
typedef enum {
    XNetworkSocket_TCP = 0,
    XNetworkSocket_UDP = 1
} XNetworkSocketType;

/** 协议类型（直接复用 XHostAddress_NetworkLayerProtocol） */
typedef XHostAddress_NetworkLayerProtocol XNetworkProtocol;
#define XNetworkProtocol_IPv4  XHostAddress_IPv4Protocol
#define XNetworkProtocol_IPv6  XHostAddress_IPv6Protocol
#define XNetworkProtocol_Any   XHostAddress_AnyIPProtocol

/* =========================================================================
 * 三、代理配置（最小化结构体）
 * ========================================================================= */

typedef enum {
    XNetworkProxy_None    = 0,
    XNetworkProxy_Socks5  = 1,
    XNetworkProxy_Http    = 2
} XNetworkProxyType;

typedef struct {
    XNetworkProxyType type;
    char    host[256];
    uint16_t port;
    char    user[128];
    char    pass[128];
    bool    hasAuth : 1;
    bool    _pad    : 7;
} XNetworkProxyInfo;

/* =========================================================================
 * 四、网络接口信息
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

/* =========================================================================
 * 五、事件回调类型（用于平台层通知中间层）
 * ========================================================================= */

enum {
    XNetworkEvent_Read    = 1 << 0,
    XNetworkEvent_Write   = 1 << 1,
    XNetworkEvent_Close   = 1 << 2,
    XNetworkEvent_Accept  = 1 << 3,
    XNetworkEvent_Connect = 1 << 4
};

typedef void (*XNetworkEventCb)(XSocketHandle sock, int eventMask, void* userData);

/* =========================================================================
 * 六、平台初始化 / 清理 / 错误
 * ========================================================================= */

void  XNetwork_ensureInit(void);
void  XNetwork_cleanup(void);
int   XNetwork_lastError(void);
bool  XNetwork_isEAgain(int err);
char* XNetwork_errorString(int errorCode);
int   XNetwork_classifyError(int errorCode);

/* =========================================================================
 * 七、核心套接字操作（所有参数使用 XHostAddress）
 * ========================================================================= */

/** @brief 创建非阻塞套接字。@return -1 失败 */
XSocketHandle XNetwork_createSocket(XNetworkSocketType type, XNetworkProtocol proto);

/** 关闭套接字 */
void XNetwork_closeSocket(XSocketHandle sock);

/** 绑定本地地址/端口 */
bool XNetwork_bind(XSocketHandle sock, const XHostAddress* addr, uint16_t port,
                   bool reuseAddr, bool shareAddr);

/** @brief 发起异步连接（DNS 由中间层完成，这里只收 IP） */
bool XNetwork_connect(XSocketHandle sock, const XHostAddress* addr, uint16_t port);

/** 关闭连接方向: 0=读 1=写 2=读写 */
bool XNetwork_shutdown(XSocketHandle sock, int how);

/** @brief 读操作。@param isDatagram 为 true 时填充 sender/senderPort */
int64_t XNetwork_recv(XSocketHandle sock, void* buf, int64_t len,
                      bool isDatagram, XHostAddress* sender, uint16_t* senderPort);

/** @brief 写操作。@param isDatagram 为 true 时指定 receiver/receiverPort */
int64_t XNetwork_send(XSocketHandle sock, const void* buf, int64_t len,
                      bool isDatagram, const XHostAddress* receiver, uint16_t receiverPort);

/** 获取本地地址/端口 */
bool XNetwork_getLocalAddr(XSocketHandle sock, XHostAddress* addr, uint16_t* port);
/** 获取对端地址/端口 */
bool XNetwork_getPeerAddr(XSocketHandle sock, XHostAddress* addr, uint16_t* port);

/* =========================================================================
 * 八、TCP 服务器
 * ========================================================================= */

/** 创建监听套接字 */
XServerHandle XNetwork_createServer(const XHostAddress* addr, uint16_t port,
                                    int backlog, bool reuseAddr);

/** 非阻塞 accept。@return -1 无连接或出错 */
XSocketHandle XNetwork_serverAccept(XServerHandle server,
                                    XHostAddress* clientAddr, uint16_t* clientPort);

/** 获取实际监听端口（创建时 port=0 由系统分配） */
uint16_t XNetwork_serverPort(XServerHandle server);

/** 关闭服务器 */
void XNetwork_closeServer(XServerHandle server);

/* =========================================================================
 * 九、DNS 查询
 * ========================================================================= */

/** 同步解析主机名。结果通过 addrs/count 返回，调用者负责释放 addrs（XHostAddress_delete_base） */
bool XNetwork_lookupName(const char* name, XHostAddress** addrs, int* count);

/** 本机主机名（调用者 free） */
char* XNetwork_localHostName(void);

/** 本机域名（可能为空，调用者 free） */
char* XNetwork_localDomainName(void);

/* =========================================================================
 * 十、网络接口枚举
 * ========================================================================= */

XNetworkInterfaceIterator XNetwork_enumInterfacesBegin(void);
bool XNetwork_enumInterfacesNext(XNetworkInterfaceIterator iter, XNetworkInterfaceEntry* out);
void XNetwork_enumInterfacesEnd(XNetworkInterfaceIterator iter);

/** 获取指定接口上的地址。调用者负责释放 addrs（XHostAddress_delete_base）和 masks */
bool XNetwork_getInterfaceAddresses(const char* ifname,
                                    XHostAddress** addrs, XHostAddress** masks,
                                    int* count);

/* =========================================================================
 * 十一、事件通知（集成 IOCP / epoll）
 * ========================================================================= */

/** @brief 将套接字绑定到事件通知系统，有事件时回调 cb(userData) */
bool XNetwork_registerEvents(XSocketHandle sock, XNetworkEventCb cb, void* userData);
void XNetwork_unregisterEvents(XSocketHandle sock);

/** @brief 对服务器套接字注册事件 */
bool XNetwork_serverRegisterEvents(XServerHandle server, XNetworkEventCb cb, void* userData);
void XNetwork_serverUnregisterEvents(XServerHandle server);

/* =========================================================================
 * 十二、代理隧道握手
 * ========================================================================= */

/** SOCKS5 握手 */
bool XNetwork_socks5Connect(XSocketHandle sock, const XNetworkProxyInfo* proxy,
                            const XHostAddress* targetAddr, uint16_t targetPort);

/* =========================================================================
 * 十三、套接字私有数据管理（供 XAbstractSocket 使用）
 * ========================================================================= */

typedef struct XNetworkSocketPrivate XNetworkSocketPrivate;

XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner);
void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv);
XSocketHandle XNetwork_privateSocket(const XNetworkSocketPrivate* priv);
void XNetwork_privateSetSocket(XNetworkSocketPrivate* priv, XSocketHandle sock);
bool XNetwork_privateIsConnecting(const XNetworkSocketPrivate* priv);
void XNetwork_privateSetConnecting(XNetworkSocketPrivate* priv, bool connecting);
void* XNetwork_privateOwner(const XNetworkSocketPrivate* priv);
const XHostAddress* XNetwork_privatePendingPeerAddr(const XNetworkSocketPrivate* priv);
void XNetwork_privateSetPendingPeerAddr(XNetworkSocketPrivate* priv, const XHostAddress* addr);
uint16_t XNetwork_privatePendingPeerPort(const XNetworkSocketPrivate* priv);
void XNetwork_privateSetPendingPeerPort(XNetworkSocketPrivate* priv, uint16_t port);

/* =========================================================================
 * 十四、UDP 数据报操作
 * ========================================================================= */

/**
 * @brief 判断是否有待读的数据报
 */
bool XNetwork_hasPendingDatagrams(XSocketHandle sock);

/**
 * @brief 获取待读数据报大小
 * @return 数据报字节数，无数据报时返回 -1
 */
int64_t XNetwork_pendingDatagramSize(XSocketHandle sock);

/* =========================================================================
 * 十五、多播组操作
 * ========================================================================= */

/**
 * @brief 加入多播组
 * @param sock 套接字句柄
 * @param groupAddress 多播组地址
 * @param interfaceIndex 网络接口索引（0 表示默认）
 * @return 成功返回 true
 */
bool XNetwork_joinMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress,
                                  uint32_t interfaceIndex);

/**
 * @brief 离开多播组
 */
bool XNetwork_leaveMulticastGroup(XSocketHandle sock, const XHostAddress* groupAddress,
                                   uint32_t interfaceIndex);

/**
 * @brief 设置多播输出接口
 */
bool XNetwork_setMulticastInterface(XSocketHandle sock, uint32_t interfaceIndex);

/**
 * @brief 获取多播输出接口
 */
uint32_t XNetwork_multicastInterface(XSocketHandle sock);

/**
 * @brief 设置多播 TTL（跳数限制）
 */
bool XNetwork_setMulticastTtl(XSocketHandle sock, int ttl);

/**
 * @brief 获取多播 TTL
 */
int XNetwork_multicastTtl(XSocketHandle sock);

/**
 * @brief 设置多播回环
 */
bool XNetwork_setMulticastLoopback(XSocketHandle sock, bool enabled);

/**
 * @brief 获取多播回环设置
 */
bool XNetwork_multicastLoopback(XSocketHandle sock);

/* =========================================================================
 * 十六、高级操作（供 XAbstractSocket 直接使用）
 * 
 * 这些函数封装了完整的操作流程，包括 IOCP 异步 I/O
 * ========================================================================= */

/**
 * @brief 绑定套接字到地址/端口
 */
bool XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                         uint16_t port, bool reuseAddr, bool shareAddr, int sockType);

/**
 * @brief 异步连接到主机
 */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const char* hostName,
                            uint16_t port, int protocol, int sockType,
                            const void* proxy);

/**
 * @brief 断开连接
 */
void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv);

/**
 * @brief 从套接字读取数据到缓冲区
 */
int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len, 
                            int sockType, void* ringBuffer);

/**
 * @brief 将数据写入套接字
 */
int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             int sockType, const XHostAddress* destAddr, uint16_t destPort,
                             void* ringBuffer);

/**
 * @brief 处理 IOCP 完成事件
 */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, XEvent* e);

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

/**
 * @brief 获取套接字描述符
 */
intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv);

/**
 * @brief 设置外部套接字描述符
 */
bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, 
                                  int state, int openMode);

/**
 * @brief 获取连接状态
 */
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv);

/**
 * @brief 更新对端地址/端口
 */
void XNetwork_socketUpdatePeerInfo(XNetworkSocketPrivate* priv, 
                                   const XHostAddress* addr, uint16_t port);

/**
 * @brief 获取读取缓冲区指针
 */
const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv);

/**
 * @brief 获取已完成读取的字节数
 */
size_t XNetwork_socketFinishedBytes(const XNetworkSocketPrivate* priv);

/**
 * @brief 检查是否有挂起的读操作
 */
bool XNetwork_socketIsReadPending(const XNetworkSocketPrivate* priv);

/**
 * @brief 重置读状态并启动下一次异步读取
 */
void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_PLATFORM_H */