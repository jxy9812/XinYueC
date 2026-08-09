// XTcpSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XTcpSocket.h"
#include "XMemory.h"
#if XNETWORK_ON
#if XNETWORK_ABSTRACT_SOCKET_ON

/**
 * @brief 初始化已分配的 XTcpSocket 结构体。
 */
void XTcpSocket_init(XTcpSocket* sock) {
    if (!sock) return;
    // 直接调用父类初始化，指定 TCP 类型
    XAbstractSocket_init((XAbstractSocket*)sock, XAbstractSocket_TcpSocket);
}

/**
 * @brief 在堆上创建并初始化一个 XTcpSocket 实例。
 */
XTcpSocket* XTcpSocket_create(void) {
    XTcpSocket* sock = XNew(XTcpSocket);
    if (!sock) return NULL;
    
    XTcpSocket_init(sock);
    Set_Class_MemoryFree(sock, XFree_System);
    return sock;
}
#endif // XNETWORK_ABSTRACT_SOCKET_ON
#endif /* XNETWORK_ON */
