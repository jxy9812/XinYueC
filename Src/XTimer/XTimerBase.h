#ifndef XTIMERBASE_H
#define XTIMERBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XClass.h"
typedef void (*XTimerBaseCallback)(void* userData);
typedef struct XTimerBase XTimerBase;
#define XTIMERBASE_VTABLE_SIZE (XCLASS_VTABLE_SIZE+3)       //XTimerBase虚函数表大小
enum XTimerBaseVtableEnum
{
	EXTimerBase_Start= XCLASS_VTABLE_SIZE,
	EXTimerBase_Stop,
	EXTimerBase_SetTimeOut,
	EXTimerBase_SetInterval,
	//EXTimerBase_IsPeriodic,
	//EXTimerBase_IsRun,
	//EXTimerBase_GetInterval,
	//EXTimerBase_GetTimerId,
};
//定时器抽象
typedef struct XTimerBase
{
	XClass m_parent;//类
	bool m_autoFree;//自动释放
	bool m_isRun;//是否运行
	size_t m_timeout;//首次超时时间
	size_t m_interval;//定时间隔
	size_t timerId;//定时器id
	void* m_userData;
	XTimerBaseCallback m_timerCallback; // 回调函数
	size_t number;//超时次数
}XTimerBase;
XTimerBase* XTimerBase_new(XVtable*vtable);
void XTimerBase_init(XTimerBase* timer, XVtable* vtable);
void XTimerBase_free_base(XTimerBase* timer);
void XTimerBase_start_base(XTimerBase*timer);
void XTimerBase_stop_base(XTimerBase* timer);
//设置定时时间
void XTimerBase_setTimeout_base(XTimerBase* timer, size_t value);
void XTimerBase_setInterval_base(XTimerBase* timer, size_t value);
void XTimerBase_setUserData(XTimerBase* timer, void* userData);
void XTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId);
// 是否为周期性任务
bool XTimerBase_isPeriodic(XTimerBase* timer);
bool XTimerBase_isRunning(XTimerBase* timer);
size_t XTimerBase_getTimeout(XTimerBase* timer);
size_t XTimerBase_getInterval(XTimerBase* timer);
size_t XTimerBase_getTimerId(XTimerBase* timer);
void*  XTimerBase_getUserData(XTimerBase* timer);
//超时回调函数
void XTimerBase_out(XTimerBase* timer);

/*                              以毫秒为单位的时间锉                                     */
//当前时间  累计添加
void XTimerBase_inc(size_t tick_period);
//设置当前时间 时间锉
void XTimerBase_setCurrentTime(size_t time);
//获得当前时间
size_t XTimerBase_getCurrentTime();
//设置获取当前时间的函数方法
void XTimerBase_setCurrentTimeFunc(size_t(*get)());
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
