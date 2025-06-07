#ifndef XTIMERWHEEL_H
#define XTIMERWHEEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerBase.h"
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef struct XListSLinked XListSLinked;
#define XTIMERWHEEL_VTABLE_SIZE (XTIMERBASE_VTABLE_SIZE)       //XTimerWheel虚函数表大小
// 定时器节点结构
typedef struct XTimerWheel 
{
	XTimerBase m_parent;
	size_t m_expire_ticks;     // 到期时间戳（毫秒）
	XListSLinked* m_list;//加入的链表
} XTimerWheel;
XVtable* XTimerWheel_class_init();
XTimerWheel* XTimerWheel_create();
void XTimerWheel_init(XTimerWheel* timer);
#define XTimerWheel_delete_base XTimerBase_delete_base
#define XTimerWheel_start_base XTimerBase_start_base
#define XTimerWheel_stop_base XTimerBase_stop_base
//设置定时时间
#define XTimerWheel_setTimeout_base XTimerBase_setTimeout_base
#define XTimerWheel_setInterval_base XTimerBase_setInterval_base
#define XTimerWheel_setUserData XTimerBase_setUserData
#define XTimerWheel_setTimerCallback XTimerBase_setTimerCallback
#define XTimerWheel_setGroup XTimerBase_setTimerId
// 是否为周期性任务
#define XTimerWheel_isPeriodic XTimerBase_isPeriodic
#define XTimerWheel_isRunning XTimerBase_isRunning
#define XTimerWheel_getTimeout XTimerBase_getTimeout
#define XTimerWheel_getInterval XTimerBase_getInterval
#define XTimerWheel_getGroup XTimerBase_getTimerId
#define XTimerWheel_getUserData XTimerBase_getUserData
//超时回调函数
#define XTimerWheel_out XTimerBase_out
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
