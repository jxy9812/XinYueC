#include "XDeviceNetwork.h"
#include "XAbstractSocket.h"
#include "XIODevice.h"
#include "XIODevicePrivate.h"
#include "XRingBuffer.h"
#include "XSocketDescriptor.h"
#include "XEvent.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XAbstractEventDispatcher.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

#if XNETWORK_ON && XNETWORK_ABSTRACT_SOCKET_ON

#if XNETWORK_USE_LWIP
/* 前置声明：定义在文件底部（g_deviceNetwork 附近） */
static bool xLwipEventLoopPoll(void* userData);
static XHandle g_lwipPollHandle;
static int g_lwipDeviceCount;
#endif /* XNETWORK_USE_LWIP */


/* 所选平台在自己的 .c 中实现这些钩子；它们不是 XDeviceNetwork 公共 API。 */
XDeviceNetworkContext* XDeviceNetwork_createContext(void);
void XDeviceNetwork_deleteContext(XDeviceNetworkContext* context);
intptr_t XDeviceNetwork_socketDescriptor(XFd fd);
uint16_t XDeviceNetwork_socketBind(XFd fd, const XHostAddress* address,
    uint16_t port, bool reuseAddr, bool shareAddr, XDeviceNetworkSocketType socketType);
bool XDeviceNetwork_socketConnect(XFd fd, const XString* hostName, uint16_t port,
    XDeviceNetworkProtocol protocol, XDeviceNetworkSocketType socketType);
bool XDeviceNetwork_socketConnectLocal(XFd fd, const XString* endpoint,
    XDeviceNetworkLocalStreamType streamType, int timeoutMs,
    XDeviceNetworkSocketType socketType);
void XDeviceNetwork_socketDisconnect(XFd fd);
int64_t XDeviceNetwork_socketRead(XFd fd, void* buffer, int64_t size,
    XDeviceNetworkSocketType socketType, void* ringBuffer);
int64_t XDeviceNetwork_socketWrite(XFd fd, const void* data, int64_t size,
    XDeviceNetworkSocketType socketType, const XHostAddress* destination,
    uint16_t destinationPort, void* ringBuffer);
bool XDeviceNetwork_socketHandleEvent(XFd fd, void* event);
bool XDeviceNetwork_socketSetDescriptor(XFd fd, intptr_t socketDescriptor,
    int state, int openMode);
bool XDeviceNetwork_serverSetDescriptor(XFd fd, intptr_t socketDescriptor);
bool XDeviceNetwork_socketSetOption(XFd fd, int option, const void* value);
void* XDeviceNetwork_socketGetOption(XFd fd, int option);
void XDeviceNetwork_socketSetReadBufferSize(XFd fd, int64_t size);
const char* XDeviceNetwork_socketReadBuffer(XFd fd);
size_t XDeviceNetwork_socketReadFinishedBytes(XFd fd);
size_t XDeviceNetwork_socketWriteFinishedBytes(XFd fd);
bool XDeviceNetwork_socketWritePending(XFd fd);
void XDeviceNetwork_socketContinueRead(XFd fd, bool isUdp);
void XDeviceNetwork_socketContinueWrite(XFd fd, XRingBuffer* ringBuffer, bool isUdp);
XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd fd,
    const XHostAddress* address, uint16_t port, int backlog, bool reuseAddress);
bool XDeviceNetwork_serverAccept(XFd fd);
void XDeviceNetwork_serverClose(XFd fd, XDeviceNetworkServerHandle server);
uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server);
XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd fd,
    XHostAddress* clientAddress, uint16_t* clientPort);
bool XDeviceNetwork_platformGetLastDatagramSender(XFd fd,
    XHostAddress* sourceAddress, uint16_t* sourcePort);

typedef enum XMulticastOp {
    XMC_Join,
    XMC_Leave,
    XMC_SetIf,
    XMC_GetIf,
    XMC_SetTtl,
    XMC_GetTtl,
    XMC_SetLoop,
    XMC_GetLoop
} XMulticastOp;

bool XDeviceNetwork_multicastGroup(XDeviceNetworkSocketHandle socketHandle, bool join,
    const XHostAddress* groupAddress, uint32_t interfaceIndex);
int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle socketHandle,
    XMulticastOp operation, void* argument);

static XDeviceContext* VXDeviceNetwork_open(XDevice* self, const XDeviceOpenOptions* opts, int* err);
static void VXDeviceNetwork_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDeviceNetwork_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size);
static int64_t VXDeviceNetwork_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size);
static int64_t VXDeviceNetwork_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence);
static bool VXDeviceNetwork_flush(XDevice* self, XDeviceContext* handle);
static bool VXDeviceNetwork_resize(XDevice* self, XDeviceContext* handle, int64_t size);
static bool VXDeviceNetwork_setProperty(XDevice* self, XDeviceContext* handle, uint32_t property, const XVariant* value);
static bool VXDeviceNetwork_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);
static bool VXDeviceNetwork_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);
static bool VXDeviceNetwork_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                    const XVarList* in, XVarList* out);

static XDeviceNetworkContext* networkCtx(XDeviceContext* handle)
{
    return (XDeviceNetworkContext*)handle;
}

static XDeviceNetworkSocketType toNetworkSocketType(XDeviceNetworkSocketType type)
{
    return type == XDeviceNetwork_Udp ? XDeviceNetwork_Udp : XDeviceNetwork_Tcp;
}

static XDeviceNetworkProtocol toNetworkProtocol(XDeviceNetworkProtocol protocol)
{
    switch (protocol) {
    case XDeviceNetwork_IPv4: return XDeviceNetwork_IPv4;
    case XDeviceNetwork_IPv6: return XDeviceNetwork_IPv6;
    default: return XDeviceNetwork_Any;
    }
}

