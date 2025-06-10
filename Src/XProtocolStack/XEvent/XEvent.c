#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
XEvent* XEvent_create(size_t eventDataSize)
{
	size_t size = sizeof(XEvent) - sizeof(void*) + eventDataSize;
	XEvent* event = XMemory_malloc(size);
	if (event != NULL)
		memset(event,0,sizeof(size));
	return event;
}

bool XEventQueue_pushEvent(XEventQueue* queue, XEvent* event)
{
	if (queue == NULL)
		return false;
	event->timestamp = XTimerBase_getCurrentTime();
	return XQueueBase_push_base(queue,&event);
}

bool XEventQueue_receive(XEventQueue* queue, void* pvBuffer)
{
	if (queue == NULL)
		return false;
	return XQueueBase_receive_base(queue, &pvBuffer);
}

void XEventQueue_delete(XEventQueue* queue)
{
	if (queue == NULL)
		return ;
	XEvent* event = NULL;
	while (XEventQueue_receive(queue,&event))
	{//释放之前先释放事件
		XMemory_free(event);
	}
	XQueueBase_delete_base(queue);
}
