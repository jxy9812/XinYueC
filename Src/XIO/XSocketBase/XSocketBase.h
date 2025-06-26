#ifndef XSocketBase_H
#define XSocketBase_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODeviceBase.h"
#define XSOCKETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSocketBase))       //XSocketBase容器虚函数表大小
//XSwitchDevice虚函数表枚举
XCLASS_DEFINE_BEGING(XSocketBase)
XCLASS_DEFINE_ENUM(XSocketBase, SetState) = XCLASS_VTABLE_GET_SIZE(XIODeviceBase),
XCLASS_DEFINE_ENUM(XSocketBase, GetState),
XCLASS_DEFINE_END(XSocketBase)
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
//开关设备
typedef struct XSocketBase
{
	XIODeviceBase m_parent;//父对象
    XSocketState m_state;//
    XSocketType m_socketType;
    XString* m_peerName;//远程主机名
    XString* m_peerAddress;//远程主机地址
    uint16_t m_peerPort;//远程主机端口
}XSocketBase;
//初始化类
XVtable* XSocketBase_class_init();
//开关设备
XSocketBase* XSocketBase_create();
//初始化
void XSocketBase_init(XSocketBase* socket);
void XSocketBase_connectToHost_base(XSocketBase* socket,const char* hostName,uint16_t port, XIODeviceBaseMode mode);
void XSocketBase_disconnectFromHost_base(XSocketBase* socket);
void XSocketBase_waitForConnected_base(XSocketBase* socket,int msecs);
void XSocketBase_waitForDisconnected_base(XSocketBase* socket, int msecs);
const char* XSocketBase_localAddress_base(XSocketBase* socket);
uint16_t XSocketBase_localPort_base(XSocketBase* socket);

const char* XSocketBase_peerAddress(XSocketBase* socket);
const char* XSocketBase_peerName(XSocketBase* socket);
uint16_t XSocketBase_peerPort(XSocketBase* socket);
XSocketType XSocketBase_socketType(const XSocketBase* socket);
XSocketState XSocketBase_state(const XSocketBase* socket);
#define XSocketBase_isOpen					XIODeviceBase_isOpen
#define XSocketBase_open_base				XIODeviceBase_open_base
#define XSocketBase_close_base				XIODeviceBase_close_base
#define XSocketBase_setDevice_base			XIODeviceBase_setDevice_base
#define XSocketBase_delete_base				XIODeviceBase_delete_base
#define XSocketBase_poll_base				XIODeviceBase_poll_base

#ifdef __cplusplus
}
#endif
#endif