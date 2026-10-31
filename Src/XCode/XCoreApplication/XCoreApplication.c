#include "XPrintf.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XCryptographic.h"
#include "XHashSet.h"
#include "XMutex.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XString.h"
#include "XTimeWheelGroup.h"
#include "XDateTime.h" /* execBounded 单调时钟（XDateTime_currentMSecsSinceEpoch）。 */
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XTimer.h"
#include "XMultiPool.h"
#include "XAbstractNativeEventFilter.h"
#include "XSystem.h"
#include "XStringUtils.h"
#include <string.h>
#include <stdio.h>

static XCoreApplication* g_app = NULL;
/* argv[0] 进程级留存（create 时复制，delete 不清）：Qt 语义下单例随 main
   全程存活，但本框架允许测试销毁重建实例（delete 后 instance()==NULL），
   applicationDirPath 需跨实例窗口期解析程序目录。调用方 argv 可能是栈上
   桩（回归测试如此），故复制而非存指针；替换旧值，进程退出由 OS 回收。 */
static char* g_argv0Copy = NULL;
static XThread* g_mainThread = NULL;
static bool is_app_running = false;
static bool is_app_closing = false;
static bool setuidAllowed = false;
/* 当前 QCoreApplication::exec 对应的栈事件循环；用于安全退出当前循环。 */
static XEventLoop* g_execLoop = NULL;

static bool VXCoreApplication_notify(XObject* receiver, XEvent* e);
static bool VXCoreApplication_event(XObject* self, XEvent* e);

/* ==================== compressEvent 实现（对标 Qt 6.8 QCoreApplication::compressEvent） ==================== */
/* Qt 6.8: compressEvent 是非虚函数 */

bool XCoreApplication_compressEvent(XEvent* event, XObject* receiver, void* postedEvents)
{
    if (!event || !receiver || !postedEvents)
        return false;

    XVector* postList = (XVector*)postedEvents;

    if (event->m_type == XEVENT_TYPE_TIMER) {
        int timerId = ((XTimerEvent*)event)->timerId;
        for (size_t i = 0; i < XVector_size_base(postList); ++i) {
            XPostEvent* pe = (XPostEvent*)XVector_at_base(postList, i);
            if (pe && pe->m_event && pe->m_event->m_type == XEVENT_TYPE_TIMER && pe->m_receiver == receiver) {
                if (((XTimerEvent*)pe->m_event)->timerId == timerId) {
                    XClassDelete(event);
                    return true;
                }
            }
        }
        return false;
    }

    if (event->m_type == XEVENT_TYPE_QUIT) {
        for (size_t i = 0; i < XVector_size_base(postList); ++i) {
            XPostEvent* pe = (XPostEvent*)XVector_at_base(postList, i);
            if (pe && pe->m_event && pe->m_event->m_type == XEVENT_TYPE_QUIT && pe->m_receiver == receiver) {
                XClassDelete(event);
                return true;
            }
        }
        return false;
    }

    /* 对标 Qt 6.8 QApplication::compressEvent（qapplication.cpp:792-827）：
       LayoutRequest 与同接收者队列中已有 LayoutRequest 合并（纯通知事件
       无负载，后者覆盖前者即可）——子控件批量显隐/布局级联失效在单拍内
       多次 post 时只派发一次，activate 幂等早退兜底剩余重复。 */
    if (event->m_type == XEVENT_TYPE_LAYOUT_REQUEST) {
        for (size_t i = 0; i < XVector_size_base(postList); ++i) {
            XPostEvent* pe = (XPostEvent*)XVector_at_base(postList, i);
            if (pe && pe->m_event && pe->m_event->m_type == XEVENT_TYPE_LAYOUT_REQUEST
                && pe->m_receiver == receiver) {
                XClassDelete(event);
                return true;
            }
        }
        return false;
    }

    return false;
}

static void VXCoreApplication_deinit(XCoreApplication* app);

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

XCoreApplication* XCoreApplication_create_ex(XMemoryType memory, int argc, char** argv) {
    if (g_app != NULL)
        return g_app;

    XCoreApplication* app = XMemory_malloc(sizeof(XCoreApplication), memory);
    if (!app) return NULL;

    XCoreApplication_init(app, argc, argv);
    Set_Class_Memory(app, memory); Set_Class_IsHeap(app, true);
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
    if (argc > 0 && argv && argv[0] && argv[0][0]) {
        size_t n = XStrlen(argv[0]);
        char* keep = (char*)XMalloc_System(n + 1);
        if (keep) {
            memcpy(keep, argv[0], n + 1);
            if (g_argv0Copy) XFree_System(g_argv0Copy);
            g_argv0Copy = keep;
        }
    }
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
    if (app->m_applicationName) XClassDelete(app->m_applicationName);
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
    if (app->m_version) XClassDelete(app->m_version);
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
    if (app->m_orgName) XClassDelete(app->m_orgName);
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
    if (app->m_orgDomain) XClassDelete(app->m_orgDomain);
    app->m_orgDomain = orgDomain ? XString_create_copy(orgDomain) : NULL;
    XCoreApplication_organizationDomainChanged_signal(app);
}

