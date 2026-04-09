#include"XTimerTest.h"
#include"XTimerGroupWheel.h"
#include"XTimerTimeWheel.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XThread.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器1触发:%d ms\n",XTimerBase_getCurrentTime()-current);
	current = XTimerBase_getCurrentTime();

	/*XTimerTimeWheel* timer = XTimerTimeWheel_create();
	XTimerTimeWheel_setUserData(timer, userData);
	XTimerTimeWheel_setTimeout(timer, 5);
	XTimerTimeWheel_setTimerCallback(timer, Callback1);
	XTimerTimeWheel_start_base(timer);
	XTimerGroupBase_addTimer_base(userData, timer);*/
}
static void Callback2(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器2触发:%d ms\n", XTimerBase_getCurrentTime() - current);
	current = XTimerBase_getCurrentTime();
}

void XTimerTimeWheelTest()
{
	XPrintf("时间轮定时器测试\n");
	XTimerGroupWheel* wheel= XTimerGroupWheel_create(1);
	//wheel= XTimerGroupWheel_create(1);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	{
		XTimerTimeWheel* timer = XTimerTimeWheel_create();
		XTimerTimeWheel_setUserData(timer, wheel);
		XTimerTimeWheel_setInterval(timer,2);
		XTimerTimeWheel_setTimeout(timer, 5);
		XTimerTimeWheel_setTimerCallback(timer,Callback1);
		XObject_setParent(timer, wheel);
		XTimerTimeWheel_start_base(timer);
	
	}
	{
		XTimerTimeWheel* timer = XTimerTimeWheel_create();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		XTimerTimeWheel_setInterval(timer, 49);
		XTimerTimeWheel_setTimeout(timer, 15);
		XTimerTimeWheel_setTimerCallback(timer, Callback2);
		XObject_setParent(timer, wheel);
		XTimerTimeWheel_start_base(timer);
		XTimerBase_delete_base(timer);
	}
	while(true) XTimerGroupWheel_handler_base(wheel);
}

void XMenu_XTimerTimeWheelTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XTimerTimeWheel(时间轮定时器)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XTimerTimeWheelTest);
	}
}