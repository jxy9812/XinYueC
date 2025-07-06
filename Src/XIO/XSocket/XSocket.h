#ifndef XSOCKET_H
#define XSOCKET_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODeviceBase.h"
#include<stdio.h>
#include<stdint.h>
#define XSOCKETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSocket))       //XSocketBase虚函数表大小
//XSwitchDevice虚函数表枚举
XCLASS_DEFINE_BEGING(XSocket)
XCLASS_DEFINE_ENUM(XSocket, ConnectToHost) = XCLASS_VTABLE_GET_SIZE(XIODeviceBase),
XCLASS_DEFINE_ENUM(XSocket, DisconnectFromHost),
XCLASS_DEFINE_ENUM(XSocket, WaitForConnected),
XCLASS_DEFINE_ENUM(XSocket, WaitForDisconnected),
XCLASS_DEFINE_ENUM(XSocket, LocalAddress),
XCLASS_DEFINE_ENUM(XSocket, LocalPort),
XCLASS_DEFINE_END(XSocket)
typedef enum {
    XSOCKET_UNCONNECTED_STATE = 0,  // 套接字未连接
    XSOCKET_HOST_LOOKUP_STATE = 1,  // 套接字正在执行主机名查找
    XSOCKET_CONNECTING_STATE = 2,   // 套接字已开始建立连接
    XSOCKET_CONNECTED_STATE = 3,    // 连接已建立
    XSOCKET_BOUND_STATE = 4,        // 套接字已绑定到地址和端口
    XSOCKET_LISTENING_STATE = 5,    // 仅用于内部使用
    XSOCKET_CLOSING_STATE = 6       // 套接字即将关闭(数据可能仍在等待写入)
}XSocketState;
typedef enum {
    XSOCKET_TYPE_TCP = 0,          // TCP协议
    XSOCKET_TYPE_UDP = 1,          // UDP协议
    XSOCKET_TYPE_SCTP = 2,         // SCTP协议
    XSOCKET_TYPE_UNKNOWN = -1      // 非TCP、UDP和SCTP的其他协议
} XSocketType;
//套接字
typedef struct XSocketBase
{
	XIODeviceBase m_parent;//父对象
    XSocketState m_state;//
    XSocketType m_socketType;
    XString* m_peerName;//远程主机名
    XString* m_peerAddress;//远程主机地址
    uint16_t m_peerPort;//远程主机端口
    bool m_ipv6Enabled;           // IPv6启用标志
}XSocketBase;
//初始化
void XSocketBase_init(XSocketBase* socket);

void XSocket_connectToHost_base(XSocketBase* socket,const char* hostName,uint16_t port, XIODeviceBaseMode mode);
void XSocket_disconnectFromHost_base(XSocketBase* socket);
void XSocket_waitForConnected_base(XSocketBase* socket,int msecs);
void XSocket_waitForDisconnected_base(XSocketBase* socket, int msecs);
const char* XSocket_localAddress_base(XSocketBase* socket);
uint16_t XSocket_localPort_base(XSocketBase* socket);

const char* XSocket_peerAddress(XSocketBase* socket);
const char* XSocket_peerName(XSocketBase* socket);
uint16_t XSocket_peerPort(XSocketBase* socket);
void XSocket_setSocketType(XSocketBase* socket, XSocketType type);
XSocketType XSocket_socketType(const XSocketBase* socket);
XSocketState XSocket_state(const XSocketBase* socket);
#define XSocket_isOpen					XIODeviceBase_isOpen
#define XSocket_open_base				XIODeviceBase_open_base
#define XSocket_close_base				XIODeviceBase_close_base
#define XSocket_setDevice_base			XIODeviceBase_setDevice_base
#define XSocket_delete_base				XIODeviceBase_delete_base
#define XSocket_poll_base				XIODeviceBase_poll_base
#define XSocket_setWriteBuffer_base     XIODeviceBase_setWriteBuffer_base
#define XSocket_setReadBuffer_base      XIODeviceBase_setReadBuffer_base
#define XSocket_write_base              XIODeviceBase_write_base
#define XSocket_read_base               XIODeviceBase_read_base
#define XSocket_getBytesAvailable_base  XIODeviceBase_getBytesAvailable_base
#define XSocket_getBytesToWrite_base    XIODeviceBase_getBytesToWrite_base
#define XSocket_atEnd_base              XIODeviceBase_atEnd_base
#define XSocket_writeFull_base          XIODeviceBase_writeFull_base


//以下是平台的具体实现
#ifdef WIN32
#include"XSocketWin32.h"
#elif defined(USE_STDPERIPH_DRIVER) 
#endif
#ifdef __cplusplus
}
#endif
#endif