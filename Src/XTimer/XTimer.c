#include"XTimer.h"
#include"XCoreApplication.h"
#include"XMemory.h"
#include"XAbstractEventDispatcher.h"
#include<string.h>
static void VXTimerBase_setTimerCallback(XTimer* timer, XTimerBaseCallback callback);
static void VXTimerBase_setUserData(XTimer* timer, void* userData);
static void TimerOutEventCb(XEvent* event);
static  void TimerCallback(void* userData);
void VXTimerBase_setTimeout(XTimerBase* timer, size_t value);
void VXTimerBase_out(XTimerBase* timer);

static void VXTimerBase_setInterval(XTimer* timer, size_t value);
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
VXTimerBase_start,VXTimerBase_stop,VXTimerBase_setTimerCallback,VXTimerBase_setUserData,
VXTimerBase_setTimeout,VXTimerBase_setInterval,
VXTimerBase_out
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetTimerCallback, VXTimerBase_setTimerCallback);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetUserData, VXTimerBase_setUserData);
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

	//XObject_postEvent(userData,XEvent_create(NULL,XEVENT_TIMEROUT,0), XEVENT_PRIORITY_NORMAL);
}
void TimerOutEventCb(XEvent* event)
{
	//XPrintf("触发\n");
	/*XTimer* timer = event->receiver;
	if (timer->callback)
		timer->callback(timer->m_userData);*/
	/*XTimer_timeout_signal(event->receiver);*/
	XEvent_accept(event);
}
void* XTimer_timeout_signal(XTimer* timer)
{
	XEmitSignal(timer, XTimer_timeout_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
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
	//timer->callback = callback;
}

void VXTimerBase_setUserData(XTimer* timer, void* userData)
{
	//timer->m_userData = userData;
}

void VXTimerBase_setInterval(XTimer* timer, size_t value)
{
	timer->m_class.m_interval = value;
}
void VXObject_timerEvent(XTimerBase* timer, XTimerEvent* event)
{
	//XPrintf("定时器触发\n");
	XTimer_timeout_signal(timer);
	XEvent_accept(event);
}
void VXTimerBase_deinit(XTimerBase* timer)
{
	XPrintf("释放定时器\n");
	XTimerBase_stop_base(timer);
	// 释放父对象
	XClass_Deinit_Parent(XObject, timer);
}

void VXTimerBase_start(XTimerBase* timer)
{
	XTimerBase_stop_base(timer);
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(timer);
	timer->timerId=XAbstractEventDispatcher_registerTimer(disp, timer->m_interval* 1000000, XCoarseTimer, timer);
	
	if(timer->timerId)
		timer->m_isRun = true;
	//timer->m_isPeriodic = true;
}
void VXTimerBase_stop(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId && XTimerBase_isRunning(timer))
	{
		//// 关闭定时器
		XAbstractEventDispatcher* disp = XObject_eventDispatcher(timer);
		XAbstractEventDispatcher_unregisterTimer_base(disp, timer->timerId);
		//XPrintf("停止定时器: id:%d\n", timer->timerId);
		timer->timerId = 0;
		timer->m_isRun = false;
	}
}