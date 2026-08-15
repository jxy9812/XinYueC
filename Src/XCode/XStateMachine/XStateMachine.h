#ifndef XSTATEMACHINE_H
#define XSTATEMACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XState.h"

/** @brief 状态机事件优先级，对应 QStateMachine::EventPriority。 */
typedef enum XStateMachine_EventPriority {
    XStateMachine_NormalPriority, ///< 普通外部事件，按投递顺序处理。
    XStateMachine_HighPriority    ///< 高优先级内部事件，先于外部事件处理。
} XStateMachine_EventPriority;

/** @brief 状态图运行时错误，对应 QStateMachine::Error。 */
typedef enum XStateMachine_Error {
    XStateMachine_NoError, ///< 状态图没有错误。
    XStateMachine_NoInitialStateError, ///< 互斥复合状态缺少初始子状态。
    XStateMachine_NoDefaultStateInHistoryStateError, ///< 首次进入历史状态时缺少默认目标。
    XStateMachine_NoCommonAncestorForTransitionError, ///< 转换源和目标没有共同状态机祖先。
    XStateMachine_StateMachineChildModeSetToParallelError ///< 并行根状态机包含非法跨区域转换。
} XStateMachine_Error;

/**
 * @brief 状态机内部生命周期状态。
 * @note 公开代码应使用 XStateMachine_isRunning 查询，不依赖该枚举。
 */
typedef enum XStateMachine_PrivateState {
    XStateMachine_NotRunning, ///< 尚未启动或已经停止、完成。
    XStateMachine_Starting,   ///< start 已调用，等待事件循环建立初始配置。
    XStateMachine_Running     ///< 初始配置已经建立，能够处理事件。
} XStateMachine_PrivateState;

/**
 * @brief 信号转换使用的内部事件，对应 QStateMachine::SignalEvent。
 */
typedef struct XStateMachine_SignalEvent {
    XEvent m_class;          ///< 继承 XEvent。
    XObject* m_sender;       ///< 原始信号发送者，不取得所有权。
    size_t m_signal;         ///< 原始信号标识。
    XVarList* m_arguments;   ///< 信号参数的值拷贝，由事件拥有。
} XStateMachine_SignalEvent;

/**
 * @brief 对象事件转换使用的包装事件，对应 QStateMachine::WrappedEvent。
 */
typedef struct XStateMachine_WrappedEvent {
    XEvent m_class;    ///< 继承 XEvent。
    XObject* m_object; ///< 原始事件源，不取得所有权。
    XEvent* m_event;   ///< 原始事件的克隆，由包装事件拥有。
} XStateMachine_WrappedEvent;

XCLASS_DEFINE_BEGING(XStateMachine_SignalEvent)
XCLASS_DEFINE_EXTEND_END(XStateMachine_SignalEvent, XEvent)
XCLASS_DEFINE_BEGING(XStateMachine_WrappedEvent)
XCLASS_DEFINE_EXTEND_END(XStateMachine_WrappedEvent, XEvent)

/** @brief 延迟事件的内部记录。 */
typedef struct XStateMachine_DelayedEvent {
    int m_id;              ///< 对外返回的延迟事件编号。
    XTimerId m_timerId;    ///< XObject 定时器编号；零延迟时为 XTIMER_INVALID_ID。
    XEvent* m_event;       ///< 到期后进入外部队列的事件。
} XStateMachine_DelayedEvent;

XCLASS_DEFINE_BEGING(XStateMachine)
XCLASS_DEFINE_ENUM(XStateMachine, BeginSelectTransitions) = XCLASS_VTABLE_GET_SIZE(XState),
XCLASS_DEFINE_ENUM(XStateMachine, EndSelectTransitions),
XCLASS_DEFINE_ENUM(XStateMachine, BeginMicrostep),
XCLASS_DEFINE_ENUM(XStateMachine, EndMicrostep),
XCLASS_DEFINE_END(XStateMachine)

/**
 * @brief 分层有限状态机，对应 Qt 6.8 的 QStateMachine。
 * @note XStateMachine 继承 XState，本身作为状态图的隐式根状态。
 */
typedef struct XStateMachine {
    XState m_class;                              ///< 继承 XState。
    XVector* m_configuration;                    ///< 当前最大一致活动配置。
    XVector* m_internalEventQueue;               ///< 高优先级内部事件队列。
    XVector* m_externalEventQueue;               ///< 普通外部事件队列。
    XVector* m_delayedEvents;                     ///< 延迟事件记录列表。
    XVector* m_signalConnections;                 ///< 共享信号事件连接列表。
    XAbstractState* m_pendingErrorState;          ///< 下一 microstep 需要进入的错误状态。
    XStateMachine_PrivateState m_state;           ///< 生命周期状态。
    XStateMachine_Error m_error;                  ///< 最近一次状态图错误。
    const char* m_errorString;                    ///< 最近一次错误的静态描述。
    int m_nextDelayedEventId;                     ///< 下一个延迟事件编号。
    bool m_processing;                            ///< 当前是否正在执行 macrostep。
    bool m_processingScheduled;                   ///< 是否已投递处理事件。
    bool m_stopRequested;                         ///< 是否请求在安全点停止。
} XStateMachine;

