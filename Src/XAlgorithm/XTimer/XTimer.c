#include"XTimer.h"
#include"XMemory.h"
#include<string.h>
XTimer* XTimer_new(XTimer_PortFuncInit* port)
{
	if(port==NULL)
		return NULL;
	XTimer* timer = XMemory_malloc(sizeof(XTimer));
	if (timer == NULL)
		return;
	//开始初始化
	memset(timer, 0, sizeof(XTimer) - sizeof(XTimer_PortFunc));
	//绑定函数指针
	memcpy(&(timer->m_port), port, sizeof(XTimer_PortFunc));
	return timer;
}
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
//#pragma comment(lib, "winmm.lib")
// 定时器回调函数
static VOID CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer)
{
	XTimer* timer = ((XTimer*)Context);
	XTimer_out(timer);
}
static void XTimerCreateWin32(XTimer* timer)
{
	//printf("创建定时器\n");
	if (timer == NULL)
		return;
	// 创建线程池计时器
	timer->timerId = CreateThreadpoolTimer(
		TimerCallback,      // 回调函数
		timer,         // 传递给回调函数的上下文
		NULL                // 使用默认环境
	);
}
static void XTimerStartWin32(XTimer* timer)
{
	//printf("启动定时器\n");
	XTimer_stop(timer);
	// 设置计时器立即启动，每1000ms触发一次
	if (timer == NULL || timer->timerId == 0)
		return;
	ULONGLONG dueTime = 0; // 立即启动
	DWORD period = timer->interval;   // 1000ms = 1秒
	DWORD tolerance = 0;   // 允许的触发时间偏差（毫秒）

	// 转换dueTime为FILETIME格式（相对于1601年1月1日的100纳秒间隔数）
	FILETIME ftDueTime;
	dueTime = -((ULONGLONG)dueTime * 10000); // 转换为100纳秒间隔
	ftDueTime.dwLowDateTime = (DWORD)(dueTime & 0xFFFFFFFF);
	ftDueTime.dwHighDateTime = (DWORD)(dueTime >> 32);

	// 启动计时器
	SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), &ftDueTime, period, tolerance);
}
static void XTimerStopWin32(XTimer* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId)
	{
		SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), NULL, 0, 0);
	}
}
static void XTimerFreeWin32(XTimer* timer)
{
	if (timer->timerId)
	{
		// 等待所有回调完成
		WaitForThreadpoolTimerCallbacks(((PTP_TIMER)(timer->timerId)), false);
		// 清理资源
		CloseThreadpoolTimer(((PTP_TIMER)(timer->timerId)));
	}
}
XTimer* XTimer_new_Win32()
{
	XTimer_PortFunc port=XTimer_PortFunc_Win32();
	XTimer* timer= XTimer_new(&port);
	return timer;
}
XTimer_PortFunc XTimer_PortFunc_Win32()
{
	XTimer_PortFunc port = { 0 };
	port.create = XTimerCreateWin32;
	port.free = XTimerFreeWin32;
	port.start = XTimerStartWin32;
	port.stop = XTimerStopWin32;
	return port;
}
#endif
void XTimer_create(XTimer* timer)
{
	if (timer != NULL && timer->m_port.create)
		timer->m_port.create(timer);
}

void XTimer_free(XTimer* timer)
{
	if (timer && timer->m_port.free)
	{
		XTimer_stop(timer);
		timer->m_port.free(timer);
		XMemory_free(timer);
	}
}

void XTimer_start(XTimer* timer)
{
	if (timer&&timer->m_port.start)
	{
		timer->m_port.start(timer);
		timer->number = 0;
	}
}

void XTimer_stop(XTimer* timer)
{
	if (timer&&timer->m_port.stop)
	{
		timer->m_port.stop(timer);
	}
}

void XTimer_setInterval(XTimer* timer,int value)
{
	if (timer == NULL /*||timer->setInterval==NULL*/)
		return;
	timer->interval=value;
	//XTimer_start(timer);
	/*if(timer->setInterval)
		timer->setInterval(timer);*/
}
void XTimer_out(XTimer* timer)
{
	if (timer == NULL)
		return;
	++timer->number;
	if(timer->m_port.timerCallback != NULL)
		timer->m_port.timerCallback(timer);
}
static size_t currentTime=0;
void XTimer_inc(size_t tick_period)
{
	currentTime += tick_period;
}

void XTimer_setCurrentTime(size_t time)
{
	currentTime = time;
}

size_t XTimer_getCurrentTime()
{
	return currentTime;
}
