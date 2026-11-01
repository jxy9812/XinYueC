#include"XTimer.h"
#include "XPrintf.h"   /* printf 直出改经 XPrintf（输出重定向栈，约束文档时间/依赖规则） */
#include"XCoreApplication.h"
#include"XMemory.h"
#include"XAbstractEventDispatcher.h"
#include<string.h>
static void VXTimer_start(XTimer* timer);
static void VXTimer_stop(XTimer* timer);
static void VXTimer_deinit(XTimer* timer);
static void VXObject_timerEvent(XTimer* timer, XTimerEvent* event);
XVtable* XTimer_class_init()
{
	XVTABLE_INIT_DEFAULT(XTimer)
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
	XCLASS_SHOW_SIZE_DEFAULT(XTimer);
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
	timer->m_timerData.timerId = XTIMER_INVALID_ID;
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

/* [改周期重装 2026-10-29] 运行中改 interval 必须重新向分发器装订，不能只
 * 改对象字段：
 *   ① 时间轮节点在 create_timer_node 里对 XTimerData 做的是**值拷贝**
 *      （XTimeWheelGroup.c:158 `timer_data->m_data = *data`），周期重装
 *      读的是节点副本的 m_interval，字段改了节点不变 ⇒ 实际周期仍是旧值；
 *   ② VXObject_timerEvent 只在 m_firstTrigger==false 的首拍按字段值重装
 *      一次，之后节点自持周期副本，不再回读对象。
 * 原实现只赋值，调「轮询(ms)」这类运行期改周期的操作完全不生效（实测
 * 主站页设 50ms 仍按建连时的 1000ms 跑，用户观感=「设了没用」）。此处
 * 在运行态走 stop+start 重装：XTimer_start_base 会按新字段值重新注册，
 * 时间轮重新按新周期落槽。未运行（或未启动）时只存字段，等 start 生效。 */
void XTimer_setInterval(XTimer * timer, size_t value)
{
	bool wasRunning;
	if (!timer)
		return;
	XTimerData_setInterval(&timer->m_timerData, value);
	wasRunning = timer->m_isRun
		&& timer->m_timerData.timerId != XTIMER_INVALID_ID;
	if (wasRunning)
		XTimer_start_base(timer);
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

XTimer* XTimer_create_ex(XMemoryType memory)
{
	XTimer* timer = XMemory_malloc(sizeof(XTimer), memory);
	if (timer == NULL)
		return timer;
	XTimer_init(timer);
	Set_Class_Memory(timer, memory); Set_Class_IsHeap(timer, true);
	return timer;
}

void XTimer_out(XTimer* timer)
{
	if (timer == NULL)
		return;
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
	if (!timer)
		return;
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
	if (!timer)
		return;
	XTimer_setTimeout(timer, msec);
	XTimer_setAutoDelete(timer, true);
	XTimer_setSingleShot(timer, true);
	XObject_connect_2(timer, XSignal(XTimer_timeout_signal), slot_func);
	XTimer_start_base(timer);
}
void VXObject_timerEvent(XTimer* timer, XTimerEvent* event)
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
	if(timer->m_timerData.timerId!= XTIMER_INVALID_ID)
		timer->m_isRun = true;
}
void VXTimer_stop(XTimer* timer)
{
	//XPrintf("停止定时器\n");
	if (timer->m_timerData.timerId!= XTIMER_INVALID_ID && XTimer_isRunning(timer))
	{
		// 关闭定时器
		XObject_killTimer(timer, timer->m_timerData.timerId);
		//XPrintf("停止定时器: id:%d\n", timer->timerId);
		timer->m_timerData.timerId = XTIMER_INVALID_ID;
		timer->m_isRun = false;
	}
}
