/* 单线程（裸机）同步原语后端 —— 取代 Drive/FreeRTOS/Sync/*FreeRTOS.c
 * ---------------------------------------------------------------------------
 * 背景：本轮总装删除仓库 FreeRTOS V9（port.c/portasm.S/port_isr.c/heap_4.c +
 * tasks/list/queue + xTaskStartScheduler），回到「XCoreApplication 事件循环即
 * 主循环」的单线程形态。Drive/FreeRTOS/Sync 下四个后端文件整体被
 * `#ifdef __FreeRTOS__` 包着，本工程不再定义该宏 → 链接期缺少
 * XMutex_platform_* / XSemaphore_* / XWaitCondition_* / XThread_* 四族符号。
 * 本文件按单线程语义补齐这四族：
 *   - XMutex        ：PlatformPrivate 退化为一个 owner 标志，lock/tryLock/
 *                     unlock 在单线程下恒成功（tryLock 恒 true）；
 *   - XSemaphore    ：只保留原实现的计数语义（结构体同构，去掉 FreeRTOS
 *                     计数信号量句柄）；acquire 单线程下不阻塞，只做计数断言；
 *   - XWaitCondition：wait_count 计数 + 谓词返回（单线程无并发等待者，
 *                     wait 直接返回 true，wake 仅计数）；
 *   - XThread       ：不创建任务；start 以「同步跑完运行体」语义执行
 *                     （VXThread_run 跑到 m_finished 置位），handle 恒 1；
 *                     TLS 用一个静态槽（单线程只有一条执行流）。
 *
 * 铁律：本文件不涉及中断/时序/UART，不触碰 cre.S / --defsym / I2C 时序。
 */
#include "XMutex.h"
#include "XSemaphore.h"
#include "XWaitCondition.h"
#include "XThread.h"
#include "XThreadData.h"
#include "XVarList.h"
#include "XTask.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XMemory.h"
#include "XObject.h"
#include <string.h>

#if XSYNC_ON

/* ============================ XMutex ============================ */
/* 与 Drive/FreeRTOS/Sync/XMutexFreeRTOS.c 的 PlatformPrivate 同名同角色：
 * XMutex.c:41 的 XMutex_getPlatformPrivate 取的就是本结构体首地址。 */
struct PlatformPrivate {
    unsigned char locked;   /* 单线程下仅作调试标记（1=持有中） */
};

size_t XMutex_PlatformPrivate_size(void)
{
    return sizeof(struct PlatformPrivate);
}

void XMutex_platform_init(struct PlatformPrivate* p)
{
    if (p) p->locked = 0;
}

void XMutex_platform_deinit(struct PlatformPrivate* p)
{
    if (p) p->locked = 0;
}

void XMutex_platform_lock(struct PlatformPrivate* p)
{
    if (p) p->locked = 1;
}

bool XMutex_platform_tryLock(struct PlatformPrivate* p)
{
    if (!p) return false;
    if (p->locked) return false;
    p->locked = 1;
    return true;
}

void XMutex_platform_unlock(struct PlatformPrivate* p)
{
    if (p) p->locked = 0;
}

/* ============================ XSemaphore ============================ */
/* 结构体同构于 XSemaphoreFreeRTOS.c（去掉 xSemaphoreHandle 字段）。 */
struct XSemaphore {
    volatile int32_t available_count;
    int32_t maximum;
};

size_t XSemaphore_typeSize(void)
{
    return sizeof(struct XSemaphore);
}

void XSemaphore_init(XSemaphore* sem, int32_t initial, int32_t maximum)
{
    if (!sem || initial < 0 || maximum <= 0 || initial > maximum) return;
    sem->available_count = initial;
    sem->maximum = maximum;
}

void XSemaphore_deinit(XSemaphore* sem)
{
    if (sem) { sem->available_count = 0; sem->maximum = 0; }
}

XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum)
{
    XSemaphore* sem = (XSemaphore*)XMalloc_System(sizeof(struct XSemaphore));
    if (sem) XSemaphore_init(sem, initial, maximum);
    return sem;
}

void XSemaphore_delete(XSemaphore* sem)
{
    if (sem) { XSemaphore_deinit(sem); XFree_System(sem); }
}

void XSemaphore_acquire(XSemaphore* sem, int32_t n)
{
    /* 单线程无并发释放者：计数只会够用；不足时钳到 0（不阻塞主循环）。 */
    if (!sem || n <= 0) return;
    sem->available_count -= n;
    if (sem->available_count < 0) sem->available_count = 0;
}

void XSemaphore_release(XSemaphore* sem, int32_t n)
{
    if (!sem || n <= 0) return;
    sem->available_count += n;
    if (sem->maximum > 0 && sem->available_count > sem->maximum)
        sem->available_count = sem->maximum;
}

bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n)
{
    if (!sem || n <= 0) return false;
    if (sem->available_count < n) return false;
    sem->available_count -= n;
    return true;
}

bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout)
{
    (void)timeout;
    return XSemaphore_tryAcquire(sem, n);
}

int32_t XSemaphore_available(XSemaphore* sem)
{
    return sem ? sem->available_count : -1;
}

/* ============================ XWaitCondition ============================ */
struct XWaitCondition {
    volatile int32_t wait_count;
};

size_t XWaitCondition_typeSize(void)
{
    return sizeof(struct XWaitCondition);
}

