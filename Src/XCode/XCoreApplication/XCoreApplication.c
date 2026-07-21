#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHashFunc.h"
#include "XHashSet.h"
#include "XMutex.h"
#include "XObject.h"
#include "XString.h"
#include "XTimeWheelGroup.h"
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XTimer.h"
#include "XMultiPool.h"
#include <string.h>
#include <stdlib.h>
static XCoreApplication* g_app = NULL; // 全局应用程序实例
static XThread* g_mainThread = NULL; // Qt: QCoreApplicationPrivate::theMainThread
static bool is_app_running = false;   // Qt: QCoreApplicationPrivate::is_app_running
static bool is_app_closing = false;   // Qt: QCoreApplicationPrivate::is_app_closing
static bool setuidAllowed = false;    // Qt: QCoreApplicationPrivate::setuidAllowed
bool VXCoreApplication_notify(XObject* receiver, XEvent* e);
bool VXCoreApplication_event(XObject* self, XEvent* e);
//static void VXObject_timerEvent(XCoreApplication* app, XTimerEvent* event);
static void VXCoreApplication_deinit(XCoreApplication* app);
XVtable* XCoreApplication_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XCoreApplication))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { VXCoreApplication_notify };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXCoreApplication_event);
    //XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXObject_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCoreApplication_deinit);
#if SHOWCONTAINERSIZE
        printf("XCoreApplication size:%d\n", XVtable_size(XClassVtable));
#endif
    return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_instance() {
    return g_app;
}

XCoreApplication* XCoreApplication_create(int argc, char** argv) {
    // 确保全局实例唯一
    if (g_app != NULL)
        return g_app;

    // 创建并初始化应用程序实例
    XCoreApplication* app = XMalloc_System(sizeof(XCoreApplication));
    if (!app) return NULL;

    XCoreApplication_init(app, argc, argv);
    Set_Class_MemoryFree(app, XFree_System);
    return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv) {
    if (app == NULL)
        return;
    //初始化内存池
    XMultiPool_global();
    //初始化全局时间轮
    //XTimeWheelGroup_global();
    memset(((XObject*)app)+1,0,sizeof(XCoreApplication)-sizeof(XObject));
    // 初始化父类
    XObject_init(app);
    XClassGetVtable(app) = XCoreApplication_class_init();

    // 初始化成员变量
    app->m_argc = argc;
    app->m_argv = argv;
    g_mainThread = XThread_createMainThread(app);
    // Update app's threadData to main thread's XThreadData (already set by XObject_init
    // but we ensure it points to the actual main thread data)
    if (g_mainThread && g_mainThread->m_data) {
        XThreadData_ref(g_mainThread->m_data);
        XThreadData* oldTd = (XThreadData*)XAtomic_exchange_uintptr_t(
            &((XObject*)app)->m_threadData, (uintptr_t)g_mainThread->m_data,
            XAtomic_MemoryOrder_AcqRel);
        XThreadData_deref(oldTd);
    }
    //app->m_eventLoop = XEventLoop_create();
    //app->m_eventLoop = NULL;
    XBitArray_init(&app->m_attribute, XCORE_APPLICATION_ATTRIBUTE_COUNT,false);


    // 设置全局实例
    g_app = app;

    // Qt: 标记应用正在运行
    is_app_running = true;
    app->m_in_exec = false;
    app->m_aboutToQuitEmitted = false;
}


void XCoreApplication_setApplicationName(const XString* applicationName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app||!applicationName)return;
    if (!app->m_applicationName)
        app->m_applicationName = XString_create();
    if(XString_compare(app->m_applicationName, applicationName)!=XCompare_Equality)
    {
        XString_assign(app->m_applicationName, applicationName);
        XCoreApplication_applicationNameChanged_signal(xApp);
    }

}

const XString* XCoreApplication_applicationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app?app->m_applicationName:NULL;
}

void XCoreApplication_setApplicationVersion(const XString* version)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !version)return;
    if (!app->m_version)
        app->m_version = XString_create();
    if (XString_compare(app->m_version, version) != XCompare_Equality)
    {
        XString_assign(app->m_version, version);
        XCoreApplication_applicationVersionChanged_signal(xApp);
    }
}

const XString* XCoreApplication_applicationVersion(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_version : NULL;
}

void XCoreApplication_setOrganizationName(const XString* orgName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !orgName)return;
    if (!app->m_orgName)
        app->m_orgName = XString_create();
    if (XString_compare(app->m_orgName, orgName) != XCompare_Equality)
    {
        XString_assign(app->m_orgName, orgName);
        XCoreApplication_organizationNameChanged_signal(xApp);
    }
}

