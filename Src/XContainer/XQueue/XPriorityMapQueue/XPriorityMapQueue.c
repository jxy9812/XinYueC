#include "XPriorityMapQueue.h"
#include "XPriorityQueue.h"
#include "XMap.h"
XVtable* XPriorityMapQueue_class_init()
{
    return NULL;
}
void XPriorityMapQueue_init(XPriorityMapQueue* this_queue, size_t prioritySize, XCompare priorityCom, size_t typeSize)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_queue, typeSize);
	XClassGetVtable(this_queue) = XPriorityMapQueue_class_init();
	XContainerDataPtr(this_queue)= XMap_create(prioritySize, typeSize, priorityCom);
	//this_queue->low_freq_queue = XPriorityQueue_create();
}