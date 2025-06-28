#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XEquality.h"
#include"XListSLinked.h"
#include"XObject.h"
typedef struct XEventCallback
{
	XEventCB callback;             // 可选的回调函数
	void* userData;              // 可选的用户数据指针
}XEventCallback;
XEventMin* XEventMin_create(int code, size_t timestamp)
{
	XEventMin* event = XMemory_malloc(sizeof(XEventMin));
	XEventMin_init(event,code,timestamp);
	return event;
}
void XEventMin_init(XEventMin* event,int code, size_t timestamp)
{
	if (event)
	{
		event->accept = false;
		event->code = code;
		event->timestamp = timestamp;
		event->userData = NULL;
	}
}
XEvent* XEvent_create(void* eventData, size_t eventDataSize)
{
	if (eventDataSize < sizeof(void*))
		eventDataSize = sizeof(void*);
	size_t size = sizeof(XEvent) - sizeof(void*) + eventDataSize;
	XEvent* event = XMemory_malloc(size);
	if (event != NULL)
	{
		if (eventData)
		{
			memset(event, 0, sizeof(XEvent) - sizeof(void*));
			memcpy(XEvent_DataPtr(event), eventData, eventDataSize);
		}
		else
		{
			memset(event, 0, size);
		}
	}
	return event;
}

XEventDispatcher* XEventDispatcher_create(XQueueBase* queue,XMapBase* map_cb)
{
	XEventDispatcher* dispatcher = XMemory_malloc(sizeof(XEventDispatcher));
	if(dispatcher==NULL)
		return dispatcher;
	dispatcher->m_queue = queue;
	dispatcher->m_filter_cb = map_cb;
	dispatcher->m_allEvent_cb = NULL;
	dispatcher->m_allEvent_user_data = NULL;
	dispatcher->m_pollList = XListSLinked_Create(XObject*);
	dispatcher->m_pollList->m_equality = XEquality_ptr;
	return dispatcher;
}

XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount)
{
	return XEventDispatcher_create(XCircularQueueAtomic_Create(XEventMin*, queueCount), XHashMap_Create(int, XEventCallback, XHash_murmur3_32,XEquality_int));
}

bool XEventDispatcher_addEvent(XEventDispatcher* dispatcher, XEventMin* event)
{
	if (dispatcher == NULL || event == NULL)
		return false;
	if (XEvent_Timestamp(event) == 0)
		XEvent_Timestamp(event) = XTimerBase_getCurrentTime();
	return XQueueBase_push_base(dispatcher->m_queue, &event);
}

void XEventDispatcher_delete(XEventDispatcher* dispatcher)
{
	if (dispatcher == NULL)
		return ;
	XEventMin* event = NULL;
	while (XQueueBase_receive_base(dispatcher->m_queue,&event))
	{//释放之前先释放事件
		XMemory_free(event);
	}
	XQueueBase_delete_base(dispatcher->m_queue);
	XMapBase_delete_base(dispatcher->m_filter_cb);

	//释放所有管理的Object
	for_each_iterator(dispatcher->m_pollList, XListSLinked, it)
	{
		XObject_delete_base(*((XObject**)XListSLinked_iterator_data(&it)));
	}
	XListBase_delete_base(dispatcher->m_pollList);
	XMemory_free(dispatcher);
}

bool XEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XEventCB cb, int code, void* userData)
{
	if (dispatcher == NULL|| cb==NULL)
		return false;
	XEventCallback c = {cb,userData };
	XMapBase_insert_base(dispatcher->m_filter_cb,&code,&c);
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
	for_each_iterator(dispatcher->m_pollList,XListSLinked,it)
	{
		XObject_poll_base(*((XObject**)XListSLinked_iterator_data(&it)));
	}
	XEventMin* event = NULL;
	while (XQueueBase_receive_base(dispatcher->m_queue, &event))
	//if (XQueueBase_receive_base(dispatcher->m_queue, &event))
	{
		if (event != NULL)
		{
			if (!event->accept&& dispatcher->m_filter_cb!=NULL)
			{
				XEventCallback* c = XMapBase_value_base(dispatcher->m_filter_cb, &(event->code));
				if (c != NULL)
				{//有回调函数
					event->userData = c->userData;
					c->callback(event);
				}
			}
			
			
			if (!event->accept&&dispatcher->m_allEvent_cb)
			{
				event->userData = dispatcher->m_allEvent_user_data;
				dispatcher->m_allEvent_cb(event);
			}
			//if(event->accept)//
				XMemory_free(event);//事件被接受，执行完了释放
		}

	}
}

bool XEventDispatcher_addObject(XEventDispatcher* dispatcher, XObject* object)
{
	if (dispatcher == NULL || object == NULL)
		return false;
	if (XListBase_push_back_base(dispatcher->m_pollList, &object) != NULL)
	{
		object->m_eventDispatcher = dispatcher;
		return true;
	}
	return false;
}

bool XEventDispatcher_removeObject(XEventDispatcher* dispatcher, XObject* object)
{
	if (dispatcher == NULL || object == NULL)
		return false;
	if (object->m_eventDispatcher == dispatcher)
	{
		XListBase_remove_base(dispatcher->m_pollList, &object);
		object->m_eventDispatcher == NULL;
		return true;
	}
	return false;
}

size_t XEventDispatcher_getObjectSize(XEventDispatcher* dispatcher)
{
	if(dispatcher==NULL)
		return 0;
	XListBase_getSize_base(dispatcher->m_pollList);
}
