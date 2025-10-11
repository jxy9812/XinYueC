#include"XTimer.h"
#include"XCoreApplication.h"
#include"XMemory.h"
#if XTIMER_IS_TIMEWHEEL
#include"XTimerTimeWheel.h"
#elif XTIMER_IS_TIMESETEVENT
#include"XTimerWin32TimeSetEvent.h"
#elif XTIMER_IS_THREADPOOLTIMER
#include "XTimerWin32ThreadpoolTimer.h"
#endif
#include<string.h>
static void VXTimerBase_setTimerCallback(XTimer* timer, XTimerBaseCallback callback);
static void VXTimerBase_setUserData(XTimer* timer, void* userData);
static void TimerOutEventCb(XEvent* event);
static  void TimerCallback(void* userData);

typedef struct XTimer
{
#if XTIMER_IS_TIMEWHEEL
	XTimerTimeWheel m_class;
#elif XTIMER_IS_TIMESETEVENT
	XTimerWin32TimeSetEvent	m_class;
#elif XTIMER_IS_THREADPOOLTIMER
	XTimerWin32ThreadpoolTimer	m_class;
#endif
	XTimerBaseCallback callback;
	void* m_userData;
} XTimer;

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
#if XTIMER_IS_TIMEWHEEL
	XVTABLE_INHERIT_DEFAULT(XTimerTimeWheel_class_init());
#elif XTIMER_IS_TIMESETEVENT
	XVTABLE_INHERIT_DEFAULT(XTimerWin32TimeSetEvent_class_init());
#elif XTIMER_IS_THREADPOOLTIMER
	XVTABLE_INHERIT_DEFAULT(XTimerWin32ThreadpoolTimer_class_init());
#endif
	//void* table[] = 
	//{

	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	//XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetTimerCallback, VXTimerBase_setTimerCallback);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetUserData, VXTimerBase_setUserData);
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
	//初始化父类以外的数据
#if XTIMER_IS_TIMEWHEEL
	memset(((XTimerTimeWheel*)timer) + 1, 0, sizeof(XTimer) - sizeof(XTimerTimeWheel));
	XTimerTimeWheel_init(timer);
#elif XTIMER_IS_TIMESETEVENT
	memset(((XTimerWin32TimeSetEvent*)timer) + 1, 0, sizeof(XTimer) - sizeof(XTimerWin32TimeSetEvent));
	XTimerWin32TimeSetEvent_init(timer);
#elif XTIMER_IS_THREADPOOLTIMER
	memset(((XTimerWin32ThreadpoolTimer*)timer) + 1, 0, sizeof(XTimer) - sizeof(XTimerWin32ThreadpoolTimer));
	XTimerWin32ThreadpoolTimer_init(timer);
#endif
	XClassGetVtable(timer) = XTimer_class_init();
	XObject_setParent(timer, XCoreApplication_getTimerGroup());
	XObject_addEventFilter(timer, XEVENT_TIMEROUT, TimerOutEventCb, NULL);
	((XTimerBase*)timer)->m_timerCallback = TimerCallback;
	((XTimerBase*)timer)->m_userData = timer;
}

XTimer* XTimer_create()
{
	XTimer* timer = XMemory_malloc(sizeof(XTimer));
	if (timer == NULL)
		return timer;
	XTimer_init(timer);
	return timer;
}
void TimerCallback(void* userData)
{
	XObject_postEvent(userData,XEvent_create(NULL,XEVENT_TIMEROUT,0), XEVENT_PRIORITY_NORMAL);
}
void TimerOutEventCb(XEvent* event)
{
	//XPrintf("触发\n");
	XTimer* timer = event->receiver;
	if (timer->callback)
		timer->callback(timer->m_userData);
	XTimer_timeout_signal(event->receiver);
	XEvent_Accept(event);
}
void* XTimer_timeout_signal(XTimer* timer)
{
	if (timer)
		XSignalSlot_emit(((XObject*)timer)->m_signalSlot, XTimer_timeout_signal, NULL, XEVENT_PRIORITY_NORMAL);
	return XTimer_timeout_signal;
}

void XTimer_callOnTimeout(XTimer* timer, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(timer&& slot_func)
		XObject_connect(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, XConnectionType_Auto);
}

void XTimer_singleShot(size_t msec, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout_base(timer, msec);
	XTimerBase_setAutoDelete(timer,true);
	XTimerBase_setSingleShote(timer, true);
	XObject_connect(timer, XSignal(XTimer_timeout_signal), receiver, slot_func, type);
	XTimer_start_base(timer);
}

void VXTimerBase_setTimerCallback(XTimer* timer, XTimerBaseCallback callback)
{
	timer->callback = callback;
}

void VXTimerBase_setUserData(XTimer* timer, void* userData)
{
	timer->m_userData = userData;
}

//void VXTimerBase_deinit(XTimer* timer)
//{
//	//调用父类释放函数
//	XVtableGetFunc(XTimerTimeWheel_class_init(), EXClass_Deinit, void(*)(XTimerTimeWheel*))(timer);
//}
//
//void VXTimerBase_out(XTimer* timer)
//{
//	//调用父类释放函数
//	XVtableGetFunc(XTimerTimeWheel_class_init(), EXTimerBase_Out, void(*)(XTimerTimeWheel*))(timer);
//	//XTimer_timeout_signal(timer);
//}


