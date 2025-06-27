#include"XSocketBase.h"
#include"XMemory.h"
#include"XString.h"
#include"XEvent.h"
#include<string.h>

XSocketBase * XSocketBase_create()
{
    XSocketBase* socket = XMemory_malloc(sizeof(XSocketBase));
    XSocketBase_init(socket);
    return socket;
}

void XSocketBase_init(XSocketBase* socket)
{
    if (socket == NULL)
        return;
    memset(((XIODeviceBase*)socket) + 1, 0, sizeof(XSocketBase) - sizeof(XIODeviceBase));
    XIODeviceBase_init(socket, NULL);
    XClassGetVtable(socket) = XSocketBase_class_init();
    socket->m_peerName = XString_create(NULL);
    socket->m_peerAddress = XString_create(NULL);
    socket->m_eventDispatcher = XEventDispatcher_createDefault(10); // 创建事件调度器
}

void XSocketBase_connectToHost_base(XSocketBase* socket, const char* hostName, uint16_t port, XIODeviceBaseMode mode)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocketBase_ConnectToHost, void(*)(XSocketBase*, const char*, uint16_t, XIODeviceBaseMode))(socket, hostName, port, mode);
}

void XSocketBase_disconnectFromHost_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocketBase_DisconnectFromHost, void(*)(XSocketBase*))(socket);
}

void XSocketBase_waitForConnected_base(XSocketBase* socket, int msecs)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocketBase_WaitForConnected, void(*)(XSocketBase*, int))(socket, msecs);
}

void XSocketBase_waitForDisconnected_base(XSocketBase* socket, int msecs)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocketBase_WaitForDisconnected, void(*)(XSocketBase*, int))(socket, msecs);
}

const char* XSocketBase_localAddress_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return NULL;
    return XClassGetVirtualFunc(socket, EXSocketBase_LocalAddress, const char* (*)(XSocketBase*))(socket);
}

uint16_t XSocketBase_localPort_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return 0;
    return XClassGetVirtualFunc(socket, EXSocketBase_LocalAddress, uint16_t(*)(XSocketBase*))(socket);
}

const char* XSocketBase_peerAddress(XSocketBase* socket)
{
    if (socket)
        return XString_c_str(socket->m_peerAddress);
    return NULL;
}

const char* XSocketBase_peerName(XSocketBase* socket)
{
    if (socket)
        return XString_c_str(socket->m_peerName);
    return NULL;
}

uint16_t XSocketBase_peerPort(XSocketBase* socket)
{
    if (socket)
        return socket->m_peerPort;
    return 0;
}

void XSocketBase_setSocketType(XSocketBase* socket, XSocketType type)
{
    if (socket && socket->m_state == XSOCKET_UNCONNECTED_STATE)
    {
        socket->m_socketType = type;
    }
}

XSocketType XSocketBase_socketType(const XSocketBase* socket)
{
    if (socket)
        return socket->m_socketType;
    return XSOCKET_TYPE_UNKNOWN;
}

XSocketState XSocketBase_state(const XSocketBase* socket)
{
    if (socket)
        return socket->m_state;
    return XSOCKET_UNCONNECTED_STATE;
}