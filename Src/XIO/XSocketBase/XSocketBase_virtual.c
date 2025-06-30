#include"XSocketBase.h"
#include"XString.h"
#include"XEvent.h"
#include"XEventDispatcher.h"
static void VXIODevice_poll(XSocketBase* socket);
static bool VXIODevice_open(XSocketBase* socket, XIODeviceBaseMode mode);
static bool VXIODevice_close(XSocketBase* socket);
static void VXIODevice_delete(XSocketBase* socket);
XVtable* XSocketBase_class_init()
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
	//void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXIODevice_delete);
#if SHOWCONTAINERSIZE
	printf("XSocketBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXIODevice_poll(XSocketBase* socket)
{
	// 处理事件调度器中的事件
	XEventDispatcher_handler(socket->m_eventDispatcher);
}

bool VXIODevice_open(XSocketBase* socket, XIODeviceBaseMode mode)
{
	return false;
}

bool VXIODevice_close(XSocketBase* socket)
{
	return false;
}

void VXIODevice_delete(XSocketBase* socket)
{
	if (socket->m_peerAddress)
		XString_delete_base(socket->m_peerAddress);
	if(socket->m_peerName)
		XString_delete_base(socket->m_peerName);
	if (socket->m_eventDispatcher)
		XEventDispatcher_delete_base(socket->m_eventDispatcher);
	// 释放父对象
	XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Delete, void(*)(XIODeviceBase*))(socket);
}
