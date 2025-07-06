#include"XSocket.h"
#include"XMemory.h"
#include"XString.h"
#include"XEvent.h"
#include "XEventDispatcher.h"
#include<string.h>

void XSocketBase_init(XSocketBase* socket)
{
    if (socket == NULL)
        return;
    memset(((XIODeviceBase*)socket) + 1, 0, sizeof(XSocketBase) - sizeof(XIODeviceBase));
    XIODeviceBase_init(socket);
   
    socket->m_peerName = XString_create(NULL);
    socket->m_peerAddress = XString_create(NULL);
}

void XSocket_connectToHost_base(XSocketBase* socket, const char* hostName, uint16_t port, XIODeviceBaseMode mode)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocket_ConnectToHost, void(*)(XSocketBase*, const char*, uint16_t, XIODeviceBaseMode))(socket, hostName, port, mode);
}

void XSocket_disconnectFromHost_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocket_DisconnectFromHost, void(*)(XSocketBase*))(socket);
}

void XSocket_waitForConnected_base(XSocketBase* socket, int msecs)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocket_WaitForConnected, void(*)(XSocketBase*, int))(socket, msecs);
}

void XSocket_waitForDisconnected_base(XSocketBase* socket, int msecs)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return;
    XClassGetVirtualFunc(socket, EXSocket_WaitForDisconnected, void(*)(XSocketBase*, int))(socket, msecs);
}

const char* XSocket_localAddress_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return NULL;
    return XClassGetVirtualFunc(socket, EXSocket_LocalAddress, const char* (*)(XSocketBase*))(socket);
}

uint16_t XSocket_localPort_base(XSocketBase* socket)
{
    if (ISNULL(socket, "") || ISNULL(XClassGetVtable(socket), ""))
        return 0;
    return XClassGetVirtualFunc(socket, EXSocket_LocalAddress, uint16_t(*)(XSocketBase*))(socket);
}

const char* XSocket_peerAddress(XSocketBase* socket)
{
    if (socket)
        return XString_c_str(socket->m_peerAddress);
    return NULL;
}

const char* XSocket_peerName(XSocketBase* socket)
{
    if (socket)
        return XString_c_str(socket->m_peerName);
    return NULL;
}

uint16_t XSocket_peerPort(XSocketBase* socket)
{
    if (socket)
        return socket->m_peerPort;
    return 0;
}

void XSocket_setSocketType(XSocketBase* socket, XSocketType type)
{
    if (socket && socket->m_state == XSOCKET_UNCONNECTED_STATE)
    {
        socket->m_socketType = type;
    }
}

XSocketType XSocket_socketType(const XSocketBase* socket)
{
    if (socket)
        return socket->m_socketType;
    return XSOCKET_TYPE_UNKNOWN;
}

XSocketState XSocket_state(const XSocketBase* socket)
{
    if (socket)
        return socket->m_state;
    return XSOCKET_UNCONNECTED_STATE;
}