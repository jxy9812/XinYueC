#include"XDataStructTest.h"
#if DEMOTEST
#include"XQueue.h"
#include"XPriorityQueue.h"
#include"XSort.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//队列测试
static void XQueueTest();
//优先队列
static void XPriorityQueueTest();
void XQueueTest()
{
#if	XQueue_ON
	XPrintf("XQueue 测试\n");
	XQueue* queue = XQueue_Create(int);
	int array[] = { 0,1,2,3,4,5,6,7,8,9 };
	for (size_t i = 0; i < sizeof(array)/sizeof(array[0]); i++)
	{
		XQueue_push_base(queue, array + i);
	}
	while (!XQueue_isEmpty_base(queue))
	{
		XPrintf("%d ", XQueue_Top_Base(queue,int));
		XQueue_pop_base(queue);
	}
	XPrintf("\n");
	XQueue_delete_base(queue);
#endif
	XCoreApplication_quit();
}
#if	XPriorityQueue_ON
static insertData(void* value ,void*args)
{
	XPriorityQueue_push_base(args, value);
	XPrintf("入队:%d 堆顶:%d\n", *(int*)value, *(int*)XPriorityQueue_top_base(args));
}
#endif
void XPriorityQueueTest()
{
#if	XPriorityQueue_ON
	XPrintf("XPriority_QueueTest 测试\n");
	//XPriorityQueue* queue=XPriorityQueue_create(sizeof(int),XLess_int);//小堆，先出小的
	XPriorityQueue* queue = XPriorityQueue_create(sizeof(int), XCompare_int,XSORT_DESC);//大堆，先出大的
	XVector* v = XVector_Create(int);
	for (size_t i = 0; i < 10; i++)
	{
		XVector_push_back_base(v, &i);
	}
	XDerangement(XContainerDataPtr(v),XVector_size_base(v), sizeof(int));
	//XPrintf("入队数据:");
	XVector_iterator_for_each(v, insertData, queue);
	XPrintf("\n队列循环出队:");
	while (!XPriorityQueue_isEmpty_base(queue))
	{
		int* value = XPriorityQueue_top_base(queue);
		XPrintf("%d ", *value);
		XPriorityQueue_pop_base(queue);
	}
	XPrintf("\n");
	XPriorityQueue_delete_base(queue);
	XVector_delete_base(v);
#endif
	XCoreApplication_quit();
}
void XMenu_XQueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XQueue(普通队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XQueueTest);
	}
}
void XMenu_XPriorityQueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XPriorityQueue(优先队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XPriorityQueueTest);
	}
}
#endif