// XAbstractSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XAbstractSocket.h"
#include "XNetwork_platform.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XIODevicePrivate.h"
#include "XDateTime.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XRingBuffer.h"
#include "XNetworkProxyHandshake.h"
#include <string.h>
#include <assert.h>

// ==================== 辅助宏 ====================
#define getPriv(sock) ((XNetworkSocketPrivate*)(sock)->d_ptr)

/* SocketType 转换：XAbstractSocket_SocketType -> XNetworkSocketType */
static XNetworkSocketType toNetworkSockType(XAbstractSocket_SocketType type)
{
    return (type == XAbstractSocket_UdpSocket) ? XNetwork_Udp : XNetwork_Tcp;
}

/* Protocol 转换：XAbstractSocket_NetworkLayerProtocol -> XNetworkProtocol */
static XNetworkProtocol toNetworkProtocol(XAbstractSocket_NetworkLayerProtocol protocol)
{
    switch (protocol) {
    case XAbstractSocket_IPv4Protocol: return XNetwork_IPv4;
    case XAbstractSocket_IPv6Protocol: return XNetwork_IPv6;
    default: return XNetwork_Any;
    }
}

// ==================== 内部辅助函数声明 ====================
static void VXAbstractSocket_deinit(XAbstractSocket* sock);
static bool VXAbstractSocket_open(XAbstractSocket* self, XIODeviceBaseMode mode);
static void VXAbstractSocket_close(XAbstractSocket* self);
static int64_t VXAbstractSocket_readData(XAbstractSocket* self, char* data, int64_t maxlen);
static int64_t VXAbstractSocket_writeData(XAbstractSocket* self, const char* data, int64_t len);
static bool VXAbstractSocket_isSequential(const XAbstractSocket* self);
//static int64_t XAbstractSocket_bytesAvailable_base(const XAbstractSocket* self);
//static int64_t XAbstractSocket_bytesToWrite_base(const XAbstractSocket* self);
static bool VXAbstractSocket_canReadLine(const XAbstractSocket* self);
static bool VXAbstractSocket_waitForReadyRead(XAbstractSocket* self, int msecs);
static bool VXAbstractSocket_waitForBytesWritten(XAbstractSocket* self, int msecs);
static bool VXAbstractSocket_atEnd(XAbstractSocket* self);
static int64_t VXAbstractSocket_pos(const XAbstractSocket* self);
static int64_t VXAbstractSocket_size(const XAbstractSocket* self);
static bool VXAbstractSocket_seek(XAbstractSocket* self, int64_t pos);
static bool VXAbstractSocket_reset(XAbstractSocket* self);
static int64_t VXAbstractSocket_readLineData(XAbstractSocket* self, char* data, int64_t maxlen);
static int64_t VXAbstractSocket_skipData(XAbstractSocket* self, int64_t maxSize);
static bool VXAbstractSocket_event(XAbstractSocket* self, XEvent* e);
// --- XAbstractSocket 特有虚函数（默认实现）---
static void VXAbstractSocket_Resume(XAbstractSocket* self);
static bool VXAbstractSocket_Bind(XAbstractSocket* self, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode);
static void VXAbstractSocket_ConnectToHost(XAbstractSocket* self, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol);
static void VXAbstractSocket_DisconnectFromHost(XAbstractSocket* self);
static intptr_t VXAbstractSocket_SocketDescriptor(const XAbstractSocket* self);
static bool VXAbstractSocket_SetSocketDescriptor(XAbstractSocket* self, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode);
static void VXAbstractSocket_SetSocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option, const XVariant* value);
static XVariant* VXAbstractSocket_SocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option);
static void VXAbstractSocket_SetReadBufferSize(XAbstractSocket* self, int64_t size);
static bool VXAbstractSocket_WaitForConnected(XAbstractSocket* self, int msecs);
static bool VXAbstractSocket_WaitForDisconnected(XAbstractSocket* self, int msecs);
// ==================== 虚函数表初始化 ====================
XVtable* XAbstractSocket_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractSocket))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承 XIODevice 的虚表
    XVTABLE_INHERIT_XCLASS(XIODevice);
    // 添加 XAbstractSocket 特有虚函数
    void* table[] = {
        VXAbstractSocket_Resume,
        VXAbstractSocket_Bind,
        VXAbstractSocket_ConnectToHost,
        VXAbstractSocket_DisconnectFromHost,
        VXAbstractSocket_SocketDescriptor,
        VXAbstractSocket_SetSocketDescriptor,
        VXAbstractSocket_SetSocketOption,
        VXAbstractSocket_SocketOption,
        VXAbstractSocket_SetReadBufferSize,
        VXAbstractSocket_WaitForConnected,
        VXAbstractSocket_WaitForDisconnected
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
        // 重载关键虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractSocket_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXAbstractSocket_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXAbstractSocket_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXAbstractSocket_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXAbstractSocket_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXAbstractSocket_isSequential);
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, XAbstractSocket_bytesAvailable_base);
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesToWrite, XAbstractSocket_bytesToWrite_base);
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_CanReadLine, VXAbstractSocket_canReadLine);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead, VXAbstractSocket_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten, VXAbstractSocket_waitForBytesWritten);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_AtEnd, VXAbstractSocket_atEnd);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Pos, VXAbstractSocket_pos);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Size, VXAbstractSocket_size);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Seek, VXAbstractSocket_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Reset, VXAbstractSocket_reset);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadLineData, VXAbstractSocket_readLineData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SkipData, VXAbstractSocket_skipData);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXAbstractSocket_event);

