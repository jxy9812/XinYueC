#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
// 在包含任何头文件之前定义 POSIX 特性宏
#define _POSIX_C_SOURCE 200809L  // 或更高版本，确保包含 CLOCK_MONOTONIC 定义
#define SCHED_IDLE SCHED_OTHER
#if defined(__APPLE__) || defined(__BSD__)
#define _BSD_SOURCE  // 部分 BSD 系统（如 macOS）需要此宏
#endif
#include "XThread.h"
#include "XEvent.h"
#include "XHashSet.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEventLoop.h"
#include "XEventDispatcher.h"
#include "CXinYueConfig.h"
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
void XThread_mapInsert(XThread* Object);
void XThread_mapRemove(XThread* Object);
// 前向声明虚函数
static bool VXThread_start(XThread* Object);
static bool VXThread_wait(XThread* Object, unsigned long time);
static bool VXThread_isFinished(const XThread* Object);
static bool VXThread_isRunning(const XThread* Object);
static int VXThread_loopLevel(const XThread* Object);
static XThread_Priority VXThread_priority(const XThread* Object);
static void VXThread_requestInterruption(XThread* Object);
static void VXThread_setPriority(XThread* Object, XThread_Priority priority);
static void VXThread_setStackSize(XThread* Object, uint32_t stackSize);
static void VXThread_deinit(XThread* Object);
static bool VXThread_terminate(XThread* Object);
// 虚函数表初始化
XVtable* XThread_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XThread))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = {
        VXThread_start, VXThread_wait,
        VXThread_isFinished,
        VXThread_isRunning, VXThread_loopLevel, VXThread_priority,VXThread_terminate,
        VXThread_requestInterruption,
        VXThread_setPriority, VXThread_setStackSize
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);

#if SHOWCONTAINERSIZE
    printf("XThread(Posix) size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 线程函数包装器
static void* ThreadFunction(void* arg) 
{
    XThread* Object = (XThread*)arg;
    XThread_mapInsert(Object);
    // 执行用户线程函数
    if (Object->m_start_routine) {
        Object->m_start_routine(Object->m_varList);
    }
    // 运行事件循环
    if (Object->loopLevel > 0 && Object->m_eventLoop) {
        XEventLoop_exec(Object->m_eventLoop);
    }
    // 处理事件调度器逻辑
    // if (Object->m_eventLoop && Object->m_eventLoop->m_dispatcher) {
    //     XEventDispatcher* dispatcher = Object->m_eventLoop->m_dispatcher;
    //     if (dispatcher->m_Objects && !XSetBase_isEmpty_base(dispatcher->m_Objects)) {
    //         while (!Object->m_interruptionRequested) {
    //             XEventDispatcherThread_handler_base(dispatcher);
    //         }
    //     }
    // }
    // 标记线程结束
    Object->m_finished = true;
    XThread_mapRemove(Object);
    return NULL;
}

// 启动线程
static bool VXThread_start(XThread* Object) {
    if (Object == NULL || Object->m_handle != 0) {
        DEBUG_PRINTF("Thread already started or invalid object");
        return false;
    }

    pthread_t thread;
    pthread_attr_t attr;
    int ret = pthread_attr_init(&attr);
    if (ret != 0) {
        DEBUG_PRINTF("pthread_attr_init failed: %d", ret);
        return false;
    }

    // 设置栈大小
    if (Object->m_stackSize > 0) {
        ret = pthread_attr_setstacksize(&attr, Object->m_stackSize);
        if (ret != 0) {
            DEBUG_PRINTF("pthread_attr_setstacksize failed: %d", ret);
            pthread_attr_destroy(&attr);
            return false;
        }
    }

    // 创建线程
    ret = pthread_create(&thread, &attr, ThreadFunction, Object);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        DEBUG_PRINTF("pthread_create failed: %d", ret);
        return false;
    }

    Object->m_handle = (XHandle)thread;
    XThread_setPriority(Object, XThread_priority(Object));
    return true;
}

// 等待线程结束（支持超时）
static bool VXThread_wait(XThread* Object, unsigned long time) {
    if (Object == NULL || Object->m_handle == 0) {
        DEBUG_PRINTF("Invalid thread object or handle");
        return false;
    }

    pthread_t thread = (pthread_t)Object->m_handle;
    if (time == UINT32_MAX) {
        // 无限等待
        int ret = pthread_join(thread, NULL);
        if (ret != 0) {
            DEBUG_PRINTF("pthread_join failed: %d", ret);
            return false;
        }
        return true;
    }
    else {
        // 超时等待模拟
        const struct timespec start = { 0 };
        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            DEBUG_PRINTF("clock_gettime failed: %d", errno);
            return false;
        }
        const unsigned long timeout_ns = time * 1000000;

        while (!Object->m_finished) {
            struct timespec now = { 0 };
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                DEBUG_PRINTF("clock_gettime failed: %d", errno);
                return false;
            }
            unsigned long elapsed = (now.tv_sec - start.tv_sec) * 1000000000 +
                (now.tv_nsec - start.tv_nsec);
            if (elapsed >= timeout_ns) {
                DEBUG_PRINTF("Thread wait timed out");
                return false;
            }
            usleep(1000); // 1ms轮询
        }
        pthread_join(thread, NULL);
        return true;
    }
}

