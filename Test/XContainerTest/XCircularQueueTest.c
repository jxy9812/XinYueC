#include"XDataStructTest.h"
#if DEMOTEST
#include"XCircularQueue.h"
void XCircularQueueTest()
{
#if XCircularQueue_ON
	printf("循环队列 测试\n");
	XCircularQueue* queue = XCircularQueue_New(int,5);
	int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XCircularQueue_push(queue, arr + i);
	}
	XCircularQueue_pop(queue);
	XCircularQueue_pop(queue);
	XCircularQueue_pop(queue);
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XCircularQueue_push(queue, arr + i);
	}
	while (!XCircularQueue_isEmpty(queue))
	{
		printf("%d size:%d\n", XCircularQueue_Top(queue, int), XCircularQueue_size(queue));
		XCircularQueue_pop(queue);
	}
	
	XCircularQueue_free(queue);
	printf("循环队列 空\n");
	
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

#endif