#include"XThreadPool.h"
#include"XMutex.h"
#include"XWaitCondition.h"
#include"XThread.h"
#include"XPriorityQueue.h"
#include"XCircularQueue.h"  
#include"XCoreApplication.h"
#include"XDateTime.h"
static  void XThreadPool_worker_thread(XThread* thread, XThreadPool* pool);
// 在文件顶部添加（在结构体定义之后）
typedef struct {
    XRunnable* runnable;
    int priority;  // 优先级值，数值越小优先级越高（与 Qt 一致）
} PrioritizedTask;
// 优先级比较函数：数值越小优先级越高
static int prioritizedTask_compare(const void* a, const void* b) {
    const PrioritizedTask* taskA = (const PrioritizedTask*)a;
    const PrioritizedTask* taskB = (const PrioritizedTask*)b;

    if (taskA->priority < taskB->priority) return XCompare_Less;    // A 优先级更高
    if (taskA->priority > taskB->priority) return XCompare_Greater; // B 优先级更高
    return XCompare_Equality; // 优先级相同
}
/**
 * @brief XThreadPool类结构体定义，用于管理线程池
 * @note 继承自XObject，提供线程池功能，管理一组工作线程
 */
typedef struct XThreadPool
{
    XObject m_object;                    ///< 继承的基类成员
   
    XPriorityQueue m_waitQueue;                //线程任务等待队列
    XCircularQueue m_reservedQueue;
    XVector m_threadGroup;              //当前线程组包含 XThread
    XMutex* m_mutex;                    //互斥锁
    XWaitCondition* m_waitCond;         // 条件变量：用于线程休眠/唤醒
    XWaitCondition* m_doneCond;         // 新增：条件变量：用于任务完成通知
    // 线程池核心属性
    uint16_t max_thread_count;               ///< 最大线程数
    XAtomic_uint32_t active_thread_count;    ///< 当前活跃线程数 原子操作
    XAtomic_uint32_t idle_thread_count;      ///< 当前空闲线程数
    XAtomic_uint32_t reserved_thread_count;  // 预留线程数量（始终保持的最小线程数）
    uint32_t expiry_timeout;                 ///< 线程过期超时时间（毫秒）
    uint32_t stack_size;                     ///< 工作线程栈大小
   
} XThreadPool;
// 全局线程池实例
static XThreadPool* g_globalThreadPool = NULL;
// 虚函数声明
static void VXThreadPool_deinit(XThreadPool* pool);
XVtable* XThreadPool_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XThreadPool))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_XCLASS(XObject);
	//void* table[] = {
	//	VXEvent_default_setAccepted,VXEvent_default_clone
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThreadPool_deinit);
#if SHOWCONTAINERSIZE
	printf("XThreadPool size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XThreadPool_init(XThreadPool* pool, XObject* parent)
{
    XAssert(pool, "pool is NULL");

    // 初始化基类
    XObject_init(pool);
    XClassSetVtable(pool, XThreadPool);
    Set_Class_MemoryFree(pool, NULL);
    XObject_setParent(pool, parent);
    // 初始化成员变量
    XPriorityQueue_init(&pool->m_waitQueue, sizeof(PrioritizedTask), prioritizedTask_compare, XSORT_ASC);
    XVector_init(&pool->m_threadGroup, sizeof(XThread*));
    XContainerSetCompare(&pool->m_waitQueue, uintptr_t_compare);
    XContainerSetCompare(&pool->m_threadGroup, uintptr_t_compare);
    XCircularQueue_init(&pool->m_reservedQueue, sizeof(XRunnable*), 64);
    pool->m_mutex = XMutex_create(XLock_NonRecursive);
    pool->m_waitCond = XWaitCondition_create();
    pool->m_doneCond = XWaitCondition_create();  

    pool->max_thread_count = 0; // 0表示使用理想线程数
    XAtomic_init(pool->active_thread_count, 0);
    XAtomic_init(pool->idle_thread_count, 0);
    XAtomic_init(pool->reserved_thread_count, 0);
    pool->expiry_timeout = 30000; // 默认30秒
    pool->stack_size = 0; // 使用系统默认栈大小
}
XThreadPool* XThreadPool_create(XObject* parent)
{
    XThreadPool* pool = (XThreadPool*)XMalloc_System(sizeof(XThreadPool));
    if (!pool)
        return NULL;

    XThreadPool_init(pool, parent);
    Set_Class_MemoryFree(pool, XFree_System);
    return pool;
}
XThreadPool* XThreadPool_globalInstance()
{
    XThreadPool* instance = g_globalThreadPool;
    if (!instance) {
        // 尝试创建新实例
        XThreadPool* new_instance = XThreadPool_create(NULL);
        if (!new_instance) {
            return NULL; // 创建失败
        }
        Set_Class_MemoryFree(new_instance, NULL);

        // 原子地设置全局实例
        if (!XAtomic_compare_exchange_strong_uintptr_t((void**)&g_globalThreadPool,
            (void**)&instance, new_instance,
            XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            // 其他线程已经设置了实例，删除我们创建的实例
            XThreadPool_deleteLater(new_instance);
            return instance;
        }
        instance = new_instance;
    }
    return instance;
}
// 内部辅助函数：获取最大线程数
static uint16_t XThreadPool_getMaxThreadCount(const XThreadPool* pool)
{
    if (pool->max_thread_count == 0)
        return (uint16_t)XThread_idealThreadCount();
    return pool->max_thread_count;
}
 void XThreadPool_worker_thread(XThread* thread, XThreadPool* pool)
{
    //XVarList_args_1(varList,XThreadPool*,pool);
    PrioritizedTask task;
    XRunnable* runnable = NULL;
    //while (!XThread_isInterruptionRequested(thread))
    //{
    //    // 关键修改：优先检查预留任务队列（无锁操作，但需要在锁内保证一致性）
    //    XMutex_lock(pool->m_mutex);
    //    if (XQueueBase_receive_base(&pool->m_reservedQueue, &runnable))
    //    {
    //        XMutex_unlock(pool->m_mutex);
    //        // 执行预留任务
    //        XRunnable_run_base(runnable);
    //        if (XRunnable_autoDelete(runnable)) {
    //            XRunnable_delete_base(runnable);
    //        }
    //        continue;
    //    }
  
    //    if (XQueueBase_receive_base(&pool->m_waitQueue, &task))
    //    {
    //        XMutex_unlock(pool->m_mutex);
    //        runnable = task.runnable;
    //        XRunnable_run_base(runnable);
    //        if (XRunnable_autoDelete(runnable))
    //        {
    //            XRunnable_delete_base(runnable);
    //        }
    //        continue;
    //    }
    //    // 3. 此时线程真正空闲，增加空闲计数并进入等待
    //    XAtomic_fetch_add_uint32(&pool->idle_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
    //    // 检查是否所有任务都完成了
    //    if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
    //        XContainer_isEmpty_base(&pool->m_reservedQueue) &&
    //        XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed) ==
    //        XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed)) {
    //        XWaitCondition_wakeAll(pool->m_doneCond);
    //        // 触发任务空信号
    //        XThreadPool_tasksEmpty_signal(pool);
    //    }
    //    // 获取当前预留线程数和活跃线程数
    //    uint32_t reserved = XAtomic_load_uint32(&pool->reserved_thread_count, XAtomic_MemoryOrder_Relaxed);
    //    uint32_t active = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
    //   
    //    if (active > reserved) 
    //    {
    //        // 当前活跃线程数超过预留数量，可以考虑退出
    //        //XPrintf("XThread:%p 休眠 %p %p\n",thread, pool->m_waitCond, pool->m_mutex);
    //        bool should_exit = XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, (int32_t)pool->expiry_timeout);
    //        //XPrintf("XThread:%p 唤醒 %p %p\n", thread, pool->m_waitCond, pool->m_mutex);
    //        if (!should_exit&&
    //            XContainer_isEmpty_base(&pool->m_waitQueue) &&
    //            XContainer_isEmpty_base(&pool->m_reservedQueue)) 
    //        {
    //            break;
    //        }
    //        //XMutex_unlock(pool->m_mutex);
    //    }
    //    else {
    //        // 活跃线程数 <= 预留数量，必须保持活跃，无限等待
    //        //XPrintf("XThread:%p 下休眠\n", thread);
    //        XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, -1);
    //        //XPrintf("XThread:%p 下唤醒\n", thread);
    //        // 被唤醒后，减少空闲计数
    //        XAtomic_fetch_sub_uint32(&pool->idle_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
    //        XMutex_unlock(pool->m_mutex);
    //        continue;
    //    }
    //    // 被唤醒后，减少空闲计数
    //    XAtomic_fetch_sub_uint32(&pool->idle_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
    //    //XMutex_unlock(pool->m_mutex);
    //}
    //
   
    //// 触发线程删除信号（在从线程组移除之前）
    //XThreadPool_threadDeleted_signal(pool, thread);
    ////XMutex_lock(pool->m_mutex);
    //XVector_remove_base(&pool->m_threadGroup, XVector_indexOf(&pool->m_threadGroup, &thread, 0), 1);
    //thread->m_varList = NULL;
    //XMutex_unlock(pool->m_mutex);
    //XThread_deleteLater(thread);
    //// 退出前减少活动线程计数
    //XAtomic_fetch_sub_uint32(&pool->active_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
    //return;

    while (!XThread_isInterruptionRequested(thread))
    {
        // 尝试获取任务（先预留队列，再普通队列）
        bool has_task = false;
        XMutex_lock(pool->m_mutex);

        // 优先检查预留任务队列
        if (XQueueBase_receive_base(&pool->m_reservedQueue, &runnable)) {
            has_task = true;
        }
        // 再检查普通等待队列
        else if (XQueueBase_receive_base(&pool->m_waitQueue, &task)) {
            runnable = task.runnable;
            has_task = true;
        }

        if (has_task) {
            XMutex_unlock(pool->m_mutex);
            // 执行任务
            XRunnable_run_base(runnable);
            if (XRunnable_autoDelete(runnable)) {
                XRunnable_delete_base(runnable);
            }
            continue;
        }

        // 没有任务，进入空闲等待状态
        uint32_t reserved = XAtomic_load_uint32(&pool->reserved_thread_count, XAtomic_MemoryOrder_Relaxed);
        uint32_t active = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);

        // 标记为空闲线程
        XAtomic_fetch_add_uint32(&pool->idle_thread_count, 1, XAtomic_MemoryOrder_Relaxed);

        // 检查是否所有任务都完成
        if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
            XContainer_isEmpty_base(&pool->m_reservedQueue) &&
            active == XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed)) {
            XWaitCondition_wakeAll(pool->m_doneCond);
            XThreadPool_tasksEmpty_signal(pool);
        }

        bool should_exit = false;
        bool timed_out = false;

        if (active > reserved) {
            // 非预留线程：可以超时退出
            timed_out = !XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, (int32_t)pool->expiry_timeout);
            should_exit = timed_out &&
                XContainer_isEmpty_base(&pool->m_waitQueue) &&
                XContainer_isEmpty_base(&pool->m_reservedQueue);
        }
        else {
            // 预留线程：无限等待
            XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, -1);
        }

        // 被唤醒或超时，标记为非空闲
        XAtomic_fetch_sub_uint32(&pool->idle_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
        XMutex_unlock(pool->m_mutex);

        // 如果是非预留线程且超时退出
        if (should_exit) {
            break;
        }
    }
    // 线程退出清理
    XMutex_lock(pool->m_mutex);
    XThreadPool_threadDeleted_signal(pool, thread);
    XVector_remove_base(&pool->m_threadGroup, XVector_indexOf(&pool->m_threadGroup, &thread, 0), 1);
    thread->m_varList = NULL;
    XMutex_unlock(pool->m_mutex);

    XThread_deleteLater(thread);
    XAtomic_fetch_sub_uint32(&pool->active_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
}
// 内部辅助函数：启动新工作线程
static bool XThreadPool_startWorkerThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    // 获取当前活跃线程数（线程池内部的工作线程）
    uint32_t active = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);

    // 获取预留线程数（被外部使用的线程槽位）
    uint32_t reserved = XAtomic_load_uint32(&pool->reserved_thread_count, XAtomic_MemoryOrder_Relaxed);

    // 获取最大线程数限制
    uint16_t max_threads = XThreadPool_getMaxThreadCount(pool);

    // 计算线程池实际可用的最大线程数
    // 可用线程数 = 最大线程数 - 预留线程数
    if (reserved >= max_threads) {
        // 所有线程槽位都被预留了，线程池无法创建任何工作线程
        return false;
    }

    uint16_t available_max_threads = max_threads - (uint16_t)reserved;

    // 检查当前活跃线程数是否已经达到可用上限
    if (active >= available_max_threads) {
        return false;
    }

   /* XVarList* varList = XVarList_Create(XVar(XThreadPool*, pool));
    if (varList==NULL)
        return false;*/
    XThread* thread = XThread_create_func((XThreadFunc)XThreadPool_worker_thread, pool);
  /*  XVarList_args_1(varList, XThreadPool*, tp);

    if (!thread)
    {
        XVarList_delete(varList);
        return false;
    }*/

    // 设置线程属性
    if (pool->stack_size > 0)
        XThread_setStackSize(thread, pool->stack_size);

    // 启动线程
    if (!XThread_start(thread))
    {
        XThread_deleteLater(thread);
        return false;
    }
    // 将线程添加到线程组
    if (!XVector_push_back_base(&pool->m_threadGroup, &thread))
    {
        XThread_deleteLater(thread);
        return false;
    }
    // 触发线程创建信号
    XThreadPool_threadCreated_signal(pool, thread);
    // 增加活跃线程计数
    XAtomic_fetch_add_uint32(&pool->active_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}
