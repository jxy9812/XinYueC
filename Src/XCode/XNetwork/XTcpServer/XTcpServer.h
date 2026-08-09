// XTcpServer.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QTcpServer，继承自 XObject。
// 提供 TCP 服务器功能，接受传入的 TCP 连接。

#ifndef XTCPSERVER_H
#define XTCPSERVER_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_TCPSERVER_ON

#include "XObject.h"
#include "XHostAddress.h"
#include "XAbstractSocket.h"
#include "XNetworkProxy.h"
#include "XSocketDescriptor.h"
#include "XVector.h"
#include <stdint.h>
#include <stdbool.h>

// =============== 前向声明 ===============
typedef struct XTcpSocket XTcpSocket;

/**
 * @brief 为新接受的连接创建套接字对象。
 * @param context 创建器私有上下文；生命周期由调用方负责。
 * @return 新建且尚未绑定描述符的 XTcpSocket 派生对象；失败返回 NULL。
 * @note 创建器仅在监听器接收到连接时调用。返回 XSslSocket 等派生对象可在
 *       描述符首次绑定前完成其专用配置，避免跨对象转移套接字所有权。
 */
typedef XTcpSocket* (*XTcpServer_IncomingSocketFactory)(void* context);

// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XTcpServer)
XCLASS_DEFINE_ENUM(XTcpServer, HasPendingConnections) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTcpServer, NextPendingConnection),
XCLASS_DEFINE_ENUM(XTcpServer, IncomingConnection),
XCLASS_DEFINE_END(XTcpServer)

// =============== 核心结构体 ===============

/**
 * @brief TCP 服务器类，继承自 XObject。
 * @note 接受传入的 TCP 连接，类似 Qt 的 QTcpServer。
 */
typedef struct XTcpServer {
    XObject base;                              ///< 继承 XObject
    void* d_ptr;                               ///< XNetworkSocketPrivate*（服务器套接字平台私有数据）
    XHostAddress serverAddress;                ///< 服务器监听地址
    uint16_t serverPort;                       ///< 服务器监听端口
    bool listening;                            ///< 是否正在监听
    bool pauseAccepting;                       ///< 是否暂停接受连接
    XVector* pendingConnections;               ///< 待处理连接队列（XTcpSocket* 列表）
    int maxPendingConnections;                 ///< 最大待处理连接数（默认30）
    int listenBacklogSize;                     ///< 监听积压大小（默认50）
    XAbstractSocket_SocketError lastError;     ///< 最后一次错误代码
    XString* errorString;                      ///< 错误字符串
    XNetworkProxy proxy;                       ///< 代理配置
    XTcpServer_IncomingSocketFactory incomingSocketFactory; ///< 接受连接对象创建器
    void* incomingSocketFactoryContext;        ///< 创建器私有上下文（不拥有）
    /* 临时存储最近接受的连接信息（供 incomingConnection_base 使用）*/
    XHostAddress lastAcceptedAddr;             ///< 最近接受的客户端地址
    uint16_t lastAcceptedPort;                 ///< 最近接受的客户端端口
} XTcpServer;

// =============== 构造与析构 ===============

/**
 * @brief 初始化已分配的 XTcpServer 结构体。
 * @param server 指向未初始化的 XTcpServer 实例
 */
void XTcpServer_init(XTcpServer* server);

/**
 * @brief 创建 XTcpServer 实例。
 * @return 新分配的实例，需调用 XTcpServer_delete_base() 释放
 */
XTcpServer* XTcpServer_create(void);

/**
 * @brief 延迟销毁 XTcpServer 实例。
 * @param server 指向要销毁的 XTcpServer 实例
 */
#define XTcpServer_deleteLater XObject_deleteLater

/**
 * @brief 初始化虚函数表。
 * @return 虚函数表指针
 */
XVtable* XTcpServer_class_init(void);

// =============== 核心功能 ===============

/**
 * @brief 开始监听连接。
 * @param server 服务器实例
 * @param address 监听地址（NULL 或 XHostAddress_AnySpecial 表示所有接口）
 * @param port 监听端口（0表示自动分配）
 * @return 成功返回 true
 */
bool XTcpServer_listen(XTcpServer* server, const XHostAddress* address, uint16_t port);

/**
 * @brief 关闭服务器，停止监听。
 * @param server 服务器实例
 */
void XTcpServer_close(XTcpServer* server);

/**
 * @brief 是否正在监听。
 * @param server 服务器实例
 * @return 正在监听返回 true
 */
bool XTcpServer_isListening(const XTcpServer* server);

// =============== 连接管理 ===============

/**
 * @brief 设置最大待处理连接数。
 * @param server 服务器实例
 * @param numConnections 最大连接数（默认30）
 */
void XTcpServer_setMaxPendingConnections(XTcpServer* server, int numConnections);

/**
 * @brief 获取最大待处理连接数。
 * @param server 服务器实例
 * @return 最大连接数
 */
int XTcpServer_maxPendingConnections(const XTcpServer* server);

/**
 * @brief 设置接受连接时使用的套接字对象创建器。
 * @param server 服务器实例。
 * @param factory 创建器；NULL 时恢复默认 XTcpSocket。
 * @param context 传给创建器的私有上下文，服务器不拥有也不释放。
 * @note 必须在 listen() 前设置；正在监听时调用会被忽略。
 */
void XTcpServer_setIncomingSocketFactory(XTcpServer* server,
                                         XTcpServer_IncomingSocketFactory factory,
                                         void* context);

/**
 * @brief 设置监听积压大小。
 * @param server 服务器实例
 * @param size 积压大小（默认50）
 * @note 必须在 listen() 之前调用
 */
