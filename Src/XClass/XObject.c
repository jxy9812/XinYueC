#include "XObject.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XThread.h"
#include "XSetBase.h"
#include "XCoreApplication.h"
#include "XSignalSlot.h"
#include "XEventDispatcher.h"
#include "XMapBase.h"
#include "XTimer.h"
#include <stdarg.h>
static void VXObject_poll(XObject* object);
static void VXObject_deinit(XObject* object);
XVtable* XObject_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = { VXObject_poll };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXObject_deinit);
#if SHOWCONTAINERSIZE
	printf("XObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XObject* XObject_create()
{
	XObject* object = XMemory_malloc(sizeof(XObject));
	XObject_init(object);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
	object->m_eventLoop = NULL;
	XThread* thread = XThread_currentThread();
	XEventLoop* d= XThread_currentEventLoop();
	if (d == NULL)
		return;
	object->m_eventLoop = d;
	//信号与槽初始化
	object->m_signalSlot = NULL;
	XObject_addEventFilter(object, XEVENT_SLOT_RUN, XEventSlotFuncRunCB, NULL);

	//定时器
	object->m_poolTimer = NULL;
}

void XObject_poll_base(XObject* object)
{
	if (ISNULL(object, "") || ISNULL(XClassGetVtable(object), ""))
		return;
	XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))(object);
}

void XObject_setPollingInterval(XObject* object, size_t interval)
{
	if (object == NULL)
		return;
	if (interval == 0&& object->m_poolTimer)
	{//关闭轮询
		XTimerBase_delete_base(object->m_poolTimer);
		object->m_poolTimer = NULL;
		return;
	}
	if (object->m_poolTimer == NULL)
	{
		object->m_poolTimer = XTimer_create();
		XTimerBase_setAutoDelete(object->m_poolTimer,false);
		XTimerBase_setTimerCallback_base(object->m_poolTimer,XObject_poll_base);
		XTimerBase_setUserData_base(object->m_poolTimer,object);
		XTimerBase_setSingleShote(object->m_poolTimer, false);
		//XTimerBase_setTimerGroup(object->m_poolTimer, XThread_currentTimerGroup());
	}
	XTimerBase_setTimeout_base(object->m_poolTimer, interval);
	XTimerBase_setInterval_base(object->m_poolTimer, interval);
	XTimerBase_start_base(object->m_poolTimer);
}

bool XObject_addEventFilter(XObject* object, int code, XEventCB cb, void* userData)
{
	if (object == NULL)
		return false;
	XEventDispatcher* d = XObject_getEventDispatcher(object);
	return XEventDispatcher_addEventCb_base(d, object, code, cb, userData);
}

bool XObject_removeEventFilter(XObject* object, int code)
{
	if (object == NULL)
		return false;
	XEventDispatcher* d = XObject_getEventDispatcher(object);
	return XEventDispatcher_removeEventCb_base(d, object, code);
}

bool XObject_moveToThread(XObject* object, XThread* thread)
{
	//处理剩余的所有事件,防止遗漏
	if (object->m_eventLoop&& object->m_eventLoop->m_dispatcher)
	{
		//XEventDispatcher_handler_base(object->m_eventLoop->m_dispatcher);
		if (XEventDispatcher_object_move(object->m_eventLoop->m_dispatcher, XThread_getDispatcher(thread), object))
		{
			object->m_eventLoop = thread->m_eventLoop;//更新事件循环
		}
	}
	return false;
}

bool XObject_postEvent(XObject* object, XEventMin* event, XEventPriority priority)
{
	if (object == NULL || object->m_eventLoop==NULL || event == NULL)
		return false;
	event->receiver = object;
	return XEventDispatcher_postEvent_base(object->m_eventLoop->m_dispatcher,event, priority);
}

XThread* XObject_thread(XObject* object)
{
	if(object==NULL)
		return NULL;
	return XThread_currentThread();
}

XEventDispatcher* XObject_getEventDispatcher(XObject* object)
{
	if(object&& object->m_eventLoop)
		return object->m_eventLoop->m_dispatcher;
	return NULL;
}

XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(object==NULL || slot_func == NULL)
		return NULL;
	if(object->m_signalSlot==NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect(object->m_signalSlot,signal,receiver,slot_func,type);
}

bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func)
{
	if (object == NULL|| slot_func==NULL)
		return false;
	return XSignalSlot_disconnect(object->m_signalSlot, signal, receiver, slot_func);
}

bool XObject_disconnect_conn(XConnection* conn)
{
	if ( conn == NULL)
		return false;
	return XSignalSlot_disconnect_conn(conn);
}

bool XObject_setSignalSendMode(XObject* object, XSignalSendMode mode)
{
	if(!object)
		return false;
	if (object->m_signalSlot == NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_setSendMode(object->m_signalSlot,mode);
}

XSignalSendMode XObject_getSignalSendMode(XObject* object)
{
	if (!object)
		return XSIGNAL_SEND_INVALID;
	return XSignalSlot_getSendMode(object->m_signalSlot);
}

void* XObject_deinit_signal(XObject* object)
{
	if(object)
	{
		XSignalSlot_emit(object->m_signalSlot, XObject_deinit_signal, NULL);
	}
	return XObject_deinit_signal;
	
}

void XObject_deinit_event(XObject* object)
{
	if (object == NULL)
		return;
	XObject_postEvent(object, XEventFunc_create_oneAccept(object, XObject_deinit_base, object), XEVENT_PRIORITY_LOWEST);
}

void XObject_delete_event(XObject* object)
{
	if (object == NULL)
		return;
	XObject_postEvent(object, XEventFunc_create_oneAccept(object, XObject_delete_base, object), XEVENT_PRIORITY_LOWEST);
}

void VXObject_poll(XObject* object)
{
}

void VXObject_deinit(XObject* object)
{
	//发送释放信号
	XObject_deinit_signal(object);
	//处理剩余的所有事件,防止遗漏
	if(object->m_eventLoop&& object->m_eventLoop->m_dispatcher)
		XEventDispatcher_handler_base(object->m_eventLoop->m_dispatcher);
	//释放信号与槽
	if(object->m_signalSlot)
	{
		XSignalSlot_delete(object->m_signalSlot);
		object->m_signalSlot = NULL;
	}
	if (object->m_poolTimer)
	{
		XTimerBase_delete_base(object->m_poolTimer);
		object->m_poolTimer = NULL;
	}
}
