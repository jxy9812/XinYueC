#include "XAbstractEventDispatcher.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XVector.h"
#include "XAbstractNativeEventFilter.h"
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
    dp->nativeFilters= XVector_Create(void*);
    dp->mutex = XMutex_create();
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
        XVTABLE_INHERIT_DEFAULT(XObject_class_init());

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
    XAbstractEventDispatcher* self = (XAbstractEventDispatcher*)XMemory_malloc(sizeof(XAbstractEventDispatcher));
    if (self) {
        XAbstractEventDispatcher_init(self, parent);
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
    (void)self; (void)flags;
    // 纯虚函数，子类必须重写
    return false;
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
    (void)self;
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

void XAbstractEventDispatcher_registerTimer_base(XAbstractEventDispatcher* self, XTimerId timerId, XDuration interval, XTimerType timerType, XObject* object)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXAbstractEventDispatcher_RegisterTimer, void(*)(XAbstractEventDispatcher*, XTimerId, XDuration, XTimerType, XObject*))(self, timerId, interval, timerType, object);
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
    XAbstractEventDispatcher* self,
    XDuration interval,
    XTimerType timerType,
    XObject* object)
{
    if (ISNULL(self, "")) return XTIMER_ID_INVALID;
    static XAtomic_uint64_t s_nextTimerId = {.value=1};
    //XTimerId id = 0;
    XTimerId id = XAtomic_fetch_add_size_t(&s_nextTimerId, 1);
    if (id == 0) id = XAtomic_fetch_add_size_t(&s_nextTimerId, 1); // 避免 0
    XAbstractEventDispatcher_registerTimer_base(self, id, interval, timerType, object);
    return id;
}

XAbstractEventDispatcher* XAbstractEventDispatcher_instance(XThread* thread)
{
    // 从线程或全局应用获取当前调度器
    //if (thread == NULL) {
    //    thread = XThread_currentThread();
    //}
    //if (thread) {
    //    return (XAbstractEventDispatcher*)XThread_getDispatcher((XThread*)thread);
    //}
    //return (XAbstractEventDispatcher*)XCoreApplication_getEventDispatcher();
    return NULL;
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