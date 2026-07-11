#include "XAbstractEventDispatcher.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XVector.h"
#include "XTimeWheelGroup.h"
#include "XAbstractNativeEventFilter.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XHrTimerGroup.h"
#include "XDateTime.h"
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"
#include "XNetwork_platform.h"
#include <string.h>
static XVector* global_nativeFilters;///< 本地事件过滤器列表
static XMutex* global_mutex = NULL;
#define PlatformPrivate(Dispatcher)  (((XAbstractEventDispatcher*)Dispatcher)->d_ptr)
#define GetXMutex(Dispatcher)         PlatformPrivate(Dispatcher)->mutex
#define Global_Lock             XMutex_lock(global_mutex)
#define Global_UnLock           XMutex_unlock(global_mutex)
// 前向声明虚函数
static bool VXAbstractEventDispatcher_processEvents(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags);
static void VXAbstractEventDispatcher_registerSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier);
static void VXAbstractEventDispatcher_unregisterSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier);
static void VXAbstractEventDispatcher_registerTimer(XAbstractEventDispatcher* self, XTimerId timerId, XDuration interval, XTimerType timerType, XObject* object);
static bool VXAbstractEventDispatcher_unregisterTimer(XAbstractEventDispatcher* self, XTimerId timerId);
static bool VXAbstractEventDispatcher_unregisterTimers(XAbstractEventDispatcher* self, XObject* object);
static XVector* VXAbstractEventDispatcher_timersForObject(const XAbstractEventDispatcher* self, const XObject* object);
static XDuration VXAbstractEventDispatcher_remainingTime(const XAbstractEventDispatcher* self, XTimerId timerId);
static void VXAbstractEventDispatcher_wakeUp(XAbstractEventDispatcher* self);
static void VXAbstractEventDispatcher_interrupt(XAbstractEventDispatcher* self);
static void VXAbstractEventDispatcher_startingUp(XAbstractEventDispatcher* self);
static void VXAbstractEventDispatcher_closingDown(XAbstractEventDispatcher* self);
static void VXAbstractEventDispatcher_deinit(XAbstractEventDispatcher* self);

static void global_init();
// ===================================================================
// === 虚函数表初始化 ================================================
// ===================================================================

void XAbstractEventDispatcherPrivate_init(XAbstractEventDispatcherPrivate* dp)
{
    dp->m_hrtimerGroup = NULL;
    dp->notifiers = NULL;
    dp->m_ioRing = NULL;
    dp->m_interrupt = false;
    dp->m_threadData = NULL;
}

void XAbstractEventDispatcherPrivate_deinit(XAbstractEventDispatcherPrivate * dp)
{
    if (dp->m_hrtimerGroup)
    {
        XClass_delete_base(dp->m_hrtimerGroup);
        dp->m_hrtimerGroup = NULL;
    }
    if (dp->notifiers)
    {
        XHashMap_delete_base(dp->notifiers);
        dp->notifiers = NULL;
    }
    /* ioRing 是全局单例，生命周期独立，此处仅清空指针 */
    dp->m_ioRing = NULL;
}

