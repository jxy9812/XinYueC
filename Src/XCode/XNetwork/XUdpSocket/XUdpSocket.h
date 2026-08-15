// XUdpSocket.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QUdpSocket，继承自 XAbstractSocket。
// 提供 UDP 数据报通信功能。

#ifndef XUDPSOCKET_H
#define XUDPSOCKET_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_UDPSOCKET_ON

#include "XAbstractSocket.h"
#include "XNetworkDatagram.h"

// =============== 前向声明 ===============
typedef struct XNetworkInterface XNetworkInterface;
// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XUdpSocket)
XCLASS_DEFINE_EXTEND_END(XUdpSocket, XAbstractSocket)
// =============== 核心结构体 ===============

/**
 * @brief UDP 套接字类，继承自 XAbstractSocket。
 * @note 自动设置 socketType 为 UdpSocket。
 */
typedef struct XUdpSocket {
    XAbstractSocket base;  ///< 继承 XAbstractSocket
} XUdpSocket;

// =============== 构造与析构 ===============

/**
 * @brief 初始化已分配的 XUdpSocket 结构体。
 * @param sock 指向未初始化的 XUdpSocket 实例
 */
void XUdpSocket_init(XUdpSocket* sock);

/**
 * @brief 创建 XUdpSocket 实例。
 * @return 新分配的实例，需调用 XUdpSocket_delete_base() 释放
 */
XUdpSocket* XUdpSocket_create_ex(XMemoryType memory);

#define XUdpSocket_deleteLater XIODevice_deleteLater

/**
 * @brief 初始化虚函数表。
 * @return 虚函数表指针
 */
XVtable* XUdpSocket_class_init(void);

// =============== 继承自 XAbstractSocket 的 API（符号重命名）===============

// 生命周期
#define XUdpSocket_deinitLater            XAbstractSocket_deinitLater

// 打开/关闭
#define XUdpSocket_open_base               XIODevice_open_base
#define XUdpSocket_close_base              XIODevice_close_base
#define XUdpSocket_isOpen                  XIODevice_isOpen

// 读写操作（用于 connectToHost 后的连接模式）
#define XUdpSocket_read_1                  XIODevice_read_1
#define XUdpSocket_write_1                 XIODevice_write_1
#define XUdpSocket_readAll                 XIODevice_readAll_3
#define XUdpSocket_readLine_1              XIODevice_readLine_1
#define XUdpSocket_peek_1                  XIODevice_peek_1

// 状态查询
#define XUdpSocket_isReadable              XIODevice_isReadable
#define XUdpSocket_isWritable              XIODevice_isWritable
#define XUdpSocket_isSequential            XIODevice_isSequential
#define XUdpSocket_atEnd_base              XIODevice_atEnd_base
#define XUdpSocket_bytesAvailable_base     XIODevice_bytesAvailable_base
#define XUdpSocket_bytesToWrite_base       XIODevice_bytesToWrite_base
#define XUdpSocket_canReadLine_base        XIODevice_canReadLine_base

// 等待函数
#define XUdpSocket_waitForReadyRead_base   XIODevice_waitForReadyRead_base
#define XUdpSocket_waitForBytesWritten     XIODevice_waitForBytesWritten
#define XUdpSocket_waitForConnected_base   XAbstractSocket_waitForConnected_base
#define XUdpSocket_waitForDisconnected_base XAbstractSocket_waitForDisconnected_base

// Socket 状态
#define XUdpSocket_state                   XAbstractSocket_state
#define XUdpSocket_error                   XAbstractSocket_error
#define XUdpSocket_errorString             XAbstractSocket_errorString
#define XUdpSocket_isValid                 XAbstractSocket_isValid

// Socket 信息
#define XUdpSocket_socketType              XAbstractSocket_socketType
#define XUdpSocket_localAddress            XAbstractSocket_localAddress
#define XUdpSocket_localPort               XAbstractSocket_localPort
#define XUdpSocket_peerAddress             XAbstractSocket_peerAddress
#define XUdpSocket_peerPort                XAbstractSocket_peerPort
#define XUdpSocket_peerName                XAbstractSocket_peerName
#define XUdpSocket_socketDescriptor_base    XAbstractSocket_socketDescriptor_base

// Socket 选项
#define XUdpSocket_setSocketOption_base    XAbstractSocket_setSocketOption_base
#define XUdpSocket_socketOption_base       XAbstractSocket_socketOption_base

// 缓冲区
#define XUdpSocket_readBufferSize          XAbstractSocket_readBufferSize
#define XUdpSocket_setReadBufferSize_base  XAbstractSocket_setReadBufferSize_base

// 绑定和连接
#define XUdpSocket_bind_base               XAbstractSocket_bind_base
#define XUdpSocket_bindAny                 XAbstractSocket_bindAny
#define XUdpSocket_connectToHost_base      XAbstractSocket_connectToHost_base
#define XUdpSocket_disconnectFromHost_base XAbstractSocket_disconnectFromHost_base
#define XUdpSocket_abort                   XAbstractSocket_abort
#define XUdpSocket_flush                   XAbstractSocket_flush

// 代理
#define XUdpSocket_setProxy                XAbstractSocket_setProxy
#define XUdpSocket_proxy                   XAbstractSocket_proxy

