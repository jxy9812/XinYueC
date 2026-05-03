#include"XTimerTest.h"
#include"XTimerGroupWheel.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XThread.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器1触发:%d ms\n",XTimer_getCurrentTime()-current);
	current = XTimer_getCurrentTime();

	/*XTimerTimeWheel* timer = XTimer_create();
	XTimer_setUserData(timer, userData);
	XTimer_setTimeout(timer, 5);
	XTimer_setTimerCallback(timer, Callback1);
	XTimer_start_base(timer);
	XTimerGroupBase_addTimer_base(userData, timer);*/
}
static void Callback2(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器2触发:%d ms\n", XTimer_getCurrentTime() - current);
	current = XTimer_getCurrentTime();
	//XTimer_deleteLater(userData);
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
		XTimerWheelData* timer = XTimerWheelData_create();
		XTimerData_setUserData(timer, wheel);
		XTimerData_setInterval(timer,300);
		XTimerData_setTimeout(timer, 50);
		XTimerData_setTimerCallback(timer,Callback1);
		XTimerGroupWheel_addTimer_base(wheel, timer);
	
	}
	{
		XTimerWheelData* timer = XTimerWheelData_create();

		XTimerData_setInterval(timer, 20);
		XTimerData_setTimeout(timer, 15);
		XTimerData_setTimerCallback(timer, Callback2);
		XTimerData_setUserData(timer, timer);
		XTimerGroupWheel_addTimer_base(wheel, timer);
		
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