// 检查线程是否已结束
static bool VXThread_isFinished(const XThread* Object) {
    if (Object == NULL || Object->m_handle == 0) {
        return false;
    }
    return Object->m_finished;
}

// 检查线程是否正在运行
static bool VXThread_isRunning(const XThread* Object) {
    if (Object == NULL || Object->m_handle == 0) {
        return false;
    }
    return !Object->m_finished;
}

// 获取循环级别
static int VXThread_loopLevel(const XThread* Object) {
    return Object ? Object->loopLevel : 0;
}

// 获取优先级
static XThread_Priority VXThread_priority(const XThread* Object) {
    return Object ? Object->m_priority : XThread_NormalPriority;
}
static bool VXThread_terminate(XThread* Object) {
    if (Object == NULL || Object->m_handle == 0) {
        DEBUG_PRINTF("Invalid thread object or handle (NULL or uninitialized)");
        return false;
    }
    pthread_t thread = (pthread_t)Object->m_handle;
    // 尝试取消线程
    int ret = pthread_cancel(thread);
    if (ret != 0) {
        DEBUG_PRINTF("pthread_cancel failed with error: % d", ret);
        return false;
    }
    // 等待线程终止并回收资源
    void* thread_result;
    ret = pthread_join(thread, &thread_result);
    if (ret != 0) {
        DEBUG_PRINTF("pthread_join failed with error: % d", ret);
        return false;
    }
    // 验证线程终止状态
    if (thread_result != PTHREAD_CANCELED) {
        DEBUG_PRINTF("Thread did not terminate via cancellation");
    }
    // 更新线程状态
    Object->m_finished = true;
    Object->m_handle = 0; // 重置句柄，避免重复操作
    Object->m_interruptionRequested = false;
    return true;
}

// 请求中断线程
static void VXThread_requestInterruption(XThread* Object) {
    if (Object) {
        Object->m_interruptionRequested = true;
        // 唤醒事件循环
        if (Object->m_eventLoop) {
            XEventLoop_wakeUp(Object->m_eventLoop);
        }
    }
}

// 设置线程优先级
static void VXThread_setPriority(XThread* Object, XThread_Priority priority) {
    if (Object == NULL) {
        return;
    }

    Object->m_priority = priority;
    if (Object->m_handle == 0) {
        return;
    }

    int policy;
    struct sched_param param = { 0 };
    pthread_t thread = (pthread_t)Object->m_handle;

    if (pthread_getschedparam(thread, &policy, &param) != 0) {
        DEBUG_PRINTF("pthread_getschedparam failed: %d", errno);
        return;
    }

    // 优先级映射
    switch (priority) {
    case XThread_IdlePriority:
        policy = SCHED_IDLE;
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

    if (pthread_setschedparam(thread, policy, &param) != 0) {
        DEBUG_PRINTF("Failed to set thread priority: %d", errno);
    }
}

// 设置栈大小
static void VXThread_setStackSize(XThread* Object, uint32_t stackSize) {
    if (Object) {
        Object->m_stackSize = stackSize;
    }
}

// 销毁线程对象
static void VXThread_deinit(XThread* Object) {
    if (Object == NULL) return;

    if (XThread_isRunning(Object)) {
        XThread_requestInterruption(Object);
        XThread_wait(Object, UINT32_MAX);
    }

    if (Object->m_eventLoop) {
        XEventLoop_deleteLater(Object->m_eventLoop);
        Object->m_eventLoop = NULL;
    }

    XThread_mapRemove(Object);
    Object->m_handle = 0;
    Object->m_finished = false;
    Object->m_interruptionRequested = false;
}

// 获取当前线程ID
XHandle XThread_currentThreadId() {
    return (XHandle)pthread_self();
}
// 创建 XThread 对象
XThread* XThread_create_func(void (*start_routine)(void*), void* arg)
{
    XThread* Object = (XThread*)XMemory_malloc(sizeof(XThread));
    if (Object == NULL) {
        return NULL;
    }
    XThread_init(Object);
    SET_CLASS_HEAP(Object);
    Object->m_start_routine = start_routine;
    Object->m_varList = arg;

    XThread_currentThread();//初始化

    return Object;
}
#endif