static bool isSocketTypeValid(XDeviceNetworkSocketType type)
{
    return type == XDeviceNetwork_Tcp || type == XDeviceNetwork_Udp;
}

static bool isProtocolValid(XDeviceNetworkProtocol protocol)
{
    return protocol == XDeviceNetwork_IPv4 || protocol == XDeviceNetwork_IPv6 ||
           protocol == XDeviceNetwork_Any;
}

static void setAnyAddress(XHostAddress* address, XDeviceNetworkProtocol protocol)
{
    XHostAddress_init(address);
    if (protocol == XDeviceNetwork_IPv6)
        XHostAddress_setAddressSpecial(address, XHostAddress_AnyIPv6Special);
    else
        XHostAddress_setAddressSpecial(address, XHostAddress_AnySpecial);
}

static bool setContextError(XDeviceNetworkContext* ctx, int error)
{
    if (ctx) ctx->m_base.m_lastError = (int16_t)error;
    return false;
}

XVtable* XDeviceNetwork_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceNetwork)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceNetwork_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceNetwork_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Read, VXDeviceNetwork_read);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Write, VXDeviceNetwork_write);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Seek, VXDeviceNetwork_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Flush, VXDeviceNetwork_flush);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Resize, VXDeviceNetwork_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_SetProperty, VXDeviceNetwork_setProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_GetProperty, VXDeviceNetwork_getProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_QueryProperty, VXDeviceNetwork_queryProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceNetwork_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("socket");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceNetwork);
    return XVTABLE_DEFAULT;
}

void XDeviceNetwork_init(XDeviceNetwork* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(XDeviceNetwork) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceNetwork);
    self->m_base.m_type = XDeviceType_Socket;
    self->m_base.m_capabilities = XDeviceCap_Read | XDeviceCap_Write |
                                   XDeviceCap_Flush | XDeviceCap_Async;
}

