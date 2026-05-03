#ifdef __linux__ || defined(__APPLE__) || defined(__BSD__)
#include "XSocketPosix.h"
#include "XMemory.h"
#include "XString.h"
#include "XEvent.h"
#include "XTimer.h"
#include "XEventDispatcher.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>  // 包含 FIONREAD 定义
#include <errno.h>

void VXSocketBase_waitForConnected(XSocketBase* so, int msecs);
void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs);

// 前向声明所有虚函数
static void VXSocketBase_connectToHost(XSocket* so, const char* hostName, uint16_t port, XIODeviceBaseMode mode);
static void VXSocketBase_disconnectFromHost(XSocketBase* so);
//static void VXSocketBase_waitForConnected(XSocketBase* so, int msecs);
//static void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs);
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
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSOCKETBASE_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承父类虚函数表
        XVTABLE_INHERIT_XCLASS(XIODevice);

    void* table[] = {
        VXSocketBase_connectToHost,
        VXSocketBase_disconnectFromHost,
        VXSocketBase_waitForConnected,
        VXSocketBase_waitForDisconnected,
        VXSocketBase_localAddress,
        VXSocketBase_localPort
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重载IO设备虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXIODevice_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsOpen, VXIODevice_isOpen);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXIODevice_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesAvailable, VXIODevice_getBytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesToWrite, VXIODevice_getBytesToWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_AtEnd, VXIODevice_atEnd);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetReadBuffer, VXIODevice_setReadBuffer);

#if SHOWCONTAINERSIZE
    XPrintf("XSocket(Linux) size:%d\n", XVtable_size(XVTABLE_DEFAULT));
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

    // 初始化Linux socket成员
    so->m_socket = -1;
    so->m_addrInfo = NULL;
    // so->m_pollfd.fd = -1;
    // so->m_pollfd.events = 0;
    // so->m_pollfd.revents = 0;
    // so->m_netEvents = 0;
}

static bool create_socket(XSocket* so)
{
    int domain = AF_INET;
    int type = (XSocket_socketType((XSocketBase*)so) == XSOCKET_TYPE_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (XSocket_socketType((XSocketBase*)so) == XSOCKET_TYPE_TCP) ? IPPROTO_TCP : IPPROTO_UDP;

    // 如果需要IPv6支持，这里可以添加判断
    if (((XSocketBase*)so)->m_ipv6Enabled) {
        domain = AF_INET6;
    }

    so->m_socket = socket(domain, type | SOCK_NONBLOCK, protocol);
    if (so->m_socket == -1) {
        return false;
    }
    XCoreApplication_addFd(so, so->m_socket, XEVENT_READY | XEVENT_WRITE);
    //so->m_pollfd.fd = so->m_socket;
    return true;
}

static void VXSocketBase_connectToHost(XSocket* so, const char* hostName, uint16_t port, XIODeviceBaseMode mode)
{
    if (XSocket_state((XSocketBase*)so) != XSOCKET_UNCONNECTED_STATE || !hostName)
        return;

    XSocketBase* base = (XSocketBase*)so;
    XString_clear_base(base->m_peerName);
    XString_append_utf8(base->m_peerName, hostName);
    base->m_peerPort = port;

    
    XIODevice_open_base((XIODevice*)so, mode);
}

static void VXSocketBase_disconnectFromHost(XSocketBase* so)
{
    if (so && so->m_state != XSOCKET_UNCONNECTED_STATE) {
        VXIODevice_close((XSocket*)so);
    }
}

//static void VXSocketBase_waitForConnected(XSocketBase* so, int msecs)
//{
//    if (!so || so->m_state != XSOCKET_CONNECTING_STATE) return;
//
//    XSocket* linuxSo = (XSocket*)so;
//    if (linuxSo->m_socket == -1) return;
//
//    struct pollfd pfd;
//    pfd.fd = linuxSo->m_socket;
//    pfd.events = POLLOUT;
//    pfd.revents = 0;
//
//    int ret = poll(&pfd, 1, msecs);
//    if (ret <= 0) {
//        // 超时或错误
//        return;
//    }
//
//    if (pfd.revents & POLLOUT) {
//        // 检查连接是否成功
//        int error = 0;
//        socklen_t len = sizeof(error);
//        if (getsockopt(linuxSo->m_socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
//            so->m_state = XSOCKET_CONNECTED_STATE;
//            XSocket_stateChanged_signal(linuxSo, so->m_state);
//            XSocket_connected_signal(linuxSo);
//        }
//        else {
//            so->m_state = XSOCKET_UNCONNECTED_STATE;
//            XSocket_stateChanged_signal(linuxSo, so->m_state);
//        }
//    }
//}
//
//static void VXSocketBase_waitForDisconnected(XSocketBase* so, int msecs)
//{
//    if (!so || so->m_state == XSOCKET_UNCONNECTED_STATE) return;
//
//    XSocket* linuxSo = (XSocket*)so;
//    if (linuxSo->m_socket == -1) return;
//
//    struct pollfd pfd;
//    pfd.fd = linuxSo->m_socket;
//    pfd.events = POLLIN | POLLRDHUP;
//    pfd.revents = 0;
//
//    int ret = poll(&pfd, 1, msecs);
//    if (ret <= 0) {
//        return;
//    }
//
//    if (pfd.revents & (POLLRDHUP | POLLHUP | POLLERR)) {
//        so->m_state = XSOCKET_UNCONNECTED_STATE;
//        XSocket_stateChanged_signal(linuxSo, so->m_state);
//        XSocket_disconnected_signal(linuxSo);
//    }
//}

static const char* VXSocketBase_localAddress(XSocket* so)
{
    XSocketBase* base = (XSocketBase*)so;
    if (!so || so->m_socket == -1) return NULL;

    static char localIP[46];
    struct sockaddr_storage localAddr;
    socklen_t addrLen = sizeof(localAddr);

    if (getsockname(so->m_socket, (struct sockaddr*)&localAddr, &addrLen) == -1) {
        return NULL;
    }

    if (localAddr.ss_family == AF_INET) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)&localAddr;
        inet_ntop(AF_INET, &ipv4->sin_addr, localIP, sizeof(localIP));
    }
    else if (localAddr.ss_family == AF_INET6) {
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)&localAddr;
        inet_ntop(AF_INET6, &ipv6->sin6_addr, localIP, sizeof(localIP));
    }
    else {
        return NULL;
    }

    return localIP;
}

