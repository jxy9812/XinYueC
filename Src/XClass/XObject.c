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
#include <string.h>
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
	memset(((XClass*)object)+1,0,sizeof(XObject)-sizeof(XClass));
	XClass_init(object);
	XClassGetVtable(object) = XObject_class_init();
	object->m_thread = XThread_currentThread();
	//信号与槽初始化
	//object->m_signalSlot = NULL;
	XObject_addEventFilter(object, XEVENT_SLOT_RUN, XEventSlotFuncRunCB, NULL);
	XObject_addEventFilter(object, XEVENT_FUNC_RUN, XEventFuncRunCB, NULL);
	//定时器
	//object->m_poolTimer = NULL;
	//object->m_isEventBubblingEnabled = false;
}

void XObject_poll_base(XObject* object)
{
	if (ISNULL(object, "") || ISNULL(XClassGetVtable(object), ""))
		return;
	XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))(object);
}

void XObject_setPollingInterval(XObject* object, size_t interval)
{
	if (object == NULL|| XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))==NULL)
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
		//XObject_setParent(object->m_poolTimer, XThread_currentTimerGroup());
	}
	XTimerBase_setTimeout_base(object->m_poolTimer, interval);
	XTimerBase_setInterval_base(object->m_poolTimer, interval);
	XTimerBase_start_base(object->m_poolTimer);
}

void XObject_setParent(XObject* object, XObject* parent)
{
	if (object)
		object->m_parent = parent;
}

XObject* XObject_getParent(XObject* object)
{
	if (object)
		return object->m_parent;
	return NULL;
}

void XObject_setEventBubblingEnabled(XObject* object, bool enable)
{
	if (object)
		object->m_isEventBubblingEnabled = enable;
}

bool XObject_isEventBubbling(XObject* object)
{
	if (object)
		return object->m_isEventBubblingEnabled;
	return false;
}

bool XObject_addEventFilter(XObject* object, int code, XEventCB cb, void* userData)
{
	if (object == NULL)
		return false;
	XEventDispatcher* d = XObject_getEventDispatcher(object);
	if(d)
		return XEventDispatcher_addEventCb_base(d, object, code, cb, userData);
	return false;
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
	if (!object||object->m_thread==thread)
		return false;
	XEventDispatcher* dispatcher = XObject_getEventDispatcher(object);
	//处理剩余的所有事件,防止遗漏
	if (dispatcher)
	{
		//XEventDispatcher_handler_base(object->m_eventLoop->m_dispatcher);
		if (XEventDispatcher_object_move(dispatcher, XThread_getDispatcher(thread), object))
		{
			object->m_thread = thread;
			return true;
		}
	}
	return false;
}

bool XObject_postEvent(XObject* object, XEvent* event, XEventPriority priority)
{
	if (object == NULL || event == NULL)
		return false;
	event->receiver = object;
	return XEventDispatcher_postEvent_base(XObject_getEventDispatcher(object), event, priority);
}

bool XObject_postFunc(XObject* object, void(*func)(void*), void* args, XEventSendMode mode, XEventPriority priority)
{
	if (object == NULL|| func == NULL|| mode== XEVENT_SEND_INVALID)
		return false;
	if (mode == XEVENT_SEND_DIRECT)
	{
		//投递函数
		return XObject_postEvent(object, XEventFunc_create(func,args), XEVENT_PRIORITY_NORMAL);
	}
	else if (mode == XEVENT_SEND_QUEUED)
	{//投递到队列，等待主线程事件循环中调用
		return XCoreApplication_postFunc(object,func,args, priority);
	}
}

XThread* XObject_thread(XObject* object)
{
	if(object==NULL)
		return NULL;
	return object->m_thread;
}

XEventDispatcher* XObject_getEventDispatcher(XObject* object)
{
	if (!object)
		return NULL;
	XEventLoop* loop=XObject_getEventLoop(object);
	if (loop)
		return loop->m_dispatcher;
	return NULL;
}

XEventLoop* XObject_getEventLoop(XObject* object)
{
	if(object==NULL)
		return NULL;
	if (object->m_thread)
		return object->m_thread->m_eventLoop;
	return XCoreApplication_getEventLoop();
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

bool XObject_setSignalSendMode(XObject* object, XEventSendMode mode)
{
	if(!object)
		return false;
	if (object->m_signalSlot == NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_setSendMode(object->m_signalSlot,mode);
}

XEventSendMode XObject_getSignalSendMode(XObject* object)
{
	if (!object)
		return XEVENT_SEND_INVALID;
	return XSignalSlot_getSendMode(object->m_signalSlot);
}

void XObject_emitSignal(XObject* object, size_t signal, void* args, XEventPriority priority)
{
	if(object)
		XSignalSlot_emit(object->m_signalSlot, signal, args, priority);
}

void* XObject_deinit_signal(XObject* object)
{
	if(object)
	{
		XObject_emitSignal(object, XObject_deinit_signal, NULL,XEVENT_PRIORITY_LOWEST);
	}
	return XObject_deinit_signal;
	
}

void XObject_deinit_event(XObject* object)
{
	if (object == NULL)
		return;
	XObject_postEvent(object, XEventFunc_create_oneAccept(XObject_deinit_base, object), XEVENT_PRIORITY_LOWEST);
}

void XObject_delete_event(XObject* object)
{
	if (object == NULL)
		return;
	XObject_postEvent(object, XEventFunc_create_oneAccept(XObject_delete_base, object), XEVENT_PRIORITY_LOWEST);
}

void VXObject_poll(XObject* object)
{
}

void VXObject_deinit(XObject* object)
{
	//发送释放信号
	XObject_deinit_signal(object);
	//处理剩余的所有事件,防止遗漏
	XEventDispatcher* dispatcher=XObject_getEventDispatcher(object);
	if(dispatcher)
		XEventDispatcher_handler_base(dispatcher);
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
