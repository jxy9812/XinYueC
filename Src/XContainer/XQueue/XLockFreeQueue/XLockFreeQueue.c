#include "XLockFreeQueue.h"
#if XCircularQueue_ON
void XLockFreeQueue_init(XLockFreeQueue* this_queue, size_t typeSize, size_t count)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
		return NULL;
	XVector_init(this_queue, typeSize);
	XVector_resize_base(this_queue,count+1);
	XAtomic_init(this_queue->m_head,0);
	XAtomic_init(this_queue->m_tail, 0);
	XClassGetVtable(this_queue) = XLockFreeQueue_class_init();
}
XLockFreeQueue* XLockFreeQueue_create(size_t typeSize, size_t count)
{
	if (ISNULL(typeSize, "")|| ISNULL(count, ""))
		return NULL;
	XLockFreeQueue* this_queue = XMemory_malloc(sizeof(XLockFreeQueue));
	XLockFreeQueue_init(this_queue,typeSize,count);
	Set_Class_MemoryFree(this_queue, XFree);
	return this_queue;
}


#endif