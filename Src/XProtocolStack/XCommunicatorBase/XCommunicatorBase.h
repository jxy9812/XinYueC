#ifndef XCOMMUNICATORBASE_H
#define XCOMMUNICATORBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODeviceBase.h"
typedef struct XVector XVector;
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef void (*RecvDataCallback)(const void* data, size_t size, void* userData);
#define XCOMMUNICATORBASE_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XCommunicatorBase))       //XCommunicatorBase虚函数表大小
XCLASS_DEFINE_BEGING(XCommunicatorBase)
XCLASS_DEFINE_ENUM(XCommunicatorBase, Connect) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XCommunicatorBase, Disconnect),
XCLASS_DEFINE_ENUM(XCommunicatorBase, Send),
XCLASS_DEFINE_ENUM(XCommunicatorBase, Recv),
XCLASS_DEFINE_ENUM(XCommunicatorBase, SendAsync),
XCLASS_DEFINE_ENUM(XCommunicatorBase, RecvAsync),
XCLASS_DEFINE_ENUM(XCommunicatorBase, IsConnected),
XCLASS_DEFINE_ENUM(XCommunicatorBase, Poll),
XCLASS_DEFINE_ENUM(XCommunicatorBase, SetOption),
XCLASS_DEFINE_ENUM(XCommunicatorBase, GetOption),
XCLASS_DEFINE_END(XCommunicatorBase)

//通信基础类
typedef struct XCommunicatorBase
{
	XClass m_parent;//继承类
	uint16_t m_opt_timeout;//操作超时时间（毫秒），影响 Send/Receive 的阻塞时长。
	size_t   m_currentTimeout;//调用阻塞函数的时候记录开始时间 
	XIODeviceBase* m_io;//io设备
	void* m_userData;//用户数据
	RecvDataCallback m_recvDataCallback;//接收数据回调函数
	XVector* m_recvAsyncBuffer;//异步接收数据的缓冲区
	XTimerGroupWheel* m_wheel;//时间轮定时器组  默认3级(每级100轮) 1ms~999 秒
}XCommunicatorBase;
XVtable* XCommunicatorBase_class_init();
void XCommunicatorBase_init(XCommunicatorBase* comm);
bool XCommunicatorBase_connect_base(XCommunicatorBase* comm);
bool  XCommunicatorBase_disconnect_base(XCommunicatorBase* comm);
bool  XCommunicatorBase_isConnected_base(XCommunicatorBase* comm);
size_t XCommunicatorBase_send_base(XCommunicatorBase* comm,const void* data,size_t size);
size_t XCommunicatorBase_recv_base(XCommunicatorBase* comm, void* data, size_t maxSize);
bool XCommunicatorBase_sendAsync_base(XCommunicatorBase* comm, const void* data, size_t size); // 异步发送
bool XCommunicatorBase_recvAsync_base(XCommunicatorBase* comm, size_t maxSize); // 异步接收
void XCommunicatorBase_poll_base(XCommunicatorBase* comm);
void XCommunicatorBase_setOption_base(XCommunicatorBase* comm, int optionId, const void* value, size_t size);
void XCommunicatorBase_getOption_base(XCommunicatorBase* comm, int optionId, void* value, size_t* size);
#define XCommunicatorBase_delete_base XClass_delete_base

enum XCommunicatorBaseOption
{
	/*通用*/
	OPT_TIMEOUT,//uint16_t 操作超时时间（毫秒），影响 Send/Receive 的阻塞时长。
	OPT_SEND_BUFFER_SIZE,//size_t 发送 /缓冲区大小（字节），影响数据吞吐量。
	OPT_RECV_BUFFER_SIZE,//size_t 接收 / 缓冲区大小（字节），影响数据吞吐量。
	/*串口通信*/
	OPT_BAUD_RATE,//波特率（如 9600、115200）。
	OPT_DATA_BITS,//数据位（5-8）。	
	OPT_PARITY, //校验位（无、奇、偶）。
	OPT_STOP_BITS, //停止位（1、1.5、2）。
	OPT_FLOW_CONTROL,// 流控制（无、硬件、软件）。
};
#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
