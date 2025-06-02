#include"XTimerBase.h"
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


void XTimerBase_free_base(XTimerBase* timer)
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

void XTimerBase_start_base(XTimerBase* timer)
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

void XTimerBase_stop_base(XTimerBase* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_Stop, void(*)(XTimerBase*))(timer);
	/*if (timer&&timer->m_port.stop)
	{
		timer->m_port.stop(timer);
	}*/
}

void XTimerBase_setInterval_base(XTimerBase* timer, size_t value)
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
size_t XTimerBase_getInterval(XTimerBase* timer)
{
	if(timer)
		return timer->m_interval;
	return 0;
}
size_t XTimerBase_getTimerId(XTimerBase* timer)
{
	if(timer)
		return timer->timerId;
	return 0;
}
void* XTimerBase_getUserData(XTimerBase* timer)
{
	if(timer)
		return timer->m_userData;
	return NULL;
}
void XTimerBase_out(XTimerBase* timer)
{
	if (timer == NULL)
		return;
	++timer->number;
	if(timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData);
}
//
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 获取当前毫秒级时间戳（自1970-01-01 00:00:00 UTC）
static size_t GetCurrentTimeMillis() {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);

	// 将FILETIME转换为64位整数（100纳秒间隔数）
	ULARGE_INTEGER uli;
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;

	// 1601年到1970年的偏移量（100纳秒间隔数）
	const size_t EPOCH_OFFSET = 116444736000000000LL;

	// 转换为毫秒（除以10,000）
	return (uli.QuadPart - EPOCH_OFFSET) / 10000;
}
static size_t(*getCurrentTime)() = GetCurrentTimeMillis;
#else
static size_t(*getCurrentTime)() = NULL;

#endif

static size_t currentTime = 0;
void XTimerBase_inc(size_t tick_period)
{
	currentTime += tick_period;
}

void XTimerBase_setCurrentTime(size_t time)
{
	currentTime = time;
}

size_t XTimerBase_getCurrentTime()
{
	if(getCurrentTime==NULL)
		return currentTime;
	return getCurrentTime();
}

void XTimerBase_setCurrentTimeFunc(size_t(*get)())
{
	getCurrentTime = get;
}
