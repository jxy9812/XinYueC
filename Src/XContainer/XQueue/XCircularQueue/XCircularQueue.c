#include "XCircularQueue.h"
#if XCircularQueue_ON
void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize_base(this_queue,count+1);
	this_queue->m_autoExpansion = false;
	this_queue->m_head = 0;
	this_queue->m_tail = 0;
	XClassGetVtable(this_queue) = XCircularQueue_class_init();
}
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueue* this_queue = XMemory_malloc(sizeof(XCircularQueue));
	XCircularQueue_init(this_queue,typeSize,count);
	return this_queue;
}

void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion)
{
	if (this_queue)
	{
		this_queue->m_autoExpansion = autoExpansion;
	}
}


#endif