#include "XThread.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XEventLoop.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include "XThreadData.h"
#include <string.h>
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist)
{
    XThread* thread = (XThread*)XMalloc_System(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    XThread_init(thread);
    thread->m_start_routine = start_routine;
    thread->m_varList = varlist;
    Set_Class_MemoryFree(thread, XFree_System);
    return thread;
}
XThread* XThread_create(XObject* parent)
{
    XThread* thread = (XThread*)XMalloc_System(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    XThread_init(thread);
    thread->m_start_routine = NULL;
    thread->m_varList = NULL;

    Set_Class_MemoryFree(thread, XFree_System);
    if (parent)
        XObject_setParent((XObject*)thread, parent);
    return thread;
}
XThread* XThread_createMainThread(XObject* parent)
{
    XThread* thread = (XThread*)XMalloc_System(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    memset(((XObject*)thread) + 1, 0, sizeof(XThread) - sizeof(XObject));
    XObject_init((XObject*)thread);
    XClassGetVtable(thread) = XThread_class_init();

    thread->m_handle = 0;
    thread->m_interruptionRequested = false;
    thread->m_isMainThread = true;
    thread->m_running = true;
    thread->m_finished = false;
    thread->m_start_routine = NULL;
    thread->m_varList = NULL;
    thread->m_priority = XThread_NormalPriority;
    thread->m_stackSize = 0;
    Set_Class_MemoryFree(thread, XFree_System);
    thread->m_data = XThreadData_initMainThread(thread);
    return thread;
}
void XThread_init(XThread* thread)
{
    if (!thread)return;
    memset(((XObject*)thread) + 1, 0, sizeof(XThread) - sizeof(XObject));
    XObject_init((XObject*)thread);
    XClassGetVtable(thread) = XThread_class_init();
    thread->m_handle = 0;
    thread->m_interruptionRequested = false;
    thread->m_isMainThread = false;
    thread->m_running = false;
    thread->m_finished = false;
    thread->m_start_routine = NULL;
    thread->m_varList = NULL;
    thread->m_loop = NULL;
    thread->m_priority = XThread_NormalPriority;
    thread->m_stackSize = 512;
    thread->m_data = XThreadData_create(thread);  /* Qt: QThreadPrivate creates QThreadData in constructor */
}
void* XThread_finished_signal(XThread* thread)
{
    if (thread)
        XObject_emitSignal((XObject*)thread, (size_t)XThread_finished_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XThread_finished_signal;
}
void* XThread_started_signal(XThread* thread)
{
    if (thread)
        XObject_emitSignal((XObject*)thread, (size_t)XThread_started_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XThread_started_signal;
}
XThread* XThread_currentThread()
{
    XThreadData* data = XThreadData_current();
    return data ? data->m_thread : NULL;
}
XEventDispatcher* XThread_currentEventDispatcher(void)
{
    XThread* th = XThread_currentThread();
    return th ? XThread_dispatcher(th) : NULL;
}
bool XThread_isMainThread()
{
    XThread* th=XThread_currentThread();
    return th? th->m_isMainThread:false;
}
XHandle XThread_getHandle(XThread* thread)
{
    return thread ? thread->m_handle : 0;
}
XEventDispatcher* XThread_dispatcher(const XThread* thread)
{
    if (thread)
        return thread->m_data? thread->m_data->m_eventDispatcher:NULL;
    return XCoreApplication_eventDispatcher();
}
bool XThread_isCurrentThread(const XThread* thread)
{
    return XThread_currentThread()==thread;
}
bool XThread_isInterruptionRequested(const XThread* thread)
{
    return thread && thread->m_interruptionRequested;
}
int XThread_loopLevel(const XThread* thread)
{
    if (!thread || !thread->m_data)
        return 0;
    return XAtomic_load_size_t(&thread->m_data->m_loopLevel, XAtomic_MemoryOrder_Relaxed);
}
void XThread_requestInterruption(XThread* thread)
{
    if (thread)
        thread->m_interruptionRequested = true;
}
void XThread_setEventDispatcher(XThread* thread, XEventDispatcher* eventDispatcher)
{
    if (!thread || !eventDispatcher) {
        return;
    }
    if (!thread->m_data) {
        return;
    }
    if (thread->m_data->m_eventDispatcher) {
        XClass_delete_base(thread->m_data->m_eventDispatcher);
    }
    thread->m_data->m_eventDispatcher = eventDispatcher;
     XObject_setParent((XObject*)eventDispatcher, (XObject*)thread);
}

void XThread_exit(XThread* thread, int returnCode)
{
    if (!thread)
        return;

    XEventLoop* loop = thread->m_loop;
    XThreadData* data = thread->m_data;
    if (!loop)
        return;

    /* Wake first.  Once the loop state changes, the worker may immediately
     * destroy its dispatcher and thread data, so neither may be touched after
     * XEventLoop_exit(). */
    if (data && data->m_eventDispatcher)
        XAbstractEventDispatcher_wakeUp_base(data->m_eventDispatcher);
    XEventLoop_exit(loop, returnCode);
}
void XThread_quit(XThread * thread)
{
    XThread_exit(thread, 0);
}
int XThread_exec(XThread* thread)
{
    if (!thread) return -1;
    if(!thread->m_loop)
    {
        thread->m_loop = XEventLoop_create();
    }
    int result = XEventLoop_exec(thread->m_loop, XEventLoop_AllEvents);
    return result;
}

void XThread_run_base(XThread* thread)
{
    XClassGetVirtualFunc(thread, EXThread_Run, void(*)(XThread*))(thread);
}

/* The XThread object keeps the affinity of its creator.  Only this function
 * executes in the worker, matching QThread's object/worker split. */
void VXThread_run(XThread* thread)
{
    if (!thread) return;

    /* 保存标志到局部变量: sendPostedEvents 可能通过 deleteLater 释放 thread 对象,
     * 之后不能再访问 thread-> 成员 */
    bool isMainThread = thread->m_isMainThread;
    XThreadData* data = thread->m_data;

    if (!isMainThread)
    {
        /* Qt: XThreadData already created in XThread_init constructor.
         * Update threadId and create dispatcher in the worker thread. */
        if (!data)
        {
            thread->m_running = false;
            thread->m_finished = true;
            return;
        }
        data->m_threadId = XThread_currentThreadId();
        /* Qt 6.8: set_thread_data(data) - 注册到 TLS (对标 QThreadPrivate::start) */
        XThreadStorage_set(data);
        /* The dispatcher belongs to XThreadData, not to the XThread QObject. */
        data->m_eventDispatcher = XEventDispatcher_create(NULL);
        if (!data->m_eventDispatcher)
        {
            /* Qt 6.8: clearCurrentThreadData() - 清除 TLS 后退出 */
            XThreadData_clearCurrentThreadData();
            thread->m_running = false;
            thread->m_finished = true;
            return;
        }
    }

    thread->m_finished = false;
    thread->m_running = true;
    XThread_started_signal(thread);

    if (thread->m_start_routine)
        thread->m_start_routine(thread, thread->m_varList);

    XThread_finished_signal(thread);

    /* The event loop and deferred worker objects are destroyed in the worker. */
    if (thread->m_loop)
    {
        XClass_delete_base((XClass*)thread->m_loop);
        thread->m_loop = NULL;
    }

    /* 在 sendPostedEvents 之前设置线程状态和清空 m_data:
     * sendPostedEvents 可能通过 deleteLater 释放 thread 对象本身,
     * 之后只能使用局部变量 isMainThread/data (对标 QThreadPrivate::finish) */
    thread->m_data = NULL;  /* 防止 XThread 析构时 double-deref */
    thread->m_running = false;
    thread->m_finished = true;

    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    /* Qt 6.8: QThreadPrivate::cleanup() - 删除事件分发器 (对标 delete eventDispatcher)
     * 必须在 clearCurrentThreadData() 之前: dispatcher 析构 (VXObject_deinit) 需通过 TLS
     * 访问当前线程数据,若 TLS 已清空会误创建 adopted 线程数据。删除后置空,
     * 使后续 XThreadData_deref 触发的 XThreadData_delete 不再重复删除分发器。 */
    if (!isMainThread && data && data->m_eventDispatcher)
    {
        XAbstractEventDispatcher* ed = data->m_eventDispatcher;
        data->m_eventDispatcher = NULL;
        XClass_delete_base((XClass*)ed);
    }

    if (!isMainThread)
        /* Qt 6.8: clearCurrentThreadData() - 清除 TLS (对标 QThreadData::clearCurrentThreadData) */
        XThreadData_clearCurrentThreadData();

    /* Qt: deref XThreadData held by XThread; moved objects keep their own refs.
     * 在 sendPostedEvents 之后 deref: 确保 deferred delete 处理时 data 仍有效 */
    if (data)
        XThreadData_deref(data);
}