XDeviceNetwork* XDeviceNetwork_create(void)
{
    XDeviceNetwork* self = (XDeviceNetwork*)XClass_Malloc(XDeviceNetwork);
    if (!self) return NULL;
    XDeviceNetwork_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

static XDeviceContext* VXDeviceNetwork_open(XDevice* self, const XDeviceOpenOptions* opts, int* err)
{
    const XDeviceNetworkOpenOptions* nopts = (const XDeviceNetworkOpenOptions*)opts;
    XDeviceNetworkContext* ctx;
    XHostAddress anyAddress;
    const XHostAddress* bindAddress;
    uint16_t actualPort;
    int mode;
    bool operationOk = true;

    if (!opts || !nopts || !isSocketTypeValid(nopts->m_socketType) ||
        !isProtocolValid(nopts->m_protocol) ||
        ((nopts->m_operation == XDeviceNetworkOpen_Connect ||
          nopts->m_operation == XDeviceNetworkOpen_Local) && !nopts->m_base.m_target) ||
        (nopts->m_operation != XDeviceNetworkOpen_None &&
         nopts->m_operation != XDeviceNetworkOpen_Connect &&
         nopts->m_operation != XDeviceNetworkOpen_Bind &&
         nopts->m_operation != XDeviceNetworkOpen_Adopt &&
         nopts->m_operation != XDeviceNetworkOpen_Local &&
         nopts->m_operation != XDeviceNetworkOpen_Listen &&
         nopts->m_operation != XDeviceNetworkOpen_ListenAdopt) ||
        ((nopts->m_operation == XDeviceNetworkOpen_Listen ||
          nopts->m_operation == XDeviceNetworkOpen_ListenAdopt) &&
         (nopts->m_socketType != XDeviceNetwork_Tcp || !nopts->m_owner)) ||
        (nopts->m_operation == XDeviceNetworkOpen_Listen && nopts->m_listenBacklog <= 0)) {
        if (err) *err = (int)XDeviceError_InvalidArgument;
        return NULL;
    }

    XDeviceNetwork_ensureInit();
    ctx = XDeviceNetwork_createContext();
    if (!ctx) {
        XDeviceNetwork_cleanup();
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    ctx->m_base.m_fd = XFD_INVALID;
    ctx->m_base.m_device = self;
    ctx->m_base.m_state = (uint16_t)XDeviceState_Opening;
    ctx->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Async;

    ctx->m_socketType = nopts->m_socketType;
    ctx->m_protocol = nopts->m_protocol;
    ctx->m_flags = nopts->m_base.m_flags;
    ctx->m_peerPort = nopts->m_peerPort;
    XHostAddress_init(&ctx->m_peerAddress);
    if (nopts->m_peerAddress) {
        XClass_copy_base((XClass*)&ctx->m_peerAddress,
                         (const XClass*)nopts->m_peerAddress);
        ctx->m_hasPeerAddress = true;
    }

    ctx->m_owner = nopts->m_owner;
    ctx->m_isServer = nopts->m_operation == XDeviceNetworkOpen_Listen ||
                      nopts->m_operation == XDeviceNetworkOpen_ListenAdopt;
    if (!ctx->m_isServer && nopts->m_owner) {
        ctx->m_endpoint.m_socket = (XAbstractSocket*)nopts->m_owner;
    } else if (!ctx->m_isServer) {
        ctx->m_endpoint.m_socket = (XAbstractSocket*)XCalloc_System(1, sizeof(XAbstractSocket));
        if (ctx->m_endpoint.m_socket) {
            XAbstractSocket_init(ctx->m_endpoint.m_socket,
            nopts->m_socketType == XDeviceNetwork_Udp ? XAbstractSocket_UdpSocket : XAbstractSocket_TcpSocket);
        }
        ctx->m_ownsSocket = true;
    }
    if (!ctx->m_isServer && !ctx->m_endpoint.m_socket) {
        XClass_deinit_base((XClass*)&ctx->m_peerAddress);
        XDeviceNetwork_cleanup();
        XDeviceNetwork_deleteContext(ctx);
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }

    ctx->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &ctx->m_base, ctx->m_owner);
    if (ctx->m_base.m_fd == XFD_INVALID) {
        if (ctx->m_ownsSocket) {
            XClass_deinit_base((XClass*)ctx->m_endpoint.m_socket);
            XFree_System(ctx->m_endpoint.m_socket);
        }
        XClass_deinit_base((XClass*)&ctx->m_peerAddress);
        XDeviceNetwork_deleteContext(ctx);
        XDeviceNetwork_cleanup();
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }

    mode = nopts->m_base.m_openMode ? nopts->m_base.m_openMode : XIODevice_ReadWrite;
    ctx->m_openMode = mode;
    if (!ctx->m_isServer)
        ctx->m_endpoint.m_socket->base.m_openMode = (uint8_t)mode;

    if (nopts->m_operation == XDeviceNetworkOpen_Listen) {
        if (nopts->m_address) {
            bindAddress = nopts->m_address;
        } else {
            setAnyAddress(&anyAddress, nopts->m_protocol);
            bindAddress = &anyAddress;
        }
        ctx->m_endpoint.m_serverHandle = XDeviceNetwork_serverCreate(ctx->m_base.m_fd, bindAddress,
            nopts->m_port, nopts->m_listenBacklog, nopts->m_reuseAddress);
        operationOk = XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(ctx->m_endpoint.m_serverHandle));
        if (operationOk) actualPort = XDeviceNetwork_serverPort(ctx->m_endpoint.m_serverHandle);
        if (!nopts->m_address)
            XClass_deinit_base((XClass*)&anyAddress);
    } else if (nopts->m_operation == XDeviceNetworkOpen_ListenAdopt) {
        operationOk = nopts->m_socketDescriptor >= 0 &&
            XDeviceNetwork_serverSetDescriptor(ctx->m_base.m_fd, nopts->m_socketDescriptor);
        if (operationOk) {
            ctx->m_endpoint.m_serverHandle = nopts->m_socketDescriptor;
            actualPort = XDeviceNetwork_serverPort(ctx->m_endpoint.m_serverHandle);
        }
    } else if (nopts->m_operation == XDeviceNetworkOpen_Bind) {
        if (nopts->m_address) {
            bindAddress = nopts->m_address;
        } else {
            setAnyAddress(&anyAddress, nopts->m_protocol);
            bindAddress = &anyAddress;
        }
        actualPort = XDeviceNetwork_socketBind(ctx->m_base.m_fd, bindAddress, nopts->m_port,
            nopts->m_reuseAddress, nopts->m_shareAddress, toNetworkSocketType(nopts->m_socketType));
        operationOk = actualPort != 0;
        if (operationOk) {
            XAbstractSocket_setLocalAddress(ctx->m_endpoint.m_socket, bindAddress);
            XAbstractSocket_setLocalPort(ctx->m_endpoint.m_socket, actualPort);
            ctx->m_endpoint.m_socket->state = XAbstractSocket_BoundState;
        }
        if (!nopts->m_address)
            XClass_deinit_base((XClass*)&anyAddress);
    } else if (nopts->m_operation == XDeviceNetworkOpen_Connect) {
        ctx->m_connectedMode = true;
        operationOk = XDeviceNetwork_socketConnect(ctx->m_base.m_fd, nopts->m_base.m_target, nopts->m_port,
            toNetworkProtocol(nopts->m_protocol), toNetworkSocketType(nopts->m_socketType));
        if (operationOk) {
            XAbstractSocket_setPeerPort(ctx->m_endpoint.m_socket, nopts->m_port);
            ctx->m_endpoint.m_socket->state = XAbstractSocket_ConnectingState;
        }
    } else if (nopts->m_operation == XDeviceNetworkOpen_Adopt) {
        operationOk = nopts->m_socketDescriptor >= 0 &&
            XDeviceNetwork_socketSetDescriptor(ctx->m_base.m_fd, nopts->m_socketDescriptor,
                                               nopts->m_initialState, mode);
        if (operationOk)
            ctx->m_endpoint.m_socket->state = (XAbstractSocket_SocketState)nopts->m_initialState;
    } else if (nopts->m_operation == XDeviceNetworkOpen_Local) {
        operationOk = XDeviceNetwork_socketConnectLocal(ctx->m_base.m_fd, nopts->m_base.m_target,
            nopts->m_localStreamType, nopts->m_timeoutMs, toNetworkSocketType(nopts->m_socketType));
        if (operationOk)
            ctx->m_endpoint.m_socket->state = XAbstractSocket_ConnectedState;
    }

    if (!operationOk) {
        if (ctx->m_isServer)
            XDeviceNetwork_serverClose(ctx->m_base.m_fd, ctx->m_endpoint.m_serverHandle);
        else
            XDeviceNetwork_socketDisconnect(ctx->m_base.m_fd);
        if (ctx->m_ownsSocket) {
            XClass_deinit_base((XClass*)ctx->m_endpoint.m_socket);
            XFree_System(ctx->m_endpoint.m_socket);
        }
        XClass_deinit_base((XClass*)&ctx->m_peerAddress);
        XFd_free(ctx->m_base.m_fd);
        XDeviceNetwork_deleteContext(ctx);
        XDeviceNetwork_cleanup();
        if (err) *err = (int)XDeviceError_IoFail;
        return NULL;
    }

    ctx->m_base.m_device = self;
    ctx->m_base.m_state = (uint16_t)XDeviceState_Active;
    ctx->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Async;
    ctx->m_base.m_pendingOps = 0;
    ctx->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (err) *err = (int)XDeviceError_None;

#if XNETWORK_USE_LWIP
    /* 打开第一个网络设备时，向事件循环注册 lwIP 轮询回调 */
    if (g_lwipDeviceCount == 0) {
        g_lwipPollHandle = XAbstractEventDispatcher_addPollCallback(
            xLwipEventLoopPoll, NULL);
    }
    ++g_lwipDeviceCount;
#endif /* XNETWORK_USE_LWIP */
    return &ctx->m_base;
}