#if SHOWCONTAINERSIZE
        printf("XAbstractSocket size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

// ==================== 析构函数 ====================
static void VXAbstractSocket_deinit(XAbstractSocket* sock)
{
    if (!sock) return;
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_sendPostedEvents(sock, 0);
    // 调用 XNetwork 清理私有数据
    if (sock->d_ptr) {
        XNetwork_deleteSocketPrivate(sock->d_ptr);
        sock->d_ptr = NULL;
    }

    // 清理自身资源
    if (sock->errorString) {
        XString_delete_base(sock->errorString);
        sock->errorString = NULL;
    }
    if (sock->peerName) {
        XString_delete_base(sock->peerName);
        sock->peerName = NULL;
    }
    if (sock->protocolTag) {
        XString_delete_base(sock->protocolTag);
        sock->protocolTag = NULL;
    }
    // 释放代理配置资源
    XNetworkProxy_deinit_base(&sock->proxy);
    // 释放代理握手上下文
    if (sock->proxyHandshakeCtx) {
        XNetworkProxyHandshake_destroyContext(sock->proxyHandshakeCtx);
        sock->proxyHandshakeCtx = NULL;
    }
    // 释放地址对象
    XHostAddress_deinit_base((XHostAddress*)&sock->localAddress);
    XHostAddress_deinit_base((XHostAddress*)&sock->peerAddress);
    // 调用父类析构（XIODevice → XObject → XClass）
    XClass_Deinit_Parent(XIODevice, sock);
}

// ==================== 初始化函数 ====================
void XAbstractSocket_init(XAbstractSocket* sock, XAbstractSocket_SocketType type)
{
    if (!sock) return;

    // 初始化父类 XIODevice
    XIODevice_init((XIODevice*)sock);
    XClassGetVtable(sock) = XAbstractSocket_class_init();

    // 初始化自身字段
    sock->socketType = type;
    sock->state = XAbstractSocket_UnconnectedState;
    sock->error = XAbstractSocket_UnknownSocketError;
    sock->errorString = NULL;
    XHostAddress_init(&sock->localAddress);
    XHostAddress_setAddressSpecial(&sock->localAddress, XHostAddress_NullSpecial);
    sock->localPort = 0;
    XHostAddress_init(&sock->peerAddress);
    XHostAddress_setAddressSpecial(&sock->peerAddress, XHostAddress_NullSpecial);
    sock->peerPort = 0;
    sock->peerName = NULL;
    sock->readBufferSize = -1; // 无限制
    sock->m_pauseMode = XAbstractSocket_PauseNever;
    sock->autoDeleteOnDisconnect = false;
    sock->isValidFlag = false;
    sock->protocolTag = NULL;
    XNetworkProxy_init(&sock->proxy);
    sock->proxyHandshakeCtx = NULL;
    sock->d_ptr = NULL;

    // 初始化私有数据（通过 XNetwork 平台层）
    XNetwork_ensureInit();
    sock->d_ptr = XNetwork_createSocketPrivate(sock);
}

// ==================== Getter 实现 ====================
XAbstractSocket_SocketType XAbstractSocket_socketType(const XAbstractSocket* sock)
{
    return sock ? sock->socketType : XAbstractSocket_UnknownSocketType;
}

XAbstractSocket_SocketState XAbstractSocket_state(const XAbstractSocket* sock)
{
    return sock ? sock->state : XAbstractSocket_UnconnectedState;
}

XAbstractSocket_SocketError XAbstractSocket_error(const XAbstractSocket* sock)
{
    return sock ? sock->error : XAbstractSocket_UnknownSocketError;
}

XString* XAbstractSocket_errorString(const XAbstractSocket* sock)
{
    if (!sock) return XString_create_utf8("Invalid socket");
    if (sock->errorString) return XString_create_copy(sock->errorString);
    return XString_create_utf8("Unknown error");
}

const XHostAddress* XAbstractSocket_localAddress(const XAbstractSocket* sock)
{
    return sock ? &sock->localAddress : &XHostAddress_Null;
}

uint16_t XAbstractSocket_localPort(const XAbstractSocket* sock)
{
    return sock ? sock->localPort : 0;
}

const XHostAddress* XAbstractSocket_peerAddress(const XAbstractSocket* sock)
{
    return sock ? &sock->peerAddress : &XHostAddress_Null;
}

uint16_t XAbstractSocket_peerPort(const XAbstractSocket* sock)
{
    return sock ? sock->peerPort : 0;
}

XString* XAbstractSocket_peerName(const XAbstractSocket* sock)
{
    if (!sock || !sock->peerName) return NULL;
    return XString_create_copy(sock->peerName);
}

int64_t XAbstractSocket_readBufferSize(const XAbstractSocket* sock)
{
    return sock ? sock->readBufferSize : -1;
}

XAbstractSocket_PauseModes XAbstractSocket_pauseMode(const XAbstractSocket* sock)
{
    return sock ? sock->m_pauseMode : XAbstractSocket_PauseNever;
}

bool XAbstractSocket_isValid(const XAbstractSocket* sock)
{
    return sock && sock->isValidFlag;
}

XString* XAbstractSocket_protocolTag(const XAbstractSocket* sock)
{
    return sock ? sock->protocolTag : NULL;
}

// ==================== Setter 实现 ====================
void XAbstractSocket_setReadBufferSize_base(XAbstractSocket* sock, int64_t size)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return ;
    XClassGetVirtualFunc(sock, EXAbstractSocket_SetReadBufferSize, void (*)(XAbstractSocket*, int64_t))(sock, size);
}

