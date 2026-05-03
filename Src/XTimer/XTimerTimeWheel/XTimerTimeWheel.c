#include"XTimerTimeWheel.h"
#include"XMemory.h"
#include<string.h>
XTimerTimeWheel* XTimerTimeWheel_create()
{
	XTimerTimeWheel* timer = XMemory_malloc(sizeof(XTimerTimeWheel));
	if (timer == NULL)
		return timer;
	XTimerTimeWheel_init(timer);
	Set_Class_MemoryFree(timer, XFree);
	return timer;
}

void XTimerTimeWheel_init(XTimerTimeWheel* timer)
{
	if (timer == NULL)
		return;
	//初始化父类以外的数据
	memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimerTimeWheel) - sizeof(XTimerBase));
	XTimerBase_init(timer,NULL);
	XClassGetVtable(timer) = XTimerTimeWheel_class_init();
}