const XString* XCoreApplication_organizationDomain(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgDomain : NULL;
}

/* ==================== 应用程序属性 ==================== */

/** @brief 属性设置完成回调钩子；GUI 层（XGuiApplication）注册，全进程单钩子。 */
static XCoreApplicationAttributeHook g_attributeHook = NULL;

void XCoreApplication_setAttributeHook(XCoreApplicationAttributeHook hook)
{
    g_attributeHook = hook;
}

void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) return;
    XBitArray_setBit(&app->m_attribute, attribute, on);
    /* 属性设置完成钩子：把 GUI 语义属性（如属性 12 的触摸→鼠标仿真
     * 开关）的转发收敛到基类统一出口——直调基类与经 XGuiApplication
     * 包装层调用行为一致（批次十七报备项收敛）。 */
    if (g_attributeHook)
        g_attributeHook(attribute, on);
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
                XClassDelete(s);
            }
        }
    }
    return args;
}

/* ==================== 应用程序路径 ==================== */

const XString* XCoreApplication_applicationDirPath(void)
{
    const char* path = g_argv0Copy; /* 进程级留存，不依赖实例存活 */
    const char* lastSlash;
    const char* lastBack;

    if (path) {
        lastSlash = XStrrchr(path, '/');
        lastBack = XStrrchr(path, '\\');
        if (lastBack && (!lastSlash || lastBack > lastSlash)) {
            lastSlash = lastBack;
        }
        if (lastSlash && lastSlash != path) {
            size_t len = (size_t)(lastSlash - path);
            char* dir = (char*)XMalloc_System(len + 1);
            if (!dir) return NULL;
            memcpy(dir, path, len);
            dir[len] = '\0';
            XString* result = XString_create_utf8(dir);
            XFree_System(dir);
            return result;
        }
        /* argv[0] 无目录部分（如 PATH 搜索裸名）：落到平台查询。 */
    }

    /* argv[0] 不可用或无目录时经 XSystem 平台抽象查询（本文件不触平台 API）。 */
    {
        char exePath[XSYSTEM_EXECUTABLE_PATH_MAX];
        if (XSystem_executableFilePath(exePath, sizeof(exePath))) {
            const char* slash = XStrrchr(exePath, '/');
            const char* back = XStrrchr(exePath, '\\');
            const char* last = (back && (!slash || back > slash)) ? back : slash;
            if (last && last != exePath) {
                exePath[last - exePath] = '\0';
                return XString_create_utf8(exePath);
            }
        }
    }
    if (!path) return NULL; /* 从未创建过应用且平台查询失败：NULL 语义。 */
    return XString_create_utf8(".");
}

const XString* XCoreApplication_applicationFilePath(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_argv || !app->m_argv[0]) return NULL;
    return XString_create_utf8(app->m_argv[0]);
}

int64_t XCoreApplication_applicationPid(void)
{
    return XSystem_pid();
}