// XThreadPool_start1 - 提交任务到线程池
void XThreadPool_start1(XThreadPool* pool, XRunnable* runnable, int priority)
{
    XAssert(pool && runnable, "pool or runnable is NULL");

    XMutex_lock(pool->m_mutex);

    // 创建带优先级的任务并添加到优先级队列
    PrioritizedTask task = { runnable, priority };
    XPriorityQueue_push_base(&pool->m_waitQueue, &task);

    // 尝试启动工作线程（如果当前活跃线程数小于最大线程数）
    uint32_t active_count = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
    uint16_t max_threads = XThreadPool_getMaxThreadCount(pool);

    if (active_count < max_threads) {
        // 解锁后启动线程，避免死锁
        XMutex_unlock(pool->m_mutex);
        XThreadPool_startWorkerThread(pool);
        return;
    }
    XMutex_unlock(pool->m_mutex);
    // 唤醒一个等待的工作线程
    XWaitCondition_wakeOne(pool->m_waitCond);

}
void XThreadPool_start2(XThreadPool* pool, XCallableToRun function, XVarList* argsList, int priority)
{
    XThreadPool_start1(pool,XRunnable_create_from_function(function,argsList,true),priority);
}
void XThreadPool_startOnReservedThread1(XThreadPool* pool, XRunnable* runnable)
{
    XAssert(pool && runnable, "pool or runnable is NULL");

    XMutex_lock(pool->m_mutex);

    uint32_t reserved = XAtomic_load_uint32(&pool->reserved_thread_count, XAtomic_MemoryOrder_Acquire);
    if (reserved == 0) {
        XMutex_unlock(pool->m_mutex);
        return;
    }

    XAtomic_fetch_sub_uint32(&pool->reserved_thread_count, 1, XAtomic_MemoryOrder_Release);

    if (!XQueueBase_push_base(&pool->m_reservedQueue, &runnable)) {
        // 队列满，恢复预留计数
        XAtomic_fetch_add_uint32(&pool->reserved_thread_count, 1, XAtomic_MemoryOrder_Release);
        XMutex_unlock(pool->m_mutex);
        return;
    }

    uint32_t active_count = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Acquire);
    uint16_t max_threads = XThreadPool_getMaxThreadCount(pool);

    if (active_count < max_threads) {
        XMutex_unlock(pool->m_mutex);
        XThreadPool_startWorkerThread(pool);
    }
    else {
        XWaitCondition_wakeOne(pool->m_waitCond);
        XMutex_unlock(pool->m_mutex);
    }
}
void XThreadPool_startOnReservedThread2(XThreadPool* pool, XCallableToRun function, XVarList* argsList)
{
    XThreadPool_startOnReservedThread1(pool, XRunnable_create_from_function(function, argsList, true));
}
// XThreadPool_tryStart1 - 尝试启动任务
bool XThreadPool_tryStart1(XThreadPool* pool, XRunnable* runnable)
{
    XAssert(pool && runnable, "pool or runnable is NULL");
    XMutex_lock(pool->m_mutex);

    // 检查是否有真正的空闲线程
    uint32_t idle_count = XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed);
    uint32_t active_count = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
    uint16_t max_threads = XThreadPool_getMaxThreadCount(pool);

    if (idle_count > 0 && active_count <= max_threads) {
        // 有真正空闲的线程，将任务放入预留队列
        if (XQueueBase_push_base(&pool->m_reservedQueue, &runnable)) {
            XWaitCondition_wakeOne(pool->m_waitCond);
            XMutex_unlock(pool->m_mutex);
            return true;
        }
    }

    XMutex_unlock(pool->m_mutex);
    return false;
}
bool XThreadPool_tryStart2(XThreadPool* pool, XCallableToRun function, XVarList* argsList)
{
    return XThreadPool_tryStart1(pool, XRunnable_create_from_function(function, argsList, true));
}
// XThreadPool_expiryTimeout - 获取过期超时时间
int XThreadPool_expiryTimeout(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return (int)pool->expiry_timeout;
}

