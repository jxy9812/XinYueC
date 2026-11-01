#include"XThreadPool.h"
#include<string.h>
#include"XMutex.h"
#include"XWaitCondition.h"
#include"XThread.h"
#include"XPriorityQueue.h"
#include"XCircularQueue.h"  
#include"XCoreApplication.h"
#include"XDateTime.h"
#if XSYNC_ON
#if XTHREADPOOL_ON
static  void XThreadPool_worker_thread(XThread* thread, XThreadPool* pool);
// 在文件顶部添加（在结构体定义之后）
typedef struct {
    XRunnable* runnable;
    int priority;  // 优先级值，数值越小优先级越高（与 Qt 一致）
} PrioritizedTask;
// 优先级比较函数：数值越小优先级越高
static int XPrioritizedTask_compare(const void* a, const void* b) {
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
    XObject m_class;                     ///< 继承的基类成员
   
    XPriorityQueue m_waitQueue;                //线程任务等待队列
    XCircularQueue m_reservedQueue;
    XVector m_threadGroup;              //当前线程组包含 XThread
    XMutex* m_mutex;                    //互斥锁
    XWaitCondition* m_waitCond;         // 条件变量：用于线程休眠/唤醒
    XWaitCondition* m_doneCond;         // 新增：条件变量：用于任务完成通知
    // 线程池核心属性
    uint16_t m_maxThreadCount;               ///< 最大线程数
    XAtomic_Uint32 m_activeThreadCount;    ///< 当前活跃线程数 原子操作
    XAtomic_Uint32 m_idleThreadCount;      ///< 当前空闲线程数
    XAtomic_Uint32 m_reservedThreadCount;  // 预留线程数量（始终保持的最小线程数）
    uint32_t m_expiryTimeout;                ///< 线程过期超时时间（毫秒）
    uint32_t m_stackSize;                    ///< 工作线程栈大小
   
} XThreadPool;
// 全局线程池实例
static XThreadPool* g_globalThreadPool = NULL;
// 虚函数声明
static void VXThreadPool_deinit(XThreadPool* pool);
// 虚函数实现
void VXThreadPool_deinit(XThreadPool* pool)
{
    if (!pool)
        return;
    // 反注册全局单例：删除后 g_globalThreadPool 不再指向失效对象，
    // 仅当全局指针仍指向本池时才清空（避免覆盖竞态新登记的实例）
    XThreadPool* expected = pool;
    XAtomic_compare_exchange_strong_uintptr_t((void**)&g_globalThreadPool,
        (void**)&expected, NULL,
        XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed);
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
    XClassDeinit(&pool->m_waitQueue);
    XClassDeinit(&pool->m_reservedQueue);
    XClassDeinit(&pool->m_threadGroup);

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
XVtable* XThreadPool_class_init()
{
	XVTABLE_INIT_DEFAULT(XThreadPool)
		//继承类
		XVTABLE_INHERIT_XCLASS(XObject);
	//void* table[] = {
	//	VXEvent_default_setAccepted,VXEvent_default_clone
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThreadPool_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XThreadPool);
	return XVTABLE_DEFAULT;
}
void XThreadPool_init(XThreadPool* pool, XObject* parent)
{
    XAssert(pool, "pool is NULL");

    // 清零内存
    memset(pool, 0, sizeof(XThreadPool));
    // 初始化基类
    XObject_init(pool);
    XClassSetVtable(pool, XThreadPool);
    XObject_setParent(pool, parent);
    // 初始化成员变量
    XPriorityQueue_init(&pool->m_waitQueue, sizeof(PrioritizedTask), XPrioritizedTask_compare, XSORT_ASC);
    XVector_init(&pool->m_threadGroup, sizeof(XThread*),false);
    XContainerSetCompare(&pool->m_waitQueue, Xuintptr_t_compare);
    XContainerSetCompare(&pool->m_threadGroup, Xuintptr_t_compare);
    XCircularQueue_init(&pool->m_reservedQueue, sizeof(XRunnable*), 64);
    pool->m_mutex = XMutex_create(XLock_NonRecursive);
    XAssert(pool->m_mutex, "mutex create failed");
    pool->m_waitCond = XWaitCondition_create();
    XAssert(pool->m_waitCond, "wait condition create failed");
    pool->m_doneCond = XWaitCondition_create();
    XAssert(pool->m_doneCond, "done condition create failed");

    pool->m_maxThreadCount = 0; // 0表示使用理想线程数
    XAtomic_init(pool->m_activeThreadCount, 0);
    XAtomic_init(pool->m_idleThreadCount, 0);
    XAtomic_init(pool->m_reservedThreadCount, 0);
    pool->m_expiryTimeout = 30000; // 默认30秒
    pool->m_stackSize = 0; // 使用系统默认栈大小
}
XThreadPool* XThreadPool_create_ex(XMemoryType memory, XObject* parent)
{
    XThreadPool* pool = (XThreadPool*)XMemory_malloc(sizeof(XThreadPool), memory);
    if (!pool)
        return NULL;

    XThreadPool_init(pool, parent);
    Set_Class_Memory(pool, memory); Set_Class_IsHeap(pool, true);
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
        // 原子地设置全局实例
        if (!XAtomic_compare_exchange_strong_uintptr_t((void**)&g_globalThreadPool,
            (void**)&instance, new_instance,
            XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            // 其他线程已经设置了实例，删除我们创建的实例
            //（保留堆标记，deleteLater 经 XClassDelete 才能释放整个结构体）
            XThreadPool_deleteLater(new_instance);
            return instance;
        }
        // 仅获胜方清零堆标记：全局实例常驻，不随 XClassDelete 释放结构体
        Class_Memory(new_instance) = NULL;
        Class_IsHeap(new_instance) = 0;
        instance = new_instance;
    }
    return instance;
}
// 内部辅助函数：获取最大线程数
static uint16_t XThreadPool_getMaxThreadCount(const XThreadPool* pool)
{
    if (pool->m_maxThreadCount == 0)
        return (uint16_t)XThread_idealThreadCount();
    return pool->m_maxThreadCount;
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
    //            XClassDelete(runnable);
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
    //            XClassDelete(runnable);
    //        }
    //        continue;
    //    }
    //    // 3. 此时线程真正空闲，增加空闲计数并进入等待
    //    XAtomic_fetch_add_uint32(&pool->m_idleThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
    //    // 检查是否所有任务都完成了
    //    if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
    //        XContainer_isEmpty_base(&pool->m_reservedQueue) &&
    //        XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed) ==
    //        XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed)) {
    //        XWaitCondition_wakeAll(pool->m_doneCond);
    //        // 触发任务空信号
    //        XThreadPool_tasksEmpty_signal(pool);
    //    }
    //    // 获取当前预留线程数和活跃线程数
    //    uint32_t reserved = XAtomic_load_uint32(&pool->m_reservedThreadCount, XAtomic_MemoryOrder_Relaxed);
    //    uint32_t active = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
    //   
    //    if (active > reserved) 
    //    {
    //        // 当前活跃线程数超过预留数量，可以考虑退出
    //        //XPrintf("XThread:%p 休眠 %p %p\n",thread, pool->m_waitCond, pool->m_mutex);
    //        bool should_exit = XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, (int32_t)pool->m_expiryTimeout);
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
    //        XAtomic_fetch_sub_uint32(&pool->m_idleThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
    //        XMutex_unlock(pool->m_mutex);
    //        continue;
    //    }
    //    // 被唤醒后，减少空闲计数
    //    XAtomic_fetch_sub_uint32(&pool->m_idleThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
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
    //XAtomic_fetch_sub_uint32(&pool->m_activeThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
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
            if (XRunnable_autoDelete(runnable)) 
            {
                XClassDelete(runnable);
            }
            continue;
        }

        // 没有任务，进入空闲等待状态
        uint32_t reserved = XAtomic_load_uint32(&pool->m_reservedThreadCount, XAtomic_MemoryOrder_Relaxed);
        uint32_t active = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);

        // 标记为空闲线程
        XAtomic_fetch_add_uint32(&pool->m_idleThreadCount, 1, XAtomic_MemoryOrder_Relaxed);

        // 检查是否所有任务都完成
        if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
            XContainer_isEmpty_base(&pool->m_reservedQueue) &&
            active == XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed)) {
            XWaitCondition_wakeAll(pool->m_doneCond);
            XThreadPool_tasksEmpty_signal(pool);
        }

        bool should_exit = false;
        bool timed_out = false;

        if (active > reserved) {
            // 非预留线程：可以超时退出
            timed_out = !XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, (int32_t)pool->m_expiryTimeout);
            should_exit = timed_out &&
                XContainer_isEmpty_base(&pool->m_waitQueue) &&
                XContainer_isEmpty_base(&pool->m_reservedQueue);
        }
        else {
            // 预留线程：无限等待
            XWaitCondition_wait(pool->m_waitCond, pool->m_mutex, -1);
        }

        // 被唤醒或超时，标记为非空闲
        XAtomic_fetch_sub_uint32(&pool->m_idleThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
        XMutex_unlock(pool->m_mutex);

        // 如果是非预留线程且超时退出
        if (should_exit) {
            break;
        }
    }
    // 线程退出清理
    // 入组失败的孤儿线程不在 m_threadGroup 中：indexOf 返回 -1（XVector_indexOf 未找到
    // 返回 -1），跳过组移除；活跃计数递减保持无条件——startWorkerThread 在 XThread_start
    // 前已配对递增，无论是否成功入组都必须在此回退
    XMutex_lock(pool->m_mutex);
    XThreadPool_threadDeleted_signal(pool, thread);
    int64_t groupIndex = XVector_indexOf(&pool->m_threadGroup, &thread, 0);
    if (groupIndex >= 0)
        XVector_remove_base(&pool->m_threadGroup, groupIndex, 1);
    thread->m_varList = NULL;
    XMutex_unlock(pool->m_mutex);

    XThread_deleteLater(thread);
    XAtomic_fetch_sub_uint32(&pool->m_activeThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
}
// 内部辅助函数：启动新工作线程
static bool XThreadPool_startWorkerThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    // 获取当前活跃线程数（线程池内部的工作线程）
    uint32_t active = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);

    // 获取预留线程数（被外部使用的线程槽位）
    uint32_t reserved = XAtomic_load_uint32(&pool->m_reservedThreadCount, XAtomic_MemoryOrder_Relaxed);

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
    if (!thread)
        return false;
  /*  XVarList_args_1(varList, XThreadPool*, tp);

    if (!thread)
    {
        XVarList_delete(varList);
        return false;
    }*/

    // 设置线程属性
    if (pool->m_stackSize > 0)
        XThread_setStackSize(thread, pool->m_stackSize);

    // 递增活跃计数必须先于 XThread_start：worker 退出路径（XThreadPool_worker_thread
    // 末尾）无条件 fetch_sub，计数先于线程启动递增才能保证每次递减都有配对递增
    XAtomic_fetch_add_uint32(&pool->m_activeThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
    // 启动线程
    if (!XThread_start(thread))
    {
        // 启动失败：线程从未运行，worker 退出路径不会执行，需自行回退计数；
        // m_varList 仍指向存活的线程池，平台析构会把它当 XVarList 错误释放，
        // 先清空（对标 worker 退出时的清理）
        XAtomic_fetch_sub_uint32(&pool->m_activeThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
        thread->m_varList = NULL;
        XThread_deleteLater(thread);
        return false;
    }
    // 将线程添加到线程组
    if (!XVector_push_back_1_base(&pool->m_threadGroup, &thread))
    {
        // 入组失败时线程已在运行且计数已配对：不能对运行中的线程对象 deleteLater
        // （worker 退出路径会自行 deleteLater，提前删造成 UAF/双删），也不回退计数
        // （由 worker 退出路径配对递减）。请求中断使其尽快退出，由 worker 退出路径
        // 自行完成组移除（未入组自动跳过）、计数递减与自删除
        XThread_requestInterruption(thread);
        return false;
    }
    // 触发线程创建信号
    XThreadPool_threadCreated_signal(pool, thread);
    return true;
}
// XThreadPool_start1 - 提交任务到线程池
void XThreadPool_start1(XThreadPool* pool, XRunnable* runnable, int priority)
{
    XAssert(pool && runnable, "pool or runnable is NULL");

    XMutex_lock(pool->m_mutex);

    // 创建带优先级的任务并添加到优先级队列
    PrioritizedTask task = { runnable, priority };
    if (!XPriorityQueue_push_base(&pool->m_waitQueue, &task)) {
        // 入队失败：已按约定接管所有权，就地删除防止任务泄漏
        if (XRunnable_autoDelete(runnable))
            XClassDelete(runnable);
        XMutex_unlock(pool->m_mutex);
        return;
    }

    // 尝试启动工作线程（如果当前活跃线程数小于最大线程数）
    uint32_t active_count = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
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

    uint32_t reserved = XAtomic_load_uint32(&pool->m_reservedThreadCount, XAtomic_MemoryOrder_Acquire);
    if (reserved == 0) {
        // 无预留线程：任务未入队，按所有权约定删除防止泄漏
        if (XRunnable_autoDelete(runnable))
            XClassDelete(runnable);
        XMutex_unlock(pool->m_mutex);
        return;
    }

    XAtomic_fetch_sub_uint32(&pool->m_reservedThreadCount, 1, XAtomic_MemoryOrder_Release);

    if (!XQueueBase_push_base(&pool->m_reservedQueue, &runnable)) {
        // 队列满，恢复预留计数
        XAtomic_fetch_add_uint32(&pool->m_reservedThreadCount, 1, XAtomic_MemoryOrder_Release);
        // 入队失败：任务未入队，按所有权约定删除防止泄漏
        if (XRunnable_autoDelete(runnable))
            XClassDelete(runnable);
        XMutex_unlock(pool->m_mutex);
        return;
    }

    uint32_t active_count = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Acquire);
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
    uint32_t idle_count = XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed);
    uint32_t active_count = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
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
    XRunnable* runnable = XRunnable_create_from_function(function, argsList, true);
    if (!runnable)
        return false;
    if (XThreadPool_tryStart1(pool, runnable))
        return true;
    // tryStart1 失败视为任务未入队；本函数无句柄交还调用方，就地删除防止泄漏
    if (XRunnable_autoDelete(runnable))
        XClassDelete(runnable);
    return false;
}
// XThreadPool_expiryTimeout - 获取过期超时时间
int XThreadPool_expiryTimeout(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return (int)pool->m_expiryTimeout;
}