static void VXDeviceNetwork_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    (void)self;
    if (!ctx) return;
    if (ctx->m_isServer)
        XDeviceNetwork_serverClose(ctx->m_base.m_fd, ctx->m_endpoint.m_serverHandle);
    else
        XDeviceNetwork_socketDisconnect(ctx->m_base.m_fd);
    if (ctx->m_ownsSocket) {
        XClass_deinit_base((XClass*)ctx->m_endpoint.m_socket);
        XFree_System(ctx->m_endpoint.m_socket);
    }
    XClass_deinit_base((XClass*)&ctx->m_peerAddress);
    XDeviceNetwork_deleteContext(ctx);
    XDeviceNetwork_cleanup();

#if XNETWORK_USE_LWIP
    /* 关闭最后一个网络设备时，从事件循环注销 lwIP 轮询回调 */
    if (g_lwipDeviceCount > 0) {
        --g_lwipDeviceCount;
        if (g_lwipDeviceCount == 0 && g_lwipPollHandle) {
            XAbstractEventDispatcher_removePollCallback(g_lwipPollHandle);
            g_lwipPollHandle = NULL;
        }
    }
#endif /* XNETWORK_USE_LWIP */
}

static int64_t VXDeviceNetwork_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    XRingBuffer* readBuffer;
    int64_t result;
    (void)self;
    if (!ctx || ctx->m_isServer || !ctx->m_endpoint.m_socket || size < 0 || (!buffer && size > 0)) return -1;
    if (size == 0) return 0;
    readBuffer = XIODevicePrivate_getOrCreateReadBuffer(ctx->m_endpoint.m_socket->base.m_d,
                                                        ctx->m_endpoint.m_socket->base.m_currentReadChannel);
    if (!readBuffer) return -1;
    result = XDeviceNetwork_socketRead(ctx->m_base.m_fd, buffer, size,
        toNetworkSocketType(ctx->m_socketType), readBuffer);
    if (result < 0) ctx->m_base.m_lastError = (int16_t)XDeviceError_IoFail;
    return result;
}

static int64_t VXDeviceNetwork_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    XRingBuffer* writeBuffer;
    const XHostAddress* destination = NULL;
    int64_t result;
    (void)self;
    if (!ctx || ctx->m_isServer || !ctx->m_endpoint.m_socket || size < 0 || (!data && size > 0)) return -1;
    if (size == 0) return 0;
    writeBuffer = XIODevicePrivate_getOrCreateWriteBuffer(ctx->m_endpoint.m_socket->base.m_d,
                                                          ctx->m_endpoint.m_socket->base.m_currentWriteChannel);
    if (!writeBuffer) return -1;
    if (ctx->m_socketType == XDeviceNetwork_Udp) {
        if (!ctx->m_hasPeerAddress && !ctx->m_connectedMode) {
            ctx->m_base.m_lastError = (int16_t)XDeviceError_InvalidArgument;
            return -1;
        }
        destination = ctx->m_hasPeerAddress ? &ctx->m_peerAddress : NULL;
    }
    result = XDeviceNetwork_socketWrite(ctx->m_base.m_fd, data, size,
        toNetworkSocketType(ctx->m_socketType), destination, ctx->m_peerPort, writeBuffer);
    if (result < 0) ctx->m_base.m_lastError = (int16_t)XDeviceError_IoFail;
    return result;
}

static int64_t VXDeviceNetwork_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence)
{
    (void)self; (void)handle; (void)offset; (void)whence;
    return -1;
}

static bool VXDeviceNetwork_flush(XDevice* self, XDeviceContext* handle)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    (void)self;
    if (!ctx || ctx->m_isServer || !ctx->m_endpoint.m_socket) return false;
    return XIODevice_bytesToWrite_base(&ctx->m_endpoint.m_socket->base) == 0 &&
           !XDeviceNetwork_socketWritePending(ctx->m_base.m_fd);
}

static bool VXDeviceNetwork_resize(XDevice* self, XDeviceContext* handle, int64_t size)
{
    (void)self; (void)handle; (void)size;
    return false;
}

static bool VXDeviceNetwork_setProperty(XDevice* self, XDeviceContext* handle, uint32_t property, const XVariant* value)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    int type;
    int64_t port;
    (void)self;
    if (!ctx || ctx->m_isServer || !value) return false;

    /* UDP 的默认目标端口和读缓冲容量都是打开后仍可调整的设备属性。 */
    if (property == XDeviceNetworkProperty_ReadBufferSize) {
        int64_t size;
        type = XVariant_type((XVariant*)value);
        if (type != XVariantType_Int && type != XVariantType_Int32 &&
            type != XVariantType_Int64 && type != XVariantType_Size_t)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        size = XVariant_toInt64(value);
        if (size < 0) return setContextError(ctx, XDeviceError_InvalidArgument);
        XDeviceNetwork_socketSetReadBufferSize(ctx->m_base.m_fd, size);
        ctx->m_readBufferSize = size;
        ctx->m_base.m_lastError = (int16_t)XDeviceError_None;
        return true;
    }
    if (property != XDeviceNetworkProperty_PeerPort ||
        ctx->m_socketType != XDeviceNetwork_Udp) {
        return false;
    }
    type = XVariant_type((XVariant*)value);
    if (type != XVariantType_Int && type != XVariantType_Int32 &&
        type != XVariantType_Int64 && type != XVariantType_Size_t) {
        return setContextError(ctx, XDeviceError_InvalidArgument);
    }
    port = XVariant_toInt64(value);
    if (port < 0 || port > 65535)
        return setContextError(ctx, XDeviceError_InvalidArgument);
    ctx->m_peerPort = (uint16_t)port;
    ctx->m_base.m_lastError = (int16_t)XDeviceError_None;
    return true;
}

