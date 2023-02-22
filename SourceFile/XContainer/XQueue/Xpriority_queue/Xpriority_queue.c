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
	if (childNSel == 0)//一个元素不用调整
		return;
	size_t parentNSel = (childNSel - 1) / 2;//父亲节点,索引下标
	char* LPparent=NULL;//父亲的元素地址
	char* LPchild = NULL;//孩子的元素地址
	while (true)
	{
		LPparent = (char*)LParray + parentNSel * TypeSize;
		LPchild= (char*)LParray + childNSel * TypeSize;
		if (compare(LPchild, LPparent))
		{
			swap(LPchild, LPparent, TypeSize);
		}
		if (parentNSel == 0)//已经调整到顶部了
			return;
		childNSel = parentNSel;
		parentNSel = (childNSel - 1) / 2;
	}
}
void XPriority_Queue_push(XPriority_Queue* this_queue, void* val)
{
	//检测是否要扩容
	XPriority_QueueCapacity(this_queue);
	//拷贝数据进来
	char* start = (char*)this_queue->object._data + this_queue->object._type * this_queue->object._size;
	memcpy(start, val, this_queue->object._type);
	this_queue->object._size++;
	AdjustUp(this_queue->object._data, this_queue->object._type, this_queue->object._size-1, this_queue->compare);
}
