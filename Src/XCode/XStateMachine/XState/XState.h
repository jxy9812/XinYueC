#ifndef XSTATE_H
#define XSTATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractState.h"
#include "XAbstractTransition.h"
#include "XVector.h"

typedef struct XSignalTransition XSignalTransition;

/** @brief 子状态执行模式，对应 QState::ChildMode。 */
typedef enum XState_ChildMode {
    XState_ExclusiveStates, ///< 同一时刻只允许一个直接子状态活动。
    XState_ParallelStates   ///< 进入状态时同时进入全部子状态区域。
} XState_ChildMode;

/** @brief 状态属性恢复策略，对应 QState::RestorePolicy。 */
typedef enum XState_RestorePolicy {
    XState_DontRestoreProperties, ///< 离开状态时不恢复进入前的属性值。
    XState_RestoreProperties      ///< 离开状态时恢复进入前的属性值。
} XState_RestorePolicy;

XCLASS_DEFINE_BEGING(XState)
XCLASS_DEFINE_EXTEND_END(XState, XAbstractState);

/**
 * @brief 可包含子状态和转换的标准状态，对应 Qt 6.8 的 QState。
 */
typedef struct XState {
    XAbstractState m_class;          ///< 继承 XAbstractState。
    XVector* m_childStates;          ///< 直接子状态列表，元素类型为 XAbstractState*。
    XVector* m_transitions;          ///< 出站转换列表，元素类型为 XAbstractTransition*。
    XAbstractState* m_initialState;  ///< 互斥模式的初始子状态。
    XAbstractState* m_errorState;    ///< 本状态及其后代发生错误时进入的状态。
    XState_ChildMode m_childMode;    ///< 子状态模式。
} XState;

/**
 * @brief 初始化 XState 虚函数表。
 * @return XState 的共享虚函数表。
 */
XVtable* XState_class_init(void);
/**
 * @brief 创建无父状态的互斥状态。
 * @return 新状态；内存分配失败时返回 NULL。
 * @note 返回对象由调用者拥有，加入父状态后由父状态取得所有权。
 */
XState* XState_create(void);
/**
 * @brief 创建指定模式和父状态的状态。
 * @param childMode 子状态模式。
 * @param parent 父状态，可为 NULL。
 * @return 新状态，失败返回 NULL。
 * @note parent 非 NULL 时，新状态会立即加入父状态的子状态列表并转移所有权。
 */
XState* XState_create_ex(XState_ChildMode childMode, XState* parent);
/**
 * @brief 初始化无父状态的互斥状态。
 * @param state 调用者提供的未初始化存储。
 * @note 适用于栈对象或嵌入其他结构体的对象，不设置内存释放器。
 */
void XState_init(XState* state);
/**
 * @brief 初始化指定模式和父状态的状态。
 * @param state 调用者提供的未初始化存储。
 * @param childMode 互斥或并行子状态模式。
 * @param parent 父状态，可为 NULL；非 NULL 时取得该状态的所有权。
 */
void XState_init_ex(XState* state, XState_ChildMode childMode, XState* parent);

#define XState_delete_base XAbstractState_delete_base
#define XState_deinit_base XAbstractState_deinit_base

/**
 * @brief 获取本状态配置的错误状态。
 * @param state 状态实例。
 * @return 错误状态；未配置或 state 为 NULL 时返回 NULL。
 * @note 返回的是内部引用，不转移所有权。
 */
XAbstractState* XState_errorState(const XState* state);
/**
 * @brief 设置错误状态。
 * @param state 发生本状态或后代入口错误时的上下文状态。
 * @param errorState 发生错误后进入的状态；传入 NULL 表示清除。
 * @return 错误状态属于同一状态机或为 NULL 时返回 true。
 * @note errorState 不由 state 取得所有权，其生命周期仍由状态树管理。
 */
bool XState_setErrorState(XState* state, XAbstractState* errorState);

