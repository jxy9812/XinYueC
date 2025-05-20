#include"XPriority_Queue.h"
#if XPriority_Queue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
XPriority_Queue* XPriority_Queue_new(size_t typeSize, XCompare compare)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XPriority_Queue* this_queue = XMemory_malloc(sizeof(XPriority_Queue));
	XPriority_Queue_init(this_queue, typeSize,compare);
	return this_queue;
}

void XPriority_Queue_init(XPriority_Queue* this_queue, size_t typeSize, XCompare compare)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_queue, typeSize);
	XPriority_Queue_class_init();
	this_queue->m_compare = compare;
	ObjectVtable(this_queue)=XPriority_QueueVtable;
}


void XPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return ;
	typedef void (*funcPtr)(XPriority_Queue*, void*);
	ObjectVirtualFunc(this_queue, EXPriority_Queue_Push, funcPtr)(this_queue, LpValue);
}


void XPriority_Queue_pop(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return;
	typedef void (*funcPtr)(XPriority_Queue*);
	ObjectVirtualFunc(this_queue, EXPriority_Queue_Pop, funcPtr)(this_queue);
}

void* XPriority_Queue_top(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(ObjectVtable(this_queue), ""))
		return NULL;
	typedef void* (*funcPtr)(XPriority_Queue*);
	return ObjectVirtualFunc(this_queue, EXPriority_Queue_Top, funcPtr)(this_queue);
}

bool XPriority_Queue_empty(XPriority_Queue* this_queue)
{
	return XContainerObject_isEmpty(this_queue);
}

size_t XPriority_Queue_size(XPriority_Queue* this_queue)
{
	return XContainerObject_size(this_queue);
}

void XPriority_Queue_clear(XPriority_Queue* this_queue)
{
	XVector_clear(this_queue);
	//char** LPParr = &XContainerDataPtr(this_queue);//指向数组的开始
	//if (*LPParr != NULL)
	//{
	//	XMemory_free(*LPParr);//清空数组
	//	*LPParr = NULL;
	//}
}

void XPriority_Queue_free(XPriority_Queue* this_queue)
{
	XVector_free(this_queue);
	/*XPriority_Queue_clear(this_queue);
	XMemory_free(this_queue);*/
}
#endif