XVtable* XAbstractEventDispatcher_class_init(void)
{
    XVTABLE_CREAT_DEFAULT // static XVtable* XVTABLE_DEFAULT = NULL; if exists return

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractEventDispatcher))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        // 继承 XObject 的虚函数表
        XVTABLE_INHERIT_XCLASS(XObject);

    // 添加本类虚函数
    void* table[] = {
        (void*)VXAbstractEventDispatcher_processEvents,
        (void*)VXAbstractEventDispatcher_registerSocketNotifier,
        (void*)VXAbstractEventDispatcher_unregisterSocketNotifier,
        (void*)VXAbstractEventDispatcher_registerTimer,
        (void*)VXAbstractEventDispatcher_unregisterTimer,
        (void*)VXAbstractEventDispatcher_unregisterTimers,
        (void*)VXAbstractEventDispatcher_timersForObject,
        (void*)VXAbstractEventDispatcher_remainingTime,
        (void*)VXAbstractEventDispatcher_wakeUp,
        (void*)VXAbstractEventDispatcher_interrupt,
        (void*)VXAbstractEventDispatcher_startingUp,
        (void*)VXAbstractEventDispatcher_closingDown
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, (void*)VXAbstractEventDispatcher_deinit);
#if SHOWCONTAINERSIZE
    printf("XAbstractEventDispatcher size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

// ===================================================================
// === 对象创建与初始化 ==============================================
// ===================================================================

XAbstractEventDispatcher* XAbstractEventDispatcher_create(XObject* parent)
{
    XAbstractEventDispatcher* self = (XAbstractEventDispatcher*)XMalloc_System(sizeof(XAbstractEventDispatcher));
    if (self) {
        XAbstractEventDispatcher_init(self, parent);
        Set_Class_MemoryFree(self, XFree_System);
    }
    return self;
}

void XAbstractEventDispatcher_init(XAbstractEventDispatcher* self, XObject* parent)
{
    if (ISNULL(self, "self is NULL in XAbstractEventDispatcher_init")) {
        return;
    }

    // 初始化基类（XObject）
    XObject_init((XObject*)self);
    XClassGetVtable(self) = XAbstractEventDispatcher_class_init();

    // 设置父对象
    XObject_setParent((XObject*)self, (XObject*)parent);

    ///* 不能在这里分配并初始化私有数据，这个类要被继承，子类需要扩展数据*/
    //self->d_ptr = (XAbstractEventDispatcherPrivate*)XMalloc_System(sizeof(XAbstractEventDispatcherPrivate));
    //if (self->d_ptr) {
    //    XAbstractEventDispatcherPrivate_init(self->d_ptr);
    //}

    global_init();
    //self->m_hrtimerGroup = XHrTimerGroup_create(1);
  
}

// ===================================================================
// === 虚函数默认实现（纯虚函数应由子类重写，此处提供空/错误实现）===
// ===================================================================

static bool VXAbstractEventDispatcher_processEvents(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags)
{
    /* 1. 先处理定时器任务 */
    if (XAbstractEventDispatcher_isMainThread(self) && XTimeWheelGroup_GlobalExists())
    {
        if (XTimeWheelGroup_count(XTimeWheelGroup_global()))
        {
            XTimeWheelGroup_handler_base(XTimeWheelGroup_global());
        }
    }
    if (self->d_ptr->m_hrtimerGroup)
    {
        XHrTimerGroup_handler_base(self->d_ptr->m_hrtimerGroup);
    }
    XAbstractNetIoRing* ioRing = self->d_ptr->m_ioRing;
    if (XAbstractEventDispatcher_isMainThread(self))
    {
        /* 2. 处理 I/O 事件（仅主线程轮询共享 IOCP/epoll 完成端口，
         *    工作线程不触碰共享 IOCP，防止窃取主线程的 I/O 完成事件） */
#ifdef XNETWORK_USE_LWIP
        XAbstractNetIoRing_pollLwip();
#endif
        if (ioRing && XAbstractNetIoRing_isEnabled(ioRing))
        {
            XAbstractNetIoRing_processReady(ioRing);
        }
    }

    /* 3. 处理投递事件（含定时器/I/O 产生的事件） */
    size_t size = 0;
    XVector* events = XThreadData_takePostedEvents();
    if (events)
    {
        for_each_iterator(events, XVector, it)
        {
            XPostEvent* ePost = XVector_iterator_data(&it);
            if (!ePost|| !ePost->event) continue;
            if ((flags & XEventLoop_ExcludeUserInputEvents) && ePost->event->input_event == XEventLoop_ExcludeUserInputEvents)
            {
                continue;
            }
            if ((flags & XEventLoop_ExcludeSocketNotifiers) && ePost->event->type == XEVENT_TYPE_SOCK_ACT) {
                continue;
            }
            if ((flags & XEventLoop_X11ExcludeTimers) && ePost->event->type == XEVENT_TYPE_TIMER) {
                continue;
            }
            if (!ePost->receiver||ePost->receiver->is_deleting_children)
            {
                XEvent_delete_base(ePost->event);
                ++size;
            }
            XEvent* e = ePost->event;
            ePost->event = NULL;
            if(XCoreApplication_sendEvent(ePost->receiver, e))
               ++size;
        }
        if(size< XVector_size_base(events))
            XThreadData_push_front_list(events);
        XVector_delete_base(events);
    }

    /* 4. 阻塞等待（WaitForMoreEvents 标志且无事件处理且未中断） */
    if ((flags & XEventLoop_WaitForMoreEvents) && size == 0 && !self->d_ptr->m_interrupt)
    {
        /* 计算超时：优先使用高精度定时器的下次到期时间 */
        int timeoutMs = -1;
        if (self->d_ptr->m_hrtimerGroup &&
            XHrTimerGroup_count(self->d_ptr->m_hrtimerGroup))
        {
            int64_t ns = XHrTimerGroup_getNextExpireTime(self->d_ptr->m_hrtimerGroup)
                       - XDateTime_currentNSecsSinceEpoch();
            timeoutMs = (int)(ns / 1000000);
            if (timeoutMs < 0) timeoutMs = 0;
            if (timeoutMs > 999999999) timeoutMs = 0;
        }
        /* 限制最大阻塞时间，确保时间轮/定时器定期轮询 */
        if (timeoutMs < 0 || timeoutMs > 20) timeoutMs = 20;

        if (XAbstractEventDispatcher_isMainThread(self))
        {
            /* 主线程：阻塞在 IOCP 完成端口上（I/O 完成事件唤醒） */
            if (ioRing && XAbstractNetIoRing_isEnabled(ioRing))
            {
                XAbstractEventDispatcher_aboutToBlock_signal(self);
                XAbstractNetIoRing_waitForEvents_base(ioRing, timeoutMs);
                XAbstractEventDispatcher_awake_signal(self);
            }
        }
        else
        {
            /* 工作线程：阻塞在线程局部信号量上（不触碰共享 IOCP，
             *    防止窃取主线程的 I/O 完成事件；由 postEvent -> signalWake 唤醒） */
            if (self->d_ptr->m_threadData)
            {
                XAbstractEventDispatcher_aboutToBlock_signal(self);
                XThreadData_waitForWake(self->d_ptr->m_threadData, timeoutMs);
                XAbstractEventDispatcher_awake_signal(self);
            }
        }
    }

    return size > 0;
}

static void VXAbstractEventDispatcher_registerSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    if (!notifier || !self || !self->d_ptr) return;
    XFd fd = XSocketNotifier_socket(notifier);
    if (fd < 0) return;
    if (!self->d_ptr->notifiers)
    {
        self->d_ptr->notifiers = XHashMap_Create(XFd, XVector, uintptr_t_compare);
        XContainerSetDataDeinitMethod(self->d_ptr->notifiers, XVector_deinit_base);
    }
    /* 从 dispatcher 全局 HashMap 查找或创建 XVector */
    XVector* v = (XVector*)XHashMap_value_base(self->d_ptr->notifiers, &fd);
    if (!v)
    {
        v = XVector_Create(XSocketNotifier*);
        XContainerSetCompare(v, uintptr_t_compare);
        XMapBase_insert_valueMove_base(self->d_ptr->notifiers, &fd, v);
        XVector_delete_base(v);
        v = (XVector*)XHashMap_value_base(self->d_ptr->notifiers, &fd);
        if (!v)return;
    }
    if (XVector_indexOf(v, &notifier, 0) == -1)
    {
        XVector_append_1_base(v, &notifier);
    }
}

