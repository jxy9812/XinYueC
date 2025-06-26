#ifdef WIN32
#ifndef XSOCKETWIN32_H
#define XSOCKETWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSocketBase.h"
#include <winsock2.h>
typedef struct XSocketWin32
{
    XSocketBase m_parent;//父对象
    WSADATA m_wsaData;
    SOCKET  m_socket;
    struct addrinfo* m_addrInfo;  // 地址信息
}XSocketWin32;
//初始化类
XVtable* XSocketWin32_class_init();
//开关设备
XSocketWin32* XSocketWin32_create();
//套接字
void XSocketWin32_init(XSocketWin32* socket);
#define XSocketWin32_connectToHost_base                         XSocketBase_connectToHost_base;
#define XSocketWin32_disconnectFromHost_base                    XSocketBase_disconnectFromHost_base
#define XSocketWin32_waitForConnected_base                      XSocketBase_waitForConnected_base
#define XSocketWin32_waitForDisconnected_base                   XSocketBase_waitForDisconnected_base
#define XSocketWin32_localAddress_base                          XSocketBase_localAddress_base
#define XSocketWin32_localPort_base                             XSocketBase_localPort_base

#define XSocketWin32_peerAddress                                XSocketBase_peerAddress
#define XSocketWin32_peerName                                   XSocketBase_peerName
#define XSocketWin32_peerPort                                   XSocketBase_peerPort
#define XSocketWin32_setSocketType                              XSocketBase_setSocketType
#define XSocketWin32_socketType                                 XSocketBase_socketType
#define XSocketWin32_state                                      XSocketBase_state
#define XSocketWin32_isOpen					                    XIODeviceBase_isOpen
#define XSocketWin32_open_base				                    XIODeviceBase_open_base
#define XSocketWin32_close_base				                    XIODeviceBase_close_base
#define XSocketWin32_setDevice_base			                    XIODeviceBase_setDevice_base
#define XSocketWin32_delete_base				                XIODeviceBase_delete_base
#define XSocketWin32_poll_base				                    XIODeviceBase_poll_base

#ifdef __cplusplus
}
#endif
#endif
#endif