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
#include "XAbstractNativeEventFilter.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static XCoreApplication* g_app = NULL;
static XThread* g_mainThread = NULL;
static bool is_app_running = false;
static bool is_app_closing = false;
static bool setuidAllowed = false;
/* 当前 QCoreApplication::exec 对应的栈事件循环；用于安全退出当前循环。 */
static XEventLoop* g_execLoop = NULL;

bool VXCoreApplication_notify(XObject* receiver, XEvent* e);
bool VXCoreApplication_event(XObject* self, XEvent* e);

/* ==================== compressEvent 实现（对标 Qt 6.8 QCoreApplication::compressEvent） ==================== */
/* Qt 6.8: compressEvent 是非虚函数 */

bool XCoreApplication_compressEvent(XEvent* event, XObject* receiver, void* postedEvents)
{
    if (!event || !receiver || !postedEvents)
        return false;

    XVector* postList = (XVector*)postedEvents;

    if (event->type == XEVENT_TYPE_TIMER) {
        int timerId = ((XTimerEvent*)event)->timerId;
        for (size_t i = 0; i < XVector_size_base(postList); ++i) {
            XPostEvent* pe = (XPostEvent*)XVector_at_base(postList, i);
            if (pe && pe->event && pe->event->type == XEVENT_TYPE_TIMER && pe->receiver == receiver) {
                if (((XTimerEvent*)pe->event)->timerId == timerId) {
                    XEvent_delete_base(event);
                    return true;
                }
            }
        }
        return false;
    }

    if (event->type == XEVENT_TYPE_QUIT) {
        for (size_t i = 0; i < XVector_size_base(postList); ++i) {
            XPostEvent* pe = (XPostEvent*)XVector_at_base(postList, i);
            if (pe && pe->event && pe->event->type == XEVENT_TYPE_QUIT && pe->receiver == receiver) {
                XEvent_delete_base(event);
                return true;
            }
        }
        return false;
    }

    return false;
}

void VXCoreApplication_deinit(XCoreApplication* app);

XVtable* XCoreApplication_class_init() {
    XVTABLE_INIT_DEFAULT(XCoreApplication)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* Qt 6.8: QCoreApplication 重载 notify 和 event 虚函数 */
    void* table[] = { VXCoreApplication_notify, VXCoreApplication_event };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCoreApplication_deinit);
        XCLASS_SHOW_SIZE_DEFAULT(XCoreApplication);
    return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_instance() {
    return g_app;
}

XCoreApplication* XCoreApplication_create(int argc, char** argv) {
    if (g_app != NULL)
        return g_app;

    XCoreApplication* app = XMalloc_System(sizeof(XCoreApplication));
    if (!app) return NULL;

    XCoreApplication_init(app, argc, argv);
    Set_Class_MemoryFree(app, XFree_System);
    return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv) {
    if (app == NULL)
        return;
    XMultiPool_global();
    memset(((XObject*)app)+1,0,sizeof(XCoreApplication)-sizeof(XObject));
    XObject_init(app);
    XClassGetVtable(app) = XCoreApplication_class_init();
    XBitArray_init(&app->m_attribute, XCORE_APPLICATION_ATTRIBUTE_COUNT, false);

    app->m_argc = argc;
    app->m_argv = argv;
#if XTHREAD_ON
    g_mainThread = XThread_createMainThread(app);
    /* XThread_createMainThread 内部已通过 ensureEventDispatcher 创建事件分发器 */
    /* 不需要再手动创建，否则会泄漏前一个分发器 */
#else
    XThreadData* td = XThreadData_current();
    if (td) {
        XThreadData_initMainThread(NULL);
        XThreadData_ensureEventDispatcher(td);
    }
#endif

    g_app = app;
    is_app_running = true;
    is_app_closing = false;
}

/* ==================== 应用程序元信息 ==================== */

void XCoreApplication_setApplicationName(const XString* applicationName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    if (app->m_applicationName) XString_delete_base(app->m_applicationName);
    app->m_applicationName = applicationName ? XString_create_copy(applicationName) : NULL;
    XCoreApplication_applicationNameChanged_signal(app);
}

const XString* XCoreApplication_applicationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_applicationName : NULL;
}

void XCoreApplication_setApplicationVersion(const XString* version)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    if (app->m_version) XString_delete_base(app->m_version);
    app->m_version = version ? XString_create_copy(version) : NULL;
    XCoreApplication_applicationVersionChanged_signal(app);
}

