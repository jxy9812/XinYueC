// XAbstractSocket.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QAbstractSocket，继承自 XIODevice。
// 设计原则：
//   - 结构体首成员必须是 XIODevice（内存布局兼容）
//   - 所有继承自 XIODevice 的公共 API 通过 #define 符号重命名暴露（零 wrapper）
//   - 信号函数以 _signal 结尾，返回自身地址，支持 XSignal() 宏
//   - 参数传递策略：
//       • 0 参数：data = NULL
//       • 1 非指针参数：XVariant + XVariant_delete_base
//       • ≥2 参数：XVariantList + XVariantList_delete_base
//   - 使用 PIMPL 隐藏实现细节

#ifndef XABSTRACTSOCKET_H
#define XABSTRACTSOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XIODevice.h"
#include "XHostAddress.h"
#include "XVariant.h"
#include "XNetworkProxy.h"

// =============== 枚举定义（与 Qt6 语义一致）===============

/**
 * @brief 套接字传输类型。
 */
typedef enum XAbstractSocket_SocketType {
    XAbstractSocket_TcpSocket = 0,          ///< TCP 流套接字（可靠、面向连接）
    XAbstractSocket_UdpSocket = 1,          ///< UDP 数据报套接字（无连接、不可靠）
    XAbstractSocket_SctpSocket = 2,         ///< SCTP 套接字（保留，未实现）
    XAbstractSocket_UnknownSocketType = -1  ///< 未知或无效套接字类型
} XAbstractSocket_SocketType;

/**
 * @brief 网络层协议版本。
 */
typedef enum XAbstractSocket_NetworkLayerProtocol {
    XAbstractSocket_IPv4Protocol = XHostAddress_IPv4Protocol,          ///< IPv4 协议
    XAbstractSocket_IPv6Protocol = XHostAddress_IPv6Protocol,          ///< IPv6 协议
    XAbstractSocket_AnyIPProtocol = XHostAddress_AnyIPProtocol,        ///< 自动选择 IPv4/IPv6
    XAbstractSocket_UnknownNetworkLayerProtocol = XHostAddress_UnknownNetworkLayerProtocol  ///< 未知协议
} XAbstractSocket_NetworkLayerProtocol;

/**
 * @brief 套接字错误码。
 */
typedef enum XAbstractSocket_SocketError {
    XAbstractSocket_ConnectionRefusedError = 0,           ///< 连接被远程主机拒绝
    XAbstractSocket_RemoteHostClosedError = 1,            ///< 对端主动关闭连接
    XAbstractSocket_HostNotFoundError = 2,                ///< 主机名解析失败（DNS 错误）
    XAbstractSocket_SocketAccessError = 3,                ///< 权限不足（如绑定特权端口）
    XAbstractSocket_SocketResourceError = 4,              ///< 系统资源不足（如 fd 耗尽）
    XAbstractSocket_SocketTimeoutError = 5,               ///< 操作超时（connect/read/write）
    XAbstractSocket_DatagramTooLargeError = 6,            ///< UDP 数据报超过 MTU
    XAbstractSocket_NetworkError = 7,                     ///< 底层网络错误（如 ICMP 不可达）
    XAbstractSocket_AddressInUseError = 8,                ///< 地址已被使用（bind 失败）
    XAbstractSocket_SocketAddressNotAvailableError = 9,   ///< 本地地址不可用
    XAbstractSocket_UnsupportedSocketOperationError = 10, ///< 不支持的操作（如 UDP 调用 listen）
    XAbstractSocket_UnfinishedSocketOperationError = 11,  ///< 上一异步操作未完成
    XAbstractSocket_ProxyAuthenticationRequiredError = 12,///< 代理需要认证
    XAbstractSocket_SslHandshakeFailedError = 13,         ///< SSL/TLS 握手失败
    XAbstractSocket_ProxyConnectionRefusedError = 14,     ///< 代理连接被拒绝
    XAbstractSocket_ProxyConnectionClosedError = 15,      ///< 代理连接被关闭
    XAbstractSocket_ProxyConnectionTimeoutError = 16,     ///< 代理连接超时
    XAbstractSocket_ProxyNotFoundError = 17,              ///< 代理服务器未找到
    XAbstractSocket_ProxyProtocolError = 18,              ///< 代理协议错误
    XAbstractSocket_OperationError = 19,                  ///< 通用操作错误
    XAbstractSocket_SslInternalError = 20,                ///< SSL 内部库错误
    XAbstractSocket_SslInvalidUserDataError = 21,         ///< SSL 用户数据无效（如证书错误）
    XAbstractSocket_TemporaryError = 22,                  ///< 临时错误（可重试，如 EAGAIN）
    XAbstractSocket_UnknownSocketError = -1               ///< 未知错误
} XAbstractSocket_SocketError;