static bool VXDeviceNetwork_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    intptr_t descriptor;
    (void)self;
    if (!ctx || !value) return false;
    switch (property) {
    case XDeviceProperty_OpenMode:
        XVariant_setValue_int(value, ctx->m_openMode);
        return true;
    case XDeviceProperty_NonBlocking:
        XVariant_setValue_bool(value, (ctx->m_flags & XDeviceOpenFlag_NonBlocking) != 0);
        return true;
    case XDeviceProperty_IoMode:
        XVariant_setValue_int(value, (int)ctx->m_base.m_ioMode);
        return true;
    case XDeviceProperty_State:
        XVariant_setValue_int(value, (int)ctx->m_base.m_state);
        return true;
    case XDeviceProperty_LastError:
        XVariant_setValue_int(value, (int)ctx->m_base.m_lastError);
        return true;
    case XDeviceProperty_NativeHandle:
        descriptor = ctx->m_isServer ? ctx->m_endpoint.m_serverHandle :
            XDeviceNetwork_socketDescriptor(ctx->m_base.m_fd);
        XVariant_setValue_ptr(value, descriptor < 0 ? NULL : (void*)descriptor);
        return true;
    case XDeviceNetworkProperty_SocketType:
        XVariant_setValue_int(value, (int)ctx->m_socketType);
        return true;
    case XDeviceNetworkProperty_Protocol:
        XVariant_setValue_int(value, (int)ctx->m_protocol);
        return true;
    case XDeviceNetworkProperty_Connected:
        XVariant_setValue_bool(value, ctx->m_isServer || ctx->m_connected);
        return true;
    case XDeviceNetworkProperty_LocalPort:
        XVariant_setValue_int(value, ctx->m_isServer ?
                              (int)XDeviceNetwork_serverPort(ctx->m_endpoint.m_serverHandle) :
                              (int)ctx->m_endpoint.m_socket->localPort);
        return true;
    case XDeviceNetworkProperty_PeerPort:
        XVariant_setValue_int(value, (int)ctx->m_peerPort);
        return true;
    case XDeviceNetworkProperty_ReadBufferSize:
        XVariant_setValue_int64(value, ctx->m_readBufferSize);
        return true;
    case XDeviceNetworkProperty_ReadFinishedBytes:
        XVariant_setValue_size_t(value, XDeviceNetwork_socketReadFinishedBytes(ctx->m_base.m_fd));
        return true;
    case XDeviceNetworkProperty_WriteFinishedBytes:
        XVariant_setValue_size_t(value, XDeviceNetwork_socketWriteFinishedBytes(ctx->m_base.m_fd));
        return true;
    case XDeviceNetworkProperty_WritePending:
        XVariant_setValue_bool(value, XDeviceNetwork_socketWritePending(ctx->m_base.m_fd));
        return true;
    default:
        return false;
    }
}

static bool VXDeviceNetwork_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
    return VXDeviceNetwork_getProperty(self, handle, property, value);
}

static bool networkGetPropertyVariant(XFd fd, XDeviceNetworkProperty property,
    XVariant* value)
{
    bool result;
    memset(value, 0, sizeof(*value));
    XVariant_init(value, NULL, 0, XVariantType_NULL);
    result = XDevice_getProperty(fd, (XDeviceProperty)property, value);
    if (!result)
        XVariant_deinit_base((XClass*)value);
    return result;
}

static bool networkSetPropertyValue(XFd fd, XDeviceNetworkProperty property,
    const void* data, size_t size, int type)
{
    XVariant value;
    bool result;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, (void*)data, size, type);
    result = XDevice_setProperty(fd, (XDeviceProperty)property, &value);
    XVariant_deinit_base((XClass*)&value);
    return result;
}

bool XDeviceNetwork_getSocketType(XFd fd, XDeviceNetworkSocketType* value)
{
    XVariant variant;
    bool result;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_SocketType, &variant))
        return false;
    *value = (XDeviceNetworkSocketType)XVariant_toInt(&variant);
    XVariant_deinit_base((XClass*)&variant);
    result = isSocketTypeValid(*value);
    return result;
}

bool XDeviceNetwork_getProtocol(XFd fd, XDeviceNetworkProtocol* value)
{
    XVariant variant;
    bool result;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_Protocol, &variant))
        return false;
    *value = (XDeviceNetworkProtocol)XVariant_toInt(&variant);
    XVariant_deinit_base((XClass*)&variant);
    result = isProtocolValid(*value);
    return result;
}

bool XDeviceNetwork_getConnected(XFd fd, bool* value)
{
    XVariant variant;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_Connected, &variant))
        return false;
    *value = XVariant_toBool(&variant);
    XVariant_deinit_base((XClass*)&variant);
    return true;
}

bool XDeviceNetwork_getLocalPort(XFd fd, uint16_t* value)
{
    XVariant variant;
    int64_t port;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_LocalPort, &variant))
        return false;
    port = XVariant_toInt64(&variant);
    XVariant_deinit_base((XClass*)&variant);
    if (port < 0 || port > UINT16_MAX) return false;
    *value = (uint16_t)port;
    return true;
}

