/**
 * @file XAbstractEventDispatcher.h
 * @brief 事件调度器抽象基类（仅新版高精度 API，对标 Qt 7+ QAbstractEventDispatcher）。
 *
 * 此类是所有平台特定事件调度器（如 epoll、kqueue、IOCP）的统一抽象接口。
 * 它负责协调事件循环、定时器、套接字通知器等核心功能。
 *
 * 设计严格遵循你的 C 多态风格：
 * - 继承自 XObject
 * - 虚函数通过 XVtable 实现
 * - 公共多态入口函数命名为 XXX_base
 * - 仅保留新版 Duration/TimerId API（纳秒级）
 */

#ifndef XABSTRACTEVENTDISPATCHER_H
#define XABSTRACTEVENTDISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"
#include "XEventLoop.h"
#include "XSocketNotifier.h"
#include "XTimer.h"
#include "XHashMap.h"
#include "XFileDescriptor.h"
#include "XConsoleShellConfig.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct XHrTimerGroup XHrTimerGroup;
typedef struct XAbstractNetIoRing XAbstractNetIoRing;
typedef struct XThreadData XThreadData;
/**
 * @brief 定时器信息结构体 (Windows 私有)
 */
typedef struct {
    XTimerType timerType;       ///< 定时器类型
    XTimerId timerId;           ///< 定时器 ID
    union
    {
        XHandle* m_handle;  ///定时器句柄
    };
    int64_t interval;           ///< 间隔 (纳秒)
} XAbstractEventDispatcher_TimerInfo;
// 前向声明
struct XAbstractNativeEventFilter;
/* XAbstractEventDispatcherPrivate 私有结构体及其 _init/_deinit 已迁至
 * XAbstractEventDispatcher_Protected.h，此处仅保留不透明前向 typedef（PIMPL）；
 * 需解引用 m_d_ptr 的内部实现须显式 include 该保护头文件。 */
typedef struct XAbstractEventDispatcherPrivate XAbstractEventDispatcherPrivate;
// ===================================================================
// === 核心类型定义（对标 Qt 7+） =====================================
// ===================================================================



/**
 * @brief 定时器信息结构体（高精度版本）。
 */
typedef struct XAbstractEventDispatcher_TimerInfoV2 {
    XDuration interval;   ///< 间隔（纳秒）
    XTimerId timerId;     ///< 定时器 ID
    XTimerType timerType; ///< 定时器类型
} XAbstractEventDispatcher_TimerInfoV2;

// ===================================================================
// === 虚函数表枚举定义 ===============================================
// ===================================================================

/**
 * @brief 开始定义 XAbstractEventDispatcher 类的虚函数表枚举。
 * @note 继承自 XObject 的虚函数表大小，确保索引不冲突。
 */
XCLASS_DEFINE_BEGING(XAbstractEventDispatcher)
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, ProcessEvents) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, RegisterSocketNotifier),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, UnregisterSocketNotifier),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, RegisterTimer),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, UnregisterTimer),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, UnregisterTimers),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, TimersForObject),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, RemainingTime),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, WakeUp),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, Interrupt),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, StartingUp),
XCLASS_DEFINE_ENUM(XAbstractEventDispatcher, ClosingDown),
XCLASS_DEFINE_END(XAbstractEventDispatcher)

// ===================================================================
// === 类结构体定义 ===================================================
// ===================================================================
/**
 * @brief XAbstractEventDispatcher 实例所属线程类型。
 * @details 记录在 XAbstractEventDispatcher 的 m_type 成员中，经
 *          XAbstractEventDispatcher_threadType 查询，用于区分主线程
 *          调度器与工作线程调度器。
 */
typedef enum
{
    XDispatcherThreadType_Worker,   ///< 子线程/工作线程
    XDispatcherThreadType_Main      ///< 主线程
} XDispatcherThreadType;
/**
 * @brief XAbstractEventDispatcher 类结构体。
 *
 * 所有平台事件调度器必须从此结构派生，并实现其虚函数。
 */
