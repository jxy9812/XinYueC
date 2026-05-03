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
	//XTimerGroupBase* m_group;//加入的组
} XTimerTimeWheel;
XVtable* XTimerTimeWheel_class_init();
XTimerTimeWheel* XTimerTimeWheel_create();
void XTimerTimeWheel_init(XTimerTimeWheel* timer);
#define XTimerTimeWheel_delete_base XTimerBase_deleteLater
#define XTimerTimeWheel_start_base XTimerBase_start_base
#define XTimerTimeWheel_stop_base XTimerBase_stop_base
//设置定时时间
#define XTimerTimeWheel_setTimeout XTimerBase_setTimeout
#define XTimerTimeWheel_setInterval XTimerBase_setInterval
#define XTimerTimeWheel_setUserData XTimerBase_setUserData
#define XTimerTimeWheel_setTimerCallback XTimerBase_setTimerCallback
#define XTimerTimeWheel_setGroup	XObject_setParent
// 是否为周期性任务
#define XTimerTimeWheel_isPeriodic XTimerBase_isPeriodic
#define XTimerTimeWheel_isRunning XTimerBase_isRunning
#define XTimerTimeWheel_timeout XTimerBase_timeout
#define XTimerTimeWheel_interval XTimerBase_interval
#define XTimerTimeWheel_group XObject_parent
#define XTimerTimeWheel_userData XTimerBase_userData
//超时回调函数
#define XTimerTimeWheel_out XTimerBase_out
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
