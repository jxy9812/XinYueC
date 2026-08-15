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
 * @brief 事件处理标志（对标 Qt 6.8 QEventLoop::ProcessEventsFlag）
 */
typedef enum
{
    XEventLoop_AllEvents = 0x00,         // 处理所有事件
    XEventLoop_ExcludeUserInputEvents = 0x01,  // 排除用户输入事件（如鼠标/键盘操作）
    XEventLoop_ExcludeSocketNotifiers = 0x02,  // 排除套接字通知事件（如网络数据就绪）
    XEventLoop_WaitForMoreEvents = 0x04,// 等待更多事件（若无待处理事件则阻塞）
    XEventLoop_X11ExcludeTimers = 0x08,// X11环境下排除定时器事件
    XEventLoop_EventLoopExec = 0x20, // 事件循环执行态（基础事件循环运行标记）
    XEventLoop_DialogExec = 0x40,// 对话框执行态（模态对话框事件循环标记）
    XEventLoop_ApplicationExec = 0x80// 应用程序执行态（主程序事件循环标记）
} XEventLoopProcessEventsFlags;
XCLASS_DEFINE_BEGING(XEventLoop)
//XCLASS_DEFINE_ENUM(XEventLoop, Exec) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_EXTEND_END(XEventLoop, XObject)

/**
 * @brief 事件循环类，负责处理事件队列和定时器（对标 Qt 6.8 QEventLoop）
 */
typedef struct XEventLoop
{
    XObject m_class;                  // 父类
    XAbstractEventDispatcher* m_dispatcher;   // 关联的事件调度器
    XEventLoopState m_state;          // 事件循环状态
    //XWaitCondition* m_condition;      // 等待条件变量
    //XMutex* m_mutex;                  // 互斥锁
    XTimer* m_deley;
    int m_exitCode;                   // 退出代码（对标 QEventLoopPrivate::returnCode）
    bool inExec;                      // Qt 6.8: 防止重入 exec()（对标 QEventLoopPrivate::inExec）
    XAtomic_bool m_exit;              // 原子退出标志，允许其他线程安全调用 exit()
    XAtomic_int32_t m_returnCode;     // 原子返回码
} XEventLoop;

/**
 * @brief 初始化事件循环类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventLoop_class_init();

/**
 * @brief 创建事件循环实例
 * @return 新创建的事件循环实例
 */
XEventLoop* XEventLoop_create_ex(XMemoryType memory);

/**
 * @brief 初始化事件循环
 * @param loop 要初始化的事件循环
 */
void XEventLoop_init(XEventLoop* loop);

/**
 * @brief 启动事件循环（对标 Qt 6.8 QEventLoop::exec()）
 * @param loop 事件循环实例
 * @return 退出代码
 */
int XEventLoop_exec(XEventLoop* loop, XEventLoopProcessEventsFlags flags);

/**
 * @brief 退出事件循环（对标 Qt 6.8 QEventLoop::exit()）
 * @param loop 事件循环实例
 * @param exitCode 退出代码
 */
void XEventLoop_exit(XEventLoop* loop, int exitCode);

void XEventLoop_quit(XEventLoop* loop);

/**
 * @brief 判断事件循环是否正在运行（对标 Qt 6.8 QEventLoop::isRunning()）
 * @param loop 事件循环实例
 * @return true 表示正在运行
 */
bool XEventLoop_isRunning(XEventLoop* loop);

/**
 * @brief 唤醒事件循环（对标 Qt 6.8 QEventLoop::wakeUp()）
 * @param loop 事件循环实例
 */
void XEventLoop_wakeUp(XEventLoop* loop);

/**
 * @brief 处理当前待处理的事件（对标 Qt 6.8 QEventLoop::processEvents()）
 * @param loop 事件循环对象
 * @param flags 事件处理标志
 * @return true 表示处理了事件
 */
bool XEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags);

/**
 * @brief 释放事件循环资源
 * @param loop 事件循环实例
 */
#define XEventLoop_deleteLater    XObject_deleteLater
#define XEventLoop_deinitLater    XObject_deinitLater
//定时器延迟
void XEventLoop_delay(size_t msec);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XEventLoop_create
#define XEventLoop_create() XEventLoop_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XEVENTLOOP_H
