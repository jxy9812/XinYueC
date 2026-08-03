/**
 * @file XNetwork.h
 * @brief 跨平台网络后端的公共抽象接口。
 * @details 本文件位于 Src/XPlatform，定义 XAbstractSocket、XTcpServer、
 *          XUdpSocket 等通用网络模块与平台后端之间的契约，对齐 Qt 的
 *          QAbstractSocket、QTcpServer 和 QUdpSocket 的基础能力。声明本身
 *          不得调用 Windows、POSIX 或 lwIP API；具体实现在 Drive 目录中，
 *          由构建配置选择。所有 XString 参数仅在调用期间借用，其内部按
 *          UTF-16 代码单元存储；返回对象的所有权由各 API 注释明确说明。
 *
 * 架构层级：
 *   XAbstractSocket / XTcpSocket / XUdpSocket  -> 业务中间层（通用代码）
 *        |
 *   XNetwork.h (本文件)                        -> 平台抽象
 *   |-- windows/XNetwork_win32.c               -> Windows IOCP 实现
 *   |-- Posix/XNetwork_posix.c                 -> Linux io_uring 实现
 *   |-- lwip/XNetwork_lwip.c                   -> lwIP 回调驱动实现
 *
 * 代理协议（SOCKS5/HTTP）在 XNetworkProxy 模块中用通用代码实现
 */

#ifndef XNETWORK_H
#define XNETWORK_H

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

/** @brief 套接字平台描述符；值为 -1 时表示无效句柄。 */
typedef intptr_t XSocketHandle;

/** @brief TCP 服务器平台句柄；值为 -1 时表示无效句柄。 */
typedef intptr_t XServerHandle;

/** @brief 套接字传输类型；枚举值不可按位组合。 */
typedef enum {
    XNetwork_Tcp = 0,   /**< 面向连接的 TCP 套接字。 */
    XNetwork_Udp = 1    /**< 无连接的 UDP 数据报套接字。 */
} XNetworkSocketType;

/** @brief 平台本地字节流传输类型；仅供数据库等后端使用，不可按位组合。 */
typedef enum {
    XNetwork_LocalStream_Unknown = 0, /**< 未知或未指定的本地传输。 */
    XNetwork_LocalStream_UnixSocket,  /**< POSIX 域套接字。 */
    XNetwork_LocalStream_NamedPipe    /**< Windows 命名管道。 */
} XNetworkLocalStreamType;

/** @brief 地址族类型；复用 XHostAddress 的网络层协议枚举，枚举值不可按位组合。 */
typedef XHostAddress_NetworkLayerProtocol XNetworkProtocol;
#define XNetwork_IPv4  XHostAddress_IPv4Protocol /**< IPv4 地址族。 */
#define XNetwork_IPv6  XHostAddress_IPv6Protocol /**< IPv6 地址族。 */
#define XNetwork_Any   XHostAddress_AnyIPProtocol /**< 由平台选择可用地址族。 */

/* =========================================================================
 * 二、平台初始化与错误
 * ========================================================================= */

/**
 * @brief 确保网络子系统已初始化
 * @return 无。
 * @note 多次调用安全，内部有引用计数；与 XNetwork_cleanup 成对使用。
 */
void XNetwork_ensureInit(void);

/**
 * @brief 清理网络子系统
 * @return 无。
 * @note 与 XNetwork_ensureInit 配对使用，内部有引用计数。
 */
void XNetwork_cleanup(void);

/**
 * @brief 获取最后一次网络错误码
 * @return 当前线程最近一次平台网络调用的错误码；没有错误时返回 0。
 */
int XNetwork_lastError(void);

/**
 * @brief 将错误码转换为可读字符串
 * @param errorCode 平台错误码。
 * @return 新分配的 NUL 结尾错误描述；调用者必须使用 XFree_System 释放，分配失败返回 NULL。
 */
char* XNetwork_errorString(int errorCode);

/* =========================================================================
 * DNS
 * ========================================================================= */

