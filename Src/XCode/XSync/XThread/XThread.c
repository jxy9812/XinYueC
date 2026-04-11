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
// 创建 XThread 对象
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist)
{
    XThread* Object = (XThread*)XMemory_malloc(sizeof(XThread));
    if (Object == NULL) {
        return NULL;
    }
    XThread_init(Object);
    SET_CLASS_HEAP(Object);
    Object->m_start_routine = start_routine;
    Object->m_arg = varlist;

    //XThread_currentThread();//初始化

    return Object;
}
XThread* XThread_create(XObject* parent)
{
    XThread* Object = (XThread*)XMemory_malloc(sizeof(XThread));
    if (Object == NULL) {
        return NULL;
    }
    XThread_init(Object);
    SET_CLASS_HEAP(Object);
    Object->m_start_routine = NULL;
    Object->m_arg = NULL;

    //XThread_currentThread();//初始化

    return Object;
}
// 初始化 XThread 对象
void XThread_init(XThread* Object)
{
    XObject_init(Object);
    XClassGetVtable(Object) = XThread_class_init();
    Object->m_handle = 0;
    Object->m_finished = false;
    Object->m_interruptionRequested = false;
    Object->loopLevel = 0;
    Object->m_priority = XThread_NormalPriority;
    Object->m_stackSize = 512;
    Object->m_data = XThreadData_create(Object);
    //((XObject*)Object)->m_thread = Object;//将线程指针设为自己，才可以使用事件
}
XThread* XThread_currentThread()
{
    XThreadData* data = XThreadData_current();
    return data ? data->m_thread:NULL;
}
XEventDispatcher* XThread_currentDispatcher()
{
    XThread* th = XThread_currentThread();
    return th ? XThread_dispatcher(th) : XCoreApplication_eventDispatcher();
}



// 获取 XThread 句柄
XHandle XThread_getHandle(XThread* Object)
{
    return Object->m_handle;
}

// 获取事件调度器
XEventDispatcher* XThread_dispatcher(const XThread* Object)
{
    if (Object)
        return Object->m_data? Object->m_data->m_dispatcher:NULL;
    return XCoreApplication_eventDispatcher();
}

// 判断线程是否被请求中断
bool XThread_isInterruptionRequested(const XThread* Object)
{
    return Object->m_interruptionRequested;
}
// 获取线程循环级别
int XThread_loopLevel(const XThread* Object)
{
    return Object->loopLevel;
}

// 获取线程优先级
XThread_Priority XThread_priority(const XThread* Object)
{
    return Object->m_priority;
}

// 请求中断线程
void XThread_requestInterruption(XThread* Object)
{
    Object->m_interruptionRequested = true;
}

// 设置事件调度器
void XThread_setEventDispatcher(XThread* Object, XEventDispatcher* eventDispatcher)
{
    if (!Object || !eventDispatcher)return;
    if (Object->m_data && Object->m_data->m_dispatcher)
        XClass_delete_base(Object->m_data->m_dispatcher);
    Object->m_data->m_dispatcher=eventDispatcher;
}


// 获取线程栈大小
uint32_t XThread_stackSize(const XThread* Object)
{
    return Object->m_stackSize;
}

int XThread_exec(XThread* thread)
{
    if (!thread) return;
    XEventLoop loop;
    XEventLoop_init(&loop);
    int result = XEventLoop_exec(&loop);
    XEventLoop_deinitLater(&loop);
    return result;
}

void XThread_run_base(XThread* thread)
{
    return XClassGetVirtualFunc(thread, EXThread_Run, bool(*)(XThread*))(thread);
}
//虚函数默认实现,供平台实现文件调用
void VXThread_run(XThread* thread)
{
    if (thread && thread->m_start_routine)
        thread->m_start_routine(thread,thread->m_arg);
}