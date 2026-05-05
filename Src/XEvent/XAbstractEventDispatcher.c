#include "XAbstractEventDispatcher.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XVector.h"
#include "XTimeWheelGroup.h"
#include "XAbstractNativeEventFilter.h"
#include "XPriorityMapQueue.h"
#include "XThreadData.h"
#include "XThread.h"
#include <string.h>
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

// ===================================================================
// === 虚函数表初始化 ================================================
// ===================================================================

void XAbstractEventDispatcherPrivate_init(XAbstractEventDispatcherPrivate* dp)
{
    dp->nativeFilters = XVector_Create(void*);
    dp->m_timerIds = XVector_Create(XTimerId);
    dp->mutex = XMutex_create(XLock_NonRecursive);
}

void XAbstractEventDispatcherPrivate_deinit(XAbstractEventDispatcherPrivate * dp)
{
    if (dp->nativeFilters)
    {
        XVector_delete_base(dp->nativeFilters);
        dp->nativeFilters = NULL;
    }
    if (dp->mutex)
    {
        XMutex_delete(dp->mutex);
        dp->mutex = NULL;
    }
    if (dp->m_timerIds)
    {
        XVector_delete_base(dp->m_timerIds);
        dp->m_timerIds = NULL;
    }
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

}

// ===================================================================
// === 虚函数默认实现（纯虚函数应由子类重写，此处提供空/错误实现）===
// ===================================================================

static bool VXAbstractEventDispatcher_processEvents(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags)
{
    //先处理定时器任务
    if (XAbstractEventDispatcher_isMainThread(self)&& XTimeWheelGroup_GlobalExists())
    {
        //XPrintf("轮询定时器中\n");
        if(XTimeWheelGroup_count(XTimeWheelGroup_global()))
        {
            XTimeWheelGroup_handler(XTimeWheelGroup_global());
        }
    }
    size_t size = 0;
    //处理事件
    XVector* events = XThreadData_takePostedEvents();
    if (!events)
    {
      /*  if (XAbstractEventDispatcher_isMainThread(self))
            XThread_yieldCurrentThread();*/
        return false;
    }
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (!ePost|| !ePost->event) continue;
        // 根据 flags 排除特定事件类型
        if ((flags & XEventLoop_ExcludeUserInputEvents) && ePost->event->input_event == XEventLoop_ExcludeUserInputEvents)
        {
            //XCoreApplication_postEvent(ePost->receiver,ePost->event,ePost->priority);

            continue; // 跳过用户输入事件
        }
        if ((flags & XEventLoop_ExcludeSocketNotifiers) && ePost->event->type == XEVENT_TYPE_SOCK_ACT) {
            //XCoreApplication_postEvent(ePost->receiver, ePost->event, ePost->priority);
            continue; // 跳过 socket 事件
        }
        if ((flags & XEventLoop_X11ExcludeTimers) && ePost->event->type == XEVENT_TYPE_TIMER) {
            //XCoreApplication_postEvent(ePost->receiver, ePost->event, ePost->priority);
            continue; // 跳过定时器事件
        }
        /*if (ePost->event->type == XEVENT_TYPE_META_CALL)
        {
            XPrintf("XEventMetaCall:%p 出问题了\n", ePost->event);
        }*/
        if (!ePost->receiver||ePost->receiver->is_deleting_children)
        {
            XEvent_delete_base(ePost->event);
            ++size;
        }
        XEvent* e = ePost->event;
        ePost->event = NULL;//处理过的事件置空
        if(XCoreApplication_sendEvent(ePost->receiver, e))
           ++size;
    }
    //如果有未处理的事件，再次投递到事件队列头部，保证及时处理
    if(size< XVector_size_base(events))
        XThreadData_push_front_list(events);
    //XVector_clear_base(events);
    XVector_delete_base(events);

    return size;
}

static void VXAbstractEventDispatcher_registerSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    (void)self; (void)notifier;
    // 纯虚函数
}

static void VXAbstractEventDispatcher_unregisterSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    (void)self; (void)notifier;
    // 纯虚函数
}

static void VXAbstractEventDispatcher_registerTimer(XAbstractEventDispatcher* self, XTimerId timerId, XDuration interval, XTimerType timerType, XObject* object)
{
    (void)self; (void)timerId; (void)interval; (void)timerType; (void)object;
    // 纯虚函数
}

static bool VXAbstractEventDispatcher_unregisterTimer(XAbstractEventDispatcher* self, XTimerId timerId)
{
    (void)self; (void)timerId;
    return false;
}

static bool VXAbstractEventDispatcher_unregisterTimers(XAbstractEventDispatcher* self, XObject* object)
{
    (void)self; (void)object;
    return false;
}

