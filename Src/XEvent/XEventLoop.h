#ifndef XEVENTLOOP_H
#define XEVENTLOOP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XObject.h"
#include "XWaitCondition.h"
#include "XEventType.h"
#include "XAtomic.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 事件循环状态枚举
 */
typedef enum
{
    XEventLoop_Running,    // 运行中
    XEventLoop_Quit,       // 已退出
    XEventLoop_Suspended   // 已暂停
} XEventLoopState;

/**
 * @brief 事件处理标志
 */
typedef enum
{
    XEventLoop_AllEvents = 0x00,         // 处理所有事件
    XEventLoop_ExcludeUserInputEvents = 0x01,  // 排除用户输入事件
    XEventLoop_ExcludeSocketNotifiers = 0x02,  // 排除套接字通知事件
    XEventLoop_WaitForMoreEvents = 0x04   // 等待更多事件
} XEventLoopProcessEventsFlags;
XCLASS_DEFINE_BEGING(XEventLoop)
XCLASS_DEFINE_ENUM(XEventLoop, Exec) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XEventLoop, Quit),
XCLASS_DEFINE_ENUM(XEventLoop, WakeUp),
XCLASS_DEFINE_ENUM(XEventLoop, ProcessEvents),
XCLASS_DEFINE_ENUM(XEventLoop, HasPendingEvents),
XCLASS_DEFINE_END(XEventLoop)

/**
 * @brief 事件循环类，负责处理事件队列和定时器
 */
typedef struct XEventLoop
{
    XObject m_class;                  // 父类
    XEventDispatcher* m_dispatcher;   // 关联的事件调度器
    XCircularQueueAtomic* m_postQueue;//信号发送队列(引用XCoreApplication)
    XTimerGroupWheel* m_timerGroup;   // 定时器组(引用XCoreApplication)
    XEventLoopState m_state;          // 事件循环状态
    XWaitCondition* m_condition;      // 等待条件变量
    XMutex* m_mutex;                  // 互斥锁
    XAtomic_int32_t* m_ref_count;         // 引用计数：用于 Copy-On-Write 机制的资源管理
    int m_exitCode;                   // 退出代码
    bool m_quitOnLastWindowClosed;    // 当最后一个窗口关闭时退出
} XEventLoop;

/**
 * @brief 初始化事件循环类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventLoop_class_init();

/**
 * @brief 创建事件循环实例
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 * @return 新创建的事件循环实例
 */
XEventLoop* XEventLoop_create();

/**
 * @brief 初始化事件循环
 * @param loop 要初始化的事件循环
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 */
void XEventLoop_init(XEventLoop* loop);

/**
 * @brief 启动事件循环
 * @param loop 事件循环实例
 * @return 退出代码
 */
int XEventLoop_exec_base(XEventLoop* loop);
/**
 * @brief 退出事件循环
 * @param loop 事件循环实例
 * @param exitCode 退出代码
 */
void XEventLoop_quit_base(XEventLoop* loop, int exitCode);

/**
 * @brief 唤醒事件循环
 * @param loop 事件循环实例
 */
void XEventLoop_wakeUp_base(XEventLoop* loop);

/**
 * @brief 处理当前待处理的事件
 * @param loop 事件循环对象
 * @param flags 事件处理标志
 */
void XEventLoop_processEvents_base(XEventLoop* loop, XEventLoopProcessEventsFlags flags);

/**
 * @brief 检查是否有待处理的事件
 * @param loop 事件循环实例
 * @return 是否有待处理的事件
 */
bool XEventLoop_hasPendingEvents_base(XEventLoop* loop);
/**
 * @brief 投递信号发送（异步处理）
 * @param loop 事件循环调度器
 * @param sendSignalFunc 信号发送函数
 * @param signalSlot 信号槽
 * @param signal 信号
 * @param args 参数
 * @return 是否成功加入队列
 */
bool XEventLoop_postSendSignal(XEventLoop* loop, void(*sendFunc)(XSignalSlot*, size_t, void*), XSignalSlot* signalSlot, size_t signal, void* args, XAtomic_int32_t* ref_count, XEventPriority priority);
//投递函数(异步投递)
bool XEventLoop_postFunc(XEventLoop* loop, XObject* receiver, void(*func)(void*), void* args, XEventPriority priority);
/**
 * @brief 获取事件循环关联的调度器
 * @param loop 事件循环实例
 * @return 事件调度器
 */
XEventDispatcher* XEventLoop_getDispatcher(XEventLoop* loop);

/**
 * @brief 设置当最后一个窗口关闭时是否退出事件循环
 * @param loop 事件循环实例
 * @param quit 是否退出
 */
void XEventLoop_setQuitOnLastWindowClosed(XEventLoop* loop, bool quit);

/**
 * @brief 释放事件循环资源
 * @param loop 事件循环实例
 */
#define XEventLoop_delete_base    XObject_delete_base

#ifdef __cplusplus
}
#endif
#endif // XEVENTLOOP_H