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
	//XTimerTimeWheel_deleteLater(userData);
}

void XTimerTimeWheelTest()
{
	XPrintf("时间轮定时器测试\n");
	/*XTimerGroupWheel* wheel= XTimerGroupWheel_create(1);
	wheel= XTimerGroupWheel_create(1);
	XTimerGroupWheel_addTimeWheel_base(wheel,100);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);*/
	XTimerGroupWheel* wheel = XTimerGroupWheel_global();
	size_t min_time = 0, max_time = 0;
	XTimerGroupWheel_timeRange(wheel,&min_time,&max_time);
	XPrintf("定时器时间轮时间范围 %ld-%ld ms\n",min_time,max_time);
	{
		XTimerTimeWheel* timer = XTimerTimeWheel_create();
		XTimerTimeWheel_setUserData(timer, wheel);
		XTimerTimeWheel_setInterval(timer,2000);
		XTimerTimeWheel_setTimeout(timer, 50);
		XTimerTimeWheel_setTimerCallback(timer,Callback1);
		XTimerTimeWheel_setGroup(timer, wheel);
		XTimerTimeWheel_start_base(timer);
	
	}
	{
		XTimerTimeWheel* timer = XTimerTimeWheel_create();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		XTimerTimeWheel_setInterval(timer, 20);
		XTimerTimeWheel_setTimeout(timer, 15);
		XTimerTimeWheel_setTimerCallback(timer, Callback2);
		XTimerTimeWheel_setUserData(timer, timer);
		XObject_setParent(timer, wheel);
		XTimerTimeWheel_start_base(timer);
		
	}
	XCoreApplication_exec();
	//while(true) XTimerGroupWheel_handler_base(wheel);
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