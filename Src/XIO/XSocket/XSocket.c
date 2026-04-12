#include"XSocket.h"
#include"XMemory.h"
#include"XString.h"
#include"XTimer.h"
#include"XEvent.h"
#include"XEventLoop.h"
#include<string.h>
#include<stdarg.h>
void VXSocketBase_waitForConnected(XSocketBase* so, int msecs);
void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs);
//写入事件回调
static void WriteEventCB(XEvent* event)
{
   /* XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTING_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_CONNECTED_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
        XSocket_connected_signal(so);
    }*/

}
//读取事件回调
static void ReadEventCB(XEvent* event)
{
   /* XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    {
        XIODevice_readyRead_signal(so);
    }*/
}
//错误事件回调
static void ErrorEventCB(XEvent* event)
{
    /*XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
        XSocket_disconnected_signal(so);
        XSocket_stateChanged_signal(so, XSOCKET_UNCONNECTED_STATE);
        XIODevice_close_base(so);
    }*/
}

void XSocketBase_init(XSocketBase* socket)
{
    if (socket == NULL)
        return;
    memset(((XIODevice*)socket) + 1, 0, sizeof(XSocketBase) - sizeof(XIODevice));
    XIODevice_init(socket);
   
    socket->m_peerName = XString_create_utf8(NULL);
    socket->m_peerAddress = XString_create_utf8(NULL);

    //XObject_setPollTime(socket,50);
    // return;
    /*XObject_addEventFilter(socket, XEVENT_WRITE, WriteEventCB, NULL);
    XObject_addEventFilter(socket, XEVENT_READY, ReadEventCB, NULL);
    XObject_addEventFilter(socket, XEVENT_ERROR, ErrorEventCB, NULL);*/
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

void* XSocket_connected_signal(XSocket* socket)
{
    XEmitSignal(socket, XSocket_connected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSocket_disconnected_signal(XSocket* socket)
{
    XEmitSignal(socket, XSocket_disconnected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSocket_stateChanged_signal(XSocket* socket, XSocketState state)
{
    XEmitSignal(socket, XSocket_stateChanged_signal, (size_t)state, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}


void VXSocketBase_waitForConnected(XSocketBase* so, int msecs)
{
    if (!so || so->m_state != XSOCKET_CONNECTING_STATE) return;
    XEventLoop* loop = XEventLoop_create();
    XTimer* timer = NULL;
    //XObject_connect1(so, XSignal(XSocket_connected_signal), loop, XEventLoop_quit, XConnectionType_Auto);
    if (msecs > 0)
    {
        timer = XTimer_create();
        XTimer_setInterval(timer, msecs);
        XTimer_setTimeout(timer, msecs);
        XTimerBase_setSingleShot(timer, true);
        //XObject_connect1(timer, XSignal(XTimer_timeout_signal), loop, XEventLoop_quit, XConnectionType_Auto);
        XTimer_start_base(timer);
    }
    if (so->m_state == XSOCKET_CONNECTING_STATE)
    {
        XEventLoop_exec(loop);
    }
    else
    {
        XTimer_delete_base(timer);
    }
    XEventLoop_deleteLater(loop);
}

void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs)
{
    if (!so || so->m_state == XSOCKET_UNCONNECTED_STATE) return;

    XEventLoop* loop = XEventLoop_create();
    XTimer* timer = NULL;
    //XObject_connect1(so, XSignal(XSocket_disconnected_signal), loop, XEventLoop_quit, XConnectionType_Auto);
    if (msecs > 0)
    {
        timer = XTimer_create();
        XTimer_setInterval(timer, msecs);
        XTimer_setTimeout(timer, msecs);
        XTimerBase_setSingleShot(timer, true);
        //XObject_connect1(timer, XSignal(XTimer_timeout_signal), loop, XEventLoop_quit, XConnectionType_Auto);
        XTimer_start_base(timer);
    }
    if (so->m_state == XSOCKET_CONNECTED_STATE)
    {
        XEventLoop_exec(loop);
    }
    else
    {
        XTimer_delete_base(timer);
    }
    XEventLoop_deleteLater(loop);
}
