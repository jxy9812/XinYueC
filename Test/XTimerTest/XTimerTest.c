#include"XTimerTest.h"
#include"XMenu.h"
#include"XAction.h"
#include"XTimer.h"
#include"XThread.h"
#include"XTimerGroupWheel.h"
#include"XEventLoop.h"
#include"XCoreApplication.h"
static size_t currentTimer = 0;
static void Callback(void* userData)
{
	

	XPrintf("定时器1触发:%d ms\n", XTimerBase_getCurrentTime() - currentTimer);
	currentTimer = XTimerBase_getCurrentTime();

	/*XTimerTimeWheel* timer = XTimerTimeWheel_create();
	XTimerTimeWheel_setUserData(timer, userData);
	XTimerTimeWheel_setTimeout_base(timer, 5);
	XTimerTimeWheel_setTimerCallback(timer, Callback1);
	XTimerTimeWheel_start_base(timer);
	XTimerGroupBase_addTimer_base(userData, timer);*/
}
static void  timerSlotFunc(XObject* sender, XObject* receiver, void* args)
{
	Callback(NULL);
	XEventLoop_quit_base(receiver,0);
}
void XTimerTest()
{
	currentTimer = XTimerBase_getCurrentTime();	
	while (true)//测试内存是否泄漏
	{
		XTimer* timer = XTimer_create();
		XTimer_setInterval_base(timer, 10);
		XTimer_setTimeout_base(timer, 50);
		XTimerBase_setSingleShote(timer, true);
	
		//XConnection* conn=XObject_connect(timer,XSignal(XTimer_timeout_signal),timer, timerSlotFunc,XConnectionType_Queued);
		//XObject_disconnect_conn(conn);
		//XObject_disconnect(timer, XSignal(XTimer_timeout_signal), NULL, timerSlotFunc);
		//XTimer_callOnTimeout(timer,NULL, timerSlotFunc, XConnectionType_Auto);
		//XTimer_singleShot(100, NULL, timerSlotFunc, XConnectionType_Auto);
		XEventLoop* loop = XEventLoop_create();
		XObject_connect(timer, XSignal(XTimer_timeout_signal), loop, timerSlotFunc, XConnectionType_Auto);
		XTimer_start_base(timer);
		XPrintf("事件循环等待\n");
		XEventLoop_exec_base(loop);
		XPrintf("事件循环结束\n");
		
		XEventLoop_delete_base(loop);
		//XTimer_delete_base(timer);
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
