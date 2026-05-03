#include"XTimerTest.h"
#include"XMenu.h"
#include"XAction.h"
#include"XTimer.h"
#include"XThread.h"
#include"XTimeWheelGroup.h"
#include"XEventLoop.h"
#include"XCoreApplication.h"
static size_t currentTimer = 0;
static void deleteCb(XObject* sender, XVarList* args)
{
	XPrintf("触发删除\n");
}
static void Callback(XObject* sender, XVarList* args)
{
	

	XPrintf("定时器1触发:%d ms\n", XTimer_getCurrentTime() - currentTimer);
	currentTimer = XTimer_getCurrentTime();

	/*XTimerTimeWheel* timer = XTimer_create();
	XTimer_setUserData(timer, userData);
	XTimer_setTimeout(timer, 5);
	XTimer_setTimerCallback(timer, Callback1);
	XTimer_start_base(timer);
	XTimerGroupBase_addTimer_base(userData, timer);*/
}
void XTimerTest()
{
	currentTimer = XTimer_getCurrentTime();	
	while (true)//测试内存是否泄漏
	{
		/*XCoreApplication_processEventsWithMaxTime(0, 1000);
		XPrintf("正式结束\n");
		continue;*/
		XTimer* timer = XTimer_create();
		XTimer_setInterval(timer, 100);
		//XTimer_setTimeout(timer, 5000);
		XTimer_setSingleShot(timer, true);
		XTimer_setAutoDelete(timer, true);
		XTimer_setTimerType(timer, XTimerType_PreciseTimer);
		//XConnection* conn=XObject_connect1(timer,XSignal(XTimer_timeout_signal),timer, timerSlotFunc,XConnectionType_Queued);
		//XObject_disconnect2(conn);
		//XObject_disconnect1(timer, XSignal(XTimer_timeout_signal), NULL, timerSlotFunc);
		//XTimer_callOnTimeout1(timer,NULL, timerSlotFunc, XConnectionType_Auto);
		// XTimer_start_base(timer);
		// return;
		//XTimer_singleShot1(100, NULL, timerSlotFunc, XConnectionType_Auto);
		XEventLoop* loop = XEventLoop_create();
		XTimer_callOnTimeout1(timer,  loop, XEventLoop_quit, XConnectionType_Auto);
		XObject_connect2(timer, XSignal(XTimer_timeout_signal), Callback);
		XTimer_callOnTimeout1(timer, loop, XEventLoop_deleteLater, XConnectionType_Auto);
		XObject_connect2(timer, XSignal(XObject_destroyed_signal), deleteCb);
		XTimer_start_base(timer);
		//XPrintf("事件循环等待\n");
		XEventLoop_exec(loop);
		XPrintf("事件循环结束\n");
		//XObject_setParent(timer,loop);
		
		XEventLoop_deleteLater(loop);
	/*	XTimer_deleteLater(timer);
		XPrintf("3s后正式结束\n");
		XCoreApplication_processEvents(0);
		XCoreApplication_processEventsWithMaxTime(0, 3000);
		XPrintf("正式结束\n");*/
		XCoreApplication_processEvents(0);
		XCoreApplication_processEvents(0);
	}

	XCoreApplication_quit();
}
void XMenu_XTimerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("定时器");
	XMenu_addMenu(root, menu);
	XMenu_XTimerTimeWheelTest(menu);

	{
		XMenu* tmenu = XMenu_create("XTimer(定时器)");
		XMenu_addMenu(menu, tmenu);
		{
			XAction* action = XMenu_addAction(tmenu, "主测试");
			XAction_setAction(action, XTimerTest);
		}
	}
}
