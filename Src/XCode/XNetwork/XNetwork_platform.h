/**
 * @file XNetwork_platform.h
 * @brief 网络操作平台抽象接口（精简版）
 * 
 * 职责：
 * - 平台相关的所有网络操作（Windows IOCP / Linux io_uring / lwIP）
 * - 异步 I/O 管理
 * - 套接字私有数据管理
 * 
 * 架构层级：
 *   XAbstractSocket / XTcpSocket / XUdpSocket  -> 业务中间层（通用代码）
 *        |
 *   XNetwork_platform.h (本文件)               -> 平台抽象
 *   |-- windows/XNetwork_win32.c               -> Windows IOCP 实现
 *   |-- Posix/XNetwork_posix.c                 -> Linux io_uring 实现
 *   |-- lwip/XNetwork_lwip.c                   -> lwIP 回调驱动实现
 * 
 * 代理协议（SOCKS5/HTTP）在 XNetworkProxy 模块中用通用代码实现
 */

#ifndef XNETWORK_PLATFORM_H
#define XNETWORK_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XHostAddress.h"
#include "XFileDescriptor.h"
#include "XNetworkProxy.h"
#include "XNetwork_config.h"
#include "XByteArray.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 前置声明 - XNetworkInterface 在 XNetworkInterface.h 中定义 */

/* lwIP 平台额外声明 */
#ifdef XNETWORK_USE_LWIP
#include "XNetwork_lwip_platform.h"
#endif

typedef struct XNetworkInterface XNetworkInterface;
typedef struct XRingBuffer XRingBuffer;
/* =========================================================================
 * 平台移植指南
 * =========================================================================
 *
 * 本文件声明的函数均为各平台必须实现的公共接口。移植新平台时，
 * 根据异步 I/O 模型不同，额外需要实现以下平台内部函数：
 *
 * 【模型 A：投递 + 完成通知】— Windows IOCP / Linux io_uring
 *   - static startAsyncRead(priv, isUdp)
 *       向异步引擎投递一次读操作（WSARecv / IORING_OP_RECV）。
 *       由 socketBind / socketConnect / socketRead / socketContinueRead 内部调用。
 *   - static startAsyncWrite(priv, data, len, destAddr, destPort, isUdp)
 *       向异步引擎投递一次写操作（WSASend / IORING_OP_SEND）。
 *       由 socketWrite 内部调用。
 *   以上两个函数为平台内部 static 实现，不在本头文件声明，
 *   但移植 IOCP / io_uring 类平台时必须实现。
 *
 * 【模型 B：回调驱动】— lwIP
 *   - 读：协议栈通过回调（tcp_recv / udp_recv）自动填充内部缓冲区，
 *         应用层 pull 取数据，无需 startAsyncRead。
 *   - 写：直接调用 tcp_write + tcp_output / udp_sendto，同步入栈，
 *         无需 startAsyncWrite。
 *   回调驱动平台不需要实现 startAsyncRead / startAsyncWrite。
 *
 * 两种模型都必须实现本文件声明的全部公共函数。
 * ========================================================================= */

/* =========================================================================
 * 一、基础类型
 * ========================================================================= */

/** 套接字描述符，-1 为无效 */
typedef intptr_t XSocketHandle;

/** TCP 服务器句柄，-1 为无效 */
typedef intptr_t XServerHandle;

/** 套接字类型 */
typedef enum {
    XNetwork_Tcp = 0,   /**< TCP 套接字 */
    XNetwork_Udp = 1    /**< UDP 套接字 */
} XNetworkSocketType;

/** 协议类型（复用 XHostAddress） */
typedef XHostAddress_NetworkLayerProtocol XNetworkProtocol;
#define XNetwork_IPv4  XHostAddress_IPv4Protocol
#define XNetwork_IPv6  XHostAddress_IPv6Protocol
#define XNetwork_Any   XHostAddress_AnyIPProtocol

/* =========================================================================
 * 二、平台初始化与错误
 * ========================================================================= */

/**
 * @brief 确保网络子系统已初始化
 * @note 多次调用安全，内部有引用计数
 */
void XNetwork_ensureInit(void);

/**
 * @brief 清理网络子系统
 * @note 与 ensureInit 配对使用，内部有引用计数
 */
void XNetwork_cleanup(void);

/**
 * @brief 获取最后一次网络错误码
 * @return 平台相关的错误码
 */
int XNetwork_lastError(void);

/**
 * @brief 将错误码转换为可读字符串
 * @param errorCode 错误码
 * @return 错误描述字符串，需调用者 XFree_System 释放
 */
char* XNetwork_errorString(int errorCode);

/* =========================================================================
 * DNS
 * ========================================================================= */

/**
 * @brief 获取本机主机名
 * @return XString 主机名（调用者需用 XString_delete_base 释放），失败返回 NULL
 */
XString* XNetwork_localHostName(void);

/**
 * @brief 同步 DNS 查询
 * @param name 待解析的主机名
 * @return XVector<XHostAddress> 地址列表，失败返回 NULL
 * @note 调用者需用 XVector_delete_base 释放返回的向量
 */