/**
 * @brief 套接字状态机。
 */
typedef enum XAbstractSocket_SocketState {
    XAbstractSocket_UnconnectedState = 0,  ///< 初始状态或已断开
    XAbstractSocket_HostLookupState = 1,   ///< 正在进行 DNS 主机名解析
    XAbstractSocket_ConnectingState = 2,   ///< 正在建立 TCP 连接
    XAbstractSocket_ConnectedState = 3,    ///< 已连接（TCP）或已绑定可通信（UDP）
    XAbstractSocket_BoundState = 4,        ///< 已绑定本地地址（仅 UDP）
    XAbstractSocket_ListeningState = 5,    ///< 监听传入连接（本类不支持，由子类实现）
    XAbstractSocket_ClosingState = 6       ///< 正在关闭连接
} XAbstractSocket_SocketState;

/**
 * @brief 套接字选项类型。
 */
typedef enum XAbstractSocket_SocketOption {
    XAbstractSocket_LowDelayOption,                ///< 启用 TCP_NODELAY（禁用 Nagle）
    XAbstractSocket_KeepAliveOption,               ///< 启用 SO_KEEPALIVE
    XAbstractSocket_MulticastTtlOption,            ///< 设置多播 TTL（跳数）
    XAbstractSocket_MulticastLoopbackOption,       ///< 控制多播回环（本地是否接收自己发的包）
    XAbstractSocket_TypeOfServiceOption,           ///< 设置 IP TOS 字段（QoS）
    XAbstractSocket_SendBufferSizeSocketOption,    ///< 设置发送缓冲区大小（SO_SNDBUF）
    XAbstractSocket_ReceiveBufferSizeSocketOption, ///< 设置接收缓冲区大小（SO_RCVBUF）
    XAbstractSocket_PathMtuSocketOption            ///< 获取路径 MTU（只读）
} XAbstractSocket_SocketOption;

/**
 * @brief 绑定行为标志。
 */
typedef enum XAbstractSocket_BindFlag {
    XAbstractSocket_DefaultForPlatform = 0x0,  ///< 使用平台默认行为
    XAbstractSocket_ShareAddress = 0x1,        ///< 允许多个套接字绑定同一地址（SO_REUSEADDR）
    XAbstractSocket_DontShareAddress = 0x2,    ///< 禁止地址共享
    XAbstractSocket_ReuseAddressHint = 0x4     ///< 提示重用 TIME_WAIT 状态的地址
} XAbstractSocket_BindFlag;

typedef int XAbstractSocket_BindMode; ///< 绑定模式位掩码（由 XAbstractSocket_BindFlag 组合）

/**
 * @brief SSL 错误处理暂停策略。
 */
typedef enum XAbstractSocket_PauseMode {
    XAbstractSocket_PauseNever = 0x0,          ///< 从不暂停，错误立即上报
    XAbstractSocket_PauseOnSslErrors = 0x1     ///< SSL 错误时暂停，等待 resume()
} XAbstractSocket_PauseMode;

typedef int XAbstractSocket_PauseModes; ///< 暂停模式位掩码

// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XAbstractSocket)
XCLASS_DEFINE_ENUM(XAbstractSocket, Resume) = XCLASS_VTABLE_GET_SIZE(XIODevice),
XCLASS_DEFINE_ENUM(XAbstractSocket, Bind),
XCLASS_DEFINE_ENUM(XAbstractSocket, ConnectToHost),
XCLASS_DEFINE_ENUM(XAbstractSocket, DisconnectFromHost),
XCLASS_DEFINE_ENUM(XAbstractSocket, SocketDescriptor),
XCLASS_DEFINE_ENUM(XAbstractSocket, SetSocketDescriptor),
XCLASS_DEFINE_ENUM(XAbstractSocket, SetSocketOption),
XCLASS_DEFINE_ENUM(XAbstractSocket, SocketOption),
XCLASS_DEFINE_ENUM(XAbstractSocket, SetReadBufferSize),
XCLASS_DEFINE_ENUM(XAbstractSocket, WaitForConnected),
XCLASS_DEFINE_ENUM(XAbstractSocket, WaitForDisconnected),
XCLASS_DEFINE_END(XAbstractSocket)

// =============== 前向声明 ===============
typedef struct XAbstractSocketPrivate XAbstractSocketPrivate;

// =============== 核心结构体 ===============

/**
 * @brief 抽象套接字基类，提供 TCP/UDP 通用接口。
 * @note 必须通过 XAbstractSocket_init() 初始化。
 * @note 首成员为 XIODevice，支持类型转换：(XIODevice*)sock。
 */
typedef struct XAbstractSocket {
    XIODevice base; ///< 继承 XIODevice（内存布局兼容）

    // ====== XAbstractSocket 特有状态字段 ======
    XAbstractSocket_SocketType socketType;          ///< 套接字类型（TCP/UDP）
    XAbstractSocket_SocketState state;              ///< 当前连接状态
    XAbstractSocket_SocketError error;              ///< 最近发生的错误码
    XString* errorString;                           ///< 错误描述字符串（UTF-8，可为 NULL）

    XHostAddress localAddress;                      ///< 本地绑定地址
    XHostAddress peerAddress;                       ///< 对端地址
    uint16_t localPort;                             ///< 本地绑定端口（主机字节序）
    uint16_t peerPort;                              ///< 对端端口（主机字节序）
    XString* peerName;                              ///< 对端主机名（connectToHost 时解析并缓存）

    XAbstractSocket_PauseModes m_pauseMode;         ///< SSL 错误暂停策略
    bool autoDeleteOnDisconnect;                    ///< 若为 true，disconnect 后自动 delete
    bool isValidFlag;                               ///< 是否处于有效状态（已连接且无致命错误）
    XString* protocolTag;                           ///< 协议标签（用于调试和日志）
    XNetworkProxy proxy;                            ///< 代理配置

    struct XAbstractSocketPrivate* d_ptr;           ///< 私有数据指针（PIMPL）
    int64_t readBufferSize;                         ///< 读缓冲区大小（字节），-1 表示无限制
} XAbstractSocket;

// =============== 继承自 XIODevice 的 API（符号重命名，无参数包装）===============

#define XAbstractSocket_open_base                XIODevice_open_base
#define XAbstractSocket_close_base               XIODevice_close_base
#define XAbstractSocket_read                     XIODevice_read
#define XAbstractSocket_write                    XIODevice_write
#define XAbstractSocket_readAll                  XIODevice_readAll
#define XAbstractSocket_readLine_base            XIODevice_readLine
#define XAbstractSocket_bytesAvailable_base      XIODevice_bytesAvailable_base
#define XAbstractSocket_bytesToWrite_base        XIODevice_bytesToWrite_base
#define XAbstractSocket_canReadLine_base         XIODevice_canReadLine_base
#define XAbstractSocket_waitForReadyRead_base    XIODevice_waitForReadyRead_base
#define XAbstractSocket_waitForBytesWritten_base XIODevice_waitForBytesWritten_base
#define XAbstractSocket_delete_base              XIODevice_deleteLater

#define XAbstractSocket_isOpen                   XIODevice_isOpen
#define XAbstractSocket_isReadable               XIODevice_isReadable
#define XAbstractSocket_isWritable               XIODevice_isWritable
#define XAbstractSocket_isSequential             XIODevice_isSequential
#define XAbstractSocket_atEnd_base               XIODevice_atEnd_base
#define XAbstractSocket_bytesAvailable_base      XIODevice_bytesAvailable_base
#define XAbstractSocket_bytesToWrite_base        XIODevice_bytesToWrite_base
#define XAbstractSocket_canReadLine_base         XIODevice_canReadLine_base
#define XAbstractSocket_waitForReadyRead_base    XIODevice_waitForReadyRead_base
#define XAbstractSocket_waitForBytesWritten      XIODevice_waitForBytesWritten