const XString* XCoreApplication_organizationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgName : NULL;
}

void XCoreApplication_setOrganizationDomain(const XString* orgDomain)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !orgDomain)return;
    if (!app->m_orgDomain)
        app->m_orgDomain = XString_create();
    if (XString_compare(app->m_orgDomain, orgDomain) != XCompare_Equality)
    {
        XString_assign(app->m_orgDomain, orgDomain);
        XCoreApplication_organizationDomainChanged_signal(xApp);
    }
}

const XString* XCoreApplication_organizationDomain(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgDomain : NULL;
}

void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return;
    XBitArray_setBit(&app->m_attribute, (size_t)attribute,on);
}

bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return false;
    return XBitArray_getBit(&app->m_attribute, (size_t)attribute);
}

XStringList* XCoreApplication_arguments(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return NULL;
    XStringList* list = XStringList_create();
    for (size_t i = 0; i < app->m_argc; i++)
    {
        XStringList_push_back_utf8(list, app->m_argv[i]);
    }
    return list;
}

const XString* XCoreApplication_applicationDirPath(void)
{
    return NULL;
}

const XString* XCoreApplication_applicationFilePath(void)
{
    return NULL;
}
int64_t XCoreApplication_applicationPid(void)
{
    return 0;
}

void XCoreApplication_exit(int returnCode)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)
        return;

    // Qt 6.8: 发出 aboutToQuit 信号（仅第一次）
    if (!app->m_aboutToQuitEmitted) {
        app->m_aboutToQuitEmitted = true;
        XCoreApplication_aboutToQuit_signal(app);
    }

    // Qt 6.8: 设置 quitNow 标志，通知所有事件循环退出
    XThreadData* data = XObject_threadData((XObject*)app);
    if (data) {
        data->m_quitNow = true;
        // Qt 6.8: 遍历所有嵌套 event loop 调用 exit()
        // XThreadData 中的 m_eventLoops 栈
        for (size_t i = 0; i < XStack_size_base(&data->m_eventLoops); ++i) {
            XEventLoop** pLoop = (XEventLoop**)XVector_at_base(&data->m_eventLoops, i);
            if (pLoop && *pLoop) {
                XEventLoop_exit(*pLoop, returnCode);
            }
        }
    }
}

void XCoreApplication_quit()
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)
        return;

    // Qt 6.8: quit() 在没有 exec 时不执行任何操作
    if (!app->m_in_exec)
        return;

    // Qt 6.8: quit() 通过 QEvent::Quit 事件机制
    // 主线程: sendEvent(self, QEvent::Quit) → event() → exit(0)
    // 其他线程: postEvent(self, QEvent::Quit)
    if (XThread_currentThread() == g_mainThread) {
        // 主线程: 直接发送 Quit 事件
        XEvent quitEvent;
        XEvent_init(&quitEvent, XEVENT_TYPE_QUIT);
        XCoreApplication_sendEvent((XObject*)app, &quitEvent);
    } else {
        // 其他线程: 投递 Quit 事件
        XEvent* quitEvent = XEvent_create(XEVENT_TYPE_QUIT);
        if (quitEvent)
            XCoreApplication_postEvent((XObject*)app, quitEvent, XEVENT_PRIORITY_HIGHEST);
    }
}

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags) 
{
    XThreadData* data = XThreadData_current();
    if(data)
        XAbstractEventDispatcher_processEvents_base(data->m_eventDispatcher, flags);
}

void XCoreApplication_processEventsTimed(XEventLoopProcessEventsFlags flags, int maxtime)
{
    XTimer* timer = XTimer_create();
    XTimer_setInterval(timer,maxtime);
    XTimer_setSingleShot(timer,true);
    XTimer_start_base(timer);
    while (XTimer_isRunning(timer))
    {
        XCoreApplication_processEvents(flags);
    }
    XTimer_deleteLater(timer);
}

void XCoreApplication_installNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher_installNativeEventFilter(XCoreApplication_eventDispatcher(),filter);
}

void XCoreApplication_removeNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher_removeNativeEventFilter(XCoreApplication_eventDispatcher(), filter);
}

