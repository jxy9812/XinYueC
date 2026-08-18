// XUdpSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XUdpSocket.h"
#include "XDeviceNetwork.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XByteArray.h"
#include <string.h>
#if XNETWORK_ON
#if XNETWORK_UDPSOCKET_ON

static bool xudpsocket_control(XUdpSocket* socket, XDeviceNetworkCommand command,
                               const XVarList* input, XVarList* output)
{
    if (!socket || socket->base.m_deviceFd == XFD_INVALID) return false;
    return XDevice_control(socket->base.m_deviceFd, command, input, output);
}

static bool xudpsocket_setMulticastGroup(XUdpSocket* socket, bool join,
                                         const XHostAddress* group, uint32_t interfaceIndex)
{
    XVarList* input;
    bool ok;
    input = XVarList_Create(XVar(bool, join), XVar(const XHostAddress*, group),
                            XVar(uint32_t, interfaceIndex));
    if (!input) return false;
    ok = xudpsocket_control(socket, XDeviceNetworkCommand_SetMulticastGroup, input, NULL);
    XVarList_delete(input);
    return ok;
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XUdpSocket_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUdpSocket)
    XVTABLE_INHERIT_XCLASS(XAbstractSocket);

    XCLASS_SHOW_SIZE_DEFAULT(XUdpSocket);
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

XUdpSocket* XUdpSocket_create_ex(XMemoryType memory)
{
    XUdpSocket* sock = (XUdpSocket*)XMemory_malloc(sizeof(XUdpSocket), memory);
    if (!sock) return NULL;
    
    XUdpSocket_init(sock);
    Set_Class_Memory(sock, memory); Set_Class_IsHeap(sock, true);
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
    
    /* 使用父类 XAbstractSocket 的 read 方法读取数据 */
    int64_t bytesRead = XAbstractSocket_read(&sock->base, data, maxSize);
    
    if (bytesRead <= 0) return bytesRead;
    
    /* 获取发送者地址信息 */
    if (address || port) {
        XVarList* output = XVarList_Create(XVar(XHostAddress*, address), XVar(uint16_t*, port));
        if (!output) return -1;
        if (!xudpsocket_control(sock, XDeviceNetworkCommand_GetLastDatagramSender, NULL, output)) {
            XVarList_delete(output);
            return -1;
        }
        XVarList_delete(output);
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
    
    XVarList* input;
    XVarList* output;
    int64_t written = -1;
    bool ok;
    input = XVarList_Create(XVar(const void*, data), XVar(int64_t, size),
                            XVar(const XHostAddress*, address), XVar(uint16_t, port));
    output = XVarList_Create(XVar(int64_t, written));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return -1;
    }
    ok = xudpsocket_control(sock, XDeviceNetworkCommand_SendDatagram, input, output);
    if (ok) {
        XVarList_start(output);
        written = XVarList_arg(output, int64_t);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return ok ? written : -1;
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
    
    return xudpsocket_setMulticastGroup(sock, true, groupAddress, 0);
}

bool XUdpSocket_joinMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                       const XNetworkInterface* iface)
{
    if (!sock || !groupAddress) return false;
    
    /* TODO: 从 XNetworkInterface 获取接口索引 */
    uint32_t ifIndex = 0;
    (void)iface;
    
    return xudpsocket_setMulticastGroup(sock, true, groupAddress, ifIndex);
}

bool XUdpSocket_leaveMulticastGroup(XUdpSocket* sock, const XHostAddress* groupAddress)
{
    if (!sock || !groupAddress) return false;
    
    return xudpsocket_setMulticastGroup(sock, false, groupAddress, 0);
}

bool XUdpSocket_leaveMulticastGroup_2(XUdpSocket* sock, const XHostAddress* groupAddress,
                                        const XNetworkInterface* iface)
{
    if (!sock || !groupAddress) return false;
    
    /* TODO: 从 XNetworkInterface 获取接口索引 */
    uint32_t ifIndex = 0;
    (void)iface;
    
    return xudpsocket_setMulticastGroup(sock, false, groupAddress, ifIndex);
}

uint32_t XUdpSocket_multicastInterface(const XUdpSocket* sock)
{
    if (!sock) return 0;
    
    uint32_t ifIndex = 0;
    uint32_t* interfaceIndexOut = &ifIndex;
    XVarList* output = XVarList_Create(XVar(uint32_t*, interfaceIndexOut));
    bool ok;
    if (!output) return 0;
    ok = xudpsocket_control((XUdpSocket*)sock, XDeviceNetworkCommand_GetMulticastInterface, NULL, output);
    XVarList_delete(output);
    return ok ? ifIndex : 0;
}

void XUdpSocket_setMulticastInterface(XUdpSocket* sock, uint32_t interfaceIndex)
{
    if (!sock) return;
    
    XVarList* input = XVarList_Create(XVar(uint32_t, interfaceIndex));
    if (!input) return;
    (void)xudpsocket_control(sock, XDeviceNetworkCommand_SetMulticastInterface, input, NULL);
    XVarList_delete(input);
}
#endif // XNETWORK_UDPSOCKET_ON
#endif /* XNETWORK_ON */
