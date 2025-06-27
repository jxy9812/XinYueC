#ifdef WIN32
#include "XSocketWin32.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
static void VXSocketBase_connectToHost(XSocketWin32* socket, const char* hostName, uint16_t port, XIODeviceBaseMode mode);
static void VXSocketBase_disconnectFromHost(XSocketBase* socket);
static void VXSocketBase_waitForConnected(XSocketBase* socket, int msecs);
static void VXSocketBase_waitForDisconnected(XSocketBase* socket, int msecs);
static const char* VXSocketBase_localAddress(XSocketWin32* socket);
static uint16_t VXSocketBase_localPort(XSocketWin32* socket);
static void VXIODevice_poll(XSocketWin32* socket);
static bool VXIODevice_open(XSocketWin32* socket, XIODeviceBaseMode mode);
static bool VXIODevice_close(XSocketWin32* socket);

XVtable* XSocketWin32_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XSOCKETBASE_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
	void* table[] = { VXSocketBase_connectToHost,VXSocketBase_disconnectFromHost, 
                      VXSocketBase_waitForConnected,VXSocketBase_waitForDisconnected ,
                       VXSocketBase_localAddress,VXSocketBase_localPort };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Poll, VXIODevice_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
#if SHOWCONTAINERSIZE
	printf("XSocketWin32 size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XSocketWin32* XSocketWin32_create()
{
	XSocketWin32* socket = XMemory_malloc(sizeof(XSocketWin32));
	XSocketWin32_init(socket);
	return socket;
}

void XSocketWin32_init(XSocketWin32* socket)
{
	if (socket == NULL)
		return;
	memset(((XSocketBase*)socket) + 1, 0, sizeof(XSocketWin32) - sizeof(XSocketBase));
	XSocketBase_init(socket);
	XClassGetVtable(socket) = XSocketWin32_class_init();

}

void VXSocketBase_connectToHost(XSocketWin32* socket, const char* hostName, uint16_t port, XIODeviceBaseMode mode)
{
	if (XSocketBase_state(socket) != XSOCKET_UNCONNECTED_STATE||hostName==NULL)
		return;
	XSocketBase* base = socket;
	XString_clear_base(base->m_peerName);
	XString_append_base(base->m_peerName,hostName);
	base->m_peerPort = port;
	XIODeviceBase_open_base(socket,mode);
}

void VXSocketBase_disconnectFromHost(XSocketBase* socket)
{
}

void VXSocketBase_waitForConnected(XSocketBase* socket, int msecs)
{
    if (!socket || socket->m_state != XSOCKET_CONNECTING_STATE) return;

    // 对于阻塞套接字，connect已经完成或失败
    // 这里可以实现非阻塞套接字的等待逻辑
}

void VXSocketBase_waitForDisconnected(XSocketBase* socket, int msecs)
{
    if (!socket || socket->m_state == XSOCKET_UNCONNECTED_STATE) return;

    // 实现等待断开连接的逻辑
    // 可以使用select或超时机制
}

const char* VXSocketBase_localAddress(XSocketWin32* so)
{
    XSocketBase* base = so;
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

uint16_t VXSocketBase_localPort(XSocketWin32* so)
{
    XSocketBase* base = so;
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

void VXIODevice_poll(XSocketWin32* socket)
{
}

bool VXIODevice_open(XSocketWin32* so, XIODeviceBaseMode mode)
{
    if (XIODeviceBase_isOpen(so))
        return true;
    if (XSocketBase_state(so) != XSOCKET_UNCONNECTED_STATE)
		return false;
	XSocketBase* base = so;
    // 更新状态
    base->m_state = XSOCKET_HOST_LOOKUP_STATE;
	int result;
    WSADATA wsaData;
	// 初始化 Winsock
	result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		printf("WSAStartup failed: %d\n", result);
		return false;
	}
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
        base->m_state = XSOCKET_UNCONNECTED_STATE;
        return false;
    }
   
    // 将 uint16_t 端口转换为字符串
    char portStr[6]; // 最大 "65535" + '\0'
    sprintf(portStr, "%hu", base->m_peerPort);

    // 解析服务器地址和端口
    printf("正在解析服务器地址: %s:%s\n", XString_c_str(base->m_peerName), portStr);
    result = getaddrinfo(XString_c_str(base->m_peerName), portStr, &hints, &(so->m_addrInfo));
    if (result != 0) {
        printf("getaddrinfo failed: %d\n", result);
        base->m_state = XSOCKET_UNCONNECTED_STATE;
        WSACleanup();
        return false;
    }
    // 尝试连接
    base->m_state = XSOCKET_CONNECTING_STATE;
    struct addrinfo* addr = so->m_addrInfo;

    while (addr) {
        // 创建套接字
        so->m_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (so->m_socket == INVALID_SOCKET) {
            addr = addr->ai_next;
            continue;
        }

        // 对于TCP套接字，尝试连接
        if (base->m_socketType == XSOCKET_TYPE_TCP) {
            result = connect(so->m_socket, addr->ai_addr, (int)addr->ai_addrlen);
            if (result == SOCKET_ERROR) {
                closesocket(so->m_socket);
                so->m_socket = INVALID_SOCKET;
                addr = addr->ai_next;
                continue;
            }
        }
        // 对于UDP，不需要显式连接，但需要保存地址

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

        base->m_peerAddress = XString_create(ipStr);
        base->m_state = XSOCKET_CONNECTED_STATE;
        break;
    }

    // 如果没有找到可用地址
    if (!addr && base->m_state != XSOCKET_CONNECTED_STATE) {
        if (so->m_socket != INVALID_SOCKET) {
            closesocket(so->m_socket);
            so->m_socket = INVALID_SOCKET;
        }
        if (so->m_addrInfo) {
            freeaddrinfo(so->m_addrInfo);
            so->m_addrInfo = NULL;
        }
        base->m_state = XSOCKET_UNCONNECTED_STATE;
    }
    ((XIODeviceBase*)so)->m_mode = XIODeviceBase_ReadWrite;
    return true;
}

bool VXIODevice_close(XSocketWin32* so)
{
    if (!XIODeviceBase_isOpen(so)|| XSocketBase_state(so) == XSOCKET_UNCONNECTED_STATE)
        return false;
    XSocketBase* base = so;

    if (so->m_socket != INVALID_SOCKET) {
        base->m_state = XSOCKET_CLOSING_STATE;

        // 优雅地关闭连接
        shutdown(so->m_socket, SD_BOTH);
        closesocket(so->m_socket);
        so->m_socket = INVALID_SOCKET;
    }

    if (so->m_addrInfo) {
        freeaddrinfo(so->m_addrInfo);
        so->m_addrInfo = NULL;
    }

    base->m_state = XSOCKET_UNCONNECTED_STATE;

    ((XIODeviceBase*)so)->m_mode = XIODeviceBase_NotOpen;
    return true;
}













#endif

