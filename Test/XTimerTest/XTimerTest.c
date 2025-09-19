#include"XTimerTest.h"
#include"XMenu.h"
#include"XAction.h"
#include"XTimer.h"
#include"XThread.h"
#include"XTimerGroupWheel.h"
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
}
void XTimerTest()
{
	currentTimer = XTimerBase_getCurrentTime();

	XTimer* timer = XTimer_create();
	XTimer_setInterval_base(timer, 10);
	XTimer_setTimeout_base(timer, 50);
	//XConnection* conn=XObject_connect(timer,XSignal(XTimer_timeout_signal),timer, timerSlotFunc,XConnectionType_Queued);
	/*XObject_disconnect_conn(conn);
	XObject_disconnect(timer, XTimer_timeout_signal(NULL), NULL, timerSlotFunc);*/
	XTimer_callOnTimeout(timer,NULL, timerSlotFunc, XConnectionType_Auto);
	//XTimer_singleShot(100, NULL, timerSlotFunc, XConnectionType_Auto);
	XTimer_start_base(timer);
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
