#include"XQueue.h"
#include"XQueue_head.h"
#include<stdlib.h>
//初始化函数
XQueue* XQueue_init(int sizeType)
{
	XQUEUE* this_queue = malloc(sizeof(XQUEUE));
	this_queue->clear = XQueue_clear;//清空queue的队列，释放内存
	this_queue->push = XQueue_Push;//插入到队列的队尾
	this_queue->pop = XQueue_pop;//删除queue的队头元素
	this_queue->front = XQueue_front;//返回队列的队头元素指针，但不删除该元素
	this_queue->back = XQueue_back; //返回队列的队尾元素指针，但不删除该元素
	this_queue->empty = XQueue_empty;//当队列为空时返回true，否则返回false
	this_queue->size = XQueue_size;////返回队列中元素的个数
	this_queue->free = XQueue_free;
	this_queue->_typeSize = sizeType;
	this_queue->_size = 0;
	this_queue->_data = NULL;
	this_queue->_current = 0;
	return this_queue;
}