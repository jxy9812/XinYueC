#include"XTimerWheel.h"
#include"XMemory.h"
#include<string.h>
XTimerWheel* XTimerWheel_new()
{
	XTimerWheel* timer = XMemory_malloc(sizeof(XTimerWheel));
	if (timer == NULL)
		return timer;
	XTimerWheel_init(timer);
	return timer;
}

void XTimerWheel_init(XTimerWheel* timer)
{
	if (timer == NULL)
		return;
	//初始化父类以外的数据
	memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimerWheel) - sizeof(XTimerBase));
	XTimerBase_init(timer,NULL);
	XClassGetVtable(timer) = XTimerWheel_class_init();
}