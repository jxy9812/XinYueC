#include"XTimerData.h"
#include"XMemory.h"
#include<string.h>
XTimerData* XTimerData_create(XVtable* vtable)
{
	if (vtable == NULL)
		return NULL;
	XTimerData* timer = XMalloc_System(sizeof(XTimerData));
	if (timer == NULL)
		return NULL;
	XTimerData_init(timer,vtable);
	Set_Class_MemoryFree(timer, XFree_System);
	return timer;
}

void XTimerData_init(XTimerData* timer, XVtable* vtable)
{
	if (timer == NULL)
		return;
	//开始初始化
	memset(timer, 0, sizeof(XTimerData));
	//XObject_init(timer);
	//XClassGetVtable(timer) = vtable;
	timer->m_autoDelete = false;
}

void XTimerData_delete(XTimerData* timer)
{
	if (timer)
		XFree_System(timer);
}

void XTimerData_setTimerId(XTimerData* timer, size_t timerId)
{
	if (timer)
		timer->timerId=timerId;
}
void XTimerData_setAutoDelete(XTimerData* timer, bool del)
{
	if (timer)
		timer->m_autoDelete = del;
}
void XTimerData_setSingleShot(XTimerData* timer, bool ss)
{
	if (timer)
		timer->m_isSingleShot = ss;
}
bool XTimerData_isSingleShot(XTimerData* timer)
{
	return timer?timer->m_isSingleShot:false;
}
bool XTimerData_isPeriodic(XTimerData* timer)
{
	if (timer)
		return timer->m_interval==0;
	return false;
}
size_t XTimerData_timeout(XTimerData* timer)
{
	if (timer)
		return timer->m_timeout;
	return 0;
}
size_t XTimerData_interval(XTimerData* timer)
{
	if(timer)
		return timer->m_interval;
	return 0;
}
size_t XTimerData_timerId(XTimerData* timer)
{
	if(timer)
		return timer->timerId;
	return 0;
}
void* XTimerData_userData(XTimerData* timer)
{
	if(timer)
		return timer->m_userData;
	return NULL;
}
bool XTimerData_isAutoDelete(XTimerData* timer)
{
	if (timer)
		return timer->m_autoDelete;
	return false;
}

void XTimerData_setTimerCallback(XTimerData* timer, XTimerCallback callback)
{
	timer->m_timerCallback = callback;
}

void XTimerData_setUserData(XTimerData* timer, void* userData)
{
	timer->m_userData = userData;
}
void XTimerData_setTimeout(XTimerData* timer, size_t value)
{
	timer->m_timeout = value;
}
void XTimerData_setInterval(XTimerData* timer, size_t value)
{
	timer->m_interval = value;
}
void XTimerData_out(XTimerData* timer)
{
	if (timer == NULL)
		return;
	//++timer->number;
	if (timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData,timer);

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
	if(global_getCurrentTime==NULL)
		return currentTime;
	return global_getCurrentTime();
}

void XTimer_setCurrentTimeFunc(size_t(*get)())
{
	global_getCurrentTime = get;
}