/**
 * @brief 获取本机主机名
 * @return 新建的 XString 主机名；调用者必须使用 XString_delete_base 释放，失败返回 NULL。
 */
XString* XNetwork_localHostName(void);

/**
 * @brief 同步 DNS 查询
 * @param name 待解析的主机名；借用，不能为 NULL。
 * @return 新建的 XVector<XHostAddress> 地址列表；调用者必须使用 XVector_delete_base 释放，失败返回 NULL。
 */
XVector* XNetwork_lookupName(const XString* name);

/* =========================================================================
 * 三、套接字私有数据（由平台层管理异步IO状态）
 * ========================================================================= */

/**
 * @brief 平台套接字私有状态的公共前缀。
 * @details 由 XNetwork_createSocketPrivate 创建并由 XNetwork_deleteSocketPrivate 销毁。
 *          平台实现可在此公共前缀后附加私有字段；调用方不得按字段偏移访问附加状态。
 */
typedef struct XNetworkSocketPrivate {
    void*    owner;     /**< 借用的拥有者 XAbstractSocket 指针；由创建者提供，平台层不得释放。 */
    XVector* notifiers; /**< 平台拥有的 XVector<XSocketNotifier*>；销毁私有状态时一并释放。 */
} XNetworkSocketPrivate;

/**
 * @brief 创建套接字私有数据
 * @param owner 拥有者对象，通常为 XAbstractSocket；借用，平台层不会释放。
 * @return 新建的平台私有状态；调用者必须使用 XNetwork_deleteSocketPrivate 释放，失败返回 NULL。
 */
XNetworkSocketPrivate* XNetwork_createSocketPrivate(void* owner);

/**
 * @brief 销毁套接字私有数据
 * @param priv 平台私有状态；可为 NULL，此时不执行任何操作。调用后该指针失效。
 * @return 无。
 */
void XNetwork_deleteSocketPrivate(XNetworkSocketPrivate* priv);

/**
 * @brief 获取套接字描述符
 * @param priv 平台私有状态；借用，可为 NULL。
 * @return 套接字描述符，无效返回 -1
 */
intptr_t XNetwork_socketDescriptor(const XNetworkSocketPrivate* priv);

/**
 * @brief 检查套接字是否已连接
 * @param priv 平台私有状态；借用，可为 NULL。
 * @return 已连接返回 true
 */
bool XNetwork_socketIsConnected(const XNetworkSocketPrivate* priv);

/* =========================================================================
 * 四、核心操作（统一异步接口）
 * ========================================================================= */

/**
 * @brief 绑定套接字到指定地址和端口。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param address 绑定地址；借用，不能为 NULL。
 * @param port 请求绑定的端口；0 表示由平台自动分配。
 * @param reuseAddr 为 true 时请求地址重用，映射到平台的 SO_REUSEADDR 等语义。
 * @param shareAddr 为 true 时允许地址共享；Windows 上与 SO_EXCLUSIVEADDRUSE 的语义相反。
 * @param sockType 套接字传输类型；取值为 XNetworkSocketType。
 * @return 绑定成功返回实际端口；失败返回 0。
 * @note UDP 绑定成功后可自动启动异步读取；调用方仍须保持 priv 有效。
 */
uint16_t XNetwork_socketBind(XNetworkSocketPrivate* priv, const XHostAddress* address,
                              uint16_t port, bool reuseAddr, bool shareAddr,
                              XNetworkSocketType sockType);

/**
 * @brief 异步连接到远端主机。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param hostName 主机名或 IP 地址文本；借用，不能为 NULL。
 * @param port 目标端口。
 * @param protocol 地址族选择；取值为 XNetworkProtocol。
 * @param sockType 套接字传输类型；取值为 XNetworkSocketType。
 * @return 成功提交连接请求返回 true；参数错误或立即失败返回 false。
 * @note 连接完成或失败通过事件循环通知；返回 true 不表示已经建立连接。
 */
bool XNetwork_socketConnect(XNetworkSocketPrivate* priv, const XString* hostName,
                            uint16_t port, XNetworkProtocol protocol,
                            XNetworkSocketType sockType);

