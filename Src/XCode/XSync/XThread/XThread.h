#ifndef XTHREAD_H
#define XTHREAD_H

#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 线程优先级枚举
 * 定义了不同的线程优先级选项
 */
typedef enum 
{
    XThread_err=-1,
    XThread_IdlePriority,         /**< 空闲优先级 */
    XThread_LowestPriority,       /**< 最低优先级 */
    XThread_LowPriority,          /**< 低优先级 */
    XThread_NormalPriority,       /**< 正常优先级 */
    XThread_HighPriority,         /**< 高优先级 */
    XThread_HighestPriority,      /**< 最高优先级 */
    XThread_TimeCriticalPriority, /**< 时间关键优先级 */
    XThread_InheritPriority       /**< 继承优先级 */
} XThread_Priority;

/**
 * @brief XThread虚函数表枚举
 * 定义了XThread类的虚函数表枚举值
 */
XCLASS_DEFINE_BEGING(XThread)
XCLASS_DEFINE_ENUM(XThread, Run) = XCLASS_VTABLE_GET_SIZE(XObject),  //
XCLASS_DEFINE_END(XThread)
typedef struct XVarList XVarList;
typedef struct XThreadData XThreadData;
typedef void (*XThreadFunc)(XThread*,XVarList*);
/**
 * @brief 线程类
 * 表示一个线程对象，包含线程的基本属性和状态
 */
typedef struct XThread
{
    XObject m_base;                           /**< 基类 */
    uint8_t  m_finished:1;                  /**< 线程是否结束 */
    uint8_t  m_interruptionRequested:1;     /**< 是否请求中断线程 */
    uint8_t  m_running:1;                   // 线程是否正在运行
    uint8_t  m_isMainThread : 1;            // 当前是主线程
    XThread_Priority m_priority;            /**< 线程优先级 */
    uint32_t m_stackSize;                   /**< 线程栈大小 */
    XHandle m_handle;                       /**< 线程句柄 */
    XThreadData* m_data;                    // 指向该线程私有的线程数据（包含事件队列、锁等）
    XThreadFunc  m_start_routine;
    XVarList* m_varList;
    XEventLoop* m_loop;//事件循环
} XThread;
/**
 * @brief 初始化XThread类的虚函数表
 * @retval 返回初始化后的XVtable指针
 */
XVtable* XThread_class_init();

/**
 * @brief 初始化XThread对象
 * @param Object 指向XThread对象的指针
 */
void XThread_init(XThread* thread);

/**
 * @brief 创建一个新的XThread对象
 * @param start_routine 线程启动时要执行的函数指针
 * @param arg 传递给线程启动函数的参数
 * @retval 若内存分配成功，返回新创建的XThread对象指针；否则返回NULL
 */
XThread* XThread_create_func(XThreadFunc start_routine, XVarList* varlist);
XThread* XThread_create(XObject*parent);
XThread* XThread_createMainThread(XObject* parent);
/**
 * @brief 获取XThread对象的句柄
 * @param Object 指向XThread对象的指针
 * @retval 返回线程的句柄
 */
XHandle XThread_getHandle(XThread* thread);

/**
 * @brief 等待XThread对象对应的线程结束
 * @param Object 指向XThread对象的指针
 * @param time 等待的时间（毫秒）
 * @retval 若线程在指定时间内结束，返回true；否则返回false
 */
bool XThread_wait(XThread* thread, uint32_t time);

/**
 * @brief 启动XThread对象对应的线程
 * @param Object 指向XThread对象的指针
 * @retval 若线程启动成功，返回true；否则返回false
 */
bool XThread_start(XThread* thread);

/**
 * @brief 获取XThread对象的事件调度器
 * @param Object 指向常量XThread对象的指针
 * @retval 返回事件调度器的指针
 */
XEventDispatcher* XThread_dispatcher(const XThread* thread);

bool XThread_isCurrentThread(const XThread* thread);
/**
 * @brief 判断XThread对象对应的线程是否结束
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程已结束，返回true；否则返回false
 */
bool XThread_isFinished(const XThread* thread);

/**
 * @brief 判断XThread对象对应的线程是否被请求中断
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程被请求中断，返回true；否则返回false
 */
bool XThread_isInterruptionRequested(const XThread* thread);

/**
 * @brief 判断XThread对象对应的线程是否正在运行
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程正在运行，返回true；否则返回false
 */
bool XThread_isRunning(const XThread* thread);

/**
 * @brief 获取XThread对象的线程循环级别
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的循环级别
 */
int XThread_loopLevel(const XThread* thread);

/**
 * @brief 获取XThread对象的线程优先级
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的优先级
 */
XThread_Priority XThread_priority(const XThread* thread);

/**
 * @brief 请求中断XThread对象对应的线程
 * @param Object 指向XThread对象的指针
 */
void XThread_requestInterruption(XThread* thread);

/**
 * @brief 设置XThread对象的事件调度器
 * @param Object 指向XThread对象的指针
 * @param eventDispatcher 事件调度器的指针
 */
void XThread_setEventDispatcher(XThread* thread, XEventDispatcher* eventDispatcher);

/**
 * @brief 设置XThread对象的线程优先级
 * @param Object 指向XThread对象的指针
 * @param priority 要设置的线程优先级
 */
void XThread_setPriority(XThread* thread, XThread_Priority priority);

/**
 * @brief 设置XThread对象的线程栈大小
 * @param Object 指向XThread对象的指针
 * @param stackSize 要设置的线程栈大小
 */
void XThread_setStackSize(XThread* thread, uint32_t stackSize);

/**
 * @brief 获取XThread对象的线程栈大小
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的栈大小
 */
uint32_t XThread_stackSize(const XThread* thread);
void XThread_exit(XThread* thread,int returnCode);
void XThread_quit(XThread* thread);

//强制终止线程
bool XThread_terminate(XThread* thread);
/**
 * @brief 删除XThread对象
 * 等价于调用XClass_delete_base
 */
#define XThread_deleteLater XObject_deleteLater
#define XThread_deinitLater XObject_deinitLater

Signals
//线程结束信号
void* XThread_finished_signal(XThread* thread);
//线程启动信号
void* XThread_started_signal(XThread* thread);

XThread* XThread_currentThread();
//返回当前线程的事件循环
XEventDispatcher* XThread_currentDispatcher();
XHandle XThread_currentThreadId();
bool XThread_isMainThread();
//返回理想的线程数量
int XThread_idealThreadCount();
void XThread_msleep(uint32_t msecs);
void XThread_sleep(uint32_t secs);
void XThread_usleep(uint32_t tsecs);
//主动让出当前线程的 CPU 时间片
void XThread_yieldCurrentThread();

Protected
//启动线程事件循环，在 run内调用
int XThread_exec(XThread* thread);
//在传入的回调函数中调用,外部不能调用
void XThread_run_base(XThread* thread);
#ifdef __cplusplus
}
#endif
#endif