const XString* XCoreApplication_applicationVersion(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_version : NULL;
}

void XCoreApplication_setOrganizationName(const XString* orgName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    if (app->m_orgName) XString_delete_base(app->m_orgName);
    app->m_orgName = orgName ? XString_create_copy(orgName) : NULL;
    XCoreApplication_organizationNameChanged_signal(app);
}

const XString* XCoreApplication_organizationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgName : NULL;
}

void XCoreApplication_setOrganizationDomain(const XString* orgDomain)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    if (app->m_orgDomain) XString_delete_base(app->m_orgDomain);
    app->m_orgDomain = orgDomain ? XString_create_copy(orgDomain) : NULL;
    XCoreApplication_organizationDomainChanged_signal(app);
}

const XString* XCoreApplication_organizationDomain(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgDomain : NULL;
}

/* ==================== 应用程序属性 ==================== */

void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    XBitArray_setBit(&app->m_attribute, attribute, on);
}

bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? XBitArray_testBit(&app->m_attribute, attribute) : false;
}

/* ==================== 命令行参数 ==================== */

XStringList* XCoreApplication_arguments(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_argv) return NULL;

    XStringList* args = XStringList_create();
    if (!args) return NULL;

    for (int i = 0; i < app->m_argc; ++i) {
        if (app->m_argv[i]) {
            XString* s = XString_create_utf8(app->m_argv[i]);
            if (s) {
                XStringList_push_back_move_base(args, s);
                XString_delete_base(s);
            }
        }
    }
    return args;
}

/* ==================== 应用程序路径 ==================== */

const XString* XCoreApplication_applicationDirPath(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_argv || !app->m_argv[0]) return NULL;

    const char* path = app->m_argv[0];
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash) {
        return XString_create_utf8(".");
    }

    size_t len = lastSlash - path;
    char* dir = (char*)XMalloc_System(len + 1);
    if (!dir) return NULL;
    memcpy(dir, path, len);
    dir[len] = '\0';
    XString* result = XString_create_utf8(dir);
    XFree_System(dir);
    return result;
}

const XString* XCoreApplication_applicationFilePath(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_argv || !app->m_argv[0]) return NULL;
    return XString_create_utf8(app->m_argv[0]);
}

int64_t XCoreApplication_applicationPid(void)
{
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

/* ==================== 事件循环控制 ==================== */

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags)
{
    XThreadData* data = XThreadData_current();
    if (data && data->m_eventDispatcher)
        XAbstractEventDispatcher_processEvents_base(data->m_eventDispatcher, flags);
}

/* Qt 6.8: QCoreApplication::processEvents(QEventLoop::ProcessEventsFlags flags, int ms) */
void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime)
{
    (void)maxtime;
    XThreadData* data = XThreadData_current();
    if (data && data->m_eventDispatcher) {
        XAbstractEventDispatcher_processEvents_base(data->m_eventDispatcher, flags);
    }
}

/* ==================== 权限系统 ==================== */

XPermissionStatus XCoreApplication_checkPermission(const XPermission* permission)
{
    if (!permission) return XPERMISSION_STATUS_DENIED;
    /* Qt 6.8: 默认返回 GRANTED */
    return XPERMISSION_STATUS_GRANTED;
}

void XCoreApplication_requestPermission(XPermission* permission, XPermissionCallback callback, void* userData)
{
    if (!permission) return;
    permission->status = XPERMISSION_STATUS_GRANTED;
    if (callback)
        callback(permission, userData);
}

/* ==================== 原生事件过滤器 ==================== */

void XCoreApplication_installNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher* ed = XCoreApplication_eventDispatcher();
    if (ed)
        XAbstractEventDispatcher_installNativeEventFilter(ed, filter);
}

void XCoreApplication_removeNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher* ed = XCoreApplication_eventDispatcher();
    if (ed)
        XAbstractEventDispatcher_removeNativeEventFilter(ed, filter);
}

/* ==================== notify_base ==================== */

bool XCoreApplication_notify_base(XObject* receiver, XEvent* e)
{
    if (!receiver || !XClassGetVtable(receiver))
        return false;
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)
        return VXCoreApplication_notify(receiver, e);
    return XClassGetVirtualFunc(app, EXCoreApplication_Notify, bool(*)(XObject*, XEvent*))(receiver, e);
}

/* ==================== exec（对标 Qt 6.8 QCoreApplication::exec） ==================== */

