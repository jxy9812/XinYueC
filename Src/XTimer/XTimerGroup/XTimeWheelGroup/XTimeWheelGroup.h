#ifndef XTimeWheelGroup_H
#define XTimeWheelGroup_H
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
typedef struct XTimeWheelGroup XTimeWheelGroup;
#define XTIMEWHEELGROUP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimeWheelGroup))       //XTimeGroupWheel虚函数表大小
XCLASS_DEFINE_BEGING(XTimeWheelGroup)
//XCLASS_DEFINE_ENUM(XTimeWheelGroup, Add_TimeWheel) = XCLASS_VTABLE_GET_SIZE(XTimerGroupBase),
XCLASS_DEFINE_EXTEND_END(XTimeWheelGroup, XTimerGroupBase)
//定时器轮组    -当前设计仅支持毫秒
typedef struct XTimeWheelGroup
{
	XTimerGroupBase m_class;//继承
	XVector m_timeWheel;//多时间轮	
	XAtomic_size_t m_count;//正在管理的定时器数量
	XMutex* m_mutex;
}XTimeWheelGroup;
XVtable* XTimeWheelGroup_class_init();
XTimeWheelGroup* XTimeWheelGroup_create(uint16_t precision);
void XTimeWheelGroup_init(XTimeWheelGroup* group, uint16_t precision);
void XTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group,size_t slotsCount);
void XTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group);
size_t XTimeWheelGroup_count(XTimeWheelGroup* group);
#define XTimeWheelGroup_addTimerMs_base				XTimerGroupBase_addTimerMs
#define XTimeWheelGroup_removeTimer_base			XTimerGroupBase_removeTimer_base
#define XTimeWheelGroup_timeRange					XTimerGroupBase_timeRange
#define XTimeWheelGroup_min_time					XTimerGroupBase_min_time
#define XTimeWheelGroup_max_time					XTimerGroupBase_max_time
#define XTimeWheelGroup_tick_base					XTimerGroupBase_tick_base
#define XTimeWheelGroup_handler_base				XTimerGroupBase_handler_base
#define XTimeWheelGroup_clear_base					XTimerGroupBase_clear_base
#define XTimeWheelGroup_deleteLater					XTimerGroupBase_deleteLater

//全局时间轮
XTimeWheelGroup*XTimeWheelGroup_global();
//全局是否存在
bool XTimeWheelGroup_GlobalExists(void);
#ifdef __cplusplus
}
#endif
#endif // !XTimeWheelGroup_H
