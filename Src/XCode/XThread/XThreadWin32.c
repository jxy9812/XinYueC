#ifdef _WIN32
#include"XThread.h"
#include"XEvent.h"
#include"XHashSet.h"
#include"XMemory.h"
#include"XObject.h"
#include"XEventDispatcherThread.h"
#include <windows.h>
static bool VXThread_start(XThread* Object);
static bool VXThread_wait(XThread* Object, unsigned long time);
static bool VXThread_isFinished(const XThread* Object);
static bool VXThread_isRunning(const XThread* Object);
static int VXThread_loopLevel(const XThread* Object);
static XThread_Priority VXThread_priority(const XThread* Object);
static void VXThread_requestInterruption(XThread* Object);
static void VXThread_setPriority(XThread* Object, XThread_Priority priority);
static void VXThread_setStackSize(XThread* Object, uint32_t m_stackSize);

static void VXThread_delete(XThread* Object);
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
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());
        void* table[] = {
         VXThread_start,VXThread_wait,
         VXThread_isFinished,
         VXThread_isRunning,VXThread_loopLevel,VXThread_priority,
         VXThread_requestInterruption,
         VXThread_setPriority,VXThread_setStackSize
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXThread_delete);
#if SHOWCONTAINERSIZE
    printf("XThread size:%d\n", XVtable_size(XThreadVtable));
#endif
    return XVTABLE_DEFAULT;
}


// 线程函数包装器，用于调用事件调度器
static DWORD WINAPI ThreadFunction(LPVOID lpParam) {
    XThread* Object = (XThread*)lpParam;
    //运行函数
    if (Object->m_start_routine)
        Object->m_start_routine(Object->m_arg);
    //运行事件调度
    if (Object->m_eventDispatcher->m_Objects && !XSetBase_isEmpty_base(Object->m_eventDispatcher->m_Objects))
    {
        while (!(Object->m_interruptionRequested))
        {
            XEventDispatcherThread_handler_base(Object->m_eventDispatcher);
        }
    }
    Object->m_finished = true;
    return 0;
}

bool VXThread_start(XThread* Object)
{
    if (Object->m_handle != NULL) 
    {
        return false; // 线程已经启动
    }
   Object->m_handle = CreateThread(NULL, Object->m_stackSize, ThreadFunction, Object, 0, NULL);
    if (Object->m_handle == NULL) 
    {
        return false;
    }
    XThread_setPriority_base(Object, XThread_priority_base(Object));
    return true;
}

bool VXThread_wait(XThread* Object, unsigned long time)
{
    if (Object->m_handle == NULL) {
        return false;
    }
    DWORD result = WaitForSingleObject(Object->m_handle, time);
    return (result == WAIT_OBJECT_0);
}

bool VXThread_isFinished(const XThread* Object)
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

bool VXThread_isRunning(const XThread* Object)
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

void VXThread_setPriority(XThread* Object, XThread_Priority priority)
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
        SetThreadPriority(Object->m_handle, winPriority);
    }
    Object->m_priority = priority;
}

void VXThread_setStackSize(XThread* Object, uint32_t m_stackSize)
{
    Object->m_stackSize = m_stackSize;
}

void VXThread_delete(XThread* Object)
{
    if (XThread_isRunning_base(Object))
        XThread_requestInterruption_base(Object);
    XThread_wait_base(Object,UINT32_MAX);
   /* if (Object->m_handle != NULL) 
    {
        CloseHandle(Object->m_handle);
       Object->m_handle = NULL;
    }*/
    if (Object->m_eventDispatcher)
        XEventDispatcher_delete_base(Object->m_eventDispatcher);
    XThread_mapRemove(Object);
    XMemory_free(Object);
}

XHandle XThread_currentThreadId()
{
    return GetCurrentThreadId();
}
#endif