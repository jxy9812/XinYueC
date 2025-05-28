#include"XTimer.h"
#include"XMemory.h"
#include<string.h>

XTimerBase* XTimerBase_new(XVtable* vtable)
{
	if (vtable == NULL)
		return NULL;
	XTimerBase* timer = XMemory_malloc(sizeof(XTimerBase));
	if (timer == NULL)
		return NULL;
	//开始初始化
	memset(timer, 0, sizeof(XTimerBase));
	XClassGetVtable(timer) = vtable;
	return timer;
}
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 定时器回调函数
static VOID CALLBACK TimerCallbackThreadpoolTimer(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer)
{
	XTimerBase* timer = ((XTimerBase*)Context);
	XTimer_out(timer);
}
static void XTimerCreateWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("创建定时器\n");
	if (timer == NULL)
		return;
	// 创建线程池计时器
	timer->timerId = CreateThreadpoolTimer(
		TimerCallbackThreadpoolTimer,      // 回调函数
		timer,         // 传递给回调函数的上下文
		NULL                // 使用默认环境
	);
}
static void XTimerStartWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stopBase(timer);
	// 设置计时器立即启动，每1000ms触发一次
	if (timer == NULL || timer->timerId == 0)
		return;
	ULONGLONG dueTime = 0; // 立即启动
	DWORD period = timer->m_interval;   // 1000ms = 1秒
	DWORD tolerance = 0;   // 允许的触发时间偏差（毫秒）

	// 转换dueTime为FILETIME格式（相对于1601年1月1日的100纳秒间隔数）
	FILETIME ftDueTime;
	dueTime = -((ULONGLONG)dueTime * 10000); // 转换为100纳秒间隔
	ftDueTime.dwLowDateTime = (DWORD)(dueTime & 0xFFFFFFFF);
	ftDueTime.dwHighDateTime = (DWORD)(dueTime >> 32);

	// 启动计时器
	SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), &ftDueTime, period, tolerance);
}
static void XTimerStopWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId)
	{
		SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), NULL, 0, 0);
	}
}
static void XTimerFreeWin32ThreadpoolTimer(XTimerBase* timer)
{
	if (timer->timerId)
	{
		// 等待所有回调完成
		WaitForThreadpoolTimerCallbacks(((PTP_TIMER)(timer->timerId)), false);
		// 清理资源
		CloseThreadpoolTimer(((PTP_TIMER)(timer->timerId)));
	}
}
static void XTimerSetIntervalWin32ThreadpoolTimer(XTimerBase* timer, size_t value)
{
	timer->m_interval = value;
	if (XTimerBase_isRunning(timer))
	{
		XTimerBase_startBase(timer);
	}
}
XTimerBase* XTimer_new_Win32ThreadpoolTimer()
{
	static XVtable* XTimerBaseVtable = NULL;
	if (XTimerBaseVtable != NULL)
	{
		XTimerBase* timer=XTimerBase_new(XTimerBaseVtable);
		XTimerCreateWin32ThreadpoolTimer(timer);
		return timer;
	}
#if VTABLE_ISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[XTIMERBASE_VTABLE_SIZE];//虚函数数据
	XVtable_init_stack(&vtable, vtable_data, XTIMERBASE_VTABLE_SIZE);
	XTimerBaseVtable = &vtable;
#else
	XTimerBaseVtable = XVtable_new();
#endif
	void* table[] = { XTimerFreeWin32ThreadpoolTimer,XTimerStartWin32ThreadpoolTimer,XTimerStopWin32ThreadpoolTimer,XTimerSetIntervalWin32ThreadpoolTimer };
	XVtable_append_array(XTimerBaseVtable, table, sizeof(table) / sizeof(table[0]));

	XTimerBase* timer = XTimerBase_new(XTimerBaseVtable);
	XTimerCreateWin32ThreadpoolTimer(timer);
	return timer;
}

