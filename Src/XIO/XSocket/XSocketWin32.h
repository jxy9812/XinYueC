#ifdef WIN32
#ifndef XSOCKETWIN32_H
#define XSOCKETWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSocket.h"
typedef struct XSocket
{
    XSocketBase m_parent;//父对象
    size_t  m_socket;
    void* m_addrInfo;  // 地址信息             //struct addrinfo* m_addrInfo;  // 地址信息
    void* m_pollEvent; // 新增：轮询事件对象   //WSAEVENT m_pollEvent;        // 新增：轮询事件对象
    void* m_netEvents; // 网络事件结构         //WSANETWORKEVENTS* m_netEvents; // 网络事件结构
}XSocket;
//初始化类
XVtable* XSocket_class_init();
//套接字
XSocket* XSocket_create();
//套接字
void XSocket_init(XSocket* socket);
#ifdef __cplusplus
}
#endif
#endif
#endif