static void VXAbstractEventDispatcher_unregisterSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    if (!notifier || !self || !self->d_ptr || !self->d_ptr->notifiers) return;
    XFd fd = XSocketNotifier_socket(notifier);
    if (fd < 0) return;

    XVector* v = (XVector*)XHashMap_value_base(self->d_ptr->notifiers, &fd);
    if (!v) return;

    int index = XVector_indexOf(v, &notifier, 0);
    if (index != -1)
    {
        XVector_remove_base(v, index, 1);
    }
    if (XVector_isEmpty_base(v))
    {
        XHashMap_remove_base(self->d_ptr->notifiers, &fd);
        //XVector_delete_base(v);
    }
}
static void TimerCallback(void* userData, XTimerData* timer)
{
    XObject* object = (XObject*)userData;
    if (!object)return;
    XEvent* timerEvent = XEventTimer_create(XTimerData_timerId(timer));
    if (timerEvent)
    {
        timerEvent->posted = true;
        timerEvent->spontaneous = true;
        XCoreApplication_postEvent(object, timerEvent, XEVENT_PRIORITY_NORMAL);
    }
}
static void VXAbstractEventDispatcher_registerTimer(XAbstractEventDispatcher* dispatcher, XTimerId timerId, XDuration intervalNs, XTimerType timerType, XObject* object)
{
    if (!intervalNs || !object)return;
    XFileDescriptor* desc = XFd_get(timerId);
    if (!desc) return;

    XAbstractEventDispatcher_TimerInfo* timerInfo = (XAbstractEventDispatcher_TimerInfo*)XMalloc_Hybrid(sizeof(XAbstractEventDispatcher_TimerInfo));
    if (!timerInfo) {
        XFd_free(timerId);
        return;
    }
    memset(timerInfo, 0, sizeof(XAbstractEventDispatcher_TimerInfo));
    timerInfo->timerId = timerId;
    timerInfo->interval = intervalNs;
    timerInfo->timerType = timerType;
    //timerInfo->object = object;

    XTimerData data = { 0 };
    XTimerData_setAutoDelete(&data, true);
    XTimerData_setTimerId(&data, timerId);
    XTimerData_setTimerCallback(&data, TimerCallback);
    XTimerData_setUserData(&data, object);

    if (timerType != XTimerType_PreciseTimer)
    {
        size_t intervalMs = (intervalNs + 999999) / 1000000;
        if (timerType == XTimerType_VeryCoarseTimer)
            intervalMs=((intervalMs + 999) / 1000) * 1000;
        XTimeWheelGroup* group = XTimeWheelGroup_global();
        if (group && XTimeWheelGroup_max_time(group) > intervalMs)
        {
            XTimerData_setInterval(&data, intervalMs);
            XHandle handle = XTimeWheelGroup_addTimerMs_base(group, data);
            if (handle)
            {
                timerInfo->Xhandle = handle;
                desc->handle = timerInfo;
                return;
            }
        }
    }
    if (dispatcher->d_ptr->m_hrtimerGroup == NULL)
    {
        dispatcher->d_ptr->m_hrtimerGroup = XHrTimerGroup_create(1);
        XHrTimerGroup_setHighResTimeFunc(dispatcher->d_ptr->m_hrtimerGroup,XDateTime_currentNSecsSinceEpoch);
    }
    timerInfo->timerType = XTimerType_PreciseTimer;
    XTimerData_setInterval(&data, intervalNs);
    XHandle handle = XHrTimerGroup_addTimerNs_base(dispatcher->d_ptr->m_hrtimerGroup, data);
    if (handle)
    {
        timerInfo->Xhandle = handle;
        desc->handle = timerInfo;
        XAbstractEventDispatcher_wakeUp_base(dispatcher);
        return;
    }
    XFree_Hybrid(timerInfo);
    XFd_free(timerId);
}