bool XCoreApplication_notify_base(XObject* receiver, XEvent* e)
{
    if (ISNULL(receiver, "") || ISNULL(XClassGetVtable(receiver), ""))
        return false;
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)
        return VXCoreApplication_notify(receiver, e);
    return XClassGetVirtualFunc(app, EXCoreApplication_Notify, bool(*)(XObject*, XEvent*))(receiver, e);
}
int XCoreApplication_exec() 
{
    XCoreApplication* app = XCoreApplication_instance();
    if (app == NULL)
        return -1;

    // Qt 6.8: 检查是否在主线程
    XThread* currentThread = XThread_currentThread();
    if (currentThread != g_mainThread) {
        XERROR_PRINTF("XCoreApplication::exec: Must be called from the main thread\n");
        return -1;
    }

    // Qt 6.8: 检查事件循环是否已在运行
    XThreadData* data = XObject_threadData((XObject*)app);
    if (data && XStack_size_base(&data->m_eventLoops) > 0) {
        XERROR_PRINTF("XCoreApplication::exec: The event loop is already running\n");
        return -1;
    }

    // Qt 6.8: 重置退出标志
    if (data)
        data->m_quitNow = false;
    app->m_in_exec = true;
    app->m_aboutToQuitEmitted = false;

    // Qt 6.8: 创建 QEventLoop 并调用 exec(ApplicationExec)
    XEventLoop eventLoop;
    XEventLoop_init(&eventLoop);
    int result = XEventLoop_exec(&eventLoop, XEventLoop_ApplicationExec);

    // Qt 6.8: execCleanup - 发送 DeferredDelete 事件
    if (data)
        data->m_quitNow = false;
    app->m_in_exec = false;
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    return result;
}

bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) {
        return false;
    }
    // Qt 6.8: 标记为非自发事件（对标 event->m_spont = false）
    event->spontaneous = false;
    // 【核心】将事件分发工作交给 notify
    return XCoreApplication_notify_base(receiver, event);
}

void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority)
{
    if (!receiver || !event)
        return;

    // Qt 6.8: 事件压缩 — 相同 timerId 的 Timer 事件合并
    if (event->type == XEVENT_TYPE_TIMER && XAtomic_load_int32(&receiver->m_posted_events, XAtomic_MemoryOrder_Relaxed) > 0) {
        XThreadData* td = XThreadData_lockPostEventList(receiver);
        if (td) {
            XTimerEvent* newTimerEvent = (XTimerEvent*)event;
            bool compressed = false;
            for (size_t i = 0; i < XVector_size_base(&td->m_postEventList); ++i) {
                XPostEvent* pe = (XPostEvent*)XVector_at_base(&td->m_postEventList, i);
                if (pe && pe->event && pe->receiver == receiver
                    && pe->event->type == XEVENT_TYPE_TIMER) {
                    XTimerEvent* existing = (XTimerEvent*)pe->event;
                    if (existing->timerId == newTimerEvent->timerId) {
                        // 压缩：删除新事件，保留旧的
                        XEvent_delete_base(event);
                        compressed = true;
                        break;
                    }
                }
            }
            XMutex_unlock(td->m_mutex);
            if (compressed)
                return;
        }
    }

    // Qt 6.8: 事件压缩 — 相同接收者的 Quit 事件去重
    if (event->type == XEVENT_TYPE_QUIT && XAtomic_load_int32(&receiver->m_posted_events, XAtomic_MemoryOrder_Relaxed) > 0) {
        XThreadData* td = XThreadData_lockPostEventList(receiver);
        if (td) {
            bool compressed = false;
            for (size_t i = 0; i < XVector_size_base(&td->m_postEventList); ++i) {
                XPostEvent* pe = (XPostEvent*)XVector_at_base(&td->m_postEventList, i);
                if (pe && pe->event && pe->receiver == receiver
                    && pe->event->type == XEVENT_TYPE_QUIT) {
                    XEvent_delete_base(event);
                    compressed = true;
                    break;
                }
            }
            XMutex_unlock(td->m_mutex);
            if (compressed)
                return;
        }
    }

    XThreadData_postEvent(receiver, event, priority);
}

bool XCoreApplication_tryPostEvent(XObject* receiver, XEvent* event, int priority)
{
    return XThreadData_tryPostEvent(receiver, event, priority);
}

