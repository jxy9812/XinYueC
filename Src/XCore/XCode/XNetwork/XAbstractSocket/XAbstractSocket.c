// XAbstractSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XAbstractSocket.h"
#include "XDeviceNetwork.h"
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
#include "XHostInfo.h"
#include <string.h>
#include <assert.h>
#if XNETWORK_ON
#if XNETWORK_ABSTRACT_SOCKET_ON

/* SocketType 转换：XAbstractSocket_SocketType -> XDeviceNetworkSocketType */
static XDeviceNetworkSocketType toNetworkSockType(XAbstractSocket_SocketType type)
{
    return (type == XAbstractSocket_UdpSocket) ? XDeviceNetwork_Udp : XDeviceNetwork_Tcp;
}

/* Protocol 转换：XAbstractSocket_NetworkLayerProtocol -> XDeviceNetworkProtocol */
static XDeviceNetworkProtocol toNetworkProtocol(XAbstractSocket_NetworkLayerProtocol protocol)
{
    switch (protocol) {
    case XAbstractSocket_IPv4Protocol: return XDeviceNetwork_IPv4;
    case XAbstractSocket_IPv6Protocol: return XDeviceNetwork_IPv6;
    default: return XDeviceNetwork_Any;
    }
}

static XFd socketDeviceFd(const XAbstractSocket* socket)
{
    return socket ? socket->m_deviceFd : XFD_INVALID;
}

static bool socketGetBoolProperty(const XAbstractSocket* socket, XDeviceProperty property, bool* value)
{
    XVariant result;
    if (!socket || !value || socketDeviceFd(socket) == XFD_INVALID) return false;
    /* 桶规约: 必须走 XVariant_init(建 vtable)+setValue_null(经 XClassDeinit
     * 释放值槽)。旧 memset+XVariant_clear 组合只清内容不释放——ASan 实测
     * 每次 getProperty 泄漏值槽堆块(远程会话统计采样随时长无上限增长)。
     * 【真机修复 2026-10-04】init 前必须 memset: XVariant_init 对传入结构
     * 自带「旧 m_data 防御性释放」(XVariant.c:88-92), 未初始化栈对象该字段
     * 为栈残影——armel Release(-O3) 热栈槽残留陈旧非空指针, init 内直接
     * XFree_System(垃圾指针) → glibc "free(): invalid pointer" 启动即崩
     * (Debug 栈区恰逢全零故不现形; 真机复验: 5 处补 memset 后干净监听)。 */
    memset(&result, 0, sizeof(result));
    XVariant_init(&result, NULL, 0, XVariantType_NULL);
    if (!XDevice_getProperty(socketDeviceFd(socket), property, &result)) {
        XVariant_setValue_null(&result);
        return false;
    }
    *value = XVariant_toBool(&result);
    XVariant_setValue_null(&result);
    return true;
}

static bool socketGetSizeProperty(const XAbstractSocket* socket, XDeviceProperty property, size_t* value)
{
    XVariant result;
    if (!socket || !value || socketDeviceFd(socket) == XFD_INVALID) return false;
    memset(&result, 0, sizeof(result)); /* fix(r7crash): 见 socketGetBoolProperty 真机修复注。 */
    XVariant_init(&result, NULL, 0, XVariantType_NULL);
    if (!XDevice_getProperty(socketDeviceFd(socket), property, &result)) {
        XVariant_setValue_null(&result);
        return false;
    }
    *value = XVariant_toSize_t(&result);
    XVariant_setValue_null(&result);
    return true;
}

static bool socketControlEvent(XAbstractSocket* socket, XEvent* event)
{
    XVarList* input;
    bool ok;
    if (!socket || !event || socketDeviceFd(socket) == XFD_INVALID) return false;
    input = XVarList_Create(XVar(XEvent*, event));
    if (!input) return false;
    ok = XDevice_control(socketDeviceFd(socket), XDeviceNetworkCommand_HandleEvent, input, NULL);
    XVarList_delete(input);
    return ok;
}

static bool socketControlNoArgs(XAbstractSocket* socket, XDeviceNetworkCommand command)
{
    return socket && socketDeviceFd(socket) != XFD_INVALID &&
        XDevice_control(socketDeviceFd(socket), command, NULL, NULL);
}

static const char* socketReadBuffer(XAbstractSocket* socket)
{
    const char* buffer = NULL;
    XVarList* output;
    bool ok;
    if (!socket || socketDeviceFd(socket) == XFD_INVALID) return NULL;
    output = XVarList_Create(XVar(const char*, buffer));
    if (!output) return NULL;
    ok = XDevice_control(socketDeviceFd(socket), XDeviceNetworkCommand_GetReadBuffer, NULL, output);
    if (ok) {
        XVarList_start(output);
        buffer = XVarList_arg(output, const char*);
    }
    XVarList_delete(output);
    return buffer;
}

static void socketCloseDevice(XAbstractSocket* socket)
{
    if (!socket || socketDeviceFd(socket) == XFD_INVALID) return;
    XDevice_close(socket->m_deviceFd);
    socket->m_deviceFd = XFD_INVALID;
}

static XFd socketOpenDevice(XAbstractSocket* socket, XDeviceNetworkOpenOptions* options, int* error)
{
    XFd fd;
    if (!socket || !options) return XFD_INVALID;
    socketCloseDevice(socket);
    options->m_owner = socket;
    fd = XDevice_open(XDeviceType_Socket, &options->m_base, error);
    if (fd != XFD_INVALID)
        socket->m_deviceFd = fd;
    return fd;
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
/* 主机名解析 / 断开收尾（实现在 ConnectToHost 之前） */
static void socketCancelHostLookup(XAbstractSocket* self);
static bool socketWriteIdle(const XAbstractSocket* self);
static void socketFinishDisconnectIfIdle(XAbstractSocket* self);
static bool socketStartConnect(XAbstractSocket* self, const XHostAddress* address,
                               uint16_t port, XIODeviceBaseMode mode,
                               XAbstractSocket_NetworkLayerProtocol protocol);
static void socketCompleteHostLookup(XAbstractSocket* self);
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
    XVTABLE_INIT_DEFAULT(XAbstractSocket)
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
    /* `EXIODevice_BytesAvailable` 不重载：基类 `VXIODevice_bytesAvailable`
     * （XIODevice_virtual.c:139）已经是「直接返回当前读通道 ring buffer
     * 的可用字节数」，正是套接字需要的语义。若在此重载并回调
     * `XIODevice_bytesAvailable_base`，会经虚表回到本类自身形成无限递归
     * （实测栈溢出；Qt 里 `QIODevice::bytesAvailable()` 调的是非虚的基类
     * 实现，不存在该问题）。 */
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

        XCLASS_SHOW_SIZE_DEFAULT(XAbstractSocket);
        return XVTABLE_DEFAULT;
}