static bool VXAbstractEventDispatcher_unregisterTimer(XAbstractEventDispatcher* dispatcher, XTimerId timerId)
{
    if (timerId == XTIMER_INVALID_ID) return false;
    XFileDescriptor* desc = XFd_get(timerId);
    if (!desc) return false;
    XAbstractEventDispatcher_TimerInfo* timerInfo = (XAbstractEventDispatcher_TimerInfo*)desc->handle;
    if (!timerInfo) return false;

    bool is_ok = false;
    if (timerInfo->timerType != XTimerType_PreciseTimer)
    {
        is_ok = XTimeWheelGroup_removeTimer_base(XTimeWheelGroup_global(), timerInfo->Xhandle);
    }
    else
    {
        is_ok = XHrTimerGroup_removeTimer_base(dispatcher->d_ptr->m_hrtimerGroup, timerInfo->Xhandle);
    }
    if (!is_ok) return false;

    XFree_Hybrid(timerInfo);
    XFd_free(timerId);
    return true;
}

static bool VXAbstractEventDispatcher_unregisterTimers(XAbstractEventDispatcher* dispatcher, XObject* object)
{
    if (!object) return false;
    XThread* objectThread = XObject_thread((XObject*)object);
    XThread* dispatcherThread = XObject_thread((XObject*)dispatcher);
    bool found = false;
    /* 遍历整个 XFd 表，查找属于指定 object 且同线程的定时器 */
    for (int i = 0; i < XFD_TABLE_SIZE; i++)
    {
        XFileDescriptor* desc = XFd_get(i);
        if (!desc) continue;
        if ((XFdType)desc->type != XFD_TYPE_TIMER) continue;
        if (desc->ctx != object) continue;
        /* 检查所属线程 */
        if (objectThread && dispatcherThread && objectThread != dispatcherThread) continue;

        XAbstractEventDispatcher_TimerInfo* timerInfo = (XAbstractEventDispatcher_TimerInfo*)desc->handle;
        if (!timerInfo) continue;

        if (timerInfo->timerType != XTimerType_PreciseTimer)
        {
            XTimeWheelGroup_removeTimer_base(XTimeWheelGroup_global(), timerInfo->Xhandle);
        }
        else
        {
            XHrTimerGroup_removeTimer_base(dispatcher->d_ptr->m_hrtimerGroup, timerInfo->Xhandle);
        }
        XFree_Hybrid(timerInfo);
        XFd_free(i);
        found = true;
    }
    return found;
}

