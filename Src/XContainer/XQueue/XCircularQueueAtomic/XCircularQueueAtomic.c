#include "XCircularQueueAtomic.h"
#if XCircularQueue_ON
void XCircularQueueAtomic_init(XCircularQueueAtomic* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize_base(this_queue,count+1);
	XAtomic_init(this_queue->m_head,0);
	XAtomic_init(this_queue->m_tail, 0);
	XClassGetVtable(this_queue) = XCircularQueueAtomic_class_init();
}
XCircularQueueAtomic* XCircularQueueAtomic_create(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XCircularQueueAtomic* this_queue = XMemory_malloc(sizeof(XCircularQueueAtomic));
	XCircularQueueAtomic_init(this_queue,typeSize,count);
	return this_queue;
}


#endif