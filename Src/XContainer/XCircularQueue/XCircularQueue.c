#include "XCircularQueue.h"
#if XCircularQueue_ON
void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize(this_queue,count);
	this_queue->m_head = 0;
	this_queue->m_tail = 0;
	XCircularQueue_class_init();
	ObjectVtable(this_queue) = XCircularQueueVtable;
}
XCircularQueue* XCircularQueue_new(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueue* this_queue = XMemory_malloc(sizeof(XCircularQueue));
	XCircularQueue_init(this_queue,typeSize,count+1);
	return this_queue;
}

bool XCircularQueue_push(XCircularQueue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return false;
	return ObjectVirtualFunc(this_queue, EXCircularQueue_Push, bool (*)(XCircularQueue*, void*))(this_queue, LpValue);
}

void XCircularQueue_pop(XCircularQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return;
	ObjectVirtualFunc(this_queue, EXCircularQueue_Pop, void (*)(XCircularQueue*))(this_queue);
}

void* XCircularQueue_top(XCircularQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return NULL;
	return ObjectVirtualFunc(this_queue, EXCircularQueue_Top, void* (*)(XCircularQueue*))(this_queue);
}

#endif