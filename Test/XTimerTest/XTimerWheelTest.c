#include"XTimerTest.h"
#include"XTimerGroupWheel.h"
#include"XTimerWheel.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	printf("定时器1触发:%d ms\n",XTimerBase_getCurrentTime()-current);
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

	printf("定时器2触发:%d ms\n", XTimerBase_getCurrentTime() - current);
	current = XTimerBase_getCurrentTime();
}

void XTimerWheelTest()
{
	printf("时间轮定时器测试\n");
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
		//XTimerGroupBase_addTimer_base(wheel, timer);
	
	}
	{
		XTimerWheel* timer = XTimerWheel_create();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		XTimerWheel_setInterval_base(timer, 49);
		XTimerWheel_setTimeout_base(timer, 15);
		XTimerWheel_setTimerCallback(timer, Callback2);
		//XTimerGroupBase_addTimer_base(wheel, timer);
		//仅从任务中删除，需要手动释放
		//XTimerGroupWheel_removeTimer_base(wheel,timer);
	}
	//XTimerGroupWheel_removeTimeWheel_base(wheel);
	/*while (true)
	{
		XTimerGroupWheel_poll_base(wheel);
	}*/
}