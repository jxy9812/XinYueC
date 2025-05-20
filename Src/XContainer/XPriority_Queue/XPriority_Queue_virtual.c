#include"XPriority_Queue.h"
#if XPriority_Queue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
//虚函数表定义
XVtable* XPriority_QueueVtable = NULL;
//插入到队列的队尾
static void VXPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue);
//出队
static void VXPriority_Queue_pop(XPriority_Queue* this_queue);
// 返回优先队列堆顶元素
static void* VXPriority_Queue_top(XPriority_Queue* this_queue);
#if VTABLEISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[XPRIORITY_QUEUE_VTABLE_SIZE];//虚函数数据
#endif
void XPriority_Queue_class_init()
{
	if (XPriority_QueueVtable)
		return;
	void* table[] = { VXPriority_Queue_push,VXPriority_Queue_pop,VXPriority_Queue_top};
#if !VTABLEISSTACK
	XPriority_QueueVtable = XVtable_new();
#else
	XPriority_QueueVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XPriority_QueueVtable, XVectorVtable);
	//追加函数
	XVtable_append_array(XPriority_QueueVtable, table, sizeof(table) / sizeof(table[0]));

#if SHOWCONTAINERSIZE
	printf("XPriority_Queue size:%d\n", XVtable_size(XPriority_QueueVtable));
#endif // SHOWCONTAINERSIZE
}
//插入向上调整
static void AdjustUp(void* LParray, const size_t TypeSize, size_t childNSel, XCompare compare)
{
	size_t parentNSel = (childNSel - 1) / 2;//父亲节点,索引下标
	char* LPparent = NULL;//父亲的元素地址
	char* LPchild = NULL;//孩子的元素地址
	while (true)
	{
		LPparent = (char*)LParray + parentNSel * TypeSize;
		LPchild = (char*)LParray + childNSel * TypeSize;
		if (compare(LPchild, LPparent))
		{
			XSwap(LPchild, LPparent, TypeSize);
		}
		if (parentNSel == 0)//已经调整到顶部了
			return;
		childNSel = parentNSel;
		parentNSel = (childNSel - 1) / 2;
	}
}
//向下调整
static void AdjustDwon(void* LParray, const size_t nSize, const size_t TypeSize, size_t parentNSel, XCompare compare)
{
	size_t child = parentNSel * 2 + 1;//默认左孩子
	while (child < nSize)
	{
		char* LPparent = (char*)LParray + parentNSel * TypeSize;//父亲当前指针
		char* LPchild = (char*)LParray + child * TypeSize;//左孩子指针
		if (child + 1 < nSize)//右孩子存在时
		{
			char* LPRchild = LPchild + TypeSize;//右孩子指针
			if (!compare(LPchild, LPRchild))//排序比较函数，选出大的那个
			{
				LPchild = LPRchild;//右孩子大，默认孩子指向右孩子
			}
		}
		if (compare(LPchild, LPparent))//排序比较函数
		{
			XSwap(LPchild, LPparent, TypeSize);//交换函数
			parentNSel = child;//父亲节点更新
			child = parentNSel * 2 + 1;//默认孩子更新
		}
		else
		{
			break;
		}
	}
}

void VXPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, "")|| ISNULL(LpValue, ""))
		return ;
	XVector_push_back(this_queue, LpValue);
	size_t size = XContainerSize(this_queue) - 1;
	if (size > 0)//一个元素不用调整
		AdjustUp(XContainerDataPtr(this_queue), XContainerTypeSize(this_queue), size, this_queue->m_compare);
}

void VXPriority_Queue_pop(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, "")|| XContainerObject_empty(this_queue))
		return ;
	char* LParr = XContainerDataPtr(this_queue);//指向数组的开始
	size_t arrSize = XContainerObject_size(this_queue);//数组元素数量
	size_t TypeSize = XContainerObject_typeSize(this_queue);//单个元素大小字节
	//拷贝最后一个元素到第一个
	if (arrSize > 1)
	{
		memcpy(LParr, LParr + (arrSize - 1) * TypeSize, TypeSize);
		AdjustDwon(LParr, arrSize, TypeSize, 0, this_queue->m_compare);
	}
	--XContainerSize(this_queue);
}

void* VXPriority_Queue_top(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	return  XVector_front(this_queue);//指向数组的开始
}
#endif