#include"XDataStructTest.h"
#if DEMOTEST
#include<stdint.h>
#include"XLockFreeQueue.h"
#include"XThread.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
static void XLockFreeQueueTest();

// 线程函数 1：输出 "Thread 1 is running"
static void ThreadReceive(XThread* thread, XVarList* list)
{
	XVarList_args_3(list, XLockFreeQueue*, queue, XLockFreeQueue*, vqueue, XAtomic_int32_t*, count);
	XHandle id = XThread_currentThreadId();
	int value;
	int index = 0;
	while (!XLockFreeQueue_isEmpty_base(queue))
	{
		if (XLockFreeQueue_receive_base(queue, &value))
		{
			while (!XLockFreeQueue_push_base(vqueue, &value));
			//XPrintf("XThread:%p count:%d value:%d size:%d\n", id, index++, value, XLockFreeQueue_size_base(queue));
		}
	}
	value = XAtomic_fetch_sub_int32(count, 1, XAtomic_MemoryOrder_Relaxed);
	if (value <= 1)
	{
		XCoreApplication* app = xApp;
		XEventLoop* l = NULL;
		while (true)
		{
			XThread* t = ((XObject*)app)->m_thread;
			if (!t)continue;
			l = t->m_loop;
			if (!l)continue;
			break;
		}

		while (l->m_state != XEventLoop_Running);
		XThread_deleteLater(thread);
		XCoreApplication_quit();
	}

	return 0;
}

void XLockFreeQueueTest()
{
#if XLockFreeQueue_ON
	XPrintf("循环队列 测试\n");
	XLockFreeQueue* queue = XLockFreeQueue_Create(int,10000),*vqueue= XLockFreeQueue_Create(int, 10000);
	XAtomic_int32_t active_consumer_count = { 0 },*lpcount=&active_consumer_count;
	for (size_t i = 0; i < 10000; i++)
	{
		int n = i;
		while (!XLockFreeQueue_push_base(queue, &n));
		//Sleep(100);
	}
	for (size_t i = 0; i < 16; i++)
	{
		XThread* thread = XThread_create_func(ThreadReceive, XVarList_Create(XVar(XLockFreeQueue*, queue), XVar(XLockFreeQueue*, vqueue), XVar(XAtomic_int32_t*, lpcount)));
		XAtomic_fetch_add_int32(&active_consumer_count, 1, XAtomic_MemoryOrder_Relaxed);
		if (!XThread_start(thread))
		{
			XThread_deleteLater(thread);
			int value = XAtomic_fetch_sub_int32(&active_consumer_count, 1, XAtomic_MemoryOrder_Relaxed);

		}
	}

	XCoreApplication_exec();
	
	/*XThread_wait(thread,UINT32_MAX);
	XThread_deleteLater(thread);*/
	XLockFreeQueue_delete_base(queue);
	int value;
	int index = 0;
	while (!XLockFreeQueue_isEmpty_base(vqueue))
	{
		if (XLockFreeQueue_receive_base(vqueue, &value))
		{
			XPrintf("count:%d value:%d size:%d\n", index++, value, XLockFreeQueue_size_base(vqueue));
		}
	}
	XLockFreeQueue_delete_base(vqueue);
	
	XPrintf("循环队列 空\n");


	
#else
	IS_ON_DEBUG(XLockFreeQueue_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XLockFreeQueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XLockFreeQueue(无锁环形队列)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XLockFreeQueueTest);
	}
}
#endif