/* ==================== 事件循环控制 ==================== */

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags)
{
    XThreadData* data = XThreadData_current();
#if XPRINTF_OUTPUT_REDIRECT_ON
    /* [2026-10-31] 抽水线程收集的标准 C 输出在主线程排空到当前回调
     * （GUI 安全：抽水线程不得直调非线程安全的捕获回调）。 */
    XPrintf_drainRedirectedOutput();
#endif
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
        XERROR_PRINTF( "XCoreApplication::exec: 必须在主线程中调用\n");
        return -1;
    }

    XThreadData* data = XObject_threadData((XObject*)app);
    if (data && XStack_size_base(&data->m_eventLoops) > 0) {
        XERROR_PRINTF( "XCoreApplication::exec: 事件循环已在运行\n");
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

/* ==================== execBounded（限时事件循环；测试域专用） ==================== */

int XCoreApplication_execBounded(int timeoutMs)
{
    XCoreApplication* app = XCoreApplication_instance();
    XThread* currentThread;
    XThreadData* data;
    XEventLoop eventLoop;
    int64_t deadlineMs;
    int result = 1; /* 缺省=超时强制返回。 */
    bool pushed = false;

    if (!app || timeoutMs < 0)
        return -1;
    currentThread = XThread_currentThread();
    if (currentThread != g_mainThread) {
        XERROR_PRINTF(
            "XCoreApplication::execBounded: 必须在主线程中调用\n");
        return -1;
    }
    data = XObject_threadData((XObject*)app);

    if (data)
        data->m_quitNow = false;
    app->m_in_exec = true;
    app->m_aboutToQuitEmitted = false;

    /* 嵌套限时循环（对标 Qt QEventLoop::exec 的嵌套语义，区别于
       QCoreApplication::exec 的不可重入）：GUI 主循环在场时动作内
       依旧可以跑一层局部循环等线程收尾——UI 事件照常流转（嵌套层
       processEvents 消费），线程的收尾打印仍落在重定向窗口内。
       [g_execLoop 最内层接管 2026-10-31] 测试动作里的
       XCoreApplication_quit()（线程收尾信号，如 XStackTest 大量数据项）
       走 XCoreApplication_exit 的 g_execLoop 分支——**只退出本限时层**
       ：外层主循环（GUI demo）与全局 quitNow/aboutToQuit 一概不动。
       此前 quit() 落到全局分支：GUI demo 直接退出主循环（用户实测
       「执行后程序意外关闭」），扫雷探针里 aboutToQuit 全局收尾破坏
       后续动作的信号设施（AV on freed queue node）。保存/恢复实现
       LIFO 嵌套，与 Qt QEventLoop::quit 只退本层的语义一致。 */
    XEventLoop* prevExecLoop = g_execLoop;
    XEventLoop_init(&eventLoop);
    g_execLoop = &eventLoop;
    if (data) {
        XThreadData_pushEventloop(data, &eventLoop);
        pushed = true;
    }
    deadlineMs = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    while (!XAtomic_load_bool(&eventLoop.m_exit, XAtomic_MemoryOrder_Acquire)) {
        int64_t remain = deadlineMs - XDateTime_currentMSecsSinceEpoch();
        if (remain <= 0) {
            /* 超时强制收口：测试线程未 quit（收尾调用被注释）不再拖死
               宿主；告警走 XError 通道（stderr/日志），不污染重定向。 */
            XERROR_PRINTF(
                "XCoreApplication::execBounded: timeout %dms, forcing exit\n",
                timeoutMs);
            result = 1;
            break;
        }
        XEventLoop_processEvents(
            &eventLoop,
            (XEventLoopProcessEventsFlags)(
                XEventLoop_WaitForMoreEvents | XEventLoop_EventLoopExec));
    }
    if (XAtomic_load_bool(&eventLoop.m_exit, XAtomic_MemoryOrder_Acquire))
        result = 0; /* 正常退出优先于超时判定。 */
    g_execLoop = prevExecLoop; /* LIFO 恢复外层 exec 循环指针。 */
    if (pushed)
        XThreadData_popEventloop(data, &eventLoop);
    eventLoop.inExec = false;
    eventLoop.m_state = XEventLoop_Quit;
    /* 限时层内收到的 quit 只指向本层（g_execLoop 分支），不会置全局
       quitNow——此处防御性复位不变。 */
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
        /* [2026-10-31] 非 exec 路径发完即复位：此分支只在「无活动 exec
         * 循环」时进入（g_execLoop LIFO 分支已提前返回），每次 quit 独立
         * 成对发一次。此前标志持久不复位——测试域先前的动作 quit 过一次
         * 后，后续所有 quit（含 XCoreApplicationTest 的信号顺序用例）
         * 静默不发，断言 g_aboutToQuitCount==1 失败（全量扫雷 188 实锚）。
         * exec 路径的入口复位（exec/execBounded 置 false）不受影响。 */
        app->m_aboutToQuitEmitted = false;
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
    event->m_spontaneous = 0;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== sendSpontaneousEvent（对标 Qt 6.8 QCoreApplication::sendSpontaneousEvent） ==================== */

bool XCoreApplication_sendSpontaneousEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) return false;
    event->m_spontaneous = 1;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== forwardEvent（对标 Qt 6.8 QCoreApplication::forwardEvent） ==================== */

bool XCoreApplication_forwardEvent(XObject* receiver, XEvent* event, XEvent* originatingEvent)
{
    if (!receiver || !event) return false;
    /* Qt 6.8: forwardEvent 复制源事件的 spontaneous 状态 */
    if (originatingEvent)
        event->m_spontaneous = originatingEvent->m_spontaneous;
    return XCoreApplication_notifyInternal2(receiver, event);
}

/* ==================== postInvoke（对标 Qt 6.8 QMetaObject::invokeMethod 队列编组） ==================== */

/** @brief postInvoke 兑现蹦床：XEventFunc（XEVENT_TYPE_FUNC_RUN）在接收者
 *  所属线程由 VXObject_event 派发（XObject.c）时解包「回调+上下文」并兑现。
 *  载荷两参数均为指针宽度值字节复制，兑现后随事件析构释放（XEventFunc_deinit
 *  删 argList），无跨线程共享存活期。 */
static void XCoreApplication_postInvoke_trampoline(XVarList* args)
{
    XCoreApplicationInvokeCallback callback;
    void* userData;
    if (!args) return;
    XVarList_start(args);
    callback = XVarList_arg(args, XCoreApplicationInvokeCallback);
    userData = XVarList_arg(args, void*);
    if (callback) callback(userData);
}

bool XCoreApplication_postInvoke(XObject* receiver,
                                 XCoreApplicationInvokeCallback callback,
                                 void* userData)
{
    XVarList* args;
    XEventFunc* event;
    if (!receiver || !callback) return false;
    args = XVarList_Create(XVar(XCoreApplicationInvokeCallback, callback),
                           XVar(void*, userData));
    if (!args) return false;
    event = XEventFunc_create(XCoreApplication_postInvoke_trampoline, args, NULL);
    if (!event) {
        XVarList_delete(args);
        return false;
    }
    /* 所有权移交事件队列：投递失败由 postEvent 内部删除事件（含 argList）。 */
    XCoreApplication_postEvent(receiver, (XEvent*)event, XEVENT_PRIORITY_NORMAL);
    return true;
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
        XClassDelete(event);
        return;
    }

    /* Qt 6.8: 如果接收者已有已投递事件，尝试压缩 */
    if (XAtomic_load_int32(&receiver->m_posted_events, XAtomic_MemoryOrder_Acquire) > 0) {
        if (XCoreApplication_compressEvent(event, receiver, &td->m_postEventList)) {
            XMutex_unlock(td->m_mutex);
            return;
        }
    }

    event->m_posted = true;
    XAtomic_fetch_add_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);

    XPostEvent pe = { receiver, event, priority };
    if (!XVector_push_back_1_base(&td->m_postEventList, &pe)) {
        XAtomic_fetch_sub_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);
        event->m_posted = false;
        XMutex_unlock(td->m_mutex);
        XClassDelete(event);
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
        if (!ePost || !ePost->m_event) continue;
        if (receiver && ePost->m_receiver != receiver) continue;
        if (eventType && eventType != ePost->m_event->m_type) continue;
        XThreadData_discardPostedEvent(ePost);
    }
    XThreadData_push_front_list(events);
    XClassDelete(events);
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
        XClassDelete(events);
        return;
    }

    size_t insertionOffset = XVector_size_base(events);
    size_t i = 0;
    while (i < XVector_size_base(events)) {
        if (i >= insertionOffset) break;

        XPostEvent* ePost = (XPostEvent*)XVector_at_base(events, i);
        ++i;

        if (!ePost || !ePost->m_event) continue;
        if (receiver && ePost->m_receiver != receiver) {
            XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
            continue;
        }
        if (eventType && eventType != ePost->m_event->m_type) {
            XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
            continue;
        }

        /* Qt 6.8: DeferredDelete 事件处理 — 不能发送时重新投递到线程队列 */
        if (ePost->m_event->m_type == XEVENT_TYPE_DEFERRED_DELETE) {
            if (!XDeferredDeleteEvent_shouldDeliver(
                    (XDeferredDeleteEvent*)ePost->m_event,
                    data,
                    eventType == XEVENT_TYPE_DEFERRED_DELETE)) {
                /* Qt 6.8: 复制事件并重新投递到线程的 postEventList（原始队列）
                 * 将原条目 event 置空，复制的事件重新添加到队列尾部 */
                XPostEvent pe_copy = *ePost;
                ePost->m_event = NULL;  // 原条目置空，避免后续处理

                /* 重新添加到线程数据的 postEventList */
                XMutex_lock(data->m_mutex);
                if (!XVector_push_back_1_base(&data->m_postEventList, &pe_copy)) {
                    /* 入队失败：把事件挂回原条目，末尾 push_front_list
                     * 会统一 discard（释放事件并回退 posted_events），
                     * 否则事件对象与计数双双泄漏。 */
                    ePost->m_event = pe_copy.m_event;
                }
                XAtomic_store_bool(&data->m_canWait, false, XAtomic_MemoryOrder_Release);
                XMutex_unlock(data->m_mutex);
                continue;
            }
        }

        XThreadData_deliverPostedEvent(ePost);
    }

    XThreadData_popActivePostedEvents(events);
    XThreadData_push_front_list(events);
    XClassDelete(events);
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
        XERROR_PRINTF( "XCoreApplication::setEventDispatcher: 事件分发器已存在，无法替换\n");
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
    XClassCopy(app->m_paths, paths);
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

