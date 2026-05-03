#ifndef XTIMERGROUPWHEEL_H
#define XTIMERGROUPWHEEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XVector.h"
#include"XTimerGroupBase.h"
#include"XTimerData.h"
typedef struct XListSLinked XListSLinked;
typedef struct XVector XVector;
typedef struct XTimerGroupWheel XTimerGroupWheel;
#define XTIMEGROUPWHEEL_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimerGroupWheel))       //XTimeGroupWheel虚函数表大小
XCLASS_DEFINE_BEGING(XTimerGroupWheel)
XCLASS_DEFINE_ENUM(XTimerGroupWheel, Add_TimeWheel) = XCLASS_VTABLE_GET_SIZE(XTimerGroupBase),
XCLASS_DEFINE_ENUM(XTimerGroupWheel, Remove_TimeWheel),
XCLASS_DEFINE_END(XTimerGroupWheel)
//定时器轮组
typedef struct XTimerGroupWheel
{
	XTimerGroupBase m_class;//继承
	XVector m_timeWheel;//多时间轮	
	XMutex* m_mutex;//互斥锁
	XAtomic_size_t m_count;//正在管理的定时器数量
}XTimerGroupWheel;
XVtable* XTimerGroupWheel_class_init();
XTimerGroupWheel* XTimerGroupWheel_create(uint16_t precision);
void XTimerGroupWheel_init(XTimerGroupWheel* group, uint16_t precision);
void XTimerGroupWheel_addTimeWheel_base(XTimerGroupWheel* group,size_t slotsCount);
void XTimerGroupWheel_removeTimeWheel_base(XTimerGroupWheel* group);
void XTimerGroupWheel_setMutex(XTimerGroupWheel* group, XMutex* mutex);
size_t XTimerGroupWheel_count(XTimerGroupWheel* group);
#define XTimerGroupWheel_addTimer_base				XTimerGroupBase_addTimer_base
#define XTimerGroupWheel_removeTimer_base			XTimerGroupBase_removeTimer_base
#define XTimerGroupWheel_timeRange					XTimerGroupBase_timeRange
#define XTimerGroupWheel_min_time					XTimerGroupBase_min_time
#define XTimerGroupWheel_max_time					XTimerGroupBase_max_time
#define XTimerGroupWheel_handler_base				XTimerGroupBase_handler_base
#define XTimerGroupWheel_delete_base				XTimerGroupBase_delete_base

//全局时间轮
XTimerGroupWheel*XTimerGroupWheel_global();
//全局是否存在
bool XTimerGroupWheel_GlobalExists(void);
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
