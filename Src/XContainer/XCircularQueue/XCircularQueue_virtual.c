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
static bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue);
static bool VXCircularQueue_isFull(const XCircularQueue* this_queue);
static void VXCircularQueue_clear(XCircularQueue* this_queue);//清空
static size_t VXCircularQueue_size(const XCircularQueue* this_queue);
//插入到队列的队尾
static bool VXCircularQueue_push(XCircularQueue* this_queue, void* LpValue);
//出队
static void VXCircularQueue_pop(XCircularQueue* this_queue);
// 返回队头元素
static void* VXCircularQueue_top(XCircularQueue* this_queue);
static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer);
void XCircularQueue_class_init()
{
	if (XCircularQueueVtable)
		return;
	void* table[] = { VXCircularQueue_push,VXCircularQueue_pop,VXCircularQueue_top,VXCircularQueue_receive,VXCircularQueue_isFull };
#if !VTABLEISSTACK
	XCircularQueueVtable = XVtable_new();
#else
	XCircularQueueVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XCircularQueueVtable, XContainerObjectVtable);
	//追加函数
	XVtable_append_array(XCircularQueueVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XCircularQueueVtable, EXContainerObject_IsEmpty) = VXCircularQueue_isEmpty;
	XVtable_At(XCircularQueueVtable, EXContainerObject_Clear) = VXCircularQueue_clear;
	XVtable_At(XCircularQueueVtable, EXContainerObject_Size) = VXCircularQueue_size;
#if SHOWCONTAINERSIZE
	printf("XPriorityQueue size:%d\n", XVtable_size(XCircularQueueVtable));
#endif // SHOWCONTAINERSIZE
}


bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return true;*/
	return ((this_queue->m_head) == (this_queue->m_tail));//头指针等于尾指针时为空
}

bool VXCircularQueue_isFull(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return false;*/
	return ((this_queue->m_tail + 1) % XContainerSize(this_queue) == this_queue->m_head);//尾指针下一个位置等于头指针时为满
}

void VXCircularQueue_clear(XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return;*/
	while (!VXCircularQueue_isEmpty(this_queue))
	{
		VXCircularQueue_pop(this_queue);
	}
}

size_t VXCircularQueue_size(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return 0;*/
	if (this_queue->m_tail >= this_queue->m_head)
		return this_queue->m_tail - this_queue->m_head;
	else
		return this_queue->m_tail+XContainerSize(this_queue) - this_queue->m_head;;

}

bool VXCircularQueue_push(XCircularQueue* this_queue, void* LpValue)
{
	if (VXCircularQueue_isFull(this_queue))
		return false;//插入失败
	memcpy(((char*)XContainerDataPtr(this_queue))+this_queue->m_tail*XContainerTypeSize(this_queue),LpValue, XContainerTypeSize(this_queue));
	this_queue->m_tail = (this_queue->m_tail + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}

void VXCircularQueue_pop(XCircularQueue* this_queue)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return;
	if (XContainerDataFreeMethod(this_queue) != NULL)
		XContainerDataFreeMethod(this_queue)(VXCircularQueue_top(this_queue));
	this_queue->m_head= (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
}

void* VXCircularQueue_top(XCircularQueue* this_queue)
{
	if(VXCircularQueue_isEmpty(this_queue))
		return NULL;
	return ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
}
bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return false;
	void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
	memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));
	if (XContainerDataFreeMethod(this_queue) != NULL)
		XContainerDataFreeMethod(this_queue)(pvTop);
	this_queue->m_head = (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}
#endif