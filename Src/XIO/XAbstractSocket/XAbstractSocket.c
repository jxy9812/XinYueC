// XAbstractSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XAbstractSocket.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XIODevicePrivate.h"
#include <string.h>
#include <assert.h>

// ==================== 内部辅助函数声明 ====================
static void VXAbstractSocket_deinit(XAbstractSocket* sock);
static bool VXAbstractSocket_open(XAbstractSocket* self, XIODeviceBaseMode mode);
static void VXAbstractSocket_close(XAbstractSocket* self);
static int64_t VXAbstractSocket_readData(XAbstractSocket* self, char* data, int64_t maxlen);
static int64_t VXAbstractSocket_writeData(XAbstractSocket* self, const char* data, int64_t len);
static bool VXAbstractSocket_isSequential(const XAbstractSocket* self);
//static int64_t VXAbstractSocket_bytesAvailable(const XAbstractSocket* self);
//static bool VXAbstractSocket_canReadLine(const XAbstractSocket* self);
static bool VXAbstractSocket_waitForReadyRead(XAbstractSocket* self, int msecs);
static bool VXAbstractSocket_waitForBytesWritten(XAbstractSocket* self, int msecs);
static bool VXAbstractSocket_atEnd(XAbstractSocket* self);
static int64_t VXAbstractSocket_pos(const XAbstractSocket* self);
static int64_t VXAbstractSocket_size(const XAbstractSocket* self);
static bool VXAbstractSocket_seek(XAbstractSocket* self, int64_t pos);
static bool VXAbstractSocket_reset(XAbstractSocket* self);
static int64_t VXAbstractSocket_readLineData(XAbstractSocket* self, char* data, int64_t maxlen);
static int64_t VXAbstractSocket_skipData(XAbstractSocket* self, int64_t maxSize);
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
    XVTABLE_INHERIT_DEFAULT(XIODevice_class_init());
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
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VXAbstractSocket_bytesAvailable);
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

#if SHOWCONTAINERSIZE
        printf("XAbstractSocket size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

// ==================== 析构函数 ====================
static void VXAbstractSocket_deinit(XAbstractSocket* sock)
{
    if (!sock) return;

    // 安全关闭（触发 aboutToClose 信号）
    if (sock->base.m_openMode != XIODevice_NotOpen) {
        if (!sock->base.m_d->aboutToCloseEmitted) {
            XIODevice_aboutToClose_signal((XIODevice*)sock);
            sock->base.m_d->aboutToCloseEmitted = true;
        }
        sock->base.m_openMode = XIODevice_NotOpen;
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

    // 清理私有数据（PIMPL）——由子类负责释放 d_ptr，此处不处理
    // if (sock->d_ptr) { ... }

    // 释放 platformHandle（如果需要）
    // 注意：通常由 setSocketDescriptor 的用户管理，此处不 close()

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
    XHostAddress_setAddressSpecial(&sock->localAddress, XHostAddress_NullSpecial);
    sock->localPort = 0;
    XHostAddress_setAddressSpecial(&sock->peerAddress, XHostAddress_NullSpecial);
    sock->peerPort = 0;
    sock->peerName = NULL;
    sock->readBufferSize = -1; // 无限制
    sock->m_pauseMode = XAbstractSocket_PauseNever;
    sock->autoDeleteOnDisconnect = false;
    sock->isValidFlag = false;
    sock->d_ptr = NULL; // 由子类初始化
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

// ==================== 受保护的 setter ====================
void XAbstractSocket_setLocalPort(XAbstractSocket* sock, uint16_t port)
{
    if (sock) sock->localPort = port;
}

void XAbstractSocket_setLocalAddress(XAbstractSocket* sock, const XHostAddress* address)
{
    if (sock && address) {
        sock->localAddress = *XHostAddress_create_copy(address);
    }
}

void XAbstractSocket_setPeerPort(XAbstractSocket* sock, uint16_t port)
{
    if (sock) sock->peerPort = port;
}

void XAbstractSocket_setPeerAddress(XAbstractSocket* sock, const XHostAddress* address)
{
    if (sock && address) {
        sock->peerAddress = *XHostAddress_create_copy(address);
    }
}

void XAbstractSocket_setPeerName(XAbstractSocket* sock, const char* name)
{
    if (!sock) return;
    if (sock->peerName) {
        XString_delete_base(sock->peerName);
        sock->peerName = NULL;
    }
    if (name) {
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
    if (oldState == XAbstractSocket_HostLookupState && state == XAbstractSocket_ConnectedState) {
        XAbstractSocket_hostFound_signal(sock);
        XAbstractSocket_connected_signal(sock);
    }
    else if (state == XAbstractSocket_UnconnectedState && oldState != XAbstractSocket_UnconnectedState) {
        XAbstractSocket_disconnected_signal(sock);
        if (sock->autoDeleteOnDisconnect) {
            XAbstractSocket_delete_base(sock);
        }
    }
}

void XAbstractSocket_setSocketError(XAbstractSocket* sock, XAbstractSocket_SocketError error, const char* str)
{
    if (!sock) return;
    sock->error = error;
    if (sock->errorString) {
        XString_delete_base(sock->errorString);
        sock->errorString = NULL;
    }
    if (str) {
        sock->errorString = XString_create_utf8(str);
    }

    // 发射 errorOccurred 信号
    XAbstractSocket_errorOccurred_signal(sock, error);
}

// ==================== 核心操作（存根，由子类实现）====================
bool XAbstractSocket_bind(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_Bind, bool(*)(XAbstractSocket*, const XHostAddress*, uint16_t, XAbstractSocket_BindMode))(sock, address,port, mode);
}

bool XAbstractSocket_bindAny(XAbstractSocket* sock, uint16_t port, XAbstractSocket_BindMode mode)
{
    XHostAddress any;
    if (sock->socketType == XAbstractSocket_UdpSocket) {
        XHostAddress_setAddressSpecial(&any, XHostAddress_AnySpecial);
    }
    else {
        XHostAddress_setAddressSpecial(&any, XHostAddress_AnyIPv6Special);
    }
    return XAbstractSocket_bind(sock, &any, port, mode);
}

void XAbstractSocket_connectToHost_base(XAbstractSocket* sock, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return ;
    XClassGetVirtualFunc(sock, EXAbstractSocket_ConnectToHost, bool(*)(XAbstractSocket*, const char*, uint16_t, XAbstractSocket_BindMode, XAbstractSocket_NetworkLayerProtocol))(sock, hostName, port, mode, protocol);
}

void XAbstractSocket_connectToAddress(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XIODeviceBaseMode mode)
{
    (void)sock; (void)address; (void)port; (void)mode;
    // 抽象类，子类必须实现
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
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return 0;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SocketDescriptor, intptr_t(*)(XAbstractSocket*))(sock);
}

bool XAbstractSocket_setSocketDescriptor_base(XAbstractSocket* sock, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SetSocketDescriptor, bool (*)(XAbstractSocket*, intptr_t, XAbstractSocket_SocketState, XIODeviceBaseMode))(sock, socketDescriptor, state, openMode);
}

// ==================== 同步等待（存根）====================
bool XAbstractSocket_waitForConnected_base(XAbstractSocket* sock, int msecs)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    XClassGetVirtualFunc(sock, EXAbstractSocket_WaitForConnected, bool (*)(XAbstractSocket*, int))(sock, msecs);
}

