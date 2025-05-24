#include"XQueue.h"
#if XQueue_ON
#include<stdlib.h>

XQueue* XQueue_new(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XQueue* this_queue = XMemory_malloc(sizeof(XQueue));
	XQueue_init(this_queue, typeSize);
	return this_queue;
}

void XQueue_init(XQueue* this_queue, size_t typeSize)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XList_init(this_queue, typeSize);
	XQueue_class_init();
	XClassGetVtable(this_queue) = XQueueVtable;
}

void XQueue_free(XQueue* this_queue)
{
	XList_free(this_queue);
}

void XQueue_clear(XQueue* this_queue)
{
	XList_clear(this_queue);
}

void XQueue_push(XQueue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XQueue*, void*);
	XClassGetVirtualFunc(this_queue, EXQueue_Push, funcPtr)(this_queue, LpValue);
}

void XQueue_pop(XQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XQueue*);
	XClassGetVirtualFunc(this_queue, EXQueue_Pop, funcPtr)(this_queue);
}

void* XQueue_front(XQueue* this_queue)
{
	return XList_front(this_queue);
}

void* XQueue_back(XQueue* this_queue)
{
	return XList_back(this_queue);
}

void* XQueue_top(XQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	typedef void* (*funcPtr)(XQueue*);
	return XClassGetVirtualFunc(this_queue, EXQueue_Top, funcPtr)(this_queue);
}

bool XQueue_isEmpty(XQueue* this_queue)
{
	return XList_isEmpty(this_queue);
}

size_t XQueue_size(XQueue* this_queue)
{
	return XList_size(this_queue);
}


#endif