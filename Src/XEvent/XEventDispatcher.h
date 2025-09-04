#ifndef XEVENTDISPATCHER_H
#define XEVENTDISPATCHER_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XClass.h"
#include "XQueueBase.h"
#include "XMapBase.h"
#include "XEvent.h"
#include "XListSLinked.h"
#include "XMutex.h"
#include "XEventLoop.h"

// 事件调度器虚函数表索引
XCLASS_DEFINE_BEGING(XEventDispatcher)
    EXEventDispatcher_SendEvent = XCLASS_VTABLE_GET_SIZE(XClass),        // 发送事件（同步）
    EXEventDispatcher_PostEvent,        // 投递事件（异步）
    EXEventDispatcher_AddEventCb,   // 添加事件过滤器
    EXEventDispatcher_RemoveEventCb,// 移除事件过滤器
    EXEventDispatcher_Handler,          // 处理事件
    EXEventDispatcher_RegisterTimer,    // 注册定时器
    EXEventDispatcher_UnregisterTimer,  // 注销定时器
    EXEventDispatcher_RegisterSocketNotifier, // 注册套接字通知器
    EXEventDispatcher_UnregisterSocketNotifier, // 注销套接字通知器
    EXEventDispatcher_WakeUp,           // 唤醒事件循环
    EXEventDispatcher_GetSupportedEvents,// 获取支持的事件类型
XCLASS_DEFINE_END(XEventDispatcher)

/**
 * @brief 套接字通知器类型
 */
typedef enum {
    XSocketNotifier_Read,
    XSocketNotifier_Write,
    XSocketNotifier_Exception
} XSocketNotifierType;

/**
 * @brief 套接字通知器结构
 */
typedef struct XSocketNotifier {
    int socket;                         // 套接字句柄
    XSocketNotifierType type;           // 通知类型
    XObject* receiver;                  // 接收对象
    int eventType;                      // 事件类型
    bool enabled;                       // 是否启用
} XSocketNotifier;

/**
 * @brief 事件调度器基类
 * 负责事件的发送、投递、过滤和处理，是事件循环的核心组件
 */
typedef struct XEventDispatcher {
    XClass m_parent;                    // 父类
    XCircularQueueAtomic* m_queues[XEVENT_PRIORITY_COUNT];       // 多个优先级的事件队列
    XMapBase* m_filter_cb;              // 事件过滤器映射表
    XListSLinked* m_socketNotifiers;    // 套接字通知器列表
    XMutex* m_mutex;                    // 互斥锁，保证线程安全
    XEventLoop* m_eventLoop;            // 关联的事件循环
} XEventDispatcher;

/**
 * @brief 初始化事件调度器类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventDispatcher_class_init();

/**
 * @brief 创建事件调度器实例
 * @param queueSize 事件队列大小
 * @return 新创建的事件调度器实例
 */
XEventDispatcher* XEventDispatcher_create(size_t queueSize);

/**
 * @brief 初始化事件调度器
 * @param dispatcher 要初始化的事件调度器
 * @param queueSize 事件队列大小
 */
void XEventDispatcher_init(XEventDispatcher* dispatcher, size_t queueSize);

/**
 * @brief 发送事件（同步处理）
 * @param dispatcher 事件调度器
 * @param event 要发送的事件
 * @return 事件是否被处理
 */
bool XEventDispatcher_sendEvent_base(XEventDispatcher* dispatcher, XEventMin* event);

/**
 * @brief 投递事件（异步处理）
 * @param dispatcher 事件调度器
 * @param event 要投递的事件
 * @return 事件是否成功加入队列
 */
bool XEventDispatcher_postEvent_base(XEventDispatcher* dispatcher, XEventMin* event, XEventPriority priority);

/**
 * @brief 添加事件过滤器
 * @param dispatcher 事件调度器
 * @param receiver 接收对象
 * @param code 事件类型
 * @param cb 事件回调函数
 * @param userData 用户数据
 * @return 是否添加成功
 */
bool XEventDispatcher_addEventCb_base(XEventDispatcher* dispatcher, XObject* receiver, int code, XEventCB cb, void* userData);

/**
 * @brief 移除事件过滤器
 * @param dispatcher 事件调度器
 * @param receiver 接收对象
 * @param code 事件类型
 * @return 是否移除成功
 */
bool XEventDispatcher_removeEventCb_base(XEventDispatcher* dispatcher, XObject* receiver, int code);

/**
 * @brief 处理事件队列中的事件
 * @param dispatcher 事件调度器
 */
void XEventDispatcher_handler_base(XEventDispatcher* dispatcher);

/**
 * @brief 注册定时器
 * @param dispatcher 事件调度器
 * @param timer 定时器对象
 * @param interval 时间间隔（毫秒）
 * @param singleShot 是否为单次定时器
 * @return 是否注册成功
 */
bool XEventDispatcher_registerTimer_base(XEventDispatcher* dispatcher, XTimerBase* timer, uint64_t interval, bool singleShot);

/**
 * @brief 注销定时器
 * @param dispatcher 事件调度器
 * @param timer 定时器对象
 * @return 是否注销成功
 */
bool XEventDispatcher_unregisterTimer_base(XEventDispatcher* dispatcher, XTimerBase* timer);

/**
 * @brief 注册套接字通知器
 * @param dispatcher 事件调度器
 * @param notifier 套接字通知器
 * @return 是否注册成功
 */
bool XEventDispatcher_registerSocketNotifier_base(XEventDispatcher* dispatcher, XSocketNotifier* notifier);

/**
 * @brief 注销套接字通知器
 * @param dispatcher 事件调度器
 * @param notifier 套接字通知器
 * @return 是否注销成功
 */
bool XEventDispatcher_unregisterSocketNotifier_base(XEventDispatcher* dispatcher, XSocketNotifier* notifier);

/**
 * @brief 唤醒事件循环
 * @param dispatcher 事件调度器
 */
void XEventDispatcher_wakeUp_base(XEventDispatcher* dispatcher);

/**
 * @brief 设置关联的事件循环
 * @param dispatcher 事件调度器
 * @param loop 事件循环
 */
void XEventDispatcher_setEventLoop(XEventDispatcher* dispatcher, XEventLoop* loop);

/**
 * @brief 获取关联的事件循环
 * @param dispatcher 事件调度器
 * @return 事件循环
 */
XEventLoop* XEventDispatcher_getEventLoop(XEventDispatcher* dispatcher);

/**
 * @brief 删除事件调度器
 * @param dispatcher 事件调度器
 */
#define XEventDispatcher_delete_base(dispatcher) XClass_delete_base((XClass*)dispatcher)

#ifdef __cplusplus
}
#endif
#endif