void XWaitCondition_init(XWaitCondition* cond)
{
    if (cond) cond->wait_count = 0;
}

void XWaitCondition_deinit(XWaitCondition* cond)
{
    if (cond) cond->wait_count = 0;
}

XWaitCondition* XWaitCondition_create(void)
{
    XWaitCondition* cond = (XWaitCondition*)XMalloc_System(sizeof(struct XWaitCondition));
    if (cond) XWaitCondition_init(cond);
    return cond;
}

void XWaitCondition_delete(XWaitCondition* cond)
{
    if (!cond) return;
    XWaitCondition_deinit(cond);
    XFree_System(cond);
}

bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int32_t timeout)
{
    /* 单线程不存在「让出后由他人唤醒」的场景：等待即刻谓词为真。
     * 仍按原契约释放/重取互斥量，保持调用方的临界区语义。 */
    (void)timeout;
    if (!cond || !mutex) return false;
    cond->wait_count++;
    XMutex_unlock(mutex);
    cond->wait_count--;
    XMutex_lock(mutex);
    return true;
}

void XWaitCondition_wakeOne(XWaitCondition* cond)
{
    if (!cond) return;
    if (cond->wait_count > 0) cond->wait_count--;
}

void XWaitCondition_wakeAll(XWaitCondition* cond)
{
    if (cond) cond->wait_count = 0;
}

#if XTHREAD_ON
/* ============================ XThread ============================ */
/* 公共层运行体（Src/XCode/XSync/XThread/XThread.c） */
extern void VXThread_run(XThread* thread);

static void VXThread_deinit(XThread* thread)
{
    if (!thread) return;
    XTask_unregisterThread(thread);
    thread->m_handle = 0;
    thread->m_finished = 1;
    if (thread->m_varList) {
        XVarList_delete(thread->m_varList);
        thread->m_varList = NULL;
    }
    if (thread->m_loop) {
        XObject_deleteLater(thread->m_loop);
        thread->m_loop = NULL;
    }
    {
        XThreadData* data = thread->m_data;
        XClass_Deinit_Parent(XObject, thread);
        if (data) {
            XThreadData_delete(data);
            thread->m_data = NULL;
        }
    }
}

XVtable* XThread_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XThread)
        XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { VXThread_run };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XThread);
    return XVTABLE_DEFAULT;
}

bool XThread_start(XThread* thread)
{
    /* 单线程「启动」= 同步跑完运行体。控制台/事件循环路径不调用本函数
     * （XConsoleShell 的外部进程命令在本固件裁剪档为 0），真被调到时按
     * 同步语义执行，不派生任务、不占栈。 */
    if (!thread || thread->m_handle != 0) return false;
    thread->m_handle = (XHandle)1;
    VXThread_run(thread);
    thread->m_finished = 1;
    thread->m_handle = 0;
    return true;
}

bool XThread_wait(XThread* thread, uint32_t time)
{
    (void)time;
    return thread ? thread->m_finished != 0 : false;
}

bool XThread_isFinished(const XThread* thread)
{
    return thread ? (thread->m_finished != 0) : false;
}

bool XThread_isRunning(const XThread* thread)
{
    if (!thread || thread->m_handle == 0) return false;
    return thread->m_finished == 0;
}

XThread_Priority XThread_priority(const XThread* thread)
{
    if (!thread || thread->m_priority == XThread_InvalidPriority)
        return XThread_NormalPriority;
    return thread->m_priority;
}

uint32_t XThread_stackSize(const XThread* thread)
{
    return thread ? thread->m_stackSize : 0;
}

void XThread_setPriority(XThread* thread, XThread_Priority priority)
{
    if (thread) thread->m_priority = priority;
}

void XThread_setStackSize(XThread* thread, uint32_t stackSize)
{
    if (thread) thread->m_stackSize = stackSize;
}

bool XThread_terminate(XThread* thread)
{
    if (!thread) return false;
    thread->m_interruptionRequested = 1;
    thread->m_finished = 1;
    return true;
}
#endif /* XTHREAD_ON */

int XThread_idealThreadCount(void)
{
    return 1;
}

void XThread_msleep(uint32_t msecs)
{
    /* 单线程事件循环里「睡眠」只能是空转让出——NONOS 的 os_timer 回调
     * 同在 SYS 上下文，让不出去。调用点（XConsoleShellGpio/Network 的
     * 分片等待）在本固件裁剪档不可达；留空实现保证可链接。 */
    (void)msecs;
}

void XThread_sleep(uint32_t secs)
{
    (void)secs;
}

void XThread_usleep(uint32_t usecs)
{
    (void)usecs;
}

void XThread_yieldCurrentThread(void)
{
    /* 单线程无调度器可让。 */
}

/* ---- TLS：单线程只有一条执行流，一个静态槽即可 ---- */
static void* g_thread_storage = 0;

XHandle XThread_currentThreadId(void)
{
    return (XHandle)&g_thread_storage;   /* 恒非 0，充当线程标识 */
}

void XThreadStorage_set(void* p)
{
    g_thread_storage = p;
}

void* XThreadStorage_get(void)
{
    return g_thread_storage;
}

void XThreadStorage_taskCleanup(void* task)
{
    /* 无 RTOS 内核，无 TCB 释放钩子。单线程下线程退出即进程结束。 */
    (void)task;
}

#endif /* XSYNC_ON */