typedef struct XAbstractEventDispatcher 
{
    XObject m_class; ///< 第一个成员（继承自 XObject），由 XClass 管理，禁止手工修改
    XDispatcherThreadType m_type;
    // 私有数据（PIMPL）
    XAbstractEventDispatcherPrivate* m_d_ptr;
} XAbstractEventDispatcher;

// ===================================================================
// === 虚函数表初始化与类创建 =========================================
// ===================================================================

/**
 * @brief 初始化 XAbstractEventDispatcher 类的虚函数表。
 * @return 指向初始化完成的 XVtable 的指针。
 */
XVtable* XAbstractEventDispatcher_class_init(void);

/**
 * @brief 在堆上创建 XAbstractEventDispatcher 实例。
 * @note 此为抽象基类，不应直接实例化！应由子类（如 XEventDispatcherPOSIX）实现。
 * @param memory 内存分配器类型（XMALLOC_TYPE_*，如 XCLASS_DEFAULT_MEMORY_TYPE）。
 * @param parent 父对象（可为 NULL）。
 * @return 新创建的对象指针。
 */
XAbstractEventDispatcher* XAbstractEventDispatcher_create_ex(XMemoryType memory,  XObject* parent);

/**
 * @brief 初始化 XAbstractEventDispatcher 实例。
 * @param dispatcher 待初始化的对象指针。
 * @param parent 父对象。
 */
void XAbstractEventDispatcher_init(XAbstractEventDispatcher* dispatcher, XObject* parent);

// ===================================================================
// === 虚函数多态入口（带 _base 后缀） ================================
// ===================================================================

/**
 * @brief 处理待处理的事件（多态入口）。
 * @param self 调度器自身指针。
 * @param flags 事件处理标志。
 * @return true 如果至少处理了一个事件。
 */
bool XAbstractEventDispatcher_processEvents_base(XAbstractEventDispatcher* self, XEventLoopProcessEventsFlags flags);

/**
 * @brief 注册套接字通知器（多态入口）。
 * @param self 调度器自身指针。
 * @param notifier 要注册的通知器。
 */
void XAbstractEventDispatcher_registerSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier);

/**
 * @brief 注销套接字通知器（多态入口）。
 * @param self 调度器自身指针。
 * @param notifier 要注销的通知器。
 */
void XAbstractEventDispatcher_unregisterSocketNotifier_base(XAbstractEventDispatcher* self, XSocketNotifier* notifier);

/**
 * @brief 注册高精度定时器（多态入口）。
 * @param self 调度器自身指针。
 * @param timerId 定时器唯一 ID（非零）。
 * @param interval 间隔（纳秒）。
 * @param timerType 定时器类型。
 * @param object 所属对象。
 */
void XAbstractEventDispatcher_registerTimer_base(XAbstractEventDispatcher* self, XTimerId timerId, XDuration interval, XTimerType timerType, XObject* object);

/**
 * @brief 注销指定 ID 的定时器（多态入口）。
 * @param self 调度器自身指针。
 * @param timerId 定时器 ID。
 * @return true 成功注销。
 */
bool XAbstractEventDispatcher_unregisterTimer_base(XAbstractEventDispatcher* self, XTimerId timerId);

/**
 * @brief 注销某对象的所有定时器（多态入口）。
 * @param self 调度器自身指针。
 * @param object 对象指针。
 * @return true 如果至少注销了一个。
 */
bool XAbstractEventDispatcher_unregisterTimers_base(XAbstractEventDispatcher* self, XObject* object);