XVector* XNetwork_lookupName(const XString* name);

/* =========================================================================
 * 三、套接字私有数据（由平台层管理异步IO状态）
 * ========================================================================= */

typedef struct XNetworkSocketPrivate {
    void*    owner;                     /**< 拥有者 XAbstractSocket */
    XVector* notifiers;                /**< XVector<XSocketNotifier*>，socket notifier 列表 */
} XNetworkSocketPrivate;

/**
 * @brief 创建套接字私有数据
 * @param owner 拥有者对象（通常是 XAbstractSocket）
 * @return 私有数据指针，失败返回 NULL
 */
XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner);

/**
 * @brief 销毁套接字私有数据
 * @param priv 私有数据指针
 */
void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv);

/**
 * @brief 获取套接字描述符
 * @param priv 私有数据
 * @return 套接字描述符，无效返回 -1
 */
intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv);

/**
 * @brief 检查套接字是否已连接
 * @param priv 私有数据
 * @return 已连接返回 true
 */
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv);

/* =========================================================================
 * 四、核心操作（统一异步接口）
 * ========================================================================= */

/**
 * @brief 绑定套接字到地址/端口
 * @param priv 私有数据
 * @param address 绑定地址
 * @param port 端口，0 表示自动分配
 * @param reuseAddr 是否重用地址（SO_REUSEADDR）
 * @param shareAddr 是否共享地址（SO_EXCLUSIVEADDRUSE 的反向）
 * @param sockType 套接字类型
 * @return 成功返回实际端口，失败返回 0
 * @note UDP 绑定后会自动启动异步读取
 */
uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr, 
                              XNetworkSocketType sockType);

/**
 * @brief 异步连接到主机
 * @param priv 私有数据
 * @param hostName 主机名或 IP 地址
 * @param port 端口
 * @param protocol 协议类型（IPv4/IPv6/Any）
 * @param sockType 套接字类型
 * @return 成功发起连接返回 true，立即失败返回 false
 * @note 连接结果通过事件通知，成功后状态变为 ConnectedState
 */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const XString* hostName,
                            uint16_t port, XNetworkProtocol protocol, 
                            XNetworkSocketType sockType);

/**
 * @brief 断开套接字连接
 * @param priv 私有数据
 * @note 关闭套接字，清理异步操作，状态变为 UnconnectedState
 */
void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv);

/**
 * @brief 从套接字读取数据
 * @param priv 私有数据
 * @param buf 目标缓冲区
 * @param len 最大读取长度
 * @param sockType 套接字类型
 * @param ringBuffer 外部环形缓冲区（可为 NULL，用于缓冲管理）
 * @return 实际读取字节数，-1 表示错误，0 表示无数据
 * @note 数据来自内部接收缓冲区，由异步 IO 填充
 */
int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer);

/**
 * @brief 异步写入数据到套接字
 * @param priv 私有数据
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param sockType 套接字类型
 * @param destAddr 目标地址（UDP 使用，TCP 传 NULL）
 * @param destPort 目标端口（UDP 使用）
 * @param ringBuffer 外部环形缓冲区（可为 NULL，用于写入队列）
 * @return 已提交发送的字节数，-1 表示错误
 * @note 实际发送是异步的，通过 bytesWritten 信号通知完成
 */
int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr, 
                             uint16_t destPort, void* ringBuffer);

/**
 * @brief 处理平台事件
 * @param priv 私有数据
 * @param event 事件对象
 * @return 成功处理返回 true
 * @note 由事件循环调用，处理 IOCP 完成、epoll 事件等
 */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event);

/**
 * @brief 设置套接字描述符（外部设置）
 * @param priv 私有数据
 * @param fd 套接字描述符
 * @param state 初始状态（XAbstractSocket_SocketState）
 * @param openMode 打开模式（XIODeviceBaseMode）
 * @return 成功返回 true
 * @note 用于接受外部创建的套接字（如服务器 accept 的客户端）
 */
bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd, 
                                  int state, int openMode);

/**
 * @brief 设置套接字选项
 * @param priv 私有数据
 * @param option 选项类型（XAbstractSocket_SocketOption）
 * @param value 选项值指针
 * @return 成功返回 true
 */
bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value);

/**
 * @brief 获取套接字选项
 * @param priv 私有数据
 * @param option 选项类型（XAbstractSocket_SocketOption）
 * @return 选项值指针（静态存储），失败返回 NULL
 */
void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option);

/**
 * @brief 设置读缓冲区大小
 * @param priv 私有数据
 * @param size 缓冲区大小，-1 表示无限制
 */
void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size);

/* =========================================================================
 * 五、异步读取状态（供事件处理使用）
 * ========================================================================= */

/**
 * @brief 获取异步读取缓冲区
 * @param priv 私有数据
 * @return 内部读取缓冲区指针
 */
const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv);

/**
 * @brief 获取异步读取完成的字节数
 * @param priv 私有数据
 * @return 本次完成的字节数
 */