void XAbstractSocket_setPauseMode(XAbstractSocket* sock, XAbstractSocket_PauseModes mode)
{
    if (sock) sock->m_pauseMode = mode;
}

void XAbstractSocket_setProtocolTag(XAbstractSocket* sock, const char* tag)
{
    if (!sock) return;
  
    // 释放旧的标签
    if (sock->protocolTag) {
        XString_delete_base(sock->protocolTag);
        sock->protocolTag = NULL;
    }
  
    // 创建新的标签
    if (tag && tag[0] != '\0') {
        sock->protocolTag = XString_create_fmt_utf8(tag);
    }
}

void XAbstractSocket_setProxy(XAbstractSocket* sock, const XNetworkProxy* proxy)
{
    if (!sock) return;
  
    // 释放旧的代理配置资源
    XNetworkProxy_deinit_base(&sock->proxy);
  
    if (proxy) {
        // 深拷贝代理配置
        sock->proxy.type = proxy->type;
        sock->proxy.capabilities = proxy->capabilities;
        sock->proxy.port = proxy->port;
        sock->proxy.hostName = proxy->hostName ? XStrdup(proxy->hostName) : NULL;
        sock->proxy.user = proxy->user ? XStrdup(proxy->user) : NULL;
        sock->proxy.password = proxy->password ? XStrdup(proxy->password) : NULL;
    } else {
        // 使用默认代理配置
        XNetworkProxy_init(&sock->proxy);
    }
}

XNetworkProxy* XAbstractSocket_proxy(const XAbstractSocket* sock)
{
    return sock ? (XNetworkProxy*)&sock->proxy : NULL;
}

// flush - 刷新发送缓冲区，等待所有待发送数据发送完毕
bool XAbstractSocket_flush(XAbstractSocket* sock)
{
    if (!sock) return false;
    //XAbstractSocketPrivate* priv = getPriv(sock);
    //if (!priv || priv->socketHandle == INVALID_SOCKET) return false;

    XIODevice* io = (XIODevice*)sock;

    // 检查是否有待发送数据
    while (XIODevice_bytesToWrite_base(io) > 0) {
        // 等待数据发送完成
        if (!XAbstractSocket_waitForBytesWritten(sock, 100)) {
            // 超时或出错
            return false;
        }
    }

    return true;
}

// ==================== 受保护的 setter ====================
void XAbstractSocket_setLocalPort(XAbstractSocket* sock, uint16_t port)
{
    if (sock) sock->localPort = port;
}

void XAbstractSocket_setLocalAddress(XAbstractSocket* sock, const XHostAddress* address)
{
    if (sock && address)
    {
        XHostAddress_copy_base(&sock->localAddress, address);
    }
}

void XAbstractSocket_setPeerPort(XAbstractSocket* sock, uint16_t port)
{
    if (sock) sock->peerPort = port;
}

