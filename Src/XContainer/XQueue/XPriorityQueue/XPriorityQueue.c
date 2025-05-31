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
	XClassGetVtable(this_queue)= XPriorityQueue_class_init();
	this_queue->m_compare = compare;
}


#endif