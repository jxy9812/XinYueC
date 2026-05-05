#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XThreadPool.h"
#include"XThread.h"
#include"XTimer.h"
#include"XAtomic.h"
static void threadFunc( XVarList* list)
{
	XThread* thread = XThread_currentThread();
	//XVarList_args_1(list, XAtomic_int32_t*, rt);
	XPrintf("子线程:%p \n", XThread_currentThread());
	XTimer* timer = XTimer_create();
	//XPrintf("XTimer:thread:%p\n",((XObject*)timer)->m_thread);
	XTimer_setInterval(timer, 20);

	XTimer_setSingleShot(timer, true);
	XTimer_setAutoDelete(timer, true);
	XTimer_setTimerType(timer, XTimerType_PreciseTimer);
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), thread, XThread_quit, XConnectionType_Auto);
	XTimer_start_base(timer);

	XThread_exec(thread);
	//XPrintf("子线程:%p 任务退出\n", XThread_currentThread());
	//XThread_deleteLater(thread);
}
void XThreadPoolTest()
{
	XAtomic_int32_t* rt= XAtomic_create(int32_t);
	XThreadPool* pool = XThreadPool_create(NULL);
	XObject_connect1(pool, XSignal(XThreadPool_tasksEmpty_signal), xApp, XCoreApplication_quit, XConnectionType_Auto);
	while (true)
	{
		//XPrintf("主线程:%p\n", XThread_currentThread());
		
		//XThreadPool_setMaxThreadCount(pool,2);
		
		for (size_t i = 0; i < 1; i++)
		{
			XThreadPool_start2(pool, threadFunc, NULL, 0);
		}
	
	
		XCoreApplication_exec();
		
		//XThreadPool_deleteLater(pool);
		XCoreApplication_processEvents(XEventLoop_AllEvents);
	}
}

void XMenu_XThreadPoolTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XThreadPool(线程池)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XThreadPoolTest);
	}
}