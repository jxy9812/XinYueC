#include "XCircularQueueAtomic.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
static bool VXCircularQueueAtomic_isEmpty(const XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_isFull(const XCircularQueueAtomic* this_queue);
static void VXCircularQueueAtomic_clear(XCircularQueueAtomic* this_queue);//清空
static size_t VXCircularQueueAtomic_getSize(const XCircularQueueAtomic* this_queue);
//插入到队列的队尾
static bool VXCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* LpValue);
//出队
static void VXCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue);
// 返回队头元素
static void* VXCircularQueueAtomic_top(XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_receive(XCircularQueueAtomic* this_queue, void* pvBuffer);
XVtable* XCircularQueueAtomic_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCIRCULARQUEUEATOMIC_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = { VXCircularQueueAtomic_push,VXCircularQueueAtomic_pop,VXCircularQueueAtomic_top,VXCircularQueueAtomic_receive,VXCircularQueueAtomic_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_IsEmpty, VXCircularQueueAtomic_isEmpty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXCircularQueueAtomic_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Size, VXCircularQueueAtomic_getSize);
#if SHOWCONTAINERSIZE
	printf("XCircularQueueAtomic size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
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

size_t VXCircularQueueAtomic_getSize(const XCircularQueueAtomic* this_queue)
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
	if (XContainerDataDeleteMethod(this_queue) != NULL)
		XContainerDataDeleteMethod(this_queue)(VXCircularQueueAtomic_top(this_queue));
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
	//printf("原子接收数据\n");
	size_t head = XAtomic_load_size_t(&(this_queue->m_head));
	void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (head * XContainerTypeSize(this_queue));
	memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));
	if (XContainerDataDeleteMethod(this_queue) != NULL)
		XContainerDataDeleteMethod(this_queue)(pvTop);
	// 更新队头指针(原子操作)
	XAtomic_store_size_t(&(this_queue->m_head), (head + 1) % XContainerSize(this_queue));
	return true;
}
#endif