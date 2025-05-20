#include "XCircularQueue.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
//虚函数表定义
XVtable* XCircularQueueVtable = NULL;
#if VTABLEISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XCIRCULARQUEUE_VTABLE_SIZE];//虚函数数据
#endif
static bool VXCircularQueue_empty(const XCircularQueue* this_queue);
//插入到队列的队尾
static void VXCircularQueue_push(XCircularQueue* this_queue, void* LpValue);
//出队
static void VXCircularQueue_pop(XCircularQueue* this_queue);
// 返回优先队列堆顶元素
static void* VXCircularQueue_top(XCircularQueue* this_queue);
void XCircularQueue_class_init()
{
	if (XCircularQueueVtable)
		return;
	void* table[] = { VXCircularQueue_push,VXCircularQueue_pop,VXCircularQueue_top };
#if !VTABLEISSTACK
	XCircularQueueVtable = XVtable_new();
#else
	XCircularQueueVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XCircularQueueVtable, XVectorVtable);
	//追加函数
	XVtable_append_array(XCircularQueueVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XCircularQueueVtable, EXContainerObject_Empty) = VXCircularQueue_empty;

#if SHOWCONTAINERSIZE
	printf("XPriority_Queue size:%d\n", XVtable_size(XCircularQueueVtable));
#endif // SHOWCONTAINERSIZE
}


bool VXCircularQueue_empty(const XCircularQueue* this_queue)
{
	if (this_queue == NULL)
		return;
	return ((this_queue->m_head) == (this_queue->m_tail));
}

void VXCircularQueue_push(XCircularQueue* this_queue, void* LpValue)
{

}

void VXCircularQueue_pop(XCircularQueue* this_queue)
{
}

void* VXCircularQueue_top(XCircularQueue* this_queue)
{
	return NULL;
}
#endif