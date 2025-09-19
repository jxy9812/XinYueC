#ifdef __linux__ || defined(__APPLE__) || defined(__BSD__)
#ifndef XSOCKETPOSIX_H
#define XSOCKETPOSIX_H
#ifdef __cplusplus
extern "C" {
#endif
// 在包含<poll.h>前添加宏定义
#define _GNU_SOURCE  // 启用GNU扩展（POLLRDHUP最初是GNU扩展）
// 或定义POSIX标准版本（需系统支持POSIX.1-2008及以上）
// #define _POSIX_C_SOURCE 200809L
#include "XSocket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

typedef struct XSocket
{
    XSocketBase m_class;       // 父对象
    int         m_socket;       // Linux socket描述符
    struct addrinfo* m_addrInfo;// 地址信息
    struct pollfd m_pollfd;     // 轮询事件结构
    short       m_netEvents;    // 网络事件掩码
} XSocket;

// 初始化类
XVtable* XSocket_class_init();
// 创建套接字
XSocket* XSocket_create();
// 初始化套接字
void XSocket_init(XSocket* socket);

#ifdef __cplusplus
}
#endif
#endif
#endif