void XAbstractSocket_setPeerAddress(XAbstractSocket* sock, const XHostAddress* address)
{
    if (sock && address) {
        XHostAddress_copy_base(&sock->peerAddress, address);
    }
}

void XAbstractSocket_setPeerName(XAbstractSocket* sock, const char* name)
{
    if (!sock|| !name) return;
    if (sock->peerName) {
        XString_assign_utf8(sock->peerName, name);
    }
    else
    {
        sock->peerName = XString_create_utf8(name);
    }
}

void XAbstractSocket_setSocketState(XAbstractSocket* sock, XAbstractSocket_SocketState state)
{
    if (!sock || sock->state == state) return;

    XAbstractSocket_SocketState oldState = sock->state;
    sock->state = state;

    // 发射 stateChanged 信号
    XAbstractSocket_stateChanged_signal(sock, state);

    // 特殊状态转换处理
    if (state == XAbstractSocket_ConnectedState) 
    {
        XIODevice_open_base(sock, XIODevice_ReadWrite);
        // 从 HostLookupState 转换时，先发出 hostFound 信号
        if (oldState == XAbstractSocket_HostLookupState) {
            XAbstractSocket_hostFound_signal(sock);
        }
        // 无论从哪个状态转换到 ConnectedState，都发出 connected 信号
        XAbstractSocket_connected_signal(sock);
    }
    else if (state == XAbstractSocket_UnconnectedState && oldState != XAbstractSocket_UnconnectedState) 
    {
        ((XIODevice*)sock)->m_openMode = XIODevice_NotOpen;
        XAbstractSocket_disconnected_signal(sock);
        if (sock->autoDeleteOnDisconnect) {
            XAbstractSocket_deleteLater(sock);
        }
    }
}

void XAbstractSocket_setSocketError(XAbstractSocket* sock, XAbstractSocket_SocketError error, const char* str)
{
    if (!sock) return;

    sock->error = error;

    if (str) {
        if (sock->errorString) {
            XString_assign_utf8(sock->errorString, str);
        }
        else {
            sock->errorString = XString_create_utf8(str);
        }
    }

    // 发射 errorOccurred 信号
    XAbstractSocket_errorOccurred_signal(sock, error);
}

// ==================== 核心操作（存根，由子类实现）====================
bool XAbstractSocket_bind_base(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_Bind, bool(*)(XAbstractSocket*, const XHostAddress*, uint16_t, XAbstractSocket_BindMode))(sock, address,port, mode);
}

bool XAbstractSocket_bindAny(XAbstractSocket* sock, uint16_t port, XAbstractSocket_BindMode mode)
{
    XHostAddress any;
    XHostAddress_init(&any);
    //XHostAddress_setAddress(&any,"127.0.0.1");
    if (sock->socketType == XAbstractSocket_UdpSocket) {
        XHostAddress_setAddressSpecial(&any, XHostAddress_AnySpecial);
    }
    else {
        XHostAddress_setAddressSpecial(&any, XHostAddress_AnyIPv6Special);
    }
    bool result = XAbstractSocket_bind_base(sock, &any, port, mode);
    XHostAddress_deinit_base(&any);
    return result;
}

void XAbstractSocket_connectToHost_base(XAbstractSocket* sock, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return ;
    XClassGetVirtualFunc(sock, EXAbstractSocket_ConnectToHost, bool(*)(XAbstractSocket*, const char*, uint16_t, XIODeviceBaseMode, XAbstractSocket_NetworkLayerProtocol))(sock, hostName, port, mode, protocol);
}
void XAbstractSocket_disconnectFromHost_base(XAbstractSocket* sock)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return;
    XClassGetVirtualFunc(sock, EXAbstractSocket_DisconnectFromHost, bool(*)(XAbstractSocket*))(sock);
}

void XAbstractSocket_abort(XAbstractSocket* sock)
{
    if (!sock) return;
    XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
    XIODevice_close_base((XIODevice*)sock);
}

void XAbstractSocket_resume_base(XAbstractSocket* sock)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return ;
    XClassGetVirtualFunc(sock, EXAbstractSocket_Resume, void(*)(XAbstractSocket*))(sock);
}

// ==================== 套接字选项（存根）====================
void XAbstractSocket_setSocketOption_base(XAbstractSocket* sock, XAbstractSocket_SocketOption option, const XVariant* value)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return ;
    XClassGetVirtualFunc(sock, EXAbstractSocket_SetSocketOption, void (*)(XAbstractSocket*, XAbstractSocket_SocketOption, const XVariant*))(sock, option, value);
}

