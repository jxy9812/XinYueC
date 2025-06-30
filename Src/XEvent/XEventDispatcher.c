#include"XEvent.h"
#include"XEventDispatcher.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XEquality.h"
#include"XListSLinked.h"
#include"XObject.h"
static void VXEventDispatcher_delete(XEventDispatcher* dispatcher);
static bool VXEventDispatcher_sendEvent(XEventDispatcher* dispatcher, XEventMin* event);
static bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEventMin* event);
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
	void* table[] = { VXEventDispatcher_sendEvent,VXEventDispatcher_postEvent };
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
	return XEventDispatcher_create(XCircularQueueAtomic_Create(XEventMin*, queueCount), XHashMap_Create(int, XEventCallback, XHash_murmur3_32, XEquality_int));
}

void XEventDispatcher_init(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return dispatcher;
	memset(((XClass*)dispatcher)+1,0,sizeof(XEventDispatcher)-sizeof(XClass));
	XClass_init(dispatcher);
	XClassGetVtable(dispatcher) = XEventDispatcher_class_init();
}

bool XEventDispatcher_addEvent(XEventDispatcher* dispatcher, XEventMin* event)
{
	if (dispatcher == NULL || event == NULL)
		return false;
	if (XEvent_Timestamp(event) == 0)
		XEvent_Timestamp(event) = XTimerBase_getCurrentTime();
	return XQueueBase_push_base(dispatcher->m_queue, &event);
}

bool XEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XEventCB cb, int code, void* userData)
{
	if (dispatcher == NULL || cb == NULL)
		return false;
	XEventCallback c = { cb,userData };
	XMapBase_insert_base(dispatcher->m_filter_cb, &code, &c);
	return true;
}

bool XEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code)
{
	if (dispatcher == NULL)
		return false;
	XMapBase_remove_base(dispatcher->m_filter_cb, &code);
	return true;
}

bool XEventDispatcher_setAllEventCb(XEventDispatcher* dispatcher, XEventCB cb, void* userData)
{
	if (dispatcher == NULL)
		return false;
	dispatcher->m_allEvent_cb = cb;
	dispatcher->m_allEvent_user_data = userData;
}

void XEventDispatcher_handler(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return;
	XEventMin* event = NULL;
	while (XQueueBase_receive_base(dispatcher->m_queue, &event))
		//if (XQueueBase_receive_base(dispatcher->m_queue, &event))
	{
		if (event != NULL)
		{
			if (!event->accept && dispatcher->m_filter_cb != NULL)
			{
				XEventCallback* c = XMapBase_value_base(dispatcher->m_filter_cb, &(event->code));
				if (c != NULL)
				{//有回调函数
					event->userData = c->userData;
					c->callback(event);
				}
			}


			if (!event->accept && dispatcher->m_allEvent_cb)
			{
				event->userData = dispatcher->m_allEvent_user_data;
				dispatcher->m_allEvent_cb(event);
			}
			//if(event->accept)//
			XMemory_free(event);//事件被接受，执行完了释放
		}

	}
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

bool VXEventDispatcher_sendEvent(XEventDispatcher* dispatcher, XEventMin* event)
{
	return false;
}

bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEventMin* event)
{
	return false;
}
