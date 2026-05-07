#include"XTimerTest.h"
#include"XTimeWheelGroup.h"
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
	XTimerGroupBase_addTimerMs_base(userData, timer);*/
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
	/*XTimeWheelGroup* wheel= XTimeWheelGroup_create(1);
	wheel= XTimeWheelGroup_create(1);
	XTimeWheelGroup_addTimeWheel(wheel,100);
	XTimeWheelGroup_addTimeWheel(wheel,10);
	XTimeWheelGroup_addTimeWheel(wheel,10);*/
	XTimeWheelGroup* wheel = XTimeWheelGroup_global();
	size_t min_time = 0, max_time = 0;
	XTimeWheelGroup_timeRange(wheel,&min_time,&max_time);
	XPrintf("定时器时间轮时间范围 %ld-%ld ms\n",min_time,max_time);
	{
		XTimerData data = { 0 };
		XTimerData_setUserData(&data, wheel);
		XTimerData_setInterval(&data,2);
		XTimerData_setTimeout(&data, 50);
		XTimerData_setTimerCallback(&data,Callback1);
		XHandle handle = XTimeWheelGroup_addTimerMs_base(wheel, data);
	
	}
	{
		XTimerData data = { 0 };

		XTimerData_setInterval(&data, 20);
		XTimerData_setTimeout(&data, 15);
		XTimerData_setTimerCallback(&data, Callback2);
		//XTimerData_setUserData(&data, timer);
		XHandle handle = XTimeWheelGroup_addTimerMs_base(wheel, data);
		
	}
	XCoreApplication_exec();
	//while(true) XTimeWheelGroup_handler_base(wheel);
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