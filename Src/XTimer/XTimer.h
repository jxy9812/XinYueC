#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerWheel.h"
#define XTIMER_VTABLE_SIZE (XTIMERWHEEL_VTABLE_SIZE)       //XTimer虚函数表大小
//
typedef struct XTimer 
{
	XTimerBase m_parent;
	XTimerBaseCallback callback;
	void* m_userData;
} XTimer;
XVtable* XTimer_class_init();
XTimer* XTimer_create();
void XTimer_init(XTimer* timer);
#define XTimer_delete_base			XTimerBase_delete_base
#define XTimer_start_base			XTimerBase_start_base
#define XTimer_stop_base			XTimerBase_stop_base
//设置定时时间
#define XTimer_setTimeout_base		XTimerBase_setTimeout_base
#define XTimer_setInterval_base		XTimerBase_setInterval_base
#define XTimer_setUserData			XTimerBase_setUserData_base
#define XTimer_setTimerCallback		XTimerBase_setTimerCallback_base
#define XTimer_setGroup				XTimerBase_setTimerId
// 是否为周期性任务
#define XTimer_isPeriodic			XTimerBase_isPeriodic
#define XTimer_isRunning			XTimerBase_isRunning
#define XTimer_getTimeout			XTimerBase_getTimeout
#define XTimer_getInterval			XTimerBase_getInterval
#define XTimer_getGroup				XTimerBase_getTimerId
#define XTimer_getUserData			XTimerBase_getUserData
//超时回调函数
#define XTimer_out_base				XTimerBase_out_base
//定时器超时触发信号
void* XTimer_timeout_signal(XTimer* timer);
//连接槽函数
void XTimer_callOnTimeout(XTimer* timer,XObject* receiver, XSlotFunc slot_func, XConnectionType type);
//一次触发
void XTimer_singleShot(size_t msec, XObject* receiver, XSlotFunc slot_func, XConnectionType type);

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