// ==================== 析构函数 ====================
static void VXAbstractSocket_deinit(XAbstractSocket* sock)
{
    if (!sock) return;
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_sendPostedEvents(sock, 0);
    socketCloseDevice(sock);

    // 清理自身资源
    if (sock->errorString) {
        XClassDelete(sock->errorString);
        sock->errorString = NULL;
    }
    if (sock->peerName) {
        XClassDelete(sock->peerName);
        sock->peerName = NULL;
    }
    if (sock->protocolTag) {
        XClassDelete(sock->protocolTag);
        sock->protocolTag = NULL;
    }
    /* 在途主机名解析：撤销待处理事件并释放目标名（不遗留悬空事件）。 */
    if (sock->hostLookupPending) {
        XCoreApplication_removePostedEvents((XObject*)sock, XEVENT_TYPE_HOST_LOOKUP);
        sock->hostLookupPending = false;
    }
    if (sock->lookupHostName) {
        XClassDelete(sock->lookupHostName);
        sock->lookupHostName = NULL;
    }
    // 释放代理配置资源
    XClassDeinit(&sock->proxy);
    // 释放代理握手上下文
    if (sock->proxyHandshakeCtx) {
        XNetworkProxyHandshake_destroyContext(sock->proxyHandshakeCtx);
        sock->proxyHandshakeCtx = NULL;
    }
    // 释放地址对象
    XClassDeinit((XHostAddress*)&sock->localAddress);
    XClassDeinit((XHostAddress*)&sock->peerAddress);
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
    sock->disconnectPending = false;
    sock->readPaused = false;
    sock->hostLookupPending = false;
    sock->lookupEmitHostFound = true;
    sock->lookupHostName = NULL;
    sock->lookupPort = 0;
    sock->lookupMode = (int32_t)XIODevice_ReadWrite;
    sock->lookupProtocol = (int32_t)XAbstractSocket_AnyIPProtocol;
    sock->protocolTag = NULL;
    XNetworkProxy_init(&sock->proxy);
    sock->proxyHandshakeCtx = NULL;
    sock->d_ptr = NULL;
    sock->m_deviceFd = XFD_INVALID;
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
    /* Qt 6.8（qabsock.cpp:1595）：引擎有效即有效（连接中/已连接/已绑定都算），
     * 无引擎时退回 isOpen()。本框架以「网络设备句柄已打开」等价引擎有效；
     * `isValidFlag` 保留给通过描述符接管等特殊路径。 */
    if (!sock) return false;
    if (socketDeviceFd(sock) != XFD_INVALID) return true;
    return sock->isValidFlag;
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
        XClassDelete(sock->protocolTag);
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

    /* 经四件套入口整体替换嵌入 proxy（对齐 XTcpServer_setProxy 标准写法）：
     * XAbstractSocket_init 已对 sock->proxy 调 XNetworkProxy_init（vtable 就绪），
     * deinit 释放旧载荷、init 复位空壳，字符串经 XNetworkProxy_set* 深拷贝，
     * 不再逐字段手工释放 + 复制。 */
    XClassDeinit(&sock->proxy);
    XNetworkProxy_init(&sock->proxy);
    if (proxy) {
        sock->proxy.type = proxy->type;
        sock->proxy.capabilities = proxy->capabilities;
        sock->proxy.port = proxy->port;
        if (proxy->hostName) {
            XNetworkProxy_setHostName(&sock->proxy, proxy->hostName);
        }
        if (proxy->user) {
            XNetworkProxy_setUser(&sock->proxy, proxy->user);
        }
        if (proxy->password) {
            XNetworkProxy_setPassword(&sock->proxy, proxy->password);
        }
    }
}

XNetworkProxy* XAbstractSocket_proxy(const XAbstractSocket* sock)
{
    return sock ? (XNetworkProxy*)&sock->proxy : NULL;
}

// flush - 刷新发送缓冲区，等待所有待发送数据发送完毕
bool XAbstractSocket_flush(XAbstractSocket* sock)
{
    bool writePending;
    if (!sock) return false;
    XIODevice* io = (XIODevice*)sock;

    // 检查是否有待发送数据：既包括 XIODevice 环形缓冲队列，也包括平台
    // 已提交但尚未完成的异步写请求（io_uring/IOCP 的 SEND 仍在内核队列时，
    // bytesToWrite 可能已经是 0，但 writePending 仍为 true，直接关闭会丢包）。
    while (XIODevice_bytesToWrite_base(io) > 0 ||
           (socketGetBoolProperty(sock, (XDeviceProperty)XDeviceNetworkProperty_WritePending, &writePending) && writePending)) {
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
        XClassCopy(&sock->localAddress, address);
    }
}

void XAbstractSocket_setPeerPort(XAbstractSocket* sock, uint16_t port)
{
    if (sock) sock->peerPort = port;
}

void XAbstractSocket_setPeerAddress(XAbstractSocket* sock, const XHostAddress* address)
{
    if (sock && address) {
        XClassCopy(&sock->peerAddress, address);
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
        /* Qt 的 isValid()（qabsock.cpp:1595）在引擎有效期间恒为 true；
         * 本框架以「设备句柄有效"或"已连接/已绑定」为准（见 isValid）。 */
        sock->isValidFlag = true;
        XIODevice_open_base(sock, XIODevice_ReadWrite);
        /* hostFound 不再在此发射：[2026-11-04] 对齐 Qt 的时序——它在
         * **主机名解析成功后、发起 connect 之前**发射（qabsock.cpp:979），
         * 由 socketCompleteHostLookup 负责；「HostLookup→Connected」这条
         * 转换在正常 connectToHost 下永远不会发生（中间必经过
         * ConnectingState），原实现因此从未发出过该信号。 */
        // 无论从哪个状态转换到 ConnectedState，都发出 connected 信号
        XAbstractSocket_connected_signal(sock);
    }
    else if (state == XAbstractSocket_UnconnectedState && oldState != XAbstractSocket_UnconnectedState)
    {
        /* [2026-11-04 Qt 6.8 对齐] 远端 FIN 不关闭 QIODevice 的 openMode。
         * Qt 的 `disconnectFromHost`（qabsock.cpp:2612）只 reset socket 引擎
         * 并置 UnconnectedState；**只有显式 `QAbstractSocket::close()`
         * （qabsock.cpp:2593）才调 `QIODevice::close()` 清 openMode**。
         * 原实现在这里把 m_openMode 置 NotOpen，于是「数据先到、FIN 后到」
         * 的时序下（HTTP/1.0 Connection: close 的常态），已经取进 ring buffer
         * 的字节会被 `XIODevice_read_1` 的 CHECK_READABLE 拒掉，返回 -1 ⇒
         * 应用侧看到空应答（实测 DDNS 15 项失败、XLink 文件传输全断）。
         * 显式关闭路径由 `VXAbstractSocket_close` → 基类 `XIODevice_close` 负责。 */
        sock->isValidFlag = false;
        XAbstractSocket_disconnected_signal(sock);
        if (sock->autoDeleteOnDisconnect) {
            XAbstractSocket_deleteLater(sock);
        }
    }
    else if (state == XAbstractSocket_BoundState)
    {
        /* Qt：bind 成功后引擎有效 ⇒ isValid() 为 true。 */
        sock->isValidFlag = true;
    }
}

/**
 * @brief 只写错误字段，不发 errorOccurred 信号。
 * @note 对齐 Qt 6.8 `QAbstractSocket::setSocketError`（qabsock.cpp:2797）：
 *       受保护 setter 只赋值；信号由私有的 `setErrorAndEmit` 发射。
 *       需要“写字段 + 发信号”的调用方用 `XAbstractSocket_setSocketErrorAndEmit`。
 */
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
}

