#include"queue.h"
#include"queue_head.h"
#include<stdlib.h>
//初始化函数
queue* NewQueue(int sizeType)
{
	QUEUE* que = malloc(sizeof(QUEUE));
	que->clear = Queue_clear;//清空queue的队列，释放内存
	que->push = Queue_Push;//插入到队列的队尾
	que->pop = Queue_pop;//删除queue的队头元素
	que->front = Queue_front;//返回队列的队头元素指针，但不删除该元素
	que->back = Queue_back; //返回队列的队尾元素指针，但不删除该元素
	que->empty = Queue_empty;//当队列为空时返回true，否则返回false
	que->size = Queue_size;////返回队列中元素的个数
	que->_type = sizeType;
	que->_size = 0;
	que->_date = NULL;
	que->_current = 0;
	return que;
}