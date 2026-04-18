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

void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
void VXTimerBase_setUserData(XTimerBase* timer, void* userData);
void VXTimerBase_setTimeout(XTimerBase* timer, size_t value);
void VXTimerBase_out(XTimerBase* timer);
// 前向声明
static void VXTimerBase_start(XTimerBase* timer);
static void VXTimerBase_stop(XTimerBase* timer);
static void VXTimerBase_deinit(XTimerBase* timer);
static void VXTimerBase_setInterval(XTimerPosixTimerFd* timer, size_t value);
static void timerFdEventCallback(int fd, void* userData);

void VXTimerBase_start(XTimerBase* timer) {
    XTimerBase_stop_base(timer);
    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;

    if (((XTimerBase*)linuxTimer)->timerId == 0) {
        ((XTimerBase*)linuxTimer)->timerId = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        
    }

    struct itimerspec new_value = {0};
    new_value.it_value.tv_sec = timer->m_timeout / 1000;
    new_value.it_value.tv_nsec = (timer->m_timeout % 1000) * 1000000;

    if (!timer->m_isSingleShot) {
        new_value.it_interval.tv_sec = timer->m_interval / 1000;
        new_value.it_interval.tv_nsec = (timer->m_interval % 1000) * 1000000;
    }

    timerfd_settime(((XTimerBase*)linuxTimer)->timerId, 0, &new_value, NULL);
    XEventLoop_addFd(XCoreApplication_eventLoop(), timer,((XTimerBase*)linuxTimer)->timerId,XEVENT_READY);
    timer->m_isRun = true;
}

void VXTimerBase_stop(XTimerBase* timer) {
    if (!XTimerBase_isRunning(timer)) return;

    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;
    struct itimerspec new_value = {0};
    timerfd_settime(((XTimerBase*)linuxTimer)->timerId, 0, &new_value, NULL);
    
    
    timer->m_isRun = false;
    linuxTimer->m_twoCb = false;
}

void VXTimerBase_deinit(XTimerBase* timer) {
    VXTimerBase_stop(timer);
    XTimerPosixTimerFd* linuxTimer = (XTimerPosixTimerFd*)timer;
    
    if (((XTimerBase*)linuxTimer)->timerId != 0)
    {
        XEventLoop_removeFd(XCoreApplication_eventLoop(), ((XTimerBase*)linuxTimer)->timerId);
        close(((XTimerBase*)linuxTimer)->timerId);
        ((XTimerBase*)linuxTimer)->timerId = 0;
    }

    // 调用父类析构
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(timer);
}

void VXTimerBase_setInterval(XTimerPosixTimerFd* timer, size_t value) {
    ((XTimerBase*)timer)->m_interval = value;
    if (XTimerBase_isRunning((XTimerBase*)timer)) {
        XTimerBase_start_base((XTimerBase*)timer);
    }
}

XVtable* XTimerPosixTimerFd_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XTIMERLINUXTIMERFD_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_DEFAULT(XObject_class_init());
    
    void* table[] = {
        VXTimerBase_start,
        VXTimerBase_stop,
        VXTimerBase_setTimerCallback,
        VXTimerBase_setUserData,
        VXTimerBase_setTimeout,
        VXTimerBase_setInterval,
        VXTimerBase_out
    };
    
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
    return XVTABLE_DEFAULT;
}

XTimerPosixTimerFd* XTimerPosixTimerFd_create() {
    XTimerPosixTimerFd* timer = XMemory_malloc(sizeof(XTimerPosixTimerFd));
    if (timer) {
        XTimerPosixTimerFd_init(timer);
        Set_Class_MemoryFree(timer, XFree);
    }
    return timer;
}

void XTimerPosixTimerFd_init(XTimerPosixTimerFd* timer) {
    if (!timer) return;
    memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimerPosixTimerFd) - sizeof(XTimerBase));
    XTimerBase_init((XTimerBase*)timer, XTimerPosixTimerFd_class_init());
    ((XTimerBase*)timer)->timerId =0;
    timer->m_twoCb = false;
    XObject_addEventFilter(timer,XEVENT_READY,ReadEventCb,NULL);
}
void ReadEventCb(XEventMin *ev)
{
	XTimerBase* timer = ((XTimerBase*)ev->receiver);
    uint64_t exp;
    ssize_t ret = read(((XTimerBase*)timer)->timerId, &exp, sizeof(exp));
    if (ret != sizeof(exp)) return;

    XTimerBase_out(timer);

    if (timer->m_isSingleShot) {
        XTimerBase_stop_base(timer);
        if (timer->m_autoDelete) {
            XObject_deleteLater(timer);
        }
    } 
    else if (!((XTimerPosixTimerFd*)timer)->m_twoCb)
     {
        VXTimerBase_stop(timer);
        ((XTimerPosixTimerFd*)timer)->m_twoCb = true;
        
        struct itimerspec new_value = {0};
        new_value.it_interval.tv_sec = timer->m_interval / 1000;
        new_value.it_interval.tv_nsec = (timer->m_interval % 1000) * 1000000;
        new_value.it_value = new_value.it_interval;

        timerfd_settime(((XTimerBase*)timer)->timerId, 0, &new_value, NULL);
        timer->m_isRun = true;
    }
}
#endif