/**
 * @brief 写错误字段并发 errorOccurred 信号（对标 Qt 私有 setErrorAndEmit）。
 */
void XAbstractSocket_setSocketErrorAndEmit(XAbstractSocket* sock, XAbstractSocket_SocketError error, const char* str)
{
    if (!sock) return;
    XAbstractSocket_setSocketError(sock, error, str);
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
    /* [Qt 6.8 对齐] `QAbstractSocket::bind(quint16 port, BindMode)`
     * （qabsock.cpp:1580）等价于 `bind(QHostAddress::Any, port, mode)`：
     * Any 走双栈（AF_INET6 + IPV6_V6ONLY=0），**不分 TCP/UDP**。
     * 原实现给 UDP 选 IPv4-Any，导致绑定后收不到 IPv6 数据报。 */
    XHostAddress_setAddressSpecial(&any, XHostAddress_AnyIPv6Special);
    bool result = XAbstractSocket_bind_base(sock, &any, port, mode);
    XClassDeinit(&any);
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

/**
 * @brief 立即中止套接字（对标 Qt 6.8 `QAbstractSocket::abort`，
 *        qabsock.cpp:2346）。
 * @details Qt 实现：
 *          ```
 *          d->setWriteChannelCount(0);   // 丢弃待写通道
 *          d->abortCalled = true;        // 标记「不等残留数据」
 *          close();                      // 走 close 统一收尾
 *          ```
 *          原实现顺序相反（先把 state 直接改成 UnconnectedState 再 close），
 *          跳过了「丢弃待写」这一步，且绕开了 close 的完整收尾。
 *
 *          本框架的对应关系：
 *          - `setWriteChannelCount(0)` → 关闭时由平台层 `socketDisconnect`
 *            丢弃 ring buffer 在途写（已在后端实现）；
 *          - `abortCalled` 语义 → 直接 close（不等数据），由
 *            `XIODevice_close_base` 触发 `VXAbstractSocket_close` 的完整
 *            收尾（关设备句柄 + 复位状态）。
 * @note 状态复位交给 `VXAbstractSocket_close` 之后统一按 Unconnected 处理；
 *       这里不抢先改 state，避免「状态已 Unconnected 但设备仍打开」的
 *       中间态被上层观察到。
 */
void XAbstractSocket_abort(XAbstractSocket* sock)
{
    if (!sock) return;
    socketCancelHostLookup(sock);
    sock->disconnectPending = false;
    XIODevice_close_base((XIODevice*)sock);
    XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
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
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), "")|| XVtable_size(XClassGetVtable(sock))<= EXAbstractSocket_SocketDescriptor)
        return 0;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SocketDescriptor, intptr_t(*)(XAbstractSocket*))(sock);
}

bool XAbstractSocket_setSocketDescriptor_base(XAbstractSocket* sock, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    if (ISNULL(sock, "") || ISNULL(XClassGetVtable(sock), ""))
        return false;
    return XClassGetVirtualFunc(sock, EXAbstractSocket_SetSocketDescriptor, bool (*)(XAbstractSocket*, intptr_t, XAbstractSocket_SocketState, XIODeviceBaseMode))(sock, socketDescriptor, state, openMode);
}

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
    socketCancelHostLookup(self);
    self->disconnectPending = false;
    if (socketDeviceFd(self) != XFD_INVALID) {
        socketCloseDevice(self);
        self->isValidFlag = false;
    }
    XClass_Parent(XIODevice, EXIODevice_Close, void (*)(XIODevice*))((XIODevice*)self);
    /* [Qt 6.8 对齐] `QAbstractSocket::close()`（qabsock.cpp:2593）=
     * `QIODevice::close()` + 状态未断开时走 `disconnectFromHost()`；后者
     * 最终把状态落到 UnconnectedState 并发 disconnected。原实现只关设备，
     * 状态机停在 Connected/Closing，调用方观察到"已关闭但仍已连接"。 */
    if (self->state != XAbstractSocket_UnconnectedState)
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
}

/**
 * @brief 底层读（对标 Qt 6.8 `QAbstractSocket::readData`，qabsock.cpp:2385）。
 * @details Qt 语义：
 *          - 未连接 / 引擎无效 ⇒ `maxSize ? -1 : 0`（-1 表示 EOF）。
 *          - 引擎读到 EAGAIN（Qt 引擎返回 -2）⇒ 转 0，即「暂无数据」，
 *            由上层稍后再读。**不是错误**。
 *          - 引擎读出错（< 0）⇒ 置错误、复位套接字层、转 Unconnected。
 * @note 本实现把「设备层返回的『暂不可读』」统一折算为 0：平台后端用
 *       `-1` 同时表达「真错误」和「背压/暂无数据」，而 Qt 用 -2 区分。
 *       按 Qt 语义，**暂无数据必须返回 0**，否则调用方会把正常背压
 *       当成读失败（实测 DDNS/HTTP 逐段收包因此整段丢弃）。
 */
static int64_t VXAbstractSocket_readData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    int64_t got;
    if (!self) return maxlen ? -1 : 0;
    if (socketDeviceFd(self) == XFD_INVALID)
        return maxlen ? -1 : 0;             /* Qt：未连接按 EOF 处理。 */
    if (maxlen == 0)
        return 0;

    /* [Qt 6.8 对齐] 读缓冲曾因 setReadBufferSize 上限暂停（readPaused）：
     * 应用开始消费、缓冲低于上限 ⇒ 恢复读通知。Qt 在
     * setReadBufferSize/读走数据后重新使能读通知（qabsock.cpp:2736-2747）。 */
    if (self->readPaused
        && (self->readBufferSize <= 0
            || XIODevice_bytesAvailable_base((XIODevice*)self) < self->readBufferSize)) {
        self->readPaused = false;
        socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
    }

    got = XDevice_read(socketDeviceFd(self), data, maxlen);
    if (got == 0)
        return 0;                            /* 暂无数据（等价 Qt 引擎 -2 → 0）。 */
    return got;
}

static int64_t VXAbstractSocket_writeData(XAbstractSocket* self, const char* data, int64_t len)
{
    if (!self || socketDeviceFd(self) == XFD_INVALID) return -1;
    return XDevice_write(socketDeviceFd(self), data, len);
}

static bool VXAbstractSocket_isSequential(const XAbstractSocket* self)
{
    (void)self;
    return true; // 所有套接字都是顺序设备
}


/* [死码清理] VXAbstractSocket_canReadLine 已删除：与基类缺省
 * VXIODevice_canReadLine（XIODevice_virtual.c:173，同为
 * XIODevicePrivate_canReadLineFromBuffer）逐字同体，挂接行注释后成
 * 孤儿；基类经虚表分派已覆盖 socket 语义，无需第二份。 */