/**
 * @brief 建立平台特有的本地字节流连接。
 * @param priv 套接字私有数据；不能为 NULL。
 * @param endpoint Unix socket 路径或 Windows 命名管道名称；借用，不能为 NULL。
 * @param streamType 本地流传输类型。
 * @param timeoutMs 连接等待超时（毫秒），负数表示不限制。
 * @param sockType 套接字类型，目前只支持 XNetwork_Tcp。
 * @return 连接成功返回 true；当前平台或参数不支持返回 false。
 * @note 平台内部接口，不属于 XAbstractSocket 的公开 API。
 */
bool XNetwork_socketConnectLocal(XNetworkSocketPrivate* priv, const XString* endpoint,
                                 XNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XNetworkSocketType sockType);

/**
 * @brief 断开套接字连接并取消关联异步操作。
 * @param priv 平台私有状态；借用，可为 NULL，此时不执行任何操作。
 * @return 无。
 * @note 关闭成功后套接字状态变为未连接；调用方仍负责销毁 priv。
 */
void XNetwork_socketDisconnect(XNetworkSocketPrivate* priv);

/**
 * @brief 从异步接收缓冲区读取数据。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param buf 调用方提供的可写缓冲区；借用，len 大于 0 时不能为 NULL。
 * @param len 最大读取字节数；必须大于 0。
 * @param sockType 套接字传输类型；取值为 XNetworkSocketType。
 * @param ringBuffer 可选的外部接收环形缓冲区；借用，可为 NULL。
 * @return 实际读取字节数；没有可读数据返回 0，参数错误、连接失效或平台错误返回 -1。
 * @note 数据由异步 I/O 或协议栈回调填充；函数不会取得 buf 或 ringBuffer 的所有权。
 */
int64_t XNetwork_socketRead(XNetworkSocketPrivate* priv, void* buf, int64_t len,
                            XNetworkSocketType sockType, void* ringBuffer);

/**
 * @brief 向套接字异步提交待发送数据。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param buf 待发送字节缓冲区；借用，len 大于 0 时不能为 NULL。
 * @param len 待发送字节数；必须大于 0。
 * @param sockType 套接字传输类型；取值为 XNetworkSocketType。
 * @param destAddr UDP 目标地址；借用，TCP 传 NULL。
 * @param destPort UDP 目标端口；TCP 时忽略。
 * @param ringBuffer 可选的外部写入队列；借用，可为 NULL。
 * @return 已提交发送的字节数；无法提交返回 -1。
 * @note 返回成功仅表示数据已提交给平台或队列；实际发送结果通过 bytesWritten 等事件通知。
 */
int64_t XNetwork_socketWrite(XNetworkSocketPrivate* priv, const void* buf, int64_t len,
                             XNetworkSocketType sockType, const XHostAddress* destAddr,
                             uint16_t destPort, void* ringBuffer);

/**
 * @brief 处理事件循环投递的套接字平台事件。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param event 平台事件对象；借用，不能为 NULL，所有权仍归事件循环。
 * @return 事件已被当前套接字识别并处理返回 true；事件不匹配或处理失败返回 false。
 * @note 由事件循环调用，可处理 IOCP、io_uring、epoll 或 lwIP 回调转换后的事件。
 */
bool XNetwork_socketHandleEvent(XNetworkSocketPrivate* priv, void* event);

/**
 * @brief 将外部创建的套接字描述符接管到平台私有状态。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param fd 平台套接字描述符；成功接管后所有权转移给 priv。
 * @param state 初始 XAbstractSocket_SocketState 枚举值。
 * @param openMode 初始 XIODeviceBaseMode 枚举值。
 * @return 接管成功返回 true；参数无效或平台初始化失败返回 false，失败时 fd 所有权不转移。
 * @note 用于接受外部创建的套接字（如服务器 accept 的客户端）。fd 的具体
 *       表示由平台后端定义；lwIP 后端使用 XFd 索引，并转移其 Raw API
 *       tcp_pcb 所有权，不使用操作系统套接字 API。
 */
