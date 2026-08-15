#ifndef XHISTORYSTATE_H
#define XHISTORYSTATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractState.h"

typedef struct XAbstractTransition XAbstractTransition;

/** @brief 历史记录深度，对应 QHistoryState::HistoryType。 */
typedef enum XHistoryState_HistoryType {
    XHistoryState_ShallowHistory, ///< 保存父状态退出前的直接活动子状态。
    XHistoryState_DeepHistory     ///< 保存父状态退出前的活动原子配置。
} XHistoryState_HistoryType;

XCLASS_DEFINE_BEGING(XHistoryState)
XCLASS_DEFINE_EXTEND_END(XHistoryState, XAbstractState);

/**
 * @brief 历史伪状态，对应 Qt 6.8 的 QHistoryState。
 * @note 历史状态本身不会进入活动配置，转换会被转发到已保存配置或默认转换目标。
 */
typedef struct XHistoryState {
    XAbstractState m_class;                   ///< 继承 XAbstractState。
    XAbstractTransition* m_defaultTransition; ///< 尚无历史记录时使用的默认转换。
    XHistoryState_HistoryType m_historyType;  ///< 浅历史或深历史。
    XVector* m_configuration;                 ///< 保存的状态配置，元素类型为 XAbstractState*。
} XHistoryState;

/**
 * @brief 初始化 XHistoryState 虚函数表。
 * @return XHistoryState 的共享虚函数表。
 */
XVtable* XHistoryState_class_init(void);
/**
 * @brief 创建无父状态的浅历史状态。
 * @return 新历史状态；内存分配失败时返回 NULL。
 */
/**
 * @brief 创建指定类型和父状态的历史状态。
 * @param type 浅历史或深历史。
 * @param parent 需要记录配置的父状态，可为 NULL。
 * @return 新历史状态；内存分配失败时返回 NULL。
 */
XHistoryState* XHistoryState_create_ex(XMemoryType memory, XHistoryState_HistoryType type, XState* parent);
/**
 * @brief 初始化无父状态的浅历史状态。
 * @param state 调用者提供的未初始化存储。
 */
void XHistoryState_init(XHistoryState* state);
/**
 * @brief 初始化指定类型和父状态的历史状态。
 * @param state 调用者提供的未初始化存储。
 * @param type 浅历史或深历史。
 * @param parent 需要记录配置的父状态，可为 NULL。
 */
void XHistoryState_init_ex(XHistoryState* state, XHistoryState_HistoryType type, XState* parent);

#define XHistoryState_delete_base XAbstractState_delete_base
#define XHistoryState_deinit_base XAbstractState_deinit_base

/**
 * @brief 获取无历史配置时使用的默认转换。
 * @param state 历史状态实例。
 * @return 默认转换；未设置或 state 为 NULL 时返回 NULL。
 * @note 返回内部引用，不转移所有权。
 */
XAbstractTransition* XHistoryState_defaultTransition(const XHistoryState* state);
/**
 * @brief 设置默认转换并取得其所有权。
 * @param state 历史状态实例。
 * @param transition 无历史记录时执行的转换；NULL 表示清除默认转换。
 * @return 设置成功返回 true。
 * @note 默认转换的 eventTest 不会被调用，其 onTransition 会收到触发历史入口的事件。
 */
bool XHistoryState_setDefaultTransition(XHistoryState* state, XAbstractTransition* transition);
/**
 * @brief 获取默认转换的第一个目标状态。
 * @param state 历史状态实例。
 * @return 默认目标状态；未设置默认转换时返回 NULL。
 */
XAbstractState* XHistoryState_defaultState(const XHistoryState* state);
/**
 * @brief 设置默认状态。
 * @param state 历史状态实例。
 * @param defaultState 历史状态的直接兄弟状态；NULL 表示空目标默认转换。
 * @return 默认状态是历史状态的直接兄弟状态或为 NULL 时返回 true。
 */
bool XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState);

/**
 * @brief 获取历史记录深度。
 * @param state 历史状态实例。
 * @return 浅历史或深历史；state 为 NULL 时返回浅历史。
 */
XHistoryState_HistoryType XHistoryState_historyType(const XHistoryState* state);
/**
 * @brief 设置历史记录深度。
 * @param state 历史状态实例。
 * @param type 新的浅历史或深历史类型。
 * @note 修改类型会清空已经保存的历史配置。
 */
void XHistoryState_setHistoryType(XHistoryState* state, XHistoryState_HistoryType type);

/**
 * @brief 默认转换变化信号。
 * @param state 默认转换发生变化的历史状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XHistoryState_defaultTransitionChanged_signal(XHistoryState* state);
/**
 * @brief 默认状态变化信号。
 * @param state 默认状态发生变化的历史状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XHistoryState_defaultStateChanged_signal(XHistoryState* state);
/**
 * @brief 历史记录深度变化信号。
 * @param state 历史类型发生变化的历史状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XHistoryState_historyTypeChanged_signal(XHistoryState* state);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XHistoryState_create
#define XHistoryState_create() \
	XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHistoryState_ShallowHistory, NULL)

#endif // XHISTORYSTATE_H