/**
 * @brief 阻塞等待可读（对标 Qt 6.8 `QAbstractSocket::waitForReadyRead`，
 *        qabsock.cpp:2139）。
 * @details Qt 语义要点（原实现三处偏差）：
 *          1. **UnconnectedState ⇒ 立即 false**（不是等待超时）。
 *          2. **ConnectingState / HostLookupState ⇒ 先 waitForConnected**；
 *             连接失败直接 false。原实现把它当「非 Connected 就 false」，
 *             会让「connectToHost 后立刻 waitForReadyRead」的调用方
 *             （DDNS/HTTP 的同步 GET）在连接完成的瞬间失败退出。
 *          3. 循环退出条件是 `deadline` 到期，**且每轮都必须让出事件循环**
 *             以驱动平台 I/O 与 CQ 派发；`msecs < 0` 表示不超时。
 * @note 本实现沿用框架的 `processEvents` 驱动模型（Qt 走引擎的
 *       `waitForReadOrWrite`），但判定顺序与返回值语义与 Qt 一致。
 */
static bool VXAbstractSocket_waitForReadyRead(XAbstractSocket* self, int msecs)
{
    uint64_t deadline;
    if (!self) return false;


    /* 1. Qt：未连接直接 false。 */
    if (self->state == XAbstractSocket_UnconnectedState) return false;

    /* 2. Qt：仍在连接中 ⇒ 先等连接完成。 */
    if (self->state == XAbstractSocket_HostLookupState ||
        self->state == XAbstractSocket_ConnectingState) {
        if (!VXAbstractSocket_WaitForConnected(self, msecs)) return false;
    }

    if (XAbstractSocket_bytesAvailable_base(self) > 0) return true;

    deadline = (msecs < 0) ? 0 : (XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs);
    do {
        /* Qt：只有 ConnectedState / BoundState 才继续等。 */
        if (self->state != XAbstractSocket_ConnectedState &&
            self->state != XAbstractSocket_BoundState)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XAbstractSocket_bytesAvailable_base(self) > 0) return true;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    } while (1);
}

/**
 * @brief 阻塞等待至少一个字节写出（对标 Qt 6.8
 *        `QAbstractSocket::waitForBytesWritten`，qabsock.cpp:2208）。
 * @details Qt 语义要点（原实现两处偏差）：
 *          1. **写缓冲为空 ⇒ 返回 false**（Qt 原文 `if (d->writeBuffer.isEmpty())
 *             return false;`）。原实现返回 **true**，语义相反——调用方据此
 *             以为「本轮确实写出去过」，把空闲误报成写成功。
 *          2. **ConnectingState / HostLookupState ⇒ 先 waitForConnected**。
 * @note 「写缓冲」在本框架对应「用户 ring buffer 里待发 + 平台在途写」
 *       两部分，两者都为 0 才算空。
 */
static bool VXAbstractSocket_waitForBytesWritten(XAbstractSocket* self, int msecs)
{
    bool writePending = false;
    uint64_t deadline;
    int64_t buffered;

    if (!self) return false;

    /* 1. Qt：未连接直接 false。 */
    if (self->state == XAbstractSocket_UnconnectedState) return false;

    /* 2. Qt：仍在连接中 ⇒ 先等连接完成。 */
    if (self->state == XAbstractSocket_HostLookupState ||
        self->state == XAbstractSocket_ConnectingState) {
        if (!VXAbstractSocket_WaitForConnected(self, msecs)) return false;
    }

    /* 3. Qt：写缓冲为空 ⇒ false（没有「待写」可等）。 */
    buffered = XAbstractSocket_bytesToWrite_base(self);
    if (!socketGetBoolProperty(self,
            (XDeviceProperty)XDeviceNetworkProperty_WritePending, &writePending))
        writePending = false;
    if (buffered == 0 && !writePending) return false;

    deadline = (msecs < 0) ? 0 : (XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs);
    do {
        XCoreApplication_processEvents(XEventLoop_AllEvents);

        buffered = XAbstractSocket_bytesToWrite_base(self);
        if (!socketGetBoolProperty(self,
                (XDeviceProperty)XDeviceNetworkProperty_WritePending, &writePending))
            writePending = false;
        if (buffered == 0 && !writePending) return true;   /* 已全部写出。 */

        if (self->state != XAbstractSocket_ConnectedState) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    } while (1);
}

/**
 * @brief 是否已无数据可读（对标 Qt 6.8 `QIODevice::atEnd`，qiodevice.cpp:910）。
 * @details Qt 判据：`openMode == NotOpen || (缓冲区空 && bytesAvailable() == 0)`。
 *          原实现用「state != ConnectedState」判定，与 Qt 有两处偏差：
 *            - **BoundState（已 bind 未 connect 的 UDP）在 Qt 下仍可 atEnd**
 *              按数据可用性判断，原实现恒返回 true；
 *            - 关闭流程中（ClosingState）Qt 仍按缓冲区实际内容判断，
 *              原实现立刻判 true，会让「关前把残留数据读完」的循环提前退出。
 */