bool XNetwork_socketSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd,
                                  int state, int openMode);

/**
 * @brief 将已有监听套接字交给 TCP 服务器平台私有层。
 * @param priv TCP 服务器的平台私有状态；借用，不能为 NULL。
 * @param fd 已打开的监听套接字平台描述符；lwIP 后端为 XFd 索引，成功时所有权转移。
 * @return 接管成功返回 true；参数无效、重复接管或平台初始化失败返回 false。
 * @note TCP 服务器不是 XIODevice，不能复用 socketSetDescriptor 的 fd 表路径。
 *       接管成功后，fd 的所有权转移给平台私有数据。
 */
bool XNetwork_serverSetDescriptor(XNetworkSocketPrivate* priv, intptr_t fd);

/**
 * @brief 设置套接字选项。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param option XAbstractSocket_SocketOption 枚举值。
 * @param value 指向与 option 匹配的值的借用指针；不能为 NULL，函数不会保存该指针。
 * @return 平台接受并设置选项返回 true；不支持、参数无效或系统调用失败返回 false。
 */
bool XNetwork_socketSetOption(XNetworkSocketPrivate* priv, int option, const void* value);

/**
 * @brief 获取套接字选项。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param option XAbstractSocket_SocketOption 枚举值。
 * @return 指向平台临时或静态选项值的借用指针；不支持或失败返回 NULL。
 * @warning 返回存储可能在后续调用中被复用，调用方不得释放、修改或跨线程长期保存。
 */
void* XNetwork_socketGetOption(XNetworkSocketPrivate* priv, int option);

/**
 * @brief 设置套接字接收缓冲区的期望大小。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param size 缓冲区大小，单位为字节；-1 表示请求取消限制。
 * @return 无。
 * @note 平台可能调整实际大小或不支持取消限制；本接口不返回该调整后的值。
 */
void XNetwork_socketSetReadBufferSize(XNetworkSocketPrivate* priv, int64_t size);

/* =========================================================================
 * 五、异步读取状态（供事件处理使用）
 * ========================================================================= */

/**
 * @brief 获取平台异步读取缓冲区的首地址。
 * @param priv 平台私有状态；借用，可为 NULL。
 * @return 指向平台内部读取缓冲区的借用指针；priv 为 NULL 或无数据时返回 NULL。
 * @warning 返回地址在下一次读取、断开或销毁 priv 后可能失效，调用方不得释放或修改。
 */
const char* XNetwork_socketReadBuffer(const XNetworkSocketPrivate* priv);

/**
 * @brief 获取最近一次异步读取完成的字节数。
 * @param priv 平台私有状态；借用，可为 NULL。
 * @return 最近一次读取完成的字节数；priv 为 NULL 时返回 0。
 */
size_t XNetwork_socketReadFinishedBytes(const XNetworkSocketPrivate* priv);

/**
 * @brief 获取最近一次异步写入完成的字节数。
 * @param priv 平台私有状态；借用，可为 NULL。
 * @return 最近一次写入完成的字节数；priv 为 NULL 时返回 0。
 */
size_t XNetwork_socketWriteFinishedBytes(const XNetworkSocketPrivate* priv);

/**
 * @brief 在处理完当前数据后继续提交异步读取。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param isUdp 为 true 时按 UDP 语义读取，为 false 时按 TCP 语义读取。
 * @return 无。
 * @note 由读取事件处理完成后调用；未连接或平台回调模型不需要投递读操作时可不产生新请求。
 */
void XNetwork_socketContinueRead(XNetworkSocketPrivate* priv, bool isUdp);

/**
 * @brief 在写完成后继续提交环形缓冲区中的剩余数据。
 * @param priv 平台私有状态；借用，不能为 NULL。
 * @param ringBuffer 写入队列；借用，不能为 NULL。
 * @param isUdp 为 true 时按 UDP 语义继续发送，为 false 时按 TCP 语义继续发送。
 * @return 无。
 * @note 只能在上一次异步写完成后调用；调用方负责在整个异步写入期间保持 ringBuffer 有效。
 */