// =============== Getter 函数 ===============

/**
 * @brief 获取套接字类型。
 * @param sock 套接字实例（非 NULL）
 * @return 套接字类型（如 TCP 或 UDP）
 */
XAbstractSocket_SocketType XAbstractSocket_socketType(const XAbstractSocket* sock);

/**
 * @brief 获取当前连接状态。
 * @param sock 套接字实例（非 NULL）
 * @return 当前状态（如 ConnectedState、UnconnectedState 等）
 */
XAbstractSocket_SocketState XAbstractSocket_state(const XAbstractSocket* sock);

/**
 * @brief 获取最近发生的错误码。
 * @param sock 套接字实例（非 NULL）
 * @return 错误码（如 ConnectionRefusedError）
 */
XAbstractSocket_SocketError XAbstractSocket_error(const XAbstractSocket* sock);

/**
 * @brief 获取错误描述字符串。
 * @param sock 套接字实例（非 NULL）
 * @return UTF-8 编码的错误信息（可能为 NULL）
 */
XString* XAbstractSocket_errorString(const XAbstractSocket* sock);

/**
 * @brief 获取本地绑定地址。
 * @param sock 套接字实例（非 NULL）
 * @return 指向本地地址的常量指针
 */
const XHostAddress* XAbstractSocket_localAddress(const XAbstractSocket* sock);

/**
 * @brief 获取本地绑定端口。
 * @param sock 套接字实例（非 NULL）
 * @return 本地端口号（主机字节序）
 */
uint16_t XAbstractSocket_localPort(const XAbstractSocket* sock);

/**
 * @brief 获取对端地址。
 * @param sock 套接字实例（非 NULL）
 * @return 指向对端地址的常量指针
 */
const XHostAddress* XAbstractSocket_peerAddress(const XAbstractSocket* sock);

/**
 * @brief 获取对端端口。
 * @param sock 套接字实例（非 NULL）
 * @return 对端端口号（主机字节序）
 */
uint16_t XAbstractSocket_peerPort(const XAbstractSocket* sock);

/**
 * @brief 获取对端主机名。
 * @param sock 套接字实例（非 NULL）
 * @return 主机名字符串（可能为 NULL）
 */
XString* XAbstractSocket_peerName(const XAbstractSocket* sock);

/**
 * @brief 获取读缓冲区大小限制。
 * @param sock 套接字实例（非 NULL）
 * @return 缓冲区大小（字节），-1 表示无限制
 */
int64_t XAbstractSocket_readBufferSize(const XAbstractSocket* sock);

/**
 * @brief 获取当前暂停模式。
 * @param sock 套接字实例（非 NULL）
 * @return 暂停模式位掩码
 */
XAbstractSocket_PauseModes XAbstractSocket_pauseMode(const XAbstractSocket* sock);

/**
 * @brief 判断套接字是否处于有效状态。
 * @param sock 套接字实例（非 NULL）
 * @return true 表示已连接且无致命错误
 */
bool XAbstractSocket_isValid(const XAbstractSocket* sock);

/**
 * @brief 获取协议标签。
 * @param sock 套接字实例（非 NULL）
 * @return 协议标签字符串（可能为 NULL）
 */
XString* XAbstractSocket_protocolTag(const XAbstractSocket* sock);

// =============== Setter 函数 ===============

/**
 * @brief 设置读缓冲区大小。
 * @param sock 套接字实例（非 NULL）
 * @param size 缓冲区大小（字节），-1 表示无限制
 */
void XAbstractSocket_setReadBufferSize_base(XAbstractSocket* sock, int64_t size);

/**
 * @brief 设置 SSL 错误暂停模式。
 * @param sock 套接字实例（非 NULL）
 * @param mode 暂停模式位掩码
 */
void XAbstractSocket_setPauseMode(XAbstractSocket* sock, XAbstractSocket_PauseModes mode);

/**
 * @brief 设置协议标签（用于调试和日志）。
 * @param sock 套接字实例（非 NULL）
 * @param tag 协议标签字符串（如 "http", "mqtt" 等）
 */
