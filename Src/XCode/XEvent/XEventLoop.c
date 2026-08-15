#include "XEventLoop.h"
#include "XAbstractEventDispatcher.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XTimer.h"
#include "XQueueBase.h"
#include "XThread.h"
#include "XThreadData.h"
#include "XCoreApplication.h"
#include "XLockFreeQueue.h"
#include "XTimeWheelGroup.h"
#include "XAtomic.h"
#include <stdlib.h>

static void VXEventLoop_deinit(XEventLoop* loop);
static bool VXEventLoop_event(XEventLoop* loop, XEvent* event);

static XThreadData* XEventLoop_threadData(const XEventLoop* loop)
{
    if (!loop) return NULL;
    XThreadData* td = XObject_threadData((const XObject*)loop);
    if (!td) return XThreadData_current();
    return td;
}

static XAbstractEventDispatcher* XEventLoop_dispatcher(XEventLoop* loop)
{
    XThreadData* data = XEventLoop_threadData(loop);
    return data ? data->m_eventDispatcher : NULL;
}

/**
 * @brief 初始化事件循环类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventLoop_class_init() {
    XVTABLE_INIT_DEFAULT(XEventLoop)
        XVTABLE_INHERIT_XCLASS(XObject);

    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXEventLoop_event);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventLoop_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XEventLoop);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建事件循环实例
 * @return 新创建的事件循环实例
 */
XEventLoop* XEventLoop_create_ex(XMemoryType memory) {
    XEventLoop* loop = XMemory_malloc(sizeof(XEventLoop), memory);
    if (!loop)
        return NULL;
    XEventLoop_init(loop);
    Set_Class_Memory(loop, memory); Set_Class_IsHeap(loop, true);
    return loop;
}

/**
 * @brief 初始化事件循环（对标 Qt 6.8 QEventLoop 构造函数）
 */
void XEventLoop_init(XEventLoop* loop)
{
    if (loop == NULL) return;

    XObject_init(&loop->m_class);
    XClassGetVtable(loop) = XEventLoop_class_init();

    loop->m_deley = NULL;
    XThreadData* data = XThreadData_current();
    loop->m_dispatcher = XThreadData_ensureEventDispatcher(data);
    loop->m_state = XEventLoop_Suspended;
    loop->m_exitCode = -1;
    loop->inExec = false;  // Qt 6.8: 初始未进入 exec
    XAtomic_init(loop->m_exit, true);
    XAtomic_init(loop->m_returnCode, -1);
}

void XEventLoop_delay(size_t msec)
{
    (void)msec;
    /* 保留接口，实现待定 */
}

/**
 * @brief 启动事件循环（对标 Qt 6.8 QEventLoop::exec()）
 *
 * Qt 6.8 关键行为:
 *  1. 检查 quitNow → 立即返回 -1
 *  2. 检查 inExec → 防止重入
 *  3. 锁定线程互斥锁保护 QThread::exit 竞态
 *  4. push/pop eventLoops 栈 + loopLevel 增减
 *  5. 移除已投递的 Quit 事件
 *  6. 循环 processEvents(flags | WaitForMoreEvents | EventLoopExec)
 */