static XVector* VXAbstractEventDispatcher_timersForObject(const XAbstractEventDispatcher* dispatcher, const XObject* object)
{
    if (!object) return NULL;
    XThread* objectThread = XObject_thread((XObject*)object);
    XThread* dispatcherThread = XObject_thread((XObject*)dispatcher);

    XVector* result = XVector_Create(XAbstractEventDispatcher_TimerInfoV2);
    if (!result) return NULL;

    for (int i = 0; i < XFD_TABLE_SIZE; i++)
    {
        XFileDescriptor* desc = XFd_get(i);
        if (!desc) continue;
        if ((XFdType)desc->type != XFD_TYPE_TIMER) continue;
        if (desc->ctx != object) continue;
        if (objectThread && dispatcherThread && objectThread != dispatcherThread) continue;

        XAbstractEventDispatcher_TimerInfo* timerInfo = (XAbstractEventDispatcher_TimerInfo*)desc->handle;
        if (!timerInfo) continue;

        XAbstractEventDispatcher_TimerInfoV2 info = {
            .interval = timerInfo->interval,
            .timerId = timerInfo->timerId,
            .timerType = timerInfo->timerType
        };
        XVector_push_back_1_base(result, &info);
    }
    return result;
}

static XDuration VXAbstractEventDispatcher_remainingTime(const XAbstractEventDispatcher* self, XTimerId timerId)
{
    (void)self; (void)timerId;
    return -1;
}

