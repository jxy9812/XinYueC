#include"XPriorityQueue.h"
#if XPriorityQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
XPriorityQueue* XPriorityQueue_new(size_t typeSize, XCompare compare)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XPriorityQueue* this_queue = XMemory_malloc(sizeof(XPriorityQueue));
	XPriorityQueue_init(this_queue, typeSize,compare);
	return this_queue;
}

void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_queue, typeSize);
	XPriorityQueue_class_init();
	this_queue->m_compare = compare;
	XClassGetVtable(this_queue)=XPriorityQueueVtable;
}


void XPriorityQueue_push_base(XPriorityQueue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XPriorityQueue*, void*);
	XClassGetVirtualFunc(this_queue, EXPriorityQueue_Push, funcPtr)(this_queue, LpValue);
}


void XPriorityQueue_pop_base(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	typedef void (*funcPtr)(XPriorityQueue*);
	XClassGetVirtualFunc(this_queue, EXPriorityQueue_Pop, funcPtr)(this_queue);
}

void* XPriorityQueue_top_base(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return NULL;
	typedef void* (*funcPtr)(XPriorityQueue*);
	return XClassGetVirtualFunc(this_queue, EXPriorityQueue_Top, funcPtr)(this_queue);
}

#endif