static bool VXCoreApplication_event(XObject* self, XEvent* e)
{
    if (e->m_type == XEVENT_TYPE_QUIT) {
        XCoreApplication_exit(0);
        return true;
    }
    return XClass_Parent(XObject, EXObject_Event, bool(*)(XObject*, XEvent*))(self, e);
}

/* ==================== VXCoreApplication_notify（对标 Qt 6.8 QCoreApplication::notify） ==================== */

static bool VXCoreApplication_notify(XObject* receiver, XEvent* event)
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
                if (XObject_eventFilter_base(filter, receiver, event)) {
                    return true;
                }
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

    /* 对标 Qt QApplication::notify：receiver 在事件处理中可能被销毁
     * （setFloating→setParent→destroyWindow 级联），isWidgetType 与
     * parent 须在分发前捕获——事件处理后 receiver 可能已释放（读
     * 悬空指针导致 access violation，XObject_isWidgetType+0x17 实测
     * 崩溃）。分发后只允许使用预捕获值。 */
    bool recvIsWidget = XObject_isWidgetType(receiver);
    XObject* recvParent = XObject_parent(receiver);
    handled = XObject_event_base(receiver, event);
    if (!handled && recvIsWidget && recvParent)
        handled = XCoreApplication_notify_base(recvParent, event);