bool XAbstractSocket_waitForDisconnected_base(XAbstractSocket* sock, int msecs)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    XClassGetVirtualFunc(sock, EXAbstractSocket_WaitForDisconnected, bool (*)(XAbstractSocket*, int))(sock, msecs);
}

bool XAbstractSocket_waitForBytesWritten(XAbstractSocket* sock, int msecs)
{
    return XIODevice_waitForBytesWritten_base((XIODevice*)sock, msecs);
}

bool XAbstractSocket_flush(XAbstractSocket* sock)
{
    (void)sock;
    return true; // 抽象类视为成功
}

// ==================== 虚函数重载 ====================

static bool VXAbstractSocket_open(XAbstractSocket* self, XIODeviceBaseMode mode)
{
    // 抽象套接字不能直接 open，必须通过 connect/bind
    return false;
}

static void VXAbstractSocket_close(XAbstractSocket* self)
{
    if (!self) return;
    // 重置状态
    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
    self->isValidFlag = false;
    // 调用父类 close（清空缓冲区等）
    XIODevice_close_base((XIODevice*)self);
}

static int64_t VXAbstractSocket_readData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    (void)self; (void)data; (void)maxlen;
    // 抽象类，子类必须实现
    return -1;
}

static int64_t VXAbstractSocket_writeData(XAbstractSocket* self, const char* data, int64_t len)
{
    (void)self; (void)data; (void)len;
    // 抽象类，子类必须实现
    return -1;
}

static bool VXAbstractSocket_isSequential(const XAbstractSocket* self)
{
    (void)self;
    return true; // 所有套接字都是顺序设备
}

//static int64_t VXAbstractSocket_bytesAvailable(const XAbstractSocket* self)
//{
//    if (!self || !self->base.m_d) return 0;
//    // 返回缓冲区中可读字节数
//    return XIODevice_bytesAvailable_base((XIODevice*)self);
//}

//static bool VXAbstractSocket_canReadLine(const XAbstractSocket* self)
//{
//    if (!self || !self->base.m_d) return false;
//    return XIODevice_canReadLine_base((XIODevice*)self);
//}

