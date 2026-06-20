// XUdpSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XUdpSocket.h"
#include "XNetwork_platform.h"
#include "XMemory.h"
#include "XByteArray.h"
#include <string.h>

/* Helper macro to get private data from socket */
#define GET_PRIV(sock) ((XNetworkSocketPrivate*)(sock)->base.d_ptr)

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XUdpSocket_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XUdpSocket))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XAbstractSocket);

#if SHOWCONTAINERSIZE
    printf("XUdpSocket vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

void XUdpSocket_init(XUdpSocket* sock)
{
    if (!sock) return;
    
    XAbstractSocket_init(&sock->base, XAbstractSocket_UdpSocket);
    XClassSetVtable(sock, XUdpSocket);
}

XUdpSocket* XUdpSocket_create(void)
{
    XUdpSocket* sock = (XUdpSocket*)XMalloc_System(sizeof(XUdpSocket));
    if (!sock) return NULL;
    
    XUdpSocket_init(sock);
    Set_Class_MemoryFree(sock, XFree_System);
    return sock;
}

/* ============================================================================
 * 数据报操作
 * ============================================================================ */

bool XUdpSocket_hasPendingDatagrams(const XUdpSocket* sock)
{
    if (!sock) return false;
    
    /* 数据在 XIODevice 的 ringBuffer 中，检查 bytesAvailable */
    return XIODevice_bytesAvailable_base((const XIODevice*)sock) > 0;
}

int64_t XUdpSocket_pendingDatagramSize(const XUdpSocket* sock)
{
    if (!sock) return -1;
    
    /* 数据在 XIODevice 的 ringBuffer 中 */
    int64_t available = XIODevice_bytesAvailable_base((const XIODevice*)sock);
    return available > 0 ? available : -1;
}

int64_t XUdpSocket_readDatagram(XUdpSocket* sock, char* data, int64_t maxSize,
                                 XHostAddress* address, uint16_t* port)
{
    if (!sock || !data || maxSize <= 0) return -1;
    
    XNetworkSocketPrivate* priv = GET_PRIV(sock);
    if (!priv) return -1;
    
    /* 使用父类 XAbstractSocket 的 read 方法读取数据 */
    int64_t bytesRead = XAbstractSocket_read(&sock->base, data, maxSize);
    
    if (bytesRead <= 0) return bytesRead;
    
    /* 获取发送者地址信息 */
    if (address || port) {
        XNetwork_getLastDatagramSender(priv, address, port);
    }
    
    return bytesRead;
}

XNetworkDatagram* XUdpSocket_receiveDatagram(XUdpSocket* sock, int64_t maxSize)
{
    if (!sock) return NULL;
    
    /* 获取数据报大小 */
    int64_t dgramSize = XUdpSocket_pendingDatagramSize(sock);
    if (dgramSize < 0) return NULL;
    
    /* 确定读取大小 */
    int64_t readSize = (maxSize < 0) ? dgramSize : (maxSize < dgramSize ? maxSize : dgramSize);
    
    /* 分配缓冲区 */
    char* buffer = (char*)XMalloc_System((size_t)readSize);
    if (!buffer) return NULL;
    
    /* 读取数据报 */
    XHostAddress senderAddr;
    XHostAddress_init(&senderAddr);
    uint16_t senderPort = 0;
    
    int64_t bytesRead = XUdpSocket_readDatagram(sock, buffer, readSize, &senderAddr, &senderPort);
    
    if (bytesRead < 0) {
        XFree_System(buffer);
        return NULL;
    }
    
    /* 创建 XNetworkDatagram */
    XNetworkDatagram* dgram = XNetworkDatagram_create();
    if (!dgram) {
        XFree_System(buffer);
        return NULL;
    }
    
    /* 设置数据 */
    XByteArray* data = XByteArray_create();
    if (data) {
        XByteArray_push_back_2(data, buffer, bytesRead);
        XNetworkDatagram_setData(dgram, data);
        XByteArray_delete_base(data);
    }
    
    /* 设置发送者信息 */
    XNetworkDatagram_setSender(dgram, &senderAddr, senderPort);
    
    XFree_System(buffer);
    return dgram;
}

int64_t XUdpSocket_writeDatagram(XUdpSocket* sock, const char* data, int64_t size,
                                  const XHostAddress* address, uint16_t port)
{
    if (!sock || !data || size <= 0 || !address) return -1;
    
    XNetworkSocketPrivate* priv = GET_PRIV(sock);
    if (!priv) return -1;
    
    /* 使用平台层的 socketWrite 进行 UDP 发送 */
    return XNetwork_socketWrite(priv, data, size, XNetwork_Udp, address, port, NULL);
}

int64_t XUdpSocket_writeDatagram_2(XUdpSocket* sock, const XByteArray* datagram,
                                    const XHostAddress* address, uint16_t port)
{
    if (!sock || !datagram || !address) return -1;
    
    const char* data = (const char*)XByteArray_data((XByteArray*)datagram);
    int64_t size = XByteArray_size_base(datagram);
    
    return XUdpSocket_writeDatagram(sock, data, size, address, port);
}

int64_t XUdpSocket_writeDatagram_3(XUdpSocket* sock, const XNetworkDatagram* datagram)
{
    if (!sock || !datagram) return -1;
    
    /* 获取目标地址 */
    const XHostAddress* destAddr = XNetworkDatagram_destinationAddress(datagram);
    int destPort = XNetworkDatagram_destinationPort(datagram);
    
    if (!destAddr || XHostAddress_isNull(destAddr) || destPort < 0) {
        /* 如果没有设置目标，使用 connectToHost 的目标 */
        destAddr = XAbstractSocket_peerAddress(&sock->base);
        destPort = XAbstractSocket_peerPort(&sock->base);
    }
    
    if (!destAddr || XHostAddress_isNull(destAddr) || destPort < 0) {
        return -1;
    }
    
    const XByteArray* data = XNetworkDatagram_data(datagram);
    if (!data) return -1;
    
    /* TODO: 处理 hopLimit 和 interfaceIndex */
    
    return XUdpSocket_writeDatagram_2(sock, data, destAddr, (uint16_t)destPort);
}

/* ============================================================================
 * 多播组管理
 * ============================================================================ */

bool XUdpSocket_joinMulticastGroup(XUdpSocket* sock, const XHostAddress* groupAddress)
{
    if (!sock || !groupAddress) return false;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return false;
    
    return XNetwork_joinMulticastGroup(sd, groupAddress, 0);
}

bool XUdpSocket_joinMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                       const XNetworkInterface* iface)
{
    if (!sock || !groupAddress) return false;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return false;
    
    /* TODO: 从 XNetworkInterface 获取接口索引 */
    uint32_t ifIndex = 0;
    (void)iface;
    
    return XNetwork_joinMulticastGroup(sd, groupAddress, ifIndex);
}

bool XUdpSocket_leaveMulticastGroup(XUdpSocket* sock, const XHostAddress* groupAddress)
{
    if (!sock || !groupAddress) return false;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return false;
    
    return XNetwork_leaveMulticastGroup(sd, groupAddress, 0);
}

bool XUdpSocket_leaveMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                        const XNetworkInterface* iface)
{
    if (!sock || !groupAddress) return false;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return false;
    
    /* TODO: 从 XNetworkInterface 获取接口索引 */
    uint32_t ifIndex = 0;
    (void)iface;
    
    return XNetwork_leaveMulticastGroup(sd, groupAddress, ifIndex);
}

uint32_t XUdpSocket_multicastInterface(const XUdpSocket* sock)
{
    if (!sock) return 0;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return 0;
    
    return XNetwork_multicastInterface(sd);
}

void XUdpSocket_setMulticastInterface(XUdpSocket* sock, uint32_t interfaceIndex)
{
    if (!sock) return;
    
    intptr_t sd = XAbstractSocket_socketDescriptor_base(&sock->base);
    if (sd < 0) return;
    
    XNetwork_setMulticastInterface(sd, interfaceIndex);
}
