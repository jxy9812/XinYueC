#ifndef XTIMERBASE_H
#define XTIMERBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XObject.h"
typedef void (*XTimerBaseCallback)(void* userData);
typedef struct XTimerBase XTimerBase;
typedef struct XTimerGroupBase XTimerGroupBase;
#define XTIMERBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimerBase))       //XTimerBase虚函数表大小
XCLASS_DEFINE_BEGING(XTimerBase)
XCLASS_DEFINE_ENUM(XTimerBase,Start) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTimerBase,Stop),
XCLASS_DEFINE_ENUM(XTimerBase,SetTimerCallback),
XCLASS_DEFINE_ENUM(XTimerBase,SetUserData),
XCLASS_DEFINE_ENUM(XTimerBase,SetTimeOut),
XCLASS_DEFINE_ENUM(XTimerBase,SetInterval),
XCLASS_DEFINE_ENUM(XTimerBase,Out),
XCLASS_DEFINE_END(XTimerBase)
//定时器抽象
typedef struct XTimerBase
{
	XObject m_class;//类
	bool m_autoDelete;//自动释放
	bool m_isRun;//是否运行
	bool m_singleShot;              // 是否为单次定时器
	size_t m_timeout;//首次超时时间
	size_t m_interval;//定时间隔
	size_t m_expire_ticks;     // 到期时间戳（毫秒）
	size_t timerId;//定时器id
	XTimerGroupBase* m_timerGroup;//定时器组
	void* m_data;//定时器数据
	void* m_userData;
	XTimerBaseCallback m_timerCallback; // 回调函数
	size_t number;//超时次数
}XTimerBase;
XTimerBase* XTimerBase_create(XVtable*vtable);
void XTimerBase_init(XTimerBase* timer, XVtable* vtable);
#define XTimerBase_delete_base    XClass_delete_base
#define XTimerBase_deinit_base    XClass_deinit_base
void XTimerBase_start_base(XTimerBase*timer);
void XTimerBase_stop_base(XTimerBase* timer);
//设置定时时间
void XTimerBase_setTimeout_base(XTimerBase* timer, size_t value);
void XTimerBase_setInterval_base(XTimerBase* timer, size_t value);
void XTimerBase_setUserData_base(XTimerBase* timer, void* userData);
void XTimerBase_setTimerCallback_base(XTimerBase* timer, XTimerBaseCallback callback);
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId);
void XTimerBase_setAutoDelete(XTimerBase* timer, bool del);
void XTimerBase_setSingleShote(XTimerBase* timer, bool ss);
void XTimerBase_setTimerGroup(XTimerBase* timer, XTimerGroupBase* group);
// 是否为周期性任务
bool XTimerBase_isPeriodic(XTimerBase* timer);
bool XTimerBase_isRunning(XTimerBase* timer);
size_t XTimerBase_getTimeout(XTimerBase* timer);
size_t XTimerBase_getInterval(XTimerBase* timer);
size_t XTimerBase_getTimerId(XTimerBase* timer);
void*  XTimerBase_getUserData(XTimerBase* timer);
bool   XTimerBase_isAutoDelete(XTimerBase* timer);
XTimerGroupBase* XTimerBase_getTimerGroup(XTimerBase* timer);
//超时回调函数
void XTimerBase_out_base(XTimerBase* timer);

/*                              以毫秒为单位的时间锉                                     */
//当前时间  累计添加
void XTimerBase_inc(size_t tick_period);
//设置当前时间 时间锉
void XTimerBase_setCurrentTime(size_t time);
//获得当前时间
size_t XTimerBase_getCurrentTime();
//设置获取当前时间的函数方法
void XTimerBase_setCurrentTimeFunc(size_t(*get)());
//设置延迟函数毫秒级
void XTimerBase_setDelayMsFunc(void(*delay)(const size_t msec));
//延迟毫秒数
void XTimerBase_delay_ms(const size_t delay_ms);
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
