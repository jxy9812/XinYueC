#include"Test.h"
#include"XQueue.h"
#include"XPriority_Queue.h"
#include"XSort.h"
#include"XLess.h"
void queueTest()
{
	XQueue* queue = XQueue_init(sizeof(int));
	int array[] = { 0,1,2,3,4,5,6,7,8,9 };
	for (size_t i = 0; i < sizeof(array)/sizeof(array[0]); i++)
	{
		XQueue_Push(queue, array + i);
	}
	while (!XQueue_empty(queue))
	{
		printf("%d ", *(int*)XQueue_front(queue));
		XQueue_pop(queue);
	}
	XQueue_free(queue);
}
static insertData(void* data ,void*args)
{
	XPriority_Queue_push(args, data);
	printf("%d ", *(int*)data);
}
void XPriority_QueueTest()
{
	XPriority_Queue* queue=XPriority_Queue_init(sizeof(int),XLess_int);
	XVector* v = XVector_init("int",sizeof(int));
	for (size_t i = 0; i < 10; i++)
	{
		XVector_push_back(v, &i);
	}
	XDerangement(XVector_begin(v),XVector_size(v), sizeof(int));
	printf("入队数据:");
	XVector_iterator_for_each(v, insertData, queue);

	printf("\n队列数据:");
	for (size_t i = 0; i < queue->object._size; i++)
	{
		int* data = (char*)queue->object._data + sizeof(int) * i;
		printf("%d ", *data);
	}

}