static void VXAbstractEventDispatcher_wakeUp(XAbstractEventDispatcher* self)
{
    if (!self || !self->d_ptr) return;
    if (XAbstractEventDispatcher_isMainThread(self))
    {
        /* 主线程：通过 IOCP 唤醒（主线程在 GetQueuedCompletionStatus 上阻塞） */
        if (self->d_ptr->m_ioRing)
            XAbstractNetIoRing_wakeUp_base(self->d_ptr->m_ioRing);
    }
    else
    {
        /* 工作线程：通过线程局部信号量唤醒（不触碰共享 IOCP） */
        if (self->d_ptr->m_threadData)
            XThreadData_signalWake(self->d_ptr->m_threadData);
    }
}

static void VXAbstractEventDispatcher_interrupt(XAbstractEventDispatcher* self)
{
    if (self && self->d_ptr)
    {
        self->d_ptr->m_interrupt = true;
        XAbstractEventDispatcher_wakeUp_base(self);
    }
}

static void VXAbstractEventDispatcher_startingUp(XAbstractEventDispatcher* self)
{
    (void)self;
}

static void VXAbstractEventDispatcher_closingDown(XAbstractEventDispatcher* self)
{
    (void)self;
}
void VXAbstractEventDispatcher_deinit(XAbstractEventDispatcher* self)
{
    if (self->d_ptr) {
        XAbstractEventDispatcherPrivate_deinit(self->d_ptr);
        XFree_System(self->d_ptr);
        self->d_ptr = NULL;
    }
    XClass_Deinit_Parent(XObject, self);
}
void global_init()
{
    if (global_mutex)return;
    global_mutex = XMutex_create(XLock_Spin);
    XFd_init();
}

/* ================================================================
 * 工厂函数（平台无关）
 *
 * 创建 XAbstractEventDispatcher 实例 + 分配私有数据 + 创建平台 I/O 后端。
 * 原实现位于 XEventDispatcher_win32.c，现迁移至此处实现平台无关化。
 * ================================================================ */
XAbstractEventDispatcher* XEventDispatcher_create(XObject* parent)
{
    XAbstractEventDispatcher* self = (XAbstractEventDispatcher*)XMalloc_System(sizeof(XAbstractEventDispatcher));
    if (!self) return NULL;

    /* 初始化基类（设置 XAbstractEventDispatcher 虚函数表） */
    XAbstractEventDispatcher_init(self, parent);

    /* 分配私有数据 */
    XAbstractEventDispatcherPrivate* d = (XAbstractEventDispatcherPrivate*)XCalloc_System(1, sizeof(XAbstractEventDispatcherPrivate));
    if (!d)
    {
        XFree_System(self);
        return NULL;
    }
    XAbstractEventDispatcherPrivate_init(d);
    self->d_ptr = d;

    /* 设置线程类型 */
    if (XThread_isMainThread())
        self->type = XDISPATCHER_THREAD_TYPE_MAIN;
    else
        self->type = XDISPATCHER_THREAD_TYPE_WORKER;

    /* 建立反向引用：调度器 -> 所属线程的 XThreadData（工作线程信号量唤醒用） */
    d->m_threadData = XThreadData_current();

    /* 创建 I/O 事件环（平台钩子） */
#if XAbstractNetIoRing_ON
    if (!XAbstractNetIoRing_global())
    {
        d->m_ioRing = XAbstractNetIoRing_createPlatform();
        if (d->m_ioRing)
            XAbstractNetIoRing_setGlobal(d->m_ioRing);
    }
    else
    {
        /* 后续线程复用全局 ioRing 实例 */
        d->m_ioRing = XAbstractNetIoRing_global();
    }
#endif

    Set_Class_MemoryFree(self, XFree_System);
    return self;
}
// ===================================================================
// === 虚函数多态入口（_base 函数）====================================
// ===================================================================

