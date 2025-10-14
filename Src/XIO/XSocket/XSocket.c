#include"XSocket.h"
#include"XMemory.h"
#include"XString.h"
#include"XTimer.h"
#include"XEvent.h"
#include"XEventDispatcher.h"
#include<string.h>
#include<stdarg.h>
void VXSocketBase_waitForConnected(XSocketBase* so, int msecs);
void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs);
//写入事件回调
static void WriteEventCB(XEvent* event)
{
    XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTING_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_CONNECTED_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
        XSocket_connected_signal(so);
    }

}
//读取事件回调
static void ReadEventCB(XEvent* event)
{
    XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    {
        XIODeviceBase_readyRead_signal(so);
    }
}
//错误事件回调
static void ErrorEventCB(XEvent* event)
{
    XSocket* so = event->receiver;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
        XSocket_disconnected_signal(so);
        XSocket_stateChanged_signal(so, XSOCKET_UNCONNECTED_STATE);
        XIODeviceBase_close_base(so);
    }
}

void XSocketBase_init(XSocketBase* socket)
{
    if (socket == NULL)
        return;
    memset(((XIODeviceBase*)socket) + 1, 0, sizeof(XSocketBase) - sizeof(XIODeviceBase));
    XIODeviceBase_init(socket);
   
    socket->m_peerName = XString_create_utf8(NULL);
    socket->m_peerAddress = XString_create_utf8(NULL);

    //XObject_setPollingInterval(socket,50);
    // return;
    XObject_addEventFilter(socket, XEVENT_WRITE, WriteEventCB, NULL);
    XObject_addEventFilter(socket, XEVENT_READY, ReadEventCB, NULL);
    XObject_addEventFilter(socket, XEVENT_ERROR, ErrorEventCB, NULL);
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
    if(socket)
        XSignalSlot_emit(((XObject*)socket)->m_signalSlot, XSocket_connected_signal, NULL, XEVENT_PRIORITY_NORMAL);
    return XSocket_connected_signal;
}

void* XSocket_disconnected_signal(XSocket* socket)
{
    if (socket)
        XSignalSlot_emit(((XObject*)socket)->m_signalSlot, XSocket_disconnected_signal, NULL, XEVENT_PRIORITY_NORMAL);
    return XSocket_disconnected_signal;
}

void* XSocket_stateChanged_signal(XSocket* socket, XSocketState state)
{
    if (socket)
    {
        XSignalSlot_emit(((XObject*)socket)->m_signalSlot, XSocket_stateChanged_signal, (size_t)state, XEVENT_PRIORITY_NORMAL);
    }
    return XSocket_stateChanged_signal;
}


void VXSocketBase_waitForConnected(XSocketBase* so, int msecs)
{
    if (!so || so->m_state != XSOCKET_CONNECTING_STATE) return;
    XEventLoop* loop = XEventLoop_create();
    XTimer* timer = NULL;
    XObject_connect(so, XSignal(XSocket_connected_signal), loop, XEventLoop_quit_base, XConnectionType_Auto);
    if (msecs > 0)
    {
        timer = XTimer_create();
        XTimer_setInterval_base(timer, msecs);
        XTimer_setTimeout_base(timer, msecs);
        XTimerBase_setSingleShote(timer, true);
        XObject_connect(timer, XSignal(XTimer_timeout_signal), loop, XEventLoop_quit_base, XConnectionType_Auto);
        XTimer_start_base(timer);
    }
    if (so->m_state == XSOCKET_CONNECTING_STATE)
    {
        XEventLoop_exec_base(loop);
    }
    else
    {
        XTimer_delete_base(timer);
    }
    XEventLoop_delete_base(loop);
}

void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs)
{
    if (!so || so->m_state == XSOCKET_UNCONNECTED_STATE) return;

    XEventLoop* loop = XEventLoop_create();
    XTimer* timer = NULL;
    XObject_connect(so, XSignal(XSocket_disconnected_signal), loop, XEventLoop_quit_base, XConnectionType_Auto);
    if (msecs > 0)
    {
        timer = XTimer_create();
        XTimer_setInterval_base(timer, msecs);
        XTimer_setTimeout_base(timer, msecs);
        XTimerBase_setSingleShote(timer, true);
        XObject_connect(timer, XSignal(XTimer_timeout_signal), loop, XEventLoop_quit_base, XConnectionType_Auto);
        XTimer_start_base(timer);
    }
    if (so->m_state == XSOCKET_CONNECTED_STATE)
    {
        XEventLoop_exec_base(loop);
    }
    else
    {
        XTimer_delete_base(timer);
    }
    XEventLoop_delete_base(loop);
}