bool XDeviceNetwork_getPeerPort(XFd fd, uint16_t* value)
{
    XVariant variant;
    int64_t port;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_PeerPort, &variant))
        return false;
    port = XVariant_toInt64(&variant);
    XVariant_deinit_base((XClass*)&variant);
    if (port < 0 || port > UINT16_MAX) return false;
    *value = (uint16_t)port;
    return true;
}

bool XDeviceNetwork_setPeerPort(XFd fd, uint16_t value)
{
    return networkSetPropertyValue(fd, XDeviceNetworkProperty_PeerPort, &value,
        sizeof(value), XVariantType_Uint16);
}

bool XDeviceNetwork_getReadBufferSize(XFd fd, int64_t* value)
{
    XVariant variant;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_ReadBufferSize, &variant))
        return false;
    *value = XVariant_toInt64(&variant);
    XVariant_deinit_base((XClass*)&variant);
    return true;
}

bool XDeviceNetwork_setReadBufferSize(XFd fd, int64_t value)
{
    if (value < 0) return false;
    return networkSetPropertyValue(fd, XDeviceNetworkProperty_ReadBufferSize,
        &value, sizeof(value), XVariantType_Int64);
}

bool XDeviceNetwork_getReadFinishedBytes(XFd fd, size_t* value)
{
    XVariant variant;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_ReadFinishedBytes, &variant))
        return false;
    *value = XVariant_toSize_t(&variant);
    XVariant_deinit_base((XClass*)&variant);
    return true;
}

bool XDeviceNetwork_getWriteFinishedBytes(XFd fd, size_t* value)
{
    XVariant variant;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_WriteFinishedBytes, &variant))
        return false;
    *value = XVariant_toSize_t(&variant);
    XVariant_deinit_base((XClass*)&variant);
    return true;
}

bool XDeviceNetwork_getWritePending(XFd fd, bool* value)
{
    XVariant variant;
    if (!value || !networkGetPropertyVariant(fd, XDeviceNetworkProperty_WritePending, &variant))
        return false;
    *value = XVariant_toBool(&variant);
    XVariant_deinit_base((XClass*)&variant);
    return true;
}

static bool networkControlSimple(XFd fd, XDeviceNetworkCommand command)
{
    return XDevice_control(fd, command, NULL, NULL);
}

