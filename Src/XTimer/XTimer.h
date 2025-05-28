#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>


typedef struct XTimer XTimer;
typedef void (*XTimerCreate)(XTimer* timer);
typedef void (*XTimerFree)(XTimer* timer);

typedef void (*XTimerStart)(XTimer* timer);
typedef void (*XTimerStop)(XTimer* timer);
//定时器超时回调函数
typedef void (*XTimerCallback)(XTimer* timer);
typedef struct XTimer_PortFunc
{
	XTimerCreate create;//创建
	XTimerFree   free;//释放
	XTimerStart start;//启动
	XTimerStop stop;//关闭
	//void (*setInterval)(XTimer* timer,size_t val);//
	XTimerCallback timerCallback;//超时回调
}XTimer_PortFunc;
typedef XTimer_PortFunc XTimer_PortFuncInit ;
//定时器抽象
typedef struct XTimer
{
	void* data;
	size_t timerId;//定时器id
	int interval;//定时间隔
	int remainingTime;//超时前的剩余时间
	size_t number;//超时次数
	//函数
	XTimer_PortFunc m_port;//接口
}XTimer;
XTimer* XTimer_new(XTimer_PortFuncInit* port);
#ifdef WIN32
XTimer* XTimer_new_Win32ThreadpoolTimer();
XTimer_PortFunc XTimer_PortFunc_Win32ThreadpoolTimer();
XTimer* XTimer_new_Win32TimeSetEvent();
XTimer_PortFunc XTimer_PortFunc_Win32TimeSetEvent();
#endif
void XTimer_create(XTimer* timer);
void XTimer_free(XTimer* timer);
void XTimer_start(XTimer*timer);
void XTimer_stop(XTimer* timer);
//设置定时时间
void XTimer_setInterval(XTimer* timer,int value);
//超时回调函数
void XTimer_out(XTimer* timer);

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
	uint32_t expire_time;     // 到期时间戳（毫秒）
	uint32_t interval;        // 间隔时间（用于周期性任务）
	bool is_periodic;         // 是否为周期性任务
	void (*TimerCallback)(void* arg);   // 回调函数
	void* arg;                // 回调函数参数
} XTimerTask;

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
