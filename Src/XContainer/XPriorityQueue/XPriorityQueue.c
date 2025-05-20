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
	XPriorityQueue_class_init();
	this_queue->m_compare = compare;
	ObjectVtable(this_queue)=XPriorityQueueVtable;
}


void XPriorityQueue_push(XPriorityQueue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XPriorityQueue*, void*);
	ObjectVirtualFunc(this_queue, EXPriorityQueue_Push, funcPtr)(this_queue, LpValue);
}


void XPriorityQueue_pop(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return;
	typedef void (*funcPtr)(XPriorityQueue*);
	ObjectVirtualFunc(this_queue, EXPriorityQueue_Pop, funcPtr)(this_queue);
}

void* XPriorityQueue_top(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return NULL;
	typedef void* (*funcPtr)(XPriorityQueue*);
	return ObjectVirtualFunc(this_queue, EXPriorityQueue_Top, funcPtr)(this_queue);
}

bool XPriorityQueue_isEmpty(XPriorityQueue* this_queue)
{
	return XContainerObject_isEmpty(this_queue);
}

size_t XPriorityQueue_size(XPriorityQueue* this_queue)
{
	return XContainerObject_size(this_queue);
}

void XPriorityQueue_clear(XPriorityQueue* this_queue)
{
	XVector_clear(this_queue);
	//char** LPParr = &XContainerDataPtr(this_queue);//指向数组的开始
	//if (*LPParr != NULL)
	//{
	//	XMemory_free(*LPParr);//清空数组
	//	*LPParr = NULL;
	//}
}

void XPriorityQueue_free(XPriorityQueue* this_queue)
{
	XVector_free(this_queue);
	/*XPriorityQueue_clear(this_queue);
	XMemory_free(this_queue);*/
}
#endif