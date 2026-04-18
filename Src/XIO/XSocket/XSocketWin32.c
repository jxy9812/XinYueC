#ifdef WIN32
#include "XSocketWin32.h"
#include "XCircularQueue.h"
#include "XMemory.h"
#include "XString.h"
#include "XEvent.h"
//#include "XTimer.h"
//#include "XEventDispatcher.h"
#include "XPrintf.h"
#include "XCoreApplication.h"
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
void VXSocketBase_waitForConnected(XSocketBase* so, int msecs);
void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs);

// 前向声明所有虚函数
static void VXSocketBase_connectToHost(XSocket* so, const char* hostName, uint16_t port, XIODeviceBaseMode mode);
static void VXSocketBase_disconnectFromHost(XSocketBase* so);
static const char* VXSocketBase_localAddress(XSocket* so);
static uint16_t VXSocketBase_localPort(XSocket* so);
static void VXIODevice_poll(XSocket* so);
static bool VXIODevice_open(XSocket* so, XIODeviceBaseMode mode);
static bool VXIODevice_isOpen(XSocket* so);
static bool VXIODevice_close(XSocket* so);
static size_t VXIODevice_write(XSocket* so, const char* data, size_t maxSize);
static size_t VXIODevice_writeFull(XSocket* so);
static size_t VXIODevice_read(XSocket* so, char* data, size_t maxSize);
static size_t VXIODevice_getBytesAvailable(XSocket* so);
static size_t VXIODevice_getBytesToWrite(XSocket* so);
static bool VXIODevice_atEnd(XSocket* so);
static void VXIODevice_setWriteBuffer(XSocket* so, size_t count);
static void VXIODevice_setReadBuffer(XSocket* so, size_t count);
static void VXIODevice_deinit(XSocket* so);

XVtable* XSocket_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSOCKETBASE_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_DEFAULT(XIODevice_class_init());
    void* table[] = {
        VXSocketBase_connectToHost, VXSocketBase_disconnectFromHost,
        VXSocketBase_waitForConnected, VXSocketBase_waitForDisconnected,
        VXSocketBase_localAddress, VXSocketBase_localPort
    };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXIODevice_open);
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsOpen, VXIODevice_isOpen);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXIODevice_close);
   /* XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesAvailable, VXIODevice_getBytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesToWrite, VXIODevice_getBytesToWrite);*/
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_AtEnd, VXIODevice_atEnd);
    /*XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetReadBuffer, VXIODevice_setReadBuffer);*/
#if SHOWCONTAINERSIZE
    XPrintf("XSocket size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSocket* XSocket_create()
{
    XSocket* so = XMemory_malloc(sizeof(XSocket));
    XSocket_init(so);
    Set_Class_MemoryFree(so, XFree);
    return so;
}

void XSocket_init(XSocket* so)
{
    if (so == NULL)
        return;
    memset(((XSocketBase*)so) + 1, 0, sizeof(XSocket) - sizeof(XSocketBase));
    XSocketBase_init((XSocketBase*)so);
    XClassGetVtable(so) = XSocket_class_init();
    
    so->m_socket = INVALID_SOCKET;
    // 初始化网络事件结构
   /* so->m_netEvents = XMemory_malloc(sizeof(WSANETWORKEVENTS));
    if (so->m_netEvents) {
        memset(so->m_netEvents, 0, sizeof(WSANETWORKEVENTS));
    }*/
}

void VXSocketBase_connectToHost(XSocket* so, const char* hostName, uint16_t port, XIODeviceBaseMode mode)
{
    if (XSocket_state((XSocketBase*)so) != XSOCKET_UNCONNECTED_STATE || hostName == NULL)
        return;
    XSocketBase* base = (XSocketBase*)so;
    XString_clear_base(base->m_peerName);
    XString_append_utf8(base->m_peerName, hostName);
    base->m_peerPort = port;
    XIODevice_open_base((XIODevice*)so, mode);
}

void VXSocketBase_disconnectFromHost(XSocketBase* so)
{
    if (so && so->m_state != XSOCKET_UNCONNECTED_STATE) {
        VXIODevice_close((XSocket*)so);
    }
}


const char* VXSocketBase_localAddress(XSocket* so)
{
    XSocketBase* base = (XSocketBase*)so;
    if (!so || so->m_socket == INVALID_SOCKET) return NULL;

    static char localIP[46];
    struct sockaddr_storage localAddr;
    socklen_t addrLen = sizeof(localAddr);

    if (getsockname(so->m_socket, (struct sockaddr*)&localAddr, &addrLen) == SOCKET_ERROR) {
        return NULL;
    }

    void* addrPtr;
    if (localAddr.ss_family == AF_INET) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)&localAddr;
        addrPtr = &(ipv4->sin_addr);
        inet_ntop(AF_INET, addrPtr, localIP, sizeof(localIP));
    }
    else {
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)&localAddr;
        addrPtr = &(ipv6->sin6_addr);
        inet_ntop(AF_INET6, addrPtr, localIP, sizeof(localIP));
    }

    return localIP;
}

