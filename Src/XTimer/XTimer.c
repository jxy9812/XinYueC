#include"XTimer.h"
#include"XCoreApplication.h"
#include"XMemory.h"
#include"XAbstractEventDispatcher.h"
#include<string.h>
static void VXTimer_start(XTimer* timer);
static void VXTimer_stop(XTimer* timer);
static void VXTimer_deinit(XTimer* timer);
static void VXObject_timerEvent(XTimer* timer, XEventTimer* event);
XVtable* XTimer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMER_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
//继承类
XVTABLE_INHERIT_XCLASS(XObject);
void* table[] = {
VXTimer_start,VXTimer_stop
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimer_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXObject_timerEvent);
	//XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll,NULL);
#if SHOWCONTAINERSIZE
	printf("XTimer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void XTimer_init(XTimer* timer)
{
	if (timer == NULL)
		return;
	memset(((XObject*)timer) + 1, 0, sizeof(XTimer) - sizeof(XObject));
	XObject_init(timer);
	XClassGetVtable(timer) = XTimer_class_init();
	timer->m_type = XTimerType_CoarseTimer;
}

void XTimer_start_base(XTimer* timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimer_Start, void(*)(XTimer*))(timer);
}

void XTimer_stop_base(XTimer * timer)
{
	if (ISNULL(timer, "") || ISNULL(XClassGetVtable(timer), ""))
		return;
	XClassGetVirtualFunc(timer, EXTimer_Stop, void(*)(XTimer*))(timer);
}

void XTimer_setTimeout(XTimer* timer, size_t value)
{
	if(timer)
		XTimerData_setTimeout(&timer->m_timerData,value);
}

void XTimer_setInterval(XTimer * timer, size_t value)
{
	if (timer)
		XTimerData_setInterval(&timer->m_timerData, value);
}

void XTimer_setUserData(XTimer * timer, void* userData)
{
	if (timer)
		XTimerData_setUserData(&timer->m_timerData, userData);
}

void XTimer_setTimerCallback(XTimer * timer, XTimerCallback callback)
{
	if (timer)
		XTimerData_setTimerCallback(&timer->m_timerData, callback);
}

void XTimer_setTimerId(XTimer * timer, size_t timerId)
{
	if (timer)
		XTimerData_setTimerId(&timer->m_timerData, timerId);
}

void XTimer_setAutoDelete(XTimer * timer, bool del)
{
	if (timer)
		XTimerData_setAutoDelete(&timer->m_timerData, del);
}

void XTimer_setSingleShot(XTimer * timer, bool ss)
{
	if (timer)
		XTimerData_setSingleShot(&timer->m_timerData, ss);
}

bool XTimer_isSingleShot(XTimer * timer)
{
	return timer?XTimerData_isSingleShot(&timer->m_timerData):false;
}

bool XTimer_isPeriodic(XTimer* timer)
{
	return timer ? XTimerData_isPeriodic(&timer->m_timerData) : false;
}

bool XTimer_isRunning(XTimer* timer)
{
	return timer ? timer->m_isRun : false;
}

size_t XTimer_timeout(XTimer* timer)
{
	return timer ? XTimerData_timeout(&timer->m_timerData) : 0;
}

size_t XTimer_interval(XTimer* timer)
{
	return timer ? XTimerData_interval(&timer->m_timerData) : 0;
}

size_t XTimer_timerId(XTimer* timer)
{
	return timer ? XTimerData_timerId(&timer->m_timerData) : 0;
}

void* XTimer_userData(XTimer* timer)
{
	return timer ? XTimerData_userData(&timer->m_timerData) : NULL;
}

bool XTimer_isAutoDelete(XTimer* timer)
{
	return timer ? XTimerData_isAutoDelete(&timer->m_timerData) : false;
}

XTimer* XTimer_create()
{
	XTimer* timer = XNew(XTimer);
	if (timer == NULL)
		return timer;
	XTimer_init(timer);
	Set_Class_MemoryFree(timer, XFree_System);
	return timer;
}

