#include "XEventQueue.h"
#include "XMemory.h"
#include<assert.h>
XEventQueue* XEventQueue_create(XEventQueueInit init)
{
	XEventQueue* queue = XMemory_malloc(sizeof(XEventQueue));
	if (!init(queue))
	{
		XMemory_free(queue);
		return NULL;
	}
	return queue;
}
#if XEventQueueDefaultConfig
#include"XQueue.h"
static void XEventQueue_defaultConfigDelete(XEventQueue* queue)
{
	if (queue&&queue->queue)
	{
		XQueue_delete_base(queue->queue);
		queue->queue = NULL;
		XMemory_free(queue);
	}
}
static bool XEventQueue_defaultConfigPush(XEventQueue* queue, XEventQueueEventType event)
{
	if (queue && queue->queue)
	{
		XQueue_Push_Base(queue->queue, XEventQueueEventType,event);
		return true;
	}
	return false;
}
static XEventQueueEventType XEventQueue_defaultConfigTop(XEventQueue* queue)
{
	assert(queue && queue->queue);
	//if (queue && queue->queue)
	{
		return XQueue_Top_Base(queue->queue, XEventQueueEventType);
	}
	//return false;
}
static bool XEventQueue_defaultConfigPop(XEventQueue* queue)
{
	if (queue && queue->queue)
	{
		XQueue_pop_base(queue->queue);
		return true;
	}
	return false;
}
static bool XEventQueue_defaultConfigEmpty(XEventQueue* queue)
{
	if (queue && queue->queue)
	{
		return XQueue_isEmpty_base(queue->queue);
	}
	return true;
}

static void XEventQueue_defaultConfigClear(XEventQueue* queue)
{
	if (queue && queue->queue)
		XQueue_clear_base(queue->queue);
}
bool XEventQueue_defaultConfigInit(XEventQueue* queue)
{
	if(queue==NULL)
		return false;
	queue->queue = XQueue_Create(XEventQueueEventType);
	queue->free = XEventQueue_defaultConfigDelete;
	queue->pop = XEventQueue_defaultConfigPop;
	queue->push = XEventQueue_defaultConfigPush;
	queue->top = XEventQueue_defaultConfigTop;
	queue->empty = XEventQueue_defaultConfigEmpty;
	queue->clear = XEventQueue_defaultConfigClear;
}
#endif // XEventQueueDefaultConfig
void XEventQueue_free(XEventQueue* queue)
{
	if (queue && queue->free)
	{
		queue->free(queue);
	}
}
bool XEventQueue_push(XEventQueue* queue, XEventQueueEventType event)
{
	if (queue && queue->free)
		return queue->push(queue,event);
	return false;
}
XEventQueueEventType XEventQueue_Top(XEventQueue* queue)
{
	if (queue && queue->top)
		return queue->top(queue);
}
bool XEventQueue_pop(XEventQueue* queue)
{
	if (queue && queue->pop)
		return queue->pop(queue);
	return false;
}

bool XEventQueue_empty(XEventQueue* queue)
{
	if (queue && queue->empty)
		return queue->empty(queue);
	return true;
}

void XEventQueue_clear(XEventQueue* queue)
{
	if (queue && queue->clear)
		queue->clear(queue);
}