static uint16_t VXSocketBase_localPort(XSocket* so)
{
    if (!so || so->m_socket == -1) return 0;

    struct sockaddr_storage localAddr;
    socklen_t addrLen = sizeof(localAddr);

    if (getsockname(so->m_socket, (struct sockaddr*)&localAddr, &addrLen) == -1) {
        return 0;
    }

    if (localAddr.ss_family == AF_INET) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)&localAddr;
        return ntohs(ipv4->sin_port);
    }
    else if (localAddr.ss_family == AF_INET6) {
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)&localAddr;
        return ntohs(ipv6->sin6_port);
    }

    return 0;
}

static bool VXIODevice_open(XSocket* so, XIODeviceBaseMode mode)
{
    if (!so) return false;
    if (XIODevice_isOpen(so))return true;
    // 创建socket
    if (!create_socket(so)) {
        return false;
    }
    XSocketBase* base = (XSocketBase*)so;
    // 解析主机名
    struct addrinfo hints, * res;
    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%hu", base->m_peerPort);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = base->m_ipv6Enabled ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = (base->m_socketType == XSOCKET_TYPE_TCP) ? SOCK_STREAM : SOCK_DGRAM;

    if (getaddrinfo(XString_toUtf8(base->m_peerName), portStr, &hints, &res) != 0) {
        close(so->m_socket);
        so->m_socket = -1;
        return false;
    }

    so->m_addrInfo = res;
    base->m_state = XSOCKET_CONNECTING_STATE;
    XSocket_stateChanged_signal(so, base->m_state);

    // 发起非阻塞连接
    if (connect(so->m_socket, res->ai_addr, res->ai_addrlen) == -1) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            freeaddrinfo(res);
            so->m_addrInfo = NULL;
            close(so->m_socket);
            so->m_socket = -1;
            base->m_state = XSOCKET_UNCONNECTED_STATE;
            XSocket_stateChanged_signal(so, base->m_state);
            return false;
        }
    }

    ((XIODevice*)so)->m_openMode = mode;
    return true;
}

static bool VXIODevice_isOpen(XSocket* so)
{
    //return so && so->m_socket != -1;
     return (((XIODevice*)so)->m_openMode != XIODevice_NotOpen)&&so->m_class.m_state== XSOCKET_CONNECTED_STATE;
}

