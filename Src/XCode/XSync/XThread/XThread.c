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

// 初始化 XThread 对象
void XThread_init(XThread* Object)
{
    XClass_init((XClass*)Object);
    XClassGetVtable(Object) = XThread_class_init();
    Object->m_handle = 0;
    Object->m_finished = false;
    Object->m_interruptionRequested = false;
    Object->loopLevel = 0;
    Object->m_priority = XThread_NormalPriority;
    Object->m_stackSize = 512;
    Object->m_data = XThreadData_create(Object);
    //Object->m_eventLoop = XEventLoop_create();
    //Object->m_eventLoop->m_dispatcher = Object->m_data->m_dispatcher;
}
XThread* XThread_currentThread()
{
    XThreadData* data = XThreadData_current();
    return data ? data->m_thread:NULL;
}
bool XThread_terminate_base(XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_Terminate, bool(*)(const XThread*))(Object);
}
XEventLoop* XThread_currentEventLoop()
{
    XThread* th = XThread_currentThread();
    return th?XThread_eventLoop(th):XCoreApplication_eventLoop();
}
XEventDispatcher* XThread_currentDispatcher()
{
    XThread* th = XThread_currentThread();
    return th ? XThread_dispatcher(th) : XCoreApplication_dispatcher();
}



// 获取 XThread 句柄
XHandle XThread_getHandle(XThread* Object)
{
    return Object->m_handle;
}

// 等待 XThread 结束
bool XThread_wait_base(XThread* Object, unsigned long time)
{
    return XClassGetVirtualFunc(Object, EXThread_Wait, bool(*)(XThread*, unsigned long))(Object, time);
}

// 获取事件调度器
XEventDispatcher* XThread_dispatcher(const XThread* Object)
{
    if (Object)
        return Object->m_data? Object->m_data->m_dispatcher:NULL;
    return XCoreApplication_dispatcher();
}

XEventLoop* XThread_eventLoop(const XThread* Object)
{
    return Object? Object->m_eventLoop:NULL;
}

// 判断线程是否结束
bool XThread_isFinished_base(const XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_IsFinished, bool(*)(const XThread*))(Object);
}

// 判断线程是否被请求中断
bool XThread_isInterruptionRequested(const XThread* Object)
{
    return Object->m_interruptionRequested;
}

// 判断线程是否正在运行
bool XThread_isRunning_base(const XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_IsRunning, bool(*)(const XThread*))(Object);
}

// 获取线程循环级别
int XThread_loopLevel_base(const XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_LoopLevel, int(*)(const XThread*))(Object);
}

// 获取线程优先级
XThread_Priority XThread_priority_base(const XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_Priority, XThread_Priority(*)(const XThread*))(Object);
}

// 请求中断线程
void XThread_requestInterruption_base(XThread* Object)
{
    XClassGetVirtualFunc(Object, EXThread_RequestInterruption, void(*)(XThread*))(Object);
}

// 设置事件调度器
void XThread_setEventDispatcher(XThread* Object, XEventDispatcher* eventDispatcher)
{
    if (!Object || !eventDispatcher)return;
    if (Object->m_data && Object->m_data->m_dispatcher)
        XClass_delete_base(Object->m_data->m_dispatcher);
    Object->m_data->m_dispatcher=eventDispatcher;
   /* if (Object->m_eventLoop->m_dispatcher != NULL)
        XEventDispatcher_delete_base(Object->m_eventLoop->m_dispatcher);
    Object->m_eventLoop->m_dispatcher = eventDispatcher;*/
}

// 设置线程优先级
void XThread_setPriority_base(XThread* Object, XThread_Priority priority)
{
    XClassGetVirtualFunc(Object, EXThread_SetPriority, void(*)(XThread*, XThread_Priority))(Object, priority);
}

// 设置线程栈大小
void XThread_setStackSize_base(XThread* Object, uint32_t stackSize)
{
    XClassGetVirtualFunc(Object, EXThread_SetStackSize, void(*)(XThread*, uint32_t))(Object, stackSize);
}

// 获取线程栈大小
uint32_t XThread_stackSize(const XThread* Object)
{
    return Object->m_stackSize;
}

// 启动线程
bool XThread_start_base(XThread* Object)
{
    return XClassGetVirtualFunc(Object, EXThread_Start, bool(*)(XThread*))(Object);
}