void XNetwork_socketContinueWrite(XNetworkSocketPrivate* priv, XRingBuffer* ringBuffer, bool isUdp);

/* =========================================================================
 * 六、TCP 服务器
 * ========================================================================= */

/**
 * @brief 创建并监听 TCP 服务器套接字。
 * @param priv TCP 服务器的平台私有状态；借用，不能为 NULL。
 * @param addr 绑定地址；借用，不能为 NULL。
 * @param port 请求监听端口；0 表示由平台自动分配。
 * @param backlog 监听队列最大长度；必须为正数。
 * @param reuseAddr 为 true 时请求地址重用。
 * @return 新建的服务器句柄；失败返回 -1。
 * @note 返回句柄由调用方管理，必须使用 XNetwork_serverClose 关闭。
 */
XServerHandle XNetwork_serverCreate(XNetworkSocketPrivate* priv, const XHostAddress* addr,
                                     uint16_t port, int backlog, bool reuseAddr);

/**
 * @brief 提交一次异步接受客户端连接请求。
 * @param priv TCP 服务器的平台私有状态；借用，不能为 NULL。
 * @return 成功提交或已有挂起请求返回 true；服务器未监听或平台调用失败返回 false。
 * @note 在 serverCreate 成功后由用户层调用，启动异步 Accept；
 *       每次处理完一个连接后再次调用以继续接受。接受结果通过事件通知。
 */
bool XNetwork_serverAccept(XNetworkSocketPrivate* priv);

/**
 * @brief 关闭 TCP 服务器并释放监听套接字。
 * @param priv TCP 服务器的平台私有状态；借用，可为 NULL。
 * @param server 由 XNetwork_serverCreate 返回的服务器句柄；-1 时不执行操作。
 * @return 无。
 * @note 调用后 server 失效，所有未完成的接受请求将被取消。
 */
void XNetwork_serverClose(XNetworkSocketPrivate* priv, XServerHandle server);

/**
 * @brief 获取服务器实际绑定的端口。
 * @param server 有效服务器句柄；借用。
 * @return 实际绑定端口；查询失败或句柄无效返回 0。
 */
uint16_t XNetwork_serverPort(XServerHandle server);

/**
 * @brief 取出最近一次异步接受完成的客户端套接字。
 * @param priv TCP 服务器的平台私有状态；借用，不能为 NULL。
 * @param clientAddr 可选的调用方地址输出存储；非 NULL 时成功后写入客户端地址。
 * @param clientPort 可选的调用方端口输出存储；非 NULL 时成功后写入客户端端口。
 * @return 新接管的客户端套接字句柄；当前没有已完成连接或失败时返回 -1。
 * @note 调用后客户端套接字从待处理队列移除，调用方负责将其交给 XNetwork_socketSetDescriptor 或直接关闭。
 */
XSocketHandle XNetwork_serverGetAcceptedSocket(XNetworkSocketPrivate* priv, XHostAddress* clientAddr, uint16_t* clientPort);

/* =========================================================================
 * 七、多播组（精简为 2 个 API）
 * ========================================================================= */

/** @brief 多播套接字参数操作类型；枚举值不可按位组合。 */
typedef enum {
    XMC_Join,       /**< 加入多播组。 */
    XMC_Leave,      /**< 离开多播组。 */
    XMC_SetIf,      /**< 设置多播接口，arg 指向 uint32_t 接口索引。 */
    XMC_GetIf,      /**< 获取多播接口，arg 指向 uint32_t 输出存储。 */
    XMC_SetTtl,     /**< 设置 TTL，arg 指向 int 值。 */
    XMC_GetTtl,     /**< 获取 TTL，arg 指向 int 输出存储。 */
    XMC_SetLoop,    /**< 设置多播回环，arg 指向 bool 值。 */
    XMC_GetLoop     /**< 获取多播回环，arg 指向 bool 输出存储。 */
} XMulticastOp;