bool XAbstractEventDispatcher_processEvents_base(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return false;
    return XClassGetVirtualFunc(self, EXAbstractEventDispatcher_ProcessEvents, bool(*)(XAbstractEventDispatcher*, XEventLoopProcessEventsFlags))(self, flags);
}

void XAbstractEventDispatcher_registerSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_RegisterSocketNotifier, void(*)(XAbstractEventDispatcher*, XSocketNotifier*))(self, notifier);
}

void XAbstractEventDispatcher_unregisterSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_UnregisterSocketNotifier, void(*)(XAbstractEventDispatcher*, XSocketNotifier*))(self, notifier);
}

void XAbstractEventDispatcher_registerTimer_base(XAbstractEventDispatcher* self, XTimerId timerId, XDuration interval, XTimerType timerType, XObject* obj)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_RegisterTimer, void(*)(XAbstractEventDispatcher*, XTimerId, XDuration, XTimerType, XObject*))(self, timerId, interval, timerType,obj);
}

bool XAbstractEventDispatcher_unregisterTimer_base(XAbstractEventDispatcher* self, XTimerId timerId)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return false;
    return XClassGetVirtualFunc(self, EXAbstractEventDispatcher_UnregisterTimer, bool(*)(XAbstractEventDispatcher*, XTimerId))(self, timerId);
}

bool XAbstractEventDispatcher_unregisterTimers_base(XAbstractEventDispatcher* self, XObject* object)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return false;
    return XClassGetVirtualFunc(self, EXAbstractEventDispatcher_UnregisterTimers, bool(*)(XAbstractEventDispatcher*, XObject*))(self, object);
}

XVector* XAbstractEventDispatcher_timersForObject_base(const XAbstractEventDispatcher* self, const XObject* object)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return NULL;
    return XClassGetVirtualFunc(self, EXAbstractEventDispatcher_TimersForObject, XVector * (*)(const XAbstractEventDispatcher*, const XObject*))(self, object);
}

XDuration XAbstractEventDispatcher_remainingTime_base(const XAbstractEventDispatcher* self, XTimerId timerId)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return -1;
    return XClassGetVirtualFunc(self, EXAbstractEventDispatcher_RemainingTime, XDuration(*)(const XAbstractEventDispatcher*, XTimerId))(self, timerId);
}

void XAbstractEventDispatcher_wakeUp_base(XAbstractEventDispatcher* self)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_WakeUp, void(*)(XAbstractEventDispatcher*))(self);
}

void XAbstractEventDispatcher_interrupt_base(XAbstractEventDispatcher* self)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_Interrupt, void(*)(XAbstractEventDispatcher*))(self);
}

void XAbstractEventDispatcher_startingUp_base(XAbstractEventDispatcher* self)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_StartingUp, void(*)(XAbstractEventDispatcher*))(self);
}

void XAbstractEventDispatcher_closingDown_base(XAbstractEventDispatcher* self)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_ClosingDown, void(*)(XAbstractEventDispatcher*))(self);
}

void XAbstractEventDispatcher_installNativeEventFilter(XAbstractEventDispatcher* self, struct XAbstractNativeEventFilter* filter)
{
    (void)self; // 保留参数以匹配 Qt 签名
    if (ISNULL(filter, "filter is NULL")) return;
    if (XObject_thread(self))return;//子线程不需要原生事件过滤器
    //XMutex_lock(self->d_ptr->mutex);
    Global_Lock;
    // 首次使用时创建 vector
    if (!global_nativeFilters) {
        global_nativeFilters = XVector_Create(void*); // 宏展开为 sizeof(void*)
    }

    // 检查是否已存在（避免重复）
    for (size_t i = 0; i < XVector_size_base(global_nativeFilters); ++i) {
        XAbstractNativeEventFilter* existing = XVector_At_Base(global_nativeFilters, i, XAbstractNativeEventFilter*);
        if (existing == filter) {
            return; // 已存在
        }
    }

    // 头插：插入到索引 0（实现“后安装的先调用”）
    XVector_Insert(global_nativeFilters, 0, void*, filter);
    //XMutex_unlock(self->d_ptr->mutex);
    Global_UnLock;
}

