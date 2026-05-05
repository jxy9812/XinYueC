#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XThreadPool.h"
#include"XThread.h"
#include"XTimer.h"
#include"XAtomic.h"
#include"XTimeWheelGroup.h"
#include"XThreadData.h"
static void deleteTimer(XTimer* timer)
{
	XPrintf("XTimer:%p 释放了\n",timer);
	XTimer_deleteLater(timer);
}
static void threadFunc( XVarList* list)
{
	XThread* thread = XThread_currentThread();
	//XVarList_args_1(list, XAtomic_int32_t*, rt);
	XPrintf("子线程:%p \n", XThread_currentThread());
	XTimer* timer = XTimer_create();
	//XObject_moveToThread(timer, XThreadData_mainThread()->m_thread);
	//XPrintf("XTimer:thread:%p\n",((XObject*)timer)->m_thread);
	XTimer_setInterval(timer, 20);

	XTimer_setSingleShot(timer, true);
	XTimer_setAutoDelete(timer, true);
	XTimer_setTimerType(timer, XTimerType_PreciseTimer);
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), thread, XThread_quit, XConnectionType_Auto);
	XTimer_start_base(timer);
	
	XThread_exec(thread);
	
	XTimeWheelGroup* group = XTimeWheelGroup_global();
	//
	//XTimerData data = { 0 };
	//XTimerData_setSingleShot(&data, true);
	//XTimerData_setAutoDelete(&data, true);
	////XTimerData_setTimerId(&data, timerId);
	//XTimerData_setInterval(&data, 10);
	//XTimerData_setTimerCallback(&data, deleteTimer);
	//XTimerData_setUserData(&data, timer);
	//XTimeWheelGroup_addTimer_base(group, data);
	//XPrintf("子线程:%p 任务退出\n", XThread_currentThread());
	//XThread_deleteLater(thread);
	//XTimer_deleteLater(timer);
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
		
		for (size_t i = 0; i < 16; i++)
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