done:
    if (receiverData)
        --receiverData->m_scopeLevel;
    return handled;
}

/* ==================== VXCoreApplication_deinit ==================== */

static void VXCoreApplication_deinit(XCoreApplication* app)
{
    if (!app) return;

    is_app_closing = true;
    is_app_running = false;

    /* 释放应用程序元信息字符串 */
    if (app->m_applicationName) {
        XClassDelete(app->m_applicationName);
        app->m_applicationName = NULL;
    }
    if (app->m_version) {
        XClassDelete(app->m_version);
        app->m_version = NULL;
    }
    if (app->m_orgName) {
        XClassDelete(app->m_orgName);
        app->m_orgName = NULL;
    }
    if (app->m_orgDomain) {
        XClassDelete(app->m_orgDomain);
        app->m_orgDomain = NULL;
    }
    if (app->m_paths) {
        XClassDelete(app->m_paths);
        app->m_paths = NULL;
    }
    XClassDeinit(&app->m_attribute);

    /* 释放事件分发器 */
    XThreadData* td = XObject_threadData((XObject*)app);
    if (td && td->m_eventDispatcher) {
        XClassDelete(td->m_eventDispatcher);
        td->m_eventDispatcher = NULL;
    }

#if XTHREAD_ON
    /* 释放主线程对象：g_mainThread 由 init 经 XThread_createMainThread 堆
       分配且无其他释放点，此处不释放则每次销毁重建实例都泄漏一个 XThread，
       旧线程还会永久残留 XTask 线程注册表。XThreadData 归 TLS/
       main_thread_data 所有、跨实例复用（与 init 的无 XTHREAD 分支一致），
       按 VXThread_run 收尾的同一做法先 detach 再删，防止平台层
       VXThread_deinit 将仍在服役的主线程数据一并删除；同时解除
       XThreadData->m_thread 对本线程对象的反向引用，避免悬挂（下次
       XThreadData_initMainThread 检测到 m_thread 为空会重新绑定新实例）。 */
    if (g_mainThread) {
        XThreadData* mainData = g_mainThread->m_data;
        if (mainData && mainData->m_thread == g_mainThread)
            mainData->m_thread = NULL;
        g_mainThread->m_data = NULL;
        XClassDelete((XClass*)g_mainThread);
        g_mainThread = NULL;
    }
#endif

    XClass_Deinit_Parent(XObject, app);

    if (g_app == app) {
        g_app = NULL;
    }
    /* 析构完成即解除事件拦截：is_app_closing 仅保护析构窗口期，
       若保持置位会让后续(无应用实例期间)所有 notify 派发被永久
       吞掉（回归多套件先后建/删应用时暴露）。 */
    is_app_closing = false;
}
