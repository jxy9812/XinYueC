#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XListSLinked.h"
#include"XObject.h"
#include"XVariant.h"

XEvent* XEvent_create(XObject* receiver, XEventType code, size_t timestamp)
{
	XEvent* event = XMemory_malloc(sizeof(XEvent));
	XEvent_init(event, receiver, code, timestamp);
	return event;
}
void XEvent_init(XEvent* event, XObject* receiver, XEventType code, size_t timestamp)
{
	if (event)
	{
		event->status = 0;
		event->accept = false;
		event->code = code;
		event->timestamp = timestamp ? timestamp : XTimerBase_getCurrentTime();
		event->userData = NULL;
		event->receiver = receiver;
		event->spontaneous = false;
		event->deinit = NULL;
	}
}

void XEvent_deinit(XEvent* ev)
{
	if (ev && ev->deinit)
		ev->deinit(ev);
}

void XEvent_delete(XEvent* ev)
{
	XEvent_deinit(ev);
	XMemory_free(ev);
}

XEventFunc* XEventFunc_create(void(*func)(void*), void* args)
{
	XEventFunc* event = XMemory_malloc(sizeof(XEventFunc));
	if (event)
	{
		XEvent_init(&event->event, NULL, XEVENT_FUNC_RUN, 0);
		event->func = func;
		event->args = args;
		event->oneAccept = false;
	}
	return event;
}

XEventFunc* XEventFunc_create_oneAccept(void(*func)(void*), void* args)
{
	XEventFunc* event = XEventFunc_create(func, args);
	if (event)
		event->oneAccept = true;
	return event;
}

void XEventFuncRunCB(XEventFunc* event)
{
	if (event && event->func)
		event->func(event->args);
	if (event && event->oneAccept)
		XEvent_Accept(event);
}

XEventSlotFunc* XEventSlotFunc_create(XObject* sender, XObject* receiver, XSlotFunc func, void* args, XAtomic_int32_t* ref_count)
{
	XEventSlotFunc* event = XMemory_malloc(sizeof(XEventSlotFunc));
	if (event == NULL)
		return NULL;
	XEvent_init(&event->event, receiver, XEVENT_SLOT_RUN, 0);
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
		if (XAtomic_fetch_sub_int32(event->ref_count, 1) == 1)
		{
			if (event->args)
				XVariant_delete(event->args);
			XMemory_free(event->ref_count);
		}
	}
	XEvent_Accept(event);
}