/**
 * @brief 初始化 XStateMachine 虚函数表。
 * @return XStateMachine 的共享虚函数表。
 */
XVtable* XStateMachine_class_init(void);
/**
 * @brief 创建互斥根状态机。
 * @return 新状态机；内存分配失败时返回 NULL。
 * @note 调用 start 前必须使用 XState_setInitialState 配置顶层初始状态。
 */
/**
 * @brief 创建指定根子状态模式的状态机。
 * @param childMode 根状态的子状态模式。
 * @return 新状态机；内存分配失败时返回 NULL。
 * @warning Qt 允许构造并行根状态机，但跨根区域转换会报告
 * XStateMachine_StateMachineChildModeSetToParallelError。
 */
XStateMachine* XStateMachine_create_ex(XMemoryType memory, XState_ChildMode childMode);
/**
 * @brief 初始化互斥根状态机。
 * @param machine 调用者提供的未初始化存储。
 */
void XStateMachine_init(XStateMachine* machine);
/**
 * @brief 初始化指定根子状态模式的状态机。
 * @param machine 调用者提供的未初始化存储。
 * @param childMode 根状态的互斥或并行模式。
 */
void XStateMachine_init_ex(XStateMachine* machine, XState_ChildMode childMode);

#define XStateMachine_delete_base XState_delete_base
#define XStateMachine_deinit_base XState_deinit_base

/**
 * @brief 将状态添加为状态机的顶层状态并取得所有权。
 * @param machine 目标状态机。
 * @param state 待加入的状态；若属于其他父状态，会先从原父状态移除。
 * @return 添加成功返回 true。
 */
bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state);
/**
 * @brief 移除顶层状态并释放所有权。
 * @param machine 当前拥有该顶层状态的状态机。
 * @param state 待移除的顶层状态。
 * @return state 属于 machine 且当前不在活动配置中时返回 true。
 * @note 该函数不会销毁状态。
 */
bool XStateMachine_removeState(XStateMachine* machine, XAbstractState* state);

/**
 * @brief 获取最近一次状态图错误。
 * @param machine 状态机实例。
 * @return 最近错误码；machine 为 NULL 时返回 XStateMachine_NoError。
 */
XStateMachine_Error XStateMachine_error(const XStateMachine* machine);
/**
 * @brief 获取最近一次状态图错误描述。
 * @param machine 状态机实例。
 * @return 静态只读错误描述；无错误或 machine 为 NULL 时返回 NULL。
 */
const char* XStateMachine_errorString(const XStateMachine* machine);
/**
 * @brief 清除错误代码、错误描述和尚未处理的错误状态。
 * @param machine 状态机实例。
 * @note 该函数不会改变当前活动配置或运行状态。
 */
void XStateMachine_clearError(XStateMachine* machine);

/**
 * @brief 查询状态机是否已经完成异步启动并处于运行状态。
 * @param machine 状态机实例。
 * @return 生命周期状态为 Running 时返回 true。
 */
bool XStateMachine_isRunning(const XStateMachine* machine);
/**
 * @brief 异步启动状态机。
 * @param machine 待启动的状态机。
 * @note 初始配置在状态机所属线程的事件循环中建立。
 * @note 互斥根状态没有 initialState 时拒绝启动；重复启动不会重建配置。
 */
void XStateMachine_start(XStateMachine* machine);
/**
 * @brief 异步停止状态机。
 * @param machine 待停止的状态机。
 * @note 显式停止发出 stopped，不发出继承自 XState 的 finished。
 * @note 停止请求在当前 microstep 的安全点生效，活动配置不会执行退出动作。
 */
void XStateMachine_stop(XStateMachine* machine);
/**
 * @brief 根据 running 参数调用 start 或 stop。
 * @param machine 目标状态机。
 * @param running true 表示请求启动，false 表示请求停止。
 */
void XStateMachine_setRunning(XStateMachine* machine, bool running);

/**
 * @brief 投递状态机事件并转移事件所有权。
 * @param machine 接收事件的状态机。
 * @param event 堆上创建的事件，不可为 NULL。
 * @param priority 普通或高优先级；高优先级队列先于普通队列处理。
 * @return 状态机处于 Starting 或 Running 且投递成功时返回 true。
 * @note 返回 false 时事件所有权仍属于调用者。
 */
bool XStateMachine_postEvent(XStateMachine* machine, XEvent* event, XStateMachine_EventPriority priority);
/**
 * @brief 延迟投递普通优先级事件。
 * @param machine 接收事件的运行中状态机。
 * @param event 堆上创建的事件，不可为 NULL。
 * @param delayMs 非负延迟毫秒数。
 * @return 成功返回非负事件编号，失败返回 -1 且不取得事件所有权。
 * @note delayMs 为 0 时仍异步投递，下一轮应用事件循环才会处理事件。
 */
int XStateMachine_postDelayedEvent(XStateMachine* machine, XEvent* event, int delayMs);
/**
 * @brief 取消指定延迟事件并销毁其持有的事件对象。
 * @param machine 拥有延迟事件的运行中状态机。
 * @param id XStateMachine_postDelayedEvent 返回的事件编号。
 * @return 找到并取消事件时返回 true，否则返回 false。
 */
