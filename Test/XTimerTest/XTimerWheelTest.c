#include"XTimerTest.h"
#include"XTimerGroupWheel.h"
#include"XTimerWheel.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	printf("定时器1触发:%d ms\n",XTimerBase_getCurrentTime()-current);
	current = XTimerBase_getCurrentTime();
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
	XTimerGroupWheel* wheel=XTimerGroupWheel_new(1);
	XTimerGroupWheel_addTimeWheel_base(wheel,10);
	/*XTimerGroupWheel_addTimeWheel_base(wheel,10);
	XTimerGroupWheel_addTimeWheel_base(wheel, 10);*/
	{
		XTimerWheel* timer = XTimerWheel_new();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		parentTimer->m_interval = 5;
		parentTimer->m_timeout = 5;
		parentTimer->m_timerCallback = Callback1;
		XTimerGroupBase_addTimer_base(wheel, timer);
	}
	{
		XTimerWheel* timer = XTimerWheel_new();
		XTimerBase* parentTimer = (XTimerBase*)timer;
		parentTimer->m_interval = 2;
		parentTimer->m_timeout = 5;
		parentTimer->m_timerCallback = Callback2;
		XTimerGroupBase_addTimer_base(wheel, timer);
		//仅从任务中删除，需要手动释放
		XTimerGroupWheel_removeTimer_base(wheel,timer);
	}
	//XTimerGroupWheel_removeTimeWheel_base(wheel);
	while (true)
	{
		XTimerGroupWheel_poll_base(wheel);
	}
}