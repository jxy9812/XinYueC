#ifndef XABSTRACTTRANSITION_H
#define XABSTRACTTRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"
#include "XVector.h"

typedef struct XAbstractState XAbstractState;
typedef struct XState XState;
typedef struct XStateMachine XStateMachine;

/**
 * @brief 转换的执行类型，对应 QAbstractTransition::TransitionType。
 */
typedef enum XAbstractTransition_TransitionType {
    XAbstractTransition_ExternalTransition, ///< 外部转换会退出并重新进入转换域。
    XAbstractTransition_InternalTransition  ///< 内部转换保留源复合状态自身。
} XAbstractTransition_TransitionType;

/** @brief 转换实现种类，供状态机注册外部事件源时使用。 */
typedef enum XAbstractTransition_Kind {
    XAbstractTransition_CustomTransition,     ///< 用户派生或无条件转换。
    XAbstractTransition_SignalTransition,     ///< XSignalTransition 信号转换。
    XAbstractTransition_EventTransition,      ///< XEventTransition 通用对象事件转换。
    XAbstractTransition_KeyEventTransition,   ///< XKeyEventTransition 键盘事件转换。
    XAbstractTransition_MouseEventTransition  ///< XMouseEventTransition 鼠标事件转换。
} XAbstractTransition_Kind;

XCLASS_DEFINE_BEGING(XAbstractTransition)
XCLASS_DEFINE_ENUM(XAbstractTransition, EventTest) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XAbstractTransition, OnTransition),
XCLASS_DEFINE_END(XAbstractTransition)

/**
 * @brief 所有状态转换的抽象基类，对应 Qt 6.8 的 QAbstractTransition。
 */
typedef struct XAbstractTransition {
    XObject m_class;                                      ///< 继承 XObject。
    XState* m_sourceState;                                ///< 源状态，由所属 XState 管理。
    XVector* m_targetStates;                              ///< 目标状态列表，元素类型为 XAbstractState*。
    XAbstractTransition_TransitionType m_transitionType;  ///< 内部或外部转换。
    XAbstractTransition_Kind m_kind;                      ///< 转换实现种类。
} XAbstractTransition;

/**
 * @brief 初始化 XAbstractTransition 虚函数表。
 * @return XAbstractTransition 的共享虚函数表。
 */
XVtable* XAbstractTransition_class_init(void);

/**
 * @brief 初始化抽象转换的基类部分。
 * @param transition 待初始化的转换。
 * @param sourceState 源状态，可为 NULL。
 * @note 供转换子类调用；传入源状态时，源状态取得转换所有权。
 */
void XAbstractTransition_init(XAbstractTransition* transition, XState* sourceState);

#define XAbstractTransition_delete_base XClass_delete_base
#define XAbstractTransition_deinit_base XClass_deinit_base

/**
 * @brief 获取转换的源状态。
 * @param transition 转换实例。
 * @return 所属源状态；转换尚未加入 XState 时返回 NULL。
 */
XState* XAbstractTransition_sourceState(const XAbstractTransition* transition);
/**
 * @brief 获取目标状态列表中的第一个状态。
 * @param transition 转换实例。
 * @return 第一个目标状态；无目标转换或 transition 为 NULL 时返回 NULL。
 * @note 多目标转换应使用 XAbstractTransition_targetStates_const 获取完整列表。
 */
XAbstractState* XAbstractTransition_targetState(const XAbstractTransition* transition);

/**
 * @brief 设置单个目标状态。
 * @param transition 转换实例。
 * @param target 目标状态；传入 NULL 会创建无目标转换。
 * @return 设置成功返回 true。
 */
bool XAbstractTransition_setTargetState(XAbstractTransition* transition, XAbstractState* target);

/**
 * @brief 获取目标状态列表的只读内部引用。
 * @param transition 转换实例。
 * @return 元素类型为 XAbstractState* 的 XVector，不得由调用者修改或释放。
 * @note transition 为 NULL 时返回 NULL；列表中的状态不转移所有权。
 */
const XVector* XAbstractTransition_targetStates_const(const XAbstractTransition* transition);

/**
 * @brief 设置多个目标状态。
 * @param transition 转换实例。
 * @param targets 元素类型必须为 XAbstractState*；NULL 等价于空目标列表。
 * @return 所有目标均有效且设置成功时返回 true。
 */
bool XAbstractTransition_setTargetStates(XAbstractTransition* transition, const XVector* targets);

/**
 * @brief 获取转换类型。
 * @param transition 转换实例。
 * @return 内部转换或外部转换；transition 为 NULL 时返回外部转换。
 */
XAbstractTransition_TransitionType XAbstractTransition_transitionType(const XAbstractTransition* transition);
/**
 * @brief 设置转换类型。
 * @param transition 转换实例。
 * @param type 新的内部或外部转换类型。
 * @note 内部转换仅在源为互斥复合状态且所有目标均为其后代时保留源状态。
 */
void XAbstractTransition_setTransitionType(XAbstractTransition* transition, XAbstractTransition_TransitionType type);
/**
 * @brief 获取转换所属的状态机。
 * @param transition 转换实例。
 * @return 源状态所属的状态机；转换尚未加入状态图时返回 NULL。
 */
XStateMachine* XAbstractTransition_machine(const XAbstractTransition* transition);

/**
 * @brief 调用事件匹配虚函数。
 * @param transition 待测试的转换。
 * @param event 当前 macrostep 正在处理的事件；无事件转换选择阶段为 None 事件。
 * @return 当前事件允许该转换时返回 true。
 * @note 通过 EXAbstractTransition_EventTest 调度到实际派生类实现。
 */
bool XAbstractTransition_eventTest_base(XAbstractTransition* transition, XEvent* event);

/**
 * @brief 调用转换动作虚函数。
 * @param transition 已经选中并正在执行的转换。
 * @param event 触发转换的事件；启动和无事件转换使用 None 事件。
 * @note 状态机在退出状态之后、进入目标状态之前调用该函数。
 */
void XAbstractTransition_onTransition_base(XAbstractTransition* transition, XEvent* event);

/**
 * @brief onTransition 执行完成后发出的 triggered 信号。
 * @param transition 已经完成动作的转换。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractTransition_triggered_signal(XAbstractTransition* transition);
/**
 * @brief 第一个目标状态发生变化时发出的 targetStateChanged 信号。
 * @param transition 目标状态发生变化的转换。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractTransition_targetStateChanged_signal(XAbstractTransition* transition);
/**
 * @brief 目标列表发生变化时发出的 targetStatesChanged 信号。
 * @param transition 目标列表发生变化的转换。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractTransition_targetStatesChanged_signal(XAbstractTransition* transition);

#ifdef __cplusplus
}
#endif
#endif // XABSTRACTTRANSITION_H
