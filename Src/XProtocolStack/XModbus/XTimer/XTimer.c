#include"XTimer.h"
#include"XMemory.h"
#include<string.h>
XTimer* XTimer_new(XTimerCreate create)
{
	if (create == NULL)
		return NULL;
	XTimer* timer = XMemory_malloc(sizeof(XTimer));
	if (timer == NULL)
		return;
	memset(timer,0, sizeof(XTimer));
	timer->remainingTime = -1;
	
	create(timer);
	return timer;
}

void XTimer_free(XTimer* timer, XTimerFree free)
{
	if (timer && free)
	{
		XTimer_stop(timer);
		free(timer);
		XMemory_free(timer);
	}
}

void XTimer_start(XTimer* timer)
{
	if (timer&&timer->start)
	{
		timer->start(timer);
		timer->number = 0;
	}
}

void XTimer_stop(XTimer* timer)
{
	if (timer&&timer->stop)
	{
		timer->stop(timer);
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
	if (timer == NULL||timer->timeout==NULL)
		return;
	++timer->number;
	timer->timeout(timer);
}