void XCoreApplication_sendPostedEvents(XObject * receiver, XEventType eventType)
{
    XThreadData* data = XThreadData_current();
    if (!data) return;

    XVector* events = XThreadData_takePostedEvents();
    if (!events) return;
    if (!XThreadData_pushActivePostedEvents(events)) {
        XThreadData_push_front_list(events);
        XVector_delete_base(events);
        return;
    }

    // Qt 6.8: 记录插入偏移，防止活锁
    size_t insertionOffset = XVector_size_base(events);
    size_t i = 0;
    while (i < XVector_size_base(events)) {
        if (i >= insertionOffset) break;

        XPostEvent* ePost = (XPostEvent*)XVector_at_base(events, i);
        ++i;

        if (!ePost || !ePost->event) continue;
        if (receiver && ePost->receiver != receiver) {
            XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
            continue;
        }
        if (eventType && eventType != ePost->event->type) {
            XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
            continue;
        }

        // Qt 6.8: DeferredDelete 事件处理 — 不可投递时重新投递
        if (ePost->event->type == XEVENT_TYPE_DEFERRED_DELETE) {
            if (!XDeferredDeleteEvent_shouldDeliver(
                    (XDeferredDeleteEvent*)ePost->event,
                    data,
                    eventType == XEVENT_TYPE_DEFERRED_DELETE)) {
                // Qt 6.8: 重新投递 — 复制事件，原位置置空，追加到列表末尾
                XPostEvent pe_copy = *ePost;
                ePost->event = NULL;
                if (!XVector_push_back_1_base(events, &pe_copy)) {
                    // 无法重新投递，丢弃
                    XThreadData_discardPostedEvent(&pe_copy);
                }
                continue;
            }
        }

        XThreadData_deliverPostedEvent(ePost);
    }

    XThreadData_popActivePostedEvents(events);
    XThreadData_push_front_list(events);
    XVector_delete_base(events);
}

void XCoreApplication_removePostedEvents(XObject * receiver, XEventType eventType)
{
    XThreadData_discardActivePostedEvents(receiver, eventType);
    XVector* events = XThreadData_takePostedEvents();
    if (!events)return;
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (!ePost || !ePost->event) continue;
        if (receiver && ePost->receiver != receiver)
            continue;//如果有指定的接收者，跳过其他接收者
        if (eventType && eventType != ePost->event->type)
            continue;//如果有指定的事件类型，跳过其他事件
        XThreadData_discardPostedEvent(ePost);
    }
    XThreadData_push_front_list(events);
    XVector_delete_base(events);
}

XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return NULL;
    XThreadData* td = XObject_threadData((XObject*)app);
    return td ? td->m_eventDispatcher : NULL;
}

void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher)
{
    // Qt 6.8: setEventDispatcher 委托给主线程
    if (!g_mainThread) {
        g_mainThread = XThread_currentThread();
    }
    XThreadData* data = XObject_threadData((XObject*)g_mainThread);
    if (!data) return;

    // Qt 6.8: 如果已有事件分发器，打印警告并忽略（对标 QThread::setEventDispatcher 行为）
    if (data->m_eventDispatcher != NULL && dispatcher != NULL) {
        XERROR_PRINTF("XCoreApplication::setEventDispatcher: "
                       "Cannot set event dispatcher when one is already installed\n");
        return;
    }

    // Qt 6.8: 清理旧的事件分发器
    if (data->m_eventDispatcher != NULL && dispatcher == NULL) {
        XObject_deleteLater((XObject*)(data->m_eventDispatcher));
    }

    data->m_eventDispatcher = dispatcher;
}

void XCoreApplication_setLibraryPaths(const XStringList* paths)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return;
    if (!app->m_paths)app->m_paths = XStringList_create();
    XString_copy_base(app->m_paths,paths);
}

const XStringList* XCoreApplication_libraryPaths(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app?app->m_paths:NULL;
}

void XCoreApplication_addLibraryPath(const XString* path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app|| !path)return;
    if (!app->m_paths)app->m_paths = XStringList_create();
    XStringList_push_back_base(app->m_paths, path);
}

void XCoreApplication_removeLibraryPath(const XString * path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_paths||!path)return;
    XStringList_remove_base(app->m_paths, XStringList_indexOf(app->m_paths, path, 0, XChar_CaseSensitive),1);
}

