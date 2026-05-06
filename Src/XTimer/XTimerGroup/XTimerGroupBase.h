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
XCLASS_DEFINE_ENUM(XTimerGroupBase, Add_TimerMs) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTimerGroupBase, Add_TimerNs),
XCLASS_DEFINE_ENUM(XTimerGroupBase,Remove_Timer),
XCLASS_DEFINE_ENUM(XTimerGroupBase, Tick),
XCLASS_DEFINE_ENUM(XTimerGroupBase, Handler),
XCLASS_DEFINE_END(XTimerGroupBase)
/**
 * @brief 高精度时间获取函数指针类型
 * 该函数应返回自某个固定起点（如系统启动）以来的纳秒数。
 */
typedef uint64_t(*XHighResTimeFunc)(void);
typedef struct XTimerGroupBase
{
	XObject m_class;
	uint64_t m_precision;		 ///< 定时器组精度（单位由具体实现决定，如毫秒、纳秒）
	uint64_t m_min_time;//最小时间
	uint64_t  m_max_time;//最大时间
	uint64_t m_current_tick;      // 当前系统滴答
	XHighResTimeFunc m_high_res_time_func; ///< 高精度时间获取函数指针
}XTimerGroupBase;
void XTimerGroupBase_init(XTimerGroupBase*group, uint16_t precision);
/**
 * @brief 为定时器组设置高精度时间源
 * @param base 定时器组基类指针
 * @param func 高精度时间获取函数
 */
void XTimerGroupBase_setHighResTimeFunc(XTimerGroupBase* base, XHighResTimeFunc func);
XHandle XTimerGroupBase_addTimerMs_base(XTimerGroupBase* group, XTimerData data);
XHandle XTimerGroupBase_addTimerNs_base(XTimerGroupBase* group, XTimerData data);
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
void XTimerGroupBase_handler_base(XTimerGroupBase* group);
#define XTimerGroupBase_deleteLater			XObject_deleteLater
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