/**
 * @brief 加入或离开 IPv4/IPv6 多播组。
 * @param sock 有效 UDP 套接字句柄；借用。
 * @param join 为 true 时加入，为 false 时离开。
 * @param groupAddress 多播组地址；借用，不能为 NULL。
 * @param ifIndex 出入接口索引；0 表示由平台选择默认接口。
 * @return 平台成功完成多播组操作返回 true；参数不支持或系统调用失败返回 false。
 */
bool XNetwork_multicastGroup(XSocketHandle sock, bool join,
                             const XHostAddress* groupAddress, uint32_t ifIndex);

/**
 * @brief 设置或查询多播接口、TTL 和回环参数。
 * @param sock 有效 UDP 套接字句柄；借用。
 * @param op 要执行的多播操作；取值为 XMulticastOp。
 * @param arg 与 op 对应的输入或输出存储；不能为 NULL，具体类型见 XMulticastOp 枚举值注释。
 * @return 操作成功返回 0；参数无效、操作不支持或系统调用失败返回 -1。
 */
int XNetwork_multicastOp(XSocketHandle sock, XMulticastOp op, void* arg);

/* =========================================================================
 * 八、UDP 特有
 * ========================================================================= */

/**
 * @brief 获取最近接收 UDP 数据报的发送者地址和端口。
 * @param priv UDP 套接字的平台私有状态；借用，不能为 NULL。
 * @param srcAddr 可选的调用方地址输出存储；非 NULL 时成功后写入发送者地址。
 * @param srcPort 可选的调用方端口输出存储；非 NULL 时成功后写入发送者端口。
 * @return 已保存发送者信息返回 true；当前没有数据报或参数无效返回 false。
 * @note 必须在读取该数据报后、开始下一次读取前调用。
 */
bool XNetwork_getLastDatagramSender(const XNetworkSocketPrivate* priv,
                                     XHostAddress* srcAddr, uint16_t* srcPort);

/**
 * @brief 以同步方式发送一个 UDP 数据报。
 * @param sock 有效 UDP 套接字句柄；借用。
 * @param data 待发送数据；借用，size 大于 0 时不能为 NULL。
 * @param size 待发送字节数；必须非负。
 * @param address 目标地址；借用，不能为 NULL。
 * @param port 目标端口。
 * @return 实际发送字节数；发送失败返回 -1。
 */
int64_t XNetwork_sendDatagram(XSocketHandle sock, const void* data, int64_t size,
                               const XHostAddress* address, uint16_t port);

/* =========================================================================
 * 九、系统代理与GSSAPI（平台相关）
 * ========================================================================= */

/**
 * @brief 获取系统代理配置
 * @param queryUrl 查询 URL；借用，可为 NULL，NULL 表示查询默认系统代理。
 * @param outProxy 调用方提供的已初始化代理配置；成功时函数写入结果，失败时不保证其内容。
 * @return 成功获得系统代理设置返回 true；平台不支持或查询失败返回 false。
 * @note Windows: WinHttpGetProxyForUrl/注册表; Linux: 环境变量; macOS: CFProxySupport
 */
bool XNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy);

/**
 * @brief GSSAPI认证处理
 * @param serviceName 服务名称，如 "HTTP@proxy.example.com"；借用，不能为 NULL。
 * @param inputToken 输入令牌；借用，可为 NULL，NULL 表示初始化协商。
 * @param outputToken 调用方提供的 XByteArray；成功时写入要发送的令牌，不能为 NULL。
 * @param context 输入输出上下文；首次调用时 *context 必须为 NULL，函数在协商期间写入内部状态。
 * @return 协商完成返回 0，需要继续交换令牌返回 1，失败返回 -1。
 * @note 协商进行中调用方必须原样保存 *context，不得释放或修改其指向的内部状态；
 *       调用返回 0 或 -1 后应以平台返回的 *context 值为准。
 */
int XNetwork_gssapiAuth(const XString* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_H */
