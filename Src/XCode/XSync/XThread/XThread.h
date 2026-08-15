#ifndef XTHREAD_H
#define XTHREAD_H
#include "XSync_config.h"
#if XSYNC_ON

#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations: XThread/XThreadData/XVarList incomplete types stay available even when XTHREAD_ON is off. */
typedef struct XVarList XVarList;
typedef struct XThreadData XThreadData;
typedef struct XThread XThread;

#if XTHREAD_ON

/**
 * @brief 线程优先级枚举（对标 Qt 6.8 QThread::Priority）
 *
 * 定义线程的调度优先级选项，从空闲到时间关键；InheritPriority 表示继承创建者优先级。
 */
typedef enum 
{
    XThread_err=-1,                 /**< 错误哨兵值（无效优先级） */
    XThread_IdlePriority,           /**< 空闲优先级（仅在无其它线程运行时调度） */
    XThread_LowestPriority,         /**< 最低优先级 */
    XThread_LowPriority,            /**< 低优先级 */
    XThread_NormalPriority,         /**< 正常优先级（默认） */
    XThread_HighPriority,           /**< 高优先级 */
    XThread_HighestPriority,        /**< 最高优先级 */
    XThread_TimeCriticalPriority,   /**< 时间关键优先级（尽可能频繁调度） */
    XThread_InheritPriority         /**< 继承优先级（继承创建线程的优先级） */
} XThread_Priority;

/**
 * @brief XThread 虚函数表枚举
 *
 * 定义 XThread 类的虚函数表索引，Run 为线程入口虚函数（对标 QThread::run）。
 */
XCLASS_DEFINE_BEGING(XThread)
XCLASS_DEFINE_ENUM(XThread, Run) = XCLASS_VTABLE_GET_SIZE(XObject),  /**< 线程入口虚函数（对标 QThread::run） */
XCLASS_DEFINE_END(XThread)
/**
 * @brief 线程入口函数类型（对标 QThread::run 的回调形式）
 * @param thread 当前线程对象
 * @param varList 传入线程的参数列表
 */
typedef void (*XThreadFunc)(XThread*,XVarList*);

/**
 * @brief 线程类（对标 Qt 6.8 QThread）
 *
 * 表示一个可管理的线程对象，封装线程句柄、优先级、栈大小、事件循环等，
 * 支持 moveToThread 线程亲和性、跨线程信号槽投递与中断请求。
 */
typedef struct XThread
{
    XObject m_base;                           /**< 基类（对标 QObject） */
    uint8_t  m_finished:1;                    /**< 线程是否已结束 */
    uint8_t  m_interruptionRequested:1;       /**< 是否请求中断线程（对标 QThread::isInterruptionRequested） */
    uint8_t  m_running:1;                     /**< 线程是否正在运行 */
    uint8_t  m_isMainThread : 1;              /**< 当前是否为主线程 */
    XThread_Priority m_priority;              /**< 线程优先级（对标 QThread::priority） */
    uint32_t m_stackSize;                     /**< 线程栈大小（字节，对标 QThread::stackSize） */
    XHandle m_handle;                         /**< 平台线程句柄（对标 Qt::HANDLE） */
    XThreadData* m_data;                      /**< 该线程私有的线程数据（事件队列、锁等，对标 QThreadData） */
    XThreadFunc  m_start_routine;             /**< 线程入口函数指针（func 模式创建时使用） */
    XVarList* m_varList;                      /**< 传入线程入口函数的参数列表 */
    XEventLoop* m_loop;                       /**< 线程事件循环 */
} XThread;
/**
 * @brief 初始化 XThread 类的虚函数表
 * @return 初始化后的 XVtable 指针
 */
XVtable* XThread_class_init();

/**
 * @brief 初始化 XThread 对象（栈对象，对标 QThread 构造函数）
 * @param thread 指向 XThread 对象的指针
 */
void XThread_init(XThread* thread);

/**
 * @brief 以回调函数模式创建并初始化 XThread 对象（堆对象）
 * @param start_routine 线程启动时要执行的函数指针
 * @param varlist 传递给线程启动函数的参数列表
 * @return 成功返回新创建的 XThread 对象指针，失败返回 NULL
 */
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist);

/**
 * @brief 创建一个新的 XThread 对象（堆对象，对标 QThread 构造函数）
 * @param parent 父对象，可为 NULL
 * @return 成功返回新创建的 XThread 对象指针，失败返回 NULL
 */
XThread* XThread_create_ex(XMemoryType memory,  XObject*parent);

/**
 * @brief 创建主线程包装对象（对标 Qt QAdoptedThread，绑定主线程 XThreadData）
 * @param parent 父对象，可为 NULL
 * @return 主线程 XThread 对象指针
 */
