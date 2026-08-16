#ifndef XMQTTTCPSERVER_H
#define XMQTTTCPSERVER_H

#include "XMqttServer.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XHostAddress.h"
#include "XMap.h"
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttTcpServer.h
 * @brief 基于 TCP 的 MQTT 服务器（Broker）。
 * @details 继承 XMqttServer（传输无关的 MQTT 协议引擎），通过 XTcpServer /
 *          XTcpSocket 提供 TCP 传输通道：
 *          - listen() 监听指定地址/端口，接受多个客户端并发连接；
 *          - 每个客户端连接对应一个 XTcpSocket，收到数据后转交
 *            XMqttServer_feedData 解析，协议引擎通过重写后的
 *            XMqttServer_sendData_base / XMqttServer_closeClient_base
 *            下发报文与主动断开连接；
 *          - 连接断开自动调用 XMqttServer_endClient 清理协议状态并触发
 *            clientDisconnected 信号。
 *
 *          协议功能（CONNECT/CONNACK、PUBLISH QoS0/1/2、SUBSCRIBE、
 *          保留消息、遗嘱、持久会话、MQTT 5.0 属性等）全部由基类
 *          XMqttServer 实现，本类只负责 TCP 传输层。
 */

// ==================== 虚函数表定义（仅继承父类） ====================
XCLASS_DEFINE_BEGING(XMqttTcpServer)
XCLASS_DEFINE_EXTEND_END(XMqttTcpServer, XMqttServer)

/**
 * @brief MQTT TCP 服务器结构体。
 * @details 继承自 XMqttServer。m_tcpServer 负责监听与接受连接；
 *          m_connectedClients 保存每个在线客户端（XTcpSocket* -> uint8_t
 *          在线标记），键为借用指针，由 m_tcpServer 生命周期管理。
 *          报文缓冲由基类协议引擎内部的客户端输入缓冲完成，本层不再
 *          重复缓冲。
 */
typedef struct XMqttTcpServer {
    XMqttServer m_base;              ///< 基类（MQTT 协议引擎），第一个成员，由 XClass 管理。
    XTcpServer* m_tcpServer;         ///< TCP 监听服务器（本类拥有）。
    XMap* m_connectedClients;        ///< 客户端映射：XTcpSocket* -> uint8_t（在线标记）。
} XMqttTcpServer;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化 XMqttTcpServer 的虚函数表。
 * @return 初始化完成的虚函数表指针，失败返回 NULL。
 */
XVtable* XMqttTcpServer_class_init(void);

/**
 * @brief 在堆上创建并初始化 XMqttTcpServer 实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）。
 * @return 新创建的实例指针，失败返回 NULL；调用者负责调用 XClass_delete_base 释放。
 */
XMqttTcpServer* XMqttTcpServer_create_ex(XMemoryType memory);

/**
 * @brief 初始化已分配的 XMqttTcpServer 实例。
 * @param server 待初始化的实例指针（非 NULL）。
 * @note 栈上使用必须与 XClass_deinit_base 成对调用。
 */
void XMqttTcpServer_init(XMqttTcpServer* server);

/******************************************************************************************
 * TCP 服务器控制接口
 ******************************************************************************************/

/**
 * @brief 开始监听指定地址与端口。
 * @details 监听成功后，新连接会进入待处理队列并触发 newConnection 信号，
 *          由本类内部槽函数自动接受并登记为 MQTT 客户端。监听前调用
 *          XMqttServer_setXxx 系列配置生效。
 * @param server 服务器实例指针（非 NULL）。
 * @param address 监听地址；NULL 或 XHostAddress_AnySpecial 表示所有接口。
 * @param port 监听端口（1..65535）。
 * @return 监听成功返回 true，失败返回 false。
 */
bool XMqttTcpServer_listen(XMqttTcpServer* server, const XHostAddress* address, uint16_t port);

/**
 * @brief 停止监听并断开全部客户端连接。
 * @details 断开每个客户端会触发 XMqttServer_clientDisconnected_signal；
 *          协议引擎按 MQTT 规则处理遗嘱与持久会话。
 * @param server 服务器实例指针（非 NULL）。
 */
void XMqttTcpServer_close(XMqttTcpServer* server);

/**
 * @brief 查询服务器是否正在监听。
 * @param server 服务器实例指针。
 * @return 正在监听返回 true，server 为 NULL 时返回 false。
 */
bool XMqttTcpServer_isListening(const XMqttTcpServer* server);

/**
 * @brief 获取服务器监听端口。
 * @param server 服务器实例指针。
 * @return 监听端口，未监听或 server 为 NULL 时返回 0。
 */
uint16_t XMqttTcpServer_serverPort(const XMqttTcpServer* server);

/**
 * @brief 获取当前在线客户端数量。
 * @param server 服务器实例指针。
 * @return 在线客户端数量，server 为 NULL 时返回 0。
 */
int XMqttTcpServer_connectedClientCount(const XMqttTcpServer* server);

/******************************************************************************************
 * 继承自父类的 API（宏转发）
 ******************************************************************************************/

#define XMqttTcpServer_publish                  XMqttServer_publish
#define XMqttTcpServer_publishWithProperties    XMqttServer_publishWithProperties
#define XMqttTcpServer_setAuthenticator         XMqttServer_setAuthenticator
#define XMqttTcpServer_setMaximumPacketSize     XMqttServer_setMaximumPacketSize
#define XMqttTcpServer_maximumPacketSize        XMqttServer_maximumPacketSize
#define XMqttTcpServer_setTopicAliasMaximum     XMqttServer_setTopicAliasMaximum
#define XMqttTcpServer_topicAliasMaximum        XMqttServer_topicAliasMaximum
#define XMqttTcpServer_setServerKeepAlive       XMqttServer_setServerKeepAlive
#define XMqttTcpServer_serverKeepAlive          XMqttServer_serverKeepAlive
#define XMqttTcpServer_setMaximumQoS            XMqttServer_setMaximumQoS
#define XMqttTcpServer_maximumQoS               XMqttServer_maximumQoS
#define XMqttTcpServer_setRetainAvailable       XMqttServer_setRetainAvailable
#define XMqttTcpServer_retainAvailable          XMqttServer_retainAvailable
#define XMqttTcpServer_setWildcardAvailable     XMqttServer_setWildcardAvailable
#define XMqttTcpServer_wildcardAvailable        XMqttServer_wildcardAvailable
#define XMqttTcpServer_setSubscriptionIdAvailable XMqttServer_setSubscriptionIdAvailable
#define XMqttTcpServer_subscriptionIdAvailable  XMqttServer_subscriptionIdAvailable
#define XMqttTcpServer_setSharedAvailable       XMqttServer_setSharedAvailable
#define XMqttTcpServer_sharedAvailable          XMqttServer_sharedAvailable
#define XMqttTcpServer_clientConnected_signal   XMqttServer_clientConnected_signal
#define XMqttTcpServer_clientDisconnected_signal XMqttServer_clientDisconnected_signal
#define XMqttTcpServer_messageReceived_signal   XMqttServer_messageReceived_signal

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XMqttTcpServer_deinitLater   XMqttServer_deinitLater
#define XMqttTcpServer_deleteLater   XMqttServer_deleteLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttTcpServer_create
#define XMqttTcpServer_create() XMqttTcpServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XMQTTTCPSERVER_H */