void XAbstractEventDispatcher_removeNativeEventFilter(XAbstractEventDispatcher* self, struct XAbstractNativeEventFilter* filter)
{
    (void)self;
    if (ISNULL(filter, "filter is NULL") || !global_nativeFilters) return;
    if (XObject_thread(self))return;//子线程不需要原生事件过滤器
    //XMutex_lock(self->d_ptr->mutex);
    Global_Lock;
    // 查找并移除
    for (size_t i = 0; i < XVector_size_base(global_nativeFilters); ++i) {
        XAbstractNativeEventFilter* existing = XVector_At_Base(global_nativeFilters, i, XAbstractNativeEventFilter*);
        if (existing == filter) {
            XVector_removeAt_base(global_nativeFilters, i);
            break;
        }
    }
    //XMutex_unlock(self->d_ptr->mutex);
    Global_UnLock;
}

// ===================================================================
// === 非虚函数实现 ==================================================
// ===================================================================

bool XAbstractEventDispatcher_filterNativeEvent(XAbstractEventDispatcher* self, const XByteArray* eventType, void* message, int64_t* result)
{
    (void)self;
    if (ISNULL(eventType, "eventType is NULL") || !global_nativeFilters) {
        return false;
    }
    //XMutex_lock(self->d_ptr->mutex);
    Global_Lock;
   for (size_t i = 0; i < XVector_size_base(global_nativeFilters); ++i) {
        XAbstractNativeEventFilter* filter = *(XAbstractNativeEventFilter**)XVector_at_base(global_nativeFilters, i);
        if (filter && XAbstractNativeEventFilter_nativeEventFilter_base(filter, eventType, message, result)) {
            //XMutex_unlock(self->d_ptr->mutex);
            Global_UnLock;
            return true;
        }
    }
    //XMutex_unlock(self->d_ptr->mutex);
   Global_UnLock;
    return false;
}


XTimerId XAbstractEventDispatcher_registerTimer(
    XAbstractEventDispatcher* self,
    XDuration interval,
    XTimerType timerType,
    XObject* object)
{
    if (ISNULL(self, "")) return XTIMER_INVALID_ID;
    XFd fd = XFd_alloc(XFD_TYPE_TIMER, NULL, object);
    if (fd < 0) return XTIMER_INVALID_ID;
    XAbstractEventDispatcher_registerTimer_base(self, (XTimerId)fd, interval, timerType, object);
    return (XTimerId)fd;
}

XAbstractEventDispatcher* XAbstractEventDispatcher_instance(XThread* thread)
{
    if(thread)
        return (XAbstractEventDispatcher*)XThread_dispatcher((XThread*)thread);
    return XCoreApplication_eventDispatcher();
}

// ===================================================================
// === 信号实现 ======================================================
// ===================================================================

void* XAbstractEventDispatcher_awake_signal(XAbstractEventDispatcher* self)
{
    if (self)
        XObject_emitSignal(self, XAbstractEventDispatcher_awake_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XAbstractEventDispatcher_awake_signal;
}

void* XAbstractEventDispatcher_aboutToBlock_signal(XAbstractEventDispatcher* self)
{
    if (self)
        XObject_emitSignal(self, XAbstractEventDispatcher_aboutToBlock_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XAbstractEventDispatcher_aboutToBlock_signal;
}

XDispatcherThreadType XAbstractEventDispatcher_threadType(XAbstractEventDispatcher* self)
{
    return self ? self->type: XDISPATCHER_THREAD_TYPE_WORKER;
}

bool XAbstractEventDispatcher_isMainThread(XAbstractEventDispatcher* self)
{
    return self ? self->type == XDISPATCHER_THREAD_TYPE_MAIN : false;
}