// XThreadPool_setExpiryTimeout - 设置过期超时时间
void XThreadPool_setExpiryTimeout(XThreadPool* pool, int expiryTimeout)
{
    XAssert(pool, "pool is NULL");
    pool->expiry_timeout = (uint32_t)expiryTimeout;
}

// XThreadPool_maxThreadCount - 获取最大线程数
int XThreadPool_maxThreadCount(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return (int)XThreadPool_getMaxThreadCount(pool);
}

// XThreadPool_setMaxThreadCount - 设置最大线程数
void XThreadPool_setMaxThreadCount(XThreadPool* pool, int maxThreadCount)
{
    XAssert(pool, "pool is NULL");
    pool->max_thread_count = (uint16_t)(maxThreadCount > 0 ? maxThreadCount : 0);
}

// XThreadPool_activeThreadCount - 获取当前活跃线程数
int XThreadPool_activeThreadCount(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return (int)XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
}

// XThreadPool_setStackSize - 设置工作线程栈大小
void XThreadPool_setStackSize(XThreadPool* pool, uint32_t stackSize)
{
    XAssert(pool, "pool is NULL");
    pool->stack_size = stackSize;
}

// XThreadPool_stackSize - 获取工作线程栈大小
uint32_t XThreadPool_stackSize(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return pool->stack_size;
}
// XThreadPool_reserveThread - 预留线程
void XThreadPool_reserveThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    XAtomic_fetch_add_uint32(&pool->reserved_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
}

