#ifndef XCOMMUNICATORBASE_H
#define XCOMMUNICATORBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODeviceBase.h"
#define XCOMMUNICATORBASE_VTABLE_SIZE		(XCLASS_VTABLE_SIZE+12)       //XCommunicatorBase虚函数表大小
enum XCommunicatorBaseVtableEnum
{
	EXCommunicatorBase_Open = XCLASS_VTABLE_SIZE,
	EXCommunicatorBase_Close,
	EXCommunicatorBase_Send,
	EXCommunicatorBase_Recv,
	EXCommunicatorBase_IsConnected,
	EXCommunicatorBase_Poll,
	EXCommunicatorBase_SetOption,
	EXCommunicatorBase_GetOption,
};
//通信基础类
typedef struct XCommunicatorBase
{
	XClass m_parent;//继承类
	XIODeviceBase* m_io;//io设备
}XCommunicatorBase;
XVtable* XCommunicatorBase_class_init();
void XCommunicatorBase_init(XCommunicatorBase* comm);
void XCommunicatorBase_open_base(XCommunicatorBase* comm);
void XCommunicatorBase_close_base(XCommunicatorBase* comm);
size_t XCommunicatorBase_send_base(XCommunicatorBase* comm,const void* data,size_t size);
size_t XCommunicatorBase_recv_base(XCommunicatorBase* comm, void* data, size_t maxSize);
bool  XCommunicatorBase_isConnected_base(XCommunicatorBase* comm);
void XCommunicatorBase_poll_base(XCommunicatorBase* comm);
void XCommunicatorBase_setOption_base(XCommunicatorBase* comm, int optionId, const void* value, size_t size);
void XCommunicatorBase_getOption_base(XCommunicatorBase* comm, int optionId, void* value, size_t* size);
#define XCommunicatorBase_free_base XClass_free_base
#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