XThread* XThread_createMainThread(XObject* parent);

/**
 * @brief 获取 XThread 对象的平台线程句柄
 * @param thread 指向 XThread 对象的指针
 * @return 线程句柄（对标 Qt::HANDLE）
 */
XHandle XThread_getHandle(XThread* thread);

/**
 * @brief 等待 XThread 对应的线程结束（对标 QThread::wait）
 * @param thread 指向 XThread 对象的指针
 * @param time 等待超时时间（毫秒）
 * @return 线程在指定时间内结束返回 true，否则返回 false
 */
bool XThread_wait(XThread* thread, uint32_t time);

/**
 * @brief 启动 XThread 对应的线程（对标 QThread::start）
 * @param thread 指向 XThread 对象的指针
 * @return 线程启动成功返回 true，否则返回 false
 */
bool XThread_start(XThread* thread);

/**
 * @brief 获取 XThread 对象的事件分发器（对标 QThread::eventDispatcher）
 * @param thread 指向常量 XThread 对象的指针
 * @return 事件分发器指针，未创建返回 NULL
 */
XEventDispatcher* XThread_dispatcher(const XThread* thread);

/**
 * @brief 判断当前执行线程是否为该 XThread（对标 QThread::isCurrentThread）
 * @param thread 指向常量 XThread 对象的指针
 * @return 是当前线程返回 true，否则返回 false
 */
bool XThread_isCurrentThread(const XThread* thread);

/**
 * @brief 判断 XThread 对应的线程是否已结束（对标 QThread::isFinished）
 * @param thread 指向常量 XThread 对象的指针
 * @return 线程已结束返回 true，否则返回 false
 */
bool XThread_isFinished(const XThread* thread);

/**
 * @brief 判断 XThread 对应的线程是否被请求中断（对标 QThread::isInterruptionRequested）
 * @param thread 指向常量 XThread 对象的指针
 * @return 已请求中断返回 true，否则返回 false
 */
bool XThread_isInterruptionRequested(const XThread* thread);

/**
 * @brief 判断 XThread 对应的线程是否正在运行（对标 QThread::isRunning）
 * @param thread 指向常量 XThread 对象的指针
 * @return 线程正在运行返回 true，否则返回 false
 */
bool XThread_isRunning(const XThread* thread);

/**
 * @brief 获取 XThread 对象的事件循环嵌套层级（对标 QThread::loopLevel）
 * @param thread 指向常量 XThread 对象的指针
 * @return 当前事件循环嵌套层级
 */
int XThread_loopLevel(const XThread* thread);

/**
 * @brief 获取线程的事件循环
 * @param thread 指向常量 XThread 对象的指针
 * @return 线程的事件循环指针，无则返回 NULL
 */
XEventLoop* XThread_eventLoop(const XThread* thread);

/**
 * @brief 获取 XThread 对象的线程优先级（对标 QThread::priority）
 * @param thread 指向常量 XThread 对象的指针
 * @return 线程优先级
 */
XThread_Priority XThread_priority(const XThread* thread);

/**
 * @brief 请求中断 XThread 对应的线程（对标 QThread::requestInterruption）
 * @param thread 指向 XThread 对象的指针
 */
void XThread_requestInterruption(XThread* thread);

/**
 * @brief 设置 XThread 对象的事件分发器（对标 QThread::setEventDispatcher）
 * @param thread 指向 XThread 对象的指针
 * @param eventDispatcher 事件分发器指针
 */
void XThread_setEventDispatcher(XThread* thread, XEventDispatcher* eventDispatcher);

/**
 * @brief 设置 XThread 对象的线程优先级（对标 QThread::setPriority）
 * @param thread 指向 XThread 对象的指针
 * @param priority 要设置的线程优先级
 */
void XThread_setPriority(XThread* thread, XThread_Priority priority);

/**
 * @brief 设置 XThread 对象的线程栈大小（对标 QThread::setStackSize）
 * @param thread 指向 XThread 对象的指针
 * @param stackSize 要设置的线程栈大小（字节）
 */
void XThread_setStackSize(XThread* thread, uint32_t stackSize);

/**
 * @brief 获取 XThread 对象的线程栈大小（对标 QThread::stackSize）
 * @param thread 指向常量 XThread 对象的指针
 * @return 线程栈大小（字节）
 */
uint32_t XThread_stackSize(const XThread* thread);

/**
 * @brief 退出线程的事件循环并指定返回码（对标 QThread::exit）
 * @param thread 指向 XThread 对象的指针
 * @param returnCode 线程退出返回码
 */
