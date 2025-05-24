#include "XCircularQueueAtomic.h"
#if XCircularQueue_ON
void XCircularQueueAtomic_init(XCircularQueueAtomic* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize(this_queue,count);
	XAtomic_init(this_queue->m_head,0);
	XAtomic_init(this_queue->m_tail, 0);
	XCircularQueueAtomic_class_init();
	ObjectVtable(this_queue) = XCircularQueueAtomicVtable;
}
XCircularQueueAtomic* XCircularQueueAtomic_new(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueueAtomic* this_queue = XMemory_malloc(sizeof(XCircularQueueAtomic));
	XCircularQueueAtomic_init(this_queue,typeSize,count+1);
	return this_queue;
}

bool XCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return false;
	return ObjectVirtualFunc(this_queue, EXCircularQueueAtomic_Push, bool (*)(XCircularQueueAtomic*, void*))(this_queue, LpValue);
}

void XCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return;
	ObjectVirtualFunc(this_queue, EXCircularQueueAtomic_Pop, void (*)(XCircularQueueAtomic*))(this_queue);
}

void* XCircularQueueAtomic_top(XCircularQueueAtomic* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return NULL;
	return ObjectVirtualFunc(this_queue, EXCircularQueueAtomic_Top, void* (*)(XCircularQueueAtomic*))(this_queue);
}

#endif