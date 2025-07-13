#include"XEventDispatcherThread.h"
#include"XEvent.h"
#include"XHash.h"
#include"XEquality.h"
#include"XHashFunc.h"
#include"XObject.h"
#include"XHashSet.h"
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
typedef struct XEventCallback
{
	XEventCB callback;             // 可选的回调函数
	void* userData;              // 可选的用户数据指针
}XEventCallback;
static void VXEventDispatcher_delete(XEventDispatcherThread* dispatcher);
static bool VXEventDispatcher_sendEvent(XEventDispatcherThread* dispatcher, XEventMin* event);
static bool VXEventDispatcher_postEvent(XEventDispatcherThread* dispatcher, XEventMin* event);
static bool VXEventDispatcher_addEventCb(XEventDispatcherThread* dispatcher, XObject* receiver, int code, XEventCB cb, void* userData);
static bool VXEventDispatcher_removeEventCb(XEventDispatcherThread* dispatcher, XObject* receiver, int code);
static void VXEventDispatcher_handler(XEventDispatcherThread* dispatcher);
static bool VXEventDispatcherThread_addObject(XEventDispatcherThread* dispatcher, XObject* object);
static bool VXEventDispatcherThread_removeObject(XEventDispatcherThread* dispatcher, XObject* object);
static bool VXEventDispatcherThread_isEmptyObject(XEventDispatcherThread* dispatcher);

XVtable* XEventDispatcherThread_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventDispatcherThread))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XEventDispatcher_class_init());
	void* table[] = { 
		VXEventDispatcherThread_addObject,VXEventDispatcherThread_removeObject,VXEventDispatcherThread_isEmptyObject
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXEventDispatcher_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXEventDispatcher_SendEvent, VXEventDispatcher_sendEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXEventDispatcher_PostEvent, VXEventDispatcher_postEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXEventDispatcher_AddEventCb, VXEventDispatcher_addEventCb);
	XVTABLE_OVERLOAD_DEFAULT(EXEventDispatcher_RemoveEventCb, VXEventDispatcher_removeEventCb);
	XVTABLE_OVERLOAD_DEFAULT(EXEventDispatcher_Handler, VXEventDispatcher_handler);
#if SHOWCONTAINERSIZE
	printf("XEventDispatcherThread size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XEventDispatcherThread* XEventDispatcherThread_create(size_t queueCount)
{
	XEventDispatcherThread* dispatcher = XMemory_malloc(sizeof(XEventDispatcherThread));
	XEventDispatcherThread_init(dispatcher);
	XEventDispatcher* d = dispatcher;
	d->m_queue = XCircularQueueAtomic_Create(XEventMin*, queueCount);
	return dispatcher;
}

void XEventDispatcherThread_init(XEventDispatcherThread* dispatcher)
{
	if (dispatcher == NULL)
		return;
	XEventDispatcher_init(dispatcher);
	XClassGetVtable(dispatcher) = XEventDispatcherThread_class_init();
	XEventDispatcher* d = dispatcher;
	d->m_filter_cb = XHash_Create(XObject*,XHash*,XHash_murmur3_32,XEquality_ptr);
	dispatcher->m_Objects= XHashSet_Create(XObject*, XHash_murmur3_32, XEquality_ptr);
}

bool XEventDispatcherThread_addEventCb_base(XEventDispatcherThread* dispatcher, XObject* object, int code, XEventCB cb,void* userData)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_AddEventCb, bool (*)(XEventDispatcher*, XObject* , int, XEventCB, void*))(dispatcher, object, code, cb, userData);
}

bool XEventDispatcherThread_removeEventCb_base(XEventDispatcherThread* dispatcher, XObject* object, int code)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_RemoveEventCb, bool (*)(XEventDispatcher*, XObject*, int))(dispatcher, object,code);
}

bool XEventDispatcherThread_addObject_base(XEventDispatcherThread* dispatcher, XObject* object)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcherThread_AddObject, bool (*)(XEventDispatcher*, XObject*))(dispatcher, object);
}

bool XEventDispatcherThread_removeObject_base(XEventDispatcherThread* dispatcher, XObject* object)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcherThread_RemoveObject, bool (*)(XEventDispatcher*, XObject*))(dispatcher, object);
}

bool XEventDispatcherThread_isEmptyObject_base(XEventDispatcherThread* dispatcher)
{
	if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
		return false;
	return XClassGetVirtualFunc(dispatcher, EXEventDispatcherThread_IsEmptyObject, bool (*)(XEventDispatcher*))(dispatcher);
}

