#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XTimerPosixTimerFd.h"
#include "XMemory.h"
#include "XEventLoop.h"
#include "XCoreApplication.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

static void ReadEventCb(XEventMin*ev);

void VXTimer_setTimerCallback(XTimer* timer, XTimerCallback callback);
void VXTimer_setUserData(XTimer* timer, void* userData);
void VXTimer_setTimeout(XTimer* timer, size_t value);
void VXTimer_out(XTimer* timer);
// 前向声明
static void VXTimer_start(XTimer* timer);
static void VXTimer_stop(XTimer* timer);
static void VXTimer_deinit(XTimer* timer);
static void VXTimer_setInterval(XTimerPosixTimerFd* timer, size_t value);
static void timerFdEventCallback(int fd, void* userData);

void VXTimer_start(XTimer* timer) {
    XTimer_stop_base(timer);
    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;

    if (((XTimer*)linuxTimer)->timerId == 0) {
        ((XTimer*)linuxTimer)->timerId = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        
    }

    struct itimerspec new_value = {0};
    new_value.it_value.tv_sec = timer->m_timeout / 1000;
    new_value.it_value.tv_nsec = (timer->m_timeout % 1000) * 1000000;

    if (!timer->m_isSingleShot) {
        new_value.it_interval.tv_sec = timer->m_interval / 1000;
        new_value.it_interval.tv_nsec = (timer->m_interval % 1000) * 1000000;
    }

    timerfd_settime(((XTimer*)linuxTimer)->timerId, 0, &new_value, NULL);
    XEventLoop_addFd(XCoreApplication_eventLoop(), timer,((XTimer*)linuxTimer)->timerId,XEVENT_READY);
    timer->m_isRun = true;
}

void VXTimer_stop(XTimer* timer) {
    if (!XTimer_isRunning(timer)) return;

    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;
    struct itimerspec new_value = {0};
    timerfd_settime(((XTimer*)linuxTimer)->timerId, 0, &new_value, NULL);
    
    
    timer->m_isRun = false;
    linuxTimer->m_twoCb = false;
}

void VXTimer_deinit(XTimer* timer) {
    VXTimer_stop(timer);
    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;
    
    if (((XTimer*)linuxTimer)->timerId != 0)
    {
        XEventLoop_removeFd(XCoreApplication_eventLoop(), ((XTimer*)linuxTimer)->timerId);
        close(((XTimer*)linuxTimer)->timerId);
        ((XTimer*)linuxTimer)->timerId = 0;
    }

    // 调用父类析构
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(timer);
}

void VXTimer_setInterval(XTimerPosixTimerFd* timer, size_t value) {
    ((XTimer*)timer)->m_interval = value;
    if (XTimer_isRunning((XTimer*)timer)) {
        XTimer_start_base((XTimer*)timer);
    }
}

XVtable* XTimerPosixTimerFd_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT_SIZE(XTIMERLINUXTIMERFD_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    
    void* table[] = {
        VXTimer_start,
        VXTimer_stop,
        VXTimer_setTimerCallback,
        VXTimer_setUserData,
        VXTimer_setTimeout,
        VXTimer_setInterval,
        VXTimer_out
    };
    
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimer_deinit);
    return XVTABLE_DEFAULT;
}

XTimerPosixTimerFd* XTimerPosixTimerFd_create() {
    XTimerPosixTimerFd* timer = XMalloc_System(sizeof(XTimerPosixTimerFd));
    if (timer) {
        XTimerPosixTimerFd_init(timer);
        Set_Class_MemoryFree(timer, XFree_System);
    }
    return timer;
}

void XTimerPosixTimerFd_init(XTimerPosixTimerFd* timer) {
    if (!timer) return;
    memset(((XTimer*)timer) + 1, 0, sizeof(XTimerPosixTimerFd) - sizeof(XTimer));
    XTimer_init((XTimer*)timer, XTimerPosixTimerFd_class_init());
    ((XTimer*)timer)->timerId =0;
    timer->m_twoCb = false;
    XObject_addEventFilter(timer,XEVENT_READY,ReadEventCb,NULL);
}
void ReadEventCb(XEventMin *ev)
{
	XTimer* timer = ((XTimer*)ev->receiver);
    uint64_t exp;
    ssize_t ret = read(((XTimer*)timer)->timerId, &exp, sizeof(exp));
    if (ret != sizeof(exp)) return;

    XTimer_out(timer);

    if (timer->m_isSingleShot) {
        XTimer_stop_base(timer);
        if (timer->m_autoDelete) {
            XObject_deleteLater(timer);
        }
    } 
    else if (!((XTimerPosixTimerFd*)timer)->m_twoCb)
     {
        VXTimer_stop(timer);
        ((XTimerPosixTimerFd*)timer)->m_twoCb = true;
        
        struct itimerspec new_value = {0};
        new_value.it_interval.tv_sec = timer->m_interval / 1000;
        new_value.it_interval.tv_nsec = (timer->m_interval % 1000) * 1000000;
        new_value.it_value = new_value.it_interval;

        timerfd_settime(((XTimer*)timer)->timerId, 0, &new_value, NULL);
        timer->m_isRun = true;
    }
}
#endif