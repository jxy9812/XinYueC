#include "XCircularQueueAtomic.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
//虚函数表定义
XVtable* XCircularQueueAtomicVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XCIRCULARQUEUEATOMIC_VTABLE_SIZE];//虚函数数据
#endif
static bool VXCircularQueueAtomic_isEmpty(const XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_isFull(const XCircularQueueAtomic* this_queue);
static void VXCircularQueueAtomic_clear(XCircularQueueAtomic* this_queue);//清空
static size_t VXCircularQueueAtomic_size(const XCircularQueueAtomic* this_queue);
//插入到队列的队尾
static bool VXCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* LpValue);
//出队
static void VXCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue);
// 返回队头元素
static void* VXCircularQueueAtomic_top(XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_receive(XCircularQueueAtomic* this_queue, void* pvBuffer);
void XCircularQueueAtomic_class_init()
{
	if (XCircularQueueAtomicVtable)
		return;
	void* table[] = { VXCircularQueueAtomic_push,VXCircularQueueAtomic_pop,VXCircularQueueAtomic_top,VXCircularQueueAtomic_receive,VXCircularQueueAtomic_isFull };
#if !VTABLE_ISSTACK
	XCircularQueueAtomicVtable = XVtable_new();
#else
	XCircularQueueAtomicVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XCircularQueueAtomicVtable, XContainerObjectVtable);
	//追加函数
	XVtable_append_array(XCircularQueueAtomicVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XCircularQueueAtomicVtable, EXContainerObject_IsEmpty) = VXCircularQueueAtomic_isEmpty;
	XVtable_At(XCircularQueueAtomicVtable, EXContainerObject_Clear) = VXCircularQueueAtomic_clear;
	XVtable_At(XCircularQueueAtomicVtable, EXContainerObject_Size) = VXCircularQueueAtomic_size;
#if SHOWCONTAINERSIZE
	printf("XPriorityQueue size:%d\n", XVtable_size(XCircularQueueAtomicVtable));
#endif // SHOWCONTAINERSIZE
}


bool VXCircularQueueAtomic_isEmpty(const XCircularQueueAtomic* this_queue)
{
	if (this_queue == NULL)
		return true;
	return (XAtomic_load_size_t(&(this_queue->m_head)) == XAtomic_load_size_t(&(this_queue->m_tail)));//头指针等于尾指针时为空
}

bool VXCircularQueueAtomic_isFull(const XCircularQueueAtomic* this_queue)
{
	if (this_queue == NULL)
		return false;
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	size_t tail = XAtomic_load_size_t(&(this_queue->m_tail));
	return ((tail + 1) % XContainerSize(this_queue) == head);//尾指针下一个位置等于头指针时为满
}

void VXCircularQueueAtomic_clear(XCircularQueueAtomic* this_queue)
{
	if (this_queue == NULL)
		return;
	while (!VXCircularQueueAtomic_isEmpty(this_queue))
	{
		VXCircularQueueAtomic_pop(this_queue);
	}
}

size_t VXCircularQueueAtomic_size(const XCircularQueueAtomic* this_queue)
{
	if (this_queue == NULL)
		return 0;
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	size_t tail = XAtomic_load_size_t(&(this_queue->m_tail));
	return (tail >= head) ? (tail - head) : (XContainerSize(this_queue) - head + tail);
}

bool VXCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* LpValue)
{
	if (VXCircularQueueAtomic_isFull(this_queue))
		return false;//插入失败
	size_t tail = XAtomic_load_size_t(&(this_queue->m_tail));
	size_t next_tail = (tail + 1) % XContainerSize(this_queue);
	memcpy(((char*)XContainerDataPtr(this_queue))+ tail *XContainerTypeSize(this_queue),LpValue, XContainerTypeSize(this_queue));
	// 更新队尾指针(原子操作)
	XAtomic_store_size_t(&(this_queue->m_tail), next_tail);
	return true;
}

void VXCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue)
{
	if (VXCircularQueueAtomic_isEmpty(this_queue))
		return;
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	if (XContainerDataFreeMethod(this_queue) != NULL)
		XContainerDataFreeMethod(this_queue)(VXCircularQueueAtomic_top(this_queue));
	// 更新队头指针(原子操作)
	XAtomic_store_size_t(&(this_queue->m_head), (head + 1) % XContainerSize(this_queue));
}

void* VXCircularQueueAtomic_top(XCircularQueueAtomic* this_queue)
{
	if(VXCircularQueueAtomic_isEmpty(this_queue))
		return NULL;
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	return ((char*)XContainerDataPtr(this_queue)) + (head * XContainerTypeSize(this_queue));
}
bool VXCircularQueueAtomic_receive(XCircularQueueAtomic* this_queue, void* pvBuffer)
{
	if (VXCircularQueueAtomic_isEmpty(this_queue))
		return false;
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (head * XContainerTypeSize(this_queue));
	memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));
	if (XContainerDataFreeMethod(this_queue) != NULL)
		XContainerDataFreeMethod(this_queue)(pvTop);
	// 更新队头指针(原子操作)
	XAtomic_store_size_t(&(this_queue->m_head), (head + 1) % XContainerSize(this_queue));
	return true;
}
#endif