// 定时器回调函数
static void CALLBACK TimerCallbackTimeSetEvent(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	XTimerBase* timer = ((XTimerBase*)dwUser);
	XTimer_out(timer);
}
static void XTimerCreateWin32TimeSetEvent(XTimerBase* timer)
{
	//printf("创建定时器\n");
	if (timer == NULL)
		return;
}
static void XTimerStartWin32TimeSetEvent(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stopBase(timer);
	timer->timerId = timeSetEvent(
		timer->m_interval,           // 触发间隔毫秒
		1,             // 精度1毫秒
		TimerCallbackTimeSetEvent, // 回调函数
		timer,             // 不传递用户数据
		TIME_PERIODIC  // 周期性触发
	);
}
static void XTimerStopWin32TimeSetEvent(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId)
	{
		// 关闭定时器
		timeKillEvent(timer->timerId);
		timer->timerId = 0;
	}
}
static void XTimerFreeWin32TimeSetEvent(XTimerBase* timer)
{
	XTimerStopWin32TimeSetEvent(timer);
}
void XTimerSetIntervalWin32TimeSetEvent(XTimerBase* timer, size_t value)
{
	timer->m_interval = value;
	if(XTimerBase_isRunning(timer))
	{
		XTimerBase_startBase(timer);
	}
}
XTimerBase* XTimer_new_Win32TimeSetEvent()
{
	static XVtable* XTimerBaseVtable = NULL;
	if (XTimerBaseVtable!=NULL)
		return XTimerBase_new(XTimerBaseVtable);
#if VTABLE_ISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[XTIMERBASE_VTABLE_SIZE];//虚函数数据
	XVtable_init_stack(&vtable, vtable_data, XTIMERBASE_VTABLE_SIZE);
	XTimerBaseVtable = &vtable;
#else
	XTimerBaseVtable = XVtable_new();
#endif
	void* table[] = { XTimerFreeWin32TimeSetEvent,XTimerStartWin32TimeSetEvent,XTimerStopWin32TimeSetEvent,XTimerSetIntervalWin32TimeSetEvent };
	XVtable_append_array(XTimerBaseVtable, table, sizeof(table) / sizeof(table[0]));
	return XTimerBase_new(XTimerBaseVtable);
}
#endif

void XTimerBase_freeBase(XTimerBase* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_Free, void(*)(XTimerBase*))(timer);
	/*if (timer && timer->m_port.free)
	{
		XTimer_stop(timer);
		timer->m_port.free(timer);
		XMemory_free(timer);
	}*/
}

void XTimerBase_startBase(XTimerBase* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_Start, void(*)(XTimerBase*))(timer);
	/*if (timer&&timer->m_port.start)
	{
		timer->m_port.start(timer);
		timer->number = 0;
	}*/
}

void XTimerBase_stopBase(XTimerBase* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_Stop, void(*)(XTimerBase*))(timer);
	/*if (timer&&timer->m_port.stop)
	{
		timer->m_port.stop(timer);
	}*/
}

void XTimerBase_setIntervalBase(XTimerBase* timer, size_t value)
{
	if (ISNULL(timer, "")|| ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_SetInterval, void(*)(XTimerBase*, size_t))(timer, value);
	//if (timer == NULL /*||timer->setInterval==NULL*/)
	//	return;
	//timer->m_interval=value;
	//XTimer_start(timer);
	/*if(timer->setInterval)
		timer->setInterval(timer);*/
}
void XTimerBase_setUserData(XTimerBase* timer, void* userData)
{
	if (timer)
		timer->m_userData = userData;
}
void XTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback)
{
	if (timer)
		timer->m_timerCallback = callback;
}
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId)
{
	if (timer)
		timer->timerId=timerId;
}
bool XTimerBase_isPeriodic(XTimerBase* timer)
{
	if (timer)
		return timer->m_isPeriodic;
	return false;
}
bool XTimerBase_isRunning(XTimerBase* timer)
{
	if (timer)
		return timer->m_isRun;
	return false;
}
size_t XTimerBase_interval(XTimerBase* timer)
{
	if(timer)
		return timer->m_interval;
	return 0;
}
size_t XTimerBase_timerId(XTimerBase* timer)
{
	if(timer)
		return timer->timerId;
	return 0;
}
void* XTimerBase_userData(XTimerBase* timer)
{
	if(timer)
		return timer->m_userData;
	return NULL;
}
void XTimer_out(XTimerBase* timer)
{
	if (timer == NULL)
		return;
	++timer->number;
	if(timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData);
}
static size_t(*getCurrentTime)()=NULL;
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
	if(getCurrentTime==NULL)
		return currentTime;
	return getCurrentTime();
}

void XTimer_setCurrentTimeFunc(size_t(*get)())
{
	getCurrentTime = get;
}