uint16_t VXSocketBase_localPort(XSocket* so)
{
    XSocketBase* base = (XSocketBase*)so;
    if (!so || so->m_socket == INVALID_SOCKET) return 0;

    struct sockaddr_storage localAddr;
    socklen_t addrLen = sizeof(localAddr);

    if (getsockname(so->m_socket, (struct sockaddr*)&localAddr, &addrLen) == SOCKET_ERROR) {
        return 0;
    }

    if (localAddr.ss_family == AF_INET) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)&localAddr;
        return ntohs(ipv4->sin_port);
    }
    else {
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)&localAddr;
        return ntohs(ipv6->sin6_port);
    }
}

// 写入数据
static size_t VXIODevice_write(XSocket* so, const char* data, size_t maxSize) 
{
    if (so == NULL || data == NULL || so->m_socket == INVALID_SOCKET) {
        return 0;
    }

    XIODevice* io = (XIODevice*)so; // 转换为父类IO设备指针
    // 检查设备是否允许写入
    if (!(io->m_openMode & XIODevice_WriteOnly)) {
        return 0;
    }

    size_t totalSent = 0;
    return 0;
    //// 情况1：无写入缓冲区，直接发送（兼容原有逻辑）
    //if (io->m_writeBuffer == NULL) {
    //    while (totalSent < maxSize) {
    //        int result = send(so->m_socket, data + totalSent, (int)(maxSize - totalSent), 0);
    //        if (result == SOCKET_ERROR) {
    //            if (WSAGetLastError() == WSAEWOULDBLOCK) {
    //                // 非阻塞模式下暂时无法发送，退出循环
    //                break;
    //            }
    //            else {
    //                // 其他错误，返回已发送字节数
    //                return totalSent;
    //            }
    //        }
    //        totalSent += result;
    //    }
    //    // 直接发送成功后触发信号
    //    if (totalSent > 0) {
    //        XIODevice_bytesWritten_signal(io, totalSent);
    //    }
    //    return totalSent;
    //}

    //// 情况2：有写入缓冲区，先写入队列再按需刷写
    //while (totalSent < maxSize) {
    //    // 尝试将数据写入缓冲区（按字节入队，适配环形队列的char类型）
    //    if (XCircularQueue_push_base(io->m_writeBuffer, data + totalSent)) {
    //        totalSent++; // 成功写入1字节
    //    }
    //    else {
    //        //// 缓冲区满，先刷写缓冲区
    //        //size_t flushed = VXIODevice_writeFull(so);
    //        //if (flushed == 0) {
    //        //    // 刷写失败（如非阻塞暂时无法发送），退出循环
    //        //    break;
    //        //}
    //    }
    //}

    ////// 若缓冲区仍有空间，尝试刷写一次（非必须，可根据需求调整）
    ////if (totalSent > 0 && !XCircularQueue_isFull_base(io->m_writeBuffer)) {
    ////    VXIODevice_writeFull(so);
    ////}

    //return totalSent; // 返回写入缓冲区的总字节数
}

