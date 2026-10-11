#include"XTimerData.h"
#include"XMemory.h"
#include<string.h>
XTimerData* XTimerData_create_ex(XMemoryType memory, XVtable* vtable)
{
	if (vtable == NULL)
		return NULL;
	XTimerData* timer = XMemory_malloc(sizeof(XTimerData), memory);
	if (timer == NULL)
		return NULL;
	XTimerData_init(timer,vtable);
	Set_Class_Memory(timer, memory); Set_Class_IsHeap(timer, true);
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
		XClassDelete(timer);/* 按对象记录的分配器配对释放（栈对象仅反初始化） */
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
	if (timer)
		timer->m_timerCallback = callback;
}

void XTimerData_setUserData(XTimerData* timer, void* userData)
{
	if (timer)
		timer->m_userData = userData;
}
void XTimerData_setTimeout(XTimerData* timer, size_t value)
{
	if (timer)
		timer->m_timeout = value;
}
void XTimerData_setInterval(XTimerData* timer, size_t value)
{
	if (timer)
		timer->m_interval = value;
}
void XTimerData_out(XTimerData* timer)
{
	if (timer == NULL)
		return;
	//++timer->number;
	if (timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData, timer);

}