int XCoreApplication_exec()
{
    XCoreApplication* app = XCoreApplication_instance();
    if (app == NULL)
        return -1;

    XThread* currentThread = XThread_currentThread();
    if (currentThread != g_mainThread) {
        fprintf(stderr, "XCoreApplication::exec: 必须在主线程中调用\n");
        return -1;
    }

    XThreadData* data = XObject_threadData((XObject*)app);
    if (data && XStack_size_base(&data->m_eventLoops) > 0) {
        fprintf(stderr, "XCoreApplication::exec: 事件循环已在运行\n");
        return -1;
    }

    if (data)
        data->m_quitNow = false;
    app->m_in_exec = true;
    app->m_aboutToQuitEmitted = false;

    XEventLoop eventLoop;
    XEventLoop_init(&eventLoop);
    g_execLoop = &eventLoop;
    int result = XEventLoop_exec(&eventLoop, XEventLoop_ApplicationExec);
    g_execLoop = NULL;

    /* Qt 6.8: execCleanup — 事件循环结束后发送 DeferredDelete 事件 */
    if (data)
        data->m_quitNow = false;
    app->m_in_exec = false;
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    return result;
}

/* ==================== exit（对标 Qt 6.8 QCoreApplication::exit） ==================== */

void XCoreApplication_exit(int returnCode)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;

    /* exec() 的循环对象是栈对象，直接退出它可以避免在事件回调中遍历
       正在变化的线程事件循环栈。 */
    if (g_execLoop) {
        XEventLoop_exit(g_execLoop, returnCode);
        return;
    }

    /* Qt 6.8: 发出 aboutToQuit 信号（仅第一次），在设置 quitNow 之前 */
    if (!app->m_aboutToQuitEmitted) {
        app->m_aboutToQuitEmitted = true;
        XCoreApplication_aboutToQuit_signal(app);
    }

    /* Qt 6.8: 设置 quitNow 标志，通知所有事件循环退出 */
    XThreadData* data = XObject_threadData((XObject*)app);
    if (data) {
        data->m_quitNow = true;
        for (size_t i = 0; i < XStack_size_base(&data->m_eventLoops); ++i) {
            XEventLoop** pLoop = (XEventLoop**)XVector_at_base(&data->m_eventLoops, i);
            if (pLoop && *pLoop) {
                XEventLoop_exit(*pLoop, returnCode);
            }
        }
    }
}

/* ==================== quit（对标 Qt 6.8 QCoreApplication::quit） ==================== */

void XCoreApplication_quit()
{
    /* Qt 6.8: quit() 直接调用 exit(0) */
    XCoreApplication_exit(0);
}

/* ==================== sendEvent（对标 Qt 6.8 QCoreApplication::sendEvent） ==================== */

bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return false;
    /* Qt 6.8: sendEvent 设置 spontaneous = false，然后调用 notifyInternal2 */
    event->spontaneous = 0;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== sendSpontaneousEvent（对标 Qt 6.8 QCoreApplication::sendSpontaneousEvent） ==================== */

bool XCoreApplication_sendSpontaneousEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return false;
    event->spontaneous = 1;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== forwardEvent（对标 Qt 6.8 QCoreApplication::forwardEvent） ==================== */

bool XCoreApplication_forwardEvent(XObject* receiver, XEvent* event, XEvent* originatingEvent)
{
    if (!receiver || !event) return false;
    /* Qt 6.8: forwardEvent 复制源事件的 spontaneous 状态 */
    if (originatingEvent)
        event->spontaneous = originatingEvent->spontaneous;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== notifyInternal2（对标 Qt 6.8 QCoreApplication::notifyInternal2） ==================== */

bool XCoreApplication_notifyInternal2(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return false;

    XThreadData* threadData = XObject_threadData(receiver);
    if (threadData)
        ++threadData->m_scopeLevel;

    /* Qt 6.8: notifyInternal2 直接调用 notify，不经过应用级事件过滤器 */
    bool result = XCoreApplication_notify_base(receiver, event);

    if (threadData)
        --threadData->m_scopeLevel;
    return result;
}

/* ==================== postEvent（对标 Qt 6.8 QCoreApplication::postEvent） ==================== */

void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority)
{
    if (!receiver || !event) return;

    XThreadData* td = XThreadData_lockPostEventList(receiver);
    if (!td) {
        XEvent_delete_base(event);
        return;
    }

    /* Qt 6.8: 如果接收者已有已投递事件，尝试压缩 */
    if (XAtomic_load_int32(&receiver->m_posted_events, XAtomic_MemoryOrder_Acquire) > 0) {
        if (XCoreApplication_compressEvent(event, receiver, &td->m_postEventList)) {
            XMutex_unlock(td->m_mutex);
            return;
        }
    }

    event->posted = true;
    XAtomic_fetch_add_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);

    XPostEvent pe = { receiver, event, priority };
    if (!XVector_push_back_1_base(&td->m_postEventList, &pe)) {
        XAtomic_fetch_sub_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);
        event->posted = false;
        XMutex_unlock(td->m_mutex);
        XEvent_delete_base(event);
        return;
    }

    XAtomic_store_bool(&td->m_canWait, false, XAtomic_MemoryOrder_Release);
    XMutex_unlock(td->m_mutex);

    if (td->m_eventDispatcher)
        XAbstractEventDispatcher_wakeUp_base(td->m_eventDispatcher);
}

