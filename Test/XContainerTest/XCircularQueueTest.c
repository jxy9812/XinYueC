#include"XDataStructTest.h"
#if DEMOTEST
#include"XCircularQueue.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//循环队列测试
static void XCircularQueueTest();
void XCircularQueueTest()
{
#if XCircularQueue_ON
	XPrintf("循环队列 测试\n");
	XCircularQueue* queue = XCircularQueue_Create(int,5);
	int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XCircularQueue_push_base(queue, arr + i);
	}
	XCircularQueue_pop_base(queue);
	XCircularQueue_pop_base(queue);
	XCircularQueue_pop_base(queue);
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XCircularQueue_push_base(queue, arr + i);
	}
	while (!XCircularQueue_isEmpty_base(queue))
	{
		XPrintf("%d size:%d\n", XCircularQueue_Top_Base(queue, int), XCircularQueue_size_base(queue));
		XCircularQueue_pop_base(queue);
	}
	
	XCircularQueue_delete_base(queue);
	XPrintf("循环队列 空\n");
	
#else
	IS_ON_DEBUG(XStack_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XCircularQueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XCircularQueue(环形队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XCircularQueueTest);
	}
}
#endif