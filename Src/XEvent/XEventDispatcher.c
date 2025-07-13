#include"XEvent.h"
#include"XEventDispatcher.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHash.h"
#include"XEquality.h"
#include"XListSLinked.h"
#include"XObject.h"
static void VXEventDispatcher_delete(XEventDispatcher* dispatcher);
static bool VXEventDispatcher_sendEvent(XEventDispatcher* dispatcher, XEventMin* event);
static bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEventMin* event);
static bool VXEventDispatcher_addEventCb(XEventDispatcher* dispatcher, int code, XEventCB cb,void* userData);
static bool VXEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code);
static void VXEventDispatcher_handler(XEventDispatcher* dispatcher);
typedef struct XEventCallback
{
	XEventCB callback;             // 可选的回调函数
	void* userData;              // 可选的用户数据指针
}XEventCallback;
XVtable* XEventDispatcher_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventDispatcher))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = { VXEventDispatcher_sendEvent,VXEventDispatcher_postEvent,
	VXEventDispatcher_addEventCb,VXEventDispatcher_removeEventCb,VXEventDispatcher_handler
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXEventDispatcher_delete);
#if SHOWCONTAINERSIZE
	printf("XEventDispatcher size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEventDispatcher* XEventDispatcher_create(XQueueBase* queue, XMapBase* map_cb)
{
	XEventDispatcher* dispatcher = XMemory_malloc(sizeof(XEventDispatcher));
	XEventDispatcher_init(dispatcher);
	dispatcher->m_queue = queue;
	dispatcher->m_filter_cb = map_cb;
	return dispatcher;
}

XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount)
{
	return XEventDispatcher_create(XCircularQueueAtomic_Create(XEventMin*, queueCount), XHash_Create(int, XEventCallback, XHash_murmur3_32, XEquality_int));
}

void XEventDispatcher_init(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return dispatcher;
	memset(((XClass*)dispatcher)+1,0,sizeof(XEventDispatcher)-sizeof(XClass));
	XClass_init(dispatcher);
	XClassGetVtable(dispatcher) = XEventDispatcher_class_init();
}

bool XEventDispatcher_sendEvent_base(XEventDispatcher* dispatcher, XEventMin* event)
{
	if (ISNULL(dispatcher, "") || ISNULL(event, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_SendEvent , bool (*)(XEventDispatcher*, XEventMin*))(dispatcher, event);
}

bool XEventDispatcher_postEvent_base(XEventDispatcher* dispatcher, XEventMin* event)
{
	if (ISNULL(dispatcher, "") || ISNULL(event, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_PostEvent, bool (*)(XEventDispatcher*, XEventMin*))(dispatcher, event);
}

bool XEventDispatcher_addEventCb_base(XEventDispatcher* dispatcher, int code,XEventCB cb, void* userData)
{
	if (ISNULL(dispatcher, "") ||  ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_AddEventCb, bool (*)(XEventDispatcher*,int, XEventCB, void*))(dispatcher, code, cb,userData);
}

bool XEventDispatcher_removeEventCb_base(XEventDispatcher* dispatcher,int code)
{
	if (ISNULL(dispatcher, "") ||ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_RemoveEventCb, bool (*)(XEventDispatcher*, int))(dispatcher, code);
}

void XEventDispatcher_handler_base(XEventDispatcher* dispatcher)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return ;
	XClassGetVirtualFunc(dispatcher, EXEventDispatcher_Handler, void(*)(XEventDispatcher*))(dispatcher);
}

void VXEventDispatcher_delete(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return;
	XEventMin* event = NULL;
	while (XQueueBase_receive_base(dispatcher->m_queue, &event))
	{//释放之前先释放事件
		XMemory_free(event);
	}
	XQueueBase_delete_base(dispatcher->m_queue);
	XMapBase_delete_base(dispatcher->m_filter_cb);
	XMemory_free(dispatcher);
}

bool VXEventDispatcher_sendEvent(XEventDispatcher* d, XEventMin* event)
{
	//XEventDispatcher* d = dispatcher;
	if (!event->accept)
	{
		XMapBase** pvCodeMap = XMapBase_value_base(d->m_filter_cb, &(event->receiver));
		if (pvCodeMap != NULL)
		{
			XEventCallback* c = XMapBase_value_base(*pvCodeMap, &(event->code));
			if (c != NULL)
			{//有回调函数
				event->userData = c->userData;
				c->callback(event);
			}
			if (!event->accept)
			{//接收全部事件
				int code = XEVENT_ALL;
				XEventCallback* callback = XMapBase_value_base(*pvCodeMap, &code);
				if (callback != NULL)
				{//有回调函数
					event->userData = callback->userData;
					callback->callback(event);
				}
			}
		}
		else if (event->receiver == NULL)
		{//接收者无指定
			for_each_iterator(d->m_filter_cb, XHash, it)
			{
				XMapBase* pvMap = XPair_Second(XHash_iterator_data(&it), XMapBase*);
				XEventCallback* c = XMapBase_value_base(pvMap, &(event->code));
				if (c != NULL)
				{//有回调函数
					event->userData = c->userData;
					c->callback(event);
					if (event->accept)
						break;
				}
				if (!event->accept)
				{//接收全部事件
					int code = XEVENT_ALL;
					XEventCallback* callback = XMapBase_value_base(pvMap, &code);
					if (callback != NULL)
					{//有回调函数
						event->userData = callback->userData;
						callback->callback(event);
						if (event->accept)
							break;
					}
				}
			}
		}
	}
	//if(event->accept)//
	XMemory_free(event);//事件被接受，执行完了释放
	return true;
}

bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEventMin* event)
{
	if (dispatcher == NULL || event == NULL)
		return false;
	if (XEvent_Timestamp(event) == 0)//修正事件发生时间
		XEvent_Timestamp(event) = XTimerBase_getCurrentTime();
	return XQueueBase_push_base(dispatcher->m_queue, &event);
}

bool VXEventDispatcher_addEventCb(XEventDispatcher* dispatcher, int code, XEventCB cb, void* userData)
{
	if (dispatcher == NULL || cb == NULL)
		return false;
	XEventCallback c = { cb,userData };
	XMapBase_insert_base(dispatcher->m_filter_cb, &code, &c);
	return true;
}

bool VXEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code)
{
	if (dispatcher == NULL)
		return false;
	XMapBase_remove_base(dispatcher->m_filter_cb, &code);
	return true;
}

void VXEventDispatcher_handler(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return;
	XEventMin* event = NULL;
	while (XQueueBase_receive_base(dispatcher->m_queue, &event))
	{
		if (event == NULL)
			continue;
		VXEventDispatcher_sendEvent(dispatcher, event);
	}
}
