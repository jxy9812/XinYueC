/**
 * @file XThreadPosix.c
 * @brief XThread POSIX 平台实现（Linux/macOS/BSD）
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#define _POSIX_C_SOURCE 200809L
#include "XThread.h"
#include "XThreadData.h"
#include "XTask.h"
#include "XEventLoop.h"
#include "XMemory.h"
#include "XVarList.h"
#include "CXinYueConfig.h"
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XSYNC_ON
#if XTHREAD_ON
void VXThread_run(XThread* thread);
/* =========================================================================
 * 虚函数表
 * ========================================================================= */
static void VXThread_deinit(XThread* thread);

XVtable* XThread_class_init()
{
    XVTABLE_INIT_DEFAULT(XThread)
   //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXThread_run
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XThread);
    return XVTABLE_DEFAULT;
}

/* =========================================================================
 * 线程函数包装器
 * ========================================================================= */
static void* ThreadFunction(void* arg)
{
    XThread* thread = (XThread*)arg;
    XThread_run_base(thread);
    return NULL;
}

/* =========================================================================
 * 平台 API 实现
 * ========================================================================= */
bool XThread_start(XThread* thread)
{
    if (!thread || thread->m_handle != 0) {
        return false;
    }

    pthread_t pthread;
    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    if (ret != 0) return false;

    if (thread->m_stackSize >= PTHREAD_STACK_MIN) {
        ret = pthread_attr_setstacksize(&attr, thread->m_stackSize);
        if (ret != 0) {
            pthread_attr_destroy(&attr);
            return false;
        }
    }

    thread->m_finished = false;
    thread->m_running = true;
    thread->m_interruptionRequested = false;
    ret = pthread_create(&pthread, &attr, ThreadFunction, thread);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        thread->m_running = false;
        return false;
    }

    thread->m_handle = (XHandle)pthread;
    XThread_setPriority(thread, XThread_priority(thread));
    return true;
}