static bool VXIODevice_close(XSocket* so)
{
     if ((((XIODevice*)so)->m_openMode == XIODevice_NotOpen))
     return true;
    XSocketBase* base = (XSocketBase*)so;
    // if (((XSocketBase*)so)->m_state == XSOCKET_CONNECTED_STATE)
    // {
    //     XIODevice_aboutToClose_signal(so);
    //     ((XSocketBase*)so)->m_state = XSOCKET_CLOSING_STATE;
    //     XSocket_stateChanged_signal(so, ((XSocketBase*)so)->m_state);
    // }
 
    if(so->m_socket!=-1)
    {
        XIODevice_aboutToClose_signal(so);
        XCoreApplication_removeFd(so->m_socket);
        close(so->m_socket);
        so->m_socket = -1;
    }
    
    //so->m_pollfd.fd = -1;

    if (so->m_addrInfo) {
        freeaddrinfo(so->m_addrInfo);
        so->m_addrInfo = NULL;
    }

    if (base->m_state != XSOCKET_UNCONNECTED_STATE) {
        base->m_state = XSOCKET_UNCONNECTED_STATE;
        XSocket_stateChanged_signal(so, base->m_state);
        XSocket_disconnected_signal(so);
    }

    ((XIODevice*)so)->m_openMode = XIODevice_NotOpen;
    return true;
}
static size_t VXIODevice_write(XSocket* so, const char* data, size_t maxSize)
{
    if (so == NULL || data == NULL || so->m_socket == -1) {
        return 0;
    }

    XIODevice* io = (XIODevice*)so;
    // 检查设备是否允许写入
    if (!(io->m_openMode & XIODevice_WriteOnly)) {
        return 0;
    }

    size_t totalSent = 0;

    // 无写入缓冲区，直接发送
    if (io->m_writeBuffer == NULL) {
        while (totalSent < maxSize) {
            ssize_t result = send(so->m_socket, data + totalSent,
                (maxSize - totalSent), 0);
            if (result == -1) {
                // 非阻塞模式下暂时无法发送，退出循环
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                else {
                    // 其他错误，返回已发送字节数
                    return totalSent;
                }
            }
            totalSent += result;
        }
        // 发送成功后触发字节写入信号
        if (totalSent > 0) {
            XIODevice_bytesWritten_signal(io, totalSent);
        }
        return totalSent;
    }

    // 有写入缓冲区，先写入缓冲区
    size_t bytesToWrite = maxSize;
    while (bytesToWrite > 0) {
        // 替换：获取空闲空间 = 总容量 - 已使用大小
        size_t capacity = XQueueBase_capacity_base(io->m_writeBuffer);
        size_t used = XQueueBase_size_base(io->m_writeBuffer);
        size_t freeSpace = capacity - used;

        if (freeSpace == 0) {
            // 缓冲区满，尝试发送部分数据
            //size_t sent = VXIODevice_writeFull(so);
            //if (sent == 0) {
            //    // 无法发送且缓冲区满，返回已写入缓冲区的字节数
            //    break;
            //}
            continue;
        }

        size_t writeSize = (bytesToWrite < freeSpace) ? bytesToWrite : freeSpace;
        // 替换：循环写入缓冲区（单个元素入队）
        for (size_t i = 0; i < writeSize; i++) {
            XQueueBase_push_base(io->m_writeBuffer, &(data[totalSent + i]));
        }
        totalSent += writeSize;
        bytesToWrite -= writeSize;
    }

    // 尝试发送缓冲区数据
    //VXIODevice_writeFull(so);
    return totalSent;
}

static size_t VXIODevice_writeFull(XSocket* so)
{
    if (so == NULL || so->m_socket == -1) {
        return 0;
    }

    XIODevice* io = (XIODevice*)so;
    if (!(io->m_openMode & XIODevice_WriteOnly) || io->m_writeBuffer == NULL) {
        return 0;
    }

    size_t totalSent = 0;
    // 替换：获取已使用空间
    size_t available = XQueueBase_size_base(io->m_writeBuffer);

    while (available > 0) {
        // 替换：获取队头元素指针
        const char* bufferData = XQueueBase_top_base(io->m_writeBuffer);
        if (bufferData == NULL) {
            break;
        }

        ssize_t result = send(so->m_socket, bufferData, available, 0);
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下暂时无法发送，退出循环
                break;
            }
            else {
                // 发生错误，清空缓冲区并返回已发送字节数
                XQueueBase_clear_base(io->m_writeBuffer); // 替换：清空队列
                return totalSent;
            }
        }

        // 替换：移动读指针（出队result个元素）
        for (size_t i = 0; i < (size_t)result; i++) {
            XQueueBase_pop_base(io->m_writeBuffer);
        }
        totalSent += result;
        available -= result;
    }

    // 触发字节写入信号
    if (totalSent > 0) {
        XIODevice_bytesWritten_signal(io, totalSent);
    }

    return totalSent;
}

