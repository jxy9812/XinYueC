#ifndef XABSTRACTSTATE_H
#define XABSTRACTSTATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"

typedef struct XState XState;
typedef struct XStateMachine XStateMachine;

/**
 * @brief 状态节点的内部种类。
 * @note 该枚举仅用于状态机算法区分标准状态、伪状态和根状态机。
 */
typedef enum XAbstractState_Kind {
    XAbstractState_AtomicState,   ///< 不包含子状态的内部原子状态种类。
    XAbstractState_StandardState, ///< 可包含子状态和转换的 XState。
    XAbstractState_FinalState,    ///< 表示父状态完成的 XFinalState。
    XAbstractState_HistoryState,  ///< 恢复历史配置的 XHistoryState 伪状态。
    XAbstractState_StateMachine   ///< 作为状态图根节点的 XStateMachine。
} XAbstractState_Kind;

XCLASS_DEFINE_BEGING(XAbstractState)
XCLASS_DEFINE_ENUM(XAbstractState, OnEntry) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XAbstractState, OnExit),
XCLASS_DEFINE_END(XAbstractState)

/**
 * @brief 所有状态节点的抽象基类，对应 Qt 6.8 的 QAbstractState。
 * @note 状态只能由所属状态机进入或退出，调用者不应直接修改活动标志。
 */
typedef struct XAbstractState {
    XObject m_class;                  ///< 继承 XObject，必须位于结构体第一位。
    XAbstractState_Kind m_kind;       ///< 状态节点种类。
    XState* m_parentState;            ///< 父状态，顶层状态的父状态为状态机根节点。
    XStateMachine* m_machine;         ///< 所属状态机，未加入状态图时为 NULL。
    bool m_active;                    ///< 当前是否位于状态机活动配置中。
} XAbstractState;

/**
 * @brief 初始化 XAbstractState 虚函数表。
 * @return XAbstractState 的共享虚函数表。
 */
XVtable* XAbstractState_class_init(void);

/**
 * @brief 初始化抽象状态的基类部分。
 * @param state 待初始化的状态。
 * @param kind 状态节点种类。
 * @param parent 父状态，可为 NULL。
 * @note 供状态子类构造函数调用，不直接创建抽象状态实例。
 */
void XAbstractState_init(XAbstractState* state, XAbstractState_Kind kind, XState* parent);

#define XAbstractState_delete_base XClass_delete_base
#define XAbstractState_deinit_base XClass_deinit_base

/**
 * @brief 获取父状态。
 * @param state 状态实例。
 * @return 父状态；无父状态时返回 NULL。
 */
XState* XAbstractState_parentState(const XAbstractState* state);

/**
 * @brief 获取状态所属的状态机。
 * @param state 状态实例。
 * @return 所属状态机；状态不在状态图中时返回 NULL。
 */
XStateMachine* XAbstractState_machine(const XAbstractState* state);

/**
 * @brief 查询状态是否活动。
 * @param state 状态实例。
 * @return 状态位于当前活动配置中时返回 true。
 */
bool XAbstractState_active(const XAbstractState* state);

/**
 * @brief 调用状态进入虚函数。
 * @param state 即将进入的状态。
 * @param event 导致进入状态的事件，可为 NULL。
 * @note 子类通过重载 EXAbstractState_OnEntry 实现进入动作。
 */
void XAbstractState_onEntry_base(XAbstractState* state, XEvent* event);

/**
 * @brief 调用状态退出虚函数。
 * @param state 即将退出的状态。
 * @param event 导致退出状态的事件，可为 NULL。
 * @note 子类通过重载 EXAbstractState_OnExit 实现退出动作。
 */
void XAbstractState_onExit_base(XAbstractState* state, XEvent* event);

/**
 * @brief 状态完成 onEntry 后发出的 entered 信号。
 * @param state 已经完成进入动作的状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 * @note 发出该信号时 active 属性尚未切换为 true，与 Qt 的时序一致。
 */
void* XAbstractState_entered_signal(XAbstractState* state);
/**
 * @brief 状态完成 onExit 后发出的 exited 信号。
 * @param state 已经完成退出动作的状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 * @note 发出该信号前 active 属性已经切换为 false。
 */
void* XAbstractState_exited_signal(XAbstractState* state);
/**
 * @brief 状态活动属性变化时发出的 activeChanged 信号。
 * @param state 活动属性发生变化的状态。
 * @param active 新的活动状态；true 表示已进入 configuration。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractState_activeChanged_signal(XAbstractState* state, bool active);

#ifdef __cplusplus
}
#endif
#endif // XABSTRACTSTATE_H