/**
 * @brief 添加出站转换并取得其所有权。
 * @param state 作为转换源的状态。
 * @param transition 待添加的转换。
 * @return 添加成功返回 true。
 */
bool XState_addTransition(XState* state, XAbstractTransition* transition);

/**
 * @brief 创建并添加信号转换。
 * @param state 转换的源状态，同时取得新转换的所有权。
 * @param sender 信号发送对象，不转移所有权且不可为 NULL。
 * @param signal 由 XSignal 取得的非零信号标识。
 * @param target 目标状态，不转移所有权且不可为 NULL。
 * @return 新建的 XSignalTransition，失败返回 NULL。
 */
XSignalTransition* XState_addTransition_2(XState* state, const XObject* sender, size_t signal, XAbstractState* target);

/**
 * @brief 创建并添加无条件转换。
 * @param state 转换的源状态，同时取得新转换的所有权。
 * @param target 目标状态，不转移所有权且不可为 NULL。
 * @return 新建的转换，失败返回 NULL。
 * @note 状态机在每个 macrostep 处理外部事件前优先选择无条件转换。
 */
XAbstractTransition* XState_addTransition_3(XState* state, XAbstractState* target);

/**
 * @brief 移除转换并释放状态对它的所有权。
 * @param state 当前拥有该转换的源状态。
 * @param transition 待移除的转换。
 * @return 找到且成功移除时返回 true，否则返回 false。
 * @note 该函数不会销毁转换。
 */
bool XState_removeTransition(XState* state, XAbstractTransition* transition);
/**
 * @brief 获取出站转换列表的只读内部引用。
 * @param state 状态实例。
 * @return 元素类型为 XAbstractTransition* 的内部 XVector；state 为 NULL 时返回 NULL。
 * @note 调用者不得修改或释放返回的容器和其中的转换。
 */
const XVector* XState_transitions_const(const XState* state);

/**
 * @brief 获取互斥状态的初始子状态。
 * @param state 状态实例。
 * @return 初始直接子状态；未设置或 state 为 NULL 时返回 NULL。
 */
XAbstractState* XState_initialState(const XState* state);
/**
 * @brief 设置互斥状态的初始直接子状态。
 * @param state 待配置的互斥状态。
 * @param initialState state 的直接子状态或历史状态；NULL 表示清除。
 * @return 参数有效时返回 true；并行状态拒绝设置初始状态。
 */
bool XState_setInitialState(XState* state, XAbstractState* initialState);

/**
 * @brief 获取子状态模式。
 * @param state 状态实例。
 * @return 互斥或并行模式；state 为 NULL 时返回互斥模式。
 */
XState_ChildMode XState_childMode(const XState* state);
/**
 * @brief 设置子状态模式。
 * @param state 待修改的状态。
 * @param mode 新的互斥或并行模式。
 * @note 切换到并行模式时会清除已经设置的初始状态。
 */
void XState_setChildMode(XState* state, XState_ChildMode mode);

/**
 * @brief 进入直接最终子状态时发出的 finished 信号。
 * @param state 已经完成的复合状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XState_finished_signal(XState* state);
/**
 * @brief 本状态完成进入处理后发出的 propertiesAssigned 信号。
 * @param state 已经完成进入处理的标准状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 * @note 当前项目未启用对象属性系统，因此该信号紧随 entered/activeChanged 发出。
 */
void* XState_propertiesAssigned_signal(XState* state);
/**
 * @brief 子状态模式变化信号。
 * @param state childMode 已发生变化的状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XState_childModeChanged_signal(XState* state);
/**
 * @brief 初始状态变化信号。
 * @param state initialState 已发生变化的状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XState_initialStateChanged_signal(XState* state);
/**
 * @brief 错误状态变化信号。
 * @param state errorState 已发生变化的状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XState_errorStateChanged_signal(XState* state);

#ifdef __cplusplus
}
#endif
#endif // XSTATE_H