bool XDeviceNetwork_handleEvent(XFd fd, XEvent* event)
{
    XVarList* input;
    bool result;
    if (!event) return false;
    input = XVarList_Create(XVar(XEvent*, event));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_HandleEvent, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceNetwork_continueRead(XFd fd)
{
    return networkControlSimple(fd, XDeviceNetworkCommand_ContinueRead);
}

bool XDeviceNetwork_continueWrite(XFd fd)
{
    return networkControlSimple(fd, XDeviceNetworkCommand_ContinueWrite);
}

bool XDeviceNetwork_setSocketOption(XFd fd, int option, const XVariant* value)
{
    XVarList* input;
    bool result;
    if (!value) return false;
    input = XVarList_Create(XVar(int, option), XVar(const XVariant*, value));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_SetSocketOption, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceNetwork_getSocketOption(XFd fd, int option, int* value)
{
    XVarList* input;
    XVarList* output;
    int optionValue = 0;
    bool result;
    if (!value) return false;
    input = XVarList_Create(XVar(int, option));
    output = XVarList_Create(XVar(int, optionValue));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return false;
    }
    result = XDevice_control(fd, XDeviceNetworkCommand_GetSocketOption, input, output);
    if (result) {
        XVarList_start(output);
        *value = XVarList_arg(output, int);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_getLastDatagramSender(XFd fd, XHostAddress* address, uint16_t* port)
{
    XVarList* output;
    XHostAddress* addressOut = address;
    uint16_t* portOut = port;
    bool result;
    if (!address && !port) return false;
    output = XVarList_Create(XVar(XHostAddress*, addressOut), XVar(uint16_t*, portOut));
    if (!output) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_GetLastDatagramSender, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_getReadBuffer(XFd fd, const char** buffer)
{
    XVarList* output;
    const char* value = NULL;
    bool result;
    if (!buffer) return false;
    output = XVarList_Create(XVar(const char*, value));
    if (!output) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_GetReadBuffer, NULL, output);
    if (result) {
        XVarList_start(output);
        *buffer = XVarList_arg(output, const char*);
    }
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_sendDatagram(XFd fd, const void* data, int64_t size,
    const XHostAddress* address, uint16_t port, int64_t* written)
{
    XVarList* input;
    XVarList* output;
    int64_t resultValue = -1;
    bool result;
    if (!data || size < 0 || !address || !written) return false;
    input = XVarList_Create(XVar(const void*, data), XVar(int64_t, size),
        XVar(const XHostAddress*, address), XVar(uint16_t, port));
    output = XVarList_Create(XVar(int64_t, resultValue));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return false;
    }
    result = XDevice_control(fd, XDeviceNetworkCommand_SendDatagram, input, output);
    if (result) {
        XVarList_start(output);
        *written = XVarList_arg(output, int64_t);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_setMulticastGroup(XFd fd, bool join,
    const XHostAddress* group, uint32_t interfaceIndex)
{
    XVarList* input;
    bool result;
    if (!group) return false;
    input = XVarList_Create(XVar(bool, join), XVar(const XHostAddress*, group),
        XVar(uint32_t, interfaceIndex));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_SetMulticastGroup, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceNetwork_setMulticastInterface(XFd fd, uint32_t interfaceIndex)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(uint32_t, interfaceIndex));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_SetMulticastInterface, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceNetwork_getMulticastInterface(XFd fd, uint32_t* interfaceIndex)
{
    XVarList* output;
    uint32_t* value = interfaceIndex;
    bool result;
    if (!interfaceIndex) return false;
    output = XVarList_Create(XVar(uint32_t*, value));
    if (!output) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_GetMulticastInterface, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_continueAccept(XFd fd)
{
    return networkControlSimple(fd, XDeviceNetworkCommand_ContinueAccept);
}

bool XDeviceNetwork_getAcceptedSocket(XFd fd, XDeviceNetworkSocketHandle* handle,
    XHostAddress* address, uint16_t* port)
{
    XVarList* output;
    XDeviceNetworkSocketHandle* handleOut = handle;
    XHostAddress* addressOut = address;
    uint16_t* portOut = port;
    bool result;
    if (!handle) return false;
    output = XVarList_Create(XVar(XDeviceNetworkSocketHandle*, handleOut),
        XVar(XHostAddress*, addressOut), XVar(uint16_t*, portOut));
    if (!output) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_GetAcceptedSocket, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceNetwork_closeAcceptedSocket(XFd fd, XDeviceNetworkSocketHandle handle)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceNetworkSocketHandle, handle));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceNetworkCommand_CloseAcceptedSocket, input, NULL);
    XVarList_delete(input);
    return result;
}

static bool VXDeviceNetwork_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                    const XVarList* in, XVarList* out)
{
    XDeviceNetworkContext* ctx = networkCtx(handle);
    XVarList* arguments = (XVarList*)in;
    (void)self;
    if (!ctx || ctx->m_base.m_fd == XFD_INVALID) return false;

    switch (command) {
    case XDeviceNetworkCommand_ContinueAccept:
        return ctx->m_isServer && XDeviceNetwork_serverAccept(ctx->m_base.m_fd);
    case XDeviceNetworkCommand_GetAcceptedSocket:
    {
        XDeviceNetworkSocketHandle* socketHandle;
        XHostAddress* address;
        uint16_t* port;
        if (!ctx->m_isServer || in || !out ||
            out->m_size != sizeof(socketHandle) + sizeof(address) + sizeof(port))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(out);
        socketHandle = XVarList_arg(out, XDeviceNetworkSocketHandle*);
        address = XVarList_arg(out, XHostAddress*);
        port = XVarList_arg(out, uint16_t*);
        if (!socketHandle)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        *socketHandle = XDeviceNetwork_serverGetAcceptedSocket(ctx->m_base.m_fd, address, port);
        return XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(*socketHandle));
    }
    case XDeviceNetworkCommand_CloseAcceptedSocket:
    {
        XDeviceNetworkSocketHandle socketHandle;
        if (!ctx->m_isServer || !arguments || arguments->m_size != sizeof(socketHandle))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        socketHandle = XVarList_arg(arguments, XDeviceNetworkSocketHandle);
        XDeviceNetwork_serverClose(ctx->m_base.m_fd, socketHandle);
        return true;
    }
    default:
        break;
    }

    if (ctx->m_isServer)
        return false;

    switch (command) {
    case XDeviceCommand_Cancel:
        XDeviceNetwork_socketDisconnect(ctx->m_base.m_fd);
        ctx->m_endpoint.m_socket->state = XAbstractSocket_UnconnectedState;
        return true;
    case XDeviceCommand_Poll:
        if (out) {
            bool connected = ctx->m_isServer || ctx->m_connected;
            if (out->m_size != sizeof(bool))
                return setContextError(ctx, XDeviceError_InvalidArgument);
            memcpy(out->data, &connected, sizeof(connected));
            XVarList_start(out);
        }
        return true;
    case XDeviceNetworkCommand_HandleEvent:
    {
        XEvent* event;
        if (!arguments || arguments->m_size != sizeof(XEvent*))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        event = XVarList_arg(arguments, XEvent*);
        if (!event) return setContextError(ctx, XDeviceError_InvalidArgument);
        return XDeviceNetwork_socketHandleEvent(ctx->m_base.m_fd, event);
    }
    case XDeviceNetworkCommand_ContinueRead:
        XDeviceNetwork_socketContinueRead(ctx->m_base.m_fd, ctx->m_socketType == XDeviceNetwork_Udp);
        return true;
    case XDeviceNetworkCommand_ContinueWrite:
        XDeviceNetwork_socketContinueWrite(ctx->m_base.m_fd,
                                     XIODevicePrivate_getOrCreateWriteBuffer(ctx->m_endpoint.m_socket->base.m_d,
                                                                              ctx->m_endpoint.m_socket->base.m_currentWriteChannel),
                                     ctx->m_socketType == XDeviceNetwork_Udp);
        return true;
    case XDeviceNetworkCommand_SetSocketOption:
    {
        int option;
        const XVariant* value;
        if (!arguments || arguments->m_size != sizeof(int) + sizeof(const XVariant*))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        option = XVarList_arg(arguments, int);
        value = XVarList_arg(arguments, const XVariant*);
        if (!value)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        return XDeviceNetwork_socketSetOption(ctx->m_base.m_fd, option,
                                        XVariant_data((XVariant*)value));
    }
    case XDeviceNetworkCommand_GetSocketOption:
    {
        int option;
        void* result;
        if (!arguments || arguments->m_size != sizeof(int) || !out || out->m_size != sizeof(int))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        option = XVarList_arg(arguments, int);
        result = XDeviceNetwork_socketGetOption(ctx->m_base.m_fd, option);
        if (!result) return setContextError(ctx, XDeviceError_IoFail);
        memcpy(out->data, result, sizeof(int));
        XVarList_start(out);
        return true;
    }
    case XDeviceNetworkCommand_GetLastDatagramSender:
    {
        XHostAddress* address;
        uint16_t* port;
        if (in || !out || out->m_size != sizeof(address) + sizeof(port))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(out);
        address = XVarList_arg(out, XHostAddress*);
        port = XVarList_arg(out, uint16_t*);
        if (!address && !port)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceNetwork_platformGetLastDatagramSender(ctx->m_base.m_fd, address, port)) {
            return setContextError(ctx, XDeviceError_IoFail);
        }
        XVarList_start(out);
        return true;
    }
    case XDeviceNetworkCommand_GetReadBuffer:
    {
        const char* buffer;
        if (in || !out || out->m_size != sizeof(buffer))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        buffer = XDeviceNetwork_socketReadBuffer(ctx->m_base.m_fd);
        memcpy(out->data, &buffer, sizeof(buffer));
        XVarList_start(out);
        return buffer != NULL;
    }
    case XDeviceNetworkCommand_SendDatagram:
    {
        const void* data;
        int64_t size;
        const XHostAddress* address;
        uint16_t port;
        int64_t written;
        if (!arguments || arguments->m_size != sizeof(data) + sizeof(size) + sizeof(address) + sizeof(port) ||
            !out || out->m_size != sizeof(written))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        data = XVarList_arg(arguments, const void*);
        size = XVarList_arg(arguments, int64_t);
        address = XVarList_arg(arguments, const XHostAddress*);
        port = XVarList_arg(arguments, uint16_t);
        if (!data || size < 0 || !address)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        if (ctx->m_socketType != XDeviceNetwork_Udp)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        written = XDeviceNetwork_socketWrite(ctx->m_base.m_fd, data, size,
            XDeviceNetwork_Udp, address, port, NULL);
        if (written < 0) return setContextError(ctx, XDeviceError_IoFail);
        memcpy(out->data, &written, sizeof(written));
        XVarList_start(out);
        return true;
    }
    case XDeviceNetworkCommand_SetMulticastGroup:
    {
        bool join;
        const XHostAddress* group;
        uint32_t interfaceIndex;
        intptr_t descriptor;
        if (!arguments || arguments->m_size != sizeof(join) + sizeof(group) + sizeof(interfaceIndex))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        join = XVarList_arg(arguments, bool);
        group = XVarList_arg(arguments, const XHostAddress*);
        interfaceIndex = XVarList_arg(arguments, uint32_t);
        descriptor = XDeviceNetwork_socketDescriptor(ctx->m_base.m_fd);
        if (descriptor < 0 || !group ||
            !XDeviceNetwork_multicastGroup(descriptor, join, group, interfaceIndex))
            return setContextError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceNetworkCommand_SetMulticastInterface:
    {
        uint32_t interfaceIndex;
        intptr_t descriptor;
        if (!arguments || arguments->m_size != sizeof(interfaceIndex))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        interfaceIndex = XVarList_arg(arguments, uint32_t);
        descriptor = XDeviceNetwork_socketDescriptor(ctx->m_base.m_fd);
        if (descriptor < 0 || XDeviceNetwork_multicastOp(descriptor, XMC_SetIf, &interfaceIndex) != 0)
            return setContextError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceNetworkCommand_GetMulticastInterface:
    {
        uint32_t* interfaceIndex;
        intptr_t descriptor;
        if (in || !out || out->m_size != sizeof(interfaceIndex))
            return setContextError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(out);
        interfaceIndex = XVarList_arg(out, uint32_t*);
        if (!interfaceIndex)
            return setContextError(ctx, XDeviceError_InvalidArgument);
        descriptor = XDeviceNetwork_socketDescriptor(ctx->m_base.m_fd);
        if (descriptor < 0 || XDeviceNetwork_multicastOp(descriptor, XMC_GetIf, interfaceIndex) != 0)
            return setContextError(ctx, XDeviceError_IoFail);
        XVarList_start(out);
        return true;
    }
    default:
        return false;
    }
}

#if XNETWORK_USE_LWIP
#include "lwip/opt.h"     /* NO_SYS 宏定义 */
#include "lwip/sys.h"     /* sys_prot_t, sys_arch_protect/unprotect */

/* lwIP 协议栈锁：
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=1: sys_arch_protect（递归锁）
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0: 单线程，锁为空操作（零开销）
 *   NO_SYS=0: tcpip_thread 处理锁，poll 无需额外锁 */
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
extern sys_prot_t sys_arch_protect(void);
extern void sys_arch_unprotect(sys_prot_t pval);
typedef sys_prot_t XLwipPollLock;
#define XLWIP_LOCK()        sys_arch_protect()
#define XLWIP_UNLOCK(l)     sys_arch_unprotect(l)
#elif NO_SYS
typedef int XLwipPollLock;
#define XLWIP_LOCK()        0
#define XLWIP_UNLOCK(l)     (void)(l)
#else
typedef int XLwipPollLock;
#define XLWIP_LOCK()        0
#define XLWIP_UNLOCK(l)     (void)(l)
#endif

/* 事件循环轮询回调句柄；打开第一个网络设备时注册，关闭最后一个时注销。 */
static XHandle g_lwipPollHandle = NULL;
static int g_lwipDeviceCount = 0;

/* 轮询 lwIP pcap 网卡数据包，加锁调用 XDeviceNetwork_poll()。 */
static void xLwipPollInternal(void) {
    XLwipPollLock p = XLWIP_LOCK();
    XDeviceNetwork_poll();
    XLWIP_UNLOCK(p);
}

/* 事件循环轮询回调 */
static bool xLwipEventLoopPoll(void* userData)
{
    (void)userData;
    xLwipPollInternal();
    return true;
}
#endif /* XNETWORK_USE_LWIP */

static XDeviceNetwork g_deviceNetwork;

bool XDeviceNetwork_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceNetwork_init(&g_deviceNetwork);
    if (!XDevice_register(&g_deviceNetwork.m_base)) return false;
    registered = true;
    return true;
}

#else

bool XDeviceNetwork_register(void) { return false; }

#endif