static bool VXAbstractSocket_atEnd(XAbstractSocket* self)
{
    if (!self) return true;
    /* Qt：设备未打开 ⇒ atEnd。 */
    if (self->base.m_openMode == XIODevice_NotOpen) return true;
    /* Qt：缓冲区空且底层也无可读字节 ⇒ atEnd。 */
    if (XIODevice_bytesAvailable_base((XIODevice*)self) > 0) return false;
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

/**
 * @brief 读一行（对标 Qt 6.8 `QIODevice::readLine` 的前置约束，
 *        qiodevice.cpp:1314 + `CHECK_LINEMAXLEN`，qiodevice.cpp:82）。
 * @details Qt 在 `readLine` 入口就要求 `maxSize >= 2`（要留一个字节给
 *          终止符）；原实现只在 `maxlen <= 0` 时拒绝，`maxlen == 1`
 *          会走进 `readLineFromBuffer` 并写下越界的终止符。
 */
static int64_t VXAbstractSocket_readLineData(XAbstractSocket* self, char* data, int64_t maxlen)
{
    if (!self || !self->base.m_d) return -1;
    /* Qt CHECK_LINEMAXLEN：maxSize < 2 视为参数错误。 */
    if (maxlen < 2) return -1;
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
    if (!self) return false;
    /* [memhunt F2 根修 2026-10-06] fd 失效不可拦截 DeferredDelete：
     * abort/close 后 fd=XFD_INVALID，旧代码此行把投递到本 socket 的
     * XEVENT_TYPE_DEFERRED_DELETE 一并 return false 吞掉——XObject_event
     * 的 DeferredDelete 分支（XClassDelete(receiver)）永不执行，
     * deleteLater 永不兑现，会话设备树（4KB 通道 ring buffer+设备私有，
     * XIODevicePrivate.c:58/:121）整体泄漏。LSan 实证：单轮「连接→对端
     * 消失→优雅退出」SUMMARY 55008B/138，设备树全 Indirect、[DD-handler]
     * 对 socket 对象零兑现；派发门控(shouldDeliver)已判 "->1 放行"后
     * 事件仍消失，断点即本行。修后仅放行 DD（与 Qt QObject::event 对
     * DeferredDelete 的处理同构），SOCK_ACT/SOCK_CLOSE 等其余事件维持
     * fd 失效挡护（行为不变，改动最小化）。 */
    if (socketDeviceFd(self) == XFD_INVALID &&
        e->m_type != XEVENT_TYPE_DEFERRED_DELETE &&
        e->m_type != XEVENT_TYPE_HOST_LOOKUP)
        return false;

    /* 主机名解析完成事件：此时设备尚未打开（fd 无效），必须在 fd 守卫之后、
     * SOCK_ACT 分支之前处理。解析与连接都在本线程完成，无跨线程状态。 */
    if (e->m_type == XEVENT_TYPE_HOST_LOOKUP) {
        socketCompleteHostLookup(self);
        return true;
    }

    if (e->m_type == XEVENT_TYPE_SOCK_ACT) {
        XEventSockAct* sockAct = (XEventSockAct*)e;
        //XPrintf("消费:%p %d\n", e, sockAct->actType);
        socketControlEvent(self, e);

        if (sockAct->actType & XSocketActType_Read) {
            // 检查是否在代理握手过程中
            if (self->proxyHandshakeCtx && !XNetworkProxyHandshake_isCompleted(self->proxyHandshakeCtx)) {
                // 处理代理握手读事件
                XProxyHandshakeState hsState = XNetworkProxyHandshake_process(self, self->proxyHandshakeCtx);
                
                if (hsState == XProxyHandshakeState_Completed) {
                    // 代理握手完成，进入连接状态
                    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                    socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
                }
                else if (hsState == XProxyHandshakeState_Failed) {
                    // 代理握手失败
                    XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ProxyProtocolError,
                        XNetworkProxyHandshake_errorMessage(self->proxyHandshakeCtx));
                    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                }
                // 否则握手仍在进行中，继续等待
            }
            else {
                // 正常数据读取
                size_t bytesTransferred = 0;
                socketGetSizeProperty(self, (XDeviceProperty)XDeviceNetworkProperty_ReadFinishedBytes,
                                      &bytesTransferred);
                if (bytesTransferred > 0 && self->base.m_d) {
                    const char* readBuf = socketReadBuffer(self);
                    int channel = XIODevice_currentReadChannel((XIODevice*)self);
                    struct XRingBuffer* rb = XIODevicePrivate_getOrCreateReadBuffer(self->base.m_d, channel);
                    if (rb && readBuf) {
                        XRingBuffer_write(rb, readBuf, bytesTransferred);
                        /* [Qt 6.8 对齐] setReadBufferSize 的背压语义：达到上限
                         * 时**关闭读通知**（不再 ContinueRead），等应用读走、
                         * 缓冲低于上限后再恢复（见 readData 的恢复点）。
                         * Qt 在 canReadNotification 里同样按 readBufferMaxSize
                         * 关闭读通知（qabstractsocket.cpp:616-622）。 */
                        if (self->readBufferSize > 0
                            && XIODevice_bytesAvailable_base((XIODevice*)self)
                                   >= self->readBufferSize) {
                            self->readPaused = true;
                        } else {
                            /* Release the backend buffer before emitting: direct
                             * readyRead slots call XIODevice_read(), which would
                             * otherwise consume the same bytes from both buffers. */
                            socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
                        }
                        XIODevice_readyRead_signal((XIODevice*)self);
                    } else {
                        socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
                    }
                } else {
                    socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
                }
            }
        }

        if (sockAct->actType & XSocketActType_Write) {
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
                    XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ProxyProtocolError,
                        XNetworkProxyHandshake_errorMessage(self->proxyHandshakeCtx));
                    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
                    XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
                    self->proxyHandshakeCtx = NULL;
                }
            }
            else {
                // 正常数据写入完成
                size_t bytesWritten = 0;
                socketGetSizeProperty(self, (XDeviceProperty)XDeviceNetworkProperty_WriteFinishedBytes,
                                      &bytesWritten);
                if (bytesWritten > 0) {
                    XIODevice_bytesWritten_signal((XIODevice*)self, bytesWritten);
                }
                /* 继续投递写环形缓冲区中残留数据 */
                {
                    socketControlNoArgs(self, XDeviceNetworkCommand_ContinueWrite);
                }
                /* 延迟断开收尾：写缓冲已排空且此前请求过断开 ⇒ 此刻真正关闭。 */
                socketFinishDisconnectIfIdle(self);
            }
        }

        if (sockAct->actType & XSocketActType_Connect) {
            bool connected = false;
            if (socketGetBoolProperty(self, (XDeviceProperty)XDeviceNetworkProperty_Connected, &connected) && connected) {
                // 检查是否需要代理握手
                if (self->proxyHandshakeCtx) {
                    // 开始异步代理握手
                    if (!XNetworkProxyHandshake_start(self, self->proxyHandshakeCtx)) {
                        XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ProxyProtocolError,
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
                    socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
                }
            }
            else {
                XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ConnectionRefusedError, "Connection failed");
                XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            }
        }

        return true;
    }
    else if (e->m_type == XEVENT_TYPE_SOCK_CLOSE) {
        socketCloseDevice(self);
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
    XDeviceNetworkOpenOptions options;
    XVariant value;
    bool reuseAddr = (mode & XAbstractSocket_ShareAddress) || (mode & XAbstractSocket_ReuseAddressHint);
    bool shareAddr = (mode & XAbstractSocket_ShareAddress) != 0;
    uint16_t actualPort;
    int error = XDeviceError_None;
    if (!self || !address) return false;
    memset(&options, 0, sizeof(options));
    options.m_base.m_openMode = XIODevice_ReadWrite;
    options.m_socketType = toNetworkSockType(self->socketType);
    options.m_protocol = (XDeviceNetworkProtocol)XHostAddress_protocol(address);
    options.m_operation = XDeviceNetworkOpen_Bind;
    options.m_address = address;
    options.m_port = port;
    options.m_reuseAddress = reuseAddr;
    options.m_shareAddress = shareAddr;
    if (socketOpenDevice(self, &options, &error) == XFD_INVALID) {
        return false;
    }
    memset(&value, 0, sizeof(value)); /* fix(r7crash): 见 socketGetBoolProperty 真机修复注。 */
    XVariant_init(&value, NULL, 0, XVariantType_NULL); /* 桶规约: 见 socketGetBoolProperty。 */
    if (!XDevice_getProperty(socketDeviceFd(self), (XDeviceProperty)XDeviceNetworkProperty_LocalPort, &value)) {
        XVariant_setValue_null(&value);
        socketCloseDevice(self);
        return false;
    }
    actualPort = (uint16_t)XVariant_toInt(&value);
    XVariant_setValue_null(&value);
    XAbstractSocket_setLocalAddress(self, address);
    XAbstractSocket_setLocalPort(self, actualPort);  // 使用实际端口
    XAbstractSocket_setSocketState(self, XAbstractSocket_BoundState);

    XClass_Parent(XIODevice, EXIODevice_Open, bool (*)(XIODevice*, XIODeviceBaseMode))(self, XIODevice_ReadWrite);
    return true;
}

/* ==================== 主机名解析（同线程 · 事件驱动） ====================
 * 设计约束（用户指定）：不新增线程、不使用线程池；解析在调用线程的事件
 * 循环里执行，完成后用 XEVENT_TYPE_HOST_LOOKUP 事件通知同一个 socket。
 * 与 Qt 的差异仅在执行体所在线程（Qt 用 QHostInfo 工作线程），对外契约
 * （connectToHost 立即返回、HostLookupState、hostFound 时序）保持一致。 */

