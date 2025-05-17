#include"XTimer.h"
#include"XMemory.h"
#include<string.h>
XTimer* XTimer_new(XTimer_PortFunc* port)
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
	XTimer_start(timer);
	/*if(timer->setInterval)
		timer->setInterval(timer);*/
}
void XTimer_out(XTimer* timer)
{
	if (timer == NULL||timer->m_port.timeout==NULL)
		return;
	++timer->number;
	timer->m_port.timeout(timer);
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
