#ifndef XTHREAD_H
#define XTHREAD_H

#include "XClass.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 线程优先级枚举
 * 定义了不同的线程优先级选项
 */
    typedef enum {
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
XCLASS_DEFINE_ENUM(XThread, Start),  //
XCLASS_DEFINE_ENUM(XThread, GetHandle),
XCLASS_DEFINE_ENUM(XThread, Wait),
XCLASS_DEFINE_ENUM(XThread, EventDispatcher),
XCLASS_DEFINE_ENUM(XThread, IsFinished),
XCLASS_DEFINE_ENUM(XThread, IsInterruptionRequested),
XCLASS_DEFINE_ENUM(XThread, IsRunning),
XCLASS_DEFINE_ENUM(XThread, LoopLevel),
XCLASS_DEFINE_ENUM(XThread, Priority),
XCLASS_DEFINE_ENUM(XThread, RequestInterruption),
XCLASS_DEFINE_ENUM(XThread, SetEventDispatcher),
XCLASS_DEFINE_ENUM(XThread, SetPriority),
XCLASS_DEFINE_ENUM(XThread, SetStackSize),
XCLASS_DEFINE_ENUM(XThread, StackSize),
XCLASS_DEFINE_END(XThread)

/**
 * @brief 线程类
 * 表示一个线程对象，包含线程的基本属性和状态
 */
    typedef struct XThread
{
    XClass m_base;                 /**< 基类 */
    XHandle m_handle;              /**< 线程句柄 */
    bool m_finished;               /**< 线程是否结束 */
    bool m_interruptionRequested;  /**< 是否请求中断线程 */
    int loopLevel;               /**< 线程循环级别 */
    XThread_Priority m_priority;   /**< 线程优先级 */
    uint32_t m_stackSize;          /**< 线程栈大小 */
    XEventDispatcher* m_eventDispatcher;       /**< 事件调度器指针 */
    void* (*m_start_routine)(void*);
    void* m_arg;
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
void XThread_init(XThread* Object);

/**
 * @brief 创建一个新的XThread对象
 * @param start_routine 线程启动时要执行的函数指针
 * @param arg 传递给线程启动函数的参数
 * @retval 若内存分配成功，返回新创建的XThread对象指针；否则返回NULL
 */
XThread* XThread_create(void* (*start_routine)(void*), void* arg);

/**
 * @brief 获取XThread对象的句柄
 * @param Object 指向XThread对象的指针
 * @retval 返回线程的句柄
 */
XHandle XThread_getHandle(XThread* Object);

/**
 * @brief 等待XThread对象对应的线程结束
 * @param Object 指向XThread对象的指针
 * @param time 等待的时间（毫秒）
 * @retval 若线程在指定时间内结束，返回true；否则返回false
 */
bool XThread_wait(XThread* Object, unsigned long time);

/**
 * @brief 启动XThread对象对应的线程
 * @param Object 指向XThread对象的指针
 * @retval 若线程启动成功，返回true；否则返回false
 */
bool XThread_start(XThread* Object);

/**
 * @brief 获取XThread对象的事件调度器
 * @param Object 指向常量XThread对象的指针
 * @retval 返回事件调度器的指针
 */
XEventDispatcher* XThread_eventDispatcher(const XThread* Object);

/**
 * @brief 判断XThread对象对应的线程是否结束
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程已结束，返回true；否则返回false
 */
bool XThread_isFinished(const XThread* Object);

/**
 * @brief 判断XThread对象对应的线程是否被请求中断
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程被请求中断，返回true；否则返回false
 */
bool XThread_isInterruptionRequested(const XThread* Object);

/**
 * @brief 判断XThread对象对应的线程是否正在运行
 * @param Object 指向常量XThread对象的指针
 * @retval 若线程正在运行，返回true；否则返回false
 */
bool XThread_isRunning(const XThread* Object);

/**
 * @brief 获取XThread对象的线程循环级别
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的循环级别
 */
int XThread_loopLevel(const XThread* Object);

/**
 * @brief 获取XThread对象的线程优先级
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的优先级
 */
XThread_Priority XThread_priority(const XThread* Object);

/**
 * @brief 请求中断XThread对象对应的线程
 * @param Object 指向XThread对象的指针
 */
void XThread_requestInterruption(XThread* Object);

/**
 * @brief 设置XThread对象的事件调度器
 * @param Object 指向XThread对象的指针
 * @param eventDispatcher 事件调度器的指针
 */
void XThread_setEventDispatcher(XThread* Object, XEventDispatcher* eventDispatcher);

/**
 * @brief 设置XThread对象的线程优先级
 * @param Object 指向XThread对象的指针
 * @param priority 要设置的线程优先级
 */
void XThread_setPriority(XThread* Object, XThread_Priority priority);

/**
 * @brief 设置XThread对象的线程栈大小
 * @param Object 指向XThread对象的指针
 * @param stackSize 要设置的线程栈大小
 */
void XThread_setStackSize(XThread* Object, uint32_t stackSize);

/**
 * @brief 获取XThread对象的线程栈大小
 * @param Object 指向常量XThread对象的指针
 * @retval 返回线程的栈大小
 */
uint32_t XThread_stackSize(const XThread* Object);

/**
 * @brief 删除XThread对象
 * 等价于调用XClass_delete_base
 */
#define XThread_delete XClass_delete_base

XThread* XThread_currentThread();
XHandle XThread_currentThreadId();

//不是给用户的内部API
void XThread_mapRemove(XThread* Object);
#ifdef __cplusplus
}
#endif
#endif