// XThreadPool_setExpiryTimeout - 设置过期超时时间
void XThreadPool_setExpiryTimeout(XThreadPool* pool, int expiryTimeout)
{
    XAssert(pool, "pool is NULL");
    pool->m_expiryTimeout = (uint32_t)expiryTimeout;
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
    pool->m_maxThreadCount = (uint16_t)(maxThreadCount > 0 ? maxThreadCount : 0);
}

// XThreadPool_activeThreadCount - 获取当前活跃线程数
int XThreadPool_activeThreadCount(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return (int)XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
}

// XThreadPool_setStackSize - 设置工作线程栈大小
void XThreadPool_setStackSize(XThreadPool* pool, uint32_t stackSize)
{
    XAssert(pool, "pool is NULL");
    pool->m_stackSize = stackSize;
}

// XThreadPool_stackSize - 获取工作线程栈大小
uint32_t XThreadPool_stackSize(const XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    return pool->m_stackSize;
}
// XThreadPool_reserveThread - 预留线程
void XThreadPool_reserveThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    XAtomic_fetch_add_uint32(&pool->m_reservedThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
}

// XThreadPool_releaseThread - 释放预留的线程
void XThreadPool_releaseThread(XThreadPool* pool)
{
    XAssert(pool, "pool is NULL");
    XAtomic_fetch_sub_uint32(&pool->m_reservedThreadCount, 1, XAtomic_MemoryOrder_Relaxed);
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
    uint32_t active = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
    uint32_t idle = XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed);

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
            XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed) !=
            XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed)) {
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
            XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed) !=
            XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed)) {
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
        uint32_t final_active = XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed);
        uint32_t final_idle = XAtomic_load_uint32(&pool->m_idleThreadCount, XAtomic_MemoryOrder_Relaxed);
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
                XClassDelete(task.runnable);
            }
        }
    }
    // 清空预留队列
    while (!XContainer_isEmpty_base(&pool->m_reservedQueue)) {
        XRunnable* runnable;
        if (XQueueBase_receive_base(&pool->m_reservedQueue, &runnable)) {
            if (XRunnable_autoDelete(runnable)) {
                XClassDelete(runnable);
            }
        }
    }
    // 如果队列变空了，唤醒等待完成的线程
    if (XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed) == 0) {
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
            XClassDelete(runnable);
        }

        // 如果队列变空且没有活跃线程，唤醒等待完成的线程
        if (XContainer_isEmpty_base(&pool->m_waitQueue) &&
            XAtomic_load_uint32(&pool->m_activeThreadCount, XAtomic_MemoryOrder_Relaxed) == 0) {
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

#endif // XTHREADPOOL_ON
#endif /* XSYNC_ON */
