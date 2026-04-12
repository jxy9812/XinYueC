#include"XTimer.h"
#include"XCoreApplication.h"
#include"XMemory.h"
#include"XAbstractEventDispatcher.h"
#include<string.h>
static void VXTimerBase_start(XTimerBase* timer);
static void VXTimerBase_stop(XTimerBase* timer);
static void VXTimerBase_deinit(XTimerBase* timer);
static void VXObject_timerEvent(XTimerBase* timer, XTimerEvent* event);
XVtable* XTimer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMERBASE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
//继承类
XVTABLE_INHERIT_DEFAULT(XObject_class_init());
void* table[] = {
VXTimerBase_start,VXTimerBase_stop
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXObject_timerEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll,NULL);
#if SHOWCONTAINERSIZE
	printf("XTimer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void XTimer_init(XTimer* timer)
{
	if (timer == NULL)
		return;
	memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimer) - sizeof(XTimerBase));
	XTimerBase_init(timer,NULL);
	XClassGetVtable(timer) = XTimer_class_init();
	timer->m_type = XTimerType_CoarseTimer;
}

XTimer* XTimer_create()
{
	XTimer* timer = XNew(XTimer);
	if (timer == NULL)
		return timer;
	XTimer_init(timer);
	SET_CLASS_HEAP(timer);
	return timer;
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
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, type);
}

void XTimer_callOnTimeout2(XTimer* timer, XSlotFunc2 slot_func)
{
	if (timer == 0 || slot_func == NULL)
		return;
	XObject_connect2(timer, XSignal(XTimer_timeout_signal), slot_func);
}

void XTimer_singleShot1(size_t msec, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout(timer, msec);
	XTimerBase_setAutoDelete(timer,true);
	XTimerBase_setSingleShot(timer, true);
	XObject_connect1(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, type);
	XTimer_start_base(timer);
}
void XTimer_singleShot2(size_t msec, XSlotFunc1 slot_func)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout(timer, msec);
	XTimerBase_setAutoDelete(timer, true);
	XTimerBase_setSingleShot(timer, true);
	XObject_connect2(timer, XSignal(XTimer_timeout_signal), slot_func);
	XTimer_start_base(timer);
}
void VXObject_timerEvent(XTimerBase* timer, XTimerEvent* event)
{
	if(timer->timerId==event->timerId)
	{
		XTimer_out(timer);
		XTimer_timeout_signal(timer);
		if (timer->m_isSingleShot)
		{
			//XPrintf("定时器触发\n");
			XTimer_stop_base(timer);
			if (timer->m_autoDelete)
				XTimer_delete_base(timer);
		}
		else if (!timer->m_firstTrigger)
		{
			timer->m_firstTrigger = true;//第一次触发
			XTimer_stop_base(timer);
			if (timer->m_interval)
				timer->timerId = XObject_startTimer_ms(timer, timer->m_interval, XTimer_timerType(timer));
			if (timer->timerId)
				timer->m_isRun = true;
		}
		XEvent_accept(event);
	}
	else
	{
		XClassGetVirtualFunc(timer, EXObject_TimerEvent, void(*)(XObject*))(timer);
	}
}
void VXTimerBase_deinit(XTimerBase* timer)
{
	//XPrintf("释放定时器\n");
	XTimerBase_stop_base(timer);
	// 释放父对象
	XClass_Deinit_Parent(XObject, timer);
}

void VXTimerBase_start(XTimerBase* timer)
{
	timer->m_firstTrigger = false;
	XTimerBase_stop_base(timer);
	if(timer->m_timeout)
		timer->timerId = XObject_startTimer_ms(timer, timer->m_timeout, XTimer_timerType(timer));
	else if(timer->m_interval)
		timer->timerId = XObject_startTimer_ms(timer, timer->m_interval, XTimer_timerType(timer));
	if(timer->timerId)
		timer->m_isRun = true;
}
void VXTimerBase_stop(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId && XTimerBase_isRunning(timer))
	{
		// 关闭定时器
		XObject_killTimer(timer, timer->timerId);
		//XPrintf("停止定时器: id:%d\n", timer->timerId);
		timer->timerId = 0;
		timer->m_isRun = false;
	}
}