bool XThread_wait(XThread* thread, uint32_t time)
{
    if (!thread || thread->m_handle == 0) return false;

    pthread_t pthread = (pthread_t)thread->m_handle;
    if (pthread_equal(pthread, pthread_self()))
        return false;

    int ret;
    if (time == UINT32_MAX) {
        ret = pthread_join(pthread, NULL);
    }
#if defined(__linux__)
    else {
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
            return false;
        deadline.tv_sec += time / 1000;
        deadline.tv_nsec += (long)(time % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        ret = pthread_timedjoin_np(pthread, NULL, &deadline);
    }
#else
    else {
        struct timespec start;
        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
            return false;
        uint64_t timeout_ns = (uint64_t)time * 1000000ULL;
        while (!thread->m_finished) {
            struct timespec now;
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                return false;
            uint64_t elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000000000ULL +
                               (uint64_t)(now.tv_nsec - start.tv_nsec);
            if (elapsed >= timeout_ns)
                return false;
            usleep(1000);
        }
        ret = pthread_join(pthread, NULL);
    }
#endif

    if (ret != 0)
        return false;

    thread->m_handle = 0;
    thread->m_running = false;
    thread->m_finished = true;
    return true;
}

bool XThread_terminate(XThread* thread)
{
    if (!thread || thread->m_handle == 0) return false;

    pthread_t pthread = (pthread_t)thread->m_handle;
    int ret = pthread_cancel(pthread);
    if (ret != 0) return false;

    void* result;
    ret = pthread_join(pthread, &result);
    if (ret != 0) return false;

    thread->m_finished = true;
    thread->m_running = false;
    thread->m_handle = 0;
    thread->m_interruptionRequested = false;
    return true;
}

void XThread_setPriority(XThread* thread, XThread_Priority priority)
{
    if (!thread) return;
    thread->m_priority = priority;
    if (thread->m_handle == 0) return;

    int policy;
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    pthread_t pthread = (pthread_t)thread->m_handle;

    if (pthread_getschedparam(pthread, &policy, &param) != 0) return;

    switch (priority) {
    case XThread_IdlePriority:
        policy = SCHED_OTHER;
        param.sched_priority = 0;
        break;
    case XThread_LowestPriority:
        param.sched_priority = sched_get_priority_min(policy);
        break;
    case XThread_LowPriority: {
        int min = sched_get_priority_min(policy);
        int max = sched_get_priority_max(policy);
        param.sched_priority = min + (max - min) / 4;
        break;
    }
    case XThread_NormalPriority:
        policy = SCHED_OTHER;
        param.sched_priority = 0;
        break;
    case XThread_HighPriority: {
        int min = sched_get_priority_min(policy);
        int max = sched_get_priority_max(policy);
        param.sched_priority = min + 3 * (max - min) / 4;
        break;
    }
    case XThread_HighestPriority:
        param.sched_priority = sched_get_priority_max(policy);
        break;
    case XThread_TimeCriticalPriority:
        policy = SCHED_FIFO;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        break;
    case XThread_InheritPriority:
        policy = SCHED_OTHER;
        param.sched_priority = 0;
        break;
    default:
        param.sched_priority = 0;
        break;
    }

    pthread_setschedparam(pthread, policy, &param);
}

static void VXThread_deinit(XThread* thread)
{
    if (!thread) return;

    XTask_unregisterThread(thread);

    if (thread->m_handle != 0) {
        if (XThread_isRunning(thread))
            XThread_requestInterruption(thread);
        /* A finished POSIX thread remains joinable until pthread_join(). */
        XThread_wait(thread, UINT32_MAX);
    }

    if (thread->m_loop) {
        XEventLoop_deleteLater(thread->m_loop);
        thread->m_loop = NULL;
    }

    thread->m_handle = 0;
    thread->m_finished = false;
    thread->m_interruptionRequested = false;
    XThreadData* data = thread->m_data;
    if (thread->m_varList) {
        XVarList_delete(thread->m_varList);
        thread->m_varList = NULL;
    }
    XClass_Deinit_Parent(XObject, thread);
    if(data)
    {
        XThreadData_delete(data);
        thread->m_data = NULL;
    }
}

bool XThread_isRunning(const XThread* thread)
{
    return thread && thread->m_handle != 0 && !thread->m_finished;
}

bool XThread_isFinished(const XThread* thread)
{
    return thread && thread->m_finished;
}

XThread_Priority XThread_priority(const XThread* thread)
{
    return thread ? thread->m_priority : XThread_NormalPriority;
}

void XThread_setStackSize(XThread* thread, uint32_t stackSize)
{
    if (thread) thread->m_stackSize = stackSize;
}

uint32_t XThread_stackSize(const XThread* thread)
{
    return thread ? thread->m_stackSize : 0;
}

#endif /* XTHREAD_ON */
#if XTHREADDATA_ON
XHandle XThread_currentThreadId(void)
{
    return (XHandle)pthread_self();
}

void XThread_msleep(uint32_t msecs)
{
    usleep(msecs * 1000);
}

void XThread_sleep(uint32_t secs)
{
    sleep(secs);
}

void XThread_usleep(uint32_t usecs)
{
    usleep(usecs);
}

void XThread_yieldCurrentThread(void)
{
    sched_yield();
}

int XThread_idealThreadCount(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

/* ===================== TLS: XThreadData 专用每线程指针存储 =====================
 * 对标 Qt 6.8: thread_local currentThreadData + pthreadTlsKey (带析构函数)
 * - XThreadStorage_get(): O(1) 无锁读取 (对标 get_thread_data / thread_local)
 * - XThreadStorage_set(): 设置 TLS 值 (对标 set_thread_data)
 * - 析构函数: 线程退出时自动清理 adopted 线程的 XThreadData (对标 destroy_current_thread_data)
 */
static pthread_key_t g_tlsKey;
static pthread_once_t g_tlsOnce = PTHREAD_ONCE_INIT;

/* Qt 6.8: pthread_key 析构函数 (对标 destroy_current_thread_data)
 * 直接将 XThreadData_destroyTlsData 传给 pthread_key_create,无需中间包装层。
 * 线程退出时由 pthreads 自动调用,清理未显式 clearCurrentThreadData() 的 adopted 线程数据。
 * XThread 管理的线程在 VXThread_run 末尾已 set TLS(NULL),不会触发此回调。 */
static void XThreadStorage_createKey(void)
{
    pthread_key_create(&g_tlsKey, XThreadData_destroyTlsData);
}

void XThreadStorage_set(void* p)
{
    pthread_once(&g_tlsOnce, XThreadStorage_createKey);
    pthread_setspecific(g_tlsKey, p);
}

void* XThreadStorage_get(void)
{
    pthread_once(&g_tlsOnce, XThreadStorage_createKey);
    return pthread_getspecific(g_tlsKey);
}

#endif /* XTHREADDATA_ON */
#endif /* XSYNC_ON */
#endif /* POSIX */