void XTcpServer_setListenBacklogSize(XTcpServer* server, int size);

/**
 * @brief 获取监听积压大小。
 * @param server 服务器实例
 * @return 积压大小
 */
int XTcpServer_listenBacklogSize(const XTcpServer* server);

/**
 * @brief 获取服务器端口。
 * @param server 服务器实例
 * @return 服务器端口，未监听返回 0
 */
uint16_t XTcpServer_serverPort(const XTcpServer* server);

/**
 * @brief 获取服务器地址。
 * @param server 服务器实例
 * @return 服务器地址，未监听返回 XHostAddress_Null
 */
const XHostAddress* XTcpServer_serverAddress(const XTcpServer* server);

// =============== 套接字描述符 ===============

/**
 * @brief 获取套接字描述符。
 * @param server 服务器实例
 * @return 套接字描述符，未监听返回 -1
 */
intptr_t XTcpServer_socketDescriptor(const XTcpServer* server);

/**
 * @brief 设置套接字描述符（假设已在监听状态）。
 * @param server 服务器实例
 * @param socketDescriptor 套接字描述符
 * @return 成功返回 true
 */
bool XTcpServer_setSocketDescriptor(XTcpServer* server, intptr_t socketDescriptor);

// =============== 连接处理 ===============

/**
 * @brief 等待新连接（阻塞）。
 * @param server 服务器实例
 * @param msec 等待时间（毫秒），0表示非阻塞，-1表示无限等待
 * @param timedOut 输出是否超时（可为NULL）
 * @return 有连接可用返回 true
 */
bool XTcpServer_waitForNewConnection(XTcpServer* server, int msec, bool* timedOut);

/**
 * @brief 是否有待处理连接（虚函数，可重写）。
 * @param server 服务器实例
 * @return 有待处理连接返回 true
 */
bool XTcpServer_hasPendingConnections_base(const XTcpServer* server);

/**
 * @brief 获取下一个待处理连接（虚函数，可重写）。
 * @param server 服务器实例
 * @return 新的已连接 XTcpSocket，失败返回 NULL
 * @note 返回的套接字是服务器的子对象，服务器销毁时自动释放
 */
XTcpSocket* XTcpServer_nextPendingConnection_base(XTcpServer* server);

// =============== 错误处理 ===============

/**
 * @brief 获取服务器错误。
 * @param server 服务器实例
 * @return 错误代码
 */
XAbstractSocket_SocketError XTcpServer_serverError(const XTcpServer* server);

/**
 * @brief 获取错误字符串。
 * @param server 服务器实例
 * @return 错误字符串（返回新分配的字符串，需调用者释放）
 */
char* XTcpServer_errorString(const XTcpServer* server);

// =============== 接受控制（非虚函数）===============

/**
 * @brief 暂停接受连接。
 * @param server 服务器实例
 * @note 已排队的连接仍保留在队列中
 */
void XTcpServer_pauseAccepting(XTcpServer* server);

/**
 * @brief 恢复接受连接。
 * @param server 服务器实例
 */
void XTcpServer_resumeAccepting(XTcpServer* server);

// =============== 代理设置 ===============

/**
 * @brief 设置代理。
 * @param server 服务器实例
 * @param proxy 代理配置
 */
void XTcpServer_setProxy(XTcpServer* server, const XNetworkProxy* proxy);

/**
 * @brief 获取代理配置。
 * @param server 服务器实例
 * @return 代理配置指针（不应被释放）
 */
XNetworkProxy* XTcpServer_proxy(const XTcpServer* server);

// =============== 受保护的虚函数 ===============

/**
 * @brief 处理传入连接（虚函数，可重写）。
 * @param server 服务器实例
 * @param handle 新连接的套接字描述符
 * @note 基类实现创建 XTcpSocket，设置描述符，添加到待处理队列，发射 newConnection 信号
 */
void XTcpServer_incomingConnection_base(XTcpServer* server, intptr_t handle);

/**
 * @brief 添加待处理连接。
 * @param server 服务器实例
 * @param socket 已连接的 TCP 套接字
 * @note 添加后会发射 pendingConnectionAvailable 信号
 */
void XTcpServer_addPendingConnection(XTcpServer* server, XTcpSocket* socket);

// =============== 信号函数 ===============

/**
 * @brief 新连接信号。
 * @param server 服务器实例
 * @return 信号发送结果
 */
void* XTcpServer_newConnection_signal(XTcpServer* server);

/**
 * @brief 待处理连接可用信号。
 * @param server 服务器实例
 * @return 信号发送结果
 */
void* XTcpServer_pendingConnectionAvailable_signal(XTcpServer* server);

/**
 * @brief 接受错误信号。
 * @param server 服务器实例
 * @param socketError 错误代码
 * @return 信号发送结果
 */
void* XTcpServer_acceptError_signal(XTcpServer* server, XAbstractSocket_SocketError socketError);

// =============== 继承自 XObject 的 API（符号重命名）===============

// 生命周期
#define XTcpServer_deinitLater           XObject_deinitLater

// 对象信息
#define XTcpServer_objectName            XObject_objectName
#define XTcpServer_setObjectName         XObject_setObjectName

// 线程亲和性
#define XTcpServer_thread                XObject_thread
#define XTcpServer_moveToThread          XObject_moveToThread

// 父子关系
#define XTcpServer_parent                XObject_parent
#define XTcpServer_setParent             XObject_setParent
#define XTcpServer_children              XObject_children

// 信号槽
#define XTcpServer_connect               XObject_connect
#define XTcpServer_disconnect            XObject_disconnect

#endif // XNETWORK_TCPSERVER_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif

#endif // XTCPSERVER_H