void XTimer_out(XTimer* timer)
{
	XTimerData_out(&timer->m_timerData);
}

void XTimer_setTimerType(XTimer* timer,XTimerType type)
{
	if (timer)timer->m_type = type;
}

XTimerType XTimer_timerType(XTimer* timer)
{
	return timer?timer->m_type: XTimerType_PreciseTimer;
}

void* XTimer_timeout_signal(XTimer* timer)
{
	XEmitSignal(timer, XTimer_timeout_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void XTimer_callOnTimeout1(XTimer* timer, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if (timer == 0 || slot_func == NULL)
		return;
	XObject_connect_1(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, type);
}

void XTimer_callOnTimeout2(XTimer* timer, XSlotFunc2 slot_func)
{
	if (timer == 0 || slot_func == NULL)
		return;
	XObject_connect_2(timer, XSignal(XTimer_timeout_signal), slot_func);
}

void XTimer_singleShot1(size_t msec, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout(timer, msec);
	XTimer_setAutoDelete(timer,true);
	XTimer_setSingleShot(timer, true);
	XObject_connect_1(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, type);
	XTimer_start_base(timer);
}
void XTimer_singleShot2(size_t msec, XSlotFunc1 slot_func)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout(timer, msec);
	XTimer_setAutoDelete(timer, true);
	XTimer_setSingleShot(timer, true);
	XObject_connect_2(timer, XSignal(XTimer_timeout_signal), slot_func);
	XTimer_start_base(timer);
}
void VXObject_timerEvent(XTimer* timer, XEventTimer* event)
{
	if(XTimer_timerId(timer) == event->timerId)
	{
		XTimer_out(timer);
		XTimer_timeout_signal(timer);
		if (XTimer_isSingleShot(timer))
		{
			//XPrintf("定时器触发\n");
			XTimer_stop_base(timer);
			if (XTimer_isAutoDelete(timer))
				XTimer_deleteLater(timer);
		}
		else if (!timer->m_firstTrigger)
		{
			timer->m_firstTrigger = true;//第一次触发
			XTimer_stop_base(timer);
			if (XTimer_interval(timer))
				timer->m_timerData.timerId = XObject_startTimer_ms(timer, XTimer_interval(timer), XTimer_timerType(timer));
			if (XTimer_timerId(timer))
				timer->m_isRun = true;
		}
		XEvent_accept(event);
	}
	else
	{
		XClass_Parent(XObject, EXObject_TimerEvent, void(*)(XObject*))(timer);
		//(XVtableGetFunc(XObject_class_init(), EXObject_TimerEvent, void(*)(XObject*))(timer));
	}
}
void VXTimer_deinit(XTimer* timer)
{
	//XPrintf("XTimer:%p 释放了\n", timer);
	//XPrintf("释放定时器\n");
	XTimer_stop_base(timer);
	// 释放父对象
	XClass_Deinit_Parent(XObject, timer);
}

void VXTimer_start(XTimer* timer)
{
	timer->m_firstTrigger = false;
	XTimer_stop_base(timer);
	if(timer->m_timerData.m_timeout)
		timer->m_timerData.timerId = XObject_startTimer_ms(timer, timer->m_timerData.m_timeout, XTimer_timerType(timer));
	else if(XTimer_interval(timer))
		timer->m_timerData.timerId = XObject_startTimer_ms(timer, XTimer_interval(timer), XTimer_timerType(timer));
	if(timer->m_timerData.timerId)
		timer->m_isRun = true;
}
void VXTimer_stop(XTimer* timer)
{
	//printf("停止定时器\n");
	if (timer->m_timerData.timerId && XTimer_isRunning(timer))
	{
		// 关闭定时器
		XObject_killTimer(timer, timer->m_timerData.timerId);
		//XPrintf("停止定时器: id:%d\n", timer->timerId);
		timer->m_timerData.timerId = 0;
		timer->m_isRun = false;
	}
}