void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app) 
{
    XEmitSignal(app, XCoreApplication_aboutToQuit_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}

void* XCoreApplication_applicationNameChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_applicationNameChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_applicationVersionChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_applicationVersionChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_organizationDomainChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_organizationDomainChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_organizationNameChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_organizationNameChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

bool VXCoreApplication_notify(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return true;

    // Qt 6.8: 应用关闭中不派发事件
    if (is_app_closing)
        return true;

    XThreadData* currentData = XThreadData_current();
    XThreadData* receiverData = XObject_threadData(receiver);
    if (receiverData && receiverData != currentData)
        return false;
    if (receiverData)
        ++receiverData->m_scopeLevel;

    // Qt 6.8: 应用级事件过滤器（对标 QCoreApplicationPrivate::sendThroughApplicationEventFilters）
    // 仅在主线程中执行
    if (XThread_currentThread() == g_mainThread && g_app) {
        XVector* appFilters = ((XObject*)g_app)->m_filters;
        if (appFilters) {
            for_each_iterator(appFilters, XVector, it)
            {
                XObject* filter = *((XObject**)XVector_iterator_data(&it));
                if (!filter) continue;
                if (XObject_eventFilter_base(filter, receiver, event))
                    return true;
            }
        }
    }

    bool handled = false;

    XVector* filters = receiver->m_filters;
    if (filters)
    {
        for_each_iterator(filters, XVector, it)
        {
            XObject* filter = *((XObject**)XVector_iterator_data(&it));
            if (!filter || XObject_threadData(filter) != receiverData)
                continue;
            if (XObject_eventFilter_base(filter, receiver, event)) {
                handled = true;
                goto done;
            }
        }
    }

    handled = XObject_event_base(receiver, event);
    if (!handled && XObject_isWidgetType(receiver))
    {
        XObject* parent = XObject_parent(receiver);
        if(parent)
            handled = XCoreApplication_notify_base(parent, event);
    }

done:
    if (receiverData)
        --receiverData->m_scopeLevel;
    return handled;
}

/* ==================== sendSpontaneousEvent（对标 Qt 6.8 QCoreApplication::sendSpontaneousEvent） ==================== */

bool XCoreApplication_sendSpontaneousEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) {
        return false;
    }
    // Qt 6.8: 标记为自发事件（对标 event->m_spont = true）
    event->spontaneous = true;
    // 通过 notify 分发
    return XCoreApplication_notify_base(receiver, event);
}

/* ==================== quitLock 管理（对标 Qt 6.8 QCoreApplication::isQuitLockEnabled / setQuitLockEnabled） ==================== */

static int quitLockRef = 1;  // Qt 默认 quitLockEnabled = true
static bool quitLockEnabled = true;

bool XCoreApplication_isQuitLockEnabled(void)
{
    return quitLockEnabled;
}

void XCoreApplication_setQuitLockEnabled(bool enabled)
{
    quitLockEnabled = enabled;
    if (!enabled && quitLockRef <= 0) {
        // Qt 6.8: 如果 quitLock 被禁用且没有引用，自动退出
        XCoreApplication_quit();
    }
}

//void VXObject_timerEvent(XCoreApplication* app, XTimerEvent* event)
//{
//    app->m_quit = true;//XCoreApplication_processEventsWithMaxTime 定时简单处理，如果类中有多个定时就要添加标志位单独处理
//}

/* ==================== QCoreApplication::event 实现（对标 Qt 6.8 QCoreApplication::event） ==================== */

bool VXCoreApplication_event(XObject* self, XEvent* e)
{
    // Qt 6.8: 处理 QEvent::Quit → exit(0)
    if (e->type == XEVENT_TYPE_QUIT) {
        XCoreApplication_exit(0);
        return true;
    }
    // Qt 6.8: 其他事件交给父类处理（使用 XClass_Parent 避免递归）
    return XClass_Parent(XObject, EXObject_Event, bool(*)(XObject*, XEvent*))(self, e);
}

/* ==================== 应用状态（对标 Qt QCoreApplication::startingUp / closingDown） ==================== */

bool XCoreApplication_startingUp(void)
{
    return !is_app_running;
}

bool XCoreApplication_closingDown(void)
{
    return is_app_closing;
}

/* ==================== setuid 安全（对标 Qt QCoreApplication::setSetuidAllowed / isSetuidAllowed） ==================== */

void XCoreApplication_setSetuidAllowed(bool allow)
{
    setuidAllowed = allow;
}

bool XCoreApplication_isSetuidAllowed(void)
{
    return setuidAllowed;
}


//{
//    app->m_quit = true;//XCoreApplication_processEventsWithMaxTime 定时简单处理，如果类中有多个定时就要添加标志位单独处理
//}

void VXCoreApplication_deinit(XCoreApplication* app)
{
    if (!app) return;

    // Qt 6.8: 标记应用正在关闭
    is_app_closing = true;
    is_app_running = false;

    // 释放事件循环
   /* if (app->m_eventLoop)
    {
        XEventLoop_deleteLater(app->m_eventLoop);
        app->m_eventLoop = NULL;
    }*/

    // 释放父类资源
    XClass_Deinit_Parent(XObject, app);

    // 清除全局实例
    if (g_app == app) {
        g_app = NULL;
    }
}
