#include "XEventLoop.h"
#include "XEventDispatcher.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XTimerBase.h"
#include "XQueueBase.h"
#include "XThread.h"
#include "XCoreApplication.h"
//#include "XWindow.h"  // 假设存在窗口相关定义

static void VXEventLoop_deinit(XEventLoop* loop);
static int VXEventLoop_exec(XEventLoop* loop);
static void VXEventLoop_quit(XEventLoop* loop, int exitCode);
static void VXEventLoop_wakeUp(XEventLoop* loop);
static void VXEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags);
static bool VXEventLoop_hasPendingEvents(XEventLoop* loop);
static uint64_t VXEventLoop_calculateNextTimeout(XEventLoop* loop);

/**
 * @brief 初始化事件循环类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventLoop_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventLoop))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = {
        VXEventLoop_exec,
        VXEventLoop_quit,
        VXEventLoop_wakeUp,
        VXEventLoop_processEvents,
        VXEventLoop_hasPendingEvents
    };

    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventLoop_deinit);

#if SHOWCONTAINERSIZE
    printf("XEventLoop size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建事件循环实例
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 * @return 新创建的事件循环实例
 */
XEventLoop* XEventLoop_create() {
    XEventLoop* loop = XMemory_malloc(sizeof(XEventLoop));
    XEventLoop_init(loop);
    return loop;
}

XEventLoop* XEventLoop_create_thread()
{
    XEventLoop* loop = XMemory_malloc(sizeof(XEventLoop));
    XClass_init(&loop->m_parent);
    XClassGetVtable(loop) = XEventLoop_class_init();

    // 初始化互斥锁和条件变量
    loop->m_mutex = XMutex_create();
    loop->m_condition = XWaitCondition_create();

    loop->m_dispatcher = XEventDispatcher_create(XEventLoop_QueueSize);
    XEventDispatcher_setEventLoop(loop->m_dispatcher, loop);

    // 初始化定时器组
    loop->m_timerGroup = XTimerGroupWheel_create(1);
    XTimerGroupWheel_addTimeWheel_base(loop->m_timerGroup,100);//0~100ms    -1ms
    XTimerGroupWheel_addTimeWheel_base(loop->m_timerGroup,10);//100ms~1S    -100ms
    XTimerGroupWheel_addTimeWheel_base(loop->m_timerGroup,10);//1S~10S      -1s
    XTimerGroupWheel_addTimeWheel_base(loop->m_timerGroup,10);//10~100S     -10s
    XTimerGroupWheel_addTimeWheel_base(loop->m_timerGroup,10);//100~1000s   -100s
    loop->m_state = XEventLoop_Suspended;
    loop->m_exitCode = 0;
    loop->m_quitOnLastWindowClosed = true;
    return loop;
}

/**
 * @brief 初始化事件循环
 * @param loop 要初始化的事件循环
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 */
void XEventLoop_init(XEventLoop* loop) {
    if (loop == NULL) return;

    XClass_init(&loop->m_parent);
    XClassGetVtable(loop) = XEventLoop_class_init();

    // 初始化互斥锁和条件变量
    loop->m_mutex = XMutex_create();
    loop->m_condition = XWaitCondition_create();

    XThread* thread = XThread_currentThread();
    XEventDispatcher* d = NULL;
    if (thread == NULL)
    {//当前是主线程
        d = XCoreApplication_getDispatcher();
    }
    else
    {
        d = XThread_getDispatcher(thread);
    }
    // 使用提供的调度器或创建默认调度器
    if (d) {
        loop->m_dispatcher = d;
    }
    else {
        loop->m_dispatcher = XEventDispatcher_create(1024);
    }
    XEventDispatcher_setEventLoop(loop->m_dispatcher, loop);

    // 初始化定时器组
    loop->m_timerGroup = XTimerGroupWheel_create(1);

    loop->m_state = XEventLoop_Suspended;
    loop->m_exitCode = 0;
    loop->m_quitOnLastWindowClosed = true;
}

