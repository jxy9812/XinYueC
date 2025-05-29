#include "XCircularQueue.h"
#if XCircularQueue_ON
void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize(this_queue,count);
	this_queue->m_autoExpansion = false;
	this_queue->m_head = 0;
	this_queue->m_tail = 0;
	XCircularQueue_class_init();
	XClassGetVtable(this_queue) = XCircularQueueVtable;
}
XCircularQueue* XCircularQueue_new(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueue* this_queue = XMemory_malloc(sizeof(XCircularQueue));
	XCircularQueue_init(this_queue,typeSize,count+1);
	return this_queue;
}

void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion)
{
	if (this_queue)
	{
		this_queue->m_autoExpansion = autoExpansion;
	}
}

bool XCircularQueue_push_base(XCircularQueue* this_queue, void* pvData)
{
	if (ISNULL(this_queue, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Push, bool (*)(XCircularQueue*, void*))(this_queue, pvData);
}

void XCircularQueue_pop_base(XCircularQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	XClassGetVirtualFunc(this_queue, EXCircularQueue_Pop, void (*)(XCircularQueue*))(this_queue);
}

bool XCircularQueue_receive_base(XCircularQueue* this_queue, void* pvBuffer)
{
	if (ISNULL(this_queue, "") || ISNULL(pvBuffer, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Receive, bool (*)(XCircularQueue*, void*))(this_queue, pvBuffer);
}

void* XCircularQueue_top_base(XCircularQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return NULL;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_Top, void* (*)(XCircularQueue*))(this_queue);
}

bool XCircularQueue_isFull_base(XCircularQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXCircularQueue_IsFull, bool (*)(XCircularQueue*))(this_queue);
}

#endif