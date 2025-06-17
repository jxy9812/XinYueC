#ifndef XTIMERGROUPWHEEL_H
#define XTIMERGROUPWHEEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerGroupBase.h"
#include"XTimerWheel.h"
typedef struct XListSLinked XListSLinked;
typedef struct XVector XVector;
typedef struct XTimerWheel  XTimerWheel;
typedef struct XTimerGroupWheel XTimerGroupWheel;
#define XTIMEGROUPWHEEL_VTABLE_SIZE (XTIMERGROUPBASE_VTABLE_SIZE+2)       //XTimeGroupWheel虚函数表大小
enum XTimerGroupWheelVtableEnum
{
	EXTimeGroupWheel_Add_TimeWheel = XTIMERGROUPBASE_VTABLE_SIZE,
	EXTimeGroupWheel_Remove_TimeWheel,
};
// 单个时间轮结构
typedef struct XTimeWheel {
	size_t m_tick;						// 当前滴答计数
	XVector* m_slots;					// 槽数组，每个槽是一个链表头 /XVector<XListSLinked<XTimerWheel*>>
} XTimeWheel;
//定时器轮组
typedef struct XTimerGroupWheel
{
	XTimerGroupBase m_parent;//继承
	XVector* m_timeWheel;//多时间轮	/XVector<XTimeWheel>
}XTimerGroupWheel;
XVtable* XTimerGroupWheel_class_init();
XTimerGroupWheel* XTimerGroupWheel_create(uint16_t precision);
void XTimerGroupWheel_init(XTimerGroupWheel* group, uint16_t precision);
void XTimerGroupWheel_addTimeWheel_base(XTimerGroupWheel* group,size_t slotsCount);
void XTimerGroupWheel_removeTimeWheel_base(XTimerGroupWheel* group);
#define XTimerGroupWheel_addTimer_base				XTimerGroupBase_addTimer_base
#define XTimerGroupWheel_removeTimer_base			XTimerGroupBase_removeTimer_base
#define XTimerGroupWheel_poll_base					XTimerGroupBase_poll_base
#define XTimerGroupWheel_delete_base				XTimerGroupBase_delete_base
//如果全局定时器组不存在就创建默认的三级时间轮(1-1000s)
void XTimerGroupWheel_setGlobal();
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