/**
 * @brief 获取事件循环关联的调度器
 * @param loop 事件循环实例
 * @return 事件调度器
 */
XEventDispatcher* XEventLoop_getDispatcher(XEventLoop* loop) {
    return loop ? loop->m_dispatcher : NULL;
}

/**
 * @brief 获取事件循环关联的定时器组
 * @param loop 事件循环实例
 * @return 定时器组
 */
XTimerGroupWheel* XEventLoop_getTimerGroup(XEventLoop* loop) {
    return loop ? loop->m_timerGroup : NULL;
}

/**
 * @brief 设置当最后一个窗口关闭时是否退出事件循环
 * @param loop 事件循环实例
 * @param quit 是否退出
 */
void XEventLoop_setQuitOnLastWindowClosed(XEventLoop* loop, bool quit) {
    if (loop) {
        loop->m_quitOnLastWindowClosed = quit;
    }
}

/**
 * @brief 启动事件循环
 * @param loop 事件循环实例
 * @return 退出代码
 */
static int VXEventLoop_exec(XEventLoop* loop) {
    if (!loop || loop->m_state == XEventLoop_Running) return -1;

    loop->m_state = XEventLoop_Running;
    loop->m_exitCode = 0;

    while (loop->m_state == XEventLoop_Running) {
        // 处理定时器事件
        XTimerGroupWheel_handler_base(loop->m_timerGroup);

        // 处理事件
        VXEventLoop_processEvents(loop, XEventLoop_AllEvents);

        // 检查是否需要退出（如最后一个窗口关闭）
       /* if (loop->m_quitOnLastWindowClosed && XWindow_getWindowCount() == 0) {
            VXEventLoop_quit(loop, 0);
            break;
        }*/

        // 如果没有事件，等待唤醒
        //if (!VXEventLoop_hasPendingPendingEvents(loop) && loop->m_state == XEventLoop_Running) {
        //    // 计算下一个定时器触发时间，作为等待超时时间
        //    uint64_t nextTimeout = VXEventLoop_calculateNextTimeout(loop);

        //    XMutex_lock(loop->m_mutex);
        //    if (nextTimeout > 0) {
        //        XWaitCondition_wait(loop->m_condition, loop->m_mutex, nextTimeout);
        //    }
        //    else {
        //        XWaitCondition_wait(loop->m_condition, loop->m_mutex, -1);
        //    }
        //    XMutex_unlock(loop->m_mutex);
        //}
    }

    return loop->m_exitCode;
}

/**
 * @brief 退出事件循环
 * @param loop 事件循环实例
 * @param exitCode 退出代码
 */
static void VXEventLoop_quit(XEventLoop* loop, int exitCode) {
    if (!loop) return;

    XMutex_lock(loop->m_mutex);
    loop->m_state = XEventLoop_Quit;
    loop->m_exitCode = exitCode;
    XMutex_unlock(loop->m_mutex);

    VXEventLoop_wakeUp(loop);
}

/**
 * @brief 唤醒事件循环
 * @param loop 事件循环实例
 */
static void VXEventLoop_wakeUp(XEventLoop* loop) {
    if (!loop) return;

    XMutex_lock(loop->m_mutex);
    XWaitCondition_wakeOne(loop->m_condition);
    XMutex_unlock(loop->m_mutex);
}

/**
 * @brief 处理当前待处理的事件
 * @param loop 事件循环对象
 * @param flags 事件处理标志
 */
static void VXEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags) {
    if (!loop || !loop->m_dispatcher || loop->m_state != XEventLoop_Running) return;

    // 根据标志志处理不同类型的事件
    if (flags & XEventLoop_ExcludeUserInputEvents) {
        // 排除用户输入事件的处理逻辑
        // 这里简化处理，实际实现需要过滤相关事件类型
    }
    else if (flags & XEventLoop_ExcludeSocketNotifiers) {
        // 排除套接字字通知事件的处理逻辑
    }

    // 处理事件队列中的所有事件
    XEventDispatcher_handler_base(loop->m_dispatcher);

    // 如果需要等待更多事件，这里里可以添加额外逻辑
    if (flags & XEventLoop_WaitForMoreEvents && !VXEventLoop_hasPendingEvents(loop)) {
        XMutex_lock(loop->m_mutex);
        XWaitCondition_wait(loop->m_condition, loop->m_mutex, 10); // 短暂等待
        XMutex_unlock(loop->m_mutex);
    }
}