int XEventLoop_exec(XEventLoop* loop, XEventLoopProcessEventsFlags flags) {
    if (!loop) return -1;

    XThreadData* data = XEventLoop_threadData(loop);
    if (!data || data != XThreadData_current()) return -1;

    /* Qt 6.8: 对齐 QThread::exit 竞态保护 — 检查 quitNow */
    if (data->m_quitNow)
        return -1;

    /* Qt 6.8: 防止重入 exec() */
    if (loop->inExec) {
        XERROR_PRINTF("XEventLoop::exec: instance %p has already called exec()\n", (void*)loop);
        return -1;
    }

    /* Qt 6.8: 标记进入 exec，压入事件循环栈 */
    loop->inExec = true;
    loop->m_state = XEventLoop_Running;
    loop->m_exitCode = 0;
    XAtomic_store_bool(&loop->m_exit, false, XAtomic_MemoryOrder_Release);
    XAtomic_store_int32(&loop->m_returnCode, 0, XAtomic_MemoryOrder_Relaxed);

    XThreadData_pushEventloop(data, loop);

    /* Qt 6.8: 移除已投递的 Quit 事件（对标 removePostedEvents(app, QEvent::Quit)） */
    XCoreApplication* app = XCoreApplication_instance();
    if (app && XObject_threadData((XObject*)app) == XObject_threadData((const XObject*)loop))
        XCoreApplication_removePostedEvents((XObject*)app, XEVENT_TYPE_QUIT);

    /* Qt 6.8: 循环处理事件，携带 WaitForMoreEvents | EventLoopExec 标志 */
    while (!XAtomic_load_bool(&loop->m_exit, XAtomic_MemoryOrder_Acquire))
    {
        XEventLoop_processEvents(loop, flags | XEventLoop_WaitForMoreEvents | XEventLoop_EventLoopExec);
    }

    XThreadData_popEventloop(data, loop);
    loop->inExec = false;

    loop->m_exitCode = XAtomic_load_int32(&loop->m_returnCode, XAtomic_MemoryOrder_Relaxed);
    loop->m_state = XEventLoop_Quit;
    return loop->m_exitCode;
}

/**
 * @brief 退出事件循环（对标 Qt 6.8 QEventLoop::exit()）
 *
 * Qt 6.8 关键行为:
 *  1. 检查 hasEventDispatcher()
 *  2. 设置 returnCode 和 exit 标志
 *  3. 调用 dispatcher->interrupt() 唤醒阻塞等待
 */
void XEventLoop_exit(XEventLoop* loop, int exitCode) {
    if (!loop) return;

    XAbstractEventDispatcher* dispatcher = XEventLoop_dispatcher(loop);
    if (!dispatcher)
        return;

    XAtomic_store_int32(&loop->m_returnCode, exitCode, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_bool(&loop->m_exit, true, XAtomic_MemoryOrder_Release);

    XAbstractEventDispatcher_interrupt_base(dispatcher);
}

void XEventLoop_quit(XEventLoop* loop)
{
    XEventLoop_exit(loop, 0);
}

/**
 * @brief 判断事件循环是否正在运行（对标 Qt 6.8 QEventLoop::isRunning()）
 */
bool XEventLoop_isRunning(XEventLoop* loop)
{
    return loop && !XAtomic_load_bool(&loop->m_exit, XAtomic_MemoryOrder_Acquire);
}

/**
 * @brief 唤醒事件循环（对标 Qt 6.8 QEventLoop::wakeUp()）
 */
void XEventLoop_wakeUp(XEventLoop* loop) {
    if (!loop) return;

    XAbstractEventDispatcher* dispatcher = XEventLoop_dispatcher(loop);
    if (!dispatcher)
        return;

    XAbstractEventDispatcher_wakeUp_base(dispatcher);
}

/**
 * @brief 处理当前待处理的事件（对标 Qt 6.8 QEventLoop::processEvents()）
 *
 * Qt 6.8 关键行为:
 *  1. 检查 hasEventDispatcher()
 *  2. 委托给 dispatcher->processEvents(flags)
 *  3. 返回 bool
 */
bool XEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags)
{
    XAbstractEventDispatcher* dispatcher = XEventLoop_dispatcher(loop);
    if (!dispatcher)
        return false;

    return XAbstractEventDispatcher_processEvents_base(dispatcher, flags);
}

static bool VXEventLoop_event(XEventLoop* loop, XEvent* event)
{
    if (!loop || !event) return false;
    if (event->type == XEVENT_TYPE_QUIT) {
        XEventLoop_quit(loop);
        XEvent_accept(event);
        return true;
    }
    return XClass_Parent(XObject, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)loop, event);
}

/**
 * @brief 释放事件循环资源
 */
static void VXEventLoop_deinit(XEventLoop* loop)
{
    if (!loop) return;
    XClass_Deinit_Parent(XObject, loop);
}