size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv);

/**
 * @brief 获取异步写入完成的字节数
 * @param priv 私有数据
 * @return 本次完成的字节数
 */
size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv);

/**
 * @brief 继续异步读取
 * @param priv 私有数据
 * @param isUdp 是否为 UDP 套接字
 * @note 处理完数据后调用，发起下一次异步读取
 */
void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp);

/**
 * @brief 继续异步写入（写完成后将写环形缓冲区中残留数据继续投递）
 */
void XNetwork_socketContinueWrite(XNetworkSocketPrivate* priv, XRingBuffer* ringBuffer, bool isUdp);

/* =========================================================================
 * 六、TCP 服务器
 * ========================================================================= */

/**
 * @brief 创建 TCP 服务器
 * @param priv 服务器套接字私有数据
 * @param addr 绑定地址
 * @param port 端口，0 表示自动分配
 * @param backlog 监听队列最大长度
 * @param reuseAddr 是否重用地址
 * @return 服务器句柄，失败返回 -1
 */
XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr, 
                                     uint16_t port, int backlog, bool reuseAddr);

/**
 * @brief 启动异步接受客户端连接
 * @param priv 服务器私有数据
 * @return 成功发起异步 Accept 返回 true，失败返回 false
 * @note 在 serverCreate 成功后由用户层调用，启动异步 Accept；
 *       每次处理完一个连接后再次调用以继续接受。接受结果通过事件通知。
 */
bool XNetwork_serverAccept(XNetworkSocketPrivate* priv);

/**
 * @brief 关闭 TCP 服务器
 * @param priv 服务器私有数据
 * @param server 服务器句柄
 */
void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server);

/**
 * @brief 获取服务器绑定的端口
 * @param server 服务器句柄
 * @return 绑定的端口号，失败返回 0
 */
uint16_t XNetwork_serverPort(XServerHandle server);

/**
 * @brief 获取已接受的客户端 Socket
 * @param priv 服务器私有数据
 * @param clientAddr 输出客户端地址（可为 NULL）
 * @param clientPort 输出客户端端口（可为 NULL）
 * @return 客户端 Socket 句柄，失败返回 -1
 * @note 调用后客户端 Socket 从待处理队列中移除，调用者负责管理其生命周期
 */
XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv, XHostAddress* clientAddr, uint16_t* clientPort);

/* =========================================================================
 * 七、多播组（精简为 2 个 API）
 * ========================================================================= */

/** 多播操作类型 */
typedef enum {
    XMC_Join,       /**< 加入多播组 */
    XMC_Leave,      /**< 离开多播组 */
    XMC_SetIf,      /**< 设置多播接口 (arg: uint32_t* ifIndex) */
    XMC_GetIf,      /**< 获取多播接口 (arg: uint32_t* ifIndex) */
    XMC_SetTtl,     /**< 设置 TTL (arg: int* ttl) */
    XMC_GetTtl,     /**< 获取 TTL (arg: int* ttl) */
    XMC_SetLoop,    /**< 设置回环 (arg: bool* enabled) */
    XMC_GetLoop     /**< 获取回环 (arg: bool* enabled) */
} XMulticastOp;

/**
 * @brief 多播组操作（加入/离开）
 * @param sock 套接字句柄
 * @param join true=加入, false=离开
 * @param groupAddress 多播组地址
 * @param ifIndex 接口索引，0 表示默认接口
 * @return 成功返回 true
 */
bool XNetwork_multicastGroup(XSocketHandle sock, bool join, 
                             const XHostAddress* groupAddress, uint32_t ifIndex);

/**
 * @brief 多播参数操作（接口/TTL/回环）
 * @param sock 套接字句柄
 * @param op 操作类型
 * @param arg 参数指针（类型取决于 op）
 * @return 成功返回 0，失败返回 -1
 */
int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg);

/* =========================================================================
 * 八、UDP 特有
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
 * @param sock 套接字句柄
 * @param data 数据缓冲区
 * @param size 数据大小
 * @param address 目标地址
 * @param port 目标端口
 * @return 实际发送字节数，-1 表示错误
 */
int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port);

/* =========================================================================
 * 九、系统代理与GSSAPI（平台相关）
 * ========================================================================= */

/**
 * @brief 获取系统代理配置
 * @param queryUrl 查询URL（可为NULL）
 * @param outProxy 输出代理配置（需XNetworkProxy_delete_base释放）
 * @return 成功返回true
 * @note Windows: WinHttpGetProxyForUrl/注册表; Linux: 环境变量; macOS: CFProxySupport
 */
bool XNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy);

/**
 * @brief GSSAPI认证处理
 * @param serviceName 服务名称（如 "HTTP@proxy.example.com"）
 * @param inputToken 输入令牌（可为NULL表示初始化）
 * @param outputToken 输出令牌（XByteArray）
 * @param context 上下文指针（内部维护状态，首次传NULL）
 * @return 0=完成, 1=继续, -1=失败
 */
int XNetwork_gssapiAuth(const XString* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_PLATFORM_H */