/** @brief 取消在途解析：撤销待处理事件并释放目标名。 */
static void socketCancelHostLookup(XAbstractSocket* self)
{
    if (!self) return;
    if (self->hostLookupPending) {
        XCoreApplication_removePostedEvents((XObject*)self, XEVENT_TYPE_HOST_LOOKUP);
        self->hostLookupPending = false;
    }
    if (self->lookupHostName) {
        XClassDelete(self->lookupHostName);
        self->lookupHostName = NULL;
    }
}

/**
 * @brief 是否已无待写数据（用户 ring buffer + 平台在途写都为空）。
 * @details 对应 Qt `allWriteBuffersEmpty() && engine->bytesToWrite() == 0`
 *          在 `disconnectFromHost` 里的判据。
 */
static bool socketWriteIdle(const XAbstractSocket* self)
{
    bool writePending = false;
    if (!self) return true;
    if (XIODevice_bytesToWrite_base((XIODevice*)self) > 0) return false;
    if (socketGetBoolProperty(self,
            (XDeviceProperty)XDeviceNetworkProperty_WritePending, &writePending)
        && writePending) return false;
    return true;
}

/** @brief 写缓冲已排空且此前请求过断开 ⇒ 完成关闭收尾。 */
static void socketFinishDisconnectIfIdle(XAbstractSocket* self)
{
    if (!self || !self->disconnectPending) return;
    if (!socketWriteIdle(self)) return;
    self->disconnectPending = false;
    socketCloseDevice(self);
    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
}

/**
 * @brief 用已解析地址发起连接（不做 DNS；地址串为数字形式）。
 * @details 对齐 Qt `connectToHost(QHostAddress, ...)` 的语义：不产生
 *          HostLookupState、不发 hostFound。
 */
static bool socketStartConnect(XAbstractSocket* self, const XHostAddress* address,
                               uint16_t port, XIODeviceBaseMode mode,
                               XAbstractSocket_NetworkLayerProtocol protocol)
{
    XDeviceNetworkOpenOptions options;
    XString* target;
    int error = XDeviceError_None;

    if (!self || !address) return false;
    target = XHostAddress_toString((XHostAddress*)address);
    if (!target) {
        XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_OperationError,
                                              "Failed to format address");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return false;
    }
    memset(&options, 0, sizeof(options));
    options.m_base.m_openMode = mode;
    options.m_base.m_target = target;
    options.m_socketType = toNetworkSockType(self->socketType);
    options.m_protocol = toNetworkProtocol(protocol);
    options.m_operation = XDeviceNetworkOpen_Connect;
    options.m_port = port;
    if (socketOpenDevice(self, &options, &error) == XFD_INVALID) {
        XClassDelete(target);
        XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ConnectionRefusedError,
                                              "Connection failed");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return false;
    }
    XClassDelete(target);

    /* 对齐 QAbstractSocket::connectToHost：`QIODevice::open(openMode)`。
     * 少了这一步，设备自身 m_openMode 恒 NotOpen ⇒ read_1/write_1 全被
     * 守卫拒绝（实测 read_1 NOT READABLE mode=0x0）。 */
    XIODevice_open_base((XIODevice*)self, mode);

    /* UDP 的 connect 在平台层立即完成且不产生连接事件，直接置 Connected；
     * TCP 保持 ConnectingState，等平台 Connect 完成事件。 */
    if (self->socketType == XAbstractSocket_UdpSocket)
        XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectedState);
    else
        XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    return true;
}

/** @brief 按协议族复制地址（XClassCopy 不会搬 XHostAddress 的字段）。 */
static bool socketCopyHostAddress(XHostAddress* dst, const XHostAddress* src)
{
    if (!dst || !src) return false;
    if (XHostAddress_protocol(src) == XHostAddress_IPv4Protocol) {
        XHostAddress_setAddressIPv4(dst, XHostAddress_toIPv4Address(src));
        return true;
    }
    if (XHostAddress_protocol(src) == XHostAddress_IPv6Protocol) {
        uint8_t ip6[16];
        XHostAddress_toIPv6Address(src, ip6);
        XHostAddress_setAddressIPv6(dst, ip6);
        return true;
    }
    return false;
}

/**
 * @brief 解析完成回调（由 HOST_LOOKUP 事件在调用线程驱动）。
 * @details 与 Qt `qabstractsocket.cpp:975-979` 的时序一致：
 *          先进入 ConnectingState（发 stateChanged），再发 hostFound，
 *          最后发起 connect。数字地址不发 hostFound。
 */
