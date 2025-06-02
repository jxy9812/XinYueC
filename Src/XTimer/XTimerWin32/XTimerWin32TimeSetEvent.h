#ifdef WIN32
#ifndef XTIMERWIN32TIMESETEVENT_H
#define XTIMERWIN32TIMESETEVENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTimerBase.h"
#define XTIMERWIN32TIMESETEVENT_VTABLE_SIZE (XTIMERBASE_VTABLE_SIZE)       //XTimerWin32TimeSetEvent虚函数表大小
//定时器TimeSetEvent 封装
typedef struct XTimerWin32TimeSetEvent
{
	XTimerBase m_parent;//类
	
}XTimerWin32TimeSetEvent;
XVtable* XTimerWin32TimeSetEvent_class_init();
XTimerWin32TimeSetEvent* XTimerWin32TimeSetEvent_new();
void XTimerWin32TimeSetEvent_init(XTimerWin32TimeSetEvent* timer);
#endif
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
