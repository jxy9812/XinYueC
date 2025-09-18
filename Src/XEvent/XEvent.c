#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XListSLinked.h"
#include"XObject.h"
#include"XVariant.h"

XEventMin* XEventMin_create(XObject* receiver, XEventType code, size_t timestamp)
{
	XEventMin* event = XMemory_malloc(sizeof(XEventMin));
	XEventMin_init(event, receiver, code, timestamp);
	return event;
}
void XEventMin_init(XEventMin* event, XObject* receiver, XEventType code, size_t timestamp)
{
	if (event)
	{
		event->accept = false;
		event->code = code;
		event->timestamp = timestamp ? timestamp : XTimerBase_getCurrentTime();
		event->userData = NULL;
		event->receiver = receiver;
		event->spontaneous = false;
	}
}
XTimerEvent* XTimerEvent_create(XObject* receiver, XTimerBase* timer, size_t timestamp)
{
	XTimerEvent* event = XMemory_malloc(sizeof(XTimerEvent));
	if (event)
	{
		XEventMin_init(&event->event, receiver, XEVENT_TIMEROUT, timestamp);
		event->timer = timer;
		event->event.spontaneous = true;
	}
	return event;
}
XEvent* XEvent_create(void* eventData, size_t eventDataSize, XObject* receiver, XEventType code, size_t timestamp)
{
	if (eventDataSize < sizeof(void*))
		eventDataSize = sizeof(void*);
	size_t size = sizeof(XEvent) - sizeof(void*) + eventDataSize;
	XEvent* event = XMemory_malloc(size);
	if (event != NULL)
	{
		XEventMin_init(&event->event, receiver, code, timestamp);
		if (eventData)
		{
			memcpy(XEvent_DataPtr(event), eventData, eventDataSize);
		}
		else
		{
			memset(XEvent_DataPtr(event), 0, eventDataSize);
		}
		event->status = 0;
	}
	return event;
}

XEventFunc* XEventFunc_create(XObject* receiver, void(*func)(void*), void* args)
{
	XEventFunc* event = XMemory_malloc(sizeof(XEventFunc));
	if (event)
	{
		XEventMin_init(&event->event, receiver, XEVENT_FUNC_RUN, 0);
		event->func = func;
		event->args = args;
		event->oneAccept = false;
	}
	return event;
}

XEventFunc* XEventFunc_create_oneAccept(XObject* receiver, void(*func)(void*), void* args)
{
	XEventFunc* event = XEventFunc_create(receiver, func, args);
	if (event)
		event->oneAccept = true;
	return event;
}

void XEventFuncRunCB(XEventFunc* event)
{
	if (event && event->func)
		event->func(event->args);
	if (event && event->oneAccept)
		event->event.accept = true;
}

XEventSlotFunc* XEventSlotFunc_create(XObject* sender, XObject* receiver, XSlotFunc func, void* args, XAtomic_int32_t* ref_count)
{
	XEventSlotFunc* event = XMemory_malloc(sizeof(XEventSlotFunc));
	if (event == NULL)
		return NULL;
	XEventMin_init(&event->event, receiver, XEVENT_SLOT_RUN, 0);
	event->sender = sender;
	event->func = func;
	event->args = args;
	event->ref_count = ref_count;
	return event;
}

void XEventSlotFuncRunCB(XEventSlotFunc* event)
{
	if (event && event->func)
		event->func(event->sender, event->event.receiver, event->args);

	if (event && event->ref_count)
	{
		XAtomic_fetch_sub_int32(event->ref_count, 1);
		if (XAtomic_load_int32(event->ref_count) == 0)
		{
			if (event->args)
				XVariant_delete(event->args);
			XMemory_free(event->ref_count);
		}
	}
}

