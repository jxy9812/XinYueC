#include"XPriority_Queue.h"
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
XPriority_Queue* XPriority_Queue_new(size_t typeSize, XCompare compare)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_queue = malloc(sizeof(XPriority_Queue));
	XPriority_Queue_init(this_queue, typeSize,compare);
	return this_queue;
}

void XPriority_Queue_init(XPriority_Queue* this_queue, size_t typeSize, XCompare compare)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_queue, typeSize);
	this_queue->compare = compare;
}

//插入向上调整
static void AdjustUp(void* LParray,const size_t TypeSize, size_t childNSel, XCompare compare)
{
	size_t parentNSel = (childNSel - 1) / 2;//父亲节点,索引下标
	char* LPparent=NULL;//父亲的元素地址
	char* LPchild = NULL;//孩子的元素地址
	while (true)
	{
		LPparent = (char*)LParray + parentNSel * TypeSize;
		LPchild= (char*)LParray + childNSel * TypeSize;
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
void XPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	XVector_push_back(this_queue, LpValue);
	size_t size=ObjectSize(this_queue)-1;
	if (size >0)//一个元素不用调整
		AdjustUp(ObjectDataPtr(this_queue), ObjectTypeSize(this_queue), size, this_queue->compare);
}
//向下调整
static void AdjustDwon(void* LParray, const size_t nSize, const size_t TypeSize,size_t parentNSel, XCompare compare)
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
				LPchild= LPRchild;//右孩子大，默认孩子指向右孩子
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

void XPriority_Queue_pop(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	char* LParr = ObjectDataPtr(this_queue);//指向数组的开始
	size_t arrSize = XContainerObject_size(this_queue);//数组元素数量
	size_t TypeSize = XContainerObject_typeSize(this_queue);//单个元素大小字节
	//拷贝最后一个元素到第一个
	memcpy(LParr, LParr+ (arrSize - 1) * TypeSize, TypeSize);
	if(arrSize>1)
	AdjustDwon(LParr, arrSize, TypeSize,0, this_queue->compare);
	--ObjectSize(this_queue);
}

void* XPriority_Queue_top(XPriority_Queue* this_queue)
{
	if (ISNULL(this_queue, ""))
		return NULL;
	return  XVector_front(this_queue);//指向数组的开始
}

bool XPriority_Queue_empty(XPriority_Queue* this_queue)
{
	return XContainerObject_empty(this_queue);
}

size_t XPriority_Queue_size(XPriority_Queue* this_queue)
{
	return XContainerObject_size(this_queue);
}

void XPriority_Queue_clear(XPriority_Queue* this_queue)
{
	XVector_clear(this_queue);
	//char** LPParr = &ObjectDataPtr(this_queue);//指向数组的开始
	//if (*LPParr != NULL)
	//{
	//	free(*LPParr);//清空数组
	//	*LPParr = NULL;
	//}
}

void XPriority_Queue_free(XPriority_Queue* this_queue)
{
	XVector_free(this_queue);
	/*XPriority_Queue_clear(this_queue);
	free(this_queue);*/
}
