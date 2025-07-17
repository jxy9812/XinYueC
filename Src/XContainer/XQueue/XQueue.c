#include"XQueue.h"
#if XQueue_ON
#include<stdlib.h>

XQueue* XQueue_create(size_t typeSize)
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
	XListDLinked_init(this_queue, typeSize);
	XClassGetVtable(this_queue) = XQueue_class_init();
}

void XQueue_push_base(XQueue* this_queue, void* pvValue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XQueue*, void*);
	XClassGetVirtualFunc(this_queue, EXQueue_Push, funcPtr)(this_queue, pvValue);
}

void XQueue_pop_base(XQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XQueue*);
	XClassGetVirtualFunc(this_queue, EXQueue_Pop, funcPtr)(this_queue);
}

void* XQueue_front_base(XQueue* this_queue)
{
	return XListDLinked_front_base(this_queue);
}

void* XQueue_back_base(XQueue* this_queue)
{
	return XListDLinked_back_base(this_queue);
}

void* XQueue_top_base(XQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	typedef void* (*funcPtr)(XQueue*);
	return XClassGetVirtualFunc(this_queue, EXQueue_Top, funcPtr)(this_queue);
}

#endif