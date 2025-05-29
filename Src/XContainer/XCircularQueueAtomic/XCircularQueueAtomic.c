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
	XClassGetVtable(this_queue) = XCircularQueueAtomicVtable;
}
XCircularQueueAtomic* XCircularQueueAtomic_new(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueueAtomic* this_queue = XMemory_malloc(sizeof(XCircularQueueAtomic));
	XCircularQueueAtomic_init(this_queue,typeSize,count+1);
	return this_queue;
}

bool XCircularQueueAtomic_push_base(XCircularQueueAtomic* this_queue, void* pvData)
{
	if (ISNULL(this_queue, "") || ISNULL(pvData, "") ||ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Push, bool (*)(XCircularQueueAtomic*, void*))(this_queue, pvData);
}

void XCircularQueueAtomic_pop_base(XCircularQueueAtomic* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	XClassGetVirtualFunc(this_queue, EXCircularQueue_Pop, void (*)(XCircularQueueAtomic*))(this_queue);
}

bool XCircularQueueAtomic_receive_base(XCircularQueueAtomic* this_queue, void* pvBuffer)
{
	if (ISNULL(this_queue, "") || ISNULL(pvBuffer, "")||ISNULL(XClassGetVtable(this_queue), ""))
		return;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Receive, bool (*)(XCircularQueueAtomic*,void*))(this_queue, pvBuffer);
}

void* XCircularQueueAtomic_top_base(XCircularQueueAtomic* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return NULL;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Top, void* (*)(XCircularQueueAtomic*))(this_queue);
}

bool XCircularQueueAtomic_isFull_base(XCircularQueueAtomic* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_IsFull, bool (*)(XCircularQueueAtomic*))(this_queue);
}

#endif