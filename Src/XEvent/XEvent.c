#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XEquality.h"
#include"XListSLinked.h"
#include"XObject.h"

XEventMin* XEventMin_create(XObject* object,int code, size_t timestamp)
{
	XEventMin* event = XMemory_malloc(sizeof(XEventMin));
	XEventMin_init(event, object,code,timestamp);
	return event;
}
void XEventMin_init(XEventMin* event, XObject* object,int code, size_t timestamp)
{
	if (event)
	{
		event->accept = false;
		event->code = code;
		event->timestamp = timestamp;
		event->userData = NULL;
		event->object = object;
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

