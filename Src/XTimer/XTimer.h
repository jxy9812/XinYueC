#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XClass.h"
typedef void (*XTimerBaseCallback)(void* userData);
typedef struct XTimerBase XTimerBase;
#define XTIMERBASE_VTABLE_SIZE (4)       //XTimerBase虚函数表大小
enum XTimerBaseVtableEnum
{
	EXTimerBase_Free,
	EXTimerBase_Start,
	EXTimerBase_Stop,
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
	bool m_isPeriodic;         // 是否为周期性任务
	bool m_isRun;//是否运行
	size_t m_interval;//定时间隔
	size_t timerId;//定时器id
	void* m_userData;
	XTimerBaseCallback m_timerCallback; // 回调函数
	size_t number;//超时次数
}XTimerBase;
XTimerBase* XTimerBase_new(XVtable*vtable);
#ifdef WIN32
XTimerBase* XTimer_new_Win32ThreadpoolTimer();
XTimerBase* XTimer_new_Win32TimeSetEvent();
#endif

void XTimerBase_freeBase(XTimerBase* timer);
void XTimerBase_startBase(XTimerBase*timer);
void XTimerBase_stopBase(XTimerBase* timer);
//设置定时时间
void XTimerBase_setIntervalBase(XTimerBase* timer, size_t value);
void XTimerBase_setUserData(XTimerBase* timer, void* userData);
void XTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId);
bool XTimerBase_isPeriodic(XTimerBase* timer);
bool XTimerBase_isRunning(XTimerBase* timer);
size_t XTimerBase_interval(XTimerBase* timer);
size_t XTimerBase_timerId(XTimerBase* timer);
void*  XTimerBase_userData(XTimerBase* timer);
//超时回调函数
void XTimer_out(XTimerBase* timer);

/*                              以毫秒为单位的时间锉                                     */
//当前时间  累计添加
void XTimer_inc(size_t tick_period);
//设置当前时间 时间锉
void XTimer_setCurrentTime(size_t time);
//获得当前时间
size_t XTimer_getCurrentTime();
//设置获取当前时间的函数方法
void XTimer_setCurrentTimeFunc(size_t(*get)());
// 定时器任务结构
typedef struct XTimerTask
{
	uint32_t m_expire_time;     // 到期时间戳（毫秒）
	uint32_t m_interval;        // 间隔时间（用于周期性任务）
	bool is_periodic;         // 是否为周期性任务
	void (*TimerCallback)(void* arg);   // 回调函数
	void* arg;                // 回调函数参数
} XTimerTask;

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
