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
	XContainerSetCompare(queue,int_compare);
	XCircularQueue_setAutoExpansion(queue,true);

	for (size_t i = 0; i < 10; i++)
	{
		XCircularQueue_push_base(queue, &i);
	}
	int remove = 5;
	XCircularQueue_remove(queue,&remove,1);
	/*XCircularQueue_pop_base(queue);
	XCircularQueue_pop_base(queue);
	XCircularQueue_pop_base(queue);*/
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