#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#ifndef XTIMERLINUXTIMERFD_H
#define XTIMERLINUXTIMERFD_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XTimer.h"
#include <stdint.h>

#define XTIMERLINUXTIMERFD_VTABLE_SIZE (XTimer_VTABLE_SIZE)

typedef struct XTimerPosixTimerFd 
{
    XTimer m_class;
    bool m_twoCb;
    //int timerFd; // Linux timerfd句柄
} XTimerPosixTimerFd;

XVtable* XTimerPosixTimerFd_class_init();
XTimerPosixTimerFd* XTimerPosixTimerFd_create();
void XTimerPosixTimerFd_init(XTimerPosixTimerFd* timer);

#ifdef __cplusplus
}
#endif
#endif // XTIMERLINUXTIMERFD_H
#endif