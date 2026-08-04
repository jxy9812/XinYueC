#ifdef _WIN32
#include"XThread.h"
#include"XEvent.h"
#include"XHashSet.h"
#include"XMemory.h"
#include"XObject.h"
#include"XEventLoop.h"
#include"XThreadData.h"
#include <windows.h>
#include"XAtomic.h"
void VXThread_run(XThread* thread);
static void VXThread_deinit(XThread* thread);
// 虚函数表初始化
XVtable* XThread_class_init()
{
    XVTABLE_INIT_DEFAULT(XThread)
	XCLASS_SET_CLASS_NAME_DEFAULT("XThread");
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXThread_run
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XThread);
    return XVTABLE_DEFAULT;
}


// 线程函数包装器，用于调用事件调度器
static DWORD WINAPI ThreadFunction(LPVOID lpParam) 
{
    XThread* thread = (XThread*)lpParam;
    //XPrintf("XThread:%p XVector* children:%p\n", thread,((XObject*)thread)->children);
  
    //运行函数
    XThread_run_base(thread);
   
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

bool XThread_wait(XThread* thread, uint32_t time)
{
    if (thread->m_handle == NULL) {
        return false;
    }
    DWORD result = WaitForSingleObject(thread->m_handle, time);
    return (result == WAIT_OBJECT_0);
}

bool XThread_isFinished(const XThread* thread)
{
    if (thread->m_handle == NULL) {
        return false;
    }
    DWORD exitCode;
    if (GetExitCodeThread(thread->m_handle, &exitCode)) {
        return (exitCode != STILL_ACTIVE);
    }
    return false;
}

bool XThread_isRunning(const XThread* thread)
{
    if (thread->m_handle == NULL) {
        return false;
    }
    DWORD exitCode;
    if (GetExitCodeThread(thread->m_handle, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return false;
}

XThread_Priority XThread_priority(const XThread* thread)
{
    if (!thread) {
        return XThread_NormalPriority;
    }

    // 如果不是主线程，直接返回缓存的值
    if (!thread->m_isMainThread|| thread->m_priority!= XThread_err) {
        return thread->m_priority;
    }

    // 主线程：从操作系统查询真实优先级
    int currentWinPriority = GetThreadPriority(GetCurrentThread());
    if (currentWinPriority == THREAD_PRIORITY_ERROR_RETURN) {
        // 查询失败，返回默认值
        printf("Failed to get main thread priority: %d\n", GetLastError());
        return XThread_NormalPriority;
    }

    // 将 Windows 优先级映射回 XThread_Priority 枚举
    XThread_Priority xPriority;
    switch (currentWinPriority) {
    case THREAD_PRIORITY_IDLE:
        xPriority = XThread_IdlePriority;
        break;
    case THREAD_PRIORITY_LOWEST:
        xPriority = XThread_LowestPriority;
        break;
    case THREAD_PRIORITY_BELOW_NORMAL:
        xPriority = XThread_LowPriority;
        break;
    case THREAD_PRIORITY_NORMAL:
        xPriority = XThread_NormalPriority;
        break;
    case THREAD_PRIORITY_ABOVE_NORMAL:
        xPriority = XThread_HighPriority;
        break;
    case THREAD_PRIORITY_HIGHEST:
        xPriority = XThread_HighestPriority;
        break;
    case THREAD_PRIORITY_TIME_CRITICAL:
        xPriority = XThread_TimeCriticalPriority;
        break;
    default:
        // 对于其他不常见的优先级（如 THREAD_PRIORITY_ABOVE_NORMAL + 1），统一视为 Normal
        xPriority = XThread_NormalPriority;
        break;
    }

    // 可选：更新对象内部的缓存值，使其与真实值同步
    //((XThread*)thread)->m_priority = xPriority;

    return xPriority;
}
// 获取线程栈大小
uint32_t XThread_stackSize(const XThread* thread)
{
    if (!thread) {
        return 0;
    }

    // 如果不是主线程，直接返回缓存的值（对于工作线程，这是创建时设置的）
    if (!thread->m_isMainThread|| thread->m_stackSize) {
        return thread->m_stackSize;
    }

    // 主线程：从 TEB (Thread Environment Block) 获取栈信息
    // NtCurrentTeb() 是一个内联函数，可直接获取当前线程的 TEB
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();

    // 栈基地址 (Stack Base) 是栈的最高地址
    // 栈限制地址 (Stack Limit) 是栈的最低地址
    // 栈大小 = StackBase - StackLimit
    size_t stackSize = (size_t)tib->StackBase - (size_t)tib->StackLimit;

    // 可选：更新对象内部的缓存值，使其与真实值同步
    //((XThread*)thread)->m_stackSize = (uint32_t)stackSize;

    return (uint32_t)stackSize;
}
void VXThread_requestInterruption(XThread* thread)
{
    thread->m_interruptionRequested = true;
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


    // 判断是否为主线程
    if (Object->m_isMainThread) {
        // 主线程：使用 GetCurrentThread() 获取当前线程的伪句柄
        if (!SetThreadPriority(GetCurrentThread(), winPriority)) {
            printf("Failed to set main thread priority: %d\n", GetLastError());
        }
    }
    else {
        // 工作线程：使用已保存的 m_handle
        if (Object->m_handle != NULL) {
            if (!SetThreadPriority(Object->m_handle, winPriority)) {
                printf("Failed to set worker thread priority: %d\n", GetLastError());
            }
        }
    }
    // 无论成功与否，都更新对象内部的状态
    Object->m_priority = priority;
}

void XThread_setStackSize(XThread* thread, uint32_t m_stackSize)
{
    thread->m_stackSize = m_stackSize;
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

void VXThread_deinit(XThread* thread)
{
    if (XThread_isRunning(thread))
        XThread_requestInterruption(thread);
    XThread_wait(thread,UINT32_MAX);
    if (thread->m_handle != NULL) 
    {
        CloseHandle(thread->m_handle);
        thread->m_handle = NULL;
    }
    if (thread->m_varList)
    {
        XVarList_delete(thread->m_varList);
        thread->m_varList = NULL;
    }
    if (thread->m_loop)
    {
        XObject_deleteLater(thread->m_loop);
        thread->m_loop = NULL;
    }
    XThreadData* data = thread->m_data;
    XClass_Deinit_Parent(XObject, thread);
    if(data)
    {
        XThreadData_delete(data);
        thread->m_data = NULL;
    }
    //XFree_System(thread);
}

XHandle XThread_currentThreadId()
{
    return GetCurrentThreadId();
}
int XThread_idealThreadCount()
{
    return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}
// 毫秒级休眠 (milliseconds)
void XThread_msleep(uint32_t msecs)
{
    // Windows API Sleep 函数的参数单位就是毫秒
    Sleep(msecs);
}

// 秒级休眠 (seconds)
void XThread_sleep(uint32_t secs)
{
    // 将秒转换为毫秒 (1秒 = 1000毫秒)
    Sleep(secs * 1000);
}

// 微秒级休眠 (microseconds)
// 注意：Windows 的 Sleep 精度通常在 1-15 毫秒左右，
// 所以小于 1000 微秒 (1毫秒) 的休眠可能不精确。
void XThread_usleep(uint32_t usecs)
{
    // 将微秒转换为毫秒
    // 如果 usecs < 1000, 那么 usecs / 1000 的结果是 0
    // 此时 Sleep(0) 会主动让出 CPU 时间片，但不会真正休眠
    DWORD msecs = usecs / 1000;

    // 处理微秒不足1毫秒的情况
    if (msecs == 0 && usecs > 0) {
        // 对于非常短的延迟，调用 Sleep(0) 让出时间片
        Sleep(0);
    }
    else {
        Sleep(msecs);
    }
}
void XThread_yieldCurrentThread()
{
    SwitchToThread();
}
#ifndef TLS_OUT_OF_BOUNDS
#define TLS_OUT_OF_BOUNDS ((DWORD)0xFFFFFFFF)   /* TlsAlloc 失败返回值,部分 SDK 未定义 */
#endif
//==================== TLS: 每线程指针存储 (对标 Qt 6.8 QThreadStorage) ====================
//使用 TlsAlloc 分配 TLS 槽,首次访问通过原子 CAS 初始化(once 语义)
//存储 (slotIndex+1) 以区分 "索引0合法" 的情况
static XAtomic_uintptr_t g_tlsSlot = { 0 };

static DWORD XThreadStorage_slot(void)
{
    uintptr_t v = XAtomic_load_uintptr_t(&g_tlsSlot, XAtomic_MemoryOrder_Acquire);
    if (v)
        return (DWORD)(v - 1);            //还原真实索引
    DWORD slot = TlsAlloc();
    if (slot == TLS_OUT_OF_BOUNDS)
        return TLS_OUT_OF_BOUNDS;
    uintptr_t expected = 0;
    if (XAtomic_compare_exchange_strong_uintptr_t(&g_tlsSlot, &expected, (uintptr_t)slot + 1,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire))
        return slot;                       //赢家:已发布
    TlsFree(slot);                         //输家:另一线程已发布,释放本次冗余
    return (DWORD)(expected - 1);
}

void XThreadStorage_set(void* p)
{
    DWORD slot = XThreadStorage_slot();
    if (slot != TLS_OUT_OF_BOUNDS)
        TlsSetValue(slot, p);
}

void* XThreadStorage_get(void)
{
    DWORD slot = XThreadStorage_slot();
    if (slot == TLS_OUT_OF_BOUNDS)
        return NULL;
    return TlsGetValue(slot);
}

#endif