bool XCoreApplication_tryPostEvent(XObject* receiver, XEvent* event, int priority)
{
    return XThreadData_tryPostEvent(receiver, event, priority);
}

/* ==================== removePostedEvents（对标 Qt 6.8 QCoreApplication::removePostedEvents） ==================== */

void XCoreApplication_removePostedEvents(XObject* receiver, XEventType eventType)
{
    XThreadData_discardActivePostedEvents(receiver, eventType);
    XVector* events = XThreadData_takePostedEvents();
    if (!events) return;
    for_each_iterator(events, XVector, it) {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (!ePost || !ePost->event) continue;
        if (receiver && ePost->receiver != receiver) continue;
        if (eventType && eventType != ePost->event->type) continue;
        XThreadData_discardPostedEvent(ePost);
    }
    XThreadData_push_front_list(events);
    XVector_delete_base(events);
}

/* ==================== sendPostedEvents（对标 Qt 6.8 QCoreApplication::sendPostedEvents） ==================== */

void XCoreApplication_sendPostedEvents(XObject* receiver, XEventType eventType)
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

        /* Qt 6.8: DeferredDelete 事件处理 — 不能发送时重新投递到线程队列 */
        if (ePost->event->type == XEVENT_TYPE_DEFERRED_DELETE) {
            if (!XDeferredDeleteEvent_shouldDeliver(
                    (XDeferredDeleteEvent*)ePost->event,
                    data,
                    eventType == XEVENT_TYPE_DEFERRED_DELETE)) {
                /* Qt 6.8: 复制事件并重新投递到线程的 postEventList（原始队列）
                 * 将原条目 event 置空，复制的事件重新添加到队列尾部 */
                XPostEvent pe_copy = *ePost;
                ePost->event = NULL;  // 原条目置空，避免后续处理

                /* 重新添加到线程数据的 postEventList */
                XMutex_lock(data->m_mutex);
                XVector_push_back_1_base(&data->m_postEventList, &pe_copy);
                XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
                XMutex_unlock(data->m_mutex);
                continue;
            }
        }

        XThreadData_deliverPostedEvent(ePost);
    }

    XThreadData_popActivePostedEvents(events);
    XThreadData_push_front_list(events);
    XVector_delete_base(events);
}

/* ==================== eventDispatcher ==================== */

XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void)
{
    /* Qt 6.8: 返回主线程的事件分发器 */
    XThreadData* td = XObject_threadData((XObject*)g_app);
    return td ? td->m_eventDispatcher : NULL;
}

void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher)
{
    /* Qt 6.8: 只能在没有事件分发器时设置（即 QCoreApplication 实例化之前） */
    XThreadData* data = XObject_threadData((XObject*)g_app);
    if (!data) return;

    if (data->m_eventDispatcher) {
        fprintf(stderr, "XCoreApplication::setEventDispatcher: 事件分发器已存在，无法替换\n");
        return;
    }

    data->m_eventDispatcher = dispatcher;
}

/* ==================== 库路径管理 ==================== */

void XCoreApplication_setLibraryPaths(const XStringList* paths)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    if (!app->m_paths) app->m_paths = XStringList_create();
    XString_copy_base(app->m_paths, paths);
}

const XStringList* XCoreApplication_libraryPaths(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_paths : NULL;
}

void XCoreApplication_addLibraryPath(const XString* path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !path) return;
    if (!app->m_paths) app->m_paths = XStringList_create();
    XStringList_push_back_base(app->m_paths, path);
}

