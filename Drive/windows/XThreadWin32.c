#ifdef _WIN32
#include"XThread.h"
#include"XEvent.h"
#include"XHashSet.h"
#include"XMemory.h"
#include"XObject.h"
#include"XEventLoop.h"
#include"XThreadData.h"
#include <windows.h>
void VXThread_run(XThread* thread);
static void VXThread_deinit(XThread* Object);
// 虚函数表初始化
XVtable* XThread_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XThread))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_DEFAULT(XObject_class_init());
    void* table[] = {
        VXThread_run
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
#if SHOWCONTAINERSIZE
    printf("XThread size:%d\n", XVtable_size(XThreadVtable));
#endif
    return XVTABLE_DEFAULT;
}


// 线程函数包装器，用于调用事件调度器
static DWORD WINAPI ThreadFunction(LPVOID lpParam) 
{
    XThread* obj = (XThread*)lpParam;
    //XPrintf("XThread:%p XVector* children:%p\n", obj,((XObject*)obj)->children);
    obj->m_finished = false;
    obj->m_running = true;
    XThreadData_mapInsert(obj->m_data);
    //运行函数
    XThread_run_base(obj);
    obj->m_finished = true;
    obj->m_running = false;
    XThreadData_mapInsert(obj->m_data);
    return 0;
}

bool XThread_start(XThread* thread)
{
    if (thread->m_handle != NULL) 
    {
        return false; // 线程已经启动
    }
   thread->m_handle = CreateThread(NULL, thread->m_stackSize, ThreadFunction, thread, 0, NULL);
    if (thread->m_handle == NULL) 
    {
        return false;
    }
    XThread_setPriority(thread, XThread_priority(thread));
    return true;
}

bool XThread_wait(XThread* Object, unsigned long time)
{
    if (Object->m_handle == NULL) {
        return false;
    }
    DWORD result = WaitForSingleObject(Object->m_handle, time);
    return (result == WAIT_OBJECT_0);
}

bool XThread_isFinished(const XThread* Object)
{
    if (Object->m_handle == NULL) {
        return false;
    }
    DWORD exitCode;
    if (GetExitCodeThread(Object->m_handle, &exitCode)) {
        return (exitCode != STILL_ACTIVE);
    }
    return false;
}

bool XThread_isRunning(const XThread* Object)
{
    if (Object->m_handle == NULL) {
        return false;
    }
    DWORD exitCode;
    if (GetExitCodeThread(Object->m_handle, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return false;
}

int VXThread_loopLevel(const XThread* Object)
{
    return Object->loopLevel;
}

XThread_Priority VXThread_priority(const XThread* Object)
{
    return Object->m_priority;
}

void VXThread_requestInterruption(XThread* Object)
{
    Object->m_interruptionRequested = true;
}

void XThread_setPriority(XThread* Object, XThread_Priority priority)
{
    int winPriority;
    switch (priority) {
    case XThread_IdlePriority:
        winPriority = THREAD_PRIORITY_IDLE;
        break;
    case XThread_LowestPriority:
        winPriority = THREAD_PRIORITY_LOWEST;
        break;
    case XThread_LowPriority:
        winPriority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case XThread_NormalPriority:
        winPriority = THREAD_PRIORITY_NORMAL;
        break;
    case XThread_HighPriority:
        winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    case XThread_HighestPriority:
        winPriority = THREAD_PRIORITY_HIGHEST;
        break;
    case XThread_TimeCriticalPriority:
        winPriority = THREAD_PRIORITY_TIME_CRITICAL;
        break;
    case XThread_InheritPriority:
        winPriority = THREAD_PRIORITY_NORMAL;
        break;
    default:
        winPriority = THREAD_PRIORITY_NORMAL;
        break;
    }
    if (Object->m_handle != NULL) {
        if (!SetThreadPriority(Object->m_handle, winPriority)) {
            // 可选：记录错误日志，如GetLastError()
             printf("Failed to set thread priority: %d\n", GetLastError());
        }
    }
    Object->m_priority = priority;
}

void XThread_setStackSize(XThread* Object, uint32_t m_stackSize)
{
    Object->m_stackSize = m_stackSize;
}

bool XThread_terminate(XThread* Object)
{
    if (Object->m_handle != NULL && XThread_isRunning(Object)) {
        if (!TerminateThread(Object->m_handle, 0))
            return false;
        Object->m_finished = true;
    }
    return true;
}

void VXThread_deinit(XThread* Object)
{
    XThreadData_delete(Object->m_data);

    if (XThread_isRunning(Object))
        XThread_requestInterruption(Object);
    XThread_wait(Object,UINT32_MAX);
    if (Object->m_handle != NULL) 
    {
        CloseHandle(Object->m_handle);
        Object->m_handle = NULL;
    }
    if (Object->m_arg)XVarList_delete(Object->m_arg);
    XClass_Deinit_Parent(XObject,Object);
    //XMemory_free(obj);
}

XHandle XThread_currentThreadId()
{
    return GetCurrentThreadId();
}

#endif