// 将剩余的数据刷入设备
static size_t VXIODevice_writeFull(XSocket* so) 
{
    if (so == NULL || so->m_socket == INVALID_SOCKET) {
        return 0;
    }
    return 0;
    //XIODevice* io = (XIODevice*)so; // 转换为父类IO设备指针
    //XCircularQueue* writeBuffer = io->m_writeBuffer;

    //// 缓冲区不存在或为空，直接返回
    //if (writeBuffer == NULL || XCircularQueue_isEmpty_base(writeBuffer)) {
    //    return 0;
    //}

    //size_t totalSent = 0; // 总发送字节数
    //char elemBuffer[XBuffSize];
    //size_t elemSize;

    //// 循环发送缓冲区中的数据
    //while (!XCircularQueue_isEmpty_base(writeBuffer)) {
    //    elemSize = 0;
    //    // 从环形队列读取数据到临时缓冲区（最多XBuffSize字节）
    //    while (elemSize < XBuffSize &&
    //        XCircularQueue_receive_base(writeBuffer, &elemBuffer[elemSize])) {
    //        elemSize++; // 成功读取1字节，累加计数
    //    }

    //    if (elemSize == 0) {
    //        break; // 未读取到数据，退出循环
    //    }

    //    // 发送临时缓冲区中的数据
    //    int sentBytes = send(so->m_socket, elemBuffer, (int)elemSize, 0);
    //    if (sentBytes == SOCKET_ERROR) {
    //        int err = WSAGetLastError();
    //        if (err == WSAEWOULDBLOCK) {
    //            // 非阻塞模式下暂时无法发送，将未发送的数据放回缓冲区
    //            for (size_t i = 0; i < elemSize; i++) {
    //                XCircularQueue_push_base(writeBuffer, &elemBuffer[i]);
    //            }
    //            // 短暂等待后重试（避免CPU空转）
    //            Sleep(10);
    //            break; // 退出本次循环，等待下次poll触发再试
    //        }
    //        else {
    //            // 其他错误：将未发送的数据放回缓冲区，返回已发送字节数
    //            XPrintf("发送失败，错误码: %d\n", err);
    //          /*  for (size_t i = 0; i < elemSize; i++) {
    //                XCircularQueue_push_base(writeBuffer, &elemBuffer[i]);
    //            }*/
    //            return totalSent;
    //        }
    //    }
    //    else if (sentBytes < (int)elemSize) {
    //        // 部分发送成功，将未发送的剩余数据放回缓冲区
    //        size_t unsent = elemSize - sentBytes;
    //        /*for (size_t i = 0; i < unsent; i++) {
    //            XCircularQueue_push_base(writeBuffer, &elemBuffer[sentBytes + i]);
    //        }*/
    //        totalSent += sentBytes;
    //        // 触发信号通知已发送的字节数
    //        XIODevice_bytesWritten_signal(io, sentBytes);
    //        break; // 部分发送后退出，等待下次再试
    //    }
    //    else {
    //        // 全部发送成功
    //        totalSent += elemSize;
    //        // 触发信号通知已发送的字节数
    //        XIODevice_bytesWritten_signal(io, elemSize);
    //    }
    //}

    //return totalSent;
}

// 读取数据
static size_t VXIODevice_read(XSocket* so, char* data, size_t maxSize) {
    if (so == NULL || data == NULL || so->m_socket == INVALID_SOCKET) {
        return 0;
    }

    int result = recv(so->m_socket, data, (int)maxSize, 0);
    if (result == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            // 非阻塞模式下，暂时没有数据可读
            return 0;
        }
        else {
            // 发生其他错误
            return 0;
        }
    }
    return (size_t)result;
}

// 获取可供读取的字节数
static size_t VXIODevice_getBytesAvailable(XSocket* so) {
    if (so == NULL || so->m_socket == INVALID_SOCKET) {
        return 0;
    }

    u_long bytesAvailable = 0;
    if (ioctlsocket(so->m_socket, FIONREAD, &bytesAvailable) == SOCKET_ERROR) {
        return 0;
    }
    //if(bytesAvailable)XPrintf("可读取数据:%d\n", bytesAvailable);
    return (size_t)bytesAvailable;
}

// 查询当前待写入设备的数据量
static size_t VXIODevice_getBytesToWrite(XSocket* so) {
    // 这里假设没有额外的写入缓冲区，直接返回 0
    return 0;
}

// 是否到达末尾
static bool VXIODevice_atEnd(XSocket* so) {
    if (so == NULL || so->m_socket == INVALID_SOCKET) {
        return false;
    }

    // 简单判断，如果连接关闭则认为到达末尾
    return so->m_class.m_state == XSOCKET_UNCONNECTED_STATE;
}

// 设置写入缓冲区
static void VXIODevice_setWriteBuffer(XSocket* so, size_t count) {
    if (so == NULL || so->m_socket == INVALID_SOCKET) {
        return;
    }

    // 设置发送缓冲区大小
    int result = setsockopt(so->m_socket,
        SOL_SOCKET,
        SO_SNDBUF,
        (const char*)&count,
        sizeof(count));

    if (result == SOCKET_ERROR) {
        // 处理错误
        XPrintf("设置发送缓冲区失败: %d\n", WSAGetLastError());
    }
}

