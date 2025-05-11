#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
typedef struct XTimer XTimer;
typedef void (*XTimerCreate)(XTimer* timer);
typedef void (*XTimerFree)(XTimer* timer);

typedef void (*XTimerStart)(XTimer* timer);
typedef void (*XTimerStop)(XTimer* timer);
//typedef void (*XTimerSetInterval)(XTimer* timer);
//定时器超时回调函数
typedef void (*XTimerOut)(XTimer* timer);
typedef struct XTimer_PortFunc
{
	XTimerCreate create;//创建
	XTimerFree   free;//释放
	XTimerStart start;//启动
	XTimerStop stop;//关闭
	XTimerOut timeout;//超时回调
}XTimer_PortFunc;
//定时器抽象
typedef struct XTimer
{
	void* data;
	unsigned int timerId;//定时器id
	int interval;//定时间隔
	int remainingTime;//超时前的剩余时间
	size_t number;//超时次数
	//函数
	XTimer_PortFunc m_port;//接口
}XTimer;
XTimer* XTimer_new(XTimer_PortFunc* port);
void XTimer_free(XTimer* timer);
void XTimer_start(XTimer*timer);
void XTimer_stop(XTimer* timer);
//设置定时时间
void XTimer_setInterval(XTimer* timer,int value);

//超时回调函数
void XTimer_out(XTimer* timer);

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