static bool VXAbstractSocket_waitForReadyRead(XAbstractSocket* self, int msecs)
{
    (void)self; (void)msecs;
    // 默认不支持，子类可重载
    return false;
}

static bool VXAbstractSocket_waitForBytesWritten(XAbstractSocket* self, int msecs)
{
    (void)self; (void)msecs;
    return false;
}

static bool VXAbstractSocket_atEnd(XAbstractSocket* self)
{
    // 顺序设备，只要未连接或出错即为 end
    if (!self) return true;
    if (self->state != XAbstractSocket_ConnectedState) return true;
    return XClassGetVirtualFunc(self, EXIODevice_AtEnd, bool(*)(XIODevice*))(self);
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

int64_t VXAbstractSocket_readLineData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    (void)self; (void)data; (void)maxlen;
    return -1; // 默认回退到 readData
}

int64_t VXAbstractSocket_skipData(XAbstractSocket* self, int64_t maxSize)
{
    (void)self; (void)maxSize;
    return 0; // 默认不支持跳过
}

// ==================== XAbstractSocket 特有虚函数（默认实现）====================
static void VXAbstractSocket_Resume(XAbstractSocket* self)
{
    (void)self; // 默认空操作
}
static bool VXAbstractSocket_Bind(XAbstractSocket* self, const XHostAddress* address, uint16_t port, XAbstractSocket_BindMode mode)
{
    (void)self; (void)address; (void)port; (void)mode;
    return false; // 抽象类，子类实现
}
static void VXAbstractSocket_ConnectToHost(XAbstractSocket* self, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol)
{
    (void)self; (void)hostName; (void)port; (void)mode; (void)protocol;
    // 应触发 HostLookupState → ConnectedState，但抽象类不实现
}
static void VXAbstractSocket_DisconnectFromHost(XAbstractSocket* self)
{
    (void)self;
    XAbstractSocket_abort(self); // 默认行为：abort
}
static intptr_t VXAbstractSocket_SocketDescriptor(const XAbstractSocket* self)
{
    return NULL;//子类需要实现
}
static bool VXAbstractSocket_SetSocketDescriptor(XAbstractSocket* self, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    if (!self) return false;
    //self->platformHandle = (void*)socketDescriptor;
    XAbstractSocket_setSocketState(self, state);
    self->base.m_openMode = openMode;
    self->isValidFlag = (state == XAbstractSocket_ConnectedState || state == XAbstractSocket_BoundState);
    return true;
}
static void VXAbstractSocket_SetSocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option, const XVariant* value)
{
    (void)self; (void)option; (void)value;
    // 默认忽略
}
static XVariant* VXAbstractSocket_SocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option)
{
    (void)self; (void)option;
    return NULL; // 默认不支持
}
static void VXAbstractSocket_SetReadBufferSize(XAbstractSocket* self, int64_t size)
{
    if (self) self->readBufferSize = size;
}
static bool VXAbstractSocket_WaitForConnected(XAbstractSocket* self, int msecs)
{
    (void)self; (void)msecs;
    return false; // 默认不支持
}
static bool VXAbstractSocket_WaitForDisconnected(XAbstractSocket* self, int msecs)
{
    (void)self; (void)msecs;
    return false; // 默认不支持
}

// ==================== 信号实现 ====================

void* XAbstractSocket_hostFound_signal(XAbstractSocket* sock)
{
    if (sock) {
        XObject_emitSignal((XObject*)sock, XAbstractSocket_hostFound_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XAbstractSocket_hostFound_signal;
}

void* XAbstractSocket_connected_signal(XAbstractSocket* sock)
{
    if (sock) {
        XObject_emitSignal((XObject*)sock, XAbstractSocket_connected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XAbstractSocket_connected_signal;
}

void* XAbstractSocket_disconnected_signal(XAbstractSocket* sock)
{
    if (sock) {
        XObject_emitSignal((XObject*)sock, XAbstractSocket_disconnected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XAbstractSocket_disconnected_signal;
}

void* XAbstractSocket_stateChanged_signal(XAbstractSocket* sock, XAbstractSocket_SocketState state)
{
    if (sock) {
        XVariant* var = XVariant_create_int((int)state);
        XObject_emitSignal((XObject*)sock, XAbstractSocket_stateChanged_signal, var, XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XAbstractSocket_stateChanged_signal;
}

void* XAbstractSocket_errorOccurred_signal(XAbstractSocket* sock, XAbstractSocket_SocketError error)
{
    if (sock) {
        XVariant* var = XVariant_create_int((int)error);
        XObject_emitSignal((XObject*)sock, XAbstractSocket_errorOccurred_signal, var, XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XAbstractSocket_errorOccurred_signal;
}