// 设置读取缓冲区
static void VXIODevice_setReadBuffer(XSocket* so, size_t count) {
    if (so == NULL || so->m_socket == INVALID_SOCKET) {
        return;
    }

    // 设置接收缓冲区大小
    int result = setsockopt(so->m_socket,
        SOL_SOCKET,
        SO_RCVBUF,
        (const char*)&count,
        sizeof(count));

    if (result == SOCKET_ERROR) {
        // 处理错误
        XPrintf("设置接收缓冲区失败: %d\n", WSAGetLastError());
    }
}

void VXIODevice_deinit(XSocket* so)
{
    if (!so) return;

    // 确保设备已关闭
    if (XIODevice_isOpen((XIODevice*)so)) {
        VXIODevice_close(so);
    }


    // 释放地址信息
    if (so->m_addrInfo) {
        freeaddrinfo(so->m_addrInfo);
        so->m_addrInfo = NULL;
    }

    // 清理事件对象和网络事件结构
   /* if (so->m_pollEvent != NULL) {
        WSACloseEvent(so->m_pollEvent);
        so->m_pollEvent = NULL;
    }*/

    /*if (so->m_netEvents)
    {
        XMemory_free(so->m_netEvents);
        so->m_netEvents = NULL;
    }*/

    // 清理Winsock（仅在删除对象时调用一次）
    WSACleanup();

    {
        XSocketBase* socket = so;
        if (socket->m_peerAddress)
        {
            XString_delete_base(socket->m_peerAddress);
            socket->m_peerAddress = NULL;
        }
        if (socket->m_peerName)
        {
            XString_delete_base(socket->m_peerName);
            socket->m_peerName = NULL;
        }
        // 释放父对象
        XVtableGetFunc(XIODevice_class_init(), EXClass_Deinit, void(*)(XIODevice*))(socket);
    }
}

void VXIODevice_poll(XSocket* so)
{
    return;
    //if (!so || so->m_socket == INVALID_SOCKET || !so->m_netEvents) return;

    //// 首次调用时初始化事件对象
    //if (so->m_pollEvent == NULL) {
    //    so->m_pollEvent = WSACreateEvent();
    //    if (so->m_pollEvent == WSA_INVALID_EVENT) {
    //        return;
    //    }
    //}

    //// 使用WSAWaitForMultipleEvents进行非阻塞轮询
    //DWORD result = WSAWaitForMultipleEvents(1, &so->m_pollEvent, FALSE, 0, FALSE);

    //if (result == WSA_WAIT_FAILED) {
    //    return;
    //}

    //// 如果有事件发生
    //if (result == WSA_WAIT_EVENT_0) {
    //    if (WSAEnumNetworkEvents(so->m_socket, so->m_pollEvent, so->m_netEvents) == SOCKET_ERROR) {
    //        return;
    //    }
    //    WSANETWORKEVENTS* netEvents = so->m_netEvents;

    //    // 根据不同的网络事件类型，创建相应的事件并添加到事件调度器
    //    if (netEvents->lNetworkEvents & FD_CONNECT) {
    //        if (netEvents->iErrorCode[FD_CONNECT_BIT] != 0) {
    //            // 连接错误
    //           /* XEvent* event = XEvent_create(so, XEVENT_SOCKET_ERROR, XTimerBase_getCurrentTime());
    //            XEventDispatcher_postEvent_base(XObject_eventDispatcher(so), event);*/
    //            if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
    //            {
    //                ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
    //                XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    //            }
    //        }
    //        else {
    //            // 连接成功
    //           /* XEvent* event = XEvent_create(so,XEVENT_SOCKET_CONNECTED, XTimerBase_getCurrentTime());
    //            XEventDispatcher_postEvent_base(XObject_eventDispatcher(so), event);*/
    //            if (((XSocketBase*)so)->m_state != XSOCKET_CONNECTED_STATE)
    //            {
    //                ((XSocketBase*)so)->m_state = XSOCKET_CONNECTED_STATE;
    //                XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    //            }
    //            XSocket_connected_signal(so);
    //        }
    //    }

    //    if (netEvents->lNetworkEvents & FD_CLOSE) {
    //        // 连接关闭
    //       // XEvent* event = XEvent_create(so, XEVENT_SOCKET_DISCONNECTED, XTimerBase_getCurrentTime());
    //       //// event->userData = eventData;
    //       // XEventDispatcher_postEvent_base(XObject_eventDispatcher(so), event);
    //        if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
    //        {
    //            ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
    //            XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    //        }
    //        XSocket_disconnected_signal(so);
    //    }

    //    if (netEvents->lNetworkEvents & FD_READ) 
    //    {
    //        // 有数据可读
    //        XIODevice_readyRead_signal(so);
    //        //XEvent* event = XEvent_create(so, XEVENT_SOCKET_DATA_READY, XTimerBase_getCurrentTime());
    //        ////event->userData = eventData;
    //        //XEventDispatcher_postEvent_base(XObject_eventDispatcher(so), event);
    //    }

    //    // 可以添加对FD_WRITE事件的处理...
    //}
}