void XThread_exit(XThread* thread,int returnCode);

/**
 * @brief 请求线程的事件循环退出（对标 QThread::quit，等价于 exit(0)）
 * @param thread 指向 XThread 对象的指针
 */
void XThread_quit(XThread* thread);

/**
 * @brief 强制终止 XThread 对应的线程（对标 QThread::terminate）
 * @param thread 指向 XThread 对象的指针
 * @return 终止成功返回 true，否则返回 false
 */
bool XThread_terminate(XThread* thread);

/**
 * @brief 延迟删除 XThread 对象（等价于 XObject_deleteLater）
 */
#define XThread_deleteLater XObject_deleteLater

/**
 * @brief 延迟反初始化 XThread 对象（等价于 XObject_deinitLater）
 */
#define XThread_deinitLater XObject_deinitLater

Signals

/**
 * @brief 线程结束信号（对标 QThread::finished）
 * @param thread 指向 XThread 对象的指针
 * @return 信号对象指针
 */
void* XThread_finished_signal(XThread* thread);

/**
 * @brief 线程启动信号（对标 QThread::started）
 * @param thread 指向 XThread 对象的指针
 * @return 信号对象指针
 */
void* XThread_started_signal(XThread* thread);



Protected

/**
 * @brief 启动线程的事件循环，在 run() 内调用（对标 QThread::exec）
 * @param thread 指向 XThread 对象的指针
 * @return 事件循环退出返回码
 */
int XThread_exec(XThread* thread);

/**
 * @brief 线程入口基类实现，由平台层在回调中调用，外部不应直接调用（对标 QThread::run）
 * @param thread 指向 XThread 对象的指针
 */
void XThread_run_base(XThread* thread);
#endif // XTHREAD_ON

#if XTHREADDATA_ON

/**
 * @brief 获取当前线程对应的 XThread 对象（对标 QThread::currentThread）
 * @return 当前线程的 XThread 指针，主线程返回主线程包装对象
 */
XThread* XThread_currentThread();

/**
 * @brief 获取当前线程的事件分发器（对标 QAbstractEventDispatcher::instance）
 * @return 当前线程的事件分发器指针，未创建返回 NULL
 */
XEventDispatcher* XThread_currentEventDispatcher(void);

/**
 * @brief 获取当前线程的平台线程 ID（对标 QThread::currentThreadId）
 * @return 当前线程句柄（对标 Qt::HANDLE）
 */
XHandle XThread_currentThreadId();

/**
 * @brief 设置当前线程的 XThreadData 专用 TLS 指针（O(1) 无锁读取）
 *
 * 对标 Qt 6.8 thread_local currentThreadData + pthreadTlsKey（带析构函数）。
 * Posix: pthread_key_create 带析构，线程退出自动清理 adopted 线程数据；
 * Win32: TlsAlloc（无析构，需显式 clearCurrentThreadData）；
 * FreeRTOS: no-op。
 * @param p 要设置的 XThreadData 指针，NULL 表示清除当前线程 TLS
 */
void  XThreadStorage_set(void* p);

/**
 * @brief 获取当前线程的 XThreadData 专用 TLS 指针（O(1) 无锁）
 * @return 当前线程的 XThreadData 指针，未设置返回 NULL
 */
void* XThreadStorage_get(void);

/**
 * @brief 判断当前线程是否为主线程（对标 QThread::isMainThread）
 * @return 是主线程返回 true，否则返回 false
 */
bool XThread_isMainThread();

/**
 * @brief 返回理想的并发线程数量（对标 QThread::idealThreadCount）
 * @return 系统建议的线程数（通常等于 CPU 核心数）
 */
int XThread_idealThreadCount();

/**
 * @brief 当前线程休眠指定毫秒数（对标 QThread::msleep）
 * @param msecs 休眠时间（毫秒）
 */
void XThread_msleep(uint32_t msecs);

/**
 * @brief 当前线程休眠指定秒数（对标 QThread::sleep）
 * @param secs 休眠时间（秒）
 */
void XThread_sleep(uint32_t secs);

/**
 * @brief 当前线程休眠指定微秒数（对标 QThread::usleep）
 * @param tsecs 休眠时间（微秒）
 */
void XThread_usleep(uint32_t tsecs);

/**
 * @brief 主动让出当前线程的 CPU 时间片（对标 QThread::yieldCurrentThread）
 */
void XThread_yieldCurrentThread();

#endif // XTHREADDATA_ON

#ifdef __cplusplus
}
#endif
#endif /* XSYNC_ON */

/* XClass create API default-memory wrappers. */
#undef XThread_create
#define XThread_create(...) XThread_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif
