#include "XThread.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XHashMap.h"
#include "XHashSet.h"
#include "XHashFunc.h"
#include "XMutex.h"
#include "XEventLoop.h"
//#include "XEventDispatcher.h"
#include "XCoreApplication.h"
#include "XThreadData.h"
#include <string.h>
// 创建 XThread 对象
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist)
{
    XThread* thread = (XThread*)XMemory_malloc(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    XThread_init(thread);
    thread->m_start_routine = start_routine;
    thread->m_varList = varlist;
    Set_Class_MemoryFree(thread, XFree);
    return thread;
}
XThread* XThread_create(XObject* parent)
{
    XThread* thread = (XThread*)XMemory_malloc(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    XThread_init(thread);
    thread->m_start_routine = NULL;
    thread->m_varList = NULL;

    Set_Class_MemoryFree(thread, XFree);
    return thread;
}
XThread* XThread_createMainThread(XObject* parent)
{
    XThread* thread = (XThread*)XMemory_malloc(sizeof(XThread));
    if (thread == NULL) {
        return NULL;
    }
    memset(((XObject*)thread) + 1, 0, sizeof(XThread) - sizeof(XObject));
    XObject_init(thread);
    XClassGetVtable(thread) = XThread_class_init();
    thread->m_handle = 0;
    thread->m_finished = false;
    thread->m_interruptionRequested = false;
    thread->m_running = true;
    thread->m_loop = NULL;
    thread->m_priority = XThread_err;
    thread->m_stackSize = 0;
    Set_Class_MemoryFree(thread, XFree);
    thread->m_start_routine = NULL;
    thread->m_varList = NULL;
    thread->m_isMainThread = true;
    // 初始化优先级和栈大小为真实值
    thread->m_priority = XThread_priority(thread); // 这会触发上面的新逻辑
    thread->m_stackSize = XThread_stackSize(thread); // 这会触发上面的新逻辑
    thread->m_data = XThreadData_initMainThread(thread);
    //thread->m_data = NULL;
    return thread;
}
// 初始化 XThread 对象
void XThread_init(XThread* thread)
{
    if (!thread)return;
    memset(((XObject*)thread) + 1, 0, sizeof(XThread) - sizeof(XObject));
    XObject_init(thread);
    XClassGetVtable(thread) = XThread_class_init();
    thread->m_handle = 0;
    thread->m_finished = false;
    thread->m_interruptionRequested = false;
    thread->m_running = false;
    thread->m_loop = NULL;
    thread->m_priority = XThread_NormalPriority;
    thread->m_stackSize = 512;
    //thread->m_data = XThreadData_create(thread);
    thread->m_data = NULL;
    //((Xthread*)thread)->m_thread = thread;//将线程指针设为自己，才可以使用事件
}
void* XThread_finished_signal(XThread* thread)
{
    XEmitSignal(thread, XThread_finished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
void* XThread_started_signal(XThread* thread)
{
    XEmitSignal(thread, XThread_started_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
XThread* XThread_currentThread()
{
    XThreadData* data = XThreadData_current();
    return data ? data->m_thread:NULL;
}
XEventDispatcher* XThread_currentDispatcher()
{
    XThread* th = XThread_currentThread();
    return th ? XThread_dispatcher(th) : NULL;
}

bool XThread_isMainThread()
{
    XThread* th=XThread_currentThread();
    return th? th->m_isMainThread:false;
}

// 获取 XThread 句柄
XHandle XThread_getHandle(XThread* thread)
{
    return thread->m_handle;
}

// 获取事件调度器
XEventDispatcher* XThread_dispatcher(const XThread* thread)
{
    if (thread)
        return thread->m_data? thread->m_data->m_dispatcher:NULL;
    return XCoreApplication_eventDispatcher();
}

bool XThread_isCurrentThread(const XThread* thread)
{
    return XThread_currentThread()==thread;
}

// 判断线程是否被请求中断
bool XThread_isInterruptionRequested(const XThread* thread)
{
    return thread->m_interruptionRequested;
}
// 获取线程循环级别
int XThread_loopLevel(const XThread* thread)
{
    return XAtomic_load_size_t(&thread->m_data->m_loopLevel, XAtomic_MemoryOrder_Relaxed);
}

// 请求中断线程
void XThread_requestInterruption(XThread* thread)
{
    thread->m_interruptionRequested = true;
}

// 设置事件调度器
void XThread_setEventDispatcher(XThread* thread, XEventDispatcher* eventDispatcher)
{
    // 参数校验
    if (!thread || !eventDispatcher) {
        return;
    }

    // 确保线程的私有数据已经初始化
    if (!thread->m_data) {
        // 如果 m_data 不存在，说明线程对象可能尚未正确初始化。
        // 根据您的框架设计，这里可以选择初始化它或者直接返回错误。
        // 考虑到 XThread_init 和 XThread_createMainThread 都会初始化 m_data，
        // 这种情况通常不应该发生。
        return;
    }

    // --- 关键步骤：替换分发器 ---
    // 1. 先删除旧的分发器（如果存在）
    if (thread->m_data->m_dispatcher) {
        XClass_delete_base(thread->m_data->m_dispatcher);
    }

    // 2. 设置新的分发器
    thread->m_data->m_dispatcher = eventDispatcher;

    // --- 可选：设置新分发器的父对象 ---
    // 为了让新分发器能随着线程对象的销毁而自动清理，可以将其父对象设为线程。
    // 这需要您的对象模型支持父子关系管理（如 Qt 的 Qthread）。
    // 如果您的 Xthread 支持 setParent，可以加上下面这行：
     XObject_setParent((XObject*)eventDispatcher, (XObject*)thread);
}

void XThread_exit(XThread* thread, int returnCode)
{
    if (thread && thread->m_loop)
        XEventLoop_exit(thread->m_loop, returnCode);
}
void XThread_quit(XThread * thread)
{
    if (thread && thread->m_loop)
    {
        XEventLoop_quit(thread->m_loop);
        XAbstractEventDispatcher_wakeUp_base(thread->m_data->m_dispatcher);
    }
}
int XThread_exec(XThread* thread)
{
    if (!thread) return;
    if(!thread->m_loop)
    {
        thread->m_loop = XEventLoop_create();
    }
    int result = XEventLoop_exec(thread->m_loop);
    //Xthread_connect(thread,XSignal(Xthread_destroyed_signal), thread->m_loop,XClass_delete_base,XConnectionType_Direct);
    //XEventLoop_deleteLater(thread->m_loop);
    //XCoreApplication_processEvents(0);
    //thread->m_loop = NULL;
    return result;
}

void XThread_run_base(XThread* thread)
{
    return XClassGetVirtualFunc(thread, EXThread_Run, bool(*)(XThread*))(thread);
}
//虚函数默认实现,供平台实现文件调用
void VXThread_run(XThread* thread)
{
    if (!thread)return;
    XHandle id = 0;
    if (!thread->m_isMainThread)
    {
        thread->m_data = XThreadData_create(thread);
        id = XThreadData_mapInsert(thread->m_data);
        thread->m_data->m_dispatcher= XEventDispatcher_create(thread);
    }
   
    thread->m_finished = false;
    thread->m_running = true;
    XThread_started_signal(thread);

    if (thread && thread->m_start_routine)
        thread->m_start_routine(thread,thread->m_varList);
    XThread_finished_signal(thread);
    thread->m_finished = true;
    thread->m_running = false;
    if (thread->m_loop&& XObject_thread(thread->m_loop)==XThread_currentThread())
    {//当前线程创建的在当前线程结束前释放
        XObject_deleteLater(thread->m_loop);
        thread->m_loop = NULL;
    }
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    if(id)
        XThreadData_mapRemove(id);
}