bool XStateMachine_cancelDelayedEvent(XStateMachine* machine, int id);
/**
 * @brief 获取当前活动配置的只读内部引用。
 * @param machine 状态机实例。
 * @return 元素类型为 XAbstractState* 的内部 XVector；machine 为 NULL 时返回 NULL。
 * @note 状态机本身不包含在配置中；父状态和最终状态包含在配置中。
 * @warning 返回容器随 microstep 改变，调用者不得修改、缓存元素或释放容器。
 */
const XVector* XStateMachine_configuration_const(const XStateMachine* machine);

/**
 * @brief 调用转换选择开始钩子。
 * @param machine 正在选择转换的状态机。
 * @param event 当前待匹配事件。
 * @note 通过 EXStateMachine_BeginSelectTransitions 调度派生实现。
 */
void XStateMachine_beginSelectTransitions_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用转换选择结束钩子。
 * @param machine 已完成转换选择的状态机。
 * @param event 当前待匹配事件。
 */
void XStateMachine_endSelectTransitions_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用 microstep 开始钩子。
 * @param machine 即将执行 microstep 的状态机。
 * @param event 触发本次 microstep 的事件。
 */
void XStateMachine_beginMicrostep_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用 microstep 结束钩子。
 * @param machine 已完成 microstep 的状态机。
 * @param event 触发本次 microstep 的事件。
 */
void XStateMachine_endMicrostep_base(XStateMachine* machine, XEvent* event);

/**
 * @brief 创建信号事件，并复制参数列表 data 区域中的值。
 * @param sender 原始信号发送者，不转移所有权。
 * @param signal 原始非零信号标识。
 * @param arguments 信号参数列表，可为 NULL。
 * @return 新信号事件；内存分配失败时返回 NULL。
 * @note 指针参数只复制指针值，不深拷贝指针指向的对象。
 */
XStateMachine_SignalEvent* XStateMachine_SignalEvent_create_ex(XMemoryType memory,  XObject* sender, size_t signal, const XVarList* arguments);
/**
 * @brief 获取信号事件的发送者。
 * @param event 信号事件。
 * @return 原始发送者；event 为 NULL 时返回 NULL。
 */
XObject* XStateMachine_SignalEvent_sender(const XStateMachine_SignalEvent* event);
/**
 * @brief 获取信号事件的信号标识。
 * @param event 信号事件。
 * @return 原始信号标识；event 为 NULL 时返回 0。
 */
size_t XStateMachine_SignalEvent_signal(const XStateMachine_SignalEvent* event);
/**
 * @brief 获取信号参数列表的只读内部引用。
 * @param event 信号事件。
 * @return 事件持有的参数列表；无参数或 event 为 NULL 时返回 NULL。
 * @warning 返回值随 event 一起销毁，不得修改或释放。
 */
const XVarList* XStateMachine_SignalEvent_arguments_const(const XStateMachine_SignalEvent* event);

/**
 * @brief 创建包装事件并取得传入事件的所有权。
 * @param object 原始事件源，不转移所有权。
 * @param event 原始事件的堆克隆，成功后由包装事件拥有。
 * @return 新包装事件；event 为 NULL 或分配失败时返回 NULL。
 * @note 创建失败时 event 所有权仍属于调用者。
 */
XStateMachine_WrappedEvent* XStateMachine_WrappedEvent_create_ex(XMemoryType memory,  XObject* object, XEvent* event);
/**
 * @brief 获取包装事件的原始事件源。
 * @param event 包装事件。
 * @return 原始事件源；event 为 NULL 时返回 NULL。
 */
XObject* XStateMachine_WrappedEvent_object(const XStateMachine_WrappedEvent* event);
/**
 * @brief 获取包装事件持有的原始事件克隆。
 * @param event 包装事件。
 * @return 内部事件克隆；event 为 NULL 时返回 NULL。
 * @warning 返回值由包装事件拥有，调用者不得释放。
 */
XEvent* XStateMachine_WrappedEvent_event(const XStateMachine_WrappedEvent* event);

/**
 * @brief 初始配置建立完成后发出的 started 信号。
 * @param machine 已经完成初始入口的状态机。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XStateMachine_started_signal(XStateMachine* machine);
/**
 * @brief 显式停止完成后发出的 stopped 信号。
 * @param machine 已经停止的状态机。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XStateMachine_stopped_signal(XStateMachine* machine);
/**
 * @brief 运行属性变化时发出的 runningChanged 信号。
 * @param machine 运行状态发生变化的状态机。
 * @param running 新运行状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XStateMachine_runningChanged_signal(XStateMachine* machine, bool running);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XStateMachine_create
#define XStateMachine_create() \
	XStateMachine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates)
#undef XStateMachine_SignalEvent_create
#define XStateMachine_SignalEvent_create(...) XStateMachine_SignalEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)
#undef XStateMachine_WrappedEvent_create
#define XStateMachine_WrappedEvent_create(...) XStateMachine_WrappedEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XSTATEMACHINE_H