XVariant* XAbstractSocket_socketOption_base(XAbstractSocket* sock, XAbstractSocket_SocketOption option)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return NULL;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SocketOption, XVariant* (*)(XAbstractSocket*, XAbstractSocket_SocketOption))(sock, option);
}

// ==================== 平台句柄 ====================
intptr_t XAbstractSocket_socketDescriptor_base(const XAbstractSocket* sock)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), "")|| XClassGetVtable(sock)->size< EXAbstractSocket_SocketDescriptor)
        return 0;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SocketDescriptor, intptr_t(*)(XAbstractSocket*))(sock);
}

bool XAbstractSocket_setSocketDescriptor_base(XAbstractSocket* sock, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SetSocketDescriptor, bool (*)(XAbstractSocket*, intptr_t, XAbstractSocket_SocketState, XIODeviceBaseMode))(sock, socketDescriptor, state, openMode);
}

//XFd XAbstractSocket_fd(const XAbstractSocket* sock)
//{
//    return XNetwork_socketFd(sock->d_ptr);
//}

// ==================== 同步等待 ====================
bool XAbstractSocket_waitForConnected_base(XAbstractSocket* sock, int msecs)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_WaitForConnected, bool (*)(XAbstractSocket*, int))(sock, msecs);
}

bool XAbstractSocket_waitForDisconnected_base(XAbstractSocket* sock, int msecs)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_WaitForDisconnected, bool (*)(XAbstractSocket*, int))(sock, msecs);
}

bool XAbstractSocket_waitForBytesWritten(XAbstractSocket* sock, int msecs)
{
    return XIODevice_waitForBytesWritten_base((XIODevice*)sock, msecs);
}

// ==================== 虚函数重载 ====================

static bool VXAbstractSocket_open(XAbstractSocket* self, XIODeviceBaseMode mode)
{
    // 内部使用：设置打开模式
    if (!self) return false;
    self->base.m_openMode = mode;
    return true;
}

static void VXAbstractSocket_close(XAbstractSocket* self)
{
    if (!self) return;
    XNetworkSocketPrivate* priv = getPriv(self);
    if (priv) {
        XNetwork_socketDisconnect(priv);
        self->isValidFlag = false;
    }
    XClass_Parent(XIODevice, EXIODevice_Close, void (*)(XIODevice*))((XIODevice*)self);
}

static int64_t VXAbstractSocket_readData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return -1;

    void* ringBuffer = NULL;
    if (self->base.m_d) {
        int channel = XIODevice_currentReadChannel((XIODevice*)self);
        ringBuffer = XIODevicePrivate_getOrCreateReadBuffer(self->base.m_d, channel);
    }

    return XNetwork_socketRead(priv, data, maxlen, toNetworkSockType(self->socketType), ringBuffer);
}

static int64_t VXAbstractSocket_writeData(XAbstractSocket* self, const char* data, int64_t len)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return -1;

    void* ringBuffer = NULL;
    if (self->base.m_d) {
        int channel = XIODevice_currentWriteChannel((XIODevice*)self);
        ringBuffer = XIODevicePrivate_getOrCreateWriteBuffer(self->base.m_d, channel);
    }

    return XNetwork_socketWrite(priv, data, len, toNetworkSockType(self->socketType),
        &self->peerAddress, self->peerPort, ringBuffer);
}

static bool VXAbstractSocket_isSequential(const XAbstractSocket* self)
{
    (void)self;
    return true; // 所有套接字都是顺序设备
}


static bool VXAbstractSocket_canReadLine(const XAbstractSocket* self)
{
    // 通用实现：检查缓冲区是否有完整行
    if (!self || !self->base.m_d) return false;
    return XIODevicePrivate_canReadLineFromBuffer(self->base.m_d);
}