static XVector* VXAbstractEventDispatcher_timersForObject(const XAbstractEventDispatcher* self, const XObject* object)
{
    (void)self; (void)object;
    return NULL;
}

static XDuration VXAbstractEventDispatcher_remainingTime(const XAbstractEventDispatcher* self, XTimerId timerId)
{
    (void)self; (void)timerId;
    return -1;
}

static void VXAbstractEventDispatcher_wakeUp(XAbstractEventDispatcher* self)
{
    return;
}

static void VXAbstractEventDispatcher_interrupt(XAbstractEventDispatcher* self)
{
    (void)self;
}

static void VXAbstractEventDispatcher_startingUp(XAbstractEventDispatcher* self)
{
    (void)self;
}

static void VXAbstractEventDispatcher_closingDown(XAbstractEventDispatcher* self)
{
    (void)self;
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
    XMutex_lock(self->d_ptr->mutex);
    // 首次使用时创建 vector
    if (!self->d_ptr->nativeFilters) {
        self->d_ptr->nativeFilters = XVector_Create(void*); // 宏展开为 sizeof(void*)
    }

    // 检查是否已存在（避免重复）
    for (size_t i = 0; i < XVector_size_base(self->d_ptr->nativeFilters); ++i) {
        XAbstractNativeEventFilter* existing = XVector_At_Base(self->d_ptr->nativeFilters, i, XAbstractNativeEventFilter*);
        if (existing == filter) {
            return; // 已存在
        }
    }

    // 头插：插入到索引 0（实现“后安装的先调用”）
    XVector_Insert(self->d_ptr->nativeFilters, 0, void*, filter);
    XMutex_unlock(self->d_ptr->mutex);
}

void XAbstractEventDispatcher_removeNativeEventFilter(XAbstractEventDispatcher* self, struct XAbstractNativeEventFilter* filter)
{
    (void)self;
    if (ISNULL(filter, "filter is NULL") || !self->d_ptr->nativeFilters) return;
    if (XObject_thread(self))return;//子线程不需要原生事件过滤器
    XMutex_lock(self->d_ptr->mutex);
    // 查找并移除
    for (size_t i = 0; i < XVector_size_base(self->d_ptr->nativeFilters); ++i) {
        XAbstractNativeEventFilter* existing = XVector_At_Base(self->d_ptr->nativeFilters, i, XAbstractNativeEventFilter*);
        if (existing == filter) {
            XVector_removeAt_base(self->d_ptr->nativeFilters, i);
            break;
        }
    }
    XMutex_unlock(self->d_ptr->mutex);
}

// ===================================================================
// === 非虚函数实现 ==================================================
// ===================================================================

bool XAbstractEventDispatcher_filterNativeEvent(XAbstractEventDispatcher* self, const XByteArray* eventType, void* message, int64_t* result)
{
    (void)self;
    if (ISNULL(eventType, "eventType is NULL") || !self->d_ptr->nativeFilters) {
        return false;
    }
    XMutex_lock(self->d_ptr->mutex);
   for (size_t i = 0; i < XVector_size_base(self->d_ptr->nativeFilters); ++i) {
        XAbstractNativeEventFilter* filter = *(XAbstractNativeEventFilter**)XVector_at_base(self->d_ptr->nativeFilters, i);
        if (filter && XAbstractNativeEventFilter_nativeEventFilter_base(filter, eventType, message, result)) {
            XMutex_unlock(self->d_ptr->mutex);
            return true;
        }
    }
    XMutex_unlock(self->d_ptr->mutex);
    return false;
}


XTimerId XAbstractEventDispatcher_registerTimer(
    XAbstractEventDispatcher* s,
    XDuration interval,
    XTimerType timerType,
    XObject* object)
{
    if (ISNULL(s, "")) return XTIMER_ID_INVALID;
    XAbstractEventDispatcher* self = XCoreApplication_eventDispatcher();
    static XAtomic_uint64_t s_nextTimerId = {.value=1};
    XTimerId id = 0;
    XMutex_lock(self->d_ptr->mutex);
    if(XVector_isEmpty_base(self->d_ptr->m_timerIds))
    {
        id = XAtomic_fetch_add_size_t(&s_nextTimerId, 1, XAtomic_MemoryOrder_Relaxed);
    }
    else
    {
        id = XVector_Back_Base(self->d_ptr->m_timerIds, XTimerId);
        XVector_pop_back_base(self->d_ptr->m_timerIds);
    }
    XMutex_unlock(self->d_ptr->mutex);
    
    if (id == 0) id = XAtomic_fetch_add_size_t(&s_nextTimerId, 1, XAtomic_MemoryOrder_Relaxed); // 避免 0
    XAbstractEventDispatcher_registerTimer_base(self, id, interval, timerType, object);
    return id;
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
