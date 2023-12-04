#include"Test.h"
#include"XQueue.h"
#include"XPriority_Queue.h"
#include"XSort.h"
#include"XLess.h"
#include"XGreater.h"
#include"XVector.h"
void queueTest()
{
	XQueue* queue = XQueue_Init(int);
	int array[] = { 0,1,2,3,4,5,6,7,8,9 };
	for (size_t i = 0; i < sizeof(array)/sizeof(array[0]); i++)
	{
		XQueue_push(queue, array + i);
	}
	while (!XQueue_empty(queue))
	{
		printf("%d ", XQueue_Front(queue,int));
		XQueue_pop(queue);
	}
	XQueue_free(queue);
}
static insertData(void* values ,void*args)
{
	XPriority_Queue_push(args, values);
	printf("入队:%d 堆顶:%d\n", *(int*)values, *(int*)XPriority_Queue_top(args));
}
void XPriority_QueueTest()
{
	//XPriority_Queue* queue=XPriority_Queue_init(sizeof(int),XLess_int);//小堆，先出小的
	XPriority_Queue* queue = XPriority_Queue_init(sizeof(int), XGreater_int);//大堆，先出大的
	XVector* v = XVector_init(sizeof(int));
	for (size_t i = 0; i < 10; i++)
	{
		XVector_push_back(v, &i);
	}
	XDerangement(XVector_begin(v),XVector_size(v), sizeof(int));
	//printf("入队数据:");
	XVector_iterator_for_each(v, insertData, queue);
	printf("\n队列循环出队:");
	while (!XPriority_Queue_empty(queue))
	{
		int* values = XPriority_Queue_top(queue);
		printf("%d ", *values);
		XPriority_Queue_pop(queue);
	}
	XPriority_Queue_free(queue);

}