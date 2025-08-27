#include"XTimerTest.h"
#include"XTimerGroupWheel.h"
#include"XTimerWheel.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	XPrintf_utf8_fmt("定时器1触发:%d ms\n",XTimerBase_getCurrentTime()-current);
	current = XTimerBase_getCurrentTime();

	/*XTimerWheel* timer = XTimerWheel_create();
	XTimerWheel_setUserData(timer, userData);
	XTimerWheel_setTimeout_base(timer, 5);
	XTimerWheel_setTimerCallback(timer, Callback1);
	XTimerWheel_start_base(timer);
	XTimerGroupBase_addTimer_base(userData, timer);*/
}
static void Callback2(void* userData)
{
	static size_t current = 0;

	XPrintf_utf8_fmt("定时器2触发:%d ms\n", XTimerBase_getCurrentTime() - current);
	current = XTimerBase_getCurrentTime();
}

void XTimerWheelTest()
{
	XPrintf_utf8_fmt("时间轮定时器测试\n");
	XTimerGroupWheel* wheel=XTimerGroupWheel_create(1);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel, 10);
	{
		XTimerWheel* timer = XTimerWheel_create();
		XTimerWheel_setUserData(timer, wheel);
		XTimerWheel_setInterval_base(timer,99);
		XTimerWheel_setTimeout_base(timer, 5);
		XTimerWheel_setTimerCallback(timer,Callback1);
		XTimerBase_setTimerGroup(timer,wheel);
		XTimerWheel_start_base(timer);
	
	}
	{
		XTimerWheel* timer = XTimerWheel_create();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		XTimerWheel_setInterval_base(timer, 49);
		XTimerWheel_setTimeout_base(timer, 15);
		XTimerWheel_setTimerCallback(timer, Callback2);
		//XTimerWheel_start_base(timer);
	}
}

void XMenu_XTimerWheelTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XTimerWheel(时间轮定时器)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XTimerWheelTest);
	}
}