static size_t VXIODevice_read(XSocket* so, char* data, size_t maxSize)
{
    if (!so || !data || so->m_socket == -1) return 0;

    ssize_t recvd = recv(so->m_socket, data, maxSize, 0);
    if (recvd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            VXIODevice_close(so);
        }
        return 0;
    }
    else if (recvd == 0) {
        // 连接关闭
        VXIODevice_close(so);
        return 0;
    }
    return (size_t)recvd;
}

static size_t VXIODevice_getBytesAvailable(XSocket* so)
{
     if (!so || so->m_socket == -1) {
        return 0;
    }

    int available = 0;
    // 使用 FIONREAD 命令获取可读取的字节数，结果存入 available
    if (ioctl(so->m_socket, FIONREAD, &available) == -1) {
        return 0;
    }

    // 确保返回值非负（FIONREAD 通常返回非负值，但做一次安全处理）
    return (available < 0) ? 0 : (size_t)available;
}

static size_t VXIODevice_getBytesToWrite(XSocket* so)
{
    if (!so || so->m_socket == -1) return 0;

    int pending = 0;
    // TIOCOUTQ：获取输出队列中未发送的字节数，结果存入 pending
    if (ioctl(so->m_socket, TIOCOUTQ, &pending) == -1) {
        return 0;
    }
    // 确保返回值非负（TIOCOUTQ 通常返回非负值，做安全处理）
    return (pending < 0) ? 0 : (size_t)pending;
}

static bool VXIODevice_atEnd(XSocket* so)
{
    return !VXIODevice_isOpen(so) && VXIODevice_getBytesAvailable(so) == 0;
}

static void VXIODevice_setWriteBuffer(XSocket* so, size_t count)
{
    if (so && so->m_socket != -1) {
        int bufSize = (int)count;
        setsockopt(so->m_socket, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize));
    }
}

static void VXIODevice_setReadBuffer(XSocket* so, size_t count)
{
    if (so && so->m_socket != -1) {
        int bufSize = (int)count;
        setsockopt(so->m_socket, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
    }
}

static void VXIODevice_poll(XSocket* so)
{
    //printf("XSocket轮询\n");
    
    // if (!so || so->m_socket == -1) return;

    // so->m_pollfd.events = 0;
    // if (((XIODevice*)so)->m_openMode & XIODevice_ReadOnly) {
    //     so->m_pollfd.events |= POLLIN;
    // }
    // if (((XIODevice*)so)->m_openMode & XIODevice_WriteOnly) {
    //     so->m_pollfd.events |= POLLOUT;
    // }

    // int ret = poll(&so->m_pollfd, 1, 0);
    // if (ret <= 0) return;

    // XSocketBase* base = (XSocketBase*)so;
    // if (so->m_pollfd.revents & POLLOUT) {
    //     if (base->m_state == XSOCKET_CONNECTING_STATE) {
    //         // 检查连接结果
    //         int error = 0;
    //         socklen_t len = sizeof(error);
    //         if (getsockopt(so->m_socket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
    //             base->m_state = XSOCKET_CONNECTED_STATE;
    //             XSocket_stateChanged_signal(so, base->m_state);
    //             XSocket_connected_signal(so);
    //         }
    //         else {
    //             base->m_state = XSOCKET_UNCONNECTED_STATE;
    //             XSocket_stateChanged_signal(so, base->m_state);
    //         }
    //     }
    // }
    // if (so->m_pollfd.revents & (POLLIN | POLLPRI)) {
    //     // 数据可读，触发读事件（可根据需要添加信号发射）
    //     XIODevice_readyRead_signal(so);
    // }

    // if (so->m_pollfd.revents & (POLLERR | POLLHUP | POLLRDHUP)) {
    //     // 连接错误或关闭
    //     VXIODevice_close(so);
    //     XSocket_disconnected_signal(so);
    // }
}

static void VXIODevice_deinit(XSocket* so)
{
    if (!so) return;
    VXIODevice_close(so);
    XMemory_free(so);
}

#endif