void XAbstractSocket_setProtocolTag(XAbstractSocket* sock, const char* tag);

/**
 * @brief 设置套接字的代理配置。
 * @param sock 套接字实例（非 NULL）
 * @param proxy 代理配置
 */
void XAbstractSocket_setProxy(XAbstractSocket* sock, const XNetworkProxy* proxy);

/**
 * @brief 获取套接字的代理配置。
 * @param sock 套接字实例（非 NULL）
 * @return 指向代理配置的指针（不应被释放）
 */
XNetworkProxy* XAbstractSocket_proxy(const XAbstractSocket* sock);

// =============== 核心操作函数 ===============

/**
 * @brief 绑定到指定本地地址和端口。
 * @param sock 套接字实例（非 NULL）
 * @param address 本地地址（可为 IPv4/IPv6/Any）
 * @param port 本地端口（主机字节序）
 * @param mode 绑定标志组合（如 ShareAddress）
 * @return 成功返回 true，否则 false
 */
bool XAbstractSocket_bind_base(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode);

/**
 * @brief 绑定到任意本地地址（INADDR_ANY / in6addr_any）。
 * @param sock 套接字实例（非 NULL）
 * @param port 本地端口（主机字节序）
 * @param mode 绑定标志组合
 * @return 成功返回 true，否则 false
 */
bool XAbstractSocket_bindAny(XAbstractSocket* sock, uint16_t port, XAbstractSocket_BindMode mode);

/**
 * @brief 异步连接到指定主机名和端口。
 * @param sock 套接字实例（非 NULL）
 * @param hostName 远程主机名（将触发 DNS 解析）
 * @param port 远程端口（主机字节序）
 * @param mode 打开模式（如 ReadWrite）
 * @param protocol 网络协议（IPv4/IPv6/Any）
 */
void XAbstractSocket_connectToHost_base(XAbstractSocket* sock, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol);
/**
 * @brief 正常断开连接（优雅关闭）。
 * @param sock 套接字实例（非 NULL）
 */
void XAbstractSocket_disconnectFromHost_base(XAbstractSocket* sock);

/**
 * @brief 立即中止连接（不等待缓冲区清空）。
 * @param sock 套接字实例（非 NULL）
 */
void XAbstractSocket_abort(XAbstractSocket* sock);

/**
 * @brief 恢复因 SSL 错误而暂停的操作。
 * @param sock 套接字实例（非 NULL）
 */
void XAbstractSocket_resume_base(XAbstractSocket* sock);

// =============== 套接字选项 ===============

/**
 * @brief 设置套接字选项。
 * @param sock 套接字实例（非 NULL）
 * @param option 选项类型
 * @param value 指向选项值的指针
 * @param valueSize 值的大小（字节）
 */
void XAbstractSocket_setSocketOption_base(XAbstractSocket* sock, XAbstractSocket_SocketOption option, const XVariant* value);

/**
 * @brief 获取套接字选项值。
 * @param sock 套接字实例（非 NULL）
 * @param option 选项类型
 * @return 指向动态分配值的指针（调用者需 free）
 */
XVariant* XAbstractSocket_socketOption_base(XAbstractSocket* sock, XAbstractSocket_SocketOption option);

// =============== 平台句柄 ===============

/**
 * @brief 获取底层平台套接字描述符。
 * @param sock 套接字实例（非 NULL）
 * @return 描述符（POSIX: int, Windows: SOCKET），无效时返回 -1
 */
intptr_t XAbstractSocket_socketDescriptor_base(const XAbstractSocket* sock);

/**
 * @brief 设置底层平台套接字描述符。
 * @param sock 套接字实例（非 NULL）
 * @param socketDescriptor 平台描述符
 * @param state 初始状态
 * @param openMode 打开模式
 * @return 成功返回 true
 */
bool XAbstractSocket_setSocketDescriptor_base(XAbstractSocket* sock, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode);

// =============== 同步等待函数 ===============

/**
 * @brief 等待连接成功。
 * @param sock 套接字实例（非 NULL）
 * @param msecs 超时毫秒数（-1 表示无限等待）
 * @return 成功连接返回 true
 */
bool XAbstractSocket_waitForConnected_base(XAbstractSocket* sock, int msecs);