static bool VXAbstractSocket_waitForReadyRead(XAbstractSocket* self, int msecs)
{
    // 通用实现：事件循环等待
    if (!self) return false;
    if (XAbstractSocket_bytesAvailable_base(self) > 0) return true;
    
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (XAbstractSocket_bytesAvailable_base(self) == 0) {
        if (self->state != XAbstractSocket_ConnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

static bool VXAbstractSocket_waitForBytesWritten(XAbstractSocket* self, int msecs)
{
    // 通用实现：事件循环等待
    if (!self) return false;
    if (XAbstractSocket_bytesToWrite_base(self) == 0) return true;
    
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (XAbstractSocket_bytesToWrite_base(self) > 0) {
        if (self->state != XAbstractSocket_ConnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

static bool VXAbstractSocket_atEnd(XAbstractSocket* self)
{
    // 通用实现：未连接或无数据可读
    if (!self) return true;
    if (self->state != XAbstractSocket_ConnectedState) return true;
    return XAbstractSocket_bytesAvailable_base(self) == 0;
}

static int64_t VXAbstractSocket_pos(const XAbstractSocket* self)
{
    (void)self;
    return 0; // 顺序设备无位置概念
}

static int64_t VXAbstractSocket_size(const XAbstractSocket* self)
{
    (void)self;
    return 0; // 未知大小
}

static bool VXAbstractSocket_seek(XAbstractSocket* self, int64_t pos)
{
    (void)self; (void)pos;
    return false; // 顺序设备不可 seek
}

static bool VXAbstractSocket_reset(XAbstractSocket* self)
{
    (void)self;
    return false; // 顺序设备不可 reset
}

static int64_t VXAbstractSocket_readLineData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    // 通用实现：从缓冲区读取一行
    if (!self || !self->base.m_d || maxlen <= 0) return -1;
    return XIODevicePrivate_readLineFromBuffer(self->base.m_d, data, maxlen);
}

static int64_t VXAbstractSocket_skipData(XAbstractSocket* self, int64_t maxSize)
{
    // 通用实现：从缓冲区跳过数据
    if (!self || !self->base.m_d || maxSize <= 0) return 0;
    int currentReadChannel = XIODevice_currentReadChannel((XIODevice*)self);
    struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(self->base.m_d, currentReadChannel);
    if (!readBuf) return 0;
    size_t available = XRingBuffer_available(readBuf);
    size_t toSkip = (maxSize > (int64_t)available) ? available : (size_t)maxSize;
    XRingBuffer_skip(readBuf, toSkip);
    return (int64_t)toSkip;
}

static bool VXAbstractSocket_event(XAbstractSocket* self, XEvent* e)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return false;

    if (e->type == XEVENT_TYPE_SOCK_ACT) {
        XEventSockAct* sockAct = (XEventSockAct*)e;
        //XPrintf("消费:%p %d\n", e, sockAct->actType);
        XNetwork_socketHandleEvent(priv, e);

        if (sockAct->actType & XSocketAct_Read) {
            // 检查是否在代理握手过程中
            if (self->proxyHandshakeCtx && !XNetworkProxyHandshake_isCompleted(self->proxyHandshakeCtx)) {
                // 处理代理握手读事件
                XProxyHandshakeState hsState = XNetworkProxyHandshake_process(self, self->proxyHandshakeCtx);
                
                if (hsState == XProxyHandshakeState_Completed) {
                    // 代理握手完成，进入连接状态
                    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                    XNetwork_socketContinueRead(priv, self->socketType == XAbstractSocket_UdpSocket);
                }
                else if (hsState == XProxyHandshakeState_Failed) {
                    // 代理握手失败
                    XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError,
                        XNetworkProxyHandshake_errorMessage(self->proxyHandshakeCtx));
                    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                }
                // 否则握手仍在进行中，继续等待
            }
            else {
                // 正常数据读取
                size_t bytesTransferred = XNetwork_socketReadFinishedBytes(priv);
                if (bytesTransferred > 0 && self->base.m_d) {
                    const char* readBuf = XNetwork_socketReadBuffer(priv);
                    int channel = XIODevice_currentReadChannel((XIODevice*)self);
                    struct XRingBuffer* rb = XIODevicePrivate_getOrCreateReadBuffer(self->base.m_d, channel);
                    if (rb && readBuf) {
                        XRingBuffer_write(rb, readBuf, bytesTransferred);
                        XIODevice_readyRead_signal((XIODevice*)self);
                    }
                }
                XNetwork_socketContinueRead(priv, self->socketType == XAbstractSocket_UdpSocket);
            }
        }

        if (sockAct->actType & XSocketAct_Write) {
            // 检查是否在代理握手过程中
            if (self->proxyHandshakeCtx && !XNetworkProxyHandshake_isCompleted(self->proxyHandshakeCtx)) {
                // 处理代理握手写事件
                XProxyHandshakeState hsState = XNetworkProxyHandshake_process(self, self->proxyHandshakeCtx);
                
                if (hsState == XProxyHandshakeState_Completed) {
                    // 代理握手完成，进入连接状态
                    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                }
                else if (hsState == XProxyHandshakeState_Failed) {
                    // 代理握手失败
                    XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError,
                        XNetworkProxyHandshake_errorMessage(self->proxyHandshakeCtx));
                    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                }
            }
            else {
                // 正常数据写入完成
                size_t bytesWritten = XNetwork_socketWriteFinishedBytes(priv);
                if (bytesWritten > 0) {
                    XIODevice_bytesWritten_signal((XIODevice*)self, bytesWritten);
                }
                /* 继续投递写环形缓冲区中残留数据 */
                {
                    int __wch = XIODevice_currentWriteChannel((XIODevice*)self);
                    struct XRingBuffer* __wrb = XIODevicePrivate_getOrCreateWriteBuffer(self->base.m_d, __wch);
                    XNetwork_socketContinueWrite(priv, __wrb, self->socketType == XAbstractSocket_UdpSocket);
                }
            }
        }

        if (sockAct->actType & XSocketAct_Connect) {
            if (XNetwork_socketIsConnected(priv)) {
                // 检查是否需要代理握手
                if (self->proxyHandshakeCtx) {
                    // 开始异步代理握手
                    if (!XNetworkProxyHandshake_start(self, self->proxyHandshakeCtx)) {
                        XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyProtocolError, 
                            XNetworkProxyHandshake_errorMessage(self->proxyHandshakeCtx));
                        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                        XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                        self->proxyHandshakeCtx = NULL;
                    }
                    // 握手开始，等待读写事件继续处理
                }
                else {
                    // 无代理，直接连接成功
                    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
                    XNetwork_socketContinueRead(priv, self->socketType == XAbstractSocket_UdpSocket);
                }
            }
            else {
                XAbstractSocket_setSocketError(self, XAbstractSocket_ConnectionRefusedError, "Connection failed");
                XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            }
        }

        return true;
    }
    else if (e->type == XEVENT_TYPE_SOCK_CLOSE) {
        XNetwork_socketDisconnect(priv);
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return true;
    }

    // 调用父类事件处理
    return XClass_Parent(XIODevice, EXObject_Event, bool (*)(XIODevice*, XEvent*))((XIODevice*)self, e);
}

// ==================== XAbstractSocket 特有虚函数 ====================
static void VXAbstractSocket_Resume(XAbstractSocket* self)
{
    // 非SSL套接字不需要特殊处理
    (void)self;
}

static bool VXAbstractSocket_Bind(XAbstractSocket* self, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode)
{
       XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return false;

    bool reuseAddr = (mode & XAbstractSocket_ShareAddress) || (mode & XAbstractSocket_ReuseAddressHint);
    bool shareAddr = (mode & XAbstractSocket_ShareAddress) != 0;

    uint16_t actualPort = XNetwork_socketBind(priv, address, port, reuseAddr, shareAddr,
        toNetworkSockType(self->socketType));
    
    if (actualPort == 0) {
        return false;
    }
    
    XAbstractSocket_setLocalAddress(self, address);
    XAbstractSocket_setLocalPort(self, actualPort);  // 使用实际端口
    XAbstractSocket_setSocketState(self, XAbstractSocket_BoundState);

    XClass_Parent(XIODevice, EXIODevice_Open, bool (*)(XIODevice*, XIODeviceBaseMode))(self, XIODevice_ReadWrite);
    return true;
}

static void VXAbstractSocket_ConnectToHost(XAbstractSocket* self, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol)
{
    (void)mode;
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return;

    XAbstractSocket_setPeerName(self, hostName);
    XAbstractSocket_setPeerPort(self, port);

    // 检查是否需要使用代理
    if (self->proxy.type != XNetworkProxy_NoProxy && self->proxy.type != XNetworkProxy_DefaultProxy) {
        // 使用代理连接
        if (!self->proxy.hostName || self->proxy.port == 0) {
            XAbstractSocket_setSocketError(self, XAbstractSocket_ProxyNotFoundError, "Proxy server not configured");
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        // 销毁旧的代理握手上下文
        if (self->proxyHandshakeCtx) {
            XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
        }
        
        // 创建代理握手上下文
        self->proxyHandshakeCtx = XNetworkProxyHandshake_createContext(
            &self->proxy, hostName, port);
        
        if (!self->proxyHandshakeCtx) {
            XAbstractSocket_setSocketError(self, XAbstractSocket_OperationError, "Failed to create proxy handshake context");
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        // 先连接到代理服务器
        XAbstractSocket_setSocketState(self, XAbstractSocket_HostLookupState);

        if (!XNetwork_socketConnect(priv, self->proxy.hostName, self->proxy.port, 
            toNetworkProtocol(protocol), toNetworkSockType(self->socketType))) {
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    }
    else {
        // 直接连接（无代理）
        XAbstractSocket_setSocketState(self, XAbstractSocket_HostLookupState);

        XString* hostStr = hostName ? XString_create_utf8(hostName) : NULL;
        if (!XNetwork_socketConnect(priv, hostStr, port, toNetworkProtocol(protocol),
            toNetworkSockType(self->socketType))) {
            XString_delete_base(hostStr);
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }
        XString_delete_base(hostStr);

        XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    }
}


static void VXAbstractSocket_DisconnectFromHost(XAbstractSocket* self)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return;
    
    XAbstractSocket_setSocketState(self, XAbstractSocket_ClosingState);
    XNetwork_socketDisconnect(priv);
    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
}

static intptr_t VXAbstractSocket_SocketDescriptor(const XAbstractSocket* self)
{
    return XNetwork_socketDescriptor(getPriv(self));
}

static bool VXAbstractSocket_SetSocketDescriptor(XAbstractSocket* self, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return false;
    
    if (!XNetwork_socketSetDescriptor(priv, socketDescriptor, state, openMode)) {
        return false;
    }
    
    XAbstractSocket_setSocketState(self, state);
    self->base.m_openMode = openMode;
    self->isValidFlag = (state == XAbstractSocket_ConnectedState);
    return true;
}

static void VXAbstractSocket_SetSocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option, const XVariant* value)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv || !value) return;

    /* 将 XVariant 转换为 int 值 */
    int intValue = 0;
    if (XVariant_type(value)== XVariantType_Int) {
        intValue = XVariant_toInt(value);
    }
    else if (XVariant_type(value) == XVariantType_Bool) {
        intValue = XVariant_toBool(value) ? 1 : 0;
    }

    XNetwork_socketSetOption(priv, (int)option, &intValue);
}

