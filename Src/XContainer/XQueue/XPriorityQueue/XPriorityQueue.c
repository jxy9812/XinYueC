#include"XPriorityQueue.h"
#if XPriorityQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
XPriorityQueue* XPriorityQueue_create(size_t typeSize, XCompare compare, XSortOrder order)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XPriorityQueue* this_queue = XMemory_malloc(sizeof(XPriorityQueue));
	XPriorityQueue_init(this_queue, typeSize,compare,order);
	return this_queue;
}

void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare, XSortOrder order)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_queue, typeSize);
	XClassGetVtable(this_queue)= XPriorityQueue_class_init();
	XContainerSetCompare(this_queue, compare);
	this_queue->m_order = order;
}


#endif