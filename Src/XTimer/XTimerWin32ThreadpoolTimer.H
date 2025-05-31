#ifdef WIN32
#ifndef XTIMERWIN32THREADPOOLTIMER_H
#define XTIMERWIN32THREADPOOLTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTimerBase.h"
//定时器ThreadpoolTimer 封装
typedef struct XTimerWin32ThreadpoolTimer
{
	XTimerBase m_parent;//类

}XTimerWin32ThreadpoolTimer;
XVtable* XTimerWin32ThreadpoolTimer_class_init();
XTimerWin32ThreadpoolTimer* XTimerXTimerWin32ThreadpoolTimer_new();
void XTimerWin32ThreadpoolTimer_init(XTimerWin32ThreadpoolTimer* timer);
#endif
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