static void socketCompleteHostLookup(XAbstractSocket* self)
{
    XHostInfo* info = NULL;
    XHostAddress chosen;
    bool haveAddress = false;
    bool emitHostFound;
    uint16_t port;
    XIODeviceBaseMode mode;
    XAbstractSocket_NetworkLayerProtocol protocol;
    XString* hostName;

    if (!self || !self->hostLookupPending) return;
    self->hostLookupPending = false;

    hostName = self->lookupHostName;
    self->lookupHostName = NULL;
    port = self->lookupPort;
    mode = (XIODeviceBaseMode)self->lookupMode;
    protocol = (XAbstractSocket_NetworkLayerProtocol)self->lookupProtocol;
    emitHostFound = self->lookupEmitHostFound;

    if (!hostName) {
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    XHostAddress_init(&chosen);

    /* 数字地址：不再走 DNS（对齐 Qt：地址直连无解析、无 hostFound）。
     * 注意 `XHostAddress.protocol` 是 31 位位域：Unknown(-1) 存成
     * 0x7FFFFFFF，不能与枚举常量直接比较；用 `isNull` 判定更可靠。 */
    XHostAddress_setAddress(&chosen, XString_toUtf8(hostName));
    if (!chosen.isNull) {
        haveAddress = true;
        emitHostFound = false;
    } else {
        info = XHostInfo_fromName(hostName);
        if (info) {
            const XVector* addresses = XHostInfo_addresses_const(info);
            size_t count = addresses ? XVector_size_base(addresses) : 0;
            size_t i;
            for (i = 0; i < count; ++i) {
                const XHostAddress* candidate =
                    (const XHostAddress*)XVector_at_base(addresses, (int64_t)i);
                int candidateProtocol;
                if (!candidate) continue;
                candidateProtocol = (int)XHostAddress_protocol(candidate);
                if (protocol == XAbstractSocket_IPv4Protocol
                    && candidateProtocol != (int)XHostAddress_IPv4Protocol)
                    continue;
                if (protocol == XAbstractSocket_IPv6Protocol
                    && candidateProtocol != (int)XHostAddress_IPv6Protocol)
                    continue;
                if (socketCopyHostAddress(&chosen, candidate)) {
                    haveAddress = true;
                    break;
                }
            }
            XClassDelete(info);
        }
    }

    XClassDelete(hostName);

    if (!haveAddress) {
        XClassDeinit((XClass*)&chosen);
        XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_HostNotFoundError,
                                              "Host not found");
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    XAbstractSocket_setPeerAddress(self, &chosen);
    XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    if (emitHostFound)
        XAbstractSocket_hostFound_signal(self);
    (void)socketStartConnect(self, &chosen, port, mode, protocol);
    XClassDeinit((XClass*)&chosen);
}

static void VXAbstractSocket_ConnectToHost(XAbstractSocket* self, const char* hostName, uint16_t port, XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol protocol)
{
    XDeviceNetworkOpenOptions options;
    int error = XDeviceError_None;
    if (!self || !hostName) return;

    XAbstractSocket_setPeerName(self, hostName);
    XAbstractSocket_setPeerPort(self, port);

    // 检查是否需要使用代理
    if (self->proxy.type != XNetworkProxy_NoProxy && self->proxy.type != XNetworkProxy_DefaultProxy) {
        // 使用代理连接
        if (!self->proxy.hostName || self->proxy.port == 0) {
            XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_ProxyNotFoundError, "Proxy server not configured");
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
            XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_OperationError, "Failed to create proxy handshake context");
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        // 先连接到代理服务器
        XAbstractSocket_setSocketState(self, XAbstractSocket_HostLookupState);

        memset(&options, 0, sizeof(options));
        options.m_base.m_openMode = mode;
        options.m_base.m_target = self->proxy.hostName;
        options.m_socketType = toNetworkSockType(self->socketType);
        options.m_protocol = toNetworkProtocol(protocol);
        options.m_operation = XDeviceNetworkOpen_Connect;
        options.m_port = self->proxy.port;
        if (socketOpenDevice(self, &options, &error) == XFD_INVALID) {
            /* 代理设备打开失败: fd 无效, 先清代理握手上下文再复位状态,
               状态机归未连接, 上层由 connectToHost 的错误信号收尾。 */
            XNetworkProxyHandshake_destroyContext(self->proxyHandshakeCtx);
            self->proxyHandshakeCtx = NULL;
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }

        XAbstractSocket_setSocketState(self, XAbstractSocket_ConnectingState);
    }
    else {
        /* 直接连接（无代理）：**同线程、事件驱动**的主机名解析。
         *
         * 对齐 `QAbstractSocket::connectToHost`（qabsock.cpp:1623）的异步契约：
         * 调用即刻返回，先进入 HostLookupState；解析成功后置 Connecting、
         * 发 hostFound，再发起 connect。区别仅在**执行线程**：Qt 把解析放到
         * 全局线程池（QHostInfo 的工作线程），本实现按项目约束**不新增线程、
         * 不使用线程池**，而是在调用线程的事件循环里执行解析（结果经
         * XEVENT_TYPE_HOST_LOOKUP 事件回到同一个 socket）。
         *
         * 取舍：`getaddrinfo` 仍占用事件循环一次调用的时长；换来的是零新增
         * 线程、零跨线程状态，以及 `connectToHost` 不再同步阻塞调用方。 */
        socketCancelHostLookup(self);
        if (socketDeviceFd(self) != XFD_INVALID)
            socketCloseDevice(self);

        self->lookupHostName = XString_create_utf8(hostName);
        if (!self->lookupHostName) {
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return;
        }
        self->lookupPort = port;
        self->lookupMode = (int32_t)mode;
        self->lookupProtocol = (int32_t)protocol;
        self->lookupEmitHostFound = true;
        self->hostLookupPending = true;
        XAbstractSocket_setSocketState(self, XAbstractSocket_HostLookupState);
        {
            XEvent* ev = XEvent_create(XEVENT_TYPE_HOST_LOOKUP);
            if (ev)
                XCoreApplication_postEvent((XObject*)self, ev, XEVENT_PRIORITY_NORMAL);
            else
                socketCompleteHostLookup(self);   /* 事件申请失败：同线程立即完成 */
        }
        return;
    }
}


/**
 * @brief 正常断开（对标 Qt 6.8 `QAbstractSocket::disconnectFromHost`，:2612）。
 * @details Qt 语义：
 *          - 已 Unconnected ⇒ 空操作；
 *          - 仍在连接中 ⇒ 记 pendingClose，等连接完成后再断；
 *          - 有待写数据 ⇒ 置 ClosingState 并**延迟断开**，写完由写通知收尾；
 *          - 无待写数据 ⇒ 立即 reset 并转 Unconnected（发 disconnected）。
 *          原实现直接关设备返回，未发完的写缓冲被丢弃。
 */
static void VXAbstractSocket_DisconnectFromHost(XAbstractSocket* self)
{
    if (!self) return;
    if (self->state == XAbstractSocket_UnconnectedState) return;

    /* 解析在途：取消后直接收尾（Qt: ConnectingState 时置 pendingClose）。 */
    if (self->hostLookupPending
        || self->state == XAbstractSocket_HostLookupState) {
        socketCancelHostLookup(self);
        socketCloseDevice(self);
        XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
        return;
    }

    XAbstractSocket_setSocketState(self, XAbstractSocket_ClosingState);

    /* 有待写数据：延迟断开，写事件里由 socketFinishDisconnectIfIdle 收尾。 */
    if (!socketWriteIdle(self)) {
        self->disconnectPending = true;
        return;
    }

    socketCloseDevice(self);
    XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
}

static intptr_t VXAbstractSocket_SocketDescriptor(const XAbstractSocket* self)
{
    XVariant value;
    if (!self || socketDeviceFd(self) == XFD_INVALID) return -1;
    memset(&value, 0, sizeof(value)); /* fix(r7crash): 见 socketGetBoolProperty 真机修复注。 */
    XVariant_init(&value, NULL, 0, XVariantType_NULL); /* 桶规约: 见 socketGetBoolProperty。 */
    if (!XDevice_getProperty(socketDeviceFd(self), XDeviceProperty_NativeHandle, &value)) {
        XVariant_setValue_null(&value);
        return -1;
    }
    {
        intptr_t handle = (intptr_t)XVariant_toPtr(&value);
        XVariant_setValue_null(&value);
        return handle;
    }
}

static bool VXAbstractSocket_SetSocketDescriptor(XAbstractSocket* self, intptr_t socketDescriptor, XAbstractSocket_SocketState state, XIODeviceBaseMode openMode)
{
    XDeviceNetworkOpenOptions options;
    int error = XDeviceError_None;
    if (!self || socketDescriptor < 0) return false;
    memset(&options, 0, sizeof(options));
    options.m_base.m_openMode = openMode;
    options.m_socketType = toNetworkSockType(self->socketType);
    options.m_protocol = XDeviceNetwork_Any;
    options.m_operation = XDeviceNetworkOpen_Adopt;
    options.m_socketDescriptor = socketDescriptor;
    options.m_initialState = (int)state;
    if (socketOpenDevice(self, &options, &error) == XFD_INVALID) {
        return false;
    }
    
    XAbstractSocket_setSocketState(self, state);
    self->base.m_openMode = openMode;
    self->isValidFlag = (state == XAbstractSocket_ConnectedState);
    return true;
}

static void VXAbstractSocket_SetSocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option, const XVariant* value)
{
    XVarList* input;
    int networkOption = (int)option;
    if (!self || !value || socketDeviceFd(self) == XFD_INVALID) return;
    input = XVarList_Create(XVar(int, networkOption), XVar(const XVariant*, value));
    if (!input) return;
    (void)XDevice_control(socketDeviceFd(self), XDeviceNetworkCommand_SetSocketOption, input, NULL);
    XVarList_delete(input);
}