bool VXIODevice_open(XSocket* so, XIODeviceBaseMode mode)
{
    if (XIODevice_isOpen((XIODevice*)so)|| so->m_class.m_state == XSOCKET_HOST_LOOKUP_STATE || so->m_class.m_state == XSOCKET_CONNECTING_STATE)
        return true;
    //至少要读或者写
    if (!(mode & XIODevice_ReadOnly || mode & XIODevice_WriteOnly))
        return false;
    if (XSocket_state((XSocketBase*)so) != XSOCKET_UNCONNECTED_STATE)
        return false;
    XSocketBase* base = (XSocketBase*)so;
    // 更新状态
    if (((XSocketBase*)so)->m_state != XSOCKET_HOST_LOOKUP_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_HOST_LOOKUP_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    }
    int result;

    // 初始化 Winsock
    WSADATA wsaData;
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        XPrintf("WSAStartup failed: %d\n", result);
        return false;
    }

    //// 关键修改：确保事件对象在使用前已创建
    //if (so->m_pollEvent == NULL) {
    //    so->m_pollEvent = WSACreateEvent();
    //    if (so->m_pollEvent == WSA_INVALID_EVENT) {
    //        XPrintf("WSACreateEvent failed\n");
    //        WSACleanup(); // 释放Winsock资源
    //        return false;
    //    }
    //}

    // 准备地址解析
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    // 根据IPv6启用状态设置地址族
    if (base->m_ipv6Enabled) {
        hints.ai_family = AF_UNSPEC; // 同时支持IPv4和IPv6
    }
    else {
        hints.ai_family = AF_INET;   // 仅支持IPv4
    }
    // 设置套接字类型
    switch (base->m_socketType) {
    case XSOCKET_TYPE_TCP:
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        break;
    case XSOCKET_TYPE_UDP:
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        break;
    case XSOCKET_TYPE_SCTP:
        hints.ai_socktype = SOCK_STREAM; // SCTP通常使用流
        hints.ai_protocol = IPPROTO_SCTP;
        break;
    default:
        if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
        {
            ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
            XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
        }
        WSACleanup(); // 释放Winsock资源
        return false;
    }

    // 将 uint16_t 端口转换为字符串
    char portStr[6]; // 最大 "65535" + '\0'
    sprintf(portStr, "%hu", base->m_peerPort);

    // 解析服务器地址和端口
    XPrintf("正在解析服务器地址: %s:%s\n", XString_c_str(base->m_peerName), portStr);
    result = getaddrinfo(XString_c_str(base->m_peerName), portStr, &hints, &(so->m_addrInfo));
    if (result != 0) {
        XPrintf("getaddrinfo failed: %d\n", result);
        if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
        {
            ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
            XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
        }
        WSACleanup(); // 释放Winsock资源
        return false;
    }

    // 尝试连接
    if (((XSocketBase*)so)->m_state != XSOCKET_CONNECTING_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_CONNECTING_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    }
    struct addrinfo* addr = so->m_addrInfo;
    bool connected = false;

    while (addr)
    {
        if (so->m_socket != INVALID_SOCKET) {
            //XCoreApplication_removeFd(so->m_socket);
            shutdown(so->m_socket, SD_BOTH);
            closesocket(so->m_socket);
            so->m_socket = INVALID_SOCKET;
        }
        // 创建套接字
        so->m_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (so->m_socket == INVALID_SOCKET) {
            XPrintf("socket failed: %d\n", WSAGetLastError());
            addr = addr->ai_next;
            continue;
        }

        // 设置套接字为非阻塞模式
        unsigned long nonBlocking = 1;
        if (ioctlsocket(so->m_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            XPrintf("ioctlsocket failed: %d\n", WSAGetLastError());
            closesocket(so->m_socket);
            so->m_socket = INVALID_SOCKET;
            addr = addr->ai_next;
            continue;
        }
        //XCoreApplication_addFd(so, so->m_socket, XEVENT_READY| XEVENT_WRITE);
        
        // 注册网络事件
       /* long events = FD_READ | FD_WRITE | FD_CLOSE | FD_CONNECT;
        if (WSAEventSelect(so->m_socket, so->m_pollEvent, events) == SOCKET_ERROR) {
            XPrintf("WSAEventSelect failed: %d\n", WSAGetLastError());
            closesocket(so->m_socket);
            so->m_socket = INVALID_SOCKET;
            addr = addr->ai_next;
            continue;
        }*/

        // 对于TCP套接字，尝试连接
        if (base->m_socketType == XSOCKET_TYPE_TCP) {
            result = connect(so->m_socket, addr->ai_addr, (int)addr->ai_addrlen);
            if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    XPrintf("connect failed: %d\n", error);
                    closesocket(so->m_socket);
                    so->m_socket = INVALID_SOCKET;
                    addr = addr->ai_next;
                    continue;
                }
                // 非阻塞模式下返回WSAEWOULDBLOCK表示连接正在进行中
            }
            connected = true;
        }
        else {
            // UDP不需要显式连接
            connected = true;
        }

        // 保存远程地址信息
        if (base->m_peerAddress) {
            XMemory_free(base->m_peerAddress);
        }

        char ipStr[46]; // 足够存储IPv6地址
        void* addrPtr;

        if (addr->ai_family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)addr->ai_addr;
            addrPtr = &(ipv4->sin_addr);
            inet_ntop(AF_INET, addrPtr, ipStr, sizeof(ipStr));
        }
        else {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)addr->ai_addr;
            addrPtr = &(ipv6->sin6_addr);
            inet_ntop(AF_INET6, addrPtr, ipStr, sizeof(ipStr));
        }

        base->m_peerAddress = XString_create_utf8(ipStr);
        break;
    }

    // 如果没有找到可用地址或连接失败
    if (!connected) {
        // 清理资源
        if (so->m_socket != INVALID_SOCKET) {
            closesocket(so->m_socket);
            so->m_socket = INVALID_SOCKET;
        }

        if (so->m_addrInfo) {
            freeaddrinfo(so->m_addrInfo);
            so->m_addrInfo = NULL;
        }

        if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
        {
            ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
            XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
        }
        WSACleanup(); // 释放Winsock资源
        return false;
    }

    ((XIODevice*)so)->m_openMode = mode;
    return true;
}
bool VXIODevice_isOpen(XSocket* so)
{
    return (((XIODevice*)so)->m_openMode != XIODevice_NotOpen)&&so->m_class.m_state== XSOCKET_CONNECTED_STATE;
}
bool VXIODevice_close(XSocket* so)
{
    if ((((XIODevice*)so)->m_openMode == XIODevice_NotOpen))
        return true;
    XSocketBase* base = (XSocketBase*)so;
    if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    {
        XIODevice_aboutToClose_signal(so);
        ((XSocketBase*)so)->m_state = XSOCKET_CLOSING_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    }
  
    // 关闭套接字相关资源
    if (so->m_socket != INVALID_SOCKET) {
        //XCoreApplication_removeFd(so->m_socket);
        shutdown(so->m_socket, SD_BOTH);
        closesocket(so->m_socket);
        so->m_socket = INVALID_SOCKET;
    }

    // 停止事件选择
  /*  if (so->m_pollEvent != NULL) {
        WSAEventSelect(so->m_socket, so->m_pollEvent, 0);
    }*/

    // 更新状态
    if (((XSocketBase*)so)->m_state != XSOCKET_UNCONNECTED_STATE)
    {
        ((XSocketBase*)so)->m_state = XSOCKET_UNCONNECTED_STATE;
        XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    }
    ((XIODevice*)so)->m_openMode = XIODevice_NotOpen;

    XSocket_disconnected_signal(so);
    return true;
}
#endif