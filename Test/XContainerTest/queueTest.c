#include"XDataStructTest.h"
#if DEMOTEST
#include"XQueue.h"
#include"XPriorityQueue.h"
#include"XSort.h"
#include"XLess.h"
#include"XGreater.h"
#include"XVector.h"
void queueTest()
{
#if	XQueue_ON
	printf("XQueue 测试\n");
	XQueue* queue = XQueue_Create(int);
	int array[] = { 0,1,2,3,4,5,6,7,8,9 };
	for (size_t i = 0; i < sizeof(array)/sizeof(array[0]); i++)
	{
		XQueue_push_base(queue, array + i);
	}
	while (!XQueue_isEmpty_base(queue))
	{
		printf("%d ", XQueue_Top_Base(queue,int));
		XQueue_pop_base(queue);
	}
	printf("\n");
	XQueue_delete_base(queue);
#endif
}
#if	XPriorityQueue_ON
static insertData(void* values ,void*args)
{
	XPriorityQueue_push_base(args, values);
	printf("入队:%d 堆顶:%d\n", *(int*)values, *(int*)XPriorityQueue_top_base(args));
}
#endif
void XPriority_QueueTest()
{
#if	XPriorityQueue_ON
	printf("XPriority_QueueTest 测试\n");
	//XPriorityQueue* queue=XPriorityQueue_create(sizeof(int),XLess_int);//小堆，先出小的
	XPriorityQueue* queue = XPriorityQueue_create(sizeof(int), XGreater_int);//大堆，先出大的
	XVector* v = XVector_Create(int);
	for (size_t i = 0; i < 10; i++)
	{
		XVector_push_back_base(v, &i);
	}
	XDerangement(XContainerDataPtr(v),XVector_getSize_base(v), sizeof(int));
	//printf("入队数据:");
	XVector_iterator_for_each(v, insertData, queue);
	printf("\n队列循环出队:");
	while (!XPriorityQueue_isEmpty_base(queue))
	{
		int* values = XPriorityQueue_top_base(queue);
		printf("%d ", *values);
		XPriorityQueue_pop_base(queue);
	}
	printf("\n");
	XPriorityQueue_delete_base(queue);
	XVector_delete_base(v);
#endif
}
#endif