// XThreadPool_releaseThread - 释放预留的线程
void XThreadPool_releaseThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    XAtomic_fetch_sub_uint32(&pool->reserved_thread_count, 1, XAtomic_MemoryOrder_Relaxed);
}
// XThreadPool_waitForDone - 等待所有任务完成
bool XThreadPool_waitForDone(XThreadPool* pool, int msecs)
{
    XAssert(pool, "pool is NULL");

    XMutex_lock(pool->m_mutex);

    // ✅ 完整的任务完成条件：
    // 1. 等待队列为空
    // 2. 预留队列为空  
    // 3. 没有线程正在执行任务（活跃线程数 == 空闲线程数）
    uint32_t active = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
    uint32_t idle = XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed);

    if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
        XContainer_isEmpty_base(&pool->m_reservedQueue) &&
        active == idle) {  // 所有活跃线程都处于空闲状态
        XMutex_unlock(pool->m_mutex);
        return true;
    }

    bool result;
    if (msecs < 0) {
        // 无限等待
        while (!XContainer_isEmpty_base(&pool->m_waitQueue) ||
            !XContainer_isEmpty_base(&pool->m_reservedQueue) ||
            XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed) !=
            XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed)) {
            XWaitCondition_wait(pool->m_doneCond, pool->m_mutex, -1);
        }
        result = true;
    }
    else {
        // 有限等待
        uint64_t start_time = XDateTime_currentMSecsSinceEpoch();
        uint64_t end_time = start_time + (uint64_t)msecs;

        while (!XContainer_isEmpty_base(&pool->m_waitQueue) ||
            !XContainer_isEmpty_base(&pool->m_reservedQueue) ||
            XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed) !=
            XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed)) {
            uint64_t current_time = XDateTime_currentMSecsSinceEpoch();
            if (current_time >= end_time) {
                result = false;
                break;
            }
            int32_t remaining_ms = (int32_t)(end_time - current_time);
            if (!XWaitCondition_wait(pool->m_doneCond, pool->m_mutex, remaining_ms)) {
                result = false;
                break;
            }
        }
        // 最终检查
        uint32_t final_active = XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed);
        uint32_t final_idle = XAtomic_load_uint32(&pool->idle_thread_count, XAtomic_MemoryOrder_Relaxed);
        result = (XContainer_isEmpty_base(&pool->m_waitQueue) &&
            XContainer_isEmpty_base(&pool->m_reservedQueue) &&
            final_active == final_idle);
    }

    XMutex_unlock(pool->m_mutex);
    return result;
}
// XThreadPool_clear - 清空待执行任务
void XThreadPool_clear(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");

    XMutex_lock(pool->m_mutex);

    // 清空等待队列中的所有任务
    while (!XContainer_isEmpty_base(&pool->m_waitQueue)) {
        PrioritizedTask task;
        if (XQueueBase_receive_base(&pool->m_waitQueue, &task)) {
            // 如果任务设置了 autoDelete，需要手动删除
            if (XRunnable_autoDelete(task.runnable)) {
                XRunnable_delete_base(task.runnable);
            }
        }
    }
    // 清空预留队列
    while (!XContainer_isEmpty_base(&pool->m_reservedQueue)) {
        XRunnable* runnable;
        if (XQueueBase_receive_base(&pool->m_reservedQueue, &runnable)) {
            if (XRunnable_autoDelete(runnable)) {
                XRunnable_delete_base(runnable);
            }
        }
    }
    // 如果队列变空了，唤醒等待完成的线程
    if (XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed) == 0) {
        XWaitCondition_wakeAll(pool->m_doneCond);
    }
    XMutex_unlock(pool->m_mutex);
}