void VXEventDispatcher_delete(XEventDispatcherThread* dispatcher)
{
	XEventMin* event = NULL;
	XEventDispatcher* d = dispatcher;
	while (XQueueBase_receive_base(d->m_queue, &event))
	{
		XMemory_free(event);//清空事件
	}
	XQueueBase_delete_base(d->m_queue);
	for_each_iterator(d->m_filter_cb, XHash, it)
	{
		XMapBase* pvMap = XPair_Second(XHash_iterator_data(&it), XMapBase*);
		XMapBase_delete_base(pvMap);
	}
	XMapBase_delete_base(d->m_filter_cb);

	//释放管理的XObject类
	XObject* object = NULL;
	for_each_iterator(dispatcher->m_Objects, XHashSet, it)
	{
		object = *((XObject**)XHashSet_iterator_data(&it));
		XObject_delete_base(object);
	}
	XSetBase_delete_base(dispatcher->m_Objects);

	XMemory_free(dispatcher);
}

bool VXEventDispatcher_sendEvent(XEventDispatcherThread* dispatcher, XEventMin* event)
{
	XEventDispatcher* d = dispatcher;
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

bool VXEventDispatcher_postEvent(XEventDispatcherThread* dispatcher, XEventMin* event)
{
	if (XEvent_Timestamp(event) == 0)
		XEvent_Timestamp(event) = XTimerBase_getCurrentTime();
	if (!XQueueBase_push_base(((XEventDispatcher*)dispatcher)->m_queue, &event))
	{
		printf("线程事件调度器事件队列需要扩容,添加事件失败\n");
		XMemory_free(event);
		return false;
	}
	return true;
}

bool VXEventDispatcher_addEventCb(XEventDispatcherThread* dispatcher, XObject* receiver, int code, XEventCB cb,void* userData)
{
	if(receiver==NULL)
		return false;
	XEventDispatcher* d = dispatcher;
	XMapBase** pvMap=XMapBase_value_base(d->m_filter_cb,&receiver);
	XMapBase* p = NULL;
	if (pvMap == NULL)
	{
		p = XHash_Create(int, XEventCallback,XHash_murmur3_32,XEquality_int);
		XMapBase_insert_base(d->m_filter_cb,&receiver, &p);
	}
	else
	{
		p = *pvMap;
	}
	XEventCallback c = { cb,userData };
	XMapBase_insert_base(p, &code, &c);
	return true;
}

bool VXEventDispatcher_removeEventCb(XEventDispatcherThread* dispatcher, XObject* receiver, int code)
{
	if (receiver == NULL)
		return false;
	XEventDispatcher* d = dispatcher;
	XMapBase** pvMap = XMapBase_value_base(d->m_filter_cb, &receiver);
	if (pvMap == NULL)
		return false;
	return XMapBase_remove_base(*pvMap,&code);
}

void VXEventDispatcher_handler(XEventDispatcherThread* dispatcher)
{
	XEventMin* event = NULL;
	XEventDispatcher* d = dispatcher;
	if (d->m_filter_cb==NULL)
		return;
	//处理线程事件
	while (XQueueBase_receive_base(d->m_queue, &event))
	{
		if (event == NULL)
			continue;
		VXEventDispatcher_sendEvent(dispatcher,event);
	}
	//统一处理XObject的轮询
	if (!XSetBase_isEmpty_base(dispatcher->m_Objects))
	{
		XObject* object = NULL;
		for_each_iterator(dispatcher->m_Objects, XHashSet, it)
		{
			object = *((XObject**)XHashSet_iterator_data(&it));
			if (XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*)))
				XObject_poll_base(object);
		}
	}
}

bool VXEventDispatcherThread_addObject(XEventDispatcherThread* dispatcher, XObject* object)
{
	if (XSetBase_insert_base(dispatcher->m_Objects, &object))
	{
		object->m_eventDispatcher = dispatcher;
		return true;
	}
	return false;
}

bool VXEventDispatcherThread_removeObject(XEventDispatcherThread* dispatcher, XObject* object)
{
	if (XSetBase_remove_base(dispatcher->m_Objects, &object))
	{
		object->m_eventDispatcher = NULL;
		XMapBase** pvCodeMap =XMapBase_value_base(((XEventDispatcher*)dispatcher)->m_filter_cb, &object);
		if (pvCodeMap != NULL)
		{//存在事件过滤
			XMapBase_delete_base(*pvCodeMap);
			XMapBase_remove_base(((XEventDispatcher*)dispatcher)->m_filter_cb,pvCodeMap);
		}
		return true;
	}	
	return false;
}

bool VXEventDispatcherThread_isEmptyObject(XEventDispatcherThread* dispatcher)
{
	return XSetBase_isEmpty_base(dispatcher->m_Objects);
}
