#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XEquality.h"
#include"XListSLinked.h"
#include"XObject.h"
#include"XVariant.h"
XEventMin* XEventMin_create(XObject* receiver,int code, size_t timestamp)
{
	XEventMin* event = XMemory_malloc(sizeof(XEventMin));
	XEventMin_init(event, receiver,code,timestamp);
	return event;
}
void XEventMin_init(XEventMin* event, XObject* receiver,int code, size_t timestamp)
{
	if (event)
	{
		event->accept = false;
		event->code = code;
		event->timestamp = timestamp;
		event->userData = NULL;
		event->receiver = receiver;
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

XEventFunc* XEventFunc_create(XObject* receiver, void(*func)(void*), void* args)
{
	XEventFunc* event = XMemory_malloc(sizeof(XEventFunc));
	XEventMin_init(event, receiver, XEVENT_FUNC_RUN, 0);
	//XEvent_UserData(event)=userData;
	event->func = func;
	event->args = args;
	event->oneAccept = false;
	return event;
}

XEventFunc* XEventFunc_create_oneAccept(XObject* receiver, void(*func)(void*), void* args)
{
	XEventFunc* event = XEventFunc_create(receiver,func,args);
	event->oneAccept = true;
	return event;
}


void XEventFuncRunCB(XEventFunc* event)
{
	if (event->func)
		event->func(event->args);
	if (event->oneAccept)
		event->event.accept = true;
}

XEventSlotFunc* XEventSlotFunc_create(XObject* sender, XObject* receiver, XSlotFunc func, void* args, XAtomic_int32_t* ref_count)
{
	XEventSlotFunc* event = XMemory_malloc(sizeof(XEventSlotFunc));
	if (event == NULL)
		return NULL;
	XEventMin_init(event, receiver, XEVENT_SLOT_RUN, 0);
	event->sender = sender;
	event->func = func;
	event->args = args;
	event->ref_count = ref_count;
	return event;
}


void XEventSlotFuncRunCB(XEventSlotFunc* event)
{
	if (event->func)
		event->func(event->sender,event->event.receiver,event->args);
	if (event->ref_count)
	{
		XAtomic_int32_t* ref_count = event->ref_count;
		XAtomic_fetch_sub_int32(ref_count, 1);
		if (XAtomic_load_int32(ref_count) == 0)
		{//该释放了
			if (event->args)
				XVariant_delete(event->args);
			XMemory_free(ref_count);
		}
	}
}

