#include"XDataStructTest.h"
#if DEMOTEST
#include<stdint.h>
#include"XCircularQueueAtomic.h"
#include"XThread.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
static void XCircularQueueAtomicTest();

// 线程函数 1：输出 "Thread 1 is running"
static void ThreadReceive(void* lpParam)
{
	XPrintf("线程进入\n");
	XCircularQueueAtomic* queue = lpParam;
	//int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < 10000; i++)
	{
		int n = i;
		while (!XCircularQueueAtomic_push_base(queue, &n));
		//Sleep(100);
	}

	return 0;
}

void XCircularQueueAtomicTest()
{
#if XCircularQueueAtomic_ON
	XPrintf("循环队列 测试\n");
	XCircularQueueAtomic* queue = XCircularQueueAtomic_Create(int,1000);
	XThread* thread = XThread_create(ThreadReceive,queue);
	XThread_start_base(thread);
	//threadTest(queue);
	int index = 0;
	int value;
	while (index<10000)
	{
		if (XCircularQueueAtomic_receive_base(queue, &value))
		{

			XPrintf("index:%d %d size:%d\n",index++,value ,XCircularQueueAtomic_size_base(queue));
			//XCircularQueueAtomic_pop_base(queue);
		}
	}
	XThread_wait_base(thread,UINT32_MAX);
	XThread_delete_base(thread);
	XCircularQueueAtomic_delete_base(queue);
	XPrintf("循环队列 空\n");
	
#else
	IS_ON_DEBUG(XCircularQueueAtomic_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XCircularQueueAtomicTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XCircularQueueAtomic(无锁环形队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XCircularQueueAtomicTest);
	}
}
#endif