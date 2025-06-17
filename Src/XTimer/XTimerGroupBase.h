#ifndef XTIMERGROUPBASE_H
#define XTIMERGROUPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XClass.h"
typedef struct XTimerBase XTimerBase;
typedef struct XTimerGroupBase XTimerGroupBase;
#define XTIMERGROUPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XClass)+3)       //XTimerGroupBase虚函数表大小
enum XTimerGroupBaseVtableEnum
{
	EXTimerGroupBase_Add_Timer = XCLASS_VTABLE_GET_SIZE(XClass),
	EXTimerGroupBase_Remove_Timer,
	EXTimerGroupBase_Poll,
};
typedef struct XTimerGroupBase
{
	XClass m_parent;
	uint16_t m_precision;		//精度 毫秒   
	size_t m_current_tick;      // 当前系统滴答
}XTimerGroupBase;
void XTimerGroupBase_init(XTimerGroupBase*group, XVtable* vtable, uint16_t precision);
bool XTimerGroupBase_addTimer_base(XTimerGroupBase* group, XTimerBase* timer);
//仅从任务中删除，需要手动释放
bool XTimerGroupBase_removeTimer_base(XTimerGroupBase* group, XTimerBase* timer);
void XTimerGroupBase_poll_base(XTimerGroupBase* group);
#define XTimerGroupBase_delete_base XClass_delete_base
//设置
void XTimerGroupBase_setGlobal(XTimerGroupBase* group);
XTimerGroupBase* XTimerGroupBase_global();
void XTimerGroupBase_global_poll();
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
