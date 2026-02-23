#include"XCommunicatorBase.h"
#include"XTimerGroupWheel.h"
#include"XCoreApplication.h"
#include"XThread.h"
#include<string.h>
void XCommunicatorBase_init(XCommunicatorBase* comm, XIODevice* io)
{
    //开始初始化
    memset(((XObject*)comm)+1, 0, sizeof(XCommunicatorBase)-sizeof(XObject));
    XObject_init(comm);
    XClassGetVtable(comm) = XCommunicatorBase_class_init();
    comm->m_io = io;
   
    XObject_setPollingInterval(comm, 2);
}
bool XCommunicatorBase_connect_base(XCommunicatorBase* comm)
{
    if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
        return false;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_Connect, bool(*)(XCommunicatorBase*))(comm);
}

bool XCommunicatorBase_disconnect_base(XCommunicatorBase* comm)
{
    if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
        return false;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_Disconnect, bool(*)(XCommunicatorBase*))(comm);
}

size_t XCommunicatorBase_send_base(XCommunicatorBase* comm, const void* data, size_t size)
{
    if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(comm), ""))
        return 0;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_Send, size_t(*)(XCommunicatorBase*,const void*,size_t))(comm,data,size);
}

size_t XCommunicatorBase_recv_base(XCommunicatorBase* comm, void* data, size_t maxSize)
{
    if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(comm), ""))
        return 0;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_Recv, size_t(*)(XCommunicatorBase*,void*, size_t))(comm, data, maxSize);
}

bool XCommunicatorBase_sendAsync_base(XCommunicatorBase* comm, const void* data, size_t size)
{
    if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(comm), ""))
        return false;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_SendAsync, bool(*)(XCommunicatorBase*, const void*, size_t))(comm, data, size);
}

bool XCommunicatorBase_recvAsync_base(XCommunicatorBase* comm, size_t maxSize)
{
    if (ISNULL(comm, "")  || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(comm), ""))
        return false;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_RecvAsync, bool(*)(XCommunicatorBase*,size_t))(comm, maxSize);
}

bool XCommunicatorBase_isConnected_base(XCommunicatorBase* comm)
{
    if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
        return false;
    return XClassGetVirtualFunc(comm, EXCommunicatorBase_IsConnected, bool(*)(XCommunicatorBase*))(comm);
}
void XCommunicatorBase_setOption_base(XCommunicatorBase* comm, int optionId, const void* value, size_t size)
{
    if (ISNULL(comm, "") || ISNULL(value, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(comm), ""))
        return;
    XClassGetVirtualFunc(comm, EXCommunicatorBase_SetOption, void(*)(XCommunicatorBase*, int, const void*, size_t))(comm,optionId,value,size);
}

void XCommunicatorBase_getOption_base(XCommunicatorBase* comm, int optionId, void* value, size_t* size)
{
    if (ISNULL(comm, "") || ISNULL(value, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(comm), ""))
        return;
    XClassGetVirtualFunc(comm, EXCommunicatorBase_GetOption, void(*)(XCommunicatorBase*, int, void*, size_t))(comm, optionId, value, size);
}

void* XCommunicatorBase_connect_signal(XCommunicatorBase* comm)
{
    XEmitSignal(comm, XCommunicatorBase_connect_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCommunicatorBase_disconnect_signal(XCommunicatorBase* comm)
{
    XEmitSignal(comm, XCommunicatorBase_disconnect_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCommunicatorBase_recvBuffFull_signal(XCommunicatorBase* comm, XVector* buffer)
{
    XEmitSignal(comm, XCommunicatorBase_recvBuffFull_signal, buffer, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
