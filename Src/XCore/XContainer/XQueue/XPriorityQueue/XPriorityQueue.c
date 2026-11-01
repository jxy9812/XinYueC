#include"XPriorityQueue.h"
#if XPriorityQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
XPriorityQueue* XPriorityQueue_create_ex(XMemoryType memory, size_t typeSize, XCompare compare, XSortOrder order)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XPriorityQueue* this_queue = XMemory_malloc(sizeof(XPriorityQueue), memory);
	XPriorityQueue_init(this_queue, typeSize,compare,order);
	Set_Class_Memory(this_queue, memory); Set_Class_IsHeap(this_queue, true);
	return this_queue;
}

void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare, XSortOrder order)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_queue, typeSize,false);
	XClassGetVtable(this_queue)= XPriorityQueue_class_init();
	//XContainerSetCompare(this_queue, compare);
    this_queue->compare = compare;
	this_queue->m_order = order;
}


#endif