// =============== XUdpSocket 特有 API（非虚函数）===============

/**
 * @brief 判断是否有待读的数据报。
 * @param sock UDP 套接字实例
 * @return true 若至少有一个数据报等待读取
 */
bool XUdpSocket_hasPendingDatagrams(const XUdpSocket* sock);

/**
 * @brief 获取第一个待读数据报的大小。
 * @param sock UDP 套接字实例
 * @return 数据报字节数，无数据报时返回 -1
 */
int64_t XUdpSocket_pendingDatagramSize(const XUdpSocket* sock);

/**
 * @brief 读取一个数据报。
 * @param sock UDP 套接字实例
 * @param data 数据缓冲区
 * @param maxSize 缓冲区最大大小
 * @param address 输出发送者地址（可为 NULL）
 * @param port 输出发送者端口（可为 NULL）
 * @return 实际读取的字节数，失败返回 -1
 */
int64_t XUdpSocket_readDatagram(XUdpSocket* sock, char* data, int64_t maxSize,
                                 XHostAddress* address, uint16_t* port);

/**
 * @brief 接收一个数据报（返回 XNetworkDatagram）。
 * @param sock UDP 套接字实例
 * @param maxSize 最大读取大小（-1 表示读取整个数据报）
 * @return 数据报对象（需调用者释放），失败返回 NULL
 */
XNetworkDatagram* XUdpSocket_receiveDatagram(XUdpSocket* sock, int64_t maxSize);

/**
 * @brief 发送数据报。
 * @param sock UDP 套接字实例
 * @param data 数据缓冲区
 * @param size 数据大小
 * @param address 目标地址
 * @param port 目标端口
 * @return 实际发送的字节数，失败返回 -1
 */
int64_t XUdpSocket_writeDatagram(XUdpSocket* sock, const char* data, int64_t size,
                                  const XHostAddress* address, uint16_t port);

/**
 * @brief 发送数据报（XByteArray 版本）。
 * @param sock UDP 套接字实例
 * @param datagram 数据内容
 * @param address 目标地址
 * @param port 目标端口
 * @return 实际发送的字节数，失败返回 -1
 */
int64_t XUdpSocket_writeDatagram_2(XUdpSocket* sock, const XByteArray* datagram,
                                    const XHostAddress* address, uint16_t port);

/**
 * @brief 发送数据报（XNetworkDatagram 版本）。
 * @param sock UDP 套接字实例
 * @param datagram 数据报对象
 * @return 实际发送的字节数，失败返回 -1
 */
int64_t XUdpSocket_writeDatagram_3(XUdpSocket* sock, const XNetworkDatagram* datagram);

// =============== 多播组管理 ===============

/**
 * @brief 加入多播组（使用默认接口）。
 * @param sock UDP 套接字实例
 * @param groupAddress 多播组地址
 * @return 成功返回 true
 */
bool XUdpSocket_joinMulticastGroup(XUdpSocket* sock, const XHostAddress* groupAddress);

/**
 * @brief 加入多播组（指定接口）。
 * @param sock UDP 套接字实例
 * @param groupAddress 多播组地址
 * @param iface 网络接口
 * @return 成功返回 true
 */
bool XUdpSocket_joinMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                       const XNetworkInterface* iface);

/**
 * @brief 离开多播组（使用默认接口）。
 * @param sock UDP 套接字实例
 * @param groupAddress 多播组地址
 * @return 成功返回 true
 */
bool XUdpSocket_leaveMulticastGroup(XUdpSocket* sock, const XHostAddress* groupAddress);

/**
 * @brief 离开多播组（指定接口）。
 * @param sock UDP 套接字实例
 * @param groupAddress 多播组地址
 * @param iface 网络接口
 * @return 成功返回 true
 */
bool XUdpSocket_leaveMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                        const XNetworkInterface* iface);

/**
 * @brief 获取多播输出接口。
 * @param sock UDP 套接字实例
 * @return 网络接口索引，失败返回 0
 */
uint32_t XUdpSocket_multicastInterface(const XUdpSocket* sock);

/**
 * @brief 设置多播输出接口。
 * @param sock UDP 套接字实例
 * @param interfaceIndex 网络接口索引
 */
void XUdpSocket_setMulticastInterface(XUdpSocket* sock, uint32_t interfaceIndex);

// =============== 信号函数（继承自 XAbstractSocket）===============

#define XUdpSocket_hostFound_signal        XAbstractSocket_hostFound_signal
#define XUdpSocket_connected_signal        XAbstractSocket_connected_signal
#define XUdpSocket_disconnected_signal     XAbstractSocket_disconnected_signal
#define XUdpSocket_stateChanged_signal     XAbstractSocket_stateChanged_signal
#define XUdpSocket_errorOccurred_signal    XAbstractSocket_errorOccurred_signal
#define XUdpSocket_readyRead_signal        XIODevice_readyRead_signal
#define XUdpSocket_bytesWritten_signal     XIODevice_bytesWritten_signal

#endif // XNETWORK_UDPSOCKET_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XUdpSocket_create
#define XUdpSocket_create(...) XUdpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XUDPSOCKET_H
