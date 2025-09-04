#include"XPriorityQueue.h"
#if XPriorityQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
//插入到队列的队尾
static void VXPriorityQueue_push(XPriorityQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXPriorityQueue_pop(XPriorityQueue* this_queue);
// 返回优先队列堆顶元素
static void* VXPriorityQueue_top(XPriorityQueue* this_queue);
static bool VXPriorityQueue_receive(XPriorityQueue* this_queue, void* pvBuffer);
static bool VXPriorityQueue_isFull(const XPriorityQueue* this_queue);
XVtable* XPriorityQueue_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XPRIORITYQUEUE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = { VXPriorityQueue_push,VXPriorityQueue_pop,VXPriorityQueue_top,VXPriorityQueue_receive,VXPriorityQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

#if SHOWCONTAINERSIZE
	printf("XPriorityQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
//插入向上调整
static void AdjustUp(void* LParray, const size_t TypeSize, size_t childNSel, XCompare compare, XSortOrder order)
{
	size_t parentNSel = (childNSel - 1) / 2;//父亲节点,索引下标
	char* LPparent = NULL;//父亲的元素地址
	char* LPchild = NULL;//孩子的元素地址
	int32_t cmp;
	while (true)
	{
		LPparent = (char*)LParray + parentNSel * TypeSize;
		LPchild = (char*)LParray + childNSel * TypeSize;
		cmp = compare(LPchild, LPparent);
		if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
		//if (compare(LPchild, LPparent))
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
static void AdjustDwon(void* LParray, const size_t nSize, const size_t TypeSize, size_t parentNSel, XCompare compare, XSortOrder order)
{
	size_t child = parentNSel * 2 + 1;//默认左孩子
	int32_t cmp;
	while (child < nSize)
	{
		char* LPparent = (char*)LParray + parentNSel * TypeSize;//父亲当前指针
		char* LPchild = (char*)LParray + child * TypeSize;//左孩子指针
		if (child + 1 < nSize)//右孩子存在时
		{
			char* LPRchild = LPchild + TypeSize;//右孩子指针
			cmp = compare(LPchild, LPRchild);
			if (!((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
			//if (!compare(LPchild, LPRchild))//排序比较函数，选出大的那个
			{
				LPchild = LPRchild;//右孩子大，默认孩子指向右孩子
			}
		}
		cmp = compare(LPchild, LPparent);
		if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
		//if (compare(LPchild, LPparent))//排序比较函数
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

void VXPriorityQueue_push(XPriorityQueue* this_queue, void* pvData, XCDataCreatMethod dataCreatMethod)
{
	if (ISNULL(this_queue, "")|| ISNULL(pvData, ""))
		return ;
	XVtableGetFunc(XVector_class_init(), EXVector_Push_Back,void(*)(XVector*,void*, XCDataCreatMethod))(this_queue, pvData,dataCreatMethod);
	size_t size = XContainerSize(this_queue) - 1;
	if (size > 0)//一个元素不用调整
		AdjustUp(XContainerDataPtr(this_queue), XContainerTypeSize(this_queue), size, XContainerCompare(this_queue), this_queue->m_order);
}

void VXPriorityQueue_pop(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, "")|| XContainerObject_isEmpty_base(this_queue))
		return ;
	char* LParr = XContainerDataPtr(this_queue);//指向数组的开始
	size_t arrSize = XContainerObject_size_base(this_queue);//数组元素数量
	size_t TypeSize = XContainerObject_typeSize_base(this_queue);//单个元素大小字节
	//拷贝最后一个元素到第一个
	if (arrSize > 1)
	{
		memcpy(LParr, LParr + (arrSize - 1) * TypeSize, TypeSize);
		AdjustDwon(LParr, arrSize, TypeSize, 0, XContainerCompare(this_queue),this_queue->m_order);
	}
	--XContainerSize(this_queue);
}

void* VXPriorityQueue_top(XPriorityQueue* this_queue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	return XVtableGetFunc(XVector_class_init(), EXVector_Front, void*(*)(XVector*))(this_queue);
	//return  XVector_front_base(this_queue);//指向数组的开始
}
bool VXPriorityQueue_receive(XPriorityQueue* this_queue, void* pvBuffer)
{
	void* data = VXPriorityQueue_top(this_queue);//指向数组的开始
	if(data==NULL)
		return false;
	memcpy(pvBuffer, data,XContainerCapacity(this_queue));
	VXPriorityQueue_pop(this_queue);
	return true;
}
bool VXPriorityQueue_isFull(const XPriorityQueue* this_queue)
{
	return XContainerObject_size_base(this_queue)== XContainerObject_capacity_base(this_queue);
}
#endif