/**
 * @brief 注销指定 ID 的定时器，**并校验归属**（非虚公共入口）。
 * @details 对标 Qt 6.8 `QObject::killTimer` 的 `runningTimers.indexOf(id)` 检查
 *          （qobject.cpp:1926-1936）：定时器 id 属于**启动它的对象**，拿别人的 id
 *          来注销必须拒绝——否则误传 id 会静默杀掉另一个对象的定时器。
 *          X 的归属事实记在 `XFileDescriptor::object`（`XFd_alloc` 时由
 *          `XAbstractEventDispatcher_registerTimer` 写入），跨 moveToThread
 *          也保持不变（id 跟着对象走）。
 * @param self 调度器自身指针。
 * @param timerId 定时器 ID。
 * @param object 期望的归属对象。
 * @return true 归属匹配且注销请求已下发（含「已从本调度器摘除、待新线程重注册」
 *         的迁移态）；false 表示该 id 不是 `object` 的定时器（调用方应告警并
 *         保持原状），或参数非法。
 * @note 底层 `XDeviceTimer_cancel` 的返回值不参与本函数的结论：tick 已分离
 *       节点时它也返回 false，但定时器确实已停（见 `VXAbstractEventDispatcher_unregisterTimer`）。
 */
bool XAbstractEventDispatcher_unregisterTimerForObject(XAbstractEventDispatcher* self,
                                                       XTimerId timerId, XObject* object);

/**
 * @brief 摘除某对象的全部定时器，但**保留定时器 id**（非虚公共入口）。
 * @details 仅供 `QObject::moveToThread` 的 ThreadChange 迁移使用。对标 Qt 6.8
 *          `QObject::event` 的 ThreadChange 分支（qobject.cpp:1422-1443）：
 *          `timersForObject` 收集 → `unregisterTimers` 摘除 → 在**新线程**用
 *          显式 id `registerTimer(ti.timerId, ti.interval, ti.timerType, this)`
 *          重注册；Qt 原注释明写「do not to release our timer ids back to the
 *          pool (since the timer ids are moving to a new thread)」。X 的 timer id
 *          就是 `XFd` 槽位 ⇒「不归还」等于**不调用 `XFd_free`**：本函数取消
 *          底层设备定时器并释放 TimerInfo，但把 `XFD_TYPE_TIMER` 槽位与
 *          `XFd_object` 归属留在表里，供 `XAbstractEventDispatcher_registerTimer_base`
 *          以同一 id 重新布防。
 * @param self 调度器自身指针（对象**当前**所属线程的调度器）。
 * @param object 对象指针。
 * @return true 至少摘除并保留了一个定时器 id。
 * @warning 调用方必须保证「摘除后一定重注册」：id 不会归还池子，若既不重注册
 *          也不注销，该槽位会一直被占用（对象析构时
 *          `XAbstractEventDispatcher_unregisterTimers_base` 会回收它）。
 */
bool XAbstractEventDispatcher_detachTimers(XAbstractEventDispatcher* self, XObject* object);

/**
 * @brief 获取某对象注册的所有定时器信息（多态入口）。
 * @param self 调度器自身指针。
 * @param object 对象指针。
 * @return 列表指针（元素为 XAbstractEventDispatcher_TimerInfoV2*），调用者负责释放。
 */
XVector* XAbstractEventDispatcher_timersForObject_base(const XAbstractEventDispatcher* self, const XObject* object);

/**
 * @brief 获取指定定时器的剩余时间（多态入口）。
 * @param self 调度器自身指针。
 * @param timerId 定时器 ID。
 * @return 剩余时间（纳秒），若无效返回 -1。
 */
XDuration XAbstractEventDispatcher_remainingTime_base(const XAbstractEventDispatcher* self, XTimerId timerId);

/**
 * @brief 唤醒事件循环（多态入口）。
 * @param self 调度器自身指针。
 */
void XAbstractEventDispatcher_wakeUp_base(XAbstractEventDispatcher* self);

/**
 * @brief 中断事件循环（多态入口）。
 * @param self 调度器自身指针。
 */
void XAbstractEventDispatcher_interrupt_base(XAbstractEventDispatcher* self);

/**
 * @brief 事件循环启动回调（多态入口，默认空实现）。
 * @param self 调度器自身指针。
 */
void XAbstractEventDispatcher_startingUp_base(XAbstractEventDispatcher* self);

/**
 * @brief 事件循环关闭回调（多态入口，默认空实现）。
 * @param self 调度器自身指针。
 */
void XAbstractEventDispatcher_closingDown_base(XAbstractEventDispatcher* self);

