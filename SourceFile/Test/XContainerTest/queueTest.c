#include"Test.h"
#include"XQueue.h"
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