static XVariant* VXAbstractSocket_SocketOption(XAbstractSocket* self, XAbstractSocket_SocketOption option)
{
    XVarList* input;
    XVarList* output;
    int networkOption = (int)option;
    int result = 0;
    bool ok;
    if (!self || socketDeviceFd(self) == XFD_INVALID) return NULL;
    input = XVarList_Create(XVar(int, networkOption));
    output = XVarList_Create(XVar(int, result));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return NULL;
    }
    ok = XDevice_control(socketDeviceFd(self), XDeviceNetworkCommand_GetSocketOption, input, output);
    if (ok) {
        XVarList_start(output);
        result = XVarList_arg(output, int);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return ok ? XVariant_create_int(result) : NULL;
}


static void VXAbstractSocket_SetReadBufferSize(XAbstractSocket* self, int64_t size)
{
    XVariant value;
    if (!self) return;
    self->readBufferSize = size;
    /* [Qt 6.8 对齐] 上限放宽/取消时若读通知处于暂停态，立即恢复
     * （qabsock.cpp:2736-2747：`setReadNotificationEnabled(size == 0
     * || buffer.size() < size)`）。 */
    if (self->readPaused
        && (size <= 0
            || XIODevice_bytesAvailable_base((XIODevice*)self) < size)) {
        self->readPaused = false;
        if (socketDeviceFd(self) != XFD_INVALID)
            socketControlNoArgs(self, XDeviceNetworkCommand_ContinueRead);
    }
    if (socketDeviceFd(self) == XFD_INVALID) return;
    memset(&value, 0, sizeof(value)); /* fix(r7crash): 见 socketGetBoolProperty 真机修复注。 */
    XVariant_init(&value, NULL, 0, XVariantType_NULL); /* 桶规约: 见 socketGetBoolProperty。 */
    XVariant_setValue_int64(&value, size);
    (void)XDevice_setProperty(socketDeviceFd(self),
                              (XDeviceProperty)XDeviceNetworkProperty_ReadBufferSize, &value);
    XVariant_setValue_null(&value);
}

/**
 * @brief 阻塞等待连接完成（对标 Qt 6.8 `QAbstractSocket::waitForConnected`，
 *        qabsock.cpp:2049）。
 * @details Qt 语义要点（原实现两处偏差）：
 *          1. **UnconnectedState ⇒ 立即 false**（不是继续等）。
 *          2. **超时/仍在 ConnectingState ⇒ 置 SocketTimeoutError 并转
 *             UnconnectedState + resetSocketLayer**，而不是静默返回 false
 *             把「超时」伪装成普通未连接——调用方据 `error()` 无法区分
 *             「被拒」与「超时」。
 * @note 本实现沿用 `processEvents` 驱动；判定与收尾语义对齐 Qt。
 */
static bool VXAbstractSocket_WaitForConnected(XAbstractSocket* self, int msecs)
{
    uint64_t deadline;

    if (!self) return false;
    /* Qt：已连接直接 true。 */
    if (self->state == XAbstractSocket_ConnectedState) return true;
    /* Qt：未连接且不在连接中 ⇒ false。 */
    if (self->state == XAbstractSocket_UnconnectedState) return false;

    deadline = (msecs < 0) ? 0 : (XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs);
    while (self->state == XAbstractSocket_HostLookupState ||
           self->state == XAbstractSocket_ConnectingState) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (self->state == XAbstractSocket_ConnectedState) return true;
        if (self->state == XAbstractSocket_UnconnectedState) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) {
            /* Qt：超时 ⇒ 置错误 + 转未连接 + 复位套接字层。 */
            socketCancelHostLookup(self);
            XAbstractSocket_setSocketErrorAndEmit(self, XAbstractSocket_SocketTimeoutError,
                                           "Socket operation timed out");
            XAbstractSocket_setSocketState(self, XAbstractSocket_UnconnectedState);
            return false;
        }
    }
    return self->state == XAbstractSocket_ConnectedState;
}

static bool VXAbstractSocket_WaitForDisconnected(XAbstractSocket* self, int msecs)
{
    // 通用实现：事件循环等待断开状态
    if (!self) return true;
    if (self->state == XAbstractSocket_UnconnectedState) return true;
    
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (self->state != XAbstractSocket_UnconnectedState) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        /* 延迟断开可能不再产生写事件（积压已在 processEvents 中排空）：
         * 这里补一次收尾检查，避免等待循环空转到超时。 */
        socketFinishDisconnectIfIdle(self);
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
/**
 * @brief 直接按已解析地址连接（对齐 Qt `connectToHost(const QHostAddress&, ...)`）。
 * @details Qt 的地址重载**不做主机名解析、不进入 HostLookupState、不发
 *          hostFound**；原实现转成字符串后走 hostName 重载，会多绕一圈
 *          解析语义。这里直接走 socketStartConnect。
 */
void XAbstractSocket_connectToHostByAddress(XAbstractSocket* sock, const XHostAddress* address, uint16_t port, XIODeviceBaseMode mode)
{
    if (!sock || !address) return;
    socketCancelHostLookup(sock);
    if (socketDeviceFd(sock) != XFD_INVALID)
        socketCloseDevice(sock);
    XAbstractSocket_setPeerAddress(sock, address);
    XAbstractSocket_setPeerPort(sock, port);
    (void)socketStartConnect(sock, address, port, mode, XAbstractSocket_AnyIPProtocol);
}

bool XAbstractSocket_connectLocalStream_private(XAbstractSocket* sock,
                                                const XString* endpoint,
                                                XDeviceNetworkLocalStreamType streamType,
                                                int timeoutMs)
{
    XDeviceNetworkOpenOptions options;
    const char* path;
    int error = XDeviceError_None;
    if (!sock || !endpoint || XString_length_base(endpoint) == 0) return false;
    path = XString_toUtf8(endpoint);
    if (!path) return false;
    memset(&options, 0, sizeof(options));
    options.m_base.m_openMode = XIODevice_ReadWrite;
    options.m_base.m_target = endpoint;
    options.m_socketType = toNetworkSockType(sock->socketType);
    options.m_protocol = XDeviceNetwork_Any;
    options.m_operation = XDeviceNetworkOpen_Local;
    options.m_localStreamType = streamType;
    options.m_timeoutMs = timeoutMs;
    if (socketOpenDevice(sock, &options, &error) == XFD_INVALID) {
        XAbstractSocket_setSocketErrorAndEmit(sock, XAbstractSocket_OperationError,
                                       "Local stream connection failed");
        XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
        return false;
    }
    XAbstractSocket_setPeerName(sock, path);
    XAbstractSocket_setPeerPort(sock, 0);
    XAbstractSocket_setSocketState(sock, XAbstractSocket_ConnectedState);
    sock->isValidFlag = true;
    return true;
}

// ==================== Qt6 对齐：代理认证信号 ====================
void* XAbstractSocket_proxyAuthenticationRequired_signal(XAbstractSocket* sock, XNetworkProxy* proxy, void* authenticator)
{
    XEmitSignal(sock, XAbstractSocket_proxyAuthenticationRequired_signal,
                XVarList_Create(XVar(void*, proxy), XVar(void*, authenticator)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XAbstractSocket_proxyAuthenticationRequired_signal;
}
#endif // XNETWORK_ABSTRACT_SOCKET_ON
#endif /* XNETWORK_ON */
