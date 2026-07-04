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
}

void XAbstractEventDispatcherPrivate_deinit(XAbstractEventDispatcherPrivate * dp)
{
    if (dp->m_hrtimerGroup)
    {
        XClass_delete_base(dp->m_hrtimerGroup);
        dp->m_hrtimerGroup = NULL;
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

    /* 分配并初始化私有数据 */
    self->d_ptr = (XAbstractEventDispatcherPrivate*)XMalloc_System(sizeof(XAbstractEventDispatcherPrivate));
    if (self->d_ptr) {
        XAbstractEventDispatcherPrivate_init(self->d_ptr);
    }

    global_init();
    //self->m_hrtimerGroup = XHrTimerGroup_create(1);
  
}

// ===================================================================
// === 虚函数默认实现（纯虚函数应由子类重写，此处提供空/错误实现）===
// ===================================================================

static bool VXAbstractEventDispatcher_processEvents(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags)
{
    //先处理定时器任务
    if (XAbstractEventDispatcher_isMainThread(self) && XTimeWheelGroup_GlobalExists())
    {
        //XPrintf("轮询定时器中\n");
        if (XTimeWheelGroup_count(XTimeWheelGroup_global()))
        {
            XTimeWheelGroup_handler_base(XTimeWheelGroup_global());
        }
    }
    if (self->d_ptr->m_hrtimerGroup)
    {
        XHrTimerGroup_handler_base(self->d_ptr->m_hrtimerGroup);
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
        //if (ePost->event->type == XEVENT_TYPE_META_CALL)
        //{
          //  XEventMetaCall* e = ePost->event;
            //if(e->sem)
            //XPrintf("VXAbstractEventDispatcher_processEvents XEventMetaCall:%p 出问题了&p\n", ePost->event ,e->sem);
        //}
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
    if (!notifier)return;
    XFd fd = XSocketNotifier_socket(notifier);
    if (fd < 0) return;
    XFileDescriptor* desc = XFd_get(fd);
    if (!desc) return;

    /* 从 desc->handle 获取 XNetworkSocketPrivate* */
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)desc->handle;
    if (!priv) return;

    /* 首次注册时分配 XVector */
    if (!priv->notifiers)
    {
        XVector* v = (XVector*)XMalloc_Hybrid(sizeof(XVector));
        if (!v) return;
        XVector_init(v, sizeof(XSocketNotifier*), false);
        XContainerSetCompare(v, uintptr_t_compare);
        priv->notifiers = v;
    }
    if (XVector_indexOf(priv->notifiers, &notifier, 0) == -1)
    {
        XVector_append_1_base(priv->notifiers, &notifier);
    }
}

static void VXAbstractEventDispatcher_unregisterSocketNotifier(XAbstractEventDispatcher* self, XSocketNotifier* notifier)
{
    if (!notifier)return;
    XFd fd = XSocketNotifier_socket(notifier);
    if (fd < 0) return;
    XFileDescriptor* desc = XFd_get(fd);
    if (!desc) return;

    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)desc->handle;
    if (!priv || !priv->notifiers) return;

    int index = XVector_indexOf(priv->notifiers, &notifier, 0);
    if (index != -1)
    {
        XVector_remove_base(priv->notifiers, index, 1);
    }
    if (XVector_isEmpty_base(priv->notifiers))
    {
        XVector_deinit_base(priv->notifiers);
        XFree_Hybrid(priv->notifiers);
        priv->notifiers = NULL;
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