/**
 * @brief 检查是否有待处理的事件
 * @param loop 事件循环实例
 * @return 是否有待处理的事件
 */
static bool VXEventLoop_hasPendingEvents(XEventLoop* loop) {
    if (!loop) return false;

    /*for (int i = 0; i < XEVENT_PRIORITY_COUNT; i++) 
    {
        if (loop->m_dispatcher->m_queue[i]&& !XQueueBase_isEmpty_base(loop->m_dispatcher->m_queue[i]))
        {
            return true;
        }
    }*/
    if (!XQueueBase_isEmpty_base(loop->m_dispatcher->m_queue))
        return true;
    // 检查事件队列和定时器
    return XTimerGroupWheel_hasActiveTimers(loop->m_timerGroup);
}

/**
 * @brief 计算下一个定时器超时时间
 * @param loop 事件循环实例
 * @return 下一个超时时间（毫秒），0表示无定时器
 */
static uint64_t VXEventLoop_calculateNextTimeout(XEventLoop* loop) {
    if (!loop || !loop->m_timerGroup) return 0;
    return XTimerGroupWheel_getNextTimeout(loop->m_timerGroup);
}

/**
 * @brief 释放事件循环资源
 * @param loop 事件循环实例
 */
static void VXEventLoop_deinit(XEventLoop* loop) {
    if (!loop) return;

    // 释放定时器组
    if (loop->m_timerGroup) {
        XTimerGroupWheel_delete_base(loop->m_timerGroup);
        loop->m_timerGroup = NULL;
    }

    // 释放放事件调度器
    if (loop->m_dispatcher) {
        XEventDispatcher_delete_base(loop->m_dispatcher);
        loop->m_dispatcher = NULL;
    }

    // 销毁互斥锁和条件变量
    XMutex_delete(loop->m_mutex);
    XWaitCondition_delete(loop->m_condition);
}

// 基础函数实现
int XEventLoop_exec_base(XEventLoop* loop) {
    if (ISNULL(loop, "") || ISNULL(XClassGetVtable(loop), ""))
        return -1;
    return XClassGetVirtualFunc(loop, EXEventLoop_Exec, int (*)(XEventLoop*))(loop);
}

void XEventLoop_quit_base(XEventLoop* loop, int exitCode) {
    if (ISNULL(loop, "") || ISNULL(XClassGetVtable(loop), ""))
        return;
    XClassGetVirtualFunc(loop, EXEventLoop_Quit, void (*)(XEventLoop*, int))(loop, exitCode);
}

void XEventLoop_wakeUp_base(XEventLoop* loop) {
    if (ISNULL(loop, "") || ISNULL(XClassGetVtable(loop), ""))
        return;
    XClassGetVirtualFunc(loop, EXEventLoop_WakeUp, void (*)(XEventLoop*))(loop);
}

void XEventLoop_processEvents_base(XEventLoop* loop, XEventLoopProcessEventsFlags flags) {
    if (!loop || !XClassGetVtable(loop)) return;
    XClassGetVirtualFunc(loop, EXEventLoop_ProcessEvents, void (*)(XEventLoop*, XEventLoopProcessEventsFlags))(loop, flags);
}

bool XEventLoop_hasPendingEvents_base(XEventLoop* loop) {
    if (ISNULL(loop, "") || ISNULL(XClassGetVtable(loop), ""))
        return false;
    return XClassGetVirtualFunc(loop, EXEventLoop_HasPendingEvents, bool (*)(XEventLoop*))(loop);
}