static XVariant* VXAbstractSocket_SocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option)
{
    XNetworkSocketPrivate* priv = getPriv(self);
    if (!priv) return NULL;

    void* result = XNetwork_socketGetOption(priv, (int)option);
    if (!result) return NULL;

    return XVariant_create_int(*(int*)result);
}


static void VXAbstractSocket_SetReadBufferSize(XAbstractSocket* self, int64_t size)
{
    if (!self) return;
    self->readBufferSize = size;
    
    XNetworkSocketPrivate* priv = getPriv(self);
    if (priv) {
        XNetwork_socketSetReadBufferSize(priv, size);
    }
}

static bool VXAbstractSocket_WaitForConnected(XAbstractSocket* self, int msecs)
{
    // 通用实现：事件循环等待连接状态
    if (!self) return false;
    if (self->state == XAbstractSocket_ConnectedState) return true;
    if (self->state == XAbstractSocket_UnconnectedState) return false;
    
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (self->state != XAbstractSocket_ConnectedState) {
        if (self->state == XAbstractSocket_UnconnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

static bool VXAbstractSocket_WaitForDisconnected(XAbstractSocket* self, int msecs)
{
    // 通用实现：事件循环等待断开状态
    if (!self) return true;
    if (self->state == XAbstractSocket_UnconnectedState) return true;
    
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (self->state != XAbstractSocket_UnconnectedState) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

// ==================== 信号实现 ====================

void* XAbstractSocket_hostFound_signal(XAbstractSocket* sock)
{
    XEmitSignal(sock, XAbstractSocket_hostFound_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractSocket_connected_signal(XAbstractSocket* sock)
{
    XEmitSignal(sock, XAbstractSocket_connected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractSocket_disconnected_signal(XAbstractSocket* sock)
{
    XEmitSignal(sock, XAbstractSocket_disconnected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractSocket_stateChanged_signal(XAbstractSocket* sock, XAbstractSocket_SocketState state)
{
    XEmitSignal(sock, XAbstractSocket_stateChanged_signal, XVarList_Create(XVar(XAbstractSocket_SocketState, state)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractSocket_errorOccurred_signal(XAbstractSocket* sock, XAbstractSocket_SocketError error)
{
    XEmitSignal(sock, XAbstractSocket_errorOccurred_signal, XVarList_Create(XVar(XAbstractSocket_SocketError, error)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

// ==================== Qt6 对齐：按地址直连（跳过 DNS）====================
void XAbstractSocket_connectToHostByAddress(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XIODeviceBaseMode mode)
{
    if (!sock || !address) return;
    XString* s = XHostAddress_toString(address);
    if (!s) return;
    const char* hostStr = XString_toUtf8(s);
    /* NetworkLayerProtocol: 依据地址决定 IPv4/IPv6，交给内部协议识别 */
    XAbstractSocket_connectToHost_base(sock, hostStr, port, mode, XAbstractSocket_AnyIPProtocol);
    XString_delete_base(s);
}

// ==================== Qt6 对齐：代理认证信号 ====================
void* XAbstractSocket_proxyAuthenticationRequired_signal(XAbstractSocket* sock, XNetworkProxy* proxy, void* authenticator)
{
    XEmitSignal(sock, XAbstractSocket_proxyAuthenticationRequired_signal,
                XVarList_Create(XVar(void*, proxy), XVar(void*, authenticator)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XAbstractSocket_proxyAuthenticationRequired_signal;
}
