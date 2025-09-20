#include"XTimerBase.h"
#include"XMemory.h"
#include<string.h>
void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
void VXTimerBase_setUserData(XTimerBase* timer, void* userData);
void VXTimerBase_setTimeout(XTimerBase* timer, size_t value);
void VXTimerBase_setInterval(XTimerBase* timer, size_t value);
void VXTimerBase_out(XTimerBase* timer);
XTimerBase* XTimerBase_create(XVtable* vtable)
{
	if (vtable == NULL)
		return NULL;
	XTimerBase* timer = XMemory_malloc(sizeof(XTimerBase));
	if (timer == NULL)
		return NULL;
	XTimerBase_init(timer,vtable);
	return timer;
}

void XTimerBase_init(XTimerBase* timer, XVtable* vtable)
{
	if (timer == NULL)
		return;
	//开始初始化
	memset(timer, 0, sizeof(XTimerBase));
	XObject_init(timer);
	XClassGetVtable(timer) = vtable;
	timer->m_autoDelete = true;
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
}

void XTimerBase_setTimeout_base(XTimerBase* timer, size_t value)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_SetTimeOut, void(*)(XTimerBase*, size_t))(timer, value);
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
void XTimerBase_setUserData_base(XTimerBase* timer, void* userData)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_SetUserData, void(*)(XTimerBase*, void*))(timer, userData);
}
void XTimerBase_setTimerCallback_base(XTimerBase* timer, XTimerBaseCallback callback)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_SetTimerCallback, void(*)(XTimerBase*, XTimerBaseCallback))(timer, callback);
}
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId)
{
	if (timer)
		timer->timerId=timerId;
}
void XTimerBase_setAutoDelete(XTimerBase* timer, bool del)
{
	if (timer)
		timer->m_autoDelete = del;
}
void XTimerBase_setSingleShote(XTimerBase* timer, bool ss)
{
	if (timer)
		timer->m_singleShot = ss;
}
void XTimerBase_setTimerGroup(XTimerBase* timer, XTimerGroupBase* group)
{
	if (timer)
		timer->m_timerGroup = group;
}
bool XTimerBase_isPeriodic(XTimerBase* timer)
{
	if (timer)
		return timer->m_interval==0;
	return false;
}
bool XTimerBase_isRunning(XTimerBase* timer)
{
	if (timer)
		return timer->m_isRun;
	return false;
}
size_t XTimerBase_getTimeout(XTimerBase* timer)
{
	if (timer)
		return timer->m_timeout;
	return 0;
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
bool XTimerBase_isAutoDelete(XTimerBase* timer)
{
	if (timer)
		return timer->m_autoDelete;
	return false;
}
XTimerGroupBase* XTimerBase_getTimerGroup(XTimerBase* timer)
{
	if (timer)
		return timer->m_timerGroup;
	return NULL;
}
void XTimerBase_out_base(XTimerBase* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimerBase_Out, void(*)(XTimerBase*))(timer);
}
void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback)
{
	timer->m_timerCallback = callback;
}

void VXTimerBase_setUserData(XTimerBase* timer, void* userData)
{
	timer->m_userData = userData;
}

void VXTimerBase_setTimeout(XTimerBase* timer, size_t value)
{
	timer->m_timeout = value;
}

void VXTimerBase_setInterval(XTimerBase* timer, size_t value)
{
	timer->m_interval = value;
}
void VXTimerBase_out(XTimerBase* timer)
{
	if (timer == NULL)
		return;
	++timer->number;
	if (timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData);
}
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
static size_t(*global_getCurrentTime)() = GetCurrentTimeMillis;
static void(*global_delay_ms)(size_t msec)=(void*)Sleep;
#elif defined(__linux__) 
#include <sys/time.h>
#include <unistd.h>
static size_t get_milliseconds() 
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (size_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}
static size_t(*global_getCurrentTime)() = get_milliseconds;
static void(*global_delay_ms)(size_t msec)=(void*)sleep;
#elif defined(__FreeRTOS__) 
#include"FreeRTOS.h"
#include"task.h"
static size_t GetCurrentTime()
{
#if (configTICK_RATE_HZ % 1000 == 0)
	// 优化：当configTICK_RATE_HZ是1000的整数倍时（如1000Hz、2000Hz）
	return xTaskGetTickCount() / (configTICK_RATE_HZ / 1000);
#else
	// 通用情况：避免浮点运算，使用整数除法
	return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ;
#endif
}
static void  Delay_ms(const size_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}
static size_t(*global_getCurrentTime)() = GetCurrentTime;
static void(*global_delay_ms)(size_t) = vTaskDelay;
#else
static size_t(*global_getCurrentTime)() = NULL;
static void(*global_delay_ms)(size_t)=NULL;
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
	if(global_getCurrentTime==NULL)
		return currentTime;
	return global_getCurrentTime();
}

void XTimerBase_setCurrentTimeFunc(size_t(*get)())
{
	global_getCurrentTime = get;
}

void XTimerBase_setDelayMsFunc(void(*delay)(const size_t msec))
{
	global_delay_ms = delay;
}

void XTimerBase_delay_ms(const size_t delay_ms)
{
	if (global_delay_ms)
		global_delay_ms(delay_ms);
}
