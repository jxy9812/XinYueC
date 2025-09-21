#include"XDataStructTest.h"
#if DEMOTEST
#include"XQueue.h"
#include"XPriorityMapQueue.h"
#include"XSort.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//队列测试
static void XPriorityMapQueueTest();
void XPriorityMapQueueTest()
{
	XPrintf("XPriorityMapQueue 测试\n");
	//while (true)
	{
		XPriorityMapQueue* queue = XPriorityMapQueue_Create(int, int, XCompare_int, XSORT_ASC);
		int p = 1;
		XPriorityMapQueue_addFifoQueue(queue,&p,10);
		//XPriorityMapQueue_removeFifoQueue(queue, &p);
		int array[] = { 0,1,2,3,4,5,6,7,8,9 };
		for (size_t i = 0; i < sizeof(array) / sizeof(array[0]); i++)
		{
			XPriorityMapQueue_addFifoQueue(queue, array + i, 10);
			XPriorityMapQueue_push_base(queue, array + i, array + i);
		}
		while (!XQueue_isEmpty_base(queue))
		{
			XPrintf("%d ", XQueue_Top_Base(queue, int));
			XQueue_pop_base(queue);
		}
		XPrintf("\n");
		XQueue_delete_base(queue);
	}
	XCoreApplication_quit();
}

void XMenu_XPriorityMapQueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XPriorityMapQueue(优先映射队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XPriorityMapQueueTest);
	}
}
#endif