// XThreadPool_tryTake - 尝试移除指定任务
bool XThreadPool_tryTake(XThreadPool* pool, XRunnable* runnable)
{
    XAssert(pool, "pool is NULL");
    XAssert(runnable, "runnable is NULL");

    XMutex_lock(pool->m_mutex);

    // 创建要查找的任务模板（优先级不重要，因为我们只比较 runnable）
    PrioritizedTask search_task = { runnable, 0 };

    // 使用新的 remove 函数移除任务
    size_t removed = XPriorityQueue_remove(&pool->m_waitQueue, &search_task, 1);

    if (removed > 0) {
        // 如果任务设置了 autoDelete，需要手动删除
        if (XRunnable_autoDelete(runnable)) {
            XRunnable_delete_base(runnable);
        }

        // 如果队列变空且没有活跃线程，唤醒等待完成的线程
        if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
            XAtomic_load_uint32(&pool->active_thread_count, XAtomic_MemoryOrder_Relaxed) == 0) {
            XWaitCondition_wakeAll(pool->m_doneCond);
        }

        XMutex_unlock(pool->m_mutex);
        return true;
    }

    XMutex_unlock(pool->m_mutex);
    return false;
}
void* XThreadPool_threadCreated_signal(XThreadPool* pool, XThread* thread)
{
    XEmitSignal(pool, XThreadPool_threadCreated_signal, XVarList_Create(XVar(XThread*, thread)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XThreadPool_threadDeleted_signal(XThreadPool* pool, XThread* thread)
{
    XEmitSignal(pool, XThreadPool_threadDeleted_signal, XVarList_Create(XVar(XThread*, thread)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XThreadPool_tasksEmpty_signal(XThreadPool* pool)
{
    XEmitSignal(pool, XThreadPool_tasksEmpty_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
void VXThreadPool_deinit(XThreadPool* pool)
{
    // 等待所有任务完成
    //XThreadPool_waitForDone(pool, -1);
    XMutex_lock(pool->m_mutex);
    for_each_iterator(&pool->m_threadGroup, XVector, it)
    {
        XThread** lpTh = (XThread*)XVector_iterator_data(&it);
        if (lpTh&& *lpTh)
        {
            XThread_requestInterruption(*lpTh);
            //XThread_deleteLater(*lpTh);
        }
    }
    XThreadPool_clear(pool);
    XMutex_unlock(pool->m_mutex);

    //等待线程退出
    while(XThreadPool_activeThreadCount(pool))
    {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XWaitCondition_wakeAll(pool->m_waitCond);
    }
    //XThread_msleep(100);
    XCoreApplication_sendPostedEvents(NULL,XEVENT_TYPE_DEFERRED_DELETE);
    //
    // 清理资源
    XClass_deinit_base(&pool->m_waitQueue);
    XClass_deinit_base(&pool->m_reservedQueue);
    XVector_deinit_base(&pool->m_threadGroup);

    if (pool->m_mutex) {
        XMutex_delete(pool->m_mutex);
        pool->m_mutex = NULL;
    }

    if (pool->m_waitCond) {
        XWaitCondition_delete(pool->m_waitCond);
        pool->m_waitCond = NULL;
    }

    if (pool->m_doneCond) {  
        XWaitCondition_delete(pool->m_doneCond);
        pool->m_doneCond = NULL;
    }

    // 调用父类析构
    XClass_Deinit_Parent(XObject,pool);
}
