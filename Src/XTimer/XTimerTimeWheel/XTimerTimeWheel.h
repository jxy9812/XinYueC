#ifndef XTimerTimeWheel_H
#define XTimerTimeWheel_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerBase.h"
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef struct XListSLinked XListSLinked;
#define XTIMERTIMEWHEEL_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimerBase))       //XTimerTimeWheel虚函数表大小
// 定时器时间轮
typedef struct XTimerTimeWheel 
{
	XTimerBase m_class;
	size_t m_expire_ticks;     // 到期时间戳（毫秒）
	XListSLinked* m_list;//加入的链表
} XTimerTimeWheel;
XVtable* XTimerTimeWheel_class_init();
XTimerTimeWheel* XTimerTimeWheel_create();
void XTimerTimeWheel_init(XTimerTimeWheel* timer);
#define XTimerTimeWheel_delete_base XTimerBase_delete_base
#define XTimerTimeWheel_start_base XTimerBase_start_base
#define XTimerTimeWheel_stop_base XTimerBase_stop_base
//设置定时时间
#define XTimerTimeWheel_setTimeout_base XTimerBase_setTimeout_base
#define XTimerTimeWheel_setInterval_base XTimerBase_setInterval_base
#define XTimerTimeWheel_setUserData XTimerBase_setUserData_base
#define XTimerTimeWheel_setTimerCallback XTimerBase_setTimerCallback_base
#define XTimerTimeWheel_setGroup XTimerBase_setTimerId
// 是否为周期性任务
#define XTimerTimeWheel_isPeriodic XTimerBase_isPeriodic
#define XTimerTimeWheel_isRunning XTimerBase_isRunning
#define XTimerTimeWheel_getTimeout XTimerBase_timeout
#define XTimerTimeWheel_getInterval XTimerBase_interval
#define XTimerTimeWheel_getGroup XTimerBase_timerId
#define XTimerTimeWheel_getUserData XTimerBase_getUserData
//超时回调函数
#define XTimerTimeWheel_out_base XTimerBase_out_base
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
