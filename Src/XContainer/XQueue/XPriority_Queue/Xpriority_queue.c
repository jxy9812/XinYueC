#include"XPriority_Queue.h"
#include"XAlgorithm.h"
#include<string.h>
#define INITNUM 4
//检测是否需要扩容
static void XPriority_QueueCapacity(XPriority_Queue* this_queue)
{
	if (isNULL(isNULLInfo(this_queue, "")))
		return;
	if (this_queue->object._capacity == 0)
	{
		this_queue->object._data = malloc(this_queue->object._type * INITNUM);
		if (this_queue->object._data == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			this_queue->object._capacity = INITNUM;
		}
	}
	else if (this_queue->object._capacity == this_queue->object._size)//空间已满需要扩容
	{
		void* _data = realloc(this_queue->object._data, this_queue->object._capacity * this_queue->object._type * 1.5);
		if (_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			this_queue->object._data = _data;
			this_queue->object._capacity *= 1.5;
		}
	}
}

XPriority_Queue* XPriority_Queue_init(size_t TypeSize, XCompare compare)
{
	XPriority_Queue* this_queue = malloc(sizeof(XPriority_Queue));
	if (isNULL(isNULLInfo(this_queue, "")))
		return NULL;
	XContainerObject_init(this_queue, TypeSize);
	this_queue->compare = compare;
	return this_queue;
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
void XPriority_Queue_push(XPriority_Queue* this_queue, void* val)
{
	if (isNULL(isNULLInfo(this_queue, "")))
		return NULL;
	//检测是否要扩容
	XPriority_QueueCapacity(this_queue);
	//拷贝数据进来
	char* LParr = this_queue->object._data;//指向数组的开始
	size_t arrSize = XContainerObject_size(this_queue);//数组元素数量
	size_t TypeSize = XContainerObject_type(this_queue);//单个元素大小字节
	char* start = LParr + arrSize * TypeSize;
	memcpy(start, val, TypeSize);
	if (arrSize>0)//一个元素不用调整
		AdjustUp(LParr, TypeSize, arrSize, this_queue->compare);
	++ObjectSize(this_queue);
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
	if (isNULL(isNULLInfo(this_queue, "")))
		return NULL;
	char* LParr = this_queue->object._data;//指向数组的开始
	size_t arrSize = XContainerObject_size(this_queue);//数组元素数量
	size_t TypeSize = XContainerObject_type(this_queue);//单个元素大小字节
	//拷贝最后一个元素到第一个
	memcpy(LParr, LParr+ (arrSize - 1) * TypeSize, TypeSize);
	if(arrSize>1)
	AdjustDwon(LParr, arrSize, TypeSize,0, this_queue->compare);
	--ObjectSize(this_queue);
}

void* XPriority_Queue_top(XPriority_Queue* this_queue)
{
	if (isNULL(isNULLInfo(this_queue, "")))
		return NULL;
	return this_queue->object._data;//指向数组的开始
}

bool XPriority_Queue_empty(XPriority_Queue* this_queue)
{
	return XContainerObject_empty(this_queue);
}

int XPriority_Queue_size(XPriority_Queue* this_queue)
{
	return XContainerObject_size(this_queue);
}

void XPriority_Queue_clear(XPriority_Queue* this_queue)
{
	if (isNULL(isNULLInfo(this_queue, "")))
		return ;
	char** LPParr = &this_queue->object._data;//指向数组的开始
	if (*LPParr != NULL)
	{
		free(*LPParr);//清空数组
		*LPParr = NULL;
	}
}

void XPriority_Queue_free(XPriority_Queue* this_queue)
{
	if (isNULL(isNULLInfo(this_queue, "")))
		return ;
	XPriority_Queue_clear(this_queue);
	free(this_queue);
}