/**
 * @brief 等待连接断开。
 * @param sock 套接字实例（非 NULL）
 * @param msecs 超时毫秒数
 * @return 成功断开返回 true
 */
bool XAbstractSocket_waitForDisconnected_base(XAbstractSocket* sock, int msecs);

/**
 * @brief 等待所有待写数据发送完毕。
 * @param sock 套接字实例（非 NULL）
 * @param msecs 超时毫秒数
 * @return 成功刷新返回 true
 */
bool XAbstractSocket_waitForBytesWritten(XAbstractSocket* sock, int msecs);

// =============== 其他操作 ===============

/**
 * @brief 刷新输出缓冲区。
 * @param sock 套接字实例（非 NULL）
 * @return 成功返回 true
 */
bool XAbstractSocket_flush(XAbstractSocket* sock);

// =============== 受保护的 setter（供子类使用）==============

void XAbstractSocket_setLocalPort(XAbstractSocket* sock, uint16_t port);
void XAbstractSocket_setLocalAddress(XAbstractSocket* sock, const XHostAddress* address);
void XAbstractSocket_setPeerPort(XAbstractSocket* sock, uint16_t port);
void XAbstractSocket_setPeerAddress(XAbstractSocket* sock, const XHostAddress* address);
void XAbstractSocket_setPeerName(XAbstractSocket* sock, const char* name);
void XAbstractSocket_setSocketState(XAbstractSocket* sock, XAbstractSocket_SocketState state);
void XAbstractSocket_setSocketError(XAbstractSocket* sock, XAbstractSocket_SocketError error, const char* str);

// =============== XAbstractSocket 特有 API ===============

/**
 * @brief 初始化一个已分配的 XAbstractSocket 结构体。
 * @param sock 指向未初始化的 XAbstractSocket 实例（必须非 NULL）
 * @param type 套接字类型（如 TcpSocket）
 * @note 调用后可通过 XAbstractSocket_delete_base() 安全析构
 * @warning 不要对已初始化的实例重复调用
 */
void XAbstractSocket_init(XAbstractSocket* sock, XAbstractSocket_SocketType type);

/**
 * @brief 获取 XAbstractSocket 类的虚函数表。
 * @return 指向 XVtable 的指针
 */
XVtable* XAbstractSocket_class_init(void);

// ----------------- 信号函数（带 _signal 后缀，用于 XSignal() 宏）-----------------

/**
 * @brief 信号：主机名解析成功。
 * @param sock 套接字实例（可为 NULL，此时不 emit）
 * @return 函数指针自身（用于 XSignal 宏）
 * @emits 当 DNS 查询完成且成功时触发
 */
void* XAbstractSocket_hostFound_signal(XAbstractSocket* sock);

/**
 * @brief 信号：连接成功建立。
 * @param sock 套接字实例（可为 NULL，此时不 emit）
 * @return 函数指针自身
 * @emits 当 TCP 连接成功或 UDP 绑定完成后触发
 */
void* XAbstractSocket_connected_signal(XAbstractSocket* sock);

/**
 * @brief 信号：连接已断开。
 * @param sock 套接字实例（可为 NULL，此时不 emit）
 * @return 函数指针自身
 * @emits 当连接被对端关闭、本地 abort 或正常 disconnect 时触发
 */
void* XAbstractSocket_disconnected_signal(XAbstractSocket* sock);

/**
 * @brief 信号：套接字状态发生变更。
 * @param sock 套接字实例（可为 NULL，此时不 emit）
 * @param state 新状态
 * @return 函数指针自身
 * @emits 每当 state 字段变化时触发（包括中间状态如 HostLookup）
 */
void* XAbstractSocket_stateChanged_signal(XAbstractSocket* sock, XAbstractSocket_SocketState state);

/**
 * @brief 信号：发生套接字错误。
 * @param sock 套接字实例（可为 NULL，此时不 emit）
 * @param error 错误码
 * @return 函数指针自身
 * @emits 当任何网络操作失败时触发（error 字段已更新）
 */
void* XAbstractSocket_errorOccurred_signal(XAbstractSocket* sock, XAbstractSocket_SocketError error);

#ifdef __cplusplus
}
#endif
#endif // XABSTRACTSOCKET_H