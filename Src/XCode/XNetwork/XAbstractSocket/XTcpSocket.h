// XTcpSocket.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QTcpSocket，继承自 XAbstractSocket。
// 提供便捷的 TCP 客户端封装。

#ifndef XTCPSOCKET_H
#define XTCPSOCKET_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_ABSTRACT_SOCKET_ON

#include "XAbstractSocket.h"

// =============== 结构体定义 ===============

/**
 * @brief TCP 套接字类，继承自 XAbstractSocket。
 * @note 自动设置 socketType 为 TcpSocket。
 * @note 所有 API 直接继承自 XAbstractSocket，无需额外封装。
 */
typedef struct XTcpSocket {
    XAbstractSocket base;  ///< 继承 XAbstractSocket
} XTcpSocket;

// =============== 继承自 XAbstractSocket 的 API（符号重命名）===============

// 生命周期
#define XTcpSocket_deleteLater              XIODevice_deleteLater

// 打开/关闭
#define XTcpSocket_open_base                XIODevice_open_base
#define XTcpSocket_close_base               XIODevice_close_base
#define XTcpSocket_isOpen                   XIODevice_isOpen

// 读写操作
#define XTcpSocket_read_1                     XIODevice_read_1
#define XTcpSocket_write_1                    XIODevice_write_1
#define XTcpSocket_readAll_2                  XIODevice_readAll_3
#define XTcpSocket_readLine_1                 XIODevice_readLine_1
#define XTcpSocket_peek_1                     XIODevice_peek_1

// 状态查询
#define XTcpSocket_isReadable               XIODevice_isReadable
#define XTcpSocket_isWritable               XIODevice_isWritable
#define XTcpSocket_isSequential             XIODevice_isSequential
#define XTcpSocket_atEnd_base               XIODevice_atEnd_base
#define XTcpSocket_bytesAvailable_base      XIODevice_bytesAvailable_base
#define XTcpSocket_bytesToWrite_base        XIODevice_bytesToWrite_base
#define XTcpSocket_canReadLine_base         XIODevice_canReadLine_base

// 等待函数
#define XTcpSocket_waitForReadyRead_base    XIODevice_waitForReadyRead_base
#define XTcpSocket_waitForBytesWritten_base XIODevice_waitForBytesWritten_base
#define XTcpSocket_waitForConnected_base    XAbstractSocket_waitForConnected_base
#define XTcpSocket_waitForDisconnected_base XAbstractSocket_waitForDisconnected_base

// Socket 状态
#define XTcpSocket_state                    XAbstractSocket_state
#define XTcpSocket_error                    XAbstractSocket_error
#define XTcpSocket_errorString              XAbstractSocket_errorString
#define XTcpSocket_isValid                  XAbstractSocket_isValid

// Socket 信息
#define XTcpSocket_localAddress             XAbstractSocket_localAddress
#define XTcpSocket_localPort                XAbstractSocket_localPort
#define XTcpSocket_peerAddress              XAbstractSocket_peerAddress
#define XTcpSocket_peerPort                 XAbstractSocket_peerPort
#define XTcpSocket_peerName                 XAbstractSocket_peerName
#define XTcpSocket_socketDescriptor_base    XAbstractSocket_socketDescriptor_base

// Socket 选项
#define XTcpSocket_setSocketOption_base     XAbstractSocket_setSocketOption_base
#define XTcpSocket_socketOption_base        XAbstractSocket_socketOption_base

// 缓冲区
#define XTcpSocket_readBufferSize           XAbstractSocket_readBufferSize
#define XTcpSocket_setReadBufferSize_base   XAbstractSocket_setReadBufferSize_base

// =============== 核心操作函数 ===============

void XTcpSocket_init(XTcpSocket* sock);
XTcpSocket* XTcpSocket_create_ex(XMemoryType memory);
#define XTcpSocket_connectToHost_base       XAbstractSocket_connectToHost_base
#define XTcpSocket_disconnectFromHost_base  XAbstractSocket_disconnectFromHost_base
#define XTcpSocket_flush                    XAbstractSocket_flush

#define XTcpSocket_abort                    XAbstractSocket_abort

// =============== 信号函数（继承自 XAbstractSocket）===============

#define XTcpSocket_hostFound_signal         XAbstractSocket_hostFound_signal
#define XTcpSocket_connected_signal         XAbstractSocket_connected_signal
#define XTcpSocket_disconnected_signal      XAbstractSocket_disconnected_signal
#define XTcpSocket_stateChanged_signal      XAbstractSocket_stateChanged_signal
#define XTcpSocket_errorOccurred_signal     XAbstractSocket_errorOccurred_signal
#define XTcpSocket_readyRead_signal         XIODevice_readyRead_signal
#define XTcpSocket_bytesWritten_signal      XIODevice_bytesWritten_signal

#endif // XNETWORK_ABSTRACT_SOCKET_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XTcpSocket_create
#define XTcpSocket_create(...) XTcpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XTCPSOCKET_H