/**
 * @brief 安装原生事件过滤器。
 * @param self 调度器自身指针。
 * @param filter 过滤器对象。
 */
void XAbstractEventDispatcher_installNativeEventFilter(XAbstractEventDispatcher* self, struct XAbstractNativeEventFilter* filter);

/**
 * @brief 移除原生事件过滤器。
 * @param self 调度器自身指针。
 * @param filter 过滤器对象。
 */
void XAbstractEventDispatcher_removeNativeEventFilter(XAbstractEventDispatcher* self, struct XAbstractNativeEventFilter* filter);

/**
 * @brief 过滤原生平台事件。
 * @param self 调度器自身指针。
 * @param eventType 事件类型字符串（如 "xcb_generic_event_t"）。
 * @param message 原生消息指针。
 * @param result 输出结果指针（可为 NULL）。
 * @return true 表示事件已被处理，不再传递。
 */
bool XAbstractEventDispatcher_filterNativeEvent(XAbstractEventDispatcher* self, const XByteArray* eventType, void* message, int64_t* result);

// ===================================================================
// === 公共非虚函数 API（不带 _base）==================================
// ===================================================================

/**
 * @brief 注册一个高精度定时器（自动分配 ID）。
 * @param self 调度器指针。
 * @param interval 间隔（纳秒）。
 * @param timerType 定时器类型。
 * @param object 所属对象。
 * @return 新分配的 XTimerId，失败返回 XTIMER_INVALID_ID。
 */
XTimerId XAbstractEventDispatcher_registerTimer(
    XAbstractEventDispatcher* self,
    XDuration interval,
    XTimerType timerType,
    XObject* object
);
/**
 * @brief 获取指定线程的事件调度器实例。
 * @param thread 线程指针（NULL 表示当前线程）。
 * @return 调度器指针。
 */
XAbstractEventDispatcher* XAbstractEventDispatcher_instance(XThread* thread);

/**
 * @brief  周期轮询回调类型。
 * @param  userData  注册时传入的用户数据。
 * @return 保留，当前应返回 true。
 */
typedef bool (*XAbstractEventDispatcher_PollCallback)(void* userData);

/**
 * @brief  向当前线程的事件调度器注册一个周期轮询回调。
 * @details 每次 processEvents 都会调用一次回调，用于 USB、串口等需周期处理的设备。
 * @param  callback  轮询回调，不能为 NULL。
 * @param  userData  回调用户数据，只借用。
 * @return 注册句柄；失败返回 NULL。
 */
XHandle XAbstractEventDispatcher_addPollCallback(
    XAbstractEventDispatcher_PollCallback callback, void* userData);

/**
 * @brief  注销周期轮询回调。
 * @param  handle  注册时返回的句柄。
 */
void XAbstractEventDispatcher_removePollCallback(XHandle handle);

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
struct XConsoleShell;

/** @brief 获取事件调度器托管的默认控制台 Shell（惰性创建）。 */
struct XConsoleShell* XAbstractEventDispatcher_consoleShell(XAbstractEventDispatcher* self);

/** @brief 运行事件调度器托管的默认控制台 Shell 所属事件循环。 */
int XAbstractEventDispatcher_runDefaultShell(XAbstractEventDispatcher* self);
#endif

// ===================================================================
// === 信号模拟（通过 XObject 信号槽） ================================
// ===================================================================

/**
 * @brief awake 信号（当事件循环被唤醒时发射）。
 */
void* XAbstractEventDispatcher_awake_signal(XAbstractEventDispatcher* self);

/**
 * @brief aboutToBlock 信号（当事件循环即将阻塞时发射）。
 */
void* XAbstractEventDispatcher_aboutToBlock_signal(XAbstractEventDispatcher* self);

XDispatcherThreadType XAbstractEventDispatcher_threadType(XAbstractEventDispatcher* self);
bool XAbstractEventDispatcher_isMainThread(XAbstractEventDispatcher* self);
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XAbstractEventDispatcher_create
#define XAbstractEventDispatcher_create(...) XAbstractEventDispatcher_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XABSTRACTEVENTDISPATCHER_H