void XCoreApplication_removeLibraryPath(const XString* path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_paths || !path) return;
    XStringList_remove_base(app->m_paths, XStringList_indexOf(app->m_paths, path, 0, XChar_CaseSensitive), 1);
}

/* ==================== 信号 ==================== */

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

/* ==================== 应用状态 ==================== */

bool XCoreApplication_startingUp(void)
{
    return !is_app_running;
}

bool XCoreApplication_closingDown(void)
{
    return is_app_closing;
}

/* ==================== setuid 安全 ==================== */

void XCoreApplication_setSetuidAllowed(bool allow)
{
    setuidAllowed = allow;
}

bool XCoreApplication_isSetuidAllowed(void)
{
    return setuidAllowed;
}

/* ==================== quitLock 管理 ==================== */

/* Qt 6.8: quitLockRef 初始为 1，表示应用程序自身持有锁 */
static int quitLockRef = 1;
static bool quitLockEnabled = true;

bool XCoreApplication_isQuitLockEnabled(void)
{
    return quitLockEnabled;
}

void XCoreApplication_setQuitLockEnabled(bool enabled)
{
    quitLockEnabled = enabled;
    if (!enabled && quitLockRef <= 0) {
        XCoreApplication_quit();
    }
}

/* ==================== VXCoreApplication_event（对标 Qt 6.8 QCoreApplication::event） ==================== */

bool VXCoreApplication_event(XObject* self, XEvent* e)
{
    if (e->type == XEVENT_TYPE_QUIT) {
        XCoreApplication_exit(0);
        return true;
    }
    return XClass_Parent(XObject, EXObject_Event, bool(*)(XObject*, XEvent*))(self, e);
}

/* ==================== VXCoreApplication_notify（对标 Qt 6.8 QCoreApplication::notify） ==================== */

bool VXCoreApplication_notify(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return true;

    if (is_app_closing)
        return true;

    XThreadData* currentData = XThreadData_current();
    XThreadData* receiverData = XObject_threadData(receiver);
    if (receiverData && receiverData != currentData)
        return false;
    if (receiverData)
        ++receiverData->m_scopeLevel;

    /* Qt 6.8: 应用级事件过滤器 */
    if (XThread_currentThread() == g_mainThread && g_app) {
        XVector* appFilters = ((XObject*)g_app)->m_filters;
        if (appFilters) {
            for_each_iterator(appFilters, XVector, it) {
                XObject* filter = *((XObject**)XVector_iterator_data(&it));
                if (!filter) continue;
                if (XObject_eventFilter_base(filter, receiver, event))
                    return true;
            }
        }
    }

    bool handled = false;

    XVector* filters = receiver->m_filters;
    if (filters) {
        for_each_iterator(filters, XVector, it) {
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
    if (!handled && XObject_isWidgetType(receiver)) {
        XObject* parent = XObject_parent(receiver);
        if (parent)
            handled = XCoreApplication_notify_base(parent, event);
    }

done:
    if (receiverData)
        --receiverData->m_scopeLevel;
    return handled;
}

/* ==================== VXCoreApplication_deinit ==================== */

void VXCoreApplication_deinit(XCoreApplication* app)
{
    if (!app) return;

    is_app_closing = true;
    is_app_running = false;

    /* 释放应用程序元信息字符串 */
    if (app->m_applicationName) {
        XString_delete_base(app->m_applicationName);
        app->m_applicationName = NULL;
    }
    if (app->m_version) {
        XString_delete_base(app->m_version);
        app->m_version = NULL;
    }
    if (app->m_orgName) {
        XString_delete_base(app->m_orgName);
        app->m_orgName = NULL;
    }
    if (app->m_orgDomain) {
        XString_delete_base(app->m_orgDomain);
        app->m_orgDomain = NULL;
    }
    if (app->m_paths) {
        XStringList_delete_base(app->m_paths);
        app->m_paths = NULL;
    }
    XBitArray_deinit_base(&app->m_attribute);

    /* 释放事件分发器 */
    XThreadData* td = XObject_threadData((XObject*)app);
    if (td && td->m_eventDispatcher) {
        XClass_delete_base(td->m_eventDispatcher);
        td->m_eventDispatcher = NULL;
    }

    XClass_Deinit_Parent(XObject, app);

    if (g_app == app) {
        g_app = NULL;
    }
}
