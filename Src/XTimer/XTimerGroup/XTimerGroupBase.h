#ifndef XTIMERGROUPBASE_H
#define XTIMERGROUPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XObject.h"
#include"XTimerData.h"
#define XTIMERGROUPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimerGroupBase))       //XTimerGroupBase虚函数表大小
XCLASS_DEFINE_BEGING(XTimerGroupBase)
XCLASS_DEFINE_ENUM(XTimerGroupBase, Add_Timer) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTimerGroupBase,Remove_Timer),
XCLASS_DEFINE_ENUM(XTimerGroupBase, Tick),
XCLASS_DEFINE_END(XTimerGroupBase)
typedef struct XTimerGroupBase
{
	XObject m_class;
	uint16_t m_precision;		//精度 毫秒   
	uint16_t m_min_time;//最小时间
	size_t  m_max_time;//最大时间
	size_t m_current_tick;      // 当前系统滴答
}XTimerGroupBase;
void XTimerGroupBase_init(XTimerGroupBase*group, uint16_t precision);
XHandle XTimerGroupBase_addTimer_base(XTimerGroupBase* group, XTimerData data);
//仅从任务中删除，需要手动释放
bool XTimerGroupBase_removeTimer_base(XTimerGroupBase* group, XHandle handle);
/**
 * @brief 获取定时器组可以管理的时间范围（以毫秒为单位）
 * @param group 定时器组指针
 * @param min_time 最小可管理时间（输出参数，可为NULL）
 * @param max_time 最大可管理时间（输出参数，可为NULL）
 * @return 成功返回true，失败返回false
 */
bool XTimerGroupBase_timeRange(XTimerGroupBase* group, size_t* min_time, size_t* max_time);
size_t XTimerGroupBase_min_time(XTimerGroupBase* group);
size_t XTimerGroupBase_max_time(XTimerGroupBase* group);
void XTimerGroupBase_tick_base(XTimerGroupBase* group);
void XTimerGroupBase_handler(XTimerGroupBase* group);
#define XTimerGroupBase_deleteLater			XObject_deleteLater
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
